/*************************************************************************/ /*!
@File
@Title          Common bridge header for srvcore
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures that are used by both
                the client and sever side of the bridge for srvcore
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

#ifndef COMMON_SRVCORE_BRIDGE_H
#define COMMON_SRVCORE_BRIDGE_H

#include "img_types.h"
#include "pvrsrv_error.h"

#include "pvrsrv_device_types.h"
#include "cache_external.h"


#define PVRSRV_BRIDGE_SRVCORE_CMD_FIRST			0
#define PVRSRV_BRIDGE_SRVCORE_CONNECT			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+0
#define PVRSRV_BRIDGE_SRVCORE_DISCONNECT			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+1
#define PVRSRV_BRIDGE_SRVCORE_ENUMERATEDEVICES			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+2
#define PVRSRV_BRIDGE_SRVCORE_ACQUIREDEVICEDATA			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+3
#define PVRSRV_BRIDGE_SRVCORE_RELEASEDEVICEDATA			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+4
#define PVRSRV_BRIDGE_SRVCORE_INITSRVDISCONNECT			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+5
#define PVRSRV_BRIDGE_SRVCORE_ACQUIREGLOBALEVENTOBJECT			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+6
#define PVRSRV_BRIDGE_SRVCORE_RELEASEGLOBALEVENTOBJECT			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+7
#define PVRSRV_BRIDGE_SRVCORE_EVENTOBJECTOPEN			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+8
#define PVRSRV_BRIDGE_SRVCORE_EVENTOBJECTWAIT			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+9
#define PVRSRV_BRIDGE_SRVCORE_EVENTOBJECTCLOSE			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+10
#define PVRSRV_BRIDGE_SRVCORE_DUMPDEBUGINFO			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+11
#define PVRSRV_BRIDGE_SRVCORE_GETDEVCLOCKSPEED			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+12
#define PVRSRV_BRIDGE_SRVCORE_HWOPTIMEOUT			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+13
#define PVRSRV_BRIDGE_SRVCORE_KICKDEVICES			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+14
#define PVRSRV_BRIDGE_SRVCORE_RESETHWRLOGS			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+15
#define PVRSRV_BRIDGE_SRVCORE_SOFTRESET			PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+16
#define PVRSRV_BRIDGE_SRVCORE_CMD_LAST			(PVRSRV_BRIDGE_SRVCORE_CMD_FIRST+16)


/*******************************************
            Connect          
 *******************************************/

