/*************************************************************************/ /*!
@File
@Title          Server bridge for dc
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for dc
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

#include "dc_server.h"


#include "common_dc_bridge.h"

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
DCDeviceReleaseResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}

static PVRSRV_ERROR
DCSystemBufferReleaseResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}

static PVRSRV_ERROR
DCDisplayContextDestroyResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}

static PVRSRV_ERROR
DCBufferFreeResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}

static PVRSRV_ERROR
DCBufferUnimportResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}

static PVRSRV_ERROR
DCBufferUnpinResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}

static PVRSRV_ERROR
DCBufferReleaseResManProxy(IMG_HANDLE hResmanItem)
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
PVRSRVBridgeDCDevicesQueryCount(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCDEVICESQUERYCOUNT *psDCDevicesQueryCountIN,
					 PVRSRV_BRIDGE_OUT_DCDEVICESQUERYCOUNT *psDCDevicesQueryCountOUT,
					 CONNECTION_DATA *psConnection)
{

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCDEVICESQUERYCOUNT);

	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psDCDevicesQueryCountIN);




	psDCDevicesQueryCountOUT->eError =
		DCDevicesQueryCount(
					&psDCDevicesQueryCountOUT->ui32DeviceCount);




	return 0;
}

static IMG_INT
PVRSRVBridgeDCDevicesEnumerate(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCDEVICESENUMERATE *psDCDevicesEnumerateIN,
					 PVRSRV_BRIDGE_OUT_DCDEVICESENUMERATE *psDCDevicesEnumerateOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_UINT32 *pui32DeviceIndexInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCDEVICESENUMERATE);

	PVR_UNREFERENCED_PARAMETER(psConnection);

	psDCDevicesEnumerateOUT->pui32DeviceIndex = psDCDevicesEnumerateIN->pui32DeviceIndex;


	if (psDCDevicesEnumerateIN->ui32DeviceArraySize != 0)
	{
		pui32DeviceIndexInt = OSAllocMem(psDCDevicesEnumerateIN->ui32DeviceArraySize * sizeof(IMG_UINT32));
		if (!pui32DeviceIndexInt)
		{
			psDCDevicesEnumerateOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto DCDevicesEnumerate_exit;
		}
	}


	psDCDevicesEnumerateOUT->eError =
		DCDevicesEnumerate(
					psDCDevicesEnumerateIN->ui32DeviceArraySize,
					&psDCDevicesEnumerateOUT->ui32DeviceCount,
					pui32DeviceIndexInt);


	if ( !OSAccessOK(PVR_VERIFY_WRITE, (IMG_VOID*) psDCDevicesEnumerateOUT->pui32DeviceIndex, (psDCDevicesEnumerateOUT->ui32DeviceCount * sizeof(IMG_UINT32))) 
		|| (OSCopyToUser(NULL, psDCDevicesEnumerateOUT->pui32DeviceIndex, pui32DeviceIndexInt,
		(psDCDevicesEnumerateOUT->ui32DeviceCount * sizeof(IMG_UINT32))) != PVRSRV_OK) )
	{
		psDCDevicesEnumerateOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

		goto DCDevicesEnumerate_exit;
	}


DCDevicesEnumerate_exit:
	if (pui32DeviceIndexInt)
		OSFreeMem(pui32DeviceIndexInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeDCDeviceAcquire(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCDEVICEACQUIRE *psDCDeviceAcquireIN,
					 PVRSRV_BRIDGE_OUT_DCDEVICEACQUIRE *psDCDeviceAcquireOUT,
					 CONNECTION_DATA *psConnection)
{
	DC_DEVICE * psDeviceInt = IMG_NULL;
	IMG_HANDLE hDeviceInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCDEVICEACQUIRE);





	psDCDeviceAcquireOUT->eError =
		DCDeviceAcquire(
					psDCDeviceAcquireIN->ui32DeviceIndex,
					&psDeviceInt);
	/* Exit early if bridged call fails */
	if(psDCDeviceAcquireOUT->eError != PVRSRV_OK)
	{
		goto DCDeviceAcquire_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hDeviceInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_DC_DEVICE,
												psDeviceInt,
												(RESMAN_FREE_FN)&DCDeviceRelease);
	if (hDeviceInt2 == IMG_NULL)
	{
		psDCDeviceAcquireOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto DCDeviceAcquire_exit;
	}
	psDCDeviceAcquireOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psDCDeviceAcquireOUT->hDevice,
							(IMG_HANDLE) hDeviceInt2,
							PVRSRV_HANDLE_TYPE_DC_DEVICE,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psDCDeviceAcquireOUT->eError != PVRSRV_OK)
	{
		goto DCDeviceAcquire_exit;
	}


DCDeviceAcquire_exit:
	if (psDCDeviceAcquireOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hDeviceInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hDeviceInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psDeviceInt)
		{
			DCDeviceRelease(psDeviceInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeDCDeviceRelease(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCDEVICERELEASE *psDCDeviceReleaseIN,
					 PVRSRV_BRIDGE_OUT_DCDEVICERELEASE *psDCDeviceReleaseOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDeviceInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCDEVICERELEASE);





				{
					/* Look up the address from the handle */
					psDCDeviceReleaseOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDeviceInt2,
											psDCDeviceReleaseIN->hDevice,
											PVRSRV_HANDLE_TYPE_DC_DEVICE);
					if(psDCDeviceReleaseOUT->eError != PVRSRV_OK)
					{
						goto DCDeviceRelease_exit;
					}

				}

	psDCDeviceReleaseOUT->eError = DCDeviceReleaseResManProxy(hDeviceInt2);
	/* Exit early if bridged call fails */
	if(psDCDeviceReleaseOUT->eError != PVRSRV_OK)
	{
		goto DCDeviceRelease_exit;
	}

	psDCDeviceReleaseOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psDCDeviceReleaseIN->hDevice,
					PVRSRV_HANDLE_TYPE_DC_DEVICE);


DCDeviceRelease_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDCGetInfo(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCGETINFO *psDCGetInfoIN,
					 PVRSRV_BRIDGE_OUT_DCGETINFO *psDCGetInfoOUT,
					 CONNECTION_DATA *psConnection)
{
	DC_DEVICE * psDeviceInt = IMG_NULL;
	IMG_HANDLE hDeviceInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCGETINFO);





				{
					/* Look up the address from the handle */
					psDCGetInfoOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDeviceInt2,
											psDCGetInfoIN->hDevice,
											PVRSRV_HANDLE_TYPE_DC_DEVICE);
					if(psDCGetInfoOUT->eError != PVRSRV_OK)
					{
						goto DCGetInfo_exit;
					}

					/* Look up the data from the resman address */
					psDCGetInfoOUT->eError = ResManFindPrivateDataByPtr(hDeviceInt2, (IMG_VOID **) &psDeviceInt);

					if(psDCGetInfoOUT->eError != PVRSRV_OK)
					{
						goto DCGetInfo_exit;
					}
				}

	psDCGetInfoOUT->eError =
		DCGetInfo(
					psDeviceInt,
					&psDCGetInfoOUT->sDisplayInfo);



