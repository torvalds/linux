/*
 * LED flash driver for LM3554
 *
 * Copyright (c) 2010-2012 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/slab.h>

#include "../include/media/lm3554.h"
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <linux/acpi.h>
#include <linux/gpio/consumer.h>
#include "../include/linux/atomisp_gmin_platform.h"
#include "../include/linux/atomisp.h"

/* Registers */

#define LM3554_TORCH_BRIGHTNESS_REG	0xA0
#define LM3554_TORCH_MODE_SHIFT		0
#define LM3554_TORCH_CURRENT_SHIFT	3
#define LM3554_INDICATOR_CURRENT_SHIFT	6

#define LM3554_FLASH_BRIGHTNESS_REG	0xB0
#define LM3554_FLASH_MODE_SHIFT		0
#define LM3554_FLASH_CURRENT_SHIFT	3
#define LM3554_STROBE_SENSITIVITY_SHIFT	7

#define LM3554_FLASH_DURATION_REG	0xC0
#define LM3554_FLASH_TIMEOUT_SHIFT	0
#define LM3554_CURRENT_LIMIT_SHIFT	5

#define LM3554_FLAGS_REG		0xD0
#define LM3554_FLAG_TIMEOUT		(1 << 0)
#define LM3554_FLAG_THERMAL_SHUTDOWN	(1 << 1)
#define LM3554_FLAG_LED_FAULT		(1 << 2)
#define LM3554_FLAG_TX1_INTERRUPT	(1 << 3)
#define LM3554_FLAG_TX2_INTERRUPT	(1 << 4)
#define LM3554_FLAG_LED_THERMAL_FAULT	(1 << 5)
#define LM3554_FLAG_UNUSED		(1 << 6)
#define LM3554_FLAG_INPUT_VOLTAGE_LOW	(1 << 7)

#define LM3554_CONFIG_REG_1		0xE0
#define LM3554_ENVM_TX2_SHIFT		5
#define LM3554_TX2_POLARITY_SHIFT	6

struct lm3554 {
	struct v4l2_subdev sd;

	struct mutex power_lock;
	struct v4l2_ctrl_handler ctrl_handler;
	int power_count;

	unsigned int mode;
	int timeout;
	u8 torch_current;
	u8 indicator_current;
	u8 flash_current;

	struct timer_list flash_off_delay;
	struct lm3554_platform_data *pdata;
};

#define to_lm3554(p_sd)	container_of(p_sd, struct lm3554, sd)

/* Return negative errno else zero on success */
static int lm3554_write(struct lm3554 *flash, u8 addr, u8 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&flash->sd);
	int ret;

	ret = i2c_smbus_write_byte_data(client, addr, val);

	dev_dbg(&client->dev, "Write Addr:%02X Val:%02X %s\n", addr, val,
		ret < 0 ? "fail" : "ok");

	return ret;
}

/* Return negative errno else a data byte received from the device. */
static int lm3554_read(struct lm3554 *flash, u8 addr)
{
	struct i2c_client *client = v4l2_get_subdevdata(&flash->sd);
	int ret;

	ret = i2c_smbus_read_byte_data(client, addr);

	dev_dbg(&client->dev, "Read Addr:%02X Val:%02X %s\n", addr, ret,
		ret < 0 ? "fail" : "ok");

	return ret;
}

/* -----------------------------------------------------------------------------
 * Hardware configuration
 */

static int lm3554_set_mode(struct lm3554 *flash, unsigned int mode)
{
	u8 val;
	int ret;

	val = (mode << LM3554_FLASH_MODE_SHIFT) |
	      (flash->flash_current << LM3554_FLASH_CURRENT_SHIFT);

	ret = lm3554_write(flash, LM3554_FLASH_BRIGHTNESS_REG, val);
	if (ret == 0)
		flash->mode = mode;
	return ret;
}

static int lm3554_set_torch(struct lm3554 *flash)
{
	u8 val;

	val = (flash->mode << LM3554_TORCH_MODE_SHIFT) |
	      (flash->torch_current << LM3554_TORCH_CURRENT_SHIFT) |
	      (flash->indicator_current << LM3554_INDICATOR_CURRENT_SHIFT);

	return lm3554_write(flash, LM3554_TORCH_BRIGHTNESS_REG, val);
}

