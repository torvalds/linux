// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML2_CORE_UTILS_H__
#define __DML2_CORE_UTILS_H__
#include "dml2_internal_shared_types.h"
#include "dml2_debug.h"
#include "lib_float_math.h"

double dml2_core_utils_div_rem(double dividend, unsigned int divisor, unsigned int *remainder);
const char *dml2_core_utils_internal_bw_type_str(enum dml2_core_internal_bw_type bw_type);
bool dml2_core_utils_is_420(enum dml2_source_format_class source_format);
bool dml2_core_utils_is_422_planar(enum dml2_source_format_class source_format);
bool dml2_core_utils_is_422_packed(enum dml2_source_format_class source_format);
void dml2_core_utils_print_mode_support_info(const struct dml2_core_internal_mode_support_info *support, bool fail_only);
const char *dml2_core_utils_internal_soc_state_type_str(enum dml2_core_internal_soc_state_type dml2_core_internal_soc_state_type);
void dml2_core_utils_get_stream_output_bpp(double *out_bpp, const struct dml2_display_cfg *display_cfg);
unsigned int dml2_core_utils_round_to_multiple(unsigned int num, unsigned int multiple, bool up);
unsigned int dml2_core_util_get_num_active_pipes(int unsigned num_planes, const struct core_display_cfg_support_info *cfg_support_info);
void dml2_core_utils_pipe_plane_mapping(const struct core_display_cfg_support_info *cfg_support_info, unsigned int *pipe_plane);
bool dml2_core_utils_is_phantom_pipe(const struct dml2_plane_parameters *plane_cfg);
unsigned int dml2_core_utils_get_tile_block_size_bytes(enum dml2_swizzle_mode sw_mode, unsigned int byte_per_pixel);
bool dml2_core_utils_get_segment_horizontal_contiguous(enum dml2_swizzle_mode sw_mode, unsigned int byte_per_pixel);
bool dml2_core_utils_is_vertical_rotation(enum dml2_rotation_angle Scan);
bool dml2_core_utils_is_linear(enum dml2_swizzle_mode sw_mode);
int unsigned dml2_core_utils_get_gfx_version(enum dml2_swizzle_mode sw_mode);
unsigned int dml2_core_utils_get_qos_param_index(unsigned long uclk_freq_khz, const struct dml2_dcn4_uclk_dpm_dependent_qos_params *per_uclk_dpm_params);
unsigned int dml2_core_utils_get_active_min_uclk_dpm_index(unsigned long uclk_freq_khz, const struct dml2_soc_state_table *clk_table);
bool dml2_core_utils_is_dual_plane(enum dml2_source_format_class source_format);
unsigned int dml2_core_utils_log_and_substract_if_non_zero(unsigned int a, unsigned int subtrahend);
void dml2_core_utils_expand_implict_subvp(const struct display_configuation_with_meta *display_cfg, struct dml2_display_cfg *svp_expanded_display_cfg,
	struct dml2_core_scratch *scratch);
bool dml2_core_utils_is_stream_encoder_required(const struct dml2_stream_parameters *stream_descriptor);
bool dml2_core_utils_is_encoder_dsc_capable(const struct dml2_stream_parameters *stream_descriptor);
bool dml2_core_utils_is_dp_encoder(const struct dml2_stream_parameters *stream_descriptor);
bool dml2_core_utils_is_dio_dp_encoder(const struct dml2_stream_parameters *stream_descriptor);
bool dml2_core_utils_is_hpo_dp_encoder(const struct dml2_stream_parameters *stream_descriptor);
bool dml2_core_utils_is_dp_8b_10b_link_rate(enum dml2_output_link_dp_rate rate);
bool dml2_core_utils_is_dp_128b_132b_link_rate(enum dml2_output_link_dp_rate rate);
bool dml2_core_utils_is_odm_split(enum dml2_odm_mode odm_mode);

#endif /* __DML2_CORE_UTILS_H__ */
