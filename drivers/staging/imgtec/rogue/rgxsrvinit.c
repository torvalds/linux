/*************************************************************************/ /*!
@File
@Title          Services initialisation routines
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

#include "img_defs.h"
#include "srvinit.h"
#include "pvr_debug.h"
#include "osfunc.h"
#include "km_apphint_defs.h"
#include "htbuffer_types.h"
#include "htbuffer_init.h"

#include "devicemem.h"
#include "devicemem_pdump.h"

#include "client_rgxinit_bridge.h"

#include "rgx_fwif_sig.h"

#include "rgx_compat_bvnc.h"

#include "srvinit_osfunc.h"

#if !defined(SUPPORT_KERNEL_SRVINIT)
#include "rgxdefs.h"
#else
#include "rgxdefs_km.h"
#endif

#if defined(SUPPORT_GPUVIRT_VALIDATION)
#include "virt_validation_defs.h"
#endif

#include "srvinit_pdump.h"

#include "rgx_fwif_hwperf.h"
#include "rgx_hwperf_table.h"

#include "rgxsrvinit_script.h"

#include "rgxfwload.h"
#include "rgxlayer_impl.h"
#include "rgxfwimageutils.h"

#include "rgx_hwperf_km.h"
#include "rgx_bvnc_defs_km.h"

#if !defined(SUPPORT_KERNEL_SRVINIT)
#include "rgx_hwperf.h"
#include "rgx_fwif_km.h"
#include "rgx_fwif_client.h"
#include "rgx_fwif_alignchecks.h"
#else
#include "rgxdevice.h"
#endif
static RGX_INIT_COMMAND asDbgCommands[RGX_MAX_DEBUG_COMMANDS];

#if defined(SUPPORT_TRUSTED_DEVICE)
#if !defined(SUPPORT_KERNEL_SRVINIT)
#error "SUPPORT_KERNEL_SRVINIT is required by SUPPORT_TRUSTED_DEVICE!"
#endif
#include "rgxdevice.h"
#include "pvrsrv_device.h"
#endif


#define	HW_PERF_FILTER_DEFAULT         0x00000000 /* Default to no HWPerf */
#define HW_PERF_FILTER_DEFAULT_ALL_ON  0xFFFFFFFF /* All events */


#if defined(SUPPORT_KERNEL_SRVINIT) && defined(SUPPORT_VALIDATION)
#include "pvrsrv_apphint.h"
#endif

#if defined(SUPPORT_KERNEL_SRVINIT) && defined(LINUX)
#include "km_apphint.h"
#include "os_srvinit_param.h"
#else
#include "srvinit_param.h"
/*!
*******************************************************************************
 * AppHint mnemonic data type helper tables
******************************************************************************/
/* apphint map of name vs. enable flag */
static SRV_INIT_PARAM_UINT32_LOOKUP htb_loggroup_tbl[] = {
#define X(a, b) { #b, HTB_LOG_GROUP_FLAG(a) },
	HTB_LOG_SFGROUPLIST
#undef X
};
/* apphint map of arg vs. OpMode */
static SRV_INIT_PARAM_UINT32_LOOKUP htb_opmode_tbl[] = {
	{ "droplatest", HTB_OPMODE_DROPLATEST},
	{ "dropoldest", HTB_OPMODE_DROPOLDEST},
	/* HTB should never be started in HTB_OPMODE_BLOCK
	 * as this can lead to deadlocks
	 */
};

static SRV_INIT_PARAM_UINT32_LOOKUP fwt_logtype_tbl[] = {
	{ "trace", 2},
	{ "tbi", 1},
	{ "none", 0}
};

static SRV_INIT_PARAM_UINT32_LOOKUP timecorr_clk_tbl[] = {
	{ "mono", 0 },
	{ "mono_raw", 1 },
	{ "sched", 2 }
};

static SRV_INIT_PARAM_UINT32_LOOKUP fwt_loggroup_tbl[] = { RGXFWIF_LOG_GROUP_NAME_VALUE_MAP };

/*
 * Services AppHints initialisation
 */
#define X(a, b, c, d, e) SrvInitParamInit ## b( a, d, e )
APPHINT_LIST_ALL
#undef X
#endif /* SUPPORT_KERNEL_SRVINIT && LINUX */

/*
 * Container for all the apphints used by this module
 */
typedef struct _RGX_SRVINIT_APPHINTS_
{
	IMG_BOOL   bDustRequestInject;
	IMG_BOOL   bEnableSignatureChecks;
	IMG_UINT32 ui32SignatureChecksBufSize;

#if defined(DEBUG)
	IMG_BOOL   bAssertOnOutOfMem;
	IMG_BOOL   bAssertOnHWRTrigger;
#endif
	IMG_BOOL   bCheckMlist;
	IMG_BOOL   bDisableClockGating;
	IMG_BOOL   bDisableDMOverlap;
	IMG_BOOL   bDisableFEDLogging;
	IMG_BOOL   bDisablePDP;
	IMG_BOOL   bEnableCDMKillRand;
	IMG_BOOL   bEnableFTrace;
	IMG_BOOL   bEnableHWPerf;
	IMG_BOOL   bEnableHWPerfHost;
	IMG_BOOL   bEnableHWR;
	IMG_BOOL   bEnableRTUBypass;
	IMG_BOOL   bFilteringMode;
	IMG_BOOL   bHWPerfDisableCustomCounterFilter;
	IMG_BOOL   bZeroFreelist;
	IMG_UINT32 ui32EnableFWContextSwitch;
	IMG_UINT32 ui32FWContextSwitchProfile;
	IMG_UINT32 ui32HWPerfFWBufSize;
	IMG_UINT32 ui32HWPerfHostBufSize;
	IMG_UINT32 ui32HWPerfFilter0;
	IMG_UINT32 ui32HWPerfFilter1;
	IMG_UINT32 ui32HWPerfHostFilter;
	IMG_UINT32 ui32TimeCorrClock;
	IMG_UINT32 ui32HWRDebugDumpLimit;
	IMG_UINT32 ui32JonesDisableMask;
	IMG_UINT32 ui32LogType;
	IMG_UINT32 ui32TruncateMode;
	FW_PERF_CONF eFirmwarePerf;
	RGX_ACTIVEPM_CONF eRGXActivePMConf;
	RGX_META_T1_CONF eUseMETAT1;
	RGX_RD_POWER_ISLAND_CONF eRGXRDPowerIslandConf;

#if defined(SUPPORT_GPUVIRT_VALIDATION)
	IMG_UINT32 aui32OSidMin[GPUVIRT_VALIDATION_NUM_OS][GPUVIRT_VALIDATION_NUM_REGIONS];
	IMG_UINT32 aui32OSidMax[GPUVIRT_VALIDATION_NUM_OS][GPUVIRT_VALIDATION_NUM_REGIONS];
#endif
	IMG_BOOL   bEnableTrustedDeviceAceConfig;
} RGX_SRVINIT_APPHINTS;


