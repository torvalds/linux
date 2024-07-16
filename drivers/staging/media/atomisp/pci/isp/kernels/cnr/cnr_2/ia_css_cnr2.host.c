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

#include "ia_css_cnr2.host.h"

const struct ia_css_cnr_config default_cnr_config = {
	0,
	0,
	100,
	100,
	100,
	50,
	50,
	50
};

void
ia_css_cnr_encode(
    struct sh_css_isp_cnr_params *to,
    const struct ia_css_cnr_config *from,
    unsigned int size)
{
	(void)size;
	to->coring_u = from->coring_u;
	to->coring_v = from->coring_v;
	to->sense_gain_vy = from->sense_gain_vy;
	to->sense_gain_vu = from->sense_gain_vu;
	to->sense_gain_vv = from->sense_gain_vv;
	to->sense_gain_hy = from->sense_gain_hy;
	to->sense_gain_hu = from->sense_gain_hu;
	to->sense_gain_hv = from->sense_gain_hv;
}

void
ia_css_cnr_dump(
    const struct sh_css_isp_cnr_params *cnr,
    unsigned int level);

void
ia_css_cnr_debug_dtrace(
    const struct ia_css_cnr_config *config,
    unsigned int level)
{
	ia_css_debug_dtrace(level,
			    "config.coring_u=%d, config.coring_v=%d, config.sense_gain_vy=%d, config.sense_gain_hy=%d, config.sense_gain_vu=%d, config.sense_gain_hu=%d, config.sense_gain_vv=%d, config.sense_gain_hv=%d\n",
			    config->coring_u, config->coring_v,
			    config->sense_gain_vy, config->sense_gain_hy,
			    config->sense_gain_vu, config->sense_gain_hu,
			    config->sense_gain_vv, config->sense_gain_hv);
}

void
ia_css_init_cnr2_state(
    void/*struct sh_css_isp_cnr_vmem_state*/ * state,
    size_t size)
{
	memset(state, 0, size);
}
