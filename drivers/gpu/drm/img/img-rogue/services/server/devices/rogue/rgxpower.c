/*************************************************************************/ /*!
@File
@Title          Device specific power routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Device specific functions
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

#if defined(__linux__)
#include <linux/stddef.h>
#else
#include <stddef.h>
#endif

#include "rgxpower.h"
#include "rgxinit.h"
#include "rgx_fwif_km.h"
#include "rgxfwutils.h"
#include "pdump_km.h"
#include "pvr_debug.h"
#include "osfunc.h"
#include "rgxdebug.h"
#include "devicemem.h"
#include "devicemem_pdump.h"
#include "rgxtimecorr.h"
#include "devicemem_utils.h"
#include "htbserver.h"
#include "rgxstartstop.h"
#include "rgxfwimageutils.h"
#include "sync.h"
#include "rgxdefs_km.h"

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#include "process_stats.h"
#endif
#if defined(SUPPORT_LINUX_DVFS)
#include "pvr_dvfs_device.h"
#endif
#if defined(SUPPORT_VALIDATION) && defined(NO_HARDWARE) && defined(PDUMP)
#include "oskm_apphint.h"
#endif

static PVRSRV_ERROR RGXFWNotifyHostTimeout(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	RGXFWIF_KCCB_CMD sCmd;
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32CmdKCCBSlot;

	/* Send the Timeout notification to the FW */
	sCmd.eCmdType = RGXFWIF_KCCB_CMD_POW;
	sCmd.uCmdData.sPowData.ePowType = RGXFWIF_POW_FORCED_IDLE_REQ;
	sCmd.uCmdData.sPowData.uPowerReqData.ePowRequestType = RGXFWIF_POWER_HOST_TIMEOUT;

	eError = RGXSendCommandAndGetKCCBSlot(psDevInfo,
	                                      &sCmd,
	                                      PDUMP_FLAGS_NONE,
	                                      &ui32CmdKCCBSlot);

	return eError;
}

static void _RGXUpdateGPUUtilStats(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	RGXFWIF_GPU_UTIL_FWCB *psUtilFWCb;
	IMG_UINT64 *paui64StatsCounters;
	IMG_UINT64 ui64LastPeriod;
	IMG_UINT64 ui64LastState;
	IMG_UINT64 ui64LastTime;
	IMG_UINT64 ui64TimeNow;

	psUtilFWCb = psDevInfo->psRGXFWIfGpuUtilFWCb;
	paui64StatsCounters = &psUtilFWCb->aui64StatsCounters[0];

	OSLockAcquire(psDevInfo->hGPUUtilLock);

	ui64TimeNow = RGXFWIF_GPU_UTIL_GET_TIME(RGXTimeCorrGetClockns64(psDevInfo->psDeviceNode));

	/* Update counters to account for the time since the last update */
	ui64LastState  = RGXFWIF_GPU_UTIL_GET_STATE(psUtilFWCb->ui64LastWord);
	ui64LastTime   = RGXFWIF_GPU_UTIL_GET_TIME(psUtilFWCb->ui64LastWord);
	ui64LastPeriod = RGXFWIF_GPU_UTIL_GET_PERIOD(ui64TimeNow, ui64LastTime);
	paui64StatsCounters[ui64LastState] += ui64LastPeriod;

	/* Update state and time of the latest update */
	psUtilFWCb->ui64LastWord = RGXFWIF_GPU_UTIL_MAKE_WORD(ui64TimeNow, ui64LastState);

	OSLockRelease(psDevInfo->hGPUUtilLock);
}

static INLINE PVRSRV_ERROR RGXDoStop(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_ERROR eError;

#if defined(SUPPORT_TRUSTED_DEVICE) && !defined(NO_HARDWARE) && !defined(SUPPORT_SECURITY_VALIDATION)
	PVRSRV_DEVICE_CONFIG *psDevConfig = psDeviceNode->psDevConfig;
	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_OK);

	if (psDevConfig->pfnTDRGXStop == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXPrePowerState: TDRGXStop not implemented!"));
		return PVRSRV_ERROR_NOT_IMPLEMENTED;
	}

	eError = psDevConfig->pfnTDRGXStop(psDevConfig->hSysData);
#else
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_OK);

	eError = RGXStop(&psDevInfo->sLayerParams);
#endif

	return eError;
}

/*
	RGXPrePowerState
*/
PVRSRV_ERROR RGXPrePowerState(IMG_HANDLE				hDevHandle,
                              PVRSRV_DEV_POWER_STATE	eNewPowerState,
                              PVRSRV_DEV_POWER_STATE	eCurrentPowerState,
                              PVRSRV_POWER_FLAGS		ePwrFlags)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_DEVICE_NODE    *psDeviceNode = hDevHandle;

	if ((eNewPowerState != eCurrentPowerState) &&
	    (eNewPowerState != PVRSRV_DEV_POWER_STATE_ON))
	{
		PVRSRV_RGXDEV_INFO    *psDevInfo = psDeviceNode->pvDevice;
		RGXFWIF_KCCB_CMD      sPowCmd;
		IMG_UINT32            ui32CmdKCCBSlot;

		const RGXFWIF_SYSDATA *psFwSysData = psDevInfo->psRGXFWIfFwSysData;

		/* Send the Power off request to the FW */
		sPowCmd.eCmdType = RGXFWIF_KCCB_CMD_POW;
		sPowCmd.uCmdData.sPowData.ePowType = RGXFWIF_POW_OFF_REQ;
		sPowCmd.uCmdData.sPowData.uPowerReqData.bForced = BITMASK_HAS(ePwrFlags, PVRSRV_POWER_FLAGS_FORCED);

		eError = SyncPrimSet(psDevInfo->psPowSyncPrim, 0);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to set Power sync prim",
					__func__));
			return eError;
		}

		eError = RGXSendCommandAndGetKCCBSlot(psDevInfo,
		                                      &sPowCmd,
		                                      PDUMP_FLAGS_NONE,
		                                      &ui32CmdKCCBSlot);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to send Power off request",
					__func__));
			return eError;
		}

		/* Wait for the firmware to complete processing. It cannot use PVRSRVWaitForValueKM as it relies
		   on the EventObject which is signalled in this MISR */
		eError = RGXPollForGPCommandCompletion(psDeviceNode,
								  psDevInfo->psPowSyncPrim->pui32LinAddr,
								  0x1, 0xFFFFFFFF);

		/* Check the Power state after the answer */
		if (eError == PVRSRV_OK)
		{
			/* Finally, de-initialise some registers. */
			if (psFwSysData->ePowState == RGXFWIF_POW_OFF)
			{
#if !defined(NO_HARDWARE)
				IMG_UINT32 ui32idx;

				/* Driver takes the VZ Fw-KM connection down, preventing the
				 * firmware from submitting further interrupts */
				KM_SET_OS_CONNECTION(OFFLINE, psDevInfo);

#if defined(RGX_FW_IRQ_OS_COUNTERS)
				ui32idx = RGXFW_HOST_OS;
#else
				for_each_irq_cnt(ui32idx)
#endif /* RGX_FW_IRQ_OS_COUNTERS */
				{
					IMG_UINT32 ui32IrqCnt;

					get_irq_cnt_val(ui32IrqCnt, ui32idx, psDevInfo);

					/* Wait for the pending FW processor to host interrupts to come back. */
					eError = PVRSRVPollForValueKM(psDeviceNode,
					                              (IMG_UINT32 __iomem *)&psDevInfo->aui32SampleIRQCount[ui32idx],
					                              ui32IrqCnt,
					                              0xffffffff,
					                              POLL_FLAG_LOG_ERROR);

					if (eError != PVRSRV_OK)
					{
						PVR_DPF((PVR_DBG_ERROR,
								"%s: Wait for pending interrupts failed (DevID %u)." MSG_IRQ_CNT_TYPE " %u Host: %u, FW: %u",
								__func__,
								psDeviceNode->sDevId.ui32InternalID,
								ui32idx,
								psDevInfo->aui32SampleIRQCount[ui32idx],
								ui32IrqCnt));

						RGX_WaitForInterruptsTimeout(psDevInfo);
					}
				}
#endif /* NO_HARDWARE */

				/* Update GPU frequency and timer correlation related data */
				RGXTimeCorrEnd(psDeviceNode, RGXTIMECORR_EVENT_POWER);

				/* Update GPU state counters */
				_RGXUpdateGPUUtilStats(psDevInfo);

#if defined(SUPPORT_LINUX_DVFS)
				eError = SuspendDVFS(psDeviceNode);
				if (eError != PVRSRV_OK)
				{
					PVR_DPF((PVR_DBG_ERROR, "%s: Failed to suspend DVFS", __func__));
					return eError;
				}
#endif

				psDevInfo->bRGXPowered = IMG_FALSE;

				eError = RGXDoStop(psDeviceNode);
				if (eError != PVRSRV_OK)
				{
					/* Power down failures are treated as successful since the power was removed but logged. */
					PVR_DPF((PVR_DBG_WARNING, "%s: RGXDoStop failed (%s)",
							__func__, PVRSRVGetErrorString(eError)));
					psDevInfo->ui32ActivePMReqNonIdle++;
					eError = PVRSRV_OK;
				}
			}
			else
			{
				/* the sync was updated but the pow state isn't off -> the FW denied the transition */
				eError = PVRSRV_ERROR_DEVICE_POWER_CHANGE_DENIED;

				if (BITMASK_HAS(ePwrFlags, PVRSRV_POWER_FLAGS_FORCED))
				{	/* It is an error for a forced request to be denied */
					PVR_DPF((PVR_DBG_ERROR,
							 "%s: Failure to power off during a forced power off. FW: %d",
							 __func__, psFwSysData->ePowState));
				}
			}
		}
		else if (eError == PVRSRV_ERROR_TIMEOUT)
		{
			/* timeout waiting for the FW to ack the request: return timeout */
			PVR_DPF((PVR_DBG_WARNING,
					 "%s: Timeout waiting for powoff ack from the FW",
					 __func__));
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "%s: Error waiting for powoff ack from the FW (%s)",
					 __func__, PVRSRVGetErrorString(eError)));
			eError = PVRSRV_ERROR_DEVICE_POWER_CHANGE_FAILURE;
		}
	}

	return eError;
}

