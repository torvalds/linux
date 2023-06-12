/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#include "smu_v13_0.h"
#include "smu13_driver_if_v13_0_7.h"
#include "soc15_common.h"
#include "atom.h"
#include "smu_v13_0_7_ppt.h"
#include "smu_v13_0_7_pptable.h"
#include "smu_v13_0_7_ppsmc.h"
#include "nbio/nbio_4_3_0_offset.h"
#include "nbio/nbio_4_3_0_sh_mask.h"
#include "mp/mp_13_0_0_offset.h"
#include "mp/mp_13_0_0_sh_mask.h"

#include "asic_reg/mp/mp_13_0_0_sh_mask.h"
#include "smu_cmn.h"
#include "amdgpu_ras.h"

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
	FEATURE_MASK(FEATURE_DPM_GFXCLK_BIT)     | \
	FEATURE_MASK(FEATURE_DPM_UCLK_BIT)	 | \
	FEATURE_MASK(FEATURE_DPM_LINK_BIT)       | \
	FEATURE_MASK(FEATURE_DPM_SOCCLK_BIT)     | \
	FEATURE_MASK(FEATURE_DPM_FCLK_BIT)	 | \
	FEATURE_MASK(FEATURE_DPM_MP0CLK_BIT))

#define smnMP1_FIRMWARE_FLAGS_SMU_13_0_7   0x3b10028

#define MP0_MP1_DATA_REGION_SIZE_COMBOPPTABLE	0x4000

static struct cmn2asic_msg_mapping smu_v13_0_7_message_map[SMU_MSG_MAX_COUNT] = {
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
	MSG_MAP(ExitBaco,           PPSMC_MSG_ExitBaco,        			   0),
	MSG_MAP(SetSoftMinByFreq,		PPSMC_MSG_SetSoftMinByFreq,            1),
	MSG_MAP(SetSoftMaxByFreq,		PPSMC_MSG_SetSoftMaxByFreq,            1),
	MSG_MAP(SetHardMinByFreq,		PPSMC_MSG_SetHardMinByFreq,            1),
	MSG_MAP(SetHardMaxByFreq,		PPSMC_MSG_SetHardMaxByFreq,            0),
	MSG_MAP(GetMinDpmFreq,			PPSMC_MSG_GetMinDpmFreq,               1),
	MSG_MAP(GetMaxDpmFreq,			PPSMC_MSG_GetMaxDpmFreq,               1),
	MSG_MAP(GetDpmFreqByIndex,		PPSMC_MSG_GetDpmFreqByIndex,           1),
	MSG_MAP(PowerUpVcn,				PPSMC_MSG_PowerUpVcn,                  0),
	MSG_MAP(PowerDownVcn,			PPSMC_MSG_PowerDownVcn,                0),
	MSG_MAP(PowerUpJpeg,			PPSMC_MSG_PowerUpJpeg,                 0),
	MSG_MAP(PowerDownJpeg,			PPSMC_MSG_PowerDownJpeg,               0),
	MSG_MAP(GetDcModeMaxDpmFreq,		PPSMC_MSG_GetDcModeMaxDpmFreq,         1),
	MSG_MAP(OverridePcieParameters,		PPSMC_MSG_OverridePcieParameters,      0),
	MSG_MAP(ReenableAcDcInterrupt,		PPSMC_MSG_ReenableAcDcInterrupt,       0),
	MSG_MAP(AllowIHHostInterrupt,		PPSMC_MSG_AllowIHHostInterrupt,       0),
	MSG_MAP(DramLogSetDramAddrHigh,		PPSMC_MSG_DramLogSetDramAddrHigh,      0),
	MSG_MAP(DramLogSetDramAddrLow,		PPSMC_MSG_DramLogSetDramAddrLow,       0),
	MSG_MAP(DramLogSetDramSize,		PPSMC_MSG_DramLogSetDramSize,          0),
	MSG_MAP(AllowGfxOff,			PPSMC_MSG_AllowGfxOff,                 0),
	MSG_MAP(DisallowGfxOff,			PPSMC_MSG_DisallowGfxOff,              0),
	MSG_MAP(Mode1Reset,             PPSMC_MSG_Mode1Reset,                  0),
	MSG_MAP(PrepareMp1ForUnload,		PPSMC_MSG_PrepareMp1ForUnload,         0),
	MSG_MAP(SetMGpuFanBoostLimitRpm,	PPSMC_MSG_SetMGpuFanBoostLimitRpm,     0),
	MSG_MAP(DFCstateControl,		PPSMC_MSG_SetExternalClientDfCstateAllow, 0),
	MSG_MAP(ArmD3,				PPSMC_MSG_ArmD3,                       0),
	MSG_MAP(AllowGpo,			PPSMC_MSG_SetGpoAllow,           0),
	MSG_MAP(GetPptLimit,			PPSMC_MSG_GetPptLimit,                 0),
	MSG_MAP(NotifyPowerSource,		PPSMC_MSG_NotifyPowerSource,           0),
};

static struct cmn2asic_mapping smu_v13_0_7_clk_map[SMU_CLK_COUNT] = {
	CLK_MAP(GFXCLK,		PPCLK_GFXCLK),
	CLK_MAP(SCLK,		PPCLK_GFXCLK),
	CLK_MAP(SOCCLK,		PPCLK_SOCCLK),
	CLK_MAP(FCLK,		PPCLK_FCLK),
	CLK_MAP(UCLK,		PPCLK_UCLK),
	CLK_MAP(MCLK,		PPCLK_UCLK),
	CLK_MAP(VCLK,		PPCLK_VCLK_0),
	CLK_MAP(VCLK1,		PPCLK_VCLK_1),
	CLK_MAP(DCLK,		PPCLK_DCLK_0),
	CLK_MAP(DCLK1,		PPCLK_DCLK_1),
};

static struct cmn2asic_mapping smu_v13_0_7_feature_mask_map[SMU_FEATURE_COUNT] = {
	FEA_MAP(FW_DATA_READ),
	FEA_MAP(DPM_GFXCLK),
	FEA_MAP(DPM_GFX_POWER_OPTIMIZER),
	FEA_MAP(DPM_UCLK),
	FEA_MAP(DPM_FCLK),
	FEA_MAP(DPM_SOCCLK),
	FEA_MAP(DPM_MP0CLK),
	FEA_MAP(DPM_LINK),
	FEA_MAP(DPM_DCN),
	FEA_MAP(VMEMP_SCALING),
	FEA_MAP(VDDIO_MEM_SCALING),
	FEA_MAP(DS_GFXCLK),
	FEA_MAP(DS_SOCCLK),
	FEA_MAP(DS_FCLK),
	FEA_MAP(DS_LCLK),
	FEA_MAP(DS_DCFCLK),
	FEA_MAP(DS_UCLK),
	FEA_MAP(GFX_ULV),
	FEA_MAP(FW_DSTATE),
	FEA_MAP(GFXOFF),
	FEA_MAP(BACO),
	FEA_MAP(MM_DPM),
	FEA_MAP(SOC_MPCLK_DS),
	FEA_MAP(BACO_MPCLK_DS),
	FEA_MAP(THROTTLERS),
	FEA_MAP(SMARTSHIFT),
	FEA_MAP(GTHR),
	FEA_MAP(ACDC),
	FEA_MAP(VR0HOT),
	FEA_MAP(FW_CTF),
	FEA_MAP(FAN_CONTROL),
	FEA_MAP(GFX_DCS),
	FEA_MAP(GFX_READ_MARGIN),
	FEA_MAP(LED_DISPLAY),
	FEA_MAP(GFXCLK_SPREAD_SPECTRUM),
	FEA_MAP(OUT_OF_BAND_MONITOR),
	FEA_MAP(OPTIMIZED_VMIN),
	FEA_MAP(GFX_IMU),
	FEA_MAP(BOOT_TIME_CAL),
	FEA_MAP(GFX_PCC_DFLL),
	FEA_MAP(SOC_CG),
	FEA_MAP(DF_CSTATE),
	FEA_MAP(GFX_EDC),
	FEA_MAP(BOOT_POWER_OPT),
	FEA_MAP(CLOCK_POWER_DOWN_BYPASS),
	FEA_MAP(DS_VCN),
	FEA_MAP(BACO_CG),
	FEA_MAP(MEM_TEMP_READ),
	FEA_MAP(ATHUB_MMHUB_PG),
	FEA_MAP(SOC_PCC),
	[SMU_FEATURE_DPM_VCLK_BIT] = {1, FEATURE_MM_DPM_BIT},
	[SMU_FEATURE_DPM_DCLK_BIT] = {1, FEATURE_MM_DPM_BIT},
	[SMU_FEATURE_PPT_BIT] = {1, FEATURE_THROTTLERS_BIT},
};

