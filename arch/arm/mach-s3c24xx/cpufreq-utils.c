/*
 * Copyright (c) 2009 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX CPU Frequency scaling - utils for S3C2410/S3C2440/S3C2442
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/cpufreq.h>
#include <linux/io.h>

#include <mach/map.h>
#include <mach/regs-clock.h>

#include <plat/cpu-freq-core.h>

#include "regs-mem.h"

/**
 * s3c2410_cpufreq_setrefresh - set SDRAM refresh value
 * @cfg: The frequency configuration
 *
 * Set the SDRAM refresh value appropriately for the configured
 * frequency.
 */
void s3c2410_cpufreq_setrefresh(struct s3c_cpufreq_config *cfg)
{
	struct s3c_cpufreq_board *board = cfg->board;
	unsigned long refresh;
	unsigned long refval;

	/* Reduce both the refresh time (in ns) and the frequency (in MHz)
	 * down to ensure that we do not overflow 32 bit numbers.
	 *
	 * This should work for HCLK up to 133MHz and refresh period up
	 * to 30usec.
	 */

	refresh = (cfg->freq.hclk / 100) * (board->refresh / 10);
	refresh = DIV_ROUND_UP(refresh, (1000 * 1000)); /* apply scale  */
	refresh = (1 << 11) + 1 - refresh;

	s3c_freq_dbg("%s: refresh value %lu\n", __func__, refresh);

	refval = __raw_readl(S3C2410_REFRESH);
	refval &= ~((1 << 12) - 1);
	refval |= refresh;
	__raw_writel(refval, S3C2410_REFRESH);
}

/**
 * s3c2410_set_fvco - set the PLL value
 * @cfg: The frequency configuration
 */
void s3c2410_set_fvco(struct s3c_cpufreq_config *cfg)
{
	__raw_writel(cfg->pll.driver_data, S3C2410_MPLLCON);
}
