/*************************************************************************/ /*!
@File
@Title          Server bridge for ri
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for ri
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

#include "ri_server.h"


#include "common_ri_bridge.h"

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

static PVRSRV_ERROR
RIDeleteMEMDESCEntryResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}



/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeRIWritePMREntry(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RIWRITEPMRENTRY *psRIWritePMREntryIN,
					 PVRSRV_BRIDGE_OUT_RIWRITEPMRENTRY *psRIWritePMREntryOUT,
					 CONNECTION_DATA *psConnection)
{
	PMR * psPMRHandleInt = IMG_NULL;
	IMG_HANDLE hPMRHandleInt2 = IMG_NULL;
	IMG_CHAR *uiTextAInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RI_RIWRITEPMRENTRY);




	if (psRIWritePMREntryIN->ui32TextASize != 0)
	{
		uiTextAInt = OSAllocMem(psRIWritePMREntryIN->ui32TextASize * sizeof(IMG_CHAR));
		if (!uiTextAInt)
		{
			psRIWritePMREntryOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RIWritePMREntry_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRIWritePMREntryIN->puiTextA, psRIWritePMREntryIN->ui32TextASize * sizeof(IMG_CHAR))
				|| (OSCopyFromUser(NULL, uiTextAInt, psRIWritePMREntryIN->puiTextA,
				psRIWritePMREntryIN->ui32TextASize * sizeof(IMG_CHAR)) != PVRSRV_OK) )
			{
				psRIWritePMREntryOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RIWritePMREntry_exit;
			}

				{
					/* Look up the address from the handle */
					psRIWritePMREntryOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hPMRHandleInt2,
											psRIWritePMREntryIN->hPMRHandle,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psRIWritePMREntryOUT->eError != PVRSRV_OK)
					{
						goto RIWritePMREntry_exit;
					}

					/* Look up the data from the resman address */
					psRIWritePMREntryOUT->eError = ResManFindPrivateDataByPtr(hPMRHandleInt2, (IMG_VOID **) &psPMRHandleInt);

					if(psRIWritePMREntryOUT->eError != PVRSRV_OK)
					{
						goto RIWritePMREntry_exit;
					}
				}

	psRIWritePMREntryOUT->eError =
		RIWritePMREntryKM(
					psPMRHandleInt,
					psRIWritePMREntryIN->ui32TextASize,
					uiTextAInt,
					psRIWritePMREntryIN->uiLogicalSize);



