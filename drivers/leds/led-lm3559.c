/*
 * Copyright (C) 2010 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <linux/led-lm3559.h>

/*#define DEBUG*/

#define LM3559_ALLOWED_R_BYTES   1
#define LM3559_ALLOWED_W_BYTES   2
#define LM3559_MAX_RW_RETRIES    5
#define LM3559_I2C_RETRY_DELAY  10
#define LM3559_TORCH_STEP	64
#define LM3559_STROBE_STEP	24
#define LM3559_PRIVACY_STEP     32
#define LM3559_RGB_STEP		32

#define LM3559_ENABLE_REG	0x10
#define LM3559_PRIVACY_REG	0x11
#define LM3559_MSG_IND_REG	0x12
#define LM3559_MSG_BLINK_REG	0x13
#define LM3559_PWM_REG	        0x14
#define LM3559_GPIO_REG		0x20
#define LM3559_VLED_MON_REG	0x30
#define LM3559_ADC_DELAY_REG	0x31
#define LM3559_VIN_MONITOR	0x80
#define LM3559_LAST_FLASH	0x81
#define LM3559_TORCH_BRIGHTNESS	0xA0
#define LM3559_FLASH_BRIGHTNESS	0xB0
#define LM3559_FLASH_DURATION	0xC0
#define LM3559_FLAG_REG		0xD0
#define LM3559_CONFIG_REG_1	0xE0
#define LM3559_CONFIG_REG_2	0xF0


#define LED_FAULT		0x04
#define THERMAL_SHUTDOWN 	0x02
#define TX1_INTERRUPT_FAULT 	0x08
#define THERMAL_MONITOR_FAULT 	0x20
#define VOLTAGE_MONITOR_FAULT 	0x80

struct lm3559_data {
	struct i2c_client *client;
	struct lm3559_platform_data *pdata;
	struct led_classdev led_dev;
	struct led_classdev spotlight_dev;
	struct led_classdev msg_ind_red_class_dev;
	struct led_classdev msg_ind_green_class_dev;
	struct led_classdev msg_ind_blue_class_dev;
	struct mutex enable_reg_lock;
	struct work_struct wq;
	int camera_strobe_brightness;
	int flash_light_brightness;
	int torch_light_brightness;
	int privacy_light_brightness;
	int msg_ind_light_brightness;
	int blink_val;
};

#ifdef DEBUG
struct lm3559_reg {
	const char *name;
	uint8_t reg;
} lm3559_regs[] = {
	{ "ENABLE",		LM3559_ENABLE_REG},
	{ "PRIVACY",		LM3559_PRIVACY_REG},
	{ "MSG_IND",		LM3559_MSG_IND_REG},
	{ "MSG_BLINK",		LM3559_MSG_BLINK_REG},
	{ "PRIVACY_PWM",	LM3559_PWM_REG},
	{ "GPIO",		LM3559_GPIO_REG},
	{ "VLED_MON",		LM3559_VLED_MON_REG},
	{ "ADC_DELAY",		LM3559_ADC_DELAY_REG},
	{ "VIN_MONITOR",	LM3559_VIN_MONITOR},
	{ "LAST_FLASH",		LM3559_LAST_FLASH},
	{ "TORCH_BRIGHTNESS",	LM3559_TORCH_BRIGHTNESS},
	{ "FLASH_BRIGHTNESS",	LM3559_FLASH_BRIGHTNESS},
	{ "FLASH_DURATION",	LM3559_FLASH_DURATION},
	{ "FLAG",		LM3559_FLAG_REG},
	{ "CONFIG_REG_1",	LM3559_CONFIG_REG_1},
	{ "CONFIG_REG_2",	LM3559_CONFIG_REG_2},
};
#endif

static uint32_t lm3559_debug;
module_param_named(flash_debug, lm3559_debug, uint, 0664);

int lm3559_read_reg(struct lm3559_data *torch_data, uint8_t reg, uint8_t * val)
{
	int err = -1;
	int i = 0;
	uint8_t dest_buffer;

	if (!val) {
		pr_err("%s: invalid value pointer\n", __func__);
		return -EINVAL;
	}
	/* If I2C client doesn't exist */
	if (torch_data->client == NULL) {
		pr_err("%s: null i2c client\n", __func__);
		return -EUNATCH;
	}

	do {
		dest_buffer = reg;
		err = i2c_master_send(torch_data->client, &dest_buffer,
			LM3559_ALLOWED_R_BYTES);
		if (err == LM3559_ALLOWED_R_BYTES)
			err = i2c_master_recv(torch_data->client, val,
				LM3559_ALLOWED_R_BYTES);
		if (err != LM3559_ALLOWED_R_BYTES)
			msleep_interruptible(LM3559_I2C_RETRY_DELAY);
	} while ((err != LM3559_ALLOWED_R_BYTES) &&
		((++i) < LM3559_MAX_RW_RETRIES));

	if (err != LM3559_ALLOWED_R_BYTES)
		return -EINVAL;

	return 0;
}

