/*************************************************************************/ /*!
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
*/ /**************************************************************************/

#include <stddef.h>
#include <asm/uaccess.h>

#include "img_defs.h"

#include "srvcore.h"
#include "pvrsrv.h"


#include "common_srvcore_bridge.h"

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
ReleaseGlobalEventObjectResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}

static PVRSRV_ERROR
EventObjectCloseResManProxy(IMG_HANDLE hResmanItem)
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
PVRSRVBridgeConnect(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_CONNECT *psConnectIN,
					 PVRSRV_BRIDGE_OUT_CONNECT *psConnectOUT,
					 CONNECTION_DATA *psConnection)
{

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SRVCORE_CONNECT);

	PVR_UNREFERENCED_PARAMETER(psConnection);




	psConnectOUT->eError =
		PVRSRVConnectKM(psConnection,
					psConnectIN->ui32Flags,
					psConnectIN->ui32ClientBuildOptions,
					psConnectIN->ui32ClientDDKVersion,
					psConnectIN->ui32ClientDDKBuild,
					&psConnectOUT->ui8KernelArch,
					&psConnectOUT->ui32Log2PageSize);




	return 0;
}

static IMG_INT
PVRSRVBridgeDisconnect(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DISCONNECT *psDisconnectIN,
					 PVRSRV_BRIDGE_OUT_DISCONNECT *psDisconnectOUT,
					 CONNECTION_DATA *psConnection)
{

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SRVCORE_DISCONNECT);

	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psDisconnectIN);




	psDisconnectOUT->eError =
		PVRSRVDisconnectKM(
					);




	return 0;
}

static IMG_INT
PVRSRVBridgeEnumerateDevices(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_ENUMERATEDEVICES *psEnumerateDevicesIN,
					 PVRSRV_BRIDGE_OUT_ENUMERATEDEVICES *psEnumerateDevicesOUT,
					 CONNECTION_DATA *psConnection)
{
	PVRSRV_DEVICE_TYPE *peDeviceTypeInt = IMG_NULL;
	PVRSRV_DEVICE_CLASS *peDeviceClassInt = IMG_NULL;
	IMG_UINT32 *pui32DeviceIndexInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SRVCORE_ENUMERATEDEVICES);

	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psEnumerateDevicesIN);

	psEnumerateDevicesOUT->peDeviceType = psEnumerateDevicesIN->peDeviceType;
	psEnumerateDevicesOUT->peDeviceClass = psEnumerateDevicesIN->peDeviceClass;
	psEnumerateDevicesOUT->pui32DeviceIndex = psEnumerateDevicesIN->pui32DeviceIndex;


	
	{
		peDeviceTypeInt = OSAllocMem(PVRSRV_MAX_DEVICES * sizeof(PVRSRV_DEVICE_TYPE));
		if (!peDeviceTypeInt)
		{
			psEnumerateDevicesOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto EnumerateDevices_exit;
		}
	}

	
	{
		peDeviceClassInt = OSAllocMem(PVRSRV_MAX_DEVICES * sizeof(PVRSRV_DEVICE_CLASS));
		if (!peDeviceClassInt)
		{
			psEnumerateDevicesOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto EnumerateDevices_exit;
		}
	}

	
	{
		pui32DeviceIndexInt = OSAllocMem(PVRSRV_MAX_DEVICES * sizeof(IMG_UINT32));
		if (!pui32DeviceIndexInt)
		{
			psEnumerateDevicesOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto EnumerateDevices_exit;
		}
	}


	psEnumerateDevicesOUT->eError =
		PVRSRVEnumerateDevicesKM(
					&psEnumerateDevicesOUT->ui32NumDevices,
					peDeviceTypeInt,
					peDeviceClassInt,
					pui32DeviceIndexInt);


	if ( !OSAccessOK(PVR_VERIFY_WRITE, (IMG_VOID*) psEnumerateDevicesOUT->peDeviceType, (PVRSRV_MAX_DEVICES * sizeof(PVRSRV_DEVICE_TYPE))) 
		|| (OSCopyToUser(NULL, psEnumerateDevicesOUT->peDeviceType, peDeviceTypeInt,
		(PVRSRV_MAX_DEVICES * sizeof(PVRSRV_DEVICE_TYPE))) != PVRSRV_OK) )
	{
		psEnumerateDevicesOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

		goto EnumerateDevices_exit;
	}

	if ( !OSAccessOK(PVR_VERIFY_WRITE, (IMG_VOID*) psEnumerateDevicesOUT->peDeviceClass, (PVRSRV_MAX_DEVICES * sizeof(PVRSRV_DEVICE_CLASS))) 
		|| (OSCopyToUser(NULL, psEnumerateDevicesOUT->peDeviceClass, peDeviceClassInt,
		(PVRSRV_MAX_DEVICES * sizeof(PVRSRV_DEVICE_CLASS))) != PVRSRV_OK) )
	{
		psEnumerateDevicesOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

		goto EnumerateDevices_exit;
	}

	if ( !OSAccessOK(PVR_VERIFY_WRITE, (IMG_VOID*) psEnumerateDevicesOUT->pui32DeviceIndex, (PVRSRV_MAX_DEVICES * sizeof(IMG_UINT32))) 
		|| (OSCopyToUser(NULL, psEnumerateDevicesOUT->pui32DeviceIndex, pui32DeviceIndexInt,
		(PVRSRV_MAX_DEVICES * sizeof(IMG_UINT32))) != PVRSRV_OK) )
	{
		psEnumerateDevicesOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

		goto EnumerateDevices_exit;
	}