RIWritePMREntry_exit:
	if (uiTextAInt)
		OSFreeMem(uiTextAInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeRIWriteMEMDESCEntry(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RIWRITEMEMDESCENTRY *psRIWriteMEMDESCEntryIN,
					 PVRSRV_BRIDGE_OUT_RIWRITEMEMDESCENTRY *psRIWriteMEMDESCEntryOUT,
					 CONNECTION_DATA *psConnection)
{
	PMR * psPMRHandleInt = IMG_NULL;
	IMG_HANDLE hPMRHandleInt2 = IMG_NULL;
	IMG_CHAR *uiTextBInt = IMG_NULL;
	RI_HANDLE psRIHandleInt = IMG_NULL;
	IMG_HANDLE hRIHandleInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RI_RIWRITEMEMDESCENTRY);




	if (psRIWriteMEMDESCEntryIN->ui32TextBSize != 0)
	{
		uiTextBInt = OSAllocMem(psRIWriteMEMDESCEntryIN->ui32TextBSize * sizeof(IMG_CHAR));
		if (!uiTextBInt)
		{
			psRIWriteMEMDESCEntryOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RIWriteMEMDESCEntry_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRIWriteMEMDESCEntryIN->puiTextB, psRIWriteMEMDESCEntryIN->ui32TextBSize * sizeof(IMG_CHAR))
				|| (OSCopyFromUser(NULL, uiTextBInt, psRIWriteMEMDESCEntryIN->puiTextB,
				psRIWriteMEMDESCEntryIN->ui32TextBSize * sizeof(IMG_CHAR)) != PVRSRV_OK) )
			{
				psRIWriteMEMDESCEntryOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RIWriteMEMDESCEntry_exit;
			}

				{
					/* Look up the address from the handle */
					psRIWriteMEMDESCEntryOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hPMRHandleInt2,
											psRIWriteMEMDESCEntryIN->hPMRHandle,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psRIWriteMEMDESCEntryOUT->eError != PVRSRV_OK)
					{
						goto RIWriteMEMDESCEntry_exit;
					}

					/* Look up the data from the resman address */
					psRIWriteMEMDESCEntryOUT->eError = ResManFindPrivateDataByPtr(hPMRHandleInt2, (IMG_VOID **) &psPMRHandleInt);

					if(psRIWriteMEMDESCEntryOUT->eError != PVRSRV_OK)
					{
						goto RIWriteMEMDESCEntry_exit;
					}
				}

	psRIWriteMEMDESCEntryOUT->eError =
		RIWriteMEMDESCEntryKM(
					psPMRHandleInt,
					psRIWriteMEMDESCEntryIN->ui32TextBSize,
					uiTextBInt,
					psRIWriteMEMDESCEntryIN->uiOffset,
					psRIWriteMEMDESCEntryIN->uiSize,
					psRIWriteMEMDESCEntryIN->bIsImport,
					psRIWriteMEMDESCEntryIN->bIsExportable,
					&psRIHandleInt);
	/* Exit early if bridged call fails */
	if(psRIWriteMEMDESCEntryOUT->eError != PVRSRV_OK)
	{
		goto RIWriteMEMDESCEntry_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hRIHandleInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_RI_HANDLE,
												psRIHandleInt,
												(RESMAN_FREE_FN)&RIDeleteMEMDESCEntryKM);
	if (hRIHandleInt2 == IMG_NULL)
	{
		psRIWriteMEMDESCEntryOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto RIWriteMEMDESCEntry_exit;
	}
	psRIWriteMEMDESCEntryOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psRIWriteMEMDESCEntryOUT->hRIHandle,
							(IMG_HANDLE) hRIHandleInt2,
							PVRSRV_HANDLE_TYPE_RI_HANDLE,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psRIWriteMEMDESCEntryOUT->eError != PVRSRV_OK)
	{
		goto RIWriteMEMDESCEntry_exit;
	}


RIWriteMEMDESCEntry_exit:
	if (psRIWriteMEMDESCEntryOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hRIHandleInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hRIHandleInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psRIHandleInt)
		{
			RIDeleteMEMDESCEntryKM(psRIHandleInt);
		}
	}

	if (uiTextBInt)
		OSFreeMem(uiTextBInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeRIUpdateMEMDESCAddr(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RIUPDATEMEMDESCADDR *psRIUpdateMEMDESCAddrIN,
					 PVRSRV_BRIDGE_OUT_RIUPDATEMEMDESCADDR *psRIUpdateMEMDESCAddrOUT,
					 CONNECTION_DATA *psConnection)
{
	RI_HANDLE psRIHandleInt = IMG_NULL;
	IMG_HANDLE hRIHandleInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RI_RIUPDATEMEMDESCADDR);





				{
					/* Look up the address from the handle */
					psRIUpdateMEMDESCAddrOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hRIHandleInt2,
											psRIUpdateMEMDESCAddrIN->hRIHandle,
											PVRSRV_HANDLE_TYPE_RI_HANDLE);
					if(psRIUpdateMEMDESCAddrOUT->eError != PVRSRV_OK)
					{
						goto RIUpdateMEMDESCAddr_exit;
					}

					/* Look up the data from the resman address */
					psRIUpdateMEMDESCAddrOUT->eError = ResManFindPrivateDataByPtr(hRIHandleInt2, (IMG_VOID **) &psRIHandleInt);

					if(psRIUpdateMEMDESCAddrOUT->eError != PVRSRV_OK)
					{
						goto RIUpdateMEMDESCAddr_exit;
					}
				}

	psRIUpdateMEMDESCAddrOUT->eError =
		RIUpdateMEMDESCAddrKM(
					psRIHandleInt,
					psRIUpdateMEMDESCAddrIN->sAddr);



