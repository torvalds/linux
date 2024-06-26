// SPDX-License-Identifier: GPL-2.0-only
/*
 * lp5523.c - LP5523, LP55231 LED Driver
 *
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2012 Texas Instruments
 *
 * Contact: Samu Onkalo <samu.p.onkalo@nokia.com>
 *          Milo(Woogyom) Kim <milo.kim@ti.com>
 */

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_data/leds-lp55xx.h>
#include <linux/slab.h>

#include "leds-lp55xx-common.h"

#define LP5523_PROGRAM_LENGTH		32	/* bytes */
/* Memory is used like this:
 * 0x00 engine 1 program
 * 0x10 engine 2 program
 * 0x20 engine 3 program
 * 0x30 engine 1 muxing info
 * 0x40 engine 2 muxing info
 * 0x50 engine 3 muxing info
 */
#define LP5523_PAGES_PER_ENGINE		1
#define LP5523_MAX_LEDS			9

/* Registers */
#define LP5523_REG_ENABLE		0x00
#define LP5523_REG_OP_MODE		0x01
#define LP5523_REG_ENABLE_LEDS_MSB	0x04
#define LP5523_REG_ENABLE_LEDS_LSB	0x05
#define LP5523_REG_LED_CTRL_BASE	0x06
#define LP5523_REG_LED_PWM_BASE		0x16
#define LP5523_REG_LED_CURRENT_BASE	0x26
#define LP5523_REG_CONFIG		0x36

#define LP5523_REG_STATUS		0x3A
#define LP5523_ENGINE_BUSY		BIT(4)

#define LP5523_REG_RESET		0x3D
#define LP5523_REG_LED_TEST_CTRL	0x41
#define LP5523_REG_LED_TEST_ADC		0x42
#define LP5523_REG_MASTER_FADER_BASE	0x48
#define LP5523_REG_CH1_PROG_START	0x4C
#define LP5523_REG_CH2_PROG_START	0x4D
#define LP5523_REG_CH3_PROG_START	0x4E
#define LP5523_REG_PROG_PAGE_SEL	0x4F
#define LP5523_REG_PROG_MEM		0x50

/* Bit description in registers */
#define LP5523_ENABLE			0x40
#define LP5523_AUTO_INC			0x40
#define LP5523_PWR_SAVE			0x20
#define LP5523_PWM_PWR_SAVE		0x04
#define LP5523_CP_MODE_MASK		0x18
#define LP5523_CP_MODE_SHIFT		3
#define LP5523_AUTO_CLK			0x02
#define LP5523_DEFAULT_CONFIG \
	(LP5523_AUTO_INC | LP5523_PWR_SAVE | LP5523_AUTO_CLK | LP5523_PWM_PWR_SAVE)

#define LP5523_EN_LEDTEST		0x80
#define LP5523_LEDTEST_DONE		0x80
#define LP5523_RESET			0xFF
#define LP5523_ADC_SHORTCIRC_LIM	80
#define LP5523_EXT_CLK_USED		0x08
#define LP5523_ENG_STATUS_MASK		0x07

#define LP5523_FADER_MAPPING_MASK	0xC0
#define LP5523_FADER_MAPPING_SHIFT	6

/* Memory Page Selection */
#define LP5523_PAGE_ENG1		0
#define LP5523_PAGE_ENG2		1
#define LP5523_PAGE_ENG3		2
#define LP5523_PAGE_MUX1		3
#define LP5523_PAGE_MUX2		4
#define LP5523_PAGE_MUX3		5

/* Program Memory Operations */
#define LP5523_MODE_ENG1_M		0x30	/* Operation Mode Register */
#define LP5523_MODE_ENG2_M		0x0C
#define LP5523_MODE_ENG3_M		0x03
#define LP5523_LOAD_ENG1		0x10
#define LP5523_LOAD_ENG2		0x04
#define LP5523_LOAD_ENG3		0x01

