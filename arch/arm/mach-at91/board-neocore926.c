/*
 * linux/arch/arm/mach-at91/board-neocore926.c
 *
 *  Copyright (C) 2005 SAN People
 *  Copyright (C) 2007 Atmel Corporation
 *  Copyright (C) 2008 ADENEO.
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
#include <linux/fb.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>

#include <video/atmel_lcdc.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include <asm/sizes.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <mach/board.h>
#include <mach/at91sam9_smc.h>

#include "sam9_smc.h"
#include "generic.h"


static void __init neocore926_init_early(void)
{
	/* Initialize processor: 20 MHz crystal */
	at91_initialize(20000000);
}

/*
 * USB Host port
 */
static struct at91_usbh_data __initdata neocore926_usbh_data = {
	.ports		= 2,
	.vbus_pin	= { AT91_PIN_PA24, AT91_PIN_PA21 },
	.overcurrent_pin= {-EINVAL, -EINVAL},
};

/*
 * USB Device port
 */
static struct at91_udc_data __initdata neocore926_udc_data = {
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

static void __init neocore926_add_device_ts(void)
{
	at91_set_B_periph(AT91_PIN_PA15, 1);	/* External IRQ1, with pullup */
	at91_set_gpio_input(AT91_PIN_PC13, 1);	/* Touchscreen BUSY signal */
}
#else
static void __init neocore926_add_device_ts(void) {}
#endif

/*
 * SPI devices.
 */
static struct spi_board_info neocore926_spi_devices[] = {
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
		.chip_select	= 1,
		.max_speed_hz	= 125000 * 16,
		.bus_num	= 0,
		.platform_data	= &ads_info,
		.irq		= AT91SAM9263_ID_IRQ1,
	},
#endif
};


/*
 * MCI (SD/MMC)
 */
static struct mci_platform_data __initdata neocore926_mci0_data = {
	.slot[0] = {
		.bus_width	= 4,
		.detect_pin	= AT91_PIN_PE18,
		.wp_pin		= AT91_PIN_PE19,
	},
};


/*
 * MACB Ethernet device
 */
static struct macb_platform_data __initdata neocore926_macb_data = {
	.phy_irq_pin	= AT91_PIN_PE31,
	.is_rmii	= 1,
};


/*
 * NAND flash
 */
static struct mtd_partition __initdata neocore926_nand_partition[] = {
	{
		.name	= "Linux Kernel",	/* "Partition 1", */
		.offset	= 0,
		.size	= SZ_8M,
	},
	{
		.name	= "Filesystem",		/* "Partition 2", */
		.offset	= MTDPART_OFS_NXTBLK,
		.size	= SZ_32M,
	},
	{
		.name	= "Free",		/* "Partition 3", */
		.offset	= MTDPART_OFS_NXTBLK,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct atmel_nand_data __initdata neocore926_nand_data = {
	.ale			= 21,
	.cle			= 22,
	.rdy_pin		= AT91_PIN_PB19,
	.rdy_pin_active_low	= 1,
	.enable_pin		= AT91_PIN_PD15,
	.ecc_mode		= NAND_ECC_SOFT,
	.parts			= neocore926_nand_partition,
	.num_parts		= ARRAY_SIZE(neocore926_nand_partition),
	.det_pin		= -EINVAL,
};

static struct sam9_smc_config __initdata neocore926_nand_smc_config = {
	.ncs_read_setup		= 0,
	.nrd_setup		= 1,
	.ncs_write_setup	= 0,
	.nwe_setup		= 1,

	.ncs_read_pulse		= 4,
	.nrd_pulse		= 4,
	.ncs_write_pulse	= 4,
	.nwe_pulse		= 4,

	.read_cycle		= 6,
	.write_cycle		= 6,

	.mode			= AT91_SMC_READMODE | AT91_SMC_WRITEMODE | AT91_SMC_EXNWMODE_DISABLE | AT91_SMC_DBW_8,
	.tdf_cycles		= 2,
};

static void __init neocore926_add_device_nand(void)
{
	/* configure chip-select 3 (NAND) */
	sam9_smc_configure(0, 3, &neocore926_nand_smc_config);

	at91_add_device_nand(&neocore926_nand_data);
}


/*
 * LCD Controller
 */
#if defined(CONFIG_FB_ATMEL) || defined(CONFIG_FB_ATMEL_MODULE)
static struct fb_videomode at91_tft_vga_modes[] = {
	{
		.name		= "TX09D50VM1CCA @ 60",
		.refresh	= 60,
		.xres		= 240,		.yres		= 320,
		.pixclock	= KHZ2PICOS(5000),

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

#define AT91SAM9263_DEFAULT_LCDCON2 (ATMEL_LCDC_MEMOR_LITTLE \
					| ATMEL_LCDC_DISTYPE_TFT \
					| ATMEL_LCDC_CLKMOD_ALWAYSACTIVE)

static void at91_lcdc_power_control(int on)
{
	at91_set_gpio_value(AT91_PIN_PA30, on);
}

/* Driver datas */
static struct atmel_lcdfb_info __initdata neocore926_lcdc_data = {
	.lcdcon_is_backlight		= true,
	.default_bpp			= 16,
	.default_dmacon			= ATMEL_LCDC_DMAEN,
	.default_lcdcon2		= AT91SAM9263_DEFAULT_LCDCON2,
	.default_monspecs		= &at91fb_default_monspecs,
	.atmel_lcdfb_power_control	= at91_lcdc_power_control,
	.guard_time			= 1,
	.lcd_wiring_mode		= ATMEL_LCDC_WIRING_RGB555,
};

#else
static struct atmel_lcdfb_info __initdata neocore926_lcdc_data;
#endif


/*
 * GPIO Buttons
 */
#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
static struct gpio_keys_button neocore926_buttons[] = {
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
	},
};

static struct gpio_keys_platform_data neocore926_button_data = {
	.buttons	= neocore926_buttons,
	.nbuttons	= ARRAY_SIZE(neocore926_buttons),
};

static struct platform_device neocore926_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &neocore926_button_data,
	}
};