#if defined(SUPPORT_AUTOVZ)
static PVRSRV_ERROR _RGXWaitForGuestsToDisconnect(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_ERROR eError = PVRSRV_ERROR_TIMEOUT;
	IMG_UINT32 ui32FwTimeout = (20 * SECONDS_TO_MICROSECONDS);

	LOOP_UNTIL_TIMEOUT(ui32FwTimeout)
	{
		IMG_UINT32 ui32OSid;
		IMG_BOOL bGuestOnline = IMG_FALSE;

		for (ui32OSid = RGXFW_GUEST_OSID_START;
			 ui32OSid < RGX_NUM_OS_SUPPORTED; ui32OSid++)
		{
			RGXFWIF_CONNECTION_FW_STATE eGuestState = (RGXFWIF_CONNECTION_FW_STATE)
					psDevInfo->psRGXFWIfFwSysData->asOsRuntimeFlagsMirror[ui32OSid].bfOsState;

			if ((eGuestState == RGXFW_CONNECTION_FW_ACTIVE) ||
				(eGuestState == RGXFW_CONNECTION_FW_OFFLOADING))
			{
				bGuestOnline = IMG_TRUE;
				PVR_DPF((PVR_DBG_WARNING, "%s: Guest OS %u still online.", __func__, ui32OSid));
			}
		}

		if (!bGuestOnline)
		{
			/* Allow Guests to finish reading Connection state registers before disconnecting. */
			OSSleepms(100);

			PVR_DPF((PVR_DBG_WARNING, "%s: All Guest connections are down. "
									  "Host can power down the GPU.", __func__));
			eError = PVRSRV_OK;
			break;
		}
		else
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: Waiting for Guests to disconnect "
									  "before powering down GPU.", __func__));

			if (PVRSRVPwrLockIsLockedByMe(psDeviceNode))
			{
				/* Don't wait with the power lock held as this prevents the vz
				 * watchdog thread from keeping the fw-km connection alive. */
				PVRSRVPowerUnlock(psDeviceNode);
			}
		}

		OSSleepms(10);
	} END_LOOP_UNTIL_TIMEOUT();

	if (!PVRSRVPwrLockIsLockedByMe(psDeviceNode))
	{
		/* Take back power lock after waiting for Guests */
		eError = PVRSRVPowerLock(psDeviceNode);
	}

	return eError;
}
#endif /* defined(SUPPORT_AUTOVZ) */

/*
	RGXVzPrePowerState
*/
PVRSRV_ERROR RGXVzPrePowerState(IMG_HANDLE				hDevHandle,
                                PVRSRV_DEV_POWER_STATE	eNewPowerState,
                                PVRSRV_DEV_POWER_STATE	eCurrentPowerState,
                                PVRSRV_POWER_FLAGS		ePwrFlags)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_DEVICE_NODE *psDeviceNode = hDevHandle;

	PVR_LOG_RETURN_IF_FALSE((eNewPowerState != eCurrentPowerState), "no power change", eError);

	if (eNewPowerState != PVRSRV_DEV_POWER_STATE_ON)
	{
		/* powering down */
#if defined(SUPPORT_AUTOVZ)
		if (PVRSRV_VZ_MODE_IS(HOST) && (!psDeviceNode->bAutoVzFwIsUp))
		{
			/* The Host must ensure all Guest drivers have disconnected from the GPU before powering it down.
			 * Guest drivers regularly access hardware registers during runtime. If an attempt is made to
			 * access a GPU register while the GPU is down, the SoC might lock up. */
			eError = _RGXWaitForGuestsToDisconnect(psDeviceNode);
			PVR_LOG_RETURN_IF_ERROR(eError, "_RGXWaitForGuestsToDisconnect");

			/* Temporarily restore all power callbacks used by the driver to fully power down the GPU.
			 * Under AutoVz, power transitions requests (e.g. on driver deinitialisation and unloading)
			 * are generally ignored and the GPU power state is unaffected. Special power requests like
			 * those triggered by Suspend/Resume calls must reinstate the callbacks when needed. */
			PVRSRVSetPowerCallbacks(psDeviceNode, psDeviceNode->psPowerDev,
									&RGXVzPrePowerState, &RGXVzPostPowerState,
									psDeviceNode->psDevConfig->pfnPrePowerState,
									psDeviceNode->psDevConfig->pfnPostPowerState,
									&RGXForcedIdleRequest, &RGXCancelForcedIdleRequest);
		}
		else
		{
			PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

			if (KM_FW_CONNECTION_IS(ACTIVE, psDevInfo) &&
				KM_OS_CONNECTION_IS(ACTIVE, psDevInfo))
			{
				PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
				PVRSRV_ERROR eError = RGXFWSetFwOsState(psDevInfo, 0, RGXFWIF_OS_OFFLINE);
				PVR_LOG_RETURN_IF_ERROR(eError, "RGXFWSetFwOsState");
			}
		}
#endif
		PVR_DPF((PVR_DBG_WARNING, "%s: %s driver powering down: bAutoVzFwIsUp = %s",
								__func__, PVRSRV_VZ_MODE_IS(GUEST)? "GUEST" : "HOST",
								psDeviceNode->bAutoVzFwIsUp ? "TRUE" : "FALSE"));
	}
	else if (eCurrentPowerState != PVRSRV_DEV_POWER_STATE_ON)
	{
		/* powering up */
		PVR_DPF((PVR_DBG_WARNING, "%s: %s driver powering up: bAutoVzFwIsUp = %s",
								__func__, PVRSRV_VZ_MODE_IS(GUEST)? "GUEST" : "HOST",
								psDeviceNode->bAutoVzFwIsUp ? "TRUE" : "FALSE"));

	}

	if (!(PVRSRV_VZ_MODE_IS(GUEST) || (psDeviceNode->bAutoVzFwIsUp)))
	{
		/* call regular device power function */
		eError = RGXPrePowerState(hDevHandle, eNewPowerState, eCurrentPowerState, ePwrFlags);
	}

	return eError;
}

