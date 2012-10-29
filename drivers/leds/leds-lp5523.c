/*
 * lp5523.c - LP5523 LED Driver
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
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
#include <linux/leds-lp5523.h>
#include <linux/workqueue.h>
#include <linux/slab.h>

#define LP5523_REG_ENABLE		0x00
#define LP5523_REG_OP_MODE		0x01
#define LP5523_REG_RATIOMETRIC_MSB	0x02
#define LP5523_REG_RATIOMETRIC_LSB	0x03
#define LP5523_REG_ENABLE_LEDS_MSB	0x04
#define LP5523_REG_ENABLE_LEDS_LSB	0x05
#define LP5523_REG_LED_CNTRL_BASE	0x06
#define LP5523_REG_LED_PWM_BASE		0x16
#define LP5523_REG_LED_CURRENT_BASE	0x26
#define LP5523_REG_CONFIG		0x36
#define LP5523_REG_CHANNEL1_PC		0x37
#define LP5523_REG_CHANNEL2_PC		0x38
#define LP5523_REG_CHANNEL3_PC		0x39
#define LP5523_REG_STATUS		0x3a
#define LP5523_REG_GPO			0x3b
#define LP5523_REG_VARIABLE		0x3c
#define LP5523_REG_RESET		0x3d
#define LP5523_REG_TEMP_CTRL		0x3e
#define LP5523_REG_TEMP_READ		0x3f
#define LP5523_REG_TEMP_WRITE		0x40
#define LP5523_REG_LED_TEST_CTRL	0x41
#define LP5523_REG_LED_TEST_ADC		0x42
#define LP5523_REG_ENG1_VARIABLE	0x45
#define LP5523_REG_ENG2_VARIABLE	0x46
#define LP5523_REG_ENG3_VARIABLE	0x47
#define LP5523_REG_MASTER_FADER1	0x48
#define LP5523_REG_MASTER_FADER2	0x49
#define LP5523_REG_MASTER_FADER3	0x4a
#define LP5523_REG_CH1_PROG_START	0x4c
#define LP5523_REG_CH2_PROG_START	0x4d
#define LP5523_REG_CH3_PROG_START	0x4e
#define LP5523_REG_PROG_PAGE_SEL	0x4f
#define LP5523_REG_PROG_MEM		0x50

#define LP5523_CMD_LOAD			0x15 /* 00010101 */
#define LP5523_CMD_RUN			0x2a /* 00101010 */
#define LP5523_CMD_DISABLED		0x00 /* 00000000 */

#define LP5523_ENABLE			0x40
#define LP5523_AUTO_INC			0x40
#define LP5523_PWR_SAVE			0x20
#define LP5523_PWM_PWR_SAVE		0x04
#define LP5523_CP_1			0x08
#define LP5523_CP_1_5			0x10
#define LP5523_CP_AUTO			0x18
#define LP5523_INT_CLK			0x01
#define LP5523_AUTO_CLK			0x02
#define LP5523_EN_LEDTEST		0x80
#define LP5523_LEDTEST_DONE		0x80

#define LP5523_DEFAULT_CURRENT		50 /* microAmps */
#define LP5523_PROGRAM_LENGTH		32 /* in bytes */
#define LP5523_PROGRAM_PAGES		6
#define LP5523_ADC_SHORTCIRC_LIM	80

#define LP5523_LEDS			9
#define LP5523_ENGINES			3

#define LP5523_ENG_MASK_BASE		0x30 /* 00110000 */

#define LP5523_ENG_STATUS_MASK          0x07 /* 00000111 */

#define LP5523_IRQ_FLAGS                IRQF_TRIGGER_FALLING

#define LP5523_EXT_CLK_USED		0x08

#define LED_ACTIVE(mux, led)		(!!(mux & (0x0001 << led)))
#define SHIFT_MASK(id)			(((id) - 1) * 2)

enum lp5523_chip_id {
	LP5523,
	LP55231,
};

struct lp5523_engine {
	int		id;
	u8		mode;
	u8		prog_page;
	u8		mux_page;
	u16		led_mux;
	u8		engine_mask;
};

