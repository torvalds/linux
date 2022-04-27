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

#include "rgxfwimageutils.h"
#include "devicemem.h"
#include "devicemem_pdump.h"
#include "devicemem_utils.h"
#include "rgx_fwif_km.h"
#include "rgx_fwif_sf.h"
#include "rgxfw_log_helper.h"
#include "fwtrace_string.h"
#include "rgxfwimageutils.h"
#include "fwload.h"

#include "rgxta3d.h"
#include "rgxkicksync.h"
#include "rgxcompute.h"
#include "rgxtransfer.h"
#include "rgxtdmtransfer.h"
#include "rgxtimecorr.h"
#include "rgx_options.h"
#include "rgxinit.h"
#include "devicemem_history_server.h"
#include "info_page.h"
#include "rgx_bvnc_defs_km.h"

#define PVR_DUMP_FIRMWARE_INFO(x)														\
	PVR_DUMPDEBUG_LOG("FW info: %d.%d @ %8d (%s) build options: 0x%08x",				\
						PVRVERSION_UNPACK_MAJ((x).ui32DDKVersion),						\
						PVRVERSION_UNPACK_MIN((x).ui32DDKVersion),						\
						(x).ui32DDKBuild,												\
						((x).ui32BuildOptions & OPTIONS_DEBUG_MASK) ? "debug":"release",\
						(x).ui32BuildOptions);

#define DD_SUMMARY_INDENT  ""
#define DD_NORMAL_INDENT   "    "

#define RGX_DEBUG_STR_SIZE		(150U)
#define MAX_FW_DESCRIPTION_LENGTH	(500U)

#define RGX_CR_BIF_CAT_BASE0                              (0x1200U)
#define RGX_CR_BIF_CAT_BASE1                              (0x1208U)

#define RGX_CR_BIF_CAT_BASEN(n) \
	RGX_CR_BIF_CAT_BASE0 + \
	((RGX_CR_BIF_CAT_BASE1 - RGX_CR_BIF_CAT_BASE0) * n)


#define RGXDBG_BIF_IDS \
	X(BIF0)\
	X(BIF1)\
	X(TEXAS_BIF)\
	X(DPX_BIF) \
	X(FWCORE)

#define RGXDBG_SIDEBAND_TYPES \
	X(META)\
	X(TLA)\
	X(DMA)\
	X(VDMM)\
	X(CDM)\
	X(IPP)\
	X(PM)\
	X(TILING)\
	X(MCU)\
	X(PDS)\
	X(PBE)\
	X(VDMS)\
	X(IPF)\
	X(ISP)\
	X(TPF)\
	X(USCS)\
	X(PPP)\
	X(VCE)\
	X(TPF_CPF)\
	X(IPF_CPF)\
	X(FBCDC)

typedef enum
{
#define X(NAME) RGXDBG_##NAME,
	RGXDBG_BIF_IDS
#undef X
} RGXDBG_BIF_ID;

typedef enum
{
#define X(NAME) RGXDBG_##NAME,
	RGXDBG_SIDEBAND_TYPES
#undef X
} RGXDBG_SIDEBAND_TYPE;

static const IMG_CHAR *const pszPowStateName[] =
{
#define X(NAME)	#NAME,
	RGXFWIF_POW_STATES
#undef X
};

static const IMG_CHAR *const pszBIFNames[] =
{
#define X(NAME)	#NAME,
	RGXDBG_BIF_IDS
#undef X
};

typedef struct _IMG_FLAGS2DESC_
{
	IMG_UINT32		uiFlag;
	const IMG_CHAR	*pszLabel;
} IMG_FLAGS2DESC;

static const IMG_FLAGS2DESC asCswOpts2Description[] =
{
	{RGXFWIF_INICFG_CTXSWITCH_PROFILE_FAST, " Fast CSW profile;"},
	{RGXFWIF_INICFG_CTXSWITCH_PROFILE_MEDIUM, " Medium CSW profile;"},
	{RGXFWIF_INICFG_CTXSWITCH_PROFILE_SLOW, " Slow CSW profile;"},
	{RGXFWIF_INICFG_CTXSWITCH_PROFILE_NODELAY, " No Delay CSW profile;"},
	{RGXFWIF_INICFG_CTXSWITCH_MODE_RAND, " Random Csw enabled;"},
	{RGXFWIF_INICFG_CTXSWITCH_SRESET_EN, " SoftReset;"},
	{RGXFWIF_INICFG_VDM_CTX_STORE_MODE_INDEX, " VDM CS INDEX mode;"},
	{RGXFWIF_INICFG_VDM_CTX_STORE_MODE_INSTANCE, " VDM CS INSTANCE mode;"},
	{RGXFWIF_INICFG_VDM_CTX_STORE_MODE_LIST, " VDM CS LIST mode;"},
};

static const IMG_FLAGS2DESC asMisc2Description[] =
{
	{RGXFWIF_INICFG_POW_RASCALDUST, " Power Rascal/Dust;"},
	{RGXFWIF_INICFG_HWPERF_EN, " HwPerf EN;"},
	{RGXFWIF_INICFG_FBCDC_V3_1_EN, " FBCDCv3.1;"},
	{RGXFWIF_INICFG_CHECK_MLIST_EN, " Check MList;"},
	{RGXFWIF_INICFG_DISABLE_CLKGATING_EN, " ClockGating Off;"},
	{RGXFWIF_INICFG_POLL_COUNTERS_EN, " Poll Counters;"},
	{RGXFWIF_INICFG_REGCONFIG_EN, " Register Config;"},
	{RGXFWIF_INICFG_ASSERT_ON_OUTOFMEMORY, " Assert on OOM;"},
	{RGXFWIF_INICFG_HWP_DISABLE_FILTER, " HWP Filter Off;"},
	{RGXFWIF_INICFG_CUSTOM_PERF_TIMER_EN, " Custom PerfTimer;"},
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
	{RGXFWIF_INICFG_VALIDATE_SOCUSC_TIMER, " Validate SOC&USC timers;"},
};

static const IMG_FLAGS2DESC asFwOsCfg2Description[] =
{
	{RGXFWIF_INICFG_OS_CTXSWITCH_TDM_EN, " TDM;"},
	{RGXFWIF_INICFG_OS_CTXSWITCH_GEOM_EN, " TA;"},
	{RGXFWIF_INICFG_OS_CTXSWITCH_3D_EN, " 3D;"},
	{RGXFWIF_INICFG_OS_CTXSWITCH_CDM_EN, " CDM;"},
	{RGXFWIF_INICFG_OS_LOW_PRIO_CS_TDM, " LowPrio TDM;"},
	{RGXFWIF_INICFG_OS_LOW_PRIO_CS_GEOM, " LowPrio TA;"},
	{RGXFWIF_INICFG_OS_LOW_PRIO_CS_3D, " LowPrio 3D;"},
	{RGXFWIF_INICFG_OS_LOW_PRIO_CS_CDM, " LowPrio CDM;"},
};

static const IMG_FLAGS2DESC asHwrState2Description[] =
{
	{RGXFWIF_HWR_HARDWARE_OK, " HWR OK;"},
	{RGXFWIF_HWR_GENERAL_LOCKUP, " General lockup;"},
	{RGXFWIF_HWR_DM_RUNNING_OK, " DM running ok;"},
	{RGXFWIF_HWR_DM_STALLING, " DM stalling;"},
	{RGXFWIF_HWR_FW_FAULT, " FW fault;"},
	{RGXFWIF_HWR_RESTART_REQUESTED, " Restarting;"},
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
		{RGXFWIF_DM_STATE_HARD_CONTEXT_SWITCH, " hard context switching;"},
		{RGXFWIF_DM_STATE_GPU_ECC_HWR, " GPU ECC hwr;"},
};

#if !defined(NO_HARDWARE)
/* Translation of MIPS exception encoding */
typedef struct _MIPS_EXCEPTION_ENCODING_
{
	const IMG_CHAR *const pszStr;	/* Error type */
	const IMG_BOOL bIsFatal;	/* Error is fatal or non-fatal */
} MIPS_EXCEPTION_ENCODING;

static const MIPS_EXCEPTION_ENCODING apsMIPSExcCodes[] =
{
	{"Interrupt", IMG_FALSE},
	{"TLB modified exception", IMG_FALSE},
	{"TLB exception (load/instruction fetch)", IMG_FALSE},
	{"TLB exception (store)", IMG_FALSE},
	{"Address error exception (load/instruction fetch)", IMG_TRUE},
	{"Address error exception (store)", IMG_TRUE},
	{"Bus error exception (instruction fetch)", IMG_TRUE},
	{"Bus error exception (load/store)", IMG_TRUE},
	{"Syscall exception", IMG_FALSE},
	{"Breakpoint exception (FW assert)", IMG_FALSE},
	{"Reserved instruction exception", IMG_TRUE},
	{"Coprocessor Unusable exception", IMG_FALSE},
	{"Arithmetic Overflow exception", IMG_FALSE},
	{"Trap exception", IMG_FALSE},
	{NULL, IMG_FALSE},
	{NULL, IMG_FALSE},
	{"Implementation-Specific Exception 1 (COP2)", IMG_FALSE},
	{"CorExtend Unusable", IMG_FALSE},
	{"Coprocessor 2 exceptions", IMG_FALSE},
	{"TLB Read-Inhibit", IMG_TRUE},
	{"TLB Execute-Inhibit", IMG_TRUE},
	{NULL, IMG_FALSE},
	{NULL, IMG_FALSE},
	{"Reference to WatchHi/WatchLo address", IMG_FALSE},
	{"Machine check", IMG_FALSE},
	{NULL, IMG_FALSE},
	{"DSP Module State Disabled exception", IMG_FALSE},
	{NULL, IMG_FALSE},
	{NULL, IMG_FALSE},
	{NULL, IMG_FALSE},
	/* Can only happen in MIPS debug mode */
	{"Parity error", IMG_FALSE},
	{NULL, IMG_FALSE}
};

static IMG_CHAR const *_GetMIPSExcString(IMG_UINT32 ui32ExcCode)
{
	if (ui32ExcCode >= sizeof(apsMIPSExcCodes)/sizeof(MIPS_EXCEPTION_ENCODING))
	{
		PVR_DPF((PVR_DBG_WARNING,
		         "Only %lu exceptions available in MIPS, %u is not a valid exception code",
		         (unsigned long)sizeof(apsMIPSExcCodes)/sizeof(MIPS_EXCEPTION_ENCODING), ui32ExcCode));
		return NULL;
	}

	return apsMIPSExcCodes[ui32ExcCode].pszStr;
}
#endif

typedef struct _RGXMIPSFW_C0_DEBUG_TBL_ENTRY_
{
    IMG_UINT32 ui32Mask;
    const IMG_CHAR * pszExplanation;
} RGXMIPSFW_C0_DEBUG_TBL_ENTRY;

#if !defined(NO_HARDWARE)
static const RGXMIPSFW_C0_DEBUG_TBL_ENTRY sMIPS_C0_DebugTable[] =
{
    { RGXMIPSFW_C0_DEBUG_DSS,      "Debug single-step exception occurred" },
    { RGXMIPSFW_C0_DEBUG_DBP,      "Debug software breakpoint exception occurred" },
    { RGXMIPSFW_C0_DEBUG_DDBL,     "Debug data break exception occurred on a load" },
    { RGXMIPSFW_C0_DEBUG_DDBS,     "Debug data break exception occurred on a store" },
    { RGXMIPSFW_C0_DEBUG_DIB,      "Debug instruction break exception occurred" },
    { RGXMIPSFW_C0_DEBUG_DINT,     "Debug interrupt exception occurred" },
    { RGXMIPSFW_C0_DEBUG_DIBIMPR,  "Imprecise debug instruction break exception occurred" },
    { RGXMIPSFW_C0_DEBUG_DDBLIMPR, "Imprecise debug data break load exception occurred" },
    { RGXMIPSFW_C0_DEBUG_DDBSIMPR, "Imprecise debug data break store exception occurred" },
    { RGXMIPSFW_C0_DEBUG_IEXI,     "Imprecise error exception inhibit controls exception occurred" },
    { RGXMIPSFW_C0_DEBUG_DBUSEP,   "Data access Bus Error exception pending" },
    { RGXMIPSFW_C0_DEBUG_CACHEEP,  "Imprecise Cache Error pending" },
    { RGXMIPSFW_C0_DEBUG_MCHECKP,  "Imprecise Machine Check exception pending" },
    { RGXMIPSFW_C0_DEBUG_IBUSEP,   "Instruction fetch Bus Error exception pending" },
    { RGXMIPSFW_C0_DEBUG_DBD,      "Debug exception occurred in branch delay slot" }
};
#endif

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

static PVRSRV_ERROR
RGXPollMetaRegThroughSP(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32RegOffset,
                        IMG_UINT32 ui32PollValue, IMG_UINT32 ui32Mask)
{
	IMG_UINT32 ui32RegValue, ui32NumPolls = 0;
	PVRSRV_ERROR eError;

	do
	{
		eError = RGXReadWithSP(psDevInfo, ui32RegOffset, &ui32RegValue);
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
	eError = RGXWriteWithSP(psDevInfo, META_CR_TXUXXRXRQ_OFFSET,
	                        ui32RegAddr | META_CR_TXUXXRXRQ_RDnWR_BIT);
	PVR_LOG_RETURN_IF_ERROR(eError, "RGXWriteWithSP");

	/* Core Read Done? */
	eError = RGXPollMetaRegThroughSP(psDevInfo,
	                                 META_CR_TXUXXRXRQ_OFFSET,
	                                 META_CR_TXUXXRXRQ_DREADY_BIT,
									 META_CR_TXUXXRXRQ_DREADY_BIT);
	PVR_LOG_RETURN_IF_ERROR(eError, "RGXPollMetaRegThroughSP");

	/* Read the value */
	return RGXReadWithSP(psDevInfo, META_CR_TXUXXRXDT_OFFSET, pui32RegVal);
}

PVRSRV_ERROR
RGXReadWithSP(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32FWAddr, IMG_UINT32 *pui32Value)
{
	PVRSRV_ERROR eError = RGXReadMETAAddr(psDevInfo, ui32FWAddr, pui32Value);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: %s", __func__, PVRSRVGetErrorString(eError)));
	}

	return eError;
}

PVRSRV_ERROR
RGXWriteWithSP(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32FWAddr, IMG_UINT32 ui32Value)
{
	PVRSRV_ERROR eError = RGXWriteMETAAddr(psDevInfo, ui32FWAddr, ui32Value);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: %s", __func__, PVRSRVGetErrorString(eError)));
	}
	return eError;
}

#if !defined(NO_HARDWARE) && !defined(SUPPORT_TRUSTED_DEVICE)
static PVRSRV_ERROR _ValidateWithSP(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
						void *pvDumpDebugFile,
						PVRSRV_RGXDEV_INFO *psDevInfo,
						RGXFWIF_DEV_VIRTADDR *psFWAddr,
						void *pvHostCodeAddr,
						IMG_UINT32 ui32MaxLen,
						const IMG_CHAR *pszDesc,
						IMG_UINT32 ui32StartOffset)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32Value, i;
	IMG_UINT32 ui32FWCodeDevVAAddr = psFWAddr->ui32Addr + ui32StartOffset;
	IMG_UINT32 *pui32FWCode = (IMG_PUINT32) ((IMG_PBYTE)pvHostCodeAddr + ui32StartOffset);

	ui32MaxLen -= ui32StartOffset;
	ui32MaxLen /= sizeof(IMG_UINT32); /* Byte -> 32 bit words */

	for (i = 0; i < ui32MaxLen; i++)
	{
		eError = RGXReadMETAAddr(psDevInfo, ui32FWCodeDevVAAddr, &ui32Value);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: %s", __func__, PVRSRVGetErrorString(eError)));
			return eError;
		}

		PVR_DPF((PVR_DBG_VERBOSE, "0x%x: CPU 0x%08x, FW 0x%08x", i * 4, pui32FWCode[i], ui32Value));

		if (pui32FWCode[i] != ui32Value)
		{
			PVR_DUMPDEBUG_LOG("_ValidateWithSP: Mismatch while validating %s at offset 0x%x: CPU 0x%08x (%p), FW 0x%08x (%x)",
				 pszDesc,
				 (i * 4) + ui32StartOffset, pui32FWCode[i], pui32FWCode, ui32Value, ui32FWCodeDevVAAddr);
			return PVRSRV_ERROR_FW_IMAGE_MISMATCH;
		}

		ui32FWCodeDevVAAddr += 4;
	}

	PVR_DUMPDEBUG_LOG("Match between Host and Meta view of the %s", pszDesc);
	return PVRSRV_OK;
}

#if !defined(EMULATOR)
static PVRSRV_ERROR _ValidateWithDM(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
						void *pvDumpDebugFile,
						PVRSRV_RGXDEV_INFO *psDevInfo,
						RGXFWIF_DEV_VIRTADDR *psFWAddr,
						void *pvHostCodeAddr,
						IMG_UINT32 ui32MaxLen,
						const IMG_CHAR *pszDesc,
						IMG_UINT32 ui32StartOffset)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32Value, i;
	IMG_UINT32 ui32FWCodeDevVAAddr = psFWAddr->ui32Addr + ui32StartOffset;
	IMG_UINT32 *pui32FWCode = IMG_OFFSET_ADDR(pvHostCodeAddr, ui32StartOffset);

	ui32MaxLen -= ui32StartOffset;
	ui32MaxLen /= sizeof(IMG_UINT32); /* Byte -> 32 bit words */

	for (i = 0; i < ui32MaxLen; i++)
	{
		eError = RGXRiscvReadMem(psDevInfo, ui32FWCodeDevVAAddr, &ui32Value);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: %s", __func__, PVRSRVGetErrorString(eError)));
			return eError;
		}

		PVR_DPF((PVR_DBG_VERBOSE, "0x%x: CPU 0x%08x, FW 0x%08x", i * 4, pui32FWCode[i], ui32Value));

		if (pui32FWCode[i] != ui32Value)
		{
			PVR_DUMPDEBUG_LOG("%s: Mismatch while validating %s at offset 0x%x: CPU 0x%08x (0x%p), FW 0x%08x (0x%x)",
				 __func__, pszDesc,
				 (i * 4) + ui32StartOffset, pui32FWCode[i], pui32FWCode, ui32Value, ui32FWCodeDevVAAddr);

			return PVRSRV_ERROR_FW_IMAGE_MISMATCH;
		}

		ui32FWCodeDevVAAddr += 4;
	}

	PVR_DUMPDEBUG_LOG("Match between Host and RISC-V view of the %s", pszDesc);
	return PVRSRV_OK;
}
#endif
#endif

#if !defined(NO_HARDWARE)
static PVRSRV_ERROR _ValidateFWImageForMIPS(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
						void *pvDumpDebugFile,
						PVRSRV_RGXDEV_INFO *psDevInfo,
						char *pszFormat)
{
#if !defined(SUPPORT_TRUSTED_DEVICE)
	PVRSRV_ERROR eError;
	IMG_UINT32 *pui32HostFWCode = NULL;
	OS_FW_IMAGE *psRGXFW = NULL;
	const IMG_BYTE *pbRGXFirmware = NULL;
	IMG_UINT32 *pui32CodeMemoryPointer;
	RGX_LAYER_PARAMS sLayerParams;
	sLayerParams.psDevInfo = psDevInfo;

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

	/* Load FW image */
	eError = RGXLoadAndGetFWData(psDevInfo->psDeviceNode, &psRGXFW, &pbRGXFirmware);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to load FW image file (%s).",
		         __func__, PVRSRVGetErrorString(eError)));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto cleanup_initfw;
	}

	eError = ProcessELFCommandStream(&sLayerParams, pbRGXFirmware, pui32HostFWCode, NULL, NULL, NULL);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed in parsing FW image file.", __func__));
		goto cleanup_initfw;
	}

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWCodeMemDesc, (void **)&pui32CodeMemoryPointer);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Error in acquiring MIPS FW code memory area (%s)",
				__func__,
				PVRSRVGetErrorString(eError)));
		goto cleanup_initfw;
	}

	if (OSMemCmp(pui32HostFWCode, pui32CodeMemoryPointer, psDevInfo->ui32FWCodeSizeInBytes) == 0)
	{
		PVR_DUMPDEBUG_LOG("%sMatch between Host and MIPS views of the FW code", pszFormat);
	}
	else
	{
		IMG_UINT32 ui32Count = 10; /* Show only the first 10 mismatches */
		IMG_UINT32 ui32Offset;

		PVR_DUMPDEBUG_LOG("%sMismatch between Host and MIPS views of the FW code", pszFormat);
		for (ui32Offset = 0; (ui32Offset*4 < psDevInfo->ui32FWCodeSizeInBytes) || (ui32Count == 0); ui32Offset++)
		{
			if (pui32HostFWCode[ui32Offset] != pui32CodeMemoryPointer[ui32Offset])
			{
				PVR_DUMPDEBUG_LOG("%s   At %d bytes, code should be 0x%x but it is instead 0x%x",
				   pszFormat, ui32Offset*4, pui32HostFWCode[ui32Offset], pui32CodeMemoryPointer[ui32Offset]);
				ui32Count--;
			}
		}
	}

	DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWCodeMemDesc);

cleanup_initfw:
	if (psRGXFW)
	{
		OSUnloadFirmware(psRGXFW);
	}

	if (pui32HostFWCode)
	{
		OSFreeMem(pui32HostFWCode);
	}
	return eError;
#else
	PVR_UNREFERENCED_PARAMETER(pfnDumpDebugPrintf);
	PVR_UNREFERENCED_PARAMETER(pvDumpDebugFile);
	PVR_UNREFERENCED_PARAMETER(psDevInfo);
	PVR_UNREFERENCED_PARAMETER(pszFormat);
	return PVRSRV_OK;
#endif
}
#endif

