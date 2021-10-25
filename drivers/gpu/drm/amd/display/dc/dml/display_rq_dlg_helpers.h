/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#ifndef __DISPLAY_RQ_DLG_HELPERS_H__
#define __DISPLAY_RQ_DLG_HELPERS_H__

#include "display_mode_lib.h"

/* Function: Printer functions
 *  Print various struct
 */
void print__rq_params_st(struct display_mode_lib *mode_lib, const struct _vcs_dpi_display_rq_params_st *rq_param);
void print__data_rq_sizing_params_st(struct display_mode_lib *mode_lib, const struct _vcs_dpi_display_data_rq_sizing_params_st *rq_sizing);
void print__data_rq_dlg_params_st(struct display_mode_lib *mode_lib, const struct _vcs_dpi_display_data_rq_dlg_params_st *rq_dlg_param);
void print__data_rq_misc_params_st(struct display_mode_lib *mode_lib, const struct _vcs_dpi_display_data_rq_misc_params_st *rq_misc_param);
void print__rq_dlg_params_st(struct display_mode_lib *mode_lib, const struct _vcs_dpi_display_rq_dlg_params_st *rq_dlg_param);
void print__dlg_sys_params_st(struct display_mode_lib *mode_lib, const struct _vcs_dpi_display_dlg_sys_params_st *dlg_sys_param);

void print__data_rq_regs_st(struct display_mode_lib *mode_lib, const struct _vcs_dpi_display_data_rq_regs_st *rq_regs);
void print__rq_regs_st(struct display_mode_lib *mode_lib, const struct _vcs_dpi_display_rq_regs_st *rq_regs);
void print__dlg_regs_st(struct display_mode_lib *mode_lib, const struct _vcs_dpi_display_dlg_regs_st *dlg_regs);
void print__ttu_regs_st(struct display_mode_lib *mode_lib, const struct _vcs_dpi_display_ttu_regs_st *ttu_regs);

#endif
