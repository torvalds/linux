/*
 * TI LP8501 9 channel LED Driver
 *
 * Copyright (C) 2013 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
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
#define LP8501_CP_AUTO			0x18
#define LP8501_INT_CLK			BIT(0)
#define LP8501_DEFAULT_CFG	\
	(LP8501_PWM_PSAVE | LP8501_AUTO_INC | LP8501_PWR_SAVE | LP8501_CP_AUTO)

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

	ret = lp55xx_write(chip, LP8501_REG_CONFIG, val);
	if (ret)
		return ret;

	/* Power selection for each output */
	return lp55xx_update_bits(chip, LP8501_REG_PWR_CONFIG,
				LP8501_PWR_CONFIG_M, chip->pdata->pwr_sel);
}

static void lp8501_load_engine(struct lp55xx_chip *chip)
{
	enum lp55xx_engine_index idx = chip->engine_idx;
	u8 mask[] = {
		[LP55XX_ENGINE_1] = LP8501_MODE_ENG1_M,
		[LP55XX_ENGINE_2] = LP8501_MODE_ENG2_M,
		[LP55XX_ENGINE_3] = LP8501_MODE_ENG3_M,
	};

	u8 val[] = {
		[LP55XX_ENGINE_1] = LP8501_LOAD_ENG1,
		[LP55XX_ENGINE_2] = LP8501_LOAD_ENG2,
		[LP55XX_ENGINE_3] = LP8501_LOAD_ENG3,
	};

	u8 page_sel[] = {
		[LP55XX_ENGINE_1] = LP8501_PAGE_ENG1,
		[LP55XX_ENGINE_2] = LP8501_PAGE_ENG2,
		[LP55XX_ENGINE_3] = LP8501_PAGE_ENG3,
	};

	lp55xx_update_bits(chip, LP8501_REG_OP_MODE, mask[idx], val[idx]);

	lp8501_wait_opmode_done();

	lp55xx_write(chip, LP8501_REG_PROG_PAGE_SEL, page_sel[idx]);
}

static void lp8501_stop_engine(struct lp55xx_chip *chip)
{
	lp55xx_write(chip, LP8501_REG_OP_MODE, 0);
	lp8501_wait_opmode_done();
}

static void lp8501_turn_off_channels(struct lp55xx_chip *chip)
{
	int i;

	for (i = 0; i < LP8501_MAX_LEDS; i++)
		lp55xx_write(chip, LP8501_REG_LED_PWM_BASE + i, 0);
}

static void lp8501_run_engine(struct lp55xx_chip *chip, bool start)
{
	int ret;
	u8 mode;
	u8 exec;

	/* stop engine */
	if (!start) {
		lp8501_stop_engine(chip);
		lp8501_turn_off_channels(chip);
		return;
	}

	/*
	 * To run the engine,
	 * operation mode and enable register should updated at the same time
	 */

	ret = lp55xx_read(chip, LP8501_REG_OP_MODE, &mode);
	if (ret)
		return;

	ret = lp55xx_read(chip, LP8501_REG_ENABLE, &exec);
	if (ret)
		return;

	/* change operation mode to RUN only when each engine is loading */
	if (LP8501_ENG1_IS_LOADING(mode)) {
		mode = (mode & ~LP8501_MODE_ENG1_M) | LP8501_RUN_ENG1;
		exec = (exec & ~LP8501_EXEC_ENG1_M) | LP8501_RUN_ENG1;
	}

	if (LP8501_ENG2_IS_LOADING(mode)) {
		mode = (mode & ~LP8501_MODE_ENG2_M) | LP8501_RUN_ENG2;
		exec = (exec & ~LP8501_EXEC_ENG2_M) | LP8501_RUN_ENG2;
	}

	if (LP8501_ENG3_IS_LOADING(mode)) {
		mode = (mode & ~LP8501_MODE_ENG3_M) | LP8501_RUN_ENG3;
		exec = (exec & ~LP8501_EXEC_ENG3_M) | LP8501_RUN_ENG3;
	}

	lp55xx_write(chip, LP8501_REG_OP_MODE, mode);
	lp8501_wait_opmode_done();

	lp55xx_update_bits(chip, LP8501_REG_ENABLE, LP8501_EXEC_M, exec);
}

