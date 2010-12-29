/*
 *  linux/arch/arm/mach-pxa/z2.c
 *
 *  Support for the Zipit Z2 Handheld device.
 *
 *  Copyright (C) 2009-2010 Marek Vasut <marek.vasut@gmail.com>
 *
 *  Based on research and code by: Ken McGuire
 *  Based on mainstone.c as modified for the Zipit Z2.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/pwm_backlight.h>
#include <linux/z2_battery.h>
#include <linux/dma-mapping.h>
#include <linux/spi/spi.h>
#include <linux/spi/pxa2xx_spi.h>
#include <linux/spi/libertas_spi.h>
#include <linux/spi/lms283gf05.h>
#include <linux/power_supply.h>
#include <linux/mtd/physmap.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/delay.h>
#include <linux/regulator/machine.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/pxa27x.h>
#include <mach/mfp-pxa27x.h>
#include <mach/z2.h>
#include <mach/pxafb.h>
#include <mach/mmc.h>
#include <plat/pxa27x_keypad.h>

#include <plat/i2c.h>

#include "generic.h"
#include "devices.h"

/******************************************************************************
 * Pin configuration
 ******************************************************************************/
static unsigned long z2_pin_config[] = {

	/* LCD - 16bpp Active TFT */
	GPIO58_LCD_LDD_0,
	GPIO59_LCD_LDD_1,
	GPIO60_LCD_LDD_2,
	GPIO61_LCD_LDD_3,
	GPIO62_LCD_LDD_4,
	GPIO63_LCD_LDD_5,
	GPIO64_LCD_LDD_6,
	GPIO65_LCD_LDD_7,
	GPIO66_LCD_LDD_8,
	GPIO67_LCD_LDD_9,
	GPIO68_LCD_LDD_10,
	GPIO69_LCD_LDD_11,
	GPIO70_LCD_LDD_12,
	GPIO71_LCD_LDD_13,
	GPIO72_LCD_LDD_14,
	GPIO73_LCD_LDD_15,
	GPIO74_LCD_FCLK,
	GPIO75_LCD_LCLK,
	GPIO76_LCD_PCLK,
	GPIO77_LCD_BIAS,
	GPIO19_GPIO,		/* LCD reset */
	GPIO88_GPIO,		/* LCD chipselect */

	/* PWM */
	GPIO115_PWM1_OUT,	/* Keypad Backlight */
	GPIO11_PWM2_OUT,	/* LCD Backlight */

	/* MMC */
	GPIO32_MMC_CLK,
	GPIO112_MMC_CMD,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO96_GPIO,		/* SD detect */

	/* STUART */
	GPIO46_STUART_RXD,
	GPIO47_STUART_TXD,

	/* Keypad */
	GPIO100_KP_MKIN_0	| WAKEUP_ON_LEVEL_HIGH,
	GPIO101_KP_MKIN_1	| WAKEUP_ON_LEVEL_HIGH,
	GPIO102_KP_MKIN_2	| WAKEUP_ON_LEVEL_HIGH,
	GPIO34_KP_MKIN_3	| WAKEUP_ON_LEVEL_HIGH,
	GPIO38_KP_MKIN_4	| WAKEUP_ON_LEVEL_HIGH,
	GPIO16_KP_MKIN_5	| WAKEUP_ON_LEVEL_HIGH,
	GPIO17_KP_MKIN_6	| WAKEUP_ON_LEVEL_HIGH,
	GPIO103_KP_MKOUT_0,
	GPIO104_KP_MKOUT_1,
	GPIO105_KP_MKOUT_2,
	GPIO106_KP_MKOUT_3,
	GPIO107_KP_MKOUT_4,
	GPIO108_KP_MKOUT_5,
	GPIO35_KP_MKOUT_6,
	GPIO41_KP_MKOUT_7,

	/* I2C */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,

	/* SSP1 */
	GPIO23_SSP1_SCLK,	/* SSP1_SCK */
	GPIO25_SSP1_TXD,	/* SSP1_TXD */
	GPIO26_SSP1_RXD,	/* SSP1_RXD */

	/* SSP2 */
	GPIO22_SSP2_SCLK,	/* SSP2_SCK */
	GPIO13_SSP2_TXD,	/* SSP2_TXD */
	GPIO40_SSP2_RXD,	/* SSP2_RXD */

	/* LEDs */
	GPIO10_GPIO,		/* WiFi LED */
	GPIO83_GPIO,		/* Charging LED */
	GPIO85_GPIO,		/* Charged LED */

	/* I2S */
	GPIO28_I2S_BITCLK_OUT,
	GPIO29_I2S_SDATA_IN,
	GPIO30_I2S_SDATA_OUT,
	GPIO31_I2S_SYNC,
	GPIO113_I2S_SYSCLK,

	/* MISC */
	GPIO0_GPIO,		/* AC power detect */
	GPIO1_GPIO,		/* Power button */
	GPIO37_GPIO,		/* Headphone detect */
	GPIO98_GPIO,		/* Lid switch */
	GPIO14_GPIO,		/* WiFi Reset */
	GPIO15_GPIO,		/* WiFi Power */
	GPIO24_GPIO,		/* WiFi CS */
	GPIO36_GPIO,		/* WiFi IRQ */
	GPIO88_GPIO,		/* LCD CS */
};

