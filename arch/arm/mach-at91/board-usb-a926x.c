/*
 * linux/arch/arm/mach-at91/board-usb-a926x.c
 *
 *  Copyright (C) 2005 SAN People
 *  Copyright (C) 2007 Atmel Corporation.
 *  Copyright (C) 2007 Calao-systems
 *  Copyright (C) 2011 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
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
#include <linux/gpio_keys.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/spi/mmc_spi.h>

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

#include "sam9_smc.h"
#include "generic.h"


static void __init ek_init_early(void)
{
	/* Initialize processor: 12.00 MHz crystal */
	at91_initialize(12000000);

	/* DBGU on ttyS0. (Rx & Tx only) */
	at91_register_uart(0, 0, 0);

	/* set serial console to ttyS0 (ie, DBGU) */
	at91_set_serial_console(0);
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
	.vbus_pin	= AT91_PIN_PB11,
	.pullup_pin	= -EINVAL,		/* pull-up driven by UDC */
};

static void __init ek_add_device_udc(void)
{
	if (machine_is_usb_a9260() || machine_is_usb_a9g20())
		ek_udc_data.vbus_pin = AT91_PIN_PC5;

	at91_add_device_udc(&ek_udc_data);
}

#if defined(CONFIG_MMC_SPI) || defined(CONFIG_MMC_SPI_MODULE)
#define MMC_SPI_CARD_DETECT_INT AT91_PIN_PC4
static int at91_mmc_spi_init(struct device *dev,
	irqreturn_t (*detect_int)(int, void *), void *data)
{
	/* Configure Interrupt pin as input, no pull-up */
	at91_set_gpio_input(MMC_SPI_CARD_DETECT_INT, 0);
	return request_irq(gpio_to_irq(MMC_SPI_CARD_DETECT_INT), detect_int,
		IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
		"mmc-spi-detect", data);
}

static void at91_mmc_spi_exit(struct device *dev, void *data)
{
	free_irq(gpio_to_irq(MMC_SPI_CARD_DETECT_INT), data);
}

static struct mmc_spi_platform_data at91_mmc_spi_pdata = {
	.init = at91_mmc_spi_init,
	.exit = at91_mmc_spi_exit,
	.detect_delay = 100, /* msecs */
};
#endif

/*
 * SPI devices.
 */
static struct spi_board_info usb_a9263_spi_devices[] = {
#if !defined(CONFIG_MMC_AT91)
	{	/* DataFlash chip */
		.modalias	= "mtd_dataflash",
		.chip_select	= 0,
		.max_speed_hz	= 15 * 1000 * 1000,
		.bus_num	= 0,
	}
#endif
};

static struct spi_board_info usb_a9g20_spi_devices[] = {
#if defined(CONFIG_MMC_SPI) || defined(CONFIG_MMC_SPI_MODULE)
	{
		.modalias = "mmc_spi",
		.max_speed_hz = 20000000,	/* max spi clock (SCK) speed in HZ */
		.bus_num = 1,
		.chip_select = 0,
		.platform_data = &at91_mmc_spi_pdata,
		.mode = SPI_MODE_3,
	},
#endif
};

static void __init ek_add_device_spi(void)
{
	if (machine_is_usb_a9263())
		at91_add_device_spi(usb_a9263_spi_devices, ARRAY_SIZE(usb_a9263_spi_devices));
	else if (machine_is_usb_a9g20())
		at91_add_device_spi(usb_a9g20_spi_devices, ARRAY_SIZE(usb_a9g20_spi_devices));
}

/*
 * MACB Ethernet device
 */
static struct macb_platform_data __initdata ek_macb_data = {
	.phy_irq_pin	= AT91_PIN_PE31,
	.is_rmii	= 1,
};

static void __init ek_add_device_eth(void)
{
	if (machine_is_usb_a9260() || machine_is_usb_a9g20())
		ek_macb_data.phy_irq_pin = AT91_PIN_PA31;

	at91_add_device_eth(&ek_macb_data);
}

/*
 * NAND flash
 */
static struct mtd_partition __initdata ek_nand_partition[] = {
	{
		.name	= "barebox",
		.offset	= 0,
		.size	= 3 * SZ_128K,
	}, {
		.name	= "bareboxenv",
		.offset	= MTDPART_OFS_NXTBLK,
		.size	= SZ_128K,
	}, {
		.name	= "bareboxenv2",
		.offset	= MTDPART_OFS_NXTBLK,
		.size	= SZ_128K,
	}, {
		.name	= "kernel",
		.offset	= MTDPART_OFS_NXTBLK,
		.size	= 4 * SZ_1M,
	}, {
		.name	= "rootfs",
		.offset	= MTDPART_OFS_NXTBLK,
		.size	= 120 * SZ_1M,
	}, {
		.name	= "data",
		.offset	= MTDPART_OFS_NXTBLK,
		.size	= MTDPART_SIZ_FULL,
	}
};

