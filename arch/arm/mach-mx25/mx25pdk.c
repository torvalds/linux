/*
 * Copyright 2009 Sascha Hauer, <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/smsc911x.h>
#include <linux/platform_device.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/memory.h>
#include <asm/mach/map.h>
#include <mach/common.h>
#include <mach/imx-uart.h>
#include <mach/mx25.h>
#include <mach/mxc_nand.h>
#include "devices.h"
#include <mach/iomux-v3.h>

static struct imxuart_platform_data uart_pdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static void __init mx25pdk_init(void)
{
	mxc_register_device(&mxc_uart_device0, &uart_pdata);
	mxc_register_device(&mxc_usbh2, NULL);
}

static void __init mx25pdk_timer_init(void)
{
	mx25_clocks_init(26000000);
}

static struct sys_timer mx25pdk_timer = {
	.init   = mx25pdk_timer_init,
};

MACHINE_START(MX25_3DS, "Freescale MX25PDK (3DS)")
	/* Maintainer: Freescale Semiconductor, Inc. */
	.phys_io	= MX25_AIPS1_BASE_ADDR,
	.io_pg_offst	= ((MX25_AIPS1_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params    = PHYS_OFFSET + 0x100,
	.map_io         = mx25_map_io,
	.init_irq       = mx25_init_irq,
	.init_machine   = mx25pdk_init,
	.timer          = &mx25pdk_timer,
MACHINE_END

