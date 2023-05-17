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
#include "htbserver.h"
#include "di_server.h"

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
typedef struct _EXTRA_POWER_STATS_
{
	IMG_UINT64	ui64PreClockSpeedChangeDuration;
	IMG_UINT64	ui64BetweenPreEndingAndPostStartingDuration;
	IMG_UINT64	ui64PostClockSpeedChangeDuration;
} EXTRA_POWER_STATS;

/* For the power timing stats we need 16 variables to store all the
 * combinations of forced/not forced, power-on/power-off, pre-power/post-power
 * and device/system statistics
 */
#define NUM_POWER_STATS        (16)
#define NUM_EXTRA_POWER_STATS	10

typedef struct PVRSRV_POWER_STATS_TAG
{
	EXTRA_POWER_STATS               asClockSpeedChanges[NUM_EXTRA_POWER_STATS];
	IMG_UINT64                      ui64PreClockSpeedChangeMark;
	IMG_UINT64                      ui64FirmwareIdleDuration;
	IMG_UINT32                      aui32PowerTimingStats[NUM_POWER_STATS];
	IMG_UINT32                      ui32ClockSpeedIndexStart;
	IMG_UINT32                      ui32ClockSpeedIndexEnd;
	IMG_UINT32                      ui32FirmwareStartTimestamp;
} PVRSRV_POWER_STATS;
#endif

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
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	PVRSRV_POWER_STATS				sPowerStats;
#endif
};

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
/*
 * Power statistics related definitions
 */

/* For the mean time, use an exponentially weighted moving average with a
 * 1/4 weighting for the new measurement.
 */
#define MEAN_TIME(A, B)     ( ((3*(A))/4) + ((1 * (B))/4) )

#define UPDATE_TIME(time, newtime) \
	((time) > 0 ? MEAN_TIME((time), (newtime)) : (newtime))

/* Enum to be used as input to GET_POWER_STAT_INDEX */
typedef enum
{
	DEVICE     = 0,
	SYSTEM     = 1,
	POST_POWER = 0,
	PRE_POWER  = 2,
	POWER_OFF  = 0,
	POWER_ON   = 4,
	NOT_FORCED = 0,
	FORCED     = 8,
} PVRSRV_POWER_STAT_TYPE;

/* Macro used to access one of the power timing statistics inside an array */
#define GET_POWER_STAT_INDEX(forced,powon,prepow,system) \
	((forced) + (powon) + (prepow) + (system))

void PVRSRVSetFirmwareStartTime(PVRSRV_POWER_DEV *psPowerDevice,
						  IMG_UINT32 ui32Time)
{
	PVRSRV_POWER_STATS *psPowerStats = &psPowerDevice->sPowerStats;

	psPowerStats->ui32FirmwareStartTimestamp =
		UPDATE_TIME(psPowerStats->ui32FirmwareStartTimestamp,
					ui32Time);
}

void PVRSRVSetFirmwareHandshakeIdleTime(PVRSRV_POWER_DEV *psPowerDevice,
								  IMG_UINT64 ui64Duration)
{
	PVRSRV_POWER_STATS *psPowerStats = &psPowerDevice->sPowerStats;

	psPowerStats->ui64FirmwareIdleDuration =
		UPDATE_TIME(psPowerStats->ui64FirmwareIdleDuration,
					ui64Duration);
}

