/*
 * Device Tree support for Mediatek SoCs
 *
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Matthias Brugger <matthias.bgg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/init.h>
#include <asm/mach/arch.h>
#include <linux/of.h>
#include <linux/clk-provider.h>
#include <linux/clocksource.h>


#define GPT6_CON_MT65xx 0x10008060
#define GPT_ENABLE      0x31

static void __init mediatek_timer_init(void)
{
	void __iomem *gpt_base;

	if (of_machine_is_compatible("mediatek,mt6589") ||
	    of_machine_is_compatible("mediatek,mt8135") ||
	    of_machine_is_compatible("mediatek,mt8127")) {
		/* turn on GPT6 which ungates arch timer clocks */
		gpt_base = ioremap(GPT6_CON_MT65xx, 0x04);

		/* enable clock and set to free-run */
		writel(GPT_ENABLE, gpt_base);
		iounmap(gpt_base);
	}

	of_clk_init(NULL);
	clocksource_probe();
};

static const char * const mediatek_board_dt_compat[] = {
	"mediatek,mt2701",
	"mediatek,mt6589",
	"mediatek,mt6592",
	"mediatek,mt7623",
	"mediatek,mt8127",
	"mediatek,mt8135",
	NULL,
};

DT_MACHINE_START(MEDIATEK_DT, "Mediatek Cortex-A7 (Device Tree)")
	.dt_compat	= mediatek_board_dt_compat,
	.init_time	= mediatek_timer_init,
MACHINE_END
