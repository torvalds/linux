/*
 * linux/arch/arm/mach-at91/sam9_smc.c
 *
 * Copyright (C) 2008 Andrew Victor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <mach/at91sam9_smc.h>

#include "sam9_smc.h"


#define AT91_SMC_CS(id, n)	(smc_base_addr[id] + ((n) * 0x10))

static void __iomem *smc_base_addr[2];

static void __init sam9_smc_cs_configure(void __iomem *base, struct sam9_smc_config* config)
{

	/* Setup register */
	__raw_writel(AT91_SMC_NWESETUP_(config->nwe_setup)
		   | AT91_SMC_NCS_WRSETUP_(config->ncs_write_setup)
		   | AT91_SMC_NRDSETUP_(config->nrd_setup)
		   | AT91_SMC_NCS_RDSETUP_(config->ncs_read_setup),
		   base + AT91_SMC_SETUP);

	/* Pulse register */
	__raw_writel(AT91_SMC_NWEPULSE_(config->nwe_pulse)
		   | AT91_SMC_NCS_WRPULSE_(config->ncs_write_pulse)
		   | AT91_SMC_NRDPULSE_(config->nrd_pulse)
		   | AT91_SMC_NCS_RDPULSE_(config->ncs_read_pulse),
		   base + AT91_SMC_PULSE);

	/* Cycle register */
	__raw_writel(AT91_SMC_NWECYCLE_(config->write_cycle)
		   | AT91_SMC_NRDCYCLE_(config->read_cycle),
		   base + AT91_SMC_CYCLE);

	/* Mode register */
	__raw_writel(config->mode
		   | AT91_SMC_TDF_(config->tdf_cycles),
		   base + AT91_SMC_MODE);
}

void __init sam9_smc_configure(int id, int cs, struct sam9_smc_config* config)
{
	sam9_smc_cs_configure(AT91_SMC_CS(id, cs), config);
}

void __init at91sam9_ioremap_smc(int id, u32 addr)
{
	if (id > 1) {
		pr_warn("%s: id > 2\n", __func__);
		return;
	}
	smc_base_addr[id] = ioremap(addr, 512);
	if (!smc_base_addr[id])
		pr_warn("Impossible to ioremap smc.%d 0x%x\n", id, addr);
}
