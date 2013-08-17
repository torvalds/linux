/*************************************************************************/ /*!
@Title          Device class services functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Kernel services functions for device class devices
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

#include "services_headers.h"
#include "buffer_manager.h"
#include "kernelbuffer.h"
#include "kerneldisplay.h"
#include "pvr_bridge_km.h"
#include "pdump_km.h"
#include "deviceid.h"

#include "lists.h"

PVRSRV_ERROR AllocateDeviceID(SYS_DATA *psSysData, IMG_UINT32 *pui32DevID);
PVRSRV_ERROR FreeDeviceID(SYS_DATA *psSysData, IMG_UINT32 ui32DevID);

#if defined(SUPPORT_MISR_IN_THREAD)
void OSVSyncMISR(IMG_HANDLE, IMG_BOOL);
#endif

#if defined(SUPPORT_CUSTOM_SWAP_OPERATIONS)
IMG_VOID PVRSRVFreeCommandCompletePacketKM(IMG_HANDLE	hCmdCookie,
										   IMG_BOOL		bScheduleMISR);
#endif
/***********************************************************************
	Local Display Class Structures
************************************************************************/
typedef struct PVRSRV_DC_SRV2DISP_KMJTABLE_TAG *PPVRSRV_DC_SRV2DISP_KMJTABLE;

/*
	Display Class Buffer Info
*/
typedef struct PVRSRV_DC_BUFFER_TAG
{
	/* BC/DC common details - THIS MUST BE THE FIRST MEMBER */
	PVRSRV_DEVICECLASS_BUFFER sDeviceClassBuffer;

	struct PVRSRV_DISPLAYCLASS_INFO_TAG *psDCInfo;
	struct PVRSRV_DC_SWAPCHAIN_TAG *psSwapChain;
} PVRSRV_DC_BUFFER;

/*
	Display Device Class kernel swapchain information structure
*/
typedef struct PVRSRV_DC_SWAPCHAIN_TAG
{
	IMG_HANDLE							hExtSwapChain;
	IMG_UINT32							ui32SwapChainID;
	IMG_UINT32							ui32RefCount;
	IMG_UINT32							ui32Flags;
	PVRSRV_QUEUE_INFO					*psQueue;
	PVRSRV_DC_BUFFER					asBuffer[PVRSRV_MAX_DC_SWAPCHAIN_BUFFERS];
	IMG_UINT32							ui32BufferCount;
	PVRSRV_DC_BUFFER					*psLastFlipBuffer;
	IMG_UINT32							ui32MinSwapInterval;
	IMG_UINT32							ui32MaxSwapInterval;
#if !defined(SUPPORT_DC_CMDCOMPLETE_WHEN_NO_LONGER_DISPLAYED)
	PVRSRV_KERNEL_SYNC_INFO				**ppsLastSyncInfos;
	IMG_UINT32							ui32LastNumSyncInfos;
#endif /* !defined(SUPPORT_DC_CMDCOMPLETE_WHEN_NO_LONGER_DISPLAYED) */
	struct PVRSRV_DISPLAYCLASS_INFO_TAG *psDCInfo;
	struct PVRSRV_DC_SWAPCHAIN_TAG		*psNext;
} PVRSRV_DC_SWAPCHAIN;


/*
	Display Device Class kernel swapchain referecne structure
*/
typedef struct PVRSRV_DC_SWAPCHAIN_REF_TAG
{
	struct PVRSRV_DC_SWAPCHAIN_TAG		*psSwapChain;
	IMG_HANDLE							hResItem;	
} PVRSRV_DC_SWAPCHAIN_REF;


/*
	Display Device Class kernel services information structure
*/
typedef struct PVRSRV_DISPLAYCLASS_INFO_TAG
{
	IMG_UINT32 							ui32RefCount;
	IMG_UINT32							ui32DeviceID;
	IMG_HANDLE							hExtDevice;
	PPVRSRV_DC_SRV2DISP_KMJTABLE		psFuncTable;
	IMG_HANDLE							hDevMemContext;
	PVRSRV_DC_BUFFER 					sSystemBuffer;
	struct PVRSRV_DC_SWAPCHAIN_TAG		*psDCSwapChainShared;
} PVRSRV_DISPLAYCLASS_INFO;


/*
	Per-context Display Device Class kernel services information structure
*/
typedef struct PVRSRV_DISPLAYCLASS_PERCONTEXT_INFO_TAG
{
	PVRSRV_DISPLAYCLASS_INFO			*psDCInfo;
	PRESMAN_ITEM						hResItem;
} PVRSRV_DISPLAYCLASS_PERCONTEXT_INFO;


/***********************************************************************
	Local Buffer Class Structures
************************************************************************/
typedef struct PVRSRV_BC_SRV2BUFFER_KMJTABLE_TAG *PPVRSRV_BC_SRV2BUFFER_KMJTABLE;

/*
	Buffer Class Buffer Info
*/
typedef struct PVRSRV_BC_BUFFER_TAG
{
	/* BC/DC common details - THIS MUST BE THE FIRST MEMBER */
	PVRSRV_DEVICECLASS_BUFFER sDeviceClassBuffer;

	struct PVRSRV_BUFFERCLASS_INFO_TAG *psBCInfo;
} PVRSRV_BC_BUFFER;


/*
	Buffer Device Class kernel services information structure
*/
typedef struct PVRSRV_BUFFERCLASS_INFO_TAG
{
	IMG_UINT32 							ui32RefCount;
	IMG_UINT32							ui32DeviceID;
	IMG_HANDLE							hExtDevice;
	PPVRSRV_BC_SRV2BUFFER_KMJTABLE		psFuncTable;
	IMG_HANDLE							hDevMemContext;
	/* buffer info returned from 3rd party driver */
	IMG_UINT32							ui32BufferCount;
	PVRSRV_BC_BUFFER 					*psBuffer;

} PVRSRV_BUFFERCLASS_INFO;


/*
	Per-context Buffer Device Class kernel services information structure
*/
typedef struct PVRSRV_BUFFERCLASS_PERCONTEXT_INFO_TAG
{
	PVRSRV_BUFFERCLASS_INFO				*psBCInfo;
	IMG_HANDLE							hResItem;
} PVRSRV_BUFFERCLASS_PERCONTEXT_INFO;


/*!
******************************************************************************
 @Function	DCDeviceHandleToDCInfo

 @Description

 Convert a client-visible 3rd party device class handle to an internal
 PVRSRV_DISPLAYCLASS_INFO pointer.

 @Input hDeviceKM	- handle to display class device, returned from OpenDCDevice

 @Return
 	success: pointer to PVRSRV_DISPLAYCLASS_INFO
 	failure: IMG_NULL
******************************************************************************/
static PVRSRV_DISPLAYCLASS_INFO* DCDeviceHandleToDCInfo (IMG_HANDLE hDeviceKM)
{
	PVRSRV_DISPLAYCLASS_PERCONTEXT_INFO *psDCPerContextInfo;

	psDCPerContextInfo = (PVRSRV_DISPLAYCLASS_PERCONTEXT_INFO *)hDeviceKM;

	return psDCPerContextInfo->psDCInfo;
}


/*!
******************************************************************************
 @Function	BCDeviceHandleToBCInfo

 @Description

 Convert a client-visible 3rd party buffer class handle to an internal
 PVRSRV_BUFFERCLASS_INFO pointer.

 @Input hDeviceKM	- handle to buffer class device, returned from OpenBCDevice

 @Return
 	success: pointer to PVRSRV_BUFFERCLASS_INFO
 	failure: IMG_NULL
******************************************************************************/
static PVRSRV_BUFFERCLASS_INFO* BCDeviceHandleToBCInfo (IMG_HANDLE hDeviceKM)
{
	PVRSRV_BUFFERCLASS_PERCONTEXT_INFO *psBCPerContextInfo;

	psBCPerContextInfo = (PVRSRV_BUFFERCLASS_PERCONTEXT_INFO *)hDeviceKM;

	return psBCPerContextInfo->psBCInfo;
}

/*!
******************************************************************************
 @Function	PVRSRVEnumerateDCKM_ForEachVaCb

 @Description

 Enumerates the device node (if is of the same class as given).

 @Input psDeviceNode	- The device node to be enumerated
 		va				- variable arguments list, with:
							pui32DevCount	- The device count pointer (to be increased)
							ppui32DevID		- The pointer to the device IDs pointer (to be updated and increased)
							peDeviceClass	- The pointer to the device class of the psDeviceNode's to be enumerated.
******************************************************************************/
static IMG_VOID PVRSRVEnumerateDCKM_ForEachVaCb(PVRSRV_DEVICE_NODE *psDeviceNode, va_list va)
{
	IMG_UINT *pui32DevCount;
	IMG_UINT32 **ppui32DevID;
	PVRSRV_DEVICE_CLASS peDeviceClass;

	pui32DevCount = va_arg(va, IMG_UINT*);
	ppui32DevID = va_arg(va, IMG_UINT32**);
	peDeviceClass = va_arg(va, PVRSRV_DEVICE_CLASS);

	if	((psDeviceNode->sDevId.eDeviceClass == peDeviceClass)
	&&	(psDeviceNode->sDevId.eDeviceType == PVRSRV_DEVICE_TYPE_EXT))
	{
		(*pui32DevCount)++;
		if(*ppui32DevID)
		{
			*(*ppui32DevID)++ = psDeviceNode->sDevId.ui32DeviceIndex;
		}
	}
}


