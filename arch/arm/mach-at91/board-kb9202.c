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
#include <linux/gpio.h>
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

#include <mach/cpu.h>
#include <mach/at91rm9200_mc.h>
#include <mach/at91_ramc.h>

#include "at91_aic.h"
#include "board.h"
#include "generic.h"


static void __init kb9202_init_early(void)
{
	/* Set cpu type: PQFP */
	at91rm9200_set_type(ARCH_REVISON_9200_PQFP);

	/* Initialize processor: 10 MHz crystal */
	at91_initialize(10000000);
}

static struct macb_platform_data __initdata kb9202_eth_data = {
	.phy_irq_pin	= AT91_PIN_PB29,
	.is_rmii	= 0,
};

static struct at91_usbh_data __initdata kb9202_usbh_data = {
	.ports		= 1,
	.vbus_pin	= {-EINVAL, -EINVAL},
	.overcurrent_pin= {-EINVAL, -EINVAL},
};

static struct at91_udc_data __initdata kb9202_udc_data = {
	.vbus_pin	= AT91_PIN_PB24,
	.pullup_pin	= AT91_PIN_PB22,
};

static struct mci_platform_data __initdata kb9202_mci0_data = {
	.slot[0] = {
		.bus_width	= 4,
		.detect_pin	= AT91_PIN_PB2,
		.wp_pin		= -EINVAL,
	},
};

static struct mtd_partition __initdata kb9202_nand_partition[] = {
	{
		.name	= "nand_fs",
		.offset	= 0,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct atmel_nand_data __initdata kb9202_nand_data = {
	.ale		= 22,
	.cle		= 21,
	.det_pin	= -EINVAL,
	.rdy_pin	= AT91_PIN_PC29,
	.enable_pin	= AT91_PIN_PC28,
	.ecc_mode	= NAND_ECC_SOFT,
	.parts		= kb9202_nand_partition,
	.num_parts	= ARRAY_SIZE(kb9202_nand_partition),
};

/*
 * LEDs
 */
static struct gpio_led kb9202_leds[] = {
	{	/* D1 */
		.name			= "led1",
		.gpio			= AT91_PIN_PC19,
		.active_low		= 1,
		.default_trigger	= "heartbeat",
	},
	{	/* D2 */
		.name			= "led2",
		.gpio			= AT91_PIN_PC18,
		.active_low		= 1,
		.default_trigger	= "timer",
	}
};

static void __init kb9202_board_init(void)
{
	/* Serial */
	/* DBGU on ttyS0. (Rx & Tx only) */
	at91_register_uart(0, 0, 0);

	/* USART0 on ttyS1 (Rx & Tx only) */
	at91_register_uart(AT91RM9200_ID_US0, 1, 0);

	/* USART1 on ttyS2 (Rx & Tx only) - IRDA (optional) */
	at91_register_uart(AT91RM9200_ID_US1, 2, 0);

	/* USART3 on ttyS3 (Rx, Tx, CTS, RTS) - RS485 (optional) */
	at91_register_uart(AT91RM9200_ID_US3, 3, ATMEL_UART_CTS | ATMEL_UART_RTS);
	at91_add_device_serial();
	/* Ethernet */
	at91_add_device_eth(&kb9202_eth_data);
	/* USB Host */
	at91_add_device_usbh(&kb9202_usbh_data);
	/* USB Device */
	at91_add_device_udc(&kb9202_udc_data);
	/* MMC */
	at91_add_device_mci(0, &kb9202_mci0_data);
	/* I2C */
	at91_add_device_i2c(NULL, 0);
	/* SPI */
	at91_add_device_spi(NULL, 0);
	/* NAND */
	at91_add_device_nand(&kb9202_nand_data);
	/* LEDs */
	at91_gpio_leds(kb9202_leds, ARRAY_SIZE(kb9202_leds));
}

MACHINE_START(KB9200, "KB920x")
	/* Maintainer: KwikByte, Inc. */
	.init_time	= at91rm9200_timer_init,
	.map_io		= at91_map_io,
	.handle_irq	= at91_aic_handle_irq,
	.init_early	= kb9202_init_early,
	.init_irq	= at91_init_irq_default,
	.init_machine	= kb9202_board_init,
MACHINE_END
