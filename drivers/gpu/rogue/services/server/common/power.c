/*************************************************************************/ /*!
@File           power.c
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

#include "pdump_km.h"
#include "allocmem.h"
#include "osfunc.h"

#include "lists.h"
#include "pvrsrv.h"
#include "pvr_debug.h"
#include "process_stats.h"
#include "rk_init.h"

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
	return (eSystemPowerState == PVRSRV_SYS_POWER_STATE_ON);
}


/*!
******************************************************************************

 @Function	PVRSRVPowerLock

 @Description	Obtain the mutex for power transitions. Only allowed when
                system power is on.

 @Return	PVRSRV_ERROR_RETRY or PVRSRV_OK

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVPowerLock()
{
	PVRSRV_ERROR	eError;
	PVRSRV_DATA		*psPVRSRVData = PVRSRVGetPVRSRVData();

	/* Only allow to take powerlock when the system power is on */
	if (_IsSystemStatePowered(psPVRSRVData->eCurrentPowerState))
	{
		OSLockAcquire(psPVRSRVData->hPowerLock);
		eError = PVRSRV_OK;
	}
	else
	{
		eError = PVRSRV_ERROR_RETRY;
	}

	return eError;
}

/*!
******************************************************************************

 @Function	PVRSRVForcedPowerLock

 @Description	Obtain the mutex for power transitions regardless of
                system power state

 @Return	PVRSRV_ERROR_RETRY or PVRSRV_OK

******************************************************************************/
IMG_EXPORT
IMG_VOID PVRSRVForcedPowerLock()
{
	PVRSRV_DATA		*psPVRSRVData = PVRSRVGetPVRSRVData();
	OSLockAcquire(psPVRSRVData->hPowerLock);
}


/*!
******************************************************************************

 @Function	PVRSRVPowerUnlock

 @Description	Release the mutex for power transitions

 @Return	PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
IMG_VOID PVRSRVPowerUnlock()
{
	PVRSRV_DATA	*psPVRSRVData = PVRSRVGetPVRSRVData();

	OSLockRelease(psPVRSRVData->hPowerLock);
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
	IMG_BOOL				bForced;
	IMG_UINT64				ui32SysTimer1=0, ui32SysTimer2=0, ui32DevTimer1=0, ui32DevTimer2=0;

	/*WARNING! if types were not aligned to 4 bytes, this could be dangerous!!!*/
	bAllDevices = va_arg(va, IMG_BOOL);
	ui32DeviceIndex = va_arg(va, IMG_UINT32);
	eNewPowerState = va_arg(va, PVRSRV_DEV_POWER_STATE);
	bForced = va_arg(va, IMG_BOOL);

	if (bAllDevices || (ui32DeviceIndex == psPowerDevice->ui32DeviceIndex))
	{
		eNewDevicePowerState = (eNewPowerState == PVRSRV_DEV_POWER_STATE_DEFAULT) ?
							psPowerDevice->eDefaultPowerState : eNewPowerState;

		if (psPowerDevice->eCurrentPowerState != eNewDevicePowerState)
		{
			if (psPowerDevice->pfnDevicePrePower != IMG_NULL)
			{
				/* Call the device's power callback. */

				ui32DevTimer1=OSClockns64();

				eError = psPowerDevice->pfnDevicePrePower(psPowerDevice->hDevCookie,
															eNewDevicePowerState,
															psPowerDevice->eCurrentPowerState,
															bForced);

				ui32DevTimer2=OSClockns64();

				if (eError != PVRSRV_OK)
				{
					return eError;
				}
			}

			/* Do any required system-layer processing. */
			if (psPowerDevice->pfnSystemPrePower != IMG_NULL)
			{

				ui32SysTimer1=OSClockus();

				eError = psPowerDevice->pfnSystemPrePower(eNewDevicePowerState,
														  psPowerDevice->eCurrentPowerState,
														  bForced);

				ui32SysTimer2=OSClockus();

				if (eError != PVRSRV_OK)
				{
					return eError;
				}
			}
		}
	}


    InsertPowerTimeStatistic(PVRSRV_POWER_ENTRY_TYPE_PRE,
			psPowerDevice->eCurrentPowerState, eNewPowerState,
            ui32SysTimer1,ui32SysTimer2,
			ui32DevTimer1,ui32DevTimer2,
			bForced);


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
 @Input		bForced : TRUE if the transition should not fail (e.g. OS request)

 @Return	PVRSRV_ERROR