EnumerateDevices_exit:
	if (peDeviceTypeInt)
		OSFreeMem(peDeviceTypeInt);
	if (peDeviceClassInt)
		OSFreeMem(peDeviceClassInt);
	if (pui32DeviceIndexInt)
		OSFreeMem(pui32DeviceIndexInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeAcquireDeviceData(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_ACQUIREDEVICEDATA *psAcquireDeviceDataIN,
					 PVRSRV_BRIDGE_OUT_ACQUIREDEVICEDATA *psAcquireDeviceDataOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevCookieInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SRVCORE_ACQUIREDEVICEDATA);





	psAcquireDeviceDataOUT->eError =
		PVRSRVAcquireDeviceDataKM(
					psAcquireDeviceDataIN->ui32DevIndex,
					psAcquireDeviceDataIN->eDeviceType,
					&hDevCookieInt);
	/* Exit early if bridged call fails */
	if(psAcquireDeviceDataOUT->eError != PVRSRV_OK)
	{
		goto AcquireDeviceData_exit;
	}

	psAcquireDeviceDataOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psAcquireDeviceDataOUT->hDevCookie,
							(IMG_HANDLE) hDevCookieInt,
							PVRSRV_HANDLE_TYPE_DEV_NODE,
							PVRSRV_HANDLE_ALLOC_FLAG_SHARED
							);
	if (psAcquireDeviceDataOUT->eError != PVRSRV_OK)
	{
		goto AcquireDeviceData_exit;
	}


AcquireDeviceData_exit:
	if (psAcquireDeviceDataOUT->eError != PVRSRV_OK)
	{
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeReleaseDeviceData(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RELEASEDEVICEDATA *psReleaseDeviceDataIN,
					 PVRSRV_BRIDGE_OUT_RELEASEDEVICEDATA *psReleaseDeviceDataOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevCookieInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SRVCORE_RELEASEDEVICEDATA);





				{
					/* Look up the address from the handle */
					psReleaseDeviceDataOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevCookieInt,
											psReleaseDeviceDataIN->hDevCookie,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psReleaseDeviceDataOUT->eError != PVRSRV_OK)
					{
						goto ReleaseDeviceData_exit;
					}

				}

	psReleaseDeviceDataOUT->eError =
		PVRSRVReleaseDeviceDataKM(
					hDevCookieInt);
	/* Exit early if bridged call fails */
	if(psReleaseDeviceDataOUT->eError != PVRSRV_OK)
	{
		goto ReleaseDeviceData_exit;
	}

	psReleaseDeviceDataOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psReleaseDeviceDataIN->hDevCookie,
					PVRSRV_HANDLE_TYPE_DEV_NODE);


ReleaseDeviceData_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeInitSrvDisconnect(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_INITSRVDISCONNECT *psInitSrvDisconnectIN,
					 PVRSRV_BRIDGE_OUT_INITSRVDISCONNECT *psInitSrvDisconnectOUT,
					 CONNECTION_DATA *psConnection)
{

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SRVCORE_INITSRVDISCONNECT);





	psInitSrvDisconnectOUT->eError =
		PVRSRVInitSrvDisconnectKM(psConnection,
					psInitSrvDisconnectIN->bInitSuccesful,
					psInitSrvDisconnectIN->ui32ClientBuildOptions);




	return 0;
}