static int lm3554_set_flash(struct lm3554 *flash)
{
	u8 val;

	val = (flash->mode << LM3554_FLASH_MODE_SHIFT) |
	      (flash->flash_current << LM3554_FLASH_CURRENT_SHIFT);

	return lm3554_write(flash, LM3554_FLASH_BRIGHTNESS_REG, val);
}

static int lm3554_set_duration(struct lm3554 *flash)
{
	u8 val;

	val = (flash->timeout << LM3554_FLASH_TIMEOUT_SHIFT) |
	      (flash->pdata->current_limit << LM3554_CURRENT_LIMIT_SHIFT);

	return lm3554_write(flash, LM3554_FLASH_DURATION_REG, val);
}

static int lm3554_set_config1(struct lm3554 *flash)
{
	u8 val;

	val = (flash->pdata->envm_tx2 << LM3554_ENVM_TX2_SHIFT) |
	      (flash->pdata->tx2_polarity << LM3554_TX2_POLARITY_SHIFT);
	return lm3554_write(flash, LM3554_CONFIG_REG_1, val);
}

/* -----------------------------------------------------------------------------
 * Hardware trigger
 */
static void lm3554_flash_off_delay(long unsigned int arg)
{
	struct v4l2_subdev *sd = i2c_get_clientdata((struct i2c_client *)arg);
	struct lm3554 *flash = to_lm3554(sd);
	struct lm3554_platform_data *pdata = flash->pdata;

	gpio_set_value(pdata->gpio_strobe, 0);
}

static int lm3554_hw_strobe(struct i2c_client *client, bool strobe)
{
	int ret, timer_pending;
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct lm3554 *flash = to_lm3554(sd);
	struct lm3554_platform_data *pdata = flash->pdata;

	/*
	 * An abnormal high flash current is observed when strobe off the
	 * flash. Workaround here is firstly set flash current to lower level,
	 * wait a short moment, and then strobe off the flash.
	 */

	timer_pending = del_timer_sync(&flash->flash_off_delay);

	/* Flash off */
	if (!strobe) {
		/* set current to 70mA and wait a while */
		ret = lm3554_write(flash, LM3554_FLASH_BRIGHTNESS_REG, 0);
		if (ret < 0)
			goto err;
		mod_timer(&flash->flash_off_delay,
			  jiffies + msecs_to_jiffies(LM3554_TIMER_DELAY));
		return 0;
	}

	/* Flash on */

	/*
	 * If timer is killed before run, flash is not strobe off,
	 * so must strobe off here
	 */
	if (timer_pending)
		gpio_set_value(pdata->gpio_strobe, 0);

	/* Restore flash current settings */
	ret = lm3554_set_flash(flash);
	if (ret < 0)
		goto err;

	/* Strobe on Flash */
	gpio_set_value(pdata->gpio_strobe, 1);

	return 0;
err:
	dev_err(&client->dev, "failed to %s flash strobe (%d)\n",
		strobe ? "on" : "off", ret);
	return ret;
}

/* -----------------------------------------------------------------------------
 * V4L2 controls
 */

static int lm3554_read_status(struct lm3554 *flash)
{
	int ret;
	struct i2c_client *client = v4l2_get_subdevdata(&flash->sd);

	/* NOTE: reading register clear fault status */
	ret = lm3554_read(flash, LM3554_FLAGS_REG);
	if (ret < 0)
		return ret;

	/*
	 * Accordingly to datasheet we read back '1' in bit 6.
	 * Clear it first.
	 */
	ret &= ~LM3554_FLAG_UNUSED;

	/*
	 * Do not take TX1/TX2 signal as an error
	 * because MSIC will not turn off flash, but turn to
	 * torch mode according to gsm modem signal by hardware.
	 */
	ret &= ~(LM3554_FLAG_TX1_INTERRUPT | LM3554_FLAG_TX2_INTERRUPT);

	if (ret > 0)
		dev_dbg(&client->dev, "LM3554 flag status: %02x\n", ret);

	return ret;
}

static int lm3554_s_flash_timeout(struct v4l2_subdev *sd, u32 val)
{
	struct lm3554 *flash = to_lm3554(sd);

	val = clamp(val, LM3554_MIN_TIMEOUT, LM3554_MAX_TIMEOUT);
	val = val / LM3554_TIMEOUT_STEPSIZE - 1;

	flash->timeout = val;

	return lm3554_set_duration(flash);
}

