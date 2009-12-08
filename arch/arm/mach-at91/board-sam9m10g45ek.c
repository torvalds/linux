/*
 *  Board-specific setup code for the AT91SAM9M10G45 Evaluation Kit family
 *
 *  Covers: * AT91SAM9G45-EKES  board
 *          * AT91SAM9M10G45-EK board
 *
 *  Copyright (C) 2009 Atmel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/fb.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/leds.h>
#include <linux/clk.h>

#include <mach/hardware.h>
#include <video/atmel_lcdc.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <mach/board.h>
#include <mach/gpio.h>
#include <mach/at91sam9_smc.h>
#include <mach/at91_shdwc.h>

#include "sam9_smc.h"
#include "generic.h"


static void __init ek_map_io(void)
{
	/* Initialize processor: 12.000 MHz crystal */
	at91sam9g45_initialize(12000000);

	/* DGBU on ttyS0. (Rx & Tx only) */
	at91_register_uart(0, 0, 0);

	/* USART0 not connected on the -EK board */
	/* USART1 on ttyS2. (Rx, Tx, RTS, CTS) */
	at91_register_uart(AT91SAM9G45_ID_US1, 2, ATMEL_UART_CTS | ATMEL_UART_RTS);

	/* set serial console to ttyS0 (ie, DBGU) */
	at91_set_serial_console(0);
}

static void __init ek_init_irq(void)
{
	at91sam9g45_init_interrupts(NULL);
}


/*
 * USB HS Host port (common to OHCI & EHCI)
 */
static struct at91_usbh_data __initdata ek_usbh_hs_data = {
	.ports		= 2,
	.vbus_pin	= {AT91_PIN_PD1, AT91_PIN_PD3},
};


/*
 * USB HS Device port
 */
static struct usba_platform_data __initdata ek_usba_udc_data = {
	.vbus_pin	= AT91_PIN_PB19,
};


/*
 * SPI devices.
 */
static struct spi_board_info ek_spi_devices[] = {
	{	/* DataFlash chip */
		.modalias	= "mtd_dataflash",
		.chip_select	= 0,
		.max_speed_hz	= 15 * 1000 * 1000,
		.bus_num	= 0,
	},
};


/*
 * MACB Ethernet device
 */
static struct at91_eth_data __initdata ek_macb_data = {
	.phy_irq_pin	= AT91_PIN_PD5,
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

static struct mtd_partition * __init nand_partitions(int size, int *num_partitions)
{
	*num_partitions = ARRAY_SIZE(ek_nand_partition);
	return ek_nand_partition;
}

/* det_pin is not connected */
static struct atmel_nand_data __initdata ek_nand_data = {
	.ale		= 21,
	.cle		= 22,
	.rdy_pin	= AT91_PIN_PC8,
	.enable_pin	= AT91_PIN_PC14,
	.partition_info	= nand_partitions,
#if defined(CONFIG_MTD_NAND_AT91_BUSWIDTH_16)
	.bus_width_16	= 1,
#else
	.bus_width_16	= 0,
#endif
};

static struct sam9_smc_config __initdata ek_nand_smc_config = {
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

	.mode			= AT91_SMC_READMODE | AT91_SMC_WRITEMODE | AT91_SMC_EXNWMODE_DISABLE,
	.tdf_cycles		= 3,
};

static void __init ek_add_device_nand(void)
{
	/* setup bus-width (8 or 16) */
	if (ek_nand_data.bus_width_16)
		ek_nand_smc_config.mode |= AT91_SMC_DBW_16;
	else
		ek_nand_smc_config.mode |= AT91_SMC_DBW_8;

	/* configure chip-select 3 (NAND) */
	sam9_smc_configure(3, &ek_nand_smc_config);

	at91_add_device_nand(&ek_nand_data);
}


/*
 * LCD Controller
 */
#if defined(CONFIG_FB_ATMEL) || defined(CONFIG_FB_ATMEL_MODULE)
static struct fb_videomode at91_tft_vga_modes[] = {
	{
		.name           = "LG",
		.refresh	= 60,
		.xres		= 480,		.yres		= 272,
		.pixclock	= KHZ2PICOS(9000),

		.left_margin	= 1,		.right_margin	= 1,
		.upper_margin	= 40,		.lower_margin	= 1,
		.hsync_len	= 45,		.vsync_len	= 1,

		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
};

static struct fb_monspecs at91fb_default_monspecs = {
	.manufacturer	= "LG",
	.monitor        = "LB043WQ1",