/*
	RGXVzPostPowerState
*/
PVRSRV_ERROR RGXVzPostPowerState(IMG_HANDLE				hDevHandle,
                                 PVRSRV_DEV_POWER_STATE	eNewPowerState,
                                 PVRSRV_DEV_POWER_STATE	eCurrentPowerState,
                                 PVRSRV_POWER_FLAGS		ePwrFlags)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_DEVICE_NODE *psDeviceNode = hDevHandle;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

	PVR_LOG_RETURN_IF_FALSE((eNewPowerState != eCurrentPowerState), "no power change", eError);

	if (!(PVRSRV_VZ_MODE_IS(GUEST) || (psDeviceNode->bAutoVzFwIsUp)))
	{
		/* call regular device power function */
		eError = RGXPostPowerState(hDevHandle, eNewPowerState, eCurrentPowerState, ePwrFlags);
	}

	if (eNewPowerState != PVRSRV_DEV_POWER_STATE_ON)
	{
		/* powering down */
		PVR_LOG_RETURN_IF_FALSE((!psDeviceNode->bAutoVzFwIsUp), "AutoVz Fw active, power not changed", eError);
		PVR_DPF((PVR_DBG_WARNING, "%s: %s driver powering down: bAutoVzFwIsUp = %s",
								__func__, PVRSRV_VZ_MODE_IS(GUEST)? "GUEST" : "HOST",
								psDeviceNode->bAutoVzFwIsUp ? "TRUE" : "FALSE"));

#if !defined(SUPPORT_AUTOVZ_HW_REGS)
		/* The connection states must be reset on a GPU power cycle. If the states are kept
		 * in hardware scratch registers, they will be cleared on power down. When using shared
		 * memory the connection data must be explicitly cleared by the driver. */
		OSCachedMemSetWMB(psDevInfo->psRGXFWIfConnectionCtl, 0, sizeof(RGXFWIF_CONNECTION_CTL));
#endif /* defined(SUPPORT_AUTOVZ) && !defined(SUPPORT_AUTOVZ_HW_REGS) */

		if (PVRSRV_VZ_MODE_IS(GUEST) || (psDeviceNode->bAutoVzFwIsUp))
		{
#if defined(SUPPORT_AUTOVZ)
			/* AutoVz Guests attempting to suspend have updated their connections earlier in RGXVzPrePowerState.
			 * Skip this redundant register write, as the Host could have powered down the GPU by now. */
			if (psDeviceNode->bAutoVzFwIsUp)
#endif
			{
				/* Take the VZ connection down to prevent firmware from submitting further interrupts */
				KM_SET_OS_CONNECTION(OFFLINE, psDevInfo);
			}
			/* Power transition callbacks were not executed, update RGXPowered flag here */
			psDevInfo->bRGXPowered = IMG_FALSE;
		}
	}
	else if (eCurrentPowerState != PVRSRV_DEV_POWER_STATE_ON)
	{
		/* powering up */
		IMG_UINT32 ui32FwTimeout = (3 * SECONDS_TO_MICROSECONDS);
		volatile IMG_BOOL *pbUpdatedFlag = &psDevInfo->psRGXFWIfOsInit->sRGXCompChecks.bUpdated;

		PVR_DPF((PVR_DBG_WARNING, "%s: %s driver powering up: bAutoVzFwIsUp = %s",
								__func__, PVRSRV_VZ_MODE_IS(GUEST)? "GUEST" : "HOST",
								psDeviceNode->bAutoVzFwIsUp ? "TRUE" : "FALSE"));
		if (PVRSRV_VZ_MODE_IS(GUEST))
		{
			/* Guests don't execute the power transition callbacks, so update their RGXPowered flag here */
			psDevInfo->bRGXPowered = IMG_TRUE;

#if defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS)
			/* Guest drivers expect the firmware to have set its end of the
			 * connection to Ready state by now. Poll indefinitely otherwise. */
			if (!KM_FW_CONNECTION_IS(READY, psDevInfo))
			{
				PVR_DPF((PVR_DBG_WARNING, "%s: Firmware Connection is not in Ready state. Waiting for Firmware ...", __func__));
			}
			while (!KM_FW_CONNECTION_IS(READY, psDevInfo))
			{
				OSSleepms(10);
			}
			PVR_DPF((PVR_DBG_WARNING, "%s: Firmware Connection is Ready. Initialisation proceeding.", __func__));
#endif /* RGX_VZ_STATIC_CARVEOUT_FW_HEAPS */

			/* Guests can only access the register holding the connection states,
			 * after the GPU is confirmed to be powered up */
			KM_SET_OS_CONNECTION(READY, psDevInfo);

			OSWriteDeviceMem32WithWMB(pbUpdatedFlag, IMG_FALSE);

			/* Kick an initial dummy command to make the firmware initialise all
			 * its internal guest OS data structures and compatibility information.
			 * Use the lower-level RGXSendCommandAndGetKCCBSlot() for the job, to make
			 * sure only 1 KCCB command is issued to the firmware.
			 * The default RGXFWHealthCheckCmd() prefaces each HealthCheck command with
			 * a pre-kick cache command which can interfere with the FW-KM init handshake. */
			{
				RGXFWIF_KCCB_CMD sCmpKCCBCmd;
				sCmpKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_HEALTH_CHECK;

				eError = RGXSendCommandAndGetKCCBSlot(psDevInfo, &sCmpKCCBCmd, PDUMP_FLAGS_CONTINUOUS, NULL);
				PVR_LOG_RETURN_IF_ERROR(eError, "RGXSendCommandAndGetKCCBSlot()");
			}
		}
		else
		{
			KM_SET_OS_CONNECTION(READY, psDevInfo);

			/* Disable power callbacks that should not be run on virtualised drivers after the GPU
			 * is fully initialised: system layer pre/post functions and driver idle requests.
			 * The original device RGX Pre/Post functions are called from this Vz wrapper. */
			PVRSRVSetPowerCallbacks(psDeviceNode, psDeviceNode->psPowerDev,
									&RGXVzPrePowerState, &RGXVzPostPowerState,
									NULL, NULL, NULL, NULL);

#if defined(SUPPORT_AUTOVZ)
			/* During first-time boot the flag is set here, while subsequent reboots will already
			 * have set it earlier in RGXInit. Set to true from this point onwards in any case. */
			psDeviceNode->bAutoVzFwIsUp = IMG_TRUE;
#endif
		}

		/* Wait for the firmware to accept and enable the connection with this OS by setting its state to Active */
		while (!KM_FW_CONNECTION_IS(ACTIVE, psDevInfo))
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: Firmware Connection is not in Active state. Waiting for Firmware ...", __func__));
			OSSleepms(100);
		}
		PVR_DPF((PVR_DBG_WARNING, "%s: Firmware Connection is Active. Initialisation proceeding.", __func__));

		/* poll on the Firmware supplying the compatibility data */
		LOOP_UNTIL_TIMEOUT(ui32FwTimeout)
		{
			if (*pbUpdatedFlag)
			{
				break;
			}
			OSSleepms(10);
		} END_LOOP_UNTIL_TIMEOUT();

		PVR_LOG_RETURN_IF_FALSE(*pbUpdatedFlag, "Firmware does not respond with compatibility data. ", PVRSRV_ERROR_TIMEOUT);

		KM_SET_OS_CONNECTION(ACTIVE, psDevInfo);
	}

	return PVRSRV_OK;
}

#if defined(TRACK_FW_BOOT)
static INLINE void RGXCheckFWBootStage(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	FW_BOOT_STAGE eStage;

#if defined(RGX_FEATURE_META_MAX_VALUE_IDX)
	if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META))
	{
		/* Boot stage temporarily stored to the register below */
		eStage = OSReadHWReg32(psDevInfo->pvRegsBaseKM,
		                       RGX_FW_BOOT_STAGE_REGISTER);
	}
	else
#endif
#if defined(RGX_FEATURE_RISCV_FW_PROCESSOR_BIT_MASK)
	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, RISCV_FW_PROCESSOR))
	{
		eStage = OSReadHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_SCRATCH14);
	}
	else
#endif
	{
		IMG_BYTE *pbBootData;

		if (PVRSRV_OK != DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWDataMemDesc,
		                                          (void**)&pbBootData))
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Could not acquire pointer to FW boot stage", __func__));
			eStage = FW_BOOT_STAGE_NOT_AVAILABLE;
		}
		else
		{
			pbBootData += RGXGetFWImageSectionOffset(NULL, MIPS_BOOT_DATA);

			eStage = *(FW_BOOT_STAGE*)&pbBootData[RGXMIPSFW_BOOT_STAGE_OFFSET];

			if (eStage == FW_BOOT_STAGE_TLB_INIT_FAILURE)
			{
				RGXMIPSFW_BOOT_DATA *psBootData =
					(RGXMIPSFW_BOOT_DATA*) (pbBootData + RGXMIPSFW_BOOTLDR_CONF_OFFSET);

				PVR_LOG(("MIPS TLB could not be initialised. Boot data info:"
						 " num PT pages %u, log2 PT page size %u, PT page addresses"
						 " %"IMG_UINT64_FMTSPECx " %"IMG_UINT64_FMTSPECx
						 " %"IMG_UINT64_FMTSPECx " %"IMG_UINT64_FMTSPECx,
						 psBootData->ui32PTNumPages,
						 psBootData->ui32PTLog2PageSize,
						 psBootData->aui64PTPhyAddr[0U],
						 psBootData->aui64PTPhyAddr[1U],
						 psBootData->aui64PTPhyAddr[2U],
						 psBootData->aui64PTPhyAddr[3U]));
			}

			DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWDataMemDesc);
		}
	}

	PVR_LOG(("%s: FW reached boot stage %i/%i.",
	         __func__, eStage, FW_BOOT_INIT_DONE));
}
#endif

