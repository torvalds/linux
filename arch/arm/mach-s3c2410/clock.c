/* linux/arch/arm/mach-s3c2410/clock.c
 *
 * Copyright (c) 2004-2005 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 Clock control support
 *
 * Based on, and code from linux/arch/arm/mach-versatile/clock.c
 **
 **  Copyright (C) 2004 ARM Limited.
 **  Written by Deep Blue Solutions Limited.
 *
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
#include <linux/platform_device.h>
#include <linux/sysdev.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/clk.h>
#include <linux/mutex.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <asm/arch/regs-clock.h>

#include "clock.h"
#include "cpu.h"

/* clock information */

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);

/* old functions */

void inline s3c24xx_clk_enable(unsigned int clocks, unsigned int enable)
{
	unsigned long clkcon;

	clkcon = __raw_readl(S3C2410_CLKCON);

	if (enable)
		clkcon |= clocks;
	else
		clkcon &= ~clocks;

	/* ensure none of the special function bits set */
	clkcon &= ~(S3C2410_CLKCON_IDLE|S3C2410_CLKCON_POWER);

	__raw_writel(clkcon, S3C2410_CLKCON);
}

/* enable and disable calls for use with the clk struct */

static int clk_null_enable(struct clk *clk, int enable)
{
	return 0;
}

int s3c24xx_clkcon_enable(struct clk *clk, int enable)
{
	s3c24xx_clk_enable(clk->ctrlbit, enable);
	return 0;
}

/* Clock API calls */

struct clk *clk_get(struct device *dev, const char *id)
{
	struct clk *p;
	struct clk *clk = ERR_PTR(-ENOENT);
	int idno;

	if (dev == NULL || dev->bus != &platform_bus_type)
		idno = -1;
	else
		idno = to_platform_device(dev)->id;

	mutex_lock(&clocks_mutex);

	list_for_each_entry(p, &clocks, list) {
		if (p->id == idno &&
		    strcmp(id, p->name) == 0 &&
		    try_module_get(p->owner)) {
			clk = p;
			break;
		}
	}

	/* check for the case where a device was supplied, but the
	 * clock that was being searched for is not device specific */

	if (IS_ERR(clk)) {
		list_for_each_entry(p, &clocks, list) {
			if (p->id == -1 && strcmp(id, p->name) == 0 &&
			    try_module_get(p->owner)) {
				clk = p;
				break;
			}
		}
	}

	mutex_unlock(&clocks_mutex);
	return clk;
}

void clk_put(struct clk *clk)
{
	module_put(clk->owner);
}

int clk_enable(struct clk *clk)
{
	if (IS_ERR(clk) || clk == NULL)
		return -EINVAL;

	clk_enable(clk->parent);

	mutex_lock(&clocks_mutex);

	if ((clk->usage++) == 0)
		(clk->enable)(clk, 1);

	mutex_unlock(&clocks_mutex);
	return 0;
}

void clk_disable(struct clk *clk)
{
	if (IS_ERR(clk) || clk == NULL)
		return;

	mutex_lock(&clocks_mutex);

	if ((--clk->usage) == 0)
		(clk->enable)(clk, 0);

	mutex_unlock(&clocks_mutex);
	clk_disable(clk->parent);
}


unsigned long clk_get_rate(struct clk *clk)
{
	if (IS_ERR(clk))
		return 0;

	if (clk->rate != 0)
		return clk->rate;

	while (clk->parent != NULL && clk->rate == 0)
		clk = clk->parent;

	return clk->rate;
}

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	return rate;
}

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	return -EINVAL;
}

struct clk *clk_get_parent(struct clk *clk)
{
	return clk->parent;
}

EXPORT_SYMBOL(clk_get);
EXPORT_SYMBOL(clk_put);
EXPORT_SYMBOL(clk_enable);
EXPORT_SYMBOL(clk_disable);
EXPORT_SYMBOL(clk_get_rate);
EXPORT_SYMBOL(clk_round_rate);
EXPORT_SYMBOL(clk_set_rate);
EXPORT_SYMBOL(clk_get_parent);

