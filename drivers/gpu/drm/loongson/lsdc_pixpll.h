/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#ifndef __LSDC_PIXPLL_H__
#define __LSDC_PIXPLL_H__

#include <drm/drm_device.h>

/*
 * Loongson Pixel PLL hardware structure
 *
 * refclk: reference frequency, 100 MHz from external oscillator
 * outclk: output frequency desired.
 *
 *
 *               L1       Fref                      Fvco     L2
 * refclk   +-----------+      +------------------+      +---------+   outclk
 * ---+---> | Prescaler | ---> | Clock Multiplier | ---> | divider | -------->
 *    |     +-----------+      +------------------+      +---------+     ^
 *    |           ^                      ^                    ^          |
 *    |           |                      |                    |          |
 *    |           |                      |                    |          |
 *    |        div_ref                 loopc               div_out       |
 *    |                                                                  |
 *    +---- bypass (bypass above software configurable clock if set) ----+
 *
 *   outclk = refclk / div_ref * loopc / div_out;
 *
 *   sel_out: PLL clock output selector(enable).
 *
 *   If sel_out == 1, then enable output clock (turn On);
 *   If sel_out == 0, then disable output clock (turn Off);
 *
 * PLL working requirements:
 *
 *  1) 20 MHz <= refclk / div_ref <= 40Mhz
 *  2) 1.2 GHz <= refclk /div_out * loopc <= 3.2 Ghz
 */

struct lsdc_pixpll_parms {
	unsigned int ref_clock;
	unsigned int div_ref;
	unsigned int loopc;
	unsigned int div_out;
};

struct lsdc_pixpll;

struct lsdc_pixpll_funcs {
	int (*setup)(struct lsdc_pixpll * const this);

	int (*compute)(struct lsdc_pixpll * const this,
		       unsigned int clock,
		       struct lsdc_pixpll_parms *pout);

	int (*update)(struct lsdc_pixpll * const this,
		      struct lsdc_pixpll_parms const *pin);

	unsigned int (*get_rate)(struct lsdc_pixpll * const this);

	void (*print)(struct lsdc_pixpll * const this,
		      struct drm_printer *printer);
};

struct lsdc_pixpll {
	const struct lsdc_pixpll_funcs *funcs;

	struct drm_device *ddev;

	/* PLL register offset */
	u32 reg_base;
	/* PLL register size in bytes */
	u32 reg_size;

	void __iomem *mmio;

	struct lsdc_pixpll_parms *priv;
};

int lsdc_pixpll_init(struct lsdc_pixpll * const this,
		     struct drm_device *ddev,
		     unsigned int index);

#endif
