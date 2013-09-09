/*************************************************************************/ /*!
@Title          Device specific initialisation routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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
#include "sgxmmu.h"
#include "services_headers.h"
#include "buffer_manager.h"
#include "sgxapi_km.h"
#include "sgxinfo.h"
#include "sgx_mkif_km.h"
#include "sgxconfig.h"
#include "sysconfig.h"
#include "pvr_bridge_km.h"

#include "sgx_bridge_km.h"

#include "pdump_km.h"
#include "ra.h"
#include "mmu.h"
#include "handle.h"
#include "perproc.h"

#include "sgxutils.h"
#include "pvrversion.h"
#include "sgx_options.h"

#include "lists.h"
#include "srvkm.h"
#include "ttrace.h"

#define SYS_SGX_CLOCK_SPEED                                        (333000000)

IMG_UINT32 g_ui32HostIRQCountSample;

unsigned int *g_debug_CCB_Info_RO;
unsigned int *g_debug_CCB_Info_WO;

extern unsigned int g_debug_CCB_Info_WCNT;
#if defined(PVRSRV_USSE_EDM_STATUS_DEBUG)

static const IMG_CHAR *SGXUKernelStatusString(IMG_UINT32 code)
{
	switch(code)
	{
#define MKTC_ST(x) \
		case x: \
			return #x;
#include "sgx_ukernel_status_codes.h"
		default:
			return "(Unknown)";
	}
}

#endif /* defined(PVRSRV_USSE_EDM_STATUS_DEBUG) */

#define VAR(x) #x
/* PRQA S 0881 11 */ /* ignore 'order of evaluation' warning */
#define CHECK_SIZE(NAME) \
{	\
	if (psSGXStructSizes->ui32Sizeof_##NAME != psDevInfo->sSGXStructSizes.ui32Sizeof_##NAME) \
	{	\
		PVR_DPF((PVR_DBG_ERROR, "SGXDevInitCompatCheck: Size check failed for SGXMKIF_%s (client) = %d bytes, (ukernel) = %d bytes\n", \
			VAR(NAME), \
			psDevInfo->sSGXStructSizes.ui32Sizeof_##NAME, \
			psSGXStructSizes->ui32Sizeof_##NAME )); \
		bStructSizesFailed = IMG_TRUE; \
	}	\
}

#if defined (SYS_USING_INTERRUPTS)
IMG_BOOL SGX_ISRHandler(IMG_VOID *pvData);
#endif


static
PVRSRV_ERROR SGXGetMiscInfoUkernel(PVRSRV_SGXDEV_INFO	*psDevInfo,
								   PVRSRV_DEVICE_NODE 	*psDeviceNode,
								   IMG_HANDLE hDevMemContext);
#if defined(PDUMP)
static
PVRSRV_ERROR SGXResetPDump(PVRSRV_DEVICE_NODE *psDeviceNode);
#endif

/*!
*******************************************************************************

 @Function	SGXCommandComplete

 @Description

 SGX command complete handler

 @Input psDeviceNode - SGX device node

 @Return none

******************************************************************************/
static IMG_VOID SGXCommandComplete(PVRSRV_DEVICE_NODE *psDeviceNode)
{
#if defined(OS_SUPPORTS_IN_LISR)
	if (OSInLISR(psDeviceNode->psSysData))
	{
		/*
		 * We shouldn't call SGXScheduleProcessQueuesKM in an
		 * LISR, as it may attempt to power up SGX.
		 * We assume that the LISR will schedule the MISR, which
		 * will test the following flag, and call
		 * SGXScheduleProcessQueuesKM if the flag is set.
		 */
		psDeviceNode->bReProcessDeviceCommandComplete = IMG_TRUE;
	}
	else
	{
		SGXScheduleProcessQueuesKM(psDeviceNode);
	}
#else
	SGXScheduleProcessQueuesKM(psDeviceNode);
#endif
}

/*!
*******************************************************************************

 @Function	DeinitDevInfo

 @Description

 Deinits DevInfo

 @Input none

 @Return none

******************************************************************************/
static IMG_UINT32 DeinitDevInfo(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	if (psDevInfo->psKernelCCBInfo != IMG_NULL)
	{
		/*
			Free CCB info.
		*/
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_SGX_CCB_INFO), psDevInfo->psKernelCCBInfo, IMG_NULL);
	}

	return PVRSRV_OK;
}

/*!
*******************************************************************************

 @Function	InitDevInfo

 @Description

 Loads DevInfo

 @Input psDeviceNode

 @Return PVRSRV_ERROR

******************************************************************************/
static PVRSRV_ERROR InitDevInfo(PVRSRV_PER_PROCESS_DATA *psPerProc,
								PVRSRV_DEVICE_NODE *psDeviceNode,
								SGX_BRIDGE_INIT_INFO *psInitInfo)
{
	PVRSRV_SGXDEV_INFO *psDevInfo = (PVRSRV_SGXDEV_INFO *)psDeviceNode->pvDevice;
	PVRSRV_ERROR		eError;

	PVRSRV_SGX_CCB_INFO	*psKernelCCBInfo = IMG_NULL;

	PVR_UNREFERENCED_PARAMETER(psPerProc);
	psDevInfo->sScripts = psInitInfo->sScripts;

	psDevInfo->psKernelCCBMemInfo = (PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelCCBMemInfo;
	psDevInfo->psKernelCCB = (PVRSRV_SGX_KERNEL_CCB *) psDevInfo->psKernelCCBMemInfo->pvLinAddrKM;

	psDevInfo->psKernelCCBCtlMemInfo = (PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelCCBCtlMemInfo;
	psDevInfo->psKernelCCBCtl = (PVRSRV_SGX_CCB_CTL *) psDevInfo->psKernelCCBCtlMemInfo->pvLinAddrKM;
	g_debug_CCB_Info_RO = &(psDevInfo->psKernelCCBCtl->ui32ReadOffset);
	g_debug_CCB_Info_WO = &(psDevInfo->psKernelCCBCtl->ui32WriteOffset);

	psDevInfo->psKernelCCBEventKickerMemInfo = (PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelCCBEventKickerMemInfo;
	psDevInfo->pui32KernelCCBEventKicker = (IMG_UINT32 *)psDevInfo->psKernelCCBEventKickerMemInfo->pvLinAddrKM;

	psDevInfo->psKernelSGXHostCtlMemInfo = (PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelSGXHostCtlMemInfo;
	psDevInfo->psSGXHostCtl = (SGXMKIF_HOST_CTL *)psDevInfo->psKernelSGXHostCtlMemInfo->pvLinAddrKM;

	psDevInfo->psKernelSGXTA3DCtlMemInfo = (PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelSGXTA3DCtlMemInfo;

#if defined(FIX_HW_BRN_31272) || defined(FIX_HW_BRN_31780) || defined(FIX_HW_BRN_33920)
	psDevInfo->psKernelSGXPTLAWriteBackMemInfo = (PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelSGXPTLAWriteBackMemInfo;
#endif

	psDevInfo->psKernelSGXMiscMemInfo = (PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelSGXMiscMemInfo;

#if defined(SGX_SUPPORT_HWPROFILING)
	psDevInfo->psKernelHWProfilingMemInfo = (PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelHWProfilingMemInfo;
#endif
#if defined(SUPPORT_SGX_HWPERF)
	psDevInfo->psKernelHWPerfCBMemInfo = (PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelHWPerfCBMemInfo;
#endif
	psDevInfo->psKernelTASigBufferMemInfo = psInitInfo->hKernelTASigBufferMemInfo;
	psDevInfo->psKernel3DSigBufferMemInfo = psInitInfo->hKernel3DSigBufferMemInfo;
#if defined(SGX_FEATURE_VDM_CONTEXT_SWITCH) && defined(FIX_HW_BRN_31559)
	psDevInfo->psKernelVDMSnapShotBufferMemInfo = (PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelVDMSnapShotBufferMemInfo;
	psDevInfo->psKernelVDMCtrlStreamBufferMemInfo = (PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelVDMCtrlStreamBufferMemInfo;
#endif
#if defined(SGX_FEATURE_VDM_CONTEXT_SWITCH) && \
	defined(FIX_HW_BRN_33657) && defined(SUPPORT_SECURE_33657_FIX)
	psDevInfo->psKernelVDMStateUpdateBufferMemInfo = (PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelVDMStateUpdateBufferMemInfo;
#endif
#if defined(PVRSRV_USSE_EDM_STATUS_DEBUG)
	psDevInfo->psKernelEDMStatusBufferMemInfo = (PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelEDMStatusBufferMemInfo;
#endif
	/*
	 * 	Assign client-side build options for later verification
	 */
	psDevInfo->ui32ClientBuildOptions = psInitInfo->ui32ClientBuildOptions;

	/*
	 * 	Assign microkernel IF structure sizes for later verification
	 */
	psDevInfo->sSGXStructSizes = psInitInfo->sSGXStructSizes;

	/*
		Setup the kernel version of the CCB control
	*/
	eError = OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
						sizeof(PVRSRV_SGX_CCB_INFO),
						(IMG_VOID **)&psKernelCCBInfo, 0,
						"SGX Circular Command Buffer Info");
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"InitDevInfo: Failed to alloc memory"));
		goto failed_allockernelccb;
	}


	OSMemSet(psKernelCCBInfo, 0, sizeof(PVRSRV_SGX_CCB_INFO));
	psKernelCCBInfo->psCCBMemInfo		= psDevInfo->psKernelCCBMemInfo;
	psKernelCCBInfo->psCCBCtlMemInfo	= psDevInfo->psKernelCCBCtlMemInfo;
	psKernelCCBInfo->psCommands			= psDevInfo->psKernelCCB->asCommands;
	psKernelCCBInfo->pui32WriteOffset	= &psDevInfo->psKernelCCBCtl->ui32WriteOffset;
	psKernelCCBInfo->pui32ReadOffset	= &psDevInfo->psKernelCCBCtl->ui32ReadOffset;
	psDevInfo->psKernelCCBInfo = psKernelCCBInfo;

	/*
		Copy the USE code addresses for the host kick.
	*/
	OSMemCopy(psDevInfo->aui32HostKickAddr, psInitInfo->aui32HostKickAddr,
			  SGXMKIF_CMD_MAX * sizeof(psDevInfo->aui32HostKickAddr[0]));

 	psDevInfo->bForcePTOff = IMG_FALSE;

	psDevInfo->ui32CacheControl = psInitInfo->ui32CacheControl;

	psDevInfo->ui32EDMTaskReg0 = psInitInfo->ui32EDMTaskReg0;
	psDevInfo->ui32EDMTaskReg1 = psInitInfo->ui32EDMTaskReg1;
	psDevInfo->ui32ClkGateCtl = psInitInfo->ui32ClkGateCtl;
	psDevInfo->ui32ClkGateCtl2 = psInitInfo->ui32ClkGateCtl2;
	psDevInfo->ui32ClkGateStatusReg = psInitInfo->ui32ClkGateStatusReg;
	psDevInfo->ui32ClkGateStatusMask = psInitInfo->ui32ClkGateStatusMask;
#if defined(SGX_FEATURE_MP)
	psDevInfo->ui32MasterClkGateStatusReg = psInitInfo->ui32MasterClkGateStatusReg;
	psDevInfo->ui32MasterClkGateStatusMask = psInitInfo->ui32MasterClkGateStatusMask;
	psDevInfo->ui32MasterClkGateStatus2Reg = psInitInfo->ui32MasterClkGateStatus2Reg;
	psDevInfo->ui32MasterClkGateStatus2Mask = psInitInfo->ui32MasterClkGateStatus2Mask;
#endif /* SGX_FEATURE_MP */


	/* Initialise Dev Data */
	OSMemCopy(&psDevInfo->asSGXDevData, &psInitInfo->asInitDevData, sizeof(psDevInfo->asSGXDevData));

	return PVRSRV_OK;

failed_allockernelccb:
	DeinitDevInfo(psDevInfo);

	return eError;
}




static PVRSRV_ERROR SGXRunScript(PVRSRV_SGXDEV_INFO *psDevInfo, SGX_INIT_COMMAND *psScript, IMG_UINT32 ui32NumInitCommands)
{
	IMG_UINT32 ui32PC;
	SGX_INIT_COMMAND *psComm;

	for (ui32PC = 0, psComm = psScript;
		ui32PC < ui32NumInitCommands;
		ui32PC++, psComm++)
	{
		switch (psComm->eOp)
		{
			case SGX_INIT_OP_WRITE_HW_REG:
			{
				OSWriteHWReg(psDevInfo->pvRegsBaseKM, psComm->sWriteHWReg.ui32Offset, psComm->sWriteHWReg.ui32Value);
				PDUMPCOMMENT("SGXRunScript: Write HW reg operation");
				PDUMPREG(SGX_PDUMPREG_NAME, psComm->sWriteHWReg.ui32Offset, psComm->sWriteHWReg.ui32Value);
				break;
			}
			case SGX_INIT_OP_READ_HW_REG:
			{
				OSReadHWReg(psDevInfo->pvRegsBaseKM, psComm->sReadHWReg.ui32Offset);
#if defined(PDUMP)
				PDUMPCOMMENT("SGXRunScript: Read HW reg operation");
				PDumpRegRead(SGX_PDUMPREG_NAME, psComm->sReadHWReg.ui32Offset, PDUMP_FLAGS_CONTINUOUS);
#endif
				break;
			}
#if defined(PDUMP)
			case SGX_INIT_OP_PDUMP_HW_REG:
			{
				PDUMPCOMMENT("SGXRunScript: Dump HW reg operation");
				PDUMPREG(SGX_PDUMPREG_NAME, psComm->sPDumpHWReg.ui32Offset, psComm->sPDumpHWReg.ui32Value);
				break;
			}
#endif
			case SGX_INIT_OP_HALT:
			{
				return PVRSRV_OK;
			}
			case SGX_INIT_OP_ILLEGAL:
			/* FALLTHROUGH */
			default:
			{
				PVR_DPF((PVR_DBG_ERROR,"SGXRunScript: PC %d: Illegal command: %d", ui32PC, psComm->eOp));
				return PVRSRV_ERROR_UNKNOWN_SCRIPT_OPERATION;
			}
		}

	}

	return PVRSRV_ERROR_UNKNOWN_SCRIPT_OPERATION;
}

#if defined(SUPPORT_MEMORY_TILING)
static PVRSRV_ERROR SGX_AllocMemTilingRangeInt(PVRSRV_SGXDEV_INFO *psDevInfo,
											   IMG_UINT32 ui32Start,
											   IMG_UINT32 ui32End,
										IMG_UINT32 ui32TilingStride,
										IMG_UINT32 *pui32RangeIndex)
{
	IMG_UINT32 i;
	IMG_UINT32 ui32Offset;
	IMG_UINT32 ui32Val;

	/* HW supports 10 ranges */
	for(i=0; i < SGX_BIF_NUM_TILING_RANGES; i++)
	{
		if((psDevInfo->ui32MemTilingUsage & (1U << i)) == 0)
		{
			/* mark in use */
			psDevInfo->ui32MemTilingUsage |= 1U << i;
			/* output range index if the caller wants it */
			if(pui32RangeIndex != IMG_NULL)
			{
				*pui32RangeIndex = i;
			}
			goto RangeAllocated;
		}
	}

	PVR_DPF((PVR_DBG_ERROR,"SGX_AllocMemTilingRange: all tiling ranges in use"));
	return PVRSRV_ERROR_EXCEEDED_HW_LIMITS;

RangeAllocated:

	/* An improperly aligned range could cause BIF not to tile some memory which is intended to be tiled,
	 * or cause BIF to tile some memory which is not intended to be.
	 */
	if(ui32Start & ~SGX_BIF_TILING_ADDR_MASK)
	{
		PVR_DPF((PVR_DBG_WARNING,"SGX_AllocMemTilingRangeInt: Tiling range start (0x%08X) fails"
						"alignment test", ui32Start));
	}
	if((ui32End + 0x00001000) & ~SGX_BIF_TILING_ADDR_MASK)
	{
		PVR_DPF((PVR_DBG_WARNING,"SGX_AllocMemTilingRangeInt: Tiling range end (0x%08X) fails"
						"alignment test", ui32End));
	}

	ui32Offset = EUR_CR_BIF_TILE0 + (i<<2);

	ui32Val = ((ui32TilingStride << EUR_CR_BIF_TILE0_CFG_SHIFT) & EUR_CR_BIF_TILE0_CFG_MASK)
			| (((ui32End>>SGX_BIF_TILING_ADDR_LSB) << EUR_CR_BIF_TILE0_MAX_ADDRESS_SHIFT) & EUR_CR_BIF_TILE0_MAX_ADDRESS_MASK)
			| (((ui32Start>>SGX_BIF_TILING_ADDR_LSB) << EUR_CR_BIF_TILE0_MIN_ADDRESS_SHIFT) & EUR_CR_BIF_TILE0_MIN_ADDRESS_MASK)
			| (EUR_CR_BIF_TILE0_ENABLE << EUR_CR_BIF_TILE0_CFG_SHIFT);

	OSWriteHWReg(psDevInfo->pvRegsBaseKM, ui32Offset, ui32Val);
	PDUMPREG(SGX_PDUMPREG_NAME, ui32Offset, ui32Val);

#if defined(SGX_FEATURE_BIF_WIDE_TILING_AND_4K_ADDRESS)
	ui32Offset = EUR_CR_BIF_TILE0_ADDR_EXT + (i<<2);

	ui32Val = (((ui32End>>SGX_BIF_TILING_EXT_ADDR_LSB) << EUR_CR_BIF_TILE0_ADDR_EXT_MAX_SHIFT) & EUR_CR_BIF_TILE0_ADDR_EXT_MAX_MASK)
			| (((ui32Start>>SGX_BIF_TILING_EXT_ADDR_LSB) << EUR_CR_BIF_TILE0_ADDR_EXT_MIN_SHIFT) & EUR_CR_BIF_TILE0_ADDR_EXT_MIN_MASK);

	OSWriteHWReg(psDevInfo->pvRegsBaseKM, ui32Offset, ui32Val);
	PDUMPREG(SGX_PDUMPREG_NAME, ui32Offset, ui32Val);
#endif /* SGX_FEATURE_BIF_WIDE_TILING_AND_4K_ADDRESS */

	return PVRSRV_OK;
}

#endif /* SUPPORT_MEMORY_TILING */

/*!
*******************************************************************************

 @Function	SGXInitialise

 @Description

 (client invoked) chip-reset and initialisation

 @Input pvDeviceNode - device info. structure
 @Input bHardwareRecovery - true if recovering powered hardware,
 							false if powering up

 @Return   PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR SGXInitialise(PVRSRV_SGXDEV_INFO	*psDevInfo,
						   IMG_BOOL				bHardwareRecovery)
{
	PVRSRV_ERROR			eError;
	PVRSRV_KERNEL_MEM_INFO	*psSGXHostCtlMemInfo = psDevInfo->psKernelSGXHostCtlMemInfo;
	SGXMKIF_HOST_CTL		*psSGXHostCtl = psSGXHostCtlMemInfo->pvLinAddrKM;
	static IMG_BOOL			bFirstTime = IMG_TRUE;
#if defined(PDUMP)
	IMG_BOOL				bPDumpIsSuspended = PDumpIsSuspended();
#endif /* PDUMP */

#if defined(SGX_FEATURE_MP)
	/* Slave core clocks must be enabled during reset */
#else
	SGXInitClocks(psDevInfo, PDUMP_FLAGS_CONTINUOUS);
#endif /* SGX_FEATURE_MP */
	
	/*
		Part 1 of the initialisation script runs before resetting SGX.
	*/
	PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS, "SGX initialisation script part 1\n");
	eError = SGXRunScript(psDevInfo, psDevInfo->sScripts.asInitCommandsPart1, SGX_MAX_INIT_COMMANDS);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SGXInitialise: SGXRunScript (part 1) failed (%d)", eError));
		return eError;
	}
	PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS, "End of SGX initialisation script part 1\n");

	/* Reset the chip */
	psDevInfo->ui32NumResets++;
	
#if !defined(SGX_FEATURE_MP)
	bHardwareRecovery |= bFirstTime;
#endif /* SGX_FEATURE_MP */
	
	SGXReset(psDevInfo, bHardwareRecovery, PDUMP_FLAGS_CONTINUOUS);

#if defined(EUR_CR_POWER)
#if defined(SGX531)
	/*
		Disable half the pipes.
		531 has 2 pipes within a 4 pipe framework, so 
		the 2 redundant pipes must be disabled even
		though they do not exist.
	*/
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_POWER, 1);
	PDUMPREG(SGX_PDUMPREG_NAME, EUR_CR_POWER, 1);
#else
	/* set the default pipe count (all fully enabled) */
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_POWER, 0);
	PDUMPREG(SGX_PDUMPREG_NAME, EUR_CR_POWER, 0);
#endif
#endif

	/* Initialise the kernel CCB event kicker value */
	*psDevInfo->pui32KernelCCBEventKicker = 0;
#if defined(PDUMP)
	if (!bPDumpIsSuspended)
	{
		psDevInfo->ui32KernelCCBEventKickerDumpVal = 0;
		PDUMPMEM(&psDevInfo->ui32KernelCCBEventKickerDumpVal,
				 psDevInfo->psKernelCCBEventKickerMemInfo, 0,
				 sizeof(*psDevInfo->pui32KernelCCBEventKicker), PDUMP_FLAGS_CONTINUOUS,
				 MAKEUNIQUETAG(psDevInfo->psKernelCCBEventKickerMemInfo));
	}
#endif /* PDUMP */

#if defined(SUPPORT_MEMORY_TILING)
	{
		/* Initialise EUR_CR_BIF_TILE registers for any tiling heaps */
		DEVICE_MEMORY_HEAP_INFO *psDeviceMemoryHeap = psDevInfo->pvDeviceMemoryHeap;
		IMG_UINT32 i;

		psDevInfo->ui32MemTilingUsage = 0;

		for(i=0; i<psDevInfo->ui32HeapCount; i++)
		{
			if(psDeviceMemoryHeap[i].ui32XTileStride > 0)
			{
				/* Set up the HW control registers */
				eError = SGX_AllocMemTilingRangeInt(
						psDevInfo,
						psDeviceMemoryHeap[i].sDevVAddrBase.uiAddr,
						psDeviceMemoryHeap[i].sDevVAddrBase.uiAddr
							+ psDeviceMemoryHeap[i].ui32HeapSize,
						psDeviceMemoryHeap[i].ui32XTileStride,
						NULL);
				if(eError != PVRSRV_OK)
				{
					PVR_DPF((PVR_DBG_ERROR, "Unable to allocate SGX BIF tiling range for heap: %s",
											psDeviceMemoryHeap[i].pszName));
					break;
				}
			}
		}
	}
#endif

	/*
		Part 2 of the initialisation script runs after resetting SGX.
	*/
	PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS, "SGX initialisation script part 2\n");
	eError = SGXRunScript(psDevInfo, psDevInfo->sScripts.asInitCommandsPart2, SGX_MAX_INIT_COMMANDS);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SGXInitialise: SGXRunScript (part 2) failed (%d)", eError));
		return eError;
	}
	PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS, "End of SGX initialisation script part 2\n");

	/* Record the system timestamp for the microkernel */
	psSGXHostCtl->ui32HostClock = OSClockus();

	psSGXHostCtl->ui32InitStatus = 0;
#if defined(PDUMP)
	PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS,
						  "Reset the SGX microkernel initialisation status\n");
	PDUMPMEM(IMG_NULL, psSGXHostCtlMemInfo,
			 offsetof(SGXMKIF_HOST_CTL, ui32InitStatus),
			 sizeof(IMG_UINT32), PDUMP_FLAGS_CONTINUOUS,
			 MAKEUNIQUETAG(psSGXHostCtlMemInfo));
	PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS,
						  "Initialise the microkernel\n");
