/* pca963x.c - 4-bit I2C-bus LED driver
 *
 * Copyright (C) 2008 HTC Corporation.
 * Author: Shan-Fu Chiou <sfchiou@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/leds.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

static uint8_t address[] = { 0x02, 0x03, 0x04 };
static DEFINE_SPINLOCK(pca963x_lock);

enum op_t {
	OP_SET_BLINK,
	OP_SET_GRPPWM,
	OP_SET_GRPFREQ,
	OP_SET_BLUE_BRIGHTNESS,
	OP_SET_GREEN_BRIGHTNESS,
	OP_SET_RED_BRIGHTNESS,
};

enum power_mode {
	MODE_SLEEP,
	MODE_NORMAL,
};

struct pca963x_t {
	uint8_t colors[3];
	uint8_t blink;
	uint8_t grppwm;
	uint8_t grpfreq;
};

struct pca963x_data {
	struct pca963x_t data;
	uint8_t dirty;
	uint8_t status;
	enum power_mode mode;
	struct work_struct work;
	struct i2c_client *client;
	struct led_classdev leds[3];	/* blue, green, red */
};

static ssize_t pca963x_blink_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pca963x_data *pca963x = i2c_get_clientdata(client);

	if (((pca963x->dirty >> OP_SET_BLINK) & 0x01))
		flush_scheduled_work();
	return sprintf(buf, "%u\n", pca963x->data.blink);
}

static ssize_t pca963x_blink_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pca963x_data *pca963x = i2c_get_clientdata(client);

	int val = -1;

	sscanf(buf, "%u", &val);
	if (val < 0 || val > 1)
		return -EINVAL;

	spin_lock(&pca963x_lock);
	pca963x->dirty |= 1 << OP_SET_BLINK;
	pca963x->data.blink = val;
	spin_unlock(&pca963x_lock);
	schedule_work(&pca963x->work);

	return count;
}

static DEVICE_ATTR(blink, 0644, pca963x_blink_show, pca963x_blink_store);

static ssize_t pca963x_grpfreq_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pca963x_data *pca963x = i2c_get_clientdata(client);

	if (((pca963x->dirty >> OP_SET_GRPFREQ) & 0x01))
		flush_scheduled_work();
	return sprintf(buf, "%u\n", pca963x->data.grpfreq);
}

static ssize_t pca963x_grpfreq_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pca963x_data *pca963x = i2c_get_clientdata(client);

	unsigned long val = simple_strtoul(buf, NULL, 10);

	if (val > 0xff)
		return -EINVAL;

	spin_lock(&pca963x_lock);
	pca963x->dirty |= 1 << OP_SET_GRPFREQ;
	pca963x->data.grpfreq = val;
	spin_unlock(&pca963x_lock);
	schedule_work(&pca963x->work);

	return count;
}

static DEVICE_ATTR(grpfreq, 0644, pca963x_grpfreq_show, pca963x_grpfreq_store);

static ssize_t pca963x_grppwm_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pca963x_data *pca963x = i2c_get_clientdata(client);

	if (((pca963x->dirty >> OP_SET_GRPPWM) & 0x01))
		flush_scheduled_work();
	return sprintf(buf, "%u\n", pca963x->data.grppwm);
}

static ssize_t pca963x_grppwm_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pca963x_data *pca963x = i2c_get_clientdata(client);

	unsigned long val = simple_strtoul(buf, NULL, 10);

	if (val > 0xff)
		return -EINVAL;

	spin_lock(&pca963x_lock);
	pca963x->dirty |= 1 << OP_SET_GRPPWM;
	pca963x->data.grppwm = val;
	spin_unlock(&pca963x_lock);
	schedule_work(&pca963x->work);

	return count;
}

static DEVICE_ATTR(grppwm, 0644, pca963x_grppwm_show, pca963x_grppwm_store);

static void led_brightness_set(struct led_classdev *led_cdev,
			       enum led_brightness brightness)
{
	struct pca963x_data *pca963x;
	int idx = 2;

	spin_lock(&pca963x_lock);
	if (!strcmp(led_cdev->name, "blue")) {
		idx = 0;
	} else if (!strcmp(led_cdev->name, "green")) {
		idx = 1;
	} else {
		idx = 2;
	}
	pca963x = container_of(led_cdev, struct pca963x_data, leds[idx]);
	pca963x->data.colors[idx] = brightness;
	pca963x->dirty |= (1 << (OP_SET_BLUE_BRIGHTNESS + idx));
	spin_unlock(&pca963x_lock);

