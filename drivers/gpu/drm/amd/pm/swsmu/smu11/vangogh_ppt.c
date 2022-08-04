/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "smu_v11_0.h"
#include "smu11_driver_if_vangogh.h"
#include "vangogh_ppt.h"
#include "smu_v11_5_ppsmc.h"
#include "smu_v11_5_pmfw.h"
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
	FEATURE_MASK(FEATURE_CCLK_DPM_BIT) | \
	FEATURE_MASK(FEATURE_VCN_DPM_BIT)	 | \
	FEATURE_MASK(FEATURE_FCLK_DPM_BIT)	 | \
	FEATURE_MASK(FEATURE_SOCCLK_DPM_BIT)	 | \
	FEATURE_MASK(FEATURE_MP0CLK_DPM_BIT)	 | \
	FEATURE_MASK(FEATURE_LCLK_DPM_BIT)	 | \
	FEATURE_MASK(FEATURE_SHUBCLK_DPM_BIT)	 | \
	FEATURE_MASK(FEATURE_DCFCLK_DPM_BIT)| \
	FEATURE_MASK(FEATURE_GFX_DPM_BIT))

static struct cmn2asic_msg_mapping vangogh_message_map[SMU_MSG_MAX_COUNT] = {
	MSG_MAP(TestMessage,                    PPSMC_MSG_TestMessage,			0),
	MSG_MAP(GetSmuVersion,                  PPSMC_MSG_GetSmuVersion,		0),
	MSG_MAP(GetDriverIfVersion,             PPSMC_MSG_GetDriverIfVersion,	0),
	MSG_MAP(EnableGfxOff,                   PPSMC_MSG_EnableGfxOff,			0),
	MSG_MAP(DisallowGfxOff,                 PPSMC_MSG_DisableGfxOff,		0),
	MSG_MAP(PowerDownIspByTile,             PPSMC_MSG_PowerDownIspByTile,	0),
	MSG_MAP(PowerUpIspByTile,               PPSMC_MSG_PowerUpIspByTile,		0),
	MSG_MAP(PowerDownVcn,                   PPSMC_MSG_PowerDownVcn,			0),
	MSG_MAP(PowerUpVcn,                     PPSMC_MSG_PowerUpVcn,			0),
	MSG_MAP(RlcPowerNotify,                 PPSMC_MSG_RlcPowerNotify,		0),
	MSG_MAP(SetHardMinVcn,                  PPSMC_MSG_SetHardMinVcn,		0),
	MSG_MAP(SetSoftMinGfxclk,               PPSMC_MSG_SetSoftMinGfxclk,		0),
	MSG_MAP(ActiveProcessNotify,            PPSMC_MSG_ActiveProcessNotify,		0),
	MSG_MAP(SetHardMinIspiclkByFreq,        PPSMC_MSG_SetHardMinIspiclkByFreq,	0),
	MSG_MAP(SetHardMinIspxclkByFreq,        PPSMC_MSG_SetHardMinIspxclkByFreq,	0),
	MSG_MAP(SetDriverDramAddrHigh,          PPSMC_MSG_SetDriverDramAddrHigh,	0),
	MSG_MAP(SetDriverDramAddrLow,           PPSMC_MSG_SetDriverDramAddrLow,		0),
	MSG_MAP(TransferTableSmu2Dram,          PPSMC_MSG_TransferTableSmu2Dram,	0),
	MSG_MAP(TransferTableDram2Smu,          PPSMC_MSG_TransferTableDram2Smu,	0),
	MSG_MAP(GfxDeviceDriverReset,           PPSMC_MSG_GfxDeviceDriverReset,		0),
	MSG_MAP(GetEnabledSmuFeatures,          PPSMC_MSG_GetEnabledSmuFeatures,	0),
	MSG_MAP(Spare1,                         PPSMC_MSG_spare1,					0),
	MSG_MAP(SetHardMinSocclkByFreq,         PPSMC_MSG_SetHardMinSocclkByFreq,	0),
	MSG_MAP(SetSoftMinFclk,                 PPSMC_MSG_SetSoftMinFclk,		0),
	MSG_MAP(SetSoftMinVcn,                  PPSMC_MSG_SetSoftMinVcn,		0),
	MSG_MAP(EnablePostCode,                 PPSMC_MSG_EnablePostCode,		0),
	MSG_MAP(GetGfxclkFrequency,             PPSMC_MSG_GetGfxclkFrequency,	0),
	MSG_MAP(GetFclkFrequency,               PPSMC_MSG_GetFclkFrequency,		0),
	MSG_MAP(SetSoftMaxGfxClk,               PPSMC_MSG_SetSoftMaxGfxClk,		0),
	MSG_MAP(SetHardMinGfxClk,               PPSMC_MSG_SetHardMinGfxClk,		0),
	MSG_MAP(SetSoftMaxSocclkByFreq,         PPSMC_MSG_SetSoftMaxSocclkByFreq,	0),
	MSG_MAP(SetSoftMaxFclkByFreq,           PPSMC_MSG_SetSoftMaxFclkByFreq,		0),
	MSG_MAP(SetSoftMaxVcn,                  PPSMC_MSG_SetSoftMaxVcn,			0),
	MSG_MAP(Spare2,                         PPSMC_MSG_spare2,					0),
	MSG_MAP(SetPowerLimitPercentage,        PPSMC_MSG_SetPowerLimitPercentage,	0),
	MSG_MAP(PowerDownJpeg,                  PPSMC_MSG_PowerDownJpeg,			0),
	MSG_MAP(PowerUpJpeg,                    PPSMC_MSG_PowerUpJpeg,				0),
	MSG_MAP(SetHardMinFclkByFreq,           PPSMC_MSG_SetHardMinFclkByFreq,		0),
	MSG_MAP(SetSoftMinSocclkByFreq,         PPSMC_MSG_SetSoftMinSocclkByFreq,	0),
	MSG_MAP(PowerUpCvip,                    PPSMC_MSG_PowerUpCvip,				0),
	MSG_MAP(PowerDownCvip,                  PPSMC_MSG_PowerDownCvip,			0),
	MSG_MAP(GetPptLimit,                        PPSMC_MSG_GetPptLimit,			0),
	MSG_MAP(GetThermalLimit,                    PPSMC_MSG_GetThermalLimit,		0),
	MSG_MAP(GetCurrentTemperature,              PPSMC_MSG_GetCurrentTemperature, 0),
	MSG_MAP(GetCurrentPower,                    PPSMC_MSG_GetCurrentPower,		 0),
	MSG_MAP(GetCurrentVoltage,                  PPSMC_MSG_GetCurrentVoltage,	 0),
	MSG_MAP(GetCurrentCurrent,                  PPSMC_MSG_GetCurrentCurrent,	 0),
	MSG_MAP(GetAverageCpuActivity,              PPSMC_MSG_GetAverageCpuActivity, 0),
	MSG_MAP(GetAverageGfxActivity,              PPSMC_MSG_GetAverageGfxActivity, 0),
	MSG_MAP(GetAveragePower,                    PPSMC_MSG_GetAveragePower,		 0),
	MSG_MAP(GetAverageTemperature,              PPSMC_MSG_GetAverageTemperature, 0),
	MSG_MAP(SetAveragePowerTimeConstant,        PPSMC_MSG_SetAveragePowerTimeConstant,			0),
	MSG_MAP(SetAverageActivityTimeConstant,     PPSMC_MSG_SetAverageActivityTimeConstant,		0),
	MSG_MAP(SetAverageTemperatureTimeConstant,  PPSMC_MSG_SetAverageTemperatureTimeConstant,	0),
	MSG_MAP(SetMitigationEndHysteresis,         PPSMC_MSG_SetMitigationEndHysteresis,			0),
	MSG_MAP(GetCurrentFreq,                     PPSMC_MSG_GetCurrentFreq,						0),
	MSG_MAP(SetReducedPptLimit,                 PPSMC_MSG_SetReducedPptLimit,					0),
	MSG_MAP(SetReducedThermalLimit,             PPSMC_MSG_SetReducedThermalLimit,				0),
	MSG_MAP(DramLogSetDramAddr,                 PPSMC_MSG_DramLogSetDramAddr,					0),
	MSG_MAP(StartDramLogging,                   PPSMC_MSG_StartDramLogging,						0),
	MSG_MAP(StopDramLogging,                    PPSMC_MSG_StopDramLogging,						0),
	MSG_MAP(SetSoftMinCclk,                     PPSMC_MSG_SetSoftMinCclk,						0),
	MSG_MAP(SetSoftMaxCclk,                     PPSMC_MSG_SetSoftMaxCclk,						0),
};