static INLINE PVRSRV_ERROR RGXDoStart(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_ERROR eError;

#if defined(SUPPORT_TRUSTED_DEVICE) && !defined(NO_HARDWARE) && !defined(SUPPORT_SECURITY_VALIDATION)
	PVRSRV_DEVICE_CONFIG *psDevConfig = psDeviceNode->psDevConfig;

	if (psDevConfig->pfnTDRGXStart == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXPostPowerState: TDRGXStart not implemented!"));
		return PVRSRV_ERROR_NOT_IMPLEMENTED;
	}

	eError = psDevConfig->pfnTDRGXStart(psDevConfig->hSysData);
#else
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

	eError = RGXStart(&psDevInfo->sLayerParams);
#endif

	return eError;
}


#if defined(NO_HARDWARE) && defined(PDUMP)

#if 0
#include "emu_cr_defs.h"
#else
#define EMU_CR_SYSTEM_IRQ_STATUS                          (0x00E0U)
/* IRQ is officially defined [8 .. 0] but here we split out the old deprecated single irq. */
#define EMU_CR_SYSTEM_IRQ_STATUS_IRQ_CLRMSK               (IMG_UINT64_C(0XFFFFFFFFFFFFFE01))
#define EMU_CR_SYSTEM_IRQ_STATUS_OLD_IRQ_CLRMSK           (IMG_UINT64_C(0XFFFFFFFFFFFFFFFE))
#endif

static PVRSRV_ERROR
_ValidateIrqs(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	IMG_UINT32 ui32OSid;
	PDUMP_FLAGS_T ui32PDumpFlags = PDUMP_FLAGS_CONTINUOUS;

	/* Check if the Validation IRQ flag is set */
	if ((psDevInfo->psRGXFWIfFwSysData->ui32ConfigFlags & RGXFWIF_INICFG_VALIDATE_IRQ) == 0)
	{
		return PVRSRV_OK;
	}

	PDUMPIF(psDevInfo->psDeviceNode, "IMG_PVR_TESTBENCH", ui32PDumpFlags);
	PDUMPCOMMENTWITHFLAGS(psDevInfo->psDeviceNode, ui32PDumpFlags,
	                      "Poll for TB irq status to be set (irqs signalled)...");
	PDUMPREGPOL(psDevInfo->psDeviceNode,
	            RGX_TB_PDUMPREG_NAME,
	            EMU_CR_SYSTEM_IRQ_STATUS,
	            ~EMU_CR_SYSTEM_IRQ_STATUS_IRQ_CLRMSK,
	            ~EMU_CR_SYSTEM_IRQ_STATUS_IRQ_CLRMSK,
	            ui32PDumpFlags,
	            PDUMP_POLL_OPERATOR_EQUAL);

	PDUMPCOMMENTWITHFLAGS(psDevInfo->psDeviceNode, ui32PDumpFlags,
	                      "... and then clear them");
	for (ui32OSid = 0; ui32OSid < RGXFW_MAX_NUM_OS; ui32OSid++)
	{
		PDUMPREG32(psDevInfo->psDeviceNode,
		           RGX_PDUMPREG_NAME,
		           RGX_CR_IRQ_OS0_EVENT_CLEAR + ui32OSid * 0x10000,
		           RGX_CR_IRQ_OS0_EVENT_CLEAR_MASKFULL,
		           ui32PDumpFlags);
	}

	PDUMPFI(psDevInfo->psDeviceNode, "IMG_PVR_TESTBENCH", ui32PDumpFlags);

	/* Poll on all the interrupt status registers for all OSes */
	PDUMPCOMMENTWITHFLAGS(psDevInfo->psDeviceNode, ui32PDumpFlags,
	                      "Validate Interrupt lines.");

	for (ui32OSid = 0; ui32OSid < RGXFW_MAX_NUM_OS; ui32OSid++)
	{
		PDUMPREGPOL(psDevInfo->psDeviceNode, RGX_PDUMPREG_NAME,
		            RGX_CR_IRQ_OS0_EVENT_STATUS + ui32OSid * 0x10000,
		            0x0,
		            0xFFFFFFFF,
		            ui32PDumpFlags,
		            PDUMP_POLL_OPERATOR_EQUAL);
	}

	return PVRSRV_OK;
}
#endif /* defined(NO_HARDWARE) && defined(PDUMP) */

#if defined(SUPPORT_GPUVIRT_VALIDATION) && !defined(NO_HARDWARE)
/*
 * To validate the MTS unit we do the following:
 *  - Immediately after firmware loading for each OSID
 *    - Write the OSid to a memory location shared with FW
 *    - Kick the register of that OSid
 *         (Uncounted, DM 0)
 *    - FW clears the memory location if OSid matches
 *    - Host checks that memory location is cleared
 *
 *  See firmware/devices/rgx/rgxfw_bg.c
 */
static PVRSRV_ERROR RGXVirtualisationPowerupSidebandTest(PVRSRV_DEVICE_NODE	 *psDeviceNode,
														 RGXFWIF_SYSINIT *psFwSysInit,
														 PVRSRV_RGXDEV_INFO	 *psDevInfo)
{
	IMG_UINT32 ui32ScheduleRegister;
	IMG_UINT32 ui32OSid;
	IMG_UINT32 ui32KickType;
	IMG_UINT32 ui32OsRegBanksMapped = (psDeviceNode->psDevConfig->ui32RegsSize / RGX_VIRTUALISATION_REG_SIZE_PER_OS);

	/* Nothing to do if the device does not support GPU_VIRTUALISATION */
	if (!PVRSRV_IS_FEATURE_SUPPORTED(psDeviceNode, GPU_VIRTUALISATION))
	{
		return PVRSRV_OK;
	}

	PVR_DPF((PVR_DBG_MESSAGE, "Testing per-os kick registers:"));

	ui32OsRegBanksMapped = MIN(ui32OsRegBanksMapped, GPUVIRT_VALIDATION_NUM_OS);

	if (ui32OsRegBanksMapped != RGXFW_MAX_NUM_OS)
	{
		PVR_DPF((PVR_DBG_WARNING, "The register bank mapped into kernel VA does not cover all OS' registers:"));
		PVR_DPF((PVR_DBG_WARNING, "Maximum OS count = %d / Per-os register banks mapped = %d", RGXFW_MAX_NUM_OS, ui32OsRegBanksMapped));
		PVR_DPF((PVR_DBG_WARNING, "Only first %d MTS registers will be tested", ui32OsRegBanksMapped));
	}

	ui32KickType = RGX_CR_MTS_SCHEDULE_DM_DM0 | RGX_CR_MTS_SCHEDULE_TASK_NON_COUNTED;

	for (ui32OSid = 0; ui32OSid < ui32OsRegBanksMapped; ui32OSid++)
	{
		/* set Test field */
		psFwSysInit->ui32OSKickTest = (ui32OSid << RGXFWIF_KICK_TEST_OSID_SHIFT) | RGXFWIF_KICK_TEST_ENABLED_BIT;

#if defined(PDUMP)
		DevmemPDumpLoadMem(psDevInfo->psRGXFWIfSysInitMemDesc,
						   offsetof(RGXFWIF_SYSINIT, ui32OSKickTest),
						   sizeof(psFwSysInit->ui32OSKickTest),
						   PDUMP_FLAGS_CONTINUOUS);
#endif

		/* Force a read-back to memory to avoid posted writes on certain buses */
		OSWriteMemoryBarrier(&psFwSysInit->ui32OSKickTest);

		/* kick register */
		ui32ScheduleRegister = RGX_CR_MTS_SCHEDULE + (ui32OSid * RGX_VIRTUALISATION_REG_SIZE_PER_OS);
		PVR_DPF((PVR_DBG_MESSAGE, "  Testing OS: %u, Kick Reg: %X",
				 ui32OSid,
				 ui32ScheduleRegister));
		OSWriteHWReg32(psDevInfo->pvRegsBaseKM, ui32ScheduleRegister, ui32KickType);
		OSMemoryBarrier((IMG_BYTE*) psDevInfo->pvRegsBaseKM + ui32ScheduleRegister);

#if defined(PDUMP)
		PDUMPCOMMENTWITHFLAGS(psDevInfo->psDeviceNode, PDUMP_FLAGS_CONTINUOUS, "VZ sideband test, kicking MTS register %u", ui32OSid);

		PDUMPREG32(psDeviceNode, RGX_PDUMPREG_NAME,
				ui32ScheduleRegister, ui32KickType, PDUMP_FLAGS_CONTINUOUS);

		DevmemPDumpDevmemPol32(psDevInfo->psRGXFWIfSysInitMemDesc,
							   offsetof(RGXFWIF_SYSINIT, ui32OSKickTest),
							   0,
							   0xFFFFFFFF,
							   PDUMP_POLL_OPERATOR_EQUAL,
							   PDUMP_FLAGS_CONTINUOUS);
#endif

		/* Wait test enable bit to be unset */
		if (PVRSRVPollForValueKM(psDeviceNode,
								 (IMG_UINT32 *)&psFwSysInit->ui32OSKickTest,
								 0,
								 RGXFWIF_KICK_TEST_ENABLED_BIT,
								 POLL_FLAG_LOG_ERROR | POLL_FLAG_DEBUG_DUMP) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "Testing OS %u kick register failed: firmware did not clear test location (contents: 0x%X)",
					 ui32OSid,
					 psFwSysInit->ui32OSKickTest));

			return PVRSRV_ERROR_TIMEOUT;
		}

		/* Check that the value is what we expect */
		if (psFwSysInit->ui32OSKickTest != 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "Testing OS %u kick register failed: firmware wrote 0x%X to test location",
					 ui32OSid,
					 psFwSysInit->ui32OSKickTest));
			return PVRSRV_ERROR_INIT_FAILURE;
		}

		PVR_DPF((PVR_DBG_MESSAGE, "    PASS"));
	}

	PVR_LOG(("MTS passed sideband tests"));
	return PVRSRV_OK;
}
#endif /* defined(SUPPORT_GPUVIRT_VALIDATION) && !defined(NO_HARDWARE) */

