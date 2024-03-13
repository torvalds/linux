// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2009 Simtec Electronics
//	http://armlinux.simtec.co.uk/
//	Ben Dooks <ben@simtec.co.uk>
//
// S3C24XX CPU Frequency scaling - utils for S3C2410/S3C2440/S3C2442

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/cpufreq.h>
#include <linux/io.h>
#include <linux/clk.h>

#include "map.h"
#include "regs-clock.h"

#include <linux/soc/samsung/s3c-cpufreq-core.h>

#include "regs-mem-s3c24xx.h"

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
	if (!IS_ERR(cfg->mpll))
		clk_set_rate(cfg->mpll, cfg->pll.frequency);
}

#if defined(CONFIG_CPU_S3C2440) || defined(CONFIG_CPU_S3C2442)
u32 s3c2440_read_camdivn(void)
{
	return __raw_readl(S3C2440_CAMDIVN);
}

void s3c2440_write_camdivn(u32 camdiv)
{
	__raw_writel(camdiv, S3C2440_CAMDIVN);
}
#endif

u32 s3c24xx_read_clkdivn(void)
{
	return __raw_readl(S3C2410_CLKDIVN);
}

void s3c24xx_write_clkdivn(u32 clkdiv)
{
	__raw_writel(clkdiv, S3C2410_CLKDIVN);
}

u32 s3c24xx_read_mpllcon(void)
{
	return __raw_readl(S3C2410_MPLLCON);
}

void s3c24xx_write_locktime(u32 locktime)
{
	return __raw_writel(locktime, S3C2410_LOCKTIME);
}