#endif /* PDUMP */

#if defined(SGX_FEATURE_MULTI_EVENT_KICK)
	OSWriteMemoryBarrier();
	OSWriteHWReg(psDevInfo->pvRegsBaseKM,
				 SGX_MP_CORE_SELECT(EUR_CR_EVENT_KICK2, 0),
				 EUR_CR_EVENT_KICK2_NOW_MASK);
#else
	*psDevInfo->pui32KernelCCBEventKicker = (*psDevInfo->pui32KernelCCBEventKicker + 1) & 0xFF;
	OSWriteMemoryBarrier();
	OSWriteHWReg(psDevInfo->pvRegsBaseKM,
				 SGX_MP_CORE_SELECT(EUR_CR_EVENT_KICK, 0),
				 EUR_CR_EVENT_KICK_NOW_MASK);
#endif /* SGX_FEATURE_MULTI_EVENT_KICK */

	OSMemoryBarrier();

#if defined(PDUMP)
	/*
		Dump the host kick.
	*/
	if (!bPDumpIsSuspended)
	{
#if defined(SGX_FEATURE_MULTI_EVENT_KICK)
		PDUMPREG(SGX_PDUMPREG_NAME, SGX_MP_CORE_SELECT(EUR_CR_EVENT_KICK2, 0), EUR_CR_EVENT_KICK2_NOW_MASK);
#else
		psDevInfo->ui32KernelCCBEventKickerDumpVal = 1;
		PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS,
							  "First increment of the SGX event kicker value\n");
		PDUMPMEM(&psDevInfo->ui32KernelCCBEventKickerDumpVal,
				 psDevInfo->psKernelCCBEventKickerMemInfo,
				 0,
				 sizeof(IMG_UINT32),
				 PDUMP_FLAGS_CONTINUOUS,
				 MAKEUNIQUETAG(psDevInfo->psKernelCCBEventKickerMemInfo));
		PDUMPREG(SGX_PDUMPREG_NAME, SGX_MP_CORE_SELECT(EUR_CR_EVENT_KICK, 0), EUR_CR_EVENT_KICK_NOW_MASK);
#endif /* SGX_FEATURE_MULTI_EVENT_KICK */
	}
#endif /* PDUMP */

#if !defined(NO_HARDWARE)
	/*
		Wait for the microkernel to finish initialising.
	*/
	if (PollForValueKM(&psSGXHostCtl->ui32InitStatus,
					   PVRSRV_USSE_EDM_INIT_COMPLETE,
					   PVRSRV_USSE_EDM_INIT_COMPLETE,
					   MAX_HW_TIME_US,
					   MAX_HW_TIME_US/WAIT_TRY_COUNT,
					   IMG_FALSE) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXInitialise: Wait for uKernel initialisation failed"));

		SGXDumpDebugInfo(psDevInfo, IMG_FALSE);
		PVR_DBG_BREAK;

		return PVRSRV_ERROR_RETRY;
	}
#endif /* NO_HARDWARE */

#if defined(PDUMP)
	PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS,
						  "Wait for the SGX microkernel initialisation to complete");
	PDUMPMEMPOL(psSGXHostCtlMemInfo,
				offsetof(SGXMKIF_HOST_CTL, ui32InitStatus),
				PVRSRV_USSE_EDM_INIT_COMPLETE,
				PVRSRV_USSE_EDM_INIT_COMPLETE,
				PDUMP_POLL_OPERATOR_EQUAL,
				PDUMP_FLAGS_CONTINUOUS,
				MAKEUNIQUETAG(psSGXHostCtlMemInfo));
#endif /* PDUMP */

	PVR_ASSERT(psDevInfo->psKernelCCBCtl->ui32ReadOffset == psDevInfo->psKernelCCBCtl->ui32WriteOffset);

	bFirstTime = IMG_FALSE;
	
	return PVRSRV_OK;
}

/*!
*******************************************************************************

 @Function	SGXDeinitialise

 @Description

 (client invoked) chip-reset and deinitialisation

 @Input hDevCookie - device info. handle

 @Return   PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR SGXDeinitialise(IMG_HANDLE hDevCookie)

{
	PVRSRV_SGXDEV_INFO	*psDevInfo = (PVRSRV_SGXDEV_INFO *) hDevCookie;
	PVRSRV_ERROR		eError;

	/* Did SGXInitialise map the SGX registers in? */
	if (psDevInfo->pvRegsBaseKM == IMG_NULL)
	{
		return PVRSRV_OK;
	}

	eError = SGXRunScript(psDevInfo, psDevInfo->sScripts.asDeinitCommands, SGX_MAX_DEINIT_COMMANDS);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SGXDeinitialise: SGXRunScript failed (%d)", eError));
		return eError;
	}

	return PVRSRV_OK;
}


/*!
*******************************************************************************

 @Function	DevInitSGXPart1

 @Description

 Reset and initialise Chip

 @Input pvDeviceNode - device info. structure

 @Return   PVRSRV_ERROR

******************************************************************************/
static PVRSRV_ERROR DevInitSGXPart1 (IMG_VOID *pvDeviceNode)
{
	IMG_HANDLE hDevMemHeap = IMG_NULL;
	PVRSRV_SGXDEV_INFO	*psDevInfo;
	IMG_HANDLE		hKernelDevMemContext;
	IMG_DEV_PHYADDR		sPDDevPAddr;
	IMG_UINT32		i;
	PVRSRV_DEVICE_NODE  *psDeviceNode = (PVRSRV_DEVICE_NODE *)pvDeviceNode;
	DEVICE_MEMORY_HEAP_INFO *psDeviceMemoryHeap = psDeviceNode->sDevMemoryInfo.psDeviceMemoryHeap;
	PVRSRV_ERROR		eError;

	/* pdump info about the core */
	PDUMPCOMMENT("SGX Core Version Information: %s", SGX_CORE_FRIENDLY_NAME);
	
	#if defined(SGX_FEATURE_MP)
	#if !defined(SGX_FEATURE_MP_PLUS)
	PDUMPCOMMENT("SGX Multi-processor: %d cores", SGX_FEATURE_MP_CORE_COUNT);
	#else
	PDUMPCOMMENT("SGX Multi-processor: %d TA cores, %d 3D cores", SGX_FEATURE_MP_CORE_COUNT_TA, SGX_FEATURE_MP_CORE_COUNT_3D);
	#endif
	#endif /* SGX_FEATURE_MP */

#if (SGX_CORE_REV == 0)
	PDUMPCOMMENT("SGX Core Revision Information: head RTL");
#else
	PDUMPCOMMENT("SGX Core Revision Information: %d", SGX_CORE_REV);
#endif

	#if defined(SGX_FEATURE_SYSTEM_CACHE)
	PDUMPCOMMENT("SGX System Level Cache is present\r\n");
	#if defined(SGX_BYPASS_SYSTEM_CACHE)
	PDUMPCOMMENT("SGX System Level Cache is bypassed\r\n");
	#endif /* SGX_BYPASS_SYSTEM_CACHE */
	#endif /* SGX_FEATURE_SYSTEM_CACHE */

	PDUMPCOMMENT("SGX Initialisation Part 1");

	/* Allocate device control block */
	if(OSAllocMem( PVRSRV_OS_NON_PAGEABLE_HEAP,
					 sizeof(PVRSRV_SGXDEV_INFO),
					 (IMG_VOID **)&psDevInfo, IMG_NULL,
					 "SGX Device Info") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"DevInitSGXPart1 : Failed to alloc memory for DevInfo"));
		return (PVRSRV_ERROR_OUT_OF_MEMORY);
	}
	OSMemSet (psDevInfo, 0, sizeof(PVRSRV_SGXDEV_INFO));

	/* setup info from jdisplayconfig.h (variations controlled by build) */
	psDevInfo->eDeviceType 		= DEV_DEVICE_TYPE;
	psDevInfo->eDeviceClass 	= DEV_DEVICE_CLASS;

	/* Store the devinfo as its needed by dynamically enumerated systems called from BM */
	psDeviceNode->pvDevice = (IMG_PVOID)psDevInfo;

	/* get heap info from the devnode */
	psDevInfo->ui32HeapCount = psDeviceNode->sDevMemoryInfo.ui32HeapCount;
	psDevInfo->pvDeviceMemoryHeap = (IMG_VOID*)psDeviceMemoryHeap;

	/* create the kernel memory context */
	hKernelDevMemContext = BM_CreateContext(psDeviceNode,
											&sPDDevPAddr,
											IMG_NULL,
											IMG_NULL);
	if (hKernelDevMemContext == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"DevInitSGXPart1: Failed BM_CreateContext"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psDevInfo->sKernelPDDevPAddr = sPDDevPAddr;

	/* create the kernel, shared and shared_exported heaps */
	for(i=0; i<psDeviceNode->sDevMemoryInfo.ui32HeapCount; i++)
	{
		switch(psDeviceMemoryHeap[i].DevMemHeapType)
		{
			case DEVICE_MEMORY_HEAP_KERNEL:
			case DEVICE_MEMORY_HEAP_SHARED:
			case DEVICE_MEMORY_HEAP_SHARED_EXPORTED:
			{
				/* Shared PB heap could be zero size */
				if (psDeviceMemoryHeap[i].ui32HeapSize > 0)
				{
					hDevMemHeap = BM_CreateHeap (hKernelDevMemContext,
												&psDeviceMemoryHeap[i]);
					/*
						in the case of kernel context heaps just store
						the heap handle in the heap info structure
					*/
					psDeviceMemoryHeap[i].hDevMemHeap = hDevMemHeap;
				}
				break;
			}
		}
	}
#if defined(PDUMP)
	if(hDevMemHeap)
	{
		/* set up the MMU pdump info */
		psDevInfo->sMMUAttrib = *((BM_HEAP*)hDevMemHeap)->psMMUAttrib;
	}
#endif
	eError = MMU_BIFResetPDAlloc(psDevInfo);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"DevInitSGX : Failed to alloc memory for BIF reset"));
		return eError;
	}

	return PVRSRV_OK;
}

/*!
*******************************************************************************

 @Function	SGXGetInfoForSrvinitKM

 @Description

 Get SGX related information necessary for initilisation server

 @Input hDevHandle - device handle
	psInitInfo - pointer to structure for returned information

 @Output psInitInfo - pointer to structure containing returned information

 @Return   PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR SGXGetInfoForSrvinitKM(IMG_HANDLE hDevHandle, SGX_BRIDGE_INFO_FOR_SRVINIT *psInitInfo)
{
	PVRSRV_DEVICE_NODE	*psDeviceNode;
	PVRSRV_SGXDEV_INFO	*psDevInfo;
	PVRSRV_ERROR		eError;

	PDUMPCOMMENT("SGXGetInfoForSrvinit");

	psDeviceNode = (PVRSRV_DEVICE_NODE *)hDevHandle;
	psDevInfo = (PVRSRV_SGXDEV_INFO *)psDeviceNode->pvDevice;

	psInitInfo->sPDDevPAddr = psDevInfo->sKernelPDDevPAddr;

	eError = PVRSRVGetDeviceMemHeapsKM(hDevHandle, &psInitInfo->asHeapInfo[0]);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SGXGetInfoForSrvinit: PVRSRVGetDeviceMemHeapsKM failed (%d)", eError));
		return eError;
	}

	return eError;
}

/*!
*******************************************************************************

 @Function	DevInitSGXPart2KM

 @Description

 Reset and initialise Chip

 @Input pvDeviceNode - device info. structure

 @Return   PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR DevInitSGXPart2KM (PVRSRV_PER_PROCESS_DATA *psPerProc,
                                IMG_HANDLE hDevHandle,
                                SGX_BRIDGE_INIT_INFO *psInitInfo)
{
	PVRSRV_DEVICE_NODE		*psDeviceNode;
	PVRSRV_SGXDEV_INFO		*psDevInfo;
	PVRSRV_ERROR			eError;
	SGX_DEVICE_MAP			*psSGXDeviceMap;
	PVRSRV_DEV_POWER_STATE	eDefaultPowerState;

	PDUMPCOMMENT("SGX Initialisation Part 2");

	psDeviceNode = (PVRSRV_DEVICE_NODE *)hDevHandle;
	psDevInfo = (PVRSRV_SGXDEV_INFO *)psDeviceNode->pvDevice;

	/*
		Init devinfo
	*/
	eError = InitDevInfo(psPerProc, psDeviceNode, psInitInfo);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"DevInitSGXPart2KM: Failed to load EDM program"));
		goto failed_init_dev_info;
	}


	eError = SysGetDeviceMemoryMap(PVRSRV_DEVICE_TYPE_SGX,
									(IMG_VOID**)&psSGXDeviceMap);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"DevInitSGXPart2KM: Failed to get device memory map!"));
		return PVRSRV_ERROR_INIT_FAILURE;
	}

	/* Registers already mapped? */
	if (psSGXDeviceMap->pvRegsCpuVBase)
	{
		psDevInfo->pvRegsBaseKM = psSGXDeviceMap->pvRegsCpuVBase;
	}
	else
	{
		/* Map Regs */
		psDevInfo->pvRegsBaseKM = OSMapPhysToLin(psSGXDeviceMap->sRegsCpuPBase,
											   psSGXDeviceMap->ui32RegsSize,
											   PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
											   IMG_NULL);
		if (!psDevInfo->pvRegsBaseKM)
		{
			PVR_DPF((PVR_DBG_ERROR,"DevInitSGXPart2KM: Failed to map in regs\n"));
			return PVRSRV_ERROR_BAD_MAPPING;
		}
	}
	psDevInfo->ui32RegSize = psSGXDeviceMap->ui32RegsSize;
	psDevInfo->sRegsPhysBase = psSGXDeviceMap->sRegsSysPBase;


#if defined(SGX_FEATURE_HOST_PORT)
	if (psSGXDeviceMap->ui32Flags & SGX_HOSTPORT_PRESENT)
	{
		/* Map Host Port */
		psDevInfo->pvHostPortBaseKM = OSMapPhysToLin(psSGXDeviceMap->sHPCpuPBase,
									  	           psSGXDeviceMap->ui32HPSize,
									  	           PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
									  	           IMG_NULL);
		if (!psDevInfo->pvHostPortBaseKM)
		{
			PVR_DPF((PVR_DBG_ERROR,"DevInitSGXPart2KM: Failed to map in host port\n"));
			return PVRSRV_ERROR_BAD_MAPPING;
		}
		psDevInfo->ui32HPSize = psSGXDeviceMap->ui32HPSize;
		psDevInfo->sHPSysPAddr = psSGXDeviceMap->sHPSysPBase;
	}
#endif/* #ifdef SGX_FEATURE_HOST_PORT */

#if defined (SYS_USING_INTERRUPTS)

	/* Set up ISR callback information. */
	psDeviceNode->pvISRData = psDeviceNode;
	/* ISR handler address was set up earlier */
	PVR_ASSERT(psDeviceNode->pfnDeviceISR == SGX_ISRHandler);

#endif /* SYS_USING_INTERRUPTS */

	/* Prevent the microkernel being woken up before there is something to do. */
	psDevInfo->psSGXHostCtl->ui32PowerStatus |= PVRSRV_USSE_EDM_POWMAN_NO_WORK;
	eDefaultPowerState = PVRSRV_DEV_POWER_STATE_OFF;
	/* Register the device with the power manager. */
	eError = PVRSRVRegisterPowerDevice (psDeviceNode->sDevId.ui32DeviceIndex,
										&SGXPrePowerState, &SGXPostPowerState,
										&SGXPreClockSpeedChange, &SGXPostClockSpeedChange,
										(IMG_HANDLE)psDeviceNode,
										PVRSRV_DEV_POWER_STATE_OFF,
										eDefaultPowerState);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"DevInitSGXPart2KM: failed to register device with power manager"));
		return eError;
	}

#if defined(SUPPORT_EXTERNAL_SYSTEM_CACHE)
	/* map the external system cache control registers into the SGX MMU */
	psDevInfo->ui32ExtSysCacheRegsSize = psSGXDeviceMap->ui32ExtSysCacheRegsSize;
	psDevInfo->sExtSysCacheRegsDevPBase = psSGXDeviceMap->sExtSysCacheRegsDevPBase;
	eError = MMU_MapExtSystemCacheRegs(psDeviceNode);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SGXInitialise : Failed to map external system cache registers"));
		return eError;
	}