/******************************************************************************
 * NOR Flash
 ******************************************************************************/
#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
static struct resource z2_flash_resource = {
	.start	= PXA_CS0_PHYS,
	.end	= PXA_CS0_PHYS + SZ_8M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct mtd_partition z2_flash_parts[] = {
	{
		.name	= "U-Boot Bootloader",
		.offset	= 0x0,
		.size	= 0x40000,
	}, {
		.name	= "U-Boot Environment",
		.offset	= 0x40000,
		.size	= 0x20000,
	}, {
		.name	= "Flash",
		.offset	= 0x60000,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data z2_flash_data = {
	.width		= 2,
	.parts		= z2_flash_parts,
	.nr_parts	= ARRAY_SIZE(z2_flash_parts),
};

static struct platform_device z2_flash = {
	.name		= "physmap-flash",
	.id		= -1,
	.resource	= &z2_flash_resource,
	.num_resources	= 1,
	.dev = {
		.platform_data	= &z2_flash_data,
	},
};

static void __init z2_nor_init(void)
{
	platform_device_register(&z2_flash);
}
#else
static inline void z2_nor_init(void) {}
#endif

/******************************************************************************
 * Backlight
 ******************************************************************************/
#if defined(CONFIG_BACKLIGHT_PWM) || defined(CONFIG_BACKLIGHT_PWM_MODULE)
static struct platform_pwm_backlight_data z2_backlight_data[] = {
	[0] = {
		/* Keypad Backlight */
		.pwm_id		= 1,
		.max_brightness	= 1023,
		.dft_brightness	= 512,
		.pwm_period_ns	= 1260320,
	},
	[1] = {
		/* LCD Backlight */
		.pwm_id		= 2,
		.max_brightness	= 1023,
		.dft_brightness	= 512,
		.pwm_period_ns	= 1260320,
	},
};

static struct platform_device z2_backlight_devices[2] = {
	{
		.name	= "pwm-backlight",
		.id	= 0,
		.dev	= {
			.platform_data	= &z2_backlight_data[1],
		},
	},
	{
		.name	= "pwm-backlight",
		.id	= 1,
		.dev	= {
			.platform_data	= &z2_backlight_data[0],
		},
	},
};
static void __init z2_pwm_init(void)
{
	platform_device_register(&z2_backlight_devices[0]);
	platform_device_register(&z2_backlight_devices[1]);
}
#else
static inline void z2_pwm_init(void) {}
#endif

/******************************************************************************
 * Framebuffer
 ******************************************************************************/
#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
static struct pxafb_mode_info z2_lcd_modes[] = {
{
	.pixclock	= 192000,
	.xres		= 240,
	.yres		= 320,
	.bpp		= 16,

	.left_margin	= 4,
	.right_margin	= 8,
	.upper_margin	= 4,
	.lower_margin	= 8,

	.hsync_len	= 4,
	.vsync_len	= 4,
},
};

static struct pxafb_mach_info z2_lcd_screen = {
	.modes		= z2_lcd_modes,
	.num_modes      = ARRAY_SIZE(z2_lcd_modes),
	.lcd_conn	= LCD_COLOR_TFT_16BPP | LCD_BIAS_ACTIVE_LOW |
			  LCD_ALTERNATE_MAPPING,
};

static void __init z2_lcd_init(void)
{
	set_pxa_fb_info(&z2_lcd_screen);
}
#else
static inline void z2_lcd_init(void) {}
#endif

/******************************************************************************
 * SD/MMC card controller
 ******************************************************************************/
#if defined(CONFIG_MMC_PXA) || defined(CONFIG_MMC_PXA_MODULE)
static struct pxamci_platform_data z2_mci_platform_data = {
	.ocr_mask		= MMC_VDD_32_33 | MMC_VDD_33_34,
	.gpio_card_detect	= GPIO96_ZIPITZ2_SD_DETECT,
	.gpio_power		= -1,
	.gpio_card_ro		= -1,
	.detect_delay_ms	= 200,
};

static void __init z2_mmc_init(void)
{
	pxa_set_mci_info(&z2_mci_platform_data);
}
#else
static inline void z2_mmc_init(void) {}
#endif

/******************************************************************************
 * LEDs
 ******************************************************************************/
#if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)
struct gpio_led z2_gpio_leds[] = {
{
	.name			= "z2:green:wifi",
	.default_trigger	= "none",
	.gpio			= GPIO10_ZIPITZ2_LED_WIFI,
	.active_low		= 1,
}, {
	.name			= "z2:green:charged",
	.default_trigger	= "none",
	.gpio			= GPIO85_ZIPITZ2_LED_CHARGED,
	.active_low		= 1,
}, {
	.name			= "z2:amber:charging",
	.default_trigger	= "none",
	.gpio			= GPIO83_ZIPITZ2_LED_CHARGING,
	.active_low		= 1,
},
};

static struct gpio_led_platform_data z2_gpio_led_info = {
	.leds		= z2_gpio_leds,
	.num_leds	= ARRAY_SIZE(z2_gpio_leds),
};

static struct platform_device z2_leds = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &z2_gpio_led_info,
	}
};

static void __init z2_leds_init(void)
{
	platform_device_register(&z2_leds);
}
#else
static inline void z2_leds_init(void) {}
#endif

/******************************************************************************
 * GPIO keyboard
 ******************************************************************************/
#if defined(CONFIG_KEYBOARD_PXA27x) || defined(CONFIG_KEYBOARD_PXA27x_MODULE)
static unsigned int z2_matrix_keys[] = {
	KEY(0, 0, KEY_OPTION),
	KEY(1, 0, KEY_UP),
	KEY(2, 0, KEY_DOWN),
	KEY(3, 0, KEY_LEFT),
	KEY(4, 0, KEY_RIGHT),
	KEY(5, 0, KEY_END),
	KEY(6, 0, KEY_KPPLUS),

	KEY(0, 1, KEY_HOME),
	KEY(1, 1, KEY_Q),
	KEY(2, 1, KEY_I),
	KEY(3, 1, KEY_G),
	KEY(4, 1, KEY_X),
	KEY(5, 1, KEY_ENTER),
	KEY(6, 1, KEY_KPMINUS),

	KEY(0, 2, KEY_PAGEUP),
	KEY(1, 2, KEY_W),
	KEY(2, 2, KEY_O),
	KEY(3, 2, KEY_H),
	KEY(4, 2, KEY_C),
	KEY(5, 2, KEY_LEFTALT),

	KEY(0, 3, KEY_PAGEDOWN),
	KEY(1, 3, KEY_E),
	KEY(2, 3, KEY_P),
	KEY(3, 3, KEY_J),
	KEY(4, 3, KEY_V),
	KEY(5, 3, KEY_LEFTSHIFT),

	KEY(0, 4, KEY_ESC),
	KEY(1, 4, KEY_R),
	KEY(2, 4, KEY_A),
	KEY(3, 4, KEY_K),
	KEY(4, 4, KEY_B),
	KEY(5, 4, KEY_LEFTCTRL),

	KEY(0, 5, KEY_TAB),
	KEY(1, 5, KEY_T),
	KEY(2, 5, KEY_S),
	KEY(3, 5, KEY_L),
	KEY(4, 5, KEY_N),
	KEY(5, 5, KEY_SPACE),

	KEY(0, 6, KEY_STOPCD),
	KEY(1, 6, KEY_Y),
	KEY(2, 6, KEY_D),
	KEY(3, 6, KEY_BACKSPACE),
	KEY(4, 6, KEY_M),
	KEY(5, 6, KEY_COMMA),

	KEY(0, 7, KEY_PLAYCD),
	KEY(1, 7, KEY_U),
	KEY(2, 7, KEY_F),
	KEY(3, 7, KEY_Z),
	KEY(4, 7, KEY_SEMICOLON),
	KEY(5, 7, KEY_DOT),
};

static struct pxa27x_keypad_platform_data z2_keypad_platform_data = {
	.matrix_key_rows	= 7,
	.matrix_key_cols	= 8,
	.matrix_key_map		= z2_matrix_keys,
	.matrix_key_map_size	= ARRAY_SIZE(z2_matrix_keys),