int lm3559_write_reg(struct lm3559_data *torch_data, uint8_t reg, uint8_t val)
{
	int bytes;
	int i = 0;
	uint8_t buf[LM3559_ALLOWED_W_BYTES] = { reg, val };

	/* If I2C client doesn't exist */
	if (torch_data->client == NULL) {
		pr_err("%s: null i2c client\n", __func__);
		return -EUNATCH;
	}

	do {
		bytes = i2c_master_send(torch_data->client, buf,
			LM3559_ALLOWED_W_BYTES);

		if (bytes != LM3559_ALLOWED_W_BYTES)
			msleep_interruptible(LM3559_I2C_RETRY_DELAY);
	} while ((bytes != LM3559_ALLOWED_W_BYTES) &&
		((++i) < LM3559_MAX_RW_RETRIES));

	if (bytes != LM3559_ALLOWED_W_BYTES) {
		pr_err("%s: i2c_master_send error\n", __func__);
		return -EINVAL;
	}

	return 0;
}

#ifdef DEBUG
static ssize_t ld_lm3559_registers_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = container_of(dev->parent, struct i2c_client,
						 dev);
	struct lm3559_data *flash_data = i2c_get_clientdata(client);
	unsigned i, n, reg_count;
	uint8_t value = 0;

	reg_count = sizeof(lm3559_regs) / sizeof(lm3559_regs[0]);
	for (i = 0, n = 0; i < reg_count; i++) {
		lm3559_read_reg(flash_data, lm3559_regs[i].reg, &value);
		n += scnprintf(buf + n, PAGE_SIZE - n,
			       "%-20s = 0x%02X\n",
			       lm3559_regs[i].name,
			       value);
	}

	return n;
}

static ssize_t ld_lm3559_registers_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct i2c_client *client = container_of(dev->parent,
		struct i2c_client, dev);
	struct lm3559_data *flash_data = i2c_get_clientdata(client);
	unsigned i, reg_count, value;
	int error;
	char name[30];

	if (count >= 30) {
		pr_err("%s:input too long\n", __func__);
		return -1;
	}

	if (sscanf(buf, "%s %x", name, &value) != 2) {
		pr_err("%s:unable to parse input\n", __func__);
		return -1;
	}

	reg_count = sizeof(lm3559_regs) / sizeof(lm3559_regs[0]);
	for (i = 0; i < reg_count; i++) {
		if (!strcmp(name, lm3559_regs[i].name)) {
			error = lm3559_write_reg(flash_data,
				lm3559_regs[i].reg,
				value);
			if (error) {
				pr_err("%s:Failed to write register %s\n",
					__func__, name);
				return -1;
			}
			return count;
		}
	}

	pr_err("%s:no such register %s\n", __func__, name);
	return -1;
}
static DEVICE_ATTR(registers, 0644, ld_lm3559_registers_show,
		ld_lm3559_registers_store);
#endif