#endif /* SUPPORT_EXTERNAL_SYSTEM_CACHE */

	/*
		Initialise the Kernel CCB
	*/
	OSMemSet(psDevInfo->psKernelCCB, 0, sizeof(PVRSRV_SGX_KERNEL_CCB));
	OSMemSet(psDevInfo->psKernelCCBCtl, 0, sizeof(PVRSRV_SGX_CCB_CTL));
	OSMemSet(psDevInfo->pui32KernelCCBEventKicker, 0, sizeof(*psDevInfo->pui32KernelCCBEventKicker));
	PDUMPCOMMENT("Initialise Kernel CCB");
	PDUMPMEM(IMG_NULL, psDevInfo->psKernelCCBMemInfo, 0, sizeof(PVRSRV_SGX_KERNEL_CCB), PDUMP_FLAGS_CONTINUOUS, MAKEUNIQUETAG(psDevInfo->psKernelCCBMemInfo));
	PDUMPCOMMENT("Initialise Kernel CCB Control");
	PDUMPMEM(IMG_NULL, psDevInfo->psKernelCCBCtlMemInfo, 0, sizeof(PVRSRV_SGX_CCB_CTL), PDUMP_FLAGS_CONTINUOUS, MAKEUNIQUETAG(psDevInfo->psKernelCCBCtlMemInfo));
	PDUMPCOMMENT("Initialise Kernel CCB Event Kicker");
	PDUMPMEM(IMG_NULL, psDevInfo->psKernelCCBEventKickerMemInfo, 0, sizeof(*psDevInfo->pui32KernelCCBEventKicker), PDUMP_FLAGS_CONTINUOUS, MAKEUNIQUETAG(psDevInfo->psKernelCCBEventKickerMemInfo));

	return PVRSRV_OK;

failed_init_dev_info:
	return eError;
}

/*!
*******************************************************************************

 @Function	DevDeInitSGX

 @Description

 Reset and deinitialise Chip

 @Input pvDeviceNode - device info. structure

 @Return   PVRSRV_ERROR

******************************************************************************/
static PVRSRV_ERROR DevDeInitSGX (IMG_VOID *pvDeviceNode)
{
	PVRSRV_DEVICE_NODE			*psDeviceNode = (PVRSRV_DEVICE_NODE *)pvDeviceNode;
	PVRSRV_SGXDEV_INFO			*psDevInfo = (PVRSRV_SGXDEV_INFO*)psDeviceNode->pvDevice;
	PVRSRV_ERROR				eError;
	IMG_UINT32					ui32Heap;
	DEVICE_MEMORY_HEAP_INFO		*psDeviceMemoryHeap;
	SGX_DEVICE_MAP				*psSGXDeviceMap;

	if (!psDevInfo)
	{
		/* Can happen if DevInitSGX failed */
		PVR_DPF((PVR_DBG_ERROR,"DevDeInitSGX: Null DevInfo"));
		return PVRSRV_OK;
	}

#if defined(SUPPORT_HW_RECOVERY)
	if (psDevInfo->hTimer)
	{
		eError = OSRemoveTimer(psDevInfo->hTimer);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"DevDeInitSGX: Failed to remove timer"));
			return 	eError;
		}
		psDevInfo->hTimer = IMG_NULL;
	}
#endif /* SUPPORT_HW_RECOVERY */

#if defined(SUPPORT_EXTERNAL_SYSTEM_CACHE)
	/* unmap the external system cache control registers  */
	eError = MMU_UnmapExtSystemCacheRegs(psDeviceNode);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"DevDeInitSGX: Failed to unmap ext system cache registers"));
		return eError;
	}
#endif /* SUPPORT_EXTERNAL_SYSTEM_CACHE */

	MMU_BIFResetPDFree(psDevInfo);

	/*
		DeinitDevInfo the DevInfo
	*/
	DeinitDevInfo(psDevInfo);

	/* Destroy heaps. */
	psDeviceMemoryHeap = (DEVICE_MEMORY_HEAP_INFO *)psDevInfo->pvDeviceMemoryHeap;
	for(ui32Heap=0; ui32Heap<psDeviceNode->sDevMemoryInfo.ui32HeapCount; ui32Heap++)
	{
		switch(psDeviceMemoryHeap[ui32Heap].DevMemHeapType)
		{
			case DEVICE_MEMORY_HEAP_KERNEL:
			case DEVICE_MEMORY_HEAP_SHARED:
			case DEVICE_MEMORY_HEAP_SHARED_EXPORTED:
			{
				if (psDeviceMemoryHeap[ui32Heap].hDevMemHeap != IMG_NULL)
				{
					BM_DestroyHeap(psDeviceMemoryHeap[ui32Heap].hDevMemHeap);
				}
				break;
			}
		}
	}

	/* Destroy the kernel context. */
	eError = BM_DestroyContext(psDeviceNode->sDevMemoryInfo.pBMKernelContext, IMG_NULL);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"DevDeInitSGX : Failed to destroy kernel context"));
		return eError;
	}

	/* remove the device from the power manager */
	eError = PVRSRVRemovePowerDevice (((PVRSRV_DEVICE_NODE*)pvDeviceNode)->sDevId.ui32DeviceIndex);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	eError = SysGetDeviceMemoryMap(PVRSRV_DEVICE_TYPE_SGX,
									(IMG_VOID**)&psSGXDeviceMap);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"DevDeInitSGX: Failed to get device memory map!"));
		return eError;
	}

	/* Only unmap the registers if they were mapped here */
	if (!psSGXDeviceMap->pvRegsCpuVBase)
	{
		/* UnMap Regs */
		if (psDevInfo->pvRegsBaseKM != IMG_NULL)
		{
			OSUnMapPhysToLin(psDevInfo->pvRegsBaseKM,
							 psDevInfo->ui32RegSize,
							 PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
							 IMG_NULL);
		}
	}

#if defined(SGX_FEATURE_HOST_PORT)
	if (psSGXDeviceMap->ui32Flags & SGX_HOSTPORT_PRESENT)
	{
		/* unMap Host Port */
		if (psDevInfo->pvHostPortBaseKM != IMG_NULL)
		{
			OSUnMapPhysToLin(psDevInfo->pvHostPortBaseKM,
						   psDevInfo->ui32HPSize,
						   PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
						   IMG_NULL);
		}
	}
#endif /* #ifdef SGX_FEATURE_HOST_PORT */


	/* DeAllocate devinfo */
	OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
				sizeof(PVRSRV_SGXDEV_INFO),
				psDevInfo,
				0);

	psDeviceNode->pvDevice = IMG_NULL;

	if (psDeviceMemoryHeap != IMG_NULL)
	{
	/* Free the device memory heap info. */
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
				sizeof(DEVICE_MEMORY_HEAP_INFO) * SGX_MAX_HEAP_ID,
				psDeviceMemoryHeap,
				0);
	}

	return PVRSRV_OK;
}


/*!
*******************************************************************************

 @Function	SGXDumpDebugReg

 @Description

 Dump a single SGX debug register value

 @Input psDevInfo - SGX device info
 @Input ui32CoreNum - processor number
 @Input pszName - string used for logging
 @Input ui32RegAddr - SGX register offset

 @Return   IMG_VOID

******************************************************************************/
static IMG_VOID SGXDumpDebugReg (PVRSRV_SGXDEV_INFO	*psDevInfo,
								 IMG_UINT32			ui32CoreNum,
								 IMG_CHAR			*pszName,
								 IMG_UINT32			ui32RegAddr)
{
	IMG_UINT32	ui32RegVal;
	ui32RegVal = OSReadHWReg(psDevInfo->pvRegsBaseKM, SGX_MP_CORE_SELECT(ui32RegAddr, ui32CoreNum));
	PVR_LOG(("(P%u) %s%08X", ui32CoreNum, pszName, ui32RegVal));
}

#if defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS) || defined(FIX_HW_BRN_31620)
static INLINE IMG_UINT32 GetDirListBaseReg(IMG_UINT32 ui32Index)
{
	if (ui32Index == 0)
	{
		return EUR_CR_BIF_DIR_LIST_BASE0;
	}
	else
	{
		return (EUR_CR_BIF_DIR_LIST_BASE1 + ((ui32Index - 1) * 0x4));
	}
}
#endif

/*!
*******************************************************************************

 @Function	SGXDumpDebugInfo

 @Description

 Dump useful debugging info

 @Input psDevInfo	 - SGX device info
 @Input bDumpSGXRegs - Whether to dump SGX debug registers. Must not be done
 						when SGX is not powered.

 @Return   IMG_VOID

******************************************************************************/
IMG_VOID SGXDumpDebugInfo (PVRSRV_SGXDEV_INFO	*psDevInfo,
						   IMG_BOOL				bDumpSGXRegs)
{
	IMG_UINT32	ui32CoreNum;

	PVR_LOG(("SGX debug (%s)", PVRVERSION_STRING));

	if (bDumpSGXRegs)
	{
		PVR_DPF((PVR_DBG_ERROR,"SGX Register Base Address (Linear):   0x%p", psDevInfo->pvRegsBaseKM));
		PVR_DPF((PVR_DBG_ERROR,"SGX Register Base Address (Physical): 0x" SYSPADDR_FMT, psDevInfo->sRegsPhysBase.uiAddr));

		SGXDumpDebugReg(psDevInfo, 0, "EUR_CR_CORE_ID:          ", EUR_CR_CORE_ID);
		SGXDumpDebugReg(psDevInfo, 0, "EUR_CR_CORE_REVISION:    ", EUR_CR_CORE_REVISION);
		for (ui32CoreNum = 0; ui32CoreNum < SGX_FEATURE_MP_CORE_COUNT_3D; ui32CoreNum++)
		{
			/* Dump HW event status */
			SGXDumpDebugReg(psDevInfo, ui32CoreNum, "EUR_CR_EVENT_STATUS:     ", EUR_CR_EVENT_STATUS);
			SGXDumpDebugReg(psDevInfo, ui32CoreNum, "EUR_CR_EVENT_STATUS2:    ", EUR_CR_EVENT_STATUS2);
			SGXDumpDebugReg(psDevInfo, ui32CoreNum, "EUR_CR_BIF_CTRL:         ", EUR_CR_BIF_CTRL);
		#if defined(EUR_CR_BIF_BANK0)
			SGXDumpDebugReg(psDevInfo, ui32CoreNum, "EUR_CR_BIF_BANK0:        ", EUR_CR_BIF_BANK0);	
		#endif
			SGXDumpDebugReg(psDevInfo, ui32CoreNum, "EUR_CR_BIF_INT_STAT:     ", EUR_CR_BIF_INT_STAT);
			SGXDumpDebugReg(psDevInfo, ui32CoreNum, "EUR_CR_BIF_FAULT:        ", EUR_CR_BIF_FAULT);
			SGXDumpDebugReg(psDevInfo, ui32CoreNum, "EUR_CR_BIF_MEM_REQ_STAT: ", EUR_CR_BIF_MEM_REQ_STAT);
			SGXDumpDebugReg(psDevInfo, ui32CoreNum, "EUR_CR_CLKGATECTL:       ", EUR_CR_CLKGATECTL);
		#if defined(EUR_CR_PDS_PC_BASE)
			SGXDumpDebugReg(psDevInfo, ui32CoreNum, "EUR_CR_PDS_PC_BASE:      ", EUR_CR_PDS_PC_BASE);
		#endif
			SGXDumpDebugReg(psDevInfo, ui32CoreNum, "EUR_CR_CLKGATESTATUS:    ", EUR_CR_CLKGATESTATUS);
		}

	#if !defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS) && !defined(FIX_HW_BRN_31620)
		{
			IMG_UINT32 ui32RegVal;
			IMG_UINT32 ui32PDDevPAddr;
	
			/*
				If there was a SGX pagefault check the page table too see if the
				host thinks the fault is correct
			*/
			ui32RegVal = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_INT_STAT);
			if (ui32RegVal & EUR_CR_BIF_INT_STAT_PF_N_RW_MASK)
			{
				ui32RegVal = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_FAULT);
				ui32RegVal &= EUR_CR_BIF_FAULT_ADDR_MASK;
				ui32PDDevPAddr = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_DIR_LIST_BASE0);
				ui32PDDevPAddr &= EUR_CR_BIF_DIR_LIST_BASE0_ADDR_MASK;
				MMU_CheckFaultAddr(psDevInfo, ui32PDDevPAddr, ui32RegVal);
			}
		}
	#else
		{
			IMG_UINT32 ui32FaultAddress;
			IMG_UINT32 ui32Bank0;
			IMG_UINT32 ui32DirListIndex;
			IMG_UINT32 ui32PDDevPAddr;
	
			ui32FaultAddress = OSReadHWReg(psDevInfo->pvRegsBaseKM,
											EUR_CR_BIF_FAULT);
			ui32FaultAddress = ui32FaultAddress & EUR_CR_BIF_FAULT_ADDR_MASK;
	
			ui32Bank0 = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_BANK0);
	
			/* Check the EDM's's memory context */
			ui32DirListIndex = (ui32Bank0 & EUR_CR_BIF_BANK0_INDEX_EDM_MASK) >> EUR_CR_BIF_BANK0_INDEX_EDM_SHIFT;
			ui32PDDevPAddr = OSReadHWReg(psDevInfo->pvRegsBaseKM,
											GetDirListBaseReg(ui32DirListIndex));
			PVR_LOG(("Checking EDM memory context (index = %d, PD = 0x%08x)", ui32DirListIndex, ui32PDDevPAddr));
			MMU_CheckFaultAddr(psDevInfo, ui32PDDevPAddr, ui32FaultAddress);
	
			/* Check the TA's memory context */
			ui32DirListIndex = (ui32Bank0 & EUR_CR_BIF_BANK0_INDEX_TA_MASK) >> EUR_CR_BIF_BANK0_INDEX_TA_SHIFT;
			ui32PDDevPAddr = OSReadHWReg(psDevInfo->pvRegsBaseKM,
											GetDirListBaseReg(ui32DirListIndex));
			PVR_LOG(("Checking TA memory context (index = %d, PD = 0x%08x)", ui32DirListIndex, ui32PDDevPAddr));
			MMU_CheckFaultAddr(psDevInfo, ui32PDDevPAddr, ui32FaultAddress);
	
			/* Check the 3D's memory context */
			ui32DirListIndex = (ui32Bank0 & EUR_CR_BIF_BANK0_INDEX_3D_MASK) >> EUR_CR_BIF_BANK0_INDEX_3D_SHIFT;
			ui32PDDevPAddr = OSReadHWReg(psDevInfo->pvRegsBaseKM,
											GetDirListBaseReg(ui32DirListIndex));
			PVR_LOG(("Checking 3D memory context (index = %d, PD = 0x%08x)", ui32DirListIndex, ui32PDDevPAddr));
			MMU_CheckFaultAddr(psDevInfo, ui32PDDevPAddr, ui32FaultAddress);
	
	#if defined(EUR_CR_BIF_BANK0_INDEX_2D_MASK)
			/* Check the 2D's memory context */
			ui32DirListIndex = (ui32Bank0 & EUR_CR_BIF_BANK0_INDEX_2D_MASK) >> EUR_CR_BIF_BANK0_INDEX_2D_SHIFT;
			ui32PDDevPAddr = OSReadHWReg(psDevInfo->pvRegsBaseKM,
											GetDirListBaseReg(ui32DirListIndex));
			PVR_LOG(("Checking 2D memory context (index = %d, PD = 0x%08x)", ui32DirListIndex, ui32PDDevPAddr));
			MMU_CheckFaultAddr(psDevInfo, ui32PDDevPAddr, ui32FaultAddress);
	#endif
	
	#if defined(EUR_CR_BIF_BANK0_INDEX_PTLA_MASK)
			/* Check the 2D's memory context */
			ui32DirListIndex = (ui32Bank0 & EUR_CR_BIF_BANK0_INDEX_PTLA_MASK) >> EUR_CR_BIF_BANK0_INDEX_PTLA_SHIFT;
			ui32PDDevPAddr = OSReadHWReg(psDevInfo->pvRegsBaseKM,
											GetDirListBaseReg(ui32DirListIndex));
			PVR_LOG(("Checking PTLA memory context (index = %d, PD = 0x%08x)", ui32DirListIndex, ui32PDDevPAddr));
			MMU_CheckFaultAddr(psDevInfo, ui32PDDevPAddr, ui32FaultAddress);
	#endif
	
	#if defined(EUR_CR_BIF_BANK0_INDEX_HOST_MASK)
			/* Check the Host's memory context */
			ui32DirListIndex = (ui32Bank0 & EUR_CR_BIF_BANK0_INDEX_HOST_MASK) >> EUR_CR_BIF_BANK0_INDEX_HOST_SHIFT;
			ui32PDDevPAddr = OSReadHWReg(psDevInfo->pvRegsBaseKM,
											GetDirListBaseReg(ui32DirListIndex));
			PVR_LOG(("Checking Host memory context (index = %d, PD = 0x%08x)", ui32DirListIndex, ui32PDDevPAddr));
			MMU_CheckFaultAddr(psDevInfo, ui32PDDevPAddr, ui32FaultAddress);
	#endif
		}
	#endif
	}
	/*
		Dump out the outstanding queue items.
	*/
	QueueDumpDebugInfo();

	{
		/*
			Dump out the Host control.
		*/
		SGXMKIF_HOST_CTL	*psSGXHostCtl = psDevInfo->psSGXHostCtl;
		IMG_UINT32			*pui32HostCtlBuffer = (IMG_UINT32 *)psSGXHostCtl;
		IMG_UINT32			ui32LoopCounter;
		
		/* Report which defines are enabled that affect the HostCTL structure being dumped-out here */
		{
			IMG_UINT32 ui32CtlFlags = 0;
			#if defined(PVRSRV_USSE_EDM_BREAKPOINTS)
			ui32CtlFlags = ui32CtlFlags | 0x0001;
			#endif
			#if defined(FIX_HW_BRN_28889)
			ui32CtlFlags = ui32CtlFlags | 0x0002;
			#endif
			#if defined(SUPPORT_HW_RECOVERY)
			ui32CtlFlags = ui32CtlFlags | 0x0004;
			#endif
			#if defined(SGX_FEATURE_EXTENDED_PERF_COUNTERS)
			ui32CtlFlags = ui32CtlFlags | 0x0008;
			#endif
			PVR_LOG((" Host Ctl flags= %08x", ui32CtlFlags));
		}

		if (psSGXHostCtl->ui32AssertFail != 0)
		{
			PVR_LOG(("SGX Microkernel assert fail: 0x%08X", psSGXHostCtl->ui32AssertFail));
			psSGXHostCtl->ui32AssertFail = 0;
		}

		PVR_LOG(("SGX Host control:"));

		for (ui32LoopCounter = 0;
			 ui32LoopCounter < sizeof(*psDevInfo->psSGXHostCtl) / sizeof(*pui32HostCtlBuffer);
			 ui32LoopCounter += 4)
		{
			PVR_LOG(("\t(HC-%" SIZE_T_FMT_LEN "X) 0x%08X 0x%08X 0x%08X 0x%08X", 
                    ui32LoopCounter * sizeof(*pui32HostCtlBuffer),
					pui32HostCtlBuffer[ui32LoopCounter + 0], pui32HostCtlBuffer[ui32LoopCounter + 1],
					pui32HostCtlBuffer[ui32LoopCounter + 2], pui32HostCtlBuffer[ui32LoopCounter + 3]));
		}
	}

	{
		/*
			Dump out the TA/3D control.
		*/
		IMG_UINT32	*pui32TA3DCtlBuffer = psDevInfo->psKernelSGXTA3DCtlMemInfo->pvLinAddrKM;
		IMG_UINT32	ui32LoopCounter;

		PVR_LOG(("SGX TA/3D control:"));

		for (ui32LoopCounter = 0;
			 ui32LoopCounter < psDevInfo->psKernelSGXTA3DCtlMemInfo->uAllocSize / sizeof(*pui32TA3DCtlBuffer);
			 ui32LoopCounter += 4)
		{
			PVR_LOG(("\t(T3C-%" SIZE_T_FMT_LEN "X) 0x%08X 0x%08X 0x%08X 0x%08X", 
                    ui32LoopCounter * sizeof(*pui32TA3DCtlBuffer),
					pui32TA3DCtlBuffer[ui32LoopCounter + 0], pui32TA3DCtlBuffer[ui32LoopCounter + 1],
					pui32TA3DCtlBuffer[ui32LoopCounter + 2], pui32TA3DCtlBuffer[ui32LoopCounter + 3]));
		}
	}

	#if defined(PVRSRV_USSE_EDM_STATUS_DEBUG)
	{
		IMG_UINT32	*pui32MKTraceBuffer = psDevInfo->psKernelEDMStatusBufferMemInfo->pvLinAddrKM;
		IMG_UINT32	ui32LastStatusCode, ui32WriteOffset;

		ui32LastStatusCode = *pui32MKTraceBuffer;
		pui32MKTraceBuffer++;
		ui32WriteOffset = *pui32MKTraceBuffer;
		pui32MKTraceBuffer++;

		PVR_LOG(("Last SGX microkernel status code: %08X %s",
				 ui32LastStatusCode, SGXUKernelStatusString(ui32LastStatusCode)));

		#if defined(PVRSRV_DUMP_MK_TRACE)
		/*
			Dump the raw microkernel trace buffer to the log.
		*/
		{
			IMG_UINT32	ui32LoopCounter;

			for (ui32LoopCounter = 0;
				 ui32LoopCounter < SGXMK_TRACE_BUFFER_SIZE;
				 ui32LoopCounter++)
			{
				IMG_UINT32	*pui32BufPtr;
				pui32BufPtr = pui32MKTraceBuffer +
								(((ui32WriteOffset + ui32LoopCounter) % SGXMK_TRACE_BUFFER_SIZE) * 4);
				PVR_LOG(("\t(MKT-%X) %08X %08X %08X %08X %s", ui32LoopCounter,
						 pui32BufPtr[2], pui32BufPtr[3], pui32BufPtr[1], pui32BufPtr[0],
						 SGXUKernelStatusString(pui32BufPtr[0])));
			}
		}
		#endif /* PVRSRV_DUMP_MK_TRACE */
	}
	#endif /* PVRSRV_USSE_EDM_STATUS_DEBUG */

	{
		/*
			Dump out the kernel CCB.
		*/
		PVR_LOG(("SGX Kernel CCB WO:0x%X RO:0x%X Total:%d",
				psDevInfo->psKernelCCBCtl->ui32WriteOffset,
				psDevInfo->psKernelCCBCtl->ui32ReadOffset,
				g_debug_CCB_Info_WCNT));

		#if defined(PVRSRV_DUMP_KERNEL_CCB)
		{
			IMG_UINT32	ui32LoopCounter;

			for (ui32LoopCounter = 0;
				 ui32LoopCounter < sizeof(psDevInfo->psKernelCCB->asCommands) /
				 					sizeof(psDevInfo->psKernelCCB->asCommands[0]);
				 ui32LoopCounter++)
			{
				SGXMKIF_COMMAND	*psCommand = &psDevInfo->psKernelCCB->asCommands[ui32LoopCounter];

				PVR_LOG(("\t(KCCB-%X) %08X %08X - %08X %08X %08X %08X", ui32LoopCounter,
						psCommand->ui32ServiceAddress, psCommand->ui32CacheControl,
						psCommand->ui32Data[0], psCommand->ui32Data[1],
						psCommand->ui32Data[2], psCommand->ui32Data[3]));
			}
		}
		#endif /* PVRSRV_DUMP_KERNEL_CCB */
	}
	#if defined (TTRACE)
	PVRSRVDumpTimeTraceBuffers();
	#endif

}


