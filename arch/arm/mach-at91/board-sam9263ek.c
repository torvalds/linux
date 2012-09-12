/*
 * linux/arch/arm/mach-at91/board-sam9263ek.c
 *
 *  Copyright (C) 2005 SAN People
 *  Copyright (C) 2007 Atmel Corporation.
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
#include <linux/spi/ads7846.h>
#include <linux/i2c/at24.h>
#include <linux/fb.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/leds.h>

#include <video/atmel_lcdc.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <mach/board.h>
#include <mach/at91_aic.h>
#include <mach/at91sam9_smc.h>
#include <mach/at91_shdwc.h>
#include <mach/system_rev.h>

#include "sam9_smc.h"
#include "generic.h"


static void __init ek_init_early(void)
{
	/* Initialize processor: 16.367 MHz crystal */
	at91_initialize(16367660);
}

/*
 * USB Host port
 */
static struct at91_usbh_data __initdata ek_usbh_data = {
	.ports		= 2,
	.vbus_pin	= { AT91_PIN_PA24, AT91_PIN_PA21 },
	.vbus_pin_active_low = {1, 1},
	.overcurrent_pin= {-EINVAL, -EINVAL},
};

/*
 * USB Device port
 */
static struct at91_udc_data __initdata ek_udc_data = {
	.vbus_pin	= AT91_PIN_PA25,
	.pullup_pin	= -EINVAL,		/* pull-up driven by UDC */
};


/*
 * ADS7846 Touchscreen
 */
#if defined(CONFIG_TOUCHSCREEN_ADS7846) || defined(CONFIG_TOUCHSCREEN_ADS7846_MODULE)
static int ads7843_pendown_state(void)
{
	return !at91_get_gpio_value(AT91_PIN_PA15);	/* Touchscreen PENIRQ */
}

static struct ads7846_platform_data ads_info = {
	.model			= 7843,
	.x_min			= 150,
	.x_max			= 3830,
	.y_min			= 190,
	.y_max			= 3830,
	.vref_delay_usecs	= 100,
	.x_plate_ohms		= 450,
	.y_plate_ohms		= 250,
	.pressure_max		= 15000,
	.debounce_max		= 1,
	.debounce_rep		= 0,
	.debounce_tol		= (~0),
	.get_pendown_state	= ads7843_pendown_state,
};

static void __init ek_add_device_ts(void)
{
	at91_set_B_periph(AT91_PIN_PA15, 1);	/* External IRQ1, with pullup */
	at91_set_gpio_input(AT91_PIN_PA31, 1);	/* Touchscreen BUSY signal */
}
#else
static void __init ek_add_device_ts(void) {}
#endif

/*
 * SPI devices.
 */
static struct spi_board_info ek_spi_devices[] = {
#if defined(CONFIG_MTD_AT91_DATAFLASH_CARD)
	{	/* DataFlash card */
		.modalias	= "mtd_dataflash",
		.chip_select	= 0,
		.max_speed_hz	= 15 * 1000 * 1000,
		.bus_num	= 0,
	},
#endif
#if defined(CONFIG_TOUCHSCREEN_ADS7846) || defined(CONFIG_TOUCHSCREEN_ADS7846_MODULE)
	{
		.modalias	= "ads7846",
		.chip_select	= 3,
		.max_speed_hz	= 125000 * 26,	/* (max sample rate @ 3V) * (cmd + data + overhead) */
		.bus_num	= 0,
		.platform_data	= &ads_info,
		.irq		= AT91SAM9263_ID_IRQ1,
	},
#endif
};


/*
 * MCI (SD/MMC)
 */
static struct at91_mmc_data __initdata ek_mmc_data = {
	.wire4		= 1,
	.det_pin	= AT91_PIN_PE18,
	.wp_pin		= AT91_PIN_PE19,
	.vcc_pin	= -EINVAL,
};


/*
 * MACB Ethernet device
 */
static struct macb_platform_data __initdata ek_macb_data = {
	.phy_irq_pin	= AT91_PIN_PE31,
	.is_rmii	= 1,
};


/*
 * NAND flash
 */
static struct mtd_partition __initdata ek_nand_partition[] = {
	{
		.name	= "Partition 1",
		.offset	= 0,
		.size	= SZ_64M,
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
	.rdy_pin	= AT91_PIN_PA22,
	.enable_pin	= AT91_PIN_PD15,
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
 * LCD Controller
 */
#if defined(CONFIG_FB_ATMEL) || defined(CONFIG_FB_ATMEL_MODULE)
static struct fb_videomode at91_tft_vga_modes[] = {
	{
		.name		= "TX09D50VM1CCA @ 60",
		.refresh	= 60,
		.xres		= 240,		.yres		= 320,
		.pixclock	= KHZ2PICOS(4965),

		.left_margin	= 1,		.right_margin	= 33,
		.upper_margin	= 1,		.lower_margin	= 0,
		.hsync_len	= 5,		.vsync_len	= 1,

		.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
};

static struct fb_monspecs at91fb_default_monspecs = {
	.manufacturer	= "HIT",
	.monitor	= "TX09D70VM1CCA",

