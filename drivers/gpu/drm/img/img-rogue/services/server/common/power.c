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

#include "lock.h"
#include "pvrsrv.h"
#include "pvr_debug.h"
#include "process_stats.h"


struct _PVRSRV_POWER_DEV_TAG_
{
	PFN_PRE_POWER					pfnDevicePrePower;
	PFN_POST_POWER					pfnDevicePostPower;
	PFN_SYS_PRE_POWER				pfnSystemPrePower;
	PFN_SYS_POST_POWER				pfnSystemPostPower;
	PFN_PRE_CLOCKSPEED_CHANGE		pfnPreClockSpeedChange;
	PFN_POST_CLOCKSPEED_CHANGE		pfnPostClockSpeedChange;
	PFN_FORCED_IDLE_REQUEST			pfnForcedIdleRequest;
	PFN_FORCED_IDLE_CANCEL_REQUEST	pfnForcedIdleCancelRequest;
	PFN_GPU_UNITS_POWER_CHANGE		pfnGPUUnitsPowerChange;
	IMG_HANDLE						hSysData;
	IMG_HANDLE						hDevCookie;
	PVRSRV_DEV_POWER_STATE			eDefaultPowerState;
	ATOMIC_T						eCurrentPowerState;
};

/*!
  Typedef for a pointer to a function that will be called for re-acquiring
  device powerlock after releasing it temporarily for some timeout period
  in function PVRSRVDeviceIdleRequestKM
 */
typedef PVRSRV_ERROR (*PFN_POWER_LOCK_ACQUIRE) (PPVRSRV_DEVICE_NODE psDevNode);

static inline IMG_UINT64 PVRSRVProcessStatsGetTimeNs(void)
{
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	return OSClockns64();
#else
	return 0;
#endif
}

