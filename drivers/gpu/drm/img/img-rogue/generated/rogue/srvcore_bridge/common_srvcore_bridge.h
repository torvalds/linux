/*******************************************************************************
@File
@Title          Common bridge header for srvcore
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures used by both the client
                and server side of the bridge for srvcore
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
*******************************************************************************/

#ifndef COMMON_SRVCORE_BRIDGE_H
#define COMMON_SRVCORE_BRIDGE_H

#include <powervr/mem_types.h>

#include "img_defs.h"
#include "img_types.h"
#include "pvrsrv_error.h"

#include "pvrsrv_device_types.h"
#include "cache_ops.h"

#define PVRSRV_BRIDGE_SRVCORE_CMD_FIRST			0
#define PVRSRV_BRIDGE_SRVCORE_CONNECT			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+0
#define PVRSRV_BRIDGE_SRVCORE_DISCONNECT			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+1
#define PVRSRV_BRIDGE_SRVCORE_ACQUIREGLOBALEVENTOBJECT			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+2
#define PVRSRV_BRIDGE_SRVCORE_RELEASEGLOBALEVENTOBJECT			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+3
#define PVRSRV_BRIDGE_SRVCORE_EVENTOBJECTOPEN			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+4
#define PVRSRV_BRIDGE_SRVCORE_EVENTOBJECTWAIT			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+5
#define PVRSRV_BRIDGE_SRVCORE_EVENTOBJECTCLOSE			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+6
#define PVRSRV_BRIDGE_SRVCORE_DUMPDEBUGINFO			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+7
#define PVRSRV_BRIDGE_SRVCORE_GETDEVCLOCKSPEED			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+8
#define PVRSRV_BRIDGE_SRVCORE_HWOPTIMEOUT			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+9
#define PVRSRV_BRIDGE_SRVCORE_ALIGNMENTCHECK			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+10
#define PVRSRV_BRIDGE_SRVCORE_GETDEVICESTATUS			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+11
#define PVRSRV_BRIDGE_SRVCORE_GETMULTICOREINFO			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+12
#define PVRSRV_BRIDGE_SRVCORE_EVENTOBJECTWAITTIMEOUT			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+13
#define PVRSRV_BRIDGE_SRVCORE_FINDPROCESSMEMSTATS			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+14
#define PVRSRV_BRIDGE_SRVCORE_ACQUIREINFOPAGE			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+15
#define PVRSRV_BRIDGE_SRVCORE_RELEASEINFOPAGE			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+16
#define PVRSRV_BRIDGE_SRVCORE_CMD_LAST			(PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+16)

/*******************************************
            Connect
 *******************************************/

/* Bridge in structure for Connect */
typedef struct PVRSRV_BRIDGE_IN_CONNECT_TAG
{
	IMG_UINT32 ui32ClientBuildOptions;
	IMG_UINT32 ui32ClientDDKBuild;
	IMG_UINT32 ui32ClientDDKVersion;
	IMG_UINT32 ui32Flags;
} __packed PVRSRV_BRIDGE_IN_CONNECT;

/* Bridge out structure for Connect */
typedef struct PVRSRV_BRIDGE_OUT_CONNECT_TAG
{
	IMG_UINT64 ui64PackedBvnc;
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32CapabilityFlags;
	IMG_UINT8 ui8KernelArch;
} __packed PVRSRV_BRIDGE_OUT_CONNECT;

/*******************************************
            Disconnect
 *******************************************/

/* Bridge in structure for Disconnect */
typedef struct PVRSRV_BRIDGE_IN_DISCONNECT_TAG
{
	IMG_UINT32 ui32EmptyStructPlaceholder;
} __packed PVRSRV_BRIDGE_IN_DISCONNECT;

/* Bridge out structure for Disconnect */
typedef struct PVRSRV_BRIDGE_OUT_DISCONNECT_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_DISCONNECT;

/*******************************************
            AcquireGlobalEventObject
 *******************************************/

/* Bridge in structure for AcquireGlobalEventObject */
typedef struct PVRSRV_BRIDGE_IN_ACQUIREGLOBALEVENTOBJECT_TAG
{
	IMG_UINT32 ui32EmptyStructPlaceholder;
} __packed PVRSRV_BRIDGE_IN_ACQUIREGLOBALEVENTOBJECT;