DCGetInfo_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDCPanelQueryCount(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCPANELQUERYCOUNT *psDCPanelQueryCountIN,
					 PVRSRV_BRIDGE_OUT_DCPANELQUERYCOUNT *psDCPanelQueryCountOUT,
					 CONNECTION_DATA *psConnection)
{
	DC_DEVICE * psDeviceInt = IMG_NULL;
	IMG_HANDLE hDeviceInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCPANELQUERYCOUNT);





				{
					/* Look up the address from the handle */
					psDCPanelQueryCountOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDeviceInt2,
											psDCPanelQueryCountIN->hDevice,
											PVRSRV_HANDLE_TYPE_DC_DEVICE);
					if(psDCPanelQueryCountOUT->eError != PVRSRV_OK)
					{
						goto DCPanelQueryCount_exit;
					}

					/* Look up the data from the resman address */
					psDCPanelQueryCountOUT->eError = ResManFindPrivateDataByPtr(hDeviceInt2, (IMG_VOID **) &psDeviceInt);

					if(psDCPanelQueryCountOUT->eError != PVRSRV_OK)
					{
						goto DCPanelQueryCount_exit;
					}
				}

	psDCPanelQueryCountOUT->eError =
		DCPanelQueryCount(
					psDeviceInt,
					&psDCPanelQueryCountOUT->ui32NumPanels);



DCPanelQueryCount_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDCPanelQuery(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCPANELQUERY *psDCPanelQueryIN,
					 PVRSRV_BRIDGE_OUT_DCPANELQUERY *psDCPanelQueryOUT,
					 CONNECTION_DATA *psConnection)
{
	DC_DEVICE * psDeviceInt = IMG_NULL;
	IMG_HANDLE hDeviceInt2 = IMG_NULL;
	PVRSRV_PANEL_INFO *psPanelInfoInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCPANELQUERY);


	psDCPanelQueryOUT->psPanelInfo = psDCPanelQueryIN->psPanelInfo;


	if (psDCPanelQueryIN->ui32PanelsArraySize != 0)
	{
		psPanelInfoInt = OSAllocMem(psDCPanelQueryIN->ui32PanelsArraySize * sizeof(PVRSRV_PANEL_INFO));
		if (!psPanelInfoInt)
		{
			psDCPanelQueryOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto DCPanelQuery_exit;
		}
	}


				{
					/* Look up the address from the handle */
					psDCPanelQueryOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDeviceInt2,
											psDCPanelQueryIN->hDevice,
											PVRSRV_HANDLE_TYPE_DC_DEVICE);
					if(psDCPanelQueryOUT->eError != PVRSRV_OK)
					{
						goto DCPanelQuery_exit;
					}

					/* Look up the data from the resman address */
					psDCPanelQueryOUT->eError = ResManFindPrivateDataByPtr(hDeviceInt2, (IMG_VOID **) &psDeviceInt);

					if(psDCPanelQueryOUT->eError != PVRSRV_OK)
					{
						goto DCPanelQuery_exit;
					}
				}

	psDCPanelQueryOUT->eError =
		DCPanelQuery(
					psDeviceInt,
					psDCPanelQueryIN->ui32PanelsArraySize,
					&psDCPanelQueryOUT->ui32NumPanels,
					psPanelInfoInt);


	if ( !OSAccessOK(PVR_VERIFY_WRITE, (IMG_VOID*) psDCPanelQueryOUT->psPanelInfo, (psDCPanelQueryOUT->ui32NumPanels * sizeof(PVRSRV_PANEL_INFO))) 
		|| (OSCopyToUser(NULL, psDCPanelQueryOUT->psPanelInfo, psPanelInfoInt,
		(psDCPanelQueryOUT->ui32NumPanels * sizeof(PVRSRV_PANEL_INFO))) != PVRSRV_OK) )
	{
		psDCPanelQueryOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

		goto DCPanelQuery_exit;
	}


DCPanelQuery_exit:
	if (psPanelInfoInt)
		OSFreeMem(psPanelInfoInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeDCFormatQuery(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCFORMATQUERY *psDCFormatQueryIN,
					 PVRSRV_BRIDGE_OUT_DCFORMATQUERY *psDCFormatQueryOUT,
					 CONNECTION_DATA *psConnection)
{
	DC_DEVICE * psDeviceInt = IMG_NULL;
	IMG_HANDLE hDeviceInt2 = IMG_NULL;
	PVRSRV_SURFACE_FORMAT *psFormatInt = IMG_NULL;
	IMG_UINT32 *pui32SupportedInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCFORMATQUERY);


	psDCFormatQueryOUT->pui32Supported = psDCFormatQueryIN->pui32Supported;


	if (psDCFormatQueryIN->ui32NumFormats != 0)
	{
		psFormatInt = OSAllocMem(psDCFormatQueryIN->ui32NumFormats * sizeof(PVRSRV_SURFACE_FORMAT));
		if (!psFormatInt)
		{
			psDCFormatQueryOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto DCFormatQuery_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psDCFormatQueryIN->psFormat, psDCFormatQueryIN->ui32NumFormats * sizeof(PVRSRV_SURFACE_FORMAT))
				|| (OSCopyFromUser(NULL, psFormatInt, psDCFormatQueryIN->psFormat,
				psDCFormatQueryIN->ui32NumFormats * sizeof(PVRSRV_SURFACE_FORMAT)) != PVRSRV_OK) )
			{
				psDCFormatQueryOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto DCFormatQuery_exit;
			}
	if (psDCFormatQueryIN->ui32NumFormats != 0)
	{
		pui32SupportedInt = OSAllocMem(psDCFormatQueryIN->ui32NumFormats * sizeof(IMG_UINT32));
		if (!pui32SupportedInt)
		{
			psDCFormatQueryOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto DCFormatQuery_exit;
		}
	}


				{
					/* Look up the address from the handle */
					psDCFormatQueryOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDeviceInt2,
											psDCFormatQueryIN->hDevice,
											PVRSRV_HANDLE_TYPE_DC_DEVICE);
					if(psDCFormatQueryOUT->eError != PVRSRV_OK)
					{
						goto DCFormatQuery_exit;
					}

					/* Look up the data from the resman address */
					psDCFormatQueryOUT->eError = ResManFindPrivateDataByPtr(hDeviceInt2, (IMG_VOID **) &psDeviceInt);

					if(psDCFormatQueryOUT->eError != PVRSRV_OK)
					{
						goto DCFormatQuery_exit;
					}
				}

	psDCFormatQueryOUT->eError =
		DCFormatQuery(
					psDeviceInt,
					psDCFormatQueryIN->ui32NumFormats,
					psFormatInt,
					pui32SupportedInt);


	if ( !OSAccessOK(PVR_VERIFY_WRITE, (IMG_VOID*) psDCFormatQueryOUT->pui32Supported, (psDCFormatQueryIN->ui32NumFormats * sizeof(IMG_UINT32))) 
		|| (OSCopyToUser(NULL, psDCFormatQueryOUT->pui32Supported, pui32SupportedInt,
		(psDCFormatQueryIN->ui32NumFormats * sizeof(IMG_UINT32))) != PVRSRV_OK) )
	{
		psDCFormatQueryOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

		goto DCFormatQuery_exit;
	}


