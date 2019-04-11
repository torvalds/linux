/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#ifndef __MESON_CLK_MPLL_H
#define __MESON_CLK_MPLL_H

#include <linux/clk-provider.h>
#include <linux/spinlock.h>

#include "parm.h"

struct meson_clk_mpll_data {
	struct parm sdm;
	struct parm sdm_en;
	struct parm n2;
	struct parm ssen;
	struct parm misc;
	spinlock_t *lock;
	u8 flags;
};

#define CLK_MESON_MPLL_ROUND_CLOSEST	BIT(0)

extern const struct clk_ops meson_clk_mpll_ro_ops;
extern const struct clk_ops meson_clk_mpll_ops;

#endif /* __MESON_CLK_MPLL_H */
