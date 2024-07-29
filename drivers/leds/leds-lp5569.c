// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Christian Marangi <ansuelsmth@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/iopoll.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_data/leds-lp55xx.h>
#include <linux/slab.h>
#include <dt-bindings/leds/leds-lp55xx.h>

#include "leds-lp55xx-common.h"

#define LP5569_MAX_LEDS			9

/* Memory is used like this:
 * 0x00 engine 1 program (4 pages)
 * 0x40 engine 2 program (4 pages)
 * 0x80 engine 3 program (4 pages)
 * 0xc0 engine 1 muxing info (1 page)
 * 0xd0 engine 2 muxing info (1 page)
 * 0xe0 engine 3 muxing info (1 page)
 */
#define LP5569_PAGES_PER_ENGINE		4

#define LP5569_REG_ENABLE		0x00
#define   LP5569_ENABLE			BIT(6)

#define LP5569_REG_EXEC_CTRL		0x01
#define   LP5569_MODE_ENG_SHIFT		2

#define LP5569_REG_OP_MODE		0x02
#define   LP5569_EXEC_ENG_SHIFT		2

#define LP5569_REG_ENABLE_LEDS_MSB	0x04
#define LP5569_REG_ENABLE_LEDS_LSB	0x05
#define LP5569_REG_LED_CTRL_BASE	0x07
#define   LP5569_FADER_MAPPING_MASK	GENMASK(7, 5)
#define LP5569_REG_LED_PWM_BASE		0x16
#define LP5569_REG_LED_CURRENT_BASE	0x22
#define LP5569_REG_MISC			0x2F
#define   LP5569_AUTO_INC		BIT(6)
#define   LP5569_PWR_SAVE		BIT(5)
#define   LP5569_CP_MODE_MASK		GENMASK(4, 3)
#define   LP5569_PWM_PWR_SAVE		BIT(2)
#define   LP5569_INTERNAL_CLK		BIT(0)
#define LP5569_REG_MISC2		0x33
#define   LP5569_LED_SHORT_TEST		BIT(4)
#define   LP5569_LED_OPEN_TEST		BIT(3)
#define LP5569_REG_STATUS		0x3C
#define   LP5569_MASK_BUSY		BIT(7)
#define   LP5569_STARTUP_BUSY		BIT(6)
#define   LP5569_ENGINE_BUSY		BIT(5)
#define   LP5569_ENGINE1_INT		BIT(2)
#define   LP5569_ENGINE2_INT		BIT(1)
#define   LP5569_ENGINE3_INT		BIT(0)
#define   LP5569_ENG_STATUS_MASK	(LP5569_ENGINE1_INT | LP5569_ENGINE2_INT | \
					 LP5569_ENGINE3_INT)
#define LP5569_REG_IO_CONTROL		0x3D
#define   LP5569_CLK_OUTPUT		BIT(3)
#define LP5569_REG_RESET		0x3F
#define   LP5569_RESET			0xFF
#define LP5569_REG_MASTER_FADER_BASE	0x46
#define LP5569_REG_CH1_PROG_START	0x4B
#define LP5569_REG_CH2_PROG_START	0x4C
#define LP5569_REG_CH3_PROG_START	0x4D
#define LP5569_REG_PROG_PAGE_SEL	0x4F
#define LP5569_REG_PROG_MEM		0x50
#define LP5569_REG_LED_FAULT1		0x81
#define   LP5569_LED_FAULT8		BIT(0)
#define LP5569_REG_LED_FAULT2		0x82
#define   LP5569_LED_FAULT7		BIT(7)
#define   LP5569_LED_FAULT6		BIT(6)
#define   LP5569_LED_FAULT5		BIT(5)
#define   LP5569_LED_FAULT4		BIT(4)
#define   LP5569_LED_FAULT3		BIT(3)
#define   LP5569_LED_FAULT2		BIT(2)
#define   LP5569_LED_FAULT1		BIT(1)
#define   LP5569_LED_FAULT0		BIT(0)

#define LP5569_ENG1_PROG_ADDR		0x0
#define LP5569_ENG2_PROG_ADDR		0x40
#define LP5569_ENG3_PROG_ADDR		0x80
#define LP5569_ENG1_MUX_ADDR		0xc0
#define LP5569_ENG2_MUX_ADDR		0xd0
#define LP5569_ENG3_MUX_ADDR		0xe0

