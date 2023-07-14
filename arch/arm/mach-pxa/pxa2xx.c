// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/arm/mach-pxa/pxa2xx.c
 *
 * code specific to pxa2xx
 *
 * Copyright (C) 2008 Dmitry Baryshkov
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/io.h>

#include "pxa2xx-regs.h"
#include "mfp-pxa25x.h"
#include "generic.h"
#include "reset.h"
#include "smemc.h"
#include <linux/soc/pxa/smemc.h>

void pxa2xx_clear_reset_status(unsigned int mask)
{
	/* RESET_STATUS_* has a 1:1 mapping with RCSR */
	RCSR = mask;
}

#define MDCNFG_DRAC2(mdcnfg)	(((mdcnfg) >> 21) & 0x3)
#define MDCNFG_DRAC0(mdcnfg)	(((mdcnfg) >> 5) & 0x3)

int pxa2xx_smemc_get_sdram_rows(void)
{
	static int sdram_rows;
	unsigned int drac2 = 0, drac0 = 0;
	u32 mdcnfg;

	if (sdram_rows)
		return sdram_rows;

	mdcnfg = readl_relaxed(MDCNFG);

	if (mdcnfg & (MDCNFG_DE2 | MDCNFG_DE3))
		drac2 = MDCNFG_DRAC2(mdcnfg);

	if (mdcnfg & (MDCNFG_DE0 | MDCNFG_DE1))
		drac0 = MDCNFG_DRAC0(mdcnfg);

	sdram_rows = 1 << (11 + max(drac0, drac2));
	return sdram_rows;
}
