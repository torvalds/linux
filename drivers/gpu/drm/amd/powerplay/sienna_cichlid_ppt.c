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

#define to_amdgpu_device(x) (container_of(x, struct amdgpu_device, pm.smu_i2c))

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
	MSG_MAP(SetDriverDramAddrHigh,		PPSMC_MSG_SetDriverDramAddrHigh,       0),
	MSG_MAP(SetDriverDramAddrLow,		PPSMC_MSG_SetDriverDramAddrLow,        0),
	MSG_MAP(SetToolsDramAddrHigh,		PPSMC_MSG_SetToolsDramAddrHigh,        0),
	MSG_MAP(SetToolsDramAddrLow,		PPSMC_MSG_SetToolsDramAddrLow,         0),
	MSG_MAP(TransferTableSmu2Dram,		PPSMC_MSG_TransferTableSmu2Dram,       0),
	MSG_MAP(TransferTableDram2Smu,		PPSMC_MSG_TransferTableDram2Smu,       0),
	MSG_MAP(UseDefaultPPTable,		PPSMC_MSG_UseDefaultPPTable,           0),
	MSG_MAP(RunDcBtc,			PPSMC_MSG_RunDcBtc,                    0),
	MSG_MAP(EnterBaco,			PPSMC_MSG_EnterBaco,                   0),
	MSG_MAP(SetSoftMinByFreq,		PPSMC_MSG_SetSoftMinByFreq,            0),
	MSG_MAP(SetSoftMaxByFreq,		PPSMC_MSG_SetSoftMaxByFreq,            0),
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
	FEA_MAP(DPM_SOCCLK),
	FEA_MAP(DPM_MP0CLK),
	FEA_MAP(DPM_LINK),
	FEA_MAP(DPM_DCEFCLK),
	FEA_MAP(MEM_VDDCI_SCALING),
	FEA_MAP(MEM_MVDD_SCALING),
	FEA_MAP(DS_GFXCLK),
	FEA_MAP(DS_SOCCLK),
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
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_COMPUTE,		WORKLOAD_PPLIB_CUSTOM_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_CUSTOM,		WORKLOAD_PPLIB_CUSTOM_BIT),
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

	return 0;
}

static int sienna_cichlid_check_powerplay_table(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_11_0_7_powerplay_table *powerplay_table =
		table_context->power_play_table;
	struct smu_baco_context *smu_baco = &smu->smu_baco;

	mutex_lock(&smu_baco->mutex);
	if (powerplay_table->platform_caps & SMU_11_0_7_PP_PLATFORM_CAP_BACO ||
	    powerplay_table->platform_caps & SMU_11_0_7_PP_PLATFORM_CAP_MACO)
		smu_baco->platform_support = true;
	mutex_unlock(&smu_baco->mutex);

	table_context->thermal_controller_type =
		powerplay_table->thermal_controller_type;

	return 0;
}

static int sienna_cichlid_append_powerplay_table(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *smc_pptable = table_context->driver_pptable;
	struct atom_smc_dpm_info_v4_9 *smc_dpm_table;
	int index, ret;

	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
					    smc_dpm_info);

	ret = amdgpu_atombios_get_data_table(smu->adev, index, NULL, NULL, NULL,
				      (uint8_t **)&smc_dpm_table);
	if (ret)
		return ret;

	memcpy(smc_pptable->I2cControllers, smc_dpm_table->I2cControllers,
	       sizeof(*smc_dpm_table) - sizeof(smc_dpm_table->table_header));
	
	return 0;
}

static int sienna_cichlid_store_powerplay_table(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_11_0_7_powerplay_table *powerplay_table =
		table_context->power_play_table;

	memcpy(table_context->driver_pptable, &powerplay_table->smc_pptable,
	       sizeof(PPTable_t));

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

	return ret;
}

