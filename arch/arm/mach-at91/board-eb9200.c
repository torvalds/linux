/*
 * linux/arch/arm/mach-at91/board-eb9200.c
 *
 *  Copyright (C) 2005 SAN People, adapted for ATEB9200 from Embest
 *  by Andrew Patrikalakis
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
#include <linux/device.h>

#include <mach/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/board.h>
#include <mach/at91_aic.h>

#include "generic.h"


static void __init eb9200_init_early(void)
{
	/* Initialize processor: 18.432 MHz crystal */
	at91_initialize(18432000);
}

static struct macb_platform_data __initdata eb9200_eth_data = {
	.phy_irq_pin	= AT91_PIN_PC4,
	.is_rmii	= 1,
};

static struct at91_usbh_data __initdata eb9200_usbh_data = {
	.ports		= 2,
	.vbus_pin	= {-EINVAL, -EINVAL},
	.overcurrent_pin= {-EINVAL, -EINVAL},
};

static struct at91_udc_data __initdata eb9200_udc_data = {
	.vbus_pin	= AT91_PIN_PD4,
	.pullup_pin	= AT91_PIN_PD5,
};

static struct at91_cf_data __initdata eb9200_cf_data = {
	.irq_pin	= -EINVAL,
	.det_pin	= AT91_PIN_PB0,
	.vcc_pin	= -EINVAL,
	.rst_pin	= AT91_PIN_PC5,
};

static struct at91_mmc_data __initdata eb9200_mmc_data = {
	.slot_b		= 0,
	.wire4		= 1,
	.det_pin	= -EINVAL,
	.wp_pin		= -EINVAL,
	.vcc_pin	= -EINVAL,
};

static struct i2c_board_info __initdata eb9200_i2c_devices[] = {
	{
		I2C_BOARD_INFO("24c512", 0x50),
	},
};


static void __init eb9200_board_init(void)
{
	/* Serial */
	/* DBGU on ttyS0. (Rx & Tx only) */
	at91_register_uart(0, 0, 0);

	/* USART1 on ttyS1. (Rx, Tx, CTS, RTS, DTR, DSR, DCD, RI) */
	at91_register_uart(AT91RM9200_ID_US1, 1, ATMEL_UART_CTS | ATMEL_UART_RTS
			| ATMEL_UART_DTR | ATMEL_UART_DSR | ATMEL_UART_DCD
			| ATMEL_UART_RI);

	/* USART2 on ttyS2. (Rx, Tx) - IRDA */
	at91_register_uart(AT91RM9200_ID_US2, 2, 0);
	at91_add_device_serial();
	/* Ethernet */
	at91_add_device_eth(&eb9200_eth_data);
	/* USB Host */
	at91_add_device_usbh(&eb9200_usbh_data);
	/* USB Device */
	at91_add_device_udc(&eb9200_udc_data);
	/* I2C */
	at91_add_device_i2c(eb9200_i2c_devices, ARRAY_SIZE(eb9200_i2c_devices));
	/* Compact Flash */
	at91_add_device_cf(&eb9200_cf_data);
	/* SPI */
	at91_add_device_spi(NULL, 0);
	/* MMC */
	/* only supports 1 or 4 bit interface, not wired through to SPI */
	at91_add_device_mmc(0, &eb9200_mmc_data);
}

MACHINE_START(ATEB9200, "Embest ATEB9200")
	.timer		= &at91rm9200_timer,
	.map_io		= at91_map_io,
	.handle_irq	= at91_aic_handle_irq,
	.init_early	= eb9200_init_early,
	.init_irq	= at91_init_irq_default,
	.init_machine	= eb9200_board_init,
MACHINE_END
