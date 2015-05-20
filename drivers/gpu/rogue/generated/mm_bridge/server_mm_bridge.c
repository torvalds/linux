/*************************************************************************/ /*!
@File
@Title          Server bridge for mm
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for mm
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
#include "pmr.h"
#include "devicemem_heapcfg.h"
#include "physmem.h"


#include "common_mm_bridge.h"

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
PMRUnexportPMRResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}

static PVRSRV_ERROR
PMRUnmakeServerExportClientExportResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}

static PVRSRV_ERROR
DevmemIntCtxDestroyResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}

static PVRSRV_ERROR
DevmemIntHeapDestroyResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}

static PVRSRV_ERROR
DevmemIntUnmapPMRResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}

static PVRSRV_ERROR
DevmemIntUnreserveRangeResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}

static PVRSRV_ERROR
PMRUnrefPMRResManProxy(IMG_HANDLE hResmanItem)
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
PVRSRVBridgePMRExportPMR(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_PMREXPORTPMR *psPMRExportPMRIN,
					 PVRSRV_BRIDGE_OUT_PMREXPORTPMR *psPMRExportPMROUT,
					 CONNECTION_DATA *psConnection)
{
	PMR * psPMRInt = IMG_NULL;
	IMG_HANDLE hPMRInt2 = IMG_NULL;
	PMR_EXPORT * psPMRExportInt = IMG_NULL;
	IMG_HANDLE hPMRExportInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MM_PMREXPORTPMR);





	PMRLock();
				{
					/* Look up the address from the handle */
					psPMRExportPMROUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hPMRInt2,
											psPMRExportPMRIN->hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psPMRExportPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto PMRExportPMR_exit;
					}

					/* Look up the data from the resman address */
					psPMRExportPMROUT->eError = ResManFindPrivateDataByPtr(hPMRInt2, (IMG_VOID **) &psPMRInt);

					if(psPMRExportPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto PMRExportPMR_exit;
					}
				}

	psPMRExportPMROUT->eError =
		PMRExportPMR(
					psPMRInt,
					&psPMRExportInt,
					&psPMRExportPMROUT->ui64Size,
					&psPMRExportPMROUT->ui32Log2Contig,
					&psPMRExportPMROUT->ui64Password);
	/* Exit early if bridged call fails */
	if(psPMRExportPMROUT->eError != PVRSRV_OK)
	{
		PMRUnlock();
		goto PMRExportPMR_exit;
	}

	PMRUnlock();

	/* Create a resman item and overwrite the handle with it */
	hPMRExportInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_PMR_EXPORT,
												psPMRExportInt,
												(RESMAN_FREE_FN)&PMRUnexportPMR);
	if (hPMRExportInt2 == IMG_NULL)
	{
		psPMRExportPMROUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto PMRExportPMR_exit;
	}
	/* see if it's already exported */
	psPMRExportPMROUT->eError =
		PVRSRVFindHandle(KERNEL_HANDLE_BASE,
							&psPMRExportPMROUT->hPMRExport,
							(IMG_HANDLE) hPMRExportInt2,
							PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);
	if(psPMRExportPMROUT->eError == PVRSRV_OK)
	{
		/* It's already exported */
		return 0;
	}

	psPMRExportPMROUT->eError = PVRSRVAllocHandle(KERNEL_HANDLE_BASE,
							&psPMRExportPMROUT->hPMRExport,
							(IMG_HANDLE) hPMRExportInt2,
							PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psPMRExportPMROUT->eError != PVRSRV_OK)
	{
		goto PMRExportPMR_exit;
	}


PMRExportPMR_exit:
	if (psPMRExportPMROUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hPMRExportInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hPMRExportInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psPMRExportInt)
		{
			PMRUnexportPMR(psPMRExportInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgePMRUnexportPMR(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_PMRUNEXPORTPMR *psPMRUnexportPMRIN,
					 PVRSRV_BRIDGE_OUT_PMRUNEXPORTPMR *psPMRUnexportPMROUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPMRExportInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MM_PMRUNEXPORTPMR);

	PVR_UNREFERENCED_PARAMETER(psConnection);



	PMRLock();
				{
					/* Look up the address from the handle */
					psPMRUnexportPMROUT->eError =
						PVRSRVLookupHandle(KERNEL_HANDLE_BASE,
											(IMG_HANDLE *) &hPMRExportInt2,
											psPMRUnexportPMRIN->hPMRExport,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);
					if(psPMRUnexportPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto PMRUnexportPMR_exit;
					}

				}
	PMRUnlock();

	psPMRUnexportPMROUT->eError = PMRUnexportPMRResManProxy(hPMRExportInt2);
	/* Exit early if bridged call fails */
	if(psPMRUnexportPMROUT->eError != PVRSRV_OK)
	{
		goto PMRUnexportPMR_exit;
	}

	psPMRUnexportPMROUT->eError =
		PVRSRVReleaseHandle(KERNEL_HANDLE_BASE,
					(IMG_HANDLE) psPMRUnexportPMRIN->hPMRExport,
					PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);


PMRUnexportPMR_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgePMRGetUID(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_PMRGETUID *psPMRGetUIDIN,
					 PVRSRV_BRIDGE_OUT_PMRGETUID *psPMRGetUIDOUT,
					 CONNECTION_DATA *psConnection)
{
	PMR * psPMRInt = IMG_NULL;
	IMG_HANDLE hPMRInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MM_PMRGETUID);




	PMRLock();
				{
					/* Look up the address from the handle */
					psPMRGetUIDOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hPMRInt2,
											psPMRGetUIDIN->hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psPMRGetUIDOUT->eError != PVRSRV_OK)
					{
						goto PMRGetUID_exit;
					}

					/* Look up the data from the resman address */
					psPMRGetUIDOUT->eError = ResManFindPrivateDataByPtr(hPMRInt2, (IMG_VOID **) &psPMRInt);

					if(psPMRGetUIDOUT->eError != PVRSRV_OK)
					{
						goto PMRGetUID_exit;
					}
				}

	psPMRGetUIDOUT->eError =
		PMRGetUID(
					psPMRInt,
					&psPMRGetUIDOUT->ui64UID);



PMRGetUID_exit:
	PMRUnlock();

	return 0;
}

