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

#include "rgx_fwif_km.h"
#include "pdump_km.h"
#include "rgx_compat_bvnc.h"

#include "rgxdefs_km.h"
#include "pvrsrv.h"

#include "rgxinit.h"
#include "rgxmulticore.h"

#include "rgx_compat_bvnc.h"

#include "osfunc.h"

#include "rgxdefs_km.h"

#if defined(SUPPORT_GPUVIRT_VALIDATION)
#include "virt_validation_defs.h"
#endif

#include "rgx_fwif_hwperf.h"
#include "rgx_hwperf_table.h"

static const RGXFW_HWPERF_CNTBLK_TYPE_MODEL gasCntBlkTypeModel[] =
{
#define X(a, b, c, d, e, f, g)  {a, b, 0xFF, d, e, f, NULL}
RGX_CNT_BLK_TYPE_MODEL_DIRECT_LIST,
RGX_CNT_BLK_TYPE_MODEL_INDIRECT_LIST
#undef X
};

#include "fwload.h"
#include "rgxlayer_impl.h"
#include "rgxfwimageutils.h"
#include "rgxfwutils.h"

#include "rgx_bvnc_defs_km.h"

#include "rgxdevice.h"
#include "pvrsrv.h"

#if defined(SUPPORT_TRUSTED_DEVICE)
#include "rgxdevice.h"
#include "pvrsrv_device.h"
#endif

#define DRIVER_MODE_HOST               0          /* AppHint value for host driver mode */

#define	HW_PERF_FILTER_DEFAULT         0x00000000 /* Default to no HWPerf */
#define HW_PERF_FILTER_DEFAULT_ALL_ON  0xFFFFFFFF /* All events */
#define AVAIL_POW_UNITS_MASK_DEFAULT   (PVRSRV_APPHINT_HWVALAVAILABLESPUMASK)
#define AVAIL_RAC_MASK_DEFAULT         (PVRSRV_APPHINT_HWVALAVAILABLERACMASK)

/* Kernel CCB size */

#if !defined(PVRSRV_RGX_LOG2_KERNEL_CCB_MIN_SIZE)
#define PVRSRV_RGX_LOG2_KERNEL_CCB_MIN_SIZE 4
#endif
#if !defined(PVRSRV_RGX_LOG2_KERNEL_CCB_MAX_SIZE)
#define PVRSRV_RGX_LOG2_KERNEL_CCB_MAX_SIZE 16
#endif

#if PVRSRV_APPHINT_KCCB_SIZE_LOG2 < PVRSRV_RGX_LOG2_KERNEL_CCB_MIN_SIZE
#error PVRSRV_APPHINT_KCCB_SIZE_LOG2 is too low.
#elif PVRSRV_APPHINT_KCCB_SIZE_LOG2 > PVRSRV_RGX_LOG2_KERNEL_CCB_MAX_SIZE
#error PVRSRV_APPHINT_KCCB_SIZE_LOG2 is too high.
#endif

#if defined(SUPPORT_VALIDATION)
#include "pvrsrv_apphint.h"
#endif

#include "os_srvinit_param.h"

#if defined(__linux__)
#include "km_apphint.h"
#else
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
	{ "trace", 0},
	{ "none", 0}
#if defined(SUPPORT_TBI_INTERFACE)
	, { "tbi", 1}
#endif
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
#define X(a, b, c, d, e, f) SrvInitParamInit ## b(a, d, e)
APPHINT_LIST_ALL
#undef X
#endif /* defined(__linux__) */

/*
 * Container for all the apphints used by this module
 */
typedef struct _RGX_SRVINIT_APPHINTS_
{
	IMG_UINT32 ui32DriverMode;
	IMG_BOOL   bEnableSignatureChecks;
	IMG_UINT32 ui32SignatureChecksBufSize;

	IMG_BOOL   bAssertOnOutOfMem;
	IMG_BOOL   bAssertOnHWRTrigger;
#if defined(SUPPORT_VALIDATION)
	IMG_UINT32 ui32RenderKillingCtl;
	IMG_UINT32 ui32CDMTDMKillingCtl;
	IMG_BOOL   bValidateIrq;
	IMG_BOOL   bValidateSOCUSCTimer;
	IMG_UINT32 ui32AvailablePowUnitsMask;
	IMG_UINT32 ui32AvailableRACMask;
	IMG_BOOL   bInjectPowUnitsStateMaskChange;
	IMG_BOOL   bEnablePowUnitsStateMaskChange;
	IMG_UINT32 ui32FBCDCVersionOverride;
	IMG_UINT32 aui32TPUTrilinearFracMask[RGXFWIF_TPU_DM_LAST];
	IMG_UINT32 aui32USRMNumRegions[RGXFWIF_USRM_DM_LAST];
	IMG_UINT64 aui64UVBRMNumRegions[RGXFWIF_UVBRM_DM_LAST];
#endif
	IMG_BOOL   bCheckMlist;
	IMG_BOOL   bDisableClockGating;
	IMG_BOOL   bDisableDMOverlap;
	IMG_BOOL   bDisableFEDLogging;
	IMG_BOOL   bDisablePDP;
	IMG_BOOL   bEnableDMKillRand;
	IMG_BOOL   bEnableRandomCsw;
	IMG_BOOL   bEnableSoftResetCsw;
	IMG_BOOL   bFilteringMode;
	IMG_BOOL   bHWPerfDisableCounterFilter;
	IMG_BOOL   bZeroFreelist;
	IMG_UINT32 ui32EnableFWContextSwitch;
	IMG_UINT32 ui32FWContextSwitchProfile;
	IMG_UINT32 ui32ISPSchedulingLatencyMode;
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
	IMG_UINT32 ui32KCCBSizeLog2;
	IMG_UINT32 ui32CDMArbitrationMode;
	FW_PERF_CONF eFirmwarePerf;
	RGX_ACTIVEPM_CONF eRGXActivePMConf;
	RGX_RD_POWER_ISLAND_CONF eRGXRDPowerIslandConf;
	IMG_BOOL bSPUClockGating;

	IMG_BOOL   bEnableTrustedDeviceAceConfig;
	IMG_UINT32 ui32FWContextSwitchCrossDM;
#if defined(SUPPORT_PHYSMEM_TEST) && !defined(INTEGRITY_OS) && !defined(__QNXNTO__)
	IMG_UINT32 ui32PhysMemTestPasses;
#endif
} RGX_SRVINIT_APPHINTS;

