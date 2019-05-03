/*
 * linux/arch/arm/mach-omap1/board-ams-delta.c
 *
 * Modified from board-generic.c
 *
 * Board specific inits for the Amstrad E3 (codename Delta) videophone
 *
 * Copyright (C) 2006 Jonathan McDowell <noodles@earth.li>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/serial_8250.h>
#include <linux/export.h>
#include <linux/omapfb.h>
#include <linux/io.h>
#include <linux/platform_data/gpio-omap.h>

#include <media/soc_camera.h>

#include <asm/serial.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <linux/platform_data/keypad-omap.h>
#include <mach/mux.h>

#include <mach/hardware.h>
#include "camera.h"
#include <mach/usb.h>

#include "ams-delta-fiq.h"
#include "board-ams-delta.h"
#include "iomap.h"
#include "common.h"

static const unsigned int ams_delta_keymap[] = {
	KEY(0, 0, KEY_F1),		/* Advert    */

	KEY(0, 3, KEY_COFFEE),		/* Games     */
	KEY(0, 2, KEY_QUESTION),	/* Directory */
	KEY(2, 3, KEY_CONNECT),		/* Internet  */
	KEY(1, 2, KEY_SHOP),		/* Services  */
	KEY(1, 1, KEY_PHONE),		/* VoiceMail */

	KEY(0, 1, KEY_DELETE),		/* Delete    */
	KEY(2, 2, KEY_PLAY),		/* Play      */
	KEY(1, 0, KEY_PAGEUP),		/* Up        */
	KEY(1, 3, KEY_PAGEDOWN),	/* Down      */
	KEY(2, 0, KEY_EMAIL),		/* ReadEmail */
	KEY(2, 1, KEY_STOP),		/* Stop      */

	/* Numeric keypad portion */
	KEY(0, 7, KEY_KP1),
	KEY(0, 6, KEY_KP2),
	KEY(0, 5, KEY_KP3),
	KEY(1, 7, KEY_KP4),
	KEY(1, 6, KEY_KP5),
	KEY(1, 5, KEY_KP6),
	KEY(2, 7, KEY_KP7),
	KEY(2, 6, KEY_KP8),
	KEY(2, 5, KEY_KP9),
	KEY(3, 6, KEY_KP0),
	KEY(3, 7, KEY_KPASTERISK),
	KEY(3, 5, KEY_KPDOT),		/* # key     */
	KEY(7, 2, KEY_NUMLOCK),		/* Mute      */
	KEY(7, 1, KEY_KPMINUS),		/* Recall    */
	KEY(6, 1, KEY_KPPLUS),		/* Redial    */
	KEY(7, 6, KEY_KPSLASH),		/* Handsfree */
	KEY(6, 0, KEY_ENTER),		/* Video     */

	KEY(7, 4, KEY_CAMERA),		/* Photo     */

	KEY(0, 4, KEY_F2),		/* Home      */
	KEY(1, 4, KEY_F3),		/* Office    */
	KEY(2, 4, KEY_F4),		/* Mobile    */
	KEY(7, 7, KEY_F5),		/* SMS       */
	KEY(7, 5, KEY_F6),		/* Email     */

	/* QWERTY portion of keypad */
	KEY(3, 4, KEY_Q),
	KEY(3, 3, KEY_W),
	KEY(3, 2, KEY_E),
	KEY(3, 1, KEY_R),
	KEY(3, 0, KEY_T),
	KEY(4, 7, KEY_Y),
	KEY(4, 6, KEY_U),
	KEY(4, 5, KEY_I),
	KEY(4, 4, KEY_O),
	KEY(4, 3, KEY_P),

	KEY(4, 2, KEY_A),
	KEY(4, 1, KEY_S),
	KEY(4, 0, KEY_D),
	KEY(5, 7, KEY_F),
	KEY(5, 6, KEY_G),
	KEY(5, 5, KEY_H),
	KEY(5, 4, KEY_J),
	KEY(5, 3, KEY_K),
	KEY(5, 2, KEY_L),

	KEY(5, 1, KEY_Z),
	KEY(5, 0, KEY_X),
	KEY(6, 7, KEY_C),
	KEY(6, 6, KEY_V),
	KEY(6, 5, KEY_B),
	KEY(6, 4, KEY_N),
	KEY(6, 3, KEY_M),
	KEY(6, 2, KEY_SPACE),

	KEY(7, 0, KEY_LEFTSHIFT),	/* Vol up    */
	KEY(7, 3, KEY_LEFTCTRL),	/* Vol down  */
};

