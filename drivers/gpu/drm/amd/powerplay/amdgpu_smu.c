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
#include <linux/pci.h>

#include "pp_debug.h"
#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "smu_internal.h"
#include "soc15_common.h"
#include "smu_v11_0.h"
#include "smu_v12_0.h"
#include "atom.h"
#include "amd_pcie.h"
#include "vega20_ppt.h"
#include "arcturus_ppt.h"
#include "navi10_ppt.h"
#include "renoir_ppt.h"

#undef __SMU_DUMMY_MAP
#define __SMU_DUMMY_MAP(type)	#type
static const char* __smu_message_names[] = {
	SMU_MESSAGE_TYPES
};

const char *smu_get_message_name(struct smu_context *smu, enum smu_message_type type)
{
	if (type < 0 || type >= SMU_MSG_MAX_COUNT)
		return "unknown smu message";
	return __smu_message_names[type];
}

#undef __SMU_DUMMY_MAP
#define __SMU_DUMMY_MAP(fea)	#fea
static const char* __smu_feature_names[] = {
	SMU_FEATURE_MASKS
};

const char *smu_get_feature_name(struct smu_context *smu, enum smu_feature_mask feature)
{
	if (feature < 0 || feature >= SMU_FEATURE_COUNT)
		return "unknown smu feature";
	return __smu_feature_names[feature];
}

size_t smu_sys_get_pp_feature_mask(struct smu_context *smu, char *buf)
{
	size_t size = 0;
	int ret = 0, i = 0;
	uint32_t feature_mask[2] = { 0 };
	int32_t feature_index = 0;
	uint32_t count = 0;
	uint32_t sort_feature[SMU_FEATURE_COUNT];
	uint64_t hw_feature_count = 0;

	mutex_lock(&smu->mutex);

	ret = smu_feature_get_enabled_mask(smu, feature_mask, 2);
	if (ret)
		goto failed;

	size =  sprintf(buf + size, "features high: 0x%08x low: 0x%08x\n",
			feature_mask[1], feature_mask[0]);

	for (i = 0; i < SMU_FEATURE_COUNT; i++) {
		feature_index = smu_feature_get_index(smu, i);
		if (feature_index < 0)
			continue;
		sort_feature[feature_index] = i;
		hw_feature_count++;
	}

	for (i = 0; i < hw_feature_count; i++) {
		size += sprintf(buf + size, "%02d. %-20s (%2d) : %s\n",
			       count++,
			       smu_get_feature_name(smu, sort_feature[i]),
			       i,
			       !!smu_feature_is_enabled(smu, sort_feature[i]) ?
			       "enabled" : "disabled");
	}

failed:
	mutex_unlock(&smu->mutex);

	return size;
}

static int smu_feature_update_enable_state(struct smu_context *smu,
					   uint64_t feature_mask,
					   bool enabled)
{
	struct smu_feature *feature = &smu->smu_feature;
	uint32_t feature_low = 0, feature_high = 0;
	int ret = 0;

	if (!smu->pm_enabled)
		return ret;

	feature_low = (feature_mask >> 0 ) & 0xffffffff;
	feature_high = (feature_mask >> 32) & 0xffffffff;

	if (enabled) {
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_EnableSmuFeaturesLow,
						  feature_low);
		if (ret)
			return ret;
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_EnableSmuFeaturesHigh,
						  feature_high);
		if (ret)
			return ret;
	} else {
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_DisableSmuFeaturesLow,
						  feature_low);
		if (ret)
			return ret;
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_DisableSmuFeaturesHigh,
						  feature_high);
		if (ret)
			return ret;
	}

	mutex_lock(&feature->mutex);
	if (enabled)
		bitmap_or(feature->enabled, feature->enabled,
				(unsigned long *)(&feature_mask), SMU_FEATURE_MAX);
	else
		bitmap_andnot(feature->enabled, feature->enabled,
				(unsigned long *)(&feature_mask), SMU_FEATURE_MAX);
	mutex_unlock(&feature->mutex);

	return ret;
}

int smu_sys_set_pp_feature_mask(struct smu_context *smu, uint64_t new_mask)
{
	int ret = 0;
	uint32_t feature_mask[2] = { 0 };
	uint64_t feature_2_enabled = 0;
	uint64_t feature_2_disabled = 0;
	uint64_t feature_enables = 0;

	mutex_lock(&smu->mutex);

	ret = smu_feature_get_enabled_mask(smu, feature_mask, 2);
	if (ret)
		goto out;

	feature_enables = ((uint64_t)feature_mask[1] << 32 | (uint64_t)feature_mask[0]);

	feature_2_enabled  = ~feature_enables & new_mask;
	feature_2_disabled = feature_enables & ~new_mask;

	if (feature_2_enabled) {
		ret = smu_feature_update_enable_state(smu, feature_2_enabled, true);
		if (ret)
			goto out;
	}
	if (feature_2_disabled) {
		ret = smu_feature_update_enable_state(smu, feature_2_disabled, false);
		if (ret)
			goto out;
	}

out:
	mutex_unlock(&smu->mutex);

	return ret;
}

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
	int ret = 0;

	if (min < 0 && max < 0)
		return -EINVAL;

	if (!smu_clk_dpm_is_enabled(smu, clk_type))
		return 0;

	ret = smu_set_soft_freq_limited_range(smu, clk_type, min, max);
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
			   uint32_t *min, uint32_t *max, bool lock_needed)
{
	uint32_t clock_limit;
	int ret = 0;

	if (!min && !max)
		return -EINVAL;

	if (lock_needed)
		mutex_lock(&smu->mutex);

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
	} else {
		/*
		 * Todo: Use each asic(ASIC_ppt funcs) control the callbacks exposed to the
		 * core driver and then have helpers for stuff that is common(SMU_v11_x | SMU_v12_x funcs).
		 */
		ret = smu_get_dpm_ultimate_freq(smu, clk_type, min, max);
	}

	if (lock_needed)
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