static struct cmn2asic_mapping smu_v13_0_7_table_map[SMU_TABLE_COUNT] = {
	TAB_MAP(PPTABLE),
	TAB_MAP(WATERMARKS),
	TAB_MAP(AVFS_PSM_DEBUG),
	TAB_MAP(PMSTATUSLOG),
	TAB_MAP(SMU_METRICS),
	TAB_MAP(DRIVER_SMU_CONFIG),
	TAB_MAP(ACTIVITY_MONITOR_COEFF),
	[SMU_TABLE_COMBO_PPTABLE] = {1, TABLE_COMBO_PPTABLE},
};

static struct cmn2asic_mapping smu_v13_0_7_pwr_src_map[SMU_POWER_SOURCE_COUNT] = {
	PWR_MAP(AC),
	PWR_MAP(DC),
};

static struct cmn2asic_mapping smu_v13_0_7_workload_map[PP_SMC_POWER_PROFILE_COUNT] = {
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT,	WORKLOAD_PPLIB_DEFAULT_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_FULLSCREEN3D,		WORKLOAD_PPLIB_FULL_SCREEN_3D_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_POWERSAVING,		WORKLOAD_PPLIB_POWER_SAVING_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_VIDEO,		WORKLOAD_PPLIB_VIDEO_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_VR,			WORKLOAD_PPLIB_VR_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_COMPUTE,		WORKLOAD_PPLIB_COMPUTE_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_CUSTOM,		WORKLOAD_PPLIB_CUSTOM_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_WINDOW3D,		WORKLOAD_PPLIB_WINDOW_3D_BIT),
};

static const uint8_t smu_v13_0_7_throttler_map[] = {
	[THROTTLER_PPT0_BIT]		= (SMU_THROTTLER_PPT0_BIT),
	[THROTTLER_PPT1_BIT]		= (SMU_THROTTLER_PPT1_BIT),
	[THROTTLER_PPT2_BIT]		= (SMU_THROTTLER_PPT2_BIT),
	[THROTTLER_PPT3_BIT]		= (SMU_THROTTLER_PPT3_BIT),
	[THROTTLER_TDC_GFX_BIT]		= (SMU_THROTTLER_TDC_GFX_BIT),
	[THROTTLER_TDC_SOC_BIT]		= (SMU_THROTTLER_TDC_SOC_BIT),
	[THROTTLER_TEMP_EDGE_BIT]	= (SMU_THROTTLER_TEMP_EDGE_BIT),
	[THROTTLER_TEMP_HOTSPOT_BIT]	= (SMU_THROTTLER_TEMP_HOTSPOT_BIT),
	[THROTTLER_TEMP_MEM_BIT]	= (SMU_THROTTLER_TEMP_MEM_BIT),
	[THROTTLER_TEMP_VR_GFX_BIT]	= (SMU_THROTTLER_TEMP_VR_GFX_BIT),
	[THROTTLER_TEMP_VR_SOC_BIT]	= (SMU_THROTTLER_TEMP_VR_SOC_BIT),
	[THROTTLER_TEMP_VR_MEM0_BIT]	= (SMU_THROTTLER_TEMP_VR_MEM0_BIT),
	[THROTTLER_TEMP_VR_MEM1_BIT]	= (SMU_THROTTLER_TEMP_VR_MEM1_BIT),
	[THROTTLER_TEMP_LIQUID0_BIT]	= (SMU_THROTTLER_TEMP_LIQUID0_BIT),
	[THROTTLER_TEMP_LIQUID1_BIT]	= (SMU_THROTTLER_TEMP_LIQUID1_BIT),
	[THROTTLER_GFX_APCC_PLUS_BIT]	= (SMU_THROTTLER_APCC_BIT),
	[THROTTLER_FIT_BIT]		= (SMU_THROTTLER_FIT_BIT),
};

static int
smu_v13_0_7_get_allowed_feature_mask(struct smu_context *smu,
				  uint32_t *feature_mask, uint32_t num)
{
	struct amdgpu_device *adev = smu->adev;

	if (num > 2)
		return -EINVAL;

	memset(feature_mask, 0, sizeof(uint32_t) * num);

	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_FW_DATA_READ_BIT);

	if (adev->pm.pp_feature & PP_SCLK_DPM_MASK) {
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_GFXCLK_BIT);
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_GFX_IMU_BIT);
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_GFX_POWER_OPTIMIZER_BIT);
	}

	if (adev->pm.pp_feature & PP_GFXOFF_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_GFXOFF_BIT);

	if (adev->pm.pp_feature & PP_MCLK_DPM_MASK) {
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_UCLK_BIT);
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_FCLK_BIT);
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_VMEMP_SCALING_BIT);
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_VDDIO_MEM_SCALING_BIT);
	}

	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_SOCCLK_BIT);

	if (adev->pm.pp_feature & PP_PCIE_DPM_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_LINK_BIT);

	if (adev->pm.pp_feature & PP_SCLK_DEEP_SLEEP_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DS_GFXCLK_BIT);

	if (adev->pm.pp_feature & PP_ULV_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_GFX_ULV_BIT);

	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DS_LCLK_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_MP0CLK_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_MM_DPM_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DS_VCN_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DS_FCLK_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DF_CSTATE_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_THROTTLERS_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_VR0HOT_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_FW_CTF_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_FAN_CONTROL_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DS_SOCCLK_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_GFXCLK_SPREAD_SPECTRUM_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_MEM_TEMP_READ_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_FW_DSTATE_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_SOC_MPCLK_DS_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_BACO_MPCLK_DS_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_GFX_PCC_DFLL_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_SOC_CG_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_BACO_BIT);

	if (adev->pm.pp_feature & PP_DCEFCLK_DPM_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_DCN_BIT);

	if ((adev->pg_flags & AMD_PG_SUPPORT_ATHUB) &&
	    (adev->pg_flags & AMD_PG_SUPPORT_MMHUB))
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_ATHUB_MMHUB_PG_BIT);

	return 0;
}

static int smu_v13_0_7_check_powerplay_table(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_13_0_7_powerplay_table *powerplay_table =
		table_context->power_play_table;
	struct smu_baco_context *smu_baco = &smu->smu_baco;
	PPTable_t *smc_pptable = table_context->driver_pptable;
	BoardTable_t *BoardTable = &smc_pptable->BoardTable;

	if (powerplay_table->platform_caps & SMU_13_0_7_PP_PLATFORM_CAP_HARDWAREDC)
		smu->dc_controlled_by_gpio = true;

	if (powerplay_table->platform_caps & SMU_13_0_7_PP_PLATFORM_CAP_BACO ||
	    powerplay_table->platform_caps & SMU_13_0_7_PP_PLATFORM_CAP_MACO)
		smu_baco->platform_support = true;

	if (smu_baco->platform_support && (BoardTable->HsrEnabled || BoardTable->VddqOffEnabled))
		smu_baco->maco_support = true;

	table_context->thermal_controller_type =
		powerplay_table->thermal_controller_type;

	/*
	 * Instead of having its own buffer space and get overdrive_table copied,
	 * smu->od_settings just points to the actual overdrive_table
	 */
	smu->od_settings = &powerplay_table->overdrive_table;

	return 0;
}

static int smu_v13_0_7_store_powerplay_table(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_13_0_7_powerplay_table *powerplay_table =
		table_context->power_play_table;
	struct amdgpu_device *adev = smu->adev;

	if (adev->pdev->device == 0x51)
		powerplay_table->smc_pptable.SkuTable.DebugOverrides |= 0x00000080;

	memcpy(table_context->driver_pptable, &powerplay_table->smc_pptable,
	       sizeof(PPTable_t));

	return 0;
}