#if defined(SYS_USING_INTERRUPTS) || defined(SUPPORT_HW_RECOVERY)
/*!
*******************************************************************************

 @Function	HWRecoveryResetSGX

 @Description

 Resets SGX

 Note: may be called from an ISR so should not call pdump.

 @Input psDevInfo - dev info

 @Input ui32Component - core component to reset

 @Return   IMG_VOID

******************************************************************************/
static
IMG_VOID HWRecoveryResetSGX (PVRSRV_DEVICE_NODE *psDeviceNode,
							 IMG_UINT32 		ui32Component,
							 IMG_UINT32			ui32CallerID)
{
	PVRSRV_ERROR		eError;
	PVRSRV_SGXDEV_INFO	*psDevInfo = (PVRSRV_SGXDEV_INFO*)psDeviceNode->pvDevice;
	SGXMKIF_HOST_CTL	*psSGXHostCtl = (SGXMKIF_HOST_CTL *)psDevInfo->psSGXHostCtl;
	
#if defined(SUPPORT_HWRECOVERY_TRACE_LIMIT)	
	static IMG_UINT32	ui32Clockinus = 0;
	static IMG_UINT32	ui32HWRecoveryCount=0;
	IMG_UINT32			ui32TempClockinus=0;
#endif	
	
	PVR_UNREFERENCED_PARAMETER(ui32Component);

	/*
		Ensure that hardware recovery is serialised with any power transitions.
	*/
	eError = PVRSRVPowerLock(ui32CallerID, IMG_FALSE);
	if(eError != PVRSRV_OK)
	{
		/*
			Unable to obtain lock because there is already a power transition
			in progress.
		*/
		PVR_DPF((PVR_DBG_WARNING,"HWRecoveryResetSGX: Power transition in progress"));
		return;
	}

	psSGXHostCtl->ui32InterruptClearFlags |= PVRSRV_USSE_EDM_INTERRUPT_HWR;

	PVR_LOG(("HWRecoveryResetSGX: SGX Hardware Recovery triggered"));
	
#if defined(SUPPORT_HWRECOVERY_TRACE_LIMIT)	
/*
 * 		The following defines are system specific and should be defined in
 * 		the corresponding sysconfig.h file. The values indicated are examples only.
		SYS_SGX_HWRECOVERY_TRACE_RESET_TIME_PERIOD		5000000 //(5 Seconds)
		SYS_SGX_MAX_HWRECOVERY_OCCURANCE_COUNT			5
		*/
		ui32TempClockinus = OSClockus();
	if((ui32TempClockinus-ui32Clockinus) < SYS_SGX_HWRECOVERY_TRACE_RESET_TIME_PERIOD){
		ui32HWRecoveryCount++;
		if(SYS_SGX_MAX_HWRECOVERY_OCCURANCE_COUNT <= ui32HWRecoveryCount){
			OSPanic();	
		}
	}else{
		ui32Clockinus = ui32TempClockinus;
		SGXDumpDebugInfo(psDeviceNode->pvDevice, IMG_TRUE);
		ui32HWRecoveryCount = 0;
	}
#else	
	SGXDumpDebugInfo(psDeviceNode->pvDevice, IMG_TRUE);
#endif
	
	/* Suspend pdumping. */
	PDUMPSUSPEND();

	/* Reset and re-initialise SGX. */
	eError = SGXInitialise(psDevInfo, IMG_TRUE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"HWRecoveryResetSGX: SGXInitialise failed (%d)", eError));
	}

	/* Resume pdumping. */
	PDUMPRESUME();

	PVRSRVPowerUnlock(ui32CallerID);

	/* Send a dummy kick so that we start processing again */
	SGXScheduleProcessQueuesKM(psDeviceNode);

	/* Flush any old commands from the queues. */
	PVRSRVProcessQueues(IMG_TRUE);
}
#endif /* #if defined(SYS_USING_INTERRUPTS) || defined(SUPPORT_HW_RECOVERY) */


#if defined(SUPPORT_HW_RECOVERY)
/*!
******************************************************************************

 @Function	SGXOSTimer

 @Description

 Timer function for SGX

 @Input pvData - private data

 @Return   PVRSRV_ERROR

******************************************************************************/
IMG_VOID SGXOSTimer(IMG_VOID *pvData)
{
	PVRSRV_DEVICE_NODE *psDeviceNode = pvData;
	PVRSRV_SGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	static IMG_UINT32	ui32EDMTasks = 0;
	static IMG_UINT32	ui32LockupCounter = 0; /* To prevent false positives */
	static IMG_UINT32	ui32OpenCLDelayCounter = 0;
	static IMG_UINT32	ui32NumResets = 0;
#if defined(FIX_HW_BRN_31093)
	static IMG_BOOL		bBRN31093Inval = IMG_FALSE;
#endif
	IMG_UINT32		ui32CurrentEDMTasks;
	IMG_UINT32		ui32CurrentOpenCLDelayCounter=0;
	IMG_BOOL		bLockup = IMG_FALSE;
	IMG_BOOL		bPoweredDown;

	/* increment a timestamp */
	psDevInfo->ui32TimeStamp++;

#if defined(NO_HARDWARE)
	bPoweredDown = IMG_TRUE;
#else
	bPoweredDown = (SGXIsDevicePowered(psDeviceNode)) ? IMG_FALSE : IMG_TRUE;
#endif /* NO_HARDWARE */

	/*
	 * Check whether EDM timer tasks are getting scheduled. If not, assume
	 * that SGX has locked up and reset the chip.
	 */

	/* Check whether the timer should be running */
	if (bPoweredDown)
	{
		ui32LockupCounter = 0;
	#if defined(FIX_HW_BRN_31093)
		bBRN31093Inval = IMG_FALSE;
	#endif
	}
	else
	{
		/* The PDS timer should be running. */
		ui32CurrentEDMTasks = OSReadHWReg(psDevInfo->pvRegsBaseKM, psDevInfo->ui32EDMTaskReg0);
		if (psDevInfo->ui32EDMTaskReg1 != 0)
		{
			ui32CurrentEDMTasks ^= OSReadHWReg(psDevInfo->pvRegsBaseKM, psDevInfo->ui32EDMTaskReg1);
		}
		if ((ui32CurrentEDMTasks == ui32EDMTasks) &&
			(psDevInfo->ui32NumResets == ui32NumResets))
		{
			ui32LockupCounter++;
#if defined(DUMP_UKERNEL_INFO_AT_TIMEOUT)
			{
				SGXMKIF_HOST_CTL	*psSGXHostCtl = psDevInfo->psSGXHostCtl;

				PVR_LOG(("Check counter %d", ui32LockupCounter));

				PVR_LOG(("EDMTaskReg0 = 0x%08x", OSReadHWReg(psDevInfo->pvRegsBaseKM, psDevInfo->ui32EDMTaskReg0)));
				PVR_LOG(("EDMTaskReg1 = 0x%08x", OSReadHWReg(psDevInfo->pvRegsBaseKM, psDevInfo->ui32EDMTaskReg1)));

				if (psSGXHostCtl->ui32AssertFail != 0)
				{
					PVR_LOG(("SGX Microkernel assert fail: 0x%08X", psSGXHostCtl->ui32AssertFail));
					psSGXHostCtl->ui32AssertFail = 0;
				}
				/*
					Dump out the kernel CCB.
				*/
				PVR_LOG(("SGX Kernel CCB WO:0x%X RO:0x%X Total:%d",
					psDevInfo->psKernelCCBCtl->ui32WriteOffset,
					psDevInfo->psKernelCCBCtl->ui32ReadOffset,
					g_debug_CCB_Info_WCNT));

				SGXDumpDebugReg(psDevInfo, 0, "EUR_CR_BIF_MEM_REQ_STAT: ", EUR_CR_BIF_MEM_REQ_STAT);
				SGXDumpDebugReg(psDevInfo, 1, "EUR_CR_BIF_MEM_REQ_STAT: ", EUR_CR_BIF_MEM_REQ_STAT);
				SGXDumpDebugReg(psDevInfo, 2, "EUR_CR_BIF_MEM_REQ_STAT: ", EUR_CR_BIF_MEM_REQ_STAT);
			}
#endif
			if (ui32LockupCounter == 3)
			{
				ui32LockupCounter = 0;
				ui32CurrentOpenCLDelayCounter = (psDevInfo->psSGXHostCtl)->ui32OpenCLDelayCount;
				if(0 != ui32CurrentOpenCLDelayCounter)
				{
					if(ui32OpenCLDelayCounter != ui32CurrentOpenCLDelayCounter){
						ui32OpenCLDelayCounter = ui32CurrentOpenCLDelayCounter;
					}else{
						ui32OpenCLDelayCounter -= 1;
						(psDevInfo->psSGXHostCtl)->ui32OpenCLDelayCount = ui32OpenCLDelayCounter;
					}
					goto SGX_NoUKernel_LockUp;
				}


	#if defined(FIX_HW_BRN_31093)
				if (bBRN31093Inval == IMG_FALSE)
				{
					/* It could be a BIF hang so do a INVAL_PTE */
		#if defined(FIX_HW_BRN_29997)
					IMG_UINT32	ui32BIFCtrl;
				/* Pause the BIF before issuing the invalidate */
					ui32BIFCtrl = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_CTRL);
					OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_CTRL, ui32BIFCtrl | EUR_CR_BIF_CTRL_PAUSE_MASK);
					/* delay for 200 clocks */
					SGXWaitClocks(psDevInfo, 200);
		#endif
					/* Flag that we have attempt to un-block the BIF */
					bBRN31093Inval = IMG_TRUE;
					
					OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_CTRL_INVAL, EUR_CR_BIF_CTRL_INVAL_PTE_MASK);
					/* delay for 200 clocks */
					SGXWaitClocks(psDevInfo, 200);
						
		#if defined(FIX_HW_BRN_29997)	
					/* un-pause the BIF by restoring the BIF_CTRL */	
					OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_CTRL, ui32BIFCtrl);
		#endif
				}
				else
	#endif
				{
					PVR_DPF((PVR_DBG_ERROR, "SGXOSTimer() detected SGX lockup (0x%x tasks)", ui32EDMTasks));

					bLockup = IMG_TRUE;
					(psDevInfo->psSGXHostCtl)->ui32OpenCLDelayCount = 0;
				}
			}
		}
		else
		{
	#if defined(FIX_HW_BRN_31093)
			bBRN31093Inval = IMG_FALSE;
	#endif
			ui32LockupCounter = 0;
			ui32EDMTasks = ui32CurrentEDMTasks;
			ui32NumResets = psDevInfo->ui32NumResets;
		}
	}
SGX_NoUKernel_LockUp:

	if (bLockup)
	{
		SGXMKIF_HOST_CTL	*psSGXHostCtl = (SGXMKIF_HOST_CTL *)psDevInfo->psSGXHostCtl;

		/* increment the counter so we know the host detected the lockup */
		psSGXHostCtl->ui32HostDetectedLockups ++;

		/* Reset the chip and process the queues. */
		HWRecoveryResetSGX(psDeviceNode, 0, ISR_ID);
	}
}
#endif /* defined(SUPPORT_HW_RECOVERY) */



#if defined(SYS_USING_INTERRUPTS)

/*
	SGX ISR Handler
*/
IMG_BOOL SGX_ISRHandler (IMG_VOID *pvData)
{
	IMG_BOOL bInterruptProcessed = IMG_FALSE;


	/* Real Hardware */
	{
		IMG_UINT32 ui32EventStatus, ui32EventEnable;
		IMG_UINT32 ui32EventClear = 0;
#if defined(SGX_FEATURE_DATA_BREAKPOINTS)
		IMG_UINT32 ui32EventStatus2, ui32EventEnable2;
#endif		
		IMG_UINT32 ui32EventClear2 = 0;
		PVRSRV_DEVICE_NODE *psDeviceNode;
		PVRSRV_SGXDEV_INFO *psDevInfo;

		/* check for null pointers */
		if(pvData == IMG_NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "SGX_ISRHandler: Invalid params\n"));
			return bInterruptProcessed;
		}

		psDeviceNode = (PVRSRV_DEVICE_NODE *)pvData;
		psDevInfo = (PVRSRV_SGXDEV_INFO *)psDeviceNode->pvDevice;

		ui32EventStatus = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_STATUS);
		ui32EventEnable = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_HOST_ENABLE);

		/* test only the unmasked bits */
		ui32EventStatus &= ui32EventEnable;

#if defined(SGX_FEATURE_DATA_BREAKPOINTS)
		ui32EventStatus2 = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_STATUS2);
		ui32EventEnable2 = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_HOST_ENABLE2);

		/* test only the unmasked bits */
		ui32EventStatus2 &= ui32EventEnable2;
#endif /* defined(SGX_FEATURE_DATA_BREAKPOINTS) */

		/* Thought: is it better to insist that the bit assignment in
		   the "clear" register(s) matches that of the "status" register(s)?
		   It would greatly simplify this LISR */

		if (ui32EventStatus & EUR_CR_EVENT_STATUS_SW_EVENT_MASK)
		{
			ui32EventClear |= EUR_CR_EVENT_HOST_CLEAR_SW_EVENT_MASK;
		}

#if defined(SGX_FEATURE_DATA_BREAKPOINTS)
		if (ui32EventStatus2 & EUR_CR_EVENT_STATUS2_DATA_BREAKPOINT_UNTRAPPED_MASK)
		{
			ui32EventClear2 |= EUR_CR_EVENT_HOST_CLEAR2_DATA_BREAKPOINT_UNTRAPPED_MASK;
		}

		if (ui32EventStatus2 & EUR_CR_EVENT_STATUS2_DATA_BREAKPOINT_TRAPPED_MASK)
		{
			ui32EventClear2 |= EUR_CR_EVENT_HOST_CLEAR2_DATA_BREAKPOINT_TRAPPED_MASK;
		}
#endif /* defined(SGX_FEATURE_DATA_BREAKPOINTS) */

		if (ui32EventClear || ui32EventClear2)
		{
			bInterruptProcessed = IMG_TRUE;

			/* Clear master interrupt bit */
			ui32EventClear |= EUR_CR_EVENT_HOST_CLEAR_MASTER_INTERRUPT_MASK;

			/* clear the events */
			OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_HOST_CLEAR, ui32EventClear);
			OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_HOST_CLEAR2, ui32EventClear2);

			/*
				Sample the current count from the uKernel _before_ we clear the
				interrupt.
			*/
			g_ui32HostIRQCountSample = psDevInfo->psSGXHostCtl->ui32InterruptCount;
		}
	}

	return bInterruptProcessed;
}


/*
	SGX MISR Handler
*/
static IMG_VOID SGX_MISRHandler (IMG_VOID *pvData)
{
	PVRSRV_DEVICE_NODE	*psDeviceNode = (PVRSRV_DEVICE_NODE *)pvData;
	PVRSRV_SGXDEV_INFO	*psDevInfo = (PVRSRV_SGXDEV_INFO*)psDeviceNode->pvDevice;
	SGXMKIF_HOST_CTL	*psSGXHostCtl = (SGXMKIF_HOST_CTL *)psDevInfo->psSGXHostCtl;

	if (((psSGXHostCtl->ui32InterruptFlags & PVRSRV_USSE_EDM_INTERRUPT_HWR) != 0UL) &&
		((psSGXHostCtl->ui32InterruptClearFlags & PVRSRV_USSE_EDM_INTERRUPT_HWR) == 0UL))
	{
		HWRecoveryResetSGX(psDeviceNode, 0, ISR_ID);
	}

#if defined(OS_SUPPORTS_IN_LISR)
	if (psDeviceNode->bReProcessDeviceCommandComplete)
	{
		SGXScheduleProcessQueuesKM(psDeviceNode);
	}
#endif

	SGXTestActivePowerEvent(psDeviceNode, ISR_ID);
}
#endif /* #if defined (SYS_USING_INTERRUPTS) */

#if defined(SUPPORT_MEMORY_TILING)

IMG_INTERNAL
PVRSRV_ERROR SGX_AllocMemTilingRange(PVRSRV_DEVICE_NODE *psDeviceNode,
									 PVRSRV_KERNEL_MEM_INFO	*psMemInfo,
									 IMG_UINT32 ui32XTileStride,
									 IMG_UINT32 *pui32RangeIndex)
{
	return SGX_AllocMemTilingRangeInt(psDeviceNode->pvDevice,
		psMemInfo->sDevVAddr.uiAddr,
		psMemInfo->sDevVAddr.uiAddr + ((IMG_UINT32) psMemInfo->uAllocSize) + SGX_MMU_PAGE_SIZE - 1,
		ui32XTileStride,
		pui32RangeIndex);
}