static PVRSRV_ERROR _ValidateFWImageForMETA(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
						void *pvDumpDebugFile,
						PVRSRV_RGXDEV_INFO *psDevInfo)
{
#if !defined(NO_HARDWARE) && !defined(SUPPORT_TRUSTED_DEVICE)
	IMG_UINT32 *pui32HostFWCode = NULL, *pui32HostFWCoremem = NULL;
	OS_FW_IMAGE *psRGXFW = NULL;
	const IMG_BYTE *pbRGXFirmware = NULL;
	RGXFWIF_DEV_VIRTADDR sFWAddr;
	PVRSRV_ERROR eError;
	RGX_LAYER_PARAMS sLayerParams;
	sLayerParams.psDevInfo = psDevInfo;

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

	eError = ProcessLDRCommandStream(&sLayerParams, pbRGXFirmware,
					(void*) pui32HostFWCode, NULL,
					(void*) pui32HostFWCoremem, NULL, NULL);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed in parsing FW image file.", __func__));
		goto cleanup_initfw;
	}

	/* starting checking after BOOT LOADER config */
	sFWAddr.ui32Addr = RGXFW_BOOTLDR_META_ADDR;
	eError = _ValidateWithSP(pfnDumpDebugPrintf, pvDumpDebugFile,
					psDevInfo, &sFWAddr,
					pui32HostFWCode, psDevInfo->ui32FWCodeSizeInBytes,
					"FW code", RGXFW_MAX_BOOTLDR_OFFSET);
	if (eError != PVRSRV_OK)
	{
		goto cleanup_initfw;
	}

	if (psDevInfo->ui32FWCorememCodeSizeInBytes)
	{
		sFWAddr.ui32Addr = RGXGetFWImageSectionAddress(NULL, META_COREMEM_CODE);

		eError = _ValidateWithSP(pfnDumpDebugPrintf, pvDumpDebugFile,
						psDevInfo, &sFWAddr,
						pui32HostFWCoremem, psDevInfo->ui32FWCorememCodeSizeInBytes,
						"FW coremem code", 0);
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

static PVRSRV_ERROR _ValidateFWImageForRISCV(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
						void *pvDumpDebugFile,
						PVRSRV_RGXDEV_INFO *psDevInfo)
{
#if !defined(NO_HARDWARE) && !defined(SUPPORT_TRUSTED_DEVICE) && !defined(EMULATOR)
	IMG_UINT32 *pui32HostFWCode = NULL, *pui32HostFWCoremem = NULL;
	OS_FW_IMAGE *psRGXFW = NULL;
	const IMG_BYTE *pbRGXFirmware = NULL;
	RGXFWIF_DEV_VIRTADDR sFWAddr;
	PVRSRV_ERROR eError;
	RGX_LAYER_PARAMS sLayerParams;
	sLayerParams.psDevInfo = psDevInfo;

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

	eError = ProcessELFCommandStream(&sLayerParams, pbRGXFirmware,
	                                 pui32HostFWCode, NULL,
	                                 pui32HostFWCoremem, NULL);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed in parsing FW image file.", __func__));
		goto cleanup_initfw;
	}

	/* Use bootloader code remap which is always configured before the FW is started */
	sFWAddr.ui32Addr = RGXRISCVFW_BOOTLDR_CODE_BASE;

	eError = _ValidateWithDM(pfnDumpDebugPrintf, pvDumpDebugFile,
					psDevInfo, &sFWAddr,
					pui32HostFWCode, psDevInfo->ui32FWCodeSizeInBytes,
					"FW code", 0);
	if (eError != PVRSRV_OK)
	{
		goto cleanup_initfw;
	}

	if (psDevInfo->ui32FWCorememCodeSizeInBytes)
	{
		sFWAddr.ui32Addr = RGXGetFWImageSectionAddress(NULL, RISCV_COREMEM_CODE);

		/* Core must be halted while issuing abstract commands */
		eError = RGXRiscvHalt(psDevInfo);
		PVR_GOTO_IF_ERROR(eError, cleanup_initfw);

		eError = _ValidateWithDM(pfnDumpDebugPrintf, pvDumpDebugFile,
						psDevInfo, &sFWAddr,
						pui32HostFWCoremem, psDevInfo->ui32FWCorememCodeSizeInBytes,
						"FW coremem code", 0);

		eError = RGXRiscvResume(psDevInfo);
		PVR_GOTO_IF_ERROR(eError, cleanup_initfw);
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
#if defined(EMULATOR)
	PVR_DUMPDEBUG_LOG("Validation of RISC-V FW code is disabled on emulator");
#endif
	PVR_UNREFERENCED_PARAMETER(pfnDumpDebugPrintf);
	PVR_UNREFERENCED_PARAMETER(pvDumpDebugFile);
	PVR_UNREFERENCED_PARAMETER(psDevInfo);
	return PVRSRV_OK;
#endif
}

#if defined(SUPPORT_EXTRA_METASP_DEBUG)
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

	sFWAddr.ui32Addr = RGXFW_BOOTLDR_META_ADDR;
	eError = _ValidateWithSP(NULL, NULL, psDevInfo, &sFWAddr, pbCodeMemoryPointer, psDevInfo->ui32FWCodeSizeInBytes, "FW code", 0);
	if (eError != PVRSRV_OK)
	{
		goto releaseFWCodeMapping;
	}

	if (psDevInfo->ui32FWCorememCodeSizeInBytes)
	{
		eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWCorememMemDesc, (void **)&pbCodeMemoryPointer);
		if (eError != PVRSRV_OK)
		{
			goto releaseFWCoreCodeMapping;
		}

		sFWAddr.ui32Addr = RGXGetFWImageSectionAddress(NULL, META_COREMEM_CODE);

		eError = _ValidateWithSP(NULL, NULL, psDevInfo, &sFWAddr, pbCodeMemoryPointer,
						psDevInfo->ui32FWCorememCodeSizeInBytes, "FW coremem code", 0);
	}

releaseFWCoreCodeMapping:
	if (psDevInfo->ui32FWCorememCodeSizeInBytes)
	{
		DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWCorememMemDesc);
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

 @Function	_RGXDecodePMPC

 @Description

 Return the name for the PM managed Page Catalogues

 @Input ui32PC	 - Page Catalogue number

 @Return   void

******************************************************************************/
static const IMG_CHAR* _RGXDecodePMPC(IMG_UINT32 ui32PC)
{
	const IMG_CHAR* pszPMPC = " (-)";

	switch (ui32PC)
	{
		case 0x8: pszPMPC = " (PM-VCE0)"; break;
		case 0x9: pszPMPC = " (PM-TE0)"; break;
		case 0xA: pszPMPC = " (PM-ZLS0)"; break;
		case 0xB: pszPMPC = " (PM-ALIST0)"; break;
		case 0xC: pszPMPC = " (PM-VCE1)"; break;
		case 0xD: pszPMPC = " (PM-TE1)"; break;
		case 0xE: pszPMPC = " (PM-ZLS1)"; break;
		case 0xF: pszPMPC = " (PM-ALIST1)"; break;
	}

	return pszPMPC;
}

/*!
*******************************************************************************

 @Function	_RGXDecodeBIFReqTags

 @Description

 Decode the BIF Tag ID and sideband data fields from BIF_FAULT_BANK_REQ_STATUS regs

 @Input eBankID             - BIF identifier
 @Input ui32TagID           - Tag ID value
 @Input ui32TagSB           - Tag Sideband data
 @Output ppszTagID          - Decoded string from the Tag ID
 @Output ppszTagSB          - Decoded string from the Tag SB
 @Output pszScratchBuf      - Buffer provided to the function to generate the debug strings
 @Input ui32ScratchBufSize  - Size of the provided buffer

 @Return   void

******************************************************************************/
#include "rgxmhdefs_km.h"

static void _RGXDecodeBIFReqTagsXE(PVRSRV_RGXDEV_INFO	*psDevInfo,
								   IMG_UINT32	ui32TagID,
								   IMG_UINT32	ui32TagSB,
								   IMG_CHAR		**ppszTagID,
								   IMG_CHAR		**ppszTagSB,
								   IMG_CHAR		*pszScratchBuf,
								   IMG_UINT32	ui32ScratchBufSize)
{
	/* default to unknown */
	IMG_CHAR *pszTagID = "-";
	IMG_CHAR *pszTagSB = "-";
	IMG_BOOL bNewTagEncoding = IMG_FALSE;

	PVR_ASSERT(ppszTagID != NULL);
	PVR_ASSERT(ppszTagSB != NULL);

	/* tags updated for all cores (auto & consumer) with branch > 36 or only auto cores with branch = 36 */
	if ((psDevInfo->sDevFeatureCfg.ui32B > 36) ||
	    (RGX_IS_FEATURE_SUPPORTED(psDevInfo, TILE_REGION_PROTECTION) && (psDevInfo->sDevFeatureCfg.ui32B == 36)))
	{
		bNewTagEncoding = IMG_TRUE;
	}

	switch (ui32TagID)
	{
		/* MMU tags */
		case RGX_MH_TAG_ENCODING_MH_TAG_MMU_PT:
		case RGX_MH_TAG_ENCODING_MH_TAG_MMU_PD:
		case RGX_MH_TAG_ENCODING_MH_TAG_MMU_PC:
		case RGX_MH_TAG_ENCODING_MH_TAG_MMU_PM:
		{
			switch (ui32TagID)
			{
				case RGX_MH_TAG_ENCODING_MH_TAG_MMU_PT:	pszTagID = "MMU PT"; break;
				case RGX_MH_TAG_ENCODING_MH_TAG_MMU_PD:	pszTagID = "MMU PD"; break;
				case RGX_MH_TAG_ENCODING_MH_TAG_MMU_PC:	pszTagID = "MMU PC"; break;
				case RGX_MH_TAG_ENCODING_MH_TAG_MMU_PM:	pszTagID = "MMU PM"; break;
			}
			switch (ui32TagSB)
			{
				case RGX_MH_TAG_SB_MMU_ENCODING_MMU_TAG_PT_REQUEST:		pszTagSB = "PT"; break;
				case RGX_MH_TAG_SB_MMU_ENCODING_MMU_TAG_PD_REQUEST:		pszTagSB = "PD"; break;
				case RGX_MH_TAG_SB_MMU_ENCODING_MMU_TAG_PC_REQUEST:		pszTagSB = "PC"; break;
				case RGX_MH_TAG_SB_MMU_ENCODING_MMU_TAG_PM_PT_REQUEST:	pszTagSB = "PM PT"; break;
				case RGX_MH_TAG_SB_MMU_ENCODING_MMU_TAG_PM_PD_REQUEST:	pszTagSB = "PM PD"; break;
				case RGX_MH_TAG_SB_MMU_ENCODING_MMU_TAG_PM_PC_REQUEST:	pszTagSB = "PM PC"; break;
				case RGX_MH_TAG_SB_MMU_ENCODING_MMU_TAG_PM_PD_WREQUEST:	pszTagSB = "PM PD W"; break;
				case RGX_MH_TAG_SB_MMU_ENCODING_MMU_TAG_PM_PC_WREQUEST:	pszTagSB = "PM PC W"; break;
			}
			break;
		}

		/* MIPS */
		case RGX_MH_TAG_ENCODING_MH_TAG_MIPS:
		{
			pszTagID = "MIPS";
			switch (ui32TagSB)
			{
				case RGX_MH_TAG_SB_MIPS_ENCODING_MIPS_TAG_OPCODE_FETCH:	pszTagSB = "Opcode"; break;
				case RGX_MH_TAG_SB_MIPS_ENCODING_MIPS_TAG_DATA_ACCESS:	pszTagSB = "Data"; break;
			}
			break;
		}

		/* CDM tags */
		case RGX_MH_TAG_ENCODING_MH_TAG_CDM_STG0:
		case RGX_MH_TAG_ENCODING_MH_TAG_CDM_STG1:
		case RGX_MH_TAG_ENCODING_MH_TAG_CDM_STG2:
		case RGX_MH_TAG_ENCODING_MH_TAG_CDM_STG3:
		{
			switch (ui32TagID)
			{
				case RGX_MH_TAG_ENCODING_MH_TAG_CDM_STG0:	pszTagID = "CDM Stage 0"; break;
				case RGX_MH_TAG_ENCODING_MH_TAG_CDM_STG1:	pszTagID = "CDM Stage 1"; break;
				case RGX_MH_TAG_ENCODING_MH_TAG_CDM_STG2:	pszTagID = "CDM Stage 2"; break;
				case RGX_MH_TAG_ENCODING_MH_TAG_CDM_STG3:	pszTagID = "CDM Stage 3"; break;
			}
			switch (ui32TagSB)
			{
				case RGX_MH_TAG_SB_CDM_ENCODING_CDM_TAG_CONTROL_STREAM:	pszTagSB = "Control"; break;
				case RGX_MH_TAG_SB_CDM_ENCODING_CDM_TAG_INDIRECT_DATA:	pszTagSB = "Indirect"; break;
				case RGX_MH_TAG_SB_CDM_ENCODING_CDM_TAG_EVENT_DATA:		pszTagSB = "Event"; break;
				case RGX_MH_TAG_SB_CDM_ENCODING_CDM_TAG_CONTEXT_STATE:	pszTagSB = "Context"; break;
			}
			break;
		}

		/* VDM tags */
		case RGX_MH_TAG_ENCODING_MH_TAG_VDM_STG0:
		case RGX_MH_TAG_ENCODING_MH_TAG_VDM_STG1:
		case RGX_MH_TAG_ENCODING_MH_TAG_VDM_STG2:
		case RGX_MH_TAG_ENCODING_MH_TAG_VDM_STG3:
		case RGX_MH_TAG_ENCODING_MH_TAG_VDM_STG4:
		{
			switch (ui32TagID)
			{
				case RGX_MH_TAG_ENCODING_MH_TAG_VDM_STG0:	pszTagID = "VDM Stage 0"; break;
				case RGX_MH_TAG_ENCODING_MH_TAG_VDM_STG1:	pszTagID = "VDM Stage 1"; break;
				case RGX_MH_TAG_ENCODING_MH_TAG_VDM_STG2:	pszTagID = "VDM Stage 2"; break;
				case RGX_MH_TAG_ENCODING_MH_TAG_VDM_STG3:	pszTagID = "VDM Stage 3"; break;
				case RGX_MH_TAG_ENCODING_MH_TAG_VDM_STG4:	pszTagID = "VDM Stage 4"; break;
			}
			switch (ui32TagSB)
			{
				case RGX_MH_TAG_SB_VDM_ENCODING_VDM_TAG_CONTROL:	pszTagSB = "Control"; break;
				case RGX_MH_TAG_SB_VDM_ENCODING_VDM_TAG_STATE:		pszTagSB = "State"; break;
				case RGX_MH_TAG_SB_VDM_ENCODING_VDM_TAG_INDEX:		pszTagSB = "Index"; break;
				case RGX_MH_TAG_SB_VDM_ENCODING_VDM_TAG_STACK:		pszTagSB = "Stack"; break;
				case RGX_MH_TAG_SB_VDM_ENCODING_VDM_TAG_CONTEXT:	pszTagSB = "Context"; break;
			}
			break;
		}

		/* PDS */
		case RGX_MH_TAG_ENCODING_MH_TAG_PDS_0:
			pszTagID = "PDS req 0"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_PDS_1:
			pszTagID = "PDS req 1"; break;

		/* MCU */
		case RGX_MH_TAG_ENCODING_MH_TAG_MCU_USCA:
			pszTagID = "MCU USCA"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_MCU_USCB:
			pszTagID = "MCU USCB"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_MCU_USCC:
			pszTagID = "MCU USCC"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_MCU_USCD:
			pszTagID = "MCU USCD"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_MCU_PDS_USCA:
			pszTagID = "MCU PDS USCA"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_MCU_PDS_USCB:
			pszTagID = "MCU PDS USCB"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_MCU_PDS_USCC:
			pszTagID = "MCU PDS USCC"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_MCU_PDS_USCD:
			pszTagID = "MCU PDSUSCD"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_MCU_PDSRW:
			pszTagID = "MCU PDS PDSRW"; break;

		/* TCU */
		case RGX_MH_TAG_ENCODING_MH_TAG_TCU_0:
			pszTagID = "TCU req 0"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_TCU_1:
			pszTagID = "TCU req 1"; break;

		/* FBCDC */
		case RGX_MH_TAG_ENCODING_MH_TAG_FBCDC_0:
			pszTagID = bNewTagEncoding ? "TFBDC_TCU0" : "FBCDC0"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_FBCDC_1:
			pszTagID = bNewTagEncoding ? "TFBDC_ZLS0" : "FBCDC1"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_FBCDC_2:
			pszTagID = bNewTagEncoding ? "TFBDC_TCU1" : "FBCDC2"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_FBCDC_3:
			pszTagID = bNewTagEncoding ? "TFBDC_ZLS1" : "FBCDC3"; break;

		/* USC Shared */
		case RGX_MH_TAG_ENCODING_MH_TAG_USC:
			pszTagID = "USCS"; break;

		/* ISP */
		case RGX_MH_TAG_ENCODING_MH_TAG_ISP_ZLS:
			pszTagID = "ISP0 ZLS"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_ISP_DS:
			pszTagID = "ISP0 DS"; break;

		/* TPF */
		case RGX_MH_TAG_ENCODING_MH_TAG_TPF:
		case RGX_MH_TAG_ENCODING_MH_TAG_TPF_PBCDBIAS:
		case RGX_MH_TAG_ENCODING_MH_TAG_TPF_SPF:
		{
			switch (ui32TagID)
			{
				case RGX_MH_TAG_ENCODING_MH_TAG_TPF:           pszTagID = "TPF0"; break;
				case RGX_MH_TAG_ENCODING_MH_TAG_TPF_PBCDBIAS:  pszTagID = "TPF0 DBIAS"; break;
				case RGX_MH_TAG_ENCODING_MH_TAG_TPF_SPF:       pszTagID = "TPF0 SPF"; break;
			}
			switch (ui32TagSB)
			{
				case RGX_MH_TAG_SB_TPF_ENCODING_TPF_TAG_PDS_STATE:	pszTagSB = "PDS state"; break;
				case RGX_MH_TAG_SB_TPF_ENCODING_TPF_TAG_DEPTH_BIAS:	pszTagSB = "Depth bias"; break;
				case RGX_MH_TAG_SB_TPF_ENCODING_TPF_TAG_FLOOR_OFFSET_DATA:	pszTagSB = "Floor offset"; break;
				case RGX_MH_TAG_SB_TPF_ENCODING_TPF_TAG_DELTA_DATA:	pszTagSB = "Delta"; break;
			}
			break;
		}

		/* IPF */
		case RGX_MH_TAG_ENCODING_MH_TAG_IPF_CREQ:
		case RGX_MH_TAG_ENCODING_MH_TAG_IPF_OTHERS:
		{
			switch (ui32TagID)
			{
				case RGX_MH_TAG_ENCODING_MH_TAG_IPF_CREQ:      pszTagID = "IPF0"; break;
				case RGX_MH_TAG_ENCODING_MH_TAG_IPF_OTHERS:    pszTagID = "IPF0"; break;
			}

			if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, NUM_ISP_IPP_PIPES))
			{
				if (ui32TagID < RGX_GET_FEATURE_VALUE(psDevInfo, NUM_ISP_IPP_PIPES))
				{
					OSSNPrintf(pszScratchBuf, ui32ScratchBufSize, "CReq%d", ui32TagID);
					pszTagSB = pszScratchBuf;
				}
				else if (ui32TagID < 2 * RGX_GET_FEATURE_VALUE(psDevInfo, NUM_ISP_IPP_PIPES))
				{
					ui32TagID -= RGX_GET_FEATURE_VALUE(psDevInfo, NUM_ISP_IPP_PIPES);
					OSSNPrintf(pszScratchBuf, ui32ScratchBufSize, "PReq%d", ui32TagID);
					pszTagSB = pszScratchBuf;
				}
				else
				{
					switch (ui32TagSB - 2 * RGX_GET_FEATURE_VALUE(psDevInfo, NUM_ISP_IPP_PIPES))
					{
						case 0:	pszTagSB = "RReq"; break;
						case 1:	pszTagSB = "DBSC"; break;
						case 2:	pszTagSB = "CPF"; break;
						case 3:	pszTagSB = "Delta"; break;
					}
				}
			}
			break;
		}

		/* VDM Stage 5 (temporary) */
		case RGX_MH_TAG_ENCODING_MH_TAG_VDM_STG5:
			pszTagID = "VDM Stage 5"; break;

		/* TA */
		case RGX_MH_TAG_ENCODING_MH_TAG_TA_PPP:
			pszTagID = "PPP"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_TA_TPWRTC:
			pszTagID = "TPW RTC"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_TA_TEACRTC:
			pszTagID = "TEAC RTC"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_TA_PSGRTC:
			pszTagID = "PSG RTC"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_TA_PSGREGION:
			pszTagID = "PSG Region"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_TA_PSGSTREAM:
			pszTagID = "PSG Stream"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_TA_TPW:
			pszTagID = "TPW"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_TA_TPC:
			pszTagID = "TPC"; break;

		/* PM */
		case RGX_MH_TAG_ENCODING_MH_TAG_PM_ALLOC:
		{
			pszTagID = "PMA";
			switch (ui32TagSB)
			{
				case RGX_MH_TAG_SB_PMA_ENCODING_PM_TAG_PMA_TAFSTACK:	pszTagSB = "TA Fstack"; break;
				case RGX_MH_TAG_SB_PMA_ENCODING_PM_TAG_PMA_TAMLIST:		pszTagSB = "TA MList"; break;
				case RGX_MH_TAG_SB_PMA_ENCODING_PM_TAG_PMA_3DFSTACK:	pszTagSB = "3D Fstack"; break;
				case RGX_MH_TAG_SB_PMA_ENCODING_PM_TAG_PMA_3DMLIST:		pszTagSB = "3D MList"; break;
				case RGX_MH_TAG_SB_PMA_ENCODING_PM_TAG_PMA_PMCTX0:		pszTagSB = "Context0"; break;
				case RGX_MH_TAG_SB_PMA_ENCODING_PM_TAG_PMA_PMCTX1:		pszTagSB = "Context1"; break;
				case RGX_MH_TAG_SB_PMA_ENCODING_PM_TAG_PMA_MAVP:		pszTagSB = "MAVP"; break;
				case RGX_MH_TAG_SB_PMA_ENCODING_PM_TAG_PMA_UFSTACK:		pszTagSB = "UFstack"; break;
				case RGX_MH_TAG_SB_PMA_ENCODING_PM_TAG_PMA_TAMMUSTACK:	pszTagSB = "TA MMUstack"; break;
				case RGX_MH_TAG_SB_PMA_ENCODING_PM_TAG_PMA_3DMMUSTACK:	pszTagSB = "3D MMUstack"; break;
				case RGX_MH_TAG_SB_PMA_ENCODING_PM_TAG_PMA_TAUFSTACK:	pszTagSB = "TA UFstack"; break;
				case RGX_MH_TAG_SB_PMA_ENCODING_PM_TAG_PMA_3DUFSTACK:	pszTagSB = "3D UFstack"; break;
				case RGX_MH_TAG_SB_PMA_ENCODING_PM_TAG_PMA_TAVFP:		pszTagSB = "TA VFP"; break;
			}
			break;
		}
		case RGX_MH_TAG_ENCODING_MH_TAG_PM_DEALLOC:
		{
			pszTagID = "PMD";
			switch (ui32TagSB)
			{
				case RGX_MH_TAG_SB_PMD_ENCODING_PM_TAG_PMD_TAFSTACK:	pszTagSB = "TA Fstack"; break;
				case RGX_MH_TAG_SB_PMD_ENCODING_PM_TAG_PMD_TAMLIST:		pszTagSB = "TA MList"; break;
				case RGX_MH_TAG_SB_PMD_ENCODING_PM_TAG_PMD_3DFSTACK:	pszTagSB = "3D Fstack"; break;
				case RGX_MH_TAG_SB_PMD_ENCODING_PM_TAG_PMD_3DMLIST:		pszTagSB = "3D MList"; break;
				case RGX_MH_TAG_SB_PMD_ENCODING_PM_TAG_PMD_PMCTX0:		pszTagSB = "Context0"; break;
				case RGX_MH_TAG_SB_PMD_ENCODING_PM_TAG_PMD_PMCTX1:		pszTagSB = "Context1"; break;
				case RGX_MH_TAG_SB_PMD_ENCODING_PM_TAG_PMD_UFSTACK:		pszTagSB = "UFstack"; break;
				case RGX_MH_TAG_SB_PMD_ENCODING_PM_TAG_PMD_TAMMUSTACK:	pszTagSB = "TA MMUstack"; break;
				case RGX_MH_TAG_SB_PMD_ENCODING_PM_TAG_PMD_3DMMUSTACK:	pszTagSB = "3D MMUstack"; break;
				case RGX_MH_TAG_SB_PMD_ENCODING_PM_TAG_PMD_TAUFSTACK:	pszTagSB = "TA UFstack"; break;
				case RGX_MH_TAG_SB_PMD_ENCODING_PM_TAG_PMD_3DUFSTACK:	pszTagSB = "3D UFstack"; break;
				case RGX_MH_TAG_SB_PMD_ENCODING_PM_TAG_PMD_TAVFP:		pszTagSB = "TA VFP"; break;
				case RGX_MH_TAG_SB_PMD_ENCODING_PM_TAG_PMD_3DVFP:		pszTagSB = "3D VFP"; break;
			}
			break;
		}

		/* TDM */
		case RGX_MH_TAG_ENCODING_MH_TAG_TDM_DMA:
		{
			pszTagID = "TDM DMA";
			switch (ui32TagSB)
			{
				case RGX_MH_TAG_SB_TDM_DMA_ENCODING_TDM_DMA_TAG_CTL_STREAM: pszTagSB = "Ctl stream"; break;
				case RGX_MH_TAG_SB_TDM_DMA_ENCODING_TDM_DMA_TAG_CTX_BUFFER: pszTagSB = "Ctx buffer"; break;
				case RGX_MH_TAG_SB_TDM_DMA_ENCODING_TDM_DMA_TAG_QUEUE_CTL:  pszTagSB = "Queue ctl"; break;
			}
			break;
		}
		case RGX_MH_TAG_ENCODING_MH_TAG_TDM_CTL:
		{
			pszTagID = "TDM CTL";
			switch (ui32TagSB)
			{
				case RGX_MH_TAG_SB_TDM_CTL_ENCODING_TDM_CTL_TAG_FENCE:   pszTagSB = "Fence"; break;
				case RGX_MH_TAG_SB_TDM_CTL_ENCODING_TDM_CTL_TAG_CONTEXT: pszTagSB = "Context"; break;
				case RGX_MH_TAG_SB_TDM_CTL_ENCODING_TDM_CTL_TAG_QUEUE:   pszTagSB = "Queue"; break;
			}
			break;
		}

		/* PBE */
		case RGX_MH_TAG_ENCODING_MH_TAG_PBE0:
			pszTagID = "PBE0"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_PBE1:
			pszTagID = "PBE1"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_PBE2:
			pszTagID = "PBE2"; break;
		case RGX_MH_TAG_ENCODING_MH_TAG_PBE3:
			pszTagID = "PBE3"; break;
	}

	*ppszTagID = pszTagID;
	*ppszTagSB = pszTagSB;
}

