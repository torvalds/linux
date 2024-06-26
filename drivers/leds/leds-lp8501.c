// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI LP8501 9 channel LED Driver
 *
 * Copyright (C) 2013 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 */

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_data/leds-lp55xx.h>
#include <linux/slab.h>

#include "leds-lp55xx-common.h"

#define LP8501_PROGRAM_LENGTH		32
#define LP8501_PAGES_PER_ENGINE		1
#define LP8501_MAX_LEDS			9

/* Registers */
#define LP8501_REG_ENABLE		0x00
#define LP8501_ENABLE			BIT(6)
#define LP8501_EXEC_M			0x3F
#define LP8501_EXEC_ENG1_M		0x30
#define LP8501_EXEC_ENG2_M		0x0C
#define LP8501_EXEC_ENG3_M		0x03
#define LP8501_RUN_ENG1			0x20
#define LP8501_RUN_ENG2			0x08
#define LP8501_RUN_ENG3			0x02

#define LP8501_REG_OP_MODE		0x01
#define LP8501_MODE_ENG1_M		0x30
#define LP8501_MODE_ENG2_M		0x0C
#define LP8501_MODE_ENG3_M		0x03
#define LP8501_LOAD_ENG1		0x10
#define LP8501_LOAD_ENG2		0x04
#define LP8501_LOAD_ENG3		0x01

#define LP8501_REG_PWR_CONFIG		0x05
#define LP8501_PWR_CONFIG_M		0x03

#define LP8501_REG_LED_PWM_BASE		0x16

#define LP8501_REG_LED_CURRENT_BASE	0x26

#define LP8501_REG_CONFIG		0x36
#define LP8501_PWM_PSAVE		BIT(7)
#define LP8501_AUTO_INC			BIT(6)
#define LP8501_PWR_SAVE			BIT(5)
#define LP8501_CP_MODE_MASK		0x18
#define LP8501_CP_MODE_SHIFT		3
#define LP8501_INT_CLK			BIT(0)
#define LP8501_DEFAULT_CFG (LP8501_PWM_PSAVE | LP8501_AUTO_INC | LP8501_PWR_SAVE)

#define LP8501_REG_STATUS		0x3A
#define LP8501_ENGINE_BUSY		BIT(4)

#define LP8501_REG_RESET		0x3D
#define LP8501_RESET			0xFF

#define LP8501_REG_PROG_PAGE_SEL	0x4F
#define LP8501_PAGE_ENG1		0
#define LP8501_PAGE_ENG2		1
#define LP8501_PAGE_ENG3		2

#define LP8501_REG_PROG_MEM		0x50

#define LP8501_ENG1_IS_LOADING(mode)	\
	((mode & LP8501_MODE_ENG1_M) == LP8501_LOAD_ENG1)
#define LP8501_ENG2_IS_LOADING(mode)	\
	((mode & LP8501_MODE_ENG2_M) == LP8501_LOAD_ENG2)
#define LP8501_ENG3_IS_LOADING(mode)	\
	((mode & LP8501_MODE_ENG3_M) == LP8501_LOAD_ENG3)

static inline void lp8501_wait_opmode_done(void)
{
	usleep_range(1000, 2000);
}

static void lp8501_set_led_current(struct lp55xx_led *led, u8 led_current)
{
	led->led_current = led_current;
	lp55xx_write(led->chip, LP8501_REG_LED_CURRENT_BASE + led->chan_nr,
		led_current);
}

static int lp8501_post_init_device(struct lp55xx_chip *chip)
{
	int ret;
	u8 val = LP8501_DEFAULT_CFG;

	ret = lp55xx_write(chip, LP8501_REG_ENABLE, LP8501_ENABLE);
	if (ret)
		return ret;

	/* Chip startup time is 500 us, 1 - 2 ms gives some margin */
	usleep_range(1000, 2000);

	if (chip->pdata->clock_mode != LP55XX_CLOCK_EXT)
		val |= LP8501_INT_CLK;

	val |= (chip->pdata->charge_pump_mode << LP8501_CP_MODE_SHIFT) & LP8501_CP_MODE_MASK;

	ret = lp55xx_write(chip, LP8501_REG_CONFIG, val);
	if (ret)
		return ret;

	/* Power selection for each output */
	return lp55xx_update_bits(chip, LP8501_REG_PWR_CONFIG,
				LP8501_PWR_CONFIG_M, chip->pdata->pwr_sel);
}

