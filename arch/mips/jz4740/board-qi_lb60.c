/*
 * linux/arch/mips/jz4740/board-qi_lb60.c
 *
 * QI_LB60 board support
 *
 * Copyright (c) 2009 Qi Hardware inc.,
 * Author: Xiangfu Liu <xiangfu@qi-hardware.com>
 * Copyright 2010, Lars-Peter Clausen <lars@metafoo.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or later
 * as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/gpio/machine.h>

#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/input/matrix_keypad.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_gpio.h>
#include <linux/power_supply.h>
#include <linux/power/jz4740-battery.h>
#include <linux/power/gpio-charger.h>
#include <linux/pwm.h>

#include <asm/mach-jz4740/gpio.h>
#include <asm/mach-jz4740/jz4740_fb.h>
#include <asm/mach-jz4740/jz4740_mmc.h>
#include <asm/mach-jz4740/jz4740_nand.h>

#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>

#include <asm/mach-jz4740/platform.h>

#include "clock.h"

/* GPIOs */
#define QI_LB60_GPIO_SD_CD		JZ_GPIO_PORTD(0)
#define QI_LB60_GPIO_SD_VCC_EN_N	JZ_GPIO_PORTD(2)

#define QI_LB60_GPIO_KEYOUT(x)		(JZ_GPIO_PORTC(10) + (x))
#define QI_LB60_GPIO_KEYIN(x)		(JZ_GPIO_PORTD(18) + (x))
#define QI_LB60_GPIO_KEYIN8		JZ_GPIO_PORTD(26)

/* NAND */

/* Early prototypes of the QI LB60 had only 1GB of NAND.
 * In order to support these devices as well the partition and ecc layout is
 * initialized depending on the NAND size */
static struct mtd_partition qi_lb60_partitions_1gb[] = {
	{
		.name = "NAND BOOT partition",
		.offset = 0 * 0x100000,
		.size = 4 * 0x100000,
	},
	{
		.name = "NAND KERNEL partition",
		.offset = 4 * 0x100000,
		.size = 4 * 0x100000,
	},
	{
		.name = "NAND ROOTFS partition",
		.offset = 8 * 0x100000,
		.size = (504 + 512) * 0x100000,
	},
};

static struct mtd_partition qi_lb60_partitions_2gb[] = {
	{
		.name = "NAND BOOT partition",
		.offset = 0 * 0x100000,
		.size = 4 * 0x100000,
	},
	{
		.name = "NAND KERNEL partition",
		.offset = 4 * 0x100000,
		.size = 4 * 0x100000,
	},
	{
		.name = "NAND ROOTFS partition",
		.offset = 8 * 0x100000,
		.size = (504 + 512 + 1024) * 0x100000,
	},
};

static int qi_lb60_ooblayout_ecc(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *oobregion)
{
	if (section)
		return -ERANGE;

	oobregion->length = 36;
	oobregion->offset = 6;

	if (mtd->oobsize == 128) {
		oobregion->length *= 2;
		oobregion->offset *= 2;
	}

	return 0;
}

static int qi_lb60_ooblayout_free(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *oobregion)
{
	int eccbytes = 36, eccoff = 6;

	if (section > 1)
		return -ERANGE;

	if (mtd->oobsize == 128) {
		eccbytes *= 2;
		eccoff *= 2;
	}

	if (!section) {
		oobregion->offset = 2;
		oobregion->length = eccoff - 2;
	} else {
		oobregion->offset = eccoff + eccbytes;
		oobregion->length = mtd->oobsize - oobregion->offset;
	}

	return 0;
}

static const struct mtd_ooblayout_ops qi_lb60_ooblayout_ops = {
	.ecc = qi_lb60_ooblayout_ecc,
	.free = qi_lb60_ooblayout_free,
};

static void qi_lb60_nand_ident(struct platform_device *pdev,
		struct mtd_info *mtd, struct mtd_partition **partitions,
		int *num_partitions)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (chip->page_shift == 12) {
		*partitions = qi_lb60_partitions_2gb;
		*num_partitions = ARRAY_SIZE(qi_lb60_partitions_2gb);
	} else {
		*partitions = qi_lb60_partitions_1gb;
		*num_partitions = ARRAY_SIZE(qi_lb60_partitions_1gb);
	}

	mtd_set_ooblayout(mtd, &qi_lb60_ooblayout_ops);
}

static struct jz_nand_platform_data qi_lb60_nand_pdata = {
	.ident_callback = qi_lb60_nand_ident,
	.banks = { 1 },
};

