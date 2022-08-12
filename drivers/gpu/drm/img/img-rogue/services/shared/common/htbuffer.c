/*************************************************************************/ /*!
@File           htbuffer.c
@Title          Host Trace Buffer shared API.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Host Trace Buffer provides a mechanism to log Host events to a
                buffer in a similar way to the Firmware Trace mechanism.
                Host Trace Buffer logs data using a Transport Layer buffer.
                The Transport Layer and pvrtld tool provides the mechanism to
                retrieve the trace data.
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#if defined(__linux__)
 #include <linux/version.h>

 #if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
  #include <linux/stdarg.h>
 #else
  #include <stdarg.h>
 #endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0) */
#else
 #include <stdarg.h>
#endif /* __linux__ */

#include "htbuffer.h"
#include "osfunc.h"
#include "client_htbuffer_bridge.h"

/* The group flags array of ints large enough to store all the group flags
 * NB: This will only work while all logging is in the kernel
 */
IMG_INTERNAL HTB_FLAG_EL_T g_auiHTBGroupEnable[HTB_FLAG_NUM_EL] = {0};


/*************************************************************************/ /*!
 @Function      HTBControl
 @Description   Update the configuration of the Host Trace Buffer
 @Input         hSrvHandle      Server Handle
 @Input         ui32NumFlagGroups Number of group enable flags words
 @Input         aui32GroupEnable  Flags words controlling groups to be logged
 @Input         ui32LogLevel    Log level to record
 @Input         ui32EnablePID   PID to enable logging for a specific process
 @Input         eLogPidMode     Enable logging for all or specific processes,
 @Input         eOpMode         Control what trace data is dropped if the TL
                                buffer is full
 @Return        eError          Internal services call returned eError error
                                number
*/ /**************************************************************************/
IMG_INTERNAL PVRSRV_ERROR
HTBControl(
	IMG_HANDLE hSrvHandle,
	IMG_UINT32 ui32NumFlagGroups,
	IMG_UINT32 * aui32GroupEnable,
	IMG_UINT32 ui32LogLevel,
	IMG_UINT32 ui32EnablePID,
	HTB_LOGMODE_CTRL eLogPidMode,
	HTB_OPMODE_CTRL eOpMode
)
{
	return BridgeHTBControl(
			hSrvHandle,
			ui32NumFlagGroups,
			aui32GroupEnable,
			ui32LogLevel,
			ui32EnablePID,
			eLogPidMode,
			eOpMode
			);
}


/*************************************************************************/ /*!
*/ /**************************************************************************/
static PVRSRV_ERROR
_HTBLog(IMG_HANDLE hSrvHandle, IMG_UINT32 PID, IMG_UINT32 TID, IMG_UINT64 ui64TimeStampus,
		HTB_LOG_SFids SF, va_list args)
{
#if defined(__KERNEL__)
	IMG_UINT32 i;
	IMG_UINT32 ui32NumArgs = HTB_SF_PARAMNUM(SF);
#if defined(__KLOCWORK__)
	IMG_UINT32 aui32Args[HTB_LOG_MAX_PARAMS + 1];	// Prevent KW False-positive
#else
	IMG_UINT32 aui32Args[HTB_LOG_MAX_PARAMS];
#endif

	PVR_ASSERT(ui32NumArgs <= HTB_LOG_MAX_PARAMS);
	ui32NumArgs = (ui32NumArgs>HTB_LOG_MAX_PARAMS) ?
			HTB_LOG_MAX_PARAMS : ui32NumArgs;

	/* unpack var args before sending over bridge */
	for (i=0; i<ui32NumArgs; i++)
	{
		aui32Args[i] = va_arg(args, IMG_UINT32);
	}

	return BridgeHTBLog(hSrvHandle, PID, TID, ui64TimeStampus, SF,
			ui32NumArgs, aui32Args);
#else
	PVR_UNREFERENCED_PARAMETER(hSrvHandle);
	PVR_UNREFERENCED_PARAMETER(PID);
	PVR_UNREFERENCED_PARAMETER(TID);
	PVR_UNREFERENCED_PARAMETER(ui64TimeStampus);
	PVR_UNREFERENCED_PARAMETER(SF);
	PVR_UNREFERENCED_PARAMETER(args);

	PVR_ASSERT(0=="HTB Logging in UM is not yet supported");
	return PVRSRV_ERROR_NOT_SUPPORTED;
#endif
}


/*************************************************************************/ /*!
 @Function      HTBLog
 @Description   Record a Host Trace Buffer log event
 @Input         PID     The PID of the process the event is associated
                        with. This is provided as an argument rather
                        than querying internally so that events
                        associated with a particular process, but
                        performed by another can be logged correctly.
 @Input         ui64TimeStampus The timestamp to be associated with this
                                log event
 @Input         SF              The log event ID
 @Input         ...             Log parameters
 @Return        PVRSRV_OK       Success.
*/ /**************************************************************************/
IMG_INTERNAL PVRSRV_ERROR
HTBLog(IMG_HANDLE hSrvHandle, IMG_UINT32 PID, IMG_UINT32 TID, IMG_UINT64 ui64TimeStampns,
		IMG_UINT32 SF, ...)
{
	PVRSRV_ERROR eError;
	va_list args;
	va_start(args, SF);
	eError =_HTBLog(hSrvHandle, PID, TID, ui64TimeStampns, SF, args);
	va_end(args);
	return eError;
}


/*************************************************************************/ /*!
 @Function      HTBLogSimple
 @Description   Record a Host Trace Buffer log event with implicit PID and
                Timestamp
 @Input         SF              The log event ID
 @Input         ...             Log parameters
 @Return        PVRSRV_OK       Success.
*/ /**************************************************************************/
IMG_INTERNAL PVRSRV_ERROR
HTBLogSimple(IMG_HANDLE hSrvHandle, IMG_UINT32 SF, ...)
{
	PVRSRV_ERROR eError;
	IMG_UINT64 ui64Timestamp;
	va_list args;
	va_start(args, SF);
	OSClockMonotonicns64(&ui64Timestamp);
	eError = _HTBLog(hSrvHandle, OSGetCurrentProcessID(), OSGetCurrentThreadID(), ui64Timestamp,
			SF, args);
	va_end(args);
	return eError;
}

/* EOF */