/* RISC-V pf tags */
#define RGX_MH_TAG_ENCODING_MH_TAG_CPU_MMU  (0x00000001U)
#define RGX_MH_TAG_ENCODING_MH_TAG_CPU_IFU  (0x00000002U)
#define RGX_MH_TAG_ENCODING_MH_TAG_CPU_LSU  (0x00000003U)

static void _RGXDecodeBIFReqTagsFwcore(PVRSRV_RGXDEV_INFO *psDevInfo,
									   IMG_UINT32 ui32TagID,
									   IMG_UINT32 ui32TagSB,
									   IMG_CHAR **ppszTagID,
									   IMG_CHAR **ppszTagSB)
{
	/* default to unknown */
	IMG_CHAR *pszTagID = "-";
	IMG_CHAR *pszTagSB = "-";
	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, RISCV_FW_PROCESSOR))
	{
		pszTagSB = "RISC-V";

		switch (ui32TagID)
		{
			case RGX_MH_TAG_ENCODING_MH_TAG_CPU_MMU:	pszTagID = "RISC-V MMU"; break;
			case RGX_MH_TAG_ENCODING_MH_TAG_CPU_IFU:	pszTagID = "RISC-V Instruction Fetch Unit"; break;
			case RGX_MH_TAG_ENCODING_MH_TAG_CPU_LSU:	pszTagID = "RISC-V Load/Store Unit"; break; /* Or Debug Module System Bus */
		}
	}

	*ppszTagID = pszTagID;
	*ppszTagSB = pszTagSB;
}

static void _RGXDecodeBIFReqTags(PVRSRV_RGXDEV_INFO	*psDevInfo,
								 RGXDBG_BIF_ID	eBankID,
								 IMG_UINT32		ui32TagID,
								 IMG_UINT32		ui32TagSB,
								 IMG_CHAR		**ppszTagID,
								 IMG_CHAR		**ppszTagSB,
								 IMG_CHAR		*pszScratchBuf,
								 IMG_UINT32		ui32ScratchBufSize)
{
	/* default to unknown */
	IMG_CHAR *pszTagID = "-";
	IMG_CHAR *pszTagSB = "-";

	PVR_ASSERT(ppszTagID != NULL);
	PVR_ASSERT(ppszTagSB != NULL);

	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, XE_MEMORY_HIERARCHY))
	{
		if (eBankID == RGXDBG_FWCORE)
		{
			_RGXDecodeBIFReqTagsFwcore(psDevInfo, ui32TagID, ui32TagSB, ppszTagID, ppszTagSB);
		}
		else
		{
			_RGXDecodeBIFReqTagsXE(psDevInfo, ui32TagID, ui32TagSB, ppszTagID, ppszTagSB, pszScratchBuf, ui32ScratchBufSize);
		}
		return;
	}

	switch (ui32TagID)
	{
		case 0x0:
		{
			pszTagID = "MMU";
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "Table"; break;
				case 0x1: pszTagSB = "Directory"; break;
				case 0x2: pszTagSB = "Catalogue"; break;
			}
			break;
		}
		case 0x1:
		{
			pszTagID = "TLA";
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "Pixel data"; break;
				case 0x1: pszTagSB = "Command stream data"; break;
				case 0x2: pszTagSB = "Fence or flush"; break;
			}
			break;
		}
		case 0x2:
		{
			pszTagID = "HOST";
			break;
		}
		case 0x3:
		{
			if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META))
			{
					pszTagID = "META";
					switch (ui32TagSB)
					{
						case 0x0: pszTagSB = "DCache - Thread 0"; break;
						case 0x1: pszTagSB = "ICache - Thread 0"; break;
						case 0x2: pszTagSB = "JTag - Thread 0"; break;
						case 0x3: pszTagSB = "Slave bus - Thread 0"; break;
						case 0x4: pszTagSB = "DCache - Thread "; break;
						case 0x5: pszTagSB = "ICache - Thread 1"; break;
						case 0x6: pszTagSB = "JTag - Thread 1"; break;
						case 0x7: pszTagSB = "Slave bus - Thread 1"; break;
					}
			}
			else if (RGX_IS_ERN_SUPPORTED(psDevInfo, 57596))
			{
				pszTagID="TCU";
			}
			else
			{
				/* Unreachable code */
				PVR_ASSERT(IMG_FALSE);
			}
			break;
		}
		case 0x4:
		{
			pszTagID = "USC";
			OSSNPrintf(pszScratchBuf, ui32ScratchBufSize,
			           "Cache line %d", (ui32TagSB & 0x3f));
			pszTagSB = pszScratchBuf;
			break;
		}
		case 0x5:
		{
			pszTagID = "PBE";
			break;
		}
		case 0x6:
		{
			pszTagID = "ISP";
			switch (ui32TagSB)
			{
				case 0x00: pszTagSB = "ZLS"; break;
				case 0x20: pszTagSB = "Occlusion Query"; break;
			}
			break;
		}
		case 0x7:
		{
			if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, CLUSTER_GROUPING))
			{
				if (eBankID == RGXDBG_TEXAS_BIF)
				{
					pszTagID = "IPF";
					switch (ui32TagSB)
					{
						case 0x0: pszTagSB = "CPF"; break;
						case 0x1: pszTagSB = "DBSC"; break;
						case 0x2:
						case 0x4:
						case 0x6:
						case 0x8: pszTagSB = "Control Stream"; break;
						case 0x3:
						case 0x5:
						case 0x7:
						case 0x9: pszTagSB = "Primitive Block"; break;
					}
				}
				else
				{
					pszTagID = "IPP";
					switch (ui32TagSB)
					{
						case 0x0: pszTagSB = "Macrotile Header"; break;
						case 0x1: pszTagSB = "Region Header"; break;
					}
				}
			}
			else if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, SIMPLE_INTERNAL_PARAMETER_FORMAT))
			{
				pszTagID = "IPF";
				switch (ui32TagSB)
				{
					case 0x0: pszTagSB = "Region Header"; break;
					case 0x1: pszTagSB = "DBSC"; break;
					case 0x2: pszTagSB = "CPF"; break;
					case 0x3: pszTagSB = "Control Stream"; break;
					case 0x4: pszTagSB = "Primitive Block"; break;
				}
			}
			else
			{
				pszTagID = "IPF";
				switch (ui32TagSB)
				{
					case 0x0: pszTagSB = "Macrotile Header"; break;
					case 0x1: pszTagSB = "Region Header"; break;
					case 0x2: pszTagSB = "DBSC"; break;
					case 0x3: pszTagSB = "CPF"; break;
					case 0x4:
					case 0x6:
					case 0x8: pszTagSB = "Control Stream"; break;
					case 0x5:
					case 0x7:
					case 0x9: pszTagSB = "Primitive Block"; break;
				}
			}
			break;
		}
		case 0x8:
		{
			pszTagID = "CDM";
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "Control Stream"; break;
				case 0x1: pszTagSB = "Indirect Data"; break;
				case 0x2: pszTagSB = "Event Write"; break;
				case 0x3: pszTagSB = "Context State"; break;
			}
			break;
		}
		case 0x9:
		{
			pszTagID = "VDM";
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "Control Stream"; break;
				case 0x1: pszTagSB = "PPP State"; break;
				case 0x2: pszTagSB = "Index Data"; break;
				case 0x4: pszTagSB = "Call Stack"; break;
				case 0x8: pszTagSB = "Context State"; break;
			}
			break;
		}
		case 0xA:
		{
			pszTagID = "PM";
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "PMA_TAFSTACK"; break;
				case 0x1: pszTagSB = "PMA_TAMLIST"; break;
				case 0x2: pszTagSB = "PMA_3DFSTACK"; break;
				case 0x3: pszTagSB = "PMA_3DMLIST"; break;
				case 0x4: pszTagSB = "PMA_PMCTX0"; break;
				case 0x5: pszTagSB = "PMA_PMCTX1"; break;
				case 0x6: pszTagSB = "PMA_MAVP"; break;
				case 0x7: pszTagSB = "PMA_UFSTACK"; break;
				case 0x8: pszTagSB = "PMD_TAFSTACK"; break;
				case 0x9: pszTagSB = "PMD_TAMLIST"; break;
				case 0xA: pszTagSB = "PMD_3DFSTACK"; break;
				case 0xB: pszTagSB = "PMD_3DMLIST"; break;
				case 0xC: pszTagSB = "PMD_PMCTX0"; break;
				case 0xD: pszTagSB = "PMD_PMCTX1"; break;
				case 0xF: pszTagSB = "PMD_UFSTACK"; break;
				case 0x10: pszTagSB = "PMA_TAMMUSTACK"; break;
				case 0x11: pszTagSB = "PMA_3DMMUSTACK"; break;
				case 0x12: pszTagSB = "PMD_TAMMUSTACK"; break;
				case 0x13: pszTagSB = "PMD_3DMMUSTACK"; break;
				case 0x14: pszTagSB = "PMA_TAUFSTACK"; break;
				case 0x15: pszTagSB = "PMA_3DUFSTACK"; break;
				case 0x16: pszTagSB = "PMD_TAUFSTACK"; break;
				case 0x17: pszTagSB = "PMD_3DUFSTACK"; break;
				case 0x18: pszTagSB = "PMA_TAVFP"; break;
				case 0x19: pszTagSB = "PMD_3DVFP"; break;
				case 0x1A: pszTagSB = "PMD_TAVFP"; break;
			}
			break;
		}
		case 0xB:
		{
			pszTagID = "TA";
			switch (ui32TagSB)
			{
				case 0x1: pszTagSB = "VCE"; break;
				case 0x2: pszTagSB = "TPC"; break;
				case 0x3: pszTagSB = "TE Control Stream"; break;
				case 0x4: pszTagSB = "TE Region Header"; break;
				case 0x5: pszTagSB = "TE Render Target Cache"; break;
				case 0x6: pszTagSB = "TEAC Render Target Cache"; break;
				case 0x7: pszTagSB = "VCE Render Target Cache"; break;
				case 0x8: pszTagSB = "PPP Context State"; break;
			}
			break;
		}
		case 0xC:
		{
			pszTagID = "TPF";
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "TPF0: Primitive Block"; break;
				case 0x1: pszTagSB = "TPF0: Depth Bias"; break;
				case 0x2: pszTagSB = "TPF0: Per Primitive IDs"; break;
				case 0x3: pszTagSB = "CPF - Tables"; break;
				case 0x4: pszTagSB = "TPF1: Primitive Block"; break;
				case 0x5: pszTagSB = "TPF1: Depth Bias"; break;
				case 0x6: pszTagSB = "TPF1: Per Primitive IDs"; break;
				case 0x7: pszTagSB = "CPF - Data: Pipe 0"; break;
				case 0x8: pszTagSB = "TPF2: Primitive Block"; break;
				case 0x9: pszTagSB = "TPF2: Depth Bias"; break;
				case 0xA: pszTagSB = "TPF2: Per Primitive IDs"; break;
				case 0xB: pszTagSB = "CPF - Data: Pipe 1"; break;
				case 0xC: pszTagSB = "TPF3: Primitive Block"; break;
				case 0xD: pszTagSB = "TPF3: Depth Bias"; break;
				case 0xE: pszTagSB = "TPF3: Per Primitive IDs"; break;
				case 0xF: pszTagSB = "CPF - Data: Pipe 2"; break;
			}
			break;
		}
		case 0xD:
		{
			pszTagID = "PDS";
			break;
		}
		case 0xE:
		{
			pszTagID = "MCU";
			{
				IMG_UINT32 ui32Burst = (ui32TagSB >> 5) & 0x7;
				IMG_UINT32 ui32GroupEnc = (ui32TagSB >> 2) & 0x7;
				IMG_UINT32 ui32Group = ui32TagSB & 0x3;

				IMG_CHAR* pszBurst = "";
				IMG_CHAR* pszGroupEnc = "";
				IMG_CHAR* pszGroup = "";

				switch (ui32Burst)
				{
					case 0x0:
					case 0x1: pszBurst = "128bit word within the Lower 256bits"; break;
					case 0x2:
					case 0x3: pszBurst = "128bit word within the Upper 256bits"; break;
					case 0x4: pszBurst = "Lower 256bits"; break;
					case 0x5: pszBurst = "Upper 256bits"; break;
					case 0x6: pszBurst = "512 bits"; break;
				}
				switch (ui32GroupEnc)
				{
					case 0x0: pszGroupEnc = "TPUA_USC"; break;
					case 0x1: pszGroupEnc = "TPUB_USC"; break;
					case 0x2: pszGroupEnc = "USCA_USC"; break;
					case 0x3: pszGroupEnc = "USCB_USC"; break;
					case 0x4: pszGroupEnc = "PDS_USC"; break;
					case 0x5:
						if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, NUM_CLUSTERS) &&
							6 > RGX_GET_FEATURE_VALUE(psDevInfo, NUM_CLUSTERS))
						{
							pszGroupEnc = "PDSRW";
						} else if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, NUM_CLUSTERS) &&
							6 == RGX_GET_FEATURE_VALUE(psDevInfo, NUM_CLUSTERS))
						{
							pszGroupEnc = "UPUC_USC";
						}
						break;
					case 0x6:
						if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, NUM_CLUSTERS) &&
							6 == RGX_GET_FEATURE_VALUE(psDevInfo, NUM_CLUSTERS))
						{
							pszGroupEnc = "TPUC_USC";
						}
						break;
					case 0x7:
						if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, NUM_CLUSTERS) &&
							6 == RGX_GET_FEATURE_VALUE(psDevInfo, NUM_CLUSTERS))
						{
							pszGroupEnc = "PDSRW";
						}
						break;
				}
				switch (ui32Group)
				{
					case 0x0: pszGroup = "Banks 0-3"; break;
					case 0x1: pszGroup = "Banks 4-7"; break;
					case 0x2: pszGroup = "Banks 8-11"; break;
					case 0x3: pszGroup = "Banks 12-15"; break;
				}

				OSSNPrintf(pszScratchBuf, ui32ScratchBufSize,
								"%s, %s, %s", pszBurst, pszGroupEnc, pszGroup);
				pszTagSB = pszScratchBuf;
			}
			break;
		}
		case 0xF:
		{
			pszTagID = "FB_CDC";

			if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, XT_TOP_INFRASTRUCTURE))
			{
				IMG_UINT32 ui32Req   = (ui32TagSB >> 0) & 0xf;
				IMG_UINT32 ui32MCUSB = (ui32TagSB >> 4) & 0x3;
				IMG_CHAR* pszReqOrig = "";

				switch (ui32Req)
				{
					case 0x0: pszReqOrig = "FBC Request, originator ZLS"; break;
					case 0x1: pszReqOrig = "FBC Request, originator PBE"; break;
					case 0x2: pszReqOrig = "FBC Request, originator Host"; break;
					case 0x3: pszReqOrig = "FBC Request, originator TLA"; break;
					case 0x4: pszReqOrig = "FBDC Request, originator ZLS"; break;
					case 0x5: pszReqOrig = "FBDC Request, originator MCU"; break;
					case 0x6: pszReqOrig = "FBDC Request, originator Host"; break;
					case 0x7: pszReqOrig = "FBDC Request, originator TLA"; break;
					case 0x8: pszReqOrig = "FBC Request, originator ZLS Requester Fence"; break;
					case 0x9: pszReqOrig = "FBC Request, originator PBE Requester Fence"; break;
					case 0xa: pszReqOrig = "FBC Request, originator Host Requester Fence"; break;
					case 0xb: pszReqOrig = "FBC Request, originator TLA Requester Fence"; break;
					case 0xc: pszReqOrig = "Reserved"; break;
					case 0xd: pszReqOrig = "Reserved"; break;
					case 0xe: pszReqOrig = "FBDC Request, originator FBCDC(Host) Memory Fence"; break;
					case 0xf: pszReqOrig = "FBDC Request, originator FBCDC(TLA) Memory Fence"; break;
				}
				OSSNPrintf(pszScratchBuf, ui32ScratchBufSize,
				           "%s, MCU sideband 0x%X", pszReqOrig, ui32MCUSB);
				pszTagSB = pszScratchBuf;
			}
			else
			{
				IMG_UINT32 ui32Req   = (ui32TagSB >> 2) & 0x7;
				IMG_UINT32 ui32MCUSB = (ui32TagSB >> 0) & 0x3;
				IMG_CHAR* pszReqOrig = "";

				switch (ui32Req)
				{
					case 0x0: pszReqOrig = "FBC Request, originator ZLS";   break;
					case 0x1: pszReqOrig = "FBC Request, originator PBE";   break;
					case 0x2: pszReqOrig = "FBC Request, originator Host";  break;
					case 0x3: pszReqOrig = "FBC Request, originator TLA";   break;
					case 0x4: pszReqOrig = "FBDC Request, originator ZLS";  break;
					case 0x5: pszReqOrig = "FBDC Request, originator MCU";  break;
					case 0x6: pszReqOrig = "FBDC Request, originator Host"; break;
					case 0x7: pszReqOrig = "FBDC Request, originator TLA";  break;
				}
				OSSNPrintf(pszScratchBuf, ui32ScratchBufSize,
				           "%s, MCU sideband 0x%X", pszReqOrig, ui32MCUSB);
				pszTagSB = pszScratchBuf;
			}
			break;
		}
	} /* switch (TagID) */

	*ppszTagID = pszTagID;
	*ppszTagSB = pszTagSB;
}



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
 @Input ui32TagSB           - Tag Sideband data
 @Input bRead               - Read flag
 @Output ppszTagID          - Decoded string from the Tag ID
 @Output ppszTagSB          - Decoded string from the Tag SB
 @Output pszScratchBuf      - Buffer provided to the function to generate the debug strings
 @Input ui32ScratchBufSize  - Size of the provided buffer

 @Return   void

