/*************************************************************************/ /*!
@File
@Title          RGX firmware utility routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX firmware utility routines
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

#if !defined(__RGXFWUTILS_H__)
#define __RGXFWUTILS_H__

#include "rgxdevice.h"
#include "rgxccb.h"
#include "devicemem.h"
#include "device.h"
#include "pvr_notifier.h"
#include "pvrsrv.h"
#include "connection_server.h"
#include "rgxta3d.h"
#include "devicemem_utils.h"

#if defined(SUPPORT_TRUSTED_DEVICE)
#include "physmem_tdfwcode.h"
#include "physmem_tdsecbuf.h"
#endif


/*
 * Firmware-only allocation (which are initialised by the host) must be aligned to the SLC cache line size.
 * This is because firmware-only allocations are GPU_CACHE_INCOHERENT and this causes problems
 * if two allocations share the same cache line; e.g. the initialisation of the second allocation won't
 * make it into the SLC cache because it has been already loaded when accessing the content of the first allocation.
 */
static INLINE PVRSRV_ERROR DevmemFwAllocate(PVRSRV_RGXDEV_INFO *psDevInfo,
											IMG_DEVMEM_SIZE_T uiSize,
											DEVMEM_FLAGS_T uiFlags,
						                    IMG_PCHAR pszText,
											DEVMEM_MEMDESC **ppsMemDescPtr)
{
	IMG_DEV_VIRTADDR sTmpDevVAddr;
	PVRSRV_ERROR eError;

	PVR_DPF_ENTERED;

#if defined(PVRSRV_GPUVIRT_GUESTDRV)
	uiFlags |= PVRSRV_MEMALLOCFLAG_UNCACHED;
	uiFlags &= ~PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(FIRMWARE_CACHED);
#endif

	/* Ensure all RI labels begin 'Fw' for the FW heap. */
	PVR_ASSERT((pszText != NULL) && (pszText[0] == 'F') && (pszText[1] == 'w'));

	eError = DevmemAllocate(psDevInfo->psFirmwareHeap,
							uiSize,
							GET_ROGUE_CACHE_LINE_SIZE(psDevInfo->sDevFeatureCfg.ui32CacheLineSize),
							uiFlags | PVRSRV_MEMALLOCFLAG_FW_LOCAL,
							pszText,
							ppsMemDescPtr);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF_RETURN_RC(eError);
	}

	/*
		We need to map it so the heap for this allocation
		is set
	*/
	eError = DevmemMapToDevice(*ppsMemDescPtr,
							   psDevInfo->psFirmwareHeap,
							   &sTmpDevVAddr);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF_RETURN_RC(eError);
	}


	PVR_DPF_RETURN_RC(eError);
}

static INLINE PVRSRV_ERROR DevmemFwAllocateExportable(PVRSRV_DEVICE_NODE *psDeviceNode,
													  IMG_DEVMEM_SIZE_T uiSize,
													  IMG_DEVMEM_ALIGN_T uiAlign,
													  DEVMEM_FLAGS_T uiFlags,
									                  IMG_PCHAR pszText,
													  DEVMEM_MEMDESC **ppsMemDescPtr)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *) psDeviceNode->pvDevice;
	IMG_DEV_VIRTADDR sTmpDevVAddr;
	PVRSRV_ERROR eError;

#if defined(PVRSRV_GPUVIRT_GUESTDRV)
	uiFlags |= PVRSRV_MEMALLOCFLAG_UNCACHED;
	uiFlags &= ~PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(FIRMWARE_CACHED);
#endif

	PVR_DPF_ENTERED;

	PVR_ASSERT((pszText != NULL) &&
			(pszText[0] == 'F') && (pszText[1] == 'w') &&
			(pszText[2] == 'E') && (pszText[3] == 'x'));

	eError = DevmemAllocateExportable(psDeviceNode,
									  uiSize,
									  uiAlign,
									  DevmemGetHeapLog2PageSize(psDevInfo->psFirmwareHeap),
									  uiFlags | PVRSRV_MEMALLOCFLAG_FW_LOCAL,
									  pszText,
									  ppsMemDescPtr);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"FW DevmemAllocateExportable failed (%u)", eError));
		PVR_DPF_RETURN_RC(eError);
	}

	/*
		We need to map it so the heap for this allocation
		is set
	*/
	eError = DevmemMapToDevice(*ppsMemDescPtr,
							   psDevInfo->psFirmwareHeap,
							   &sTmpDevVAddr);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"FW DevmemMapToDevice failed (%u)", eError));
	}

	PVR_DPF_RETURN_RC1(eError, *ppsMemDescPtr);
}

static void DevmemFWPoison(DEVMEM_MEMDESC *psMemDesc, IMG_BYTE ubPoisonValue)
{
	void *pvLinAddr;
	PVRSRV_ERROR eError;

	eError = DevmemAcquireCpuVirtAddr(psMemDesc, &pvLinAddr);

	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to acquire FW allocation mapping "
					"to poison: %s",
							__func__,
							PVRSRVGETERRORSTRING(eError)));
		return;
	}

	OSDeviceMemSet(pvLinAddr, ubPoisonValue, psMemDesc->uiAllocSize);

	DevmemReleaseCpuVirtAddr(psMemDesc);
}