static struct cmn2asic_mapping vangogh_feature_mask_map[SMU_FEATURE_COUNT] = {
	FEA_MAP(PPT),
	FEA_MAP(TDC),
	FEA_MAP(THERMAL),
	FEA_MAP(DS_GFXCLK),
	FEA_MAP(DS_SOCCLK),
	FEA_MAP(DS_LCLK),
	FEA_MAP(DS_FCLK),
	FEA_MAP(DS_MP1CLK),
	FEA_MAP(DS_MP0CLK),
	FEA_MAP(ATHUB_PG),
	FEA_MAP(CCLK_DPM),
	FEA_MAP(FAN_CONTROLLER),
	FEA_MAP(ULV),
	FEA_MAP(VCN_DPM),
	FEA_MAP(LCLK_DPM),
	FEA_MAP(SHUBCLK_DPM),
	FEA_MAP(DCFCLK_DPM),
	FEA_MAP(DS_DCFCLK),
	FEA_MAP(S0I2),
	FEA_MAP(SMU_LOW_POWER),
	FEA_MAP(GFX_DEM),
	FEA_MAP(PSI),
	FEA_MAP(PROCHOT),
	FEA_MAP(CPUOFF),
	FEA_MAP(STAPM),
	FEA_MAP(S0I3),
	FEA_MAP(DF_CSTATES),
	FEA_MAP(PERF_LIMIT),
	FEA_MAP(CORE_DLDO),
	FEA_MAP(RSMU_LOW_POWER),
	FEA_MAP(SMN_LOW_POWER),
	FEA_MAP(THM_LOW_POWER),
	FEA_MAP(SMUIO_LOW_POWER),
	FEA_MAP(MP1_LOW_POWER),
	FEA_MAP(DS_VCN),
	FEA_MAP(CPPC),
	FEA_MAP(OS_CSTATES),
	FEA_MAP(ISP_DPM),
	FEA_MAP(A55_DPM),
	FEA_MAP(CVIP_DSP_DPM),
	FEA_MAP(MSMU_LOW_POWER),
};

