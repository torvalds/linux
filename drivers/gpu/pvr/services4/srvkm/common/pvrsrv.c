/*************************************************************************/ /*!
@Title          core services functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Main APIs for core services functions
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
#include "pvr_bridge_km.h"
#include "handle.h"
#include "perproc.h"
#include "pdump_km.h"
#include "deviceid.h"
#include "ra.h"
#if defined(TTRACE)
#include "ttrace.h"
#endif
#include "perfkm.h"
#include "devicemem.h"

#include "pvrversion.h"

#include "lists.h"

IMG_UINT32	g_ui32InitFlags;

/* mark which parts of Services were initialised */
#define		INIT_DATA_ENABLE_PDUMPINIT	0x1U
#define		INIT_DATA_ENABLE_TTARCE		0x2U
#define		INIT_DATA_ENABLE_DEVMEM		0x4U

/*!
******************************************************************************

 @Function	AllocateDeviceID

 @Description

 allocates a device id from the pool of valid ids

 @input psSysData :	system data

 @input pui32DevID : device id to return

 @Return device id

******************************************************************************/
PVRSRV_ERROR AllocateDeviceID(SYS_DATA *psSysData, IMG_UINT32 *pui32DevID)
{
	SYS_DEVICE_ID* psDeviceWalker;
	SYS_DEVICE_ID* psDeviceEnd;

	psDeviceWalker = &psSysData->sDeviceID[0];
	psDeviceEnd = psDeviceWalker + psSysData->ui32NumDevices;

	/* find a free ID */
	while (psDeviceWalker < psDeviceEnd)
	{
		if (!psDeviceWalker->bInUse)
		{
			psDeviceWalker->bInUse = IMG_TRUE;
			*pui32DevID = psDeviceWalker->uiID;
			return PVRSRV_OK;
		}
		psDeviceWalker++;
	}

	PVR_DPF((PVR_DBG_ERROR,"AllocateDeviceID: No free and valid device IDs available!"));

	/* Should never get here: sDeviceID[] may have been setup too small */
	PVR_ASSERT(psDeviceWalker < psDeviceEnd);

	return PVRSRV_ERROR_NO_FREE_DEVICEIDS_AVALIABLE;
}


/*!
******************************************************************************

 @Function	FreeDeviceID

 @Description

 frees a device id from the pool of valid ids

 @input psSysData :	system data

 @input ui32DevID : device id to free

 @Return device id

******************************************************************************/
PVRSRV_ERROR FreeDeviceID(SYS_DATA *psSysData, IMG_UINT32 ui32DevID)
{
	SYS_DEVICE_ID* psDeviceWalker;
	SYS_DEVICE_ID* psDeviceEnd;

	psDeviceWalker = &psSysData->sDeviceID[0];
	psDeviceEnd = psDeviceWalker + psSysData->ui32NumDevices;

	/* find the ID to free */
	while (psDeviceWalker < psDeviceEnd)
	{
		/* if matching id and in use, free */
		if	(
				(psDeviceWalker->uiID == ui32DevID) &&
				(psDeviceWalker->bInUse)
			)
		{
			psDeviceWalker->bInUse = IMG_FALSE;
			return PVRSRV_OK;
		}
		psDeviceWalker++;
	}

	PVR_DPF((PVR_DBG_ERROR,"FreeDeviceID: no matching dev ID that is in use!"));

	/* should never get here */
	PVR_ASSERT(psDeviceWalker < psDeviceEnd);

	return PVRSRV_ERROR_INVALID_DEVICEID;
}


/*!
******************************************************************************

 @Function	ReadHWReg

 @Description

 register access function

 @input pvLinRegBaseAddr :	lin addr of register block base

 @input ui32Offset :	 byte offset from register base

 @Return   register value

******************************************************************************/
#ifndef ReadHWReg
IMG_EXPORT
IMG_UINT32 ReadHWReg(IMG_PVOID pvLinRegBaseAddr, IMG_UINT32 ui32Offset)
{
	return *(volatile IMG_UINT32*)((IMG_UINTPTR_T)pvLinRegBaseAddr+ui32Offset);
}
#endif


/*!
******************************************************************************

 @Function	WriteHWReg

 @Description

 register access function

 @input pvLinRegBaseAddr :	lin addr of register block base

 @input ui32Offset :	 byte offset from register base

 @input ui32Value :	 value to write to register

 @Return   register value : original reg. value

******************************************************************************/
#ifndef WriteHWReg
IMG_EXPORT
IMG_VOID WriteHWReg(IMG_PVOID pvLinRegBaseAddr, IMG_UINT32 ui32Offset, IMG_UINT32 ui32Value)
{
	PVR_DPF((PVR_DBG_MESSAGE,"WriteHWReg Base:%p, Offset: %x, Value %x",
			pvLinRegBaseAddr,ui32Offset,ui32Value));

	*(IMG_UINT32*)((IMG_UINTPTR_T)pvLinRegBaseAddr+ui32Offset) = ui32Value;
}
#endif


/*!
******************************************************************************

 @Function	WriteHWRegs

 @Description

 register access function

 @input pvLinRegBaseAddr :	lin addr of register block base

 @input ui32Count :	 register count

 @input psHWRegs :	 address/value register list

 @Return   none

******************************************************************************/
#ifndef WriteHWRegs
IMG_EXPORT
IMG_VOID WriteHWRegs(IMG_PVOID pvLinRegBaseAddr, IMG_UINT32 ui32Count, PVRSRV_HWREG *psHWRegs)
{
	while (ui32Count)
	{
		WriteHWReg (pvLinRegBaseAddr, psHWRegs->ui32RegAddr, psHWRegs->ui32RegVal);
		psHWRegs++;
		ui32Count--;
	}
}
#endif

/*!
******************************************************************************
 @Function	PVRSRVEnumerateDCKM_ForEachVaCb

 @Description

 Enumerates the device node (if is of the same class as given).

 @Input psDeviceNode	- The device node to be enumerated
 		va				- variable arguments list, with:
							pui32DevCount	- The device count pointer (to be increased)
							ppui32DevID		- The pointer to the device IDs pointer (to be updated and increased)
******************************************************************************/
static IMG_VOID PVRSRVEnumerateDevicesKM_ForEachVaCb(PVRSRV_DEVICE_NODE *psDeviceNode, va_list va)
{
	IMG_UINT *pui32DevCount;
	PVRSRV_DEVICE_IDENTIFIER **ppsDevIdList;

	pui32DevCount = va_arg(va, IMG_UINT*);
	ppsDevIdList = va_arg(va, PVRSRV_DEVICE_IDENTIFIER**);

	if (psDeviceNode->sDevId.eDeviceType != PVRSRV_DEVICE_TYPE_EXT)
	{
		*(*ppsDevIdList) = psDeviceNode->sDevId;
		(*ppsDevIdList)++;
		(*pui32DevCount)++;
	}
}