#define LATCH1_PHYS	0x01000000
#define LATCH1_VIRT	0xEA000000
#define MODEM_PHYS	0x04000000
#define MODEM_VIRT	0xEB000000
#define LATCH2_PHYS	0x08000000
#define LATCH2_VIRT	0xEC000000

static struct map_desc ams_delta_io_desc[] __initdata = {
	/* AMS_DELTA_LATCH1 */
	{
		.virtual	= LATCH1_VIRT,
		.pfn		= __phys_to_pfn(LATCH1_PHYS),
		.length		= 0x01000000,
		.type		= MT_DEVICE
	},
	/* AMS_DELTA_LATCH2 */
	{
		.virtual	= LATCH2_VIRT,
		.pfn		= __phys_to_pfn(LATCH2_PHYS),
		.length		= 0x01000000,
		.type		= MT_DEVICE
	},
	/* AMS_DELTA_MODEM */
	{
		.virtual	= MODEM_VIRT,
		.pfn		= __phys_to_pfn(MODEM_PHYS),
		.length		= 0x01000000,
		.type		= MT_DEVICE
	}
};

static const struct omap_lcd_config ams_delta_lcd_config __initconst = {
	.ctrl_name	= "internal",
};

static struct omap_usb_config ams_delta_usb_config __initdata = {
	.register_host	= 1,
	.hmc_mode	= 16,
	.pins[0]	= 2,
};

#define LATCH1_NGPIO		8

static struct resource latch1_resources[] = {
	[0] = {
		.name	= "dat",
		.start	= LATCH1_PHYS,
		.end	= LATCH1_PHYS + (LATCH1_NGPIO - 1) / 8,
		.flags	= IORESOURCE_MEM,
	},
};

#define LATCH1_LABEL	"latch1"

static struct bgpio_pdata latch1_pdata = {
	.label	= LATCH1_LABEL,
	.base	= -1,
	.ngpio	= LATCH1_NGPIO,
};

static struct platform_device latch1_gpio_device = {
	.name		= "basic-mmio-gpio",
	.id		= 0,
	.resource	= latch1_resources,
	.num_resources	= ARRAY_SIZE(latch1_resources),
	.dev		= {
		.platform_data	= &latch1_pdata,
	},
};

#define LATCH1_PIN_LED_CAMERA		0
#define LATCH1_PIN_LED_ADVERT		1
#define LATCH1_PIN_LED_MAIL		2
#define LATCH1_PIN_LED_HANDSFREE	3
#define LATCH1_PIN_LED_VOICEMAIL	4
#define LATCH1_PIN_LED_VOICE		5
#define LATCH1_PIN_DOCKIT1		6
#define LATCH1_PIN_DOCKIT2		7

#define LATCH2_NGPIO			16

static struct resource latch2_resources[] = {
	[0] = {
		.name	= "dat",
		.start	= LATCH2_PHYS,
		.end	= LATCH2_PHYS + (LATCH2_NGPIO - 1) / 8,
		.flags	= IORESOURCE_MEM,
	},
};

#define LATCH2_LABEL	"latch2"

static struct bgpio_pdata latch2_pdata = {
	.label	= LATCH2_LABEL,
	.base	= -1,
	.ngpio	= LATCH2_NGPIO,
};

static struct platform_device latch2_gpio_device = {
	.name		= "basic-mmio-gpio",
	.id		= 1,
	.resource	= latch2_resources,
	.num_resources	= ARRAY_SIZE(latch2_resources),
	.dev		= {
		.platform_data	= &latch2_pdata,
	},
};