/*!
*******************************************************************************

 @Function      GetApphints

 @Description   Read init time apphints and initialise internal variables

 @Input         psHints : Pointer to apphints container

 @Return        void

******************************************************************************/
static INLINE void GetApphints(PVRSRV_RGXDEV_INFO *psDevInfo, RGX_SRVINIT_APPHINTS *psHints)
{
	void *pvParamState = SrvInitParamOpen();
	IMG_UINT32 ui32ParamTemp;

	/*
	 * NB AppHints initialised to a default value via SrvInitParamInit* macros above
	 */
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,    DriverMode,                         psHints->ui32DriverMode);
	SrvInitParamGetBOOL(INITPARAM_NO_DEVICE,          pvParamState,    EnableSignatureChecks,      psHints->bEnableSignatureChecks);
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,    SignatureChecksBufSize, psHints->ui32SignatureChecksBufSize);

	SrvInitParamGetBOOL(psDevInfo->psDeviceNode,      pvParamState,    AssertOutOfMemory,               psHints->bAssertOnOutOfMem);
	SrvInitParamGetBOOL(psDevInfo->psDeviceNode,      pvParamState,    AssertOnHWRTrigger,            psHints->bAssertOnHWRTrigger);
	SrvInitParamGetBOOL(psDevInfo->psDeviceNode,      pvParamState,    CheckMList,                            psHints->bCheckMlist);
	SrvInitParamGetBOOL(INITPARAM_NO_DEVICE,          pvParamState,    DisableClockGating,            psHints->bDisableClockGating);
	SrvInitParamGetBOOL(INITPARAM_NO_DEVICE,          pvParamState,    DisableDMOverlap,                psHints->bDisableDMOverlap);
	SrvInitParamGetBOOL(psDevInfo->psDeviceNode,      pvParamState,    DisableFEDLogging,              psHints->bDisableFEDLogging);
	SrvInitParamGetUINT32(psDevInfo->psDeviceNode,    pvParamState,    EnableAPM,                                    ui32ParamTemp);
	psHints->eRGXActivePMConf = ui32ParamTemp;
	SrvInitParamGetBOOL(INITPARAM_NO_DEVICE,          pvParamState,    EnableGenericDMKillingRandMode,  psHints->bEnableDMKillRand);
	SrvInitParamGetBOOL(INITPARAM_NO_DEVICE,          pvParamState,    EnableRandomContextSwitch,        psHints->bEnableRandomCsw);
	SrvInitParamGetBOOL(INITPARAM_NO_DEVICE,          pvParamState,    EnableSoftResetContextSwitch,  psHints->bEnableSoftResetCsw);
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,    EnableFWContextSwitch,   psHints->ui32EnableFWContextSwitch);
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,    EnableRDPowerIsland,                          ui32ParamTemp);
	psHints->eRGXRDPowerIslandConf = ui32ParamTemp;
	SrvInitParamGetBOOL(INITPARAM_NO_DEVICE,		  pvParamState,    EnableSPUClockGating,              psHints->bSPUClockGating);
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,    FirmwarePerf,                                 ui32ParamTemp);
	psHints->eFirmwarePerf = ui32ParamTemp;
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,    FWContextSwitchProfile, psHints->ui32FWContextSwitchProfile);
	SrvInitParamGetBOOL(INITPARAM_NO_DEVICE,          pvParamState,
		HWPerfDisableCounterFilter, psHints->bHWPerfDisableCounterFilter);
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,    HWPerfHostBufSizeInKB,       psHints->ui32HWPerfHostBufSize);
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,    HWPerfFWBufSizeInKB,           psHints->ui32HWPerfFWBufSize);
	SrvInitParamGetUINT32(psDevInfo->psDeviceNode,    pvParamState,    KernelCCBSizeLog2,                psHints->ui32KCCBSizeLog2);

	if (psHints->ui32KCCBSizeLog2 < PVRSRV_RGX_LOG2_KERNEL_CCB_MIN_SIZE)
	{
		PVR_DPF((PVR_DBG_WARNING, "KCCB size %u is too low, setting to %u",
		         psHints->ui32KCCBSizeLog2, PVRSRV_RGX_LOG2_KERNEL_CCB_MIN_SIZE));
		psHints->ui32KCCBSizeLog2 = PVRSRV_RGX_LOG2_KERNEL_CCB_MIN_SIZE;
	}
	else if (psHints->ui32KCCBSizeLog2 > PVRSRV_RGX_LOG2_KERNEL_CCB_MAX_SIZE)
	{
		PVR_DPF((PVR_DBG_WARNING, "KCCB size %u is too high, setting to %u",
		         psHints->ui32KCCBSizeLog2, PVRSRV_RGX_LOG2_KERNEL_CCB_MAX_SIZE));
		psHints->ui32KCCBSizeLog2 = PVRSRV_RGX_LOG2_KERNEL_CCB_MAX_SIZE;
	}

#if defined(SUPPORT_VALIDATION)
	if (psHints->ui32KCCBSizeLog2 != PVRSRV_APPHINT_KCCB_SIZE_LOG2)
	{
		PVR_LOG(("KernelCCBSizeLog2 set to %u", psHints->ui32KCCBSizeLog2));
	}
#endif

	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,
		ISPSchedulingLatencyMode, psHints->ui32ISPSchedulingLatencyMode);
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,     CDMArbitrationOverride,    psHints->ui32CDMArbitrationMode);
#if defined(__linux__)
	/* name changes */
	{
		IMG_UINT64 ui64Tmp;
		SrvInitParamGetBOOL(psDevInfo->psDeviceNode,    pvParamState,    DisablePDumpPanic,                 psHints->bDisablePDP);
		SrvInitParamGetUINT64(psDevInfo->psDeviceNode,  pvParamState,    HWPerfFWFilter,                                 ui64Tmp);
		psHints->ui32HWPerfFilter0 = (IMG_UINT32)(ui64Tmp & 0xffffffffllu);
		psHints->ui32HWPerfFilter1 = (IMG_UINT32)((ui64Tmp >> 32) & 0xffffffffllu);
	}
#else
	SrvInitParamUnreferenced(DisablePDumpPanic);
	SrvInitParamUnreferenced(HWPerfFWFilter);
	SrvInitParamUnreferenced(RGXBVNC);
#endif
	SrvInitParamGetUINT32(psDevInfo->psDeviceNode,      pvParamState,    HWPerfHostFilter,             psHints->ui32HWPerfHostFilter);
	SrvInitParamGetUINT32List(psDevInfo->psDeviceNode,  pvParamState,    TimeCorrClock,                   psHints->ui32TimeCorrClock);
	SrvInitParamGetUINT32(psDevInfo->psDeviceNode,      pvParamState,    HWRDebugDumpLimit,                            ui32ParamTemp);
	psHints->ui32HWRDebugDumpLimit = MIN(ui32ParamTemp, RGXFWIF_HWR_DEBUG_DUMP_ALL);
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,          pvParamState,    JonesDisableMask,                             ui32ParamTemp);
	psHints->ui32JonesDisableMask = ui32ParamTemp & RGX_CR_JONES_FIX__ROGUE3__DISABLE_CLRMSK;

	SrvInitParamGetBOOL(INITPARAM_NO_DEVICE,            pvParamState,    NewFilteringMode,                   psHints->bFilteringMode);
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,          pvParamState,    TruncateMode,                     psHints->ui32TruncateMode);

	SrvInitParamGetBOOL(psDevInfo->psDeviceNode,        pvParamState,    ZeroFreelist,                        psHints->bZeroFreelist);
#if defined(__linux__)
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,          pvParamState,    FWContextSwitchCrossDM, psHints->ui32FWContextSwitchCrossDM);
#else
	SrvInitParamUnreferenced(FWContextSwitchCrossDM);
#endif

#if defined(SUPPORT_PHYSMEM_TEST) && !defined(INTEGRITY_OS) && !defined(__QNXNTO__)
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,          pvParamState,    PhysMemTestPasses,           psHints->ui32PhysMemTestPasses);
#endif