int smu_get_dpm_level_range(struct smu_context *smu, enum smu_clk_type clk_type,
			    uint32_t *min_value, uint32_t *max_value)
{
	int ret = 0;
	uint32_t level_count = 0;

	if (!min_value && !max_value)
		return -EINVAL;

	if (min_value) {
		/* by default, level 0 clock value as min value */
		ret = smu_get_dpm_freq_by_index(smu, clk_type, 0, min_value);
		if (ret)
			return ret;
	}

	if (max_value) {
		ret = smu_get_dpm_level_count(smu, clk_type, &level_count);
		if (ret)
			return ret;

		ret = smu_get_dpm_freq_by_index(smu, clk_type, level_count - 1, max_value);
		if (ret)
			return ret;
	}

	return ret;
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
		return false;
	}

	return true;
}

/**
 * smu_dpm_set_power_gate - power gate/ungate the specific IP block
 *
 * @smu:        smu_context pointer
 * @block_type: the IP block to power gate/ungate
 * @gate:       to power gate if true, ungate otherwise
 *
 * This API uses no smu->mutex lock protection due to:
 * 1. It is either called by other IP block(gfx/sdma/vcn/uvd/vce).
 *    This is guarded to be race condition free by the caller.
 * 2. Or get called on user setting request of power_dpm_force_performance_level.
 *    Under this case, the smu->mutex lock protection is already enforced on
 *    the parent API smu_force_performance_level of the call path.
 */
int smu_dpm_set_power_gate(struct smu_context *smu, uint32_t block_type,
			   bool gate)
{
	int ret = 0;

	switch (block_type) {
	case AMD_IP_BLOCK_TYPE_UVD:
		ret = smu_dpm_set_uvd_enable(smu, !gate);
		break;
	case AMD_IP_BLOCK_TYPE_VCE:
		ret = smu_dpm_set_vce_enable(smu, !gate);
		break;
	case AMD_IP_BLOCK_TYPE_GFX:
		ret = smu_gfx_off_control(smu, gate);
		break;
	case AMD_IP_BLOCK_TYPE_SDMA:
		ret = smu_powergate_sdma(smu, gate);
		break;
	case AMD_IP_BLOCK_TYPE_JPEG:
		ret = smu_dpm_set_jpeg_enable(smu, !gate);
		break;
	default:
		break;
	}

	return ret;
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

	if(!data || !size)
		return -EINVAL;

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
	struct amdgpu_device *adev = smu->adev;
	struct smu_table *table = &smu_table->driver_table;
	int table_id = smu_table_get_index(smu, table_index);
	uint32_t table_size;
	int ret = 0;

	if (!table_data || table_id >= SMU_TABLE_COUNT || table_id < 0)
		return -EINVAL;

	table_size = smu_table->tables[table_index].size;

	if (drv2smu) {
		memcpy(table->cpu_addr, table_data, table_size);
		/*
		 * Flush hdp cache: to guard the content seen by
		 * GPU is consitent with CPU.
		 */
		amdgpu_asic_flush_hdp(adev, NULL);
	}

	ret = smu_send_smc_msg_with_param(smu, drv2smu ?
					  SMU_MSG_TransferTableDram2Smu :
					  SMU_MSG_TransferTableSmu2Dram,
					  table_id | ((argument & 0xFFFF) << 16));
	if (ret)
		return ret;

	if (!drv2smu) {
		amdgpu_asic_flush_hdp(adev, NULL);
		memcpy(table_data, table->cpu_addr, table_size);
	}

	return ret;
}

bool is_support_sw_smu(struct amdgpu_device *adev)
{
	if (adev->asic_type == CHIP_VEGA20)
		return (amdgpu_dpm == 2) ? true : false;
	else if (adev->asic_type >= CHIP_ARCTURUS) {
		if (amdgpu_sriov_vf(adev)&& !amdgpu_sriov_is_pp_one_vf(adev))
			return false;
		else
			return true;
	} else
		return false;
}

bool is_support_sw_smu_xgmi(struct amdgpu_device *adev)
{
	if (!is_support_sw_smu(adev))
		return false;

	if (adev->asic_type == CHIP_VEGA20)
		return true;

	return false;
}