#if defined(SUPPORT_VALIDATION) && defined(NO_HARDWARE) && defined(PDUMP)
#define SCRATCH_VALUE  (0x12345678U)

static void RGXRiscvDebugModuleTest(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	void *pvAppHintState = NULL;
	IMG_UINT32 ui32AppHintDefault = 0;
	IMG_BOOL bRunRiscvDmiTest;

	IMG_UINT32 *pui32FWCode = NULL;
	PVRSRV_ERROR eError;

	OSCreateKMAppHintState(&pvAppHintState);
	OSGetKMAppHintBOOL(APPHINT_NO_DEVICE, pvAppHintState, RiscvDmiTest,
	                   &ui32AppHintDefault, &bRunRiscvDmiTest);
	OSFreeKMAppHintState(pvAppHintState);

	if (bRunRiscvDmiTest == IMG_FALSE)
	{
		return;
	}

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWCodeMemDesc, (void **)&pui32FWCode);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Error acquiring FW code memory pointer (%s)",
		         __func__,
		         PVRSRVGetErrorString(eError)));
	}

	PDumpIfKM(psDevInfo->psDeviceNode, "ENABLE_RISCV_DMI_TEST", PDUMP_FLAGS_CONTINUOUS);
	PDUMPCOMMENTWITHFLAGS(psDevInfo->psDeviceNode, PDUMP_FLAGS_CONTINUOUS, "DMI_TEST BEGIN");

	RGXRiscvHalt(psDevInfo);

	/*
	 * Test RISC-V register reads/writes.
	 * RGXRiscv[Write/Poll]Reg are used to access internal RISC-V registers
	 * via debug module.
	 */

	/* Write RISC-V mscratch register */
	RGXRiscvWriteReg(psDevInfo, RGXRISCVFW_MSCRATCH_ADDR, SCRATCH_VALUE);
	/* Read RISC-V misa register (compare against default standard value) */
	RGXRiscvPollReg(psDevInfo,  RGXRISCVFW_MISA_ADDR,     RGXRISCVFW_MISA_VALUE);
	/* Read RISC-V mscratch register (compare against previously written value) */
	RGXRiscvPollReg(psDevInfo,  RGXRISCVFW_MSCRATCH_ADDR, SCRATCH_VALUE);

	/*
	 * Test RISC-V memory reads/writes.
	 * RGXRiscv[Write/Poll]Mem are used to access system memory via debug module
	 * (from RISC-V point of view).
	 */

	if (pui32FWCode != NULL)
	{
		IMG_UINT32 ui32Tmp;

		/* Acquire pointer to FW code (bootloader) */
		pui32FWCode += RGXGetFWImageSectionOffset(NULL, RISCV_UNCACHED_CODE) / sizeof(IMG_UINT32);
		/* Save FW code at address (bootloader) */
		ui32Tmp = *pui32FWCode;

		/* Write FW code at address (bootloader) */
		RGXWriteFWModuleAddr(psDevInfo, RGXRISCVFW_BOOTLDR_CODE_BASE,     SCRATCH_VALUE);
		/* Read FW code at address (bootloader + 4) (compare against value read from Host) */
		RGXRiscvPollMem(psDevInfo,  RGXRISCVFW_BOOTLDR_CODE_BASE + 4, *(pui32FWCode + 1));
		/* Read FW code at address (bootloader) (compare against previously written value) */
		RGXRiscvPollMem(psDevInfo,  RGXRISCVFW_BOOTLDR_CODE_BASE,     SCRATCH_VALUE);
		/* Restore FW code at address (bootloader) */
		RGXWriteFWModuleAddr(psDevInfo, RGXRISCVFW_BOOTLDR_CODE_BASE,     ui32Tmp);

		DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWCodeMemDesc);
	}

	/*
	 * Test GPU register reads/writes.
	 * RGXRiscv[Write/Poll]Mem are used to access GPU registers via debug module
	 * (from RISC-V point of view).
	 * Note that system memory and GPU register accesses both use the same
	 * debug module interface, targeting different address ranges.
	 */

	/* Write SCRATCH0 from the Host */
	PDUMPREG32(psDevInfo->psDeviceNode, RGX_PDUMPREG_NAME, RGX_CR_SCRATCH0,
	           SCRATCH_VALUE, PDUMP_FLAGS_CONTINUOUS);
	/* Read SCRATCH0 */
	RGXRiscvPollMem(psDevInfo,  RGXRISCVFW_SOCIF_BASE | RGX_CR_SCRATCH0, SCRATCH_VALUE);
	/* Write SCRATCH0 */
	RGXWriteFWModuleAddr(psDevInfo, RGXRISCVFW_SOCIF_BASE | RGX_CR_SCRATCH0, ~SCRATCH_VALUE);
	/* Read SCRATCH0 from the Host */
	PDUMPREGPOL(psDevInfo->psDeviceNode, RGX_PDUMPREG_NAME, RGX_CR_SCRATCH0,
	            ~SCRATCH_VALUE, 0xFFFFFFFFU,
	            PDUMP_FLAGS_CONTINUOUS, PDUMP_POLL_OPERATOR_EQUAL);

	RGXRiscvResume(psDevInfo);

	PDUMPCOMMENTWITHFLAGS(psDevInfo->psDeviceNode, PDUMP_FLAGS_CONTINUOUS, "DMI_TEST END");
	PDumpFiKM(psDevInfo->psDeviceNode, "ENABLE_RISCV_DMI_TEST", PDUMP_FLAGS_CONTINUOUS);
}
#endif

