/*
 * drivers/media/i2c/as3645a.c - AS3645A and LM3555 flash controllers driver
 *
 * Copyright (C) 2008-2011 Nokia Corporation
 * Copyright (c) 2011, Intel Corporation.
 *
 * Contact: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
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
 *
 * TODO:
 * - Check hardware FSTROBE control when sensor driver add support for this
 *
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include <media/as3645a.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>

#define AS_TIMER_MS_TO_CODE(t)			(((t) - 100) / 50)
#define AS_TIMER_CODE_TO_MS(c)			(50 * (c) + 100)

/* Register definitions */

/* Read-only Design info register: Reset state: xxxx 0001 */
#define AS_DESIGN_INFO_REG			0x00
#define AS_DESIGN_INFO_FACTORY(x)		(((x) >> 4))
#define AS_DESIGN_INFO_MODEL(x)			((x) & 0x0f)

/* Read-only Version control register: Reset state: 0000 0000
 * for first engineering samples
 */
#define AS_VERSION_CONTROL_REG			0x01
#define AS_VERSION_CONTROL_RFU(x)		(((x) >> 4))
#define AS_VERSION_CONTROL_VERSION(x)		((x) & 0x0f)

/* Read / Write	(Indicator and timer register): Reset state: 0000 1111 */
#define AS_INDICATOR_AND_TIMER_REG		0x02
#define AS_INDICATOR_AND_TIMER_TIMEOUT_SHIFT	0
#define AS_INDICATOR_AND_TIMER_VREF_SHIFT	4
#define AS_INDICATOR_AND_TIMER_INDICATOR_SHIFT	6

/* Read / Write	(Current set register): Reset state: 0110 1001 */
#define AS_CURRENT_SET_REG			0x03
#define AS_CURRENT_ASSIST_LIGHT_SHIFT		0
#define AS_CURRENT_LED_DET_ON			(1 << 3)
#define AS_CURRENT_FLASH_CURRENT_SHIFT		4

/* Read / Write	(Control register): Reset state: 1011 0100 */
#define AS_CONTROL_REG				0x04
#define AS_CONTROL_MODE_SETTING_SHIFT		0
#define AS_CONTROL_STROBE_ON			(1 << 2)
#define AS_CONTROL_OUT_ON			(1 << 3)
#define AS_CONTROL_EXT_TORCH_ON			(1 << 4)
#define AS_CONTROL_STROBE_TYPE_EDGE		(0 << 5)
#define AS_CONTROL_STROBE_TYPE_LEVEL		(1 << 5)
#define AS_CONTROL_COIL_PEAK_SHIFT		6

/* Read only (D3 is read / write) (Fault and info): Reset state: 0000 x000 */
#define AS_FAULT_INFO_REG			0x05
#define AS_FAULT_INFO_INDUCTOR_PEAK_LIMIT	(1 << 1)
#define AS_FAULT_INFO_INDICATOR_LED		(1 << 2)
#define AS_FAULT_INFO_LED_AMOUNT		(1 << 3)
#define AS_FAULT_INFO_TIMEOUT			(1 << 4)
#define AS_FAULT_INFO_OVER_TEMPERATURE		(1 << 5)
#define AS_FAULT_INFO_SHORT_CIRCUIT		(1 << 6)
#define AS_FAULT_INFO_OVER_VOLTAGE		(1 << 7)

/* Boost register */
#define AS_BOOST_REG				0x0d
#define AS_BOOST_CURRENT_DISABLE		(0 << 0)
#define AS_BOOST_CURRENT_ENABLE			(1 << 0)

/* Password register is used to unlock boost register writing */
#define AS_PASSWORD_REG				0x0f
#define AS_PASSWORD_UNLOCK_VALUE		0x55

enum as_mode {
	AS_MODE_EXT_TORCH = 0 << AS_CONTROL_MODE_SETTING_SHIFT,
	AS_MODE_INDICATOR = 1 << AS_CONTROL_MODE_SETTING_SHIFT,
	AS_MODE_ASSIST = 2 << AS_CONTROL_MODE_SETTING_SHIFT,
	AS_MODE_FLASH = 3 << AS_CONTROL_MODE_SETTING_SHIFT,
};