/*!
******************************************************************************

 @Function	PVRSRVEnumerateDCKM

 @Description

 Enumerates devices available in a given class.
 On first call, pass valid ptr for pui32DevCount and IMG_NULL for pui32DevID,
 On second call, pass same ptr for pui32DevCount and client allocated ptr
 for pui32DevID device id list

 @Input hServices 		- handle for services connection
 @Input ui32DevClass	- device class identifier
 @Output pui32DevCount	- number of devices available in class
 @Output pui32DevID		- list of device ids in the device class

 @Return
 	success: handle to matching display class device
 	failure: IMG_NULL

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVEnumerateDCKM (PVRSRV_DEVICE_CLASS DeviceClass,
								  IMG_UINT32 *pui32DevCount,
								  IMG_UINT32 *pui32DevID )
{
	/*PVRSRV_DEVICE_NODE	*psDeviceNode;*/
	IMG_UINT			ui32DevCount = 0;
	SYS_DATA 			*psSysData;

	SysAcquireData(&psSysData);

	/* search devonode list for devices in specified class and return the device ids */
	List_PVRSRV_DEVICE_NODE_ForEach_va(psSysData->psDeviceNodeList,
										&PVRSRVEnumerateDCKM_ForEachVaCb,
										&ui32DevCount,
										&pui32DevID,
										DeviceClass);

	if(pui32DevCount)
	{
		*pui32DevCount = ui32DevCount;
	}
	else if(pui32DevID == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVEnumerateDCKM: Invalid parameters"));
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	PVRSRVRegisterDCDeviceKM

 @Description

 registers an external device with the system

 @Input		psFuncTable		: device function table

 @Output	pui32DeviceID	: unique device key (for case of multiple identical devices)

 @Return	PVRSRV_ERROR 	:

******************************************************************************/
static
PVRSRV_ERROR PVRSRVRegisterDCDeviceKM (PVRSRV_DC_SRV2DISP_KMJTABLE *psFuncTable,
									   IMG_UINT32 *pui32DeviceID)
{
	PVRSRV_DISPLAYCLASS_INFO 	*psDCInfo = IMG_NULL;
	PVRSRV_DEVICE_NODE			*psDeviceNode;
	SYS_DATA					*psSysData;

	/*
		IN:
		 - name of client side ext. device driver library for subsequent loading
		 - predefined list of callbacks into kernel ext. device driver (based on class type)

		FUNCTION TASKS:
		 - allocate display device class info structure
		 - hang ext.device kernel callbacks on this structure (pfnKSwapToSystem)

		OUT:
		 - DEVICE_ID
		 - pass back devinfo? no

		Q&A:
		 - DEVICE_ID passed in or allocated - assume allocate
	*/

	SysAcquireData(&psSysData);

	/*
		If we got this far we're doing dynamic enumeration
		or first time static registration
	*/

	/* Allocate device control block */
	if(OSAllocMem( PVRSRV_OS_PAGEABLE_HEAP,
					 sizeof(*psDCInfo),
					 (IMG_VOID **)&psDCInfo, IMG_NULL,
					 "Display Class Info") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterDCDeviceKM: Failed psDCInfo alloc"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	OSMemSet (psDCInfo, 0, sizeof(*psDCInfo));

	/* setup the display device information structure */
	if(OSAllocMem( PVRSRV_OS_PAGEABLE_HEAP,
					 sizeof(PVRSRV_DC_SRV2DISP_KMJTABLE),
					 (IMG_VOID **)&psDCInfo->psFuncTable, IMG_NULL,
					 "Function table for SRVKM->DISPLAY") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterDCDeviceKM: Failed psFuncTable alloc"));
		goto ErrorExit;
	}
	OSMemSet (psDCInfo->psFuncTable, 0, sizeof(PVRSRV_DC_SRV2DISP_KMJTABLE));

	/* copy the jump table */
	*psDCInfo->psFuncTable = *psFuncTable;

	/* Allocate device node */
	if(OSAllocMem( PVRSRV_OS_NON_PAGEABLE_HEAP,
					 sizeof(PVRSRV_DEVICE_NODE),
					 (IMG_VOID **)&psDeviceNode, IMG_NULL,
					 "Device Node") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterDCDeviceKM: Failed psDeviceNode alloc"));
		goto ErrorExit;
	}
	OSMemSet (psDeviceNode, 0, sizeof(PVRSRV_DEVICE_NODE));

	psDeviceNode->pvDevice = (IMG_VOID*)psDCInfo;
	psDeviceNode->ui32pvDeviceSize = sizeof(*psDCInfo);
	psDeviceNode->ui32RefCount = 1;
	psDeviceNode->sDevId.eDeviceType = PVRSRV_DEVICE_TYPE_EXT;
	psDeviceNode->sDevId.eDeviceClass = PVRSRV_DEVICE_CLASS_DISPLAY;
	psDeviceNode->psSysData = psSysData;

	/* allocate a unique device id */
	if (AllocateDeviceID(psSysData, &psDeviceNode->sDevId.ui32DeviceIndex) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterBCDeviceKM: Failed to allocate Device ID"));
		goto ErrorExit;
	}
	psDCInfo->ui32DeviceID = psDeviceNode->sDevId.ui32DeviceIndex;
	if (pui32DeviceID)
	{
		*pui32DeviceID = psDeviceNode->sDevId.ui32DeviceIndex;
	}

	/* Register the device with the system */
	SysRegisterExternalDevice(psDeviceNode);

	/* and finally insert the device into the dev-list */
	List_PVRSRV_DEVICE_NODE_Insert(&psSysData->psDeviceNodeList, psDeviceNode);

	return PVRSRV_OK;

ErrorExit:

	if(psDCInfo->psFuncTable)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_DC_SRV2DISP_KMJTABLE), psDCInfo->psFuncTable, IMG_NULL);
		psDCInfo->psFuncTable = IMG_NULL;
	}

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_DISPLAYCLASS_INFO), psDCInfo, IMG_NULL);
	/*not nulling pointer, out of scope*/

	return PVRSRV_ERROR_OUT_OF_MEMORY;
}

/*!
******************************************************************************

 @Function	PVRSRVRemoveDCDeviceKM

 @Description

 Removes external device from services system record

 @Input	   ui32DeviceIndex		: unique device key (for case of multiple identical devices)

 @Return   PVRSRV_ERROR  :

******************************************************************************/
static PVRSRV_ERROR PVRSRVRemoveDCDeviceKM(IMG_UINT32 ui32DevIndex)
{
	SYS_DATA					*psSysData;
	PVRSRV_DEVICE_NODE			*psDeviceNode;
	PVRSRV_DISPLAYCLASS_INFO	*psDCInfo;

	SysAcquireData(&psSysData);

	/*search the node matching the devindex and display class*/
	psDeviceNode = (PVRSRV_DEVICE_NODE*)
		List_PVRSRV_DEVICE_NODE_Any_va(psSysData->psDeviceNodeList,
									   &MatchDeviceKM_AnyVaCb,
									   ui32DevIndex,
									   IMG_FALSE,
									   PVRSRV_DEVICE_CLASS_DISPLAY);
	if (!psDeviceNode)
	{
		/*device not found*/
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRemoveDCDeviceKM: requested device %d not present", ui32DevIndex));
		return PVRSRV_ERROR_NO_DEVICENODE_FOUND;
	}

	/* setup DCInfo ptr */
	psDCInfo = (PVRSRV_DISPLAYCLASS_INFO*)psDeviceNode->pvDevice;

	/*
		The device can only be removed if there are
		no open connections in the Services interface
	*/
	if(psDCInfo->ui32RefCount == 0)
	{
		/*
			Remove from the device list.
		*/
		List_PVRSRV_DEVICE_NODE_Remove(psDeviceNode);

		/* Unregister the device with the system */
		SysRemoveExternalDevice(psDeviceNode);

		/*
			OK found a device with a matching devindex
			remove registration information
		*/
		PVR_ASSERT(psDCInfo->ui32RefCount == 0);
		(IMG_VOID)FreeDeviceID(psSysData, ui32DevIndex);
		(IMG_VOID)OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_DC_SRV2DISP_KMJTABLE), psDCInfo->psFuncTable, IMG_NULL);
		psDCInfo->psFuncTable = IMG_NULL;
		(IMG_VOID)OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_DISPLAYCLASS_INFO), psDCInfo, IMG_NULL);
		/*not nulling original pointer, overwritten*/
		(IMG_VOID)OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_DEVICE_NODE), psDeviceNode, IMG_NULL);
		/*not nulling pointer, out of scope*/
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRemoveDCDeviceKM: failed as %d Services DC API connections are still open", psDCInfo->ui32RefCount));
		return PVRSRV_ERROR_UNABLE_TO_REMOVE_DEVICE;
	}

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	PVRSRVRegisterBCDeviceKM

 @Description

 registers an external device with the system

 @Input		psFuncTable		: device function table
 @Input		ui32DeviceIndex		: unique device key (for case of multiple identical devices)

 @Return   PVRSRV_ERROR  :

******************************************************************************/
static
PVRSRV_ERROR PVRSRVRegisterBCDeviceKM (PVRSRV_BC_SRV2BUFFER_KMJTABLE *psFuncTable,
									   IMG_UINT32	*pui32DeviceID)
{
	PVRSRV_BUFFERCLASS_INFO	*psBCInfo = IMG_NULL;
	PVRSRV_DEVICE_NODE		*psDeviceNode;
	SYS_DATA				*psSysData;
	/*
		IN:
		 - name of client side ext. device driver library for subsequent loading
		 - predefined list of callbacks into kernel ext. device driver (based on class type)

		FUNCTION TASKS:
		 - allocate buffer device class info structure

		OUT:
		 - DEVICE_ID
		 - pass back devinfo? no

		Q&A:
		 - DEVICE_ID passed in or allocated - assume allcoate
	*/

	SysAcquireData(&psSysData);

	/*
		If we got this far we're doing dynamic enumeration
		or first time static registration
	*/

	/* Allocate device control block */
	if(OSAllocMem( PVRSRV_OS_PAGEABLE_HEAP,
					 sizeof(*psBCInfo),
					 (IMG_VOID **)&psBCInfo, IMG_NULL,
					 "Buffer Class Info") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterBCDeviceKM: Failed psBCInfo alloc"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	OSMemSet (psBCInfo, 0, sizeof(*psBCInfo));

	/* setup the buffer device information structure */
	if(OSAllocMem( PVRSRV_OS_PAGEABLE_HEAP,
					 sizeof(PVRSRV_BC_SRV2BUFFER_KMJTABLE),
					 (IMG_VOID **)&psBCInfo->psFuncTable, IMG_NULL,
					 "Function table for SRVKM->BUFFER") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterBCDeviceKM: Failed psFuncTable alloc"));
		goto ErrorExit;
	}
	OSMemSet (psBCInfo->psFuncTable, 0, sizeof(PVRSRV_BC_SRV2BUFFER_KMJTABLE));

	/* copy the jump table */
	*psBCInfo->psFuncTable = *psFuncTable;

	/* Allocate device node */
	if(OSAllocMem( PVRSRV_OS_NON_PAGEABLE_HEAP,
					 sizeof(PVRSRV_DEVICE_NODE),
					 (IMG_VOID **)&psDeviceNode, IMG_NULL,
					 "Device Node") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterBCDeviceKM: Failed psDeviceNode alloc"));
		goto ErrorExit;
	}
	OSMemSet (psDeviceNode, 0, sizeof(PVRSRV_DEVICE_NODE));

	psDeviceNode->pvDevice = (IMG_VOID*)psBCInfo;
	psDeviceNode->ui32pvDeviceSize = sizeof(*psBCInfo);
	psDeviceNode->ui32RefCount = 1;
	psDeviceNode->sDevId.eDeviceType = PVRSRV_DEVICE_TYPE_EXT;
	psDeviceNode->sDevId.eDeviceClass = PVRSRV_DEVICE_CLASS_BUFFER;
	psDeviceNode->psSysData = psSysData;

	/* allocate a unique device id */
	if (AllocateDeviceID(psSysData, &psDeviceNode->sDevId.ui32DeviceIndex) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterBCDeviceKM: Failed to allocate Device ID"));
		goto ErrorExit;
	}
	psBCInfo->ui32DeviceID = psDeviceNode->sDevId.ui32DeviceIndex;
	if (pui32DeviceID)
	{
		*pui32DeviceID = psDeviceNode->sDevId.ui32DeviceIndex;
	}

	/* and finally insert the device into the dev-list */
	List_PVRSRV_DEVICE_NODE_Insert(&psSysData->psDeviceNodeList, psDeviceNode);

	return PVRSRV_OK;

ErrorExit:

	if(psBCInfo->psFuncTable)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PPVRSRV_BC_SRV2BUFFER_KMJTABLE), psBCInfo->psFuncTable, IMG_NULL);
		psBCInfo->psFuncTable = IMG_NULL;
	}

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_BUFFERCLASS_INFO), psBCInfo, IMG_NULL);
	/*not nulling shared pointer, wasn't allocated to this point*/

	return PVRSRV_ERROR_OUT_OF_MEMORY;
}


/*!
******************************************************************************

 @Function	PVRSRVRemoveBCDeviceKM

 @Description

 Removes external device from services system record

 @Input	   ui32DeviceIndex		: unique device key (for case of multiple identical devices)

 @Return   PVRSRV_ERROR  :

******************************************************************************/
static PVRSRV_ERROR PVRSRVRemoveBCDeviceKM(IMG_UINT32 ui32DevIndex)
{
	SYS_DATA					*psSysData;
	PVRSRV_DEVICE_NODE			*psDevNode;
	PVRSRV_BUFFERCLASS_INFO		*psBCInfo;

	SysAcquireData(&psSysData);

	/*search the device node with the devindex and buffer class*/
	psDevNode = (PVRSRV_DEVICE_NODE*)
		List_PVRSRV_DEVICE_NODE_Any_va(psSysData->psDeviceNodeList,
									   &MatchDeviceKM_AnyVaCb,
									   ui32DevIndex,
									   IMG_FALSE,
									   PVRSRV_DEVICE_CLASS_BUFFER);

	if (!psDevNode)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRemoveBCDeviceKM: requested device %d not present", ui32DevIndex));
		return PVRSRV_ERROR_NO_DEVICENODE_FOUND;
	}

	/* set-up devnode ptr */
/*	psDevNode = *(ppsDevNode); */
	/* setup BCInfo ptr */
	psBCInfo = (PVRSRV_BUFFERCLASS_INFO*)psDevNode->pvDevice;

	/*
		The device can only be removed if there are
		no open connections in the Services interface
	*/
	if(psBCInfo->ui32RefCount == 0)
	{
		/*
			Remove from the device list.
		*/
		List_PVRSRV_DEVICE_NODE_Remove(psDevNode);

		/*
			OK found a device with a matching devindex
			remove registration information
		*/
		(IMG_VOID)FreeDeviceID(psSysData, ui32DevIndex);
		(IMG_VOID)OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_BC_SRV2BUFFER_KMJTABLE), psBCInfo->psFuncTable, IMG_NULL);
		psBCInfo->psFuncTable = IMG_NULL;
		(IMG_VOID)OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_BUFFERCLASS_INFO), psBCInfo, IMG_NULL);
		/*not nulling pointer, copy on stack*/
		(IMG_VOID)OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_DEVICE_NODE), psDevNode, IMG_NULL);
		/*not nulling pointer, out of scope*/
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRemoveBCDeviceKM: failed as %d Services BC API connections are still open", psBCInfo->ui32RefCount));
		return PVRSRV_ERROR_UNABLE_TO_REMOVE_DEVICE;
	}

	return PVRSRV_OK;
}