/*!
******************************************************************************

 @Function PVRSRVEnumerateDevicesKM

 @Description
 This function will enumerate all the devices supported by the
 PowerVR services within the target system.
 The function returns a list of the device ID strcutres stored either in
 the services or constructed in the user mode glue component in certain
 environments. The number of devices in the list is also returned.

 In a binary layered component which does not support dynamic runtime selection,
 the glue code should compile to return the supported devices statically,
 e.g. multiple instances of the same device if multiple devices are supported,
 or the target combination of MBX and display device.

 In the case of an environment (for instance) where one MBX1 may connect to two
 display devices this code would enumerate all three devices and even
 non-dynamic MBX1 selection code should retain the facility to parse the list
 to find the index of the MBX device

 @output pui32NumDevices :	On success, contains the number of devices present
 							in the system

 @output psDevIdList	 :	Pointer to called supplied buffer to receive the
 							list of PVRSRV_DEVICE_IDENTIFIER

 @return PVRSRV_ERROR  :	PVRSRV_NO_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVEnumerateDevicesKM(IMG_UINT32 *pui32NumDevices,
											 	   PVRSRV_DEVICE_IDENTIFIER *psDevIdList)
{
	SYS_DATA			*psSysData;
/*	PVRSRV_DEVICE_NODE	*psDeviceNode; */
	IMG_UINT32 			i;

	if (!pui32NumDevices || !psDevIdList)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVEnumerateDevicesKM: Invalid params"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	SysAcquireData(&psSysData);

	/*
		setup input buffer to be `empty'
	*/
	for (i=0; i<PVRSRV_MAX_DEVICES; i++)
	{
		psDevIdList[i].eDeviceType = PVRSRV_DEVICE_TYPE_UNKNOWN;
	}

	/* and zero device count */
	*pui32NumDevices = 0;

	/*
		Search through the device list for services managed devices
		return id info for each device and the number of devices
		available
	*/
	List_PVRSRV_DEVICE_NODE_ForEach_va(psSysData->psDeviceNodeList,
									   &PVRSRVEnumerateDevicesKM_ForEachVaCb,
									   pui32NumDevices,
									   &psDevIdList);


	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	PVRSRVInit

 @Description	Initialise services

 @Input	   psSysData	: sysdata structure

 @Return   PVRSRV_ERROR	:

******************************************************************************/
PVRSRV_ERROR IMG_CALLCONV PVRSRVInit(PSYS_DATA psSysData)
{
	PVRSRV_ERROR	eError;

	/* Initialise Resource Manager */
	eError = ResManInit();
	if (eError != PVRSRV_OK)
	{
		goto Error;
	}

	eError = PVRSRVPerProcessDataInit();
	if(eError != PVRSRV_OK)
	{
		goto Error;
	}

	/* Initialise handles */
	eError = PVRSRVHandleInit();
	if(eError != PVRSRV_OK)
	{
		goto Error;
	}

	/* Initialise Power Manager Lock */
	eError = OSCreateResource(&psSysData->sPowerStateChangeResource);
	if (eError != PVRSRV_OK)
	{
		goto Error;
	}

	/* Initialise system power state */
	psSysData->eCurrentPowerState = PVRSRV_SYS_POWER_STATE_D0;
	psSysData->eFailedPowerState = PVRSRV_SYS_POWER_STATE_Unspecified;

	/* Create an event object */
	if(OSAllocMem( PVRSRV_PAGEABLE_SELECT,
					 sizeof(PVRSRV_EVENTOBJECT) ,
					 (IMG_VOID **)&psSysData->psGlobalEventObject, 0,
					 "Event Object") != PVRSRV_OK)
	{

		goto Error;
	}

	if(OSEventObjectCreateKM("PVRSRV_GLOBAL_EVENTOBJECT", psSysData->psGlobalEventObject) != PVRSRV_OK)
	{
		goto Error;
	}

	/* Store OS high res timer fallbacks, the system is free to overide these */
	psSysData->pfnHighResTimerCreate = OSFuncHighResTimerCreate;
	psSysData->pfnHighResTimerGetus = OSFuncHighResTimerGetus;
	psSysData->pfnHighResTimerDestroy = OSFuncHighResTimerDestroy;

#if defined(TTRACE)
	eError = PVRSRVTimeTraceInit();
	if (eError != PVRSRV_OK)
		goto Error;
	g_ui32InitFlags |= INIT_DATA_ENABLE_TTARCE;
#endif

	/* Initialise pdump */
	PDUMPINIT();
	g_ui32InitFlags |= INIT_DATA_ENABLE_PDUMPINIT;

#if defined(SUPPORT_ION)
	eError = PVRSRVInitDeviceMem();
	if (eError != PVRSRV_OK)
		goto Error;
	g_ui32InitFlags |= INIT_DATA_ENABLE_DEVMEM;
#endif

	PERFINIT();
	return eError;

Error:
	PVRSRVDeInit(psSysData);
	return eError;
}



/*!
******************************************************************************

 @Function	PVRSRVDeInit

 @Description	De-Initialise services

 @Input	   psSysData	: sysdata structure

 @Return   PVRSRV_ERROR	:

******************************************************************************/
IMG_VOID IMG_CALLCONV PVRSRVDeInit(PSYS_DATA psSysData)
{
	PVRSRV_ERROR	eError;

	PVR_UNREFERENCED_PARAMETER(psSysData);

	if (psSysData == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDeInit: PVRSRVHandleDeInit failed - invalid param"));
		return;
	}

	PERFDEINIT();

#if defined(SUPPORT_ION)
	if ((g_ui32InitFlags & INIT_DATA_ENABLE_DEVMEM) > 0)
	{
		PVRSRVDeInitDeviceMem();
	}
#endif

#if defined(TTRACE)
	/* deinitialise ttrace */
	if ((g_ui32InitFlags & INIT_DATA_ENABLE_TTARCE) > 0)
	{
		PVRSRVTimeTraceDeinit();
	}
#endif
	/* deinitialise pdump */
	if( (g_ui32InitFlags & INIT_DATA_ENABLE_PDUMPINIT) > 0)
	{
		PDUMPDEINIT();
	}
	
	/* destroy event object */
	if(psSysData->psGlobalEventObject)
	{
		OSEventObjectDestroyKM(psSysData->psGlobalEventObject);
		OSFreeMem( PVRSRV_PAGEABLE_SELECT,
						 sizeof(PVRSRV_EVENTOBJECT),
						 psSysData->psGlobalEventObject,
						 0);
		psSysData->psGlobalEventObject = IMG_NULL;
	}

	eError = PVRSRVHandleDeInit();
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDeInit: PVRSRVHandleDeInit failed"));
	}

	eError = PVRSRVPerProcessDataDeInit();
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDeInit: PVRSRVPerProcessDataDeInit failed"));
	}

	ResManDeInit();
}


/*!
******************************************************************************

 @Function	PVRSRVRegisterDevice

 @Description

 registers a device with the system

 @Input	   psSysData			: sysdata structure

 @Input	   pfnRegisterDevice	: device registration function

 @Input	   ui32SOCInterruptBit	: SoC interrupt bit for this device

 @Output	pui32DeviceIndex		: unique device key (for case of multiple identical devices)

 @Return   PVRSRV_ERROR  :

******************************************************************************/
PVRSRV_ERROR IMG_CALLCONV PVRSRVRegisterDevice(PSYS_DATA psSysData,
											  PVRSRV_ERROR (*pfnRegisterDevice)(PVRSRV_DEVICE_NODE*),
											  IMG_UINT32 ui32SOCInterruptBit,
			 								  IMG_UINT32 *pui32DeviceIndex)
{
	PVRSRV_ERROR		eError;
	PVRSRV_DEVICE_NODE	*psDeviceNode;

	/* Allocate device node */
	if(OSAllocMem( PVRSRV_OS_NON_PAGEABLE_HEAP,
					 sizeof(PVRSRV_DEVICE_NODE),
					 (IMG_VOID **)&psDeviceNode, IMG_NULL,
					 "Device Node") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterDevice : Failed to alloc memory for psDeviceNode"));
		return (PVRSRV_ERROR_OUT_OF_MEMORY);
	}
	OSMemSet (psDeviceNode, 0, sizeof(PVRSRV_DEVICE_NODE));

	eError = pfnRegisterDevice(psDeviceNode);
	if (eError != PVRSRV_OK)
	{
		OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
					sizeof(PVRSRV_DEVICE_NODE), psDeviceNode, IMG_NULL);
		/*not nulling pointer, out of scope*/
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterDevice : Failed to register device"));
		return (PVRSRV_ERROR_DEVICE_REGISTER_FAILED);
	}

	/*
		make the refcount 1 and test on this to initialise device
		at acquiredevinfo. On release if refcount is 1, deinitialise
		and when refcount is 0 (sysdata de-alloc) deallocate the device
		structures
	*/
	psDeviceNode->ui32RefCount = 1;
	psDeviceNode->psSysData = psSysData;
	psDeviceNode->ui32SOCInterruptBit = ui32SOCInterruptBit;

	/* all devices need a unique identifier */
	AllocateDeviceID(psSysData, &psDeviceNode->sDevId.ui32DeviceIndex);

	/* and finally insert the device into the dev-list */
	List_PVRSRV_DEVICE_NODE_Insert(&psSysData->psDeviceNodeList, psDeviceNode);

	/* and copy back index */
	*pui32DeviceIndex = psDeviceNode->sDevId.ui32DeviceIndex;

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	PVRSRVInitialiseDevice

 @Description

 initialises device by index

 @Input	   ui32DevIndex : Index to the required device

 @Return   PVRSRV_ERROR  :