#define LP5569_STARTUP_SLEEP		500

#define LEDn_STATUS_FAULT(n, status)	((status) >> (n) & BIT(0))

#define LP5569_DEFAULT_CONFIG \
	(LP5569_AUTO_INC | LP5569_PWR_SAVE | LP5569_PWM_PWR_SAVE)

static void lp5569_run_engine(struct lp55xx_chip *chip, bool start)
{
	if (!start) {
		lp55xx_stop_engine(chip);
		lp55xx_turn_off_channels(chip);
		return;
	}

	lp55xx_run_engine_common(chip);
}

static int lp5569_init_program_engine(struct lp55xx_chip *chip)
{
	int i;
	int j;
	int ret;
	u8 status;
	/* Precompiled pattern per ENGINE setting LED MUX start and stop addresses */
	static const u8 pattern[][LP55xx_BYTES_PER_PAGE] =  {
		{ 0x9c, LP5569_ENG1_MUX_ADDR, 0x9c, 0xb0, 0x9d, 0x80, 0xd8, 0x00, 0},
		{ 0x9c, LP5569_ENG2_MUX_ADDR, 0x9c, 0xc0, 0x9d, 0x80, 0xd8, 0x00, 0},
		{ 0x9c, LP5569_ENG3_MUX_ADDR, 0x9c, 0xd0, 0x9d, 0x80, 0xd8, 0x00, 0},
	};

	/* Setup each ENGINE program start address */
	ret = lp55xx_write(chip, LP5569_REG_CH1_PROG_START, LP5569_ENG1_PROG_ADDR);
	if (ret)
		return ret;

	ret = lp55xx_write(chip, LP5569_REG_CH2_PROG_START, LP5569_ENG2_PROG_ADDR);
	if (ret)
		return ret;

	ret = lp55xx_write(chip, LP5569_REG_CH3_PROG_START, LP5569_ENG3_PROG_ADDR);
	if (ret)
		return ret;

	/* Write precompiled pattern for LED MUX address space for each ENGINE */
	for (i = LP55XX_ENGINE_1; i <= LP55XX_ENGINE_3; i++) {
		chip->engine_idx = i;
		lp55xx_load_engine(chip);

		for (j = 0; j < LP55xx_BYTES_PER_PAGE; j++) {
			ret = lp55xx_write(chip, LP5569_REG_PROG_MEM + j,
					   pattern[i - 1][j]);
			if (ret)
				goto out;
		}
	}

	lp5569_run_engine(chip, true);

	/* Let the programs run for couple of ms and check the engine status */
	usleep_range(3000, 6000);
	lp55xx_read(chip, LP5569_REG_STATUS, &status);
	status = FIELD_GET(LP5569_ENG_STATUS_MASK, status);

	if (status != LP5569_ENG_STATUS_MASK) {
		dev_err(&chip->cl->dev,
			"could not configure LED engine, status = 0x%.2x\n",
			status);
		ret = -EINVAL;
	}

out:
	lp55xx_stop_all_engine(chip);
	return ret;
}

static int lp5569_post_init_device(struct lp55xx_chip *chip)
{
	int ret;
	u8 val;

	val = LP5569_DEFAULT_CONFIG;
	val |= FIELD_PREP(LP5569_CP_MODE_MASK, chip->pdata->charge_pump_mode);
	ret = lp55xx_write(chip, LP5569_REG_MISC, val);
	if (ret)
		return ret;

	if (chip->pdata->clock_mode == LP55XX_CLOCK_INT) {
		/* Internal clock MUST be configured before CLK output */
		ret = lp55xx_update_bits(chip, LP5569_REG_MISC,
					 LP5569_INTERNAL_CLK,
					 LP5569_INTERNAL_CLK);
		if (ret)
			return ret;

		ret = lp55xx_update_bits(chip, LP5569_REG_IO_CONTROL,
					 LP5569_CLK_OUTPUT,
					 LP5569_CLK_OUTPUT);
		if (ret)
			return ret;
	}

	ret = lp55xx_write(chip, LP5569_REG_ENABLE, LP5569_ENABLE);
	if (ret)
		return ret;

	read_poll_timeout(lp55xx_read, ret, !(val & LP5569_STARTUP_BUSY),
			  LP5569_STARTUP_SLEEP, LP5569_STARTUP_SLEEP * 10, false,
			  chip, LP5569_REG_STATUS, &val);

	return lp5569_init_program_engine(chip);
}