static INLINE void DevmemFwFree(PVRSRV_RGXDEV_INFO *psDevInfo,
								DEVMEM_MEMDESC *psMemDesc)
{
	PVR_DPF_ENTERED1(psMemDesc);

	if(psDevInfo->bEnableFWPoisonOnFree)
	{
		DevmemFWPoison(psMemDesc, psDevInfo->ubFWPoisonOnFreeValue);
	}

	DevmemReleaseDevVirtAddr(psMemDesc);
	DevmemFree(psMemDesc);

	PVR_DPF_RETURN;
}

#if defined(SUPPORT_TRUSTED_DEVICE)
static INLINE
PVRSRV_ERROR DevmemImportTDFWCode(PVRSRV_DEVICE_NODE *psDeviceNode,
                                  IMG_DEVMEM_SIZE_T uiSize,
                                  PMR_LOG2ALIGN_T uiLog2Align,
                                  IMG_UINT32 uiMemAllocFlags,
                                  IMG_BOOL bFWCorememCode,
                                  DEVMEM_MEMDESC **ppsMemDescPtr)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *) psDeviceNode->pvDevice;
	PMR *psTDFWCodePMR;
	IMG_DEV_VIRTADDR sTmpDevVAddr;
	IMG_DEVMEM_SIZE_T uiMemDescSize;
	IMG_DEVMEM_ALIGN_T uiAlign = 1 << uiLog2Align;
	PVRSRV_ERROR eError;

	PVR_ASSERT(ppsMemDescPtr);

	DevmemExportalignAdjustSizeAndAlign(DevmemGetHeapLog2PageSize(psDevInfo->psFirmwareHeap),
	                                    &uiSize,
	                                    &uiAlign);

	eError = PhysmemNewTDFWCodePMR(psDeviceNode,
	                               uiSize,
	                               uiLog2Align,
	                               uiMemAllocFlags,
	                               bFWCorememCode,
	                               &psTDFWCodePMR);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PhysmemNewTDFWCodePMR failed (%u)", eError));
		goto PMRCreateError;
	}

	/* NB: TDFWCodePMR refcount: 1 -> 2 */
	eError = DevmemLocalImport(psDeviceNode,
	                           psTDFWCodePMR,
	                           uiMemAllocFlags,
	                           ppsMemDescPtr,
	                           &uiMemDescSize,
	                           "TDFWCode");
	if(eError != PVRSRV_OK)
	{
		goto ImportError;
	}

	eError = DevmemMapToDevice(*ppsMemDescPtr,
	                           psDevInfo->psFirmwareHeap,
	                           &sTmpDevVAddr);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"Failed to map TD META code PMR (%u)", eError));
		goto MapError;
	}

	/* NB: TDFWCodePMR refcount: 2 -> 1
	 * The PMR will be unreferenced again (and destroyed) when
	 * the memdesc tracking it is cleaned up
	 */
	PMRUnrefPMR(psTDFWCodePMR);

	return PVRSRV_OK;

MapError:
	DevmemFree(*ppsMemDescPtr);
	*ppsMemDescPtr = NULL;
ImportError:
	/* Unref and destroy the PMR */
	PMRUnrefPMR(psTDFWCodePMR);
PMRCreateError:

	return eError;
}

static INLINE
PVRSRV_ERROR DevmemImportTDSecureBuf(PVRSRV_DEVICE_NODE *psDeviceNode,
                                     IMG_DEVMEM_SIZE_T uiSize,
                                     PMR_LOG2ALIGN_T uiLog2Align,
                                     IMG_UINT32 uiMemAllocFlags,
                                     DEVMEM_MEMDESC **ppsMemDescPtr,
                                     IMG_UINT64 *pui64SecBufHandle)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *) psDeviceNode->pvDevice;
	PMR *psTDSecureBufPMR;
	IMG_DEV_VIRTADDR sTmpDevVAddr;
	IMG_DEVMEM_SIZE_T uiMemDescSize;
	IMG_DEVMEM_ALIGN_T uiAlign = 1 << uiLog2Align;
	PVRSRV_ERROR eError;

	PVR_ASSERT(ppsMemDescPtr);

	DevmemExportalignAdjustSizeAndAlign(DevmemGetHeapLog2PageSize(psDevInfo->psFirmwareHeap),
	                                    &uiSize,
	                                    &uiAlign);

	eError = PhysmemNewTDSecureBufPMR(NULL,
	                                  psDeviceNode,
	                                  uiSize,
	                                  uiLog2Align,
	                                  uiMemAllocFlags,
	                                  &psTDSecureBufPMR,
	                                  pui64SecBufHandle);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PhysmemNewTDSecureBufPMR failed (%u)", eError));
		goto PMRCreateError;
	}

	/* NB: psTDSecureBufPMR refcount: 1 -> 2 */
	eError = DevmemLocalImport(psDeviceNode,
	                           psTDSecureBufPMR,
	                           uiMemAllocFlags,
	                           ppsMemDescPtr,
	                           &uiMemDescSize,
	                           "TDSecureBuffer");
	if(eError != PVRSRV_OK)
	{
		goto ImportError;
	}

	eError = DevmemMapToDevice(*ppsMemDescPtr,
	                           psDevInfo->psFirmwareHeap,
	                           &sTmpDevVAddr);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"Failed to map TD secure buffer PMR (%u)", eError));
		goto MapError;
	}

	/* NB: psTDSecureBufPMR refcount: 2 -> 1
	 * The PMR will be unreferenced again (and destroyed) when
	 * the memdesc tracking it is cleaned up
	 */
	PMRUnrefPMR(psTDSecureBufPMR);

	return PVRSRV_OK;