static struct cmn2asic_mapping vangogh_table_map[SMU_TABLE_COUNT] = {
	TAB_MAP_VALID(WATERMARKS),
	TAB_MAP_VALID(SMU_METRICS),
	TAB_MAP_VALID(CUSTOM_DPM),
	TAB_MAP_VALID(DPMCLOCKS),
};

static int vangogh_tables_init(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = smu_table->tables;

	SMU_TABLE_INIT(tables, SMU_TABLE_WATERMARKS, sizeof(Watermarks_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_SMU_METRICS, sizeof(SmuMetrics_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_DPMCLOCKS, sizeof(DpmClocks_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_PMSTATUSLOG, SMU11_TOOL_SIZE,
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_ACTIVITY_MONITOR_COEFF, sizeof(DpmActivityMonitorCoeffExt_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	smu_table->metrics_table = kzalloc(sizeof(SmuMetrics_t), GFP_KERNEL);
	if (!smu_table->metrics_table)
		goto err0_out;
	smu_table->metrics_time = 0;

	smu_table->gpu_metrics_table_size = sizeof(struct gpu_metrics_v2_0);
	smu_table->gpu_metrics_table = kzalloc(smu_table->gpu_metrics_table_size, GFP_KERNEL);
	if (!smu_table->gpu_metrics_table)
		goto err1_out;

	smu_table->watermarks_table = kzalloc(sizeof(Watermarks_t), GFP_KERNEL);
	if (!smu_table->watermarks_table)
		goto err2_out;

	smu_table->clocks_table = kzalloc(sizeof(DpmClocks_t), GFP_KERNEL);
	if (!smu_table->clocks_table)
		goto err3_out;

	return 0;

err3_out:
	kfree(smu_table->clocks_table);
err2_out:
	kfree(smu_table->gpu_metrics_table);
err1_out:
	kfree(smu_table->metrics_table);
err0_out:
	return -ENOMEM;
}

static int vangogh_get_smu_metrics_data(struct smu_context *smu,
				       MetricsMember_t member,
				       uint32_t *value)
{
	struct smu_table_context *smu_table = &smu->smu_table;

	SmuMetrics_t *metrics = (SmuMetrics_t *)smu_table->metrics_table;
	int ret = 0;

	mutex_lock(&smu->metrics_lock);

	ret = smu_cmn_get_metrics_table_locked(smu,
					       NULL,
					       false);
	if (ret) {
		mutex_unlock(&smu->metrics_lock);
		return ret;
	}

	switch (member) {
	case METRICS_AVERAGE_GFXCLK:
		*value = metrics->GfxclkFrequency;
		break;
	case METRICS_AVERAGE_SOCCLK:
		*value = metrics->SocclkFrequency;
		break;
	case METRICS_AVERAGE_UCLK:
		*value = metrics->MemclkFrequency;
		break;
	case METRICS_AVERAGE_GFXACTIVITY:
		*value = metrics->GfxActivity / 100;
		break;
	case METRICS_AVERAGE_VCNACTIVITY:
		*value = metrics->UvdActivity;
		break;
	case METRICS_AVERAGE_SOCKETPOWER:
		*value = (metrics->CurrentSocketPower << 8) /
		1000 ;
		break;
	case METRICS_TEMPERATURE_EDGE:
		*value = metrics->GfxTemperature / 100 *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_TEMPERATURE_HOTSPOT:
		*value = metrics->SocTemperature / 100 *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_THROTTLER_STATUS:
		*value = metrics->ThrottlerStatus;
		break;
	case METRICS_VOLTAGE_VDDGFX:
		*value = metrics->Voltage[2];
		break;
	case METRICS_VOLTAGE_VDDSOC:
		*value = metrics->Voltage[1];
		break;
	default:
		*value = UINT_MAX;
		break;
	}

	mutex_unlock(&smu->metrics_lock);

	return ret;
}

static int vangogh_allocate_dpm_context(struct smu_context *smu)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;

	smu_dpm->dpm_context = kzalloc(sizeof(struct smu_11_0_dpm_context),
				       GFP_KERNEL);
	if (!smu_dpm->dpm_context)
		return -ENOMEM;

	smu_dpm->dpm_context_size = sizeof(struct smu_11_0_dpm_context);

	return 0;
}

static int vangogh_init_smc_tables(struct smu_context *smu)
{
	int ret = 0;

	ret = vangogh_tables_init(smu);
	if (ret)
		return ret;

	ret = vangogh_allocate_dpm_context(smu);
	if (ret)
		return ret;

	return smu_v11_0_init_smc_tables(smu);
}

static int vangogh_dpm_set_vcn_enable(struct smu_context *smu, bool enable)
{
	int ret = 0;

	if (enable) {
		/* vcn dpm on is a prerequisite for vcn power gate messages */
		if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_VCN_PG_BIT)) {
			ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_PowerUpVcn, 0, NULL);
			if (ret)
				return ret;
		}
	} else {
		if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_VCN_PG_BIT)) {
			ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_PowerDownVcn, 0, NULL);
			if (ret)
				return ret;
		}
	}

	return ret;
}

