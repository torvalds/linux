/*************************************************************************/ /*!
@File
@Title          Rgx debug information
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX debugging functions
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
//#define PVR_DPF_FUNCTION_TRACE_ON 1
#undef PVR_DPF_FUNCTION_TRACE_ON

#include "img_defs.h"
#include "rgxdefs_km.h"
#include "rgxdevice.h"
#include "rgxmem.h"
#include "allocmem.h"
#include "cache_km.h"
#include "osfunc.h"

#include "rgxdebug.h"
#include "pvrversion.h"
#include "pvr_debug.h"
#include "srvkm.h"
#include "rgxutils.h"
#include "tlstream.h"
#include "rgxfwutils.h"
#include "pvrsrv.h"
#include "services_km.h"

#include "devicemem.h"
#include "devicemem_pdump.h"
#include "devicemem_utils.h"
#include "rgx_fwif_km.h"
#include "rgx_fwif_sf.h"
#include "rgxfw_log_helper.h"
#include "fwtrace_string.h"
#include "rgxfwimageutils.h"
#include "fwload.h"
#include "debug_common.h"

#include "rgxta3d.h"
#if defined(SUPPORT_RGXKICKSYNC_BRIDGE)
#include "rgxkicksync.h"
#endif
#include "rgxcompute.h"
#include "rgxtdmtransfer.h"
#include "rgxtimecorr.h"
#include "rgx_options.h"
#include "rgxinit.h"
#include "rgxlayer_impl.h"
#include "devicemem_history_server.h"
#include "info_page.h"

#define PVR_DUMP_FIRMWARE_INFO(x)														\
	PVR_DUMPDEBUG_LOG("FW info: %d.%d @ %8d (%s) build options: 0x%08x",				\
						PVRVERSION_UNPACK_MAJ((x).ui32DDKVersion),						\
						PVRVERSION_UNPACK_MIN((x).ui32DDKVersion),						\
						(x).ui32DDKBuild,												\
						((x).ui32BuildOptions & OPTIONS_DEBUG_EN) ? "debug":"release",	\
						(x).ui32BuildOptions);

#define DD_SUMMARY_INDENT  ""
#define DD_NORMAL_INDENT   "    "

#define RGX_DEBUG_STR_SIZE			(150U)
#define MAX_FW_DESCRIPTION_LENGTH	(600U)


#define RGX_TEXAS_BIF0_ID				(0)
#define RGX_TEXAS_BIF1_ID				(1)

/*
 *  The first 7 or 8 cat bases are memory contexts used for PM
 *  or firmware. The rest are application contexts. The numbering
 *  is zero-based.
 */
#if defined(SUPPORT_TRUSTED_DEVICE)
#define MAX_RESERVED_FW_MMU_CONTEXT		(7)
#else
#define MAX_RESERVED_FW_MMU_CONTEXT		(6)
#endif

static const IMG_CHAR *const pszPowStateName[] =
{
#define X(NAME)	#NAME,
	RGXFWIF_POW_STATES
#undef X
};

static const IMG_CHAR * const apszFwOsStateName[RGXFW_CONNECTION_FW_STATE_COUNT] =
{
	"offline",
	"ready",
	"active",
	"offloading"
};

#if defined(PVR_ENABLE_PHR)
static const IMG_FLAGS2DESC asPHRConfig2Description[] =
{
	{BIT_ULL(RGXFWIF_PHR_MODE_OFF), "off"},
	{BIT_ULL(RGXFWIF_PHR_MODE_RD_RESET), "reset RD hardware"},
	{BIT_ULL(RGXFWIF_PHR_MODE_FULL_RESET), "full gpu reset "},
};
#endif

#if !defined(NO_HARDWARE)
static PVRSRV_ERROR
RGXPollMetaRegThroughSP(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32RegOffset,
                        IMG_UINT32 ui32PollValue, IMG_UINT32 ui32Mask)
{
	IMG_UINT32 ui32RegValue, ui32NumPolls = 0;
	PVRSRV_ERROR eError;

	do
	{
		eError = RGXReadFWModuleAddr(psDevInfo, ui32RegOffset, &ui32RegValue);
		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	} while (((ui32RegValue & ui32Mask) != ui32PollValue) && (ui32NumPolls++ < 1000));

	return ((ui32RegValue & ui32Mask) == ui32PollValue) ? PVRSRV_OK : PVRSRV_ERROR_RETRY;
}

static PVRSRV_ERROR
RGXReadMetaCoreReg(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32RegAddr, IMG_UINT32 *pui32RegVal)
{
	PVRSRV_ERROR eError;

	/* Core Read Ready? */
	eError = RGXPollMetaRegThroughSP(psDevInfo,
	                                 META_CR_TXUXXRXRQ_OFFSET,
	                                 META_CR_TXUXXRXRQ_DREADY_BIT,
									 META_CR_TXUXXRXRQ_DREADY_BIT);
	PVR_LOG_RETURN_IF_ERROR(eError, "RGXPollMetaRegThroughSP");

	/* Set the reg we are interested in reading */
	eError = RGXWriteFWModuleAddr(psDevInfo, META_CR_TXUXXRXRQ_OFFSET,
	                        ui32RegAddr | META_CR_TXUXXRXRQ_RDnWR_BIT);
	PVR_LOG_RETURN_IF_ERROR(eError, "RGXWriteFWModuleAddr");

	/* Core Read Done? */
	eError = RGXPollMetaRegThroughSP(psDevInfo,
	                                 META_CR_TXUXXRXRQ_OFFSET,
	                                 META_CR_TXUXXRXRQ_DREADY_BIT,
									 META_CR_TXUXXRXRQ_DREADY_BIT);
	PVR_LOG_RETURN_IF_ERROR(eError, "RGXPollMetaRegThroughSP");

	/* Read the value */
	return RGXReadFWModuleAddr(psDevInfo, META_CR_TXUXXRXDT_OFFSET, pui32RegVal);
}
#endif

#if !defined(NO_HARDWARE) && !defined(SUPPORT_TRUSTED_DEVICE)
static PVRSRV_ERROR _ValidateWithFWModule(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
						void *pvDumpDebugFile,
						PVRSRV_RGXDEV_INFO *psDevInfo,
						RGXFWIF_DEV_VIRTADDR *psFWAddr,
						void *pvHostCodeAddr,
						IMG_UINT32 ui32MaxLen,
						const IMG_CHAR *pszDesc,
						IMG_UINT32 ui32StartOffset)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 ui32Value = 0;
	IMG_UINT32 ui32FWCodeDevVAAddr = psFWAddr->ui32Addr + ui32StartOffset;
	IMG_UINT32 *pui32FWCode = (IMG_PUINT32) ((IMG_PBYTE)pvHostCodeAddr + ui32StartOffset);
	IMG_UINT32 i;

#if defined(EMULATOR)
	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, RISCV_FW_PROCESSOR))
	{
		return PVRSRV_OK;
	}
#endif

	ui32MaxLen -= ui32StartOffset;
	ui32MaxLen /= sizeof(IMG_UINT32); /* Byte -> 32 bit words */

	for (i = 0; i < ui32MaxLen; i++)
	{
		eError = RGXReadFWModuleAddr(psDevInfo, ui32FWCodeDevVAAddr, &ui32Value);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: %s", __func__, PVRSRVGetErrorString(eError)));
			return eError;
		}

#if defined(EMULATOR)
		if (!RGX_IS_FEATURE_SUPPORTED(psDevInfo, RISCV_FW_PROCESSOR))
#endif
		{
			PVR_DPF((PVR_DBG_VERBOSE, "0x%x: CPU 0x%08x, FW 0x%08x", i * 4, pui32FWCode[i], ui32Value));

			if (pui32FWCode[i] != ui32Value)
			{
				PVR_DUMPDEBUG_LOG("%s: Mismatch while validating %s at offset 0x%x: CPU 0x%08x (%p), FW 0x%08x (%x)",
					 __func__, pszDesc,
					 (i * 4) + ui32StartOffset, pui32FWCode[i], pui32FWCode, ui32Value, ui32FWCodeDevVAAddr);
				return PVRSRV_ERROR_FW_IMAGE_MISMATCH;
			}
		}

		ui32FWCodeDevVAAddr += 4;
	}

	PVR_DUMPDEBUG_LOG("Match between Host and Firmware view of the %s", pszDesc);
	return PVRSRV_OK;
}
#endif

#if !defined(NO_HARDWARE)
static PVRSRV_ERROR _ValidateFWImage(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
						void *pvDumpDebugFile,
						PVRSRV_RGXDEV_INFO *psDevInfo)
{
#if !defined(NO_HARDWARE) && !defined(SUPPORT_TRUSTED_DEVICE)
	PVRSRV_ERROR eError;
	IMG_UINT32 *pui32HostFWCode = NULL, *pui32HostFWCoremem = NULL;
	OS_FW_IMAGE *psRGXFW = NULL;
	const IMG_BYTE *pbRGXFirmware = NULL;
	RGXFWIF_DEV_VIRTADDR sFWAddr;
	IMG_UINT32 ui32StartOffset = 0;
	RGX_LAYER_PARAMS sLayerParams;
	sLayerParams.psDevInfo = psDevInfo;

#if defined(EMULATOR)
	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, RISCV_FW_PROCESSOR))
	{
		PVR_DUMPDEBUG_LOG("Validation of RISC-V FW code is disabled on emulator");
		return PVRSRV_OK;
	}
#endif

	if (psDevInfo->pvRegsBaseKM == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: RGX registers not mapped yet!", __func__));
		return PVRSRV_ERROR_BAD_MAPPING;
	}

	/* Load FW from system for code verification */
	pui32HostFWCode = OSAllocZMem(psDevInfo->ui32FWCodeSizeInBytes);
	if (pui32HostFWCode == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Failed in allocating memory for FW code. "
				"So skipping FW code verification",
				__func__));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	if (psDevInfo->ui32FWCorememCodeSizeInBytes)
	{
		pui32HostFWCoremem = OSAllocZMem(psDevInfo->ui32FWCorememCodeSizeInBytes);
		if (pui32HostFWCoremem == NULL)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: Failed in allocating memory for FW core code. "
					"So skipping FW code verification",
					__func__));
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto freeHostFWCode;
		}
	}

	/* Load FW image */
	eError = RGXLoadAndGetFWData(psDevInfo->psDeviceNode, &psRGXFW, &pbRGXFirmware);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to load FW image file (%s).",
		         __func__, PVRSRVGetErrorString(eError)));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto cleanup_initfw;
	}

	if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META))
	{
		eError = ProcessLDRCommandStream(&sLayerParams, pbRGXFirmware,
						(void*) pui32HostFWCode, NULL,
						(void*) pui32HostFWCoremem, NULL, NULL);
	}
	else if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, RISCV_FW_PROCESSOR))
	{
		eError = ProcessELFCommandStream(&sLayerParams, pbRGXFirmware,
		                                 pui32HostFWCode, NULL,
		                                 pui32HostFWCoremem, NULL);
	}

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed in parsing FW image file.", __func__));
		goto cleanup_initfw;
	}

	if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META))
	{
		/* starting checking after BOOT LOADER config */
		sFWAddr.ui32Addr = RGXFW_BOOTLDR_META_ADDR;

		ui32StartOffset = RGXFW_MAX_BOOTLDR_OFFSET;
	}
	else
	{
		/* Use bootloader code remap which is always configured before the FW is started */
		sFWAddr.ui32Addr = RGXRISCVFW_BOOTLDR_CODE_BASE;
	}

	eError = _ValidateWithFWModule(pfnDumpDebugPrintf, pvDumpDebugFile,
					psDevInfo, &sFWAddr,
					pui32HostFWCode, psDevInfo->ui32FWCodeSizeInBytes,
					"FW code", ui32StartOffset);
	if (eError != PVRSRV_OK)
	{
		goto cleanup_initfw;
	}

	if (psDevInfo->ui32FWCorememCodeSizeInBytes)
	{
		if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META))
		{
			sFWAddr.ui32Addr = RGXGetFWImageSectionAddress(NULL, META_COREMEM_CODE);
		}
		else
		{
			sFWAddr.ui32Addr = RGXGetFWImageSectionAddress(NULL, RISCV_COREMEM_CODE);

			/* Core must be halted while issuing abstract commands */
			eError = RGXRiscvHalt(psDevInfo);
			PVR_GOTO_IF_ERROR(eError, cleanup_initfw);
		}

		eError = _ValidateWithFWModule(pfnDumpDebugPrintf, pvDumpDebugFile,
						psDevInfo, &sFWAddr,
						pui32HostFWCoremem, psDevInfo->ui32FWCorememCodeSizeInBytes,
						"FW coremem code", 0);

		if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, RISCV_FW_PROCESSOR))
		{
			eError = RGXRiscvResume(psDevInfo);
			PVR_GOTO_IF_ERROR(eError, cleanup_initfw);
		}
	}

cleanup_initfw:
	if (psRGXFW)
	{
		OSUnloadFirmware(psRGXFW);
	}

	if (pui32HostFWCoremem)
	{
		OSFreeMem(pui32HostFWCoremem);
	}
freeHostFWCode:
	if (pui32HostFWCode)
	{
		OSFreeMem(pui32HostFWCode);
	}
	return eError;
#else
	PVR_UNREFERENCED_PARAMETER(pfnDumpDebugPrintf);
	PVR_UNREFERENCED_PARAMETER(pvDumpDebugFile);
	PVR_UNREFERENCED_PARAMETER(psDevInfo);
	return PVRSRV_OK;
#endif
}
#endif /* !defined(NO_HARDWARE) */

#if defined(SUPPORT_FW_VIEW_EXTRA_DEBUG)
PVRSRV_ERROR ValidateFWOnLoad(PVRSRV_RGXDEV_INFO *psDevInfo)
{
#if !defined(NO_HARDWARE) && !defined(SUPPORT_TRUSTED_DEVICE)
	IMG_PBYTE pbCodeMemoryPointer;
	PVRSRV_ERROR eError;
	RGXFWIF_DEV_VIRTADDR sFWAddr;

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWCodeMemDesc, (void **)&pbCodeMemoryPointer);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META))
	{
		sFWAddr.ui32Addr = RGXFW_BOOTLDR_META_ADDR;
	}
	else
	{
		PVR_ASSERT(RGX_IS_FEATURE_SUPPORTED(psDevInfo, RISCV_FW_PROCESSOR));
		sFWAddr.ui32Addr = RGXRISCVFW_BOOTLDR_CODE_BASE;
	};

	eError = _ValidateWithFWModule(NULL, NULL, psDevInfo, &sFWAddr, pbCodeMemoryPointer, psDevInfo->ui32FWCodeSizeInBytes, "FW code", 0);
	if (eError != PVRSRV_OK)
	{
		goto releaseFWCodeMapping;
	}

	if (psDevInfo->ui32FWCorememCodeSizeInBytes)
	{
		eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWCorememCodeMemDesc, (void **)&pbCodeMemoryPointer);
		if (eError != PVRSRV_OK)
		{
			goto releaseFWCoreCodeMapping;
		}

		if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META))
		{
			sFWAddr.ui32Addr = RGXGetFWImageSectionAddress(NULL, META_COREMEM_CODE);
		}
		else
		{
			PVR_ASSERT(RGX_IS_FEATURE_SUPPORTED(psDevInfo, RISCV_FW_PROCESSOR));
			sFWAddr.ui32Addr = RGXGetFWImageSectionAddress(NULL, RISCV_COREMEM_CODE);
		}

		eError = _ValidateWithFWModule(NULL, NULL, psDevInfo, &sFWAddr, pbCodeMemoryPointer,
						psDevInfo->ui32FWCorememCodeSizeInBytes, "FW coremem code", 0);
	}

releaseFWCoreCodeMapping:
	if (psDevInfo->ui32FWCorememCodeSizeInBytes)
	{
		DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWCorememCodeMemDesc);
	}
releaseFWCodeMapping:
	DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWCodeMemDesc);

	return eError;
#else
	PVR_UNREFERENCED_PARAMETER(psDevInfo);
	return PVRSRV_OK;
#endif
}
#endif


/*!
*******************************************************************************

 @Function	_RGXDecodeMMULevel

 @Description

 Return the name for the MMU level that faulted.

 @Input ui32MMULevel	 - MMU level

 @Return   IMG_CHAR* to the sting describing the MMU level that faulted.

******************************************************************************/
static const IMG_CHAR* _RGXDecodeMMULevel(IMG_UINT32 ui32MMULevel)
{
	const IMG_CHAR* pszMMULevel = "";

	switch (ui32MMULevel)
	{
		case 0x0: pszMMULevel = " (Page Table)"; break;
		case 0x1: pszMMULevel = " (Page Directory)"; break;
		case 0x2: pszMMULevel = " (Page Catalog)"; break;
		case 0x3: pszMMULevel = " (Cat Base Reg)"; break;
	}

	return pszMMULevel;
}


/*!
*******************************************************************************

 @Function	_RGXDecodeMMUReqTags

 @Description

 Decodes the MMU Tag ID and Sideband data fields from RGX_CR_MMU_FAULT_META_STATUS and
 RGX_CR_MMU_FAULT_STATUS regs.

 @Input ui32TagID           - Tag ID value
 @Input ui32BIFModule       - BIF module
 @Input bRead               - Read flag
 @Input bWriteBack          - Write Back flag
 @Output ppszTagID          - Decoded string from the Tag ID
 @Output ppszTagSB          - Decoded string from the Tag SB
 @Output pszScratchBuf      - Buffer provided to the function to generate the debug strings
 @Input ui32ScratchBufSize  - Size of the provided buffer

 @Return   void

******************************************************************************/
#define RGX_TEXAS_BIF0_MCU_L1_TAG_LAST__SERIES8		(12)
#define RGX_TEXAS_BIF0_TAG_IPFID_ARRAY_FIRST__SERIES8	(15)
#define RGX_TEXAS_BIF0_MCU_L1_TAG_LAST__ALBIORIX		(6)
#define RGX_TEXAS_BIF0_TAG_IPFID_ARRAY_FIRST__ALBIORIX	(9)
#define RGX_TEXAS_BIF0_TAG_IPFID_ARRAY_LAST	(33)
#define RGX_TEXAS_BIF0_TAG_RTU_RAC_FIRST	(41)
#define RGX_TEXAS_BIF0_TAG_RTU_RAC_LAST		(48)
#define RGX_TEXAS_BIF0_TAG_LAST				(51)

#define RGX_TEXAS_BIF1_TAG_LAST				(26)

#define RGX_JONES_BIF_IPP_TAG				(0)
#define RGX_JONES_BIF_DCE_TAG_FIRST			(1)
#define RGX_JONES_BIF_DCE_TAG_LAST			(14)
#define RGX_JONES_BIF_TDM_TAG_FIRST			(15)
#define RGX_JONES_BIF_TDM_TAG_LAST			(19)
#define RGX_JONES_BIF_PM_TAG				(20)
#define RGX_JONES_BIF_CDM_TAG_FIRST			(21)
#define RGX_JONES_BIF_CDM_TAG_LAST			(31)
#define RGX_JONES_BIF_META_TAG				(32)
#define RGX_JONES_BIF_META_DMA_TAG			(33)
#define RGX_JONES_BIF_TE_TAG_FIRST			(34)
#define RGX_JONES_BIF_TE_TAG_LAST			(47)
#define RGX_JONES_BIF_RTU_TAG_FIRST			(48)
#define RGX_JONES_BIF_RTU_TAG_LAST			(53)
#define RGX_JONES_BIF_RPM_TAG				(54)
#define RGX_JONES_BIF_TAG_LAST				(54)


/* The MCU L1 requestors are common to all Texas BIFs so put them
 * in their own function. */
static INLINE void _RGXDecodeMMUReqMCULevel1(PVRSRV_RGXDEV_INFO    *psDevInfo,
											 IMG_UINT32  ui32TagID,
											 IMG_CHAR    **ppszTagSB)
{
	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, ALBIORIX_TOP_INFRASTRUCTURE))
	{
		switch (ui32TagID)
		{
			case  0: *ppszTagSB = "IP0 PDS"; break;
			case  1: *ppszTagSB = "IP0 Global"; break;
			case  2: *ppszTagSB = "IP1 PDS"; break;
			case  3: *ppszTagSB = "IP1 Global"; break;
			case  4: *ppszTagSB = "IP2 PDS"; break;
			case  5: *ppszTagSB = "IP2 Global"; break;
		}
	}
	else
	{
		switch (ui32TagID)
		{
			case  0: *ppszTagSB = "IP0 PDS"; break;
			case  1: *ppszTagSB = "IP0 Global"; break;
			case  2: *ppszTagSB = "IP0 BSC"; break;
			case  3: *ppszTagSB = "IP0 Constants"; break;

			case  4: *ppszTagSB = "IP1 PDS"; break;
			case  5: *ppszTagSB = "IP1 Global"; break;
			case  6: *ppszTagSB = "IP1 BSC"; break;
			case  7: *ppszTagSB = "IP1 Constants"; break;

			case  8: *ppszTagSB = "IP2 PDS"; break;
			case  9: *ppszTagSB = "IP2 Global"; break;
			case 10: *ppszTagSB = "IP2 BSC"; break;
			case 11: *ppszTagSB = "IP2 Constants"; break;
		}
	}
}

