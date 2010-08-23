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
#include <linux/leds-lp8550.h>
#include <linux/slab.h>
#include <linux/types.h>

#define DEBUG

#define LD_LP8550_ON_OFF_MASK 0xFE

#define LD_LP8550_ALLOWED_R_BYTES 1
#define LD_LP8550_ALLOWED_W_BYTES 2
#define LD_LP8550_MAX_RW_RETRIES 5
#define LD_LP8550_I2C_RETRY_DELAY 10

#define LP8550_BRIGHTNESS_CTRL	0x00
#define LP8550_DEVICE_CTRL	0x01
#define LP8550_FAULT		0x02
#define LP8550_CHIP_ID		0x03
#define LP8550_DIRECT_CTRL	0x04
#define LP8550_TEMP_MSB		0x05
#define LP8550_TEMP_LSB		0x06

/* EEPROM Register address */
#define LP8550_EEPROM_CTRL	0x72
#define LP8550_EEPROM_A0	0xa0
#define LP8550_EEPROM_A1	0xa1
#define LP8550_EEPROM_A2	0xa2
#define LP8550_EEPROM_A3	0xa3
#define LP8550_EEPROM_A4	0xa4
#define LP8550_EEPROM_A5	0xa5
#define LP8550_EEPROM_A6	0xa6
#define LP8550_EEPROM_A7	0xa7


struct lp8550_data {
	struct led_classdev led_dev;
	struct i2c_client *client;
	struct work_struct wq;
	struct lp8550_platform_data *led_pdata;
	uint8_t last_requested_brightness;
	int brightness;
};

#ifdef DEBUG
struct lp8550_reg {
	const char *name;
	uint8_t reg;
} lp8550_regs[] = {
	{ "BRIGHTNESS_CTRL",	LP8550_BRIGHTNESS_CTRL },
	{ "DEV_CTRL",		LP8550_DEVICE_CTRL },
	{ "FAULT",		LP8550_FAULT },
	{ "CHIP_ID",		LP8550_CHIP_ID },
	{ "DIRECT_CTRL",	LP8550_DIRECT_CTRL },
	{ "EEPROM_CTRL",	LP8550_EEPROM_CTRL },
	{ "EEPROM_A0",		LP8550_EEPROM_A0 },
	{ "EEPROM_A1",		LP8550_EEPROM_A1 },
	{ "EEPROM_A2",		LP8550_EEPROM_A2 },
	{ "EEPROM_A3",		LP8550_EEPROM_A3 },
	{ "EEPROM_A4",		LP8550_EEPROM_A4 },
	{ "EEPROM_A5",		LP8550_EEPROM_A5 },
	{ "EEPROM_A6",		LP8550_EEPROM_A6 },
	{ "EEPROM_A7",		LP8550_EEPROM_A7 },
};
#endif

static uint32_t lp8550_debug;
module_param_named(als_debug, lp8550_debug, uint, 0664);

static int lp8550_read_reg(struct lp8550_data *led_data, uint8_t reg,
		   uint8_t *value)
{
	int error = 0;
	int i = 0;
	uint8_t dest_buffer;

	if (!value) {
		pr_err("%s: invalid value pointer\n", __func__);
		return -EINVAL;
	}
	do {
		dest_buffer = reg;
		error = i2c_master_send(led_data->client, &dest_buffer, 1);
		if (error == 1) {
			error = i2c_master_recv(led_data->client,
				&dest_buffer, LD_LP8550_ALLOWED_R_BYTES);
		}
		if (error != LD_LP8550_ALLOWED_R_BYTES) {
			pr_err("%s: read[%i] failed: %d\n", __func__, i, error);
			msleep(LD_LP8550_I2C_RETRY_DELAY);
		}
	} while ((error != LD_LP8550_ALLOWED_R_BYTES) &&
			((++i) < LD_LP8550_MAX_RW_RETRIES));

	if (error == LD_LP8550_ALLOWED_R_BYTES) {
		error = 0;
		*value = dest_buffer;
	}

	return error;
}

static int lp8550_write_reg(struct lp8550_data *led_data, uint8_t reg,
			uint8_t value)
{
	uint8_t buf[LD_LP8550_ALLOWED_W_BYTES] = { reg, value };
	int bytes;
	int i = 0;

	do {
		bytes = i2c_master_send(led_data->client, buf,
					LD_LP8550_ALLOWED_W_BYTES);

		if (bytes != LD_LP8550_ALLOWED_W_BYTES) {
			pr_err("%s: write %d failed: %d\n", __func__, i, bytes);
			msleep(LD_LP8550_I2C_RETRY_DELAY);
		}
	} while ((bytes != (LD_LP8550_ALLOWED_W_BYTES))
		 && ((++i) < LD_LP8550_MAX_RW_RETRIES));

	if (bytes != LD_LP8550_ALLOWED_W_BYTES) {
		pr_err("%s: i2c_master_send error\n", __func__);
		return -EINVAL;
	}
	return 0;
}