#if defined(SUPPORT_VALIDATION)
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,  KillingCtl,                              psHints->ui32RenderKillingCtl);
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,  CDMTDMKillingCtl,                        psHints->ui32CDMTDMKillingCtl);
	SrvInitParamGetBOOL(INITPARAM_NO_DEVICE,          pvParamState,  ValidateIrq,                                     psHints->bValidateIrq);
	SrvInitParamGetBOOL(INITPARAM_NO_DEVICE,          pvParamState,  ValidateSOCUSCTimer,                     psHints->bValidateSOCUSCTimer);
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,  HWValAvailableSPUMask,              psHints->ui32AvailablePowUnitsMask);
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,  HWValAvailableRACMask,                   psHints->ui32AvailableRACMask);
	SrvInitParamGetBOOL(psDevInfo->psDeviceNode,      pvParamState,  GPUUnitsPowerChange,           psHints->bInjectPowUnitsStateMaskChange);
	SrvInitParamGetBOOL(INITPARAM_NO_DEVICE,          pvParamState,  HWValEnableSPUPowerMaskChange, psHints->bEnablePowUnitsStateMaskChange);
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,  FBCDCVersionOverride,                psHints->ui32FBCDCVersionOverride);

	/* Apphints for Unified Store virtual partitioning. */
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,  USRMNumRegionsVDM,   psHints->aui32USRMNumRegions[RGXFWIF_USRM_DM_VDM]);
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,  USRMNumRegionsDDM,   psHints->aui32USRMNumRegions[RGXFWIF_USRM_DM_DDM]);
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,  USRMNumRegionsCDM,   psHints->aui32USRMNumRegions[RGXFWIF_USRM_DM_CDM]);
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,  USRMNumRegionsPDM,   psHints->aui32USRMNumRegions[RGXFWIF_USRM_DM_PDM]);
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,  USRMNumRegionsTDM,   psHints->aui32USRMNumRegions[RGXFWIF_USRM_DM_TDM]);

	/* Apphints for UVB virtual partitioning. */
	SrvInitParamGetUINT64(INITPARAM_NO_DEVICE,        pvParamState,  UVBRMNumRegionsVDM, psHints->aui64UVBRMNumRegions[RGXFWIF_UVBRM_DM_VDM]);
	SrvInitParamGetUINT64(INITPARAM_NO_DEVICE,        pvParamState,  UVBRMNumRegionsDDM, psHints->aui64UVBRMNumRegions[RGXFWIF_UVBRM_DM_DDM]);

	/* Apphints for TPU trilinear frac masking */
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,  TPUTrilinearFracMaskPDM, psHints->aui32TPUTrilinearFracMask[RGXFWIF_TPU_DM_PDM]);
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,  TPUTrilinearFracMaskVDM, psHints->aui32TPUTrilinearFracMask[RGXFWIF_TPU_DM_VDM]);
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,  TPUTrilinearFracMaskCDM, psHints->aui32TPUTrilinearFracMask[RGXFWIF_TPU_DM_CDM]);
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,  TPUTrilinearFracMaskTDM, psHints->aui32TPUTrilinearFracMask[RGXFWIF_TPU_DM_TDM]);
#if defined(SUPPORT_RAY_TRACING)
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,        pvParamState,  TPUTrilinearFracMaskRDM, psHints->aui32TPUTrilinearFracMask[RGXFWIF_TPU_DM_RDM]);
#endif
#endif

	/*
	 * FW logs apphints
	 */
	{
		IMG_UINT32 ui32LogGroup, ui32TraceOrTBI;

		SrvInitParamGetUINT32BitField(psDevInfo->psDeviceNode,  pvParamState,    EnableLogGroup,    ui32LogGroup);
		SrvInitParamGetUINT32List(psDevInfo->psDeviceNode,      pvParamState,    FirmwareLogType, ui32TraceOrTBI);

		/* Defaulting to TRACE */
		BITMASK_SET(ui32LogGroup, RGXFWIF_LOG_TYPE_TRACE);

#if defined(SUPPORT_TBI_INTERFACE)
		if (ui32TraceOrTBI == 1 /* TBI */)
		{
			if ((ui32LogGroup & RGXFWIF_LOG_TYPE_GROUP_MASK) == 0)
			{
				/* No groups configured - defaulting to MAIN group */
				BITMASK_SET(ui32LogGroup, RGXFWIF_LOG_TYPE_GROUP_MAIN);
			}
			BITMASK_UNSET(ui32LogGroup, RGXFWIF_LOG_TYPE_TRACE);
		}
#endif

		psHints->ui32LogType = ui32LogGroup;
	}

	SrvInitParamGetBOOL(INITPARAM_NO_DEVICE,  pvParamState,  EnableTrustedDeviceAceConfig,  psHints->bEnableTrustedDeviceAceConfig);

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
static INLINE void GetFWConfigFlags(PVRSRV_DEVICE_NODE *psDeviceNode,
                                    RGX_SRVINIT_APPHINTS *psHints,
                                    IMG_UINT32 *pui32FWConfigFlags,
                                    IMG_UINT32 *pui32FWConfigFlagsExt,
                                    IMG_UINT32 *pui32FwOsCfgFlags)
{
	IMG_UINT32 ui32FWConfigFlags = 0;
	IMG_UINT32 ui32FWConfigFlagsExt = 0;
	IMG_UINT32 ui32FwOsCfgFlags = psHints->ui32FWContextSwitchCrossDM |
	                              (psHints->ui32EnableFWContextSwitch & ~RGXFWIF_INICFG_OS_CTXSWITCH_CLRMSK);

	if (PVRSRV_VZ_MODE_IS(GUEST))
	{
		ui32FWConfigFlags = 0;
		ui32FWConfigFlagsExt = 0;
	}
	else
	{
		ui32FWConfigFlags |= psHints->bAssertOnOutOfMem ? RGXFWIF_INICFG_ASSERT_ON_OUTOFMEMORY : 0;
		ui32FWConfigFlags |= psHints->bAssertOnHWRTrigger ? RGXFWIF_INICFG_ASSERT_ON_HWR_TRIGGER : 0;
		ui32FWConfigFlags |= psHints->bCheckMlist ? RGXFWIF_INICFG_CHECK_MLIST_EN : 0;
		ui32FWConfigFlags |= psHints->bDisableClockGating ? RGXFWIF_INICFG_DISABLE_CLKGATING_EN : 0;
		ui32FWConfigFlags |= psHints->bDisableDMOverlap ? RGXFWIF_INICFG_DISABLE_DM_OVERLAP : 0;
		ui32FWConfigFlags |= psHints->bDisablePDP ? RGXFWIF_INICFG_DISABLE_PDP_EN : 0;
		ui32FWConfigFlags |= psHints->bEnableDMKillRand ? RGXFWIF_INICFG_DM_KILL_MODE_RAND_EN : 0;
		ui32FWConfigFlags |= psHints->bEnableRandomCsw ? RGXFWIF_INICFG_CTXSWITCH_MODE_RAND : 0;
		ui32FWConfigFlags |= psHints->bEnableSoftResetCsw ? RGXFWIF_INICFG_CTXSWITCH_SRESET_EN : 0;
		ui32FWConfigFlags |= (psHints->ui32HWPerfFilter0 != 0 || psHints->ui32HWPerfFilter1 != 0) ? RGXFWIF_INICFG_HWPERF_EN : 0;
		ui32FWConfigFlags |= (psHints->ui32ISPSchedulingLatencyMode << RGXFWIF_INICFG_ISPSCHEDMODE_SHIFT) & RGXFWIF_INICFG_ISPSCHEDMODE_MASK;
#if defined(SUPPORT_VALIDATION)
#if defined(NO_HARDWARE) && defined(PDUMP)
		ui32FWConfigFlags |= psHints->bValidateIrq ? RGXFWIF_INICFG_VALIDATE_IRQ : 0;
#endif
#endif
		ui32FWConfigFlags |= psHints->bHWPerfDisableCounterFilter ? RGXFWIF_INICFG_HWP_DISABLE_FILTER : 0;
		ui32FWConfigFlags |= (psHints->ui32FWContextSwitchProfile << RGXFWIF_INICFG_CTXSWITCH_PROFILE_SHIFT) & RGXFWIF_INICFG_CTXSWITCH_PROFILE_MASK;

#if defined(SUPPORT_VALIDATION)
		ui32FWConfigFlags |= psHints->bEnablePowUnitsStateMaskChange ? RGXFWIF_INICFG_SPU_POWER_STATE_MASK_CHANGE_EN : 0;
		ui32FWConfigFlags |= psHints->bValidateSOCUSCTimer ? RGXFWIF_INICFG_VALIDATE_SOCUSC_TIMER : 0;
		ui32FWConfigFlags |= psHints->bSPUClockGating ? RGXFWIF_INICFG_SPU_CLOCK_GATE : 0;

		if ((ui32FWConfigFlags & RGXFWIF_INICFG_VALIDATE_SOCUSC_TIMER) &&
		    (psHints->eRGXActivePMConf != 0) )
		{
			psHints->eRGXActivePMConf = 0;
			PVR_DPF((PVR_DBG_WARNING, "SoC/USC Timer test needs to run with EnableAPM disabled.\n"
				 "Overriding current value with new value 0."));
		}
#endif
		ui32FWConfigFlags |= psDeviceNode->pfnHasFBCDCVersion31(psDeviceNode) ? RGXFWIF_INICFG_FBCDC_V3_1_EN : 0;
		ui32FWConfigFlags |= (psHints->ui32CDMArbitrationMode << RGXFWIF_INICFG_CDM_ARBITRATION_SHIFT) & RGXFWIF_INICFG_CDM_ARBITRATION_MASK;
	}

	if ((ui32FwOsCfgFlags & RGXFWIF_INICFG_OS_CTXSWITCH_3D_EN) &&
		((ui32FWConfigFlags & RGXFWIF_INICFG_ISPSCHEDMODE_MASK) == RGXFWIF_INICFG_ISPSCHEDMODE_NONE))
	{
		ui32FwOsCfgFlags &= ~RGXFWIF_INICFG_OS_CTXSWITCH_3D_EN;
		PVR_DPF((PVR_DBG_WARNING, "ISPSchedulingLatencyMode=0 implies context switching is inoperable on DM_3D.\n"
				 "Overriding current value EnableFWContextSwitch=0x%x with new value 0x%x",
				 psHints->ui32EnableFWContextSwitch,
				 ui32FwOsCfgFlags & RGXFWIF_INICFG_OS_CTXSWITCH_DM_ALL));
	}

	*pui32FWConfigFlags    = ui32FWConfigFlags;
	*pui32FWConfigFlagsExt = ui32FWConfigFlagsExt;
	*pui32FwOsCfgFlags     = ui32FwOsCfgFlags;
}