/*!
*******************************************************************************

 @Function      GetApphints

 @Description   Read init time apphints and initialise internal variables

 @Input         psHints : Pointer to apphints container

 @Return        void

******************************************************************************/
static INLINE void GetApphints(RGX_SRVINIT_APPHINTS *psHints, IMG_UINT64 ui64ErnsBrns, IMG_UINT64 ui64Features)
{
	void *pvParamState = SrvInitParamOpen();
	IMG_UINT32 ui32ParamTemp;
	IMG_BOOL bS7TopInfra = IMG_FALSE, bE42290 = IMG_FALSE, bTPUFiltermodeCtrl = IMG_FALSE, \
			bE41805 = IMG_FALSE, bE42606 = IMG_FALSE, bAXIACELite = IMG_FALSE;

#if defined(PVRSRV_GPUVIRT_GUESTDRV)
	PVR_UNREFERENCED_PARAMETER(bE41805);
	PVR_UNREFERENCED_PARAMETER(bE42606);
	PVR_UNREFERENCED_PARAMETER(bE42290);
	PVR_UNREFERENCED_PARAMETER(bS7TopInfra);
	PVR_UNREFERENCED_PARAMETER(bTPUFiltermodeCtrl);
#endif

#if !defined(SUPPORT_KERNEL_SRVINIT)
	PVR_UNREFERENCED_PARAMETER(ui64ErnsBrns);
	PVR_UNREFERENCED_PARAMETER(ui64Features);
	PVR_UNREFERENCED_PARAMETER(bAXIACELite);
#if defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE)
	bS7TopInfra = IMG_TRUE;
#endif
#if defined(HW_ERN_42290)
	bE42290 = IMG_TRUE;
#endif
#if defined(HW_ERN_41805)
	bE41805 = IMG_TRUE;
#endif
#if defined(HW_ERN_42606)
	bE42606 = IMG_TRUE;
#endif
#if defined(RGX_FEATURE_AXI_ACELITE)
	bAXIACELite = IMG_TRUE;
#endif
#else
	if(ui64Features & RGX_FEATURE_S7_TOP_INFRASTRUCTURE_BIT_MASK)
	{
		bS7TopInfra = IMG_TRUE;
	}

	if(ui64Features & RGX_FEATURE_TPU_FILTERING_MODE_CONTROL_BIT_MASK)
	{
		bTPUFiltermodeCtrl = IMG_TRUE;
	}

	if(ui64ErnsBrns & HW_ERN_42290_BIT_MASK)
	{
		bE42290 = IMG_TRUE;
	}

	if(ui64ErnsBrns & HW_ERN_41805_BIT_MASK)
	{
		bE41805 = IMG_TRUE;
	}

	if(ui64ErnsBrns & HW_ERN_42606_BIT_MASK)
	{
		bE42606 = IMG_TRUE;
	}

	if(ui64Features & RGX_FEATURE_AXI_ACELITE_BIT_MASK)
	{
		bAXIACELite = IMG_TRUE;
	}
#endif
	/*
	 * KM AppHints not passed through the srvinit interface
	 */
#if !defined(SUPPORT_KERNEL_SRVINIT)
	SrvInitParamUnreferenced(FWPoisonOnFreeValue);
	SrvInitParamUnreferenced(EnableFWPoisonOnFree);
	SrvInitParamUnreferenced(GeneralNon4KHeapPageSize);
	SrvInitParamUnreferenced(WatchdogThreadWeight);
	SrvInitParamUnreferenced(WatchdogThreadPriority);
	SrvInitParamUnreferenced(CleanupThreadWeight);
	SrvInitParamUnreferenced(CleanupThreadPriority);
	SrvInitParamUnreferenced(RGXBVNC);
#endif

	/*
	 * NB AppHints initialised to a default value via SrvInitParamInit* macros above
	 */

	SrvInitParamGetBOOL(pvParamState,     DustRequestInject, psHints->bDustRequestInject);
	SrvInitParamGetBOOL(pvParamState,     EnableSignatureChecks, psHints->bEnableSignatureChecks);
	SrvInitParamGetUINT32(pvParamState,   SignatureChecksBufSize, psHints->ui32SignatureChecksBufSize);

#if defined(DEBUG)
	SrvInitParamGetBOOL(pvParamState,    AssertOutOfMemory, psHints->bAssertOnOutOfMem);
	SrvInitParamGetBOOL(pvParamState,    AssertOnHWRTrigger, psHints->bAssertOnHWRTrigger);
#endif
	SrvInitParamGetBOOL(pvParamState,    CheckMList, psHints->bCheckMlist);
	SrvInitParamGetBOOL(pvParamState,    DisableClockGating, psHints->bDisableClockGating);
	SrvInitParamGetBOOL(pvParamState,    DisableDMOverlap, psHints->bDisableDMOverlap);
	SrvInitParamGetBOOL(pvParamState,    DisableFEDLogging, psHints->bDisableFEDLogging);
	SrvInitParamGetUINT32(pvParamState,  EnableAPM, ui32ParamTemp);
	psHints->eRGXActivePMConf = ui32ParamTemp;
	SrvInitParamGetBOOL(pvParamState,    EnableCDMKillingRandMode, psHints->bEnableCDMKillRand);
	SrvInitParamGetBOOL(pvParamState,    EnableFTraceGPU, psHints->bEnableFTrace);
	SrvInitParamGetUINT32(pvParamState,  EnableFWContextSwitch, psHints->ui32EnableFWContextSwitch);
	SrvInitParamGetBOOL(pvParamState,    EnableHWPerf, psHints->bEnableHWPerf);
	SrvInitParamGetBOOL(pvParamState,    EnableHWPerfHost, psHints->bEnableHWPerfHost);
	SrvInitParamGetBOOL(pvParamState,    EnableHWR, psHints->bEnableHWR);
	SrvInitParamGetUINT32(pvParamState,  EnableRDPowerIsland, ui32ParamTemp);
	psHints->eRGXRDPowerIslandConf = ui32ParamTemp;
	SrvInitParamGetBOOL(pvParamState,    EnableRTUBypass, psHints->bEnableRTUBypass);
	SrvInitParamGetUINT32(pvParamState,  FirmwarePerf, ui32ParamTemp);
	psHints->eFirmwarePerf = ui32ParamTemp;
	SrvInitParamGetUINT32(pvParamState,  FWContextSwitchProfile, psHints->ui32FWContextSwitchProfile);
	SrvInitParamGetBOOL(pvParamState,    HWPerfDisableCustomCounterFilter, psHints->bHWPerfDisableCustomCounterFilter);
	SrvInitParamGetUINT32(pvParamState,  HWPerfHostBufSizeInKB, psHints->ui32HWPerfHostBufSize);
	SrvInitParamGetUINT32(pvParamState,  HWPerfFWBufSizeInKB, psHints->ui32HWPerfFWBufSize);
#if defined(SUPPORT_KERNEL_SRVINIT) && defined(LINUX)
	/* name changes */
	{
		IMG_UINT64 ui64Tmp;
		SrvInitParamGetBOOL(pvParamState,    DisablePDumpPanic, psHints->bDisablePDP);
		SrvInitParamGetUINT64(pvParamState,  HWPerfFWFilter, ui64Tmp);
		psHints->ui32HWPerfFilter0 = (IMG_UINT32)(ui64Tmp & 0xffffffffllu);
		psHints->ui32HWPerfFilter1 = (IMG_UINT32)((ui64Tmp >> 32) & 0xffffffffllu);
	}
#else
	SrvInitParamGetBOOL(pvParamState,    DisablePDP, psHints->bDisablePDP);
	SrvInitParamGetUINT32(pvParamState,  HWPerfFilter0, psHints->ui32HWPerfFilter0);
	SrvInitParamGetUINT32(pvParamState,  HWPerfFilter1, psHints->ui32HWPerfFilter1);
	SrvInitParamUnreferenced(DisablePDumpPanic);
	SrvInitParamUnreferenced(HWPerfFWFilter);
	SrvInitParamUnreferenced(RGXBVNC);
#endif
	SrvInitParamGetUINT32(pvParamState,  HWPerfHostFilter, psHints->ui32HWPerfHostFilter);
	SrvInitParamGetUINT32List(pvParamState,  TimeCorrClock, psHints->ui32TimeCorrClock);
	SrvInitParamGetUINT32(pvParamState,  HWRDebugDumpLimit, ui32ParamTemp);
	psHints->ui32HWRDebugDumpLimit = MIN(ui32ParamTemp, RGXFWIF_HWR_DEBUG_DUMP_ALL);

	if(bS7TopInfra)
	{
	#define RGX_CR_JONES_FIX_MT_ORDER_ISP_TE_CLRMSK	(0XFFFFFFCFU)
	#define RGX_CR_JONES_FIX_MT_ORDER_ISP_EN	(0X00000020U)
	#define RGX_CR_JONES_FIX_MT_ORDER_TE_EN		(0X00000010U)

		SrvInitParamGetUINT32(pvParamState,  JonesDisableMask, ui32ParamTemp);
		if (((ui32ParamTemp & ~RGX_CR_JONES_FIX_MT_ORDER_ISP_TE_CLRMSK) == RGX_CR_JONES_FIX_MT_ORDER_ISP_EN) ||
			((ui32ParamTemp & ~RGX_CR_JONES_FIX_MT_ORDER_ISP_TE_CLRMSK) == RGX_CR_JONES_FIX_MT_ORDER_TE_EN))
		{
			ui32ParamTemp |= (RGX_CR_JONES_FIX_MT_ORDER_TE_EN |
							  RGX_CR_JONES_FIX_MT_ORDER_ISP_EN);
			PVR_DPF((PVR_DBG_WARNING, "Tile reordering mode requires both TE and ISP enabled. Forcing JonesDisableMask = %d",
					ui32ParamTemp));
		}
		psHints->ui32JonesDisableMask = ui32ParamTemp;
	}

	if ( (bE42290) && (bTPUFiltermodeCtrl))
	{
		SrvInitParamGetBOOL(pvParamState,    NewFilteringMode, psHints->bFilteringMode);
	}

	if(bE41805 || bE42606)
	{
		SrvInitParamGetUINT32(pvParamState,  TruncateMode, psHints->ui32TruncateMode);
	}
#if defined(EMULATOR)
	if(bAXIACELite)
	{
		SrvInitParamGetBOOL(pvParamState, EnableTrustedDeviceAceConfig, psHints->bEnableTrustedDeviceAceConfig);
	}
#else
#if !defined(SUPPORT_KERNEL_SRVINIT)
	SrvInitParamUnreferenced(EnableTrustedDeviceAceConfig);
#endif
#endif	

	SrvInitParamGetUINT32(pvParamState,  UseMETAT1, ui32ParamTemp);
	psHints->eUseMETAT1 = ui32ParamTemp & RGXFWIF_INICFG_METAT1_MASK;

	SrvInitParamGetBOOL(pvParamState,    ZeroFreelist, psHints->bZeroFreelist);


	/*
	 * HWPerf filter apphints setup
	 */
	if (psHints->bEnableHWPerf)
	{
		if (psHints->ui32HWPerfFilter0 == 0 && psHints->ui32HWPerfFilter1 == 0)
		{
			psHints->ui32HWPerfFilter0 = HW_PERF_FILTER_DEFAULT_ALL_ON;
			psHints->ui32HWPerfFilter1 = HW_PERF_FILTER_DEFAULT_ALL_ON;
		}
	}
	else
	{
		if (psHints->ui32HWPerfFilter0 != 0 || psHints->ui32HWPerfFilter1 != 0)
		{
			psHints->bEnableHWPerf = IMG_TRUE;
		}
	}

#if defined(SUPPORT_GPUTRACE_EVENTS)
	if (psHints->bEnableFTrace)
	{
		/* In case we have not set EnableHWPerf AppHint just request creation
		 * of certain events we need for the FTrace i.e. only the Kick/Finish
		 * HW events */
		if (!psHints->bEnableHWPerf)
		{
			psHints->ui32HWPerfFilter0 = (IMG_UINT32) (RGX_HWPERF_EVENT_MASK_HW_KICKFINISH & 0xFFFFFFFF);
			psHints->ui32HWPerfFilter1 = (IMG_UINT32) ((RGX_HWPERF_EVENT_MASK_HW_KICKFINISH & 0xFFFFFFFF00000000) >> 32);
		}
		else
		{
			psHints->ui32HWPerfFilter0 = HW_PERF_FILTER_DEFAULT_ALL_ON;
			psHints->ui32HWPerfFilter1 = HW_PERF_FILTER_DEFAULT_ALL_ON;
		}

	}
#endif
	
	if (psHints->bEnableHWPerfHost)
	{
		if (psHints->ui32HWPerfHostFilter == 0)
		{
			psHints->ui32HWPerfHostFilter = HW_PERF_FILTER_DEFAULT_ALL_ON;
		}
	}
	else
	{
		if (psHints->ui32HWPerfHostFilter != 0)
		{
			psHints->bEnableHWPerfHost = IMG_TRUE;
		}
	}

	/*
	 * FW logs apphints
	 */
	{
		IMG_UINT32 ui32LogType;
		IMG_BOOL bFirmwareLogTypeConfigured, bAnyLogGroupConfigured;

		SrvInitParamGetUINT32BitField(pvParamState, EnableLogGroup, ui32LogType);
		bAnyLogGroupConfigured = ui32LogType ? IMG_TRUE : IMG_FALSE;
		bFirmwareLogTypeConfigured = SrvInitParamGetUINT32List(pvParamState, FirmwareLogType, ui32ParamTemp);

		if (bFirmwareLogTypeConfigured)
		{
			if (ui32ParamTemp == 2 /* TRACE */)
			{
				if (!bAnyLogGroupConfigured)
				{
					/* No groups configured - defaulting to MAIN group */
					ui32LogType |= RGXFWIF_LOG_TYPE_GROUP_MAIN;
				}
				ui32LogType |= RGXFWIF_LOG_TYPE_TRACE;
			}
			else if (ui32ParamTemp == 1 /* TBI */)
			{
				if (!bAnyLogGroupConfigured)
				{
					/* No groups configured - defaulting to MAIN group */
					ui32LogType |= RGXFWIF_LOG_TYPE_GROUP_MAIN;
				}
				ui32LogType &= ~RGXFWIF_LOG_TYPE_TRACE;
			}
			else if (ui32ParamTemp == 0 /* NONE */)
			{
				ui32LogType = RGXFWIF_LOG_TYPE_NONE;
			}
		}
		else
		{
			/* No log type configured - defaulting to TRACE */
			ui32LogType |= RGXFWIF_LOG_TYPE_TRACE;
		}

		psHints->ui32LogType = ui32LogType;
	}

#if defined(SUPPORT_GPUVIRT_VALIDATION)
	/*
	 * GPU virtualisation validation apphints
	 */
	{
		IMG_UINT uiCounter, uiRegion;

		PVR_DPF((PVR_DBG_MESSAGE,"\n[GPU Virtualization Validation]: Reading OSid limits\n"));

		for (uiRegion = 0; uiRegion < GPUVIRT_VALIDATION_NUM_REGIONS; uiRegion++)
		{
			for (uiCounter = 0; uiCounter < GPUVIRT_VALIDATION_NUM_OS; uiCounter++)
			{
				IMG_CHAR   pszHintString[GPUVIRT_VALIDATION_MAX_STRING_LENGTH];
				IMG_UINT32 ui32Default = 0;

				snprintf(pszHintString, GPUVIRT_VALIDATION_MAX_STRING_LENGTH, "OSidRegion%dMin%d", uiRegion, uiCounter);
				PVRSRVGetAppHint(pvParamState,
				                 pszHintString,
				                 IMG_UINT_TYPE,
				                 &ui32Default,
				                 &(psHints->aui32OSidMin[uiCounter][uiRegion]));

				snprintf(pszHintString, GPUVIRT_VALIDATION_MAX_STRING_LENGTH, "OSidRegion%dMax%d", uiRegion, uiCounter);
				PVRSRVGetAppHint(pvParamState,
				                 pszHintString,
				                 IMG_UINT_TYPE,
				                 &ui32Default,
				                 &(psHints->aui32OSidMax[uiCounter][uiRegion]));
			}
		}

		for (uiCounter = 0; uiCounter < GPUVIRT_VALIDATION_NUM_OS; uiCounter++)
		{
			for (uiRegion = 0; uiRegion < GPUVIRT_VALIDATION_NUM_REGIONS; uiRegion++)
			{
				PVR_DPF((PVR_DBG_MESSAGE,
				         "\n[GPU Virtualization Validation]: Region:%d, OSid:%d, Min:%u, Max:%u\n",
				         uiRegion, uiCounter,
				         psHints->aui32OSidMin[uiCounter][uiRegion],
				         psHints->aui32OSidMax[uiCounter][uiRegion]));
			}
		}
	}
#endif /* defined(SUPPORT_GPUVIRT_VALIDATION) */


	SrvInitParamClose(pvParamState);
}


/*!
*******************************************************************************

 @Function      GetFWConfigFlags

 @Description   Initialise and return FW config flags

 @Input         psHints            : Apphints container
 @Input         pui32FWConfigFlags : Pointer to config flags

 @Return        void

******************************************************************************/
static INLINE void GetFWConfigFlags(RGX_SRVINIT_APPHINTS *psHints,
                                    IMG_UINT32 *pui32FWConfigFlags)
{
	IMG_UINT32 ui32FWConfigFlags = 0;

#if defined(DEBUG)
	ui32FWConfigFlags |= psHints->bAssertOnOutOfMem ? RGXFWIF_INICFG_ASSERT_ON_OUTOFMEMORY : 0;
	ui32FWConfigFlags |= psHints->bAssertOnHWRTrigger ? RGXFWIF_INICFG_ASSERT_ON_HWR_TRIGGER : 0;
#endif
	ui32FWConfigFlags |= psHints->bCheckMlist ? RGXFWIF_INICFG_CHECK_MLIST_EN : 0;
	ui32FWConfigFlags |= psHints->bDisableClockGating ? RGXFWIF_INICFG_DISABLE_CLKGATING_EN : 0;
	ui32FWConfigFlags |= psHints->bDisableDMOverlap ? RGXFWIF_INICFG_DISABLE_DM_OVERLAP : 0;
	ui32FWConfigFlags |= psHints->bDisablePDP ? RGXFWIF_SRVCFG_DISABLE_PDP_EN : 0;
	ui32FWConfigFlags |= psHints->bEnableCDMKillRand ? RGXFWIF_INICFG_CDM_KILL_MODE_RAND_EN : 0;
#if defined(SUPPORT_GPUTRACE_EVENTS)
	/* Since FTrace GPU events depends on HWPerf, ensure it is enabled here */
	ui32FWConfigFlags |= psHints->bEnableFTrace ? RGXFWIF_INICFG_HWPERF_EN : 0;
#endif
	ui32FWConfigFlags |= psHints->bEnableHWPerf ? RGXFWIF_INICFG_HWPERF_EN : 0;
#if !defined(NO_HARDWARE)
	ui32FWConfigFlags |= psHints->bEnableHWR ? RGXFWIF_INICFG_HWR_EN : 0;
#endif
	ui32FWConfigFlags |= psHints->bEnableRTUBypass ? RGXFWIF_INICFG_RTU_BYPASS_EN : 0;
	ui32FWConfigFlags |= psHints->bHWPerfDisableCustomCounterFilter ? RGXFWIF_INICFG_HWP_DISABLE_FILTER : 0;
	ui32FWConfigFlags |= (psHints->eFirmwarePerf == FW_PERF_CONF_CUSTOM_TIMER) ? RGXFWIF_INICFG_CUSTOM_PERF_TIMER_EN : 0;
	ui32FWConfigFlags |= (psHints->eFirmwarePerf == FW_PERF_CONF_POLLS) ? RGXFWIF_INICFG_POLL_COUNTERS_EN : 0;
	ui32FWConfigFlags |= psHints->eUseMETAT1 << RGXFWIF_INICFG_METAT1_SHIFT;
	ui32FWConfigFlags |= psHints->ui32EnableFWContextSwitch & ~RGXFWIF_INICFG_CTXSWITCH_CLRMSK;
	ui32FWConfigFlags |= (psHints->ui32FWContextSwitchProfile << RGXFWIF_INICFG_CTXSWITCH_PROFILE_SHIFT) & RGXFWIF_INICFG_CTXSWITCH_PROFILE_MASK;

	*pui32FWConfigFlags = ui32FWConfigFlags;
}