static void __init neocore926_add_device_buttons(void)
{
	at91_set_GPIO_periph(AT91_PIN_PC5, 0);	/* left button */
	at91_set_deglitch(AT91_PIN_PC5, 1);
	at91_set_GPIO_periph(AT91_PIN_PC4, 0);	/* right button */
	at91_set_deglitch(AT91_PIN_PC4, 1);

	platform_device_register(&neocore926_button_device);
}
#else
static void __init neocore926_add_device_buttons(void) {}
#endif


/*
 * AC97
 */
static struct ac97c_platform_data neocore926_ac97_data = {
	.reset_pin	= AT91_PIN_PA13,
};


static void __init neocore926_board_init(void)
{
	/* Serial */
	/* DBGU on ttyS0. (Rx & Tx only) */
	at91_register_uart(0, 0, 0);

	/* USART0 on ttyS1. (Rx, Tx, RTS, CTS) */
	at91_register_uart(AT91SAM9263_ID_US0, 1, ATMEL_UART_CTS | ATMEL_UART_RTS);
	at91_add_device_serial();

	/* USB Host */
	at91_add_device_usbh(&neocore926_usbh_data);

	/* USB Device */
	at91_add_device_udc(&neocore926_udc_data);

	/* SPI */
	at91_set_gpio_output(AT91_PIN_PE20, 1);		/* select spi0 clock */
	at91_add_device_spi(neocore926_spi_devices, ARRAY_SIZE(neocore926_spi_devices));

	/* Touchscreen */
	neocore926_add_device_ts();

	/* MMC */
	at91_add_device_mci(0, &neocore926_mci0_data);

	/* Ethernet */
	at91_add_device_eth(&neocore926_macb_data);

	/* NAND */
	neocore926_add_device_nand();

	/* I2C */
	at91_add_device_i2c(NULL, 0);

	/* LCD Controller */
	at91_add_device_lcdc(&neocore926_lcdc_data);

	/* Push Buttons */
	neocore926_add_device_buttons();

	/* AC97 */
	at91_add_device_ac97(&neocore926_ac97_data);
}

MACHINE_START(NEOCORE926, "ADENEO NEOCORE 926")
	/* Maintainer: ADENEO */
	.timer		= &at91sam926x_timer,
	.map_io		= at91_map_io,
	.init_early	= neocore926_init_early,
	.init_irq	= at91_init_irq_default,
	.init_machine	= neocore926_board_init,
MACHINE_END