/*!
*******************************************************************************

 @Function      GetFilterFlags

 @Description   Initialise and return filter flags

 @Input         psHints : Apphints container

 @Return        IMG_UINT32 : Filter flags

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

 @Function      InitDeviceFlags

 @Description   Initialise and return device flags

 @Input         psHints          : Apphints container
 @Input         pui32DeviceFlags : Pointer to device flags

 @Return        void

******************************************************************************/
static INLINE void InitDeviceFlags(RGX_SRVINIT_APPHINTS *psHints,
                                  IMG_UINT32 *pui32DeviceFlags)
{
	IMG_UINT32 ui32DeviceFlags = 0;

#if defined(SUPPORT_VALIDATION)
	ui32DeviceFlags |= psHints->bInjectPowUnitsStateMaskChange? RGXKM_DEVICE_STATE_GPU_UNITS_POWER_CHANGE_EN : 0;
#endif
	ui32DeviceFlags |= psHints->bZeroFreelist ? RGXKM_DEVICE_STATE_ZERO_FREELIST : 0;
	ui32DeviceFlags |= psHints->bDisableFEDLogging ? RGXKM_DEVICE_STATE_DISABLE_DW_LOGGING_EN : 0;
#if defined(SUPPORT_VALIDATION)
	ui32DeviceFlags |= psHints->bEnablePowUnitsStateMaskChange ? RGXKM_DEVICE_STATE_ENABLE_SPU_UNITS_POWER_MASK_CHANGE_EN : 0;
#endif
#if defined(PVRSRV_ENABLE_CCCB_GROW)
	BITMASK_SET(ui32DeviceFlags, RGXKM_DEVICE_STATE_CCB_GROW_EN);
#endif

	*pui32DeviceFlags = ui32DeviceFlags;
}

#if defined(SUPPORT_TRUSTED_DEVICE) && !defined(NO_HARDWARE) && !defined(SUPPORT_SECURITY_VALIDATION)
/*!
*******************************************************************************

 @Function      RGXTDProcessFWImage

 @Description   Fetch and send data used by the trusted device to complete
                the FW image setup

 @Input         psDeviceNode : Device node
 @Input         psRGXFW      : Firmware blob
 @Input         puFWParams   : Parameters used by the FW at boot time

 @Return        PVRSRV_ERROR
******************************************************************************/
static PVRSRV_ERROR RGXTDProcessFWImage(PVRSRV_DEVICE_NODE *psDeviceNode,
                                        OS_FW_IMAGE *psRGXFW,
                                        PVRSRV_FW_BOOT_PARAMS *puFWParams)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig = psDeviceNode->psDevConfig;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_TD_FW_PARAMS sTDFWParams;
	PVRSRV_ERROR eError;

	if (psDevConfig->pfnTDSendFWImage == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: TDSendFWImage not implemented!", __func__));
		return PVRSRV_ERROR_NOT_IMPLEMENTED;
	}

	sTDFWParams.pvFirmware       = OSFirmwareData(psRGXFW);
	sTDFWParams.ui32FirmwareSize = OSFirmwareSize(psRGXFW);

	if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META))
	{
		sTDFWParams.uFWP.sMeta = puFWParams->sMeta;
	}
	else
	{
		sTDFWParams.uFWP.sRISCV = puFWParams->sRISCV;
	}

	eError = psDevConfig->pfnTDSendFWImage(psDevConfig->hSysData, &sTDFWParams);

	return eError;
}
#endif

/*!
*******************************************************************************

 @Function      InitFirmware

 @Description   Allocate, initialise and pdump Firmware code and data memory

 @Input         psDeviceNode : Device Node
 @Input         psHints      : Apphints

 @Return        PVRSRV_ERROR

******************************************************************************/
static PVRSRV_ERROR InitFirmware(PVRSRV_DEVICE_NODE *psDeviceNode,
                                 RGX_SRVINIT_APPHINTS *psHints)
{
	OS_FW_IMAGE       *psRGXFW = NULL;
	const IMG_BYTE    *pbRGXFirmware = NULL;

	/* FW code memory */
	IMG_DEVMEM_SIZE_T uiFWCodeAllocSize;
	void              *pvFWCodeHostAddr;

	/* FW data memory */
	IMG_DEVMEM_SIZE_T uiFWDataAllocSize;
	void              *pvFWDataHostAddr;

	/* FW coremem code memory */
	IMG_DEVMEM_SIZE_T uiFWCorememCodeAllocSize;
	void              *pvFWCorememCodeHostAddr = NULL;

	/* FW coremem data memory */
	IMG_DEVMEM_SIZE_T uiFWCorememDataAllocSize;
	void              *pvFWCorememDataHostAddr = NULL;

	PVRSRV_FW_BOOT_PARAMS uFWParams;
	RGX_LAYER_PARAMS sLayerParams;
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *)psDeviceNode->pvDevice;

	/*
	 * Get pointer to Firmware image
	 */
	eError = RGXLoadAndGetFWData(psDeviceNode, &psRGXFW, &pbRGXFirmware);

	if (eError != PVRSRV_OK)
	{
		/* Error or confirmation message generated in RGXLoadAndGetFWData */
		goto fw_load_fail;
	}

	sLayerParams.psDevInfo = psDevInfo;

	/*
	 * Allocate Firmware memory
	 */

	eError = RGXGetFWImageAllocSize(&sLayerParams,
	                                pbRGXFirmware,
	                                OSFirmwareSize(psRGXFW),
	                                &uiFWCodeAllocSize,
	                                &uiFWDataAllocSize,
	                                &uiFWCorememCodeAllocSize,
	                                &uiFWCorememDataAllocSize);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "%s: RGXGetFWImageAllocSize failed",
		        __func__));
		goto cleanup_initfw;
	}

	psDevInfo->ui32FWCodeSizeInBytes = uiFWCodeAllocSize;

#if defined(SUPPORT_TRUSTED_DEVICE)
	if (!RGX_IS_FEATURE_SUPPORTED(psDevInfo, META_DMA))
	{
		PVR_DPF((PVR_DBG_WARNING,
		        "%s: META DMA not available, disabling core memory code/data",
		        __func__));
		uiFWCorememCodeAllocSize = 0;
		uiFWCorememDataAllocSize = 0;
	}
#endif

	psDevInfo->ui32FWCorememCodeSizeInBytes = uiFWCorememCodeAllocSize;

	eError = RGXInitAllocFWImgMem(psDeviceNode,
	                              uiFWCodeAllocSize,
	                              uiFWDataAllocSize,
	                              uiFWCorememCodeAllocSize,
	                              uiFWCorememDataAllocSize);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "%s: PVRSRVRGXInitAllocFWImgMem failed (%d)",
		        __func__,
		        eError));
		goto cleanup_initfw;
	}

	/*
	 * Acquire pointers to Firmware allocations
	 */

#if !defined(SUPPORT_TRUSTED_DEVICE) || defined(NO_HARDWARE) || defined(SUPPORT_SECURITY_VALIDATION)
	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWCodeMemDesc, &pvFWCodeHostAddr);
	PVR_LOG_GOTO_IF_ERROR(eError, "DevmemAcquireCpuVirtAddr", cleanup_initfw);

#else
	/* We can't get a pointer to a secure FW allocation from within the DDK */
	pvFWCodeHostAddr = NULL;
#endif

#if !defined(SUPPORT_TRUSTED_DEVICE) || defined(NO_HARDWARE) || defined(SUPPORT_SECURITY_VALIDATION)
	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWDataMemDesc, &pvFWDataHostAddr);
	PVR_LOG_GOTO_IF_ERROR(eError, "DevmemAcquireCpuVirtAddr", release_code);

#else
	/* We can't get a pointer to a secure FW allocation from within the DDK */
	pvFWDataHostAddr = NULL;
#endif

#if !defined(SUPPORT_TRUSTED_DEVICE) || defined(NO_HARDWARE) || defined(SUPPORT_SECURITY_VALIDATION)
	if (uiFWCorememCodeAllocSize != 0)
	{
		eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWCorememCodeMemDesc, &pvFWCorememCodeHostAddr);
		PVR_LOG_GOTO_IF_ERROR(eError, "DevmemAcquireCpuVirtAddr", release_data);
	}
	else
	{
		pvFWCorememCodeHostAddr = NULL;
	}
#else
	/* We can't get a pointer to a secure FW allocation from within the DDK */
	pvFWCorememCodeHostAddr = NULL;
#endif

#if !defined(SUPPORT_TRUSTED_DEVICE) || defined(NO_HARDWARE) || defined(SUPPORT_SECURITY_VALIDATION)
	if (uiFWCorememDataAllocSize != 0)
	{
		eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWIfCorememDataStoreMemDesc, &pvFWCorememDataHostAddr);
		PVR_LOG_GOTO_IF_ERROR(eError, "DevmemAcquireCpuVirtAddr", release_corememcode);
	}
	else
