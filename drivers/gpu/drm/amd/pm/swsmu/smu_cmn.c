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
 */

#define SWSMU_CODE_LAYER_L4

#include "amdgpu.h"
#include "amdgpu_smu.h"
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

/*
 * Although these are defined in each ASIC's specific header file.
 * They share the same definitions and values. That makes common
 * APIs for SMC messages issuing for all ASICs possible.
 */
#define mmMP1_SMN_C2PMSG_66                                                                            0x0282
#define mmMP1_SMN_C2PMSG_66_BASE_IDX                                                                   0

#define mmMP1_SMN_C2PMSG_82                                                                            0x0292
#define mmMP1_SMN_C2PMSG_82_BASE_IDX                                                                   0

#define mmMP1_SMN_C2PMSG_90                                                                            0x029a
#define mmMP1_SMN_C2PMSG_90_BASE_IDX                                                                   0

#define MP1_C2PMSG_90__CONTENT_MASK                                                                    0xFFFFFFFFL

#undef __SMU_DUMMY_MAP
#define __SMU_DUMMY_MAP(type)	#type
static const char* __smu_message_names[] = {
	SMU_MESSAGE_TYPES
};

static const char *smu_get_message_name(struct smu_context *smu,
					enum smu_message_type type)
{
	if (type < 0 || type >= SMU_MSG_MAX_COUNT)
		return "unknown smu message";

	return __smu_message_names[type];
}

static void smu_cmn_read_arg(struct smu_context *smu,
			     uint32_t *arg)
{
	struct amdgpu_device *adev = smu->adev;

	*arg = RREG32_SOC15_NO_KIQ(MP1, 0, mmMP1_SMN_C2PMSG_82);
}

static int smu_cmn_wait_for_response(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t cur_value, i, timeout = adev->usec_timeout * 10;

	for (i = 0; i < timeout; i++) {
		cur_value = RREG32_SOC15_NO_KIQ(MP1, 0, mmMP1_SMN_C2PMSG_90);
		if ((cur_value & MP1_C2PMSG_90__CONTENT_MASK) != 0)
			return cur_value;

		udelay(1);
	}

	/* timeout means wrong logic */
	if (i == timeout)
		return -ETIME;

	return RREG32_SOC15_NO_KIQ(MP1, 0, mmMP1_SMN_C2PMSG_90);
}

int smu_cmn_send_msg_without_waiting(struct smu_context *smu,
				     uint16_t msg, uint32_t param)
{
	struct amdgpu_device *adev = smu->adev;
	int ret;

	ret = smu_cmn_wait_for_response(smu);
	if (ret != 0x1) {
		dev_err(adev->dev, "Msg issuing pre-check failed and "
		       "SMU may be not in the right state!\n");
		if (ret != -ETIME)
			ret = -EIO;
		return ret;
	}

	WREG32_SOC15_NO_KIQ(MP1, 0, mmMP1_SMN_C2PMSG_90, 0);
	WREG32_SOC15_NO_KIQ(MP1, 0, mmMP1_SMN_C2PMSG_82, param);
	WREG32_SOC15_NO_KIQ(MP1, 0, mmMP1_SMN_C2PMSG_66, msg);

	return 0;
}

int smu_cmn_send_smc_msg_with_param(struct smu_context *smu,
				    enum smu_message_type msg,
				    uint32_t param,
				    uint32_t *read_arg)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0, index = 0;

	if (smu->adev->in_pci_err_recovery)
		return 0;

	index = smu_cmn_to_asic_specific_index(smu,
					       CMN2ASIC_MAPPING_MSG,
					       msg);
	if (index < 0)
		return index == -EACCES ? 0 : index;

	mutex_lock(&smu->message_lock);
	ret = smu_cmn_send_msg_without_waiting(smu, (uint16_t)index, param);
	if (ret)
		goto out;

	ret = smu_cmn_wait_for_response(smu);
	if (ret != 0x1) {
		if (ret == -ETIME) {
			dev_err(adev->dev, "message: %15s (%d) \tparam: 0x%08x is timeout (no response)\n",
				smu_get_message_name(smu, msg), index, param);
		} else {
			dev_err(adev->dev, "failed send message: %15s (%d) \tparam: 0x%08x response %#x\n",
				smu_get_message_name(smu, msg), index, param,
				ret);
			ret = -EIO;
		}
		goto out;
	}

	if (read_arg)
		smu_cmn_read_arg(smu, read_arg);

	ret = 0; /* 0 as driver return value */