/* base clocks */

static struct clk clk_xtal = {
	.name		= "xtal",
	.id		= -1,
	.rate		= 0,
	.parent		= NULL,
	.ctrlbit	= 0,
};

static struct clk clk_f = {
	.name		= "fclk",
	.id		= -1,
	.rate		= 0,
	.parent		= NULL,
	.ctrlbit	= 0,
};

static struct clk clk_h = {
	.name		= "hclk",
	.id		= -1,
	.rate		= 0,
	.parent		= NULL,
	.ctrlbit	= 0,
};

static struct clk clk_p = {
	.name		= "pclk",
	.id		= -1,
	.rate		= 0,
	.parent		= NULL,
	.ctrlbit	= 0,
};

/* clocks that could be registered by external code */

struct clk s3c24xx_dclk0 = {
	.name		= "dclk0",
	.id		= -1,
};

struct clk s3c24xx_dclk1 = {
	.name		= "dclk1",
	.id		= -1,
};

struct clk s3c24xx_clkout0 = {
	.name		= "clkout0",
	.id		= -1,
};

struct clk s3c24xx_clkout1 = {
	.name		= "clkout1",
	.id		= -1,
};

struct clk s3c24xx_uclk = {
	.name		= "uclk",
	.id		= -1,
};


/* clock definitions */

static struct clk init_clocks[] = {
	{
		.name		= "nand",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c24xx_clkcon_enable,
		.ctrlbit	= S3C2410_CLKCON_NAND,
	}, {
		.name		= "lcd",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c24xx_clkcon_enable,
		.ctrlbit	= S3C2410_CLKCON_LCDC,
	}, {
		.name		= "usb-host",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c24xx_clkcon_enable,
		.ctrlbit	= S3C2410_CLKCON_USBH,
	}, {
		.name		= "usb-device",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c24xx_clkcon_enable,
		.ctrlbit	= S3C2410_CLKCON_USBD,
	}, {
		.name		= "timers",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c24xx_clkcon_enable,
		.ctrlbit	= S3C2410_CLKCON_PWMT,
	}, {
		.name		= "sdi",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c24xx_clkcon_enable,
		.ctrlbit	= S3C2410_CLKCON_SDI,
	}, {
		.name		= "uart",
		.id		= 0,
		.parent		= &clk_p,
		.enable		= s3c24xx_clkcon_enable,
		.ctrlbit	= S3C2410_CLKCON_UART0,
	}, {
		.name		= "uart",
		.id		= 1,
		.parent		= &clk_p,
		.enable		= s3c24xx_clkcon_enable,
		.ctrlbit	= S3C2410_CLKCON_UART1,
	}, {
		.name		= "uart",
		.id		= 2,
		.parent		= &clk_p,
		.enable		= s3c24xx_clkcon_enable,
		.ctrlbit	= S3C2410_CLKCON_UART2,
	}, {
		.name		= "gpio",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c24xx_clkcon_enable,
		.ctrlbit	= S3C2410_CLKCON_GPIO,
	}, {
		.name		= "rtc",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c24xx_clkcon_enable,
		.ctrlbit	= S3C2410_CLKCON_RTC,
	}, {
		.name		= "adc",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c24xx_clkcon_enable,
		.ctrlbit	= S3C2410_CLKCON_ADC,
	}, {
		.name		= "i2c",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c24xx_clkcon_enable,
		.ctrlbit	= S3C2410_CLKCON_IIC,
	}, {
		.name		= "iis",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c24xx_clkcon_enable,
		.ctrlbit	= S3C2410_CLKCON_IIS,
	}, {
		.name		= "spi",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c24xx_clkcon_enable,
		.ctrlbit	= S3C2410_CLKCON_SPI,
	}, {
		.name		= "watchdog",
		.id		= -1,
		.parent		= &clk_p,
		.ctrlbit	= 0,
	}
};

