// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.


#ifndef __DML2_CORE_SHARED_H__
#define __DML2_CORE_SHARED_H__

#define __DML_VBA_DEBUG__
#define __DML2_CALCS_MAX_VRATIO_PRE_OTO__ 4.0 //<brief Prefetch schedule max vratio for one to one scheduling calculation for prefetch
#define __DML2_CALCS_MAX_VRATIO_PRE_ENHANCE_PREFETCH_ACC__ 6.0 //<brief Prefetch schedule max vratio when enhance prefetch schedule acceleration is enabled and vstartup is earliest possible already
#define __DML2_CALCS_DPP_INVALID__ 0
#define __DML2_CALCS_DCFCLK_FACTOR__ 1.15 //<brief fudge factor for min dcfclk calclation
#define __DML2_CALCS_PIPE_NO_PLANE__ 99

#include "dml2_core_shared_types.h"
#include "dml2_internal_shared_types.h"

double dml2_core_shared_div_rem(double dividend, unsigned int divisor, unsigned int *remainder);

const char *dml2_core_internal_bw_type_str(enum dml2_core_internal_bw_type bw_type);
const char *dml2_core_internal_soc_state_type_str(enum dml2_core_internal_soc_state_type dml2_core_internal_soc_state_type);
bool dml2_core_shared_is_420(enum dml2_source_format_class source_format);

bool dml2_core_shared_mode_support(struct dml2_core_calcs_mode_support_ex *in_out_params);
bool dml2_core_shared_mode_programming(struct dml2_core_calcs_mode_programming_ex *in_out_params);
void dml2_core_shared_get_watermarks(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib, struct dml2_dchub_watermark_regs *out);
void dml2_core_shared_get_arb_params(const struct dml2_core_internal_display_mode_lib *mode_lib, struct dml2_display_arb_regs *out);
void dml2_core_shared_get_pipe_regs(const struct dml2_display_cfg *display_cfg,	struct dml2_core_internal_display_mode_lib *mode_lib,	struct dml2_dchub_per_pipe_register_set *out, int pipe_index);
void dml2_core_shared_get_stream_programming(const struct dml2_core_internal_display_mode_lib *mode_lib, struct dml2_per_stream_programming *out, int pipe_index);
void dml2_core_shared_get_mcache_allocation(const struct dml2_core_internal_display_mode_lib *mode_lib, struct dml2_mcache_surface_allocation *out, int plane_idx);
void dml2_core_shared_get_mall_allocation(struct dml2_core_internal_display_mode_lib *mode_lib, unsigned int *out, int pipe_index);
void dml2_core_shared_get_plane_support_info(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib, struct core_plane_support_info *out, int plane_idx);
void dml2_core_shared_get_stream_support_info(const struct dml2_display_cfg *display_cfg, const struct dml2_core_internal_display_mode_lib *mode_lib, struct core_stream_support_info *out, int plane_index);
void dml2_core_shared_get_informative(const struct dml2_core_internal_display_mode_lib *mode_lib, struct dml2_display_cfg_programming *out);
void dml2_core_shared_cursor_dlg_reg(struct dml2_cursor_dlg_regs *cursor_dlg_regs, const struct dml2_get_cursor_dlg_reg *p);

#endif
