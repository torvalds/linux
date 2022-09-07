/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#define SWSMU_CODE_LAYER_L2

#include <linux/firmware.h>
#include <linux/pci.h>
#include <linux/i2c.h>
#include "amdgpu.h"
#include "amdgpu_dpm.h"
#include "amdgpu_smu.h"
#include "atomfirmware.h"
#include "amdgpu_atomfirmware.h"
#include "amdgpu_atombios.h"
#include "smu_v11_0.h"
#include "smu11_driver_if_sienna_cichlid.h"
#include "soc15_common.h"
#include "atom.h"
#include "sienna_cichlid_ppt.h"
#include "smu_v11_0_7_pptable.h"
#include "smu_v11_0_7_ppsmc.h"
#include "nbio/nbio_2_3_offset.h"
#include "nbio/nbio_2_3_sh_mask.h"
#include "thm/thm_11_0_2_offset.h"
#include "thm/thm_11_0_2_sh_mask.h"
#include "mp/mp_11_0_offset.h"
#include "mp/mp_11_0_sh_mask.h"

#include "asic_reg/mp/mp_11_0_sh_mask.h"
#include "amdgpu_ras.h"
#include "smu_cmn.h"

/*
 * DO NOT use these for err/warn/info/debug messages.
 * Use dev_err, dev_warn, dev_info and dev_dbg instead.
 * They are more MGPU friendly.
 */
#undef pr_err
#undef pr_warn
#undef pr_info
#undef pr_debug

#define FEATURE_MASK(feature) (1ULL << feature)
#define SMC_DPM_FEATURE ( \
	FEATURE_MASK(FEATURE_DPM_PREFETCHER_BIT) | \
	FEATURE_MASK(FEATURE_DPM_GFXCLK_BIT)     | \
	FEATURE_MASK(FEATURE_DPM_UCLK_BIT)	 | \
	FEATURE_MASK(FEATURE_DPM_LINK_BIT)       | \
	FEATURE_MASK(FEATURE_DPM_SOCCLK_BIT)     | \
	FEATURE_MASK(FEATURE_DPM_FCLK_BIT)	 | \
	FEATURE_MASK(FEATURE_DPM_DCEFCLK_BIT)	 | \
	FEATURE_MASK(FEATURE_DPM_MP0CLK_BIT))

#define SMU_11_0_7_GFX_BUSY_THRESHOLD 15

#define GET_PPTABLE_MEMBER(field, member) do {\
	if (smu->adev->ip_versions[MP1_HWIP][0] == IP_VERSION(11, 0, 13))\
		(*member) = (smu->smu_table.driver_pptable + offsetof(PPTable_beige_goby_t, field));\
	else\
		(*member) = (smu->smu_table.driver_pptable + offsetof(PPTable_t, field));\
} while(0)

/* STB FIFO depth is in 64bit units */
#define SIENNA_CICHLID_STB_DEPTH_UNIT_BYTES 8

/*
 * SMU support ECCTABLE since version 58.70.0,
 * use this to check whether ECCTABLE feature is supported.
 */
#define SUPPORT_ECCTABLE_SMU_VERSION 0x003a4600

static int get_table_size(struct smu_context *smu)
{
	if (smu->adev->ip_versions[MP1_HWIP][0] == IP_VERSION(11, 0, 13))
		return sizeof(PPTable_beige_goby_t);
	else
		return sizeof(PPTable_t);
}

static struct cmn2asic_msg_mapping sienna_cichlid_message_map[SMU_MSG_MAX_COUNT] = {
	MSG_MAP(TestMessage,			PPSMC_MSG_TestMessage,                 1),
	MSG_MAP(GetSmuVersion,			PPSMC_MSG_GetSmuVersion,               1),
	MSG_MAP(GetDriverIfVersion,		PPSMC_MSG_GetDriverIfVersion,          1),
	MSG_MAP(SetAllowedFeaturesMaskLow,	PPSMC_MSG_SetAllowedFeaturesMaskLow,   0),
	MSG_MAP(SetAllowedFeaturesMaskHigh,	PPSMC_MSG_SetAllowedFeaturesMaskHigh,  0),
	MSG_MAP(EnableAllSmuFeatures,		PPSMC_MSG_EnableAllSmuFeatures,        0),
	MSG_MAP(DisableAllSmuFeatures,		PPSMC_MSG_DisableAllSmuFeatures,       0),
	MSG_MAP(EnableSmuFeaturesLow,		PPSMC_MSG_EnableSmuFeaturesLow,        1),
	MSG_MAP(EnableSmuFeaturesHigh,		PPSMC_MSG_EnableSmuFeaturesHigh,       1),
	MSG_MAP(DisableSmuFeaturesLow,		PPSMC_MSG_DisableSmuFeaturesLow,       1),
	MSG_MAP(DisableSmuFeaturesHigh,		PPSMC_MSG_DisableSmuFeaturesHigh,      1),
	MSG_MAP(GetEnabledSmuFeaturesLow,       PPSMC_MSG_GetRunningSmuFeaturesLow,    1),
	MSG_MAP(GetEnabledSmuFeaturesHigh,	PPSMC_MSG_GetRunningSmuFeaturesHigh,   1),
	MSG_MAP(SetWorkloadMask,		PPSMC_MSG_SetWorkloadMask,             1),
	MSG_MAP(SetPptLimit,			PPSMC_MSG_SetPptLimit,                 0),
	MSG_MAP(SetDriverDramAddrHigh,		PPSMC_MSG_SetDriverDramAddrHigh,       1),
	MSG_MAP(SetDriverDramAddrLow,		PPSMC_MSG_SetDriverDramAddrLow,        1),
	MSG_MAP(SetToolsDramAddrHigh,		PPSMC_MSG_SetToolsDramAddrHigh,        0),
	MSG_MAP(SetToolsDramAddrLow,		PPSMC_MSG_SetToolsDramAddrLow,         0),
	MSG_MAP(TransferTableSmu2Dram,		PPSMC_MSG_TransferTableSmu2Dram,       1),
	MSG_MAP(TransferTableDram2Smu,		PPSMC_MSG_TransferTableDram2Smu,       0),
	MSG_MAP(UseDefaultPPTable,		PPSMC_MSG_UseDefaultPPTable,           0),
	MSG_MAP(RunDcBtc,			PPSMC_MSG_RunDcBtc,                    0),
	MSG_MAP(EnterBaco,			PPSMC_MSG_EnterBaco,                   0),
	MSG_MAP(SetSoftMinByFreq,		PPSMC_MSG_SetSoftMinByFreq,            1),
	MSG_MAP(SetSoftMaxByFreq,		PPSMC_MSG_SetSoftMaxByFreq,            1),
	MSG_MAP(SetHardMinByFreq,		PPSMC_MSG_SetHardMinByFreq,            1),
	MSG_MAP(SetHardMaxByFreq,		PPSMC_MSG_SetHardMaxByFreq,            0),
	MSG_MAP(GetMinDpmFreq,			PPSMC_MSG_GetMinDpmFreq,               1),
	MSG_MAP(GetMaxDpmFreq,			PPSMC_MSG_GetMaxDpmFreq,               1),
	MSG_MAP(GetDpmFreqByIndex,		PPSMC_MSG_GetDpmFreqByIndex,           1),
	MSG_MAP(SetGeminiMode,			PPSMC_MSG_SetGeminiMode,               0),
	MSG_MAP(SetGeminiApertureHigh,		PPSMC_MSG_SetGeminiApertureHigh,       0),
	MSG_MAP(SetGeminiApertureLow,		PPSMC_MSG_SetGeminiApertureLow,        0),
	MSG_MAP(OverridePcieParameters,		PPSMC_MSG_OverridePcieParameters,      0),
	MSG_MAP(ReenableAcDcInterrupt,		PPSMC_MSG_ReenableAcDcInterrupt,       0),
	MSG_MAP(NotifyPowerSource,		PPSMC_MSG_NotifyPowerSource,           0),
	MSG_MAP(SetUclkFastSwitch,		PPSMC_MSG_SetUclkFastSwitch,           0),
	MSG_MAP(SetVideoFps,			PPSMC_MSG_SetVideoFps,                 0),
	MSG_MAP(PrepareMp1ForUnload,		PPSMC_MSG_PrepareMp1ForUnload,         1),
	MSG_MAP(AllowGfxOff,			PPSMC_MSG_AllowGfxOff,                 0),
	MSG_MAP(DisallowGfxOff,			PPSMC_MSG_DisallowGfxOff,              0),
	MSG_MAP(GetPptLimit,			PPSMC_MSG_GetPptLimit,                 0),
	MSG_MAP(GetDcModeMaxDpmFreq,		PPSMC_MSG_GetDcModeMaxDpmFreq,         1),
	MSG_MAP(ExitBaco,			PPSMC_MSG_ExitBaco,                    0),
	MSG_MAP(PowerUpVcn,			PPSMC_MSG_PowerUpVcn,                  0),
	MSG_MAP(PowerDownVcn,			PPSMC_MSG_PowerDownVcn,                0),
	MSG_MAP(PowerUpJpeg,			PPSMC_MSG_PowerUpJpeg,                 0),
	MSG_MAP(PowerDownJpeg,			PPSMC_MSG_PowerDownJpeg,               0),
	MSG_MAP(BacoAudioD3PME,			PPSMC_MSG_BacoAudioD3PME,              0),
	MSG_MAP(ArmD3,				PPSMC_MSG_ArmD3,                       0),
	MSG_MAP(Mode1Reset,                     PPSMC_MSG_Mode1Reset,		       0),
	MSG_MAP(SetMGpuFanBoostLimitRpm,	PPSMC_MSG_SetMGpuFanBoostLimitRpm,     0),
	MSG_MAP(SetGpoFeaturePMask,		PPSMC_MSG_SetGpoFeaturePMask,          0),
	MSG_MAP(DisallowGpo,			PPSMC_MSG_DisallowGpo,                 0),
	MSG_MAP(Enable2ndUSB20Port,		PPSMC_MSG_Enable2ndUSB20Port,          0),
};

static struct cmn2asic_mapping sienna_cichlid_clk_map[SMU_CLK_COUNT] = {
	CLK_MAP(GFXCLK,		PPCLK_GFXCLK),
	CLK_MAP(SCLK,		PPCLK_GFXCLK),
	CLK_MAP(SOCCLK,		PPCLK_SOCCLK),
	CLK_MAP(FCLK,		PPCLK_FCLK),
	CLK_MAP(UCLK,		PPCLK_UCLK),
	CLK_MAP(MCLK,		PPCLK_UCLK),
	CLK_MAP(DCLK,		PPCLK_DCLK_0),
	CLK_MAP(DCLK1,		PPCLK_DCLK_1),
	CLK_MAP(VCLK,		PPCLK_VCLK_0),
	CLK_MAP(VCLK1,		PPCLK_VCLK_1),
	CLK_MAP(DCEFCLK,	PPCLK_DCEFCLK),
	CLK_MAP(DISPCLK,	PPCLK_DISPCLK),
	CLK_MAP(PIXCLK,		PPCLK_PIXCLK),
	CLK_MAP(PHYCLK,		PPCLK_PHYCLK),
};

static struct cmn2asic_mapping sienna_cichlid_feature_mask_map[SMU_FEATURE_COUNT] = {
	FEA_MAP(DPM_PREFETCHER),
	FEA_MAP(DPM_GFXCLK),
	FEA_MAP(DPM_GFX_GPO),
	FEA_MAP(DPM_UCLK),
	FEA_MAP(DPM_FCLK),
	FEA_MAP(DPM_SOCCLK),
	FEA_MAP(DPM_MP0CLK),
	FEA_MAP(DPM_LINK),
	FEA_MAP(DPM_DCEFCLK),
	FEA_MAP(DPM_XGMI),
	FEA_MAP(MEM_VDDCI_SCALING),
	FEA_MAP(MEM_MVDD_SCALING),
	FEA_MAP(DS_GFXCLK),
	FEA_MAP(DS_SOCCLK),
	FEA_MAP(DS_FCLK),
	FEA_MAP(DS_LCLK),
	FEA_MAP(DS_DCEFCLK),
	FEA_MAP(DS_UCLK),
	FEA_MAP(GFX_ULV),
	FEA_MAP(FW_DSTATE),
	FEA_MAP(GFXOFF),
	FEA_MAP(BACO),
	FEA_MAP(MM_DPM_PG),
	FEA_MAP(RSMU_SMN_CG),
	FEA_MAP(PPT),
	FEA_MAP(TDC),
	FEA_MAP(APCC_PLUS),
	FEA_MAP(GTHR),
	FEA_MAP(ACDC),
	FEA_MAP(VR0HOT),
	FEA_MAP(VR1HOT),
	FEA_MAP(FW_CTF),
	FEA_MAP(FAN_CONTROL),
	FEA_MAP(THERMAL),
	FEA_MAP(GFX_DCS),
	FEA_MAP(RM),
	FEA_MAP(LED_DISPLAY),
	FEA_MAP(GFX_SS),
	FEA_MAP(OUT_OF_BAND_MONITOR),
	FEA_MAP(TEMP_DEPENDENT_VMIN),
	FEA_MAP(MMHUB_PG),
	FEA_MAP(ATHUB_PG),
	FEA_MAP(APCC_DFLL),
};

static struct cmn2asic_mapping sienna_cichlid_table_map[SMU_TABLE_COUNT] = {
	TAB_MAP(PPTABLE),
	TAB_MAP(WATERMARKS),
	TAB_MAP(AVFS_PSM_DEBUG),
	TAB_MAP(AVFS_FUSE_OVERRIDE),
	TAB_MAP(PMSTATUSLOG),
	TAB_MAP(SMU_METRICS),
	TAB_MAP(DRIVER_SMU_CONFIG),
	TAB_MAP(ACTIVITY_MONITOR_COEFF),
	TAB_MAP(OVERDRIVE),
	TAB_MAP(I2C_COMMANDS),
	TAB_MAP(PACE),
	TAB_MAP(ECCINFO),
};

static struct cmn2asic_mapping sienna_cichlid_pwr_src_map[SMU_POWER_SOURCE_COUNT] = {
	PWR_MAP(AC),
	PWR_MAP(DC),
};

static struct cmn2asic_mapping sienna_cichlid_workload_map[PP_SMC_POWER_PROFILE_COUNT] = {
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT,	WORKLOAD_PPLIB_DEFAULT_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_FULLSCREEN3D,		WORKLOAD_PPLIB_FULL_SCREEN_3D_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_POWERSAVING,		WORKLOAD_PPLIB_POWER_SAVING_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_VIDEO,		WORKLOAD_PPLIB_VIDEO_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_VR,			WORKLOAD_PPLIB_VR_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_COMPUTE,		WORKLOAD_PPLIB_COMPUTE_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_CUSTOM,		WORKLOAD_PPLIB_CUSTOM_BIT),
};

static const uint8_t sienna_cichlid_throttler_map[] = {
	[THROTTLER_TEMP_EDGE_BIT]	= (SMU_THROTTLER_TEMP_EDGE_BIT),
	[THROTTLER_TEMP_HOTSPOT_BIT]	= (SMU_THROTTLER_TEMP_HOTSPOT_BIT),
	[THROTTLER_TEMP_MEM_BIT]	= (SMU_THROTTLER_TEMP_MEM_BIT),
	[THROTTLER_TEMP_VR_GFX_BIT]	= (SMU_THROTTLER_TEMP_VR_GFX_BIT),
	[THROTTLER_TEMP_VR_MEM0_BIT]	= (SMU_THROTTLER_TEMP_VR_MEM0_BIT),
	[THROTTLER_TEMP_VR_MEM1_BIT]	= (SMU_THROTTLER_TEMP_VR_MEM1_BIT),
	[THROTTLER_TEMP_VR_SOC_BIT]	= (SMU_THROTTLER_TEMP_VR_SOC_BIT),
	[THROTTLER_TEMP_LIQUID0_BIT]	= (SMU_THROTTLER_TEMP_LIQUID0_BIT),
	[THROTTLER_TEMP_LIQUID1_BIT]	= (SMU_THROTTLER_TEMP_LIQUID1_BIT),
	[THROTTLER_TDC_GFX_BIT]		= (SMU_THROTTLER_TDC_GFX_BIT),
	[THROTTLER_TDC_SOC_BIT]		= (SMU_THROTTLER_TDC_SOC_BIT),
	[THROTTLER_PPT0_BIT]		= (SMU_THROTTLER_PPT0_BIT),
	[THROTTLER_PPT1_BIT]		= (SMU_THROTTLER_PPT1_BIT),
	[THROTTLER_PPT2_BIT]		= (SMU_THROTTLER_PPT2_BIT),
	[THROTTLER_PPT3_BIT]		= (SMU_THROTTLER_PPT3_BIT),
	[THROTTLER_FIT_BIT]		= (SMU_THROTTLER_FIT_BIT),
	[THROTTLER_PPM_BIT]		= (SMU_THROTTLER_PPM_BIT),
	[THROTTLER_APCC_BIT]		= (SMU_THROTTLER_APCC_BIT),
};

static int
sienna_cichlid_get_allowed_feature_mask(struct smu_context *smu,
				  uint32_t *feature_mask, uint32_t num)
{
	struct amdgpu_device *adev = smu->adev;

	if (num > 2)
		return -EINVAL;

	memset(feature_mask, 0, sizeof(uint32_t) * num);

	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_PREFETCHER_BIT)
				| FEATURE_MASK(FEATURE_DPM_FCLK_BIT)
				| FEATURE_MASK(FEATURE_DPM_MP0CLK_BIT)
				| FEATURE_MASK(FEATURE_DS_SOCCLK_BIT)
				| FEATURE_MASK(FEATURE_DS_DCEFCLK_BIT)
				| FEATURE_MASK(FEATURE_DS_FCLK_BIT)
				| FEATURE_MASK(FEATURE_DS_UCLK_BIT)
				| FEATURE_MASK(FEATURE_FW_DSTATE_BIT)
				| FEATURE_MASK(FEATURE_DF_CSTATE_BIT)
				| FEATURE_MASK(FEATURE_RSMU_SMN_CG_BIT)
				| FEATURE_MASK(FEATURE_GFX_SS_BIT)
				| FEATURE_MASK(FEATURE_VR0HOT_BIT)
				| FEATURE_MASK(FEATURE_PPT_BIT)
				| FEATURE_MASK(FEATURE_TDC_BIT)
				| FEATURE_MASK(FEATURE_BACO_BIT)
				| FEATURE_MASK(FEATURE_APCC_DFLL_BIT)
				| FEATURE_MASK(FEATURE_FW_CTF_BIT)
				| FEATURE_MASK(FEATURE_FAN_CONTROL_BIT)
				| FEATURE_MASK(FEATURE_THERMAL_BIT)
				| FEATURE_MASK(FEATURE_OUT_OF_BAND_MONITOR_BIT);

	if (adev->pm.pp_feature & PP_SCLK_DPM_MASK) {
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_GFXCLK_BIT);
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_GFX_GPO_BIT);
	}

	if ((adev->pm.pp_feature & PP_GFX_DCS_MASK) &&
	    (adev->ip_versions[MP1_HWIP][0] > IP_VERSION(11, 0, 7)) &&
	    !(adev->flags & AMD_IS_APU))
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_GFX_DCS_BIT);

	if (adev->pm.pp_feature & PP_MCLK_DPM_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_UCLK_BIT)
					| FEATURE_MASK(FEATURE_MEM_VDDCI_SCALING_BIT)
					| FEATURE_MASK(FEATURE_MEM_MVDD_SCALING_BIT);

	if (adev->pm.pp_feature & PP_PCIE_DPM_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_LINK_BIT);

	if (adev->pm.pp_feature & PP_DCEFCLK_DPM_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_DCEFCLK_BIT);

	if (adev->pm.pp_feature & PP_SOCCLK_DPM_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_SOCCLK_BIT);

	if (adev->pm.pp_feature & PP_ULV_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_GFX_ULV_BIT);

	if (adev->pm.pp_feature & PP_SCLK_DEEP_SLEEP_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DS_GFXCLK_BIT);

	if (adev->pm.pp_feature & PP_GFXOFF_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_GFXOFF_BIT);

	if (smu->adev->pg_flags & AMD_PG_SUPPORT_ATHUB)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_ATHUB_PG_BIT);

	if (smu->adev->pg_flags & AMD_PG_SUPPORT_MMHUB)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_MMHUB_PG_BIT);

	if (smu->adev->pg_flags & AMD_PG_SUPPORT_VCN ||
	    smu->adev->pg_flags & AMD_PG_SUPPORT_JPEG)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_MM_DPM_PG_BIT);

	if (smu->dc_controlled_by_gpio)
       *(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_ACDC_BIT);

	if (amdgpu_device_should_use_aspm(adev))
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DS_LCLK_BIT);

	return 0;
}

static void sienna_cichlid_check_bxco_support(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_11_0_7_powerplay_table *powerplay_table =
		table_context->power_play_table;
	struct smu_baco_context *smu_baco = &smu->smu_baco;
	struct amdgpu_device *adev = smu->adev;
	uint32_t val;

	if (powerplay_table->platform_caps & SMU_11_0_7_PP_PLATFORM_CAP_BACO) {
		val = RREG32_SOC15(NBIO, 0, mmRCC_BIF_STRAP0);
		smu_baco->platform_support =
			(val & RCC_BIF_STRAP0__STRAP_PX_CAPABLE_MASK) ? true :
									false;

		/*
		 * Disable BACO entry/exit completely on below SKUs to
		 * avoid hardware intermittent failures.
		 */
		if (((adev->pdev->device == 0x73A1) &&
		    (adev->pdev->revision == 0x00)) ||
		    ((adev->pdev->device == 0x73BF) &&
		    (adev->pdev->revision == 0xCF)))
			smu_baco->platform_support = false;

	}
}

static void sienna_cichlid_check_fan_support(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *pptable = table_context->driver_pptable;
	uint64_t features = *(uint64_t *) pptable->FeaturesToRun;

	/* Fan control is not possible if PPTable has it disabled */
	smu->adev->pm.no_fan =
		!(features & (1ULL << FEATURE_FAN_CONTROL_BIT));
	if (smu->adev->pm.no_fan)
		dev_info_once(smu->adev->dev,
			      "PMFW based fan control disabled");
}

static int sienna_cichlid_check_powerplay_table(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_11_0_7_powerplay_table *powerplay_table =
		table_context->power_play_table;

	if (powerplay_table->platform_caps & SMU_11_0_7_PP_PLATFORM_CAP_HARDWAREDC)
		smu->dc_controlled_by_gpio = true;

	sienna_cichlid_check_bxco_support(smu);
	sienna_cichlid_check_fan_support(smu);

	table_context->thermal_controller_type =
		powerplay_table->thermal_controller_type;

	/*
	 * Instead of having its own buffer space and get overdrive_table copied,
	 * smu->od_settings just points to the actual overdrive_table
	 */
	smu->od_settings = &powerplay_table->overdrive_table;

	return 0;
}

static int sienna_cichlid_append_powerplay_table(struct smu_context *smu)
{
	struct atom_smc_dpm_info_v4_9 *smc_dpm_table;
	int index, ret;
	I2cControllerConfig_t *table_member;

	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
					    smc_dpm_info);

	ret = amdgpu_atombios_get_data_table(smu->adev, index, NULL, NULL, NULL,
				      (uint8_t **)&smc_dpm_table);
	if (ret)
		return ret;
	GET_PPTABLE_MEMBER(I2cControllers, &table_member);
	memcpy(table_member, smc_dpm_table->I2cControllers,
			sizeof(*smc_dpm_table) - sizeof(smc_dpm_table->table_header));

	return 0;
}

static int sienna_cichlid_store_powerplay_table(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_11_0_7_powerplay_table *powerplay_table =
		table_context->power_play_table;
	int table_size;

	table_size = get_table_size(smu);
	memcpy(table_context->driver_pptable, &powerplay_table->smc_pptable,
	       table_size);

	return 0;
}

static int sienna_cichlid_patch_pptable_quirk(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t *board_reserved;
	uint16_t *freq_table_gfx;
	uint32_t i;

	/* Fix some OEM SKU specific stability issues */
	GET_PPTABLE_MEMBER(BoardReserved, &board_reserved);
	if ((adev->pdev->device == 0x73DF) &&
	    (adev->pdev->revision == 0XC3) &&
	    (adev->pdev->subsystem_device == 0x16C2) &&
	    (adev->pdev->subsystem_vendor == 0x1043))
		board_reserved[0] = 1387;

	GET_PPTABLE_MEMBER(FreqTableGfx, &freq_table_gfx);
	if ((adev->pdev->device == 0x73DF) &&
	    (adev->pdev->revision == 0XC3) &&
	    ((adev->pdev->subsystem_device == 0x16C2) ||
	    (adev->pdev->subsystem_device == 0x133C)) &&
	    (adev->pdev->subsystem_vendor == 0x1043)) {
		for (i = 0; i < NUM_GFXCLK_DPM_LEVELS; i++) {
			if (freq_table_gfx[i] > 2500)
				freq_table_gfx[i] = 2500;
		}
	}

	return 0;
}

static int sienna_cichlid_setup_pptable(struct smu_context *smu)
{
	int ret = 0;

	ret = smu_v11_0_setup_pptable(smu);
	if (ret)
		return ret;

	ret = sienna_cichlid_store_powerplay_table(smu);
	if (ret)
		return ret;

	ret = sienna_cichlid_append_powerplay_table(smu);
	if (ret)
		return ret;

	ret = sienna_cichlid_check_powerplay_table(smu);
	if (ret)
		return ret;

	return sienna_cichlid_patch_pptable_quirk(smu);
}

