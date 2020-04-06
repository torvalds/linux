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

#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "smu_internal.h"
#include "smu_v12_0_ppsmc.h"
#include "smu12_driver_if.h"
#include "smu_v12_0.h"
#include "renoir_ppt.h"


#define CLK_MAP(clk, index) \
	[SMU_##clk] = {1, (index)}

#define MSG_MAP(msg, index) \
	[SMU_MSG_##msg] = {1, (index)}

#define TAB_MAP_VALID(tab) \
	[SMU_TABLE_##tab] = {1, TABLE_##tab}

#define TAB_MAP_INVALID(tab) \
	[SMU_TABLE_##tab] = {0, TABLE_##tab}

static struct smu_12_0_cmn2aisc_mapping renoir_message_map[SMU_MSG_MAX_COUNT] = {
	MSG_MAP(TestMessage,                    PPSMC_MSG_TestMessage),
	MSG_MAP(GetSmuVersion,                  PPSMC_MSG_GetSmuVersion),
	MSG_MAP(GetDriverIfVersion,             PPSMC_MSG_GetDriverIfVersion),
	MSG_MAP(PowerUpGfx,                     PPSMC_MSG_PowerUpGfx),
	MSG_MAP(AllowGfxOff,                    PPSMC_MSG_EnableGfxOff),
	MSG_MAP(DisallowGfxOff,                 PPSMC_MSG_DisableGfxOff),
	MSG_MAP(PowerDownIspByTile,             PPSMC_MSG_PowerDownIspByTile),
	MSG_MAP(PowerUpIspByTile,               PPSMC_MSG_PowerUpIspByTile),
	MSG_MAP(PowerDownVcn,                   PPSMC_MSG_PowerDownVcn),
	MSG_MAP(PowerUpVcn,                     PPSMC_MSG_PowerUpVcn),
	MSG_MAP(PowerDownSdma,                  PPSMC_MSG_PowerDownSdma),
	MSG_MAP(PowerUpSdma,                    PPSMC_MSG_PowerUpSdma),
	MSG_MAP(SetHardMinIspclkByFreq,         PPSMC_MSG_SetHardMinIspclkByFreq),
	MSG_MAP(SetHardMinVcn,                  PPSMC_MSG_SetHardMinVcn),
	MSG_MAP(Spare1,                         PPSMC_MSG_spare1),
	MSG_MAP(Spare2,                         PPSMC_MSG_spare2),
	MSG_MAP(SetAllowFclkSwitch,             PPSMC_MSG_SetAllowFclkSwitch),
	MSG_MAP(SetMinVideoGfxclkFreq,          PPSMC_MSG_SetMinVideoGfxclkFreq),
	MSG_MAP(ActiveProcessNotify,            PPSMC_MSG_ActiveProcessNotify),
	MSG_MAP(SetCustomPolicy,                PPSMC_MSG_SetCustomPolicy),
	MSG_MAP(SetVideoFps,                    PPSMC_MSG_SetVideoFps),
	MSG_MAP(NumOfDisplays,                  PPSMC_MSG_SetDisplayCount),
	MSG_MAP(QueryPowerLimit,                PPSMC_MSG_QueryPowerLimit),
	MSG_MAP(SetDriverDramAddrHigh,          PPSMC_MSG_SetDriverDramAddrHigh),
	MSG_MAP(SetDriverDramAddrLow,           PPSMC_MSG_SetDriverDramAddrLow),
	MSG_MAP(TransferTableSmu2Dram,          PPSMC_MSG_TransferTableSmu2Dram),
	MSG_MAP(TransferTableDram2Smu,          PPSMC_MSG_TransferTableDram2Smu),
	MSG_MAP(GfxDeviceDriverReset,           PPSMC_MSG_GfxDeviceDriverReset),
	MSG_MAP(SetGfxclkOverdriveByFreqVid,    PPSMC_MSG_SetGfxclkOverdriveByFreqVid),
	MSG_MAP(SetHardMinDcfclkByFreq,         PPSMC_MSG_SetHardMinDcfclkByFreq),
	MSG_MAP(SetHardMinSocclkByFreq,         PPSMC_MSG_SetHardMinSocclkByFreq),
	MSG_MAP(ControlIgpuATS,                 PPSMC_MSG_ControlIgpuATS),
	MSG_MAP(SetMinVideoFclkFreq,            PPSMC_MSG_SetMinVideoFclkFreq),
	MSG_MAP(SetMinDeepSleepDcfclk,          PPSMC_MSG_SetMinDeepSleepDcfclk),
	MSG_MAP(ForcePowerDownGfx,              PPSMC_MSG_ForcePowerDownGfx),
	MSG_MAP(SetPhyclkVoltageByFreq,         PPSMC_MSG_SetPhyclkVoltageByFreq),
	MSG_MAP(SetDppclkVoltageByFreq,         PPSMC_MSG_SetDppclkVoltageByFreq),
	MSG_MAP(SetSoftMinVcn,                  PPSMC_MSG_SetSoftMinVcn),
	MSG_MAP(EnablePostCode,                 PPSMC_MSG_EnablePostCode),
	MSG_MAP(GetGfxclkFrequency,             PPSMC_MSG_GetGfxclkFrequency),
	MSG_MAP(GetFclkFrequency,               PPSMC_MSG_GetFclkFrequency),
	MSG_MAP(GetMinGfxclkFrequency,          PPSMC_MSG_GetMinGfxclkFrequency),
	MSG_MAP(GetMaxGfxclkFrequency,          PPSMC_MSG_GetMaxGfxclkFrequency),
	MSG_MAP(SoftReset,                      PPSMC_MSG_SoftReset),
	MSG_MAP(SetGfxCGPG,                     PPSMC_MSG_SetGfxCGPG),
	MSG_MAP(SetSoftMaxGfxClk,               PPSMC_MSG_SetSoftMaxGfxClk),
	MSG_MAP(SetHardMinGfxClk,               PPSMC_MSG_SetHardMinGfxClk),
	MSG_MAP(SetSoftMaxSocclkByFreq,         PPSMC_MSG_SetSoftMaxSocclkByFreq),
	MSG_MAP(SetSoftMaxFclkByFreq,           PPSMC_MSG_SetSoftMaxFclkByFreq),
	MSG_MAP(SetSoftMaxVcn,                  PPSMC_MSG_SetSoftMaxVcn),
	MSG_MAP(PowerGateMmHub,                 PPSMC_MSG_PowerGateMmHub),
	MSG_MAP(UpdatePmeRestore,               PPSMC_MSG_UpdatePmeRestore),
	MSG_MAP(GpuChangeState,                 PPSMC_MSG_GpuChangeState),
	MSG_MAP(SetPowerLimitPercentage,        PPSMC_MSG_SetPowerLimitPercentage),
	MSG_MAP(ForceGfxContentSave,            PPSMC_MSG_ForceGfxContentSave),
	MSG_MAP(EnableTmdp48MHzRefclkPwrDown,   PPSMC_MSG_EnableTmdp48MHzRefclkPwrDown),
	MSG_MAP(PowerDownJpeg,                  PPSMC_MSG_PowerDownJpeg),
	MSG_MAP(PowerUpJpeg,                    PPSMC_MSG_PowerUpJpeg),
	MSG_MAP(PowerGateAtHub,                 PPSMC_MSG_PowerGateAtHub),
	MSG_MAP(SetSoftMinJpeg,                 PPSMC_MSG_SetSoftMinJpeg),
	MSG_MAP(SetHardMinFclkByFreq,           PPSMC_MSG_SetHardMinFclkByFreq),
};

static struct smu_12_0_cmn2aisc_mapping renoir_clk_map[SMU_CLK_COUNT] = {
	CLK_MAP(GFXCLK, CLOCK_GFXCLK),
	CLK_MAP(SCLK,	CLOCK_GFXCLK),
	CLK_MAP(SOCCLK, CLOCK_SOCCLK),
	CLK_MAP(UCLK, CLOCK_FCLK),
	CLK_MAP(MCLK, CLOCK_FCLK),
};

static struct smu_12_0_cmn2aisc_mapping renoir_table_map[SMU_TABLE_COUNT] = {
	TAB_MAP_VALID(WATERMARKS),
	TAB_MAP_INVALID(CUSTOM_DPM),
	TAB_MAP_VALID(DPMCLOCKS),
	TAB_MAP_VALID(SMU_METRICS),
};

static int renoir_get_smu_msg_index(struct smu_context *smc, uint32_t index)
{
	struct smu_12_0_cmn2aisc_mapping mapping;

	if (index >= SMU_MSG_MAX_COUNT)
		return -EINVAL;

	mapping = renoir_message_map[index];
	if (!(mapping.valid_mapping))
		return -EINVAL;

	return mapping.map_to;
}

static int renoir_get_smu_clk_index(struct smu_context *smc, uint32_t index)
{
	struct smu_12_0_cmn2aisc_mapping mapping;

	if (index >= SMU_CLK_COUNT)
		return -EINVAL;

	mapping = renoir_clk_map[index];
	if (!(mapping.valid_mapping)) {
		return -EINVAL;
	}

	return mapping.map_to;
}

static int renoir_get_smu_table_index(struct smu_context *smc, uint32_t index)
{
	struct smu_12_0_cmn2aisc_mapping mapping;

	if (index >= SMU_TABLE_COUNT)
		return -EINVAL;

	mapping = renoir_table_map[index];
	if (!(mapping.valid_mapping))
		return -EINVAL;

	return mapping.map_to;
}

static int renoir_get_metrics_table(struct smu_context *smu,
				    SmuMetrics_t *metrics_table)
{
	struct smu_table_context *smu_table= &smu->smu_table;
	int ret = 0;

	mutex_lock(&smu->metrics_lock);
	if (!smu_table->metrics_time || time_after(jiffies, smu_table->metrics_time + msecs_to_jiffies(100))) {
		ret = smu_update_table(smu, SMU_TABLE_SMU_METRICS, 0,
				(void *)smu_table->metrics_table, false);
		if (ret) {
			pr_info("Failed to export SMU metrics table!\n");
			mutex_unlock(&smu->metrics_lock);
			return ret;
		}
		smu_table->metrics_time = jiffies;
	}

	memcpy(metrics_table, smu_table->metrics_table, sizeof(SmuMetrics_t));
	mutex_unlock(&smu->metrics_lock);

	return ret;
}

static int renoir_tables_init(struct smu_context *smu, struct smu_table *tables)
{
	struct smu_table_context *smu_table = &smu->smu_table;

	SMU_TABLE_INIT(tables, SMU_TABLE_WATERMARKS, sizeof(Watermarks_t),
		PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_DPMCLOCKS, sizeof(DpmClocks_t),
		PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_SMU_METRICS, sizeof(SmuMetrics_t),
		PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);

	smu_table->clocks_table = kzalloc(sizeof(DpmClocks_t), GFP_KERNEL);
	if (!smu_table->clocks_table)
		return -ENOMEM;

	smu_table->metrics_table = kzalloc(sizeof(SmuMetrics_t), GFP_KERNEL);
	if (!smu_table->metrics_table)
		return -ENOMEM;
	smu_table->metrics_time = 0;

	smu_table->watermarks_table = kzalloc(sizeof(Watermarks_t), GFP_KERNEL);
	if (!smu_table->watermarks_table)
		return -ENOMEM;

	return 0;
}

/**
 * This interface just for getting uclk ultimate freq and should't introduce
 * other likewise function result in overmuch callback.
 */
static int renoir_get_dpm_clk_limited(struct smu_context *smu, enum smu_clk_type clk_type,
						uint32_t dpm_level, uint32_t *freq)
{
	DpmClocks_t *clk_table = smu->smu_table.clocks_table;

	if (!clk_table || clk_type >= SMU_CLK_COUNT)
		return -EINVAL;

	GET_DPM_CUR_FREQ(clk_table, clk_type, dpm_level, *freq);

	return 0;
}

static int renoir_print_clk_levels(struct smu_context *smu,
			enum smu_clk_type clk_type, char *buf)
{
	int i, size = 0, ret = 0;
	uint32_t cur_value = 0, value = 0, count = 0, min = 0, max = 0;
	DpmClocks_t *clk_table = smu->smu_table.clocks_table;
	SmuMetrics_t metrics;

	if (!clk_table || clk_type >= SMU_CLK_COUNT)
		return -EINVAL;

	memset(&metrics, 0, sizeof(metrics));

	ret = renoir_get_metrics_table(smu, &metrics);
	if (ret)
		return ret;

	switch (clk_type) {
	case SMU_GFXCLK:
	case SMU_SCLK:
		/* retirve table returned paramters unit is MHz */
		cur_value = metrics.ClockFrequency[CLOCK_GFXCLK];
		ret = smu_get_dpm_freq_range(smu, SMU_GFXCLK, &min, &max, false);
		if (!ret) {
			/* driver only know min/max gfx_clk, Add level 1 for all other gfx clks */
			if (cur_value  == max)
				i = 2;
			else if (cur_value == min)
				i = 0;
			else
				i = 1;

			size += sprintf(buf + size, "0: %uMhz %s\n", min,
					i == 0 ? "*" : "");
			size += sprintf(buf + size, "1: %uMhz %s\n",
					i == 1 ? cur_value : RENOIR_UMD_PSTATE_GFXCLK,
					i == 1 ? "*" : "");
			size += sprintf(buf + size, "2: %uMhz %s\n", max,
					i == 2 ? "*" : "");
		}
		return size;
	case SMU_SOCCLK:
		count = NUM_SOCCLK_DPM_LEVELS;
		cur_value = metrics.ClockFrequency[CLOCK_SOCCLK];
		break;
	case SMU_MCLK:
		count = NUM_MEMCLK_DPM_LEVELS;
		cur_value = metrics.ClockFrequency[CLOCK_FCLK];
		break;
	case SMU_DCEFCLK:
		count = NUM_DCFCLK_DPM_LEVELS;
		cur_value = metrics.ClockFrequency[CLOCK_DCFCLK];
		break;
	case SMU_FCLK:
		count = NUM_FCLK_DPM_LEVELS;
		cur_value = metrics.ClockFrequency[CLOCK_FCLK];
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < count; i++) {
		GET_DPM_CUR_FREQ(clk_table, clk_type, i, value);
		size += sprintf(buf + size, "%d: %uMhz %s\n", i, value,
				cur_value == value ? "*" : "");
	}

	return size;
}

static enum amd_pm_state_type renoir_get_current_power_state(struct smu_context *smu)
{
	enum amd_pm_state_type pm_type;
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);

	if (!smu_dpm_ctx->dpm_context ||
	    !smu_dpm_ctx->dpm_current_power_state)
		return -EINVAL;

	switch (smu_dpm_ctx->dpm_current_power_state->classification.ui_label) {
	case SMU_STATE_UI_LABEL_BATTERY:
		pm_type = POWER_STATE_TYPE_BATTERY;
		break;
	case SMU_STATE_UI_LABEL_BALLANCED:
		pm_type = POWER_STATE_TYPE_BALANCED;
		break;
	case SMU_STATE_UI_LABEL_PERFORMANCE:
		pm_type = POWER_STATE_TYPE_PERFORMANCE;
		break;
	default:
		if (smu_dpm_ctx->dpm_current_power_state->classification.flags & SMU_STATE_CLASSIFICATION_FLAG_BOOT)
			pm_type = POWER_STATE_TYPE_INTERNAL_BOOT;
		else
			pm_type = POWER_STATE_TYPE_DEFAULT;
		break;
	}

	return pm_type;
}

static int renoir_dpm_set_uvd_enable(struct smu_context *smu, bool enable)
{
	struct smu_power_context *smu_power = &smu->smu_power;
	struct smu_power_gate *power_gate = &smu_power->power_gate;
	int ret = 0;

	if (enable) {
		/* vcn dpm on is a prerequisite for vcn power gate messages */
		if (smu_feature_is_enabled(smu, SMU_FEATURE_VCN_PG_BIT)) {
			ret = smu_send_smc_msg_with_param(smu, SMU_MSG_PowerUpVcn, 0, NULL);
			if (ret)
				return ret;
		}
		power_gate->vcn_gated = false;
	} else {
		if (smu_feature_is_enabled(smu, SMU_FEATURE_VCN_PG_BIT)) {
			ret = smu_send_smc_msg(smu, SMU_MSG_PowerDownVcn, NULL);
			if (ret)
				return ret;
		}
		power_gate->vcn_gated = true;
	}

	return ret;
}

static int renoir_dpm_set_jpeg_enable(struct smu_context *smu, bool enable)
{
	struct smu_power_context *smu_power = &smu->smu_power;
	struct smu_power_gate *power_gate = &smu_power->power_gate;
	int ret = 0;

	if (enable) {
		if (smu_feature_is_enabled(smu, SMU_FEATURE_JPEG_PG_BIT)) {
			ret = smu_send_smc_msg_with_param(smu, SMU_MSG_PowerUpJpeg, 0, NULL);
			if (ret)
				return ret;
		}
		power_gate->jpeg_gated = false;
	} else {
		if (smu_feature_is_enabled(smu, SMU_FEATURE_JPEG_PG_BIT)) {
			ret = smu_send_smc_msg_with_param(smu, SMU_MSG_PowerDownJpeg, 0, NULL);
			if (ret)
				return ret;
		}
		power_gate->jpeg_gated = true;
	}

	return ret;
}

static int renoir_get_current_clk_freq_by_table(struct smu_context *smu,
				       enum smu_clk_type clk_type,
				       uint32_t *value)
{
	int ret = 0, clk_id = 0;
	SmuMetrics_t metrics;

	ret = renoir_get_metrics_table(smu, &metrics);
	if (ret)
		return ret;

	clk_id = smu_clk_get_index(smu, clk_type);
	if (clk_id < 0)
		return clk_id;

	*value = metrics.ClockFrequency[clk_id];

	return ret;
}

static int renoir_force_dpm_limit_value(struct smu_context *smu, bool highest)
{
	int ret = 0, i = 0;
	uint32_t min_freq, max_freq, force_freq;
	enum smu_clk_type clk_type;

	enum smu_clk_type clks[] = {
		SMU_GFXCLK,
		SMU_MCLK,
		SMU_SOCCLK,
	};

	for (i = 0; i < ARRAY_SIZE(clks); i++) {
		clk_type = clks[i];
		ret = smu_get_dpm_freq_range(smu, clk_type, &min_freq, &max_freq, false);
		if (ret)
			return ret;

		force_freq = highest ? max_freq : min_freq;
		ret = smu_set_soft_freq_range(smu, clk_type, force_freq, force_freq, false);
		if (ret)
			return ret;
	}

	return ret;
}

static int renoir_unforce_dpm_levels(struct smu_context *smu) {

	int ret = 0, i = 0;
	uint32_t min_freq, max_freq;
	enum smu_clk_type clk_type;

	struct clk_feature_map {
		enum smu_clk_type clk_type;
		uint32_t	feature;
	} clk_feature_map[] = {
		{SMU_GFXCLK, SMU_FEATURE_DPM_GFXCLK_BIT},
		{SMU_MCLK,   SMU_FEATURE_DPM_UCLK_BIT},
		{SMU_SOCCLK, SMU_FEATURE_DPM_SOCCLK_BIT},
	};

	for (i = 0; i < ARRAY_SIZE(clk_feature_map); i++) {
		if (!smu_feature_is_enabled(smu, clk_feature_map[i].feature))
		    continue;

		clk_type = clk_feature_map[i].clk_type;

		ret = smu_get_dpm_freq_range(smu, clk_type, &min_freq, &max_freq, false);
		if (ret)
			return ret;

		ret = smu_set_soft_freq_range(smu, clk_type, min_freq, max_freq, false);
		if (ret)
			return ret;
	}

	return ret;
}

static int renoir_get_gpu_temperature(struct smu_context *smu, uint32_t *value)
{
	int ret = 0;
	SmuMetrics_t metrics;

	if (!value)
		return -EINVAL;

	ret = renoir_get_metrics_table(smu, &metrics);
	if (ret)
		return ret;

	*value = (metrics.GfxTemperature / 100) *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;

	return 0;
}

static int renoir_get_current_activity_percent(struct smu_context *smu,
					       enum amd_pp_sensors sensor,
					       uint32_t *value)
{
	int ret = 0;
	SmuMetrics_t metrics;

	if (!value)
		return -EINVAL;

	ret = renoir_get_metrics_table(smu, &metrics);
	if (ret)
		return ret;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		*value = metrics.AverageGfxActivity / 100;
		break;
	default:
		pr_err("Invalid sensor for retrieving clock activity\n");
		return -EINVAL;
	}

	return 0;
}

static int renoir_get_workload_type(struct smu_context *smu, uint32_t profile)
{

	uint32_t  pplib_workload = 0;

	switch (profile) {
	case PP_SMC_POWER_PROFILE_FULLSCREEN3D:
		pplib_workload = WORKLOAD_PPLIB_FULL_SCREEN_3D_BIT;
		break;
	case PP_SMC_POWER_PROFILE_CUSTOM:
		pplib_workload = WORKLOAD_PPLIB_COUNT;
		break;
	case PP_SMC_POWER_PROFILE_VIDEO:
		pplib_workload = WORKLOAD_PPLIB_VIDEO_BIT;
		break;
	case PP_SMC_POWER_PROFILE_VR:
		pplib_workload = WORKLOAD_PPLIB_VR_BIT;
		break;
	case PP_SMC_POWER_PROFILE_COMPUTE:
		pplib_workload = WORKLOAD_PPLIB_COMPUTE_BIT;
		break;
	default:
		return -EINVAL;
	}

	return pplib_workload;
}

static int renoir_get_profiling_clk_mask(struct smu_context *smu,
					 enum amd_dpm_forced_level level,
					 uint32_t *sclk_mask,
					 uint32_t *mclk_mask,
					 uint32_t *soc_mask)
{

	if (level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK) {
		if (sclk_mask)
			*sclk_mask = 0;
	} else if (level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK) {
		if (mclk_mask)
			*mclk_mask = 0;
	} else if (level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) {
		if(sclk_mask)
			/* The sclk as gfxclk and has three level about max/min/current */
			*sclk_mask = 3 - 1;

		if(mclk_mask)
			*mclk_mask = NUM_MEMCLK_DPM_LEVELS - 1;

		if(soc_mask)
			*soc_mask = NUM_SOCCLK_DPM_LEVELS - 1;
	}

	return 0;
}

/**
 * This interface get dpm clock table for dc
 */
static int renoir_get_dpm_clock_table(struct smu_context *smu, struct dpm_clocks *clock_table)
{
	DpmClocks_t *table = smu->smu_table.clocks_table;
	int i;

	if (!clock_table || !table)
		return -EINVAL;

	for (i = 0; i < NUM_DCFCLK_DPM_LEVELS; i++) {
		clock_table->DcfClocks[i].Freq = table->DcfClocks[i].Freq;
		clock_table->DcfClocks[i].Vol = table->DcfClocks[i].Vol;
	}

	for (i = 0; i < NUM_SOCCLK_DPM_LEVELS; i++) {
		clock_table->SocClocks[i].Freq = table->SocClocks[i].Freq;
		clock_table->SocClocks[i].Vol = table->SocClocks[i].Vol;
	}

	for (i = 0; i < NUM_FCLK_DPM_LEVELS; i++) {
		clock_table->FClocks[i].Freq = table->FClocks[i].Freq;
		clock_table->FClocks[i].Vol = table->FClocks[i].Vol;
	}

	for (i = 0; i<  NUM_MEMCLK_DPM_LEVELS; i++) {
		clock_table->MemClocks[i].Freq = table->MemClocks[i].Freq;
		clock_table->MemClocks[i].Vol = table->MemClocks[i].Vol;
	}

	return 0;
}

static int renoir_force_clk_levels(struct smu_context *smu,
				   enum smu_clk_type clk_type, uint32_t mask)
{

	int ret = 0 ;
	uint32_t soft_min_level = 0, soft_max_level = 0, min_freq = 0, max_freq = 0;
	DpmClocks_t *clk_table = smu->smu_table.clocks_table;

	soft_min_level = mask ? (ffs(mask) - 1) : 0;
	soft_max_level = mask ? (fls(mask) - 1) : 0;

	switch (clk_type) {
	case SMU_GFXCLK:
	case SMU_SCLK:
		if (soft_min_level > 2 || soft_max_level > 2) {
			pr_info("Currently sclk only support 3 levels on APU\n");
			return -EINVAL;
		}

		ret = smu_get_dpm_freq_range(smu, SMU_GFXCLK, &min_freq, &max_freq, false);
		if (ret)
			return ret;
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMaxGfxClk,
					soft_max_level == 0 ? min_freq :
					soft_max_level == 1 ? RENOIR_UMD_PSTATE_GFXCLK : max_freq,
					NULL);
		if (ret)
			return ret;
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetHardMinGfxClk,
					soft_min_level == 2 ? max_freq :
					soft_min_level == 1 ? RENOIR_UMD_PSTATE_GFXCLK : min_freq,
					NULL);
		if (ret)
			return ret;
		break;
	case SMU_SOCCLK:
		GET_DPM_CUR_FREQ(clk_table, clk_type, soft_min_level, min_freq);
		GET_DPM_CUR_FREQ(clk_table, clk_type, soft_max_level, max_freq);
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMaxSocclkByFreq, max_freq, NULL);
		if (ret)
			return ret;
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetHardMinSocclkByFreq, min_freq, NULL);
		if (ret)
			return ret;
		break;
	case SMU_MCLK:
	case SMU_FCLK:
		GET_DPM_CUR_FREQ(clk_table, clk_type, soft_min_level, min_freq);
		GET_DPM_CUR_FREQ(clk_table, clk_type, soft_max_level, max_freq);
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMaxFclkByFreq, max_freq, NULL);
		if (ret)
			return ret;
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetHardMinFclkByFreq, min_freq, NULL);
		if (ret)
			return ret;
		break;
	default:
		break;
	}

	return ret;
}