struct lp5523_led {
	int			id;
	u8			chan_nr;
	u8			led_current;
	u8			max_current;
	struct led_classdev     cdev;
	struct work_struct	brightness_work;
	u8			brightness;
};

struct lp5523_chip {
	struct mutex		lock; /* Serialize control */
	struct i2c_client	*client;
	struct lp5523_engine	engines[LP5523_ENGINES];
	struct lp5523_led	leds[LP5523_LEDS];
	struct lp5523_platform_data *pdata;
	u8			num_channels;
	u8			num_leds;
};

static inline struct lp5523_led *cdev_to_led(struct led_classdev *cdev)
{
	return container_of(cdev, struct lp5523_led, cdev);
}

static inline struct lp5523_chip *engine_to_lp5523(struct lp5523_engine *engine)
{
	return container_of(engine, struct lp5523_chip,
			    engines[engine->id - 1]);
}

static inline struct lp5523_chip *led_to_lp5523(struct lp5523_led *led)
{
	return container_of(led, struct lp5523_chip,
			    leds[led->id]);
}

static void lp5523_set_mode(struct lp5523_engine *engine, u8 mode);
static int lp5523_set_engine_mode(struct lp5523_engine *engine, u8 mode);
static int lp5523_load_program(struct lp5523_engine *engine, const u8 *pattern);

static void lp5523_led_brightness_work(struct work_struct *work);