/*!
*******************************************************************************

 @Function      GetFilterFlags

 @Description   Initialise and return filter flags

 @Input         psHints : Apphints container

 @Return        Filter flags

******************************************************************************/
static INLINE IMG_UINT32 GetFilterFlags(RGX_SRVINIT_APPHINTS *psHints)
{
	IMG_UINT32 ui32FilterFlags = 0;

	ui32FilterFlags |= psHints->bFilteringMode ? RGXFWIF_FILTCFG_NEW_FILTER_MODE : 0;
	if (psHints->ui32TruncateMode == 2)
	{
		ui32FilterFlags |= RGXFWIF_FILTCFG_TRUNCATE_INT;
	}
	else if (psHints->ui32TruncateMode == 3)
	{
		ui32FilterFlags |= RGXFWIF_FILTCFG_TRUNCATE_HALF;
	}

	return ui32FilterFlags;
}


/*!
*******************************************************************************

 @Function      GetDeviceFlags

 @Description   Initialise and return device flags

 @Input         psHints          : Apphints container
 @Input         pui32DeviceFlags : Pointer to device flags

 @Return        void

******************************************************************************/
static INLINE void GetDeviceFlags(RGX_SRVINIT_APPHINTS *psHints,
                                  IMG_UINT32 *pui32DeviceFlags)
{
	IMG_UINT32 ui32DeviceFlags = 0;

	ui32DeviceFlags |= psHints->bDustRequestInject? RGXKMIF_DEVICE_STATE_DUST_REQUEST_INJECT_EN : 0;

	ui32DeviceFlags |= psHints->bZeroFreelist ? RGXKMIF_DEVICE_STATE_ZERO_FREELIST : 0;
	ui32DeviceFlags |= psHints->bDisableFEDLogging ? RGXKMIF_DEVICE_STATE_DISABLE_DW_LOGGING_EN : 0;
	ui32DeviceFlags |= psHints->bEnableHWPerfHost ? RGXKMIF_DEVICE_STATE_HWPERF_HOST_EN : 0;
#if defined(SUPPORT_GPUTRACE_EVENTS)
	ui32DeviceFlags |= psHints->bEnableFTrace ? RGXKMIF_DEVICE_STATE_FTRACE_EN : 0;
#endif

	*pui32DeviceFlags = ui32DeviceFlags;
}


/*!
*******************************************************************************

 @Function		PrepareDebugScript

 @Description	Generates a script to dump debug info

 @Input			psScript

 @Return		IMG_BOOL True if it runs out of cmds when building the script

******************************************************************************/
static IMG_BOOL PrepareDebugScript(RGX_SCRIPT_BUILD* psDbgInitScript,
					IMG_BOOL bFirmwarePerf,
					void *pvDeviceInfo)
{
#define DBG_READ(T, R, S)		if (!ScriptDBGReadRGXReg(psDbgInitScript, T, R, S)) return IMG_FALSE;
#if defined(RGX_FEATURE_META) || defined(SUPPORT_KERNEL_SRVINIT)
#define DBG_MSP_READ(R, S)		if (!ScriptDBGReadMetaRegThroughSP(psDbgInitScript, R, S)) return IMG_FALSE;
#define DBG_MCR_READ(R, S)		if (!ScriptDBGReadMetaCoreReg(psDbgInitScript, R, S)) return IMG_FALSE;
#else
#define DBG_MSP_READ(R, S)
#define DBG_MCR_READ(R, S)
#endif
#define DBG_CALC(R, S, T, U, V)	if (!ScriptDBGCalc(psDbgInitScript, R, S, T, U, V)) return IMG_FALSE;
#define DBG_STRING(S)			if (!ScriptDBGString(psDbgInitScript, S)) return IMG_FALSE;
#define DBG_READ32(R, S)				DBG_READ(RGX_INIT_OP_DBG_READ32_HW_REG, R, S)
#define DBG_READ64(R, S)				DBG_READ(RGX_INIT_OP_DBG_READ64_HW_REG, R, S)
#define DBG_CALC_TA_AND_3D(R, S, T, U)	DBG_CALC(RGX_INIT_OP_DBG_CALC, R, S, T, U)
	IMG_BOOL	bS7Infra, bXTInfra, e44871, bRayTracing, e47025, bVIVTSlc, bMIPS, bPBVNC;
	IMG_UINT32	ui32SLCBanks = 0, ui32Meta = 0;
#if !defined(SUPPORT_KERNEL_SRVINIT)
	PVR_UNREFERENCED_PARAMETER(pvDeviceInfo);
#else
	PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *)pvDeviceInfo;
#endif
	PVR_UNREFERENCED_PARAMETER(bFirmwarePerf);
	bS7Infra = bXTInfra = e44871 = bRayTracing = e47025 = bVIVTSlc = bMIPS = bPBVNC = IMG_FALSE;


#if !defined(SUPPORT_KERNEL_SRVINIT)
	#if defined(RGX_FEATURE_META)
		ui32Meta = RGX_FEATURE_META;
	#endif
	#if defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE)
		bS7Infra = IMG_TRUE;
	#endif

	#if defined(RGX_FEATURE_XT_TOP_INFRASTRUCTURE)
		bXTInfra = IMG_TRUE;
	#endif

	#if	defined(FIX_HW_BRN_44871)
		e44871 = IMG_TRUE;
	#endif

	#if	defined(HW_ERN_47025)
		e47025 = IMG_TRUE;
	#endif

	#if defined(RGX_FEATURE_RAY_TRACING)
		bRayTracing = IMG_TRUE;
	#endif

	#if defined(RGX_FEATURE_SLC_BANKS)
		ui32SLCBanks = RGX_FEATURE_SLC_BANKS;
	#endif

	#if defined(RGX_FEATURE_SLC_VIVT)
		bVIVTSlc = IMG_TRUE;
	#endif

	#if defined(RGX_FEATURE_MIPS)
		bMIPS = IMG_TRUE;
	#endif
	#if defined(RGX_FEATURE_PBVNC_COREID_REG)
		bPBVNC = IMG_TRUE;
	#endif
#else
	do{
		if(NULL == psDevInfo)
			break;

		if(psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_PBVNC_COREID_REG_BIT_MASK)
		{
			bPBVNC = IMG_TRUE;
		}

		if(psDevInfo->sDevFeatureCfg.ui32META)
		{
			ui32Meta = psDevInfo->sDevFeatureCfg.ui32META;
		}

		if(psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_S7_TOP_INFRASTRUCTURE_BIT_MASK)
		{
			bS7Infra = IMG_TRUE;
		}

		if(psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_XT_TOP_INFRASTRUCTURE_BIT_MASK)
		{
			bXTInfra = IMG_TRUE;
		}

		if(psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_RAY_TRACING_BIT_MASK)
		{
			bRayTracing = IMG_TRUE;
		}

		if(psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_SLC_VIVT_BIT_MASK)
		{
			bVIVTSlc = IMG_TRUE;
		}

		if(psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_MIPS_BIT_MASK)
		{
			bMIPS = IMG_TRUE;
		}


		if(psDevInfo->sDevFeatureCfg.ui32SLCBanks)
		{
			ui32SLCBanks = psDevInfo->sDevFeatureCfg.ui32SLCBanks;
		}

		if(psDevInfo->sDevFeatureCfg.ui64ErnsBrns & FIX_HW_BRN_44871_BIT_MASK)
		{
			e44871 = IMG_TRUE;
		}

		if(psDevInfo->sDevFeatureCfg.ui64ErnsBrns & HW_ERN_47025_POS)
		{
			e47025 = IMG_TRUE;
		}

	}while(0);
