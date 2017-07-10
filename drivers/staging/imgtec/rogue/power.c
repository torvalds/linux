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
#include "rk_init.h"

#include "lists.h"
#include "pvrsrv.h"
#include "pvr_debug.h"
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#include "process_stats.h"
#endif

struct _PVRSRV_POWER_DEV_TAG_
{
	PFN_PRE_POWER					pfnDevicePrePower;
	PFN_POST_POWER					pfnDevicePostPower;
	PFN_SYS_DEV_PRE_POWER			pfnSystemPrePower;
	PFN_SYS_DEV_POST_POWER			pfnSystemPostPower;
	PFN_PRE_CLOCKSPEED_CHANGE		pfnPreClockSpeedChange;
	PFN_POST_CLOCKSPEED_CHANGE		pfnPostClockSpeedChange;
	PFN_FORCED_IDLE_REQUEST			pfnForcedIdleRequest;
	PFN_FORCED_IDLE_CANCEL_REQUEST	pfnForcedIdleCancelRequest;
	PFN_DUST_COUNT_REQUEST			pfnDustCountRequest;
	IMG_HANDLE						hSysData;
	IMG_HANDLE						hDevCookie;
	PVRSRV_DEV_POWER_STATE 			eDefaultPowerState;
	PVRSRV_DEV_POWER_STATE 			eCurrentPowerState;
};

#if !defined(SUPPORT_KERNEL_SRVINIT)
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
					 "%s: Unknown state %x", __func__, eInitServerState));
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
					 "%s: Unknown state %x", __func__, eInitServerState));
			bReturnVal = IMG_FALSE;
	}

	return bReturnVal;
}
#endif /* !defined(SUPPORT_KERNEL_SRVINIT) */

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

 @Function	IsSystemStatePowered

 @Description	Tests whether a given system state represents powered-up.

 @Return	IMG_BOOL

******************************************************************************/
IMG_EXPORT
IMG_BOOL IsSystemStatePowered(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	OSLockAcquire(psDeviceNode->hPowerLock);
	if (_IsSystemStatePowered(psDeviceNode->eCurrentSysPowerState))
	{
		OSLockRelease(psDeviceNode->hPowerLock);
		return IMG_TRUE;
	}
	OSLockRelease(psDeviceNode->hPowerLock);
	return IMG_FALSE;
}


/*!
******************************************************************************

 @Function	PVRSRVPowerLock

 @Description	Obtain the mutex for power transitions. Only allowed when
                system power is on.

 @Return	PVRSRV_ERROR_RETRY or PVRSRV_OK

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVPowerLock(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	OSLockAcquire(psDeviceNode->hPowerLock);

	/* Only allow to take powerlock when the system power is on */
	if (_IsSystemStatePowered(psDeviceNode->eCurrentSysPowerState))
	{
		return PVRSRV_OK;
	}

	OSLockRelease(psDeviceNode->hPowerLock);

	return PVRSRV_ERROR_RETRY;
}

/*!
******************************************************************************

 @Function	PVRSRVForcedPowerLock

 @Description	Obtain the mutex for power transitions regardless of
                system power state

 @Return	PVRSRV_ERROR_RETRY or PVRSRV_OK

******************************************************************************/
IMG_EXPORT
void PVRSRVForcedPowerLock(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	OSLockAcquire(psDeviceNode->hPowerLock);
}