static void _InsertPowerTimeStatistic(PVRSRV_POWER_DEV *psPowerDevice,
									  IMG_UINT64 ui64SysStartTime, IMG_UINT64 ui64SysEndTime,
									  IMG_UINT64 ui64DevStartTime, IMG_UINT64 ui64DevEndTime,
									  IMG_BOOL bForced, IMG_BOOL bPowerOn, IMG_BOOL bPrePower)
{
	PVRSRV_POWER_STATS *psPowerStats = &psPowerDevice->sPowerStats;
	IMG_UINT32 *pui32Stat;
	IMG_UINT64 ui64DeviceDiff = ui64DevEndTime - ui64DevStartTime;
	IMG_UINT64 ui64SystemDiff = ui64SysEndTime - ui64SysStartTime;
	IMG_UINT32 ui32Index;

	if (bPrePower)
	{
		HTBLOGK(HTB_SF_MAIN_PRE_POWER, bPowerOn, ui64DeviceDiff, ui64SystemDiff);
	}
	else
	{
		HTBLOGK(HTB_SF_MAIN_POST_POWER, bPowerOn, ui64SystemDiff, ui64DeviceDiff);
	}

	ui32Index = GET_POWER_STAT_INDEX(bForced ? FORCED : NOT_FORCED,
	                                 bPowerOn ? POWER_ON : POWER_OFF,
	                                 bPrePower ? PRE_POWER : POST_POWER,
	                                 DEVICE);
	pui32Stat = &psPowerStats->aui32PowerTimingStats[ui32Index];
	*pui32Stat = UPDATE_TIME(*pui32Stat, ui64DeviceDiff);

	ui32Index = GET_POWER_STAT_INDEX(bForced ? FORCED : NOT_FORCED,
	                                 bPowerOn ? POWER_ON : POWER_OFF,
	                                 bPrePower ? PRE_POWER : POST_POWER,
	                                 SYSTEM);
	pui32Stat = &psPowerStats->aui32PowerTimingStats[ui32Index];
	*pui32Stat = UPDATE_TIME(*pui32Stat, ui64SystemDiff);
}

static void _InsertPowerTimeStatisticExtraPre(PVRSRV_POWER_DEV *psPowerDevice,
											  IMG_UINT64 ui64StartTimer,
											  IMG_UINT64 ui64Stoptimer)
{
	PVRSRV_POWER_STATS *psPowerStats = &psPowerDevice->sPowerStats;

	psPowerStats->asClockSpeedChanges[psPowerStats->ui32ClockSpeedIndexEnd].ui64PreClockSpeedChangeDuration =
		ui64Stoptimer - ui64StartTimer;

	psPowerStats->ui64PreClockSpeedChangeMark = OSClockus();
}

static void _InsertPowerTimeStatisticExtraPost(PVRSRV_POWER_DEV *psPowerDevice,
											   IMG_UINT64 ui64StartTimer,
											   IMG_UINT64 ui64StopTimer)
{
	PVRSRV_POWER_STATS *psPowerStats = &psPowerDevice->sPowerStats;
	IMG_UINT64 ui64Duration = ui64StartTimer - psPowerStats->ui64PreClockSpeedChangeMark;

	PVR_ASSERT(psPowerStats->ui64PreClockSpeedChangeMark > 0);

	psPowerStats->asClockSpeedChanges[psPowerStats->ui32ClockSpeedIndexEnd].ui64BetweenPreEndingAndPostStartingDuration = ui64Duration;
	psPowerStats->asClockSpeedChanges[psPowerStats->ui32ClockSpeedIndexEnd].ui64PostClockSpeedChangeDuration = ui64StopTimer - ui64StartTimer;

	psPowerStats->ui32ClockSpeedIndexEnd = (psPowerStats->ui32ClockSpeedIndexEnd + 1) % NUM_EXTRA_POWER_STATS;

	if (psPowerStats->ui32ClockSpeedIndexEnd == psPowerStats->ui32ClockSpeedIndexStart)
	{
		psPowerStats->ui32ClockSpeedIndexStart = (psPowerStats->ui32ClockSpeedIndexStart + 1) % NUM_EXTRA_POWER_STATS;
	}

	psPowerStats->ui64PreClockSpeedChangeMark = 0;
}