static int lm3554_g_flash_timeout(struct v4l2_subdev *sd, s32 *val)
{
	struct lm3554 *flash = to_lm3554(sd);

	*val = (u32)(flash->timeout + 1) * LM3554_TIMEOUT_STEPSIZE;

	return 0;
}

static int lm3554_s_flash_intensity(struct v4l2_subdev *sd, u32 intensity)
{
	struct lm3554 *flash = to_lm3554(sd);

	intensity = LM3554_CLAMP_PERCENTAGE(intensity);
	intensity = LM3554_PERCENT_TO_VALUE(intensity, LM3554_FLASH_STEP);

	flash->flash_current = intensity;

	return lm3554_set_flash(flash);
}

static int lm3554_g_flash_intensity(struct v4l2_subdev *sd, s32 *val)
{
	struct lm3554 *flash = to_lm3554(sd);

	*val = LM3554_VALUE_TO_PERCENT((u32)flash->flash_current,
			LM3554_FLASH_STEP);

	return 0;
}

static int lm3554_s_torch_intensity(struct v4l2_subdev *sd, u32 intensity)
{
	struct lm3554 *flash = to_lm3554(sd);

	intensity = LM3554_CLAMP_PERCENTAGE(intensity);
	intensity = LM3554_PERCENT_TO_VALUE(intensity, LM3554_TORCH_STEP);

	flash->torch_current = intensity;

	return lm3554_set_torch(flash);
}

static int lm3554_g_torch_intensity(struct v4l2_subdev *sd, s32 *val)
{
	struct lm3554 *flash = to_lm3554(sd);

	*val = LM3554_VALUE_TO_PERCENT((u32)flash->torch_current,
			LM3554_TORCH_STEP);

	return 0;
}

static int lm3554_s_indicator_intensity(struct v4l2_subdev *sd, u32 intensity)
{
	struct lm3554 *flash = to_lm3554(sd);

	intensity = LM3554_CLAMP_PERCENTAGE(intensity);
	intensity = LM3554_PERCENT_TO_VALUE(intensity, LM3554_INDICATOR_STEP);

	flash->indicator_current = intensity;

	return lm3554_set_torch(flash);
}

static int lm3554_g_indicator_intensity(struct v4l2_subdev *sd, s32 *val)
{
	struct lm3554 *flash = to_lm3554(sd);

	*val = LM3554_VALUE_TO_PERCENT((u32)flash->indicator_current,
			LM3554_INDICATOR_STEP);

	return 0;
}

static int lm3554_s_flash_strobe(struct v4l2_subdev *sd, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return lm3554_hw_strobe(client, val);
}

static int lm3554_s_flash_mode(struct v4l2_subdev *sd, u32 new_mode)
{
	struct lm3554 *flash = to_lm3554(sd);
	unsigned int mode;

	switch (new_mode) {
	case ATOMISP_FLASH_MODE_OFF:
		mode = LM3554_MODE_SHUTDOWN;
		break;
	case ATOMISP_FLASH_MODE_FLASH:
		mode = LM3554_MODE_FLASH;
		break;
	case ATOMISP_FLASH_MODE_INDICATOR:
		mode = LM3554_MODE_INDICATOR;
		break;
	case ATOMISP_FLASH_MODE_TORCH:
		mode = LM3554_MODE_TORCH;
		break;
	default:
		return -EINVAL;
	}

	return lm3554_set_mode(flash, mode);
}

static int lm3554_g_flash_mode(struct v4l2_subdev *sd, s32 *val)
{
	struct lm3554 *flash = to_lm3554(sd);
	*val = flash->mode;
	return 0;
}

static int lm3554_g_flash_status(struct v4l2_subdev *sd, s32 *val)
{
	struct lm3554 *flash = to_lm3554(sd);
	int value;

	value = lm3554_read_status(flash);
	if (value < 0)
		return value;

	if (value & LM3554_FLAG_TIMEOUT)
		*val = ATOMISP_FLASH_STATUS_TIMEOUT;
	else if (value > 0)
		*val = ATOMISP_FLASH_STATUS_HW_ERROR;
	else
		*val = ATOMISP_FLASH_STATUS_OK;

	return 0;
}

