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
 */

#include <linux/firmware.h>

#include "pp_debug.h"
#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "soc15_common.h"
#include "smu_v11_0.h"
#include "atom.h"
#include "amd_pcie.h"

int smu_get_smc_version(struct smu_context *smu, uint32_t *if_version, uint32_t *smu_version)
{
	int ret = 0;

	if (!if_version && !smu_version)
		return -EINVAL;

	if (if_version) {
		ret = smu_send_smc_msg(smu, SMU_MSG_GetDriverIfVersion);
		if (ret)
			return ret;

		ret = smu_read_smc_arg(smu, if_version);
		if (ret)
			return ret;
	}

	if (smu_version) {
		ret = smu_send_smc_msg(smu, SMU_MSG_GetSmuVersion);
		if (ret)
			return ret;

		ret = smu_read_smc_arg(smu, smu_version);
		if (ret)
			return ret;
	}

	return ret;
}

int smu_set_soft_freq_range(struct smu_context *smu, enum smu_clk_type clk_type,
			    uint32_t min, uint32_t max)
{
	int ret = 0, clk_id = 0;
	uint32_t param;

	if (min <= 0 && max <= 0)
		return -EINVAL;

	if (!smu_clk_dpm_is_enabled(smu, clk_type))
		return 0;

	clk_id = smu_clk_get_index(smu, clk_type);
	if (clk_id < 0)
		return clk_id;

	if (max > 0) {
		param = (uint32_t)((clk_id << 16) | (max & 0xffff));
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMaxByFreq,
						  param);
		if (ret)
			return ret;
	}

	if (min > 0) {
		param = (uint32_t)((clk_id << 16) | (min & 0xffff));
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMinByFreq,
						  param);
		if (ret)
			return ret;
	}


	return ret;
}

int smu_set_hard_freq_range(struct smu_context *smu, enum smu_clk_type clk_type,
			    uint32_t min, uint32_t max)
{
	int ret = 0, clk_id = 0;
	uint32_t param;

	if (min <= 0 && max <= 0)
		return -EINVAL;

	if (!smu_clk_dpm_is_enabled(smu, clk_type))
		return 0;

	clk_id = smu_clk_get_index(smu, clk_type);
	if (clk_id < 0)
		return clk_id;

	if (max > 0) {
		param = (uint32_t)((clk_id << 16) | (max & 0xffff));
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetHardMaxByFreq,
						  param);
		if (ret)
			return ret;
	}

	if (min > 0) {
		param = (uint32_t)((clk_id << 16) | (min & 0xffff));
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetHardMinByFreq,
						  param);
		if (ret)
			return ret;
	}


	return ret;
}

int smu_get_dpm_freq_range(struct smu_context *smu, enum smu_clk_type clk_type,
			   uint32_t *min, uint32_t *max)
{
	int ret = 0, clk_id = 0;
	uint32_t param = 0;
	uint32_t clock_limit;

	if (!min && !max)
		return -EINVAL;

	if (!smu_clk_dpm_is_enabled(smu, clk_type)) {
		switch (clk_type) {
		case SMU_MCLK:
		case SMU_UCLK:
			clock_limit = smu->smu_table.boot_values.uclk;
			break;
		case SMU_GFXCLK:
		case SMU_SCLK:
			clock_limit = smu->smu_table.boot_values.gfxclk;
			break;
		case SMU_SOCCLK:
			clock_limit = smu->smu_table.boot_values.socclk;
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

	mutex_lock(&smu->mutex);
	clk_id = smu_clk_get_index(smu, clk_type);
	if (clk_id < 0) {
		ret = -EINVAL;
		goto failed;
	}

	param = (clk_id & 0xffff) << 16;

	if (max) {
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_GetMaxDpmFreq, param);
		if (ret)
			goto failed;
		ret = smu_read_smc_arg(smu, max);
		if (ret)
			goto failed;
	}

	if (min) {
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_GetMinDpmFreq, param);
		if (ret)
			goto failed;
		ret = smu_read_smc_arg(smu, min);
		if (ret)
			goto failed;
	}

failed:
	mutex_unlock(&smu->mutex);
	return ret;
}

int smu_get_dpm_freq_by_index(struct smu_context *smu, enum smu_clk_type clk_type,
			      uint16_t level, uint32_t *value)
{
	int ret = 0, clk_id = 0;
	uint32_t param;

	if (!value)
		return -EINVAL;

	if (!smu_clk_dpm_is_enabled(smu, clk_type))
		return 0;

	clk_id = smu_clk_get_index(smu, clk_type);
	if (clk_id < 0)
		return clk_id;

	param = (uint32_t)(((clk_id & 0xffff) << 16) | (level & 0xffff));

	ret = smu_send_smc_msg_with_param(smu,SMU_MSG_GetDpmFreqByIndex,
					  param);
	if (ret)
		return ret;

	ret = smu_read_smc_arg(smu, &param);
	if (ret)
		return ret;

	/* BIT31:  0 - Fine grained DPM, 1 - Dicrete DPM
	 * now, we un-support it */
	*value = param & 0x7fffffff;

	return ret;
}

int smu_get_dpm_level_count(struct smu_context *smu, enum smu_clk_type clk_type,
			    uint32_t *value)
{
	return smu_get_dpm_freq_by_index(smu, clk_type, 0xff, value);
}