#endif

	if(bPBVNC)
	{
		DBG_READ64(RGX_CR_CORE_ID,							"CORE_ID                         ");
	}else
	{
		DBG_READ32(RGX_CR_CORE_ID,							"CORE_ID                         ");
	}

	DBG_READ32(RGX_CR_CORE_REVISION,					"CORE_REVISION                   ");
	DBG_READ32(RGX_CR_DESIGNER_REV_FIELD1,				"DESIGNER_REV_FIELD1             ");
	DBG_READ32(RGX_CR_DESIGNER_REV_FIELD2,				"DESIGNER_REV_FIELD2             ");
	DBG_READ64(RGX_CR_CHANGESET_NUMBER,					"CHANGESET_NUMBER                ");
	if(ui32Meta)
	{
		DBG_READ32(RGX_CR_META_SP_MSLVIRQSTATUS,			"META_SP_MSLVIRQSTATUS           ");
	}
	DBG_READ64(RGX_CR_CLK_CTRL,							"CLK_CTRL                        ");
	DBG_READ64(RGX_CR_CLK_STATUS,						"CLK_STATUS                      ");
	DBG_READ64(RGX_CR_CLK_CTRL2,						"CLK_CTRL2                       ");
	DBG_READ64(RGX_CR_CLK_STATUS2,						"CLK_STATUS2                     ");
	if (bS7Infra)
	{
		DBG_READ64(RGX_CR_CLK_XTPLUS_CTRL,					"CLK_XTPLUS_CTRL                 ");
		DBG_READ64(RGX_CR_CLK_XTPLUS_STATUS,				"CLK_XTPLUS_STATUS               ");
	}
	DBG_READ32(RGX_CR_EVENT_STATUS,						"EVENT_STATUS                    ");
	DBG_READ64(RGX_CR_TIMER,							"TIMER                           ");
	if (bS7Infra)
	{
		DBG_READ64(RGX_CR_MMU_FAULT_STATUS,					"MMU_FAULT_STATUS                ");
		DBG_READ64(RGX_CR_MMU_FAULT_STATUS_META,			"MMU_FAULT_STATUS_META           ");
	}
	else
	{
		DBG_READ32(RGX_CR_BIF_FAULT_BANK0_MMU_STATUS,		"BIF_FAULT_BANK0_MMU_STATUS      ");
		DBG_READ64(RGX_CR_BIF_FAULT_BANK0_REQ_STATUS,		"BIF_FAULT_BANK0_REQ_STATUS      ");
		DBG_READ32(RGX_CR_BIF_FAULT_BANK1_MMU_STATUS,		"BIF_FAULT_BANK1_MMU_STATUS      ");
		DBG_READ64(RGX_CR_BIF_FAULT_BANK1_REQ_STATUS,		"BIF_FAULT_BANK1_REQ_STATUS      ");
	}

	DBG_READ32(RGX_CR_BIF_MMU_STATUS,					"BIF_MMU_STATUS                  ");
	DBG_READ32(RGX_CR_BIF_MMU_ENTRY,					"BIF_MMU_ENTRY                   ");
	DBG_READ64(RGX_CR_BIF_MMU_ENTRY_STATUS,				"BIF_MMU_ENTRY_STATUS            ");
	if (bS7Infra)
	{
		DBG_READ32(RGX_CR_BIF_JONES_OUTSTANDING_READ,		"BIF_JONES_OUTSTANDING_READ      ");
		DBG_READ32(RGX_CR_BIF_BLACKPEARL_OUTSTANDING_READ,	"BIF_BLACKPEARL_OUTSTANDING_READ ");
		DBG_READ32(RGX_CR_BIF_DUST_OUTSTANDING_READ,		"BIF_DUST_OUTSTANDING_READ       ");
	}else
	{

		if (!bXTInfra)
		{
			DBG_READ32(RGX_CR_BIF_STATUS_MMU,					"BIF_STATUS_MMU                  ");
			DBG_READ32(RGX_CR_BIF_READS_EXT_STATUS,				"BIF_READS_EXT_STATUS            ");
			DBG_READ32(RGX_CR_BIF_READS_INT_STATUS,				"BIF_READS_INT_STATUS            ");
		}
		DBG_READ32(RGX_CR_BIFPM_STATUS_MMU,					"BIFPM_STATUS_MMU                ");
		DBG_READ32(RGX_CR_BIFPM_READS_EXT_STATUS,			"BIFPM_READS_EXT_STATUS          ");
		DBG_READ32(RGX_CR_BIFPM_READS_INT_STATUS,			"BIFPM_READS_INT_STATUS          ");
	}

	if(e44871)
	{
		DBG_STRING("Warning: BRN44871 is present");
	}

	if(e47025)
	{
		DBG_READ64(RGX_CR_CDM_CONTEXT_LOAD_PDS0,			"CDM_CONTEXT_LOAD_PDS0           ");
		DBG_READ64(RGX_CR_CDM_CONTEXT_LOAD_PDS1,			"CDM_CONTEXT_LOAD_PDS1           ");
	}

	DBG_READ32(RGX_CR_SLC_STATUS0,						"SLC_STATUS0                     ");
	DBG_READ64(RGX_CR_SLC_STATUS1,						"SLC_STATUS1                     ");

	if (ui32SLCBanks)
	{
		DBG_READ64(RGX_CR_SLC_STATUS2,						"SLC_STATUS2                     ");
	}

	if (bVIVTSlc)
	{
		DBG_READ64(RGX_CR_CONTEXT_MAPPING0,					"CONTEXT_MAPPING0                ");
		DBG_READ64(RGX_CR_CONTEXT_MAPPING1,					"CONTEXT_MAPPING1                ");
		DBG_READ64(RGX_CR_CONTEXT_MAPPING2,					"CONTEXT_MAPPING2                ");
		DBG_READ64(RGX_CR_CONTEXT_MAPPING3,					"CONTEXT_MAPPING3                ");
		DBG_READ64(RGX_CR_CONTEXT_MAPPING4,					"CONTEXT_MAPPING4                ");
	}else{
		DBG_READ64(RGX_CR_BIF_CAT_BASE_INDEX,				"BIF_CAT_BASE_INDEX              ");
		DBG_READ64(RGX_CR_BIF_CAT_BASE0,					"BIF_CAT_BASE0                   ");
		DBG_READ64(RGX_CR_BIF_CAT_BASE1,					"BIF_CAT_BASE1                   ");
		DBG_READ64(RGX_CR_BIF_CAT_BASE2,					"BIF_CAT_BASE2                   ");
		DBG_READ64(RGX_CR_BIF_CAT_BASE3,					"BIF_CAT_BASE3                   ");
		DBG_READ64(RGX_CR_BIF_CAT_BASE4,					"BIF_CAT_BASE4                   ");
		DBG_READ64(RGX_CR_BIF_CAT_BASE5,					"BIF_CAT_BASE5                   ");
		DBG_READ64(RGX_CR_BIF_CAT_BASE6,					"BIF_CAT_BASE6                   ");
		DBG_READ64(RGX_CR_BIF_CAT_BASE7,					"BIF_CAT_BASE7                   ");
	}

	DBG_READ32(RGX_CR_BIF_CTRL_INVAL,					"BIF_CTRL_INVAL                  ");
	DBG_READ32(RGX_CR_BIF_CTRL,							"BIF_CTRL                        ");

	DBG_READ64(RGX_CR_BIF_PM_CAT_BASE_VCE0,				"BIF_PM_CAT_BASE_VCE0            ");
	DBG_READ64(RGX_CR_BIF_PM_CAT_BASE_TE0,				"BIF_PM_CAT_BASE_TE0             ");
	DBG_READ64(RGX_CR_BIF_PM_CAT_BASE_ALIST0,			"BIF_PM_CAT_BASE_ALIST0          ");
	DBG_READ64(RGX_CR_BIF_PM_CAT_BASE_VCE1,				"BIF_PM_CAT_BASE_VCE1            ");
	DBG_READ64(RGX_CR_BIF_PM_CAT_BASE_TE1,				"BIF_PM_CAT_BASE_TE1             ");
	DBG_READ64(RGX_CR_BIF_PM_CAT_BASE_ALIST1,			"BIF_PM_CAT_BASE_ALIST1          ");
	
	DBG_READ32(RGX_CR_PERF_TA_PHASE,					"PERF_TA_PHASE                   ");
	DBG_READ32(RGX_CR_PERF_TA_CYCLE,					"PERF_TA_CYCLE                   ");
	DBG_READ32(RGX_CR_PERF_3D_PHASE,					"PERF_3D_PHASE                   ");
	DBG_READ32(RGX_CR_PERF_3D_CYCLE,					"PERF_3D_CYCLE                   ");

	DBG_READ32(RGX_CR_PERF_TA_OR_3D_CYCLE,				"PERF_TA_OR_3D_CYCLE             ");
	DBG_CALC_TA_AND_3D(RGX_CR_PERF_TA_CYCLE, RGX_CR_PERF_3D_CYCLE, RGX_CR_PERF_TA_OR_3D_CYCLE,
														"PERF_TA_AND_3D_CYCLE            ");

	DBG_READ32(RGX_CR_PERF_COMPUTE_PHASE,				"PERF_COMPUTE_PHASE              ");
	DBG_READ32(RGX_CR_PERF_COMPUTE_CYCLE,				"PERF_COMPUTE_CYCLE              ");

	DBG_READ32(RGX_CR_PM_PARTIAL_RENDER_ENABLE,			"PARTIAL_RENDER_ENABLE           ");

	DBG_READ32(RGX_CR_ISP_RENDER,						"ISP_RENDER                      ");
	DBG_READ64(RGX_CR_TLA_STATUS,						"TLA_STATUS                      ");
	DBG_READ64(RGX_CR_MCU_FENCE,						"MCU_FENCE                       ");

	DBG_READ32(RGX_CR_VDM_CONTEXT_STORE_STATUS,			"VDM_CONTEXT_STORE_STATUS        ");
	DBG_READ64(RGX_CR_VDM_CONTEXT_STORE_TASK0,			"VDM_CONTEXT_STORE_TASK0         ");
	DBG_READ64(RGX_CR_VDM_CONTEXT_STORE_TASK1,			"VDM_CONTEXT_STORE_TASK1         ");
	DBG_READ64(RGX_CR_VDM_CONTEXT_STORE_TASK2,			"VDM_CONTEXT_STORE_TASK2         ");
	DBG_READ64(RGX_CR_VDM_CONTEXT_RESUME_TASK0,			"VDM_CONTEXT_RESUME_TASK0        ");
	DBG_READ64(RGX_CR_VDM_CONTEXT_RESUME_TASK1,			"VDM_CONTEXT_RESUME_TASK1        ");
	DBG_READ64(RGX_CR_VDM_CONTEXT_RESUME_TASK2,			"VDM_CONTEXT_RESUME_TASK2        ");

	DBG_READ32(RGX_CR_ISP_CTL,							"ISP_CTL                         ");
	DBG_READ32(RGX_CR_ISP_STATUS,						"ISP_STATUS                      ");
	DBG_READ32(RGX_CR_MTS_INTCTX,						"MTS_INTCTX                      ");
	DBG_READ32(RGX_CR_MTS_BGCTX,						"MTS_BGCTX                       ");
	DBG_READ32(RGX_CR_MTS_BGCTX_COUNTED_SCHEDULE,		"MTS_BGCTX_COUNTED_SCHEDULE      ");
	DBG_READ32(RGX_CR_MTS_SCHEDULE,						"MTS_SCHEDULE                    ");
	DBG_READ32(RGX_CR_MTS_GPU_INT_STATUS,				"MTS_GPU_INT_STATUS              ");

	DBG_READ32(RGX_CR_CDM_CONTEXT_STORE_STATUS,			"CDM_CONTEXT_STORE_STATUS        ");
	DBG_READ64(RGX_CR_CDM_CONTEXT_PDS0,					"CDM_CONTEXT_PDS0                ");
	DBG_READ64(RGX_CR_CDM_CONTEXT_PDS1,					"CDM_CONTEXT_PDS1                ");
	DBG_READ64(RGX_CR_CDM_TERMINATE_PDS,				"CDM_TERMINATE_PDS               ");
	DBG_READ64(RGX_CR_CDM_TERMINATE_PDS1,				"CDM_TERMINATE_PDS1              ");

	if(e47025)
	{
		DBG_READ64(RGX_CR_CDM_CONTEXT_LOAD_PDS0,			"CDM_CONTEXT_LOAD_PDS0           ");
		DBG_READ64(RGX_CR_CDM_CONTEXT_LOAD_PDS1,			"CDM_CONTEXT_LOAD_PDS1           ");
	}

	if(bRayTracing)
	{
#if defined(RGX_FEATURE_RAY_TRACING) || defined(SUPPORT_KERNEL_SRVINIT)
		DBG_READ32(DPX_CR_BIF_MMU_STATUS,					"DPX_CR_BIF_MMU_STATUS           ");
		DBG_READ64(DPX_CR_BIF_FAULT_BANK_MMU_STATUS,		"DPX_CR_BIF_FAULT_BANK_MMU_STATUS");
		DBG_READ64(DPX_CR_BIF_FAULT_BANK_REQ_STATUS,		"DPX_CR_BIF_FAULT_BANK_REQ_STATUS");

		DBG_READ64(RGX_CR_RPM_SHF_FPL,						"RGX_CR_RPM_SHF_FPL	             ");
		DBG_READ32(RGX_CR_RPM_SHF_FPL_READ,					"RGX_CR_RPM_SHF_FPL_READ         ");
		DBG_READ32(RGX_CR_RPM_SHF_FPL_WRITE,				"RGX_CR_RPM_SHF_FPL_WRITE        ");
		DBG_READ64(RGX_CR_RPM_SHG_FPL,   					"RGX_CR_RPM_SHG_FPL	             ");
		DBG_READ32(RGX_CR_RPM_SHG_FPL_READ,					"RGX_CR_RPM_SHG_FPL_READ         ");
		DBG_READ32(RGX_CR_RPM_SHG_FPL_WRITE,				"RGX_CR_RPM_SHG_FPL_WRITE        ");
#endif
	}

	if (bS7Infra)
	{
		DBG_READ32(RGX_CR_JONES_IDLE,						"JONES_IDLE                      ");
	}

	DBG_READ32(RGX_CR_SIDEKICK_IDLE,					"SIDEKICK_IDLE                   ");
	if (!bS7Infra)
	{
		DBG_READ32(RGX_CR_SLC_IDLE,							"SLC_IDLE                        ");
	}else
	{
		DBG_READ32(RGX_CR_SLC3_IDLE,						"SLC3_IDLE                       ");
		DBG_READ64(RGX_CR_SLC3_STATUS,						"SLC3_STATUS                     ");
		DBG_READ32(RGX_CR_SLC3_FAULT_STOP_STATUS,			"SLC3_FAULT_STOP_STATUS          ");
	}

	if (ui32Meta)
	{
		DBG_MSP_READ(META_CR_T0ENABLE_OFFSET,				"T0 TXENABLE                     ");
		DBG_MSP_READ(META_CR_T0STATUS_OFFSET,				"T0 TXSTATUS                     ");
		DBG_MSP_READ(META_CR_T0DEFR_OFFSET,					"T0 TXDEFR                       ");
		DBG_MCR_READ(META_CR_THR0_PC,						"T0 PC                           ");
		DBG_MCR_READ(META_CR_THR0_PCX,						"T0 PCX                          ");
		DBG_MCR_READ(META_CR_THR0_SP,						"T0 SP                           ");
	}

	if ((ui32Meta == MTP218) || (ui32Meta == MTP219))
	{
		DBG_MSP_READ(META_CR_T1ENABLE_OFFSET,				"T1 TXENABLE                     ");
		DBG_MSP_READ(META_CR_T1STATUS_OFFSET,				"T1 TXSTATUS                     ");
		DBG_MSP_READ(META_CR_T1DEFR_OFFSET,					"T1 TXDEFR                       ");
		DBG_MCR_READ(META_CR_THR1_PC,						"T1 PC                           ");
		DBG_MCR_READ(META_CR_THR1_PCX,						"T1 PCX                          ");
		DBG_MCR_READ(META_CR_THR1_SP,						"T1 SP                           ");
	}

	if (bFirmwarePerf)
	{
		DBG_MSP_READ(META_CR_PERF_COUNT0,				"PERF_COUNT0                     ");
		DBG_MSP_READ(META_CR_PERF_COUNT1,				"PERF_COUNT1                     ");
	}

	if (bMIPS)
	{
		DBG_READ32(RGX_CR_MIPS_EXCEPTION_STATUS,            "MIPS_EXCEPTION_STATUS           ");
	}

	return IMG_TRUE;
}


#if defined(SUPPORT_TRUSTED_DEVICE) && !defined(NO_HARDWARE)
/*************************************************************************/ /*!
 @Function       RGXTDProcessFWImage

 @Description    Fetch and send data used by the trusted device to complete
                 the FW image setup

 @Input          psDeviceNode - Device node
 @Input          psRGXFW      - Firmware blob

 @Return         PVRSRV_ERROR
*/ /**************************************************************************/
static PVRSRV_ERROR RGXTDProcessFWImage(PVRSRV_DEVICE_NODE *psDeviceNode,
                                        struct RGXFW *psRGXFW)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig = psDeviceNode->psDevConfig;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_TD_FW_PARAMS sTDFWParams;
	PVRSRV_ERROR eError;

	if (psDevConfig->pfnTDSendFWImage == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXTDProcessFWImage: TDProcessFWImage not implemented!"));
		return PVRSRV_ERROR_NOT_IMPLEMENTED;
	}

	sTDFWParams.pvFirmware = RGXFirmwareData(psRGXFW);
	sTDFWParams.ui32FirmwareSize = RGXFirmwareSize(psRGXFW);
	sTDFWParams.sFWCodeDevVAddrBase = psDevInfo->sFWCodeDevVAddrBase;
	sTDFWParams.sFWDataDevVAddrBase = psDevInfo->sFWDataDevVAddrBase;
	sTDFWParams.sFWCorememCodeFWAddr = psDevInfo->sFWCorememCodeFWAddr;
	sTDFWParams.sFWInitFWAddr = psDevInfo->sFWInitFWAddr;

	eError = psDevConfig->pfnTDSendFWImage(psDevConfig->hSysData, &sTDFWParams);

	return eError;
}
#endif