DCFormatQuery_exit:
	if (psFormatInt)
		OSFreeMem(psFormatInt);
	if (pui32SupportedInt)
		OSFreeMem(pui32SupportedInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeDCDimQuery(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCDIMQUERY *psDCDimQueryIN,
					 PVRSRV_BRIDGE_OUT_DCDIMQUERY *psDCDimQueryOUT,
					 CONNECTION_DATA *psConnection)
{
	DC_DEVICE * psDeviceInt = IMG_NULL;
	IMG_HANDLE hDeviceInt2 = IMG_NULL;
	PVRSRV_SURFACE_DIMS *psDimInt = IMG_NULL;
	IMG_UINT32 *pui32SupportedInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCDIMQUERY);


	psDCDimQueryOUT->pui32Supported = psDCDimQueryIN->pui32Supported;


	if (psDCDimQueryIN->ui32NumDims != 0)
	{
		psDimInt = OSAllocMem(psDCDimQueryIN->ui32NumDims * sizeof(PVRSRV_SURFACE_DIMS));
		if (!psDimInt)
		{
			psDCDimQueryOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto DCDimQuery_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psDCDimQueryIN->psDim, psDCDimQueryIN->ui32NumDims * sizeof(PVRSRV_SURFACE_DIMS))
				|| (OSCopyFromUser(NULL, psDimInt, psDCDimQueryIN->psDim,
				psDCDimQueryIN->ui32NumDims * sizeof(PVRSRV_SURFACE_DIMS)) != PVRSRV_OK) )
			{
				psDCDimQueryOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto DCDimQuery_exit;
			}
	if (psDCDimQueryIN->ui32NumDims != 0)
	{
		pui32SupportedInt = OSAllocMem(psDCDimQueryIN->ui32NumDims * sizeof(IMG_UINT32));
		if (!pui32SupportedInt)
		{
			psDCDimQueryOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto DCDimQuery_exit;
		}
	}


				{
					/* Look up the address from the handle */
					psDCDimQueryOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDeviceInt2,
											psDCDimQueryIN->hDevice,
											PVRSRV_HANDLE_TYPE_DC_DEVICE);
					if(psDCDimQueryOUT->eError != PVRSRV_OK)
					{
						goto DCDimQuery_exit;
					}

					/* Look up the data from the resman address */
					psDCDimQueryOUT->eError = ResManFindPrivateDataByPtr(hDeviceInt2, (IMG_VOID **) &psDeviceInt);

					if(psDCDimQueryOUT->eError != PVRSRV_OK)
					{
						goto DCDimQuery_exit;
					}
				}

	psDCDimQueryOUT->eError =
		DCDimQuery(
					psDeviceInt,
					psDCDimQueryIN->ui32NumDims,
					psDimInt,
					pui32SupportedInt);


	if ( !OSAccessOK(PVR_VERIFY_WRITE, (IMG_VOID*) psDCDimQueryOUT->pui32Supported, (psDCDimQueryIN->ui32NumDims * sizeof(IMG_UINT32))) 
		|| (OSCopyToUser(NULL, psDCDimQueryOUT->pui32Supported, pui32SupportedInt,
		(psDCDimQueryIN->ui32NumDims * sizeof(IMG_UINT32))) != PVRSRV_OK) )
	{
		psDCDimQueryOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

		goto DCDimQuery_exit;
	}


DCDimQuery_exit:
	if (psDimInt)
		OSFreeMem(psDimInt);
	if (pui32SupportedInt)
		OSFreeMem(pui32SupportedInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeDCSetBlank(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCSETBLANK *psDCSetBlankIN,
					 PVRSRV_BRIDGE_OUT_DCSETBLANK *psDCSetBlankOUT,
					 CONNECTION_DATA *psConnection)
{
	DC_DEVICE * psDeviceInt = IMG_NULL;
	IMG_HANDLE hDeviceInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCSETBLANK);





				{
					/* Look up the address from the handle */
					psDCSetBlankOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDeviceInt2,
											psDCSetBlankIN->hDevice,
											PVRSRV_HANDLE_TYPE_DC_DEVICE);
					if(psDCSetBlankOUT->eError != PVRSRV_OK)
					{
						goto DCSetBlank_exit;
					}

					/* Look up the data from the resman address */
					psDCSetBlankOUT->eError = ResManFindPrivateDataByPtr(hDeviceInt2, (IMG_VOID **) &psDeviceInt);

					if(psDCSetBlankOUT->eError != PVRSRV_OK)
					{
						goto DCSetBlank_exit;
					}
				}

	psDCSetBlankOUT->eError =
		DCSetBlank(
					psDeviceInt,
					psDCSetBlankIN->bEnabled);



DCSetBlank_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDCSetVSyncReporting(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCSETVSYNCREPORTING *psDCSetVSyncReportingIN,
					 PVRSRV_BRIDGE_OUT_DCSETVSYNCREPORTING *psDCSetVSyncReportingOUT,
					 CONNECTION_DATA *psConnection)
{
	DC_DEVICE * psDeviceInt = IMG_NULL;
	IMG_HANDLE hDeviceInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCSETVSYNCREPORTING);





				{
					/* Look up the address from the handle */
					psDCSetVSyncReportingOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDeviceInt2,
											psDCSetVSyncReportingIN->hDevice,
											PVRSRV_HANDLE_TYPE_DC_DEVICE);
					if(psDCSetVSyncReportingOUT->eError != PVRSRV_OK)
					{
						goto DCSetVSyncReporting_exit;
					}

					/* Look up the data from the resman address */
					psDCSetVSyncReportingOUT->eError = ResManFindPrivateDataByPtr(hDeviceInt2, (IMG_VOID **) &psDeviceInt);

					if(psDCSetVSyncReportingOUT->eError != PVRSRV_OK)
					{
						goto DCSetVSyncReporting_exit;
					}
				}

	psDCSetVSyncReportingOUT->eError =
		DCSetVSyncReporting(
					psDeviceInt,
					psDCSetVSyncReportingIN->bEnabled);



DCSetVSyncReporting_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDCLastVSyncQuery(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCLASTVSYNCQUERY *psDCLastVSyncQueryIN,
					 PVRSRV_BRIDGE_OUT_DCLASTVSYNCQUERY *psDCLastVSyncQueryOUT,
					 CONNECTION_DATA *psConnection)
{
	DC_DEVICE * psDeviceInt = IMG_NULL;
	IMG_HANDLE hDeviceInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCLASTVSYNCQUERY);





				{
					/* Look up the address from the handle */
					psDCLastVSyncQueryOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDeviceInt2,
											psDCLastVSyncQueryIN->hDevice,
											PVRSRV_HANDLE_TYPE_DC_DEVICE);
					if(psDCLastVSyncQueryOUT->eError != PVRSRV_OK)
					{
						goto DCLastVSyncQuery_exit;
					}

					/* Look up the data from the resman address */
					psDCLastVSyncQueryOUT->eError = ResManFindPrivateDataByPtr(hDeviceInt2, (IMG_VOID **) &psDeviceInt);

					if(psDCLastVSyncQueryOUT->eError != PVRSRV_OK)
					{
						goto DCLastVSyncQuery_exit;
					}
				}

	psDCLastVSyncQueryOUT->eError =
		DCLastVSyncQuery(
					psDeviceInt,
					&psDCLastVSyncQueryOUT->i64Timestamp);



DCLastVSyncQuery_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDCSystemBufferAcquire(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCSYSTEMBUFFERACQUIRE *psDCSystemBufferAcquireIN,
					 PVRSRV_BRIDGE_OUT_DCSYSTEMBUFFERACQUIRE *psDCSystemBufferAcquireOUT,
					 CONNECTION_DATA *psConnection)
{
	DC_DEVICE * psDeviceInt = IMG_NULL;
	IMG_HANDLE hDeviceInt2 = IMG_NULL;
	DC_BUFFER * psBufferInt = IMG_NULL;
	IMG_HANDLE hBufferInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCSYSTEMBUFFERACQUIRE);





				{
					/* Look up the address from the handle */
					psDCSystemBufferAcquireOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDeviceInt2,
											psDCSystemBufferAcquireIN->hDevice,
											PVRSRV_HANDLE_TYPE_DC_DEVICE);
					if(psDCSystemBufferAcquireOUT->eError != PVRSRV_OK)
					{
						goto DCSystemBufferAcquire_exit;
					}

					/* Look up the data from the resman address */
					psDCSystemBufferAcquireOUT->eError = ResManFindPrivateDataByPtr(hDeviceInt2, (IMG_VOID **) &psDeviceInt);

					if(psDCSystemBufferAcquireOUT->eError != PVRSRV_OK)
					{
						goto DCSystemBufferAcquire_exit;
					}
				}

	psDCSystemBufferAcquireOUT->eError =
		DCSystemBufferAcquire(
					psDeviceInt,
					&psDCSystemBufferAcquireOUT->ui32Stride,
					&psBufferInt);
	/* Exit early if bridged call fails */
	if(psDCSystemBufferAcquireOUT->eError != PVRSRV_OK)
	{
		goto DCSystemBufferAcquire_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hBufferInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_DC_BUFFER,
												psBufferInt,
												(RESMAN_FREE_FN)&DCSystemBufferRelease);
	if (hBufferInt2 == IMG_NULL)
	{
		psDCSystemBufferAcquireOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto DCSystemBufferAcquire_exit;
	}
	psDCSystemBufferAcquireOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psDCSystemBufferAcquireOUT->hBuffer,
							(IMG_HANDLE) hBufferInt2,
							PVRSRV_HANDLE_TYPE_DC_BUFFER,
							PVRSRV_HANDLE_ALLOC_FLAG_SHARED
							);
	if (psDCSystemBufferAcquireOUT->eError != PVRSRV_OK)
	{
		goto DCSystemBufferAcquire_exit;
	}