/*
 * struct as3645a
 *
 * @subdev:		V4L2 subdev
 * @pdata:		Flash platform data
 * @power_lock:		Protects power_count
 * @power_count:	Power reference count
 * @led_mode:		V4L2 flash LED mode
 * @timeout:		Flash timeout in microseconds
 * @flash_current:	Flash current (0=200mA ... 15=500mA). Maximum
 *			values are 400mA for two LEDs and 500mA for one LED.
 * @assist_current:	Torch/Assist light current (0=20mA, 1=40mA ... 7=160mA)
 * @indicator_current:	Indicator LED current (0=0mA, 1=2.5mA ... 4=10mA)
 * @strobe_source:	Flash strobe source (software or external)
 */
struct as3645a {
	struct v4l2_subdev subdev;
	const struct as3645a_platform_data *pdata;

	struct mutex power_lock;
	int power_count;

	/* Controls */
	struct v4l2_ctrl_handler ctrls;

	enum v4l2_flash_led_mode led_mode;
	unsigned int timeout;
	u8 flash_current;
	u8 assist_current;
	u8 indicator_current;
	enum v4l2_flash_strobe_source strobe_source;
};

#define to_as3645a(sd) container_of(sd, struct as3645a, subdev)

/* Return negative errno else zero on success */
static int as3645a_write(struct as3645a *flash, u8 addr, u8 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&flash->subdev);
	int rval;

	rval = i2c_smbus_write_byte_data(client, addr, val);

	dev_dbg(&client->dev, "Write Addr:%02X Val:%02X %s\n", addr, val,
		rval < 0 ? "fail" : "ok");

	return rval;
}

/* Return negative errno else a data byte received from the device. */
static int as3645a_read(struct as3645a *flash, u8 addr)
{
	struct i2c_client *client = v4l2_get_subdevdata(&flash->subdev);
	int rval;

	rval = i2c_smbus_read_byte_data(client, addr);

	dev_dbg(&client->dev, "Read Addr:%02X Val:%02X %s\n", addr, rval,
		rval < 0 ? "fail" : "ok");

	return rval;
}

/* -----------------------------------------------------------------------------
 * Hardware configuration and trigger
 */

/*
 * as3645a_set_config - Set flash configuration registers
 * @flash: The flash
 *
 * Configure the hardware with flash, assist and indicator currents, as well as
 * flash timeout.
 *
 * Return 0 on success, or a negative error code if an I2C communication error
 * occurred.
 */
static int as3645a_set_config(struct as3645a *flash)
{
	int ret;
	u8 val;

	val = (flash->flash_current << AS_CURRENT_FLASH_CURRENT_SHIFT)
	    | (flash->assist_current << AS_CURRENT_ASSIST_LIGHT_SHIFT)
	    | AS_CURRENT_LED_DET_ON;

	ret = as3645a_write(flash, AS_CURRENT_SET_REG, val);
	if (ret < 0)
		return ret;

	val = AS_TIMER_MS_TO_CODE(flash->timeout / 1000)
		    << AS_INDICATOR_AND_TIMER_TIMEOUT_SHIFT;

	val |= (flash->pdata->vref << AS_INDICATOR_AND_TIMER_VREF_SHIFT)
	    |  ((flash->indicator_current ? flash->indicator_current - 1 : 0)
		 << AS_INDICATOR_AND_TIMER_INDICATOR_SHIFT);

	return as3645a_write(flash, AS_INDICATOR_AND_TIMER_REG, val);
}

/*
 * as3645a_set_control - Set flash control register
 * @flash: The flash
 * @mode: Desired output mode
 * @on: Desired output state
 *
 * Configure the hardware with output mode and state.
 *
 * Return 0 on success, or a negative error code if an I2C communication error
 * occurred.
 */
static int
as3645a_set_control(struct as3645a *flash, enum as_mode mode, bool on)
{
	u8 reg;

	/* Configure output parameters and operation mode. */
	reg = (flash->pdata->peak << AS_CONTROL_COIL_PEAK_SHIFT)
	    | (on ? AS_CONTROL_OUT_ON : 0)
	    | mode;

	if (flash->led_mode == V4L2_FLASH_LED_MODE_FLASH &&
	    flash->strobe_source == V4L2_FLASH_STROBE_SOURCE_EXTERNAL) {
		reg |= AS_CONTROL_STROBE_TYPE_LEVEL
		    |  AS_CONTROL_STROBE_ON;
	}

	return as3645a_write(flash, AS_CONTROL_REG, reg);
}