static INLINE void _PowerStatsPrintGroup(IMG_UINT32 *pui32Stats,
										 OSDI_IMPL_ENTRY *psEntry,
										 PVRSRV_POWER_STAT_TYPE eForced,
										 PVRSRV_POWER_STAT_TYPE ePowerOn)
{
	IMG_UINT32 ui32Index;

	ui32Index = GET_POWER_STAT_INDEX(eForced, ePowerOn, PRE_POWER, DEVICE);
	DIPrintf(psEntry, "  Pre-Device:  %9u\n", pui32Stats[ui32Index]);

	ui32Index = GET_POWER_STAT_INDEX(eForced, ePowerOn, PRE_POWER, SYSTEM);
	DIPrintf(psEntry, "  Pre-System:  %9u\n", pui32Stats[ui32Index]);

	ui32Index = GET_POWER_STAT_INDEX(eForced, ePowerOn, POST_POWER, SYSTEM);
	DIPrintf(psEntry, "  Post-System: %9u\n", pui32Stats[ui32Index]);

	ui32Index = GET_POWER_STAT_INDEX(eForced, ePowerOn, POST_POWER, DEVICE);
	DIPrintf(psEntry, "  Post-Device: %9u\n", pui32Stats[ui32Index]);
}

int PVRSRVPowerStatsPrintElements(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	PVRSRV_DEVICE_NODE *psDeviceNode = DIGetPrivData(psEntry);
	PVRSRV_POWER_DEV *psPowerDevice = psDeviceNode->psPowerDev;
	PVRSRV_POWER_STATS *psPowerStats;
	IMG_UINT32 *pui32Stats;
	IMG_UINT32 ui32Idx;

	PVR_UNREFERENCED_PARAMETER(pvData);

	if (psPowerDevice == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "Device not initialised when "
				 "reading power timing stats!"));
		return -EIO;
	}

	psPowerStats = &psPowerDevice->sPowerStats;

	pui32Stats = &psPowerStats->aui32PowerTimingStats[0];

	DIPrintf(psEntry, "Forced Power-on Transition (nanoseconds):\n");
	_PowerStatsPrintGroup(pui32Stats, psEntry, FORCED, POWER_ON);
	DIPrintf(psEntry, "\n");

	DIPrintf(psEntry, "Forced Power-off Transition (nanoseconds):\n");
	_PowerStatsPrintGroup(pui32Stats, psEntry, FORCED, POWER_OFF);
	DIPrintf(psEntry, "\n");

	DIPrintf(psEntry, "Not Forced Power-on Transition (nanoseconds):\n");
	_PowerStatsPrintGroup(pui32Stats, psEntry, NOT_FORCED, POWER_ON);
	DIPrintf(psEntry, "\n");

	DIPrintf(psEntry, "Not Forced Power-off Transition (nanoseconds):\n");
	_PowerStatsPrintGroup(pui32Stats, psEntry, NOT_FORCED, POWER_OFF);
	DIPrintf(psEntry, "\n");


	DIPrintf(psEntry, "FW bootup time (timer ticks): %u\n", psPowerStats->ui32FirmwareStartTimestamp);
	DIPrintf(psEntry, "Host Acknowledge Time for FW Idle Signal (timer ticks): %u\n", (IMG_UINT32)(psPowerStats->ui64FirmwareIdleDuration));
	DIPrintf(psEntry, "\n");

	DIPrintf(psEntry, "Last %d Clock Speed Change Timers (nanoseconds):\n", NUM_EXTRA_POWER_STATS);
	DIPrintf(psEntry, "Prepare DVFS\tDVFS Change\tPost DVFS\n");

	for (ui32Idx = psPowerStats->ui32ClockSpeedIndexStart;
		 ui32Idx != psPowerStats->ui32ClockSpeedIndexEnd;
		 ui32Idx = (ui32Idx + 1) % NUM_EXTRA_POWER_STATS)
	{
		DIPrintf(psEntry, "%12llu\t%11llu\t%9llu\n",
				 psPowerStats->asClockSpeedChanges[ui32Idx].ui64PreClockSpeedChangeDuration,
				 psPowerStats->asClockSpeedChanges[ui32Idx].ui64BetweenPreEndingAndPostStartingDuration,
				 psPowerStats->asClockSpeedChanges[ui32Idx].ui64PostClockSpeedChangeDuration);
	}

	return 0;
}

#else /* defined(PVRSRV_ENABLE_PROCESS_STATS) */