#ifndef CSS15
static int lm3554_g_flash_status_register(struct v4l2_subdev *sd, s32 *val)
{
	struct lm3554 *flash = to_lm3554(sd);
	int ret;

	ret = lm3554_read(flash, LM3554_FLAGS_REG);

	if (ret < 0)
		return ret;

	*val = ret;
	return 0;
}
#endif

static int lm3554_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct lm3554 *dev =
	    container_of(ctrl->handler, struct lm3554, ctrl_handler);
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_FLASH_TIMEOUT:
		ret = lm3554_s_flash_timeout(&dev->sd, ctrl->val);
		break;
	case V4L2_CID_FLASH_INTENSITY:
		ret = lm3554_s_flash_intensity(&dev->sd, ctrl->val);
		break;
	case V4L2_CID_FLASH_TORCH_INTENSITY:
		ret = lm3554_s_torch_intensity(&dev->sd, ctrl->val);
		break;
	case V4L2_CID_FLASH_INDICATOR_INTENSITY:
		ret = lm3554_s_indicator_intensity(&dev->sd, ctrl->val);
		break;
	case V4L2_CID_FLASH_STROBE:
		ret = lm3554_s_flash_strobe(&dev->sd, ctrl->val);
		break;
	case V4L2_CID_FLASH_MODE:
		ret = lm3554_s_flash_mode(&dev->sd, ctrl->val);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int lm3554_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct lm3554 *dev =
	    container_of(ctrl->handler, struct lm3554, ctrl_handler);
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_FLASH_TIMEOUT:
		ret = lm3554_g_flash_timeout(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_FLASH_INTENSITY:
		ret = lm3554_g_flash_intensity(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_FLASH_TORCH_INTENSITY:
		ret = lm3554_g_torch_intensity(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_FLASH_INDICATOR_INTENSITY:
		ret = lm3554_g_indicator_intensity(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_FLASH_MODE:
		ret = lm3554_g_flash_mode(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_FLASH_STATUS:
		ret = lm3554_g_flash_status(&dev->sd, &ctrl->val);
		break;
#ifndef CSS15
	case V4L2_CID_FLASH_STATUS_REGISTER:
		ret = lm3554_g_flash_status_register(&dev->sd, &ctrl->val);
		break;
#endif
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct v4l2_ctrl_ops ctrl_ops = {
	.s_ctrl = lm3554_s_ctrl,
	.g_volatile_ctrl = lm3554_g_volatile_ctrl
};

static const struct v4l2_ctrl_config lm3554_controls[] = {
	{
	 .ops = &ctrl_ops,
	 .id = V4L2_CID_FLASH_TIMEOUT,
	 .type = V4L2_CTRL_TYPE_INTEGER,
	 .name = "Flash Timeout",
	 .min = 0x0,
	 .max = LM3554_MAX_TIMEOUT,
	 .step = 0x01,
	 .def = LM3554_DEFAULT_TIMEOUT,
	 .flags = 0,
	 },
	{
	 .ops = &ctrl_ops,
	 .id = V4L2_CID_FLASH_INTENSITY,
	 .type = V4L2_CTRL_TYPE_INTEGER,
	 .name = "Flash Intensity",
	 .min = LM3554_MIN_PERCENT,
	 .max = LM3554_MAX_PERCENT,
	 .step = 0x01,
	 .def = LM3554_FLASH_DEFAULT_BRIGHTNESS,
	 .flags = 0,
	 },
	{
	 .ops = &ctrl_ops,
	 .id = V4L2_CID_FLASH_TORCH_INTENSITY,
	 .type = V4L2_CTRL_TYPE_INTEGER,
	 .name = "Torch Intensity",
	 .min = LM3554_MIN_PERCENT,
	 .max = LM3554_MAX_PERCENT,
	 .step = 0x01,
	 .def = LM3554_TORCH_DEFAULT_BRIGHTNESS,
	 .flags = 0,
	 },
	{
	 .ops = &ctrl_ops,
	 .id = V4L2_CID_FLASH_INDICATOR_INTENSITY,
	 .type = V4L2_CTRL_TYPE_INTEGER,
	 .name = "Indicator Intensity",
	 .min = LM3554_MIN_PERCENT,
	 .max = LM3554_MAX_PERCENT,
	 .step = 0x01,
	 .def = LM3554_INDICATOR_DEFAULT_BRIGHTNESS,
	 .flags = 0,
	 },
	{
	 .ops = &ctrl_ops,
	 .id = V4L2_CID_FLASH_STROBE,
	 .type = V4L2_CTRL_TYPE_BOOLEAN,
	 .name = "Flash Strobe",
	 .min = 0,
	 .max = 1,
	 .step = 1,
	 .def = 0,
	 .flags = 0,
	 },
	{
	 .ops = &ctrl_ops,
	 .id = V4L2_CID_FLASH_MODE,
	 .type = V4L2_CTRL_TYPE_INTEGER,
	 .name = "Flash Mode",
	 .min = 0,
	 .max = 100,
	 .step = 1,
	 .def = ATOMISP_FLASH_MODE_OFF,
	 .flags = 0,
	 },
	{
	 .ops = &ctrl_ops,
	 .id = V4L2_CID_FLASH_STATUS,
	 .type = V4L2_CTRL_TYPE_BOOLEAN,
	 .name = "Flash Status",
	 .min = 0,
	 .max = 100,
	 .step = 1,
	 .def = ATOMISP_FLASH_STATUS_OK,
	 .flags = 0,
	 },
#ifndef CSS15
	{
	 .ops = &ctrl_ops,
	 .id = V4L2_CID_FLASH_STATUS_REGISTER,
	 .type = V4L2_CTRL_TYPE_BOOLEAN,
	 .name = "Flash Status Register",
	 .min = 0,
	 .max = 100,
	 .step = 1,
	 .def = 0,
	 .flags = 0,
	 },
#endif
};

/* -----------------------------------------------------------------------------
 * V4L2 subdev core operations
 */

/* Put device into known state. */
static int lm3554_setup(struct lm3554 *flash)
{
	struct i2c_client *client = v4l2_get_subdevdata(&flash->sd);
	int ret;

	/* clear the flags register */
	ret = lm3554_read(flash, LM3554_FLAGS_REG);
	if (ret < 0)
		return ret;

	dev_dbg(&client->dev, "Fault info: %02x\n", ret);

	ret = lm3554_set_config1(flash);
	if (ret < 0)
		return ret;

	ret = lm3554_set_duration(flash);
	if (ret < 0)
		return ret;

	ret = lm3554_set_torch(flash);
	if (ret < 0)
		return ret;

	ret = lm3554_set_flash(flash);
	if (ret < 0)
		return ret;

	/* read status */
	ret = lm3554_read_status(flash);
	if (ret < 0)
		return ret;

	return ret ? -EIO : 0;
}

static int __lm3554_s_power(struct lm3554 *flash, int power)
{
	struct lm3554_platform_data *pdata = flash->pdata;
	int ret;

	/*initialize flash driver*/
	gpio_set_value(pdata->gpio_reset, power);
	usleep_range(100, 100 + 1);

	if (power) {
		/* Setup default values. This makes sure that the chip
		 * is in a known state.
		 */
		ret = lm3554_setup(flash);
		if (ret < 0) {
			__lm3554_s_power(flash, 0);
			return ret;
		}
	}

	return 0;
}

static int lm3554_s_power(struct v4l2_subdev *sd, int power)
{
	struct lm3554 *flash = to_lm3554(sd);
	int ret = 0;

	mutex_lock(&flash->power_lock);

	if (flash->power_count == !power) {
		ret = __lm3554_s_power(flash, !!power);
		if (ret < 0)
			goto done;
	}

	flash->power_count += power ? 1 : -1;
	WARN_ON(flash->power_count < 0);

done:
	mutex_unlock(&flash->power_lock);
	return ret;
}

static const struct v4l2_subdev_core_ops lm3554_core_ops = {
	.s_power = lm3554_s_power,
};

static const struct v4l2_subdev_ops lm3554_ops = {
	.core = &lm3554_core_ops,
};

static int lm3554_detect(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_adapter *adapter = client->adapter;
	struct lm3554 *flash = to_lm3554(sd);
	int ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "lm3554_detect i2c error\n");
		return -ENODEV;
	}

	/* Power up the flash driver and reset it */
	ret = lm3554_s_power(&flash->sd, 1);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to power on lm3554 LED flash\n");
	} else {
		dev_dbg(&client->dev, "Successfully detected lm3554 LED flash\n");
		lm3554_s_power(&flash->sd, 0);
	}

	return ret;
}

static int lm3554_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return lm3554_s_power(sd, 1);
}

static int lm3554_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return lm3554_s_power(sd, 0);
}