/*!
******************************************************************************

 @Function	PVRSRVCloseDCDeviceKM

 @Description

 Closes a connection to the Display Class device

 @Input	   hDeviceKM		: device handle

 @Return   PVRSRV_ERROR  :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVCloseDCDeviceKM (IMG_HANDLE	hDeviceKM)
{
	PVRSRV_ERROR eError;
	PVRSRV_DISPLAYCLASS_PERCONTEXT_INFO *psDCPerContextInfo;

	psDCPerContextInfo = (PVRSRV_DISPLAYCLASS_PERCONTEXT_INFO *)hDeviceKM;

	/* Remove the item from the resman list and trigger the callback. */
	eError = ResManFreeResByPtr(psDCPerContextInfo->hResItem, CLEANUP_WITH_POLL);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVCloseDCDeviceKM: ResManFreeResByPtr failed %d", eError));
		PVR_DPF((PVR_DBG_ERROR, "ResManFreeResByPtr: hResItem 0x%x", (unsigned int)psDCPerContextInfo->hResItem));
	}
	return eError;
}


static PVRSRV_ERROR CloseDCDeviceCallBack(IMG_PVOID  pvParam,
										  IMG_UINT32 ui32Param,
										  IMG_BOOL   bDummy)
{
	PVRSRV_DISPLAYCLASS_PERCONTEXT_INFO *psDCPerContextInfo;
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;

	PVR_UNREFERENCED_PARAMETER(ui32Param);
	PVR_UNREFERENCED_PARAMETER(bDummy);

	psDCPerContextInfo = (PVRSRV_DISPLAYCLASS_PERCONTEXT_INFO *)pvParam;
	psDCInfo = psDCPerContextInfo->psDCInfo;

	if(psDCInfo->sSystemBuffer.sDeviceClassBuffer.ui32MemMapRefCount != 0)
	{
		PVR_DPF((PVR_DBG_MESSAGE,"CloseDCDeviceCallBack: system buffer (0x%p) still mapped (refcount = %d)",
				&psDCInfo->sSystemBuffer.sDeviceClassBuffer,
				psDCInfo->sSystemBuffer.sDeviceClassBuffer.ui32MemMapRefCount));
	}

	psDCInfo->ui32RefCount--;
	if(psDCInfo->ui32RefCount == 0)
	{
		/* close the external device */
		psDCInfo->psFuncTable->pfnCloseDCDevice(psDCInfo->hExtDevice);

		PVRSRVKernelSyncInfoDecRef(psDCInfo->sSystemBuffer.sDeviceClassBuffer.psKernelSyncInfo, IMG_NULL);

		psDCInfo->hDevMemContext = IMG_NULL;
		psDCInfo->hExtDevice = IMG_NULL;
	}

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_DISPLAYCLASS_PERCONTEXT_INFO), psDCPerContextInfo, IMG_NULL);
	/*not nulling pointer, copy on stack*/

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	PVRSRVOpenDCDeviceKM

 @Description

 Opens a connection to the Display Class device, associating the connection
 with a Device Memory Context for a services managed device

 @Input	   psPerProc		: Per-process data
 @Input	   ui32DeviceID		: unique device index
 @Input	   hDevCookie		: devcookie used to derive the Device Memory
 								Context into BC surfaces will be mapped into
 @Outut	   phDeviceKM		: handle to the DC device

 @Return   PVRSRV_ERROR  :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVOpenDCDeviceKM (PVRSRV_PER_PROCESS_DATA	*psPerProc,
								   IMG_UINT32				ui32DeviceID,
								   IMG_HANDLE				hDevCookie,
								   IMG_HANDLE				*phDeviceKM)
{
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	PVRSRV_DISPLAYCLASS_PERCONTEXT_INFO *psDCPerContextInfo;
	PVRSRV_DEVICE_NODE	*psDeviceNode;
	SYS_DATA			*psSysData;
	PVRSRV_ERROR eError;

	if(!phDeviceKM || !hDevCookie)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenDCDeviceKM: Invalid params"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	SysAcquireData(&psSysData);

	/* find the matching devicenode */
	psDeviceNode = (PVRSRV_DEVICE_NODE*)
			List_PVRSRV_DEVICE_NODE_Any_va(psSysData->psDeviceNodeList,
										   &MatchDeviceKM_AnyVaCb,
										   ui32DeviceID,
										   IMG_FALSE,
										   PVRSRV_DEVICE_CLASS_DISPLAY);
	if (!psDeviceNode)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenDCDeviceKM: no devnode matching index %d", ui32DeviceID));
		return PVRSRV_ERROR_NO_DEVICENODE_FOUND;
	}
	psDCInfo = (PVRSRV_DISPLAYCLASS_INFO*)psDeviceNode->pvDevice;

	/*
		Allocate the per-context DC Info before calling the external device,
		to make error handling easier.
	*/
	if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
				  sizeof(*psDCPerContextInfo),
				  (IMG_VOID **)&psDCPerContextInfo, IMG_NULL,
				  "Display Class per Context Info") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenDCDeviceKM: Failed psDCPerContextInfo alloc"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	OSMemSet(psDCPerContextInfo, 0, sizeof(*psDCPerContextInfo));

	if(psDCInfo->ui32RefCount++ == 0)
	{

		psDeviceNode = (PVRSRV_DEVICE_NODE *)hDevCookie;

		/* store the device kernel context to map into */
		psDCInfo->hDevMemContext = (IMG_HANDLE)psDeviceNode->sDevMemoryInfo.pBMKernelContext;

		/* create a syncinfo for the device's system surface */
		eError = PVRSRVAllocSyncInfoKM(IMG_NULL,
									(IMG_HANDLE)psDeviceNode->sDevMemoryInfo.pBMKernelContext,
									&psDCInfo->sSystemBuffer.sDeviceClassBuffer.psKernelSyncInfo);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenDCDeviceKM: Failed sync info alloc"));
			psDCInfo->ui32RefCount--;
			return eError;
		}

		/* open the external device */
		eError = psDCInfo->psFuncTable->pfnOpenDCDevice(ui32DeviceID,
                                                        	&psDCInfo->hExtDevice,
								(PVRSRV_SYNC_DATA*)psDCInfo->sSystemBuffer.sDeviceClassBuffer.psKernelSyncInfo->psSyncDataMemInfoKM->pvLinAddrKM);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenDCDeviceKM: Failed to open external DC device"));
			psDCInfo->ui32RefCount--;
			PVRSRVKernelSyncInfoDecRef(psDCInfo->sSystemBuffer.sDeviceClassBuffer.psKernelSyncInfo, IMG_NULL);
			return eError;
		}

		psDCPerContextInfo->psDCInfo = psDCInfo;
		eError = PVRSRVGetDCSystemBufferKM(psDCPerContextInfo, IMG_NULL);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenDCDeviceKM: Failed to get system buffer"));
			psDCInfo->ui32RefCount--;
			PVRSRVKernelSyncInfoDecRef(psDCInfo->sSystemBuffer.sDeviceClassBuffer.psKernelSyncInfo, IMG_NULL);
			return eError;
		}
		psDCInfo->sSystemBuffer.sDeviceClassBuffer.ui32MemMapRefCount = 0;
	}
	else
	{
		psDCPerContextInfo->psDCInfo = psDCInfo;
	}

	psDCPerContextInfo->hResItem = ResManRegisterRes(psPerProc->hResManContext,
													 RESMAN_TYPE_DISPLAYCLASS_DEVICE,
													 psDCPerContextInfo,
													 0,
													 &CloseDCDeviceCallBack);

	/* return a reference to the DCPerContextInfo */
	*phDeviceKM = (IMG_HANDLE)psDCPerContextInfo;

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	PVRSRVEnumDCFormatsKM

 @Description

 Enumerates the devices pixel formats

 @Input	   hDeviceKM		: device handle
 @Output   pui32Count		: number of pixel formats
 @Output   psFormat			: format list

 @Return   PVRSRV_ERROR  :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVEnumDCFormatsKM (IMG_HANDLE hDeviceKM,
									IMG_UINT32 *pui32Count,
									DISPLAY_FORMAT *psFormat)
{
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;

	if(!hDeviceKM || !pui32Count || !psFormat)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVEnumDCFormatsKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);

	/* call into the display device driver to get info */
	return psDCInfo->psFuncTable->pfnEnumDCFormats(psDCInfo->hExtDevice, pui32Count, psFormat);
}



/*!
******************************************************************************

 @Function	PVRSRVEnumDCDimsKM

 @Description

 Enumerates the devices mode dimensions for a given pixel format

 @Input	   hDeviceKM		: device handle
 @Input	   psFormat			: pixel format
 @Output   pui32Count		: number of dimensions
 @Output   psDim			: dimensions list

 @Return   PVRSRV_ERROR  :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVEnumDCDimsKM (IMG_HANDLE hDeviceKM,
								 DISPLAY_FORMAT *psFormat,
								 IMG_UINT32 *pui32Count,
								 DISPLAY_DIMS *psDim)
{
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;

	if(!hDeviceKM || !pui32Count || !psFormat)	//  psDim==NULL to query number of dims
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVEnumDCDimsKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);

	/* call into the display device driver to get info */
	return psDCInfo->psFuncTable->pfnEnumDCDims(psDCInfo->hExtDevice, psFormat, pui32Count, psDim);
}


/*!
******************************************************************************

 @Function	PVRSRVGetDCSystemBufferKM

 @Description

 Get the primary surface and optionally return its buffer handle

 @Input	   hDeviceKM		: device handle
 @Output   phBuffer			: Optional buffer handle

 @Return   PVRSRV_ERROR  :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVGetDCSystemBufferKM (IMG_HANDLE hDeviceKM,
										IMG_HANDLE *phBuffer)
{
	PVRSRV_ERROR eError;
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	IMG_HANDLE hExtBuffer;

	if(!hDeviceKM)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVGetDCSystemBufferKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);

	/* call into the display device driver to get info */
	eError = psDCInfo->psFuncTable->pfnGetDCSystemBuffer(psDCInfo->hExtDevice, &hExtBuffer);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVGetDCSystemBufferKM: Failed to get valid buffer handle from external driver"));
		return eError;
	}

	/* save the new info */
	psDCInfo->sSystemBuffer.sDeviceClassBuffer.pfnGetBufferAddr = psDCInfo->psFuncTable->pfnGetBufferAddr;
	psDCInfo->sSystemBuffer.sDeviceClassBuffer.hDevMemContext = psDCInfo->hDevMemContext;
	psDCInfo->sSystemBuffer.sDeviceClassBuffer.hExtDevice = psDCInfo->hExtDevice;
	psDCInfo->sSystemBuffer.sDeviceClassBuffer.hExtBuffer = hExtBuffer;

	psDCInfo->sSystemBuffer.psDCInfo = psDCInfo;

	/* return handle */
	if (phBuffer)
	{
		*phBuffer = (IMG_HANDLE)&(psDCInfo->sSystemBuffer);
	}

	return PVRSRV_OK;
}


/******************************************************************************

 @Function	PVRSRVGetDCInfoKM

 @Description

 Gets Display Class device Info

 @Input		hDeviceKM		: device handle
 @Output	psDisplayInfo

 @Return   PVRSRV_ERROR  :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVGetDCInfoKM (IMG_HANDLE hDeviceKM,
								DISPLAY_INFO *psDisplayInfo)
{
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	PVRSRV_ERROR eError;

	if(!hDeviceKM || !psDisplayInfo)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVGetDCInfoKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);

	/* call into the display device driver to get info */
	eError = psDCInfo->psFuncTable->pfnGetDCInfo(psDCInfo->hExtDevice, psDisplayInfo);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	if (psDisplayInfo->ui32MaxSwapChainBuffers > PVRSRV_MAX_DC_SWAPCHAIN_BUFFERS)
	{
		psDisplayInfo->ui32MaxSwapChainBuffers = PVRSRV_MAX_DC_SWAPCHAIN_BUFFERS;
	}

	return PVRSRV_OK;
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVDestroyDCSwapChainKM(IMG_HANDLE hSwapChainRef)
{
	PVRSRV_ERROR eError;
	PVRSRV_DC_SWAPCHAIN_REF *psSwapChainRef;

	if(!hSwapChainRef)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDestroyDCSwapChainKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psSwapChainRef = hSwapChainRef;

	eError = ResManFreeResByPtr(psSwapChainRef->hResItem, CLEANUP_WITH_POLL);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVDestroyDCSwapChainKM: ResManFreeResByPtr failed %d", eError));
		PVR_DPF((PVR_DBG_ERROR, "ResManFreeResByPtr: hResItem 0x%x", (unsigned int)psSwapChainRef->hResItem));
	}
	return eError;
}