static int renoir_set_power_profile_mode(struct smu_context *smu, long *input, uint32_t size)
{
	int workload_type, ret;
	uint32_t profile_mode = input[size];

	if (profile_mode > PP_SMC_POWER_PROFILE_CUSTOM) {
		pr_err("Invalid power profile mode %d\n", smu->power_profile_mode);
		return -EINVAL;
	}

	/* conv PP_SMC_POWER_PROFILE* to WORKLOAD_PPLIB_*_BIT */
	workload_type = smu_workload_get_type(smu, smu->power_profile_mode);
	if (workload_type < 0) {
		/*
		 * TODO: If some case need switch to powersave/default power mode
		 * then can consider enter WORKLOAD_COMPUTE/WORKLOAD_CUSTOM for power saving.
		 */
		pr_err_once("Unsupported power profile mode %d on RENOIR\n",smu->power_profile_mode);
		return -EINVAL;
	}

	ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetWorkloadMask,
				    1 << workload_type,
				    NULL);
	if (ret) {
		pr_err_once("Fail to set workload type %d\n", workload_type);
		return ret;
	}

	smu->power_profile_mode = profile_mode;

	return 0;
}

static int renoir_set_peak_clock_by_device(struct smu_context *smu)
{
	int ret = 0;
	uint32_t sclk_freq = 0, uclk_freq = 0;

	ret = smu_get_dpm_freq_range(smu, SMU_SCLK, NULL, &sclk_freq, false);
	if (ret)
		return ret;

	ret = smu_set_soft_freq_range(smu, SMU_SCLK, sclk_freq, sclk_freq, false);
	if (ret)
		return ret;

	ret = smu_get_dpm_freq_range(smu, SMU_UCLK, NULL, &uclk_freq, false);
	if (ret)
		return ret;

	ret = smu_set_soft_freq_range(smu, SMU_UCLK, uclk_freq, uclk_freq, false);
	if (ret)
		return ret;

	return ret;
}