/*!
******************************************************************************

 @Function	PVRSRVPowerUnlock

 @Description	Release the mutex for power transitions

 @Return	PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
void PVRSRVPowerUnlock(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	OSLockRelease(psDeviceNode->hPowerLock);
}
IMG_EXPORT
IMG_BOOL PVRSRVDeviceIsDefaultStateOFF(PVRSRV_POWER_DEV *psPowerDevice)
{
	return (psPowerDevice->eDefaultPowerState == PVRSRV_DEV_POWER_STATE_OFF);
}

/*!
******************************************************************************

 @Function      PVRSRVSetDeviceDefaultPowerState

 @Description   Set the default device power state to eNewPowerState

 @Input		    psDeviceNode : Device node
 @Input         eNewPowerState : New power state

 @Return        PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR PVRSRVSetDeviceDefaultPowerState(const PVRSRV_DEVICE_NODE *psDeviceNode,
					PVRSRV_DEV_POWER_STATE eNewPowerState)
{
	PVRSRV_POWER_DEV *psPowerDevice;

	psPowerDevice = psDeviceNode->psPowerDev;
	if (psPowerDevice == NULL)
	{
		return PVRSRV_ERROR_INVALID_DEVICE;
	}

	psPowerDevice->eDefaultPowerState = eNewPowerState;

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVDeviceIdleRequestKM

 @Description

 Perform device-specific processing required to force the device idle.

 @Input		psDeviceNode : Device node
 @Input		pfnCheckIdleReq : Filter function used to determine whether a forced idle is required for the device
 @Input		bDeviceOffPermitted :	IMG_TRUE if the transition should not fail if device off
					IMG_FALSE if the transition should fail if device off

 @Return	PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVDeviceIdleRequestKM(PVRSRV_DEVICE_NODE *psDeviceNode,
					PFN_SYS_DEV_IS_DEFAULT_STATE_OFF	pfnIsDefaultStateOff,
					IMG_BOOL				bDeviceOffPermitted)
{
	PVRSRV_POWER_DEV *psPowerDev = psDeviceNode->psPowerDev;

	if (psPowerDev && psPowerDev->pfnForcedIdleRequest)
	{
		if (!pfnIsDefaultStateOff || pfnIsDefaultStateOff(psPowerDev))
		{
			return psPowerDev->pfnForcedIdleRequest(psPowerDev->hDevCookie,
													bDeviceOffPermitted);
		}
	}

	return PVRSRV_OK;
}
/*!
******************************************************************************

 @Function	PVRSRVDeviceIdleCancelRequestKM

 @Description

 Perform device-specific processing required to cancel the forced idle state on the device, returning to normal operation.

 @Input		psDeviceNode : Device node

 @Return	PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVDeviceIdleCancelRequestKM(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_POWER_DEV *psPowerDev = psDeviceNode->psPowerDev;

	if (psPowerDev && psPowerDev->pfnForcedIdleCancelRequest)
	{
		return psPowerDev->pfnForcedIdleCancelRequest(psPowerDev->hDevCookie);
	}

	return PVRSRV_OK;
}
/*!
******************************************************************************

 @Function	PVRSRVDevicePrePowerStateKM

 @Description

 Perform device-specific processing required before a power transition

 @Input		psPowerDevice : Power device
 @Input		eNewPowerState : New power state
 @Input		bForced : TRUE if the transition should not fail (e.g. OS request)

 @Return	PVRSRV_ERROR

******************************************************************************/
static
PVRSRV_ERROR PVRSRVDevicePrePowerStateKM(PVRSRV_POWER_DEV		*psPowerDevice,
										 PVRSRV_DEV_POWER_STATE	eNewPowerState,
										 IMG_BOOL				bForced)
{
	IMG_UINT64 ui64SysTimer1 = 0;
	IMG_UINT64 ui64SysTimer2 = 0;
	IMG_UINT64 ui64DevTimer1 = 0;
	IMG_UINT64 ui64DevTimer2 = 0;
	PVRSRV_ERROR eError;

	PVR_ASSERT(eNewPowerState != PVRSRV_DEV_POWER_STATE_DEFAULT);

	if (psPowerDevice->pfnDevicePrePower != NULL)
	{
		/* Call the device's power callback. */
		ui64DevTimer1 = OSClockns64();
		eError = psPowerDevice->pfnDevicePrePower(psPowerDevice->hDevCookie,
												  eNewPowerState,
												  psPowerDevice->eCurrentPowerState,
												  bForced);
		ui64DevTimer2 = OSClockns64();

		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}

	/* Do any required system-layer processing. */
	if (psPowerDevice->pfnSystemPrePower != NULL)
	{
		ui64SysTimer1 = OSClockns64();
		eError = psPowerDevice->pfnSystemPrePower(psPowerDevice->hSysData,
												  eNewPowerState,
												  psPowerDevice->eCurrentPowerState,
												  bForced);
		ui64SysTimer2 = OSClockns64();

		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	InsertPowerTimeStatistic(ui64SysTimer1, ui64SysTimer2,
							 ui64DevTimer1, ui64DevTimer2,
							 bForced,
							 eNewPowerState == PVRSRV_DEV_POWER_STATE_ON,
							 IMG_TRUE);
#endif

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVDevicePostPowerStateKM

 @Description

 Perform device-specific processing required after a power transition

 @Input		psPowerDevice : Power device
 @Input		eNewPowerState : New power state
 @Input		bForced : TRUE if the transition should not fail (e.g. OS request)

 @Return	PVRSRV_ERROR

******************************************************************************/
static
PVRSRV_ERROR PVRSRVDevicePostPowerStateKM(PVRSRV_POWER_DEV			*psPowerDevice,
										  PVRSRV_DEV_POWER_STATE	eNewPowerState,
										  IMG_BOOL					bForced)
{
	IMG_UINT64 ui64SysTimer1 = 0;
	IMG_UINT64 ui64SysTimer2 = 0;
	IMG_UINT64 ui64DevTimer1 = 0;
	IMG_UINT64 ui64DevTimer2 = 0;
	PVRSRV_ERROR eError;

	PVR_ASSERT(eNewPowerState != PVRSRV_DEV_POWER_STATE_DEFAULT);

	/* Do any required system-layer processing. */
	if (psPowerDevice->pfnSystemPostPower != NULL)
	{
		ui64SysTimer1 = OSClockns64();
		eError = psPowerDevice->pfnSystemPostPower(psPowerDevice->hSysData,
												   eNewPowerState,
												   psPowerDevice->eCurrentPowerState,
												   bForced);
		ui64SysTimer2 = OSClockns64();

		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}

	if (psPowerDevice->pfnDevicePostPower != NULL)
	{
		/* Call the device's power callback. */
		ui64DevTimer1 = OSClockns64();
		eError = psPowerDevice->pfnDevicePostPower(psPowerDevice->hDevCookie,
												   eNewPowerState,
												   psPowerDevice->eCurrentPowerState,
												   bForced);
		ui64DevTimer2 = OSClockns64();

		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	InsertPowerTimeStatistic(ui64SysTimer1, ui64SysTimer2,
							 ui64DevTimer1, ui64DevTimer2,
							 bForced,
							 eNewPowerState == PVRSRV_DEV_POWER_STATE_ON,
							 IMG_FALSE);
#endif
	psPowerDevice->eCurrentPowerState = eNewPowerState;

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVSetDevicePowerStateKM

 @Description	Set the Device into a new state

 @Input		psDeviceNode : Device node
 @Input		eNewPowerState : New power state
 @Input		bForced : TRUE if the transition should not fail (e.g. OS request)

 @Return	PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVSetDevicePowerStateKM(PVRSRV_DEVICE_NODE		*psDeviceNode,
										 PVRSRV_DEV_POWER_STATE	eNewPowerState,
										 IMG_BOOL				bForced)
{
	PVRSRV_ERROR	eError;
	PVRSRV_DATA*    psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_POWER_DEV *psPowerDevice;

	psPowerDevice = psDeviceNode->psPowerDev;
	if (!psPowerDevice)
	{
		return PVRSRV_OK;
	}

	if (eNewPowerState == PVRSRV_DEV_POWER_STATE_DEFAULT)
	{
		eNewPowerState = psPowerDevice->eDefaultPowerState;
	}

	if (psPowerDevice->eCurrentPowerState != eNewPowerState)
	{
		eError = PVRSRVDevicePrePowerStateKM(psPowerDevice,
											 eNewPowerState,
											 bForced);
		if (eError != PVRSRV_OK)
		{
			goto ErrorExit;
		}

		eError = PVRSRVDevicePostPowerStateKM(psPowerDevice,
											  eNewPowerState,
											  bForced);
		if (eError != PVRSRV_OK)
		{
			goto ErrorExit;
		}

		/* Signal Device Watchdog Thread about power mode change. */
		if (eNewPowerState == PVRSRV_DEV_POWER_STATE_ON)
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
	}

	return PVRSRV_OK;

ErrorExit:

	if (eError == PVRSRV_ERROR_DEVICE_POWER_CHANGE_DENIED)
	{
		PVR_DPF((PVR_DBG_MESSAGE,
				 "%s: Transition to %d was denied, Forced=%d",
				 __func__, eNewPowerState, bForced));
	}
	else if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_WARNING,
				 "%s: Transition to %d FAILED (%s)",
				 __func__, eNewPowerState, PVRSRVGetErrorStringKM(eError)));
	}
	
	return eError;
}

/*************************************************************************/ /*!
@Function     PVRSRVSetDeviceSystemPowerState
@Description  Set the device into a new power state based on the systems power
              state
@Input        psDeviceNode          Device node
@Input        eNewSysPowerState  New system power state
@Return       PVRSRV_ERROR       PVRSRV_OK on success or an error otherwise
*/ /**************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVSetDeviceSystemPowerState(PVRSRV_DEVICE_NODE *psDeviceNode,
											 PVRSRV_SYS_POWER_STATE eNewSysPowerState)
{
	PVRSRV_ERROR	eError;
	IMG_UINT        uiStage = 0;

	PVRSRV_DEV_POWER_STATE eNewDevicePowerState = 
	  _IsSystemStatePowered(eNewSysPowerState)? PVRSRV_DEV_POWER_STATE_DEFAULT : PVRSRV_DEV_POWER_STATE_OFF;

	/* require a proper power state */
	if (eNewSysPowerState == PVRSRV_SYS_POWER_STATE_Unspecified)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Prevent simultaneous SetPowerStateKM calls */
	PVRSRVForcedPowerLock(psDeviceNode);

	/* no power transition requested, so do nothing */
	if (eNewSysPowerState == psDeviceNode->eCurrentSysPowerState)
	{
		PVRSRVPowerUnlock(psDeviceNode);
		return PVRSRV_OK;
	}

	if ((eNewDevicePowerState == PVRSRV_DEV_POWER_STATE_OFF) || 
		(eNewDevicePowerState == PVRSRV_DEV_POWER_STATE_DEFAULT))
	{
		/* If setting devices to default state, selectively force idle all devices whose default state is off */
		 PFN_SYS_DEV_IS_DEFAULT_STATE_OFF pfnIsDefaultStateOff =
			(eNewDevicePowerState == PVRSRV_DEV_POWER_STATE_DEFAULT) ? PVRSRVDeviceIsDefaultStateOFF : NULL;

		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError = PVRSRVDeviceIdleRequestKM(psDeviceNode,
											   pfnIsDefaultStateOff, IMG_TRUE);

			if (eError == PVRSRV_OK)
			{
				break;
			}
			else if (eError == PVRSRV_ERROR_DEVICE_IDLE_REQUEST_DENIED)
			{
				PVRSRVPowerUnlock(psDeviceNode);
				OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
				PVRSRVForcedPowerLock(psDeviceNode);
			}
			else
			{
				uiStage++;
				goto ErrorExit;
			}
		} END_LOOP_UNTIL_TIMEOUT();
	}

	eError = PVRSRVSetDevicePowerStateKM(psDeviceNode, eNewDevicePowerState,
										 IMG_TRUE);
	if (eError != PVRSRV_OK)
	{
		uiStage++;
		goto ErrorExit;
	}

	psDeviceNode->eCurrentSysPowerState = eNewSysPowerState;

	PVRSRVPowerUnlock(psDeviceNode);

	return PVRSRV_OK;