static IMG_INT
PVRSRVBridgePMRMakeServerExportClientExport(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_PMRMAKESERVEREXPORTCLIENTEXPORT *psPMRMakeServerExportClientExportIN,
					 PVRSRV_BRIDGE_OUT_PMRMAKESERVEREXPORTCLIENTEXPORT *psPMRMakeServerExportClientExportOUT,
					 CONNECTION_DATA *psConnection)
{
	DEVMEM_EXPORTCOOKIE * psPMRServerExportInt = IMG_NULL;
	PMR_EXPORT * psPMRExportOutInt = IMG_NULL;
	IMG_HANDLE hPMRExportOutInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MM_PMRMAKESERVEREXPORTCLIENTEXPORT);





				{
					/* Look up the address from the handle */
					psPMRMakeServerExportClientExportOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &psPMRServerExportInt,
											psPMRMakeServerExportClientExportIN->hPMRServerExport,
											PVRSRV_HANDLE_TYPE_SERVER_EXPORTCOOKIE);
					if(psPMRMakeServerExportClientExportOUT->eError != PVRSRV_OK)
					{
						goto PMRMakeServerExportClientExport_exit;
					}

				}

	psPMRMakeServerExportClientExportOUT->eError =
		PMRMakeServerExportClientExport(
					psPMRServerExportInt,
					&psPMRExportOutInt,
					&psPMRMakeServerExportClientExportOUT->ui64Size,
					&psPMRMakeServerExportClientExportOUT->ui32Log2Contig,
					&psPMRMakeServerExportClientExportOUT->ui64Password);
	/* Exit early if bridged call fails */
	if(psPMRMakeServerExportClientExportOUT->eError != PVRSRV_OK)
	{
		goto PMRMakeServerExportClientExport_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hPMRExportOutInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_PMR_EXPORT,
												psPMRExportOutInt,
												(RESMAN_FREE_FN)&PMRUnmakeServerExportClientExport);
	if (hPMRExportOutInt2 == IMG_NULL)
	{
		psPMRMakeServerExportClientExportOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto PMRMakeServerExportClientExport_exit;
	}
	/* see if it's already exported */
	psPMRMakeServerExportClientExportOUT->eError =
		PVRSRVFindHandle(KERNEL_HANDLE_BASE,
							&psPMRMakeServerExportClientExportOUT->hPMRExportOut,
							(IMG_HANDLE) hPMRExportOutInt2,
							PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);
	if(psPMRMakeServerExportClientExportOUT->eError == PVRSRV_OK)
	{
		/* It's already exported */
		return 0;
	}

	psPMRMakeServerExportClientExportOUT->eError = PVRSRVAllocHandle(KERNEL_HANDLE_BASE,
							&psPMRMakeServerExportClientExportOUT->hPMRExportOut,
							(IMG_HANDLE) hPMRExportOutInt2,
							PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psPMRMakeServerExportClientExportOUT->eError != PVRSRV_OK)
	{
		goto PMRMakeServerExportClientExport_exit;
	}


PMRMakeServerExportClientExport_exit:
	if (psPMRMakeServerExportClientExportOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hPMRExportOutInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hPMRExportOutInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psPMRExportOutInt)
		{
			PMRUnmakeServerExportClientExport(psPMRExportOutInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgePMRUnmakeServerExportClientExport(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_PMRUNMAKESERVEREXPORTCLIENTEXPORT *psPMRUnmakeServerExportClientExportIN,
					 PVRSRV_BRIDGE_OUT_PMRUNMAKESERVEREXPORTCLIENTEXPORT *psPMRUnmakeServerExportClientExportOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPMRExportInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MM_PMRUNMAKESERVEREXPORTCLIENTEXPORT);

	PVR_UNREFERENCED_PARAMETER(psConnection);




				{
					/* Look up the address from the handle */
					psPMRUnmakeServerExportClientExportOUT->eError =
						PVRSRVLookupHandle(KERNEL_HANDLE_BASE,
											(IMG_HANDLE *) &hPMRExportInt2,
											psPMRUnmakeServerExportClientExportIN->hPMRExport,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);
					if(psPMRUnmakeServerExportClientExportOUT->eError != PVRSRV_OK)
					{
						goto PMRUnmakeServerExportClientExport_exit;
					}

				}

	psPMRUnmakeServerExportClientExportOUT->eError = PMRUnmakeServerExportClientExportResManProxy(hPMRExportInt2);
	/* Exit early if bridged call fails */
	if(psPMRUnmakeServerExportClientExportOUT->eError != PVRSRV_OK)
	{
		goto PMRUnmakeServerExportClientExport_exit;
	}

	psPMRUnmakeServerExportClientExportOUT->eError =
		PVRSRVReleaseHandle(KERNEL_HANDLE_BASE,
					(IMG_HANDLE) psPMRUnmakeServerExportClientExportIN->hPMRExport,
					PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);


PMRUnmakeServerExportClientExport_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgePMRImportPMR(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_PMRIMPORTPMR *psPMRImportPMRIN,
					 PVRSRV_BRIDGE_OUT_PMRIMPORTPMR *psPMRImportPMROUT,
					 CONNECTION_DATA *psConnection)
{
	PMR_EXPORT * psPMRExportInt = IMG_NULL;
	IMG_HANDLE hPMRExportInt2 = IMG_NULL;
	PMR * psPMRInt = IMG_NULL;
	IMG_HANDLE hPMRInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MM_PMRIMPORTPMR);




#if defined (SUPPORT_AUTH)
	psPMRImportPMROUT->eError = OSCheckAuthentication(psConnection, 1);
	if (psPMRImportPMROUT->eError != PVRSRV_OK)
	{
		goto PMRImportPMR_exit;
	}
#endif

	PMRLock();
				{
					/* Look up the address from the handle */
					psPMRImportPMROUT->eError =
						PVRSRVLookupHandle(KERNEL_HANDLE_BASE,
											(IMG_HANDLE *) &hPMRExportInt2,
											psPMRImportPMRIN->hPMRExport,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);
					if(psPMRImportPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto PMRImportPMR_exit;
					}

					/* Look up the data from the resman address */
					psPMRImportPMROUT->eError = ResManFindPrivateDataByPtr(hPMRExportInt2, (IMG_VOID **) &psPMRExportInt);

					if(psPMRImportPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto PMRImportPMR_exit;
					}
				}

	psPMRImportPMROUT->eError =
		PMRImportPMR(
					psPMRExportInt,
					psPMRImportPMRIN->ui64uiPassword,
					psPMRImportPMRIN->ui64uiSize,
					psPMRImportPMRIN->ui32uiLog2Contig,
					&psPMRInt);
	/* Exit early if bridged call fails */
	if(psPMRImportPMROUT->eError != PVRSRV_OK)
	{
		PMRUnlock();
		goto PMRImportPMR_exit;
	}

	PMRUnlock();

	/* Create a resman item and overwrite the handle with it */
	hPMRInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_PMR,
												psPMRInt,
												(RESMAN_FREE_FN)&PMRUnrefPMR);
	if (hPMRInt2 == IMG_NULL)
	{
		psPMRImportPMROUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto PMRImportPMR_exit;
	}
	psPMRImportPMROUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psPMRImportPMROUT->hPMR,
							(IMG_HANDLE) hPMRInt2,
							PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psPMRImportPMROUT->eError != PVRSRV_OK)
	{
		goto PMRImportPMR_exit;
	}


