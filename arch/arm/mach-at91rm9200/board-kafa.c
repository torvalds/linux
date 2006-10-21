/*
 * linux/arch/arm/mach-at91rm9200/board-kafa.c
 *
 *  Copyright (C) 2006 Sperry-Sun
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

#include <asm/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/arch/board.h>
#include <asm/arch/gpio.h>

#include "generic.h"


/*
 * Serial port configuration.
 *    0 .. 3 = USART0 .. USART3
 *    4      = DBGU
 */
static struct at91_uart_config __initdata kafa_uart_config = {
	.console_tty	= 0,				/* ttyS0 */
	.nr_tty		= 2,
	.tty_map	= { 4, 0, -1, -1, -1 }		/* ttyS0, ..., ttyS4 */
};

static void __init kafa_map_io(void)
{
	/* Initialize processor: 18.432 MHz crystal */
	at91rm9200_initialize(18432000, AT91RM9200_PQFP);

	/* Set up the LEDs */
	at91_init_leds(AT91_PIN_PB4, AT91_PIN_PB4);

	/* Setup the serial ports and console */
	at91_init_serial(&kafa_uart_config);
}

static void __init kafa_init_irq(void)
{
	at91rm9200_init_interrupts(NULL);
}

static struct at91_eth_data __initdata kafa_eth_data = {
	.phy_irq_pin	= AT91_PIN_PC4,
	.is_rmii	= 0,
};

static struct at91_usbh_data __initdata kafa_usbh_data = {
	.ports		= 1,
};

static struct at91_udc_data __initdata kafa_udc_data = {
	.vbus_pin	= AT91_PIN_PB6,
	.pullup_pin	= AT91_PIN_PB7,
};

static void __init kafa_board_init(void)
{
	/* Serial */
	at91_add_device_serial();
	/* Ethernet */
	at91_add_device_eth(&kafa_eth_data);
	/* USB Host */
	at91_add_device_usbh(&kafa_usbh_data);
	/* USB Device */
	at91_add_device_udc(&kafa_udc_data);
	/* I2C */
	at91_add_device_i2c();
	/* SPI */
	at91_add_device_spi(NULL, 0);
}

MACHINE_START(KAFA, "Sperry-Sun KAFA")
	/* Maintainer: Sergei Sharonov */
	.phys_io	= AT91_BASE_SYS,
	.io_pg_offst	= (AT91_VA_BASE_SYS >> 18) & 0xfffc,
	.boot_params	= AT91_SDRAM_BASE + 0x100,
	.timer		= &at91rm9200_timer,
	.map_io		= kafa_map_io,
	.init_irq	= kafa_init_irq,
	.init_machine	= kafa_board_init,
MACHINE_END