ErrorExit:
	PVRSRVPowerUnlock(psDeviceNode);

	PVR_DPF((PVR_DBG_ERROR,
			 "%s: Transition from %d to %d FAILED (%s) at stage %d. Dumping debug info.",
			 __func__, psDeviceNode->eCurrentSysPowerState, eNewSysPowerState,
			 PVRSRVGetErrorStringKM(eError), uiStage));

	PVRSRVDebugRequest(psDeviceNode, DEBUG_REQUEST_VERBOSITY_MAX, NULL, NULL);

	return eError;
}


PVRSRV_ERROR PVRSRVRegisterPowerDevice(PVRSRV_DEVICE_NODE			*psDeviceNode,
									   PFN_PRE_POWER				pfnDevicePrePower,
									   PFN_POST_POWER				pfnDevicePostPower,
									   PFN_SYS_DEV_PRE_POWER		pfnSystemPrePower,
									   PFN_SYS_DEV_POST_POWER		pfnSystemPostPower,
									   PFN_PRE_CLOCKSPEED_CHANGE	pfnPreClockSpeedChange,
									   PFN_POST_CLOCKSPEED_CHANGE	pfnPostClockSpeedChange,
									   PFN_FORCED_IDLE_REQUEST	pfnForcedIdleRequest,
									   PFN_FORCED_IDLE_CANCEL_REQUEST	pfnForcedIdleCancelRequest,
									   PFN_DUST_COUNT_REQUEST	pfnDustCountRequest,
									   IMG_HANDLE					hDevCookie,
									   PVRSRV_DEV_POWER_STATE		eCurrentPowerState,
									   PVRSRV_DEV_POWER_STATE		eDefaultPowerState)
{
	PVRSRV_POWER_DEV *psPowerDevice;

	PVR_ASSERT(!psDeviceNode->psPowerDev);

	PVR_ASSERT(eCurrentPowerState != PVRSRV_DEV_POWER_STATE_DEFAULT);
	PVR_ASSERT(eDefaultPowerState != PVRSRV_DEV_POWER_STATE_DEFAULT);

	psPowerDevice = OSAllocMem(sizeof(PVRSRV_POWER_DEV));
	if (psPowerDevice == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: Failed to alloc PVRSRV_POWER_DEV", __func__));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/* setup device for power manager */
	psPowerDevice->pfnDevicePrePower = pfnDevicePrePower;
	psPowerDevice->pfnDevicePostPower = pfnDevicePostPower;
	psPowerDevice->pfnSystemPrePower = pfnSystemPrePower;
	psPowerDevice->pfnSystemPostPower = pfnSystemPostPower;
	psPowerDevice->pfnPreClockSpeedChange = pfnPreClockSpeedChange;
	psPowerDevice->pfnPostClockSpeedChange = pfnPostClockSpeedChange;
	psPowerDevice->pfnForcedIdleRequest = pfnForcedIdleRequest;
	psPowerDevice->pfnForcedIdleCancelRequest = pfnForcedIdleCancelRequest;
	psPowerDevice->pfnDustCountRequest = pfnDustCountRequest;
	psPowerDevice->hSysData = psDeviceNode->psDevConfig->hSysData;
	psPowerDevice->hDevCookie = hDevCookie;
	psPowerDevice->eCurrentPowerState = eCurrentPowerState;
	psPowerDevice->eDefaultPowerState = eDefaultPowerState;

	psDeviceNode->psPowerDev = psPowerDevice;

	return (PVRSRV_OK);
}


/*!
******************************************************************************

 @Function	PVRSRVRemovePowerDevice

 @Description

 Removes device from power management register. Device is located by Device Index

 @Input		psDeviceNode : Device node

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR PVRSRVRemovePowerDevice(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	if (psDeviceNode->psPowerDev)
	{
		OSFreeMem(psDeviceNode->psPowerDev);
		psDeviceNode->psPowerDev = NULL;
	}

	return (PVRSRV_OK);
}


/*!
******************************************************************************

 @Function	PVRSRVGetDevicePowerState

 @Description

	Return the device power state

 @Input		psDeviceNode : Device node
 @Output	psPowerState : Current power state 

 @Return	PVRSRV_ERROR_UNKNOWN_POWER_STATE if device could not be found. PVRSRV_OK otherwise.

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVGetDevicePowerState(PVRSRV_DEVICE_NODE *psDeviceNode,
									   PPVRSRV_DEV_POWER_STATE pePowerState)
{
	PVRSRV_POWER_DEV *psPowerDevice;

	psPowerDevice = psDeviceNode->psPowerDev;
	if (psPowerDevice == NULL)
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

 @Input		psDeviceNode : Device node

 @Return	IMG_BOOL

******************************************************************************/
IMG_EXPORT
IMG_BOOL PVRSRVIsDevicePowered(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_DEV_POWER_STATE ePowerState;

	if (OSLockIsLocked(psDeviceNode->hPowerLock))
	{
		return IMG_FALSE;
	}

	if (PVRSRVGetDevicePowerState(psDeviceNode, &ePowerState) != PVRSRV_OK)
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

 @Input		psDeviceNode : Device node
 @Input		bIdleDevice : whether the device should be idled
 @Input		pvInfo

 @Return	void

******************************************************************************/
PVRSRV_ERROR PVRSRVDevicePreClockSpeedChange(PVRSRV_DEVICE_NODE *psDeviceNode,
											 IMG_BOOL	bIdleDevice,
											 void	*pvInfo)
{
	PVRSRV_ERROR		eError = PVRSRV_OK;
	PVRSRV_POWER_DEV	*psPowerDevice;
	IMG_UINT64			ui64StartTimer, ui64StopTimer;

	PVR_UNREFERENCED_PARAMETER(pvInfo);

	ui64StartTimer = OSClockus();

	/* This mutex is released in PVRSRVDevicePostClockSpeedChange. */
	eError = PVRSRVPowerLock(psDeviceNode);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: failed to acquire lock (%s)",
				 __func__, PVRSRVGetErrorStringKM(eError)));
		return eError;
	}

	psPowerDevice = psDeviceNode->psPowerDev;
	if (psPowerDevice)
	{
		if ((psPowerDevice->eCurrentPowerState == PVRSRV_DEV_POWER_STATE_ON) && bIdleDevice)
		{
			LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
			{	/* We can change the clock speed if the device is either IDLE or OFF */
				eError = PVRSRVDeviceIdleRequestKM(psDeviceNode, NULL, IMG_TRUE);

				if (eError == PVRSRV_OK)
				{
					break;
				}
				else if (eError == PVRSRV_ERROR_DEVICE_IDLE_REQUEST_DENIED)
				{
					PVRSRV_ERROR	eError2;

					PVRSRVPowerUnlock(psDeviceNode);
					OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
					eError2 = PVRSRVPowerLock(psDeviceNode);

					if (eError2 != PVRSRV_OK)
					{
						PVR_DPF((PVR_DBG_ERROR,
								 "%s: failed to acquire lock (%s)",
								 __func__, PVRSRVGetErrorStringKM(eError)));
						return eError2;
					}
				}
				else
				{
					PVRSRVPowerUnlock(psDeviceNode);
					return eError;
				}
			} END_LOOP_UNTIL_TIMEOUT();

			if (eError == PVRSRV_ERROR_DEVICE_IDLE_REQUEST_DENIED)
			{
				PVRSRVPowerUnlock(psDeviceNode);
				return eError;
			}
		}

		eError = psPowerDevice->pfnPreClockSpeedChange(psPowerDevice->hDevCookie,
		                                               psPowerDevice->eCurrentPowerState);
	}

	ui64StopTimer = OSClockus();

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	InsertPowerTimeStatisticExtraPre(ui64StartTimer, ui64StopTimer);
#endif

	return eError;
}