	.modedb		= at91_tft_vga_modes,
	.modedb_len	= ARRAY_SIZE(at91_tft_vga_modes),
	.hfmin		= 15000,
	.hfmax		= 64000,
	.vfmin		= 50,
	.vfmax		= 150,
};

#define AT91SAM9263_DEFAULT_LCDCON2 	(ATMEL_LCDC_MEMOR_LITTLE \
					| ATMEL_LCDC_DISTYPE_TFT \
					| ATMEL_LCDC_CLKMOD_ALWAYSACTIVE)

static void at91_lcdc_power_control(int on)
{
	at91_set_gpio_value(AT91_PIN_PA30, on);
}

/* Driver datas */
static struct atmel_lcdfb_info __initdata ek_lcdc_data = {
	.lcdcon_is_backlight		= true,
	.default_bpp			= 16,
	.default_dmacon			= ATMEL_LCDC_DMAEN,
	.default_lcdcon2		= AT91SAM9263_DEFAULT_LCDCON2,
	.default_monspecs		= &at91fb_default_monspecs,
	.atmel_lcdfb_power_control	= at91_lcdc_power_control,
	.guard_time			= 1,
};

#else
static struct atmel_lcdfb_info __initdata ek_lcdc_data;
#endif


/*
 * GPIO Buttons
 */
#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
static struct gpio_keys_button ek_buttons[] = {
	{	/* BP1, "leftclic" */
		.code		= BTN_LEFT,
		.gpio		= AT91_PIN_PC5,
		.active_low	= 1,
		.desc		= "left_click",
		.wakeup		= 1,
	},
	{	/* BP2, "rightclic" */
		.code		= BTN_RIGHT,
		.gpio		= AT91_PIN_PC4,
		.active_low	= 1,
		.desc		= "right_click",
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
	at91_set_GPIO_periph(AT91_PIN_PC5, 1);	/* left button */
	at91_set_deglitch(AT91_PIN_PC5, 1);
	at91_set_GPIO_periph(AT91_PIN_PC4, 1);	/* right button */
	at91_set_deglitch(AT91_PIN_PC4, 1);

	platform_device_register(&ek_button_device);
}
#else
static void __init ek_add_device_buttons(void) {}
#endif


/*
 * AC97
 * reset_pin is not connected: NRST
 */
static struct ac97c_platform_data ek_ac97_data = {
	.reset_pin	= -EINVAL,
};


/*
 * LEDs ... these could all be PWM-driven, for variable brightness
 */
static struct gpio_led ek_leds[] = {
	{	/* "right" led, green, userled2 (could be driven by pwm2) */
		.name			= "ds2",
		.gpio			= AT91_PIN_PC29,
		.active_low		= 1,
		.default_trigger	= "nand-disk",
	},
	{	/* "power" led, yellow (could be driven by pwm0) */
		.name			= "ds3",
		.gpio			= AT91_PIN_PB7,
		.default_trigger	= "heartbeat",
	}
};

/*
 * PWM Leds
 */
static struct gpio_led ek_pwm_led[] = {
	/* For now only DS1 is PWM-driven (by pwm1) */
	{
		.name			= "ds1",
		.gpio			= 1,	/* is PWM channel number */
		.active_low		= 1,
		.default_trigger	= "none",
	}
};

/*
 * CAN
 */
static void sam9263ek_transceiver_switch(int on)
{
	if (on) {
		at91_set_gpio_output(AT91_PIN_PA18, 1); /* CANRXEN */
		at91_set_gpio_output(AT91_PIN_PA19, 0); /* CANRS */
	} else {
		at91_set_gpio_output(AT91_PIN_PA18, 0); /* CANRXEN */
		at91_set_gpio_output(AT91_PIN_PA19, 1); /* CANRS */
	}
}

static struct at91_can_data ek_can_data = {
	.transceiver_switch = sam9263ek_transceiver_switch,
};

static void __init ek_board_init(void)
{
	/* Serial */
	/* DBGU on ttyS0. (Rx & Tx only) */
	at91_register_uart(0, 0, 0);

	/* USART0 on ttyS1. (Rx, Tx, RTS, CTS) */
	at91_register_uart(AT91SAM9263_ID_US0, 1, ATMEL_UART_CTS | ATMEL_UART_RTS);
	at91_add_device_serial();
	/* USB Host */
	at91_add_device_usbh(&ek_usbh_data);
	/* USB Device */
	at91_add_device_udc(&ek_udc_data);
	/* SPI */
	at91_set_gpio_output(AT91_PIN_PE20, 1);		/* select spi0 clock */
	at91_add_device_spi(ek_spi_devices, ARRAY_SIZE(ek_spi_devices));
	/* Touchscreen */
	ek_add_device_ts();
	/* MMC */
	at91_add_device_mmc(1, &ek_mmc_data);
	/* Ethernet */
	at91_add_device_eth(&ek_macb_data);
	/* NAND */
	ek_add_device_nand();
	/* I2C */
	at91_add_device_i2c(ek_i2c_devices, ARRAY_SIZE(ek_i2c_devices));
	/* LCD Controller */
	at91_add_device_lcdc(&ek_lcdc_data);
	/* Push Buttons */
	ek_add_device_buttons();
	/* AC97 */
	at91_add_device_ac97(&ek_ac97_data);
	/* LEDs */
	at91_gpio_leds(ek_leds, ARRAY_SIZE(ek_leds));
	at91_pwm_leds(ek_pwm_led, ARRAY_SIZE(ek_pwm_led));
	/* CAN */
	at91_add_device_can(&ek_can_data);
}

MACHINE_START(AT91SAM9263EK, "Atmel AT91SAM9263-EK")
	/* Maintainer: Atmel */
	.timer		= &at91sam926x_timer,
	.map_io		= at91_map_io,
	.handle_irq	= at91_aic_handle_irq,
	.init_early	= ek_init_early,
	.init_irq	= at91_init_irq_default,
	.init_machine	= ek_board_init,
MACHINE_END
