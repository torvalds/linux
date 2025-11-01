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

#define FDO_PWM_MODE_STATIC  1
#define FDO_PWM_MODE_STATIC_RPM 5

#define SMU_IH_INTERRUPT_ID_TO_DRIVER                   0xFE
#define SMU_IH_INTERRUPT_CONTEXT_ID_BACO                0x2
#define SMU_IH_INTERRUPT_CONTEXT_ID_AC                  0x3
#define SMU_IH_INTERRUPT_CONTEXT_ID_DC                  0x4
#define SMU_IH_INTERRUPT_CONTEXT_ID_AUDIO_D0            0x5
#define SMU_IH_INTERRUPT_CONTEXT_ID_AUDIO_D3            0x6
#define SMU_IH_INTERRUPT_CONTEXT_ID_THERMAL_THROTTLING  0x7
#define SMU_IH_INTERRUPT_CONTEXT_ID_FAN_ABNORMAL        0x8
#define SMU_IH_INTERRUPT_CONTEXT_ID_FAN_RECOVERY        0x9

#define SMU_IGNORE_IF_VERSION 0xFFFFFFFF

#define smu_cmn_init_soft_gpu_metrics(ptr, frev, crev)                   \
	do {                                                             \
		typecheck(struct gpu_metrics_v##frev##_##crev *, (ptr)); \
		struct gpu_metrics_v##frev##_##crev *tmp = (ptr);        \
		struct metrics_table_header *header =                    \
			(struct metrics_table_header *)tmp;              \
		memset(header, 0xFF, sizeof(*tmp));                      \
		header->format_revision = frev;                          \
		header->content_revision = crev;                         \
		header->structure_size = sizeof(*tmp);                   \
	} while (0)

#define smu_cmn_init_partition_metrics(ptr, fr, cr)                        \
	do {                                                               \
		typecheck(struct amdgpu_partition_metrics_v##fr##_##cr *,  \
			  (ptr));                                          \
		struct amdgpu_partition_metrics_v##fr##_##cr *tmp = (ptr); \
		struct metrics_table_header *header =                      \
			(struct metrics_table_header *)tmp;                \
		memset(header, 0xFF, sizeof(*tmp));                        \
		header->format_revision = fr;                              \
		header->content_revision = cr;                             \
		header->structure_size = sizeof(*tmp);                     \
	} while (0)

#define smu_cmn_init_baseboard_temp_metrics(ptr, fr, cr)                        \
	do {                                                                    \
		typecheck(struct amdgpu_baseboard_temp_metrics_v##fr##_##cr *,  \
			  (ptr));                                               \
		struct amdgpu_baseboard_temp_metrics_v##fr##_##cr *tmp = (ptr); \
		struct metrics_table_header *header =                           \
			(struct metrics_table_header *)tmp;                     \
		memset(header, 0xFF, sizeof(*tmp));                             \
		header->format_revision = fr;                                   \
		header->content_revision = cr;                                  \
		header->structure_size = sizeof(*tmp);                          \
	} while (0)

#define smu_cmn_init_gpuboard_temp_metrics(ptr, fr, cr)                         \
	do {                                                                    \
		typecheck(struct amdgpu_gpuboard_temp_metrics_v##fr##_##cr *,   \
			  (ptr));                                               \
		struct amdgpu_gpuboard_temp_metrics_v##fr##_##cr *tmp = (ptr);  \
		struct metrics_table_header *header =                           \
			(struct metrics_table_header *)tmp;                     \
		memset(header, 0xFF, sizeof(*tmp));                             \
		header->format_revision = fr;                                   \
		header->content_revision = cr;                                  \
		header->structure_size = sizeof(*tmp);                          \
	} while (0)

extern const int link_speed[];

/* Helper to Convert from PCIE Gen 1/2/3/4/5/6 to 0.1 GT/s speed units */
static inline int pcie_gen_to_speed(uint32_t gen)
{
	return ((gen == 0) ? link_speed[0] : link_speed[gen - 1]);
}

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

int smu_cmn_send_debug_smc_msg(struct smu_context *smu,
			 uint32_t msg);

int smu_cmn_send_debug_smc_msg_with_param(struct smu_context *smu,
			 uint32_t msg, uint32_t param);

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
			     uint64_t *feature_mask);

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

int smu_cmn_get_metrics_table(struct smu_context *smu,
			      void *metrics_table,
			      bool bypass_cache);

int smu_cmn_get_combo_pptable(struct smu_context *smu);

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
void smu_cmn_generic_soc_policy_desc(struct smu_dpm_policy *policy);
void smu_cmn_generic_plpd_policy_desc(struct smu_dpm_policy *policy);

void smu_cmn_get_backend_workload_mask(struct smu_context *smu,
				       u32 workload_mask,
				       u32 *backend_workload_mask);

#endif
#endif
