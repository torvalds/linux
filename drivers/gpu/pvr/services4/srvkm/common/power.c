/*************************************************************************/ /*!
@Title          Power management functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Main APIs for power management functions
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
#include "pdump_km.h"

#include "lists.h"

static IMG_BOOL gbInitServerRunning = IMG_FALSE;
static IMG_BOOL gbInitServerRan = IMG_FALSE;
static IMG_BOOL gbInitSuccessful = IMG_FALSE;

/*!
******************************************************************************

 @Function	PVRSRVSetInitServerState

 @Description	Sets given services init state.

 @Input		eInitServerState : a services init state
 @Input		bState : a state to set

 @Return	PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVSetInitServerState(PVRSRV_INIT_SERVER_STATE eInitServerState, IMG_BOOL bState)
{

	switch(eInitServerState)
	{
		case PVRSRV_INIT_SERVER_RUNNING:
			gbInitServerRunning	= bState;
			break;
		case PVRSRV_INIT_SERVER_RAN:
			gbInitServerRan	= bState;
			break;
		case PVRSRV_INIT_SERVER_SUCCESSFUL:
			gbInitSuccessful = bState;
			break;
		default:
			PVR_DPF((PVR_DBG_ERROR,
				"PVRSRVSetInitServerState : Unknown state %x", eInitServerState));
			return PVRSRV_ERROR_UNKNOWN_INIT_SERVER_STATE;
	}

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVGetInitServerState

 @Description	Tests whether a given services init state was run.

 @Input		eInitServerState : a services init state

 @Return	IMG_BOOL

******************************************************************************/
IMG_EXPORT
IMG_BOOL PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_STATE eInitServerState)
{
	IMG_BOOL	bReturnVal;

	switch(eInitServerState)
	{
		case PVRSRV_INIT_SERVER_RUNNING:
			bReturnVal = gbInitServerRunning;
			break;
		case PVRSRV_INIT_SERVER_RAN:
			bReturnVal = gbInitServerRan;
			break;
		case PVRSRV_INIT_SERVER_SUCCESSFUL:
			bReturnVal = gbInitSuccessful;
			break;
		default:
			PVR_DPF((PVR_DBG_ERROR,
				"PVRSRVGetInitServerState : Unknown state %x", eInitServerState));
			bReturnVal = IMG_FALSE;
	}

	return bReturnVal;
}

/*!
******************************************************************************

 @Function	_IsSystemStatePowered

 @Description	Tests whether a given system state represents powered-up.

 @Input		eSystemPowerState : a system power state

 @Return	IMG_BOOL

******************************************************************************/
static IMG_BOOL _IsSystemStatePowered(PVRSRV_SYS_POWER_STATE eSystemPowerState)
{
	return (IMG_BOOL)(eSystemPowerState < PVRSRV_SYS_POWER_STATE_D2);
}


/*!
******************************************************************************

 @Function	PVRSRVPowerLock

 @Description	Obtain the mutex for power transitions

 @Input		ui32CallerID : KERNEL_ID or ISR_ID
 @Input		bSystemPowerEvent : Only pass IMG_TRUE if the lock is for a
 								system power state change

 @Return	PVRSRV_ERROR IMG_CALLCONV

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVPowerLock(IMG_UINT32	ui32CallerID,
							 IMG_BOOL	bSystemPowerEvent)
{
	PVRSRV_ERROR	eError;
	SYS_DATA	*psSysData;
	IMG_UINT32	ui32Timeout = 1000000;
	IMG_BOOL	bTryLock = (ui32CallerID == ISR_ID);

	SysAcquireData(&psSysData);

	eError = OSPowerLockWrap(bTryLock);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	do
	{
		eError = OSLockResource(&psSysData->sPowerStateChangeResource,
								ui32CallerID);
		if (eError == PVRSRV_OK)
		{
			break;
		}
		else if (bTryLock)
		{
			/*
				ISR failed to acquire lock so it must be held by a kernel thread.
			*/
			eError = PVRSRV_ERROR_RETRY;
			break;
		}

		OSWaitus(1);
		ui32Timeout--;
	} while (ui32Timeout > 0);

	if (eError != PVRSRV_OK)
	{
		OSPowerLockUnwrap();
	}

	/* PRQA S 3415 3 */ /* side effects desired */
	if ((eError == PVRSRV_OK) &&
		!bSystemPowerEvent &&
		!_IsSystemStatePowered(psSysData->eCurrentPowerState))
	{
		/* Reject device power state change due to system power state. */
		PVRSRVPowerUnlock(ui32CallerID);
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVPowerLock side effect - eError:%d bSystemPowerEvent:%u eCurrentPowerState:%d",
				eError, bSystemPowerEvent, psSysData->eCurrentPowerState));
		eError = PVRSRV_ERROR_RETRY;
	}

	return eError;
}


