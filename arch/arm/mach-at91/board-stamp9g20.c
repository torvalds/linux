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

#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/w1-gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/board.h>
#include <mach/at91sam9_smc.h>

#include "sam9_smc.h"
#include "generic.h"


static void __init portuxg20_map_io(void)
{
	/* Initialize processor: 18.432 MHz crystal */
	at91sam9260_initialize(18432000);

	/* DGBU on ttyS0. (Rx & Tx only) */
	at91_register_uart(0, 0, 0);

	/* USART0 on ttyS1. (Rx, Tx, CTS, RTS, DTR, DSR, DCD, RI) */
	at91_register_uart(AT91SAM9260_ID_US0, 1, ATMEL_UART_CTS | ATMEL_UART_RTS
						| ATMEL_UART_DTR | ATMEL_UART_DSR
						| ATMEL_UART_DCD | ATMEL_UART_RI);

	/* USART1 on ttyS2. (Rx, Tx, CTS, RTS) */
	at91_register_uart(AT91SAM9260_ID_US1, 2, ATMEL_UART_CTS | ATMEL_UART_RTS);

	/* USART2 on ttyS3. (Rx, Tx, CTS, RTS) */
	at91_register_uart(AT91SAM9260_ID_US2, 3, ATMEL_UART_CTS | ATMEL_UART_RTS);

	/* USART4 on ttyS5. (Rx, Tx only) */
	at91_register_uart(AT91SAM9260_ID_US4, 5, 0);

	/* USART5 on ttyS6. (Rx, Tx only) */
	at91_register_uart(AT91SAM9260_ID_US5, 6, 0);

	/* set serial console to ttyS0 (ie, DBGU) */
	at91_set_serial_console(0);
}

static void __init stamp9g20_map_io(void)
{
	/* Initialize processor: 18.432 MHz crystal */
	at91sam9260_initialize(18432000);

	/* DGBU on ttyS0. (Rx & Tx only) */
	at91_register_uart(0, 0, 0);

	/* USART0 on ttyS1. (Rx, Tx, CTS, RTS, DTR, DSR, DCD, RI) */
	at91_register_uart(AT91SAM9260_ID_US0, 1, ATMEL_UART_CTS | ATMEL_UART_RTS
						| ATMEL_UART_DTR | ATMEL_UART_DSR
						| ATMEL_UART_DCD | ATMEL_UART_RI);

	/* set serial console to ttyS0 (ie, DBGU) */
	at91_set_serial_console(0);
}

static void __init init_irq(void)
{
	at91sam9260_init_interrupts(NULL);
}


/*
 * NAND flash
 */
static struct atmel_nand_data __initdata nand_data = {
	.ale		= 21,
	.cle		= 22,
	.rdy_pin	= AT91_PIN_PC13,
	.enable_pin	= AT91_PIN_PC14,
	.bus_width_16	= 0,
};

static struct sam9_smc_config __initdata nand_smc_config = {
	.ncs_read_setup		= 0,
	.nrd_setup		= 2,
	.ncs_write_setup	= 0,
	.nwe_setup		= 2,

	.ncs_read_pulse		= 4,
	.nrd_pulse		= 4,
	.ncs_write_pulse	= 4,
	.nwe_pulse		= 4,

	.read_cycle		= 7,
	.write_cycle		= 7,

	.mode			= AT91_SMC_READMODE | AT91_SMC_WRITEMODE | AT91_SMC_EXNWMODE_DISABLE | AT91_SMC_DBW_8,
	.tdf_cycles		= 3,
};

static void __init add_device_nand(void)
{
	/* configure chip-select 3 (NAND) */
	sam9_smc_configure(3, &nand_smc_config);

	at91_add_device_nand(&nand_data);
}


/*
 * MCI (SD/MMC)
 * det_pin, wp_pin and vcc_pin are not connected
 */
#if defined(CONFIG_MMC_ATMELMCI) || defined(CONFIG_MMC_ATMELMCI_MODULE)
static struct mci_platform_data __initdata mmc_data = {
	.slot[0] = {
		.bus_width	= 4,
	},
};
#else
static struct at91_mmc_data __initdata mmc_data = {
	.slot_b		= 0,
	.wire4		= 1,
};
#endif


/*
 * USB Host port
 */
static struct at91_usbh_data __initdata usbh_data = {
	.ports		= 2,
};


/*
 * USB Device port
 */
static struct at91_udc_data __initdata portuxg20_udc_data = {
	.vbus_pin	= AT91_PIN_PC7,
	.pullup_pin	= 0,		/* pull-up driven by UDC */
};

