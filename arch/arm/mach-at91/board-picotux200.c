/*
 * linux/arch/arm/mach-at91/board-picotux200.c
 *
 *  Copyright (C) 2005 SAN People
 *  Copyright (C) 2007 Kleinhenz Elektronik GmbH
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
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/mtd/physmap.h>

#include <mach/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/board.h>
#include <mach/gpio.h>
#include <mach/at91rm9200_mc.h>

#include "generic.h"


static void __init picotux200_map_io(void)
{
	/* Initialize processor: 18.432 MHz crystal */
	at91rm9200_initialize(18432000, AT91RM9200_BGA);

	/* DBGU on ttyS0. (Rx & Tx only) */
	at91_register_uart(0, 0, 0);

	/* USART1 on ttyS1. (Rx, Tx, CTS, RTS, DTR, DSR, DCD, RI) */
	at91_register_uart(AT91RM9200_ID_US1, 1, ATMEL_UART_CTS | ATMEL_UART_RTS
			  | ATMEL_UART_DTR | ATMEL_UART_DSR | ATMEL_UART_DCD
			  | ATMEL_UART_RI);

	/* set serial console to ttyS0 (ie, DBGU) */
	at91_set_serial_console(0);
}

static void __init picotux200_init_irq(void)
{
	at91rm9200_init_interrupts(NULL);
}

static struct at91_eth_data __initdata picotux200_eth_data = {
	.phy_irq_pin	= AT91_PIN_PC4,
	.is_rmii	= 1,
};

static struct at91_usbh_data __initdata picotux200_usbh_data = {
	.ports		= 1,
};

// static struct at91_udc_data __initdata picotux200_udc_data = {
// 	.vbus_pin	= AT91_PIN_PD4,
// 	.pullup_pin	= AT91_PIN_PD5,
// };

static struct at91_mmc_data __initdata picotux200_mmc_data = {
	.det_pin	= AT91_PIN_PB27,
	.slot_b		= 0,
	.wire4		= 1,
	.wp_pin		= AT91_PIN_PA17,
};

// static struct spi_board_info picotux200_spi_devices[] = {
// 	{	/* DataFlash chip */
// 		.modalias	= "mtd_dataflash",
// 		.chip_select	= 0,
// 		.max_speed_hz	= 15 * 1000 * 1000,
// 	},
// #ifdef CONFIG_MTD_AT91_DATAFLASH_CARD
// 	{	/* DataFlash card */
// 		.modalias	= "mtd_dataflash",
// 		.chip_select	= 3,
// 		.max_speed_hz	= 15 * 1000 * 1000,
// 	},
// #endif
// };

#define PICOTUX200_FLASH_BASE	AT91_CHIPSELECT_0
#define PICOTUX200_FLASH_SIZE	SZ_4M

static struct physmap_flash_data picotux200_flash_data = {
	.width	= 2,
};

static struct resource picotux200_flash_resource = {
	.start		= PICOTUX200_FLASH_BASE,
	.end		= PICOTUX200_FLASH_BASE + PICOTUX200_FLASH_SIZE - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device picotux200_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
				.platform_data	= &picotux200_flash_data,
			},
	.resource	= &picotux200_flash_resource,
	.num_resources	= 1,
};

static void __init picotux200_board_init(void)
{
	/* Serial */
	at91_add_device_serial();
	/* Ethernet */
	at91_add_device_eth(&picotux200_eth_data);
	/* USB Host */
	at91_add_device_usbh(&picotux200_usbh_data);
	/* USB Device */
	// at91_add_device_udc(&picotux200_udc_data);
	// at91_set_multi_drive(picotux200_udc_data.pullup_pin, 1);	/* pullup_pin is connected to reset */
	/* I2C */
	at91_add_device_i2c(NULL, 0);
	/* SPI */
	// at91_add_device_spi(picotux200_spi_devices, ARRAY_SIZE(picotux200_spi_devices));
#ifdef CONFIG_MTD_AT91_DATAFLASH_CARD
	/* DataFlash card */
	at91_set_gpio_output(AT91_PIN_PB22, 0);
#else
	/* MMC */
	at91_set_gpio_output(AT91_PIN_PB22, 1);	/* this MMC card slot can optionally use SPI signaling (CS3). */
	at91_add_device_mmc(0, &picotux200_mmc_data);
#endif
	/* NOR Flash */
	platform_device_register(&picotux200_flash);
}

MACHINE_START(PICOTUX2XX, "picotux 200")
	/* Maintainer: Kleinhenz Elektronik GmbH */
	.boot_params	= AT91_SDRAM_BASE + 0x100,
	.timer		= &at91rm9200_timer,
	.map_io		= picotux200_map_io,
	.init_irq	= picotux200_init_irq,
	.init_machine	= picotux200_board_init,
MACHINE_END