static IMG_INT
PVRSRVBridgeAcquireGlobalEventObject(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_ACQUIREGLOBALEVENTOBJECT *psAcquireGlobalEventObjectIN,
					 PVRSRV_BRIDGE_OUT_ACQUIREGLOBALEVENTOBJECT *psAcquireGlobalEventObjectOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hGlobalEventObjectInt = IMG_NULL;
	IMG_HANDLE hGlobalEventObjectInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SRVCORE_ACQUIREGLOBALEVENTOBJECT);

	PVR_UNREFERENCED_PARAMETER(psAcquireGlobalEventObjectIN);




	psAcquireGlobalEventObjectOUT->eError =
		AcquireGlobalEventObjectServer(
					&hGlobalEventObjectInt);
	/* Exit early if bridged call fails */
	if(psAcquireGlobalEventObjectOUT->eError != PVRSRV_OK)
	{
		goto AcquireGlobalEventObject_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hGlobalEventObjectInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_SHARED_EVENT_OBJECT,
												hGlobalEventObjectInt,
												(RESMAN_FREE_FN)&ReleaseGlobalEventObjectServer);
	if (hGlobalEventObjectInt2 == IMG_NULL)
	{
		psAcquireGlobalEventObjectOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto AcquireGlobalEventObject_exit;
	}
	psAcquireGlobalEventObjectOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psAcquireGlobalEventObjectOUT->hGlobalEventObject,
							(IMG_HANDLE) hGlobalEventObjectInt2,
							PVRSRV_HANDLE_TYPE_SHARED_EVENT_OBJECT,
							PVRSRV_HANDLE_ALLOC_FLAG_SHARED
							);
	if (psAcquireGlobalEventObjectOUT->eError != PVRSRV_OK)
	{
		goto AcquireGlobalEventObject_exit;
	}


AcquireGlobalEventObject_exit:
	if (psAcquireGlobalEventObjectOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hGlobalEventObjectInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hGlobalEventObjectInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (hGlobalEventObjectInt)
		{
			ReleaseGlobalEventObjectServer(hGlobalEventObjectInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeReleaseGlobalEventObject(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RELEASEGLOBALEVENTOBJECT *psReleaseGlobalEventObjectIN,
					 PVRSRV_BRIDGE_OUT_RELEASEGLOBALEVENTOBJECT *psReleaseGlobalEventObjectOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hGlobalEventObjectInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SRVCORE_RELEASEGLOBALEVENTOBJECT);





				{
					/* Look up the address from the handle */
					psReleaseGlobalEventObjectOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hGlobalEventObjectInt2,
											psReleaseGlobalEventObjectIN->hGlobalEventObject,
											PVRSRV_HANDLE_TYPE_SHARED_EVENT_OBJECT);
					if(psReleaseGlobalEventObjectOUT->eError != PVRSRV_OK)
					{
						goto ReleaseGlobalEventObject_exit;
					}

				}

	psReleaseGlobalEventObjectOUT->eError = ReleaseGlobalEventObjectResManProxy(hGlobalEventObjectInt2);
	/* Exit early if bridged call fails */
	if(psReleaseGlobalEventObjectOUT->eError != PVRSRV_OK)
	{
		goto ReleaseGlobalEventObject_exit;
	}

	psReleaseGlobalEventObjectOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psReleaseGlobalEventObjectIN->hGlobalEventObject,
					PVRSRV_HANDLE_TYPE_SHARED_EVENT_OBJECT);


ReleaseGlobalEventObject_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeEventObjectOpen(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_EVENTOBJECTOPEN *psEventObjectOpenIN,
					 PVRSRV_BRIDGE_OUT_EVENTOBJECTOPEN *psEventObjectOpenOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hEventObjectInt = IMG_NULL;
	IMG_HANDLE hEventObjectInt2 = IMG_NULL;
	IMG_HANDLE hOSEventInt = IMG_NULL;
	IMG_HANDLE hOSEventInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SRVCORE_EVENTOBJECTOPEN);





				{
					/* Look up the address from the handle */
					psEventObjectOpenOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hEventObjectInt2,
											psEventObjectOpenIN->hEventObject,
											PVRSRV_HANDLE_TYPE_SHARED_EVENT_OBJECT);
					if(psEventObjectOpenOUT->eError != PVRSRV_OK)
					{
						goto EventObjectOpen_exit;
					}

					/* Look up the data from the resman address */
					psEventObjectOpenOUT->eError = ResManFindPrivateDataByPtr(hEventObjectInt2, (IMG_VOID **) &hEventObjectInt);

					if(psEventObjectOpenOUT->eError != PVRSRV_OK)
					{
						goto EventObjectOpen_exit;
					}
				}

	psEventObjectOpenOUT->eError =
		OSEventObjectOpen(
					hEventObjectInt,
					&hOSEventInt);
	/* Exit early if bridged call fails */
	if(psEventObjectOpenOUT->eError != PVRSRV_OK)
	{
		goto EventObjectOpen_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hOSEventInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_EVENT_OBJECT,
												hOSEventInt,
												(RESMAN_FREE_FN)&OSEventObjectClose);
	if (hOSEventInt2 == IMG_NULL)
	{
		psEventObjectOpenOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto EventObjectOpen_exit;
	}
	psEventObjectOpenOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psEventObjectOpenOUT->hOSEvent,
							(IMG_HANDLE) hOSEventInt2,
							PVRSRV_HANDLE_TYPE_EVENT_OBJECT_CONNECT,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							);
	if (psEventObjectOpenOUT->eError != PVRSRV_OK)
	{
		goto EventObjectOpen_exit;
	}


