/*************************************************************************/ /*!
@File
@Title          Server bridge for debugmisc
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for debugmisc
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

#include "devicemem_server.h"
#include "debugmisc_server.h"
#include "pmr.h"
#include "physmem_osmem.h"


#include "common_debugmisc_bridge.h"

#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#include "rgx_bridge.h"
#include "srvcore.h"
#include "handle.h"

#if defined (SUPPORT_AUTH)
#include "osauth.h"
#endif

#include <linux/slab.h>

/* ***************************************************************************
 * Bridge proxy functions
 */



/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeDebugMiscSLCSetBypassState(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DEBUGMISCSLCSETBYPASSSTATE *psDebugMiscSLCSetBypassStateIN,
					 PVRSRV_BRIDGE_OUT_DEBUGMISCSLCSETBYPASSSTATE *psDebugMiscSLCSetBypassStateOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DEBUGMISC_DEBUGMISCSLCSETBYPASSSTATE);





				{
					/* Look up the address from the handle */
					psDebugMiscSLCSetBypassStateOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevNodeInt,
											psDebugMiscSLCSetBypassStateIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psDebugMiscSLCSetBypassStateOUT->eError != PVRSRV_OK)
					{
						goto DebugMiscSLCSetBypassState_exit;
					}

				}

	psDebugMiscSLCSetBypassStateOUT->eError =
		PVRSRVDebugMiscSLCSetBypassStateKM(
					hDevNodeInt,
					psDebugMiscSLCSetBypassStateIN->ui32Flags,
					psDebugMiscSLCSetBypassStateIN->bIsBypassed);



DebugMiscSLCSetBypassState_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXDebugMiscSetFWLog(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXDEBUGMISCSETFWLOG *psRGXDebugMiscSetFWLogIN,
					 PVRSRV_BRIDGE_OUT_RGXDEBUGMISCSETFWLOG *psRGXDebugMiscSetFWLogOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DEBUGMISC_RGXDEBUGMISCSETFWLOG);





				{
					/* Look up the address from the handle */
					psRGXDebugMiscSetFWLogOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevNodeInt,
											psRGXDebugMiscSetFWLogIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXDebugMiscSetFWLogOUT->eError != PVRSRV_OK)
					{
						goto RGXDebugMiscSetFWLog_exit;
					}

				}

	psRGXDebugMiscSetFWLogOUT->eError =
		PVRSRVRGXDebugMiscSetFWLogKM(
					hDevNodeInt,
					psRGXDebugMiscSetFWLogIN->ui32RGXFWLogType);



RGXDebugMiscSetFWLog_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXDebugMiscDumpFreelistPageList(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXDEBUGMISCDUMPFREELISTPAGELIST *psRGXDebugMiscDumpFreelistPageListIN,
					 PVRSRV_BRIDGE_OUT_RGXDEBUGMISCDUMPFREELISTPAGELIST *psRGXDebugMiscDumpFreelistPageListOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DEBUGMISC_RGXDEBUGMISCDUMPFREELISTPAGELIST);





				{
					/* Look up the address from the handle */
					psRGXDebugMiscDumpFreelistPageListOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevNodeInt,
											psRGXDebugMiscDumpFreelistPageListIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXDebugMiscDumpFreelistPageListOUT->eError != PVRSRV_OK)
					{
						goto RGXDebugMiscDumpFreelistPageList_exit;
					}

				}

	psRGXDebugMiscDumpFreelistPageListOUT->eError =
		PVRSRVRGXDebugMiscDumpFreelistPageListKM(
					hDevNodeInt);