/*!
******************************************************************************

 @Function	PVRSRVPowerUnlock

 @Description	Release the mutex for power transitions

 @Input		ui32CallerID : KERNEL_ID or ISR_ID

 @Return	PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
IMG_VOID PVRSRVPowerUnlock(IMG_UINT32	ui32CallerID)
{
	OSUnlockResource(&gpsSysData->sPowerStateChangeResource, ui32CallerID);
	OSPowerLockUnwrap();
}


/*!
******************************************************************************

 @Function	PVRSRVDevicePrePowerStateKM_AnyVaCb

 @Description

 Perform device-specific processing required before a power transition

 @Input		psPowerDevice : the device
 @Input		va : variable argument list with:
 				bAllDevices : IMG_TRUE - All devices
 						  	  IMG_FALSE - Use ui32DeviceIndex
				ui32DeviceIndex : device index
				eNewPowerState : New power state

 @Return	PVRSRV_ERROR

******************************************************************************/
static PVRSRV_ERROR PVRSRVDevicePrePowerStateKM_AnyVaCb(PVRSRV_POWER_DEV *psPowerDevice, va_list va)
{
	PVRSRV_DEV_POWER_STATE	eNewDevicePowerState;
	PVRSRV_ERROR			eError;

	/*Variable Argument variables*/
	IMG_BOOL				bAllDevices;
	IMG_UINT32				ui32DeviceIndex;
	PVRSRV_DEV_POWER_STATE	eNewPowerState;

	/* WARNING: if types were not aligned to 4 bytes, this could be dangerous. */
	bAllDevices = va_arg(va, IMG_BOOL);
	ui32DeviceIndex = va_arg(va, IMG_UINT32);
	eNewPowerState = va_arg(va, PVRSRV_DEV_POWER_STATE);

	if (bAllDevices || (ui32DeviceIndex == psPowerDevice->ui32DeviceIndex))
	{
		eNewDevicePowerState = (eNewPowerState == PVRSRV_DEV_POWER_STATE_DEFAULT) ?
							psPowerDevice->eDefaultPowerState : eNewPowerState;

		if (psPowerDevice->eCurrentPowerState != eNewDevicePowerState)
		{
			if (psPowerDevice->pfnPrePower != IMG_NULL)
			{
				/* Call the device's power callback. */
				eError = psPowerDevice->pfnPrePower(psPowerDevice->hDevCookie,
													eNewDevicePowerState,
													psPowerDevice->eCurrentPowerState);
				if (eError != PVRSRV_OK)
				{
					return eError;
				}
			}

			/* Do any required system-layer processing. */
			eError = SysDevicePrePowerState(psPowerDevice->ui32DeviceIndex,
											eNewDevicePowerState,
											psPowerDevice->eCurrentPowerState);
			if (eError != PVRSRV_OK)
			{
				return eError;
			}
		}
	}

	return  PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVDevicePrePowerStateKM

 @Description

 Perform device-specific processing required before a power transition

 @Input		bAllDevices : IMG_TRUE - All devices
 						  IMG_FALSE - Use ui32DeviceIndex
 @Input		ui32DeviceIndex : device index
 @Input		eNewPowerState : New power state

 @Return	PVRSRV_ERROR

******************************************************************************/
static
PVRSRV_ERROR PVRSRVDevicePrePowerStateKM(IMG_BOOL				bAllDevices,
										 IMG_UINT32				ui32DeviceIndex,
										 PVRSRV_DEV_POWER_STATE	eNewPowerState)
{
	PVRSRV_ERROR		eError;
	SYS_DATA			*psSysData;

	SysAcquireData(&psSysData);

	/* Loop through the power devices. */
	eError = List_PVRSRV_POWER_DEV_PVRSRV_ERROR_Any_va(psSysData->psPowerDeviceList,
														&PVRSRVDevicePrePowerStateKM_AnyVaCb,
														bAllDevices,
														ui32DeviceIndex,
														eNewPowerState);

	return eError;
}

/*!
******************************************************************************

 @Function	PVRSRVDevicePostPowerStateKM_AnyVaCb

 @Description

 Perform device-specific processing required after a power transition

 @Input		psPowerDevice : the device
 @Input		va : variable argument list with:
 				bAllDevices : IMG_TRUE - All devices
 						  	  IMG_FALSE - Use ui32DeviceIndex
				ui32DeviceIndex : device index
				eNewPowerState : New power state

 @Return	PVRSRV_ERROR

******************************************************************************/
static PVRSRV_ERROR PVRSRVDevicePostPowerStateKM_AnyVaCb(PVRSRV_POWER_DEV *psPowerDevice, va_list va)
{
	PVRSRV_DEV_POWER_STATE	eNewDevicePowerState;
	PVRSRV_ERROR			eError;

	/*Variable Argument variables*/
	IMG_BOOL				bAllDevices;
	IMG_UINT32				ui32DeviceIndex;
	PVRSRV_DEV_POWER_STATE	eNewPowerState;

	/* WARNING: if types were not aligned to 4 bytes, this could be dangerous. */
	bAllDevices = va_arg(va, IMG_BOOL);
	ui32DeviceIndex = va_arg(va, IMG_UINT32);
	eNewPowerState = va_arg(va, PVRSRV_DEV_POWER_STATE);

	if (bAllDevices || (ui32DeviceIndex == psPowerDevice->ui32DeviceIndex))
	{
		eNewDevicePowerState = (eNewPowerState == PVRSRV_DEV_POWER_STATE_DEFAULT) ?
								psPowerDevice->eDefaultPowerState : eNewPowerState;

		if (psPowerDevice->eCurrentPowerState != eNewDevicePowerState)
		{
			/* Do any required system-layer processing. */
			eError = SysDevicePostPowerState(psPowerDevice->ui32DeviceIndex,
											 eNewDevicePowerState,
											 psPowerDevice->eCurrentPowerState);
			if (eError != PVRSRV_OK)
			{
				return eError;
			}

			if (psPowerDevice->pfnPostPower != IMG_NULL)
			{
				/* Call the device's power callback. */
				eError = psPowerDevice->pfnPostPower(psPowerDevice->hDevCookie,
													 eNewDevicePowerState,
													 psPowerDevice->eCurrentPowerState);
				if (eError != PVRSRV_OK)
				{
					return eError;
				}
			}

			psPowerDevice->eCurrentPowerState = eNewDevicePowerState;
		}
	}
	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVDevicePostPowerStateKM

 @Description

 Perform device-specific processing required after a power transition

 @Input		bAllDevices : IMG_TRUE - All devices
 						  IMG_FALSE - Use ui32DeviceIndex
 @Input		ui32DeviceIndex : device index
 @Input		eNewPowerState : New power state

 @Return	PVRSRV_ERROR

******************************************************************************/
static
PVRSRV_ERROR PVRSRVDevicePostPowerStateKM(IMG_BOOL					bAllDevices,
										  IMG_UINT32				ui32DeviceIndex,
										  PVRSRV_DEV_POWER_STATE	eNewPowerState)
{
	PVRSRV_ERROR		eError;
	SYS_DATA			*psSysData;

	SysAcquireData(&psSysData);

	/* Loop through the power devices. */
	eError = List_PVRSRV_POWER_DEV_PVRSRV_ERROR_Any_va(psSysData->psPowerDeviceList,
														&PVRSRVDevicePostPowerStateKM_AnyVaCb,
														bAllDevices,
														ui32DeviceIndex,
														eNewPowerState);

	return eError;
}


/*!
******************************************************************************

 @Function	PVRSRVSetDevicePowerStateKM

 @Description	Set the Device into a new state

 @Input		ui32DeviceIndex : device index
 @Input		eNewPowerState : New power state
 @Input		ui32CallerID : KERNEL_ID or ISR_ID
 @Input		bRetainMutex : If true, the power mutex is retained on exit

 @Return	PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVSetDevicePowerStateKM(IMG_UINT32				ui32DeviceIndex,
										 PVRSRV_DEV_POWER_STATE	eNewPowerState)
{
	PVRSRV_ERROR	eError;
	SYS_DATA		*psSysData;

	SysAcquireData(&psSysData);

	#if defined(PDUMP)
	if (eNewPowerState == PVRSRV_DEV_POWER_STATE_DEFAULT)
	{
		/*
			Pdump a power-up regardless of the default state.
			Then disable pdump and transition to the default power state.
			This ensures that a power-up is always present in the pdump when necessary.
		*/
		eError = PVRSRVDevicePrePowerStateKM(IMG_FALSE, ui32DeviceIndex, PVRSRV_DEV_POWER_STATE_ON);
		if(eError != PVRSRV_OK)
		{
			goto Exit;
		}

		eError = PVRSRVDevicePostPowerStateKM(IMG_FALSE, ui32DeviceIndex, PVRSRV_DEV_POWER_STATE_ON);

		if (eError != PVRSRV_OK)
		{
			goto Exit;
		}

		PDUMPSUSPEND();
	}
	#endif /* PDUMP */

	eError = PVRSRVDevicePrePowerStateKM(IMG_FALSE, ui32DeviceIndex, eNewPowerState);
	if(eError != PVRSRV_OK)
	{
		if (eNewPowerState == PVRSRV_DEV_POWER_STATE_DEFAULT)
		{
			PDUMPRESUME();
		}
		goto Exit;
	}

	eError = PVRSRVDevicePostPowerStateKM(IMG_FALSE, ui32DeviceIndex, eNewPowerState);

	if (eNewPowerState == PVRSRV_DEV_POWER_STATE_DEFAULT)
	{
		PDUMPRESUME();
	}

Exit:

	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"PVRSRVSetDevicePowerStateKM : Transition to %d FAILED 0x%x", eNewPowerState, eError));
	}

	return eError;
}