#define LATCH2_PIN_LCD_VBLEN		0
#define LATCH2_PIN_LCD_NDISP		1
#define LATCH2_PIN_NAND_NCE		2
#define LATCH2_PIN_NAND_NRE		3
#define LATCH2_PIN_NAND_NWP		4
#define LATCH2_PIN_NAND_NWE		5
#define LATCH2_PIN_NAND_ALE		6
#define LATCH2_PIN_NAND_CLE		7
#define LATCH2_PIN_KEYBRD_PWR		8
#define LATCH2_PIN_KEYBRD_DATAOUT	9
#define LATCH2_PIN_SCARD_RSTIN		10
#define LATCH2_PIN_SCARD_CMDVCC		11
#define LATCH2_PIN_MODEM_NRESET		12
#define LATCH2_PIN_MODEM_CODEC		13
#define LATCH2_PIN_AUDIO_MUTE		14
#define LATCH2_PIN_HOOKFLASH		15

static struct regulator_consumer_supply modem_nreset_consumers[] = {
	REGULATOR_SUPPLY("RESET#", "serial8250.1"),
	REGULATOR_SUPPLY("POR", "cx20442-codec"),
};

static struct regulator_init_data modem_nreset_data = {
	.constraints		= {
		.valid_ops_mask		= REGULATOR_CHANGE_STATUS,
		.boot_on		= 1,
	},
	.num_consumer_supplies	= ARRAY_SIZE(modem_nreset_consumers),
	.consumer_supplies	= modem_nreset_consumers,
};

static struct fixed_voltage_config modem_nreset_config = {
	.supply_name		= "modem_nreset",
	.microvolts		= 3300000,
	.startup_delay		= 25000,
	.enabled_at_boot	= 1,
	.init_data		= &modem_nreset_data,
};

static struct platform_device modem_nreset_device = {
	.name	= "reg-fixed-voltage",
	.id	= -1,
	.dev	= {
		.platform_data	= &modem_nreset_config,
	},
};

static struct gpiod_lookup_table ams_delta_nreset_gpiod_table = {
	.dev_id = "reg-fixed-voltage",
	.table = {
		GPIO_LOOKUP(LATCH2_LABEL, LATCH2_PIN_MODEM_NRESET,
			    NULL, GPIO_ACTIVE_HIGH),
		{ },
	},
};

struct modem_private_data {
	struct regulator *regulator;
};

static struct modem_private_data modem_priv;

static struct platform_device ams_delta_nand_device = {
	.name	= "ams-delta-nand",
	.id	= -1,
};

#define OMAP_GPIO_LABEL		"gpio-0-15"
#define OMAP_MPUIO_LABEL	"mpuio"

static struct gpiod_lookup_table ams_delta_nand_gpio_table = {
	.table = {
		GPIO_LOOKUP(OMAP_GPIO_LABEL, AMS_DELTA_GPIO_PIN_NAND_RB, "rdy",
			    0),
		GPIO_LOOKUP(LATCH2_LABEL, LATCH2_PIN_NAND_NCE, "nce", 0),
		GPIO_LOOKUP(LATCH2_LABEL, LATCH2_PIN_NAND_NRE, "nre", 0),
		GPIO_LOOKUP(LATCH2_LABEL, LATCH2_PIN_NAND_NWP, "nwp", 0),
		GPIO_LOOKUP(LATCH2_LABEL, LATCH2_PIN_NAND_NWE, "nwe", 0),
		GPIO_LOOKUP(LATCH2_LABEL, LATCH2_PIN_NAND_ALE, "ale", 0),
		GPIO_LOOKUP(LATCH2_LABEL, LATCH2_PIN_NAND_CLE, "cle", 0),
		GPIO_LOOKUP_IDX(OMAP_MPUIO_LABEL, 0, "data", 0, 0),
		GPIO_LOOKUP_IDX(OMAP_MPUIO_LABEL, 1, "data", 1, 0),
		GPIO_LOOKUP_IDX(OMAP_MPUIO_LABEL, 2, "data", 2, 0),
		GPIO_LOOKUP_IDX(OMAP_MPUIO_LABEL, 3, "data", 3, 0),
		GPIO_LOOKUP_IDX(OMAP_MPUIO_LABEL, 4, "data", 4, 0),
		GPIO_LOOKUP_IDX(OMAP_MPUIO_LABEL, 5, "data", 5, 0),
		GPIO_LOOKUP_IDX(OMAP_MPUIO_LABEL, 6, "data", 6, 0),
		GPIO_LOOKUP_IDX(OMAP_MPUIO_LABEL, 7, "data", 7, 0),
		{ },
	},
};

static struct resource ams_delta_kp_resources[] = {
	[0] = {
		.start	= INT_KEYBOARD,
		.end	= INT_KEYBOARD,
		.flags	= IORESOURCE_IRQ,
	},
};