static int renoir_set_performance_level(struct smu_context *smu,
					enum amd_dpm_forced_level level)
{
	int ret = 0;
	uint32_t sclk_mask, mclk_mask, soc_mask;

	switch (level) {
	case AMD_DPM_FORCED_LEVEL_HIGH:
		ret = smu_force_dpm_limit_value(smu, true);
		break;
	case AMD_DPM_FORCED_LEVEL_LOW:
		ret = smu_force_dpm_limit_value(smu, false);
		break;
	case AMD_DPM_FORCED_LEVEL_AUTO:
	case AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD:
		ret = smu_unforce_dpm_levels(smu);
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK:
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK:
		ret = smu_get_profiling_clk_mask(smu, level,
						 &sclk_mask,
						 &mclk_mask,
						 &soc_mask);
		if (ret)
			return ret;
		smu_force_clk_levels(smu, SMU_SCLK, 1 << sclk_mask, false);
		smu_force_clk_levels(smu, SMU_MCLK, 1 << mclk_mask, false);
		smu_force_clk_levels(smu, SMU_SOCCLK, 1 << soc_mask, false);
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_PEAK:
		ret = renoir_set_peak_clock_by_device(smu);
		break;
	case AMD_DPM_FORCED_LEVEL_MANUAL:
	case AMD_DPM_FORCED_LEVEL_PROFILE_EXIT:
	default:
		break;
	}
	return ret;
}