static int lp5523_write(struct i2c_client *client, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

static int lp5523_read(struct i2c_client *client, u8 reg, u8 *buf)
{
	s32 ret = i2c_smbus_read_byte_data(client, reg);

	if (ret < 0)
		return -EIO;

	*buf = ret;
	return 0;
}

static int lp5523_detect(struct i2c_client *client)
{
	int ret;
	u8 buf;

	ret = lp5523_write(client, LP5523_REG_ENABLE, LP5523_ENABLE);
	if (ret)
		return ret;
	ret = lp5523_read(client, LP5523_REG_ENABLE, &buf);
	if (ret)
		return ret;
	if (buf == 0x40)
		return 0;
	else
		return -ENODEV;
}

static int lp5523_configure(struct i2c_client *client)
{
	struct lp5523_chip *chip = i2c_get_clientdata(client);
	int ret = 0;
	u8 status;

	/* one pattern per engine setting led mux start and stop addresses */
	static const u8 pattern[][LP5523_PROGRAM_LENGTH] =  {
		{ 0x9c, 0x30, 0x9c, 0xb0, 0x9d, 0x80, 0xd8, 0x00, 0},
		{ 0x9c, 0x40, 0x9c, 0xc0, 0x9d, 0x80, 0xd8, 0x00, 0},
		{ 0x9c, 0x50, 0x9c, 0xd0, 0x9d, 0x80, 0xd8, 0x00, 0},
	};

	ret |= lp5523_write(client, LP5523_REG_ENABLE, LP5523_ENABLE);
	/* Chip startup time is 500 us, 1 - 2 ms gives some margin */
	usleep_range(1000, 2000);

	ret |= lp5523_write(client, LP5523_REG_CONFIG,
			    LP5523_AUTO_INC | LP5523_PWR_SAVE |
			    LP5523_CP_AUTO | LP5523_AUTO_CLK |
			    LP5523_PWM_PWR_SAVE);

	/* turn on all leds */
	ret |= lp5523_write(client, LP5523_REG_ENABLE_LEDS_MSB, 0x01);
	ret |= lp5523_write(client, LP5523_REG_ENABLE_LEDS_LSB, 0xff);

	/* hardcode 32 bytes of memory for each engine from program memory */
	ret |= lp5523_write(client, LP5523_REG_CH1_PROG_START, 0x00);
	ret |= lp5523_write(client, LP5523_REG_CH2_PROG_START, 0x10);
	ret |= lp5523_write(client, LP5523_REG_CH3_PROG_START, 0x20);

	/* write led mux address space for each channel */
	ret |= lp5523_load_program(&chip->engines[0], pattern[0]);
	ret |= lp5523_load_program(&chip->engines[1], pattern[1]);
	ret |= lp5523_load_program(&chip->engines[2], pattern[2]);

	if (ret) {
		dev_err(&client->dev, "could not load mux programs\n");
		return -1;
	}

	/* set all engines exec state and mode to run 00101010 */
	ret |= lp5523_write(client, LP5523_REG_ENABLE,
			    (LP5523_CMD_RUN | LP5523_ENABLE));

	ret |= lp5523_write(client, LP5523_REG_OP_MODE, LP5523_CMD_RUN);

	if (ret) {
		dev_err(&client->dev, "could not start mux programs\n");
		return -1;
	}

	/* Let the programs run for couple of ms and check the engine status */
	usleep_range(3000, 6000);
	ret = lp5523_read(client, LP5523_REG_STATUS, &status);
	if (ret < 0)
		return ret;

	status &= LP5523_ENG_STATUS_MASK;

	if (status == LP5523_ENG_STATUS_MASK) {
		dev_dbg(&client->dev, "all engines configured\n");
	} else {
		dev_info(&client->dev, "status == %x\n", status);
		dev_err(&client->dev, "cound not configure LED engine\n");
		return -1;
	}

	dev_info(&client->dev, "disabling engines\n");

	ret |= lp5523_write(client, LP5523_REG_OP_MODE, LP5523_CMD_DISABLED);

	return ret;
}

static int lp5523_set_engine_mode(struct lp5523_engine *engine, u8 mode)
{
	struct lp5523_chip *chip = engine_to_lp5523(engine);
	struct i2c_client *client = chip->client;
	int ret;
	u8 engine_state;

	ret = lp5523_read(client, LP5523_REG_OP_MODE, &engine_state);
	if (ret)
		goto fail;

	engine_state &= ~(engine->engine_mask);

	/* set mode only for this engine */
	mode &= engine->engine_mask;

	engine_state |= mode;

	ret |= lp5523_write(client, LP5523_REG_OP_MODE, engine_state);
fail:
	return ret;
}

static int lp5523_load_mux(struct lp5523_engine *engine, u16 mux)
{
	struct lp5523_chip *chip = engine_to_lp5523(engine);
	struct i2c_client *client = chip->client;
	int ret = 0;

	ret |= lp5523_set_engine_mode(engine, LP5523_CMD_LOAD);

	ret |= lp5523_write(client, LP5523_REG_PROG_PAGE_SEL, engine->mux_page);
	ret |= lp5523_write(client, LP5523_REG_PROG_MEM,
			    (u8)(mux >> 8));
	ret |= lp5523_write(client, LP5523_REG_PROG_MEM + 1, (u8)(mux));
	engine->led_mux = mux;

	return ret;
}

static int lp5523_load_program(struct lp5523_engine *engine, const u8 *pattern)
{
	struct lp5523_chip *chip = engine_to_lp5523(engine);
	struct i2c_client *client = chip->client;

	int ret = 0;

	ret |= lp5523_set_engine_mode(engine, LP5523_CMD_LOAD);

	ret |= lp5523_write(client, LP5523_REG_PROG_PAGE_SEL,
			    engine->prog_page);
	ret |= i2c_smbus_write_i2c_block_data(client, LP5523_REG_PROG_MEM,
					      LP5523_PROGRAM_LENGTH, pattern);

	return ret;
}

static int lp5523_run_program(struct lp5523_engine *engine)
{
	struct lp5523_chip *chip = engine_to_lp5523(engine);
	struct i2c_client *client = chip->client;
	int ret;

	ret = lp5523_write(client, LP5523_REG_ENABLE,
					LP5523_CMD_RUN | LP5523_ENABLE);
	if (ret)
		goto fail;

	ret = lp5523_set_engine_mode(engine, LP5523_CMD_RUN);
fail:
	return ret;
}

static int lp5523_mux_parse(const char *buf, u16 *mux, size_t len)
{
	int i;
	u16 tmp_mux = 0;

	len = min_t(int, len, LP5523_LEDS);
	for (i = 0; i < len; i++) {
		switch (buf[i]) {
		case '1':
			tmp_mux |= (1 << i);
			break;
		case '0':
			break;
		case '\n':
			i = len;
			break;
		default:
			return -1;
		}
	}
	*mux = tmp_mux;

	return 0;
}

static void lp5523_mux_to_array(u16 led_mux, char *array)
{
	int i, pos = 0;
	for (i = 0; i < LP5523_LEDS; i++)
		pos += sprintf(array + pos, "%x", LED_ACTIVE(led_mux, i));

	array[pos] = '\0';
}

/*--------------------------------------------------------------*/
/*			Sysfs interface				*/
/*--------------------------------------------------------------*/

static ssize_t show_engine_leds(struct device *dev,
			    struct device_attribute *attr,
			    char *buf, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lp5523_chip *chip = i2c_get_clientdata(client);
	char mux[LP5523_LEDS + 1];

	lp5523_mux_to_array(chip->engines[nr - 1].led_mux, mux);

	return sprintf(buf, "%s\n", mux);
}

#define show_leds(nr)							\
static ssize_t show_engine##nr##_leds(struct device *dev,		\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	return show_engine_leds(dev, attr, buf, nr);			\
}
show_leds(1)
show_leds(2)
show_leds(3)