static const struct matrix_keymap_data ams_delta_keymap_data = {
	.keymap		= ams_delta_keymap,
	.keymap_size	= ARRAY_SIZE(ams_delta_keymap),
};

static struct omap_kp_platform_data ams_delta_kp_data = {
	.rows		= 8,
	.cols		= 8,
	.keymap_data	= &ams_delta_keymap_data,
	.delay		= 9,
};

static struct platform_device ams_delta_kp_device = {
	.name		= "omap-keypad",
	.id		= -1,
	.dev		= {
		.platform_data = &ams_delta_kp_data,
	},
	.num_resources	= ARRAY_SIZE(ams_delta_kp_resources),
	.resource	= ams_delta_kp_resources,
};

static struct platform_device ams_delta_lcd_device = {
	.name	= "lcd_ams_delta",
	.id	= -1,
};

static struct gpiod_lookup_table ams_delta_lcd_gpio_table = {
	.table = {
		GPIO_LOOKUP(LATCH2_LABEL, LATCH2_PIN_LCD_VBLEN, "vblen", 0),
		GPIO_LOOKUP(LATCH2_LABEL, LATCH2_PIN_LCD_NDISP, "ndisp", 0),
		{ },
	},
};

static struct gpio_led gpio_leds[] __initdata = {
	[LATCH1_PIN_LED_CAMERA] = {
		.name		 = "camera",
		.default_state	 = LEDS_GPIO_DEFSTATE_OFF,
#ifdef CONFIG_LEDS_TRIGGERS
		.default_trigger = "ams_delta_camera",
#endif
	},
	[LATCH1_PIN_LED_ADVERT] = {
		.name		 = "advert",
		.default_state	 = LEDS_GPIO_DEFSTATE_OFF,
	},
	[LATCH1_PIN_LED_MAIL] = {
		.name		 = "email",
		.default_state	 = LEDS_GPIO_DEFSTATE_OFF,
	},
	[LATCH1_PIN_LED_HANDSFREE] = {
		.name		 = "handsfree",
		.default_state	 = LEDS_GPIO_DEFSTATE_OFF,
	},
	[LATCH1_PIN_LED_VOICEMAIL] = {
		.name		 = "voicemail",
		.default_state	 = LEDS_GPIO_DEFSTATE_OFF,
	},
	[LATCH1_PIN_LED_VOICE] = {
		.name		 = "voice",
		.default_state	 = LEDS_GPIO_DEFSTATE_OFF,
	},
};

static const struct gpio_led_platform_data leds_pdata __initconst = {
	.leds		= gpio_leds,
	.num_leds	= ARRAY_SIZE(gpio_leds),
};

static struct gpiod_lookup_table leds_gpio_table = {
	.table = {
		GPIO_LOOKUP_IDX(LATCH1_LABEL, LATCH1_PIN_LED_CAMERA, NULL,
				LATCH1_PIN_LED_CAMERA, 0),
		GPIO_LOOKUP_IDX(LATCH1_LABEL, LATCH1_PIN_LED_ADVERT, NULL,
				LATCH1_PIN_LED_ADVERT, 0),
		GPIO_LOOKUP_IDX(LATCH1_LABEL, LATCH1_PIN_LED_MAIL, NULL,
				LATCH1_PIN_LED_MAIL, 0),
		GPIO_LOOKUP_IDX(LATCH1_LABEL, LATCH1_PIN_LED_HANDSFREE, NULL,
				LATCH1_PIN_LED_HANDSFREE, 0),
		GPIO_LOOKUP_IDX(LATCH1_LABEL, LATCH1_PIN_LED_VOICEMAIL, NULL,
				LATCH1_PIN_LED_VOICEMAIL, 0),
		GPIO_LOOKUP_IDX(LATCH1_LABEL, LATCH1_PIN_LED_VOICE, NULL,
				LATCH1_PIN_LED_VOICE, 0),
		{ },
	},
};

static struct i2c_board_info ams_delta_camera_board_info[] = {
	{
		I2C_BOARD_INFO("ov6650", 0x60),
	},
};

#ifdef CONFIG_LEDS_TRIGGERS
DEFINE_LED_TRIGGER(ams_delta_camera_led_trigger);