	.debounce_interval	= 30,
};

static void __init z2_mkp_init(void)
{
	pxa_set_keypad_info(&z2_keypad_platform_data);
}
#else
static inline void z2_mkp_init(void) {}
#endif

/******************************************************************************
 * GPIO keys
 ******************************************************************************/
#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
static struct gpio_keys_button z2_pxa_buttons[] = {
	{KEY_POWER, GPIO1_ZIPITZ2_POWER_BUTTON, 0, "Power Button" },
	{KEY_CLOSE, GPIO98_ZIPITZ2_LID_BUTTON, 0, "Lid Button" },
};

static struct gpio_keys_platform_data z2_pxa_keys_data = {
	.buttons	= z2_pxa_buttons,
	.nbuttons	= ARRAY_SIZE(z2_pxa_buttons),
};

static struct platform_device z2_pxa_keys = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data = &z2_pxa_keys_data,
	},
};

static void __init z2_keys_init(void)
{
	platform_device_register(&z2_pxa_keys);
}
#else
static inline void z2_keys_init(void) {}
#endif

/******************************************************************************
 * Battery
 ******************************************************************************/
#if defined(CONFIG_I2C_PXA) || defined(CONFIG_I2C_PXA_MODULE)
static struct z2_battery_info batt_chip_info = {
	.batt_I2C_bus	= 0,
	.batt_I2C_addr	= 0x55,
	.batt_I2C_reg	= 2,
	.charge_gpio	= GPIO0_ZIPITZ2_AC_DETECT,
	.min_voltage	= 2400000,
	.max_voltage	= 3700000,
	.batt_div	= 69,
	.batt_mult	= 1000000,
	.batt_tech	= POWER_SUPPLY_TECHNOLOGY_LION,
	.batt_name	= "Z2",
};