******************************************************************************/
PVRSRV_ERROR IMG_CALLCONV PVRSRVInitialiseDevice (IMG_UINT32 ui32DevIndex)
{
	PVRSRV_DEVICE_NODE	*psDeviceNode;
	SYS_DATA			*psSysData;
	PVRSRV_ERROR		eError;

	PVR_DPF((PVR_DBG_MESSAGE, "PVRSRVInitialiseDevice"));

	SysAcquireData(&psSysData);

	/* Find device in the list */
	psDeviceNode = (PVRSRV_DEVICE_NODE*)
					 List_PVRSRV_DEVICE_NODE_Any_va(psSysData->psDeviceNodeList,
													&MatchDeviceKM_AnyVaCb,
													ui32DevIndex,
													IMG_TRUE);
	if(!psDeviceNode)
	{
		/* Devinfo not in the list */
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVInitialiseDevice: requested device is not present"));
		return PVRSRV_ERROR_INIT_FAILURE;
	}
/*
FoundDevice:
*/

	PVR_ASSERT (psDeviceNode->ui32RefCount > 0);

	/*
		Create the device's resource manager context.
	*/
	eError = PVRSRVResManConnect(IMG_NULL, &psDeviceNode->hResManContext);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVInitialiseDevice: Failed PVRSRVResManConnect call"));
		return eError;
	}

	/* Initialise the device */
	if(psDeviceNode->pfnInitDevice != IMG_NULL)
	{
		eError = psDeviceNode->pfnInitDevice(psDeviceNode);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVInitialiseDevice: Failed InitDevice call"));
			return eError;
		}
	}

	return PVRSRV_OK;
}


static PVRSRV_ERROR PVRSRVFinaliseSystem_SetPowerState_AnyCb(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_ERROR eError;

	eError = PVRSRVPowerLock(KERNEL_ID, IMG_FALSE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVFinaliseSystem: Failed PVRSRVPowerLock call (device index: %d)", psDeviceNode->sDevId.ui32DeviceIndex));
		return eError;
	}

	eError = PVRSRVSetDevicePowerStateKM(psDeviceNode->sDevId.ui32DeviceIndex,
										 PVRSRV_DEV_POWER_STATE_DEFAULT);
	PVRSRVPowerUnlock(KERNEL_ID);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVFinaliseSystem: Failed PVRSRVSetDevicePowerStateKM call (device index: %d)", psDeviceNode->sDevId.ui32DeviceIndex));
	}
	return eError;
}

/*wraps the PVRSRVDevInitCompatCheck call and prints a debugging message if failed*/
static PVRSRV_ERROR PVRSRVFinaliseSystem_CompatCheck_AnyCb(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_ERROR eError;
	eError = PVRSRVDevInitCompatCheck(psDeviceNode);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVFinaliseSystem: Failed PVRSRVDevInitCompatCheck call (device index: %d)", psDeviceNode->sDevId.ui32DeviceIndex));
	}
	return eError;
}


/*!
******************************************************************************

 @Function	PVRSRVFinaliseSystem

 @Description

 Final part of system initialisation.

 @Input	   ui32DevIndex : Index to the required device

 @Return   PVRSRV_ERROR  :

******************************************************************************/
PVRSRV_ERROR IMG_CALLCONV PVRSRVFinaliseSystem(IMG_BOOL bInitSuccessful)
{
/*	PVRSRV_DEVICE_NODE	*psDeviceNode;*/
	SYS_DATA		*psSysData;
	PVRSRV_ERROR		eError;

	PVR_DPF((PVR_DBG_MESSAGE, "PVRSRVFinaliseSystem"));

	SysAcquireData(&psSysData);

	if (bInitSuccessful)
	{
		eError = SysFinalise();
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVFinaliseSystem: SysFinalise failed (%d)", eError));
			return eError;
		}

		/* Place all devices into their default power state. */
		eError = List_PVRSRV_DEVICE_NODE_PVRSRV_ERROR_Any(psSysData->psDeviceNodeList,
														&PVRSRVFinaliseSystem_SetPowerState_AnyCb);
		if (eError != PVRSRV_OK)
		{
			return eError;
		}

		/* Verify microkernel compatibility for devices */
		eError = List_PVRSRV_DEVICE_NODE_PVRSRV_ERROR_Any(psSysData->psDeviceNodeList,
													&PVRSRVFinaliseSystem_CompatCheck_AnyCb);
		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}

	/* Some platforms call this too early in the boot phase. */
	PDUMPENDINITPHASE();

	return PVRSRV_OK;
}