static const struct v4l2_subdev_internal_ops lm3554_internal_ops = {
	.registered = lm3554_detect,
	.open = lm3554_open,
	.close = lm3554_close,
};

/* -----------------------------------------------------------------------------
 *  I2C driver
 */
#ifdef CONFIG_PM

static int lm3554_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct lm3554 *flash = to_lm3554(subdev);
	int rval;

	if (flash->power_count == 0)
		return 0;

	rval = __lm3554_s_power(flash, 0);

	dev_dbg(&client->dev, "Suspend %s\n", rval < 0 ? "failed" : "ok");

	return rval;
}

static int lm3554_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct lm3554 *flash = to_lm3554(subdev);
	int rval;

	if (flash->power_count == 0)
		return 0;

	rval = __lm3554_s_power(flash, 1);

	dev_dbg(&client->dev, "Resume %s\n", rval < 0 ? "fail" : "ok");

	return rval;
}

#else

#define lm3554_suspend NULL
#define lm3554_resume  NULL

#endif /* CONFIG_PM */

static int lm3554_gpio_init(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct lm3554 *flash = to_lm3554(sd);
	struct lm3554_platform_data *pdata = flash->pdata;
	int ret;

	if (!gpio_is_valid(pdata->gpio_reset))
		return -EINVAL;

	ret = gpio_direction_output(pdata->gpio_reset, 0);
	if (ret < 0)
		goto err_gpio_reset;
	dev_info(&client->dev, "flash led reset successfully\n");

	if (!gpio_is_valid(pdata->gpio_strobe)) {
		ret = -EINVAL;
		goto err_gpio_dir_reset;
	}

	ret = gpio_direction_output(pdata->gpio_strobe, 0);
	if (ret < 0)
		goto err_gpio_strobe;

	return 0;

err_gpio_strobe:
	gpio_free(pdata->gpio_strobe);
err_gpio_dir_reset:
	gpio_direction_output(pdata->gpio_reset, 0);
err_gpio_reset:
	gpio_free(pdata->gpio_reset);

	return ret;
}