/*!
******************************************************************************

 @Function	PVRSRVDevicePostClockSpeedChange

 @Description

	Notification from system layer that a device clock speed change has just happened.

 @Input		psDeviceNode : Device node
 @Input		bIdleDevice : whether the device had been idled
 @Input		pvInfo

 @Return	void

******************************************************************************/
void PVRSRVDevicePostClockSpeedChange(PVRSRV_DEVICE_NODE *psDeviceNode,
									  IMG_BOOL		bIdleDevice,
									  void		*pvInfo)
{
	PVRSRV_ERROR		eError;
	PVRSRV_POWER_DEV	*psPowerDevice;
	IMG_UINT64			ui64StartTimer, ui64StopTimer;

    PVR_UNREFERENCED_PARAMETER(pvInfo);

	ui64StartTimer = OSClockus();

	psPowerDevice = psDeviceNode->psPowerDev;
	if (psPowerDevice)
	{
		eError = psPowerDevice->pfnPostClockSpeedChange(psPowerDevice->hDevCookie,
														psPowerDevice->eCurrentPowerState);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Device %p failed (%s)",
					 __func__, psDeviceNode, PVRSRVGetErrorStringKM(eError)));
		}

		if ((psPowerDevice->eCurrentPowerState == PVRSRV_DEV_POWER_STATE_ON) && bIdleDevice)
		{
			eError = PVRSRVDeviceIdleCancelRequestKM(psDeviceNode);

			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,
						 "%s: Failed to cancel forced IDLE.", __func__));
			}
		}
	}

	/* This mutex was acquired in PVRSRVDevicePreClockSpeedChange. */
	PVRSRVPowerUnlock(psDeviceNode);

	ui64StopTimer = OSClockus();

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	InsertPowerTimeStatisticExtraPost(ui64StartTimer, ui64StopTimer);
#endif
}