******************************************************************************/
static void _RGXDecodeMMUReqTags(PVRSRV_RGXDEV_INFO    *psDevInfo,
								IMG_UINT32  ui32TagID,
								 IMG_UINT32  ui32TagSB,
								 IMG_BOOL    bRead,
								 IMG_CHAR    **ppszTagID,
								 IMG_CHAR    **ppszTagSB,
								 IMG_CHAR    *pszScratchBuf,
								 IMG_UINT32  ui32ScratchBufSize)
{
	IMG_INT32  i32SideBandType = -1;
	IMG_CHAR   *pszTagID = "-";
	IMG_CHAR   *pszTagSB = "-";

	PVR_ASSERT(ppszTagID != NULL);
	PVR_ASSERT(ppszTagSB != NULL);


	switch (ui32TagID)
	{
		case  0: pszTagID = "META (Jones)"; i32SideBandType = RGXDBG_META; break;
		case  1: pszTagID = "TLA (Jones)"; i32SideBandType = RGXDBG_TLA; break;
		case  2: pszTagID = "DMA (Jones)"; i32SideBandType = RGXDBG_DMA; break;
		case  3: pszTagID = "VDMM (Jones)"; i32SideBandType = RGXDBG_VDMM; break;
		case  4: pszTagID = "CDM (Jones)"; i32SideBandType = RGXDBG_CDM; break;
		case  5: pszTagID = "IPP (Jones)"; i32SideBandType = RGXDBG_IPP; break;
		case  6: pszTagID = "PM (Jones)"; i32SideBandType = RGXDBG_PM; break;
		case  7: pszTagID = "Tiling (Jones)"; i32SideBandType = RGXDBG_TILING; break;
		case  8: pszTagID = "MCU (Texas 0)"; i32SideBandType = RGXDBG_MCU; break;
		case 12: pszTagID = "VDMS (Black Pearl 0)"; i32SideBandType = RGXDBG_VDMS; break;
		case 13: pszTagID = "IPF (Black Pearl 0)"; i32SideBandType = RGXDBG_IPF; break;
		case 14: pszTagID = "ISP (Black Pearl 0)"; i32SideBandType = RGXDBG_ISP; break;
		case 15: pszTagID = "TPF (Black Pearl 0)"; i32SideBandType = RGXDBG_TPF; break;
		case 16: pszTagID = "USCS (Black Pearl 0)"; i32SideBandType = RGXDBG_USCS; break;
		case 17: pszTagID = "PPP (Black Pearl 0)"; i32SideBandType = RGXDBG_PPP; break;
		case 20: pszTagID = "MCU (Texas 1)"; i32SideBandType = RGXDBG_MCU; break;
		case 24: pszTagID = "MCU (Texas 2)"; i32SideBandType = RGXDBG_MCU; break;
		case 28: pszTagID = "VDMS (Black Pearl 1)"; i32SideBandType = RGXDBG_VDMS; break;
		case 29: pszTagID = "IPF (Black Pearl 1)"; i32SideBandType = RGXDBG_IPF; break;
		case 30: pszTagID = "ISP (Black Pearl 1)"; i32SideBandType = RGXDBG_ISP; break;
		case 31: pszTagID = "TPF (Black Pearl 1)"; i32SideBandType = RGXDBG_TPF; break;
		case 32: pszTagID = "USCS (Black Pearl 1)"; i32SideBandType = RGXDBG_USCS; break;
		case 33: pszTagID = "PPP (Black Pearl 1)"; i32SideBandType = RGXDBG_PPP; break;
		case 36: pszTagID = "MCU (Texas 3)"; i32SideBandType = RGXDBG_MCU; break;
		case 40: pszTagID = "MCU (Texas 4)"; i32SideBandType = RGXDBG_MCU; break;
		case 44: pszTagID = "VDMS (Black Pearl 2)"; i32SideBandType = RGXDBG_VDMS; break;
		case 45: pszTagID = "IPF (Black Pearl 2)"; i32SideBandType = RGXDBG_IPF; break;
		case 46: pszTagID = "ISP (Black Pearl 2)"; i32SideBandType = RGXDBG_ISP; break;
		case 47: pszTagID = "TPF (Black Pearl 2)"; i32SideBandType = RGXDBG_TPF; break;
		case 48: pszTagID = "USCS (Black Pearl 2)"; i32SideBandType = RGXDBG_USCS; break;
		case 49: pszTagID = "PPP (Black Pearl 2)"; i32SideBandType = RGXDBG_PPP; break;
		case 52: pszTagID = "MCU (Texas 5)"; i32SideBandType = RGXDBG_MCU; break;
		case 56: pszTagID = "MCU (Texas 6)"; i32SideBandType = RGXDBG_MCU; break;
		case 60: pszTagID = "VDMS (Black Pearl 3)"; i32SideBandType = RGXDBG_VDMS; break;
		case 61: pszTagID = "IPF (Black Pearl 3)"; i32SideBandType = RGXDBG_IPF; break;
		case 62: pszTagID = "ISP (Black Pearl 3)"; i32SideBandType = RGXDBG_ISP; break;
		case 63: pszTagID = "TPF (Black Pearl 3)"; i32SideBandType = RGXDBG_TPF; break;
		case 64: pszTagID = "USCS (Black Pearl 3)"; i32SideBandType = RGXDBG_USCS; break;
		case 65: pszTagID = "PPP (Black Pearl 3)"; i32SideBandType = RGXDBG_PPP; break;
		case 68: pszTagID = "MCU (Texas 7)"; i32SideBandType = RGXDBG_MCU; break;
	}
	if (('-' == pszTagID[0]) && '\n' == pszTagID[1])
	{

		if (RGX_IS_ERN_SUPPORTED(psDevInfo, 50539) ||
			(RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, FBCDC_ARCHITECTURE) && RGX_GET_FEATURE_VALUE(psDevInfo, FBCDC_ARCHITECTURE) >= 3))
		{
			switch (ui32TagID)
			{
			case 18: pszTagID = "TPF_CPF (Black Pearl 0)"; i32SideBandType = RGXDBG_TPF_CPF; break;
			case 19: pszTagID = "IPF_CPF (Black Pearl 0)"; i32SideBandType = RGXDBG_IPF_CPF; break;
			case 34: pszTagID = "TPF_CPF (Black Pearl 1)"; i32SideBandType = RGXDBG_TPF_CPF; break;
			case 35: pszTagID = "IPF_CPF (Black Pearl 1)"; i32SideBandType = RGXDBG_IPF_CPF; break;
			case 50: pszTagID = "TPF_CPF (Black Pearl 2)"; i32SideBandType = RGXDBG_TPF_CPF; break;
			case 51: pszTagID = "IPF_CPF (Black Pearl 2)"; i32SideBandType = RGXDBG_IPF_CPF; break;
			case 66: pszTagID = "TPF_CPF (Black Pearl 3)"; i32SideBandType = RGXDBG_TPF_CPF; break;
			case 67: pszTagID = "IPF_CPF (Black Pearl 3)"; i32SideBandType = RGXDBG_IPF_CPF; break;
			}

			if (RGX_IS_ERN_SUPPORTED(psDevInfo, 50539))
			{
				switch (ui32TagID)
				{
				case 9:	pszTagID = "PBE (Texas 0)"; i32SideBandType = RGXDBG_PBE; break;
				case 10: pszTagID = "PDS (Texas 0)"; i32SideBandType = RGXDBG_PDS; break;
				case 11: pszTagID = "FBCDC (Texas 0)"; i32SideBandType = RGXDBG_FBCDC; break;
				case 21: pszTagID = "PBE (Texas 1)"; i32SideBandType = RGXDBG_PBE; break;
				case 22: pszTagID = "PDS (Texas 1)"; i32SideBandType = RGXDBG_PDS; break;
				case 23: pszTagID = "FBCDC (Texas 1)"; i32SideBandType = RGXDBG_FBCDC; break;
				case 25: pszTagID = "PBE (Texas 2)"; i32SideBandType = RGXDBG_PBE; break;
				case 26: pszTagID = "PDS (Texas 2)"; i32SideBandType = RGXDBG_PDS; break;
				case 27: pszTagID = "FBCDC (Texas 2)"; i32SideBandType = RGXDBG_FBCDC; break;
				case 37: pszTagID = "PBE (Texas 3)"; i32SideBandType = RGXDBG_PBE; break;
				case 38: pszTagID = "PDS (Texas 3)"; i32SideBandType = RGXDBG_PDS; break;
				case 39: pszTagID = "FBCDC (Texas 3)"; i32SideBandType = RGXDBG_FBCDC; break;
				case 41: pszTagID = "PBE (Texas 4)"; i32SideBandType = RGXDBG_PBE; break;
				case 42: pszTagID = "PDS (Texas 4)"; i32SideBandType = RGXDBG_PDS; break;
				case 43: pszTagID = "FBCDC (Texas 4)"; i32SideBandType = RGXDBG_FBCDC; break;
				case 53: pszTagID = "PBE (Texas 5)"; i32SideBandType = RGXDBG_PBE; break;
				case 54: pszTagID = "PDS (Texas 5)"; i32SideBandType = RGXDBG_PDS; break;
				case 55: pszTagID = "FBCDC (Texas 5)"; i32SideBandType = RGXDBG_FBCDC; break;
				case 57: pszTagID = "PBE (Texas 6)"; i32SideBandType = RGXDBG_PBE; break;
				case 58: pszTagID = "PDS (Texas 6)"; i32SideBandType = RGXDBG_PDS; break;
				case 59: pszTagID = "FBCDC (Texas 6)"; i32SideBandType = RGXDBG_FBCDC; break;
				case 69: pszTagID = "PBE (Texas 7)"; i32SideBandType = RGXDBG_PBE; break;
				case 70: pszTagID = "PDS (Texas 7)"; i32SideBandType = RGXDBG_PDS; break;
				case 71: pszTagID = "FBCDC (Texas 7)"; i32SideBandType = RGXDBG_FBCDC; break;
				}
			}else
			{
				switch (ui32TagID)
				{
				case 9:	pszTagID = "PDS (Texas 0)"; i32SideBandType = RGXDBG_PDS; break;
				case 10: pszTagID = "PBE (Texas 0)"; i32SideBandType = RGXDBG_PBE; break;
				case 11: pszTagID = "FBCDC (Texas 0)"; i32SideBandType = RGXDBG_FBCDC; break;
				case 21: pszTagID = "PDS (Texas 1)"; i32SideBandType = RGXDBG_PDS; break;
				case 22: pszTagID = "PBE (Texas 1)"; i32SideBandType = RGXDBG_PBE; break;
				case 23: pszTagID = "FBCDC (Texas 1)"; i32SideBandType = RGXDBG_FBCDC; break;
				case 25: pszTagID = "PDS (Texas 2)"; i32SideBandType = RGXDBG_PDS; break;
				case 26: pszTagID = "PBE (Texas 2)"; i32SideBandType = RGXDBG_PBE; break;
				case 27: pszTagID = "FBCDC (Texas 2)"; i32SideBandType = RGXDBG_FBCDC; break;
				case 37: pszTagID = "PDS (Texas 3)"; i32SideBandType = RGXDBG_PDS; break;
				case 38: pszTagID = "PBE (Texas 3)"; i32SideBandType = RGXDBG_PBE; break;
				case 39: pszTagID = "FBCDC (Texas 3)"; i32SideBandType = RGXDBG_FBCDC; break;
				case 41: pszTagID = "PDS (Texas 4)"; i32SideBandType = RGXDBG_PDS; break;
				case 42: pszTagID = "PBE (Texas 4)"; i32SideBandType = RGXDBG_PBE; break;
				case 43: pszTagID = "FBCDC (Texas 4)"; i32SideBandType = RGXDBG_FBCDC; break;
				case 53: pszTagID = "PDS (Texas 5)"; i32SideBandType = RGXDBG_PDS; break;
				case 54: pszTagID = "PBE (Texas 5)"; i32SideBandType = RGXDBG_PBE; break;
				case 55: pszTagID = "FBCDC (Texas 5)"; i32SideBandType = RGXDBG_FBCDC; break;
				case 57: pszTagID = "PDS (Texas 6)"; i32SideBandType = RGXDBG_PDS; break;
				case 58: pszTagID = "PBE (Texas 6)"; i32SideBandType = RGXDBG_PBE; break;
				case 59: pszTagID = "FBCDC (Texas 6)"; i32SideBandType = RGXDBG_FBCDC; break;
				case 69: pszTagID = "PDS (Texas 7)"; i32SideBandType = RGXDBG_PDS; break;
				case 70: pszTagID = "PBE (Texas 7)"; i32SideBandType = RGXDBG_PBE; break;
				case 71: pszTagID = "FBCDC (Texas 7)"; i32SideBandType = RGXDBG_FBCDC; break;
				}
			}
		}else
		{
			switch (ui32TagID)
			{
			case 9:	pszTagID = "PDS (Texas 0)"; i32SideBandType = RGXDBG_PDS; break;
			case 10: pszTagID = "PBE0 (Texas 0)"; i32SideBandType = RGXDBG_PBE; break;
			case 11: pszTagID = "PBE1 (Texas 0)"; i32SideBandType = RGXDBG_PBE; break;
			case 18: pszTagID = "VCE (Black Pearl 0)"; i32SideBandType = RGXDBG_VCE; break;
			case 19: pszTagID = "FBCDC (Black Pearl 0)"; i32SideBandType = RGXDBG_FBCDC; break;
			case 21: pszTagID = "PDS (Texas 1)"; i32SideBandType = RGXDBG_PDS; break;
			case 22: pszTagID = "PBE0 (Texas 1)"; i32SideBandType = RGXDBG_PBE; break;
			case 23: pszTagID = "PBE1 (Texas 1)"; i32SideBandType = RGXDBG_PBE; break;
			case 25: pszTagID = "PDS (Texas 2)"; i32SideBandType = RGXDBG_PDS; break;
			case 26: pszTagID = "PBE0 (Texas 2)"; i32SideBandType = RGXDBG_PBE; break;
			case 27: pszTagID = "PBE1 (Texas 2)"; i32SideBandType = RGXDBG_PBE; break;
			case 34: pszTagID = "VCE (Black Pearl 1)"; i32SideBandType = RGXDBG_VCE; break;
			case 35: pszTagID = "FBCDC (Black Pearl 1)"; i32SideBandType = RGXDBG_FBCDC; break;
			case 37: pszTagID = "PDS (Texas 3)"; i32SideBandType = RGXDBG_PDS; break;
			case 38: pszTagID = "PBE0 (Texas 3)"; i32SideBandType = RGXDBG_PBE; break;
			case 39: pszTagID = "PBE1 (Texas 3)"; i32SideBandType = RGXDBG_PBE; break;
			case 41: pszTagID = "PDS (Texas 4)"; i32SideBandType = RGXDBG_PDS; break;
			case 42: pszTagID = "PBE0 (Texas 4)"; i32SideBandType = RGXDBG_PBE; break;
			case 43: pszTagID = "PBE1 (Texas 4)"; i32SideBandType = RGXDBG_PBE; break;
			case 50: pszTagID = "VCE (Black Pearl 2)"; i32SideBandType = RGXDBG_VCE; break;
			case 51: pszTagID = "FBCDC (Black Pearl 2)"; i32SideBandType = RGXDBG_FBCDC; break;
			case 53: pszTagID = "PDS (Texas 5)"; i32SideBandType = RGXDBG_PDS; break;
			case 54: pszTagID = "PBE0 (Texas 5)"; i32SideBandType = RGXDBG_PBE; break;
			case 55: pszTagID = "PBE1 (Texas 5)"; i32SideBandType = RGXDBG_PBE; break;
			case 57: pszTagID = "PDS (Texas 6)"; i32SideBandType = RGXDBG_PDS; break;
			case 58: pszTagID = "PBE0 (Texas 6)"; i32SideBandType = RGXDBG_PBE; break;
			case 59: pszTagID = "PBE1 (Texas 6)"; i32SideBandType = RGXDBG_PBE; break;
			case 66: pszTagID = "VCE (Black Pearl 3)"; i32SideBandType = RGXDBG_VCE; break;
			case 67: pszTagID = "FBCDC (Black Pearl 3)"; i32SideBandType = RGXDBG_FBCDC; break;
			case 69: pszTagID = "PDS (Texas 7)"; i32SideBandType = RGXDBG_PDS; break;
			case 70: pszTagID = "PBE0 (Texas 7)"; i32SideBandType = RGXDBG_PBE; break;
			case 71: pszTagID = "PBE1 (Texas 7)"; i32SideBandType = RGXDBG_PBE; break;
			}
		}

	}

	switch (i32SideBandType)
	{
		case RGXDBG_META:
		{
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "DCache - Thread 0"; break;
				case 0x1: pszTagSB = "ICache - Thread 0"; break;
				case 0x2: pszTagSB = "JTag - Thread 0"; break;
				case 0x3: pszTagSB = "Slave bus - Thread 0"; break;
				case 0x4: pszTagSB = "DCache - Thread 1"; break;
				case 0x5: pszTagSB = "ICache - Thread 1"; break;
				case 0x6: pszTagSB = "JTag - Thread 1"; break;
				case 0x7: pszTagSB = "Slave bus - Thread 1"; break;
			}
			break;
		}

		case RGXDBG_TLA:
		{
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "Pixel data"; break;
				case 0x1: pszTagSB = "Command stream data"; break;
				case 0x2: pszTagSB = "Fence or flush"; break;
			}
			break;
		}

		case RGXDBG_VDMM:
		{
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "Control Stream - Read Only"; break;
				case 0x1: pszTagSB = "PPP State - Read Only"; break;
				case 0x2: pszTagSB = "Indices - Read Only"; break;
				case 0x4: pszTagSB = "Call Stack - Read/Write"; break;
				case 0x6: pszTagSB = "DrawIndirect - Read Only"; break;
				case 0xA: pszTagSB = "Context State - Write Only"; break;
			}
			break;
		}

		case RGXDBG_CDM:
		{
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "Control Stream"; break;
				case 0x1: pszTagSB = "Indirect Data"; break;
				case 0x2: pszTagSB = "Event Write"; break;
				case 0x3: pszTagSB = "Context State"; break;
			}
			break;
		}

		case RGXDBG_IPP:
		{
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "Macrotile Header"; break;
				case 0x1: pszTagSB = "Region Header"; break;
			}
			break;
		}

		case RGXDBG_PM:
		{
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "PMA_TAFSTACK"; break;
				case 0x1: pszTagSB = "PMA_TAMLIST"; break;
				case 0x2: pszTagSB = "PMA_3DFSTACK"; break;
				case 0x3: pszTagSB = "PMA_3DMLIST"; break;
				case 0x4: pszTagSB = "PMA_PMCTX0"; break;
				case 0x5: pszTagSB = "PMA_PMCTX1"; break;
				case 0x6: pszTagSB = "PMA_MAVP"; break;
				case 0x7: pszTagSB = "PMA_UFSTACK"; break;
				case 0x8: pszTagSB = "PMD_TAFSTACK"; break;
				case 0x9: pszTagSB = "PMD_TAMLIST"; break;
				case 0xA: pszTagSB = "PMD_3DFSTACK"; break;
				case 0xB: pszTagSB = "PMD_3DMLIST"; break;
				case 0xC: pszTagSB = "PMD_PMCTX0"; break;
				case 0xD: pszTagSB = "PMD_PMCTX1"; break;
				case 0xF: pszTagSB = "PMD_UFSTACK"; break;
				case 0x10: pszTagSB = "PMA_TAMMUSTACK"; break;
				case 0x11: pszTagSB = "PMA_3DMMUSTACK"; break;
				case 0x12: pszTagSB = "PMD_TAMMUSTACK"; break;
				case 0x13: pszTagSB = "PMD_3DMMUSTACK"; break;
				case 0x14: pszTagSB = "PMA_TAUFSTACK"; break;
				case 0x15: pszTagSB = "PMA_3DUFSTACK"; break;
				case 0x16: pszTagSB = "PMD_TAUFSTACK"; break;
				case 0x17: pszTagSB = "PMD_3DUFSTACK"; break;
				case 0x18: pszTagSB = "PMA_TAVFP"; break;
				case 0x19: pszTagSB = "PMD_3DVFP"; break;
				case 0x1A: pszTagSB = "PMD_TAVFP"; break;
			}
			break;
		}

		case RGXDBG_TILING:
		{
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "PSG Control Stream TP0"; break;
				case 0x1: pszTagSB = "TPC TP0"; break;
				case 0x2: pszTagSB = "VCE0"; break;
				case 0x3: pszTagSB = "VCE1"; break;
				case 0x4: pszTagSB = "PSG Control Stream TP1"; break;
				case 0x5: pszTagSB = "TPC TP1"; break;
				case 0x8: pszTagSB = "PSG Region Header TP0"; break;
				case 0xC: pszTagSB = "PSG Region Header TP1"; break;
			}
			break;
		}

		case RGXDBG_VDMS:
		{
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "Context State - Write Only"; break;
			}
			break;
		}

		case RGXDBG_IPF:
		{
			switch (ui32TagSB)
			{
				case 0x00:
				case 0x20: pszTagSB = "CPF"; break;
				case 0x01: pszTagSB = "DBSC"; break;
				case 0x02:
				case 0x04:
				case 0x06:
				case 0x08:
				case 0x0A:
				case 0x0C:
				case 0x0E:
				case 0x10: pszTagSB = "Control Stream"; break;
				case 0x03:
				case 0x05:
				case 0x07:
				case 0x09:
				case 0x0B:
				case 0x0D:
				case 0x0F:
				case 0x11: pszTagSB = "Primitive Block"; break;
			}
			break;
		}

		case RGXDBG_ISP:
		{
			switch (ui32TagSB)
			{
				case 0x00: pszTagSB = "ZLS read/write"; break;
				case 0x20: pszTagSB = "Occlusion query read/write"; break;
			}
			break;
		}

		case RGXDBG_TPF:
		{
			switch (ui32TagSB)
			{
				case 0x0: pszTagSB = "TPF0: Primitive Block"; break;
				case 0x1: pszTagSB = "TPF0: Depth Bias"; break;
				case 0x2: pszTagSB = "TPF0: Per Primitive IDs"; break;
				case 0x3: pszTagSB = "CPF - Tables"; break;
				case 0x4: pszTagSB = "TPF1: Primitive Block"; break;
				case 0x5: pszTagSB = "TPF1: Depth Bias"; break;
				case 0x6: pszTagSB = "TPF1: Per Primitive IDs"; break;
				case 0x7: pszTagSB = "CPF - Data: Pipe 0"; break;
				case 0x8: pszTagSB = "TPF2: Primitive Block"; break;
				case 0x9: pszTagSB = "TPF2: Depth Bias"; break;
				case 0xA: pszTagSB = "TPF2: Per Primitive IDs"; break;
				case 0xB: pszTagSB = "CPF - Data: Pipe 1"; break;
				case 0xC: pszTagSB = "TPF3: Primitive Block"; break;
				case 0xD: pszTagSB = "TPF3: Depth Bias"; break;
				case 0xE: pszTagSB = "TPF3: Per Primitive IDs"; break;
				case 0xF: pszTagSB = "CPF - Data: Pipe 2"; break;
			}
			break;
		}

		case RGXDBG_FBCDC:
		{
			/*
			 * FBC faults on a 4-cluster phantom does not always set SB
			 * bit 5, but since FBC is write-only and FBDC is read-only,
			 * we can set bit 5 if this is a write fault, before decoding.
			 */
			if (bRead == IMG_FALSE)
			{
				ui32TagSB |= 0x20;
			}

			switch (ui32TagSB)
			{
				case 0x00: pszTagSB = "FBDC Request, originator ZLS"; break;
				case 0x02: pszTagSB = "FBDC Request, originator MCU Dust 0"; break;
				case 0x03: pszTagSB = "FBDC Request, originator MCU Dust 1"; break;
				case 0x20: pszTagSB = "FBC Request, originator ZLS"; break;
				case 0x22: pszTagSB = "FBC Request, originator PBE Dust 0, Cluster 0"; break;
				case 0x23: pszTagSB = "FBC Request, originator PBE Dust 0, Cluster 1"; break;
				case 0x24: pszTagSB = "FBC Request, originator PBE Dust 1, Cluster 0"; break;
				case 0x25: pszTagSB = "FBC Request, originator PBE Dust 1, Cluster 1"; break;
				case 0x28: pszTagSB = "FBC Request, originator ZLS Fence"; break;
				case 0x2a: pszTagSB = "FBC Request, originator PBE Dust 0, Cluster 0, Fence"; break;
				case 0x2b: pszTagSB = "FBC Request, originator PBE Dust 0, Cluster 1, Fence"; break;
				case 0x2c: pszTagSB = "FBC Request, originator PBE Dust 1, Cluster 0, Fence"; break;
				case 0x2d: pszTagSB = "FBC Request, originator PBE Dust 1, Cluster 1, Fence"; break;
			}
			break;
		}

		case RGXDBG_MCU:
		{
			IMG_UINT32 ui32SetNumber = (ui32TagSB >> 5) & 0x7;
			IMG_UINT32 ui32WayNumber = (ui32TagSB >> 2) & 0x7;
			IMG_UINT32 ui32Group     = ui32TagSB & 0x3;

			IMG_CHAR* pszGroup = "";

			switch (ui32Group)
			{
				case 0x0: pszGroup = "Banks 0-1"; break;
				case 0x1: pszGroup = "Banks 2-3"; break;
				case 0x2: pszGroup = "Banks 4-5"; break;
				case 0x3: pszGroup = "Banks 6-7"; break;
			}

			OSSNPrintf(pszScratchBuf, ui32ScratchBufSize,
			           "Set=%d, Way=%d, %s", ui32SetNumber, ui32WayNumber, pszGroup);
			pszTagSB = pszScratchBuf;
			break;
		}

		default:
		{
			OSSNPrintf(pszScratchBuf, ui32ScratchBufSize, "SB=0x%02x", ui32TagSB);
			pszTagSB = pszScratchBuf;
			break;
		}
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

 @Input uiPID              - The process ID to search for allocations belonging to
 @Input sFaultDevVAddr     - The device address to search for allocations at/before/after
 @Input asQueryOut         - Storage for the query results
 @Input ui32PageSizeBytes  - Faulted page size in bytes

 @Return IMG_BOOL          - IMG_TRUE if any results were found for this page fault

