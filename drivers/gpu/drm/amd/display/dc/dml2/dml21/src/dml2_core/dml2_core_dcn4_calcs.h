// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML2_CORE_DCN4_CALCS_H__
#define __DML2_CORE_DCN4_CALCS_H__

#include "dml2_core_shared_types.h"

struct dml2_dchub_watermark_regs;
struct dml2_display_arb_regs;
struct dml2_per_stream_programming;
struct dml2_dchub_per_pipe_register_set;
struct core_plane_support_info;
struct core_stream_support_info;
struct dml2_cursor_dlg_regs;
struct display_configuation_with_meta;

unsigned int dml2_core_calcs_mode_support_ex(struct dml2_core_calcs_mode_support_ex *in_out_params);
bool dml2_core_calcs_mode_programming_ex(struct dml2_core_calcs_mode_programming_ex *in_out_params);
void dml2_core_calcs_get_watermarks(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib, struct dml2_dchub_watermark_regs *out);
void dml2_core_calcs_get_arb_params(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib, struct dml2_display_arb_regs *out);
void dml2_core_calcs_get_pipe_regs(const struct dml2_display_cfg *dml2_display_cfg, struct dml2_core_internal_display_mode_lib *mode_lib, struct dml2_dchub_per_pipe_register_set *out, int pipe_index);
void dml2_core_calcs_get_stream_programming(const struct dml2_core_internal_display_mode_lib *mode_lib, struct dml2_per_stream_programming *out, int pipe_index);
void dml2_core_calcs_get_global_sync_programming(const struct dml2_core_internal_display_mode_lib *mode_lib, union dml2_global_sync_programming *out, int pipe_index);
void dml2_core_calcs_get_mcache_allocation(const struct dml2_core_internal_display_mode_lib *mode_lib, struct dml2_mcache_surface_allocation *out, int plane_index);
void dml2_core_calcs_get_plane_support_info(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib, struct core_plane_support_info *out, int plane_index);
void dml2_core_calcs_get_informative(const struct dml2_core_internal_display_mode_lib *mode_lib, struct dml2_display_cfg_programming *out);
void dml2_core_calcs_get_stream_support_info(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib, struct core_stream_support_info *out, int plane_index);
void dml2_core_calcs_get_mall_allocation(struct dml2_core_internal_display_mode_lib *mode_lib, unsigned int *out, int pipe_index);
void dml2_core_calcs_get_stream_fams2_programming(const struct dml2_core_internal_display_mode_lib *mode_lib, const struct display_configuation_with_meta *display_cfg, struct dmub_fams2_stream_static_state *fams2_programming, enum dml2_uclk_pstate_support_method pstate_method, int plane_index);
void dml2_core_calcs_get_global_fams2_programming(const struct dml2_core_internal_display_mode_lib *mode_lib, const struct display_configuation_with_meta *display_cfg, struct dmub_cmd_fams2_global_config *fams2_global_config);

void dml2_core_calcs_get_dpte_row_height(unsigned int *dpte_row_height, struct dml2_core_internal_display_mode_lib *mode_lib, bool is_plane1, enum dml2_source_format_class SourcePixelFormat, enum dml2_swizzle_mode SurfaceTiling, enum dml2_rotation_angle ScanDirection, unsigned int pitch, unsigned int GPUVMMinPageSizeKBytes);
void dml2_core_calcs_cursor_dlg_reg(struct dml2_cursor_dlg_regs *cursor_dlg_regs, const struct dml2_get_cursor_dlg_reg *p);
const char *dml2_core_internal_bw_type_str(enum dml2_core_internal_bw_type bw_type);
const char *dml2_core_internal_soc_state_type_str(enum dml2_core_internal_soc_state_type dml2_core_internal_soc_state_type);

#endif