MapError:
	DevmemFree(*ppsMemDescPtr);
	*ppsMemDescPtr = NULL;
ImportError:
	/* Unref and destroy the PMR */
	PMRUnrefPMR(psTDSecureBufPMR);
PMRCreateError:

	return eError;
}
#endif


/*
 * This function returns the value of the hardware register RGX_CR_TIMER
 * which is a timer counting in ticks.
 */

static INLINE IMG_UINT64 RGXReadHWTimerReg(PVRSRV_RGXDEV_INFO *psDevInfo)
{
    IMG_UINT64  ui64Time = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_TIMER);

    /*
     *  In order to avoid having to issue three 32-bit reads to detect the
     *  lower 32-bits wrapping, the MSB of the low 32-bit word is duplicated
     *  in the MSB of the high 32-bit word. If the wrap happens, we just read
     *  the register again (it will not wrap again so soon).
     */
    if ((ui64Time ^ (ui64Time << 32)) & ~RGX_CR_TIMER_BIT31_CLRMSK)
    {
        ui64Time = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_TIMER);
    }

    return ((ui64Time & ~RGX_CR_TIMER_VALUE_CLRMSK)	>> RGX_CR_TIMER_VALUE_SHIFT);
}

/*
 * This FW Common Context is only mapped into kernel for initialisation and cleanup purposes.
 * Otherwise this allocation is only used by the FW.
 * Therefore the GPU cache doesn't need coherency,
 * and write-combine is suffice on the CPU side (WC buffer will be flushed at the first kick)
 */
#define RGX_FWCOMCTX_ALLOCFLAGS	(PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) | \
								 PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(FIRMWARE_CACHED)| \
								 PVRSRV_MEMALLOCFLAG_GPU_READABLE | \
								 PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE | \
								 PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT | \
								 PVRSRV_MEMALLOCFLAG_CPU_READABLE | \
								 PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE | \
								 PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE | \
								 PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE | \
								 PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC)

/******************************************************************************
 * RGXSetFirmwareAddress Flags
 *****************************************************************************/
#define RFW_FWADDR_FLAG_NONE		(0)			/*!< Void flag */
#define RFW_FWADDR_NOREF_FLAG		(1U << 0)	/*!< It is safe to immediately release the reference to the pointer, 
												  otherwise RGXUnsetFirmwareAddress() must be call when finished. */

IMG_BOOL RGXTraceBufferIsInitRequired(PVRSRV_RGXDEV_INFO *psDevInfo);
PVRSRV_ERROR RGXTraceBufferInitOnDemandResources(PVRSRV_RGXDEV_INFO *psDevInfo);

PVRSRV_ERROR RGXSetupFirmware(PVRSRV_DEVICE_NODE       *psDeviceNode,
                              IMG_BOOL                 bEnableSignatureChecks,
                              IMG_UINT32               ui32SignatureChecksBufSize,
                              IMG_UINT32               ui32HWPerfFWBufSizeKB,
                              IMG_UINT64               ui64HWPerfFilter,
                              IMG_UINT32               ui32RGXFWAlignChecksArrLength,
                              IMG_UINT32               *pui32RGXFWAlignChecks,
                              IMG_UINT32               ui32ConfigFlags,
                              IMG_UINT32               ui32LogType,
                              RGXFWIF_BIFTILINGMODE    eBifTilingMode,
                              IMG_UINT32               ui32NumTilingCfgs,
                              IMG_UINT32               *pui32BIFTilingXStrides,
                              IMG_UINT32               ui32FilterFlags,
                              IMG_UINT32               ui32JonesDisableMask,
                              IMG_UINT32               ui32HWRDebugDumpLimit,
                              IMG_UINT32               ui32HWPerfCountersDataSize,
                              PMR                      **ppsHWPerfPMR,
                              RGXFWIF_DEV_VIRTADDR     *psRGXFWInitFWAddr,
                              RGX_RD_POWER_ISLAND_CONF eRGXRDPowerIslandConf,
                              FW_PERF_CONF             eFirmwarePerf);



void RGXFreeFirmware(PVRSRV_RGXDEV_INFO 	*psDevInfo);