static int sienna_cichlid_tables_init(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = smu_table->tables;
	int table_size;

	table_size = get_table_size(smu);
	SMU_TABLE_INIT(tables, SMU_TABLE_PPTABLE, table_size,
			       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_WATERMARKS, sizeof(Watermarks_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_SMU_METRICS, sizeof(SmuMetricsExternal_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_I2C_COMMANDS, sizeof(SwI2cRequest_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_OVERDRIVE, sizeof(OverDriveTable_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_PMSTATUSLOG, SMU11_TOOL_SIZE,
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_ACTIVITY_MONITOR_COEFF,
		       sizeof(DpmActivityMonitorCoeffIntExternal_t), PAGE_SIZE,
	               AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_ECCINFO, sizeof(EccInfoTable_t),
			PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_DRIVER_SMU_CONFIG, sizeof(DriverSmuConfigExternal_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);

	smu_table->metrics_table = kzalloc(sizeof(SmuMetricsExternal_t), GFP_KERNEL);
	if (!smu_table->metrics_table)
		goto err0_out;
	smu_table->metrics_time = 0;

	smu_table->gpu_metrics_table_size = sizeof(struct gpu_metrics_v1_3);
	smu_table->gpu_metrics_table = kzalloc(smu_table->gpu_metrics_table_size, GFP_KERNEL);
	if (!smu_table->gpu_metrics_table)
		goto err1_out;

	smu_table->watermarks_table = kzalloc(sizeof(Watermarks_t), GFP_KERNEL);
	if (!smu_table->watermarks_table)
		goto err2_out;

	smu_table->ecc_table = kzalloc(tables[SMU_TABLE_ECCINFO].size, GFP_KERNEL);
	if (!smu_table->ecc_table)
		goto err3_out;

	smu_table->driver_smu_config_table =
		kzalloc(tables[SMU_TABLE_DRIVER_SMU_CONFIG].size, GFP_KERNEL);
	if (!smu_table->driver_smu_config_table)
		goto err4_out;

	return 0;

err4_out:
	kfree(smu_table->ecc_table);
err3_out:
	kfree(smu_table->watermarks_table);
err2_out:
	kfree(smu_table->gpu_metrics_table);
err1_out:
	kfree(smu_table->metrics_table);
err0_out:
	return -ENOMEM;
}

static uint32_t sienna_cichlid_get_throttler_status_locked(struct smu_context *smu)
{
	struct smu_table_context *smu_table= &smu->smu_table;
	SmuMetricsExternal_t *metrics_ext =
		(SmuMetricsExternal_t *)(smu_table->metrics_table);
	uint32_t throttler_status = 0;
	int i;

	if ((smu->adev->ip_versions[MP1_HWIP][0] == IP_VERSION(11, 0, 7)) &&
	     (smu->smc_fw_version >= 0x3A4900)) {
		for (i = 0; i < THROTTLER_COUNT; i++)
			throttler_status |=
				(metrics_ext->SmuMetrics_V3.ThrottlingPercentage[i] ? 1U << i : 0);
	} else if ((smu->adev->ip_versions[MP1_HWIP][0] == IP_VERSION(11, 0, 7)) &&
	     (smu->smc_fw_version >= 0x3A4300)) {
		for (i = 0; i < THROTTLER_COUNT; i++)
			throttler_status |=
				(metrics_ext->SmuMetrics_V2.ThrottlingPercentage[i] ? 1U << i : 0);
	} else {
		throttler_status = metrics_ext->SmuMetrics.ThrottlerStatus;
	}

	return throttler_status;
}

static int sienna_cichlid_get_power_limit(struct smu_context *smu,
					  uint32_t *current_power_limit,
					  uint32_t *default_power_limit,
					  uint32_t *max_power_limit)
{
	struct smu_11_0_7_powerplay_table *powerplay_table =
		(struct smu_11_0_7_powerplay_table *)smu->smu_table.power_play_table;
	uint32_t power_limit, od_percent;
	uint16_t *table_member;

	GET_PPTABLE_MEMBER(SocketPowerLimitAc, &table_member);

	if (smu_v11_0_get_current_power_limit(smu, &power_limit)) {
		power_limit =
			table_member[PPT_THROTTLER_PPT0];
	}

	if (current_power_limit)
		*current_power_limit = power_limit;
	if (default_power_limit)
		*default_power_limit = power_limit;

	if (max_power_limit) {
		if (smu->od_enabled) {
			od_percent =
				le32_to_cpu(powerplay_table->overdrive_table.max[
							SMU_11_0_7_ODSETTING_POWERPERCENTAGE]);

			dev_dbg(smu->adev->dev, "ODSETTING_POWERPERCENTAGE: %d (default: %d)\n",
					od_percent, power_limit);

			power_limit *= (100 + od_percent);
			power_limit /= 100;
		}
		*max_power_limit = power_limit;
	}

	return 0;
}

static void sienna_cichlid_get_smartshift_power_percentage(struct smu_context *smu,
					uint32_t *apu_percent,
					uint32_t *dgpu_percent)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	SmuMetrics_V4_t *metrics_v4 =
		&(((SmuMetricsExternal_t *)(smu_table->metrics_table))->SmuMetrics_V4);
	uint16_t powerRatio = 0;
	uint16_t apu_power_limit = 0;
	uint16_t dgpu_power_limit = 0;
	uint32_t apu_boost = 0;
	uint32_t dgpu_boost = 0;
	uint32_t cur_power_limit;

	if (metrics_v4->ApuSTAPMSmartShiftLimit != 0) {
		sienna_cichlid_get_power_limit(smu, &cur_power_limit, NULL, NULL);
		apu_power_limit = metrics_v4->ApuSTAPMLimit;
		dgpu_power_limit = cur_power_limit;
		powerRatio = (((apu_power_limit +
						  dgpu_power_limit) * 100) /
						  metrics_v4->ApuSTAPMSmartShiftLimit);
		if (powerRatio > 100) {
			apu_power_limit = (apu_power_limit * 100) /
									 powerRatio;
			dgpu_power_limit = (dgpu_power_limit * 100) /
									  powerRatio;
		}
		if (metrics_v4->AverageApuSocketPower > apu_power_limit &&
			 apu_power_limit != 0) {
			apu_boost = ((metrics_v4->AverageApuSocketPower -
							apu_power_limit) * 100) /
							apu_power_limit;
			if (apu_boost > 100)
				apu_boost = 100;
		}

		if (metrics_v4->AverageSocketPower > dgpu_power_limit &&
			 dgpu_power_limit != 0) {
			dgpu_boost = ((metrics_v4->AverageSocketPower -
							 dgpu_power_limit) * 100) /
							 dgpu_power_limit;
			if (dgpu_boost > 100)
				dgpu_boost = 100;
		}

		if (dgpu_boost >= apu_boost)
			apu_boost = 0;
		else
			dgpu_boost = 0;
	}
	*apu_percent = apu_boost;
	*dgpu_percent = dgpu_boost;
}

static int sienna_cichlid_get_smu_metrics_data(struct smu_context *smu,
					       MetricsMember_t member,
					       uint32_t *value)
{
	struct smu_table_context *smu_table= &smu->smu_table;
	SmuMetrics_t *metrics =
		&(((SmuMetricsExternal_t *)(smu_table->metrics_table))->SmuMetrics);
	SmuMetrics_V2_t *metrics_v2 =
		&(((SmuMetricsExternal_t *)(smu_table->metrics_table))->SmuMetrics_V2);
	SmuMetrics_V3_t *metrics_v3 =
		&(((SmuMetricsExternal_t *)(smu_table->metrics_table))->SmuMetrics_V3);
	bool use_metrics_v2 = false;
	bool use_metrics_v3 = false;
	uint16_t average_gfx_activity;
	int ret = 0;
	uint32_t apu_percent = 0;
	uint32_t dgpu_percent = 0;

	switch (smu->adev->ip_versions[MP1_HWIP][0]) {
	case IP_VERSION(11, 0, 7):
		if (smu->smc_fw_version >= 0x3A4900)
			use_metrics_v3 = true;
		else if (smu->smc_fw_version >= 0x3A4300)
			use_metrics_v2 = true;
		break;
	case IP_VERSION(11, 0, 11):
		if (smu->smc_fw_version >= 0x412D00)
			use_metrics_v2 = true;
		break;
	case IP_VERSION(11, 0, 12):
		if (smu->smc_fw_version >= 0x3B2300)
			use_metrics_v2 = true;
		break;
	case IP_VERSION(11, 0, 13):
		if (smu->smc_fw_version >= 0x491100)
			use_metrics_v2 = true;
		break;
	default:
		break;
	}

	ret = smu_cmn_get_metrics_table(smu,
					NULL,
					false);
	if (ret)
		return ret;

	switch (member) {
	case METRICS_CURR_GFXCLK:
		*value = use_metrics_v3 ? metrics_v3->CurrClock[PPCLK_GFXCLK] :
			use_metrics_v2 ? metrics_v2->CurrClock[PPCLK_GFXCLK] :
			metrics->CurrClock[PPCLK_GFXCLK];
		break;
	case METRICS_CURR_SOCCLK:
		*value = use_metrics_v3 ? metrics_v3->CurrClock[PPCLK_SOCCLK] :
			use_metrics_v2 ? metrics_v2->CurrClock[PPCLK_SOCCLK] :
			metrics->CurrClock[PPCLK_SOCCLK];
		break;
	case METRICS_CURR_UCLK:
		*value = use_metrics_v3 ? metrics_v3->CurrClock[PPCLK_UCLK] :
			use_metrics_v2 ? metrics_v2->CurrClock[PPCLK_UCLK] :
			metrics->CurrClock[PPCLK_UCLK];
		break;
	case METRICS_CURR_VCLK:
		*value = use_metrics_v3 ? metrics_v3->CurrClock[PPCLK_VCLK_0] :
			use_metrics_v2 ? metrics_v2->CurrClock[PPCLK_VCLK_0] :
			metrics->CurrClock[PPCLK_VCLK_0];
		break;
	case METRICS_CURR_VCLK1:
		*value = use_metrics_v3 ? metrics_v3->CurrClock[PPCLK_VCLK_1] :
			use_metrics_v2 ? metrics_v2->CurrClock[PPCLK_VCLK_1] :
			metrics->CurrClock[PPCLK_VCLK_1];
		break;
	case METRICS_CURR_DCLK:
		*value = use_metrics_v3 ? metrics_v3->CurrClock[PPCLK_DCLK_0] :
			use_metrics_v2 ? metrics_v2->CurrClock[PPCLK_DCLK_0] :
			metrics->CurrClock[PPCLK_DCLK_0];
		break;
	case METRICS_CURR_DCLK1:
		*value = use_metrics_v3 ? metrics_v3->CurrClock[PPCLK_DCLK_1] :
			use_metrics_v2 ? metrics_v2->CurrClock[PPCLK_DCLK_1] :
			metrics->CurrClock[PPCLK_DCLK_1];
		break;
	case METRICS_CURR_DCEFCLK:
		*value = use_metrics_v3 ? metrics_v3->CurrClock[PPCLK_DCEFCLK] :
			use_metrics_v2 ? metrics_v2->CurrClock[PPCLK_DCEFCLK] :
			metrics->CurrClock[PPCLK_DCEFCLK];
		break;
	case METRICS_CURR_FCLK:
		*value = use_metrics_v3 ? metrics_v3->CurrClock[PPCLK_FCLK] :
			use_metrics_v2 ? metrics_v2->CurrClock[PPCLK_FCLK] :
			metrics->CurrClock[PPCLK_FCLK];
		break;
	case METRICS_AVERAGE_GFXCLK:
		average_gfx_activity = use_metrics_v3 ? metrics_v3->AverageGfxActivity :
			use_metrics_v2 ? metrics_v2->AverageGfxActivity :
			metrics->AverageGfxActivity;
		if (average_gfx_activity <= SMU_11_0_7_GFX_BUSY_THRESHOLD)
			*value = use_metrics_v3 ? metrics_v3->AverageGfxclkFrequencyPostDs :
				use_metrics_v2 ? metrics_v2->AverageGfxclkFrequencyPostDs :
				metrics->AverageGfxclkFrequencyPostDs;
		else
			*value = use_metrics_v3 ? metrics_v3->AverageGfxclkFrequencyPreDs :
				use_metrics_v2 ? metrics_v2->AverageGfxclkFrequencyPreDs :
				metrics->AverageGfxclkFrequencyPreDs;
		break;
	case METRICS_AVERAGE_FCLK:
		*value = use_metrics_v3 ? metrics_v3->AverageFclkFrequencyPostDs :
			use_metrics_v2 ? metrics_v2->AverageFclkFrequencyPostDs :
			metrics->AverageFclkFrequencyPostDs;
		break;
	case METRICS_AVERAGE_UCLK:
		*value = use_metrics_v3 ? metrics_v3->AverageUclkFrequencyPostDs :
			use_metrics_v2 ? metrics_v2->AverageUclkFrequencyPostDs :
			metrics->AverageUclkFrequencyPostDs;
		break;
	case METRICS_AVERAGE_GFXACTIVITY:
		*value = use_metrics_v3 ? metrics_v3->AverageGfxActivity :
			use_metrics_v2 ? metrics_v2->AverageGfxActivity :
			metrics->AverageGfxActivity;
		break;
	case METRICS_AVERAGE_MEMACTIVITY:
		*value = use_metrics_v3 ? metrics_v3->AverageUclkActivity :
			use_metrics_v2 ? metrics_v2->AverageUclkActivity :
			metrics->AverageUclkActivity;
		break;
	case METRICS_AVERAGE_SOCKETPOWER:
		*value = use_metrics_v3 ? metrics_v3->AverageSocketPower << 8 :
			use_metrics_v2 ? metrics_v2->AverageSocketPower << 8 :
			metrics->AverageSocketPower << 8;
		break;
	case METRICS_TEMPERATURE_EDGE:
		*value = (use_metrics_v3 ? metrics_v3->TemperatureEdge :
			use_metrics_v2 ? metrics_v2->TemperatureEdge :
			metrics->TemperatureEdge) * SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_TEMPERATURE_HOTSPOT:
		*value = (use_metrics_v3 ? metrics_v3->TemperatureHotspot :
			use_metrics_v2 ? metrics_v2->TemperatureHotspot :
			metrics->TemperatureHotspot) * SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_TEMPERATURE_MEM:
		*value = (use_metrics_v3 ? metrics_v3->TemperatureMem :
			use_metrics_v2 ? metrics_v2->TemperatureMem :
			metrics->TemperatureMem) * SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_TEMPERATURE_VRGFX:
		*value = (use_metrics_v3 ? metrics_v3->TemperatureVrGfx :
			use_metrics_v2 ? metrics_v2->TemperatureVrGfx :
			metrics->TemperatureVrGfx) * SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_TEMPERATURE_VRSOC:
		*value = (use_metrics_v3 ? metrics_v3->TemperatureVrSoc :
			use_metrics_v2 ? metrics_v2->TemperatureVrSoc :
			metrics->TemperatureVrSoc) * SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_THROTTLER_STATUS:
		*value = sienna_cichlid_get_throttler_status_locked(smu);
		break;
	case METRICS_CURR_FANSPEED:
		*value = use_metrics_v3 ? metrics_v3->CurrFanSpeed :
			use_metrics_v2 ? metrics_v2->CurrFanSpeed : metrics->CurrFanSpeed;
		break;
	case METRICS_UNIQUE_ID_UPPER32:
		/* Only supported in 0x3A5300+, metrics_v3 requires 0x3A4900+ */
		*value = use_metrics_v3 ? metrics_v3->PublicSerialNumUpper32 : 0;
		break;
	case METRICS_UNIQUE_ID_LOWER32:
		/* Only supported in 0x3A5300+, metrics_v3 requires 0x3A4900+ */
		*value = use_metrics_v3 ? metrics_v3->PublicSerialNumLower32 : 0;
		break;
	case METRICS_SS_APU_SHARE:
		sienna_cichlid_get_smartshift_power_percentage(smu, &apu_percent, &dgpu_percent);
		*value = apu_percent;
		break;
	case METRICS_SS_DGPU_SHARE:
		sienna_cichlid_get_smartshift_power_percentage(smu, &apu_percent, &dgpu_percent);
		*value = dgpu_percent;
		break;

	default:
		*value = UINT_MAX;
		break;
	}

	return ret;

}

static int sienna_cichlid_allocate_dpm_context(struct smu_context *smu)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;

	smu_dpm->dpm_context = kzalloc(sizeof(struct smu_11_0_dpm_context),
				       GFP_KERNEL);
	if (!smu_dpm->dpm_context)
		return -ENOMEM;

	smu_dpm->dpm_context_size = sizeof(struct smu_11_0_dpm_context);

	return 0;
}

static void sienna_cichlid_stb_init(struct smu_context *smu);

static int sienna_cichlid_init_smc_tables(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;

	ret = sienna_cichlid_tables_init(smu);
	if (ret)
		return ret;

	ret = sienna_cichlid_allocate_dpm_context(smu);
	if (ret)
		return ret;

	if (!amdgpu_sriov_vf(adev))
		sienna_cichlid_stb_init(smu);

	return smu_v11_0_init_smc_tables(smu);
}

static int sienna_cichlid_set_default_dpm_table(struct smu_context *smu)
{
	struct smu_11_0_dpm_context *dpm_context = smu->smu_dpm.dpm_context;
	struct smu_11_0_dpm_table *dpm_table;
	struct amdgpu_device *adev = smu->adev;
	int i, ret = 0;
	DpmDescriptor_t *table_member;

	/* socclk dpm table setup */
	dpm_table = &dpm_context->dpm_tables.soc_table;
	GET_PPTABLE_MEMBER(DpmDescriptor, &table_member);
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_SOCCLK_BIT)) {
		ret = smu_v11_0_set_single_dpm_table(smu,
						     SMU_SOCCLK,
						     dpm_table);
		if (ret)
			return ret;
		dpm_table->is_fine_grained =
			!table_member[PPCLK_SOCCLK].SnapToDiscrete;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.socclk / 100;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->min = dpm_table->dpm_levels[0].value;
		dpm_table->max = dpm_table->dpm_levels[0].value;
	}

	/* gfxclk dpm table setup */
	dpm_table = &dpm_context->dpm_tables.gfx_table;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_GFXCLK_BIT)) {
		ret = smu_v11_0_set_single_dpm_table(smu,
						     SMU_GFXCLK,
						     dpm_table);
		if (ret)
			return ret;
		dpm_table->is_fine_grained =
			!table_member[PPCLK_GFXCLK].SnapToDiscrete;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.gfxclk / 100;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->min = dpm_table->dpm_levels[0].value;
		dpm_table->max = dpm_table->dpm_levels[0].value;
	}

	/* uclk dpm table setup */
	dpm_table = &dpm_context->dpm_tables.uclk_table;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT)) {
		ret = smu_v11_0_set_single_dpm_table(smu,
						     SMU_UCLK,
						     dpm_table);
		if (ret)
			return ret;
		dpm_table->is_fine_grained =
			!table_member[PPCLK_UCLK].SnapToDiscrete;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.uclk / 100;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->min = dpm_table->dpm_levels[0].value;
		dpm_table->max = dpm_table->dpm_levels[0].value;
	}

	/* fclk dpm table setup */
	dpm_table = &dpm_context->dpm_tables.fclk_table;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_FCLK_BIT)) {
		ret = smu_v11_0_set_single_dpm_table(smu,
						     SMU_FCLK,
						     dpm_table);
		if (ret)
			return ret;
		dpm_table->is_fine_grained =
			!table_member[PPCLK_FCLK].SnapToDiscrete;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.fclk / 100;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->min = dpm_table->dpm_levels[0].value;
		dpm_table->max = dpm_table->dpm_levels[0].value;
	}

	/* vclk0/1 dpm table setup */
	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		dpm_table = &dpm_context->dpm_tables.vclk_table;
		if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_MM_DPM_PG_BIT)) {
			ret = smu_v11_0_set_single_dpm_table(smu,
							     i ? SMU_VCLK1 : SMU_VCLK,
							     dpm_table);
			if (ret)
				return ret;
			dpm_table->is_fine_grained =
				!table_member[i ? PPCLK_VCLK_1 : PPCLK_VCLK_0].SnapToDiscrete;
		} else {
			dpm_table->count = 1;
			dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.vclk / 100;
			dpm_table->dpm_levels[0].enabled = true;
			dpm_table->min = dpm_table->dpm_levels[0].value;
			dpm_table->max = dpm_table->dpm_levels[0].value;
		}
	}

	/* dclk0/1 dpm table setup */
	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;
		dpm_table = &dpm_context->dpm_tables.dclk_table;
		if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_MM_DPM_PG_BIT)) {
			ret = smu_v11_0_set_single_dpm_table(smu,
							     i ? SMU_DCLK1 : SMU_DCLK,
							     dpm_table);
			if (ret)
				return ret;
			dpm_table->is_fine_grained =
				!table_member[i ? PPCLK_DCLK_1 : PPCLK_DCLK_0].SnapToDiscrete;
		} else {
			dpm_table->count = 1;
			dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.dclk / 100;
			dpm_table->dpm_levels[0].enabled = true;
			dpm_table->min = dpm_table->dpm_levels[0].value;
			dpm_table->max = dpm_table->dpm_levels[0].value;
		}
	}

	/* dcefclk dpm table setup */
	dpm_table = &dpm_context->dpm_tables.dcef_table;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_DCEFCLK_BIT)) {
		ret = smu_v11_0_set_single_dpm_table(smu,
						     SMU_DCEFCLK,
						     dpm_table);
		if (ret)
			return ret;
		dpm_table->is_fine_grained =
			!table_member[PPCLK_DCEFCLK].SnapToDiscrete;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.dcefclk / 100;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->min = dpm_table->dpm_levels[0].value;
		dpm_table->max = dpm_table->dpm_levels[0].value;
	}

	/* pixelclk dpm table setup */
	dpm_table = &dpm_context->dpm_tables.pixel_table;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_DCEFCLK_BIT)) {
		ret = smu_v11_0_set_single_dpm_table(smu,
						     SMU_PIXCLK,
						     dpm_table);
		if (ret)
			return ret;
		dpm_table->is_fine_grained =
			!table_member[PPCLK_PIXCLK].SnapToDiscrete;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.dcefclk / 100;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->min = dpm_table->dpm_levels[0].value;
		dpm_table->max = dpm_table->dpm_levels[0].value;
	}

	/* displayclk dpm table setup */
	dpm_table = &dpm_context->dpm_tables.display_table;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_DCEFCLK_BIT)) {
		ret = smu_v11_0_set_single_dpm_table(smu,
						     SMU_DISPCLK,
						     dpm_table);
		if (ret)
			return ret;
		dpm_table->is_fine_grained =
			!table_member[PPCLK_DISPCLK].SnapToDiscrete;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.dcefclk / 100;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->min = dpm_table->dpm_levels[0].value;
		dpm_table->max = dpm_table->dpm_levels[0].value;
	}

	/* phyclk dpm table setup */
	dpm_table = &dpm_context->dpm_tables.phy_table;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_DCEFCLK_BIT)) {
		ret = smu_v11_0_set_single_dpm_table(smu,
						     SMU_PHYCLK,
						     dpm_table);
		if (ret)
			return ret;
		dpm_table->is_fine_grained =
			!table_member[PPCLK_PHYCLK].SnapToDiscrete;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.dcefclk / 100;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->min = dpm_table->dpm_levels[0].value;
		dpm_table->max = dpm_table->dpm_levels[0].value;
	}

	return 0;
}

static int sienna_cichlid_dpm_set_vcn_enable(struct smu_context *smu, bool enable)
{
	struct amdgpu_device *adev = smu->adev;
	int i, ret = 0;

	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;
		/* vcn dpm on is a prerequisite for vcn power gate messages */
		if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_MM_DPM_PG_BIT)) {
			ret = smu_cmn_send_smc_msg_with_param(smu, enable ?
							      SMU_MSG_PowerUpVcn : SMU_MSG_PowerDownVcn,
							      0x10000 * i, NULL);
			if (ret)
				return ret;
		}
	}

	return ret;
}

static int sienna_cichlid_dpm_set_jpeg_enable(struct smu_context *smu, bool enable)
{
	int ret = 0;

	if (enable) {
		if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_MM_DPM_PG_BIT)) {
			ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_PowerUpJpeg, 0, NULL);
			if (ret)
				return ret;
		}
	} else {
		if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_MM_DPM_PG_BIT)) {
			ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_PowerDownJpeg, 0, NULL);
			if (ret)
				return ret;
		}
	}

	return ret;
}

static int sienna_cichlid_get_current_clk_freq_by_table(struct smu_context *smu,
				       enum smu_clk_type clk_type,
				       uint32_t *value)
{
	MetricsMember_t member_type;
	int clk_id = 0;

	clk_id = smu_cmn_to_asic_specific_index(smu,
						CMN2ASIC_MAPPING_CLK,
						clk_type);
	if (clk_id < 0)
		return clk_id;

	switch (clk_id) {
	case PPCLK_GFXCLK:
		member_type = METRICS_CURR_GFXCLK;
		break;
	case PPCLK_UCLK:
		member_type = METRICS_CURR_UCLK;
		break;
	case PPCLK_SOCCLK:
		member_type = METRICS_CURR_SOCCLK;
		break;
	case PPCLK_FCLK:
		member_type = METRICS_CURR_FCLK;
		break;
	case PPCLK_VCLK_0:
		member_type = METRICS_CURR_VCLK;
		break;
	case PPCLK_VCLK_1:
		member_type = METRICS_CURR_VCLK1;
		break;
	case PPCLK_DCLK_0:
		member_type = METRICS_CURR_DCLK;
		break;
	case PPCLK_DCLK_1:
		member_type = METRICS_CURR_DCLK1;
		break;
	case PPCLK_DCEFCLK:
		member_type = METRICS_CURR_DCEFCLK;
		break;
	default:
		return -EINVAL;
	}

	return sienna_cichlid_get_smu_metrics_data(smu,
						   member_type,
						   value);

}

static bool sienna_cichlid_is_support_fine_grained_dpm(struct smu_context *smu, enum smu_clk_type clk_type)
{
	DpmDescriptor_t *dpm_desc = NULL;
	DpmDescriptor_t *table_member;
	uint32_t clk_index = 0;

	GET_PPTABLE_MEMBER(DpmDescriptor, &table_member);
	clk_index = smu_cmn_to_asic_specific_index(smu,
						   CMN2ASIC_MAPPING_CLK,
						   clk_type);
	dpm_desc = &table_member[clk_index];

	/* 0 - Fine grained DPM, 1 - Discrete DPM */
	return dpm_desc->SnapToDiscrete == 0;
}

static bool sienna_cichlid_is_od_feature_supported(struct smu_11_0_7_overdrive_table *od_table,
						   enum SMU_11_0_7_ODFEATURE_CAP cap)
{
	return od_table->cap[cap];
}

static void sienna_cichlid_get_od_setting_range(struct smu_11_0_7_overdrive_table *od_table,
						enum SMU_11_0_7_ODSETTING_ID setting,
						uint32_t *min, uint32_t *max)
{
	if (min)
		*min = od_table->min[setting];
	if (max)
		*max = od_table->max[setting];
}

static int sienna_cichlid_print_clk_levels(struct smu_context *smu,
			enum smu_clk_type clk_type, char *buf)
{
	struct amdgpu_device *adev = smu->adev;
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct smu_11_0_dpm_context *dpm_context = smu_dpm->dpm_context;
	uint16_t *table_member;