PMRImportPMR_exit:
	if (psPMRImportPMROUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hPMRInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hPMRInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psPMRInt)
		{
			PMRUnrefPMR(psPMRInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIntCtxCreate(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DEVMEMINTCTXCREATE *psDevmemIntCtxCreateIN,
					 PVRSRV_BRIDGE_OUT_DEVMEMINTCTXCREATE *psDevmemIntCtxCreateOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDeviceNodeInt = IMG_NULL;
	DEVMEMINT_CTX * psDevMemServerContextInt = IMG_NULL;
	IMG_HANDLE hDevMemServerContextInt2 = IMG_NULL;
	IMG_HANDLE hPrivDataInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MM_DEVMEMINTCTXCREATE);



	psDevmemIntCtxCreateOUT->hDevMemServerContext = IMG_NULL;


				{
					/* Look up the address from the handle */
					psDevmemIntCtxCreateOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDeviceNodeInt,
											psDevmemIntCtxCreateIN->hDeviceNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psDevmemIntCtxCreateOUT->eError != PVRSRV_OK)
					{
						goto DevmemIntCtxCreate_exit;
					}

				}

	psDevmemIntCtxCreateOUT->eError =
		DevmemIntCtxCreate(
					hDeviceNodeInt,
					&psDevMemServerContextInt,
					&hPrivDataInt);
	/* Exit early if bridged call fails */
	if(psDevmemIntCtxCreateOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntCtxCreate_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hDevMemServerContextInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_DEVICEMEM2_CONTEXT,
												psDevMemServerContextInt,
												(RESMAN_FREE_FN)&DevmemIntCtxDestroy);
	if (hDevMemServerContextInt2 == IMG_NULL)
	{
		psDevmemIntCtxCreateOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto DevmemIntCtxCreate_exit;
	}
	psDevmemIntCtxCreateOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psDevmemIntCtxCreateOUT->hDevMemServerContext,
							(IMG_HANDLE) hDevMemServerContextInt2,
							PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psDevmemIntCtxCreateOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntCtxCreate_exit;
	}
	psDevmemIntCtxCreateOUT->eError = PVRSRVAllocSubHandle(psConnection->psHandleBase,
							&psDevmemIntCtxCreateOUT->hPrivData,
							(IMG_HANDLE) hPrivDataInt,
							PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,psDevmemIntCtxCreateOUT->hDevMemServerContext);
	if (psDevmemIntCtxCreateOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntCtxCreate_exit;
	}


DevmemIntCtxCreate_exit:
	if (psDevmemIntCtxCreateOUT->eError != PVRSRV_OK)
	{
		if (psDevmemIntCtxCreateOUT->hDevMemServerContext)
		{
			PVRSRVReleaseHandle(psConnection->psHandleBase,
						(IMG_HANDLE) psDevmemIntCtxCreateOUT->hDevMemServerContext,
						PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX);
		}

		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hDevMemServerContextInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hDevMemServerContextInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psDevMemServerContextInt)
		{
			DevmemIntCtxDestroy(psDevMemServerContextInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIntCtxDestroy(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DEVMEMINTCTXDESTROY *psDevmemIntCtxDestroyIN,
					 PVRSRV_BRIDGE_OUT_DEVMEMINTCTXDESTROY *psDevmemIntCtxDestroyOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevmemServerContextInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MM_DEVMEMINTCTXDESTROY);





				{
					/* Look up the address from the handle */
					psDevmemIntCtxDestroyOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevmemServerContextInt2,
											psDevmemIntCtxDestroyIN->hDevmemServerContext,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX);
					if(psDevmemIntCtxDestroyOUT->eError != PVRSRV_OK)
					{
						goto DevmemIntCtxDestroy_exit;
					}

				}

	psDevmemIntCtxDestroyOUT->eError = DevmemIntCtxDestroyResManProxy(hDevmemServerContextInt2);
	/* Exit early if bridged call fails */
	if(psDevmemIntCtxDestroyOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntCtxDestroy_exit;
	}

	psDevmemIntCtxDestroyOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psDevmemIntCtxDestroyIN->hDevmemServerContext,
					PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX);