/*************************************************************************/ /*!
@Function       RGXSetFirmwareAddress

@Description    Sets a pointer in a firmware data structure.

@Input          ppDest		 Address of the pointer to set
@Input          psSrc		 MemDesc describing the pointer
@Input          ui32Flags	 Any combination of  RFW_FWADDR_*_FLAG

@Return			void
*/ /**************************************************************************/
void RGXSetFirmwareAddress(RGXFWIF_DEV_VIRTADDR	*ppDest,
						   DEVMEM_MEMDESC		*psSrc,
						   IMG_UINT32			uiOffset,
						   IMG_UINT32			ui32Flags);


/*************************************************************************/ /*!
@Function       RGXSetMetaDMAAddress

@Description    Fills a Firmware structure used to setup the Meta DMA with two
                pointers to the same data, one on 40 bit and one on 32 bit
                (pointer in the FW memory space).

@Input          ppDest		 	Address of the structure to set
@Input          psSrcMemDesc	MemDesc describing the pointer
@Input			psSrcFWDevVAddr Firmware memory space pointer

@Return			void
*/ /**************************************************************************/
void RGXSetMetaDMAAddress(RGXFWIF_DMA_ADDR		*psDest,
						  DEVMEM_MEMDESC		*psSrcMemDesc,
						  RGXFWIF_DEV_VIRTADDR	*psSrcFWDevVAddr,
						  IMG_UINT32			uiOffset);


/*************************************************************************/ /*!
@Function       RGXUnsetFirmwareAddress

@Description    Unsets a pointer in a firmware data structure

@Input          psSrc		 MemDesc describing the pointer

@Return			void
*/ /**************************************************************************/
void RGXUnsetFirmwareAddress(DEVMEM_MEMDESC			*psSrc);

/*************************************************************************/ /*!
@Function       FWCommonContextAllocate

@Description    Allocate a FW common context. This allocates the HW memory
                for the context, the CCB and wires it all together.

@Input          psConnection            Connection this context is being created on
@Input          psDeviceNode		    Device node to create the FW context on
                                        (must be RGX device node)
@Input          eRGXCCBRequestor        RGX_CCB_REQUESTOR_TYPE enum constant which
                                        which represents the requestor of this FWCC
@Input          eDM                     Data Master type
@Input          psAllocatedMemDesc      Pointer to pre-allocated MemDesc to use
                                        as the FW context or NULL if this function
                                        should allocate it
@Input          ui32AllocatedOffset     Offset into pre-allocate MemDesc to use
                                        as the FW context. If psAllocatedMemDesc
                                        is NULL then this parameter is ignored
@Input          psFWMemContextMemDesc   MemDesc of the FW memory context this
                                        common context resides on
@Input          psContextStateMemDesc   FW context state (context switch) MemDesc
@Input          ui32CCBAllocSize        Size of the CCB for this context
@Input          ui32Priority            Priority of the context
@Input          psInfo                  Structure that contains extra info
                                        required for the creation of the context
                                        (elements might change from core to core)
@Return			PVRSRV_OK if the context was successfully created
*/ /**************************************************************************/
PVRSRV_ERROR FWCommonContextAllocate(CONNECTION_DATA *psConnection,
									 PVRSRV_DEVICE_NODE *psDeviceNode,
									 RGX_CCB_REQUESTOR_TYPE eRGXCCBRequestor,
									 RGXFWIF_DM eDM,
									 DEVMEM_MEMDESC *psAllocatedMemDesc,
									 IMG_UINT32 ui32AllocatedOffset,
									 DEVMEM_MEMDESC *psFWMemContextMemDesc,
									 DEVMEM_MEMDESC *psContextStateMemDesc,
									 IMG_UINT32 ui32CCBAllocSize,
									 IMG_UINT32 ui32Priority,
									 RGX_COMMON_CONTEXT_INFO *psInfo,
									 RGX_SERVER_COMMON_CONTEXT **ppsServerCommonContext);

									 

void FWCommonContextFree(RGX_SERVER_COMMON_CONTEXT *psServerCommonContext);

PRGXFWIF_FWCOMMONCONTEXT FWCommonContextGetFWAddress(RGX_SERVER_COMMON_CONTEXT *psServerCommonContext);

RGX_CLIENT_CCB *FWCommonContextGetClientCCB(RGX_SERVER_COMMON_CONTEXT *psServerCommonContext);

RGXFWIF_CONTEXT_RESET_REASON FWCommonContextGetLastResetReason(RGX_SERVER_COMMON_CONTEXT *psServerCommonContext,
                                                               IMG_UINT32 *pui32LastResetJobRef);

/*!
******************************************************************************

 @Function	RGXScheduleProcessQueuesKM

 @Description - Software command complete handler
				(sends uncounted kicks for all the DMs through the MISR)

 @Input hCmdCompHandle - RGX device node

******************************************************************************/
IMG_IMPORT
void RGXScheduleProcessQueuesKM(PVRSRV_CMDCOMP_HANDLE hCmdCompHandle);