static ssize_t lp5569_led_open_test(struct lp55xx_led *led, char *buf)
{
	struct lp55xx_chip *chip = led->chip;
	struct lp55xx_platform_data *pdata = chip->pdata;
	bool leds_fault[LP5569_MAX_LEDS];
	struct lp55xx_led *led_tmp = led;
	int i, ret, pos = 0;
	u8 status;

	/* Set in STANDBY state */
	ret = lp55xx_write(chip, LP5569_REG_ENABLE, 0);
	if (ret)
		goto exit;

	/* Wait 1ms for device to enter STANDBY state */
	usleep_range(1000, 2000);

	/* Set Charge Pump to 1.5x */
	ret = lp55xx_update_bits(chip, LP5569_REG_MISC,
				 FIELD_PREP(LP5569_CP_MODE_MASK, LP55XX_CP_BOOST),
				 LP5569_CP_MODE_MASK);
	if (ret)
		goto exit;

	/* Enable LED Open Test */
	ret = lp55xx_update_bits(chip, LP5569_REG_MISC2, LP5569_LED_OPEN_TEST,
				 LP5569_LED_OPEN_TEST);
	if (ret)
		goto exit;

	/* Put Device in NORMAL state */
	ret = lp55xx_write(chip, LP5569_REG_ENABLE, LP5569_ENABLE);
	if (ret)
		goto exit;

	/* Wait 500 us for device to enter NORMAL state */
	usleep_range(500, 750);

	/* Enable LED and set to 100% brightness */
	for (i = 0; i < pdata->num_channels; i++) {
		ret = lp55xx_write(chip, LP5569_REG_LED_PWM_BASE + led_tmp->chan_nr,
				   LED_FULL);
		if (ret)
			goto exit;

		led_tmp++;
	}

	/* Wait 500 us for device to fill status regs */
	usleep_range(500, 750);

	/* Parse status led fault 1 regs */
	ret = lp55xx_read(chip, LP5569_REG_LED_FAULT1, &status);
	if (ret < 0)
		goto exit;

	for (i = 0; i < 8; i++)
		leds_fault[i] = !!((status >> i) & 0x1);

	/* Parse status led fault 2 regs */
	ret = lp55xx_read(chip, LP5569_REG_LED_FAULT2, &status);
	if (ret < 0)
		goto exit;

	for (i = 0; i < 1; i++)
		leds_fault[i + 8] = !!((status >> i) & 0x1);

	/* Report LED fault */
	led_tmp = led;
	for (i = 0; i < pdata->num_channels; i++) {
		if (leds_fault[led_tmp->chan_nr])
			pos += sysfs_emit_at(buf, pos, "LED %d OPEN FAIL\n",
					     led_tmp->chan_nr);

		led_tmp++;
	}

	ret = pos;

exit:
	/* Disable LED Open Test */
	lp55xx_update_bits(chip, LP5569_REG_MISC2, LP5569_LED_OPEN_TEST, 0);

	led_tmp = led;
	for (i = 0; i < pdata->num_channels; i++) {
		lp55xx_write(chip, LP5569_REG_LED_PWM_BASE + led_tmp->chan_nr, 0);

		led_tmp++;
	}

	return ret;
}