******************************************************************************/
static
PVRSRV_ERROR PVRSRVDevicePrePowerStateKM(IMG_BOOL				bAllDevices,
										 IMG_UINT32				ui32DeviceIndex,
										 PVRSRV_DEV_POWER_STATE	eNewPowerState,
										 IMG_BOOL				bForced)
{
	PVRSRV_ERROR		eError;
	PVRSRV_DATA			*psPVRSRVData = PVRSRVGetPVRSRVData();

	/* Loop through the power devices. */
	eError = List_PVRSRV_POWER_DEV_PVRSRV_ERROR_Any_va(psPVRSRVData->psPowerDeviceList,
														&PVRSRVDevicePrePowerStateKM_AnyVaCb,
														bAllDevices,
														ui32DeviceIndex,
														eNewPowerState,
														bForced);

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
	IMG_BOOL				bForced;
	IMG_UINT64				ui32SysTimer1=0, ui32SysTimer2=0, ui32DevTimer1=0, ui32DevTimer2=0;

	/*WARNING! if types were not aligned to 4 bytes, this could be dangerous!!!*/
	bAllDevices = va_arg(va, IMG_BOOL);
	ui32DeviceIndex = va_arg(va, IMG_UINT32);
	eNewPowerState = va_arg(va, PVRSRV_DEV_POWER_STATE);
	bForced = va_arg(va, IMG_BOOL);

	if (bAllDevices || (ui32DeviceIndex == psPowerDevice->ui32DeviceIndex))
	{
		eNewDevicePowerState = (eNewPowerState == PVRSRV_DEV_POWER_STATE_DEFAULT) ?
								psPowerDevice->eDefaultPowerState : eNewPowerState;

		if (psPowerDevice->eCurrentPowerState != eNewDevicePowerState)
		{
			/* Do any required system-layer processing. */
			if (psPowerDevice->pfnSystemPostPower != IMG_NULL)
			{

				ui32SysTimer1=OSClockns64();

				eError = psPowerDevice->pfnSystemPostPower(eNewDevicePowerState,
														   psPowerDevice->eCurrentPowerState,
														   bForced);

				ui32SysTimer2=OSClockns64();

				if (eError != PVRSRV_OK)
				{
					return eError;
				}
			}

			if (psPowerDevice->pfnDevicePostPower != IMG_NULL)
			{
				/* Call the device's power callback. */

				ui32DevTimer1=OSClockus();

				eError = psPowerDevice->pfnDevicePostPower(psPowerDevice->hDevCookie,
														   eNewDevicePowerState,
														   psPowerDevice->eCurrentPowerState,
														   bForced);

				ui32DevTimer2=OSClockus();

				if (eError != PVRSRV_OK)
				{
					return eError;
				}
			}

			psPowerDevice->eCurrentPowerState = eNewDevicePowerState;
		}
	}


    InsertPowerTimeStatistic(PVRSRV_POWER_ENTRY_TYPE_POST,
							psPowerDevice->eCurrentPowerState, eNewPowerState,
                            ui32SysTimer1,ui32SysTimer2,
							ui32DevTimer1,ui32DevTimer2,
							bForced);


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
										  PVRSRV_DEV_POWER_STATE	eNewPowerState,
										  IMG_BOOL					bForced)
{
	PVRSRV_ERROR		eError;
	PVRSRV_DATA			*psPVRSRVData = PVRSRVGetPVRSRVData();

	/* Loop through the power devices. */
	eError = List_PVRSRV_POWER_DEV_PVRSRV_ERROR_Any_va(psPVRSRVData->psPowerDeviceList,
														&PVRSRVDevicePostPowerStateKM_AnyVaCb,
														bAllDevices,
														ui32DeviceIndex,
														eNewPowerState,
														bForced);

	return eError;
}


