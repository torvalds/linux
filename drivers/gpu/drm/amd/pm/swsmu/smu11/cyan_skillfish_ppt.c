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

#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "smu_v11_0.h"
#include "smu11_driver_if_cyan_skillfish.h"
#include "cyan_skillfish_ppt.h"
#include "smu_v11_8_ppsmc.h"
#include "smu_v11_8_pmfw.h"
#include "smu_cmn.h"
#include "soc15_common.h"

/*
 * DO NOT use these for err/warn/info/debug messages.
 * Use dev_err, dev_warn, dev_info and dev_dbg instead.
 * They are more MGPU friendly.
 */

#undef pr_err
#undef pr_warn
#undef pr_info
#undef pr_debug

/* unit: MHz */
#define CYAN_SKILLFISH_SCLK_MIN			1000
#define CYAN_SKILLFISH_SCLK_MAX			2000

/* unit: mV */
#define CYAN_SKILLFISH_VDDC_MIN			700
#define CYAN_SKILLFISH_VDDC_MAX			1129
#define CYAN_SKILLFISH_VDDC_MAGIC			5118 // 0x13fe

static struct gfx_user_settings {
	uint32_t sclk;
	uint32_t vddc;
} cyan_skillfish_user_settings;

static uint32_t cyan_skillfish_sclk_default;

#define FEATURE_MASK(feature) (1ULL << feature)
#define SMC_DPM_FEATURE ( \
	FEATURE_MASK(FEATURE_FCLK_DPM_BIT)	|	\
	FEATURE_MASK(FEATURE_SOC_DPM_BIT)	|	\
	FEATURE_MASK(FEATURE_GFX_DPM_BIT))

static struct cmn2asic_msg_mapping cyan_skillfish_message_map[SMU_MSG_MAX_COUNT] = {
	MSG_MAP(TestMessage,                    PPSMC_MSG_TestMessage,			0),
	MSG_MAP(GetSmuVersion,                  PPSMC_MSG_GetSmuVersion,		0),
	MSG_MAP(GetDriverIfVersion,             PPSMC_MSG_GetDriverIfVersion,		0),
	MSG_MAP(SetDriverDramAddrHigh,          PPSMC_MSG_SetDriverTableDramAddrHigh,	0),
	MSG_MAP(SetDriverDramAddrLow,           PPSMC_MSG_SetDriverTableDramAddrLow,	0),
	MSG_MAP(TransferTableSmu2Dram,          PPSMC_MSG_TransferTableSmu2Dram,	0),
	MSG_MAP(TransferTableDram2Smu,          PPSMC_MSG_TransferTableDram2Smu,	0),
	MSG_MAP(GetEnabledSmuFeatures,          PPSMC_MSG_GetEnabledSmuFeatures,	0),
	MSG_MAP(RequestGfxclk,                  PPSMC_MSG_RequestGfxclk,		0),
	MSG_MAP(ForceGfxVid,                    PPSMC_MSG_ForceGfxVid,			0),
	MSG_MAP(UnforceGfxVid,                  PPSMC_MSG_UnforceGfxVid,		0),
};

static struct cmn2asic_mapping cyan_skillfish_table_map[SMU_TABLE_COUNT] = {
	TAB_MAP_VALID(SMU_METRICS),
};

static int cyan_skillfish_tables_init(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = smu_table->tables;

	SMU_TABLE_INIT(tables, SMU_TABLE_SMU_METRICS,
				sizeof(SmuMetrics_t),
				PAGE_SIZE,
				AMDGPU_GEM_DOMAIN_VRAM);

	smu_table->metrics_table = kzalloc(sizeof(SmuMetrics_t), GFP_KERNEL);
	if (!smu_table->metrics_table)
		goto err0_out;

	smu_table->gpu_metrics_table_size = sizeof(struct gpu_metrics_v2_2);
	smu_table->gpu_metrics_table = kzalloc(smu_table->gpu_metrics_table_size, GFP_KERNEL);
	if (!smu_table->gpu_metrics_table)
		goto err1_out;

	smu_table->metrics_time = 0;

	return 0;

err1_out:
	smu_table->gpu_metrics_table_size = 0;
	kfree(smu_table->metrics_table);
err0_out:
	return -ENOMEM;
}