static int smu_v13_0_7_check_fw_status(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t mp1_fw_flags;

	mp1_fw_flags = RREG32_PCIE(MP1_Public |
				   (smnMP1_FIRMWARE_FLAGS_SMU_13_0_7 & 0xffffffff));

	if ((mp1_fw_flags & MP1_FIRMWARE_FLAGS__INTERRUPTS_ENABLED_MASK) >>
			MP1_FIRMWARE_FLAGS__INTERRUPTS_ENABLED__SHIFT)
		return 0;

	return -EIO;
}

#ifndef atom_smc_dpm_info_table_13_0_7
struct atom_smc_dpm_info_table_13_0_7
{
	struct atom_common_table_header table_header;
	BoardTable_t BoardTable;
};
#endif

static int smu_v13_0_7_append_powerplay_table(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;

	PPTable_t *smc_pptable = table_context->driver_pptable;

	struct atom_smc_dpm_info_table_13_0_7 *smc_dpm_table;

	BoardTable_t *BoardTable = &smc_pptable->BoardTable;

	int index, ret;

	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
	smc_dpm_info);

	ret = amdgpu_atombios_get_data_table(smu->adev, index, NULL, NULL, NULL,
			(uint8_t **)&smc_dpm_table);
	if (ret)
		return ret;

	memcpy(BoardTable, &smc_dpm_table->BoardTable, sizeof(BoardTable_t));

	return 0;
}

static int smu_v13_0_7_get_pptable_from_pmfw(struct smu_context *smu,
					     void **table,
					     uint32_t *size)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	void *combo_pptable = smu_table->combo_pptable;
	int ret = 0;

	ret = smu_cmn_get_combo_pptable(smu);
	if (ret)
		return ret;

	*table = combo_pptable;
	*size = sizeof(struct smu_13_0_7_powerplay_table);

	return 0;
}

static int smu_v13_0_7_setup_pptable(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;

	/*
	 * With SCPM enabled, the pptable used will be signed. It cannot
	 * be used directly by driver. To get the raw pptable, we need to
	 * rely on the combo pptable(and its revelant SMU message).
	 */
	ret = smu_v13_0_7_get_pptable_from_pmfw(smu,
						&smu_table->power_play_table,
						&smu_table->power_play_table_size);
	if (ret)
		return ret;

	ret = smu_v13_0_7_store_powerplay_table(smu);
	if (ret)
		return ret;

	/*
	 * With SCPM enabled, the operation below will be handled
	 * by PSP. Driver involvment is unnecessary and useless.
	 */
	if (!adev->scpm_enabled) {
		ret = smu_v13_0_7_append_powerplay_table(smu);
		if (ret)
			return ret;
	}

	ret = smu_v13_0_7_check_powerplay_table(smu);
	if (ret)
		return ret;

	return ret;
}

static int smu_v13_0_7_tables_init(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = smu_table->tables;

	SMU_TABLE_INIT(tables, SMU_TABLE_PPTABLE, sizeof(PPTable_t),
		PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);

	SMU_TABLE_INIT(tables, SMU_TABLE_WATERMARKS, sizeof(Watermarks_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_SMU_METRICS, sizeof(SmuMetricsExternal_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_I2C_COMMANDS, sizeof(SwI2cRequest_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_OVERDRIVE, sizeof(OverDriveTable_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_PMSTATUSLOG, SMU13_TOOL_SIZE,
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_ACTIVITY_MONITOR_COEFF,
		       sizeof(DpmActivityMonitorCoeffIntExternal_t), PAGE_SIZE,
	               AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_COMBO_PPTABLE, MP0_MP1_DATA_REGION_SIZE_COMBOPPTABLE,
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

	return 0;

err2_out:
	kfree(smu_table->gpu_metrics_table);
err1_out:
	kfree(smu_table->metrics_table);
err0_out:
	return -ENOMEM;
}

static int smu_v13_0_7_allocate_dpm_context(struct smu_context *smu)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;

	smu_dpm->dpm_context = kzalloc(sizeof(struct smu_13_0_dpm_context),
				       GFP_KERNEL);
	if (!smu_dpm->dpm_context)
		return -ENOMEM;

	smu_dpm->dpm_context_size = sizeof(struct smu_13_0_dpm_context);

	return 0;
}

static int smu_v13_0_7_init_smc_tables(struct smu_context *smu)
{
	int ret = 0;

	ret = smu_v13_0_7_tables_init(smu);
	if (ret)
		return ret;

	ret = smu_v13_0_7_allocate_dpm_context(smu);
	if (ret)
		return ret;

	return smu_v13_0_init_smc_tables(smu);
}

static int smu_v13_0_7_set_default_dpm_table(struct smu_context *smu)
{
	struct smu_13_0_dpm_context *dpm_context = smu->smu_dpm.dpm_context;
	PPTable_t *driver_ppt = smu->smu_table.driver_pptable;
	SkuTable_t *skutable = &driver_ppt->SkuTable;
	struct smu_13_0_dpm_table *dpm_table;
	struct smu_13_0_pcie_table *pcie_table;
	uint32_t link_level;
	int ret = 0;

	/* socclk dpm table setup */
	dpm_table = &dpm_context->dpm_tables.soc_table;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_SOCCLK_BIT)) {
		ret = smu_v13_0_set_single_dpm_table(smu,
						     SMU_SOCCLK,
						     dpm_table);
		if (ret)
			return ret;
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
		ret = smu_v13_0_set_single_dpm_table(smu,
						     SMU_GFXCLK,
						     dpm_table);
		if (ret)
			return ret;

		if (skutable->DriverReportedClocks.GameClockAc &&
			(dpm_table->dpm_levels[dpm_table->count - 1].value >
			skutable->DriverReportedClocks.GameClockAc)) {
			dpm_table->dpm_levels[dpm_table->count - 1].value =
				skutable->DriverReportedClocks.GameClockAc;
			dpm_table->max = skutable->DriverReportedClocks.GameClockAc;
		}
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
		ret = smu_v13_0_set_single_dpm_table(smu,
						     SMU_UCLK,
						     dpm_table);
		if (ret)
			return ret;
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
		ret = smu_v13_0_set_single_dpm_table(smu,
						     SMU_FCLK,
						     dpm_table);
		if (ret)
			return ret;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.fclk / 100;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->min = dpm_table->dpm_levels[0].value;
		dpm_table->max = dpm_table->dpm_levels[0].value;
	}

	/* vclk dpm table setup */
	dpm_table = &dpm_context->dpm_tables.vclk_table;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_VCLK_BIT)) {
		ret = smu_v13_0_set_single_dpm_table(smu,
						     SMU_VCLK,
						     dpm_table);
		if (ret)
			return ret;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.vclk / 100;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->min = dpm_table->dpm_levels[0].value;
		dpm_table->max = dpm_table->dpm_levels[0].value;
	}

	/* dclk dpm table setup */
	dpm_table = &dpm_context->dpm_tables.dclk_table;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_DCLK_BIT)) {
		ret = smu_v13_0_set_single_dpm_table(smu,
						     SMU_DCLK,
						     dpm_table);
		if (ret)
			return ret;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.dclk / 100;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->min = dpm_table->dpm_levels[0].value;
		dpm_table->max = dpm_table->dpm_levels[0].value;
	}

	/* lclk dpm table setup */
	pcie_table = &dpm_context->dpm_tables.pcie_table;
	pcie_table->num_of_link_levels = 0;
	for (link_level = 0; link_level < NUM_LINK_LEVELS; link_level++) {
		if (!skutable->PcieGenSpeed[link_level] &&
		    !skutable->PcieLaneCount[link_level] &&
		    !skutable->LclkFreq[link_level])
			continue;

		pcie_table->pcie_gen[pcie_table->num_of_link_levels] =
					skutable->PcieGenSpeed[link_level];
		pcie_table->pcie_lane[pcie_table->num_of_link_levels] =
					skutable->PcieLaneCount[link_level];
		pcie_table->clk_freq[pcie_table->num_of_link_levels] =
					skutable->LclkFreq[link_level];
		pcie_table->num_of_link_levels++;
	}

	return 0;
}

static bool smu_v13_0_7_is_dpm_running(struct smu_context *smu)
{
	int ret = 0;
	uint64_t feature_enabled;

	ret = smu_cmn_get_enabled_mask(smu, &feature_enabled);
	if (ret)
		return false;

	return !!(feature_enabled & SMC_DPM_FEATURE);
}