static void _RGXDecodeMMUReqTags(PVRSRV_RGXDEV_INFO    *psDevInfo,
								 IMG_UINT32  ui32TagID,
								 IMG_UINT32  ui32BIFModule,
								 IMG_BOOL    bRead,
								 IMG_BOOL    bWriteBack,
								 IMG_BOOL    bFBMFault,
								 IMG_CHAR    **ppszTagID,
								 IMG_CHAR    **ppszTagSB,
								 IMG_CHAR    *pszScratchBuf,
								 IMG_UINT32  ui32ScratchBufSize)
{
	IMG_UINT32 ui32BIFsPerSPU = 2;
	IMG_CHAR   *pszTagID = "-";
	IMG_CHAR   *pszTagSB = "-";

	PVR_ASSERT(ppszTagID != NULL);
	PVR_ASSERT(ppszTagSB != NULL);

	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, ALBIORIX_TOP_INFRASTRUCTURE))
	{
		ui32BIFsPerSPU = 4;
	}

	if (bFBMFault)
	{
		pszTagID = "FBM";
		if (bWriteBack)
		{
			pszTagSB = "Header/state cache request";
		}
	}
	else if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, NUM_SPU) &&
	         ui32BIFModule <  RGX_GET_FEATURE_VALUE(psDevInfo, NUM_SPU)*ui32BIFsPerSPU)
	{
		if ((ui32BIFModule % ui32BIFsPerSPU) == 0)
		{
			IMG_UINT32 ui32Tag_RGX_TEXAS_BIF0_MCU_L1_TAG_LAST =
				(RGX_IS_FEATURE_SUPPORTED(psDevInfo, ALBIORIX_TOP_INFRASTRUCTURE))
				? RGX_TEXAS_BIF0_MCU_L1_TAG_LAST__ALBIORIX
				: RGX_TEXAS_BIF0_MCU_L1_TAG_LAST__SERIES8;
			IMG_UINT32 ui32Tag_RGX_TEXAS_BIF0_TAG_IPFID_ARRAY_FIRST =
				(RGX_IS_FEATURE_SUPPORTED(psDevInfo, ALBIORIX_TOP_INFRASTRUCTURE))
				? RGX_TEXAS_BIF0_TAG_IPFID_ARRAY_FIRST__ALBIORIX
				: RGX_TEXAS_BIF0_TAG_IPFID_ARRAY_FIRST__SERIES8;

			/* Texas 0 BIF */
			if (ui32TagID < ui32Tag_RGX_TEXAS_BIF0_MCU_L1_TAG_LAST)
			{
				pszTagID = "MCU L1";
				_RGXDecodeMMUReqMCULevel1(psDevInfo, ui32TagID, &pszTagSB);
			}
			else if (ui32TagID < ui32Tag_RGX_TEXAS_BIF0_TAG_IPFID_ARRAY_FIRST)
			{
				if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, ALBIORIX_TOP_INFRASTRUCTURE))
				{
					switch (ui32TagID)
					{
						case 6: pszTagID = "TCU L1"; break;
						case 7:
						case 8: pszTagID = "PBE0"; break;
					}
				}
				else
				{
					switch (ui32TagID)
					{
						case 12: pszTagID = "TCU L1"; break;
						case 13:
						case 14: pszTagID = "PBE0"; break;
					}
				}
			}
			else if (ui32TagID <= RGX_TEXAS_BIF0_TAG_IPFID_ARRAY_LAST)
			{
				pszTagID = "IPF ID Array";
			}
			else if (ui32TagID < RGX_TEXAS_BIF0_TAG_RTU_RAC_FIRST)
			{
				switch (ui32TagID)
				{
					case 34: pszTagID = "IPF_CPF"; break;
					case 35: pszTagID = "PPP"; break;
					case 36:
					case 37: pszTagID = "ISP0 ID Array"; break;
					case 38:
					case 39: pszTagID = "ISP2 ID Array"; break;
					case 40: pszTagID = "VCE RTC"; break;
				}
			}
			else if (ui32TagID <= RGX_TEXAS_BIF0_TAG_RTU_RAC_LAST)
			{
				pszTagID = "RTU RAC";
			}
			else if (ui32TagID <= RGX_TEXAS_BIF0_TAG_LAST)
			{
				switch (ui32TagID)
				{
					case 49: pszTagID = "VCE AMC"; break;
					case 50:
					case 51: pszTagID = "SHF"; break;
				}
			}
			else
			{
				PVR_DPF((PVR_DBG_WARNING, "%s: Unidentified Texas BIF Tag ID: %d", __func__, ui32TagID));
			}
		}
		else if ((ui32BIFModule % ui32BIFsPerSPU) == 1)
		{
			IMG_UINT32 ui32Tag_RGX_TEXAS_BIF0_MCU_L1_TAG_LAST =
				(RGX_IS_FEATURE_SUPPORTED(psDevInfo, ALBIORIX_TOP_INFRASTRUCTURE))
				? RGX_TEXAS_BIF0_MCU_L1_TAG_LAST__ALBIORIX
				: RGX_TEXAS_BIF0_MCU_L1_TAG_LAST__SERIES8;

			/* Texas 1 BIF */
			if (ui32TagID < ui32Tag_RGX_TEXAS_BIF0_MCU_L1_TAG_LAST)
			{
				pszTagID = "MCU L1";
				_RGXDecodeMMUReqMCULevel1(psDevInfo, ui32TagID, &pszTagSB);
			}
			else if (ui32TagID <= RGX_TEXAS_BIF1_TAG_LAST)
			{
				switch (ui32TagID)
				{
					/** Albiorix/NUM_TPU_PER_SPU > 1 **/
					case 6:
					case 7:  pszTagID = "BSC"; break;
					/** All cores **/
					case 12: pszTagID = "TCU L1"; break;
					case 13: pszTagID = "TPF"; break;
					case 14: pszTagID = "TPF CPF"; break;
					case 15:
					case 16: pszTagID = "PBE1"; break;
					case 17: pszTagID = "PDSRW cache"; break;
					case 18: pszTagID = "PDS"; break;
					case 19:
					case 20: pszTagID = "ISP1 ID Array"; break;
					case 21: pszTagID = "USC L2"; break;
					case 22: pszTagID = "VDM L2"; break;
					case 23: pszTagID = "RTU FBA L2"; break;
					case 24: pszTagID = "RTU SHR L2"; break;
					case 25: pszTagID = "RTU SHG L2"; break;
					case 26: pszTagID = "RTU TUL L2"; break;
				}
			}
			else
			{
				PVR_DPF((PVR_DBG_WARNING, "%s: Unidentified Texas BIF Tag ID: %d", __func__, ui32TagID));
			}
		}
	}
	else if (ui32BIFModule == RGX_GET_FEATURE_VALUE(psDevInfo, NUM_SPU)*ui32BIFsPerSPU)
	{
		/* Jones BIF */

		if ((ui32TagID >= RGX_JONES_BIF_DCE_TAG_FIRST) && (ui32TagID <= RGX_JONES_BIF_DCE_TAG_LAST))
		{
			pszTagID = "DCE";
		}
		else if ((ui32TagID >= RGX_JONES_BIF_TDM_TAG_FIRST) && (ui32TagID <= RGX_JONES_BIF_TDM_TAG_LAST))
		{
			pszTagID = "TDM";
		}
		else if ((ui32TagID >= RGX_JONES_BIF_CDM_TAG_FIRST) && (ui32TagID <= RGX_JONES_BIF_CDM_TAG_LAST))
		{
			pszTagID = "CDM";
		}
		else if ((ui32TagID >= RGX_JONES_BIF_TE_TAG_FIRST) && (ui32TagID <= RGX_JONES_BIF_TE_TAG_LAST))
		{
			pszTagID = "Tiling Engine (TE3)";
		}
		else if ((ui32TagID >= RGX_JONES_BIF_RTU_TAG_FIRST) && (ui32TagID <= RGX_JONES_BIF_RTU_TAG_LAST))
		{
			pszTagID = "RTU";
		}
		else if (ui32TagID <= RGX_JONES_BIF_TAG_LAST)
		{
			switch (ui32TagID)
			{
				case RGX_JONES_BIF_IPP_TAG:		pszTagID = "IPP"; break;
				case RGX_JONES_BIF_PM_TAG:		pszTagID = "PM"; break;
				case RGX_JONES_BIF_META_TAG:	pszTagID = "META"; break;
				case RGX_JONES_BIF_META_DMA_TAG:pszTagID = "META DMA"; break;
				case RGX_JONES_BIF_RPM_TAG:		pszTagID = "RPM"; break;
			}
		}
		else
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: Unidentified Jones BIF Tag ID: %d", __func__, ui32TagID));
		}
	}
	else if (bWriteBack)
	{
		pszTagID = "";
		pszTagSB = "Writeback of dirty cacheline";
	}
	else
	{
		PVR_DPF((PVR_DBG_WARNING, "%s: Unidentified BIF Module: %d", __func__, ui32BIFModule));
	}

	*ppszTagID = pszTagID;
	*ppszTagSB = pszTagSB;
}


static void ConvertOSTimestampToSAndNS(IMG_UINT64 ui64OSTimer,
							IMG_UINT64 *pui64Seconds,
							IMG_UINT64 *pui64Nanoseconds)
{
	IMG_UINT32 ui32Remainder;

	*pui64Seconds = OSDivide64r64(ui64OSTimer, 1000000000, &ui32Remainder);
	*pui64Nanoseconds = ui64OSTimer - (*pui64Seconds * 1000000000ULL);
}


typedef enum _DEVICEMEM_HISTORY_QUERY_INDEX_
{
	DEVICEMEM_HISTORY_QUERY_INDEX_PRECEDING,
	DEVICEMEM_HISTORY_QUERY_INDEX_FAULTED,
	DEVICEMEM_HISTORY_QUERY_INDEX_NEXT,
	DEVICEMEM_HISTORY_QUERY_INDEX_COUNT,
} DEVICEMEM_HISTORY_QUERY_INDEX;


/*!
*******************************************************************************

 @Function	_PrintDevicememHistoryQueryResult

 @Description

 Print details of a single result from a DevicememHistory query

 @Input pfnDumpDebugPrintf       - Debug printf function
 @Input pvDumpDebugFile          - Optional file identifier to be passed to the
                                   'printf' function if required
 @Input psFaultProcessInfo       - The process info derived from the page fault
 @Input psResult                 - The DevicememHistory result to be printed
 @Input ui32Index                - The index of the result

 @Return   void

******************************************************************************/
static void _PrintDevicememHistoryQueryResult(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
						void *pvDumpDebugFile,
						RGXMEM_PROCESS_INFO *psFaultProcessInfo,
						DEVICEMEM_HISTORY_QUERY_OUT_RESULT *psResult,
						IMG_UINT32 ui32Index,
						const IMG_CHAR* pszIndent)
{
	IMG_UINT32 ui32Remainder;
	IMG_UINT64 ui64Seconds, ui64Nanoseconds;

	ConvertOSTimestampToSAndNS(psResult->ui64When,
							&ui64Seconds,
							&ui64Nanoseconds);

	if (psFaultProcessInfo->uiPID != RGXMEM_SERVER_PID_FIRMWARE)
	{
		PVR_DUMPDEBUG_LOG("%s    [%u] Name: %s Base address: " IMG_DEV_VIRTADDR_FMTSPEC
					" Size: " IMG_DEVMEM_SIZE_FMTSPEC
					" Operation: %s Modified: %" IMG_UINT64_FMTSPEC
					" us ago (OS time %" IMG_UINT64_FMTSPEC
					".%09" IMG_UINT64_FMTSPEC " s)",
						pszIndent,
						ui32Index,
						psResult->szString,
						psResult->sBaseDevVAddr.uiAddr,
						psResult->uiSize,
						psResult->bMap ? "Map": "Unmap",
						OSDivide64r64(psResult->ui64Age, 1000, &ui32Remainder),
						ui64Seconds,
						ui64Nanoseconds);
	}
	else
	{
		PVR_DUMPDEBUG_LOG("%s    [%u] Name: %s Base address: " IMG_DEV_VIRTADDR_FMTSPEC
					" Size: " IMG_DEVMEM_SIZE_FMTSPEC
					" Operation: %s Modified: %" IMG_UINT64_FMTSPEC
					" us ago (OS time %" IMG_UINT64_FMTSPEC
					".%09" IMG_UINT64_FMTSPEC
					") PID: %u (%s)",
						pszIndent,
						ui32Index,
						psResult->szString,
						psResult->sBaseDevVAddr.uiAddr,
						psResult->uiSize,
						psResult->bMap ? "Map": "Unmap",
						OSDivide64r64(psResult->ui64Age, 1000, &ui32Remainder),
						ui64Seconds,
						ui64Nanoseconds,
						psResult->sProcessInfo.uiPID,
						psResult->sProcessInfo.szProcessName);
	}

	if (!psResult->bRange)
	{
		PVR_DUMPDEBUG_LOG("%s        Whole allocation was %s", pszIndent, psResult->bMap ? "mapped": "unmapped");
	}
	else
	{
		PVR_DUMPDEBUG_LOG("%s        Pages %u to %u (" IMG_DEV_VIRTADDR_FMTSPEC "-" IMG_DEV_VIRTADDR_FMTSPEC ") %s%s",
										pszIndent,
										psResult->ui32StartPage,
										psResult->ui32StartPage + psResult->ui32PageCount - 1,
										psResult->sMapStartAddr.uiAddr,
										psResult->sMapEndAddr.uiAddr,
										psResult->bAll ? "(whole allocation) " : "",
										psResult->bMap ? "mapped": "unmapped");
	}
}

/*!
*******************************************************************************

 @Function	_PrintDevicememHistoryQueryOut

 @Description

 Print details of all the results from a DevicememHistory query

 @Input pfnDumpDebugPrintf       - Debug printf function
 @Input pvDumpDebugFile          - Optional file identifier to be passed to the
                                   'printf' function if required
 @Input psFaultProcessInfo       - The process info derived from the page fault
 @Input psQueryOut               - Storage for the query results

 @Return   void

******************************************************************************/
static void _PrintDevicememHistoryQueryOut(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
						void *pvDumpDebugFile,
						RGXMEM_PROCESS_INFO *psFaultProcessInfo,
						DEVICEMEM_HISTORY_QUERY_OUT *psQueryOut,
						const IMG_CHAR* pszIndent)
{
	IMG_UINT32 i;

	if (psQueryOut->ui32NumResults == 0)
	{
		PVR_DUMPDEBUG_LOG("%s    No results", pszIndent);
	}
	else
	{
		for (i = 0; i < psQueryOut->ui32NumResults; i++)
		{
			_PrintDevicememHistoryQueryResult(pfnDumpDebugPrintf, pvDumpDebugFile,
									psFaultProcessInfo,
									&psQueryOut->sResults[i],
									i,
									pszIndent);
		}
	}
}

/* table of HW page size values and the equivalent */
static const unsigned int aui32HWPageSizeTable[][2] =
{
	{ 0, PVRSRV_4K_PAGE_SIZE },
	{ 1, PVRSRV_16K_PAGE_SIZE },
	{ 2, PVRSRV_64K_PAGE_SIZE },
	{ 3, PVRSRV_256K_PAGE_SIZE },
	{ 4, PVRSRV_1M_PAGE_SIZE },
	{ 5, PVRSRV_2M_PAGE_SIZE }
};

/*!
*******************************************************************************

 @Function	_PageSizeHWToBytes

 @Description

 Convert a HW page size value to its size in bytes

 @Input ui32PageSizeHW     - The HW page size value

 @Return   IMG_UINT32      The page size in bytes

******************************************************************************/
static IMG_UINT32 _PageSizeHWToBytes(IMG_UINT32 ui32PageSizeHW)
{
	if (ui32PageSizeHW > 5)
	{
		/* This is invalid, so return a default value as we cannot ASSERT in this code! */
		return PVRSRV_4K_PAGE_SIZE;
	}

	return aui32HWPageSizeTable[ui32PageSizeHW][1];
}

/*!
*******************************************************************************

 @Function	_GetDevicememHistoryData

 @Description

 Get the DevicememHistory results for the given PID and faulting device virtual address.
 The function will query DevicememHistory for information about the faulting page, as well
 as the page before and after.

 @Input psDeviceNode       - The device which this allocation search should be made on
 @Input uiPID              - The process ID to search for allocations belonging to
 @Input sFaultDevVAddr     - The device address to search for allocations at/before/after
 @Input asQueryOut         - Storage for the query results
 @Input ui32PageSizeBytes  - Faulted page size in bytes

 @Return IMG_BOOL          - IMG_TRUE if any results were found for this page fault

******************************************************************************/
static IMG_BOOL _GetDevicememHistoryData(PVRSRV_DEVICE_NODE *psDeviceNode, IMG_PID uiPID,
							IMG_DEV_VIRTADDR sFaultDevVAddr,
							DEVICEMEM_HISTORY_QUERY_OUT asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_COUNT],
							IMG_UINT32 ui32PageSizeBytes)
{
	DEVICEMEM_HISTORY_QUERY_IN sQueryIn;
	IMG_BOOL bAnyHits = IMG_FALSE;

	/* if the page fault originated in the firmware then the allocation may
	 * appear to belong to any PID, because FW allocations are attributed
	 * to the client process creating the allocation, so instruct the
	 * devicemem_history query to search all available PIDs
	 */
	if (uiPID == RGXMEM_SERVER_PID_FIRMWARE)
	{
		sQueryIn.uiPID = DEVICEMEM_HISTORY_PID_ANY;
	}
	else
	{
		sQueryIn.uiPID = uiPID;
	}

	sQueryIn.psDevNode = psDeviceNode;
	/* Query the DevicememHistory for all allocations in the previous page... */
	sQueryIn.sDevVAddr.uiAddr = (sFaultDevVAddr.uiAddr & ~(IMG_UINT64)(ui32PageSizeBytes - 1)) - ui32PageSizeBytes;
	if (DevicememHistoryQuery(&sQueryIn, &asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_PRECEDING],
	                          ui32PageSizeBytes, IMG_TRUE))
	{
		bAnyHits = IMG_TRUE;
	}

	/* Query the DevicememHistory for any record at the exact address... */
	sQueryIn.sDevVAddr = sFaultDevVAddr;
	if (DevicememHistoryQuery(&sQueryIn, &asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_FAULTED],
	                          ui32PageSizeBytes, IMG_FALSE))
	{
		bAnyHits = IMG_TRUE;
	}
	else
	{
		/* If not matched then try matching any record in the faulting page... */
		if (DevicememHistoryQuery(&sQueryIn, &asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_FAULTED],
		                          ui32PageSizeBytes, IMG_TRUE))
		{
			bAnyHits = IMG_TRUE;
		}
	}

	/* Query the DevicememHistory for all allocations in the next page... */
	sQueryIn.sDevVAddr.uiAddr = (sFaultDevVAddr.uiAddr & ~(IMG_UINT64)(ui32PageSizeBytes - 1)) + ui32PageSizeBytes;
	if (DevicememHistoryQuery(&sQueryIn, &asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_NEXT],
	                          ui32PageSizeBytes, IMG_TRUE))
	{
		bAnyHits = IMG_TRUE;
	}

	return bAnyHits;
}

/* stored data about one page fault */
typedef struct _FAULT_INFO_
{
	/* the process info of the memory context that page faulted */
	RGXMEM_PROCESS_INFO sProcessInfo;
	IMG_DEV_VIRTADDR sFaultDevVAddr;
	MMU_FAULT_DATA   sMMUFaultData;
	DEVICEMEM_HISTORY_QUERY_OUT asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_COUNT];
	/* the CR timer value at the time of the fault, recorded by the FW.
	 * used to differentiate different page faults
	 */
	IMG_UINT64 ui64CRTimer;
	/* time when this FAULT_INFO entry was added. used for timing
	 * reference against the map/unmap information
	 */
	IMG_UINT64 ui64When;
	IMG_UINT32 ui32FaultInfoFlags;
} FAULT_INFO;

/* history list of page faults.
 * Keeps the first `n` page faults and the last `n` page faults, like the FW
 * HWR log
 */
typedef struct _FAULT_INFO_LOG_
{
	IMG_UINT32 ui32Head;
	/* the number of faults in this log need not correspond exactly to
	 * the HWINFO number of the FW, as the FW HWINFO log may contain
	 * non-page fault HWRs
	 */
	FAULT_INFO asFaults[RGXFWIF_HWINFO_MAX];
} FAULT_INFO_LOG;

#define FAULT_INFO_PROC_INFO   (0x1U)
#define FAULT_INFO_DEVMEM_HIST (0x2U)

static FAULT_INFO_LOG gsFaultInfoLog = { 0 };

static void _FillAppForFWFaults(PVRSRV_RGXDEV_INFO *psDevInfo,
							FAULT_INFO *psInfo,
							RGXMEM_PROCESS_INFO *psProcInfo)
{
	IMG_UINT32 i, j;

	for (i = 0; i < DEVICEMEM_HISTORY_QUERY_INDEX_COUNT; i++)
	{
		for (j = 0; j < DEVICEMEM_HISTORY_QUERY_OUT_MAX_RESULTS; j++)
		{
			IMG_BOOL bFound;

			RGXMEM_PROCESS_INFO *psProcInfo = &psInfo->asQueryOut[i].sResults[j].sProcessInfo;
			bFound = RGXPCPIDToProcessInfo(psDevInfo,
								psProcInfo->uiPID,
								psProcInfo);
			if (!bFound)
			{
				OSStringLCopy(psProcInfo->szProcessName,
								"(unknown)",
								sizeof(psProcInfo->szProcessName));
			}
		}
	}
}