static inline IMG_UINT64 PVRSRVProcessStatsGetTimeUs(void)
{
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	return OSClockus();
#else
	return 0;
#endif
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

/* We don't expect PID=0 to acquire device power-lock */
#define PWR_LOCK_OWNER_PID_CLR_VAL 0

PVRSRV_ERROR PVRSRVPowerLockInit(PPVRSRV_DEVICE_NODE psDeviceNode)
{
	PVRSRV_ERROR eError;

	eError = OSLockCreate(&psDeviceNode->hPowerLock);
	PVR_LOG_RETURN_IF_ERROR(eError, "OSLockCreate");

	psDeviceNode->uiPwrLockOwnerPID = PWR_LOCK_OWNER_PID_CLR_VAL;
	return PVRSRV_OK;
}

void PVRSRVPowerLockDeInit(PPVRSRV_DEVICE_NODE psDeviceNode)
{
	psDeviceNode->uiPwrLockOwnerPID = PWR_LOCK_OWNER_PID_CLR_VAL;
	OSLockDestroy(psDeviceNode->hPowerLock);
}

IMG_BOOL PVRSRVPwrLockIsLockedByMe(PCPVRSRV_DEVICE_NODE psDeviceNode)
{
	return OSLockIsLocked(psDeviceNode->hPowerLock) &&
	       OSGetCurrentClientProcessIDKM() == psDeviceNode->uiPwrLockOwnerPID;
}

PVRSRV_ERROR PVRSRVPowerLock(PPVRSRV_DEVICE_NODE psDeviceNode)
{
	OSLockAcquire(psDeviceNode->hPowerLock);

	/* Only allow to take powerlock when the system power is on */
	if (_IsSystemStatePowered(psDeviceNode->eCurrentSysPowerState))
	{
		psDeviceNode->uiPwrLockOwnerPID = OSGetCurrentClientProcessIDKM();
		return PVRSRV_OK;
	}

	OSLockRelease(psDeviceNode->hPowerLock);

	return PVRSRV_ERROR_SYSTEM_STATE_POWERED_OFF;
}

PVRSRV_ERROR PVRSRVPowerTryLock(PPVRSRV_DEVICE_NODE psDeviceNode)
{
	if (!(OSTryLockAcquire(psDeviceNode->hPowerLock)))
	{
		return PVRSRV_ERROR_RETRY;
	}

	/* Only allow to take powerlock when the system power is on */
	if (_IsSystemStatePowered(psDeviceNode->eCurrentSysPowerState))
	{
		psDeviceNode->uiPwrLockOwnerPID = OSGetCurrentClientProcessIDKM();

		/* System is powered ON, return OK */
		return PVRSRV_OK;
	}
	else
	{
		/* System is powered OFF, release the lock and return error */
		OSLockRelease(psDeviceNode->hPowerLock);
		return PVRSRV_ERROR_SYSTEM_STATE_POWERED_OFF;
	}
}

/*!
******************************************************************************

 @Function     _PVRSRVForcedPowerLock

 @Description  Obtain the mutex for power transitions regardless of system
               power state

 @Return       Always returns PVRSRV_OK. Function prototype required same as
               PFN_POWER_LOCK_ACQUIRE

******************************************************************************/
static PVRSRV_ERROR _PVRSRVForcedPowerLock(PPVRSRV_DEVICE_NODE psDeviceNode)
{
	OSLockAcquire(psDeviceNode->hPowerLock);
	psDeviceNode->uiPwrLockOwnerPID = OSGetCurrentClientProcessIDKM();

	return PVRSRV_OK;
}

void PVRSRVPowerUnlock(PPVRSRV_DEVICE_NODE psDeviceNode)
{
	PVR_ASSERT(PVRSRVPwrLockIsLockedByMe(psDeviceNode));

	/* Reset uiPwrLockOwnerPID before releasing lock */
	psDeviceNode->uiPwrLockOwnerPID = PWR_LOCK_OWNER_PID_CLR_VAL;
	OSLockRelease(psDeviceNode->hPowerLock);
}

IMG_BOOL PVRSRVDeviceIsDefaultStateOFF(PVRSRV_POWER_DEV *psPowerDevice)
{
	return (psPowerDevice->eDefaultPowerState == PVRSRV_DEV_POWER_STATE_OFF);
}

PVRSRV_ERROR PVRSRVSetDeviceDefaultPowerState(PCPVRSRV_DEVICE_NODE psDeviceNode,
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

/*
 @Input       pfnPowerLockAcquire  : Function to re-acquire power-lock in-case
                                     it was necessary to release it.
*/
static PVRSRV_ERROR _PVRSRVDeviceIdleRequestKM(PPVRSRV_DEVICE_NODE psDeviceNode,
					PFN_SYS_DEV_IS_DEFAULT_STATE_OFF    pfnIsDefaultStateOff,
					IMG_BOOL                            bDeviceOffPermitted,
					PFN_POWER_LOCK_ACQUIRE              pfnPowerLockAcquire)
{
	PVRSRV_POWER_DEV *psPowerDev = psDeviceNode->psPowerDev;
	PVRSRV_ERROR eError;

	if ((psPowerDev && psPowerDev->pfnForcedIdleRequest) &&
	    (!pfnIsDefaultStateOff || pfnIsDefaultStateOff(psPowerDev)))
	{
		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError = psPowerDev->pfnForcedIdleRequest(psPowerDev->hDevCookie,
			                                          bDeviceOffPermitted);
			if (eError == PVRSRV_ERROR_DEVICE_IDLE_REQUEST_DENIED)
			{
				PVRSRV_ERROR eErrPwrLockAcq;
				/* FW denied idle request */
				PVRSRVPowerUnlock(psDeviceNode);

				OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);

				eErrPwrLockAcq = pfnPowerLockAcquire(psDeviceNode);
				if (eErrPwrLockAcq != PVRSRV_OK)
				{
					/* We only understand PVRSRV_ERROR_RETRY, so assert on others.
					 * Moreover, we've ended-up releasing the power-lock which was
					 * originally "held" by caller before calling this function -
					 * since this needs vigilant handling at call-site, we pass
					 * back an explicit error, for caller(s) to "avoid" calling
					 * PVRSRVPowerUnlock */
					PVR_ASSERT(eErrPwrLockAcq == PVRSRV_ERROR_RETRY);
					PVR_DPF((PVR_DBG_ERROR, "%s: Failed to re-acquire power-lock "
					         "(%s) after releasing it for a time-out",
							 __func__, PVRSRVGetErrorString(eErrPwrLockAcq)));
					return PVRSRV_ERROR_PWLOCK_RELEASED_REACQ_FAILED;
				}
			}
			else
			{
				/* idle request successful or some other error occurred, return */
				break;
			}
		} END_LOOP_UNTIL_TIMEOUT();
	}
	else
	{
		return PVRSRV_OK;
	}

	return eError;
}