static struct atmel_nand_data __initdata ek_nand_data = {
	.ale		= 21,
	.cle		= 22,
	.det_pin	= -EINVAL,
	.rdy_pin	= AT91_PIN_PA22,
	.enable_pin	= AT91_PIN_PD15,
	.ecc_mode	= NAND_ECC_SOFT,
	.on_flash_bbt	= 1,
	.parts		= ek_nand_partition,
	.num_parts	= ARRAY_SIZE(ek_nand_partition),
};

static struct sam9_smc_config __initdata usb_a9260_nand_smc_config = {
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

	.mode			= AT91_SMC_READMODE | AT91_SMC_WRITEMODE | AT91_SMC_EXNWMODE_DISABLE | AT91_SMC_DBW_8,
	.tdf_cycles		= 2,
};

static struct sam9_smc_config __initdata usb_a9g20_nand_smc_config = {
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

static void __init ek_add_device_nand(void)
{
	if (machine_is_usb_a9260() || machine_is_usb_a9g20()) {
		ek_nand_data.rdy_pin	= AT91_PIN_PC13;
		ek_nand_data.enable_pin	= AT91_PIN_PC14;
	}

	/* configure chip-select 3 (NAND) */
	if (machine_is_usb_a9g20())
		sam9_smc_configure(0, 3, &usb_a9g20_nand_smc_config);
	else
		sam9_smc_configure(0, 3, &usb_a9260_nand_smc_config);

	at91_add_device_nand(&ek_nand_data);
}


/*
 * GPIO Buttons
 */
#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
static struct gpio_keys_button ek_buttons[] = {
	{	/* USER PUSH BUTTON */
		.code		= KEY_ENTER,
		.gpio		= AT91_PIN_PB10,
		.active_low	= 1,
		.desc		= "user_pb",
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
	at91_set_GPIO_periph(AT91_PIN_PB10, 1);	/* user push button, pull up enabled */
	at91_set_deglitch(AT91_PIN_PB10, 1);

	platform_device_register(&ek_button_device);
}
#else
static void __init ek_add_device_buttons(void) {}
#endif

/*
 * LEDs
 */
static struct gpio_led ek_leds[] = {
	{	/* user_led (green) */
		.name			= "user_led",
		.gpio			= AT91_PIN_PB21,
		.active_low		= 1,
		.default_trigger	= "heartbeat",
	}
};

static struct i2c_board_info __initdata ek_i2c_devices[] = {
	{
		I2C_BOARD_INFO("rv3029c2", 0x56),
	},
};

static void __init ek_add_device_leds(void)
{
	if (machine_is_usb_a9260() || machine_is_usb_a9g20())
		ek_leds[0].active_low = 0;

	at91_gpio_leds(ek_leds, ARRAY_SIZE(ek_leds));
}

static void __init ek_board_init(void)
{
	/* Serial */
	at91_add_device_serial();
	/* USB Host */
	at91_add_device_usbh(&ek_usbh_data);
	/* USB Device */
	ek_add_device_udc();
	/* SPI */
	ek_add_device_spi();
	/* Ethernet */
	ek_add_device_eth();
	/* NAND */
	ek_add_device_nand();
	/* Push Buttons */
	ek_add_device_buttons();
	/* LEDs */
	ek_add_device_leds();

	if (machine_is_usb_a9g20()) {
		/* I2C */
		at91_add_device_i2c(ek_i2c_devices, ARRAY_SIZE(ek_i2c_devices));
	} else {
		/* I2C */
		at91_add_device_i2c(NULL, 0);
		/* shutdown controller, wakeup button (5 msec low) */
		at91_shdwc_write(AT91_SHDW_MR, AT91_SHDW_CPTWK0_(10)
				| AT91_SHDW_WKMODE0_LOW
				| AT91_SHDW_RTTWKEN);
	}
}

MACHINE_START(USB_A9263, "CALAO USB_A9263")
	/* Maintainer: calao-systems */
	.timer		= &at91sam926x_timer,
	.map_io		= at91_map_io,
	.init_early	= ek_init_early,
	.init_irq	= at91_init_irq_default,
	.init_machine	= ek_board_init,
MACHINE_END

MACHINE_START(USB_A9260, "CALAO USB_A9260")
	/* Maintainer: calao-systems */
	.timer		= &at91sam926x_timer,
	.map_io		= at91_map_io,
	.init_early	= ek_init_early,
	.init_irq	= at91_init_irq_default,
	.init_machine	= ek_board_init,
MACHINE_END

MACHINE_START(USB_A9G20, "CALAO USB_A92G0")
	/* Maintainer: Jean-Christophe PLAGNIOL-VILLARD */
	.timer		= &at91sam926x_timer,
	.map_io		= at91_map_io,
	.init_early	= ek_init_early,
	.init_irq	= at91_init_irq_default,
	.init_machine	= ek_board_init,
MACHINE_END