******************************************************************************/
static IMG_BOOL _GetDevicememHistoryData(IMG_PID uiPID, IMG_DEV_VIRTADDR sFaultDevVAddr,
							DEVICEMEM_HISTORY_QUERY_OUT asQueryOut[DEVICEMEM_HISTORY_QUERY_INDEX_COUNT],
							IMG_UINT32 ui32PageSizeBytes)
{
	IMG_UINT32 i;
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

	/* query the DevicememHistory about the preceding / faulting / next page */

	for (i = DEVICEMEM_HISTORY_QUERY_INDEX_PRECEDING; i < DEVICEMEM_HISTORY_QUERY_INDEX_COUNT; i++)
	{
		IMG_BOOL bHits;

		switch (i)
		{
			case DEVICEMEM_HISTORY_QUERY_INDEX_PRECEDING:
				sQueryIn.sDevVAddr.uiAddr = (sFaultDevVAddr.uiAddr & ~(IMG_UINT64)(ui32PageSizeBytes - 1)) - 1;
				break;
			case DEVICEMEM_HISTORY_QUERY_INDEX_FAULTED:
				sQueryIn.sDevVAddr = sFaultDevVAddr;
				break;
			case DEVICEMEM_HISTORY_QUERY_INDEX_NEXT:
				sQueryIn.sDevVAddr.uiAddr = (sFaultDevVAddr.uiAddr & ~(IMG_UINT64)(ui32PageSizeBytes - 1)) + ui32PageSizeBytes;
				break;
		}

		/* First try matching any record at the exact address... */
		bHits = DevicememHistoryQuery(&sQueryIn, &asQueryOut[i], ui32PageSizeBytes, IMG_FALSE);
		if (!bHits)
		{
			/* If not matched then try matching any record in the same page... */
			bHits = DevicememHistoryQuery(&sQueryIn, &asQueryOut[i], ui32PageSizeBytes, IMG_TRUE);
		}

		if (bHits)
		{
			bAnyHits = IMG_TRUE;
		}
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
							", PID: %u "
							"(%s, unregistered: %u) OS time: "
							"%" IMG_UINT64_FMTSPEC ".%09" IMG_UINT64_FMTSPEC,
					pszIndent,
					psInfo->sFaultDevVAddr.uiAddr,
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
			const IMG_CHAR *pszWhich;

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
				bHits = _GetDevicememHistoryData(sProcessInfo.uiPID,
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

 @Function	_RGXDumpRGXBIFBank

 @Description

 Dump BIF Bank state in human readable form.

 @Input pfnDumpDebugPrintf   - The debug printf function
 @Input pvDumpDebugFile      - Optional file identifier to be passed to the
                               'printf' function if required
 @Input psDevInfo            - RGX device info
 @Input eBankID              - BIF identifier
 @Input ui64MMUStatus        - MMU Status register value
 @Input ui64ReqStatus        - BIF request Status register value
 @Return   void

******************************************************************************/
static void _RGXDumpRGXBIFBank(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile,
					PVRSRV_RGXDEV_INFO *psDevInfo,
					RGXDBG_BIF_ID eBankID,
					IMG_UINT64 ui64MMUStatus,
					IMG_UINT64 ui64ReqStatus,
					const IMG_CHAR *pszIndent)
{
	if (ui64MMUStatus == 0x0)
	{
		PVR_DUMPDEBUG_LOG("%s - OK", pszBIFNames[eBankID]);
	}
	else
	{
		IMG_UINT32 ui32PageSize;
		IMG_UINT32 ui32PC =
			(ui64MMUStatus & ~RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_CAT_BASE_CLRMSK) >>
				RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_CAT_BASE_SHIFT;

		/* Bank 0 & 1 share the same fields */
		PVR_DUMPDEBUG_LOG("%s%s - FAULT:",
						  pszIndent,
						  pszBIFNames[eBankID]);

		/* MMU Status */
		{
			IMG_UINT32 ui32MMUDataType =
				(ui64MMUStatus & ~RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_DATA_TYPE_CLRMSK) >>
					RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_DATA_TYPE_SHIFT;

			IMG_BOOL bROFault = (ui64MMUStatus & RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_FAULT_RO_EN) != 0;
			IMG_BOOL bProtFault = (ui64MMUStatus & RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_FAULT_PM_META_RO_EN) != 0;

			ui32PageSize = (ui64MMUStatus & ~RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_PAGE_SIZE_CLRMSK) >>
						RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_PAGE_SIZE_SHIFT;

			PVR_DUMPDEBUG_LOG("%s  * MMU status (0x%016" IMG_UINT64_FMTSPECx "): PC = %d%s, Page Size = %d%s%s%s.",
						pszIndent,
						ui64MMUStatus,
						ui32PC,
						(ui32PC < 0x8)?"":_RGXDecodePMPC(ui32PC),
						ui32PageSize,
						(bROFault)?", Read Only fault":"",
						(bProtFault)?", PM/META protection fault":"",
						_RGXDecodeMMULevel(ui32MMUDataType));
		}

		/* Req Status */
		{
			IMG_CHAR *pszTagID;
			IMG_CHAR *pszTagSB;
			IMG_CHAR aszScratch[RGX_DEBUG_STR_SIZE];
			IMG_BOOL bRead;
			IMG_UINT32 ui32TagSB, ui32TagID;
			IMG_UINT64 ui64Addr;

			if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, XE_MEMORY_HIERARCHY))
			{
				bRead = (ui64ReqStatus & RGX_CR_BIF_FAULT_BANK0_REQ_STATUS__XE_MEM__RNW_EN) != 0;
				ui32TagSB = (ui64ReqStatus & ~RGX_CR_BIF_FAULT_BANK0_REQ_STATUS__XE_MEM__TAG_SB_CLRMSK) >>
					RGX_CR_BIF_FAULT_BANK0_REQ_STATUS__XE_MEM__TAG_SB_SHIFT;
				ui32TagID = (ui64ReqStatus & ~RGX_CR_BIF_FAULT_BANK0_REQ_STATUS__XE_MEM__TAG_ID_CLRMSK) >>
					RGX_CR_BIF_FAULT_BANK0_REQ_STATUS__XE_MEM__TAG_ID_SHIFT;
			}
			else
			{
				bRead = (ui64ReqStatus & RGX_CR_BIF_FAULT_BANK0_REQ_STATUS_RNW_EN) != 0;
				ui32TagSB = (ui64ReqStatus & ~RGX_CR_BIF_FAULT_BANK0_REQ_STATUS_TAG_SB_CLRMSK) >>
					RGX_CR_BIF_FAULT_BANK0_REQ_STATUS_TAG_SB_SHIFT;
				ui32TagID = (ui64ReqStatus & ~RGX_CR_BIF_FAULT_BANK0_REQ_STATUS_TAG_ID_CLRMSK) >>
					RGX_CR_BIF_FAULT_BANK0_REQ_STATUS_TAG_ID_SHIFT;
			}
			ui64Addr = ((ui64ReqStatus & ~RGX_CR_BIF_FAULT_BANK0_REQ_STATUS_ADDRESS_CLRMSK) >>
				RGX_CR_BIF_FAULT_BANK0_REQ_STATUS_ADDRESS_SHIFT) <<
				RGX_CR_BIF_FAULT_BANK0_REQ_STATUS_ADDRESS_ALIGNSHIFT;

			_RGXDecodeBIFReqTags(psDevInfo, eBankID, ui32TagID, ui32TagSB, &pszTagID, &pszTagSB, &aszScratch[0], RGX_DEBUG_STR_SIZE);

			PVR_DUMPDEBUG_LOG("%s  * Request (0x%016" IMG_UINT64_FMTSPECx
						"): %s (%s), %s " IMG_DEV_VIRTADDR_FMTSPEC ".",
							  pszIndent,
							  ui64ReqStatus,
			                  pszTagID,
			                  pszTagSB,
			                  (bRead)?"Reading from":"Writing to",
			                  ui64Addr);
		}
	}
}
static_assert((RGX_CR_BIF_FAULT_BANK0_REQ_STATUS__XE_MEM__RNW_EN == RGX_CR_FWCORE_MEM_FAULT_REQ_STATUS_RNW_EN),
			  "RGX_CR_FWCORE_MEM_FAULT_REQ_STATUS_RNW_EN mismatch!");
static_assert((RGX_CR_BIF_FAULT_BANK0_REQ_STATUS__XE_MEM__TAG_SB_CLRMSK == RGX_CR_FWCORE_MEM_FAULT_REQ_STATUS_TAG_SB_CLRMSK),
			  "RGX_CR_FWCORE_MEM_FAULT_REQ_STATUS_TAG_SB_CLRMSK mismatch!");
static_assert((RGX_CR_BIF_FAULT_BANK0_REQ_STATUS__XE_MEM__TAG_SB_SHIFT == RGX_CR_FWCORE_MEM_FAULT_REQ_STATUS_TAG_SB_SHIFT),
			  "RGX_CR_FWCORE_MEM_FAULT_REQ_STATUS_TAG_SB_SHIFT mismatch!");
static_assert((RGX_CR_BIF_FAULT_BANK0_REQ_STATUS__XE_MEM__TAG_ID_CLRMSK == RGX_CR_FWCORE_MEM_FAULT_REQ_STATUS_TAG_ID_CLRMSK),
			  "RGX_CR_FWCORE_MEM_FAULT_REQ_STATUS_TAG_ID_CLRMSK mismatch!");
static_assert((RGX_CR_BIF_FAULT_BANK0_REQ_STATUS__XE_MEM__TAG_ID_SHIFT == RGX_CR_FWCORE_MEM_FAULT_REQ_STATUS_TAG_ID_SHIFT),
			  "RGX_CR_FWCORE_MEM_FAULT_REQ_STATUS_TAG_ID_SHIFT mismatch!");
static_assert((RGX_CR_BIF_FAULT_BANK0_REQ_STATUS_ADDRESS_CLRMSK == RGX_CR_FWCORE_MEM_FAULT_REQ_STATUS_ADDRESS_CLRMSK),
			  "RGX_CR_FWCORE_MEM_FAULT_REQ_STATUS_ADDRESS_CLRMSK mismatch!");
static_assert((RGX_CR_BIF_FAULT_BANK0_REQ_STATUS_ADDRESS_SHIFT == RGX_CR_FWCORE_MEM_FAULT_REQ_STATUS_ADDRESS_SHIFT),
			  "RGX_CR_FWCORE_MEM_FAULT_REQ_STATUS_ADDRESS_SHIFT mismatch!");
static_assert((RGX_CR_BIF_FAULT_BANK0_REQ_STATUS_ADDRESS_ALIGNSHIFT == RGX_CR_FWCORE_MEM_FAULT_REQ_STATUS_ADDRESS_ALIGNSHIFT),
			  "RGX_CR_FWCORE_MEM_FAULT_REQ_STATUS_ADDRESS_ALIGNSHIFT mismatch!");

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
					IMG_UINT64 ui64MMUStatus,
					const IMG_PCHAR pszMetaOrCore,
					const IMG_CHAR *pszIndent)
{
	if (ui64MMUStatus == 0x0)
	{
		PVR_DUMPDEBUG_LOG("%sMMU (%s) - OK", pszIndent, pszMetaOrCore);
	}
	else
	{
		IMG_UINT32 ui32PC        = (ui64MMUStatus & ~RGX_CR_MMU_FAULT_STATUS_CONTEXT_CLRMSK) >>
		                           RGX_CR_MMU_FAULT_STATUS_CONTEXT_SHIFT;
		IMG_UINT64 ui64Addr      = ((ui64MMUStatus & ~RGX_CR_MMU_FAULT_STATUS_ADDRESS_CLRMSK) >>
		                           RGX_CR_MMU_FAULT_STATUS_ADDRESS_SHIFT) <<  4; /* align shift */
		IMG_UINT32 ui32Requester = (ui64MMUStatus & ~RGX_CR_MMU_FAULT_STATUS_REQ_ID_CLRMSK) >>
		                           RGX_CR_MMU_FAULT_STATUS_REQ_ID_SHIFT;
		IMG_UINT32 ui32SideBand  = (ui64MMUStatus & ~RGX_CR_MMU_FAULT_STATUS_TAG_SB_CLRMSK) >>
		                           RGX_CR_MMU_FAULT_STATUS_TAG_SB_SHIFT;
		IMG_UINT32 ui32MMULevel  = (ui64MMUStatus & ~RGX_CR_MMU_FAULT_STATUS_LEVEL_CLRMSK) >>
		                           RGX_CR_MMU_FAULT_STATUS_LEVEL_SHIFT;
		IMG_BOOL bRead           = (ui64MMUStatus & RGX_CR_MMU_FAULT_STATUS_RNW_EN) != 0;
		IMG_BOOL bFault          = (ui64MMUStatus & RGX_CR_MMU_FAULT_STATUS_FAULT_EN) != 0;
		IMG_BOOL bROFault        = ((ui64MMUStatus & ~RGX_CR_MMU_FAULT_STATUS_TYPE_CLRMSK) >>
		                            RGX_CR_MMU_FAULT_STATUS_TYPE_SHIFT) == 0x2;
		IMG_BOOL bProtFault      = ((ui64MMUStatus & ~RGX_CR_MMU_FAULT_STATUS_TYPE_CLRMSK) >>
		                            RGX_CR_MMU_FAULT_STATUS_TYPE_SHIFT) == 0x3;
		IMG_CHAR aszScratch[RGX_DEBUG_STR_SIZE];
		IMG_CHAR *pszTagID;
		IMG_CHAR *pszTagSB;

		_RGXDecodeMMUReqTags(psDevInfo, ui32Requester, ui32SideBand, bRead, &pszTagID, &pszTagSB, aszScratch, RGX_DEBUG_STR_SIZE);

		PVR_DUMPDEBUG_LOG("%sMMU (%s) - FAULT:", pszIndent, pszMetaOrCore);
		PVR_DUMPDEBUG_LOG("%s  * MMU status (0x%016" IMG_UINT64_FMTSPECx "): PC = %d, %s 0x%010" IMG_UINT64_FMTSPECx ", %s (%s)%s%s%s%s.",
						  pszIndent,
						  ui64MMUStatus,
						  ui32PC,
						  (bRead)?"Reading from":"Writing to",
						  ui64Addr,
						  pszTagID,
						  pszTagSB,
						  (bFault)?", Fault":"",
						  (bROFault)?", Read Only fault":"",
						  (bProtFault)?", PM/META protection fault":"",
						  _RGXDecodeMMULevel(ui32MMULevel));

	}
}
static_assert((RGX_CR_MMU_FAULT_STATUS_CONTEXT_CLRMSK == RGX_CR_MMU_FAULT_STATUS_META_CONTEXT_CLRMSK),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS_CONTEXT_SHIFT == RGX_CR_MMU_FAULT_STATUS_META_CONTEXT_SHIFT),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS_ADDRESS_CLRMSK == RGX_CR_MMU_FAULT_STATUS_META_ADDRESS_CLRMSK),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS_ADDRESS_SHIFT == RGX_CR_MMU_FAULT_STATUS_META_ADDRESS_SHIFT),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS_TAG_SB_CLRMSK == RGX_CR_MMU_FAULT_STATUS_META_TAG_SB_CLRMSK),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS_TAG_SB_SHIFT == RGX_CR_MMU_FAULT_STATUS_META_TAG_SB_SHIFT),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS_REQ_ID_CLRMSK == RGX_CR_MMU_FAULT_STATUS_META_REQ_ID_CLRMSK),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS_REQ_ID_SHIFT == RGX_CR_MMU_FAULT_STATUS_META_REQ_ID_SHIFT),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS_LEVEL_CLRMSK == RGX_CR_MMU_FAULT_STATUS_META_LEVEL_CLRMSK),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS_LEVEL_SHIFT == RGX_CR_MMU_FAULT_STATUS_META_LEVEL_SHIFT),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS_RNW_EN == RGX_CR_MMU_FAULT_STATUS_META_RNW_EN),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS_FAULT_EN == RGX_CR_MMU_FAULT_STATUS_META_FAULT_EN),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS_TYPE_CLRMSK == RGX_CR_MMU_FAULT_STATUS_META_TYPE_CLRMSK),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS_TYPE_SHIFT == RGX_CR_MMU_FAULT_STATUS_META_TYPE_SHIFT),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS_TYPE_CLRMSK == RGX_CR_MMU_FAULT_STATUS_META_TYPE_CLRMSK),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");
static_assert((RGX_CR_MMU_FAULT_STATUS_TYPE_SHIFT == RGX_CR_MMU_FAULT_STATUS_META_TYPE_SHIFT),
			  "RGX_CR_MMU_FAULT_STATUS_META mismatch!");



#if !defined(NO_HARDWARE)
static PVRSRV_ERROR _RGXMipsExtraDebug(PVRSRV_RGXDEV_INFO *psDevInfo, RGX_MIPS_STATE *psMIPSState)
{
	void __iomem *pvRegsBaseKM = psDevInfo->pvRegsBaseKM;
	IMG_UINT32 ui32RegRead;
	IMG_UINT32 eError = PVRSRV_OK;
	IMG_UINT32 *pui32NMIMemoryPointer;
	IMG_UINT32 volatile *pui32SyncFlag;
	IMG_DEVMEM_OFFSET_T uiNMIMemoryBootOffset;

	/* Map the FW data area to the kernel */
	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWDataMemDesc,
									 (void **)&pui32NMIMemoryPointer);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Failed to acquire NMI shared memory area (%s)",
				__func__,
				PVRSRVGetErrorString(eError)));
		goto map_error_fail;
	}

	/* Calculate offset to the boot/NMI data page */
	uiNMIMemoryBootOffset = RGXMIPSFW_GET_OFFSET_IN_DWORDS(RGXGetFWImageSectionOffset(NULL, MIPS_BOOT_DATA));

	/* Jump to the NMI shared data area within the page above */
	pui32NMIMemoryPointer += uiNMIMemoryBootOffset + RGXMIPSFW_GET_OFFSET_IN_DWORDS(RGXMIPSFW_NMI_SHARED_DATA_BASE);

	/* Acquire the NMI operations lock */
	OSLockAcquire(psDevInfo->hNMILock);

	/* Make sure the synchronisation flag is set to 0 */
	pui32SyncFlag = &pui32NMIMemoryPointer[RGXMIPSFW_NMI_SYNC_FLAG_OFFSET];
	*pui32SyncFlag = 0;
	OSWriteMemoryBarrier();
	(void) *pui32SyncFlag;

	/* Enable NMI issuing in the MIPS wrapper */
	OSWriteHWReg64(pvRegsBaseKM,
				   RGX_CR_MIPS_WRAPPER_NMI_ENABLE,
				   RGX_CR_MIPS_WRAPPER_NMI_ENABLE_EVENT_EN);
	(void) OSReadHWReg64(pvRegsBaseKM, RGX_CR_MIPS_WRAPPER_NMI_ENABLE);

	/* Check the MIPS is not in error state already (e.g. it is booting or an NMI has already been requested) */
	ui32RegRead = OSReadHWReg32(pvRegsBaseKM,
				   RGX_CR_MIPS_EXCEPTION_STATUS);
	if ((ui32RegRead & RGX_CR_MIPS_EXCEPTION_STATUS_SI_ERL_EN) || (ui32RegRead & RGX_CR_MIPS_EXCEPTION_STATUS_SI_NMI_TAKEN_EN))
	{

		eError = PVRSRV_ERROR_MIPS_STATUS_UNAVAILABLE;
		goto fail;
	}
	ui32RegRead = 0;

	/* Issue NMI */
	OSWriteHWReg32(pvRegsBaseKM,
				   RGX_CR_MIPS_WRAPPER_NMI_EVENT,
				   RGX_CR_MIPS_WRAPPER_NMI_EVENT_TRIGGER_EN);
	(void) OSReadHWReg64(pvRegsBaseKM, RGX_CR_MIPS_WRAPPER_NMI_EVENT);


	/* Wait for NMI Taken to be asserted */
	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		ui32RegRead = OSReadHWReg32(pvRegsBaseKM,
									RGX_CR_MIPS_EXCEPTION_STATUS);
		if (ui32RegRead & RGX_CR_MIPS_EXCEPTION_STATUS_SI_NMI_TAKEN_EN)
		{
			break;
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} END_LOOP_UNTIL_TIMEOUT();

	if ((ui32RegRead & RGX_CR_MIPS_EXCEPTION_STATUS_SI_NMI_TAKEN_EN) == 0)
	{
			eError = PVRSRV_ERROR_MIPS_STATUS_UNAVAILABLE;
			goto fail;
	}
	ui32RegRead = 0;

	/* Allow the firmware to proceed */
	*pui32SyncFlag = 1;
	OSWriteMemoryBarrier();
	(void) *pui32SyncFlag;

	/* Wait for the FW to have finished the NMI routine */
	ui32RegRead = OSReadHWReg32(pvRegsBaseKM,
								RGX_CR_MIPS_EXCEPTION_STATUS);

	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		ui32RegRead = OSReadHWReg32(pvRegsBaseKM,
									RGX_CR_MIPS_EXCEPTION_STATUS);
		if (!(ui32RegRead & RGX_CR_MIPS_EXCEPTION_STATUS_SI_ERL_EN))
		{
			break;
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} END_LOOP_UNTIL_TIMEOUT();
	if (ui32RegRead & RGX_CR_MIPS_EXCEPTION_STATUS_SI_ERL_EN)
	{
			eError = PVRSRV_ERROR_MIPS_STATUS_UNAVAILABLE;
			goto fail;
	}
	ui32RegRead = 0;

	/* Copy state */
	OSDeviceMemCopy(psMIPSState, pui32NMIMemoryPointer + RGXMIPSFW_NMI_STATE_OFFSET, sizeof(*psMIPSState));

	--(psMIPSState->ui32ErrorEPC);
	--(psMIPSState->ui32EPC);

	/* Disable NMI issuing in the MIPS wrapper */
	OSWriteHWReg32(pvRegsBaseKM,
				   RGX_CR_MIPS_WRAPPER_NMI_ENABLE,
				   0);
	(void) OSReadHWReg64(pvRegsBaseKM, RGX_CR_MIPS_WRAPPER_NMI_ENABLE);

fail:
	/* Release the NMI operations lock */
	OSLockRelease(psDevInfo->hNMILock);
	DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWDataMemDesc);
map_error_fail:
	return eError;
}

/* Print decoded information from cause register */
static void _RGXMipsDumpCauseDecode(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                    void *pvDumpDebugFile,
                                    IMG_UINT32 ui32Cause,
                                    IMG_UINT32 ui32ErrorState)
{
#define INDENT "    "
	const IMG_UINT32 ui32ExcCode = RGXMIPSFW_C0_CAUSE_EXCCODE(ui32Cause);
	const IMG_CHAR * const pszException = _GetMIPSExcString(ui32ExcCode);

	if (ui32ErrorState == RGXMIPSFW_NMI_ERROR_STATE_SET &&
	    pszException != NULL)
	{
		PVR_DUMPDEBUG_LOG(INDENT "Cause exception: %s", pszException);
	}

	if (ui32Cause & RGXMIPSFW_C0_CAUSE_FDCIPENDING)
	{
		PVR_DUMPDEBUG_LOG(INDENT "FDC interrupt pending");
	}

	if (!(ui32Cause & RGXMIPSFW_C0_CAUSE_IV))
	{
		PVR_DUMPDEBUG_LOG(INDENT "Interrupt uses general interrupt vector");
	}

	if (ui32Cause & RGXMIPSFW_C0_CAUSE_PCIPENDING)
	{
		PVR_DUMPDEBUG_LOG(INDENT "Performance Counter Interrupt pending");
	}

	/* Unusable Coproc exception */
	if (ui32ExcCode == 11)
	{
		PVR_DUMPDEBUG_LOG(INDENT "Unusable Coprocessor: %d", RGXMIPSFW_C0_CAUSE_UNUSABLE_UNIT(ui32Cause));
	}

#undef INDENT
}

static IMG_BOOL _IsFWCodeException(IMG_UINT32 ui32ExcCode)
{
	if (ui32ExcCode >= sizeof(apsMIPSExcCodes)/sizeof(MIPS_EXCEPTION_ENCODING))
	{
		PVR_DPF((PVR_DBG_WARNING,
		         "Only %lu exceptions available in MIPS, %u is not a valid exception code",
		         (unsigned long)sizeof(apsMIPSExcCodes)/sizeof(MIPS_EXCEPTION_ENCODING), ui32ExcCode));
		return IMG_FALSE;
	}

	return apsMIPSExcCodes[ui32ExcCode].bIsFatal;
}

static void _RGXMipsDumpDebugDecode(PVRSRV_RGXDEV_INFO *psDevInfo,
					DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile,
					IMG_UINT32 ui32Debug,
					IMG_UINT32 ui32DEPC)
{
	const IMG_CHAR *pszDException = NULL;
	IMG_UINT32 i;
#define INDENT "    "

	if (!(ui32Debug & RGXMIPSFW_C0_DEBUG_DM))
	{
		return;
	}

	PVR_DUMPDEBUG_LOG("DEBUG                        :");

	pszDException = _GetMIPSExcString(RGXMIPSFW_C0_DEBUG_EXCCODE(ui32Debug));

	if (pszDException != NULL)
	{
		PVR_DUMPDEBUG_LOG(INDENT "Debug exception: %s", pszDException);
	}

	for (i = 0; i < ARRAY_SIZE(sMIPS_C0_DebugTable); ++i)
	{
		const RGXMIPSFW_C0_DEBUG_TBL_ENTRY * const psDebugEntry = &sMIPS_C0_DebugTable[i];

		if (ui32Debug & psDebugEntry->ui32Mask)
		{
			PVR_DUMPDEBUG_LOG(INDENT "%s", psDebugEntry->pszExplanation);
		}
	}
#undef INDENT
	PVR_DUMPDEBUG_LOG("DEPC                    :0x%08X", ui32DEPC);
}

static inline void _GetMipsTLBPARanges(const RGX_MIPS_TLB_ENTRY *psTLBEntry,
                                       const RGX_MIPS_REMAP_ENTRY *psRemapEntry0,
                                       const RGX_MIPS_REMAP_ENTRY *psRemapEntry1,
                                       IMG_UINT64 *pui64PA0Start,
                                       IMG_UINT64 *pui64PA0End,
                                       IMG_UINT64 *pui64PA1Start,
                                       IMG_UINT64 *pui64PA1End)
{
	IMG_BOOL bUseRemapOutput = (psRemapEntry0 != NULL && psRemapEntry1 != NULL) ? IMG_TRUE : IMG_FALSE;
	IMG_UINT64 ui64PageSize = RGXMIPSFW_TLB_GET_PAGE_SIZE(psTLBEntry->ui32TLBPageMask);

	if ((psTLBEntry->ui32TLBLo0 & RGXMIPSFW_TLB_VALID) == 0)
	{
		/* Dummy values to fail the range checks later */
		*pui64PA0Start = -1ULL;
		*pui64PA0End   = -1ULL;
	}
	else if (bUseRemapOutput)
	{
		*pui64PA0Start = (IMG_UINT64)psRemapEntry0->ui32RemapAddrOut << 12;
		*pui64PA0End   = *pui64PA0Start + ui64PageSize - 1;
	}
	else
	{
		*pui64PA0Start = RGXMIPSFW_TLB_GET_PA(psTLBEntry->ui32TLBLo0);
		*pui64PA0End   = *pui64PA0Start + ui64PageSize - 1;
	}

	if ((psTLBEntry->ui32TLBLo1 & RGXMIPSFW_TLB_VALID) == 0)
	{
		/* Dummy values to fail the range checks later */
		*pui64PA1Start = -1ULL;
		*pui64PA1End   = -1ULL;
	}
	else if (bUseRemapOutput)
	{
		*pui64PA1Start = (IMG_UINT64)psRemapEntry1->ui32RemapAddrOut << 12;
		*pui64PA1End   = *pui64PA1Start + ui64PageSize - 1;
	}
	else
	{
		*pui64PA1Start = RGXMIPSFW_TLB_GET_PA(psTLBEntry->ui32TLBLo1);
		*pui64PA1End   = *pui64PA1Start + ui64PageSize - 1;
	}
}

static void _CheckMipsTLBDuplicatePAs(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                      void *pvDumpDebugFile,
                                      const RGX_MIPS_TLB_ENTRY *psTLB,
                                      const RGX_MIPS_REMAP_ENTRY *psRemap)
{
	IMG_UINT64 ui64PA0StartI, ui64PA1StartI, ui64PA0StartJ, ui64PA1StartJ;
	IMG_UINT64 ui64PA0EndI,   ui64PA1EndI,   ui64PA0EndJ,   ui64PA1EndJ;
	IMG_UINT32 i, j;

#define RANGES_OVERLAP(start0,end0,start1,end1)  ((start0) < (end1) && (start1) < (end0))

	for (i = 0; i < RGXMIPSFW_NUMBER_OF_TLB_ENTRIES; i++)
	{
		_GetMipsTLBPARanges(&psTLB[i],
		                    psRemap ? &psRemap[i] : NULL,
		                    psRemap ? &psRemap[i + RGXMIPSFW_NUMBER_OF_TLB_ENTRIES] : NULL,
		                    &ui64PA0StartI, &ui64PA0EndI,
		                    &ui64PA1StartI, &ui64PA1EndI);

		for (j = i + 1; j < RGXMIPSFW_NUMBER_OF_TLB_ENTRIES; j++)
		{
			_GetMipsTLBPARanges(&psTLB[j],
			                    psRemap ? &psRemap[j] : NULL,
			                    psRemap ? &psRemap[j + RGXMIPSFW_NUMBER_OF_TLB_ENTRIES] : NULL,
			                    &ui64PA0StartJ, &ui64PA0EndJ,
			                    &ui64PA1StartJ, &ui64PA1EndJ);

			if (RANGES_OVERLAP(ui64PA0StartI, ui64PA0EndI, ui64PA0StartJ, ui64PA0EndJ) ||
			    RANGES_OVERLAP(ui64PA0StartI, ui64PA0EndI, ui64PA1StartJ, ui64PA1EndJ) ||
			    RANGES_OVERLAP(ui64PA1StartI, ui64PA1EndI, ui64PA0StartJ, ui64PA0EndJ) ||
			    RANGES_OVERLAP(ui64PA1StartI, ui64PA1EndI, ui64PA1StartJ, ui64PA1EndJ)  )
			{
				PVR_DUMPDEBUG_LOG("Overlap between TLB entry %u and %u", i , j);
			}
		}
	}
}