#define LP5523_ENG1_IS_LOADING(mode)	\
	((mode & LP5523_MODE_ENG1_M) == LP5523_LOAD_ENG1)
#define LP5523_ENG2_IS_LOADING(mode)	\
	((mode & LP5523_MODE_ENG2_M) == LP5523_LOAD_ENG2)
#define LP5523_ENG3_IS_LOADING(mode)	\
	((mode & LP5523_MODE_ENG3_M) == LP5523_LOAD_ENG3)

#define LP5523_EXEC_ENG1_M		0x30	/* Enable Register */
#define LP5523_EXEC_ENG2_M		0x0C
#define LP5523_EXEC_ENG3_M		0x03
#define LP5523_EXEC_M			0x3F
#define LP5523_RUN_ENG1			0x20
#define LP5523_RUN_ENG2			0x08
#define LP5523_RUN_ENG3			0x02

#define LED_ACTIVE(mux, led)		(!!(mux & (0x0001 << led)))

enum lp5523_chip_id {
	LP5523,
	LP55231,
};

static int lp5523_init_program_engine(struct lp55xx_chip *chip);

static inline void lp5523_wait_opmode_done(void)
{
	usleep_range(1000, 2000);
}

static int lp5523_post_init_device(struct lp55xx_chip *chip)
{
	int ret;
	int val;

	ret = lp55xx_write(chip, LP5523_REG_ENABLE, LP5523_ENABLE);
	if (ret)
		return ret;

	/* Chip startup time is 500 us, 1 - 2 ms gives some margin */
	usleep_range(1000, 2000);

	val = LP5523_DEFAULT_CONFIG;
	val |= (chip->pdata->charge_pump_mode << LP5523_CP_MODE_SHIFT) & LP5523_CP_MODE_MASK;

	ret = lp55xx_write(chip, LP5523_REG_CONFIG, val);
	if (ret)
		return ret;

	/* turn on all leds */
	ret = lp55xx_write(chip, LP5523_REG_ENABLE_LEDS_MSB, 0x01);
	if (ret)
		return ret;

	ret = lp55xx_write(chip, LP5523_REG_ENABLE_LEDS_LSB, 0xff);
	if (ret)
		return ret;

	return lp5523_init_program_engine(chip);
}

static void lp5523_run_engine(struct lp55xx_chip *chip, bool start)
{
	/* stop engine */
	if (!start) {
		lp55xx_stop_engine(chip);
		lp55xx_turn_off_channels(chip);
		return;
	}

	lp55xx_run_engine_common(chip);
}

static int lp5523_init_program_engine(struct lp55xx_chip *chip)
{
	int i;
	int j;
	int ret;
	u8 status;
	/* one pattern per engine setting LED MUX start and stop addresses */
	static const u8 pattern[][LP5523_PROGRAM_LENGTH] =  {
		{ 0x9c, 0x30, 0x9c, 0xb0, 0x9d, 0x80, 0xd8, 0x00, 0},
		{ 0x9c, 0x40, 0x9c, 0xc0, 0x9d, 0x80, 0xd8, 0x00, 0},
		{ 0x9c, 0x50, 0x9c, 0xd0, 0x9d, 0x80, 0xd8, 0x00, 0},
	};

	/* hardcode 32 bytes of memory for each engine from program memory */
	ret = lp55xx_write(chip, LP5523_REG_CH1_PROG_START, 0x00);
	if (ret)
		return ret;

	ret = lp55xx_write(chip, LP5523_REG_CH2_PROG_START, 0x10);
	if (ret)
		return ret;

	ret = lp55xx_write(chip, LP5523_REG_CH3_PROG_START, 0x20);
	if (ret)
		return ret;

	/* write LED MUX address space for each engine */
	for (i = LP55XX_ENGINE_1; i <= LP55XX_ENGINE_3; i++) {
		chip->engine_idx = i;
		lp55xx_load_engine(chip);

		for (j = 0; j < LP5523_PROGRAM_LENGTH; j++) {
			ret = lp55xx_write(chip, LP5523_REG_PROG_MEM + j,
					pattern[i - 1][j]);
			if (ret)
				goto out;
		}
	}

	lp5523_run_engine(chip, true);

	/* Let the programs run for couple of ms and check the engine status */
	usleep_range(3000, 6000);
	ret = lp55xx_read(chip, LP5523_REG_STATUS, &status);
	if (ret)
		goto out;
	status &= LP5523_ENG_STATUS_MASK;

	if (status != LP5523_ENG_STATUS_MASK) {
		dev_err(&chip->cl->dev,
			"could not configure LED engine, status = 0x%.2x\n",
			status);
		ret = -1;
	}

out:
	lp55xx_stop_all_engine(chip);
	return ret;
}