/*
 * Wrapper function helps limiting calling complexity of supplying additional
 * PFN_POWER_LOCK_ACQUIRE argument (required by _PVRSRVDeviceIdleRequestKM)
 */
inline PVRSRV_ERROR PVRSRVDeviceIdleRequestKM(PPVRSRV_DEVICE_NODE psDeviceNode,
					PFN_SYS_DEV_IS_DEFAULT_STATE_OFF      pfnIsDefaultStateOff,
					IMG_BOOL                              bDeviceOffPermitted)
{
	return _PVRSRVDeviceIdleRequestKM(psDeviceNode,
	                                  pfnIsDefaultStateOff,
	                                  bDeviceOffPermitted,
	                                  PVRSRVPowerLock);
}

PVRSRV_ERROR PVRSRVDeviceIdleCancelRequestKM(PPVRSRV_DEVICE_NODE psDeviceNode)
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
	PVRSRV_DEV_POWER_STATE eCurrentPowerState;
	IMG_UINT64 ui64SysTimer1 = 0;
	IMG_UINT64 ui64SysTimer2 = 0;
	IMG_UINT64 ui64DevTimer1 = 0;
	IMG_UINT64 ui64DevTimer2 = 0;
	PVRSRV_ERROR eError;

	PVR_ASSERT(eNewPowerState != PVRSRV_DEV_POWER_STATE_DEFAULT);

	eCurrentPowerState = OSAtomicRead(&psPowerDevice->eCurrentPowerState);

	if (psPowerDevice->pfnDevicePrePower != NULL)
	{
		ui64DevTimer1 = PVRSRVProcessStatsGetTimeNs();

		/* Call the device's power callback. */
		eError = psPowerDevice->pfnDevicePrePower(psPowerDevice->hDevCookie,
												  eNewPowerState,
												  eCurrentPowerState,
												  bForced);

		ui64DevTimer2 = PVRSRVProcessStatsGetTimeNs();

		PVR_RETURN_IF_ERROR(eError);
	}

	/* Do any required system-layer processing. */
	if (psPowerDevice->pfnSystemPrePower != NULL)
	{
		ui64SysTimer1 = PVRSRVProcessStatsGetTimeNs();

		eError = psPowerDevice->pfnSystemPrePower(psPowerDevice->hSysData,
												  (eNewPowerState == PVRSRV_DEV_POWER_STATE_ON) ?
													PVRSRV_SYS_POWER_STATE_ON :
													PVRSRV_SYS_POWER_STATE_OFF,
												  (eCurrentPowerState == PVRSRV_DEV_POWER_STATE_ON) ?
													PVRSRV_SYS_POWER_STATE_ON :
													PVRSRV_SYS_POWER_STATE_OFF,
												  bForced);

		ui64SysTimer2 = PVRSRVProcessStatsGetTimeNs();

		PVR_RETURN_IF_ERROR(eError);
	}

	InsertPowerTimeStatistic(ui64SysTimer1, ui64SysTimer2,
							 ui64DevTimer1, ui64DevTimer2,
							 bForced,
							 eNewPowerState == PVRSRV_DEV_POWER_STATE_ON,
							 IMG_TRUE);

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
	PVRSRV_DEV_POWER_STATE eCurrentPowerState;
	IMG_UINT64 ui64SysTimer1 = 0;
	IMG_UINT64 ui64SysTimer2 = 0;
	IMG_UINT64 ui64DevTimer1 = 0;
	IMG_UINT64 ui64DevTimer2 = 0;
	PVRSRV_ERROR eError;

	PVR_ASSERT(eNewPowerState != PVRSRV_DEV_POWER_STATE_DEFAULT);

	eCurrentPowerState = OSAtomicRead(&psPowerDevice->eCurrentPowerState);

	/* Do any required system-layer processing. */
	if (psPowerDevice->pfnSystemPostPower != NULL)
	{
		ui64SysTimer1 = PVRSRVProcessStatsGetTimeNs();

		eError = psPowerDevice->pfnSystemPostPower(psPowerDevice->hSysData,
												   (eNewPowerState == PVRSRV_DEV_POWER_STATE_ON) ?
													 PVRSRV_SYS_POWER_STATE_ON :
													 PVRSRV_SYS_POWER_STATE_OFF,
												   (eCurrentPowerState == PVRSRV_DEV_POWER_STATE_ON) ?
													 PVRSRV_SYS_POWER_STATE_ON :
													 PVRSRV_SYS_POWER_STATE_OFF,
												   bForced);

		ui64SysTimer2 = PVRSRVProcessStatsGetTimeNs();

		PVR_RETURN_IF_ERROR(eError);
	}

	if (psPowerDevice->pfnDevicePostPower != NULL)
	{
		ui64DevTimer1 = PVRSRVProcessStatsGetTimeNs();

		/* Call the device's power callback. */
		eError = psPowerDevice->pfnDevicePostPower(psPowerDevice->hDevCookie,
												   eNewPowerState,
												   eCurrentPowerState,
												   bForced);

		ui64DevTimer2 = PVRSRVProcessStatsGetTimeNs();

		PVR_RETURN_IF_ERROR(eError);
	}

	InsertPowerTimeStatistic(ui64SysTimer1, ui64SysTimer2,
							 ui64DevTimer1, ui64DevTimer2,
							 bForced,
							 eNewPowerState == PVRSRV_DEV_POWER_STATE_ON,
							 IMG_FALSE);

	OSAtomicWrite(&psPowerDevice->eCurrentPowerState, eNewPowerState);

	return PVRSRV_OK;
}