/* Bridge out structure for AcquireGlobalEventObject */
typedef struct PVRSRV_BRIDGE_OUT_ACQUIREGLOBALEVENTOBJECT_TAG
{
	IMG_HANDLE hGlobalEventObject;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_ACQUIREGLOBALEVENTOBJECT;

/*******************************************
            ReleaseGlobalEventObject
 *******************************************/

/* Bridge in structure for ReleaseGlobalEventObject */
typedef struct PVRSRV_BRIDGE_IN_RELEASEGLOBALEVENTOBJECT_TAG
{
	IMG_HANDLE hGlobalEventObject;
} __packed PVRSRV_BRIDGE_IN_RELEASEGLOBALEVENTOBJECT;

/* Bridge out structure for ReleaseGlobalEventObject */
typedef struct PVRSRV_BRIDGE_OUT_RELEASEGLOBALEVENTOBJECT_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RELEASEGLOBALEVENTOBJECT;

/*******************************************
            EventObjectOpen
 *******************************************/

/* Bridge in structure for EventObjectOpen */
typedef struct PVRSRV_BRIDGE_IN_EVENTOBJECTOPEN_TAG
{
	IMG_HANDLE hEventObject;
} __packed PVRSRV_BRIDGE_IN_EVENTOBJECTOPEN;

/* Bridge out structure for EventObjectOpen */
typedef struct PVRSRV_BRIDGE_OUT_EVENTOBJECTOPEN_TAG
{
	IMG_HANDLE hOSEvent;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_EVENTOBJECTOPEN;

/*******************************************
            EventObjectWait
 *******************************************/

/* Bridge in structure for EventObjectWait */
typedef struct PVRSRV_BRIDGE_IN_EVENTOBJECTWAIT_TAG
{
	IMG_HANDLE hOSEventKM;
} __packed PVRSRV_BRIDGE_IN_EVENTOBJECTWAIT;

/* Bridge out structure for EventObjectWait */
typedef struct PVRSRV_BRIDGE_OUT_EVENTOBJECTWAIT_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_EVENTOBJECTWAIT;

/*******************************************
            EventObjectClose
 *******************************************/

/* Bridge in structure for EventObjectClose */
typedef struct PVRSRV_BRIDGE_IN_EVENTOBJECTCLOSE_TAG
{
	IMG_HANDLE hOSEventKM;
} __packed PVRSRV_BRIDGE_IN_EVENTOBJECTCLOSE;

/* Bridge out structure for EventObjectClose */
typedef struct PVRSRV_BRIDGE_OUT_EVENTOBJECTCLOSE_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_EVENTOBJECTCLOSE;

/*******************************************
            DumpDebugInfo
 *******************************************/

/* Bridge in structure for DumpDebugInfo */
typedef struct PVRSRV_BRIDGE_IN_DUMPDEBUGINFO_TAG
{
	IMG_UINT32 ui32VerbLevel;
} __packed PVRSRV_BRIDGE_IN_DUMPDEBUGINFO;

/* Bridge out structure for DumpDebugInfo */
typedef struct PVRSRV_BRIDGE_OUT_DUMPDEBUGINFO_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_DUMPDEBUGINFO;

/*******************************************
            GetDevClockSpeed
 *******************************************/

/* Bridge in structure for GetDevClockSpeed */
typedef struct PVRSRV_BRIDGE_IN_GETDEVCLOCKSPEED_TAG
{
	IMG_UINT32 ui32EmptyStructPlaceholder;
} __packed PVRSRV_BRIDGE_IN_GETDEVCLOCKSPEED;

/* Bridge out structure for GetDevClockSpeed */
typedef struct PVRSRV_BRIDGE_OUT_GETDEVCLOCKSPEED_TAG
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32ClockSpeed;
} __packed PVRSRV_BRIDGE_OUT_GETDEVCLOCKSPEED;

/*******************************************
            HWOpTimeout
 *******************************************/

/* Bridge in structure for HWOpTimeout */
typedef struct PVRSRV_BRIDGE_IN_HWOPTIMEOUT_TAG
{
	IMG_UINT32 ui32EmptyStructPlaceholder;
} __packed PVRSRV_BRIDGE_IN_HWOPTIMEOUT;

/* Bridge out structure for HWOpTimeout */
typedef struct PVRSRV_BRIDGE_OUT_HWOPTIMEOUT_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_HWOPTIMEOUT;

/*******************************************
            AlignmentCheck
 *******************************************/

/* Bridge in structure for AlignmentCheck */
typedef struct PVRSRV_BRIDGE_IN_ALIGNMENTCHECK_TAG
{
	IMG_UINT32 *pui32AlignChecks;
	IMG_UINT32 ui32AlignChecksSize;
} __packed PVRSRV_BRIDGE_IN_ALIGNMENTCHECK;

/* Bridge out structure for AlignmentCheck */
typedef struct PVRSRV_BRIDGE_OUT_ALIGNMENTCHECK_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_ALIGNMENTCHECK;

/*******************************************
            GetDeviceStatus
 *******************************************/

/* Bridge in structure for GetDeviceStatus */
typedef struct PVRSRV_BRIDGE_IN_GETDEVICESTATUS_TAG
{
	IMG_UINT32 ui32EmptyStructPlaceholder;
} __packed PVRSRV_BRIDGE_IN_GETDEVICESTATUS;

/* Bridge out structure for GetDeviceStatus */
typedef struct PVRSRV_BRIDGE_OUT_GETDEVICESTATUS_TAG
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32DeviceSatus;
} __packed PVRSRV_BRIDGE_OUT_GETDEVICESTATUS;