static ssize_t store_engine_leds(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lp5523_chip *chip = i2c_get_clientdata(client);
	u16 mux = 0;
	ssize_t ret;

	if (lp5523_mux_parse(buf, &mux, len))
		return -EINVAL;

	mutex_lock(&chip->lock);
	ret = -EINVAL;
	if (chip->engines[nr - 1].mode != LP5523_CMD_LOAD)
		goto leave;

	if (lp5523_load_mux(&chip->engines[nr - 1], mux))
		goto leave;

	ret = len;
leave:
	mutex_unlock(&chip->lock);
	return ret;
}

#define store_leds(nr)						\
static ssize_t store_engine##nr##_leds(struct device *dev,	\
			     struct device_attribute *attr,	\
			     const char *buf, size_t len)	\
{								\
	return store_engine_leds(dev, attr, buf, len, nr);	\
}
store_leds(1)
store_leds(2)
store_leds(3)

static ssize_t lp5523_selftest(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lp5523_chip *chip = i2c_get_clientdata(client);
	int i, ret, pos = 0;
	int led = 0;
	u8 status, adc, vdd;

	mutex_lock(&chip->lock);

	ret = lp5523_read(chip->client, LP5523_REG_STATUS, &status);
	if (ret < 0)
		goto fail;

	/* Check that ext clock is really in use if requested */
	if ((chip->pdata) && (chip->pdata->clock_mode == LP5523_CLOCK_EXT))
		if  ((status & LP5523_EXT_CLK_USED) == 0)
			goto fail;

	/* Measure VDD (i.e. VBAT) first (channel 16 corresponds to VDD) */
	lp5523_write(chip->client, LP5523_REG_LED_TEST_CTRL,
				    LP5523_EN_LEDTEST | 16);
	usleep_range(3000, 6000); /* ADC conversion time is typically 2.7 ms */
	ret = lp5523_read(chip->client, LP5523_REG_STATUS, &status);
	if (ret < 0)
		goto fail;

	if (!(status & LP5523_LEDTEST_DONE))
		usleep_range(3000, 6000); /* Was not ready. Wait little bit */

	ret = lp5523_read(chip->client, LP5523_REG_LED_TEST_ADC, &vdd);
	if (ret < 0)
		goto fail;

	vdd--;	/* There may be some fluctuation in measurement */

	for (i = 0; i < LP5523_LEDS; i++) {
		/* Skip non-existing channels */
		if (chip->pdata->led_config[i].led_current == 0)
			continue;

		/* Set default current */
		lp5523_write(chip->client,
			LP5523_REG_LED_CURRENT_BASE + i,
			chip->pdata->led_config[i].led_current);

		lp5523_write(chip->client, LP5523_REG_LED_PWM_BASE + i, 0xff);
		/* let current stabilize 2 - 4ms before measurements start */
		usleep_range(2000, 4000);
		lp5523_write(chip->client,
			     LP5523_REG_LED_TEST_CTRL,
			     LP5523_EN_LEDTEST | i);
		/* ADC conversion time is 2.7 ms typically */
		usleep_range(3000, 6000);
		ret = lp5523_read(chip->client, LP5523_REG_STATUS, &status);
		if (ret < 0)
			goto fail;

		if (!(status & LP5523_LEDTEST_DONE))
			usleep_range(3000, 6000);/* Was not ready. Wait. */
		ret = lp5523_read(chip->client, LP5523_REG_LED_TEST_ADC, &adc);
		if (ret < 0)
			goto fail;

		if (adc >= vdd || adc < LP5523_ADC_SHORTCIRC_LIM)
			pos += sprintf(buf + pos, "LED %d FAIL\n", i);

		lp5523_write(chip->client, LP5523_REG_LED_PWM_BASE + i, 0x00);

		/* Restore current */
		lp5523_write(chip->client,
			LP5523_REG_LED_CURRENT_BASE + i,
			chip->leds[led].led_current);
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

static void lp5523_set_brightness(struct led_classdev *cdev,
			     enum led_brightness brightness)
{
	struct lp5523_led *led = cdev_to_led(cdev);

	led->brightness = (u8)brightness;

	schedule_work(&led->brightness_work);
}

static void lp5523_led_brightness_work(struct work_struct *work)
{
	struct lp5523_led *led = container_of(work,
					      struct lp5523_led,
					      brightness_work);
	struct lp5523_chip *chip = led_to_lp5523(led);
	struct i2c_client *client = chip->client;

	mutex_lock(&chip->lock);

	lp5523_write(client, LP5523_REG_LED_PWM_BASE + led->chan_nr,
		     led->brightness);

	mutex_unlock(&chip->lock);
}

static int lp5523_do_store_load(struct lp5523_engine *engine,
				const char *buf, size_t len)
{
	struct lp5523_chip *chip = engine_to_lp5523(engine);
	struct i2c_client *client = chip->client;
	int  ret, nrchars, offset = 0, i = 0;
	char c[3];
	unsigned cmd;
	u8 pattern[LP5523_PROGRAM_LENGTH] = {0};

	if (engine->mode != LP5523_CMD_LOAD)
		return -EINVAL;

	while ((offset < len - 1) && (i < LP5523_PROGRAM_LENGTH)) {
		/* separate sscanfs because length is working only for %s */
		ret = sscanf(buf + offset, "%2s%n ", c, &nrchars);
		ret = sscanf(c, "%2x", &cmd);
		if (ret != 1)
			goto fail;
		pattern[i] = (u8)cmd;

		offset += nrchars;
		i++;
	}

	/* Each instruction is 16bit long. Check that length is even */
	if (i % 2)
		goto fail;

	mutex_lock(&chip->lock);
	ret = lp5523_load_program(engine, pattern);
	mutex_unlock(&chip->lock);

	if (ret) {
		dev_err(&client->dev, "failed loading pattern\n");
		return ret;
	}

	return len;
fail:
	dev_err(&client->dev, "wrong pattern format\n");
	return -EINVAL;
}

static ssize_t store_engine_load(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lp5523_chip *chip = i2c_get_clientdata(client);
	return lp5523_do_store_load(&chip->engines[nr - 1], buf, len);
}

#define store_load(nr)							\
static ssize_t store_engine##nr##_load(struct device *dev,		\
				     struct device_attribute *attr,	\
				     const char *buf, size_t len)	\
{									\
	return store_engine_load(dev, attr, buf, len, nr);		\
}
store_load(1)
store_load(2)
store_load(3)

static ssize_t show_engine_mode(struct device *dev,
				struct device_attribute *attr,
				char *buf, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lp5523_chip *chip = i2c_get_clientdata(client);
	switch (chip->engines[nr - 1].mode) {
	case LP5523_CMD_RUN:
		return sprintf(buf, "run\n");
	case LP5523_CMD_LOAD:
		return sprintf(buf, "load\n");
	case LP5523_CMD_DISABLED:
		return sprintf(buf, "disabled\n");
	default:
		return sprintf(buf, "disabled\n");
	}
}

#define show_mode(nr)							\
static ssize_t show_engine##nr##_mode(struct device *dev,		\
				    struct device_attribute *attr,	\
				    char *buf)				\
{									\
	return show_engine_mode(dev, attr, buf, nr);			\
}
show_mode(1)
show_mode(2)
show_mode(3)