DCSystemBufferAcquire_exit:
	if (psDCSystemBufferAcquireOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hBufferInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hBufferInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psBufferInt)
		{
			DCSystemBufferRelease(psBufferInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeDCSystemBufferRelease(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCSYSTEMBUFFERRELEASE *psDCSystemBufferReleaseIN,
					 PVRSRV_BRIDGE_OUT_DCSYSTEMBUFFERRELEASE *psDCSystemBufferReleaseOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hBufferInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCSYSTEMBUFFERRELEASE);





				{
					/* Look up the address from the handle */
					psDCSystemBufferReleaseOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hBufferInt2,
											psDCSystemBufferReleaseIN->hBuffer,
											PVRSRV_HANDLE_TYPE_DC_BUFFER);
					if(psDCSystemBufferReleaseOUT->eError != PVRSRV_OK)
					{
						goto DCSystemBufferRelease_exit;
					}

				}

	psDCSystemBufferReleaseOUT->eError = DCSystemBufferReleaseResManProxy(hBufferInt2);
	/* Exit early if bridged call fails */
	if(psDCSystemBufferReleaseOUT->eError != PVRSRV_OK)
	{
		goto DCSystemBufferRelease_exit;
	}

	psDCSystemBufferReleaseOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psDCSystemBufferReleaseIN->hBuffer,
					PVRSRV_HANDLE_TYPE_DC_BUFFER);


DCSystemBufferRelease_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDCDisplayContextCreate(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCDISPLAYCONTEXTCREATE *psDCDisplayContextCreateIN,
					 PVRSRV_BRIDGE_OUT_DCDISPLAYCONTEXTCREATE *psDCDisplayContextCreateOUT,
					 CONNECTION_DATA *psConnection)
{
	DC_DEVICE * psDeviceInt = IMG_NULL;
	IMG_HANDLE hDeviceInt2 = IMG_NULL;
	DC_DISPLAY_CONTEXT * psDisplayContextInt = IMG_NULL;
	IMG_HANDLE hDisplayContextInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCDISPLAYCONTEXTCREATE);





				{
					/* Look up the address from the handle */
					psDCDisplayContextCreateOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDeviceInt2,
											psDCDisplayContextCreateIN->hDevice,
											PVRSRV_HANDLE_TYPE_DC_DEVICE);
					if(psDCDisplayContextCreateOUT->eError != PVRSRV_OK)
					{
						goto DCDisplayContextCreate_exit;
					}

					/* Look up the data from the resman address */
					psDCDisplayContextCreateOUT->eError = ResManFindPrivateDataByPtr(hDeviceInt2, (IMG_VOID **) &psDeviceInt);

					if(psDCDisplayContextCreateOUT->eError != PVRSRV_OK)
					{
						goto DCDisplayContextCreate_exit;
					}
				}

	psDCDisplayContextCreateOUT->eError =
		DCDisplayContextCreate(
					psDeviceInt,
					&psDisplayContextInt);
	/* Exit early if bridged call fails */
	if(psDCDisplayContextCreateOUT->eError != PVRSRV_OK)
	{
		goto DCDisplayContextCreate_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hDisplayContextInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_DC_DISPLAY_CONTEXT,
												psDisplayContextInt,
												(RESMAN_FREE_FN)&DCDisplayContextDestroy);
	if (hDisplayContextInt2 == IMG_NULL)
	{
		psDCDisplayContextCreateOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto DCDisplayContextCreate_exit;
	}
	psDCDisplayContextCreateOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psDCDisplayContextCreateOUT->hDisplayContext,
							(IMG_HANDLE) hDisplayContextInt2,
							PVRSRV_HANDLE_TYPE_DC_DISPLAY_CONTEXT,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psDCDisplayContextCreateOUT->eError != PVRSRV_OK)
	{
		goto DCDisplayContextCreate_exit;
	}


DCDisplayContextCreate_exit:
	if (psDCDisplayContextCreateOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hDisplayContextInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hDisplayContextInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psDisplayContextInt)
		{
			DCDisplayContextDestroy(psDisplayContextInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeDCDisplayContextConfigureCheck(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCDISPLAYCONTEXTCONFIGURECHECK *psDCDisplayContextConfigureCheckIN,
					 PVRSRV_BRIDGE_OUT_DCDISPLAYCONTEXTCONFIGURECHECK *psDCDisplayContextConfigureCheckOUT,
					 CONNECTION_DATA *psConnection)
{
	DC_DISPLAY_CONTEXT * psDisplayContextInt = IMG_NULL;
	IMG_HANDLE hDisplayContextInt2 = IMG_NULL;
	PVRSRV_SURFACE_CONFIG_INFO *psSurfInfoInt = IMG_NULL;
	DC_BUFFER * *psBuffersInt = IMG_NULL;
	IMG_HANDLE *hBuffersInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCDISPLAYCONTEXTCONFIGURECHECK);




	if (psDCDisplayContextConfigureCheckIN->ui32PipeCount != 0)
	{
		psSurfInfoInt = OSAllocMem(psDCDisplayContextConfigureCheckIN->ui32PipeCount * sizeof(PVRSRV_SURFACE_CONFIG_INFO));
		if (!psSurfInfoInt)
		{
			psDCDisplayContextConfigureCheckOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto DCDisplayContextConfigureCheck_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psDCDisplayContextConfigureCheckIN->psSurfInfo, psDCDisplayContextConfigureCheckIN->ui32PipeCount * sizeof(PVRSRV_SURFACE_CONFIG_INFO))
				|| (OSCopyFromUser(NULL, psSurfInfoInt, psDCDisplayContextConfigureCheckIN->psSurfInfo,
				psDCDisplayContextConfigureCheckIN->ui32PipeCount * sizeof(PVRSRV_SURFACE_CONFIG_INFO)) != PVRSRV_OK) )
			{
				psDCDisplayContextConfigureCheckOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto DCDisplayContextConfigureCheck_exit;
			}
	if (psDCDisplayContextConfigureCheckIN->ui32PipeCount != 0)
	{
		psBuffersInt = OSAllocMem(psDCDisplayContextConfigureCheckIN->ui32PipeCount * sizeof(DC_BUFFER *));
		if (!psBuffersInt)
		{
			psDCDisplayContextConfigureCheckOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto DCDisplayContextConfigureCheck_exit;
		}
		hBuffersInt2 = OSAllocMem(psDCDisplayContextConfigureCheckIN->ui32PipeCount * sizeof(IMG_HANDLE));
		if (!hBuffersInt2)
		{
			psDCDisplayContextConfigureCheckOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto DCDisplayContextConfigureCheck_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psDCDisplayContextConfigureCheckIN->phBuffers, psDCDisplayContextConfigureCheckIN->ui32PipeCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hBuffersInt2, psDCDisplayContextConfigureCheckIN->phBuffers,
				psDCDisplayContextConfigureCheckIN->ui32PipeCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psDCDisplayContextConfigureCheckOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto DCDisplayContextConfigureCheck_exit;
			}

				{
					/* Look up the address from the handle */
					psDCDisplayContextConfigureCheckOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDisplayContextInt2,
											psDCDisplayContextConfigureCheckIN->hDisplayContext,
											PVRSRV_HANDLE_TYPE_DC_DISPLAY_CONTEXT);
					if(psDCDisplayContextConfigureCheckOUT->eError != PVRSRV_OK)
					{
						goto DCDisplayContextConfigureCheck_exit;
					}

					/* Look up the data from the resman address */
					psDCDisplayContextConfigureCheckOUT->eError = ResManFindPrivateDataByPtr(hDisplayContextInt2, (IMG_VOID **) &psDisplayContextInt);

					if(psDCDisplayContextConfigureCheckOUT->eError != PVRSRV_OK)
					{
						goto DCDisplayContextConfigureCheck_exit;
					}
				}

	{
		IMG_UINT32 i;

		for (i=0;i<psDCDisplayContextConfigureCheckIN->ui32PipeCount;i++)
		{
				{
					/* Look up the address from the handle */
					psDCDisplayContextConfigureCheckOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hBuffersInt2[i],
											hBuffersInt2[i],
											PVRSRV_HANDLE_TYPE_DC_BUFFER);
					if(psDCDisplayContextConfigureCheckOUT->eError != PVRSRV_OK)
					{
						goto DCDisplayContextConfigureCheck_exit;
					}

					/* Look up the data from the resman address */
					psDCDisplayContextConfigureCheckOUT->eError = ResManFindPrivateDataByPtr(hBuffersInt2[i], (IMG_VOID **) &psBuffersInt[i]);

					if(psDCDisplayContextConfigureCheckOUT->eError != PVRSRV_OK)
					{
						goto DCDisplayContextConfigureCheck_exit;
					}
				}
		}
	}

	psDCDisplayContextConfigureCheckOUT->eError =
		DCDisplayContextConfigureCheck(
					psDisplayContextInt,
					psDCDisplayContextConfigureCheckIN->ui32PipeCount,
					psSurfInfoInt,
					psBuffersInt);



DCDisplayContextConfigureCheck_exit:
	if (psSurfInfoInt)
		OSFreeMem(psSurfInfoInt);
	if (psBuffersInt)
		OSFreeMem(psBuffersInt);
	if (hBuffersInt2)
		OSFreeMem(hBuffersInt2);

	return 0;
}