bool smu_clk_dpm_is_enabled(struct smu_context *smu, enum smu_clk_type clk_type)
{
	enum smu_feature_mask feature_id = 0;

	switch (clk_type) {
	case SMU_MCLK:
	case SMU_UCLK:
		feature_id = SMU_FEATURE_DPM_UCLK_BIT;
		break;
	case SMU_GFXCLK:
	case SMU_SCLK:
		feature_id = SMU_FEATURE_DPM_GFXCLK_BIT;
		break;
	case SMU_SOCCLK:
		feature_id = SMU_FEATURE_DPM_SOCCLK_BIT;
		break;
	default:
		return true;
	}

	if(!smu_feature_is_enabled(smu, feature_id)) {
		pr_warn("smu %d clk dpm feature %d is not enabled\n", clk_type, feature_id);
		return false;
	}

	return true;
}


int smu_dpm_set_power_gate(struct smu_context *smu, uint32_t block_type,
			   bool gate)
{
	int ret = 0;

	switch (block_type) {
	case AMD_IP_BLOCK_TYPE_UVD:
		ret = smu_dpm_set_uvd_enable(smu, gate);
		break;
	case AMD_IP_BLOCK_TYPE_VCE:
		ret = smu_dpm_set_vce_enable(smu, gate);
		break;
	case AMD_IP_BLOCK_TYPE_GFX:
		ret = smu_gfx_off_control(smu, gate);
		break;
	default:
		break;
	}

	return ret;
}

enum amd_pm_state_type smu_get_current_power_state(struct smu_context *smu)
{
	/* not support power state */
	return POWER_STATE_TYPE_DEFAULT;
}

int smu_get_power_num_states(struct smu_context *smu,
			     struct pp_states_info *state_info)
{
	if (!state_info)
		return -EINVAL;

	/* not support power state */
	memset(state_info, 0, sizeof(struct pp_states_info));
	state_info->nums = 1;
	state_info->states[0] = POWER_STATE_TYPE_DEFAULT;

	return 0;
}

int smu_common_read_sensor(struct smu_context *smu, enum amd_pp_sensors sensor,
			   void *data, uint32_t *size)
{
	struct smu_power_context *smu_power = &smu->smu_power;
	struct smu_power_gate *power_gate = &smu_power->power_gate;
	int ret = 0;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_STABLE_PSTATE_SCLK:
		*((uint32_t *)data) = smu->pstate_sclk;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_STABLE_PSTATE_MCLK:
		*((uint32_t *)data) = smu->pstate_mclk;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_ENABLED_SMC_FEATURES_MASK:
		ret = smu_feature_get_enabled_mask(smu, (uint32_t *)data, 2);
		*size = 8;
		break;
	case AMDGPU_PP_SENSOR_UVD_POWER:
		*(uint32_t *)data = smu_feature_is_enabled(smu, SMU_FEATURE_DPM_UVD_BIT) ? 1 : 0;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_VCE_POWER:
		*(uint32_t *)data = smu_feature_is_enabled(smu, SMU_FEATURE_DPM_VCE_BIT) ? 1 : 0;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_VCN_POWER_STATE:
		*(uint32_t *)data = power_gate->vcn_gated ? 0 : 1;
		*size = 4;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret)
		*size = 0;

	return ret;
}

int smu_update_table(struct smu_context *smu, enum smu_table_id table_index, int argument,
		     void *table_data, bool drv2smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *table = NULL;
	int ret = 0;
	int table_id = smu_table_get_index(smu, table_index);

	if (!table_data || table_id >= smu_table->table_count)
		return -EINVAL;

	table = &smu_table->tables[table_index];

	if (drv2smu)
		memcpy(table->cpu_addr, table_data, table->size);

	ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetDriverDramAddrHigh,
					  upper_32_bits(table->mc_address));
	if (ret)
		return ret;
	ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetDriverDramAddrLow,
					  lower_32_bits(table->mc_address));
	if (ret)
		return ret;
	ret = smu_send_smc_msg_with_param(smu, drv2smu ?
					  SMU_MSG_TransferTableDram2Smu :
					  SMU_MSG_TransferTableSmu2Dram,
					  table_id | ((argument & 0xFFFF) << 16));
	if (ret)
		return ret;

	if (!drv2smu)
		memcpy(table_data, table->cpu_addr, table->size);

	return ret;
}

bool is_support_sw_smu(struct amdgpu_device *adev)
{
	if (adev->asic_type == CHIP_VEGA20)
		return (amdgpu_dpm == 2) ? true : false;
	else if (adev->asic_type >= CHIP_NAVI10)
		return true;
	else
		return false;
}

int smu_sys_get_pp_table(struct smu_context *smu, void **table)
{
	struct smu_table_context *smu_table = &smu->smu_table;

	if (!smu_table->power_play_table && !smu_table->hardcode_pptable)
		return -EINVAL;

	if (smu_table->hardcode_pptable)
		*table = smu_table->hardcode_pptable;
	else
		*table = smu_table->power_play_table;

	return smu_table->power_play_table_size;
}