/*!
*******************************************************************************

 @Function     AcquireHostData

 @Description  Acquire Device MemDesc and CPU pointer for a given PMR

 @Input        hServices      : Services connection
 @Input        hPMR           : PMR
 @Output       ppsHostMemDesc : Returned MemDesc
 @Output       ppvHostAddr    : Returned CPU pointer

 @Return       PVRSRV_ERROR

******************************************************************************/
static INLINE
PVRSRV_ERROR AcquireHostData(SHARED_DEV_CONNECTION hServices,
                             IMG_HANDLE hPMR,
                             DEVMEM_MEMDESC **ppsHostMemDesc,
                             void **ppvHostAddr)
{
	IMG_HANDLE hImportHandle;
	IMG_DEVMEM_SIZE_T uiImportSize;
	PVRSRV_ERROR eError;

	eError = DevmemMakeLocalImportHandle(hServices,
	                                     hPMR,
	                                     &hImportHandle);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "AcquireHostData: DevmemMakeLocalImportHandle failed (%d)", eError));
		goto acquire_failmakehandle;
	}

	eError = DevmemLocalImport(hServices,
	                           hImportHandle,
	                           PVRSRV_MEMALLOCFLAG_CPU_READABLE |
	                           PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
	                           PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT |
	                           PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE,
	                           ppsHostMemDesc,
	                           &uiImportSize,
	                           "AcquireHostData");
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "AcquireHostData: DevmemLocalImport failed (%d)", eError));
		goto acquire_failimport;
	}

	eError = DevmemAcquireCpuVirtAddr(*ppsHostMemDesc,
	                                  ppvHostAddr);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "AcquireHostData: DevmemAcquireCpuVirtAddr failed (%d)", eError));
		goto acquire_failcpuaddr;
	}

	/* We don't need the import handle anymore */
	DevmemUnmakeLocalImportHandle(hServices, hImportHandle);

	return PVRSRV_OK;


acquire_failcpuaddr:
	DevmemFree(*ppsHostMemDesc);

acquire_failimport:
	DevmemUnmakeLocalImportHandle(hServices, hImportHandle);

acquire_failmakehandle:
	return eError;
}

/*!
*******************************************************************************

 @Function     ReleaseHostData

 @Description  Releases resources associated with a Device MemDesc

 @Input        psHostMemDesc : MemDesc to free

 @Return       PVRSRV_ERROR

******************************************************************************/
static INLINE void ReleaseHostData(DEVMEM_MEMDESC *psHostMemDesc)
{
	DevmemReleaseCpuVirtAddr(psHostMemDesc);
	DevmemFree(psHostMemDesc);
}

/*!
*******************************************************************************

 @Function     GetFirmwareBVNC

 @Description  Retrieves FW BVNC information from binary data

 @Input        psRGXFW : Firmware binary handle to get BVNC from

 @Output       psRGXFWBVNC : structure store BVNC info

 @Return       IMG_TRUE upon success, IMG_FALSE otherwise

******************************************************************************/
static INLINE IMG_BOOL GetFirmwareBVNC(struct RGXFW *psRGXFW,
                                       RGXFWIF_COMPCHECKS_BVNC *psFWBVNC)
{
#if defined(LINUX)
	const size_t FWSize = RGXFirmwareSize(psRGXFW);
	const RGXFWIF_COMPCHECKS_BVNC * psBinBVNC;
#endif

#if !defined(LINUX)
	/* Check not available in non linux OSes. Just fill the struct and return true */
	psFWBVNC->ui32LayoutVersion = RGXFWIF_COMPCHECKS_LAYOUT_VERSION;
	psFWBVNC->ui32VLenMax = RGXFWIF_COMPCHECKS_BVNC_V_LEN_MAX;
#if !defined(SUPPORT_KERNEL_SRVINIT)
	rgx_bvnc_packed(&psFWBVNC->ui64BNC, psFWBVNC->aszV, psFWBVNC->ui32VLenMax,
	                RGX_BNC_B, RGX_BVNC_V_ST, RGX_BNC_N, RGX_BNC_C);
#else
	rgx_bvnc_packed(&psFWBVNC->ui64BNC, psFWBVNC->aszV, psFWBVNC->ui32VLenMax,
	                RGX_BNC_KM_B, RGX_BVNC_KM_V_ST, RGX_BNC_KM_N, RGX_BNC_KM_C);
#endif /* SUPPORT_KERNEL_SRVINIT */

#else

	if (FWSize < FW_BVNC_BACKWARDS_OFFSET)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Firmware is too small (%zu bytes)",
		         __func__, FWSize));
		return IMG_FALSE;
	}

	psBinBVNC = (RGXFWIF_COMPCHECKS_BVNC *) ((IMG_UINT8 *) (RGXFirmwareData(psRGXFW)) +
	                                         (FWSize - FW_BVNC_BACKWARDS_OFFSET));

	psFWBVNC->ui32LayoutVersion = RGX_INT32_FROM_BE(psBinBVNC->ui32LayoutVersion);

	psFWBVNC->ui32VLenMax = RGX_INT32_FROM_BE(psBinBVNC->ui32VLenMax);

	psFWBVNC->ui64BNC = RGX_INT64_FROM_BE(psBinBVNC->ui64BNC);

	strncpy(psFWBVNC->aszV, psBinBVNC->aszV, sizeof(psFWBVNC->aszV));
#endif /* defined(LINUX) */

	return IMG_TRUE;
}


/*!
*******************************************************************************

 @Function     RGXInitFirmwareBridgeWrapper

 @Description  Calls the proper RGXInitFirmware bridge version

 @Return       PVRSRV_ERROR

******************************************************************************/
static INLINE PVRSRV_ERROR RGXInitFirmwareBridgeWrapper(SHARED_DEV_CONNECTION    hServices,
                                                        RGXFWIF_DEV_VIRTADDR     *psRGXFwInit,
                                                        IMG_BOOL                 bEnableSignatureChecks,
                                                        IMG_UINT32               ui32SignatureChecksBufSize,
                                                        IMG_UINT32               ui32HWPerfFWBufSizeKB,
                                                        IMG_UINT64               ui64HWPerfFilter,
                                                        IMG_UINT32               ui32RGXFWAlignChecksArrLength,
                                                        IMG_UINT32               *pui32RGXFWAlignChecks,
                                                        IMG_UINT32               ui32FWConfigFlags,
                                                        IMG_UINT32               ui32LogType,
                                                        IMG_UINT32               ui32FilterFlags,
                                                        IMG_UINT32               ui32JonesDisableMask,
                                                        IMG_UINT32               ui32HWRDebugDumpLimit,
                                                        RGXFWIF_COMPCHECKS_BVNC  *psClientBVNC,
                                                        RGXFWIF_COMPCHECKS_BVNC  *psFirmwareBVNC,
                                                        IMG_UINT32               ui32HWPerfCountersDataSize,
                                                        IMG_HANDLE               *phHWPerfDataPMR,
                                                        RGX_RD_POWER_ISLAND_CONF eRGXRDPowerIslandConf,
                                                        FW_PERF_CONF             eFirmwarePerf)
{
	PVRSRV_ERROR eError;

	RGX_FW_INIT_IN_PARAMS sInParams = {
		RGXFWINITPARAMS_VERSION,
		bEnableSignatureChecks,
		ui32SignatureChecksBufSize,
		ui32HWPerfFWBufSizeKB,
		ui64HWPerfFilter,
		ui32FWConfigFlags,
		ui32LogType,
		ui32FilterFlags,
		ui32JonesDisableMask,
		ui32HWRDebugDumpLimit,
		{ 0 },
		{ 0 },
		ui32HWPerfCountersDataSize,
		eRGXRDPowerIslandConf,
		eFirmwarePerf,
		{ 0 }
	};

	memcpy(&(sInParams.sClientBVNC), psClientBVNC, sizeof (sInParams.sClientBVNC));
	memcpy(&(sInParams.sFirmwareBVNC), psFirmwareBVNC, sizeof (sInParams.sFirmwareBVNC));


	eError = BridgeRGXInitFirmwareExtended(hServices, ui32RGXFWAlignChecksArrLength,
	                                       pui32RGXFWAlignChecks, psRGXFwInit, phHWPerfDataPMR, &sInParams);

	/* Error calling the bridge could be due to old KM not implementing the extended version */
	if ((eError == PVRSRV_ERROR_BRIDGE_CALL_FAILED)
	    || (eError == PVRSRV_ERROR_BRIDGE_EINVAL))
	{
		eError = BridgeRGXInitFirmware(hServices,
		                               psRGXFwInit,
		                               bEnableSignatureChecks,
		                               ui32SignatureChecksBufSize,
		                               ui32HWPerfFWBufSizeKB,
		                               ui64HWPerfFilter,
		                               ui32RGXFWAlignChecksArrLength,
		                               pui32RGXFWAlignChecks,
		                               ui32FWConfigFlags,
		                               ui32LogType,
		                               ui32FilterFlags,
		                               ui32JonesDisableMask,
		                               ui32HWRDebugDumpLimit,
		                               psClientBVNC,
		                               ui32HWPerfCountersDataSize,
		                               phHWPerfDataPMR,
		                               eRGXRDPowerIslandConf,
		                               eFirmwarePerf);
	}

	return eError;
}

/*!
*******************************************************************************

 @Function     InitFirmware

 @Description  Allocate, initialise and pdump Firmware code and data memory

 @Input        hServices       : Services connection
 @Input        psHints         : Apphints
 @Input        psBVNC          : Compatibility checks
 @Output       phFWCodePMR     : FW code PMR handle
 @Output       phFWDataPMR     : FW data PMR handle
 @Output       phFWCorememPMR  : FW coremem code PMR handle
 @Output       phHWPerfDataPMR : HWPerf control PMR handle

 @Return       PVRSRV_ERROR

******************************************************************************/
static PVRSRV_ERROR InitFirmware(SHARED_DEV_CONNECTION hServices,
                                 RGX_SRVINIT_APPHINTS *psHints,
                                 RGXFWIF_COMPCHECKS_BVNC *psBVNC,
                                 IMG_HANDLE *phFWCodePMR,
                                 IMG_HANDLE *phFWDataPMR,
                                 IMG_HANDLE *phFWCorememPMR,
                                 IMG_HANDLE *phHWPerfDataPMR)
{
	struct RGXFW      *psRGXFW = NULL;
	const IMG_BYTE    *pbRGXFirmware = NULL;
	RGXFWIF_COMPCHECKS_BVNC sFWBVNC;

	/* FW code memory */
	IMG_DEVMEM_SIZE_T uiFWCodeAllocSize;
	IMG_DEV_VIRTADDR  sFWCodeDevVAddrBase;
	DEVMEM_MEMDESC    *psFWCodeHostMemDesc;
	void              *pvFWCodeHostAddr;

	/* FW data memory */
	IMG_DEVMEM_SIZE_T uiFWDataAllocSize;
	IMG_DEV_VIRTADDR  sFWDataDevVAddrBase;
	DEVMEM_MEMDESC    *psFWDataHostMemDesc;
	void              *pvFWDataHostAddr;

	/* FW coremem code memory */
	IMG_DEVMEM_SIZE_T uiFWCorememCodeAllocSize;
	IMG_DEV_VIRTADDR  sFWCorememDevVAddrBase;

	/* 
	 * Only declare psFWCorememHostMemDesc where used (PVR_UNREFERENCED_PARAMETER doesn't
	 * help for local vars when using certain compilers)
	 */
	DEVMEM_MEMDESC    *psFWCorememHostMemDesc;
	void              *pvFWCorememHostAddr = NULL;

	RGXFWIF_DEV_VIRTADDR sFWCorememFWAddr; /* FW coremem data */
	RGXFWIF_DEV_VIRTADDR sRGXFwInit;       /* FW init struct */
	RGX_INIT_LAYER_PARAMS sInitParams;
#if !defined(SUPPORT_KERNEL_SRVINIT)
	IMG_UINT32 aui32RGXFWAlignChecks[] = {RGXFW_ALIGN_CHECKS_INIT};
#endif
	IMG_UINT32 ui32FWConfigFlags;
	PVRSRV_ERROR eError;
	IMG_CHAR *pszFWFilename = NULL;
	IMG_CHAR *pszFWpFilename = NULL;
#if defined(SUPPORT_KERNEL_SRVINIT)
	IMG_CHAR aszFWFilenameStr[OSStringLength(RGX_FW_FILENAME)+MAX_BVNC_STRING_LEN+2];
	IMG_CHAR aszFWpFilenameStr[OSStringLength(RGX_FW_FILENAME)+MAX_BVNC_STRING_LEN+3];
	PVRSRV_DEVICE_NODE *psDeviceNode = (PVRSRV_DEVICE_NODE *)hServices;
	PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *)psDeviceNode->pvDevice;

	pszFWFilename = &aszFWFilenameStr[0];
	OSSNPrintf(pszFWFilename, OSStringLength(RGX_FW_FILENAME)+MAX_BVNC_STRING_LEN+2, "%s.%d.%d.%d.%d", RGX_FW_FILENAME,
	           psDevInfo->sDevFeatureCfg.ui32B, psDevInfo->sDevFeatureCfg.ui32V,
	           psDevInfo->sDevFeatureCfg.ui32N, psDevInfo->sDevFeatureCfg.ui32C);
	pszFWpFilename = &aszFWpFilenameStr[0];
	OSSNPrintf(pszFWpFilename, OSStringLength(RGX_FW_FILENAME)+MAX_BVNC_STRING_LEN+3, "%s.%d.%dp.%d.%d", RGX_FW_FILENAME,
	           psDevInfo->sDevFeatureCfg.ui32B, psDevInfo->sDevFeatureCfg.ui32V,
	           psDevInfo->sDevFeatureCfg.ui32N, psDevInfo->sDevFeatureCfg.ui32C);