static void smu_v13_0_7_dump_pptable(struct smu_context *smu)
{
       struct smu_table_context *table_context = &smu->smu_table;
       PPTable_t *pptable = table_context->driver_pptable;
       SkuTable_t *skutable = &pptable->SkuTable;

       dev_info(smu->adev->dev, "Dumped PPTable:\n");

       dev_info(smu->adev->dev, "Version = 0x%08x\n", skutable->Version);
       dev_info(smu->adev->dev, "FeaturesToRun[0] = 0x%08x\n", skutable->FeaturesToRun[0]);
       dev_info(smu->adev->dev, "FeaturesToRun[1] = 0x%08x\n", skutable->FeaturesToRun[1]);
}

static uint32_t smu_v13_0_7_get_throttler_status(SmuMetrics_t *metrics)
{
	uint32_t throttler_status = 0;
	int i;

	for (i = 0; i < THROTTLER_COUNT; i++)
		throttler_status |=
			(metrics->ThrottlingPercentage[i] ? 1U << i : 0);

	return throttler_status;
}

#define SMU_13_0_7_BUSY_THRESHOLD	15
static int smu_v13_0_7_get_smu_metrics_data(struct smu_context *smu,
					    MetricsMember_t member,
					    uint32_t *value)
{
	struct smu_table_context *smu_table= &smu->smu_table;
	SmuMetrics_t *metrics =
		&(((SmuMetricsExternal_t *)(smu_table->metrics_table))->SmuMetrics);
	int ret = 0;

	ret = smu_cmn_get_metrics_table(smu,
					NULL,
					false);
	if (ret)
		return ret;

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
	case METRICS_CURR_FCLK:
		*value = metrics->CurrClock[PPCLK_FCLK];
		break;
	case METRICS_AVERAGE_GFXCLK:
		*value = metrics->AverageGfxclkFrequencyPreDs;
		break;
	case METRICS_AVERAGE_FCLK:
		if (metrics->AverageUclkActivity <= SMU_13_0_7_BUSY_THRESHOLD)
			*value = metrics->AverageFclkFrequencyPostDs;
		else
			*value = metrics->AverageFclkFrequencyPreDs;
		break;
	case METRICS_AVERAGE_UCLK:
		if (metrics->AverageUclkActivity <= SMU_13_0_7_BUSY_THRESHOLD)
			*value = metrics->AverageMemclkFrequencyPostDs;
		else
			*value = metrics->AverageMemclkFrequencyPreDs;
		break;
	case METRICS_AVERAGE_VCLK:
		*value = metrics->AverageVclk0Frequency;
		break;
	case METRICS_AVERAGE_DCLK:
		*value = metrics->AverageDclk0Frequency;
		break;
	case METRICS_AVERAGE_VCLK1:
		*value = metrics->AverageVclk1Frequency;
		break;
	case METRICS_AVERAGE_DCLK1:
		*value = metrics->AverageDclk1Frequency;
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
		*value = metrics->AvgTemperature[TEMP_EDGE] *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_TEMPERATURE_HOTSPOT:
		*value = metrics->AvgTemperature[TEMP_HOTSPOT] *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_TEMPERATURE_MEM:
		*value = metrics->AvgTemperature[TEMP_MEM] *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_TEMPERATURE_VRGFX:
		*value = metrics->AvgTemperature[TEMP_VR_GFX] *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_TEMPERATURE_VRSOC:
		*value = metrics->AvgTemperature[TEMP_VR_SOC] *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_THROTTLER_STATUS:
		*value = smu_v13_0_7_get_throttler_status(metrics);
		break;
	case METRICS_CURR_FANSPEED:
		*value = metrics->AvgFanRpm;
		break;
	case METRICS_CURR_FANPWM:
		*value = metrics->AvgFanPwm;
		break;
	case METRICS_VOLTAGE_VDDGFX:
		*value = metrics->AvgVoltage[SVI_PLANE_GFX];
		break;
	case METRICS_PCIE_RATE:
		*value = metrics->PcieRate;
		break;
	case METRICS_PCIE_WIDTH:
		*value = metrics->PcieWidth;
		break;
	default:
		*value = UINT_MAX;
		break;
	}

	return ret;
}

static int smu_v13_0_7_get_dpm_ultimate_freq(struct smu_context *smu,
					     enum smu_clk_type clk_type,
					     uint32_t *min,
					     uint32_t *max)
{
	struct smu_13_0_dpm_context *dpm_context =
		smu->smu_dpm.dpm_context;
	struct smu_13_0_dpm_table *dpm_table;

	switch (clk_type) {
	case SMU_MCLK:
	case SMU_UCLK:
		/* uclk dpm table */
		dpm_table = &dpm_context->dpm_tables.uclk_table;
		break;
	case SMU_GFXCLK:
	case SMU_SCLK:
		/* gfxclk dpm table */
		dpm_table = &dpm_context->dpm_tables.gfx_table;
		break;
	case SMU_SOCCLK:
		/* socclk dpm table */
		dpm_table = &dpm_context->dpm_tables.soc_table;
		break;
	case SMU_FCLK:
		/* fclk dpm table */
		dpm_table = &dpm_context->dpm_tables.fclk_table;
		break;
	case SMU_VCLK:
	case SMU_VCLK1:
		/* vclk dpm table */
		dpm_table = &dpm_context->dpm_tables.vclk_table;
		break;
	case SMU_DCLK:
	case SMU_DCLK1:
		/* dclk dpm table */
		dpm_table = &dpm_context->dpm_tables.dclk_table;
		break;
	default:
		dev_err(smu->adev->dev, "Unsupported clock type!\n");
		return -EINVAL;
	}

	if (min)
		*min = dpm_table->min;
	if (max)
		*max = dpm_table->max;

	return 0;
}