static int lp8501_update_program_memory(struct lp55xx_chip *chip,
					const u8 *data, size_t size)
{
	u8 pattern[LP8501_PROGRAM_LENGTH] = {0};
	unsigned cmd;
	char c[3];
	int update_size;
	int nrchars;
	int offset = 0;
	int ret;
	int i;

	/* clear program memory before updating */
	for (i = 0; i < LP8501_PROGRAM_LENGTH; i++)
		lp55xx_write(chip, LP8501_REG_PROG_MEM + i, 0);

	i = 0;
	while ((offset < size - 1) && (i < LP8501_PROGRAM_LENGTH)) {
		/* separate sscanfs because length is working only for %s */
		ret = sscanf(data + offset, "%2s%n ", c, &nrchars);
		if (ret != 1)
			goto err;

		ret = sscanf(c, "%2x", &cmd);
		if (ret != 1)
			goto err;

		pattern[i] = (u8)cmd;
		offset += nrchars;
		i++;
	}

	/* Each instruction is 16bit long. Check that length is even */
	if (i % 2)
		goto err;

	update_size = i;
	for (i = 0; i < update_size; i++)
		lp55xx_write(chip, LP8501_REG_PROG_MEM + i, pattern[i]);

	return 0;

err:
	dev_err(&chip->cl->dev, "wrong pattern format\n");
	return -EINVAL;
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
	 * Program momery sequence
	 *  1) set engine mode to "LOAD"
	 *  2) write firmware data into program memory
	 */

	lp8501_load_engine(chip);
	lp8501_update_program_memory(chip, fw->data, fw->size);
}

static void lp8501_led_brightness_work(struct work_struct *work)
{
	struct lp55xx_led *led = container_of(work, struct lp55xx_led,
					      brightness_work);
	struct lp55xx_chip *chip = led->chip;

	mutex_lock(&chip->lock);
	lp55xx_write(chip, LP8501_REG_LED_PWM_BASE + led->chan_nr,
		     led->brightness);
	mutex_unlock(&chip->lock);
}

/* Chip specific configurations */
static struct lp55xx_device_config lp8501_cfg = {
	.reset = {
		.addr = LP8501_REG_RESET,
		.val  = LP8501_RESET,
	},
	.enable = {
		.addr = LP8501_REG_ENABLE,
		.val  = LP8501_ENABLE,
	},
	.max_channel  = LP8501_MAX_LEDS,
	.post_init_device   = lp8501_post_init_device,
	.brightness_work_fn = lp8501_led_brightness_work,
	.set_led_current    = lp8501_set_led_current,
	.firmware_cb        = lp8501_firmware_loaded,
	.run_engine         = lp8501_run_engine,
};

static int lp8501_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret;
	struct lp55xx_chip *chip;
	struct lp55xx_led *led;
	struct lp55xx_platform_data *pdata;
	struct device_node *np = client->dev.of_node;

	if (!dev_get_platdata(&client->dev)) {
		if (np) {
			ret = lp55xx_of_populate_pdata(&client->dev, np);
			if (ret < 0)
				return ret;
		} else {
			dev_err(&client->dev, "no platform data\n");
			return -EINVAL;
		}
	}
	pdata = dev_get_platdata(&client->dev);

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	led = devm_kzalloc(&client->dev,
			sizeof(*led) * pdata->num_channels, GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	chip->cl = client;
	chip->pdata = pdata;
	chip->cfg = &lp8501_cfg;

	mutex_init(&chip->lock);

	i2c_set_clientdata(client, led);

	ret = lp55xx_init_device(chip);
	if (ret)
		goto err_init;

	dev_info(&client->dev, "%s Programmable led chip found\n", id->name);

	ret = lp55xx_register_leds(led, chip);
	if (ret)
		goto err_register_leds;

	ret = lp55xx_register_sysfs(chip);
	if (ret) {
		dev_err(&client->dev, "registering sysfs failed\n");
		goto err_register_sysfs;
	}

	return 0;

err_register_sysfs:
	lp55xx_unregister_leds(led, chip);
err_register_leds:
	lp55xx_deinit_device(chip);
err_init:
	return ret;
}

static int lp8501_remove(struct i2c_client *client)
{
	struct lp55xx_led *led = i2c_get_clientdata(client);
	struct lp55xx_chip *chip = led->chip;

	lp8501_stop_engine(chip);
	lp55xx_unregister_sysfs(chip);
	lp55xx_unregister_leds(led, chip);
	lp55xx_deinit_device(chip);

	return 0;
}

static const struct i2c_device_id lp8501_id[] = {
	{ "lp8501",  0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lp8501_id);

#ifdef CONFIG_OF
static const struct of_device_id of_lp8501_leds_match[] = {
	{ .compatible = "ti,lp8501", },
	{},
};

MODULE_DEVICE_TABLE(of, of_lp8501_leds_match);
#endif

static struct i2c_driver lp8501_driver = {
	.driver = {
		.name	= "lp8501",
		.of_match_table = of_match_ptr(of_lp8501_leds_match),
	},
	.probe		= lp8501_probe,
	.remove		= lp8501_remove,
	.id_table	= lp8501_id,
};

module_i2c_driver(lp8501_driver);

MODULE_DESCRIPTION("Texas Instruments LP8501 LED drvier");
MODULE_AUTHOR("Milo Kim");
MODULE_LICENSE("GPL");