/*!
******************************************************************************

 @Function	PVRSRVSystemPrePowerStateKM

 @Description	Perform processing required before a system power transition

 @Input		eNewSysPowerState :

 @Return	PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVSystemPrePowerStateKM(PVRSRV_SYS_POWER_STATE eNewSysPowerState)
{
	PVRSRV_ERROR			eError;
	SYS_DATA				*psSysData;
	PVRSRV_DEV_POWER_STATE	eNewDevicePowerState;

	SysAcquireData(&psSysData);

	/* This mutex is unlocked in PVRSRVSystemPostPowerStateKM() */
	eError = PVRSRVPowerLock(KERNEL_ID, IMG_TRUE);
	if(eError != PVRSRV_OK)
	{
		return eError;
	}

	if (_IsSystemStatePowered(eNewSysPowerState) !=
		_IsSystemStatePowered(psSysData->eCurrentPowerState))
	{
		if (_IsSystemStatePowered(eNewSysPowerState))
		{
			/* Return device back to its default state. */
			eNewDevicePowerState = PVRSRV_DEV_POWER_STATE_DEFAULT;
		}
		else
		{
			eNewDevicePowerState = PVRSRV_DEV_POWER_STATE_OFF;
		}

		/* Perform device-specific transitions. */
		eError = PVRSRVDevicePrePowerStateKM(IMG_TRUE, 0, eNewDevicePowerState);
		if (eError != PVRSRV_OK)
		{
			goto ErrorExit;
		}
	}

	if (eNewSysPowerState != psSysData->eCurrentPowerState)
	{
		/* Perform system-specific power transitions. */
		eError = SysSystemPrePowerState(eNewSysPowerState);
		if (eError != PVRSRV_OK)
		{
			goto ErrorExit;
		}
	}

	return eError;

