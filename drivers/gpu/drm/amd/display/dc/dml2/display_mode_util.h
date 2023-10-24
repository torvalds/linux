/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#ifndef __DISPLAY_MODE_UTIL_H__
#define __DISPLAY_MODE_UTIL_H__

#include "display_mode_core_structs.h"
#include "cmntypes.h"


#include "dml_assert.h"
#include "dml_logging.h"

__DML_DLL_EXPORT__ dml_bool_t dml_util_is_420(enum dml_source_format_class source_format);
__DML_DLL_EXPORT__ dml_float_t dml_ceil(dml_float_t x, dml_float_t granularity);
__DML_DLL_EXPORT__ dml_float_t dml_floor(dml_float_t x, dml_float_t granularity);
__DML_DLL_EXPORT__ dml_float_t dml_min(dml_float_t x, dml_float_t y);
__DML_DLL_EXPORT__ dml_float_t dml_min3(dml_float_t x, dml_float_t y, dml_float_t z);
__DML_DLL_EXPORT__ dml_float_t dml_min4(dml_float_t x, dml_float_t y, dml_float_t z, dml_float_t w);
__DML_DLL_EXPORT__ dml_float_t dml_max(dml_float_t x, dml_float_t y);
__DML_DLL_EXPORT__ dml_float_t dml_max3(dml_float_t x, dml_float_t y, dml_float_t z);
__DML_DLL_EXPORT__ dml_float_t dml_max4(dml_float_t a, dml_float_t b, dml_float_t c, dml_float_t d);
__DML_DLL_EXPORT__ dml_float_t dml_max5(dml_float_t a, dml_float_t b, dml_float_t c, dml_float_t d, dml_float_t e);
__DML_DLL_EXPORT__ dml_float_t dml_log(dml_float_t x, dml_float_t base);
__DML_DLL_EXPORT__ dml_float_t dml_log2(dml_float_t x);
__DML_DLL_EXPORT__ dml_float_t dml_round(dml_float_t val, dml_bool_t bankers_rounding);
__DML_DLL_EXPORT__ dml_float_t dml_pow(dml_float_t base, int exp);
__DML_DLL_EXPORT__ dml_uint_t dml_round_to_multiple(dml_uint_t num, dml_uint_t multiple, dml_bool_t up);
__DML_DLL_EXPORT__ dml_bool_t dml_is_vertical_rotation(enum dml_rotation_angle scan);
__DML_DLL_EXPORT__ dml_uint_t  dml_get_cursor_bit_per_pixel(enum dml_cursor_bpp ebpp);
__DML_DLL_EXPORT__ void dml_print_data_rq_regs_st(const dml_display_plane_rq_regs_st *data_rq_regs);
__DML_DLL_EXPORT__ void dml_print_rq_regs_st(const dml_display_rq_regs_st *rq_regs);
__DML_DLL_EXPORT__ void dml_print_dlg_regs_st(const dml_display_dlg_regs_st *dlg_regs);
__DML_DLL_EXPORT__ void dml_print_ttu_regs_st(const dml_display_ttu_regs_st *ttu_regs);
__DML_DLL_EXPORT__ void dml_print_dml_policy(const struct dml_mode_eval_policy_st *policy);
__DML_DLL_EXPORT__ void dml_print_mode_support(struct display_mode_lib_st *mode_lib, dml_uint_t j);
__DML_DLL_EXPORT__ void dml_print_dml_mode_support_info(const struct dml_mode_support_info_st *support, dml_bool_t fail_only);
__DML_DLL_EXPORT__ void dml_print_dml_display_cfg_timing(const struct dml_timing_cfg_st *timing, dml_uint_t num_plane);
__DML_DLL_EXPORT__ void dml_print_dml_display_cfg_plane(const struct dml_plane_cfg_st *plane, dml_uint_t num_plane);
__DML_DLL_EXPORT__ void dml_print_dml_display_cfg_surface(const struct dml_surface_cfg_st *surface, dml_uint_t num_plane);
__DML_DLL_EXPORT__ void dml_print_dml_display_cfg_hw_resource(const struct dml_hw_resource_st *hw, dml_uint_t num_plane);
__DML_DLL_EXPORT__ void dml_print_soc_state_bounding_box(const struct soc_state_bounding_box_st *state);
__DML_DLL_EXPORT__ void dml_print_soc_bounding_box(const struct soc_bounding_box_st *soc);
__DML_DLL_EXPORT__ void dml_print_clk_cfg(const struct dml_clk_cfg_st *clk_cfg);

__DML_DLL_EXPORT__ dml_uint_t dml_get_num_active_planes(const struct dml_display_cfg_st *display_cfg);
__DML_DLL_EXPORT__ dml_uint_t dml_get_num_active_pipes(const struct dml_display_cfg_st *display_cfg);
__DML_DLL_EXPORT__ dml_uint_t dml_get_plane_idx(const struct display_mode_lib_st *mode_lib, dml_uint_t pipe_idx);
__DML_DLL_EXPORT__ dml_uint_t dml_get_pipe_idx(const struct display_mode_lib_st *mode_lib, dml_uint_t plane_idx);
__DML_DLL_EXPORT__ void       dml_calc_pipe_plane_mapping(const struct dml_hw_resource_st *hw, dml_uint_t *pipe_plane);


#endif
