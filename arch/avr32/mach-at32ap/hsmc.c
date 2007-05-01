/*
 * Static Memory Controller for AT32 chips
 *
 * Copyright (C) 2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define DEBUG
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include <asm/arch/smc.h>

#include "hsmc.h"

#define NR_CHIP_SELECTS 6

struct hsmc {
	void __iomem *regs;
	struct clk *pclk;
	struct clk *mck;
};

static struct hsmc *hsmc;

int smc_set_configuration(int cs, const struct smc_config *config)
{
	unsigned long mul;
	unsigned long offset;
	u32 setup, pulse, cycle, mode;

	if (!hsmc)
		return -ENODEV;
	if (cs >= NR_CHIP_SELECTS)
		return -EINVAL;

	/*
	 * cycles = x / T = x * f
	 *   = ((x * 1000000000) * ((f * 65536) / 1000000000)) / 65536
	 *   = ((x * 1000000000) * (((f / 10000) * 65536) / 100000)) / 65536
	 */
	mul = (clk_get_rate(hsmc->mck) / 10000) << 16;
	mul /= 100000;

#define ns2cyc(x) ((((x) * mul) + 65535) >> 16)

	setup = (HSMC_BF(NWE_SETUP, ns2cyc(config->nwe_setup))
		 | HSMC_BF(NCS_WR_SETUP, ns2cyc(config->ncs_write_setup))
		 | HSMC_BF(NRD_SETUP, ns2cyc(config->nrd_setup))
		 | HSMC_BF(NCS_RD_SETUP, ns2cyc(config->ncs_read_setup)));
	pulse = (HSMC_BF(NWE_PULSE, ns2cyc(config->nwe_pulse))
		 | HSMC_BF(NCS_WR_PULSE, ns2cyc(config->ncs_write_pulse))
		 | HSMC_BF(NRD_PULSE, ns2cyc(config->nrd_pulse))
		 | HSMC_BF(NCS_RD_PULSE, ns2cyc(config->ncs_read_pulse)));
	cycle = (HSMC_BF(NWE_CYCLE, ns2cyc(config->write_cycle))
		 | HSMC_BF(NRD_CYCLE, ns2cyc(config->read_cycle)));

	switch (config->bus_width) {
	case 1:
		mode = HSMC_BF(DBW, HSMC_DBW_8_BITS);
		break;
	case 2:
		mode = HSMC_BF(DBW, HSMC_DBW_16_BITS);
		break;
	case 4:
		mode = HSMC_BF(DBW, HSMC_DBW_32_BITS);
		break;
	default:
		return -EINVAL;
	}

	switch (config->nwait_mode) {
	case 0:
		mode |= HSMC_BF(EXNW_MODE, HSMC_EXNW_MODE_DISABLED);
		break;
	case 1:
		mode |= HSMC_BF(EXNW_MODE, HSMC_EXNW_MODE_RESERVED);
		break;
	case 2:
		mode |= HSMC_BF(EXNW_MODE, HSMC_EXNW_MODE_FROZEN);
		break;
	case 3:
		mode |= HSMC_BF(EXNW_MODE, HSMC_EXNW_MODE_READY);
		break;
	default:
		return -EINVAL;
	}

	if (config->tdf_cycles) {
		mode |= HSMC_BF(TDF_CYCLES, config->tdf_cycles);
	}

	if (config->nrd_controlled)
		mode |= HSMC_BIT(READ_MODE);
	if (config->nwe_controlled)
		mode |= HSMC_BIT(WRITE_MODE);
	if (config->byte_write)
		mode |= HSMC_BIT(BAT);
	if (config->tdf_mode)
		mode |= HSMC_BIT(TDF_MODE);

	pr_debug("smc cs%d: setup/%08x pulse/%08x cycle/%08x mode/%08x\n",
		 cs, setup, pulse, cycle, mode);

	offset = cs * 0x10;
	hsmc_writel(hsmc, SETUP0 + offset, setup);
	hsmc_writel(hsmc, PULSE0 + offset, pulse);
	hsmc_writel(hsmc, CYCLE0 + offset, cycle);
	hsmc_writel(hsmc, MODE0 + offset, mode);
	hsmc_readl(hsmc, MODE0); /* I/O barrier */

	return 0;
}
EXPORT_SYMBOL(smc_set_configuration);

static int hsmc_probe(struct platform_device *pdev)
{
	struct resource *regs;
	struct clk *pclk, *mck;
	int ret;

	if (hsmc)
		return -EBUSY;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs)
		return -ENXIO;
	pclk = clk_get(&pdev->dev, "pclk");
	if (IS_ERR(pclk))
		return PTR_ERR(pclk);
	mck = clk_get(&pdev->dev, "mck");
	if (IS_ERR(mck)) {
		ret = PTR_ERR(mck);
		goto out_put_pclk;
	}

	ret = -ENOMEM;
	hsmc = kzalloc(sizeof(struct hsmc), GFP_KERNEL);
	if (!hsmc)
		goto out_put_clocks;

	clk_enable(pclk);
	clk_enable(mck);

	hsmc->pclk = pclk;
	hsmc->mck = mck;
	hsmc->regs = ioremap(regs->start, regs->end - regs->start + 1);
	if (!hsmc->regs)
		goto out_disable_clocks;

	dev_info(&pdev->dev, "Atmel Static Memory Controller at 0x%08lx\n",
		 (unsigned long)regs->start);

	platform_set_drvdata(pdev, hsmc);

	return 0;

out_disable_clocks:
	clk_disable(mck);
	clk_disable(pclk);
	kfree(hsmc);
out_put_clocks:
	clk_put(mck);
out_put_pclk:
	clk_put(pclk);
	hsmc = NULL;
	return ret;
}

static struct platform_driver hsmc_driver = {
	.probe		= hsmc_probe,
	.driver		= {
		.name	= "smc",
	},
};

static int __init hsmc_init(void)
{
	return platform_driver_register(&hsmc_driver);
}
arch_initcall(hsmc_init);