int smu_sys_set_pp_table(struct smu_context *smu,  void *buf, size_t size)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	ATOM_COMMON_TABLE_HEADER *header = (ATOM_COMMON_TABLE_HEADER *)buf;
	int ret = 0;

	if (!smu->pm_enabled)
		return -EINVAL;
	if (header->usStructureSize != size) {
		pr_err("pp table size not matched !\n");
		return -EIO;
	}

	mutex_lock(&smu->mutex);
	if (!smu_table->hardcode_pptable)
		smu_table->hardcode_pptable = kzalloc(size, GFP_KERNEL);
	if (!smu_table->hardcode_pptable) {
		ret = -ENOMEM;
		goto failed;
	}

	memcpy(smu_table->hardcode_pptable, buf, size);
	smu_table->power_play_table = smu_table->hardcode_pptable;
	smu_table->power_play_table_size = size;
	mutex_unlock(&smu->mutex);

	ret = smu_reset(smu);
	if (ret)
		pr_info("smu reset failed, ret = %d\n", ret);

	return ret;

failed:
	mutex_unlock(&smu->mutex);
	return ret;
}

int smu_feature_init_dpm(struct smu_context *smu)
{
	struct smu_feature *feature = &smu->smu_feature;
	int ret = 0;
	uint32_t allowed_feature_mask[SMU_FEATURE_MAX/32];

	if (!smu->pm_enabled)
		return ret;
	mutex_lock(&feature->mutex);
	bitmap_zero(feature->allowed, SMU_FEATURE_MAX);
	mutex_unlock(&feature->mutex);

	ret = smu_get_allowed_feature_mask(smu, allowed_feature_mask,
					     SMU_FEATURE_MAX/32);
	if (ret)
		return ret;

	mutex_lock(&feature->mutex);
	bitmap_or(feature->allowed, feature->allowed,
		      (unsigned long *)allowed_feature_mask,
		      feature->feature_num);
	mutex_unlock(&feature->mutex);

	return ret;
}

int smu_feature_is_enabled(struct smu_context *smu, enum smu_feature_mask mask)
{
	struct smu_feature *feature = &smu->smu_feature;
	uint32_t feature_id;
	int ret = 0;

	feature_id = smu_feature_get_index(smu, mask);

	WARN_ON(feature_id > feature->feature_num);

	mutex_lock(&feature->mutex);
	ret = test_bit(feature_id, feature->enabled);
	mutex_unlock(&feature->mutex);

	return ret;
}

int smu_feature_set_enabled(struct smu_context *smu, enum smu_feature_mask mask,
			    bool enable)
{
	struct smu_feature *feature = &smu->smu_feature;
	uint32_t feature_id;
	int ret = 0;

	feature_id = smu_feature_get_index(smu, mask);

	WARN_ON(feature_id > feature->feature_num);

	mutex_lock(&feature->mutex);
	ret = smu_feature_update_enable_state(smu, feature_id, enable);
	if (ret)
		goto failed;

	if (enable)
		test_and_set_bit(feature_id, feature->enabled);
	else
		test_and_clear_bit(feature_id, feature->enabled);

failed:
	mutex_unlock(&feature->mutex);

	return ret;
}

int smu_feature_is_supported(struct smu_context *smu, enum smu_feature_mask mask)
{
	struct smu_feature *feature = &smu->smu_feature;
	uint32_t feature_id;
	int ret = 0;

	feature_id = smu_feature_get_index(smu, mask);

	WARN_ON(feature_id > feature->feature_num);

	mutex_lock(&feature->mutex);
	ret = test_bit(feature_id, feature->supported);
	mutex_unlock(&feature->mutex);

	return ret;
}

int smu_feature_set_supported(struct smu_context *smu,
			      enum smu_feature_mask mask,
			      bool enable)
{
	struct smu_feature *feature = &smu->smu_feature;
	uint32_t feature_id;
	int ret = 0;

	feature_id = smu_feature_get_index(smu, mask);

	WARN_ON(feature_id > feature->feature_num);

	mutex_lock(&feature->mutex);
	if (enable)
		test_and_set_bit(feature_id, feature->supported);
	else
		test_and_clear_bit(feature_id, feature->supported);
	mutex_unlock(&feature->mutex);

	return ret;
}

static int smu_set_funcs(struct amdgpu_device *adev)
{
	struct smu_context *smu = &adev->smu;

	switch (adev->asic_type) {
	case CHIP_VEGA20:
	case CHIP_NAVI10:
		if (adev->pm.pp_feature & PP_OVERDRIVE_MASK)
			smu->od_enabled = true;
		smu_v11_0_set_smu_funcs(smu);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int smu_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;

	smu->adev = adev;
	smu->pm_enabled = !!amdgpu_dpm;
	mutex_init(&smu->mutex);

	return smu_set_funcs(adev);
}

static int smu_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;

	if (!smu->pm_enabled)
		return 0;
	mutex_lock(&smu->mutex);
	smu_handle_task(&adev->smu,
			smu->smu_dpm.dpm_level,
			AMD_PP_TASK_COMPLETE_INIT);
	mutex_unlock(&smu->mutex);

	return 0;
}

int smu_get_atom_data_table(struct smu_context *smu, uint32_t table,
			    uint16_t *size, uint8_t *frev, uint8_t *crev,
			    uint8_t **addr)
{
	struct amdgpu_device *adev = smu->adev;
	uint16_t data_start;

	if (!amdgpu_atom_parse_data_header(adev->mode_info.atom_context, table,
					   size, frev, crev, &data_start))
		return -EINVAL;

	*addr = (uint8_t *)adev->mode_info.atom_context->bios + data_start;

	return 0;
}

static int smu_initialize_pptable(struct smu_context *smu)
{
	/* TODO */
	return 0;
}