ErrorExit:

	PVR_DPF((PVR_DBG_ERROR,
			"PVRSRVSystemPrePowerStateKM: Transition from %d to %d FAILED 0x%x",
			psSysData->eCurrentPowerState, eNewSysPowerState, eError));

	/* save the power state for the re-attempt */
	psSysData->eFailedPowerState = eNewSysPowerState;

	PVRSRVPowerUnlock(KERNEL_ID);

	return eError;
}


/*!
******************************************************************************

 @Function	PVRSRVSystemPostPowerStateKM

 @Description	Perform processing required after a system power transition

 @Input		eNewSysPowerState :

 @Return	PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVSystemPostPowerStateKM(PVRSRV_SYS_POWER_STATE eNewSysPowerState)
{
	PVRSRV_ERROR			eError = PVRSRV_OK;
	SYS_DATA				*psSysData;
	PVRSRV_DEV_POWER_STATE	eNewDevicePowerState;

	SysAcquireData(&psSysData);

	if (eNewSysPowerState != psSysData->eCurrentPowerState)
	{
		/* Perform system-specific power transitions. */
		eError = SysSystemPostPowerState(eNewSysPowerState);
		if (eError != PVRSRV_OK)
		{
			goto Exit;
		}
	}

	if (_IsSystemStatePowered(eNewSysPowerState) !=
		_IsSystemStatePowered(psSysData->eCurrentPowerState))
	{
		if (_IsSystemStatePowered(eNewSysPowerState))
		{
			/* Return device back to its default state. */
			eNewDevicePowerState = PVRSRV_DEV_POWER_STATE_DEFAULT;
		}
		else
		{
			eNewDevicePowerState = PVRSRV_DEV_POWER_STATE_OFF;
		}

		/* Perform device-specific power transitions. */
		eError = PVRSRVDevicePostPowerStateKM(IMG_TRUE, 0, eNewDevicePowerState);
		if (eError != PVRSRV_OK)
		{
			goto Exit;
		}
	}

	PVR_DPF((PVR_DBG_MESSAGE,
			"PVRSRVSystemPostPowerStateKM: System Power Transition from %d to %d OK",
			psSysData->eCurrentPowerState, eNewSysPowerState));

	psSysData->eCurrentPowerState = eNewSysPowerState;