static struct i2c_board_info __initdata z2_i2c_board_info[] = {
	{
		I2C_BOARD_INFO("aer915", 0x55),
		.platform_data	= &batt_chip_info,
	}, {
		I2C_BOARD_INFO("wm8750", 0x1b),
	},

};

static void __init z2_i2c_init(void)
{
	pxa_set_i2c_info(NULL);
	i2c_register_board_info(0, ARRAY_AND_SIZE(z2_i2c_board_info));
}
#else
static inline void z2_i2c_init(void) {}
#endif

/******************************************************************************
 * SSP Devices - WiFi and LCD control
 ******************************************************************************/
#if defined(CONFIG_SPI_PXA2XX) || defined(CONFIG_SPI_PXA2XX_MODULE)
/* WiFi */
static int z2_lbs_spi_setup(struct spi_device *spi)
{
	int ret = 0;

	ret = gpio_request(GPIO15_ZIPITZ2_WIFI_POWER, "WiFi Power");
	if (ret)
		goto err;

	ret = gpio_direction_output(GPIO15_ZIPITZ2_WIFI_POWER, 1);
	if (ret)
		goto err2;

	ret = gpio_request(GPIO14_ZIPITZ2_WIFI_RESET, "WiFi Reset");
	if (ret)
		goto err2;

	ret = gpio_direction_output(GPIO14_ZIPITZ2_WIFI_RESET, 0);
	if (ret)
		goto err3;

	/* Reset the card */
	mdelay(180);
	gpio_set_value(GPIO14_ZIPITZ2_WIFI_RESET, 1);
	mdelay(20);

	spi->bits_per_word = 16;
	spi->mode = SPI_MODE_2,

	spi_setup(spi);

	return 0;

err3:
	gpio_free(GPIO14_ZIPITZ2_WIFI_RESET);
err2:
	gpio_free(GPIO15_ZIPITZ2_WIFI_POWER);
err:
	return ret;
};

static int z2_lbs_spi_teardown(struct spi_device *spi)
{
	gpio_set_value(GPIO14_ZIPITZ2_WIFI_RESET, 0);
	gpio_set_value(GPIO15_ZIPITZ2_WIFI_POWER, 0);
	gpio_free(GPIO14_ZIPITZ2_WIFI_RESET);
	gpio_free(GPIO15_ZIPITZ2_WIFI_POWER);
	return 0;

};

static struct pxa2xx_spi_chip z2_lbs_chip_info = {
	.rx_threshold	= 8,
	.tx_threshold	= 8,
	.timeout	= 1000,
	.gpio_cs	= GPIO24_ZIPITZ2_WIFI_CS,
};

