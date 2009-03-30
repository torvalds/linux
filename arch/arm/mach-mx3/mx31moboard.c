/*
 *  Copyright (C) 2008 Valentin Longchamp, EPFL Mobots group
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

#include <linux/types.h>
#include <linux/init.h>

#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/partitions.h>
#include <linux/memory.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <mach/common.h>
#include <mach/imx-uart.h>
#include <mach/iomux-mx3.h>
#include <mach/board-mx31moboard.h>

#include "devices.h"

static struct physmap_flash_data mx31moboard_flash_data = {
	.width  	= 2,
};

static struct resource mx31moboard_flash_resource = {
	.start	= 0xa0000000,
	.end	= 0xa1ffffff,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device mx31moboard_flash = {
	.name	= "physmap-flash",
	.id	= 0,
	.dev	= {
		.platform_data  = &mx31moboard_flash_data,
	},
	.resource = &mx31moboard_flash_resource,
	.num_resources = 1,
};

static struct imxuart_platform_data uart_pdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static struct platform_device *devices[] __initdata = {
	&mx31moboard_flash,
};

static int mxc_uart0_pins[] = {
	MX31_PIN_CTS1__CTS1, MX31_PIN_RTS1__RTS1,
	MX31_PIN_TXD1__TXD1, MX31_PIN_RXD1__RXD1,
};
static int mxc_uart4_pins[] = {
	MX31_PIN_PC_RST__CTS5, MX31_PIN_PC_VS2__RTS5,
	MX31_PIN_PC_BVD2__TXD5, MX31_PIN_PC_BVD1__RXD5,
};

static int mx31moboard_baseboard;
core_param(mx31moboard_baseboard, mx31moboard_baseboard, int, 0444);

/*
 * Board specific initialization.
 */
static void __init mxc_board_init(void)
{
	platform_add_devices(devices, ARRAY_SIZE(devices));

	mxc_iomux_setup_multiple_pins(mxc_uart0_pins, ARRAY_SIZE(mxc_uart0_pins), "uart0");
	mxc_register_device(&mxc_uart_device0, &uart_pdata);

	mxc_iomux_setup_multiple_pins(mxc_uart4_pins, ARRAY_SIZE(mxc_uart4_pins), "uart4");
	mxc_register_device(&mxc_uart_device4, &uart_pdata);

	switch (mx31moboard_baseboard) {
	case MX31NOBOARD:
		break;
	case MX31DEVBOARD:
		mx31moboard_devboard_init();
		break;
	case MX31MARXBOT:
		mx31moboard_marxbot_init();
		break;
	default:
		printk(KERN_ERR "Illegal mx31moboard_baseboard type %d\n", mx31moboard_baseboard);
	}
}

static void __init mx31moboard_timer_init(void)
{
	mx31_clocks_init(26000000);
}

struct sys_timer mx31moboard_timer = {
	.init	= mx31moboard_timer_init,
};

MACHINE_START(MX31MOBOARD, "EPFL Mobots mx31moboard")
	/* Maintainer: Valentin Longchamp, EPFL Mobots group */
	.phys_io	= AIPS1_BASE_ADDR,
	.io_pg_offst	= ((AIPS1_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params    = PHYS_OFFSET + 0x100,
	.map_io         = mxc_map_io,
	.init_irq       = mxc_init_irq,
	.init_machine   = mxc_board_init,
	.timer          = &mx31moboard_timer,
MACHINE_END