static PVRSRV_ERROR DestroyDCSwapChain(PVRSRV_DC_SWAPCHAIN *psSwapChain)
{
	PVRSRV_ERROR				eError;
	PVRSRV_DISPLAYCLASS_INFO	*psDCInfo = psSwapChain->psDCInfo;
	IMG_UINT32 i;

	/* Update shared swapchains list */
	if( psDCInfo->psDCSwapChainShared )
	{
		if( psDCInfo->psDCSwapChainShared == psSwapChain )
		{
			psDCInfo->psDCSwapChainShared = psSwapChain->psNext;
		}
		else 
		{
			PVRSRV_DC_SWAPCHAIN *psCurrentSwapChain;
			psCurrentSwapChain = psDCInfo->psDCSwapChainShared; 		
			while( psCurrentSwapChain->psNext )
			{
				if( psCurrentSwapChain->psNext != psSwapChain ) 
				{
					psCurrentSwapChain = psCurrentSwapChain->psNext;
					continue;
				}
				psCurrentSwapChain->psNext = psSwapChain->psNext;
				break;				
			}
		}
	}

	/* Destroy command queue before swapchain - it may use the swapchain when commands are flushed. */
	PVRSRVDestroyCommandQueueKM(psSwapChain->psQueue);

	/* call into the display device driver to destroy a swapchain */
	eError = psDCInfo->psFuncTable->pfnDestroyDCSwapChain(psDCInfo->hExtDevice,
															psSwapChain->hExtSwapChain);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"DestroyDCSwapChainCallBack: Failed to destroy DC swap chain"));
		return eError;
	}

	/* free the resources */
	for(i=0; i<psSwapChain->ui32BufferCount; i++)
	{
		if(psSwapChain->asBuffer[i].sDeviceClassBuffer.psKernelSyncInfo)
		{
			PVRSRVKernelSyncInfoDecRef(psSwapChain->asBuffer[i].sDeviceClassBuffer.psKernelSyncInfo, IMG_NULL);
		}
	}

#if !defined(SUPPORT_DC_CMDCOMPLETE_WHEN_NO_LONGER_DISPLAYED)
	if (psSwapChain->ppsLastSyncInfos)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_KERNEL_SYNC_INFO *) * psSwapChain->ui32LastNumSyncInfos,
					psSwapChain->ppsLastSyncInfos, IMG_NULL);
	}
#endif /* !defined(SUPPORT_DC_CMDCOMPLETE_WHEN_NO_LONGER_DISPLAYED) */

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_DC_SWAPCHAIN), psSwapChain, IMG_NULL);
	/*not nulling pointer, copy on stack*/

	return eError;
}


static PVRSRV_ERROR DestroyDCSwapChainRefCallBack(IMG_PVOID pvParam,
												  IMG_UINT32 ui32Param,
												  IMG_BOOL bDummy)
{
	PVRSRV_DC_SWAPCHAIN_REF *psSwapChainRef = (PVRSRV_DC_SWAPCHAIN_REF *) pvParam;
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 i;

	PVR_UNREFERENCED_PARAMETER(ui32Param);
	PVR_UNREFERENCED_PARAMETER(bDummy);

	for (i = 0; i < psSwapChainRef->psSwapChain->ui32BufferCount; i++)
	{
		if (psSwapChainRef->psSwapChain->asBuffer[i].sDeviceClassBuffer.ui32MemMapRefCount != 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "DestroyDCSwapChainRefCallBack: swapchain (0x%p) still mapped (ui32MemMapRefCount = %d)",
					&psSwapChainRef->psSwapChain->asBuffer[i].sDeviceClassBuffer,
					psSwapChainRef->psSwapChain->asBuffer[i].sDeviceClassBuffer.ui32MemMapRefCount));
		}
	}

	if(--psSwapChainRef->psSwapChain->ui32RefCount == 0) 
	{
		eError = DestroyDCSwapChain(psSwapChainRef->psSwapChain);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "DestroyDCSwapChainRefCallBack: DestroyDCSwapChain Failed %d", eError));
			PVR_DPF((PVR_DBG_ERROR, "DestroyDCSwapChainRefCallBack: psSwapChain 0x%x", (unsigned int)psSwapChainRef->psSwapChain));
		}
	}

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_DC_SWAPCHAIN_REF), psSwapChainRef, IMG_NULL);
	return eError;
}

static PVRSRV_DC_SWAPCHAIN* PVRSRVFindSharedDCSwapChainKM(PVRSRV_DISPLAYCLASS_INFO *psDCInfo,
														 IMG_UINT32 ui32SwapChainID)
{
	PVRSRV_DC_SWAPCHAIN *psCurrentSwapChain;

	for(psCurrentSwapChain = psDCInfo->psDCSwapChainShared; 
		psCurrentSwapChain; 
		psCurrentSwapChain = psCurrentSwapChain->psNext) 
	{
		if(psCurrentSwapChain->ui32SwapChainID == ui32SwapChainID)
			return psCurrentSwapChain;
	}
	return IMG_NULL;
}

static PVRSRV_ERROR PVRSRVCreateDCSwapChainRefKM(PVRSRV_PER_PROCESS_DATA	*psPerProc,
												 PVRSRV_DC_SWAPCHAIN 		*psSwapChain, 
												 PVRSRV_DC_SWAPCHAIN_REF 	**ppsSwapChainRef)
{
	PVRSRV_DC_SWAPCHAIN_REF *psSwapChainRef = IMG_NULL;

	/* Allocate swapchain reference structre*/
	if(OSAllocMem( PVRSRV_OS_PAGEABLE_HEAP,
					 sizeof(PVRSRV_DC_SWAPCHAIN_REF),
					 (IMG_VOID **)&psSwapChainRef, IMG_NULL,
					 "Display Class Swapchain Reference") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateDCSwapChainRefKM: Failed psSwapChainRef alloc"));
		return  PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	OSMemSet (psSwapChainRef, 0, sizeof(PVRSRV_DC_SWAPCHAIN_REF));

	/* Bump refcount */
	psSwapChain->ui32RefCount++;

	/* Create reference resource */
	psSwapChainRef->psSwapChain = psSwapChain;
	psSwapChainRef->hResItem = ResManRegisterRes(psPerProc->hResManContext,
												  RESMAN_TYPE_DISPLAYCLASS_SWAPCHAIN_REF,
												  psSwapChainRef,
												  0,
												  &DestroyDCSwapChainRefCallBack);

	*ppsSwapChainRef = psSwapChainRef;

	return PVRSRV_OK;
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVCreateDCSwapChainKM (PVRSRV_PER_PROCESS_DATA	*psPerProc,
										IMG_HANDLE				hDeviceKM,
										IMG_UINT32				ui32Flags,
										DISPLAY_SURF_ATTRIBUTES	*psDstSurfAttrib,
										DISPLAY_SURF_ATTRIBUTES *psSrcSurfAttrib,
										IMG_UINT32				ui32BufferCount,
										IMG_UINT32				ui32OEMFlags,
										IMG_HANDLE				*phSwapChainRef,
										IMG_UINT32				*pui32SwapChainID)
{
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	PVRSRV_DC_SWAPCHAIN *psSwapChain = IMG_NULL;
	PVRSRV_DC_SWAPCHAIN_REF *psSwapChainRef = IMG_NULL;
	PVRSRV_SYNC_DATA *apsSyncData[PVRSRV_MAX_DC_SWAPCHAIN_BUFFERS];
	PVRSRV_QUEUE_INFO *psQueue = IMG_NULL;
	PVRSRV_ERROR eError;
	IMG_UINT32 i;
	DISPLAY_INFO sDisplayInfo;


	if(!hDeviceKM
	|| !psDstSurfAttrib
	|| !psSrcSurfAttrib
	|| !phSwapChainRef
	|| !pui32SwapChainID)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateDCSwapChainKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (ui32BufferCount > PVRSRV_MAX_DC_SWAPCHAIN_BUFFERS)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateDCSwapChainKM: Too many buffers"));
		return PVRSRV_ERROR_TOOMANYBUFFERS;
	}

	if (ui32BufferCount < 2)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateDCSwapChainKM: Too few buffers"));
		return PVRSRV_ERROR_TOO_FEW_BUFFERS;
	}

	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);

	if( ui32Flags & PVRSRV_CREATE_SWAPCHAIN_QUERY )
	{
		/* Query - use pui32SwapChainID as input */
		psSwapChain = PVRSRVFindSharedDCSwapChainKM(psDCInfo, *pui32SwapChainID );
		if( psSwapChain  ) 
		{	
			/* Create new reference */		   
			eError = PVRSRVCreateDCSwapChainRefKM(psPerProc, 
												  psSwapChain, 
												  &psSwapChainRef);
			if( eError != PVRSRV_OK ) 
			{
				PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateDCSwapChainKM: Couldn't create swap chain reference"));
				return eError;
			}

			*phSwapChainRef = (IMG_HANDLE)psSwapChainRef;
			return PVRSRV_OK;
		}
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateDCSwapChainKM: No shared SwapChain found for query"));
		return PVRSRV_ERROR_FLIP_CHAIN_EXISTS;		
	}

	/* Allocate swapchain control structure for srvkm */
	if(OSAllocMem( PVRSRV_OS_PAGEABLE_HEAP,
					 sizeof(PVRSRV_DC_SWAPCHAIN),
					 (IMG_VOID **)&psSwapChain, IMG_NULL,
					 "Display Class Swapchain") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateDCSwapChainKM: Failed psSwapChain alloc"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorExit;
	}
	OSMemSet (psSwapChain, 0, sizeof(PVRSRV_DC_SWAPCHAIN));

	/* Create a command queue for the swapchain	*/
	eError = PVRSRVCreateCommandQueueKM(1024, &psQueue);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateDCSwapChainKM: Failed to create CmdQueue"));
		goto ErrorExit;
	}

	/* store the Queue */
	psSwapChain->psQueue = psQueue;

	/* Create a Sync Object for each surface in the swapchain */
	for(i=0; i<ui32BufferCount; i++)
	{
		eError = PVRSRVAllocSyncInfoKM(IMG_NULL,
										psDCInfo->hDevMemContext,
										&psSwapChain->asBuffer[i].sDeviceClassBuffer.psKernelSyncInfo);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateDCSwapChainKM: Failed to alloc syninfo for psSwapChain"));
			goto ErrorExit;
		}

		/* setup common device class info */
		psSwapChain->asBuffer[i].sDeviceClassBuffer.pfnGetBufferAddr = psDCInfo->psFuncTable->pfnGetBufferAddr;
		psSwapChain->asBuffer[i].sDeviceClassBuffer.hDevMemContext = psDCInfo->hDevMemContext;
		psSwapChain->asBuffer[i].sDeviceClassBuffer.hExtDevice = psDCInfo->hExtDevice;

		/* save off useful ptrs */
		psSwapChain->asBuffer[i].psDCInfo = psDCInfo;
		psSwapChain->asBuffer[i].psSwapChain = psSwapChain;

		/* syncinfos must be passed as array of syncdata ptrs to the 3rd party driver */
		apsSyncData[i] = (PVRSRV_SYNC_DATA*)psSwapChain->asBuffer[i].sDeviceClassBuffer.psKernelSyncInfo->psSyncDataMemInfoKM->pvLinAddrKM;
	}

	psSwapChain->ui32BufferCount = ui32BufferCount;
	psSwapChain->psDCInfo = psDCInfo;

