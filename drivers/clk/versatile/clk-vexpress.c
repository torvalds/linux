/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright (C) 2012 ARM Limited
 */

#include <linux/amba/sp810.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/vexpress.h>

static struct clk *vexpress_sp810_timerclken[4];
static DEFINE_SPINLOCK(vexpress_sp810_lock);

static void __init vexpress_sp810_init(void __iomem *base)
{
	int i;

	if (WARN_ON(!base))
		return;

	for (i = 0; i < ARRAY_SIZE(vexpress_sp810_timerclken); i++) {
		char name[12];
		const char *parents[] = {
			"v2m:refclk32khz", /* REFCLK */
			"v2m:refclk1mhz" /* TIMCLK */
		};

		snprintf(name, ARRAY_SIZE(name), "timerclken%d", i);

		vexpress_sp810_timerclken[i] = clk_register_mux(NULL, name,
				parents, 2, CLK_SET_RATE_NO_REPARENT,
				base + SCCTRL, SCCTRL_TIMERENnSEL_SHIFT(i), 1,
				0, &vexpress_sp810_lock);

		if (WARN_ON(IS_ERR(vexpress_sp810_timerclken[i])))
			break;
	}
}


static const char * const vexpress_clk_24mhz_periphs[] __initconst = {
	"mb:uart0", "mb:uart1", "mb:uart2", "mb:uart3",
	"mb:mmci", "mb:kmi0", "mb:kmi1"
};

void __init vexpress_clk_init(void __iomem *sp810_base)
{
	struct clk *clk;
	int i;

	clk = clk_register_fixed_rate(NULL, "dummy_apb_pclk", NULL,
			CLK_IS_ROOT, 0);
	WARN_ON(clk_register_clkdev(clk, "apb_pclk", NULL));

	clk = clk_register_fixed_rate(NULL, "v2m:clk_24mhz", NULL,
			CLK_IS_ROOT, 24000000);
	for (i = 0; i < ARRAY_SIZE(vexpress_clk_24mhz_periphs); i++)
		WARN_ON(clk_register_clkdev(clk, NULL,
				vexpress_clk_24mhz_periphs[i]));

	clk = clk_register_fixed_rate(NULL, "v2m:refclk32khz", NULL,
			CLK_IS_ROOT, 32768);
	WARN_ON(clk_register_clkdev(clk, NULL, "v2m:wdt"));

	clk = clk_register_fixed_rate(NULL, "v2m:refclk1mhz", NULL,
			CLK_IS_ROOT, 1000000);

	vexpress_sp810_init(sp810_base);

	for (i = 0; i < ARRAY_SIZE(vexpress_sp810_timerclken); i++)
		WARN_ON(clk_set_parent(vexpress_sp810_timerclken[i], clk));

	WARN_ON(clk_register_clkdev(vexpress_sp810_timerclken[0],
				"v2m-timer0", "sp804"));
	WARN_ON(clk_register_clkdev(vexpress_sp810_timerclken[1],
				"v2m-timer1", "sp804"));
}
