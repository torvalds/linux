/*
 *  Copyright (C) 2008 Sascha Hauer, Pengutronix
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
#include <linux/mtd/plat-ram.h>
#include <linux/memory.h>
#include <linux/gpio.h>
#include <linux/smc911x.h>
#include <linux/interrupt.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <mach/common.h>
#include <mach/imx-uart.h>
#include <mach/iomux-mx3.h>
#include <mach/board-pcm037.h>
#include <mach/mxc_nand.h>

#include "devices.h"

static struct physmap_flash_data pcm037_flash_data = {
	.width  = 2,
};

static struct resource pcm037_flash_resource = {
	.start	= 0xa0000000,
	.end	= 0xa1ffffff,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device pcm037_flash = {
	.name	= "physmap-flash",
	.id	= 0,
	.dev	= {
		.platform_data  = &pcm037_flash_data,
	},
	.resource = &pcm037_flash_resource,
	.num_resources = 1,
};

static struct imxuart_platform_data uart_pdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static struct resource smc911x_resources[] = {
	[0] = {
		.start		= CS1_BASE_ADDR + 0x300,
		.end		= CS1_BASE_ADDR + 0x300 + SZ_64K - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= IOMUX_TO_IRQ(MX31_PIN_GPIO3_1),
		.end		= IOMUX_TO_IRQ(MX31_PIN_GPIO3_1),
		.flags		= IORESOURCE_IRQ,
	},
};

static struct smc911x_platdata smc911x_info = {
	.flags		= SMC911X_USE_32BIT,
	.irq_flags	= IRQF_SHARED | IRQF_TRIGGER_LOW,
};

static struct platform_device pcm037_eth = {
	.name		= "smc911x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smc911x_resources),
	.resource	= smc911x_resources,
	.dev		= {
		.platform_data = &smc911x_info,
	},
};

static struct platdata_mtd_ram pcm038_sram_data = {
	.bankwidth = 2,
};

static struct resource pcm038_sram_resource = {
	.start = CS4_BASE_ADDR,
	.end   = CS4_BASE_ADDR + 512 * 1024 - 1,
	.flags = IORESOURCE_MEM,
};

static struct platform_device pcm037_sram_device = {
	.name = "mtd-ram",
	.id = 0,
	.dev = {
		.platform_data = &pcm038_sram_data,
	},
	.num_resources = 1,
	.resource = &pcm038_sram_resource,
};

static struct mxc_nand_platform_data pcm037_nand_board_info = {
	.width = 1,
	.hw_ecc = 1,
};

static struct platform_device *devices[] __initdata = {
	&pcm037_flash,
	&pcm037_eth,
	&pcm037_sram_device,
};

/*
 * Board specific initialization.
 */
static void __init mxc_board_init(void)
{
	platform_add_devices(devices, ARRAY_SIZE(devices));

	mxc_iomux_mode(MX31_PIN_CTS1__CTS1);
	mxc_iomux_mode(MX31_PIN_RTS1__RTS1);
	mxc_iomux_mode(MX31_PIN_TXD1__TXD1);
	mxc_iomux_mode(MX31_PIN_RXD1__RXD1);

	mxc_register_device(&mxc_uart_device0, &uart_pdata);

	mxc_iomux_mode(MX31_PIN_CSPI3_MOSI__RXD3);
	mxc_iomux_mode(MX31_PIN_CSPI3_MISO__TXD3);

	mxc_register_device(&mxc_uart_device2, &uart_pdata);

	mxc_iomux_mode(MX31_PIN_BATT_LINE__OWIRE);
	mxc_register_device(&mxc_w1_master_device, NULL);

	/* SMSC9215 IRQ pin */
	mxc_iomux_mode(IOMUX_MODE(MX31_PIN_GPIO3_1, IOMUX_CONFIG_GPIO));
	if (!gpio_request(MX31_PIN_GPIO3_1, "pcm037-eth"))
		gpio_direction_input(MX31_PIN_GPIO3_1);

	mxc_register_device(&mxc_nand_device, &pcm037_nand_board_info);
}

/*
 * This structure defines static mappings for the pcm037 board.
 */
static struct map_desc pcm037_io_desc[] __initdata = {
	{
		.virtual	= AIPS1_BASE_ADDR_VIRT,
		.pfn		= __phys_to_pfn(AIPS1_BASE_ADDR),
		.length		= AIPS1_SIZE,
		.type		= MT_DEVICE_NONSHARED
	}, {
		.virtual	= AIPS2_BASE_ADDR_VIRT,
		.pfn		= __phys_to_pfn(AIPS2_BASE_ADDR),
		.length		= AIPS2_SIZE,
		.type		= MT_DEVICE_NONSHARED
	},
};

/*
 * Set up static virtual mappings.
 */
void __init pcm037_map_io(void)
{
	mxc_map_io();
	iotable_init(pcm037_io_desc, ARRAY_SIZE(pcm037_io_desc));
}

static void __init pcm037_timer_init(void)
{
	mxc_clocks_init(26000000);
	mxc_timer_init("ipg_clk.0");
}

struct sys_timer pcm037_timer = {
	.init	= pcm037_timer_init,
};

MACHINE_START(PCM037, "Phytec Phycore pcm037")
	/* Maintainer: Pengutronix */
	.phys_io	= AIPS1_BASE_ADDR,
	.io_pg_offst	= ((AIPS1_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params    = PHYS_OFFSET + 0x100,
	.map_io         = pcm037_map_io,
	.init_irq       = mxc_init_irq,
	.init_machine   = mxc_board_init,
	.timer          = &pcm037_timer,
MACHINE_END