/*
 * as3645a_set_output - Configure output and operation mode
 * @flash: Flash controller
 * @strobe: Strobe the flash (only valid in flash mode)
 *
 * Turn the LEDs output on/off and set the operation mode based on the current
 * parameters.
 *
 * The AS3645A can't control the indicator LED independently of the flash/torch
 * LED. If the flash controller is in V4L2_FLASH_LED_MODE_NONE mode, set the
 * chip to indicator mode. Otherwise set it to assist light (torch) or flash
 * mode.
 *
 * In indicator and assist modes, turn the output on/off based on the indicator
 * and torch currents. In software strobe flash mode, turn the output on/off
 * based on the strobe parameter.
 */
static int as3645a_set_output(struct as3645a *flash, bool strobe)
{
	enum as_mode mode;
	bool on;

	switch (flash->led_mode) {
	case V4L2_FLASH_LED_MODE_NONE:
		on = flash->indicator_current != 0;
		mode = AS_MODE_INDICATOR;
		break;
	case V4L2_FLASH_LED_MODE_TORCH:
		on = true;
		mode = AS_MODE_ASSIST;
		break;
	case V4L2_FLASH_LED_MODE_FLASH:
		on = strobe;
		mode = AS_MODE_FLASH;
		break;
	default:
		BUG();
	}

	/* Configure output parameters and operation mode. */
	return as3645a_set_control(flash, mode, on);
}

/* -----------------------------------------------------------------------------
 * V4L2 controls
 */

static int as3645a_is_active(struct as3645a *flash)
{
	int ret;

	ret = as3645a_read(flash, AS_CONTROL_REG);
	return ret < 0 ? ret : !!(ret & AS_CONTROL_OUT_ON);
}

static int as3645a_read_fault(struct as3645a *flash)
{
	struct i2c_client *client = v4l2_get_subdevdata(&flash->subdev);
	int rval;

	/* NOTE: reading register clear fault status */
	rval = as3645a_read(flash, AS_FAULT_INFO_REG);
	if (rval < 0)
		return rval;

	if (rval & AS_FAULT_INFO_INDUCTOR_PEAK_LIMIT)
		dev_dbg(&client->dev, "Inductor Peak limit fault\n");

	if (rval & AS_FAULT_INFO_INDICATOR_LED)
		dev_dbg(&client->dev, "Indicator LED fault: "
			"Short circuit or open loop\n");

	dev_dbg(&client->dev, "%u connected LEDs\n",
		rval & AS_FAULT_INFO_LED_AMOUNT ? 2 : 1);

	if (rval & AS_FAULT_INFO_TIMEOUT)
		dev_dbg(&client->dev, "Timeout fault\n");

	if (rval & AS_FAULT_INFO_OVER_TEMPERATURE)
		dev_dbg(&client->dev, "Over temperature fault\n");

	if (rval & AS_FAULT_INFO_SHORT_CIRCUIT)
		dev_dbg(&client->dev, "Short circuit fault\n");

	if (rval & AS_FAULT_INFO_OVER_VOLTAGE)
		dev_dbg(&client->dev, "Over voltage fault: "
			"Indicates missing capacitor or open connection\n");

	return rval;
}

static int as3645a_get_ctrl(struct v4l2_ctrl *ctrl)
{
	struct as3645a *flash =
		container_of(ctrl->handler, struct as3645a, ctrls);
	struct i2c_client *client = v4l2_get_subdevdata(&flash->subdev);
	int value;

	switch (ctrl->id) {
	case V4L2_CID_FLASH_FAULT:
		value = as3645a_read_fault(flash);
		if (value < 0)
			return value;

		ctrl->cur.val = 0;
		if (value & AS_FAULT_INFO_SHORT_CIRCUIT)
			ctrl->cur.val |= V4L2_FLASH_FAULT_SHORT_CIRCUIT;
		if (value & AS_FAULT_INFO_OVER_TEMPERATURE)
			ctrl->cur.val |= V4L2_FLASH_FAULT_OVER_TEMPERATURE;
		if (value & AS_FAULT_INFO_TIMEOUT)
			ctrl->cur.val |= V4L2_FLASH_FAULT_TIMEOUT;
		if (value & AS_FAULT_INFO_OVER_VOLTAGE)
			ctrl->cur.val |= V4L2_FLASH_FAULT_OVER_VOLTAGE;
		if (value & AS_FAULT_INFO_INDUCTOR_PEAK_LIMIT)
			ctrl->cur.val |= V4L2_FLASH_FAULT_OVER_CURRENT;
		if (value & AS_FAULT_INFO_INDICATOR_LED)
			ctrl->cur.val |= V4L2_FLASH_FAULT_INDICATOR;
		break;

	case V4L2_CID_FLASH_STROBE_STATUS:
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH) {
			ctrl->cur.val = 0;
			break;
		}

		value = as3645a_is_active(flash);
		if (value < 0)
			return value;

		ctrl->cur.val = value;
		break;
	}

	dev_dbg(&client->dev, "G_CTRL %08x:%d\n", ctrl->id, ctrl->cur.val);

	return 0;
}