static void _InsertPowerTimeStatistic(PVRSRV_POWER_DEV *psPowerDevice,
									  IMG_UINT64 ui64SysStartTime, IMG_UINT64 ui64SysEndTime,
									  IMG_UINT64 ui64DevStartTime, IMG_UINT64 ui64DevEndTime,
									  IMG_BOOL bForced, IMG_BOOL bPowerOn, IMG_BOOL bPrePower)
{
	PVR_UNREFERENCED_PARAMETER(psPowerDevice);
	PVR_UNREFERENCED_PARAMETER(ui64SysStartTime);
	PVR_UNREFERENCED_PARAMETER(ui64SysEndTime);
	PVR_UNREFERENCED_PARAMETER(ui64DevStartTime);
	PVR_UNREFERENCED_PARAMETER(ui64DevEndTime);
	PVR_UNREFERENCED_PARAMETER(bForced);
	PVR_UNREFERENCED_PARAMETER(bPowerOn);
	PVR_UNREFERENCED_PARAMETER(bPrePower);
}

static void _InsertPowerTimeStatisticExtraPre(PVRSRV_POWER_DEV *psPowerDevice,
											  IMG_UINT64 ui64StartTimer,
											  IMG_UINT64 ui64Stoptimer)
{
	PVR_UNREFERENCED_PARAMETER(psPowerDevice);
	PVR_UNREFERENCED_PARAMETER(ui64StartTimer);
	PVR_UNREFERENCED_PARAMETER(ui64Stoptimer);
}

static void _InsertPowerTimeStatisticExtraPost(PVRSRV_POWER_DEV *psPowerDevice,
											   IMG_UINT64 ui64StartTimer,
											   IMG_UINT64 ui64StopTimer)
{
	PVR_UNREFERENCED_PARAMETER(psPowerDevice);
	PVR_UNREFERENCED_PARAMETER(ui64StartTimer);
	PVR_UNREFERENCED_PARAMETER(ui64StopTimer);
}
#endif

const char *PVRSRVSysPowerStateToString(PVRSRV_SYS_POWER_STATE eState)
{
	switch (eState) {
#define X(name, _) \
		case PVRSRV_SYS_POWER_STATE_##name: \
			return #name;
		_PVRSRV_SYS_POWER_STATES
#undef X
		default:
			return "unknown";
	}
}

const char *PVRSRVDevPowerStateToString(PVRSRV_DEV_POWER_STATE eState)
{
	switch (eState) {
		case PVRSRV_DEV_POWER_STATE_DEFAULT:
			return "DEFAULT";
		case PVRSRV_DEV_POWER_STATE_OFF:
			return "OFF";
		case PVRSRV_DEV_POWER_STATE_ON:
			return "ON";
		default:
			return "unknown";
	}
}

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

PVRSRV_ERROR PVRSRVSetDeviceCurrentPowerState(PVRSRV_POWER_DEV *psPowerDevice,
					PVRSRV_DEV_POWER_STATE eNewPowerState)
{
	if (psPowerDevice == NULL)
	{
		return PVRSRV_ERROR_INVALID_DEVICE;
	}

	OSAtomicWrite(&psPowerDevice->eCurrentPowerState, eNewPowerState);

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

	/* if pfnIsDefaultStateOff not provided or pfnIsDefaultStateOff(psPowerDev)
	 * is true (which means that the default state is OFF) then force idle. */
	if ((psPowerDev && psPowerDev->pfnForcedIdleRequest) &&
	    (pfnIsDefaultStateOff == NULL || pfnIsDefaultStateOff(psPowerDev)))
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
		PVRSRVSetSystemPowerState(psDeviceNode->psDevConfig, PVRSRV_SYS_POWER_STATE_ON);
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
 @Input		ePwrFlags : Power state change flags

 @Return	PVRSRV_ERROR

******************************************************************************/
static
PVRSRV_ERROR PVRSRVDevicePrePowerStateKM(PVRSRV_POWER_DEV		*psPowerDevice,
										 PVRSRV_DEV_POWER_STATE	eNewPowerState,
										 PVRSRV_POWER_FLAGS		ePwrFlags)
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
												  ePwrFlags);

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
												  ePwrFlags);

		ui64SysTimer2 = PVRSRVProcessStatsGetTimeNs();

		PVR_GOTO_IF_ERROR(eError, ErrRestorePowerState);
	}

	_InsertPowerTimeStatistic(psPowerDevice, ui64SysTimer1, ui64SysTimer2,
							 ui64DevTimer1, ui64DevTimer2,
							 BITMASK_HAS(ePwrFlags, PVRSRV_POWER_FLAGS_FORCED),
							 eNewPowerState == PVRSRV_DEV_POWER_STATE_ON,
							 IMG_TRUE);

	return PVRSRV_OK;

