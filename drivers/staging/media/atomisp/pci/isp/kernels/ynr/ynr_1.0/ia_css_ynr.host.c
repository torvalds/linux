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
#include "sh_css_frac.h"

#include "bnr/bnr_1.0/ia_css_bnr.host.h"
#include "ia_css_ynr.host.h"

const struct ia_css_nr_config default_nr_config = {
	16384,
	8192,
	1280,
	0,
	0
};

const struct ia_css_ee_config default_ee_config = {
	8192,
	128,
	2048
};

void
ia_css_nr_encode(
    struct sh_css_isp_ynr_params *to,
    const struct ia_css_nr_config *from,
    unsigned int size)
{
	(void)size;
	/* YNR (Y Noise Reduction) */
	to->threshold =
	    uDIGIT_FITTING(8192U, 16, SH_CSS_BAYER_BITS);
	to->gain_all =
	    uDIGIT_FITTING(from->ynr_gain, 16, SH_CSS_YNR_GAIN_SHIFT);
	to->gain_dir =
	    uDIGIT_FITTING(from->ynr_gain, 16, SH_CSS_YNR_GAIN_SHIFT);
	to->threshold_cb =
	    uDIGIT_FITTING(from->threshold_cb, 16, SH_CSS_BAYER_BITS);
	to->threshold_cr =
	    uDIGIT_FITTING(from->threshold_cr, 16, SH_CSS_BAYER_BITS);
}

void
ia_css_yee_encode(
    struct sh_css_isp_yee_params *to,
    const struct ia_css_yee_config *from,
    unsigned int size)
{
	int asiWk1 = (int)from->ee.gain;
	int asiWk2 = asiWk1 / 8;
	int asiWk3 = asiWk1 / 4;

	(void)size;
	/* YEE (Y Edge Enhancement) */
	to->dirthreshold_s =
	    min((uDIGIT_FITTING(from->nr.direction, 16, SH_CSS_BAYER_BITS)
		 << 1),
		SH_CSS_BAYER_MAXVAL);
	to->dirthreshold_g =
	    min((uDIGIT_FITTING(from->nr.direction, 16, SH_CSS_BAYER_BITS)
		 << 4),
		SH_CSS_BAYER_MAXVAL);
	to->dirthreshold_width_log2 =
	    uFRACTION_BITS_FITTING(8);
	to->dirthreshold_width =
	    1 << to->dirthreshold_width_log2;
	to->detailgain =
	    uDIGIT_FITTING(from->ee.detail_gain, 11,
			   SH_CSS_YEE_DETAIL_GAIN_SHIFT);
	to->coring_s =
	    (uDIGIT_FITTING(56U, 16, SH_CSS_BAYER_BITS) *
	     from->ee.threshold) >> 8;
	to->coring_g =
	    (uDIGIT_FITTING(224U, 16, SH_CSS_BAYER_BITS) *
	     from->ee.threshold) >> 8;
	/* 8; // *1.125 ->[s4.8] */
	to->scale_plus_s =
	    (asiWk1 + asiWk2) >> (11 - SH_CSS_YEE_SCALE_SHIFT);
	/* 8; // ( * -.25)->[s4.8] */
	to->scale_plus_g =
	    (0 - asiWk3) >> (11 - SH_CSS_YEE_SCALE_SHIFT);
	/* 8; // *0.875 ->[s4.8] */
	to->scale_minus_s =
	    (asiWk1 - asiWk2) >> (11 - SH_CSS_YEE_SCALE_SHIFT);
	/* 8; // ( *.25 ) ->[s4.8] */
	to->scale_minus_g =
	    (asiWk3) >> (11 - SH_CSS_YEE_SCALE_SHIFT);
	to->clip_plus_s =
	    uDIGIT_FITTING(32760U, 16, SH_CSS_BAYER_BITS);
	to->clip_plus_g = 0;
	to->clip_minus_s =
	    uDIGIT_FITTING(504U, 16, SH_CSS_BAYER_BITS);
	to->clip_minus_g =
	    uDIGIT_FITTING(32256U, 16, SH_CSS_BAYER_BITS);
	to->Yclip = SH_CSS_BAYER_MAXVAL;
}

void
ia_css_nr_dump(
    const struct sh_css_isp_ynr_params *ynr,
    unsigned int level)
{
	if (!ynr) return;
	ia_css_debug_dtrace(level,
			    "Y Noise Reduction:\n");
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "ynr_threshold", ynr->threshold);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "ynr_gain_all", ynr->gain_all);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "ynr_gain_dir", ynr->gain_dir);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "ynr_threshold_cb", ynr->threshold_cb);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "ynr_threshold_cr", ynr->threshold_cr);
}

void
ia_css_yee_dump(
    const struct sh_css_isp_yee_params *yee,
    unsigned int level)
{
	ia_css_debug_dtrace(level,
			    "Y Edge Enhancement:\n");
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "ynryee_dirthreshold_s",
			    yee->dirthreshold_s);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "ynryee_dirthreshold_g",
			    yee->dirthreshold_g);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "ynryee_dirthreshold_width_log2",
			    yee->dirthreshold_width_log2);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "ynryee_dirthreshold_width",
			    yee->dirthreshold_width);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "yee_detailgain",
			    yee->detailgain);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "yee_coring_s",
			    yee->coring_s);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "yee_coring_g",
			    yee->coring_g);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "yee_scale_plus_s",
			    yee->scale_plus_s);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "yee_scale_plus_g",
			    yee->scale_plus_g);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "yee_scale_minus_s",
			    yee->scale_minus_s);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "yee_scale_minus_g",
			    yee->scale_minus_g);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "yee_clip_plus_s",
			    yee->clip_plus_s);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "yee_clip_plus_g",
			    yee->clip_plus_g);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "yee_clip_minus_s",
			    yee->clip_minus_s);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "yee_clip_minus_g",
			    yee->clip_minus_g);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "ynryee_Yclip",
			    yee->Yclip);
}

void
ia_css_nr_debug_dtrace(
    const struct ia_css_nr_config *config,
    unsigned int level)
{
	ia_css_debug_dtrace(level,
			    "config.direction=%d, config.bnr_gain=%d, config.ynr_gain=%d, config.threshold_cb=%d, config.threshold_cr=%d\n",
			    config->direction,
			    config->bnr_gain, config->ynr_gain,
			    config->threshold_cb, config->threshold_cr);
}

void
ia_css_ee_debug_dtrace(
    const struct ia_css_ee_config *config,
    unsigned int level)
{
	ia_css_debug_dtrace(level,
			    "config.threshold=%d, config.gain=%d, config.detail_gain=%d\n",
			    config->threshold, config->gain, config->detail_gain);
}

void
ia_css_init_ynr_state(
    void/*struct sh_css_isp_ynr_vmem_state*/ * state,
    size_t size)
{
	memset(state, 0, size);
}