/*!
******************************************************************************

 @Function	RGXInstallProcessQueuesMISR

 @Description - Installs the MISR to handle Process Queues operations

 @Input phMISR - Pointer to the MISR handler

 @Input psDeviceNode - RGX Device node

******************************************************************************/
IMG_IMPORT
PVRSRV_ERROR RGXInstallProcessQueuesMISR(IMG_HANDLE *phMISR, PVRSRV_DEVICE_NODE *psDeviceNode);

/*************************************************************************/ /*!
@Function       RGXSendCommandWithPowLock

@Description    Sends a command to a particular DM without honouring
				pending cache operations but taking the power lock.

@Input          psDevInfo			Device Info
@Input          eDM				To which DM the cmd is sent.
@Input          psKCCBCmd			The cmd to send.
@Input          ui32CmdSize			The cmd size.
@Input          ui32PDumpFlags			Pdump flags

@Return			PVRSRV_ERROR
*/ /**************************************************************************/
PVRSRV_ERROR RGXSendCommandWithPowLock(PVRSRV_RGXDEV_INFO 	*psDevInfo,
										RGXFWIF_DM			eKCCBType,
									 	RGXFWIF_KCCB_CMD	*psKCCBCmd,
									 	IMG_UINT32			ui32CmdSize,
									 	IMG_UINT32			ui32PDumpFlags);

/*************************************************************************/ /*!
@Function       RGXSendCommand

@Description    Sends a command to a particular DM without honouring
				pending cache operations or the power lock. 
                The function flushes any deferred KCCB commands first.

@Input          psDevInfo			Device Info
@Input          eDM				To which DM the cmd is sent.
@Input          psKCCBCmd			The cmd to send.
@Input          ui32CmdSize			The cmd size.
@Input          uiPdumpFlags			PDump flags.

@Return			PVRSRV_ERROR
*/ /**************************************************************************/
PVRSRV_ERROR RGXSendCommand(PVRSRV_RGXDEV_INFO 	*psDevInfo,
								 RGXFWIF_DM		eKCCBType,
								 RGXFWIF_KCCB_CMD	*psKCCBCmd,
								 IMG_UINT32		ui32CmdSize,
								 PDUMP_FLAGS_T		uiPdumpFlags);


/*************************************************************************/ /*!
@Function       RGXScheduleCommand

@Description    Sends a command to a particular DM

@Input          psDevInfo			Device Info
@Input          eDM				To which DM the cmd is sent.
@Input          psKCCBCmd			The cmd to send.
@Input          ui32CmdSize			The cmd size.
@Input          ui32CacheOpFence		Pending cache op. fence value.
@Input          ui32PDumpFlags			PDump flags

@Return			PVRSRV_ERROR
*/ /**************************************************************************/
PVRSRV_ERROR RGXScheduleCommand(PVRSRV_RGXDEV_INFO 	*psDevInfo,
								RGXFWIF_DM		eKCCBType,
								RGXFWIF_KCCB_CMD	*psKCCBCmd,
								IMG_UINT32		ui32CmdSize,
								IMG_UINT32		ui32CacheOpFence,
								IMG_UINT32 		ui32PDumpFlags);

/*************************************************************************/ /*!
@Function       RGXScheduleCommandAndWait

@Description    Schedules the command with RGXScheduleCommand and then waits 
				for the FW to update a sync. The sync must be piggy backed on
				the cmd, either by passing a sync cmd or a cmd that contains the
				sync which the FW will eventually update. The sync is created in
				the function, therefore the function provides a FWAddr and 
				UpdateValue for that cmd.

@Input          psDevInfo			Device Info
@Input          eDM				To which DM the cmd is sent.
@Input          psKCCBCmd			The cmd to send.
@Input          ui32CmdSize			The cmd size.
@Input          puiSyncObjFWAddr	Pointer to the location with the FWAddr of 
									the sync.
@Input          puiUpdateValue		Pointer to the location with the update 
									value of the sync.
@Input          ui32PDumpFlags		PDump flags

@Return			PVRSRV_ERROR
*/ /**************************************************************************/
PVRSRV_ERROR RGXScheduleCommandAndWait(PVRSRV_RGXDEV_INFO 	*psDevInfo,
									   RGXFWIF_DM			eDM,
									   RGXFWIF_KCCB_CMD		*psKCCBCmd,
									   IMG_UINT32			ui32CmdSize,
									   IMG_UINT32			*puiSyncObjDevVAddr,
									   IMG_UINT32			*puiUpdateValue,
									   PVRSRV_CLIENT_SYNC_PRIM 	*psSyncPrim,
									   IMG_UINT32			ui32PDumpFlags);

PVRSRV_ERROR RGXFirmwareUnittests(PVRSRV_RGXDEV_INFO *psDevInfo);


/*! ***********************************************************************//**
@brief          Copy framework command into FW addressable buffer

@param          psFWFrameworkMemDesc
@param          pbyGPUFRegisterList
@param          ui32FrameworkRegisterSize

@returns        PVRSRV_ERROR 
*/ /**************************************************************************/
PVRSRV_ERROR PVRSRVRGXFrameworkCopyCommand(DEVMEM_MEMDESC	*psFWFrameworkMemDesc,
										   IMG_PBYTE		pbyGPUFRegisterList,
										   IMG_UINT32		ui32FrameworkRegisterSize);