/*!
******************************************************************************

 @Function	PVRSRVDeviceDustCountChange

 @Description

	Request from system layer that a dust count change is requested.

 @Input		psDeviceNode : Device node
 @Input		ui32DustCount : dust count to be set

 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR PVRSRVDeviceDustCountChange(PVRSRV_DEVICE_NODE *psDeviceNode,
						IMG_UINT32	ui32DustCount)
{
	PVRSRV_ERROR		eError = PVRSRV_OK;
	PVRSRV_POWER_DEV	*psPowerDevice;

	psPowerDevice = psDeviceNode->psPowerDev;
	if (psPowerDevice)
	{
		PVRSRV_DEV_POWER_STATE eDevicePowerState;

		eError = PVRSRVPowerLock(psDeviceNode);

		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: failed to acquire lock (%s)",
					 __func__, PVRSRVGetErrorStringKM(eError)));
			return eError;
		}

		eDevicePowerState = psPowerDevice->eCurrentPowerState;
		if (eDevicePowerState == PVRSRV_DEV_POWER_STATE_ON)
		{
			/* Device must be idle to change dust count */
			LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
			{
				eError = PVRSRVDeviceIdleRequestKM(psDeviceNode, NULL, IMG_FALSE);
				if (eError == PVRSRV_OK)
				{
					break;
				}
				else if (eError == PVRSRV_ERROR_DEVICE_IDLE_REQUEST_DENIED)
				{
					PVRSRV_ERROR	eError2;

					PVRSRVPowerUnlock(psDeviceNode);
					OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
					eError2 = PVRSRVPowerLock(psDeviceNode);

					if (eError2 != PVRSRV_OK)
					{
						PVR_DPF((PVR_DBG_ERROR, "%s: failed to acquire lock (%s)",
								 __func__, PVRSRVGetErrorStringKM(eError)));
						return eError2;
					}
				}
				else
				{
					PVR_DPF((PVR_DBG_ERROR,
							 "%s: error occurred whilst forcing idle (%s)",
							 __func__, PVRSRVGetErrorStringKM(eError)));
					goto ErrorExit;
				}
			} END_LOOP_UNTIL_TIMEOUT();

			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,
						 "%s: timeout occurred attempting to force idle (%s)",
						 __func__, PVRSRVGetErrorStringKM(eError)));
				goto ErrorExit;
			}
		}

		if (psPowerDevice->pfnDustCountRequest != NULL)
		{
			PVRSRV_ERROR	eError2 = psPowerDevice->pfnDustCountRequest(psPowerDevice->hDevCookie, ui32DustCount);

			if (eError2 != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Device %p failed (%s)",
						 __func__, psDeviceNode,
						 PVRSRVGetErrorStringKM(eError)));
			}
		}

		if (eDevicePowerState == PVRSRV_DEV_POWER_STATE_ON)
		{
			eError = PVRSRVDeviceIdleCancelRequestKM(psDeviceNode);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,
						 "%s: Failed to cancel forced IDLE.", __func__));
				goto ErrorExit;
			}
		}

		PVRSRVPowerUnlock(psDeviceNode);
	}

	return eError;

ErrorExit:
	PVRSRVPowerUnlock(psDeviceNode);
	return eError;
}


/******************************************************************************
 End of file (power.c)
******************************************************************************/