	struct smu_11_0_7_overdrive_table *od_settings = smu->od_settings;
	OverDriveTable_t *od_table =
		(OverDriveTable_t *)table_context->overdrive_table;
	int i, size = 0, ret = 0;
	uint32_t cur_value = 0, value = 0, count = 0;
	uint32_t freq_values[3] = {0};
	uint32_t mark_index = 0;
	uint32_t gen_speed, lane_width;
	uint32_t min_value, max_value;
	uint32_t smu_version;

	smu_cmn_get_sysfs_buf(&buf, &size);

	switch (clk_type) {
	case SMU_GFXCLK:
	case SMU_SCLK:
	case SMU_SOCCLK:
	case SMU_MCLK:
	case SMU_UCLK:
	case SMU_FCLK:
	case SMU_VCLK:
	case SMU_VCLK1:
	case SMU_DCLK:
	case SMU_DCLK1:
	case SMU_DCEFCLK:
		ret = sienna_cichlid_get_current_clk_freq_by_table(smu, clk_type, &cur_value);
		if (ret)
			goto print_clk_out;

		ret = smu_v11_0_get_dpm_level_count(smu, clk_type, &count);
		if (ret)
			goto print_clk_out;

		if (!sienna_cichlid_is_support_fine_grained_dpm(smu, clk_type)) {
			for (i = 0; i < count; i++) {
				ret = smu_v11_0_get_dpm_freq_by_index(smu, clk_type, i, &value);
				if (ret)
					goto print_clk_out;

				size += sysfs_emit_at(buf, size, "%d: %uMhz %s\n", i, value,
						cur_value == value ? "*" : "");
			}
		} else {
			ret = smu_v11_0_get_dpm_freq_by_index(smu, clk_type, 0, &freq_values[0]);
			if (ret)
				goto print_clk_out;
			ret = smu_v11_0_get_dpm_freq_by_index(smu, clk_type, count - 1, &freq_values[2]);
			if (ret)
				goto print_clk_out;

			freq_values[1] = cur_value;
			mark_index = cur_value == freq_values[0] ? 0 :
				     cur_value == freq_values[2] ? 2 : 1;

			count = 3;
			if (mark_index != 1) {
				count = 2;
				freq_values[1] = freq_values[2];
			}

			for (i = 0; i < count; i++) {
				size += sysfs_emit_at(buf, size, "%d: %uMhz %s\n", i, freq_values[i],
						cur_value  == freq_values[i] ? "*" : "");
			}

		}
		break;
	case SMU_PCIE:
		gen_speed = smu_v11_0_get_current_pcie_link_speed_level(smu);
		lane_width = smu_v11_0_get_current_pcie_link_width_level(smu);
		GET_PPTABLE_MEMBER(LclkFreq, &table_member);
		for (i = 0; i < NUM_LINK_LEVELS; i++)
			size += sysfs_emit_at(buf, size, "%d: %s %s %dMhz %s\n", i,
					(dpm_context->dpm_tables.pcie_table.pcie_gen[i] == 0) ? "2.5GT/s," :
					(dpm_context->dpm_tables.pcie_table.pcie_gen[i] == 1) ? "5.0GT/s," :
					(dpm_context->dpm_tables.pcie_table.pcie_gen[i] == 2) ? "8.0GT/s," :
					(dpm_context->dpm_tables.pcie_table.pcie_gen[i] == 3) ? "16.0GT/s," : "",
					(dpm_context->dpm_tables.pcie_table.pcie_lane[i] == 1) ? "x1" :
					(dpm_context->dpm_tables.pcie_table.pcie_lane[i] == 2) ? "x2" :
					(dpm_context->dpm_tables.pcie_table.pcie_lane[i] == 3) ? "x4" :
					(dpm_context->dpm_tables.pcie_table.pcie_lane[i] == 4) ? "x8" :
					(dpm_context->dpm_tables.pcie_table.pcie_lane[i] == 5) ? "x12" :
					(dpm_context->dpm_tables.pcie_table.pcie_lane[i] == 6) ? "x16" : "",
					table_member[i],
					(gen_speed == dpm_context->dpm_tables.pcie_table.pcie_gen[i]) &&
					(lane_width == dpm_context->dpm_tables.pcie_table.pcie_lane[i]) ?
					"*" : "");
		break;
	case SMU_OD_SCLK:
		if (!smu->od_enabled || !od_table || !od_settings)
			break;

		if (!sienna_cichlid_is_od_feature_supported(od_settings, SMU_11_0_7_ODCAP_GFXCLK_LIMITS))
			break;

		size += sysfs_emit_at(buf, size, "OD_SCLK:\n");
		size += sysfs_emit_at(buf, size, "0: %uMhz\n1: %uMhz\n", od_table->GfxclkFmin, od_table->GfxclkFmax);
		break;

	case SMU_OD_MCLK:
		if (!smu->od_enabled || !od_table || !od_settings)
			break;

		if (!sienna_cichlid_is_od_feature_supported(od_settings, SMU_11_0_7_ODCAP_UCLK_LIMITS))
			break;

		size += sysfs_emit_at(buf, size, "OD_MCLK:\n");
		size += sysfs_emit_at(buf, size, "0: %uMhz\n1: %uMHz\n", od_table->UclkFmin, od_table->UclkFmax);
		break;

	case SMU_OD_VDDGFX_OFFSET:
		if (!smu->od_enabled || !od_table || !od_settings)
			break;

		/*
		 * OD GFX Voltage Offset functionality is supported only by 58.41.0
		 * and onwards SMU firmwares.
		 */
		smu_cmn_get_smc_version(smu, NULL, &smu_version);
		if ((adev->ip_versions[MP1_HWIP][0] == IP_VERSION(11, 0, 7)) &&
		     (smu_version < 0x003a2900))
			break;

		size += sysfs_emit_at(buf, size, "OD_VDDGFX_OFFSET:\n");
		size += sysfs_emit_at(buf, size, "%dmV\n", od_table->VddGfxOffset);
		break;

	case SMU_OD_RANGE:
		if (!smu->od_enabled || !od_table || !od_settings)
			break;

		size += sysfs_emit_at(buf, size, "%s:\n", "OD_RANGE");

		if (sienna_cichlid_is_od_feature_supported(od_settings, SMU_11_0_7_ODCAP_GFXCLK_LIMITS)) {
			sienna_cichlid_get_od_setting_range(od_settings, SMU_11_0_7_ODSETTING_GFXCLKFMIN,
							    &min_value, NULL);
			sienna_cichlid_get_od_setting_range(od_settings, SMU_11_0_7_ODSETTING_GFXCLKFMAX,
							    NULL, &max_value);
			size += sysfs_emit_at(buf, size, "SCLK: %7uMhz %10uMhz\n",
					min_value, max_value);
		}

		if (sienna_cichlid_is_od_feature_supported(od_settings, SMU_11_0_7_ODCAP_UCLK_LIMITS)) {
			sienna_cichlid_get_od_setting_range(od_settings, SMU_11_0_7_ODSETTING_UCLKFMIN,
							    &min_value, NULL);
			sienna_cichlid_get_od_setting_range(od_settings, SMU_11_0_7_ODSETTING_UCLKFMAX,
							    NULL, &max_value);
			size += sysfs_emit_at(buf, size, "MCLK: %7uMhz %10uMhz\n",
					min_value, max_value);
		}
		break;

	default:
		break;
	}

print_clk_out:
	return size;
}

static int sienna_cichlid_force_clk_levels(struct smu_context *smu,
				   enum smu_clk_type clk_type, uint32_t mask)
{
	int ret = 0;
	uint32_t soft_min_level = 0, soft_max_level = 0, min_freq = 0, max_freq = 0;

	soft_min_level = mask ? (ffs(mask) - 1) : 0;
	soft_max_level = mask ? (fls(mask) - 1) : 0;

	switch (clk_type) {
	case SMU_GFXCLK:
	case SMU_SCLK:
	case SMU_SOCCLK:
	case SMU_MCLK:
	case SMU_UCLK:
	case SMU_FCLK:
		/* There is only 2 levels for fine grained DPM */
		if (sienna_cichlid_is_support_fine_grained_dpm(smu, clk_type)) {
			soft_max_level = (soft_max_level >= 1 ? 1 : 0);
			soft_min_level = (soft_min_level >= 1 ? 1 : 0);
		}

		ret = smu_v11_0_get_dpm_freq_by_index(smu, clk_type, soft_min_level, &min_freq);
		if (ret)
			goto forec_level_out;

		ret = smu_v11_0_get_dpm_freq_by_index(smu, clk_type, soft_max_level, &max_freq);
		if (ret)
			goto forec_level_out;

		ret = smu_v11_0_set_soft_freq_limited_range(smu, clk_type, min_freq, max_freq);
		if (ret)
			goto forec_level_out;
		break;
	case SMU_DCEFCLK:
		dev_info(smu->adev->dev,"Setting DCEFCLK min/max dpm level is not supported!\n");
		break;
	default:
		break;
	}

forec_level_out:
	return 0;
}

static int sienna_cichlid_populate_umd_state_clk(struct smu_context *smu)
{
	struct smu_11_0_dpm_context *dpm_context =
				smu->smu_dpm.dpm_context;
	struct smu_11_0_dpm_table *gfx_table =
				&dpm_context->dpm_tables.gfx_table;
	struct smu_11_0_dpm_table *mem_table =
				&dpm_context->dpm_tables.uclk_table;
	struct smu_11_0_dpm_table *soc_table =
				&dpm_context->dpm_tables.soc_table;
	struct smu_umd_pstate_table *pstate_table =
				&smu->pstate_table;
	struct amdgpu_device *adev = smu->adev;

	pstate_table->gfxclk_pstate.min = gfx_table->min;
	pstate_table->gfxclk_pstate.peak = gfx_table->max;

	pstate_table->uclk_pstate.min = mem_table->min;
	pstate_table->uclk_pstate.peak = mem_table->max;

	pstate_table->socclk_pstate.min = soc_table->min;
	pstate_table->socclk_pstate.peak = soc_table->max;

	switch (adev->ip_versions[MP1_HWIP][0]) {
	case IP_VERSION(11, 0, 7):
	case IP_VERSION(11, 0, 11):
		pstate_table->gfxclk_pstate.standard = SIENNA_CICHLID_UMD_PSTATE_PROFILING_GFXCLK;
		pstate_table->uclk_pstate.standard = SIENNA_CICHLID_UMD_PSTATE_PROFILING_MEMCLK;
		pstate_table->socclk_pstate.standard = SIENNA_CICHLID_UMD_PSTATE_PROFILING_SOCCLK;
		break;
	case IP_VERSION(11, 0, 12):
		pstate_table->gfxclk_pstate.standard = DIMGREY_CAVEFISH_UMD_PSTATE_PROFILING_GFXCLK;
		pstate_table->uclk_pstate.standard = DIMGREY_CAVEFISH_UMD_PSTATE_PROFILING_MEMCLK;
		pstate_table->socclk_pstate.standard = DIMGREY_CAVEFISH_UMD_PSTATE_PROFILING_SOCCLK;
		break;
	case IP_VERSION(11, 0, 13):
		pstate_table->gfxclk_pstate.standard = BEIGE_GOBY_UMD_PSTATE_PROFILING_GFXCLK;
		pstate_table->uclk_pstate.standard = BEIGE_GOBY_UMD_PSTATE_PROFILING_MEMCLK;
		pstate_table->socclk_pstate.standard = BEIGE_GOBY_UMD_PSTATE_PROFILING_SOCCLK;
		break;
	default:
		break;
	}

	return 0;
}

static int sienna_cichlid_pre_display_config_changed(struct smu_context *smu)
{
	int ret = 0;
	uint32_t max_freq = 0;

	/* Sienna_Cichlid do not support to change display num currently */
	return 0;
#if 0
	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_NumOfDisplays, 0, NULL);
	if (ret)
		return ret;
#endif

	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT)) {
		ret = smu_v11_0_get_dpm_ultimate_freq(smu, SMU_UCLK, NULL, &max_freq);
		if (ret)
			return ret;
		ret = smu_v11_0_set_hard_freq_limited_range(smu, SMU_UCLK, 0, max_freq);
		if (ret)
			return ret;
	}

	return ret;
}

static int sienna_cichlid_display_config_changed(struct smu_context *smu)
{
	int ret = 0;

	if ((smu->watermarks_bitmap & WATERMARKS_EXIST) &&
	    smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_DCEFCLK_BIT) &&
	    smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_SOCCLK_BIT)) {
#if 0
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_NumOfDisplays,
						  smu->display_config->num_display,
						  NULL);
#endif
		if (ret)
			return ret;
	}

	return ret;
}

static bool sienna_cichlid_is_dpm_running(struct smu_context *smu)
{
	int ret = 0;
	uint64_t feature_enabled;

	ret = smu_cmn_get_enabled_mask(smu, &feature_enabled);
	if (ret)
		return false;

	return !!(feature_enabled & SMC_DPM_FEATURE);
}

static int sienna_cichlid_get_fan_speed_rpm(struct smu_context *smu,
					    uint32_t *speed)
{
	if (!speed)
		return -EINVAL;

	/*
	 * For Sienna_Cichlid and later, the fan speed(rpm) reported
	 * by pmfw is always trustable(even when the fan control feature
	 * disabled or 0 RPM kicked in).
	 */
	return sienna_cichlid_get_smu_metrics_data(smu,
						   METRICS_CURR_FANSPEED,
						   speed);
}

static int sienna_cichlid_get_fan_parameters(struct smu_context *smu)
{
	uint16_t *table_member;

	GET_PPTABLE_MEMBER(FanMaximumRpm, &table_member);
	smu->fan_max_rpm = *table_member;

	return 0;
}

static int sienna_cichlid_get_power_profile_mode(struct smu_context *smu, char *buf)
{
	DpmActivityMonitorCoeffIntExternal_t activity_monitor_external;
	DpmActivityMonitorCoeffInt_t *activity_monitor =
		&(activity_monitor_external.DpmActivityMonitorCoeffInt);
	uint32_t i, size = 0;
	int16_t workload_type = 0;
	static const char *title[] = {
			"PROFILE_INDEX(NAME)",
			"CLOCK_TYPE(NAME)",
			"FPS",
			"MinFreqType",
			"MinActiveFreqType",
			"MinActiveFreq",
			"BoosterFreqType",
			"BoosterFreq",
			"PD_Data_limit_c",
			"PD_Data_error_coeff",
			"PD_Data_error_rate_coeff"};
	int result = 0;

	if (!buf)
		return -EINVAL;

	size += sysfs_emit_at(buf, size, "%16s %s %s %s %s %s %s %s %s %s %s\n",
			title[0], title[1], title[2], title[3], title[4], title[5],
			title[6], title[7], title[8], title[9], title[10]);

	for (i = 0; i <= PP_SMC_POWER_PROFILE_CUSTOM; i++) {
		/* conv PP_SMC_POWER_PROFILE* to WORKLOAD_PPLIB_*_BIT */
		workload_type = smu_cmn_to_asic_specific_index(smu,
							       CMN2ASIC_MAPPING_WORKLOAD,
							       i);
		if (workload_type < 0)
			return -EINVAL;

		result = smu_cmn_update_table(smu,
					  SMU_TABLE_ACTIVITY_MONITOR_COEFF, workload_type,
					  (void *)(&activity_monitor_external), false);
		if (result) {
			dev_err(smu->adev->dev, "[%s] Failed to get activity monitor!", __func__);
			return result;
		}

		size += sysfs_emit_at(buf, size, "%2d %14s%s:\n",
			i, amdgpu_pp_profile_name[i], (i == smu->power_profile_mode) ? "*" : " ");

		size += sysfs_emit_at(buf, size, "%19s %d(%13s) %7d %7d %7d %7d %7d %7d %7d %7d %7d\n",
			" ",
			0,
			"GFXCLK",
			activity_monitor->Gfx_FPS,
			activity_monitor->Gfx_MinFreqStep,
			activity_monitor->Gfx_MinActiveFreqType,
			activity_monitor->Gfx_MinActiveFreq,
			activity_monitor->Gfx_BoosterFreqType,
			activity_monitor->Gfx_BoosterFreq,
			activity_monitor->Gfx_PD_Data_limit_c,
			activity_monitor->Gfx_PD_Data_error_coeff,
			activity_monitor->Gfx_PD_Data_error_rate_coeff);

		size += sysfs_emit_at(buf, size, "%19s %d(%13s) %7d %7d %7d %7d %7d %7d %7d %7d %7d\n",
			" ",
			1,
			"SOCCLK",
			activity_monitor->Fclk_FPS,
			activity_monitor->Fclk_MinFreqStep,
			activity_monitor->Fclk_MinActiveFreqType,
			activity_monitor->Fclk_MinActiveFreq,
			activity_monitor->Fclk_BoosterFreqType,
			activity_monitor->Fclk_BoosterFreq,
			activity_monitor->Fclk_PD_Data_limit_c,
			activity_monitor->Fclk_PD_Data_error_coeff,
			activity_monitor->Fclk_PD_Data_error_rate_coeff);

		size += sysfs_emit_at(buf, size, "%19s %d(%13s) %7d %7d %7d %7d %7d %7d %7d %7d %7d\n",
			" ",
			2,
			"MEMLK",
			activity_monitor->Mem_FPS,
			activity_monitor->Mem_MinFreqStep,
			activity_monitor->Mem_MinActiveFreqType,
			activity_monitor->Mem_MinActiveFreq,
			activity_monitor->Mem_BoosterFreqType,
			activity_monitor->Mem_BoosterFreq,
			activity_monitor->Mem_PD_Data_limit_c,
			activity_monitor->Mem_PD_Data_error_coeff,
			activity_monitor->Mem_PD_Data_error_rate_coeff);
	}

	return size;
}

static int sienna_cichlid_set_power_profile_mode(struct smu_context *smu, long *input, uint32_t size)
{

	DpmActivityMonitorCoeffIntExternal_t activity_monitor_external;
	DpmActivityMonitorCoeffInt_t *activity_monitor =
		&(activity_monitor_external.DpmActivityMonitorCoeffInt);
	int workload_type, ret = 0;

	smu->power_profile_mode = input[size];

	if (smu->power_profile_mode > PP_SMC_POWER_PROFILE_CUSTOM) {
		dev_err(smu->adev->dev, "Invalid power profile mode %d\n", smu->power_profile_mode);
		return -EINVAL;
	}

	if (smu->power_profile_mode == PP_SMC_POWER_PROFILE_CUSTOM) {

		ret = smu_cmn_update_table(smu,
				       SMU_TABLE_ACTIVITY_MONITOR_COEFF, WORKLOAD_PPLIB_CUSTOM_BIT,
				       (void *)(&activity_monitor_external), false);
		if (ret) {
			dev_err(smu->adev->dev, "[%s] Failed to get activity monitor!", __func__);
			return ret;
		}

		switch (input[0]) {
		case 0: /* Gfxclk */
			activity_monitor->Gfx_FPS = input[1];
			activity_monitor->Gfx_MinFreqStep = input[2];
			activity_monitor->Gfx_MinActiveFreqType = input[3];
			activity_monitor->Gfx_MinActiveFreq = input[4];
			activity_monitor->Gfx_BoosterFreqType = input[5];
			activity_monitor->Gfx_BoosterFreq = input[6];
			activity_monitor->Gfx_PD_Data_limit_c = input[7];
			activity_monitor->Gfx_PD_Data_error_coeff = input[8];
			activity_monitor->Gfx_PD_Data_error_rate_coeff = input[9];
			break;
		case 1: /* Socclk */
			activity_monitor->Fclk_FPS = input[1];
			activity_monitor->Fclk_MinFreqStep = input[2];
			activity_monitor->Fclk_MinActiveFreqType = input[3];
			activity_monitor->Fclk_MinActiveFreq = input[4];
			activity_monitor->Fclk_BoosterFreqType = input[5];
			activity_monitor->Fclk_BoosterFreq = input[6];
			activity_monitor->Fclk_PD_Data_limit_c = input[7];
			activity_monitor->Fclk_PD_Data_error_coeff = input[8];
			activity_monitor->Fclk_PD_Data_error_rate_coeff = input[9];
			break;
		case 2: /* Memlk */
			activity_monitor->Mem_FPS = input[1];
			activity_monitor->Mem_MinFreqStep = input[2];
			activity_monitor->Mem_MinActiveFreqType = input[3];
			activity_monitor->Mem_MinActiveFreq = input[4];
			activity_monitor->Mem_BoosterFreqType = input[5];
			activity_monitor->Mem_BoosterFreq = input[6];
			activity_monitor->Mem_PD_Data_limit_c = input[7];
			activity_monitor->Mem_PD_Data_error_coeff = input[8];
			activity_monitor->Mem_PD_Data_error_rate_coeff = input[9];
			break;
		}

		ret = smu_cmn_update_table(smu,
				       SMU_TABLE_ACTIVITY_MONITOR_COEFF, WORKLOAD_PPLIB_CUSTOM_BIT,
				       (void *)(&activity_monitor_external), true);
		if (ret) {
			dev_err(smu->adev->dev, "[%s] Failed to set activity monitor!", __func__);
			return ret;
		}
	}

	/* conv PP_SMC_POWER_PROFILE* to WORKLOAD_PPLIB_*_BIT */
	workload_type = smu_cmn_to_asic_specific_index(smu,
						       CMN2ASIC_MAPPING_WORKLOAD,
						       smu->power_profile_mode);
	if (workload_type < 0)
		return -EINVAL;
	smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetWorkloadMask,
				    1 << workload_type, NULL);

	return ret;
}

static int sienna_cichlid_notify_smc_display_config(struct smu_context *smu)
{
	struct smu_clocks min_clocks = {0};
	struct pp_display_clock_request clock_req;
	int ret = 0;

	min_clocks.dcef_clock = smu->display_config->min_dcef_set_clk;
	min_clocks.dcef_clock_in_sr = smu->display_config->min_dcef_deep_sleep_set_clk;
	min_clocks.memory_clock = smu->display_config->min_mem_set_clock;

	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_DCEFCLK_BIT)) {
		clock_req.clock_type = amd_pp_dcef_clock;
		clock_req.clock_freq_in_khz = min_clocks.dcef_clock * 10;

		ret = smu_v11_0_display_clock_voltage_request(smu, &clock_req);
		if (!ret) {
			if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DS_DCEFCLK_BIT)) {
				ret = smu_cmn_send_smc_msg_with_param(smu,
								  SMU_MSG_SetMinDeepSleepDcefclk,
								  min_clocks.dcef_clock_in_sr/100,
								  NULL);
				if (ret) {
					dev_err(smu->adev->dev, "Attempt to set divider for DCEFCLK Failed!");
					return ret;
				}
			}
		} else {
			dev_info(smu->adev->dev, "Attempt to set Hard Min for DCEFCLK Failed!");
		}
	}

	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT)) {
		ret = smu_v11_0_set_hard_freq_limited_range(smu, SMU_UCLK, min_clocks.memory_clock/100, 0);
		if (ret) {
			dev_err(smu->adev->dev, "[%s] Set hard min uclk failed!", __func__);
			return ret;
		}
	}

	return 0;
}

static int sienna_cichlid_set_watermarks_table(struct smu_context *smu,
					       struct pp_smu_wm_range_sets *clock_ranges)
{
	Watermarks_t *table = smu->smu_table.watermarks_table;
	int ret = 0;
	int i;

	if (clock_ranges) {
		if (clock_ranges->num_reader_wm_sets > NUM_WM_RANGES ||
		    clock_ranges->num_writer_wm_sets > NUM_WM_RANGES)
			return -EINVAL;

		for (i = 0; i < clock_ranges->num_reader_wm_sets; i++) {
			table->WatermarkRow[WM_DCEFCLK][i].MinClock =
				clock_ranges->reader_wm_sets[i].min_drain_clk_mhz;
			table->WatermarkRow[WM_DCEFCLK][i].MaxClock =
				clock_ranges->reader_wm_sets[i].max_drain_clk_mhz;
			table->WatermarkRow[WM_DCEFCLK][i].MinUclk =
				clock_ranges->reader_wm_sets[i].min_fill_clk_mhz;
			table->WatermarkRow[WM_DCEFCLK][i].MaxUclk =
				clock_ranges->reader_wm_sets[i].max_fill_clk_mhz;

			table->WatermarkRow[WM_DCEFCLK][i].WmSetting =
				clock_ranges->reader_wm_sets[i].wm_inst;
		}

		for (i = 0; i < clock_ranges->num_writer_wm_sets; i++) {
			table->WatermarkRow[WM_SOCCLK][i].MinClock =
				clock_ranges->writer_wm_sets[i].min_fill_clk_mhz;
			table->WatermarkRow[WM_SOCCLK][i].MaxClock =
				clock_ranges->writer_wm_sets[i].max_fill_clk_mhz;
			table->WatermarkRow[WM_SOCCLK][i].MinUclk =
				clock_ranges->writer_wm_sets[i].min_drain_clk_mhz;
			table->WatermarkRow[WM_SOCCLK][i].MaxUclk =
				clock_ranges->writer_wm_sets[i].max_drain_clk_mhz;

			table->WatermarkRow[WM_SOCCLK][i].WmSetting =
				clock_ranges->writer_wm_sets[i].wm_inst;
		}

		smu->watermarks_bitmap |= WATERMARKS_EXIST;
	}

	if ((smu->watermarks_bitmap & WATERMARKS_EXIST) &&
	     !(smu->watermarks_bitmap & WATERMARKS_LOADED)) {
		ret = smu_cmn_write_watermarks_table(smu);
		if (ret) {
			dev_err(smu->adev->dev, "Failed to update WMTABLE!");
			return ret;
		}
		smu->watermarks_bitmap |= WATERMARKS_LOADED;
	}

	return 0;
}