/* initialise the clock system */

int s3c24xx_register_clock(struct clk *clk)
{
	clk->owner = THIS_MODULE;

	if (clk->enable == NULL)
		clk->enable = clk_null_enable;

	/* if this is a standard clock, set the usage state */

	if (clk->ctrlbit) {
		unsigned long clkcon = __raw_readl(S3C2410_CLKCON);

		clk->usage = (clkcon & clk->ctrlbit) ? 1 : 0;
	}

	/* add to the list of available clocks */

	mutex_lock(&clocks_mutex);
	list_add(&clk->list, &clocks);
	mutex_unlock(&clocks_mutex);

	return 0;
}

/* initalise all the clocks */

int __init s3c24xx_setup_clocks(unsigned long xtal,
				unsigned long fclk,
				unsigned long hclk,
				unsigned long pclk)
{
	unsigned long clkslow = __raw_readl(S3C2410_CLKSLOW);
	struct clk *clkp = init_clocks;
	int ptr;
	int ret;

	printk(KERN_INFO "S3C2410 Clocks, (c) 2004 Simtec Electronics\n");

	/* initialise the main system clocks */

	clk_xtal.rate = xtal;

	clk_h.rate = hclk;
	clk_p.rate = pclk;
	clk_f.rate = fclk;

	/* We must be careful disabling the clocks we are not intending to
	 * be using at boot time, as subsytems such as the LCD which do
	 * their own DMA requests to the bus can cause the system to lockup
	 * if they where in the middle of requesting bus access.
	 *
	 * Disabling the LCD clock if the LCD is active is very dangerous,
	 * and therefore the bootloader should be  careful to not enable
	 * the LCD clock if it is not needed.
	*/

	mutex_lock(&clocks_mutex);

	s3c24xx_clk_enable(S3C2410_CLKCON_NAND, 0);
	s3c24xx_clk_enable(S3C2410_CLKCON_USBH, 0);
	s3c24xx_clk_enable(S3C2410_CLKCON_USBD, 0);
	s3c24xx_clk_enable(S3C2410_CLKCON_ADC, 0);
	s3c24xx_clk_enable(S3C2410_CLKCON_IIC, 0);
	s3c24xx_clk_enable(S3C2410_CLKCON_SPI, 0);

	mutex_unlock(&clocks_mutex);

	/* assume uart clocks are correctly setup */

	/* register our clocks */

	if (s3c24xx_register_clock(&clk_xtal) < 0)
		printk(KERN_ERR "failed to register master xtal\n");

	if (s3c24xx_register_clock(&clk_f) < 0)
		printk(KERN_ERR "failed to register cpu fclk\n");

	if (s3c24xx_register_clock(&clk_h) < 0)
		printk(KERN_ERR "failed to register cpu hclk\n");

	if (s3c24xx_register_clock(&clk_p) < 0)
		printk(KERN_ERR "failed to register cpu pclk\n");

	/* register clocks from clock array */

	for (ptr = 0; ptr < ARRAY_SIZE(init_clocks); ptr++, clkp++) {
		ret = s3c24xx_register_clock(clkp);
		if (ret < 0) {
			printk(KERN_ERR "Failed to register clock %s (%d)\n",
			       clkp->name, ret);
		}
	}

	/* show the clock-slow value */

	printk("CLOCK: Slow mode (%ld.%ld MHz), %s, MPLL %s, UPLL %s\n",
	       print_mhz(xtal / ( 2 * S3C2410_CLKSLOW_GET_SLOWVAL(clkslow))),
	       (clkslow & S3C2410_CLKSLOW_SLOW) ? "slow" : "fast",
	       (clkslow & S3C2410_CLKSLOW_MPLL_OFF) ? "off" : "on",
	       (clkslow & S3C2410_CLKSLOW_UCLK_OFF) ? "off" : "on");

	return 0;
}
