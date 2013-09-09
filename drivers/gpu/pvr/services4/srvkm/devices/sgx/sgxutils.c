/*************************************************************************/ /*!
@Title          Device specific utility routines
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

#include <stddef.h>

#include "sgxdefs.h"
#include "services_headers.h"
#include "buffer_manager.h"
#include "sgx_bridge_km.h"
#include "sgxapi_km.h"
#include "sgxinfo.h"
#include "sgx_mkif_km.h"
#include "sysconfig.h"
#include "pdump_km.h"
#include "mmu.h"
#include "pvr_bridge_km.h"
#include "osfunc.h"
#include "pvr_debug.h"
#include "sgxutils.h"
#include "ttrace.h"
#include "sgxmmu.h"

#ifdef __linux__
#include <linux/kernel.h>	// sprintf
#include <linux/string.h>	// strncpy, strlen
#else
#include <stdio.h>
#endif

IMG_UINT64 ui64KickCount;
int g_debug_CCB_Info_WCNT;

#if defined(SYS_CUSTOM_POWERDOWN)
PVRSRV_ERROR SysPowerDownMISR(PVRSRV_DEVICE_NODE	* psDeviceNode, IMG_UINT32 ui32CallerID);
#endif



/*!
******************************************************************************

 @Function	SGXPostActivePowerEvent

 @Description

	 post power event functionality (e.g. restart)

 @Input	psDeviceNode : SGX Device Node
 @Input	ui32CallerID - KERNEL_ID or ISR_ID

 @Return   IMG_VOID :

******************************************************************************/
static IMG_VOID SGXPostActivePowerEvent(PVRSRV_DEVICE_NODE	* psDeviceNode,
										IMG_UINT32           ui32CallerID)
{
	PVRSRV_SGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	SGXMKIF_HOST_CTL	*psSGXHostCtl = psDevInfo->psSGXHostCtl;

	/* Update the counter for stats. */
	psSGXHostCtl->ui32NumActivePowerEvents++;

	if ((psSGXHostCtl->ui32PowerStatus & PVRSRV_USSE_EDM_POWMAN_POWEROFF_RESTART_IMMEDIATE) != 0)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "SGXPostActivePowerEvent: SGX requests immediate restart"));
		
		/*
			Events were queued during the active power
			request, so SGX will need to be restarted.
	     */
		if (ui32CallerID == ISR_ID)
		{
			psDeviceNode->bReProcessDeviceCommandComplete = IMG_TRUE;
		}
		else
		{
			SGXScheduleProcessQueuesKM(psDeviceNode);
		}
	}
}


/*!
******************************************************************************

 @Function	SGXTestActivePowerEvent

 @Description

 Checks whether the microkernel has generated an active power event. If so,
 	perform the power transition.

 @Input	psDeviceNode : SGX Device Node
 @Input	ui32CallerID - KERNEL_ID or ISR_ID

 @Return   IMG_VOID :

******************************************************************************/
IMG_VOID SGXTestActivePowerEvent (PVRSRV_DEVICE_NODE	*psDeviceNode,
								  IMG_UINT32			ui32CallerID)
{
	PVRSRV_ERROR		eError = PVRSRV_OK;
	PVRSRV_SGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	SGXMKIF_HOST_CTL	*psSGXHostCtl = psDevInfo->psSGXHostCtl;

#if defined(SYS_SUPPORTS_SGX_IDLE_CALLBACK)
	if (!psDevInfo->bSGXIdle &&
		((psSGXHostCtl->ui32InterruptFlags & PVRSRV_USSE_EDM_INTERRUPT_IDLE) != 0))
	{
		psDevInfo->bSGXIdle = IMG_TRUE;
		SysSGXIdleTransition(psDevInfo->bSGXIdle);
	}
	else if (psDevInfo->bSGXIdle &&
			((psSGXHostCtl->ui32InterruptFlags & PVRSRV_USSE_EDM_INTERRUPT_IDLE) == 0))
	{
		psDevInfo->bSGXIdle = IMG_FALSE;
		SysSGXIdleTransition(psDevInfo->bSGXIdle);
	}
#endif /* SYS_SUPPORTS_SGX_IDLE_CALLBACK */

	/*
	 * Quickly check (without lock) if there is an APM event we should handle.
	 * This check fails most of the time so we don't want to incur lock overhead.
	 * Check the flags in the reverse order that microkernel clears them to prevent
	 * us from seeing an inconsistent state.
	 */
	if (((psSGXHostCtl->ui32InterruptClearFlags & PVRSRV_USSE_EDM_INTERRUPT_ACTIVE_POWER) == 0) &&
		((psSGXHostCtl->ui32InterruptFlags & PVRSRV_USSE_EDM_INTERRUPT_ACTIVE_POWER) != 0))
	{
		eError = PVRSRVPowerLock(ui32CallerID, IMG_FALSE);
		if (eError == PVRSRV_ERROR_RETRY)
		{
			return;
		}
		else if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SGXTestActivePowerEvent failed to acquire lock - "
                    "ui32CallerID:%d eError:%u", ui32CallerID, eError));
			return;
		}

		/*
		 * Check again (with lock) if APM event has been cleared or handled. A race
		 * condition may allow multiple threads to pass the quick check.
		 */
		if (((psSGXHostCtl->ui32InterruptClearFlags & PVRSRV_USSE_EDM_INTERRUPT_ACTIVE_POWER) != 0) ||
			((psSGXHostCtl->ui32InterruptFlags & PVRSRV_USSE_EDM_INTERRUPT_ACTIVE_POWER) == 0))
		{
			PVRSRVPowerUnlock(ui32CallerID);
			return;
		}

		/* Microkernel is idle and is requesting to be powered down. */
		psSGXHostCtl->ui32InterruptClearFlags |= PVRSRV_USSE_EDM_INTERRUPT_ACTIVE_POWER;

		/* Suspend pdumping. */
		PDUMPSUSPEND();

#if defined(SYS_CUSTOM_POWERDOWN)
		/*
		 	Some power down code cannot be executed inside an MISR on
		 	some platforms that use mutexes inside the power code.
		 */
		eError = SysPowerDownMISR(psDeviceNode, ui32CallerID);
#else
		eError = PVRSRVSetDevicePowerStateKM(psDeviceNode->sDevId.ui32DeviceIndex,
											 PVRSRV_DEV_POWER_STATE_OFF);
		if (eError == PVRSRV_OK)
		{
			SGXPostActivePowerEvent(psDeviceNode, ui32CallerID);
		}
#endif
		PVRSRVPowerUnlock(ui32CallerID);

		/* Resume pdumping */
		PDUMPRESUME();
	}

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXTestActivePowerEvent error:%u", eError));
	}
}


/******************************************************************************
 FUNCTION	: SGXAcquireKernelCCBSlot

 PURPOSE	: Attempts to obtain a slot in the Kernel CCB

 PARAMETERS	: psCCB - the CCB

 RETURNS	: Address of space if available, IMG_NULL otherwise
******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(SGXAcquireKernelCCBSlot)
#endif
static INLINE SGXMKIF_COMMAND * SGXAcquireKernelCCBSlot(PVRSRV_SGX_CCB_INFO *psCCB)
{
	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		if(((*psCCB->pui32WriteOffset + 1) & 255) != *psCCB->pui32ReadOffset)
		{
			return &psCCB->psCommands[*psCCB->pui32WriteOffset];
		}

		OSSleepms(1);
	} END_LOOP_UNTIL_TIMEOUT();

	/* Time out on waiting for CCB space */
	return IMG_NULL;
}