DevmemIntCtxDestroy_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIntHeapCreate(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DEVMEMINTHEAPCREATE *psDevmemIntHeapCreateIN,
					 PVRSRV_BRIDGE_OUT_DEVMEMINTHEAPCREATE *psDevmemIntHeapCreateOUT,
					 CONNECTION_DATA *psConnection)
{
	DEVMEMINT_CTX * psDevmemCtxInt = IMG_NULL;
	IMG_HANDLE hDevmemCtxInt2 = IMG_NULL;
	DEVMEMINT_HEAP * psDevmemHeapPtrInt = IMG_NULL;
	IMG_HANDLE hDevmemHeapPtrInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MM_DEVMEMINTHEAPCREATE);





				{
					/* Look up the address from the handle */
					psDevmemIntHeapCreateOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevmemCtxInt2,
											psDevmemIntHeapCreateIN->hDevmemCtx,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX);
					if(psDevmemIntHeapCreateOUT->eError != PVRSRV_OK)
					{
						goto DevmemIntHeapCreate_exit;
					}

					/* Look up the data from the resman address */
					psDevmemIntHeapCreateOUT->eError = ResManFindPrivateDataByPtr(hDevmemCtxInt2, (IMG_VOID **) &psDevmemCtxInt);

					if(psDevmemIntHeapCreateOUT->eError != PVRSRV_OK)
					{
						goto DevmemIntHeapCreate_exit;
					}
				}

	psDevmemIntHeapCreateOUT->eError =
		DevmemIntHeapCreate(
					psDevmemCtxInt,
					psDevmemIntHeapCreateIN->sHeapBaseAddr,
					psDevmemIntHeapCreateIN->uiHeapLength,
					psDevmemIntHeapCreateIN->ui32Log2DataPageSize,
					&psDevmemHeapPtrInt);
	/* Exit early if bridged call fails */
	if(psDevmemIntHeapCreateOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntHeapCreate_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hDevmemHeapPtrInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_DEVICEMEM2_HEAP,
												psDevmemHeapPtrInt,
												(RESMAN_FREE_FN)&DevmemIntHeapDestroy);
	if (hDevmemHeapPtrInt2 == IMG_NULL)
	{
		psDevmemIntHeapCreateOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto DevmemIntHeapCreate_exit;
	}
	psDevmemIntHeapCreateOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psDevmemIntHeapCreateOUT->hDevmemHeapPtr,
							(IMG_HANDLE) hDevmemHeapPtrInt2,
							PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psDevmemIntHeapCreateOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntHeapCreate_exit;
	}


DevmemIntHeapCreate_exit:
	if (psDevmemIntHeapCreateOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hDevmemHeapPtrInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hDevmemHeapPtrInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psDevmemHeapPtrInt)
		{
			DevmemIntHeapDestroy(psDevmemHeapPtrInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIntHeapDestroy(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DEVMEMINTHEAPDESTROY *psDevmemIntHeapDestroyIN,
					 PVRSRV_BRIDGE_OUT_DEVMEMINTHEAPDESTROY *psDevmemIntHeapDestroyOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevmemHeapInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MM_DEVMEMINTHEAPDESTROY);





				{
					/* Look up the address from the handle */
					psDevmemIntHeapDestroyOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevmemHeapInt2,
											psDevmemIntHeapDestroyIN->hDevmemHeap,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP);
					if(psDevmemIntHeapDestroyOUT->eError != PVRSRV_OK)
					{
						goto DevmemIntHeapDestroy_exit;
					}

				}

	psDevmemIntHeapDestroyOUT->eError = DevmemIntHeapDestroyResManProxy(hDevmemHeapInt2);
	/* Exit early if bridged call fails */
	if(psDevmemIntHeapDestroyOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntHeapDestroy_exit;
	}

	psDevmemIntHeapDestroyOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psDevmemIntHeapDestroyIN->hDevmemHeap,
					PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP);


DevmemIntHeapDestroy_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIntMapPMR(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DEVMEMINTMAPPMR *psDevmemIntMapPMRIN,
					 PVRSRV_BRIDGE_OUT_DEVMEMINTMAPPMR *psDevmemIntMapPMROUT,
					 CONNECTION_DATA *psConnection)
{
	DEVMEMINT_HEAP * psDevmemServerHeapInt = IMG_NULL;
	IMG_HANDLE hDevmemServerHeapInt2 = IMG_NULL;
	DEVMEMINT_RESERVATION * psReservationInt = IMG_NULL;
	IMG_HANDLE hReservationInt2 = IMG_NULL;
	PMR * psPMRInt = IMG_NULL;
	IMG_HANDLE hPMRInt2 = IMG_NULL;
	DEVMEMINT_MAPPING * psMappingInt = IMG_NULL;
	IMG_HANDLE hMappingInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MM_DEVMEMINTMAPPMR);





	PMRLock();
				{
					/* Look up the address from the handle */
					psDevmemIntMapPMROUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevmemServerHeapInt2,
											psDevmemIntMapPMRIN->hDevmemServerHeap,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP);
					if(psDevmemIntMapPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto DevmemIntMapPMR_exit;
					}

					/* Look up the data from the resman address */
					psDevmemIntMapPMROUT->eError = ResManFindPrivateDataByPtr(hDevmemServerHeapInt2, (IMG_VOID **) &psDevmemServerHeapInt);

					if(psDevmemIntMapPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto DevmemIntMapPMR_exit;
					}
				}

				{
					/* Look up the address from the handle */
					psDevmemIntMapPMROUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hReservationInt2,
											psDevmemIntMapPMRIN->hReservation,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION);
					if(psDevmemIntMapPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto DevmemIntMapPMR_exit;
					}

					/* Look up the data from the resman address */
					psDevmemIntMapPMROUT->eError = ResManFindPrivateDataByPtr(hReservationInt2, (IMG_VOID **) &psReservationInt);

					if(psDevmemIntMapPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto DevmemIntMapPMR_exit;
					}
				}

				{
					/* Look up the address from the handle */
					psDevmemIntMapPMROUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hPMRInt2,
											psDevmemIntMapPMRIN->hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psDevmemIntMapPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto DevmemIntMapPMR_exit;
					}

					/* Look up the data from the resman address */
					psDevmemIntMapPMROUT->eError = ResManFindPrivateDataByPtr(hPMRInt2, (IMG_VOID **) &psPMRInt);

					if(psDevmemIntMapPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto DevmemIntMapPMR_exit;
					}
				}

	psDevmemIntMapPMROUT->eError =
		DevmemIntMapPMR(
					psDevmemServerHeapInt,
					psReservationInt,
					psPMRInt,
					psDevmemIntMapPMRIN->uiMapFlags,
					&psMappingInt);
	/* Exit early if bridged call fails */
	if(psDevmemIntMapPMROUT->eError != PVRSRV_OK)
	{
		PMRUnlock();
		goto DevmemIntMapPMR_exit;
	}
	PMRUnlock();

	/* Create a resman item and overwrite the handle with it */
	hMappingInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_DEVICEMEM2_MAPPING,
												psMappingInt,
												(RESMAN_FREE_FN)&DevmemIntUnmapPMR);
	if (hMappingInt2 == IMG_NULL)
	{
		psDevmemIntMapPMROUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto DevmemIntMapPMR_exit;
	}
	psDevmemIntMapPMROUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psDevmemIntMapPMROUT->hMapping,
							(IMG_HANDLE) hMappingInt2,
							PVRSRV_HANDLE_TYPE_DEVMEMINT_MAPPING,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psDevmemIntMapPMROUT->eError != PVRSRV_OK)
	{
		goto DevmemIntMapPMR_exit;
	}