static struct gpiod_lookup_table qi_lb60_nand_gpio_table = {
	.dev_id = "jz4740-nand.0",
	.table = {
		GPIO_LOOKUP("Bank C", 30, "busy", 0),
		{ },
	},
};


/* Keyboard*/

#define KEY_QI_QI	KEY_F13
#define KEY_QI_UPRED	KEY_RIGHTALT
#define KEY_QI_VOLUP	KEY_VOLUMEUP
#define KEY_QI_VOLDOWN	KEY_VOLUMEDOWN
#define KEY_QI_FN	KEY_LEFTCTRL

static const uint32_t qi_lb60_keymap[] = {
	KEY(0, 0, KEY_F1),	/* S2 */
	KEY(0, 1, KEY_F2),	/* S3 */
	KEY(0, 2, KEY_F3),	/* S4 */
	KEY(0, 3, KEY_F4),	/* S5 */
	KEY(0, 4, KEY_F5),	/* S6 */
	KEY(0, 5, KEY_F6),	/* S7 */
	KEY(0, 6, KEY_F7),	/* S8 */

	KEY(1, 0, KEY_Q),	/* S10 */
	KEY(1, 1, KEY_W),	/* S11 */
	KEY(1, 2, KEY_E),	/* S12 */
	KEY(1, 3, KEY_R),	/* S13 */
	KEY(1, 4, KEY_T),	/* S14 */
	KEY(1, 5, KEY_Y),	/* S15 */
	KEY(1, 6, KEY_U),	/* S16 */
	KEY(1, 7, KEY_I),	/* S17 */
	KEY(2, 0, KEY_A),	/* S18 */
	KEY(2, 1, KEY_S),	/* S19 */
	KEY(2, 2, KEY_D),	/* S20 */
	KEY(2, 3, KEY_F),	/* S21 */
	KEY(2, 4, KEY_G),	/* S22 */
	KEY(2, 5, KEY_H),	/* S23 */
	KEY(2, 6, KEY_J),	/* S24 */
	KEY(2, 7, KEY_K),	/* S25 */
	KEY(3, 0, KEY_ESC),	/* S26 */
	KEY(3, 1, KEY_Z),	/* S27 */
	KEY(3, 2, KEY_X),	/* S28 */
	KEY(3, 3, KEY_C),	/* S29 */
	KEY(3, 4, KEY_V),	/* S30 */
	KEY(3, 5, KEY_B),	/* S31 */
	KEY(3, 6, KEY_N),	/* S32 */
	KEY(3, 7, KEY_M),	/* S33 */
	KEY(4, 0, KEY_TAB),	/* S34 */
	KEY(4, 1, KEY_CAPSLOCK),	/* S35 */
	KEY(4, 2, KEY_BACKSLASH),	/* S36 */
	KEY(4, 3, KEY_APOSTROPHE),	/* S37 */
	KEY(4, 4, KEY_COMMA),	/* S38 */
	KEY(4, 5, KEY_DOT),	/* S39 */
	KEY(4, 6, KEY_SLASH),	/* S40 */
	KEY(4, 7, KEY_UP),	/* S41 */
	KEY(5, 0, KEY_O),	/* S42 */
	KEY(5, 1, KEY_L),	/* S43 */
	KEY(5, 2, KEY_EQUAL),	/* S44 */
	KEY(5, 3, KEY_QI_UPRED),	/* S45 */
	KEY(5, 4, KEY_SPACE),	/* S46 */
	KEY(5, 5, KEY_QI_QI),	/* S47 */
	KEY(5, 6, KEY_RIGHTCTRL),	/* S48 */
	KEY(5, 7, KEY_LEFT),	/* S49 */
	KEY(6, 0, KEY_F8),	/* S50 */
	KEY(6, 1, KEY_P),	/* S51 */
	KEY(6, 2, KEY_BACKSPACE),/* S52 */
	KEY(6, 3, KEY_ENTER),	/* S53 */
	KEY(6, 4, KEY_QI_VOLUP),	/* S54 */
	KEY(6, 5, KEY_QI_VOLDOWN),	/* S55 */
	KEY(6, 6, KEY_DOWN),	/* S56 */
	KEY(6, 7, KEY_RIGHT),	/* S57 */

	KEY(7, 0, KEY_LEFTSHIFT),	/* S58 */
	KEY(7, 1, KEY_LEFTALT), /* S59 */
	KEY(7, 2, KEY_QI_FN),	/* S60 */
};