/*! ***********************************************************************//**
@brief          Create FW addressable buffer for framework

@param          psDeviceNode
@param          ppsFWFrameworkMemDesc
@param          ui32FrameworkRegisterSize

@returns        PVRSRV_ERROR 
*/ /**************************************************************************/
PVRSRV_ERROR PVRSRVRGXFrameworkCreateKM(PVRSRV_DEVICE_NODE * psDeviceNode,
										DEVMEM_MEMDESC     ** ppsFWFrameworkMemDesc,
										IMG_UINT32         ui32FrameworkRegisterSize);

/*************************************************************************/ /*!
@Function       RGXWaitForFWOp

@Description    Send a sync command and wait to be signalled.

@Input          psDevInfo			Device Info
@Input          eDM				To which DM the cmd is sent.
@Input          ui32PDumpFlags			PDump flags

@Return			void
*/ /**************************************************************************/
PVRSRV_ERROR RGXWaitForFWOp(PVRSRV_RGXDEV_INFO	*psDevInfo,
									RGXFWIF_DM	eDM,
									PVRSRV_CLIENT_SYNC_PRIM *psSyncPrim,
									IMG_UINT32	ui32PDumpFlags);

/*************************************************************************/ /*!
@Function       RGXStateFlagCtrl

@Description    Set and return FW internal state flags.

@Input          psDevInfo       Device Info
@Input          ui32Config      AppHint config flags
@Output         pui32State      Current AppHint state flag configuration
@Input          bSetNotClear    Set or clear the provided config flags

@Return         PVRSRV_ERROR
*/ /**************************************************************************/
PVRSRV_ERROR RGXStateFlagCtrl(PVRSRV_RGXDEV_INFO *psDevInfo,
				IMG_UINT32 ui32Config,
				IMG_UINT32 *pui32State,
				IMG_BOOL bSetNotClear);

/*!
******************************************************************************

 @Function	RGXFWRequestCommonContextCleanUp

 @Description Schedules a FW common context cleanup. The firmware will doesn't
              block waiting for the resource to become idle but rather notifies
              the host that the resources is busy.

 @Input psDeviceNode - pointer to device node

 @Input psFWContext - firmware address of the context to be cleaned up

 @Input eDM - Data master, to which the cleanup command should be send

 @Input ui32PDumpFlags - PDump continuous flag

******************************************************************************/
PVRSRV_ERROR RGXFWRequestCommonContextCleanUp(PVRSRV_DEVICE_NODE *psDeviceNode,
											  RGX_SERVER_COMMON_CONTEXT *psServerCommonContext,
											  PVRSRV_CLIENT_SYNC_PRIM *psSyncPrim,
											  RGXFWIF_DM eDM,
											  IMG_UINT32 ui32PDumpFlags);

/*!
******************************************************************************

 @Function	RGXFWRequestHWRTDataCleanUp

 @Description Schedules a FW HWRTData memory cleanup. The firmware will doesn't
              block waiting for the resource to become idle but rather notifies
              the host that the resources is busy.

 @Input psDeviceNode - pointer to device node

 @Input psHWRTData - firmware address of the HWRTData to be cleaned up

 @Input eDM - Data master, to which the cleanup command should be send

 ******************************************************************************/
PVRSRV_ERROR RGXFWRequestHWRTDataCleanUp(PVRSRV_DEVICE_NODE *psDeviceNode,
										 PRGXFWIF_HWRTDATA psHWRTData,
										 PVRSRV_CLIENT_SYNC_PRIM *psSync,
										 RGXFWIF_DM eDM);

PVRSRV_ERROR RGXFWRequestRayFrameDataCleanUp(PVRSRV_DEVICE_NODE *psDeviceNode,
											 PRGXFWIF_RAY_FRAME_DATA psHWFrameData,
											 PVRSRV_CLIENT_SYNC_PRIM *psSync,
											 RGXFWIF_DM eDM);

/*!
******************************************************************************

 @Function	RGXFWRequestRPMFreeListCleanUp

 @Description Schedules a FW RPM FreeList cleanup. The firmware will doesn't block
              waiting for the resource to become idle but rather notifies the
              host that the resources is busy.

 @Input psDeviceNode - pointer to device node

 @Input psFWRPMFreeList - firmware address of the RPM freelist to be cleaned up

 @Input psSync - Sync object associated with cleanup

 ******************************************************************************/
PVRSRV_ERROR RGXFWRequestRPMFreeListCleanUp(PVRSRV_RGXDEV_INFO *psDevInfo,
											PRGXFWIF_RPM_FREELIST psFWRPMFreeList,
											PVRSRV_CLIENT_SYNC_PRIM *psSync);


