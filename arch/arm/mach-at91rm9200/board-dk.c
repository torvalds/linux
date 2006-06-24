/*
 * linux/arch/arm/mach-at91rm9200/board-dk.c
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
#include <linux/spi/spi.h>

#include <asm/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/hardware.h>
#include <asm/arch/board.h>
#include <asm/arch/gpio.h>

#include "generic.h"

static void __init dk_init_irq(void)
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
static struct at91_uart_config __initdata dk_uart_config = {
	.console_tty	= 0,				/* ttyS0 */
	.nr_tty		= 2,
	.tty_map	= { 4, 1, -1, -1, -1 }		/* ttyS0, ..., ttyS4 */
};

static void __init dk_map_io(void)
{
	at91rm9200_map_io();

	/* Initialize clocks: 18.432 MHz crystal */
	at91_clock_init(18432000);

	/* Setup the LEDs */
	at91_init_leds(AT91_PIN_PB2, AT91_PIN_PB2);

	/* Setup the serial ports and console */
	at91_init_serial(&dk_uart_config);
}

static struct at91_eth_data __initdata dk_eth_data = {
	.phy_irq_pin	= AT91_PIN_PC4,
	.is_rmii	= 1,
};

static struct at91_usbh_data __initdata dk_usbh_data = {
	.ports		= 2,
};

static struct at91_udc_data __initdata dk_udc_data = {
	.vbus_pin	= AT91_PIN_PD4,
	.pullup_pin	= AT91_PIN_PD5,
};

static struct at91_cf_data __initdata dk_cf_data = {
	.det_pin	= AT91_PIN_PB0,
	.rst_pin	= AT91_PIN_PC5,
	// .irq_pin	= ... not connected
	// .vcc_pin	= ... always powered
};

static struct at91_mmc_data __initdata dk_mmc_data = {
	.is_b		= 0,
	.wire4		= 1,
};

static struct spi_board_info dk_spi_devices[] = {
	{	/* DataFlash chip */
		.modalias	= "mtd_dataflash",
		.chip_select	= 0,
		.max_speed_hz	= 15 * 1000 * 1000,
	},
	{	/* UR6HCPS2-SP40 PS2-to-SPI adapter */
		.modalias	= "ur6hcps2",
		.chip_select	= 1,
		.max_speed_hz	= 250 *  1000,
	},
	{	/* TLV1504 ADC, 4 channels, 10 bits; one is a temp sensor */
		.modalias	= "tlv1504",
		.chip_select	= 2,
		.max_speed_hz	= 20 * 1000 * 1000,
	},
#ifdef CONFIG_MTD_AT91_DATAFLASH_CARD
	{	/* DataFlash card */
		.modalias	= "mtd_dataflash",
		.chip_select	= 3,
		.max_speed_hz	= 15 * 1000 * 1000,
	}
#endif
};

static void __init dk_board_init(void)
{
	/* Serial */
	at91_add_device_serial();
	/* Ethernet */
	at91_add_device_eth(&dk_eth_data);
	/* USB Host */
	at91_add_device_usbh(&dk_usbh_data);
	/* USB Device */
	at91_add_device_udc(&dk_udc_data);
	at91_set_multi_drive(dk_udc_data.pullup_pin, 1);	/* pullup_pin is connected to reset */
	/* Compact Flash */
	at91_add_device_cf(&dk_cf_data);
	/* I2C */
	at91_add_device_i2c();
	/* SPI */
	at91_add_device_spi(dk_spi_devices, ARRAY_SIZE(dk_spi_devices));
#ifdef CONFIG_MTD_AT91_DATAFLASH_CARD
	/* DataFlash card */
	at91_set_gpio_output(AT91_PIN_PB7, 0);
#else
	/* MMC */
	at91_set_gpio_output(AT91_PIN_PB7, 1);	/* this MMC card slot can optionally use SPI signaling (CS3). */
	at91_add_device_mmc(&dk_mmc_data);
#endif
	/* VGA */
//	dk_add_device_video();
}

MACHINE_START(AT91RM9200DK, "Atmel AT91RM9200-DK")
	/* Maintainer: SAN People/Atmel */
	.phys_io	= AT91_BASE_SYS,
	.io_pg_offst	= (AT91_VA_BASE_SYS >> 18) & 0xfffc,
	.boot_params	= AT91_SDRAM_BASE + 0x100,
	.timer		= &at91rm9200_timer,
	.map_io		= dk_map_io,
	.init_irq	= dk_init_irq,
	.init_machine	= dk_board_init,
MACHINE_END