/*!
*******************************************************************************

 @Function	_PrintFaultInfo

 @Description

 Print all the details of a page fault from a FAULT_INFO structure

 @Input pfnDumpDebugPrintf   - The debug printf function
 @Input pvDumpDebugFile      - Optional file identifier to be passed to the
                               'printf' function if required
 @Input psInfo               - The page fault occurrence to print

 @Return   void

******************************************************************************/
static void _PrintFaultInfo(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					PVRSRV_DEVICE_NODE *psDevNode,
					void *pvDumpDebugFile,
					FAULT_INFO *psInfo,
					const IMG_CHAR* pszIndent)
{
	IMG_UINT32 i;
	IMG_UINT64 ui64Seconds, ui64Nanoseconds;

	ConvertOSTimestampToSAndNS(psInfo->ui64When, &ui64Seconds, &ui64Nanoseconds);

	if (BITMASK_HAS(psInfo->ui32FaultInfoFlags, FAULT_INFO_PROC_INFO))
	{
		IMG_PID uiPID = (psInfo->sProcessInfo.uiPID == RGXMEM_SERVER_PID_FIRMWARE || psInfo->sProcessInfo.uiPID == RGXMEM_SERVER_PID_PM) ?
							0 : psInfo->sProcessInfo.uiPID;

		PVR_DUMPDEBUG_LOG("%sDevice memory history for page fault address " IMG_DEV_VIRTADDR_FMTSPEC
							", CRTimer: 0x%016" IMG_UINT64_FMTSPECX
							", PID: %u (%s, unregistered: %u) OS time: "
							"%" IMG_UINT64_FMTSPEC ".%09" IMG_UINT64_FMTSPEC,
					pszIndent,
					psInfo->sFaultDevVAddr.uiAddr,
					psInfo->ui64CRTimer,
					uiPID,
					psInfo->sProcessInfo.szProcessName,
					psInfo->sProcessInfo.bUnregistered,
					ui64Seconds,
					ui64Nanoseconds);
	}
	else
	{
		PVR_DUMPDEBUG_LOG("%sCould not find PID for device memory history on PC of the fault", pszIndent);
	}

	if (BITMASK_HAS(psInfo->ui32FaultInfoFlags, FAULT_INFO_DEVMEM_HIST))
	{
		for (i = DEVICEMEM_HISTORY_QUERY_INDEX_PRECEDING; i < DEVICEMEM_HISTORY_QUERY_INDEX_COUNT; i++)
		{
			const IMG_CHAR *pszWhich = NULL;

			switch (i)
			{
				case DEVICEMEM_HISTORY_QUERY_INDEX_PRECEDING:
					pszWhich = "Preceding page";
					break;
				case DEVICEMEM_HISTORY_QUERY_INDEX_FAULTED:
					pszWhich = "Faulted page";
					break;
				case DEVICEMEM_HISTORY_QUERY_INDEX_NEXT:
					pszWhich = "Next page";
					break;
			}

			PVR_DUMPDEBUG_LOG("%s  %s:", pszIndent, pszWhich);
			_PrintDevicememHistoryQueryOut(pfnDumpDebugPrintf, pvDumpDebugFile,
								&psInfo->sProcessInfo,
								&psInfo->asQueryOut[i],
								pszIndent);
		}
	}
	else
	{
		PVR_DUMPDEBUG_LOG("%s  No matching Devmem History for fault address", pszIndent);
		DevicememHistoryDumpRecordStats(psDevNode, pfnDumpDebugPrintf, pvDumpDebugFile);
		PVR_DUMPDEBUG_LOG("%s  Records Searched -"
		                  " PP:%"IMG_UINT64_FMTSPEC
		                  " FP:%"IMG_UINT64_FMTSPEC
		                  " NP:%"IMG_UINT64_FMTSPEC,
		                  pszIndent,
		                  psInfo->asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_PRECEDING].ui64SearchCount,
		                  psInfo->asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_FAULTED].ui64SearchCount,
		                  psInfo->asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_NEXT].ui64SearchCount);
	}
}

static void _RecordFaultInfo(PVRSRV_RGXDEV_INFO *psDevInfo,
					FAULT_INFO *psInfo,
					IMG_DEV_VIRTADDR sFaultDevVAddr,
					IMG_DEV_PHYADDR sPCDevPAddr,
					IMG_UINT64 ui64CRTimer,
					IMG_UINT32 ui32PageSizeBytes)
{
	IMG_BOOL bFound = IMG_FALSE, bIsPMFault = IMG_FALSE;
	RGXMEM_PROCESS_INFO sProcessInfo;

	psInfo->ui32FaultInfoFlags = 0;
	psInfo->sFaultDevVAddr = sFaultDevVAddr;
	psInfo->ui64CRTimer = ui64CRTimer;
	psInfo->ui64When = OSClockns64();

	if (GetInfoPageDebugFlagsKM() & DEBUG_FEATURE_PAGE_FAULT_DEBUG_ENABLED)
	{
		/* Check if this is PM fault */
		if (psInfo->sMMUFaultData.eType == MMU_FAULT_TYPE_PM)
		{
			bIsPMFault = IMG_TRUE;
			bFound = IMG_TRUE;
			sProcessInfo.uiPID = RGXMEM_SERVER_PID_PM;
			OSStringLCopy(sProcessInfo.szProcessName, "PM", sizeof(sProcessInfo.szProcessName));
			sProcessInfo.szProcessName[sizeof(sProcessInfo.szProcessName) - 1] = '\0';
			sProcessInfo.bUnregistered = IMG_FALSE;
		}
		else
		{
			/* look up the process details for the faulting page catalogue */
			bFound = RGXPCAddrToProcessInfo(psDevInfo, sPCDevPAddr, &sProcessInfo);
		}

		if (bFound)
		{
			IMG_BOOL bHits;

			psInfo->ui32FaultInfoFlags = FAULT_INFO_PROC_INFO;
			psInfo->sProcessInfo = sProcessInfo;

			if (bIsPMFault)
			{
				bHits = IMG_TRUE;
			}
			else
			{
				/* get any DevicememHistory data for the faulting address */
				bHits = _GetDevicememHistoryData(psDevInfo->psDeviceNode,
								 sProcessInfo.uiPID,
								 sFaultDevVAddr,
								 psInfo->asQueryOut,
								 ui32PageSizeBytes);

				if (bHits)
				{
					psInfo->ui32FaultInfoFlags |= FAULT_INFO_DEVMEM_HIST;

					/* if the page fault was caused by the firmware then get information about
					 * which client application created the related allocations.
					 *
					 * Fill in the process info data for each query result.
					 */

					if (sProcessInfo.uiPID == RGXMEM_SERVER_PID_FIRMWARE)
					{
						_FillAppForFWFaults(psDevInfo, psInfo, &sProcessInfo);
					}
				}
			}
		}
	}
}

/*!
*******************************************************************************

 @Function	_DumpFaultAddressHostView

 @Description

 Dump FW HWR fault status in human readable form.

 @Input ui32Index            - Index of global Fault info
 @Input pfnDumpDebugPrintf   - The debug printf function
 @Input pvDumpDebugFile      - Optional file identifier to be passed to the
                               'printf' function if required
 @Return   void

******************************************************************************/
static void _DumpFaultAddressHostView(MMU_FAULT_DATA *psFaultData,
					DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile,
					const IMG_CHAR* pszIndent)
{
	MMU_LEVEL eTopLevel;
	const IMG_CHAR szPageLevel[][4] = {"", "PTE", "PDE", "PCE" };
	const IMG_CHAR szPageError[][3] = {"", "PT",  "PD",  "PC"  };

	eTopLevel = psFaultData->eTopLevel;

	if (psFaultData->eType == MMU_FAULT_TYPE_UNKNOWN)
	{
		PVR_DUMPDEBUG_LOG("%sNo live host MMU data available", pszIndent);
		return;
	}
	else if (psFaultData->eType == MMU_FAULT_TYPE_PM)
	{
		PVR_DUMPDEBUG_LOG("%sPM faulted at PC address = 0x%016" IMG_UINT64_FMTSPECx, pszIndent, psFaultData->sLevelData[MMU_LEVEL_0].ui64Address);
	}
	else
	{
		MMU_LEVEL eCurrLevel;
		PVR_ASSERT(eTopLevel < MMU_LEVEL_LAST);

		for (eCurrLevel = eTopLevel; eCurrLevel > MMU_LEVEL_0; eCurrLevel--)
		{
			MMU_LEVEL_DATA *psMMULevelData = &psFaultData->sLevelData[eCurrLevel];
			if (psMMULevelData->ui64Address)
			{
				if (psMMULevelData->uiBytesPerEntry == 4)
				{
					PVR_DUMPDEBUG_LOG("%s%s for index %d = 0x%08x and is %s",
								pszIndent,
								szPageLevel[eCurrLevel],
								psMMULevelData->ui32Index,
								(IMG_UINT) psMMULevelData->ui64Address,
								psMMULevelData->psDebugStr);
				}
				else
				{
					PVR_DUMPDEBUG_LOG("%s%s for index %d = 0x%016" IMG_UINT64_FMTSPECx " and is %s",
								pszIndent,
								szPageLevel[eCurrLevel],
								psMMULevelData->ui32Index,
								psMMULevelData->ui64Address,
								psMMULevelData->psDebugStr);
				}
			}
			else
			{
				PVR_DUMPDEBUG_LOG("%s%s index (%d) out of bounds (%d)",
							pszIndent,
							szPageError[eCurrLevel],
							psMMULevelData->ui32Index,
							psMMULevelData->ui32NumOfEntries);
				break;
			}
		}
	}

}

/*!
*******************************************************************************

 @Function	_RGXDumpRGXMMUFaultStatus

 @Description

 Dump MMU Fault status in human readable form.

 @Input pfnDumpDebugPrintf   - The debug printf function
 @Input pvDumpDebugFile      - Optional file identifier to be passed to the
                               'printf' function if required
 @Input psDevInfo            - RGX device info
 @Input ui64MMUStatus        - MMU Status register value
 @Input pszMetaOrCore        - string representing call is for META or MMU core
 @Return   void

******************************************************************************/
static void _RGXDumpRGXMMUFaultStatus(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile,
					PVRSRV_RGXDEV_INFO *psDevInfo,
					const IMG_UINT64 aui64MMUStatus[],
					const IMG_PCHAR pszMetaOrCore,
					const IMG_CHAR *pszIndent)
{
	if (aui64MMUStatus[0] == 0x0)
	{
		PVR_DUMPDEBUG_LOG("%sMMU (%s) - OK", pszIndent, pszMetaOrCore);
	}
	else
	{
		IMG_UINT32 ui32PC        = (aui64MMUStatus[0] & ~RGX_CR_MMU_FAULT_STATUS1_CONTEXT_CLRMSK) >>
		                           RGX_CR_MMU_FAULT_STATUS1_CONTEXT_SHIFT;
		IMG_UINT64 ui64Addr      = ((aui64MMUStatus[0] & ~RGX_CR_MMU_FAULT_STATUS1_ADDRESS_CLRMSK) >>
		                           RGX_CR_MMU_FAULT_STATUS1_ADDRESS_SHIFT) <<  4; /* align shift */
		IMG_UINT32 ui32Requester = (aui64MMUStatus[0] & ~RGX_CR_MMU_FAULT_STATUS1_REQ_ID_CLRMSK) >>
		                           RGX_CR_MMU_FAULT_STATUS1_REQ_ID_SHIFT;
		IMG_UINT32 ui32MMULevel  = (aui64MMUStatus[0] & ~RGX_CR_MMU_FAULT_STATUS1_LEVEL_CLRMSK) >>
		                           RGX_CR_MMU_FAULT_STATUS1_LEVEL_SHIFT;
		IMG_BOOL bRead           = (aui64MMUStatus[0] & RGX_CR_MMU_FAULT_STATUS1_RNW_EN) != 0;
		IMG_BOOL bFault          = (aui64MMUStatus[0] & RGX_CR_MMU_FAULT_STATUS1_FAULT_EN) != 0;
		IMG_BOOL bROFault        = ((aui64MMUStatus[0] & ~RGX_CR_MMU_FAULT_STATUS1_TYPE_CLRMSK) >>
		                            RGX_CR_MMU_FAULT_STATUS1_TYPE_SHIFT) == 0x2;
		IMG_BOOL bProtFault      = ((aui64MMUStatus[0] & ~RGX_CR_MMU_FAULT_STATUS1_TYPE_CLRMSK) >>
		                            RGX_CR_MMU_FAULT_STATUS1_TYPE_SHIFT) == 0x3;
		IMG_UINT32 ui32BIFModule;
		IMG_BOOL bWriteBack, bFBMFault;
		IMG_CHAR aszScratch[RGX_DEBUG_STR_SIZE];
		IMG_CHAR *pszTagID = NULL;
		IMG_CHAR *pszTagSB = NULL;
		const IMG_PCHAR pszMetaOrRiscv = RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META) ? "META" : "RISCV";

		if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, ALBIORIX_TOP_INFRASTRUCTURE))
		{
			ui32BIFModule = (aui64MMUStatus[1] & ~RGX_CR_MMU_FAULT_STATUS2__ALBTOP__BIF_ID_CLRMSK) >>
										RGX_CR_MMU_FAULT_STATUS2__ALBTOP__BIF_ID_SHIFT;
			bWriteBack    = (aui64MMUStatus[1] & RGX_CR_MMU_FAULT_STATUS2__ALBTOP__WRITEBACK_EN) != 0;
			bFBMFault     = (aui64MMUStatus[1] & RGX_CR_MMU_FAULT_STATUS2__ALBTOP__FBM_FAULT_EN) != 0;
		}
		else
		{
			ui32BIFModule = (aui64MMUStatus[1] & ~RGX_CR_MMU_FAULT_STATUS2_BIF_ID_CLRMSK) >>
										RGX_CR_MMU_FAULT_STATUS2_BIF_ID_SHIFT;
			bWriteBack    = (aui64MMUStatus[1] & RGX_CR_MMU_FAULT_STATUS2_WRITEBACK_EN) != 0;
			bFBMFault     = (aui64MMUStatus[1] & RGX_CR_MMU_FAULT_STATUS2_FBM_FAULT_EN) != 0;
		}

		if (strcmp(pszMetaOrCore, "Core") != 0)
		{
			ui32PC		= (aui64MMUStatus[0] & ~RGX_CR_MMU_FAULT_STATUS_META_CONTEXT_CLRMSK) >>
								RGX_CR_MMU_FAULT_STATUS_META_CONTEXT_SHIFT;
			ui64Addr	= ((aui64MMUStatus[0] & ~RGX_CR_MMU_FAULT_STATUS_META_ADDRESS_CLRMSK) >>
								RGX_CR_MMU_FAULT_STATUS_META_ADDRESS_SHIFT) <<  4; /* align shift */
			ui32Requester = (aui64MMUStatus[0] & ~RGX_CR_MMU_FAULT_STATUS_META_REQ_ID_CLRMSK) >>
								RGX_CR_MMU_FAULT_STATUS_META_REQ_ID_SHIFT;
			ui32MMULevel  = (aui64MMUStatus[0] & ~RGX_CR_MMU_FAULT_STATUS_META_LEVEL_CLRMSK) >>
								RGX_CR_MMU_FAULT_STATUS_META_LEVEL_SHIFT;
			bRead		= (aui64MMUStatus[0] & RGX_CR_MMU_FAULT_STATUS_META_RNW_EN) != 0;
			bFault      = (aui64MMUStatus[0] & RGX_CR_MMU_FAULT_STATUS_META_FAULT_EN) != 0;
			bROFault    = ((aui64MMUStatus[0] & ~RGX_CR_MMU_FAULT_STATUS_META_TYPE_CLRMSK) >>
								RGX_CR_MMU_FAULT_STATUS_META_TYPE_SHIFT) == 0x2;
			bProtFault  = ((aui64MMUStatus[0] & ~RGX_CR_MMU_FAULT_STATUS_META_TYPE_CLRMSK) >>
								RGX_CR_MMU_FAULT_STATUS_META_TYPE_SHIFT) == 0x3;
		}
		else
		{
			_RGXDecodeMMUReqTags(psDevInfo, ui32Requester, ui32BIFModule, bRead, bWriteBack, bFBMFault, &pszTagID, &pszTagSB, aszScratch, RGX_DEBUG_STR_SIZE);
		}

		PVR_DUMPDEBUG_LOG("%sMMU (%s) - FAULT:", pszIndent, pszMetaOrCore);
		PVR_DUMPDEBUG_LOG("%s  * MMU status (0x%016" IMG_UINT64_FMTSPECX " | 0x%08" IMG_UINT64_FMTSPECX "): PC = %d, %s 0x%010" IMG_UINT64_FMTSPECX ", %s(%s)%s%s%s%s.",
						  pszIndent,
						  aui64MMUStatus[0],
						  aui64MMUStatus[1],
						  ui32PC,
						  (bRead)?"Reading from":"Writing to",
						  ui64Addr,
						  (pszTagID)? pszTagID : pszMetaOrRiscv,
						  (pszTagSB)? pszTagSB : "-",
						  (bFault)?", Fault":"",
						  (bROFault)?", Read Only fault":"",
						  (bProtFault)?", PM/FW core protection fault":"",
						  _RGXDecodeMMULevel(ui32MMULevel));

	}
}

static_assert((RGX_CR_MMU_FAULT_STATUS1_CONTEXT_CLRMSK == RGX_CR_MMU_FAULT_STATUS_META_CONTEXT_CLRMSK),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS1_CONTEXT_SHIFT == RGX_CR_MMU_FAULT_STATUS_META_CONTEXT_SHIFT),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS1_ADDRESS_CLRMSK == RGX_CR_MMU_FAULT_STATUS_META_ADDRESS_CLRMSK),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS1_ADDRESS_SHIFT == RGX_CR_MMU_FAULT_STATUS_META_ADDRESS_SHIFT),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS1_REQ_ID_CLRMSK == RGX_CR_MMU_FAULT_STATUS_META_REQ_ID_CLRMSK),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS1_REQ_ID_SHIFT == RGX_CR_MMU_FAULT_STATUS_META_REQ_ID_SHIFT),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS1_LEVEL_CLRMSK == RGX_CR_MMU_FAULT_STATUS_META_LEVEL_CLRMSK),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS1_LEVEL_SHIFT == RGX_CR_MMU_FAULT_STATUS_META_LEVEL_SHIFT),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS1_RNW_EN == RGX_CR_MMU_FAULT_STATUS_META_RNW_EN),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS1_FAULT_EN == RGX_CR_MMU_FAULT_STATUS_META_FAULT_EN),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS1_TYPE_CLRMSK == RGX_CR_MMU_FAULT_STATUS_META_TYPE_CLRMSK),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS1_TYPE_SHIFT == RGX_CR_MMU_FAULT_STATUS_META_TYPE_SHIFT),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");


static const IMG_FLAGS2DESC asCswOpts2Description[] =
{
	{RGXFWIF_INICFG_CTXSWITCH_PROFILE_FAST, " Fast CSW profile;"},
	{RGXFWIF_INICFG_CTXSWITCH_PROFILE_MEDIUM, " Medium CSW profile;"},
	{RGXFWIF_INICFG_CTXSWITCH_PROFILE_SLOW, " Slow CSW profile;"},
	{RGXFWIF_INICFG_CTXSWITCH_PROFILE_NODELAY, " No Delay CSW profile;"},
	{RGXFWIF_INICFG_CTXSWITCH_MODE_RAND, " Random Csw enabled;"},
	{RGXFWIF_INICFG_CTXSWITCH_SRESET_EN, " SoftReset;"},
};

static const IMG_FLAGS2DESC asMisc2Description[] =
{
	{RGXFWIF_INICFG_POW_RASCALDUST, " Power Rascal/Dust;"},
	{RGXFWIF_INICFG_SPU_CLOCK_GATE, " SPU Clock Gating (requires Power Rascal/Dust);"},
	{RGXFWIF_INICFG_HWPERF_EN, " HwPerf EN;"},
	{RGXFWIF_INICFG_FBCDC_V3_1_EN, " FBCDCv3.1;"},
	{RGXFWIF_INICFG_CHECK_MLIST_EN, " Check MList;"},
	{RGXFWIF_INICFG_DISABLE_CLKGATING_EN, " ClockGating Off;"},
	{RGXFWIF_INICFG_REGCONFIG_EN, " Register Config;"},
	{RGXFWIF_INICFG_ASSERT_ON_OUTOFMEMORY, " Assert on OOM;"},
	{RGXFWIF_INICFG_HWP_DISABLE_FILTER, " HWP Filter Off;"},
	{RGXFWIF_INICFG_DM_KILL_MODE_RAND_EN, " CDM Random kill;"},
	{RGXFWIF_INICFG_DISABLE_DM_OVERLAP, " DM Overlap Off;"},
	{RGXFWIF_INICFG_ASSERT_ON_HWR_TRIGGER, " Assert on HWR;"},
	{RGXFWIF_INICFG_FABRIC_COHERENCY_ENABLED, " Coherent fabric on;"},
	{RGXFWIF_INICFG_VALIDATE_IRQ, " Validate IRQ;"},
	{RGXFWIF_INICFG_DISABLE_PDP_EN, " PDUMP Panic off;"},
	{RGXFWIF_INICFG_SPU_POWER_STATE_MASK_CHANGE_EN, " SPU Pow mask change on;"},
	{RGXFWIF_INICFG_WORKEST, " Workload Estim;"},
	{RGXFWIF_INICFG_PDVFS, " PDVFS;"},
	{RGXFWIF_INICFG_CDM_ARBITRATION_TASK_DEMAND, " CDM task demand arbitration;"},
	{RGXFWIF_INICFG_CDM_ARBITRATION_ROUND_ROBIN, " CDM round-robin arbitration;"},
	{RGXFWIF_INICFG_ISPSCHEDMODE_VER1_IPP, " ISP v1 scheduling;"},
	{RGXFWIF_INICFG_ISPSCHEDMODE_VER2_ISP, " ISP v2 scheduling;"},
	{RGXFWIF_INICFG_VALIDATE_SOCUSC_TIMER, " Validate SOC&USC timers;"}
};

static const IMG_FLAGS2DESC asFwOsCfg2Description[] =
{
	{RGXFWIF_INICFG_OS_CTXSWITCH_TDM_EN, " TDM;"},
	{RGXFWIF_INICFG_OS_CTXSWITCH_GEOM_EN, " GEOM;"},
	{RGXFWIF_INICFG_OS_CTXSWITCH_3D_EN, " 3D;"},
	{RGXFWIF_INICFG_OS_CTXSWITCH_CDM_EN, " CDM;"},
	{RGXFWIF_INICFG_OS_CTXSWITCH_RDM_EN, " RDM;"},
	{RGXFWIF_INICFG_OS_LOW_PRIO_CS_TDM, " LowPrio TDM;"},
	{RGXFWIF_INICFG_OS_LOW_PRIO_CS_GEOM, " LowPrio GEOM;"},
	{RGXFWIF_INICFG_OS_LOW_PRIO_CS_3D, " LowPrio 3D;"},
	{RGXFWIF_INICFG_OS_LOW_PRIO_CS_CDM, " LowPrio CDM;"},
	{RGXFWIF_INICFG_OS_LOW_PRIO_CS_RDM, " LowPrio RDM;"},
};

static const IMG_FLAGS2DESC asHwrState2Description[] =
{
	{RGXFWIF_HWR_HARDWARE_OK, " HWR OK;"},
	{RGXFWIF_HWR_RESET_IN_PROGRESS, " Reset ongoing;"},
	{RGXFWIF_HWR_GENERAL_LOCKUP, " General lockup;"},
	{RGXFWIF_HWR_DM_RUNNING_OK, " DM running ok;"},
	{RGXFWIF_HWR_DM_STALLING, " DM stalling;"},
	{RGXFWIF_HWR_FW_FAULT, " FW Fault;"},
	{RGXFWIF_HWR_RESTART_REQUESTED, " Restart requested;"},
};