int smu_sys_get_pp_table(struct smu_context *smu, void **table)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	uint32_t powerplay_table_size;

	if (!smu_table->power_play_table && !smu_table->hardcode_pptable)
		return -EINVAL;

	mutex_lock(&smu->mutex);

	if (smu_table->hardcode_pptable)
		*table = smu_table->hardcode_pptable;
	else
		*table = smu_table->power_play_table;

	powerplay_table_size = smu_table->power_play_table_size;

	mutex_unlock(&smu->mutex);

	return powerplay_table_size;
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

	/*
	 * Special hw_fini action(for Navi1x, the DPMs disablement will be
	 * skipped) may be needed for custom pptable uploading.
	 */
	smu->uploading_custom_pp_table = true;

	ret = smu_reset(smu);
	if (ret)
		pr_info("smu reset failed, ret = %d\n", ret);

	smu->uploading_custom_pp_table = false;

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
	int feature_id;
	int ret = 0;

	if (smu->is_apu)
		return 1;

	feature_id = smu_feature_get_index(smu, mask);
	if (feature_id < 0)
		return 0;

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
	int feature_id;

	feature_id = smu_feature_get_index(smu, mask);
	if (feature_id < 0)
		return -EINVAL;

	WARN_ON(feature_id > feature->feature_num);

	return smu_feature_update_enable_state(smu,
					       1ULL << feature_id,
					       enable);
}

int smu_feature_is_supported(struct smu_context *smu, enum smu_feature_mask mask)
{
	struct smu_feature *feature = &smu->smu_feature;
	int feature_id;
	int ret = 0;

	feature_id = smu_feature_get_index(smu, mask);
	if (feature_id < 0)
		return 0;

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
	int feature_id;
	int ret = 0;

	feature_id = smu_feature_get_index(smu, mask);
	if (feature_id < 0)
		return -EINVAL;

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

	if (adev->pm.pp_feature & PP_OVERDRIVE_MASK)
		smu->od_enabled = true;

	switch (adev->asic_type) {
	case CHIP_VEGA20:
		adev->pm.pp_feature &= ~PP_GFXOFF_MASK;
		vega20_set_ppt_funcs(smu);
		break;
	case CHIP_NAVI10:
	case CHIP_NAVI14:
	case CHIP_NAVI12:
		navi10_set_ppt_funcs(smu);
		break;
	case CHIP_ARCTURUS:
		adev->pm.pp_feature &= ~PP_GFXOFF_MASK;
		arcturus_set_ppt_funcs(smu);
		/* OD is not supported on Arcturus */
		smu->od_enabled =false;
		break;
	case CHIP_RENOIR:
		renoir_set_ppt_funcs(smu);
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
	smu->is_apu = false;
	mutex_init(&smu->mutex);

	return smu_set_funcs(adev);
}

static int smu_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;

	if (!smu->pm_enabled)
		return 0;

	smu_handle_task(&adev->smu,
			smu->smu_dpm.dpm_level,
			AMD_PP_TASK_COMPLETE_INIT,
			false);

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

	mutex_init(&smu->sensor_lock);
	mutex_init(&smu->metrics_lock);

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
	struct smu_table *driver_table = &(smu_table->driver_table);
	uint32_t max_table_size = 0;
	int ret, i;

	/* VRAM allocation for tool table */
	if (tables[SMU_TABLE_PMSTATUSLOG].size) {
		ret = amdgpu_bo_create_kernel(adev,
					      tables[SMU_TABLE_PMSTATUSLOG].size,
					      tables[SMU_TABLE_PMSTATUSLOG].align,
					      tables[SMU_TABLE_PMSTATUSLOG].domain,
					      &tables[SMU_TABLE_PMSTATUSLOG].bo,
					      &tables[SMU_TABLE_PMSTATUSLOG].mc_address,
					      &tables[SMU_TABLE_PMSTATUSLOG].cpu_addr);
		if (ret) {
			pr_err("VRAM allocation for tool table failed!\n");
			return ret;
		}
	}

	/* VRAM allocation for driver table */
	for (i = 0; i < SMU_TABLE_COUNT; i++) {
		if (tables[i].size == 0)
			continue;

		if (i == SMU_TABLE_PMSTATUSLOG)
			continue;

		if (max_table_size < tables[i].size)
			max_table_size = tables[i].size;
	}

	driver_table->size = max_table_size;
	driver_table->align = PAGE_SIZE;
	driver_table->domain = AMDGPU_GEM_DOMAIN_VRAM;

	ret = amdgpu_bo_create_kernel(adev,
				      driver_table->size,
				      driver_table->align,
				      driver_table->domain,
				      &driver_table->bo,
				      &driver_table->mc_address,
				      &driver_table->cpu_addr);
	if (ret) {
		pr_err("VRAM allocation for driver table failed!\n");
		if (tables[SMU_TABLE_PMSTATUSLOG].mc_address)
			amdgpu_bo_free_kernel(&tables[SMU_TABLE_PMSTATUSLOG].bo,
					      &tables[SMU_TABLE_PMSTATUSLOG].mc_address,
					      &tables[SMU_TABLE_PMSTATUSLOG].cpu_addr);
	}

	return ret;
}

