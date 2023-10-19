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
#ifndef IA_CSS_NO_DEBUG
/* FIXME: See BZ 4427 */
#include "ia_css_debug.h"
#endif

#include "ia_css_csc.host.h"

const struct ia_css_cc_config default_cc_config = {
	8,
	{255, 29, 120, 0, -374, -342, 0, -672, 301},
};

void
ia_css_encode_cc(
    struct sh_css_isp_csc_params *to,
    const struct ia_css_cc_config *from,
    unsigned int size)
{
	(void)size;
#ifndef IA_CSS_NO_DEBUG
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE, "ia_css_encode_cc() enter:\n");
#endif

	to->m_shift    = (int16_t)from->fraction_bits;
	to->m00 = (int16_t)from->matrix[0];
	to->m01 = (int16_t)from->matrix[1];
	to->m02 = (int16_t)from->matrix[2];
	to->m10 = (int16_t)from->matrix[3];
	to->m11 = (int16_t)from->matrix[4];
	to->m12 = (int16_t)from->matrix[5];
	to->m20 = (int16_t)from->matrix[6];
	to->m21 = (int16_t)from->matrix[7];
	to->m22 = (int16_t)from->matrix[8];

#ifndef IA_CSS_NO_DEBUG
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE, "ia_css_encode_cc() leave:\n");
#endif
}

void
ia_css_csc_encode(
    struct sh_css_isp_csc_params *to,
    const struct ia_css_cc_config *from,
    unsigned int size)
{
	ia_css_encode_cc(to, from, size);
}

#ifndef IA_CSS_NO_DEBUG
void
ia_css_cc_dump(
    const struct sh_css_isp_csc_params *csc,
    unsigned int level,
    const char *name)
{
	if (!csc) return;
	ia_css_debug_dtrace(level, "%s\n", name);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "m_shift",
			    csc->m_shift);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "m00",
			    csc->m00);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "m01",
			    csc->m01);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "m02",
			    csc->m02);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "m10",
			    csc->m10);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "m11",
			    csc->m11);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "m12",
			    csc->m12);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "m20",
			    csc->m20);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "m21",
			    csc->m21);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "m22",
			    csc->m22);
}

void
ia_css_csc_dump(
    const struct sh_css_isp_csc_params *csc,
    unsigned int level)
{
	ia_css_cc_dump(csc, level, "Color Space Conversion");
}

void
ia_css_cc_config_debug_dtrace(
    const struct ia_css_cc_config *config,
    unsigned int level)
{
	ia_css_debug_dtrace(level,
			    "config.m[0]=%d, config.m[1]=%d, config.m[2]=%d, config.m[3]=%d, config.m[4]=%d, config.m[5]=%d, config.m[6]=%d, config.m[7]=%d, config.m[8]=%d\n",
			    config->matrix[0],
			    config->matrix[1], config->matrix[2],
			    config->matrix[3], config->matrix[4],
			    config->matrix[5], config->matrix[6],
			    config->matrix[7], config->matrix[8]);
}
#endif