static const IMG_FLAGS2DESC asDmState2Description[] =
{
	{RGXFWIF_DM_STATE_READY_FOR_HWR, " ready for hwr;"},
	{RGXFWIF_DM_STATE_NEEDS_SKIP, " needs skip;"},
	{RGXFWIF_DM_STATE_NEEDS_PR_CLEANUP, " needs PR cleanup;"},
	{RGXFWIF_DM_STATE_NEEDS_TRACE_CLEAR, " needs trace clear;"},
	{RGXFWIF_DM_STATE_GUILTY_LOCKUP, " guilty lockup;"},
	{RGXFWIF_DM_STATE_INNOCENT_LOCKUP, " innocent lockup;"},
	{RGXFWIF_DM_STATE_GUILTY_OVERRUNING, " guilty overrunning;"},
	{RGXFWIF_DM_STATE_INNOCENT_OVERRUNING, " innocent overrunning;"},
	{RGXFWIF_DM_STATE_GPU_ECC_HWR, " GPU ECC hwr;"},
};

static const IMG_FLAGS2DESC asHWErrorState[] =
{
	{RGX_HW_ERR_NA, "N/A"},
	{RGX_HW_ERR_PRIMID_FAILURE_DURING_DMKILL, "Primitive ID failure during DM kill."},
};

#if !defined(NO_HARDWARE)
static inline IMG_CHAR const *_GetRISCVException(IMG_UINT32 ui32Mcause)
{
	switch (ui32Mcause)
	{
#define X(value, fatal, description) \
		case value: \
			if (fatal) \
				return description; \
			return NULL;

		RGXRISCVFW_MCAUSE_TABLE
#undef X

		default:
			PVR_DPF((PVR_DBG_WARNING, "Invalid RISC-V FW mcause value 0x%08x", ui32Mcause));
			return NULL;
	}
}
#endif // !defined(NO_HARDWARE)

/*
 *  Translate ID code to descriptive string.
 *  Returns on the first match.
 */
static void _ID2Description(IMG_CHAR *psDesc, IMG_UINT32 ui32DescSize, const IMG_FLAGS2DESC *psConvTable, IMG_UINT32 ui32TableSize, IMG_UINT32 ui32ID)
{
	IMG_UINT32 ui32Idx;

	for (ui32Idx = 0; ui32Idx < ui32TableSize; ui32Idx++)
	{
		if (ui32ID == psConvTable[ui32Idx].uiFlag)
		{
			OSStringLCopy(psDesc, psConvTable[ui32Idx].pszLabel, ui32DescSize);
			return;
		}
	}
}

/*
	Writes flags strings to an uninitialised buffer.
*/
static void _GetFwSysFlagsDescription(IMG_CHAR *psDesc, IMG_UINT32 ui32DescSize, IMG_UINT32 ui32RawFlags)
{
	const IMG_CHAR szCswLabel[] = "Ctx switch options:";
	size_t uLabelLen = sizeof(szCswLabel) - 1;
	const size_t uiBytesPerDesc = (ui32DescSize - uLabelLen) / 2U - 1U;

	OSStringLCopy(psDesc, szCswLabel, ui32DescSize);

	DebugCommonFlagStrings(psDesc, uiBytesPerDesc + uLabelLen, asCswOpts2Description, ARRAY_SIZE(asCswOpts2Description), ui32RawFlags);
	DebugCommonFlagStrings(psDesc, ui32DescSize, asMisc2Description, ARRAY_SIZE(asMisc2Description), ui32RawFlags);
}

static void _GetFwOsFlagsDescription(IMG_CHAR *psDesc, IMG_UINT32 ui32DescSize, IMG_UINT32 ui32RawFlags)
{
	const IMG_CHAR szCswLabel[] = "Ctx switch:";
	size_t uLabelLen = sizeof(szCswLabel) - 1;
	const size_t uiBytesPerDesc = (ui32DescSize - uLabelLen) / 2U - 1U;

	OSStringLCopy(psDesc, szCswLabel, ui32DescSize);

	DebugCommonFlagStrings(psDesc, uiBytesPerDesc + uLabelLen, asFwOsCfg2Description, ARRAY_SIZE(asFwOsCfg2Description), ui32RawFlags);
}


/*!
*******************************************************************************

 @Function	_RGXDumpFWAssert

 @Description

 Dump FW assert strings when a thread asserts.

 @Input pfnDumpDebugPrintf   - The debug printf function
 @Input pvDumpDebugFile      - Optional file identifier to be passed to the
                               'printf' function if required
 @Input psRGXFWIfTraceBufCtl - RGX FW trace buffer

 @Return   void

******************************************************************************/
static void _RGXDumpFWAssert(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile,
					const RGXFWIF_TRACEBUF *psRGXFWIfTraceBufCtl)
{
	const IMG_CHAR *pszTraceAssertPath;
	const IMG_CHAR *pszTraceAssertInfo;
	IMG_INT32 ui32TraceAssertLine;
	IMG_UINT32 i;

	for (i = 0; i < RGXFW_THREAD_NUM; i++)
	{
		pszTraceAssertPath = psRGXFWIfTraceBufCtl->sTraceBuf[i].sAssertBuf.szPath;
		pszTraceAssertInfo = psRGXFWIfTraceBufCtl->sTraceBuf[i].sAssertBuf.szInfo;
		ui32TraceAssertLine = psRGXFWIfTraceBufCtl->sTraceBuf[i].sAssertBuf.ui32LineNum;

		/* print non-null assert strings */
		if (*pszTraceAssertInfo)
		{
			PVR_DUMPDEBUG_LOG("FW-T%d Assert: %s (%s:%d)",
			                  i, pszTraceAssertInfo, pszTraceAssertPath, ui32TraceAssertLine);
		}
	}
}

/*!
*******************************************************************************

 @Function	_RGXDumpFWFaults

 @Description

 Dump FW assert strings when a thread asserts.

 @Input pfnDumpDebugPrintf   - The debug printf function
 @Input pvDumpDebugFile      - Optional file identifier to be passed to the
                               'printf' function if required
 @Input psFwSysData       - RGX FW shared system data

 @Return   void

******************************************************************************/
static void _RGXDumpFWFaults(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                             void *pvDumpDebugFile,
                             const RGXFWIF_SYSDATA *psFwSysData)
{
	if (psFwSysData->ui32FWFaults > 0)
	{
		IMG_UINT32	ui32StartFault = psFwSysData->ui32FWFaults - RGXFWIF_FWFAULTINFO_MAX;
		IMG_UINT32	ui32EndFault   = psFwSysData->ui32FWFaults - 1;
		IMG_UINT32  ui32Index;

		if (psFwSysData->ui32FWFaults < RGXFWIF_FWFAULTINFO_MAX)
		{
			ui32StartFault = 0;
		}

		for (ui32Index = ui32StartFault; ui32Index <= ui32EndFault; ui32Index++)
		{
			const RGX_FWFAULTINFO *psFaultInfo = &psFwSysData->sFaultInfo[ui32Index % RGXFWIF_FWFAULTINFO_MAX];
			IMG_UINT64 ui64Seconds, ui64Nanoseconds;

			/* Split OS timestamp in seconds and nanoseconds */
			ConvertOSTimestampToSAndNS(psFaultInfo->ui64OSTimer, &ui64Seconds, &ui64Nanoseconds);

			PVR_DUMPDEBUG_LOG("FW Fault %d: %s (%s:%d)",
			                  ui32Index+1, psFaultInfo->sFaultBuf.szInfo,
			                  psFaultInfo->sFaultBuf.szPath,
			                  psFaultInfo->sFaultBuf.ui32LineNum);
			PVR_DUMPDEBUG_LOG("            Data = 0x%08x, CRTimer = 0x%012"IMG_UINT64_FMTSPECX", OSTimer = %" IMG_UINT64_FMTSPEC ".%09" IMG_UINT64_FMTSPEC,
			                  psFaultInfo->ui32Data,
			                  psFaultInfo->ui64CRTimer,
			                  ui64Seconds, ui64Nanoseconds);
		}
	}
}

static void _RGXDumpFWPoll(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile,
					const RGXFWIF_SYSDATA *psFwSysData)
{
	IMG_UINT32 i;
	for (i = 0; i < RGXFW_THREAD_NUM; i++)
	{
		if (psFwSysData->aui32CrPollAddr[i])
		{
			PVR_DUMPDEBUG_LOG("T%u polling %s (reg:0x%08X mask:0x%08X)",
			                  i,
			                  ((psFwSysData->aui32CrPollAddr[i] & RGXFW_POLL_TYPE_SET)?("set"):("unset")),
			                  psFwSysData->aui32CrPollAddr[i] & ~RGXFW_POLL_TYPE_SET,
			                  psFwSysData->aui32CrPollMask[i]);
		}
	}

}

static void _RGXDumpFWHWRInfo(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
							  void *pvDumpDebugFile,
							  const RGXFWIF_SYSDATA *psFwSysData,
							  const RGXFWIF_HWRINFOBUF *psHWRInfoBuf,
							  PVRSRV_RGXDEV_INFO *psDevInfo)
{
	IMG_BOOL          bAnyLocked = IMG_FALSE;
	IMG_UINT32        dm, i;
	IMG_UINT32        ui32LineSize;
	IMG_CHAR          *pszLine, *pszTemp;
	const IMG_CHAR    *apszDmNames[RGXFWIF_DM_MAX] = {"GP", "TDM", "GEOM", "3D", "CDM", "RAY", "GEOM2", "GEOM3", "GEOM4"};
	const IMG_CHAR    szMsgHeader[] = "Number of HWR: ";
	const IMG_CHAR    szMsgFalse[] = "FALSE(";
	IMG_CHAR          *pszLockupType = "";
	const IMG_UINT32  ui32MsgHeaderCharCount = ARRAY_SIZE(szMsgHeader) - 1; /* size includes the null */
	const IMG_UINT32  ui32MsgFalseCharCount = ARRAY_SIZE(szMsgFalse) - 1;
	IMG_UINT32        ui32HWRRecoveryFlags;
	IMG_UINT32        ui32ReadIndex;

	for (dm = 0; dm < psDevInfo->sDevFeatureCfg.ui32MAXDMCount; dm++)
	{
		if (psHWRInfoBuf->aui32HwrDmLockedUpCount[dm] ||
		    psHWRInfoBuf->aui32HwrDmOverranCount[dm])
		{
			bAnyLocked = IMG_TRUE;
			break;
		}
	}

	if (!PVRSRV_VZ_MODE_IS(GUEST) && !bAnyLocked && (psFwSysData->ui32HWRStateFlags & RGXFWIF_HWR_HARDWARE_OK))
	{
		/* No HWR situation, print nothing */
		return;
	}

	if (PVRSRV_VZ_MODE_IS(GUEST))
	{
		IMG_BOOL bAnyHWROccured = IMG_FALSE;

		for (dm = 0; dm < psDevInfo->sDevFeatureCfg.ui32MAXDMCount; dm++)
		{
			if (psHWRInfoBuf->aui32HwrDmRecoveredCount[dm] != 0 ||
				psHWRInfoBuf->aui32HwrDmLockedUpCount[dm] != 0 ||
				psHWRInfoBuf->aui32HwrDmOverranCount[dm] !=0)
				{
					bAnyHWROccured = IMG_TRUE;
					break;
				}
		}

		if (!bAnyHWROccured)
		{
			return;
		}
	}

	ui32LineSize = sizeof(IMG_CHAR) * (
			ui32MsgHeaderCharCount +
			(psDevInfo->sDevFeatureCfg.ui32MAXDMCount*(	4/*DM name + left parenthesis*/ +
				10/*UINT32 max num of digits*/ +
				1/*slash*/ +
				10/*UINT32 max num of digits*/ +
				3/*right parenthesis + comma + space*/)) +
			ui32MsgFalseCharCount + 1 + (psDevInfo->sDevFeatureCfg.ui32MAXDMCount*6) + 1
				/* 'FALSE(' + ')' + (UINT16 max num + comma) per DM + \0 */
			);

	pszLine = OSAllocMem(ui32LineSize);
	if (pszLine == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
			"%s: Out of mem allocating line string (size: %d)",
			__func__,
			ui32LineSize));
		return;
	}

	OSStringLCopy(pszLine, szMsgHeader, ui32LineSize);
	pszTemp = pszLine + ui32MsgHeaderCharCount;

	for (dm = 0; dm < psDevInfo->sDevFeatureCfg.ui32MAXDMCount; dm++)
	{
		pszTemp += OSSNPrintf(pszTemp,
				4 + 10 + 1 + 10 + 1 + 10 + 1 + 1 + 1 + 1
				/* (name + left parenthesis) + UINT32 + slash + UINT32 + plus + UINT32 + right parenthesis + comma + space + \0 */,
				"%s(%u/%u+%u), ",
				apszDmNames[dm],
				psHWRInfoBuf->aui32HwrDmRecoveredCount[dm],
				psHWRInfoBuf->aui32HwrDmLockedUpCount[dm],
				psHWRInfoBuf->aui32HwrDmOverranCount[dm]);
	}

	OSStringLCat(pszLine, szMsgFalse, ui32LineSize);
	pszTemp += ui32MsgFalseCharCount;

	for (dm = 0; dm < psDevInfo->sDevFeatureCfg.ui32MAXDMCount; dm++)
	{
		pszTemp += OSSNPrintf(pszTemp,
				10 + 1 + 1 /* UINT32 max num + comma + \0 */,
				(dm < psDevInfo->sDevFeatureCfg.ui32MAXDMCount-1 ? "%u," : "%u)"),
				psHWRInfoBuf->aui32HwrDmFalseDetectCount[dm]);
	}

	PVR_DUMPDEBUG_LOG("%s", pszLine);

	OSFreeMem(pszLine);

	/* Print out per HWR info */
	for (dm = 0; dm < psDevInfo->sDevFeatureCfg.ui32MAXDMCount; dm++)
	{
		if (dm == RGXFWIF_DM_GP)
		{
			PVR_DUMPDEBUG_LOG("DM %d (GP)", dm);
		}
		else
		{
			if (!PVRSRV_VZ_MODE_IS(GUEST))
			{
				IMG_UINT32 ui32HWRRecoveryFlags = psFwSysData->aui32HWRRecoveryFlags[dm];
				IMG_CHAR sPerDmHwrDescription[RGX_DEBUG_STR_SIZE];
				sPerDmHwrDescription[0] = '\0';

				if (ui32HWRRecoveryFlags == RGXFWIF_DM_STATE_WORKING)
				{
					OSStringLCopy(sPerDmHwrDescription, " working;", RGX_DEBUG_STR_SIZE);
				}
				else
				{
					DebugCommonFlagStrings(sPerDmHwrDescription, RGX_DEBUG_STR_SIZE,
						asDmState2Description, ARRAY_SIZE(asDmState2Description),
						ui32HWRRecoveryFlags);
				}
				PVR_DUMPDEBUG_LOG("DM %d (HWRflags 0x%08x:%s)", dm, ui32HWRRecoveryFlags, sPerDmHwrDescription);
			}
			else
			{
				PVR_DUMPDEBUG_LOG("DM %d", dm);
			}
		}

		ui32ReadIndex = 0;
		for (i = 0 ; i < RGXFWIF_HWINFO_MAX ; i++)
		{
			IMG_BOOL bPMFault = IMG_FALSE;
			IMG_UINT32 ui32PC;
			IMG_UINT32 ui32PageSize = 0;
			IMG_DEV_PHYADDR sPCDevPAddr = { 0 };
			const RGX_HWRINFO *psHWRInfo = &psHWRInfoBuf->sHWRInfo[ui32ReadIndex];

			if ((psHWRInfo->eDM == dm) && (psHWRInfo->ui32HWRNumber != 0))
			{
				IMG_CHAR aui8RecoveryNum[10+10+1];
				IMG_UINT64 ui64Seconds, ui64Nanoseconds;
				IMG_BOOL bPageFault = IMG_FALSE;
				IMG_DEV_VIRTADDR sFaultDevVAddr;

				/* Split OS timestamp in seconds and nanoseconds */
				ConvertOSTimestampToSAndNS(psHWRInfo->ui64OSTimer, &ui64Seconds, &ui64Nanoseconds);

				ui32HWRRecoveryFlags = psHWRInfo->ui32HWRRecoveryFlags;
				if (ui32HWRRecoveryFlags & RGXFWIF_DM_STATE_GUILTY_LOCKUP) { pszLockupType = ", Guilty Lockup"; }
				else if (ui32HWRRecoveryFlags & RGXFWIF_DM_STATE_INNOCENT_LOCKUP) { pszLockupType = ", Innocent Lockup"; }
				else if (ui32HWRRecoveryFlags & RGXFWIF_DM_STATE_GUILTY_OVERRUNING) { pszLockupType = ", Guilty Overrun"; }
				else if (ui32HWRRecoveryFlags & RGXFWIF_DM_STATE_INNOCENT_OVERRUNING) { pszLockupType = ", Innocent Overrun"; }
				else if (ui32HWRRecoveryFlags & RGXFWIF_DM_STATE_HARD_CONTEXT_SWITCH) { pszLockupType = ", Hard Context Switch"; }
				else if (ui32HWRRecoveryFlags & RGXFWIF_DM_STATE_GPU_ECC_HWR) { pszLockupType = ", GPU ECC HWR"; }

				OSSNPrintf(aui8RecoveryNum, sizeof(aui8RecoveryNum), "Recovery %d:", psHWRInfo->ui32HWRNumber);
				if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, GPU_MULTICORE_SUPPORT))
				{
					PVR_DUMPDEBUG_LOG("  %s Core = %u, PID = %u / %s, frame = %d, HWRTData = 0x%08X, EventStatus = 0x%08X%s",
				                   aui8RecoveryNum,
				                   psHWRInfo->ui32CoreID,
				                   psHWRInfo->ui32PID,
                                                   psHWRInfo->szProcName,
				                   psHWRInfo->ui32FrameNum,
				                   psHWRInfo->ui32ActiveHWRTData,
				                   psHWRInfo->ui32EventStatus,
				                   pszLockupType);
				}
				else
				{
					PVR_DUMPDEBUG_LOG("  %s PID = %u / %s, frame = %d, HWRTData = 0x%08X, EventStatus = 0x%08X%s",
				                   aui8RecoveryNum,
				                   psHWRInfo->ui32PID,
                                                   psHWRInfo->szProcName,
				                   psHWRInfo->ui32FrameNum,
				                   psHWRInfo->ui32ActiveHWRTData,
				                   psHWRInfo->ui32EventStatus,
				                   pszLockupType);
				}

				if (psHWRInfo->eHWErrorCode != RGX_HW_ERR_NA)
				{
					IMG_CHAR sHWDebugInfo[RGX_DEBUG_STR_SIZE] = "";

					_ID2Description(sHWDebugInfo, RGX_DEBUG_STR_SIZE, asHWErrorState, ARRAY_SIZE(asHWErrorState),
						psHWRInfo->eHWErrorCode);
					PVR_DUMPDEBUG_LOG("  HW error code = 0x%X: %s",
									  psHWRInfo->eHWErrorCode, sHWDebugInfo);
				}

				pszTemp = &aui8RecoveryNum[0];
				while (*pszTemp != '\0')
				{
					*pszTemp++ = ' ';
				}

				/* There's currently no time correlation for the Guest OSes on the Firmware so there's no point printing OS Timestamps on Guests */
				if (!PVRSRV_VZ_MODE_IS(GUEST))
				{
					PVR_DUMPDEBUG_LOG("  %s CRTimer = 0x%012"IMG_UINT64_FMTSPECX", OSTimer = %" IMG_UINT64_FMTSPEC ".%09" IMG_UINT64_FMTSPEC ", CyclesElapsed = %" IMG_INT64_FMTSPECd,
									   aui8RecoveryNum,
									   psHWRInfo->ui64CRTimer,
									   ui64Seconds,
									   ui64Nanoseconds,
									   (psHWRInfo->ui64CRTimer-psHWRInfo->ui64CRTimeOfKick)*256);
				}
				else
				{
					PVR_DUMPDEBUG_LOG("  %s CRTimer = 0x%012"IMG_UINT64_FMTSPECX", CyclesElapsed = %" IMG_INT64_FMTSPECd,
									   aui8RecoveryNum,
									   psHWRInfo->ui64CRTimer,
									   (psHWRInfo->ui64CRTimer-psHWRInfo->ui64CRTimeOfKick)*256);
				}

				if (psHWRInfo->ui64CRTimeHWResetFinish != 0)
				{
					if (psHWRInfo->ui64CRTimeFreelistReady != 0)
					{
						/* If ui64CRTimeFreelistReady is less than ui64CRTimeHWResetFinish it means APM kicked in and the time is not valid. */
						if (psHWRInfo->ui64CRTimeHWResetFinish < psHWRInfo->ui64CRTimeFreelistReady)
						{
							PVR_DUMPDEBUG_LOG("  %s PreResetTimeInCycles = %" IMG_INT64_FMTSPECd ", HWResetTimeInCycles = %" IMG_INT64_FMTSPECd ", FreelistReconTimeInCycles = %" IMG_INT64_FMTSPECd ", TotalRecoveryTimeInCycles = %" IMG_INT64_FMTSPECd,
											   aui8RecoveryNum,
											   (psHWRInfo->ui64CRTimeHWResetStart-psHWRInfo->ui64CRTimer)*256,
											   (psHWRInfo->ui64CRTimeHWResetFinish-psHWRInfo->ui64CRTimeHWResetStart)*256,
											   (psHWRInfo->ui64CRTimeFreelistReady-psHWRInfo->ui64CRTimeHWResetFinish)*256,
											   (psHWRInfo->ui64CRTimeFreelistReady-psHWRInfo->ui64CRTimer)*256);
						}
						else
						{
							PVR_DUMPDEBUG_LOG("  %s PreResetTimeInCycles = %" IMG_INT64_FMTSPECd ", HWResetTimeInCycles = %" IMG_INT64_FMTSPECd ", FreelistReconTimeInCycles = <not_timed>, TotalResetTimeInCycles = %" IMG_INT64_FMTSPECd,
											   aui8RecoveryNum,
											   (psHWRInfo->ui64CRTimeHWResetStart-psHWRInfo->ui64CRTimer)*256,
											   (psHWRInfo->ui64CRTimeHWResetFinish-psHWRInfo->ui64CRTimeHWResetStart)*256,
											   (psHWRInfo->ui64CRTimeHWResetFinish-psHWRInfo->ui64CRTimer)*256);
						}
					}
					else
					{
						PVR_DUMPDEBUG_LOG("  %s PreResetTimeInCycles = %" IMG_INT64_FMTSPECd ", HWResetTimeInCycles = %" IMG_INT64_FMTSPECd ", TotalResetTimeInCycles = %" IMG_INT64_FMTSPECd,
										   aui8RecoveryNum,
										   (psHWRInfo->ui64CRTimeHWResetStart-psHWRInfo->ui64CRTimer)*256,
										   (psHWRInfo->ui64CRTimeHWResetFinish-psHWRInfo->ui64CRTimeHWResetStart)*256,
										   (psHWRInfo->ui64CRTimeHWResetFinish-psHWRInfo->ui64CRTimer)*256);
					}
				}

				if (RGX_IS_ERN_SUPPORTED(psDevInfo, 65104))
				{
					PVR_DUMPDEBUG_LOG("    Active PDS DM USCs = 0x%08x", psHWRInfo->ui32PDSActiveDMUSCs);
				}

				if (RGX_IS_ERN_SUPPORTED(psDevInfo, 69700))
				{
					PVR_DUMPDEBUG_LOG("    DMs stalled waiting on PDS Store space = 0x%08x", psHWRInfo->ui32PDSStalledDMs);
				}

				switch (psHWRInfo->eHWRType)
				{
					case RGX_HWRTYPE_ECCFAULT:
					{
						PVR_DUMPDEBUG_LOG("    ECC fault GPU=0x%08x", psHWRInfo->uHWRData.sECCInfo.ui32FaultGPU);
					}
					break;

					case RGX_HWRTYPE_MMUFAULT:
					{
						_RGXDumpRGXMMUFaultStatus(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo,
										&psHWRInfo->uHWRData.sMMUInfo.aui64MMUStatus[0],
										"Core",
										DD_NORMAL_INDENT);

						bPageFault = IMG_TRUE;
						sFaultDevVAddr.uiAddr =   psHWRInfo->uHWRData.sMMUInfo.aui64MMUStatus[0];
						sFaultDevVAddr.uiAddr &=  ~RGX_CR_MMU_FAULT_STATUS1_ADDRESS_CLRMSK;
						sFaultDevVAddr.uiAddr >>= RGX_CR_MMU_FAULT_STATUS1_ADDRESS_SHIFT;
						sFaultDevVAddr.uiAddr <<= 4; /* align shift */
						ui32PC  = (psHWRInfo->uHWRData.sMMUInfo.aui64MMUStatus[0] & ~RGX_CR_MMU_FAULT_STATUS1_CONTEXT_CLRMSK) >>
												   RGX_CR_MMU_FAULT_STATUS1_CONTEXT_SHIFT;
						bPMFault = (ui32PC <= 8);
						sPCDevPAddr.uiAddr = psHWRInfo->uHWRData.sMMUInfo.ui64PCAddress;

					}
					break;

					case RGX_HWRTYPE_MMUMETAFAULT:
					{
						const IMG_PCHAR pszMetaOrRiscv = RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META) ? "Meta" : "RiscV";

						_RGXDumpRGXMMUFaultStatus(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo,
											&psHWRInfo->uHWRData.sMMUInfo.aui64MMUStatus[0],
											pszMetaOrRiscv,
											DD_NORMAL_INDENT);

						bPageFault = IMG_TRUE;
						sFaultDevVAddr.uiAddr =   psHWRInfo->uHWRData.sMMUInfo.aui64MMUStatus[0];
						sFaultDevVAddr.uiAddr &=  ~RGX_CR_MMU_FAULT_STATUS1_ADDRESS_CLRMSK;
						sFaultDevVAddr.uiAddr >>= RGX_CR_MMU_FAULT_STATUS1_ADDRESS_SHIFT;
						sFaultDevVAddr.uiAddr <<= 4; /* align shift */
						sPCDevPAddr.uiAddr = psHWRInfo->uHWRData.sMMUInfo.ui64PCAddress;
					}
					break;

					case RGX_HWRTYPE_POLLFAILURE:
					{
						PVR_DUMPDEBUG_LOG("    T%u polling %s (reg:0x%08X mask:0x%08X last:0x%08X)",
										  psHWRInfo->uHWRData.sPollInfo.ui32ThreadNum,
										  ((psHWRInfo->uHWRData.sPollInfo.ui32CrPollAddr & RGXFW_POLL_TYPE_SET)?("set"):("unset")),
										  psHWRInfo->uHWRData.sPollInfo.ui32CrPollAddr & ~RGXFW_POLL_TYPE_SET,
										  psHWRInfo->uHWRData.sPollInfo.ui32CrPollMask,
										  psHWRInfo->uHWRData.sPollInfo.ui32CrPollLastValue);
					}
					break;

					case RGX_HWRTYPE_OVERRUN:
					case RGX_HWRTYPE_UNKNOWNFAILURE:
					{
						/* Nothing to dump */
					}
					break;

					default:
					{
						PVR_DUMPDEBUG_LOG("    Unknown HWR Info type: 0x%x", psHWRInfo->eHWRType);
					}
					break;
				}

				if (bPageFault)
				{

					FAULT_INFO *psInfo;

					OSLockAcquire(psDevInfo->hDebugFaultInfoLock);

					/* Find the matching Fault Info for this HWRInfo */
					psInfo = &gsFaultInfoLog.asFaults[ui32ReadIndex];

					/* if they do not match, we need to update the psInfo */
					if ((psInfo->ui64CRTimer != psHWRInfo->ui64CRTimer) ||
						(psInfo->sFaultDevVAddr.uiAddr != sFaultDevVAddr.uiAddr))
					{
						MMU_FAULT_DATA *psFaultData = &psInfo->sMMUFaultData;

						psFaultData->eType = MMU_FAULT_TYPE_UNKNOWN;

						if (bPMFault)
						{
							/* PM fault and we dump PC details only */
							psFaultData->eTopLevel = MMU_LEVEL_0;
							psFaultData->eType     = MMU_FAULT_TYPE_PM;
							psFaultData->sLevelData[MMU_LEVEL_0].ui64Address = sPCDevPAddr.uiAddr;
						}
						else
						{
							RGXCheckFaultAddress(psDevInfo, &sFaultDevVAddr, &sPCDevPAddr, psFaultData);
						}

						_RecordFaultInfo(psDevInfo, psInfo,
									sFaultDevVAddr, sPCDevPAddr, psHWRInfo->ui64CRTimer,
									_PageSizeHWToBytes(ui32PageSize));

					}

					_DumpFaultAddressHostView(&psInfo->sMMUFaultData, pfnDumpDebugPrintf, pvDumpDebugFile, DD_NORMAL_INDENT);

					if (GetInfoPageDebugFlagsKM() & DEBUG_FEATURE_PAGE_FAULT_DEBUG_ENABLED)
					{
						_PrintFaultInfo(pfnDumpDebugPrintf, psDevInfo->psDeviceNode, pvDumpDebugFile, psInfo, DD_NORMAL_INDENT);
					}

					OSLockRelease(psDevInfo->hDebugFaultInfoLock);
				}

			}

			if (ui32ReadIndex == RGXFWIF_HWINFO_MAX_FIRST - 1)
				ui32ReadIndex = psHWRInfoBuf->ui32WriteIndex;
			else
				ui32ReadIndex = (ui32ReadIndex + 1) - (ui32ReadIndex / RGXFWIF_HWINFO_LAST_INDEX) * RGXFWIF_HWINFO_MAX_LAST;
		}
	}
}