/*!
******************************************************************************

 @Function	SGXScheduleCCBCommand

 @Description - Submits a CCB command and kicks the ukernel (without
 				power management)

 @Input psDevInfo - pointer to device info
 @Input eCmdType - see SGXMKIF_CMD_*
 @Input psCommandData - kernel CCB command
 @Input ui32CallerID - KERNEL_ID or ISR_ID
 @Input ui32PDumpFlags

 @Return ui32Error - success or failure

******************************************************************************/
PVRSRV_ERROR SGXScheduleCCBCommand(PVRSRV_DEVICE_NODE	*psDeviceNode,
								   SGXMKIF_CMD_TYPE		eCmdType,
								   SGXMKIF_COMMAND		*psCommandData,
								   IMG_UINT32			ui32CallerID,
								   IMG_UINT32			ui32PDumpFlags,
								   IMG_HANDLE			hDevMemContext,
								   IMG_BOOL				bLastInScene)
{
	PVRSRV_SGX_CCB_INFO *psKernelCCB;
	PVRSRV_ERROR eError = PVRSRV_OK;
	SGXMKIF_COMMAND *psSGXCommand;
	PVRSRV_SGXDEV_INFO 	*psDevInfo = psDeviceNode->pvDevice;
	SGXMKIF_HOST_CTL	*psSGXHostCtl = psDevInfo->psSGXHostCtl;
#if defined(FIX_HW_BRN_31620)
	IMG_UINT32 ui32CacheMasks[4];
	IMG_UINT32 i;
	MMU_CONTEXT		*psMMUContext;
#endif
#if defined(PDUMP)
	IMG_VOID *pvDumpCommand;
	IMG_BOOL bPDumpIsSuspended = PDumpIsSuspended();
#if defined(SUPPORT_PDUMP_MULTI_PROCESS)
	IMG_BOOL bPDumpActive = _PDumpIsProcessActive();
#else
	IMG_BOOL bPDumpActive = IMG_TRUE;
#endif
#else
	PVR_UNREFERENCED_PARAMETER(ui32CallerID);
	PVR_UNREFERENCED_PARAMETER(ui32PDumpFlags);
#endif

#if defined(FIX_HW_BRN_31620)
	for(i=0;i<4;i++)
	{
		ui32CacheMasks[i] = 0;
	}

	psMMUContext = psDevInfo->hKernelMMUContext;
	psDeviceNode->pfnMMUGetCacheFlushRange(psMMUContext, &ui32CacheMasks[0]);

	/* Put the apps memory context in the bottom half */
	if (hDevMemContext)
	{
		BM_CONTEXT *psBMContext = (BM_CONTEXT *) hDevMemContext;

		psMMUContext = psBMContext->psMMUContext;
		psDeviceNode->pfnMMUGetCacheFlushRange(psMMUContext, &ui32CacheMasks[2]);
	}

	/* If we have an outstanding flush request then set the cachecontrol bit */
	if (ui32CacheMasks[0] || ui32CacheMasks[1] || ui32CacheMasks[2] || ui32CacheMasks[3])
	{
		psDevInfo->ui32CacheControl |= SGXMKIF_CC_INVAL_BIF_PD;
	}
#endif

#if defined(FIX_HW_BRN_28889)
	/*
		If the data cache and bif cache need invalidating there has been a cleanup
		request. Therefore, we need to send the invalidate seperately and wait
		for it to complete.
	*/
	if ( (eCmdType != SGXMKIF_CMD_PROCESS_QUEUES) &&
		 ((psDevInfo->ui32CacheControl & SGXMKIF_CC_INVAL_DATA) != 0) &&
		 ((psDevInfo->ui32CacheControl & (SGXMKIF_CC_INVAL_BIF_PT | SGXMKIF_CC_INVAL_BIF_PD)) != 0))
	{
	#if defined(PDUMP)
		PVRSRV_KERNEL_MEM_INFO	*psSGXHostCtlMemInfo = psDevInfo->psKernelSGXHostCtlMemInfo;
	#endif
		SGXMKIF_HOST_CTL	*psSGXHostCtl = psDevInfo->psSGXHostCtl;
		SGXMKIF_COMMAND		sCacheCommand = {0};

		eError = SGXScheduleCCBCommand(psDeviceNode,
									   SGXMKIF_CMD_PROCESS_QUEUES,
									   &sCacheCommand,
									   ui32CallerID,
									   ui32PDumpFlags,
									   hDevMemContext,
									   bLastInScene);
		if (eError != PVRSRV_OK)
		{
			goto Exit;
		}

		/* Wait for the invalidate to happen */
		#if !defined(NO_HARDWARE)
		if(PollForValueKM(&psSGXHostCtl->ui32InvalStatus,
						  PVRSRV_USSE_EDM_BIF_INVAL_COMPLETE,
						  PVRSRV_USSE_EDM_BIF_INVAL_COMPLETE,
						  2 * MAX_HW_TIME_US,
						  MAX_HW_TIME_US/WAIT_TRY_COUNT,
						  IMG_FALSE) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SGXScheduleCCBCommand: Wait for uKernel to Invalidate BIF cache failed"));
			PVR_DBG_BREAK;
		}
		#endif

		#if defined(PDUMP)
		/* Pdump the poll as well. */
		PDUMPCOMMENTWITHFLAGS(0, "Host Control - Poll for BIF cache invalidate request to complete");
		PDUMPMEMPOL(psSGXHostCtlMemInfo,
					offsetof(SGXMKIF_HOST_CTL, ui32InvalStatus),
					PVRSRV_USSE_EDM_BIF_INVAL_COMPLETE,
					PVRSRV_USSE_EDM_BIF_INVAL_COMPLETE,
					PDUMP_POLL_OPERATOR_EQUAL,
					0,
					MAKEUNIQUETAG(psSGXHostCtlMemInfo));
		#endif /* PDUMP */

		psSGXHostCtl->ui32InvalStatus &= ~(PVRSRV_USSE_EDM_BIF_INVAL_COMPLETE);
		PDUMPMEM(IMG_NULL, psSGXHostCtlMemInfo, offsetof(SGXMKIF_HOST_CTL, ui32CleanupStatus), sizeof(IMG_UINT32), 0, MAKEUNIQUETAG(psSGXHostCtlMemInfo));
	}
#else
	PVR_UNREFERENCED_PARAMETER(hDevMemContext);
#endif

#if defined(FIX_HW_BRN_31620)
	if ((eCmdType != SGXMKIF_CMD_FLUSHPDCACHE) && (psDevInfo->ui32CacheControl & SGXMKIF_CC_INVAL_BIF_PD))
	{
		SGXMKIF_COMMAND		sPDECacheCommand = {0};
		IMG_DEV_PHYADDR		sDevPAddr;

		/* Put the kernel info in the top 1/2 of the data */
		psMMUContext = psDevInfo->hKernelMMUContext;

		psDeviceNode->pfnMMUGetPDPhysAddr(psMMUContext, &sDevPAddr);
		sPDECacheCommand.ui32Data[0] = sDevPAddr.uiAddr | 1;
		sPDECacheCommand.ui32Data[1] = ui32CacheMasks[0];
		sPDECacheCommand.ui32Data[2] = ui32CacheMasks[1];

		/* Put the apps memory context in the bottom half */
		if (hDevMemContext)
		{
			BM_CONTEXT *psBMContext = (BM_CONTEXT *) hDevMemContext;

			psMMUContext = psBMContext->psMMUContext;

			psDeviceNode->pfnMMUGetPDPhysAddr(psMMUContext, &sDevPAddr);
			/* Or in 1 to the lsb to show we have a valid context */
			sPDECacheCommand.ui32Data[3] = sDevPAddr.uiAddr | 1;
			sPDECacheCommand.ui32Data[4] = ui32CacheMasks[2];
			sPDECacheCommand.ui32Data[5] = ui32CacheMasks[3];
		}

		/* Only do a kick if there is any update */
		if (sPDECacheCommand.ui32Data[1] | sPDECacheCommand.ui32Data[2] | sPDECacheCommand.ui32Data[4] |
			sPDECacheCommand.ui32Data[5])
		{
			eError = SGXScheduleCCBCommand(psDeviceNode,
										   SGXMKIF_CMD_FLUSHPDCACHE,
										   &sPDECacheCommand,
										   ui32CallerID,
										   ui32PDumpFlags,
										   hDevMemContext,
										   bLastInScene);
			if (eError != PVRSRV_OK)
			{
				goto Exit;
			}
		}
	}
#endif
	psKernelCCB = psDevInfo->psKernelCCBInfo;

	psSGXCommand = SGXAcquireKernelCCBSlot(psKernelCCB);

	/* Wait for CCB space timed out */
	if(!psSGXCommand)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXScheduleCCBCommand: Wait for CCB space timed out")) ;
		eError = PVRSRV_ERROR_TIMEOUT;
		goto Exit;
	}

	/* embed cache control word */
	psCommandData->ui32CacheControl = psDevInfo->ui32CacheControl;

#if defined(PDUMP)
	/* Accumulate any cache invalidates that may have happened */
	psDevInfo->sPDContext.ui32CacheControl |= psDevInfo->ui32CacheControl;
#endif

	/* and clear it */
	psDevInfo->ui32CacheControl = 0;

	/* Copy command data over */
	*psSGXCommand = *psCommandData;

	if (eCmdType >= SGXMKIF_CMD_MAX)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXScheduleCCBCommand: Unknown command type: %d", eCmdType)) ;
		eError = PVRSRV_ERROR_INVALID_CCB_COMMAND;
		goto Exit;
	}

	if (eCmdType == SGXMKIF_CMD_2D ||
		eCmdType == SGXMKIF_CMD_TRANSFER ||
		((eCmdType == SGXMKIF_CMD_TA) && bLastInScene))
	{
		SYS_DATA *psSysData;

		/* CPU cache clean control */
		SysAcquireData(&psSysData);

		if(psSysData->ePendingCacheOpType == PVRSRV_MISC_INFO_CPUCACHEOP_FLUSH)
		{
			OSFlushCPUCacheKM();
		}
		else if(psSysData->ePendingCacheOpType == PVRSRV_MISC_INFO_CPUCACHEOP_CLEAN)
		{
			OSCleanCPUCacheKM();
		}

		/* Clear the pending op */
		psSysData->ePendingCacheOpType = PVRSRV_MISC_INFO_CPUCACHEOP_NONE;
	}

	PVR_ASSERT(eCmdType < SGXMKIF_CMD_MAX);
	psSGXCommand->ui32ServiceAddress = psDevInfo->aui32HostKickAddr[eCmdType];	/* PRQA S 3689 */ /* misuse of enums for bounds checking */

#if defined(PDUMP)
	if ((ui32CallerID != ISR_ID) && (bPDumpIsSuspended == IMG_FALSE) &&
		(bPDumpActive == IMG_TRUE) )
	{
		/* Poll for space in the CCB. */
		PDUMPCOMMENTWITHFLAGS(ui32PDumpFlags, "Poll for space in the Kernel CCB\r\n");
		PDUMPMEMPOL(psKernelCCB->psCCBCtlMemInfo,
					offsetof(PVRSRV_SGX_CCB_CTL, ui32ReadOffset),
					(psKernelCCB->ui32CCBDumpWOff + 1) & 0xff,
					0xff,
					PDUMP_POLL_OPERATOR_NOTEQUAL,
					ui32PDumpFlags,
					MAKEUNIQUETAG(psKernelCCB->psCCBCtlMemInfo));

		PDUMPCOMMENTWITHFLAGS(ui32PDumpFlags, "Kernel CCB command (type == %d)\r\n", eCmdType);
		pvDumpCommand = (IMG_VOID *)((IMG_UINT8 *)psKernelCCB->psCCBMemInfo->pvLinAddrKM + (*psKernelCCB->pui32WriteOffset * sizeof(SGXMKIF_COMMAND)));

		PDUMPMEM(pvDumpCommand,
					psKernelCCB->psCCBMemInfo,
					psKernelCCB->ui32CCBDumpWOff * sizeof(SGXMKIF_COMMAND),
					sizeof(SGXMKIF_COMMAND),
					ui32PDumpFlags,
					MAKEUNIQUETAG(psKernelCCB->psCCBMemInfo));

		/* Overwrite cache control with pdump shadow */
		PDUMPMEM(&psDevInfo->sPDContext.ui32CacheControl,
					psKernelCCB->psCCBMemInfo,
					psKernelCCB->ui32CCBDumpWOff * sizeof(SGXMKIF_COMMAND) +
					offsetof(SGXMKIF_COMMAND, ui32CacheControl),
					sizeof(IMG_UINT32),
					ui32PDumpFlags,
					MAKEUNIQUETAG(psKernelCCB->psCCBMemInfo));

		if (PDumpIsCaptureFrameKM()
		|| ((ui32PDumpFlags & PDUMP_FLAGS_CONTINUOUS) != 0))
		{
			/* Clear cache invalidate shadow */
			psDevInfo->sPDContext.ui32CacheControl = 0;
		}
	}