Exit:

	PVRSRVPowerUnlock(KERNEL_ID);

	/* PRQA S 3415 2 */ /* side effects desired */
	if (_IsSystemStatePowered(eNewSysPowerState) &&
			PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_SUCCESSFUL))
	{
		/*
			Reprocess the devices' queues in case commands were blocked during
			the power transition.
		*/
		PVRSRVScheduleDeviceCallbacks();
	}

	return eError;
}


/*!
******************************************************************************

 @Function	PVRSRVSetPowerStateKM

 @Description	Set the system into a new state

 @Input		eNewPowerState :

 @Return	PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVSetPowerStateKM(PVRSRV_SYS_POWER_STATE eNewSysPowerState)
{
	PVRSRV_ERROR	eError;
	SYS_DATA		*psSysData;

	SysAcquireData(&psSysData);

	eError = PVRSRVSystemPrePowerStateKM(eNewSysPowerState);
	if(eError != PVRSRV_OK)
	{
		goto ErrorExit;
	}

	eError = PVRSRVSystemPostPowerStateKM(eNewSysPowerState);
	if(eError != PVRSRV_OK)
	{
		goto ErrorExit;
	}

	/* save new power state */
	psSysData->eFailedPowerState = PVRSRV_SYS_POWER_STATE_Unspecified;

	return PVRSRV_OK;

ErrorExit:

	PVR_DPF((PVR_DBG_ERROR,
			"PVRSRVSetPowerStateKM: Transition from %d to %d FAILED 0x%x",
			psSysData->eCurrentPowerState, eNewSysPowerState, eError));

	/* save the power state for the re-attempt */
	psSysData->eFailedPowerState = eNewSysPowerState;

	return eError;
}


