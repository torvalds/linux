/*
 * linux/arch/arm/mach-at91/board-csb637.c
 *
 *  Copyright (C) 2005 SAN People
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
#include <linux/gpio.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <mach/board.h>
#include <mach/at91_aic.h>

#include "generic.h"


static void __init csb637_init_early(void)
{
	/* Initialize processor: 3.6864 MHz crystal */
	at91_initialize(3686400);
}

static struct macb_platform_data __initdata csb637_eth_data = {
	.phy_irq_pin	= AT91_PIN_PC0,
	.is_rmii	= 0,
};

static struct at91_usbh_data __initdata csb637_usbh_data = {
	.ports		= 2,
	.vbus_pin	= {-EINVAL, -EINVAL},
	.overcurrent_pin= {-EINVAL, -EINVAL},
};

static struct at91_udc_data __initdata csb637_udc_data = {
	.vbus_pin     = AT91_PIN_PB28,
	.pullup_pin   = AT91_PIN_PB1,
};

#define CSB_FLASH_BASE	AT91_CHIPSELECT_0
#define CSB_FLASH_SIZE	SZ_16M

static struct mtd_partition csb_flash_partitions[] = {
	{
		.name		= "uMON flash",
		.offset		= 0,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= MTD_WRITEABLE,	/* read only */
	}
};

static struct physmap_flash_data csb_flash_data = {
	.width		= 2,
	.parts		= csb_flash_partitions,
	.nr_parts	= ARRAY_SIZE(csb_flash_partitions),
};

static struct resource csb_flash_resources[] = {
	{
		.start	= CSB_FLASH_BASE,
		.end	= CSB_FLASH_BASE + CSB_FLASH_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device csb_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
				.platform_data = &csb_flash_data,
			},
	.resource	= csb_flash_resources,
	.num_resources	= ARRAY_SIZE(csb_flash_resources),
};

static struct gpio_led csb_leds[] = {
	{	/* "d1", red */
		.name			= "d1",
		.gpio			= AT91_PIN_PB2,
		.active_low		= 1,
		.default_trigger	= "heartbeat",
	},
};

static void __init csb637_board_init(void)
{
	/* LED(s) */
	at91_gpio_leds(csb_leds, ARRAY_SIZE(csb_leds));
	/* Serial */
	/* DBGU on ttyS0. (Rx & Tx only) */
	at91_register_uart(0, 0, 0);
	at91_add_device_serial();
	/* Ethernet */
	at91_add_device_eth(&csb637_eth_data);
	/* USB Host */
	at91_add_device_usbh(&csb637_usbh_data);
	/* USB Device */
	at91_add_device_udc(&csb637_udc_data);
	/* I2C */
	at91_add_device_i2c(NULL, 0);
	/* SPI */
	at91_add_device_spi(NULL, 0);
	/* NOR flash */
	platform_device_register(&csb_flash);
}

MACHINE_START(CSB637, "Cogent CSB637")
	/* Maintainer: Bill Gatliff */
	.timer		= &at91rm9200_timer,
	.map_io		= at91_map_io,
	.handle_irq	= at91_aic_handle_irq,
	.init_early	= csb637_init_early,
	.init_irq	= at91_init_irq_default,
	.init_machine	= csb637_board_init,
MACHINE_END