static void lp8501_turn_off_channels(struct lp55xx_chip *chip)
{
	int i;

	for (i = 0; i < LP8501_MAX_LEDS; i++)
		lp55xx_write(chip, LP8501_REG_LED_PWM_BASE + i, 0);
}

static void lp8501_run_engine(struct lp55xx_chip *chip, bool start)
{
	/* stop engine */
	if (!start) {
		lp55xx_stop_all_engine(chip);
		lp8501_turn_off_channels(chip);
		return;
	}

	lp55xx_run_engine_common(chip);
}

static void lp8501_firmware_loaded(struct lp55xx_chip *chip)
{
	const struct firmware *fw = chip->fw;

	if (fw->size > LP8501_PROGRAM_LENGTH) {
		dev_err(&chip->cl->dev, "firmware data size overflow: %zu\n",
			fw->size);
		return;
	}

	/*
	 * Program memory sequence
	 *  1) set engine mode to "LOAD"
	 *  2) write firmware data into program memory
	 */

	lp55xx_load_engine(chip);
	lp55xx_update_program_memory(chip, fw->data, fw->size);
}

static int lp8501_led_brightness(struct lp55xx_led *led)
{
	struct lp55xx_chip *chip = led->chip;
	int ret;

	mutex_lock(&chip->lock);
	ret = lp55xx_write(chip, LP8501_REG_LED_PWM_BASE + led->chan_nr,
		     led->brightness);
	mutex_unlock(&chip->lock);

	return ret;
}

/* Chip specific configurations */
static struct lp55xx_device_config lp8501_cfg = {
	.reg_op_mode = {
		.addr = LP8501_REG_OP_MODE,
	},
	.reg_exec = {
		.addr = LP8501_REG_ENABLE,
	},
	.engine_busy = {
		.addr = LP8501_REG_STATUS,
		.mask = LP8501_ENGINE_BUSY,
	},
	.reset = {
		.addr = LP8501_REG_RESET,
		.val  = LP8501_RESET,
	},
	.enable = {
		.addr = LP8501_REG_ENABLE,
		.val  = LP8501_ENABLE,
	},
	.prog_mem_base = {
		.addr = LP8501_REG_PROG_MEM,
	},
	.pages_per_engine   = LP8501_PAGES_PER_ENGINE,
	.max_channel  = LP8501_MAX_LEDS,
	.post_init_device   = lp8501_post_init_device,
	.brightness_fn      = lp8501_led_brightness,
	.set_led_current    = lp8501_set_led_current,
	.firmware_cb        = lp8501_firmware_loaded,
	.run_engine         = lp8501_run_engine,
};

static const struct i2c_device_id lp8501_id[] = {
	{ "lp8501",  .driver_data = (kernel_ulong_t)&lp8501_cfg, },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lp8501_id);

static const struct of_device_id of_lp8501_leds_match[] = {
	{ .compatible = "ti,lp8501", .data = &lp8501_cfg, },
	{},
};

MODULE_DEVICE_TABLE(of, of_lp8501_leds_match);

static struct i2c_driver lp8501_driver = {
	.driver = {
		.name	= "lp8501",
		.of_match_table = of_lp8501_leds_match,
	},
	.probe		= lp55xx_probe,
	.remove		= lp55xx_remove,
	.id_table	= lp8501_id,
};

module_i2c_driver(lp8501_driver);

MODULE_DESCRIPTION("Texas Instruments LP8501 LED driver");
MODULE_AUTHOR("Milo Kim");
MODULE_LICENSE("GPL");