DevmemIntMapPMR_exit:
	if (psDevmemIntMapPMROUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hMappingInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hMappingInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psMappingInt)
		{
			DevmemIntUnmapPMR(psMappingInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIntUnmapPMR(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DEVMEMINTUNMAPPMR *psDevmemIntUnmapPMRIN,
					 PVRSRV_BRIDGE_OUT_DEVMEMINTUNMAPPMR *psDevmemIntUnmapPMROUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hMappingInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MM_DEVMEMINTUNMAPPMR);





	PMRLock();
				{
					/* Look up the address from the handle */
					psDevmemIntUnmapPMROUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hMappingInt2,
											psDevmemIntUnmapPMRIN->hMapping,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_MAPPING);
					if(psDevmemIntUnmapPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto DevmemIntUnmapPMR_exit;
					}

				}

	psDevmemIntUnmapPMROUT->eError = DevmemIntUnmapPMRResManProxy(hMappingInt2);
	/* Exit early if bridged call fails */
	if(psDevmemIntUnmapPMROUT->eError != PVRSRV_OK)
	{
		PMRUnlock();
		goto DevmemIntUnmapPMR_exit;
	}
	PMRUnlock();

	psDevmemIntUnmapPMROUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psDevmemIntUnmapPMRIN->hMapping,
					PVRSRV_HANDLE_TYPE_DEVMEMINT_MAPPING);


DevmemIntUnmapPMR_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIntReserveRange(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DEVMEMINTRESERVERANGE *psDevmemIntReserveRangeIN,
					 PVRSRV_BRIDGE_OUT_DEVMEMINTRESERVERANGE *psDevmemIntReserveRangeOUT,
					 CONNECTION_DATA *psConnection)
{
	DEVMEMINT_HEAP * psDevmemServerHeapInt = IMG_NULL;
	IMG_HANDLE hDevmemServerHeapInt2 = IMG_NULL;
	DEVMEMINT_RESERVATION * psReservationInt = IMG_NULL;
	IMG_HANDLE hReservationInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MM_DEVMEMINTRESERVERANGE);





				{
					/* Look up the address from the handle */
					psDevmemIntReserveRangeOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevmemServerHeapInt2,
											psDevmemIntReserveRangeIN->hDevmemServerHeap,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP);
					if(psDevmemIntReserveRangeOUT->eError != PVRSRV_OK)
					{
						goto DevmemIntReserveRange_exit;
					}

					/* Look up the data from the resman address */
					psDevmemIntReserveRangeOUT->eError = ResManFindPrivateDataByPtr(hDevmemServerHeapInt2, (IMG_VOID **) &psDevmemServerHeapInt);

					if(psDevmemIntReserveRangeOUT->eError != PVRSRV_OK)
					{
						goto DevmemIntReserveRange_exit;
					}
				}

	psDevmemIntReserveRangeOUT->eError =
		DevmemIntReserveRange(
					psDevmemServerHeapInt,
					psDevmemIntReserveRangeIN->sAddress,
					psDevmemIntReserveRangeIN->uiLength,
					&psReservationInt);
	/* Exit early if bridged call fails */
	if(psDevmemIntReserveRangeOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntReserveRange_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hReservationInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_DEVICEMEM2_RESERVATION,
												psReservationInt,
												(RESMAN_FREE_FN)&DevmemIntUnreserveRange);
	if (hReservationInt2 == IMG_NULL)
	{
		psDevmemIntReserveRangeOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto DevmemIntReserveRange_exit;
	}
	psDevmemIntReserveRangeOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psDevmemIntReserveRangeOUT->hReservation,
							(IMG_HANDLE) hReservationInt2,
							PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psDevmemIntReserveRangeOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntReserveRange_exit;
	}


DevmemIntReserveRange_exit:
	if (psDevmemIntReserveRangeOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hReservationInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hReservationInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psReservationInt)
		{
			DevmemIntUnreserveRange(psReservationInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIntUnreserveRange(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DEVMEMINTUNRESERVERANGE *psDevmemIntUnreserveRangeIN,
					 PVRSRV_BRIDGE_OUT_DEVMEMINTUNRESERVERANGE *psDevmemIntUnreserveRangeOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hReservationInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MM_DEVMEMINTUNRESERVERANGE);





				{
					/* Look up the address from the handle */
					psDevmemIntUnreserveRangeOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hReservationInt2,
											psDevmemIntUnreserveRangeIN->hReservation,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION);
					if(psDevmemIntUnreserveRangeOUT->eError != PVRSRV_OK)
					{
						goto DevmemIntUnreserveRange_exit;
					}

				}

	psDevmemIntUnreserveRangeOUT->eError = DevmemIntUnreserveRangeResManProxy(hReservationInt2);
	/* Exit early if bridged call fails */
	if(psDevmemIntUnreserveRangeOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntUnreserveRange_exit;
	}

	psDevmemIntUnreserveRangeOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psDevmemIntUnreserveRangeIN->hReservation,
					PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION);