int lm3559_init_registers(struct lm3559_data *torch_data)
{

	if (lm3559_write_reg(torch_data, LM3559_TORCH_BRIGHTNESS,
			     torch_data->pdata->torch_brightness_def) ||
		lm3559_write_reg(torch_data, LM3559_ADC_DELAY_REG,
			     torch_data->pdata->adc_delay_reg_def) ||
		lm3559_write_reg(torch_data, LM3559_FLASH_BRIGHTNESS,
			     torch_data->pdata->flash_brightness_def) ||
		lm3559_write_reg(torch_data, LM3559_FLASH_DURATION,
			     torch_data->pdata->flash_duration_def) ||
		lm3559_write_reg(torch_data, LM3559_CONFIG_REG_1,
			     torch_data->pdata->config_reg_1_def) ||
		lm3559_write_reg(torch_data, LM3559_CONFIG_REG_2,
			     torch_data->pdata->config_reg_2_def) ||
		lm3559_write_reg(torch_data, LM3559_VIN_MONITOR,
			     torch_data->pdata->vin_monitor_def) ||
		lm3559_write_reg(torch_data, LM3559_GPIO_REG,
			     torch_data->pdata->gpio_reg_def) ||
		lm3559_write_reg(torch_data, LM3559_FLAG_REG,
			     torch_data->pdata->flag_reg_def) ||
		lm3559_write_reg(torch_data, LM3559_PRIVACY_REG,
			     torch_data->pdata->privacy_reg_def) ||
		lm3559_write_reg(torch_data, LM3559_MSG_IND_REG,
			     torch_data->pdata->msg_ind_reg_def) ||
		lm3559_write_reg(torch_data, LM3559_MSG_BLINK_REG,
			     torch_data->pdata->msg_ind_blink_reg_def) ||
		lm3559_write_reg(torch_data, LM3559_PWM_REG,
			     torch_data->pdata->pwm_reg_def) ||
		lm3559_write_reg(torch_data, LM3559_ENABLE_REG,
			     torch_data->pdata->enable_reg_def)) {
		pr_err("%s:Register initialization failed\n", __func__);
		return -EIO;
	}
	return 0;
}
static int lm3559_enable_led(struct lm3559_data *torch_data, uint8_t enable_val)
{
	int err = 0;
	uint8_t val = 0;
	uint8_t temp_val;

	mutex_lock(&torch_data->enable_reg_lock);
	err = lm3559_read_reg(torch_data, LM3559_ENABLE_REG,
		&val);
	if (err) {
		pr_err("%s: Reading 0x%X failed %i\n",
			__func__, LM3559_ENABLE_REG, err);
		goto unlock_and_return;
	}

	temp_val = (val | enable_val);
	err = lm3559_write_reg(torch_data, LM3559_ENABLE_REG,
		temp_val);
	if (err) {
		pr_err("%s: Writing to 0x%X failed %i\n",
			__func__, LM3559_ENABLE_REG, err);
	}

unlock_and_return:
	mutex_unlock(&torch_data->enable_reg_lock);
	return err;
}

static int lm3559_disable_led(struct lm3559_data *torch_data,
		uint8_t enable_val)
{
	int err = 0;
	uint8_t val = 0;
	uint8_t temp_val;

	mutex_lock(&torch_data->enable_reg_lock);
	err = lm3559_read_reg(torch_data, LM3559_ENABLE_REG,
		&val);
	if (err) {
		pr_err("%s: Reading 0x%X failed %i\n",
			__func__, LM3559_ENABLE_REG, err);
		goto unlock_and_return;
	}
	temp_val = (val & enable_val);
	err = lm3559_write_reg(torch_data, LM3559_ENABLE_REG,
		temp_val);
	if (err) {
		pr_err("%s: Writing to 0x%X failed %i\n",
			__func__, LM3559_ENABLE_REG, err);
	}

unlock_and_return:
	mutex_unlock(&torch_data->enable_reg_lock);
	return err;
}
static int lm3559_privacy_write(struct lm3559_data *torch_data)
{
	int err = 0;
	uint8_t privacy_light_val;
	uint8_t err_flags;

	err = lm3559_read_reg(torch_data, LM3559_FLAG_REG, &err_flags);
	if (err) {
		pr_err("%s: Reading the status failed for %i\n",
			__func__, err);
		return -EIO;
	}

	if (torch_data->pdata->flags & LM3559_ERROR_CHECK) {
		if (err_flags & (VOLTAGE_MONITOR_FAULT |
			THERMAL_MONITOR_FAULT |	LED_FAULT |
			THERMAL_SHUTDOWN)) {
				pr_err("%s: Error indicated by the chip 0x%X\n",
					__func__, err_flags);
				return err_flags;
		}
	}
	if (torch_data->privacy_light_brightness) {
		privacy_light_val = (torch_data->pdata->privacy_reg_def & 0xf8);
		privacy_light_val |= (torch_data->privacy_light_brightness /
				LM3559_PRIVACY_STEP);
		err = lm3559_write_reg(torch_data, LM3559_PRIVACY_REG,
			privacy_light_val);
		if (err) {
			pr_err("%s: Writing to 0x%X failed %i\n",
				__func__, LM3559_PRIVACY_REG, err);
			return -EIO;
		}
		err = lm3559_write_reg(torch_data, LM3559_PWM_REG,
			torch_data->pdata->pwm_val);
		if (err) {
			pr_err("%s: Writing to 0x%X failed %i\n",
				__func__, LM3559_PWM_REG, err);
			return -EIO;
		}
		err = lm3559_enable_led(torch_data,
			torch_data->pdata->privacy_enable_val);
	} else {
		err = lm3559_disable_led(torch_data, 0xc0);
	}

	return err;
}