static int smu_v13_0_7_read_sensor(struct smu_context *smu,
				   enum amd_pp_sensors sensor,
				   void *data,
				   uint32_t *size)
{
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *smc_pptable = table_context->driver_pptable;
	int ret = 0;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_MAX_FAN_RPM:
		*(uint16_t *)data = smc_pptable->SkuTable.FanMaximumRpm;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_MEM_LOAD:
		ret = smu_v13_0_7_get_smu_metrics_data(smu,
						       METRICS_AVERAGE_MEMACTIVITY,
						       (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		ret = smu_v13_0_7_get_smu_metrics_data(smu,
						       METRICS_AVERAGE_GFXACTIVITY,
						       (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_POWER:
		ret = smu_v13_0_7_get_smu_metrics_data(smu,
						       METRICS_AVERAGE_SOCKETPOWER,
						       (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_HOTSPOT_TEMP:
		ret = smu_v13_0_7_get_smu_metrics_data(smu,
						       METRICS_TEMPERATURE_HOTSPOT,
						       (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_EDGE_TEMP:
		ret = smu_v13_0_7_get_smu_metrics_data(smu,
						       METRICS_TEMPERATURE_EDGE,
						       (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_MEM_TEMP:
		ret = smu_v13_0_7_get_smu_metrics_data(smu,
						       METRICS_TEMPERATURE_MEM,
						       (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GFX_MCLK:
		ret = smu_v13_0_7_get_smu_metrics_data(smu,
						       METRICS_AVERAGE_UCLK,
						       (uint32_t *)data);
		*(uint32_t *)data *= 100;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GFX_SCLK:
		ret = smu_v13_0_7_get_smu_metrics_data(smu,
						       METRICS_AVERAGE_GFXCLK,
						       (uint32_t *)data);
		*(uint32_t *)data *= 100;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_VDDGFX:
		ret = smu_v13_0_7_get_smu_metrics_data(smu,
						       METRICS_VOLTAGE_VDDGFX,
						       (uint32_t *)data);
		*size = 4;
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static int smu_v13_0_7_get_current_clk_freq_by_table(struct smu_context *smu,
						     enum smu_clk_type clk_type,
						     uint32_t *value)
{
	MetricsMember_t member_type;
	int clk_id = 0;

	clk_id = smu_cmn_to_asic_specific_index(smu,
						CMN2ASIC_MAPPING_CLK,
						clk_type);
	if (clk_id < 0)
		return -EINVAL;

	switch (clk_id) {
	case PPCLK_GFXCLK:
		member_type = METRICS_AVERAGE_GFXCLK;
		break;
	case PPCLK_UCLK:
		member_type = METRICS_CURR_UCLK;
		break;
	case PPCLK_FCLK:
		member_type = METRICS_CURR_FCLK;
		break;
	case PPCLK_SOCCLK:
		member_type = METRICS_CURR_SOCCLK;
		break;
	case PPCLK_VCLK_0:
		member_type = METRICS_CURR_VCLK;
		break;
	case PPCLK_DCLK_0:
		member_type = METRICS_CURR_DCLK;
		break;
	case PPCLK_VCLK_1:
		member_type = METRICS_CURR_VCLK1;
		break;
	case PPCLK_DCLK_1:
		member_type = METRICS_CURR_DCLK1;
		break;
	default:
		return -EINVAL;
	}

	return smu_v13_0_7_get_smu_metrics_data(smu,
						member_type,
						value);
}

static int smu_v13_0_7_print_clk_levels(struct smu_context *smu,
					enum smu_clk_type clk_type,
					char *buf)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct smu_13_0_dpm_context *dpm_context = smu_dpm->dpm_context;
	struct smu_13_0_dpm_table *single_dpm_table;
	struct smu_13_0_pcie_table *pcie_table;
	uint32_t gen_speed, lane_width;
	int i, curr_freq, size = 0;
	int ret = 0;

	smu_cmn_get_sysfs_buf(&buf, &size);

	if (amdgpu_ras_intr_triggered()) {
		size += sysfs_emit_at(buf, size, "unavailable\n");
		return size;
	}

	switch (clk_type) {
	case SMU_SCLK:
		single_dpm_table = &(dpm_context->dpm_tables.gfx_table);
		break;
	case SMU_MCLK:
		single_dpm_table = &(dpm_context->dpm_tables.uclk_table);
		break;
	case SMU_SOCCLK:
		single_dpm_table = &(dpm_context->dpm_tables.soc_table);
		break;
	case SMU_FCLK:
		single_dpm_table = &(dpm_context->dpm_tables.fclk_table);
		break;
	case SMU_VCLK:
	case SMU_VCLK1:
		single_dpm_table = &(dpm_context->dpm_tables.vclk_table);
		break;
	case SMU_DCLK:
	case SMU_DCLK1:
		single_dpm_table = &(dpm_context->dpm_tables.dclk_table);
		break;
	default:
		break;
	}

	switch (clk_type) {
	case SMU_SCLK:
	case SMU_MCLK:
	case SMU_SOCCLK:
	case SMU_FCLK:
	case SMU_VCLK:
	case SMU_VCLK1:
	case SMU_DCLK:
	case SMU_DCLK1:
		ret = smu_v13_0_7_get_current_clk_freq_by_table(smu, clk_type, &curr_freq);
		if (ret) {
			dev_err(smu->adev->dev, "Failed to get current clock freq!");
			return ret;
		}

		if (single_dpm_table->is_fine_grained) {
			/*
			 * For fine grained dpms, there are only two dpm levels:
			 *   - level 0 -> min clock freq
			 *   - level 1 -> max clock freq
			 * And the current clock frequency can be any value between them.
			 * So, if the current clock frequency is not at level 0 or level 1,
			 * we will fake it as three dpm levels:
			 *   - level 0 -> min clock freq
			 *   - level 1 -> current actual clock freq
			 *   - level 2 -> max clock freq
			 */
			if ((single_dpm_table->dpm_levels[0].value != curr_freq) &&
			     (single_dpm_table->dpm_levels[1].value != curr_freq)) {
				size += sysfs_emit_at(buf, size, "0: %uMhz\n",
						single_dpm_table->dpm_levels[0].value);
				size += sysfs_emit_at(buf, size, "1: %uMhz *\n",
						curr_freq);
				size += sysfs_emit_at(buf, size, "2: %uMhz\n",
						single_dpm_table->dpm_levels[1].value);
			} else {
				size += sysfs_emit_at(buf, size, "0: %uMhz %s\n",
						single_dpm_table->dpm_levels[0].value,
						single_dpm_table->dpm_levels[0].value == curr_freq ? "*" : "");
				size += sysfs_emit_at(buf, size, "1: %uMhz %s\n",
						single_dpm_table->dpm_levels[1].value,
						single_dpm_table->dpm_levels[1].value == curr_freq ? "*" : "");
			}
		} else {
			for (i = 0; i < single_dpm_table->count; i++)
				size += sysfs_emit_at(buf, size, "%d: %uMhz %s\n",
						i, single_dpm_table->dpm_levels[i].value,
						single_dpm_table->dpm_levels[i].value == curr_freq ? "*" : "");
		}
		break;
	case SMU_PCIE:
		ret = smu_v13_0_7_get_smu_metrics_data(smu,
						       METRICS_PCIE_RATE,
						       &gen_speed);
		if (ret)
			return ret;

		ret = smu_v13_0_7_get_smu_metrics_data(smu,
						       METRICS_PCIE_WIDTH,
						       &lane_width);
		if (ret)
			return ret;

		pcie_table = &(dpm_context->dpm_tables.pcie_table);
		for (i = 0; i < pcie_table->num_of_link_levels; i++)
			size += sysfs_emit_at(buf, size, "%d: %s %s %dMhz %s\n", i,
					(pcie_table->pcie_gen[i] == 0) ? "2.5GT/s," :
					(pcie_table->pcie_gen[i] == 1) ? "5.0GT/s," :
					(pcie_table->pcie_gen[i] == 2) ? "8.0GT/s," :
					(pcie_table->pcie_gen[i] == 3) ? "16.0GT/s," : "",
					(pcie_table->pcie_lane[i] == 1) ? "x1" :
					(pcie_table->pcie_lane[i] == 2) ? "x2" :
					(pcie_table->pcie_lane[i] == 3) ? "x4" :
					(pcie_table->pcie_lane[i] == 4) ? "x8" :
					(pcie_table->pcie_lane[i] == 5) ? "x12" :
					(pcie_table->pcie_lane[i] == 6) ? "x16" : "",
					pcie_table->clk_freq[i],
					(gen_speed == DECODE_GEN_SPEED(pcie_table->pcie_gen[i])) &&
					(lane_width == DECODE_LANE_WIDTH(pcie_table->pcie_lane[i])) ?
					"*" : "");
		break;

	default:
		break;
	}

	return size;
}

static int smu_v13_0_7_force_clk_levels(struct smu_context *smu,
					enum smu_clk_type clk_type,
					uint32_t mask)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct smu_13_0_dpm_context *dpm_context = smu_dpm->dpm_context;
	struct smu_13_0_dpm_table *single_dpm_table;
	uint32_t soft_min_level, soft_max_level;
	uint32_t min_freq, max_freq;
	int ret = 0;

	soft_min_level = mask ? (ffs(mask) - 1) : 0;
	soft_max_level = mask ? (fls(mask) - 1) : 0;

	switch (clk_type) {
	case SMU_GFXCLK:
	case SMU_SCLK:
		single_dpm_table = &(dpm_context->dpm_tables.gfx_table);
		break;
	case SMU_MCLK:
	case SMU_UCLK:
		single_dpm_table = &(dpm_context->dpm_tables.uclk_table);
		break;
	case SMU_SOCCLK:
		single_dpm_table = &(dpm_context->dpm_tables.soc_table);
		break;
	case SMU_FCLK:
		single_dpm_table = &(dpm_context->dpm_tables.fclk_table);
		break;
	case SMU_VCLK:
	case SMU_VCLK1:
		single_dpm_table = &(dpm_context->dpm_tables.vclk_table);
		break;
	case SMU_DCLK:
	case SMU_DCLK1:
		single_dpm_table = &(dpm_context->dpm_tables.dclk_table);
		break;
	default:
		break;
	}

	switch (clk_type) {
	case SMU_GFXCLK:
	case SMU_SCLK:
	case SMU_MCLK:
	case SMU_UCLK:
	case SMU_SOCCLK:
	case SMU_FCLK:
	case SMU_VCLK:
	case SMU_VCLK1:
	case SMU_DCLK:
	case SMU_DCLK1:
		if (single_dpm_table->is_fine_grained) {
			/* There is only 2 levels for fine grained DPM */
			soft_max_level = (soft_max_level >= 1 ? 1 : 0);
			soft_min_level = (soft_min_level >= 1 ? 1 : 0);
		} else {
			if ((soft_max_level >= single_dpm_table->count) ||
			    (soft_min_level >= single_dpm_table->count))
				return -EINVAL;
		}

		min_freq = single_dpm_table->dpm_levels[soft_min_level].value;
		max_freq = single_dpm_table->dpm_levels[soft_max_level].value;

		ret = smu_v13_0_set_soft_freq_limited_range(smu,
							    clk_type,
							    min_freq,
							    max_freq);
		break;
	case SMU_DCEFCLK:
	case SMU_PCIE:
	default:
		break;
	}

	return ret;
}

static int smu_v13_0_7_update_pcie_parameters(struct smu_context *smu,
					      uint32_t pcie_gen_cap,
					      uint32_t pcie_width_cap)
{
	struct smu_13_0_dpm_context *dpm_context = smu->smu_dpm.dpm_context;
	struct smu_13_0_pcie_table *pcie_table =
				&dpm_context->dpm_tables.pcie_table;
	uint32_t smu_pcie_arg;
	int ret, i;

	for (i = 0; i < pcie_table->num_of_link_levels; i++) {
		if (pcie_table->pcie_gen[i] > pcie_gen_cap)
			pcie_table->pcie_gen[i] = pcie_gen_cap;
		if (pcie_table->pcie_lane[i] > pcie_width_cap)
			pcie_table->pcie_lane[i] = pcie_width_cap;

		smu_pcie_arg = i << 16;
		smu_pcie_arg |= pcie_table->pcie_gen[i] << 8;
		smu_pcie_arg |= pcie_table->pcie_lane[i];

		ret = smu_cmn_send_smc_msg_with_param(smu,
						      SMU_MSG_OverridePcieParameters,
						      smu_pcie_arg,
						      NULL);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct smu_temperature_range smu13_thermal_policy[] =
{
	{-273150,  99000, 99000, -273150, 99000, 99000, -273150, 99000, 99000},
	{ 120000, 120000, 120000, 120000, 120000, 120000, 120000, 120000, 120000},
};

static int smu_v13_0_7_get_thermal_temperature_range(struct smu_context *smu,
						     struct smu_temperature_range *range)
{
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_13_0_7_powerplay_table *powerplay_table =
		table_context->power_play_table;
	PPTable_t *pptable = smu->smu_table.driver_pptable;

	if (!range)
		return -EINVAL;

	memcpy(range, &smu13_thermal_policy[0], sizeof(struct smu_temperature_range));

	range->max = pptable->SkuTable.TemperatureLimit[TEMP_EDGE] *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->edge_emergency_max = (pptable->SkuTable.TemperatureLimit[TEMP_EDGE] + CTF_OFFSET_EDGE) *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->hotspot_crit_max = pptable->SkuTable.TemperatureLimit[TEMP_HOTSPOT] *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->hotspot_emergency_max = (pptable->SkuTable.TemperatureLimit[TEMP_HOTSPOT] + CTF_OFFSET_HOTSPOT) *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->mem_crit_max = pptable->SkuTable.TemperatureLimit[TEMP_MEM] *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->mem_emergency_max = (pptable->SkuTable.TemperatureLimit[TEMP_MEM] + CTF_OFFSET_MEM)*
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->software_shutdown_temp = powerplay_table->software_shutdown_temp;
	range->software_shutdown_temp_offset = pptable->SkuTable.FanAbnormalTempLimitOffset;

	return 0;
}

#define MAX(a, b)	((a) > (b) ? (a) : (b))
static ssize_t smu_v13_0_7_get_gpu_metrics(struct smu_context *smu,
					   void **table)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct gpu_metrics_v1_3 *gpu_metrics =
		(struct gpu_metrics_v1_3 *)smu_table->gpu_metrics_table;
	SmuMetricsExternal_t metrics_ext;
	SmuMetrics_t *metrics = &metrics_ext.SmuMetrics;
	int ret = 0;

	ret = smu_cmn_get_metrics_table(smu,
					&metrics_ext,
					true);
	if (ret)
		return ret;

	smu_cmn_init_soft_gpu_metrics(gpu_metrics, 1, 3);

	gpu_metrics->temperature_edge = metrics->AvgTemperature[TEMP_EDGE];
	gpu_metrics->temperature_hotspot = metrics->AvgTemperature[TEMP_HOTSPOT];
	gpu_metrics->temperature_mem = metrics->AvgTemperature[TEMP_MEM];
	gpu_metrics->temperature_vrgfx = metrics->AvgTemperature[TEMP_VR_GFX];
	gpu_metrics->temperature_vrsoc = metrics->AvgTemperature[TEMP_VR_SOC];
	gpu_metrics->temperature_vrmem = MAX(metrics->AvgTemperature[TEMP_VR_MEM0],
					     metrics->AvgTemperature[TEMP_VR_MEM1]);

	gpu_metrics->average_gfx_activity = metrics->AverageGfxActivity;
	gpu_metrics->average_umc_activity = metrics->AverageUclkActivity;
	gpu_metrics->average_mm_activity = MAX(metrics->Vcn0ActivityPercentage,
					       metrics->Vcn1ActivityPercentage);

	gpu_metrics->average_socket_power = metrics->AverageSocketPower;
	gpu_metrics->energy_accumulator = metrics->EnergyAccumulator;

	if (metrics->AverageGfxActivity <= SMU_13_0_7_BUSY_THRESHOLD)
		gpu_metrics->average_gfxclk_frequency = metrics->AverageGfxclkFrequencyPostDs;
	else
		gpu_metrics->average_gfxclk_frequency = metrics->AverageGfxclkFrequencyPreDs;

	if (metrics->AverageUclkActivity <= SMU_13_0_7_BUSY_THRESHOLD)
		gpu_metrics->average_uclk_frequency = metrics->AverageMemclkFrequencyPostDs;
	else
		gpu_metrics->average_uclk_frequency = metrics->AverageMemclkFrequencyPreDs;

	gpu_metrics->average_vclk0_frequency = metrics->AverageVclk0Frequency;
	gpu_metrics->average_dclk0_frequency = metrics->AverageDclk0Frequency;
	gpu_metrics->average_vclk1_frequency = metrics->AverageVclk1Frequency;
	gpu_metrics->average_dclk1_frequency = metrics->AverageDclk1Frequency;

	gpu_metrics->current_gfxclk = metrics->CurrClock[PPCLK_GFXCLK];
	gpu_metrics->current_vclk0 = metrics->CurrClock[PPCLK_VCLK_0];
	gpu_metrics->current_dclk0 = metrics->CurrClock[PPCLK_DCLK_0];
	gpu_metrics->current_vclk1 = metrics->CurrClock[PPCLK_VCLK_1];
	gpu_metrics->current_dclk1 = metrics->CurrClock[PPCLK_DCLK_1];

	gpu_metrics->throttle_status =
			smu_v13_0_7_get_throttler_status(metrics);
	gpu_metrics->indep_throttle_status =
			smu_cmn_get_indep_throttler_status(gpu_metrics->throttle_status,
							   smu_v13_0_7_throttler_map);

	gpu_metrics->current_fan_speed = metrics->AvgFanRpm;

	gpu_metrics->pcie_link_width = metrics->PcieWidth;
	gpu_metrics->pcie_link_speed = metrics->PcieRate;

	gpu_metrics->system_clock_counter = ktime_get_boottime_ns();

	gpu_metrics->voltage_gfx = metrics->AvgVoltage[SVI_PLANE_GFX];
	gpu_metrics->voltage_soc = metrics->AvgVoltage[SVI_PLANE_SOC];
	gpu_metrics->voltage_mem = metrics->AvgVoltage[SVI_PLANE_VMEMP];

	*table = (void *)gpu_metrics;

	return sizeof(struct gpu_metrics_v1_3);
}

static int smu_v13_0_7_populate_umd_state_clk(struct smu_context *smu)
{
	struct smu_13_0_dpm_context *dpm_context =
				smu->smu_dpm.dpm_context;
	struct smu_13_0_dpm_table *gfx_table =
				&dpm_context->dpm_tables.gfx_table;
	struct smu_13_0_dpm_table *mem_table =
				&dpm_context->dpm_tables.uclk_table;
	struct smu_13_0_dpm_table *soc_table =
				&dpm_context->dpm_tables.soc_table;
	struct smu_13_0_dpm_table *vclk_table =
				&dpm_context->dpm_tables.vclk_table;
	struct smu_13_0_dpm_table *dclk_table =
				&dpm_context->dpm_tables.dclk_table;
	struct smu_13_0_dpm_table *fclk_table =
				&dpm_context->dpm_tables.fclk_table;
	struct smu_umd_pstate_table *pstate_table =
				&smu->pstate_table;
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *pptable = table_context->driver_pptable;
	DriverReportedClocks_t driver_clocks =
		pptable->SkuTable.DriverReportedClocks;

	pstate_table->gfxclk_pstate.min = gfx_table->min;
	if (driver_clocks.GameClockAc &&
		(driver_clocks.GameClockAc < gfx_table->max))
		pstate_table->gfxclk_pstate.peak = driver_clocks.GameClockAc;
	else
		pstate_table->gfxclk_pstate.peak = gfx_table->max;

	pstate_table->uclk_pstate.min = mem_table->min;
	pstate_table->uclk_pstate.peak = mem_table->max;

	pstate_table->socclk_pstate.min = soc_table->min;
	pstate_table->socclk_pstate.peak = soc_table->max;

	pstate_table->vclk_pstate.min = vclk_table->min;
	pstate_table->vclk_pstate.peak = vclk_table->max;

	pstate_table->dclk_pstate.min = dclk_table->min;
	pstate_table->dclk_pstate.peak = dclk_table->max;

	pstate_table->fclk_pstate.min = fclk_table->min;
	pstate_table->fclk_pstate.peak = fclk_table->max;

	if (driver_clocks.BaseClockAc &&
		driver_clocks.BaseClockAc < gfx_table->max)
		pstate_table->gfxclk_pstate.standard = driver_clocks.BaseClockAc;
	else
		pstate_table->gfxclk_pstate.standard = gfx_table->max;
	pstate_table->uclk_pstate.standard = mem_table->max;
	pstate_table->socclk_pstate.standard = soc_table->min;
	pstate_table->vclk_pstate.standard = vclk_table->min;
	pstate_table->dclk_pstate.standard = dclk_table->min;
	pstate_table->fclk_pstate.standard = fclk_table->min;

	return 0;
}

static int smu_v13_0_7_get_fan_speed_pwm(struct smu_context *smu,
					 uint32_t *speed)
{
	int ret;

	if (!speed)
		return -EINVAL;

	ret = smu_v13_0_7_get_smu_metrics_data(smu,
					       METRICS_CURR_FANPWM,
					       speed);
	if (ret) {
		dev_err(smu->adev->dev, "Failed to get fan speed(PWM)!");
		return ret;
	}

	/* Convert the PMFW output which is in percent to pwm(255) based */
	*speed = MIN(*speed * 255 / 100, 255);

	return 0;
}

static int smu_v13_0_7_get_fan_speed_rpm(struct smu_context *smu,
					 uint32_t *speed)
{
	if (!speed)
		return -EINVAL;

	return smu_v13_0_7_get_smu_metrics_data(smu,
						METRICS_CURR_FANSPEED,
						speed);
}

static int smu_v13_0_7_enable_mgpu_fan_boost(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *pptable = table_context->driver_pptable;
	SkuTable_t *skutable = &pptable->SkuTable;

	/*
	 * Skip the MGpuFanBoost setting for those ASICs
	 * which do not support it
	 */
	if (skutable->MGpuAcousticLimitRpmThreshold == 0)
		return 0;

	return smu_cmn_send_smc_msg_with_param(smu,
					       SMU_MSG_SetMGpuFanBoostLimitRpm,
					       0,
					       NULL);
}

static int smu_v13_0_7_get_power_limit(struct smu_context *smu,
				       uint32_t *current_power_limit,
				       uint32_t *default_power_limit,
				       uint32_t *max_power_limit)
{
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_13_0_7_powerplay_table *powerplay_table =
		(struct smu_13_0_7_powerplay_table *)table_context->power_play_table;
	PPTable_t *pptable = table_context->driver_pptable;
	SkuTable_t *skutable = &pptable->SkuTable;
	uint32_t power_limit, od_percent;

	if (smu_v13_0_get_current_power_limit(smu, &power_limit))
		power_limit = smu->adev->pm.ac_power ?
			      skutable->SocketPowerLimitAc[PPT_THROTTLER_PPT0] :
			      skutable->SocketPowerLimitDc[PPT_THROTTLER_PPT0];

	if (current_power_limit)
		*current_power_limit = power_limit;
	if (default_power_limit)
		*default_power_limit = power_limit;

	if (max_power_limit) {
		if (smu->od_enabled) {
			od_percent = le32_to_cpu(powerplay_table->overdrive_table.max[SMU_13_0_7_ODSETTING_POWERPERCENTAGE]);

			dev_dbg(smu->adev->dev, "ODSETTING_POWERPERCENTAGE: %d (default: %d)\n", od_percent, power_limit);

			power_limit *= (100 + od_percent);
			power_limit /= 100;
		}
		*max_power_limit = power_limit;
	}

	return 0;
}

static int smu_v13_0_7_get_power_profile_mode(struct smu_context *smu, char *buf)
{
	DpmActivityMonitorCoeffIntExternal_t *activity_monitor_external;
	uint32_t i, j, size = 0;
	int16_t workload_type = 0;
	int result = 0;

	if (!buf)
		return -EINVAL;

	activity_monitor_external = kcalloc(PP_SMC_POWER_PROFILE_COUNT,
					    sizeof(*activity_monitor_external),
					    GFP_KERNEL);
	if (!activity_monitor_external)
		return -ENOMEM;

	size += sysfs_emit_at(buf, size, "                              ");
	for (i = 0; i <= PP_SMC_POWER_PROFILE_WINDOW3D; i++)
		size += sysfs_emit_at(buf, size, "%-14s%s", amdgpu_pp_profile_name[i],
			(i == smu->power_profile_mode) ? "* " : "  ");

	size += sysfs_emit_at(buf, size, "\n");

	for (i = 0; i <= PP_SMC_POWER_PROFILE_WINDOW3D; i++) {
		/* conv PP_SMC_POWER_PROFILE* to WORKLOAD_PPLIB_*_BIT */
		workload_type = smu_cmn_to_asic_specific_index(smu,
							       CMN2ASIC_MAPPING_WORKLOAD,
							       i);
		if (workload_type == -ENOTSUPP)
			continue;
		else if (workload_type < 0) {
			result = -EINVAL;
			goto out;
		}

		result = smu_cmn_update_table(smu,
					  SMU_TABLE_ACTIVITY_MONITOR_COEFF, workload_type,
					  (void *)(&activity_monitor_external[i]), false);
		if (result) {
			dev_err(smu->adev->dev, "[%s] Failed to get activity monitor!", __func__);
			goto out;
		}
	}

#define PRINT_DPM_MONITOR(field)									\
do {													\
	size += sysfs_emit_at(buf, size, "%-30s", #field);						\
	for (j = 0; j <= PP_SMC_POWER_PROFILE_WINDOW3D; j++)						\
		size += sysfs_emit_at(buf, size, "%-16d", activity_monitor_external[j].DpmActivityMonitorCoeffInt.field);		\
	size += sysfs_emit_at(buf, size, "\n");								\
} while (0)

	PRINT_DPM_MONITOR(Gfx_ActiveHystLimit);
	PRINT_DPM_MONITOR(Gfx_IdleHystLimit);
	PRINT_DPM_MONITOR(Gfx_FPS);
	PRINT_DPM_MONITOR(Gfx_MinActiveFreqType);
	PRINT_DPM_MONITOR(Gfx_BoosterFreqType);
	PRINT_DPM_MONITOR(Gfx_MinActiveFreq);
	PRINT_DPM_MONITOR(Gfx_BoosterFreq);
	PRINT_DPM_MONITOR(Fclk_ActiveHystLimit);
	PRINT_DPM_MONITOR(Fclk_IdleHystLimit);
	PRINT_DPM_MONITOR(Fclk_FPS);
	PRINT_DPM_MONITOR(Fclk_MinActiveFreqType);
	PRINT_DPM_MONITOR(Fclk_BoosterFreqType);
	PRINT_DPM_MONITOR(Fclk_MinActiveFreq);
	PRINT_DPM_MONITOR(Fclk_BoosterFreq);
#undef PRINT_DPM_MONITOR

	result = size;
out:
	kfree(activity_monitor_external);
	return result;
}

static int smu_v13_0_7_set_power_profile_mode(struct smu_context *smu, long *input, uint32_t size)
{

	DpmActivityMonitorCoeffIntExternal_t activity_monitor_external;
	DpmActivityMonitorCoeffInt_t *activity_monitor =
		&(activity_monitor_external.DpmActivityMonitorCoeffInt);
	int workload_type, ret = 0;

	smu->power_profile_mode = input[size];

	if (smu->power_profile_mode > PP_SMC_POWER_PROFILE_WINDOW3D) {
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
			activity_monitor->Gfx_ActiveHystLimit = input[1];
			activity_monitor->Gfx_IdleHystLimit = input[2];
			activity_monitor->Gfx_FPS = input[3];
			activity_monitor->Gfx_MinActiveFreqType = input[4];
			activity_monitor->Gfx_BoosterFreqType = input[5];
			activity_monitor->Gfx_MinActiveFreq = input[6];
			activity_monitor->Gfx_BoosterFreq = input[7];
			break;
		case 1: /* Fclk */
			activity_monitor->Fclk_ActiveHystLimit = input[1];
			activity_monitor->Fclk_IdleHystLimit = input[2];
			activity_monitor->Fclk_FPS = input[3];
			activity_monitor->Fclk_MinActiveFreqType = input[4];
			activity_monitor->Fclk_BoosterFreqType = input[5];
			activity_monitor->Fclk_MinActiveFreq = input[6];
			activity_monitor->Fclk_BoosterFreq = input[7];
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

static int smu_v13_0_7_set_mp1_state(struct smu_context *smu,
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

static int smu_v13_0_7_baco_enter(struct smu_context *smu)
{
	struct smu_baco_context *smu_baco = &smu->smu_baco;
	struct amdgpu_device *adev = smu->adev;

	if (adev->in_runpm && smu_cmn_is_audio_func_enabled(adev))
		return smu_v13_0_baco_set_armd3_sequence(smu,
				smu_baco->maco_support ? BACO_SEQ_BAMACO : BACO_SEQ_BACO);
	else
		return smu_v13_0_baco_enter(smu);
}

static int smu_v13_0_7_baco_exit(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;

	if (adev->in_runpm && smu_cmn_is_audio_func_enabled(adev)) {
		/* Wait for PMFW handling for the Dstate change */
		usleep_range(10000, 11000);
		return smu_v13_0_baco_set_armd3_sequence(smu, BACO_SEQ_ULPS);
	} else {
		return smu_v13_0_baco_exit(smu);
	}
}

static bool smu_v13_0_7_is_mode1_reset_supported(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;

	/* SRIOV does not support SMU mode1 reset */
	if (amdgpu_sriov_vf(adev))
		return false;

	return true;
}

static int smu_v13_0_7_set_df_cstate(struct smu_context *smu,
				     enum pp_df_cstate state)
{
	return smu_cmn_send_smc_msg_with_param(smu,
					       SMU_MSG_DFCstateControl,
					       state,
					       NULL);
}

static const struct pptable_funcs smu_v13_0_7_ppt_funcs = {
	.get_allowed_feature_mask = smu_v13_0_7_get_allowed_feature_mask,
	.set_default_dpm_table = smu_v13_0_7_set_default_dpm_table,
	.is_dpm_running = smu_v13_0_7_is_dpm_running,
	.dump_pptable = smu_v13_0_7_dump_pptable,
	.init_microcode = smu_v13_0_init_microcode,
	.load_microcode = smu_v13_0_load_microcode,
	.fini_microcode = smu_v13_0_fini_microcode,
	.init_smc_tables = smu_v13_0_7_init_smc_tables,
	.fini_smc_tables = smu_v13_0_fini_smc_tables,
	.init_power = smu_v13_0_init_power,
	.fini_power = smu_v13_0_fini_power,
	.check_fw_status = smu_v13_0_7_check_fw_status,
	.setup_pptable = smu_v13_0_7_setup_pptable,
	.check_fw_version = smu_v13_0_check_fw_version,
	.write_pptable = smu_cmn_write_pptable,
	.set_driver_table_location = smu_v13_0_set_driver_table_location,
	.system_features_control = smu_v13_0_system_features_control,
	.set_allowed_mask = smu_v13_0_set_allowed_mask,
	.get_enabled_mask = smu_cmn_get_enabled_mask,
	.dpm_set_vcn_enable = smu_v13_0_set_vcn_enable,
	.dpm_set_jpeg_enable = smu_v13_0_set_jpeg_enable,
	.init_pptable_microcode = smu_v13_0_init_pptable_microcode,
	.populate_umd_state_clk = smu_v13_0_7_populate_umd_state_clk,
	.get_dpm_ultimate_freq = smu_v13_0_7_get_dpm_ultimate_freq,
	.get_vbios_bootup_values = smu_v13_0_get_vbios_bootup_values,
	.read_sensor = smu_v13_0_7_read_sensor,
	.feature_is_enabled = smu_cmn_feature_is_enabled,
	.print_clk_levels = smu_v13_0_7_print_clk_levels,
	.force_clk_levels = smu_v13_0_7_force_clk_levels,
	.update_pcie_parameters = smu_v13_0_7_update_pcie_parameters,
	.get_thermal_temperature_range = smu_v13_0_7_get_thermal_temperature_range,
	.register_irq_handler = smu_v13_0_register_irq_handler,
	.enable_thermal_alert = smu_v13_0_enable_thermal_alert,
	.disable_thermal_alert = smu_v13_0_disable_thermal_alert,
	.notify_memory_pool_location = smu_v13_0_notify_memory_pool_location,
	.get_gpu_metrics = smu_v13_0_7_get_gpu_metrics,
	.set_soft_freq_limited_range = smu_v13_0_set_soft_freq_limited_range,
	.set_performance_level = smu_v13_0_set_performance_level,
	.gfx_off_control = smu_v13_0_gfx_off_control,
	.get_fan_speed_pwm = smu_v13_0_7_get_fan_speed_pwm,
	.get_fan_speed_rpm = smu_v13_0_7_get_fan_speed_rpm,
	.set_fan_speed_pwm = smu_v13_0_set_fan_speed_pwm,
	.set_fan_speed_rpm = smu_v13_0_set_fan_speed_rpm,
	.get_fan_control_mode = smu_v13_0_get_fan_control_mode,
	.set_fan_control_mode = smu_v13_0_set_fan_control_mode,
	.enable_mgpu_fan_boost = smu_v13_0_7_enable_mgpu_fan_boost,
	.get_power_limit = smu_v13_0_7_get_power_limit,
	.set_power_limit = smu_v13_0_set_power_limit,
	.set_power_source = smu_v13_0_set_power_source,
	.get_power_profile_mode = smu_v13_0_7_get_power_profile_mode,
	.set_power_profile_mode = smu_v13_0_7_set_power_profile_mode,
	.set_tool_table_location = smu_v13_0_set_tool_table_location,
	.get_pp_feature_mask = smu_cmn_get_pp_feature_mask,
	.set_pp_feature_mask = smu_cmn_set_pp_feature_mask,
	.baco_is_support = smu_v13_0_baco_is_support,
	.baco_get_state = smu_v13_0_baco_get_state,
	.baco_set_state = smu_v13_0_baco_set_state,
	.baco_enter = smu_v13_0_7_baco_enter,
	.baco_exit = smu_v13_0_7_baco_exit,
	.mode1_reset_is_support = smu_v13_0_7_is_mode1_reset_supported,
	.mode1_reset = smu_v13_0_mode1_reset,
	.set_mp1_state = smu_v13_0_7_set_mp1_state,
	.set_df_cstate = smu_v13_0_7_set_df_cstate,
	.gpo_control = smu_v13_0_gpo_control,
};

void smu_v13_0_7_set_ppt_funcs(struct smu_context *smu)
{
	smu->ppt_funcs = &smu_v13_0_7_ppt_funcs;
	smu->message_map = smu_v13_0_7_message_map;
	smu->clock_map = smu_v13_0_7_clk_map;
	smu->feature_map = smu_v13_0_7_feature_mask_map;
	smu->table_map = smu_v13_0_7_table_map;
	smu->pwr_src_map = smu_v13_0_7_pwr_src_map;
	smu->workload_map = smu_v13_0_7_workload_map;
	smu_v13_0_set_smu_mailbox_registers(smu);
}