DevmemIntUnreserveRange_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgePhysmemNewRamBackedPMR(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_PHYSMEMNEWRAMBACKEDPMR *psPhysmemNewRamBackedPMRIN,
					 PVRSRV_BRIDGE_OUT_PHYSMEMNEWRAMBACKEDPMR *psPhysmemNewRamBackedPMROUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDeviceNodeInt = IMG_NULL;
	IMG_BOOL *bMappingTableInt = IMG_NULL;
	PMR * psPMRPtrInt = IMG_NULL;
	IMG_HANDLE hPMRPtrInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MM_PHYSMEMNEWRAMBACKEDPMR);




	if (psPhysmemNewRamBackedPMRIN->ui32NumVirtChunks != 0)
	{
		bMappingTableInt = OSAllocMem(psPhysmemNewRamBackedPMRIN->ui32NumVirtChunks * sizeof(IMG_BOOL));
		if (!bMappingTableInt)
		{
			psPhysmemNewRamBackedPMROUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto PhysmemNewRamBackedPMR_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psPhysmemNewRamBackedPMRIN->pbMappingTable, psPhysmemNewRamBackedPMRIN->ui32NumVirtChunks * sizeof(IMG_BOOL))
				|| (OSCopyFromUser(NULL, bMappingTableInt, psPhysmemNewRamBackedPMRIN->pbMappingTable,
				psPhysmemNewRamBackedPMRIN->ui32NumVirtChunks * sizeof(IMG_BOOL)) != PVRSRV_OK) )
			{
				psPhysmemNewRamBackedPMROUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto PhysmemNewRamBackedPMR_exit;
			}

	PMRLock();
				{
					/* Look up the address from the handle */
					psPhysmemNewRamBackedPMROUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDeviceNodeInt,
											psPhysmemNewRamBackedPMRIN->hDeviceNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psPhysmemNewRamBackedPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto PhysmemNewRamBackedPMR_exit;
					}

				}

	psPhysmemNewRamBackedPMROUT->eError =
		PhysmemNewRamBackedPMR(
					hDeviceNodeInt,
					psPhysmemNewRamBackedPMRIN->uiSize,
					psPhysmemNewRamBackedPMRIN->uiChunkSize,
					psPhysmemNewRamBackedPMRIN->ui32NumPhysChunks,
					psPhysmemNewRamBackedPMRIN->ui32NumVirtChunks,
					bMappingTableInt,
					psPhysmemNewRamBackedPMRIN->ui32Log2PageSize,
					psPhysmemNewRamBackedPMRIN->uiFlags,
					&psPMRPtrInt);
	/* Exit early if bridged call fails */
	if(psPhysmemNewRamBackedPMROUT->eError != PVRSRV_OK)
	{
		PMRUnlock();
		goto PhysmemNewRamBackedPMR_exit;
	}
	PMRUnlock();

	/* Create a resman item and overwrite the handle with it */
	hPMRPtrInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_PMR,
												psPMRPtrInt,
												(RESMAN_FREE_FN)&PMRUnrefPMR);
	if (hPMRPtrInt2 == IMG_NULL)
	{
		psPhysmemNewRamBackedPMROUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto PhysmemNewRamBackedPMR_exit;
	}
	psPhysmemNewRamBackedPMROUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psPhysmemNewRamBackedPMROUT->hPMRPtr,
							(IMG_HANDLE) hPMRPtrInt2,
							PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psPhysmemNewRamBackedPMROUT->eError != PVRSRV_OK)
	{
		goto PhysmemNewRamBackedPMR_exit;
	}


PhysmemNewRamBackedPMR_exit:
	if (psPhysmemNewRamBackedPMROUT->eError != PVRSRV_OK)
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

	if (bMappingTableInt)
		OSFreeMem(bMappingTableInt);

	return 0;
}

static IMG_INT
PVRSRVBridgePMRLocalImportPMR(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_PMRLOCALIMPORTPMR *psPMRLocalImportPMRIN,
					 PVRSRV_BRIDGE_OUT_PMRLOCALIMPORTPMR *psPMRLocalImportPMROUT,
					 CONNECTION_DATA *psConnection)
{
	PMR * psExtHandleInt = IMG_NULL;
	IMG_HANDLE hExtHandleInt2 = IMG_NULL;
	PMR * psPMRInt = IMG_NULL;
	IMG_HANDLE hPMRInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MM_PMRLOCALIMPORTPMR);





	PMRLock();
				{
					/* Look up the address from the handle */
					psPMRLocalImportPMROUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hExtHandleInt2,
											psPMRLocalImportPMRIN->hExtHandle,
											PVRSRV_HANDLE_TYPE_DEVMEM_MEM_IMPORT);
					if(psPMRLocalImportPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto PMRLocalImportPMR_exit;
					}

					/* Look up the data from the resman address */
					psPMRLocalImportPMROUT->eError = ResManFindPrivateDataByPtr(hExtHandleInt2, (IMG_VOID **) &psExtHandleInt);

					if(psPMRLocalImportPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto PMRLocalImportPMR_exit;
					}
				}

	psPMRLocalImportPMROUT->eError =
		PMRLocalImportPMR(
					psExtHandleInt,
					&psPMRInt,
					&psPMRLocalImportPMROUT->uiSize,
					&psPMRLocalImportPMROUT->sAlign);
	/* Exit early if bridged call fails */
	if(psPMRLocalImportPMROUT->eError != PVRSRV_OK)
	{
		PMRUnlock();
		goto PMRLocalImportPMR_exit;
	}
	PMRUnlock();

	/* Create a resman item and overwrite the handle with it */
	hPMRInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_PMR,
												psPMRInt,
												(RESMAN_FREE_FN)&PMRUnrefPMR);
	if (hPMRInt2 == IMG_NULL)
	{
		psPMRLocalImportPMROUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto PMRLocalImportPMR_exit;
	}
	psPMRLocalImportPMROUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psPMRLocalImportPMROUT->hPMR,
							(IMG_HANDLE) hPMRInt2,
							PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psPMRLocalImportPMROUT->eError != PVRSRV_OK)
	{
		goto PMRLocalImportPMR_exit;
	}


PMRLocalImportPMR_exit:
	if (psPMRLocalImportPMROUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hPMRInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hPMRInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psPMRInt)
		{
			PMRUnrefPMR(psPMRInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgePMRUnrefPMR(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_PMRUNREFPMR *psPMRUnrefPMRIN,
					 PVRSRV_BRIDGE_OUT_PMRUNREFPMR *psPMRUnrefPMROUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPMRInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MM_PMRUNREFPMR);




	PMRLock();
				{
					/* Look up the address from the handle */
					psPMRUnrefPMROUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hPMRInt2,
											psPMRUnrefPMRIN->hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psPMRUnrefPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto PMRUnrefPMR_exit;
					}

				}

	psPMRUnrefPMROUT->eError = PMRUnrefPMRResManProxy(hPMRInt2);
	/* Exit early if bridged call fails */
	if(psPMRUnrefPMROUT->eError != PVRSRV_OK)
	{
		PMRUnlock();
		goto PMRUnrefPMR_exit;
	}

	PMRUnlock();

	psPMRUnrefPMROUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psPMRUnrefPMRIN->hPMR,
					PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);