static ssize_t store_engine_mode(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t len, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lp5523_chip *chip = i2c_get_clientdata(client);
	struct lp5523_engine *engine = &chip->engines[nr - 1];
	mutex_lock(&chip->lock);

	if (!strncmp(buf, "run", 3))
		lp5523_set_mode(engine, LP5523_CMD_RUN);
	else if (!strncmp(buf, "load", 4))
		lp5523_set_mode(engine, LP5523_CMD_LOAD);
	else if (!strncmp(buf, "disabled", 8))
		lp5523_set_mode(engine, LP5523_CMD_DISABLED);

	mutex_unlock(&chip->lock);
	return len;
}

#define store_mode(nr)							\
static ssize_t store_engine##nr##_mode(struct device *dev,		\
				     struct device_attribute *attr,	\
				     const char *buf, size_t len)	\
{									\
	return store_engine_mode(dev, attr, buf, len, nr);		\
}
store_mode(1)
store_mode(2)
store_mode(3)

static ssize_t show_max_current(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct lp5523_led *led = cdev_to_led(led_cdev);

	return sprintf(buf, "%d\n", led->max_current);
}

static ssize_t show_current(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct lp5523_led *led = cdev_to_led(led_cdev);

	return sprintf(buf, "%d\n", led->led_current);
}