/*!
******************************************************************************

 @Function	PVRSRVRegisterPowerDevice

 @Description

 Registers a device with the power manager.  Passes Pre/Post Power handlers
 and private device handle to be passed to power handlers

 @Input		ui32DeviceIndex : device index
 @Input		pfnPrePower : Pre power transition handler
 @Input		pfnPostPower : Post power transition handler
 @Input		pfnPreClockSpeedChange : Pre clock speed transition handler (if required)
 @Input		pfnPostClockSpeedChange : Post clock speed transition handler (if required)
 @Input		hDevCookie : Dev cookie for dev power handlers
 @Input		eCurrentPowerState : Current power state of the device
 @Input		eDefaultPowerState : Default power state of the device

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR PVRSRVRegisterPowerDevice(IMG_UINT32					ui32DeviceIndex,
									   PFN_PRE_POWER				pfnPrePower,
									   PFN_POST_POWER				pfnPostPower,
									   PFN_PRE_CLOCKSPEED_CHANGE	pfnPreClockSpeedChange,
									   PFN_POST_CLOCKSPEED_CHANGE	pfnPostClockSpeedChange,
									   IMG_HANDLE					hDevCookie,
									   PVRSRV_DEV_POWER_STATE		eCurrentPowerState,
									   PVRSRV_DEV_POWER_STATE		eDefaultPowerState)
{
	PVRSRV_ERROR		eError;
	SYS_DATA			*psSysData;
	PVRSRV_POWER_DEV	*psPowerDevice;

	if (pfnPrePower == IMG_NULL &&
		pfnPostPower == IMG_NULL)
	{
		return PVRSRVRemovePowerDevice(ui32DeviceIndex);
	}

	SysAcquireData(&psSysData);

	eError = OSAllocMem( PVRSRV_OS_NON_PAGEABLE_HEAP,
						 sizeof(PVRSRV_POWER_DEV),
						 (IMG_VOID **)&psPowerDevice, IMG_NULL,
						 "Power Device");
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterPowerDevice: Failed to alloc PVRSRV_POWER_DEV"));
		return eError;
	}

	/* setup device for power manager */
	psPowerDevice->pfnPrePower = pfnPrePower;
	psPowerDevice->pfnPostPower = pfnPostPower;
	psPowerDevice->pfnPreClockSpeedChange = pfnPreClockSpeedChange;
	psPowerDevice->pfnPostClockSpeedChange = pfnPostClockSpeedChange;
	psPowerDevice->hDevCookie = hDevCookie;
	psPowerDevice->ui32DeviceIndex = ui32DeviceIndex;
	psPowerDevice->eCurrentPowerState = eCurrentPowerState;
	psPowerDevice->eDefaultPowerState = eDefaultPowerState;

	/* insert into power device list */
	List_PVRSRV_POWER_DEV_Insert(&(psSysData->psPowerDeviceList), psPowerDevice);

	return (PVRSRV_OK);
}


/*!
******************************************************************************

 @Function	PVRSRVRemovePowerDevice

 @Description

 Removes device from power management register. Device is located by Device Index

 @Input		ui32DeviceIndex : device index

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR PVRSRVRemovePowerDevice (IMG_UINT32 ui32DeviceIndex)
{
	SYS_DATA			*psSysData;
	PVRSRV_POWER_DEV	*psPowerDev;

	SysAcquireData(&psSysData);

	/* find device in list and remove it */
	psPowerDev = (PVRSRV_POWER_DEV*)
					List_PVRSRV_POWER_DEV_Any_va(psSysData->psPowerDeviceList,
												 &MatchPowerDeviceIndex_AnyVaCb,
												 ui32DeviceIndex);

	if (psPowerDev)
	{
		List_PVRSRV_POWER_DEV_Remove(psPowerDev);
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_POWER_DEV), psPowerDev, IMG_NULL);
		/*not nulling pointer, copy on stack*/
	}

	return (PVRSRV_OK);
}


/*!
******************************************************************************

 @Function	PVRSRVIsDevicePowered

 @Description

	Whether the device is powered, for the purposes of lockup detection.

 @Input		ui32DeviceIndex : device index

 @Return	IMG_BOOL

******************************************************************************/
IMG_EXPORT
IMG_BOOL PVRSRVIsDevicePowered(IMG_UINT32 ui32DeviceIndex)
{
	SYS_DATA			*psSysData;
	PVRSRV_POWER_DEV	*psPowerDevice;

	SysAcquireData(&psSysData);

	/* PRQA S 3415 2 */ /* order not important */
	if (OSIsResourceLocked(&psSysData->sPowerStateChangeResource, KERNEL_ID) ||
		OSIsResourceLocked(&psSysData->sPowerStateChangeResource, ISR_ID))
	{
		return IMG_FALSE;
	}

	psPowerDevice = (PVRSRV_POWER_DEV*)
					List_PVRSRV_POWER_DEV_Any_va(psSysData->psPowerDeviceList,
												 &MatchPowerDeviceIndex_AnyVaCb,
												 ui32DeviceIndex);
	return (psPowerDevice && (psPowerDevice->eCurrentPowerState == PVRSRV_DEV_POWER_STATE_ON))
			? IMG_TRUE : IMG_FALSE;
}