static int lm3559_torch_write(struct lm3559_data *torch_data)
{
	int err = 0;
	int temp_val;
	int val = 0;;

	if (torch_data->torch_light_brightness) {
		temp_val = torch_data->torch_light_brightness /
			LM3559_TORCH_STEP;
		val |= temp_val << 3;
		val |= temp_val;

		err = lm3559_write_reg(torch_data, LM3559_TORCH_BRIGHTNESS,
			val);

		if (err) {
			pr_err("%s: Writing to 0x%X failed %i\n",
				__func__, LM3559_TORCH_BRIGHTNESS, err);
			return -EIO;
		}

		err = lm3559_write_reg(torch_data, LM3559_FLASH_DURATION,
			torch_data->pdata->flash_duration_def);
		if (err) {
			pr_err("%s: Writing to 0x%X failed %i\n",
				__func__, LM3559_FLASH_DURATION, err);
			return -EIO;
		}

		err = lm3559_write_reg(torch_data, LM3559_VIN_MONITOR,
					torch_data->pdata->vin_monitor_def);
		if (err) {
			pr_err("%s: Writing to 0x%X failed %i\n",
				__func__, LM3559_VIN_MONITOR, err);
			return -EIO;
		}

		err = lm3559_enable_led(torch_data,
			torch_data->pdata->torch_enable_val);
	} else {
		err = lm3559_disable_led(torch_data, 0xc0);
	}

	return err;
}

static int lm3559_strobe_write(struct lm3559_data *torch_data)
{
	int err;
	uint8_t err_flags;
	uint8_t strobe_brightness;
	int val;

	err = lm3559_read_reg(torch_data, LM3559_FLAG_REG, &err_flags);
	if (err) {
		pr_err("%s: Reading the status failed for %i\n",
			__func__, err);
		return -EIO;
	}
	if (torch_data->pdata->flags & LM3559_ERROR_CHECK) {
		if (err_flags & (VOLTAGE_MONITOR_FAULT |
			THERMAL_MONITOR_FAULT | LED_FAULT |
			THERMAL_SHUTDOWN)) {
			pr_err("%s: Error indicated by the chip 0x%X\n",
				__func__, err_flags);
			return err_flags;
		}
	}
	if (torch_data->camera_strobe_brightness) {
		val = torch_data->camera_strobe_brightness / LM3559_STROBE_STEP;
		strobe_brightness = val << 4;
		strobe_brightness |= val ;

		err = lm3559_write_reg(torch_data, LM3559_FLASH_BRIGHTNESS,
			torch_data->camera_strobe_brightness);
		if (err) {
			pr_err("%s: Writing to 0x%X failed %i\n",
				__func__, LM3559_FLASH_BRIGHTNESS, err);
			return -EIO;
		}

		err = lm3559_write_reg(torch_data, LM3559_TORCH_BRIGHTNESS,
			torch_data->pdata->torch_brightness_def);
		if (err) {
			pr_err("%s: Writing to 0x%X failed %i\n",
				__func__, LM3559_TORCH_BRIGHTNESS, err);
			return -EIO;
		}

		err = lm3559_write_reg(torch_data, LM3559_FLASH_DURATION,
			torch_data->pdata->flash_duration_def);
		if (err) {
			pr_err("%s: Writing to 0x%X failed %i\n",
				__func__, LM3559_FLASH_DURATION, err);
			return -EIO;
		}

		err = lm3559_write_reg(torch_data, LM3559_VIN_MONITOR,
				torch_data->pdata->vin_monitor_def);
		if (err) {
			pr_err("%s: Writing to 0x%X failed %i\n",
				__func__, LM3559_VIN_MONITOR, err);
			return -EIO;
		}

		err = lm3559_enable_led(torch_data,
			torch_data->pdata->flash_enable_val);
	} else {
		err = lm3559_disable_led(torch_data, 0xc0);
	}

	return err;
}

