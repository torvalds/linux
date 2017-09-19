/*
 * LP5521 LED chip driver.
 *
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2012 Texas Instruments
 *
 * Contact: Samu Onkalo <samu.p.onkalo@nokia.com>
 *          Milo(Woogyom) Kim <milo.kim@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_data/leds-lp55xx.h>
#include <linux/slab.h>
#include <linux/of.h>

#include "leds-lp55xx-common.h"

#define LP5521_PROGRAM_LENGTH		32
#define LP5521_MAX_LEDS			3
#define LP5521_CMD_DIRECT		0x3F

/* Registers */
#define LP5521_REG_ENABLE		0x00
#define LP5521_REG_OP_MODE		0x01
#define LP5521_REG_R_PWM		0x02
#define LP5521_REG_G_PWM		0x03
#define LP5521_REG_B_PWM		0x04
#define LP5521_REG_R_CURRENT		0x05
#define LP5521_REG_G_CURRENT		0x06
#define LP5521_REG_B_CURRENT		0x07
#define LP5521_REG_CONFIG		0x08
#define LP5521_REG_STATUS		0x0C
#define LP5521_REG_RESET		0x0D
#define LP5521_REG_R_PROG_MEM		0x10
#define LP5521_REG_G_PROG_MEM		0x30
#define LP5521_REG_B_PROG_MEM		0x50

/* Base register to set LED current */
#define LP5521_REG_LED_CURRENT_BASE	LP5521_REG_R_CURRENT
/* Base register to set the brightness */
#define LP5521_REG_LED_PWM_BASE		LP5521_REG_R_PWM

/* Bits in ENABLE register */
#define LP5521_MASTER_ENABLE		0x40	/* Chip master enable */
#define LP5521_LOGARITHMIC_PWM		0x80	/* Logarithmic PWM adjustment */
#define LP5521_EXEC_RUN			0x2A
#define LP5521_ENABLE_DEFAULT	\
	(LP5521_MASTER_ENABLE | LP5521_LOGARITHMIC_PWM)
#define LP5521_ENABLE_RUN_PROGRAM	\
	(LP5521_ENABLE_DEFAULT | LP5521_EXEC_RUN)

/* CONFIG register */
#define LP5521_PWM_HF			0x40	/* PWM: 0 = 256Hz, 1 = 558Hz */
#define LP5521_PWRSAVE_EN		0x20	/* 1 = Power save mode */
#define LP5521_CP_MODE_OFF		0	/* Charge pump (CP) off */
#define LP5521_CP_MODE_BYPASS		8	/* CP forced to bypass mode */
#define LP5521_CP_MODE_1X5		0x10	/* CP forced to 1.5x mode */
#define LP5521_CP_MODE_AUTO		0x18	/* Automatic mode selection */
#define LP5521_R_TO_BATT		0x04	/* R out: 0 = CP, 1 = Vbat */
#define LP5521_CLK_INT			0x01	/* Internal clock */
#define LP5521_DEFAULT_CFG		\
	(LP5521_PWM_HF | LP5521_PWRSAVE_EN | LP5521_CP_MODE_AUTO)

/* Status */
#define LP5521_EXT_CLK_USED		0x08

/* default R channel current register value */
#define LP5521_REG_R_CURR_DEFAULT	0xAF

/* Reset register value */
#define LP5521_RESET			0xFF

/* Program Memory Operations */
#define LP5521_MODE_R_M			0x30	/* Operation Mode Register */
#define LP5521_MODE_G_M			0x0C
#define LP5521_MODE_B_M			0x03
#define LP5521_LOAD_R			0x10
#define LP5521_LOAD_G			0x04
#define LP5521_LOAD_B			0x01

#define LP5521_R_IS_LOADING(mode)	\
	((mode & LP5521_MODE_R_M) == LP5521_LOAD_R)
#define LP5521_G_IS_LOADING(mode)	\
	((mode & LP5521_MODE_G_M) == LP5521_LOAD_G)