static int smu_smc_table_sw_init(struct smu_context *smu)
{
	int ret;

	ret = smu_initialize_pptable(smu);
	if (ret) {
		pr_err("Failed to init smu_initialize_pptable!\n");
		return ret;
	}

	/**
	 * Create smu_table structure, and init smc tables such as
	 * TABLE_PPTABLE, TABLE_WATERMARKS, TABLE_SMU_METRICS, and etc.
	 */
	ret = smu_init_smc_tables(smu);
	if (ret) {
		pr_err("Failed to init smc tables!\n");
		return ret;
	}

	/**
	 * Create smu_power_context structure, and allocate smu_dpm_context and
	 * context size to fill the smu_power_context data.
	 */
	ret = smu_init_power(smu);
	if (ret) {
		pr_err("Failed to init smu_init_power!\n");
		return ret;
	}

	return 0;
}

static int smu_smc_table_sw_fini(struct smu_context *smu)
{
	int ret;

	ret = smu_fini_smc_tables(smu);
	if (ret) {
		pr_err("Failed to smu_fini_smc_tables!\n");
		return ret;
	}

	return 0;
}

static int smu_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;
	int ret;

	smu->pool_size = adev->pm.smu_prv_buffer_size;
	smu->smu_feature.feature_num = SMU_FEATURE_MAX;
	mutex_init(&smu->smu_feature.mutex);
	bitmap_zero(smu->smu_feature.supported, SMU_FEATURE_MAX);
	bitmap_zero(smu->smu_feature.enabled, SMU_FEATURE_MAX);
	bitmap_zero(smu->smu_feature.allowed, SMU_FEATURE_MAX);

	mutex_init(&smu->smu_baco.mutex);
	smu->smu_baco.state = SMU_BACO_STATE_EXIT;
	smu->smu_baco.platform_support = false;

	smu->watermarks_bitmap = 0;
	smu->power_profile_mode = PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT;
	smu->default_power_profile_mode = PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT;

	smu->workload_mask = 1 << smu->workload_prority[PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT];
	smu->workload_prority[PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT] = 0;
	smu->workload_prority[PP_SMC_POWER_PROFILE_FULLSCREEN3D] = 1;
	smu->workload_prority[PP_SMC_POWER_PROFILE_POWERSAVING] = 2;
	smu->workload_prority[PP_SMC_POWER_PROFILE_VIDEO] = 3;
	smu->workload_prority[PP_SMC_POWER_PROFILE_VR] = 4;
	smu->workload_prority[PP_SMC_POWER_PROFILE_COMPUTE] = 5;
	smu->workload_prority[PP_SMC_POWER_PROFILE_CUSTOM] = 6;

	smu->workload_setting[0] = PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT;
	smu->workload_setting[1] = PP_SMC_POWER_PROFILE_FULLSCREEN3D;
	smu->workload_setting[2] = PP_SMC_POWER_PROFILE_POWERSAVING;
	smu->workload_setting[3] = PP_SMC_POWER_PROFILE_VIDEO;
	smu->workload_setting[4] = PP_SMC_POWER_PROFILE_VR;
	smu->workload_setting[5] = PP_SMC_POWER_PROFILE_COMPUTE;
	smu->workload_setting[6] = PP_SMC_POWER_PROFILE_CUSTOM;
	smu->display_config = &adev->pm.pm_display_cfg;

	smu->smu_dpm.dpm_level = AMD_DPM_FORCED_LEVEL_AUTO;
	smu->smu_dpm.requested_dpm_level = AMD_DPM_FORCED_LEVEL_AUTO;
	ret = smu_init_microcode(smu);
	if (ret) {
		pr_err("Failed to load smu firmware!\n");
		return ret;
	}

	ret = smu_smc_table_sw_init(smu);
	if (ret) {
		pr_err("Failed to sw init smc table!\n");
		return ret;
	}

	ret = smu_register_irq_handler(smu);
	if (ret) {
		pr_err("Failed to register smc irq handler!\n");
		return ret;
	}

	return 0;
}

static int smu_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;
	int ret;

	kfree(smu->irq_source);
	smu->irq_source = NULL;

	ret = smu_smc_table_sw_fini(smu);
	if (ret) {
		pr_err("Failed to sw fini smc table!\n");
		return ret;
	}

	ret = smu_fini_power(smu);
	if (ret) {
		pr_err("Failed to init smu_fini_power!\n");
		return ret;
	}

	return 0;
}

static int smu_init_fb_allocations(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = smu_table->tables;
	uint32_t table_count = smu_table->table_count;
	uint32_t i = 0;
	int32_t ret = 0;

	if (table_count <= 0)
		return -EINVAL;

	for (i = 0 ; i < table_count; i++) {
		if (tables[i].size == 0)
			continue;
		ret = amdgpu_bo_create_kernel(adev,
					      tables[i].size,
					      tables[i].align,
					      tables[i].domain,
					      &tables[i].bo,
					      &tables[i].mc_address,
					      &tables[i].cpu_addr);
		if (ret)
			goto failed;
	}

	return 0;
failed:
	for (; i > 0; i--) {
		if (tables[i].size == 0)
			continue;
		amdgpu_bo_free_kernel(&tables[i].bo,
				      &tables[i].mc_address,
				      &tables[i].cpu_addr);

	}
	return ret;
}