/* save watermark settings into pplib smu structure,
 * also pass data to smu controller
 */
static int renoir_set_watermarks_table(
		struct smu_context *smu,
		void *watermarks,
		struct dm_pp_wm_sets_with_clock_ranges_soc15 *clock_ranges)
{
	int i;
	int ret = 0;
	Watermarks_t *table = watermarks;

	if (!table || !clock_ranges)
		return -EINVAL;

	if (clock_ranges->num_wm_dmif_sets > 4 ||
			clock_ranges->num_wm_mcif_sets > 4)
		return -EINVAL;

	/* save into smu->smu_table.tables[SMU_TABLE_WATERMARKS]->cpu_addr*/
	for (i = 0; i < clock_ranges->num_wm_dmif_sets; i++) {
		table->WatermarkRow[WM_DCFCLK][i].MinClock =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_dmif_clocks_ranges[i].wm_min_dcfclk_clk_in_khz));
		table->WatermarkRow[WM_DCFCLK][i].MaxClock =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_dmif_clocks_ranges[i].wm_max_dcfclk_clk_in_khz));
		table->WatermarkRow[WM_DCFCLK][i].MinMclk =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_dmif_clocks_ranges[i].wm_min_mem_clk_in_khz));
		table->WatermarkRow[WM_DCFCLK][i].MaxMclk =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_dmif_clocks_ranges[i].wm_max_mem_clk_in_khz));
		table->WatermarkRow[WM_DCFCLK][i].WmSetting = (uint8_t)
				clock_ranges->wm_dmif_clocks_ranges[i].wm_set_id;
	}

	for (i = 0; i < clock_ranges->num_wm_mcif_sets; i++) {
		table->WatermarkRow[WM_SOCCLK][i].MinClock =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_mcif_clocks_ranges[i].wm_min_socclk_clk_in_khz));
		table->WatermarkRow[WM_SOCCLK][i].MaxClock =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_mcif_clocks_ranges[i].wm_max_socclk_clk_in_khz));
		table->WatermarkRow[WM_SOCCLK][i].MinMclk =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_mcif_clocks_ranges[i].wm_min_mem_clk_in_khz));
		table->WatermarkRow[WM_SOCCLK][i].MaxMclk =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_mcif_clocks_ranges[i].wm_max_mem_clk_in_khz));
		table->WatermarkRow[WM_SOCCLK][i].WmSetting = (uint8_t)
				clock_ranges->wm_mcif_clocks_ranges[i].wm_set_id;
	}

	smu->watermarks_bitmap |= WATERMARKS_EXIST;

	/* pass data to smu controller */
	if (!(smu->watermarks_bitmap & WATERMARKS_LOADED)) {
		ret = smu_write_watermarks_table(smu);
		if (ret) {
			pr_err("Failed to update WMTABLE!");
			return ret;
		}
		smu->watermarks_bitmap |= WATERMARKS_LOADED;
	}

	return 0;
}

