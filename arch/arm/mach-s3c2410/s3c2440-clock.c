/* linux/arch/arm/mach-s3c2410/s3c2440-clock.c
 *
 * Copyright (c) 2004-2005 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2440 Clock support
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

#include <asm/hardware.h>
#include <asm/atomic.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <asm/hardware/clock.h>
#include <asm/arch/regs-clock.h>

#include "clock.h"
#include "cpu.h"

/* S3C2440 extended clock support */

static struct clk s3c2440_clk_upll = {
	.name		= "upll",
	.id		= -1,
};

static struct clk s3c2440_clk_cam = {
	.name		= "camif",
	.id		= -1,
	.enable		= s3c24xx_clkcon_enable,
	.ctrlbit	= S3C2440_CLKCON_CAMERA,
};

static struct clk s3c2440_clk_ac97 = {
	.name		= "ac97",
	.id		= -1,
	.enable		= s3c24xx_clkcon_enable,
	.ctrlbit	= S3C2440_CLKCON_CAMERA,
};

static int s3c2440_clk_add(struct sys_device *sysdev)
{
	unsigned long upllcon = __raw_readl(S3C2410_UPLLCON);
	unsigned long camdivn = __raw_readl(S3C2440_CAMDIVN);
	struct clk *clk_h;
	struct clk *clk_p;
	struct clk *clk_xtal;

	clk_xtal = clk_get(NULL, "xtal");
	if (IS_ERR(clk_xtal)) {
		printk(KERN_ERR "S3C2440: Failed to get clk_xtal\n");
		return -EINVAL;
	}

	s3c2440_clk_upll.rate = s3c2410_get_pll(upllcon, clk_xtal->rate);

	printk("S3C2440: Clock Support, UPLL %ld.%03ld MHz, DVS %s\n",
	       print_mhz(s3c2440_clk_upll.rate),
	       (camdivn & S3C2440_CAMDIVN_DVSEN) ? "on" : "off");

	clk_p = clk_get(NULL, "pclk");
	clk_h = clk_get(NULL, "hclk");

	if (IS_ERR(clk_p) || IS_ERR(clk_h)) {
		printk(KERN_ERR "S3C2440: Failed to get parent clocks\n");
		return -EINVAL;
	}

	s3c2440_clk_cam.parent = clk_h;
	s3c2440_clk_ac97.parent = clk_p;

	s3c24xx_register_clock(&s3c2440_clk_ac97);
	s3c24xx_register_clock(&s3c2440_clk_cam);
	s3c24xx_register_clock(&s3c2440_clk_upll);

	clk_disable(&s3c2440_clk_ac97);
	clk_disable(&s3c2440_clk_cam);

	return 0;
}

static struct sysdev_driver s3c2440_clk_driver = {
	.add	= s3c2440_clk_add,
};

static __init int s3c24xx_clk_driver(void)
{
	return sysdev_driver_register(&s3c2440_sysclass, &s3c2440_clk_driver);
}

arch_initcall(s3c24xx_clk_driver);