static int ld_lp8550_init_registers(struct lp8550_data *led_data)
{
	unsigned i, n, reg_count, reg_addr;
	uint8_t value = 0;

	/* Check the EEPROM values and update if neccessary */
	reg_count = 8;
	reg_addr = LP8550_EEPROM_A0;
	for (i = 0, n = 0; i < reg_count; i++) {
		lp8550_read_reg(led_data, reg_addr, &value);
		if (lp8550_debug)
			pr_info("%s:Register 0x%x value 0x%X\n", __func__,
				reg_addr, value);
		if (value != led_data->led_pdata->eeprom_table[n].eeprom_data) {
			if (lp8550_debug)
				pr_info("%s:Writing 0x%x to 0x%X\n", __func__,
				led_data->led_pdata->eeprom_table[n].eeprom_data,
				reg_addr);
			if (lp8550_write_reg(led_data, LP8550_DEVICE_CTRL,
					0x05))
				pr_err("%s:Register initialization failed\n",
					__func__);
			if (lp8550_write_reg(led_data, reg_addr,
				led_data->led_pdata->eeprom_table[n].eeprom_data))
				pr_err("%s:Register initialization failed\n",
					__func__);
			if (lp8550_write_reg(led_data, LP8550_EEPROM_CTRL,
					0x04))
				pr_err("%s:Register initialization failed\n",
					__func__);
			if (lp8550_write_reg(led_data, LP8550_EEPROM_CTRL,
					0x02))
				pr_err("%s:Register initialization failed\n",
					__func__);
			msleep(200);
			if (lp8550_write_reg(led_data, LP8550_EEPROM_CTRL,
					0x00))
				pr_err("%s:Register initialization failed\n",
					__func__);
		}
		n++;
		reg_addr++;
	}

	if (lp8550_write_reg(led_data, LP8550_DEVICE_CTRL,
		led_data->led_pdata->dev_ctrl_config)) {
		pr_err("%s:Register initialization failed\n", __func__);
		return -EINVAL;
	}
	return 0;
}

static void ld_lp8550_brightness_set(struct led_classdev *led_cdev,
				     enum led_brightness brightness)
{
	struct lp8550_data *led_data =
		container_of(led_cdev, struct lp8550_data, led_dev);

	if (brightness > 255)
		brightness = 255;

	led_data->brightness = brightness;
	schedule_work(&led_data->wq);
}
EXPORT_SYMBOL(ld_lp8550_brightness_set);

static void lp8550_brightness_work(struct work_struct *work)
{
	int brightness = 0;
	int error = 0;
	struct lp8550_data *led_data =
		container_of(work, struct lp8550_data, wq);

	brightness = led_data->brightness;

	if (lp8550_debug)
		pr_info("%s: setting brightness to %i\n",
			__func__, brightness);

	if (brightness == LED_OFF) {
		if (lp8550_write_reg(led_data, LP8550_DEVICE_CTRL,
				(led_data->led_pdata->dev_ctrl_config &
				LD_LP8550_ON_OFF_MASK))) {
			pr_err("%s:writing failed while setting brightness:%d\n",
				__func__, error);
		}
	} else {
		if (lp8550_write_reg(led_data, LP8550_DEVICE_CTRL,
				led_data->led_pdata->dev_ctrl_config | 0x01)) {
			pr_err("%s:writing failed while setting brightness:%d\n",
				__func__, error);
		}
		if (lp8550_write_reg(led_data, LP8550_BRIGHTNESS_CTRL, brightness)) {
				pr_err("%s:Failed to set brightness:%d\n",
				__func__, error);
		}
		led_data->last_requested_brightness = brightness;
	}
}

#ifdef DEBUG
static ssize_t ld_lp8550_registers_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = container_of(dev->parent, struct i2c_client,
						 dev);
	struct lp8550_data *led_data = i2c_get_clientdata(client);
	unsigned i, n, reg_count;
	uint8_t value = 0;

	reg_count = sizeof(lp8550_regs) / sizeof(lp8550_regs[0]);
	for (i = 0, n = 0; i < reg_count; i++) {
		lp8550_read_reg(led_data, lp8550_regs[i].reg, &value);
		n += scnprintf(buf + n, PAGE_SIZE - n,
			       "%-20s = 0x%02X\n",
			       lp8550_regs[i].name,
			       value);
	}

	return n;
}