#if !defined(NO_HARDWARE)

/*!
*******************************************************************************

 @Function	_CheckForPendingPage

 @Description

 Check if the MMU indicates it is blocked on a pending page
 MMU4 does not support pending pages, so return false.

 @Input psDevInfo	 - RGX device info

 @Return   IMG_BOOL      - IMG_TRUE if there is a pending page

******************************************************************************/
static INLINE IMG_BOOL _CheckForPendingPage(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	/* MMU4 doesn't support pending pages */
	return (RGX_GET_FEATURE_VALUE(psDevInfo, MMU_VERSION) < 4) &&
		   (OSReadHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_MMU_ENTRY) & RGX_CR_MMU_ENTRY_PENDING_EN);
}

/*!
*******************************************************************************

 @Function	_GetPendingPageInfo

 @Description

 Get information about the pending page from the MMU status registers

 @Input psDevInfo	 - RGX device info
 @Output psDevVAddr      - The device virtual address of the pending MMU address translation
 @Output pui32CatBase    - The page catalog base

 @Return   void

******************************************************************************/
static void _GetPendingPageInfo(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_DEV_VIRTADDR *psDevVAddr,
								IMG_UINT32 *pui32CatBase)
{
	IMG_UINT64 ui64BIFMMUEntryStatus;

	ui64BIFMMUEntryStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_MMU_ENTRY_STATUS);

	psDevVAddr->uiAddr = (ui64BIFMMUEntryStatus & ~RGX_CR_MMU_ENTRY_STATUS_ADDRESS_CLRMSK);

	*pui32CatBase = (ui64BIFMMUEntryStatus & ~RGX_CR_MMU_ENTRY_STATUS_CONTEXT_ID_CLRMSK) >>
								RGX_CR_MMU_ENTRY_STATUS_CONTEXT_ID_SHIFT;
}

#endif

void RGXDumpRGXDebugSummary(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile,
					PVRSRV_RGXDEV_INFO *psDevInfo,
					IMG_BOOL bRGXPoweredON)
{
	IMG_CHAR *pszState, *pszReason;
	const RGXFWIF_SYSDATA *psFwSysData = psDevInfo->psRGXFWIfFwSysData;
	const RGXFWIF_TRACEBUF *psRGXFWIfTraceBufCtl = psDevInfo->psRGXFWIfTraceBufCtl;
	IMG_UINT32 ui32DriverID;
	const RGXFWIF_RUNTIME_CFG *psRuntimeCfg = psDevInfo->psRGXFWIfRuntimeCfg;
	/* space for the current clock speed and 3 previous */
	RGXFWIF_TIME_CORR asTimeCorrs[4];
	IMG_UINT32 ui32NumClockSpeedChanges;

#if defined(NO_HARDWARE)
	PVR_UNREFERENCED_PARAMETER(bRGXPoweredON);
#else
	if ((bRGXPoweredON) && !PVRSRV_VZ_MODE_IS(GUEST))
	{
		IMG_UINT64	aui64RegValMMUStatus[2];
		const IMG_PCHAR pszMetaOrRiscv = RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META) ? "Meta" : "RiscV";

		aui64RegValMMUStatus[0] = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_MMU_FAULT_STATUS1);
		aui64RegValMMUStatus[1] = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_MMU_FAULT_STATUS2);
		_RGXDumpRGXMMUFaultStatus(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo, &aui64RegValMMUStatus[0], "Core", DD_SUMMARY_INDENT);

		aui64RegValMMUStatus[0] = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_MMU_FAULT_STATUS_META);
		_RGXDumpRGXMMUFaultStatus(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo, &aui64RegValMMUStatus[0], pszMetaOrRiscv, DD_SUMMARY_INDENT);

		if (_CheckForPendingPage(psDevInfo))
		{
			IMG_UINT32 ui32CatBase;
			IMG_DEV_VIRTADDR sDevVAddr;

			PVR_DUMPDEBUG_LOG("MMU Pending page: Yes");

			_GetPendingPageInfo(psDevInfo, &sDevVAddr, &ui32CatBase);

			if (ui32CatBase <= MAX_RESERVED_FW_MMU_CONTEXT)
			{
				PVR_DUMPDEBUG_LOG("Cannot check address on PM cat base %u", ui32CatBase);
			}
			else
			{
				IMG_UINT64 ui64CBaseMapping;
				IMG_DEV_PHYADDR sPCDevPAddr;
				MMU_FAULT_DATA sFaultData;
				IMG_BOOL bIsValid;
				IMG_UINT32 ui32CBaseMapCtxReg;

				if (RGX_GET_FEATURE_VALUE(psDevInfo, HOST_SECURITY_VERSION) > 1)
				{
					ui32CBaseMapCtxReg = RGX_CR_MMU_CBASE_MAPPING_CONTEXT__HOST_SECURITY_GT1_AND_MH_PASID_WIDTH_LT6_AND_MMU_GE4;

					OSWriteUncheckedHWReg32(psDevInfo->pvSecureRegsBaseKM, ui32CBaseMapCtxReg, ui32CatBase);

					ui64CBaseMapping = OSReadUncheckedHWReg64(psDevInfo->pvSecureRegsBaseKM, RGX_CR_MMU_CBASE_MAPPING__HOST_SECURITY_GT1);
					sPCDevPAddr.uiAddr = (((ui64CBaseMapping & ~RGX_CR_MMU_CBASE_MAPPING__HOST_SECURITY_GT1__BASE_ADDR_CLRMSK)
												>> RGX_CR_MMU_CBASE_MAPPING__HOST_SECURITY_GT1__BASE_ADDR_SHIFT)
												<< RGX_CR_MMU_CBASE_MAPPING__HOST_SECURITY_GT1__BASE_ADDR_ALIGNSHIFT);
					bIsValid = !(ui64CBaseMapping & RGX_CR_MMU_CBASE_MAPPING__HOST_SECURITY_GT1__INVALID_EN);
				}
				else
				{
					ui32CBaseMapCtxReg = RGX_CR_MMU_CBASE_MAPPING_CONTEXT;

					OSWriteUncheckedHWReg32(psDevInfo->pvSecureRegsBaseKM, ui32CBaseMapCtxReg, ui32CatBase);

					ui64CBaseMapping = OSReadUncheckedHWReg64(psDevInfo->pvSecureRegsBaseKM, RGX_CR_MMU_CBASE_MAPPING);
					sPCDevPAddr.uiAddr = (((ui64CBaseMapping & ~RGX_CR_MMU_CBASE_MAPPING_BASE_ADDR_CLRMSK)
												>> RGX_CR_MMU_CBASE_MAPPING_BASE_ADDR_SHIFT)
												<< RGX_CR_MMU_CBASE_MAPPING_BASE_ADDR_ALIGNSHIFT);
					bIsValid = !(ui64CBaseMapping & RGX_CR_MMU_CBASE_MAPPING_INVALID_EN);
				}

				PVR_DUMPDEBUG_LOG("Checking device virtual address " IMG_DEV_VIRTADDR_FMTSPEC
							" on cat base %u. PC Addr = 0x%llX is %s",
								(unsigned long long) sDevVAddr.uiAddr,
								ui32CatBase,
								(unsigned long long) sPCDevPAddr.uiAddr,
								bIsValid ? "valid":"invalid");
				RGXCheckFaultAddress(psDevInfo, &sDevVAddr, &sPCDevPAddr, &sFaultData);
				_DumpFaultAddressHostView(&sFaultData, pfnDumpDebugPrintf, pvDumpDebugFile, DD_SUMMARY_INDENT);
			}
		}
	}
#endif /* NO_HARDWARE */

	/* Firmware state */
	switch (OSAtomicRead(&psDevInfo->psDeviceNode->eHealthStatus))
	{
		case PVRSRV_DEVICE_HEALTH_STATUS_OK:  pszState = "OK";  break;
		case PVRSRV_DEVICE_HEALTH_STATUS_NOT_RESPONDING:  pszState = "NOT RESPONDING";  break;
		case PVRSRV_DEVICE_HEALTH_STATUS_DEAD:  pszState = "DEAD";  break;
		case PVRSRV_DEVICE_HEALTH_STATUS_FAULT:  pszState = "FAULT";  break;
		case PVRSRV_DEVICE_HEALTH_STATUS_UNDEFINED:  pszState = "UNDEFINED";  break;
		default:  pszState = "UNKNOWN";  break;
	}

	switch (OSAtomicRead(&psDevInfo->psDeviceNode->eHealthReason))
	{
		case PVRSRV_DEVICE_HEALTH_REASON_NONE:  pszReason = "";  break;
		case PVRSRV_DEVICE_HEALTH_REASON_ASSERTED:  pszReason = " - Asserted";  break;
		case PVRSRV_DEVICE_HEALTH_REASON_POLL_FAILING:  pszReason = " - Poll failing";  break;
		case PVRSRV_DEVICE_HEALTH_REASON_TIMEOUTS:  pszReason = " - Global Event Object timeouts rising";  break;
		case PVRSRV_DEVICE_HEALTH_REASON_QUEUE_CORRUPT:  pszReason = " - KCCB offset invalid";  break;
		case PVRSRV_DEVICE_HEALTH_REASON_QUEUE_STALLED:  pszReason = " - KCCB stalled";  break;
		case PVRSRV_DEVICE_HEALTH_REASON_IDLING:  pszReason = " - Idling";  break;
		case PVRSRV_DEVICE_HEALTH_REASON_RESTARTING:  pszReason = " - Restarting";  break;
		case PVRSRV_DEVICE_HEALTH_REASON_MISSING_INTERRUPTS:  pszReason = " - Missing interrupts";  break;
		default:  pszReason = " - Unknown reason";  break;
	}

#if !defined(NO_HARDWARE)
	/* Determine the type virtualisation support used */
#if defined(RGX_NUM_DRIVERS_SUPPORTED) && (RGX_NUM_DRIVERS_SUPPORTED > 1)
	if (!PVRSRV_VZ_MODE_IS(NATIVE))
	{
#if defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS)
#if defined(SUPPORT_AUTOVZ)
#if defined(SUPPORT_AUTOVZ_HW_REGS)
		PVR_DUMPDEBUG_LOG("RGX Virtualisation type: AutoVz with HW register support");
#else
		PVR_DUMPDEBUG_LOG("RGX Virtualisation type: AutoVz with shared memory");
#endif /* defined(SUPPORT_AUTOVZ_HW_REGS) */
#else
		PVR_DUMPDEBUG_LOG("RGX Virtualisation type: Hypervisor-assisted with static Fw heap allocation");
#endif /* defined(SUPPORT_AUTOVZ) */
#else
		PVR_DUMPDEBUG_LOG("RGX Virtualisation type: Hypervisor-assisted with dynamic Fw heap allocation");
#endif /* defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS) */
	}
#endif /* (RGX_NUM_DRIVERS_SUPPORTED > 1) */

#if defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS) || (defined(RGX_NUM_DRIVERS_SUPPORTED) && (RGX_NUM_DRIVERS_SUPPORTED > 1))
	if (!PVRSRV_VZ_MODE_IS(NATIVE))
	{
		RGXFWIF_CONNECTION_FW_STATE eFwState = KM_GET_FW_CONNECTION(psDevInfo);
		RGXFWIF_CONNECTION_OS_STATE eOsState = KM_GET_OS_CONNECTION(psDevInfo);

		PVR_DUMPDEBUG_LOG("RGX Virtualisation firmware connection state: %s (Fw=%s; OS=%s)",
						  ((eFwState == RGXFW_CONNECTION_FW_ACTIVE) && (eOsState == RGXFW_CONNECTION_OS_ACTIVE)) ? ("UP") : ("DOWN"),
						  (eFwState < RGXFW_CONNECTION_FW_STATE_COUNT) ? (apszFwOsStateName[eFwState]) : ("invalid"),
						  (eOsState < RGXFW_CONNECTION_OS_STATE_COUNT) ? (apszFwOsStateName[eOsState]) : ("invalid"));

	}
#endif

#if defined(SUPPORT_AUTOVZ) && defined(RGX_NUM_DRIVERS_SUPPORTED) && (RGX_NUM_DRIVERS_SUPPORTED > 1)
	if (!PVRSRV_VZ_MODE_IS(NATIVE))
	{
		IMG_UINT32 ui32FwAliveTS = KM_GET_FW_ALIVE_TOKEN(psDevInfo);
		IMG_UINT32 ui32OsAliveTS = KM_GET_OS_ALIVE_TOKEN(psDevInfo);

		PVR_DUMPDEBUG_LOG("RGX Virtualisation watchdog timestamps (in GPU timer ticks): Fw=%u; OS=%u; diff(FW, OS) = %u",
						  ui32FwAliveTS, ui32OsAliveTS, ui32FwAliveTS - ui32OsAliveTS);
	}