static const struct matrix_keymap_data qi_lb60_keymap_data = {
	.keymap		= qi_lb60_keymap,
	.keymap_size	= ARRAY_SIZE(qi_lb60_keymap),
};

static const unsigned int qi_lb60_keypad_cols[] = {
	QI_LB60_GPIO_KEYOUT(0),
	QI_LB60_GPIO_KEYOUT(1),
	QI_LB60_GPIO_KEYOUT(2),
	QI_LB60_GPIO_KEYOUT(3),
	QI_LB60_GPIO_KEYOUT(4),
	QI_LB60_GPIO_KEYOUT(5),
	QI_LB60_GPIO_KEYOUT(6),
	QI_LB60_GPIO_KEYOUT(7),
};

static const unsigned int qi_lb60_keypad_rows[] = {
	QI_LB60_GPIO_KEYIN(0),
	QI_LB60_GPIO_KEYIN(1),
	QI_LB60_GPIO_KEYIN(2),
	QI_LB60_GPIO_KEYIN(3),
	QI_LB60_GPIO_KEYIN(4),
	QI_LB60_GPIO_KEYIN(5),
	QI_LB60_GPIO_KEYIN(6),
	QI_LB60_GPIO_KEYIN8,
};

static struct matrix_keypad_platform_data qi_lb60_pdata = {
	.keymap_data = &qi_lb60_keymap_data,
	.col_gpios	= qi_lb60_keypad_cols,
	.row_gpios	= qi_lb60_keypad_rows,
	.num_col_gpios	= ARRAY_SIZE(qi_lb60_keypad_cols),
	.num_row_gpios	= ARRAY_SIZE(qi_lb60_keypad_rows),
	.col_scan_delay_us	= 10,
	.debounce_ms		= 10,
	.wakeup			= 1,
	.active_low		= 1,
};

static struct platform_device qi_lb60_keypad = {
	.name		= "matrix-keypad",
	.id		= -1,
	.dev		= {
		.platform_data = &qi_lb60_pdata,
	},
};

/* Display */
static struct fb_videomode qi_lb60_video_modes[] = {
	{
		.name = "320x240",
		.xres = 320,
		.yres = 240,
		.refresh = 30,
		.left_margin = 140,
		.right_margin = 273,
		.upper_margin = 20,
		.lower_margin = 2,
		.hsync_len = 1,
		.vsync_len = 1,
		.sync = 0,
		.vmode = FB_VMODE_NONINTERLACED,
	},
};

static struct jz4740_fb_platform_data qi_lb60_fb_pdata = {
	.width		= 60,
	.height		= 45,
	.num_modes	= ARRAY_SIZE(qi_lb60_video_modes),
	.modes		= qi_lb60_video_modes,
	.bpp		= 24,
	.lcd_type	= JZ_LCD_TYPE_8BIT_SERIAL,
	.pixclk_falling_edge = 1,
};

struct spi_gpio_platform_data spigpio_platform_data = {
	.sck = JZ_GPIO_PORTC(23),
	.mosi = JZ_GPIO_PORTC(22),
	.miso = -1,
	.num_chipselect = 1,
};

static struct platform_device spigpio_device = {
	.name = "spi_gpio",
	.id   = 1,
	.dev = {
		.platform_data = &spigpio_platform_data,
	},
};

static struct spi_board_info qi_lb60_spi_board_info[] = {
	{
		.modalias = "ili8960",
		.controller_data = (void *)JZ_GPIO_PORTC(21),
		.chip_select = 0,
		.bus_num = 1,
		.max_speed_hz = 30 * 1000,
		.mode = SPI_3WIRE,
	},
};

/* Battery */
static struct jz_battery_platform_data qi_lb60_battery_pdata = {
	.gpio_charge =	JZ_GPIO_PORTC(27),
	.gpio_charge_active_low = 1,
	.info = {
		.name = "battery",
		.technology = POWER_SUPPLY_TECHNOLOGY_LIPO,
		.voltage_max_design = 4200000,
		.voltage_min_design = 3600000,
	},
};

/* GPIO Key: power */
static struct gpio_keys_button qi_lb60_gpio_keys_buttons[] = {
	[0] = {
		.code		= KEY_POWER,
		.gpio		= JZ_GPIO_PORTD(29),
		.active_low	= 1,
		.desc		= "Power",
		.wakeup		= 1,
	},
};