static int sienna_cichlid_tables_init(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = smu_table->tables;

	SMU_TABLE_INIT(tables, SMU_TABLE_PPTABLE, sizeof(PPTable_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_WATERMARKS, sizeof(Watermarks_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_SMU_METRICS, sizeof(SmuMetrics_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_I2C_COMMANDS, sizeof(SwI2cRequest_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_OVERDRIVE, sizeof(OverDriveTable_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_PMSTATUSLOG, SMU11_TOOL_SIZE,
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_ACTIVITY_MONITOR_COEFF,
		       sizeof(DpmActivityMonitorCoeffInt_t), PAGE_SIZE,
	               AMDGPU_GEM_DOMAIN_VRAM);

	smu_table->metrics_table = kzalloc(sizeof(SmuMetrics_t), GFP_KERNEL);
	if (!smu_table->metrics_table)
		return -ENOMEM;
	smu_table->metrics_time = 0;

	smu_table->watermarks_table = kzalloc(sizeof(Watermarks_t), GFP_KERNEL);
	if (!smu_table->watermarks_table)
		return -ENOMEM;

	return 0;
}

static int sienna_cichlid_get_smu_metrics_data(struct smu_context *smu,
					       MetricsMember_t member,
					       uint32_t *value)
{
	struct smu_table_context *smu_table= &smu->smu_table;
	SmuMetrics_t *metrics = (SmuMetrics_t *)smu_table->metrics_table;
	int ret = 0;

	mutex_lock(&smu->metrics_lock);
	if (!smu_table->metrics_time ||
	     time_after(jiffies, smu_table->metrics_time + msecs_to_jiffies(1))) {
		ret = smu_cmn_update_table(smu,
				       SMU_TABLE_SMU_METRICS,
				       0,
				       smu_table->metrics_table,
				       false);
		if (ret) {
			dev_info(smu->adev->dev, "Failed to export SMU metrics table!\n");
			mutex_unlock(&smu->metrics_lock);
			return ret;
		}
		smu_table->metrics_time = jiffies;
	}

	switch (member) {
	case METRICS_CURR_GFXCLK:
		*value = metrics->CurrClock[PPCLK_GFXCLK];
		break;
	case METRICS_CURR_SOCCLK:
		*value = metrics->CurrClock[PPCLK_SOCCLK];
		break;
	case METRICS_CURR_UCLK:
		*value = metrics->CurrClock[PPCLK_UCLK];
		break;
	case METRICS_CURR_VCLK:
		*value = metrics->CurrClock[PPCLK_VCLK_0];
		break;
	case METRICS_CURR_VCLK1:
		*value = metrics->CurrClock[PPCLK_VCLK_1];
		break;
	case METRICS_CURR_DCLK:
		*value = metrics->CurrClock[PPCLK_DCLK_0];
		break;
	case METRICS_CURR_DCLK1:
		*value = metrics->CurrClock[PPCLK_DCLK_1];
		break;
	case METRICS_CURR_DCEFCLK:
		*value = metrics->CurrClock[PPCLK_DCEFCLK];
		break;
	case METRICS_AVERAGE_GFXCLK:
		if (metrics->AverageGfxActivity <= SMU_11_0_7_GFX_BUSY_THRESHOLD)
			*value = metrics->AverageGfxclkFrequencyPostDs;
		else
			*value = metrics->AverageGfxclkFrequencyPreDs;
		break;
	case METRICS_AVERAGE_FCLK:
		*value = metrics->AverageFclkFrequencyPostDs;
		break;
	case METRICS_AVERAGE_UCLK:
		*value = metrics->AverageUclkFrequencyPostDs;
		break;
	case METRICS_AVERAGE_GFXACTIVITY:
		*value = metrics->AverageGfxActivity;
		break;
	case METRICS_AVERAGE_MEMACTIVITY:
		*value = metrics->AverageUclkActivity;
		break;
	case METRICS_AVERAGE_SOCKETPOWER:
		*value = metrics->AverageSocketPower << 8;
		break;
	case METRICS_TEMPERATURE_EDGE:
		*value = metrics->TemperatureEdge *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_TEMPERATURE_HOTSPOT:
		*value = metrics->TemperatureHotspot *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_TEMPERATURE_MEM:
		*value = metrics->TemperatureMem *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_TEMPERATURE_VRGFX:
		*value = metrics->TemperatureVrGfx *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_TEMPERATURE_VRSOC:
		*value = metrics->TemperatureVrSoc *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_THROTTLER_STATUS:
		*value = metrics->ThrottlerStatus;
		break;
	case METRICS_CURR_FANSPEED:
		*value = metrics->CurrFanSpeed;
		break;
	default:
		*value = UINT_MAX;
		break;
	}

	mutex_unlock(&smu->metrics_lock);

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

static int sienna_cichlid_init_smc_tables(struct smu_context *smu)
{
	int ret = 0;

	ret = sienna_cichlid_tables_init(smu);
	if (ret)
		return ret;

	ret = sienna_cichlid_allocate_dpm_context(smu);
	if (ret)
		return ret;

	return smu_v11_0_init_smc_tables(smu);
}

static int sienna_cichlid_set_default_dpm_table(struct smu_context *smu)
{
	struct smu_11_0_dpm_context *dpm_context = smu->smu_dpm.dpm_context;
	PPTable_t *driver_ppt = smu->smu_table.driver_pptable;
	struct smu_11_0_dpm_table *dpm_table;
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;

	/* socclk dpm table setup */
	dpm_table = &dpm_context->dpm_tables.soc_table;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_SOCCLK_BIT)) {
		ret = smu_v11_0_set_single_dpm_table(smu,
						     SMU_SOCCLK,
						     dpm_table);
		if (ret)
			return ret;
		dpm_table->is_fine_grained =
			!driver_ppt->DpmDescriptor[PPCLK_SOCCLK].SnapToDiscrete;
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
			!driver_ppt->DpmDescriptor[PPCLK_GFXCLK].SnapToDiscrete;
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
			!driver_ppt->DpmDescriptor[PPCLK_UCLK].SnapToDiscrete;
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
			!driver_ppt->DpmDescriptor[PPCLK_FCLK].SnapToDiscrete;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.fclk / 100;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->min = dpm_table->dpm_levels[0].value;
		dpm_table->max = dpm_table->dpm_levels[0].value;
	}

	/* vclk0 dpm table setup */
	dpm_table = &dpm_context->dpm_tables.vclk_table;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_MM_DPM_PG_BIT)) {
		ret = smu_v11_0_set_single_dpm_table(smu,
						     SMU_VCLK,
						     dpm_table);
		if (ret)
			return ret;
		dpm_table->is_fine_grained =
			!driver_ppt->DpmDescriptor[PPCLK_VCLK_0].SnapToDiscrete;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.vclk / 100;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->min = dpm_table->dpm_levels[0].value;
		dpm_table->max = dpm_table->dpm_levels[0].value;
	}

	/* vclk1 dpm table setup */
	if (adev->vcn.num_vcn_inst > 1) {
		dpm_table = &dpm_context->dpm_tables.vclk1_table;
		if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_MM_DPM_PG_BIT)) {
			ret = smu_v11_0_set_single_dpm_table(smu,
							     SMU_VCLK1,
							     dpm_table);
			if (ret)
				return ret;
			dpm_table->is_fine_grained =
				!driver_ppt->DpmDescriptor[PPCLK_VCLK_1].SnapToDiscrete;
		} else {
			dpm_table->count = 1;
			dpm_table->dpm_levels[0].value =
				smu->smu_table.boot_values.vclk / 100;
			dpm_table->dpm_levels[0].enabled = true;
			dpm_table->min = dpm_table->dpm_levels[0].value;
			dpm_table->max = dpm_table->dpm_levels[0].value;
		}
	}

	/* dclk0 dpm table setup */
	dpm_table = &dpm_context->dpm_tables.dclk_table;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_MM_DPM_PG_BIT)) {
		ret = smu_v11_0_set_single_dpm_table(smu,
						     SMU_DCLK,
						     dpm_table);
		if (ret)
			return ret;
		dpm_table->is_fine_grained =
			!driver_ppt->DpmDescriptor[PPCLK_DCLK_0].SnapToDiscrete;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.dclk / 100;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->min = dpm_table->dpm_levels[0].value;
		dpm_table->max = dpm_table->dpm_levels[0].value;
	}

	/* dclk1 dpm table setup */
	if (adev->vcn.num_vcn_inst > 1) {
		dpm_table = &dpm_context->dpm_tables.dclk1_table;
		if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_MM_DPM_PG_BIT)) {
			ret = smu_v11_0_set_single_dpm_table(smu,
							     SMU_DCLK1,
							     dpm_table);
			if (ret)
				return ret;
			dpm_table->is_fine_grained =
				!driver_ppt->DpmDescriptor[PPCLK_DCLK_1].SnapToDiscrete;
		} else {
			dpm_table->count = 1;
			dpm_table->dpm_levels[0].value =
				smu->smu_table.boot_values.dclk / 100;
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
			!driver_ppt->DpmDescriptor[PPCLK_DCEFCLK].SnapToDiscrete;
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
			!driver_ppt->DpmDescriptor[PPCLK_PIXCLK].SnapToDiscrete;
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
			!driver_ppt->DpmDescriptor[PPCLK_DISPCLK].SnapToDiscrete;
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
			!driver_ppt->DpmDescriptor[PPCLK_PHYCLK].SnapToDiscrete;
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
	int ret = 0;

	if (enable) {
		/* vcn dpm on is a prerequisite for vcn power gate messages */
		if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_MM_DPM_PG_BIT)) {
			ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_PowerUpVcn, 0, NULL);
			if (ret)
				return ret;
			if (adev->vcn.num_vcn_inst > 1) {
				ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_PowerUpVcn,
								  0x10000, NULL);
				if (ret)
					return ret;
			}
		}
	} else {
		if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_MM_DPM_PG_BIT)) {
			ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_PowerDownVcn, 0, NULL);
			if (ret)
				return ret;
			if (adev->vcn.num_vcn_inst > 1) {
				ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_PowerDownVcn,
								  0x10000, NULL);
				if (ret)
					return ret;
			}
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
	PPTable_t *pptable = smu->smu_table.driver_pptable;
	DpmDescriptor_t *dpm_desc = NULL;
	uint32_t clk_index = 0;

	clk_index = smu_cmn_to_asic_specific_index(smu,
						   CMN2ASIC_MAPPING_CLK,
						   clk_type);
	dpm_desc = &pptable->DpmDescriptor[clk_index];

	/* 0 - Fine grained DPM, 1 - Discrete DPM */
	return dpm_desc->SnapToDiscrete == 0 ? true : false;
}