/*!
******************************************************************************

 @Function	RGXFWRequestFreeListCleanUp

 @Description Schedules a FW FreeList cleanup. The firmware will doesn't block
              waiting for the resource to become idle but rather notifies the
              host that the resources is busy.

 @Input psDeviceNode - pointer to device node

 @Input psHWRTData - firmware address of the HWRTData to be cleaned up

 @Input eDM - Data master, to which the cleanup command should be send

 ******************************************************************************/
PVRSRV_ERROR RGXFWRequestFreeListCleanUp(PVRSRV_RGXDEV_INFO *psDeviceNode,
										 PRGXFWIF_FREELIST psFWFreeList,
										 PVRSRV_CLIENT_SYNC_PRIM *psSync);

/*!
******************************************************************************

 @Function	RGXFWRequestZSBufferCleanUp

 @Description Schedules a FW ZS Buffer cleanup. The firmware will doesn't block
              waiting for the resource to become idle but rather notifies the
              host that the resources is busy.

 @Input psDeviceNode - pointer to device node

 @Input psFWZSBuffer - firmware address of the ZS Buffer to be cleaned up

 @Input eDM - Data master, to which the cleanup command should be send

 ******************************************************************************/

PVRSRV_ERROR RGXFWRequestZSBufferCleanUp(PVRSRV_RGXDEV_INFO *psDevInfo,
										 PRGXFWIF_ZSBUFFER psFWZSBuffer,
										 PVRSRV_CLIENT_SYNC_PRIM *psSync);

PVRSRV_ERROR ContextSetPriority(RGX_SERVER_COMMON_CONTEXT *psContext,
								CONNECTION_DATA *psConnection,
								PVRSRV_RGXDEV_INFO *psDevInfo,
								IMG_UINT32 ui32Priority,
								RGXFWIF_DM eDM);

/*!
******************************************************************************

 @Function				RGXFWSetHCSDeadline

 @Description			Requests the Firmware to set a new Hard Context
						Switch timeout deadline. Context switches that
						surpass that deadline cause the system to kill
						the currently running workloads.

 @Input psDeviceNode	pointer to device node

 @Input ui32HCSDeadlineMs	The deadline in milliseconds.
 ******************************************************************************/
PVRSRV_ERROR RGXFWSetHCSDeadline(PVRSRV_RGXDEV_INFO *psDevInfo,
								IMG_UINT32 ui32HCSDeadlineMs);

/*!
******************************************************************************

 @Function				RGXFWChangeOSidPriority

 @Description			Requests the Firmware to change the priority of an
						operating system. Higher priority number equals
						higher priority on the scheduling system.

 @Input psDeviceNode	pointer to device node

 @Input ui32OSid		The OSid whose priority is to be altered

 @Input ui32Priority	The new priority number for the specified OSid
 ******************************************************************************/
PVRSRV_ERROR RGXFWChangeOSidPriority(PVRSRV_RGXDEV_INFO *psDevInfo,
									 IMG_UINT32 ui32OSid,
									 IMG_UINT32 ui32Priority);

/*!
****************************************************************************

 @Function				RGXFWSetOSIsolationThreshold

 @Description			Requests the Firmware to change the priority
						threshold of the OS Isolation group. Any OS with a
						priority higher or equal than the threshold is
						considered to be belonging to the isolation group.

 @Input psDeviceNode	pointer to device node

 @Input ui32IsolationPriorityThreshold	The new priority threshold
 ***************************************************************************/
PVRSRV_ERROR RGXFWSetOSIsolationThreshold(PVRSRV_RGXDEV_INFO *psDevInfo,
								IMG_UINT32 ui32IsolationPriorityThreshold);

/*!
****************************************************************************

 @Function              RGXFWOSConfig

 @Description           Sends the OS Init structure to the FW to complete
                        the initialization process. The FW will then set all
                        the OS specific parameters for that DDK

 @Input psDeviceNode    pointer to device node
 ***************************************************************************/
PVRSRV_ERROR RGXFWOSConfig(PVRSRV_RGXDEV_INFO *psDevInfo);

/*!
****************************************************************************

 @Function				RGXFWSetVMOnlineState

 @Description			Requests the Firmware to change the guest OS Online
						states. This should be initiated by the VMM when a
						guest VM comes online or goes offline. If offline,
						the FW offloads any current resource from that OSID.
						The request is repeated until the FW has had time to
						free all the resources or has waited for workloads
						to finish.

 @Input psDeviceNode	pointer to device node

 @Input ui32OSid		The Guest OSid whose state is being altered

 @Input eOSOnlineState	The new state (Online or Offline)
 ***************************************************************************/
PVRSRV_ERROR RGXFWSetVMOnlineState(PVRSRV_RGXDEV_INFO *psDevInfo,
								IMG_UINT32 ui32OSid,
								RGXFWIF_OS_STATE_CHANGE eOSOnlineState);
/*!
******************************************************************************

 @Function	RGXReadMETAAddr

 @Description Reads a value at given address in META memory space
              (it can be either a memory location or a META register)

 @Input psDevInfo - pointer to device info

 @Input ui32METAAddr - address in META memory space

 @Output pui32Value - value

 ******************************************************************************/