static ssize_t lp5569_led_short_test(struct lp55xx_led *led, char *buf)
{
	struct lp55xx_chip *chip = led->chip;
	struct lp55xx_platform_data *pdata = chip->pdata;
	bool leds_fault[LP5569_MAX_LEDS];
	struct lp55xx_led *led_tmp = led;
	int i, ret, pos = 0;
	u8 status;

	/* Set in STANDBY state */
	ret = lp55xx_write(chip, LP5569_REG_ENABLE, 0);
	if (ret)
		goto exit;

	/* Wait 1ms for device to enter STANDBY state */
	usleep_range(1000, 2000);

	/* Set Charge Pump to 1x */
	ret = lp55xx_update_bits(chip, LP5569_REG_MISC,
				 FIELD_PREP(LP5569_CP_MODE_MASK, LP55XX_CP_BYPASS),
				 LP5569_CP_MODE_MASK);
	if (ret)
		goto exit;

	/* Enable LED and set to 100% brightness and current to 100% (25.5mA) */
	for (i = 0; i < pdata->num_channels; i++) {
		ret = lp55xx_write(chip, LP5569_REG_LED_PWM_BASE + led_tmp->chan_nr,
				   LED_FULL);
		if (ret)
			goto exit;

		ret = lp55xx_write(chip, LP5569_REG_LED_CURRENT_BASE + led_tmp->chan_nr,
				   LED_FULL);
		if (ret)
			goto exit;

		led_tmp++;
	}

	/* Put Device in NORMAL state */
	ret = lp55xx_write(chip, LP5569_REG_ENABLE, LP5569_ENABLE);
	if (ret)
		goto exit;

	/* Wait 500 us for device to enter NORMAL state */
	usleep_range(500, 750);

	/* Enable LED Shorted Test */
	ret = lp55xx_update_bits(chip, LP5569_REG_MISC2, LP5569_LED_OPEN_TEST,
				 LP5569_LED_SHORT_TEST);
	if (ret)
		goto exit;

	/* Wait 500 us for device to fill status regs */
	usleep_range(500, 750);

	/* Parse status led fault 1 regs */
	ret = lp55xx_read(chip, LP5569_REG_LED_FAULT1, &status);
	if (ret < 0)
		goto exit;

	for (i = 0; i < 8; i++)
		leds_fault[i] = !!LEDn_STATUS_FAULT(i, status);

	/* Parse status led fault 2 regs */
	ret = lp55xx_read(chip, LP5569_REG_LED_FAULT2, &status);
	if (ret < 0)
		goto exit;

	for (i = 0; i < 1; i++)
		leds_fault[i + 8] = !!LEDn_STATUS_FAULT(i, status);

	/* Report LED fault */
	led_tmp = led;
	for (i = 0; i < pdata->num_channels; i++) {
		if (leds_fault[led_tmp->chan_nr])
			pos += sysfs_emit_at(buf, pos, "LED %d SHORTED FAIL\n",
					     led_tmp->chan_nr);

		led_tmp++;
	}

	ret = pos;

exit:
	/* Disable LED Shorted Test */
	lp55xx_update_bits(chip, LP5569_REG_MISC2, LP5569_LED_SHORT_TEST, 0);

	led_tmp = led;
	for (i = 0; i < pdata->num_channels; i++) {
		lp55xx_write(chip, LP5569_REG_LED_PWM_BASE + led_tmp->chan_nr, 0);

		led_tmp++;
	}

	return ret;
}

static ssize_t lp5569_selftest(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct lp55xx_led *led = i2c_get_clientdata(to_i2c_client(dev));
	struct lp55xx_chip *chip = led->chip;
	int i, pos = 0;

	guard(mutex)(&chip->lock);

	/* Test LED Open */
	pos = lp5569_led_open_test(led, buf);
	if (pos < 0)
		return sprintf(buf, "FAIL\n");

	/* Test LED Shorted */
	pos += lp5569_led_short_test(led, buf);
	if (pos < 0)
		return sprintf(buf, "FAIL\n");

	for (i = 0; i < chip->pdata->num_channels; i++) {
		/* Restore current */
		lp55xx_write(chip, LP5569_REG_LED_CURRENT_BASE + led->chan_nr,
			     led->led_current);

		/* Restore brightness */
		lp55xx_write(chip, LP5569_REG_LED_PWM_BASE + led->chan_nr,
			     led->brightness);
		led++;
	}

	return pos == 0 ? sysfs_emit(buf, "OK\n") : pos;
}