IMG_INTERNAL
PVRSRV_ERROR SGX_FreeMemTilingRange(PVRSRV_DEVICE_NODE *psDeviceNode,
										IMG_UINT32 ui32RangeIndex)
{
	PVRSRV_SGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	IMG_UINT32 ui32Offset;
	IMG_UINT32 ui32Val;

	if(ui32RangeIndex >= 10)
	{
		PVR_DPF((PVR_DBG_ERROR,"SGX_FreeMemTilingRange: invalid Range index "));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* clear the usage bit */
	psDevInfo->ui32MemTilingUsage &= ~(1<<ui32RangeIndex);

	/* disable the range */
	ui32Offset = EUR_CR_BIF_TILE0 + (ui32RangeIndex<<2);
	ui32Val = 0;

	OSWriteHWReg(psDevInfo->pvRegsBaseKM, ui32Offset, ui32Val);
	PDUMPREG(SGX_PDUMPREG_NAME, ui32Offset, ui32Val);

	return PVRSRV_OK;
}

#endif /* defined(SUPPORT_MEMORY_TILING) */


static IMG_VOID SGXCacheInvalidate(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_SGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

	#if defined(SGX_FEATURE_MP)
	psDevInfo->ui32CacheControl |= SGXMKIF_CC_INVAL_BIF_SL;
	#else
	PVR_UNREFERENCED_PARAMETER(psDevInfo);
	#endif /* SGX_FEATURE_MP */
}

/*!
*******************************************************************************

 @Function	SGXRegisterDevice

 @Description

 Registers the device with the system

 @Input: 	psDeviceNode - device node

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR SGXRegisterDevice (PVRSRV_DEVICE_NODE *psDeviceNode)
{
	DEVICE_MEMORY_INFO *psDevMemoryInfo;
	DEVICE_MEMORY_HEAP_INFO *psDeviceMemoryHeap;

	/* setup details that never change */
	psDeviceNode->sDevId.eDeviceType		= DEV_DEVICE_TYPE;
	psDeviceNode->sDevId.eDeviceClass		= DEV_DEVICE_CLASS;
#if defined(PDUMP)
	{
		/* memory space names are set up in system code */
		SGX_DEVICE_MAP *psSGXDeviceMemMap;
		SysGetDeviceMemoryMap(PVRSRV_DEVICE_TYPE_SGX,
							  (IMG_VOID**)&psSGXDeviceMemMap);

		psDeviceNode->sDevId.pszPDumpDevName = psSGXDeviceMemMap->pszPDumpDevName;
		PVR_ASSERT(psDeviceNode->sDevId.pszPDumpDevName != IMG_NULL);
	}
	
	psDeviceNode->sDevId.pszPDumpRegName	= SGX_PDUMPREG_NAME;
#endif /* PDUMP */

	psDeviceNode->pfnInitDevice		= &DevInitSGXPart1;
	psDeviceNode->pfnDeInitDevice	= &DevDeInitSGX;

	psDeviceNode->pfnInitDeviceCompatCheck	= &SGXDevInitCompatCheck;
#if defined(PDUMP)
	psDeviceNode->pfnPDumpInitDevice = &SGXResetPDump;
	psDeviceNode->pfnMMUGetContextID = &MMU_GetPDumpContextID;
#endif
	/*
		MMU callbacks
	*/
	psDeviceNode->pfnMMUInitialise = &MMU_Initialise;
	psDeviceNode->pfnMMUFinalise = &MMU_Finalise;
	psDeviceNode->pfnMMUInsertHeap = &MMU_InsertHeap;
	psDeviceNode->pfnMMUCreate = &MMU_Create;
	psDeviceNode->pfnMMUDelete = &MMU_Delete;
	psDeviceNode->pfnMMUAlloc = &MMU_Alloc;
	psDeviceNode->pfnMMUFree = &MMU_Free;
	psDeviceNode->pfnMMUMapPages = &MMU_MapPages;
	psDeviceNode->pfnMMUMapShadow = &MMU_MapShadow;
	psDeviceNode->pfnMMUUnmapPages = &MMU_UnmapPages;
	psDeviceNode->pfnMMUMapScatter = &MMU_MapScatter;
	psDeviceNode->pfnMMUGetPhysPageAddr = &MMU_GetPhysPageAddr;
	psDeviceNode->pfnMMUGetPDDevPAddr = &MMU_GetPDDevPAddr;
#if defined(SUPPORT_PDUMP_MULTI_PROCESS)
	psDeviceNode->pfnMMUIsHeapShared = &MMU_IsHeapShared;
#endif
#if defined(FIX_HW_BRN_31620)
	psDeviceNode->pfnMMUGetCacheFlushRange = &MMU_GetCacheFlushRange;
	psDeviceNode->pfnMMUGetPDPhysAddr = &MMU_GetPDPhysAddr;
#else
	psDeviceNode->pfnMMUGetCacheFlushRange = IMG_NULL;
	psDeviceNode->pfnMMUGetPDPhysAddr = IMG_NULL;
#endif
	psDeviceNode->pfnMMUMapPagesSparse = &MMU_MapPagesSparse;
	psDeviceNode->pfnMMUMapShadowSparse = &MMU_MapShadowSparse;

#if defined (SYS_USING_INTERRUPTS)
	/*
		SGX ISR handler
	*/
	psDeviceNode->pfnDeviceISR = SGX_ISRHandler;
	psDeviceNode->pfnDeviceMISR = SGX_MISRHandler;
#endif

#if defined(SUPPORT_MEMORY_TILING)
	psDeviceNode->pfnAllocMemTilingRange = SGX_AllocMemTilingRange;
	psDeviceNode->pfnFreeMemTilingRange = SGX_FreeMemTilingRange;
#endif

	/*
		SGX command complete handler
	*/
	psDeviceNode->pfnDeviceCommandComplete = &SGXCommandComplete;

	psDeviceNode->pfnCacheInvalidate = SGXCacheInvalidate;

	/*
		and setup the device's memory map:
	*/
	psDevMemoryInfo = &psDeviceNode->sDevMemoryInfo;
	/* size of address space */
	psDevMemoryInfo->ui32AddressSpaceSizeLog2 = SGX_FEATURE_ADDRESS_SPACE_SIZE;

	/* flags, backing store details to be specified by system */
	psDevMemoryInfo->ui32Flags = 0;

	/* device memory heap info about each heap in a device address space */
	if(OSAllocMem( PVRSRV_OS_PAGEABLE_HEAP,
					 sizeof(DEVICE_MEMORY_HEAP_INFO) * SGX_MAX_HEAP_ID,
					 (IMG_VOID **)&psDevMemoryInfo->psDeviceMemoryHeap, 0,
					 "Array of Device Memory Heap Info") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SGXRegisterDevice : Failed to alloc memory for DEVICE_MEMORY_HEAP_INFO"));
		return (PVRSRV_ERROR_OUT_OF_MEMORY);
	}
	OSMemSet(psDevMemoryInfo->psDeviceMemoryHeap, 0, sizeof(DEVICE_MEMORY_HEAP_INFO) * SGX_MAX_HEAP_ID);

	psDeviceMemoryHeap = psDevMemoryInfo->psDeviceMemoryHeap;

	/*
		setup heaps
		Note: backing store to be setup by system (defaults to UMA)
	*/

	/************* general ***************/
	psDeviceMemoryHeap->ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX, SGX_GENERAL_HEAP_ID);
	psDeviceMemoryHeap->sDevVAddrBase.uiAddr = SGX_GENERAL_HEAP_BASE;
	psDeviceMemoryHeap->ui32HeapSize = SGX_GENERAL_HEAP_SIZE;
	psDeviceMemoryHeap->ui32Attribs = PVRSRV_HAP_WRITECOMBINE
														| PVRSRV_MEM_RAM_BACKED_ALLOCATION
														| PVRSRV_HAP_SINGLE_PROCESS;
	psDeviceMemoryHeap->pszName = "General";
	psDeviceMemoryHeap->pszBSName = "General BS";
	psDeviceMemoryHeap->DevMemHeapType = DEVICE_MEMORY_HEAP_PERCONTEXT;
	/* set the default (4k). System can override these as required */
	psDeviceMemoryHeap->ui32DataPageSize = SGX_MMU_PAGE_SIZE;
#if !defined(SUPPORT_SGX_GENERAL_MAPPING_HEAP)
	/* specify the mapping heap ID for this device */
	psDevMemoryInfo->ui32MappingHeapID = (IMG_UINT32)(psDeviceMemoryHeap - psDevMemoryInfo->psDeviceMemoryHeap);
#endif
	psDeviceMemoryHeap++;/* advance to the next heap */

#if defined(SUPPORT_MEMORY_TILING)
	/************* VPB tiling ***************/
	psDeviceMemoryHeap->ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX, SGX_VPB_TILED_HEAP_ID);
	psDeviceMemoryHeap->sDevVAddrBase.uiAddr = SGX_VPB_TILED_HEAP_BASE;
	psDeviceMemoryHeap->ui32HeapSize = SGX_VPB_TILED_HEAP_SIZE;
	psDeviceMemoryHeap->ui32Attribs = PVRSRV_HAP_WRITECOMBINE
														| PVRSRV_MEM_RAM_BACKED_ALLOCATION
														| PVRSRV_HAP_SINGLE_PROCESS;
	psDeviceMemoryHeap->pszName = "VPB Tiled";
	psDeviceMemoryHeap->pszBSName = "VPB Tiled BS";
	psDeviceMemoryHeap->DevMemHeapType = DEVICE_MEMORY_HEAP_PERCONTEXT;
	/* set the default (4k). System can override these as required */
	psDeviceMemoryHeap->ui32DataPageSize = SGX_MMU_PAGE_SIZE;
	psDeviceMemoryHeap->ui32XTileStride = SGX_VPB_TILED_HEAP_STRIDE;
	PVR_DPF((PVR_DBG_WARNING, "VPB tiling heap tiling stride = 0x%x", psDeviceMemoryHeap->ui32XTileStride));
	psDeviceMemoryHeap++;/* advance to the next heap */
#endif

	/************* TA data ***************/
	psDeviceMemoryHeap->ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX, SGX_TADATA_HEAP_ID);
	psDeviceMemoryHeap->sDevVAddrBase.uiAddr = SGX_TADATA_HEAP_BASE;
	psDeviceMemoryHeap->ui32HeapSize = SGX_TADATA_HEAP_SIZE;
	psDeviceMemoryHeap->ui32Attribs = PVRSRV_HAP_WRITECOMBINE
														| PVRSRV_MEM_RAM_BACKED_ALLOCATION
														| PVRSRV_HAP_MULTI_PROCESS;
	psDeviceMemoryHeap->pszName = "TA Data";
	psDeviceMemoryHeap->pszBSName = "TA Data BS";
	psDeviceMemoryHeap->DevMemHeapType = DEVICE_MEMORY_HEAP_PERCONTEXT;
	/* set the default (4k). System can override these as required */
	psDeviceMemoryHeap->ui32DataPageSize = SGX_MMU_PAGE_SIZE;
	psDeviceMemoryHeap++;/* advance to the next heap */


	/************* kernel code ***************/
	psDeviceMemoryHeap->ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX, SGX_KERNEL_CODE_HEAP_ID);
	psDeviceMemoryHeap->sDevVAddrBase.uiAddr = SGX_KERNEL_CODE_HEAP_BASE;
	psDeviceMemoryHeap->ui32HeapSize = SGX_KERNEL_CODE_HEAP_SIZE;
	psDeviceMemoryHeap->ui32Attribs = PVRSRV_HAP_WRITECOMBINE
															| PVRSRV_MEM_RAM_BACKED_ALLOCATION
															| PVRSRV_HAP_MULTI_PROCESS;
	psDeviceMemoryHeap->pszName = "Kernel Code";
	psDeviceMemoryHeap->pszBSName = "Kernel Code BS";
	psDeviceMemoryHeap->DevMemHeapType = DEVICE_MEMORY_HEAP_SHARED_EXPORTED;
	/* set the default (4k). System can override these as required */
	psDeviceMemoryHeap->ui32DataPageSize = SGX_MMU_PAGE_SIZE;
	psDeviceMemoryHeap++;/* advance to the next heap */


	/************* Kernel Video Data ***************/
	psDeviceMemoryHeap->ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX, SGX_KERNEL_DATA_HEAP_ID);
	psDeviceMemoryHeap->sDevVAddrBase.uiAddr = SGX_KERNEL_DATA_HEAP_BASE;
	psDeviceMemoryHeap->ui32HeapSize = SGX_KERNEL_DATA_HEAP_SIZE;
	psDeviceMemoryHeap->ui32Attribs = PVRSRV_HAP_WRITECOMBINE
																| PVRSRV_MEM_RAM_BACKED_ALLOCATION
																| PVRSRV_HAP_MULTI_PROCESS;
	psDeviceMemoryHeap->pszName = "KernelData";
	psDeviceMemoryHeap->pszBSName = "KernelData BS";
	psDeviceMemoryHeap->DevMemHeapType = DEVICE_MEMORY_HEAP_SHARED_EXPORTED;
	/* set the default (4k). System can override these as required */
	psDeviceMemoryHeap->ui32DataPageSize = SGX_MMU_PAGE_SIZE;
	psDeviceMemoryHeap++;/* advance to the next heap */


	/************* PixelShaderUSSE ***************/
	psDeviceMemoryHeap->ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX, SGX_PIXELSHADER_HEAP_ID);
	psDeviceMemoryHeap->sDevVAddrBase.uiAddr = SGX_PIXELSHADER_HEAP_BASE;
	/*
		The actual size of the pixel and vertex shader heap must be such that all
		addresses are within range of the one of the USSE code base registers, but
		the addressable range is hardware-dependent.
		SGX_PIXELSHADER_HEAP_SIZE is defined to be the maximum possible size
		to ensure that the heap layout is consistent across all SGXs.
	 */
	psDeviceMemoryHeap->ui32HeapSize = ((10 << SGX_USE_CODE_SEGMENT_RANGE_BITS) - 0x00001000);
	PVR_ASSERT(psDeviceMemoryHeap->ui32HeapSize <= SGX_PIXELSHADER_HEAP_SIZE);
	psDeviceMemoryHeap->ui32Attribs = PVRSRV_HAP_WRITECOMBINE
																| PVRSRV_MEM_RAM_BACKED_ALLOCATION
																| PVRSRV_HAP_SINGLE_PROCESS;
	psDeviceMemoryHeap->pszName = "PixelShaderUSSE";
	psDeviceMemoryHeap->pszBSName = "PixelShaderUSSE BS";
	psDeviceMemoryHeap->DevMemHeapType = DEVICE_MEMORY_HEAP_PERCONTEXT;
	/* set the default (4k). System can override these as required */
	psDeviceMemoryHeap->ui32DataPageSize = SGX_MMU_PAGE_SIZE;
	psDeviceMemoryHeap++;/* advance to the next heap */


	/************* VertexShaderUSSE ***************/
	psDeviceMemoryHeap->ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX, SGX_VERTEXSHADER_HEAP_ID);
	psDeviceMemoryHeap->sDevVAddrBase.uiAddr = SGX_VERTEXSHADER_HEAP_BASE;
	/* See comment above with PixelShaderUSSE ui32HeapSize */
	psDeviceMemoryHeap->ui32HeapSize = ((4 << SGX_USE_CODE_SEGMENT_RANGE_BITS) - 0x00001000);
	PVR_ASSERT(psDeviceMemoryHeap->ui32HeapSize <= SGX_VERTEXSHADER_HEAP_SIZE);
	psDeviceMemoryHeap->ui32Attribs = PVRSRV_HAP_WRITECOMBINE
																| PVRSRV_MEM_RAM_BACKED_ALLOCATION
																| PVRSRV_HAP_SINGLE_PROCESS;
	psDeviceMemoryHeap->pszName = "VertexShaderUSSE";
	psDeviceMemoryHeap->pszBSName = "VertexShaderUSSE BS";
	psDeviceMemoryHeap->DevMemHeapType = DEVICE_MEMORY_HEAP_PERCONTEXT;
	/* set the default (4k). System can override these as required */
	psDeviceMemoryHeap->ui32DataPageSize = SGX_MMU_PAGE_SIZE;
	psDeviceMemoryHeap++;/* advance to the next heap */


	/************* PDS Pixel Code/Data ***************/
	psDeviceMemoryHeap->ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX, SGX_PDSPIXEL_CODEDATA_HEAP_ID);
	psDeviceMemoryHeap->sDevVAddrBase.uiAddr = SGX_PDSPIXEL_CODEDATA_HEAP_BASE;
	psDeviceMemoryHeap->ui32HeapSize = SGX_PDSPIXEL_CODEDATA_HEAP_SIZE;
	psDeviceMemoryHeap->ui32Attribs = PVRSRV_HAP_WRITECOMBINE
																| PVRSRV_MEM_RAM_BACKED_ALLOCATION
																| PVRSRV_HAP_SINGLE_PROCESS;
	psDeviceMemoryHeap->pszName = "PDSPixelCodeData";
	psDeviceMemoryHeap->pszBSName = "PDSPixelCodeData BS";
	psDeviceMemoryHeap->DevMemHeapType = DEVICE_MEMORY_HEAP_PERCONTEXT;
	/* set the default (4k). System can override these as required */
	psDeviceMemoryHeap->ui32DataPageSize = SGX_MMU_PAGE_SIZE;
	psDeviceMemoryHeap++;/* advance to the next heap */


	/************* PDS Vertex Code/Data ***************/
	psDeviceMemoryHeap->ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX, SGX_PDSVERTEX_CODEDATA_HEAP_ID);
	psDeviceMemoryHeap->sDevVAddrBase.uiAddr = SGX_PDSVERTEX_CODEDATA_HEAP_BASE;
	psDeviceMemoryHeap->ui32HeapSize = SGX_PDSVERTEX_CODEDATA_HEAP_SIZE;
	psDeviceMemoryHeap->ui32Attribs = PVRSRV_HAP_WRITECOMBINE
																| PVRSRV_MEM_RAM_BACKED_ALLOCATION
																| PVRSRV_HAP_SINGLE_PROCESS;
	psDeviceMemoryHeap->pszName = "PDSVertexCodeData";
	psDeviceMemoryHeap->pszBSName = "PDSVertexCodeData BS";
	psDeviceMemoryHeap->DevMemHeapType = DEVICE_MEMORY_HEAP_PERCONTEXT;
	/* set the default (4k). System can override these as required */
	psDeviceMemoryHeap->ui32DataPageSize = SGX_MMU_PAGE_SIZE;
	psDeviceMemoryHeap++;/* advance to the next heap */


	/************* CacheCoherent ***************/
	psDeviceMemoryHeap->ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX, SGX_SYNCINFO_HEAP_ID);
	psDeviceMemoryHeap->sDevVAddrBase.uiAddr = SGX_SYNCINFO_HEAP_BASE;
	psDeviceMemoryHeap->ui32HeapSize = SGX_SYNCINFO_HEAP_SIZE;
	psDeviceMemoryHeap->ui32Attribs = PVRSRV_HAP_WRITECOMBINE
														| PVRSRV_MEM_RAM_BACKED_ALLOCATION
														| PVRSRV_HAP_MULTI_PROCESS;
	psDeviceMemoryHeap->pszName = "CacheCoherent";
	psDeviceMemoryHeap->pszBSName = "CacheCoherent BS";
	psDeviceMemoryHeap->DevMemHeapType = DEVICE_MEMORY_HEAP_SHARED_EXPORTED;
	/* set the default (4k). System can override these as required */
	psDeviceMemoryHeap->ui32DataPageSize = SGX_MMU_PAGE_SIZE;
	/* set the sync heap id */
	psDevMemoryInfo->ui32SyncHeapID = (IMG_UINT32)(psDeviceMemoryHeap - psDevMemoryInfo->psDeviceMemoryHeap);
	psDeviceMemoryHeap++;/* advance to the next heap */


	/************* Shared 3D Parameters ***************/
	psDeviceMemoryHeap->ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX, SGX_SHARED_3DPARAMETERS_HEAP_ID);
	psDeviceMemoryHeap->sDevVAddrBase.uiAddr = SGX_SHARED_3DPARAMETERS_HEAP_BASE;
	psDeviceMemoryHeap->ui32HeapSize = SGX_SHARED_3DPARAMETERS_HEAP_SIZE;
	psDeviceMemoryHeap->pszName = "Shared 3DParameters";
	psDeviceMemoryHeap->pszBSName = "Shared 3DParameters BS";
	psDeviceMemoryHeap->ui32Attribs = PVRSRV_HAP_WRITECOMBINE
													| PVRSRV_MEM_RAM_BACKED_ALLOCATION
													| PVRSRV_HAP_MULTI_PROCESS;
	psDeviceMemoryHeap->DevMemHeapType = DEVICE_MEMORY_HEAP_SHARED_EXPORTED;

	/* set the default (4k). System can override these as required */
	psDeviceMemoryHeap->ui32DataPageSize = SGX_MMU_PAGE_SIZE;
	psDeviceMemoryHeap++;/* advance to the next heap */

	/************* Percontext 3D Parameters ***************/
	psDeviceMemoryHeap->ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX, SGX_PERCONTEXT_3DPARAMETERS_HEAP_ID);
	psDeviceMemoryHeap->sDevVAddrBase.uiAddr = SGX_PERCONTEXT_3DPARAMETERS_HEAP_BASE;
	psDeviceMemoryHeap->ui32HeapSize = SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE;
	psDeviceMemoryHeap->pszName = "Percontext 3DParameters";
	psDeviceMemoryHeap->pszBSName = "Percontext 3DParameters BS";
	psDeviceMemoryHeap->ui32Attribs = PVRSRV_HAP_WRITECOMBINE
															| PVRSRV_MEM_RAM_BACKED_ALLOCATION
															| PVRSRV_HAP_SINGLE_PROCESS;
	psDeviceMemoryHeap->DevMemHeapType = DEVICE_MEMORY_HEAP_PERCONTEXT;
	/* set the default (4k). System can override these as required */
	psDeviceMemoryHeap->ui32DataPageSize = SGX_MMU_PAGE_SIZE;
	psDeviceMemoryHeap++;/* advance to the next heap */


