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
#include "soc15_common.h"
#include "asic_reg/gc/gc_10_3_0_offset.h"
#include "asic_reg/gc/gc_10_3_0_sh_mask.h"
#include <asm/processor.h>

/*
 * DO NOT use these for err/warn/info/debug messages.
 * Use dev_err, dev_warn, dev_info and dev_dbg instead.
 * They are more MGPU friendly.
 */
#undef pr_err
#undef pr_warn
#undef pr_info
#undef pr_debug

// Registers related to GFXOFF
// addressBlock: smuio_smuio_SmuSmuioDec
// base address: 0x5a000
#define mmSMUIO_GFX_MISC_CNTL			0x00c5
#define mmSMUIO_GFX_MISC_CNTL_BASE_IDX		0

//SMUIO_GFX_MISC_CNTL
#define SMUIO_GFX_MISC_CNTL__SMU_GFX_cold_vs_gfxoff__SHIFT	0x0
#define SMUIO_GFX_MISC_CNTL__PWR_GFXOFF_STATUS__SHIFT		0x1
#define SMUIO_GFX_MISC_CNTL__SMU_GFX_cold_vs_gfxoff_MASK	0x00000001L
#define SMUIO_GFX_MISC_CNTL__PWR_GFXOFF_STATUS_MASK		0x00000006L

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
	MSG_MAP(AllowGfxOff,                    PPSMC_MSG_AllowGfxOff,          0),
	MSG_MAP(DisallowGfxOff,                 PPSMC_MSG_DisallowGfxOff,		0),
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
	MSG_MAP(RequestActiveWgp,                   PPSMC_MSG_RequestActiveWgp,                     0),
	MSG_MAP(SetFastPPTLimit,                    PPSMC_MSG_SetFastPPTLimit,						0),
	MSG_MAP(SetSlowPPTLimit,                    PPSMC_MSG_SetSlowPPTLimit,						0),
	MSG_MAP(GetFastPPTLimit,                    PPSMC_MSG_GetFastPPTLimit,						0),
	MSG_MAP(GetSlowPPTLimit,                    PPSMC_MSG_GetSlowPPTLimit,						0),
	MSG_MAP(GetGfxOffStatus,		    PPSMC_MSG_GetGfxOffStatus,						0),
	MSG_MAP(GetGfxOffEntryCount,		    PPSMC_MSG_GetGfxOffEntryCount,					0),
	MSG_MAP(LogGfxOffResidency,		    PPSMC_MSG_LogGfxOffResidency,					0),
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
	FEA_MAP_REVERSE(SOCCLK),
	FEA_MAP_REVERSE(FCLK),
	FEA_MAP_HALF_REVERSE(GFX),
};

static struct cmn2asic_mapping vangogh_table_map[SMU_TABLE_COUNT] = {
	TAB_MAP_VALID(WATERMARKS),
	TAB_MAP_VALID(SMU_METRICS),
	TAB_MAP_VALID(CUSTOM_DPM),
	TAB_MAP_VALID(DPMCLOCKS),
};

static struct cmn2asic_mapping vangogh_workload_map[PP_SMC_POWER_PROFILE_COUNT] = {
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_FULLSCREEN3D,		WORKLOAD_PPLIB_FULL_SCREEN_3D_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_VIDEO,		WORKLOAD_PPLIB_VIDEO_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_VR,			WORKLOAD_PPLIB_VR_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_COMPUTE,		WORKLOAD_PPLIB_COMPUTE_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_CUSTOM,		WORKLOAD_PPLIB_CUSTOM_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_CAPPED,		WORKLOAD_PPLIB_CAPPED_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_UNCAPPED,		WORKLOAD_PPLIB_UNCAPPED_BIT),
};

static const uint8_t vangogh_throttler_map[] = {
	[THROTTLER_STATUS_BIT_SPL]	= (SMU_THROTTLER_SPL_BIT),
	[THROTTLER_STATUS_BIT_FPPT]	= (SMU_THROTTLER_FPPT_BIT),
	[THROTTLER_STATUS_BIT_SPPT]	= (SMU_THROTTLER_SPPT_BIT),
	[THROTTLER_STATUS_BIT_SPPT_APU]	= (SMU_THROTTLER_SPPT_APU_BIT),
	[THROTTLER_STATUS_BIT_THM_CORE]	= (SMU_THROTTLER_TEMP_CORE_BIT),
	[THROTTLER_STATUS_BIT_THM_GFX]	= (SMU_THROTTLER_TEMP_GPU_BIT),
	[THROTTLER_STATUS_BIT_THM_SOC]	= (SMU_THROTTLER_TEMP_SOC_BIT),
	[THROTTLER_STATUS_BIT_TDC_VDD]	= (SMU_THROTTLER_TDC_VDD_BIT),
	[THROTTLER_STATUS_BIT_TDC_SOC]	= (SMU_THROTTLER_TDC_SOC_BIT),
	[THROTTLER_STATUS_BIT_TDC_GFX]	= (SMU_THROTTLER_TDC_GFX_BIT),
	[THROTTLER_STATUS_BIT_TDC_CVIP]	= (SMU_THROTTLER_TDC_CVIP_BIT),
};

static int vangogh_tables_init(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = smu_table->tables;
	uint32_t if_version;
	uint32_t smu_version;
	uint32_t ret = 0;

	ret = smu_cmn_get_smc_version(smu, &if_version, &smu_version);
	if (ret) {
		return ret;
	}

	SMU_TABLE_INIT(tables, SMU_TABLE_WATERMARKS, sizeof(Watermarks_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_DPMCLOCKS, sizeof(DpmClocks_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_PMSTATUSLOG, SMU11_TOOL_SIZE,
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_ACTIVITY_MONITOR_COEFF, sizeof(DpmActivityMonitorCoeffExt_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);

	if (if_version < 0x3) {
		SMU_TABLE_INIT(tables, SMU_TABLE_SMU_METRICS, sizeof(SmuMetrics_legacy_t),
				PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
		smu_table->metrics_table = kzalloc(sizeof(SmuMetrics_legacy_t), GFP_KERNEL);
	} else {
		SMU_TABLE_INIT(tables, SMU_TABLE_SMU_METRICS, sizeof(SmuMetrics_t),
				PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
		smu_table->metrics_table = kzalloc(sizeof(SmuMetrics_t), GFP_KERNEL);
	}
	if (!smu_table->metrics_table)
		goto err0_out;
	smu_table->metrics_time = 0;

	if (smu_version >= 0x043F3E00)
		smu_table->gpu_metrics_table_size = sizeof(struct gpu_metrics_v2_3);
	else
		smu_table->gpu_metrics_table_size = sizeof(struct gpu_metrics_v2_2);
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
	kfree(smu_table->watermarks_table);
err2_out:
	kfree(smu_table->gpu_metrics_table);
err1_out:
	kfree(smu_table->metrics_table);
err0_out:
	return -ENOMEM;
}

static int vangogh_get_legacy_smu_metrics_data(struct smu_context *smu,
				       MetricsMember_t member,
				       uint32_t *value)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	SmuMetrics_legacy_t *metrics = (SmuMetrics_legacy_t *)smu_table->metrics_table;
	int ret = 0;

	ret = smu_cmn_get_metrics_table(smu,
					NULL,
					false);
	if (ret)
		return ret;

	switch (member) {
	case METRICS_CURR_GFXCLK:
		*value = metrics->GfxclkFrequency;
		break;
	case METRICS_AVERAGE_SOCCLK:
		*value = metrics->SocclkFrequency;
		break;
	case METRICS_AVERAGE_VCLK:
		*value = metrics->VclkFrequency;
		break;
	case METRICS_AVERAGE_DCLK:
		*value = metrics->DclkFrequency;
		break;
	case METRICS_CURR_UCLK:
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
	case METRICS_AVERAGE_CPUCLK:
		memcpy(value, &metrics->CoreFrequency[0],
		       smu->cpu_core_num * sizeof(uint16_t));
		break;
	default:
		*value = UINT_MAX;
		break;
	}

	return ret;
}

static int vangogh_get_smu_metrics_data(struct smu_context *smu,
				       MetricsMember_t member,
				       uint32_t *value)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	SmuMetrics_t *metrics = (SmuMetrics_t *)smu_table->metrics_table;
	int ret = 0;

	ret = smu_cmn_get_metrics_table(smu,
					NULL,
					false);
	if (ret)
		return ret;

	switch (member) {
	case METRICS_CURR_GFXCLK:
		*value = metrics->Current.GfxclkFrequency;
		break;
	case METRICS_AVERAGE_SOCCLK:
		*value = metrics->Current.SocclkFrequency;
		break;
	case METRICS_AVERAGE_VCLK:
		*value = metrics->Current.VclkFrequency;
		break;
	case METRICS_AVERAGE_DCLK:
		*value = metrics->Current.DclkFrequency;
		break;
	case METRICS_CURR_UCLK:
		*value = metrics->Current.MemclkFrequency;
		break;
	case METRICS_AVERAGE_GFXACTIVITY:
		*value = metrics->Current.GfxActivity;
		break;
	case METRICS_AVERAGE_VCNACTIVITY:
		*value = metrics->Current.UvdActivity;
		break;
	case METRICS_AVERAGE_SOCKETPOWER:
		*value = (metrics->Current.CurrentSocketPower << 8) /
		1000;
		break;
	case METRICS_TEMPERATURE_EDGE:
		*value = metrics->Current.GfxTemperature / 100 *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_TEMPERATURE_HOTSPOT:
		*value = metrics->Current.SocTemperature / 100 *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_THROTTLER_STATUS:
		*value = metrics->Current.ThrottlerStatus;
		break;
	case METRICS_VOLTAGE_VDDGFX:
		*value = metrics->Current.Voltage[2];
		break;
	case METRICS_VOLTAGE_VDDSOC:
		*value = metrics->Current.Voltage[1];
		break;
	case METRICS_AVERAGE_CPUCLK:
		memcpy(value, &metrics->Current.CoreFrequency[0],
		       smu->cpu_core_num * sizeof(uint16_t));
		break;
	default:
		*value = UINT_MAX;
		break;
	}

	return ret;
}

static int vangogh_common_get_smu_metrics_data(struct smu_context *smu,
				       MetricsMember_t member,
				       uint32_t *value)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t if_version;
	int ret = 0;

	ret = smu_cmn_get_smc_version(smu, &if_version, NULL);
	if (ret) {
		dev_err(adev->dev, "Failed to get smu if version!\n");
		return ret;
	}

	if (if_version < 0x3)
		ret = vangogh_get_legacy_smu_metrics_data(smu, member, value);
	else
		ret = vangogh_get_smu_metrics_data(smu, member, value);

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

#ifdef CONFIG_X86
	/* AMD x86 APU only */
	smu->cpu_core_num = boot_cpu_data.x86_max_cores;
#else
	smu->cpu_core_num = 4;
#endif

	return smu_v11_0_init_smc_tables(smu);
}

static int vangogh_dpm_set_vcn_enable(struct smu_context *smu, bool enable)
{
	int ret = 0;

	if (enable) {
		/* vcn dpm on is a prerequisite for vcn power gate messages */
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_PowerUpVcn, 0, NULL);
		if (ret)
			return ret;
	} else {
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_PowerDownVcn, 0, NULL);
		if (ret)
			return ret;
	}

	return ret;
}

