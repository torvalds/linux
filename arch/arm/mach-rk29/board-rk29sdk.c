/* arch/arm/mach-rk29/board-rk29.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
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
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/mmc/host.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>
#include <asm/hardware/gic.h>

#include <mach/irqs.h>
#include <mach/rk29_iomap.h>
#include <mach/board.h>


#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

extern struct sys_timer rk29_timer;

static void __init rk29_gic_init_irq(void)
{
	gic_dist_init(0, RK29_GICPERI_BASE, 32);
	gic_cpu_init(0, RK29_GICCPU_BASE);
}

static void __init machine_rk29_init_irq(void)
{
	rk29_gic_init_irq();
	//rk29_gpio_init(rk29_gpioBank, 8);
	//rk29_gpio_irq_setup();
}
static void __init machine_rk29_board_init(void)
{
	
}

static void __init machine_rk29_mapio(void)
{
	rk29_map_common_io();
	//rk29_clock_init();
	//rk29_iomux_init();	
}

MACHINE_START(RK29, "RK29board")

/* UART for LL DEBUG */
	.phys_io	= RK29_UART1_PHYS, 
	.io_pg_offst	= ((RK29_ADDR_BASE0) >> 18) & 0xfffc,
	.boot_params	= RK29_SDRAM_PHYS + 0xf8000,
	.map_io		= machine_rk29_mapio,
	.init_irq	= machine_rk29_init_irq,
	.init_machine	= machine_rk29_board_init,
	.timer		= &rk29_timer,
MACHINE_END