LP55XX_DEV_ATTR_ENGINE_MODE(1);
LP55XX_DEV_ATTR_ENGINE_MODE(2);
LP55XX_DEV_ATTR_ENGINE_MODE(3);
LP55XX_DEV_ATTR_ENGINE_LEDS(1);
LP55XX_DEV_ATTR_ENGINE_LEDS(2);
LP55XX_DEV_ATTR_ENGINE_LEDS(3);
LP55XX_DEV_ATTR_ENGINE_LOAD(1);
LP55XX_DEV_ATTR_ENGINE_LOAD(2);
LP55XX_DEV_ATTR_ENGINE_LOAD(3);
static LP55XX_DEV_ATTR_RO(selftest, lp5569_selftest);
LP55XX_DEV_ATTR_MASTER_FADER(1);
LP55XX_DEV_ATTR_MASTER_FADER(2);
LP55XX_DEV_ATTR_MASTER_FADER(3);
static LP55XX_DEV_ATTR_RW(master_fader_leds, lp55xx_show_master_fader_leds,
			  lp55xx_store_master_fader_leds);

static struct attribute *lp5569_attributes[] = {
	&dev_attr_engine1_mode.attr,
	&dev_attr_engine2_mode.attr,
	&dev_attr_engine3_mode.attr,
	&dev_attr_engine1_load.attr,
	&dev_attr_engine2_load.attr,
	&dev_attr_engine3_load.attr,
	&dev_attr_engine1_leds.attr,
	&dev_attr_engine2_leds.attr,
	&dev_attr_engine3_leds.attr,
	&dev_attr_selftest.attr,
	&dev_attr_master_fader1.attr,
	&dev_attr_master_fader2.attr,
	&dev_attr_master_fader3.attr,
	&dev_attr_master_fader_leds.attr,
	NULL,
};

static const struct attribute_group lp5569_group = {
	.attrs = lp5569_attributes,
};

/* Chip specific configurations */
static struct lp55xx_device_config lp5569_cfg = {
	.reg_op_mode = {
		.addr = LP5569_REG_OP_MODE,
		.shift = LP5569_MODE_ENG_SHIFT,
	},
	.reg_exec = {
		.addr = LP5569_REG_EXEC_CTRL,
		.shift = LP5569_EXEC_ENG_SHIFT,
	},
	.reset = {
		.addr = LP5569_REG_RESET,
		.val  = LP5569_RESET,
	},
	.enable = {
		.addr = LP5569_REG_ENABLE,
		.val  = LP5569_ENABLE,
	},
	.prog_mem_base = {
		.addr = LP5569_REG_PROG_MEM,
	},
	.reg_led_pwm_base = {
		.addr = LP5569_REG_LED_PWM_BASE,
	},
	.reg_led_current_base = {
		.addr = LP5569_REG_LED_CURRENT_BASE,
	},
	.reg_master_fader_base = {
		.addr = LP5569_REG_MASTER_FADER_BASE,
	},
	.reg_led_ctrl_base = {
		.addr = LP5569_REG_LED_CTRL_BASE,
	},
	.pages_per_engine   = LP5569_PAGES_PER_ENGINE,
	.max_channel  = LP5569_MAX_LEDS,
	.post_init_device   = lp5569_post_init_device,
	.brightness_fn      = lp55xx_led_brightness,
	.multicolor_brightness_fn = lp55xx_multicolor_brightness,
	.set_led_current    = lp55xx_set_led_current,
	.firmware_cb        = lp55xx_firmware_loaded_cb,
	.run_engine         = lp5569_run_engine,
	.dev_attr_group     = &lp5569_group,
};

static const struct i2c_device_id lp5569_id[] = {
	{ "lp5569",  .driver_data = (kernel_ulong_t)&lp5569_cfg, },
	{ }
};

MODULE_DEVICE_TABLE(i2c, lp5569_id);

static const struct of_device_id of_lp5569_leds_match[] = {
	{ .compatible = "ti,lp5569", .data = &lp5569_cfg, },
	{},
};

MODULE_DEVICE_TABLE(of, of_lp5569_leds_match);

static struct i2c_driver lp5569_driver = {
	.driver = {
		.name	= "lp5569",
		.of_match_table = of_lp5569_leds_match,
	},
	.probe		= lp55xx_probe,
	.remove		= lp55xx_remove,
	.id_table	= lp5569_id,
};

module_i2c_driver(lp5569_driver);

MODULE_AUTHOR("Christian Marangi <ansuelsmth@gmail.com>");
MODULE_DESCRIPTION("LP5569 LED engine");
MODULE_LICENSE("GPL");