#endif
#endif /* !defined(NO_HARDWARE) */

	if (!PVRSRV_VZ_MODE_IS(GUEST))
	{
		IMG_CHAR sHwrStateDescription[RGX_DEBUG_STR_SIZE];
		IMG_BOOL bDriverIsolationEnabled = IMG_FALSE;
		IMG_UINT32 ui32HostIsolationGroup;

		if (psFwSysData == NULL)
		{
			/* can't dump any more information */
			PVR_DUMPDEBUG_LOG("RGX FW State: %s%s", pszState, pszReason);
			return;
		}

		sHwrStateDescription[0] = '\0';

		DebugCommonFlagStrings(sHwrStateDescription, RGX_DEBUG_STR_SIZE,
			asHwrState2Description, ARRAY_SIZE(asHwrState2Description),
			psFwSysData->ui32HWRStateFlags);
		PVR_DUMPDEBUG_LOG("RGX FW State: %s%s (HWRState 0x%08x:%s)", pszState, pszReason, psFwSysData->ui32HWRStateFlags, sHwrStateDescription);
		PVR_DUMPDEBUG_LOG("RGX FW Power State: %s (APM %s: %d ok, %d denied, %d non-idle, %d retry, %d other, %d total. Latency: %u ms)",
		                  (psFwSysData->ePowState < ARRAY_SIZE(pszPowStateName) ? pszPowStateName[psFwSysData->ePowState] : "???"),
		                  (psDevInfo->pvAPMISRData)?"enabled":"disabled",
		                  psDevInfo->ui32ActivePMReqOk - psDevInfo->ui32ActivePMReqNonIdle,
		                  psDevInfo->ui32ActivePMReqDenied,
		                  psDevInfo->ui32ActivePMReqNonIdle,
		                  psDevInfo->ui32ActivePMReqRetry,
		                  psDevInfo->ui32ActivePMReqTotal -
		                  psDevInfo->ui32ActivePMReqOk -
		                  psDevInfo->ui32ActivePMReqDenied -
		                  psDevInfo->ui32ActivePMReqRetry -
		                  psDevInfo->ui32ActivePMReqNonIdle,
		                  psDevInfo->ui32ActivePMReqTotal,
		                  psRuntimeCfg->ui32ActivePMLatencyms);

		ui32NumClockSpeedChanges = (IMG_UINT32) OSAtomicRead(&psDevInfo->psDeviceNode->iNumClockSpeedChanges);
		RGXGetTimeCorrData(psDevInfo->psDeviceNode, asTimeCorrs, ARRAY_SIZE(asTimeCorrs));

		PVR_DUMPDEBUG_LOG("RGX DVFS: %u frequency changes. "
		                  "Current frequency: %u.%03u MHz (sampled at %" IMG_UINT64_FMTSPEC " ns). "
		                  "FW frequency: %u.%03u MHz.",
		                  ui32NumClockSpeedChanges,
		                  asTimeCorrs[0].ui32CoreClockSpeed / 1000000,
		                  (asTimeCorrs[0].ui32CoreClockSpeed / 1000) % 1000,
		                  asTimeCorrs[0].ui64OSTimeStamp,
		                  psRuntimeCfg->ui32CoreClockSpeed / 1000000,
		                  (psRuntimeCfg->ui32CoreClockSpeed / 1000) % 1000);
		if (ui32NumClockSpeedChanges > 0)
		{
			PVR_DUMPDEBUG_LOG("          Previous frequencies: %u.%03u, %u.%03u, %u.%03u MHz (Sampled at "
							"%" IMG_UINT64_FMTSPEC ", %" IMG_UINT64_FMTSPEC ", %" IMG_UINT64_FMTSPEC ")",
												asTimeCorrs[1].ui32CoreClockSpeed / 1000000,
												(asTimeCorrs[1].ui32CoreClockSpeed / 1000) % 1000,
												asTimeCorrs[2].ui32CoreClockSpeed / 1000000,
												(asTimeCorrs[2].ui32CoreClockSpeed / 1000) % 1000,
												asTimeCorrs[3].ui32CoreClockSpeed / 1000000,
												(asTimeCorrs[3].ui32CoreClockSpeed / 1000) % 1000,
												asTimeCorrs[1].ui64OSTimeStamp,
												asTimeCorrs[2].ui64OSTimeStamp,
												asTimeCorrs[3].ui64OSTimeStamp);
		}

		ui32HostIsolationGroup = psDevInfo->psRGXFWIfRuntimeCfg->aui32DriverIsolationGroup[RGXFW_HOST_DRIVER_ID];

		FOREACH_SUPPORTED_DRIVER(ui32DriverID)
		{
			RGXFWIF_OS_RUNTIME_FLAGS sFwRunFlags = psFwSysData->asOsRuntimeFlagsMirror[ui32DriverID];
			IMG_UINT32 ui32IsolationGroup = psDevInfo->psRGXFWIfRuntimeCfg->aui32DriverIsolationGroup[ui32DriverID];
			IMG_BOOL bMTSEnabled = IMG_FALSE;

#if !defined(NO_HARDWARE)
			if (bRGXPoweredON)
			{
				bMTSEnabled = (!RGX_IS_FEATURE_SUPPORTED(psDevInfo, GPU_VIRTUALISATION)) ? IMG_TRUE :
								((OSReadHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_MTS_SCHEDULE_ENABLE) & BIT(ui32DriverID)) != 0);
			}
#endif

			PVR_DUMPDEBUG_LOG("RGX FW OS %u - State: %s; Freelists: %s%s; Priority: %u; Isolation group: %u; %s", ui32DriverID,
							  apszFwOsStateName[sFwRunFlags.bfOsState],
							  (sFwRunFlags.bfFLOk) ? "Ok" : "Not Ok",
							  (sFwRunFlags.bfFLGrowPending) ? "; Grow Request Pending" : "",
							  psDevInfo->psRGXFWIfRuntimeCfg->aui32DriverPriority[ui32DriverID],
							  ui32IsolationGroup,
							  (bMTSEnabled) ? "MTS on;" : "MTS off;"
							 );

			if (ui32IsolationGroup != ui32HostIsolationGroup)
			{
				bDriverIsolationEnabled = IMG_TRUE;
			}
		}

#if defined(PVR_ENABLE_PHR)
		{
			IMG_CHAR sPHRConfigDescription[RGX_DEBUG_STR_SIZE];

			sPHRConfigDescription[0] = '\0';
			DebugCommonFlagStrings(sPHRConfigDescription, RGX_DEBUG_STR_SIZE,
			                   asPHRConfig2Description, ARRAY_SIZE(asPHRConfig2Description),
			                   BIT_ULL(psDevInfo->psRGXFWIfRuntimeCfg->ui32PHRMode));

			PVR_DUMPDEBUG_LOG("RGX PHR configuration: (%d) %s", psDevInfo->psRGXFWIfRuntimeCfg->ui32PHRMode, sPHRConfigDescription);
		}
#endif

		if (bDriverIsolationEnabled)
		{
			PVR_DUMPDEBUG_LOG("RGX Hard Context Switch deadline: %u ms", psDevInfo->psRGXFWIfRuntimeCfg->ui32HCSDeadlineMS);
		}

		_RGXDumpFWAssert(pfnDumpDebugPrintf, pvDumpDebugFile, psRGXFWIfTraceBufCtl);
		_RGXDumpFWFaults(pfnDumpDebugPrintf, pvDumpDebugFile, psFwSysData);
		_RGXDumpFWPoll(pfnDumpDebugPrintf, pvDumpDebugFile, psFwSysData);
	}
	else
	{
		PVR_DUMPDEBUG_LOG("RGX FW State: Unavailable under Guest Mode of operation");
		PVR_DUMPDEBUG_LOG("RGX FW Power State: Unavailable under Guest Mode of operation");
	}

	_RGXDumpFWHWRInfo(pfnDumpDebugPrintf, pvDumpDebugFile, psFwSysData, psDevInfo->psRGXFWIfHWRInfoBufCtl, psDevInfo);

#if defined(SUPPORT_RGXFW_STATS_FRAMEWORK)
	/* Dump all non-zero values in lines of 8... */
	{
		IMG_CHAR    pszLine[(9*RGXFWIF_STATS_FRAMEWORK_LINESIZE)+1];
		const IMG_UINT32 *pui32FWStatsBuf = psFwSysData->aui32FWStatsBuf;
		IMG_UINT32  ui32Index1, ui32Index2;

		PVR_DUMPDEBUG_LOG("STATS[START]: RGXFWIF_STATS_FRAMEWORK_MAX=%d", RGXFWIF_STATS_FRAMEWORK_MAX);
		for (ui32Index1 = 0;  ui32Index1 < RGXFWIF_STATS_FRAMEWORK_MAX;  ui32Index1 += RGXFWIF_STATS_FRAMEWORK_LINESIZE)
		{
			IMG_UINT32  ui32OrOfValues = 0;
			IMG_CHAR    *pszBuf = pszLine;

			/* Print all values in this line and skip if all zero... */
			for (ui32Index2 = 0;  ui32Index2 < RGXFWIF_STATS_FRAMEWORK_LINESIZE;  ui32Index2++)
			{
				ui32OrOfValues |= pui32FWStatsBuf[ui32Index1+ui32Index2];
				OSSNPrintf(pszBuf, 9 + 1, " %08x", pui32FWStatsBuf[ui32Index1+ui32Index2]);
				pszBuf += 9; /* write over the '\0' */
			}

			if (ui32OrOfValues != 0)
			{
				PVR_DUMPDEBUG_LOG("STATS[%08x]:%s", ui32Index1, pszLine);
			}
		}
		PVR_DUMPDEBUG_LOG("STATS[END]");
	}
#endif
}

#if !defined(NO_HARDWARE)
static void _RGXDumpMetaSPExtraDebugInfo(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
						void *pvDumpDebugFile,
						PVRSRV_RGXDEV_INFO *psDevInfo)
{
/* List of extra META Slave Port debug registers */
/* Order in these two initialisers must match */
#define RGX_META_SP_EXTRA_DEBUG \
			X(RGX_CR_META_SP_MSLVCTRL0) \
			X(RGX_CR_META_SP_MSLVCTRL1) \
			X(RGX_CR_META_SP_MSLVDATAX) \
			X(RGX_CR_META_SP_MSLVIRQSTATUS) \
			X(RGX_CR_META_SP_MSLVIRQENABLE) \
			X(RGX_CR_META_SP_MSLVIRQLEVEL)

#define RGX_META_SP_EXTRA_DEBUG__HOST_SECURITY_V1_AND_METAREG_UNPACKED_ACCESSES \
			X(RGX_CR_META_SP_MSLVCTRL0__HOST_SECURITY_V1_AND_METAREG_UNPACKED) \
			X(RGX_CR_META_SP_MSLVCTRL1__HOST_SECURITY_V1_AND_METAREG_UNPACKED) \
			X(RGX_CR_META_SP_MSLVDATAX__HOST_SECURITY_V1_AND_METAREG_UNPACKED) \
			X(RGX_CR_META_SP_MSLVIRQSTATUS__HOST_SECURITY_V1_AND_METAREG_UNPACKED) \
			X(RGX_CR_META_SP_MSLVIRQENABLE__HOST_SECURITY_V1_AND_METAREG_UNPACKED) \
			X(RGX_CR_META_SP_MSLVIRQLEVEL__HOST_SECURITY_V1_AND_METAREG_UNPACKED)

#define RGX_META_SP_EXTRA_DEBUG__HOST_SECURITY_GT1_AND_METAREG_UNPACKED_ACCESSES \
			X(RGX_CR_META_SP_MSLVCTRL0__HOST_SECURITY_GT1_AND_METAREG_UNPACKED) \
			X(RGX_CR_META_SP_MSLVCTRL1__HOST_SECURITY_GT1_AND_METAREG_UNPACKED) \
			X(RGX_CR_META_SP_MSLVDATAX__HOST_SECURITY_GT1_AND_METAREG_UNPACKED) \
			X(RGX_CR_META_SP_MSLVIRQSTATUS__HOST_SECURITY_GT1_AND_METAREG_UNPACKED) \
			X(RGX_CR_META_SP_MSLVIRQENABLE__HOST_SECURITY_GT1_AND_METAREG_UNPACKED) \
			X(RGX_CR_META_SP_MSLVIRQLEVEL__HOST_SECURITY_GT1_AND_METAREG_UNPACKED)

	IMG_UINT32 ui32Idx;
	IMG_UINT32 ui32RegVal;
	IMG_UINT32 ui32RegAddr;

	const IMG_UINT32* pui32DebugRegAddr;
	const IMG_UINT32 aui32DebugRegAddr[] = {
#define X(A) A,
		RGX_META_SP_EXTRA_DEBUG
#undef X
		};
	const IMG_UINT32 aui32DebugRegAddrUAHSV1[] = {
#define X(A) A,
		RGX_META_SP_EXTRA_DEBUG__HOST_SECURITY_V1_AND_METAREG_UNPACKED_ACCESSES
#undef X
		};

	const IMG_UINT32 aui32DebugRegAddrUAHSGT1[] = {
#define X(A) A,
		RGX_META_SP_EXTRA_DEBUG__HOST_SECURITY_GT1_AND_METAREG_UNPACKED_ACCESSES
#undef X
		};

	const IMG_CHAR* apszDebugRegName[] = {
#define X(A) #A,
	RGX_META_SP_EXTRA_DEBUG
#undef X
	};

	PVR_DUMPDEBUG_LOG("META Slave Port extra debug:");

	/* array of register offset values depends on feature. But don't augment names in apszDebugRegName */
	PVR_ASSERT(sizeof(aui32DebugRegAddrUAHSGT1) == sizeof(aui32DebugRegAddr));
	PVR_ASSERT(sizeof(aui32DebugRegAddrUAHSV1) == sizeof(aui32DebugRegAddr));
	pui32DebugRegAddr = RGX_IS_FEATURE_SUPPORTED(psDevInfo, META_REGISTER_UNPACKED_ACCESSES) ?
						((RGX_GET_FEATURE_VALUE(psDevInfo, HOST_SECURITY_VERSION) > 1) ? (aui32DebugRegAddrUAHSGT1) : (aui32DebugRegAddrUAHSV1)) : aui32DebugRegAddr;

	/* dump set of Slave Port debug registers */
	for (ui32Idx = 0; ui32Idx < sizeof(aui32DebugRegAddr)/sizeof(IMG_UINT32); ui32Idx++)
	{
		const IMG_CHAR* pszRegName = apszDebugRegName[ui32Idx];

		ui32RegAddr = pui32DebugRegAddr[ui32Idx];
		ui32RegVal = OSReadUncheckedHWReg32(psDevInfo->pvSecureRegsBaseKM, ui32RegAddr);
		PVR_DUMPDEBUG_LOG("  * %s: 0x%8.8X", pszRegName, ui32RegVal);
	}

}
#endif /* !defined(NO_HARDWARE) */

/*
 *  Array of all the Firmware Trace log IDs used to convert the trace data.
 */
typedef struct _TRACEBUF_LOG_ {
	RGXFW_LOG_SFids	eSFId;
	const IMG_CHAR	*pszName;
	const IMG_CHAR	*pszFmt;
	IMG_UINT32		ui32ArgNum;
} TRACEBUF_LOG;

static const TRACEBUF_LOG aLogDefinitions[] =
{
#define X(a, b, c, d, e) {RGXFW_LOG_CREATESFID(a,b,e), #c, d, e},
	RGXFW_LOG_SFIDLIST
#undef X
};

#define NARGS_MASK ~(0xF<<16)
static IMG_BOOL _FirmwareTraceIntegrityCheck(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
						void *pvDumpDebugFile)
{
	const TRACEBUF_LOG *psLogDef = &aLogDefinitions[0];
	IMG_BOOL bIntegrityOk = IMG_TRUE;

	/*
	 * For every log ID, check the format string and number of arguments is valid.
	 */
	while (psLogDef->eSFId != RGXFW_SF_LAST)
	{
		const TRACEBUF_LOG *psLogDef2;
		const IMG_CHAR *pszString;
		IMG_UINT32 ui32Count;

		/*
		 * Check the number of arguments matches the number of '%' in the string and
		 * check that no string uses %s which is not supported as it requires a
		 * pointer to memory that is not going to be valid.
		 */
		pszString = psLogDef->pszFmt;
		ui32Count = 0;

		while (*pszString != '\0')
		{
			if (*pszString++ == '%')
			{
				ui32Count++;
				if (*pszString == 's')
				{
					bIntegrityOk = IMG_FALSE;
					PVR_DUMPDEBUG_LOG("Integrity Check FAIL: %s has an unsupported type not recognized (fmt: %%%c). Please fix.",
									  psLogDef->pszName, *pszString);
				}
				else if (*pszString == '%')
				{
					/* Double % is a printable % sign and not a format string... */
					ui32Count--;
				}
			}
		}

		if (ui32Count != psLogDef->ui32ArgNum)
		{
			bIntegrityOk = IMG_FALSE;
			PVR_DUMPDEBUG_LOG("Integrity Check FAIL: %s has %d arguments but only %d are specified. Please fix.",
			                  psLogDef->pszName, ui32Count, psLogDef->ui32ArgNum);
		}

		/* RGXDumpFirmwareTrace() has a hardcoded limit of supporting up to 20 arguments... */
		if (ui32Count > 20)
		{
			bIntegrityOk = IMG_FALSE;
			PVR_DUMPDEBUG_LOG("Integrity Check FAIL: %s has %d arguments but a maximum of 20 are supported. Please fix.",
			                  psLogDef->pszName, ui32Count);
		}

		/* Check the id number is unique (don't take into account the number of arguments) */
		ui32Count = 0;
		psLogDef2 = &aLogDefinitions[0];

		while (psLogDef2->eSFId != RGXFW_SF_LAST)
		{
			if ((psLogDef->eSFId & NARGS_MASK) == (psLogDef2->eSFId & NARGS_MASK))
			{
				ui32Count++;
			}
			psLogDef2++;
		}

		if (ui32Count != 1)
		{
			bIntegrityOk = IMG_FALSE;
			PVR_DUMPDEBUG_LOG("Integrity Check FAIL: %s id %x is not unique, there are %d more. Please fix.",
			                  psLogDef->pszName, psLogDef->eSFId, ui32Count - 1);
		}

		/* Move to the next log ID... */
		psLogDef++;
	}

	return bIntegrityOk;
}

typedef struct {
	IMG_UINT16     ui16Mask;
	const IMG_CHAR *pszStr;
} RGXFWT_DEBUG_INFO_MSKSTR; /* pair of bit mask and debug info message string */


/*!
*******************************************************************************

 @Function	RGXPrepareExtraDebugInfo

 @Description

 Prepares debug info string by decoding ui16DebugInfo value passed

 @Input pszBuffer	 - pointer to debug info string buffer

 @Return   void

******************************************************************************/
static void RGXPrepareExtraDebugInfo(IMG_CHAR *pszBuffer, IMG_UINT32 ui32BufferSize, IMG_UINT16 ui16DebugInfo)
{
	const RGXFWT_DEBUG_INFO_MSKSTR aDebugInfoMskStr[] =
	{
#define X(a, b) {a, b},
		RGXFWT_DEBUG_INFO_MSKSTRLIST
#undef X
	};

	IMG_UINT32 ui32NumFields = sizeof(aDebugInfoMskStr)/sizeof(RGXFWT_DEBUG_INFO_MSKSTR);
	IMG_UINT32 i;
	IMG_BOOL   bHasExtraDebugInfo = IMG_FALSE;

	/* Add prepend string */
	OSStringLCopy(pszBuffer, RGXFWT_DEBUG_INFO_STR_PREPEND, ui32BufferSize);

	/* Add debug info strings */
	for (i = 0; i < ui32NumFields; i++)
	{
		if (ui16DebugInfo & aDebugInfoMskStr[i].ui16Mask)
		{
			if (bHasExtraDebugInfo)
			{
				OSStringLCat(pszBuffer, ", ", ui32BufferSize); /* Add comma separator */
			}
			OSStringLCat(pszBuffer, aDebugInfoMskStr[i].pszStr, ui32BufferSize);
			bHasExtraDebugInfo = IMG_TRUE;
		}
	}

	/* Add append string */
	OSStringLCat(pszBuffer, RGXFWT_DEBUG_INFO_STR_APPEND, ui32BufferSize);
}

