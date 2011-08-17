/*
 *  linux/arch/arm/mach-omap2/clock_common_data.c
 *
 *  Copyright (C) 2005-2009 Texas Instruments, Inc.
 *  Copyright (C) 2004-2009 Nokia Corporation
 *
 *  Contacts:
 *  Richard Woodruff <r-woodruff2@ti.com>
 *  Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This file contains clock data that is common to both the OMAP2xxx and
 * OMAP3xxx clock definition files.
 */

#include "clock.h"

/* clksel_rate data common to 24xx/343x */
const struct clksel_rate gpt_32k_rates[] = {
	 { .div = 1, .val = 0, .flags = RATE_IN_24XX | RATE_IN_3XXX },
	 { .div = 0 }
};

const struct clksel_rate gpt_sys_rates[] = {
	 { .div = 1, .val = 1, .flags = RATE_IN_24XX | RATE_IN_3XXX },
	 { .div = 0 }
};

const struct clksel_rate gfx_l3_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX | RATE_IN_3XXX },
	{ .div = 2, .val = 2, .flags = RATE_IN_24XX | RATE_IN_3XXX },
	{ .div = 3, .val = 3, .flags = RATE_IN_243X | RATE_IN_3XXX },
	{ .div = 4, .val = 4, .flags = RATE_IN_243X | RATE_IN_3XXX },
	{ .div = 0 }
};

const struct clksel_rate dsp_ick_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 2, .val = 2, .flags = RATE_IN_24XX },
	{ .div = 3, .val = 3, .flags = RATE_IN_243X },
	{ .div = 0 },
};