#endif

#if defined(FIX_HW_BRN_26620) && defined(SGX_FEATURE_SYSTEM_CACHE) && !defined(SGX_BYPASS_SYSTEM_CACHE)
	/* Make sure the previous command has been read before send the next one */
	eError = PollForValueKM (psKernelCCB->pui32ReadOffset,
								*psKernelCCB->pui32WriteOffset,
								0xFF,
								MAX_HW_TIME_US,
								MAX_HW_TIME_US/WAIT_TRY_COUNT,
								IMG_FALSE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXScheduleCCBCommand: Timeout waiting for previous command to be read")) ;
		eError = PVRSRV_ERROR_TIMEOUT;
		goto Exit;
	}
#endif

	/*
		Increment the write offset
	*/
	*psKernelCCB->pui32WriteOffset = (*psKernelCCB->pui32WriteOffset + 1) & 255;
	g_debug_CCB_Info_WCNT++;

#if defined(PDUMP)
	if ((ui32CallerID != ISR_ID) && (bPDumpIsSuspended == IMG_FALSE) &&
		(bPDumpActive == IMG_TRUE) )
	{
	#if defined(FIX_HW_BRN_26620) && defined(SGX_FEATURE_SYSTEM_CACHE) && !defined(SGX_BYPASS_SYSTEM_CACHE)
		PDUMPCOMMENTWITHFLAGS(ui32PDumpFlags, "Poll for previous Kernel CCB CMD to be read\r\n");
		PDUMPMEMPOL(psKernelCCB->psCCBCtlMemInfo,
					offsetof(PVRSRV_SGX_CCB_CTL, ui32ReadOffset),
					(psKernelCCB->ui32CCBDumpWOff),
					0xFF,
					PDUMP_POLL_OPERATOR_EQUAL,
					ui32PDumpFlags,
					MAKEUNIQUETAG(psKernelCCB->psCCBCtlMemInfo));
	#endif

		if (PDumpIsCaptureFrameKM()
		|| ((ui32PDumpFlags & PDUMP_FLAGS_CONTINUOUS) != 0))
		{
			psKernelCCB->ui32CCBDumpWOff = (psKernelCCB->ui32CCBDumpWOff + 1) & 0xFF;
			psDevInfo->ui32KernelCCBEventKickerDumpVal = (psDevInfo->ui32KernelCCBEventKickerDumpVal + 1) & 0xFF;
		}

		PDUMPCOMMENTWITHFLAGS(ui32PDumpFlags, "Kernel CCB write offset\r\n");
		PDUMPMEM(&psKernelCCB->ui32CCBDumpWOff,
				 psKernelCCB->psCCBCtlMemInfo,
				 offsetof(PVRSRV_SGX_CCB_CTL, ui32WriteOffset),
				 sizeof(IMG_UINT32),
				 ui32PDumpFlags,
				 MAKEUNIQUETAG(psKernelCCB->psCCBCtlMemInfo));
		PDUMPCOMMENTWITHFLAGS(ui32PDumpFlags, "Kernel CCB event kicker\r\n");
		PDUMPMEM(&psDevInfo->ui32KernelCCBEventKickerDumpVal,
				 psDevInfo->psKernelCCBEventKickerMemInfo,
				 0,
				 sizeof(IMG_UINT32),
				 ui32PDumpFlags,
				 MAKEUNIQUETAG(psDevInfo->psKernelCCBEventKickerMemInfo));
		PDUMPCOMMENTWITHFLAGS(ui32PDumpFlags, "Kick the SGX microkernel\r\n");
	#if defined(FIX_HW_BRN_26620) && defined(SGX_FEATURE_SYSTEM_CACHE) && !defined(SGX_BYPASS_SYSTEM_CACHE)
		PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, SGX_MP_CORE_SELECT(EUR_CR_EVENT_KICK2, 0), EUR_CR_EVENT_KICK2_NOW_MASK, ui32PDumpFlags);
	#else
		PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, SGX_MP_CORE_SELECT(EUR_CR_EVENT_KICK, 0), EUR_CR_EVENT_KICK_NOW_MASK, ui32PDumpFlags);
	#endif
	}
#endif

	*psDevInfo->pui32KernelCCBEventKicker = (*psDevInfo->pui32KernelCCBEventKicker + 1) & 0xFF;

	/*
	 * New command submission is considered a proper handling of any pending APM
	 * event, so mark it as handled to prevent other host threads from taking
	 * action.
	 */
	psSGXHostCtl->ui32InterruptClearFlags |= PVRSRV_USSE_EDM_INTERRUPT_ACTIVE_POWER;

	OSWriteMemoryBarrier();

	/* Order is importent for post processor! */
	PVR_TTRACE_UI32(PVRSRV_TRACE_GROUP_MKSYNC, PVRSRV_TRACE_CLASS_NONE,
			MKSYNC_TOKEN_KERNEL_CCB_OFFSET, *psKernelCCB->pui32WriteOffset);
	PVR_TTRACE_UI32(PVRSRV_TRACE_GROUP_MKSYNC, PVRSRV_TRACE_CLASS_NONE,
			MKSYNC_TOKEN_CORE_CLK, psDevInfo->ui32CoreClockSpeed);
	PVR_TTRACE_UI32(PVRSRV_TRACE_GROUP_MKSYNC, PVRSRV_TRACE_CLASS_NONE,
			MKSYNC_TOKEN_UKERNEL_CLK, psDevInfo->ui32uKernelTimerClock);


#if defined(FIX_HW_BRN_26620) && defined(SGX_FEATURE_SYSTEM_CACHE) && !defined(SGX_BYPASS_SYSTEM_CACHE)
	OSWriteHWReg(psDevInfo->pvRegsBaseKM,
				SGX_MP_CORE_SELECT(EUR_CR_EVENT_KICK2, 0),
				EUR_CR_EVENT_KICK2_NOW_MASK);
#else
	OSWriteHWReg(psDevInfo->pvRegsBaseKM,
				SGX_MP_CORE_SELECT(EUR_CR_EVENT_KICK, 0),
				EUR_CR_EVENT_KICK_NOW_MASK);
#endif

	OSMemoryBarrier();

#if defined(NO_HARDWARE)
	/* Increment read offset */
	*psKernelCCB->pui32ReadOffset = (*psKernelCCB->pui32ReadOffset + 1) & 255;
#endif

	ui64KickCount++;
Exit:
	return eError;
}


/*!
******************************************************************************

 @Function	SGXScheduleCCBCommandKM

 @Description - Submits a CCB command and kicks the ukernel

 @Input psDeviceNode - pointer to SGX device node
 @Input eCmdType - see SGXMKIF_CMD_*
 @Input psCommandData - kernel CCB command
 @Input ui32CallerID - KERNEL_ID or ISR_ID
 @Input ui32PDumpFlags

 @Return ui32Error - success or failure

******************************************************************************/
PVRSRV_ERROR SGXScheduleCCBCommandKM(PVRSRV_DEVICE_NODE		*psDeviceNode,
									 SGXMKIF_CMD_TYPE		eCmdType,
									 SGXMKIF_COMMAND		*psCommandData,
									 IMG_UINT32				ui32CallerID,
									 IMG_UINT32				ui32PDumpFlags,
									 IMG_HANDLE				hDevMemContext,
									 IMG_BOOL				bLastInScene)
{
	PVRSRV_ERROR	eError;
	PVRSRV_SGXDEV_INFO      *psDevInfo = psDeviceNode->pvDevice;
	eError = PVRSRVPowerLock(ui32CallerID, IMG_FALSE);

	if (psDevInfo->psKernelCCBCtl->ui32ReadOffset  > 0xff || psDevInfo->psKernelCCBCtl->ui32WriteOffset > 0xff)
		PVR_DPF((PVR_DBG_ERROR, "SGX CCB check error: RO: %x, WO:%x ", psDevInfo->psKernelCCBCtl->ui32ReadOffset,
					psDevInfo->psKernelCCBCtl->ui32WriteOffset));
	PVR_ASSERT(psDevInfo->psKernelCCBCtl->ui32ReadOffset <= 0xff &&
		psDevInfo->psKernelCCBCtl->ui32WriteOffset <= 0xff);

	if (eError == PVRSRV_ERROR_RETRY)
	{
		if (ui32CallerID == ISR_ID)
		{
			SYS_DATA *psSysData;
			
			/*
				ISR failed to acquire lock so it must be held by a kernel thread.
				Bring up and kick SGX if necessary when the lock is available.
			*/
			psDeviceNode->bReProcessDeviceCommandComplete = IMG_TRUE;
			eError = PVRSRV_OK;

			SysAcquireData(&psSysData);
			OSScheduleMISR(psSysData);
		}
		else
		{
			/*
				Return to srvclient for retry.
			*/
		}

		return eError;
	}
	else if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SGXScheduleCCBCommandKM failed to acquire lock - "
				"ui32CallerID:%d eError:%u", ui32CallerID, eError));
		return eError;
	}

	/* Note that a power-up has been dumped in the init phase. */
	PDUMPSUSPEND();

	/* Ensure that SGX is powered up before kicking the ukernel. */
	eError = PVRSRVSetDevicePowerStateKM(psDeviceNode->sDevId.ui32DeviceIndex,
										 PVRSRV_DEV_POWER_STATE_ON);

	PDUMPRESUME();

	if (eError == PVRSRV_OK)
	{
		psDeviceNode->bReProcessDeviceCommandComplete = IMG_FALSE;
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR,"SGXScheduleCCBCommandKM failed to acquire lock - "
				 "ui32CallerID:%d eError:%u", ui32CallerID, eError));
		PVRSRVPowerUnlock(ui32CallerID);
		return eError;
	}

	eError = SGXScheduleCCBCommand(psDeviceNode, eCmdType, psCommandData, ui32CallerID, ui32PDumpFlags, hDevMemContext, bLastInScene);

	PVRSRVPowerUnlock(ui32CallerID);
	return eError;
}