EventObjectOpen_exit:
	if (psEventObjectOpenOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hOSEventInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hOSEventInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (hOSEventInt)
		{
			OSEventObjectClose(hOSEventInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeEventObjectWait(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_EVENTOBJECTWAIT *psEventObjectWaitIN,
					 PVRSRV_BRIDGE_OUT_EVENTOBJECTWAIT *psEventObjectWaitOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hOSEventKMInt = IMG_NULL;
	IMG_HANDLE hOSEventKMInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SRVCORE_EVENTOBJECTWAIT);





				{
					/* Look up the address from the handle */
					psEventObjectWaitOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hOSEventKMInt2,
											psEventObjectWaitIN->hOSEventKM,
											PVRSRV_HANDLE_TYPE_EVENT_OBJECT_CONNECT);
					if(psEventObjectWaitOUT->eError != PVRSRV_OK)
					{
						goto EventObjectWait_exit;
					}

					/* Look up the data from the resman address */
					psEventObjectWaitOUT->eError = ResManFindPrivateDataByPtr(hOSEventKMInt2, (IMG_VOID **) &hOSEventKMInt);

					if(psEventObjectWaitOUT->eError != PVRSRV_OK)
					{
						goto EventObjectWait_exit;
					}
				}

	psEventObjectWaitOUT->eError =
		OSEventObjectWait(
					hOSEventKMInt);



EventObjectWait_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeEventObjectClose(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_EVENTOBJECTCLOSE *psEventObjectCloseIN,
					 PVRSRV_BRIDGE_OUT_EVENTOBJECTCLOSE *psEventObjectCloseOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hOSEventKMInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SRVCORE_EVENTOBJECTCLOSE);





				{
					/* Look up the address from the handle */
					psEventObjectCloseOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hOSEventKMInt2,
											psEventObjectCloseIN->hOSEventKM,
											PVRSRV_HANDLE_TYPE_EVENT_OBJECT_CONNECT);
					if(psEventObjectCloseOUT->eError != PVRSRV_OK)
					{
						goto EventObjectClose_exit;
					}

				}

	psEventObjectCloseOUT->eError = EventObjectCloseResManProxy(hOSEventKMInt2);
	/* Exit early if bridged call fails */
	if(psEventObjectCloseOUT->eError != PVRSRV_OK)
	{
		goto EventObjectClose_exit;
	}

	psEventObjectCloseOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psEventObjectCloseIN->hOSEventKM,
					PVRSRV_HANDLE_TYPE_EVENT_OBJECT_CONNECT);


EventObjectClose_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDumpDebugInfo(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DUMPDEBUGINFO *psDumpDebugInfoIN,
					 PVRSRV_BRIDGE_OUT_DUMPDEBUGINFO *psDumpDebugInfoOUT,
					 CONNECTION_DATA *psConnection)
{

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SRVCORE_DUMPDEBUGINFO);

	PVR_UNREFERENCED_PARAMETER(psConnection);




	psDumpDebugInfoOUT->eError =
		PVRSRVDumpDebugInfoKM(
					psDumpDebugInfoIN->ui32ui32VerbLevel);




	return 0;
}

static IMG_INT
PVRSRVBridgeGetDevClockSpeed(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_GETDEVCLOCKSPEED *psGetDevClockSpeedIN,
					 PVRSRV_BRIDGE_OUT_GETDEVCLOCKSPEED *psGetDevClockSpeedOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SRVCORE_GETDEVCLOCKSPEED);





				{
					/* Look up the address from the handle */
					psGetDevClockSpeedOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevNodeInt,
											psGetDevClockSpeedIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psGetDevClockSpeedOUT->eError != PVRSRV_OK)
					{
						goto GetDevClockSpeed_exit;
					}

				}

	psGetDevClockSpeedOUT->eError =
		PVRSRVGetDevClockSpeedKM(
					hDevNodeInt,
					&psGetDevClockSpeedOUT->ui32ui32RGXClockSpeed);