out:
	mutex_unlock(&smu->message_lock);
	return ret;
}

int smu_cmn_send_smc_msg(struct smu_context *smu,
			 enum smu_message_type msg,
			 uint32_t *read_arg)
{
	return smu_cmn_send_smc_msg_with_param(smu,
					       msg,
					       0,
					       read_arg);
}

int smu_cmn_to_asic_specific_index(struct smu_context *smu,
				   enum smu_cmn2asic_mapping_type type,
				   uint32_t index)
{
	struct cmn2asic_msg_mapping msg_mapping;
	struct cmn2asic_mapping mapping;

	switch (type) {
	case CMN2ASIC_MAPPING_MSG:
		if (index >= SMU_MSG_MAX_COUNT ||
		    !smu->message_map)
			return -EINVAL;

		msg_mapping = smu->message_map[index];
		if (!msg_mapping.valid_mapping)
			return -EINVAL;

		if (amdgpu_sriov_vf(smu->adev) &&
		    !msg_mapping.valid_in_vf)
			return -EACCES;

		return msg_mapping.map_to;

	case CMN2ASIC_MAPPING_CLK:
		if (index >= SMU_CLK_COUNT ||
		    !smu->clock_map)
			return -EINVAL;

		mapping = smu->clock_map[index];
		if (!mapping.valid_mapping)
			return -EINVAL;

		return mapping.map_to;

	case CMN2ASIC_MAPPING_FEATURE:
		if (index >= SMU_FEATURE_COUNT ||
		    !smu->feature_map)
			return -EINVAL;

		mapping = smu->feature_map[index];
		if (!mapping.valid_mapping)
			return -EINVAL;

		return mapping.map_to;

	case CMN2ASIC_MAPPING_TABLE:
		if (index >= SMU_TABLE_COUNT ||
		    !smu->table_map)
			return -EINVAL;

		mapping = smu->table_map[index];
		if (!mapping.valid_mapping)
			return -EINVAL;

		return mapping.map_to;

	case CMN2ASIC_MAPPING_PWR:
		if (index >= SMU_POWER_SOURCE_COUNT ||
		    !smu->pwr_src_map)
			return -EINVAL;

		mapping = smu->pwr_src_map[index];
		if (!mapping.valid_mapping)
			return -EINVAL;

		return mapping.map_to;

	case CMN2ASIC_MAPPING_WORKLOAD:
		if (index > PP_SMC_POWER_PROFILE_CUSTOM ||
		    !smu->workload_map)
			return -EINVAL;

		mapping = smu->workload_map[index];
		if (!mapping.valid_mapping)
			return -EINVAL;

		return mapping.map_to;

	default:
		return -EINVAL;
	}
}

int smu_cmn_feature_is_supported(struct smu_context *smu,
				 enum smu_feature_mask mask)
{
	struct smu_feature *feature = &smu->smu_feature;
	int feature_id;
	int ret = 0;

	feature_id = smu_cmn_to_asic_specific_index(smu,
						    CMN2ASIC_MAPPING_FEATURE,
						    mask);
	if (feature_id < 0)
		return 0;

	WARN_ON(feature_id > feature->feature_num);

	mutex_lock(&feature->mutex);
	ret = test_bit(feature_id, feature->supported);
	mutex_unlock(&feature->mutex);

	return ret;
}

int smu_cmn_feature_is_enabled(struct smu_context *smu,
			       enum smu_feature_mask mask)
{
	struct smu_feature *feature = &smu->smu_feature;
	struct amdgpu_device *adev = smu->adev;
	int feature_id;
	int ret = 0;

	if (smu->is_apu && adev->family < AMDGPU_FAMILY_VGH)
		return 1;

	feature_id = smu_cmn_to_asic_specific_index(smu,
						    CMN2ASIC_MAPPING_FEATURE,
						    mask);
	if (feature_id < 0)
		return 0;