static int as3645a_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct as3645a *flash =
		container_of(ctrl->handler, struct as3645a, ctrls);
	struct i2c_client *client = v4l2_get_subdevdata(&flash->subdev);
	int ret;

	dev_dbg(&client->dev, "S_CTRL %08x:%d\n", ctrl->id, ctrl->val);

	/* If a control that doesn't apply to the current mode is modified,
	 * we store the value and return immediately. The setting will be
	 * applied when the LED mode is changed. Otherwise we apply the setting
	 * immediately.
	 */

	switch (ctrl->id) {
	case V4L2_CID_FLASH_LED_MODE:
		if (flash->indicator_current)
			return -EBUSY;

		ret = as3645a_set_config(flash);
		if (ret < 0)
			return ret;

		flash->led_mode = ctrl->val;
		return as3645a_set_output(flash, false);

	case V4L2_CID_FLASH_STROBE_SOURCE:
		flash->strobe_source = ctrl->val;

		/* Applies to flash mode only. */
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH)
			break;

		return as3645a_set_output(flash, false);

	case V4L2_CID_FLASH_STROBE:
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH)
			return -EBUSY;

		return as3645a_set_output(flash, true);

	case V4L2_CID_FLASH_STROBE_STOP:
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH)
			return -EBUSY;

		return as3645a_set_output(flash, false);

	case V4L2_CID_FLASH_TIMEOUT:
		flash->timeout = ctrl->val;

		/* Applies to flash mode only. */
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH)
			break;

		return as3645a_set_config(flash);

	case V4L2_CID_FLASH_INTENSITY:
		flash->flash_current = (ctrl->val - AS3645A_FLASH_INTENSITY_MIN)
				     / AS3645A_FLASH_INTENSITY_STEP;

		/* Applies to flash mode only. */
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH)
			break;

		return as3645a_set_config(flash);

	case V4L2_CID_FLASH_TORCH_INTENSITY:
		flash->assist_current =
			(ctrl->val - AS3645A_TORCH_INTENSITY_MIN)
			/ AS3645A_TORCH_INTENSITY_STEP;

		/* Applies to torch mode only. */
		if (flash->led_mode != V4L2_FLASH_LED_MODE_TORCH)
			break;

		return as3645a_set_config(flash);

	case V4L2_CID_FLASH_INDICATOR_INTENSITY:
		if (flash->led_mode != V4L2_FLASH_LED_MODE_NONE)
			return -EBUSY;

		flash->indicator_current =
			(ctrl->val - AS3645A_INDICATOR_INTENSITY_MIN)
			/ AS3645A_INDICATOR_INTENSITY_STEP;

		ret = as3645a_set_config(flash);
		if (ret < 0)
			return ret;

		if ((ctrl->val == 0) == (ctrl->cur.val == 0))
			break;

		return as3645a_set_output(flash, false);
	}

	return 0;
}

static const struct v4l2_ctrl_ops as3645a_ctrl_ops = {
	.g_volatile_ctrl = as3645a_get_ctrl,
	.s_ctrl = as3645a_set_ctrl,
};

/* -----------------------------------------------------------------------------
 * V4L2 subdev core operations
 */