#if defined(PDUMP)
	PDUMPCOMMENT("Allocate DC swap chain (SwapChainID == %u, BufferCount == %u)",
			*pui32SwapChainID,
			ui32BufferCount);
	PDUMPCOMMENT("  Src surface dimensions == %u x %u",
			psSrcSurfAttrib->sDims.ui32Width,
			psSrcSurfAttrib->sDims.ui32Height);
	PDUMPCOMMENT("  Dst surface dimensions == %u x %u",
			psDstSurfAttrib->sDims.ui32Width,
			psDstSurfAttrib->sDims.ui32Height);
#endif

	eError = psDCInfo->psFuncTable->pfnGetDCInfo(psDCInfo->hExtDevice, &sDisplayInfo);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateDCSwapChainKM: Failed to get DC info"));
		return eError;
	}
	
	psSwapChain->ui32MinSwapInterval = sDisplayInfo.ui32MinSwapInterval;
	psSwapChain->ui32MaxSwapInterval = sDisplayInfo.ui32MaxSwapInterval;

	/* call into the display device driver to create a swapchain */
	eError =  psDCInfo->psFuncTable->pfnCreateDCSwapChain(psDCInfo->hExtDevice,
														ui32Flags,
														psDstSurfAttrib,
														psSrcSurfAttrib,
														ui32BufferCount,
														apsSyncData,
														ui32OEMFlags,
														&psSwapChain->hExtSwapChain,
														&psSwapChain->ui32SwapChainID);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateDCSwapChainKM: Failed to create 3rd party SwapChain"));
		PDUMPCOMMENT("Swapchain allocation failed.");
		goto ErrorExit;
	}

	/* Create new reference */		   
	eError = PVRSRVCreateDCSwapChainRefKM(psPerProc, 
										  psSwapChain, 
										  &psSwapChainRef);
	if( eError != PVRSRV_OK ) 
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateDCSwapChainKM: Couldn't create swap chain reference"));
		PDUMPCOMMENT("Swapchain allocation failed.");
		goto ErrorExit;
	}

	psSwapChain->ui32RefCount = 1;
	psSwapChain->ui32Flags = ui32Flags;

	/* Save pointer in DC structure if ti's shared struct */
	if( ui32Flags & PVRSRV_CREATE_SWAPCHAIN_SHARED )
	{
   		if(! psDCInfo->psDCSwapChainShared ) 
		{
			psDCInfo->psDCSwapChainShared = psSwapChain;
		} 
		else 
		{	
			PVRSRV_DC_SWAPCHAIN *psOldHead = psDCInfo->psDCSwapChainShared;
			psDCInfo->psDCSwapChainShared = psSwapChain;
			psSwapChain->psNext = psOldHead;
		}
	}

	/* We create swapchain - pui32SwapChainID is output */
	*pui32SwapChainID = psSwapChain->ui32SwapChainID;

	/* return the swapchain reference handle */
	*phSwapChainRef= (IMG_HANDLE)psSwapChainRef;

	return eError;

ErrorExit:

	for(i=0; i<ui32BufferCount; i++)
	{
		if(psSwapChain->asBuffer[i].sDeviceClassBuffer.psKernelSyncInfo)
		{
			PVRSRVKernelSyncInfoDecRef(psSwapChain->asBuffer[i].sDeviceClassBuffer.psKernelSyncInfo, IMG_NULL);
		}
	}

	if(psQueue)
	{
		PVRSRVDestroyCommandQueueKM(psQueue);
	}

	if(psSwapChain)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_DC_SWAPCHAIN), psSwapChain, IMG_NULL);
		/*not nulling pointer, out of scope*/
	}

	return eError;
}




IMG_EXPORT
PVRSRV_ERROR PVRSRVSetDCDstRectKM(IMG_HANDLE	hDeviceKM,
								  IMG_HANDLE	hSwapChainRef,
								  IMG_RECT		*psRect)
{
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	PVRSRV_DC_SWAPCHAIN *psSwapChain;

	if(!hDeviceKM || !hSwapChainRef)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSetDCDstRectKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);
	psSwapChain = ((PVRSRV_DC_SWAPCHAIN_REF*)hSwapChainRef)->psSwapChain;

	return psDCInfo->psFuncTable->pfnSetDCDstRect(psDCInfo->hExtDevice,
													psSwapChain->hExtSwapChain,
													psRect);
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVSetDCSrcRectKM(IMG_HANDLE	hDeviceKM,
								  IMG_HANDLE	hSwapChainRef,
								  IMG_RECT		*psRect)
{
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	PVRSRV_DC_SWAPCHAIN *psSwapChain;

	if(!hDeviceKM || !hSwapChainRef)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSetDCSrcRectKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);
	psSwapChain = ((PVRSRV_DC_SWAPCHAIN_REF*)hSwapChainRef)->psSwapChain;

	return psDCInfo->psFuncTable->pfnSetDCSrcRect(psDCInfo->hExtDevice,
													psSwapChain->hExtSwapChain,
													psRect);
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVSetDCDstColourKeyKM(IMG_HANDLE	hDeviceKM,
									   IMG_HANDLE	hSwapChainRef,
									   IMG_UINT32	ui32CKColour)
{
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	PVRSRV_DC_SWAPCHAIN *psSwapChain;

	if(!hDeviceKM || !hSwapChainRef)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSetDCDstColourKeyKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);
	psSwapChain = ((PVRSRV_DC_SWAPCHAIN_REF*)hSwapChainRef)->psSwapChain;

	return psDCInfo->psFuncTable->pfnSetDCDstColourKey(psDCInfo->hExtDevice,
														psSwapChain->hExtSwapChain,
														ui32CKColour);
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVSetDCSrcColourKeyKM(IMG_HANDLE	hDeviceKM,
									   IMG_HANDLE	hSwapChainRef,
									   IMG_UINT32	ui32CKColour)
{
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	PVRSRV_DC_SWAPCHAIN *psSwapChain;

	if(!hDeviceKM || !hSwapChainRef)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSetDCSrcColourKeyKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);
	psSwapChain = ((PVRSRV_DC_SWAPCHAIN_REF*)hSwapChainRef)->psSwapChain;

	return psDCInfo->psFuncTable->pfnSetDCSrcColourKey(psDCInfo->hExtDevice,
														psSwapChain->hExtSwapChain,
														ui32CKColour);
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVGetDCBuffersKM(IMG_HANDLE	hDeviceKM,
								  IMG_HANDLE	hSwapChainRef,
								  IMG_UINT32	*pui32BufferCount,
								  IMG_HANDLE	*phBuffer,
								  IMG_SYS_PHYADDR *psPhyAddr)
{
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	PVRSRV_DC_SWAPCHAIN *psSwapChain;
	IMG_HANDLE ahExtBuffer[PVRSRV_MAX_DC_SWAPCHAIN_BUFFERS];
	PVRSRV_ERROR eError;
	IMG_UINT32 i;

	if(!hDeviceKM || !hSwapChainRef || !phBuffer || !psPhyAddr)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVGetDCBuffersKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);
	psSwapChain = ((PVRSRV_DC_SWAPCHAIN_REF*)hSwapChainRef)->psSwapChain;

	/* call into the display device driver to get info */
	eError = psDCInfo->psFuncTable->pfnGetDCBuffers(psDCInfo->hExtDevice,
													psSwapChain->hExtSwapChain,
													pui32BufferCount,
													ahExtBuffer);

	PVR_ASSERT(*pui32BufferCount <= PVRSRV_MAX_DC_SWAPCHAIN_BUFFERS);

	/*
		populate the srvkm's buffer structure with the 3rd party buffer handles
		and return the services buffer handles
	*/
	for(i=0; i<*pui32BufferCount; i++)
	{
		psSwapChain->asBuffer[i].sDeviceClassBuffer.hExtBuffer = ahExtBuffer[i];
		phBuffer[i] = (IMG_HANDLE)&psSwapChain->asBuffer[i];
	}

	return eError;
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVSwapToDCBufferKM(IMG_HANDLE	hDeviceKM,
									IMG_HANDLE	hBuffer,
									IMG_UINT32	ui32SwapInterval,
									IMG_HANDLE	hPrivateTag,
									IMG_UINT32	ui32ClipRectCount,
									IMG_RECT	*psClipRect)
{
	PVRSRV_ERROR eError;
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	PVRSRV_DC_BUFFER *psBuffer;
	PVRSRV_QUEUE_INFO *psQueue;
	DISPLAYCLASS_FLIP_COMMAND *psFlipCmd;
	IMG_UINT32 i;
	IMG_BOOL bAddReferenceToLast = IMG_TRUE;
	IMG_UINT16 ui16SwapCommandID = DC_FLIP_COMMAND;
	IMG_UINT32 ui32NumSrcSyncs = 1;
	PVRSRV_KERNEL_SYNC_INFO *apsSrcSync[2];
	PVRSRV_COMMAND *psCommand;
	SYS_DATA *psSysData;

	if(!hDeviceKM || !hBuffer || !psClipRect)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCBufferKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psBuffer = (PVRSRV_DC_BUFFER*)hBuffer;
	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);

	/* Validate swap interval against limits */
	if(ui32SwapInterval < psBuffer->psSwapChain->ui32MinSwapInterval ||
		ui32SwapInterval > psBuffer->psSwapChain->ui32MaxSwapInterval)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCBufferKM: Invalid swap interval. Requested %u, Allowed range %u-%u",
				 ui32SwapInterval, psBuffer->psSwapChain->ui32MinSwapInterval, psBuffer->psSwapChain->ui32MaxSwapInterval));
		return PVRSRV_ERROR_INVALID_SWAPINTERVAL;
	}

#if defined(SUPPORT_CUSTOM_SWAP_OPERATIONS)

	if(psDCInfo->psFuncTable->pfnQuerySwapCommandID != IMG_NULL)
	{
		psDCInfo->psFuncTable->pfnQuerySwapCommandID(psDCInfo->hExtDevice,
													 psBuffer->psSwapChain->hExtSwapChain,
													 psBuffer->sDeviceClassBuffer.hExtBuffer, 
													 hPrivateTag, 
													 &ui16SwapCommandID,
													 &bAddReferenceToLast);
		
	}

#endif

	/* get the queue from the buffer structure */
	psQueue = psBuffer->psSwapChain->psQueue;

	/* specify the syncs */
	apsSrcSync[0] = psBuffer->sDeviceClassBuffer.psKernelSyncInfo;
	if(bAddReferenceToLast && psBuffer->psSwapChain->psLastFlipBuffer &&
		psBuffer != psBuffer->psSwapChain->psLastFlipBuffer)
	{
		apsSrcSync[1] = psBuffer->psSwapChain->psLastFlipBuffer->sDeviceClassBuffer.psKernelSyncInfo;
		ui32NumSrcSyncs++;
	}

	/* insert the command (header) */
	eError = PVRSRVInsertCommandKM (psQueue,
									&psCommand,
									psDCInfo->ui32DeviceID,
									ui16SwapCommandID,
									0,
									IMG_NULL,
									ui32NumSrcSyncs,
									apsSrcSync,
									sizeof(DISPLAYCLASS_FLIP_COMMAND) + (sizeof(IMG_RECT) * ui32ClipRectCount),
									IMG_NULL,
									IMG_NULL);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCBufferKM: Failed to get space in queue"));
		goto Exit;
	}

	/*  setup the flip command */
	psFlipCmd = (DISPLAYCLASS_FLIP_COMMAND*)psCommand->pvData;

	/* Ext Device Handle */
	psFlipCmd->hExtDevice = psDCInfo->hExtDevice;

	/* Ext SwapChain Handle */
	psFlipCmd->hExtSwapChain = psBuffer->psSwapChain->hExtSwapChain;

	/* Ext Buffer Handle (Buffer to Flip to) */
	psFlipCmd->hExtBuffer = psBuffer->sDeviceClassBuffer.hExtBuffer;

	/* private tag */
	psFlipCmd->hPrivateTag = hPrivateTag;

	/* setup the clip rects */
	psFlipCmd->ui32ClipRectCount = ui32ClipRectCount;
	/* cliprect memory appends the command structure */
	psFlipCmd->psClipRect = (IMG_RECT*)((IMG_UINT8*)psFlipCmd + sizeof(DISPLAYCLASS_FLIP_COMMAND));	// PRQA S 3305
	/* copy the clip rects */
	for(i=0; i<ui32ClipRectCount; i++)
	{
		psFlipCmd->psClipRect[i] = psClipRect[i];
	}

	/* number of vsyncs between successive flips */
	psFlipCmd->ui32SwapInterval = ui32SwapInterval;

	SysAcquireData(&psSysData);

	/* Because we might be composing just software surfaces, without
	 * any SGX renders since the last frame, we won't necessarily
	 * have cleaned/flushed the CPU caches before the buffers need
	 * to be displayed.
	 *
	 * Doing so now is safe because InsertCommand bumped ROP2 on the
	 * affected buffers (preventing more SW renders starting) but the
	 * display won't start to process the buffers until SubmitCommand.
	 */
	{
		if(psSysData->ePendingCacheOpType == PVRSRV_MISC_INFO_CPUCACHEOP_FLUSH)
		{
			OSFlushCPUCacheKM();
		}
		else if(psSysData->ePendingCacheOpType == PVRSRV_MISC_INFO_CPUCACHEOP_CLEAN)
		{
			OSCleanCPUCacheKM();
		}

		psSysData->ePendingCacheOpType = PVRSRV_MISC_INFO_CPUCACHEOP_NONE;
	}

	/* submit the command */
	eError = PVRSRVSubmitCommandKM (psQueue, psCommand);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCBufferKM: Failed to submit command"));
		goto Exit;
	}

	/*
		Schedule an MISR to process it
	*/
    eError = OSScheduleMISR(psSysData);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCBufferKM: Failed to schedule MISR"));
		goto Exit;
	}

	/* update the last flip buffer */
	psBuffer->psSwapChain->psLastFlipBuffer = psBuffer;

