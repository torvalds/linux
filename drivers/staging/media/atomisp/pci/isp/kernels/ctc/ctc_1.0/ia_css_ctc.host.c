// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "ia_css_types.h"
#include "sh_css_defs.h"
#include "ia_css_debug.h"
#include "assert_support.h"

#include "ia_css_ctc.host.h"

const struct ia_css_ctc_config default_ctc_config = {
	((1 << IA_CSS_CTC_COEF_SHIFT) + 1) / 2,		/* 0.5 */
	((1 << IA_CSS_CTC_COEF_SHIFT) + 1) / 2,		/* 0.5 */
	((1 << IA_CSS_CTC_COEF_SHIFT) + 1) / 2,		/* 0.5 */
	((1 << IA_CSS_CTC_COEF_SHIFT) + 1) / 2,		/* 0.5 */
	((1 << IA_CSS_CTC_COEF_SHIFT) + 1) / 2,		/* 0.5 */
	((1 << IA_CSS_CTC_COEF_SHIFT) + 1) / 2,		/* 0.5 */
	1,
	SH_CSS_BAYER_MAXVAL / 5,	/* To be implemented */
	SH_CSS_BAYER_MAXVAL * 2 / 5,	/* To be implemented */
	SH_CSS_BAYER_MAXVAL * 3 / 5,	/* To be implemented */
	SH_CSS_BAYER_MAXVAL * 4 / 5,	/* To be implemented */
};

void
ia_css_ctc_vamem_encode(
    struct sh_css_isp_ctc_vamem_params *to,
    const struct ia_css_ctc_table *from,
    unsigned int size)
{
	(void)size;
	memcpy(&to->ctc,  &from->data, sizeof(to->ctc));
}

void
ia_css_ctc_debug_dtrace(
    const struct ia_css_ctc_config *config,
    unsigned int level)
{
	ia_css_debug_dtrace(level,
			    "config.ce_gain_exp=%d, config.y0=%d, config.x1=%d, config.y1=%d, config.x2=%d, config.y2=%d, config.x3=%d, config.y3=%d, config.x4=%d, config.y4=%d\n",
			    config->ce_gain_exp, config->y0,
			    config->x1, config->y1,
			    config->x2, config->y2,
			    config->x3, config->y3,
			    config->x4, config->y4);
}
