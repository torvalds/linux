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

extern const struct clk_ops meson_clk_triphase_ops;

#endif /* __MESON_CLKC_AUDIO_H */