static struct at91_udc_data __initdata stamp9g20_udc_data = {
	.vbus_pin	= AT91_PIN_PA22,
	.pullup_pin	= 0,		/* pull-up driven by UDC */
};


/*
 * MACB Ethernet device
 */
static struct at91_eth_data __initdata macb_data = {
	.phy_irq_pin	= AT91_PIN_PA28,
	.is_rmii	= 1,
};


/*
 * LEDs
 */
static struct gpio_led portuxg20_leds[] = {
	{
		.name			= "LED2",
		.gpio			= AT91_PIN_PC5,
		.default_trigger	= "none",
	}, {
		.name			= "LED3",
		.gpio			= AT91_PIN_PC4,
		.default_trigger	= "none",
	}, {
		.name			= "LED4",
		.gpio			= AT91_PIN_PC10,
		.default_trigger	= "heartbeat",
	}
};

static struct gpio_led stamp9g20_leds[] = {
	{
		.name			= "D8",
		.gpio			= AT91_PIN_PB18,
		.active_low		= 1,
		.default_trigger	= "none",
	}, {
		.name			= "D9",
		.gpio			= AT91_PIN_PB19,
		.active_low		= 1,
		.default_trigger	= "none",
	}, {
		.name			= "D10",
		.gpio			= AT91_PIN_PB20,
		.active_low		= 1,
		.default_trigger	= "heartbeat",
	}
};


/*
 * SPI devices
 */
static struct spi_board_info portuxg20_spi_devices[] = {
	{
		.modalias	= "spidev",
		.chip_select	= 0,
		.max_speed_hz	= 1 * 1000 * 1000,
		.bus_num	= 0,
	}, {
		.modalias	= "spidev",
		.chip_select	= 0,
		.max_speed_hz	= 1 * 1000 * 1000,
		.bus_num	= 1,
	},
};


/*
 * Dallas 1-Wire
 */
static struct w1_gpio_platform_data w1_gpio_pdata = {
	.pin		= AT91_PIN_PA29,
	.is_open_drain	= 1,
};

static struct platform_device w1_device = {
	.name			= "w1-gpio",
	.id			= -1,
	.dev.platform_data	= &w1_gpio_pdata,
};

void add_w1(void)
{
	at91_set_GPIO_periph(w1_gpio_pdata.pin, 1);
	at91_set_multi_drive(w1_gpio_pdata.pin, 1);
	platform_device_register(&w1_device);
}


static void __init generic_board_init(void)
{
	/* Serial */
	at91_add_device_serial();
	/* NAND */
	add_device_nand();
	/* MMC */
#if defined(CONFIG_MMC_ATMELMCI) || defined(CONFIG_MMC_ATMELMCI_MODULE)
	at91_add_device_mci(0, &mmc_data);
#else
	at91_add_device_mmc(0, &mmc_data);
#endif
	/* USB Host */
	at91_add_device_usbh(&usbh_data);
	/* Ethernet */
	at91_add_device_eth(&macb_data);
	/* I2C */
	at91_add_device_i2c(NULL, 0);
	/* W1 */
	add_w1();
}

static void __init portuxg20_board_init(void)
{
	generic_board_init();
	/* SPI */
	at91_add_device_spi(portuxg20_spi_devices, ARRAY_SIZE(portuxg20_spi_devices));
	/* USB Device */
	at91_add_device_udc(&portuxg20_udc_data);
	/* LEDs */
	at91_gpio_leds(portuxg20_leds, ARRAY_SIZE(portuxg20_leds));
}

static void __init stamp9g20_board_init(void)
{
	generic_board_init();
	/* USB Device */
	at91_add_device_udc(&stamp9g20_udc_data);
	/* LEDs */
	at91_gpio_leds(stamp9g20_leds, ARRAY_SIZE(stamp9g20_leds));
}

MACHINE_START(PORTUXG20, "taskit PortuxG20")
	/* Maintainer: taskit GmbH */
	.boot_params	= AT91_SDRAM_BASE + 0x100,
	.timer		= &at91sam926x_timer,
	.map_io		= portuxg20_map_io,
	.init_irq	= init_irq,
	.init_machine	= portuxg20_board_init,
MACHINE_END

MACHINE_START(STAMP9G20, "taskit Stamp9G20")
	/* Maintainer: taskit GmbH */
	.boot_params	= AT91_SDRAM_BASE + 0x100,
	.timer		= &at91sam926x_timer,
	.map_io		= stamp9g20_map_io,
	.init_irq	= init_irq,
	.init_machine	= stamp9g20_board_init,
MACHINE_END
