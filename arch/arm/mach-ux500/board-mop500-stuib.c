/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License (GPL), version 2
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mfd/stmpe.h>
#include <linux/input/bu21013.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/input/matrix_keypad.h>
#include <asm/mach-types.h>

#include "board-mop500.h"

/* STMPE/SKE keypad use this key layout */
static const unsigned int mop500_keymap[] = {
	KEY(2, 5, KEY_END),
	KEY(4, 1, KEY_POWER),
	KEY(3, 5, KEY_VOLUMEDOWN),
	KEY(1, 3, KEY_3),
	KEY(5, 2, KEY_RIGHT),
	KEY(5, 0, KEY_9),

	KEY(0, 5, KEY_MENU),
	KEY(7, 6, KEY_ENTER),
	KEY(4, 5, KEY_0),
	KEY(6, 7, KEY_2),
	KEY(3, 4, KEY_UP),
	KEY(3, 3, KEY_DOWN),

	KEY(6, 4, KEY_SEND),
	KEY(6, 2, KEY_BACK),
	KEY(4, 2, KEY_VOLUMEUP),
	KEY(5, 5, KEY_1),
	KEY(4, 3, KEY_LEFT),
	KEY(3, 2, KEY_7),
};

static const struct matrix_keymap_data mop500_keymap_data = {
	.keymap		= mop500_keymap,
	.keymap_size    = ARRAY_SIZE(mop500_keymap),
};
/*
 * STMPE1601
 */
static struct stmpe_keypad_platform_data stmpe1601_keypad_data = {
	.debounce_ms    = 64,
	.scan_count     = 8,
	.no_autorepeat  = true,
	.keymap_data    = &mop500_keymap_data,
};

static struct stmpe_platform_data stmpe1601_data = {
	.id		= 1,
	.blocks		= STMPE_BLOCK_KEYPAD,
	.irq_trigger    = IRQF_TRIGGER_FALLING,
	.irq_base       = MOP500_STMPE1601_IRQ(0),
	.keypad		= &stmpe1601_keypad_data,
	.autosleep      = true,
	.autosleep_timeout = 1024,
};

static struct i2c_board_info __initdata mop500_i2c0_devices_stuib[] = {
	{
		I2C_BOARD_INFO("stmpe1601", 0x40),
		.irq = NOMADIK_GPIO_TO_IRQ(218),
		.platform_data = &stmpe1601_data,
		.flags = I2C_CLIENT_WAKE,
	},
};

/*
 * BU21013 ROHM touchscreen interface on the STUIBs
 */

/* tracks number of bu21013 devices being enabled */
static int bu21013_devices;

#define TOUCH_GPIO_PIN  84

#define TOUCH_XMAX	384
#define TOUCH_YMAX	704

#define PRCMU_CLOCK_OCR		0x1CC
#define TSC_EXT_CLOCK_9_6MHZ	0x840000

/**
 * bu21013_gpio_board_init : configures the touch panel.
 * @reset_pin: reset pin number
 * This function can be used to configures
 * the voltage and reset the touch panel controller.
 */
static int bu21013_gpio_board_init(int reset_pin)
{
	int retval = 0;

	bu21013_devices++;
	if (bu21013_devices == 1) {
		retval = gpio_request(reset_pin, "touchp_reset");
		if (retval) {
			printk(KERN_ERR "Unable to request gpio reset_pin");
			return retval;
		}
		retval = gpio_direction_output(reset_pin, 1);
		if (retval < 0) {
			printk(KERN_ERR "%s: gpio direction failed\n",
					__func__);
			return retval;
		}
	}

	return retval;
}

/**
 * bu21013_gpio_board_exit : deconfigures the touch panel controller
 * @reset_pin: reset pin number
 * This function can be used to deconfigures the chip selection
 * for touch panel controller.
 */
static int bu21013_gpio_board_exit(int reset_pin)
{
	int retval = 0;

	if (bu21013_devices == 1) {
		retval = gpio_direction_output(reset_pin, 0);
		if (retval < 0) {
			printk(KERN_ERR "%s: gpio direction failed\n",
					__func__);
			return retval;
		}
		gpio_set_value(reset_pin, 0);
	}
	bu21013_devices--;

	return retval;
}

/**
 * bu21013_read_pin_val : get the interrupt pin value
 * This function can be used to get the interrupt pin value for touch panel
 * controller.
 */
static int bu21013_read_pin_val(void)
{
	return gpio_get_value(TOUCH_GPIO_PIN);
}

static struct bu21013_platform_device tsc_plat_device = {
	.cs_en = bu21013_gpio_board_init,
	.cs_dis = bu21013_gpio_board_exit,
	.irq_read_val = bu21013_read_pin_val,
	.irq = NOMADIK_GPIO_TO_IRQ(TOUCH_GPIO_PIN),
	.touch_x_max = TOUCH_XMAX,
	.touch_y_max = TOUCH_YMAX,
	.ext_clk = false,
	.x_flip = false,
	.y_flip = true,
};

static struct i2c_board_info __initdata u8500_i2c3_devices_stuib[] = {
	{
		I2C_BOARD_INFO("bu21013_tp", 0x5C),
		.platform_data = &tsc_plat_device,
	},
	{
		I2C_BOARD_INFO("bu21013_tp", 0x5D),
		.platform_data = &tsc_plat_device,
	},

};

void __init mop500_stuib_init(void)
{
	if (machine_is_hrefv60())
		tsc_plat_device.cs_pin = HREFV60_TOUCH_RST_GPIO;
	else
		tsc_plat_device.cs_pin = GPIO_BU21013_CS;

	mop500_uib_i2c_add(0, mop500_i2c0_devices_stuib,
			ARRAY_SIZE(mop500_i2c0_devices_stuib));

	mop500_uib_i2c_add(3, u8500_i2c3_devices_stuib,
			ARRAY_SIZE(u8500_i2c3_devices_stuib));
}