static int cyan_skillfish_init_smc_tables(struct smu_context *smu)
{
	int ret = 0;

	ret = cyan_skillfish_tables_init(smu);
	if (ret)
		return ret;

	return smu_v11_0_init_smc_tables(smu);
}

static int
cyan_skillfish_get_smu_metrics_data(struct smu_context *smu,
					MetricsMember_t member,
					uint32_t *value)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	SmuMetrics_t *metrics = (SmuMetrics_t *)smu_table->metrics_table;
	int ret = 0;

	ret = smu_cmn_get_metrics_table(smu, NULL, false);
	if (ret)
		return ret;

	switch (member) {
	case METRICS_CURR_GFXCLK:
		*value = metrics->Current.GfxclkFrequency;
		break;
	case METRICS_CURR_SOCCLK:
		*value = metrics->Current.SocclkFrequency;
		break;
	case METRICS_CURR_VCLK:
		*value = metrics->Current.VclkFrequency;
		break;
	case METRICS_CURR_DCLK:
		*value = metrics->Current.DclkFrequency;
		break;
	case METRICS_CURR_UCLK:
		*value = metrics->Current.MemclkFrequency;
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
	case METRICS_VOLTAGE_VDDSOC:
		*value = metrics->Current.Voltage[0];
		break;
	case METRICS_VOLTAGE_VDDGFX:
		*value = metrics->Current.Voltage[1];
		break;
	case METRICS_THROTTLER_STATUS:
		*value = metrics->Current.ThrottlerStatus;
		break;
	default:
		*value = UINT_MAX;
		break;
	}

	return ret;
}