PMRUnrefPMR_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemSLCFlushInvalRequest(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DEVMEMSLCFLUSHINVALREQUEST *psDevmemSLCFlushInvalRequestIN,
					 PVRSRV_BRIDGE_OUT_DEVMEMSLCFLUSHINVALREQUEST *psDevmemSLCFlushInvalRequestOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDeviceNodeInt = IMG_NULL;
	PMR * psPmrInt = IMG_NULL;
	IMG_HANDLE hPmrInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MM_DEVMEMSLCFLUSHINVALREQUEST);





				{
					/* Look up the address from the handle */
					psDevmemSLCFlushInvalRequestOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDeviceNodeInt,
											psDevmemSLCFlushInvalRequestIN->hDeviceNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psDevmemSLCFlushInvalRequestOUT->eError != PVRSRV_OK)
					{
						goto DevmemSLCFlushInvalRequest_exit;
					}

				}

				{
					/* Look up the address from the handle */
					psDevmemSLCFlushInvalRequestOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hPmrInt2,
											psDevmemSLCFlushInvalRequestIN->hPmr,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psDevmemSLCFlushInvalRequestOUT->eError != PVRSRV_OK)
					{
						goto DevmemSLCFlushInvalRequest_exit;
					}

					/* Look up the data from the resman address */
					psDevmemSLCFlushInvalRequestOUT->eError = ResManFindPrivateDataByPtr(hPmrInt2, (IMG_VOID **) &psPmrInt);

					if(psDevmemSLCFlushInvalRequestOUT->eError != PVRSRV_OK)
					{
						goto DevmemSLCFlushInvalRequest_exit;
					}
				}

	psDevmemSLCFlushInvalRequestOUT->eError =
		DevmemSLCFlushInvalRequest(
					hDeviceNodeInt,
					psPmrInt);



DevmemSLCFlushInvalRequest_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeHeapCfgHeapConfigCount(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_HEAPCFGHEAPCONFIGCOUNT *psHeapCfgHeapConfigCountIN,
					 PVRSRV_BRIDGE_OUT_HEAPCFGHEAPCONFIGCOUNT *psHeapCfgHeapConfigCountOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDeviceNodeInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MM_HEAPCFGHEAPCONFIGCOUNT);





				{
					/* Look up the address from the handle */
					psHeapCfgHeapConfigCountOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDeviceNodeInt,
											psHeapCfgHeapConfigCountIN->hDeviceNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psHeapCfgHeapConfigCountOUT->eError != PVRSRV_OK)
					{
						goto HeapCfgHeapConfigCount_exit;
					}

				}

	psHeapCfgHeapConfigCountOUT->eError =
		HeapCfgHeapConfigCount(
					hDeviceNodeInt,
					&psHeapCfgHeapConfigCountOUT->ui32NumHeapConfigs);



HeapCfgHeapConfigCount_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeHeapCfgHeapCount(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_HEAPCFGHEAPCOUNT *psHeapCfgHeapCountIN,
					 PVRSRV_BRIDGE_OUT_HEAPCFGHEAPCOUNT *psHeapCfgHeapCountOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDeviceNodeInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MM_HEAPCFGHEAPCOUNT);





				{
					/* Look up the address from the handle */
					psHeapCfgHeapCountOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDeviceNodeInt,
											psHeapCfgHeapCountIN->hDeviceNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psHeapCfgHeapCountOUT->eError != PVRSRV_OK)
					{
						goto HeapCfgHeapCount_exit;
					}

				}

	psHeapCfgHeapCountOUT->eError =
		HeapCfgHeapCount(
					hDeviceNodeInt,
					psHeapCfgHeapCountIN->ui32HeapConfigIndex,
					&psHeapCfgHeapCountOUT->ui32NumHeaps);



HeapCfgHeapCount_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeHeapCfgHeapConfigName(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_HEAPCFGHEAPCONFIGNAME *psHeapCfgHeapConfigNameIN,
					 PVRSRV_BRIDGE_OUT_HEAPCFGHEAPCONFIGNAME *psHeapCfgHeapConfigNameOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDeviceNodeInt = IMG_NULL;
	IMG_CHAR *puiHeapConfigNameInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MM_HEAPCFGHEAPCONFIGNAME);


	psHeapCfgHeapConfigNameOUT->puiHeapConfigName = psHeapCfgHeapConfigNameIN->puiHeapConfigName;


	if (psHeapCfgHeapConfigNameIN->ui32HeapConfigNameBufSz != 0)
	{
		puiHeapConfigNameInt = OSAllocMem(psHeapCfgHeapConfigNameIN->ui32HeapConfigNameBufSz * sizeof(IMG_CHAR));
		if (!puiHeapConfigNameInt)
		{
			psHeapCfgHeapConfigNameOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto HeapCfgHeapConfigName_exit;
		}
	}


				{
					/* Look up the address from the handle */
					psHeapCfgHeapConfigNameOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDeviceNodeInt,
											psHeapCfgHeapConfigNameIN->hDeviceNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psHeapCfgHeapConfigNameOUT->eError != PVRSRV_OK)
					{
						goto HeapCfgHeapConfigName_exit;
					}

				}

	psHeapCfgHeapConfigNameOUT->eError =
		HeapCfgHeapConfigName(
					hDeviceNodeInt,
					psHeapCfgHeapConfigNameIN->ui32HeapConfigIndex,
					psHeapCfgHeapConfigNameIN->ui32HeapConfigNameBufSz,
					puiHeapConfigNameInt);


	if ( !OSAccessOK(PVR_VERIFY_WRITE, (IMG_VOID*) psHeapCfgHeapConfigNameOUT->puiHeapConfigName, (psHeapCfgHeapConfigNameIN->ui32HeapConfigNameBufSz * sizeof(IMG_CHAR))) 
		|| (OSCopyToUser(NULL, psHeapCfgHeapConfigNameOUT->puiHeapConfigName, puiHeapConfigNameInt,
		(psHeapCfgHeapConfigNameIN->ui32HeapConfigNameBufSz * sizeof(IMG_CHAR))) != PVRSRV_OK) )
	{
		psHeapCfgHeapConfigNameOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

		goto HeapCfgHeapConfigName_exit;
	}


