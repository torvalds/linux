/*************************************************************************/ /*!
@File
@Title          Server bridge for breakpoint
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for breakpoint
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

#include <stddef.h>
#include <asm/uaccess.h>

#include "img_defs.h"

#include "rgxbreakpoint.h"


#include "common_breakpoint_bridge.h"

#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#include "rgx_bridge.h"
#include "srvcore.h"
#include "handle.h"

#include <linux/slab.h>



#if !defined(EXCLUDE_BREAKPOINT_BRIDGE)



/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeRGXSetBreakpoint(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXSETBREAKPOINT *psRGXSetBreakpointIN,
					  PVRSRV_BRIDGE_OUT_RGXSETBREAKPOINT *psRGXSetBreakpointOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPrivData = psRGXSetBreakpointIN->hPrivData;
	IMG_HANDLE hPrivDataInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXSetBreakpointOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &hPrivDataInt,
											hPrivData,
											PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA,
											IMG_TRUE);
					if(psRGXSetBreakpointOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXSetBreakpoint_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXSetBreakpointOUT->eError =
		PVRSRVRGXSetBreakpointKM(psConnection, OSGetDevData(psConnection),
					hPrivDataInt,
					psRGXSetBreakpointIN->eFWDataMaster,
					psRGXSetBreakpointIN->ui32BreakpointAddr,
					psRGXSetBreakpointIN->ui32HandlerAddr,
					psRGXSetBreakpointIN->ui32DM);




RGXSetBreakpoint_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(hPrivDataInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hPrivData,
											PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();


	return 0;
}


static IMG_INT
PVRSRVBridgeRGXClearBreakpoint(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCLEARBREAKPOINT *psRGXClearBreakpointIN,
					  PVRSRV_BRIDGE_OUT_RGXCLEARBREAKPOINT *psRGXClearBreakpointOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPrivData = psRGXClearBreakpointIN->hPrivData;
	IMG_HANDLE hPrivDataInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXClearBreakpointOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &hPrivDataInt,
											hPrivData,
											PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA,
											IMG_TRUE);
					if(psRGXClearBreakpointOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXClearBreakpoint_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXClearBreakpointOUT->eError =
		PVRSRVRGXClearBreakpointKM(psConnection, OSGetDevData(psConnection),
					hPrivDataInt);




RGXClearBreakpoint_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(hPrivDataInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hPrivData,
											PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();


	return 0;
}


static IMG_INT
PVRSRVBridgeRGXEnableBreakpoint(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXENABLEBREAKPOINT *psRGXEnableBreakpointIN,
					  PVRSRV_BRIDGE_OUT_RGXENABLEBREAKPOINT *psRGXEnableBreakpointOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPrivData = psRGXEnableBreakpointIN->hPrivData;
	IMG_HANDLE hPrivDataInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXEnableBreakpointOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &hPrivDataInt,
											hPrivData,
											PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA,
											IMG_TRUE);
					if(psRGXEnableBreakpointOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXEnableBreakpoint_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXEnableBreakpointOUT->eError =
		PVRSRVRGXEnableBreakpointKM(psConnection, OSGetDevData(psConnection),
					hPrivDataInt);




RGXEnableBreakpoint_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(hPrivDataInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hPrivData,
											PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();


	return 0;
}


static IMG_INT
PVRSRVBridgeRGXDisableBreakpoint(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXDISABLEBREAKPOINT *psRGXDisableBreakpointIN,
					  PVRSRV_BRIDGE_OUT_RGXDISABLEBREAKPOINT *psRGXDisableBreakpointOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPrivData = psRGXDisableBreakpointIN->hPrivData;
	IMG_HANDLE hPrivDataInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXDisableBreakpointOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &hPrivDataInt,
											hPrivData,
											PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA,
											IMG_TRUE);
					if(psRGXDisableBreakpointOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXDisableBreakpoint_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXDisableBreakpointOUT->eError =
		PVRSRVRGXDisableBreakpointKM(psConnection, OSGetDevData(psConnection),
					hPrivDataInt);




RGXDisableBreakpoint_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(hPrivDataInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hPrivData,
											PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();


	return 0;
}


static IMG_INT
PVRSRVBridgeRGXOverallocateBPRegisters(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXOVERALLOCATEBPREGISTERS *psRGXOverallocateBPRegistersIN,
					  PVRSRV_BRIDGE_OUT_RGXOVERALLOCATEBPREGISTERS *psRGXOverallocateBPRegistersOUT,
					 CONNECTION_DATA *psConnection)
{








	psRGXOverallocateBPRegistersOUT->eError =
		PVRSRVRGXOverallocateBPRegistersKM(psConnection, OSGetDevData(psConnection),
					psRGXOverallocateBPRegistersIN->ui32TempRegs,
					psRGXOverallocateBPRegistersIN->ui32SharedRegs);








	return 0;
}




/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */

static IMG_BOOL bUseLock = IMG_TRUE;
#endif /* EXCLUDE_BREAKPOINT_BRIDGE */

#if !defined(EXCLUDE_BREAKPOINT_BRIDGE)
PVRSRV_ERROR InitBREAKPOINTBridge(void);
PVRSRV_ERROR DeinitBREAKPOINTBridge(void);

/*
 * Register all BREAKPOINT functions with services
 */
PVRSRV_ERROR InitBREAKPOINTBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_BREAKPOINT, PVRSRV_BRIDGE_BREAKPOINT_RGXSETBREAKPOINT, PVRSRVBridgeRGXSetBreakpoint,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_BREAKPOINT, PVRSRV_BRIDGE_BREAKPOINT_RGXCLEARBREAKPOINT, PVRSRVBridgeRGXClearBreakpoint,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_BREAKPOINT, PVRSRV_BRIDGE_BREAKPOINT_RGXENABLEBREAKPOINT, PVRSRVBridgeRGXEnableBreakpoint,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_BREAKPOINT, PVRSRV_BRIDGE_BREAKPOINT_RGXDISABLEBREAKPOINT, PVRSRVBridgeRGXDisableBreakpoint,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_BREAKPOINT, PVRSRV_BRIDGE_BREAKPOINT_RGXOVERALLOCATEBPREGISTERS, PVRSRVBridgeRGXOverallocateBPRegisters,
					NULL, bUseLock);


	return PVRSRV_OK;
}

/*
 * Unregister all breakpoint functions with services
 */
PVRSRV_ERROR DeinitBREAKPOINTBridge(void)
{
	return PVRSRV_OK;
}
#else /* EXCLUDE_BREAKPOINT_BRIDGE */
/* This bridge is conditional on EXCLUDE_BREAKPOINT_BRIDGE - when defined,
 * do not populate the dispatch table with its functions
 */
#define InitBREAKPOINTBridge() \
	PVRSRV_OK

#define DeinitBREAKPOINTBridge() \
	PVRSRV_OK

#endif /* EXCLUDE_BREAKPOINT_BRIDGE */
