/*
 * Copyright (c) 2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410,S3C2440,S3C2442 Clock control support
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
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/serial_core.h>
#include <linux/io.h>

#include <asm/mach/map.h>

#include <mach/hardware.h>

#include <plat/regs-serial.h>
#include <mach/regs-clock.h>
#include <mach/regs-gpio.h>

#include <plat/clock.h>
#include <plat/cpu.h>

int s3c2410_clkcon_enable(struct clk *clk, int enable)
{
	unsigned int clocks = clk->ctrlbit;
	unsigned long clkcon;

	clkcon = __raw_readl(S3C2410_CLKCON);

	if (enable)
		clkcon |= clocks;
	else
		clkcon &= ~clocks;

	/* ensure none of the special function bits set */
	clkcon &= ~(S3C2410_CLKCON_IDLE|S3C2410_CLKCON_POWER);

	__raw_writel(clkcon, S3C2410_CLKCON);

	return 0;
}

static int s3c2410_upll_enable(struct clk *clk, int enable)
{
	unsigned long clkslow = __raw_readl(S3C2410_CLKSLOW);
	unsigned long orig = clkslow;

	if (enable)
		clkslow &= ~S3C2410_CLKSLOW_UCLK_OFF;
	else
		clkslow |= S3C2410_CLKSLOW_UCLK_OFF;

	__raw_writel(clkslow, S3C2410_CLKSLOW);

	/* if we started the UPLL, then allow to settle */

	if (enable && (orig & S3C2410_CLKSLOW_UCLK_OFF))
		udelay(200);

	return 0;
}

/* standard clock definitions */

static struct clk init_clocks_off[] = {
	{
		.name		= "nand",
		.parent		= &clk_h,
		.enable		= s3c2410_clkcon_enable,
		.ctrlbit	= S3C2410_CLKCON_NAND,
	}, {
		.name		= "sdi",
		.parent		= &clk_p,
		.enable		= s3c2410_clkcon_enable,
		.ctrlbit	= S3C2410_CLKCON_SDI,
	}, {
		.name		= "adc",
		.parent		= &clk_p,
		.enable		= s3c2410_clkcon_enable,
		.ctrlbit	= S3C2410_CLKCON_ADC,
	}, {
		.name		= "i2c",
		.parent		= &clk_p,
		.enable		= s3c2410_clkcon_enable,
		.ctrlbit	= S3C2410_CLKCON_IIC,
	}, {
		.name		= "iis",
		.parent		= &clk_p,
		.enable		= s3c2410_clkcon_enable,
		.ctrlbit	= S3C2410_CLKCON_IIS,
	}, {
		.name		= "spi",
		.parent		= &clk_p,
		.enable		= s3c2410_clkcon_enable,
		.ctrlbit	= S3C2410_CLKCON_SPI,
	}
};

static struct clk clk_lcd = {
	.name		= "lcd",
	.parent		= &clk_h,
	.enable		= s3c2410_clkcon_enable,
	.ctrlbit	= S3C2410_CLKCON_LCDC,
};

static struct clk clk_gpio = {
	.name		= "gpio",
	.parent		= &clk_p,
	.enable		= s3c2410_clkcon_enable,
	.ctrlbit	= S3C2410_CLKCON_GPIO,
};

static struct clk clk_usb_host = {
	.name		= "usb-host",
	.parent		= &clk_h,
	.enable		= s3c2410_clkcon_enable,
	.ctrlbit	= S3C2410_CLKCON_USBH,
};

static struct clk clk_usb_device = {
	.name		= "usb-device",
	.parent		= &clk_h,
	.enable		= s3c2410_clkcon_enable,
	.ctrlbit	= S3C2410_CLKCON_USBD,
};

static struct clk clk_timers = {
	.name		= "timers",
	.parent		= &clk_p,
	.enable		= s3c2410_clkcon_enable,
	.ctrlbit	= S3C2410_CLKCON_PWMT,
};

struct clk s3c24xx_clk_uart0 = {
	.name		= "uart",
	.devname	= "s3c2410-uart.0",
	.parent		= &clk_p,
	.enable		= s3c2410_clkcon_enable,
	.ctrlbit	= S3C2410_CLKCON_UART0,
};