/*!
******************************************************************************

 @Function	SGXScheduleProcessQueuesKM

 @Description - Software command complete handler

 @Input psDeviceNode - SGX device node

******************************************************************************/
PVRSRV_ERROR SGXScheduleProcessQueuesKM(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_ERROR 		eError;
	PVRSRV_SGXDEV_INFO 	*psDevInfo = psDeviceNode->pvDevice;
	SGXMKIF_HOST_CTL	*psHostCtl = psDevInfo->psKernelSGXHostCtlMemInfo->pvLinAddrKM;
	IMG_UINT32			ui32PowerStatus;
	SGXMKIF_COMMAND		sCommand = {0};

	ui32PowerStatus = psHostCtl->ui32PowerStatus;
	if ((ui32PowerStatus & PVRSRV_USSE_EDM_POWMAN_NO_WORK) != 0)
	{
		/* The ukernel has no work to do so don't waste power. */
		return PVRSRV_OK;
	}

	eError = SGXScheduleCCBCommandKM(psDeviceNode, SGXMKIF_CMD_PROCESS_QUEUES, &sCommand, ISR_ID, 0, IMG_NULL, IMG_FALSE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SGXScheduleProcessQueuesKM failed to schedule CCB command: %u", eError));
		return eError;
	}

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	SGXIsDevicePowered

 @Description

	Whether the device is powered, for the purposes of lockup detection.

 @Input psDeviceNode - pointer to device node

 @Return   IMG_BOOL  : Whether device is powered

******************************************************************************/
IMG_BOOL SGXIsDevicePowered(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	return PVRSRVIsDevicePowered(psDeviceNode->sDevId.ui32DeviceIndex);
}

/*!
*******************************************************************************

 @Function	SGXGetInternalDevInfoKM

 @Description
	Gets device information that is not intended to be passed
	on beyond the srvclient libs.

 @Input hDevCookie

 @Output psSGXInternalDevInfo

 @Return   PVRSRV_ERROR :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR SGXGetInternalDevInfoKM(IMG_HANDLE hDevCookie,
									SGX_INTERNAL_DEVINFO *psSGXInternalDevInfo)
{
	PVRSRV_SGXDEV_INFO *psDevInfo = (PVRSRV_SGXDEV_INFO *)((PVRSRV_DEVICE_NODE *)hDevCookie)->pvDevice;

	psSGXInternalDevInfo->ui32Flags = psDevInfo->ui32Flags;
	psSGXInternalDevInfo->bForcePTOff = (IMG_BOOL)psDevInfo->bForcePTOff;

	/* This should be patched up by OS bridge code */
	psSGXInternalDevInfo->hHostCtlKernelMemInfoHandle =
		(IMG_HANDLE)psDevInfo->psKernelSGXHostCtlMemInfo;

	return PVRSRV_OK;
}


/*****************************************************************************
 FUNCTION	: SGXCleanupRequest

 PURPOSE	: Wait for the microkernel to clean up its references to either a
 				render context or render target.

 PARAMETERS	:	psDeviceNode - SGX device node
 				psHWDataDevVAddr - Device Address of the resource
				ui32CleanupType - PVRSRV_CLEANUPCMD_*
				bForceCleanup - Skips sync polling

 RETURNS	: error status
*****************************************************************************/
PVRSRV_ERROR SGXCleanupRequest(PVRSRV_DEVICE_NODE *psDeviceNode,
							   IMG_DEV_VIRTADDR   *psHWDataDevVAddr,
							   IMG_UINT32          ui32CleanupType,
							   IMG_BOOL            bForceCleanup)
{
	PVRSRV_ERROR			eError;
	PVRSRV_SGXDEV_INFO		*psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_KERNEL_MEM_INFO	*psHostCtlMemInfo = psDevInfo->psKernelSGXHostCtlMemInfo;
	SGXMKIF_HOST_CTL		*psHostCtl = psHostCtlMemInfo->pvLinAddrKM;

	SGXMKIF_COMMAND		sCommand = {0};


	if (bForceCleanup != FORCE_CLEANUP)
	{
		sCommand.ui32Data[0] = ui32CleanupType;
		sCommand.ui32Data[1] = (psHWDataDevVAddr == IMG_NULL) ? 0 : psHWDataDevVAddr->uiAddr;
		PDUMPCOMMENTWITHFLAGS(0, "Request ukernel resource clean-up, Type %u, Data 0x%X", sCommand.ui32Data[0], sCommand.ui32Data[1]);
	
		eError = SGXScheduleCCBCommandKM(psDeviceNode, SGXMKIF_CMD_CLEANUP, &sCommand, KERNEL_ID, 0, IMG_NULL, IMG_FALSE);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "SGXCleanupRequest: Failed to submit clean-up command %d", eError));
			SGXDumpDebugInfo(psDevInfo, IMG_TRUE);
			PVR_DBG_BREAK;
			return eError;
		}
		
		/* Wait for the uKernel process the cleanup request */
		#if !defined(NO_HARDWARE)
		if(PollForValueKM(&psHostCtl->ui32CleanupStatus,
						  PVRSRV_USSE_EDM_CLEANUPCMD_COMPLETE,
						  PVRSRV_USSE_EDM_CLEANUPCMD_COMPLETE,
						  10 * MAX_HW_TIME_US,
						  1000,
						  IMG_TRUE) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SGXCleanupRequest: Wait for uKernel to clean up (%u) failed", ui32CleanupType));
			eError = PVRSRV_ERROR_TIMEOUT;
			SGXDumpDebugInfo(psDevInfo, IMG_FALSE);
			PVR_DBG_BREAK;
		}
		#endif
	
		#if defined(PDUMP)
		/*
			Pdump the poll as well.
			Note:
			We don't expect the cleanup to report busy as the client should have
			ensured the the resource has been finished with before requesting
			it's cleanup. This isn't true of the abnormal termination case but
			we don't expect to PDump that. Unless/until PDump has flow control
			there isn't anything else we can do.
		*/
		PDUMPCOMMENTWITHFLAGS(0, "Host Control - Poll for clean-up request to complete");
		PDUMPMEMPOL(psHostCtlMemInfo,
					offsetof(SGXMKIF_HOST_CTL, ui32CleanupStatus),
					PVRSRV_USSE_EDM_CLEANUPCMD_COMPLETE | PVRSRV_USSE_EDM_CLEANUPCMD_DONE,
					PVRSRV_USSE_EDM_CLEANUPCMD_COMPLETE | PVRSRV_USSE_EDM_CLEANUPCMD_DONE,
					PDUMP_POLL_OPERATOR_EQUAL,
					0,
					MAKEUNIQUETAG(psHostCtlMemInfo));
		#endif /* PDUMP */
	
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "SGXCleanupRequest: eError = %d", eError));
			return eError;
		}
	}

	if (psHostCtl->ui32CleanupStatus & PVRSRV_USSE_EDM_CLEANUPCMD_BUSY)
	{
		/* Only one flag should be set */
		PVR_ASSERT((psHostCtl->ui32CleanupStatus & PVRSRV_USSE_EDM_CLEANUPCMD_DONE) == 0);
		eError = PVRSRV_ERROR_RETRY;
		psHostCtl->ui32CleanupStatus &= ~(PVRSRV_USSE_EDM_CLEANUPCMD_COMPLETE | PVRSRV_USSE_EDM_CLEANUPCMD_BUSY);
	}
	else
	{
		eError = PVRSRV_OK;
		psHostCtl->ui32CleanupStatus &= ~(PVRSRV_USSE_EDM_CLEANUPCMD_COMPLETE | PVRSRV_USSE_EDM_CLEANUPCMD_DONE);
	}
	
	PDUMPMEM(IMG_NULL, psHostCtlMemInfo, offsetof(SGXMKIF_HOST_CTL, ui32CleanupStatus), sizeof(IMG_UINT32), 0, MAKEUNIQUETAG(psHostCtlMemInfo));

	/* Request the cache invalidate */
#if defined(SGX_FEATURE_SYSTEM_CACHE)
	psDevInfo->ui32CacheControl |= (SGXMKIF_CC_INVAL_BIF_SL | SGXMKIF_CC_INVAL_DATA);
#else
	psDevInfo->ui32CacheControl |= SGXMKIF_CC_INVAL_DATA;
#endif
	return eError;
}


typedef struct _SGX_HW_RENDER_CONTEXT_CLEANUP_
{
	PVRSRV_DEVICE_NODE *psDeviceNode;
    PVRSRV_KERNEL_MEM_INFO *psHWRenderContextMemInfo;
    IMG_HANDLE hBlockAlloc;
	PRESMAN_ITEM psResItem;
	IMG_BOOL bCleanupTimerRunning;
	IMG_PVOID pvTimeData;
} SGX_HW_RENDER_CONTEXT_CLEANUP;