#define LP5521_B_IS_LOADING(mode)	\
	((mode & LP5521_MODE_B_M) == LP5521_LOAD_B)

#define LP5521_EXEC_R_M			0x30	/* Enable Register */
#define LP5521_EXEC_G_M			0x0C
#define LP5521_EXEC_B_M			0x03
#define LP5521_EXEC_M			0x3F
#define LP5521_RUN_R			0x20
#define LP5521_RUN_G			0x08
#define LP5521_RUN_B			0x02

static inline void lp5521_wait_opmode_done(void)
{
	/* operation mode change needs to be longer than 153 us */
	usleep_range(200, 300);
}

static inline void lp5521_wait_enable_done(void)
{
	/* it takes more 488 us to update ENABLE register */
	usleep_range(500, 600);
}

static void lp5521_set_led_current(struct lp55xx_led *led, u8 led_current)
{
	led->led_current = led_current;
	lp55xx_write(led->chip, LP5521_REG_LED_CURRENT_BASE + led->chan_nr,
		led_current);
}

static void lp5521_load_engine(struct lp55xx_chip *chip)
{
	enum lp55xx_engine_index idx = chip->engine_idx;
	static const u8 mask[] = {
		[LP55XX_ENGINE_1] = LP5521_MODE_R_M,
		[LP55XX_ENGINE_2] = LP5521_MODE_G_M,
		[LP55XX_ENGINE_3] = LP5521_MODE_B_M,
	};

	static const u8 val[] = {
		[LP55XX_ENGINE_1] = LP5521_LOAD_R,
		[LP55XX_ENGINE_2] = LP5521_LOAD_G,
		[LP55XX_ENGINE_3] = LP5521_LOAD_B,
	};

	lp55xx_update_bits(chip, LP5521_REG_OP_MODE, mask[idx], val[idx]);

	lp5521_wait_opmode_done();
}

static void lp5521_stop_all_engines(struct lp55xx_chip *chip)
{
	lp55xx_write(chip, LP5521_REG_OP_MODE, 0);
	lp5521_wait_opmode_done();
}

static void lp5521_stop_engine(struct lp55xx_chip *chip)
{
	enum lp55xx_engine_index idx = chip->engine_idx;
	static const u8 mask[] = {
		[LP55XX_ENGINE_1] = LP5521_MODE_R_M,
		[LP55XX_ENGINE_2] = LP5521_MODE_G_M,
		[LP55XX_ENGINE_3] = LP5521_MODE_B_M,
	};

	lp55xx_update_bits(chip, LP5521_REG_OP_MODE, mask[idx], 0);

	lp5521_wait_opmode_done();
}

static void lp5521_run_engine(struct lp55xx_chip *chip, bool start)
{
	int ret;
	u8 mode;
	u8 exec;

	/* stop engine */
	if (!start) {
		lp5521_stop_engine(chip);
		lp55xx_write(chip, LP5521_REG_OP_MODE, LP5521_CMD_DIRECT);
		lp5521_wait_opmode_done();
		return;
	}

	/*
	 * To run the engine,
	 * operation mode and enable register should updated at the same time
	 */

	ret = lp55xx_read(chip, LP5521_REG_OP_MODE, &mode);
	if (ret)
		return;

	ret = lp55xx_read(chip, LP5521_REG_ENABLE, &exec);
	if (ret)
		return;

	/* change operation mode to RUN only when each engine is loading */
	if (LP5521_R_IS_LOADING(mode)) {
		mode = (mode & ~LP5521_MODE_R_M) | LP5521_RUN_R;
		exec = (exec & ~LP5521_EXEC_R_M) | LP5521_RUN_R;
	}

	if (LP5521_G_IS_LOADING(mode)) {
		mode = (mode & ~LP5521_MODE_G_M) | LP5521_RUN_G;
		exec = (exec & ~LP5521_EXEC_G_M) | LP5521_RUN_G;
	}

	if (LP5521_B_IS_LOADING(mode)) {
		mode = (mode & ~LP5521_MODE_B_M) | LP5521_RUN_B;
		exec = (exec & ~LP5521_EXEC_B_M) | LP5521_RUN_B;
	}

	lp55xx_write(chip, LP5521_REG_OP_MODE, mode);
	lp5521_wait_opmode_done();

	lp55xx_update_bits(chip, LP5521_REG_ENABLE, LP5521_EXEC_M, exec);
	lp5521_wait_enable_done();
}

