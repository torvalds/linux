// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2412 CPU Frequency scalling
*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/cpufreq.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/regs-clock.h>
#include <mach/s3c2412.h>

#include <plat/cpu.h>
#include <plat/cpu-freq-core.h>

/* our clock resources. */
static struct clk *xtal;
static struct clk *fclk;
static struct clk *hclk;
static struct clk *armclk;

/* HDIV: 1, 2, 3, 4, 6, 8 */

static int s3c2412_cpufreq_calcdivs(struct s3c_cpufreq_config *cfg)
{
	unsigned int hdiv, pdiv, armdiv, dvs;
	unsigned long hclk, fclk, armclk, armdiv_clk;
	unsigned long hclk_max;

	fclk = cfg->freq.fclk;
	armclk = cfg->freq.armclk;
	hclk_max = cfg->max.hclk;

	/* We can't run hclk above armclk as at the best we have to
	 * have armclk and hclk in dvs mode. */

	if (hclk_max > armclk)
		hclk_max = armclk;

	s3c_freq_dbg("%s: fclk=%lu, armclk=%lu, hclk_max=%lu\n",
		     __func__, fclk, armclk, hclk_max);
	s3c_freq_dbg("%s: want f=%lu, arm=%lu, h=%lu, p=%lu\n",
		     __func__, cfg->freq.fclk, cfg->freq.armclk,
		     cfg->freq.hclk, cfg->freq.pclk);

	armdiv = fclk / armclk;

	if (armdiv < 1)
		armdiv = 1;
	if (armdiv > 2)
		armdiv = 2;

	cfg->divs.arm_divisor = armdiv;
	armdiv_clk = fclk / armdiv;

	hdiv = armdiv_clk / hclk_max;
	if (hdiv < 1)
		hdiv = 1;

	cfg->freq.hclk = hclk = armdiv_clk / hdiv;

	/* set dvs depending on whether we reached armclk or not. */
	cfg->divs.dvs = dvs = armclk < armdiv_clk;

	/* update the actual armclk we achieved. */
	cfg->freq.armclk = dvs ? hclk : armdiv_clk;

	s3c_freq_dbg("%s: armclk %lu, hclk %lu, armdiv %d, hdiv %d, dvs %d\n",
		     __func__, armclk, hclk, armdiv, hdiv, cfg->divs.dvs);

	if (hdiv > 4)
		goto invalid;

	pdiv = (hclk > cfg->max.pclk) ? 2 : 1;

	if ((hclk / pdiv) > cfg->max.pclk)
		pdiv++;

	cfg->freq.pclk = hclk / pdiv;

	s3c_freq_dbg("%s: pdiv %d\n", __func__, pdiv);

	if (pdiv > 2)
		goto invalid;

	pdiv *= hdiv;

	/* store the result, and then return */

	cfg->divs.h_divisor = hdiv * armdiv;
	cfg->divs.p_divisor = pdiv * armdiv;

	return 0;

invalid:
	return -EINVAL;
}

static void s3c2412_cpufreq_setdivs(struct s3c_cpufreq_config *cfg)
{
	unsigned long clkdiv;
	unsigned long olddiv;

	olddiv = clkdiv = __raw_readl(S3C2410_CLKDIVN);

	/* clear off current clock info */

	clkdiv &= ~S3C2412_CLKDIVN_ARMDIVN;
	clkdiv &= ~S3C2412_CLKDIVN_HDIVN_MASK;
	clkdiv &= ~S3C2412_CLKDIVN_PDIVN;

	if (cfg->divs.arm_divisor == 2)
		clkdiv |= S3C2412_CLKDIVN_ARMDIVN;

	clkdiv |= ((cfg->divs.h_divisor / cfg->divs.arm_divisor) - 1);

	if (cfg->divs.p_divisor != cfg->divs.h_divisor)
		clkdiv |= S3C2412_CLKDIVN_PDIVN;

	s3c_freq_dbg("%s: div %08lx => %08lx\n", __func__, olddiv, clkdiv);
	__raw_writel(clkdiv, S3C2410_CLKDIVN);

	clk_set_parent(armclk, cfg->divs.dvs ? hclk : fclk);
}