static PVRSRV_ERROR SGXCleanupHWRenderContextCallback(IMG_PVOID		pvParam,
													  IMG_UINT32	ui32Param,
													  IMG_BOOL		bForceCleanup)
{
	PVRSRV_ERROR eError;
	SGX_HW_RENDER_CONTEXT_CLEANUP *psCleanup = pvParam;

	PVR_UNREFERENCED_PARAMETER(ui32Param);

	eError = SGXCleanupRequest(psCleanup->psDeviceNode,
					  &psCleanup->psHWRenderContextMemInfo->sDevVAddr,
					  PVRSRV_CLEANUPCMD_RC,
					  bForceCleanup);

	if (eError == PVRSRV_ERROR_RETRY)
	{
		if (!psCleanup->bCleanupTimerRunning)
		{
			OSTimeCreateWithUSOffset(&psCleanup->pvTimeData, MAX_CLEANUP_TIME_US);
			psCleanup->bCleanupTimerRunning = IMG_TRUE;
		}
		else
		{
			if (OSTimeHasTimePassed(psCleanup->pvTimeData))
			{
				eError = PVRSRV_ERROR_TIMEOUT_POLLING_FOR_VALUE;
				psCleanup->bCleanupTimerRunning = IMG_FALSE;
				OSTimeDestroy(psCleanup->pvTimeData);
			}
		}
	}
	else
	{
		if (psCleanup->bCleanupTimerRunning)
		{
			OSTimeDestroy(psCleanup->pvTimeData);
		}
	}

	if (eError != PVRSRV_ERROR_RETRY)
	{
	    /* Free the Device Mem allocated */
	    PVRSRVFreeDeviceMemKM(psCleanup->psDeviceNode,
	            psCleanup->psHWRenderContextMemInfo);
	
	    /* Finally, free the cleanup structure itself */
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
				  sizeof(SGX_HW_RENDER_CONTEXT_CLEANUP),
				  psCleanup,
				  psCleanup->hBlockAlloc);
		/*not nulling pointer, copy on stack*/
	}

	return eError;
}

typedef struct _SGX_HW_TRANSFER_CONTEXT_CLEANUP_
{
	PVRSRV_DEVICE_NODE *psDeviceNode;
    PVRSRV_KERNEL_MEM_INFO *psHWTransferContextMemInfo;
	IMG_HANDLE hBlockAlloc;
	PRESMAN_ITEM psResItem;
	IMG_BOOL bCleanupTimerRunning;
	IMG_PVOID pvTimeData;
} SGX_HW_TRANSFER_CONTEXT_CLEANUP;


static PVRSRV_ERROR SGXCleanupHWTransferContextCallback(IMG_PVOID	pvParam,
														IMG_UINT32	ui32Param,
														IMG_BOOL	bForceCleanup)
{
	PVRSRV_ERROR eError;
	SGX_HW_TRANSFER_CONTEXT_CLEANUP *psCleanup = (SGX_HW_TRANSFER_CONTEXT_CLEANUP *)pvParam;

	PVR_UNREFERENCED_PARAMETER(ui32Param);

	eError = SGXCleanupRequest(psCleanup->psDeviceNode,
					  &psCleanup->psHWTransferContextMemInfo->sDevVAddr,
					  PVRSRV_CLEANUPCMD_TC,
					  bForceCleanup);

	if (eError == PVRSRV_ERROR_RETRY)
	{
		if (!psCleanup->bCleanupTimerRunning)
		{
			OSTimeCreateWithUSOffset(&psCleanup->pvTimeData, MAX_CLEANUP_TIME_US);
			psCleanup->bCleanupTimerRunning = IMG_TRUE;
		}
		else
		{
			if (OSTimeHasTimePassed(psCleanup->pvTimeData))
			{
				eError = PVRSRV_ERROR_TIMEOUT_POLLING_FOR_VALUE;
				psCleanup->bCleanupTimerRunning = IMG_FALSE;
				OSTimeDestroy(psCleanup->pvTimeData);
			}
		}
	}
	else
	{
		if (psCleanup->bCleanupTimerRunning)
		{
			OSTimeDestroy(psCleanup->pvTimeData);
		}
	}

	if (eError != PVRSRV_ERROR_RETRY)
	{
	    /* Free the Device Mem allocated */
	    PVRSRVFreeDeviceMemKM(psCleanup->psDeviceNode,
	            psCleanup->psHWTransferContextMemInfo);
	
	    /* Finally, free the cleanup structure itself */
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
				  sizeof(SGX_HW_TRANSFER_CONTEXT_CLEANUP),
				  psCleanup,
				  psCleanup->hBlockAlloc);
		/*not nulling pointer, copy on stack*/
	}

	return eError;
}

IMG_EXPORT
IMG_HANDLE SGXRegisterHWRenderContextKM(IMG_HANDLE				hDeviceNode,
                                        IMG_CPU_VIRTADDR        *psHWRenderContextCpuVAddr,
                                        IMG_UINT32              ui32HWRenderContextSize,
                                        IMG_UINT32              ui32OffsetToPDDevPAddr,
                                        IMG_HANDLE              hDevMemContext,
                                        IMG_DEV_VIRTADDR        *psHWRenderContextDevVAddr,
										PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_ERROR eError;
	IMG_HANDLE hBlockAlloc;
	SGX_HW_RENDER_CONTEXT_CLEANUP *psCleanup;
    PVRSRV_DEVICE_NODE *psDeviceNode = (PVRSRV_DEVICE_NODE *)hDeviceNode;
	DEVICE_MEMORY_INFO	*psDevMemoryInfo;
    DEVICE_MEMORY_HEAP_INFO *psHeapInfo;
    IMG_HANDLE hDevMemContextInt;
    MMU_CONTEXT *psMMUContext;
    IMG_DEV_PHYADDR sPDDevPAddr;
    int iPtrByte;
    IMG_UINT8 *pSrc;
    IMG_UINT8 *pDst;
	PRESMAN_ITEM psResItem;
	IMG_UINT32 ui32PDDevPAddrInDirListFormat;

	eError = OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
						sizeof(SGX_HW_RENDER_CONTEXT_CLEANUP),
						(IMG_VOID **)&psCleanup,
						&hBlockAlloc,
						"SGX Hardware Render Context Cleanup");

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXRegisterHWRenderContextKM: Couldn't allocate memory for SGX_HW_RENDER_CONTEXT_CLEANUP structure"));
		goto exit0;
	}

	psDevMemoryInfo = &psDeviceNode->sDevMemoryInfo;
    psHeapInfo = &psDevMemoryInfo->psDeviceMemoryHeap[SGX_KERNEL_DATA_HEAP_ID];

    eError = PVRSRVAllocDeviceMemKM(hDeviceNode,
                               psPerProc,
                               psHeapInfo->hDevMemHeap,
                               PVRSRV_MEM_READ | PVRSRV_MEM_WRITE 
                                 | PVRSRV_MEM_NO_SYNCOBJ | PVRSRV_MEM_EDM_PROTECT 
                                 | PVRSRV_MEM_CACHE_CONSISTENT,
                               ui32HWRenderContextSize,
                               32,
                               IMG_NULL,
                               0,
                               0,0,0,IMG_NULL,	/* No sparse mapping data */
                               &psCleanup->psHWRenderContextMemInfo,
                               "HW Render Context");

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXRegisterHWRenderContextKM: Couldn't allocate device memory for HW Render Context"));
		goto exit1;
	}

    eError = OSCopyFromUser(psPerProc,
                            psCleanup->psHWRenderContextMemInfo->pvLinAddrKM,
                            psHWRenderContextCpuVAddr,
                            ui32HWRenderContextSize);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXRegisterHWRenderContextKM: Couldn't copy user-mode copy of HWContext into device memory"));
		goto exit2;
	}

    /* Pass the DevVAddr of the new context back up through the bridge */
    psHWRenderContextDevVAddr->uiAddr = psCleanup->psHWRenderContextMemInfo->sDevVAddr.uiAddr;

    /* Retrieve the PDDevPAddr */
    eError = PVRSRVLookupHandle(psPerProc->psHandleBase, 
                           &hDevMemContextInt,
                           hDevMemContext,
                           PVRSRV_HANDLE_TYPE_DEV_MEM_CONTEXT);

    if (eError != PVRSRV_OK)
    {
        PVR_DPF((PVR_DBG_ERROR, "SGXRegisterHWRenderContextKM: Can't lookup DevMem Context"));
        goto exit2;
    }

    psMMUContext = BM_GetMMUContextFromMemContext(hDevMemContextInt);
    sPDDevPAddr = psDeviceNode->pfnMMUGetPDDevPAddr(psMMUContext);

	/* 
	   The PDDevPAddr needs to be shifted-down, as the uKernel expects it in the
	   format it will be inserted into the DirList registers in.
	*/
	ui32PDDevPAddrInDirListFormat = (IMG_UINT32)(sPDDevPAddr.uiAddr >> SGX_MMU_PTE_ADDR_ALIGNSHIFT);

    /* 
       patch-in the Page-Directory Device-Physical address. Note that this is
       copied-in one byte at a time, as we have no guarantee that the usermode-
       provided ui32OffsetToPDDevPAddr is a validly-aligned address for the
       current CPU architecture.
     */
	pSrc = (IMG_UINT8 *)&ui32PDDevPAddrInDirListFormat;
    pDst = (IMG_UINT8 *)psCleanup->psHWRenderContextMemInfo->pvLinAddrKM;
    pDst += ui32OffsetToPDDevPAddr;

    for (iPtrByte = 0; iPtrByte < sizeof(ui32PDDevPAddrInDirListFormat); iPtrByte++)
    {
        pDst[iPtrByte] = pSrc[iPtrByte];
    }

#if defined(PDUMP)
    /* PDUMP the HW context */
	PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS, "HW Render context struct");

	PDUMPMEM(
        IMG_NULL, 
        psCleanup->psHWRenderContextMemInfo,
        0, 
        ui32HWRenderContextSize, 
        PDUMP_FLAGS_CONTINUOUS, 
        MAKEUNIQUETAG(psCleanup->psHWRenderContextMemInfo));

    /* PDUMP the PDDevPAddr */
	PDUMPCOMMENT("Page directory address in HW render context");
    PDUMPPDDEVPADDR(
            psCleanup->psHWRenderContextMemInfo,
            ui32OffsetToPDDevPAddr,
            sPDDevPAddr,
            MAKEUNIQUETAG(psCleanup->psHWRenderContextMemInfo),
            PDUMP_PD_UNIQUETAG);