static int smu_fini_fb_allocations(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = smu_table->tables;
	struct smu_table *driver_table = &(smu_table->driver_table);

	if (!tables)
		return 0;

	if (tables[SMU_TABLE_PMSTATUSLOG].mc_address)
		amdgpu_bo_free_kernel(&tables[SMU_TABLE_PMSTATUSLOG].bo,
				      &tables[SMU_TABLE_PMSTATUSLOG].mc_address,
				      &tables[SMU_TABLE_PMSTATUSLOG].cpu_addr);

	amdgpu_bo_free_kernel(&driver_table->bo,
			      &driver_table->mc_address,
			      &driver_table->cpu_addr);

	return 0;
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

	if (adev->asic_type != CHIP_ARCTURUS) {
		ret = smu_init_display_count(smu, 0);
		if (ret)
			return ret;
	}

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

	/* smu_dump_pptable(smu); */
	if (!amdgpu_sriov_vf(adev)) {
		ret = smu_set_driver_table_location(smu);
		if (ret)
			return ret;

		/*
		 * Copy pptable bo in the vram to smc with SMU MSGs such as
		 * SetDriverDramAddr and TransferTableDram2Smu.
		 */
		ret = smu_write_pptable(smu);
		if (ret)
			return ret;

		/* issue Run*Btc msg */
		ret = smu_run_btc(smu);
		if (ret)
			return ret;
		ret = smu_feature_set_allowed_mask(smu);
		if (ret)
			return ret;

		ret = smu_system_features_control(smu, true);
		if (ret)
			return ret;

		if (adev->asic_type == CHIP_NAVI10) {
			if ((adev->pdev->device == 0x731f && (adev->pdev->revision == 0xc2 ||
							      adev->pdev->revision == 0xc3 ||
							      adev->pdev->revision == 0xca ||
							      adev->pdev->revision == 0xcb)) ||
			    (adev->pdev->device == 0x66af && (adev->pdev->revision == 0xf3 ||
							      adev->pdev->revision == 0xf4 ||
							      adev->pdev->revision == 0xf5 ||
							      adev->pdev->revision == 0xf6))) {
				ret = smu_disable_umc_cdr_12gbps_workaround(smu);
				if (ret) {
					pr_err("Workaround failed to disable UMC CDR feature on 12Gbps SKU!\n");
					return ret;
				}
			}
		}
	}
	if (adev->asic_type != CHIP_ARCTURUS) {
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
	}

	/*
	 * Set initialized values (get from vbios) to dpm tables context such as
	 * gfxclk, memclk, dcefclk, and etc. And enable the DPM feature for each
	 * type of clks.
	 */
	if (initialize) {
		ret = smu_populate_smc_tables(smu);
		if (ret)
			return ret;

		ret = smu_init_max_sustainable_clocks(smu);
		if (ret)
			return ret;
	}

	if (adev->asic_type != CHIP_ARCTURUS) {
		ret = smu_override_pcie_parameters(smu);
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

		ret = smu_get_power_limit(smu, &smu->default_power_limit, false, false);
		if (ret)
			return ret;
	}

	/*
	 * Set PMSTATUSLOG table bo address with SetToolsDramAddr MSG for tools.
	 */
	if (!amdgpu_sriov_vf(adev)) {
		ret = smu_set_tool_table_location(smu);
	}
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

	if (memory_pool->size == SMU_MEMORY_POOL_SIZE_ZERO)
		return 0;

	amdgpu_bo_free_kernel(&memory_pool->bo,
			      &memory_pool->mc_address,
			      &memory_pool->cpu_addr);

	memset(memory_pool, 0, sizeof(struct smu_table));

	return 0;
}

static int smu_start_smc_engine(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;

	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
		if (adev->asic_type < CHIP_NAVI10) {
			if (smu->ppt_funcs->load_microcode) {
				ret = smu->ppt_funcs->load_microcode(smu);
				if (ret)
					return ret;
			}
		}
	}

	if (smu->ppt_funcs->check_fw_status) {
		ret = smu->ppt_funcs->check_fw_status(smu);
		if (ret)
			pr_err("SMC is not ready\n");
	}

	return ret;
}

