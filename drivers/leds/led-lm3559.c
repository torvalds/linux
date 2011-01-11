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
#include <linux/err.h>

#include <linux/led-lm3559.h>

/* #define DEBUG */

#define LM3559_ALLOWED_R_BYTES   1
#define LM3559_ALLOWED_W_BYTES   2
#define LM3559_MAX_RW_RETRIES    5
#define LM3559_I2C_RETRY_DELAY  10
#define LM3559_TORCH_STEP	64
#define LM3559_STROBE_STEP	16
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
	struct led_classdev flash_dev;
	struct led_classdev torch_dev;
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

static int lm3559_read_reg(struct lm3559_data *torch_data,
				uint8_t reg, uint8_t* val)
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

static int lm3559_write_reg(struct lm3559_data *torch_data,
				uint8_t reg, uint8_t val)
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
	if (lm3559_write_reg(torch_data, LM3559_TORCH_BRIGHTNESS, 0) ||
		lm3559_write_reg(torch_data, LM3559_ADC_DELAY_REG, 0) ||
		lm3559_write_reg(torch_data, LM3559_FLASH_BRIGHTNESS, 0) ||
		lm3559_write_reg(torch_data, LM3559_FLASH_DURATION,
			     torch_data->pdata->flash_duration_def) ||
		lm3559_write_reg(torch_data, LM3559_CONFIG_REG_1, 0x6C) ||
		lm3559_write_reg(torch_data, LM3559_CONFIG_REG_2, 0) ||
		lm3559_write_reg(torch_data, LM3559_VIN_MONITOR,
			torch_data->pdata->vin_monitor_def) ||
		lm3559_write_reg(torch_data, LM3559_GPIO_REG, 0) ||
		lm3559_write_reg(torch_data, LM3559_FLAG_REG, 0) ||
		lm3559_write_reg(torch_data, LM3559_PRIVACY_REG, 0x10) ||
		lm3559_write_reg(torch_data, LM3559_MSG_IND_REG, 0) ||
		lm3559_write_reg(torch_data, LM3559_MSG_BLINK_REG, 0) ||
		lm3559_write_reg(torch_data, LM3559_PWM_REG, 0) ||
		lm3559_write_reg(torch_data, LM3559_ENABLE_REG, 0)) {
		pr_err("%s:Register initialization failed\n", __func__);
		return -EIO;
	}
	return 0;
}

static int lm3559_check_led_error(struct lm3559_data *torch_data) {
	int err = 0;

	if (torch_data->pdata->flags & LM3559_FLAG_ERROR_CHECK) {

		uint8_t err_flags;
		err = lm3559_read_reg(torch_data, LM3559_FLAG_REG, &err_flags);
		if (err) {
			pr_err("%s: Reading the status failed for %i\n",
				__func__, err);
			return -EIO;
		}

		if (err_flags & (VOLTAGE_MONITOR_FAULT |
				THERMAL_MONITOR_FAULT |
				LED_FAULT |
				THERMAL_SHUTDOWN)) {
			pr_err("%s: Error indicated by the chip 0x%X\n",
				__func__, err_flags);
			err = -EIO;
		}
	}

	return err;
}