void RGXDumpFirmwareTrace(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile,
				PVRSRV_RGXDEV_INFO  *psDevInfo)
{
	RGXFWIF_TRACEBUF  *psRGXFWIfTraceBufCtl = psDevInfo->psRGXFWIfTraceBufCtl;
	static IMG_BOOL   bIntegrityCheckPassed = IMG_FALSE;

	/* Check that the firmware trace is correctly defined... */
	if (!bIntegrityCheckPassed)
	{
		bIntegrityCheckPassed = _FirmwareTraceIntegrityCheck(pfnDumpDebugPrintf, pvDumpDebugFile);
		if (!bIntegrityCheckPassed)
		{
			return;
		}
	}

	/* Dump FW trace information... */
	if (psRGXFWIfTraceBufCtl != NULL)
	{
		IMG_UINT32  tid;
		IMG_UINT32  ui32TraceBufSizeInDWords = psRGXFWIfTraceBufCtl->ui32TraceBufSizeInDWords;

		/* Print the log type settings... */
		if (psRGXFWIfTraceBufCtl->ui32LogType & RGXFWIF_LOG_TYPE_GROUP_MASK)
		{
			PVR_DUMPDEBUG_LOG("Debug log type: %s ( " RGXFWIF_LOG_ENABLED_GROUPS_LIST_PFSPEC ")",
							  ((psRGXFWIfTraceBufCtl->ui32LogType & RGXFWIF_LOG_TYPE_TRACE)?("trace"):("tbi")),
							  RGXFWIF_LOG_ENABLED_GROUPS_LIST(psRGXFWIfTraceBufCtl->ui32LogType)
							  );
		}
		else
		{
			PVR_DUMPDEBUG_LOG("Debug log type: none");
		}

		/* Print the decoded log for each thread... */
		for (tid = 0;  tid < RGXFW_THREAD_NUM;  tid++)
		{
			volatile IMG_UINT32  *pui32FWWrapCount = &(psRGXFWIfTraceBufCtl->sTraceBuf[tid].ui32WrapCount);
			volatile IMG_UINT32  *pui32FWTracePtr  = &(psRGXFWIfTraceBufCtl->sTraceBuf[tid].ui32TracePointer);
			IMG_UINT32           *pui32TraceBuf    = psRGXFWIfTraceBufCtl->sTraceBuf[tid].pui32TraceBuffer;
			IMG_UINT32           ui32HostWrapCount = *pui32FWWrapCount;
			IMG_UINT32           ui32HostTracePtr  = *pui32FWTracePtr;
			IMG_UINT32           ui32Count         = 0;

			if (pui32TraceBuf == NULL)
			{
				/* trace buffer not yet allocated */
				continue;
			}

			while (ui32Count < ui32TraceBufSizeInDWords)
			{
				IMG_UINT32  ui32Data, ui32DataToId;

				/* Find the first valid log ID, skipping whitespace... */
				do
				{
					ui32Data     = pui32TraceBuf[ui32HostTracePtr];
					ui32DataToId = idToStringID(ui32Data, SFs);

					/* If an unrecognized id is found it may be inconsistent data or a firmware trace error. */
					if (ui32DataToId == RGXFW_SF_LAST  &&  RGXFW_LOG_VALIDID(ui32Data))
					{
						PVR_DUMPDEBUG_LOG("WARNING: Unrecognized id (%x). From here on the trace might be wrong!", ui32Data);
					}

					/* Update the trace pointer... */
					ui32HostTracePtr++;
					if (ui32HostTracePtr >= ui32TraceBufSizeInDWords)
					{
						ui32HostTracePtr = 0;
						ui32HostWrapCount++;
					}
					ui32Count++;
				} while ((RGXFW_SF_LAST == ui32DataToId)  &&
				         ui32Count < ui32TraceBufSizeInDWords);

				if (ui32Count < ui32TraceBufSizeInDWords)
				{
					IMG_CHAR   szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN] = "%" IMG_UINT64_FMTSPEC ":T%u-%s> ";
					IMG_CHAR   szDebugInfoBuffer[RGXFWT_DEBUG_INFO_STR_MAXLEN] = "";
					IMG_UINT64 ui64Timestamp;
					IMG_UINT16 ui16DebugInfo;

					/* If we hit the ASSERT message then this is the end of the log... */
					if (ui32Data == RGXFW_SF_MAIN_ASSERT_FAILED)
					{
						PVR_DUMPDEBUG_LOG("ASSERTION %s failed at %s:%u",
										  psRGXFWIfTraceBufCtl->sTraceBuf[tid].sAssertBuf.szInfo,
										  psRGXFWIfTraceBufCtl->sTraceBuf[tid].sAssertBuf.szPath,
										  psRGXFWIfTraceBufCtl->sTraceBuf[tid].sAssertBuf.ui32LineNum);
						break;
					}

					ui64Timestamp = (IMG_UINT64)(pui32TraceBuf[(ui32HostTracePtr + 0) % ui32TraceBufSizeInDWords]) << 32 |
					                (IMG_UINT64)(pui32TraceBuf[(ui32HostTracePtr + 1) % ui32TraceBufSizeInDWords]);

					ui16DebugInfo = (IMG_UINT16) ((ui64Timestamp & ~RGXFWT_TIMESTAMP_DEBUG_INFO_CLRMSK) >> RGXFWT_TIMESTAMP_DEBUG_INFO_SHIFT);
					ui64Timestamp = (ui64Timestamp & ~RGXFWT_TIMESTAMP_TIME_CLRMSK) >> RGXFWT_TIMESTAMP_TIME_SHIFT;

					/*
					 * Print the trace string and provide up to 20 arguments which
					 * printf function will be able to use. We have already checked
					 * that no string uses more than this.
					 */
					OSStringLCat(szBuffer, SFs[ui32DataToId].psName, PVR_MAX_DEBUG_MESSAGE_LEN);

					/* Check and append any extra debug info available */
					if (ui16DebugInfo)
					{
						/* Prepare debug info string */
						RGXPrepareExtraDebugInfo(szDebugInfoBuffer, RGXFWT_DEBUG_INFO_STR_MAXLEN, ui16DebugInfo);

						/* Append debug info string */
						OSStringLCat(szBuffer, szDebugInfoBuffer, PVR_MAX_DEBUG_MESSAGE_LEN);
					}

					PVR_DUMPDEBUG_LOG(szBuffer, ui64Timestamp, tid, groups[RGXFW_SF_GID(ui32Data)],
									  pui32TraceBuf[(ui32HostTracePtr +  2) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32HostTracePtr +  3) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32HostTracePtr +  4) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32HostTracePtr +  5) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32HostTracePtr +  6) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32HostTracePtr +  7) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32HostTracePtr +  8) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32HostTracePtr +  9) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32HostTracePtr + 10) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32HostTracePtr + 11) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32HostTracePtr + 12) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32HostTracePtr + 13) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32HostTracePtr + 14) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32HostTracePtr + 15) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32HostTracePtr + 16) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32HostTracePtr + 17) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32HostTracePtr + 18) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32HostTracePtr + 19) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32HostTracePtr + 20) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32HostTracePtr + 21) % ui32TraceBufSizeInDWords]);

					/* Update the trace pointer... */
					ui32HostTracePtr = ui32HostTracePtr + 2 + RGXFW_SF_PARAMNUM(ui32Data);
					if (ui32HostTracePtr >= ui32TraceBufSizeInDWords)
					{
						ui32HostTracePtr = ui32HostTracePtr % ui32TraceBufSizeInDWords;
						ui32HostWrapCount++;
					}
					ui32Count = (ui32Count + 2 + RGXFW_SF_PARAMNUM(ui32Data));

					/* Has the FW trace buffer overtaken the host pointer during the last line printed??? */
					if ((*pui32FWWrapCount > ui32HostWrapCount) ||
					    ((*pui32FWWrapCount == ui32HostWrapCount) && (*pui32FWTracePtr > ui32HostTracePtr)))
					{
						/* Move forward to the oldest entry again... */
						PVR_DUMPDEBUG_LOG(". . .");
						ui32HostWrapCount = *pui32FWWrapCount;
						ui32HostTracePtr  = *pui32FWTracePtr;
					}
				}
			}
		}
	}
}

static const IMG_CHAR *_RGXGetDebugDevStateString(PVRSRV_DEVICE_STATE eDevState)
{
	switch (eDevState)
	{
		case PVRSRV_DEVICE_STATE_CREATING:
			return "Creating";
		case PVRSRV_DEVICE_STATE_CREATED:
			return "Initialising";
		case PVRSRV_DEVICE_STATE_ACTIVE:
			return "Active";
		case PVRSRV_DEVICE_STATE_DEINIT:
			return "De-initialising";
		case PVRSRV_DEVICE_STATE_BAD:
			return "Bad";
		case PVRSRV_DEVICE_STATE_UNDEFINED:
			PVR_ASSERT(!"Device has undefined state");
			__fallthrough;
		default:
			return "Unknown";
	}
}

static const IMG_CHAR* _RGXGetDebugDevPowerStateString(PVRSRV_DEV_POWER_STATE ePowerState)
{
	switch (ePowerState)
	{
		case PVRSRV_DEV_POWER_STATE_DEFAULT: return "DEFAULT";
		case PVRSRV_DEV_POWER_STATE_OFF: return "OFF";
		case PVRSRV_DEV_POWER_STATE_ON: return "ON";
		default: return "UNKNOWN";
	}
}

/* Helper macros to emit data */
#define REG32_FMTSPEC   "%-30s: 0x%08X"
#define REG64_FMTSPEC   "%-30s: 0x%016" IMG_UINT64_FMTSPECX
#define DDLOG32(R)      PVR_DUMPDEBUG_LOG(REG32_FMTSPEC, #R, OSReadHWReg32(pvRegsBaseKM, RGX_CR_##R));
#define DDLOG64(R)      PVR_DUMPDEBUG_LOG(REG64_FMTSPEC, #R, OSReadHWReg64(pvRegsBaseKM, RGX_CR_##R));
#define DDLOG32_DPX(R)  PVR_DUMPDEBUG_LOG(REG32_FMTSPEC, #R, OSReadHWReg32(pvRegsBaseKM, DPX_CR_##R));
#define DDLOG64_DPX(R)  PVR_DUMPDEBUG_LOG(REG64_FMTSPEC, #R, OSReadHWReg64(pvRegsBaseKM, DPX_CR_##R));
#define DDLOGVAL32(S,V) PVR_DUMPDEBUG_LOG(REG32_FMTSPEC, S, V);

#if !defined(NO_HARDWARE)
static PVRSRV_ERROR RGXDumpRISCVState(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
									  void *pvDumpDebugFile,
									  PVRSRV_RGXDEV_INFO *psDevInfo)
{
	void __iomem *pvRegsBaseKM = psDevInfo->pvRegsBaseKM;
	RGXRISCVFW_STATE sRiscvState;
	const IMG_CHAR *pszException;
	PVRSRV_ERROR eError;

	/* Limit dump to what is currently being used */
	DDLOG64(FWCORE_ADDR_REMAP_CONFIG4);
	DDLOG64(FWCORE_ADDR_REMAP_CONFIG5);
	DDLOG64(FWCORE_ADDR_REMAP_CONFIG6);
	DDLOG64(FWCORE_ADDR_REMAP_CONFIG12);
	DDLOG64(FWCORE_ADDR_REMAP_CONFIG13);
	DDLOG64(FWCORE_ADDR_REMAP_CONFIG14);

	PVR_DUMPDEBUG_LOG("---- [ RISC-V internal state ] ----");

#if defined(SUPPORT_VALIDATION) || defined(SUPPORT_RISCV_GDB)
	if (RGXRiscvIsHalted(psDevInfo))
	{
		/* Avoid resuming the RISC-V FW as most operations
		 * on the debug module require a halted core */
		PVR_DUMPDEBUG_LOG("(skipping as RISC-V found halted)");
		return PVRSRV_OK;
	}
#endif

	eError = RGXRiscvHalt(psDevInfo);
	PVR_GOTO_IF_ERROR(eError, _RISCVDMError);

#define X(name, address)												\
	eError = RGXRiscvReadReg(psDevInfo, address, &sRiscvState.name);	\
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXRiscvReadReg", _RISCVDMError);	\
	DDLOGVAL32(#name, sRiscvState.name);

	RGXRISCVFW_DEBUG_DUMP_REGISTERS
#undef X

	eError = RGXRiscvResume(psDevInfo);
	PVR_GOTO_IF_ERROR(eError, _RISCVDMError);

	pszException = _GetRISCVException(sRiscvState.mcause);
	if (pszException != NULL)
	{
		PVR_DUMPDEBUG_LOG("RISC-V FW hit an exception: %s", pszException);

		eError = _ValidateFWImage(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo);
		if (eError != PVRSRV_OK)
		{
			PVR_DUMPDEBUG_LOG("Failed to validate any FW code corruption");
		}
	}

	return PVRSRV_OK;

_RISCVDMError:
	PVR_DPF((PVR_DBG_ERROR, "Failed to communicate with the Debug Module"));

	return eError;
}
#endif

PVRSRV_ERROR RGXDumpRGXRegisters(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
								 void *pvDumpDebugFile,
								 PVRSRV_RGXDEV_INFO *psDevInfo)
{
#if defined(NO_HARDWARE)
	PVR_DUMPDEBUG_LOG("------[ RGX registers ]------");
	PVR_DUMPDEBUG_LOG("(Not supported for NO_HARDWARE builds)");

	return PVRSRV_OK;
#else
	IMG_UINT32   ui32Meta = RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META) ? RGX_GET_FEATURE_VALUE(psDevInfo, META) : 0;
	IMG_UINT32   ui32RegVal;
	PVRSRV_ERROR eError;
	IMG_BOOL     bFirmwarePerf;
	IMG_BOOL     bMulticore = RGX_IS_FEATURE_SUPPORTED(psDevInfo, GPU_MULTICORE_SUPPORT);
	void __iomem *pvRegsBaseKM = psDevInfo->pvRegsBaseKM;

	/* Check if firmware perf was set at Init time */
	bFirmwarePerf = (psDevInfo->psRGXFWIfSysInit->eFirmwarePerf != FW_PERF_CONF_NONE);

	DDLOG64(CORE_ID);

	if (bMulticore)
	{
		DDLOG64(MULTICORE);
		DDLOG32(MULTICORE_SYSTEM);
		DDLOG32(MULTICORE_DOMAIN);
	}
	DDLOG32(EVENT_STATUS);
	DDLOG64(TIMER);
	DDLOG64(CLK_CTRL0);
	DDLOG64(CLK_STATUS0);
	DDLOG64(CLK_CTRL1);
	DDLOG64(CLK_STATUS1);
	DDLOG64(MMU_FAULT_STATUS1);
	DDLOG64(MMU_FAULT_STATUS2);
	DDLOG64(MMU_FAULT_STATUS_PM);
	DDLOG64(MMU_FAULT_STATUS_META);
	DDLOG64(SLC_STATUS1);
	DDLOG64(SLC_STATUS2);
	DDLOG64(SLC_STATUS_DEBUG);
	DDLOG64(MMU_STATUS);
	DDLOG32(BIF_PFS);
	DDLOG32(BIF_TEXAS0_PFS);
	DDLOG32(BIF_TEXAS1_PFS);
	DDLOG32(BIF_OUTSTANDING_READ);
	DDLOG32(BIF_TEXAS0_OUTSTANDING_READ);
	DDLOG32(BIF_TEXAS1_OUTSTANDING_READ);
	DDLOG32(FBCDC_IDLE);
	DDLOG32(FBCDC_STATUS);
	DDLOG32(SPU_ENABLE);

	DDLOG64(CONTEXT_MAPPING0);
	DDLOG64(CONTEXT_MAPPING2);
	DDLOG64(CONTEXT_MAPPING3);
	DDLOG64(CONTEXT_MAPPING4);

	if (bMulticore)
	{
#if !defined(RGX_CR_MULTICORE_AXI)
#define RGX_CR_MULTICORE_AXI                              (0x2508U)
#define RGX_CR_MULTICORE_AXI_ERROR                        (0x2510U)
#endif
		DDLOG32(MULTICORE_AXI);
		DDLOG32(MULTICORE_AXI_ERROR);
		DDLOG32(MULTICORE_TDM_CTRL_COMMON);
		DDLOG32(MULTICORE_FRAGMENT_CTRL_COMMON);
		DDLOG32(MULTICORE_COMPUTE_CTRL_COMMON);
	}

	DDLOG32(PERF_PHASE_2D);
	DDLOG32(PERF_CYCLE_2D_TOTAL);
	DDLOG32(PERF_PHASE_GEOM);
	DDLOG32(PERF_CYCLE_GEOM_TOTAL);
	DDLOG32(PERF_PHASE_FRAG);
	DDLOG32(PERF_CYCLE_FRAG_TOTAL);
	DDLOG32(PERF_CYCLE_GEOM_OR_FRAG_TOTAL);
	DDLOG32(PERF_CYCLE_GEOM_AND_FRAG_TOTAL);
	DDLOG32(PERF_PHASE_COMP);
	DDLOG32(PERF_CYCLE_COMP_TOTAL);
	DDLOG32(PM_PARTIAL_RENDER_ENABLE);

	DDLOG32(ISP_RENDER);
	DDLOG32(ISP_CTL);

	DDLOG32(MTS_INTCTX);
	DDLOG32(MTS_BGCTX);
	DDLOG32(MTS_BGCTX_COUNTED_SCHEDULE);
	DDLOG32(MTS_SCHEDULE);
	DDLOG32(MTS_GPU_INT_STATUS);

	DDLOG32(CDM_CONTEXT_STORE_STATUS);
	DDLOG64(CDM_CONTEXT_PDS0);
	DDLOG64(CDM_CONTEXT_PDS1);
	DDLOG64(CDM_TERMINATE_PDS);
	DDLOG64(CDM_TERMINATE_PDS1);
	DDLOG64(CDM_CONTEXT_LOAD_PDS0);
	DDLOG64(CDM_CONTEXT_LOAD_PDS1);

	DDLOG32(JONES_IDLE);
	DDLOG32(SLC_IDLE);
	DDLOG32(SLC_FAULT_STOP_STATUS);

	DDLOG64(SCRATCH0);
	DDLOG64(SCRATCH1);
	DDLOG64(SCRATCH2);
	DDLOG64(SCRATCH3);
	DDLOG64(SCRATCH4);
	DDLOG64(SCRATCH5);
	DDLOG64(SCRATCH6);
	DDLOG64(SCRATCH7);
	DDLOG64(SCRATCH8);
	DDLOG64(SCRATCH9);
	DDLOG64(SCRATCH10);
	DDLOG64(SCRATCH11);
	DDLOG64(SCRATCH12);
	DDLOG64(SCRATCH13);
	DDLOG64(SCRATCH14);
	DDLOG64(SCRATCH15);
	DDLOG32(IRQ_OS0_EVENT_STATUS);

	if (ui32Meta)
	{
		IMG_BOOL bIsT0Enabled = IMG_FALSE, bIsFWFaulted = IMG_FALSE;
		IMG_UINT32 ui32MSlvIrqStatusReg = RGX_IS_FEATURE_SUPPORTED(psDevInfo, META_REGISTER_UNPACKED_ACCESSES) ?
				((RGX_GET_FEATURE_VALUE(psDevInfo, HOST_SECURITY_VERSION) > 1) ?
					RGX_CR_META_SP_MSLVIRQSTATUS__HOST_SECURITY_GT1_AND_METAREG_UNPACKED :
					RGX_CR_META_SP_MSLVIRQSTATUS__HOST_SECURITY_V1_AND_METAREG_UNPACKED) :
				RGX_CR_META_SP_MSLVIRQSTATUS;

		PVR_DUMPDEBUG_LOG(REG32_FMTSPEC, "META_SP_MSLVIRQSTATUS", OSReadUncheckedHWReg32(psDevInfo->pvSecureRegsBaseKM, ui32MSlvIrqStatusReg));

		eError = RGXReadFWModuleAddr(psDevInfo, META_CR_T0ENABLE_OFFSET, &ui32RegVal);
		PVR_LOG_GOTO_IF_ERROR(eError, "RGXReadFWModuleAddr", _METASPError);
		DDLOGVAL32("T0 TXENABLE", ui32RegVal);
		if (ui32RegVal & META_CR_TXENABLE_ENABLE_BIT)
		{
			bIsT0Enabled = IMG_TRUE;
		}

		eError = RGXReadFWModuleAddr(psDevInfo, META_CR_T0STATUS_OFFSET, &ui32RegVal);
		PVR_LOG_GOTO_IF_ERROR(eError, "RGXReadFWModuleAddr", _METASPError);
		DDLOGVAL32("T0 TXSTATUS", ui32RegVal);

		/* check for FW fault */
		if (((ui32RegVal >> 20) & 0x3) == 0x2)
		{
			bIsFWFaulted = IMG_TRUE;
		}

		eError = RGXReadFWModuleAddr(psDevInfo, META_CR_T0DEFR_OFFSET, &ui32RegVal);
		PVR_LOG_GOTO_IF_ERROR(eError, "RGXReadFWModuleAddr", _METASPError);
		DDLOGVAL32("T0 TXDEFR", ui32RegVal);

		eError = RGXReadMetaCoreReg(psDevInfo, META_CR_THR0_PC, &ui32RegVal);
		PVR_LOG_GOTO_IF_ERROR(eError, "RGXReadMetaCoreReg", _METASPError);
		DDLOGVAL32("T0 PC", ui32RegVal);

		eError = RGXReadMetaCoreReg(psDevInfo, META_CR_THR0_PCX, &ui32RegVal);
		PVR_LOG_GOTO_IF_ERROR(eError, "RGXReadMetaCoreReg", _METASPError);
		DDLOGVAL32("T0 PCX", ui32RegVal);

		eError = RGXReadMetaCoreReg(psDevInfo, META_CR_THR0_SP, &ui32RegVal);
		PVR_LOG_GOTO_IF_ERROR(eError, "RGXReadMetaCoreReg", _METASPError);
		DDLOGVAL32("T0 SP", ui32RegVal);

		if ((ui32Meta == MTP218) || (ui32Meta == MTP219))
		{
			eError = RGXReadFWModuleAddr(psDevInfo, META_CR_T1ENABLE_OFFSET, &ui32RegVal);
			PVR_LOG_GOTO_IF_ERROR(eError, "RGXReadFWModuleAddr", _METASPError);
			DDLOGVAL32("T1 TXENABLE", ui32RegVal);

			eError = RGXReadFWModuleAddr(psDevInfo, META_CR_T1STATUS_OFFSET, &ui32RegVal);
			PVR_LOG_GOTO_IF_ERROR(eError, "RGXReadFWModuleAddr", _METASPError);
			DDLOGVAL32("T1 TXSTATUS", ui32RegVal);

			eError = RGXReadFWModuleAddr(psDevInfo, META_CR_T1DEFR_OFFSET, &ui32RegVal);
			PVR_LOG_GOTO_IF_ERROR(eError, "RGXReadFWModuleAddr", _METASPError);
			DDLOGVAL32("T1 TXDEFR", ui32RegVal);

			eError = RGXReadMetaCoreReg(psDevInfo, META_CR_THR1_PC, &ui32RegVal);
			PVR_LOG_GOTO_IF_ERROR(eError, "RGXReadMetaCoreReg", _METASPError);
			DDLOGVAL32("T1 PC", ui32RegVal);

			eError = RGXReadMetaCoreReg(psDevInfo, META_CR_THR1_PCX, &ui32RegVal);
			PVR_LOG_GOTO_IF_ERROR(eError, "RGXReadMetaCoreReg", _METASPError);
			DDLOGVAL32("T1 PCX", ui32RegVal);

			eError = RGXReadMetaCoreReg(psDevInfo, META_CR_THR1_SP, &ui32RegVal);
			PVR_LOG_GOTO_IF_ERROR(eError, "RGXReadMetaCoreReg", _METASPError);
			DDLOGVAL32("T1 SP", ui32RegVal);
		}

		if (bFirmwarePerf)
		{
			eError = RGXReadFWModuleAddr(psDevInfo, META_CR_PERF_COUNT0, &ui32RegVal);
			PVR_LOG_GOTO_IF_ERROR(eError, "RGXReadFWModuleAddr", _METASPError);
			DDLOGVAL32("META_CR_PERF_COUNT0", ui32RegVal);

			eError = RGXReadFWModuleAddr(psDevInfo, META_CR_PERF_COUNT1, &ui32RegVal);
			PVR_LOG_GOTO_IF_ERROR(eError, "RGXReadFWModuleAddr", _METASPError);
			DDLOGVAL32("META_CR_PERF_COUNT1", ui32RegVal);
		}

		if (bIsT0Enabled & bIsFWFaulted)
		{
			eError = _ValidateFWImage(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo);
			if (eError != PVRSRV_OK)
			{
				PVR_DUMPDEBUG_LOG("Failed to validate any FW code corruption");
			}
		}
		else if (bIsFWFaulted)
		{
			PVR_DUMPDEBUG_LOG("Skipping FW code memory corruption checking as META is disabled");
		}
	}

	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, RISCV_FW_PROCESSOR))
	{
		eError = RGXDumpRISCVState(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo);
		PVR_RETURN_IF_ERROR(eError);
	}

	return PVRSRV_OK;

_METASPError:
	PVR_DUMPDEBUG_LOG("Dump Slave Port debug information");
	_RGXDumpMetaSPExtraDebugInfo(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo);

	return eError;
#endif	/* defined(NO_HARDWARE) */
}

#undef REG32_FMTSPEC
#undef REG64_FMTSPEC
#undef DDLOG32
#undef DDLOG64
#undef DDLOG32_DPX
#undef DDLOG64_DPX
#undef DDLOGVAL32