#if defined(SUPPORT_SGX_GENERAL_MAPPING_HEAP)
	/************* General Mapping ***************/
	psDeviceMemoryHeap->ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX, SGX_GENERAL_MAPPING_HEAP_ID);
	psDeviceMemoryHeap->sDevVAddrBase.uiAddr = SGX_GENERAL_MAPPING_HEAP_BASE;
	psDeviceMemoryHeap->ui32HeapSize = SGX_GENERAL_MAPPING_HEAP_SIZE;
	psDeviceMemoryHeap->ui32Attribs = PVRSRV_HAP_WRITECOMBINE
														| PVRSRV_MEM_RAM_BACKED_ALLOCATION
														| PVRSRV_HAP_MULTI_PROCESS;
	psDeviceMemoryHeap->pszName = "GeneralMapping";
	psDeviceMemoryHeap->pszBSName = "GeneralMapping BS";
	#if defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS) && defined(FIX_HW_BRN_23410)
	/*
		if((2D hardware is enabled)
		&& (multi-mem contexts enabled)
		&& (BRN23410 is present))
		 	- then don't make the heap per-context otherwise
		 	the TA and 2D requestors must always be aligned to
		 	the same address space which could affect performance
	*/
		psDeviceMemoryHeap->DevMemHeapType = DEVICE_MEMORY_HEAP_SHARED_EXPORTED;
	#else /* defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS) && defined(FIX_HW_BRN_23410) */
		psDeviceMemoryHeap->DevMemHeapType = DEVICE_MEMORY_HEAP_PERCONTEXT;
	#endif /* defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS) && defined(FIX_HW_BRN_23410) */

	/* set the default (4k). System can override these as required */
	psDeviceMemoryHeap->ui32DataPageSize = SGX_MMU_PAGE_SIZE;
	/* specify the mapping heap ID for this device */
	psDevMemoryInfo->ui32MappingHeapID = (IMG_UINT32)(psDeviceMemoryHeap - psDevMemoryInfo->psDeviceMemoryHeap);
	psDeviceMemoryHeap++;/* advance to the next heap */
#endif /* #if defined(SUPPORT_SGX_GENERAL_MAPPING_HEAP) */


#if defined(SGX_FEATURE_2D_HARDWARE)
	/************* 2D HW Heap ***************/
	psDeviceMemoryHeap->ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX, SGX_2D_HEAP_ID);
	psDeviceMemoryHeap->sDevVAddrBase.uiAddr = SGX_2D_HEAP_BASE;
	psDeviceMemoryHeap->ui32HeapSize = SGX_2D_HEAP_SIZE;
	psDeviceMemoryHeap->ui32Attribs = PVRSRV_HAP_WRITECOMBINE
														| PVRSRV_MEM_RAM_BACKED_ALLOCATION
														| PVRSRV_HAP_SINGLE_PROCESS;
	psDeviceMemoryHeap->pszName = "2D";
	psDeviceMemoryHeap->pszBSName = "2D BS";
	psDeviceMemoryHeap->DevMemHeapType = DEVICE_MEMORY_HEAP_SHARED_EXPORTED;
	/* set the default (4k). System can override these as required */
	psDeviceMemoryHeap->ui32DataPageSize = SGX_MMU_PAGE_SIZE;
	psDeviceMemoryHeap++;/* advance to the next heap */
#endif /* #if defined(SGX_FEATURE_2D_HARDWARE) */


	/* set the heap count */
	psDevMemoryInfo->ui32HeapCount = (IMG_UINT32)(psDeviceMemoryHeap - psDevMemoryInfo->psDeviceMemoryHeap);

	return PVRSRV_OK;
}

#if defined(PDUMP)
static
PVRSRV_ERROR SGXResetPDump(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_SGXDEV_INFO *psDevInfo = (PVRSRV_SGXDEV_INFO *)(psDeviceNode->pvDevice);
	psDevInfo->psKernelCCBInfo->ui32CCBDumpWOff = 0;
	PVR_DPF((PVR_DBG_MESSAGE, "Reset pdump CCB write offset."));
	
	return PVRSRV_OK;
}
#endif /* PDUMP */


/*!
*******************************************************************************

 @Function	SGXGetClientInfoKM

 @Description	Gets the client information

 @Input hDevCookie

 @Output psClientInfo

 @Return   PVRSRV_ERROR :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR SGXGetClientInfoKM(IMG_HANDLE					hDevCookie,
								SGX_CLIENT_INFO*		psClientInfo)
{
	PVRSRV_SGXDEV_INFO *psDevInfo = (PVRSRV_SGXDEV_INFO *)((PVRSRV_DEVICE_NODE *)hDevCookie)->pvDevice;

	/*
		If this is the first client to connect to SGX perform initialisation
	*/
	psDevInfo->ui32ClientRefCount++;

	/*
		Copy information to the client info.
	*/
	psClientInfo->ui32ProcessID = OSGetCurrentProcessIDKM();

	/*
		Copy requested information.
	*/
	OSMemCopy(&psClientInfo->asDevData, &psDevInfo->asSGXDevData, sizeof(psClientInfo->asDevData));

	/* just return OK */
	return PVRSRV_OK;
}


/*!
*******************************************************************************

 @Function	SGXPanic

 @Description

 Called when an unrecoverable situation is detected. Dumps SGX debug
 information and tells the OS to panic.

 @Input psDevInfo - SGX device info

 @Return IMG_VOID

******************************************************************************/
IMG_VOID SGXPanic(PVRSRV_SGXDEV_INFO	*psDevInfo)
{
	PVR_LOG(("SGX panic"));
	SGXDumpDebugInfo(psDevInfo, IMG_FALSE);
	OSPanic();
}


/*!
*******************************************************************************

 @Function	SGXDevInitCompatCheck

 @Description

 Check compatibility of host driver and microkernel (DDK and build options)
 for SGX devices at services/device initialisation

 @Input psDeviceNode - device node

 @Return   PVRSRV_ERROR - depending on mismatch found

******************************************************************************/
PVRSRV_ERROR SGXDevInitCompatCheck(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_ERROR	eError;
	PVRSRV_SGXDEV_INFO 				*psDevInfo;
	IMG_UINT32 			ui32BuildOptions, ui32BuildOptionsMismatch;
#if !defined(NO_HARDWARE)
	PPVRSRV_KERNEL_MEM_INFO			psMemInfo;
	PVRSRV_SGX_MISCINFO_INFO		*psSGXMiscInfoInt; 	/*!< internal misc info for ukernel */
	PVRSRV_SGX_MISCINFO_FEATURES	*psSGXFeatures;
	SGX_MISCINFO_STRUCT_SIZES		*psSGXStructSizes;	/*!< microkernel structure sizes */
	IMG_BOOL						bStructSizesFailed;

	/* Exceptions list for core rev check, format is pairs of (hw rev, sw rev) */
	IMG_BOOL	bCheckCoreRev;
	const IMG_UINT32 aui32CoreRevExceptions[] =
	{
		0x10100, 0x10101
	};
	const IMG_UINT32	ui32NumCoreExceptions = sizeof(aui32CoreRevExceptions) / (2*sizeof(IMG_UINT32));
	IMG_UINT	i;
#endif

	/* Ensure it's a SGX device */
	if(psDeviceNode->sDevId.eDeviceType != PVRSRV_DEVICE_TYPE_SGX)
	{
		PVR_LOG(("(FAIL) SGXInit: Device not of type SGX"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto chk_exit;
	}

	psDevInfo = psDeviceNode->pvDevice;

	/*
	 *  1. Check kernel-side and client-side build options
	 * 	2. Ensure ukernel DDK version and driver DDK version are compatible
	 * 	3. Check ukernel build options against kernel-side build options
	 */

	/*
	 * Check KM build options against client-side host driver
	 */

	ui32BuildOptions = (SGX_BUILD_OPTIONS);
	if (ui32BuildOptions != psDevInfo->ui32ClientBuildOptions)
	{
		ui32BuildOptionsMismatch = ui32BuildOptions ^ psDevInfo->ui32ClientBuildOptions;
		if ( (psDevInfo->ui32ClientBuildOptions & ui32BuildOptionsMismatch) != 0)
		{
			PVR_LOG(("(FAIL) SGXInit: Mismatch in client-side and KM driver build options; "
				"extra options present in client-side driver: (0x%x). Please check sgx_options.h",
				psDevInfo->ui32ClientBuildOptions & ui32BuildOptionsMismatch ));
		}

		if ( (ui32BuildOptions & ui32BuildOptionsMismatch) != 0)
		{
			PVR_LOG(("(FAIL) SGXInit: Mismatch in client-side and KM driver build options; "
				"extra options present in KM: (0x%x). Please check sgx_options.h",
				ui32BuildOptions & ui32BuildOptionsMismatch ));
		}
		eError = PVRSRV_ERROR_BUILD_MISMATCH;
		goto chk_exit;
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "SGXInit: Client-side and KM driver build options match. [ OK ]"));
	}

#if !defined (NO_HARDWARE)
	psMemInfo = psDevInfo->psKernelSGXMiscMemInfo;

	/* Clear state (not strictly necessary since this is the first call) */
	psSGXMiscInfoInt = psMemInfo->pvLinAddrKM;
	psSGXMiscInfoInt->ui32MiscInfoFlags = 0;
	psSGXMiscInfoInt->ui32MiscInfoFlags |= PVRSRV_USSE_MISCINFO_GET_STRUCT_SIZES;
	eError = SGXGetMiscInfoUkernel(psDevInfo, psDeviceNode, IMG_NULL);

	/*
	 * Validate DDK version
	 */
	if(eError != PVRSRV_OK)
	{
		PVR_LOG(("(FAIL) SGXInit: Unable to validate device DDK version"));
		goto chk_exit;
	}
	psSGXFeatures = &((PVRSRV_SGX_MISCINFO_INFO*)(psMemInfo->pvLinAddrKM))->sSGXFeatures;
	if( (psSGXFeatures->ui32DDKVersion !=
		((PVRVERSION_MAJ << 16) |
		 (PVRVERSION_MIN << 8))) ||
		(psSGXFeatures->ui32DDKBuild != PVRVERSION_BUILD) )
	{
		PVR_LOG(("(FAIL) SGXInit: Incompatible driver DDK revision (%d)/device DDK revision (%d).",
				PVRVERSION_BUILD, psSGXFeatures->ui32DDKBuild));
		eError = PVRSRV_ERROR_DDK_VERSION_MISMATCH;
		goto chk_exit;
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "SGXInit: driver DDK (%d) and device DDK (%d) match. [ OK ]",
				PVRVERSION_BUILD, psSGXFeatures->ui32DDKBuild));
	}

	/*
	 * 	Check hardware core revision is compatible with the one in software
	 */
	if (psSGXFeatures->ui32CoreRevSW == 0)
	{
		/*
			Head core revision cannot be checked.
		*/
		PVR_LOG(("SGXInit: HW core rev (%x) check skipped.",
				psSGXFeatures->ui32CoreRev));
	}
	else
	{
		/* For some cores the hw/sw core revisions are expected not to match. For these
		 * exceptional cases the core rev compatibility check should be skipped.
		 */
		bCheckCoreRev = IMG_TRUE;
		for(i=0; i<ui32NumCoreExceptions; i+=2)
		{
			if( (psSGXFeatures->ui32CoreRev==aui32CoreRevExceptions[i]) &&
				(psSGXFeatures->ui32CoreRevSW==aui32CoreRevExceptions[i+1])	)
			{
				PVR_LOG(("SGXInit: HW core rev (%x), SW core rev (%x) check skipped.",
						psSGXFeatures->ui32CoreRev,
						psSGXFeatures->ui32CoreRevSW));
				bCheckCoreRev = IMG_FALSE;
			}
		}

		if (bCheckCoreRev)
		{
			if (psSGXFeatures->ui32CoreRev != psSGXFeatures->ui32CoreRevSW)
			{
				PVR_LOG(("(FAIL) SGXInit: Incompatible HW core rev (%x) and SW core rev (%x).",
						psSGXFeatures->ui32CoreRev, psSGXFeatures->ui32CoreRevSW));
						eError = PVRSRV_ERROR_BUILD_MISMATCH;
						goto chk_exit;
			}
			else
			{
				PVR_DPF((PVR_DBG_MESSAGE, "SGXInit: HW core rev (%x) and SW core rev (%x) match. [ OK ]",
						psSGXFeatures->ui32CoreRev, psSGXFeatures->ui32CoreRevSW));
			}
		}
	}

	/*
	 * 	Check ukernel structure sizes are the same as those in the driver
	 */
	psSGXStructSizes = &((PVRSRV_SGX_MISCINFO_INFO*)(psMemInfo->pvLinAddrKM))->sSGXStructSizes;

	bStructSizesFailed = IMG_FALSE;

	CHECK_SIZE(HOST_CTL);
	CHECK_SIZE(COMMAND);
#if defined(SGX_FEATURE_2D_HARDWARE)
	CHECK_SIZE(2DCMD);
	CHECK_SIZE(2DCMD_SHARED);
#endif
	CHECK_SIZE(CMDTA);
	CHECK_SIZE(CMDTA_SHARED);
	CHECK_SIZE(TRANSFERCMD);
	CHECK_SIZE(TRANSFERCMD_SHARED);

	CHECK_SIZE(3DREGISTERS);
	CHECK_SIZE(HWPBDESC);
	CHECK_SIZE(HWRENDERCONTEXT);
	CHECK_SIZE(HWRENDERDETAILS);
	CHECK_SIZE(HWRTDATA);
	CHECK_SIZE(HWRTDATASET);
	CHECK_SIZE(HWTRANSFERCONTEXT);

	if (bStructSizesFailed == IMG_TRUE)
	{
		PVR_LOG(("(FAIL) SGXInit: Mismatch in SGXMKIF structure sizes."));
		eError = PVRSRV_ERROR_BUILD_MISMATCH;
		goto chk_exit;
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "SGXInit: SGXMKIF structure sizes match. [ OK ]"));
	}

	/*
	 * Check ukernel build options against KM host driver
	 */

	ui32BuildOptions = psSGXFeatures->ui32BuildOptions;
	if (ui32BuildOptions != (SGX_BUILD_OPTIONS))
	{
		ui32BuildOptionsMismatch = ui32BuildOptions ^ (SGX_BUILD_OPTIONS);
		if ( ((SGX_BUILD_OPTIONS) & ui32BuildOptionsMismatch) != 0)
		{
			PVR_LOG(("(FAIL) SGXInit: Mismatch in driver and microkernel build options; "
				"extra options present in driver: (0x%x). Please check sgx_options.h",
				(SGX_BUILD_OPTIONS) & ui32BuildOptionsMismatch ));
		}

		if ( (ui32BuildOptions & ui32BuildOptionsMismatch) != 0)
		{
			PVR_LOG(("(FAIL) SGXInit: Mismatch in driver and microkernel build options; "
				"extra options present in microkernel: (0x%x). Please check sgx_options.h",
				ui32BuildOptions & ui32BuildOptionsMismatch ));
		}
		eError = PVRSRV_ERROR_BUILD_MISMATCH;
		goto chk_exit;
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "SGXInit: Driver and microkernel build options match. [ OK ]"));
	}
#endif // NO_HARDWARE

	eError = PVRSRV_OK;
chk_exit:
#if defined(IGNORE_SGX_INIT_COMPATIBILITY_CHECK)
	return PVRSRV_OK;
#else
	return eError;
#endif
}

/*
 * @Function	SGXGetMiscInfoUkernel
 *
 * @Description	Returns misc info (e.g. SGX build info/flags) from microkernel
 *
 * @Input	psDevInfo : device info from init phase
 * @Input	psDeviceNode : device node, used for scheduling ukernel to query SGX features
 *
 * @Return	PVRSRV_ERROR :
 *
 */
static
PVRSRV_ERROR SGXGetMiscInfoUkernel(PVRSRV_SGXDEV_INFO	*psDevInfo,
								   PVRSRV_DEVICE_NODE 	*psDeviceNode,
								   IMG_HANDLE hDevMemContext)
{
	PVRSRV_ERROR		eError;
	SGXMKIF_COMMAND		sCommandData;  /* CCB command data */
	PVRSRV_SGX_MISCINFO_INFO			*psSGXMiscInfoInt; 	/*!< internal misc info for ukernel */
	PVRSRV_SGX_MISCINFO_FEATURES		*psSGXFeatures;		/*!< sgx features for client */
	SGX_MISCINFO_STRUCT_SIZES			*psSGXStructSizes;	/*!< internal info: microkernel structure sizes */

	PPVRSRV_KERNEL_MEM_INFO	psMemInfo = psDevInfo->psKernelSGXMiscMemInfo;

	if (! psMemInfo->pvLinAddrKM)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXGetMiscInfoUkernel: Invalid address."));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	psSGXMiscInfoInt = psMemInfo->pvLinAddrKM;
	psSGXFeatures = &psSGXMiscInfoInt->sSGXFeatures;
	psSGXStructSizes = &psSGXMiscInfoInt->sSGXStructSizes;

	psSGXMiscInfoInt->ui32MiscInfoFlags &= ~PVRSRV_USSE_MISCINFO_READY;

	/* Reset SGX features */
	OSMemSet(psSGXFeatures, 0, sizeof(*psSGXFeatures));
	OSMemSet(psSGXStructSizes, 0, sizeof(*psSGXStructSizes));

	/* set up buffer address for SGX features in CCB */
	sCommandData.ui32Data[1] = psMemInfo->sDevVAddr.uiAddr; /* device V addr of output buffer */

	PDUMPCOMMENT("Microkernel kick for SGXGetMiscInfo");
	eError = SGXScheduleCCBCommandKM(psDeviceNode,
									 SGXMKIF_CMD_GETMISCINFO,
									 &sCommandData,
									 KERNEL_ID,
									 0,
									 hDevMemContext,
									 IMG_FALSE);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXGetMiscInfoUkernel: SGXScheduleCCBCommandKM failed."));
		return eError;
	}

	/* FIXME: DWORD value to determine code path in ukernel?
	 * E.g. could use getMiscInfo to obtain register values for diagnostics? */

#if !defined(NO_HARDWARE)
	{
		IMG_BOOL bExit;

		bExit = IMG_FALSE;
		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			if ((psSGXMiscInfoInt->ui32MiscInfoFlags & PVRSRV_USSE_MISCINFO_READY) != 0)
			{
				bExit = IMG_TRUE;
				break;
			}
		} END_LOOP_UNTIL_TIMEOUT();

		/*if the loop exited because a timeout*/
		if (!bExit)
		{
			PVR_DPF((PVR_DBG_ERROR, "SGXGetMiscInfoUkernel: Timeout occurred waiting for misc info."));
			return PVRSRV_ERROR_TIMEOUT;
		}
	}