/*
	RGXPostPowerState
*/
PVRSRV_ERROR RGXPostPowerState(IMG_HANDLE				hDevHandle,
                               PVRSRV_DEV_POWER_STATE	eNewPowerState,
                               PVRSRV_DEV_POWER_STATE	eCurrentPowerState,
                               PVRSRV_POWER_FLAGS		ePwrFlags)
{
	PVRSRV_DEVICE_NODE	 *psDeviceNode = hDevHandle;
	PVRSRV_RGXDEV_INFO	 *psDevInfo = psDeviceNode->pvDevice;

	if ((eNewPowerState != eCurrentPowerState) &&
	    (eCurrentPowerState != PVRSRV_DEV_POWER_STATE_ON))
	{
		PVRSRV_ERROR		 eError;

		if (eCurrentPowerState == PVRSRV_DEV_POWER_STATE_OFF)
		{
			/* Update timer correlation related data */
			RGXTimeCorrBegin(psDeviceNode, RGXTIMECORR_EVENT_POWER);

			/* Update GPU state counters */
			_RGXUpdateGPUUtilStats(psDevInfo);

			eError = RGXDoStart(psDeviceNode);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "RGXPostPowerState: RGXDoStart failed"));
				return eError;
			}

			OSMemoryBarrier(NULL);

			/*
			 * Check whether the FW has started by polling on bFirmwareStarted flag
			 */
			if (PVRSRVPollForValueKM(psDeviceNode,
			                         (IMG_UINT32 __iomem *)&psDevInfo->psRGXFWIfSysInit->bFirmwareStarted,
			                         IMG_TRUE,
			                         0xFFFFFFFF,
			                         POLL_FLAG_LOG_ERROR | POLL_FLAG_DEBUG_DUMP) != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "RGXPostPowerState: Polling for 'FW started' flag failed."));
				eError = PVRSRV_ERROR_TIMEOUT;

#if defined(TRACK_FW_BOOT)
				RGXCheckFWBootStage(psDevInfo);
#endif

				/*
				 * When bFirmwareStarted fails some info may be gained by doing the following
				 * debug dump but unfortunately it could be potentially dangerous if the reason
				 * for not booting is the GPU power is not ON. However, if we have reached this
				 * point the System Layer has returned without errors, we assume the GPU power
				 * is indeed ON.
				 */
				RGXDumpRGXDebugSummary(NULL, NULL, psDeviceNode->pvDevice, IMG_TRUE);
				RGXDumpRGXRegisters(NULL, NULL, psDeviceNode->pvDevice);

				return eError;
			}

#if defined(PDUMP)
			PDUMPCOMMENTWITHFLAGS(psDeviceNode, PDUMP_FLAGS_CONTINUOUS, "Wait for the Firmware to start.");
			eError = DevmemPDumpDevmemPol32(psDevInfo->psRGXFWIfSysInitMemDesc,
			                                offsetof(RGXFWIF_SYSINIT, bFirmwareStarted),
			                                IMG_TRUE,
			                                0xFFFFFFFFU,
			                                PDUMP_POLL_OPERATOR_EQUAL,
			                                PDUMP_FLAGS_CONTINUOUS);

			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,
				         "RGXPostPowerState: problem pdumping POL for psRGXFWIfSysInitMemDesc (%d)",
				         eError));
				return eError;
			}

#if defined(NO_HARDWARE) && defined(PDUMP)
			eError = _ValidateIrqs(psDevInfo);
			if (eError != PVRSRV_OK)
			{
				return eError;
			}
#endif
#endif

#if defined(SUPPORT_GPUVIRT_VALIDATION) && !defined(NO_HARDWARE)
			eError = RGXVirtualisationPowerupSidebandTest(psDeviceNode, psDevInfo->psRGXFWIfSysInit, psDevInfo);
			if (eError != PVRSRV_OK)
			{
				return eError;
			}
#endif

#if defined(SUPPORT_VALIDATION) && defined(NO_HARDWARE) && defined(PDUMP)
			RGXRiscvDebugModuleTest(psDevInfo);
#endif

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
			SetFirmwareStartTime(psDevInfo->psRGXFWIfSysInit->ui32FirmwareStartedTimeStamp);
#endif

			HTBSyncPartitionMarker(psDevInfo->psRGXFWIfSysInit->ui32MarkerVal);

			psDevInfo->bRGXPowered = IMG_TRUE;

#if defined(SUPPORT_LINUX_DVFS)
			eError = ResumeDVFS(psDeviceNode);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "RGXPostPowerState: Failed to resume DVFS"));
				return eError;
			}
#endif
		}
	}

	PDUMPCOMMENT(psDeviceNode,
	             "RGXPostPowerState: Current state: %d, New state: %d",
	             eCurrentPowerState, eNewPowerState);

	return PVRSRV_OK;
}

/*
	RGXPreClockSpeedChange
*/
PVRSRV_ERROR RGXPreClockSpeedChange(IMG_HANDLE				hDevHandle,
                                    PVRSRV_DEV_POWER_STATE	eCurrentPowerState)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_DEVICE_NODE *psDeviceNode = hDevHandle;
	const PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	const RGX_DATA *psRGXData = (RGX_DATA*)psDeviceNode->psDevConfig->hDevData;
	const RGXFWIF_SYSDATA *psFwSysData = psDevInfo->psRGXFWIfFwSysData;
	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_OK);
	PVR_UNREFERENCED_PARAMETER(psRGXData);

	PVR_DPF((PVR_DBG_MESSAGE, "RGXPreClockSpeedChange: RGX clock speed was %uHz",
			psRGXData->psRGXTimingInfo->ui32CoreClockSpeed));

	if ((eCurrentPowerState != PVRSRV_DEV_POWER_STATE_OFF) &&
	    (psFwSysData->ePowState != RGXFWIF_POW_OFF))
	{
		/* Update GPU frequency and timer correlation related data */
		RGXTimeCorrEnd(psDeviceNode, RGXTIMECORR_EVENT_DVFS);
	}

	return eError;
}

/*
	RGXPostClockSpeedChange
*/
PVRSRV_ERROR RGXPostClockSpeedChange(IMG_HANDLE				hDevHandle,
                                     PVRSRV_DEV_POWER_STATE	eCurrentPowerState)
{
	PVRSRV_DEVICE_NODE *psDeviceNode = hDevHandle;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	const RGX_DATA *psRGXData = (RGX_DATA*)psDeviceNode->psDevConfig->hDevData;
	const RGXFWIF_SYSDATA *psFwSysData = psDevInfo->psRGXFWIfFwSysData;
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 ui32NewClockSpeed = psRGXData->psRGXTimingInfo->ui32CoreClockSpeed;

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_ERROR_NOT_SUPPORTED);

	/* Update runtime configuration with the new value */
	OSWriteDeviceMem32WithWMB(&psDevInfo->psRGXFWIfRuntimeCfg->ui32CoreClockSpeed,
	                          ui32NewClockSpeed);

	if ((eCurrentPowerState != PVRSRV_DEV_POWER_STATE_OFF) &&
	    (psFwSysData->ePowState != RGXFWIF_POW_OFF))
	{
		RGXFWIF_KCCB_CMD sCOREClkSpeedChangeCmd;
		IMG_UINT32 ui32CmdKCCBSlot;

		RGXTimeCorrBegin(psDeviceNode, RGXTIMECORR_EVENT_DVFS);

		sCOREClkSpeedChangeCmd.eCmdType = RGXFWIF_KCCB_CMD_CORECLKSPEEDCHANGE;
		sCOREClkSpeedChangeCmd.uCmdData.sCoreClkSpeedChangeData.ui32NewClockSpeed = ui32NewClockSpeed;

		PDUMPCOMMENT(psDeviceNode, "Scheduling CORE clock speed change command");

		PDUMPPOWCMDSTART(psDeviceNode);
		eError = RGXSendCommandAndGetKCCBSlot(psDeviceNode->pvDevice,
		                                      &sCOREClkSpeedChangeCmd,
		                                      PDUMP_FLAGS_NONE,
		                                      &ui32CmdKCCBSlot);
		PDUMPPOWCMDEND(psDeviceNode);

		if (eError != PVRSRV_OK)
		{
			PDUMPCOMMENT(psDeviceNode, "Scheduling CORE clock speed change command failed");
			PVR_DPF((PVR_DBG_ERROR, "RGXPostClockSpeedChange: Scheduling KCCB command failed. Error:%u", eError));
			return eError;
		}

		PVR_DPF((PVR_DBG_MESSAGE, "RGXPostClockSpeedChange: RGX clock speed changed to %uHz",
				psRGXData->psRGXTimingInfo->ui32CoreClockSpeed));
	}

	return eError;
}

/*!
 ******************************************************************************

 @Function	RGXDustCountChange

 @Description

	Does change of number of DUSTs

 @Input	   hDevHandle : RGX Device Node
 @Input	   ui32NumberOfDusts : Number of DUSTs to make transition to

 @Return   PVRSRV_ERROR :

 ******************************************************************************/