/* Put device into know state. */
static int as3645a_setup(struct as3645a *flash)
{
	struct i2c_client *client = v4l2_get_subdevdata(&flash->subdev);
	int ret;

	/* clear errors */
	ret = as3645a_read(flash, AS_FAULT_INFO_REG);
	if (ret < 0)
		return ret;

	dev_dbg(&client->dev, "Fault info: %02x\n", ret);

	ret = as3645a_set_config(flash);
	if (ret < 0)
		return ret;

	ret = as3645a_set_output(flash, false);
	if (ret < 0)
		return ret;

	/* read status */
	ret = as3645a_read_fault(flash);
	if (ret < 0)
		return ret;

	dev_dbg(&client->dev, "AS_INDICATOR_AND_TIMER_REG: %02x\n",
		as3645a_read(flash, AS_INDICATOR_AND_TIMER_REG));
	dev_dbg(&client->dev, "AS_CURRENT_SET_REG: %02x\n",
		as3645a_read(flash, AS_CURRENT_SET_REG));
	dev_dbg(&client->dev, "AS_CONTROL_REG: %02x\n",
		as3645a_read(flash, AS_CONTROL_REG));

	return ret & ~AS_FAULT_INFO_LED_AMOUNT ? -EIO : 0;
}

static int __as3645a_set_power(struct as3645a *flash, int on)
{
	int ret;

	if (!on)
		as3645a_set_control(flash, AS_MODE_EXT_TORCH, false);

	if (flash->pdata->set_power) {
		ret = flash->pdata->set_power(&flash->subdev, on);
		if (ret < 0)
			return ret;
	}

	if (!on)
		return 0;

	ret = as3645a_setup(flash);
	if (ret < 0) {
		if (flash->pdata->set_power)
			flash->pdata->set_power(&flash->subdev, 0);
	}

	return ret;
}

static int as3645a_set_power(struct v4l2_subdev *sd, int on)
{
	struct as3645a *flash = to_as3645a(sd);
	int ret = 0;

	mutex_lock(&flash->power_lock);

	if (flash->power_count == !on) {
		ret = __as3645a_set_power(flash, !!on);
		if (ret < 0)
			goto done;
	}

	flash->power_count += on ? 1 : -1;
	WARN_ON(flash->power_count < 0);

done:
	mutex_unlock(&flash->power_lock);
	return ret;
}

static int as3645a_registered(struct v4l2_subdev *sd)
{
	struct as3645a *flash = to_as3645a(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int rval, man, model, rfu, version;
	const char *vendor;

	/* Power up the flash driver and read manufacturer ID, model ID, RFU
	 * and version.
	 */
	rval = as3645a_set_power(&flash->subdev, 1);
	if (rval < 0)
		return rval;

	rval = as3645a_read(flash, AS_DESIGN_INFO_REG);
	if (rval < 0)
		goto power_off;

	man = AS_DESIGN_INFO_FACTORY(rval);
	model = AS_DESIGN_INFO_MODEL(rval);

	rval = as3645a_read(flash, AS_VERSION_CONTROL_REG);
	if (rval < 0)
		goto power_off;

	rfu = AS_VERSION_CONTROL_RFU(rval);
	version = AS_VERSION_CONTROL_VERSION(rval);

	/* Verify the chip model and version. */
	if (model != 0x01 || rfu != 0x00) {
		dev_err(&client->dev, "AS3645A not detected "
			"(model %d rfu %d)\n", model, rfu);
		rval = -ENODEV;
		goto power_off;
	}

	switch (man) {
	case 1:
		vendor = "AMS, Austria Micro Systems";
		break;
	case 2:
		vendor = "ADI, Analog Devices Inc.";
		break;
	case 3:
		vendor = "NSC, National Semiconductor";
		break;
	case 4:
		vendor = "NXP";
		break;
	case 5:
		vendor = "TI, Texas Instrument";
		break;
	default:
		vendor = "Unknown";
	}

	dev_info(&client->dev, "Chip vendor: %s (%d) Version: %d\n", vendor,
		 man, version);

	rval = as3645a_write(flash, AS_PASSWORD_REG, AS_PASSWORD_UNLOCK_VALUE);
	if (rval < 0)
		goto power_off;

	rval = as3645a_write(flash, AS_BOOST_REG, AS_BOOST_CURRENT_DISABLE);
	if (rval < 0)
		goto power_off;

	/* Setup default values. This makes sure that the chip is in a known
	 * state, in case the power rail can't be controlled.
	 */
	rval = as3645a_setup(flash);

power_off:
	as3645a_set_power(&flash->subdev, 0);

	return rval;
}

static int as3645a_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return as3645a_set_power(sd, 1);
}