#endif /* NO_HARDWARE */

	return PVRSRV_OK;
}



/*
 * @Function	SGXGetMiscInfoKM
 *
 * @Description	Returns miscellaneous SGX info
 *
 * @Input	psDevInfo : device info from init phase
 * @Input	psDeviceNode : device node, used for scheduling ukernel to query SGX features
 *
 * @Output	psMiscInfo : query request plus user-mode mem for holding returned data
 *
 * @Return	PVRSRV_ERROR :
 *
 */
IMG_EXPORT
PVRSRV_ERROR SGXGetMiscInfoKM(PVRSRV_SGXDEV_INFO	*psDevInfo,
							  SGX_MISC_INFO			*psMiscInfo,
 							  PVRSRV_DEVICE_NODE 	*psDeviceNode,
 							  IMG_HANDLE 			 hDevMemContext)
{
	PVRSRV_ERROR eError;
	PPVRSRV_KERNEL_MEM_INFO	psMemInfo = psDevInfo->psKernelSGXMiscMemInfo;
	IMG_UINT32	*pui32MiscInfoFlags = &((PVRSRV_SGX_MISCINFO_INFO*)(psMemInfo->pvLinAddrKM))->ui32MiscInfoFlags;

	/* Reset the misc info state flags */
	*pui32MiscInfoFlags = 0;

#if !defined(SUPPORT_SGX_EDM_MEMORY_DEBUG)
	PVR_UNREFERENCED_PARAMETER(hDevMemContext);
#endif

	switch(psMiscInfo->eRequest)
	{
#if defined(SGX_FEATURE_DATA_BREAKPOINTS)
		case SGX_MISC_INFO_REQUEST_SET_BREAKPOINT:
		{
			IMG_UINT32      ui32MaskDM;
			IMG_UINT32      ui32CtrlWEnable;
			IMG_UINT32      ui32CtrlREnable;
			IMG_UINT32      ui32CtrlTrapEnable;
			IMG_UINT32		ui32RegVal;
			IMG_UINT32		ui32StartRegVal;
			IMG_UINT32		ui32EndRegVal;
			SGXMKIF_COMMAND	sCommandData;

			/* Set or Clear BP? */
			if(psMiscInfo->uData.sSGXBreakpointInfo.bBPEnable)
			{
				/* set the break point */
				IMG_DEV_VIRTADDR sBPDevVAddr = psMiscInfo->uData.sSGXBreakpointInfo.sBPDevVAddr;
				IMG_DEV_VIRTADDR sBPDevVAddrEnd = psMiscInfo->uData.sSGXBreakpointInfo.sBPDevVAddrEnd;

				/* BP address */
				ui32StartRegVal = sBPDevVAddr.uiAddr & EUR_CR_BREAKPOINT0_START_ADDRESS_MASK;
				ui32EndRegVal = sBPDevVAddrEnd.uiAddr & EUR_CR_BREAKPOINT0_END_ADDRESS_MASK;

				ui32MaskDM = psMiscInfo->uData.sSGXBreakpointInfo.ui32DataMasterMask;
				ui32CtrlWEnable = psMiscInfo->uData.sSGXBreakpointInfo.bWrite;
				ui32CtrlREnable = psMiscInfo->uData.sSGXBreakpointInfo.bRead;
				ui32CtrlTrapEnable = psMiscInfo->uData.sSGXBreakpointInfo.bTrapped;

				/* normal data BP */
				ui32RegVal = ((ui32MaskDM<<EUR_CR_BREAKPOINT0_MASK_DM_SHIFT) & EUR_CR_BREAKPOINT0_MASK_DM_MASK) |
							 ((ui32CtrlWEnable<<EUR_CR_BREAKPOINT0_CTRL_WENABLE_SHIFT) & EUR_CR_BREAKPOINT0_CTRL_WENABLE_MASK) |
							 ((ui32CtrlREnable<<EUR_CR_BREAKPOINT0_CTRL_RENABLE_SHIFT) & EUR_CR_BREAKPOINT0_CTRL_RENABLE_MASK) |
							 ((ui32CtrlTrapEnable<<EUR_CR_BREAKPOINT0_CTRL_TRAPENABLE_SHIFT) & EUR_CR_BREAKPOINT0_CTRL_TRAPENABLE_MASK);
			}
			else
			{
				/* clear the break point */
				ui32RegVal = ui32StartRegVal = ui32EndRegVal = 0;
			}

			/* setup the command */
			sCommandData.ui32Data[0] = psMiscInfo->uData.sSGXBreakpointInfo.ui32BPIndex;
			sCommandData.ui32Data[1] = ui32StartRegVal;
			sCommandData.ui32Data[2] = ui32EndRegVal;
			sCommandData.ui32Data[3] = ui32RegVal;

			/* clear signal flags */
			psDevInfo->psSGXHostCtl->ui32BPSetClearSignal = 0;

			PDUMPCOMMENT("Microkernel kick for setting a data breakpoint");
			eError = SGXScheduleCCBCommandKM(psDeviceNode,
											 SGXMKIF_CMD_DATABREAKPOINT,
											 &sCommandData,
											 KERNEL_ID,
											 0,
											 hDevMemContext,
											 IMG_FALSE);

			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "SGXGetMiscInfoKM: SGXScheduleCCBCommandKM failed."));
				return eError;
			}

#if defined(NO_HARDWARE)
			/* clear signal flags */
			psDevInfo->psSGXHostCtl->ui32BPSetClearSignal = 0;
#else
			{
				IMG_BOOL bExit;

				bExit = IMG_FALSE;
				LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
				{
					if (psDevInfo->psSGXHostCtl->ui32BPSetClearSignal != 0)
					{
						bExit = IMG_TRUE;
						/* clear signal flags */
						psDevInfo->psSGXHostCtl->ui32BPSetClearSignal = 0;
						break;
					}
				} END_LOOP_UNTIL_TIMEOUT();

				/*if the loop exited because a timeout*/
				if (!bExit)
				{
					PVR_DPF((PVR_DBG_ERROR, "SGXGetMiscInfoKM: Timeout occurred waiting BP set/clear"));
					return PVRSRV_ERROR_TIMEOUT;
				}
			}
#endif /* NO_HARDWARE */

			return PVRSRV_OK;
		}

		case SGX_MISC_INFO_REQUEST_POLL_BREAKPOINT:
		{
			/* This request checks to see whether a breakpoint has
			   been trapped.  If so, it returns the number of the
			   breakpoint number that was trapped in ui32BPIndex,
			   sTrappedBPDevVAddr to the address which was trapped,
			   and sets bTrappedBP.  Otherwise, bTrappedBP will be
			   false, and other fields should be ignored. */
			/* The uKernel is not used, since if we are stopped on a
			   breakpoint, it is not possible to guarantee that the
			   uKernel would be able to run */
#if !defined(NO_HARDWARE)
#if defined(SGX_FEATURE_MP)
			IMG_BOOL bTrappedBPMaster;
			IMG_UINT32 ui32CoreNum, ui32TrappedBPCoreNum;
#if defined(SGX_FEATURE_PERPIPE_BKPT_REGS)
			IMG_UINT32 ui32PipeNum, ui32TrappedBPPipeNum;
/* ui32PipeNum is the pipe number plus 1, or 0 to represent "partition" */
#define NUM_PIPES_PLUS_ONE (SGX_FEATURE_PERPIPE_BKPT_REGS_NUMPIPES+1)
#endif
			IMG_BOOL bTrappedBPAny;
#endif /* defined(SGX_FEATURE_MP) */
			IMG_BOOL bFoundOne;

#if defined(SGX_FEATURE_MP)
			ui32TrappedBPCoreNum = 0;
			bTrappedBPMaster = !!(EUR_CR_MASTER_BREAKPOINT_TRAPPED_MASK & OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_MASTER_BREAKPOINT));
			bTrappedBPAny = bTrappedBPMaster;
#if defined(SGX_FEATURE_PERPIPE_BKPT_REGS)
			ui32TrappedBPPipeNum = 0; /* just to keep the (incorrect) compiler happy */
#endif
			for (ui32CoreNum = 0; ui32CoreNum < SGX_FEATURE_MP_CORE_COUNT_3D; ui32CoreNum++)
			{
#if defined(SGX_FEATURE_PERPIPE_BKPT_REGS)
				/* FIXME:  this macro makes the assumption that the PARTITION regs are the same
				   distance before the PIPE0 regs as the PIPE1 regs are after it, _and_
				   assumes that the fields in the partition regs are in the same place
				   in the pipe regs.  Need to validate these assumptions, or assert them */
#define SGX_MP_CORE_PIPE_SELECT(r,c,p) \
				((SGX_MP_CORE_SELECT(EUR_CR_PARTITION_##r,c) + p*(EUR_CR_PIPE0_##r-EUR_CR_PARTITION_##r)))
				for (ui32PipeNum = 0; ui32PipeNum < NUM_PIPES_PLUS_ONE; ui32PipeNum++)
				{
					bFoundOne =
						0 != (EUR_CR_PARTITION_BREAKPOINT_TRAPPED_MASK & 
							  OSReadHWReg(psDevInfo->pvRegsBaseKM, 
										  SGX_MP_CORE_PIPE_SELECT(BREAKPOINT,
																  ui32CoreNum,
																  ui32PipeNum)));
					if (bFoundOne)
					{
						bTrappedBPAny = IMG_TRUE;
						ui32TrappedBPCoreNum = ui32CoreNum;
						ui32TrappedBPPipeNum = ui32PipeNum;
					}
				}
#else /* defined(SGX_FEATURE_PERPIPE_BKPT_REGS) */
				bFoundOne = !!(EUR_CR_BREAKPOINT_TRAPPED_MASK & OSReadHWReg(psDevInfo->pvRegsBaseKM, SGX_MP_CORE_SELECT(EUR_CR_BREAKPOINT, ui32CoreNum)));
				if (bFoundOne)
				{
					bTrappedBPAny = IMG_TRUE;
					ui32TrappedBPCoreNum = ui32CoreNum;
				}
#endif /* defined(SGX_FEATURE_PERPIPE_BKPT_REGS) */
			}

			psMiscInfo->uData.sSGXBreakpointInfo.bTrappedBP = bTrappedBPAny;
#else /* defined(SGX_FEATURE_MP) */
#if defined(SGX_FEATURE_PERPIPE_BKPT_REGS)
			#error Not yet considered the case for per-pipe regs in non-mp case
#endif
			psMiscInfo->uData.sSGXBreakpointInfo.bTrappedBP = 0 != (EUR_CR_BREAKPOINT_TRAPPED_MASK & OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BREAKPOINT));
#endif /* defined(SGX_FEATURE_MP) */

			if (psMiscInfo->uData.sSGXBreakpointInfo.bTrappedBP)
			{
				IMG_UINT32 ui32Info0, ui32Info1;

#if defined(SGX_FEATURE_MP)
#if defined(SGX_FEATURE_PERPIPE_BKPT_REGS)
				ui32Info0 = OSReadHWReg(psDevInfo->pvRegsBaseKM, bTrappedBPMaster?EUR_CR_MASTER_BREAKPOINT_TRAP_INFO0:SGX_MP_CORE_PIPE_SELECT(BREAKPOINT_TRAP_INFO0, ui32TrappedBPCoreNum, ui32TrappedBPPipeNum));
				ui32Info1 = OSReadHWReg(psDevInfo->pvRegsBaseKM, bTrappedBPMaster?EUR_CR_MASTER_BREAKPOINT_TRAP_INFO1:SGX_MP_CORE_PIPE_SELECT(BREAKPOINT_TRAP_INFO1, ui32TrappedBPCoreNum, ui32TrappedBPPipeNum));
#else /* defined(SGX_FEATURE_PERPIPE_BKPT_REGS) */
				ui32Info0 = OSReadHWReg(psDevInfo->pvRegsBaseKM, bTrappedBPMaster?EUR_CR_MASTER_BREAKPOINT_TRAP_INFO0:SGX_MP_CORE_SELECT(EUR_CR_BREAKPOINT_TRAP_INFO0, ui32TrappedBPCoreNum));
				ui32Info1 = OSReadHWReg(psDevInfo->pvRegsBaseKM, bTrappedBPMaster?EUR_CR_MASTER_BREAKPOINT_TRAP_INFO1:SGX_MP_CORE_SELECT(EUR_CR_BREAKPOINT_TRAP_INFO1, ui32TrappedBPCoreNum));
#endif /* defined(SGX_FEATURE_PERPIPE_BKPT_REGS) */
#else /* defined(SGX_FEATURE_MP) */
				ui32Info0 = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BREAKPOINT_TRAP_INFO0);
				ui32Info1 = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BREAKPOINT_TRAP_INFO1);
#endif /* defined(SGX_FEATURE_MP) */

#ifdef SGX_FEATURE_PERPIPE_BKPT_REGS
				psMiscInfo->uData.sSGXBreakpointInfo.ui32BPIndex = (ui32Info1 & EUR_CR_PARTITION_BREAKPOINT_TRAP_INFO1_NUMBER_MASK) >> EUR_CR_PARTITION_BREAKPOINT_TRAP_INFO1_NUMBER_SHIFT;
				psMiscInfo->uData.sSGXBreakpointInfo.sTrappedBPDevVAddr.uiAddr = ui32Info0 & EUR_CR_PARTITION_BREAKPOINT_TRAP_INFO0_ADDRESS_MASK;
				psMiscInfo->uData.sSGXBreakpointInfo.ui32TrappedBPBurstLength = (ui32Info1 & EUR_CR_PARTITION_BREAKPOINT_TRAP_INFO1_SIZE_MASK) >> EUR_CR_PARTITION_BREAKPOINT_TRAP_INFO1_SIZE_SHIFT;
				psMiscInfo->uData.sSGXBreakpointInfo.bTrappedBPRead = !!(ui32Info1 & EUR_CR_PARTITION_BREAKPOINT_TRAP_INFO1_RNW_MASK);
				psMiscInfo->uData.sSGXBreakpointInfo.ui32TrappedBPDataMaster = (ui32Info1 & EUR_CR_PARTITION_BREAKPOINT_TRAP_INFO1_DATA_MASTER_MASK) >> EUR_CR_PARTITION_BREAKPOINT_TRAP_INFO1_DATA_MASTER_SHIFT;
				psMiscInfo->uData.sSGXBreakpointInfo.ui32TrappedBPTag = (ui32Info1 & EUR_CR_PARTITION_BREAKPOINT_TRAP_INFO1_TAG_MASK) >> EUR_CR_PARTITION_BREAKPOINT_TRAP_INFO1_TAG_SHIFT;
#else /* defined(SGX_FEATURE_PERPIPE_BKPT_REGS) */
				psMiscInfo->uData.sSGXBreakpointInfo.ui32BPIndex = (ui32Info1 & EUR_CR_BREAKPOINT_TRAP_INFO1_NUMBER_MASK) >> EUR_CR_BREAKPOINT_TRAP_INFO1_NUMBER_SHIFT;
				psMiscInfo->uData.sSGXBreakpointInfo.sTrappedBPDevVAddr.uiAddr = ui32Info0 & EUR_CR_BREAKPOINT_TRAP_INFO0_ADDRESS_MASK;
				psMiscInfo->uData.sSGXBreakpointInfo.ui32TrappedBPBurstLength = (ui32Info1 & EUR_CR_BREAKPOINT_TRAP_INFO1_SIZE_MASK) >> EUR_CR_BREAKPOINT_TRAP_INFO1_SIZE_SHIFT;
				psMiscInfo->uData.sSGXBreakpointInfo.bTrappedBPRead = !!(ui32Info1 & EUR_CR_BREAKPOINT_TRAP_INFO1_RNW_MASK);
				psMiscInfo->uData.sSGXBreakpointInfo.ui32TrappedBPDataMaster = (ui32Info1 & EUR_CR_BREAKPOINT_TRAP_INFO1_DATA_MASTER_MASK) >> EUR_CR_BREAKPOINT_TRAP_INFO1_DATA_MASTER_SHIFT;
				psMiscInfo->uData.sSGXBreakpointInfo.ui32TrappedBPTag = (ui32Info1 & EUR_CR_BREAKPOINT_TRAP_INFO1_TAG_MASK) >> EUR_CR_BREAKPOINT_TRAP_INFO1_TAG_SHIFT;
#endif /* defined(SGX_FEATURE_PERPIPE_BKPT_REGS) */
#if defined(SGX_FEATURE_MP)
#if defined(SGX_FEATURE_PERPIPE_BKPT_REGS)
				/* mp, per-pipe regbanks */
				psMiscInfo->uData.sSGXBreakpointInfo.ui32CoreNum = bTrappedBPMaster?65535:(ui32TrappedBPCoreNum + (ui32TrappedBPPipeNum<<10));
#else /* defined(SGX_FEATURE_PERPIPE_BKPT_REGS) */
				/* mp, regbanks unsplit */
				psMiscInfo->uData.sSGXBreakpointInfo.ui32CoreNum = bTrappedBPMaster?65535:ui32TrappedBPCoreNum;
#endif /* defined(SGX_FEATURE_PERPIPE_BKPT_REGS) */
#else /* defined(SGX_FEATURE_MP) */
#if defined(SGX_FEATURE_PERPIPE_BKPT_REGS)
				/* non-mp, per-pipe regbanks */
#error non-mp perpipe regs not yet supported
#else /* defined(SGX_FEATURE_PERPIPE_BKPT_REGS) */
				/* non-mp */
				psMiscInfo->uData.sSGXBreakpointInfo.ui32CoreNum = 65534;
#endif /* defined(SGX_FEATURE_PERPIPE_BKPT_REGS) */
#endif /* defined(SGX_FEATURE_MP) */
			}
#endif /* !defined(NO_HARDWARE) */
			return PVRSRV_OK;
		}

		case SGX_MISC_INFO_REQUEST_RESUME_BREAKPOINT:
		{
			/* This request resumes from the currently trapped breakpoint. */
			/* Core number must be supplied */
			/* Polls for notify to be acknowledged by h/w */
#if !defined(NO_HARDWARE)
#if defined(SGX_FEATURE_MP)
			IMG_UINT32 ui32CoreNum;
			IMG_BOOL bMaster;
#if defined(SGX_FEATURE_PERPIPE_BKPT_REGS)
			IMG_UINT32 ui32PipeNum;
#endif
#endif /* defined(SGX_FEATURE_MP) */
			IMG_UINT32 ui32OldSeqNum, ui32NewSeqNum;

#if defined(SGX_FEATURE_MP)
#if defined(SGX_FEATURE_PERPIPE_BKPT_REGS)
			ui32PipeNum = psMiscInfo->uData.sSGXBreakpointInfo.ui32CoreNum >> 10;
			ui32CoreNum = psMiscInfo->uData.sSGXBreakpointInfo.ui32CoreNum & 1023;
			bMaster = psMiscInfo->uData.sSGXBreakpointInfo.ui32CoreNum > 32767;
#else /* defined(SGX_FEATURE_PERPIPE_BKPT_REGS) */
			ui32CoreNum = psMiscInfo->uData.sSGXBreakpointInfo.ui32CoreNum;
			bMaster = ui32CoreNum > SGX_FEATURE_MP_CORE_COUNT_3D;
#endif /* defined(SGX_FEATURE_PERPIPE_BKPT_REGS) */
			if (bMaster)
			{
				/* master */
				/* EUR_CR_MASTER_BREAKPOINT_TRAPPED_MASK | EUR_CR_MASTER_BREAKPOINT_SEQNUM_MASK */
				ui32OldSeqNum = 0x1c & OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_MASTER_BREAKPOINT);
				OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_MASTER_BREAKPOINT_TRAP, EUR_CR_MASTER_BREAKPOINT_TRAP_WRNOTIFY_MASK | EUR_CR_MASTER_BREAKPOINT_TRAP_CONTINUE_MASK);
				do
				{
					ui32NewSeqNum = 0x1c & OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_MASTER_BREAKPOINT);
				}
				while (ui32OldSeqNum == ui32NewSeqNum);
			}
			else