/*!
******************************************************************************

 @Function	PVRSRVSetDevicePowerStateKM

 @Description	Set the Device into a new state

 @Input		ui32DeviceIndex : device index
 @Input		eNewPowerState : New power state
 @Input		bForced : TRUE if the transition should not fail (e.g. OS request)

 @Return	PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVSetDevicePowerStateKM(IMG_UINT32				ui32DeviceIndex,
										 PVRSRV_DEV_POWER_STATE	eNewPowerState,
										 IMG_BOOL				bForced)
{
	PVRSRV_ERROR	eError;
	PVRSRV_DATA*    psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_DEV_POWER_STATE eOldPowerState;

	eError = PVRSRVGetDevicePowerState(ui32DeviceIndex, &eOldPowerState);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_WARNING, "PVRSRVSetDevicePowerStateKM: Couldn't read power state."));
		eOldPowerState = PVRSRV_DEV_POWER_STATE_DEFAULT;
	}

	eError = PVRSRVDevicePrePowerStateKM(IMG_FALSE, ui32DeviceIndex, eNewPowerState, bForced);
	if(eError != PVRSRV_OK)
	{
		goto Exit;
	}

	eError = PVRSRVDevicePostPowerStateKM(IMG_FALSE, ui32DeviceIndex, eNewPowerState, bForced);

	/* Signal Device Watchdog Thread about power mode change. */
	if (eOldPowerState != eNewPowerState && eNewPowerState == PVRSRV_DEV_POWER_STATE_ON)
	{
		psPVRSRVData->ui32DevicesWatchdogPwrTrans++;

		if (psPVRSRVData->ui32DevicesWatchdogTimeout == DEVICES_WATCHDOG_POWER_OFF_SLEEP_TIMEOUT)
		{
			if (psPVRSRVData->hDevicesWatchdogEvObj)
			{
				eError = OSEventObjectSignal(psPVRSRVData->hDevicesWatchdogEvObj);
				PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");
			}
		}
	}

Exit:

	if (eError == PVRSRV_ERROR_DEVICE_POWER_CHANGE_DENIED)
	{
		PVR_DPF((PVR_DBG_MESSAGE,
				"PVRSRVSetDevicePowerStateKM : Transition to %d was denied, Forced=%d", eNewPowerState, bForced));
	}
	else if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_WARNING,
				"PVRSRVSetDevicePowerStateKM : Transition to %d FAILED (%s)", eNewPowerState, PVRSRVGetErrorStringKM(eError)));
	}
	
	return eError;
}

/*!
******************************************************************************

 @Function	PVRSRVSetPowerStateKM

 @Description	Set the system into a new state

 @Input		eNewPowerState :
 @Input		bForced : TRUE if the transition should not fail (e.g. OS request)

 @Return	PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVSetPowerStateKM(PVRSRV_SYS_POWER_STATE eNewSysPowerState, IMG_BOOL bForced)
{
	PVRSRV_ERROR	eError;
	PVRSRV_DATA		*psPVRSRVData = PVRSRVGetPVRSRVData();
	IMG_UINT        uiStage = 0;

	PVRSRV_DEV_POWER_STATE eNewDevicePowerState = 
	  _IsSystemStatePowered(eNewSysPowerState)? PVRSRV_DEV_POWER_STATE_DEFAULT : PVRSRV_DEV_POWER_STATE_OFF;

	/* require a proper power state */
	if (eNewSysPowerState == PVRSRV_SYS_POWER_STATE_Unspecified)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* no power transition requested, so do nothing */
	if (eNewSysPowerState == psPVRSRVData->eCurrentPowerState)
	{
		return PVRSRV_OK;
	}

	/* Prevent simultaneous SetPowerStateKM calls */
	PVRSRVForcedPowerLock();

	/* Perform pre transitions: first device and then sys layer */
	eError = PVRSRVDevicePrePowerStateKM(IMG_TRUE, 0, eNewDevicePowerState, bForced);
	if (eError != PVRSRV_OK)
	{
		uiStage++;
		goto ErrorExit;
	}
	eError = PVRSRVSysPrePowerState(eNewSysPowerState, bForced);
	if (eError != PVRSRV_OK)
	{
		uiStage++;
		goto ErrorExit;
	}

	/* Perform system-specific post power transitions: first sys layer and then device */
	eError = PVRSRVSysPostPowerState(eNewSysPowerState, bForced);
	if (eError != PVRSRV_OK)
	{
		uiStage++;
		goto ErrorExit;
	}
	eError = PVRSRVDevicePostPowerStateKM(IMG_TRUE, 0, eNewDevicePowerState, bForced);
	if (eError != PVRSRV_OK)
	{
		uiStage++;
		goto ErrorExit;
	}

	psPVRSRVData->eCurrentPowerState = eNewSysPowerState;
	psPVRSRVData->eFailedPowerState = PVRSRV_SYS_POWER_STATE_Unspecified;

	PVRSRVPowerUnlock();

	/*
		Reprocess the devices' queues in case commands were blocked during
		the power transition.
	*/
	if (_IsSystemStatePowered(eNewSysPowerState) &&
			PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_SUCCESSFUL))
	{
		PVRSRVCheckStatus(IMG_NULL);
	}

	return PVRSRV_OK;