PVRSRV_ERROR PVRSRVSetDevicePowerStateKM(PPVRSRV_DEVICE_NODE psDeviceNode,
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

	if (OSAtomicRead(&psPowerDevice->eCurrentPowerState) != eNewPowerState)
	{
		eError = PVRSRVDevicePrePowerStateKM(psPowerDevice,
											 eNewPowerState,
											 bForced);
		PVR_GOTO_IF_ERROR(eError, ErrorExit);

		eError = PVRSRVDevicePostPowerStateKM(psPowerDevice,
											  eNewPowerState,
											  bForced);
		PVR_GOTO_IF_ERROR(eError, ErrorExit);

		/* Signal Device Watchdog Thread about power mode change. */
		if (eNewPowerState == PVRSRV_DEV_POWER_STATE_ON)
		{
			psPVRSRVData->ui32DevicesWatchdogPwrTrans++;
#if !defined(PVRSRV_SERVER_THREADS_INDEFINITE_SLEEP)
			if (psPVRSRVData->ui32DevicesWatchdogTimeout == DEVICES_WATCHDOG_POWER_OFF_SLEEP_TIMEOUT)
#endif
			{
				eError = OSEventObjectSignal(psPVRSRVData->hDevicesWatchdogEvObj);
				PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");
			}
		}
#if defined(PVRSRV_SERVER_THREADS_INDEFINITE_SLEEP)
		else if (eNewPowerState == PVRSRV_DEV_POWER_STATE_OFF)
		{
			/* signal watchdog thread and give it a chance to switch to
			 * longer / infinite wait time */
			eError = OSEventObjectSignal(psPVRSRVData->hDevicesWatchdogEvObj);
			PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");
		}
#endif /* defined(PVRSRV_SERVER_THREADS_INDEFINITE_SLEEP) */
	}

	return PVRSRV_OK;

ErrorExit:

	if (eError == PVRSRV_ERROR_DEVICE_POWER_CHANGE_DENIED)
	{
		PVR_DPF((PVR_DBG_MESSAGE,
				 "%s: Transition to %d was denied, Forced=%d",
				 __func__, eNewPowerState, bForced));
	}
	else if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_WARNING,
				 "%s: Transition to %d FAILED (%s)",
				 __func__, eNewPowerState, PVRSRVGetErrorString(eError)));
	}

	return eError;
}