HeapCfgHeapConfigName_exit:
	if (puiHeapConfigNameInt)
		OSFreeMem(puiHeapConfigNameInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeHeapCfgHeapDetails(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_HEAPCFGHEAPDETAILS *psHeapCfgHeapDetailsIN,
					 PVRSRV_BRIDGE_OUT_HEAPCFGHEAPDETAILS *psHeapCfgHeapDetailsOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDeviceNodeInt = IMG_NULL;
	IMG_CHAR *puiHeapNameOutInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MM_HEAPCFGHEAPDETAILS);


	psHeapCfgHeapDetailsOUT->puiHeapNameOut = psHeapCfgHeapDetailsIN->puiHeapNameOut;


	if (psHeapCfgHeapDetailsIN->ui32HeapNameBufSz != 0)
	{
		puiHeapNameOutInt = OSAllocMem(psHeapCfgHeapDetailsIN->ui32HeapNameBufSz * sizeof(IMG_CHAR));
		if (!puiHeapNameOutInt)
		{
			psHeapCfgHeapDetailsOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto HeapCfgHeapDetails_exit;
		}
	}


				{
					/* Look up the address from the handle */
					psHeapCfgHeapDetailsOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDeviceNodeInt,
											psHeapCfgHeapDetailsIN->hDeviceNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psHeapCfgHeapDetailsOUT->eError != PVRSRV_OK)
					{
						goto HeapCfgHeapDetails_exit;
					}

				}

	psHeapCfgHeapDetailsOUT->eError =
		HeapCfgHeapDetails(
					hDeviceNodeInt,
					psHeapCfgHeapDetailsIN->ui32HeapConfigIndex,
					psHeapCfgHeapDetailsIN->ui32HeapIndex,
					psHeapCfgHeapDetailsIN->ui32HeapNameBufSz,
					puiHeapNameOutInt,
					&psHeapCfgHeapDetailsOUT->sDevVAddrBase,
					&psHeapCfgHeapDetailsOUT->uiHeapLength,
					&psHeapCfgHeapDetailsOUT->ui32Log2DataPageSizeOut);


	if ( !OSAccessOK(PVR_VERIFY_WRITE, (IMG_VOID*) psHeapCfgHeapDetailsOUT->puiHeapNameOut, (psHeapCfgHeapDetailsIN->ui32HeapNameBufSz * sizeof(IMG_CHAR))) 
		|| (OSCopyToUser(NULL, psHeapCfgHeapDetailsOUT->puiHeapNameOut, puiHeapNameOutInt,
		(psHeapCfgHeapDetailsIN->ui32HeapNameBufSz * sizeof(IMG_CHAR))) != PVRSRV_OK) )
	{
		psHeapCfgHeapDetailsOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

		goto HeapCfgHeapDetails_exit;
	}


HeapCfgHeapDetails_exit:
	if (puiHeapNameOutInt)
		OSFreeMem(puiHeapNameOutInt);

	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */
 
PVRSRV_ERROR RegisterMMFunctions(IMG_VOID);
IMG_VOID UnregisterMMFunctions(IMG_VOID);

/*
 * Register all MM functions with services
 */
PVRSRV_ERROR RegisterMMFunctions(IMG_VOID)
{
	SetDispatchTableEntry(PVRSRV_BRIDGE_MM_PMREXPORTPMR, PVRSRVBridgePMRExportPMR);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MM_PMRUNEXPORTPMR, PVRSRVBridgePMRUnexportPMR);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MM_PMRGETUID, PVRSRVBridgePMRGetUID);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MM_PMRMAKESERVEREXPORTCLIENTEXPORT, PVRSRVBridgePMRMakeServerExportClientExport);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MM_PMRUNMAKESERVEREXPORTCLIENTEXPORT, PVRSRVBridgePMRUnmakeServerExportClientExport);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MM_PMRIMPORTPMR, PVRSRVBridgePMRImportPMR);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MM_DEVMEMINTCTXCREATE, PVRSRVBridgeDevmemIntCtxCreate);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MM_DEVMEMINTCTXDESTROY, PVRSRVBridgeDevmemIntCtxDestroy);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MM_DEVMEMINTHEAPCREATE, PVRSRVBridgeDevmemIntHeapCreate);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MM_DEVMEMINTHEAPDESTROY, PVRSRVBridgeDevmemIntHeapDestroy);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MM_DEVMEMINTMAPPMR, PVRSRVBridgeDevmemIntMapPMR);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MM_DEVMEMINTUNMAPPMR, PVRSRVBridgeDevmemIntUnmapPMR);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MM_DEVMEMINTRESERVERANGE, PVRSRVBridgeDevmemIntReserveRange);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MM_DEVMEMINTUNRESERVERANGE, PVRSRVBridgeDevmemIntUnreserveRange);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MM_PHYSMEMNEWRAMBACKEDPMR, PVRSRVBridgePhysmemNewRamBackedPMR);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MM_PMRLOCALIMPORTPMR, PVRSRVBridgePMRLocalImportPMR);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MM_PMRUNREFPMR, PVRSRVBridgePMRUnrefPMR);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MM_DEVMEMSLCFLUSHINVALREQUEST, PVRSRVBridgeDevmemSLCFlushInvalRequest);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MM_HEAPCFGHEAPCONFIGCOUNT, PVRSRVBridgeHeapCfgHeapConfigCount);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MM_HEAPCFGHEAPCOUNT, PVRSRVBridgeHeapCfgHeapCount);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MM_HEAPCFGHEAPCONFIGNAME, PVRSRVBridgeHeapCfgHeapConfigName);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MM_HEAPCFGHEAPDETAILS, PVRSRVBridgeHeapCfgHeapDetails);

	return PVRSRV_OK;
}

/*
 * Unregister all mm functions with services
 */
IMG_VOID UnregisterMMFunctions(IMG_VOID)
{
}