static void lm3559_rgb_work(struct work_struct *work)
{
	int err;
	uint8_t val = 0;
	uint8_t temp_val;
	uint8_t rgb_val;

	struct lm3559_data *msg_ind_data =
		container_of(work, struct lm3559_data, wq);

	if (msg_ind_data->msg_ind_light_brightness) {
		if (msg_ind_data->blink_val) {
			err = lm3559_write_reg(msg_ind_data,
				LM3559_MSG_BLINK_REG,
				msg_ind_data->pdata->msg_ind_blink_val);
			if (err) {
				pr_err("%s: Reading 0x%X failed %i\n",
					__func__, LM3559_MSG_BLINK_REG, err);
				return;
			}
		}

		rgb_val = msg_ind_data->pdata->msg_ind_val;
		rgb_val |= (msg_ind_data->msg_ind_light_brightness /
			LM3559_RGB_STEP);

		err = lm3559_write_reg(msg_ind_data, LM3559_MSG_IND_REG,
			rgb_val);
		if (err) {
			pr_err("%s: Writing reg 0x%X failed %i\n",
				__func__, LM3559_MSG_IND_REG, err);
			return;
		}

		if (msg_ind_data->blink_val) {
			if (lm3559_debug)
				pr_info("%s: Blink MSG LED\n", __func__);
			val = 0x80;
		}

		temp_val = (val | 0x40);
		err = lm3559_enable_led(msg_ind_data, temp_val);

	} else {
		if (msg_ind_data->blink_val == 0) {
			if (lm3559_debug)
				pr_info("%s: Turn off blink MSG LED\n",
					__func__);
			val = 0x7f;
		} else {

			val = 0x3f;
		}
		err = lm3559_disable_led(msg_ind_data, val);
	}
}

/* This is a dummy interface for the LED class this will clear
the error flag register */
static void lm3559_brightness_set(struct led_classdev *led_cdev,
				  enum led_brightness value)
{
	int err;
	uint8_t err_flags;

	struct lm3559_data *torch_data =
	    container_of(led_cdev, struct lm3559_data, led_dev);

	err = lm3559_read_reg(torch_data, LM3559_FLAG_REG, &err_flags);
	if (err) {
		pr_err("%s: Reading the status failed for %i\n", __func__,
		       err);
		return;
	}
	return;
}

static ssize_t lm3559_strobe_err_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int err;
	uint8_t err_flags;
	struct i2c_client *client = container_of(dev->parent,
						 struct i2c_client, dev);
	struct lm3559_data *torch_data = i2c_get_clientdata(client);

	err = lm3559_read_reg(torch_data, LM3559_FLAG_REG, &err_flags);
	if (err) {
		pr_err("%s: Reading the status failed for %i\n",
			__func__, err);
		return -EIO;
	}

	sprintf(buf, "%d\n", (err_flags & TX1_INTERRUPT_FAULT));

	return sizeof(buf);
}
static DEVICE_ATTR(strobe_err, 0644, lm3559_strobe_err_show, NULL);

static ssize_t lm3559_torch_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = container_of(dev->parent,
						 struct i2c_client, dev);
	struct lm3559_data *torch_data = i2c_get_clientdata(client);

	sprintf(buf, "%d\n", torch_data->flash_light_brightness);

	return sizeof(buf);
}


static ssize_t lm3559_torch_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	int err = 0;
	unsigned long torch_val = LED_OFF;

	struct i2c_client *client = container_of(dev->parent,
						 struct i2c_client, dev);
	struct lm3559_data *torch_data = i2c_get_clientdata(client);

	err = strict_strtoul(buf, 10, &torch_val);
	if (err) {
		pr_err("%s: Invalid parameter sent\n", __func__);
		return -1;
	}

	torch_data->torch_light_brightness = torch_val;
	err = lm3559_torch_write(torch_data);

	return err;

};
static DEVICE_ATTR(flash_light, 0644, lm3559_torch_show, lm3559_torch_store);

static void lm3559_spot_light_brightness_set(struct led_classdev *led_cdev,
				  enum led_brightness value)
{
	struct lm3559_data *torch_data =
	    container_of(led_cdev, struct lm3559_data, spotlight_dev);

	torch_data->torch_light_brightness = value;
	lm3559_torch_write(torch_data);
}

static ssize_t lm3559_strobe_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = container_of(dev->parent,
						 struct i2c_client, dev);
	struct lm3559_data *torch_data = i2c_get_clientdata(client);

	sprintf(buf, "%d\n", torch_data->camera_strobe_brightness);

	return sizeof(buf);
}

static ssize_t lm3559_strobe_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int err;
	unsigned long strobe_val = LED_OFF;

	struct i2c_client *client = container_of(dev->parent,
						 struct i2c_client, dev);
	struct lm3559_data *torch_data = i2c_get_clientdata(client);

	err = strict_strtoul(buf, 10, &strobe_val);
	if (err) {
		pr_err("%s: Invalid parameter sent\n", __func__);
		return -1;
	}

	torch_data->camera_strobe_brightness = strobe_val;
	err = lm3559_strobe_write(torch_data);

	return err;
}
static DEVICE_ATTR(camera_strobe, 0644, lm3559_strobe_show,
	lm3559_strobe_store);

