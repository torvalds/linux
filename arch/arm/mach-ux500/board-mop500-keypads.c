/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 *
 * Keypad layouts for various boards
 */

#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/mfd/stmpe.h>
#include <linux/mfd/tc3589x.h>
#include <linux/input/matrix_keypad.h>

#include <plat/pincfg.h>
#include <plat/ske.h>

#include <mach/devices.h>
#include <mach/hardware.h>

#include "devices-db8500.h"
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
 * Nomadik SKE keypad
 */
#define ROW_PIN_I0      164
#define ROW_PIN_I1      163
#define ROW_PIN_I2      162
#define ROW_PIN_I3      161
#define ROW_PIN_I4      156
#define ROW_PIN_I5      155
#define ROW_PIN_I6      154
#define ROW_PIN_I7      153
#define COL_PIN_O0      168
#define COL_PIN_O1      167
#define COL_PIN_O2      166
#define COL_PIN_O3      165
#define COL_PIN_O4      160
#define COL_PIN_O5      159
#define COL_PIN_O6      158
#define COL_PIN_O7      157

#define SKE_KPD_MAX_ROWS	8
#define SKE_KPD_MAX_COLS	8

static int ske_kp_rows[] = {
	ROW_PIN_I0, ROW_PIN_I1, ROW_PIN_I2, ROW_PIN_I3,
	ROW_PIN_I4, ROW_PIN_I5, ROW_PIN_I6, ROW_PIN_I7,
};

/*
 * ske_set_gpio_row: request and set gpio rows
 */
static int ske_set_gpio_row(int gpio)
{
	int ret;

	ret = gpio_request(gpio, "ske-kp");
	if (ret < 0) {
		pr_err("ske_set_gpio_row: gpio request failed\n");
		return ret;
	}

	ret = gpio_direction_output(gpio, 1);
	if (ret < 0) {
		pr_err("ske_set_gpio_row: gpio direction failed\n");
		gpio_free(gpio);
	}

	return ret;
}

/*
 * ske_kp_init - enable the gpio configuration
 */
static int ske_kp_init(void)
{
	int ret, i;

	for (i = 0; i < SKE_KPD_MAX_ROWS; i++) {
		ret = ske_set_gpio_row(ske_kp_rows[i]);
		if (ret < 0) {
			pr_err("ske_kp_init: failed init\n");
			return ret;
		}
	}

	return 0;
}

static struct ske_keypad_platform_data ske_keypad_board = {
	.init		= ske_kp_init,
	.keymap_data    = &mop500_keymap_data,
	.no_autorepeat  = true,
	.krow		= SKE_KPD_MAX_ROWS,     /* 8x8 matrix */
	.kcol		= SKE_KPD_MAX_COLS,
	.debounce_ms    = 40,			/* in millisecs */
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
	.id             = 1,
	.blocks         = STMPE_BLOCK_KEYPAD,
	.irq_trigger    = IRQF_TRIGGER_FALLING,
	.irq_base       = MOP500_STMPE1601_IRQ(0),
	.keypad         = &stmpe1601_keypad_data,
	.autosleep      = true,
	.autosleep_timeout = 1024,
};

static struct i2c_board_info mop500_i2c0_devices_stuib[] = {
	{
		I2C_BOARD_INFO("stmpe1601", 0x40),
		.irq = NOMADIK_GPIO_TO_IRQ(218),
		.platform_data = &stmpe1601_data,
		.flags = I2C_CLIENT_WAKE,
	},
};

/*
 * TC35893
 */

static const unsigned int uib_keymap[] = {
	KEY(3, 1, KEY_END),
	KEY(4, 1, KEY_POWER),
	KEY(6, 4, KEY_VOLUMEDOWN),
	KEY(4, 2, KEY_EMAIL),
	KEY(3, 3, KEY_RIGHT),
	KEY(2, 5, KEY_BACKSPACE),

	KEY(6, 7, KEY_MENU),
	KEY(5, 0, KEY_ENTER),
	KEY(4, 3, KEY_0),
	KEY(3, 4, KEY_DOT),
	KEY(5, 2, KEY_UP),
	KEY(3, 5, KEY_DOWN),

	KEY(4, 5, KEY_SEND),
	KEY(0, 5, KEY_BACK),
	KEY(6, 2, KEY_VOLUMEUP),
	KEY(1, 3, KEY_SPACE),
	KEY(7, 6, KEY_LEFT),
	KEY(5, 5, KEY_SEARCH),
};

static struct matrix_keymap_data uib_keymap_data = {
	.keymap         = uib_keymap,
	.keymap_size    = ARRAY_SIZE(uib_keymap),
};

static struct tc3589x_keypad_platform_data tc35893_data = {
	.krow = TC_KPD_ROWS,
	.kcol = TC_KPD_COLUMNS,
	.debounce_period = TC_KPD_DEBOUNCE_PERIOD,
	.settle_time = TC_KPD_SETTLE_TIME,
	.irqtype = IRQF_TRIGGER_FALLING,
	.enable_wakeup = true,
	.keymap_data    = &uib_keymap_data,
	.no_autorepeat  = true,
};

static struct tc3589x_platform_data tc3589x_keypad_data = {
	.block = TC3589x_BLOCK_KEYPAD,
	.keypad = &tc35893_data,
	.irq_base = MOP500_EGPIO_IRQ_BASE,
};

static struct i2c_board_info mop500_i2c0_devices_uib[] = {
	{
		I2C_BOARD_INFO("tc3589x", 0x44),
		.platform_data = &tc3589x_keypad_data,
		.irq = NOMADIK_GPIO_TO_IRQ(218),
		.flags = I2C_CLIENT_WAKE,
	},
};

void mop500_keypad_init(void)
{
	db8500_add_ske_keypad(&ske_keypad_board);

	i2c_register_board_info(0, mop500_i2c0_devices_stuib,
			ARRAY_SIZE(mop500_i2c0_devices_stuib));

	i2c_register_board_info(0, mop500_i2c0_devices_uib,
			ARRAY_SIZE(mop500_i2c0_devices_uib));

}