RIUpdateMEMDESCAddr_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRIDeleteMEMDESCEntry(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RIDELETEMEMDESCENTRY *psRIDeleteMEMDESCEntryIN,
					 PVRSRV_BRIDGE_OUT_RIDELETEMEMDESCENTRY *psRIDeleteMEMDESCEntryOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hRIHandleInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RI_RIDELETEMEMDESCENTRY);





				{
					/* Look up the address from the handle */
					psRIDeleteMEMDESCEntryOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hRIHandleInt2,
											psRIDeleteMEMDESCEntryIN->hRIHandle,
											PVRSRV_HANDLE_TYPE_RI_HANDLE);
					if(psRIDeleteMEMDESCEntryOUT->eError != PVRSRV_OK)
					{
						goto RIDeleteMEMDESCEntry_exit;
					}

				}

	psRIDeleteMEMDESCEntryOUT->eError = RIDeleteMEMDESCEntryResManProxy(hRIHandleInt2);
	/* Exit early if bridged call fails */
	if(psRIDeleteMEMDESCEntryOUT->eError != PVRSRV_OK)
	{
		goto RIDeleteMEMDESCEntry_exit;
	}

	psRIDeleteMEMDESCEntryOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRIDeleteMEMDESCEntryIN->hRIHandle,
					PVRSRV_HANDLE_TYPE_RI_HANDLE);


RIDeleteMEMDESCEntry_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRIDumpList(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RIDUMPLIST *psRIDumpListIN,
					 PVRSRV_BRIDGE_OUT_RIDUMPLIST *psRIDumpListOUT,
					 CONNECTION_DATA *psConnection)
{
	PMR * psPMRHandleInt = IMG_NULL;
	IMG_HANDLE hPMRHandleInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RI_RIDUMPLIST);





				{
					/* Look up the address from the handle */
					psRIDumpListOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hPMRHandleInt2,
											psRIDumpListIN->hPMRHandle,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psRIDumpListOUT->eError != PVRSRV_OK)
					{
						goto RIDumpList_exit;
					}

					/* Look up the data from the resman address */
					psRIDumpListOUT->eError = ResManFindPrivateDataByPtr(hPMRHandleInt2, (IMG_VOID **) &psPMRHandleInt);

					if(psRIDumpListOUT->eError != PVRSRV_OK)
					{
						goto RIDumpList_exit;
					}
				}

	psRIDumpListOUT->eError =
		RIDumpListKM(
					psPMRHandleInt);



RIDumpList_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRIDumpAll(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RIDUMPALL *psRIDumpAllIN,
					 PVRSRV_BRIDGE_OUT_RIDUMPALL *psRIDumpAllOUT,
					 CONNECTION_DATA *psConnection)
{

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RI_RIDUMPALL);

	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psRIDumpAllIN);




	psRIDumpAllOUT->eError =
		RIDumpAllKM(
					);




	return 0;
}

static IMG_INT
PVRSRVBridgeRIDumpProcess(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RIDUMPPROCESS *psRIDumpProcessIN,
					 PVRSRV_BRIDGE_OUT_RIDUMPPROCESS *psRIDumpProcessOUT,
					 CONNECTION_DATA *psConnection)
{

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RI_RIDUMPPROCESS);

	PVR_UNREFERENCED_PARAMETER(psConnection);




	psRIDumpProcessOUT->eError =
		RIDumpProcessKM(
					psRIDumpProcessIN->ui32Pid);




	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */
 
PVRSRV_ERROR RegisterRIFunctions(IMG_VOID);
IMG_VOID UnregisterRIFunctions(IMG_VOID);

/*
 * Register all RI functions with services
 */
PVRSRV_ERROR RegisterRIFunctions(IMG_VOID)
{
	SetDispatchTableEntry(PVRSRV_BRIDGE_RI_RIWRITEPMRENTRY, PVRSRVBridgeRIWritePMREntry);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RI_RIWRITEMEMDESCENTRY, PVRSRVBridgeRIWriteMEMDESCEntry);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RI_RIUPDATEMEMDESCADDR, PVRSRVBridgeRIUpdateMEMDESCAddr);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RI_RIDELETEMEMDESCENTRY, PVRSRVBridgeRIDeleteMEMDESCEntry);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RI_RIDUMPLIST, PVRSRVBridgeRIDumpList);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RI_RIDUMPALL, PVRSRVBridgeRIDumpAll);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RI_RIDUMPPROCESS, PVRSRVBridgeRIDumpProcess);

	return PVRSRV_OK;
}

/*
 * Unregister all ri functions with services
 */
IMG_VOID UnregisterRIFunctions(IMG_VOID)
{
}
