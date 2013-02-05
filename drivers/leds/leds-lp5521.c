/*
 * LP5521 LED chip driver.
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contact: Samu Onkalo <samu.p.onkalo@nokia.com>
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/leds.h>
#include <linux/leds-lp5521.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/platform_data/leds-lp55xx.h>
#include <linux/firmware.h>

#include "leds-lp55xx-common.h"

#define LP5521_PROGRAM_LENGTH		32	/* in bytes */

#define LP5521_MAX_LEDS			3	/* Maximum number of LEDs */
#define LP5521_MAX_ENGINES		3	/* Maximum number of engines */

#define LP5521_ENG_MASK_BASE		0x30	/* 00110000 */
#define LP5521_ENG_STATUS_MASK		0x07	/* 00000111 */

#define LP5521_CMD_LOAD			0x15	/* 00010101 */
#define LP5521_CMD_RUN			0x2a	/* 00101010 */
#define LP5521_CMD_DIRECT		0x3f	/* 00111111 */
#define LP5521_CMD_DISABLED		0x00	/* 00000000 */

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
#define LP5521_REG_R_CHANNEL_PC		0x09
#define LP5521_REG_G_CHANNEL_PC		0x0A
#define LP5521_REG_B_CHANNEL_PC		0x0B
#define LP5521_REG_STATUS		0x0C
#define LP5521_REG_RESET		0x0D
#define LP5521_REG_GPO			0x0E
#define LP5521_REG_R_PROG_MEM		0x10
#define LP5521_REG_G_PROG_MEM		0x30
#define LP5521_REG_B_PROG_MEM		0x50

#define LP5521_PROG_MEM_BASE		LP5521_REG_R_PROG_MEM
#define LP5521_PROG_MEM_SIZE		0x20

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

/* Status */
#define LP5521_EXT_CLK_USED		0x08

/* default R channel current register value */
#define LP5521_REG_R_CURR_DEFAULT	0xAF

/* Pattern Mode */
#define PATTERN_OFF	0

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

struct lp5521_led {
	int			id;
	u8			chan_nr;
	u8			led_current;
	u8			max_current;
	struct led_classdev	cdev;
	struct work_struct	brightness_work;
	u8			brightness;
};

struct lp5521_chip {
	struct lp5521_platform_data *pdata;
	struct mutex		lock; /* Serialize control */
	struct i2c_client	*client;
	struct lp5521_led	leds[LP5521_MAX_LEDS];
	u8			num_channels;
	u8			num_leds;
};

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

static inline int lp5521_write(struct i2c_client *client, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

static int lp5521_read(struct i2c_client *client, u8 reg, u8 *buf)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0)
		return ret;

	*buf = ret;
	return 0;
}

static void lp5521_load_engine(struct lp55xx_chip *chip)
{
	enum lp55xx_engine_index idx = chip->engine_idx;
	u8 mask[] = {
		[LP55XX_ENGINE_1] = LP5521_MODE_R_M,
		[LP55XX_ENGINE_2] = LP5521_MODE_G_M,
		[LP55XX_ENGINE_3] = LP5521_MODE_B_M,
	};

	u8 val[] = {
		[LP55XX_ENGINE_1] = LP5521_LOAD_R,
		[LP55XX_ENGINE_2] = LP5521_LOAD_G,
		[LP55XX_ENGINE_3] = LP5521_LOAD_B,
	};

	lp55xx_update_bits(chip, LP5521_REG_OP_MODE, mask[idx], val[idx]);

	lp5521_wait_opmode_done();
}

static void lp5521_stop_engine(struct lp55xx_chip *chip)
{
	lp55xx_write(chip, LP5521_REG_OP_MODE, 0);
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
	u8 addr[] = {
		[LP55XX_ENGINE_1] = LP5521_REG_R_PROG_MEM,
		[LP55XX_ENGINE_2] = LP5521_REG_G_PROG_MEM,
		[LP55XX_ENGINE_3] = LP5521_REG_B_PROG_MEM,
	};
	unsigned cmd;
	char c[3];
	int program_size;
	int nrchars;
	int offset = 0;
	int ret;
	int i;

	/* clear program memory before updating */
	for (i = 0; i < LP5521_PROGRAM_LENGTH; i++)
		lp55xx_write(chip, addr[idx] + i, 0);

	i = 0;
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

	program_size = i;
	for (i = 0; i < program_size; i++)
		lp55xx_write(chip, addr[idx] + i, pattern[i]);

	return 0;

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
	 * Program momery sequence
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

	val = chip->pdata->update_config ?
		: (LP5521_PWRSAVE_EN | LP5521_CP_MODE_AUTO | LP5521_R_TO_BATT);
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

static int lp5521_run_selftest(struct lp5521_chip *chip, char *buf)
{
	int ret;
	u8 status;

	ret = lp5521_read(chip->client, LP5521_REG_STATUS, &status);
	if (ret < 0)
		return ret;

	/* Check that ext clock is really in use if requested */
	if (chip->pdata && chip->pdata->clock_mode == LP5521_CLOCK_EXT)
		if  ((status & LP5521_EXT_CLK_USED) == 0)
			return -EIO;
	return 0;
}

static void lp5521_led_brightness_work(struct work_struct *work)
{
	struct lp55xx_led *led = container_of(work, struct lp55xx_led,
					      brightness_work);
	struct lp55xx_chip *chip = led->chip;

	mutex_lock(&chip->lock);
	lp55xx_write(chip, LP5521_REG_LED_PWM_BASE + led->chan_nr,
		led->brightness);
	mutex_unlock(&chip->lock);
}

static ssize_t lp5521_selftest(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lp5521_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->lock);
	ret = lp5521_run_selftest(chip, buf);
	mutex_unlock(&chip->lock);
	return sprintf(buf, "%s\n", ret ? "FAIL" : "OK");
}

