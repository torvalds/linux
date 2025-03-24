// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#include "type_support.h"
#include "ia_css_bnr2_2.host.h"

#ifndef IA_CSS_NO_DEBUG
#include "ia_css_debug.h" /* ia_css_debug_dtrace() */
#endif

/* Default kernel parameters. */
const struct ia_css_bnr2_2_config default_bnr2_2_config = {
	200,
	200,
	200,
	0,
	0,
	0,
	200,
	200,
	200,
	0,
	0,
	0,
	0,
	4096,
	8191,
	128,
	1,
	0,
	0,
	0,
	8191,
	0,
	8191
};

void
ia_css_bnr2_2_encode(
    struct sh_css_isp_bnr2_2_params *to,
    const struct ia_css_bnr2_2_config *from,
    size_t size)
{
	(void)size;
	to->d_var_gain_r = from->d_var_gain_r;
	to->d_var_gain_g = from->d_var_gain_g;
	to->d_var_gain_b = from->d_var_gain_b;
	to->d_var_gain_slope_r = from->d_var_gain_slope_r;
	to->d_var_gain_slope_g = from->d_var_gain_slope_g;
	to->d_var_gain_slope_b = from->d_var_gain_slope_b;

	to->n_var_gain_r = from->n_var_gain_r;
	to->n_var_gain_g = from->n_var_gain_g;
	to->n_var_gain_b = from->n_var_gain_b;
	to->n_var_gain_slope_r = from->n_var_gain_slope_r;
	to->n_var_gain_slope_g = from->n_var_gain_slope_g;
	to->n_var_gain_slope_b = from->n_var_gain_slope_b;

	to->dir_thres = from->dir_thres;
	to->dir_thres_w = from->dir_thres_w;
	to->var_offset_coef = from->var_offset_coef;

	to->dir_gain = from->dir_gain;
	to->detail_gain	= from->detail_gain;
	to->detail_gain_divisor = from->detail_gain_divisor;
	to->detail_level_offset = from->detail_level_offset;

	to->d_var_th_min = from->d_var_th_min;
	to->d_var_th_max = from->d_var_th_max;
	to->n_var_th_min = from->n_var_th_min;
	to->n_var_th_max = from->n_var_th_max;
}

#ifndef IA_CSS_NO_DEBUG
void
ia_css_bnr2_2_debug_dtrace(
    const struct ia_css_bnr2_2_config *bnr,
    unsigned int level)
{
	if (!bnr)
		return;

	ia_css_debug_dtrace(level, "Bayer Noise Reduction 2.2:\n");
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "d_var_gain_r", bnr->d_var_gain_r);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "d_var_gain_g", bnr->d_var_gain_g);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "d_var_gain_b", bnr->d_var_gain_b);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "d_var_gain_slope_r",
			    bnr->d_var_gain_slope_r);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "d_var_gain_slope_g",
			    bnr->d_var_gain_slope_g);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "d_var_gain_slope_b",
			    bnr->d_var_gain_slope_b);

	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "n_var_gain_r", bnr->n_var_gain_r);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "n_var_gain_g", bnr->n_var_gain_g);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "n_var_gain_b", bnr->n_var_gain_b);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "n_var_gain_slope_r",
			    bnr->n_var_gain_slope_r);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "n_var_gain_slope_g",
			    bnr->n_var_gain_slope_g);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "n_var_gain_slope_b",
			    bnr->n_var_gain_slope_b);

	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "dir_thres", bnr->dir_thres);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "dir_thres_w", bnr->dir_thres_w);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "var_offset_coef",
			    bnr->var_offset_coef);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "dir_gain", bnr->dir_gain);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "detail_gain", bnr->detail_gain);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "detail_gain_divisor",
			    bnr->detail_gain_divisor);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "detail_level_offset",
			    bnr->detail_level_offset);

	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "d_var_th_min", bnr->d_var_th_min);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "d_var_th_max", bnr->d_var_th_max);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "n_var_th_min", bnr->n_var_th_min);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "n_var_th_max", bnr->n_var_th_max);
}
#endif /* IA_CSS_NO_DEBUG */