static int vangogh_dpm_set_jpeg_enable(struct smu_context *smu, bool enable)
{
	int ret = 0;

	if (enable) {
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_PowerUpJpeg, 0, NULL);
		if (ret)
			return ret;
	} else {
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_PowerDownJpeg, 0, NULL);
		if (ret)
			return ret;
	}

	return ret;
}

static bool vangogh_is_dpm_running(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;
	uint64_t feature_enabled;

	/* we need to re-init after suspend so return false */
	if (adev->in_suspend)
		return false;

	ret = smu_cmn_get_enabled_mask(smu, &feature_enabled);

	if (ret)
		return false;

	return !!(feature_enabled & SMC_DPM_FEATURE);
}

static int vangogh_get_dpm_clk_limited(struct smu_context *smu, enum smu_clk_type clk_type,
						uint32_t dpm_level, uint32_t *freq)
{
	DpmClocks_t *clk_table = smu->smu_table.clocks_table;

	if (!clk_table || clk_type >= SMU_CLK_COUNT)
		return -EINVAL;

	switch (clk_type) {
	case SMU_SOCCLK:
		if (dpm_level >= clk_table->NumSocClkLevelsEnabled)
			return -EINVAL;
		*freq = clk_table->SocClocks[dpm_level];
		break;
	case SMU_VCLK:
		if (dpm_level >= clk_table->VcnClkLevelsEnabled)
			return -EINVAL;
		*freq = clk_table->VcnClocks[dpm_level].vclk;
		break;
	case SMU_DCLK:
		if (dpm_level >= clk_table->VcnClkLevelsEnabled)
			return -EINVAL;
		*freq = clk_table->VcnClocks[dpm_level].dclk;
		break;
	case SMU_UCLK:
	case SMU_MCLK:
		if (dpm_level >= clk_table->NumDfPstatesEnabled)
			return -EINVAL;
		*freq = clk_table->DfPstateTable[dpm_level].memclk;

		break;
	case SMU_FCLK:
		if (dpm_level >= clk_table->NumDfPstatesEnabled)
			return -EINVAL;
		*freq = clk_table->DfPstateTable[dpm_level].fclk;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int vangogh_print_legacy_clk_levels(struct smu_context *smu,
			enum smu_clk_type clk_type, char *buf)
{
	DpmClocks_t *clk_table = smu->smu_table.clocks_table;
	SmuMetrics_legacy_t metrics;
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);
	int i, size = 0, ret = 0;
	uint32_t cur_value = 0, value = 0, count = 0;
	bool cur_value_match_level = false;

	memset(&metrics, 0, sizeof(metrics));

	ret = smu_cmn_get_metrics_table(smu, &metrics, false);
	if (ret)
		return ret;

	smu_cmn_get_sysfs_buf(&buf, &size);

	switch (clk_type) {
	case SMU_OD_SCLK:
		if (smu_dpm_ctx->dpm_level == AMD_DPM_FORCED_LEVEL_MANUAL) {
			size += sysfs_emit_at(buf, size, "%s:\n", "OD_SCLK");
			size += sysfs_emit_at(buf, size, "0: %10uMhz\n",
			(smu->gfx_actual_hard_min_freq > 0) ? smu->gfx_actual_hard_min_freq : smu->gfx_default_hard_min_freq);
			size += sysfs_emit_at(buf, size, "1: %10uMhz\n",
			(smu->gfx_actual_soft_max_freq > 0) ? smu->gfx_actual_soft_max_freq : smu->gfx_default_soft_max_freq);
		}
		break;
	case SMU_OD_CCLK:
		if (smu_dpm_ctx->dpm_level == AMD_DPM_FORCED_LEVEL_MANUAL) {
			size += sysfs_emit_at(buf, size, "CCLK_RANGE in Core%d:\n",  smu->cpu_core_id_select);
			size += sysfs_emit_at(buf, size, "0: %10uMhz\n",
			(smu->cpu_actual_soft_min_freq > 0) ? smu->cpu_actual_soft_min_freq : smu->cpu_default_soft_min_freq);
			size += sysfs_emit_at(buf, size, "1: %10uMhz\n",
			(smu->cpu_actual_soft_max_freq > 0) ? smu->cpu_actual_soft_max_freq : smu->cpu_default_soft_max_freq);
		}
		break;
	case SMU_OD_RANGE:
		if (smu_dpm_ctx->dpm_level == AMD_DPM_FORCED_LEVEL_MANUAL) {
			size += sysfs_emit_at(buf, size, "%s:\n", "OD_RANGE");
			size += sysfs_emit_at(buf, size, "SCLK: %7uMhz %10uMhz\n",
				smu->gfx_default_hard_min_freq, smu->gfx_default_soft_max_freq);
			size += sysfs_emit_at(buf, size, "CCLK: %7uMhz %10uMhz\n",
				smu->cpu_default_soft_min_freq, smu->cpu_default_soft_max_freq);
		}
		break;
	case SMU_SOCCLK:
		/* the level 3 ~ 6 of socclk use the same frequency for vangogh */
		count = clk_table->NumSocClkLevelsEnabled;
		cur_value = metrics.SocclkFrequency;
		break;
	case SMU_VCLK:
		count = clk_table->VcnClkLevelsEnabled;
		cur_value = metrics.VclkFrequency;
		break;
	case SMU_DCLK:
		count = clk_table->VcnClkLevelsEnabled;
		cur_value = metrics.DclkFrequency;
		break;
	case SMU_MCLK:
		count = clk_table->NumDfPstatesEnabled;
		cur_value = metrics.MemclkFrequency;
		break;
	case SMU_FCLK:
		count = clk_table->NumDfPstatesEnabled;
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_GetFclkFrequency, 0, &cur_value);
		if (ret)
			return ret;
		break;
	default:
		break;
	}

	switch (clk_type) {
	case SMU_SOCCLK:
	case SMU_VCLK:
	case SMU_DCLK:
	case SMU_MCLK:
	case SMU_FCLK:
		for (i = 0; i < count; i++) {
			ret = vangogh_get_dpm_clk_limited(smu, clk_type, i, &value);
			if (ret)
				return ret;
			if (!value)
				continue;
			size += sysfs_emit_at(buf, size, "%d: %uMhz %s\n", i, value,
					cur_value == value ? "*" : "");
			if (cur_value == value)
				cur_value_match_level = true;
		}

		if (!cur_value_match_level)
			size += sysfs_emit_at(buf, size, "   %uMhz *\n", cur_value);
		break;
	default:
		break;
	}

	return size;
}