#endif
	{
		pvFWCorememDataHostAddr = NULL;
	}

	/*
	 * Prepare FW boot parameters
	 */

	if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META))
	{
		uFWParams.sMeta.sFWCodeDevVAddr =  psDevInfo->sFWCodeDevVAddrBase;
		uFWParams.sMeta.sFWDataDevVAddr =  psDevInfo->sFWDataDevVAddrBase;
		uFWParams.sMeta.sFWCorememCodeDevVAddr =  psDevInfo->sFWCorememCodeDevVAddrBase;
		uFWParams.sMeta.sFWCorememCodeFWAddr = psDevInfo->sFWCorememCodeFWAddr;
		uFWParams.sMeta.uiFWCorememCodeSize = uiFWCorememCodeAllocSize;
		uFWParams.sMeta.sFWCorememDataDevVAddr = psDevInfo->sFWCorememDataStoreDevVAddrBase;
		uFWParams.sMeta.sFWCorememDataFWAddr = psDevInfo->sFWCorememDataStoreFWAddr;
#if defined(RGXFW_META_SUPPORT_2ND_THREAD)
		uFWParams.sMeta.ui32NumThreads = 2;
#else
		uFWParams.sMeta.ui32NumThreads = 1;
#endif
	}
	else
	{
		uFWParams.sRISCV.sFWCorememCodeDevVAddr = psDevInfo->sFWCorememCodeDevVAddrBase;
		uFWParams.sRISCV.sFWCorememCodeFWAddr   = psDevInfo->sFWCorememCodeFWAddr;
		uFWParams.sRISCV.uiFWCorememCodeSize    = uiFWCorememCodeAllocSize;

		uFWParams.sRISCV.sFWCorememDataDevVAddr = psDevInfo->sFWCorememDataStoreDevVAddrBase;
		uFWParams.sRISCV.sFWCorememDataFWAddr   = psDevInfo->sFWCorememDataStoreFWAddr;
		uFWParams.sRISCV.uiFWCorememDataSize    = uiFWCorememDataAllocSize;
	}


	/*
	 * Process the Firmware image and setup code and data segments.
	 *
	 * When the trusted device is enabled and the FW code lives
	 * in secure memory we will only setup the data segments here,
	 * while the code segments will be loaded to secure memory
	 * by the trusted device.
	 */
	if (!psDeviceNode->bAutoVzFwIsUp)
	{
		eError = RGXProcessFWImage(&sLayerParams,
								   pbRGXFirmware,
								   pvFWCodeHostAddr,
								   pvFWDataHostAddr,
								   pvFWCorememCodeHostAddr,
								   pvFWCorememDataHostAddr,
								   &uFWParams);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
			         "%s: RGXProcessFWImage failed (%d)",
			         __func__,
			         eError));
			goto release_corememdata;
		}
	}

#if defined(SUPPORT_TRUSTED_DEVICE) && !defined(NO_HARDWARE) && !defined(SUPPORT_SECURITY_VALIDATION)
	if (psRGXFW)
	{
		RGXTDProcessFWImage(psDeviceNode, psRGXFW, &uFWParams);
	}
#endif


	/*
	 * PDump Firmware allocations
	 */

#if !defined(SUPPORT_TRUSTED_DEVICE) || defined(NO_HARDWARE) || defined(SUPPORT_SECURITY_VALIDATION)
	PDUMPCOMMENTWITHFLAGS(psDeviceNode, PDUMP_FLAGS_CONTINUOUS,
	                      "Dump firmware code image");
	DevmemPDumpLoadMem(psDevInfo->psRGXFWCodeMemDesc,
	                   0,
	                   uiFWCodeAllocSize,
	                   PDUMP_FLAGS_CONTINUOUS);
#endif

	PDUMPCOMMENTWITHFLAGS(psDeviceNode, PDUMP_FLAGS_CONTINUOUS,
	                      "Dump firmware data image");
	DevmemPDumpLoadMem(psDevInfo->psRGXFWDataMemDesc,
	                   0,
	                   uiFWDataAllocSize,
	                   PDUMP_FLAGS_CONTINUOUS);

#if !defined(SUPPORT_TRUSTED_DEVICE) || defined(NO_HARDWARE) || defined(SUPPORT_SECURITY_VALIDATION)
	if (uiFWCorememCodeAllocSize != 0)
	{
		PDUMPCOMMENTWITHFLAGS(psDeviceNode, PDUMP_FLAGS_CONTINUOUS,
		                      "Dump firmware coremem code image");
		DevmemPDumpLoadMem(psDevInfo->psRGXFWCorememCodeMemDesc,
						   0,
						   uiFWCorememCodeAllocSize,
						   PDUMP_FLAGS_CONTINUOUS);
	}
#endif

#if !defined(SUPPORT_TRUSTED_DEVICE) || defined(NO_HARDWARE) || defined(SUPPORT_SECURITY_VALIDATION)
	if (uiFWCorememDataAllocSize != 0)
	{
		PDUMPCOMMENTWITHFLAGS(psDeviceNode, PDUMP_FLAGS_CONTINUOUS,
		                      "Dump firmware coremem data store image");
		DevmemPDumpLoadMem(psDevInfo->psRGXFWIfCorememDataStoreMemDesc,
						   0,
						   uiFWCorememDataAllocSize,
						   PDUMP_FLAGS_CONTINUOUS);
	}
#endif

	/*
	 * Release Firmware allocations and clean up
	 */
release_corememdata:
#if !defined(SUPPORT_TRUSTED_DEVICE) || defined(NO_HARDWARE) || defined(SUPPORT_SECURITY_VALIDATION)
	if (uiFWCorememDataAllocSize !=0)
	{
		DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWIfCorememDataStoreMemDesc);
	}

release_corememcode:
	if (uiFWCorememCodeAllocSize != 0)
	{
		DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWCorememCodeMemDesc);
	}
#endif

#if !defined(SUPPORT_TRUSTED_DEVICE) || defined(NO_HARDWARE) || defined(SUPPORT_SECURITY_VALIDATION)
release_data:
	DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWDataMemDesc);
#endif

#if !defined(SUPPORT_TRUSTED_DEVICE) || defined(NO_HARDWARE) || defined(SUPPORT_SECURITY_VALIDATION)
release_code:
	DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWCodeMemDesc);
#endif
cleanup_initfw:
	OSUnloadFirmware(psRGXFW);
fw_load_fail:

	return eError;
}

IMG_INTERNAL static inline IMG_UINT32 RGXHWPerfMaxDefinedBlks(PVRSRV_RGXDEV_INFO *);
IMG_INTERNAL /*static inline*/ IMG_UINT32 RGXGetHWPerfBlockConfig(const RGXFW_HWPERF_CNTBLK_TYPE_MODEL **);

IMG_INTERNAL /*static inline*/ IMG_UINT32
RGXGetHWPerfBlockConfig(const RGXFW_HWPERF_CNTBLK_TYPE_MODEL **ppsModel)
{
    *ppsModel = gasCntBlkTypeModel;
    return ARRAY_SIZE(gasCntBlkTypeModel);
}

/*!
*******************************************************************************
 @Function    RGXHWPerfMaxDefinedBlks

 @Description Return the number of valid block-IDs for the given device node

 @Input       (PVRSRV_RGXDEV_INFO *)   pvDevice    device-node to query

 @Returns     (IMG_UINT32)             Number of block-IDs (RGX_CNTBLK_ID)
                                       valid for this device.
******************************************************************************/
IMG_INTERNAL static inline IMG_UINT32
RGXHWPerfMaxDefinedBlks(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	RGX_HWPERF_CNTBLK_RT_INFO sRtInfo;
	IMG_UINT32  uiRetVal;
	const RGXFW_HWPERF_CNTBLK_TYPE_MODEL *psHWPBlkConfig;
	IMG_UINT32  uiNumArrayEls, ui;

	uiRetVal = RGX_CNTBLK_ID_DIRECT_LAST;

	uiNumArrayEls = RGXGetHWPerfBlockConfig(&psHWPBlkConfig);

	if (psHWPBlkConfig == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Unexpected NULL Config Block", __func__));
		return 0;
	}
	PVR_ASSERT(uiNumArrayEls > 0);

	/* Iterate over each block-ID and find the number of instances of each
	 * block which are present for this device type. We only query the
	 * Indirect blocks as their presence varies according to GPU. All direct
	 * blocks have an entry - but they may not be physically present.
	 */
	for (ui = RGX_CNTBLK_ID_DIRECT_LAST; ui < uiNumArrayEls; ui++)
	{
		if (rgx_hwperf_blk_present(&psHWPBlkConfig[ui], (void *)psDevInfo, &sRtInfo))
		{
			uiRetVal += sRtInfo.uiNumUnits;
			PVR_DPF((PVR_DBG_VERBOSE, "%s: Block %u, NumUnits %u, Total %u",
			        __func__, ui, sRtInfo.uiNumUnits, uiRetVal));
		}
#ifdef DEBUG
		else
		{
			if (psHWPBlkConfig[ui].uiCntBlkIdBase == RGX_CNTBLK_ID_RAC0)
			{
				if (PVRSRV_GET_DEVICE_FEATURE_VALUE(psDevInfo->psDeviceNode,
				    RAY_TRACING_ARCH) > 2U)
				{
					PVR_DPF((PVR_DBG_WARNING, "%s: Block %u *NOT* present",
					        __func__, ui));
				}
			}
			else
			{
				PVR_DPF((PVR_DBG_WARNING, "%s: Block %u *NOT* present",
				        __func__, ui));
			}
		}
#endif
	}

	PVR_DPF((PVR_DBG_VERBOSE, "%s: Num Units = %u", __func__, uiRetVal));

	return uiRetVal;
}