PVRSRV_ERROR PVRSRVDevInitCompatCheck(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	/* Only check devices which specify a compatibility check callback */
	if (psDeviceNode->pfnInitDeviceCompatCheck)
		return psDeviceNode->pfnInitDeviceCompatCheck(psDeviceNode);
	else
		return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVAcquireDeviceDataKM

 @Description

 Matchs a device given a device type and a device index.

 @input	psDeviceNode :The device node to be matched.

 @Input	   va : Variable argument list with:
			eDeviceType : Required device type. If type is unknown use ui32DevIndex
						 to locate device data

 			ui32DevIndex : Index to the required device obtained from the
						PVRSRVEnumerateDevice function

 @Return   PVRSRV_ERROR  :

******************************************************************************/
static IMG_VOID * PVRSRVAcquireDeviceDataKM_Match_AnyVaCb(PVRSRV_DEVICE_NODE *psDeviceNode, va_list va)
{
	PVRSRV_DEVICE_TYPE eDeviceType;
	IMG_UINT32 ui32DevIndex;

	eDeviceType = va_arg(va, PVRSRV_DEVICE_TYPE);
	ui32DevIndex = va_arg(va, IMG_UINT32);

	if ((eDeviceType != PVRSRV_DEVICE_TYPE_UNKNOWN &&
		psDeviceNode->sDevId.eDeviceType == eDeviceType) ||
		(eDeviceType == PVRSRV_DEVICE_TYPE_UNKNOWN &&
		 psDeviceNode->sDevId.ui32DeviceIndex == ui32DevIndex))
	{
		return psDeviceNode;
	}
	else
	{
		return IMG_NULL;
	}
}

/*!
******************************************************************************

 @Function	PVRSRVAcquireDeviceDataKM

 @Description

 Returns device information

 @Input	   ui32DevIndex : Index to the required device obtained from the
						PVRSRVEnumerateDevice function

 @Input	   eDeviceType : Required device type. If type is unknown use ui32DevIndex
						 to locate device data

 @Output  *phDevCookie : Dev Cookie


 @Return   PVRSRV_ERROR  :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVAcquireDeviceDataKM (IMG_UINT32			ui32DevIndex,
													 PVRSRV_DEVICE_TYPE	eDeviceType,
													 IMG_HANDLE			*phDevCookie)
{
	PVRSRV_DEVICE_NODE	*psDeviceNode;
	SYS_DATA			*psSysData;

	PVR_DPF((PVR_DBG_MESSAGE, "PVRSRVAcquireDeviceDataKM"));

	SysAcquireData(&psSysData);

	/* Find device in the list */
	psDeviceNode = List_PVRSRV_DEVICE_NODE_Any_va(psSysData->psDeviceNodeList,
												&PVRSRVAcquireDeviceDataKM_Match_AnyVaCb,
												eDeviceType,
												ui32DevIndex);


	if (!psDeviceNode)
	{
		/* device can't be found in the list so it isn't in the system */
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVAcquireDeviceDataKM: requested device is not present"));
		return PVRSRV_ERROR_INIT_FAILURE;
	}

/*FoundDevice:*/

	PVR_ASSERT (psDeviceNode->ui32RefCount > 0);

	/* return the dev cookie? */
	if (phDevCookie)
	{
		*phDevCookie = (IMG_HANDLE)psDeviceNode;
	}

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	PVRSRVDeinitialiseDevice

 @Description

 This De-inits device

 @Input	   ui32DevIndex : Index to the required device

 @Return   PVRSRV_ERROR  :

******************************************************************************/
PVRSRV_ERROR IMG_CALLCONV PVRSRVDeinitialiseDevice(IMG_UINT32 ui32DevIndex)
{
	PVRSRV_DEVICE_NODE	*psDeviceNode;
	SYS_DATA			*psSysData;
	PVRSRV_ERROR		eError;

	SysAcquireData(&psSysData);

	psDeviceNode = (PVRSRV_DEVICE_NODE*)
					 List_PVRSRV_DEVICE_NODE_Any_va(psSysData->psDeviceNodeList,
													&MatchDeviceKM_AnyVaCb,
													ui32DevIndex,
													IMG_TRUE);

	if (!psDeviceNode)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDeinitialiseDevice: requested device %d is not present", ui32DevIndex));
		return PVRSRV_ERROR_DEVICEID_NOT_FOUND;
	}

	eError = PVRSRVPowerLock(KERNEL_ID, IMG_FALSE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDeinitialiseDevice: Failed PVRSRVPowerLock call"));
		return eError;
	}

	/*
		Power down the device if necessary.
	 */
	eError = PVRSRVSetDevicePowerStateKM(ui32DevIndex,
										 PVRSRV_DEV_POWER_STATE_OFF);
	PVRSRVPowerUnlock(KERNEL_ID);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDeinitialiseDevice: Failed PVRSRVSetDevicePowerStateKM call"));
		return eError;
	}

	/*
		Free the dissociated device memory.
	*/
	eError = ResManFreeResByCriteria(psDeviceNode->hResManContext,
									 RESMAN_CRITERIA_RESTYPE,
									 RESMAN_TYPE_DEVICEMEM_ALLOCATION,
									 IMG_NULL, 0);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDeinitialiseDevice: Failed ResManFreeResByCriteria call"));
		return eError;
	}

	/*
		De-init the device.
	*/
	if(psDeviceNode->pfnDeInitDevice != IMG_NULL)
	{
		eError = psDeviceNode->pfnDeInitDevice(psDeviceNode);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVDeinitialiseDevice: Failed DeInitDevice call"));
			return eError;
		}
	}

	/*
		Close the device's resource manager context.
	*/
	PVRSRVResManDisconnect(psDeviceNode->hResManContext, IMG_TRUE);
	psDeviceNode->hResManContext = IMG_NULL;

	/* remove node from list */
	List_PVRSRV_DEVICE_NODE_Remove(psDeviceNode);

	/* deallocate id and memory */
	(IMG_VOID)FreeDeviceID(psSysData, ui32DevIndex);
	OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
				sizeof(PVRSRV_DEVICE_NODE), psDeviceNode, IMG_NULL);
	/*not nulling pointer, out of scope*/

	return (PVRSRV_OK);
}


IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PollForValueKM (volatile IMG_UINT32*	pui32LinMemAddr,
										  IMG_UINT32			ui32Value,
										  IMG_UINT32			ui32Mask,
										  IMG_UINT32			ui32Timeoutus,
										  IMG_UINT32			ui32PollPeriodus,
										  IMG_BOOL				bAllowPreemption)
{
#if defined (EMULATOR)
	{
		PVR_UNREFERENCED_PARAMETER(bAllowPreemption);
		#if !defined(__linux__)
		PVR_UNREFERENCED_PARAMETER(ui32PollPeriodus);
		#endif	
		
		/* For the Emulator we want the system to stop when a lock-up is detected so the state can be analysed.
		 * Also the Emulator is much slower than real silicon so timeouts are not valid. 
		 */
		do
		{
			if((*pui32LinMemAddr & ui32Mask) == ui32Value)
			{
				return PVRSRV_OK;
			}

			#if defined(__linux__)
			OSWaitus(ui32PollPeriodus);
			#else
			OSReleaseThreadQuanta();
			#endif	

		} while (ui32Timeoutus); /* Endless loop only for the Emulator */
	}
#else
	{
		IMG_UINT32	ui32ActualValue = 0xFFFFFFFFU; /* Initialiser only required to prevent incorrect warning */

		if (bAllowPreemption)
		{
			PVR_ASSERT(ui32PollPeriodus >= 1000);
		}

		/* PRQA S 3415,4109 1 */ /* macro format critical - leave alone */
		LOOP_UNTIL_TIMEOUT(ui32Timeoutus)
		{
			ui32ActualValue = (*pui32LinMemAddr & ui32Mask);
			if(ui32ActualValue == ui32Value)
			{
				return PVRSRV_OK;
			}
			
			if (bAllowPreemption)
			{
				OSSleepms(ui32PollPeriodus / 1000);
			}
			else
			{
				OSWaitus(ui32PollPeriodus);
			}
		} END_LOOP_UNTIL_TIMEOUT();
	
		PVR_DPF((PVR_DBG_ERROR,"PollForValueKM: Timeout. Expected 0x%x but found 0x%x (mask 0x%x).",
				ui32Value, ui32ActualValue, ui32Mask));
	}
#endif /* #if defined (EMULATOR) */

	return PVRSRV_ERROR_TIMEOUT;
}


/*Level 3 of the loop nesting*/
static IMG_VOID PVRSRVGetMiscInfoKM_RA_GetStats_ForEachVaCb(BM_HEAP *psBMHeap, va_list va)
{
	IMG_CHAR **ppszStr;
	IMG_UINT32 *pui32StrLen;
	IMG_UINT32 ui32Mode;
	PVRSRV_ERROR (*pfnGetStats)(RA_ARENA *, IMG_CHAR **, IMG_UINT32 *);

	ppszStr = va_arg(va, IMG_CHAR**);
	pui32StrLen = va_arg(va, IMG_UINT32*);
	ui32Mode = va_arg(va, IMG_UINT32);

	/* Would be better to pass fn pointer in the variable args list
	 * but MS C compiler complains with error C2066: In ANSI C,
	 * it is not legal to cast between a function pointer and a data pointer.
	 */
	switch(ui32Mode)
	{
		case PVRSRV_MISC_INFO_MEMSTATS_PRESENT:
			pfnGetStats = &RA_GetStats;
			break;
		case PVRSRV_MISC_INFO_FREEMEM_PRESENT:
			pfnGetStats = &RA_GetStatsFreeMem;
			break;
		default:
			return;
	}

	if(psBMHeap->pImportArena)
	{
		pfnGetStats(psBMHeap->pImportArena,
					ppszStr,
					pui32StrLen);
	}

	if(psBMHeap->pVMArena)
	{
		pfnGetStats(psBMHeap->pVMArena,
					ppszStr,
					pui32StrLen);
	}
}