PVRSRV_ERROR RGXDustCountChange(IMG_HANDLE		hDevHandle,
                                IMG_UINT32		ui32NumberOfDusts)
{

	PVRSRV_DEVICE_NODE	*psDeviceNode = hDevHandle;
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_ERROR		eError;
	RGXFWIF_KCCB_CMD	sDustCountChange;
	IMG_UINT32			ui32MaxAvailableDusts = psDevInfo->sDevFeatureCfg.ui32MAXDustCount;
	IMG_UINT32			ui32CmdKCCBSlot;
	RGXFWIF_RUNTIME_CFG *psRuntimeCfg = psDevInfo->psRGXFWIfRuntimeCfg;
	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_OK);

	if (ui32NumberOfDusts > ui32MaxAvailableDusts)
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Invalid number of DUSTs (%u) while expecting value within <0,%u>. Error:%u",
				__func__,
				ui32NumberOfDusts,
				ui32MaxAvailableDusts,
				eError));
		return eError;
	}

	psRuntimeCfg->ui32DefaultDustsNumInit = ui32NumberOfDusts;
	OSWriteMemoryBarrier(&psRuntimeCfg->ui32DefaultDustsNumInit);

#if !defined(NO_HARDWARE)
	{
		const RGXFWIF_SYSDATA *psFwSysData = psDevInfo->psRGXFWIfFwSysData;

		if (psFwSysData->ePowState == RGXFWIF_POW_OFF)
		{
			return PVRSRV_OK;
		}

		if (psFwSysData->ePowState != RGXFWIF_POW_FORCED_IDLE)
		{
			eError = PVRSRV_ERROR_DEVICE_POWER_CHANGE_DENIED;
			PVR_DPF((PVR_DBG_ERROR,
					 "%s: Attempt to change dust count when not IDLE",
					 __func__));
			return eError;
		}
	}
#endif

	eError = SyncPrimSet(psDevInfo->psPowSyncPrim, 0);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to set Power sync prim",
				__func__));
		return eError;
	}

	sDustCountChange.eCmdType = RGXFWIF_KCCB_CMD_POW;
	sDustCountChange.uCmdData.sPowData.ePowType = RGXFWIF_POW_NUM_UNITS_CHANGE;
	sDustCountChange.uCmdData.sPowData.uPowerReqData.ui32NumOfDusts = ui32NumberOfDusts;

	PDUMPCOMMENT(psDeviceNode,
	             "Scheduling command to change Dust Count to %u",
	             ui32NumberOfDusts);
	eError = RGXSendCommandAndGetKCCBSlot(psDeviceNode->pvDevice,
	                                      &sDustCountChange,
	                                      PDUMP_FLAGS_NONE,
	                                      &ui32CmdKCCBSlot);

	if (eError != PVRSRV_OK)
	{
		PDUMPCOMMENT(psDeviceNode,
		             "Scheduling command to change Dust Count failed. Error:%u",
		             eError);
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: Scheduling KCCB to change Dust Count failed. Error:%u",
				 __func__, eError));
		return eError;
	}

	/* Wait for the firmware to answer. */
	eError = RGXPollForGPCommandCompletion(psDeviceNode,
	                              psDevInfo->psPowSyncPrim->pui32LinAddr,
								  0x1, 0xFFFFFFFF);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Timeout waiting for idle request", __func__));
		return eError;
	}

#if defined(PDUMP)
	PDUMPCOMMENT(psDeviceNode,
	             "RGXDustCountChange: Poll for Kernel SyncPrim [0x%p] on DM %d",
	             psDevInfo->psPowSyncPrim->pui32LinAddr, RGXFWIF_DM_GP);

	SyncPrimPDumpPol(psDevInfo->psPowSyncPrim,
	                 1,
	                 0xffffffff,
	                 PDUMP_POLL_OPERATOR_EQUAL,
	                 0);
#endif

	return PVRSRV_OK;
}

/*
 @Function	RGXAPMLatencyChange
*/
PVRSRV_ERROR RGXAPMLatencyChange(IMG_HANDLE		hDevHandle,
                                 IMG_UINT32		ui32ActivePMLatencyms,
                                 IMG_BOOL		bActivePMLatencyPersistant)
{

	PVRSRV_DEVICE_NODE	*psDeviceNode = hDevHandle;
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_ERROR		eError;
	RGXFWIF_RUNTIME_CFG	*psRuntimeCfg = psDevInfo->psRGXFWIfRuntimeCfg;
	IMG_UINT32			ui32CmdKCCBSlot;
	PVRSRV_DEV_POWER_STATE	ePowerState;
	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_OK);

	eError = PVRSRVPowerLock(psDeviceNode);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXAPMLatencyChange: Failed to acquire power lock"));
		return eError;
	}

	/* Update runtime configuration with the new values and ensure the
	 * new APM latency is written to memory before requesting the FW to
	 * read it
	 */
	psRuntimeCfg->ui32ActivePMLatencyms = ui32ActivePMLatencyms;
	psRuntimeCfg->bActivePMLatencyPersistant = bActivePMLatencyPersistant;
	OSWriteMemoryBarrier(&psRuntimeCfg->bActivePMLatencyPersistant);

	eError = PVRSRVGetDevicePowerState(psDeviceNode, &ePowerState);

	if ((eError == PVRSRV_OK) && (ePowerState != PVRSRV_DEV_POWER_STATE_OFF))
	{
		RGXFWIF_KCCB_CMD	sActivePMLatencyChange;
		sActivePMLatencyChange.eCmdType = RGXFWIF_KCCB_CMD_POW;
		sActivePMLatencyChange.uCmdData.sPowData.ePowType = RGXFWIF_POW_APM_LATENCY_CHANGE;

		PDUMPCOMMENT(psDeviceNode,
		             "Scheduling command to change APM latency to %u",
		             ui32ActivePMLatencyms);
		eError = RGXSendCommandAndGetKCCBSlot(psDeviceNode->pvDevice,
		                                      &sActivePMLatencyChange,
		                                      PDUMP_FLAGS_NONE,
		                                      &ui32CmdKCCBSlot);

		if (eError != PVRSRV_OK)
		{
			PDUMPCOMMENT(psDeviceNode,
			            "Scheduling command to change APM latency failed. Error:%u",
			            eError);
			PVR_DPF((PVR_DBG_ERROR, "RGXAPMLatencyChange: Scheduling KCCB to change APM latency failed. Error:%u", eError));
			goto ErrorExit;
		}
	}

ErrorExit:
	PVRSRVPowerUnlock(psDeviceNode);

	return eError;
}

/*
	RGXActivePowerRequest
*/
PVRSRV_ERROR RGXActivePowerRequest(IMG_HANDLE hDevHandle)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_DEVICE_NODE	*psDeviceNode = hDevHandle;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	const RGXFWIF_SYSDATA *psFwSysData = psDevInfo->psRGXFWIfFwSysData;
	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_OK);


	psDevInfo->ui32ActivePMReqTotal++;

	/* Powerlock to avoid further requests from racing with the FW hand-shake
	 * from now on (previous kicks to this point are detected by the FW)
	 * PVRSRVPowerLock is replaced with PVRSRVPowerTryLock to avoid
	 * potential dead lock between PDumpWriteLock and PowerLock
	 * during 'DriverLive + PDUMP=1 + EnableAPM=1'.
	 */
	eError = PVRSRVPowerTryLock(psDeviceNode);
	if (eError != PVRSRV_OK)
	{
		if (eError != PVRSRV_ERROR_RETRY)
		{
			PVR_LOG_ERROR(eError, "PVRSRVPowerTryLock");
		}
		else
		{
			psDevInfo->ui32ActivePMReqRetry++;
		}
		goto _RGXActivePowerRequest_PowerLock_failed;
	}

	/* Check again for IDLE once we have the power lock */
	if (psFwSysData->ePowState == RGXFWIF_POW_IDLE)
	{
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
		SetFirmwareHandshakeIdleTime(RGXReadHWTimerReg(psDevInfo)-psFwSysData->ui64StartIdleTime);
#endif

		PDUMPPOWCMDSTART(psDeviceNode);
		eError = PVRSRVSetDevicePowerStateKM(psDeviceNode,
		                                     PVRSRV_DEV_POWER_STATE_OFF,
		                                     PVRSRV_POWER_FLAGS_NONE);
		PDUMPPOWCMDEND(psDeviceNode);

		if (eError == PVRSRV_OK)
		{
			psDevInfo->ui32ActivePMReqOk++;
		}
		else if (eError == PVRSRV_ERROR_DEVICE_POWER_CHANGE_DENIED)
		{
			psDevInfo->ui32ActivePMReqDenied++;
		}
	}
	else
	{
		psDevInfo->ui32ActivePMReqNonIdle++;
	}

	PVRSRVPowerUnlock(psDeviceNode);