/*!
*******************************************************************************

 @Function      InitialiseHWPerfCounters

 @Description   Initialisation of hardware performance counters and dumping
                them out to pdump, so that they can be modified at a later
                point.

 @Input         pvDevice
 @Input         psHWPerfDataMemDesc
 @Input         psHWPerfInitDataInt

 @Return        void

******************************************************************************/

static void InitialiseHWPerfCounters(PVRSRV_DEVICE_NODE *psDeviceNode,
                                     void *pvDevice,
                                     DEVMEM_MEMDESC *psHWPerfDataMemDesc,
                                     RGXFWIF_HWPERF_CTL *psHWPerfInitDataInt)
{
	RGXFWIF_HWPERF_CTL_BLK *psHWPerfInitBlkData;
	IMG_UINT32 ui32CntBlkModelLen;
	const RGXFW_HWPERF_CNTBLK_TYPE_MODEL *asCntBlkTypeModel;
	const RGXFW_HWPERF_CNTBLK_TYPE_MODEL* psBlkTypeDesc;
	IMG_UINT32 ui32BlockID, ui32BlkCfgIdx, ui32CounterIdx;
	RGX_HWPERF_CNTBLK_RT_INFO sCntBlkRtInfo;
	IMG_UINT32 uiUnit;
	IMG_BOOL bDirect;

	ui32CntBlkModelLen = RGXGetHWPerfBlockConfig(&asCntBlkTypeModel);

	PVR_DPF((PVR_DBG_VERBOSE, "%s: #BlockConfig entries = %d", __func__, ui32CntBlkModelLen));

	/* Initialise the number of blocks in the RGXFWIF_HWPERF_CTL structure.
	 * This allows Firmware to validate that it has been correctly configured.
	 */
	psHWPerfInitDataInt->ui32NumBlocks = RGXHWPerfMaxDefinedBlks(pvDevice);

	PDUMPCOMMENTWITHFLAGS(psDeviceNode, PDUMP_FLAGS_CONTINUOUS,
	    "HWPerf Block count = %u.",
	    psHWPerfInitDataInt->ui32NumBlocks);
#if defined(PDUMP)
	/* Ensure that we record the BVNC specific ui32NumBlocks in the PDUMP data
	 * so that when we playback we have the correct value present.
	 */
	DevmemPDumpLoadMemValue32(psHWPerfDataMemDesc,
	    (size_t)&(psHWPerfInitDataInt->ui32NumBlocks) - (size_t)(psHWPerfInitDataInt),
	    psHWPerfInitDataInt->ui32NumBlocks, PDUMP_FLAGS_CONTINUOUS);
#endif	/* defined(PDUMP) */

	PDUMPCOMMENTWITHFLAGS(psDeviceNode, PDUMP_FLAGS_CONTINUOUS,
	    "HWPerf Counter config starts here.");

	/* Simply iterate over all the RGXFWIW_HWPERF_CTL blocks in order */
	psHWPerfInitBlkData = &psHWPerfInitDataInt->sBlkCfg[0];

	for (ui32BlkCfgIdx = 0; ui32BlkCfgIdx < ui32CntBlkModelLen;
	     ui32BlkCfgIdx++, psHWPerfInitBlkData++)
	{
		IMG_BOOL bSingleton;

		/* Exit early if this core does not have any of these counter blocks
		 * due to core type/BVNC features.... */
		psBlkTypeDesc = &asCntBlkTypeModel[ui32BlkCfgIdx];

		if (psBlkTypeDesc == NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Unexpected NULL - Index %d / %d",
			    __func__, ui32BlkCfgIdx, ui32CntBlkModelLen));
			continue;
		}

		PVR_DPF((PVR_DBG_VERBOSE,
		        "%s: CfgIdx = %u, InitBlkData @ 0x%p, BlkTypeDesc @ 0x%p",
		        __func__, ui32BlkCfgIdx, psHWPerfInitBlkData, psBlkTypeDesc));

		if (psBlkTypeDesc->pfnIsBlkPresent(psBlkTypeDesc, pvDevice, &sCntBlkRtInfo) == IMG_FALSE)
		{
			PVR_DPF((PVR_DBG_VERBOSE, "%s: %s [ID 0x%x] NOT present", __func__,
			    psBlkTypeDesc->pszBlockNameComment,
			    psBlkTypeDesc->uiCntBlkIdBase ));
			/* Block isn't present, but has an entry in the table. Populate
			 * the Init data so that we can track the block later.
			 */
			psHWPerfInitBlkData->uiBlockID = psBlkTypeDesc->uiCntBlkIdBase;
			continue;
		}
#ifdef DEBUG
		else
		{
			PVR_DPF((PVR_DBG_VERBOSE, "%s: %s has %d %s", __func__,
				psBlkTypeDesc->pszBlockNameComment, sCntBlkRtInfo.uiNumUnits,
			    (sCntBlkRtInfo.uiNumUnits > 1) ? "units" : "unit"));
		}