static int cyan_skillfish_read_sensor(struct smu_context *smu,
					enum amd_pp_sensors sensor,
					void *data,
					uint32_t *size)
{
	int ret = 0;

	if (!data || !size)
		return -EINVAL;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_GFX_SCLK:
		ret = cyan_skillfish_get_smu_metrics_data(smu,
						   METRICS_CURR_GFXCLK,
						   (uint32_t *)data);
		*(uint32_t *)data *= 100;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GFX_MCLK:
		ret = cyan_skillfish_get_smu_metrics_data(smu,
						   METRICS_CURR_UCLK,
						   (uint32_t *)data);
		*(uint32_t *)data *= 100;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_POWER:
		ret = cyan_skillfish_get_smu_metrics_data(smu,
						   METRICS_AVERAGE_SOCKETPOWER,
						   (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_HOTSPOT_TEMP:
		ret = cyan_skillfish_get_smu_metrics_data(smu,
						   METRICS_TEMPERATURE_HOTSPOT,
						   (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_EDGE_TEMP:
		ret = cyan_skillfish_get_smu_metrics_data(smu,
						   METRICS_TEMPERATURE_EDGE,
						   (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_VDDNB:
		ret = cyan_skillfish_get_smu_metrics_data(smu,
						   METRICS_VOLTAGE_VDDSOC,
						   (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_VDDGFX:
		ret = cyan_skillfish_get_smu_metrics_data(smu,
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

static int cyan_skillfish_get_current_clk_freq(struct smu_context *smu,
						enum smu_clk_type clk_type,
						uint32_t *value)
{
	MetricsMember_t member_type;

	switch (clk_type) {
	case SMU_GFXCLK:
	case SMU_SCLK:
		member_type = METRICS_CURR_GFXCLK;
		break;
	case SMU_FCLK:
	case SMU_MCLK:
		member_type = METRICS_CURR_UCLK;
		break;
	case SMU_SOCCLK:
		member_type = METRICS_CURR_SOCCLK;
		break;
	case SMU_VCLK:
		member_type = METRICS_CURR_VCLK;
		break;
	case SMU_DCLK:
		member_type = METRICS_CURR_DCLK;
		break;
	default:
		return -EINVAL;
	}

	return cyan_skillfish_get_smu_metrics_data(smu, member_type, value);
}

static int cyan_skillfish_print_clk_levels(struct smu_context *smu,
					enum smu_clk_type clk_type,
					char *buf)
{
	int ret = 0, size = 0;
	uint32_t cur_value = 0;
	int i;

	smu_cmn_get_sysfs_buf(&buf, &size);

	switch (clk_type) {
	case SMU_OD_SCLK:
		ret  = cyan_skillfish_get_smu_metrics_data(smu, METRICS_CURR_GFXCLK, &cur_value);
		if (ret)
			return ret;
		size += sysfs_emit_at(buf, size,"%s:\n", "OD_SCLK");
		size += sysfs_emit_at(buf, size, "0: %uMhz *\n", cur_value);
		break;
	case SMU_OD_VDDC_CURVE:
		ret  = cyan_skillfish_get_smu_metrics_data(smu, METRICS_VOLTAGE_VDDGFX, &cur_value);
		if (ret)
			return ret;
		size += sysfs_emit_at(buf, size,"%s:\n", "OD_VDDC");
		size += sysfs_emit_at(buf, size, "0: %umV *\n", cur_value);
		break;
	case SMU_OD_RANGE:
		size += sysfs_emit_at(buf, size, "%s:\n", "OD_RANGE");
		size += sysfs_emit_at(buf, size, "SCLK: %7uMhz %10uMhz\n",
						CYAN_SKILLFISH_SCLK_MIN, CYAN_SKILLFISH_SCLK_MAX);
		size += sysfs_emit_at(buf, size, "VDDC: %7umV  %10umV\n",
						CYAN_SKILLFISH_VDDC_MIN, CYAN_SKILLFISH_VDDC_MAX);
		break;
	case SMU_FCLK:
	case SMU_MCLK:
	case SMU_SOCCLK:
	case SMU_VCLK:
	case SMU_DCLK:
		ret = cyan_skillfish_get_current_clk_freq(smu, clk_type, &cur_value);
		if (ret)
			return ret;
		size += sysfs_emit_at(buf, size, "0: %uMhz *\n", cur_value);
		break;
	case SMU_SCLK:
	case SMU_GFXCLK:
		ret = cyan_skillfish_get_current_clk_freq(smu, clk_type, &cur_value);
		if (ret)
			return ret;
		if (cur_value  == CYAN_SKILLFISH_SCLK_MAX)
			i = 2;
		else if (cur_value == CYAN_SKILLFISH_SCLK_MIN)
			i = 0;
		else
			i = 1;
		size += sysfs_emit_at(buf, size, "0: %uMhz %s\n", CYAN_SKILLFISH_SCLK_MIN,
				i == 0 ? "*" : "");
		size += sysfs_emit_at(buf, size, "1: %uMhz %s\n",
				i == 1 ? cur_value : cyan_skillfish_sclk_default,
				i == 1 ? "*" : "");
		size += sysfs_emit_at(buf, size, "2: %uMhz %s\n", CYAN_SKILLFISH_SCLK_MAX,
				i == 2 ? "*" : "");
		break;
	default:
		dev_warn(smu->adev->dev, "Unsupported clock type\n");
		return ret;
	}

	return size;
}

static bool cyan_skillfish_is_dpm_running(struct smu_context *smu)
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

	/*
	 * cyan_skillfish specific, query default sclk inseted of hard code.
	 */
	if (!cyan_skillfish_sclk_default)
		cyan_skillfish_get_smu_metrics_data(smu, METRICS_CURR_GFXCLK,
			&cyan_skillfish_sclk_default);

	return !!(feature_enabled & SMC_DPM_FEATURE);
}

static ssize_t cyan_skillfish_get_gpu_metrics(struct smu_context *smu,
						void **table)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct gpu_metrics_v2_2 *gpu_metrics =
		(struct gpu_metrics_v2_2 *)smu_table->gpu_metrics_table;
	SmuMetrics_t metrics;
	int i, ret = 0;

	ret = smu_cmn_get_metrics_table(smu, &metrics, true);
	if (ret)
		return ret;

	smu_cmn_init_soft_gpu_metrics(gpu_metrics, 2, 2);

	gpu_metrics->temperature_gfx = metrics.Current.GfxTemperature;
	gpu_metrics->temperature_soc = metrics.Current.SocTemperature;

	gpu_metrics->average_socket_power = metrics.Current.CurrentSocketPower;
	gpu_metrics->average_soc_power = metrics.Current.Power[0];
	gpu_metrics->average_gfx_power = metrics.Current.Power[1];

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

	for (i = 0; i < 6; i++) {
		gpu_metrics->temperature_core[i] = metrics.Current.CoreTemperature[i];
		gpu_metrics->average_core_power[i] = metrics.Average.CorePower[i];
		gpu_metrics->current_coreclk[i] = metrics.Current.CoreFrequency[i];
	}

	for (i = 0; i < 2; i++) {
		gpu_metrics->temperature_l3[i] = metrics.Current.L3Temperature[i];
		gpu_metrics->current_l3clk[i] = metrics.Current.L3Frequency[i];
	}

	gpu_metrics->throttle_status = metrics.Current.ThrottlerStatus;
	gpu_metrics->system_clock_counter = ktime_get_boottime_ns();

	*table = (void *)gpu_metrics;

	return sizeof(struct gpu_metrics_v2_2);
}

static int cyan_skillfish_od_edit_dpm_table(struct smu_context *smu,
					enum PP_OD_DPM_TABLE_COMMAND type,
					long input[], uint32_t size)
{
	int ret = 0;
	uint32_t vid;

	switch (type) {
	case PP_OD_EDIT_VDDC_CURVE:
		if (size != 3 || input[0] != 0) {
			dev_err(smu->adev->dev, "Invalid parameter!\n");
			return -EINVAL;
		}

		if (input[1] < CYAN_SKILLFISH_SCLK_MIN ||
			input[1] > CYAN_SKILLFISH_SCLK_MAX) {
			dev_err(smu->adev->dev, "Invalid sclk! Valid sclk range: %uMHz - %uMhz\n",
					CYAN_SKILLFISH_SCLK_MIN, CYAN_SKILLFISH_SCLK_MAX);
			return -EINVAL;
		}

		if (input[2] < CYAN_SKILLFISH_VDDC_MIN ||
			input[2] > CYAN_SKILLFISH_VDDC_MAX) {
			dev_err(smu->adev->dev, "Invalid vddc! Valid vddc range: %umV - %umV\n",
					CYAN_SKILLFISH_VDDC_MIN, CYAN_SKILLFISH_VDDC_MAX);
			return -EINVAL;
		}

		cyan_skillfish_user_settings.sclk = input[1];
		cyan_skillfish_user_settings.vddc = input[2];

		break;
	case PP_OD_RESTORE_DEFAULT_TABLE:
		if (size != 0) {
			dev_err(smu->adev->dev, "Invalid parameter!\n");
			return -EINVAL;
		}

		cyan_skillfish_user_settings.sclk = cyan_skillfish_sclk_default;
		cyan_skillfish_user_settings.vddc = CYAN_SKILLFISH_VDDC_MAGIC;

		break;
	case PP_OD_COMMIT_DPM_TABLE:
		if (size != 0) {
			dev_err(smu->adev->dev, "Invalid parameter!\n");
			return -EINVAL;
		}

		if (cyan_skillfish_user_settings.sclk < CYAN_SKILLFISH_SCLK_MIN ||
		    cyan_skillfish_user_settings.sclk > CYAN_SKILLFISH_SCLK_MAX) {
			dev_err(smu->adev->dev, "Invalid sclk! Valid sclk range: %uMHz - %uMhz\n",
					CYAN_SKILLFISH_SCLK_MIN, CYAN_SKILLFISH_SCLK_MAX);
			return -EINVAL;
		}

		if ((cyan_skillfish_user_settings.vddc != CYAN_SKILLFISH_VDDC_MAGIC) &&
			(cyan_skillfish_user_settings.vddc < CYAN_SKILLFISH_VDDC_MIN ||
			cyan_skillfish_user_settings.vddc > CYAN_SKILLFISH_VDDC_MAX)) {
			dev_err(smu->adev->dev, "Invalid vddc! Valid vddc range: %umV - %umV\n",
					CYAN_SKILLFISH_VDDC_MIN, CYAN_SKILLFISH_VDDC_MAX);
			return -EINVAL;
		}

		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_RequestGfxclk,
					cyan_skillfish_user_settings.sclk, NULL);
		if (ret) {
			dev_err(smu->adev->dev, "Set sclk failed!\n");
			return ret;
		}

		if (cyan_skillfish_user_settings.vddc == CYAN_SKILLFISH_VDDC_MAGIC) {
			ret = smu_cmn_send_smc_msg(smu, SMU_MSG_UnforceGfxVid, NULL);
			if (ret) {
				dev_err(smu->adev->dev, "Unforce vddc failed!\n");
				return ret;
			}
		} else {
			/*
			 * PMFW accepts SVI2 VID code, convert voltage to VID:
			 * vid = (uint32_t)((1.55 - voltage) * 160.0 + 0.00001)
			 */
			vid = (1550 - cyan_skillfish_user_settings.vddc) * 160 / 1000;
			ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_ForceGfxVid, vid, NULL);
			if (ret) {
				dev_err(smu->adev->dev, "Force vddc failed!\n");
				return ret;
			}
		}

		break;
	default:
		return -EOPNOTSUPP;
	}

	return ret;
}

static int cyan_skillfish_get_dpm_ultimate_freq(struct smu_context *smu,
						enum smu_clk_type clk_type,
						uint32_t *min,
						uint32_t *max)
{
	int ret = 0;
	uint32_t low, high;

	switch (clk_type) {
	case SMU_GFXCLK:
	case SMU_SCLK:
		low = CYAN_SKILLFISH_SCLK_MIN;
		high = CYAN_SKILLFISH_SCLK_MAX;
		break;
	default:
		ret = cyan_skillfish_get_current_clk_freq(smu, clk_type, &low);
		if (ret)
			return ret;
		high = low;
		break;
	}

	if (min)
		*min = low;
	if (max)
		*max = high;

	return 0;
}

static int cyan_skillfish_get_enabled_mask(struct smu_context *smu,
					   uint64_t *feature_mask)
{
	if (!feature_mask)
		return -EINVAL;
	memset(feature_mask, 0xff, sizeof(*feature_mask));

	return 0;
}

static const struct pptable_funcs cyan_skillfish_ppt_funcs = {

	.check_fw_status = smu_v11_0_check_fw_status,
	.check_fw_version = smu_v11_0_check_fw_version,
	.init_power = smu_v11_0_init_power,
	.fini_power = smu_v11_0_fini_power,
	.init_smc_tables = cyan_skillfish_init_smc_tables,
	.fini_smc_tables = smu_v11_0_fini_smc_tables,
	.read_sensor = cyan_skillfish_read_sensor,
	.print_clk_levels = cyan_skillfish_print_clk_levels,
	.get_enabled_mask = cyan_skillfish_get_enabled_mask,
	.is_dpm_running = cyan_skillfish_is_dpm_running,
	.get_gpu_metrics = cyan_skillfish_get_gpu_metrics,
	.od_edit_dpm_table = cyan_skillfish_od_edit_dpm_table,
	.get_dpm_ultimate_freq = cyan_skillfish_get_dpm_ultimate_freq,
	.register_irq_handler = smu_v11_0_register_irq_handler,
	.notify_memory_pool_location = smu_v11_0_notify_memory_pool_location,
	.send_smc_msg_with_param = smu_cmn_send_smc_msg_with_param,
	.send_smc_msg = smu_cmn_send_smc_msg,
	.set_driver_table_location = smu_v11_0_set_driver_table_location,
	.interrupt_work = smu_v11_0_interrupt_work,
};

void cyan_skillfish_set_ppt_funcs(struct smu_context *smu)
{
	smu->ppt_funcs = &cyan_skillfish_ppt_funcs;
	smu->message_map = cyan_skillfish_message_map;
	smu->table_map = cyan_skillfish_table_map;
	smu->is_apu = true;
	smu_v11_0_set_smu_mailbox_registers(smu);
}