struct clk s3c24xx_clk_uart1 = {
	.name		= "uart",
	.devname	= "s3c2410-uart.1",
	.parent		= &clk_p,
	.enable		= s3c2410_clkcon_enable,
	.ctrlbit	= S3C2410_CLKCON_UART1,
};

struct clk s3c24xx_clk_uart2 = {
	.name		= "uart",
	.devname	= "s3c2410-uart.2",
	.parent		= &clk_p,
	.enable		= s3c2410_clkcon_enable,
	.ctrlbit	= S3C2410_CLKCON_UART2,
};

static struct clk clk_rtc = {
	.name		= "rtc",
	.parent		= &clk_p,
	.enable		= s3c2410_clkcon_enable,
	.ctrlbit	= S3C2410_CLKCON_RTC,
};

static struct clk clk_watchdog = {
	.name		= "watchdog",
	.parent		= &clk_p,
	.ctrlbit	= 0,
};

static struct clk clk_usb_bus_host = {
	.name		= "usb-bus-host",
	.parent		= &clk_usb_bus,
};

static struct clk clk_usb_bus_gadget = {
	.name		= "usb-bus-gadget",
	.parent		= &clk_usb_bus,
};

static struct clk *init_clocks[] = {
	&clk_lcd,
	&clk_gpio,
	&clk_usb_host,
	&clk_usb_device,
	&clk_timers,
	&s3c24xx_clk_uart0,
	&s3c24xx_clk_uart1,
	&s3c24xx_clk_uart2,
	&clk_rtc,
	&clk_watchdog,
	&clk_usb_bus_host,
	&clk_usb_bus_gadget,
};

/* s3c2410_baseclk_add()
 *
 * Add all the clocks used by the s3c2410 or compatible CPUs
 * such as the S3C2440 and S3C2442.
 *
 * We cannot use a system device as we are needed before any
 * of the init-calls that initialise the devices are actually
 * done.
*/

int __init s3c2410_baseclk_add(void)
{
	unsigned long clkslow = __raw_readl(S3C2410_CLKSLOW);
	unsigned long clkcon  = __raw_readl(S3C2410_CLKCON);
	struct clk *xtal;
	int ret;
	int ptr;

	clk_upll.enable = s3c2410_upll_enable;

	if (s3c24xx_register_clock(&clk_usb_bus) < 0)
		printk(KERN_ERR "failed to register usb bus clock\n");

	/* register clocks from clock array */

	for (ptr = 0; ptr < ARRAY_SIZE(init_clocks); ptr++) {
		struct clk *clkp = init_clocks[ptr];

		/* ensure that we note the clock state */

		clkp->usage = clkcon & clkp->ctrlbit ? 1 : 0;

		ret = s3c24xx_register_clock(clkp);
		if (ret < 0) {
			printk(KERN_ERR "Failed to register clock %s (%d)\n",
			       clkp->name, ret);
		}
	}

	/* We must be careful disabling the clocks we are not intending to
	 * be using at boot time, as subsystems such as the LCD which do
	 * their own DMA requests to the bus can cause the system to lockup
	 * if they where in the middle of requesting bus access.
	 *
	 * Disabling the LCD clock if the LCD is active is very dangerous,
	 * and therefore the bootloader should be careful to not enable
	 * the LCD clock if it is not needed.
	*/

	/* install (and disable) the clocks we do not need immediately */

	s3c_register_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));
	s3c_disable_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));

	/* show the clock-slow value */

	xtal = clk_get(NULL, "xtal");

	printk("CLOCK: Slow mode (%ld.%ld MHz), %s, MPLL %s, UPLL %s\n",
	       print_mhz(clk_get_rate(xtal) /
			 ( 2 * S3C2410_CLKSLOW_GET_SLOWVAL(clkslow))),
	       (clkslow & S3C2410_CLKSLOW_SLOW) ? "slow" : "fast",
	       (clkslow & S3C2410_CLKSLOW_MPLL_OFF) ? "off" : "on",
	       (clkslow & S3C2410_CLKSLOW_UCLK_OFF) ? "off" : "on");

	return 0;
}