PVRSRV_ERROR PVRSRVSetDeviceSystemPowerState(PPVRSRV_DEVICE_NODE psDeviceNode,
											 PVRSRV_SYS_POWER_STATE eNewSysPowerState)
{
	PVRSRV_ERROR eError;
	IMG_UINT uiStage = 0;

	PVRSRV_DEV_POWER_STATE eNewDevicePowerState =
	  _IsSystemStatePowered(eNewSysPowerState)? PVRSRV_DEV_POWER_STATE_DEFAULT : PVRSRV_DEV_POWER_STATE_OFF;

	/* If setting devices to default state, force idle all devices whose default state is off */
	PFN_SYS_DEV_IS_DEFAULT_STATE_OFF pfnIsDefaultStateOff =
	  (eNewDevicePowerState == PVRSRV_DEV_POWER_STATE_DEFAULT) ? PVRSRVDeviceIsDefaultStateOFF : NULL;

	/* require a proper power state */
	if (eNewSysPowerState == PVRSRV_SYS_POWER_STATE_Unspecified)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Prevent simultaneous SetPowerStateKM calls */
	_PVRSRVForcedPowerLock(psDeviceNode);

	/* no power transition requested, so do nothing */
	if (eNewSysPowerState == psDeviceNode->eCurrentSysPowerState)
	{
		PVRSRVPowerUnlock(psDeviceNode);
		return PVRSRV_OK;
	}

	eError = _PVRSRVDeviceIdleRequestKM(psDeviceNode, pfnIsDefaultStateOff,
	                                    IMG_TRUE, _PVRSRVForcedPowerLock);
	if (eError != PVRSRV_OK)
	{
		PVR_LOG_ERROR(eError, "_PVRSRVDeviceIdleRequestKM");
		uiStage++;
		goto ErrorExit;
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
			 "%s: Transition from %d to %d FAILED (%s) at stage %u. Dumping debug info.",
			 __func__, psDeviceNode->eCurrentSysPowerState, eNewSysPowerState,
			 PVRSRVGetErrorString(eError), uiStage));

	PVRSRVDebugRequest(psDeviceNode, DEBUG_REQUEST_VERBOSITY_MAX, NULL, NULL);

	return eError;
}

PVRSRV_ERROR PVRSRVSetSystemPowerState(PVRSRV_DEVICE_CONFIG *psDevConfig,
											 PVRSRV_SYS_POWER_STATE eNewSysPowerState)
{
	PVRSRV_ERROR eError;
	PVRSRV_DEVICE_NODE *psDevNode = psDevConfig->psDevNode;
	PVRSRV_SYS_POWER_STATE eCurrentSysPowerState;

	if (psDevNode != NULL)
	{
		eCurrentSysPowerState = psDevNode->eCurrentSysPowerState;
	}
	else
	{
		/* assume power is off if no device node */
		eCurrentSysPowerState = PVRSRV_SYS_POWER_STATE_OFF;
	}

	/* no power transition requested, so do nothing */
	if (eNewSysPowerState == eCurrentSysPowerState)
	{
		return PVRSRV_OK;
	}

	if (psDevConfig->pfnPrePowerState != NULL)
	{
		eError = psDevConfig->pfnPrePowerState(psDevConfig->hSysData,
												  eNewSysPowerState,
												  eCurrentSysPowerState,
												  IMG_TRUE);

		PVR_RETURN_IF_ERROR(eError);
	}

	if (psDevConfig->pfnPostPowerState != NULL)
	{
		eError = psDevConfig->pfnPostPowerState(psDevConfig->hSysData,
												   eNewSysPowerState,
												   eCurrentSysPowerState,
												   IMG_TRUE);

		PVR_RETURN_IF_ERROR(eError);
	}

	if (psDevNode != NULL)
	{
		psDevNode->eCurrentSysPowerState = eNewSysPowerState;
	}

	return PVRSRV_OK;
}