static inline IMG_UINT32 _GetMIPSRemapRegionSize(IMG_UINT32 ui32RegionSizeEncoding)
{
    return 1U << ((ui32RegionSizeEncoding + 1U) << 1U);
}

static inline void _RGXMipsDumpTLBEntry(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                        void *pvDumpDebugFile,
                                        const RGX_MIPS_TLB_ENTRY *psTLBEntry,
                                        const RGX_MIPS_REMAP_ENTRY *psRemapEntry0,
                                        const RGX_MIPS_REMAP_ENTRY *psRemapEntry1,
                                        IMG_UINT32 ui32Index)
{
	IMG_BOOL bDumpRemapEntries = (psRemapEntry0 != NULL && psRemapEntry1 != NULL) ? IMG_TRUE : IMG_FALSE;
	IMG_UINT64 ui64PA0 = RGXMIPSFW_TLB_GET_PA(psTLBEntry->ui32TLBLo0);
	IMG_UINT64 ui64PA1 = RGXMIPSFW_TLB_GET_PA(psTLBEntry->ui32TLBLo1);
	IMG_UINT64 ui64Remap0AddrOut = 0, ui64Remap1AddrOut = 0;
	IMG_UINT32 ui32Remap0AddrIn = 0, ui32Remap1AddrIn = 0;

	static const IMG_CHAR * const apszPermissionInhibit[4] =
	{
		"",
		"XI",
		"RI",
		"RIXI"
	};

	static const IMG_CHAR * const apszCoherencyTLB[8] =
	{
		"C",
		"C",
		" ",
		"C",
		"C",
		"C",
		"C",
		" "
	};

	static const IMG_CHAR * const apszDirtyGlobalValid[8] =
	{
		"   ",
		"  G",
		" V ",
		" VG",
		"D  ",
		"D G",
		"DV ",
		"DVG"
	};

	if (bDumpRemapEntries)
	{
		/* RemapAddrIn is always 4k aligned and on 32 bit */
		ui32Remap0AddrIn = psRemapEntry0->ui32RemapAddrIn << 12;
		ui32Remap1AddrIn = psRemapEntry1->ui32RemapAddrIn << 12;

		/* RemapAddrOut is always 4k aligned and on 32 or 36 bit */
		ui64Remap0AddrOut = (IMG_UINT64)psRemapEntry0->ui32RemapAddrOut << 12;
		ui64Remap1AddrOut = (IMG_UINT64)psRemapEntry1->ui32RemapAddrOut << 12;

		/* If TLB and remap entries match, then merge them else, print them separately */
		if ((IMG_UINT32)ui64PA0 == ui32Remap0AddrIn &&
		    (IMG_UINT32)ui64PA1 == ui32Remap1AddrIn)
		{
			ui64PA0 = ui64Remap0AddrOut;
			ui64PA1 = ui64Remap1AddrOut;
			bDumpRemapEntries = IMG_FALSE;
		}
	}

	PVR_DUMPDEBUG_LOG("%2u) VA 0x%08X (%3uk) -> PA0 0x%08" IMG_UINT64_FMTSPECx " %s%s%s, "
	                                           "PA1 0x%08" IMG_UINT64_FMTSPECx " %s%s%s",
	                  ui32Index,
	                  psTLBEntry->ui32TLBHi,
	                  RGXMIPSFW_TLB_GET_PAGE_SIZE(psTLBEntry->ui32TLBPageMask),
	                  ui64PA0,
	                  apszPermissionInhibit[RGXMIPSFW_TLB_GET_INHIBIT(psTLBEntry->ui32TLBLo0)],
	                  apszDirtyGlobalValid[RGXMIPSFW_TLB_GET_DGV(psTLBEntry->ui32TLBLo0)],
	                  apszCoherencyTLB[RGXMIPSFW_TLB_GET_COHERENCY(psTLBEntry->ui32TLBLo0)],
	                  ui64PA1,
	                  apszPermissionInhibit[RGXMIPSFW_TLB_GET_INHIBIT(psTLBEntry->ui32TLBLo1)],
	                  apszDirtyGlobalValid[RGXMIPSFW_TLB_GET_DGV(psTLBEntry->ui32TLBLo1)],
	                  apszCoherencyTLB[RGXMIPSFW_TLB_GET_COHERENCY(psTLBEntry->ui32TLBLo1)]);

	if (bDumpRemapEntries)
	{
		PVR_DUMPDEBUG_LOG("    Remap %2u : IN 0x%08X (%3uk) => OUT 0x%08" IMG_UINT64_FMTSPECx,
		                  ui32Index,
		                  ui32Remap0AddrIn,
		                  _GetMIPSRemapRegionSize(psRemapEntry0->ui32RemapRegionSize),
		                  ui64Remap0AddrOut);

		PVR_DUMPDEBUG_LOG("    Remap %2u : IN 0x%08X (%3uk) => OUT 0x%08" IMG_UINT64_FMTSPECx,
		                  ui32Index + RGXMIPSFW_NUMBER_OF_TLB_ENTRIES,
		                  ui32Remap1AddrIn,
		                  _GetMIPSRemapRegionSize(psRemapEntry1->ui32RemapRegionSize),
		                  ui64Remap1AddrOut);
	}
}

#endif /* !defined(NO_HARDWARE) */

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

/*
	Appends flags strings to a null-terminated string buffer - each flag
	description string starts with a space.
*/
static void _Flags2Description(IMG_CHAR *psDesc, IMG_UINT32 ui32DescSize, const IMG_FLAGS2DESC *psConvTable,
				IMG_UINT32 ui32TableSize, IMG_UINT32 ui32Flags)
{
	IMG_UINT32 ui32Idx;

	for (ui32Idx = 0; ui32Idx < ui32TableSize; ui32Idx++)
	{
		if ((ui32Flags & psConvTable[ui32Idx].uiFlag) == psConvTable[ui32Idx].uiFlag)
		{
			OSStringLCat(psDesc, psConvTable[ui32Idx].pszLabel, ui32DescSize);
		}
	}
}

/*
	Writes flags strings to an uninitialised buffer.
*/
static void _GetFwSysFlagsDescription(IMG_CHAR *psDesc, IMG_UINT32 ui32DescSize, IMG_UINT32 ui32RawFlags)
{
	const IMG_CHAR *psCswLabel = "Ctx switch options:";
	size_t uLabelLen = OSStringLength(psCswLabel);
	const size_t uiBytesPerDesc = (ui32DescSize - uLabelLen) / 2U - 1U;

	OSStringLCopy(psDesc, psCswLabel, ui32DescSize);

	_Flags2Description(psDesc, uiBytesPerDesc + uLabelLen, asCswOpts2Description, ARRAY_SIZE(asCswOpts2Description), ui32RawFlags);
	_Flags2Description(psDesc, ui32DescSize, asMisc2Description, ARRAY_SIZE(asMisc2Description), ui32RawFlags);
}

static void _GetFwOsFlagsDescription(IMG_CHAR *psDesc, IMG_UINT32 ui32DescSize, IMG_UINT32 ui32RawFlags)
{
	const IMG_CHAR *psCswLabel = "Ctx switch:";
	size_t uLabelLen = OSStringLength(psCswLabel);
	const size_t uiBytesPerDesc = (ui32DescSize - uLabelLen) / 2U - 1U;

	OSStringLCopy(psDesc, psCswLabel, ui32DescSize);

	_Flags2Description(psDesc, uiBytesPerDesc + uLabelLen, asFwOsCfg2Description, ARRAY_SIZE(asFwOsCfg2Description), ui32RawFlags);
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
					RGXFWIF_TRACEBUF *psRGXFWIfTraceBufCtl)
{
	IMG_CHAR    *pszTraceAssertPath;
	IMG_CHAR    *pszTraceAssertInfo;
	IMG_INT32   ui32TraceAssertLine;
	IMG_UINT32  i;

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
                             RGXFWIF_SYSDATA *psFwSysData)
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
			RGX_FWFAULTINFO *psFaultInfo = &psFwSysData->sFaultInfo[ui32Index % RGXFWIF_FWFAULTINFO_MAX];
			IMG_UINT64      ui64Seconds, ui64Nanoseconds;

			/* Split OS timestamp in seconds and nanoseconds */
			ConvertOSTimestampToSAndNS(psFaultInfo->ui64OSTimer, &ui64Seconds, &ui64Nanoseconds);

			PVR_DUMPDEBUG_LOG("FW Fault %d: %s (%s:%d)",
			                  ui32Index+1, psFaultInfo->sFaultBuf.szInfo,
			                  psFaultInfo->sFaultBuf.szPath,
			                  psFaultInfo->sFaultBuf.ui32LineNum);
			PVR_DUMPDEBUG_LOG("            Data = 0x%08x, CRTimer = 0x%012"IMG_UINT64_FMTSPECx", OSTimer = %" IMG_UINT64_FMTSPEC ".%09" IMG_UINT64_FMTSPEC,
			                  psFaultInfo->ui32Data,
			                  psFaultInfo->ui64CRTimer,
			                  ui64Seconds, ui64Nanoseconds);
		}
	}
}

static void _RGXDumpFWPoll(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile,
					RGXFWIF_SYSDATA *psFwSysData)
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

static void _RGXDumpFWHWRInfo(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf, void *pvDumpDebugFile,
					RGXFWIF_SYSDATA *psFwSysData, RGXFWIF_HWRINFOBUF *psHWRInfoBuf, PVRSRV_RGXDEV_INFO *psDevInfo)
{
	IMG_BOOL   bAnyLocked = IMG_FALSE;
	IMG_UINT32 dm, i;
	IMG_UINT32 ui32LineSize;
	IMG_CHAR   *pszLine, *pszTemp;
	const IMG_CHAR    *apszDmNames[RGXFWIF_DM_MAX] = {"GP", "TDM", "TA", "3D", "CDM", "RAY"};
	const IMG_CHAR    szMsgHeader[] = "Number of HWR: ";
	const IMG_CHAR    szMsgFalse[] = "FALSE(";
	IMG_CHAR          *pszLockupType = "";
	RGX_HWRINFO       *psHWRInfo;
	const IMG_UINT32  ui32MsgHeaderCharCount = ARRAY_SIZE(szMsgHeader) - 1; /* size includes the null */
	const IMG_UINT32  ui32MsgFalseCharCount = ARRAY_SIZE(szMsgFalse) - 1;
	IMG_UINT32        ui32HWRRecoveryFlags;
	IMG_UINT32        ui32ReadIndex;

	if (!(RGX_IS_FEATURE_SUPPORTED(psDevInfo, FASTRENDER_DM)))
	{
		apszDmNames[RGXFWIF_DM_TDM] = "2D";
	}

	for (dm = 0; dm < RGXFWIF_DM_MAX; dm++)
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

		for (dm = 0; dm < RGXFWIF_DM_MAX; dm++)
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
			(RGXFWIF_DM_MAX * (	4/*DM name + left parenthesis*/ +
				10/*UINT32 max num of digits*/ +
				1/*slash*/ +
				10/*UINT32 max num of digits*/ +
				3/*right parenthesis + comma + space*/)) +
			ui32MsgFalseCharCount + 1 + (RGXFWIF_DM_MAX*6) + 1
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

	for (dm = 0; dm < RGXFWIF_DM_MAX; dm++)
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

	for (dm = 0; dm < RGXFWIF_DM_MAX; dm++)
	{
		pszTemp += OSSNPrintf(pszTemp,
				10 + 1 + 1 /* UINT32 max num + comma + \0 */,
				(dm < RGXFWIF_DM_MAX-1 ? "%u," : "%u)"),
				psHWRInfoBuf->aui32HwrDmFalseDetectCount[dm]);
	}

	PVR_DUMPDEBUG_LOG("%s", pszLine);

	OSFreeMem(pszLine);

	/* Print out per HWR info */
	for (dm = 0; dm < RGXFWIF_DM_MAX; dm++)
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
					_Flags2Description(sPerDmHwrDescription, RGX_DEBUG_STR_SIZE,
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

			psHWRInfo = &psHWRInfoBuf->sHWRInfo[ui32ReadIndex];

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
					PVR_DUMPDEBUG_LOG("  %s Core = %u, PID = %u, frame = %d, HWRTData = 0x%08X, EventStatus = 0x%08X%s",
				                   aui8RecoveryNum,
				                   psHWRInfo->ui32CoreID,
				                   psHWRInfo->ui32PID,
				                   psHWRInfo->ui32FrameNum,
				                   psHWRInfo->ui32ActiveHWRTData,
				                   psHWRInfo->ui32EventStatus,
				                   pszLockupType);
				}
				else
				{
					PVR_DUMPDEBUG_LOG("  %s PID = %u, frame = %d, HWRTData = 0x%08X, EventStatus = 0x%08X%s",
				                   aui8RecoveryNum,
				                   psHWRInfo->ui32PID,
				                   psHWRInfo->ui32FrameNum,
				                   psHWRInfo->ui32ActiveHWRTData,
				                   psHWRInfo->ui32EventStatus,
				                   pszLockupType);
				}
				pszTemp = &aui8RecoveryNum[0];
				while (*pszTemp != '\0')
				{
					*pszTemp++ = ' ';
				}

				/* There's currently no time correlation for the Guest OSes on the Firmware so there's no point printing OS Timestamps on Guests */
				if (!PVRSRV_VZ_MODE_IS(GUEST))
				{
					PVR_DUMPDEBUG_LOG("  %s CRTimer = 0x%012"IMG_UINT64_FMTSPECx", OSTimer = %" IMG_UINT64_FMTSPEC ".%09" IMG_UINT64_FMTSPEC ", CyclesElapsed = %" IMG_INT64_FMTSPECd,
									   aui8RecoveryNum,
									   psHWRInfo->ui64CRTimer,
									   ui64Seconds,
									   ui64Nanoseconds,
									   (psHWRInfo->ui64CRTimer-psHWRInfo->ui64CRTimeOfKick)*256);
				}
				else
				{
					PVR_DUMPDEBUG_LOG("  %s CRTimer = 0x%012"IMG_UINT64_FMTSPECx", CyclesElapsed = %" IMG_INT64_FMTSPECd,
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

				switch (psHWRInfo->eHWRType)
				{
					case RGX_HWRTYPE_BIF0FAULT:
					case RGX_HWRTYPE_BIF1FAULT:
					{
						if (!(RGX_IS_FEATURE_SUPPORTED(psDevInfo, S7_TOP_INFRASTRUCTURE)))
						{
							_RGXDumpRGXBIFBank(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo, RGXFWIF_HWRTYPE_BIF_BANK_GET(psHWRInfo->eHWRType),
											psHWRInfo->uHWRData.sBIFInfo.ui64BIFMMUStatus,
											psHWRInfo->uHWRData.sBIFInfo.ui64BIFReqStatus,
											DD_NORMAL_INDENT);

							bPageFault = IMG_TRUE;
							sFaultDevVAddr.uiAddr = (psHWRInfo->uHWRData.sBIFInfo.ui64BIFReqStatus & ~RGX_CR_BIF_FAULT_BANK0_REQ_STATUS_ADDRESS_CLRMSK);
							ui32PC = (psHWRInfo->uHWRData.sBIFInfo.ui64BIFMMUStatus & ~RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_CAT_BASE_CLRMSK) >>
									RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_CAT_BASE_SHIFT;
							bPMFault = (ui32PC >= 8);
							ui32PageSize = (psHWRInfo->uHWRData.sBIFInfo.ui64BIFMMUStatus & ~RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_PAGE_SIZE_CLRMSK) >>
										RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_PAGE_SIZE_SHIFT;
							sPCDevPAddr.uiAddr = psHWRInfo->uHWRData.sBIFInfo.ui64PCAddress;
						}
					}
					break;
					case RGX_HWRTYPE_TEXASBIF0FAULT:
					{
						if (!(RGX_IS_FEATURE_SUPPORTED(psDevInfo, S7_TOP_INFRASTRUCTURE)))
						{
							if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, CLUSTER_GROUPING))
							{
								_RGXDumpRGXBIFBank(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo, RGXDBG_TEXAS_BIF,
											psHWRInfo->uHWRData.sBIFInfo.ui64BIFMMUStatus,
											psHWRInfo->uHWRData.sBIFInfo.ui64BIFReqStatus,
											DD_NORMAL_INDENT);

								bPageFault = IMG_TRUE;
								sFaultDevVAddr.uiAddr = (psHWRInfo->uHWRData.sBIFInfo.ui64BIFReqStatus & ~RGX_CR_BIF_FAULT_BANK0_REQ_STATUS_ADDRESS_CLRMSK);
								ui32PC = (psHWRInfo->uHWRData.sBIFInfo.ui64BIFMMUStatus & ~RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_CAT_BASE_CLRMSK) >>
										RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_CAT_BASE_SHIFT;
								bPMFault = (ui32PC >= 8);
								ui32PageSize = (psHWRInfo->uHWRData.sBIFInfo.ui64BIFMMUStatus & ~RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_PAGE_SIZE_CLRMSK) >>
											RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_PAGE_SIZE_SHIFT;
								sPCDevPAddr.uiAddr = psHWRInfo->uHWRData.sBIFInfo.ui64PCAddress;
							}
						}
					}
					break;

					case RGX_HWRTYPE_ECCFAULT:
					{
						PVR_DUMPDEBUG_LOG("    ECC fault GPU=0x%08x", psHWRInfo->uHWRData.sECCInfo.ui32FaultGPU);
					}
					break;

					case RGX_HWRTYPE_MMUFAULT:
					{
						if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, S7_TOP_INFRASTRUCTURE))
						{
							_RGXDumpRGXMMUFaultStatus(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo,
											psHWRInfo->uHWRData.sMMUInfo.aui64MMUStatus[0],
											"Core",
											DD_NORMAL_INDENT);

							bPageFault = IMG_TRUE;
							sFaultDevVAddr.uiAddr =   psHWRInfo->uHWRData.sMMUInfo.aui64MMUStatus[0];
							sFaultDevVAddr.uiAddr &=  ~RGX_CR_MMU_FAULT_STATUS_ADDRESS_CLRMSK;
							sFaultDevVAddr.uiAddr >>= RGX_CR_MMU_FAULT_STATUS_ADDRESS_SHIFT;
							sFaultDevVAddr.uiAddr <<= 4; /* align shift */
							ui32PC  = (psHWRInfo->uHWRData.sMMUInfo.aui64MMUStatus[0] & ~RGX_CR_MMU_FAULT_STATUS_CONTEXT_CLRMSK) >>
													   RGX_CR_MMU_FAULT_STATUS_CONTEXT_SHIFT;
#if defined(SUPPORT_TRUSTED_DEVICE)
							ui32PC = ui32PC - 1;
#endif
							bPMFault = (ui32PC <= 8);
							sPCDevPAddr.uiAddr = psHWRInfo->uHWRData.sMMUInfo.ui64PCAddress;
						}
					}
					break;
					case RGX_HWRTYPE_MMUMETAFAULT:
					{
						if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, S7_TOP_INFRASTRUCTURE))
						{
							_RGXDumpRGXMMUFaultStatus(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo,
											psHWRInfo->uHWRData.sMMUInfo.aui64MMUStatus[0],
											"Meta",
											DD_NORMAL_INDENT);

							bPageFault = IMG_TRUE;
							sFaultDevVAddr.uiAddr =   psHWRInfo->uHWRData.sMMUInfo.aui64MMUStatus[0];
							sFaultDevVAddr.uiAddr &=  ~RGX_CR_MMU_FAULT_STATUS_ADDRESS_CLRMSK;
							sFaultDevVAddr.uiAddr >>= RGX_CR_MMU_FAULT_STATUS_ADDRESS_SHIFT;
							sFaultDevVAddr.uiAddr <<= 4; /* align shift */
							sPCDevPAddr.uiAddr = psHWRInfo->uHWRData.sMMUInfo.ui64PCAddress;
						}
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

					case RGX_HWRTYPE_MIPSTLBFAULT:
					{
						if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, MIPS))
						{
							IMG_UINT32 ui32EntryLo = psHWRInfo->uHWRData.sTLBInfo.ui32EntryLo;

							/* This is not exactly what the MMU code does, but the result should be the same */
							const IMG_UINT32 ui32UnmappedEntry =
								((IMG_UINT32)(MMU_BAD_PHYS_ADDR & 0xffffffff) & RGXMIPSFW_ENTRYLO_PFN_MASK_ABOVE_32BIT) | RGXMIPSFW_ENTRYLO_UNCACHED;

							PVR_DUMPDEBUG_LOG("    MIPS TLB fault: BadVA = 0x%08X, EntryLo = 0x%08X"
											  " (page PA 0x%" IMG_UINT64_FMTSPECx", V %u)",
											  psHWRInfo->uHWRData.sTLBInfo.ui32BadVAddr,
											  ui32EntryLo,
											  RGXMIPSFW_TLB_GET_PA(ui32EntryLo),
											  ui32EntryLo & RGXMIPSFW_TLB_VALID ? 1 : 0);

							if (ui32EntryLo == ui32UnmappedEntry)
							{
								PVR_DUMPDEBUG_LOG("    Potential use-after-free detected");
							}
						}
					}
					break;

					case RGX_HWRTYPE_MMURISCVFAULT:
					{
						if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, RISCV_FW_PROCESSOR))
						{
							_RGXDumpRGXBIFBank(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo, RGXDBG_FWCORE,
											psHWRInfo->uHWRData.sBIFInfo.ui64BIFMMUStatus,
											psHWRInfo->uHWRData.sBIFInfo.ui64BIFReqStatus,
											DD_NORMAL_INDENT);

							bPageFault = IMG_TRUE;
							bPMFault = IMG_FALSE;
							sFaultDevVAddr.uiAddr =
								(psHWRInfo->uHWRData.sBIFInfo.ui64BIFReqStatus &
								 ~RGX_CR_FWCORE_MEM_FAULT_REQ_STATUS_ADDRESS_CLRMSK);
							ui32PageSize =
								(psHWRInfo->uHWRData.sBIFInfo.ui64BIFMMUStatus &
								 ~RGX_CR_FWCORE_MEM_FAULT_MMU_STATUS_PAGE_SIZE_CLRMSK) >>
								RGX_CR_FWCORE_MEM_FAULT_MMU_STATUS_PAGE_SIZE_SHIFT;
							sPCDevPAddr.uiAddr = psHWRInfo->uHWRData.sBIFInfo.ui64PCAddress;
						}
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
						_PrintFaultInfo(pfnDumpDebugPrintf, pvDumpDebugFile, psInfo, DD_NORMAL_INDENT);
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

 @Input psDevInfo	 - RGX device info

 @Return   IMG_BOOL      - IMG_TRUE if there is a pending page

******************************************************************************/
static INLINE IMG_BOOL _CheckForPendingPage(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	IMG_UINT32 ui32BIFMMUEntry;

	ui32BIFMMUEntry = OSReadHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_BIF_MMU_ENTRY);

	if (ui32BIFMMUEntry & RGX_CR_BIF_MMU_ENTRY_PENDING_EN)
	{
		return IMG_TRUE;
	}
	else
	{
		return IMG_FALSE;
	}
}