static int vangogh_print_clk_levels(struct smu_context *smu,
			enum smu_clk_type clk_type, char *buf)
{
	DpmClocks_t *clk_table = smu->smu_table.clocks_table;
	SmuMetrics_t metrics;
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);
	int i, size = 0, ret = 0;
	uint32_t cur_value = 0, value = 0, count = 0;
	bool cur_value_match_level = false;
	uint32_t min, max;

	memset(&metrics, 0, sizeof(metrics));

	ret = smu_cmn_get_metrics_table(smu, &metrics, false);
	if (ret)
		return ret;

	smu_cmn_get_sysfs_buf(&buf, &size);

	switch (clk_type) {
	case SMU_OD_SCLK:
		if (smu_dpm_ctx->dpm_level == AMD_DPM_FORCED_LEVEL_MANUAL) {
			size += sysfs_emit_at(buf, size, "%s:\n", "OD_SCLK");
			size += sysfs_emit_at(buf, size, "0: %10uMhz\n",
			(smu->gfx_actual_hard_min_freq > 0) ? smu->gfx_actual_hard_min_freq : smu->gfx_default_hard_min_freq);
			size += sysfs_emit_at(buf, size, "1: %10uMhz\n",
			(smu->gfx_actual_soft_max_freq > 0) ? smu->gfx_actual_soft_max_freq : smu->gfx_default_soft_max_freq);
		}
		break;
	case SMU_OD_CCLK:
		if (smu_dpm_ctx->dpm_level == AMD_DPM_FORCED_LEVEL_MANUAL) {
			size += sysfs_emit_at(buf, size, "CCLK_RANGE in Core%d:\n",  smu->cpu_core_id_select);
			size += sysfs_emit_at(buf, size, "0: %10uMhz\n",
			(smu->cpu_actual_soft_min_freq > 0) ? smu->cpu_actual_soft_min_freq : smu->cpu_default_soft_min_freq);
			size += sysfs_emit_at(buf, size, "1: %10uMhz\n",
			(smu->cpu_actual_soft_max_freq > 0) ? smu->cpu_actual_soft_max_freq : smu->cpu_default_soft_max_freq);
		}
		break;
	case SMU_OD_RANGE:
		if (smu_dpm_ctx->dpm_level == AMD_DPM_FORCED_LEVEL_MANUAL) {
			size += sysfs_emit_at(buf, size, "%s:\n", "OD_RANGE");
			size += sysfs_emit_at(buf, size, "SCLK: %7uMhz %10uMhz\n",
				smu->gfx_default_hard_min_freq, smu->gfx_default_soft_max_freq);
			size += sysfs_emit_at(buf, size, "CCLK: %7uMhz %10uMhz\n",
				smu->cpu_default_soft_min_freq, smu->cpu_default_soft_max_freq);
		}
		break;
	case SMU_SOCCLK:
		/* the level 3 ~ 6 of socclk use the same frequency for vangogh */
		count = clk_table->NumSocClkLevelsEnabled;
		cur_value = metrics.Current.SocclkFrequency;
		break;
	case SMU_VCLK:
		count = clk_table->VcnClkLevelsEnabled;
		cur_value = metrics.Current.VclkFrequency;
		break;
	case SMU_DCLK:
		count = clk_table->VcnClkLevelsEnabled;
		cur_value = metrics.Current.DclkFrequency;
		break;
	case SMU_MCLK:
		count = clk_table->NumDfPstatesEnabled;
		cur_value = metrics.Current.MemclkFrequency;
		break;
	case SMU_FCLK:
		count = clk_table->NumDfPstatesEnabled;
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_GetFclkFrequency, 0, &cur_value);
		if (ret)
			return ret;
		break;
	case SMU_GFXCLK:
	case SMU_SCLK:
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_GetGfxclkFrequency, 0, &cur_value);
		if (ret) {
			return ret;
		}
		break;
	default:
		break;
	}

	switch (clk_type) {
	case SMU_SOCCLK:
	case SMU_VCLK:
	case SMU_DCLK:
	case SMU_MCLK:
	case SMU_FCLK:
		for (i = 0; i < count; i++) {
			ret = vangogh_get_dpm_clk_limited(smu, clk_type, i, &value);
			if (ret)
				return ret;
			if (!value)
				continue;
			size += sysfs_emit_at(buf, size, "%d: %uMhz %s\n", i, value,
					cur_value == value ? "*" : "");
			if (cur_value == value)
				cur_value_match_level = true;
		}

		if (!cur_value_match_level)
			size += sysfs_emit_at(buf, size, "   %uMhz *\n", cur_value);
		break;
	case SMU_GFXCLK:
	case SMU_SCLK:
		min = (smu->gfx_actual_hard_min_freq > 0) ? smu->gfx_actual_hard_min_freq : smu->gfx_default_hard_min_freq;
		max = (smu->gfx_actual_soft_max_freq > 0) ? smu->gfx_actual_soft_max_freq : smu->gfx_default_soft_max_freq;
		if (cur_value  == max)
			i = 2;
		else if (cur_value == min)
			i = 0;
		else
			i = 1;
		size += sysfs_emit_at(buf, size, "0: %uMhz %s\n", min,
				i == 0 ? "*" : "");
		size += sysfs_emit_at(buf, size, "1: %uMhz %s\n",
				i == 1 ? cur_value : VANGOGH_UMD_PSTATE_STANDARD_GFXCLK,
				i == 1 ? "*" : "");
		size += sysfs_emit_at(buf, size, "2: %uMhz %s\n", max,
				i == 2 ? "*" : "");
		break;
	default:
		break;
	}

	return size;
}

static int vangogh_common_print_clk_levels(struct smu_context *smu,
			enum smu_clk_type clk_type, char *buf)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t if_version;
	int ret = 0;

	ret = smu_cmn_get_smc_version(smu, &if_version, NULL);
	if (ret) {
		dev_err(adev->dev, "Failed to get smu if version!\n");
		return ret;
	}

	if (if_version < 0x3)
		ret = vangogh_print_legacy_clk_levels(smu, clk_type, buf);
	else
		ret = vangogh_print_clk_levels(smu, clk_type, buf);

	return ret;
}

static int vangogh_get_profiling_clk_mask(struct smu_context *smu,
					 enum amd_dpm_forced_level level,
					 uint32_t *vclk_mask,
					 uint32_t *dclk_mask,
					 uint32_t *mclk_mask,
					 uint32_t *fclk_mask,
					 uint32_t *soc_mask)
{
	DpmClocks_t *clk_table = smu->smu_table.clocks_table;

	if (level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK) {
		if (mclk_mask)
			*mclk_mask = clk_table->NumDfPstatesEnabled - 1;

		if (fclk_mask)
			*fclk_mask = clk_table->NumDfPstatesEnabled - 1;

		if (soc_mask)
			*soc_mask = 0;
	} else if (level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) {
		if (mclk_mask)
			*mclk_mask = 0;

		if (fclk_mask)
			*fclk_mask = 0;

		if (soc_mask)
			*soc_mask = 1;

		if (vclk_mask)
			*vclk_mask = 1;

		if (dclk_mask)
			*dclk_mask = 1;
	} else if (level == AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD) {
		if (mclk_mask)
			*mclk_mask = 0;

		if (fclk_mask)
			*fclk_mask = 0;

		if (soc_mask)
			*soc_mask = 1;

		if (vclk_mask)
			*vclk_mask = 1;

		if (dclk_mask)
			*dclk_mask = 1;
	}

	return 0;
}

static bool vangogh_clk_dpm_is_enabled(struct smu_context *smu,
				enum smu_clk_type clk_type)
{
	enum smu_feature_mask feature_id = 0;

	switch (clk_type) {
	case SMU_MCLK:
	case SMU_UCLK:
	case SMU_FCLK:
		feature_id = SMU_FEATURE_DPM_FCLK_BIT;
		break;
	case SMU_GFXCLK:
	case SMU_SCLK:
		feature_id = SMU_FEATURE_DPM_GFXCLK_BIT;
		break;
	case SMU_SOCCLK:
		feature_id = SMU_FEATURE_DPM_SOCCLK_BIT;
		break;
	case SMU_VCLK:
	case SMU_DCLK:
		feature_id = SMU_FEATURE_VCN_DPM_BIT;
		break;
	default:
		return true;
	}

	if (!smu_cmn_feature_is_enabled(smu, feature_id))
		return false;

	return true;
}

static int vangogh_get_dpm_ultimate_freq(struct smu_context *smu,
					enum smu_clk_type clk_type,
					uint32_t *min,
					uint32_t *max)
{
	int ret = 0;
	uint32_t soc_mask;
	uint32_t vclk_mask;
	uint32_t dclk_mask;
	uint32_t mclk_mask;
	uint32_t fclk_mask;
	uint32_t clock_limit;

	if (!vangogh_clk_dpm_is_enabled(smu, clk_type)) {
		switch (clk_type) {
		case SMU_MCLK:
		case SMU_UCLK:
			clock_limit = smu->smu_table.boot_values.uclk;
			break;
		case SMU_FCLK:
			clock_limit = smu->smu_table.boot_values.fclk;
			break;
		case SMU_GFXCLK:
		case SMU_SCLK:
			clock_limit = smu->smu_table.boot_values.gfxclk;
			break;
		case SMU_SOCCLK:
			clock_limit = smu->smu_table.boot_values.socclk;
			break;
		case SMU_VCLK:
			clock_limit = smu->smu_table.boot_values.vclk;
			break;
		case SMU_DCLK:
			clock_limit = smu->smu_table.boot_values.dclk;
			break;
		default:
			clock_limit = 0;
			break;
		}

		/* clock in Mhz unit */
		if (min)
			*min = clock_limit / 100;
		if (max)
			*max = clock_limit / 100;

		return 0;
	}
	if (max) {
		ret = vangogh_get_profiling_clk_mask(smu,
							AMD_DPM_FORCED_LEVEL_PROFILE_PEAK,
							&vclk_mask,
							&dclk_mask,
							&mclk_mask,
							&fclk_mask,
							&soc_mask);
		if (ret)
			goto failed;

		switch (clk_type) {
		case SMU_UCLK:
		case SMU_MCLK:
			ret = vangogh_get_dpm_clk_limited(smu, clk_type, mclk_mask, max);
			if (ret)
				goto failed;
			break;
		case SMU_SOCCLK:
			ret = vangogh_get_dpm_clk_limited(smu, clk_type, soc_mask, max);
			if (ret)
				goto failed;
			break;
		case SMU_FCLK:
			ret = vangogh_get_dpm_clk_limited(smu, clk_type, fclk_mask, max);
			if (ret)
				goto failed;
			break;
		case SMU_VCLK:
			ret = vangogh_get_dpm_clk_limited(smu, clk_type, vclk_mask, max);
			if (ret)
				goto failed;
			break;
		case SMU_DCLK:
			ret = vangogh_get_dpm_clk_limited(smu, clk_type, dclk_mask, max);
			if (ret)
				goto failed;
			break;
		default:
			ret = -EINVAL;
			goto failed;
		}
	}
	if (min) {
		switch (clk_type) {
		case SMU_UCLK:
		case SMU_MCLK:
			ret = vangogh_get_dpm_clk_limited(smu, clk_type, mclk_mask, min);
			if (ret)
				goto failed;
			break;
		case SMU_SOCCLK:
			ret = vangogh_get_dpm_clk_limited(smu, clk_type, soc_mask, min);
			if (ret)
				goto failed;
			break;
		case SMU_FCLK:
			ret = vangogh_get_dpm_clk_limited(smu, clk_type, fclk_mask, min);
			if (ret)
				goto failed;
			break;
		case SMU_VCLK:
			ret = vangogh_get_dpm_clk_limited(smu, clk_type, vclk_mask, min);
			if (ret)
				goto failed;
			break;
		case SMU_DCLK:
			ret = vangogh_get_dpm_clk_limited(smu, clk_type, dclk_mask, min);
			if (ret)
				goto failed;
			break;
		default:
			ret = -EINVAL;
			goto failed;
		}
	}
failed:
	return ret;
}