static int smu_hw_init(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;

	ret = smu_start_smc_engine(smu);
	if (ret) {
		pr_err("SMU is not ready yet!\n");
		return ret;
	}

	if (smu->is_apu) {
		smu_powergate_sdma(&adev->smu, false);
		smu_powergate_vcn(&adev->smu, false);
		smu_powergate_jpeg(&adev->smu, false);
		smu_set_gfx_cgpg(&adev->smu, true);
	}

	if (amdgpu_sriov_vf(adev) && !amdgpu_sriov_is_pp_one_vf(adev))
		return 0;

	if (!smu->pm_enabled)
		return 0;

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

static int smu_stop_dpms(struct smu_context *smu)
{
	return smu_system_features_control(smu, false);
}

static int smu_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;
	struct smu_table_context *table_context = &smu->smu_table;
	int ret = 0;

	if (amdgpu_sriov_vf(adev)&& !amdgpu_sriov_is_pp_one_vf(adev))
		return 0;

	if (smu->is_apu) {
		smu_powergate_sdma(&adev->smu, true);
		smu_powergate_vcn(&adev->smu, true);
		smu_powergate_jpeg(&adev->smu, true);
	}

	if (!smu->pm_enabled)
		return 0;

	if (!amdgpu_sriov_vf(adev)){
		ret = smu_stop_thermal_control(smu);
		if (ret) {
			pr_warn("Fail to stop thermal control!\n");
			return ret;
		}

		/*
		 * For custom pptable uploading, skip the DPM features
		 * disable process on Navi1x ASICs.
		 *   - As the gfx related features are under control of
		 *     RLC on those ASICs. RLC reinitialization will be
		 *     needed to reenable them. That will cost much more
		 *     efforts.
		 *
		 *   - SMU firmware can handle the DPM reenablement
		 *     properly.
		 */
		if (!smu->uploading_custom_pp_table ||
				!((adev->asic_type >= CHIP_NAVI10) &&
					(adev->asic_type <= CHIP_NAVI12))) {
			ret = smu_stop_dpms(smu);
			if (ret) {
				pr_warn("Fail to stop Dpms!\n");
				return ret;
			}
		}
	}

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
	bool baco_feature_is_enabled = false;

	if (!smu->pm_enabled)
		return 0;

	if(!smu->is_apu)
		baco_feature_is_enabled = smu_feature_is_enabled(smu, SMU_FEATURE_BACO_BIT);

	ret = smu_system_features_control(smu, false);
	if (ret)
		return ret;

	if (baco_feature_is_enabled) {
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
	if (smu->is_apu)
		smu_set_gfx_cgpg(&adev->smu, false);

	return 0;
}

static int smu_resume(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;

	if (amdgpu_sriov_vf(adev)&& !amdgpu_sriov_is_pp_one_vf(adev))
		return 0;

	if (!smu->pm_enabled)
		return 0;

	pr_info("SMU is resuming...\n");

	ret = smu_start_smc_engine(smu);
	if (ret) {
		pr_err("SMU is not ready yet!\n");
		goto failed;
	}

	ret = smu_smc_table_hw_init(smu, false);
	if (ret)
		goto failed;

	ret = smu_start_thermal_control(smu);
	if (ret)
		goto failed;

	if (smu->is_apu)
		smu_set_gfx_cgpg(&adev->smu, true);

	smu->disable_uclk_switch = 0;

	pr_info("SMU is resumed successfully!\n");

	return 0;

failed:
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

	if (smu->ppt_funcs->set_deep_sleep_dcefclk)
		smu->ppt_funcs->set_deep_sleep_dcefclk(smu,
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

	if (!smu->is_apu && (!smu->pm_enabled || !smu_dpm_ctx->dpm_context))
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

	ret = smu_apply_clocks_adjust_rules(smu);
	if (ret) {
		pr_err("Failed to apply clocks adjust rules!");
		return ret;
	}

	if (!skip_display_settings) {
		ret = smu_notify_smc_display_config(smu);
		if (ret) {
			pr_err("Failed to notify smc display config!");
			return ret;
		}
	}

	if (smu_dpm_ctx->dpm_level != level) {
		ret = smu_asic_set_performance_level(smu, level);
		if (ret) {
			pr_err("Failed to set performance level!");
			return ret;
		}

		/* update the saved copy */
		smu_dpm_ctx->dpm_level = level;
	}

	if (smu_dpm_ctx->dpm_level != AMD_DPM_FORCED_LEVEL_MANUAL) {
		index = fls(smu->workload_mask);
		index = index > 0 && index <= WORKLOAD_POLICY_MAX ? index - 1 : 0;
		workload = smu->workload_setting[index];

		if (smu->power_profile_mode != workload)
			smu_set_power_profile_mode(smu, &workload, 0, false);
	}

	return ret;
}

int smu_handle_task(struct smu_context *smu,
		    enum amd_dpm_forced_level level,
		    enum amd_pp_task task_id,
		    bool lock_needed)
{
	int ret = 0;

	if (lock_needed)
		mutex_lock(&smu->mutex);

	switch (task_id) {
	case AMD_PP_TASK_DISPLAY_CONFIG_CHANGE:
		ret = smu_pre_display_config_changed(smu);
		if (ret)
			goto out;
		ret = smu_set_cpu_power_state(smu);
		if (ret)
			goto out;
		ret = smu_adjust_power_state_dynamic(smu, level, false);
		break;
	case AMD_PP_TASK_COMPLETE_INIT:
	case AMD_PP_TASK_READJUST_POWER_STATE:
		ret = smu_adjust_power_state_dynamic(smu, level, true);
		break;
	default:
		break;
	}

out:
	if (lock_needed)
		mutex_unlock(&smu->mutex);

	return ret;
}

int smu_switch_power_profile(struct smu_context *smu,
			     enum PP_SMC_POWER_PROFILE type,
			     bool en)
{
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);
	long workload;
	uint32_t index;

	if (!smu->pm_enabled)
		return -EINVAL;

	if (!(type < PP_SMC_POWER_PROFILE_CUSTOM))
		return -EINVAL;

	mutex_lock(&smu->mutex);

	if (!en) {
		smu->workload_mask &= ~(1 << smu->workload_prority[type]);
		index = fls(smu->workload_mask);
		index = index > 0 && index <= WORKLOAD_POLICY_MAX ? index - 1 : 0;
		workload = smu->workload_setting[index];
	} else {
		smu->workload_mask |= (1 << smu->workload_prority[type]);
		index = fls(smu->workload_mask);
		index = index <= WORKLOAD_POLICY_MAX ? index - 1 : 0;
		workload = smu->workload_setting[index];
	}

	if (smu_dpm_ctx->dpm_level != AMD_DPM_FORCED_LEVEL_MANUAL)
		smu_set_power_profile_mode(smu, &workload, 0, false);

	mutex_unlock(&smu->mutex);

	return 0;
}

enum amd_dpm_forced_level smu_get_performance_level(struct smu_context *smu)
{
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);
	enum amd_dpm_forced_level level;

	if (!smu->is_apu && !smu_dpm_ctx->dpm_context)
		return -EINVAL;

	mutex_lock(&(smu->mutex));
	level = smu_dpm_ctx->dpm_level;
	mutex_unlock(&(smu->mutex));

	return level;
}