static int vangogh_dpm_set_jpeg_enable(struct smu_context *smu, bool enable)
{
	int ret = 0;

	if (enable) {
		if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_JPEG_PG_BIT)) {
			ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_PowerUpJpeg, 0, NULL);
			if (ret)
				return ret;
		}
	} else {
		if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_JPEG_PG_BIT)) {
			ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_PowerDownJpeg, 0, NULL);
			if (ret)
				return ret;
		}
	}

	return ret;
}

static int vangogh_get_allowed_feature_mask(struct smu_context *smu,
					    uint32_t *feature_mask,
					    uint32_t num)
{
	struct amdgpu_device *adev = smu->adev;

	if (num > 2)
		return -EINVAL;

	memset(feature_mask, 0, sizeof(uint32_t) * num);

	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_GFX_DPM_BIT)
				| FEATURE_MASK(FEATURE_MP0CLK_DPM_BIT)
				| FEATURE_MASK(FEATURE_DS_SOCCLK_BIT)
				| FEATURE_MASK(FEATURE_PPT_BIT)
				| FEATURE_MASK(FEATURE_TDC_BIT)
				| FEATURE_MASK(FEATURE_FAN_CONTROLLER_BIT)
				| FEATURE_MASK(FEATURE_DS_LCLK_BIT)
				| FEATURE_MASK(FEATURE_DS_DCFCLK_BIT);

	if (adev->pm.pp_feature & PP_SOCCLK_DPM_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_SOCCLK_DPM_BIT);

	if (adev->pm.pp_feature & PP_DCEFCLK_DPM_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DCFCLK_DPM_BIT);

	if (smu->adev->pg_flags & AMD_PG_SUPPORT_ATHUB)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_ATHUB_PG_BIT);

	return 0;
}