#endif /* defined(SGX_FEATURE_MP) */
			{
				/* core */
#if defined(SGX_FEATURE_PERPIPE_BKPT_REGS)
				ui32OldSeqNum = 0x1c & OSReadHWReg(psDevInfo->pvRegsBaseKM, SGX_MP_CORE_PIPE_SELECT(BREAKPOINT, ui32CoreNum, ui32PipeNum));
				OSWriteHWReg(psDevInfo->pvRegsBaseKM, SGX_MP_CORE_PIPE_SELECT(BREAKPOINT_TRAP, ui32CoreNum, ui32PipeNum), EUR_CR_PARTITION_BREAKPOINT_TRAP_WRNOTIFY_MASK | EUR_CR_PARTITION_BREAKPOINT_TRAP_CONTINUE_MASK);
				do
				{
					ui32NewSeqNum = 0x1c & OSReadHWReg(psDevInfo->pvRegsBaseKM, SGX_MP_CORE_PIPE_SELECT(BREAKPOINT, ui32CoreNum, ui32PipeNum));
				}
				while (ui32OldSeqNum == ui32NewSeqNum);
#else /* defined(SGX_FEATURE_PERPIPE_BKPT_REGS) */
				ui32OldSeqNum = 0x1c & OSReadHWReg(psDevInfo->pvRegsBaseKM, SGX_MP_CORE_SELECT(EUR_CR_BREAKPOINT, ui32CoreNum));
				OSWriteHWReg(psDevInfo->pvRegsBaseKM, SGX_MP_CORE_SELECT(EUR_CR_BREAKPOINT_TRAP, ui32CoreNum), EUR_CR_BREAKPOINT_TRAP_WRNOTIFY_MASK | EUR_CR_BREAKPOINT_TRAP_CONTINUE_MASK);
				do
				{
					ui32NewSeqNum = 0x1c & OSReadHWReg(psDevInfo->pvRegsBaseKM, SGX_MP_CORE_SELECT(EUR_CR_BREAKPOINT, ui32CoreNum));
				}
				while (ui32OldSeqNum == ui32NewSeqNum);
#endif /* defined(SGX_FEATURE_PERPIPE_BKPT_REGS) */
			}
#endif /* !defined(NO_HARDWARE) */
			return PVRSRV_OK;
		}
#endif /* SGX_FEATURE_DATA_BREAKPOINTS)	*/

		case SGX_MISC_INFO_REQUEST_CLOCKSPEED:
		{
			psMiscInfo->uData.ui32SGXClockSpeed = psDevInfo->ui32CoreClockSpeed;
			return PVRSRV_OK;
		}

		case SGX_MISC_INFO_REQUEST_CLOCKSPEED_SLCSIZE:
		{
			psMiscInfo->uData.sQueryClockSpeedSLCSize.ui32SGXClockSpeed = SYS_SGX_CLOCK_SPEED;
#if defined(SGX_FEATURE_SYSTEM_CACHE) && defined(SYS_SGX_SLC_SIZE)
			psMiscInfo->uData.sQueryClockSpeedSLCSize.ui32SGXSLCSize = SYS_SGX_SLC_SIZE;
#else
			psMiscInfo->uData.sQueryClockSpeedSLCSize.ui32SGXSLCSize = 0;
#endif /* defined(SGX_FEATURE_SYSTEM_CACHE) && defined(SYS_SGX_SLC_SIZE) */
			return PVRSRV_OK;
		}

		case SGX_MISC_INFO_REQUEST_ACTIVEPOWER:
		{
			psMiscInfo->uData.sActivePower.ui32NumActivePowerEvents = psDevInfo->psSGXHostCtl->ui32NumActivePowerEvents;
			return PVRSRV_OK;
		}

		case SGX_MISC_INFO_REQUEST_LOCKUPS:
		{
#if defined(SUPPORT_HW_RECOVERY)
			psMiscInfo->uData.sLockups.ui32uKernelDetectedLockups = psDevInfo->psSGXHostCtl->ui32uKernelDetectedLockups;
			psMiscInfo->uData.sLockups.ui32HostDetectedLockups = psDevInfo->psSGXHostCtl->ui32HostDetectedLockups;
#else
			psMiscInfo->uData.sLockups.ui32uKernelDetectedLockups = 0;
			psMiscInfo->uData.sLockups.ui32HostDetectedLockups = 0;
#endif
			return PVRSRV_OK;
		}

		case SGX_MISC_INFO_REQUEST_SPM:
		{
			/* this is dealt with in UM */
			return PVRSRV_OK;
		}

		case SGX_MISC_INFO_REQUEST_SGXREV:
		{
			PVRSRV_SGX_MISCINFO_FEATURES		*psSGXFeatures;
//			PPVRSRV_KERNEL_MEM_INFO	psMemInfo = psDevInfo->psKernelSGXMiscMemInfo;

			eError = SGXGetMiscInfoUkernel(psDevInfo, psDeviceNode, hDevMemContext);
			if(eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "An error occurred in SGXGetMiscInfoUkernel: %d\n",
						eError));
				return eError;
			}
			psSGXFeatures = &((PVRSRV_SGX_MISCINFO_INFO*)(psMemInfo->pvLinAddrKM))->sSGXFeatures;

			/* Copy SGX features into misc info struct, to return to client */
			psMiscInfo->uData.sSGXFeatures = *psSGXFeatures;

			/* Debug output */
			PVR_DPF((PVR_DBG_MESSAGE, "SGXGetMiscInfoKM: Core 0x%x, sw ID 0x%x, sw Rev 0x%x\n",
					psSGXFeatures->ui32CoreRev,
					psSGXFeatures->ui32CoreIdSW,
					psSGXFeatures->ui32CoreRevSW));
			PVR_DPF((PVR_DBG_MESSAGE, "SGXGetMiscInfoKM: DDK version 0x%x, DDK build 0x%x\n",
					psSGXFeatures->ui32DDKVersion,
					psSGXFeatures->ui32DDKBuild));

			/* done! */
			return PVRSRV_OK;
		}

		case SGX_MISC_INFO_REQUEST_DRIVER_SGXREV:
		{
			PVRSRV_SGX_MISCINFO_FEATURES		*psSGXFeatures;

			psSGXFeatures = &((PVRSRV_SGX_MISCINFO_INFO*)(psMemInfo->pvLinAddrKM))->sSGXFeatures;

			/* Reset the misc information to prevent
			 * confusion with values returned from the ukernel
			 */
			OSMemSet(psMemInfo->pvLinAddrKM, 0,
					sizeof(PVRSRV_SGX_MISCINFO_INFO));

			psSGXFeatures->ui32DDKVersion =
				(PVRVERSION_MAJ << 16) |
				(PVRVERSION_MIN << 8);
			psSGXFeatures->ui32DDKBuild = PVRVERSION_BUILD;

			/* Also report the kernel module build options -- used in SGXConnectionCheck() */
			psSGXFeatures->ui32BuildOptions = (SGX_BUILD_OPTIONS);

			/* Copy SGX features into misc info struct, to return to client */
			psMiscInfo->uData.sSGXFeatures = *psSGXFeatures;
			return PVRSRV_OK;
		}

#if defined(PVRSRV_USSE_EDM_STATUS_DEBUG)
		case SGX_MISC_INFO_REQUEST_EDM_STATUS_BUFFER_INFO:
		{
			/* Report the EDM status buffer location in memory */
			psMiscInfo->uData.sEDMStatusBufferInfo.sDevVAEDMStatusBuffer = psDevInfo->psKernelEDMStatusBufferMemInfo->sDevVAddr;
			psMiscInfo->uData.sEDMStatusBufferInfo.pvEDMStatusBuffer = psDevInfo->psKernelEDMStatusBufferMemInfo->pvLinAddrKM;
			return PVRSRV_OK;
		}
#endif

#if defined(SUPPORT_SGX_EDM_MEMORY_DEBUG)
		case SGX_MISC_INFO_REQUEST_MEMREAD:
		case SGX_MISC_INFO_REQUEST_MEMCOPY:
		{
			PVRSRV_ERROR eError;
			PVRSRV_SGX_MISCINFO_FEATURES		*psSGXFeatures;
			PVRSRV_SGX_MISCINFO_MEMACCESS		*psSGXMemSrc;	/* user-defined mem read */
			PVRSRV_SGX_MISCINFO_MEMACCESS		*psSGXMemDest;	/* user-defined mem write */

			{				
				/* Set the mem read flag; src is user-defined */
				*pui32MiscInfoFlags |= PVRSRV_USSE_MISCINFO_MEMREAD;
				psSGXMemSrc = &((PVRSRV_SGX_MISCINFO_INFO*)(psMemInfo->pvLinAddrKM))->sSGXMemAccessSrc;

				if(psMiscInfo->sDevVAddrSrc.uiAddr != 0)
				{
					psSGXMemSrc->sDevVAddr = psMiscInfo->sDevVAddrSrc; /* src address */
				}
				else
				{
					return PVRSRV_ERROR_INVALID_PARAMS;
				}				
			}

			if( psMiscInfo->eRequest == SGX_MISC_INFO_REQUEST_MEMCOPY)
			{				
				/* Set the mem write flag; dest is user-defined */
				*pui32MiscInfoFlags |= PVRSRV_USSE_MISCINFO_MEMWRITE;
				psSGXMemDest = &((PVRSRV_SGX_MISCINFO_INFO*)(psMemInfo->pvLinAddrKM))->sSGXMemAccessDest;
				
				if(psMiscInfo->sDevVAddrDest.uiAddr != 0)
				{
					psSGXMemDest->sDevVAddr = psMiscInfo->sDevVAddrDest; /* dest address */
				}
				else
				{
					return PVRSRV_ERROR_INVALID_PARAMS;
				}
			}

			/* Get physical address of PD for memory read (may need to switch context in microkernel) */
			if(psMiscInfo->hDevMemContext != IMG_NULL)
			{
				SGXGetMMUPDAddrKM( (IMG_HANDLE)psDeviceNode, hDevMemContext, &psSGXMemSrc->sPDDevPAddr);
				
				/* Single app will always use the same src and dest mem context */
				psSGXMemDest->sPDDevPAddr = psSGXMemSrc->sPDDevPAddr;
			}
			else
			{
				return PVRSRV_ERROR_INVALID_PARAMS;
			}

			/* Submit the task to the ukernel */
			eError = SGXGetMiscInfoUkernel(psDevInfo, psDeviceNode);
			if(eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "An error occurred in SGXGetMiscInfoUkernel: %d\n",
						eError));
				return eError;
			}
			psSGXFeatures = &((PVRSRV_SGX_MISCINFO_INFO*)(psMemInfo->pvLinAddrKM))->sSGXFeatures;

#if !defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS)
			if(*pui32MiscInfoFlags & PVRSRV_USSE_MISCINFO_MEMREAD_FAIL)
			{
				return PVRSRV_ERROR_INVALID_MISCINFO;
			}
#endif
			/* Copy SGX features into misc info struct, to return to client */
			psMiscInfo->uData.sSGXFeatures = *psSGXFeatures;
			return PVRSRV_OK;
		}
#endif /* SUPPORT_SGX_EDM_MEMORY_DEBUG */

#if defined(SUPPORT_SGX_HWPERF)
		case SGX_MISC_INFO_REQUEST_SET_HWPERF_STATUS:
		{
			PVRSRV_SGX_MISCINFO_SET_HWPERF_STATUS	*psSetHWPerfStatus = &psMiscInfo->uData.sSetHWPerfStatus;
			const IMG_UINT32	ui32ValidFlags = PVRSRV_SGX_HWPERF_STATUS_RESET_COUNTERS |
												 PVRSRV_SGX_HWPERF_STATUS_GRAPHICS_ON |
												 PVRSRV_SGX_HWPERF_STATUS_PERIODIC_ON |
												 PVRSRV_SGX_HWPERF_STATUS_MK_EXECUTION_ON;
			SGXMKIF_COMMAND		sCommandData = {0};

			/* Check for valid flags */
			if ((psSetHWPerfStatus->ui32NewHWPerfStatus & ~ui32ValidFlags) != 0)
			{
				return PVRSRV_ERROR_INVALID_PARAMS;
			}

			#if defined(PDUMP)
			PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS,
								  "SGX ukernel HWPerf status %u\n",
								  psSetHWPerfStatus->ui32NewHWPerfStatus);
			#endif /* PDUMP */

			/* Copy the new group selector(s) to the host ctl for the ukernel */
			#if defined(SGX_FEATURE_EXTENDED_PERF_COUNTERS)
			OSMemCopy(&psDevInfo->psSGXHostCtl->aui32PerfGroup[0],
					  &psSetHWPerfStatus->aui32PerfGroup[0],
					  sizeof(psDevInfo->psSGXHostCtl->aui32PerfGroup));
			OSMemCopy(&psDevInfo->psSGXHostCtl->aui32PerfBit[0],
					  &psSetHWPerfStatus->aui32PerfBit[0],
					  sizeof(psDevInfo->psSGXHostCtl->aui32PerfBit));
			psDevInfo->psSGXHostCtl->ui32PerfCounterBitSelect = psSetHWPerfStatus->ui32PerfCounterBitSelect;
			psDevInfo->psSGXHostCtl->ui32PerfSumMux = psSetHWPerfStatus->ui32PerfSumMux;
			#if defined(PDUMP)
			PDUMPMEM(IMG_NULL, psDevInfo->psKernelSGXHostCtlMemInfo,
					 offsetof(SGXMKIF_HOST_CTL, aui32PerfGroup),
					 sizeof(psDevInfo->psSGXHostCtl->aui32PerfGroup),
					 PDUMP_FLAGS_CONTINUOUS,
					 MAKEUNIQUETAG(psDevInfo->psKernelSGXHostCtlMemInfo));
			PDUMPMEM(IMG_NULL, psDevInfo->psKernelSGXHostCtlMemInfo,
					 offsetof(SGXMKIF_HOST_CTL, aui32PerfBit),
					 sizeof(psDevInfo->psSGXHostCtl->aui32PerfBit),
					 PDUMP_FLAGS_CONTINUOUS,
					 MAKEUNIQUETAG(psDevInfo->psKernelSGXHostCtlMemInfo));
			PDUMPMEM(IMG_NULL, psDevInfo->psKernelSGXHostCtlMemInfo,
					 offsetof(SGXMKIF_HOST_CTL, ui32PerfCounterBitSelect),
					 sizeof(psDevInfo->psSGXHostCtl->ui32PerfCounterBitSelect),
					 PDUMP_FLAGS_CONTINUOUS,
					 MAKEUNIQUETAG(psDevInfo->psKernelSGXHostCtlMemInfo));
			PDUMPMEM(IMG_NULL, psDevInfo->psKernelSGXHostCtlMemInfo,
					 offsetof(SGXMKIF_HOST_CTL, ui32PerfSumMux),
					 sizeof(psDevInfo->psSGXHostCtl->ui32PerfSumMux),
					 PDUMP_FLAGS_CONTINUOUS,
					 MAKEUNIQUETAG(psDevInfo->psKernelSGXHostCtlMemInfo));
			#endif /* PDUMP */
			#else
			psDevInfo->psSGXHostCtl->ui32PerfGroup = psSetHWPerfStatus->ui32PerfGroup;
			#if defined(PDUMP)
			PDUMPMEM(IMG_NULL, psDevInfo->psKernelSGXHostCtlMemInfo,
					 offsetof(SGXMKIF_HOST_CTL, ui32PerfGroup),
					 sizeof(psDevInfo->psSGXHostCtl->ui32PerfGroup),
					 PDUMP_FLAGS_CONTINUOUS,
					 MAKEUNIQUETAG(psDevInfo->psKernelSGXHostCtlMemInfo));
			#endif /* PDUMP */
			#endif /* SGX_FEATURE_EXTENDED_PERF_COUNTERS */

			/* Kick the ukernel to update the hardware state */
			sCommandData.ui32Data[0] = psSetHWPerfStatus->ui32NewHWPerfStatus;
			eError = SGXScheduleCCBCommandKM(psDeviceNode,
											 SGXMKIF_CMD_SETHWPERFSTATUS,
											 &sCommandData,
											 KERNEL_ID,
											 0,
											 hDevMemContext,
											 IMG_FALSE);
			return eError;
		}
#endif /* SUPPORT_SGX_HWPERF */

		case SGX_MISC_INFO_DUMP_DEBUG_INFO:
		{
			PVR_LOG(("User requested SGX debug info"));

			/* Dump SGX debug data to the kernel log. */
			SGXDumpDebugInfo(psDeviceNode->pvDevice, IMG_FALSE);

			return PVRSRV_OK;
		}

		case SGX_MISC_INFO_DUMP_DEBUG_INFO_FORCE_REGS:
		{
			PVR_LOG(("User requested SGX debug info"));

			/* Dump SGX debug data to the kernel log. */
			SGXDumpDebugInfo(psDeviceNode->pvDevice, IMG_TRUE);

			return PVRSRV_OK;
		}

#if defined(DEBUG)
		/* Don't allow user-mode to reboot the device in production drivers */
		case SGX_MISC_INFO_PANIC:
		{
			PVR_LOG(("User requested SGX panic"));

			SGXPanic(psDeviceNode->pvDevice);

			return PVRSRV_OK;
		}
#endif

		default:
		{
			/* switch statement fell though, so: */
			return PVRSRV_ERROR_INVALID_PARAMS;
		}
	}
}


IMG_EXPORT
PVRSRV_ERROR SGXReadHWPerfCBKM(IMG_HANDLE					hDevHandle,
							   IMG_UINT32					ui32ArraySize,
							   PVRSRV_SGX_HWPERF_CB_ENTRY	*psClientHWPerfEntry,
							   IMG_UINT32					*pui32DataCount,
							   IMG_UINT32					*pui32ClockSpeed,
							   IMG_UINT32					*pui32HostTimeStamp)
{
	PVRSRV_ERROR    	eError = PVRSRV_OK;
	PVRSRV_DEVICE_NODE	*psDeviceNode = hDevHandle;
	PVRSRV_SGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	SGXMKIF_HWPERF_CB	*psHWPerfCB = psDevInfo->psKernelHWPerfCBMemInfo->pvLinAddrKM;
	IMG_UINT			i;

	for (i = 0;
		 psHWPerfCB->ui32Woff != psHWPerfCB->ui32Roff && i < ui32ArraySize;
		 i++)
	{
		SGXMKIF_HWPERF_CB_ENTRY *psMKPerfEntry = &psHWPerfCB->psHWPerfCBData[psHWPerfCB->ui32Roff];

		psClientHWPerfEntry[i].ui32FrameNo = psMKPerfEntry->ui32FrameNo;
		psClientHWPerfEntry[i].ui32PID = psMKPerfEntry->ui32PID;
		psClientHWPerfEntry[i].ui32RTData = psMKPerfEntry->ui32RTData;
		psClientHWPerfEntry[i].ui32Type = psMKPerfEntry->ui32Type;
		psClientHWPerfEntry[i].ui32Ordinal	= psMKPerfEntry->ui32Ordinal;
		psClientHWPerfEntry[i].ui32Info	= psMKPerfEntry->ui32Info;
		psClientHWPerfEntry[i].ui32Clocksx16 = SGXConvertTimeStamp(psDevInfo,
													psMKPerfEntry->ui32TimeWraps,
													psMKPerfEntry->ui32Time);
		OSMemCopy(&psClientHWPerfEntry[i].ui32Counters[0][0],
				  &psMKPerfEntry->ui32Counters[0][0],
				  sizeof(psMKPerfEntry->ui32Counters));

		OSMemCopy(&psClientHWPerfEntry[i].ui32MiscCounters[0][0],
				  &psMKPerfEntry->ui32MiscCounters[0][0],
				  sizeof(psMKPerfEntry->ui32MiscCounters));

		psHWPerfCB->ui32Roff = (psHWPerfCB->ui32Roff + 1) & (SGXMKIF_HWPERF_CB_SIZE - 1);
	}

	*pui32DataCount = i;
	*pui32ClockSpeed = psDevInfo->ui32CoreClockSpeed;
	*pui32HostTimeStamp = OSClockus();

	return eError;
}


/******************************************************************************
 End of file (sgxinit.c)
******************************************************************************/