int smu_force_performance_level(struct smu_context *smu, enum amd_dpm_forced_level level)
{
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);
	int ret = 0;

	if (!smu->is_apu && !smu_dpm_ctx->dpm_context)
		return -EINVAL;

	mutex_lock(&smu->mutex);

	ret = smu_enable_umd_pstate(smu, &level);
	if (ret) {
		mutex_unlock(&smu->mutex);
		return ret;
	}

	ret = smu_handle_task(smu, level,
			      AMD_PP_TASK_READJUST_POWER_STATE,
			      false);

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

int smu_force_clk_levels(struct smu_context *smu,
			 enum smu_clk_type clk_type,
			 uint32_t mask,
			 bool lock_needed)
{
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);
	int ret = 0;

	if (smu_dpm_ctx->dpm_level != AMD_DPM_FORCED_LEVEL_MANUAL) {
		pr_debug("force clock level is for dpm manual mode only.\n");
		return -EINVAL;
	}

	if (lock_needed)
		mutex_lock(&smu->mutex);

	if (smu->ppt_funcs && smu->ppt_funcs->force_clk_levels)
		ret = smu->ppt_funcs->force_clk_levels(smu, clk_type, mask);

	if (lock_needed)
		mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_mp1_state(struct smu_context *smu,
		      enum pp_mp1_state mp1_state)
{
	uint16_t msg;
	int ret;

	/*
	 * The SMC is not fully ready. That may be
	 * expected as the IP may be masked.
	 * So, just return without error.
	 */
	if (!smu->pm_enabled)
		return 0;

	mutex_lock(&smu->mutex);

	switch (mp1_state) {
	case PP_MP1_STATE_SHUTDOWN:
		msg = SMU_MSG_PrepareMp1ForShutdown;
		break;
	case PP_MP1_STATE_UNLOAD:
		msg = SMU_MSG_PrepareMp1ForUnload;
		break;
	case PP_MP1_STATE_RESET:
		msg = SMU_MSG_PrepareMp1ForReset;
		break;
	case PP_MP1_STATE_NONE:
	default:
		mutex_unlock(&smu->mutex);
		return 0;
	}

	/* some asics may not support those messages */
	if (smu_msg_get_index(smu, msg) < 0) {
		mutex_unlock(&smu->mutex);
		return 0;
	}

	ret = smu_send_smc_msg(smu, msg);
	if (ret)
		pr_err("[PrepareMp1] Failed!\n");

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_df_cstate(struct smu_context *smu,
		      enum pp_df_cstate state)
{
	int ret = 0;

	/*
	 * The SMC is not fully ready. That may be
	 * expected as the IP may be masked.
	 * So, just return without error.
	 */
	if (!smu->pm_enabled)
		return 0;

	if (!smu->ppt_funcs || !smu->ppt_funcs->set_df_cstate)
		return 0;

	mutex_lock(&smu->mutex);

	ret = smu->ppt_funcs->set_df_cstate(smu, state);
	if (ret)
		pr_err("[SetDfCstate] failed!\n");

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_write_watermarks_table(struct smu_context *smu)
{
	void *watermarks_table = smu->smu_table.watermarks_table;

	if (!watermarks_table)
		return -EINVAL;

	return smu_update_table(smu,
				SMU_TABLE_WATERMARKS,
				0,
				watermarks_table,
				true);
}

int smu_set_watermarks_for_clock_ranges(struct smu_context *smu,
		struct dm_pp_wm_sets_with_clock_ranges_soc15 *clock_ranges)
{
	void *table = smu->smu_table.watermarks_table;

	if (!table)
		return -EINVAL;

	mutex_lock(&smu->mutex);