static int renoir_get_power_profile_mode(struct smu_context *smu,
					   char *buf)
{
	static const char *profile_name[] = {
					"BOOTUP_DEFAULT",
					"3D_FULL_SCREEN",
					"POWER_SAVING",
					"VIDEO",
					"VR",
					"COMPUTE",
					"CUSTOM"};
	uint32_t i, size = 0;
	int16_t workload_type = 0;

	if (!smu->pm_enabled || !buf)
		return -EINVAL;

	for (i = 0; i <= PP_SMC_POWER_PROFILE_CUSTOM; i++) {
		/*
		 * Conv PP_SMC_POWER_PROFILE* to WORKLOAD_PPLIB_*_BIT
		 * Not all profile modes are supported on arcturus.
		 */
		workload_type = smu_workload_get_type(smu, i);
		if (workload_type < 0)
			continue;

		size += sprintf(buf + size, "%2d %14s%s\n",
			i, profile_name[i], (i == smu->power_profile_mode) ? "*" : " ");
	}

	return size;
}

static int renoir_read_sensor(struct smu_context *smu,
				 enum amd_pp_sensors sensor,
				 void *data, uint32_t *size)
{
	int ret = 0;

	if (!data || !size)
		return -EINVAL;

	mutex_lock(&smu->sensor_lock);
	switch (sensor) {
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		ret = renoir_get_current_activity_percent(smu, sensor, (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_TEMP:
		ret = renoir_get_gpu_temperature(smu, (uint32_t *)data);
		*size = 4;
		break;
	default:
		ret = smu_v12_0_read_sensor(smu, sensor, data, size);
	}
	mutex_unlock(&smu->sensor_lock);

	return ret;
}

static const struct pptable_funcs renoir_ppt_funcs = {
	.get_smu_msg_index = renoir_get_smu_msg_index,
	.get_smu_clk_index = renoir_get_smu_clk_index,
	.get_smu_table_index = renoir_get_smu_table_index,
	.tables_init = renoir_tables_init,
	.set_power_state = NULL,
	.get_dpm_clk_limited = renoir_get_dpm_clk_limited,
	.print_clk_levels = renoir_print_clk_levels,
	.get_current_power_state = renoir_get_current_power_state,
	.dpm_set_uvd_enable = renoir_dpm_set_uvd_enable,
	.dpm_set_jpeg_enable = renoir_dpm_set_jpeg_enable,
	.get_current_clk_freq_by_table = renoir_get_current_clk_freq_by_table,
	.force_dpm_limit_value = renoir_force_dpm_limit_value,
	.unforce_dpm_levels = renoir_unforce_dpm_levels,
	.get_workload_type = renoir_get_workload_type,
	.get_profiling_clk_mask = renoir_get_profiling_clk_mask,
	.force_clk_levels = renoir_force_clk_levels,
	.set_power_profile_mode = renoir_set_power_profile_mode,
	.set_performance_level = renoir_set_performance_level,
	.get_dpm_clock_table = renoir_get_dpm_clock_table,
	.set_watermarks_table = renoir_set_watermarks_table,
	.get_power_profile_mode = renoir_get_power_profile_mode,
	.read_sensor = renoir_read_sensor,
	.check_fw_status = smu_v12_0_check_fw_status,
	.check_fw_version = smu_v12_0_check_fw_version,
	.powergate_sdma = smu_v12_0_powergate_sdma,
	.powergate_vcn = smu_v12_0_powergate_vcn,
	.powergate_jpeg = smu_v12_0_powergate_jpeg,
	.send_smc_msg_with_param = smu_v12_0_send_msg_with_param,
	.set_gfx_cgpg = smu_v12_0_set_gfx_cgpg,
	.gfx_off_control = smu_v12_0_gfx_off_control,
	.init_smc_tables = smu_v12_0_init_smc_tables,
	.fini_smc_tables = smu_v12_0_fini_smc_tables,
	.populate_smc_tables = smu_v12_0_populate_smc_tables,
	.get_enabled_mask = smu_v12_0_get_enabled_mask,
	.get_current_clk_freq = smu_v12_0_get_current_clk_freq,
	.get_dpm_ultimate_freq = smu_v12_0_get_dpm_ultimate_freq,
	.mode2_reset = smu_v12_0_mode2_reset,
	.set_soft_freq_limited_range = smu_v12_0_set_soft_freq_limited_range,
	.set_driver_table_location = smu_v12_0_set_driver_table_location,
};

void renoir_set_ppt_funcs(struct smu_context *smu)
{
	smu->ppt_funcs = &renoir_ppt_funcs;
	smu->smc_if_version = SMU12_DRIVER_IF_VERSION;
	smu->is_apu = true;
}