static ssize_t lm3559_privacy_light_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = container_of(dev->parent,
						 struct i2c_client, dev);
	struct lm3559_data *torch_data = i2c_get_clientdata(client);

	sprintf(buf, "%d\n", torch_data->privacy_light_brightness);

	return sizeof(buf);
}

static ssize_t lm3559_privacy_light_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int err = 0;
	unsigned long privacy_val = LED_OFF;

	struct i2c_client *client = container_of(dev->parent,
						 struct i2c_client, dev);
	struct lm3559_data *torch_data = i2c_get_clientdata(client);

	err = strict_strtoul(buf, 10, &privacy_val);
	if (err) {
		pr_err("%s: Invalid parameter sent\n", __func__);
		return -1;
	}

	torch_data->privacy_light_brightness = privacy_val;
	err = lm3559_privacy_write(torch_data);

	return err;

};
static DEVICE_ATTR(privacy_light, 0644, lm3559_privacy_light_show,
		   lm3559_privacy_light_store);


static void msg_ind_red_set(struct led_classdev *led_cdev,
			    enum led_brightness value)
{
	struct lm3559_data *msg_ind_data =
	    container_of(led_cdev, struct lm3559_data,
			 msg_ind_red_class_dev);

	msg_ind_data->msg_ind_light_brightness = value;
	schedule_work(&msg_ind_data->wq);
}

static void msg_ind_green_set(struct led_classdev *led_cdev,
			    enum led_brightness value)
{
	struct lm3559_data *msg_ind_data =
	    container_of(led_cdev, struct lm3559_data,
			 msg_ind_green_class_dev);

	msg_ind_data->msg_ind_light_brightness = value;
	schedule_work(&msg_ind_data->wq);
}

static void msg_ind_blue_set(struct led_classdev *led_cdev,
			    enum led_brightness value)
{
	struct lm3559_data *msg_ind_data =
	    container_of(led_cdev, struct lm3559_data,
			 msg_ind_blue_class_dev);

	msg_ind_data->msg_ind_light_brightness = value;
	schedule_work(&msg_ind_data->wq);
}

static int msg_ind_red_blink(struct led_classdev *led_cdev,
		unsigned long *delay_on,
		unsigned long *delay_off)
{
	struct lm3559_data *msg_ind_data =
	    container_of(led_cdev, struct lm3559_data,
			 msg_ind_red_class_dev);

	msg_ind_data->blink_val = *delay_on;
	schedule_work(&msg_ind_data->wq);
	return 0;
}

static int msg_ind_green_blink(struct led_classdev *led_cdev,
		unsigned long *delay_on,
		unsigned long *delay_off)
{
	struct lm3559_data *msg_ind_data =
	    container_of(led_cdev, struct lm3559_data,
			 msg_ind_green_class_dev);

	msg_ind_data->blink_val = *delay_on;
	schedule_work(&msg_ind_data->wq);
	return 0;
}

static int msg_ind_blue_blink(struct led_classdev *led_cdev,
		unsigned long *delay_on,
		unsigned long *delay_off)
{
	struct lm3559_data *msg_ind_data =
	    container_of(led_cdev, struct lm3559_data,
			 msg_ind_blue_class_dev);

	msg_ind_data->blink_val = *delay_on;
	schedule_work(&msg_ind_data->wq);
	return 0;
}