static bool vangogh_is_dpm_running(struct smu_context *smu)
{
	int ret = 0;
	uint32_t feature_mask[2];
	uint64_t feature_enabled;

	ret = smu_cmn_get_enabled_32_bits_mask(smu, feature_mask, 2);

	if (ret)
		return false;

	feature_enabled = (unsigned long)((uint64_t)feature_mask[0] |
				((uint64_t)feature_mask[1] << 32));

	return !!(feature_enabled & SMC_DPM_FEATURE);
}

static int vangogh_print_fine_grain_clk(struct smu_context *smu,
			enum smu_clk_type clk_type, char *buf)
{
	int size = 0;

	switch (clk_type) {
	case SMU_OD_SCLK:
		if (smu->od_enabled) {
			size = sprintf(buf, "%s:\n", "OD_SCLK");
			size += sprintf(buf + size, "0: %10uMhz\n",
			(smu->gfx_actual_hard_min_freq > 0) ? smu->gfx_actual_hard_min_freq : smu->gfx_default_hard_min_freq);
			size += sprintf(buf + size, "1: %10uMhz\n",
			(smu->gfx_actual_soft_max_freq > 0) ? smu->gfx_actual_soft_max_freq : smu->gfx_default_soft_max_freq);
		}
		break;
	case SMU_OD_RANGE:
		if (smu->od_enabled) {
			size = sprintf(buf, "%s:\n", "OD_RANGE");
			size += sprintf(buf + size, "SCLK: %7uMhz %10uMhz\n",
				smu->gfx_default_hard_min_freq, smu->gfx_default_soft_max_freq);
		}
		break;
	default:
		break;
	}

	return size;
}

