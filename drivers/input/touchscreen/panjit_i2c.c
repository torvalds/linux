/*
 * drivers/input/touchscreen/panjit_i2c.c
 *
 * Touchscreen class input driver for Panjit touch panel using I2C bus
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/i2c.h>
#include <linux/i2c/panjit_ts.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/slab.h>

#define CSR		0x00
 #define CSR_SCAN_EN	(1 << 3)
 #define CSR_SLEEP_EN	(1 << 7)
#define C_FLAG		0x01
#define X1_H		0x03

#define DRIVER_NAME	"panjit_touch"

#ifdef CONFIG_HAS_EARLYSUSPEND
static void pj_early_suspend(struct early_suspend *h);
static void pj_late_resume(struct early_suspend *h);
#endif

struct pj_data {
	struct input_dev	*input_dev;
	struct i2c_client	*client;
	int			gpio_reset;
	struct early_suspend	early_suspend;
};

struct pj_event {
	__be16	coord[2][2];
	__u8	fingers;
	__u8	gesture;
};

union pj_buff {
	struct pj_event	data;
	unsigned char	buff[sizeof(struct pj_data)];
};

static void pj_reset(struct pj_data *touch)
{
	if (touch->gpio_reset < 0)
		return;

	gpio_set_value(touch->gpio_reset, 1);
	msleep(50);
	gpio_set_value(touch->gpio_reset, 0);
	msleep(50);
}

static irqreturn_t pj_irq(int irq, void *dev_id)
{
	struct pj_data *touch = dev_id;
	struct i2c_client *client = touch->client;
	union pj_buff event;
	int ret, i;

	ret = i2c_smbus_read_i2c_block_data(client, X1_H,
					    sizeof(event.buff), event.buff);
	if (WARN_ON(ret < 0)) {
		dev_err(&client->dev, "error %d reading event data\n", ret);
		return IRQ_NONE;
	}
	ret = i2c_smbus_write_byte_data(client, C_FLAG, 0);
	if (WARN_ON(ret < 0)) {
		dev_err(&client->dev, "error %d clearing interrupt\n", ret);
		return IRQ_NONE;
	}

	input_report_key(touch->input_dev, BTN_TOUCH,
			 (event.data.fingers == 1 || event.data.fingers == 2));
	input_report_key(touch->input_dev, BTN_2, (event.data.fingers == 2));

	if (!event.data.fingers || (event.data.fingers > 2))
		goto out;

	for (i = 0; i < event.data.fingers; i++) {
		input_report_abs(touch->input_dev, ABS_MT_POSITION_X,
				 __be16_to_cpu(event.data.coord[i][0]));
		input_report_abs(touch->input_dev, ABS_MT_POSITION_Y,
				 __be16_to_cpu(event.data.coord[i][1]));
		input_report_abs(touch->input_dev, ABS_MT_TRACKING_ID, i + 1);
		input_mt_sync(touch->input_dev);
	}

out:
	input_sync(touch->input_dev);
	return IRQ_HANDLED;
}

static int pj_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct panjit_i2c_ts_platform_data *pdata = client->dev.platform_data;
	struct pj_data *touch = NULL;
	struct input_dev *input_dev = NULL;
	int ret = 0;

	touch = kzalloc(sizeof(struct pj_data), GFP_KERNEL);
	if (!touch) {
		dev_err(&client->dev, "%s: no memory\n", __func__);
		return -ENOMEM;
	}

	touch->gpio_reset = -EINVAL;

	if (pdata) {
		ret = gpio_request(pdata->gpio_reset, "panjit_reset");
		if (!ret) {
			ret = gpio_direction_output(pdata->gpio_reset, 1);
			if (ret < 0)
				gpio_free(pdata->gpio_reset);
		}

		if (!ret)
			touch->gpio_reset = pdata->gpio_reset;
		else
			dev_warn(&client->dev, "unable to configure GPIO\n");
	}

	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&client->dev, "%s: no memory\n", __func__);
		kfree(touch);
		return -ENOMEM;
	}

	touch->client = client;
	i2c_set_clientdata(client, touch);

	pj_reset(touch);

	/* clear interrupt */
	ret = i2c_smbus_write_byte_data(touch->client, C_FLAG, 0);
	if (ret < 0) {
		dev_err(&client->dev, "%s: clear interrupt failed\n",
			__func__);
		goto fail_i2c_or_register;
	}

	/* enable scanning */
	ret = i2c_smbus_write_byte_data(touch->client, CSR, CSR_SCAN_EN);
	if (ret < 0) {
		dev_err(&client->dev, "%s: enable interrupt failed\n",
			__func__);
		goto fail_i2c_or_register;
	}

	touch->input_dev = input_dev;
	touch->input_dev->name = DRIVER_NAME;

	set_bit(EV_SYN, touch->input_dev->evbit);
	set_bit(EV_KEY, touch->input_dev->evbit);
	set_bit(EV_ABS, touch->input_dev->evbit);
	set_bit(BTN_TOUCH, touch->input_dev->keybit);
	set_bit(BTN_2, touch->input_dev->keybit);

	/* expose multi-touch capabilities */
	set_bit(ABS_MT_POSITION_X, touch->input_dev->keybit);
	set_bit(ABS_MT_POSITION_Y, touch->input_dev->keybit);
	set_bit(ABS_X, touch->input_dev->keybit);
	set_bit(ABS_Y, touch->input_dev->keybit);

	/* all coordinates are reported in 0..4095 */
	input_set_abs_params(touch->input_dev, ABS_X, 0, 4095, 0, 0);
	input_set_abs_params(touch->input_dev, ABS_Y, 0, 4095, 0, 0);
	input_set_abs_params(touch->input_dev, ABS_HAT0X, 0, 4095, 0, 0);
	input_set_abs_params(touch->input_dev, ABS_HAT0Y, 0, 4095, 0, 0);
	input_set_abs_params(touch->input_dev, ABS_HAT1X, 0, 4095, 0, 0);
	input_set_abs_params(touch->input_dev, ABS_HAT1Y, 0, 4095, 0, 0);

	input_set_abs_params(touch->input_dev, ABS_MT_POSITION_X, 0, 4095, 0, 0);
	input_set_abs_params(touch->input_dev, ABS_MT_POSITION_Y, 0, 4095, 0, 0);
	input_set_abs_params(touch->input_dev, ABS_MT_TRACKING_ID, 0, 2, 1, 0);

	ret = input_register_device(touch->input_dev);
	if (ret) {
		dev_err(&client->dev, "%s: input_register_device failed\n",
			__func__);
		goto fail_i2c_or_register;
	}

	/* get the irq */
	ret = request_threaded_irq(touch->client->irq, NULL, pj_irq,
				   IRQF_ONESHOT | IRQF_TRIGGER_LOW,
				   DRIVER_NAME, touch);
	if (ret) {
		dev_err(&client->dev, "%s: request_irq(%d) failed\n",
			__func__, touch->client->irq);
		goto fail_irq;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	touch->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	touch->early_suspend.suspend = pj_early_suspend;
	touch->early_suspend.resume = pj_late_resume;
	register_early_suspend(&touch->early_suspend);
#endif
	dev_info(&client->dev, "%s: initialized\n", __func__);
	return 0;

fail_irq:
	input_unregister_device(touch->input_dev);

fail_i2c_or_register:
	if (touch->gpio_reset >= 0)
		gpio_free(touch->gpio_reset);

	input_free_device(input_dev);
	kfree(touch);
	return ret;
}