/*!
*******************************************************************************

 @Function	_GetPendingPageInfo

 @Description

 Get information about the pending page from the MMU status registers

 @Input psDevInfo	 - RGX device info
 @Output psDevVAddr      - The device virtual address of the pending MMU address translation
 @Output pui32CatBase    - The page catalog base
 @Output pui32DataType   - The MMU entry data type

 @Return   void

******************************************************************************/
static void _GetPendingPageInfo(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_DEV_VIRTADDR *psDevVAddr,
									IMG_UINT32 *pui32CatBase,
									IMG_UINT32 *pui32DataType)
{
	IMG_UINT64 ui64BIFMMUEntryStatus;

	ui64BIFMMUEntryStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_BIF_MMU_ENTRY_STATUS);

	psDevVAddr->uiAddr = (ui64BIFMMUEntryStatus & ~RGX_CR_BIF_MMU_ENTRY_STATUS_ADDRESS_CLRMSK);

	*pui32CatBase = (ui64BIFMMUEntryStatus & ~RGX_CR_BIF_MMU_ENTRY_STATUS_CAT_BASE_CLRMSK) >>
								RGX_CR_BIF_MMU_ENTRY_STATUS_CAT_BASE_SHIFT;

	*pui32DataType = (ui64BIFMMUEntryStatus & ~RGX_CR_BIF_MMU_ENTRY_STATUS_DATA_TYPE_CLRMSK) >>
								RGX_CR_BIF_MMU_ENTRY_STATUS_DATA_TYPE_SHIFT;
}

#endif

void RGXDumpRGXDebugSummary(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile,
					PVRSRV_RGXDEV_INFO *psDevInfo,
					IMG_BOOL bRGXPoweredON)
{
	IMG_CHAR *pszState, *pszReason;
	RGXFWIF_SYSDATA *psFwSysData = psDevInfo->psRGXFWIfFwSysData;
	RGXFWIF_TRACEBUF *psRGXFWIfTraceBufCtl = psDevInfo->psRGXFWIfTraceBufCtl;
	IMG_UINT32 ui32OSid;
	RGXFWIF_RUNTIME_CFG *psRuntimeCfg = psDevInfo->psRGXFWIfRuntimeCfg;
	/* space for the current clock speed and 3 previous */
	RGXFWIF_TIME_CORR asTimeCorrs[4];
	IMG_UINT32 ui32NumClockSpeedChanges;

#if defined(NO_HARDWARE)
	PVR_UNREFERENCED_PARAMETER(bRGXPoweredON);
#else
	if ((bRGXPoweredON) && !PVRSRV_VZ_MODE_IS(GUEST))
	{
		if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, S7_TOP_INFRASTRUCTURE))
		{

			IMG_UINT64	ui64RegValMMUStatus;

			ui64RegValMMUStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_MMU_FAULT_STATUS);
			_RGXDumpRGXMMUFaultStatus(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo, ui64RegValMMUStatus, "Core", DD_SUMMARY_INDENT);

			ui64RegValMMUStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_MMU_FAULT_STATUS_META);
			_RGXDumpRGXMMUFaultStatus(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo, ui64RegValMMUStatus, "Meta", DD_SUMMARY_INDENT);
		}
		else
		{
			IMG_UINT64	ui64RegValMMUStatus, ui64RegValREQStatus;

			ui64RegValMMUStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_BIF_FAULT_BANK0_MMU_STATUS);
			ui64RegValREQStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_BIF_FAULT_BANK0_REQ_STATUS);

			_RGXDumpRGXBIFBank(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo, RGXDBG_BIF0, ui64RegValMMUStatus, ui64RegValREQStatus, DD_SUMMARY_INDENT);

			if (!(RGX_IS_FEATURE_SUPPORTED(psDevInfo, SINGLE_BIF)))
			{
				ui64RegValMMUStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_BIF_FAULT_BANK1_MMU_STATUS);
				ui64RegValREQStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_BIF_FAULT_BANK1_REQ_STATUS);
				_RGXDumpRGXBIFBank(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo, RGXDBG_BIF1, ui64RegValMMUStatus, ui64RegValREQStatus, DD_SUMMARY_INDENT);
			}

			if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, RISCV_FW_PROCESSOR))
			{
				ui64RegValMMUStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_FWCORE_MEM_FAULT_MMU_STATUS);
				ui64RegValREQStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_FWCORE_MEM_FAULT_REQ_STATUS);
				_RGXDumpRGXBIFBank(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo, RGXDBG_FWCORE, ui64RegValMMUStatus, ui64RegValREQStatus, DD_SUMMARY_INDENT);
			}

			if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, CLUSTER_GROUPING))
			{
				IMG_UINT32  ui32PhantomCnt = RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, NUM_CLUSTERS) ?  RGX_REQ_NUM_PHANTOMS(RGX_GET_FEATURE_VALUE(psDevInfo, NUM_CLUSTERS)) : 0;

				if (ui32PhantomCnt > 1)
				{
					IMG_UINT32  ui32Phantom;
					for (ui32Phantom = 0;  ui32Phantom < ui32PhantomCnt;  ui32Phantom++)
					{
						/* This can't be done as it may interfere with the FW... */
						/*OSWriteHWReg64(RGX_CR_TEXAS_INDIRECT, ui32Phantom);*/

						ui64RegValMMUStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_TEXAS_BIF_FAULT_BANK0_MMU_STATUS);
						ui64RegValREQStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_TEXAS_BIF_FAULT_BANK0_REQ_STATUS);

						_RGXDumpRGXBIFBank(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo, RGXDBG_TEXAS_BIF, ui64RegValMMUStatus, ui64RegValREQStatus, DD_SUMMARY_INDENT);
					}
				}else
				{
					ui64RegValMMUStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_TEXAS_BIF_FAULT_BANK0_MMU_STATUS);
					ui64RegValREQStatus = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_TEXAS_BIF_FAULT_BANK0_REQ_STATUS);

					_RGXDumpRGXBIFBank(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo, RGXDBG_TEXAS_BIF, ui64RegValMMUStatus, ui64RegValREQStatus, DD_SUMMARY_INDENT);
				}
			}
		}

		if (_CheckForPendingPage(psDevInfo))
		{
			IMG_UINT32 ui32CatBase;
			IMG_UINT32 ui32DataType;
			IMG_DEV_VIRTADDR sDevVAddr;

			PVR_DUMPDEBUG_LOG("MMU Pending page: Yes");

			_GetPendingPageInfo(psDevInfo, &sDevVAddr, &ui32CatBase, &ui32DataType);

			if (ui32CatBase >= 8)
			{
				PVR_DUMPDEBUG_LOG("Cannot check address on PM cat base %u", ui32CatBase);
			}
			else
			{
				IMG_DEV_PHYADDR sPCDevPAddr;
				MMU_FAULT_DATA sFaultData;

				sPCDevPAddr.uiAddr = OSReadHWReg64(psDevInfo->pvRegsBaseKM, RGX_CR_BIF_CAT_BASEN(ui32CatBase));

				PVR_DUMPDEBUG_LOG("Checking device virtual address " IMG_DEV_VIRTADDR_FMTSPEC
							" on cat base %u. PC Addr = 0x%" IMG_UINT64_FMTSPECx,
								sDevVAddr.uiAddr,
								ui32CatBase,
								sPCDevPAddr.uiAddr);
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
#if defined(RGX_NUM_OS_SUPPORTED) && (RGX_NUM_OS_SUPPORTED > 1)
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
#endif /* (RGX_NUM_OS_SUPPORTED > 1) */

#if defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS) || (defined(RGX_NUM_OS_SUPPORTED) && (RGX_NUM_OS_SUPPORTED > 1))
	{
		RGXFWIF_CONNECTION_FW_STATE eFwState = KM_GET_FW_CONNECTION(psDevInfo);
		RGXFWIF_CONNECTION_OS_STATE eOsState = KM_GET_OS_CONNECTION(psDevInfo);

		PVR_DUMPDEBUG_LOG("RGX Virtualisation firmware connection state: %s (Fw=%s; OS=%s)",
						  ((eFwState == RGXFW_CONNECTION_FW_ACTIVE) && (eOsState == RGXFW_CONNECTION_OS_ACTIVE)) ? ("UP") : ("DOWN"),
						  (eFwState < RGXFW_CONNECTION_FW_STATE_COUNT) ? (apszFwOsStateName[eFwState]) : ("invalid"),
						  (eOsState < RGXFW_CONNECTION_OS_STATE_COUNT) ? (apszFwOsStateName[eOsState]) : ("invalid"));

	}
#endif

#if defined(SUPPORT_AUTOVZ) && defined(RGX_NUM_OS_SUPPORTED) && (RGX_NUM_OS_SUPPORTED > 1)
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
		IMG_BOOL bOsIsolationEnabled = IMG_FALSE;

		if (psFwSysData == NULL)
		{
			/* can't dump any more information */
			PVR_DUMPDEBUG_LOG("RGX FW State: %s%s", pszState, pszReason);
			return;
		}

		sHwrStateDescription[0] = '\0';
		_Flags2Description(sHwrStateDescription, RGX_DEBUG_STR_SIZE,
			asHwrState2Description, ARRAY_SIZE(asHwrState2Description),
			psFwSysData->ui32HWRStateFlags);
		PVR_DUMPDEBUG_LOG("RGX FW State: %s%s (HWRState 0x%08x:%s)", pszState, pszReason, psFwSysData->ui32HWRStateFlags, sHwrStateDescription);
		PVR_DUMPDEBUG_LOG("RGX FW Power State: %s (APM %s: %d ok, %d denied, %d non-idle, %d retry, %d other, %d total. Latency: %u ms)",
	                  pszPowStateName[psFwSysData->ePowState],
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

		for (ui32OSid = 0; ui32OSid < RGX_NUM_OS_SUPPORTED; ui32OSid++)
		{
			RGXFWIF_OS_RUNTIME_FLAGS sFwRunFlags = psFwSysData->asOsRuntimeFlagsMirror[ui32OSid];

			IMG_BOOL bMTSEnabled = (RGX_IS_BRN_SUPPORTED(psDevInfo, 64502) || !RGX_IS_FEATURE_SUPPORTED(psDevInfo, GPU_VIRTUALISATION)) ?
									IMG_TRUE : ((OSReadHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_MTS_SCHEDULE_ENABLE) & BIT(ui32OSid)) != 0);

			PVR_DUMPDEBUG_LOG("RGX FW OS %u - State: %s; Freelists: %s%s; Priority: %d;%s %s", ui32OSid,
							  apszFwOsStateName[sFwRunFlags.bfOsState],
							  (sFwRunFlags.bfFLOk) ? "Ok" : "Not Ok",
							  (sFwRunFlags.bfFLGrowPending) ? "; Grow Request Pending" : "",
							  psDevInfo->psRGXFWIfRuntimeCfg->aui32OSidPriority[ui32OSid],
							  (sFwRunFlags.bfIsolatedOS) ? " Isolated;" : "",
							  (bMTSEnabled) ? "MTS on;" : "MTS off;"
							 );

			bOsIsolationEnabled |= sFwRunFlags.bfIsolatedOS;
		}
#if defined(PVR_ENABLE_PHR)
		{
			IMG_CHAR sPHRConfigDescription[RGX_DEBUG_STR_SIZE];

			sPHRConfigDescription[0] = '\0';
			_Flags2Description(sPHRConfigDescription, RGX_DEBUG_STR_SIZE,
			                   asPHRConfig2Description, ARRAY_SIZE(asPHRConfig2Description),
			                   BIT_ULL(psDevInfo->psRGXFWIfRuntimeCfg->ui32PHRMode));

			PVR_DUMPDEBUG_LOG("RGX PHR configuration: (%d) %s", psDevInfo->psRGXFWIfRuntimeCfg->ui32PHRMode, sPHRConfigDescription);
		}
#endif

		if (bOsIsolationEnabled)
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
		IMG_UINT32  *pui32FWStatsBuf = psFwSysData->aui32FWStatsBuf;
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

static void _RGXDumpMetaSPExtraDebugInfo(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
						void *pvDumpDebugFile,
						PVRSRV_RGXDEV_INFO *psDevInfo)
{
/* List of extra META Slave Port debug registers */
#define RGX_META_SP_EXTRA_DEBUG \
			X(RGX_CR_META_SP_MSLVCTRL0) \
			X(RGX_CR_META_SP_MSLVCTRL1) \
			X(RGX_CR_META_SP_MSLVDATAX) \
			X(RGX_CR_META_SP_MSLVIRQSTATUS) \
			X(RGX_CR_META_SP_MSLVIRQENABLE) \
			X(RGX_CR_META_SP_MSLVIRQLEVEL)

	IMG_UINT32 ui32Idx, ui32RegIdx;
	IMG_UINT32 ui32RegVal;
	IMG_UINT32 ui32RegAddr;

	const IMG_UINT32 aui32DebugRegAddr[] = {
#define X(A) A,
		RGX_META_SP_EXTRA_DEBUG
#undef X
		};

	const IMG_CHAR* apszDebugRegName[] = {
#define X(A) #A,
	RGX_META_SP_EXTRA_DEBUG
#undef X
	};

	const IMG_UINT32 aui32Debug2RegAddr[] = {0xA28, 0x0A30, 0x0A38};

	PVR_DUMPDEBUG_LOG("META Slave Port extra debug:");

	/* dump first set of Slave Port debug registers */
	for (ui32Idx = 0; ui32Idx < sizeof(aui32DebugRegAddr)/sizeof(IMG_UINT32); ui32Idx++)
	{
		const IMG_CHAR* pszRegName = apszDebugRegName[ui32Idx];

		ui32RegAddr = aui32DebugRegAddr[ui32Idx];
		ui32RegVal = OSReadHWReg32(psDevInfo->pvRegsBaseKM, ui32RegAddr);
		PVR_DUMPDEBUG_LOG("  * %s: 0x%8.8X", pszRegName, ui32RegVal);
	}

	/* dump second set of Slave Port debug registers */
	for (ui32Idx = 0; ui32Idx < 4; ui32Idx++)
	{
		OSWriteHWReg32(psDevInfo->pvRegsBaseKM, 0xA20, ui32Idx);
		ui32RegVal = OSReadHWReg32(psDevInfo->pvRegsBaseKM, 0xA20);
		PVR_DUMPDEBUG_LOG("  * 0xA20[%d]: 0x%8.8X", ui32Idx, ui32RegVal);

	}

	for (ui32RegIdx = 0; ui32RegIdx < sizeof(aui32Debug2RegAddr)/sizeof(IMG_UINT32); ui32RegIdx++)
	{
		ui32RegAddr = aui32Debug2RegAddr[ui32RegIdx];
		for (ui32Idx = 0; ui32Idx < 2; ui32Idx++)
		{
			OSWriteHWReg32(psDevInfo->pvRegsBaseKM, ui32RegAddr, ui32Idx);
			ui32RegVal = OSReadHWReg32(psDevInfo->pvRegsBaseKM, ui32RegAddr);
			PVR_DUMPDEBUG_LOG("  * 0x%X[%d]: 0x%8.8X", ui32RegAddr, ui32Idx, ui32RegVal);
		}
	}

}

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
			IMG_UINT32  *pui32TraceBuf = psRGXFWIfTraceBufCtl->sTraceBuf[tid].pui32TraceBuffer;
			IMG_UINT32  ui32TracePtr  = psRGXFWIfTraceBufCtl->sTraceBuf[tid].ui32TracePointer;
			IMG_UINT32  ui32Count     = 0;

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
					ui32Data     = pui32TraceBuf[ui32TracePtr];
					ui32DataToId = idToStringID(ui32Data, SFs);

					/* If an unrecognized id is found it may be inconsistent data or a firmware trace error. */
					if (ui32DataToId == RGXFW_SF_LAST  &&  RGXFW_LOG_VALIDID(ui32Data))
					{
						PVR_DUMPDEBUG_LOG("WARNING: Unrecognized id (%x). From here on the trace might be wrong!", ui32Data);
					}

					/* Update the trace pointer... */
					ui32TracePtr = (ui32TracePtr + 1) % ui32TraceBufSizeInDWords;
					ui32Count++;
				} while ((RGXFW_SF_LAST == ui32DataToId  ||  ui32DataToId >= RGXFW_SF_FIRST)  &&
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

					ui64Timestamp = (IMG_UINT64)(pui32TraceBuf[(ui32TracePtr + 0) % ui32TraceBufSizeInDWords]) << 32 |
					                (IMG_UINT64)(pui32TraceBuf[(ui32TracePtr + 1) % ui32TraceBufSizeInDWords]);

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
									  pui32TraceBuf[(ui32TracePtr +  2) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32TracePtr +  3) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32TracePtr +  4) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32TracePtr +  5) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32TracePtr +  6) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32TracePtr +  7) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32TracePtr +  8) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32TracePtr +  9) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32TracePtr + 10) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32TracePtr + 11) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32TracePtr + 12) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32TracePtr + 13) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32TracePtr + 14) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32TracePtr + 15) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32TracePtr + 16) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32TracePtr + 17) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32TracePtr + 18) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32TracePtr + 19) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32TracePtr + 20) % ui32TraceBufSizeInDWords],
									  pui32TraceBuf[(ui32TracePtr + 21) % ui32TraceBufSizeInDWords]);

					/* Update the trace pointer... */
					ui32TracePtr = (ui32TracePtr + 2 + RGXFW_SF_PARAMNUM(ui32Data)) % ui32TraceBufSizeInDWords;
					ui32Count    = (ui32Count    + 2 + RGXFW_SF_PARAMNUM(ui32Data));
				}
			}
		}
	}
}

#if defined(SUPPORT_POWER_VALIDATION_VIA_DEBUGFS)
void RGXDumpPowerMonitoring(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile,
				PVRSRV_RGXDEV_INFO  *psDevInfo)
{
	RGXFWIF_SYSDATA  *psFwSysData = psDevInfo->psRGXFWIfFwSysData;

	/* Print the power monitoring counters... */
	if (psFwSysData != NULL)
	{
		IMG_UINT32  *pui32TraceBuf = psFwSysData->sPowerMonBuf.pui32TraceBuffer;
		IMG_UINT32  ui32TracePtr  = 0; //psFwSysData->sPowerMonBuf.ui32TracePointer;
		IMG_UINT32  ui32PowerMonBufSizeInDWords = psFwSysData->ui32PowerMonBufSizeInDWords;
		IMG_UINT32  ui32Count     = 0;
		IMG_UINT64  ui64Timestamp;

		if (pui32TraceBuf == NULL)
		{
			/* power monitoring buffer not yet allocated */
			return;
		}

		if (pui32TraceBuf[ui32TracePtr] != RGX_CR_TIMER)
		{
			PVR_DPF((PVR_DBG_WARNING, "Power monitoring data not available."));
			return;
		}
		ui64Timestamp = (IMG_UINT64)(pui32TraceBuf[(ui32TracePtr + 1) % ui32PowerMonBufSizeInDWords]) << 32 |
						(IMG_UINT64)(pui32TraceBuf[(ui32TracePtr + 2) % ui32PowerMonBufSizeInDWords]);

		/* Update the trace pointer... */
		ui32TracePtr = (ui32TracePtr + 3) % ui32PowerMonBufSizeInDWords;
		ui32Count    = (ui32Count    + 3);

		PVR_DPF((PVR_DBG_WARNING, "Dumping power monitoring buffer: CPUVAddr = %p, pointer = 0x%x, size = 0x%x",
				 pui32TraceBuf,
				 ui32TracePtr,
				 ui32PowerMonBufSizeInDWords));

		while (ui32Count < ui32PowerMonBufSizeInDWords)
		{
			/* power monitoring data is (register, value) dword pairs */
			PVR_DUMPDEBUG_LOG("%" IMG_UINT64_FMTSPEC ":POWMON  0x%08x 0x%08x  0x%08x 0x%08x",
							  ui64Timestamp,
							  pui32TraceBuf[(ui32TracePtr + 0) % ui32PowerMonBufSizeInDWords],
							  pui32TraceBuf[(ui32TracePtr + 1) % ui32PowerMonBufSizeInDWords],
							  pui32TraceBuf[(ui32TracePtr + 2) % ui32PowerMonBufSizeInDWords],
							  pui32TraceBuf[(ui32TracePtr + 3) % ui32PowerMonBufSizeInDWords]);

			if (pui32TraceBuf[(ui32TracePtr + 0) % ui32PowerMonBufSizeInDWords] == RGXFWIF_TIMEDIFF_ID ||
				pui32TraceBuf[(ui32TracePtr + 2) % ui32PowerMonBufSizeInDWords] == RGXFWIF_TIMEDIFF_ID)
			{
				/* end of buffer */
				break;
			}

			/* Update the trace pointer... */
			ui32TracePtr = (ui32TracePtr + 4) % ui32PowerMonBufSizeInDWords;
			ui32Count    = (ui32Count    + 4);
		}
	}
}
#endif