static int lm3559_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct lm3559_platform_data *pdata = client->dev.platform_data;
	struct lm3559_data *torch_data;
	int err = -1;

	if (pdata == NULL) {
		dev_err(&client->dev, "platform data is NULL. exiting.\n");
		return -ENODEV;
	}

	if (!pdata->flags) {
		pr_err("%s: Device does not exist\n", __func__);
		return -ENODEV;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "client not i2c capable\n");
		return -ENODEV;
	}

	torch_data = kzalloc(sizeof(struct lm3559_data), GFP_KERNEL);
	if (torch_data == NULL) {
		dev_err(&client->dev, "kzalloc failed\n");
		return -ENOMEM;
	}

	torch_data->client = client;
	torch_data->pdata = pdata;

	mutex_init(&torch_data->enable_reg_lock);

	i2c_set_clientdata(client, torch_data);

	err = lm3559_init_registers(torch_data);
	if (err < 0)
		goto error2;

	INIT_WORK(&torch_data->wq, lm3559_rgb_work);

	torch_data->led_dev.name = LM3559_LED_DEV;
	torch_data->led_dev.brightness_set = lm3559_brightness_set;
	torch_data->led_dev.brightness = LED_OFF;
	torch_data->led_dev.max_brightness = 255;
	err = led_classdev_register((struct device *)
				    &client->dev, &torch_data->led_dev);
	if (err < 0) {
		err = -ENODEV;
		pr_err("%s: Register led class failed: %d\n",
			__func__, err);
		goto error3;
	}

	if (torch_data->pdata->flags & LM3559_TORCH) {
		if (lm3559_debug)
			pr_info("%s: Creating Torch\n", __func__);
		err = device_create_file(torch_data->led_dev.dev,
					 &dev_attr_flash_light);
		if (err < 0) {
			err = -ENODEV;
			pr_err("%s:File device creation failed: %d\n",
				__func__, err);
			goto error4;
		}
	}

	if (torch_data->pdata->flags & LM3559_FLASH) {
		if (lm3559_debug)
			pr_info("%s: Creating Flash\n", __func__);
		err = device_create_file(torch_data->led_dev.dev,
					 &dev_attr_camera_strobe);
		if (err < 0) {
			err = -ENODEV;
			pr_err("%s:File device creation failed: %d\n",
				__func__, err);
			goto error5;
		}
		err = device_create_file(torch_data->led_dev.dev,
					 &dev_attr_strobe_err);
		if (err < 0) {
			err = -ENODEV;
			pr_err("%s:File device creation failed: %d\n",
				__func__, err);
			goto error6;
		}
	}

	if (torch_data->pdata->flags & LM3559_PRIVACY) {
		if (lm3559_debug)
			pr_info("%s: Creating Privacy\n", __func__);
		err = device_create_file(torch_data->led_dev.dev,
					 &dev_attr_privacy_light);
		if (err < 0) {
			err = -ENODEV;
			pr_err("%s:File device creation failed: %d\n",
				__func__, err);
			goto error7;
		}
	}

	if (torch_data->pdata->flags & LM3559_FLASH_LIGHT) {
		if (lm3559_debug)
			pr_info("%s: Creating Spotlight\n", __func__);
		torch_data->spotlight_dev.name = LM3559_LED_SPOTLIGHT;
		torch_data->spotlight_dev.brightness_set =
			lm3559_spot_light_brightness_set;
		torch_data->spotlight_dev.brightness = LED_OFF;
		torch_data->spotlight_dev.max_brightness = 255;

		err = led_classdev_register((struct device *)
			&client->dev, &torch_data->spotlight_dev);
		if (err < 0) {
			err = -ENODEV;
			pr_err("%s: Register led class failed: %d\n",
				__func__, err);
			goto error6;
		}
	}

	if (torch_data->pdata->flags & LM3559_MSG_IND_RED) {
		if (lm3559_debug)
			pr_info("%s: Creating MSG Red Indication\n",
			__func__);
		torch_data->msg_ind_red_class_dev.name = "red";
		torch_data->msg_ind_red_class_dev.brightness_set =
			msg_ind_red_set;
		torch_data->msg_ind_red_class_dev.blink_set =
			msg_ind_red_blink;
		torch_data->msg_ind_red_class_dev.brightness = LED_OFF;
		torch_data->msg_ind_red_class_dev.max_brightness = 255;

		err = led_classdev_register((struct device *)
			&client->dev, &torch_data->msg_ind_red_class_dev);
		if (err < 0) {
			pr_err("%s:Register Red LED class failed\n",
				__func__);
			goto err_reg_red_class_failed;
		}
	}

	if (torch_data->pdata->flags & LM3559_MSG_IND_GREEN) {
		if (lm3559_debug)
			pr_info("%s: Creating MSG Green Indication\n",
			__func__);
		torch_data->msg_ind_green_class_dev.name = "green";
		torch_data->msg_ind_green_class_dev.brightness_set =
			msg_ind_green_set;
		torch_data->msg_ind_green_class_dev.blink_set =
			msg_ind_green_blink;
		torch_data->msg_ind_red_class_dev.brightness = LED_OFF;
		torch_data->msg_ind_red_class_dev.max_brightness = 255;
		err = led_classdev_register((struct device *)
			&client->dev, &torch_data->msg_ind_green_class_dev);
		if (err < 0) {
			pr_err("%s: Register Green LED class failed\n",
				__func__);
			goto err_reg_green_class_failed;
		}
	}

	if (torch_data->pdata->flags & LM3559_MSG_IND_BLUE) {
		if (lm3559_debug)
			pr_info("%s: Creating MSG Blue Indication\n",
			__func__);
		torch_data->msg_ind_blue_class_dev.name = "blue";
		torch_data->msg_ind_blue_class_dev.brightness_set =
			msg_ind_blue_set;
		torch_data->msg_ind_blue_class_dev.blink_set =
			msg_ind_blue_blink;
		torch_data->msg_ind_blue_class_dev.brightness = LED_OFF;
		torch_data->msg_ind_blue_class_dev.max_brightness = 255;

		err = led_classdev_register((struct device *)
			&client->dev, &torch_data->msg_ind_blue_class_dev);
		if (err < 0) {
			pr_err("%s: Register Blue LED class failed\n",
				__func__);
			goto err_reg_blue_class_failed;
		}
	}