#endif	/* DEBUG */

		/* Program all counters in one block so those already on may
		 * be configured off and vice-versa. */
		bDirect = psBlkTypeDesc->uiIndirectReg == 0;

		/* Set if there is only one instance of this block-ID present */
		bSingleton = sCntBlkRtInfo.uiNumUnits == 1;

		for (ui32BlockID = psBlkTypeDesc->uiCntBlkIdBase, uiUnit = 0;
		     ui32BlockID < psBlkTypeDesc->uiCntBlkIdBase+sCntBlkRtInfo.uiNumUnits;
		     ui32BlockID++, uiUnit++)
		{

			if (bDirect)
			{
				PDUMPCOMMENTWITHFLAGS(psDeviceNode, PDUMP_FLAGS_CONTINUOUS,
				    "Block : %s", psBlkTypeDesc->pszBlockNameComment);
			}
			else
			{
				PDUMPCOMMENTWITHFLAGS(psDeviceNode,
				    PDUMP_FLAGS_CONTINUOUS,
				    "Unit %d Block : %s%d",
				    ui32BlockID-psBlkTypeDesc->uiCntBlkIdBase,
				    psBlkTypeDesc->pszBlockNameComment, uiUnit);
			}

			psHWPerfInitBlkData->uiBlockID = ui32BlockID;
			PDUMPCOMMENTWITHFLAGS(psDeviceNode, PDUMP_FLAGS_CONTINUOUS,
			    "uiBlockID: The Block ID for the layout block. See RGX_CNTBLK_ID for further information.");
#if defined(PDUMP)
			DevmemPDumpLoadMemValue32(psHWPerfDataMemDesc,
							(size_t)&(psHWPerfInitBlkData->uiBlockID) - (size_t)(psHWPerfInitDataInt),
							psHWPerfInitBlkData->uiBlockID,
							PDUMP_FLAGS_CONTINUOUS);
#endif	/* PDUMP */

			psHWPerfInitBlkData->uiNumCounters = 0;
			PDUMPCOMMENTWITHFLAGS(psDeviceNode, PDUMP_FLAGS_CONTINUOUS,
			    "uiNumCounters (X): Specifies the number of valid counters"
			    " [0..%d] which follow.", RGX_CNTBLK_COUNTERS_MAX);
#if defined(PDUMP)
			DevmemPDumpLoadMemValue32(psHWPerfDataMemDesc,
			    (size_t)&(psHWPerfInitBlkData->uiNumCounters) - (size_t)(psHWPerfInitDataInt),
			    psHWPerfInitBlkData->uiNumCounters,
			    PDUMP_FLAGS_CONTINUOUS);
#endif	/* PDUMP */

			psHWPerfInitBlkData->uiEnabled = 0;
			PDUMPCOMMENTWITHFLAGS(psDeviceNode, PDUMP_FLAGS_CONTINUOUS,
			    "uiEnabled: Set to 0x1 if the block needs to be enabled during playback.");
#if defined(PDUMP)
			DevmemPDumpLoadMemValue32(psHWPerfDataMemDesc,
			    (size_t)&(psHWPerfInitBlkData->uiEnabled) - (size_t)(psHWPerfInitDataInt),
			    psHWPerfInitBlkData->uiEnabled,
			    PDUMP_FLAGS_CONTINUOUS);
#endif	/* PDUMP */

			for (ui32CounterIdx = 0; ui32CounterIdx < RGX_CNTBLK_COUNTERS_MAX; ui32CounterIdx++)
			{
				psHWPerfInitBlkData->aui32CounterCfg[ui32CounterIdx] = IMG_UINT32_C(0x00000000);

				if (bDirect)
				{
					PDUMPCOMMENTWITHFLAGS(psDeviceNode,
					    PDUMP_FLAGS_CONTINUOUS,
					    "%s_COUNTER_%d",
					    psBlkTypeDesc->pszBlockNameComment, ui32CounterIdx);
				}
				else
				{
					PDUMPCOMMENTWITHFLAGS(psDeviceNode,
					    PDUMP_FLAGS_CONTINUOUS,
					    "%s%d_COUNTER_%d",
					    psBlkTypeDesc->pszBlockNameComment,
					    uiUnit, ui32CounterIdx);
				}
#if defined(PDUMP)
				DevmemPDumpLoadMemValue32(psHWPerfDataMemDesc,
				    (size_t)&(psHWPerfInitBlkData->aui32CounterCfg[ui32CounterIdx]) - (size_t)(psHWPerfInitDataInt),
				    psHWPerfInitBlkData->aui32CounterCfg[ui32CounterIdx],
				    PDUMP_FLAGS_CONTINUOUS);
#endif	/* PDUMP */

			}

			/* Update our block reference for indirect units which have more
			 * than a single unit present. Only increment if we have more than
			 * one unit left to process as the external loop counter will be
			 * incremented after final unit is processed.
			 */
			if (!bSingleton && (uiUnit < (sCntBlkRtInfo.uiNumUnits - 1)))
			{
				psHWPerfInitBlkData++;
			}
		}
	}
	PDUMPCOMMENTWITHFLAGS(psDeviceNode, PDUMP_FLAGS_CONTINUOUS,
	                      "HWPerf Counter config finishes here.");
}

/*!
*******************************************************************************

 @Function      InitialiseAllCounters

 @Description   Initialise HWPerf and custom counters

 @Input         psDeviceNode : Device Node

 @Return        PVRSRV_ERROR

******************************************************************************/
static PVRSRV_ERROR InitialiseAllCounters(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *)psDeviceNode->pvDevice;
	RGXFWIF_HWPERF_CTL *psHWPerfInitData;
	PVRSRV_ERROR eError;

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWIfHWPerfCountersMemDesc, (void **)&psHWPerfInitData);
	PVR_LOG_GOTO_IF_ERROR(eError, "DevmemAcquireCpuVirtAddr", failHWPerfCountersMemDescAqCpuVirt);

	InitialiseHWPerfCounters(psDeviceNode, psDevInfo, psDevInfo->psRGXFWIfHWPerfCountersMemDesc, psHWPerfInitData);

failHWPerfCountersMemDescAqCpuVirt:
	DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWIfHWPerfCountersMemDesc);

	return eError;
}

/*
 * _ParseHTBAppHints:
 *
 * Generate necessary references to the globally visible AppHints which are
 * declared in the above #include "km_apphint_defs.h"
 * Without these local references some compiler tool-chains will treat
 * unreferenced declarations as fatal errors. This function duplicates the
 * HTB_specific apphint references which are made in htbserver.c:HTBInit()
 * However, it makes absolutely *NO* use of these hints.
 */
static void
_ParseHTBAppHints(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	void *pvParamState = NULL;
	IMG_UINT32 ui32LogType;
	IMG_UINT32 ui32BufferSize;
	IMG_UINT32 ui32OpMode;

	/* Services initialisation parameters */
	pvParamState = SrvInitParamOpen();
	if (pvParamState == NULL)
		return;

	SrvInitParamGetUINT32BitField(INITPARAM_NO_DEVICE,  pvParamState,    EnableHTBLogGroup,    ui32LogType);
	SrvInitParamGetUINT32List(INITPARAM_NO_DEVICE,      pvParamState,    HTBOperationMode,      ui32OpMode);
	SrvInitParamGetUINT32(INITPARAM_NO_DEVICE,          pvParamState,    HTBufferSizeInKB,  ui32BufferSize);

	SrvInitParamClose(pvParamState);
}

#if defined(SUPPORT_TRUSTED_DEVICE)
static PVRSRV_ERROR RGXValidateTDHeap(PVRSRV_DEVICE_NODE *psDeviceNode,
									  PVRSRV_PHYS_HEAP ePhysHeap,
									  PHYS_HEAP_USAGE_FLAGS ui32RequiredFlags)
{
	PHYS_HEAP *psHeap = psDeviceNode->apsPhysHeap[ePhysHeap];
	PHYS_HEAP_USAGE_FLAGS ui32HeapFlags = PhysHeapGetFlags(psHeap);
	PHYS_HEAP_USAGE_FLAGS ui32InvalidFlags = ~(PHYS_HEAP_USAGE_FW_PRIV_DATA | PHYS_HEAP_USAGE_FW_CODE
											   | PHYS_HEAP_USAGE_GPU_SECURE | PHYS_HEAP_USAGE_FW_PRIVATE);

	PVR_LOG_RETURN_IF_FALSE_VA((ui32HeapFlags & ui32RequiredFlags) != 0,
							   PVRSRV_ERROR_NOT_SUPPORTED,
							   "TD heap is missing required flags. flags: 0x%x / required:0x%x",
							   ui32HeapFlags,
							   ui32RequiredFlags);

	PVR_LOG_RETURN_IF_FALSE_VA((ui32HeapFlags & ui32InvalidFlags) == 0,
							   PVRSRV_ERROR_NOT_SUPPORTED,
							   "TD heap uses invalid flags. flags: 0x%x / invalid:0x%x",
							   ui32HeapFlags,
							   ui32InvalidFlags);

	return PVRSRV_OK;
}

static PVRSRV_ERROR RGXValidateTDHeaps(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_ERROR eError;

	eError = RGXValidateTDHeap(psDeviceNode, PVRSRV_PHYS_HEAP_FW_PRIV_DATA, PHYS_HEAP_USAGE_FW_PRIV_DATA);
	PVR_LOG_RETURN_IF_ERROR(eError, "RGXValidateTDHeap:FW_PRIV_DATA");

	eError = RGXValidateTDHeap(psDeviceNode, PVRSRV_PHYS_HEAP_FW_CODE, PHYS_HEAP_USAGE_FW_CODE);
	PVR_LOG_RETURN_IF_ERROR(eError, "RGXValidateTDHeap:FW_CODE");

	eError = RGXValidateTDHeap(psDeviceNode, PVRSRV_PHYS_HEAP_GPU_SECURE, PHYS_HEAP_USAGE_GPU_SECURE);
	PVR_LOG_RETURN_IF_ERROR(eError, "RGXValidateTDHeap:GPU_SECURE");

	return PVRSRV_OK;
}
#endif