static ssize_t store_current(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct lp5523_led *led = cdev_to_led(led_cdev);
	struct lp5523_chip *chip = led_to_lp5523(led);
	ssize_t ret;
	unsigned long curr;

	if (strict_strtoul(buf, 0, &curr))
		return -EINVAL;

	if (curr > led->max_current)
		return -EINVAL;

	mutex_lock(&chip->lock);
	ret = lp5523_write(chip->client,
			LP5523_REG_LED_CURRENT_BASE + led->chan_nr,
			(u8)curr);
	mutex_unlock(&chip->lock);

	if (ret < 0)
		return ret;

	led->led_current = (u8)curr;

	return len;
}

/* led class device attributes */
static DEVICE_ATTR(led_current, S_IRUGO | S_IWUSR, show_current, store_current);
static DEVICE_ATTR(max_current, S_IRUGO , show_max_current, NULL);

static struct attribute *lp5523_led_attributes[] = {
	&dev_attr_led_current.attr,
	&dev_attr_max_current.attr,
	NULL,
};

static struct attribute_group lp5523_led_attribute_group = {
	.attrs = lp5523_led_attributes
};

/* device attributes */
static DEVICE_ATTR(engine1_mode, S_IRUGO | S_IWUSR,
		   show_engine1_mode, store_engine1_mode);
static DEVICE_ATTR(engine2_mode, S_IRUGO | S_IWUSR,
		   show_engine2_mode, store_engine2_mode);
static DEVICE_ATTR(engine3_mode, S_IRUGO | S_IWUSR,
		   show_engine3_mode, store_engine3_mode);
static DEVICE_ATTR(engine1_leds, S_IRUGO | S_IWUSR,
		   show_engine1_leds, store_engine1_leds);
static DEVICE_ATTR(engine2_leds, S_IRUGO | S_IWUSR,
		   show_engine2_leds, store_engine2_leds);
static DEVICE_ATTR(engine3_leds, S_IRUGO | S_IWUSR,
		   show_engine3_leds, store_engine3_leds);
static DEVICE_ATTR(engine1_load, S_IWUSR, NULL, store_engine1_load);
static DEVICE_ATTR(engine2_load, S_IWUSR, NULL, store_engine2_load);
static DEVICE_ATTR(engine3_load, S_IWUSR, NULL, store_engine3_load);
static DEVICE_ATTR(selftest, S_IRUGO, lp5523_selftest, NULL);