	.modedb		= at91_tft_vga_modes,
	.modedb_len	= ARRAY_SIZE(at91_tft_vga_modes),
	.hfmin		= 15000,
	.hfmax		= 17640,
	.vfmin		= 57,
	.vfmax		= 67,
};

#define AT91SAM9G45_DEFAULT_LCDCON2 	(ATMEL_LCDC_MEMOR_LITTLE \
					| ATMEL_LCDC_DISTYPE_TFT \
					| ATMEL_LCDC_CLKMOD_ALWAYSACTIVE)

/* Driver datas */
static struct atmel_lcdfb_info __initdata ek_lcdc_data = {
	.lcdcon_is_backlight		= true,
	.default_bpp			= 32,
	.default_dmacon			= ATMEL_LCDC_DMAEN,
	.default_lcdcon2		= AT91SAM9G45_DEFAULT_LCDCON2,
	.default_monspecs		= &at91fb_default_monspecs,
	.guard_time			= 9,
	.lcd_wiring_mode		= ATMEL_LCDC_WIRING_RGB,
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
		.gpio		= AT91_PIN_PB6,
		.active_low	= 1,
		.desc		= "left_click",
		.wakeup		= 1,
	},
	{	/* BP2, "rightclic" */
		.code		= BTN_RIGHT,
		.gpio		= AT91_PIN_PB7,
		.active_low	= 1,
		.desc		= "right_click",
		.wakeup		= 1,
	},
		/* BP3, "joystick" */
	{
		.code		= KEY_LEFT,
		.gpio		= AT91_PIN_PB14,
		.active_low	= 1,
		.desc		= "Joystick Left",
	},
	{
		.code		= KEY_RIGHT,
		.gpio		= AT91_PIN_PB15,
		.active_low	= 1,
		.desc		= "Joystick Right",
	},
	{
		.code		= KEY_UP,
		.gpio		= AT91_PIN_PB16,
		.active_low	= 1,
		.desc		= "Joystick Up",
	},
	{
		.code		= KEY_DOWN,
		.gpio		= AT91_PIN_PB17,
		.active_low	= 1,
		.desc		= "Joystick Down",
	},
	{
		.code		= KEY_ENTER,
		.gpio		= AT91_PIN_PB18,
		.active_low	= 1,
		.desc		= "Joystick Press",
	},
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
	int i;

	for (i = 0; i < ARRAY_SIZE(ek_buttons); i++) {
		at91_set_GPIO_periph(ek_buttons[i].gpio, 1);
		at91_set_deglitch(ek_buttons[i].gpio, 1);
	}

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
};


/*
 * LEDs ... these could all be PWM-driven, for variable brightness
 */
static struct gpio_led ek_leds[] = {
	{	/* "top" led, red, powerled */
		.name			= "d8",
		.gpio			= AT91_PIN_PD30,
		.default_trigger	= "heartbeat",
	},
	{	/* "left" led, green, userled2, pwm3 */
		.name			= "d6",
		.gpio			= AT91_PIN_PD0,
		.active_low		= 1,
		.default_trigger	= "nand-disk",
	},
#if !(defined(CONFIG_LEDS_ATMEL_PWM) || defined(CONFIG_LEDS_ATMEL_PWM_MODULE))
	{	/* "right" led, green, userled1, pwm1 */
		.name			= "d7",
		.gpio			= AT91_PIN_PD31,
		.active_low		= 1,
		.default_trigger	= "mmc0",
	},
#endif
};


/*
 * PWM Leds
 */
static struct gpio_led ek_pwm_led[] = {
#if defined(CONFIG_LEDS_ATMEL_PWM) || defined(CONFIG_LEDS_ATMEL_PWM_MODULE)
	{	/* "right" led, green, userled1, pwm1 */
		.name			= "d7",
		.gpio			= 1,	/* is PWM channel number */
		.active_low		= 1,
		.default_trigger	= "none",
	},
#endif
};



static void __init ek_board_init(void)
{
	/* Serial */
	at91_add_device_serial();
	/* USB HS Host */
	at91_add_device_usbh_ohci(&ek_usbh_hs_data);
	/* USB HS Device */
	at91_add_device_usba(&ek_usba_udc_data);
	/* SPI */
	at91_add_device_spi(ek_spi_devices, ARRAY_SIZE(ek_spi_devices));
	/* Ethernet */
	at91_add_device_eth(&ek_macb_data);
	/* NAND */
	ek_add_device_nand();
	/* I2C */
	at91_add_device_i2c(0, NULL, 0);
	/* LCD Controller */
	at91_add_device_lcdc(&ek_lcdc_data);
	/* Push Buttons */
	ek_add_device_buttons();
	/* AC97 */
	at91_add_device_ac97(&ek_ac97_data);
	/* LEDs */
	at91_gpio_leds(ek_leds, ARRAY_SIZE(ek_leds));
	at91_pwm_leds(ek_pwm_led, ARRAY_SIZE(ek_pwm_led));
}

MACHINE_START(AT91SAM9G45EKES, "Atmel AT91SAM9G45-EKES")
	/* Maintainer: Atmel */
	.phys_io	= AT91_BASE_SYS,
	.io_pg_offst	= (AT91_VA_BASE_SYS >> 18) & 0xfffc,
	.boot_params	= AT91_SDRAM_BASE + 0x100,
	.timer		= &at91sam926x_timer,
	.map_io		= ek_map_io,
	.init_irq	= ek_init_irq,
	.init_machine	= ek_board_init,
MACHINE_END
