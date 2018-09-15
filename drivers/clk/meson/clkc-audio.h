/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#ifndef __MESON_CLKC_AUDIO_H
#define __MESON_CLKC_AUDIO_H

#include "clkc.h"

struct meson_clk_triphase_data {
	struct parm ph0;
	struct parm ph1;
	struct parm ph2;
};

struct meson_sclk_div_data {
	struct parm div;
	struct parm hi;
	unsigned int cached_div;
	struct clk_duty cached_duty;
};

extern const struct clk_ops meson_clk_triphase_ops;
extern const struct clk_ops meson_sclk_div_ops;

#endif /* __MESON_CLKC_AUDIO_H */