static struct attribute *lp5523_attributes[] = {
	&dev_attr_engine1_mode.attr,
	&dev_attr_engine2_mode.attr,
	&dev_attr_engine3_mode.attr,
	&dev_attr_selftest.attr,
	&dev_attr_engine1_load.attr,
	&dev_attr_engine1_leds.attr,
	&dev_attr_engine2_load.attr,
	&dev_attr_engine2_leds.attr,
	&dev_attr_engine3_load.attr,
	&dev_attr_engine3_leds.attr,
	NULL,
};

static const struct attribute_group lp5523_group = {
	.attrs = lp5523_attributes,
};

static int lp5523_register_sysfs(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	int ret;

	ret = sysfs_create_group(&dev->kobj, &lp5523_group);
	if (ret < 0)
		return ret;

	return 0;
}

static void lp5523_unregister_sysfs(struct i2c_client *client)
{
	struct lp5523_chip *chip = i2c_get_clientdata(client);
	struct device *dev = &client->dev;
	int i;

	sysfs_remove_group(&dev->kobj, &lp5523_group);

	for (i = 0; i < chip->num_leds; i++)
		sysfs_remove_group(&chip->leds[i].cdev.dev->kobj,
				&lp5523_led_attribute_group);
}

/*--------------------------------------------------------------*/
/*			Set chip operating mode			*/
/*--------------------------------------------------------------*/
static void lp5523_set_mode(struct lp5523_engine *engine, u8 mode)
{
	/* if in that mode already do nothing, except for run */
	if (mode == engine->mode && mode != LP5523_CMD_RUN)
		return;

	switch (mode) {
	case LP5523_CMD_RUN:
		lp5523_run_program(engine);
		break;
	case LP5523_CMD_LOAD:
		lp5523_set_engine_mode(engine, LP5523_CMD_DISABLED);
		lp5523_set_engine_mode(engine, LP5523_CMD_LOAD);
		break;
	case LP5523_CMD_DISABLED:
		lp5523_set_engine_mode(engine, LP5523_CMD_DISABLED);
		break;
	default:
		return;
	}

	engine->mode = mode;
}

/*--------------------------------------------------------------*/
/*			Probe, Attach, Remove			*/
/*--------------------------------------------------------------*/
static int __init lp5523_init_engine(struct lp5523_engine *engine, int id)
{
	if (id < 1 || id > LP5523_ENGINES)
		return -1;
	engine->id = id;
	engine->engine_mask = LP5523_ENG_MASK_BASE >> SHIFT_MASK(id);
	engine->prog_page = id - 1;
	engine->mux_page = id + 2;

	return 0;
}

static int __devinit lp5523_init_led(struct lp5523_led *led, struct device *dev,
			   int chan, struct lp5523_platform_data *pdata,
			   const char *chip_name)
{
	char name[32];
	int res;

	if (chan >= LP5523_LEDS)
		return -EINVAL;

	if (pdata->led_config[chan].led_current) {
		led->led_current = pdata->led_config[chan].led_current;
		led->max_current = pdata->led_config[chan].max_current;
		led->chan_nr = pdata->led_config[chan].chan_nr;

		if (led->chan_nr >= LP5523_LEDS) {
			dev_err(dev, "Use channel numbers between 0 and %d\n",
				LP5523_LEDS - 1);
			return -EINVAL;
		}

		if (pdata->led_config[chan].name) {
			led->cdev.name = pdata->led_config[chan].name;
		} else {
			snprintf(name, sizeof(name), "%s:channel%d",
				pdata->label ? : chip_name, chan);
			led->cdev.name = name;
		}

		led->cdev.brightness_set = lp5523_set_brightness;
		res = led_classdev_register(dev, &led->cdev);
		if (res < 0) {
			dev_err(dev, "couldn't register led on channel %d\n",
				chan);
			return res;
		}
		res = sysfs_create_group(&led->cdev.dev->kobj,
				&lp5523_led_attribute_group);
		if (res < 0) {
			dev_err(dev, "couldn't register current attribute\n");
			led_classdev_unregister(&led->cdev);
			return res;
		}
	} else {
		led->led_current = 0;
	}
	return 0;
}