/*!
******************************************************************************

 @Function	PVRSRVDevicePreClockSpeedChange

 @Description

	Notification from system layer that a device clock speed change is about to happen.

 @Input		ui32DeviceIndex : device index
 @Input		bIdleDevice : whether the device should be idled
 @Input		pvInfo

 @Return	IMG_VOID

******************************************************************************/
PVRSRV_ERROR PVRSRVDevicePreClockSpeedChange(IMG_UINT32	ui32DeviceIndex,
											 IMG_BOOL	bIdleDevice,
											 IMG_VOID	*pvInfo)
{
	PVRSRV_ERROR		eError = PVRSRV_OK;
	SYS_DATA			*psSysData;
	PVRSRV_POWER_DEV	*psPowerDevice;

	PVR_UNREFERENCED_PARAMETER(pvInfo);

	SysAcquireData(&psSysData);

	if (bIdleDevice)
	{
		/* This mutex is released in PVRSRVDevicePostClockSpeedChange. */
		eError = PVRSRVPowerLock(KERNEL_ID, IMG_FALSE);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,	"PVRSRVDevicePreClockSpeedChange : failed to acquire lock, error:0x%x", eError));
			return eError;
		}
	}

	/*search the device and then do the pre clock speed change*/
	psPowerDevice = (PVRSRV_POWER_DEV*)
					List_PVRSRV_POWER_DEV_Any_va(psSysData->psPowerDeviceList,
												 &MatchPowerDeviceIndex_AnyVaCb,
												 ui32DeviceIndex);

	if (psPowerDevice && psPowerDevice->pfnPostClockSpeedChange)
	{
			eError = psPowerDevice->pfnPreClockSpeedChange(psPowerDevice->hDevCookie,
														   bIdleDevice,
														   psPowerDevice->eCurrentPowerState);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,
						"PVRSRVDevicePreClockSpeedChange : Device %u failed, error:0x%x",
						ui32DeviceIndex, eError));
			}
	}

	if (bIdleDevice && eError != PVRSRV_OK)
	{
		PVRSRVPowerUnlock(KERNEL_ID);
	}

	return eError;
}


/*!
******************************************************************************

 @Function	PVRSRVDevicePostClockSpeedChange

 @Description

	Notification from system layer that a device clock speed change has just happened.

 @Input		ui32DeviceIndex : device index
 @Input		bIdleDevice : whether the device had been idled
 @Input		pvInfo

 @Return	IMG_VOID

******************************************************************************/
IMG_VOID PVRSRVDevicePostClockSpeedChange(IMG_UINT32	ui32DeviceIndex,
										  IMG_BOOL		bIdleDevice,
										  IMG_VOID		*pvInfo)
{
	PVRSRV_ERROR		eError;
	SYS_DATA			*psSysData;
	PVRSRV_POWER_DEV	*psPowerDevice;

	PVR_UNREFERENCED_PARAMETER(pvInfo);

	SysAcquireData(&psSysData);

	/*search the device and then do the post clock speed change*/
	psPowerDevice = (PVRSRV_POWER_DEV*)
					List_PVRSRV_POWER_DEV_Any_va(psSysData->psPowerDeviceList,
												 &MatchPowerDeviceIndex_AnyVaCb,
												 ui32DeviceIndex);

	if (psPowerDevice && psPowerDevice->pfnPostClockSpeedChange)
	{
		eError = psPowerDevice->pfnPostClockSpeedChange(psPowerDevice->hDevCookie,
														bIdleDevice,
														psPowerDevice->eCurrentPowerState);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"PVRSRVDevicePostClockSpeedChange : Device %u failed, error:0x%x",
					ui32DeviceIndex, eError));
		}
	}


	if (bIdleDevice)
	{
		/* This mutex was acquired in PVRSRVDevicePreClockSpeedChange. */
		PVRSRVPowerUnlock(KERNEL_ID);
	}
}

/******************************************************************************
 End of file (power.c)
******************************************************************************/