/*******************************************
            GetMultiCoreInfo
 *******************************************/

/* Bridge in structure for GetMultiCoreInfo */
typedef struct PVRSRV_BRIDGE_IN_GETMULTICOREINFO_TAG
{
	IMG_UINT64 *pui64Caps;
	IMG_UINT32 ui32CapsSize;
} __packed PVRSRV_BRIDGE_IN_GETMULTICOREINFO;

/* Bridge out structure for GetMultiCoreInfo */
typedef struct PVRSRV_BRIDGE_OUT_GETMULTICOREINFO_TAG
{
	IMG_UINT64 *pui64Caps;
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32NumCores;
} __packed PVRSRV_BRIDGE_OUT_GETMULTICOREINFO;

/*******************************************
            EventObjectWaitTimeout
 *******************************************/

/* Bridge in structure for EventObjectWaitTimeout */
typedef struct PVRSRV_BRIDGE_IN_EVENTOBJECTWAITTIMEOUT_TAG
{
	IMG_UINT64 ui64uiTimeoutus;
	IMG_HANDLE hOSEventKM;
} __packed PVRSRV_BRIDGE_IN_EVENTOBJECTWAITTIMEOUT;

/* Bridge out structure for EventObjectWaitTimeout */
typedef struct PVRSRV_BRIDGE_OUT_EVENTOBJECTWAITTIMEOUT_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_EVENTOBJECTWAITTIMEOUT;

/*******************************************
            FindProcessMemStats
 *******************************************/

/* Bridge in structure for FindProcessMemStats */
typedef struct PVRSRV_BRIDGE_IN_FINDPROCESSMEMSTATS_TAG
{
	IMG_UINT32 *pui32MemStatsArray;
	IMG_BOOL bbAllProcessStats;
	IMG_UINT32 ui32ArrSize;
	IMG_UINT32 ui32PID;
} __packed PVRSRV_BRIDGE_IN_FINDPROCESSMEMSTATS;

/* Bridge out structure for FindProcessMemStats */
typedef struct PVRSRV_BRIDGE_OUT_FINDPROCESSMEMSTATS_TAG
{
	IMG_UINT32 *pui32MemStatsArray;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_FINDPROCESSMEMSTATS;

/*******************************************
            AcquireInfoPage
 *******************************************/

/* Bridge in structure for AcquireInfoPage */
typedef struct PVRSRV_BRIDGE_IN_ACQUIREINFOPAGE_TAG
{
	IMG_UINT32 ui32EmptyStructPlaceholder;
} __packed PVRSRV_BRIDGE_IN_ACQUIREINFOPAGE;

/* Bridge out structure for AcquireInfoPage */
typedef struct PVRSRV_BRIDGE_OUT_ACQUIREINFOPAGE_TAG
{
	IMG_HANDLE hPMR;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_ACQUIREINFOPAGE;

/*******************************************
            ReleaseInfoPage
 *******************************************/

/* Bridge in structure for ReleaseInfoPage */
typedef struct PVRSRV_BRIDGE_IN_RELEASEINFOPAGE_TAG
{
	IMG_HANDLE hPMR;
} __packed PVRSRV_BRIDGE_IN_RELEASEINFOPAGE;

/* Bridge out structure for ReleaseInfoPage */
typedef struct PVRSRV_BRIDGE_OUT_RELEASEINFOPAGE_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RELEASEINFOPAGE;

#endif /* COMMON_SRVCORE_BRIDGE_H */