Exit:

	if(eError == PVRSRV_ERROR_CANNOT_GET_QUEUE_SPACE)
	{
		eError = PVRSRV_ERROR_RETRY;
	}

	return eError;
}

typedef struct _CALLBACK_DATA_
{
	IMG_PVOID	pvPrivData;
	IMG_UINT32	ui32PrivDataLength;
	IMG_PVOID	ppvMemInfos;
	IMG_UINT32	ui32NumMemInfos;
} CALLBACK_DATA;

static IMG_VOID FreePrivateData(IMG_HANDLE hCallbackData)
{
	CALLBACK_DATA *psCallbackData = hCallbackData;

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, psCallbackData->ui32PrivDataLength,
			  psCallbackData->pvPrivData, IMG_NULL);
	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
			  sizeof(IMG_VOID *) * psCallbackData->ui32NumMemInfos,
			  psCallbackData->ppvMemInfos, IMG_NULL);
	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(CALLBACK_DATA), hCallbackData, IMG_NULL);
}

IMG_EXPORT
PVRSRV_ERROR PVRSRVSwapToDCBuffer2KM(IMG_HANDLE	hDeviceKM,
									 IMG_HANDLE	hSwapChain,
									 IMG_UINT32	ui32SwapInterval,
									 PVRSRV_KERNEL_MEM_INFO **ppsMemInfos,
									 PVRSRV_KERNEL_SYNC_INFO **ppsSyncInfos,
									 IMG_UINT32	ui32NumMemSyncInfos,
									 IMG_PVOID	pvPrivData,
									 IMG_UINT32	ui32PrivDataLength)
{
	PVRSRV_KERNEL_SYNC_INFO **ppsCompiledSyncInfos;
	IMG_UINT32 i, ui32NumCompiledSyncInfos;
	DISPLAYCLASS_FLIP_COMMAND2 *psFlipCmd;
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	PVRSRV_DC_SWAPCHAIN *psSwapChain;
	PVRSRV_ERROR eError = PVRSRV_OK;
	CALLBACK_DATA *psCallbackData;
	PVRSRV_QUEUE_INFO *psQueue;
	PVRSRV_COMMAND *psCommand;
	IMG_PVOID *ppvMemInfos;
	SYS_DATA *psSysData;

	if(!hDeviceKM || !hSwapChain || !ppsMemInfos || !ppsSyncInfos || ui32NumMemSyncInfos < 1)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCBuffer2KM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psSwapChain = ((PVRSRV_DC_SWAPCHAIN_REF*)hSwapChain)->psSwapChain;
	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);

	/* Validate swap interval against limits */
	if(ui32SwapInterval < psSwapChain->ui32MinSwapInterval ||
	   ui32SwapInterval > psSwapChain->ui32MaxSwapInterval)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCBuffer2KM: Invalid swap interval. Requested %u, Allowed range %u-%u",
				 ui32SwapInterval, psSwapChain->ui32MinSwapInterval, psSwapChain->ui32MaxSwapInterval));
		return PVRSRV_ERROR_INVALID_SWAPINTERVAL;
	}

	eError = OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
					  sizeof(CALLBACK_DATA),
					  (IMG_VOID **)&psCallbackData, IMG_NULL,
					  "PVRSRVSwapToDCBuffer2KM callback data");
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	psCallbackData->pvPrivData = pvPrivData;
	psCallbackData->ui32PrivDataLength = ui32PrivDataLength;

	if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
				  sizeof(IMG_VOID *) * ui32NumMemSyncInfos,
				  (IMG_VOID **)&ppvMemInfos, IMG_NULL,
				  "Swap Command Meminfos") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCBuffer2KM: Failed to allocate space for meminfo list"));
		psCallbackData->ppvMemInfos = IMG_NULL;
		goto Exit;
	}

	for(i = 0; i < ui32NumMemSyncInfos; i++)
	{
		ppvMemInfos[i] = ppsMemInfos[i];
	}

	psCallbackData->ppvMemInfos = ppvMemInfos;
	psCallbackData->ui32NumMemInfos = ui32NumMemSyncInfos;

	/* get the queue from the buffer structure */
	psQueue = psSwapChain->psQueue;

#if !defined(SUPPORT_DC_CMDCOMPLETE_WHEN_NO_LONGER_DISPLAYED)
	if(psSwapChain->ppsLastSyncInfos)
	{
		IMG_UINT32 ui32NumUniqueSyncInfos = psSwapChain->ui32LastNumSyncInfos;
		IMG_UINT32 j;

		for(j = 0; j < psSwapChain->ui32LastNumSyncInfos; j++)
		{
			for(i = 0; i < ui32NumMemSyncInfos; i++)
			{
				if(psSwapChain->ppsLastSyncInfos[j] == ppsSyncInfos[i])
				{
					psSwapChain->ppsLastSyncInfos[j] = IMG_NULL;
					ui32NumUniqueSyncInfos--;
				}
			}
		}

		ui32NumCompiledSyncInfos = ui32NumMemSyncInfos + ui32NumUniqueSyncInfos;

		if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
					  sizeof(PVRSRV_KERNEL_SYNC_INFO *) * ui32NumCompiledSyncInfos,
					  (IMG_VOID **)&ppsCompiledSyncInfos, IMG_NULL,
					  "Compiled syncinfos") != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCBuffer2KM: Failed to allocate space for meminfo list"));
			goto Exit;
		}
				
		OSMemCopy(ppsCompiledSyncInfos, ppsSyncInfos, sizeof(PVRSRV_KERNEL_SYNC_INFO *) * ui32NumMemSyncInfos);
		for(j = 0, i = ui32NumMemSyncInfos; j < psSwapChain->ui32LastNumSyncInfos; j++)
		{
			if(psSwapChain->ppsLastSyncInfos[j])
			{
				ppsCompiledSyncInfos[i] = psSwapChain->ppsLastSyncInfos[j];
				i++;
			}
		}
	}
	else
#endif /* !defined(SUPPORT_DC_CMDCOMPLETE_WHEN_NO_LONGER_DISPLAYED) */
	{
		ppsCompiledSyncInfos = ppsSyncInfos;
		ui32NumCompiledSyncInfos = ui32NumMemSyncInfos;
	}

	/* insert the command (header) */
	eError = PVRSRVInsertCommandKM (psQueue,
									&psCommand,
									psDCInfo->ui32DeviceID,
									DC_FLIP_COMMAND,
									0,
									IMG_NULL,
									ui32NumCompiledSyncInfos,
									ppsCompiledSyncInfos,
									sizeof(DISPLAYCLASS_FLIP_COMMAND2),
									FreePrivateData,
									psCallbackData);

	if (ppsCompiledSyncInfos != ppsSyncInfos)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
			  sizeof(PVRSRV_KERNEL_SYNC_INFO *) * ui32NumCompiledSyncInfos,
			  (IMG_VOID *)ppsCompiledSyncInfos,
			  IMG_NULL);
	}
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCBuffer2KM: Failed to get space in queue"));
		goto Exit;
	}

	/*  setup the flip command */
	psFlipCmd = (DISPLAYCLASS_FLIP_COMMAND2*)psCommand->pvData;

	/* Ext Device Handle */
	psFlipCmd->hExtDevice = psDCInfo->hExtDevice;

	/* Ext SwapChain Handle */
	psFlipCmd->hExtSwapChain = psSwapChain->hExtSwapChain;

	/* number of vsyncs between successive flips */
	psFlipCmd->ui32SwapInterval = ui32SwapInterval;

	/* Opaque private data, if supplied */
	psFlipCmd->pvPrivData = pvPrivData;
	psFlipCmd->ui32PrivDataLength = ui32PrivDataLength;

	psFlipCmd->ppsMemInfos = (PDC_MEM_INFO *)ppvMemInfos;
	psFlipCmd->ui32NumMemInfos = ui32NumMemSyncInfos;

	/* Even though this is "unused", we have to initialize it,
	 * as the display controller might NULL-test it.
	 */
	psFlipCmd->hUnused = IMG_NULL;

	SysAcquireData(&psSysData);

	/* Because we might be composing just software surfaces, without
	 * any SGX renders since the last frame, we won't necessarily
	 * have cleaned/flushed the CPU caches before the buffers need
	 * to be displayed.
	 *
	 * Doing so now is safe because InsertCommand bumped ROP2 on the
	 * affected buffers (preventing more SW renders starting) but the
	 * display won't start to process the buffers until SubmitCommand.
	 */
	{
		if(psSysData->ePendingCacheOpType == PVRSRV_MISC_INFO_CPUCACHEOP_FLUSH)
		{
			OSFlushCPUCacheKM();
		}
		else if(psSysData->ePendingCacheOpType == PVRSRV_MISC_INFO_CPUCACHEOP_CLEAN)
		{
			OSCleanCPUCacheKM();
		}

		psSysData->ePendingCacheOpType = PVRSRV_MISC_INFO_CPUCACHEOP_NONE;
	}

	/* submit the command */
	eError = PVRSRVSubmitCommandKM (psQueue, psCommand);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCBuffer2KM: Failed to submit command"));
		goto Exit;
	}

	/* The command has been submitted and so psCallbackData will be freed by the callback */
	psCallbackData = IMG_NULL;

	/*
		Schedule an MISR to process it
	*/
	eError = OSScheduleMISR(psSysData);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCBuffer2KM: Failed to schedule MISR"));
		goto Exit;
	}

#if !defined(SUPPORT_DC_CMDCOMPLETE_WHEN_NO_LONGER_DISPLAYED)
	/* Reallocate the syncinfo list if it was too small */
	if (psSwapChain->ui32LastNumSyncInfos < ui32NumMemSyncInfos)
	{
		if (psSwapChain->ppsLastSyncInfos)
		{
			OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_KERNEL_SYNC_INFO *) * psSwapChain->ui32LastNumSyncInfos,
						psSwapChain->ppsLastSyncInfos, IMG_NULL);
		}

		if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
					  sizeof(PVRSRV_KERNEL_SYNC_INFO *) * ui32NumMemSyncInfos,
					  (IMG_VOID **)&psSwapChain->ppsLastSyncInfos, IMG_NULL,
					  "Last syncinfos") != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCBuffer2KM: Failed to allocate space for meminfo list"));
			goto Exit;
		}
	}

	psSwapChain->ui32LastNumSyncInfos = ui32NumMemSyncInfos;

	for(i = 0; i < ui32NumMemSyncInfos; i++)
	{
		psSwapChain->ppsLastSyncInfos[i] = ppsSyncInfos[i];
	}
#endif /* !defined(SUPPORT_DC_CMDCOMPLETE_WHEN_NO_LONGER_DISPLAYED) */