static int lm3559_flash_prepare(struct lm3559_data *torch_data)
{
	int err = lm3559_check_led_error(torch_data);
	if (err)
		return err;

	if (torch_data->flash_dev.brightness) {
		uint8_t strobe_brightness;
		uint val = torch_data->flash_dev.brightness - 1;
		strobe_brightness = val | (val << 4);

		err = lm3559_write_reg(torch_data, LM3559_FLASH_BRIGHTNESS,
			strobe_brightness);
		if (err) {
			pr_err("%s: Writing to 0x%X failed %i\n",
				__func__, LM3559_FLASH_BRIGHTNESS, err);
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

		/* setup flash for trigger by strobe pin:
		   enable LED1 and LED2, but do not enable current */
		err = lm3559_write_reg(torch_data, LM3559_ENABLE_REG, 0x18);

	} else {
		/* disable LED1 and LED2 and current */
		err = lm3559_write_reg(torch_data, LM3559_ENABLE_REG, 0);
	}

	if (err)
		pr_err("%s: Writing to 0x%X failed %i\n",
			__func__, LM3559_ENABLE_REG, err);

	return err;
}

static int lm3559_torch_enable(struct lm3559_data *torch_data)
{
	int err = lm3559_check_led_error(torch_data);
	if (err)
		return err;

	if (torch_data->torch_dev.brightness) {
		uint8_t torch_brightness;
		uint val = (torch_data->torch_dev.brightness - 1) & 0x3F;
		torch_brightness = val | (val << 3);

		err = lm3559_write_reg(torch_data, LM3559_TORCH_BRIGHTNESS,
			torch_brightness);
		if (err) {
			pr_err("%s: Writing to 0x%X failed %i\n",
				__func__, LM3559_TORCH_BRIGHTNESS, err);
			return -EIO;
		}

		err = lm3559_write_reg(torch_data, LM3559_VIN_MONITOR,
					torch_data->pdata->vin_monitor_def);
		if (err) {
			pr_err("%s: Writing to 0x%X failed %i\n",
				__func__, LM3559_VIN_MONITOR, err);
			return -EIO;
		}

		/* enable LED1 and LED2, enable current */
		err = lm3559_write_reg(torch_data, LM3559_ENABLE_REG, 0x1A);

	} else {
		/* disable LED1 and LED2 and current */
		err = lm3559_write_reg(torch_data, LM3559_ENABLE_REG, 0);
	}

	return err;
}

static void lm3559_flash_brightness_set(struct led_classdev *led_cdev,
					enum led_brightness value)
{
	struct lm3559_data *torch_data =
	    container_of(led_cdev, struct lm3559_data, flash_dev);
	lm3559_flash_prepare(torch_data);
}

static void lm3559_torch_brightness_set(struct led_classdev *led_cdev,
					enum led_brightness value)
{
	struct lm3559_data *torch_data =
	    container_of(led_cdev, struct lm3559_data, torch_dev);
	lm3559_torch_enable(torch_data);
}

static int lm3559_remove(struct i2c_client *client)
{
	struct lm3559_data *torch_data = i2c_get_clientdata(client);

	if (torch_data) {

		if (!IS_ERR_OR_NULL(torch_data->torch_dev.dev)) {
#ifdef DEBUG
			device_remove_file(torch_data->torch_dev.dev,
				&dev_attr_registers);
#endif
			led_classdev_unregister(&torch_data->torch_dev);
		}

		if (!IS_ERR_OR_NULL(torch_data->flash_dev.dev)) {
#ifdef DEBUG
			device_remove_file(torch_data->flash_dev.dev,
				&dev_attr_registers);
#endif
			led_classdev_unregister(&torch_data->flash_dev);
		}

		kfree(torch_data);
	}
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

	i2c_set_clientdata(client, torch_data);

	err = lm3559_init_registers(torch_data);
	if (err < 0)
		goto error;

	torch_data->flash_dev.name = "flash";
	torch_data->flash_dev.brightness_set = lm3559_flash_brightness_set;
	torch_data->flash_dev.brightness = LED_OFF;
	torch_data->flash_dev.max_brightness = LED_FULL;
	err = led_classdev_register((struct device *)
		&client->dev, &torch_data->flash_dev);
	if (err < 0) {
		pr_err("%s: Register flash led class failed: %d\n",
			__func__, err);
		goto error;
	}

#ifdef DEBUG
	err = device_create_file(torch_data->flash_dev.dev,
		&dev_attr_registers);
	if (err < 0)
		pr_err("%s:File device creation failed: %d\n", __func__, err);
#endif

	torch_data->torch_dev.name = "torch";
	torch_data->torch_dev.brightness_set = lm3559_torch_brightness_set;
	torch_data->torch_dev.brightness = LED_OFF;
	torch_data->torch_dev.max_brightness = LED_FULL;
	err = led_classdev_register((struct device *)
		&client->dev, &torch_data->torch_dev);
	if (err < 0) {
		pr_err("%s: Register torch led class failed: %d\n",
			__func__, err);
		goto error;
	}

#ifdef DEBUG
	err = device_create_file(torch_data->torch_dev.dev,
		&dev_attr_registers);
	if (err < 0)
		pr_err("%s:File device creation failed: %d\n", __func__, err);
#endif

	return 0;
error:
	lm3559_remove(client);
	return err;
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
