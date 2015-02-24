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


/* clksel_rate blocks shared between OMAP44xx and AM33xx */

const struct clksel_rate div_1_0_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 0 },
};

const struct clksel_rate div3_1to4_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_4430 },
	{ .div = 2, .val = 1, .flags = RATE_IN_4430 },
	{ .div = 4, .val = 2, .flags = RATE_IN_4430 },
	{ .div = 0 },
};

const struct clksel_rate div_1_1_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 0 },
};

const struct clksel_rate div_1_2_rates[] = {
	{ .div = 1, .val = 2, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 0 },
};

const struct clksel_rate div_1_3_rates[] = {
	{ .div = 1, .val = 3, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 0 },
};

const struct clksel_rate div_1_4_rates[] = {
	{ .div = 1, .val = 4, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 0 },
};

const struct clksel_rate div31_1to31_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 2, .val = 2, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 3, .val = 3, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 4, .val = 4, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 5, .val = 5, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 6, .val = 6, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 7, .val = 7, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 8, .val = 8, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 9, .val = 9, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 10, .val = 10, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 11, .val = 11, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 12, .val = 12, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 13, .val = 13, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 14, .val = 14, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 15, .val = 15, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 16, .val = 16, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 17, .val = 17, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 18, .val = 18, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 19, .val = 19, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 20, .val = 20, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 21, .val = 21, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 22, .val = 22, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 23, .val = 23, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 24, .val = 24, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 25, .val = 25, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 26, .val = 26, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 27, .val = 27, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 28, .val = 28, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 29, .val = 29, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 30, .val = 30, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 31, .val = 31, .flags = RATE_IN_4430 | RATE_IN_AM33XX },
	{ .div = 0 },
};
