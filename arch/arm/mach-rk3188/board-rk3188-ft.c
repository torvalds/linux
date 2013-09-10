/*
 *
 * Copyright (C) 2013 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/clk.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/board.h>
#include <mach/gpio.h>

#define FT

#ifdef FT
#define CONSOLE_LOGLEVEL 2
#define ARM_PLL_MHZ (552)
#else
#define CONSOLE_LOGLEVEL 9
#define ARM_PLL_MHZ (816)
#endif

static void __init machine_rk30_board_init(void)
{
	console_loglevel = CONSOLE_LOGLEVEL;
}

#define ft_printk(fmt, arg...) \
	printk(KERN_EMERG fmt, ##arg)
unsigned long __init ft_test_init_arm_rate(void);

void __init board_clock_init(void)
{
	rk30_clock_data_init(periph_pll_default, codec_pll_default, RK30_CLOCKS_DEFAULT_FLAGS);
	clk_set_rate(clk_get(NULL, "cpu"), ft_test_init_arm_rate());
	preset_lpj = loops_per_jiffy;
}

static void __init ft_fixup(struct machine_desc *desc, struct tag *tags,
			char **cmdline, struct meminfo *mi)
{
	mi->nr_banks = 1;
	mi->bank[0].start = PLAT_PHYS_OFFSET;
	mi->bank[0].size = SZ_1G;
}

MACHINE_START(RK30, "RK30board")
	.boot_params	= PLAT_PHYS_OFFSET + 0x800,
	.fixup		= ft_fixup,
	.map_io		= rk30_map_io,
	.init_irq	= rk30_init_irq,
	.timer		= &rk30_timer,
	.init_machine	= machine_rk30_board_init,
MACHINE_END