static int smu_fini_fb_allocations(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = smu_table->tables;
	uint32_t table_count = smu_table->table_count;
	uint32_t i = 0;

	if (table_count == 0 || tables == NULL)
		return 0;

	for (i = 0 ; i < table_count; i++) {
		if (tables[i].size == 0)
			continue;
		amdgpu_bo_free_kernel(&tables[i].bo,
				      &tables[i].mc_address,
				      &tables[i].cpu_addr);
	}

	return 0;
}

static int smu_override_pcie_parameters(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t pcie_gen = 0, pcie_width = 0, smu_pcie_arg;
	int ret;

	if (adev->pm.pcie_gen_mask & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN4)
		pcie_gen = 3;
	else if (adev->pm.pcie_gen_mask & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN3)
		pcie_gen = 2;
	else if (adev->pm.pcie_gen_mask & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN2)
		pcie_gen = 1;
	else if (adev->pm.pcie_gen_mask & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN1)
		pcie_gen = 0;

	/* Bit 31:16: LCLK DPM level. 0 is DPM0, and 1 is DPM1
	 * Bit 15:8:  PCIE GEN, 0 to 3 corresponds to GEN1 to GEN4
	 * Bit 7:0:   PCIE lane width, 1 to 7 corresponds is x1 to x32
	 */
	if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X16)
		pcie_width = 6;
	else if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X12)
		pcie_width = 5;
	else if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X8)
		pcie_width = 4;
	else if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X4)
		pcie_width = 3;
	else if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X2)
		pcie_width = 2;
	else if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X1)
		pcie_width = 1;

	smu_pcie_arg = (1 << 16) | (pcie_gen << 8) | pcie_width;
	ret = smu_send_smc_msg_with_param(smu,
					  SMU_MSG_OverridePcieParameters,
					  smu_pcie_arg);
	if (ret)
		pr_err("[%s] Attempt to override pcie params failed!\n", __func__);
	return ret;
}

static int smu_smc_table_hw_init(struct smu_context *smu,
				 bool initialize)
{
	struct amdgpu_device *adev = smu->adev;
	int ret;

	if (smu_is_dpm_running(smu) && adev->in_suspend) {
		pr_info("dpm has been enabled\n");
		return 0;
	}

	ret = smu_init_display_count(smu, 0);
	if (ret)
		return ret;

	if (initialize) {
		/* get boot_values from vbios to set revision, gfxclk, and etc. */
		ret = smu_get_vbios_bootup_values(smu);
		if (ret)
			return ret;

		ret = smu_setup_pptable(smu);
		if (ret)
			return ret;

		ret = smu_get_clk_info_from_vbios(smu);
		if (ret)
			return ret;

		/*
		 * check if the format_revision in vbios is up to pptable header
		 * version, and the structure size is not 0.
		 */
		ret = smu_check_pptable(smu);
		if (ret)
			return ret;

		/*
		 * allocate vram bos to store smc table contents.
		 */
		ret = smu_init_fb_allocations(smu);
		if (ret)
			return ret;

		/*
		 * Parse pptable format and fill PPTable_t smc_pptable to
		 * smu_table_context structure. And read the smc_dpm_table from vbios,
		 * then fill it into smc_pptable.
		 */
		ret = smu_parse_pptable(smu);
		if (ret)
			return ret;

		/*
		 * Send msg GetDriverIfVersion to check if the return value is equal
		 * with DRIVER_IF_VERSION of smc header.
		 */
		ret = smu_check_fw_version(smu);
		if (ret)
			return ret;
	}

	/*
	 * Copy pptable bo in the vram to smc with SMU MSGs such as
	 * SetDriverDramAddr and TransferTableDram2Smu.
	 */
	ret = smu_write_pptable(smu);
	if (ret)
		return ret;

	/* issue RunAfllBtc msg */
	ret = smu_run_afll_btc(smu);
	if (ret)
		return ret;

	ret = smu_feature_set_allowed_mask(smu);
	if (ret)
		return ret;

	ret = smu_system_features_control(smu, true);
	if (ret)
		return ret;

	ret = smu_override_pcie_parameters(smu);
	if (ret)
		return ret;

	ret = smu_notify_display_change(smu);
	if (ret)
		return ret;

	/*
	 * Set min deep sleep dce fclk with bootup value from vbios via
	 * SetMinDeepSleepDcefclk MSG.
	 */
	ret = smu_set_min_dcef_deep_sleep(smu);
	if (ret)
		return ret;

	/*
	 * Set initialized values (get from vbios) to dpm tables context such as
	 * gfxclk, memclk, dcefclk, and etc. And enable the DPM feature for each
	 * type of clks.
	 */
	if (initialize) {
		ret = smu_populate_smc_pptable(smu);
		if (ret)
			return ret;

		ret = smu_init_max_sustainable_clocks(smu);
		if (ret)
			return ret;
	}

	ret = smu_set_default_od_settings(smu, initialize);
	if (ret)
		return ret;

	if (initialize) {
		ret = smu_populate_umd_state_clk(smu);
		if (ret)
			return ret;

		ret = smu_get_power_limit(smu, &smu->default_power_limit, false);
		if (ret)
			return ret;
	}

	/*
	 * Set PMSTATUSLOG table bo address with SetToolsDramAddr MSG for tools.
	 */
	ret = smu_set_tool_table_location(smu);

	if (!smu_is_dpm_running(smu))
		pr_info("dpm has been disabled\n");

	return ret;
}

/**
 * smu_alloc_memory_pool - allocate memory pool in the system memory
 *
 * @smu: amdgpu_device pointer
 *
 * This memory pool will be used for SMC use and msg SetSystemVirtualDramAddr
 * and DramLogSetDramAddr can notify it changed.
 *
 * Returns 0 on success, error on failure.
 */
