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

#ifndef __SMU_CMN_H__
#define __SMU_CMN_H__

#include "amdgpu_smu.h"

#if defined(SWSMU_CODE_LAYER_L2) || defined(SWSMU_CODE_LAYER_L3) || defined(SWSMU_CODE_LAYER_L4)
int smu_cmn_send_msg_without_waiting(struct smu_context *smu,
				     uint16_t msg_index,
				     uint32_t param);
int smu_cmn_send_smc_msg_with_param(struct smu_context *smu,
				    enum smu_message_type msg,
				    uint32_t param,
				    uint32_t *read_arg);

int smu_cmn_send_smc_msg(struct smu_context *smu,
			 enum smu_message_type msg,
			 uint32_t *read_arg);

int smu_cmn_wait_for_response(struct smu_context *smu);

int smu_cmn_to_asic_specific_index(struct smu_context *smu,
				   enum smu_cmn2asic_mapping_type type,
				   uint32_t index);

int smu_cmn_feature_is_supported(struct smu_context *smu,
				 enum smu_feature_mask mask);

int smu_cmn_feature_is_enabled(struct smu_context *smu,
			       enum smu_feature_mask mask);

bool smu_cmn_clk_dpm_is_enabled(struct smu_context *smu,
				enum smu_clk_type clk_type);

int smu_cmn_get_enabled_mask(struct smu_context *smu,
			     uint32_t *feature_mask,
			     uint32_t num);

int smu_cmn_get_enabled_32_bits_mask(struct smu_context *smu,
					uint32_t *feature_mask,
					uint32_t num);

uint64_t smu_cmn_get_indep_throttler_status(
					const unsigned long dep_status,
					const uint8_t *throttler_map);

int smu_cmn_feature_update_enable_state(struct smu_context *smu,
					uint64_t feature_mask,
					bool enabled);

int smu_cmn_feature_set_enabled(struct smu_context *smu,
				enum smu_feature_mask mask,
				bool enable);

size_t smu_cmn_get_pp_feature_mask(struct smu_context *smu,
				   char *buf);

int smu_cmn_set_pp_feature_mask(struct smu_context *smu,
				uint64_t new_mask);

int smu_cmn_disable_all_features_with_exception(struct smu_context *smu,
						bool no_hw_disablement,
						enum smu_feature_mask mask);

int smu_cmn_get_smc_version(struct smu_context *smu,
			    uint32_t *if_version,
			    uint32_t *smu_version);

int smu_cmn_update_table(struct smu_context *smu,
			 enum smu_table_id table_index,
			 int argument,
			 void *table_data,
			 bool drv2smu);

int smu_cmn_write_watermarks_table(struct smu_context *smu);

int smu_cmn_write_pptable(struct smu_context *smu);

int smu_cmn_get_metrics_table_locked(struct smu_context *smu,
				     void *metrics_table,
				     bool bypass_cache);

int smu_cmn_get_metrics_table(struct smu_context *smu,
			      void *metrics_table,
			      bool bypass_cache);

void smu_cmn_init_soft_gpu_metrics(void *table, uint8_t frev, uint8_t crev);

int smu_cmn_set_mp1_state(struct smu_context *smu,
			  enum pp_mp1_state mp1_state);

/*
 * Helper function to make sysfs_emit_at() happy. Align buf to
 * the current page boundary and record the offset.
 */
static inline void smu_cmn_get_sysfs_buf(char **buf, int *offset)
{
	if (!*buf || !offset)
		return;

	*offset = offset_in_page(*buf);
	*buf -= *offset;
}

bool smu_cmn_is_audio_func_enabled(struct amdgpu_device *adev);

#endif
#endif
