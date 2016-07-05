#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "opengl32.lib")

#pragma warning(disable:4996)

#include "Observer.h"

#include <cstdio>
#include <cstdlib>
#include <tchar.h>
#include <string>
#include <utility>
#include <functional>

#include <gl/GL.h>
#include <gl/GLU.h>
#include <Windows.h>
#include <Shlwapi.h>
#include "bass.h"
#include "bass_fx.h"
#include "Hooker.h"
#include "Server.h"

#define AUDIO_FILE_INFO_TOKEN "AudioFilename:"

std::shared_ptr<Observer> Observer::instance;
std::once_flag Observer::once_flag;

BOOL WINAPI Observer::ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
    Observer *instance = Observer::GetInstance();

    if (!instance->hookerReadFile.GetFunction()(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped))
    {
        return FALSE;
    }

    TCHAR szFilePath[MAX_PATH];
    DWORD nFilePathLength = GetFinalPathNameByHandle(hFile, szFilePath, MAX_PATH, VOLUME_NAME_DOS);
    //                  1: \\?\D:\Games\osu!\...
    DWORD dwFilePosition = SetFilePointer(hFile, 0, NULL, FILE_CURRENT) - (*lpNumberOfBytesRead);
    // ���� �д� ������ ��Ʈ�� �����̰� �պκ��� �о��ٸ� ���� ���� ��� ���:
    // AudioFilename�� �պκп� ���� / ���� �ڵ� �� ���� ���� �� �� ���� ����!
    if (wcsncmp(L".osu", &szFilePath[nFilePathLength - 4], 4) == 0 && dwFilePosition == 0)
    {
        // strtok�� �ҽ��� �����ϹǷ� �ϴ� ���
        // .osu ������ UTF-8(Multibyte) ���ڵ�
		
		/* �ٸ��� strtok���� �߶󳻼� AudioFilename: �� ã��. */
        char *buffer = strdup((const char*)(lpBuffer));

		for (char *line = strtok(buffer, "\n"); line != NULL; line = strtok(NULL, "\n"))
        {
            if (strnicmp(line, AUDIO_FILE_INFO_TOKEN, 14) != 0)
            {
                continue;
            }

            // AudioFilename �� ���
            TCHAR szAudioFileName[MAX_PATH];

            mbstowcs(szAudioFileName, &line[14], MAX_PATH);
            StrTrimW(szAudioFileName, L" \r");

            TCHAR szAudioFilePath[MAX_PATH];

			/* �պκ��� �̻��� ���ڸ� �����ϱ����� 4��° ���ں��� ����. */
            wcscpy(szAudioFilePath, &szFilePath[4]);
            PathRemoveFileSpecW(szAudioFilePath);
            PathCombineW(szAudioFilePath, szAudioFilePath, szAudioFileName);

			EnterCriticalSection(&instance->hCritiaclSection);

			instance->currentPlaying.audioPath = tstring(szAudioFilePath);
			/* �պκ��� �̻��� ���ڸ� �����ϱ����� 4��° ���ں��� ����. */
			instance->currentPlaying.beatmapPath = (tstring(&szFilePath[4]));

			LeaveCriticalSection(&instance->hCritiaclSection);

            break;
        }

        free(buffer);
    }
    return TRUE;
}


inline long long GetCurrentSysTime()
{
    long long t;
    GetSystemTimeAsFileTime(reinterpret_cast<LPFILETIME>(&t));
    return t;
}

BOOL WINAPI Observer::BASS_ChannelPlay(DWORD handle, BOOL restart)
{
    Observer *instance = Observer::GetInstance();

    if (!instance->hookerBASS_ChannelPlay.GetFunction()(handle, restart))
    {
        return FALSE;
    }

    BASS_CHANNELINFO info;
    BASS_ChannelGetInfo(handle, &info);

    if (info.ctype & BASS_CTYPE_STREAM)
    {
        double currentTimePos = BASS_ChannelBytes2Seconds(handle, BASS_ChannelGetPosition(handle, BASS_POS_BYTE));
        float tempo; BASS_ChannelGetAttribute(handle, BASS_ATTRIB_TEMPO, &tempo);
        instance->SendTempoInfomation(GetCurrentSysTime(), currentTimePos, tempo);
    }
    return TRUE;
}