static ssize_t ld_lp8550_registers_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct i2c_client *client = container_of(dev->parent, struct i2c_client,
						 dev);
	struct lp8550_data *led_data = i2c_get_clientdata(client);
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

	reg_count = sizeof(lp8550_regs) / sizeof(lp8550_regs[0]);
	for (i = 0; i < reg_count; i++) {
		if (!strcmp(name, lp8550_regs[i].name)) {
			error = lp8550_write_reg(led_data,
				lp8550_regs[i].reg,
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
static DEVICE_ATTR(registers, 0644, ld_lp8550_registers_show,
		ld_lp8550_registers_store);
#endif

static int ld_lp8550_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct lp8550_platform_data *pdata = client->dev.platform_data;
	struct lp8550_data *led_data;
	int error = 0;

	if (pdata == NULL) {
		pr_err("%s: platform data required\n", __func__);
		return -ENODEV;
	} else if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s:I2C_FUNC_I2C not supported\n", __func__);
		return -ENODEV;
	}

	led_data = kzalloc(sizeof(struct lp8550_data), GFP_KERNEL);
	if (led_data == NULL) {
		error = -ENOMEM;
		goto err_alloc_data_failed;
	}

	led_data->client = client;

	led_data->led_dev.name = LD_LP8550_LED_DEV;
	led_data->led_dev.brightness_set = ld_lp8550_brightness_set;
	led_data->led_pdata = client->dev.platform_data;

	i2c_set_clientdata(client, led_data);

	error = ld_lp8550_init_registers(led_data);
	if (error < 0) {
		pr_err("%s: Register Initialization failed: %d\n",
		       __func__, error);
		error = -ENODEV;
		goto err_reg_init_failed;
	}

	error = lp8550_write_reg(led_data, LP8550_BRIGHTNESS_CTRL,
				 pdata->power_up_brightness);
	if (error) {
		pr_err("%s:Setting power up brightness failed %d\n",
		       __func__, error);
		error = -ENODEV;
		goto err_reg_init_failed;
	}

	INIT_WORK(&led_data->wq, lp8550_brightness_work);

	error = led_classdev_register((struct device *) &client->dev,
				&led_data->led_dev);
	if (error < 0) {
		pr_err("%s: Register led class failed: %d\n", __func__, error);
		error = -ENODEV;
		goto err_class_reg_failed;
	}

#ifdef DEBUG
	error = device_create_file(led_data->led_dev.dev, &dev_attr_registers);
	if (error < 0) {
		pr_err("%s:File device creation failed: %d\n", __func__, error);
	}
#endif

	return 0;

err_class_reg_failed:
err_reg_init_failed:
	kfree(led_data);
err_alloc_data_failed:
	return error;
}

static int ld_lp8550_remove(struct i2c_client *client)
{
	struct lp8550_data *led_data = i2c_get_clientdata(client);

#ifdef DEBUG
	device_remove_file(led_data->led_dev.dev, &dev_attr_registers);
#endif
	led_classdev_unregister(&led_data->led_dev);
	kfree(led_data);
	return 0;
}

static int lp8550_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct lp8550_data *led_data = i2c_get_clientdata(client);

	if (lp8550_debug)
		pr_info("%s: Suspending\n", __func__);

	lp8550_write_reg(led_data, LP8550_DEVICE_CTRL,
        led_data->led_pdata->dev_ctrl_config & LD_LP8550_ON_OFF_MASK);

	return 0;
}

static int lp8550_resume(struct i2c_client *client)
{
	struct lp8550_data *led_data = i2c_get_clientdata(client);

	if (lp8550_debug)
		pr_info("%s: Resuming with brightness %i\n",
		__func__, led_data->brightness);

	lp8550_write_reg(led_data, LP8550_DEVICE_CTRL,
        led_data->led_pdata->dev_ctrl_config | 0x01);

	return 0;
}

static const struct i2c_device_id lp8550_id[] = {
	{LD_LP8550_NAME, 0},
	{}
};

static struct i2c_driver ld_lp8550_i2c_driver = {
	.probe = ld_lp8550_probe,
	.remove = ld_lp8550_remove,
	.suspend	= lp8550_suspend,
	.resume		= lp8550_resume,
	.id_table = lp8550_id,
	.driver = {
		   .name = LD_LP8550_NAME,
		   .owner = THIS_MODULE,
	},
};

static int __init ld_lp8550_init(void)
{
	return i2c_add_driver(&ld_lp8550_i2c_driver);
}

static void __exit ld_lp8550_exit(void)
{
	i2c_del_driver(&ld_lp8550_i2c_driver);

}

module_init(ld_lp8550_init);
module_exit(ld_lp8550_exit);

MODULE_DESCRIPTION("Lighting driver for LP8550");
MODULE_AUTHOR("Dan Murphy D.Murphy@Motorola.com");
MODULE_LICENSE("GPL");