#endif

	psCleanup->hBlockAlloc = hBlockAlloc;
	psCleanup->psDeviceNode = psDeviceNode;
	psCleanup->bCleanupTimerRunning = IMG_FALSE;

	psResItem = ResManRegisterRes(psPerProc->hResManContext,
								  RESMAN_TYPE_HW_RENDER_CONTEXT,
								  (IMG_VOID *)psCleanup,
								  0,
								  &SGXCleanupHWRenderContextCallback);

    if (psResItem == IMG_NULL)
    {
        PVR_DPF((PVR_DBG_ERROR, "SGXRegisterHWRenderContextKM: ResManRegisterRes failed"));
        goto exit2;
    }

	psCleanup->psResItem = psResItem;

	return (IMG_HANDLE)psCleanup;

/* Error exit paths */
exit2:
	 PVRSRVFreeDeviceMemKM(hDeviceNode,
                           psCleanup->psHWRenderContextMemInfo);
exit1:
    OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
              sizeof(SGX_HW_RENDER_CONTEXT_CLEANUP),
              psCleanup,
              psCleanup->hBlockAlloc);
    /*not nulling pointer, out of scope*/
exit0:
    return IMG_NULL;
}

IMG_EXPORT
PVRSRV_ERROR SGXUnregisterHWRenderContextKM(IMG_HANDLE hHWRenderContext, IMG_BOOL bForceCleanup)
{
	PVRSRV_ERROR eError;
	SGX_HW_RENDER_CONTEXT_CLEANUP *psCleanup;

	PVR_ASSERT(hHWRenderContext != IMG_NULL);

	psCleanup = (SGX_HW_RENDER_CONTEXT_CLEANUP *)hHWRenderContext;

	if (psCleanup == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXUnregisterHWRenderContextKM: invalid parameter"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = ResManFreeResByPtr(psCleanup->psResItem, bForceCleanup);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXUnregisterHWRenderContextKM: ResManFreeResByPtr failed %d", eError));
		PVR_DPF((PVR_DBG_ERROR, "ResManFreeResByPtr: hResItem 0x%x", (unsigned int)psCleanup->psResItem));
	}
	return eError;
}


IMG_EXPORT
IMG_HANDLE SGXRegisterHWTransferContextKM(IMG_HANDLE				hDeviceNode,
                                          IMG_CPU_VIRTADDR          *psHWTransferContextCpuVAddr,
                                          IMG_UINT32                ui32HWTransferContextSize,
                                          IMG_UINT32                ui32OffsetToPDDevPAddr,
                                          IMG_HANDLE                hDevMemContext,
                                          IMG_DEV_VIRTADDR          *psHWTransferContextDevVAddr,
										  PVRSRV_PER_PROCESS_DATA	*psPerProc)
{
	PVRSRV_ERROR eError;
	IMG_HANDLE hBlockAlloc;
	SGX_HW_TRANSFER_CONTEXT_CLEANUP *psCleanup;
    PVRSRV_DEVICE_NODE *psDeviceNode = (PVRSRV_DEVICE_NODE *)hDeviceNode;
	DEVICE_MEMORY_INFO	*psDevMemoryInfo;
    DEVICE_MEMORY_HEAP_INFO *psHeapInfo;
    IMG_HANDLE hDevMemContextInt;
    MMU_CONTEXT *psMMUContext;
    IMG_DEV_PHYADDR sPDDevPAddr;
    int iPtrByte;
    IMG_UINT8 *pSrc;
    IMG_UINT8 *pDst;
	PRESMAN_ITEM psResItem;
	IMG_UINT32 ui32PDDevPAddrInDirListFormat;

	eError = OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
						sizeof(SGX_HW_TRANSFER_CONTEXT_CLEANUP),
						(IMG_VOID **)&psCleanup,
						&hBlockAlloc,
						"SGX Hardware Transfer Context Cleanup");

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXRegisterHWTransferContextKM: Couldn't allocate memory for SGX_HW_TRANSFER_CONTEXT_CLEANUP structure"));
		goto exit0;
	}

	psDevMemoryInfo = &psDeviceNode->sDevMemoryInfo;
    psHeapInfo = &psDevMemoryInfo->psDeviceMemoryHeap[SGX_KERNEL_DATA_HEAP_ID];

	PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS, "Allocate HW Transfer context");
    eError = PVRSRVAllocDeviceMemKM(hDeviceNode,
                               psPerProc,
                               psHeapInfo->hDevMemHeap,
                               PVRSRV_MEM_READ | PVRSRV_MEM_WRITE 
                                 | PVRSRV_MEM_NO_SYNCOBJ | PVRSRV_MEM_EDM_PROTECT 
                                 | PVRSRV_MEM_CACHE_CONSISTENT,
                               ui32HWTransferContextSize,
                               32,
                               IMG_NULL,
                               0,
                               0,0,0,IMG_NULL,	/* No sparse mapping data */
                               &psCleanup->psHWTransferContextMemInfo,
                               "HW Render Context");

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXRegisterHWTransferContextKM: Couldn't allocate device memory for HW Render Context"));
		goto exit1;
	}

    eError = OSCopyFromUser(psPerProc,
                            psCleanup->psHWTransferContextMemInfo->pvLinAddrKM,
                            psHWTransferContextCpuVAddr,
                            ui32HWTransferContextSize);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXRegisterHWTransferContextKM: Couldn't copy user-mode copy of HWContext into device memory"));
		goto exit2;
	}

    /* Pass the DevVAddr of the new context back up through the bridge */
    psHWTransferContextDevVAddr->uiAddr = psCleanup->psHWTransferContextMemInfo->sDevVAddr.uiAddr;

    /* Retrieve the PDDevPAddr */
    eError = PVRSRVLookupHandle(psPerProc->psHandleBase, 
                           &hDevMemContextInt,
                           hDevMemContext,
                           PVRSRV_HANDLE_TYPE_DEV_MEM_CONTEXT);

    if (eError != PVRSRV_OK)
    {
        PVR_DPF((PVR_DBG_ERROR, "SGXRegisterHWTransferContextKM: Can't lookup DevMem Context"));
        goto exit2;
    }

    psMMUContext = BM_GetMMUContextFromMemContext(hDevMemContextInt);
    sPDDevPAddr = psDeviceNode->pfnMMUGetPDDevPAddr(psMMUContext);

	/* 
	   The PDDevPAddr needs to be shifted-down, as the uKernel expects it in the
	   format it will be inserted into the DirList registers in.
	*/
	ui32PDDevPAddrInDirListFormat = (IMG_UINT32)(sPDDevPAddr.uiAddr >> SGX_MMU_PTE_ADDR_ALIGNSHIFT);

    /* 
       patch-in the Page-Directory Device-Physical address. Note that this is
       copied-in one byte at a time, as we have no guarantee that the usermode-
       provided ui32OffsetToPDDevPAddr is a validly-aligned address for the
       current CPU architecture.
     */
	pSrc = (IMG_UINT8 *)&ui32PDDevPAddrInDirListFormat;
    pDst = (IMG_UINT8 *)psCleanup->psHWTransferContextMemInfo->pvLinAddrKM;
    pDst += ui32OffsetToPDDevPAddr;

    for (iPtrByte = 0; iPtrByte < sizeof(ui32PDDevPAddrInDirListFormat); iPtrByte++)
    {
        pDst[iPtrByte] = pSrc[iPtrByte];
    }

#if defined(PDUMP)
    /* PDUMP the HW Transfer Context */
	PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS, "HW Transfer context struct");

	PDUMPMEM(
        IMG_NULL, 
        psCleanup->psHWTransferContextMemInfo,
        0, 
        ui32HWTransferContextSize, 
        PDUMP_FLAGS_CONTINUOUS, 
        MAKEUNIQUETAG(psCleanup->psHWTransferContextMemInfo));

    /* PDUMP the PDDevPAddr */
	PDUMPCOMMENT("Page directory address in HW transfer context");

    PDUMPPDDEVPADDR(
            psCleanup->psHWTransferContextMemInfo,
            ui32OffsetToPDDevPAddr,
            sPDDevPAddr,
            MAKEUNIQUETAG(psCleanup->psHWTransferContextMemInfo),
            PDUMP_PD_UNIQUETAG);
#endif

	psCleanup->hBlockAlloc = hBlockAlloc;
	psCleanup->psDeviceNode = psDeviceNode;
	psCleanup->bCleanupTimerRunning = IMG_FALSE;

	psResItem = ResManRegisterRes(psPerProc->hResManContext,
								  RESMAN_TYPE_HW_TRANSFER_CONTEXT,
								  psCleanup,
								  0,
								  &SGXCleanupHWTransferContextCallback);

	if (psResItem == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXRegisterHWTransferContextKM: ResManRegisterRes failed"));
        goto exit2;
    }

	psCleanup->psResItem = psResItem;

	return (IMG_HANDLE)psCleanup;

/* Error exit paths */
exit2:
    PVRSRVFreeDeviceMemKM(hDeviceNode,
            psCleanup->psHWTransferContextMemInfo);
exit1:
    OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
              sizeof(SGX_HW_TRANSFER_CONTEXT_CLEANUP),
              psCleanup,
              psCleanup->hBlockAlloc);
    /*not nulling pointer, out of scope*/

exit0:
    return IMG_NULL;
}

IMG_EXPORT
PVRSRV_ERROR SGXUnregisterHWTransferContextKM(IMG_HANDLE hHWTransferContext, IMG_BOOL bForceCleanup)
{
	PVRSRV_ERROR eError;
	SGX_HW_TRANSFER_CONTEXT_CLEANUP *psCleanup;

	PVR_ASSERT(hHWTransferContext != IMG_NULL);

	psCleanup = (SGX_HW_TRANSFER_CONTEXT_CLEANUP *)hHWTransferContext;

	if (psCleanup == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXUnregisterHWTransferContextKM: invalid parameter"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = ResManFreeResByPtr(psCleanup->psResItem, bForceCleanup);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXUnregisterHWTransferContextKM: ResManFreeResByPtr failed %d", eError));
		PVR_DPF((PVR_DBG_ERROR, "ResManFreeResByPtr: hResItem 0x%x", (unsigned int)psCleanup->psResItem));
	}
	return eError;
}