/*Level 2 of the loop nesting*/
static PVRSRV_ERROR PVRSRVGetMiscInfoKM_BMContext_AnyVaCb(BM_CONTEXT *psBMContext, va_list va)
{

	IMG_UINT32 *pui32StrLen;
	IMG_INT32 *pi32Count;
	IMG_CHAR **ppszStr;
	IMG_UINT32 ui32Mode;

	pui32StrLen = va_arg(va, IMG_UINT32*);
	pi32Count = va_arg(va, IMG_INT32*);
	ppszStr = va_arg(va, IMG_CHAR**);
	ui32Mode = va_arg(va, IMG_UINT32);

	CHECK_SPACE(*pui32StrLen);
	*pi32Count = OSSNPrintf(*ppszStr, 100, "\nApplication Context (hDevMemContext) %p:\n",
							(IMG_HANDLE)psBMContext);
	UPDATE_SPACE(*ppszStr, *pi32Count, *pui32StrLen);

	List_BM_HEAP_ForEach_va(psBMContext->psBMHeap,
							&PVRSRVGetMiscInfoKM_RA_GetStats_ForEachVaCb,
							ppszStr,
							pui32StrLen,
							ui32Mode);
	return PVRSRV_OK;
}


/*level 1 of the loop nesting*/
static PVRSRV_ERROR PVRSRVGetMiscInfoKM_Device_AnyVaCb(PVRSRV_DEVICE_NODE *psDeviceNode, va_list va)
{
	IMG_UINT32 *pui32StrLen;
	IMG_INT32 *pi32Count;
	IMG_CHAR **ppszStr;
	IMG_UINT32 ui32Mode;

	pui32StrLen = va_arg(va, IMG_UINT32*);
	pi32Count = va_arg(va, IMG_INT32*);
	ppszStr = va_arg(va, IMG_CHAR**);
	ui32Mode = va_arg(va, IMG_UINT32);

	CHECK_SPACE(*pui32StrLen);
	*pi32Count = OSSNPrintf(*ppszStr, 100, "\n\nDevice Type %d:\n", psDeviceNode->sDevId.eDeviceType);
	UPDATE_SPACE(*ppszStr, *pi32Count, *pui32StrLen);

	/* kernel context: */
	if(psDeviceNode->sDevMemoryInfo.pBMKernelContext)
	{
		CHECK_SPACE(*pui32StrLen);
		*pi32Count = OSSNPrintf(*ppszStr, 100, "\nKernel Context:\n");
		UPDATE_SPACE(*ppszStr, *pi32Count, *pui32StrLen);

		List_BM_HEAP_ForEach_va(psDeviceNode->sDevMemoryInfo.pBMKernelContext->psBMHeap,
								&PVRSRVGetMiscInfoKM_RA_GetStats_ForEachVaCb,
								ppszStr,
								pui32StrLen,
								ui32Mode);
	}

	/* double loop app contexts:heaps */
	return List_BM_CONTEXT_PVRSRV_ERROR_Any_va(psDeviceNode->sDevMemoryInfo.pBMContext,
												&PVRSRVGetMiscInfoKM_BMContext_AnyVaCb,
							 					pui32StrLen,
												pi32Count,
												ppszStr,
												ui32Mode);
}


