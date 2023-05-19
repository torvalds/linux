/*******************************************************************************
@File
@Title          Server bridge for rgxbreakpoint
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for rgxbreakpoint
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

#include "rgxbreakpoint.h"

#include "common_rgxbreakpoint_bridge.h"

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

#if !defined(EXCLUDE_RGXBREAKPOINT_BRIDGE)

/* ***************************************************************************
 * Server-side bridge entry points
 */

static IMG_INT
PVRSRVBridgeRGXSetBreakpoint(IMG_UINT32 ui32DispatchTableEntry,
			     IMG_UINT8 * psRGXSetBreakpointIN_UI8,
			     IMG_UINT8 * psRGXSetBreakpointOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXSETBREAKPOINT *psRGXSetBreakpointIN =
	    (PVRSRV_BRIDGE_IN_RGXSETBREAKPOINT *) IMG_OFFSET_ADDR(psRGXSetBreakpointIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXSETBREAKPOINT *psRGXSetBreakpointOUT =
	    (PVRSRV_BRIDGE_OUT_RGXSETBREAKPOINT *) IMG_OFFSET_ADDR(psRGXSetBreakpointOUT_UI8, 0);

	IMG_HANDLE hPrivData = psRGXSetBreakpointIN->hPrivData;
	IMG_HANDLE hPrivDataInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXSetBreakpointOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&hPrivDataInt,
				       hPrivData, PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA, IMG_TRUE);
	if (unlikely(psRGXSetBreakpointOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXSetBreakpoint_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXSetBreakpointOUT->eError =
	    PVRSRVRGXSetBreakpointKM(psConnection, OSGetDevNode(psConnection),
				     hPrivDataInt,
				     psRGXSetBreakpointIN->eFWDataMaster,
				     psRGXSetBreakpointIN->ui64TempSpillingAddr,
				     psRGXSetBreakpointIN->ui32BreakpointAddr,
				     psRGXSetBreakpointIN->ui32HandlerAddr,
				     psRGXSetBreakpointIN->ui32DM);

RGXSetBreakpoint_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (hPrivDataInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hPrivData, PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXClearBreakpoint(IMG_UINT32 ui32DispatchTableEntry,
			       IMG_UINT8 * psRGXClearBreakpointIN_UI8,
			       IMG_UINT8 * psRGXClearBreakpointOUT_UI8,
			       CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXCLEARBREAKPOINT *psRGXClearBreakpointIN =
	    (PVRSRV_BRIDGE_IN_RGXCLEARBREAKPOINT *) IMG_OFFSET_ADDR(psRGXClearBreakpointIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXCLEARBREAKPOINT *psRGXClearBreakpointOUT =
	    (PVRSRV_BRIDGE_OUT_RGXCLEARBREAKPOINT *) IMG_OFFSET_ADDR(psRGXClearBreakpointOUT_UI8,
								     0);

	IMG_HANDLE hPrivData = psRGXClearBreakpointIN->hPrivData;
	IMG_HANDLE hPrivDataInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXClearBreakpointOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&hPrivDataInt,
				       hPrivData, PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA, IMG_TRUE);
	if (unlikely(psRGXClearBreakpointOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXClearBreakpoint_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXClearBreakpointOUT->eError =
	    PVRSRVRGXClearBreakpointKM(psConnection, OSGetDevNode(psConnection), hPrivDataInt);

RGXClearBreakpoint_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (hPrivDataInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hPrivData, PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXEnableBreakpoint(IMG_UINT32 ui32DispatchTableEntry,
				IMG_UINT8 * psRGXEnableBreakpointIN_UI8,
				IMG_UINT8 * psRGXEnableBreakpointOUT_UI8,
				CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXENABLEBREAKPOINT *psRGXEnableBreakpointIN =
	    (PVRSRV_BRIDGE_IN_RGXENABLEBREAKPOINT *) IMG_OFFSET_ADDR(psRGXEnableBreakpointIN_UI8,
								     0);
	PVRSRV_BRIDGE_OUT_RGXENABLEBREAKPOINT *psRGXEnableBreakpointOUT =
	    (PVRSRV_BRIDGE_OUT_RGXENABLEBREAKPOINT *) IMG_OFFSET_ADDR(psRGXEnableBreakpointOUT_UI8,
								      0);

	IMG_HANDLE hPrivData = psRGXEnableBreakpointIN->hPrivData;
	IMG_HANDLE hPrivDataInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXEnableBreakpointOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&hPrivDataInt,
				       hPrivData, PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA, IMG_TRUE);
	if (unlikely(psRGXEnableBreakpointOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXEnableBreakpoint_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXEnableBreakpointOUT->eError =
	    PVRSRVRGXEnableBreakpointKM(psConnection, OSGetDevNode(psConnection), hPrivDataInt);

RGXEnableBreakpoint_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (hPrivDataInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hPrivData, PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXDisableBreakpoint(IMG_UINT32 ui32DispatchTableEntry,
				 IMG_UINT8 * psRGXDisableBreakpointIN_UI8,
				 IMG_UINT8 * psRGXDisableBreakpointOUT_UI8,
				 CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXDISABLEBREAKPOINT *psRGXDisableBreakpointIN =
	    (PVRSRV_BRIDGE_IN_RGXDISABLEBREAKPOINT *) IMG_OFFSET_ADDR(psRGXDisableBreakpointIN_UI8,
								      0);
	PVRSRV_BRIDGE_OUT_RGXDISABLEBREAKPOINT *psRGXDisableBreakpointOUT =
	    (PVRSRV_BRIDGE_OUT_RGXDISABLEBREAKPOINT *)
	    IMG_OFFSET_ADDR(psRGXDisableBreakpointOUT_UI8, 0);

	IMG_HANDLE hPrivData = psRGXDisableBreakpointIN->hPrivData;
	IMG_HANDLE hPrivDataInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXDisableBreakpointOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&hPrivDataInt,
				       hPrivData, PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA, IMG_TRUE);
	if (unlikely(psRGXDisableBreakpointOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXDisableBreakpoint_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXDisableBreakpointOUT->eError =
	    PVRSRVRGXDisableBreakpointKM(psConnection, OSGetDevNode(psConnection), hPrivDataInt);

RGXDisableBreakpoint_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (hPrivDataInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hPrivData, PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXOverallocateBPRegisters(IMG_UINT32 ui32DispatchTableEntry,
				       IMG_UINT8 * psRGXOverallocateBPRegistersIN_UI8,
				       IMG_UINT8 * psRGXOverallocateBPRegistersOUT_UI8,
				       CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXOVERALLOCATEBPREGISTERS *psRGXOverallocateBPRegistersIN =
	    (PVRSRV_BRIDGE_IN_RGXOVERALLOCATEBPREGISTERS *)
	    IMG_OFFSET_ADDR(psRGXOverallocateBPRegistersIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXOVERALLOCATEBPREGISTERS *psRGXOverallocateBPRegistersOUT =
	    (PVRSRV_BRIDGE_OUT_RGXOVERALLOCATEBPREGISTERS *)
	    IMG_OFFSET_ADDR(psRGXOverallocateBPRegistersOUT_UI8, 0);

	psRGXOverallocateBPRegistersOUT->eError =
	    PVRSRVRGXOverallocateBPRegistersKM(psConnection, OSGetDevNode(psConnection),
					       psRGXOverallocateBPRegistersIN->ui32TempRegs,
					       psRGXOverallocateBPRegistersIN->ui32SharedRegs);

	return 0;
}

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

#endif /* EXCLUDE_RGXBREAKPOINT_BRIDGE */

#if !defined(EXCLUDE_RGXBREAKPOINT_BRIDGE)
PVRSRV_ERROR InitRGXBREAKPOINTBridge(void);
void DeinitRGXBREAKPOINTBridge(void);

/*
 * Register all RGXBREAKPOINT functions with services
 */
PVRSRV_ERROR InitRGXBREAKPOINTBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXBREAKPOINT,
			      PVRSRV_BRIDGE_RGXBREAKPOINT_RGXSETBREAKPOINT,
			      PVRSRVBridgeRGXSetBreakpoint, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXBREAKPOINT,
			      PVRSRV_BRIDGE_RGXBREAKPOINT_RGXCLEARBREAKPOINT,
			      PVRSRVBridgeRGXClearBreakpoint, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXBREAKPOINT,
			      PVRSRV_BRIDGE_RGXBREAKPOINT_RGXENABLEBREAKPOINT,
			      PVRSRVBridgeRGXEnableBreakpoint, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXBREAKPOINT,
			      PVRSRV_BRIDGE_RGXBREAKPOINT_RGXDISABLEBREAKPOINT,
			      PVRSRVBridgeRGXDisableBreakpoint, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXBREAKPOINT,
			      PVRSRV_BRIDGE_RGXBREAKPOINT_RGXOVERALLOCATEBPREGISTERS,
			      PVRSRVBridgeRGXOverallocateBPRegisters, NULL);

	return PVRSRV_OK;
}

/*
 * Unregister all rgxbreakpoint functions with services
 */
void DeinitRGXBREAKPOINTBridge(void)
{

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXBREAKPOINT,
				PVRSRV_BRIDGE_RGXBREAKPOINT_RGXSETBREAKPOINT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXBREAKPOINT,
				PVRSRV_BRIDGE_RGXBREAKPOINT_RGXCLEARBREAKPOINT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXBREAKPOINT,
				PVRSRV_BRIDGE_RGXBREAKPOINT_RGXENABLEBREAKPOINT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXBREAKPOINT,
				PVRSRV_BRIDGE_RGXBREAKPOINT_RGXDISABLEBREAKPOINT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXBREAKPOINT,
				PVRSRV_BRIDGE_RGXBREAKPOINT_RGXOVERALLOCATEBPREGISTERS);

}
#else /* EXCLUDE_RGXBREAKPOINT_BRIDGE */
/* This bridge is conditional on EXCLUDE_RGXBREAKPOINT_BRIDGE - when defined,
 * do not populate the dispatch table with its functions
 */
#define InitRGXBREAKPOINTBridge() \
	PVRSRV_OK

#define DeinitRGXBREAKPOINTBridge()

#endif /* EXCLUDE_RGXBREAKPOINT_BRIDGE */