IMG_EXPORT
PVRSRV_ERROR SGXSetTransferContextPriorityKM(
                IMG_HANDLE hDeviceNode,
                IMG_HANDLE hHWTransferContext,
                IMG_UINT32 ui32Priority,
                IMG_UINT32 ui32OffsetOfPriorityField)
{
	SGX_HW_TRANSFER_CONTEXT_CLEANUP *psCleanup;
    IMG_UINT8 *pSrc;
    IMG_UINT8 *pDst;
    int iPtrByte;
	PVR_UNREFERENCED_PARAMETER(hDeviceNode);

    if (hHWTransferContext != IMG_NULL)
    {
        psCleanup = (SGX_HW_TRANSFER_CONTEXT_CLEANUP *)hHWTransferContext;

        if ((ui32OffsetOfPriorityField + sizeof(ui32Priority)) 
            >= psCleanup->psHWTransferContextMemInfo->uAllocSize)
        {
            PVR_DPF((
                PVR_DBG_ERROR, 
                "SGXSetTransferContextPriorityKM: invalid context prioirty offset"));

            return PVRSRV_ERROR_INVALID_PARAMS;
        }

        /*
           cannot be sure that offset (passed from user-land) is safe to deref
           as a word-ptr on current CPU arch: copy one byte at a time.
         */
        pDst = (IMG_UINT8 *)psCleanup->psHWTransferContextMemInfo->pvLinAddrKM;
        pDst += ui32OffsetOfPriorityField;
        pSrc = (IMG_UINT8 *)&ui32Priority;

        for (iPtrByte = 0; iPtrByte < sizeof(ui32Priority); iPtrByte++)
        {
            pDst[iPtrByte] = pSrc[iPtrByte];
        }
    }
    return PVRSRV_OK;
}

IMG_EXPORT
PVRSRV_ERROR SGXSetRenderContextPriorityKM(
                IMG_HANDLE hDeviceNode,
                IMG_HANDLE hHWRenderContext,
                IMG_UINT32 ui32Priority,
                IMG_UINT32 ui32OffsetOfPriorityField)
{
	SGX_HW_RENDER_CONTEXT_CLEANUP *psCleanup;
    IMG_UINT8 *pSrc;
    IMG_UINT8 *pDst;
    int iPtrByte;
	PVR_UNREFERENCED_PARAMETER(hDeviceNode);

    if (hHWRenderContext != IMG_NULL)
    {
        psCleanup = (SGX_HW_RENDER_CONTEXT_CLEANUP *)hHWRenderContext;
        if ((ui32OffsetOfPriorityField + sizeof(ui32Priority)) 
            >= psCleanup->psHWRenderContextMemInfo->uAllocSize)
        {
            PVR_DPF((
                PVR_DBG_ERROR, 
                "SGXSetContextPriorityKM: invalid HWRenderContext prioirty offset"));

            return PVRSRV_ERROR_INVALID_PARAMS;
        }

        /*
           cannot be sure that offset (passed from user-land) is safe to deref
           as a word-ptr on current CPU arch: copy one byte at a time.
         */
        pDst = (IMG_UINT8 *)psCleanup->psHWRenderContextMemInfo->pvLinAddrKM;
        pDst += ui32OffsetOfPriorityField;

        pSrc = (IMG_UINT8 *)&ui32Priority;

        for (iPtrByte = 0; iPtrByte < sizeof(ui32Priority); iPtrByte++)
        {
            pDst[iPtrByte] = pSrc[iPtrByte];
        }
    }
    return PVRSRV_OK;
}

#if defined(SGX_FEATURE_2D_HARDWARE)
typedef struct _SGX_HW_2D_CONTEXT_CLEANUP_
{
	PVRSRV_DEVICE_NODE *psDeviceNode;
	PVRSRV_KERNEL_MEM_INFO *psHW2DContextMemInfo;
	IMG_HANDLE hBlockAlloc;
	PRESMAN_ITEM psResItem;
	IMG_BOOL bCleanupTimerRunning;
	IMG_PVOID pvTimeData;
} SGX_HW_2D_CONTEXT_CLEANUP;

static PVRSRV_ERROR SGXCleanupHW2DContextCallback(IMG_PVOID  pvParam,
												  IMG_UINT32 ui32Param,
												  IMG_BOOL   bForceCleanup)
{
	PVRSRV_ERROR eError;
	SGX_HW_2D_CONTEXT_CLEANUP *psCleanup = (SGX_HW_2D_CONTEXT_CLEANUP *)pvParam;

	PVR_UNREFERENCED_PARAMETER(ui32Param);

    /* First, ensure the context is no longer being utilised */
    eError = SGXCleanupRequest(psCleanup->psDeviceNode,
					  &psCleanup->psHW2DContextMemInfo->sDevVAddr,
					  PVRSRV_CLEANUPCMD_2DC,
					  bForceCleanup);

	if (eError == PVRSRV_ERROR_RETRY)
	{
		if (!psCleanup->bCleanupTimerRunning)
		{
			OSTimeCreateWithUSOffset(&psCleanup->pvTimeData, MAX_CLEANUP_TIME_US);
			psCleanup->bCleanupTimerRunning = IMG_TRUE;
		}
		else
		{
			if (OSTimeHasTimePassed(psCleanup->pvTimeData))
			{
				eError = PVRSRV_ERROR_TIMEOUT_POLLING_FOR_VALUE;
				psCleanup->bCleanupTimerRunning = IMG_FALSE;
				OSTimeDestroy(psCleanup->pvTimeData);
			}
		}
	}
	else
	{
		if (psCleanup->bCleanupTimerRunning)
		{
			OSTimeDestroy(psCleanup->pvTimeData);
		}
	}

	if (eError != PVRSRV_ERROR_RETRY)
	{
	    /* Free the Device Mem allocated */
	    PVRSRVFreeDeviceMemKM(psCleanup->psDeviceNode,
	            psCleanup->psHW2DContextMemInfo);
	
	    /* Finally, free the cleanup structure itself */
	    OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
				  sizeof(SGX_HW_2D_CONTEXT_CLEANUP),
				  psCleanup,
				  psCleanup->hBlockAlloc);
	                  /*not nulling pointer, copy on stack*/
	}
	return eError;
}

IMG_HANDLE SGXRegisterHW2DContextKM(IMG_HANDLE				hDeviceNode,
                                    IMG_CPU_VIRTADDR        *psHW2DContextCpuVAddr,
                                    IMG_UINT32              ui32HW2DContextSize,
                                    IMG_UINT32              ui32OffsetToPDDevPAddr,
                                    IMG_HANDLE              hDevMemContext,
                                    IMG_DEV_VIRTADDR        *psHW2DContextDevVAddr,
									PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_ERROR eError;
	IMG_HANDLE hBlockAlloc;
	SGX_HW_2D_CONTEXT_CLEANUP *psCleanup;
    PVRSRV_DEVICE_NODE *psDeviceNode = (PVRSRV_DEVICE_NODE *)hDeviceNode;
	DEVICE_MEMORY_INFO	*psDevMemoryInfo;
    DEVICE_MEMORY_HEAP_INFO *psHeapInfo;
    IMG_HANDLE hDevMemContextInt;
    MMU_CONTEXT *psMMUContext;
    IMG_DEV_PHYADDR sPDDevPAddr;
    int iPtrByte;
    IMG_UINT8 *pSrc;
    IMG_UINT8 *pDst;
	PRESMAN_ITEM psResItem;
	IMG_UINT32 ui32PDDevPAddrInDirListFormat;

	eError = OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
						sizeof(SGX_HW_2D_CONTEXT_CLEANUP),
						(IMG_VOID **)&psCleanup,
						&hBlockAlloc,
						"SGX Hardware 2D Context Cleanup");

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXRegisterHW2DContextKM: Couldn't allocate memory for SGX_HW_2D_CONTEXT_CLEANUP structure"));
		goto exit0;
	}

	psDevMemoryInfo = &psDeviceNode->sDevMemoryInfo;
    psHeapInfo = &psDevMemoryInfo->psDeviceMemoryHeap[SGX_KERNEL_DATA_HEAP_ID];

    eError = PVRSRVAllocDeviceMemKM(hDeviceNode,
                               psPerProc,
                               psHeapInfo->hDevMemHeap,
                               PVRSRV_MEM_READ | PVRSRV_MEM_WRITE 
                                 | PVRSRV_MEM_NO_SYNCOBJ | PVRSRV_MEM_EDM_PROTECT 
                                 | PVRSRV_MEM_CACHE_CONSISTENT,
                               ui32HW2DContextSize,
                               32,
                               IMG_NULL,
                               0,
                               0,0,0,IMG_NULL,	/* No sparse mapping data */
                               &psCleanup->psHW2DContextMemInfo,
                               "HW 2D Context");

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXRegisterHW2DContextKM: Couldn't allocate device memory for HW Render Context"));
		goto exit1;
	}

    eError = OSCopyFromUser(psPerProc,
                            psCleanup->psHW2DContextMemInfo->pvLinAddrKM,
                            psHW2DContextCpuVAddr,
                            ui32HW2DContextSize);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXRegisterHW2DContextKM: Couldn't copy user-mode copy of HWContext into device memory"));
		goto exit2;
	}
 
    /* Pass the DevVAddr of the new context back up through the bridge */
    psHW2DContextDevVAddr->uiAddr = psCleanup->psHW2DContextMemInfo->sDevVAddr.uiAddr;

    /* Retrieve the PDDevPAddr */
    eError = PVRSRVLookupHandle(psPerProc->psHandleBase, 
                           &hDevMemContextInt,
                           hDevMemContext,
                           PVRSRV_HANDLE_TYPE_DEV_MEM_CONTEXT);

    if (eError != PVRSRV_OK)
    {
        PVR_DPF((PVR_DBG_ERROR, "SGXRegisterHW2DContextKM: Can't lookup DevMem Context"));
        goto exit2;
    }

    psMMUContext = BM_GetMMUContextFromMemContext(hDevMemContextInt);
    sPDDevPAddr = psDeviceNode->pfnMMUGetPDDevPAddr(psMMUContext);

	/* 
	   The PDDevPAddr needs to be shifted-down, as the uKernel expects it in the
	   format it will be inserted into the DirList registers in.
	*/
	ui32PDDevPAddrInDirListFormat = sPDDevPAddr.uiAddr >> SGX_MMU_PTE_ADDR_ALIGNSHIFT;

    /* 
       patch-in the Page-Directory Device-Physical address. Note that this is
       copied-in one byte at a time, as we have no guarantee that the usermode-
       provided ui32OffsetToPDDevPAddr is a validly-aligned address for the
       current CPU architecture.
     */
	pSrc = (IMG_UINT8 *)&ui32PDDevPAddrInDirListFormat;
    pDst = (IMG_UINT8 *)psCleanup->psHW2DContextMemInfo->pvLinAddrKM;
    pDst += ui32OffsetToPDDevPAddr;

    for (iPtrByte = 0; iPtrByte < sizeof(ui32PDDevPAddrInDirListFormat); iPtrByte++)
    {
        pDst[iPtrByte] = pSrc[iPtrByte];
    }