static int __devinit lp5523_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct lp5523_chip		*chip;
	struct lp5523_platform_data	*pdata;
	int ret, i, led;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	i2c_set_clientdata(client, chip);
	chip->client = client;

	pdata = client->dev.platform_data;

	if (!pdata) {
		dev_err(&client->dev, "no platform data\n");
		return -EINVAL;
	}

	mutex_init(&chip->lock);

	chip->pdata   = pdata;

	if (pdata->setup_resources) {
		ret = pdata->setup_resources();
		if (ret < 0)
			return ret;
	}

	if (pdata->enable) {
		pdata->enable(0);
		usleep_range(1000, 2000); /* Keep enable down at least 1ms */
		pdata->enable(1);
		usleep_range(1000, 2000); /* 500us abs min. */
	}

	lp5523_write(client, LP5523_REG_RESET, 0xff);
	usleep_range(10000, 20000); /*
				     * Exact value is not available. 10 - 20ms
				     * appears to be enough for reset.
				     */
	ret = lp5523_detect(client);
	if (ret)
		goto fail1;

	dev_info(&client->dev, "%s Programmable led chip found\n", id->name);

	/* Initialize engines */
	for (i = 0; i < ARRAY_SIZE(chip->engines); i++) {
		ret = lp5523_init_engine(&chip->engines[i], i + 1);
		if (ret) {
			dev_err(&client->dev, "error initializing engine\n");
			goto fail1;
		}
	}
	ret = lp5523_configure(client);
	if (ret < 0) {
		dev_err(&client->dev, "error configuring chip\n");
		goto fail1;
	}

	/* Initialize leds */
	chip->num_channels = pdata->num_channels;
	chip->num_leds = 0;
	led = 0;
	for (i = 0; i < pdata->num_channels; i++) {
		/* Do not initialize channels that are not connected */
		if (pdata->led_config[i].led_current == 0)
			continue;

		INIT_WORK(&chip->leds[led].brightness_work,
			lp5523_led_brightness_work);

		ret = lp5523_init_led(&chip->leds[led], &client->dev, i, pdata,
				id->name);
		if (ret) {
			dev_err(&client->dev, "error initializing leds\n");
			goto fail2;
		}
		chip->num_leds++;

		chip->leds[led].id = led;
		/* Set LED current */
		lp5523_write(client,
			  LP5523_REG_LED_CURRENT_BASE + chip->leds[led].chan_nr,
			  chip->leds[led].led_current);

		led++;
	}

	ret = lp5523_register_sysfs(client);
	if (ret) {
		dev_err(&client->dev, "registering sysfs failed\n");
		goto fail2;
	}
	return ret;
fail2:
	for (i = 0; i < chip->num_leds; i++) {
		led_classdev_unregister(&chip->leds[i].cdev);
		flush_work(&chip->leds[i].brightness_work);
	}
fail1:
	if (pdata->enable)
		pdata->enable(0);
	if (pdata->release_resources)
		pdata->release_resources();
	return ret;
}

static int lp5523_remove(struct i2c_client *client)
{
	struct lp5523_chip *chip = i2c_get_clientdata(client);
	int i;

	/* Disable engine mode */
	lp5523_write(client, LP5523_REG_OP_MODE, LP5523_CMD_DISABLED);

	lp5523_unregister_sysfs(client);

	for (i = 0; i < chip->num_leds; i++) {
		led_classdev_unregister(&chip->leds[i].cdev);
		flush_work(&chip->leds[i].brightness_work);
	}

	if (chip->pdata->enable)
		chip->pdata->enable(0);
	if (chip->pdata->release_resources)
		chip->pdata->release_resources();
	return 0;
}

static const struct i2c_device_id lp5523_id[] = {
	{ "lp5523",  LP5523 },
	{ "lp55231", LP55231 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, lp5523_id);

static struct i2c_driver lp5523_driver = {
	.driver = {
		.name	= "lp5523x",
	},
	.probe		= lp5523_probe,
	.remove		= lp5523_remove,
	.id_table	= lp5523_id,
};

module_i2c_driver(lp5523_driver);

MODULE_AUTHOR("Mathias Nyman <mathias.nyman@nokia.com>");
MODULE_DESCRIPTION("LP5523 LED engine");
MODULE_LICENSE("GPL");