GetDevClockSpeed_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeHWOpTimeout(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_HWOPTIMEOUT *psHWOpTimeoutIN,
					 PVRSRV_BRIDGE_OUT_HWOPTIMEOUT *psHWOpTimeoutOUT,
					 CONNECTION_DATA *psConnection)
{

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SRVCORE_HWOPTIMEOUT);

	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psHWOpTimeoutIN);




	psHWOpTimeoutOUT->eError =
		PVRSRVHWOpTimeoutKM(
					);




	return 0;
}

static IMG_INT
PVRSRVBridgeKickDevices(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_KICKDEVICES *psKickDevicesIN,
					 PVRSRV_BRIDGE_OUT_KICKDEVICES *psKickDevicesOUT,
					 CONNECTION_DATA *psConnection)
{

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SRVCORE_KICKDEVICES);

	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psKickDevicesIN);




	psKickDevicesOUT->eError =
		PVRSRVKickDevicesKM(
					);




	return 0;
}

static IMG_INT
PVRSRVBridgeResetHWRLogs(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RESETHWRLOGS *psResetHWRLogsIN,
					 PVRSRV_BRIDGE_OUT_RESETHWRLOGS *psResetHWRLogsOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SRVCORE_RESETHWRLOGS);





				{
					/* Look up the address from the handle */
					psResetHWRLogsOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevNodeInt,
											psResetHWRLogsIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psResetHWRLogsOUT->eError != PVRSRV_OK)
					{
						goto ResetHWRLogs_exit;
					}

				}

	psResetHWRLogsOUT->eError =
		PVRSRVResetHWRLogsKM(
					hDevNodeInt);



ResetHWRLogs_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeSoftReset(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SOFTRESET *psSoftResetIN,
					 PVRSRV_BRIDGE_OUT_SOFTRESET *psSoftResetOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SRVCORE_SOFTRESET);





				{
					/* Look up the address from the handle */
					psSoftResetOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevNodeInt,
											psSoftResetIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psSoftResetOUT->eError != PVRSRV_OK)
					{
						goto SoftReset_exit;
					}

				}

	psSoftResetOUT->eError =
		PVRSRVSoftResetKM(
					hDevNodeInt,
					psSoftResetIN->ui64ResetValue);



SoftReset_exit:

	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */
 
PVRSRV_ERROR RegisterSRVCOREFunctions(IMG_VOID);
IMG_VOID UnregisterSRVCOREFunctions(IMG_VOID);

/*
 * Register all SRVCORE functions with services
 */
PVRSRV_ERROR RegisterSRVCOREFunctions(IMG_VOID)
{
	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE_CONNECT, PVRSRVBridgeConnect);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE_DISCONNECT, PVRSRVBridgeDisconnect);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE_ENUMERATEDEVICES, PVRSRVBridgeEnumerateDevices);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE_ACQUIREDEVICEDATA, PVRSRVBridgeAcquireDeviceData);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE_RELEASEDEVICEDATA, PVRSRVBridgeReleaseDeviceData);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE_INITSRVDISCONNECT, PVRSRVBridgeInitSrvDisconnect);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE_ACQUIREGLOBALEVENTOBJECT, PVRSRVBridgeAcquireGlobalEventObject);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE_RELEASEGLOBALEVENTOBJECT, PVRSRVBridgeReleaseGlobalEventObject);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE_EVENTOBJECTOPEN, PVRSRVBridgeEventObjectOpen);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE_EVENTOBJECTWAIT, PVRSRVBridgeEventObjectWait);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE_EVENTOBJECTCLOSE, PVRSRVBridgeEventObjectClose);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE_DUMPDEBUGINFO, PVRSRVBridgeDumpDebugInfo);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE_GETDEVCLOCKSPEED, PVRSRVBridgeGetDevClockSpeed);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE_HWOPTIMEOUT, PVRSRVBridgeHWOpTimeout);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE_KICKDEVICES, PVRSRVBridgeKickDevices);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE_RESETHWRLOGS, PVRSRVBridgeResetHWRLogs);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SRVCORE_SOFTRESET, PVRSRVBridgeSoftReset);

	return PVRSRV_OK;
}

/*
 * Unregister all srvcore functions with services
 */
IMG_VOID UnregisterSRVCOREFunctions(IMG_VOID)
{
}