static int sienna_cichlid_read_sensor(struct smu_context *smu,
				 enum amd_pp_sensors sensor,
				 void *data, uint32_t *size)
{
	int ret = 0;
	uint16_t *temp;
	struct amdgpu_device *adev = smu->adev;

	if(!data || !size)
		return -EINVAL;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_MAX_FAN_RPM:
		GET_PPTABLE_MEMBER(FanMaximumRpm, &temp);
		*(uint16_t *)data = *temp;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_MEM_LOAD:
		ret = sienna_cichlid_get_smu_metrics_data(smu,
							  METRICS_AVERAGE_MEMACTIVITY,
							  (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		ret = sienna_cichlid_get_smu_metrics_data(smu,
							  METRICS_AVERAGE_GFXACTIVITY,
							  (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_POWER:
		ret = sienna_cichlid_get_smu_metrics_data(smu,
							  METRICS_AVERAGE_SOCKETPOWER,
							  (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_HOTSPOT_TEMP:
		ret = sienna_cichlid_get_smu_metrics_data(smu,
							  METRICS_TEMPERATURE_HOTSPOT,
							  (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_EDGE_TEMP:
		ret = sienna_cichlid_get_smu_metrics_data(smu,
							  METRICS_TEMPERATURE_EDGE,
							  (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_MEM_TEMP:
		ret = sienna_cichlid_get_smu_metrics_data(smu,
							  METRICS_TEMPERATURE_MEM,
							  (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GFX_MCLK:
		ret = sienna_cichlid_get_current_clk_freq_by_table(smu, SMU_UCLK, (uint32_t *)data);
		*(uint32_t *)data *= 100;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GFX_SCLK:
		ret = sienna_cichlid_get_current_clk_freq_by_table(smu, SMU_GFXCLK, (uint32_t *)data);
		*(uint32_t *)data *= 100;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_VDDGFX:
		ret = smu_v11_0_get_gfx_vdd(smu, (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_SS_APU_SHARE:
		if (adev->ip_versions[MP1_HWIP][0] != IP_VERSION(11, 0, 7)) {
			ret = sienna_cichlid_get_smu_metrics_data(smu,
						METRICS_SS_APU_SHARE, (uint32_t *)data);
			*size = 4;
		} else {
			ret = -EOPNOTSUPP;
		}
		break;
	case AMDGPU_PP_SENSOR_SS_DGPU_SHARE:
		if (adev->ip_versions[MP1_HWIP][0] != IP_VERSION(11, 0, 7)) {
			ret = sienna_cichlid_get_smu_metrics_data(smu,
						METRICS_SS_DGPU_SHARE, (uint32_t *)data);
			*size = 4;
		} else {
			ret = -EOPNOTSUPP;
		}
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static void sienna_cichlid_get_unique_id(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t upper32 = 0, lower32 = 0;

	/* Only supported as of version 0.58.83.0 and only on Sienna Cichlid */
	if (smu->smc_fw_version < 0x3A5300 ||
	    smu->adev->ip_versions[MP1_HWIP][0] != IP_VERSION(11, 0, 7))
		return;

	if (sienna_cichlid_get_smu_metrics_data(smu, METRICS_UNIQUE_ID_UPPER32, &upper32))
		goto out;
	if (sienna_cichlid_get_smu_metrics_data(smu, METRICS_UNIQUE_ID_LOWER32, &lower32))
		goto out;

out:

	adev->unique_id = ((uint64_t)upper32 << 32) | lower32;
	if (adev->serial[0] == '\0')
		sprintf(adev->serial, "%016llx", adev->unique_id);
}

static int sienna_cichlid_get_uclk_dpm_states(struct smu_context *smu, uint32_t *clocks_in_khz, uint32_t *num_states)
{
	uint32_t num_discrete_levels = 0;
	uint16_t *dpm_levels = NULL;
	uint16_t i = 0;
	struct smu_table_context *table_context = &smu->smu_table;
	DpmDescriptor_t *table_member1;
	uint16_t *table_member2;

	if (!clocks_in_khz || !num_states || !table_context->driver_pptable)
		return -EINVAL;

	GET_PPTABLE_MEMBER(DpmDescriptor, &table_member1);
	num_discrete_levels = table_member1[PPCLK_UCLK].NumDiscreteLevels;
	GET_PPTABLE_MEMBER(FreqTableUclk, &table_member2);
	dpm_levels = table_member2;

	if (num_discrete_levels == 0 || dpm_levels == NULL)
		return -EINVAL;

	*num_states = num_discrete_levels;
	for (i = 0; i < num_discrete_levels; i++) {
		/* convert to khz */
		*clocks_in_khz = (*dpm_levels) * 1000;
		clocks_in_khz++;
		dpm_levels++;
	}

	return 0;
}

static int sienna_cichlid_get_thermal_temperature_range(struct smu_context *smu,
						struct smu_temperature_range *range)
{
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_11_0_7_powerplay_table *powerplay_table =
				table_context->power_play_table;
	uint16_t *table_member;
	uint16_t temp_edge, temp_hotspot, temp_mem;

	if (!range)
		return -EINVAL;

	memcpy(range, &smu11_thermal_policy[0], sizeof(struct smu_temperature_range));

	GET_PPTABLE_MEMBER(TemperatureLimit, &table_member);
	temp_edge = table_member[TEMP_EDGE];
	temp_hotspot = table_member[TEMP_HOTSPOT];
	temp_mem = table_member[TEMP_MEM];

	range->max = temp_edge * SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->edge_emergency_max = (temp_edge + CTF_OFFSET_EDGE) *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->hotspot_crit_max = temp_hotspot * SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->hotspot_emergency_max = (temp_hotspot + CTF_OFFSET_HOTSPOT) *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->mem_crit_max = temp_mem * SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->mem_emergency_max = (temp_mem + CTF_OFFSET_MEM)*
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;

	range->software_shutdown_temp = powerplay_table->software_shutdown_temp;

	return 0;
}

static int sienna_cichlid_display_disable_memory_clock_switch(struct smu_context *smu,
						bool disable_memory_clock_switch)
{
	int ret = 0;
	struct smu_11_0_max_sustainable_clocks *max_sustainable_clocks =
		(struct smu_11_0_max_sustainable_clocks *)
			smu->smu_table.max_sustainable_clocks;
	uint32_t min_memory_clock = smu->hard_min_uclk_req_from_dal;
	uint32_t max_memory_clock = max_sustainable_clocks->uclock;

	if(smu->disable_uclk_switch == disable_memory_clock_switch)
		return 0;

	if(disable_memory_clock_switch)
		ret = smu_v11_0_set_hard_freq_limited_range(smu, SMU_UCLK, max_memory_clock, 0);
	else
		ret = smu_v11_0_set_hard_freq_limited_range(smu, SMU_UCLK, min_memory_clock, 0);

	if(!ret)
		smu->disable_uclk_switch = disable_memory_clock_switch;

	return ret;
}

static int sienna_cichlid_update_pcie_parameters(struct smu_context *smu,
					 uint32_t pcie_gen_cap,
					 uint32_t pcie_width_cap)
{
	struct smu_11_0_dpm_context *dpm_context = smu->smu_dpm.dpm_context;

	uint32_t smu_pcie_arg;
	uint8_t *table_member1, *table_member2;
	int ret, i;

	GET_PPTABLE_MEMBER(PcieGenSpeed, &table_member1);
	GET_PPTABLE_MEMBER(PcieLaneCount, &table_member2);

	/* lclk dpm table setup */
	for (i = 0; i < MAX_PCIE_CONF; i++) {
		dpm_context->dpm_tables.pcie_table.pcie_gen[i] = table_member1[i];
		dpm_context->dpm_tables.pcie_table.pcie_lane[i] = table_member2[i];
	}

	for (i = 0; i < NUM_LINK_LEVELS; i++) {
		smu_pcie_arg = (i << 16) |
			((table_member1[i] <= pcie_gen_cap) ?
			 (table_member1[i] << 8) :
			 (pcie_gen_cap << 8)) |
			((table_member2[i] <= pcie_width_cap) ?
			 table_member2[i] :
			 pcie_width_cap);

		ret = smu_cmn_send_smc_msg_with_param(smu,
				SMU_MSG_OverridePcieParameters,
				smu_pcie_arg,
				NULL);
		if (ret)
			return ret;

		if (table_member1[i] > pcie_gen_cap)
			dpm_context->dpm_tables.pcie_table.pcie_gen[i] = pcie_gen_cap;
		if (table_member2[i] > pcie_width_cap)
			dpm_context->dpm_tables.pcie_table.pcie_lane[i] = pcie_width_cap;
	}

	return 0;
}

static int sienna_cichlid_get_dpm_ultimate_freq(struct smu_context *smu,
				enum smu_clk_type clk_type,
				uint32_t *min, uint32_t *max)
{
	return smu_v11_0_get_dpm_ultimate_freq(smu, clk_type, min, max);
}

static void sienna_cichlid_dump_od_table(struct smu_context *smu,
					 OverDriveTable_t *od_table)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t smu_version;

	dev_dbg(smu->adev->dev, "OD: Gfxclk: (%d, %d)\n", od_table->GfxclkFmin,
							  od_table->GfxclkFmax);
	dev_dbg(smu->adev->dev, "OD: Uclk: (%d, %d)\n", od_table->UclkFmin,
							od_table->UclkFmax);

	smu_cmn_get_smc_version(smu, NULL, &smu_version);
	if (!((adev->ip_versions[MP1_HWIP][0] == IP_VERSION(11, 0, 7)) &&
	       (smu_version < 0x003a2900)))
		dev_dbg(smu->adev->dev, "OD: VddGfxOffset: %d\n", od_table->VddGfxOffset);
}

static int sienna_cichlid_set_default_od_settings(struct smu_context *smu)
{
	OverDriveTable_t *od_table =
		(OverDriveTable_t *)smu->smu_table.overdrive_table;
	OverDriveTable_t *boot_od_table =
		(OverDriveTable_t *)smu->smu_table.boot_overdrive_table;
	OverDriveTable_t *user_od_table =
		(OverDriveTable_t *)smu->smu_table.user_overdrive_table;
	int ret = 0;

	/*
	 * For S3/S4/Runpm resume, no need to setup those overdrive tables again as
	 *   - either they already have the default OD settings got during cold bootup
	 *   - or they have some user customized OD settings which cannot be overwritten
	 */
	if (smu->adev->in_suspend)
		return 0;

	ret = smu_cmn_update_table(smu, SMU_TABLE_OVERDRIVE,
				   0, (void *)boot_od_table, false);
	if (ret) {
		dev_err(smu->adev->dev, "Failed to get overdrive table!\n");
		return ret;
	}

	sienna_cichlid_dump_od_table(smu, boot_od_table);

	memcpy(od_table, boot_od_table, sizeof(OverDriveTable_t));
	memcpy(user_od_table, boot_od_table, sizeof(OverDriveTable_t));

	return 0;
}

static int sienna_cichlid_od_setting_check_range(struct smu_context *smu,
						 struct smu_11_0_7_overdrive_table *od_table,
						 enum SMU_11_0_7_ODSETTING_ID setting,
						 uint32_t value)
{
	if (value < od_table->min[setting]) {
		dev_warn(smu->adev->dev, "OD setting (%d, %d) is less than the minimum allowed (%d)\n",
					  setting, value, od_table->min[setting]);
		return -EINVAL;
	}
	if (value > od_table->max[setting]) {
		dev_warn(smu->adev->dev, "OD setting (%d, %d) is greater than the maximum allowed (%d)\n",
					  setting, value, od_table->max[setting]);
		return -EINVAL;
	}

	return 0;
}

static int sienna_cichlid_od_edit_dpm_table(struct smu_context *smu,
					    enum PP_OD_DPM_TABLE_COMMAND type,
					    long input[], uint32_t size)
{
	struct smu_table_context *table_context = &smu->smu_table;
	OverDriveTable_t *od_table =
		(OverDriveTable_t *)table_context->overdrive_table;
	struct smu_11_0_7_overdrive_table *od_settings =
		(struct smu_11_0_7_overdrive_table *)smu->od_settings;
	struct amdgpu_device *adev = smu->adev;
	enum SMU_11_0_7_ODSETTING_ID freq_setting;
	uint16_t *freq_ptr;
	int i, ret = 0;
	uint32_t smu_version;

	if (!smu->od_enabled) {
		dev_warn(smu->adev->dev, "OverDrive is not enabled!\n");
		return -EINVAL;
	}

	if (!smu->od_settings) {
		dev_err(smu->adev->dev, "OD board limits are not set!\n");
		return -ENOENT;
	}

	if (!(table_context->overdrive_table && table_context->boot_overdrive_table)) {
		dev_err(smu->adev->dev, "Overdrive table was not initialized!\n");
		return -EINVAL;
	}

	switch (type) {
	case PP_OD_EDIT_SCLK_VDDC_TABLE:
		if (!sienna_cichlid_is_od_feature_supported(od_settings,
							    SMU_11_0_7_ODCAP_GFXCLK_LIMITS)) {
			dev_warn(smu->adev->dev, "GFXCLK_LIMITS not supported!\n");
			return -ENOTSUPP;
		}

		for (i = 0; i < size; i += 2) {
			if (i + 2 > size) {
				dev_info(smu->adev->dev, "invalid number of input parameters %d\n", size);
				return -EINVAL;
			}

			switch (input[i]) {
			case 0:
				if (input[i + 1] > od_table->GfxclkFmax) {
					dev_info(smu->adev->dev, "GfxclkFmin (%ld) must be <= GfxclkFmax (%u)!\n",
						input[i + 1], od_table->GfxclkFmax);
					return -EINVAL;
				}

				freq_setting = SMU_11_0_7_ODSETTING_GFXCLKFMIN;
				freq_ptr = &od_table->GfxclkFmin;
				break;

			case 1:
				if (input[i + 1] < od_table->GfxclkFmin) {
					dev_info(smu->adev->dev, "GfxclkFmax (%ld) must be >= GfxclkFmin (%u)!\n",
						input[i + 1], od_table->GfxclkFmin);
					return -EINVAL;
				}

				freq_setting = SMU_11_0_7_ODSETTING_GFXCLKFMAX;
				freq_ptr = &od_table->GfxclkFmax;
				break;

			default:
				dev_info(smu->adev->dev, "Invalid SCLK_VDDC_TABLE index: %ld\n", input[i]);
				dev_info(smu->adev->dev, "Supported indices: [0:min,1:max]\n");
				return -EINVAL;
			}

			ret = sienna_cichlid_od_setting_check_range(smu, od_settings,
								    freq_setting, input[i + 1]);
			if (ret)
				return ret;

			*freq_ptr = (uint16_t)input[i + 1];
		}
		break;

	case PP_OD_EDIT_MCLK_VDDC_TABLE:
		if (!sienna_cichlid_is_od_feature_supported(od_settings, SMU_11_0_7_ODCAP_UCLK_LIMITS)) {
			dev_warn(smu->adev->dev, "UCLK_LIMITS not supported!\n");
			return -ENOTSUPP;
		}

		for (i = 0; i < size; i += 2) {
			if (i + 2 > size) {
				dev_info(smu->adev->dev, "invalid number of input parameters %d\n", size);
				return -EINVAL;
			}

			switch (input[i]) {
			case 0:
				if (input[i + 1] > od_table->UclkFmax) {
					dev_info(smu->adev->dev, "UclkFmin (%ld) must be <= UclkFmax (%u)!\n",
						input[i + 1], od_table->UclkFmax);
					return -EINVAL;
				}

				freq_setting = SMU_11_0_7_ODSETTING_UCLKFMIN;
				freq_ptr = &od_table->UclkFmin;
				break;

			case 1:
				if (input[i + 1] < od_table->UclkFmin) {
					dev_info(smu->adev->dev, "UclkFmax (%ld) must be >= UclkFmin (%u)!\n",
						input[i + 1], od_table->UclkFmin);
					return -EINVAL;
				}

				freq_setting = SMU_11_0_7_ODSETTING_UCLKFMAX;
				freq_ptr = &od_table->UclkFmax;
				break;

			default:
				dev_info(smu->adev->dev, "Invalid MCLK_VDDC_TABLE index: %ld\n", input[i]);
				dev_info(smu->adev->dev, "Supported indices: [0:min,1:max]\n");
				return -EINVAL;
			}

			ret = sienna_cichlid_od_setting_check_range(smu, od_settings,
								    freq_setting, input[i + 1]);
			if (ret)
				return ret;

			*freq_ptr = (uint16_t)input[i + 1];
		}
		break;

	case PP_OD_RESTORE_DEFAULT_TABLE:
		memcpy(table_context->overdrive_table,
				table_context->boot_overdrive_table,
				sizeof(OverDriveTable_t));
		fallthrough;

	case PP_OD_COMMIT_DPM_TABLE:
		if (memcmp(od_table, table_context->user_overdrive_table, sizeof(OverDriveTable_t))) {
			sienna_cichlid_dump_od_table(smu, od_table);
			ret = smu_cmn_update_table(smu, SMU_TABLE_OVERDRIVE, 0, (void *)od_table, true);
			if (ret) {
				dev_err(smu->adev->dev, "Failed to import overdrive table!\n");
				return ret;
			}
			memcpy(table_context->user_overdrive_table, od_table, sizeof(OverDriveTable_t));
			smu->user_dpm_profile.user_od = true;

			if (!memcmp(table_context->user_overdrive_table,
				    table_context->boot_overdrive_table,
				    sizeof(OverDriveTable_t)))
				smu->user_dpm_profile.user_od = false;
		}
		break;

	case PP_OD_EDIT_VDDGFX_OFFSET:
		if (size != 1) {
			dev_info(smu->adev->dev, "invalid number of parameters: %d\n", size);
			return -EINVAL;
		}

		/*
		 * OD GFX Voltage Offset functionality is supported only by 58.41.0
		 * and onwards SMU firmwares.
		 */
		smu_cmn_get_smc_version(smu, NULL, &smu_version);
		if ((adev->ip_versions[MP1_HWIP][0] == IP_VERSION(11, 0, 7)) &&
		     (smu_version < 0x003a2900)) {
			dev_err(smu->adev->dev, "OD GFX Voltage offset functionality is supported "
						"only by 58.41.0 and onwards SMU firmwares!\n");
			return -EOPNOTSUPP;
		}

		od_table->VddGfxOffset = (int16_t)input[0];

		sienna_cichlid_dump_od_table(smu, od_table);
		break;

	default:
		return -ENOSYS;
	}

	return ret;
}

static int sienna_cichlid_run_btc(struct smu_context *smu)
{
	int res;

	res = smu_cmn_send_smc_msg(smu, SMU_MSG_RunDcBtc, NULL);
	if (res)
		dev_err(smu->adev->dev, "RunDcBtc failed!\n");

	return res;
}

static int sienna_cichlid_baco_enter(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;

	if (adev->in_runpm && smu_cmn_is_audio_func_enabled(adev))
		return smu_v11_0_baco_set_armd3_sequence(smu, BACO_SEQ_BACO);
	else
		return smu_v11_0_baco_enter(smu);
}

static int sienna_cichlid_baco_exit(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;

	if (adev->in_runpm && smu_cmn_is_audio_func_enabled(adev)) {
		/* Wait for PMFW handling for the Dstate change */
		msleep(10);
		return smu_v11_0_baco_set_armd3_sequence(smu, BACO_SEQ_ULPS);
	} else {
		return smu_v11_0_baco_exit(smu);
	}
}

static bool sienna_cichlid_is_mode1_reset_supported(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t val;
	u32 smu_version;

	/**
	 * SRIOV env will not support SMU mode1 reset
	 * PM FW support mode1 reset from 58.26
	 */
	smu_cmn_get_smc_version(smu, NULL, &smu_version);
	if (amdgpu_sriov_vf(adev) || (smu_version < 0x003a1a00))
		return false;

	/**
	 * mode1 reset relies on PSP, so we should check if
	 * PSP is alive.
	 */
	val = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_81);
	return val != 0x0;
}

static void beige_goby_dump_pptable(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_beige_goby_t *pptable = table_context->driver_pptable;
	int i;

	dev_info(smu->adev->dev, "Dumped PPTable:\n");

	dev_info(smu->adev->dev, "Version = 0x%08x\n", pptable->Version);
	dev_info(smu->adev->dev, "FeaturesToRun[0] = 0x%08x\n", pptable->FeaturesToRun[0]);
	dev_info(smu->adev->dev, "FeaturesToRun[1] = 0x%08x\n", pptable->FeaturesToRun[1]);

	for (i = 0; i < PPT_THROTTLER_COUNT; i++) {
		dev_info(smu->adev->dev, "SocketPowerLimitAc[%d] = 0x%x\n", i, pptable->SocketPowerLimitAc[i]);
		dev_info(smu->adev->dev, "SocketPowerLimitAcTau[%d] = 0x%x\n", i, pptable->SocketPowerLimitAcTau[i]);
		dev_info(smu->adev->dev, "SocketPowerLimitDc[%d] = 0x%x\n", i, pptable->SocketPowerLimitDc[i]);
		dev_info(smu->adev->dev, "SocketPowerLimitDcTau[%d] = 0x%x\n", i, pptable->SocketPowerLimitDcTau[i]);
	}

	for (i = 0; i < TDC_THROTTLER_COUNT; i++) {
		dev_info(smu->adev->dev, "TdcLimit[%d] = 0x%x\n", i, pptable->TdcLimit[i]);
		dev_info(smu->adev->dev, "TdcLimitTau[%d] = 0x%x\n", i, pptable->TdcLimitTau[i]);
	}

	for (i = 0; i < TEMP_COUNT; i++) {
		dev_info(smu->adev->dev, "TemperatureLimit[%d] = 0x%x\n", i, pptable->TemperatureLimit[i]);
	}

	dev_info(smu->adev->dev, "FitLimit = 0x%x\n", pptable->FitLimit);
	dev_info(smu->adev->dev, "TotalPowerConfig = 0x%x\n", pptable->TotalPowerConfig);
	dev_info(smu->adev->dev, "TotalPowerPadding[0] = 0x%x\n", pptable->TotalPowerPadding[0]);
	dev_info(smu->adev->dev, "TotalPowerPadding[1] = 0x%x\n", pptable->TotalPowerPadding[1]);
	dev_info(smu->adev->dev, "TotalPowerPadding[2] = 0x%x\n", pptable->TotalPowerPadding[2]);

	dev_info(smu->adev->dev, "ApccPlusResidencyLimit = 0x%x\n", pptable->ApccPlusResidencyLimit);
	for (i = 0; i < NUM_SMNCLK_DPM_LEVELS; i++) {
		dev_info(smu->adev->dev, "SmnclkDpmFreq[%d] = 0x%x\n", i, pptable->SmnclkDpmFreq[i]);
		dev_info(smu->adev->dev, "SmnclkDpmVoltage[%d] = 0x%x\n", i, pptable->SmnclkDpmVoltage[i]);
	}
	dev_info(smu->adev->dev, "ThrottlerControlMask = 0x%x\n", pptable->ThrottlerControlMask);

	dev_info(smu->adev->dev, "FwDStateMask = 0x%x\n", pptable->FwDStateMask);

	dev_info(smu->adev->dev, "UlvVoltageOffsetSoc = 0x%x\n", pptable->UlvVoltageOffsetSoc);
	dev_info(smu->adev->dev, "UlvVoltageOffsetGfx = 0x%x\n", pptable->UlvVoltageOffsetGfx);
	dev_info(smu->adev->dev, "MinVoltageUlvGfx = 0x%x\n", pptable->MinVoltageUlvGfx);
	dev_info(smu->adev->dev, "MinVoltageUlvSoc = 0x%x\n", pptable->MinVoltageUlvSoc);

	dev_info(smu->adev->dev, "SocLIVmin = 0x%x\n", pptable->SocLIVmin);

	dev_info(smu->adev->dev, "GceaLinkMgrIdleThreshold = 0x%x\n", pptable->GceaLinkMgrIdleThreshold);

	dev_info(smu->adev->dev, "MinVoltageGfx = 0x%x\n", pptable->MinVoltageGfx);
	dev_info(smu->adev->dev, "MinVoltageSoc = 0x%x\n", pptable->MinVoltageSoc);
	dev_info(smu->adev->dev, "MaxVoltageGfx = 0x%x\n", pptable->MaxVoltageGfx);
	dev_info(smu->adev->dev, "MaxVoltageSoc = 0x%x\n", pptable->MaxVoltageSoc);

	dev_info(smu->adev->dev, "LoadLineResistanceGfx = 0x%x\n", pptable->LoadLineResistanceGfx);
	dev_info(smu->adev->dev, "LoadLineResistanceSoc = 0x%x\n", pptable->LoadLineResistanceSoc);

	dev_info(smu->adev->dev, "VDDGFX_TVmin = 0x%x\n", pptable->VDDGFX_TVmin);
	dev_info(smu->adev->dev, "VDDSOC_TVmin = 0x%x\n", pptable->VDDSOC_TVmin);
	dev_info(smu->adev->dev, "VDDGFX_Vmin_HiTemp = 0x%x\n", pptable->VDDGFX_Vmin_HiTemp);
	dev_info(smu->adev->dev, "VDDGFX_Vmin_LoTemp = 0x%x\n", pptable->VDDGFX_Vmin_LoTemp);
	dev_info(smu->adev->dev, "VDDSOC_Vmin_HiTemp = 0x%x\n", pptable->VDDSOC_Vmin_HiTemp);
	dev_info(smu->adev->dev, "VDDSOC_Vmin_LoTemp = 0x%x\n", pptable->VDDSOC_Vmin_LoTemp);
	dev_info(smu->adev->dev, "VDDGFX_TVminHystersis = 0x%x\n", pptable->VDDGFX_TVminHystersis);
	dev_info(smu->adev->dev, "VDDSOC_TVminHystersis = 0x%x\n", pptable->VDDSOC_TVminHystersis);

	dev_info(smu->adev->dev, "[PPCLK_GFXCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_GFXCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_GFXCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_GFXCLK].Padding,
			pptable->DpmDescriptor[PPCLK_GFXCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_GFXCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SsFmin,
			pptable->DpmDescriptor[PPCLK_GFXCLK].Padding16);

	dev_info(smu->adev->dev, "[PPCLK_SOCCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_SOCCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_SOCCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_SOCCLK].Padding,
			pptable->DpmDescriptor[PPCLK_SOCCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_SOCCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SsFmin,
			pptable->DpmDescriptor[PPCLK_SOCCLK].Padding16);

	dev_info(smu->adev->dev, "[PPCLK_UCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_UCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_UCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_UCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_UCLK].Padding,
			pptable->DpmDescriptor[PPCLK_UCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_UCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_UCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_UCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_UCLK].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_UCLK].SsFmin,
			pptable->DpmDescriptor[PPCLK_UCLK].Padding16);

	dev_info(smu->adev->dev, "[PPCLK_FCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_FCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_FCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_FCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_FCLK].Padding,
			pptable->DpmDescriptor[PPCLK_FCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_FCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_FCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_FCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_FCLK].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_FCLK].SsFmin,
			pptable->DpmDescriptor[PPCLK_FCLK].Padding16);

	dev_info(smu->adev->dev, "[PPCLK_DCLK_0]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_DCLK_0].VoltageMode,
			pptable->DpmDescriptor[PPCLK_DCLK_0].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_DCLK_0].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_DCLK_0].Padding,
			pptable->DpmDescriptor[PPCLK_DCLK_0].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_DCLK_0].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_DCLK_0].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_DCLK_0].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_DCLK_0].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_DCLK_0].SsFmin,
			pptable->DpmDescriptor[PPCLK_DCLK_0].Padding16);

	dev_info(smu->adev->dev, "[PPCLK_VCLK_0]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_VCLK_0].VoltageMode,
			pptable->DpmDescriptor[PPCLK_VCLK_0].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_VCLK_0].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_VCLK_0].Padding,
			pptable->DpmDescriptor[PPCLK_VCLK_0].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_VCLK_0].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_VCLK_0].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_VCLK_0].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_VCLK_0].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_VCLK_0].SsFmin,
			pptable->DpmDescriptor[PPCLK_VCLK_0].Padding16);

	dev_info(smu->adev->dev, "[PPCLK_DCLK_1]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_DCLK_1].VoltageMode,
			pptable->DpmDescriptor[PPCLK_DCLK_1].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_DCLK_1].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_DCLK_1].Padding,
			pptable->DpmDescriptor[PPCLK_DCLK_1].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_DCLK_1].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_DCLK_1].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_DCLK_1].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_DCLK_1].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_DCLK_1].SsFmin,
			pptable->DpmDescriptor[PPCLK_DCLK_1].Padding16);

	dev_info(smu->adev->dev, "[PPCLK_VCLK_1]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_VCLK_1].VoltageMode,
			pptable->DpmDescriptor[PPCLK_VCLK_1].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_VCLK_1].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_VCLK_1].Padding,
			pptable->DpmDescriptor[PPCLK_VCLK_1].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_VCLK_1].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_VCLK_1].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_VCLK_1].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_VCLK_1].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_VCLK_1].SsFmin,
			pptable->DpmDescriptor[PPCLK_VCLK_1].Padding16);

	dev_info(smu->adev->dev, "FreqTableGfx\n");
	for (i = 0; i < NUM_GFXCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%02d] = 0x%x\n", i, pptable->FreqTableGfx[i]);

	dev_info(smu->adev->dev, "FreqTableVclk\n");
	for (i = 0; i < NUM_VCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%02d] = 0x%x\n", i, pptable->FreqTableVclk[i]);

	dev_info(smu->adev->dev, "FreqTableDclk\n");
	for (i = 0; i < NUM_DCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%02d] = 0x%x\n", i, pptable->FreqTableDclk[i]);

	dev_info(smu->adev->dev, "FreqTableSocclk\n");
	for (i = 0; i < NUM_SOCCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%02d] = 0x%x\n", i, pptable->FreqTableSocclk[i]);

	dev_info(smu->adev->dev, "FreqTableUclk\n");
	for (i = 0; i < NUM_UCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%02d] = 0x%x\n", i, pptable->FreqTableUclk[i]);

	dev_info(smu->adev->dev, "FreqTableFclk\n");
	for (i = 0; i < NUM_FCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%02d] = 0x%x\n", i, pptable->FreqTableFclk[i]);

	dev_info(smu->adev->dev, "DcModeMaxFreq\n");
	dev_info(smu->adev->dev, "  .PPCLK_GFXCLK = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_GFXCLK]);
	dev_info(smu->adev->dev, "  .PPCLK_SOCCLK = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_SOCCLK]);
	dev_info(smu->adev->dev, "  .PPCLK_UCLK   = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_UCLK]);
	dev_info(smu->adev->dev, "  .PPCLK_FCLK   = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_FCLK]);
	dev_info(smu->adev->dev, "  .PPCLK_DCLK_0 = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_DCLK_0]);
	dev_info(smu->adev->dev, "  .PPCLK_VCLK_0 = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_VCLK_0]);
	dev_info(smu->adev->dev, "  .PPCLK_DCLK_1 = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_DCLK_1]);
	dev_info(smu->adev->dev, "  .PPCLK_VCLK_1 = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_VCLK_1]);

	dev_info(smu->adev->dev, "FreqTableUclkDiv\n");
	for (i = 0; i < NUM_UCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->FreqTableUclkDiv[i]);

	dev_info(smu->adev->dev, "FclkBoostFreq = 0x%x\n", pptable->FclkBoostFreq);
	dev_info(smu->adev->dev, "FclkParamPadding = 0x%x\n", pptable->FclkParamPadding);

	dev_info(smu->adev->dev, "Mp0clkFreq\n");
	for (i = 0; i < NUM_MP0CLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->Mp0clkFreq[i]);

	dev_info(smu->adev->dev, "Mp0DpmVoltage\n");
	for (i = 0; i < NUM_MP0CLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->Mp0DpmVoltage[i]);

	dev_info(smu->adev->dev, "MemVddciVoltage\n");
	for (i = 0; i < NUM_UCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->MemVddciVoltage[i]);

	dev_info(smu->adev->dev, "MemMvddVoltage\n");
	for (i = 0; i < NUM_UCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->MemMvddVoltage[i]);

	dev_info(smu->adev->dev, "GfxclkFgfxoffEntry = 0x%x\n", pptable->GfxclkFgfxoffEntry);
	dev_info(smu->adev->dev, "GfxclkFinit = 0x%x\n", pptable->GfxclkFinit);
	dev_info(smu->adev->dev, "GfxclkFidle = 0x%x\n", pptable->GfxclkFidle);
	dev_info(smu->adev->dev, "GfxclkSource = 0x%x\n", pptable->GfxclkSource);
	dev_info(smu->adev->dev, "GfxclkPadding = 0x%x\n", pptable->GfxclkPadding);

	dev_info(smu->adev->dev, "GfxGpoSubFeatureMask = 0x%x\n", pptable->GfxGpoSubFeatureMask);

	dev_info(smu->adev->dev, "GfxGpoEnabledWorkPolicyMask = 0x%x\n", pptable->GfxGpoEnabledWorkPolicyMask);
	dev_info(smu->adev->dev, "GfxGpoDisabledWorkPolicyMask = 0x%x\n", pptable->GfxGpoDisabledWorkPolicyMask);
	dev_info(smu->adev->dev, "GfxGpoPadding[0] = 0x%x\n", pptable->GfxGpoPadding[0]);
	dev_info(smu->adev->dev, "GfxGpoVotingAllow = 0x%x\n", pptable->GfxGpoVotingAllow);
	dev_info(smu->adev->dev, "GfxGpoPadding32[0] = 0x%x\n", pptable->GfxGpoPadding32[0]);
	dev_info(smu->adev->dev, "GfxGpoPadding32[1] = 0x%x\n", pptable->GfxGpoPadding32[1]);
	dev_info(smu->adev->dev, "GfxGpoPadding32[2] = 0x%x\n", pptable->GfxGpoPadding32[2]);
	dev_info(smu->adev->dev, "GfxGpoPadding32[3] = 0x%x\n", pptable->GfxGpoPadding32[3]);
	dev_info(smu->adev->dev, "GfxDcsFopt = 0x%x\n", pptable->GfxDcsFopt);
	dev_info(smu->adev->dev, "GfxDcsFclkFopt = 0x%x\n", pptable->GfxDcsFclkFopt);
	dev_info(smu->adev->dev, "GfxDcsUclkFopt = 0x%x\n", pptable->GfxDcsUclkFopt);

	dev_info(smu->adev->dev, "DcsGfxOffVoltage = 0x%x\n", pptable->DcsGfxOffVoltage);
	dev_info(smu->adev->dev, "DcsMinGfxOffTime = 0x%x\n", pptable->DcsMinGfxOffTime);
	dev_info(smu->adev->dev, "DcsMaxGfxOffTime = 0x%x\n", pptable->DcsMaxGfxOffTime);
	dev_info(smu->adev->dev, "DcsMinCreditAccum = 0x%x\n", pptable->DcsMinCreditAccum);
	dev_info(smu->adev->dev, "DcsExitHysteresis = 0x%x\n", pptable->DcsExitHysteresis);
	dev_info(smu->adev->dev, "DcsTimeout = 0x%x\n", pptable->DcsTimeout);

	dev_info(smu->adev->dev, "DcsParamPadding[0] = 0x%x\n", pptable->DcsParamPadding[0]);
	dev_info(smu->adev->dev, "DcsParamPadding[1] = 0x%x\n", pptable->DcsParamPadding[1]);
	dev_info(smu->adev->dev, "DcsParamPadding[2] = 0x%x\n", pptable->DcsParamPadding[2]);
	dev_info(smu->adev->dev, "DcsParamPadding[3] = 0x%x\n", pptable->DcsParamPadding[3]);
	dev_info(smu->adev->dev, "DcsParamPadding[4] = 0x%x\n", pptable->DcsParamPadding[4]);

	dev_info(smu->adev->dev, "FlopsPerByteTable\n");
	for (i = 0; i < RLC_PACE_TABLE_NUM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->FlopsPerByteTable[i]);

	dev_info(smu->adev->dev, "LowestUclkReservedForUlv = 0x%x\n", pptable->LowestUclkReservedForUlv);
	dev_info(smu->adev->dev, "vddingMem[0] = 0x%x\n", pptable->PaddingMem[0]);
	dev_info(smu->adev->dev, "vddingMem[1] = 0x%x\n", pptable->PaddingMem[1]);
	dev_info(smu->adev->dev, "vddingMem[2] = 0x%x\n", pptable->PaddingMem[2]);

	dev_info(smu->adev->dev, "UclkDpmPstates\n");
	for (i = 0; i < NUM_UCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->UclkDpmPstates[i]);

	dev_info(smu->adev->dev, "UclkDpmSrcFreqRange\n");
	dev_info(smu->adev->dev, "  .Fmin = 0x%x\n",
		pptable->UclkDpmSrcFreqRange.Fmin);
	dev_info(smu->adev->dev, "  .Fmax = 0x%x\n",
		pptable->UclkDpmSrcFreqRange.Fmax);
	dev_info(smu->adev->dev, "UclkDpmTargFreqRange\n");
	dev_info(smu->adev->dev, "  .Fmin = 0x%x\n",
		pptable->UclkDpmTargFreqRange.Fmin);
	dev_info(smu->adev->dev, "  .Fmax = 0x%x\n",
		pptable->UclkDpmTargFreqRange.Fmax);
	dev_info(smu->adev->dev, "UclkDpmMidstepFreq = 0x%x\n", pptable->UclkDpmMidstepFreq);
	dev_info(smu->adev->dev, "UclkMidstepPadding = 0x%x\n", pptable->UclkMidstepPadding);

	dev_info(smu->adev->dev, "PcieGenSpeed\n");
	for (i = 0; i < NUM_LINK_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->PcieGenSpeed[i]);

	dev_info(smu->adev->dev, "PcieLaneCount\n");
	for (i = 0; i < NUM_LINK_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->PcieLaneCount[i]);

	dev_info(smu->adev->dev, "LclkFreq\n");
	for (i = 0; i < NUM_LINK_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->LclkFreq[i]);

	dev_info(smu->adev->dev, "FanStopTemp = 0x%x\n", pptable->FanStopTemp);
	dev_info(smu->adev->dev, "FanStartTemp = 0x%x\n", pptable->FanStartTemp);

	dev_info(smu->adev->dev, "FanGain\n");
	for (i = 0; i < TEMP_COUNT; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->FanGain[i]);

	dev_info(smu->adev->dev, "FanPwmMin = 0x%x\n", pptable->FanPwmMin);
	dev_info(smu->adev->dev, "FanAcousticLimitRpm = 0x%x\n", pptable->FanAcousticLimitRpm);
	dev_info(smu->adev->dev, "FanThrottlingRpm = 0x%x\n", pptable->FanThrottlingRpm);
	dev_info(smu->adev->dev, "FanMaximumRpm = 0x%x\n", pptable->FanMaximumRpm);
	dev_info(smu->adev->dev, "MGpuFanBoostLimitRpm = 0x%x\n", pptable->MGpuFanBoostLimitRpm);
	dev_info(smu->adev->dev, "FanTargetTemperature = 0x%x\n", pptable->FanTargetTemperature);
	dev_info(smu->adev->dev, "FanTargetGfxclk = 0x%x\n", pptable->FanTargetGfxclk);
	dev_info(smu->adev->dev, "FanPadding16 = 0x%x\n", pptable->FanPadding16);
	dev_info(smu->adev->dev, "FanTempInputSelect = 0x%x\n", pptable->FanTempInputSelect);
	dev_info(smu->adev->dev, "FanPadding = 0x%x\n", pptable->FanPadding);
	dev_info(smu->adev->dev, "FanZeroRpmEnable = 0x%x\n", pptable->FanZeroRpmEnable);
	dev_info(smu->adev->dev, "FanTachEdgePerRev = 0x%x\n", pptable->FanTachEdgePerRev);

	dev_info(smu->adev->dev, "FuzzyFan_ErrorSetDelta = 0x%x\n", pptable->FuzzyFan_ErrorSetDelta);
	dev_info(smu->adev->dev, "FuzzyFan_ErrorRateSetDelta = 0x%x\n", pptable->FuzzyFan_ErrorRateSetDelta);
	dev_info(smu->adev->dev, "FuzzyFan_PwmSetDelta = 0x%x\n", pptable->FuzzyFan_PwmSetDelta);
	dev_info(smu->adev->dev, "FuzzyFan_Reserved = 0x%x\n", pptable->FuzzyFan_Reserved);

	dev_info(smu->adev->dev, "OverrideAvfsGb[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->OverrideAvfsGb[AVFS_VOLTAGE_GFX]);
	dev_info(smu->adev->dev, "OverrideAvfsGb[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->OverrideAvfsGb[AVFS_VOLTAGE_SOC]);
	dev_info(smu->adev->dev, "dBtcGbGfxDfllModelSelect = 0x%x\n", pptable->dBtcGbGfxDfllModelSelect);
	dev_info(smu->adev->dev, "Padding8_Avfs = 0x%x\n", pptable->Padding8_Avfs);

	dev_info(smu->adev->dev, "qAvfsGb[AVFS_VOLTAGE_GFX]{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->qAvfsGb[AVFS_VOLTAGE_GFX].a,
			pptable->qAvfsGb[AVFS_VOLTAGE_GFX].b,
			pptable->qAvfsGb[AVFS_VOLTAGE_GFX].c);
	dev_info(smu->adev->dev, "qAvfsGb[AVFS_VOLTAGE_SOC]{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->qAvfsGb[AVFS_VOLTAGE_SOC].a,
			pptable->qAvfsGb[AVFS_VOLTAGE_SOC].b,
			pptable->qAvfsGb[AVFS_VOLTAGE_SOC].c);
	dev_info(smu->adev->dev, "dBtcGbGfxPll{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->dBtcGbGfxPll.a,
			pptable->dBtcGbGfxPll.b,
			pptable->dBtcGbGfxPll.c);
	dev_info(smu->adev->dev, "dBtcGbGfxAfll{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->dBtcGbGfxDfll.a,
			pptable->dBtcGbGfxDfll.b,
			pptable->dBtcGbGfxDfll.c);
	dev_info(smu->adev->dev, "dBtcGbSoc{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->dBtcGbSoc.a,
			pptable->dBtcGbSoc.b,
			pptable->dBtcGbSoc.c);
	dev_info(smu->adev->dev, "qAgingGb[AVFS_VOLTAGE_GFX]{m = 0x%x b = 0x%x}\n",
			pptable->qAgingGb[AVFS_VOLTAGE_GFX].m,
			pptable->qAgingGb[AVFS_VOLTAGE_GFX].b);
	dev_info(smu->adev->dev, "qAgingGb[AVFS_VOLTAGE_SOC]{m = 0x%x b = 0x%x}\n",
			pptable->qAgingGb[AVFS_VOLTAGE_SOC].m,
			pptable->qAgingGb[AVFS_VOLTAGE_SOC].b);

	dev_info(smu->adev->dev, "PiecewiseLinearDroopIntGfxDfll\n");
	for (i = 0; i < NUM_PIECE_WISE_LINEAR_DROOP_MODEL_VF_POINTS; i++) {
		dev_info(smu->adev->dev, "		Fset[%d] = 0x%x\n",
			i, pptable->PiecewiseLinearDroopIntGfxDfll.Fset[i]);
		dev_info(smu->adev->dev, "		Vdroop[%d] = 0x%x\n",
			i, pptable->PiecewiseLinearDroopIntGfxDfll.Vdroop[i]);
	}

	dev_info(smu->adev->dev, "qStaticVoltageOffset[AVFS_VOLTAGE_GFX]{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_GFX].a,
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_GFX].b,
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_GFX].c);
	dev_info(smu->adev->dev, "qStaticVoltageOffset[AVFS_VOLTAGE_SOC]{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_SOC].a,
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_SOC].b,
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_SOC].c);

	dev_info(smu->adev->dev, "DcTol[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcTol[AVFS_VOLTAGE_GFX]);
	dev_info(smu->adev->dev, "DcTol[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcTol[AVFS_VOLTAGE_SOC]);

	dev_info(smu->adev->dev, "DcBtcEnabled[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcBtcEnabled[AVFS_VOLTAGE_GFX]);
	dev_info(smu->adev->dev, "DcBtcEnabled[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcBtcEnabled[AVFS_VOLTAGE_SOC]);
	dev_info(smu->adev->dev, "Padding8_GfxBtc[0] = 0x%x\n", pptable->Padding8_GfxBtc[0]);
	dev_info(smu->adev->dev, "Padding8_GfxBtc[1] = 0x%x\n", pptable->Padding8_GfxBtc[1]);

	dev_info(smu->adev->dev, "DcBtcMin[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcBtcMin[AVFS_VOLTAGE_GFX]);
	dev_info(smu->adev->dev, "DcBtcMin[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcBtcMin[AVFS_VOLTAGE_SOC]);
	dev_info(smu->adev->dev, "DcBtcMax[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcBtcMax[AVFS_VOLTAGE_GFX]);
	dev_info(smu->adev->dev, "DcBtcMax[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcBtcMax[AVFS_VOLTAGE_SOC]);

	dev_info(smu->adev->dev, "DcBtcGb[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcBtcGb[AVFS_VOLTAGE_GFX]);
	dev_info(smu->adev->dev, "DcBtcGb[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcBtcGb[AVFS_VOLTAGE_SOC]);

	dev_info(smu->adev->dev, "XgmiDpmPstates\n");
	for (i = 0; i < NUM_XGMI_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->XgmiDpmPstates[i]);
	dev_info(smu->adev->dev, "XgmiDpmSpare[0] = 0x%02x\n", pptable->XgmiDpmSpare[0]);
	dev_info(smu->adev->dev, "XgmiDpmSpare[1] = 0x%02x\n", pptable->XgmiDpmSpare[1]);

	dev_info(smu->adev->dev, "DebugOverrides = 0x%x\n", pptable->DebugOverrides);
	dev_info(smu->adev->dev, "ReservedEquation0{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->ReservedEquation0.a,
			pptable->ReservedEquation0.b,
			pptable->ReservedEquation0.c);
	dev_info(smu->adev->dev, "ReservedEquation1{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->ReservedEquation1.a,
			pptable->ReservedEquation1.b,
			pptable->ReservedEquation1.c);
	dev_info(smu->adev->dev, "ReservedEquation2{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->ReservedEquation2.a,
			pptable->ReservedEquation2.b,
			pptable->ReservedEquation2.c);
	dev_info(smu->adev->dev, "ReservedEquation3{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->ReservedEquation3.a,
			pptable->ReservedEquation3.b,
			pptable->ReservedEquation3.c);

	dev_info(smu->adev->dev, "SkuReserved[0] = 0x%x\n", pptable->SkuReserved[0]);
	dev_info(smu->adev->dev, "SkuReserved[1] = 0x%x\n", pptable->SkuReserved[1]);
	dev_info(smu->adev->dev, "SkuReserved[2] = 0x%x\n", pptable->SkuReserved[2]);
	dev_info(smu->adev->dev, "SkuReserved[3] = 0x%x\n", pptable->SkuReserved[3]);
	dev_info(smu->adev->dev, "SkuReserved[4] = 0x%x\n", pptable->SkuReserved[4]);
	dev_info(smu->adev->dev, "SkuReserved[5] = 0x%x\n", pptable->SkuReserved[5]);
	dev_info(smu->adev->dev, "SkuReserved[6] = 0x%x\n", pptable->SkuReserved[6]);
	dev_info(smu->adev->dev, "SkuReserved[7] = 0x%x\n", pptable->SkuReserved[7]);

	dev_info(smu->adev->dev, "GamingClk[0] = 0x%x\n", pptable->GamingClk[0]);
	dev_info(smu->adev->dev, "GamingClk[1] = 0x%x\n", pptable->GamingClk[1]);
	dev_info(smu->adev->dev, "GamingClk[2] = 0x%x\n", pptable->GamingClk[2]);
	dev_info(smu->adev->dev, "GamingClk[3] = 0x%x\n", pptable->GamingClk[3]);
	dev_info(smu->adev->dev, "GamingClk[4] = 0x%x\n", pptable->GamingClk[4]);
	dev_info(smu->adev->dev, "GamingClk[5] = 0x%x\n", pptable->GamingClk[5]);

	for (i = 0; i < NUM_I2C_CONTROLLERS; i++) {
		dev_info(smu->adev->dev, "I2cControllers[%d]:\n", i);
		dev_info(smu->adev->dev, "                   .Enabled = 0x%x\n",
				pptable->I2cControllers[i].Enabled);
		dev_info(smu->adev->dev, "                   .Speed = 0x%x\n",
				pptable->I2cControllers[i].Speed);
		dev_info(smu->adev->dev, "                   .SlaveAddress = 0x%x\n",
				pptable->I2cControllers[i].SlaveAddress);
		dev_info(smu->adev->dev, "                   .ControllerPort = 0x%x\n",
				pptable->I2cControllers[i].ControllerPort);
		dev_info(smu->adev->dev, "                   .ControllerName = 0x%x\n",
				pptable->I2cControllers[i].ControllerName);
		dev_info(smu->adev->dev, "                   .ThermalThrottler = 0x%x\n",
				pptable->I2cControllers[i].ThermalThrotter);
		dev_info(smu->adev->dev, "                   .I2cProtocol = 0x%x\n",
				pptable->I2cControllers[i].I2cProtocol);
		dev_info(smu->adev->dev, "                   .PaddingConfig = 0x%x\n",
				pptable->I2cControllers[i].PaddingConfig);
	}

	dev_info(smu->adev->dev, "GpioScl = 0x%x\n", pptable->GpioScl);
	dev_info(smu->adev->dev, "GpioSda = 0x%x\n", pptable->GpioSda);
	dev_info(smu->adev->dev, "FchUsbPdSlaveAddr = 0x%x\n", pptable->FchUsbPdSlaveAddr);
	dev_info(smu->adev->dev, "I2cSpare[0] = 0x%x\n", pptable->I2cSpare[0]);

	dev_info(smu->adev->dev, "Board Parameters:\n");
	dev_info(smu->adev->dev, "VddGfxVrMapping = 0x%x\n", pptable->VddGfxVrMapping);
	dev_info(smu->adev->dev, "VddSocVrMapping = 0x%x\n", pptable->VddSocVrMapping);
	dev_info(smu->adev->dev, "VddMem0VrMapping = 0x%x\n", pptable->VddMem0VrMapping);
	dev_info(smu->adev->dev, "VddMem1VrMapping = 0x%x\n", pptable->VddMem1VrMapping);
	dev_info(smu->adev->dev, "GfxUlvPhaseSheddingMask = 0x%x\n", pptable->GfxUlvPhaseSheddingMask);
	dev_info(smu->adev->dev, "SocUlvPhaseSheddingMask = 0x%x\n", pptable->SocUlvPhaseSheddingMask);
	dev_info(smu->adev->dev, "VddciUlvPhaseSheddingMask = 0x%x\n", pptable->VddciUlvPhaseSheddingMask);
	dev_info(smu->adev->dev, "MvddUlvPhaseSheddingMask = 0x%x\n", pptable->MvddUlvPhaseSheddingMask);

	dev_info(smu->adev->dev, "GfxMaxCurrent = 0x%x\n", pptable->GfxMaxCurrent);
	dev_info(smu->adev->dev, "GfxOffset = 0x%x\n", pptable->GfxOffset);
	dev_info(smu->adev->dev, "Padding_TelemetryGfx = 0x%x\n", pptable->Padding_TelemetryGfx);

	dev_info(smu->adev->dev, "SocMaxCurrent = 0x%x\n", pptable->SocMaxCurrent);
	dev_info(smu->adev->dev, "SocOffset = 0x%x\n", pptable->SocOffset);
	dev_info(smu->adev->dev, "Padding_TelemetrySoc = 0x%x\n", pptable->Padding_TelemetrySoc);

	dev_info(smu->adev->dev, "Mem0MaxCurrent = 0x%x\n", pptable->Mem0MaxCurrent);
	dev_info(smu->adev->dev, "Mem0Offset = 0x%x\n", pptable->Mem0Offset);
	dev_info(smu->adev->dev, "Padding_TelemetryMem0 = 0x%x\n", pptable->Padding_TelemetryMem0);

	dev_info(smu->adev->dev, "Mem1MaxCurrent = 0x%x\n", pptable->Mem1MaxCurrent);
	dev_info(smu->adev->dev, "Mem1Offset = 0x%x\n", pptable->Mem1Offset);
	dev_info(smu->adev->dev, "Padding_TelemetryMem1 = 0x%x\n", pptable->Padding_TelemetryMem1);

	dev_info(smu->adev->dev, "MvddRatio = 0x%x\n", pptable->MvddRatio);

	dev_info(smu->adev->dev, "AcDcGpio = 0x%x\n", pptable->AcDcGpio);
	dev_info(smu->adev->dev, "AcDcPolarity = 0x%x\n", pptable->AcDcPolarity);
	dev_info(smu->adev->dev, "VR0HotGpio = 0x%x\n", pptable->VR0HotGpio);
	dev_info(smu->adev->dev, "VR0HotPolarity = 0x%x\n", pptable->VR0HotPolarity);
	dev_info(smu->adev->dev, "VR1HotGpio = 0x%x\n", pptable->VR1HotGpio);
	dev_info(smu->adev->dev, "VR1HotPolarity = 0x%x\n", pptable->VR1HotPolarity);
	dev_info(smu->adev->dev, "GthrGpio = 0x%x\n", pptable->GthrGpio);
	dev_info(smu->adev->dev, "GthrPolarity = 0x%x\n", pptable->GthrPolarity);
	dev_info(smu->adev->dev, "LedPin0 = 0x%x\n", pptable->LedPin0);
	dev_info(smu->adev->dev, "LedPin1 = 0x%x\n", pptable->LedPin1);
	dev_info(smu->adev->dev, "LedPin2 = 0x%x\n", pptable->LedPin2);
	dev_info(smu->adev->dev, "LedEnableMask = 0x%x\n", pptable->LedEnableMask);
	dev_info(smu->adev->dev, "LedPcie = 0x%x\n", pptable->LedPcie);
	dev_info(smu->adev->dev, "LedError = 0x%x\n", pptable->LedError);
	dev_info(smu->adev->dev, "LedSpare1[0] = 0x%x\n", pptable->LedSpare1[0]);
	dev_info(smu->adev->dev, "LedSpare1[1] = 0x%x\n", pptable->LedSpare1[1]);

	dev_info(smu->adev->dev, "PllGfxclkSpreadEnabled = 0x%x\n", pptable->PllGfxclkSpreadEnabled);
	dev_info(smu->adev->dev, "PllGfxclkSpreadPercent = 0x%x\n", pptable->PllGfxclkSpreadPercent);
	dev_info(smu->adev->dev, "PllGfxclkSpreadFreq = 0x%x\n",    pptable->PllGfxclkSpreadFreq);

	dev_info(smu->adev->dev, "DfllGfxclkSpreadEnabled = 0x%x\n", pptable->DfllGfxclkSpreadEnabled);
	dev_info(smu->adev->dev, "DfllGfxclkSpreadPercent = 0x%x\n", pptable->DfllGfxclkSpreadPercent);
	dev_info(smu->adev->dev, "DfllGfxclkSpreadFreq = 0x%x\n",    pptable->DfllGfxclkSpreadFreq);

	dev_info(smu->adev->dev, "UclkSpreadPadding = 0x%x\n", pptable->UclkSpreadPadding);
	dev_info(smu->adev->dev, "UclkSpreadFreq = 0x%x\n", pptable->UclkSpreadFreq);

	dev_info(smu->adev->dev, "FclkSpreadEnabled = 0x%x\n", pptable->FclkSpreadEnabled);
	dev_info(smu->adev->dev, "FclkSpreadPercent = 0x%x\n", pptable->FclkSpreadPercent);
	dev_info(smu->adev->dev, "FclkSpreadFreq = 0x%x\n", pptable->FclkSpreadFreq);

	dev_info(smu->adev->dev, "MemoryChannelEnabled = 0x%x\n", pptable->MemoryChannelEnabled);
	dev_info(smu->adev->dev, "DramBitWidth = 0x%x\n", pptable->DramBitWidth);
	dev_info(smu->adev->dev, "PaddingMem1[0] = 0x%x\n", pptable->PaddingMem1[0]);
	dev_info(smu->adev->dev, "PaddingMem1[1] = 0x%x\n", pptable->PaddingMem1[1]);
	dev_info(smu->adev->dev, "PaddingMem1[2] = 0x%x\n", pptable->PaddingMem1[2]);

	dev_info(smu->adev->dev, "TotalBoardPower = 0x%x\n", pptable->TotalBoardPower);
	dev_info(smu->adev->dev, "BoardPowerPadding = 0x%x\n", pptable->BoardPowerPadding);

	dev_info(smu->adev->dev, "XgmiLinkSpeed\n");
	for (i = 0; i < NUM_XGMI_PSTATE_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->XgmiLinkSpeed[i]);
	dev_info(smu->adev->dev, "XgmiLinkWidth\n");
	for (i = 0; i < NUM_XGMI_PSTATE_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->XgmiLinkWidth[i]);
	dev_info(smu->adev->dev, "XgmiFclkFreq\n");
	for (i = 0; i < NUM_XGMI_PSTATE_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->XgmiFclkFreq[i]);
	dev_info(smu->adev->dev, "XgmiSocVoltage\n");
	for (i = 0; i < NUM_XGMI_PSTATE_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->XgmiSocVoltage[i]);

	dev_info(smu->adev->dev, "HsrEnabled = 0x%x\n", pptable->HsrEnabled);
	dev_info(smu->adev->dev, "VddqOffEnabled = 0x%x\n", pptable->VddqOffEnabled);
	dev_info(smu->adev->dev, "PaddingUmcFlags[0] = 0x%x\n", pptable->PaddingUmcFlags[0]);
	dev_info(smu->adev->dev, "PaddingUmcFlags[1] = 0x%x\n", pptable->PaddingUmcFlags[1]);

	dev_info(smu->adev->dev, "BoardReserved[0] = 0x%x\n", pptable->BoardReserved[0]);
	dev_info(smu->adev->dev, "BoardReserved[1] = 0x%x\n", pptable->BoardReserved[1]);
	dev_info(smu->adev->dev, "BoardReserved[2] = 0x%x\n", pptable->BoardReserved[2]);
	dev_info(smu->adev->dev, "BoardReserved[3] = 0x%x\n", pptable->BoardReserved[3]);
	dev_info(smu->adev->dev, "BoardReserved[4] = 0x%x\n", pptable->BoardReserved[4]);
	dev_info(smu->adev->dev, "BoardReserved[5] = 0x%x\n", pptable->BoardReserved[5]);
	dev_info(smu->adev->dev, "BoardReserved[6] = 0x%x\n", pptable->BoardReserved[6]);
	dev_info(smu->adev->dev, "BoardReserved[7] = 0x%x\n", pptable->BoardReserved[7]);
	dev_info(smu->adev->dev, "BoardReserved[8] = 0x%x\n", pptable->BoardReserved[8]);
	dev_info(smu->adev->dev, "BoardReserved[9] = 0x%x\n", pptable->BoardReserved[9]);
	dev_info(smu->adev->dev, "BoardReserved[10] = 0x%x\n", pptable->BoardReserved[10]);

	dev_info(smu->adev->dev, "MmHubPadding[0] = 0x%x\n", pptable->MmHubPadding[0]);
	dev_info(smu->adev->dev, "MmHubPadding[1] = 0x%x\n", pptable->MmHubPadding[1]);
	dev_info(smu->adev->dev, "MmHubPadding[2] = 0x%x\n", pptable->MmHubPadding[2]);
	dev_info(smu->adev->dev, "MmHubPadding[3] = 0x%x\n", pptable->MmHubPadding[3]);
	dev_info(smu->adev->dev, "MmHubPadding[4] = 0x%x\n", pptable->MmHubPadding[4]);
	dev_info(smu->adev->dev, "MmHubPadding[5] = 0x%x\n", pptable->MmHubPadding[5]);
	dev_info(smu->adev->dev, "MmHubPadding[6] = 0x%x\n", pptable->MmHubPadding[6]);
	dev_info(smu->adev->dev, "MmHubPadding[7] = 0x%x\n", pptable->MmHubPadding[7]);
}

static void sienna_cichlid_dump_pptable(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *pptable = table_context->driver_pptable;
	int i;

	if (smu->adev->ip_versions[MP1_HWIP][0] == IP_VERSION(11, 0, 13)) {
		beige_goby_dump_pptable(smu);
		return;
	}

	dev_info(smu->adev->dev, "Dumped PPTable:\n");

	dev_info(smu->adev->dev, "Version = 0x%08x\n", pptable->Version);
	dev_info(smu->adev->dev, "FeaturesToRun[0] = 0x%08x\n", pptable->FeaturesToRun[0]);
	dev_info(smu->adev->dev, "FeaturesToRun[1] = 0x%08x\n", pptable->FeaturesToRun[1]);

	for (i = 0; i < PPT_THROTTLER_COUNT; i++) {
		dev_info(smu->adev->dev, "SocketPowerLimitAc[%d] = 0x%x\n", i, pptable->SocketPowerLimitAc[i]);
		dev_info(smu->adev->dev, "SocketPowerLimitAcTau[%d] = 0x%x\n", i, pptable->SocketPowerLimitAcTau[i]);
		dev_info(smu->adev->dev, "SocketPowerLimitDc[%d] = 0x%x\n", i, pptable->SocketPowerLimitDc[i]);
		dev_info(smu->adev->dev, "SocketPowerLimitDcTau[%d] = 0x%x\n", i, pptable->SocketPowerLimitDcTau[i]);
	}

	for (i = 0; i < TDC_THROTTLER_COUNT; i++) {
		dev_info(smu->adev->dev, "TdcLimit[%d] = 0x%x\n", i, pptable->TdcLimit[i]);
		dev_info(smu->adev->dev, "TdcLimitTau[%d] = 0x%x\n", i, pptable->TdcLimitTau[i]);
	}

	for (i = 0; i < TEMP_COUNT; i++) {
		dev_info(smu->adev->dev, "TemperatureLimit[%d] = 0x%x\n", i, pptable->TemperatureLimit[i]);
	}

	dev_info(smu->adev->dev, "FitLimit = 0x%x\n", pptable->FitLimit);
	dev_info(smu->adev->dev, "TotalPowerConfig = 0x%x\n", pptable->TotalPowerConfig);
	dev_info(smu->adev->dev, "TotalPowerPadding[0] = 0x%x\n", pptable->TotalPowerPadding[0]);
	dev_info(smu->adev->dev, "TotalPowerPadding[1] = 0x%x\n", pptable->TotalPowerPadding[1]);
	dev_info(smu->adev->dev, "TotalPowerPadding[2] = 0x%x\n", pptable->TotalPowerPadding[2]);

	dev_info(smu->adev->dev, "ApccPlusResidencyLimit = 0x%x\n", pptable->ApccPlusResidencyLimit);
	for (i = 0; i < NUM_SMNCLK_DPM_LEVELS; i++) {
		dev_info(smu->adev->dev, "SmnclkDpmFreq[%d] = 0x%x\n", i, pptable->SmnclkDpmFreq[i]);
		dev_info(smu->adev->dev, "SmnclkDpmVoltage[%d] = 0x%x\n", i, pptable->SmnclkDpmVoltage[i]);
	}
	dev_info(smu->adev->dev, "ThrottlerControlMask = 0x%x\n", pptable->ThrottlerControlMask);

	dev_info(smu->adev->dev, "FwDStateMask = 0x%x\n", pptable->FwDStateMask);

	dev_info(smu->adev->dev, "UlvVoltageOffsetSoc = 0x%x\n", pptable->UlvVoltageOffsetSoc);
	dev_info(smu->adev->dev, "UlvVoltageOffsetGfx = 0x%x\n", pptable->UlvVoltageOffsetGfx);
	dev_info(smu->adev->dev, "MinVoltageUlvGfx = 0x%x\n", pptable->MinVoltageUlvGfx);
	dev_info(smu->adev->dev, "MinVoltageUlvSoc = 0x%x\n", pptable->MinVoltageUlvSoc);

	dev_info(smu->adev->dev, "SocLIVmin = 0x%x\n", pptable->SocLIVmin);
	dev_info(smu->adev->dev, "PaddingLIVmin = 0x%x\n", pptable->PaddingLIVmin);

	dev_info(smu->adev->dev, "GceaLinkMgrIdleThreshold = 0x%x\n", pptable->GceaLinkMgrIdleThreshold);
	dev_info(smu->adev->dev, "paddingRlcUlvParams[0] = 0x%x\n", pptable->paddingRlcUlvParams[0]);
	dev_info(smu->adev->dev, "paddingRlcUlvParams[1] = 0x%x\n", pptable->paddingRlcUlvParams[1]);
	dev_info(smu->adev->dev, "paddingRlcUlvParams[2] = 0x%x\n", pptable->paddingRlcUlvParams[2]);

	dev_info(smu->adev->dev, "MinVoltageGfx = 0x%x\n", pptable->MinVoltageGfx);
	dev_info(smu->adev->dev, "MinVoltageSoc = 0x%x\n", pptable->MinVoltageSoc);
	dev_info(smu->adev->dev, "MaxVoltageGfx = 0x%x\n", pptable->MaxVoltageGfx);
	dev_info(smu->adev->dev, "MaxVoltageSoc = 0x%x\n", pptable->MaxVoltageSoc);

	dev_info(smu->adev->dev, "LoadLineResistanceGfx = 0x%x\n", pptable->LoadLineResistanceGfx);
	dev_info(smu->adev->dev, "LoadLineResistanceSoc = 0x%x\n", pptable->LoadLineResistanceSoc);

	dev_info(smu->adev->dev, "VDDGFX_TVmin = 0x%x\n", pptable->VDDGFX_TVmin);
	dev_info(smu->adev->dev, "VDDSOC_TVmin = 0x%x\n", pptable->VDDSOC_TVmin);
	dev_info(smu->adev->dev, "VDDGFX_Vmin_HiTemp = 0x%x\n", pptable->VDDGFX_Vmin_HiTemp);
	dev_info(smu->adev->dev, "VDDGFX_Vmin_LoTemp = 0x%x\n", pptable->VDDGFX_Vmin_LoTemp);
	dev_info(smu->adev->dev, "VDDSOC_Vmin_HiTemp = 0x%x\n", pptable->VDDSOC_Vmin_HiTemp);
	dev_info(smu->adev->dev, "VDDSOC_Vmin_LoTemp = 0x%x\n", pptable->VDDSOC_Vmin_LoTemp);
	dev_info(smu->adev->dev, "VDDGFX_TVminHystersis = 0x%x\n", pptable->VDDGFX_TVminHystersis);
	dev_info(smu->adev->dev, "VDDSOC_TVminHystersis = 0x%x\n", pptable->VDDSOC_TVminHystersis);

	dev_info(smu->adev->dev, "[PPCLK_GFXCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_GFXCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_GFXCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_GFXCLK].Padding,
			pptable->DpmDescriptor[PPCLK_GFXCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_GFXCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SsFmin,
			pptable->DpmDescriptor[PPCLK_GFXCLK].Padding16);

	dev_info(smu->adev->dev, "[PPCLK_SOCCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_SOCCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_SOCCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_SOCCLK].Padding,
			pptable->DpmDescriptor[PPCLK_SOCCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_SOCCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SsFmin,
			pptable->DpmDescriptor[PPCLK_SOCCLK].Padding16);

	dev_info(smu->adev->dev, "[PPCLK_UCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_UCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_UCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_UCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_UCLK].Padding,
			pptable->DpmDescriptor[PPCLK_UCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_UCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_UCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_UCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_UCLK].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_UCLK].SsFmin,
			pptable->DpmDescriptor[PPCLK_UCLK].Padding16);

	dev_info(smu->adev->dev, "[PPCLK_FCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_FCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_FCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_FCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_FCLK].Padding,
			pptable->DpmDescriptor[PPCLK_FCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_FCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_FCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_FCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_FCLK].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_FCLK].SsFmin,
			pptable->DpmDescriptor[PPCLK_FCLK].Padding16);

	dev_info(smu->adev->dev, "[PPCLK_DCLK_0]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_DCLK_0].VoltageMode,
			pptable->DpmDescriptor[PPCLK_DCLK_0].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_DCLK_0].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_DCLK_0].Padding,
			pptable->DpmDescriptor[PPCLK_DCLK_0].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_DCLK_0].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_DCLK_0].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_DCLK_0].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_DCLK_0].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_DCLK_0].SsFmin,
			pptable->DpmDescriptor[PPCLK_DCLK_0].Padding16);

	dev_info(smu->adev->dev, "[PPCLK_VCLK_0]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_VCLK_0].VoltageMode,
			pptable->DpmDescriptor[PPCLK_VCLK_0].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_VCLK_0].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_VCLK_0].Padding,
			pptable->DpmDescriptor[PPCLK_VCLK_0].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_VCLK_0].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_VCLK_0].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_VCLK_0].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_VCLK_0].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_VCLK_0].SsFmin,
			pptable->DpmDescriptor[PPCLK_VCLK_0].Padding16);

	dev_info(smu->adev->dev, "[PPCLK_DCLK_1]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_DCLK_1].VoltageMode,
			pptable->DpmDescriptor[PPCLK_DCLK_1].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_DCLK_1].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_DCLK_1].Padding,
			pptable->DpmDescriptor[PPCLK_DCLK_1].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_DCLK_1].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_DCLK_1].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_DCLK_1].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_DCLK_1].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_DCLK_1].SsFmin,
			pptable->DpmDescriptor[PPCLK_DCLK_1].Padding16);

	dev_info(smu->adev->dev, "[PPCLK_VCLK_1]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_VCLK_1].VoltageMode,
			pptable->DpmDescriptor[PPCLK_VCLK_1].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_VCLK_1].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_VCLK_1].Padding,
			pptable->DpmDescriptor[PPCLK_VCLK_1].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_VCLK_1].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_VCLK_1].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_VCLK_1].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_VCLK_1].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_VCLK_1].SsFmin,
			pptable->DpmDescriptor[PPCLK_VCLK_1].Padding16);

	dev_info(smu->adev->dev, "FreqTableGfx\n");
	for (i = 0; i < NUM_GFXCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%02d] = 0x%x\n", i, pptable->FreqTableGfx[i]);

	dev_info(smu->adev->dev, "FreqTableVclk\n");
	for (i = 0; i < NUM_VCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%02d] = 0x%x\n", i, pptable->FreqTableVclk[i]);

	dev_info(smu->adev->dev, "FreqTableDclk\n");
	for (i = 0; i < NUM_DCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%02d] = 0x%x\n", i, pptable->FreqTableDclk[i]);

	dev_info(smu->adev->dev, "FreqTableSocclk\n");
	for (i = 0; i < NUM_SOCCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%02d] = 0x%x\n", i, pptable->FreqTableSocclk[i]);

	dev_info(smu->adev->dev, "FreqTableUclk\n");
	for (i = 0; i < NUM_UCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%02d] = 0x%x\n", i, pptable->FreqTableUclk[i]);

	dev_info(smu->adev->dev, "FreqTableFclk\n");
	for (i = 0; i < NUM_FCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%02d] = 0x%x\n", i, pptable->FreqTableFclk[i]);

	dev_info(smu->adev->dev, "DcModeMaxFreq\n");
	dev_info(smu->adev->dev, "  .PPCLK_GFXCLK = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_GFXCLK]);
	dev_info(smu->adev->dev, "  .PPCLK_SOCCLK = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_SOCCLK]);
	dev_info(smu->adev->dev, "  .PPCLK_UCLK   = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_UCLK]);
	dev_info(smu->adev->dev, "  .PPCLK_FCLK   = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_FCLK]);
	dev_info(smu->adev->dev, "  .PPCLK_DCLK_0 = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_DCLK_0]);
	dev_info(smu->adev->dev, "  .PPCLK_VCLK_0 = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_VCLK_0]);
	dev_info(smu->adev->dev, "  .PPCLK_DCLK_1 = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_DCLK_1]);
	dev_info(smu->adev->dev, "  .PPCLK_VCLK_1 = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_VCLK_1]);

	dev_info(smu->adev->dev, "FreqTableUclkDiv\n");
	for (i = 0; i < NUM_UCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->FreqTableUclkDiv[i]);

	dev_info(smu->adev->dev, "FclkBoostFreq = 0x%x\n", pptable->FclkBoostFreq);
	dev_info(smu->adev->dev, "FclkParamPadding = 0x%x\n", pptable->FclkParamPadding);

	dev_info(smu->adev->dev, "Mp0clkFreq\n");
	for (i = 0; i < NUM_MP0CLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->Mp0clkFreq[i]);

	dev_info(smu->adev->dev, "Mp0DpmVoltage\n");
	for (i = 0; i < NUM_MP0CLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->Mp0DpmVoltage[i]);

	dev_info(smu->adev->dev, "MemVddciVoltage\n");
	for (i = 0; i < NUM_UCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->MemVddciVoltage[i]);

	dev_info(smu->adev->dev, "MemMvddVoltage\n");
	for (i = 0; i < NUM_UCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->MemMvddVoltage[i]);

	dev_info(smu->adev->dev, "GfxclkFgfxoffEntry = 0x%x\n", pptable->GfxclkFgfxoffEntry);
	dev_info(smu->adev->dev, "GfxclkFinit = 0x%x\n", pptable->GfxclkFinit);
	dev_info(smu->adev->dev, "GfxclkFidle = 0x%x\n", pptable->GfxclkFidle);
	dev_info(smu->adev->dev, "GfxclkSource = 0x%x\n", pptable->GfxclkSource);
	dev_info(smu->adev->dev, "GfxclkPadding = 0x%x\n", pptable->GfxclkPadding);

	dev_info(smu->adev->dev, "GfxGpoSubFeatureMask = 0x%x\n", pptable->GfxGpoSubFeatureMask);

	dev_info(smu->adev->dev, "GfxGpoEnabledWorkPolicyMask = 0x%x\n", pptable->GfxGpoEnabledWorkPolicyMask);
	dev_info(smu->adev->dev, "GfxGpoDisabledWorkPolicyMask = 0x%x\n", pptable->GfxGpoDisabledWorkPolicyMask);
	dev_info(smu->adev->dev, "GfxGpoPadding[0] = 0x%x\n", pptable->GfxGpoPadding[0]);
	dev_info(smu->adev->dev, "GfxGpoVotingAllow = 0x%x\n", pptable->GfxGpoVotingAllow);
	dev_info(smu->adev->dev, "GfxGpoPadding32[0] = 0x%x\n", pptable->GfxGpoPadding32[0]);
	dev_info(smu->adev->dev, "GfxGpoPadding32[1] = 0x%x\n", pptable->GfxGpoPadding32[1]);
	dev_info(smu->adev->dev, "GfxGpoPadding32[2] = 0x%x\n", pptable->GfxGpoPadding32[2]);
	dev_info(smu->adev->dev, "GfxGpoPadding32[3] = 0x%x\n", pptable->GfxGpoPadding32[3]);
	dev_info(smu->adev->dev, "GfxDcsFopt = 0x%x\n", pptable->GfxDcsFopt);
	dev_info(smu->adev->dev, "GfxDcsFclkFopt = 0x%x\n", pptable->GfxDcsFclkFopt);
	dev_info(smu->adev->dev, "GfxDcsUclkFopt = 0x%x\n", pptable->GfxDcsUclkFopt);

	dev_info(smu->adev->dev, "DcsGfxOffVoltage = 0x%x\n", pptable->DcsGfxOffVoltage);
	dev_info(smu->adev->dev, "DcsMinGfxOffTime = 0x%x\n", pptable->DcsMinGfxOffTime);
	dev_info(smu->adev->dev, "DcsMaxGfxOffTime = 0x%x\n", pptable->DcsMaxGfxOffTime);
	dev_info(smu->adev->dev, "DcsMinCreditAccum = 0x%x\n", pptable->DcsMinCreditAccum);
	dev_info(smu->adev->dev, "DcsExitHysteresis = 0x%x\n", pptable->DcsExitHysteresis);
	dev_info(smu->adev->dev, "DcsTimeout = 0x%x\n", pptable->DcsTimeout);

	dev_info(smu->adev->dev, "DcsParamPadding[0] = 0x%x\n", pptable->DcsParamPadding[0]);
	dev_info(smu->adev->dev, "DcsParamPadding[1] = 0x%x\n", pptable->DcsParamPadding[1]);
	dev_info(smu->adev->dev, "DcsParamPadding[2] = 0x%x\n", pptable->DcsParamPadding[2]);
	dev_info(smu->adev->dev, "DcsParamPadding[3] = 0x%x\n", pptable->DcsParamPadding[3]);
	dev_info(smu->adev->dev, "DcsParamPadding[4] = 0x%x\n", pptable->DcsParamPadding[4]);

	dev_info(smu->adev->dev, "FlopsPerByteTable\n");
	for (i = 0; i < RLC_PACE_TABLE_NUM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->FlopsPerByteTable[i]);

	dev_info(smu->adev->dev, "LowestUclkReservedForUlv = 0x%x\n", pptable->LowestUclkReservedForUlv);
	dev_info(smu->adev->dev, "vddingMem[0] = 0x%x\n", pptable->PaddingMem[0]);
	dev_info(smu->adev->dev, "vddingMem[1] = 0x%x\n", pptable->PaddingMem[1]);
	dev_info(smu->adev->dev, "vddingMem[2] = 0x%x\n", pptable->PaddingMem[2]);

	dev_info(smu->adev->dev, "UclkDpmPstates\n");
	for (i = 0; i < NUM_UCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->UclkDpmPstates[i]);

	dev_info(smu->adev->dev, "UclkDpmSrcFreqRange\n");
	dev_info(smu->adev->dev, "  .Fmin = 0x%x\n",
		pptable->UclkDpmSrcFreqRange.Fmin);
	dev_info(smu->adev->dev, "  .Fmax = 0x%x\n",
		pptable->UclkDpmSrcFreqRange.Fmax);
	dev_info(smu->adev->dev, "UclkDpmTargFreqRange\n");
	dev_info(smu->adev->dev, "  .Fmin = 0x%x\n",
		pptable->UclkDpmTargFreqRange.Fmin);
	dev_info(smu->adev->dev, "  .Fmax = 0x%x\n",
		pptable->UclkDpmTargFreqRange.Fmax);
	dev_info(smu->adev->dev, "UclkDpmMidstepFreq = 0x%x\n", pptable->UclkDpmMidstepFreq);
	dev_info(smu->adev->dev, "UclkMidstepPadding = 0x%x\n", pptable->UclkMidstepPadding);

	dev_info(smu->adev->dev, "PcieGenSpeed\n");
	for (i = 0; i < NUM_LINK_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->PcieGenSpeed[i]);

	dev_info(smu->adev->dev, "PcieLaneCount\n");
	for (i = 0; i < NUM_LINK_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->PcieLaneCount[i]);

	dev_info(smu->adev->dev, "LclkFreq\n");
	for (i = 0; i < NUM_LINK_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->LclkFreq[i]);

	dev_info(smu->adev->dev, "FanStopTemp = 0x%x\n", pptable->FanStopTemp);
	dev_info(smu->adev->dev, "FanStartTemp = 0x%x\n", pptable->FanStartTemp);

	dev_info(smu->adev->dev, "FanGain\n");
	for (i = 0; i < TEMP_COUNT; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->FanGain[i]);

	dev_info(smu->adev->dev, "FanPwmMin = 0x%x\n", pptable->FanPwmMin);
	dev_info(smu->adev->dev, "FanAcousticLimitRpm = 0x%x\n", pptable->FanAcousticLimitRpm);
	dev_info(smu->adev->dev, "FanThrottlingRpm = 0x%x\n", pptable->FanThrottlingRpm);
	dev_info(smu->adev->dev, "FanMaximumRpm = 0x%x\n", pptable->FanMaximumRpm);
	dev_info(smu->adev->dev, "MGpuFanBoostLimitRpm = 0x%x\n", pptable->MGpuFanBoostLimitRpm);
	dev_info(smu->adev->dev, "FanTargetTemperature = 0x%x\n", pptable->FanTargetTemperature);
	dev_info(smu->adev->dev, "FanTargetGfxclk = 0x%x\n", pptable->FanTargetGfxclk);
	dev_info(smu->adev->dev, "FanPadding16 = 0x%x\n", pptable->FanPadding16);
	dev_info(smu->adev->dev, "FanTempInputSelect = 0x%x\n", pptable->FanTempInputSelect);
	dev_info(smu->adev->dev, "FanPadding = 0x%x\n", pptable->FanPadding);
	dev_info(smu->adev->dev, "FanZeroRpmEnable = 0x%x\n", pptable->FanZeroRpmEnable);
	dev_info(smu->adev->dev, "FanTachEdgePerRev = 0x%x\n", pptable->FanTachEdgePerRev);

	dev_info(smu->adev->dev, "FuzzyFan_ErrorSetDelta = 0x%x\n", pptable->FuzzyFan_ErrorSetDelta);
	dev_info(smu->adev->dev, "FuzzyFan_ErrorRateSetDelta = 0x%x\n", pptable->FuzzyFan_ErrorRateSetDelta);
	dev_info(smu->adev->dev, "FuzzyFan_PwmSetDelta = 0x%x\n", pptable->FuzzyFan_PwmSetDelta);
	dev_info(smu->adev->dev, "FuzzyFan_Reserved = 0x%x\n", pptable->FuzzyFan_Reserved);

	dev_info(smu->adev->dev, "OverrideAvfsGb[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->OverrideAvfsGb[AVFS_VOLTAGE_GFX]);
	dev_info(smu->adev->dev, "OverrideAvfsGb[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->OverrideAvfsGb[AVFS_VOLTAGE_SOC]);
	dev_info(smu->adev->dev, "dBtcGbGfxDfllModelSelect = 0x%x\n", pptable->dBtcGbGfxDfllModelSelect);
	dev_info(smu->adev->dev, "Padding8_Avfs = 0x%x\n", pptable->Padding8_Avfs);

	dev_info(smu->adev->dev, "qAvfsGb[AVFS_VOLTAGE_GFX]{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->qAvfsGb[AVFS_VOLTAGE_GFX].a,
			pptable->qAvfsGb[AVFS_VOLTAGE_GFX].b,
			pptable->qAvfsGb[AVFS_VOLTAGE_GFX].c);
	dev_info(smu->adev->dev, "qAvfsGb[AVFS_VOLTAGE_SOC]{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->qAvfsGb[AVFS_VOLTAGE_SOC].a,
			pptable->qAvfsGb[AVFS_VOLTAGE_SOC].b,
			pptable->qAvfsGb[AVFS_VOLTAGE_SOC].c);
	dev_info(smu->adev->dev, "dBtcGbGfxPll{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->dBtcGbGfxPll.a,
			pptable->dBtcGbGfxPll.b,
			pptable->dBtcGbGfxPll.c);
	dev_info(smu->adev->dev, "dBtcGbGfxAfll{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->dBtcGbGfxDfll.a,
			pptable->dBtcGbGfxDfll.b,
			pptable->dBtcGbGfxDfll.c);
	dev_info(smu->adev->dev, "dBtcGbSoc{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->dBtcGbSoc.a,
			pptable->dBtcGbSoc.b,
			pptable->dBtcGbSoc.c);
	dev_info(smu->adev->dev, "qAgingGb[AVFS_VOLTAGE_GFX]{m = 0x%x b = 0x%x}\n",
			pptable->qAgingGb[AVFS_VOLTAGE_GFX].m,
			pptable->qAgingGb[AVFS_VOLTAGE_GFX].b);
	dev_info(smu->adev->dev, "qAgingGb[AVFS_VOLTAGE_SOC]{m = 0x%x b = 0x%x}\n",
			pptable->qAgingGb[AVFS_VOLTAGE_SOC].m,
			pptable->qAgingGb[AVFS_VOLTAGE_SOC].b);

	dev_info(smu->adev->dev, "PiecewiseLinearDroopIntGfxDfll\n");
	for (i = 0; i < NUM_PIECE_WISE_LINEAR_DROOP_MODEL_VF_POINTS; i++) {
		dev_info(smu->adev->dev, "		Fset[%d] = 0x%x\n",
			i, pptable->PiecewiseLinearDroopIntGfxDfll.Fset[i]);
		dev_info(smu->adev->dev, "		Vdroop[%d] = 0x%x\n",
			i, pptable->PiecewiseLinearDroopIntGfxDfll.Vdroop[i]);
	}

	dev_info(smu->adev->dev, "qStaticVoltageOffset[AVFS_VOLTAGE_GFX]{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_GFX].a,
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_GFX].b,
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_GFX].c);
	dev_info(smu->adev->dev, "qStaticVoltageOffset[AVFS_VOLTAGE_SOC]{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_SOC].a,
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_SOC].b,
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_SOC].c);

	dev_info(smu->adev->dev, "DcTol[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcTol[AVFS_VOLTAGE_GFX]);
	dev_info(smu->adev->dev, "DcTol[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcTol[AVFS_VOLTAGE_SOC]);

	dev_info(smu->adev->dev, "DcBtcEnabled[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcBtcEnabled[AVFS_VOLTAGE_GFX]);
	dev_info(smu->adev->dev, "DcBtcEnabled[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcBtcEnabled[AVFS_VOLTAGE_SOC]);
	dev_info(smu->adev->dev, "Padding8_GfxBtc[0] = 0x%x\n", pptable->Padding8_GfxBtc[0]);
	dev_info(smu->adev->dev, "Padding8_GfxBtc[1] = 0x%x\n", pptable->Padding8_GfxBtc[1]);

	dev_info(smu->adev->dev, "DcBtcMin[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcBtcMin[AVFS_VOLTAGE_GFX]);
	dev_info(smu->adev->dev, "DcBtcMin[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcBtcMin[AVFS_VOLTAGE_SOC]);
	dev_info(smu->adev->dev, "DcBtcMax[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcBtcMax[AVFS_VOLTAGE_GFX]);
	dev_info(smu->adev->dev, "DcBtcMax[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcBtcMax[AVFS_VOLTAGE_SOC]);

	dev_info(smu->adev->dev, "DcBtcGb[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcBtcGb[AVFS_VOLTAGE_GFX]);
	dev_info(smu->adev->dev, "DcBtcGb[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcBtcGb[AVFS_VOLTAGE_SOC]);

	dev_info(smu->adev->dev, "XgmiDpmPstates\n");
	for (i = 0; i < NUM_XGMI_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->XgmiDpmPstates[i]);
	dev_info(smu->adev->dev, "XgmiDpmSpare[0] = 0x%02x\n", pptable->XgmiDpmSpare[0]);
	dev_info(smu->adev->dev, "XgmiDpmSpare[1] = 0x%02x\n", pptable->XgmiDpmSpare[1]);

	dev_info(smu->adev->dev, "DebugOverrides = 0x%x\n", pptable->DebugOverrides);
	dev_info(smu->adev->dev, "ReservedEquation0{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->ReservedEquation0.a,
			pptable->ReservedEquation0.b,
			pptable->ReservedEquation0.c);
	dev_info(smu->adev->dev, "ReservedEquation1{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->ReservedEquation1.a,
			pptable->ReservedEquation1.b,
			pptable->ReservedEquation1.c);
	dev_info(smu->adev->dev, "ReservedEquation2{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->ReservedEquation2.a,
			pptable->ReservedEquation2.b,
			pptable->ReservedEquation2.c);
	dev_info(smu->adev->dev, "ReservedEquation3{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->ReservedEquation3.a,
			pptable->ReservedEquation3.b,
			pptable->ReservedEquation3.c);

	dev_info(smu->adev->dev, "SkuReserved[0] = 0x%x\n", pptable->SkuReserved[0]);
	dev_info(smu->adev->dev, "SkuReserved[1] = 0x%x\n", pptable->SkuReserved[1]);
	dev_info(smu->adev->dev, "SkuReserved[2] = 0x%x\n", pptable->SkuReserved[2]);
	dev_info(smu->adev->dev, "SkuReserved[3] = 0x%x\n", pptable->SkuReserved[3]);
	dev_info(smu->adev->dev, "SkuReserved[4] = 0x%x\n", pptable->SkuReserved[4]);
	dev_info(smu->adev->dev, "SkuReserved[5] = 0x%x\n", pptable->SkuReserved[5]);
	dev_info(smu->adev->dev, "SkuReserved[6] = 0x%x\n", pptable->SkuReserved[6]);
	dev_info(smu->adev->dev, "SkuReserved[7] = 0x%x\n", pptable->SkuReserved[7]);

	dev_info(smu->adev->dev, "GamingClk[0] = 0x%x\n", pptable->GamingClk[0]);
	dev_info(smu->adev->dev, "GamingClk[1] = 0x%x\n", pptable->GamingClk[1]);
	dev_info(smu->adev->dev, "GamingClk[2] = 0x%x\n", pptable->GamingClk[2]);
	dev_info(smu->adev->dev, "GamingClk[3] = 0x%x\n", pptable->GamingClk[3]);
	dev_info(smu->adev->dev, "GamingClk[4] = 0x%x\n", pptable->GamingClk[4]);
	dev_info(smu->adev->dev, "GamingClk[5] = 0x%x\n", pptable->GamingClk[5]);

	for (i = 0; i < NUM_I2C_CONTROLLERS; i++) {
		dev_info(smu->adev->dev, "I2cControllers[%d]:\n", i);
		dev_info(smu->adev->dev, "                   .Enabled = 0x%x\n",
				pptable->I2cControllers[i].Enabled);
		dev_info(smu->adev->dev, "                   .Speed = 0x%x\n",
				pptable->I2cControllers[i].Speed);
		dev_info(smu->adev->dev, "                   .SlaveAddress = 0x%x\n",
				pptable->I2cControllers[i].SlaveAddress);
		dev_info(smu->adev->dev, "                   .ControllerPort = 0x%x\n",
				pptable->I2cControllers[i].ControllerPort);
		dev_info(smu->adev->dev, "                   .ControllerName = 0x%x\n",
				pptable->I2cControllers[i].ControllerName);
		dev_info(smu->adev->dev, "                   .ThermalThrottler = 0x%x\n",
				pptable->I2cControllers[i].ThermalThrotter);
		dev_info(smu->adev->dev, "                   .I2cProtocol = 0x%x\n",
				pptable->I2cControllers[i].I2cProtocol);
		dev_info(smu->adev->dev, "                   .PaddingConfig = 0x%x\n",
				pptable->I2cControllers[i].PaddingConfig);
	}

	dev_info(smu->adev->dev, "GpioScl = 0x%x\n", pptable->GpioScl);
	dev_info(smu->adev->dev, "GpioSda = 0x%x\n", pptable->GpioSda);
	dev_info(smu->adev->dev, "FchUsbPdSlaveAddr = 0x%x\n", pptable->FchUsbPdSlaveAddr);
	dev_info(smu->adev->dev, "I2cSpare[0] = 0x%x\n", pptable->I2cSpare[0]);

	dev_info(smu->adev->dev, "Board Parameters:\n");
	dev_info(smu->adev->dev, "VddGfxVrMapping = 0x%x\n", pptable->VddGfxVrMapping);
	dev_info(smu->adev->dev, "VddSocVrMapping = 0x%x\n", pptable->VddSocVrMapping);
	dev_info(smu->adev->dev, "VddMem0VrMapping = 0x%x\n", pptable->VddMem0VrMapping);
	dev_info(smu->adev->dev, "VddMem1VrMapping = 0x%x\n", pptable->VddMem1VrMapping);
	dev_info(smu->adev->dev, "GfxUlvPhaseSheddingMask = 0x%x\n", pptable->GfxUlvPhaseSheddingMask);
	dev_info(smu->adev->dev, "SocUlvPhaseSheddingMask = 0x%x\n", pptable->SocUlvPhaseSheddingMask);
	dev_info(smu->adev->dev, "VddciUlvPhaseSheddingMask = 0x%x\n", pptable->VddciUlvPhaseSheddingMask);
	dev_info(smu->adev->dev, "MvddUlvPhaseSheddingMask = 0x%x\n", pptable->MvddUlvPhaseSheddingMask);

	dev_info(smu->adev->dev, "GfxMaxCurrent = 0x%x\n", pptable->GfxMaxCurrent);
	dev_info(smu->adev->dev, "GfxOffset = 0x%x\n", pptable->GfxOffset);
	dev_info(smu->adev->dev, "Padding_TelemetryGfx = 0x%x\n", pptable->Padding_TelemetryGfx);

	dev_info(smu->adev->dev, "SocMaxCurrent = 0x%x\n", pptable->SocMaxCurrent);
	dev_info(smu->adev->dev, "SocOffset = 0x%x\n", pptable->SocOffset);
	dev_info(smu->adev->dev, "Padding_TelemetrySoc = 0x%x\n", pptable->Padding_TelemetrySoc);

	dev_info(smu->adev->dev, "Mem0MaxCurrent = 0x%x\n", pptable->Mem0MaxCurrent);
	dev_info(smu->adev->dev, "Mem0Offset = 0x%x\n", pptable->Mem0Offset);
	dev_info(smu->adev->dev, "Padding_TelemetryMem0 = 0x%x\n", pptable->Padding_TelemetryMem0);

	dev_info(smu->adev->dev, "Mem1MaxCurrent = 0x%x\n", pptable->Mem1MaxCurrent);
	dev_info(smu->adev->dev, "Mem1Offset = 0x%x\n", pptable->Mem1Offset);
	dev_info(smu->adev->dev, "Padding_TelemetryMem1 = 0x%x\n", pptable->Padding_TelemetryMem1);

	dev_info(smu->adev->dev, "MvddRatio = 0x%x\n", pptable->MvddRatio);

	dev_info(smu->adev->dev, "AcDcGpio = 0x%x\n", pptable->AcDcGpio);
	dev_info(smu->adev->dev, "AcDcPolarity = 0x%x\n", pptable->AcDcPolarity);
	dev_info(smu->adev->dev, "VR0HotGpio = 0x%x\n", pptable->VR0HotGpio);
	dev_info(smu->adev->dev, "VR0HotPolarity = 0x%x\n", pptable->VR0HotPolarity);
	dev_info(smu->adev->dev, "VR1HotGpio = 0x%x\n", pptable->VR1HotGpio);
	dev_info(smu->adev->dev, "VR1HotPolarity = 0x%x\n", pptable->VR1HotPolarity);
	dev_info(smu->adev->dev, "GthrGpio = 0x%x\n", pptable->GthrGpio);
	dev_info(smu->adev->dev, "GthrPolarity = 0x%x\n", pptable->GthrPolarity);
	dev_info(smu->adev->dev, "LedPin0 = 0x%x\n", pptable->LedPin0);
	dev_info(smu->adev->dev, "LedPin1 = 0x%x\n", pptable->LedPin1);
	dev_info(smu->adev->dev, "LedPin2 = 0x%x\n", pptable->LedPin2);
	dev_info(smu->adev->dev, "LedEnableMask = 0x%x\n", pptable->LedEnableMask);
	dev_info(smu->adev->dev, "LedPcie = 0x%x\n", pptable->LedPcie);
	dev_info(smu->adev->dev, "LedError = 0x%x\n", pptable->LedError);
	dev_info(smu->adev->dev, "LedSpare1[0] = 0x%x\n", pptable->LedSpare1[0]);
	dev_info(smu->adev->dev, "LedSpare1[1] = 0x%x\n", pptable->LedSpare1[1]);

	dev_info(smu->adev->dev, "PllGfxclkSpreadEnabled = 0x%x\n", pptable->PllGfxclkSpreadEnabled);
	dev_info(smu->adev->dev, "PllGfxclkSpreadPercent = 0x%x\n", pptable->PllGfxclkSpreadPercent);
	dev_info(smu->adev->dev, "PllGfxclkSpreadFreq = 0x%x\n",    pptable->PllGfxclkSpreadFreq);

	dev_info(smu->adev->dev, "DfllGfxclkSpreadEnabled = 0x%x\n", pptable->DfllGfxclkSpreadEnabled);
	dev_info(smu->adev->dev, "DfllGfxclkSpreadPercent = 0x%x\n", pptable->DfllGfxclkSpreadPercent);
	dev_info(smu->adev->dev, "DfllGfxclkSpreadFreq = 0x%x\n",    pptable->DfllGfxclkSpreadFreq);

	dev_info(smu->adev->dev, "UclkSpreadPadding = 0x%x\n", pptable->UclkSpreadPadding);
	dev_info(smu->adev->dev, "UclkSpreadFreq = 0x%x\n", pptable->UclkSpreadFreq);

	dev_info(smu->adev->dev, "FclkSpreadEnabled = 0x%x\n", pptable->FclkSpreadEnabled);
	dev_info(smu->adev->dev, "FclkSpreadPercent = 0x%x\n", pptable->FclkSpreadPercent);
	dev_info(smu->adev->dev, "FclkSpreadFreq = 0x%x\n", pptable->FclkSpreadFreq);

	dev_info(smu->adev->dev, "MemoryChannelEnabled = 0x%x\n", pptable->MemoryChannelEnabled);
	dev_info(smu->adev->dev, "DramBitWidth = 0x%x\n", pptable->DramBitWidth);
	dev_info(smu->adev->dev, "PaddingMem1[0] = 0x%x\n", pptable->PaddingMem1[0]);
	dev_info(smu->adev->dev, "PaddingMem1[1] = 0x%x\n", pptable->PaddingMem1[1]);
	dev_info(smu->adev->dev, "PaddingMem1[2] = 0x%x\n", pptable->PaddingMem1[2]);

	dev_info(smu->adev->dev, "TotalBoardPower = 0x%x\n", pptable->TotalBoardPower);
	dev_info(smu->adev->dev, "BoardPowerPadding = 0x%x\n", pptable->BoardPowerPadding);

	dev_info(smu->adev->dev, "XgmiLinkSpeed\n");
	for (i = 0; i < NUM_XGMI_PSTATE_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->XgmiLinkSpeed[i]);
	dev_info(smu->adev->dev, "XgmiLinkWidth\n");
	for (i = 0; i < NUM_XGMI_PSTATE_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->XgmiLinkWidth[i]);
	dev_info(smu->adev->dev, "XgmiFclkFreq\n");
	for (i = 0; i < NUM_XGMI_PSTATE_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->XgmiFclkFreq[i]);
	dev_info(smu->adev->dev, "XgmiSocVoltage\n");
	for (i = 0; i < NUM_XGMI_PSTATE_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = 0x%x\n", i, pptable->XgmiSocVoltage[i]);

	dev_info(smu->adev->dev, "HsrEnabled = 0x%x\n", pptable->HsrEnabled);
	dev_info(smu->adev->dev, "VddqOffEnabled = 0x%x\n", pptable->VddqOffEnabled);
	dev_info(smu->adev->dev, "PaddingUmcFlags[0] = 0x%x\n", pptable->PaddingUmcFlags[0]);
	dev_info(smu->adev->dev, "PaddingUmcFlags[1] = 0x%x\n", pptable->PaddingUmcFlags[1]);

	dev_info(smu->adev->dev, "BoardReserved[0] = 0x%x\n", pptable->BoardReserved[0]);
	dev_info(smu->adev->dev, "BoardReserved[1] = 0x%x\n", pptable->BoardReserved[1]);
	dev_info(smu->adev->dev, "BoardReserved[2] = 0x%x\n", pptable->BoardReserved[2]);
	dev_info(smu->adev->dev, "BoardReserved[3] = 0x%x\n", pptable->BoardReserved[3]);
	dev_info(smu->adev->dev, "BoardReserved[4] = 0x%x\n", pptable->BoardReserved[4]);
	dev_info(smu->adev->dev, "BoardReserved[5] = 0x%x\n", pptable->BoardReserved[5]);
	dev_info(smu->adev->dev, "BoardReserved[6] = 0x%x\n", pptable->BoardReserved[6]);
	dev_info(smu->adev->dev, "BoardReserved[7] = 0x%x\n", pptable->BoardReserved[7]);
	dev_info(smu->adev->dev, "BoardReserved[8] = 0x%x\n", pptable->BoardReserved[8]);
	dev_info(smu->adev->dev, "BoardReserved[9] = 0x%x\n", pptable->BoardReserved[9]);
	dev_info(smu->adev->dev, "BoardReserved[10] = 0x%x\n", pptable->BoardReserved[10]);

	dev_info(smu->adev->dev, "MmHubPadding[0] = 0x%x\n", pptable->MmHubPadding[0]);
	dev_info(smu->adev->dev, "MmHubPadding[1] = 0x%x\n", pptable->MmHubPadding[1]);
	dev_info(smu->adev->dev, "MmHubPadding[2] = 0x%x\n", pptable->MmHubPadding[2]);
	dev_info(smu->adev->dev, "MmHubPadding[3] = 0x%x\n", pptable->MmHubPadding[3]);
	dev_info(smu->adev->dev, "MmHubPadding[4] = 0x%x\n", pptable->MmHubPadding[4]);
	dev_info(smu->adev->dev, "MmHubPadding[5] = 0x%x\n", pptable->MmHubPadding[5]);
	dev_info(smu->adev->dev, "MmHubPadding[6] = 0x%x\n", pptable->MmHubPadding[6]);
	dev_info(smu->adev->dev, "MmHubPadding[7] = 0x%x\n", pptable->MmHubPadding[7]);
}

static int sienna_cichlid_i2c_xfer(struct i2c_adapter *i2c_adap,
				   struct i2c_msg *msg, int num_msgs)
{
	struct amdgpu_smu_i2c_bus *smu_i2c = i2c_get_adapdata(i2c_adap);
	struct amdgpu_device *adev = smu_i2c->adev;
	struct smu_context *smu = adev->powerplay.pp_handle;
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *table = &smu_table->driver_table;
	SwI2cRequest_t *req, *res = (SwI2cRequest_t *)table->cpu_addr;
	int i, j, r, c;
	u16 dir;

	if (!adev->pm.dpm_enabled)
		return -EBUSY;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	req->I2CcontrollerPort = smu_i2c->port;
	req->I2CSpeed = I2C_SPEED_FAST_400K;
	req->SlaveAddress = msg[0].addr << 1; /* wants an 8-bit address */
	dir = msg[0].flags & I2C_M_RD;

	for (c = i = 0; i < num_msgs; i++) {
		for (j = 0; j < msg[i].len; j++, c++) {
			SwI2cCmd_t *cmd = &req->SwI2cCmds[c];

			if (!(msg[i].flags & I2C_M_RD)) {
				/* write */
				cmd->CmdConfig |= CMDCONFIG_READWRITE_MASK;
				cmd->ReadWriteData = msg[i].buf[j];
			}

			if ((dir ^ msg[i].flags) & I2C_M_RD) {
				/* The direction changes.
				 */
				dir = msg[i].flags & I2C_M_RD;
				cmd->CmdConfig |= CMDCONFIG_RESTART_MASK;
			}

			req->NumCmds++;

			/*
			 * Insert STOP if we are at the last byte of either last
			 * message for the transaction or the client explicitly
			 * requires a STOP at this particular message.
			 */
			if ((j == msg[i].len - 1) &&
			    ((i == num_msgs - 1) || (msg[i].flags & I2C_M_STOP))) {
				cmd->CmdConfig &= ~CMDCONFIG_RESTART_MASK;
				cmd->CmdConfig |= CMDCONFIG_STOP_MASK;
			}
		}
	}
	mutex_lock(&adev->pm.mutex);
	r = smu_cmn_update_table(smu, SMU_TABLE_I2C_COMMANDS, 0, req, true);
	mutex_unlock(&adev->pm.mutex);
	if (r)
		goto fail;

	for (c = i = 0; i < num_msgs; i++) {
		if (!(msg[i].flags & I2C_M_RD)) {
			c += msg[i].len;
			continue;
		}
		for (j = 0; j < msg[i].len; j++, c++) {
			SwI2cCmd_t *cmd = &res->SwI2cCmds[c];

			msg[i].buf[j] = cmd->ReadWriteData;
		}
	}
	r = num_msgs;
fail:
	kfree(req);
	return r;
}

static u32 sienna_cichlid_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}


static const struct i2c_algorithm sienna_cichlid_i2c_algo = {
	.master_xfer = sienna_cichlid_i2c_xfer,
	.functionality = sienna_cichlid_i2c_func,
};

static const struct i2c_adapter_quirks sienna_cichlid_i2c_control_quirks = {
	.flags = I2C_AQ_COMB | I2C_AQ_COMB_SAME_ADDR | I2C_AQ_NO_ZERO_LEN,
	.max_read_len  = MAX_SW_I2C_COMMANDS,
	.max_write_len = MAX_SW_I2C_COMMANDS,
	.max_comb_1st_msg_len = 2,
	.max_comb_2nd_msg_len = MAX_SW_I2C_COMMANDS - 2,
};

static int sienna_cichlid_i2c_control_init(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	int res, i;

	for (i = 0; i < MAX_SMU_I2C_BUSES; i++) {
		struct amdgpu_smu_i2c_bus *smu_i2c = &adev->pm.smu_i2c[i];
		struct i2c_adapter *control = &smu_i2c->adapter;

		smu_i2c->adev = adev;
		smu_i2c->port = i;
		mutex_init(&smu_i2c->mutex);
		control->owner = THIS_MODULE;
		control->class = I2C_CLASS_HWMON;
		control->dev.parent = &adev->pdev->dev;
		control->algo = &sienna_cichlid_i2c_algo;
		snprintf(control->name, sizeof(control->name), "AMDGPU SMU %d", i);
		control->quirks = &sienna_cichlid_i2c_control_quirks;
		i2c_set_adapdata(control, smu_i2c);

		res = i2c_add_adapter(control);
		if (res) {
			DRM_ERROR("Failed to register hw i2c, err: %d\n", res);
			goto Out_err;
		}
	}
	/* assign the buses used for the FRU EEPROM and RAS EEPROM */
	/* XXX ideally this would be something in a vbios data table */
	adev->pm.ras_eeprom_i2c_bus = &adev->pm.smu_i2c[1].adapter;
	adev->pm.fru_eeprom_i2c_bus = &adev->pm.smu_i2c[0].adapter;

	return 0;
Out_err:
	for ( ; i >= 0; i--) {
		struct amdgpu_smu_i2c_bus *smu_i2c = &adev->pm.smu_i2c[i];
		struct i2c_adapter *control = &smu_i2c->adapter;

		i2c_del_adapter(control);
	}
	return res;
}

static void sienna_cichlid_i2c_control_fini(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	int i;

	for (i = 0; i < MAX_SMU_I2C_BUSES; i++) {
		struct amdgpu_smu_i2c_bus *smu_i2c = &adev->pm.smu_i2c[i];
		struct i2c_adapter *control = &smu_i2c->adapter;

		i2c_del_adapter(control);
	}
	adev->pm.ras_eeprom_i2c_bus = NULL;
	adev->pm.fru_eeprom_i2c_bus = NULL;
}

static ssize_t sienna_cichlid_get_gpu_metrics(struct smu_context *smu,
					      void **table)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct gpu_metrics_v1_3 *gpu_metrics =
		(struct gpu_metrics_v1_3 *)smu_table->gpu_metrics_table;
	SmuMetricsExternal_t metrics_external;
	SmuMetrics_t *metrics =
		&(metrics_external.SmuMetrics);
	SmuMetrics_V2_t *metrics_v2 =
		&(metrics_external.SmuMetrics_V2);
	SmuMetrics_V3_t *metrics_v3 =
		&(metrics_external.SmuMetrics_V3);
	struct amdgpu_device *adev = smu->adev;
	bool use_metrics_v2 = false;
	bool use_metrics_v3 = false;
	uint16_t average_gfx_activity;
	int ret = 0;

	switch (smu->adev->ip_versions[MP1_HWIP][0]) {
	case IP_VERSION(11, 0, 7):
		if (smu->smc_fw_version >= 0x3A4900)
			use_metrics_v3 = true;
		else if (smu->smc_fw_version >= 0x3A4300)
			use_metrics_v2 = true;
		break;
	case IP_VERSION(11, 0, 11):
		if (smu->smc_fw_version >= 0x412D00)
			use_metrics_v2 = true;
		break;
	case IP_VERSION(11, 0, 12):
		if (smu->smc_fw_version >= 0x3B2300)
			use_metrics_v2 = true;
		break;
	case IP_VERSION(11, 0, 13):
		if (smu->smc_fw_version >= 0x491100)
			use_metrics_v2 = true;
		break;
	default:
		break;
	}

	ret = smu_cmn_get_metrics_table(smu,
					&metrics_external,
					true);
	if (ret)
		return ret;

	smu_cmn_init_soft_gpu_metrics(gpu_metrics, 1, 3);

	gpu_metrics->temperature_edge = use_metrics_v3 ? metrics_v3->TemperatureEdge :
		use_metrics_v2 ? metrics_v2->TemperatureEdge : metrics->TemperatureEdge;
	gpu_metrics->temperature_hotspot = use_metrics_v3 ? metrics_v3->TemperatureHotspot :
		use_metrics_v2 ? metrics_v2->TemperatureHotspot : metrics->TemperatureHotspot;
	gpu_metrics->temperature_mem = use_metrics_v3 ? metrics_v3->TemperatureMem :
		use_metrics_v2 ? metrics_v2->TemperatureMem : metrics->TemperatureMem;
	gpu_metrics->temperature_vrgfx = use_metrics_v3 ? metrics_v3->TemperatureVrGfx :
		use_metrics_v2 ? metrics_v2->TemperatureVrGfx : metrics->TemperatureVrGfx;
	gpu_metrics->temperature_vrsoc = use_metrics_v3 ? metrics_v3->TemperatureVrSoc :
		use_metrics_v2 ? metrics_v2->TemperatureVrSoc : metrics->TemperatureVrSoc;
	gpu_metrics->temperature_vrmem = use_metrics_v3 ? metrics_v3->TemperatureVrMem0 :
		use_metrics_v2 ? metrics_v2->TemperatureVrMem0 : metrics->TemperatureVrMem0;

	gpu_metrics->average_gfx_activity = use_metrics_v3 ? metrics_v3->AverageGfxActivity :
		use_metrics_v2 ? metrics_v2->AverageGfxActivity : metrics->AverageGfxActivity;
	gpu_metrics->average_umc_activity = use_metrics_v3 ? metrics_v3->AverageUclkActivity :
		use_metrics_v2 ? metrics_v2->AverageUclkActivity : metrics->AverageUclkActivity;
	gpu_metrics->average_mm_activity = use_metrics_v3 ?
		(metrics_v3->VcnUsagePercentage0 + metrics_v3->VcnUsagePercentage1) / 2 :
		use_metrics_v2 ? metrics_v2->VcnActivityPercentage : metrics->VcnActivityPercentage;

	gpu_metrics->average_socket_power = use_metrics_v3 ? metrics_v3->AverageSocketPower :
		use_metrics_v2 ? metrics_v2->AverageSocketPower : metrics->AverageSocketPower;
	gpu_metrics->energy_accumulator = use_metrics_v3 ? metrics_v3->EnergyAccumulator :
		use_metrics_v2 ? metrics_v2->EnergyAccumulator : metrics->EnergyAccumulator;

	if (metrics->CurrGfxVoltageOffset)
		gpu_metrics->voltage_gfx =
			(155000 - 625 * metrics->CurrGfxVoltageOffset) / 100;
	if (metrics->CurrMemVidOffset)
		gpu_metrics->voltage_mem =
			(155000 - 625 * metrics->CurrMemVidOffset) / 100;
	if (metrics->CurrSocVoltageOffset)
		gpu_metrics->voltage_soc =
			(155000 - 625 * metrics->CurrSocVoltageOffset) / 100;

	average_gfx_activity = use_metrics_v3 ? metrics_v3->AverageGfxActivity :
		use_metrics_v2 ? metrics_v2->AverageGfxActivity : metrics->AverageGfxActivity;
	if (average_gfx_activity <= SMU_11_0_7_GFX_BUSY_THRESHOLD)
		gpu_metrics->average_gfxclk_frequency =
			use_metrics_v3 ? metrics_v3->AverageGfxclkFrequencyPostDs :
			use_metrics_v2 ? metrics_v2->AverageGfxclkFrequencyPostDs :
			metrics->AverageGfxclkFrequencyPostDs;
	else
		gpu_metrics->average_gfxclk_frequency =
			use_metrics_v3 ? metrics_v3->AverageGfxclkFrequencyPreDs :
			use_metrics_v2 ? metrics_v2->AverageGfxclkFrequencyPreDs :
			metrics->AverageGfxclkFrequencyPreDs;

	gpu_metrics->average_uclk_frequency =
		use_metrics_v3 ? metrics_v3->AverageUclkFrequencyPostDs :
		use_metrics_v2 ? metrics_v2->AverageUclkFrequencyPostDs :
		metrics->AverageUclkFrequencyPostDs;
	gpu_metrics->average_vclk0_frequency = use_metrics_v3 ? metrics_v3->AverageVclk0Frequency :
		use_metrics_v2 ? metrics_v2->AverageVclk0Frequency : metrics->AverageVclk0Frequency;
	gpu_metrics->average_dclk0_frequency = use_metrics_v3 ? metrics_v3->AverageDclk0Frequency :
		use_metrics_v2 ? metrics_v2->AverageDclk0Frequency : metrics->AverageDclk0Frequency;
	gpu_metrics->average_vclk1_frequency = use_metrics_v3 ? metrics_v3->AverageVclk1Frequency :
		use_metrics_v2 ? metrics_v2->AverageVclk1Frequency : metrics->AverageVclk1Frequency;
	gpu_metrics->average_dclk1_frequency = use_metrics_v3 ? metrics_v3->AverageDclk1Frequency :
		use_metrics_v2 ? metrics_v2->AverageDclk1Frequency : metrics->AverageDclk1Frequency;

	gpu_metrics->current_gfxclk = use_metrics_v3 ? metrics_v3->CurrClock[PPCLK_GFXCLK] :
		use_metrics_v2 ? metrics_v2->CurrClock[PPCLK_GFXCLK] : metrics->CurrClock[PPCLK_GFXCLK];
	gpu_metrics->current_socclk = use_metrics_v3 ? metrics_v3->CurrClock[PPCLK_SOCCLK] :
		use_metrics_v2 ? metrics_v2->CurrClock[PPCLK_SOCCLK] : metrics->CurrClock[PPCLK_SOCCLK];
	gpu_metrics->current_uclk = use_metrics_v3 ? metrics_v3->CurrClock[PPCLK_UCLK] :
		use_metrics_v2 ? metrics_v2->CurrClock[PPCLK_UCLK] : metrics->CurrClock[PPCLK_UCLK];
	gpu_metrics->current_vclk0 = use_metrics_v3 ? metrics_v3->CurrClock[PPCLK_VCLK_0] :
		use_metrics_v2 ? metrics_v2->CurrClock[PPCLK_VCLK_0] : metrics->CurrClock[PPCLK_VCLK_0];
	gpu_metrics->current_dclk0 = use_metrics_v3 ? metrics_v3->CurrClock[PPCLK_DCLK_0] :
		use_metrics_v2 ? metrics_v2->CurrClock[PPCLK_DCLK_0] : metrics->CurrClock[PPCLK_DCLK_0];
	gpu_metrics->current_vclk1 = use_metrics_v3 ? metrics_v3->CurrClock[PPCLK_VCLK_1] :
		use_metrics_v2 ? metrics_v2->CurrClock[PPCLK_VCLK_1] : metrics->CurrClock[PPCLK_VCLK_1];
	gpu_metrics->current_dclk1 = use_metrics_v3 ? metrics_v3->CurrClock[PPCLK_DCLK_1] :
		use_metrics_v2 ? metrics_v2->CurrClock[PPCLK_DCLK_1] : metrics->CurrClock[PPCLK_DCLK_1];

	gpu_metrics->throttle_status = sienna_cichlid_get_throttler_status_locked(smu);
	gpu_metrics->indep_throttle_status =
			smu_cmn_get_indep_throttler_status(gpu_metrics->throttle_status,
							   sienna_cichlid_throttler_map);

	gpu_metrics->current_fan_speed = use_metrics_v3 ? metrics_v3->CurrFanSpeed :
		use_metrics_v2 ? metrics_v2->CurrFanSpeed : metrics->CurrFanSpeed;

	if (((adev->ip_versions[MP1_HWIP][0] == IP_VERSION(11, 0, 7)) && smu->smc_fw_version > 0x003A1E00) ||
	      ((adev->ip_versions[MP1_HWIP][0] == IP_VERSION(11, 0, 11)) && smu->smc_fw_version > 0x00410400)) {
		gpu_metrics->pcie_link_width = use_metrics_v3 ? metrics_v3->PcieWidth :
			use_metrics_v2 ? metrics_v2->PcieWidth : metrics->PcieWidth;
		gpu_metrics->pcie_link_speed = link_speed[use_metrics_v3 ? metrics_v3->PcieRate :
			use_metrics_v2 ? metrics_v2->PcieRate : metrics->PcieRate];
	} else {
		gpu_metrics->pcie_link_width =
				smu_v11_0_get_current_pcie_link_width(smu);
		gpu_metrics->pcie_link_speed =
				smu_v11_0_get_current_pcie_link_speed(smu);
	}

	gpu_metrics->system_clock_counter = ktime_get_boottime_ns();

	*table = (void *)gpu_metrics;

	return sizeof(struct gpu_metrics_v1_3);
}

static int sienna_cichlid_check_ecc_table_support(struct smu_context *smu)
{
	uint32_t if_version = 0xff, smu_version = 0xff;
	int ret = 0;

	ret = smu_cmn_get_smc_version(smu, &if_version, &smu_version);
	if (ret)
		return -EOPNOTSUPP;

	if (smu_version < SUPPORT_ECCTABLE_SMU_VERSION)
		ret = -EOPNOTSUPP;

	return ret;
}

static ssize_t sienna_cichlid_get_ecc_info(struct smu_context *smu,
					void *table)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	EccInfoTable_t *ecc_table = NULL;
	struct ecc_info_per_ch *ecc_info_per_channel = NULL;
	int i, ret = 0;
	struct umc_ecc_info *eccinfo = (struct umc_ecc_info *)table;

	ret = sienna_cichlid_check_ecc_table_support(smu);
	if (ret)
		return ret;

	ret = smu_cmn_update_table(smu,
				SMU_TABLE_ECCINFO,
				0,
				smu_table->ecc_table,
				false);
	if (ret) {
		dev_info(smu->adev->dev, "Failed to export SMU ecc table!\n");
		return ret;
	}

	ecc_table = (EccInfoTable_t *)smu_table->ecc_table;

	for (i = 0; i < SIENNA_CICHLID_UMC_CHANNEL_NUM; i++) {
		ecc_info_per_channel = &(eccinfo->ecc[i]);
		ecc_info_per_channel->ce_count_lo_chip =
			ecc_table->EccInfo[i].ce_count_lo_chip;
		ecc_info_per_channel->ce_count_hi_chip =
			ecc_table->EccInfo[i].ce_count_hi_chip;
		ecc_info_per_channel->mca_umc_status =
			ecc_table->EccInfo[i].mca_umc_status;
		ecc_info_per_channel->mca_umc_addr =
			ecc_table->EccInfo[i].mca_umc_addr;
	}

	return ret;
}
static int sienna_cichlid_enable_mgpu_fan_boost(struct smu_context *smu)
{
	uint16_t *mgpu_fan_boost_limit_rpm;

	GET_PPTABLE_MEMBER(MGpuFanBoostLimitRpm, &mgpu_fan_boost_limit_rpm);
	/*
	 * Skip the MGpuFanBoost setting for those ASICs
	 * which do not support it
	 */
	if (*mgpu_fan_boost_limit_rpm == 0)
		return 0;

	return smu_cmn_send_smc_msg_with_param(smu,
					       SMU_MSG_SetMGpuFanBoostLimitRpm,
					       0,
					       NULL);
}

static int sienna_cichlid_gpo_control(struct smu_context *smu,
				      bool enablement)
{
	uint32_t smu_version;
	int ret = 0;


	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_GFX_GPO_BIT)) {
		ret = smu_cmn_get_smc_version(smu, NULL, &smu_version);
		if (ret)
			return ret;

		if (enablement) {
			if (smu_version < 0x003a2500) {
				ret = smu_cmn_send_smc_msg_with_param(smu,
								      SMU_MSG_SetGpoFeaturePMask,
								      GFX_GPO_PACE_MASK | GFX_GPO_DEM_MASK,
								      NULL);
			} else {
				ret = smu_cmn_send_smc_msg_with_param(smu,
								      SMU_MSG_DisallowGpo,
								      0,
								      NULL);
			}
		} else {
			if (smu_version < 0x003a2500) {
				ret = smu_cmn_send_smc_msg_with_param(smu,
								      SMU_MSG_SetGpoFeaturePMask,
								      0,
								      NULL);
			} else {
				ret = smu_cmn_send_smc_msg_with_param(smu,
								      SMU_MSG_DisallowGpo,
								      1,
								      NULL);
			}
		}
	}

	return ret;
}

static int sienna_cichlid_notify_2nd_usb20_port(struct smu_context *smu)
{
	uint32_t smu_version;
	int ret = 0;

	ret = smu_cmn_get_smc_version(smu, NULL, &smu_version);
	if (ret)
		return ret;

	/*
	 * Message SMU_MSG_Enable2ndUSB20Port is supported by 58.45
	 * onwards PMFWs.
	 */
	if (smu_version < 0x003A2D00)
		return 0;

	return smu_cmn_send_smc_msg_with_param(smu,
					       SMU_MSG_Enable2ndUSB20Port,
					       smu->smu_table.boot_values.firmware_caps & ATOM_FIRMWARE_CAP_ENABLE_2ND_USB20PORT ?
					       1 : 0,
					       NULL);
}

static int sienna_cichlid_system_features_control(struct smu_context *smu,
						  bool en)
{
	int ret = 0;

	if (en) {
		ret = sienna_cichlid_notify_2nd_usb20_port(smu);
		if (ret)
			return ret;
	}

	return smu_v11_0_system_features_control(smu, en);
}

static int sienna_cichlid_set_mp1_state(struct smu_context *smu,
					enum pp_mp1_state mp1_state)
{
	int ret;

	switch (mp1_state) {
	case PP_MP1_STATE_UNLOAD:
		ret = smu_cmn_set_mp1_state(smu, mp1_state);
		break;
	default:
		/* Ignore others */
		ret = 0;
	}

	return ret;
}

static void sienna_cichlid_stb_init(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t reg;

	reg = RREG32_PCIE(MP1_Public | smnMP1_PMI_3_START);
	smu->stb_context.enabled = REG_GET_FIELD(reg, MP1_PMI_3_START, ENABLE);

	/* STB is disabled */
	if (!smu->stb_context.enabled)
		return;

	spin_lock_init(&smu->stb_context.lock);

	/* STB buffer size in bytes as function of FIFO depth */
	reg = RREG32_PCIE(MP1_Public | smnMP1_PMI_3_FIFO);
	smu->stb_context.stb_buf_size = 1 << REG_GET_FIELD(reg, MP1_PMI_3_FIFO, DEPTH);
	smu->stb_context.stb_buf_size *=  SIENNA_CICHLID_STB_DEPTH_UNIT_BYTES;

	dev_info(smu->adev->dev, "STB initialized to %d entries",
		 smu->stb_context.stb_buf_size / SIENNA_CICHLID_STB_DEPTH_UNIT_BYTES);

}

static int sienna_cichlid_get_default_config_table_settings(struct smu_context *smu,
							    struct config_table_setting *table)
{
	struct amdgpu_device *adev = smu->adev;

	if (!table)
		return -EINVAL;

	table->gfxclk_average_tau = 10;
	table->socclk_average_tau = 10;
	table->fclk_average_tau = 10;
	table->uclk_average_tau = 10;
	table->gfx_activity_average_tau = 10;
	table->mem_activity_average_tau = 10;
	table->socket_power_average_tau = 100;
	if (adev->ip_versions[MP1_HWIP][0] != IP_VERSION(11, 0, 7))
		table->apu_socket_power_average_tau = 100;

	return 0;
}

static int sienna_cichlid_set_config_table(struct smu_context *smu,
					   struct config_table_setting *table)
{
	DriverSmuConfigExternal_t driver_smu_config_table;

	if (!table)
		return -EINVAL;

	memset(&driver_smu_config_table,
	       0,
	       sizeof(driver_smu_config_table));
	driver_smu_config_table.DriverSmuConfig.GfxclkAverageLpfTau =
				table->gfxclk_average_tau;
	driver_smu_config_table.DriverSmuConfig.FclkAverageLpfTau =
				table->fclk_average_tau;
	driver_smu_config_table.DriverSmuConfig.UclkAverageLpfTau =
				table->uclk_average_tau;
	driver_smu_config_table.DriverSmuConfig.GfxActivityLpfTau =
				table->gfx_activity_average_tau;
	driver_smu_config_table.DriverSmuConfig.UclkActivityLpfTau =
				table->mem_activity_average_tau;
	driver_smu_config_table.DriverSmuConfig.SocketPowerLpfTau =
				table->socket_power_average_tau;

	return smu_cmn_update_table(smu,
				    SMU_TABLE_DRIVER_SMU_CONFIG,
				    0,
				    (void *)&driver_smu_config_table,
				    true);
}

static int sienna_cichlid_stb_get_data_direct(struct smu_context *smu,
					      void *buf,
					      uint32_t size)
{
	uint32_t *p = buf;
	struct amdgpu_device *adev = smu->adev;

	/* No need to disable interrupts for now as we don't lock it yet from ISR */
	spin_lock(&smu->stb_context.lock);

	/*
	 * Read the STB FIFO in units of 32bit since this is the accessor window
	 * (register width) we have.
	 */
	buf = ((char *) buf) + size;
	while ((void *)p < buf)
		*p++ = cpu_to_le32(RREG32_PCIE(MP1_Public | smnMP1_PMI_3));

	spin_unlock(&smu->stb_context.lock);

	return 0;
}

static const struct pptable_funcs sienna_cichlid_ppt_funcs = {
	.get_allowed_feature_mask = sienna_cichlid_get_allowed_feature_mask,
	.set_default_dpm_table = sienna_cichlid_set_default_dpm_table,
	.dpm_set_vcn_enable = sienna_cichlid_dpm_set_vcn_enable,
	.dpm_set_jpeg_enable = sienna_cichlid_dpm_set_jpeg_enable,
	.i2c_init = sienna_cichlid_i2c_control_init,
	.i2c_fini = sienna_cichlid_i2c_control_fini,
	.print_clk_levels = sienna_cichlid_print_clk_levels,
	.force_clk_levels = sienna_cichlid_force_clk_levels,
	.populate_umd_state_clk = sienna_cichlid_populate_umd_state_clk,
	.pre_display_config_changed = sienna_cichlid_pre_display_config_changed,
	.display_config_changed = sienna_cichlid_display_config_changed,
	.notify_smc_display_config = sienna_cichlid_notify_smc_display_config,
	.is_dpm_running = sienna_cichlid_is_dpm_running,
	.get_fan_speed_pwm = smu_v11_0_get_fan_speed_pwm,
	.get_fan_speed_rpm = sienna_cichlid_get_fan_speed_rpm,
	.get_power_profile_mode = sienna_cichlid_get_power_profile_mode,
	.set_power_profile_mode = sienna_cichlid_set_power_profile_mode,
	.set_watermarks_table = sienna_cichlid_set_watermarks_table,
	.read_sensor = sienna_cichlid_read_sensor,
	.get_uclk_dpm_states = sienna_cichlid_get_uclk_dpm_states,
	.set_performance_level = smu_v11_0_set_performance_level,
	.get_thermal_temperature_range = sienna_cichlid_get_thermal_temperature_range,
	.display_disable_memory_clock_switch = sienna_cichlid_display_disable_memory_clock_switch,
	.get_power_limit = sienna_cichlid_get_power_limit,
	.update_pcie_parameters = sienna_cichlid_update_pcie_parameters,
	.dump_pptable = sienna_cichlid_dump_pptable,
	.init_microcode = smu_v11_0_init_microcode,
	.load_microcode = smu_v11_0_load_microcode,
	.fini_microcode = smu_v11_0_fini_microcode,
	.init_smc_tables = sienna_cichlid_init_smc_tables,
	.fini_smc_tables = smu_v11_0_fini_smc_tables,
	.init_power = smu_v11_0_init_power,
	.fini_power = smu_v11_0_fini_power,
	.check_fw_status = smu_v11_0_check_fw_status,
	.setup_pptable = sienna_cichlid_setup_pptable,
	.get_vbios_bootup_values = smu_v11_0_get_vbios_bootup_values,
	.check_fw_version = smu_v11_0_check_fw_version,
	.write_pptable = smu_cmn_write_pptable,
	.set_driver_table_location = smu_v11_0_set_driver_table_location,
	.set_tool_table_location = smu_v11_0_set_tool_table_location,
	.notify_memory_pool_location = smu_v11_0_notify_memory_pool_location,
	.system_features_control = sienna_cichlid_system_features_control,
	.send_smc_msg_with_param = smu_cmn_send_smc_msg_with_param,
	.send_smc_msg = smu_cmn_send_smc_msg,
	.init_display_count = NULL,
	.set_allowed_mask = smu_v11_0_set_allowed_mask,
	.get_enabled_mask = smu_cmn_get_enabled_mask,
	.feature_is_enabled = smu_cmn_feature_is_enabled,
	.disable_all_features_with_exception = smu_cmn_disable_all_features_with_exception,
	.notify_display_change = NULL,
	.set_power_limit = smu_v11_0_set_power_limit,
	.init_max_sustainable_clocks = smu_v11_0_init_max_sustainable_clocks,
	.enable_thermal_alert = smu_v11_0_enable_thermal_alert,
	.disable_thermal_alert = smu_v11_0_disable_thermal_alert,
	.set_min_dcef_deep_sleep = NULL,
	.display_clock_voltage_request = smu_v11_0_display_clock_voltage_request,
	.get_fan_control_mode = smu_v11_0_get_fan_control_mode,
	.set_fan_control_mode = smu_v11_0_set_fan_control_mode,
	.set_fan_speed_pwm = smu_v11_0_set_fan_speed_pwm,
	.set_fan_speed_rpm = smu_v11_0_set_fan_speed_rpm,
	.set_xgmi_pstate = smu_v11_0_set_xgmi_pstate,
	.gfx_off_control = smu_v11_0_gfx_off_control,
	.register_irq_handler = smu_v11_0_register_irq_handler,
	.set_azalia_d3_pme = smu_v11_0_set_azalia_d3_pme,
	.get_max_sustainable_clocks_by_dc = smu_v11_0_get_max_sustainable_clocks_by_dc,
	.baco_is_support = smu_v11_0_baco_is_support,
	.baco_get_state = smu_v11_0_baco_get_state,
	.baco_set_state = smu_v11_0_baco_set_state,
	.baco_enter = sienna_cichlid_baco_enter,
	.baco_exit = sienna_cichlid_baco_exit,
	.mode1_reset_is_support = sienna_cichlid_is_mode1_reset_supported,
	.mode1_reset = smu_v11_0_mode1_reset,
	.get_dpm_ultimate_freq = sienna_cichlid_get_dpm_ultimate_freq,
	.set_soft_freq_limited_range = smu_v11_0_set_soft_freq_limited_range,
	.set_default_od_settings = sienna_cichlid_set_default_od_settings,
	.od_edit_dpm_table = sienna_cichlid_od_edit_dpm_table,
	.restore_user_od_settings = smu_v11_0_restore_user_od_settings,
	.run_btc = sienna_cichlid_run_btc,
	.set_power_source = smu_v11_0_set_power_source,
	.get_pp_feature_mask = smu_cmn_get_pp_feature_mask,
	.set_pp_feature_mask = smu_cmn_set_pp_feature_mask,
	.get_gpu_metrics = sienna_cichlid_get_gpu_metrics,
	.enable_mgpu_fan_boost = sienna_cichlid_enable_mgpu_fan_boost,
	.gfx_ulv_control = smu_v11_0_gfx_ulv_control,
	.deep_sleep_control = smu_v11_0_deep_sleep_control,
	.get_fan_parameters = sienna_cichlid_get_fan_parameters,
	.interrupt_work = smu_v11_0_interrupt_work,
	.gpo_control = sienna_cichlid_gpo_control,
	.set_mp1_state = sienna_cichlid_set_mp1_state,
	.stb_collect_info = sienna_cichlid_stb_get_data_direct,
	.get_ecc_info = sienna_cichlid_get_ecc_info,
	.get_default_config_table_settings = sienna_cichlid_get_default_config_table_settings,
	.set_config_table = sienna_cichlid_set_config_table,
	.get_unique_id = sienna_cichlid_get_unique_id,
};

void sienna_cichlid_set_ppt_funcs(struct smu_context *smu)
{
	smu->ppt_funcs = &sienna_cichlid_ppt_funcs;
	smu->message_map = sienna_cichlid_message_map;
	smu->clock_map = sienna_cichlid_clk_map;
	smu->feature_map = sienna_cichlid_feature_mask_map;
	smu->table_map = sienna_cichlid_table_map;
	smu->pwr_src_map = sienna_cichlid_pwr_src_map;
	smu->workload_map = sienna_cichlid_workload_map;
	smu_v11_0_set_smu_mailbox_registers(smu);
}
