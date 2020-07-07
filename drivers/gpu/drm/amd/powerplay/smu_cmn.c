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

#include "amdgpu.h"
#include "amdgpu_smu.h"
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

int smu_cmn_to_asic_specific_index(struct smu_context *smu,
				   enum smu_cmn2asic_mapping_type type,
				   uint32_t index)
{
	struct cmn2asic_msg_mapping msg_mapping;
	struct cmn2asic_mapping mapping;

	switch (type) {
	case CMN2ASIC_MAPPING_MSG:
		if (index > SMU_MSG_MAX_COUNT ||
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
		if (index > SMU_CLK_COUNT ||
		    !smu->clock_map)
			return -EINVAL;

		mapping = smu->clock_map[index];
		if (!mapping.valid_mapping)
			return -EINVAL;

		return mapping.map_to;

	case CMN2ASIC_MAPPING_FEATURE:
		if (index > SMU_FEATURE_COUNT ||
		    !smu->feature_map)
			return -EINVAL;

		mapping = smu->feature_map[index];
		if (!mapping.valid_mapping)
			return -EINVAL;

		return mapping.map_to;

	case CMN2ASIC_MAPPING_TABLE:
		if (index > SMU_TABLE_COUNT ||
		    !smu->table_map)
			return -EINVAL;

		mapping = smu->table_map[index];
		if (!mapping.valid_mapping)
			return -EINVAL;

		return mapping.map_to;

	case CMN2ASIC_MAPPING_PWR:
		if (index > SMU_POWER_SOURCE_COUNT ||
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