ErrRestorePowerState:
	/* In a situation where pfnDevicePrePower() succeeded but pfnSystemPrePower()
	 * failed we need to restore the device's power state from before the current
	 * request. Otherwise it will result in an inconsistency between the device's
	 * actual state and what the driver thinks the state is. */
	{
		PVRSRV_ERROR eError2 = PVRSRV_OK;

		if (psPowerDevice->pfnDevicePrePower != NULL)
		{
			/* Call the device's power callback. */
			eError2 = psPowerDevice->pfnDevicePrePower(psPowerDevice->hDevCookie,
			                                           eCurrentPowerState,
			                                           eNewPowerState,
			                                           ePwrFlags);
			PVR_LOG_IF_ERROR(eError2, "pfnDevicePrePower");
		}
		if (eError2 == PVRSRV_OK && psPowerDevice->pfnDevicePostPower != NULL)
		{
			/* Call the device's power callback. */
			eError2 = psPowerDevice->pfnDevicePostPower(psPowerDevice->hDevCookie,
			                                            eCurrentPowerState,
			                                            eNewPowerState,
			                                            ePwrFlags);
			PVR_LOG_IF_ERROR(eError2, "pfnDevicePostPower");
		}
	}

	return eError;
}

/*!
******************************************************************************

 @Function	PVRSRVDevicePostPowerStateKM

 @Description

 Perform device-specific processing required after a power transition

 @Input		psPowerDevice : Power device
 @Input		eNewPowerState : New power state
 @Input		ePwrFlags : Power state change flags

 @Return	PVRSRV_ERROR

******************************************************************************/
static
PVRSRV_ERROR PVRSRVDevicePostPowerStateKM(PVRSRV_POWER_DEV			*psPowerDevice,
										  PVRSRV_DEV_POWER_STATE	eNewPowerState,
										  PVRSRV_POWER_FLAGS		ePwrFlags)
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
												   ePwrFlags);

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
												   ePwrFlags);

		ui64DevTimer2 = PVRSRVProcessStatsGetTimeNs();

		PVR_RETURN_IF_ERROR(eError);
	}

	_InsertPowerTimeStatistic(psPowerDevice, ui64SysTimer1, ui64SysTimer2,
							 ui64DevTimer1, ui64DevTimer2,
							 BITMASK_HAS(ePwrFlags, PVRSRV_POWER_FLAGS_FORCED),
							 eNewPowerState == PVRSRV_DEV_POWER_STATE_ON,
							 IMG_FALSE);

	PVRSRVSetDeviceCurrentPowerState(psPowerDevice, eNewPowerState);

	return PVRSRV_OK;
}

PVRSRV_ERROR PVRSRVSetDevicePowerStateKM(PPVRSRV_DEVICE_NODE psDeviceNode,
										 PVRSRV_DEV_POWER_STATE eNewPowerState,
										 PVRSRV_POWER_FLAGS ePwrFlags)
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

	/* Call power function if the state change or if this is an OS request. */
	if (OSAtomicRead(&psPowerDevice->eCurrentPowerState) != eNewPowerState ||
	    BITMASK_ANY(ePwrFlags, PVRSRV_POWER_FLAGS_SUSPEND_REQ | PVRSRV_POWER_FLAGS_RESUME_REQ))
	{
		eError = PVRSRVDevicePrePowerStateKM(psPowerDevice,
											 eNewPowerState,
											 ePwrFlags);
		PVR_GOTO_IF_ERROR(eError, ErrorExit);

		eError = PVRSRVDevicePostPowerStateKM(psPowerDevice,
											  eNewPowerState,
											  ePwrFlags);
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
				 "%s: Transition to %d was denied, Flags=0x%08x",
				 __func__, eNewPowerState, ePwrFlags));
	}
	else
	{
		PVR_DPF((PVR_DBG_WARNING,
				 "%s: Transition to %d FAILED (%s)",
				 __func__, eNewPowerState, PVRSRVGetErrorString(eError)));
	}

	return eError;
}