/*!
*******************************************************************************

 @Function      RGXInit

 @Description   RGX Initialisation

 @Input         psDeviceNode

 @Return        PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RGXInit(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_ERROR eError;

	/* Services initialisation parameters */
	RGX_SRVINIT_APPHINTS sApphints = {0};
	IMG_UINT32 ui32FWConfigFlags, ui32FWConfigFlagsExt, ui32FwOsCfgFlags;
	IMG_UINT32 ui32DeviceFlags;
	IMG_UINT32 ui32AvailablePowUnitsMask, ui32AvailableRACMask;

	PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *)psDeviceNode->pvDevice;

	/* Number of HWPerf Block-IDs (RGX_CNTBLK_ID) which are available */
	IMG_UINT32 ui32NumHWPerfBlocks;

	/* Size of the RGXFWIF_HWPERF_CTL_BLK structure - varies by BVNC */
	IMG_UINT32 ui32HWPerfBlkSize;
	RGX_LAYER_PARAMS sLayerParams;

	PDUMPCOMMENT(psDeviceNode, "RGX Initialisation Part 1");

	PDUMPCOMMENT(psDeviceNode, "Device Name: %s",
	             psDeviceNode->psDevConfig->pszName);
	PDUMPCOMMENT(psDeviceNode, "Device ID: %u (%d)",
	             psDeviceNode->sDevId.ui32InternalID,
	             psDeviceNode->sDevId.i32KernelDeviceID);

	if (psDeviceNode->psDevConfig->pszVersion)
	{
		PDUMPCOMMENT(psDeviceNode, "Device Version: %s",
		             psDeviceNode->psDevConfig->pszVersion);
	}

	/* pdump info about the core */
	PDUMPCOMMENT(psDeviceNode,
	             "RGX Version Information (KM): %d.%d.%d.%d",
	             psDevInfo->sDevFeatureCfg.ui32B,
	             psDevInfo->sDevFeatureCfg.ui32V,
	             psDevInfo->sDevFeatureCfg.ui32N,
	             psDevInfo->sDevFeatureCfg.ui32C);

	RGXInitMultiCoreInfo(psDeviceNode);

	sLayerParams.psDevInfo = psDevInfo;

#if defined(SUPPORT_TRUSTED_DEVICE)
	eError = RGXValidateTDHeaps(psDeviceNode);
	PVR_LOG_RETURN_IF_ERROR(eError, "RGXValidateTDHeaps");
#endif

#if defined(SUPPORT_AUTOVZ)
	if (PVRSRV_VZ_MODE_IS(HOST))
	{
		/* The RGX_CR_MTS_DM0_INTERRUPT_ENABLE register is always set by the firmware during initialisation
		 * and it provides a good method of determining if the firmware has been booted previously */
		psDeviceNode->bAutoVzFwIsUp = (OSReadHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_MTS_DM0_INTERRUPT_ENABLE) != 0);

		PVR_LOG(("AutoVz startup check: firmware is %s;",
				(psDeviceNode->bAutoVzFwIsUp) ? "already running" : "powered down"));
	}
	else if (PVRSRV_VZ_MODE_IS(GUEST))
	{
		/* Guest assumes the firmware is always available */
		psDeviceNode->bAutoVzFwIsUp = IMG_TRUE;
	}
	else
#endif
	{
		/* Firmware does not follow the AutoVz life-cycle */
		psDeviceNode->bAutoVzFwIsUp = IMG_FALSE;
	}

	if (PVRSRV_VZ_MODE_IS(GUEST) || (psDeviceNode->bAutoVzFwIsUp))
	{
		/* set the device power state here as the regular power
		 * callbacks will not be executed on this driver */
		psDevInfo->bRGXPowered = IMG_TRUE;
	}

	/* Set which HW Safety Events will be handled by the driver */
	psDevInfo->ui32HostSafetyEventMask |= RGX_IS_FEATURE_SUPPORTED(psDevInfo, WATCHDOG_TIMER) ?
										  RGX_CR_EVENT_STATUS_WDT_TIMEOUT_EN : 0;
	psDevInfo->ui32HostSafetyEventMask |= (RGX_DEVICE_HAS_FEATURE_VALUE(&sLayerParams, ECC_RAMS)
										   && (RGX_DEVICE_GET_FEATURE_VALUE(&sLayerParams, ECC_RAMS) > 0)) ?
										  RGX_CR_EVENT_STATUS_FAULT_FW_EN : 0;

#if defined(PDUMP)
	PDUMPCOMMENTWITHFLAGS(psDeviceNode, PDUMP_FLAGS_CONTINUOUS,
	                      "Register defs revision: %d", RGX_CR_DEFS_KM_REVISION);
#endif

	ui32NumHWPerfBlocks = RGXHWPerfMaxDefinedBlks((void *)psDevInfo);

	ui32HWPerfBlkSize = sizeof(RGXFWIF_HWPERF_CTL) +
		(ui32NumHWPerfBlocks - 1) * sizeof(RGXFWIF_HWPERF_CTL_BLK);

	/* Services initialisation parameters */
	_ParseHTBAppHints(psDeviceNode);
	GetApphints(psDevInfo, &sApphints);
	InitDeviceFlags(&sApphints, &ui32DeviceFlags);

#if defined(SUPPORT_GPUVIRT_VALIDATION)
#if defined(EMULATOR)
	if ((sApphints.bEnableTrustedDeviceAceConfig) &&
		(RGX_IS_FEATURE_SUPPORTED(psDevInfo, AXI_ACE)))
	{
		SetTrustedDeviceAceEnabled(psDeviceNode->psDevConfig->hSysData);
	}
#endif
#endif

	eError = RGXInitCreateFWKernelMemoryContext(psDeviceNode);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to create FW kernel memory context (%u)",
		         __func__, eError));
		goto cleanup;
	}

	if (!PVRSRV_VZ_MODE_IS(GUEST))
	{
		eError = InitFirmware(psDeviceNode, &sApphints);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
			        "%s: InitFirmware failed (%d)",
			        __func__,
			        eError));
			goto cleanup;
		}
	}

	/*
	 * Setup Firmware initialisation data
	 */

	GetFWConfigFlags(psDeviceNode, &sApphints, &ui32FWConfigFlags, &ui32FWConfigFlagsExt, &ui32FwOsCfgFlags);

#if defined(SUPPORT_VALIDATION)
	ui32AvailablePowUnitsMask = sApphints.ui32AvailablePowUnitsMask;
	ui32AvailableRACMask = sApphints.ui32AvailableRACMask;
#else
	ui32AvailablePowUnitsMask = AVAIL_POW_UNITS_MASK_DEFAULT;
	ui32AvailableRACMask = AVAIL_RAC_MASK_DEFAULT;
#endif

	eError = RGXInitFirmware(psDeviceNode,
	                         sApphints.bEnableSignatureChecks,
	                         sApphints.ui32SignatureChecksBufSize,
	                         sApphints.ui32HWPerfFWBufSize,
	                         (IMG_UINT64)sApphints.ui32HWPerfFilter0 |
	                         ((IMG_UINT64)sApphints.ui32HWPerfFilter1 << 32),
	                         ui32FWConfigFlags,
	                         sApphints.ui32LogType,
	                         GetFilterFlags(&sApphints),
	                         sApphints.ui32JonesDisableMask,
	                         sApphints.ui32HWRDebugDumpLimit,
#if defined(SUPPORT_VALIDATION)
	                         sApphints.ui32RenderKillingCtl,
	                         sApphints.ui32CDMTDMKillingCtl,
	                         &sApphints.aui32TPUTrilinearFracMask[0],
	                         &sApphints.aui32USRMNumRegions[0],
	                         (IMG_PUINT64)&sApphints.aui64UVBRMNumRegions[0],
#else
	                         0, 0,
	                         NULL, NULL, NULL,
#endif
	                         ui32HWPerfBlkSize,
	                         sApphints.eRGXRDPowerIslandConf,
							 sApphints.bSPUClockGating,
	                         sApphints.eFirmwarePerf,
	                         sApphints.ui32KCCBSizeLog2,
	                         ui32FWConfigFlagsExt,
	                         ui32AvailablePowUnitsMask,
							 ui32AvailableRACMask,
	                         ui32FwOsCfgFlags);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRVRGXInitFirmware failed (%d)",
		        __func__,
		        eError));
		goto cleanup;
	}

	if (!PVRSRV_VZ_MODE_IS(GUEST))
	{
		eError = InitialiseAllCounters(psDeviceNode);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
			        "%s: InitialiseAllCounters failed (%d)",
			        __func__,
			        eError));
			goto cleanup;
		}
	}

	/*
	 * Perform second stage of RGX initialisation
	 */
	eError = RGXInitDevPart2(psDeviceNode,
	                         ui32DeviceFlags,
	                         sApphints.ui32HWPerfHostFilter,
	                         sApphints.eRGXActivePMConf,
	                         ui32AvailablePowUnitsMask,
							 ui32AvailableRACMask);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "%s: PVRSRVRGXInitDevPart2KM failed (%d)",
		        __func__,
		        eError));
		goto cleanup;
	}

#if defined(SUPPORT_VALIDATION)
	PVRSRVAppHintDumpState(psDeviceNode);
#endif

	eError = PVRSRV_OK;

cleanup:
	return eError;
}

/******************************************************************************
 End of file (rgxsrvinit.c)
******************************************************************************/
