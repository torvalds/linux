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
#include "isp.h"

#include "ia_css_ob.host.h"

const struct ia_css_ob_config default_ob_config = {
	IA_CSS_OB_MODE_NONE,
	0,
	0,
	0,
	0,
	0,
	0
};

/* TODO: include ob.isp.h to get isp knowledge and
   add assert on platform restrictions */

void
ia_css_ob_configure(
    struct sh_css_isp_ob_stream_config *config,
    unsigned int isp_pipe_version,
    unsigned int raw_bit_depth)
{
	config->isp_pipe_version = isp_pipe_version;
	config->raw_bit_depth    = raw_bit_depth;
}

void
ia_css_ob_encode(
    struct sh_css_isp_ob_params *to,
    const struct ia_css_ob_config *from,
    const struct sh_css_isp_ob_stream_config *config,
    unsigned int size)
{
	unsigned int ob_bit_depth
	    = config->isp_pipe_version == 2 ? SH_CSS_BAYER_BITS : config->raw_bit_depth;
	unsigned int scale = 16 - ob_bit_depth;

	(void)size;
	switch (from->mode) {
	case IA_CSS_OB_MODE_FIXED:
		to->blacklevel_gr = from->level_gr >> scale;
		to->blacklevel_r  = from->level_r  >> scale;
		to->blacklevel_b  = from->level_b  >> scale;
		to->blacklevel_gb = from->level_gb >> scale;
		to->area_start_bq = 0;
		to->area_length_bq = 0;
		to->area_length_bq_inverse = 0;
		break;
	case IA_CSS_OB_MODE_RASTER:
		to->blacklevel_gr = 0;
		to->blacklevel_r = 0;
		to->blacklevel_b = 0;
		to->blacklevel_gb = 0;
		to->area_start_bq = from->start_position;
		to->area_length_bq =
		    (from->end_position - from->start_position) + 1;
		to->area_length_bq_inverse = AREA_LENGTH_UNIT / to->area_length_bq;
		break;
	default:
		to->blacklevel_gr = 0;
		to->blacklevel_r = 0;
		to->blacklevel_b = 0;
		to->blacklevel_gb = 0;
		to->area_start_bq = 0;
		to->area_length_bq = 0;
		to->area_length_bq_inverse = 0;
		break;
	}
}

void
ia_css_ob_vmem_encode(
    struct sh_css_isp_ob_vmem_params *to,
    const struct ia_css_ob_config *from,
    const struct sh_css_isp_ob_stream_config *config,
    unsigned int size)
{
	struct sh_css_isp_ob_params tmp;
	struct sh_css_isp_ob_params *ob = &tmp;

	(void)size;
	ia_css_ob_encode(&tmp, from, config, sizeof(tmp));

	{
		unsigned int i;
		unsigned int sp_obarea_start_bq  = ob->area_start_bq;
		unsigned int sp_obarea_length_bq = ob->area_length_bq;
		unsigned int low = sp_obarea_start_bq;
		unsigned int high = low + sp_obarea_length_bq;
		u16 all_ones = ~0;

		for (i = 0; i < OBAREA_MASK_SIZE; i++) {
			if (i >= low && i < high)
				to->vmask[i / ISP_VEC_NELEMS][i % ISP_VEC_NELEMS] = all_ones;
			else
				to->vmask[i / ISP_VEC_NELEMS][i % ISP_VEC_NELEMS] = 0;
		}
	}
}

void
ia_css_ob_dump(
    const struct sh_css_isp_ob_params *ob,
    unsigned int level)
{
	if (!ob) return;
	ia_css_debug_dtrace(level, "Optical Black:\n");
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "ob_blacklevel_gr", ob->blacklevel_gr);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "ob_blacklevel_r", ob->blacklevel_r);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "ob_blacklevel_b", ob->blacklevel_b);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "ob_blacklevel_gb", ob->blacklevel_gb);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "obarea_start_bq", ob->area_start_bq);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "obarea_length_bq", ob->area_length_bq);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "obarea_length_bq_inverse",
			    ob->area_length_bq_inverse);
}

void
ia_css_ob_debug_dtrace(
    const struct ia_css_ob_config *config,
    unsigned int level)
{
	ia_css_debug_dtrace(level,
			    "config.mode=%d, config.level_gr=%d, config.level_r=%d, config.level_b=%d,  config.level_gb=%d, config.start_position=%d, config.end_position=%d\n",
			    config->mode,
			    config->level_gr, config->level_r,
			    config->level_b, config->level_gb,
			    config->start_position, config->end_position);
}