static int lm3554_gpio_uninit(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct lm3554 *flash = to_lm3554(sd);
	struct lm3554_platform_data *pdata = flash->pdata;
	int ret;

	ret = gpio_direction_output(pdata->gpio_strobe, 0);
	if (ret < 0)
		return ret;

	ret = gpio_direction_output(pdata->gpio_reset, 0);
	if (ret < 0)
		return ret;

	gpio_free(pdata->gpio_strobe);
	gpio_free(pdata->gpio_reset);
	return 0;
}

static void *lm3554_platform_data_func(struct i2c_client *client)
{
	static struct lm3554_platform_data platform_data;

	if (ACPI_COMPANION(&client->dev)) {
		platform_data.gpio_reset =
		    desc_to_gpio(gpiod_get_index(&(client->dev),
						 NULL, 2, GPIOD_OUT_LOW));
		platform_data.gpio_strobe =
		    desc_to_gpio(gpiod_get_index(&(client->dev),
						 NULL, 0, GPIOD_OUT_LOW));
		platform_data.gpio_torch =
		    desc_to_gpio(gpiod_get_index(&(client->dev),
						 NULL, 1, GPIOD_OUT_LOW));
	} else {
		platform_data.gpio_reset = -1;
		platform_data.gpio_strobe = -1;
		platform_data.gpio_torch = -1;
	}

	dev_info(&client->dev, "camera pdata: lm3554: reset: %d strobe %d torch %d\n",
		platform_data.gpio_reset, platform_data.gpio_strobe,
		platform_data.gpio_torch);

	/* Set to TX2 mode, then ENVM/TX2 pin is a power amplifier sync input:
	 * ENVM/TX pin asserted, flash forced into torch;
	 * ENVM/TX pin desserted, flash set back;
	 */
	platform_data.envm_tx2 = 1;
	platform_data.tx2_polarity = 0;

	/* set peak current limit to be 1000mA */
	platform_data.current_limit = 0;

	return &platform_data;
}