static int lp5521_update_program_memory(struct lp55xx_chip *chip,
					const u8 *data, size_t size)
{
	enum lp55xx_engine_index idx = chip->engine_idx;
	u8 pattern[LP5521_PROGRAM_LENGTH] = {0};
	static const u8 addr[] = {
		[LP55XX_ENGINE_1] = LP5521_REG_R_PROG_MEM,
		[LP55XX_ENGINE_2] = LP5521_REG_G_PROG_MEM,
		[LP55XX_ENGINE_3] = LP5521_REG_B_PROG_MEM,
	};
	unsigned cmd;
	char c[3];
	int nrchars;
	int ret;
	int offset = 0;
	int i = 0;

	while ((offset < size - 1) && (i < LP5521_PROGRAM_LENGTH)) {
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

	for (i = 0; i < LP5521_PROGRAM_LENGTH; i++) {
		ret = lp55xx_write(chip, addr[idx] + i, pattern[i]);
		if (ret)
			return -EINVAL;
	}

	return size;

err:
	dev_err(&chip->cl->dev, "wrong pattern format\n");
	return -EINVAL;
}

static void lp5521_firmware_loaded(struct lp55xx_chip *chip)
{
	const struct firmware *fw = chip->fw;

	if (fw->size > LP5521_PROGRAM_LENGTH) {
		dev_err(&chip->cl->dev, "firmware data size overflow: %zu\n",
			fw->size);
		return;
	}

	/*
	 * Program memory sequence
	 *  1) set engine mode to "LOAD"
	 *  2) write firmware data into program memory
	 */

	lp5521_load_engine(chip);
	lp5521_update_program_memory(chip, fw->data, fw->size);
}

static int lp5521_post_init_device(struct lp55xx_chip *chip)
{
	int ret;
	u8 val;

	/*
	 * Make sure that the chip is reset by reading back the r channel
	 * current reg. This is dummy read is required on some platforms -
	 * otherwise further access to the R G B channels in the
	 * LP5521_REG_ENABLE register will not have any effect - strange!
	 */
	ret = lp55xx_read(chip, LP5521_REG_R_CURRENT, &val);
	if (ret) {
		dev_err(&chip->cl->dev, "error in resetting chip\n");
		return ret;
	}
	if (val != LP5521_REG_R_CURR_DEFAULT) {
		dev_err(&chip->cl->dev,
			"unexpected data in register (expected 0x%x got 0x%x)\n",
			LP5521_REG_R_CURR_DEFAULT, val);
		ret = -EINVAL;
		return ret;
	}
	usleep_range(10000, 20000);

	/* Set all PWMs to direct control mode */
	ret = lp55xx_write(chip, LP5521_REG_OP_MODE, LP5521_CMD_DIRECT);

	/* Update configuration for the clock setting */
	val = LP5521_DEFAULT_CFG;
	if (!lp55xx_is_extclk_used(chip))
		val |= LP5521_CLK_INT;

	ret = lp55xx_write(chip, LP5521_REG_CONFIG, val);
	if (ret)
		return ret;

	/* Initialize all channels PWM to zero -> leds off */
	lp55xx_write(chip, LP5521_REG_R_PWM, 0);
	lp55xx_write(chip, LP5521_REG_G_PWM, 0);
	lp55xx_write(chip, LP5521_REG_B_PWM, 0);

	/* Set engines are set to run state when OP_MODE enables engines */
	ret = lp55xx_write(chip, LP5521_REG_ENABLE, LP5521_ENABLE_RUN_PROGRAM);
	if (ret)
		return ret;

	lp5521_wait_enable_done();

	return 0;
}

static int lp5521_run_selftest(struct lp55xx_chip *chip, char *buf)
{
	struct lp55xx_platform_data *pdata = chip->pdata;
	int ret;
	u8 status;

	ret = lp55xx_read(chip, LP5521_REG_STATUS, &status);
	if (ret < 0)
		return ret;

	if (pdata->clock_mode != LP55XX_CLOCK_EXT)
		return 0;

	/* Check that ext clock is really in use if requested */
	if  ((status & LP5521_EXT_CLK_USED) == 0)
		return -EIO;

	return 0;
}

static int lp5521_led_brightness(struct lp55xx_led *led)
{
	struct lp55xx_chip *chip = led->chip;
	int ret;

	mutex_lock(&chip->lock);
	ret = lp55xx_write(chip, LP5521_REG_LED_PWM_BASE + led->chan_nr,
		led->brightness);
	mutex_unlock(&chip->lock);

	return ret;
}

static ssize_t show_engine_mode(struct device *dev,
				struct device_attribute *attr,
				char *buf, int nr)
{
	struct lp55xx_led *led = i2c_get_clientdata(to_i2c_client(dev));
	struct lp55xx_chip *chip = led->chip;
	enum lp55xx_engine_mode mode = chip->engines[nr - 1].mode;

	switch (mode) {
	case LP55XX_ENGINE_RUN:
		return sprintf(buf, "run\n");
	case LP55XX_ENGINE_LOAD:
		return sprintf(buf, "load\n");
	case LP55XX_ENGINE_DISABLED:
	default:
		return sprintf(buf, "disabled\n");
	}
}
show_mode(1)
show_mode(2)
show_mode(3)

static ssize_t store_engine_mode(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t len, int nr)
{
	struct lp55xx_led *led = i2c_get_clientdata(to_i2c_client(dev));
	struct lp55xx_chip *chip = led->chip;
	struct lp55xx_engine *engine = &chip->engines[nr - 1];

	mutex_lock(&chip->lock);

	chip->engine_idx = nr;

	if (!strncmp(buf, "run", 3)) {
		lp5521_run_engine(chip, true);
		engine->mode = LP55XX_ENGINE_RUN;
	} else if (!strncmp(buf, "load", 4)) {
		lp5521_stop_engine(chip);
		lp5521_load_engine(chip);
		engine->mode = LP55XX_ENGINE_LOAD;
	} else if (!strncmp(buf, "disabled", 8)) {
		lp5521_stop_engine(chip);
		engine->mode = LP55XX_ENGINE_DISABLED;
	}

	mutex_unlock(&chip->lock);

	return len;
}
store_mode(1)
store_mode(2)
store_mode(3)

static ssize_t store_engine_load(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len, int nr)
{
	struct lp55xx_led *led = i2c_get_clientdata(to_i2c_client(dev));
	struct lp55xx_chip *chip = led->chip;
	int ret;

	mutex_lock(&chip->lock);

	chip->engine_idx = nr;
	lp5521_load_engine(chip);
	ret = lp5521_update_program_memory(chip, buf, len);

	mutex_unlock(&chip->lock);

	return ret;
}
store_load(1)
store_load(2)
store_load(3)

static ssize_t lp5521_selftest(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct lp55xx_led *led = i2c_get_clientdata(to_i2c_client(dev));
	struct lp55xx_chip *chip = led->chip;
	int ret;

	mutex_lock(&chip->lock);
	ret = lp5521_run_selftest(chip, buf);
	mutex_unlock(&chip->lock);

	return scnprintf(buf, PAGE_SIZE, "%s\n", ret ? "FAIL" : "OK");
}

/* device attributes */
static LP55XX_DEV_ATTR_RW(engine1_mode, show_engine1_mode, store_engine1_mode);
static LP55XX_DEV_ATTR_RW(engine2_mode, show_engine2_mode, store_engine2_mode);
static LP55XX_DEV_ATTR_RW(engine3_mode, show_engine3_mode, store_engine3_mode);
static LP55XX_DEV_ATTR_WO(engine1_load, store_engine1_load);
static LP55XX_DEV_ATTR_WO(engine2_load, store_engine2_load);
static LP55XX_DEV_ATTR_WO(engine3_load, store_engine3_load);
static LP55XX_DEV_ATTR_RO(selftest, lp5521_selftest);

static struct attribute *lp5521_attributes[] = {
	&dev_attr_engine1_mode.attr,
	&dev_attr_engine2_mode.attr,
	&dev_attr_engine3_mode.attr,
	&dev_attr_engine1_load.attr,
	&dev_attr_engine2_load.attr,
	&dev_attr_engine3_load.attr,
	&dev_attr_selftest.attr,
	NULL
};

static const struct attribute_group lp5521_group = {
	.attrs = lp5521_attributes,
};

/* Chip specific configurations */
static struct lp55xx_device_config lp5521_cfg = {
	.reset = {
		.addr = LP5521_REG_RESET,
		.val  = LP5521_RESET,
	},
	.enable = {
		.addr = LP5521_REG_ENABLE,
		.val  = LP5521_ENABLE_DEFAULT,
	},
	.max_channel  = LP5521_MAX_LEDS,
	.post_init_device   = lp5521_post_init_device,
	.brightness_fn      = lp5521_led_brightness,
	.set_led_current    = lp5521_set_led_current,
	.firmware_cb        = lp5521_firmware_loaded,
	.run_engine         = lp5521_run_engine,
	.dev_attr_group     = &lp5521_group,
};

static int lp5521_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret;
	struct lp55xx_chip *chip;
	struct lp55xx_led *led;
	struct lp55xx_platform_data *pdata = dev_get_platdata(&client->dev);
	struct device_node *np = client->dev.of_node;

	if (!pdata) {
		if (np) {
			pdata = lp55xx_of_populate_pdata(&client->dev, np);
			if (IS_ERR(pdata))
				return PTR_ERR(pdata);
		} else {
			dev_err(&client->dev, "no platform data\n");
			return -EINVAL;
		}
	}

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	led = devm_kzalloc(&client->dev,
			sizeof(*led) * pdata->num_channels, GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	chip->cl = client;
	chip->pdata = pdata;
	chip->cfg = &lp5521_cfg;

	mutex_init(&chip->lock);

	i2c_set_clientdata(client, led);

	ret = lp55xx_init_device(chip);
	if (ret)
		goto err_init;

	dev_info(&client->dev, "%s programmable led chip found\n", id->name);

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

static int lp5521_remove(struct i2c_client *client)
{
	struct lp55xx_led *led = i2c_get_clientdata(client);
	struct lp55xx_chip *chip = led->chip;

	lp5521_stop_all_engines(chip);
	lp55xx_unregister_sysfs(chip);
	lp55xx_unregister_leds(led, chip);
	lp55xx_deinit_device(chip);

	return 0;
}

static const struct i2c_device_id lp5521_id[] = {
	{ "lp5521", 0 }, /* Three channel chip */
	{ }
};
MODULE_DEVICE_TABLE(i2c, lp5521_id);

#ifdef CONFIG_OF
static const struct of_device_id of_lp5521_leds_match[] = {
	{ .compatible = "national,lp5521", },
	{},
};

MODULE_DEVICE_TABLE(of, of_lp5521_leds_match);
#endif
static struct i2c_driver lp5521_driver = {
	.driver = {
		.name	= "lp5521",
		.of_match_table = of_match_ptr(of_lp5521_leds_match),
	},
	.probe		= lp5521_probe,
	.remove		= lp5521_remove,
	.id_table	= lp5521_id,
};

module_i2c_driver(lp5521_driver);

MODULE_AUTHOR("Mathias Nyman, Yuri Zaporozhets, Samu Onkalo");
MODULE_AUTHOR("Milo Kim <milo.kim@ti.com>");
MODULE_DESCRIPTION("LP5521 LED engine");
MODULE_LICENSE("GPL v2");
