/*
 * Clock driver for the ARM Integrator/IM-PD1 board
 * Copyright (C) 2012-2013 Linus Walleij
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
	char *vco1name;
	struct clk *vco1clk;
	char *vco2name;
	struct clk *vco2clk;
	struct clk *mmciclk;
	char *uartname;
	struct clk *uartclk;
	char *spiname;
	struct clk *spiclk;
	char *scname;
	struct clk *scclk;
	struct clk_lookup *clks[6];
};

/* One entry for each connected IM-PD1 LM */
static struct impd1_clk impd1_clks[4];

/*
 * There are two VCO's on the IM-PD1
 */

static const struct icst_params impd1_vco1_params = {
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
	.params = &impd1_vco1_params,
	.vco_offset = IMPD1_OSC1,
	.lock_offset = IMPD1_LOCK,
};

static const struct icst_params impd1_vco2_params = {
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

static const struct clk_icst_desc impd1_icst2_desc = {
	.params = &impd1_vco2_params,
	.vco_offset = IMPD1_OSC2,
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

	imc->vco1name = kasprintf(GFP_KERNEL, "lm%x-vco1", id);
	clk = icst_clk_register(NULL, &impd1_icst1_desc, imc->vco1name, base);
	imc->vco1clk = clk;
	imc->clks[0] = clkdev_alloc(clk, NULL, "lm%x:01000", id);

	/* VCO2 is also called "CLK2" */
	imc->vco2name = kasprintf(GFP_KERNEL, "lm%x-vco2", id);
	clk = icst_clk_register(NULL, &impd1_icst2_desc, imc->vco2name, base);
	imc->vco2clk = clk;

	/* MMCI uses CLK2 right off */
	imc->clks[1] = clkdev_alloc(clk, NULL, "lm%x:00700", id);

	/* UART reference clock divides CLK2 by a fixed factor 4 */
	imc->uartname = kasprintf(GFP_KERNEL, "lm%x-uartclk", id);
	clk = clk_register_fixed_factor(NULL, imc->uartname, imc->vco2name,
				   CLK_IGNORE_UNUSED, 1, 4);
	imc->uartclk = clk;
	imc->clks[2] = clkdev_alloc(clk, NULL, "lm%x:00100", id);
	imc->clks[3] = clkdev_alloc(clk, NULL, "lm%x:00200", id);

	/* SPI PL022 clock divides CLK2 by a fixed factor 64 */
	imc->spiname = kasprintf(GFP_KERNEL, "lm%x-spiclk", id);
	clk = clk_register_fixed_factor(NULL, imc->spiname, imc->vco2name,
				   CLK_IGNORE_UNUSED, 1, 64);
	imc->clks[4] = clkdev_alloc(clk, NULL, "lm%x:00300", id);

	/* Smart Card clock divides CLK2 by a fixed factor 4 */
	imc->scname = kasprintf(GFP_KERNEL, "lm%x-scclk", id);
	clk = clk_register_fixed_factor(NULL, imc->scname, imc->vco2name,
				   CLK_IGNORE_UNUSED, 1, 4);
	imc->scclk = clk;
	imc->clks[5] = clkdev_alloc(clk, NULL, "lm%x:00600", id);

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
	clk_unregister(imc->spiclk);
	clk_unregister(imc->uartclk);
	clk_unregister(imc->vco2clk);
	clk_unregister(imc->vco1clk);
	kfree(imc->scname);
	kfree(imc->spiname);
	kfree(imc->uartname);
	kfree(imc->vco2name);
	kfree(imc->vco1name);
}
