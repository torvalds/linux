/*
 * linux/arch/arm/mach-at91rm9200/board-ek.c
 *
 *  Copyright (C) 2005 SAN People
 *
 *  Epson S1D framebuffer glue code is:
 *     Copyright (C) 2005 Thibaut VARENE <varenet@parisc-linux.org>
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

#include <linux/config.h>
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

#include <asm/arch/hardware.h>
#include <asm/mach/serial_at91rm9200.h>
#include <asm/arch/board.h>

#include "generic.h"

static void __init ek_init_irq(void)
{
	/* Initialize AIC controller */
	at91rm9200_init_irq(NULL);

	/* Set up the GPIO interrupts */
	at91_gpio_irq_setup(BGA_GPIO_BANKS);
}

/*
 * Serial port configuration.
 *    0 .. 3 = USART0 .. USART3
 *    4      = DBGU
 */
#define EK_UART_MAP		{ 4, 1, -1, -1, -1 }	/* ttyS0, ..., ttyS4 */
#define EK_SERIAL_CONSOLE	0			/* ttyS0 */

static void __init ek_map_io(void)
{
	int serial[AT91_NR_UART] = EK_UART_MAP;
	int i;

	at91rm9200_map_io();

	/* Initialize clocks: 18.432 MHz crystal */
	at91_clock_init(18432000);

	/* Setup the LEDs */
	at91_init_leds(AT91_PIN_PB1, AT91_PIN_PB2);

#ifdef CONFIG_SERIAL_AT91
	at91_console_port = EK_SERIAL_CONSOLE;
	memcpy(at91_serial_map, serial, sizeof(serial));

	/* Register UARTs */
	for (i = 0; i < AT91_NR_UART; i++) {
		if (serial[i] >= 0)
			at91_register_uart(i, serial[i]);
	}
#endif
}

static struct at91_eth_data __initdata ek_eth_data = {
	.phy_irq_pin	= AT91_PIN_PC4,
	.is_rmii	= 1,
};

static struct at91_usbh_data __initdata ek_usbh_data = {
	.ports		= 2,
};

static struct at91_udc_data __initdata ek_udc_data = {
	.vbus_pin	= AT91_PIN_PD4,
	.pullup_pin	= AT91_PIN_PD5,
};

static struct at91_mmc_data __initdata ek_mmc_data = {
	.det_pin	= AT91_PIN_PB27,
	.is_b		= 0,
	.wire4		= 1,
	.wp_pin		= AT91_PIN_PA17,
};

static void __init ek_board_init(void)
{
	/* Ethernet */
	at91_add_device_eth(&ek_eth_data);
	/* USB Host */
	at91_add_device_usbh(&ek_usbh_data);
	/* USB Device */
	at91_add_device_udc(&ek_udc_data);
	/* MMC */
	at91_set_gpio_output(AT91_PIN_PB22, 1);	/* this MMC card slot can optionally use SPI signaling (CS3). default: MMC */
	at91_add_device_mmc(&ek_mmc_data);
	/* VGA */
//	ek_add_device_video();
}

MACHINE_START(AT91RM9200EK, "Atmel AT91RM9200-EK")
	/* Maintainer: SAN People/Atmel */
	.phys_io	= AT91_BASE_SYS,
	.io_pg_offst	= (AT91_VA_BASE_SYS >> 18) & 0xfffc,
	.boot_params	= AT91_SDRAM_BASE + 0x100,
	.timer		= &at91rm9200_timer,
	.map_io		= ek_map_io,
	.init_irq	= ek_init_irq,
	.init_machine	= ek_board_init,
MACHINE_END
