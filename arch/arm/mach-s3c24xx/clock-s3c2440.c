/* linux/arch/arm/mach-s3c2440/clock.c
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
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/mutex.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/serial_core.h>

#include <mach/hardware.h>
#include <linux/atomic.h>
#include <asm/irq.h>

#include <mach/regs-clock.h>

#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/regs-serial.h>

/* S3C2440 extended clock support */

static unsigned long s3c2440_camif_upll_round(struct clk *clk,
					      unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	int div;

	if (rate > parent_rate)
		return parent_rate;

	/* note, we remove the +/- 1 calculations for the divisor */

	div = (parent_rate / rate) / 2;

	if (div < 1)
		div = 1;
	else if (div > 16)
		div = 16;

	return parent_rate / (div * 2);
}

static int s3c2440_camif_upll_setrate(struct clk *clk, unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long camdivn =  __raw_readl(S3C2440_CAMDIVN);

	rate = s3c2440_camif_upll_round(clk, rate);

	camdivn &= ~(S3C2440_CAMDIVN_CAMCLK_SEL | S3C2440_CAMDIVN_CAMCLK_MASK);

	if (rate != parent_rate) {
		camdivn |= S3C2440_CAMDIVN_CAMCLK_SEL;
		camdivn |= (((parent_rate / rate) / 2) - 1);
	}

	__raw_writel(camdivn, S3C2440_CAMDIVN);

	return 0;
}

/* Extra S3C2440 clocks */

static struct clk s3c2440_clk_cam = {
	.name		= "camif",
	.enable		= s3c2410_clkcon_enable,
	.ctrlbit	= S3C2440_CLKCON_CAMERA,
};

static struct clk s3c2440_clk_cam_upll = {
	.name		= "camif-upll",
	.ops		= &(struct clk_ops) {
		.set_rate	= s3c2440_camif_upll_setrate,
		.round_rate	= s3c2440_camif_upll_round,
	},
};

static struct clk s3c2440_clk_ac97 = {
	.name		= "ac97",
	.enable		= s3c2410_clkcon_enable,
	.ctrlbit	= S3C2440_CLKCON_CAMERA,
};

static unsigned long  s3c2440_fclk_n_getrate(struct clk *clk)
{
	unsigned long ucon0, ucon1, ucon2, divisor;

	/* the fun of calculating the uart divisors on the s3c2440 */
	ucon0 = __raw_readl(S3C24XX_VA_UART0 + S3C2410_UCON);
	ucon1 = __raw_readl(S3C24XX_VA_UART1 + S3C2410_UCON);
	ucon2 = __raw_readl(S3C24XX_VA_UART2 + S3C2410_UCON);

	ucon0 &= S3C2440_UCON0_DIVMASK;
	ucon1 &= S3C2440_UCON1_DIVMASK;
	ucon2 &= S3C2440_UCON2_DIVMASK;

	if (ucon0 != 0)
		divisor = (ucon0 >> S3C2440_UCON_DIVSHIFT) + 6;
	else if (ucon1 != 0)
		divisor = (ucon1 >> S3C2440_UCON_DIVSHIFT) + 21;
	else if (ucon2 != 0)
		divisor = (ucon2 >> S3C2440_UCON_DIVSHIFT) + 36;
	else
		/* manual calims 44, seems to be 9 */
		divisor = 9;

	return clk_get_rate(clk->parent) / divisor;
}

static struct clk s3c2440_clk_fclk_n = {
	.name		= "fclk_n",
	.parent		= &clk_f,
	.ops		= &(struct clk_ops) {
		.get_rate	= s3c2440_fclk_n_getrate,
	},
};

static struct clk_lookup s3c2440_clk_lookup[] = {
	CLKDEV_INIT(NULL, "clk_uart_baud1", &s3c24xx_uclk),
	CLKDEV_INIT(NULL, "clk_uart_baud2", &clk_p),
	CLKDEV_INIT(NULL, "clk_uart_baud3", &s3c2440_clk_fclk_n),
};

static int s3c2440_clk_add(struct device *dev, struct subsys_interface *sif)
{
	struct clk *clock_upll;
	struct clk *clock_h;
	struct clk *clock_p;

	clock_p = clk_get(NULL, "pclk");
	clock_h = clk_get(NULL, "hclk");
	clock_upll = clk_get(NULL, "upll");

	if (IS_ERR(clock_p) || IS_ERR(clock_h) || IS_ERR(clock_upll)) {
		printk(KERN_ERR "S3C2440: Failed to get parent clocks\n");
		return -EINVAL;
	}

	s3c2440_clk_cam.parent = clock_h;
	s3c2440_clk_ac97.parent = clock_p;
	s3c2440_clk_cam_upll.parent = clock_upll;
	s3c24xx_register_clock(&s3c2440_clk_fclk_n);

	s3c24xx_register_clock(&s3c2440_clk_ac97);
	s3c24xx_register_clock(&s3c2440_clk_cam);
	s3c24xx_register_clock(&s3c2440_clk_cam_upll);
	clkdev_add_table(s3c2440_clk_lookup, ARRAY_SIZE(s3c2440_clk_lookup));

	clk_disable(&s3c2440_clk_ac97);
	clk_disable(&s3c2440_clk_cam);

	return 0;
}

static struct subsys_interface s3c2440_clk_interface = {
	.name		= "s3c2440_clk",
	.subsys		= &s3c2440_subsys,
	.add_dev	= s3c2440_clk_add,
};

static __init int s3c24xx_clk_init(void)
{
	return subsys_interface_register(&s3c2440_clk_interface);
}

arch_initcall(s3c24xx_clk_init);