static int sienna_cichlid_print_clk_levels(struct smu_context *smu,
			enum smu_clk_type clk_type, char *buf)
{
	struct amdgpu_device *adev = smu->adev;
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct smu_11_0_dpm_context *dpm_context = smu_dpm->dpm_context;
	PPTable_t *pptable = (PPTable_t *)table_context->driver_pptable;
	int i, size = 0, ret = 0;
	uint32_t cur_value = 0, value = 0, count = 0;
	uint32_t freq_values[3] = {0};
	uint32_t mark_index = 0;
	uint32_t gen_speed, lane_width;

	switch (clk_type) {
	case SMU_GFXCLK:
	case SMU_SCLK:
	case SMU_SOCCLK:
	case SMU_MCLK:
	case SMU_UCLK:
	case SMU_FCLK:
	case SMU_DCEFCLK:
		ret = sienna_cichlid_get_current_clk_freq_by_table(smu, clk_type, &cur_value);
		if (ret)
			goto print_clk_out;

		/* no need to disable gfxoff when retrieving the current gfxclk */
		if ((clk_type == SMU_GFXCLK) || (clk_type == SMU_SCLK))
			amdgpu_gfx_off_ctrl(adev, false);

		ret = smu_v11_0_get_dpm_level_count(smu, clk_type, &count);
		if (ret)
			goto print_clk_out;

		if (!sienna_cichlid_is_support_fine_grained_dpm(smu, clk_type)) {
			for (i = 0; i < count; i++) {
				ret = smu_v11_0_get_dpm_freq_by_index(smu, clk_type, i, &value);
				if (ret)
					goto print_clk_out;

				size += sprintf(buf + size, "%d: %uMhz %s\n", i, value,
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
			if (mark_index != 1)
				freq_values[1] = (freq_values[0] + freq_values[2]) / 2;

			for (i = 0; i < 3; i++) {
				size += sprintf(buf + size, "%d: %uMhz %s\n", i, freq_values[i],
						i == mark_index ? "*" : "");
			}

		}
		break;
	case SMU_PCIE:
		gen_speed = (RREG32_PCIE(smnPCIE_LC_SPEED_CNTL) &
			     PSWUSP0_PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE_MASK)
			>> PSWUSP0_PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE__SHIFT;
		lane_width = (RREG32_PCIE(smnPCIE_LC_LINK_WIDTH_CNTL) &
			      PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD_MASK)
			>> PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD__SHIFT;
		for (i = 0; i < NUM_LINK_LEVELS; i++)
			size += sprintf(buf + size, "%d: %s %s %dMhz %s\n", i,
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
					pptable->LclkFreq[i],
					(gen_speed == dpm_context->dpm_tables.pcie_table.pcie_gen[i]) &&
					(lane_width == dpm_context->dpm_tables.pcie_table.pcie_lane[i]) ?
					"*" : "");
		break;
	default:
		break;
	}

print_clk_out:
	if ((clk_type == SMU_GFXCLK) || (clk_type == SMU_SCLK))
		amdgpu_gfx_off_ctrl(adev, true);

	return size;
}

static int sienna_cichlid_force_clk_levels(struct smu_context *smu,
				   enum smu_clk_type clk_type, uint32_t mask)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0, size = 0;
	uint32_t soft_min_level = 0, soft_max_level = 0, min_freq = 0, max_freq = 0;

	soft_min_level = mask ? (ffs(mask) - 1) : 0;
	soft_max_level = mask ? (fls(mask) - 1) : 0;

	if ((clk_type == SMU_GFXCLK) || (clk_type == SMU_SCLK))
		amdgpu_gfx_off_ctrl(adev, false);

	switch (clk_type) {
	case SMU_GFXCLK:
	case SMU_SCLK:
	case SMU_SOCCLK:
	case SMU_MCLK:
	case SMU_UCLK:
	case SMU_DCEFCLK:
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
	default:
		break;
	}

forec_level_out:
	if ((clk_type == SMU_GFXCLK) || (clk_type == SMU_SCLK))
		amdgpu_gfx_off_ctrl(adev, true);

	return size;
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

	pstate_table->gfxclk_pstate.min = gfx_table->min;
	pstate_table->gfxclk_pstate.peak = gfx_table->max;

	pstate_table->uclk_pstate.min = mem_table->min;
	pstate_table->uclk_pstate.peak = mem_table->max;

	pstate_table->socclk_pstate.min = soc_table->min;
	pstate_table->socclk_pstate.peak = soc_table->max;

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
	    smu_cmn_feature_is_supported(smu, SMU_FEATURE_DPM_DCEFCLK_BIT) &&
	    smu_cmn_feature_is_supported(smu, SMU_FEATURE_DPM_SOCCLK_BIT)) {
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

static int sienna_cichlid_get_gpu_power(struct smu_context *smu, uint32_t *value)
{
	if (!value)
		return -EINVAL;

	return sienna_cichlid_get_smu_metrics_data(smu,
						   METRICS_AVERAGE_SOCKETPOWER,
						   value);
}

static int sienna_cichlid_get_current_activity_percent(struct smu_context *smu,
					       enum amd_pp_sensors sensor,
					       uint32_t *value)
{
	int ret = 0;

	if (!value)
		return -EINVAL;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		ret = sienna_cichlid_get_smu_metrics_data(smu,
							  METRICS_AVERAGE_GFXACTIVITY,
							  value);
		break;
	case AMDGPU_PP_SENSOR_MEM_LOAD:
		ret = sienna_cichlid_get_smu_metrics_data(smu,
							  METRICS_AVERAGE_MEMACTIVITY,
							  value);
		break;
	default:
		dev_err(smu->adev->dev, "Invalid sensor for retrieving clock activity\n");
		return -EINVAL;
	}

	return ret;
}

static bool sienna_cichlid_is_dpm_running(struct smu_context *smu)
{
	int ret = 0;
	uint32_t feature_mask[2];
	uint64_t feature_enabled;

	ret = smu_cmn_get_enabled_mask(smu, feature_mask, 2);
	if (ret)
		return false;

	feature_enabled = (uint64_t)feature_mask[1] << 32 | feature_mask[0];

	return !!(feature_enabled & SMC_DPM_FEATURE);
}

static int sienna_cichlid_get_fan_speed_rpm(struct smu_context *smu,
				    uint32_t *speed)
{
	if (!speed)
		return -EINVAL;

	return sienna_cichlid_get_smu_metrics_data(smu,
						   METRICS_CURR_FANSPEED,
						   speed);
}

static int sienna_cichlid_get_fan_speed_percent(struct smu_context *smu,
					uint32_t *speed)
{
	int ret = 0;
	uint32_t percent = 0;
	uint32_t current_rpm;
	PPTable_t *pptable = smu->smu_table.driver_pptable;

	ret = sienna_cichlid_get_fan_speed_rpm(smu, &current_rpm);
	if (ret)
		return ret;

	percent = current_rpm * 100 / pptable->FanMaximumRpm;
	*speed = percent > 100 ? 100 : percent;

	return ret;
}

static int sienna_cichlid_get_power_profile_mode(struct smu_context *smu, char *buf)
{
	DpmActivityMonitorCoeffInt_t activity_monitor;
	uint32_t i, size = 0;
	int16_t workload_type = 0;
	static const char *profile_name[] = {
					"BOOTUP_DEFAULT",
					"3D_FULL_SCREEN",
					"POWER_SAVING",
					"VIDEO",
					"VR",
					"COMPUTE",
					"CUSTOM"};
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

	size += sprintf(buf + size, "%16s %s %s %s %s %s %s %s %s %s %s\n",
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
					  (void *)(&activity_monitor), false);
		if (result) {
			dev_err(smu->adev->dev, "[%s] Failed to get activity monitor!", __func__);
			return result;
		}

		size += sprintf(buf + size, "%2d %14s%s:\n",
			i, profile_name[i], (i == smu->power_profile_mode) ? "*" : " ");

		size += sprintf(buf + size, "%19s %d(%13s) %7d %7d %7d %7d %7d %7d %7d %7d %7d\n",
			" ",
			0,
			"GFXCLK",
			activity_monitor.Gfx_FPS,
			activity_monitor.Gfx_MinFreqStep,
			activity_monitor.Gfx_MinActiveFreqType,
			activity_monitor.Gfx_MinActiveFreq,
			activity_monitor.Gfx_BoosterFreqType,
			activity_monitor.Gfx_BoosterFreq,
			activity_monitor.Gfx_PD_Data_limit_c,
			activity_monitor.Gfx_PD_Data_error_coeff,
			activity_monitor.Gfx_PD_Data_error_rate_coeff);

		size += sprintf(buf + size, "%19s %d(%13s) %7d %7d %7d %7d %7d %7d %7d %7d %7d\n",
			" ",
			1,
			"SOCCLK",
			activity_monitor.Fclk_FPS,
			activity_monitor.Fclk_MinFreqStep,
			activity_monitor.Fclk_MinActiveFreqType,
			activity_monitor.Fclk_MinActiveFreq,
			activity_monitor.Fclk_BoosterFreqType,
			activity_monitor.Fclk_BoosterFreq,
			activity_monitor.Fclk_PD_Data_limit_c,
			activity_monitor.Fclk_PD_Data_error_coeff,
			activity_monitor.Fclk_PD_Data_error_rate_coeff);

		size += sprintf(buf + size, "%19s %d(%13s) %7d %7d %7d %7d %7d %7d %7d %7d %7d\n",
			" ",
			2,
			"MEMLK",
			activity_monitor.Mem_FPS,
			activity_monitor.Mem_MinFreqStep,
			activity_monitor.Mem_MinActiveFreqType,
			activity_monitor.Mem_MinActiveFreq,
			activity_monitor.Mem_BoosterFreqType,
			activity_monitor.Mem_BoosterFreq,
			activity_monitor.Mem_PD_Data_limit_c,
			activity_monitor.Mem_PD_Data_error_coeff,
			activity_monitor.Mem_PD_Data_error_rate_coeff);
	}