/*!
*******************************************************************************

 @Function	RGXDebugRequestProcess

 @Description

 This function will print out the debug for the specified level of verbosity

 @Input pfnDumpDebugPrintf  - Optional replacement print function
 @Input pvDumpDebugFile     - Optional file identifier to be passed to the
                              'printf' function if required
 @Input psDevInfo           - RGX device info
 @Input ui32VerbLevel       - Verbosity level

 @Return   void

******************************************************************************/
static
void RGXDebugRequestProcess(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile,
				PVRSRV_RGXDEV_INFO *psDevInfo,
				IMG_UINT32 ui32VerbLevel)
{
	PVRSRV_ERROR eError;
	PVRSRV_DEVICE_NODE *psDeviceNode = psDevInfo->psDeviceNode;
	PVRSRV_DEV_POWER_STATE  ePowerState;
	IMG_BOOL                bRGXPoweredON;
	IMG_UINT8               ui8FwOsCount;
	RGXFWIF_TRACEBUF        *psRGXFWIfTraceBufCtl = psDevInfo->psRGXFWIfTraceBufCtl;
	const RGXFWIF_OSDATA    *psFwOsData = psDevInfo->psRGXFWIfFwOsData;
	IMG_BOOL                bPwrLockAlreadyHeld;

	bPwrLockAlreadyHeld = PVRSRVPwrLockIsLockedByMe(psDeviceNode);
	if (!bPwrLockAlreadyHeld)
	{
		/* Only acquire the power-lock if not already held by the calling context */
		eError = PVRSRVPowerLock(psDeviceNode);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: failed to acquire lock (%s)",
					__func__,
					PVRSRVGetErrorString(eError)));
			return;
		}
	}

	ui8FwOsCount = psDevInfo->psRGXFWIfOsInit->sRGXCompChecks.sInitOptions.ui8OsCountSupport;

	eError = PVRSRVGetDevicePowerState(psDeviceNode, &ePowerState);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Error retrieving RGX power state. No debug info dumped.",
				__func__));
		goto Exit;
	}

	if ((PVRSRV_VZ_MODE_IS(NATIVE) && (ui8FwOsCount > 1)) ||
		(PVRSRV_VZ_MODE_IS(HOST) && (ui8FwOsCount != RGX_NUM_DRIVERS_SUPPORTED)))
	{
		PVR_DUMPDEBUG_LOG("Mismatch between the number of Operating Systems supported by KM driver (%d) and FW (%d)",
						  (PVRSRV_VZ_MODE_IS(NATIVE)) ? (1) : (RGX_NUM_DRIVERS_SUPPORTED), ui8FwOsCount);
	}

	PVR_DUMPDEBUG_LOG("------[ RGX Device ID:%d Start ]------", psDevInfo->psDeviceNode->sDevId.ui32InternalID);

	bRGXPoweredON = (ePowerState == PVRSRV_DEV_POWER_STATE_ON);

	PVR_DUMPDEBUG_LOG("------[ RGX Info ]------");
	PVR_DUMPDEBUG_LOG("Device Node (Info): %p (%p)", psDevInfo->psDeviceNode, psDevInfo);
	DevicememHistoryDumpRecordStats(psDevInfo->psDeviceNode, pfnDumpDebugPrintf, pvDumpDebugFile);
	PVR_DUMPDEBUG_LOG("RGX BVNC: %d.%d.%d.%d (%s)", psDevInfo->sDevFeatureCfg.ui32B,
											   psDevInfo->sDevFeatureCfg.ui32V,
											   psDevInfo->sDevFeatureCfg.ui32N,
											   psDevInfo->sDevFeatureCfg.ui32C,
											   PVR_ARCH_NAME);
	PVR_DUMPDEBUG_LOG("RGX Device State: %s", _RGXGetDebugDevStateString(psDeviceNode->eDevState));
	PVR_DUMPDEBUG_LOG("RGX Power State: %s", _RGXGetDebugDevPowerStateString(ePowerState));
	if (psDevInfo->psRGXFWIfOsInit->sRGXCompChecks.bUpdated)
	{
		PVR_DUMP_FIRMWARE_INFO(psDevInfo->psRGXFWIfOsInit->sRGXCompChecks);
	}
	else
	{
		PVR_DUMPDEBUG_LOG("FW info: UNINITIALIZED");
	}

	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, TILE_REGION_PROTECTION))
	{
#if defined(SUPPORT_TRP)
		PVR_DUMPDEBUG_LOG("TRP: HW support - Yes; SW enabled");
#else
		PVR_DUMPDEBUG_LOG("TRP: HW support - Yes; SW disabled");
#endif
	}
	else
	{
		PVR_DUMPDEBUG_LOG("TRP: HW support - No");
	}

	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, WORKGROUP_PROTECTION))
	{
#if defined(SUPPORT_WGP)
		PVR_DUMPDEBUG_LOG("WGP: HW support - Yes; SW enabled");
#else
		PVR_DUMPDEBUG_LOG("WGP: HW support - Yes; SW disabled");
#endif
	}
	else
	{
		PVR_DUMPDEBUG_LOG("WGP: HW support - No");
	}

	RGXDumpRGXDebugSummary(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo, bRGXPoweredON);

	/* Dump out the kernel CCB. */
	{
		const RGXFWIF_CCB_CTL *psKCCBCtl = psDevInfo->psKernelCCBCtl;

		if (psKCCBCtl != NULL)
		{
			PVR_DUMPDEBUG_LOG("RGX Kernel CCB WO:0x%X RO:0x%X",
							  psKCCBCtl->ui32WriteOffset,
							  psKCCBCtl->ui32ReadOffset);
		}
	}

	/* Dump out the firmware CCB. */
	{
		const RGXFWIF_CCB_CTL *psFCCBCtl = psDevInfo->psFirmwareCCBCtl;

		if (psFCCBCtl != NULL)
		{
			PVR_DUMPDEBUG_LOG("RGX Firmware CCB WO:0x%X RO:0x%X",
							   psFCCBCtl->ui32WriteOffset,
							   psFCCBCtl->ui32ReadOffset);
		}
	}

	if (psFwOsData != NULL)
	{
		IMG_UINT32 ui32TID;

		/* Dump the KCCB commands executed */
		PVR_DUMPDEBUG_LOG("RGX Kernel CCB commands executed = %d",
						  psFwOsData->ui32KCCBCmdsExecuted);

#if defined(PVRSRV_STALLED_CCB_ACTION)
		/* Dump the number of times we have performed a forced UFO update,
		 * and (if non-zero) the timestamp of the most recent occurrence/
		 */
		PVR_DUMPDEBUG_LOG("RGX SLR: Forced UFO updates requested = %d",
						  psFwOsData->ui32ForcedUpdatesRequested);
		if (psFwOsData->ui32ForcedUpdatesRequested > 0)
		{
			IMG_UINT8 ui8Idx;
			IMG_UINT64 ui64Seconds, ui64Nanoseconds;

			if (psFwOsData->ui64LastForcedUpdateTime > 0ULL)
			{
				ConvertOSTimestampToSAndNS(psFwOsData->ui64LastForcedUpdateTime, &ui64Seconds, &ui64Nanoseconds);
				PVR_DUMPDEBUG_LOG("RGX SLR: (most recent forced update was around %" IMG_UINT64_FMTSPEC ".%09" IMG_UINT64_FMTSPEC ")",
								  ui64Seconds, ui64Nanoseconds);
			}
			else
			{
				PVR_DUMPDEBUG_LOG("RGX SLR: (unable to force update as fence contained no sync checkpoints)");
			}
			/* Dump SLR log */
			if (psFwOsData->sSLRLogFirst.aszCCBName[0])
			{
				ConvertOSTimestampToSAndNS(psFwOsData->sSLRLogFirst.ui64Timestamp, &ui64Seconds, &ui64Nanoseconds);
				PVR_DUMPDEBUG_LOG("RGX SLR:{%" IMG_UINT64_FMTSPEC ".%09" IMG_UINT64_FMTSPEC
								  "} Fence found on context 0x%x '%s' has %d UFOs",
								  ui64Seconds, ui64Nanoseconds,
								  psFwOsData->sSLRLogFirst.ui32FWCtxAddr,
								  psFwOsData->sSLRLogFirst.aszCCBName,
								  psFwOsData->sSLRLogFirst.ui32NumUFOs);
			}
			for (ui8Idx=0; ui8Idx<PVR_SLR_LOG_ENTRIES;ui8Idx++)
			{
				if (psFwOsData->sSLRLog[ui8Idx].aszCCBName[0])
				{
					ConvertOSTimestampToSAndNS(psFwOsData->sSLRLog[ui8Idx].ui64Timestamp, &ui64Seconds, &ui64Nanoseconds);
					PVR_DUMPDEBUG_LOG("RGX SLR:[%" IMG_UINT64_FMTSPEC ".%09" IMG_UINT64_FMTSPEC
									  "] Fence found on context 0x%x '%s' has %d UFOs",
									  ui64Seconds, ui64Nanoseconds,
									  psFwOsData->sSLRLog[ui8Idx].ui32FWCtxAddr,
									  psFwOsData->sSLRLog[ui8Idx].aszCCBName,
									  psFwOsData->sSLRLog[ui8Idx].ui32NumUFOs);
				}
			}
		}
#else
		PVR_DUMPDEBUG_LOG("RGX SLR: Disabled");
#endif

		/* Dump the error counts */
		PVR_DUMPDEBUG_LOG("RGX Errors: WGP:%d, TRP:%d",
						  psDevInfo->sErrorCounts.ui32WGPErrorCount,
						  psDevInfo->sErrorCounts.ui32TRPErrorCount);

		for (ui32TID = 0; ui32TID < RGXFW_THREAD_NUM; ui32TID++)
		{
			/* Dump the IRQ info for threads */
			PVR_DUMPDEBUG_LOG("RGX FW thread %u: FW IRQ count = %u, Last sampled IRQ count in LISR = %u",
							  ui32TID,
							  psFwOsData->aui32InterruptCount[ui32TID],
							  psDevInfo->aui32SampleIRQCount[ui32TID]);
		}
	}

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	if (!PVRSRV_VZ_MODE_IS(GUEST))
	{
		/* Dump out the Workload estimation CCB. */
		const RGXFWIF_CCB_CTL *psWorkEstCCBCtl = psDevInfo->psWorkEstFirmwareCCBCtl;

		if (psWorkEstCCBCtl != NULL)
		{
			PVR_DUMPDEBUG_LOG("RGX WorkEst CCB WO:0x%X RO:0x%X",
							  psWorkEstCCBCtl->ui32WriteOffset,
							  psWorkEstCCBCtl->ui32ReadOffset);
		}
	}
#endif

	/* Dump the FW Sys config flags on the Host */
	if (!PVRSRV_VZ_MODE_IS(GUEST))
	{
		const RGXFWIF_SYSDATA *psFwSysData = psDevInfo->psRGXFWIfFwSysData;
		IMG_CHAR sFwSysFlagsDescription[MAX_FW_DESCRIPTION_LENGTH];

		if (!psFwSysData)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Fw Sys Data is not mapped into CPU space", __func__));
			goto Exit;
		}

		_GetFwSysFlagsDescription(sFwSysFlagsDescription, MAX_FW_DESCRIPTION_LENGTH, psFwSysData->ui32ConfigFlags);
		PVR_DUMPDEBUG_LOG("FW System config flags = 0x%08X (%s)", psFwSysData->ui32ConfigFlags, sFwSysFlagsDescription);
	}

	/* Dump the FW OS config flags */
	{
		IMG_CHAR sFwOsFlagsDescription[MAX_FW_DESCRIPTION_LENGTH];

		if (!psFwOsData)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Fw Os Data is not mapped into CPU space", __func__));
			goto Exit;
		}

		_GetFwOsFlagsDescription(sFwOsFlagsDescription, MAX_FW_DESCRIPTION_LENGTH, psFwOsData->ui32FwOsConfigFlags);
		PVR_DUMPDEBUG_LOG("FW OS config flags = 0x%08X (%s)", psFwOsData->ui32FwOsConfigFlags, sFwOsFlagsDescription);
	}

	if ((bRGXPoweredON) && !PVRSRV_VZ_MODE_IS(GUEST))
	{

#if !defined(NO_HARDWARE)
		PVR_DUMPDEBUG_LOG("------[ RGX registers ]------");
		PVR_DUMPDEBUG_LOG("RGX Register Base Address (Linear):   0x%p", psDevInfo->pvRegsBaseKM);
		PVR_DUMPDEBUG_LOG("RGX Register Base Address (Physical): 0x%08lX", (unsigned long)psDevInfo->sRegsPhysBase.uiAddr);

		if (RGX_GET_FEATURE_VALUE(psDevInfo, HOST_SECURITY_VERSION) > 1)
		{
			PVR_DUMPDEBUG_LOG("RGX Host Secure Register Base Address (Linear):   0x%p",
								psDevInfo->pvSecureRegsBaseKM);
			PVR_DUMPDEBUG_LOG("RGX Host Secure Register Base Address (Physical): 0x%08lX",
								(unsigned long)psDevInfo->sRegsPhysBase.uiAddr + RGX_HOST_SECURE_REGBANK_OFFSET);
		}

		if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META))
		{
			IMG_UINT32 ui32MSlvCtrl1Reg = RGX_IS_FEATURE_SUPPORTED(psDevInfo, META_REGISTER_UNPACKED_ACCESSES) ?
					((RGX_GET_FEATURE_VALUE(psDevInfo, HOST_SECURITY_VERSION) > 1) ?
						RGX_CR_META_SP_MSLVCTRL1__HOST_SECURITY_GT1_AND_METAREG_UNPACKED :
						RGX_CR_META_SP_MSLVCTRL1__HOST_SECURITY_V1_AND_METAREG_UNPACKED) :
					RGX_CR_META_SP_MSLVCTRL1;

			/* Forcing bit 6 of MslvCtrl1 to 0 to avoid internal reg read going through the core */
			OSWriteUncheckedHWReg32(psDevInfo->pvSecureRegsBaseKM, ui32MSlvCtrl1Reg, 0x0);
		}
#endif

		eError = RGXDumpRGXRegisters(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: RGXDumpRGXRegisters failed (%s)",
					__func__,
					PVRSRVGetErrorString(eError)));
#if defined(SUPPORT_FW_VIEW_EXTRA_DEBUG)
			if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META))
			{
				PVR_DUMPDEBUG_LOG("Dump Slave Port debug information");
				_RGXDumpMetaSPExtraDebugInfo(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo);
			}
#endif
		}
	}
	else
	{
		PVR_DUMPDEBUG_LOG(" (!) %s. No registers dumped", PVRSRV_VZ_MODE_IS(GUEST) ? "Guest Mode of operation" : "RGX power is down");
	}

	PVR_DUMPDEBUG_LOG("------[ RGX FW Trace Info ]------");

	if (DD_VERB_LVL_ENABLED(ui32VerbLevel, DEBUG_REQUEST_VERBOSITY_MEDIUM))
	{
		IMG_INT tid;
		/* Dump FW trace information */
		if (psRGXFWIfTraceBufCtl != NULL)
		{
			for (tid = 0 ; tid < RGXFW_THREAD_NUM ; tid++)
			{
				IMG_UINT32	i;
				IMG_BOOL	bPrevLineWasZero = IMG_FALSE;
				IMG_BOOL	bLineIsAllZeros = IMG_FALSE;
				IMG_UINT32	ui32CountLines = 0;
				IMG_UINT32	*pui32TraceBuffer;
				IMG_CHAR	*pszLine;

				if (psRGXFWIfTraceBufCtl->ui32LogType & RGXFWIF_LOG_TYPE_GROUP_MASK)
				{
					PVR_DUMPDEBUG_LOG("Debug log type: %s ( " RGXFWIF_LOG_ENABLED_GROUPS_LIST_PFSPEC ")",
									  ((psRGXFWIfTraceBufCtl->ui32LogType & RGXFWIF_LOG_TYPE_TRACE)?("trace"):("tbi")),
									  RGXFWIF_LOG_ENABLED_GROUPS_LIST(psRGXFWIfTraceBufCtl->ui32LogType)
									  );
				}
				else
				{
					PVR_DUMPDEBUG_LOG("Debug log type: none");
				}

				pui32TraceBuffer = psRGXFWIfTraceBufCtl->sTraceBuf[tid].pui32TraceBuffer;

				/* Skip if trace buffer is not allocated */
				if (pui32TraceBuffer == NULL)
				{
					PVR_DUMPDEBUG_LOG("RGX FW thread %d: Trace buffer not yet allocated",tid);
					continue;
				}

/* Max number of DWords to be printed per line, in debug dump output */
#define PVR_DD_FW_TRACEBUF_LINESIZE 30U
				/* each element in the line is 8 characters plus a space.  The '+ 1' is because of the final trailing '\0'. */
				pszLine = OSAllocMem(9 * PVR_DD_FW_TRACEBUF_LINESIZE + 1);
				if (pszLine == NULL)
				{
					PVR_DPF((PVR_DBG_ERROR,
							"%s: Out of mem allocating line string (size: %d)",
							__func__,
							9 * PVR_DD_FW_TRACEBUF_LINESIZE + 1));
					goto Exit;
				}

				PVR_DUMPDEBUG_LOG("------[ RGX FW thread %d trace START ]------", tid);
				PVR_DUMPDEBUG_LOG("FWT[traceptr]: %X", psRGXFWIfTraceBufCtl->sTraceBuf[tid].ui32TracePointer);
				PVR_DUMPDEBUG_LOG("FWT[tracebufsize]: %X", psRGXFWIfTraceBufCtl->ui32TraceBufSizeInDWords);

				for (i = 0; i < psRGXFWIfTraceBufCtl->ui32TraceBufSizeInDWords; i += PVR_DD_FW_TRACEBUF_LINESIZE)
				{
					IMG_UINT32 k = 0;
					IMG_UINT32 ui32Line = 0x0;
					IMG_UINT32 ui32LineOffset = i*sizeof(IMG_UINT32);
					IMG_CHAR   *pszBuf = pszLine;

					for (k = 0; k < PVR_DD_FW_TRACEBUF_LINESIZE; k++)
					{
						if ((i + k) >= psRGXFWIfTraceBufCtl->ui32TraceBufSizeInDWords)
						{
							/* Stop reading when the index goes beyond trace buffer size. This condition is
							 * hit during printing the last line in DD when ui32TraceBufSizeInDWords is not
							 * a multiple of PVR_DD_FW_TRACEBUF_LINESIZE */
							break;
						}

						ui32Line |= pui32TraceBuffer[i + k];

						/* prepare the line to print it. The '+1' is because of the trailing '\0' added */
						OSSNPrintf(pszBuf, 9 + 1, " %08x", pui32TraceBuffer[i + k]);
						pszBuf += 9; /* write over the '\0' */
					}

					bLineIsAllZeros = (ui32Line == 0x0);

					if (bLineIsAllZeros)
					{
						if (bPrevLineWasZero)
						{
							ui32CountLines++;
						}
						else
						{
							bPrevLineWasZero = IMG_TRUE;
							ui32CountLines = 1;
							PVR_DUMPDEBUG_LOG("FWT[%08x]: 00000000 ... 00000000", ui32LineOffset);
						}
					}
					else
					{
						if (bPrevLineWasZero  &&  ui32CountLines > 1)
						{
							PVR_DUMPDEBUG_LOG("FWT[...]: %d lines were all zero", ui32CountLines);
						}
						bPrevLineWasZero = IMG_FALSE;

						PVR_DUMPDEBUG_LOG("FWT[%08x]:%s", ui32LineOffset, pszLine);
					}

				}
				if (bPrevLineWasZero)
				{
					PVR_DUMPDEBUG_LOG("FWT[END]: %d lines were all zero", ui32CountLines);
				}

				PVR_DUMPDEBUG_LOG("------[ RGX FW thread %d trace END ]------", tid);

				OSFreeMem(pszLine);
			}
		}

		{
			if (DD_VERB_LVL_ENABLED(ui32VerbLevel, DEBUG_REQUEST_VERBOSITY_HIGH))
			{
				PVR_DUMPDEBUG_LOG("------[ Full CCB Status ]------");
			}
			else
			{
				PVR_DUMPDEBUG_LOG("------[ FWCtxs Next CMD ]------");
			}

			DumpRenderCtxtsInfo(psDevInfo, pfnDumpDebugPrintf, pvDumpDebugFile, ui32VerbLevel);
			DumpComputeCtxtsInfo(psDevInfo, pfnDumpDebugPrintf, pvDumpDebugFile, ui32VerbLevel);

			DumpTDMTransferCtxtsInfo(psDevInfo, pfnDumpDebugPrintf, pvDumpDebugFile, ui32VerbLevel);
#if defined(SUPPORT_RGXKICKSYNC_BRIDGE)
			DumpKickSyncCtxtsInfo(psDevInfo, pfnDumpDebugPrintf, pvDumpDebugFile, ui32VerbLevel);
#endif
		}
	}

	PVR_DUMPDEBUG_LOG("------[ RGX Device ID:%d End ]------", psDevInfo->psDeviceNode->sDevId.ui32InternalID);

Exit:
	if (!bPwrLockAlreadyHeld)
	{
		PVRSRVPowerUnlock(psDeviceNode);
	}
}

/*!
 ******************************************************************************

 @Function	RGXDebugRequestNotify

 @Description Dump the debug data for RGX

 ******************************************************************************/
static void RGXDebugRequestNotify(PVRSRV_DBGREQ_HANDLE hDbgRequestHandle,
		IMG_UINT32 ui32VerbLevel,
		DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
		void *pvDumpDebugFile)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = hDbgRequestHandle;

	/* Only action the request if we've fully init'ed */
	if (psDevInfo->bDevInit2Done)
	{
		RGXDebugRequestProcess(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo, ui32VerbLevel);
	}
}

PVRSRV_ERROR RGXDebugInit(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	return PVRSRVRegisterDeviceDbgRequestNotify(&psDevInfo->hDbgReqNotify,
		                                        psDevInfo->psDeviceNode,
		                                        RGXDebugRequestNotify,
		                                        DEBUG_REQUEST_RGX,
		                                        psDevInfo);
}

PVRSRV_ERROR RGXDebugDeinit(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	if (psDevInfo->hDbgReqNotify)
	{
		return PVRSRVUnregisterDeviceDbgRequestNotify(psDevInfo->hDbgReqNotify);
	}

	/* No notifier registered */
	return PVRSRV_OK;
}

/******************************************************************************
 End of file (rgxdebug.c)
******************************************************************************/
