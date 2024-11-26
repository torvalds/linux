// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#include "ia_css_dpc2.host.h"
#include "assert_support.h"

void
ia_css_dpc2_encode(
    struct ia_css_isp_dpc2_params *to,
    const struct ia_css_dpc2_config *from,
    size_t size)
{
	(void)size;

	assert((from->metric1 >= 0) && (from->metric1 <= METRIC1_ONE_FP));
	assert((from->metric3 >= 0) && (from->metric3 <= METRIC3_ONE_FP));
	assert((from->metric2 >= METRIC2_ONE_FP) &&
	       (from->metric2 < 256 * METRIC2_ONE_FP));
	assert((from->wb_gain_gr > 0) && (from->wb_gain_gr < 16 * WBGAIN_ONE_FP));
	assert((from->wb_gain_r  > 0) && (from->wb_gain_r  < 16 * WBGAIN_ONE_FP));
	assert((from->wb_gain_b  > 0) && (from->wb_gain_b  < 16 * WBGAIN_ONE_FP));
	assert((from->wb_gain_gb > 0) && (from->wb_gain_gb < 16 * WBGAIN_ONE_FP));

	to->metric1 = from->metric1;
	to->metric2 = from->metric2;
	to->metric3 = from->metric3;

	to->wb_gain_gr = from->wb_gain_gr;
	to->wb_gain_r  = from->wb_gain_r;
	to->wb_gain_b  = from->wb_gain_b;
	to->wb_gain_gb = from->wb_gain_gb;
}

/* TODO: AM: This needs a proper implementation. */
void
ia_css_init_dpc2_state(
    void *state,
    size_t size)
{
	(void)state;
	(void)size;
}

#ifndef IA_CSS_NO_DEBUG
/* TODO: AM: This needs a proper implementation. */
void
ia_css_dpc2_debug_dtrace(
    const struct ia_css_dpc2_config *config,
    unsigned int level)
{
	(void)config;
	(void)level;
}
#endif