	schedule_work(&pca963x->work);

}

static void pca963x_update_brightness(struct pca963x_data *pca963x, int idx,
				      int brightness)
{
	if (brightness > LED_OFF) {
		if (brightness == LED_FULL) {
			pca963x->status &= ~(1 << idx);
			pca963x->status |= (1 << (idx + 4));
		} else {
			pca963x->status |= (1 << idx);
			pca963x->status &= ~(1 << (idx + 4));
		}
	} else {
		pca963x->status &= ~(1 << idx);
		pca963x->status &= ~(1 << (idx + 4));
	}
	i2c_smbus_write_byte_data(pca963x->client, address[idx], brightness);
}

static void pca963x_work_func(struct work_struct *work)
{
	int ret;
	uint8_t dirty = 0;
	struct pca963x_t work_data;
	struct pca963x_data *pca963x =
	    container_of(work, struct pca963x_data, work);

	spin_lock(&pca963x_lock);
	work_data = pca963x->data;
	dirty = pca963x->dirty;
	pca963x->dirty = 0;
	spin_unlock(&pca963x_lock);

	ret = i2c_smbus_read_byte_data(pca963x->client, 0x00);
	/* check if should switch to normal mode */
	if (!pca963x->mode) {
		i2c_smbus_write_byte_data(pca963x->client, 0x00, 0x01);
		pca963x->mode = MODE_NORMAL;
		i2c_smbus_write_byte_data(pca963x->client, 0x08, 0xFF);
	}

	if ((dirty >> OP_SET_BLINK) & 0x01) {
		ret = i2c_smbus_read_byte_data(pca963x->client, 0x01);
		if (work_data.blink)	/* enable blinking */
			i2c_smbus_write_byte_data(pca963x->client, 0x01,
						  ret | 0x20);
		else {
			/* set group duty cycle control to default */
			i2c_smbus_write_byte_data(pca963x->client, 0x06, 0xFF);
			/* set group frequency to default */
			i2c_smbus_write_byte_data(pca963x->client, 0x07, 0x00);
			/* enable dimming */
			i2c_smbus_write_byte_data(pca963x->client, 0x01,
						  ret & 0xDF);
		}
	}

	if ((dirty >> OP_SET_GRPPWM) & 0x01) {
		i2c_smbus_write_byte_data(pca963x->client, 0x06,
					  work_data.grppwm);
	}

	if ((dirty >> OP_SET_GRPFREQ) & 0x01) {
		i2c_smbus_write_byte_data(pca963x->client, 0x07,
					  work_data.grpfreq);
	}

	if ((dirty >> OP_SET_BLUE_BRIGHTNESS) & 0x01)
		pca963x_update_brightness(pca963x, 0, work_data.colors[0]);

	if ((dirty >> OP_SET_GREEN_BRIGHTNESS) & 0x01)
		pca963x_update_brightness(pca963x, 1, work_data.colors[1]);

	if ((dirty >> OP_SET_RED_BRIGHTNESS) & 0x01)
		pca963x_update_brightness(pca963x, 2, work_data.colors[2]);

	/* check if could go to low power mode */
	if (((pca963x->status & 0x0F) == 0) && (!work_data.blink)) {
		i2c_smbus_write_byte_data(pca963x->client, 0x08, 0xAA);
		i2c_smbus_write_byte_data(pca963x->client, 0x00, 0x11);
		pca963x->mode = MODE_SLEEP;
	}
}

static void set_pca963x_default(struct i2c_client *client)
{
	i2c_smbus_write_byte_data(client, 0x00, 0x01);
	i2c_smbus_write_byte_data(client, 0x01, 0x00);
	/* set all LEDx brightness off */
	i2c_smbus_write_byte_data(client, address[0], LED_OFF);
	i2c_smbus_write_byte_data(client, address[1], LED_OFF);
	i2c_smbus_write_byte_data(client, address[2], LED_OFF);
	/* set group duty cycle control to default */
	i2c_smbus_write_byte_data(client, 0x06, 0xFF);
	/* set group frequency to default */
	i2c_smbus_write_byte_data(client, 0x07, 0x00);
	/*
	 * set LEDx individual brightness and group dimming/blinking
	 * can be controlled by * its PWMx register and GRPPWM registers.
	 */
	i2c_smbus_write_byte_data(client, 0x08, 0xFF);
	/* low power mode. oscillator off */
	i2c_smbus_write_byte_data(client, 0x00, 0x11);
}