static int vangogh_read_sensor(struct smu_context *smu,
				 enum amd_pp_sensors sensor,
				 void *data, uint32_t *size)
{
	int ret = 0;

	if (!data || !size)
		return -EINVAL;

	mutex_lock(&smu->sensor_lock);
	switch (sensor) {
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		ret = vangogh_get_smu_metrics_data(smu,
						   METRICS_AVERAGE_GFXACTIVITY,
						   (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_POWER:
		ret = vangogh_get_smu_metrics_data(smu,
						   METRICS_AVERAGE_SOCKETPOWER,
						   (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_EDGE_TEMP:
		ret = vangogh_get_smu_metrics_data(smu,
						   METRICS_TEMPERATURE_EDGE,
						   (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_HOTSPOT_TEMP:
		ret = vangogh_get_smu_metrics_data(smu,
						   METRICS_TEMPERATURE_HOTSPOT,
						   (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GFX_MCLK:
		ret = vangogh_get_smu_metrics_data(smu,
						   METRICS_AVERAGE_UCLK,
						   (uint32_t *)data);
		*(uint32_t *)data *= 100;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GFX_SCLK:
		ret = vangogh_get_smu_metrics_data(smu,
						   METRICS_AVERAGE_GFXCLK,
						   (uint32_t *)data);
		*(uint32_t *)data *= 100;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_VDDGFX:
		ret = vangogh_get_smu_metrics_data(smu,
						   METRICS_VOLTAGE_VDDGFX,
						   (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_VDDNB:
		ret = vangogh_get_smu_metrics_data(smu,
						   METRICS_VOLTAGE_VDDSOC,
						   (uint32_t *)data);
		*size = 4;
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}
	mutex_unlock(&smu->sensor_lock);

	return ret;
}

static int vangogh_set_watermarks_table(struct smu_context *smu,
				       struct pp_smu_wm_range_sets *clock_ranges)
{
	int i;
	int ret = 0;
	Watermarks_t *table = smu->smu_table.watermarks_table;

	if (!table || !clock_ranges)
		return -EINVAL;

	if (clock_ranges) {
		if (clock_ranges->num_reader_wm_sets > NUM_WM_RANGES ||
		    clock_ranges->num_writer_wm_sets > NUM_WM_RANGES)
			return -EINVAL;

		for (i = 0; i < clock_ranges->num_reader_wm_sets; i++) {
			table->WatermarkRow[WM_DCFCLK][i].MinClock =
				clock_ranges->reader_wm_sets[i].min_drain_clk_mhz;
			table->WatermarkRow[WM_DCFCLK][i].MaxClock =
				clock_ranges->reader_wm_sets[i].max_drain_clk_mhz;
			table->WatermarkRow[WM_DCFCLK][i].MinMclk =
				clock_ranges->reader_wm_sets[i].min_fill_clk_mhz;
			table->WatermarkRow[WM_DCFCLK][i].MaxMclk =
				clock_ranges->reader_wm_sets[i].max_fill_clk_mhz;

			table->WatermarkRow[WM_DCFCLK][i].WmSetting =
				clock_ranges->reader_wm_sets[i].wm_inst;
		}

		for (i = 0; i < clock_ranges->num_writer_wm_sets; i++) {
			table->WatermarkRow[WM_SOCCLK][i].MinClock =
				clock_ranges->writer_wm_sets[i].min_fill_clk_mhz;
			table->WatermarkRow[WM_SOCCLK][i].MaxClock =
				clock_ranges->writer_wm_sets[i].max_fill_clk_mhz;
			table->WatermarkRow[WM_SOCCLK][i].MinMclk =
				clock_ranges->writer_wm_sets[i].min_drain_clk_mhz;
			table->WatermarkRow[WM_SOCCLK][i].MaxMclk =
				clock_ranges->writer_wm_sets[i].max_drain_clk_mhz;

			table->WatermarkRow[WM_SOCCLK][i].WmSetting =
				clock_ranges->writer_wm_sets[i].wm_inst;
		}

		smu->watermarks_bitmap |= WATERMARKS_EXIST;
	}

	/* pass data to smu controller */
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

static ssize_t vangogh_get_gpu_metrics(struct smu_context *smu,
				      void **table)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct gpu_metrics_v2_0 *gpu_metrics =
		(struct gpu_metrics_v2_0 *)smu_table->gpu_metrics_table;
	SmuMetrics_t metrics;
	int ret = 0;

	ret = smu_cmn_get_metrics_table(smu, &metrics, true);
	if (ret)
		return ret;

	smu_v11_0_init_gpu_metrics_v2_0(gpu_metrics);

	gpu_metrics->temperature_gfx = metrics.GfxTemperature;
	gpu_metrics->temperature_soc = metrics.SocTemperature;
	memcpy(&gpu_metrics->temperature_core[0],
		&metrics.CoreTemperature[0],
		sizeof(uint16_t) * 8);
	gpu_metrics->temperature_l3[0] = metrics.L3Temperature[0];
	gpu_metrics->temperature_l3[1] = metrics.L3Temperature[1];

	gpu_metrics->average_gfx_activity = metrics.GfxActivity;
	gpu_metrics->average_mm_activity = metrics.UvdActivity;

	gpu_metrics->average_socket_power = metrics.CurrentSocketPower;
	gpu_metrics->average_cpu_power = metrics.Power[0];
	gpu_metrics->average_soc_power = metrics.Power[1];
	gpu_metrics->average_gfx_power = metrics.Power[2];
	memcpy(&gpu_metrics->average_core_power[0],
		&metrics.CorePower[0],
		sizeof(uint16_t) * 8);

	gpu_metrics->average_gfxclk_frequency = metrics.GfxclkFrequency;
	gpu_metrics->average_socclk_frequency = metrics.SocclkFrequency;
	gpu_metrics->average_uclk_frequency = metrics.MemclkFrequency;
	gpu_metrics->average_fclk_frequency = metrics.MemclkFrequency;
	gpu_metrics->average_vclk_frequency = metrics.VclkFrequency;
	gpu_metrics->average_dclk_frequency = metrics.DclkFrequency;

	memcpy(&gpu_metrics->current_coreclk[0],
		&metrics.CoreFrequency[0],
		sizeof(uint16_t) * 8);
	gpu_metrics->current_l3clk[0] = metrics.L3Frequency[0];
	gpu_metrics->current_l3clk[1] = metrics.L3Frequency[1];

	gpu_metrics->throttle_status = metrics.ThrottlerStatus;

	*table = (void *)gpu_metrics;

	return sizeof(struct gpu_metrics_v2_0);
}

static int vangogh_od_edit_dpm_table(struct smu_context *smu, enum PP_OD_DPM_TABLE_COMMAND type,
							long input[], uint32_t size)
{
	int ret = 0;

	if (!smu->od_enabled) {
		dev_warn(smu->adev->dev, "Fine grain is not enabled!\n");
		return -EINVAL;
	}

	switch (type) {
	case PP_OD_EDIT_SCLK_VDDC_TABLE:
		if (size != 2) {
			dev_err(smu->adev->dev, "Input parameter number not correct\n");
			return -EINVAL;
		}

		if (input[0] == 0) {
			if (input[1] < smu->gfx_default_hard_min_freq) {
				dev_warn(smu->adev->dev, "Fine grain setting minimum sclk (%ld) MHz is less than the minimum allowed (%d) MHz\n",
					input[1], smu->gfx_default_hard_min_freq);
				return -EINVAL;
			}
			smu->gfx_actual_hard_min_freq = input[1];
		} else if (input[0] == 1) {
			if (input[1] > smu->gfx_default_soft_max_freq) {
				dev_warn(smu->adev->dev, "Fine grain setting maximum sclk (%ld) MHz is greater than the maximum allowed (%d) MHz\n",
					input[1], smu->gfx_default_soft_max_freq);
				return -EINVAL;
			}
			smu->gfx_actual_soft_max_freq = input[1];
		} else {
			return -EINVAL;
		}
		break;
	case PP_OD_RESTORE_DEFAULT_TABLE:
		if (size != 0) {
			dev_err(smu->adev->dev, "Input parameter number not correct\n");
			return -EINVAL;
		} else {
			smu->gfx_actual_hard_min_freq = smu->gfx_default_hard_min_freq;
			smu->gfx_actual_soft_max_freq = smu->gfx_default_soft_max_freq;

			ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetHardMinGfxClk,
									smu->gfx_actual_hard_min_freq, NULL);
			if (ret) {
				dev_err(smu->adev->dev, "Restore the default hard min sclk failed!");
				return ret;
			}

			ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMaxGfxClk,
									smu->gfx_actual_soft_max_freq, NULL);
			if (ret) {
				dev_err(smu->adev->dev, "Restore the default soft max sclk failed!");
				return ret;
			}
		}
		break;
	case PP_OD_COMMIT_DPM_TABLE:
		if (size != 0) {
			dev_err(smu->adev->dev, "Input parameter number not correct\n");
			return -EINVAL;
		} else {
			if (smu->gfx_actual_hard_min_freq > smu->gfx_actual_soft_max_freq) {
				dev_err(smu->adev->dev, "The setting minimun sclk (%d) MHz is greater than the setting maximum sclk (%d) MHz\n",
				smu->gfx_actual_hard_min_freq, smu->gfx_actual_soft_max_freq);
				return -EINVAL;
			}

			ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetHardMinGfxClk,
									smu->gfx_actual_hard_min_freq, NULL);
			if (ret) {
				dev_err(smu->adev->dev, "Set hard min sclk failed!");
				return ret;
			}

			ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMaxGfxClk,
									smu->gfx_actual_soft_max_freq, NULL);
			if (ret) {
				dev_err(smu->adev->dev, "Set soft max sclk failed!");
				return ret;
			}
		}
		break;
	default:
		return -ENOSYS;
	}

	return ret;
}

static int vangogh_set_default_dpm_tables(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;

	return smu_cmn_update_table(smu, SMU_TABLE_DPMCLOCKS, 0, smu_table->clocks_table, false);
}

static int vangogh_set_fine_grain_gfx_freq_parameters(struct smu_context *smu)
{
	DpmClocks_t *clk_table = smu->smu_table.clocks_table;

	smu->gfx_default_hard_min_freq = clk_table->MinGfxClk;
	smu->gfx_default_soft_max_freq = clk_table->MaxGfxClk;
	smu->gfx_actual_hard_min_freq = 0;
	smu->gfx_actual_soft_max_freq = 0;

	return 0;
}

static int vangogh_system_features_control(struct smu_context *smu, bool en)
{
	struct amdgpu_device *adev = smu->adev;

	if (adev->pm.fw_version >= 0x43f1700)
		return smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_RlcPowerNotify,
						en ? RLC_STATUS_NORMAL : RLC_STATUS_OFF, NULL);
	else
		return 0;
}

static const struct pptable_funcs vangogh_ppt_funcs = {

	.check_fw_status = smu_v11_0_check_fw_status,
	.check_fw_version = smu_v11_0_check_fw_version,
	.init_smc_tables = vangogh_init_smc_tables,
	.fini_smc_tables = smu_v11_0_fini_smc_tables,
	.init_power = smu_v11_0_init_power,
	.fini_power = smu_v11_0_fini_power,
	.register_irq_handler = smu_v11_0_register_irq_handler,
	.get_allowed_feature_mask = vangogh_get_allowed_feature_mask,
	.notify_memory_pool_location = smu_v11_0_notify_memory_pool_location,
	.send_smc_msg_with_param = smu_cmn_send_smc_msg_with_param,
	.send_smc_msg = smu_cmn_send_smc_msg,
	.dpm_set_vcn_enable = vangogh_dpm_set_vcn_enable,
	.dpm_set_jpeg_enable = vangogh_dpm_set_jpeg_enable,
	.is_dpm_running = vangogh_is_dpm_running,
	.read_sensor = vangogh_read_sensor,
	.get_enabled_mask = smu_cmn_get_enabled_32_bits_mask,
	.get_pp_feature_mask = smu_cmn_get_pp_feature_mask,
	.set_watermarks_table = vangogh_set_watermarks_table,
	.set_driver_table_location = smu_v11_0_set_driver_table_location,
	.interrupt_work = smu_v11_0_interrupt_work,
	.get_gpu_metrics = vangogh_get_gpu_metrics,
	.od_edit_dpm_table = vangogh_od_edit_dpm_table,
	.print_clk_levels = vangogh_print_fine_grain_clk,
	.set_default_dpm_table = vangogh_set_default_dpm_tables,
	.set_fine_grain_gfx_freq_parameters = vangogh_set_fine_grain_gfx_freq_parameters,
	.system_features_control = vangogh_system_features_control,
};

void vangogh_set_ppt_funcs(struct smu_context *smu)
{
	smu->ppt_funcs = &vangogh_ppt_funcs;
	smu->message_map = vangogh_message_map;
	smu->feature_map = vangogh_feature_mask_map;
	smu->table_map = vangogh_table_map;
	smu->is_apu = true;
}