static int smu_alloc_memory_pool(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *memory_pool = &smu_table->memory_pool;
	uint64_t pool_size = smu->pool_size;
	int ret = 0;

	if (pool_size == SMU_MEMORY_POOL_SIZE_ZERO)
		return ret;

	memory_pool->size = pool_size;
	memory_pool->align = PAGE_SIZE;
	memory_pool->domain = AMDGPU_GEM_DOMAIN_GTT;

	switch (pool_size) {
	case SMU_MEMORY_POOL_SIZE_256_MB:
	case SMU_MEMORY_POOL_SIZE_512_MB:
	case SMU_MEMORY_POOL_SIZE_1_GB:
	case SMU_MEMORY_POOL_SIZE_2_GB:
		ret = amdgpu_bo_create_kernel(adev,
					      memory_pool->size,
					      memory_pool->align,
					      memory_pool->domain,
					      &memory_pool->bo,
					      &memory_pool->mc_address,
					      &memory_pool->cpu_addr);
		break;
	default:
		break;
	}

	return ret;
}

static int smu_free_memory_pool(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *memory_pool = &smu_table->memory_pool;
	int ret = 0;

	if (memory_pool->size == SMU_MEMORY_POOL_SIZE_ZERO)
		return ret;

	amdgpu_bo_free_kernel(&memory_pool->bo,
			      &memory_pool->mc_address,
			      &memory_pool->cpu_addr);

	memset(memory_pool, 0, sizeof(struct smu_table));

	return ret;
}

static int smu_hw_init(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		ret = smu_check_fw_status(smu);
		if (ret) {
			pr_err("SMC firmware status is not correct\n");
			return ret;
		}
	}

	ret = smu_feature_init_dpm(smu);
	if (ret)
		goto failed;

	ret = smu_smc_table_hw_init(smu, true);
	if (ret)
		goto failed;

	ret = smu_alloc_memory_pool(smu);
	if (ret)
		goto failed;

	/*
	 * Use msg SetSystemVirtualDramAddr and DramLogSetDramAddr can notify
	 * pool location.
	 */
	ret = smu_notify_memory_pool_location(smu);
	if (ret)
		goto failed;

	ret = smu_start_thermal_control(smu);
	if (ret)
		goto failed;

	if (!smu->pm_enabled)
		adev->pm.dpm_enabled = false;
	else
		adev->pm.dpm_enabled = true;	/* TODO: will set dpm_enabled flag while VCN and DAL DPM is workable */

	pr_info("SMU is initialized successfully!\n");

	return 0;

failed:
	return ret;
}

static int smu_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;
	struct smu_table_context *table_context = &smu->smu_table;
	int ret = 0;

	kfree(table_context->driver_pptable);
	table_context->driver_pptable = NULL;

	kfree(table_context->max_sustainable_clocks);
	table_context->max_sustainable_clocks = NULL;

	kfree(table_context->overdrive_table);
	table_context->overdrive_table = NULL;

	ret = smu_fini_fb_allocations(smu);
	if (ret)
		return ret;

	ret = smu_free_memory_pool(smu);
	if (ret)
		return ret;

	return 0;
}

int smu_reset(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;

	ret = smu_hw_fini(adev);
	if (ret)
		return ret;

	ret = smu_hw_init(adev);
	if (ret)
		return ret;

	return ret;
}

static int smu_suspend(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;
	bool baco_feature_is_enabled = smu_feature_is_enabled(smu, SMU_FEATURE_BACO_BIT);

	ret = smu_system_features_control(smu, false);
	if (ret)
		return ret;

	if (adev->in_gpu_reset && baco_feature_is_enabled) {
		ret = smu_feature_set_enabled(smu, SMU_FEATURE_BACO_BIT, true);
		if (ret) {
			pr_warn("set BACO feature enabled failed, return %d\n", ret);
			return ret;
		}
	}

	smu->watermarks_bitmap &= ~(WATERMARKS_LOADED);

	if (adev->asic_type >= CHIP_NAVI10 &&
	    adev->gfx.rlc.funcs->stop)
		adev->gfx.rlc.funcs->stop(adev);

	return 0;
}

static int smu_resume(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;

	pr_info("SMU is resuming...\n");

	mutex_lock(&smu->mutex);

	ret = smu_smc_table_hw_init(smu, false);
	if (ret)
		goto failed;

	ret = smu_start_thermal_control(smu);
	if (ret)
		goto failed;

	mutex_unlock(&smu->mutex);

	pr_info("SMU is resumed successfully!\n");

	return 0;
failed:
	mutex_unlock(&smu->mutex);
	return ret;
}

int smu_display_configuration_change(struct smu_context *smu,
				     const struct amd_pp_display_configuration *display_config)
{
	int index = 0;
	int num_of_active_display = 0;

	if (!smu->pm_enabled || !is_support_sw_smu(smu->adev))
		return -EINVAL;

	if (!display_config)
		return -EINVAL;

	mutex_lock(&smu->mutex);

	smu_set_deep_sleep_dcefclk(smu,
				   display_config->min_dcef_deep_sleep_set_clk / 100);

	for (index = 0; index < display_config->num_path_including_non_display; index++) {
		if (display_config->displays[index].controller_id != 0)
			num_of_active_display++;
	}

	smu_set_active_display_count(smu, num_of_active_display);

	smu_store_cc6_data(smu, display_config->cpu_pstate_separation_time,
			   display_config->cpu_cc6_disable,
			   display_config->cpu_pstate_disable,
			   display_config->nb_pstate_switch_disable);

	mutex_unlock(&smu->mutex);

	return 0;
}