static struct libertas_spi_platform_data z2_lbs_pdata = {
	.use_dummy_writes	= 1,
	.setup			= z2_lbs_spi_setup,
	.teardown		= z2_lbs_spi_teardown,
};

/* LCD */
static struct pxa2xx_spi_chip lms283_chip_info = {
	.rx_threshold	= 1,
	.tx_threshold	= 1,
	.timeout	= 64,
	.gpio_cs	= GPIO88_ZIPITZ2_LCD_CS,
};

static const struct lms283gf05_pdata lms283_pdata = {
	.reset_gpio	= GPIO19_ZIPITZ2_LCD_RESET,
};

static struct spi_board_info spi_board_info[] __initdata = {
{
	.modalias		= "libertas_spi",
	.platform_data		= &z2_lbs_pdata,
	.controller_data	= &z2_lbs_chip_info,
	.irq			= gpio_to_irq(GPIO36_ZIPITZ2_WIFI_IRQ),
	.max_speed_hz		= 13000000,
	.bus_num		= 1,
	.chip_select		= 0,
},
{
	.modalias		= "lms283gf05",
	.controller_data	= &lms283_chip_info,
	.platform_data		= &lms283_pdata,
	.max_speed_hz		= 400000,
	.bus_num		= 2,
	.chip_select		= 0,
},
};

static struct pxa2xx_spi_master pxa_ssp1_master_info = {
	.clock_enable	= CKEN_SSP,
	.num_chipselect	= 1,
	.enable_dma	= 1,
};

static struct pxa2xx_spi_master pxa_ssp2_master_info = {
	.clock_enable	= CKEN_SSP2,
	.num_chipselect	= 1,
};

static void __init z2_spi_init(void)
{
	pxa2xx_set_spi_info(1, &pxa_ssp1_master_info);
	pxa2xx_set_spi_info(2, &pxa_ssp2_master_info);
	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));
}
#else
static inline void z2_spi_init(void) {}
#endif

/******************************************************************************
 * Core power regulator
 ******************************************************************************/
#if defined(CONFIG_REGULATOR_TPS65023) || \
	defined(CONFIG_REGULATOR_TPS65023_MODULE)
static struct regulator_consumer_supply z2_tps65021_consumers[] = {
	{
		.supply	= "vcc_core",
	}
};

static struct regulator_init_data z2_tps65021_info[] = {
	{
		.constraints = {
			.name		= "vcc_core range",
			.min_uV		= 800000,
			.max_uV		= 1600000,
			.always_on	= 1,
			.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE,
		},
		.consumer_supplies	= z2_tps65021_consumers,
		.num_consumer_supplies	= ARRAY_SIZE(z2_tps65021_consumers),
	}, {
		.constraints = {
			.name		= "DCDC2",
			.min_uV		= 3300000,
			.max_uV		= 3300000,
			.always_on	= 1,
		},
	}, {
		.constraints = {
			.name		= "DCDC3",
			.min_uV		= 1800000,
			.max_uV		= 1800000,
			.always_on	= 1,
		},
	}, {
		.constraints = {
			.name		= "LDO1",
			.min_uV		= 1000000,
			.max_uV		= 3150000,
			.always_on	= 1,
		},
	}, {
		.constraints = {
			.name		= "LDO2",
			.min_uV		= 1050000,
			.max_uV		= 3300000,
			.always_on	= 1,
		},
	}
};

static struct i2c_board_info __initdata z2_pi2c_board_info[] = {
	{
		I2C_BOARD_INFO("tps65021", 0x48),
		.platform_data	= &z2_tps65021_info,
	},
};

static void __init z2_pmic_init(void)
{
	pxa27x_set_i2c_power_info(NULL);
	i2c_register_board_info(1, ARRAY_AND_SIZE(z2_pi2c_board_info));
}
#else
static inline void z2_pmic_init(void) {}
#endif

/******************************************************************************
 * Machine init
 ******************************************************************************/
static void __init z2_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(z2_pin_config));

	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	z2_lcd_init();
	z2_mmc_init();
	z2_mkp_init();
	z2_i2c_init();
	z2_spi_init();
	z2_nor_init();
	z2_pwm_init();
	z2_leds_init();
	z2_keys_init();
	z2_pmic_init();
}

MACHINE_START(ZIPIT2, "Zipit Z2")
	.boot_params	= 0xa0000100,
	.map_io		= pxa_map_io,
	.init_irq	= pxa27x_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= z2_init,
MACHINE_END
