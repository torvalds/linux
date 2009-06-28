/*
 * pca9532.c - 16-bit Led dimmer
 *
 * Copyright (C) 2008 Riku Voipio
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * Datasheet: http://www.nxp.com/acrobat/datasheets/PCA9532_3.pdf
 *
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/input.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/leds-pca9532.h>

static const unsigned short normal_i2c[] = { /*0x60,*/ I2C_CLIENT_END};
I2C_CLIENT_INSMOD_1(pca9532);

#define PCA9532_REG_PSC(i) (0x2+(i)*2)
#define PCA9532_REG_PWM(i) (0x3+(i)*2)
#define PCA9532_REG_LS0  0x6
#define LED_REG(led) ((led>>2)+PCA9532_REG_LS0)
#define LED_NUM(led) (led & 0x3)

#define ldev_to_led(c)       container_of(c, struct pca9532_led, ldev)

struct pca9532_data {
	struct i2c_client *client;
	struct pca9532_led leds[16];
	struct mutex update_lock;
	struct input_dev    *idev;
	struct work_struct work;
	u8 pwm[2];
	u8 psc[2];
};

static int pca9532_probe(struct i2c_client *client,
	const struct i2c_device_id *id);
static int pca9532_remove(struct i2c_client *client);

static const struct i2c_device_id pca9532_id[] = {
	{ "pca9532", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, pca9532_id);

static struct i2c_driver pca9532_driver = {
	.driver = {
		.name   = "pca9532",
	},
	.probe  = pca9532_probe,
	.remove = pca9532_remove,
	.id_table = pca9532_id,
};

/* We have two pwm/blinkers, but 16 possible leds to drive. Additionaly,
 * the clever Thecus people are using one pwm to drive the beeper. So,
 * as a compromise we average one pwm to the values requested by all
 * leds that are not ON/OFF.
 * */
static int pca9532_calcpwm(struct i2c_client *client, int pwm, int blink,
	enum led_brightness value)
{
	int a = 0, b = 0, i = 0;
	struct pca9532_data *data = i2c_get_clientdata(client);
	for (i = 0; i < 16; i++) {
		if (data->leds[i].type == PCA9532_TYPE_LED &&
			data->leds[i].state == PCA9532_PWM0+pwm) {
				a++;
				b += data->leds[i].ldev.brightness;
		}
	}
	if (a == 0) {
		dev_err(&client->dev,
		"fear of division by zero %d/%d, wanted %d\n",
			b, a, value);
		return -EINVAL;
	}
	b = b/a;
	if (b > 0xFF)
		return -EINVAL;
	data->pwm[pwm] = b;
	data->psc[pwm] = blink;
	return 0;
}

static int pca9532_setpwm(struct i2c_client *client, int pwm)
{
	struct pca9532_data *data = i2c_get_clientdata(client);
	mutex_lock(&data->update_lock);
	i2c_smbus_write_byte_data(client, PCA9532_REG_PWM(pwm),
		data->pwm[pwm]);
	i2c_smbus_write_byte_data(client, PCA9532_REG_PSC(pwm),
		data->psc[pwm]);
	mutex_unlock(&data->update_lock);
	return 0;
}

/* Set LED routing */
static void pca9532_setled(struct pca9532_led *led)
{
	struct i2c_client *client = led->client;
	struct pca9532_data *data = i2c_get_clientdata(client);
	char reg;

	mutex_lock(&data->update_lock);
	reg = i2c_smbus_read_byte_data(client, LED_REG(led->id));
	/* zero led bits */
	reg = reg & ~(0x3<<LED_NUM(led->id)*2);
	/* set the new value */
	reg = reg | (led->state << LED_NUM(led->id)*2);
	i2c_smbus_write_byte_data(client, LED_REG(led->id), reg);
	mutex_unlock(&data->update_lock);
}

static void pca9532_set_brightness(struct led_classdev *led_cdev,
	enum led_brightness value)
{
	int err = 0;
	struct pca9532_led *led = ldev_to_led(led_cdev);

	if (value == LED_OFF)
		led->state = PCA9532_OFF;
	else if (value == LED_FULL)
		led->state = PCA9532_ON;
	else {
		led->state = PCA9532_PWM0; /* Thecus: hardcode one pwm */
		err = pca9532_calcpwm(led->client, 0, 0, value);
		if (err)
			return; /* XXX: led api doesn't allow error code? */
	}
	schedule_work(&led->work);
}

static int pca9532_set_blink(struct led_classdev *led_cdev,
	unsigned long *delay_on, unsigned long *delay_off)
{
	struct pca9532_led *led = ldev_to_led(led_cdev);
	struct i2c_client *client = led->client;
	int psc;
	int err = 0;

	if (*delay_on == 0 && *delay_off == 0) {
	/* led subsystem ask us for a blink rate */
		*delay_on  = 1000;
		*delay_off = 1000;
	}
	if (*delay_on != *delay_off || *delay_on > 1690 || *delay_on < 6)
		return -EINVAL;

	/* Thecus specific: only use PSC/PWM 0 */
	psc = (*delay_on * 152-1)/1000;
	err = pca9532_calcpwm(client, 0, psc, led_cdev->brightness);
	if (err)
		return err;
	schedule_work(&led->work);
	return 0;
}

static int pca9532_event(struct input_dev *dev, unsigned int type,
	unsigned int code, int value)
{
	struct pca9532_data *data = input_get_drvdata(dev);

	if (!(type == EV_SND && (code == SND_BELL || code == SND_TONE)))
		return -1;

	/* XXX: allow different kind of beeps with psc/pwm modifications */
	if (value > 1 && value < 32767)
		data->pwm[1] = 127;
	else
		data->pwm[1] = 0;

	schedule_work(&data->work);

	return 0;
}

static void pca9532_input_work(struct work_struct *work)
{
	struct pca9532_data *data;
	data = container_of(work, struct pca9532_data, work);
	mutex_lock(&data->update_lock);
	i2c_smbus_write_byte_data(data->client, PCA9532_REG_PWM(1),
		data->pwm[1]);
	mutex_unlock(&data->update_lock);
}

static void pca9532_led_work(struct work_struct *work)
{
	struct pca9532_led *led;
	led = container_of(work, struct pca9532_led, work);
	if (led->state == PCA9532_PWM0)
		pca9532_setpwm(led->client, 0);
	pca9532_setled(led);
}

static int pca9532_configure(struct i2c_client *client,
	struct pca9532_data *data, struct pca9532_platform_data *pdata)
{
	int i, err = 0;

	for (i = 0; i < 2; i++)	{
		data->pwm[i] = pdata->pwm[i];
		data->psc[i] = pdata->psc[i];
		i2c_smbus_write_byte_data(client, PCA9532_REG_PWM(i),
			data->pwm[i]);
		i2c_smbus_write_byte_data(client, PCA9532_REG_PSC(i),
			data->psc[i]);
	}

	for (i = 0; i < 16; i++) {
		struct pca9532_led *led = &data->leds[i];
		struct pca9532_led *pled = &pdata->leds[i];
		led->client = client;
		led->id = i;
		led->type = pled->type;
		switch (led->type) {
		case PCA9532_TYPE_NONE:
			break;
		case PCA9532_TYPE_LED:
			led->state = pled->state;
			led->name =  pled->name;
			led->ldev.name = led->name;
			led->ldev.brightness = LED_OFF;
			led->ldev.brightness_set = pca9532_set_brightness;
			led->ldev.blink_set = pca9532_set_blink;
			INIT_WORK(&led->work, pca9532_led_work);
			err = led_classdev_register(&client->dev, &led->ldev);
			if (err < 0) {
				dev_err(&client->dev,
					"couldn't register LED %s\n",
					led->name);
				goto exit;
			}
			pca9532_setled(led);
			break;
		case PCA9532_TYPE_N2100_BEEP:
			BUG_ON(data->idev);
			led->state = PCA9532_PWM1;
			pca9532_setled(led);
			data->idev = input_allocate_device();
			if (data->idev == NULL) {
				err = -ENOMEM;
				goto exit;
			}
			data->idev->name = pled->name;
			data->idev->phys = "i2c/pca9532";
			data->idev->id.bustype = BUS_HOST;
			data->idev->id.vendor  = 0x001f;
			data->idev->id.product = 0x0001;
			data->idev->id.version = 0x0100;
			data->idev->evbit[0] = BIT_MASK(EV_SND);
			data->idev->sndbit[0] = BIT_MASK(SND_BELL) |
						BIT_MASK(SND_TONE);
			data->idev->event = pca9532_event;
			input_set_drvdata(data->idev, data);
			INIT_WORK(&data->work, pca9532_input_work);
			err = input_register_device(data->idev);
			if (err) {
				input_free_device(data->idev);
				cancel_work_sync(&data->work);
				data->idev = NULL;
				goto exit;
			}
			break;
		}
	}
	return 0;

exit:
	if (i > 0)
		for (i = i - 1; i >= 0; i--)
			switch (data->leds[i].type) {
			case PCA9532_TYPE_NONE:
				break;
			case PCA9532_TYPE_LED:
				led_classdev_unregister(&data->leds[i].ldev);
				cancel_work_sync(&data->leds[i].work);
				break;
			case PCA9532_TYPE_N2100_BEEP:
				if (data->idev != NULL) {
					input_unregister_device(data->idev);
					input_free_device(data->idev);
					cancel_work_sync(&data->work);
					data->idev = NULL;
				}
				break;
			}

	return err;
}

static int pca9532_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct pca9532_data *data = i2c_get_clientdata(client);
	struct pca9532_platform_data *pca9532_pdata = client->dev.platform_data;
	int err;

	if (!pca9532_pdata)
		return -EIO;

	if (!i2c_check_functionality(client->adapter,
		I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	dev_info(&client->dev, "setting platform data\n");
	i2c_set_clientdata(client, data);
	data->client = client;
	mutex_init(&data->update_lock);

	err = pca9532_configure(client, data, pca9532_pdata);
	if (err) {
		kfree(data);
		i2c_set_clientdata(client, NULL);
	}

	return err;
}

static int pca9532_remove(struct i2c_client *client)
{
	struct pca9532_data *data = i2c_get_clientdata(client);
	int i;
	for (i = 0; i < 16; i++)
		switch (data->leds[i].type) {
		case PCA9532_TYPE_NONE:
			break;
		case PCA9532_TYPE_LED:
			led_classdev_unregister(&data->leds[i].ldev);
			cancel_work_sync(&data->leds[i].work);
			break;
		case PCA9532_TYPE_N2100_BEEP:
			if (data->idev != NULL) {
				input_unregister_device(data->idev);
				input_free_device(data->idev);
				cancel_work_sync(&data->work);
				data->idev = NULL;
			}
			break;
		}

	kfree(data);
	i2c_set_clientdata(client, NULL);
	return 0;
}

static int __init pca9532_init(void)
{
	return i2c_add_driver(&pca9532_driver);
}

static void __exit pca9532_exit(void)
{
	i2c_del_driver(&pca9532_driver);
}

MODULE_AUTHOR("Riku Voipio");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PCA 9532 LED dimmer");

module_init(pca9532_init);
module_exit(pca9532_exit);

