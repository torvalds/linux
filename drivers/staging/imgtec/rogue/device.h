/**************************************************************************/ /*!
@File
@Title          Common Device header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Device related function templates and defines
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
*/ /***************************************************************************/

#ifndef __DEVICE_H__
#define __DEVICE_H__


#include "pvrsrv_device_node.h"
#include "lock_types.h"
#include "devicemem_heapcfg.h"
#include "mmu_common.h"	
#include "ra.h"  		/* RA_ARENA */
#include "pvrsrv_device.h"
#include "srvkm.h"
#include "physheap.h"
#include <powervr/sync_external.h>
#include "sysinfo.h"
#include "dllist.h"
#include "cache_km.h"

#include "lock.h"

#if defined(SUPPORT_GPUVIRT_VALIDATION)
#include "virt_validation_defs.h"
#endif

#if defined(SUPPORT_BUFFER_SYNC)
struct pvr_buffer_sync_context;
#endif

#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
struct SYNC_RECORD;
#endif

#define MMU_BAD_PHYS_ADDR (0xbadbad00badULL)


typedef enum _PVRSRV_DEVICE_HEALTH_STATUS_
{
	PVRSRV_DEVICE_HEALTH_STATUS_OK = 0,
	PVRSRV_DEVICE_HEALTH_STATUS_NOT_RESPONDING,
	PVRSRV_DEVICE_HEALTH_STATUS_DEAD
} PVRSRV_DEVICE_HEALTH_STATUS;

typedef enum _PVRSRV_DEVICE_HEALTH_REASON_
{
	PVRSRV_DEVICE_HEALTH_REASON_NONE = 0,
	PVRSRV_DEVICE_HEALTH_REASON_ASSERTED,
	PVRSRV_DEVICE_HEALTH_REASON_POLL_FAILING,
	PVRSRV_DEVICE_HEALTH_REASON_TIMEOUTS,
	PVRSRV_DEVICE_HEALTH_REASON_QUEUE_CORRUPT,
	PVRSRV_DEVICE_HEALTH_REASON_QUEUE_STALLED
} PVRSRV_DEVICE_HEALTH_REASON;

PVRSRV_ERROR IMG_CALLCONV PVRSRVDeviceFinalise(PVRSRV_DEVICE_NODE *psDeviceNode,
											   IMG_BOOL bInitSuccessful);

PVRSRV_ERROR IMG_CALLCONV PVRSRVDevInitCompatCheck(PVRSRV_DEVICE_NODE *psDeviceNode);

PVRSRV_ERROR IMG_CALLCONV RGXClientConnectCompatCheck_ClientAgainstFW(PVRSRV_DEVICE_NODE * psDeviceNode, IMG_UINT32 ui32ClientBuildOptions);

	
#endif /* __DEVICE_H__ */

/******************************************************************************
 End of file (device.h)
******************************************************************************/