static int as3645a_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return as3645a_set_power(sd, 0);
}

static const struct v4l2_subdev_core_ops as3645a_core_ops = {
	.s_power		= as3645a_set_power,
};

static const struct v4l2_subdev_ops as3645a_ops = {
	.core = &as3645a_core_ops,
};

static const struct v4l2_subdev_internal_ops as3645a_internal_ops = {
	.registered = as3645a_registered,
	.open = as3645a_open,
	.close = as3645a_close,
};

/* -----------------------------------------------------------------------------
 *  I2C driver
 */
#ifdef CONFIG_PM

static int as3645a_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct as3645a *flash = to_as3645a(subdev);
	int rval;

	if (flash->power_count == 0)
		return 0;

	rval = __as3645a_set_power(flash, 0);

	dev_dbg(&client->dev, "Suspend %s\n", rval < 0 ? "failed" : "ok");

	return rval;
}

static int as3645a_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct as3645a *flash = to_as3645a(subdev);
	int rval;

	if (flash->power_count == 0)
		return 0;

	rval = __as3645a_set_power(flash, 1);

	dev_dbg(&client->dev, "Resume %s\n", rval < 0 ? "fail" : "ok");

	return rval;
}

#else

#define as3645a_suspend	NULL
#define as3645a_resume	NULL

#endif /* CONFIG_PM */

/*
 * as3645a_init_controls - Create controls
 * @flash: The flash
 *
 * The number of LEDs reported in platform data is used to compute default
 * limits. Parameters passed through platform data can override those limits.
 */
static int as3645a_init_controls(struct as3645a *flash)
{
	const struct as3645a_platform_data *pdata = flash->pdata;
	struct v4l2_ctrl *ctrl;
	int maximum;

	v4l2_ctrl_handler_init(&flash->ctrls, 10);

	/* V4L2_CID_FLASH_LED_MODE */
	v4l2_ctrl_new_std_menu(&flash->ctrls, &as3645a_ctrl_ops,
			       V4L2_CID_FLASH_LED_MODE, 2, ~7,
			       V4L2_FLASH_LED_MODE_NONE);

	/* V4L2_CID_FLASH_STROBE_SOURCE */
	v4l2_ctrl_new_std_menu(&flash->ctrls, &as3645a_ctrl_ops,
			       V4L2_CID_FLASH_STROBE_SOURCE,
			       pdata->ext_strobe ? 1 : 0,
			       pdata->ext_strobe ? ~3 : ~1,
			       V4L2_FLASH_STROBE_SOURCE_SOFTWARE);

	flash->strobe_source = V4L2_FLASH_STROBE_SOURCE_SOFTWARE;

	/* V4L2_CID_FLASH_STROBE */
	v4l2_ctrl_new_std(&flash->ctrls, &as3645a_ctrl_ops,
			  V4L2_CID_FLASH_STROBE, 0, 0, 0, 0);

	/* V4L2_CID_FLASH_STROBE_STOP */
	v4l2_ctrl_new_std(&flash->ctrls, &as3645a_ctrl_ops,
			  V4L2_CID_FLASH_STROBE_STOP, 0, 0, 0, 0);

	/* V4L2_CID_FLASH_STROBE_STATUS */
	ctrl = v4l2_ctrl_new_std(&flash->ctrls, &as3645a_ctrl_ops,
				 V4L2_CID_FLASH_STROBE_STATUS, 0, 1, 1, 1);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	/* V4L2_CID_FLASH_TIMEOUT */
	maximum = pdata->timeout_max;

	v4l2_ctrl_new_std(&flash->ctrls, &as3645a_ctrl_ops,
			  V4L2_CID_FLASH_TIMEOUT, AS3645A_FLASH_TIMEOUT_MIN,
			  maximum, AS3645A_FLASH_TIMEOUT_STEP, maximum);

	flash->timeout = maximum;

	/* V4L2_CID_FLASH_INTENSITY */
	maximum = pdata->flash_max_current;

	v4l2_ctrl_new_std(&flash->ctrls, &as3645a_ctrl_ops,
			  V4L2_CID_FLASH_INTENSITY, AS3645A_FLASH_INTENSITY_MIN,
			  maximum, AS3645A_FLASH_INTENSITY_STEP, maximum);

	flash->flash_current = (maximum - AS3645A_FLASH_INTENSITY_MIN)
			     / AS3645A_FLASH_INTENSITY_STEP;

	/* V4L2_CID_FLASH_TORCH_INTENSITY */
	maximum = pdata->torch_max_current;

	v4l2_ctrl_new_std(&flash->ctrls, &as3645a_ctrl_ops,
			  V4L2_CID_FLASH_TORCH_INTENSITY,
			  AS3645A_TORCH_INTENSITY_MIN, maximum,
			  AS3645A_TORCH_INTENSITY_STEP,
			  AS3645A_TORCH_INTENSITY_MIN);

	flash->assist_current = 0;

	/* V4L2_CID_FLASH_INDICATOR_INTENSITY */
	v4l2_ctrl_new_std(&flash->ctrls, &as3645a_ctrl_ops,
			  V4L2_CID_FLASH_INDICATOR_INTENSITY,
			  AS3645A_INDICATOR_INTENSITY_MIN,
			  AS3645A_INDICATOR_INTENSITY_MAX,
			  AS3645A_INDICATOR_INTENSITY_STEP,
			  AS3645A_INDICATOR_INTENSITY_MIN);

	flash->indicator_current = 0;

	/* V4L2_CID_FLASH_FAULT */
	ctrl = v4l2_ctrl_new_std(&flash->ctrls, &as3645a_ctrl_ops,
				 V4L2_CID_FLASH_FAULT, 0,
				 V4L2_FLASH_FAULT_OVER_VOLTAGE |
				 V4L2_FLASH_FAULT_TIMEOUT |
				 V4L2_FLASH_FAULT_OVER_TEMPERATURE |
				 V4L2_FLASH_FAULT_SHORT_CIRCUIT, 0, 0);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	flash->subdev.ctrl_handler = &flash->ctrls;

	return flash->ctrls.error;
}