	if (!smu->disable_watermark &&
			smu_feature_is_enabled(smu, SMU_FEATURE_DPM_DCEFCLK_BIT) &&
			smu_feature_is_enabled(smu, SMU_FEATURE_DPM_SOCCLK_BIT)) {
		smu_set_watermarks_table(smu, table, clock_ranges);

		if (!(smu->watermarks_bitmap & WATERMARKS_EXIST)) {
			smu->watermarks_bitmap |= WATERMARKS_EXIST;
			smu->watermarks_bitmap &= ~WATERMARKS_LOADED;
		}
	}

	mutex_unlock(&smu->mutex);

	return 0;
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

const struct amdgpu_ip_block_version smu_v12_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_SMC,
	.major = 12,
	.minor = 0,
	.rev = 0,
	.funcs = &smu_ip_funcs,
};

int smu_load_microcode(struct smu_context *smu)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->load_microcode)
		ret = smu->ppt_funcs->load_microcode(smu);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_check_fw_status(struct smu_context *smu)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->check_fw_status)
		ret = smu->ppt_funcs->check_fw_status(smu);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_gfx_cgpg(struct smu_context *smu, bool enabled)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->set_gfx_cgpg)
		ret = smu->ppt_funcs->set_gfx_cgpg(smu, enabled);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_fan_speed_rpm(struct smu_context *smu, uint32_t speed)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->set_fan_speed_rpm)
		ret = smu->ppt_funcs->set_fan_speed_rpm(smu, speed);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_get_power_limit(struct smu_context *smu,
			uint32_t *limit,
			bool def,
			bool lock_needed)
{
	int ret = 0;

	if (lock_needed)
		mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_power_limit)
		ret = smu->ppt_funcs->get_power_limit(smu, limit, def);

	if (lock_needed)
		mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_power_limit(struct smu_context *smu, uint32_t limit)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->set_power_limit)
		ret = smu->ppt_funcs->set_power_limit(smu, limit);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_print_clk_levels(struct smu_context *smu, enum smu_clk_type clk_type, char *buf)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->print_clk_levels)
		ret = smu->ppt_funcs->print_clk_levels(smu, clk_type, buf);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_get_od_percentage(struct smu_context *smu, enum smu_clk_type type)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_od_percentage)
		ret = smu->ppt_funcs->get_od_percentage(smu, type);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_od_percentage(struct smu_context *smu, enum smu_clk_type type, uint32_t value)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->set_od_percentage)
		ret = smu->ppt_funcs->set_od_percentage(smu, type, value);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_od_edit_dpm_table(struct smu_context *smu,
			  enum PP_OD_DPM_TABLE_COMMAND type,
			  long *input, uint32_t size)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->od_edit_dpm_table)
		ret = smu->ppt_funcs->od_edit_dpm_table(smu, type, input, size);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_read_sensor(struct smu_context *smu,
		    enum amd_pp_sensors sensor,
		    void *data, uint32_t *size)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->read_sensor)
		ret = smu->ppt_funcs->read_sensor(smu, sensor, data, size);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_get_power_profile_mode(struct smu_context *smu, char *buf)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_power_profile_mode)
		ret = smu->ppt_funcs->get_power_profile_mode(smu, buf);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_power_profile_mode(struct smu_context *smu,
			       long *param,
			       uint32_t param_size,
			       bool lock_needed)
{
	int ret = 0;

	if (lock_needed)
		mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->set_power_profile_mode)
		ret = smu->ppt_funcs->set_power_profile_mode(smu, param, param_size);

	if (lock_needed)
		mutex_unlock(&smu->mutex);

	return ret;
}


int smu_get_fan_control_mode(struct smu_context *smu)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_fan_control_mode)
		ret = smu->ppt_funcs->get_fan_control_mode(smu);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_fan_control_mode(struct smu_context *smu, int value)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->set_fan_control_mode)
		ret = smu->ppt_funcs->set_fan_control_mode(smu, value);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_get_fan_speed_percent(struct smu_context *smu, uint32_t *speed)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_fan_speed_percent)
		ret = smu->ppt_funcs->get_fan_speed_percent(smu, speed);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_fan_speed_percent(struct smu_context *smu, uint32_t speed)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->set_fan_speed_percent)
		ret = smu->ppt_funcs->set_fan_speed_percent(smu, speed);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_get_fan_speed_rpm(struct smu_context *smu, uint32_t *speed)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_fan_speed_rpm)
		ret = smu->ppt_funcs->get_fan_speed_rpm(smu, speed);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_deep_sleep_dcefclk(struct smu_context *smu, int clk)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->set_deep_sleep_dcefclk)
		ret = smu->ppt_funcs->set_deep_sleep_dcefclk(smu, clk);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_active_display_count(struct smu_context *smu, uint32_t count)
{
	int ret = 0;

	if (smu->ppt_funcs->set_active_display_count)
		ret = smu->ppt_funcs->set_active_display_count(smu, count);

	return ret;
}