static int pj_suspend(struct i2c_client *client, pm_message_t state)
{
	struct pj_data *touch = i2c_get_clientdata(client);
	int ret;

	if (WARN_ON(!touch))
		return -EINVAL;

	disable_irq(client->irq);

	/* disable scanning and enable deep sleep */
	ret = i2c_smbus_write_byte_data(client, CSR, CSR_SLEEP_EN);
	if (ret < 0) {
		dev_err(&client->dev, "%s: sleep enable fail\n", __func__);
		return ret;
	}

	return 0;
}

static int pj_resume(struct i2c_client *client)
{
	struct pj_data *touch = i2c_get_clientdata(client);
	int ret = 0;

	if (WARN_ON(!touch))
		return -EINVAL;

	pj_reset(touch);

	/* enable scanning and disable deep sleep */
	ret = i2c_smbus_write_byte_data(client, C_FLAG, 0);
	if (ret >= 0)
		ret = i2c_smbus_write_byte_data(client, CSR, CSR_SCAN_EN);
	if (ret < 0) {
		dev_err(&client->dev, "%s: scan enable fail\n", __func__);
		return ret;
	}

	enable_irq(client->irq);

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void pj_early_suspend(struct early_suspend *es)
{
	struct pj_data *touch;
	touch = container_of(es, struct pj_data, early_suspend);

	if (pj_suspend(touch->client, PMSG_SUSPEND) != 0)
		dev_err(&touch->client->dev, "%s: failed\n", __func__);
}

static void pj_late_resume(struct early_suspend *es)
{
	struct pj_data *touch;
	touch = container_of(es, struct pj_data, early_suspend);

	if (pj_resume(touch->client) != 0)
		dev_err(&touch->client->dev, "%s: failed\n", __func__);
}
#endif

static int pj_remove(struct i2c_client *client)
{
	struct pj_data *touch = i2c_get_clientdata(client);

	if (!touch)
		return -EINVAL;

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&touch->early_suspend);
#endif
	free_irq(touch->client->irq, touch);
	if (touch->gpio_reset >= 0)
		gpio_free(touch->gpio_reset);
	input_unregister_device(touch->input_dev);
	input_free_device(touch->input_dev);
	kfree(touch);
	return 0;
}

static const struct i2c_device_id panjit_ts_id[] = {
	{ DRIVER_NAME, 0 },
	{ }
};

static struct i2c_driver panjit_driver = {
	.probe		= pj_probe,
	.remove		= pj_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend	= pj_suspend,
	.resume		= pj_resume,
#endif
	.id_table	= panjit_ts_id,
	.driver		= {
		.name	= DRIVER_NAME,
	},
};

static int __devinit panjit_init(void)
{
	int e;

	e = i2c_add_driver(&panjit_driver);
	if (e != 0) {
		pr_err("%s: failed to register with I2C bus with "
		       "error: 0x%x\n", __func__, e);
	}
	return e;
}

static void __exit panjit_exit(void)
{
	i2c_del_driver(&panjit_driver);
}

module_init(panjit_init);
module_exit(panjit_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Panjit I2C touch driver");
