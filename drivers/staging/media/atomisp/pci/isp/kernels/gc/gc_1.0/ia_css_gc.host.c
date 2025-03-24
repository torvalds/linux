// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#include "ia_css_types.h"
#include "sh_css_defs.h"
#ifndef IA_CSS_NO_DEBUG
/* FIXME: See BZ 4427 */
#include "ia_css_debug.h"
#endif
#include "sh_css_frac.h"
#include "vamem.h"

#include "ia_css_gc.host.h"

const struct ia_css_gc_config default_gc_config = {
	0,
	0
};

const struct ia_css_ce_config default_ce_config = {
	0,
	255
};

void
ia_css_gc_encode(
    struct sh_css_isp_gc_params *to,
    const struct ia_css_gc_config *from,
    unsigned int size)
{
	(void)size;
	to->gain_k1 =
	    uDIGIT_FITTING((int)from->gain_k1, 16,
			   IA_CSS_GAMMA_GAIN_K_SHIFT);
	to->gain_k2 =
	    uDIGIT_FITTING((int)from->gain_k2, 16,
			   IA_CSS_GAMMA_GAIN_K_SHIFT);
}

void
ia_css_ce_encode(
    struct sh_css_isp_ce_params *to,
    const struct ia_css_ce_config *from,
    unsigned int size)
{
	(void)size;
	to->uv_level_min = from->uv_level_min;
	to->uv_level_max = from->uv_level_max;
}

void
ia_css_gc_vamem_encode(
    struct sh_css_isp_gc_vamem_params *to,
    const struct ia_css_gamma_table *from,
    unsigned int size)
{
	(void)size;
	memcpy(&to->gc,  &from->data, sizeof(to->gc));
}

#ifndef IA_CSS_NO_DEBUG
void
ia_css_gc_dump(
    const struct sh_css_isp_gc_params *gc,
    unsigned int level)
{
	if (!gc) return;
	ia_css_debug_dtrace(level, "Gamma Correction:\n");
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "gamma_gain_k1", gc->gain_k1);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "gamma_gain_k2", gc->gain_k2);
}

void
ia_css_ce_dump(
    const struct sh_css_isp_ce_params *ce,
    unsigned int level)
{
	ia_css_debug_dtrace(level, "Chroma Enhancement:\n");
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "ce_uv_level_min", ce->uv_level_min);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n",
			    "ce_uv_level_max", ce->uv_level_max);
}

void
ia_css_gc_debug_dtrace(
    const struct ia_css_gc_config *config,
    unsigned int level)
{
	ia_css_debug_dtrace(level,
			    "config.gain_k1=%d, config.gain_k2=%d\n",
			    config->gain_k1, config->gain_k2);
}

void
ia_css_ce_debug_dtrace(
    const struct ia_css_ce_config *config,
    unsigned int level)
{
	ia_css_debug_dtrace(level,
			    "config.uv_level_min=%d, config.uv_level_max=%d\n",
			    config->uv_level_min, config->uv_level_max);
}
#endif
