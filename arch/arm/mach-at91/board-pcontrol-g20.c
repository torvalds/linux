/*
 *  Copyright (C) 2010 Christian Glindkamp <christian.glindkamp@taskit.de>
 *                     taskit GmbH
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
/*
 * copied and adjusted from board-stamp9g20.c
 * by Peter Gsellmann <pgsellmann@portner-elektronik.at>
 */

#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/w1-gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/board.h>
#include <mach/at91sam9_smc.h>
#include <mach/stamp9g20.h>

#include "sam9_smc.h"
#include "generic.h"


static void __init pcontrol_g20_map_io(void)
{
	stamp9g20_map_io();

	/* USART0 on ttyS1. (Rx, Tx, CTS, RTS) piggyback  A2 */
	at91_register_uart(AT91SAM9260_ID_US0, 1, ATMEL_UART_CTS
						| ATMEL_UART_RTS);

	/* USART1 on ttyS2. (Rx, Tx, CTS, RTS) isolated RS485  X5 */
	at91_register_uart(AT91SAM9260_ID_US1, 2, ATMEL_UART_CTS
						| ATMEL_UART_RTS);

	/* USART2 on ttyS3. (Rx, Tx)  9bit-Bus  Multidrop-mode  X4 */
	at91_register_uart(AT91SAM9260_ID_US4, 3, 0);
}


static void __init init_irq(void)
{
	at91sam9260_init_interrupts(NULL);
}


static struct sam9_smc_config __initdata pcontrol_smc_config[2] = { {
	.ncs_read_setup		= 16,
	.nrd_setup		= 18,
	.ncs_write_setup	= 16,
	.nwe_setup		= 18,

	.ncs_read_pulse		= 63,
	.nrd_pulse		= 55,
	.ncs_write_pulse	= 63,
	.nwe_pulse		= 55,

	.read_cycle		= 127,
	.write_cycle		= 127,

	.mode			= AT91_SMC_READMODE | AT91_SMC_WRITEMODE
			| AT91_SMC_EXNWMODE_DISABLE | AT91_SMC_BAT_SELECT
			| AT91_SMC_DBW_8 | AT91_SMC_PS_4
			| AT91_SMC_TDFMODE,
	.tdf_cycles		= 3,
}, {
	.ncs_read_setup		= 0,
	.nrd_setup		= 0,
	.ncs_write_setup	= 0,
	.nwe_setup		= 1,

	.ncs_read_pulse		= 8,
	.nrd_pulse		= 8,
	.ncs_write_pulse	= 5,
	.nwe_pulse		= 4,

	.read_cycle		= 8,
	.write_cycle		= 7,

	.mode			= AT91_SMC_READMODE | AT91_SMC_WRITEMODE
			| AT91_SMC_EXNWMODE_DISABLE | AT91_SMC_BAT_SELECT
			| AT91_SMC_DBW_16 | AT91_SMC_PS_8
			| AT91_SMC_TDFMODE,
	.tdf_cycles		= 1,
} };

static void __init add_device_pcontrol(void)
{
	/* configure chip-select 4 (IO compatible to 8051  X4 ) */
	sam9_smc_configure(4, &pcontrol_smc_config[0]);
	/* configure chip-select 7 (FerroRAM 256KiBx16bit MR2A16A  D4 ) */
	sam9_smc_configure(7, &pcontrol_smc_config[1]);
}


/*
 * USB Host port
 */
static struct at91_usbh_data __initdata usbh_data = {
	.ports		= 2,
};


/*
 * USB Device port
 */
static struct at91_udc_data __initdata pcontrol_g20_udc_data = {
	.vbus_pin	= AT91_PIN_PA22,	/* Detect +5V bus voltage */
	.pullup_pin	= AT91_PIN_PA4,		/* K-state, active low */
};


/*
 * MACB Ethernet device
 */
static struct at91_eth_data __initdata macb_data = {
	.phy_irq_pin	= AT91_PIN_PA28,
	.is_rmii	= 1,
};


/*
 * I2C devices: eeprom and phy/switch
 */
static struct i2c_board_info __initdata pcontrol_g20_i2c_devices[] = {
{		/* D7  address width=2, 8KiB */
	I2C_BOARD_INFO("24c64", 0x50)
}, {		/* D8  address width=1, 1 byte has 32 bits! */
	I2C_BOARD_INFO("lan9303", 0x0a)
}, };


/*
 * LEDs
 */
static struct gpio_led pcontrol_g20_leds[] = {
	{
		.name			= "LED1",	/* red  H5 */
		.gpio			= AT91_PIN_PB18,
		.active_low		= 1,
		.default_trigger	= "none",	/* supervisor */
	}, {
		.name			= "LED2",	/* yellow  H7 */
		.gpio			= AT91_PIN_PB19,
		.active_low		= 1,
		.default_trigger	= "mmc0",	/* SD-card activity */
	}, {
		.name			= "LED3",	/* green  H2 */
		.gpio			= AT91_PIN_PB20,
		.active_low		= 1,
		.default_trigger	= "heartbeat",	/* blinky */
	}, {
		.name			= "LED4",	/* red  H3 */
		.gpio			= AT91_PIN_PC6,
		.active_low		= 1,
		.default_trigger	= "none",	/* connection lost */
	}, {
		.name			= "LED5",	/* yellow  H6 */
		.gpio			= AT91_PIN_PC7,
		.active_low		= 1,
		.default_trigger	= "none",	/* unsent data */
	}, {
		.name			= "LED6",	/* green  H1 */
		.gpio			= AT91_PIN_PC9,
		.active_low		= 1,
		.default_trigger	= "none",	/* snafu */
	}
};


/*
 * SPI devices
 */
static struct spi_board_info pcontrol_g20_spi_devices[] = {
	{
		.modalias	= "spidev",	/* HMI port  X4 */
		.chip_select	= 1,
		.max_speed_hz	= 50 * 1000 * 1000,
		.bus_num	= 0,
	}, {
		.modalias	= "spidev",	/* piggyback  A2 */
		.chip_select	= 0,
		.max_speed_hz	= 50 * 1000 * 1000,
		.bus_num	= 1,
	},
};


static void __init pcontrol_g20_board_init(void)
{
	stamp9g20_board_init();
	at91_add_device_usbh(&usbh_data);
	at91_add_device_eth(&macb_data);
	at91_add_device_i2c(pcontrol_g20_i2c_devices,
		ARRAY_SIZE(pcontrol_g20_i2c_devices));
	add_device_pcontrol();
	at91_add_device_spi(pcontrol_g20_spi_devices,
		ARRAY_SIZE(pcontrol_g20_spi_devices));
	at91_add_device_udc(&pcontrol_g20_udc_data);
	at91_gpio_leds(pcontrol_g20_leds,
		ARRAY_SIZE(pcontrol_g20_leds));
	/* piggyback  A2 */
	at91_set_gpio_output(AT91_PIN_PB31, 1);
}


MACHINE_START(PCONTROL_G20, "PControl G20")
	/* Maintainer: pgsellmann@portner-elektronik.at */
	.boot_params	= AT91_SDRAM_BASE + 0x100,
	.timer		= &at91sam926x_timer,
	.map_io		= pcontrol_g20_map_io,
	.init_irq	= init_irq,
	.init_machine	= pcontrol_g20_board_init,
MACHINE_END