/* Bridge in structure for Connect */
typedef struct PVRSRV_BRIDGE_IN_CONNECT_TAG
{
	IMG_UINT32 ui32Flags;
	IMG_UINT32 ui32ClientBuildOptions;
	IMG_UINT32 ui32ClientDDKVersion;
	IMG_UINT32 ui32ClientDDKBuild;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_CONNECT;


/* Bridge out structure for Connect */
typedef struct PVRSRV_BRIDGE_OUT_CONNECT_TAG
{
	IMG_UINT8 ui8KernelArch;
	IMG_UINT32 ui32Log2PageSize;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_CONNECT;

/*******************************************
            Disconnect          
 *******************************************/

/* Bridge in structure for Disconnect */
typedef struct PVRSRV_BRIDGE_IN_DISCONNECT_TAG
{
	 IMG_UINT32 ui32EmptyStructPlaceholder;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_DISCONNECT;


/* Bridge out structure for Disconnect */
typedef struct PVRSRV_BRIDGE_OUT_DISCONNECT_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_DISCONNECT;

/*******************************************
            EnumerateDevices          
 *******************************************/

/* Bridge in structure for EnumerateDevices */
typedef struct PVRSRV_BRIDGE_IN_ENUMERATEDEVICES_TAG
{
	/* Output pointer peDeviceType is also an implied input */
	PVRSRV_DEVICE_TYPE * peDeviceType;
	/* Output pointer peDeviceClass is also an implied input */
	PVRSRV_DEVICE_CLASS * peDeviceClass;
	/* Output pointer pui32DeviceIndex is also an implied input */
	IMG_UINT32 * pui32DeviceIndex;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_ENUMERATEDEVICES;


/* Bridge out structure for EnumerateDevices */
typedef struct PVRSRV_BRIDGE_OUT_ENUMERATEDEVICES_TAG
{
	IMG_UINT32 ui32NumDevices;
	PVRSRV_DEVICE_TYPE * peDeviceType;
	PVRSRV_DEVICE_CLASS * peDeviceClass;
	IMG_UINT32 * pui32DeviceIndex;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_ENUMERATEDEVICES;

/*******************************************
            AcquireDeviceData          
 *******************************************/

/* Bridge in structure for AcquireDeviceData */
typedef struct PVRSRV_BRIDGE_IN_ACQUIREDEVICEDATA_TAG
{
	IMG_UINT32 ui32DevIndex;
	PVRSRV_DEVICE_TYPE eDeviceType;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_ACQUIREDEVICEDATA;


/* Bridge out structure for AcquireDeviceData */
typedef struct PVRSRV_BRIDGE_OUT_ACQUIREDEVICEDATA_TAG
{
	IMG_HANDLE hDevCookie;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_ACQUIREDEVICEDATA;

/*******************************************
            ReleaseDeviceData          
 *******************************************/

/* Bridge in structure for ReleaseDeviceData */
typedef struct PVRSRV_BRIDGE_IN_RELEASEDEVICEDATA_TAG
{
	IMG_HANDLE hDevCookie;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RELEASEDEVICEDATA;


/* Bridge out structure for ReleaseDeviceData */
typedef struct PVRSRV_BRIDGE_OUT_RELEASEDEVICEDATA_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RELEASEDEVICEDATA;

/*******************************************
            InitSrvDisconnect          
 *******************************************/

/* Bridge in structure for InitSrvDisconnect */
typedef struct PVRSRV_BRIDGE_IN_INITSRVDISCONNECT_TAG
{
	IMG_BOOL bInitSuccesful;
	IMG_UINT32 ui32ClientBuildOptions;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_INITSRVDISCONNECT;


/* Bridge out structure for InitSrvDisconnect */
typedef struct PVRSRV_BRIDGE_OUT_INITSRVDISCONNECT_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_INITSRVDISCONNECT;

/*******************************************
            AcquireGlobalEventObject          
 *******************************************/

/* Bridge in structure for AcquireGlobalEventObject */
typedef struct PVRSRV_BRIDGE_IN_ACQUIREGLOBALEVENTOBJECT_TAG
{
	 IMG_UINT32 ui32EmptyStructPlaceholder;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_ACQUIREGLOBALEVENTOBJECT;


/* Bridge out structure for AcquireGlobalEventObject */
typedef struct PVRSRV_BRIDGE_OUT_ACQUIREGLOBALEVENTOBJECT_TAG
{
	IMG_HANDLE hGlobalEventObject;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_ACQUIREGLOBALEVENTOBJECT;

/*******************************************
            ReleaseGlobalEventObject          
 *******************************************/

/* Bridge in structure for ReleaseGlobalEventObject */
typedef struct PVRSRV_BRIDGE_IN_RELEASEGLOBALEVENTOBJECT_TAG
{
	IMG_HANDLE hGlobalEventObject;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RELEASEGLOBALEVENTOBJECT;


/* Bridge out structure for ReleaseGlobalEventObject */
typedef struct PVRSRV_BRIDGE_OUT_RELEASEGLOBALEVENTOBJECT_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RELEASEGLOBALEVENTOBJECT;

/*******************************************
            EventObjectOpen          
 *******************************************/

/* Bridge in structure for EventObjectOpen */
typedef struct PVRSRV_BRIDGE_IN_EVENTOBJECTOPEN_TAG
{
	IMG_HANDLE hEventObject;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_EVENTOBJECTOPEN;


/* Bridge out structure for EventObjectOpen */
typedef struct PVRSRV_BRIDGE_OUT_EVENTOBJECTOPEN_TAG
{
	IMG_HANDLE hOSEvent;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_EVENTOBJECTOPEN;

/*******************************************
            EventObjectWait          
 *******************************************/

/* Bridge in structure for EventObjectWait */
typedef struct PVRSRV_BRIDGE_IN_EVENTOBJECTWAIT_TAG
{
	IMG_HANDLE hOSEventKM;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_EVENTOBJECTWAIT;


/* Bridge out structure for EventObjectWait */
typedef struct PVRSRV_BRIDGE_OUT_EVENTOBJECTWAIT_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_EVENTOBJECTWAIT;

/*******************************************
            EventObjectClose          
 *******************************************/

/* Bridge in structure for EventObjectClose */
typedef struct PVRSRV_BRIDGE_IN_EVENTOBJECTCLOSE_TAG
{
	IMG_HANDLE hOSEventKM;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_EVENTOBJECTCLOSE;


/* Bridge out structure for EventObjectClose */
typedef struct PVRSRV_BRIDGE_OUT_EVENTOBJECTCLOSE_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_EVENTOBJECTCLOSE;

/*******************************************
            DumpDebugInfo          
 *******************************************/

/* Bridge in structure for DumpDebugInfo */
typedef struct PVRSRV_BRIDGE_IN_DUMPDEBUGINFO_TAG
{
	IMG_UINT32 ui32ui32VerbLevel;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_DUMPDEBUGINFO;


/* Bridge out structure for DumpDebugInfo */
typedef struct PVRSRV_BRIDGE_OUT_DUMPDEBUGINFO_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_DUMPDEBUGINFO;

/*******************************************
            GetDevClockSpeed          
 *******************************************/

/* Bridge in structure for GetDevClockSpeed */
typedef struct PVRSRV_BRIDGE_IN_GETDEVCLOCKSPEED_TAG
{
	IMG_HANDLE hDevNode;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_GETDEVCLOCKSPEED;


/* Bridge out structure for GetDevClockSpeed */
typedef struct PVRSRV_BRIDGE_OUT_GETDEVCLOCKSPEED_TAG
{
	IMG_UINT32 ui32ui32ClockSpeed;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_GETDEVCLOCKSPEED;

/*******************************************
            HWOpTimeout          
 *******************************************/

/* Bridge in structure for HWOpTimeout */
typedef struct PVRSRV_BRIDGE_IN_HWOPTIMEOUT_TAG
{
	 IMG_UINT32 ui32EmptyStructPlaceholder;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_HWOPTIMEOUT;


/* Bridge out structure for HWOpTimeout */
typedef struct PVRSRV_BRIDGE_OUT_HWOPTIMEOUT_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_HWOPTIMEOUT;

/*******************************************
            KickDevices          
 *******************************************/

/* Bridge in structure for KickDevices */
typedef struct PVRSRV_BRIDGE_IN_KICKDEVICES_TAG
{
	 IMG_UINT32 ui32EmptyStructPlaceholder;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_KICKDEVICES;


/* Bridge out structure for KickDevices */
typedef struct PVRSRV_BRIDGE_OUT_KICKDEVICES_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_KICKDEVICES;

/*******************************************
            ResetHWRLogs          
 *******************************************/

/* Bridge in structure for ResetHWRLogs */
typedef struct PVRSRV_BRIDGE_IN_RESETHWRLOGS_TAG
{
	IMG_HANDLE hDevNode;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RESETHWRLOGS;


/* Bridge out structure for ResetHWRLogs */
typedef struct PVRSRV_BRIDGE_OUT_RESETHWRLOGS_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RESETHWRLOGS;

/*******************************************
            SoftReset          
 *******************************************/

/* Bridge in structure for SoftReset */
typedef struct PVRSRV_BRIDGE_IN_SOFTRESET_TAG
{
	IMG_HANDLE hDevNode;
	IMG_UINT64 ui64ResetValue1;
	IMG_UINT64 ui64ResetValue2;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_SOFTRESET;


/* Bridge out structure for SoftReset */
typedef struct PVRSRV_BRIDGE_OUT_SOFTRESET_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_SOFTRESET;

#endif /* COMMON_SRVCORE_BRIDGE_H */