/*!
******************************************************************************

 @Function	PVRSRVGetMiscInfoKM

 @Description
	Retrieves misc. info.

 @Output PVRSRV_MISC_INFO

 @Return   PVRSRV_ERROR :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetMiscInfoKM(PVRSRV_MISC_INFO *psMiscInfo)
{
	SYS_DATA *psSysData;

	if(!psMiscInfo)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVGetMiscInfoKM: invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psMiscInfo->ui32StatePresent = 0;

	/* do a basic check for uninitialised request flag */
	if(psMiscInfo->ui32StateRequest & ~(PVRSRV_MISC_INFO_TIMER_PRESENT
										|PVRSRV_MISC_INFO_CLOCKGATE_PRESENT
										|PVRSRV_MISC_INFO_MEMSTATS_PRESENT
										|PVRSRV_MISC_INFO_GLOBALEVENTOBJECT_PRESENT
										|PVRSRV_MISC_INFO_DDKVERSION_PRESENT
										|PVRSRV_MISC_INFO_CPUCACHEOP_PRESENT
										|PVRSRV_MISC_INFO_RESET_PRESENT
										|PVRSRV_MISC_INFO_FREEMEM_PRESENT
										|PVRSRV_MISC_INFO_GET_REF_COUNT_PRESENT
										|PVRSRV_MISC_INFO_GET_PAGE_SIZE_PRESENT
										|PVRSRV_MISC_INFO_FORCE_SWAP_TO_SYSTEM_PRESENT))
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVGetMiscInfoKM: invalid state request flags"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	SysAcquireData(&psSysData);

	/* return SOC Timer registers */
	if(((psMiscInfo->ui32StateRequest & PVRSRV_MISC_INFO_TIMER_PRESENT) != 0UL) &&
		(psSysData->pvSOCTimerRegisterKM != IMG_NULL))
	{
		psMiscInfo->ui32StatePresent |= PVRSRV_MISC_INFO_TIMER_PRESENT;
		psMiscInfo->pvSOCTimerRegisterKM = psSysData->pvSOCTimerRegisterKM;
		psMiscInfo->hSOCTimerRegisterOSMemHandle = psSysData->hSOCTimerRegisterOSMemHandle;
	}
	else
	{
		psMiscInfo->pvSOCTimerRegisterKM = IMG_NULL;
		psMiscInfo->hSOCTimerRegisterOSMemHandle = IMG_NULL;
	}

	/* return SOC Clock Gating registers */
	if(((psMiscInfo->ui32StateRequest & PVRSRV_MISC_INFO_CLOCKGATE_PRESENT) != 0UL) &&
		(psSysData->pvSOCClockGateRegsBase != IMG_NULL))
	{
		psMiscInfo->ui32StatePresent |= PVRSRV_MISC_INFO_CLOCKGATE_PRESENT;
		psMiscInfo->pvSOCClockGateRegs = psSysData->pvSOCClockGateRegsBase;
		psMiscInfo->ui32SOCClockGateRegsSize = psSysData->ui32SOCClockGateRegsSize;
	}

	/* memory stats */
	if(((psMiscInfo->ui32StateRequest & PVRSRV_MISC_INFO_MEMSTATS_PRESENT) != 0UL) &&
		(psMiscInfo->pszMemoryStr != IMG_NULL))
	{
		RA_ARENA			**ppArena;
/*		BM_HEAP				*psBMHeap;
		BM_CONTEXT			*psBMContext;
		PVRSRV_DEVICE_NODE	*psDeviceNode;*/
		IMG_CHAR			*pszStr;
		IMG_UINT32			ui32StrLen;
		IMG_INT32			i32Count;

		pszStr = psMiscInfo->pszMemoryStr;
		ui32StrLen = psMiscInfo->ui32MemoryStrLen;

		psMiscInfo->ui32StatePresent |= PVRSRV_MISC_INFO_MEMSTATS_PRESENT;

		/* Local backing stores */
		ppArena = &psSysData->apsLocalDevMemArena[0];
		while(*ppArena)
		{
			CHECK_SPACE(ui32StrLen);
			i32Count = OSSNPrintf(pszStr, 100, "\nLocal Backing Store:\n");
			UPDATE_SPACE(pszStr, i32Count, ui32StrLen);

			RA_GetStats(*ppArena,
							&pszStr,
							&ui32StrLen);
			/* advance through the array */
			ppArena++;
		}

		/* per device */
/*		psDeviceNode = psSysData->psDeviceNodeList;*/

		/*triple loop; devices:contexts:heaps*/
		List_PVRSRV_DEVICE_NODE_PVRSRV_ERROR_Any_va(psSysData->psDeviceNodeList,
													&PVRSRVGetMiscInfoKM_Device_AnyVaCb,
													&ui32StrLen,
													&i32Count,
													&pszStr,
													PVRSRV_MISC_INFO_MEMSTATS_PRESENT);

		/* attach a new line and string terminate */
		i32Count = OSSNPrintf(pszStr, 100, "\n");
		UPDATE_SPACE(pszStr, i32Count, ui32StrLen);
	}

	/* Lean version of mem stats: only show free mem on each RA */
	if(((psMiscInfo->ui32StateRequest & PVRSRV_MISC_INFO_FREEMEM_PRESENT) != 0)
		&& psMiscInfo->pszMemoryStr)
	{
		IMG_CHAR			*pszStr;
		IMG_UINT32			ui32StrLen;
		IMG_INT32			i32Count;
		
		pszStr = psMiscInfo->pszMemoryStr;
		ui32StrLen = psMiscInfo->ui32MemoryStrLen;
  
		psMiscInfo->ui32StatePresent |= PVRSRV_MISC_INFO_FREEMEM_PRESENT;

		/* triple loop over devices:contexts:heaps */
		List_PVRSRV_DEVICE_NODE_PVRSRV_ERROR_Any_va(psSysData->psDeviceNodeList,
													&PVRSRVGetMiscInfoKM_Device_AnyVaCb,
													&ui32StrLen,
													&i32Count,
													&pszStr,
													PVRSRV_MISC_INFO_FREEMEM_PRESENT);
		
		i32Count = OSSNPrintf(pszStr, 100, "\n");
		UPDATE_SPACE(pszStr, i32Count, ui32StrLen);
	}

	if(((psMiscInfo->ui32StateRequest & PVRSRV_MISC_INFO_GLOBALEVENTOBJECT_PRESENT) != 0UL) &&
		(psSysData->psGlobalEventObject != IMG_NULL))
	{
		psMiscInfo->ui32StatePresent |= PVRSRV_MISC_INFO_GLOBALEVENTOBJECT_PRESENT;
		psMiscInfo->sGlobalEventObject = *psSysData->psGlobalEventObject;
	}

	/* DDK version and memstats not supported in same call to GetMiscInfo */

	if (((psMiscInfo->ui32StateRequest & PVRSRV_MISC_INFO_DDKVERSION_PRESENT) != 0UL)
		&& ((psMiscInfo->ui32StateRequest & PVRSRV_MISC_INFO_MEMSTATS_PRESENT) == 0UL)
		&& (psMiscInfo->pszMemoryStr != IMG_NULL))
	{
		IMG_CHAR	*pszStr;
		IMG_UINT32	ui32StrLen;
		IMG_UINT32 	ui32LenStrPerNum = 12; /* string length per UI32: 10 digits + '.' + '\0' = 12 bytes */
		IMG_INT32	i32Count;
		IMG_INT i;
		psMiscInfo->ui32StatePresent |= PVRSRV_MISC_INFO_DDKVERSION_PRESENT;

		/* construct DDK string */
		psMiscInfo->aui32DDKVersion[0] = PVRVERSION_MAJ;
		psMiscInfo->aui32DDKVersion[1] = PVRVERSION_MIN;
		psMiscInfo->aui32DDKVersion[2] = PVRVERSION_BUILD_HI;
		psMiscInfo->aui32DDKVersion[3] = PVRVERSION_BUILD_LO;

		pszStr = psMiscInfo->pszMemoryStr;
		ui32StrLen = psMiscInfo->ui32MemoryStrLen;

		for (i=0; i<4; i++)
		{
			if (ui32StrLen < ui32LenStrPerNum)
			{
				return PVRSRV_ERROR_INVALID_PARAMS;
			}

			i32Count = OSSNPrintf(pszStr, ui32LenStrPerNum, "%u", psMiscInfo->aui32DDKVersion[i]);
			UPDATE_SPACE(pszStr, i32Count, ui32StrLen);
			if (i != 3)
			{
				i32Count = OSSNPrintf(pszStr, 2, ".");
				UPDATE_SPACE(pszStr, i32Count, ui32StrLen);
			}
		}
	}

	if((psMiscInfo->ui32StateRequest & PVRSRV_MISC_INFO_CPUCACHEOP_PRESENT) != 0UL)
	{
		psMiscInfo->ui32StatePresent |= PVRSRV_MISC_INFO_CPUCACHEOP_PRESENT;

		if(psMiscInfo->sCacheOpCtl.bDeferOp)
		{
			/* For now, assume deferred ops are "full" cache ops,
			 * and we don't need (or expect) a meminfo.
			 */
			psSysData->ePendingCacheOpType = psMiscInfo->sCacheOpCtl.eCacheOpType;
		}
		else
		{
			PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo;
			PVRSRV_PER_PROCESS_DATA *psPerProc;

			if(!psMiscInfo->sCacheOpCtl.u.psKernelMemInfo)
			{
				PVR_DPF((PVR_DBG_WARNING, "PVRSRVGetMiscInfoKM: "
						 "Ignoring non-deferred cache op with no meminfo"));
				return PVRSRV_ERROR_INVALID_PARAMS;
			}

			if(psSysData->ePendingCacheOpType != PVRSRV_MISC_INFO_CPUCACHEOP_NONE)
			{
				PVR_DPF((PVR_DBG_MESSAGE, "PVRSRVGetMiscInfoKM: "
						 "Deferred cache op is pending. It is unlikely you want "
						 "to combine deferred cache ops with immediate ones"));
			}

			psPerProc = PVRSRVFindPerProcessData();

			if(PVRSRVLookupHandle(psPerProc->psHandleBase,
								  (IMG_PVOID *)&psKernelMemInfo,
								  psMiscInfo->sCacheOpCtl.u.psKernelMemInfo,
								  PVRSRV_HANDLE_TYPE_MEM_INFO) != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "PVRSRVGetMiscInfoKM: "
						 "Can't find kernel meminfo"));
				return PVRSRV_ERROR_INVALID_PARAMS;
			}

			if(psMiscInfo->sCacheOpCtl.eCacheOpType == PVRSRV_MISC_INFO_CPUCACHEOP_FLUSH)
			{
				if(!OSFlushCPUCacheRangeKM(psKernelMemInfo->sMemBlk.hOSMemHandle,
										   0,
										   psMiscInfo->sCacheOpCtl.pvBaseVAddr,
										   psMiscInfo->sCacheOpCtl.ui32Length))
				{
					return PVRSRV_ERROR_CACHEOP_FAILED;
				}
			}
			else if(psMiscInfo->sCacheOpCtl.eCacheOpType == PVRSRV_MISC_INFO_CPUCACHEOP_CLEAN)
			{
				if(psMiscInfo->sCacheOpCtl.ui32Length!=0)
				{
					if(!OSCleanCPUCacheRangeKM(psKernelMemInfo->sMemBlk.hOSMemHandle,
											   0,
											   psMiscInfo->sCacheOpCtl.pvBaseVAddr,
											   psMiscInfo->sCacheOpCtl.ui32Length))
					{
						return PVRSRV_ERROR_CACHEOP_FAILED;
					}
				}
			}
			/* S.LSI 12.11.28 invalidate cache range add */
			else if(psMiscInfo->sCacheOpCtl.eCacheOpType == PVRSRV_MISC_INFO_CPUCACHEOP_INVALIDATE)
			{
				if(!OSInvalidateCPUCacheRangeKM(psKernelMemInfo->sMemBlk.hOSMemHandle,
										   0,
										   psMiscInfo->sCacheOpCtl.pvBaseVAddr,
										   psMiscInfo->sCacheOpCtl.ui32Length))
				{
					return PVRSRV_ERROR_CACHEOP_FAILED;
				}
			}
		}
	}

	if((psMiscInfo->ui32StateRequest & PVRSRV_MISC_INFO_GET_REF_COUNT_PRESENT) != 0UL)
	{
		PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo;
		PVRSRV_PER_PROCESS_DATA *psPerProc;

		psMiscInfo->ui32StatePresent |= PVRSRV_MISC_INFO_GET_REF_COUNT_PRESENT;

		psPerProc = PVRSRVFindPerProcessData();

		if(PVRSRVLookupHandle(psPerProc->psHandleBase,
							  (IMG_PVOID *)&psKernelMemInfo,
							  psMiscInfo->sGetRefCountCtl.u.psKernelMemInfo,
							  PVRSRV_HANDLE_TYPE_MEM_INFO) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVGetMiscInfoKM: "
									"Can't find kernel meminfo"));
			return PVRSRV_ERROR_INVALID_PARAMS;
		}

		psMiscInfo->sGetRefCountCtl.ui32RefCount = psKernelMemInfo->ui32RefCount;
	}

	if ((psMiscInfo->ui32StateRequest & PVRSRV_MISC_INFO_GET_PAGE_SIZE_PRESENT) != 0UL)
	{
		psMiscInfo->ui32PageSize = HOST_PAGESIZE();
		psMiscInfo->ui32StatePresent |= PVRSRV_MISC_INFO_GET_PAGE_SIZE_PRESENT;
	}

