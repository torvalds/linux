/*
 * linux/arch/arm/mach-at91/board-kb9202.c
 *
 *  Copyright (c) 2005 kb_admin
 *  		       KwikByte, Inc.
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


static void __init kb9202_map_io(void)
{
	/* Initialize processor: 10 MHz crystal */
	at91rm9200_initialize(10000000, AT91RM9200_PQFP);

	/* Set up the LEDs */
	at91_init_leds(AT91_PIN_PC19, AT91_PIN_PC18);

	/* DBGU on ttyS0. (Rx & Tx only) */
	at91_register_uart(0, 0, 0);

	/* USART0 on ttyS1 (Rx & Tx only) */
	at91_register_uart(AT91RM9200_ID_US0, 1, 0);

	/* USART1 on ttyS2 (Rx & Tx only) - IRDA (optional) */
	at91_register_uart(AT91RM9200_ID_US1, 2, 0);

	/* USART3 on ttyS3 (Rx, Tx, CTS, RTS) - RS485 (optional) */
	at91_register_uart(AT91RM9200_ID_US3, 3, ATMEL_UART_CTS | ATMEL_UART_RTS);

	/* set serial console to ttyS0 (ie, DBGU) */
	at91_set_serial_console(0);
}

static void __init kb9202_init_irq(void)
{
	at91rm9200_init_interrupts(NULL);
}

static struct at91_eth_data __initdata kb9202_eth_data = {
	.phy_irq_pin	= AT91_PIN_PB29,
	.is_rmii	= 0,
};

static struct at91_usbh_data __initdata kb9202_usbh_data = {
	.ports		= 1,
};

static struct at91_udc_data __initdata kb9202_udc_data = {
	.vbus_pin	= AT91_PIN_PB24,
	.pullup_pin	= AT91_PIN_PB22,
};

static struct at91_mmc_data __initdata kb9202_mmc_data = {
	.det_pin	= AT91_PIN_PB2,
	.slot_b		= 0,
	.wire4		= 1,
};

static struct mtd_partition __initdata kb9202_nand_partition[] = {
	{
		.name	= "nand_fs",
		.offset	= 0,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct mtd_partition * __init nand_partitions(int size, int *num_partitions)
{
	*num_partitions = ARRAY_SIZE(kb9202_nand_partition);
	return kb9202_nand_partition;
}

static struct atmel_nand_data __initdata kb9202_nand_data = {
	.ale		= 22,
	.cle		= 21,
	// .det_pin	= ... not there
	.rdy_pin	= AT91_PIN_PC29,
	.enable_pin	= AT91_PIN_PC28,
	.partition_info	= nand_partitions,
};

static void __init kb9202_board_init(void)
{
	/* Serial */
	at91_add_device_serial();
	/* Ethernet */
	at91_add_device_eth(&kb9202_eth_data);
	/* USB Host */
	at91_add_device_usbh(&kb9202_usbh_data);
	/* USB Device */
	at91_add_device_udc(&kb9202_udc_data);
	/* MMC */
	at91_add_device_mmc(0, &kb9202_mmc_data);
	/* I2C */
	at91_add_device_i2c(NULL, 0);
	/* SPI */
	at91_add_device_spi(NULL, 0);
	/* NAND */
	at91_add_device_nand(&kb9202_nand_data);
}

MACHINE_START(KB9200, "KB920x")
	/* Maintainer: KwikByte, Inc. */
	.boot_params	= AT91_SDRAM_BASE + 0x100,
	.timer		= &at91rm9200_timer,
	.map_io		= kb9202_map_io,
	.init_irq	= kb9202_init_irq,
	.init_machine	= kb9202_board_init,
MACHINE_END