static const IMG_CHAR *_RGXGetDebugDevStateString(PVRSRV_DEVICE_STATE eDevState)
{
	switch (eDevState)
	{
		case PVRSRV_DEVICE_STATE_INIT:
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
#define REG64_FMTSPEC   "%-30s: 0x%016" IMG_UINT64_FMTSPECx
#define DDLOG32(R)      PVR_DUMPDEBUG_LOG(REG32_FMTSPEC, #R, OSReadHWReg32(pvRegsBaseKM, RGX_CR_##R));
#define DDLOG64(R)      PVR_DUMPDEBUG_LOG(REG64_FMTSPEC, #R, OSReadHWReg64(pvRegsBaseKM, RGX_CR_##R));
#define DDLOG32_DPX(R)  PVR_DUMPDEBUG_LOG(REG32_FMTSPEC, #R, OSReadHWReg32(pvRegsBaseKM, DPX_CR_##R));
#define DDLOG64_DPX(R)  PVR_DUMPDEBUG_LOG(REG64_FMTSPEC, #R, OSReadHWReg64(pvRegsBaseKM, DPX_CR_##R));
#define DDLOGVAL32(S,V) PVR_DUMPDEBUG_LOG(REG32_FMTSPEC, S, V);

#if !defined(NO_HARDWARE)
static void RGXDumpMIPSState(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
							 void *pvDumpDebugFile,
							 PVRSRV_RGXDEV_INFO *psDevInfo)
{
	void __iomem *pvRegsBaseKM = psDevInfo->pvRegsBaseKM;
	RGX_MIPS_STATE sMIPSState = {0};
	PVRSRV_ERROR eError;

	eError = _RGXMipsExtraDebug(psDevInfo, &sMIPSState);
	PVR_DUMPDEBUG_LOG("---- [ MIPS internal state ] ----");
	if (eError != PVRSRV_OK)
	{
		PVR_DUMPDEBUG_LOG("MIPS extra debug not available");
	}
	else
	{
		DDLOGVAL32("PC", sMIPSState.ui32ErrorEPC);
		DDLOGVAL32("STATUS_REGISTER", sMIPSState.ui32StatusRegister);
		DDLOGVAL32("CAUSE_REGISTER", sMIPSState.ui32CauseRegister);
		_RGXMipsDumpCauseDecode(pfnDumpDebugPrintf, pvDumpDebugFile,
								sMIPSState.ui32CauseRegister, sMIPSState.ui32ErrorState);
		DDLOGVAL32("BAD_REGISTER", sMIPSState.ui32BadRegister);
		DDLOGVAL32("EPC", sMIPSState.ui32EPC);
		DDLOGVAL32("SP", sMIPSState.ui32SP);
		DDLOGVAL32("BAD_INSTRUCTION", sMIPSState.ui32BadInstr);
		_RGXMipsDumpDebugDecode(psDevInfo, pfnDumpDebugPrintf, pvDumpDebugFile,
								sMIPSState.ui32Debug, sMIPSState.ui32DEPC);

		{
			IMG_UINT32 ui32Idx;

			IMG_BOOL bCheckBRN63553WA =
				RGX_IS_BRN_SUPPORTED(psDevInfo, 63553) &&
				(OSReadHWReg32(pvRegsBaseKM, RGX_CR_MIPS_ADDR_REMAP5_CONFIG1) == (0x0 | RGX_CR_MIPS_ADDR_REMAP5_CONFIG1_MODE_ENABLE_EN));

			IMG_BOOL bUseRemapRanges = RGX_GET_FEATURE_VALUE(psDevInfo, PHYS_BUS_WIDTH) > 32;

			PVR_DUMPDEBUG_LOG("TLB                           :");

			for (ui32Idx = 0; ui32Idx < ARRAY_SIZE(sMIPSState.asTLB); ui32Idx++)
			{
				RGX_MIPS_REMAP_ENTRY *psRemapEntry0 = NULL;
				RGX_MIPS_REMAP_ENTRY *psRemapEntry1 = NULL;

				if (bUseRemapRanges)
				{
					psRemapEntry0 = &sMIPSState.asRemap[ui32Idx];
					psRemapEntry1 = &sMIPSState.asRemap[ui32Idx+16];
				}

				_RGXMipsDumpTLBEntry(pfnDumpDebugPrintf,
								     pvDumpDebugFile,
								     &sMIPSState.asTLB[ui32Idx],
								     psRemapEntry0,
								     psRemapEntry1,
								     ui32Idx);

				if (bCheckBRN63553WA)
				{
					const RGX_MIPS_TLB_ENTRY *psTLBEntry = &sMIPSState.asTLB[ui32Idx];

					#define BRN63553_TLB_IS_NUL(X)  (((X) & RGXMIPSFW_TLB_VALID) && (RGXMIPSFW_TLB_GET_PA(X) == 0x0))

					if (BRN63553_TLB_IS_NUL(psTLBEntry->ui32TLBLo0) || BRN63553_TLB_IS_NUL(psTLBEntry->ui32TLBLo1))
					{
						PVR_DUMPDEBUG_LOG("BRN63553 WA present with a valid TLB entry mapping address 0x0.");
					}
				}
			}

			/* This implicitly also checks for overlaps between memory and regbank addresses */
			_CheckMipsTLBDuplicatePAs(pfnDumpDebugPrintf,
									  pvDumpDebugFile,
									  sMIPSState.asTLB,
									  bUseRemapRanges ? sMIPSState.asRemap : NULL);

			if (bUseRemapRanges)
			{
				/* Dump unmapped address if it was dumped in FW, otherwise it will be 0 */
				if (sMIPSState.ui32UnmappedAddress)
				{
					PVR_DUMPDEBUG_LOG("Remap unmapped address => 0x%08X",
									  sMIPSState.ui32UnmappedAddress);
				}
			}
		}

		/* Check FW code corruption in case of known errors */
		if (_IsFWCodeException(RGXMIPSFW_C0_CAUSE_EXCCODE(sMIPSState.ui32CauseRegister)))
		{
			eError = _ValidateFWImageForMIPS(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo, "");
			if (eError != PVRSRV_OK)
			{
				PVR_DUMPDEBUG_LOG("Failed to validate any FW code corruption");
			}
		}
	}
	PVR_DUMPDEBUG_LOG("--------------------------------");
}
#endif

static PVRSRV_ERROR RGXDumpRISCVState(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
									  void *pvDumpDebugFile,
									  PVRSRV_RGXDEV_INFO *psDevInfo)
{
	void __iomem *pvRegsBaseKM = psDevInfo->pvRegsBaseKM;
	RGXRISCVFW_STATE sRiscvState;
	const IMG_CHAR *pszException;
	PVRSRV_ERROR eError;

#if defined(NO_HARDWARE)
	/* OSReadHWReg variants don't use params passed in NoHW builds */
	PVR_UNREFERENCED_PARAMETER(pvRegsBaseKM);
#endif

	DDLOG64(FWCORE_MEM_CAT_BASE0);
	DDLOG64(FWCORE_MEM_CAT_BASE1);
	DDLOG64(FWCORE_MEM_CAT_BASE2);
	DDLOG64(FWCORE_MEM_CAT_BASE3);
	DDLOG64(FWCORE_MEM_CAT_BASE4);
	DDLOG64(FWCORE_MEM_CAT_BASE5);
	DDLOG64(FWCORE_MEM_CAT_BASE6);
	DDLOG64(FWCORE_MEM_CAT_BASE7);

	/* Limit dump to what is currently being used */
	DDLOG64(FWCORE_ADDR_REMAP_CONFIG4);
	DDLOG64(FWCORE_ADDR_REMAP_CONFIG5);
	DDLOG64(FWCORE_ADDR_REMAP_CONFIG6);
	DDLOG64(FWCORE_ADDR_REMAP_CONFIG12);
	DDLOG64(FWCORE_ADDR_REMAP_CONFIG13);
	DDLOG64(FWCORE_ADDR_REMAP_CONFIG14);

	DDLOG32(FWCORE_MEM_FAULT_MMU_STATUS);
	DDLOG64(FWCORE_MEM_FAULT_REQ_STATUS);
	DDLOG32(FWCORE_MEM_MMU_STATUS);
	DDLOG32(FWCORE_MEM_READS_EXT_STATUS);
	DDLOG32(FWCORE_MEM_READS_INT_STATUS);

	PVR_DUMPDEBUG_LOG("---- [ RISC-V internal state ] ----");

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

		eError = _ValidateFWImageForRISCV(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo);
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

PVRSRV_ERROR RGXDumpRGXRegisters(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
								 void *pvDumpDebugFile,
								 PVRSRV_RGXDEV_INFO *psDevInfo)
{
	IMG_UINT32   ui32Meta = (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META)) ? RGX_GET_FEATURE_VALUE(psDevInfo, META) : 0;
	IMG_UINT32   ui32TACycles, ui323DCycles, ui32TAOr3DCycles, ui32TAAnd3DCycles;
	IMG_UINT32   ui32RegVal;
	IMG_BOOL     bFirmwarePerf;
	IMG_BOOL     bS7Infra = RGX_IS_FEATURE_SUPPORTED(psDevInfo, S7_TOP_INFRASTRUCTURE);
	IMG_BOOL     bMulticore = RGX_IS_FEATURE_SUPPORTED(psDevInfo, GPU_MULTICORE_SUPPORT);
	void __iomem *pvRegsBaseKM = psDevInfo->pvRegsBaseKM;
	PVRSRV_ERROR eError;

	PVR_DUMPDEBUG_LOG("------[ RGX registers ]------");
	PVR_DUMPDEBUG_LOG("RGX Register Base Address (Linear):   0x%p", psDevInfo->pvRegsBaseKM);
	PVR_DUMPDEBUG_LOG("RGX Register Base Address (Physical): 0x%08lX", (unsigned long)psDevInfo->sRegsPhysBase.uiAddr);

	/* Check if firmware perf was set at Init time */
	bFirmwarePerf = (psDevInfo->psRGXFWIfSysInit->eFirmwarePerf != FW_PERF_CONF_NONE);

#if defined(NO_HARDWARE)
	/* OSReadHWReg variants don't use params passed in NoHW builds */
	PVR_UNREFERENCED_PARAMETER(pvRegsBaseKM);
#endif

	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, PBVNC_COREID_REG))
	{
		DDLOG64(CORE_ID);
	}
	else
	{
		DDLOG32(CORE_ID);
	}
	DDLOG32(CORE_REVISION);
	DDLOG32(DESIGNER_REV_FIELD1);
	DDLOG32(DESIGNER_REV_FIELD2);
	DDLOG64(CHANGESET_NUMBER);
	if (ui32Meta)
	{
		DDLOG32(META_SP_MSLVIRQSTATUS);
	}

	if (bMulticore)
	{
		DDLOG32(MULTICORE_SYSTEM);
		DDLOG32(MULTICORE_GPU);
	}

	DDLOG64(CLK_CTRL);
	DDLOG64(CLK_STATUS);
	DDLOG64(CLK_CTRL2);
	DDLOG64(CLK_STATUS2);

	if (bS7Infra)
	{
		DDLOG64(CLK_XTPLUS_CTRL);
		DDLOG64(CLK_XTPLUS_STATUS);
	}
	DDLOG32(EVENT_STATUS);
	DDLOG64(TIMER);
	if (bS7Infra)
	{
		DDLOG64(MMU_FAULT_STATUS);
		DDLOG64(MMU_FAULT_STATUS_META);
	}
	else
	{
		DDLOG32(BIF_FAULT_BANK0_MMU_STATUS);
		DDLOG64(BIF_FAULT_BANK0_REQ_STATUS);
		DDLOG32(BIF_FAULT_BANK1_MMU_STATUS);
		DDLOG64(BIF_FAULT_BANK1_REQ_STATUS);
	}
	DDLOG32(BIF_MMU_STATUS);
	DDLOG32(BIF_MMU_ENTRY);
	DDLOG64(BIF_MMU_ENTRY_STATUS);

	if (bS7Infra)
	{
		DDLOG32(BIF_JONES_OUTSTANDING_READ);
		DDLOG32(BIF_BLACKPEARL_OUTSTANDING_READ);
		DDLOG32(BIF_DUST_OUTSTANDING_READ);
	}
	else
	{
		if (!(RGX_IS_FEATURE_SUPPORTED(psDevInfo, XT_TOP_INFRASTRUCTURE)))
		{
			DDLOG32(BIF_STATUS_MMU);
			DDLOG32(BIF_READS_EXT_STATUS);
			DDLOG32(BIF_READS_INT_STATUS);
		}
		DDLOG32(BIFPM_STATUS_MMU);
		DDLOG32(BIFPM_READS_EXT_STATUS);
		DDLOG32(BIFPM_READS_INT_STATUS);
	}

	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, SLC_VIVT))
	{
		DDLOG64(CONTEXT_MAPPING0);
		DDLOG64(CONTEXT_MAPPING1);
		DDLOG64(CONTEXT_MAPPING2);
		DDLOG64(CONTEXT_MAPPING3);
		DDLOG64(CONTEXT_MAPPING4);
	}
	else
	{
		DDLOG64(BIF_CAT_BASE_INDEX);
		DDLOG64(BIF_CAT_BASE0);
		DDLOG64(BIF_CAT_BASE1);
		DDLOG64(BIF_CAT_BASE2);
		DDLOG64(BIF_CAT_BASE3);
		DDLOG64(BIF_CAT_BASE4);
		DDLOG64(BIF_CAT_BASE5);
		DDLOG64(BIF_CAT_BASE6);
		DDLOG64(BIF_CAT_BASE7);
	}

	DDLOG32(BIF_CTRL_INVAL);
	DDLOG32(BIF_CTRL);

	DDLOG64(BIF_PM_CAT_BASE_VCE0);
	DDLOG64(BIF_PM_CAT_BASE_TE0);
	DDLOG64(BIF_PM_CAT_BASE_ALIST0);
	DDLOG64(BIF_PM_CAT_BASE_VCE1);
	DDLOG64(BIF_PM_CAT_BASE_TE1);
	DDLOG64(BIF_PM_CAT_BASE_ALIST1);

	if (bMulticore)
	{
		DDLOG32(MULTICORE_GEOMETRY_CTRL_COMMON);
		DDLOG32(MULTICORE_FRAGMENT_CTRL_COMMON);
		DDLOG32(MULTICORE_COMPUTE_CTRL_COMMON);
	}

	DDLOG32(PERF_TA_PHASE);
	DDLOG32(PERF_TA_CYCLE);
	DDLOG32(PERF_3D_PHASE);
	DDLOG32(PERF_3D_CYCLE);

	ui32TACycles = OSReadHWReg32(pvRegsBaseKM, RGX_CR_PERF_TA_CYCLE);
	ui323DCycles = OSReadHWReg32(pvRegsBaseKM, RGX_CR_PERF_3D_CYCLE);
	ui32TAOr3DCycles = OSReadHWReg32(pvRegsBaseKM, RGX_CR_PERF_TA_OR_3D_CYCLE);
	ui32TAAnd3DCycles = ((ui32TACycles + ui323DCycles) > ui32TAOr3DCycles) ? (ui32TACycles + ui323DCycles - ui32TAOr3DCycles) : 0;
	DDLOGVAL32("PERF_TA_OR_3D_CYCLE", ui32TAOr3DCycles);
	DDLOGVAL32("PERF_TA_AND_3D_CYCLE", ui32TAAnd3DCycles);

	DDLOG32(PERF_COMPUTE_PHASE);
	DDLOG32(PERF_COMPUTE_CYCLE);

	DDLOG32(PM_PARTIAL_RENDER_ENABLE);

	DDLOG32(ISP_RENDER);
	DDLOG64(TLA_STATUS);
	DDLOG64(MCU_FENCE);

	DDLOG32(VDM_CONTEXT_STORE_STATUS);
	DDLOG64(VDM_CONTEXT_STORE_TASK0);
	DDLOG64(VDM_CONTEXT_STORE_TASK1);
	DDLOG64(VDM_CONTEXT_STORE_TASK2);
	DDLOG64(VDM_CONTEXT_RESUME_TASK0);
	DDLOG64(VDM_CONTEXT_RESUME_TASK1);
	DDLOG64(VDM_CONTEXT_RESUME_TASK2);

	DDLOG32(ISP_CTL);
	DDLOG32(ISP_STATUS);
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

	if (RGX_IS_ERN_SUPPORTED(psDevInfo, 47025))
	{
		DDLOG64(CDM_CONTEXT_LOAD_PDS0);
		DDLOG64(CDM_CONTEXT_LOAD_PDS1);
	}

	if (bS7Infra)
	{
		DDLOG32(JONES_IDLE);
	}

	DDLOG32(SIDEKICK_IDLE);

	if (!bS7Infra)
	{
		DDLOG32(SLC_IDLE);
		DDLOG32(SLC_STATUS0);
		DDLOG64(SLC_STATUS1);

		if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, SLC_BANKS) && RGX_GET_FEATURE_VALUE(psDevInfo, SLC_BANKS))
		{
			DDLOG64(SLC_STATUS2);
		}

		DDLOG32(SLC_CTRL_BYPASS);
		DDLOG64(SLC_CTRL_MISC);
	}
	else
	{
		DDLOG32(SLC3_IDLE);
		DDLOG64(SLC3_STATUS);
		DDLOG32(SLC3_FAULT_STOP_STATUS);
	}

	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, ROGUEXE) &&
		RGX_IS_FEATURE_SUPPORTED(psDevInfo, WATCHDOG_TIMER))
	{
		DDLOG32(SAFETY_EVENT_STATUS__ROGUEXE);
		DDLOG32(MTS_SAFETY_EVENT_ENABLE__ROGUEXE);
	}

	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, WATCHDOG_TIMER))
	{
		DDLOG32(FWCORE_WDT_CTRL);
	}

	if (PVRSRV_GET_DEVICE_FEATURE_VALUE(psDevInfo->psDeviceNode, LAYOUT_MARS) > 0)
	{
		DDLOG32(SCRATCH0);
		DDLOG32(SCRATCH1);
		DDLOG32(SCRATCH2);
		DDLOG32(SCRATCH3);
		DDLOG32(SCRATCH4);
		DDLOG32(SCRATCH5);
		DDLOG32(SCRATCH6);
		DDLOG32(SCRATCH7);
		DDLOG32(SCRATCH8);
		DDLOG32(SCRATCH9);
		DDLOG32(SCRATCH10);
		DDLOG32(SCRATCH11);
		DDLOG32(SCRATCH12);
		DDLOG32(SCRATCH13);
		DDLOG32(SCRATCH14);
		DDLOG32(SCRATCH15);
	}

	if (ui32Meta)
	{
		IMG_BOOL bIsT0Enabled = IMG_FALSE, bIsFWFaulted = IMG_FALSE;

		/* Forcing bit 6 of MslvCtrl1 to 0 to avoid internal reg read going through the core */
		OSWriteHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_META_SP_MSLVCTRL1, 0x0);

		eError = RGXReadWithSP(psDevInfo, META_CR_T0ENABLE_OFFSET, &ui32RegVal);
		PVR_LOG_GOTO_IF_ERROR(eError, "RGXReadWithSP", _METASPError);
		DDLOGVAL32("T0 TXENABLE", ui32RegVal);
		if (ui32RegVal & META_CR_TXENABLE_ENABLE_BIT)
		{
			bIsT0Enabled = IMG_TRUE;
		}

		eError = RGXReadWithSP(psDevInfo, META_CR_T0STATUS_OFFSET, &ui32RegVal);
		PVR_LOG_GOTO_IF_ERROR(eError, "RGXReadWithSP", _METASPError);
		DDLOGVAL32("T0 TXSTATUS", ui32RegVal);

		/* check for FW fault */
		if (((ui32RegVal >> 20) & 0x3) == 0x2)
		{
			bIsFWFaulted = IMG_TRUE;
		}

		eError = RGXReadWithSP(psDevInfo, META_CR_T0DEFR_OFFSET, &ui32RegVal);
		PVR_LOG_GOTO_IF_ERROR(eError, "RGXReadWithSP", _METASPError);
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
			eError = RGXReadWithSP(psDevInfo, META_CR_T1ENABLE_OFFSET, &ui32RegVal);
			PVR_LOG_GOTO_IF_ERROR(eError, "RGXReadWithSP", _METASPError);
			DDLOGVAL32("T1 TXENABLE", ui32RegVal);

			eError = RGXReadWithSP(psDevInfo, META_CR_T1STATUS_OFFSET, &ui32RegVal);
			PVR_LOG_GOTO_IF_ERROR(eError, "RGXReadWithSP", _METASPError);
			DDLOGVAL32("T1 TXSTATUS", ui32RegVal);

			eError = RGXReadWithSP(psDevInfo, META_CR_T1DEFR_OFFSET, &ui32RegVal);
			PVR_LOG_GOTO_IF_ERROR(eError, "RGXReadWithSP", _METASPError);
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
			eError = RGXReadWithSP(psDevInfo, META_CR_PERF_COUNT0, &ui32RegVal);
			PVR_LOG_GOTO_IF_ERROR(eError, "RGXReadWithSP", _METASPError);
			DDLOGVAL32("PERF_COUNT0", ui32RegVal);

			eError = RGXReadWithSP(psDevInfo, META_CR_PERF_COUNT1, &ui32RegVal);
			PVR_LOG_GOTO_IF_ERROR(eError, "RGXReadWithSP", _METASPError);
			DDLOGVAL32("PERF_COUNT1", ui32RegVal);
		}

		if (bIsT0Enabled & bIsFWFaulted)
		{
			eError = _ValidateFWImageForMETA(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo);
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

	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, MIPS))
	{
		DDLOG32(MIPS_ADDR_REMAP1_CONFIG1);
		DDLOG64(MIPS_ADDR_REMAP1_CONFIG2);
		DDLOG32(MIPS_ADDR_REMAP2_CONFIG1);
		DDLOG64(MIPS_ADDR_REMAP2_CONFIG2);
		DDLOG32(MIPS_ADDR_REMAP3_CONFIG1);
		DDLOG64(MIPS_ADDR_REMAP3_CONFIG2);
		DDLOG32(MIPS_ADDR_REMAP4_CONFIG1);
		DDLOG64(MIPS_ADDR_REMAP4_CONFIG2);
		DDLOG32(MIPS_ADDR_REMAP5_CONFIG1);
		DDLOG64(MIPS_ADDR_REMAP5_CONFIG2);
		DDLOG64(MIPS_WRAPPER_CONFIG);
		DDLOG32(MIPS_EXCEPTION_STATUS);

#if !defined(NO_HARDWARE)
		RGXDumpMIPSState(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo);
#endif
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
	RGXFWIF_OSDATA    *psFwOsData = psDevInfo->psRGXFWIfFwOsData;
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
		(PVRSRV_VZ_MODE_IS(HOST) && (ui8FwOsCount != RGX_NUM_OS_SUPPORTED)))
	{
		PVR_DUMPDEBUG_LOG("Mismatch between the number of Operating Systems supported by KM driver (%d) and FW (%d)",
						  (PVRSRV_VZ_MODE_IS(NATIVE)) ? (1) : (RGX_NUM_OS_SUPPORTED), ui8FwOsCount);
	}

	PVR_DUMPDEBUG_LOG("------[ RGX Device ID:%d Start ]------", psDevInfo->psDeviceNode->sDevId.ui32InternalID);

	bRGXPoweredON = (ePowerState == PVRSRV_DEV_POWER_STATE_ON);

	PVR_DUMPDEBUG_LOG("------[ RGX Info ]------");
	PVR_DUMPDEBUG_LOG("Device Node (Info): %p (%p)", psDevInfo->psDeviceNode, psDevInfo);
	PVR_DUMPDEBUG_LOG("RGX BVNC: %d.%d.%d.%d (%s)", psDevInfo->sDevFeatureCfg.ui32B, \
											   psDevInfo->sDevFeatureCfg.ui32V,	\
											   psDevInfo->sDevFeatureCfg.ui32N, \
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

	RGXDumpRGXDebugSummary(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo, bRGXPoweredON);

	/* Dump out the kernel CCB. */
	{
		RGXFWIF_CCB_CTL *psKCCBCtl = psDevInfo->psKernelCCBCtl;

		if (psKCCBCtl != NULL)
		{
			PVR_DUMPDEBUG_LOG("RGX Kernel CCB WO:0x%X RO:0x%X",
							  psKCCBCtl->ui32WriteOffset,
							  psKCCBCtl->ui32ReadOffset);
		}
	}

	/* Dump out the firmware CCB. */
	{
		RGXFWIF_CCB_CTL *psFCCBCtl = psDevInfo->psFirmwareCCBCtl;

		if (psFCCBCtl != NULL)
		{
			PVR_DUMPDEBUG_LOG("RGX Firmware CCB WO:0x%X RO:0x%X",
							   psFCCBCtl->ui32WriteOffset,
							   psFCCBCtl->ui32ReadOffset);
		}
	}

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	/* Dump out the Workload estimation CCB. */
	{
	    RGXFWIF_CCB_CTL *psWorkEstCCBCtl = psDevInfo->psWorkEstFirmwareCCBCtl;

	    if (psWorkEstCCBCtl != NULL)
	    {
	        PVR_DUMPDEBUG_LOG("RGX WorkEst CCB WO:0x%X RO:0x%X",
	                           psWorkEstCCBCtl->ui32WriteOffset,
	                           psWorkEstCCBCtl->ui32ReadOffset);
	    }
	}
#endif


	if (psFwOsData != NULL)
	{
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

		/* Dump the IRQ info for threads or OS IDs */
#if defined(RGX_FW_IRQ_OS_COUNTERS)
		/* only Host has access to registers containing IRQ counters */
		if (!PVRSRV_VZ_MODE_IS(GUEST))
#endif
		{
			IMG_UINT32 ui32idx;

			for_each_irq_cnt(ui32idx)
			{
				IMG_UINT32 ui32IrqCnt;

				get_irq_cnt_val(ui32IrqCnt, ui32idx, psDevInfo);
				if (ui32IrqCnt)
				{
					PVR_DUMPDEBUG_LOG(MSG_IRQ_CNT_TYPE "%u: FW IRQ count = %u", ui32idx, ui32IrqCnt);
#if defined(RGX_FW_IRQ_OS_COUNTERS)
					if (ui32idx == RGXFW_HOST_OS)
#endif
					{
						PVR_DUMPDEBUG_LOG("Last sampled IRQ count in LISR = %u", psDevInfo->aui32SampleIRQCount[ui32idx]);
					}
				}
			}
		}
	}

	/* Dump the FW Sys config flags on the Host */
	if (!PVRSRV_VZ_MODE_IS(GUEST))
	{
		RGXFWIF_SYSDATA   *psFwSysData = psDevInfo->psRGXFWIfFwSysData;
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
		RGXFWIF_OSDATA   *psFwOsData = psDevInfo->psRGXFWIfFwOsData;
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

		eError = RGXDumpRGXRegisters(pfnDumpDebugPrintf, pvDumpDebugFile, psDevInfo);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: RGXDumpRGXRegisters failed (%s)",
					__func__,
					PVRSRVGetErrorString(eError)));
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
				PVR_DUMPDEBUG_LOG("------[ Stalled FWCtxs ]------");
			}

			DumpTransferCtxtsInfo(psDevInfo, pfnDumpDebugPrintf, pvDumpDebugFile, ui32VerbLevel);

			DumpRenderCtxtsInfo(psDevInfo, pfnDumpDebugPrintf, pvDumpDebugFile, ui32VerbLevel);

			DumpKickSyncCtxtsInfo(psDevInfo, pfnDumpDebugPrintf, pvDumpDebugFile, ui32VerbLevel);

			if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, COMPUTE))
			{
				DumpComputeCtxtsInfo(psDevInfo, pfnDumpDebugPrintf, pvDumpDebugFile, ui32VerbLevel);
			}
			if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, FASTRENDER_DM))
			{
				DumpTDMTransferCtxtsInfo(psDevInfo, pfnDumpDebugPrintf, pvDumpDebugFile, ui32VerbLevel);
			}
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
	return PVRSRVRegisterDbgRequestNotify(&psDevInfo->hDbgReqNotify,
		                                   psDevInfo->psDeviceNode,
		                                   RGXDebugRequestNotify,
										   DEBUG_REQUEST_SYS,
		                                   psDevInfo);
}

PVRSRV_ERROR RGXDebugDeinit(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	if (psDevInfo->hDbgReqNotify)
	{
		return PVRSRVUnregisterDbgRequestNotify(psDevInfo->hDbgReqNotify);
	}

	/* No notifier registered */
	return PVRSRV_OK;
}

/******************************************************************************
 End of file (rgxdebug.c)
******************************************************************************/