#if defined(PVRSRV_RESET_ON_HWTIMEOUT)
	if((psMiscInfo->ui32StateRequest & PVRSRV_MISC_INFO_RESET_PRESENT) != 0UL)
	{
		PVR_LOG(("User requested OS reset"));
		OSPanic();
	}
#endif /* #if defined(PVRSRV_RESET_ON_HWTIMEOUT) */

	if ((psMiscInfo->ui32StateRequest & PVRSRV_MISC_INFO_FORCE_SWAP_TO_SYSTEM_PRESENT) != 0UL)
	{
		PVRSRVSetDCState(DC_STATE_FORCE_SWAP_TO_SYSTEM);
		psMiscInfo->ui32StatePresent |= PVRSRV_MISC_INFO_FORCE_SWAP_TO_SYSTEM_PRESENT;
	}

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	PVRSRVDeviceLISR

 @Description
	OS-independent Device Low-level Interrupt Service Routine

 @Input psDeviceNode

 @Return   IMG_BOOL : Whether any interrupts were serviced

******************************************************************************/
IMG_BOOL IMG_CALLCONV PVRSRVDeviceLISR(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	SYS_DATA			*psSysData;
	IMG_BOOL			bStatus = IMG_FALSE;
	IMG_UINT32			ui32InterruptSource;

	if(!psDeviceNode)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVDeviceLISR: Invalid params\n"));
		goto out;
	}
	psSysData = psDeviceNode->psSysData;

	/* query the SOC/system to see whether this device was the source of the interrupt */
	ui32InterruptSource = SysGetInterruptSource(psSysData, psDeviceNode);
	if(ui32InterruptSource & psDeviceNode->ui32SOCInterruptBit)
	{
		if(psDeviceNode->pfnDeviceISR != IMG_NULL)
		{
			bStatus = (*psDeviceNode->pfnDeviceISR)(psDeviceNode->pvISRData);
		}

		SysClearInterrupts(psSysData, psDeviceNode->ui32SOCInterruptBit);
	}

out:
	return bStatus;
}

static IMG_VOID PVRSRVSystemLISR_ForEachVaCb(PVRSRV_DEVICE_NODE *psDeviceNode, va_list va)
{

	IMG_BOOL *pbStatus;
	IMG_UINT32 *pui32InterruptSource;
	IMG_UINT32 *pui32ClearInterrupts;

	pbStatus = va_arg(va, IMG_BOOL*);
	pui32InterruptSource = va_arg(va, IMG_UINT32*);
	pui32ClearInterrupts = va_arg(va, IMG_UINT32*);


	if(psDeviceNode->pfnDeviceISR != IMG_NULL)
	{
		if(*pui32InterruptSource & psDeviceNode->ui32SOCInterruptBit)
		{
			if((*psDeviceNode->pfnDeviceISR)(psDeviceNode->pvISRData))
			{
				/* Record if serviced any interrupts. */
				*pbStatus = IMG_TRUE;
			}
			/* Combine the SOC clear bits. */
			*pui32ClearInterrupts |= psDeviceNode->ui32SOCInterruptBit;
		}
	}
}

/*!
******************************************************************************

 @Function	PVRSRVSystemLISR

 @Description
	OS-independent System Low-level Interrupt Service Routine

 @Input pvSysData

 @Return   IMG_BOOL : Whether any interrupts were serviced

******************************************************************************/
IMG_BOOL IMG_CALLCONV PVRSRVSystemLISR(IMG_VOID *pvSysData)
{
	SYS_DATA			*psSysData = pvSysData;
	IMG_BOOL			bStatus = IMG_FALSE;
	IMG_UINT32			ui32InterruptSource;
	IMG_UINT32			ui32ClearInterrupts = 0;
/*	PVRSRV_DEVICE_NODE	*psDeviceNode;*/

	if(!psSysData)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVSystemLISR: Invalid params\n"));
/*		goto out; */
	}
	else
	{
		/* query SOC for source of interrupts */
		ui32InterruptSource = SysGetInterruptSource(psSysData, IMG_NULL);

		/* only proceed if PVR interrupts */
		if(ui32InterruptSource)
		{
			/* traverse the devices' ISR handlers */
			List_PVRSRV_DEVICE_NODE_ForEach_va(psSysData->psDeviceNodeList,
												&PVRSRVSystemLISR_ForEachVaCb,
												&bStatus,
												&ui32InterruptSource,
												&ui32ClearInterrupts);

			SysClearInterrupts(psSysData, ui32ClearInterrupts);
		}
/*out:*/
	}
	return bStatus;
}


static IMG_VOID PVRSRVMISR_ForEachCb(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	if(psDeviceNode->pfnDeviceMISR != IMG_NULL)
	{
		(*psDeviceNode->pfnDeviceMISR)(psDeviceNode->pvISRData);
	}
}