PVRSRV_ERROR RGXReadMETAAddr(PVRSRV_RGXDEV_INFO	*psDevInfo,
                             IMG_UINT32 ui32METAAddr,
                             IMG_UINT32 *pui32Value);

/*!
******************************************************************************

 @Function	RGXCheckFirmwareCCB

 @Description Processes all commands that are found in the Firmware CCB.

 @Input psDevInfo - pointer to device

 ******************************************************************************/
void RGXCheckFirmwareCCB(PVRSRV_RGXDEV_INFO *psDevInfo);

/*!
******************************************************************************

 @Function	   RGXUpdateHealthStatus

 @Description  Tests a number of conditions which might indicate a fatal error has
               occurred in the firmware. The result is stored in the device node
               eheathStatus.

 @Input        psDevNode              Pointer to device node structure.
 @Input        bCheckAfterTimePassed  When TRUE, the function will also test for
                                      firmware queues and polls not changing
                                      since the previous test.
                                      
                                      Note: if not enough time has passed since
                                      the last call, false positives may occur.

 @returns      PVRSRV_ERROR 
 ******************************************************************************/
PVRSRV_ERROR RGXUpdateHealthStatus(PVRSRV_DEVICE_NODE* psDevNode,
                                   IMG_BOOL bCheckAfterTimePassed);


PVRSRV_ERROR CheckStalledClientCommonContext(RGX_SERVER_COMMON_CONTEXT *psCurrentServerCommonContext, RGX_KICK_TYPE_DM eKickTypeDM);

void DumpStalledFWCommonContext(RGX_SERVER_COMMON_CONTEXT *psCurrentServerCommonContext,
					DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile);

/*!
******************************************************************************

 @Function	   AttachKickResourcesCleanupCtls

 @Description  Attaches the cleanup structures to a kick command so that
               submission reference counting can be performed when the
               firmware processes the command

 @Output        apsCleanupCtl          Array of CleanupCtl structure pointers to populate.
 @Output        pui32NumCleanupCtl     Number of CleanupCtl structure pointers written out.
 @Input         eDM                    Which data master is the subject of the command.
 @Input         bKick                  TRUE if the client originally wanted to kick this DM.
 @Input         psRTDataCleanup        Optional RTData cleanup associated with the command.
 @Input         psZBuffer              Optional ZBuffer associated with the command.
 @Input         psSBuffer              Optional SBuffer associated with the command.
 ******************************************************************************/
void AttachKickResourcesCleanupCtls(PRGXFWIF_CLEANUP_CTL *apsCleanupCtl,
									IMG_UINT32 *pui32NumCleanupCtl,
									RGXFWIF_DM eDM,
									IMG_BOOL bKick,
									RGX_RTDATA_CLEANUP_DATA        *psRTDataCleanup,
									RGX_ZSBUFFER_DATA              *psZBuffer,
									RGX_ZSBUFFER_DATA              *psSBuffer);

/*!
******************************************************************************

 @Function			RGXResetHWRLogs

 @Description 		Resets the HWR Logs buffer (the hardware recovery count is not reset)

 @Input 			psDevInfo	Pointer to the device

 @Return			PVRSRV_ERROR	PVRSRV_OK on success. Otherwise, a PVRSRV_
                                	error code
 ******************************************************************************/
PVRSRV_ERROR RGXResetHWRLogs(PVRSRV_DEVICE_NODE *psDevNode);


/*!
******************************************************************************

 @Function			RGXGetPhyAddr

 @Description 		Get the physical address of a certain PMR at a certain offset within it

 @Input 			psPMR	    PMR of the allocation

 @Input 			ui32LogicalOffset	    Logical offset

 @Output			psPhyAddr	    Physical address of the allocation

 @Return			PVRSRV_ERROR	PVRSRV_OK on success. Otherwise, a PVRSRV_
									error code
 ******************************************************************************/
PVRSRV_ERROR RGXGetPhyAddr(PMR *psPMR,
						   IMG_DEV_PHYADDR *psPhyAddr,
						   IMG_UINT32 ui32LogicalOffset,
						   IMG_UINT32 ui32Log2PageSize,
						   IMG_UINT32 ui32NumOfPages,
						   IMG_BOOL *bValid);

#if defined(PDUMP)
/*!
******************************************************************************

 @Function                      RGXPdumpDrainKCCB

 @Description                   Wait for the firmware to execute all the commands in the kCCB

 @Input                         psDevInfo	Pointer to the device

 @Input                         ui32WriteOffset	  Woff we have to POL for the Roff to be equal to

 @Return                        PVRSRV_ERROR	PVRSRV_OK on success. Otherwise, a PVRSRV_
									error code
 ******************************************************************************/
PVRSRV_ERROR RGXPdumpDrainKCCB(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32WriteOffset);
#endif /* PDUMP */


#endif /* __RGXFWUTILS_H__ */
/******************************************************************************
 End of file (rgxfwutils.h)
******************************************************************************/