static IMG_INT
PVRSRVBridgeDCDisplayContextConfigure(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCDISPLAYCONTEXTCONFIGURE *psDCDisplayContextConfigureIN,
					 PVRSRV_BRIDGE_OUT_DCDISPLAYCONTEXTCONFIGURE *psDCDisplayContextConfigureOUT,
					 CONNECTION_DATA *psConnection)
{
	DC_DISPLAY_CONTEXT * psDisplayContextInt = IMG_NULL;
	IMG_HANDLE hDisplayContextInt2 = IMG_NULL;
	PVRSRV_SURFACE_CONFIG_INFO *psSurfInfoInt = IMG_NULL;
	DC_BUFFER * *psBuffersInt = IMG_NULL;
	IMG_HANDLE *hBuffersInt2 = IMG_NULL;
	SERVER_SYNC_PRIMITIVE * *psSyncInt = IMG_NULL;
	IMG_HANDLE *hSyncInt2 = IMG_NULL;
	IMG_BOOL *bUpdateInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCDISPLAYCONTEXTCONFIGURE);




	if (psDCDisplayContextConfigureIN->ui32PipeCount != 0)
	{
		psSurfInfoInt = OSAllocMem(psDCDisplayContextConfigureIN->ui32PipeCount * sizeof(PVRSRV_SURFACE_CONFIG_INFO));
		if (!psSurfInfoInt)
		{
			psDCDisplayContextConfigureOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto DCDisplayContextConfigure_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psDCDisplayContextConfigureIN->psSurfInfo, psDCDisplayContextConfigureIN->ui32PipeCount * sizeof(PVRSRV_SURFACE_CONFIG_INFO))
				|| (OSCopyFromUser(NULL, psSurfInfoInt, psDCDisplayContextConfigureIN->psSurfInfo,
				psDCDisplayContextConfigureIN->ui32PipeCount * sizeof(PVRSRV_SURFACE_CONFIG_INFO)) != PVRSRV_OK) )
			{
				psDCDisplayContextConfigureOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto DCDisplayContextConfigure_exit;
			}
	if (psDCDisplayContextConfigureIN->ui32PipeCount != 0)
	{
		psBuffersInt = OSAllocMem(psDCDisplayContextConfigureIN->ui32PipeCount * sizeof(DC_BUFFER *));
		if (!psBuffersInt)
		{
			psDCDisplayContextConfigureOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto DCDisplayContextConfigure_exit;
		}
		hBuffersInt2 = OSAllocMem(psDCDisplayContextConfigureIN->ui32PipeCount * sizeof(IMG_HANDLE));
		if (!hBuffersInt2)
		{
			psDCDisplayContextConfigureOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto DCDisplayContextConfigure_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psDCDisplayContextConfigureIN->phBuffers, psDCDisplayContextConfigureIN->ui32PipeCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hBuffersInt2, psDCDisplayContextConfigureIN->phBuffers,
				psDCDisplayContextConfigureIN->ui32PipeCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psDCDisplayContextConfigureOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto DCDisplayContextConfigure_exit;
			}
	if (psDCDisplayContextConfigureIN->ui32SyncCount != 0)
	{
		psSyncInt = OSAllocMem(psDCDisplayContextConfigureIN->ui32SyncCount * sizeof(SERVER_SYNC_PRIMITIVE *));
		if (!psSyncInt)
		{
			psDCDisplayContextConfigureOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto DCDisplayContextConfigure_exit;
		}
		hSyncInt2 = OSAllocMem(psDCDisplayContextConfigureIN->ui32SyncCount * sizeof(IMG_HANDLE));
		if (!hSyncInt2)
		{
			psDCDisplayContextConfigureOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto DCDisplayContextConfigure_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psDCDisplayContextConfigureIN->phSync, psDCDisplayContextConfigureIN->ui32SyncCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hSyncInt2, psDCDisplayContextConfigureIN->phSync,
				psDCDisplayContextConfigureIN->ui32SyncCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psDCDisplayContextConfigureOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto DCDisplayContextConfigure_exit;
			}
	if (psDCDisplayContextConfigureIN->ui32SyncCount != 0)
	{
		bUpdateInt = OSAllocMem(psDCDisplayContextConfigureIN->ui32SyncCount * sizeof(IMG_BOOL));
		if (!bUpdateInt)
		{
			psDCDisplayContextConfigureOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto DCDisplayContextConfigure_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psDCDisplayContextConfigureIN->pbUpdate, psDCDisplayContextConfigureIN->ui32SyncCount * sizeof(IMG_BOOL))
				|| (OSCopyFromUser(NULL, bUpdateInt, psDCDisplayContextConfigureIN->pbUpdate,
				psDCDisplayContextConfigureIN->ui32SyncCount * sizeof(IMG_BOOL)) != PVRSRV_OK) )
			{
				psDCDisplayContextConfigureOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto DCDisplayContextConfigure_exit;
			}

				{
					/* Look up the address from the handle */
					psDCDisplayContextConfigureOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDisplayContextInt2,
											psDCDisplayContextConfigureIN->hDisplayContext,
											PVRSRV_HANDLE_TYPE_DC_DISPLAY_CONTEXT);
					if(psDCDisplayContextConfigureOUT->eError != PVRSRV_OK)
					{
						goto DCDisplayContextConfigure_exit;
					}

					/* Look up the data from the resman address */
					psDCDisplayContextConfigureOUT->eError = ResManFindPrivateDataByPtr(hDisplayContextInt2, (IMG_VOID **) &psDisplayContextInt);

					if(psDCDisplayContextConfigureOUT->eError != PVRSRV_OK)
					{
						goto DCDisplayContextConfigure_exit;
					}
				}

	{
		IMG_UINT32 i;

		for (i=0;i<psDCDisplayContextConfigureIN->ui32PipeCount;i++)
		{
				{
					/* Look up the address from the handle */
					psDCDisplayContextConfigureOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hBuffersInt2[i],
											hBuffersInt2[i],
											PVRSRV_HANDLE_TYPE_DC_BUFFER);
					if(psDCDisplayContextConfigureOUT->eError != PVRSRV_OK)
					{
						goto DCDisplayContextConfigure_exit;
					}

					/* Look up the data from the resman address */
					psDCDisplayContextConfigureOUT->eError = ResManFindPrivateDataByPtr(hBuffersInt2[i], (IMG_VOID **) &psBuffersInt[i]);

					if(psDCDisplayContextConfigureOUT->eError != PVRSRV_OK)
					{
						goto DCDisplayContextConfigure_exit;
					}
				}
		}
	}

	{
		IMG_UINT32 i;

		for (i=0;i<psDCDisplayContextConfigureIN->ui32SyncCount;i++)
		{
				{
					/* Look up the address from the handle */
					psDCDisplayContextConfigureOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hSyncInt2[i],
											hSyncInt2[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
					if(psDCDisplayContextConfigureOUT->eError != PVRSRV_OK)
					{
						goto DCDisplayContextConfigure_exit;
					}

					/* Look up the data from the resman address */
					psDCDisplayContextConfigureOUT->eError = ResManFindPrivateDataByPtr(hSyncInt2[i], (IMG_VOID **) &psSyncInt[i]);

					if(psDCDisplayContextConfigureOUT->eError != PVRSRV_OK)
					{
						goto DCDisplayContextConfigure_exit;
					}
				}
		}
	}

	psDCDisplayContextConfigureOUT->eError =
		DCDisplayContextConfigure(
					psDisplayContextInt,
					psDCDisplayContextConfigureIN->ui32PipeCount,
					psSurfInfoInt,
					psBuffersInt,
					psDCDisplayContextConfigureIN->ui32SyncCount,
					psSyncInt,
					bUpdateInt,
					psDCDisplayContextConfigureIN->ui32DisplayPeriod,
					psDCDisplayContextConfigureIN->ui32MaxDepth,
					psDCDisplayContextConfigureIN->i32AcquireFd,
					&psDCDisplayContextConfigureOUT->i32ReleaseFd);



DCDisplayContextConfigure_exit:
	if (psSurfInfoInt)
		OSFreeMem(psSurfInfoInt);
	if (psBuffersInt)
		OSFreeMem(psBuffersInt);
	if (hBuffersInt2)
		OSFreeMem(hBuffersInt2);
	if (psSyncInt)
		OSFreeMem(psSyncInt);
	if (hSyncInt2)
		OSFreeMem(hSyncInt2);
	if (bUpdateInt)
		OSFreeMem(bUpdateInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeDCDisplayContextDestroy(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCDISPLAYCONTEXTDESTROY *psDCDisplayContextDestroyIN,
					 PVRSRV_BRIDGE_OUT_DCDISPLAYCONTEXTDESTROY *psDCDisplayContextDestroyOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDisplayContextInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCDISPLAYCONTEXTDESTROY);





				{
					/* Look up the address from the handle */
					psDCDisplayContextDestroyOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDisplayContextInt2,
											psDCDisplayContextDestroyIN->hDisplayContext,
											PVRSRV_HANDLE_TYPE_DC_DISPLAY_CONTEXT);
					if(psDCDisplayContextDestroyOUT->eError != PVRSRV_OK)
					{
						goto DCDisplayContextDestroy_exit;
					}

				}

	psDCDisplayContextDestroyOUT->eError = DCDisplayContextDestroyResManProxy(hDisplayContextInt2);
	/* Exit early if bridged call fails */
	if(psDCDisplayContextDestroyOUT->eError != PVRSRV_OK)
	{
		goto DCDisplayContextDestroy_exit;
	}

	psDCDisplayContextDestroyOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psDCDisplayContextDestroyIN->hDisplayContext,
					PVRSRV_HANDLE_TYPE_DC_DISPLAY_CONTEXT);