#endif /* defined(SUPPORT_KERNEL_SRVINIT) */
#if !defined(PVRSRV_GPUVIRT_GUESTDRV)
	/*
	 * Get pointer to Firmware image
	 */

	psRGXFW = RGXLoadFirmware(hServices, pszFWFilename, pszFWpFilename);
	if (psRGXFW == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "InitFirmware: RGXLoadFirmware failed"));
		eError = PVRSRV_ERROR_INIT_FAILURE;
		goto cleanup_initfw;
	}
	pbRGXFirmware = RGXFirmwareData(psRGXFW);

	if (!GetFirmwareBVNC(psRGXFW, &sFWBVNC))
	{
		PVR_DPF((PVR_DBG_ERROR, "InitFirmware: RGXLoadFirmware failed to get Firmware BVNC"));
		eError = PVRSRV_ERROR_INIT_FAILURE;
		goto cleanup_initfw;
	}

	sInitParams.hServices = hServices;

	/*
	 * Allocate Firmware memory
	 */

	eError = RGXGetFWImageAllocSize(&sInitParams,
	                                &uiFWCodeAllocSize,
	                                &uiFWDataAllocSize,
	                                &uiFWCorememCodeAllocSize);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "InitFirmware: RGXGetFWImageAllocSize failed"));
		goto cleanup_initfw;
	}

#if defined(SUPPORT_TRUSTED_DEVICE)
	/* Disable META core memory allocation unless the META DMA is available */
#if defined(SUPPORT_KERNEL_SRVINIT)
	if (!RGXDeviceHasFeatureInit(&sInitParams, RGX_FEATURE_META_DMA_BIT_MASK))
	{
		uiFWCorememCodeAllocSize = 0;
	}
#elif !defined(RGX_FEATURE_META_DMA)
	uiFWCorememCodeAllocSize = 0;
#endif
#endif
#else
	PVR_UNREFERENCED_PARAMETER(pszFWFilename);
	PVR_UNREFERENCED_PARAMETER(pszFWpFilename);
	PVR_UNREFERENCED_PARAMETER(sInitParams);
	PVR_UNREFERENCED_PARAMETER(pbRGXFirmware);
	uiFWCodeAllocSize = 0;
	uiFWDataAllocSize = 0;
	uiFWCorememCodeAllocSize = 0;
#endif

	eError = BridgeRGXInitAllocFWImgMem(hServices,
	                                    uiFWCodeAllocSize,
	                                    uiFWDataAllocSize,
	                                    uiFWCorememCodeAllocSize,
	                                    phFWCodePMR,
	                                    &sFWCodeDevVAddrBase,
	                                    phFWDataPMR,
	                                    &sFWDataDevVAddrBase,
	                                    phFWCorememPMR,
	                                    &sFWCorememDevVAddrBase,
	                                    &sFWCorememFWAddr);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "InitFirmware: PVRSRVRGXInitAllocFWImgMem failed (%d)", eError));
		goto cleanup_initfw;
	}


	/*
	 * Setup Firmware initialisation data
	 */

	GetFWConfigFlags(psHints, &ui32FWConfigFlags);

	eError = RGXInitFirmwareBridgeWrapper(hServices,
	                                      &sRGXFwInit,
	                                      psHints->bEnableSignatureChecks,
	                                      psHints->ui32SignatureChecksBufSize,
	                                      psHints->ui32HWPerfFWBufSize,
	                                      (IMG_UINT64)psHints->ui32HWPerfFilter0 |
	                                      ((IMG_UINT64)psHints->ui32HWPerfFilter1 << 32),
#if defined(SUPPORT_KERNEL_SRVINIT)
	                                      0,
	                                      NULL,
#else
	                                      IMG_ARR_NUM_ELEMS(aui32RGXFWAlignChecks),
	                                      aui32RGXFWAlignChecks,
#endif
	                                      ui32FWConfigFlags,
	                                      psHints->ui32LogType,
	                                      GetFilterFlags(psHints),
	                                      psHints->ui32JonesDisableMask,
	                                      psHints->ui32HWRDebugDumpLimit,
	                                      psBVNC,
	                                      &sFWBVNC,
	                                      sizeof(RGXFWIF_HWPERF_CTL),
	                                      phHWPerfDataPMR,
	                                      psHints->eRGXRDPowerIslandConf,
	                                      psHints->eFirmwarePerf);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "InitFirmware: PVRSRVRGXInitFirmware failed (%d)", eError));
		goto cleanup_initfw;
	}
#if defined(PVRSRV_GPUVIRT_GUESTDRV)
	PVR_UNREFERENCED_PARAMETER(pvFWCorememHostAddr);
	PVR_UNREFERENCED_PARAMETER(psFWCorememHostMemDesc);
	PVR_UNREFERENCED_PARAMETER(pvFWDataHostAddr);
	PVR_UNREFERENCED_PARAMETER(psFWDataHostMemDesc);
	PVR_UNREFERENCED_PARAMETER(pvFWCodeHostAddr);
	PVR_UNREFERENCED_PARAMETER(psFWCodeHostMemDesc);
#else
	/*
	 * Acquire pointers to Firmware allocations
	 */

#if !defined(SUPPORT_TRUSTED_DEVICE) || defined(NO_HARDWARE)
	eError = AcquireHostData(hServices,
	                         *phFWCodePMR,
	                         &psFWCodeHostMemDesc,
	                         &pvFWCodeHostAddr);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "InitFirmware: AcquireHostData for FW code failed (%d)", eError));
		goto release_code;
	}
#else
	PVR_UNREFERENCED_PARAMETER(psFWCodeHostMemDesc);

	/* We can't get a pointer to a secure FW allocation from within the DDK */
	pvFWCodeHostAddr = NULL;
#endif

	eError = AcquireHostData(hServices,
	                         *phFWDataPMR,
	                         &psFWDataHostMemDesc,
	                         &pvFWDataHostAddr);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "InitFirmware: AcquireHostData for FW data failed (%d)", eError));
		goto release_data;
	}

#if !defined(SUPPORT_TRUSTED_DEVICE) || defined(NO_HARDWARE)
	if (uiFWCorememCodeAllocSize)
	{
		eError = AcquireHostData(hServices,
								 *phFWCorememPMR,
								 &psFWCorememHostMemDesc,
								 &pvFWCorememHostAddr);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "InitFirmware: AcquireHostData for FW coremem code failed (%d)", eError));
			goto release_corememcode;
		}
	}
#else
	PVR_UNREFERENCED_PARAMETER(psFWCorememHostMemDesc);

	/* We can't get a pointer to a secure FW allocation from within the DDK */
	pvFWCorememHostAddr = NULL;
#endif


	/*
	 * Process the Firmware image and setup code and data segments.
	 *
	 * When the trusted device is enabled and the FW code lives
	 * in secure memory we will only setup the data segments here,
	 * while the code segments will be loaded to secure memory
	 * by the trusted device.
	 */

	eError = RGXProcessFWImage(&sInitParams,
	                           pbRGXFirmware,
	                           pvFWCodeHostAddr,
	                           pvFWDataHostAddr,
	                           pvFWCorememHostAddr,
	                           &sFWCodeDevVAddrBase,
	                           &sFWDataDevVAddrBase,
	                           &sFWCorememDevVAddrBase,
	                           &sFWCorememFWAddr,
	                           &sRGXFwInit,
#if defined(RGXFW_META_SUPPORT_2ND_THREAD)
	                           2,
#else
	                           psHints->eUseMETAT1 == RGX_META_T1_OFF ? 1 : 2,
#endif
	                           psHints->eUseMETAT1 == RGX_META_T1_MAIN ? 1 : 0);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "InitFirmware: RGXProcessFWImage failed (%d)", eError));
		goto release_fw_allocations;
	}

#if defined(SUPPORT_TRUSTED_DEVICE) && !defined(NO_HARDWARE)
	RGXTDProcessFWImage(hServices, psRGXFW);
#endif


	/*
	 * Perform final steps (if any) on the kernel
	 * before pdumping the Firmware allocations
	 */
	eError = BridgeRGXInitFinaliseFWImage(hServices);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "InitFirmware: RGXInitFinaliseFWImage failed (%d)", eError));
		goto release_fw_allocations;
	}

	/*
	 * PDump Firmware allocations
	 */

#if !defined(SUPPORT_TRUSTED_DEVICE) || defined(NO_HARDWARE)
	SRVINITPDumpComment(hServices, "Dump firmware code image");
	DevmemPDumpLoadMem(psFWCodeHostMemDesc,
	                   0,
	                   uiFWCodeAllocSize,
	                   PDUMP_FLAGS_CONTINUOUS);
#endif

	SRVINITPDumpComment(hServices, "Dump firmware data image");
	DevmemPDumpLoadMem(psFWDataHostMemDesc,
	                   0,
	                   uiFWDataAllocSize,
	                   PDUMP_FLAGS_CONTINUOUS);

#if !defined(SUPPORT_TRUSTED_DEVICE) || defined(NO_HARDWARE)
	if (uiFWCorememCodeAllocSize)
	{
		SRVINITPDumpComment(hServices, "Dump firmware coremem image");
		DevmemPDumpLoadMem(psFWCorememHostMemDesc,
						   0,
						   uiFWCorememCodeAllocSize,
						   PDUMP_FLAGS_CONTINUOUS);
	}
#endif


	/*
	 * Release Firmware allocations and clean up
	 */

release_fw_allocations:
#if !defined(SUPPORT_TRUSTED_DEVICE) || defined(NO_HARDWARE)
release_corememcode:
	if (uiFWCorememCodeAllocSize)
	{
		ReleaseHostData(psFWCorememHostMemDesc);
	}
#endif

release_data:
	ReleaseHostData(psFWDataHostMemDesc);

#if !defined(SUPPORT_TRUSTED_DEVICE) || defined(NO_HARDWARE)
release_code:
	ReleaseHostData(psFWCodeHostMemDesc);
#endif
#endif /* PVRSRV_GPUVIRT_GUESTDRV */
cleanup_initfw:
	if (psRGXFW != NULL)
	{
		RGXUnloadFirmware(psRGXFW);
	}

	return eError;
}


#if defined(PDUMP)
/*!
*******************************************************************************

 @Function	InitialiseHWPerfCounters

 @Description

 Initialisation of hardware performance counters and dumping them out to pdump, so that they can be modified at a later point.

 @Input hServices

 @Input psHWPerfDataMemDesc

 @Input psHWPerfInitDataInt

 @Return  void

******************************************************************************/