static ssize_t lp5523_selftest(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct lp55xx_led *led = i2c_get_clientdata(to_i2c_client(dev));
	struct lp55xx_chip *chip = led->chip;
	struct lp55xx_platform_data *pdata = chip->pdata;
	int ret, pos = 0;
	u8 status, adc, vdd, i;

	mutex_lock(&chip->lock);

	ret = lp55xx_read(chip, LP5523_REG_STATUS, &status);
	if (ret < 0)
		goto fail;

	/* Check that ext clock is really in use if requested */
	if (pdata->clock_mode == LP55XX_CLOCK_EXT) {
		if  ((status & LP5523_EXT_CLK_USED) == 0)
			goto fail;
	}

	/* Measure VDD (i.e. VBAT) first (channel 16 corresponds to VDD) */
	lp55xx_write(chip, LP5523_REG_LED_TEST_CTRL, LP5523_EN_LEDTEST | 16);
	usleep_range(3000, 6000); /* ADC conversion time is typically 2.7 ms */
	ret = lp55xx_read(chip, LP5523_REG_STATUS, &status);
	if (ret < 0)
		goto fail;

	if (!(status & LP5523_LEDTEST_DONE))
		usleep_range(3000, 6000); /* Was not ready. Wait little bit */

	ret = lp55xx_read(chip, LP5523_REG_LED_TEST_ADC, &vdd);
	if (ret < 0)
		goto fail;

	vdd--;	/* There may be some fluctuation in measurement */

	for (i = 0; i < pdata->num_channels; i++) {
		/* Skip disabled channels */
		if (pdata->led_config[i].led_current == 0)
			continue;

		/* Set default current */
		lp55xx_write(chip, LP5523_REG_LED_CURRENT_BASE + led->chan_nr,
			pdata->led_config[i].led_current);

		lp55xx_write(chip, LP5523_REG_LED_PWM_BASE + led->chan_nr,
			     0xff);
		/* let current stabilize 2 - 4ms before measurements start */
		usleep_range(2000, 4000);
		lp55xx_write(chip, LP5523_REG_LED_TEST_CTRL,
			     LP5523_EN_LEDTEST | led->chan_nr);
		/* ADC conversion time is 2.7 ms typically */
		usleep_range(3000, 6000);
		ret = lp55xx_read(chip, LP5523_REG_STATUS, &status);
		if (ret < 0)
			goto fail;

		if (!(status & LP5523_LEDTEST_DONE))
			usleep_range(3000, 6000); /* Was not ready. Wait. */

		ret = lp55xx_read(chip, LP5523_REG_LED_TEST_ADC, &adc);
		if (ret < 0)
			goto fail;

		if (adc >= vdd || adc < LP5523_ADC_SHORTCIRC_LIM)
			pos += sprintf(buf + pos, "LED %d FAIL\n",
				       led->chan_nr);

		lp55xx_write(chip, LP5523_REG_LED_PWM_BASE + led->chan_nr,
			     0x00);

		/* Restore current */
		lp55xx_write(chip, LP5523_REG_LED_CURRENT_BASE + led->chan_nr,
			     led->led_current);
		led++;
	}
	if (pos == 0)
		pos = sprintf(buf, "OK\n");
	goto release_lock;
fail:
	pos = sprintf(buf, "FAIL\n");

release_lock:
	mutex_unlock(&chip->lock);

	return pos;
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
static LP55XX_DEV_ATTR_RO(selftest, lp5523_selftest);
LP55XX_DEV_ATTR_MASTER_FADER(1);
LP55XX_DEV_ATTR_MASTER_FADER(2);
LP55XX_DEV_ATTR_MASTER_FADER(3);
static LP55XX_DEV_ATTR_RW(master_fader_leds, lp55xx_show_master_fader_leds,
			  lp55xx_store_master_fader_leds);

static struct attribute *lp5523_attributes[] = {
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

static const struct attribute_group lp5523_group = {
	.attrs = lp5523_attributes,
};

/* Chip specific configurations */
static struct lp55xx_device_config lp5523_cfg = {
	.reg_op_mode = {
		.addr = LP5523_REG_OP_MODE,
	},
	.reg_exec = {
		.addr = LP5523_REG_ENABLE,
	},
	.engine_busy = {
		.addr = LP5523_REG_STATUS,
		.mask  = LP5523_ENGINE_BUSY,
	},
	.reset = {
		.addr = LP5523_REG_RESET,
		.val  = LP5523_RESET,
	},
	.enable = {
		.addr = LP5523_REG_ENABLE,
		.val  = LP5523_ENABLE,
	},
	.prog_mem_base = {
		.addr = LP5523_REG_PROG_MEM,
	},
	.reg_led_pwm_base = {
		.addr = LP5523_REG_LED_PWM_BASE,
	},
	.reg_led_current_base = {
		.addr = LP5523_REG_LED_CURRENT_BASE,
	},
	.reg_master_fader_base = {
		.addr = LP5523_REG_MASTER_FADER_BASE,
	},
	.reg_led_ctrl_base = {
		.addr = LP5523_REG_LED_CTRL_BASE,
	},
	.pages_per_engine   = LP5523_PAGES_PER_ENGINE,
	.max_channel  = LP5523_MAX_LEDS,
	.post_init_device   = lp5523_post_init_device,
	.brightness_fn      = lp55xx_led_brightness,
	.multicolor_brightness_fn = lp55xx_multicolor_brightness,
	.set_led_current    = lp55xx_set_led_current,
	.firmware_cb        = lp55xx_firmware_loaded_cb,
	.run_engine         = lp5523_run_engine,
	.dev_attr_group     = &lp5523_group,
};

static const struct i2c_device_id lp5523_id[] = {
	{ "lp5523",  .driver_data = (kernel_ulong_t)&lp5523_cfg, },
	{ "lp55231", .driver_data = (kernel_ulong_t)&lp5523_cfg, },
	{ }
};

MODULE_DEVICE_TABLE(i2c, lp5523_id);

static const struct of_device_id of_lp5523_leds_match[] = {
	{ .compatible = "national,lp5523", .data = &lp5523_cfg, },
	{ .compatible = "ti,lp55231", .data = &lp5523_cfg, },
	{},
};

MODULE_DEVICE_TABLE(of, of_lp5523_leds_match);

static struct i2c_driver lp5523_driver = {
	.driver = {
		.name	= "lp5523x",
		.of_match_table = of_lp5523_leds_match,
	},
	.probe		= lp55xx_probe,
	.remove		= lp55xx_remove,
	.id_table	= lp5523_id,
};

module_i2c_driver(lp5523_driver);

MODULE_AUTHOR("Mathias Nyman <mathias.nyman@nokia.com>");
MODULE_AUTHOR("Milo Kim <milo.kim@ti.com>");
MODULE_DESCRIPTION("LP5523 LED engine");
MODULE_LICENSE("GPL");