BOOL WINAPI Observer::BASS_ChannelSetPosition(DWORD handle, QWORD pos, DWORD mode)
{
    Observer *instance = Observer::GetInstance();
    if (!instance->hookerBASS_ChannelSetPosition.GetFunction()(handle, pos, mode))
    {
        return FALSE;
    }

    BASS_CHANNELINFO info;
    BASS_ChannelGetInfo(handle, &info);

    if (info.ctype & BASS_CTYPE_STREAM)
    {
        double currentTime = BASS_ChannelBytes2Seconds(handle, pos);
        float CurrentTempo; BASS_ChannelGetAttribute(handle, BASS_ATTRIB_TEMPO, &CurrentTempo);
        // ����!! pos�� ���� ������ ��,
        // ����ϸ� BASS_ChannelPlay��� �� �Լ��� ȣ��ǰ�,
        // BASS_ChannelIsActive ���� BASS_ACTIVE_PAUSED��.
        if (BASS_ChannelIsActive(handle) == BASS_ACTIVE_PAUSED)
        {
            CurrentTempo = -100;
        }

        instance->SendTempoInfomation(GetCurrentSysTime(), currentTime, CurrentTempo);
    }
    return TRUE;
}

BOOL WINAPI Observer::BASS_ChannelSetAttribute(DWORD handle, DWORD attrib, float value)
{
    Observer *instance = Observer::GetInstance();
    if (!instance->hookerBASS_ChannelSetAttribute.GetFunction()(handle, attrib, value))
    {
        return FALSE;
    }

    BASS_CHANNELINFO info;
    BASS_ChannelGetInfo(handle, &info);

    if ((info.ctype & BASS_CTYPE_STREAM) && attrib == BASS_ATTRIB_TEMPO)
    {
        double currentTime = BASS_ChannelBytes2Seconds(handle, BASS_ChannelGetPosition(handle, BASS_POS_BYTE));
        instance->SendTempoInfomation(GetCurrentSysTime(), currentTime, value);
    }
    return TRUE;
}

BOOL WINAPI Observer::BASS_ChannelPause(DWORD handle)
{
    Observer *instance = Observer::GetInstance();
    if (!instance->hookerBASS_ChannelPause.GetFunction()(handle))
    {
        return FALSE;
    }

    BASS_CHANNELINFO info;
    BASS_ChannelGetInfo(handle, &info);
    if (info.ctype & BASS_CTYPE_STREAM)
    {
        double currentTime = BASS_ChannelBytes2Seconds(handle, BASS_ChannelGetPosition(handle, BASS_POS_BYTE));
        instance->SendTempoInfomation(GetCurrentSysTime(), currentTime, -100);
    }
    return TRUE;
}

BOOL WINAPI Observer::wglSwapBuffers(HDC context)
{
	Observer *instance = Observer::GetInstance();

	HWND hwnd;
	RECT rect;

	tstring lyrics = instance->GetLyrics();

	hwnd = WindowFromDC(context);
	GetClientRect(hwnd, &rect);

	int result = DrawTextW(context, lyrics.c_str(), lyrics.length(), &rect, DT_CENTER);

	if (!result) throw result;

	return instance->hooker_wglSwapBuffers.GetFunction()(context);
}

void Observer::SendTempoInfomation(long long calledAt, double currentTime, float tempo)
{
    TCHAR message[Server::nMessageLength];

    Observer *instance = Observer::GetInstance();

	/* Get Current Playing */
    EnterCriticalSection(&instance->hCritiaclSection);
    swprintf(message, L"%llx|%s|%lf|%f|%s\n", 
		calledAt, 
		instance->currentPlaying.audioPath.c_str(),
		currentTime, 
		tempo, 
		instance->currentPlaying.beatmapPath.c_str());
	LeaveCriticalSection(&instance->hCritiaclSection);

    Server::GetInstance()->PushMessage(message);
}

void Observer::Initalize()
{
    this->hookerReadFile.Hook();

    this->hookerBASS_ChannelPlay.Hook();
    this->hookerBASS_ChannelSetPosition.Hook();
    this->hookerBASS_ChannelSetAttribute.Hook();
    this->hookerBASS_ChannelPause.Hook();
	this->hooker_wglSwapBuffers.Hook();
}

void Observer::Release()
{
	this->hooker_wglSwapBuffers.Unhook();

    this->hookerBASS_ChannelPause.Unhook();
    this->hookerBASS_ChannelSetAttribute.Unhook();
    this->hookerBASS_ChannelSetPosition.Unhook();
    this->hookerBASS_ChannelPlay.Unhook();

    this->hookerReadFile.Unhook();
}