void PVRSRVSetPowerCallbacks(PPVRSRV_DEVICE_NODE				psDeviceNode,
							 PVRSRV_POWER_DEV					*psPowerDevice,
							 PFN_PRE_POWER						pfnDevicePrePower,
							 PFN_POST_POWER						pfnDevicePostPower,
							 PFN_SYS_PRE_POWER					pfnSystemPrePower,
							 PFN_SYS_POST_POWER					pfnSystemPostPower,
							 PFN_FORCED_IDLE_REQUEST			pfnForcedIdleRequest,
							 PFN_FORCED_IDLE_CANCEL_REQUEST	pfnForcedIdleCancelRequest)
{
	if (psPowerDevice != NULL)
	{
		if (PVRSRV_VZ_MODE_IS(GUEST) || (psDeviceNode->bAutoVzFwIsUp))
		{
			psPowerDevice->pfnSystemPrePower = NULL;
			psPowerDevice->pfnSystemPostPower = NULL;
		}
		else
		{
			psPowerDevice->pfnSystemPrePower = pfnSystemPrePower;
			psPowerDevice->pfnSystemPostPower = pfnSystemPostPower;
		}

		psPowerDevice->pfnDevicePrePower = pfnDevicePrePower;
		psPowerDevice->pfnDevicePostPower = pfnDevicePostPower;
		psPowerDevice->pfnForcedIdleRequest = pfnForcedIdleRequest;
		psPowerDevice->pfnForcedIdleCancelRequest = pfnForcedIdleCancelRequest;
	}
}

PVRSRV_ERROR PVRSRVRegisterPowerDevice(PPVRSRV_DEVICE_NODE psDeviceNode,
									   PFN_PRE_POWER				pfnDevicePrePower,
									   PFN_POST_POWER				pfnDevicePostPower,
									   PFN_SYS_PRE_POWER			pfnSystemPrePower,
									   PFN_SYS_POST_POWER			pfnSystemPostPower,
									   PFN_PRE_CLOCKSPEED_CHANGE	pfnPreClockSpeedChange,
									   PFN_POST_CLOCKSPEED_CHANGE	pfnPostClockSpeedChange,
									   PFN_FORCED_IDLE_REQUEST	pfnForcedIdleRequest,
									   PFN_FORCED_IDLE_CANCEL_REQUEST	pfnForcedIdleCancelRequest,
									   PFN_GPU_UNITS_POWER_CHANGE	pfnGPUUnitsPowerChange,
									   IMG_HANDLE					hDevCookie,
									   PVRSRV_DEV_POWER_STATE		eCurrentPowerState,
									   PVRSRV_DEV_POWER_STATE		eDefaultPowerState)
{
	PVRSRV_POWER_DEV *psPowerDevice;

	PVR_ASSERT(!psDeviceNode->psPowerDev);

	PVR_ASSERT(eCurrentPowerState != PVRSRV_DEV_POWER_STATE_DEFAULT);
	PVR_ASSERT(eDefaultPowerState != PVRSRV_DEV_POWER_STATE_DEFAULT);

	psPowerDevice = OSAllocMem(sizeof(PVRSRV_POWER_DEV));
	PVR_LOG_RETURN_IF_NOMEM(psPowerDevice, "psPowerDevice");

	/* setup device for power manager */
	PVRSRVSetPowerCallbacks(psDeviceNode,
							psPowerDevice,
							pfnDevicePrePower,
							pfnDevicePostPower,
							pfnSystemPrePower,
							pfnSystemPostPower,
							pfnForcedIdleRequest,
							pfnForcedIdleCancelRequest);

	psPowerDevice->pfnPreClockSpeedChange = pfnPreClockSpeedChange;
	psPowerDevice->pfnPostClockSpeedChange = pfnPostClockSpeedChange;
	psPowerDevice->pfnGPUUnitsPowerChange = pfnGPUUnitsPowerChange;
	psPowerDevice->hSysData = psDeviceNode->psDevConfig->hSysData;
	psPowerDevice->hDevCookie = hDevCookie;
	OSAtomicWrite(&psPowerDevice->eCurrentPowerState, eCurrentPowerState);
	psPowerDevice->eDefaultPowerState = eDefaultPowerState;

	psDeviceNode->psPowerDev = psPowerDevice;

	return PVRSRV_OK;
}