_RGXActivePowerRequest_PowerLock_failed:

	return eError;
}
/*
	RGXForcedIdleRequest
*/

#define RGX_FORCED_IDLE_RETRY_COUNT 10

PVRSRV_ERROR RGXForcedIdleRequest(IMG_HANDLE hDevHandle, IMG_BOOL bDeviceOffPermitted)
{
	PVRSRV_DEVICE_NODE    *psDeviceNode = hDevHandle;
	PVRSRV_RGXDEV_INFO    *psDevInfo = psDeviceNode->pvDevice;
	RGXFWIF_KCCB_CMD      sPowCmd;
	PVRSRV_ERROR          eError;
	IMG_UINT32            ui32RetryCount = 0;
	IMG_UINT32            ui32CmdKCCBSlot;
#if !defined(NO_HARDWARE)
	const RGXFWIF_SYSDATA *psFwSysData;
#endif
	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_OK);

#if !defined(NO_HARDWARE)
	psFwSysData = psDevInfo->psRGXFWIfFwSysData;

	/* Firmware already forced idle */
	if (psFwSysData->ePowState == RGXFWIF_POW_FORCED_IDLE)
	{
		return PVRSRV_OK;
	}

	/* Firmware is not powered. Sometimes this is permitted, for instance we were forcing idle to power down. */
	if (psFwSysData->ePowState == RGXFWIF_POW_OFF)
	{
		return (bDeviceOffPermitted) ? PVRSRV_OK : PVRSRV_ERROR_DEVICE_IDLE_REQUEST_DENIED;
	}
#endif

	eError = SyncPrimSet(psDevInfo->psPowSyncPrim, 0);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to set Power sync prim",
				__func__));
		return eError;
	}
	sPowCmd.eCmdType = RGXFWIF_KCCB_CMD_POW;
	sPowCmd.uCmdData.sPowData.ePowType = RGXFWIF_POW_FORCED_IDLE_REQ;
	sPowCmd.uCmdData.sPowData.uPowerReqData.ePowRequestType = RGXFWIF_POWER_FORCE_IDLE;

	PDUMPCOMMENT(psDeviceNode,
	             "RGXForcedIdleRequest: Sending forced idle command");

	/* Send one forced IDLE command to GP */
	eError = RGXSendCommandAndGetKCCBSlot(psDevInfo,
	                                      &sPowCmd,
	                                      PDUMP_FLAGS_NONE,
	                                      &ui32CmdKCCBSlot);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to send idle request", __func__));
		return eError;
	}

	/* Wait for GPU to finish current workload */
	do {
		eError = RGXPollForGPCommandCompletion(psDeviceNode,
		                              psDevInfo->psPowSyncPrim->pui32LinAddr,
									  0x1, 0xFFFFFFFF);
		if ((eError == PVRSRV_OK) || (ui32RetryCount == RGX_FORCED_IDLE_RETRY_COUNT))
		{
			break;
		}
		ui32RetryCount++;
		PVR_DPF((PVR_DBG_WARNING,
				"%s: Request timeout. Retry %d of %d",
				 __func__, ui32RetryCount, RGX_FORCED_IDLE_RETRY_COUNT));
	} while (IMG_TRUE);

	if (eError != PVRSRV_OK)
	{
		RGXFWNotifyHostTimeout(psDevInfo);
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: Idle request failed. Firmware potentially left in forced idle state",
				 __func__));
		return eError;
	}

#if defined(PDUMP)
	PDUMPCOMMENT(psDeviceNode,
	             "RGXForcedIdleRequest: Poll for Kernel SyncPrim [0x%p] on DM %d",
	             psDevInfo->psPowSyncPrim->pui32LinAddr, RGXFWIF_DM_GP);

	SyncPrimPDumpPol(psDevInfo->psPowSyncPrim,
	                 1,
	                 0xffffffff,
	                 PDUMP_POLL_OPERATOR_EQUAL,
	                 0);
#endif

#if !defined(NO_HARDWARE)
	/* Check the firmware state for idleness */
	if (psFwSysData->ePowState != RGXFWIF_POW_FORCED_IDLE)
	{
		return PVRSRV_ERROR_DEVICE_IDLE_REQUEST_DENIED;
	}
#endif

	return PVRSRV_OK;
}

/*
	RGXCancelForcedIdleRequest
*/
PVRSRV_ERROR RGXCancelForcedIdleRequest(IMG_HANDLE hDevHandle)
{
	PVRSRV_DEVICE_NODE	*psDeviceNode = hDevHandle;
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	RGXFWIF_KCCB_CMD	sPowCmd;
	PVRSRV_ERROR		eError = PVRSRV_OK;
	IMG_UINT32			ui32CmdKCCBSlot;
	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_OK);

	eError = SyncPrimSet(psDevInfo->psPowSyncPrim, 0);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to set Power sync prim",
				__func__));
		goto ErrorExit;
	}

	/* Send the IDLE request to the FW */
	sPowCmd.eCmdType = RGXFWIF_KCCB_CMD_POW;
	sPowCmd.uCmdData.sPowData.ePowType = RGXFWIF_POW_FORCED_IDLE_REQ;
	sPowCmd.uCmdData.sPowData.uPowerReqData.ePowRequestType = RGXFWIF_POWER_CANCEL_FORCED_IDLE;

	PDUMPCOMMENT(psDeviceNode,
	             "RGXForcedIdleRequest: Sending cancel forced idle command");

	/* Send cancel forced IDLE command to GP */
	eError = RGXSendCommandAndGetKCCBSlot(psDevInfo,
	                                      &sPowCmd,
	                                      PDUMP_FLAGS_NONE,
	                                      &ui32CmdKCCBSlot);

	if (eError != PVRSRV_OK)
	{
		PDUMPCOMMENT(psDeviceNode,
		             "RGXCancelForcedIdleRequest: Failed to send cancel IDLE request for DM%d",
		             RGXFWIF_DM_GP);
		goto ErrorExit;
	}

	/* Wait for the firmware to answer. */
	eError = RGXPollForGPCommandCompletion(psDeviceNode,
	                              psDevInfo->psPowSyncPrim->pui32LinAddr,
								  1, 0xFFFFFFFF);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Timeout waiting for cancel idle request", __func__));
		goto ErrorExit;
	}

#if defined(PDUMP)
	PDUMPCOMMENT(psDeviceNode,
	             "RGXCancelForcedIdleRequest: Poll for Kernel SyncPrim [0x%p] on DM %d",
	             psDevInfo->psPowSyncPrim->pui32LinAddr, RGXFWIF_DM_GP);

	SyncPrimPDumpPol(psDevInfo->psPowSyncPrim,
	                 1,
	                 0xffffffff,
	                 PDUMP_POLL_OPERATOR_EQUAL,
	                 0);
#endif

	return eError;

ErrorExit:
	PVR_DPF((PVR_DBG_ERROR, "%s: Firmware potentially left in forced idle state", __func__));
	return eError;
}

/*!
 ******************************************************************************

 @Function	PVRSRVGetNextDustCount

 @Description

	Calculate a sequence of dust counts to achieve full transition coverage.
	We increment two counts of dusts and switch up and down between them.
	It does contain a few redundant transitions. If two dust exist, the
	output transitions should be as follows.

	0->1, 0<-1, 0->2, 0<-2, (0->1)
	1->1, 1->2, 1<-2, (1->2)
	2->2, (2->0),
	0->0. Repeat.

	Redundant transitions in brackets.

 @Input		psDustReqState : Counter state used to calculate next dust count
 @Input		ui32DustCount : Number of dusts in the core

 @Return	PVRSRV_ERROR

 ******************************************************************************/
IMG_UINT32 RGXGetNextDustCount(RGX_DUST_STATE *psDustReqState, IMG_UINT32 ui32DustCount)
{
	if (psDustReqState->bToggle)
	{
		psDustReqState->ui32DustCount2++;
	}

	if (psDustReqState->ui32DustCount2 > ui32DustCount)
	{
		psDustReqState->ui32DustCount1++;
		psDustReqState->ui32DustCount2 = psDustReqState->ui32DustCount1;
	}

	if (psDustReqState->ui32DustCount1 > ui32DustCount)
	{
		psDustReqState->ui32DustCount1 = 0;
		psDustReqState->ui32DustCount2 = 0;
	}

	psDustReqState->bToggle = !psDustReqState->bToggle;

	return (psDustReqState->bToggle) ? psDustReqState->ui32DustCount1 : psDustReqState->ui32DustCount2;
}

/******************************************************************************
 End of file (rgxpower.c)
******************************************************************************/