DCDisplayContextDestroy_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDCBufferAlloc(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCBUFFERALLOC *psDCBufferAllocIN,
					 PVRSRV_BRIDGE_OUT_DCBUFFERALLOC *psDCBufferAllocOUT,
					 CONNECTION_DATA *psConnection)
{
	DC_DISPLAY_CONTEXT * psDisplayContextInt = IMG_NULL;
	IMG_HANDLE hDisplayContextInt2 = IMG_NULL;
	DC_BUFFER * psBufferInt = IMG_NULL;
	IMG_HANDLE hBufferInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCBUFFERALLOC);





				{
					/* Look up the address from the handle */
					psDCBufferAllocOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDisplayContextInt2,
											psDCBufferAllocIN->hDisplayContext,
											PVRSRV_HANDLE_TYPE_DC_DISPLAY_CONTEXT);
					if(psDCBufferAllocOUT->eError != PVRSRV_OK)
					{
						goto DCBufferAlloc_exit;
					}

					/* Look up the data from the resman address */
					psDCBufferAllocOUT->eError = ResManFindPrivateDataByPtr(hDisplayContextInt2, (IMG_VOID **) &psDisplayContextInt);

					if(psDCBufferAllocOUT->eError != PVRSRV_OK)
					{
						goto DCBufferAlloc_exit;
					}
				}

	psDCBufferAllocOUT->eError =
		DCBufferAlloc(
					psDisplayContextInt,
					&psDCBufferAllocIN->sSurfInfo,
					&psDCBufferAllocOUT->ui32Stride,
					&psBufferInt);
	/* Exit early if bridged call fails */
	if(psDCBufferAllocOUT->eError != PVRSRV_OK)
	{
		goto DCBufferAlloc_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hBufferInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_DC_BUFFER,
												psBufferInt,
												(RESMAN_FREE_FN)&DCBufferFree);
	if (hBufferInt2 == IMG_NULL)
	{
		psDCBufferAllocOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto DCBufferAlloc_exit;
	}
	psDCBufferAllocOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psDCBufferAllocOUT->hBuffer,
							(IMG_HANDLE) hBufferInt2,
							PVRSRV_HANDLE_TYPE_DC_BUFFER,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psDCBufferAllocOUT->eError != PVRSRV_OK)
	{
		goto DCBufferAlloc_exit;
	}