/*!
******************************************************************************

 @Function	PVRSRVMISR

 @Input pvSysData

 @Description
	OS-independent Medium-level Interrupt Service Routine

******************************************************************************/
IMG_VOID IMG_CALLCONV PVRSRVMISR(IMG_VOID *pvSysData)
{
	SYS_DATA			*psSysData = pvSysData;
/*	PVRSRV_DEVICE_NODE	*psDeviceNode; */

	if(!psSysData)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVMISR: Invalid params\n"));
		return;
	}

	/* Traverse the devices' MISR handlers. */
	List_PVRSRV_DEVICE_NODE_ForEach(psSysData->psDeviceNodeList,
									&PVRSRVMISR_ForEachCb);

	/* Process the queues. */
	if (PVRSRVProcessQueues(IMG_FALSE) == PVRSRV_ERROR_PROCESSING_BLOCKED)
	{
		PVRSRVProcessQueues(IMG_FALSE);
	}

	/* signal global event object */
	if (psSysData->psGlobalEventObject)
	{
		IMG_HANDLE hOSEventKM = psSysData->psGlobalEventObject->hOSEventKM;
		if(hOSEventKM)
		{
			OSEventObjectSignalKM(hOSEventKM);
		}
	}
}


/*!
******************************************************************************

 @Function	PVRSRVProcessConnect

 @Description	Inform services that a process has connected.

 @Input		ui32PID - process ID

 @Return	PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVProcessConnect(IMG_UINT32	ui32PID, IMG_UINT32 ui32Flags)
{
	return PVRSRVPerProcessDataConnect(ui32PID, ui32Flags);
}


/*!
******************************************************************************

 @Function	PVRSRVProcessDisconnect

 @Description	Inform services that a process has disconnected.

 @Input		ui32PID - process ID

 @Return	IMG_VOID

******************************************************************************/
IMG_EXPORT
IMG_VOID IMG_CALLCONV PVRSRVProcessDisconnect(IMG_UINT32	ui32PID)
{
	PVRSRVPerProcessDataDisconnect(ui32PID);
}


/*!
******************************************************************************

 @Function	PVRSRVSaveRestoreLiveSegments

 @Input pArena - the arena the segment was originally allocated from.
        pbyBuffer - the system memory buffer set to null to get the size needed.
        puiBufSize - size of system memory buffer.
        bSave - IMG_TRUE if a save is required

 @Description
	Function to save or restore Resources Live segments

******************************************************************************/
PVRSRV_ERROR IMG_CALLCONV PVRSRVSaveRestoreLiveSegments(IMG_HANDLE hArena, IMG_PBYTE pbyBuffer,
														IMG_SIZE_T *puiBufSize, IMG_BOOL bSave)
{
	IMG_SIZE_T         uiBytesSaved = 0;
	IMG_PVOID          pvLocalMemCPUVAddr;
	RA_SEGMENT_DETAILS sSegDetails;

	if (hArena == IMG_NULL)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	sSegDetails.uiSize = 0;
	sSegDetails.sCpuPhyAddr.uiAddr = 0;
	sSegDetails.hSegment = 0;

	/* walk the arena segments and write live one to the  buffer */
	while (RA_GetNextLiveSegment(hArena, &sSegDetails))
	{
		if (pbyBuffer == IMG_NULL)
		{
			/* calc buffer required */
			uiBytesSaved += sizeof(sSegDetails.uiSize) + sSegDetails.uiSize;
		}
		else
		{
			if ((uiBytesSaved + sizeof(sSegDetails.uiSize) + sSegDetails.uiSize) > *puiBufSize)
			{
				return (PVRSRV_ERROR_OUT_OF_MEMORY);
			}

			PVR_DPF((
                PVR_DBG_MESSAGE, 
                "PVRSRVSaveRestoreLiveSegments: Base " CPUPADDR_FMT " size %x", 
                sSegDetails.sCpuPhyAddr.uiAddr, 
                sSegDetails.uiSize));

			/* Map the device's local memory area onto the host. */
			pvLocalMemCPUVAddr = OSMapPhysToLin(sSegDetails.sCpuPhyAddr,
									sSegDetails.uiSize,
									PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
									IMG_NULL);
			if (pvLocalMemCPUVAddr == IMG_NULL)
			{
				PVR_DPF((PVR_DBG_ERROR, "PVRSRVSaveRestoreLiveSegments: Failed to map local memory to host"));
				return (PVRSRV_ERROR_OUT_OF_MEMORY);
			}

			if (bSave)
			{
				/* write segment size then segment data */
				OSMemCopy(pbyBuffer, &sSegDetails.uiSize, sizeof(sSegDetails.uiSize));
				pbyBuffer += sizeof(sSegDetails.uiSize);

				OSMemCopy(pbyBuffer, pvLocalMemCPUVAddr, sSegDetails.uiSize);
				pbyBuffer += sSegDetails.uiSize;
			}
			else
			{
				IMG_UINT32 uiSize;
				/* reag segment size and validate */
				OSMemCopy(&uiSize, pbyBuffer, sizeof(sSegDetails.uiSize));

				if (uiSize != sSegDetails.uiSize)
				{
					PVR_DPF((PVR_DBG_ERROR, "PVRSRVSaveRestoreLiveSegments: Segment size error"));
				}
				else
				{
					pbyBuffer += sizeof(sSegDetails.uiSize);

					OSMemCopy(pvLocalMemCPUVAddr, pbyBuffer, sSegDetails.uiSize);
					pbyBuffer += sSegDetails.uiSize;
				}
			}


			uiBytesSaved += sizeof(sSegDetails.uiSize) + sSegDetails.uiSize;

			OSUnMapPhysToLin(pvLocalMemCPUVAddr,
		                     sSegDetails.uiSize,
		                     PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
		                     IMG_NULL);
		}
	}

	if (pbyBuffer == IMG_NULL)
	{
		*puiBufSize = uiBytesSaved;
	}

	return (PVRSRV_OK);
}


/*!
 ******************************************************************************

 @Function		PVRSRVGetErrorStringKM

 @Description	Returns a text string relating to the PVRSRV_ERROR enum.

 @Note		case statement used rather than an indexed arrary to ensure text is
 			synchronised with the correct enum

 @Input		eError : PVRSRV_ERROR enum

 @Return	const IMG_CHAR * : Text string

 @Note		Must be kept in sync with servicesext.h

******************************************************************************/

IMG_EXPORT
const IMG_CHAR *PVRSRVGetErrorStringKM(PVRSRV_ERROR eError)
{ 
/* PRQA S 5087 1 */ /* include file required here */
#include "pvrsrv_errors.h"
}

static IMG_VOID PVRSRVCommandCompleteCallbacks_ForEachCb(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	if(psDeviceNode->pfnDeviceCommandComplete != IMG_NULL)
	{
		/* Call the device's callback function. */
		(*psDeviceNode->pfnDeviceCommandComplete)(psDeviceNode);
	}
}

/*!
******************************************************************************

 @Function	PVRSRVScheduleDeviceCallbacks

 @Description	Schedule all device callbacks

 @Return	IMG_VOID

******************************************************************************/
IMG_VOID PVRSRVScheduleDeviceCallbacks(IMG_VOID)
{
	SYS_DATA				*psSysData;
/*	PVRSRV_DEVICE_NODE		*psDeviceNode;*/

	SysAcquireData(&psSysData);

	/*for all the device, invoke the callback function*/
	List_PVRSRV_DEVICE_NODE_ForEach(psSysData->psDeviceNodeList,
									&PVRSRVCommandCompleteCallbacks_ForEachCb);
}

/*!
******************************************************************************

 @Function	PVRSRVScheduleDevices

 @Description	Schedules all Services-Managed Devices to check their pending
 				command queues. The intention is that ScheduleDevices be called by the
				3rd party BC driver after it has finished writing new data to its output
				texture.

 @Return	IMG_VOID

******************************************************************************/
IMG_EXPORT
IMG_VOID PVRSRVScheduleDevicesKM(IMG_VOID)
{
	PVRSRVScheduleDeviceCallbacks();
}

/*****************************************************************************
 End of file (pvrsrv.c)
*****************************************************************************/