static int vangogh_get_power_profile_mode(struct smu_context *smu,
					   char *buf)
{
	uint32_t i, size = 0;
	int16_t workload_type = 0;

	if (!buf)
		return -EINVAL;

	for (i = 0; i < PP_SMC_POWER_PROFILE_COUNT; i++) {
		/*
		 * Conv PP_SMC_POWER_PROFILE* to WORKLOAD_PPLIB_*_BIT
		 * Not all profile modes are supported on vangogh.
		 */
		workload_type = smu_cmn_to_asic_specific_index(smu,
							       CMN2ASIC_MAPPING_WORKLOAD,
							       i);

		if (workload_type < 0)
			continue;

		size += sysfs_emit_at(buf, size, "%2d %14s%s\n",
			i, amdgpu_pp_profile_name[i], (i == smu->power_profile_mode) ? "*" : " ");
	}

	return size;
}

static int vangogh_set_power_profile_mode(struct smu_context *smu, long *input, uint32_t size)
{
	int workload_type, ret;
	uint32_t profile_mode = input[size];

	if (profile_mode >= PP_SMC_POWER_PROFILE_COUNT) {
		dev_err(smu->adev->dev, "Invalid power profile mode %d\n", profile_mode);
		return -EINVAL;
	}

	if (profile_mode == PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT ||
			profile_mode == PP_SMC_POWER_PROFILE_POWERSAVING)
		return 0;

	/* conv PP_SMC_POWER_PROFILE* to WORKLOAD_PPLIB_*_BIT */
	workload_type = smu_cmn_to_asic_specific_index(smu,
						       CMN2ASIC_MAPPING_WORKLOAD,
						       profile_mode);
	if (workload_type < 0) {
		dev_dbg(smu->adev->dev, "Unsupported power profile mode %d on VANGOGH\n",
					profile_mode);
		return -EINVAL;
	}

	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_ActiveProcessNotify,
				    1 << workload_type,
				    NULL);
	if (ret) {
		dev_err_once(smu->adev->dev, "Fail to set workload type %d\n",
					workload_type);
		return ret;
	}

	smu->power_profile_mode = profile_mode;

	return 0;
}

static int vangogh_set_soft_freq_limited_range(struct smu_context *smu,
					  enum smu_clk_type clk_type,
					  uint32_t min,
					  uint32_t max)
{
	int ret = 0;

	if (!vangogh_clk_dpm_is_enabled(smu, clk_type))
		return 0;

	switch (clk_type) {
	case SMU_GFXCLK:
	case SMU_SCLK:
		ret = smu_cmn_send_smc_msg_with_param(smu,
							SMU_MSG_SetHardMinGfxClk,
							min, NULL);
		if (ret)
			return ret;

		ret = smu_cmn_send_smc_msg_with_param(smu,
							SMU_MSG_SetSoftMaxGfxClk,
							max, NULL);
		if (ret)
			return ret;
		break;
	case SMU_FCLK:
		ret = smu_cmn_send_smc_msg_with_param(smu,
							SMU_MSG_SetHardMinFclkByFreq,
							min, NULL);
		if (ret)
			return ret;

		ret = smu_cmn_send_smc_msg_with_param(smu,
							SMU_MSG_SetSoftMaxFclkByFreq,
							max, NULL);
		if (ret)
			return ret;
		break;
	case SMU_SOCCLK:
		ret = smu_cmn_send_smc_msg_with_param(smu,
							SMU_MSG_SetHardMinSocclkByFreq,
							min, NULL);
		if (ret)
			return ret;

		ret = smu_cmn_send_smc_msg_with_param(smu,
							SMU_MSG_SetSoftMaxSocclkByFreq,
							max, NULL);
		if (ret)
			return ret;
		break;
	case SMU_VCLK:
		ret = smu_cmn_send_smc_msg_with_param(smu,
							SMU_MSG_SetHardMinVcn,
							min << 16, NULL);
		if (ret)
			return ret;
		ret = smu_cmn_send_smc_msg_with_param(smu,
							SMU_MSG_SetSoftMaxVcn,
							max << 16, NULL);
		if (ret)
			return ret;
		break;
	case SMU_DCLK:
		ret = smu_cmn_send_smc_msg_with_param(smu,
							SMU_MSG_SetHardMinVcn,
							min, NULL);
		if (ret)
			return ret;
		ret = smu_cmn_send_smc_msg_with_param(smu,
							SMU_MSG_SetSoftMaxVcn,
							max, NULL);
		if (ret)
			return ret;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int vangogh_force_clk_levels(struct smu_context *smu,
				   enum smu_clk_type clk_type, uint32_t mask)
{
	uint32_t soft_min_level = 0, soft_max_level = 0;
	uint32_t min_freq = 0, max_freq = 0;
	int ret = 0 ;

	soft_min_level = mask ? (ffs(mask) - 1) : 0;
	soft_max_level = mask ? (fls(mask) - 1) : 0;

	switch (clk_type) {
	case SMU_SOCCLK:
		ret = vangogh_get_dpm_clk_limited(smu, clk_type,
						soft_min_level, &min_freq);
		if (ret)
			return ret;
		ret = vangogh_get_dpm_clk_limited(smu, clk_type,
						soft_max_level, &max_freq);
		if (ret)
			return ret;
		ret = smu_cmn_send_smc_msg_with_param(smu,
								SMU_MSG_SetSoftMaxSocclkByFreq,
								max_freq, NULL);
		if (ret)
			return ret;
		ret = smu_cmn_send_smc_msg_with_param(smu,
								SMU_MSG_SetHardMinSocclkByFreq,
								min_freq, NULL);
		if (ret)
			return ret;
		break;
	case SMU_FCLK:
		ret = vangogh_get_dpm_clk_limited(smu,
							clk_type, soft_min_level, &min_freq);
		if (ret)
			return ret;
		ret = vangogh_get_dpm_clk_limited(smu,
							clk_type, soft_max_level, &max_freq);
		if (ret)
			return ret;
		ret = smu_cmn_send_smc_msg_with_param(smu,
								SMU_MSG_SetSoftMaxFclkByFreq,
								max_freq, NULL);
		if (ret)
			return ret;
		ret = smu_cmn_send_smc_msg_with_param(smu,
								SMU_MSG_SetHardMinFclkByFreq,
								min_freq, NULL);
		if (ret)
			return ret;
		break;
	case SMU_VCLK:
		ret = vangogh_get_dpm_clk_limited(smu,
							clk_type, soft_min_level, &min_freq);
		if (ret)
			return ret;

		ret = vangogh_get_dpm_clk_limited(smu,
							clk_type, soft_max_level, &max_freq);
		if (ret)
			return ret;


		ret = smu_cmn_send_smc_msg_with_param(smu,
								SMU_MSG_SetHardMinVcn,
								min_freq << 16, NULL);
		if (ret)
			return ret;

		ret = smu_cmn_send_smc_msg_with_param(smu,
								SMU_MSG_SetSoftMaxVcn,
								max_freq << 16, NULL);
		if (ret)
			return ret;

		break;
	case SMU_DCLK:
		ret = vangogh_get_dpm_clk_limited(smu,
							clk_type, soft_min_level, &min_freq);
		if (ret)
			return ret;

		ret = vangogh_get_dpm_clk_limited(smu,
							clk_type, soft_max_level, &max_freq);
		if (ret)
			return ret;

		ret = smu_cmn_send_smc_msg_with_param(smu,
							SMU_MSG_SetHardMinVcn,
							min_freq, NULL);
		if (ret)
			return ret;

		ret = smu_cmn_send_smc_msg_with_param(smu,
							SMU_MSG_SetSoftMaxVcn,
							max_freq, NULL);
		if (ret)
			return ret;

		break;
	default:
		break;
	}

	return ret;
}

static int vangogh_force_dpm_limit_value(struct smu_context *smu, bool highest)
{
	int ret = 0, i = 0;
	uint32_t min_freq, max_freq, force_freq;
	enum smu_clk_type clk_type;

	enum smu_clk_type clks[] = {
		SMU_SOCCLK,
		SMU_VCLK,
		SMU_DCLK,
		SMU_FCLK,
	};

	for (i = 0; i < ARRAY_SIZE(clks); i++) {
		clk_type = clks[i];
		ret = vangogh_get_dpm_ultimate_freq(smu, clk_type, &min_freq, &max_freq);
		if (ret)
			return ret;

		force_freq = highest ? max_freq : min_freq;
		ret = vangogh_set_soft_freq_limited_range(smu, clk_type, force_freq, force_freq);
		if (ret)
			return ret;
	}

	return ret;
}

static int vangogh_unforce_dpm_levels(struct smu_context *smu)
{
	int ret = 0, i = 0;
	uint32_t min_freq, max_freq;
	enum smu_clk_type clk_type;

	struct clk_feature_map {
		enum smu_clk_type clk_type;
		uint32_t	feature;
	} clk_feature_map[] = {
		{SMU_FCLK, SMU_FEATURE_DPM_FCLK_BIT},
		{SMU_SOCCLK, SMU_FEATURE_DPM_SOCCLK_BIT},
		{SMU_VCLK, SMU_FEATURE_VCN_DPM_BIT},
		{SMU_DCLK, SMU_FEATURE_VCN_DPM_BIT},
	};

	for (i = 0; i < ARRAY_SIZE(clk_feature_map); i++) {

		if (!smu_cmn_feature_is_enabled(smu, clk_feature_map[i].feature))
		    continue;

		clk_type = clk_feature_map[i].clk_type;

		ret = vangogh_get_dpm_ultimate_freq(smu, clk_type, &min_freq, &max_freq);

		if (ret)
			return ret;

		ret = vangogh_set_soft_freq_limited_range(smu, clk_type, min_freq, max_freq);

		if (ret)
			return ret;
	}

	return ret;
}

static int vangogh_set_peak_clock_by_device(struct smu_context *smu)
{
	int ret = 0;
	uint32_t socclk_freq = 0, fclk_freq = 0;
	uint32_t vclk_freq = 0, dclk_freq = 0;

	ret = vangogh_get_dpm_ultimate_freq(smu, SMU_FCLK, NULL, &fclk_freq);
	if (ret)
		return ret;

	ret = vangogh_set_soft_freq_limited_range(smu, SMU_FCLK, fclk_freq, fclk_freq);
	if (ret)
		return ret;

	ret = vangogh_get_dpm_ultimate_freq(smu, SMU_SOCCLK, NULL, &socclk_freq);
	if (ret)
		return ret;

	ret = vangogh_set_soft_freq_limited_range(smu, SMU_SOCCLK, socclk_freq, socclk_freq);
	if (ret)
		return ret;

	ret = vangogh_get_dpm_ultimate_freq(smu, SMU_VCLK, NULL, &vclk_freq);
	if (ret)
		return ret;

	ret = vangogh_set_soft_freq_limited_range(smu, SMU_VCLK, vclk_freq, vclk_freq);
	if (ret)
		return ret;

	ret = vangogh_get_dpm_ultimate_freq(smu, SMU_DCLK, NULL, &dclk_freq);
	if (ret)
		return ret;

	ret = vangogh_set_soft_freq_limited_range(smu, SMU_DCLK, dclk_freq, dclk_freq);
	if (ret)
		return ret;

	return ret;
}

static int vangogh_set_performance_level(struct smu_context *smu,
					enum amd_dpm_forced_level level)
{
	int ret = 0, i;
	uint32_t soc_mask, mclk_mask, fclk_mask;
	uint32_t vclk_mask = 0, dclk_mask = 0;

	smu->cpu_actual_soft_min_freq = smu->cpu_default_soft_min_freq;
	smu->cpu_actual_soft_max_freq = smu->cpu_default_soft_max_freq;

	switch (level) {
	case AMD_DPM_FORCED_LEVEL_HIGH:
		smu->gfx_actual_hard_min_freq = smu->gfx_default_soft_max_freq;
		smu->gfx_actual_soft_max_freq = smu->gfx_default_soft_max_freq;


		ret = vangogh_force_dpm_limit_value(smu, true);
		if (ret)
			return ret;
		break;
	case AMD_DPM_FORCED_LEVEL_LOW:
		smu->gfx_actual_hard_min_freq = smu->gfx_default_hard_min_freq;
		smu->gfx_actual_soft_max_freq = smu->gfx_default_hard_min_freq;

		ret = vangogh_force_dpm_limit_value(smu, false);
		if (ret)
			return ret;
		break;
	case AMD_DPM_FORCED_LEVEL_AUTO:
		smu->gfx_actual_hard_min_freq = smu->gfx_default_hard_min_freq;
		smu->gfx_actual_soft_max_freq = smu->gfx_default_soft_max_freq;

		ret = vangogh_unforce_dpm_levels(smu);
		if (ret)
			return ret;
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD:
		smu->gfx_actual_hard_min_freq = VANGOGH_UMD_PSTATE_STANDARD_GFXCLK;
		smu->gfx_actual_soft_max_freq = VANGOGH_UMD_PSTATE_STANDARD_GFXCLK;

		ret = vangogh_get_profiling_clk_mask(smu, level,
							&vclk_mask,
							&dclk_mask,
							&mclk_mask,
							&fclk_mask,
							&soc_mask);
		if (ret)
			return ret;

		vangogh_force_clk_levels(smu, SMU_FCLK, 1 << fclk_mask);
		vangogh_force_clk_levels(smu, SMU_SOCCLK, 1 << soc_mask);
		vangogh_force_clk_levels(smu, SMU_VCLK, 1 << vclk_mask);
		vangogh_force_clk_levels(smu, SMU_DCLK, 1 << dclk_mask);
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK:
		smu->gfx_actual_hard_min_freq = smu->gfx_default_hard_min_freq;
		smu->gfx_actual_soft_max_freq = smu->gfx_default_hard_min_freq;
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK:
		smu->gfx_actual_hard_min_freq = smu->gfx_default_hard_min_freq;
		smu->gfx_actual_soft_max_freq = smu->gfx_default_soft_max_freq;

		ret = vangogh_get_profiling_clk_mask(smu, level,
							NULL,
							NULL,
							&mclk_mask,
							&fclk_mask,
							NULL);
		if (ret)
			return ret;

		vangogh_force_clk_levels(smu, SMU_FCLK, 1 << fclk_mask);
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_PEAK:
		smu->gfx_actual_hard_min_freq = VANGOGH_UMD_PSTATE_PEAK_GFXCLK;
		smu->gfx_actual_soft_max_freq = VANGOGH_UMD_PSTATE_PEAK_GFXCLK;

		ret = vangogh_set_peak_clock_by_device(smu);
		if (ret)
			return ret;
		break;
	case AMD_DPM_FORCED_LEVEL_MANUAL:
	case AMD_DPM_FORCED_LEVEL_PROFILE_EXIT:
	default:
		return 0;
	}

	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetHardMinGfxClk,
					      smu->gfx_actual_hard_min_freq, NULL);
	if (ret)
		return ret;

	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMaxGfxClk,
					      smu->gfx_actual_soft_max_freq, NULL);
	if (ret)
		return ret;

	if (smu->adev->pm.fw_version >= 0x43f1b00) {
		for (i = 0; i < smu->cpu_core_num; i++) {
			ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMinCclk,
							      ((i << 20)
							       | smu->cpu_actual_soft_min_freq),
							      NULL);
			if (ret)
				return ret;

			ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMaxCclk,
							      ((i << 20)
							       | smu->cpu_actual_soft_max_freq),
							      NULL);
			if (ret)
				return ret;
		}
	}

	return ret;
}

