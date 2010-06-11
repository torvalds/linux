/* linux/arch/arm/plat-s3c24xx/clock.c
 *
 * Copyright (c) 2004-2005 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX Core clock control support
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
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/irq.h>

#include <mach/regs-clock.h>
#include <mach/regs-gpio.h>

#include <plat/cpu-freq.h>

#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/pll.h>

/* initialise all the clocks */

void __init_or_cpufreq s3c24xx_setup_clocks(unsigned long fclk,
					   unsigned long hclk,
					   unsigned long pclk)
{
	clk_upll.rate = s3c24xx_get_pll(__raw_readl(S3C2410_UPLLCON),
					clk_xtal.rate);

	clk_mpll.rate = fclk;
	clk_h.rate = hclk;
	clk_p.rate = pclk;
	clk_f.rate = fclk;
}
