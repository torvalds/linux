/* linux/arch/arm/plat-s3c24xx/s3c24xx-clock.c
 *
 * Copyright (c) 2004-2005,2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2440/S3C2442 Common clock support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/sysdev.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/atomic.h>
#include <asm/irq.h>

#include <mach/regs-clock.h>

#include <plat/clock.h>
#include <plat/cpu.h>

static int s3c2440_setparent_armclk(struct clk *clk, struct clk *parent)
{
	unsigned long camdivn;
	unsigned long dvs;

	if (parent == &clk_f)
		dvs = 0;
	else if (parent == &clk_h)
		dvs = S3C2440_CAMDIVN_DVSEN;
	else
		return -EINVAL;

	clk->parent = parent;

	camdivn  = __raw_readl(S3C2440_CAMDIVN);
	camdivn &= ~S3C2440_CAMDIVN_DVSEN;
	camdivn |= dvs;
	__raw_writel(camdivn, S3C2440_CAMDIVN);

	return 0;
}

static struct clk clk_arm = {
	.name		= "armclk",
	.id		= -1,
	.set_parent	= s3c2440_setparent_armclk,
};

static int s3c244x_clk_add(struct sys_device *sysdev)
{
	unsigned long camdivn = __raw_readl(S3C2440_CAMDIVN);
	unsigned long clkdivn;
	struct clk *clock_upll;
	int ret;

	printk("S3C244X: Clock Support, DVS %s\n",
	       (camdivn & S3C2440_CAMDIVN_DVSEN) ? "on" : "off");

	clk_arm.parent = (camdivn & S3C2440_CAMDIVN_DVSEN) ? &clk_h : &clk_f;

	ret = s3c24xx_register_clock(&clk_arm);
	if (ret < 0) {
		printk(KERN_ERR "S3C24XX: Failed to add armclk (%d)\n", ret);
		return ret;
	}

	clock_upll = clk_get(NULL, "upll");
	if (IS_ERR(clock_upll)) {
		printk(KERN_ERR "S3C244X: Failed to get upll clock\n");
		return -ENOENT;
	}

	/* check rate of UPLL, and if it is near 96MHz, then change
	 * to using half the UPLL rate for the system */

	if (clk_get_rate(clock_upll) > (94 * MHZ)) {
		clk_usb_bus.rate = clk_get_rate(clock_upll) / 2;

		spin_lock(&clocks_lock);

		clkdivn = __raw_readl(S3C2410_CLKDIVN);
		clkdivn |= S3C2440_CLKDIVN_UCLK;
		__raw_writel(clkdivn, S3C2410_CLKDIVN);

		spin_unlock(&clocks_lock);
	}

	return 0;
}

static struct sysdev_driver s3c2440_clk_driver = {
	.add		= s3c244x_clk_add,
};

static int s3c2440_clk_init(void)
{
	return sysdev_driver_register(&s3c2440_sysclass, &s3c2440_clk_driver);
}

arch_initcall(s3c2440_clk_init);

static struct sysdev_driver s3c2442_clk_driver = {
	.add		= s3c244x_clk_add,
};

static int s3c2442_clk_init(void)
{
	return sysdev_driver_register(&s3c2442_sysclass, &s3c2442_clk_driver);
}

arch_initcall(s3c2442_clk_init);