DCBufferAlloc_exit:
	if (psDCBufferAllocOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hBufferInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hBufferInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psBufferInt)
		{
			DCBufferFree(psBufferInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeDCBufferImport(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCBUFFERIMPORT *psDCBufferImportIN,
					 PVRSRV_BRIDGE_OUT_DCBUFFERIMPORT *psDCBufferImportOUT,
					 CONNECTION_DATA *psConnection)
{
	DC_DISPLAY_CONTEXT * psDisplayContextInt = IMG_NULL;
	IMG_HANDLE hDisplayContextInt2 = IMG_NULL;
	PMR * *psImportInt = IMG_NULL;
	IMG_HANDLE *hImportInt2 = IMG_NULL;
	DC_BUFFER * psBufferInt = IMG_NULL;
	IMG_HANDLE hBufferInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCBUFFERIMPORT);




	if (psDCBufferImportIN->ui32NumPlanes != 0)
	{
		psImportInt = OSAllocMem(psDCBufferImportIN->ui32NumPlanes * sizeof(PMR *));
		if (!psImportInt)
		{
			psDCBufferImportOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto DCBufferImport_exit;
		}
		hImportInt2 = OSAllocMem(psDCBufferImportIN->ui32NumPlanes * sizeof(IMG_HANDLE));
		if (!hImportInt2)
		{
			psDCBufferImportOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto DCBufferImport_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psDCBufferImportIN->phImport, psDCBufferImportIN->ui32NumPlanes * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hImportInt2, psDCBufferImportIN->phImport,
				psDCBufferImportIN->ui32NumPlanes * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psDCBufferImportOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto DCBufferImport_exit;
			}

				{
					/* Look up the address from the handle */
					psDCBufferImportOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDisplayContextInt2,
											psDCBufferImportIN->hDisplayContext,
											PVRSRV_HANDLE_TYPE_DC_DISPLAY_CONTEXT);
					if(psDCBufferImportOUT->eError != PVRSRV_OK)
					{
						goto DCBufferImport_exit;
					}

					/* Look up the data from the resman address */
					psDCBufferImportOUT->eError = ResManFindPrivateDataByPtr(hDisplayContextInt2, (IMG_VOID **) &psDisplayContextInt);

					if(psDCBufferImportOUT->eError != PVRSRV_OK)
					{
						goto DCBufferImport_exit;
					}
				}

	{
		IMG_UINT32 i;

		for (i=0;i<psDCBufferImportIN->ui32NumPlanes;i++)
		{
				{
					/* Look up the address from the handle */
					psDCBufferImportOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hImportInt2[i],
											hImportInt2[i],
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psDCBufferImportOUT->eError != PVRSRV_OK)
					{
						goto DCBufferImport_exit;
					}

					/* Look up the data from the resman address */
					psDCBufferImportOUT->eError = ResManFindPrivateDataByPtr(hImportInt2[i], (IMG_VOID **) &psImportInt[i]);

					if(psDCBufferImportOUT->eError != PVRSRV_OK)
					{
						goto DCBufferImport_exit;
					}
				}
		}
	}

	psDCBufferImportOUT->eError =
		DCBufferImport(
					psDisplayContextInt,
					psDCBufferImportIN->ui32NumPlanes,
					psImportInt,
					&psDCBufferImportIN->sSurfAttrib,
					&psBufferInt);
	/* Exit early if bridged call fails */
	if(psDCBufferImportOUT->eError != PVRSRV_OK)
	{
		goto DCBufferImport_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hBufferInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_DC_BUFFER,
												psBufferInt,
												(RESMAN_FREE_FN)&DCBufferFree);
	if (hBufferInt2 == IMG_NULL)
	{
		psDCBufferImportOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto DCBufferImport_exit;
	}
	psDCBufferImportOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psDCBufferImportOUT->hBuffer,
							(IMG_HANDLE) hBufferInt2,
							PVRSRV_HANDLE_TYPE_DC_BUFFER,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psDCBufferImportOUT->eError != PVRSRV_OK)
	{
		goto DCBufferImport_exit;
	}


DCBufferImport_exit:
	if (psDCBufferImportOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hBufferInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hBufferInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psBufferInt)
		{
			DCBufferFree(psBufferInt);
		}
	}

	if (psImportInt)
		OSFreeMem(psImportInt);
	if (hImportInt2)
		OSFreeMem(hImportInt2);

	return 0;
}

static IMG_INT
PVRSRVBridgeDCBufferFree(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCBUFFERFREE *psDCBufferFreeIN,
					 PVRSRV_BRIDGE_OUT_DCBUFFERFREE *psDCBufferFreeOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hBufferInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCBUFFERFREE);





				{
					/* Look up the address from the handle */
					psDCBufferFreeOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hBufferInt2,
											psDCBufferFreeIN->hBuffer,
											PVRSRV_HANDLE_TYPE_DC_BUFFER);
					if(psDCBufferFreeOUT->eError != PVRSRV_OK)
					{
						goto DCBufferFree_exit;
					}

				}

	psDCBufferFreeOUT->eError = DCBufferFreeResManProxy(hBufferInt2);
	/* Exit early if bridged call fails */
	if(psDCBufferFreeOUT->eError != PVRSRV_OK)
	{
		goto DCBufferFree_exit;
	}

	psDCBufferFreeOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psDCBufferFreeIN->hBuffer,
					PVRSRV_HANDLE_TYPE_DC_BUFFER);


DCBufferFree_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDCBufferUnimport(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCBUFFERUNIMPORT *psDCBufferUnimportIN,
					 PVRSRV_BRIDGE_OUT_DCBUFFERUNIMPORT *psDCBufferUnimportOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hBufferInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCBUFFERUNIMPORT);





				{
					/* Look up the address from the handle */
					psDCBufferUnimportOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hBufferInt2,
											psDCBufferUnimportIN->hBuffer,
											PVRSRV_HANDLE_TYPE_DC_BUFFER);
					if(psDCBufferUnimportOUT->eError != PVRSRV_OK)
					{
						goto DCBufferUnimport_exit;
					}

				}

	psDCBufferUnimportOUT->eError = DCBufferUnimportResManProxy(hBufferInt2);
	/* Exit early if bridged call fails */
	if(psDCBufferUnimportOUT->eError != PVRSRV_OK)
	{
		goto DCBufferUnimport_exit;
	}

	psDCBufferUnimportOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psDCBufferUnimportIN->hBuffer,
					PVRSRV_HANDLE_TYPE_DC_BUFFER);


DCBufferUnimport_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDCBufferPin(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCBUFFERPIN *psDCBufferPinIN,
					 PVRSRV_BRIDGE_OUT_DCBUFFERPIN *psDCBufferPinOUT,
					 CONNECTION_DATA *psConnection)
{
	DC_BUFFER * psBufferInt = IMG_NULL;
	IMG_HANDLE hBufferInt2 = IMG_NULL;
	DC_PIN_HANDLE hPinHandleInt = IMG_NULL;
	IMG_HANDLE hPinHandleInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCBUFFERPIN);





				{
					/* Look up the address from the handle */
					psDCBufferPinOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hBufferInt2,
											psDCBufferPinIN->hBuffer,
											PVRSRV_HANDLE_TYPE_DC_BUFFER);
					if(psDCBufferPinOUT->eError != PVRSRV_OK)
					{
						goto DCBufferPin_exit;
					}

					/* Look up the data from the resman address */
					psDCBufferPinOUT->eError = ResManFindPrivateDataByPtr(hBufferInt2, (IMG_VOID **) &psBufferInt);

					if(psDCBufferPinOUT->eError != PVRSRV_OK)
					{
						goto DCBufferPin_exit;
					}
				}

	psDCBufferPinOUT->eError =
		DCBufferPin(
					psBufferInt,
					&hPinHandleInt);
	/* Exit early if bridged call fails */
	if(psDCBufferPinOUT->eError != PVRSRV_OK)
	{
		goto DCBufferPin_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hPinHandleInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_DC_PIN_HANDLE,
												hPinHandleInt,
												(RESMAN_FREE_FN)&DCBufferUnpin);
	if (hPinHandleInt2 == IMG_NULL)
	{
		psDCBufferPinOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto DCBufferPin_exit;
	}
	psDCBufferPinOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psDCBufferPinOUT->hPinHandle,
							(IMG_HANDLE) hPinHandleInt2,
							PVRSRV_HANDLE_TYPE_DC_PIN_HANDLE,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psDCBufferPinOUT->eError != PVRSRV_OK)
	{
		goto DCBufferPin_exit;
	}


