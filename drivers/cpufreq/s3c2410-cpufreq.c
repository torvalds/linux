// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2006-2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 CPU Frequency scaling
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/cpufreq.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/regs-clock.h>

#include <plat/cpu.h>
#include <plat/cpu-freq-core.h>

/* Note, 2410A has an extra mode for 1:4:4 ratio, bit 2 of CLKDIV */

static void s3c2410_cpufreq_setdivs(struct s3c_cpufreq_config *cfg)
{
	u32 clkdiv = 0;

	if (cfg->divs.h_divisor == 2)
		clkdiv |= S3C2410_CLKDIVN_HDIVN;

	if (cfg->divs.p_divisor != cfg->divs.h_divisor)
		clkdiv |= S3C2410_CLKDIVN_PDIVN;

	__raw_writel(clkdiv, S3C2410_CLKDIVN);
}

static int s3c2410_cpufreq_calcdivs(struct s3c_cpufreq_config *cfg)
{
	unsigned long hclk, fclk, pclk;
	unsigned int hdiv, pdiv;
	unsigned long hclk_max;

	fclk = cfg->freq.fclk;
	hclk_max = cfg->max.hclk;

	cfg->freq.armclk = fclk;

	s3c_freq_dbg("%s: fclk is %lu, max hclk %lu\n",
		      __func__, fclk, hclk_max);

	hdiv = (fclk > cfg->max.hclk) ? 2 : 1;
	hclk = fclk / hdiv;

	if (hclk > cfg->max.hclk) {
		s3c_freq_dbg("%s: hclk too big\n", __func__);
		return -EINVAL;
	}

	pdiv = (hclk > cfg->max.pclk) ? 2 : 1;
	pclk = hclk / pdiv;

	if (pclk > cfg->max.pclk) {
		s3c_freq_dbg("%s: pclk too big\n", __func__);
		return -EINVAL;
	}

	pdiv *= hdiv;

	/* record the result */
	cfg->divs.p_divisor = pdiv;
	cfg->divs.h_divisor = hdiv;

	return 0;
}

static struct s3c_cpufreq_info s3c2410_cpufreq_info = {
	.max		= {
		.fclk	= 200000000,
		.hclk	= 100000000,
		.pclk	=  50000000,
	},

	/* transition latency is about 5ms worst-case, so
	 * set 10ms to be sure */
	.latency	= 10000000,

	.locktime_m	= 150,
	.locktime_u	= 150,
	.locktime_bits	= 12,

	.need_pll	= 1,

	.name		= "s3c2410",
	.calc_iotiming	= s3c2410_iotiming_calc,
	.set_iotiming	= s3c2410_iotiming_set,
	.get_iotiming	= s3c2410_iotiming_get,

	.set_fvco	= s3c2410_set_fvco,
	.set_refresh	= s3c2410_cpufreq_setrefresh,
	.set_divs	= s3c2410_cpufreq_setdivs,
	.calc_divs	= s3c2410_cpufreq_calcdivs,

	.debug_io_show	= s3c_cpufreq_debugfs_call(s3c2410_iotiming_debugfs),
};

static int s3c2410_cpufreq_add(struct device *dev,
			       struct subsys_interface *sif)
{
	return s3c_cpufreq_register(&s3c2410_cpufreq_info);
}

static struct subsys_interface s3c2410_cpufreq_interface = {
	.name		= "s3c2410_cpufreq",
	.subsys		= &s3c2410_subsys,
	.add_dev	= s3c2410_cpufreq_add,
};

static int __init s3c2410_cpufreq_init(void)
{
	return subsys_interface_register(&s3c2410_cpufreq_interface);
}
arch_initcall(s3c2410_cpufreq_init);

static int s3c2410a_cpufreq_add(struct device *dev,
				struct subsys_interface *sif)
{
	/* alter the maximum freq settings for S3C2410A. If a board knows
	 * it only has a maximum of 200, then it should register its own
	 * limits. */

	s3c2410_cpufreq_info.max.fclk = 266000000;
	s3c2410_cpufreq_info.max.hclk = 133000000;
	s3c2410_cpufreq_info.max.pclk =  66500000;
	s3c2410_cpufreq_info.name = "s3c2410a";

	return s3c2410_cpufreq_add(dev, sif);
}

static struct subsys_interface s3c2410a_cpufreq_interface = {
	.name		= "s3c2410a_cpufreq",
	.subsys		= &s3c2410a_subsys,
	.add_dev	= s3c2410a_cpufreq_add,
};

static int __init s3c2410a_cpufreq_init(void)
{
	return subsys_interface_register(&s3c2410a_cpufreq_interface);
}
arch_initcall(s3c2410a_cpufreq_init);