static int smu_get_clock_info(struct smu_context *smu,
			      struct smu_clock_info *clk_info,
			      enum smu_perf_level_designation designation)
{
	int ret;
	struct smu_performance_level level = {0};

	if (!clk_info)
		return -EINVAL;

	ret = smu_get_perf_level(smu, PERF_LEVEL_ACTIVITY, &level);
	if (ret)
		return -EINVAL;

	clk_info->min_mem_clk = level.memory_clock;
	clk_info->min_eng_clk = level.core_clock;
	clk_info->min_bus_bandwidth = level.non_local_mem_freq * level.non_local_mem_width;

	ret = smu_get_perf_level(smu, designation, &level);
	if (ret)
		return -EINVAL;

	clk_info->min_mem_clk = level.memory_clock;
	clk_info->min_eng_clk = level.core_clock;
	clk_info->min_bus_bandwidth = level.non_local_mem_freq * level.non_local_mem_width;

	return 0;
}

int smu_get_current_clocks(struct smu_context *smu,
			   struct amd_pp_clock_info *clocks)
{
	struct amd_pp_simple_clock_info simple_clocks = {0};
	struct smu_clock_info hw_clocks;
	int ret = 0;

	if (!is_support_sw_smu(smu->adev))
		return -EINVAL;

	mutex_lock(&smu->mutex);

	smu_get_dal_power_level(smu, &simple_clocks);

	if (smu->support_power_containment)
		ret = smu_get_clock_info(smu, &hw_clocks,
					 PERF_LEVEL_POWER_CONTAINMENT);
	else
		ret = smu_get_clock_info(smu, &hw_clocks, PERF_LEVEL_ACTIVITY);

	if (ret) {
		pr_err("Error in smu_get_clock_info\n");
		goto failed;
	}

	clocks->min_engine_clock = hw_clocks.min_eng_clk;
	clocks->max_engine_clock = hw_clocks.max_eng_clk;
	clocks->min_memory_clock = hw_clocks.min_mem_clk;
	clocks->max_memory_clock = hw_clocks.max_mem_clk;
	clocks->min_bus_bandwidth = hw_clocks.min_bus_bandwidth;
	clocks->max_bus_bandwidth = hw_clocks.max_bus_bandwidth;
	clocks->max_engine_clock_in_sr = hw_clocks.max_eng_clk;
	clocks->min_engine_clock_in_sr = hw_clocks.min_eng_clk;

        if (simple_clocks.level == 0)
                clocks->max_clocks_state = PP_DAL_POWERLEVEL_7;
        else
                clocks->max_clocks_state = simple_clocks.level;

        if (!smu_get_current_shallow_sleep_clocks(smu, &hw_clocks)) {
                clocks->max_engine_clock_in_sr = hw_clocks.max_eng_clk;
                clocks->min_engine_clock_in_sr = hw_clocks.min_eng_clk;
        }

failed:
	mutex_unlock(&smu->mutex);
	return ret;
}

static int smu_set_clockgating_state(void *handle,
				     enum amd_clockgating_state state)
{
	return 0;
}

static int smu_set_powergating_state(void *handle,
				     enum amd_powergating_state state)
{
	return 0;
}

static int smu_enable_umd_pstate(void *handle,
		      enum amd_dpm_forced_level *level)
{
	uint32_t profile_mode_mask = AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD |
					AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK |
					AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK |
					AMD_DPM_FORCED_LEVEL_PROFILE_PEAK;

	struct smu_context *smu = (struct smu_context*)(handle);
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);
	if (!smu->pm_enabled || !smu_dpm_ctx->dpm_context)
		return -EINVAL;

	if (!(smu_dpm_ctx->dpm_level & profile_mode_mask)) {
		/* enter umd pstate, save current level, disable gfx cg*/
		if (*level & profile_mode_mask) {
			smu_dpm_ctx->saved_dpm_level = smu_dpm_ctx->dpm_level;
			smu_dpm_ctx->enable_umd_pstate = true;
			amdgpu_device_ip_set_clockgating_state(smu->adev,
							       AMD_IP_BLOCK_TYPE_GFX,
							       AMD_CG_STATE_UNGATE);
			amdgpu_device_ip_set_powergating_state(smu->adev,
							       AMD_IP_BLOCK_TYPE_GFX,
							       AMD_PG_STATE_UNGATE);
		}
	} else {
		/* exit umd pstate, restore level, enable gfx cg*/
		if (!(*level & profile_mode_mask)) {
			if (*level == AMD_DPM_FORCED_LEVEL_PROFILE_EXIT)
				*level = smu_dpm_ctx->saved_dpm_level;
			smu_dpm_ctx->enable_umd_pstate = false;
			amdgpu_device_ip_set_clockgating_state(smu->adev,
							       AMD_IP_BLOCK_TYPE_GFX,
							       AMD_CG_STATE_GATE);
			amdgpu_device_ip_set_powergating_state(smu->adev,
							       AMD_IP_BLOCK_TYPE_GFX,
							       AMD_PG_STATE_GATE);
		}
	}

	return 0;
}