PVRSRV_ERROR PVRSRVRemovePowerDevice(PPVRSRV_DEVICE_NODE psDeviceNode)
{
	if (psDeviceNode->psPowerDev)
	{
		OSFreeMem(psDeviceNode->psPowerDev);
		psDeviceNode->psPowerDev = NULL;
	}

	return PVRSRV_OK;
}

PVRSRV_ERROR PVRSRVGetDevicePowerState(PCPVRSRV_DEVICE_NODE psDeviceNode,
									   PPVRSRV_DEV_POWER_STATE pePowerState)
{
	PVRSRV_POWER_DEV *psPowerDevice;

	psPowerDevice = psDeviceNode->psPowerDev;
	if (psPowerDevice == NULL)
	{
		return PVRSRV_ERROR_UNKNOWN_POWER_STATE;
	}

	*pePowerState = OSAtomicRead(&psPowerDevice->eCurrentPowerState);

	return PVRSRV_OK;
}

IMG_BOOL PVRSRVIsDevicePowered(PPVRSRV_DEVICE_NODE psDeviceNode)
{
	PVRSRV_DEV_POWER_STATE ePowerState;

	if (PVRSRVGetDevicePowerState(psDeviceNode, &ePowerState) != PVRSRV_OK)
	{
		return IMG_FALSE;
	}

	return (ePowerState == PVRSRV_DEV_POWER_STATE_ON);
}

PVRSRV_ERROR
PVRSRVDevicePreClockSpeedChange(PPVRSRV_DEVICE_NODE psDeviceNode,
                                IMG_BOOL            bIdleDevice,
                                void*               pvInfo)
{
	PVRSRV_ERROR		eError = PVRSRV_OK;
	PVRSRV_POWER_DEV	*psPowerDevice;
	IMG_UINT64			ui64StartTimer, ui64StopTimer;

	PVR_UNREFERENCED_PARAMETER(pvInfo);

	ui64StartTimer = PVRSRVProcessStatsGetTimeUs();

	/* This mutex is released in PVRSRVDevicePostClockSpeedChange. */
	eError = PVRSRVPowerLock(psDeviceNode);
	PVR_LOG_RETURN_IF_ERROR(eError, "PVRSRVPowerLock");

	psPowerDevice = psDeviceNode->psPowerDev;
	if (psPowerDevice)
	{
		PVRSRV_DEV_POWER_STATE eCurrentPowerState =
		        OSAtomicRead(&psPowerDevice->eCurrentPowerState);

		if ((eCurrentPowerState == PVRSRV_DEV_POWER_STATE_ON) && bIdleDevice)
		{
			/* We can change the clock speed if the device is either IDLE or OFF */
			eError = PVRSRVDeviceIdleRequestKM(psDeviceNode, NULL, IMG_TRUE);

			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_WARNING, "%s() failed (%s) in %s()", "PVRSRVDeviceIdleRequestKM", PVRSRVGETERRORSTRING(eError), __func__));
				if (eError != PVRSRV_ERROR_PWLOCK_RELEASED_REACQ_FAILED)
				{
					PVRSRVPowerUnlock(psDeviceNode);
				}
				return eError;
			}
		}

		eError = psPowerDevice->pfnPreClockSpeedChange(psPowerDevice->hDevCookie,
		                                               eCurrentPowerState);
	}

	ui64StopTimer = PVRSRVProcessStatsGetTimeUs();

	InsertPowerTimeStatisticExtraPre(ui64StartTimer, ui64StopTimer);

	return eError;
}