Exit:
	if (psCallbackData)
	{
		if(psCallbackData->ppvMemInfos)
		{
			OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
					  sizeof(IMG_VOID *) * psCallbackData->ui32NumMemInfos,
					  psCallbackData->ppvMemInfos, IMG_NULL);
		}
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(CALLBACK_DATA), psCallbackData, IMG_NULL);
	}
	if(eError == PVRSRV_ERROR_CANNOT_GET_QUEUE_SPACE)
	{
		eError = PVRSRV_ERROR_RETRY;
	}

	return eError;
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVSwapToDCSystemKM(IMG_HANDLE	hDeviceKM,
									IMG_HANDLE	hSwapChainRef)
{
	PVRSRV_ERROR eError;
	PVRSRV_QUEUE_INFO *psQueue;
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	PVRSRV_DC_SWAPCHAIN *psSwapChain;
	PVRSRV_DC_SWAPCHAIN_REF *psSwapChainRef;
	DISPLAYCLASS_FLIP_COMMAND *psFlipCmd;
	IMG_UINT32 ui32NumSrcSyncs = 1;
	PVRSRV_KERNEL_SYNC_INFO *apsSrcSync[2];
	PVRSRV_COMMAND *psCommand;
	IMG_BOOL bAddReferenceToLast = IMG_TRUE;
	IMG_UINT16 ui16SwapCommandID = DC_FLIP_COMMAND;
    SYS_DATA *psSysData;

	if(!hDeviceKM || !hSwapChainRef)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCSystemKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);
	psSwapChainRef = (PVRSRV_DC_SWAPCHAIN_REF*)hSwapChainRef;
	psSwapChain = psSwapChainRef->psSwapChain;

	/*
		If more than 1 reference to the swapchain exists then
		ignore any request to swap to the system buffer
	*/
	if (psSwapChain->ui32RefCount > 1)
	{
		return PVRSRV_OK;
	}

	/* get the queue from the buffer structure */
	psQueue = psSwapChain->psQueue;

#if defined(SUPPORT_CUSTOM_SWAP_OPERATIONS)

	if(psDCInfo->psFuncTable->pfnQuerySwapCommandID != IMG_NULL)
	{
		psDCInfo->psFuncTable->pfnQuerySwapCommandID(psDCInfo->hExtDevice,
													 psSwapChain->hExtSwapChain,
													 psDCInfo->sSystemBuffer.sDeviceClassBuffer.hExtBuffer, 
													 0, 
													 &ui16SwapCommandID,
													 &bAddReferenceToLast);
		
	}

#endif

	/* specify the syncs */
	apsSrcSync[0] = psDCInfo->sSystemBuffer.sDeviceClassBuffer.psKernelSyncInfo;
	if(bAddReferenceToLast && psSwapChain->psLastFlipBuffer)
	{
		/* Make sure we don't make a double dependency on the same server */
		if (apsSrcSync[0] != psSwapChain->psLastFlipBuffer->sDeviceClassBuffer.psKernelSyncInfo)
		{
			apsSrcSync[1] = psSwapChain->psLastFlipBuffer->sDeviceClassBuffer.psKernelSyncInfo;
			ui32NumSrcSyncs++;
		}
	}

	/* insert the command (header) */
	eError = PVRSRVInsertCommandKM (psQueue,
									&psCommand,
									psDCInfo->ui32DeviceID,
									ui16SwapCommandID,
									0,
									IMG_NULL,
									ui32NumSrcSyncs,
									apsSrcSync,
									sizeof(DISPLAYCLASS_FLIP_COMMAND),
									IMG_NULL,
									IMG_NULL);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCSystemKM: Failed to get space in queue"));
		goto Exit;
	}

	/*  setup the flip command */
	psFlipCmd = (DISPLAYCLASS_FLIP_COMMAND*)psCommand->pvData;

	/* Ext Device Handle */
	psFlipCmd->hExtDevice = psDCInfo->hExtDevice;

	/* Ext SwapChain Handle */
	psFlipCmd->hExtSwapChain = psSwapChain->hExtSwapChain;

	/* Ext Buffer Handle (Buffer to Flip to) */
	psFlipCmd->hExtBuffer = psDCInfo->sSystemBuffer.sDeviceClassBuffer.hExtBuffer;

	/* private tag */
	psFlipCmd->hPrivateTag = IMG_NULL;

	/* setup the clip rects */
	psFlipCmd->ui32ClipRectCount = 0;

	psFlipCmd->ui32SwapInterval = 1;

	/* submit the command */
	eError = PVRSRVSubmitCommandKM (psQueue, psCommand);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCSystemKM: Failed to submit command"));
		goto Exit;
	}

	/* Schedule an MISR to process it */
	SysAcquireData(&psSysData);
    eError = OSScheduleMISR(psSysData);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCSystemKM: Failed to schedule MISR"));
		goto Exit;
	}

	/* update the last flip buffer */
	psSwapChain->psLastFlipBuffer = &psDCInfo->sSystemBuffer;

	eError = PVRSRV_OK;

Exit:

	if(eError == PVRSRV_ERROR_CANNOT_GET_QUEUE_SPACE)
	{
		eError = PVRSRV_ERROR_RETRY;
	}

	return eError;
}


/*!
******************************************************************************

 @Function	PVRSRVRegisterSystemISRHandler

 @Description

 registers an external ISR to be called of the back of a system ISR

 @Input	   ppfnISRHandler		: ISR pointer

 @Input	   hISRHandlerData		: Callback data

 @Input	   ui32ISRSourceMask	: ISR Mask

 @Input	   ui32DeviceID			: unique device key

 @Return   PVRSRV_ERROR  :

******************************************************************************/
static
PVRSRV_ERROR PVRSRVRegisterSystemISRHandler (PFN_ISR_HANDLER	pfnISRHandler,
											 IMG_VOID			*pvISRHandlerData,
											 IMG_UINT32			ui32ISRSourceMask,
											 IMG_UINT32			ui32DeviceID)
{
	SYS_DATA 			*psSysData;
	PVRSRV_DEVICE_NODE	*psDevNode;

	PVR_UNREFERENCED_PARAMETER(ui32ISRSourceMask);

	SysAcquireData(&psSysData);

	/* Find Dev Node (just using the device id, ignore the class) */
	psDevNode = (PVRSRV_DEVICE_NODE*)
				List_PVRSRV_DEVICE_NODE_Any_va(psSysData->psDeviceNodeList,
												&MatchDeviceKM_AnyVaCb,
												ui32DeviceID,
												IMG_TRUE);

	if (psDevNode == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterSystemISRHandler: Failed to get psDevNode"));
		PVR_DBG_BREAK;
		return PVRSRV_ERROR_NO_DEVICENODE_FOUND;
	}

	/* set up data before enabling the ISR */
	psDevNode->pvISRData = (IMG_VOID*) pvISRHandlerData;

	/* enable the ISR */
	psDevNode->pfnDeviceISR	= pfnISRHandler;

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVSetDCState_ForEachVaCb

 @Description

 If the device node is a display, calls its set state function.

 @Input	psDeviceNode	- the device node
 		va				- variable argument list with:
				ui32State	- the state to be set.

******************************************************************************/
static
IMG_VOID PVRSRVSetDCState_ForEachVaCb(PVRSRV_DEVICE_NODE *psDeviceNode, va_list va)
{
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	IMG_UINT32 ui32State;
	ui32State = va_arg(va, IMG_UINT32);

	if (psDeviceNode->sDevId.eDeviceClass == PVRSRV_DEVICE_CLASS_DISPLAY)
	{
		psDCInfo = (PVRSRV_DISPLAYCLASS_INFO *)psDeviceNode->pvDevice;
		if (psDCInfo->psFuncTable->pfnSetDCState && psDCInfo->hExtDevice)
		{
			psDCInfo->psFuncTable->pfnSetDCState(psDCInfo->hExtDevice, ui32State);
		}
	}
}


/*!
******************************************************************************

 @Function	PVRSRVSetDCState

 @Description

 Calls the display driver(s) to put them into the specified state.

 @Input	   ui32State: new DC state - one of DC_STATE_*

******************************************************************************/
IMG_VOID IMG_CALLCONV PVRSRVSetDCState(IMG_UINT32 ui32State)
{
/*	PVRSRV_DISPLAYCLASS_INFO	*psDCInfo;
	PVRSRV_DEVICE_NODE			*psDeviceNode; */
	SYS_DATA					*psSysData;

	SysAcquireData(&psSysData);

	List_PVRSRV_DEVICE_NODE_ForEach_va(psSysData->psDeviceNodeList,
										&PVRSRVSetDCState_ForEachVaCb,
										ui32State);
}

static PVRSRV_ERROR
PVRSRVDCMemInfoGetCpuVAddr(PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo,
						   IMG_CPU_VIRTADDR *pVAddr)
{
	*pVAddr = psKernelMemInfo->pvLinAddrKM;
	return PVRSRV_OK;
}

static PVRSRV_ERROR
PVRSRVDCMemInfoGetCpuPAddr(PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo,
						   IMG_SIZE_T uByteOffset, IMG_CPU_PHYADDR *pPAddr)
{
	*pPAddr = OSMemHandleToCpuPAddr(psKernelMemInfo->sMemBlk.hOSMemHandle, uByteOffset);
	return PVRSRV_OK;
}

static PVRSRV_ERROR
PVRSRVDCMemInfoGetByteSize(PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo,
						   IMG_SIZE_T *uByteSize)
{
	*uByteSize = psKernelMemInfo->uAllocSize;
	return PVRSRV_OK;
}

static IMG_BOOL
PVRSRVDCMemInfoIsPhysContig(PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo)
{
	return OSMemHandleIsPhysContig(psKernelMemInfo->sMemBlk.hOSMemHandle);
}

/*!
******************************************************************************

 @Function	PVRGetDisplayClassJTable

 @Description

 Sets up function table for 3rd party Display Class Device to call through

 @Input	   psJTable : pointer to function pointer table memory

 @Return   PVRSRV_ERROR :

******************************************************************************/
IMG_EXPORT
IMG_BOOL PVRGetDisplayClassJTable(PVRSRV_DC_DISP2SRV_KMJTABLE *psJTable)
{
	psJTable->ui32TableSize = sizeof(PVRSRV_DC_DISP2SRV_KMJTABLE);
	psJTable->pfnPVRSRVRegisterDCDevice = &PVRSRVRegisterDCDeviceKM;
	psJTable->pfnPVRSRVRemoveDCDevice = &PVRSRVRemoveDCDeviceKM;
	psJTable->pfnPVRSRVOEMFunction = &SysOEMFunction;
	psJTable->pfnPVRSRVRegisterCmdProcList = &PVRSRVRegisterCmdProcListKM;
	psJTable->pfnPVRSRVRemoveCmdProcList = &PVRSRVRemoveCmdProcListKM;
#if defined(SUPPORT_MISR_IN_THREAD)
        psJTable->pfnPVRSRVCmdComplete = &OSVSyncMISR;
#else
        psJTable->pfnPVRSRVCmdComplete = &PVRSRVCommandCompleteKM;
#endif
	psJTable->pfnPVRSRVRegisterSystemISRHandler = &PVRSRVRegisterSystemISRHandler;
	psJTable->pfnPVRSRVRegisterPowerDevice = &PVRSRVRegisterPowerDevice;
#if defined(SUPPORT_CUSTOM_SWAP_OPERATIONS)
	psJTable->pfnPVRSRVFreeCmdCompletePacket = &PVRSRVFreeCommandCompletePacketKM;
#endif
	psJTable->pfnPVRSRVDCMemInfoGetCpuVAddr = &PVRSRVDCMemInfoGetCpuVAddr;
	psJTable->pfnPVRSRVDCMemInfoGetCpuPAddr = &PVRSRVDCMemInfoGetCpuPAddr;
	psJTable->pfnPVRSRVDCMemInfoGetByteSize = &PVRSRVDCMemInfoGetByteSize;
	psJTable->pfnPVRSRVDCMemInfoIsPhysContig = &PVRSRVDCMemInfoIsPhysContig;
	return IMG_TRUE;
}



/******************************************************************************

 @Function	PVRSRVCloseBCDeviceKM

 @Description

 Closes a connection to the Buffer Class device

 @Input	   hDeviceKM		: device handle

 @Return   PVRSRV_ERROR  :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVCloseBCDeviceKM (IMG_HANDLE	hDeviceKM)
{
	PVRSRV_ERROR eError;
	PVRSRV_BUFFERCLASS_PERCONTEXT_INFO *psBCPerContextInfo;

	psBCPerContextInfo = (PVRSRV_BUFFERCLASS_PERCONTEXT_INFO *)hDeviceKM;

	/* Remove the item from the resman list and trigger the callback. */
	eError = ResManFreeResByPtr(psBCPerContextInfo->hResItem, CLEANUP_WITH_POLL);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVCloseBCDeviceKM: ResManFreeResByPtr failed %d", eError));
		PVR_DPF((PVR_DBG_ERROR, "ResManFreeResByPtr: hResItem 0x%x", (unsigned int)psBCPerContextInfo->hResItem));
	}
	return eError;
}