static void s3c2412_cpufreq_setrefresh(struct s3c_cpufreq_config *cfg)
{
	struct s3c_cpufreq_board *board = cfg->board;
	unsigned long refresh;

	s3c_freq_dbg("%s: refresh %u ns, hclk %lu\n", __func__,
		     board->refresh, cfg->freq.hclk);

	/* Reduce both the refresh time (in ns) and the frequency (in MHz)
	 * by 10 each to ensure that we do not overflow 32 bit numbers. This
	 * should work for HCLK up to 133MHz and refresh period up to 30usec.
	 */

	refresh = (board->refresh / 10);
	refresh *= (cfg->freq.hclk / 100);
	refresh /= (1 * 1000 * 1000);	/* 10^6 */

	s3c_freq_dbg("%s: setting refresh 0x%08lx\n", __func__, refresh);
	__raw_writel(refresh, S3C2412_REFRESH);
}

/* set the default cpu frequency information, based on an 200MHz part
 * as we have no other way of detecting the speed rating in software.
 */

static struct s3c_cpufreq_info s3c2412_cpufreq_info = {
	.max		= {
		.fclk	= 200000000,
		.hclk	= 100000000,
		.pclk	=  50000000,
	},

	.latency	= 5000000, /* 5ms */

	.locktime_m	= 150,
	.locktime_u	= 150,
	.locktime_bits	= 16,

	.name		= "s3c2412",
	.set_refresh	= s3c2412_cpufreq_setrefresh,
	.set_divs	= s3c2412_cpufreq_setdivs,
	.calc_divs	= s3c2412_cpufreq_calcdivs,

	.calc_iotiming	= s3c2412_iotiming_calc,
	.set_iotiming	= s3c2412_iotiming_set,
	.get_iotiming	= s3c2412_iotiming_get,

	.debug_io_show  = s3c_cpufreq_debugfs_call(s3c2412_iotiming_debugfs),
};

static int s3c2412_cpufreq_add(struct device *dev,
			       struct subsys_interface *sif)
{
	unsigned long fclk_rate;

	hclk = clk_get(NULL, "hclk");
	if (IS_ERR(hclk)) {
		pr_err("cannot find hclk clock\n");
		return -ENOENT;
	}

	fclk = clk_get(NULL, "fclk");
	if (IS_ERR(fclk)) {
		pr_err("cannot find fclk clock\n");
		goto err_fclk;
	}

	fclk_rate = clk_get_rate(fclk);
	if (fclk_rate > 200000000) {
		pr_info("fclk %ld MHz, assuming 266MHz capable part\n",
			fclk_rate / 1000000);
		s3c2412_cpufreq_info.max.fclk = 266000000;
		s3c2412_cpufreq_info.max.hclk = 133000000;
		s3c2412_cpufreq_info.max.pclk =  66000000;
	}

	armclk = clk_get(NULL, "armclk");
	if (IS_ERR(armclk)) {
		pr_err("cannot find arm clock\n");
		goto err_armclk;
	}

	xtal = clk_get(NULL, "xtal");
	if (IS_ERR(xtal)) {
		pr_err("cannot find xtal clock\n");
		goto err_xtal;
	}

	return s3c_cpufreq_register(&s3c2412_cpufreq_info);

err_xtal:
	clk_put(armclk);
err_armclk:
	clk_put(fclk);
err_fclk:
	clk_put(hclk);

	return -ENOENT;
}

static struct subsys_interface s3c2412_cpufreq_interface = {
	.name		= "s3c2412_cpufreq",
	.subsys		= &s3c2412_subsys,
	.add_dev	= s3c2412_cpufreq_add,
};

static int s3c2412_cpufreq_init(void)
{
	return subsys_interface_register(&s3c2412_cpufreq_interface);
}
arch_initcall(s3c2412_cpufreq_init);