void
PVRSRVDevicePostClockSpeedChange(PPVRSRV_DEVICE_NODE psDeviceNode,
                                 IMG_BOOL            bIdleDevice,
                                 void*               pvInfo)
{
	PVRSRV_ERROR		eError;
	PVRSRV_POWER_DEV	*psPowerDevice;
	IMG_UINT64			ui64StartTimer, ui64StopTimer;

	PVR_UNREFERENCED_PARAMETER(pvInfo);

	ui64StartTimer = PVRSRVProcessStatsGetTimeUs();

	psPowerDevice = psDeviceNode->psPowerDev;
	if (psPowerDevice)
	{
		PVRSRV_DEV_POWER_STATE eCurrentPowerState =
		        OSAtomicRead(&psPowerDevice->eCurrentPowerState);

		eError = psPowerDevice->pfnPostClockSpeedChange(psPowerDevice->hDevCookie,
														eCurrentPowerState);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Device %p failed (%s)",
					 __func__, psDeviceNode, PVRSRVGetErrorString(eError)));
		}

		if ((eCurrentPowerState == PVRSRV_DEV_POWER_STATE_ON) && bIdleDevice)
		{
			eError = PVRSRVDeviceIdleCancelRequestKM(psDeviceNode);
			PVR_LOG_IF_ERROR(eError, "PVRSRVDeviceIdleCancelRequestKM");
		}
	}

	/* This mutex was acquired in PVRSRVDevicePreClockSpeedChange. */
	PVRSRVPowerUnlock(psDeviceNode);

	OSAtomicIncrement(&psDeviceNode->iNumClockSpeedChanges);

	ui64StopTimer = PVRSRVProcessStatsGetTimeUs();

	InsertPowerTimeStatisticExtraPost(ui64StartTimer, ui64StopTimer);
}

PVRSRV_ERROR PVRSRVDeviceGPUUnitsPowerChange(PPVRSRV_DEVICE_NODE psDeviceNode,
                                             IMG_UINT32 ui32NewValue)
{
	PVRSRV_ERROR		eError = PVRSRV_OK;
	PVRSRV_POWER_DEV	*psPowerDevice;

	psPowerDevice = psDeviceNode->psPowerDev;
	if (psPowerDevice)
	{
		PVRSRV_DEV_POWER_STATE eDevicePowerState;

		eError = PVRSRVPowerLock(psDeviceNode);
		PVR_LOG_RETURN_IF_ERROR(eError, "PVRSRVPowerLock");

		eDevicePowerState = OSAtomicRead(&psPowerDevice->eCurrentPowerState);
		if (eDevicePowerState == PVRSRV_DEV_POWER_STATE_ON)
		{
			/* Device must be idle to change GPU unit(s) power state */
			eError = PVRSRVDeviceIdleRequestKM(psDeviceNode, NULL, IMG_FALSE);

			if (eError != PVRSRV_OK)
			{
				PVR_LOG_ERROR(eError, "PVRSRVDeviceIdleRequestKM");
				if (eError == PVRSRV_ERROR_PWLOCK_RELEASED_REACQ_FAILED)
				{
					goto ErrorExit;
				}
				goto ErrorUnlockAndExit;
			}
		}

		if (psPowerDevice->pfnGPUUnitsPowerChange != NULL)
		{
			PVRSRV_ERROR eError2 = psPowerDevice->pfnGPUUnitsPowerChange(psPowerDevice->hDevCookie, ui32NewValue);

			if (eError2 != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Device %p failed (%s)",
				         __func__, psDeviceNode,
				         PVRSRVGetErrorString(eError2)));
			}
		}

		if (eDevicePowerState == PVRSRV_DEV_POWER_STATE_ON)
		{
			eError = PVRSRVDeviceIdleCancelRequestKM(psDeviceNode);
			PVR_LOG_GOTO_IF_ERROR(eError, "PVRSRVDeviceIdleCancelRequestKM", ErrorUnlockAndExit);
		}

		PVRSRVPowerUnlock(psDeviceNode);
	}

	return eError;

ErrorUnlockAndExit:
	PVRSRVPowerUnlock(psDeviceNode);
ErrorExit:
	return eError;
}

/******************************************************************************
 End of file (power.c)
******************************************************************************/