#if defined(PDUMP)
    /* PDUMP the HW 2D Context */
	PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS, "HW 2D context struct");

	PDUMPMEM(
        IMG_NULL, 
        psCleanup->psHW2DContextMemInfo,
        0, 
        ui32HW2DContextSize, 
        PDUMP_FLAGS_CONTINUOUS, 
        MAKEUNIQUETAG(psCleanup->psHW2DContextMemInfo));

    /* PDUMP the PDDevPAddr */
	PDUMPCOMMENT("Page directory address in HW 2D transfer context");
    PDUMPPDDEVPADDR(
            psCleanup->psHW2DContextMemInfo,
            ui32OffsetToPDDevPAddr,
            sPDDevPAddr,
            MAKEUNIQUETAG(psCleanup->psHW2DContextMemInfo),
            PDUMP_PD_UNIQUETAG);
#endif

	psCleanup->hBlockAlloc = hBlockAlloc;
	psCleanup->psDeviceNode = psDeviceNode;
	psCleanup->bCleanupTimerRunning = IMG_FALSE;

	psResItem = ResManRegisterRes(psPerProc->hResManContext,
								  RESMAN_TYPE_HW_2D_CONTEXT,
								  psCleanup,
								  0,
								  &SGXCleanupHW2DContextCallback);

	if (psResItem == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXRegisterHW2DContextKM: ResManRegisterRes failed"));
        goto exit2;
	}

	psCleanup->psResItem = psResItem;

	return (IMG_HANDLE)psCleanup;

/* Error exit paths */
exit2:
    PVRSRVFreeDeviceMemKM(hDeviceNode,
                         psCleanup->psHW2DContextMemInfo);
exit1:
    OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
              sizeof(SGX_HW_2D_CONTEXT_CLEANUP),
              psCleanup,
              psCleanup->hBlockAlloc);
    /*not nulling pointer, out of scope*/
exit0:
    return IMG_NULL;
}

IMG_EXPORT
PVRSRV_ERROR SGXUnregisterHW2DContextKM(IMG_HANDLE hHW2DContext, IMG_BOOL bForceCleanup)
{
	PVRSRV_ERROR eError;
	SGX_HW_2D_CONTEXT_CLEANUP *psCleanup;

	PVR_ASSERT(hHW2DContext != IMG_NULL);

	if (hHW2DContext == IMG_NULL)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	psCleanup = (SGX_HW_2D_CONTEXT_CLEANUP *)hHW2DContext;

	eError = ResManFreeResByPtr(psCleanup->psResItem, bForceCleanup);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXUnregisterHW2DContextKM: ResManFreeResByPtr failed %d", eError));
		PVR_DPF((PVR_DBG_ERROR, "ResManFreeResByPtr: hResItem 0x%x", (unsigned int)psCleanup->psResItem));
	}
	return eError;
}
#endif /* #if defined(SGX_FEATURE_2D_HARDWARE)*/

/*!****************************************************************************
 @Function	SGX2DQuerySyncOpsCompleteKM

 @Input		psSyncInfo : Sync object to be queried

 @Return	IMG_TRUE - ops complete, IMG_FALSE - ops pending

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(SGX2DQuerySyncOpsComplete)
#endif
static INLINE
IMG_BOOL SGX2DQuerySyncOpsComplete(PVRSRV_KERNEL_SYNC_INFO	*psSyncInfo,
								   IMG_UINT32				ui32ReadOpsPending,
								   IMG_UINT32				ui32WriteOpsPending)
{
	PVRSRV_SYNC_DATA *psSyncData = psSyncInfo->psSyncData;

	return (IMG_BOOL)(
					  (psSyncData->ui32ReadOpsComplete >= ui32ReadOpsPending) &&
					  (psSyncData->ui32WriteOpsComplete >= ui32WriteOpsPending)
					 );
}

/*!****************************************************************************
 @Function	SGX2DQueryBlitsCompleteKM

 @Input		psDevInfo : SGX device info structure

 @Input		psSyncInfo : Sync object to be queried

 @Return	PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR SGX2DQueryBlitsCompleteKM(PVRSRV_SGXDEV_INFO	*psDevInfo,
									   PVRSRV_KERNEL_SYNC_INFO *psSyncInfo,
									   IMG_BOOL bWaitForComplete)
{
	IMG_UINT32	ui32ReadOpsPending, ui32WriteOpsPending;

	PVR_UNREFERENCED_PARAMETER(psDevInfo);

	PVR_DPF((PVR_DBG_CALLTRACE, "SGX2DQueryBlitsCompleteKM: Start"));

	ui32ReadOpsPending = psSyncInfo->psSyncData->ui32ReadOpsPending;
	ui32WriteOpsPending = psSyncInfo->psSyncData->ui32WriteOpsPending;

	if(SGX2DQuerySyncOpsComplete(psSyncInfo, ui32ReadOpsPending, ui32WriteOpsPending))
	{
		/* Instant success */
		PVR_DPF((PVR_DBG_CALLTRACE, "SGX2DQueryBlitsCompleteKM: No wait. Blits complete."));
		return PVRSRV_OK;
	}

	/* Not complete yet */
	if (!bWaitForComplete)
	{
		/* Just report not complete */
		PVR_DPF((PVR_DBG_CALLTRACE, "SGX2DQueryBlitsCompleteKM: No wait. Ops pending."));
		return PVRSRV_ERROR_CMD_NOT_PROCESSED;
	}

	/* Start polling */
	PVR_DPF((PVR_DBG_MESSAGE, "SGX2DQueryBlitsCompleteKM: Ops pending. Start polling."));

	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		OSSleepms(1);

		if(SGX2DQuerySyncOpsComplete(psSyncInfo, ui32ReadOpsPending, ui32WriteOpsPending))
		{
			/* Success */
			PVR_DPF((PVR_DBG_CALLTRACE, "SGX2DQueryBlitsCompleteKM: Wait over.  Blits complete."));
			return PVRSRV_OK;
		}

		OSSleepms(1);
	} END_LOOP_UNTIL_TIMEOUT();

	/* Timed out */
	PVR_DPF((PVR_DBG_ERROR,"SGX2DQueryBlitsCompleteKM: Timed out. Ops pending."));

#if defined(DEBUG)
	{
		PVRSRV_SYNC_DATA *psSyncData = psSyncInfo->psSyncData;

		PVR_TRACE(("SGX2DQueryBlitsCompleteKM: Syncinfo: 0x%p, Syncdata: 0x%p",
				psSyncInfo, psSyncData));

		PVR_TRACE(("SGX2DQueryBlitsCompleteKM: Read ops complete: %d, Read ops pending: %d", psSyncData->ui32ReadOpsComplete, psSyncData->ui32ReadOpsPending));
		PVR_TRACE(("SGX2DQueryBlitsCompleteKM: Write ops complete: %d, Write ops pending: %d", psSyncData->ui32WriteOpsComplete, psSyncData->ui32WriteOpsPending));

	}
#endif

	return PVRSRV_ERROR_TIMEOUT;
}


IMG_EXPORT
PVRSRV_ERROR SGXFlushHWRenderTargetKM(IMG_HANDLE psDeviceNode,
									  IMG_DEV_VIRTADDR sHWRTDataSetDevVAddr,
									  IMG_BOOL bForceCleanup)
{
	PVR_ASSERT(sHWRTDataSetDevVAddr.uiAddr != IMG_NULL);

	return SGXCleanupRequest(psDeviceNode,
					  &sHWRTDataSetDevVAddr,
					  PVRSRV_CLEANUPCMD_RT,
					  bForceCleanup);
}


IMG_UINT32 SGXConvertTimeStamp(PVRSRV_SGXDEV_INFO	*psDevInfo,
							   IMG_UINT32			ui32TimeWraps,
							   IMG_UINT32			ui32Time)
{
#if defined(EUR_CR_TIMER)
	PVR_UNREFERENCED_PARAMETER(psDevInfo);
	PVR_UNREFERENCED_PARAMETER(ui32TimeWraps);
	return ui32Time;
#else
	IMG_UINT64	ui64Clocks;
	IMG_UINT32	ui32Clocksx16;

	ui64Clocks = ((IMG_UINT64)ui32TimeWraps * psDevInfo->ui32uKernelTimerClock) +
					(psDevInfo->ui32uKernelTimerClock - (ui32Time & EUR_CR_EVENT_TIMER_VALUE_MASK));
	ui32Clocksx16 = (IMG_UINT32)(ui64Clocks / 16);

	return ui32Clocksx16;
#endif /* EUR_CR_TIMER */
}


IMG_VOID SGXWaitClocks(PVRSRV_SGXDEV_INFO	*psDevInfo,
					   IMG_UINT32			ui32SGXClocks)
{
	/*
		Round up to the next microsecond.
	*/
	OSWaitus(1 + (ui32SGXClocks * 1000000 / psDevInfo->ui32CoreClockSpeed));
}



/******************************************************************************
 End of file (sgxutils.c)
******************************************************************************/