static int ams_delta_camera_power(struct device *dev, int power)
{
	/*
	 * turn on camera LED
	 */
	if (power)
		led_trigger_event(ams_delta_camera_led_trigger, LED_FULL);
	else
		led_trigger_event(ams_delta_camera_led_trigger, LED_OFF);
	return 0;
}
#else
#define ams_delta_camera_power	NULL
#endif

static struct soc_camera_link ams_delta_iclink = {
	.bus_id         = 0,	/* OMAP1 SoC camera bus */
	.i2c_adapter_id = 1,
	.board_info     = &ams_delta_camera_board_info[0],
	.module_name    = "ov6650",
	.power		= ams_delta_camera_power,
};

static struct platform_device ams_delta_camera_device = {
	.name   = "soc-camera-pdrv",
	.id     = 0,
	.dev    = {
		.platform_data = &ams_delta_iclink,
	},
};

static struct omap1_cam_platform_data ams_delta_camera_platform_data = {
	.camexclk_khz	= 12000,	/* default 12MHz clock, no extra DPLL */
	.lclk_khz_max	= 1334,		/* results in 5fps CIF, 10fps QCIF */
};

static struct platform_device ams_delta_audio_device = {
	.name   = "ams-delta-audio",
	.id     = -1,
};

static struct gpiod_lookup_table ams_delta_audio_gpio_table = {
	.table = {
		GPIO_LOOKUP(OMAP_GPIO_LABEL, AMS_DELTA_GPIO_PIN_HOOK_SWITCH,
			    "hook_switch", 0),
		GPIO_LOOKUP(LATCH2_LABEL, LATCH2_PIN_MODEM_CODEC,
			    "modem_codec", 0),
		{ },
	},
};

static struct platform_device cx20442_codec_device = {
	.name   = "cx20442-codec",
	.id     = -1,
};

static struct resource ams_delta_serio_resources[] = {
	{
		.flags	= IORESOURCE_IRQ,
		/*
		 * Initialize IRQ resource with invalid IRQ number.
		 * It will be replaced with dynamically allocated GPIO IRQ
		 * obtained from GPIO chip as soon as the chip is available.
		 */
		.start	= -EINVAL,
		.end	= -EINVAL,
	},
};

static struct platform_device ams_delta_serio_device = {
	.name		= "ams-delta-serio",
	.id		= PLATFORM_DEVID_NONE,
	.dev		= {
		/*
		 * Initialize .platform_data explicitly with NULL to
		 * indicate it is going to be used.  It will be replaced
		 * with FIQ buffer address as soon as FIQ is initialized.
		 */
		.platform_data = NULL,
	},
	.num_resources	= ARRAY_SIZE(ams_delta_serio_resources),
	.resource	= ams_delta_serio_resources,
};

static struct regulator_consumer_supply keybrd_pwr_consumers[] = {
	/*
	 * Initialize supply .dev_name with NULL.  It will be replaced
	 * with serio dev_name() as soon as the serio device is registered.
	 */
	REGULATOR_SUPPLY("vcc", NULL),
};

