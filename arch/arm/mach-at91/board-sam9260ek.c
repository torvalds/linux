/*
 * linux/arch/arm/mach-at91/board-sam9260ek.c
 *
 *  Copyright (C) 2005 SAN People
 *  Copyright (C) 2006 Atmel
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
#include <linux/spi/spi.h>
#include <linux/spi/at73c213.h>
#include <linux/clk.h>
#include <linux/i2c/at24.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <mach/board.h>
#include <mach/at91sam9_smc.h>
#include <mach/at91_shdwc.h>
#include <mach/system_rev.h>

#include "sam9_smc.h"
#include "generic.h"


static void __init ek_init_early(void)
{
	/* Initialize processor: 18.432 MHz crystal */
	at91_initialize(18432000);
}

/*
 * USB Host port
 */
static struct at91_usbh_data __initdata ek_usbh_data = {
	.ports		= 2,
	.vbus_pin	= {-EINVAL, -EINVAL},
	.overcurrent_pin= {-EINVAL, -EINVAL},
};

/*
 * USB Device port
 */
static struct at91_udc_data __initdata ek_udc_data = {
	.vbus_pin	= AT91_PIN_PC5,
	.pullup_pin	= -EINVAL,		/* pull-up driven by UDC */
};


/*
 * Audio
 */
static struct at73c213_board_info at73c213_data = {
	.ssc_id		= 0,
	.shortname	= "AT91SAM9260-EK external DAC",
};

#if defined(CONFIG_SND_AT73C213) || defined(CONFIG_SND_AT73C213_MODULE)
static void __init at73c213_set_clk(struct at73c213_board_info *info)
{
	struct clk *pck0;
	struct clk *plla;

	pck0 = clk_get(NULL, "pck0");
	plla = clk_get(NULL, "plla");

	/* AT73C213 MCK Clock */
	at91_set_B_periph(AT91_PIN_PC1, 0);	/* PCK0 */

	clk_set_parent(pck0, plla);
	clk_put(plla);

	info->dac_clk = pck0;
}
#else
static void __init at73c213_set_clk(struct at73c213_board_info *info) {}
#endif

/*
 * SPI devices.
 */
static struct spi_board_info ek_spi_devices[] = {
#if !IS_ENABLED(CONFIG_MMC_ATMELMCI)
	{	/* DataFlash chip */
		.modalias	= "mtd_dataflash",
		.chip_select	= 1,
		.max_speed_hz	= 15 * 1000 * 1000,
		.bus_num	= 0,
	},
#if defined(CONFIG_MTD_AT91_DATAFLASH_CARD)
	{	/* DataFlash card */
		.modalias	= "mtd_dataflash",
		.chip_select	= 0,
		.max_speed_hz	= 15 * 1000 * 1000,
		.bus_num	= 0,
	},
#endif
#endif
#if defined(CONFIG_SND_AT73C213) || defined(CONFIG_SND_AT73C213_MODULE)
	{	/* AT73C213 DAC */
		.modalias	= "at73c213",
		.chip_select	= 0,
		.max_speed_hz	= 10 * 1000 * 1000,
		.bus_num	= 1,
		.mode		= SPI_MODE_1,
		.platform_data	= &at73c213_data,
	},
#endif
};


/*
 * MACB Ethernet device
 */
static struct macb_platform_data __initdata ek_macb_data = {
	.phy_irq_pin	= AT91_PIN_PA7,
	.is_rmii	= 1,
};


/*
 * NAND flash
 */
