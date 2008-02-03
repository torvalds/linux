/*
 * Static Memory Controller for AT32 chips
 *
 * Copyright (C) 2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
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

void smc_set_timing(struct smc_config *config,
		    const struct smc_timing *timing)
{
	int recover;
	int cycle;

	unsigned long mul;

	/* Reset all SMC timings */
	config->ncs_read_setup	= 0;
	config->nrd_setup	= 0;
	config->ncs_write_setup	= 0;
	config->nwe_setup	= 0;
	config->ncs_read_pulse	= 0;
	config->nrd_pulse	= 0;
	config->ncs_write_pulse	= 0;
	config->nwe_pulse	= 0;
	config->read_cycle	= 0;
	config->write_cycle	= 0;

	/*
	 * cycles = x / T = x * f
	 *   = ((x * 1000000000) * ((f * 65536) / 1000000000)) / 65536
	 *   = ((x * 1000000000) * (((f / 10000) * 65536) / 100000)) / 65536
	 */
	mul = (clk_get_rate(hsmc->mck) / 10000) << 16;
	mul /= 100000;

#define ns2cyc(x) ((((x) * mul) + 65535) >> 16)

	if (timing->ncs_read_setup > 0)
		config->ncs_read_setup = ns2cyc(timing->ncs_read_setup);

	if (timing->nrd_setup > 0)
		config->nrd_setup = ns2cyc(timing->nrd_setup);

	if (timing->ncs_write_setup > 0)
		config->ncs_write_setup = ns2cyc(timing->ncs_write_setup);

	if (timing->nwe_setup > 0)
		config->nwe_setup = ns2cyc(timing->nwe_setup);

	if (timing->ncs_read_pulse > 0)
		config->ncs_read_pulse = ns2cyc(timing->ncs_read_pulse);

	if (timing->nrd_pulse > 0)
		config->nrd_pulse = ns2cyc(timing->nrd_pulse);

	if (timing->ncs_write_pulse > 0)
		config->ncs_write_pulse = ns2cyc(timing->ncs_write_pulse);

	if (timing->nwe_pulse > 0)
		config->nwe_pulse = ns2cyc(timing->nwe_pulse);

	if (timing->read_cycle > 0)
		config->read_cycle = ns2cyc(timing->read_cycle);

	if (timing->write_cycle > 0)
		config->write_cycle = ns2cyc(timing->write_cycle);

	/* Extend read cycle in needed */
	if (timing->ncs_read_recover > 0)
		recover = ns2cyc(timing->ncs_read_recover);
	else
		recover = 1;

	cycle = config->ncs_read_setup + config->ncs_read_pulse + recover;

	if (config->read_cycle < cycle)
		config->read_cycle = cycle;

	/* Extend read cycle in needed */
	if (timing->nrd_recover > 0)
		recover = ns2cyc(timing->nrd_recover);
	else
		recover = 1;

	cycle = config->nrd_setup + config->nrd_pulse + recover;

	if (config->read_cycle < cycle)
		config->read_cycle = cycle;

	/* Extend write cycle in needed */
	if (timing->ncs_write_recover > 0)
		recover = ns2cyc(timing->ncs_write_recover);
	else
		recover = 1;

	cycle = config->ncs_write_setup + config->ncs_write_pulse + recover;

	if (config->write_cycle < cycle)
		config->write_cycle = cycle;

	/* Extend write cycle in needed */
	if (timing->nwe_recover > 0)
		recover = ns2cyc(timing->nwe_recover);
	else
		recover = 1;

	cycle = config->nwe_setup + config->nwe_pulse + recover;

	if (config->write_cycle < cycle)
		config->write_cycle = cycle;
}
EXPORT_SYMBOL(smc_set_timing);

int smc_set_configuration(int cs, const struct smc_config *config)
{
	unsigned long offset;
	u32 setup, pulse, cycle, mode;

	if (!hsmc)
		return -ENODEV;
	if (cs >= NR_CHIP_SELECTS)
		return -EINVAL;

	setup = (HSMC_BF(NWE_SETUP, config->nwe_setup)
		 | HSMC_BF(NCS_WR_SETUP, config->ncs_write_setup)
		 | HSMC_BF(NRD_SETUP, config->nrd_setup)
		 | HSMC_BF(NCS_RD_SETUP, config->ncs_read_setup));
	pulse = (HSMC_BF(NWE_PULSE, config->nwe_pulse)
		 | HSMC_BF(NCS_WR_PULSE, config->ncs_write_pulse)
		 | HSMC_BF(NRD_PULSE, config->nrd_pulse)
		 | HSMC_BF(NCS_RD_PULSE, config->ncs_read_pulse));
	cycle = (HSMC_BF(NWE_CYCLE, config->write_cycle)
		 | HSMC_BF(NRD_CYCLE, config->read_cycle));

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