static void InitialiseHWPerfCounters(SHARED_DEV_CONNECTION hServices, DEVMEM_MEMDESC *psHWPerfDataMemDesc, RGXFWIF_HWPERF_CTL *psHWPerfInitDataInt)
{
	RGXFWIF_HWPERF_CTL_BLK *psHWPerfInitBlkData;
	IMG_UINT32 ui32CntBlkModelLen;
	const RGXFW_HWPERF_CNTBLK_TYPE_MODEL *asCntBlkTypeModel;
	const RGXFW_HWPERF_CNTBLK_TYPE_MODEL* psBlkTypeDesc;
	IMG_UINT32 ui32BlockID, ui32BlkCfgIdx, ui32CounterIdx ;
	void *pvDev = NULL;	// Use SHARED_DEV_CONNECTION here?
	RGX_HWPERF_CNTBLK_RT_INFO sCntBlkRtInfo;

#if defined(SUPPORT_KERNEL_SRVINIT)
	{
		PVRSRV_DEVICE_NODE *psDeviceNode = (PVRSRV_DEVICE_NODE *)hServices;
		pvDev = psDeviceNode->pvDevice;
	}
#endif
	ui32CntBlkModelLen = RGXGetHWPerfBlockConfig(&asCntBlkTypeModel);
	for(ui32BlkCfgIdx = 0; ui32BlkCfgIdx < ui32CntBlkModelLen; ui32BlkCfgIdx++)
	{
		/* Exit early if this core does not have any of these counter blocks
		 * due to core type/BVNC features.... */
		psBlkTypeDesc = &asCntBlkTypeModel[ui32BlkCfgIdx];
		if (psBlkTypeDesc->pfnIsBlkPresent(psBlkTypeDesc, pvDev, &sCntBlkRtInfo) == IMG_FALSE)
		{
			continue;
		}

		/* Program all counters in one block so those already on may
		 * be configured off and vice-a-versa. */
		for (ui32BlockID = psBlkTypeDesc->uiCntBlkIdBase;
					 ui32BlockID < psBlkTypeDesc->uiCntBlkIdBase+sCntBlkRtInfo.uiNumUnits;
					 ui32BlockID++)
		{

			SRVINITPDumpComment(hServices, "Unit %d Block : %s", ui32BlockID-psBlkTypeDesc->uiCntBlkIdBase, psBlkTypeDesc->pszBlockNameComment);
			/* Get the block configure store to update from the global store of
			 * block configuration. This is used to remember the configuration
			 * between configurations and core power on in APM */
			psHWPerfInitBlkData = rgxfw_hwperf_get_block_ctl(ui32BlockID, psHWPerfInitDataInt);
			/* Assert to check for HWPerf block mis-configuration */
			PVR_ASSERT(psHWPerfInitBlkData);

			psHWPerfInitBlkData->bValid = IMG_TRUE;	
			SRVINITPDumpComment(hServices, "bValid: This specifies if the layout block is valid for the given BVNC.");
			DevmemPDumpLoadMemValue32(psHWPerfDataMemDesc,
							(size_t)&(psHWPerfInitBlkData->bValid) - (size_t)(psHWPerfInitDataInt),
							psHWPerfInitBlkData->bValid,
							PDUMP_FLAGS_CONTINUOUS);

			psHWPerfInitBlkData->bEnabled = IMG_FALSE;
			SRVINITPDumpComment(hServices, "bEnabled: Set to 0x1 if the block needs to be enabled during playback. ");
			DevmemPDumpLoadMemValue32(psHWPerfDataMemDesc,
							(size_t)&(psHWPerfInitBlkData->bEnabled) - (size_t)(psHWPerfInitDataInt),
							psHWPerfInitBlkData->bEnabled,
							PDUMP_FLAGS_CONTINUOUS);

			psHWPerfInitBlkData->eBlockID = ui32BlockID;
			SRVINITPDumpComment(hServices, "eBlockID: The Block ID for the layout block. See RGX_HWPERF_CNTBLK_ID for further information.");
			DevmemPDumpLoadMemValue32(psHWPerfDataMemDesc,
							(size_t)&(psHWPerfInitBlkData->eBlockID) - (size_t)(psHWPerfInitDataInt),
							psHWPerfInitBlkData->eBlockID,
							PDUMP_FLAGS_CONTINUOUS);

			psHWPerfInitBlkData->uiCounterMask = 0x00;
			SRVINITPDumpComment(hServices, "uiCounterMask: Bitmask for selecting the counters that need to be configured.(Bit 0 - counter0, bit 1 - counter1 and so on. ");
			DevmemPDumpLoadMemValue32(psHWPerfDataMemDesc,
							(size_t)&(psHWPerfInitBlkData->uiCounterMask) - (size_t)(psHWPerfInitDataInt),
							psHWPerfInitBlkData->uiCounterMask,
							PDUMP_FLAGS_CONTINUOUS);

			for(ui32CounterIdx = RGX_CNTBLK_COUNTER0_ID; ui32CounterIdx < psBlkTypeDesc->uiNumCounters; ui32CounterIdx++)
			{
				psHWPerfInitBlkData->aui64CounterCfg[ui32CounterIdx] = IMG_UINT64_C(0x0000000000000000);

				SRVINITPDumpComment(hServices, "%s_COUNTER_%d", psBlkTypeDesc->pszBlockNameComment,ui32CounterIdx);
				DevmemPDumpLoadMemValue64(psHWPerfDataMemDesc,
							(size_t)&(psHWPerfInitBlkData->aui64CounterCfg[ui32CounterIdx]) - (size_t)(psHWPerfInitDataInt),
							psHWPerfInitBlkData->aui64CounterCfg[ui32CounterIdx],
							PDUMP_FLAGS_CONTINUOUS);

			}
		}
	}
}
/*!
*******************************************************************************

 @Function	InitialiseCustomCounters

 @Description

 Initialisation of custom counters and dumping them out to pdump, so that they can be modified at a later point.

 @Input hServices

 @Input psHWPerfDataMemDesc

 @Return  void

******************************************************************************/

static void InitialiseCustomCounters(SHARED_DEV_CONNECTION hServices, DEVMEM_MEMDESC *psHWPerfDataMemDesc)
{
	IMG_UINT32 ui32CustomBlock, ui32CounterID;

	SRVINITPDumpComment(hServices, "ui32SelectedCountersBlockMask - The Bitmask of the custom counters that are to be selected");
	DevmemPDumpLoadMemValue32(psHWPerfDataMemDesc,
						offsetof(RGXFWIF_HWPERF_CTL, ui32SelectedCountersBlockMask),
						0,
						PDUMP_FLAGS_CONTINUOUS);

	for( ui32CustomBlock = 0; ui32CustomBlock < RGX_HWPERF_MAX_CUSTOM_BLKS; ui32CustomBlock++ )
	{
		/*
		 * Some compilers cannot cope with the use of offsetof() below - the specific problem being the use of
		 * a non-const variable in the expression, which it needs to be const. Typical compiler error produced is
		 * "expression must have a constant value".
		 */
		const IMG_DEVMEM_OFFSET_T uiOffsetOfCustomBlockSelectedCounters
		= (IMG_DEVMEM_OFFSET_T)(uintptr_t)&(((RGXFWIF_HWPERF_CTL *)0)->SelCntr[ui32CustomBlock].ui32NumSelectedCounters);

		SRVINITPDumpComment(hServices, "ui32NumSelectedCounters - The Number of counters selected for this Custom Block: %d",ui32CustomBlock );
		DevmemPDumpLoadMemValue32(psHWPerfDataMemDesc,
					uiOffsetOfCustomBlockSelectedCounters,
					0,
					PDUMP_FLAGS_CONTINUOUS);

		for(ui32CounterID = 0; ui32CounterID < RGX_HWPERF_MAX_CUSTOM_CNTRS; ui32CounterID++ )
		{
			const IMG_DEVMEM_OFFSET_T uiOffsetOfCustomBlockSelectedCounterIDs
			= (IMG_DEVMEM_OFFSET_T)(uintptr_t)&(((RGXFWIF_HWPERF_CTL *)0)->SelCntr[ui32CustomBlock].aui32SelectedCountersIDs[ui32CounterID]);

			SRVINITPDumpComment(hServices, "CUSTOMBLK_%d_COUNTERID_%d",ui32CustomBlock, ui32CounterID);
			DevmemPDumpLoadMemValue32(psHWPerfDataMemDesc,
					uiOffsetOfCustomBlockSelectedCounterIDs,
					0,
					PDUMP_FLAGS_CONTINUOUS);
		}
	}
}

/*!
*******************************************************************************

 @Function     InitialiseAllCounters

 @Description  Initialise HWPerf and custom counters

 @Input        hServices      : Services connection
 @Input        hHWPerfDataPMR : HWPerf control PMR handle

 @Return       PVRSRV_ERROR

******************************************************************************/
static PVRSRV_ERROR InitialiseAllCounters(SHARED_DEV_CONNECTION hServices,
                                          IMG_HANDLE hHWPerfDataPMR)
{
	RGXFWIF_HWPERF_CTL *psHWPerfInitData;
	DEVMEM_MEMDESC *psHWPerfDataMemDesc;
	PVRSRV_ERROR eError;

	eError = AcquireHostData(hServices,
	                         hHWPerfDataPMR,
	                         &psHWPerfDataMemDesc,
	                         (void **)&psHWPerfInitData);


	if (eError != PVRSRV_OK)
	{
		PVR_LOGG_IF_ERROR(eError, "DevmemAcquireCpuVirtAddr", failHWPerfCountersMemDescAqCpuVirt);
	}

	InitialiseHWPerfCounters(hServices, psHWPerfDataMemDesc, psHWPerfInitData);
	InitialiseCustomCounters(hServices, psHWPerfDataMemDesc);

failHWPerfCountersMemDescAqCpuVirt:
	ReleaseHostData(psHWPerfDataMemDesc);

	return eError;
}
#endif /* PDUMP */

static void
_ParseHTBAppHints(SHARED_DEV_CONNECTION hServices)
{
	PVRSRV_ERROR eError;
	void * pvParamState = NULL;
	IMG_UINT32 ui32LogType;
	IMG_BOOL bAnyLogGroupConfigured;

	IMG_CHAR * szBufferName = "PVRHTBuffer";
	IMG_UINT32 ui32BufferSize;
	HTB_OPMODE_CTRL eOpMode;

	/* Services initialisation parameters */
	pvParamState = SrvInitParamOpen();

	SrvInitParamGetUINT32BitField(pvParamState, EnableHTBLogGroup, ui32LogType);
	bAnyLogGroupConfigured = ui32LogType ? IMG_TRUE: IMG_FALSE;
	SrvInitParamGetUINT32List(pvParamState, HTBOperationMode, eOpMode);
	SrvInitParamGetUINT32(pvParamState, HTBufferSize, ui32BufferSize);

	eError = HTBConfigure(hServices, szBufferName, ui32BufferSize);
	PVR_LOGG_IF_ERROR(eError, "PVRSRVHTBConfigure", cleanup);

	if (bAnyLogGroupConfigured)
	{
		eError = HTBControl(hServices, 1, &ui32LogType, 0, 0, HTB_LOGMODE_ALLPID, eOpMode);
		PVR_LOGG_IF_ERROR(eError, "PVRSRVHTBControl", cleanup);
	}

cleanup:
	SrvInitParamClose(pvParamState);
}

#if defined(PDUMP) && defined(SUPPORT_KERNEL_SRVINIT) && defined(__KERNEL__)
static void RGXInitFWSigRegisters(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	IMG_UINT32	ui32PhantomCnt = 0;

	if (psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_CLUSTER_GROUPING_BIT_MASK)
	{
		ui32PhantomCnt = RGX_GET_NUM_PHANTOMS(psDevInfo->sDevFeatureCfg.ui32NumClusters) - 1;
	}

	/*Initialise the TA related signature registers */
	if(0 == gui32TASigRegCount)
	{
		if(psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_SCALABLE_VDM_GPP_BIT_MASK)
		{
			asTASigRegList[gui32TASigRegCount++] = 	(RGXFW_REGISTER_LIST){RGX_CR_USC_UVB_CHECKSUM, RGX_CR_BLACKPEARL_INDIRECT,0, ui32PhantomCnt};
		}else
		{
			asTASigRegList[gui32TASigRegCount++] = 	(RGXFW_REGISTER_LIST){RGX_CR_USC_UVS0_CHECKSUM, 0, 0, 0};
			asTASigRegList[gui32TASigRegCount++] = 	(RGXFW_REGISTER_LIST){RGX_CR_USC_UVS1_CHECKSUM, 0, 0, 0};
			asTASigRegList[gui32TASigRegCount++] = 	(RGXFW_REGISTER_LIST){RGX_CR_USC_UVS2_CHECKSUM, 0, 0, 0};
			asTASigRegList[gui32TASigRegCount++] = 	(RGXFW_REGISTER_LIST){RGX_CR_USC_UVS3_CHECKSUM, 0, 0, 0};
			asTASigRegList[gui32TASigRegCount++] = 	(RGXFW_REGISTER_LIST){RGX_CR_USC_UVS4_CHECKSUM, 0, 0, 0};
			asTASigRegList[gui32TASigRegCount++] = 	(RGXFW_REGISTER_LIST){RGX_CR_USC_UVS5_CHECKSUM, 0, 0, 0};
		}

		if(psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_SCALABLE_TE_ARCH_BIT_MASK)
		{
			if(psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_SCALABLE_VDM_GPP_BIT_MASK)
			{
				asTASigRegList[gui32TASigRegCount++] = 	(RGXFW_REGISTER_LIST){RGX_CR_PPP_CLIP_CHECKSUM, RGX_CR_BLACKPEARL_INDIRECT,0, ui32PhantomCnt};
			}else
			{
				asTASigRegList[gui32TASigRegCount++] = 	(RGXFW_REGISTER_LIST){RGX_CR_PPP, 0, 0, 0};
			}
			asTASigRegList[gui32TASigRegCount++] = 	(RGXFW_REGISTER_LIST){RGX_CR_TE_CHECKSUM,0, 0, 0};
		}else
		{
			asTASigRegList[gui32TASigRegCount++] = 	(RGXFW_REGISTER_LIST){RGX_CR_PPP_SIGNATURE, 0, 0, 0};
			asTASigRegList[gui32TASigRegCount++] = 	(RGXFW_REGISTER_LIST){RGX_CR_TE_SIGNATURE, 0, 0, 0};
		}

		asTASigRegList[gui32TASigRegCount++] = (RGXFW_REGISTER_LIST){RGX_CR_VCE_CHECKSUM, 0, 0, 0};

		if(0 == (psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_PDS_PER_DUST_BIT_MASK))
		{
			asTASigRegList[gui32TASigRegCount++] = 	(RGXFW_REGISTER_LIST){RGX_CR_PDS_DOUTM_STM_SIGNATURE,0, 0, 0};
		}
	}

	if(0 == gui323DSigRegCount)
	{
		/* List of 3D signature and checksum register addresses */
		if(0 == (psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_S7_TOP_INFRASTRUCTURE_BIT_MASK))
		{
			as3DSigRegList[gui323DSigRegCount++] = (RGXFW_REGISTER_LIST){RGX_CR_ISP_PDS_CHECKSUM,			0,							0, 0};
			as3DSigRegList[gui323DSigRegCount++] = (RGXFW_REGISTER_LIST){RGX_CR_ISP_TPF_CHECKSUM,			0,							0, 0};
			as3DSigRegList[gui323DSigRegCount++] = (RGXFW_REGISTER_LIST){RGX_CR_TFPU_PLANE0_CHECKSUM,		0,							0, 0};
			as3DSigRegList[gui323DSigRegCount++] = (RGXFW_REGISTER_LIST){RGX_CR_TFPU_PLANE1_CHECKSUM,		0,							0, 0};
			as3DSigRegList[gui323DSigRegCount++] = (RGXFW_REGISTER_LIST){RGX_CR_PBE_CHECKSUM,				0,							0, 0};
			as3DSigRegList[gui323DSigRegCount++] = (RGXFW_REGISTER_LIST){RGX_CR_IFPU_ISP_CHECKSUM,			0,							0, 0};
		}else
		{
			as3DSigRegList[gui323DSigRegCount++] = (RGXFW_REGISTER_LIST){RGX_CR_ISP_PDS_CHECKSUM,			RGX_CR_BLACKPEARL_INDIRECT,	0, ui32PhantomCnt};
			as3DSigRegList[gui323DSigRegCount++] = (RGXFW_REGISTER_LIST){RGX_CR_ISP_TPF_CHECKSUM,			RGX_CR_BLACKPEARL_INDIRECT,	0, ui32PhantomCnt};
			as3DSigRegList[gui323DSigRegCount++] = (RGXFW_REGISTER_LIST){RGX_CR_TFPU_PLANE0_CHECKSUM,		RGX_CR_BLACKPEARL_INDIRECT,	0, ui32PhantomCnt};
			as3DSigRegList[gui323DSigRegCount++] = (RGXFW_REGISTER_LIST){RGX_CR_TFPU_PLANE1_CHECKSUM,		RGX_CR_BLACKPEARL_INDIRECT,	0, ui32PhantomCnt};
			as3DSigRegList[gui323DSigRegCount++] = (RGXFW_REGISTER_LIST){RGX_CR_PBE_CHECKSUM,				RGX_CR_PBE_INDIRECT,		0, psDevInfo->sDevFeatureCfg.ui32NumClusters-1};
			as3DSigRegList[gui323DSigRegCount++] = (RGXFW_REGISTER_LIST){RGX_CR_IFPU_ISP_CHECKSUM,			RGX_CR_BLACKPEARL_INDIRECT,	0, ui32PhantomCnt};
		};

	}

}
#endif

