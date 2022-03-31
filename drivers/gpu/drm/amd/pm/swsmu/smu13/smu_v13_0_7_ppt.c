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
};

static struct cmn2asic_mapping smu_v13_0_7_clk_map[SMU_CLK_COUNT] = {
	CLK_MAP(GFXCLK,		PPCLK_GFXCLK),
	CLK_MAP(SCLK,		PPCLK_GFXCLK),
	CLK_MAP(SOCCLK,		PPCLK_SOCCLK),
	CLK_MAP(FCLK,		PPCLK_FCLK),
	CLK_MAP(UCLK,		PPCLK_UCLK),
	CLK_MAP(MCLK,		PPCLK_UCLK),
};

static struct cmn2asic_mapping smu_v13_0_7_feature_mask_map[SMU_FEATURE_COUNT] = {
	FEA_MAP(DPM_GFXCLK),
};

static struct cmn2asic_mapping smu_v13_0_7_table_map[SMU_TABLE_COUNT] = {
	TAB_MAP(PPTABLE),
	TAB_MAP(WATERMARKS),
	TAB_MAP(AVFS_PSM_DEBUG),
	TAB_MAP(PMSTATUSLOG),
	TAB_MAP(SMU_METRICS),
	TAB_MAP(DRIVER_SMU_CONFIG),
	TAB_MAP(ACTIVITY_MONITOR_COEFF),
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
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_COMPUTE,		WORKLOAD_PPLIB_CUSTOM_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_CUSTOM,		WORKLOAD_PPLIB_CUSTOM_BIT),
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
	}

	if (adev->pm.pp_feature & PP_MCLK_DPM_MASK) {
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_UCLK_BIT);
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_FCLK_BIT);
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_VMEMP_SCALING_BIT);
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_VDDIO_MEM_SCALING_BIT);
	}

	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_SOCCLK_BIT);

	if (adev->pm.pp_feature & PP_PCIE_DPM_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_LINK_BIT);

	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DS_LCLK_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_MP0CLK_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_MM_DPM_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DS_VCN_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DS_FCLK_BIT);
	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DF_CSTATE_BIT);

	if (adev->pm.pp_feature & PP_DCEFCLK_DPM_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_DCN_BIT);


	return 0;
}

static int smu_v13_0_7_check_powerplay_table(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_13_0_7_powerplay_table *powerplay_table =
		table_context->power_play_table;
	struct smu_baco_context *smu_baco = &smu->smu_baco;

	if (powerplay_table->platform_caps & SMU_13_0_7_PP_PLATFORM_CAP_HARDWAREDC)
		smu->dc_controlled_by_gpio = true;

	if (powerplay_table->platform_caps & SMU_13_0_7_PP_PLATFORM_CAP_BACO ||
	    powerplay_table->platform_caps & SMU_13_0_7_PP_PLATFORM_CAP_MACO)
		smu_baco->platform_support = true;

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

int smu_v13_0_7_check_fw_status(struct smu_context *smu) {
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


static int smu_v13_0_7_setup_pptable(struct smu_context *smu)
{
	int ret = 0;

	ret = smu_v13_0_setup_pptable(smu);
	if (ret)
		return ret;

	ret = smu_v13_0_7_store_powerplay_table(smu);
	if (ret)
		return ret;

	ret = smu_v13_0_7_append_powerplay_table(smu);
	if (ret)
		return ret;

	ret = smu_v13_0_7_check_powerplay_table(smu);
	if (ret)
		return ret;

	return ret;
}

static int smu_v13_0_7_tables_init(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = smu_table->tables;
	struct amdgpu_device *adev = smu->adev;

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

	smu_table->metrics_table = kzalloc(sizeof(SmuMetricsExternal_t), GFP_KERNEL);
	if (!smu_table->metrics_table)
		goto err0_out;
	smu_table->metrics_time = 0;

	smu_table->gpu_metrics_table_size = sizeof(struct gpu_metrics_v1_1);
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
	SkuTable_t *sku_ppt = &driver_ppt->SkuTable;
	struct smu_13_0_dpm_table *dpm_table;
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;

	/* socclk dpm table setup */
	dpm_table = &dpm_context->dpm_tables.soc_table;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_SOCCLK_BIT)) {
		ret = smu_v13_0_set_single_dpm_table(smu,
						     SMU_SOCCLK,
						     dpm_table);
		if (ret)
			return ret;
		dpm_table->is_fine_grained =
			!sku_ppt->DpmDescriptor[PPCLK_SOCCLK].SnapToDiscrete;
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
		dpm_table->is_fine_grained =
			!sku_ppt->DpmDescriptor[PPCLK_GFXCLK].SnapToDiscrete;
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
		dpm_table->is_fine_grained =
			!sku_ppt->DpmDescriptor[PPCLK_UCLK].SnapToDiscrete;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.uclk / 100;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->min = dpm_table->dpm_levels[0].value;
		dpm_table->max = dpm_table->dpm_levels[0].value;
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

static const struct pptable_funcs smu_v13_0_7_ppt_funcs = {
	.get_allowed_feature_mask = smu_v13_0_7_get_allowed_feature_mask,
	.set_default_dpm_table = smu_v13_0_7_set_default_dpm_table,
	.is_dpm_running = smu_v13_0_7_is_dpm_running,
	.dump_pptable = smu_v13_0_7_dump_pptable,
	.init_microcode = smu_v13_0_init_microcode,
	.load_microcode = smu_v13_0_load_microcode,
	.init_smc_tables = smu_v13_0_7_init_smc_tables,
	.init_power = smu_v13_0_init_power,
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
};

void smu_v13_0_7_set_ppt_funcs(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;

	smu->ppt_funcs = &smu_v13_0_7_ppt_funcs;
	smu->message_map = smu_v13_0_7_message_map;
	smu->clock_map = smu_v13_0_7_clk_map;
	smu->feature_map = smu_v13_0_7_feature_mask_map;
	smu->table_map = smu_v13_0_7_table_map;
	smu->pwr_src_map = smu_v13_0_7_pwr_src_map;
	smu->workload_map = smu_v13_0_7_workload_map;
}