static int lm3554_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	int err = 0;
	struct lm3554 *flash;
	unsigned int i;
	int ret;

	flash = kzalloc(sizeof(*flash), GFP_KERNEL);
	if (!flash) {
		dev_err(&client->dev, "out of memory\n");
		return -ENOMEM;
	}

	flash->pdata = client->dev.platform_data;

	if (!flash->pdata || ACPI_COMPANION(&client->dev))
		flash->pdata = lm3554_platform_data_func(client);

	v4l2_i2c_subdev_init(&flash->sd, client, &lm3554_ops);
	flash->sd.internal_ops = &lm3554_internal_ops;
	flash->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	flash->mode = ATOMISP_FLASH_MODE_OFF;
	flash->timeout = LM3554_MAX_TIMEOUT / LM3554_TIMEOUT_STEPSIZE - 1;
	ret =
	    v4l2_ctrl_handler_init(&flash->ctrl_handler,
				   ARRAY_SIZE(lm3554_controls));
	if (ret) {
		dev_err(&client->dev, "error initialize a ctrl_handler.\n");
		goto fail2;
	}

	for (i = 0; i < ARRAY_SIZE(lm3554_controls); i++)
		v4l2_ctrl_new_custom(&flash->ctrl_handler, &lm3554_controls[i],
				     NULL);

	if (flash->ctrl_handler.error) {

		dev_err(&client->dev, "ctrl_handler error.\n");
		goto fail2;
	}

	flash->sd.ctrl_handler = &flash->ctrl_handler;
	err = media_entity_pads_init(&flash->sd.entity, 0, NULL);
	if (err) {
		dev_err(&client->dev, "error initialize a media entity.\n");
		goto fail1;
	}

	flash->sd.entity.function = MEDIA_ENT_F_FLASH;

	mutex_init(&flash->power_lock);

	setup_timer(&flash->flash_off_delay, lm3554_flash_off_delay,
		    (unsigned long)client);

	err = lm3554_gpio_init(client);
	if (err) {
		dev_err(&client->dev, "gpio request/direction_output fail");
		goto fail2;
	}
	if (ACPI_HANDLE(&client->dev))
		err = atomisp_register_i2c_module(&flash->sd, NULL, LED_FLASH);
	return 0;
fail2:
	media_entity_cleanup(&flash->sd.entity);
	v4l2_ctrl_handler_free(&flash->ctrl_handler);
fail1:
	v4l2_device_unregister_subdev(&flash->sd);
	kfree(flash);

	return err;
}

static int lm3554_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct lm3554 *flash = to_lm3554(sd);
	int ret;

	media_entity_cleanup(&flash->sd.entity);
	v4l2_ctrl_handler_free(&flash->ctrl_handler);
	v4l2_device_unregister_subdev(sd);

	atomisp_gmin_remove_subdev(sd);

	del_timer_sync(&flash->flash_off_delay);

	ret = lm3554_gpio_uninit(client);
	if (ret < 0)
		goto fail;

	kfree(flash);

	return 0;
fail:
	dev_err(&client->dev, "gpio request/direction_output fail");
	return ret;
}

static const struct i2c_device_id lm3554_id[] = {
	{LM3554_NAME, 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, lm3554_id);

static const struct dev_pm_ops lm3554_pm_ops = {
	.suspend = lm3554_suspend,
	.resume = lm3554_resume,
};

static struct acpi_device_id lm3554_acpi_match[] = {
	{ "INTCF1C" },
	{},
};

MODULE_DEVICE_TABLE(acpi, lm3554_acpi_match);

static struct i2c_driver lm3554_driver = {
	.driver = {
		.name = LM3554_NAME,
		.pm   = &lm3554_pm_ops,
		.acpi_match_table = ACPI_PTR(lm3554_acpi_match),
	},
	.probe = lm3554_probe,
	.remove = lm3554_remove,
	.id_table = lm3554_id,
};

static __init int init_lm3554(void)
{
	return i2c_add_driver(&lm3554_driver);
}

static __exit void exit_lm3554(void)
{
	i2c_del_driver(&lm3554_driver);
}

module_init(init_lm3554);
module_exit(exit_lm3554);
MODULE_AUTHOR("Jing Tao <jing.tao@intel.com>");
MODULE_DESCRIPTION("LED flash driver for LM3554");
MODULE_LICENSE("GPL");
