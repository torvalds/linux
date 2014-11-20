/*
 * Clock driver for the ARM RealView boards
 * Copyright (C) 2012 Linus Walleij
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk-provider.h>

#include <mach/hardware.h>
#include <mach/platform.h>

#include "clk-icst.h"

/*
 * Implementation of the ARM RealView clock trees.
 */

static const struct icst_params realview_oscvco_params = {
	.ref		= 24000000,
	.vco_max	= ICST307_VCO_MAX,
	.vco_min	= ICST307_VCO_MIN,
	.vd_min		= 4 + 8,
	.vd_max		= 511 + 8,
	.rd_min		= 1 + 2,
	.rd_max		= 127 + 2,
	.s2div		= icst307_s2div,
	.idx2s		= icst307_idx2s,
};

static const struct clk_icst_desc __initdata realview_osc0_desc = {
	.params = &realview_oscvco_params,
	.vco_offset = REALVIEW_SYS_OSC0_OFFSET,
	.lock_offset = REALVIEW_SYS_LOCK_OFFSET,
};

static const struct clk_icst_desc __initdata realview_osc4_desc = {
	.params = &realview_oscvco_params,
	.vco_offset = REALVIEW_SYS_OSC4_OFFSET,
	.lock_offset = REALVIEW_SYS_LOCK_OFFSET,
};

/*
 * realview_clk_init() - set up the RealView clock tree
 */
void __init realview_clk_init(void __iomem *sysbase, bool is_pb1176)
{
	struct clk *clk;

	/* APB clock dummy */
	clk = clk_register_fixed_rate(NULL, "apb_pclk", NULL, CLK_IS_ROOT, 0);
	clk_register_clkdev(clk, "apb_pclk", NULL);

	/* 24 MHz clock */
	clk = clk_register_fixed_rate(NULL, "clk24mhz", NULL, CLK_IS_ROOT,
				24000000);
	clk_register_clkdev(clk, NULL, "dev:uart0");
	clk_register_clkdev(clk, NULL, "dev:uart1");
	clk_register_clkdev(clk, NULL, "dev:uart2");
	clk_register_clkdev(clk, NULL, "fpga:kmi0");
	clk_register_clkdev(clk, NULL, "fpga:kmi1");
	clk_register_clkdev(clk, NULL, "fpga:mmc0");
	clk_register_clkdev(clk, NULL, "dev:ssp0");
	if (is_pb1176) {
		/*
		 * UART3 is on the dev chip in PB1176
		 * UART4 only exists in PB1176
		 */
		clk_register_clkdev(clk, NULL, "dev:uart3");
		clk_register_clkdev(clk, NULL, "dev:uart4");
	} else
		clk_register_clkdev(clk, NULL, "fpga:uart3");


	/* 1 MHz clock */
	clk = clk_register_fixed_rate(NULL, "clk1mhz", NULL, CLK_IS_ROOT,
				      1000000);
	clk_register_clkdev(clk, NULL, "sp804");

	/* ICST VCO clock */
	if (is_pb1176)
		clk = icst_clk_register(NULL, &realview_osc0_desc,
					"osc0", NULL, sysbase);
	else
		clk = icst_clk_register(NULL, &realview_osc4_desc,
					"osc4", NULL, sysbase);

	clk_register_clkdev(clk, NULL, "dev:clcd");
	clk_register_clkdev(clk, NULL, "issp:clcd");
}