static struct gpio_keys_platform_data qi_lb60_gpio_keys_data = {
	.nbuttons = ARRAY_SIZE(qi_lb60_gpio_keys_buttons),
	.buttons = qi_lb60_gpio_keys_buttons,
};

static struct platform_device qi_lb60_gpio_keys = {
	.name = "gpio-keys",
	.id =	-1,
	.dev = {
		.platform_data = &qi_lb60_gpio_keys_data,
	}
};

static struct jz4740_mmc_platform_data qi_lb60_mmc_pdata = {
	.gpio_card_detect	= QI_LB60_GPIO_SD_CD,
	.gpio_read_only		= -1,
	.gpio_power		= QI_LB60_GPIO_SD_VCC_EN_N,
	.power_active_low	= 1,
};

/* beeper */
static struct pwm_lookup qi_lb60_pwm_lookup[] = {
	PWM_LOOKUP("jz4740-pwm", 4, "pwm-beeper", NULL, 0,
		   PWM_POLARITY_NORMAL),
};

static struct platform_device qi_lb60_pwm_beeper = {
	.name = "pwm-beeper",
	.id = -1,
};

/* charger */
static char *qi_lb60_batteries[] = {
	"battery",
};

static struct gpio_charger_platform_data qi_lb60_charger_pdata = {
	.name = "usb",
	.type = POWER_SUPPLY_TYPE_USB,
	.gpio = JZ_GPIO_PORTD(28),
	.gpio_active_low = 1,
	.supplied_to = qi_lb60_batteries,
	.num_supplicants = ARRAY_SIZE(qi_lb60_batteries),
};

static struct platform_device qi_lb60_charger_device = {
	.name = "gpio-charger",
	.dev = {
		.platform_data = &qi_lb60_charger_pdata,
	},
};

/* audio */
static struct platform_device qi_lb60_audio_device = {
	.name = "qi-lb60-audio",
	.id = -1,
};

static struct gpiod_lookup_table qi_lb60_audio_gpio_table = {
	.dev_id = "qi-lb60-audio",
	.table = {
		GPIO_LOOKUP("Bank B", 29, "snd", 0),
		GPIO_LOOKUP("Bank D", 4, "amp", 0),
		{ },
	},
};

static struct platform_device *jz_platform_devices[] __initdata = {
	&jz4740_udc_device,
	&jz4740_udc_xceiv_device,
	&jz4740_mmc_device,
	&jz4740_nand_device,
	&qi_lb60_keypad,
	&spigpio_device,
	&jz4740_framebuffer_device,
	&jz4740_pcm_device,
	&jz4740_i2s_device,
	&jz4740_codec_device,
	&jz4740_rtc_device,
	&jz4740_adc_device,
	&jz4740_pwm_device,
	&jz4740_dma_device,
	&qi_lb60_gpio_keys,
	&qi_lb60_pwm_beeper,
	&qi_lb60_charger_device,
	&qi_lb60_audio_device,
};

static void __init board_gpio_setup(void)
{
	/* We only need to enable/disable pullup here for pins used in generic
	 * drivers. Everything else is done by the drivers themselves. */
	jz_gpio_disable_pullup(QI_LB60_GPIO_SD_VCC_EN_N);
	jz_gpio_disable_pullup(QI_LB60_GPIO_SD_CD);
}

static int __init qi_lb60_init_platform_devices(void)
{
	jz4740_framebuffer_device.dev.platform_data = &qi_lb60_fb_pdata;
	jz4740_nand_device.dev.platform_data = &qi_lb60_nand_pdata;
	jz4740_adc_device.dev.platform_data = &qi_lb60_battery_pdata;
	jz4740_mmc_device.dev.platform_data = &qi_lb60_mmc_pdata;

	gpiod_add_lookup_table(&qi_lb60_audio_gpio_table);
	gpiod_add_lookup_table(&qi_lb60_nand_gpio_table);

	spi_register_board_info(qi_lb60_spi_board_info,
				ARRAY_SIZE(qi_lb60_spi_board_info));

	pwm_add_table(qi_lb60_pwm_lookup, ARRAY_SIZE(qi_lb60_pwm_lookup));

	return platform_add_devices(jz_platform_devices,
					ARRAY_SIZE(jz_platform_devices));

}

static int __init qi_lb60_board_setup(void)
{
	printk(KERN_INFO "Qi Hardware JZ4740 QI LB60 setup\n");

	board_gpio_setup();

	if (qi_lb60_init_platform_devices())
		panic("Failed to initialize platform devices");

	return 0;
}
arch_initcall(qi_lb60_board_setup);