PVRSRV_ERROR PVRSRVSetDeviceSystemPowerState(PPVRSRV_DEVICE_NODE psDeviceNode,
											 PVRSRV_SYS_POWER_STATE eNewSysPowerState,
											 PVRSRV_POWER_FLAGS ePwrFlags)
{
	PVRSRV_ERROR eError;
	IMG_UINT uiStage = 0;

	PVRSRV_DEV_POWER_STATE eNewDevicePowerState = _IsSystemStatePowered(eNewSysPowerState)
	    ? PVRSRV_DEV_POWER_STATE_DEFAULT : PVRSRV_DEV_POWER_STATE_OFF;

	/* If setting devices to default state, force idle all devices whose default state is off */
	PFN_SYS_DEV_IS_DEFAULT_STATE_OFF pfnIsDefaultStateOff =
	  (eNewDevicePowerState == PVRSRV_DEV_POWER_STATE_DEFAULT) ? PVRSRVDeviceIsDefaultStateOFF : NULL;

	/* Require a proper power state */
	if (eNewSysPowerState == PVRSRV_SYS_POWER_STATE_Unspecified)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Prevent simultaneous SetPowerStateKM calls */
	_PVRSRVForcedPowerLock(psDeviceNode);

	/* No power transition requested, so do nothing */
	if (eNewSysPowerState == psDeviceNode->eCurrentSysPowerState)
	{
		PVRSRVPowerUnlock(psDeviceNode);
		return PVRSRV_OK;
	}

	/* If the device is already off don't send the idle request. */
	if (psDeviceNode->eCurrentSysPowerState != PVRSRV_SYS_POWER_STATE_OFF)
	{
		eError = _PVRSRVDeviceIdleRequestKM(psDeviceNode, pfnIsDefaultStateOff,
		                                    IMG_TRUE, _PVRSRVForcedPowerLock);
		if (eError != PVRSRV_OK)
		{
			PVR_LOG_ERROR(eError, "_PVRSRVDeviceIdleRequestKM");
			uiStage = 1;
			goto ErrorExit;
		}
	}

	eError = PVRSRVSetDevicePowerStateKM(psDeviceNode, eNewDevicePowerState,
										 ePwrFlags | PVRSRV_POWER_FLAGS_FORCED);
	if (eError != PVRSRV_OK)
	{
		uiStage = 2;
		goto ErrorExit;
	}

	psDeviceNode->eCurrentSysPowerState = eNewSysPowerState;

	PVRSRVPowerUnlock(psDeviceNode);

	return PVRSRV_OK;

ErrorExit:
	PVRSRVPowerUnlock(psDeviceNode);

	PVR_DPF((PVR_DBG_ERROR, "%s: Transition from %s to %s FAILED (%s) at stage "
	         "%u. Dumping debug info.", __func__,
	         PVRSRVSysPowerStateToString(psDeviceNode->eCurrentSysPowerState),
	         PVRSRVSysPowerStateToString(eNewSysPowerState),
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
												  PVRSRV_POWER_FLAGS_FORCED);

		PVR_RETURN_IF_ERROR(eError);
	}

	if (psDevConfig->pfnPostPowerState != NULL)
	{
		eError = psDevConfig->pfnPostPowerState(psDevConfig->hSysData,
												   eNewSysPowerState,
												   eCurrentSysPowerState,
												   PVRSRV_POWER_FLAGS_FORCED);

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
	PVRSRVSetDeviceCurrentPowerState(psPowerDevice, eCurrentPowerState);
	psPowerDevice->eDefaultPowerState = eDefaultPowerState;

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	OSCachedMemSet(&psPowerDevice->sPowerStats, 0, sizeof(psPowerDevice->sPowerStats));
#endif

	psDeviceNode->psPowerDev = psPowerDevice;

	return PVRSRV_OK;
}

void PVRSRVRemovePowerDevice(PPVRSRV_DEVICE_NODE psDeviceNode)
{
	if (psDeviceNode->psPowerDev)
	{
		OSFreeMem(psDeviceNode->psPowerDev);
		psDeviceNode->psPowerDev = NULL;
	}
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
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_POWER_DEV *psPowerDevice = psDeviceNode->psPowerDev;
	IMG_UINT64 ui64StartTimer, ui64StopTimer;
	PVRSRV_DEV_POWER_STATE eCurrentPowerState;

	PVR_UNREFERENCED_PARAMETER(pvInfo);

	if (psPowerDevice == NULL)
	{
		return PVRSRV_OK;
	}

	ui64StartTimer = PVRSRVProcessStatsGetTimeUs();

	/* This mutex is released in PVRSRVDevicePostClockSpeedChange. */
	eError = PVRSRVPowerLock(psDeviceNode);
	PVR_LOG_RETURN_IF_ERROR(eError, "PVRSRVPowerLock");

	eCurrentPowerState = OSAtomicRead(&psPowerDevice->eCurrentPowerState);

	if ((eCurrentPowerState == PVRSRV_DEV_POWER_STATE_ON) && bIdleDevice)
	{
		/* We can change the clock speed if the device is either IDLE or OFF */
		eError = PVRSRVDeviceIdleRequestKM(psDeviceNode, NULL, IMG_TRUE);

		if (eError != PVRSRV_OK)
		{
			/* FW Can signal denied when busy with SPM or other work it can not idle */
			if (eError != PVRSRV_ERROR_DEVICE_IDLE_REQUEST_DENIED)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Error (%s) from %s()", __func__,
				         PVRSRVGETERRORSTRING(eError), "PVRSRVDeviceIdleRequestKM"));
			}
			if (eError != PVRSRV_ERROR_PWLOCK_RELEASED_REACQ_FAILED)
			{
				PVRSRVPowerUnlock(psDeviceNode);
			}
			return eError;
		}
	}

	eError = psPowerDevice->pfnPreClockSpeedChange(psPowerDevice->hDevCookie,
	                                               eCurrentPowerState);

	ui64StopTimer = PVRSRVProcessStatsGetTimeUs();

	_InsertPowerTimeStatisticExtraPre(psPowerDevice, ui64StartTimer, ui64StopTimer);

	return eError;
}

void
PVRSRVDevicePostClockSpeedChange(PPVRSRV_DEVICE_NODE psDeviceNode,
                                 IMG_BOOL            bIdleDevice,
                                 void*               pvInfo)
{
	PVRSRV_ERROR eError;
	PVRSRV_POWER_DEV *psPowerDevice = psDeviceNode->psPowerDev;
	IMG_UINT64 ui64StartTimer, ui64StopTimer;
	PVRSRV_DEV_POWER_STATE eCurrentPowerState;

	PVR_UNREFERENCED_PARAMETER(pvInfo);

	if (psPowerDevice == NULL)
	{
		return;
	}

	ui64StartTimer = PVRSRVProcessStatsGetTimeUs();

	eCurrentPowerState = OSAtomicRead(&psPowerDevice->eCurrentPowerState);

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

	/* This mutex was acquired in PVRSRVDevicePreClockSpeedChange. */
	PVRSRVPowerUnlock(psDeviceNode);

	OSAtomicIncrement(&psDeviceNode->iNumClockSpeedChanges);

	ui64StopTimer = PVRSRVProcessStatsGetTimeUs();

	_InsertPowerTimeStatisticExtraPost(psPowerDevice, ui64StartTimer, ui64StopTimer);
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