static int vangogh_read_sensor(struct smu_context *smu,
				 enum amd_pp_sensors sensor,
				 void *data, uint32_t *size)
{
	int ret = 0;

	if (!data || !size)
		return -EINVAL;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		ret = vangogh_common_get_smu_metrics_data(smu,
						   METRICS_AVERAGE_GFXACTIVITY,
						   (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_POWER:
		ret = vangogh_common_get_smu_metrics_data(smu,
						   METRICS_AVERAGE_SOCKETPOWER,
						   (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_EDGE_TEMP:
		ret = vangogh_common_get_smu_metrics_data(smu,
						   METRICS_TEMPERATURE_EDGE,
						   (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_HOTSPOT_TEMP:
		ret = vangogh_common_get_smu_metrics_data(smu,
						   METRICS_TEMPERATURE_HOTSPOT,
						   (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GFX_MCLK:
		ret = vangogh_common_get_smu_metrics_data(smu,
						   METRICS_CURR_UCLK,
						   (uint32_t *)data);
		*(uint32_t *)data *= 100;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GFX_SCLK:
		ret = vangogh_common_get_smu_metrics_data(smu,
						   METRICS_CURR_GFXCLK,
						   (uint32_t *)data);
		*(uint32_t *)data *= 100;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_VDDGFX:
		ret = vangogh_common_get_smu_metrics_data(smu,
						   METRICS_VOLTAGE_VDDGFX,
						   (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_VDDNB:
		ret = vangogh_common_get_smu_metrics_data(smu,
						   METRICS_VOLTAGE_VDDSOC,
						   (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_CPU_CLK:
		ret = vangogh_common_get_smu_metrics_data(smu,
						   METRICS_AVERAGE_CPUCLK,
						   (uint32_t *)data);
		*size = smu->cpu_core_num * sizeof(uint16_t);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static int vangogh_get_apu_thermal_limit(struct smu_context *smu, uint32_t *limit)
{
	return smu_cmn_send_smc_msg_with_param(smu,
					      SMU_MSG_GetThermalLimit,
					      0, limit);
}

static int vangogh_set_apu_thermal_limit(struct smu_context *smu, uint32_t limit)
{
	return smu_cmn_send_smc_msg_with_param(smu,
					      SMU_MSG_SetReducedThermalLimit,
					      limit, NULL);
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

static ssize_t vangogh_get_legacy_gpu_metrics_v2_3(struct smu_context *smu,
				      void **table)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct gpu_metrics_v2_3 *gpu_metrics =
		(struct gpu_metrics_v2_3 *)smu_table->gpu_metrics_table;
	SmuMetrics_legacy_t metrics;
	int ret = 0;

	ret = smu_cmn_get_metrics_table(smu, &metrics, true);
	if (ret)
		return ret;

	smu_cmn_init_soft_gpu_metrics(gpu_metrics, 2, 3);

	gpu_metrics->temperature_gfx = metrics.GfxTemperature;
	gpu_metrics->temperature_soc = metrics.SocTemperature;
	memcpy(&gpu_metrics->temperature_core[0],
		&metrics.CoreTemperature[0],
		sizeof(uint16_t) * 4);
	gpu_metrics->temperature_l3[0] = metrics.L3Temperature[0];

	gpu_metrics->average_gfx_activity = metrics.GfxActivity;
	gpu_metrics->average_mm_activity = metrics.UvdActivity;

	gpu_metrics->average_socket_power = metrics.CurrentSocketPower;
	gpu_metrics->average_cpu_power = metrics.Power[0];
	gpu_metrics->average_soc_power = metrics.Power[1];
	gpu_metrics->average_gfx_power = metrics.Power[2];
	memcpy(&gpu_metrics->average_core_power[0],
		&metrics.CorePower[0],
		sizeof(uint16_t) * 4);

	gpu_metrics->average_gfxclk_frequency = metrics.GfxclkFrequency;
	gpu_metrics->average_socclk_frequency = metrics.SocclkFrequency;
	gpu_metrics->average_uclk_frequency = metrics.MemclkFrequency;
	gpu_metrics->average_fclk_frequency = metrics.MemclkFrequency;
	gpu_metrics->average_vclk_frequency = metrics.VclkFrequency;
	gpu_metrics->average_dclk_frequency = metrics.DclkFrequency;

	memcpy(&gpu_metrics->current_coreclk[0],
		&metrics.CoreFrequency[0],
		sizeof(uint16_t) * 4);
	gpu_metrics->current_l3clk[0] = metrics.L3Frequency[0];

	gpu_metrics->throttle_status = metrics.ThrottlerStatus;
	gpu_metrics->indep_throttle_status =
			smu_cmn_get_indep_throttler_status(metrics.ThrottlerStatus,
							   vangogh_throttler_map);

	gpu_metrics->system_clock_counter = ktime_get_boottime_ns();

	*table = (void *)gpu_metrics;

	return sizeof(struct gpu_metrics_v2_3);
}

static ssize_t vangogh_get_legacy_gpu_metrics(struct smu_context *smu,
				      void **table)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct gpu_metrics_v2_2 *gpu_metrics =
		(struct gpu_metrics_v2_2 *)smu_table->gpu_metrics_table;
	SmuMetrics_legacy_t metrics;
	int ret = 0;

	ret = smu_cmn_get_metrics_table(smu, &metrics, true);
	if (ret)
		return ret;

	smu_cmn_init_soft_gpu_metrics(gpu_metrics, 2, 2);

	gpu_metrics->temperature_gfx = metrics.GfxTemperature;
	gpu_metrics->temperature_soc = metrics.SocTemperature;
	memcpy(&gpu_metrics->temperature_core[0],
		&metrics.CoreTemperature[0],
		sizeof(uint16_t) * 4);
	gpu_metrics->temperature_l3[0] = metrics.L3Temperature[0];

	gpu_metrics->average_gfx_activity = metrics.GfxActivity;
	gpu_metrics->average_mm_activity = metrics.UvdActivity;

	gpu_metrics->average_socket_power = metrics.CurrentSocketPower;
	gpu_metrics->average_cpu_power = metrics.Power[0];
	gpu_metrics->average_soc_power = metrics.Power[1];
	gpu_metrics->average_gfx_power = metrics.Power[2];
	memcpy(&gpu_metrics->average_core_power[0],
		&metrics.CorePower[0],
		sizeof(uint16_t) * 4);

	gpu_metrics->average_gfxclk_frequency = metrics.GfxclkFrequency;
	gpu_metrics->average_socclk_frequency = metrics.SocclkFrequency;
	gpu_metrics->average_uclk_frequency = metrics.MemclkFrequency;
	gpu_metrics->average_fclk_frequency = metrics.MemclkFrequency;
	gpu_metrics->average_vclk_frequency = metrics.VclkFrequency;
	gpu_metrics->average_dclk_frequency = metrics.DclkFrequency;

	memcpy(&gpu_metrics->current_coreclk[0],
		&metrics.CoreFrequency[0],
		sizeof(uint16_t) * 4);
	gpu_metrics->current_l3clk[0] = metrics.L3Frequency[0];

	gpu_metrics->throttle_status = metrics.ThrottlerStatus;
	gpu_metrics->indep_throttle_status =
			smu_cmn_get_indep_throttler_status(metrics.ThrottlerStatus,
							   vangogh_throttler_map);

	gpu_metrics->system_clock_counter = ktime_get_boottime_ns();

	*table = (void *)gpu_metrics;

	return sizeof(struct gpu_metrics_v2_2);
}

static ssize_t vangogh_get_gpu_metrics_v2_3(struct smu_context *smu,
				      void **table)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct gpu_metrics_v2_3 *gpu_metrics =
		(struct gpu_metrics_v2_3 *)smu_table->gpu_metrics_table;
	SmuMetrics_t metrics;
	int ret = 0;

	ret = smu_cmn_get_metrics_table(smu, &metrics, true);
	if (ret)
		return ret;

	smu_cmn_init_soft_gpu_metrics(gpu_metrics, 2, 3);

	gpu_metrics->temperature_gfx = metrics.Current.GfxTemperature;
	gpu_metrics->temperature_soc = metrics.Current.SocTemperature;
	memcpy(&gpu_metrics->temperature_core[0],
		&metrics.Current.CoreTemperature[0],
		sizeof(uint16_t) * 4);
	gpu_metrics->temperature_l3[0] = metrics.Current.L3Temperature[0];

	gpu_metrics->average_temperature_gfx = metrics.Average.GfxTemperature;
	gpu_metrics->average_temperature_soc = metrics.Average.SocTemperature;
	memcpy(&gpu_metrics->average_temperature_core[0],
		&metrics.Average.CoreTemperature[0],
		sizeof(uint16_t) * 4);
	gpu_metrics->average_temperature_l3[0] = metrics.Average.L3Temperature[0];

	gpu_metrics->average_gfx_activity = metrics.Current.GfxActivity;
	gpu_metrics->average_mm_activity = metrics.Current.UvdActivity;

	gpu_metrics->average_socket_power = metrics.Current.CurrentSocketPower;
	gpu_metrics->average_cpu_power = metrics.Current.Power[0];
	gpu_metrics->average_soc_power = metrics.Current.Power[1];
	gpu_metrics->average_gfx_power = metrics.Current.Power[2];
	memcpy(&gpu_metrics->average_core_power[0],
		&metrics.Average.CorePower[0],
		sizeof(uint16_t) * 4);

	gpu_metrics->average_gfxclk_frequency = metrics.Average.GfxclkFrequency;
	gpu_metrics->average_socclk_frequency = metrics.Average.SocclkFrequency;
	gpu_metrics->average_uclk_frequency = metrics.Average.MemclkFrequency;
	gpu_metrics->average_fclk_frequency = metrics.Average.MemclkFrequency;
	gpu_metrics->average_vclk_frequency = metrics.Average.VclkFrequency;
	gpu_metrics->average_dclk_frequency = metrics.Average.DclkFrequency;

	gpu_metrics->current_gfxclk = metrics.Current.GfxclkFrequency;
	gpu_metrics->current_socclk = metrics.Current.SocclkFrequency;
	gpu_metrics->current_uclk = metrics.Current.MemclkFrequency;
	gpu_metrics->current_fclk = metrics.Current.MemclkFrequency;
	gpu_metrics->current_vclk = metrics.Current.VclkFrequency;
	gpu_metrics->current_dclk = metrics.Current.DclkFrequency;

	memcpy(&gpu_metrics->current_coreclk[0],
		&metrics.Current.CoreFrequency[0],
		sizeof(uint16_t) * 4);
	gpu_metrics->current_l3clk[0] = metrics.Current.L3Frequency[0];

	gpu_metrics->throttle_status = metrics.Current.ThrottlerStatus;
	gpu_metrics->indep_throttle_status =
			smu_cmn_get_indep_throttler_status(metrics.Current.ThrottlerStatus,
							   vangogh_throttler_map);

	gpu_metrics->system_clock_counter = ktime_get_boottime_ns();

	*table = (void *)gpu_metrics;

	return sizeof(struct gpu_metrics_v2_3);
}

static ssize_t vangogh_get_gpu_metrics(struct smu_context *smu,
				      void **table)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct gpu_metrics_v2_2 *gpu_metrics =
		(struct gpu_metrics_v2_2 *)smu_table->gpu_metrics_table;
	SmuMetrics_t metrics;
	int ret = 0;

	ret = smu_cmn_get_metrics_table(smu, &metrics, true);
	if (ret)
		return ret;

	smu_cmn_init_soft_gpu_metrics(gpu_metrics, 2, 2);

	gpu_metrics->temperature_gfx = metrics.Current.GfxTemperature;
	gpu_metrics->temperature_soc = metrics.Current.SocTemperature;
	memcpy(&gpu_metrics->temperature_core[0],
		&metrics.Current.CoreTemperature[0],
		sizeof(uint16_t) * 4);
	gpu_metrics->temperature_l3[0] = metrics.Current.L3Temperature[0];

	gpu_metrics->average_gfx_activity = metrics.Current.GfxActivity;
	gpu_metrics->average_mm_activity = metrics.Current.UvdActivity;

	gpu_metrics->average_socket_power = metrics.Current.CurrentSocketPower;
	gpu_metrics->average_cpu_power = metrics.Current.Power[0];
	gpu_metrics->average_soc_power = metrics.Current.Power[1];
	gpu_metrics->average_gfx_power = metrics.Current.Power[2];
	memcpy(&gpu_metrics->average_core_power[0],
		&metrics.Average.CorePower[0],
		sizeof(uint16_t) * 4);

	gpu_metrics->average_gfxclk_frequency = metrics.Average.GfxclkFrequency;
	gpu_metrics->average_socclk_frequency = metrics.Average.SocclkFrequency;
	gpu_metrics->average_uclk_frequency = metrics.Average.MemclkFrequency;
	gpu_metrics->average_fclk_frequency = metrics.Average.MemclkFrequency;
	gpu_metrics->average_vclk_frequency = metrics.Average.VclkFrequency;
	gpu_metrics->average_dclk_frequency = metrics.Average.DclkFrequency;

	gpu_metrics->current_gfxclk = metrics.Current.GfxclkFrequency;
	gpu_metrics->current_socclk = metrics.Current.SocclkFrequency;
	gpu_metrics->current_uclk = metrics.Current.MemclkFrequency;
	gpu_metrics->current_fclk = metrics.Current.MemclkFrequency;
	gpu_metrics->current_vclk = metrics.Current.VclkFrequency;
	gpu_metrics->current_dclk = metrics.Current.DclkFrequency;

	memcpy(&gpu_metrics->current_coreclk[0],
		&metrics.Current.CoreFrequency[0],
		sizeof(uint16_t) * 4);
	gpu_metrics->current_l3clk[0] = metrics.Current.L3Frequency[0];

	gpu_metrics->throttle_status = metrics.Current.ThrottlerStatus;
	gpu_metrics->indep_throttle_status =
			smu_cmn_get_indep_throttler_status(metrics.Current.ThrottlerStatus,
							   vangogh_throttler_map);

	gpu_metrics->system_clock_counter = ktime_get_boottime_ns();

	*table = (void *)gpu_metrics;

	return sizeof(struct gpu_metrics_v2_2);
}

static ssize_t vangogh_common_get_gpu_metrics(struct smu_context *smu,
				      void **table)
{
	uint32_t if_version;
	uint32_t smu_version;
	int ret = 0;

	ret = smu_cmn_get_smc_version(smu, &if_version, &smu_version);
	if (ret) {
		return ret;
	}

	if (smu_version >= 0x043F3E00) {
		if (if_version < 0x3)
			ret = vangogh_get_legacy_gpu_metrics_v2_3(smu, table);
		else
			ret = vangogh_get_gpu_metrics_v2_3(smu, table);
	} else {
		if (if_version < 0x3)
			ret = vangogh_get_legacy_gpu_metrics(smu, table);
		else
			ret = vangogh_get_gpu_metrics(smu, table);
	}

	return ret;
}

static int vangogh_od_edit_dpm_table(struct smu_context *smu, enum PP_OD_DPM_TABLE_COMMAND type,
					long input[], uint32_t size)
{
	int ret = 0;
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);

	if (!(smu_dpm_ctx->dpm_level == AMD_DPM_FORCED_LEVEL_MANUAL)) {
		dev_warn(smu->adev->dev,
			"pp_od_clk_voltage is not accessible if power_dpm_force_performance_level is not in manual mode!\n");
		return -EINVAL;
	}

	switch (type) {
	case PP_OD_EDIT_CCLK_VDDC_TABLE:
		if (size != 3) {
			dev_err(smu->adev->dev, "Input parameter number not correct (should be 4 for processor)\n");
			return -EINVAL;
		}
		if (input[0] >= smu->cpu_core_num) {
			dev_err(smu->adev->dev, "core index is overflow, should be less than %d\n",
				smu->cpu_core_num);
		}
		smu->cpu_core_id_select = input[0];
		if (input[1] == 0) {
			if (input[2] < smu->cpu_default_soft_min_freq) {
				dev_warn(smu->adev->dev, "Fine grain setting minimum cclk (%ld) MHz is less than the minimum allowed (%d) MHz\n",
					input[2], smu->cpu_default_soft_min_freq);
				return -EINVAL;
			}
			smu->cpu_actual_soft_min_freq = input[2];
		} else if (input[1] == 1) {
			if (input[2] > smu->cpu_default_soft_max_freq) {
				dev_warn(smu->adev->dev, "Fine grain setting maximum cclk (%ld) MHz is greater than the maximum allowed (%d) MHz\n",
					input[2], smu->cpu_default_soft_max_freq);
				return -EINVAL;
			}
			smu->cpu_actual_soft_max_freq = input[2];
		} else {
			return -EINVAL;
		}
		break;
	case PP_OD_EDIT_SCLK_VDDC_TABLE:
		if (size != 2) {
			dev_err(smu->adev->dev, "Input parameter number not correct\n");
			return -EINVAL;
		}

		if (input[0] == 0) {
			if (input[1] < smu->gfx_default_hard_min_freq) {
				dev_warn(smu->adev->dev,
					"Fine grain setting minimum sclk (%ld) MHz is less than the minimum allowed (%d) MHz\n",
					input[1], smu->gfx_default_hard_min_freq);
				return -EINVAL;
			}
			smu->gfx_actual_hard_min_freq = input[1];
		} else if (input[0] == 1) {
			if (input[1] > smu->gfx_default_soft_max_freq) {
				dev_warn(smu->adev->dev,
					"Fine grain setting maximum sclk (%ld) MHz is greater than the maximum allowed (%d) MHz\n",
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
			smu->cpu_actual_soft_min_freq = smu->cpu_default_soft_min_freq;
			smu->cpu_actual_soft_max_freq = smu->cpu_default_soft_max_freq;
		}
		break;
	case PP_OD_COMMIT_DPM_TABLE:
		if (size != 0) {
			dev_err(smu->adev->dev, "Input parameter number not correct\n");
			return -EINVAL;
		} else {
			if (smu->gfx_actual_hard_min_freq > smu->gfx_actual_soft_max_freq) {
				dev_err(smu->adev->dev,
					"The setting minimum sclk (%d) MHz is greater than the setting maximum sclk (%d) MHz\n",
					smu->gfx_actual_hard_min_freq,
					smu->gfx_actual_soft_max_freq);
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

			if (smu->adev->pm.fw_version < 0x43f1b00) {
				dev_warn(smu->adev->dev, "CPUSoftMax/CPUSoftMin are not supported, please update SBIOS!\n");
				break;
			}

			ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMinCclk,
							      ((smu->cpu_core_id_select << 20)
							       | smu->cpu_actual_soft_min_freq),
							      NULL);
			if (ret) {
				dev_err(smu->adev->dev, "Set hard min cclk failed!");
				return ret;
			}

			ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMaxCclk,
							      ((smu->cpu_core_id_select << 20)
							       | smu->cpu_actual_soft_max_freq),
							      NULL);
			if (ret) {
				dev_err(smu->adev->dev, "Set soft max cclk failed!");
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

	smu->cpu_default_soft_min_freq = 1400;
	smu->cpu_default_soft_max_freq = 3500;
	smu->cpu_actual_soft_min_freq = 0;
	smu->cpu_actual_soft_max_freq = 0;

	return 0;
}

static int vangogh_get_dpm_clock_table(struct smu_context *smu, struct dpm_clocks *clock_table)
{
	DpmClocks_t *table = smu->smu_table.clocks_table;
	int i;

	if (!clock_table || !table)
		return -EINVAL;

	for (i = 0; i < NUM_SOCCLK_DPM_LEVELS; i++) {
		clock_table->SocClocks[i].Freq = table->SocClocks[i];
		clock_table->SocClocks[i].Vol = table->SocVoltage[i];
	}

	for (i = 0; i < NUM_FCLK_DPM_LEVELS; i++) {
		clock_table->FClocks[i].Freq = table->DfPstateTable[i].fclk;
		clock_table->FClocks[i].Vol = table->DfPstateTable[i].voltage;
	}

	for (i = 0; i < NUM_FCLK_DPM_LEVELS; i++) {
		clock_table->MemClocks[i].Freq = table->DfPstateTable[i].memclk;
		clock_table->MemClocks[i].Vol = table->DfPstateTable[i].voltage;
	}

	return 0;
}


static int vangogh_system_features_control(struct smu_context *smu, bool en)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;

	if (adev->pm.fw_version >= 0x43f1700 && !en)
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_RlcPowerNotify,
						      RLC_STATUS_OFF, NULL);

	return ret;
}

static int vangogh_post_smu_init(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t tmp;
	int ret = 0;
	uint8_t aon_bits = 0;
	/* Two CUs in one WGP */
	uint32_t req_active_wgps = adev->gfx.cu_info.number/2;
	uint32_t total_cu = adev->gfx.config.max_cu_per_sh *
		adev->gfx.config.max_sh_per_se * adev->gfx.config.max_shader_engines;

	/* allow message will be sent after enable message on Vangogh*/
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_GFXCLK_BIT) &&
			(adev->pg_flags & AMD_PG_SUPPORT_GFX_PG)) {
		ret = smu_cmn_send_smc_msg(smu, SMU_MSG_EnableGfxOff, NULL);
		if (ret) {
			dev_err(adev->dev, "Failed to Enable GfxOff!\n");
			return ret;
		}
	} else {
		adev->pm.pp_feature &= ~PP_GFXOFF_MASK;
		dev_info(adev->dev, "If GFX DPM or power gate disabled, disable GFXOFF\n");
	}

	/* if all CUs are active, no need to power off any WGPs */
	if (total_cu == adev->gfx.cu_info.number)
		return 0;

	/*
	 * Calculate the total bits number of always on WGPs for all SA/SEs in
	 * RLC_PG_ALWAYS_ON_WGP_MASK.
	 */
	tmp = RREG32_KIQ(SOC15_REG_OFFSET(GC, 0, mmRLC_PG_ALWAYS_ON_WGP_MASK));
	tmp &= RLC_PG_ALWAYS_ON_WGP_MASK__AON_WGP_MASK_MASK;

	aon_bits = hweight32(tmp) * adev->gfx.config.max_sh_per_se * adev->gfx.config.max_shader_engines;

	/* Do not request any WGPs less than set in the AON_WGP_MASK */
	if (aon_bits > req_active_wgps) {
		dev_info(adev->dev, "Number of always on WGPs greater than active WGPs: WGP power save not requested.\n");
		return 0;
	} else {
		return smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_RequestActiveWgp, req_active_wgps, NULL);
	}
}

static int vangogh_mode_reset(struct smu_context *smu, int type)
{
	int ret = 0, index = 0;

	index = smu_cmn_to_asic_specific_index(smu, CMN2ASIC_MAPPING_MSG,
					       SMU_MSG_GfxDeviceDriverReset);
	if (index < 0)
		return index == -EACCES ? 0 : index;

	mutex_lock(&smu->message_lock);

	ret = smu_cmn_send_msg_without_waiting(smu, (uint16_t)index, type);

	mutex_unlock(&smu->message_lock);

	mdelay(10);

	return ret;
}

static int vangogh_mode2_reset(struct smu_context *smu)
{
	return vangogh_mode_reset(smu, SMU_RESET_MODE_2);
}

/**
 * vangogh_get_gfxoff_status - Get gfxoff status
 *
 * @smu: amdgpu_device pointer
 *
 * Get current gfxoff status
 *
 * Return:
 * * 0	- GFXOFF (default if enabled).
 * * 1	- Transition out of GFX State.
 * * 2	- Not in GFXOFF.
 * * 3	- Transition into GFXOFF.
 */
static u32 vangogh_get_gfxoff_status(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	u32 reg, gfxoff_status;

	reg = RREG32_SOC15(SMUIO, 0, mmSMUIO_GFX_MISC_CNTL);
	gfxoff_status = (reg & SMUIO_GFX_MISC_CNTL__PWR_GFXOFF_STATUS_MASK)
		>> SMUIO_GFX_MISC_CNTL__PWR_GFXOFF_STATUS__SHIFT;

	return gfxoff_status;
}

static int vangogh_get_power_limit(struct smu_context *smu,
				   uint32_t *current_power_limit,
				   uint32_t *default_power_limit,
				   uint32_t *max_power_limit)
{
	struct smu_11_5_power_context *power_context =
								smu->smu_power.power_context;
	uint32_t ppt_limit;
	int ret = 0;

	if (smu->adev->pm.fw_version < 0x43f1e00)
		return ret;

	ret = smu_cmn_send_smc_msg(smu, SMU_MSG_GetSlowPPTLimit, &ppt_limit);
	if (ret) {
		dev_err(smu->adev->dev, "Get slow PPT limit failed!\n");
		return ret;
	}
	/* convert from milliwatt to watt */
	if (current_power_limit)
		*current_power_limit = ppt_limit / 1000;
	if (default_power_limit)
		*default_power_limit = ppt_limit / 1000;
	if (max_power_limit)
		*max_power_limit = 29;

	ret = smu_cmn_send_smc_msg(smu, SMU_MSG_GetFastPPTLimit, &ppt_limit);
	if (ret) {
		dev_err(smu->adev->dev, "Get fast PPT limit failed!\n");
		return ret;
	}
	/* convert from milliwatt to watt */
	power_context->current_fast_ppt_limit =
			power_context->default_fast_ppt_limit = ppt_limit / 1000;
	power_context->max_fast_ppt_limit = 30;

	return ret;
}

static int vangogh_get_ppt_limit(struct smu_context *smu,
								uint32_t *ppt_limit,
								enum smu_ppt_limit_type type,
								enum smu_ppt_limit_level level)
{
	struct smu_11_5_power_context *power_context =
							smu->smu_power.power_context;

	if (!power_context)
		return -EOPNOTSUPP;

	if (type == SMU_FAST_PPT_LIMIT) {
		switch (level) {
		case SMU_PPT_LIMIT_MAX:
			*ppt_limit = power_context->max_fast_ppt_limit;
			break;
		case SMU_PPT_LIMIT_CURRENT:
			*ppt_limit = power_context->current_fast_ppt_limit;
			break;
		case SMU_PPT_LIMIT_DEFAULT:
			*ppt_limit = power_context->default_fast_ppt_limit;
			break;
		default:
			break;
		}
	}

	return 0;
}

static int vangogh_set_power_limit(struct smu_context *smu,
				   enum smu_ppt_limit_type limit_type,
				   uint32_t ppt_limit)
{
	struct smu_11_5_power_context *power_context =
			smu->smu_power.power_context;
	int ret = 0;

	if (!smu_cmn_feature_is_enabled(smu, SMU_FEATURE_PPT_BIT)) {
		dev_err(smu->adev->dev, "Setting new power limit is not supported!\n");
		return -EOPNOTSUPP;
	}

	switch (limit_type) {
	case SMU_DEFAULT_PPT_LIMIT:
		ret = smu_cmn_send_smc_msg_with_param(smu,
				SMU_MSG_SetSlowPPTLimit,
				ppt_limit * 1000, /* convert from watt to milliwatt */
				NULL);
		if (ret)
			return ret;

		smu->current_power_limit = ppt_limit;
		break;
	case SMU_FAST_PPT_LIMIT:
		ppt_limit &= ~(SMU_FAST_PPT_LIMIT << 24);
		if (ppt_limit > power_context->max_fast_ppt_limit) {
			dev_err(smu->adev->dev,
				"New power limit (%d) is over the max allowed %d\n",
				ppt_limit, power_context->max_fast_ppt_limit);
			return ret;
		}

		ret = smu_cmn_send_smc_msg_with_param(smu,
				SMU_MSG_SetFastPPTLimit,
				ppt_limit * 1000, /* convert from watt to milliwatt */
				NULL);
		if (ret)
			return ret;

		power_context->current_fast_ppt_limit = ppt_limit;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

/**
 * vangogh_set_gfxoff_residency
 *
 * @smu: amdgpu_device pointer
 * @start: start/stop residency log
 *
 * This function will be used to log gfxoff residency
 *
 *
 * Returns standard response codes.
 */
static u32 vangogh_set_gfxoff_residency(struct smu_context *smu, bool start)
{
	int ret = 0;
	u32 residency;
	struct amdgpu_device *adev = smu->adev;

	if (!(adev->pm.pp_feature & PP_GFXOFF_MASK))
		return 0;

	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_LogGfxOffResidency,
					      start, &residency);

	if (!start)
		adev->gfx.gfx_off_residency = residency;

	return ret;
}

/**
 * vangogh_get_gfxoff_residency
 *
 * @smu: amdgpu_device pointer
 * @residency: placeholder for return value
 *
 * This function will be used to get gfxoff residency.
 *
 * Returns standard response codes.
 */
static u32 vangogh_get_gfxoff_residency(struct smu_context *smu, uint32_t *residency)
{
	struct amdgpu_device *adev = smu->adev;

	*residency = adev->gfx.gfx_off_residency;

	return 0;
}

/**
 * vangogh_get_gfxoff_entrycount - get gfxoff entry count
 *
 * @smu: amdgpu_device pointer
 * @entrycount: placeholder for return value
 *
 * This function will be used to get gfxoff entry count
 *
 * Returns standard response codes.
 */
static u32 vangogh_get_gfxoff_entrycount(struct smu_context *smu, uint64_t *entrycount)
{
	int ret = 0, value = 0;
	struct amdgpu_device *adev = smu->adev;

	if (!(adev->pm.pp_feature & PP_GFXOFF_MASK))
		return 0;

	ret = smu_cmn_send_smc_msg(smu, SMU_MSG_GetGfxOffEntryCount, &value);
	*entrycount = value + adev->gfx.gfx_off_entrycount;

	return ret;
}

static const struct pptable_funcs vangogh_ppt_funcs = {

	.check_fw_status = smu_v11_0_check_fw_status,
	.check_fw_version = smu_v11_0_check_fw_version,
	.init_smc_tables = vangogh_init_smc_tables,
	.fini_smc_tables = smu_v11_0_fini_smc_tables,
	.init_power = smu_v11_0_init_power,
	.fini_power = smu_v11_0_fini_power,
	.register_irq_handler = smu_v11_0_register_irq_handler,
	.notify_memory_pool_location = smu_v11_0_notify_memory_pool_location,
	.send_smc_msg_with_param = smu_cmn_send_smc_msg_with_param,
	.send_smc_msg = smu_cmn_send_smc_msg,
	.dpm_set_vcn_enable = vangogh_dpm_set_vcn_enable,
	.dpm_set_jpeg_enable = vangogh_dpm_set_jpeg_enable,
	.is_dpm_running = vangogh_is_dpm_running,
	.read_sensor = vangogh_read_sensor,
	.get_apu_thermal_limit = vangogh_get_apu_thermal_limit,
	.set_apu_thermal_limit = vangogh_set_apu_thermal_limit,
	.get_enabled_mask = smu_cmn_get_enabled_mask,
	.get_pp_feature_mask = smu_cmn_get_pp_feature_mask,
	.set_watermarks_table = vangogh_set_watermarks_table,
	.set_driver_table_location = smu_v11_0_set_driver_table_location,
	.interrupt_work = smu_v11_0_interrupt_work,
	.get_gpu_metrics = vangogh_common_get_gpu_metrics,
	.od_edit_dpm_table = vangogh_od_edit_dpm_table,
	.print_clk_levels = vangogh_common_print_clk_levels,
	.set_default_dpm_table = vangogh_set_default_dpm_tables,
	.set_fine_grain_gfx_freq_parameters = vangogh_set_fine_grain_gfx_freq_parameters,
	.system_features_control = vangogh_system_features_control,
	.feature_is_enabled = smu_cmn_feature_is_enabled,
	.set_power_profile_mode = vangogh_set_power_profile_mode,
	.get_power_profile_mode = vangogh_get_power_profile_mode,
	.get_dpm_clock_table = vangogh_get_dpm_clock_table,
	.force_clk_levels = vangogh_force_clk_levels,
	.set_performance_level = vangogh_set_performance_level,
	.post_init = vangogh_post_smu_init,
	.mode2_reset = vangogh_mode2_reset,
	.gfx_off_control = smu_v11_0_gfx_off_control,
	.get_gfx_off_status = vangogh_get_gfxoff_status,
	.get_gfx_off_entrycount = vangogh_get_gfxoff_entrycount,
	.get_gfx_off_residency = vangogh_get_gfxoff_residency,
	.set_gfx_off_residency = vangogh_set_gfxoff_residency,
	.get_ppt_limit = vangogh_get_ppt_limit,
	.get_power_limit = vangogh_get_power_limit,
	.set_power_limit = vangogh_set_power_limit,
	.get_vbios_bootup_values = smu_v11_0_get_vbios_bootup_values,
};

void vangogh_set_ppt_funcs(struct smu_context *smu)
{
	smu->ppt_funcs = &vangogh_ppt_funcs;
	smu->message_map = vangogh_message_map;
	smu->feature_map = vangogh_feature_mask_map;
	smu->table_map = vangogh_table_map;
	smu->workload_map = vangogh_workload_map;
	smu->is_apu = true;
	smu_v11_0_set_smu_mailbox_registers(smu);
}
