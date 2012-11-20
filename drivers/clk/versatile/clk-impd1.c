/*
 * Clock driver for the ARM Integrator/IM-PD1 board
 * Copyright (C) 2012 Linus Walleij
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_data/clk-integrator.h>

#include <mach/impd1.h>

#include "clk-icst.h"

struct impd1_clk {
	struct clk *vcoclk;
	struct clk *uartclk;
	struct clk_lookup *clks[3];
};

static struct impd1_clk impd1_clks[4];

/*
 * There are two VCO's on the IM-PD1 but only one is used by the
 * kernel, that is why we are only implementing the control of
 * IMPD1_OSC1 here.
 */

static const struct icst_params impd1_vco_params = {
	.ref		= 24000000,	/* 24 MHz */
	.vco_max	= ICST525_VCO_MAX_3V,
	.vco_min	= ICST525_VCO_MIN,
	.vd_min		= 12,
	.vd_max		= 519,
	.rd_min		= 3,
	.rd_max		= 120,
	.s2div		= icst525_s2div,
	.idx2s		= icst525_idx2s,
};

static const struct clk_icst_desc impd1_icst1_desc = {
	.params = &impd1_vco_params,
	.vco_offset = IMPD1_OSC1,
	.lock_offset = IMPD1_LOCK,
};

/**
 * integrator_impd1_clk_init() - set up the integrator clock tree
 * @base: base address of the logic module (LM)
 * @id: the ID of this LM
 */
void integrator_impd1_clk_init(void __iomem *base, unsigned int id)
{
	struct impd1_clk *imc;
	struct clk *clk;
	int i;

	if (id > 3) {
		pr_crit("no more than 4 LMs can be attached\n");
		return;
	}
	imc = &impd1_clks[id];

	clk = icst_clk_register(NULL, &impd1_icst1_desc, base);
	imc->vcoclk = clk;
	imc->clks[0] = clkdev_alloc(clk, NULL, "lm%x:01000", id);

	/* UART reference clock */
	clk = clk_register_fixed_rate(NULL, "uartclk", NULL, CLK_IS_ROOT,
				14745600);
	imc->uartclk = clk;
	imc->clks[1] = clkdev_alloc(clk, NULL, "lm%x:00100", id);
	imc->clks[2] = clkdev_alloc(clk, NULL, "lm%x:00200", id);

	for (i = 0; i < ARRAY_SIZE(imc->clks); i++)
		clkdev_add(imc->clks[i]);
}

void integrator_impd1_clk_exit(unsigned int id)
{
	int i;
	struct impd1_clk *imc;

	if (id > 3)
		return;
	imc = &impd1_clks[id];

	for (i = 0; i < ARRAY_SIZE(imc->clks); i++)
		clkdev_drop(imc->clks[i]);
	clk_unregister(imc->uartclk);
	clk_unregister(imc->vcoclk);
}