static int as3645a_probe(struct i2c_client *client,
			 const struct i2c_device_id *devid)
{
	struct as3645a *flash;
	int ret;

	if (client->dev.platform_data == NULL)
		return -ENODEV;

	flash = kzalloc(sizeof(*flash), GFP_KERNEL);
	if (flash == NULL)
		return -ENOMEM;

	flash->pdata = client->dev.platform_data;

	v4l2_i2c_subdev_init(&flash->subdev, client, &as3645a_ops);
	flash->subdev.internal_ops = &as3645a_internal_ops;
	flash->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	ret = as3645a_init_controls(flash);
	if (ret < 0)
		goto done;

	ret = media_entity_init(&flash->subdev.entity, 0, NULL, 0);
	if (ret < 0)
		goto done;

	flash->subdev.entity.type = MEDIA_ENT_T_V4L2_SUBDEV_FLASH;

	mutex_init(&flash->power_lock);

	flash->led_mode = V4L2_FLASH_LED_MODE_NONE;

done:
	if (ret < 0) {
		v4l2_ctrl_handler_free(&flash->ctrls);
		kfree(flash);
	}

	return ret;
}

static int as3645a_remove(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct as3645a *flash = to_as3645a(subdev);

	v4l2_device_unregister_subdev(subdev);
	v4l2_ctrl_handler_free(&flash->ctrls);
	media_entity_cleanup(&flash->subdev.entity);
	mutex_destroy(&flash->power_lock);
	kfree(flash);

	return 0;
}

static const struct i2c_device_id as3645a_id_table[] = {
	{ AS3645A_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, as3645a_id_table);

static const struct dev_pm_ops as3645a_pm_ops = {
	.suspend = as3645a_suspend,
	.resume = as3645a_resume,
};

static struct i2c_driver as3645a_i2c_driver = {
	.driver	= {
		.name = AS3645A_NAME,
		.pm   = &as3645a_pm_ops,
	},
	.probe	= as3645a_probe,
	.remove	= as3645a_remove,
	.id_table = as3645a_id_table,
};

module_i2c_driver(as3645a_i2c_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("LED flash driver for AS3645A, LM3555 and their clones");
MODULE_LICENSE("GPL");