static struct regulator_init_data keybrd_pwr_initdata = {
	.constraints		= {
		.valid_ops_mask		= REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(keybrd_pwr_consumers),
	.consumer_supplies	= keybrd_pwr_consumers,
};

static struct fixed_voltage_config keybrd_pwr_config = {
	.supply_name		= "keybrd_pwr",
	.microvolts		= 5000000,
	.init_data		= &keybrd_pwr_initdata,
};

static struct platform_device keybrd_pwr_device = {
	.name	= "reg-fixed-voltage",
	.id	= PLATFORM_DEVID_AUTO,
	.dev	= {
		.platform_data	= &keybrd_pwr_config,
	},
};

static struct gpiod_lookup_table keybrd_pwr_gpio_table = {
	.table = {
		GPIO_LOOKUP(LATCH2_LABEL, LATCH2_PIN_KEYBRD_PWR, NULL,
			    GPIO_ACTIVE_HIGH),
		{ },
	},
};

static struct platform_device *ams_delta_devices[] __initdata = {
	&latch1_gpio_device,
	&latch2_gpio_device,
	&ams_delta_kp_device,
	&ams_delta_camera_device,
	&ams_delta_audio_device,
	&ams_delta_serio_device,
	&ams_delta_nand_device,
	&ams_delta_lcd_device,
	&cx20442_codec_device,
};

static struct gpiod_lookup_table *ams_delta_gpio_tables[] __initdata = {
	&ams_delta_nreset_gpiod_table,
	&ams_delta_audio_gpio_table,
	&keybrd_pwr_gpio_table,
	&ams_delta_lcd_gpio_table,
	&ams_delta_nand_gpio_table,
};

/*
 * Some drivers may not use GPIO lookup tables but need to be provided
 * with GPIO numbers.  The same applies to GPIO based IRQ lines - some
 * drivers may even not use GPIO layer but expect just IRQ numbers.
 * We could either define GPIO lookup tables then use them on behalf
 * of those devices, or we can use GPIO driver level methods for
 * identification of GPIO and IRQ numbers. For the purpose of the latter,
 * defina a helper function which identifies GPIO chips by their labels.
 */
static int gpiochip_match_by_label(struct gpio_chip *chip, void *data)
{
	char *label = data;

	return !strcmp(label, chip->label);
}

static struct gpiod_hog ams_delta_gpio_hogs[] = {
	GPIO_HOG(LATCH2_LABEL, LATCH2_PIN_KEYBRD_DATAOUT, "keybrd_dataout",
		 GPIO_ACTIVE_HIGH, GPIOD_OUT_LOW),
	GPIO_HOG(LATCH2_LABEL, LATCH2_PIN_AUDIO_MUTE, "audio_mute",
		 GPIO_ACTIVE_HIGH, GPIOD_OUT_LOW),
	{},
};

static struct plat_serial8250_port ams_delta_modem_ports[];

/*
 * Obtain MODEM IRQ GPIO descriptor using its hardware pin
 * number and assign related IRQ number to the MODEM port.
 * Keep the GPIO descriptor open so nobody steps in.
 */
static void __init modem_assign_irq(struct gpio_chip *chip)
{
	struct gpio_desc *gpiod;

	gpiod = gpiochip_request_own_desc(chip, AMS_DELTA_GPIO_PIN_MODEM_IRQ,
					  "modem_irq", 0);
	if (IS_ERR(gpiod)) {
		pr_err("%s: modem IRQ GPIO request failed (%ld)\n", __func__,
		       PTR_ERR(gpiod));
	} else {
		gpiod_direction_input(gpiod);
		ams_delta_modem_ports[0].irq = gpiod_to_irq(gpiod);
	}
}

/*
 * The purpose of this function is to take care of proper initialization of
 * devices and data structures which depend on GPIO lines provided by OMAP GPIO
 * banks but their drivers don't use GPIO lookup tables or GPIO layer at all.
 * The function may be called as soon as OMAP GPIO devices are probed.
 * Since that happens at postcore_initcall, it can be called successfully
 * from init_machine or later.
 * Dependent devices may be registered from within this function or later.
 */
static void __init omap_gpio_deps_init(void)
{
	struct gpio_chip *chip;

	chip = gpiochip_find(OMAP_GPIO_LABEL, gpiochip_match_by_label);
	if (!chip) {
		pr_err("%s: OMAP GPIO chip not found\n", __func__);
		return;
	}

	/*
	 * Start with FIQ initialization as it may have to request
	 * and release successfully each OMAP GPIO pin in turn.
	 */
	ams_delta_init_fiq(chip, &ams_delta_serio_device);

	modem_assign_irq(chip);
}

/*
 * Initialize latch2 pins with values which are safe for dependent on-board
 * devices or useful for their successull initialization even before GPIO
 * driver takes control over the latch pins:
 * - LATCH2_PIN_LCD_VBLEN	= 0
 * - LATCH2_PIN_LCD_NDISP	= 0	Keep LCD device powered off before its
 *					driver takes control over it.
 * - LATCH2_PIN_NAND_NCE	= 0
 * - LATCH2_PIN_NAND_NWP	= 0	Keep NAND device down and write-
 *					protected before its driver takes
 *					control over it.
 * - LATCH2_PIN_KEYBRD_PWR	= 0	Keep keyboard powered off before serio
 *					driver takes control over it.
 * - LATCH2_PIN_KEYBRD_DATAOUT	= 0	Keep low to avoid corruption of first
 *					byte of data received from attached
 *					keyboard when serio device is probed;
 *					the pin is also hogged low by the latch2
 *					GPIO driver as soon as it is ready.
 * - LATCH2_PIN_MODEM_NRESET	= 1	Enable voice MODEM device, allowing for
 *					its successful probe even before a
 *					regulator it depends on, which in turn
 *					takes control over the pin, is set up.
 * - LATCH2_PIN_MODEM_CODEC	= 1	Attach voice MODEM CODEC data port
 *					to the MODEM so the CODEC is under
 *					control even if audio driver doesn't
 *					take it over.
 */
static void __init ams_delta_latch2_init(void)
{
	u16 latch2 = 1 << LATCH2_PIN_MODEM_NRESET | 1 << LATCH2_PIN_MODEM_CODEC;

	__raw_writew(latch2, LATCH2_VIRT);
}

static void __init ams_delta_init(void)
{
	struct platform_device *leds_pdev;

	/* mux pins for uarts */
	omap_cfg_reg(UART1_TX);
	omap_cfg_reg(UART1_RTS);

	/* parallel camera interface */
	omap_cfg_reg(H19_1610_CAM_EXCLK);
	omap_cfg_reg(J15_1610_CAM_LCLK);
	omap_cfg_reg(L18_1610_CAM_VS);
	omap_cfg_reg(L15_1610_CAM_HS);
	omap_cfg_reg(L19_1610_CAM_D0);
	omap_cfg_reg(K14_1610_CAM_D1);
	omap_cfg_reg(K15_1610_CAM_D2);
	omap_cfg_reg(K19_1610_CAM_D3);
	omap_cfg_reg(K18_1610_CAM_D4);
	omap_cfg_reg(J14_1610_CAM_D5);
	omap_cfg_reg(J19_1610_CAM_D6);
	omap_cfg_reg(J18_1610_CAM_D7);

	omap_gpio_deps_init();
	ams_delta_latch2_init();
	gpiod_add_hogs(ams_delta_gpio_hogs);

	omap_serial_init();
	omap_register_i2c_bus(1, 100, NULL, 0);

	omap1_usb_init(&ams_delta_usb_config);
	omap1_set_camera_info(&ams_delta_camera_platform_data);
#ifdef CONFIG_LEDS_TRIGGERS
	led_trigger_register_simple("ams_delta_camera",
			&ams_delta_camera_led_trigger);
#endif
	platform_add_devices(ams_delta_devices, ARRAY_SIZE(ams_delta_devices));

	/*
	 * As soon as regulator consumers have been registered, assign their
	 * dev_names to consumer supply entries of respective regulators.
	 */
	keybrd_pwr_consumers[0].dev_name =
			dev_name(&ams_delta_serio_device.dev);

	/*
	 * Once consumer supply entries are populated with dev_names,
	 * register regulator devices.  At this stage only the keyboard
	 * power regulator has its consumer supply table fully populated.
	 */
	platform_device_register(&keybrd_pwr_device);

	/*
	 * As soon as GPIO consumers have been registered, assign
	 * their dev_names to respective GPIO lookup tables.
	 */
	ams_delta_audio_gpio_table.dev_id =
			dev_name(&ams_delta_audio_device.dev);
	keybrd_pwr_gpio_table.dev_id = dev_name(&keybrd_pwr_device.dev);
	ams_delta_nand_gpio_table.dev_id = dev_name(&ams_delta_nand_device.dev);
	ams_delta_lcd_gpio_table.dev_id = dev_name(&ams_delta_lcd_device.dev);

	/*
	 * Once GPIO lookup tables are populated with dev_names, register them.
	 */
	gpiod_add_lookup_tables(ams_delta_gpio_tables,
				ARRAY_SIZE(ams_delta_gpio_tables));

	leds_pdev = gpio_led_register_device(PLATFORM_DEVID_NONE, &leds_pdata);
	if (!IS_ERR(leds_pdev)) {
		leds_gpio_table.dev_id = dev_name(&leds_pdev->dev);
		gpiod_add_lookup_table(&leds_gpio_table);
	}

	omap_writew(omap_readw(ARM_RSTCT1) | 0x0004, ARM_RSTCT1);

	omapfb_set_lcd_config(&ams_delta_lcd_config);
}

static void modem_pm(struct uart_port *port, unsigned int state, unsigned old)
{
	struct modem_private_data *priv = port->private_data;
	int ret;

	if (!priv)
		return;

	if (IS_ERR(priv->regulator))
		return;

	if (state == old)
		return;

	if (state == 0)
		ret = regulator_enable(priv->regulator);
	else if (old == 0)
		ret = regulator_disable(priv->regulator);
	else
		ret = 0;

	if (ret)
		dev_warn(port->dev,
			 "ams_delta modem_pm: failed to %sable regulator: %d\n",
			 state ? "dis" : "en", ret);
}

static struct plat_serial8250_port ams_delta_modem_ports[] = {
	{
		.membase	= IOMEM(MODEM_VIRT),
		.mapbase	= MODEM_PHYS,
		.irq		= IRQ_NOTCONNECTED, /* changed later */
		.flags		= UPF_BOOT_AUTOCONF,
		.irqflags	= IRQF_TRIGGER_RISING,
		.iotype		= UPIO_MEM,
		.regshift	= 1,
		.uartclk	= BASE_BAUD * 16,
		.pm		= modem_pm,
		.private_data	= &modem_priv,
	},
	{ },
};

static struct platform_device ams_delta_modem_device = {
	.name	= "serial8250",
	.id	= PLAT8250_DEV_PLATFORM1,
	.dev		= {
		.platform_data = ams_delta_modem_ports,
	},
};

static int __init modem_nreset_init(void)
{
	int err;

	err = platform_device_register(&modem_nreset_device);
	if (err)
		pr_err("Couldn't register the modem regulator device\n");

	return err;
}


/*
 * This function expects MODEM IRQ number already assigned to the port.
 * The MODEM device requires its RESET# pin kept high during probe.
 * That requirement can be fulfilled in several ways:
 * - with a descriptor of already functional modem_nreset regulator
 *   assigned to the MODEM private data,
 * - with the regulator not yet controlled by modem_pm function but
 *   already enabled by default on probe,
 * - before the modem_nreset regulator is probed, with the pin already
 *   set high explicitly.
 * The last one is already guaranteed by ams_delta_latch2_init() called
 * from machine_init.
 * In order to avoid taking over ttyS0 device slot, the MODEM device
 * should be registered after OMAP serial ports.  Since those ports
 * are registered at arch_initcall, this function can be called safely
 * at arch_initcall_sync earliest.
 */
static int __init ams_delta_modem_init(void)
{
	int err;

	if (!machine_is_ams_delta())
		return -ENODEV;

	omap_cfg_reg(M14_1510_GPIO2);

	/* Initialize the modem_nreset regulator consumer before use */
	modem_priv.regulator = ERR_PTR(-ENODEV);

	err = platform_device_register(&ams_delta_modem_device);

	return err;
}
arch_initcall_sync(ams_delta_modem_init);

static int __init late_init(void)
{
	int err;

	err = modem_nreset_init();
	if (err)
		return err;

	/*
	 * Once the modem device is registered, the modem_nreset
	 * regulator can be requested on behalf of that device.
	 */
	modem_priv.regulator = regulator_get(&ams_delta_modem_device.dev,
			"RESET#");
	if (IS_ERR(modem_priv.regulator)) {
		err = PTR_ERR(modem_priv.regulator);
		goto unregister;
	}
	return 0;

unregister:
	platform_device_unregister(&ams_delta_modem_device);
	return err;
}

static void __init ams_delta_init_late(void)
{
	omap1_init_late();
	late_init();
}

static void __init ams_delta_map_io(void)
{
	omap15xx_map_io();
	iotable_init(ams_delta_io_desc, ARRAY_SIZE(ams_delta_io_desc));
}

MACHINE_START(AMS_DELTA, "Amstrad E3 (Delta)")
	/* Maintainer: Jonathan McDowell <noodles@earth.li> */
	.atag_offset	= 0x100,
	.map_io		= ams_delta_map_io,
	.init_early	= omap1_init_early,
	.init_irq	= omap1_init_irq,
	.handle_irq	= omap1_handle_irq,
	.init_machine	= ams_delta_init,
	.init_late	= ams_delta_init_late,
	.init_time	= omap1_timer_init,
	.restart	= omap1_restart,
MACHINE_END