RGXDebugMiscDumpFreelistPageList_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgePhysmemImportSecBuf(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_PHYSMEMIMPORTSECBUF *psPhysmemImportSecBufIN,
					 PVRSRV_BRIDGE_OUT_PHYSMEMIMPORTSECBUF *psPhysmemImportSecBufOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;
	PMR * psPMRPtrInt = IMG_NULL;
	IMG_HANDLE hPMRPtrInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DEBUGMISC_PHYSMEMIMPORTSECBUF);





				{
					/* Look up the address from the handle */
					psPhysmemImportSecBufOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevNodeInt,
											psPhysmemImportSecBufIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psPhysmemImportSecBufOUT->eError != PVRSRV_OK)
					{
						goto PhysmemImportSecBuf_exit;
					}

				}

	psPhysmemImportSecBufOUT->eError =
		PhysmemNewTDSecureBufPMR(
					hDevNodeInt,
					psPhysmemImportSecBufIN->uiSize,
					psPhysmemImportSecBufIN->ui32Log2PageSize,
					psPhysmemImportSecBufIN->uiFlags,
					&psPMRPtrInt);
	/* Exit early if bridged call fails */
	if(psPhysmemImportSecBufOUT->eError != PVRSRV_OK)
	{
		goto PhysmemImportSecBuf_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hPMRPtrInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_PMR,
												psPMRPtrInt,
												(RESMAN_FREE_FN)&PMRUnrefPMR);
	if (hPMRPtrInt2 == IMG_NULL)
	{
		psPhysmemImportSecBufOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto PhysmemImportSecBuf_exit;
	}
	psPhysmemImportSecBufOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psPhysmemImportSecBufOUT->hPMRPtr,
							(IMG_HANDLE) hPMRPtrInt2,
							PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psPhysmemImportSecBufOUT->eError != PVRSRV_OK)
	{
		goto PhysmemImportSecBuf_exit;
	}


PhysmemImportSecBuf_exit:
	if (psPhysmemImportSecBufOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hPMRPtrInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hPMRPtrInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psPMRPtrInt)
		{
			PMRUnrefPMR(psPMRPtrInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgePowMonTestIoctl(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_POWMONTESTIOCTL *psPowMonTestIoctlIN,
					 PVRSRV_BRIDGE_OUT_POWMONTESTIOCTL *psPowMonTestIoctlOUT,
					 CONNECTION_DATA *psConnection)
{

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DEBUGMISC_POWMONTESTIOCTL);

	PVR_UNREFERENCED_PARAMETER(psConnection);




	psPowMonTestIoctlOUT->eError =
		PowMonTestIoctlKM(
					psPowMonTestIoctlIN->ui32Cmd,
					psPowMonTestIoctlIN->ui32In1,
					psPowMonTestIoctlIN->ui32In2,
					&psPowMonTestIoctlOUT->ui32Out1,
					&psPowMonTestIoctlOUT->ui32Out2);




	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */
 
PVRSRV_ERROR RegisterDEBUGMISCFunctions(IMG_VOID);
IMG_VOID UnregisterDEBUGMISCFunctions(IMG_VOID);

/*
 * Register all DEBUGMISC functions with services
 */
PVRSRV_ERROR RegisterDEBUGMISCFunctions(IMG_VOID)
{
	SetDispatchTableEntry(PVRSRV_BRIDGE_DEBUGMISC_DEBUGMISCSLCSETBYPASSSTATE, PVRSRVBridgeDebugMiscSLCSetBypassState);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DEBUGMISC_RGXDEBUGMISCSETFWLOG, PVRSRVBridgeRGXDebugMiscSetFWLog);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DEBUGMISC_RGXDEBUGMISCDUMPFREELISTPAGELIST, PVRSRVBridgeRGXDebugMiscDumpFreelistPageList);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DEBUGMISC_PHYSMEMIMPORTSECBUF, PVRSRVBridgePhysmemImportSecBuf);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DEBUGMISC_POWMONTESTIOCTL, PVRSRVBridgePowMonTestIoctl);

	return PVRSRV_OK;
}

/*
 * Unregister all debugmisc functions with services
 */
IMG_VOID UnregisterDEBUGMISCFunctions(IMG_VOID)
{
}
