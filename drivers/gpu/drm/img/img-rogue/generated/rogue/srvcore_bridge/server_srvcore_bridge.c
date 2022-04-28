/*******************************************************************************
@File
@Title          Server bridge for srvcore
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for srvcore
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

#include <linux/uaccess.h>

#include "img_defs.h"

#include "srvcore.h"
#include "info_page.h"
#include "proc_stats.h"
#include "rgx_fwif_alignchecks.h"

#include "common_srvcore_bridge.h"

#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#if defined(SUPPORT_RGX)
#include "rgx_bridge.h"
#endif
#include "srvcore.h"
#include "handle.h"

#include <linux/slab.h>

/* ***************************************************************************
 * Server-side bridge entry points
 */

static IMG_INT
PVRSRVBridgeConnect(IMG_UINT32 ui32DispatchTableEntry,
		    IMG_UINT8 * psConnectIN_UI8,
		    IMG_UINT8 * psConnectOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_CONNECT *psConnectIN =
	    (PVRSRV_BRIDGE_IN_CONNECT *) IMG_OFFSET_ADDR(psConnectIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_CONNECT *psConnectOUT =
	    (PVRSRV_BRIDGE_OUT_CONNECT *) IMG_OFFSET_ADDR(psConnectOUT_UI8, 0);

	psConnectOUT->eError =
	    PVRSRVConnectKM(psConnection, OSGetDevNode(psConnection),
			    psConnectIN->ui32Flags,
			    psConnectIN->ui32ClientBuildOptions,
			    psConnectIN->ui32ClientDDKVersion,
			    psConnectIN->ui32ClientDDKBuild,
			    &psConnectOUT->ui8KernelArch,
			    &psConnectOUT->ui32CapabilityFlags, &psConnectOUT->ui64PackedBvnc);

	return 0;
}

static IMG_INT
PVRSRVBridgeDisconnect(IMG_UINT32 ui32DispatchTableEntry,
		       IMG_UINT8 * psDisconnectIN_UI8,
		       IMG_UINT8 * psDisconnectOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DISCONNECT *psDisconnectIN =
	    (PVRSRV_BRIDGE_IN_DISCONNECT *) IMG_OFFSET_ADDR(psDisconnectIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DISCONNECT *psDisconnectOUT =
	    (PVRSRV_BRIDGE_OUT_DISCONNECT *) IMG_OFFSET_ADDR(psDisconnectOUT_UI8, 0);

	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psDisconnectIN);

	psDisconnectOUT->eError = PVRSRVDisconnectKM();

	return 0;
}

static PVRSRV_ERROR _AcquireGlobalEventObjecthGlobalEventObjectIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = PVRSRVReleaseGlobalEventObjectKM((IMG_HANDLE) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgeAcquireGlobalEventObject(IMG_UINT32 ui32DispatchTableEntry,
				     IMG_UINT8 * psAcquireGlobalEventObjectIN_UI8,
				     IMG_UINT8 * psAcquireGlobalEventObjectOUT_UI8,
				     CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_ACQUIREGLOBALEVENTOBJECT *psAcquireGlobalEventObjectIN =
	    (PVRSRV_BRIDGE_IN_ACQUIREGLOBALEVENTOBJECT *)
	    IMG_OFFSET_ADDR(psAcquireGlobalEventObjectIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_ACQUIREGLOBALEVENTOBJECT *psAcquireGlobalEventObjectOUT =
	    (PVRSRV_BRIDGE_OUT_ACQUIREGLOBALEVENTOBJECT *)
	    IMG_OFFSET_ADDR(psAcquireGlobalEventObjectOUT_UI8, 0);

	IMG_HANDLE hGlobalEventObjectInt = NULL;

	PVR_UNREFERENCED_PARAMETER(psAcquireGlobalEventObjectIN);

	psAcquireGlobalEventObjectOUT->eError =
	    PVRSRVAcquireGlobalEventObjectKM(&hGlobalEventObjectInt);
	/* Exit early if bridged call fails */
	if (unlikely(psAcquireGlobalEventObjectOUT->eError != PVRSRV_OK))
	{
		goto AcquireGlobalEventObject_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psAcquireGlobalEventObjectOUT->eError =
	    PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
				      &psAcquireGlobalEventObjectOUT->hGlobalEventObject,
				      (void *)hGlobalEventObjectInt,
				      PVRSRV_HANDLE_TYPE_SHARED_EVENT_OBJECT,
				      PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
				      (PFN_HANDLE_RELEASE) &
				      _AcquireGlobalEventObjecthGlobalEventObjectIntRelease);
	if (unlikely(psAcquireGlobalEventObjectOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto AcquireGlobalEventObject_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

AcquireGlobalEventObject_exit:

	if (psAcquireGlobalEventObjectOUT->eError != PVRSRV_OK)
	{
		if (hGlobalEventObjectInt)
		{
			PVRSRVReleaseGlobalEventObjectKM(hGlobalEventObjectInt);
		}
	}

	return 0;
}

static IMG_INT
PVRSRVBridgeReleaseGlobalEventObject(IMG_UINT32 ui32DispatchTableEntry,
				     IMG_UINT8 * psReleaseGlobalEventObjectIN_UI8,
				     IMG_UINT8 * psReleaseGlobalEventObjectOUT_UI8,
				     CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RELEASEGLOBALEVENTOBJECT *psReleaseGlobalEventObjectIN =
	    (PVRSRV_BRIDGE_IN_RELEASEGLOBALEVENTOBJECT *)
	    IMG_OFFSET_ADDR(psReleaseGlobalEventObjectIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RELEASEGLOBALEVENTOBJECT *psReleaseGlobalEventObjectOUT =
	    (PVRSRV_BRIDGE_OUT_RELEASEGLOBALEVENTOBJECT *)
	    IMG_OFFSET_ADDR(psReleaseGlobalEventObjectOUT_UI8, 0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psReleaseGlobalEventObjectOUT->eError =
	    PVRSRVReleaseHandleStagedUnlock(psConnection->psHandleBase,
					    (IMG_HANDLE) psReleaseGlobalEventObjectIN->
					    hGlobalEventObject,
					    PVRSRV_HANDLE_TYPE_SHARED_EVENT_OBJECT);
	if (unlikely
	    ((psReleaseGlobalEventObjectOUT->eError != PVRSRV_OK)
	     && (psReleaseGlobalEventObjectOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s",
			 __func__, PVRSRVGetErrorString(psReleaseGlobalEventObjectOUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto ReleaseGlobalEventObject_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

ReleaseGlobalEventObject_exit:

	return 0;
}

static PVRSRV_ERROR _EventObjectOpenhOSEventIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = OSEventObjectClose((IMG_HANDLE) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgeEventObjectOpen(IMG_UINT32 ui32DispatchTableEntry,
			    IMG_UINT8 * psEventObjectOpenIN_UI8,
			    IMG_UINT8 * psEventObjectOpenOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_EVENTOBJECTOPEN *psEventObjectOpenIN =
	    (PVRSRV_BRIDGE_IN_EVENTOBJECTOPEN *) IMG_OFFSET_ADDR(psEventObjectOpenIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_EVENTOBJECTOPEN *psEventObjectOpenOUT =
	    (PVRSRV_BRIDGE_OUT_EVENTOBJECTOPEN *) IMG_OFFSET_ADDR(psEventObjectOpenOUT_UI8, 0);

	IMG_HANDLE hEventObject = psEventObjectOpenIN->hEventObject;
	IMG_HANDLE hEventObjectInt = NULL;
	IMG_HANDLE hOSEventInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psEventObjectOpenOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&hEventObjectInt,
				       hEventObject,
				       PVRSRV_HANDLE_TYPE_SHARED_EVENT_OBJECT, IMG_TRUE);
	if (unlikely(psEventObjectOpenOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto EventObjectOpen_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psEventObjectOpenOUT->eError = OSEventObjectOpen(hEventObjectInt, &hOSEventInt);
	/* Exit early if bridged call fails */
	if (unlikely(psEventObjectOpenOUT->eError != PVRSRV_OK))
	{
		goto EventObjectOpen_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psEventObjectOpenOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
								 &psEventObjectOpenOUT->hOSEvent,
								 (void *)hOSEventInt,
								 PVRSRV_HANDLE_TYPE_EVENT_OBJECT_CONNECT,
								 PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
								 (PFN_HANDLE_RELEASE) &
								 _EventObjectOpenhOSEventIntRelease);
	if (unlikely(psEventObjectOpenOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto EventObjectOpen_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

EventObjectOpen_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (hEventObjectInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hEventObject, PVRSRV_HANDLE_TYPE_SHARED_EVENT_OBJECT);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	if (psEventObjectOpenOUT->eError != PVRSRV_OK)
	{
		if (hOSEventInt)
		{
			OSEventObjectClose(hOSEventInt);
		}
	}

	return 0;
}

static IMG_INT
PVRSRVBridgeEventObjectWait(IMG_UINT32 ui32DispatchTableEntry,
			    IMG_UINT8 * psEventObjectWaitIN_UI8,
			    IMG_UINT8 * psEventObjectWaitOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_EVENTOBJECTWAIT *psEventObjectWaitIN =
	    (PVRSRV_BRIDGE_IN_EVENTOBJECTWAIT *) IMG_OFFSET_ADDR(psEventObjectWaitIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_EVENTOBJECTWAIT *psEventObjectWaitOUT =
	    (PVRSRV_BRIDGE_OUT_EVENTOBJECTWAIT *) IMG_OFFSET_ADDR(psEventObjectWaitOUT_UI8, 0);

	IMG_HANDLE hOSEventKM = psEventObjectWaitIN->hOSEventKM;
	IMG_HANDLE hOSEventKMInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psEventObjectWaitOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&hOSEventKMInt,
				       hOSEventKM,
				       PVRSRV_HANDLE_TYPE_EVENT_OBJECT_CONNECT, IMG_TRUE);
	if (unlikely(psEventObjectWaitOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto EventObjectWait_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psEventObjectWaitOUT->eError = OSEventObjectWait(hOSEventKMInt);

EventObjectWait_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (hOSEventKMInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hOSEventKM, PVRSRV_HANDLE_TYPE_EVENT_OBJECT_CONNECT);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static IMG_INT
PVRSRVBridgeEventObjectClose(IMG_UINT32 ui32DispatchTableEntry,
			     IMG_UINT8 * psEventObjectCloseIN_UI8,
			     IMG_UINT8 * psEventObjectCloseOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_EVENTOBJECTCLOSE *psEventObjectCloseIN =
	    (PVRSRV_BRIDGE_IN_EVENTOBJECTCLOSE *) IMG_OFFSET_ADDR(psEventObjectCloseIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_EVENTOBJECTCLOSE *psEventObjectCloseOUT =
	    (PVRSRV_BRIDGE_OUT_EVENTOBJECTCLOSE *) IMG_OFFSET_ADDR(psEventObjectCloseOUT_UI8, 0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psEventObjectCloseOUT->eError =
	    PVRSRVReleaseHandleStagedUnlock(psConnection->psHandleBase,
					    (IMG_HANDLE) psEventObjectCloseIN->hOSEventKM,
					    PVRSRV_HANDLE_TYPE_EVENT_OBJECT_CONNECT);
	if (unlikely((psEventObjectCloseOUT->eError != PVRSRV_OK) &&
		     (psEventObjectCloseOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s", __func__, PVRSRVGetErrorString(psEventObjectCloseOUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto EventObjectClose_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

EventObjectClose_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDumpDebugInfo(IMG_UINT32 ui32DispatchTableEntry,
			  IMG_UINT8 * psDumpDebugInfoIN_UI8,
			  IMG_UINT8 * psDumpDebugInfoOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DUMPDEBUGINFO *psDumpDebugInfoIN =
	    (PVRSRV_BRIDGE_IN_DUMPDEBUGINFO *) IMG_OFFSET_ADDR(psDumpDebugInfoIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DUMPDEBUGINFO *psDumpDebugInfoOUT =
	    (PVRSRV_BRIDGE_OUT_DUMPDEBUGINFO *) IMG_OFFSET_ADDR(psDumpDebugInfoOUT_UI8, 0);

	psDumpDebugInfoOUT->eError =
	    PVRSRVDumpDebugInfoKM(psConnection, OSGetDevNode(psConnection),
				  psDumpDebugInfoIN->ui32VerbLevel);

	return 0;
}

static IMG_INT
PVRSRVBridgeGetDevClockSpeed(IMG_UINT32 ui32DispatchTableEntry,
			     IMG_UINT8 * psGetDevClockSpeedIN_UI8,
			     IMG_UINT8 * psGetDevClockSpeedOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_GETDEVCLOCKSPEED *psGetDevClockSpeedIN =
	    (PVRSRV_BRIDGE_IN_GETDEVCLOCKSPEED *) IMG_OFFSET_ADDR(psGetDevClockSpeedIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_GETDEVCLOCKSPEED *psGetDevClockSpeedOUT =
	    (PVRSRV_BRIDGE_OUT_GETDEVCLOCKSPEED *) IMG_OFFSET_ADDR(psGetDevClockSpeedOUT_UI8, 0);

	PVR_UNREFERENCED_PARAMETER(psGetDevClockSpeedIN);

	psGetDevClockSpeedOUT->eError =
	    PVRSRVGetDevClockSpeedKM(psConnection, OSGetDevNode(psConnection),
				     &psGetDevClockSpeedOUT->ui32ClockSpeed);

	return 0;
}

static IMG_INT
PVRSRVBridgeHWOpTimeout(IMG_UINT32 ui32DispatchTableEntry,
			IMG_UINT8 * psHWOpTimeoutIN_UI8,
			IMG_UINT8 * psHWOpTimeoutOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_HWOPTIMEOUT *psHWOpTimeoutIN =
	    (PVRSRV_BRIDGE_IN_HWOPTIMEOUT *) IMG_OFFSET_ADDR(psHWOpTimeoutIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_HWOPTIMEOUT *psHWOpTimeoutOUT =
	    (PVRSRV_BRIDGE_OUT_HWOPTIMEOUT *) IMG_OFFSET_ADDR(psHWOpTimeoutOUT_UI8, 0);

	PVR_UNREFERENCED_PARAMETER(psHWOpTimeoutIN);

	psHWOpTimeoutOUT->eError = PVRSRVHWOpTimeoutKM(psConnection, OSGetDevNode(psConnection));

	return 0;
}

static IMG_INT
PVRSRVBridgeAlignmentCheck(IMG_UINT32 ui32DispatchTableEntry,
			   IMG_UINT8 * psAlignmentCheckIN_UI8,
			   IMG_UINT8 * psAlignmentCheckOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_ALIGNMENTCHECK *psAlignmentCheckIN =
	    (PVRSRV_BRIDGE_IN_ALIGNMENTCHECK *) IMG_OFFSET_ADDR(psAlignmentCheckIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_ALIGNMENTCHECK *psAlignmentCheckOUT =
	    (PVRSRV_BRIDGE_OUT_ALIGNMENTCHECK *) IMG_OFFSET_ADDR(psAlignmentCheckOUT_UI8, 0);

	IMG_UINT32 *ui32AlignChecksInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize =
	    (psAlignmentCheckIN->ui32AlignChecksSize * sizeof(IMG_UINT32)) + 0;

	if (unlikely(psAlignmentCheckIN->ui32AlignChecksSize > RGXFW_ALIGN_CHECKS_UM_MAX))
	{
		psAlignmentCheckOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto AlignmentCheck_exit;
	}

	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psAlignmentCheckIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psAlignmentCheckIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocZMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psAlignmentCheckOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto AlignmentCheck_exit;
			}
		}
	}

	if (psAlignmentCheckIN->ui32AlignChecksSize != 0)
	{
		ui32AlignChecksInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psAlignmentCheckIN->ui32AlignChecksSize * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psAlignmentCheckIN->ui32AlignChecksSize * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32AlignChecksInt,
		     (const void __user *)psAlignmentCheckIN->pui32AlignChecks,
		     psAlignmentCheckIN->ui32AlignChecksSize * sizeof(IMG_UINT32)) != PVRSRV_OK)
		{
			psAlignmentCheckOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto AlignmentCheck_exit;
		}
	}

	psAlignmentCheckOUT->eError =
	    PVRSRVAlignmentCheckKM(psConnection, OSGetDevNode(psConnection),
				   psAlignmentCheckIN->ui32AlignChecksSize, ui32AlignChecksInt);

AlignmentCheck_exit:

	/* Allocated space should be equal to the last updated offset */
	PVR_ASSERT(ui32BufferSize == ui32NextOffset);

#if defined(INTEGRITY_OS)
	if (pArrayArgsBuffer)
#else
	if (!bHaveEnoughSpace && pArrayArgsBuffer)
#endif
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static IMG_INT
PVRSRVBridgeGetDeviceStatus(IMG_UINT32 ui32DispatchTableEntry,
			    IMG_UINT8 * psGetDeviceStatusIN_UI8,
			    IMG_UINT8 * psGetDeviceStatusOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_GETDEVICESTATUS *psGetDeviceStatusIN =
	    (PVRSRV_BRIDGE_IN_GETDEVICESTATUS *) IMG_OFFSET_ADDR(psGetDeviceStatusIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_GETDEVICESTATUS *psGetDeviceStatusOUT =
	    (PVRSRV_BRIDGE_OUT_GETDEVICESTATUS *) IMG_OFFSET_ADDR(psGetDeviceStatusOUT_UI8, 0);

	PVR_UNREFERENCED_PARAMETER(psGetDeviceStatusIN);

	psGetDeviceStatusOUT->eError =
	    PVRSRVGetDeviceStatusKM(psConnection, OSGetDevNode(psConnection),
				    &psGetDeviceStatusOUT->ui32DeviceSatus);

	return 0;
}

static IMG_INT
PVRSRVBridgeGetMultiCoreInfo(IMG_UINT32 ui32DispatchTableEntry,
			     IMG_UINT8 * psGetMultiCoreInfoIN_UI8,
			     IMG_UINT8 * psGetMultiCoreInfoOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_GETMULTICOREINFO *psGetMultiCoreInfoIN =
	    (PVRSRV_BRIDGE_IN_GETMULTICOREINFO *) IMG_OFFSET_ADDR(psGetMultiCoreInfoIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_GETMULTICOREINFO *psGetMultiCoreInfoOUT =
	    (PVRSRV_BRIDGE_OUT_GETMULTICOREINFO *) IMG_OFFSET_ADDR(psGetMultiCoreInfoOUT_UI8, 0);

	IMG_UINT64 *pui64CapsInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = (psGetMultiCoreInfoIN->ui32CapsSize * sizeof(IMG_UINT64)) + 0;

	if (psGetMultiCoreInfoIN->ui32CapsSize > 8)
	{
		psGetMultiCoreInfoOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto GetMultiCoreInfo_exit;
	}

	psGetMultiCoreInfoOUT->pui64Caps = psGetMultiCoreInfoIN->pui64Caps;

	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psGetMultiCoreInfoIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psGetMultiCoreInfoIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocZMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psGetMultiCoreInfoOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto GetMultiCoreInfo_exit;
			}
		}
	}

	if (psGetMultiCoreInfoIN->ui32CapsSize != 0)
	{
		pui64CapsInt = (IMG_UINT64 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psGetMultiCoreInfoIN->ui32CapsSize * sizeof(IMG_UINT64);
	}

	psGetMultiCoreInfoOUT->eError =
	    PVRSRVGetMultiCoreInfoKM(psConnection, OSGetDevNode(psConnection),
				     psGetMultiCoreInfoIN->ui32CapsSize,
				     &psGetMultiCoreInfoOUT->ui32NumCores, pui64CapsInt);

	/* If dest ptr is non-null and we have data to copy */
	if ((pui64CapsInt) && ((psGetMultiCoreInfoIN->ui32CapsSize * sizeof(IMG_UINT64)) > 0))
	{
		if (unlikely
		    (OSCopyToUser
		     (NULL, (void __user *)psGetMultiCoreInfoOUT->pui64Caps, pui64CapsInt,
		      (psGetMultiCoreInfoIN->ui32CapsSize * sizeof(IMG_UINT64))) != PVRSRV_OK))
		{
			psGetMultiCoreInfoOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto GetMultiCoreInfo_exit;
		}
	}

GetMultiCoreInfo_exit:

	/* Allocated space should be equal to the last updated offset */
	PVR_ASSERT(ui32BufferSize == ui32NextOffset);

#if defined(INTEGRITY_OS)
	if (pArrayArgsBuffer)
#else
	if (!bHaveEnoughSpace && pArrayArgsBuffer)
#endif
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static IMG_INT
PVRSRVBridgeEventObjectWaitTimeout(IMG_UINT32 ui32DispatchTableEntry,
				   IMG_UINT8 * psEventObjectWaitTimeoutIN_UI8,
				   IMG_UINT8 * psEventObjectWaitTimeoutOUT_UI8,
				   CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_EVENTOBJECTWAITTIMEOUT *psEventObjectWaitTimeoutIN =
	    (PVRSRV_BRIDGE_IN_EVENTOBJECTWAITTIMEOUT *)
	    IMG_OFFSET_ADDR(psEventObjectWaitTimeoutIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_EVENTOBJECTWAITTIMEOUT *psEventObjectWaitTimeoutOUT =
	    (PVRSRV_BRIDGE_OUT_EVENTOBJECTWAITTIMEOUT *)
	    IMG_OFFSET_ADDR(psEventObjectWaitTimeoutOUT_UI8, 0);

	IMG_HANDLE hOSEventKM = psEventObjectWaitTimeoutIN->hOSEventKM;
	IMG_HANDLE hOSEventKMInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psEventObjectWaitTimeoutOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&hOSEventKMInt,
				       hOSEventKM,
				       PVRSRV_HANDLE_TYPE_EVENT_OBJECT_CONNECT, IMG_TRUE);
	if (unlikely(psEventObjectWaitTimeoutOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto EventObjectWaitTimeout_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psEventObjectWaitTimeoutOUT->eError =
	    OSEventObjectWaitTimeout(hOSEventKMInt, psEventObjectWaitTimeoutIN->ui64uiTimeoutus);

EventObjectWaitTimeout_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (hOSEventKMInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hOSEventKM, PVRSRV_HANDLE_TYPE_EVENT_OBJECT_CONNECT);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static IMG_INT
PVRSRVBridgeFindProcessMemStats(IMG_UINT32 ui32DispatchTableEntry,
				IMG_UINT8 * psFindProcessMemStatsIN_UI8,
				IMG_UINT8 * psFindProcessMemStatsOUT_UI8,
				CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_FINDPROCESSMEMSTATS *psFindProcessMemStatsIN =
	    (PVRSRV_BRIDGE_IN_FINDPROCESSMEMSTATS *) IMG_OFFSET_ADDR(psFindProcessMemStatsIN_UI8,
								     0);
	PVRSRV_BRIDGE_OUT_FINDPROCESSMEMSTATS *psFindProcessMemStatsOUT =
	    (PVRSRV_BRIDGE_OUT_FINDPROCESSMEMSTATS *) IMG_OFFSET_ADDR(psFindProcessMemStatsOUT_UI8,
								      0);

	IMG_UINT32 *pui32MemStatsArrayInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = (psFindProcessMemStatsIN->ui32ArrSize * sizeof(IMG_UINT32)) + 0;

	if (psFindProcessMemStatsIN->ui32ArrSize > PVRSRV_PROCESS_STAT_TYPE_COUNT)
	{
		psFindProcessMemStatsOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto FindProcessMemStats_exit;
	}

	PVR_UNREFERENCED_PARAMETER(psConnection);

	psFindProcessMemStatsOUT->pui32MemStatsArray = psFindProcessMemStatsIN->pui32MemStatsArray;

	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psFindProcessMemStatsIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psFindProcessMemStatsIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocZMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psFindProcessMemStatsOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto FindProcessMemStats_exit;
			}
		}
	}

	if (psFindProcessMemStatsIN->ui32ArrSize != 0)
	{
		pui32MemStatsArrayInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psFindProcessMemStatsIN->ui32ArrSize * sizeof(IMG_UINT32);
	}

	psFindProcessMemStatsOUT->eError =
	    PVRSRVFindProcessMemStatsKM(psFindProcessMemStatsIN->ui32PID,
					psFindProcessMemStatsIN->ui32ArrSize,
					psFindProcessMemStatsIN->bbAllProcessStats,
					pui32MemStatsArrayInt);

	/* If dest ptr is non-null and we have data to copy */
	if ((pui32MemStatsArrayInt) &&
	    ((psFindProcessMemStatsIN->ui32ArrSize * sizeof(IMG_UINT32)) > 0))
	{
		if (unlikely
		    (OSCopyToUser
		     (NULL, (void __user *)psFindProcessMemStatsOUT->pui32MemStatsArray,
		      pui32MemStatsArrayInt,
		      (psFindProcessMemStatsIN->ui32ArrSize * sizeof(IMG_UINT32))) != PVRSRV_OK))
		{
			psFindProcessMemStatsOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto FindProcessMemStats_exit;
		}
	}

FindProcessMemStats_exit:

	/* Allocated space should be equal to the last updated offset */
	PVR_ASSERT(ui32BufferSize == ui32NextOffset);

#if defined(INTEGRITY_OS)
	if (pArrayArgsBuffer)
#else
	if (!bHaveEnoughSpace && pArrayArgsBuffer)
#endif
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static PVRSRV_ERROR _AcquireInfoPagepsPMRIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = PVRSRVReleaseInfoPageKM((PMR *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgeAcquireInfoPage(IMG_UINT32 ui32DispatchTableEntry,
			    IMG_UINT8 * psAcquireInfoPageIN_UI8,
			    IMG_UINT8 * psAcquireInfoPageOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_ACQUIREINFOPAGE *psAcquireInfoPageIN =
	    (PVRSRV_BRIDGE_IN_ACQUIREINFOPAGE *) IMG_OFFSET_ADDR(psAcquireInfoPageIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_ACQUIREINFOPAGE *psAcquireInfoPageOUT =
	    (PVRSRV_BRIDGE_OUT_ACQUIREINFOPAGE *) IMG_OFFSET_ADDR(psAcquireInfoPageOUT_UI8, 0);

	PMR *psPMRInt = NULL;

	PVR_UNREFERENCED_PARAMETER(psAcquireInfoPageIN);

	psAcquireInfoPageOUT->eError = PVRSRVAcquireInfoPageKM(&psPMRInt);
	/* Exit early if bridged call fails */
	if (unlikely(psAcquireInfoPageOUT->eError != PVRSRV_OK))
	{
		goto AcquireInfoPage_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psProcessHandleBase->psHandleBase);

	psAcquireInfoPageOUT->eError =
	    PVRSRVAllocHandleUnlocked(psConnection->psProcessHandleBase->psHandleBase,
				      &psAcquireInfoPageOUT->hPMR, (void *)psPMRInt,
				      PVRSRV_HANDLE_TYPE_DEVMEM_MEM_IMPORT,
				      PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
				      (PFN_HANDLE_RELEASE) & _AcquireInfoPagepsPMRIntRelease);
	if (unlikely(psAcquireInfoPageOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psProcessHandleBase->psHandleBase);
		goto AcquireInfoPage_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psProcessHandleBase->psHandleBase);

AcquireInfoPage_exit:

	if (psAcquireInfoPageOUT->eError != PVRSRV_OK)
	{
		if (psPMRInt)
		{
			PVRSRVReleaseInfoPageKM(psPMRInt);
		}
	}

	return 0;
}

static IMG_INT
PVRSRVBridgeReleaseInfoPage(IMG_UINT32 ui32DispatchTableEntry,
			    IMG_UINT8 * psReleaseInfoPageIN_UI8,
			    IMG_UINT8 * psReleaseInfoPageOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RELEASEINFOPAGE *psReleaseInfoPageIN =
	    (PVRSRV_BRIDGE_IN_RELEASEINFOPAGE *) IMG_OFFSET_ADDR(psReleaseInfoPageIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RELEASEINFOPAGE *psReleaseInfoPageOUT =
	    (PVRSRV_BRIDGE_OUT_RELEASEINFOPAGE *) IMG_OFFSET_ADDR(psReleaseInfoPageOUT_UI8, 0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psProcessHandleBase->psHandleBase);

	psReleaseInfoPageOUT->eError =
	    PVRSRVReleaseHandleStagedUnlock(psConnection->psProcessHandleBase->psHandleBase,
					    (IMG_HANDLE) psReleaseInfoPageIN->hPMR,
					    PVRSRV_HANDLE_TYPE_DEVMEM_MEM_IMPORT);
	if (unlikely((psReleaseInfoPageOUT->eError != PVRSRV_OK) &&
		     (psReleaseInfoPageOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s", __func__, PVRSRVGetErrorString(psReleaseInfoPageOUT->eError)));
		UnlockHandle(psConnection->psProcessHandleBase->psHandleBase);
		goto ReleaseInfoPage_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psProcessHandleBase->psHandleBase);

ReleaseInfoPage_exit:

	return 0;
}

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

PVRSRV_ERROR InitSRVCOREBridge(void);
PVRSRV_ERROR DeinitSRVCOREBridge(void);

/*
 * Register all SRVCORE functions with services
 */
PVRSRV_ERROR InitSRVCOREBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_CONNECT,
			      PVRSRVBridgeConnect, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_DISCONNECT,
			      PVRSRVBridgeDisconnect, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_ACQUIREGLOBALEVENTOBJECT,
			      PVRSRVBridgeAcquireGlobalEventObject, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_RELEASEGLOBALEVENTOBJECT,
			      PVRSRVBridgeReleaseGlobalEventObject, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_EVENTOBJECTOPEN,
			      PVRSRVBridgeEventObjectOpen, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_EVENTOBJECTWAIT,
			      PVRSRVBridgeEventObjectWait, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_EVENTOBJECTCLOSE,
			      PVRSRVBridgeEventObjectClose, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_DUMPDEBUGINFO,
			      PVRSRVBridgeDumpDebugInfo, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_GETDEVCLOCKSPEED,
			      PVRSRVBridgeGetDevClockSpeed, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_HWOPTIMEOUT,
			      PVRSRVBridgeHWOpTimeout, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_ALIGNMENTCHECK,
			      PVRSRVBridgeAlignmentCheck, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_GETDEVICESTATUS,
			      PVRSRVBridgeGetDeviceStatus, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_GETMULTICOREINFO,
			      PVRSRVBridgeGetMultiCoreInfo, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_EVENTOBJECTWAITTIMEOUT,
			      PVRSRVBridgeEventObjectWaitTimeout, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_FINDPROCESSMEMSTATS,
			      PVRSRVBridgeFindProcessMemStats, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_ACQUIREINFOPAGE,
			      PVRSRVBridgeAcquireInfoPage, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_RELEASEINFOPAGE,
			      PVRSRVBridgeReleaseInfoPage, NULL);

	return PVRSRV_OK;
}

/*
 * Unregister all srvcore functions with services
 */
PVRSRV_ERROR DeinitSRVCOREBridge(void)
{

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_CONNECT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_DISCONNECT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE,
				PVRSRV_BRIDGE_SRVCORE_ACQUIREGLOBALEVENTOBJECT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE,
				PVRSRV_BRIDGE_SRVCORE_RELEASEGLOBALEVENTOBJECT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_EVENTOBJECTOPEN);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_EVENTOBJECTWAIT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_EVENTOBJECTCLOSE);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_DUMPDEBUGINFO);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_GETDEVCLOCKSPEED);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_HWOPTIMEOUT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_ALIGNMENTCHECK);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_GETDEVICESTATUS);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_GETMULTICOREINFO);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE,
				PVRSRV_BRIDGE_SRVCORE_EVENTOBJECTWAITTIMEOUT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_FINDPROCESSMEMSTATS);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_ACQUIREINFOPAGE);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE, PVRSRV_BRIDGE_SRVCORE_RELEASEINFOPAGE);

	return PVRSRV_OK;
}