/*!
*******************************************************************************

 @Function	RGXInit

 @Description

 RGX Initialisation

 @Input hServices

 @Return   PVRSRV_ERROR

******************************************************************************/
IMG_INTERNAL
PVRSRV_ERROR RGXInit(SHARED_DEV_CONNECTION hServices)
{
	PVRSRV_ERROR eError;
	RGXFWIF_COMPCHECKS_BVNC_DECLARE_AND_INIT(sBVNC);

	/* Services initialisation parameters */
	RGX_SRVINIT_APPHINTS sApphints = {0};
	IMG_UINT32 ui32DeviceFlags;
	IMG_UINT64	ui64ErnsBrns = 0, ui64Features = 0;

	/* Server scripts */
	RGX_SCRIPT_BUILD sDbgInitScript = {RGX_MAX_DEBUG_COMMANDS,  0, IMG_FALSE, asDbgCommands};

	/* FW allocations handles */
	IMG_HANDLE hFWCodePMR;
	IMG_HANDLE hFWDataPMR;
	IMG_HANDLE hFWCorememPMR;

	/* HWPerf Ctl allocation handle */
	IMG_HANDLE hHWPerfDataPMR;

#if defined(SUPPORT_KERNEL_SRVINIT)
	PVRSRV_DEVICE_NODE *psDeviceNode = (PVRSRV_DEVICE_NODE *)hServices;
	PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *)psDeviceNode->pvDevice;

	IMG_CHAR sV[RGXFWIF_COMPCHECKS_BVNC_V_LEN_MAX];

	OSSNPrintf(sV, sizeof(sV), "%d", psDevInfo->sDevFeatureCfg.ui32V);
	/*
	 * FIXME:
	 * Is this check redundant for the kernel mode version of srvinit?
	 * How do we check the user mode BVNC in this case?
	 */
	rgx_bvnc_packed(&sBVNC.ui64BNC, sBVNC.aszV, sBVNC.ui32VLenMax, psDevInfo->sDevFeatureCfg.ui32B, \
							sV,	\
							psDevInfo->sDevFeatureCfg.ui32N, psDevInfo->sDevFeatureCfg.ui32C);


	ui64ErnsBrns = psDevInfo->sDevFeatureCfg.ui64ErnsBrns;
	ui64Features = psDevInfo->sDevFeatureCfg.ui64Features;
#else
	rgx_bvnc_packed(&sBVNC.ui64BNC, sBVNC.aszV, sBVNC.ui32VLenMax, RGX_BVNC_B, RGX_BVNC_V_ST, RGX_BVNC_N, RGX_BVNC_C);
#endif

	/* Services initialisation parameters */
	_ParseHTBAppHints(hServices);
	GetApphints(&sApphints, ui64ErnsBrns, ui64Features);
	GetDeviceFlags(&sApphints, &ui32DeviceFlags);

#if defined(SUPPORT_GPUVIRT_VALIDATION)
{
	IMG_UINT    uiOS, uiRegion;
	IMG_UINT32  aui32Buffer[GPUVIRT_VALIDATION_NUM_OS * GPUVIRT_VALIDATION_NUM_REGIONS * 2]; /* The final 2 is 1 for Min and 1 for Max */
	IMG_UINT32  ui32Counter = 0;

	for (uiOS = 0; uiOS < GPUVIRT_VALIDATION_NUM_OS; uiOS++)
	{
		for (uiRegion = 0; uiRegion < GPUVIRT_VALIDATION_NUM_REGIONS; uiRegion++)
		{
			aui32Buffer[ui32Counter++] = sApphints.aui32OSidMin[uiOS][uiRegion];
			aui32Buffer[ui32Counter++] = sApphints.aui32OSidMax[uiOS][uiRegion];
		}
	}

	BridgeGPUVIRTPopulateLMASubArenas(hServices, ui32Counter, aui32Buffer, sApphints.bEnableTrustedDeviceAceConfig);
}
#endif


	eError = InitFirmware(hServices,
	                      &sApphints,
	                      &sBVNC,
	                      &hFWCodePMR,
	                      &hFWDataPMR,
	                      &hFWCorememPMR,
	                      &hHWPerfDataPMR);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXInit: InitFirmware failed (%d)", eError));
		goto cleanup;
	}

	/*
	 * Build Debug info script
	 */
	sDbgInitScript.psCommands = asDbgCommands;

#if defined(SUPPORT_KERNEL_SRVINIT)
	if(!PrepareDebugScript(&sDbgInitScript, sApphints.eFirmwarePerf != FW_PERF_CONF_NONE, psDevInfo))
#else
	if(!PrepareDebugScript(&sDbgInitScript, sApphints.eFirmwarePerf != FW_PERF_CONF_NONE, NULL))
#endif
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXInit: Run out of mem for the dbg commands"));
	}

	/* finish the script */
	if(!ScriptHalt(&sDbgInitScript))
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXInit: Run out of mem for the terminating dbg script"));
	}

#if defined(PDUMP)
	eError = InitialiseAllCounters(hServices, hHWPerfDataPMR);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXInit: InitialiseAllCounters failed (%d)", eError));
		goto cleanup;
	}
#endif

	/*
	 * Perform second stage of RGX initialisation
	 */
	eError = BridgeRGXInitDevPart2(hServices,
	                               sDbgInitScript.psCommands,
	                               ui32DeviceFlags,
	                               sApphints.ui32HWPerfHostBufSize,
	                               sApphints.ui32HWPerfHostFilter,
	                               sApphints.eRGXActivePMConf,
	                               hFWCodePMR,
	                               hFWDataPMR,
	                               hFWCorememPMR,
	                               hHWPerfDataPMR);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXInit: BridgeRGXInitDevPart2 failed (%d)", eError));
		goto cleanup;
	}

#if defined(SUPPORT_KERNEL_SRVINIT) && defined(SUPPORT_VALIDATION)
	PVRSRVAppHintDumpState();
#endif

#if defined(PDUMP)
	/*
	 * Dump the list of signature registers
	 */
	{
		IMG_UINT32 i;
		IMG_UINT32 ui32TASigRegCount = 0, ui323DSigRegCount= 0;
		IMG_BOOL	bRayTracing = IMG_FALSE;

#if defined(SUPPORT_KERNEL_SRVINIT) && defined(__KERNEL__)
		RGXInitFWSigRegisters(psDevInfo);
		ui32TASigRegCount = gui32TASigRegCount;
		ui323DSigRegCount = gui323DSigRegCount;
		if(psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_RAY_TRACING_BIT_MASK)
		{
			bRayTracing = IMG_TRUE;
		}
#if defined(DEBUG)
		if (gui32TASigRegCount > SIG_REG_TA_MAX_COUNT)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: TA signature registers max count exceeded",__func__));
			PVR_ASSERT(0);
		}
		if (gui323DSigRegCount > SIG_REG_3D_MAX_COUNT)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: 3D signature registers max count exceeded",__func__));
			PVR_ASSERT(0);
		}
#endif
#else
		ui32TASigRegCount = sizeof(asTASigRegList)/sizeof(RGXFW_REGISTER_LIST);
		ui323DSigRegCount = sizeof(as3DSigRegList)/sizeof(RGXFW_REGISTER_LIST);
#if defined(RGX_FEATURE_RAY_TRACING)
		bRayTracing = IMG_TRUE;
#endif
#endif



		SRVINITPDumpComment(hServices, "Signature TA registers: ");
		for (i = 0; i < ui32TASigRegCount; i++)
		{
			if (asTASigRegList[i].ui16IndirectRegNum != 0)
			{
				SRVINITPDumpComment(hServices, " * 0x%8.8X (indirect via 0x%8.8X %d to %d)",
				              asTASigRegList[i].ui16RegNum, asTASigRegList[i].ui16IndirectRegNum,
				              asTASigRegList[i].ui16IndirectStartVal, asTASigRegList[i].ui16IndirectEndVal);
			}
			else
			{
				SRVINITPDumpComment(hServices, " * 0x%8.8X", asTASigRegList[i].ui16RegNum);
			}
		}

		SRVINITPDumpComment(hServices, "Signature 3D registers: ");
		for (i = 0; i < ui323DSigRegCount; i++)
		{
			if (as3DSigRegList[i].ui16IndirectRegNum != 0)
			{
				SRVINITPDumpComment(hServices, " * 0x%8.8X (indirect via 0x%8.8X %d to %d)",
				              as3DSigRegList[i].ui16RegNum, as3DSigRegList[i].ui16IndirectRegNum,
				              as3DSigRegList[i].ui16IndirectStartVal, as3DSigRegList[i].ui16IndirectEndVal);
			}
			else
			{
				SRVINITPDumpComment(hServices, " * 0x%8.8X", as3DSigRegList[i].ui16RegNum);
			}
		}

		if(bRayTracing)
		{
#if defined (RGX_FEATURE_RAY_TRACING) || defined(SUPPORT_KERNEL_SRVINIT)
			SRVINITPDumpComment(hServices, "Signature RTU registers: ");
			for (i = 0; i < sizeof(asRTUSigRegList)/sizeof(RGXFW_REGISTER_LIST); i++)
			{
				if (asRTUSigRegList[i].ui16IndirectRegNum != 0)
				{
					SRVINITPDumpComment(hServices, " * 0x%8.8X (indirect via 0x%8.8X %d to %d)",
								  asRTUSigRegList[i].ui16RegNum, asRTUSigRegList[i].ui16IndirectRegNum,
								  asRTUSigRegList[i].ui16IndirectStartVal, asRTUSigRegList[i].ui16IndirectEndVal);
				}
				else
				{
					SRVINITPDumpComment(hServices, " * 0x%8.8X", asRTUSigRegList[i].ui16RegNum);
				}
			}

			SRVINITPDumpComment(hServices, "Signature SHG registers: ");
			for (i = 0; i < sizeof(asSHGSigRegList)/sizeof(RGXFW_REGISTER_LIST); i++)
			{
				if (asSHGSigRegList[i].ui16IndirectRegNum != 0)
				{
					SRVINITPDumpComment(hServices, " * 0x%8.8X (indirect via 0x%8.8X %d to %d)",
								  asSHGSigRegList[i].ui16RegNum, asSHGSigRegList[i].ui16IndirectRegNum,
								  asSHGSigRegList[i].ui16IndirectStartVal, asSHGSigRegList[i].ui16IndirectEndVal);
				}
				else
				{
					SRVINITPDumpComment(hServices, " * 0x%8.8X", asSHGSigRegList[i].ui16RegNum);
				}
			}
#endif
		}

	}
#endif	/* !defined(SUPPORT_KERNEL_SRVINIT) && defined(PDUMP) */

	eError = PVRSRV_OK;

cleanup:
	return eError;
}

/******************************************************************************
 End of file (rgxsrvinit.c)
******************************************************************************/
