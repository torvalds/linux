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

#include "ia_css_ynr2.host.h"

const struct ia_css_ynr_config default_ynr_config = {
	0,
	0,
	0,
	0,
};

const struct ia_css_fc_config default_fc_config = {
	1,
	0,		/* 0 -> ineffective */
	0,		/* 0 -> ineffective */
	0,		/* 0 -> ineffective */
	0,		/* 0 -> ineffective */
	(1 << (ISP_VEC_ELEMBITS - 2)),		/* 0.5 */
	(1 << (ISP_VEC_ELEMBITS - 2)),		/* 0.5 */
	(1 << (ISP_VEC_ELEMBITS - 2)),		/* 0.5 */
	(1 << (ISP_VEC_ELEMBITS - 2)),		/* 0.5 */
	(1 << (ISP_VEC_ELEMBITS - 1)) - 1,	/* 1 */
	(1 << (ISP_VEC_ELEMBITS - 1)) - 1,	/* 1 */
	(int16_t)- (1 << (ISP_VEC_ELEMBITS - 1)),	/* -1 */
	(int16_t)- (1 << (ISP_VEC_ELEMBITS - 1)),	/* -1 */
};

void
ia_css_ynr_encode(
	struct sh_css_isp_yee2_params *to,
	const struct ia_css_ynr_config *from,
	unsigned size)
{
	(void)size;
	to->edge_sense_gain_0   = from->edge_sense_gain_0;
	to->edge_sense_gain_1   = from->edge_sense_gain_1;
	to->corner_sense_gain_0 = from->corner_sense_gain_0;
	to->corner_sense_gain_1 = from->corner_sense_gain_1;
}

void
ia_css_fc_encode(
	struct sh_css_isp_fc_params *to,
	const struct ia_css_fc_config *from,
	unsigned size)
{
	(void)size;
	to->gain_exp   = from->gain_exp;

	to->coring_pos_0 = from->coring_pos_0;
	to->coring_pos_1 = from->coring_pos_1;
	to->coring_neg_0 = from->coring_neg_0;
	to->coring_neg_1 = from->coring_neg_1;

	to->gain_pos_0 = from->gain_pos_0;
	to->gain_pos_1 = from->gain_pos_1;
	to->gain_neg_0 = from->gain_neg_0;
	to->gain_neg_1 = from->gain_neg_1;

	to->crop_pos_0 = from->crop_pos_0;
	to->crop_pos_1 = from->crop_pos_1;
	to->crop_neg_0 = from->crop_neg_0;
	to->crop_neg_1 = from->crop_neg_1;
}

void
ia_css_ynr_dump(
	const struct sh_css_isp_yee2_params *yee2,
	unsigned level);

void
ia_css_fc_dump(
	const struct sh_css_isp_fc_params *fc,
	unsigned level);

void
ia_css_fc_debug_dtrace(
	const struct ia_css_fc_config *config,
	unsigned level)
{
	ia_css_debug_dtrace(level,
		"config.gain_exp=%d, "
		"config.coring_pos_0=%d, config.coring_pos_1=%d, "
		"config.coring_neg_0=%d, config.coring_neg_1=%d, "
		"config.gain_pos_0=%d, config.gain_pos_1=%d, "
		"config.gain_neg_0=%d, config.gain_neg_1=%d, "
		"config.crop_pos_0=%d, config.crop_pos_1=%d, "
		"config.crop_neg_0=%d, config.crop_neg_1=%d\n",
		config->gain_exp,
		config->coring_pos_0, config->coring_pos_1,
		config->coring_neg_0, config->coring_neg_1,
		config->gain_pos_0, config->gain_pos_1,
		config->gain_neg_0, config->gain_neg_1,
		config->crop_pos_0, config->crop_pos_1,
		config->crop_neg_0, config->crop_neg_1);
}

void
ia_css_ynr_debug_dtrace(
	const struct ia_css_ynr_config *config,
	unsigned level)
{
	ia_css_debug_dtrace(level,
		"config.edge_sense_gain_0=%d, config.edge_sense_gain_1=%d, "
		"config.corner_sense_gain_0=%d, config.corner_sense_gain_1=%d\n",
		config->edge_sense_gain_0, config->edge_sense_gain_1,
		config->corner_sense_gain_0, config->corner_sense_gain_1);
}