int smu_get_clock_by_type(struct smu_context *smu,
			  enum amd_pp_clock_type type,
			  struct amd_pp_clocks *clocks)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_clock_by_type)
		ret = smu->ppt_funcs->get_clock_by_type(smu, type, clocks);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_get_max_high_clocks(struct smu_context *smu,
			    struct amd_pp_simple_clock_info *clocks)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_max_high_clocks)
		ret = smu->ppt_funcs->get_max_high_clocks(smu, clocks);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_get_clock_by_type_with_latency(struct smu_context *smu,
				       enum smu_clk_type clk_type,
				       struct pp_clock_levels_with_latency *clocks)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_clock_by_type_with_latency)
		ret = smu->ppt_funcs->get_clock_by_type_with_latency(smu, clk_type, clocks);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_get_clock_by_type_with_voltage(struct smu_context *smu,
				       enum amd_pp_clock_type type,
				       struct pp_clock_levels_with_voltage *clocks)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_clock_by_type_with_voltage)
		ret = smu->ppt_funcs->get_clock_by_type_with_voltage(smu, type, clocks);

	mutex_unlock(&smu->mutex);

	return ret;
}


int smu_display_clock_voltage_request(struct smu_context *smu,
				      struct pp_display_clock_request *clock_req)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->display_clock_voltage_request)
		ret = smu->ppt_funcs->display_clock_voltage_request(smu, clock_req);

	mutex_unlock(&smu->mutex);

	return ret;
}


int smu_display_disable_memory_clock_switch(struct smu_context *smu, bool disable_memory_clock_switch)
{
	int ret = -EINVAL;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->display_disable_memory_clock_switch)
		ret = smu->ppt_funcs->display_disable_memory_clock_switch(smu, disable_memory_clock_switch);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_notify_smu_enable_pwe(struct smu_context *smu)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->notify_smu_enable_pwe)
		ret = smu->ppt_funcs->notify_smu_enable_pwe(smu);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_xgmi_pstate(struct smu_context *smu,
			uint32_t pstate)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->set_xgmi_pstate)
		ret = smu->ppt_funcs->set_xgmi_pstate(smu, pstate);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_set_azalia_d3_pme(struct smu_context *smu)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->set_azalia_d3_pme)
		ret = smu->ppt_funcs->set_azalia_d3_pme(smu);

	mutex_unlock(&smu->mutex);

	return ret;
}

bool smu_baco_is_support(struct smu_context *smu)
{
	bool ret = false;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs && smu->ppt_funcs->baco_is_support)
		ret = smu->ppt_funcs->baco_is_support(smu);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_baco_get_state(struct smu_context *smu, enum smu_baco_state *state)
{
	if (smu->ppt_funcs->baco_get_state)
		return -EINVAL;

	mutex_lock(&smu->mutex);
	*state = smu->ppt_funcs->baco_get_state(smu);
	mutex_unlock(&smu->mutex);

	return 0;
}

int smu_baco_enter(struct smu_context *smu)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->baco_enter)
		ret = smu->ppt_funcs->baco_enter(smu);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_baco_exit(struct smu_context *smu)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->baco_exit)
		ret = smu->ppt_funcs->baco_exit(smu);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_mode2_reset(struct smu_context *smu)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->mode2_reset)
		ret = smu->ppt_funcs->mode2_reset(smu);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_get_max_sustainable_clocks_by_dc(struct smu_context *smu,
					 struct pp_smu_nv_clock_table *max_clocks)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_max_sustainable_clocks_by_dc)
		ret = smu->ppt_funcs->get_max_sustainable_clocks_by_dc(smu, max_clocks);

	mutex_unlock(&smu->mutex);

	return ret;
}

int smu_get_uclk_dpm_states(struct smu_context *smu,
			    unsigned int *clock_values_in_khz,
			    unsigned int *num_states)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_uclk_dpm_states)
		ret = smu->ppt_funcs->get_uclk_dpm_states(smu, clock_values_in_khz, num_states);

	mutex_unlock(&smu->mutex);

	return ret;
}

enum amd_pm_state_type smu_get_current_power_state(struct smu_context *smu)
{
	enum amd_pm_state_type pm_state = POWER_STATE_TYPE_DEFAULT;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_current_power_state)
		pm_state = smu->ppt_funcs->get_current_power_state(smu);

	mutex_unlock(&smu->mutex);

	return pm_state;
}

int smu_get_dpm_clock_table(struct smu_context *smu,
			    struct dpm_clocks *clock_table)
{
	int ret = 0;

	mutex_lock(&smu->mutex);

	if (smu->ppt_funcs->get_dpm_clock_table)
		ret = smu->ppt_funcs->get_dpm_clock_table(smu, clock_table);

	mutex_unlock(&smu->mutex);

	return ret;
}

uint32_t smu_get_pptable_power_limit(struct smu_context *smu)
{
	uint32_t ret = 0;

	if (smu->ppt_funcs->get_pptable_power_limit)
		ret = smu->ppt_funcs->get_pptable_power_limit(smu);

	return ret;
}

int smu_send_smc_msg(struct smu_context *smu,
		     enum smu_message_type msg)
{
	int ret;

	ret = smu_send_smc_msg_with_param(smu, msg, 0);
	return ret;
}