ErrorExit:
	/* save the power state for the re-attempt */
	psPVRSRVData->eFailedPowerState = eNewSysPowerState;

	PVRSRVPowerUnlock();

	PVR_DPF((PVR_DBG_ERROR,
			"PVRSRVSetPowerStateKM: Transition from %d to %d FAILED (%s) at stage %d, forced: %d. Dumping debug info.",
			psPVRSRVData->eCurrentPowerState, eNewSysPowerState, PVRSRVGetErrorStringKM(eError), uiStage, bForced));

	PVRSRVDebugRequest(DEBUG_REQUEST_VERBOSITY_MAX, IMG_NULL);

	return eError;
}


PVRSRV_ERROR PVRSRVRegisterPowerDevice(IMG_UINT32					ui32DeviceIndex,
									   PFN_PRE_POWER				pfnDevicePrePower,
									   PFN_POST_POWER				pfnDevicePostPower,
									   PFN_SYS_DEV_PRE_POWER		pfnSystemPrePower,
									   PFN_SYS_DEV_POST_POWER		pfnSystemPostPower,
									   PFN_PRE_CLOCKSPEED_CHANGE	pfnPreClockSpeedChange,
									   PFN_POST_CLOCKSPEED_CHANGE	pfnPostClockSpeedChange,
									   IMG_HANDLE					hDevCookie,
									   PVRSRV_DEV_POWER_STATE		eCurrentPowerState,
									   PVRSRV_DEV_POWER_STATE		eDefaultPowerState)
{
	PVRSRV_DATA			*psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_POWER_DEV	*psPowerDevice;

	if (pfnDevicePrePower == IMG_NULL &&
		pfnDevicePostPower == IMG_NULL)
	{
		return PVRSRVRemovePowerDevice(ui32DeviceIndex);
	}

	psPowerDevice = OSAllocMem(sizeof(PVRSRV_POWER_DEV));
	if (psPowerDevice == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterPowerDevice: Failed to alloc PVRSRV_POWER_DEV"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/* setup device for power manager */
	psPowerDevice->pfnDevicePrePower = pfnDevicePrePower;
	psPowerDevice->pfnDevicePostPower = pfnDevicePostPower;
	psPowerDevice->pfnSystemPrePower = pfnSystemPrePower;
	psPowerDevice->pfnSystemPostPower = pfnSystemPostPower;
	psPowerDevice->pfnPreClockSpeedChange = pfnPreClockSpeedChange;
	psPowerDevice->pfnPostClockSpeedChange = pfnPostClockSpeedChange;
	psPowerDevice->hDevCookie = hDevCookie;
	psPowerDevice->ui32DeviceIndex = ui32DeviceIndex;
	psPowerDevice->eCurrentPowerState = eCurrentPowerState;
	psPowerDevice->eDefaultPowerState = eDefaultPowerState;

	/* insert into power device list */
	List_PVRSRV_POWER_DEV_Insert(&(psPVRSRVData->psPowerDeviceList), psPowerDevice);

#if RK33_DVFS_SUPPORT && RK33_USE_RGX_GET_GPU_UTIL
    //zxl:set device node to get pfnGetGpuUtilStats in rk_init.c
    rk33_set_device_node(hDevCookie);
#endif
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
	PVRSRV_DATA			*psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_POWER_DEV	*psPowerDev;

	/* find device in list and remove it */
	psPowerDev = (PVRSRV_POWER_DEV*)
					List_PVRSRV_POWER_DEV_Any_va(psPVRSRVData->psPowerDeviceList,
												 &MatchPowerDeviceIndex_AnyVaCb,
												 ui32DeviceIndex);

	if (psPowerDev)
	{
		List_PVRSRV_POWER_DEV_Remove(psPowerDev);
		OSFreeMem(psPowerDev);
		/*not nulling pointer, copy on stack*/
	}

#if RK33_DVFS_SUPPORT && RK33_USE_RGX_GET_GPU_UTIL
    //zxl:clear device node
    rk33_clear_device_node();
#endif

	return (PVRSRV_OK);
}


/*!
******************************************************************************

 @Function	PVRSRVGetDevicePowerState

 @Description

	Return the device power state

 @Input		ui32DeviceIndex : device index
 @Output	psPowerState : Current power state 

 @Return	PVRSRV_ERROR_UNKNOWN_POWER_STATE if device could not be found. PVRSRV_OK otherwise.

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVGetDevicePowerState(IMG_UINT32 ui32DeviceIndex, PPVRSRV_DEV_POWER_STATE pePowerState)
{
	PVRSRV_DATA			*psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_POWER_DEV	*psPowerDevice;

	psPowerDevice = (PVRSRV_POWER_DEV*)
					List_PVRSRV_POWER_DEV_Any_va(psPVRSRVData->psPowerDeviceList,
												 &MatchPowerDeviceIndex_AnyVaCb,
												 ui32DeviceIndex);
	if (psPowerDevice == IMG_NULL)
	{
		return PVRSRV_ERROR_UNKNOWN_POWER_STATE;
	}

	*pePowerState = psPowerDevice->eCurrentPowerState;

	return PVRSRV_OK;
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
	PVRSRV_DATA            *psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_DEV_POWER_STATE ePowerState;

	if (OSLockIsLocked(psPVRSRVData->hPowerLock))
	{
		return IMG_FALSE;
	}

	if (PVRSRVGetDevicePowerState(ui32DeviceIndex, &ePowerState) != PVRSRV_OK)
	{
		return IMG_FALSE;
	}

	return (ePowerState == PVRSRV_DEV_POWER_STATE_ON);
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
	PVRSRV_DATA			*psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_POWER_DEV	*psPowerDevice;

	PVR_UNREFERENCED_PARAMETER(pvInfo);

	/*search the device and then do the pre clock speed change*/
	psPowerDevice = (PVRSRV_POWER_DEV*)
					List_PVRSRV_POWER_DEV_Any_va(psPVRSRVData->psPowerDeviceList,
												 &MatchPowerDeviceIndex_AnyVaCb,
												 ui32DeviceIndex);

	do
	{
		/* This mutex is released in PVRSRVDevicePostClockSpeedChange. */
		eError = PVRSRVPowerLock();
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,	"PVRSRVDevicePreClockSpeedChange : failed to acquire lock, error:0x%x", eError));
			return eError;
		}

		if (psPowerDevice && psPowerDevice->pfnPostClockSpeedChange)
		{
			eError = psPowerDevice->pfnPreClockSpeedChange(psPowerDevice->hDevCookie,
														   bIdleDevice,
														   psPowerDevice->eCurrentPowerState);
			
			if ((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_DEVICE_POWER_CHANGE_DENIED))
			{
				PVR_DPF((PVR_DBG_ERROR,
						"PVRSRVDevicePreClockSpeedChange : Device %u failed, error:0x%x",
						ui32DeviceIndex, eError));
			}
			else if (eError == PVRSRV_ERROR_DEVICE_POWER_CHANGE_DENIED)
			{
				PVR_DPF((PVR_DBG_MESSAGE,
						"PVRSRVDevicePreClockSpeedChange : Device %u denied transition to IDLE",
						ui32DeviceIndex));
		
				PVRSRVPowerUnlock();
				OSSleepms(1);
			}
		}
		else
		{
			/* In the case of no pfnPostClockSpeedChange call being made, eError remains PVRSRV_OK... */
		}
	}
	while (eError == PVRSRV_ERROR_DEVICE_POWER_CHANGE_DENIED);

	if (eError != PVRSRV_OK)
	{
		PVRSRVPowerUnlock();
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
	PVRSRV_DATA			*psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_POWER_DEV	*psPowerDevice;

    PVR_UNREFERENCED_PARAMETER(pvInfo);

	/*search the device and then do the post clock speed change*/
	psPowerDevice = (PVRSRV_POWER_DEV*)
					List_PVRSRV_POWER_DEV_Any_va(psPVRSRVData->psPowerDeviceList,
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


	/* This mutex was acquired in PVRSRVDevicePreClockSpeedChange. */
	PVRSRVPowerUnlock();
}

/******************************************************************************
 End of file (power.c)
******************************************************************************/