static struct mtd_partition __initdata ek_nand_partition[] = {
	{
		.name	= "Partition 1",
		.offset	= 0,
		.size	= SZ_256K,
	},
	{
		.name	= "Partition 2",
		.offset	= MTDPART_OFS_NXTBLK,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct atmel_nand_data __initdata ek_nand_data = {
	.ale		= 21,
	.cle		= 22,
	.det_pin	= -EINVAL,
	.rdy_pin	= AT91_PIN_PC13,
	.enable_pin	= AT91_PIN_PC14,
	.ecc_mode	= NAND_ECC_SOFT,
	.on_flash_bbt	= 1,
	.parts		= ek_nand_partition,
	.num_parts	= ARRAY_SIZE(ek_nand_partition),
};

static struct sam9_smc_config __initdata ek_nand_smc_config = {
	.ncs_read_setup		= 0,
	.nrd_setup		= 1,
	.ncs_write_setup	= 0,
	.nwe_setup		= 1,

	.ncs_read_pulse		= 3,
	.nrd_pulse		= 3,
	.ncs_write_pulse	= 3,
	.nwe_pulse		= 3,

	.read_cycle		= 5,
	.write_cycle		= 5,

	.mode			= AT91_SMC_READMODE | AT91_SMC_WRITEMODE | AT91_SMC_EXNWMODE_DISABLE,
	.tdf_cycles		= 2,
};

static void __init ek_add_device_nand(void)
{
	ek_nand_data.bus_width_16 = board_have_nand_16bit();
	/* setup bus-width (8 or 16) */
	if (ek_nand_data.bus_width_16)
		ek_nand_smc_config.mode |= AT91_SMC_DBW_16;
	else
		ek_nand_smc_config.mode |= AT91_SMC_DBW_8;

	/* configure chip-select 3 (NAND) */
	sam9_smc_configure(0, 3, &ek_nand_smc_config);

	at91_add_device_nand(&ek_nand_data);
}


/*
 * MCI (SD/MMC)
 */
static struct mci_platform_data __initdata ek_mci0_data = {
	.slot[1] = {
		.bus_width	= 4,
		.detect_pin	= -EINVAL,
		.wp_pin		= -EINVAL,
	},
};


/*
 * LEDs
 */
static struct gpio_led ek_leds[] = {
	{	/* "bottom" led, green, userled1 to be defined */
		.name			= "ds5",
		.gpio			= AT91_PIN_PA6,
		.active_low		= 1,
		.default_trigger	= "none",
	},
	{	/* "power" led, yellow */
		.name			= "ds1",
		.gpio			= AT91_PIN_PA9,
		.default_trigger	= "heartbeat",
	}
};

/*
 * I2C devices
 */
static struct at24_platform_data at24c512 = {
	.byte_len	= SZ_512K / 8,
	.page_size	= 128,
	.flags		= AT24_FLAG_ADDR16,
};

static struct i2c_board_info __initdata ek_i2c_devices[] = {
	{
		I2C_BOARD_INFO("24c512", 0x50),
		.platform_data = &at24c512,
	},
	/* more devices can be added using expansion connectors */
};


/*
 * GPIO Buttons
 */
#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
static struct gpio_keys_button ek_buttons[] = {
	{
		.gpio		= AT91_PIN_PA30,
		.code		= BTN_3,
		.desc		= "Button 3",
		.active_low	= 1,
		.wakeup		= 1,
	},
	{
		.gpio		= AT91_PIN_PA31,
		.code		= BTN_4,
		.desc		= "Button 4",
		.active_low	= 1,
		.wakeup		= 1,
	}
};

static struct gpio_keys_platform_data ek_button_data = {
	.buttons	= ek_buttons,
	.nbuttons	= ARRAY_SIZE(ek_buttons),
};

static struct platform_device ek_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &ek_button_data,
	}
};

static void __init ek_add_device_buttons(void)
{
	at91_set_gpio_input(AT91_PIN_PA30, 1);	/* btn3 */
	at91_set_deglitch(AT91_PIN_PA30, 1);
	at91_set_gpio_input(AT91_PIN_PA31, 1);	/* btn4 */
	at91_set_deglitch(AT91_PIN_PA31, 1);

	platform_device_register(&ek_button_device);
}
#else
static void __init ek_add_device_buttons(void) {}
#endif


static void __init ek_board_init(void)
{
	/* Serial */
	/* DBGU on ttyS0. (Rx & Tx only) */
	at91_register_uart(0, 0, 0);

	/* USART0 on ttyS1. (Rx, Tx, CTS, RTS, DTR, DSR, DCD, RI) */
	at91_register_uart(AT91SAM9260_ID_US0, 1, ATMEL_UART_CTS | ATMEL_UART_RTS
			   | ATMEL_UART_DTR | ATMEL_UART_DSR | ATMEL_UART_DCD
			   | ATMEL_UART_RI);

	/* USART1 on ttyS2. (Rx, Tx, RTS, CTS) */
	at91_register_uart(AT91SAM9260_ID_US1, 2, ATMEL_UART_CTS | ATMEL_UART_RTS);
	at91_add_device_serial();
	/* USB Host */
	at91_add_device_usbh(&ek_usbh_data);
	/* USB Device */
	at91_add_device_udc(&ek_udc_data);
	/* SPI */
	at91_add_device_spi(ek_spi_devices, ARRAY_SIZE(ek_spi_devices));
	/* NAND */
	ek_add_device_nand();
	/* Ethernet */
	at91_add_device_eth(&ek_macb_data);
	/* MMC */
	at91_add_device_mci(0, &ek_mci0_data);
	/* I2C */
	at91_add_device_i2c(ek_i2c_devices, ARRAY_SIZE(ek_i2c_devices));
	/* SSC (to AT73C213) */
	at73c213_set_clk(&at73c213_data);
	at91_add_device_ssc(AT91SAM9260_ID_SSC, ATMEL_SSC_TX);
	/* LEDs */
	at91_gpio_leds(ek_leds, ARRAY_SIZE(ek_leds));
	/* Push Buttons */
	ek_add_device_buttons();
}

MACHINE_START(AT91SAM9260EK, "Atmel AT91SAM9260-EK")
	/* Maintainer: Atmel */
	.timer		= &at91sam926x_timer,
	.map_io		= at91_map_io,
	.init_early	= ek_init_early,
	.init_irq	= at91_init_irq_default,
	.init_machine	= ek_board_init,
MACHINE_END