	WARN_ON(feature_id > feature->feature_num);

	mutex_lock(&feature->mutex);
	ret = test_bit(feature_id, feature->enabled);
	mutex_unlock(&feature->mutex);

	return ret;
}

bool smu_cmn_clk_dpm_is_enabled(struct smu_context *smu,
				enum smu_clk_type clk_type)
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

	if (!smu_cmn_feature_is_enabled(smu, feature_id))
		return false;

	return true;
}

int smu_cmn_get_enabled_mask(struct smu_context *smu,
			     uint32_t *feature_mask,
			     uint32_t num)
{
	uint32_t feature_mask_high = 0, feature_mask_low = 0;
	struct smu_feature *feature = &smu->smu_feature;
	int ret = 0;

	if (!feature_mask || num < 2)
		return -EINVAL;

	if (bitmap_empty(feature->enabled, feature->feature_num)) {
		ret = smu_cmn_send_smc_msg(smu, SMU_MSG_GetEnabledSmuFeaturesHigh, &feature_mask_high);
		if (ret)
			return ret;

		ret = smu_cmn_send_smc_msg(smu, SMU_MSG_GetEnabledSmuFeaturesLow, &feature_mask_low);
		if (ret)
			return ret;

		feature_mask[0] = feature_mask_low;
		feature_mask[1] = feature_mask_high;
	} else {
		bitmap_copy((unsigned long *)feature_mask, feature->enabled,
			     feature->feature_num);
	}

	return ret;
}

int smu_cmn_get_enabled_32_bits_mask(struct smu_context *smu,
					uint32_t *feature_mask,
					uint32_t num)
{
	uint32_t feature_mask_en_low = 0;
	uint32_t feature_mask_en_high = 0;
	struct smu_feature *feature = &smu->smu_feature;
	int ret = 0;

	if (!feature_mask || num < 2)
		return -EINVAL;

	if (bitmap_empty(feature->enabled, feature->feature_num)) {
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_GetEnabledSmuFeatures, 0,
										 &feature_mask_en_low);

		if (ret)
			return ret;

		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_GetEnabledSmuFeatures, 1,
										 &feature_mask_en_high);

		if (ret)
			return ret;

		feature_mask[0] = feature_mask_en_low;
		feature_mask[1] = feature_mask_en_high;

	} else {
		bitmap_copy((unsigned long *)feature_mask, feature->enabled,
				 feature->feature_num);
	}

	return ret;

}