DCBufferPin_exit:
	if (psDCBufferPinOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hPinHandleInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hPinHandleInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (hPinHandleInt)
		{
			DCBufferUnpin(hPinHandleInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeDCBufferUnpin(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCBUFFERUNPIN *psDCBufferUnpinIN,
					 PVRSRV_BRIDGE_OUT_DCBUFFERUNPIN *psDCBufferUnpinOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPinHandleInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCBUFFERUNPIN);





				{
					/* Look up the address from the handle */
					psDCBufferUnpinOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hPinHandleInt2,
											psDCBufferUnpinIN->hPinHandle,
											PVRSRV_HANDLE_TYPE_DC_PIN_HANDLE);
					if(psDCBufferUnpinOUT->eError != PVRSRV_OK)
					{
						goto DCBufferUnpin_exit;
					}

				}

	psDCBufferUnpinOUT->eError = DCBufferUnpinResManProxy(hPinHandleInt2);
	/* Exit early if bridged call fails */
	if(psDCBufferUnpinOUT->eError != PVRSRV_OK)
	{
		goto DCBufferUnpin_exit;
	}

	psDCBufferUnpinOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psDCBufferUnpinIN->hPinHandle,
					PVRSRV_HANDLE_TYPE_DC_PIN_HANDLE);


DCBufferUnpin_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDCBufferAcquire(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCBUFFERACQUIRE *psDCBufferAcquireIN,
					 PVRSRV_BRIDGE_OUT_DCBUFFERACQUIRE *psDCBufferAcquireOUT,
					 CONNECTION_DATA *psConnection)
{
	DC_BUFFER * psBufferInt = IMG_NULL;
	IMG_HANDLE hBufferInt2 = IMG_NULL;
	PMR * psExtMemInt = IMG_NULL;
	IMG_HANDLE hExtMemInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCBUFFERACQUIRE);





				{
					/* Look up the address from the handle */
					psDCBufferAcquireOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hBufferInt2,
											psDCBufferAcquireIN->hBuffer,
											PVRSRV_HANDLE_TYPE_DC_BUFFER);
					if(psDCBufferAcquireOUT->eError != PVRSRV_OK)
					{
						goto DCBufferAcquire_exit;
					}

					/* Look up the data from the resman address */
					psDCBufferAcquireOUT->eError = ResManFindPrivateDataByPtr(hBufferInt2, (IMG_VOID **) &psBufferInt);

					if(psDCBufferAcquireOUT->eError != PVRSRV_OK)
					{
						goto DCBufferAcquire_exit;
					}
				}

	psDCBufferAcquireOUT->eError =
		DCBufferAcquire(
					psBufferInt,
					&psExtMemInt);
	/* Exit early if bridged call fails */
	if(psDCBufferAcquireOUT->eError != PVRSRV_OK)
	{
		goto DCBufferAcquire_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hExtMemInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_PMR,
												psExtMemInt,
												(RESMAN_FREE_FN)&DCBufferRelease);
	if (hExtMemInt2 == IMG_NULL)
	{
		psDCBufferAcquireOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto DCBufferAcquire_exit;
	}
	psDCBufferAcquireOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psDCBufferAcquireOUT->hExtMem,
							(IMG_HANDLE) hExtMemInt2,
							PVRSRV_HANDLE_TYPE_DEVMEM_MEM_IMPORT,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psDCBufferAcquireOUT->eError != PVRSRV_OK)
	{
		goto DCBufferAcquire_exit;
	}


DCBufferAcquire_exit:
	if (psDCBufferAcquireOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hExtMemInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hExtMemInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psExtMemInt)
		{
			DCBufferRelease(psExtMemInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeDCBufferRelease(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DCBUFFERRELEASE *psDCBufferReleaseIN,
					 PVRSRV_BRIDGE_OUT_DCBUFFERRELEASE *psDCBufferReleaseOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hExtMemInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_DC_DCBUFFERRELEASE);





				{
					/* Look up the address from the handle */
					psDCBufferReleaseOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hExtMemInt2,
											psDCBufferReleaseIN->hExtMem,
											PVRSRV_HANDLE_TYPE_DEVMEM_MEM_IMPORT);
					if(psDCBufferReleaseOUT->eError != PVRSRV_OK)
					{
						goto DCBufferRelease_exit;
					}

				}

	psDCBufferReleaseOUT->eError = DCBufferReleaseResManProxy(hExtMemInt2);
	/* Exit early if bridged call fails */
	if(psDCBufferReleaseOUT->eError != PVRSRV_OK)
	{
		goto DCBufferRelease_exit;
	}

	psDCBufferReleaseOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psDCBufferReleaseIN->hExtMem,
					PVRSRV_HANDLE_TYPE_DEVMEM_MEM_IMPORT);


DCBufferRelease_exit:

	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */
 
PVRSRV_ERROR RegisterDCFunctions(IMG_VOID);
IMG_VOID UnregisterDCFunctions(IMG_VOID);

/*
 * Register all DC functions with services
 */
PVRSRV_ERROR RegisterDCFunctions(IMG_VOID)
{
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCDEVICESQUERYCOUNT, PVRSRVBridgeDCDevicesQueryCount);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCDEVICESENUMERATE, PVRSRVBridgeDCDevicesEnumerate);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCDEVICEACQUIRE, PVRSRVBridgeDCDeviceAcquire);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCDEVICERELEASE, PVRSRVBridgeDCDeviceRelease);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCGETINFO, PVRSRVBridgeDCGetInfo);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCPANELQUERYCOUNT, PVRSRVBridgeDCPanelQueryCount);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCPANELQUERY, PVRSRVBridgeDCPanelQuery);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCFORMATQUERY, PVRSRVBridgeDCFormatQuery);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCDIMQUERY, PVRSRVBridgeDCDimQuery);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCSETBLANK, PVRSRVBridgeDCSetBlank);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCSETVSYNCREPORTING, PVRSRVBridgeDCSetVSyncReporting);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCLASTVSYNCQUERY, PVRSRVBridgeDCLastVSyncQuery);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCSYSTEMBUFFERACQUIRE, PVRSRVBridgeDCSystemBufferAcquire);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCSYSTEMBUFFERRELEASE, PVRSRVBridgeDCSystemBufferRelease);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCDISPLAYCONTEXTCREATE, PVRSRVBridgeDCDisplayContextCreate);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCDISPLAYCONTEXTCONFIGURECHECK, PVRSRVBridgeDCDisplayContextConfigureCheck);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCDISPLAYCONTEXTCONFIGURE, PVRSRVBridgeDCDisplayContextConfigure);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCDISPLAYCONTEXTDESTROY, PVRSRVBridgeDCDisplayContextDestroy);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCBUFFERALLOC, PVRSRVBridgeDCBufferAlloc);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCBUFFERIMPORT, PVRSRVBridgeDCBufferImport);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCBUFFERFREE, PVRSRVBridgeDCBufferFree);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCBUFFERUNIMPORT, PVRSRVBridgeDCBufferUnimport);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCBUFFERPIN, PVRSRVBridgeDCBufferPin);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCBUFFERUNPIN, PVRSRVBridgeDCBufferUnpin);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCBUFFERACQUIRE, PVRSRVBridgeDCBufferAcquire);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DC_DCBUFFERRELEASE, PVRSRVBridgeDCBufferRelease);

	return PVRSRV_OK;
}

/*
 * Unregister all dc functions with services
 */
IMG_VOID UnregisterDCFunctions(IMG_VOID)
{
}