static void lp5521_clear_program_memory(struct i2c_client *cl)
{
	int i;
	u8 rgb_mem[] = {
		LP5521_REG_R_PROG_MEM,
		LP5521_REG_G_PROG_MEM,
		LP5521_REG_B_PROG_MEM,
	};

	for (i = 0; i < ARRAY_SIZE(rgb_mem); i++) {
		lp5521_write(cl, rgb_mem[i], 0);
		lp5521_write(cl, rgb_mem[i] + 1, 0);
	}
}

static void lp5521_write_program_memory(struct i2c_client *cl,
				u8 base, u8 *rgb, int size)
{
	int i;

	if (!rgb || size <= 0)
		return;

	for (i = 0; i < size; i++)
		lp5521_write(cl, base + i, *(rgb + i));

	lp5521_write(cl, base + i, 0);
	lp5521_write(cl, base + i + 1, 0);
}

static inline struct lp5521_led_pattern *lp5521_get_pattern
					(struct lp5521_chip *chip, u8 offset)
{
	struct lp5521_led_pattern *ptn;
	ptn = chip->pdata->patterns + (offset - 1);
	return ptn;
}

static void lp5521_run_led_pattern(int mode, struct lp5521_chip *chip)
{
	struct lp5521_led_pattern *ptn;
	struct i2c_client *cl = chip->client;
	int num_patterns = chip->pdata->num_patterns;

	if (mode > num_patterns || !(chip->pdata->patterns))
		return;

	if (mode == PATTERN_OFF) {
		lp5521_write(cl, LP5521_REG_ENABLE, LP5521_ENABLE_DEFAULT);
		usleep_range(1000, 2000);
		lp5521_write(cl, LP5521_REG_OP_MODE, LP5521_CMD_DIRECT);
	} else {
		ptn = lp5521_get_pattern(chip, mode);
		if (!ptn)
			return;

		lp5521_write(cl, LP5521_REG_OP_MODE, LP5521_CMD_LOAD);
		usleep_range(1000, 2000);

		lp5521_clear_program_memory(cl);

		lp5521_write_program_memory(cl, LP5521_REG_R_PROG_MEM,
					ptn->r, ptn->size_r);
		lp5521_write_program_memory(cl, LP5521_REG_G_PROG_MEM,
					ptn->g, ptn->size_g);
		lp5521_write_program_memory(cl, LP5521_REG_B_PROG_MEM,
					ptn->b, ptn->size_b);

		lp5521_write(cl, LP5521_REG_OP_MODE, LP5521_CMD_RUN);
		usleep_range(1000, 2000);
		lp5521_write(cl, LP5521_REG_ENABLE, LP5521_ENABLE_RUN_PROGRAM);
	}
}

/* device attributes */
static DEVICE_ATTR(selftest, S_IRUGO, lp5521_selftest, NULL);

static struct attribute *lp5521_attributes[] = {
	&dev_attr_selftest.attr,
	NULL
};

static const struct attribute_group lp5521_group = {
	.attrs = lp5521_attributes,
};

static void lp5521_unregister_sysfs(struct i2c_client *client)
{
	struct device *dev = &client->dev;

	sysfs_remove_group(&dev->kobj, &lp5521_group);
}

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
	.brightness_work_fn = lp5521_led_brightness_work,
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
	struct lp55xx_platform_data *pdata = client->dev.platform_data;

	if (!pdata) {
		dev_err(&client->dev, "no platform data\n");
		return -EINVAL;
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
	struct lp5521_chip *old_chip = i2c_get_clientdata(client);
	struct lp55xx_led *led = i2c_get_clientdata(client);
	struct lp55xx_chip *chip = led->chip;

	lp5521_run_led_pattern(PATTERN_OFF, old_chip);
	lp5521_unregister_sysfs(client);

	lp55xx_unregister_leds(led, chip);
	lp55xx_deinit_device(chip);

	return 0;
}

static const struct i2c_device_id lp5521_id[] = {
	{ "lp5521", 0 }, /* Three channel chip */
	{ }
};
MODULE_DEVICE_TABLE(i2c, lp5521_id);

static struct i2c_driver lp5521_driver = {
	.driver = {
		.name	= "lp5521",
	},
	.probe		= lp5521_probe,
	.remove		= lp5521_remove,
	.id_table	= lp5521_id,
};

module_i2c_driver(lp5521_driver);

MODULE_AUTHOR("Mathias Nyman, Yuri Zaporozhets, Samu Onkalo");
MODULE_DESCRIPTION("LP5521 LED engine");
MODULE_LICENSE("GPL v2");