static PVRSRV_ERROR CloseBCDeviceCallBack(IMG_PVOID  pvParam,
										  IMG_UINT32 ui32Param,
										  IMG_BOOL   bDummy)
{
	PVRSRV_BUFFERCLASS_PERCONTEXT_INFO *psBCPerContextInfo;
	PVRSRV_BUFFERCLASS_INFO *psBCInfo;
	IMG_UINT32 i;

	PVR_UNREFERENCED_PARAMETER(ui32Param);
	PVR_UNREFERENCED_PARAMETER(bDummy);

	psBCPerContextInfo = (PVRSRV_BUFFERCLASS_PERCONTEXT_INFO *)pvParam;

	psBCInfo = psBCPerContextInfo->psBCInfo;

	for (i = 0; i < psBCInfo->ui32BufferCount; i++)
	{
		if (psBCInfo->psBuffer[i].sDeviceClassBuffer.ui32MemMapRefCount != 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "CloseBCDeviceCallBack: buffer %d (0x%p) still mapped (ui32MemMapRefCount = %d)",
					i,
					&psBCInfo->psBuffer[i].sDeviceClassBuffer,
					psBCInfo->psBuffer[i].sDeviceClassBuffer.ui32MemMapRefCount));
			return PVRSRV_ERROR_STILL_MAPPED;
		}
	}

	psBCInfo->ui32RefCount--;
	if(psBCInfo->ui32RefCount == 0)
	{
		/* close the external device */
		psBCInfo->psFuncTable->pfnCloseBCDevice(psBCInfo->ui32DeviceID, psBCInfo->hExtDevice);

		/* free syncinfos */
		for(i=0; i<psBCInfo->ui32BufferCount; i++)
		{
			if(psBCInfo->psBuffer[i].sDeviceClassBuffer.psKernelSyncInfo)
			{
				PVRSRVKernelSyncInfoDecRef(psBCInfo->psBuffer[i].sDeviceClassBuffer.psKernelSyncInfo, IMG_NULL);
			}
		}

		/* free buffers */
		if(psBCInfo->psBuffer)
		{
			OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_BC_BUFFER) * psBCInfo->ui32BufferCount, psBCInfo->psBuffer, IMG_NULL);
			psBCInfo->psBuffer = IMG_NULL;
		}
	}

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_BUFFERCLASS_PERCONTEXT_INFO), psBCPerContextInfo, IMG_NULL);
	/*not nulling pointer, copy on stack*/

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	PVRSRVOpenBCDeviceKM

 @Description

 Opens a connection to the Buffer Class device, associating the connection
 with a Device Memory Context for a services managed device

 @Input	   psPerProc		: Per-process data
 @Input	   ui32DeviceID		: unique device index
 @Input	   hDevCookie		: devcookie used to derive the Device Memory
 								Context into BC surfaces will be mapped into
 @Outut	   phDeviceKM		: handle to the DC device

 @Return   PVRSRV_ERROR  :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVOpenBCDeviceKM (PVRSRV_PER_PROCESS_DATA	*psPerProc,
								   IMG_UINT32				ui32DeviceID,
								   IMG_HANDLE				hDevCookie,
								   IMG_HANDLE				*phDeviceKM)
{
	PVRSRV_BUFFERCLASS_INFO	*psBCInfo;
	PVRSRV_BUFFERCLASS_PERCONTEXT_INFO	*psBCPerContextInfo;
	PVRSRV_DEVICE_NODE		*psDeviceNode;
	SYS_DATA 				*psSysData;
	IMG_UINT32 				i;
	PVRSRV_ERROR			eError;

	if(!phDeviceKM || !hDevCookie)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenBCDeviceKM: Invalid params"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	SysAcquireData(&psSysData);

	/* find the matching devicenode */
	psDeviceNode = (PVRSRV_DEVICE_NODE*)
			List_PVRSRV_DEVICE_NODE_Any_va(psSysData->psDeviceNodeList,
										   &MatchDeviceKM_AnyVaCb,
										   ui32DeviceID,
										   IMG_FALSE,
										   PVRSRV_DEVICE_CLASS_BUFFER);
	if (!psDeviceNode)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenBCDeviceKM: No devnode matching index %d", ui32DeviceID));
		return PVRSRV_ERROR_NO_DEVICENODE_FOUND;
	}
	psBCInfo = (PVRSRV_BUFFERCLASS_INFO*)psDeviceNode->pvDevice;

/*
FoundDevice:
*/
	/*
		Allocate the per-context BC Info before calling the external device,
		to make error handling easier.
	*/
	if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
				  sizeof(*psBCPerContextInfo),
				  (IMG_VOID **)&psBCPerContextInfo, IMG_NULL,
				  "Buffer Class per Context Info") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenBCDeviceKM: Failed psBCPerContextInfo alloc"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	OSMemSet(psBCPerContextInfo, 0, sizeof(*psBCPerContextInfo));

	if(psBCInfo->ui32RefCount++ == 0)
	{
		BUFFER_INFO sBufferInfo;

		psDeviceNode = (PVRSRV_DEVICE_NODE *)hDevCookie;

		/* store the device kernel context to map into */
		psBCInfo->hDevMemContext = (IMG_HANDLE)psDeviceNode->sDevMemoryInfo.pBMKernelContext;

		/* open the external device */
		eError = psBCInfo->psFuncTable->pfnOpenBCDevice(ui32DeviceID, &psBCInfo->hExtDevice);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenBCDeviceKM: Failed to open external BC device"));
			return eError;
		}

		/* get information about the buffers */
		eError = psBCInfo->psFuncTable->pfnGetBCInfo(psBCInfo->hExtDevice, &sBufferInfo);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenBCDeviceKM : Failed to get BC Info"));
			return eError;
		}

		/* interpret and store info */
		psBCInfo->ui32BufferCount = sBufferInfo.ui32BufferCount;

		/*  allocate BC buffers */
		eError = OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
							  sizeof(PVRSRV_BC_BUFFER) * sBufferInfo.ui32BufferCount,
							  (IMG_VOID **)&psBCInfo->psBuffer,
						 	  IMG_NULL,
							  "Array of Buffer Class Buffer");
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenBCDeviceKM: Failed to allocate BC buffers"));
			return eError;
		}
		OSMemSet (psBCInfo->psBuffer,
					0,
					sizeof(PVRSRV_BC_BUFFER) * sBufferInfo.ui32BufferCount);

		for(i=0; i<psBCInfo->ui32BufferCount; i++)
		{
			/* create a syncinfo for the device's system surface */
			eError = PVRSRVAllocSyncInfoKM(IMG_NULL,
										psBCInfo->hDevMemContext,
										&psBCInfo->psBuffer[i].sDeviceClassBuffer.psKernelSyncInfo);
			if(eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenBCDeviceKM: Failed sync info alloc"));
				goto ErrorExit;
			}
			
			/*
				get the buffers from the buffer class
				drivers by index, passing-in the syncdata objects
			*/
			eError = psBCInfo->psFuncTable->pfnGetBCBuffer(psBCInfo->hExtDevice,
															i,
															psBCInfo->psBuffer[i].sDeviceClassBuffer.psKernelSyncInfo->psSyncData,
															&psBCInfo->psBuffer[i].sDeviceClassBuffer.hExtBuffer);
			if(eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenBCDeviceKM: Failed to get BC buffers"));
				goto ErrorExit;
			}

			/* setup common device class info */
			psBCInfo->psBuffer[i].sDeviceClassBuffer.pfnGetBufferAddr = psBCInfo->psFuncTable->pfnGetBufferAddr;
			psBCInfo->psBuffer[i].sDeviceClassBuffer.hDevMemContext = psBCInfo->hDevMemContext;
			psBCInfo->psBuffer[i].sDeviceClassBuffer.hExtDevice = psBCInfo->hExtDevice;
			psBCInfo->psBuffer[i].sDeviceClassBuffer.ui32MemMapRefCount = 0;
		}
	}

	psBCPerContextInfo->psBCInfo = psBCInfo;
	psBCPerContextInfo->hResItem = ResManRegisterRes(psPerProc->hResManContext,
													 RESMAN_TYPE_BUFFERCLASS_DEVICE,
													 psBCPerContextInfo,
													 0,
													 &CloseBCDeviceCallBack);

	/* return a reference to the BCPerContextInfo */
	*phDeviceKM = (IMG_HANDLE)psBCPerContextInfo;

	return PVRSRV_OK;

ErrorExit:

	/* free syncinfos */
	for(i=0; i<psBCInfo->ui32BufferCount; i++)
	{
		if(psBCInfo->psBuffer[i].sDeviceClassBuffer.psKernelSyncInfo)
		{
			PVRSRVKernelSyncInfoDecRef(psBCInfo->psBuffer[i].sDeviceClassBuffer.psKernelSyncInfo, IMG_NULL);
		}
	}

	/* free buffers */
	if(psBCInfo->psBuffer)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_BC_BUFFER), psBCInfo->psBuffer, IMG_NULL);
		psBCInfo->psBuffer = IMG_NULL;
	}

	return eError;
}




/******************************************************************************

 @Function	PVRSRVGetBCInfoKM

 @Description

 Gets Buffer Class device Info

 @Input		hDeviceKM		: device handle
 @Output	psBufferInfo

 @Return   PVRSRV_ERROR  :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVGetBCInfoKM (IMG_HANDLE hDeviceKM,
								BUFFER_INFO *psBufferInfo)
{
	PVRSRV_BUFFERCLASS_INFO *psBCInfo;
	PVRSRV_ERROR 			eError;

	if(!hDeviceKM || !psBufferInfo)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVGetBCInfoKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psBCInfo = BCDeviceHandleToBCInfo(hDeviceKM);

	eError = psBCInfo->psFuncTable->pfnGetBCInfo(psBCInfo->hExtDevice, psBufferInfo);

	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVGetBCInfoKM : Failed to get BC Info"));
		return eError;
	}

	return PVRSRV_OK;
}


/******************************************************************************

 @Function	PVRSRVGetBCBufferKM

 @Description

 Gets Buffer Class Buffer Handle

 @Input		hDeviceKM		: device handle
 @Output	psBufferInfo

 @Return   PVRSRV_ERROR  :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVGetBCBufferKM (IMG_HANDLE hDeviceKM,
								  IMG_UINT32 ui32BufferIndex,
								  IMG_HANDLE *phBuffer)
{
	PVRSRV_BUFFERCLASS_INFO *psBCInfo;

	if(!hDeviceKM || !phBuffer)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVGetBCBufferKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psBCInfo = BCDeviceHandleToBCInfo(hDeviceKM);

	if(ui32BufferIndex < psBCInfo->ui32BufferCount)
	{
		*phBuffer = (IMG_HANDLE)&psBCInfo->psBuffer[ui32BufferIndex];
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVGetBCBufferKM: Buffer index %d out of range (%d)", ui32BufferIndex,psBCInfo->ui32BufferCount));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	PVRGetBufferClassJTable

 @Description

 Sets up function table for 3rd party Buffer Class Device to call through

 @Input	   psJTable : pointer to function pointer table memory

 @Return   PVRSRV_ERROR :

******************************************************************************/
IMG_EXPORT
IMG_BOOL PVRGetBufferClassJTable(PVRSRV_BC_BUFFER2SRV_KMJTABLE *psJTable)
{
	psJTable->ui32TableSize = sizeof(PVRSRV_BC_BUFFER2SRV_KMJTABLE);

	psJTable->pfnPVRSRVRegisterBCDevice = &PVRSRVRegisterBCDeviceKM;
	psJTable->pfnPVRSRVScheduleDevices = &PVRSRVScheduleDevicesKM;
	psJTable->pfnPVRSRVRemoveBCDevice = &PVRSRVRemoveBCDeviceKM;

	return IMG_TRUE;
}

/******************************************************************************
 End of file (deviceclass.c)
******************************************************************************/