	return size;
}

static int sienna_cichlid_set_power_profile_mode(struct smu_context *smu, long *input, uint32_t size)
{
	DpmActivityMonitorCoeffInt_t activity_monitor;
	int workload_type, ret = 0;

	smu->power_profile_mode = input[size];

	if (smu->power_profile_mode > PP_SMC_POWER_PROFILE_CUSTOM) {
		dev_err(smu->adev->dev, "Invalid power profile mode %d\n", smu->power_profile_mode);
		return -EINVAL;
	}

	if (smu->power_profile_mode == PP_SMC_POWER_PROFILE_CUSTOM) {

		ret = smu_cmn_update_table(smu,
				       SMU_TABLE_ACTIVITY_MONITOR_COEFF, WORKLOAD_PPLIB_CUSTOM_BIT,
				       (void *)(&activity_monitor), false);
		if (ret) {
			dev_err(smu->adev->dev, "[%s] Failed to get activity monitor!", __func__);
			return ret;
		}

		switch (input[0]) {
		case 0: /* Gfxclk */
			activity_monitor.Gfx_FPS = input[1];
			activity_monitor.Gfx_MinFreqStep = input[2];
			activity_monitor.Gfx_MinActiveFreqType = input[3];
			activity_monitor.Gfx_MinActiveFreq = input[4];
			activity_monitor.Gfx_BoosterFreqType = input[5];
			activity_monitor.Gfx_BoosterFreq = input[6];
			activity_monitor.Gfx_PD_Data_limit_c = input[7];
			activity_monitor.Gfx_PD_Data_error_coeff = input[8];
			activity_monitor.Gfx_PD_Data_error_rate_coeff = input[9];
			break;
		case 1: /* Socclk */
			activity_monitor.Fclk_FPS = input[1];
			activity_monitor.Fclk_MinFreqStep = input[2];
			activity_monitor.Fclk_MinActiveFreqType = input[3];
			activity_monitor.Fclk_MinActiveFreq = input[4];
			activity_monitor.Fclk_BoosterFreqType = input[5];
			activity_monitor.Fclk_BoosterFreq = input[6];
			activity_monitor.Fclk_PD_Data_limit_c = input[7];
			activity_monitor.Fclk_PD_Data_error_coeff = input[8];
			activity_monitor.Fclk_PD_Data_error_rate_coeff = input[9];
			break;
		case 2: /* Memlk */
			activity_monitor.Mem_FPS = input[1];
			activity_monitor.Mem_MinFreqStep = input[2];
			activity_monitor.Mem_MinActiveFreqType = input[3];
			activity_monitor.Mem_MinActiveFreq = input[4];
			activity_monitor.Mem_BoosterFreqType = input[5];
			activity_monitor.Mem_BoosterFreq = input[6];
			activity_monitor.Mem_PD_Data_limit_c = input[7];
			activity_monitor.Mem_PD_Data_error_coeff = input[8];
			activity_monitor.Mem_PD_Data_error_rate_coeff = input[9];
			break;
		}

		ret = smu_cmn_update_table(smu,
				       SMU_TABLE_ACTIVITY_MONITOR_COEFF, WORKLOAD_PPLIB_CUSTOM_BIT,
				       (void *)(&activity_monitor), true);
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

	if (smu_cmn_feature_is_supported(smu, SMU_FEATURE_DPM_DCEFCLK_BIT)) {
		clock_req.clock_type = amd_pp_dcef_clock;
		clock_req.clock_freq_in_khz = min_clocks.dcef_clock * 10;

		ret = smu_v11_0_display_clock_voltage_request(smu, &clock_req);
		if (!ret) {
			if (smu_cmn_feature_is_supported(smu, SMU_FEATURE_DS_DCEFCLK_BIT)) {
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
					       struct dm_pp_wm_sets_with_clock_ranges_soc15
					       *clock_ranges)
{
	Watermarks_t *table = smu->smu_table.watermarks_table;
	int ret = 0;
	int i;

	if (clock_ranges) {
		if (clock_ranges->num_wm_dmif_sets > 4 ||
		    clock_ranges->num_wm_mcif_sets > 4)
			return -EINVAL;

		for (i = 0; i < clock_ranges->num_wm_dmif_sets; i++) {
			table->WatermarkRow[1][i].MinClock =
				cpu_to_le16((uint16_t)
				(clock_ranges->wm_dmif_clocks_ranges[i].wm_min_dcfclk_clk_in_khz /
				1000));
			table->WatermarkRow[1][i].MaxClock =
				cpu_to_le16((uint16_t)
				(clock_ranges->wm_dmif_clocks_ranges[i].wm_max_dcfclk_clk_in_khz /
				1000));
			table->WatermarkRow[1][i].MinUclk =
				cpu_to_le16((uint16_t)
				(clock_ranges->wm_dmif_clocks_ranges[i].wm_min_mem_clk_in_khz /
				1000));
			table->WatermarkRow[1][i].MaxUclk =
				cpu_to_le16((uint16_t)
				(clock_ranges->wm_dmif_clocks_ranges[i].wm_max_mem_clk_in_khz /
				1000));
			table->WatermarkRow[1][i].WmSetting = (uint8_t)
					clock_ranges->wm_dmif_clocks_ranges[i].wm_set_id;
		}

		for (i = 0; i < clock_ranges->num_wm_mcif_sets; i++) {
			table->WatermarkRow[0][i].MinClock =
				cpu_to_le16((uint16_t)
				(clock_ranges->wm_mcif_clocks_ranges[i].wm_min_socclk_clk_in_khz /
				1000));
			table->WatermarkRow[0][i].MaxClock =
				cpu_to_le16((uint16_t)
				(clock_ranges->wm_mcif_clocks_ranges[i].wm_max_socclk_clk_in_khz /
				1000));
			table->WatermarkRow[0][i].MinUclk =
				cpu_to_le16((uint16_t)
				(clock_ranges->wm_mcif_clocks_ranges[i].wm_min_mem_clk_in_khz /
				1000));
			table->WatermarkRow[0][i].MaxUclk =
				cpu_to_le16((uint16_t)
				(clock_ranges->wm_mcif_clocks_ranges[i].wm_max_mem_clk_in_khz /
				1000));
			table->WatermarkRow[0][i].WmSetting = (uint8_t)
					clock_ranges->wm_mcif_clocks_ranges[i].wm_set_id;
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

static int sienna_cichlid_thermal_get_temperature(struct smu_context *smu,
					     enum amd_pp_sensors sensor,
					     uint32_t *value)
{
	int ret = 0;

	if (!value)
		return -EINVAL;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_HOTSPOT_TEMP:
		ret = sienna_cichlid_get_smu_metrics_data(smu,
							  METRICS_TEMPERATURE_HOTSPOT,
							  value);
		break;
	case AMDGPU_PP_SENSOR_EDGE_TEMP:
		ret = sienna_cichlid_get_smu_metrics_data(smu,
							  METRICS_TEMPERATURE_EDGE,
							  value);
		break;
	case AMDGPU_PP_SENSOR_MEM_TEMP:
		ret = sienna_cichlid_get_smu_metrics_data(smu,
							  METRICS_TEMPERATURE_MEM,
							  value);
		break;
	default:
		dev_err(smu->adev->dev, "Invalid sensor for retrieving temp\n");
		return -EINVAL;
	}

	return ret;
}

static int sienna_cichlid_read_sensor(struct smu_context *smu,
				 enum amd_pp_sensors sensor,
				 void *data, uint32_t *size)
{
	int ret = 0;
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *pptable = table_context->driver_pptable;

	if(!data || !size)
		return -EINVAL;

	mutex_lock(&smu->sensor_lock);
	switch (sensor) {
	case AMDGPU_PP_SENSOR_MAX_FAN_RPM:
		*(uint32_t *)data = pptable->FanMaximumRpm;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_MEM_LOAD:
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		ret = sienna_cichlid_get_current_activity_percent(smu, sensor, (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_POWER:
		ret = sienna_cichlid_get_gpu_power(smu, (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_HOTSPOT_TEMP:
	case AMDGPU_PP_SENSOR_EDGE_TEMP:
	case AMDGPU_PP_SENSOR_MEM_TEMP:
		ret = sienna_cichlid_thermal_get_temperature(smu, sensor, (uint32_t *)data);
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
	default:
		ret = -EOPNOTSUPP;
		break;
	}
	mutex_unlock(&smu->sensor_lock);

	return ret;
}

static int sienna_cichlid_get_uclk_dpm_states(struct smu_context *smu, uint32_t *clocks_in_khz, uint32_t *num_states)
{
	uint32_t num_discrete_levels = 0;
	uint16_t *dpm_levels = NULL;
	uint16_t i = 0;
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *driver_ppt = NULL;

	if (!clocks_in_khz || !num_states || !table_context->driver_pptable)
		return -EINVAL;

	driver_ppt = table_context->driver_pptable;
	num_discrete_levels = driver_ppt->DpmDescriptor[PPCLK_UCLK].NumDiscreteLevels;
	dpm_levels = driver_ppt->FreqTableUclk;

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
	PPTable_t *pptable = smu->smu_table.driver_pptable;

	if (!range)
		return -EINVAL;

	memcpy(range, &smu11_thermal_policy[0], sizeof(struct smu_temperature_range));

	range->max = pptable->TemperatureLimit[TEMP_EDGE] *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->edge_emergency_max = (pptable->TemperatureLimit[TEMP_EDGE] + CTF_OFFSET_EDGE) *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->hotspot_crit_max = pptable->TemperatureLimit[TEMP_HOTSPOT] *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->hotspot_emergency_max = (pptable->TemperatureLimit[TEMP_HOTSPOT] + CTF_OFFSET_HOTSPOT) *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->mem_crit_max = pptable->TemperatureLimit[TEMP_MEM] *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->mem_emergency_max = (pptable->TemperatureLimit[TEMP_MEM] + CTF_OFFSET_MEM)*
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

static int sienna_cichlid_get_power_limit(struct smu_context *smu)
{
	struct smu_11_0_7_powerplay_table *powerplay_table =
		(struct smu_11_0_7_powerplay_table *)smu->smu_table.power_play_table;
	PPTable_t *pptable = smu->smu_table.driver_pptable;
	uint32_t power_limit, od_percent;

	if (smu_v11_0_get_current_power_limit(smu, &power_limit)) {
		/* the last hope to figure out the ppt limit */
		if (!pptable) {
			dev_err(smu->adev->dev, "Cannot get PPT limit due to pptable missing!");
			return -EINVAL;
		}
		power_limit =
			pptable->SocketPowerLimitAc[PPT_THROTTLER_PPT0];
	}
	smu->current_power_limit = power_limit;

	if (smu->od_enabled) {
		od_percent = le32_to_cpu(powerplay_table->overdrive_table.max[SMU_11_0_7_ODSETTING_POWERPERCENTAGE]);

		dev_dbg(smu->adev->dev, "ODSETTING_POWERPERCENTAGE: %d (default: %d)\n", od_percent, power_limit);

		power_limit *= (100 + od_percent);
		power_limit /= 100;
	}
	smu->max_power_limit = power_limit;

	return 0;
}

static int sienna_cichlid_update_pcie_parameters(struct smu_context *smu,
					 uint32_t pcie_gen_cap,
					 uint32_t pcie_width_cap)
{
	struct smu_11_0_dpm_context *dpm_context = smu->smu_dpm.dpm_context;
	PPTable_t *pptable = smu->smu_table.driver_pptable;
	uint32_t smu_pcie_arg;
	int ret, i;

	/* lclk dpm table setup */
	for (i = 0; i < MAX_PCIE_CONF; i++) {
		dpm_context->dpm_tables.pcie_table.pcie_gen[i] = pptable->PcieGenSpeed[i];
		dpm_context->dpm_tables.pcie_table.pcie_lane[i] = pptable->PcieLaneCount[i];
	}

	for (i = 0; i < NUM_LINK_LEVELS; i++) {
		smu_pcie_arg = (i << 16) |
			((pptable->PcieGenSpeed[i] <= pcie_gen_cap) ?
					(pptable->PcieGenSpeed[i] << 8) :
					(pcie_gen_cap << 8)) |
			((pptable->PcieLaneCount[i] <= pcie_width_cap) ?
					pptable->PcieLaneCount[i] :
					pcie_width_cap);

		ret = smu_cmn_send_smc_msg_with_param(smu,
					  SMU_MSG_OverridePcieParameters,
					  smu_pcie_arg,
					  NULL);

		if (ret)
			return ret;

		if (pptable->PcieGenSpeed[i] > pcie_gen_cap)
			dpm_context->dpm_tables.pcie_table.pcie_gen[i] = pcie_gen_cap;
		if (pptable->PcieLaneCount[i] > pcie_width_cap)
			dpm_context->dpm_tables.pcie_table.pcie_lane[i] = pcie_width_cap;
	}

	return 0;
}

static int sienna_cichlid_get_dpm_ultimate_freq(struct smu_context *smu,
				enum smu_clk_type clk_type,
				uint32_t *min, uint32_t *max)
{
	struct amdgpu_device *adev = smu->adev;
	int ret;

	if (clk_type == SMU_GFXCLK)
		amdgpu_gfx_off_ctrl(adev, false);
	ret = smu_v11_0_get_dpm_ultimate_freq(smu, clk_type, min, max);
	if (clk_type == SMU_GFXCLK)
		amdgpu_gfx_off_ctrl(adev, true);

	return ret;
}

static int sienna_cichlid_run_btc(struct smu_context *smu)
{
	return smu_cmn_send_smc_msg(smu, SMU_MSG_RunDcBtc, NULL);
}

static bool sienna_cichlid_is_baco_supported(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t val;

	if (amdgpu_sriov_vf(adev) || (!smu_v11_0_baco_is_support(smu)))
		return false;

	val = RREG32_SOC15(NBIO, 0, mmRCC_BIF_STRAP0);
	return (val & RCC_BIF_STRAP0__STRAP_PX_CAPABLE_MASK) ? true : false;
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

static void sienna_cichlid_dump_pptable(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *pptable = table_context->driver_pptable;
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
	dev_info(smu->adev->dev, "PaddingAPCC[0] = 0x%x\n", pptable->PaddingAPCC[0]);
	dev_info(smu->adev->dev, "PaddingAPCC[1] = 0x%x\n", pptable->PaddingAPCC[1]);
	dev_info(smu->adev->dev, "PaddingAPCC[2] = 0x%x\n", pptable->PaddingAPCC[2]);
	dev_info(smu->adev->dev, "PaddingAPCC[3] = 0x%x\n", pptable->PaddingAPCC[3]);

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

	dev_info(smu->adev->dev, "Paddingclks[0] = 0x%x\n",  pptable->Paddingclks[0]);
	dev_info(smu->adev->dev, "Paddingclks[1] = 0x%x\n",  pptable->Paddingclks[1]);
	dev_info(smu->adev->dev, "Paddingclks[2] = 0x%x\n",  pptable->Paddingclks[2]);
	dev_info(smu->adev->dev, "Paddingclks[3] = 0x%x\n",  pptable->Paddingclks[3]);
	dev_info(smu->adev->dev, "Paddingclks[4] = 0x%x\n",  pptable->Paddingclks[4]);
	dev_info(smu->adev->dev, "Paddingclks[5] = 0x%x\n",  pptable->Paddingclks[5]);
	dev_info(smu->adev->dev, "Paddingclks[6] = 0x%x\n",  pptable->Paddingclks[6]);
	dev_info(smu->adev->dev, "Paddingclks[7] = 0x%x\n",  pptable->Paddingclks[7]);
	dev_info(smu->adev->dev, "Paddingclks[8] = 0x%x\n",  pptable->Paddingclks[8]);
	dev_info(smu->adev->dev, "Paddingclks[9] = 0x%x\n",  pptable->Paddingclks[9]);
	dev_info(smu->adev->dev, "Paddingclks[10] = 0x%x\n", pptable->Paddingclks[10]);
	dev_info(smu->adev->dev, "Paddingclks[11] = 0x%x\n", pptable->Paddingclks[11]);
	dev_info(smu->adev->dev, "Paddingclks[12] = 0x%x\n", pptable->Paddingclks[12]);
	dev_info(smu->adev->dev, "Paddingclks[13] = 0x%x\n", pptable->Paddingclks[13]);
	dev_info(smu->adev->dev, "Paddingclks[14] = 0x%x\n", pptable->Paddingclks[14]);
	dev_info(smu->adev->dev, "Paddingclks[15] = 0x%x\n", pptable->Paddingclks[15]);

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
	dev_info(smu->adev->dev, "SkuReserved[8] = 0x%x\n", pptable->SkuReserved[8]);
	dev_info(smu->adev->dev, "SkuReserved[9] = 0x%x\n", pptable->SkuReserved[9]);
	dev_info(smu->adev->dev, "SkuReserved[10] = 0x%x\n", pptable->SkuReserved[10]);
	dev_info(smu->adev->dev, "SkuReserved[11] = 0x%x\n", pptable->SkuReserved[11]);
	dev_info(smu->adev->dev, "SkuReserved[12] = 0x%x\n", pptable->SkuReserved[12]);
	dev_info(smu->adev->dev, "SkuReserved[13] = 0x%x\n", pptable->SkuReserved[13]);

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

static void sienna_cichlid_fill_i2c_req(SwI2cRequest_t  *req, bool write,
				  uint8_t address, uint32_t numbytes,
				  uint8_t *data)
{
	int i;

	BUG_ON(numbytes > MAX_SW_I2C_COMMANDS);

	req->I2CcontrollerPort = 0;
	req->I2CSpeed = 2;
	req->SlaveAddress = address;
	req->NumCmds = numbytes;

	for (i = 0; i < numbytes; i++) {
		SwI2cCmd_t *cmd =  &req->SwI2cCmds[i];

		/* First 2 bytes are always write for lower 2b EEPROM address */
		if (i < 2)
			cmd->CmdConfig = CMDCONFIG_READWRITE_MASK;
		else
			cmd->CmdConfig = write ? CMDCONFIG_READWRITE_MASK : 0;


		/* Add RESTART for read  after address filled */
		cmd->CmdConfig |= (i == 2 && !write) ? CMDCONFIG_RESTART_MASK : 0;

		/* Add STOP in the end */
		cmd->CmdConfig |= (i == (numbytes - 1)) ? CMDCONFIG_STOP_MASK : 0;

		/* Fill with data regardless if read or write to simplify code */
		cmd->ReadWriteData = data[i];
	}
}

static int sienna_cichlid_i2c_read_data(struct i2c_adapter *control,
					       uint8_t address,
					       uint8_t *data,
					       uint32_t numbytes)
{
	uint32_t  i, ret = 0;
	SwI2cRequest_t req;
	struct amdgpu_device *adev = to_amdgpu_device(control);
	struct smu_table_context *smu_table = &adev->smu.smu_table;
	struct smu_table *table = &smu_table->driver_table;

	memset(&req, 0, sizeof(req));
	sienna_cichlid_fill_i2c_req(&req, false, address, numbytes, data);

	mutex_lock(&adev->smu.mutex);
	/* Now read data starting with that address */
	ret = smu_cmn_update_table(&adev->smu, SMU_TABLE_I2C_COMMANDS, 0, &req,
					true);
	mutex_unlock(&adev->smu.mutex);

	if (!ret) {
		SwI2cRequest_t *res = (SwI2cRequest_t *)table->cpu_addr;

		/* Assume SMU  fills res.SwI2cCmds[i].Data with read bytes */
		for (i = 0; i < numbytes; i++)
			data[i] = res->SwI2cCmds[i].ReadWriteData;

		dev_dbg(adev->dev, "sienna_cichlid_i2c_read_data, address = %x, bytes = %d, data :",
				  (uint16_t)address, numbytes);

		print_hex_dump(KERN_DEBUG, "data: ", DUMP_PREFIX_NONE,
			       8, 1, data, numbytes, false);
	} else
		dev_err(adev->dev, "sienna_cichlid_i2c_read_data - error occurred :%x", ret);

	return ret;
}

static int sienna_cichlid_i2c_write_data(struct i2c_adapter *control,
						uint8_t address,
						uint8_t *data,
						uint32_t numbytes)
{
	uint32_t ret;
	SwI2cRequest_t req;
	struct amdgpu_device *adev = to_amdgpu_device(control);

	memset(&req, 0, sizeof(req));
	sienna_cichlid_fill_i2c_req(&req, true, address, numbytes, data);

	mutex_lock(&adev->smu.mutex);
	ret = smu_cmn_update_table(&adev->smu, SMU_TABLE_I2C_COMMANDS, 0, &req, true);
	mutex_unlock(&adev->smu.mutex);

	if (!ret) {
		dev_dbg(adev->dev, "sienna_cichlid_i2c_write(), address = %x, bytes = %d , data: ",
					 (uint16_t)address, numbytes);

		print_hex_dump(KERN_DEBUG, "data: ", DUMP_PREFIX_NONE,
			       8, 1, data, numbytes, false);
		/*
		 * According to EEPROM spec there is a MAX of 10 ms required for
		 * EEPROM to flush internal RX buffer after STOP was issued at the
		 * end of write transaction. During this time the EEPROM will not be
		 * responsive to any more commands - so wait a bit more.
		 */
		msleep(10);

	} else
		dev_err(adev->dev, "sienna_cichlid_i2c_write- error occurred :%x", ret);

	return ret;
}

static int sienna_cichlid_i2c_xfer(struct i2c_adapter *i2c_adap,
			      struct i2c_msg *msgs, int num)
{
	uint32_t  i, j, ret, data_size, data_chunk_size, next_eeprom_addr = 0;
	uint8_t *data_ptr, data_chunk[MAX_SW_I2C_COMMANDS] = { 0 };

	for (i = 0; i < num; i++) {
		/*
		 * SMU interface allows at most MAX_SW_I2C_COMMANDS bytes of data at
		 * once and hence the data needs to be spliced into chunks and sent each
		 * chunk separately
		 */
		data_size = msgs[i].len - 2;
		data_chunk_size = MAX_SW_I2C_COMMANDS - 2;
		next_eeprom_addr = (msgs[i].buf[0] << 8 & 0xff00) | (msgs[i].buf[1] & 0xff);
		data_ptr = msgs[i].buf + 2;

		for (j = 0; j < data_size / data_chunk_size; j++) {
			/* Insert the EEPROM dest addess, bits 0-15 */
			data_chunk[0] = ((next_eeprom_addr >> 8) & 0xff);
			data_chunk[1] = (next_eeprom_addr & 0xff);

			if (msgs[i].flags & I2C_M_RD) {
				ret = sienna_cichlid_i2c_read_data(i2c_adap,
							     (uint8_t)msgs[i].addr,
							     data_chunk, MAX_SW_I2C_COMMANDS);

				memcpy(data_ptr, data_chunk + 2, data_chunk_size);
			} else {

				memcpy(data_chunk + 2, data_ptr, data_chunk_size);

				ret = sienna_cichlid_i2c_write_data(i2c_adap,
							      (uint8_t)msgs[i].addr,
							      data_chunk, MAX_SW_I2C_COMMANDS);
			}

			if (ret) {
				num = -EIO;
				goto fail;
			}

			next_eeprom_addr += data_chunk_size;
			data_ptr += data_chunk_size;
		}

		if (data_size % data_chunk_size) {
			data_chunk[0] = ((next_eeprom_addr >> 8) & 0xff);
			data_chunk[1] = (next_eeprom_addr & 0xff);

			if (msgs[i].flags & I2C_M_RD) {
				ret = sienna_cichlid_i2c_read_data(i2c_adap,
							     (uint8_t)msgs[i].addr,
							     data_chunk, (data_size % data_chunk_size) + 2);

				memcpy(data_ptr, data_chunk + 2, data_size % data_chunk_size);
			} else {
				memcpy(data_chunk + 2, data_ptr, data_size % data_chunk_size);

				ret = sienna_cichlid_i2c_write_data(i2c_adap,
							      (uint8_t)msgs[i].addr,
							      data_chunk, (data_size % data_chunk_size) + 2);
			}

			if (ret) {
				num = -EIO;
				goto fail;
			}
		}
	}

fail:
	return num;
}

static u32 sienna_cichlid_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}


static const struct i2c_algorithm sienna_cichlid_i2c_algo = {
	.master_xfer = sienna_cichlid_i2c_xfer,
	.functionality = sienna_cichlid_i2c_func,
};

static int sienna_cichlid_i2c_control_init(struct smu_context *smu, struct i2c_adapter *control)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);
	int res;

	control->owner = THIS_MODULE;
	control->class = I2C_CLASS_SPD;
	control->dev.parent = &adev->pdev->dev;
	control->algo = &sienna_cichlid_i2c_algo;
	snprintf(control->name, sizeof(control->name), "AMDGPU SMU");

	res = i2c_add_adapter(control);
	if (res)
		DRM_ERROR("Failed to register hw i2c, err: %d\n", res);

	return res;
}

static void sienna_cichlid_i2c_control_fini(struct smu_context *smu, struct i2c_adapter *control)
{
	i2c_del_adapter(control);
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
	.get_fan_speed_percent = sienna_cichlid_get_fan_speed_percent,
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
	.system_features_control = smu_v11_0_system_features_control,
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
	.set_fan_speed_percent = smu_v11_0_set_fan_speed_percent,
	.set_fan_speed_rpm = smu_v11_0_set_fan_speed_rpm,
	.set_xgmi_pstate = smu_v11_0_set_xgmi_pstate,
	.gfx_off_control = smu_v11_0_gfx_off_control,
	.register_irq_handler = smu_v11_0_register_irq_handler,
	.set_azalia_d3_pme = smu_v11_0_set_azalia_d3_pme,
	.get_max_sustainable_clocks_by_dc = smu_v11_0_get_max_sustainable_clocks_by_dc,
	.baco_is_support= sienna_cichlid_is_baco_supported,
	.baco_get_state = smu_v11_0_baco_get_state,
	.baco_set_state = smu_v11_0_baco_set_state,
	.baco_enter = smu_v11_0_baco_enter,
	.baco_exit = smu_v11_0_baco_exit,
	.mode1_reset_is_support = sienna_cichlid_is_mode1_reset_supported,
	.mode1_reset = smu_v11_0_mode1_reset,
	.get_dpm_ultimate_freq = sienna_cichlid_get_dpm_ultimate_freq,
	.set_soft_freq_limited_range = smu_v11_0_set_soft_freq_limited_range,
	.run_btc = sienna_cichlid_run_btc,
	.get_pp_feature_mask = smu_cmn_get_pp_feature_mask,
	.set_pp_feature_mask = smu_cmn_set_pp_feature_mask,
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
}