static int smu_default_set_performance_level(struct smu_context *smu, enum amd_dpm_forced_level level)
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
	case AMD_DPM_FORCED_LEVEL_PROFILE_PEAK:
		ret = smu_get_profiling_clk_mask(smu, level,
						 &sclk_mask,
						 &mclk_mask,
						 &soc_mask);
		if (ret)
			return ret;
		smu_force_clk_levels(smu, SMU_SCLK, 1 << sclk_mask);
		smu_force_clk_levels(smu, SMU_MCLK, 1 << mclk_mask);
		smu_force_clk_levels(smu, SMU_SOCCLK, 1 << soc_mask);
		break;
	case AMD_DPM_FORCED_LEVEL_MANUAL:
	case AMD_DPM_FORCED_LEVEL_PROFILE_EXIT:
	default:
		break;
	}
	return ret;
}

int smu_adjust_power_state_dynamic(struct smu_context *smu,
				   enum amd_dpm_forced_level level,
				   bool skip_display_settings)
{
	int ret = 0;
	int index = 0;
	long workload;
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);

	if (!smu->pm_enabled)
		return -EINVAL;
	if (!skip_display_settings) {
		ret = smu_display_config_changed(smu);
		if (ret) {
			pr_err("Failed to change display config!");
			return ret;
		}
	}

	if (!smu->pm_enabled)
		return -EINVAL;
	ret = smu_apply_clocks_adjust_rules(smu);
	if (ret) {
		pr_err("Failed to apply clocks adjust rules!");
		return ret;
	}

	if (!skip_display_settings) {
		ret = smu_notify_smc_dispaly_config(smu);
		if (ret) {
			pr_err("Failed to notify smc display config!");
			return ret;
		}
	}

	if (smu_dpm_ctx->dpm_level != level) {
		ret = smu_asic_set_performance_level(smu, level);
		if (ret) {
			ret = smu_default_set_performance_level(smu, level);
		}
		if (!ret)
			smu_dpm_ctx->dpm_level = level;
	}

	if (smu_dpm_ctx->dpm_level != AMD_DPM_FORCED_LEVEL_MANUAL) {
		index = fls(smu->workload_mask);
		index = index > 0 && index <= WORKLOAD_POLICY_MAX ? index - 1 : 0;
		workload = smu->workload_setting[index];

		if (smu->power_profile_mode != workload)
			smu_set_power_profile_mode(smu, &workload, 0);
	}

	return ret;
}

int smu_handle_task(struct smu_context *smu,
		    enum amd_dpm_forced_level level,
		    enum amd_pp_task task_id)
{
	int ret = 0;

	switch (task_id) {
	case AMD_PP_TASK_DISPLAY_CONFIG_CHANGE:
		ret = smu_pre_display_config_changed(smu);
		if (ret)
			return ret;
		ret = smu_set_cpu_power_state(smu);
		if (ret)
			return ret;
		ret = smu_adjust_power_state_dynamic(smu, level, false);
		break;
	case AMD_PP_TASK_COMPLETE_INIT:
	case AMD_PP_TASK_READJUST_POWER_STATE:
		ret = smu_adjust_power_state_dynamic(smu, level, true);
		break;
	default:
		break;
	}

	return ret;
}

enum amd_dpm_forced_level smu_get_performance_level(struct smu_context *smu)
{
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);
	enum amd_dpm_forced_level level;

	if (!smu_dpm_ctx->dpm_context)
		return -EINVAL;

	mutex_lock(&(smu->mutex));
	level = smu_dpm_ctx->dpm_level;
	mutex_unlock(&(smu->mutex));

	return level;
}

int smu_force_performance_level(struct smu_context *smu, enum amd_dpm_forced_level level)
{
	int ret = 0;
	int i;
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);

	if (!smu_dpm_ctx->dpm_context)
		return -EINVAL;

	for (i = 0; i < smu->adev->num_ip_blocks; i++) {
		if (smu->adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_SMC)
			break;
	}


	smu->adev->ip_blocks[i].version->funcs->enable_umd_pstate(smu, &level);
	ret = smu_handle_task(smu, level,
			      AMD_PP_TASK_READJUST_POWER_STATE);
	if (ret)
		return ret;

	mutex_lock(&smu->mutex);
	smu_dpm_ctx->dpm_level = level;
	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_display_count(struct smu_context *smu, uint32_t count)
{
	int ret = 0;

	mutex_lock(&smu->mutex);
	ret = smu_init_display_count(smu, count);
	mutex_unlock(&smu->mutex);

	return ret;
}

const struct amd_ip_funcs smu_ip_funcs = {
	.name = "smu",
	.early_init = smu_early_init,
	.late_init = smu_late_init,
	.sw_init = smu_sw_init,
	.sw_fini = smu_sw_fini,
	.hw_init = smu_hw_init,
	.hw_fini = smu_hw_fini,
	.suspend = smu_suspend,
	.resume = smu_resume,
	.is_idle = NULL,
	.check_soft_reset = NULL,
	.wait_for_idle = NULL,
	.soft_reset = NULL,
	.set_clockgating_state = smu_set_clockgating_state,
	.set_powergating_state = smu_set_powergating_state,
	.enable_umd_pstate = smu_enable_umd_pstate,
};

const struct amdgpu_ip_block_version smu_v11_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_SMC,
	.major = 11,
	.minor = 0,
	.rev = 0,
	.funcs = &smu_ip_funcs,
};