static int pca963x_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int ret = 0;

	struct pca963x_data *pca963x;

	if (!i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_BYTE_DATA)) {
		ret = -ENODEV;
		goto exit;
	}

	pca963x = kzalloc(sizeof(struct pca963x_data), GFP_KERNEL);
	if (pca963x == NULL) {
		ret = -ENOMEM;
		goto err_alloc_failed;
	}

	INIT_WORK(&pca963x->work, pca963x_work_func);

	pca963x->client = client;

	pca963x->leds[0].name = "blue";
	pca963x->leds[0].brightness = LED_OFF;
	pca963x->leds[0].brightness_set = led_brightness_set;

	pca963x->leds[1].name = "green";
	pca963x->leds[1].brightness = LED_OFF;
	pca963x->leds[1].brightness_set = led_brightness_set;

	pca963x->leds[2].name = "red";
	pca963x->leds[2].brightness = LED_OFF;
	pca963x->leds[2].brightness_set = led_brightness_set;

	pca963x->dirty = 0;
	pca963x->status = 0;

	pca963x->data.colors[0] = LED_OFF;
	pca963x->data.colors[1] = LED_OFF;
	pca963x->data.colors[2] = LED_OFF;
	pca963x->data.blink = 0;
	pca963x->data.grppwm = 0;
	pca963x->data.grpfreq = 0;
	i2c_set_clientdata(client, pca963x);

	set_pca963x_default(client);
	pca963x->mode = MODE_SLEEP;

	/* blue */
	ret = led_classdev_register(&client->dev, &pca963x->leds[0]);
	if (ret < 0) {
		printk(KERN_ERR "pca963x: led_classdev_register failed\n");
		goto err_led0_classdev_register_failed;
	}
	/* green */
	ret = led_classdev_register(&client->dev, &pca963x->leds[1]);
	if (ret < 0) {
		printk(KERN_ERR "pca963x: led_classdev_register failed\n");
		goto err_led1_classdev_register_failed;
	}
	/* red */
	ret = led_classdev_register(&client->dev, &pca963x->leds[2]);
	if (ret < 0) {
		printk(KERN_ERR "pca963x: led_classdev_register failed\n");
		goto err_led2_classdev_register_failed;
	}

	ret = device_create_file(&client->dev, &dev_attr_blink);
	ret = device_create_file(&client->dev, &dev_attr_grppwm);
	ret = device_create_file(&client->dev, &dev_attr_grpfreq);

	return 0;

err_led2_classdev_register_failed:
	led_classdev_unregister(&pca963x->leds[2]);
err_led1_classdev_register_failed:
	led_classdev_unregister(&pca963x->leds[1]);
err_led0_classdev_register_failed:
	led_classdev_unregister(&pca963x->leds[0]);
err_alloc_failed:
	kfree(pca963x);
exit:
	return ret;
}

static int pca963x_suspend(struct i2c_client *client, pm_message_t mesg)
{
	flush_scheduled_work();
	return 0;
}

static int pca963x_remove(struct i2c_client *client)
{
	struct pca963x_data *pca963x = i2c_get_clientdata(client);

	cancel_work_sync(&pca963x->work);
	device_remove_file(&client->dev, &dev_attr_blink);
	device_remove_file(&client->dev, &dev_attr_grppwm);
	device_remove_file(&client->dev, &dev_attr_grpfreq);
	set_pca963x_default(client);
	led_classdev_unregister(&pca963x->leds[0]);
	led_classdev_unregister(&pca963x->leds[1]);
	led_classdev_unregister(&pca963x->leds[2]);

	kfree(pca963x);
	return 0;
}

static const struct i2c_device_id pca963x_id[] = {
	{ "pca963x", 0 },
	{ }
};

static struct i2c_driver pca963x_driver = {
	.driver = {
		   .name = "pca963x",
		   },
	.probe = pca963x_probe,
	.suspend = pca963x_suspend,
	.remove = pca963x_remove,
	.id_table = pca963x_id,
};

static int __init pca963x_init(void)
{
	return i2c_add_driver(&pca963x_driver);
}

static void __exit pca963x_exit(void)
{
	i2c_del_driver(&pca963x_driver);
}

MODULE_AUTHOR("Shan-Fu Chiou <sfchiou@gmail.com>");
MODULE_DESCRIPTION("pca963x driver");
MODULE_LICENSE("GPL");

module_init(pca963x_init);
module_exit(pca963x_exit);