#ifdef DEBUG
	err = device_create_file(torch_data->led_dev.dev, &dev_attr_registers);
	if (err < 0)
		pr_err("%s:File device creation failed: %d\n", __func__, err);
#endif

	return 0;

err_reg_blue_class_failed:
	if (torch_data->pdata->flags & LM3559_MSG_IND_GREEN)
		led_classdev_unregister(&torch_data->msg_ind_green_class_dev);
err_reg_green_class_failed:
	if (torch_data->pdata->flags & LM3559_MSG_IND_RED)
		led_classdev_unregister(&torch_data->msg_ind_red_class_dev);
err_reg_red_class_failed:
	if (torch_data->pdata->flags & LM3559_PRIVACY)
		device_remove_file(torch_data->led_dev.dev,
			&dev_attr_privacy_light);
error7:
	if (torch_data->pdata->flags & LM3559_FLASH_LIGHT)
		led_classdev_unregister(&torch_data->spotlight_dev);
	if (torch_data->pdata->flags & LM3559_FLASH)
		device_remove_file(torch_data->led_dev.dev,
			&dev_attr_strobe_err);
error6:
	if (torch_data->pdata->flags & LM3559_FLASH)
		device_remove_file(torch_data->led_dev.dev,
			&dev_attr_camera_strobe);
error5:
	if (torch_data->pdata->flags & LM3559_TORCH)
		device_remove_file(torch_data->led_dev.dev,
			&dev_attr_flash_light);
error4:
	led_classdev_unregister(&torch_data->led_dev);
error3:
error2:
	kfree(torch_data);
	return err;
}

static int lm3559_remove(struct i2c_client *client)
{
	struct lm3559_data *torch_data = i2c_get_clientdata(client);

	if (torch_data->pdata->flags & LM3559_FLASH) {
		device_remove_file(torch_data->led_dev.dev,
				&dev_attr_camera_strobe);
		device_remove_file(torch_data->led_dev.dev,
				&dev_attr_strobe_err);
	}
	if (torch_data->pdata->flags & LM3559_TORCH)
		device_remove_file(torch_data->led_dev.dev,
				&dev_attr_flash_light);

	led_classdev_unregister(&torch_data->led_dev);

	if (torch_data->pdata->flags & LM3559_FLASH_LIGHT)
		led_classdev_unregister(&torch_data->spotlight_dev);

	if (torch_data->pdata->flags & LM3559_PRIVACY)
		device_remove_file(torch_data->led_dev.dev,
				&dev_attr_privacy_light);

	if (torch_data->pdata->flags & LM3559_MSG_IND_RED)
		led_classdev_unregister(&torch_data->msg_ind_red_class_dev);

	if (torch_data->pdata->flags & LM3559_MSG_IND_GREEN)
		led_classdev_unregister(&torch_data->msg_ind_green_class_dev);

	if (torch_data->pdata->flags & LM3559_MSG_IND_BLUE)
		led_classdev_unregister(&torch_data->msg_ind_blue_class_dev);

#ifdef DEBUG
	device_remove_file(torch_data->led_dev.dev, &dev_attr_registers);
#endif
	kfree(torch_data->pdata);
	kfree(torch_data);
	return 0;
}

static const struct i2c_device_id lm3559_id[] = {
	{LM3559_NAME, 0},
	{}
};

static struct i2c_driver lm3559_i2c_driver = {
	.probe = lm3559_probe,
	.remove = lm3559_remove,
	.id_table = lm3559_id,
	.driver = {
		.name = LM3559_NAME,
		.owner = THIS_MODULE,
	},
};

static int __init lm3559_init(void)
{
	return i2c_add_driver(&lm3559_i2c_driver);
}

static void lm3559_exit(void)
{
	i2c_del_driver(&lm3559_i2c_driver);
}

module_init(lm3559_init);
module_exit(lm3559_exit);

/****************************************************************************/

MODULE_DESCRIPTION("Lighting driver for LM3559");
MODULE_AUTHOR("MOTOROLA");
MODULE_LICENSE("GPL");
