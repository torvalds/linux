// SPDX-License-Identifier: GPL-2.0
/* Release Version: irci_stable_candrpv_0415_20150521_0458 */
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

#include "ia_css_hdr.host.h"

void
ia_css_hdr_init_config(
    struct sh_css_isp_hdr_params *to,
    const struct ia_css_hdr_config *from,
    unsigned int size)
{
	int i;
	(void)size;

	for (i = 0; i < HDR_NUM_INPUT_FRAMES - 1; i++) {
		to->irradiance.match_shift[i] = from->irradiance.match_shift[i];
		to->irradiance.match_mul[i]   = from->irradiance.match_mul[i];
		to->irradiance.thr_low[i]     = from->irradiance.thr_low[i];
		to->irradiance.thr_high[i]    = from->irradiance.thr_high[i];
		to->irradiance.thr_coeff[i]   = from->irradiance.thr_coeff[i];
		to->irradiance.thr_shift[i]   = from->irradiance.thr_shift[i];
	}
	to->irradiance.test_irr    = from->irradiance.test_irr;
	to->irradiance.weight_bpp  = from->irradiance.weight_bpp;

	to->deghost.test_deg    = from->deghost.test_deg;
	to->exclusion.test_excl = from->exclusion.test_excl;
}
