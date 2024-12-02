/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.
 * Copyright 2011 Florian Tobias Schandinat <FlorianSchandinat@gmx.de>
 */
/*
 * clock and PLL management functions
 */

#ifndef __VIA_CLOCK_H__
#define __VIA_CLOCK_H__

#include <linux/types.h>

enum via_clksrc {
	VIA_CLKSRC_X1 = 0,
	VIA_CLKSRC_TVX1,
	VIA_CLKSRC_TVPLL,
	VIA_CLKSRC_DVP1TVCLKR,
	VIA_CLKSRC_CAP0,
	VIA_CLKSRC_CAP1,
};

struct via_pll_config {
	u16 multiplier;
	u8 divisor;
	u8 rshift;
};

struct via_clock {
	void (*set_primary_clock_state)(u8 state);
	void (*set_primary_clock_source)(enum via_clksrc src, bool use_pll);
	void (*set_primary_pll_state)(u8 state);
	void (*set_primary_pll)(struct via_pll_config config);

	void (*set_secondary_clock_state)(u8 state);
	void (*set_secondary_clock_source)(enum via_clksrc src, bool use_pll);
	void (*set_secondary_pll_state)(u8 state);
	void (*set_secondary_pll)(struct via_pll_config config);

	void (*set_engine_pll_state)(u8 state);
	void (*set_engine_pll)(struct via_pll_config config);
};


static inline u32 get_pll_internal_frequency(u32 ref_freq,
	struct via_pll_config pll)
{
	return ref_freq / pll.divisor * pll.multiplier;
}

static inline u32 get_pll_output_frequency(u32 ref_freq,
	struct via_pll_config pll)
{
	return get_pll_internal_frequency(ref_freq, pll) >> pll.rshift;
}

void via_clock_init(struct via_clock *clock, int gfx_chip);

#endif /* __VIA_CLOCK_H__ */