int smu_cmn_feature_update_enable_state(struct smu_context *smu,
					uint64_t feature_mask,
					bool enabled)
{
	struct smu_feature *feature = &smu->smu_feature;
	int ret = 0;

	if (enabled) {
		ret = smu_cmn_send_smc_msg_with_param(smu,
						  SMU_MSG_EnableSmuFeaturesLow,
						  lower_32_bits(feature_mask),
						  NULL);
		if (ret)
			return ret;
		ret = smu_cmn_send_smc_msg_with_param(smu,
						  SMU_MSG_EnableSmuFeaturesHigh,
						  upper_32_bits(feature_mask),
						  NULL);
		if (ret)
			return ret;
	} else {
		ret = smu_cmn_send_smc_msg_with_param(smu,
						  SMU_MSG_DisableSmuFeaturesLow,
						  lower_32_bits(feature_mask),
						  NULL);
		if (ret)
			return ret;
		ret = smu_cmn_send_smc_msg_with_param(smu,
						  SMU_MSG_DisableSmuFeaturesHigh,
						  upper_32_bits(feature_mask),
						  NULL);
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

int smu_cmn_feature_set_enabled(struct smu_context *smu,
				enum smu_feature_mask mask,
				bool enable)
{
	struct smu_feature *feature = &smu->smu_feature;
	int feature_id;

	feature_id = smu_cmn_to_asic_specific_index(smu,
						    CMN2ASIC_MAPPING_FEATURE,
						    mask);
	if (feature_id < 0)
		return -EINVAL;

	WARN_ON(feature_id > feature->feature_num);

	return smu_cmn_feature_update_enable_state(smu,
					       1ULL << feature_id,
					       enable);
}

#undef __SMU_DUMMY_MAP
#define __SMU_DUMMY_MAP(fea)	#fea
static const char* __smu_feature_names[] = {
	SMU_FEATURE_MASKS
};

static const char *smu_get_feature_name(struct smu_context *smu,
					enum smu_feature_mask feature)
{
	if (feature < 0 || feature >= SMU_FEATURE_COUNT)
		return "unknown smu feature";
	return __smu_feature_names[feature];
}

size_t smu_cmn_get_pp_feature_mask(struct smu_context *smu,
				   char *buf)
{
	uint32_t feature_mask[2] = { 0 };
	int feature_index = 0;
	uint32_t count = 0;
	int8_t sort_feature[SMU_FEATURE_COUNT];
	size_t size = 0;
	int ret = 0, i;

	if (!smu->is_apu) {
		ret = smu_cmn_get_enabled_mask(smu,
						feature_mask,
						2);
		if (ret)
			return 0;
	} else {
		ret = smu_cmn_get_enabled_32_bits_mask(smu,
					feature_mask,
					2);
		if (ret)
			return 0;
	}

	size =  sprintf(buf + size, "features high: 0x%08x low: 0x%08x\n",
			feature_mask[1], feature_mask[0]);

	memset(sort_feature, -1, sizeof(sort_feature));

	for (i = 0; i < SMU_FEATURE_COUNT; i++) {
		feature_index = smu_cmn_to_asic_specific_index(smu,
							       CMN2ASIC_MAPPING_FEATURE,
							       i);
		if (feature_index < 0)
			continue;

		sort_feature[feature_index] = i;
	}

	size += sprintf(buf + size, "%-2s. %-20s  %-3s : %-s\n",
			"No", "Feature", "Bit", "State");

	for (i = 0; i < SMU_FEATURE_COUNT; i++) {
		if (sort_feature[i] < 0)
			continue;

		size += sprintf(buf + size, "%02d. %-20s (%2d) : %s\n",
				count++,
				smu_get_feature_name(smu, sort_feature[i]),
				i,
				!!smu_cmn_feature_is_enabled(smu, sort_feature[i]) ?
				"enabled" : "disabled");
	}

	return size;
}

int smu_cmn_set_pp_feature_mask(struct smu_context *smu,
				uint64_t new_mask)
{
	int ret = 0;
	uint32_t feature_mask[2] = { 0 };
	uint64_t feature_2_enabled = 0;
	uint64_t feature_2_disabled = 0;
	uint64_t feature_enables = 0;

	ret = smu_cmn_get_enabled_mask(smu,
				       feature_mask,
				       2);
	if (ret)
		return ret;

	feature_enables = ((uint64_t)feature_mask[1] << 32 |
			   (uint64_t)feature_mask[0]);

	feature_2_enabled  = ~feature_enables & new_mask;
	feature_2_disabled = feature_enables & ~new_mask;

	if (feature_2_enabled) {
		ret = smu_cmn_feature_update_enable_state(smu,
							  feature_2_enabled,
							  true);
		if (ret)
			return ret;
	}
	if (feature_2_disabled) {
		ret = smu_cmn_feature_update_enable_state(smu,
							  feature_2_disabled,
							  false);
		if (ret)
			return ret;
	}

	return ret;
}

int smu_cmn_disable_all_features_with_exception(struct smu_context *smu,
						enum smu_feature_mask mask)
{
	uint64_t features_to_disable = U64_MAX;
	int skipped_feature_id;

	skipped_feature_id = smu_cmn_to_asic_specific_index(smu,
							    CMN2ASIC_MAPPING_FEATURE,
							    mask);
	if (skipped_feature_id < 0)
		return -EINVAL;

	features_to_disable &= ~(1ULL << skipped_feature_id);

	return smu_cmn_feature_update_enable_state(smu,
						   features_to_disable,
						   0);
}

int smu_cmn_get_smc_version(struct smu_context *smu,
			    uint32_t *if_version,
			    uint32_t *smu_version)
{
	int ret = 0;

	if (!if_version && !smu_version)
		return -EINVAL;

	if (smu->smc_fw_if_version && smu->smc_fw_version)
	{
		if (if_version)
			*if_version = smu->smc_fw_if_version;

		if (smu_version)
			*smu_version = smu->smc_fw_version;

		return 0;
	}

	if (if_version) {
		ret = smu_cmn_send_smc_msg(smu, SMU_MSG_GetDriverIfVersion, if_version);
		if (ret)
			return ret;

		smu->smc_fw_if_version = *if_version;
	}

	if (smu_version) {
		ret = smu_cmn_send_smc_msg(smu, SMU_MSG_GetSmuVersion, smu_version);
		if (ret)
			return ret;

		smu->smc_fw_version = *smu_version;
	}

	return ret;
}

int smu_cmn_update_table(struct smu_context *smu,
			 enum smu_table_id table_index,
			 int argument,
			 void *table_data,
			 bool drv2smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct amdgpu_device *adev = smu->adev;
	struct smu_table *table = &smu_table->driver_table;
	int table_id = smu_cmn_to_asic_specific_index(smu,
						      CMN2ASIC_MAPPING_TABLE,
						      table_index);
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

	ret = smu_cmn_send_smc_msg_with_param(smu, drv2smu ?
					  SMU_MSG_TransferTableDram2Smu :
					  SMU_MSG_TransferTableSmu2Dram,
					  table_id | ((argument & 0xFFFF) << 16),
					  NULL);
	if (ret)
		return ret;

	if (!drv2smu) {
		amdgpu_asic_invalidate_hdp(adev, NULL);
		memcpy(table_data, table->cpu_addr, table_size);
	}

	return 0;
}

int smu_cmn_write_watermarks_table(struct smu_context *smu)
{
	void *watermarks_table = smu->smu_table.watermarks_table;

	if (!watermarks_table)
		return -EINVAL;

	return smu_cmn_update_table(smu,
				    SMU_TABLE_WATERMARKS,
				    0,
				    watermarks_table,
				    true);
}

int smu_cmn_write_pptable(struct smu_context *smu)
{
	void *pptable = smu->smu_table.driver_pptable;

	return smu_cmn_update_table(smu,
				    SMU_TABLE_PPTABLE,
				    0,
				    pptable,
				    true);
}

int smu_cmn_get_metrics_table_locked(struct smu_context *smu,
				     void *metrics_table,
				     bool bypass_cache)
{
	struct smu_table_context *smu_table= &smu->smu_table;
	uint32_t table_size =
		smu_table->tables[SMU_TABLE_SMU_METRICS].size;
	int ret = 0;

	if (bypass_cache ||
	    !smu_table->metrics_time ||
	    time_after(jiffies, smu_table->metrics_time + msecs_to_jiffies(1))) {
		ret = smu_cmn_update_table(smu,
				       SMU_TABLE_SMU_METRICS,
				       0,
				       smu_table->metrics_table,
				       false);
		if (ret) {
			dev_info(smu->adev->dev, "Failed to export SMU metrics table!\n");
			return ret;
		}
		smu_table->metrics_time = jiffies;
	}

	if (metrics_table)
		memcpy(metrics_table, smu_table->metrics_table, table_size);

	return 0;
}

int smu_cmn_get_metrics_table(struct smu_context *smu,
			      void *metrics_table,
			      bool bypass_cache)
{
	int ret = 0;

	mutex_lock(&smu->metrics_lock);
	ret = smu_cmn_get_metrics_table_locked(smu,
					       metrics_table,
					       bypass_cache);
	mutex_unlock(&smu->metrics_lock);

	return ret;
}

void smu_cmn_init_soft_gpu_metrics(void *table, uint8_t frev, uint8_t crev)
{
	struct metrics_table_header *header = (struct metrics_table_header *)table;
	uint16_t structure_size;

#define METRICS_VERSION(a, b)	((a << 16) | b )

	switch (METRICS_VERSION(frev, crev)) {
	case METRICS_VERSION(1, 0):
		structure_size = sizeof(struct gpu_metrics_v1_0);
		break;
	case METRICS_VERSION(2, 0):
		structure_size = sizeof(struct gpu_metrics_v2_0);
		break;
	default:
		break;
	}

#undef METRICS_VERSION

	memset(header, 0xFF, structure_size);

	header->format_revision = frev;
	header->content_revision = crev;
	header->structure_size = structure_size;

}
