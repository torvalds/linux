/*
 * Touch Screen driver for EETI's I2C connected touch screen panels
 *   Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 *
 * See EETI's software guide for the protocol specification:
 *   http://home.eeti.com.tw/web20/eg/guide.htm
 *
 * Based on migor_ts.c
 *   Copyright (c) 2008 Magnus Damm
 *   Copyright (c) 2007 Ujjwal Pande <ujjwal@kenati.com>
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU  General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/timer.h>
#include <linux/gpio.h>
#include <linux/input/eeti_ts.h>
#include <linux/slab.h>

static int flip_x;
module_param(flip_x, bool, 0644);
MODULE_PARM_DESC(flip_x, "flip x coordinate");

static int flip_y;
module_param(flip_y, bool, 0644);
MODULE_PARM_DESC(flip_y, "flip y coordinate");

struct eeti_ts_priv {
	struct i2c_client *client;
	struct input_dev *input;
	struct work_struct work;
	struct mutex mutex;
	int irq, irq_active_high;
};

#define EETI_TS_BITDEPTH	(11)
#define EETI_MAXVAL		((1 << (EETI_TS_BITDEPTH + 1)) - 1)

#define REPORT_BIT_PRESSED	(1 << 0)
#define REPORT_BIT_AD0		(1 << 1)
#define REPORT_BIT_AD1		(1 << 2)
#define REPORT_BIT_HAS_PRESSURE	(1 << 6)
#define REPORT_RES_BITS(v)	(((v) >> 1) + EETI_TS_BITDEPTH)

static inline int eeti_ts_irq_active(struct eeti_ts_priv *priv)
{
	return gpio_get_value(irq_to_gpio(priv->irq)) == priv->irq_active_high;
}

static void eeti_ts_read(struct work_struct *work)
{
	char buf[6];
	unsigned int x, y, res, pressed, to = 100;
	struct eeti_ts_priv *priv =
		container_of(work, struct eeti_ts_priv, work);

	mutex_lock(&priv->mutex);

	while (eeti_ts_irq_active(priv) && --to)
		i2c_master_recv(priv->client, buf, sizeof(buf));

	if (!to) {
		dev_err(&priv->client->dev,
			"unable to clear IRQ - line stuck?\n");
		goto out;
	}

	/* drop non-report packets */
	if (!(buf[0] & 0x80))
		goto out;

	pressed = buf[0] & REPORT_BIT_PRESSED;
	res = REPORT_RES_BITS(buf[0] & (REPORT_BIT_AD0 | REPORT_BIT_AD1));
	x = buf[2] | (buf[1] << 8);
	y = buf[4] | (buf[3] << 8);

	/* fix the range to 11 bits */
	x >>= res - EETI_TS_BITDEPTH;
	y >>= res - EETI_TS_BITDEPTH;

	if (flip_x)
		x = EETI_MAXVAL - x;

	if (flip_y)
		y = EETI_MAXVAL - y;

	if (buf[0] & REPORT_BIT_HAS_PRESSURE)
		input_report_abs(priv->input, ABS_PRESSURE, buf[5]);

	input_report_abs(priv->input, ABS_X, x);
	input_report_abs(priv->input, ABS_Y, y);
	input_report_key(priv->input, BTN_TOUCH, !!pressed);
	input_sync(priv->input);

out:
	mutex_unlock(&priv->mutex);
}

static irqreturn_t eeti_ts_isr(int irq, void *dev_id)
{
	struct eeti_ts_priv *priv = dev_id;

	 /* postpone I2C transactions as we are atomic */
	schedule_work(&priv->work);

	return IRQ_HANDLED;
}

static void eeti_ts_start(struct eeti_ts_priv *priv)
{
	enable_irq(priv->irq);

	/* Read the events once to arm the IRQ */
	eeti_ts_read(&priv->work);
}

static void eeti_ts_stop(struct eeti_ts_priv *priv)
{
	disable_irq(priv->irq);
	cancel_work_sync(&priv->work);
}

static int eeti_ts_open(struct input_dev *dev)
{
	struct eeti_ts_priv *priv = input_get_drvdata(dev);

	eeti_ts_start(priv);

	return 0;
}

static void eeti_ts_close(struct input_dev *dev)
{
	struct eeti_ts_priv *priv = input_get_drvdata(dev);

	eeti_ts_stop(priv);
}

static int __devinit eeti_ts_probe(struct i2c_client *client,
				   const struct i2c_device_id *idp)
{
	struct eeti_ts_platform_data *pdata;
	struct eeti_ts_priv *priv;
	struct input_dev *input;
	unsigned int irq_flags;
	int err = -ENOMEM;

	/*
	 * In contrast to what's described in the datasheet, there seems
	 * to be no way of probing the presence of that device using I2C
	 * commands. So we need to blindly believe it is there, and wait
	 * for interrupts to occur.
	 */

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&client->dev, "failed to allocate driver data\n");
		goto err0;
	}

	mutex_init(&priv->mutex);
	input = input_allocate_device();

	if (!input) {
		dev_err(&client->dev, "Failed to allocate input device.\n");
		goto err1;
	}

	input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	input_set_abs_params(input, ABS_X, 0, EETI_MAXVAL, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, EETI_MAXVAL, 0, 0);
	input_set_abs_params(input, ABS_PRESSURE, 0, 0xff, 0, 0);

	input->name = client->name;
	input->id.bustype = BUS_I2C;
	input->dev.parent = &client->dev;
	input->open = eeti_ts_open;
	input->close = eeti_ts_close;

	priv->client = client;
	priv->input = input;
	priv->irq = client->irq;

	pdata = client->dev.platform_data;

	if (pdata)
		priv->irq_active_high = pdata->irq_active_high;

	irq_flags = priv->irq_active_high ?
		IRQF_TRIGGER_RISING : IRQF_TRIGGER_FALLING;

	INIT_WORK(&priv->work, eeti_ts_read);
	i2c_set_clientdata(client, priv);
	input_set_drvdata(input, priv);

	err = input_register_device(input);
	if (err)
		goto err1;

	err = request_irq(priv->irq, eeti_ts_isr, irq_flags,
			  client->name, priv);
	if (err) {
		dev_err(&client->dev, "Unable to request touchscreen IRQ.\n");
		goto err2;
	}

	/*
	 * Disable the device for now. It will be enabled once the
	 * input device is opened.
	 */
	eeti_ts_stop(priv);

	device_init_wakeup(&client->dev, 0);
	return 0;

err2:
	input_unregister_device(input);
	input = NULL; /* so we dont try to free it below */
err1:
	input_free_device(input);
	i2c_set_clientdata(client, NULL);
	kfree(priv);
err0:
	return err;
}

static int __devexit eeti_ts_remove(struct i2c_client *client)
{
	struct eeti_ts_priv *priv = i2c_get_clientdata(client);

	free_irq(priv->irq, priv);
	/*
	 * eeti_ts_stop() leaves IRQ disabled. We need to re-enable it
	 * so that device still works if we reload the driver.
	 */
	enable_irq(priv->irq);

	input_unregister_device(priv->input);
	i2c_set_clientdata(client, NULL);
	kfree(priv);

	return 0;
}

#ifdef CONFIG_PM
static int eeti_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct eeti_ts_priv *priv = i2c_get_clientdata(client);
	struct input_dev *input_dev = priv->input;

	mutex_lock(&input_dev->mutex);

	if (input_dev->users)
		eeti_ts_stop(priv);

	mutex_unlock(&input_dev->mutex);

	if (device_may_wakeup(&client->dev))
		enable_irq_wake(priv->irq);

	return 0;
}

static int eeti_ts_resume(struct i2c_client *client)
{
	struct eeti_ts_priv *priv = i2c_get_clientdata(client);
	struct input_dev *input_dev = priv->input;

	if (device_may_wakeup(&client->dev))
		disable_irq_wake(priv->irq);

	mutex_lock(&input_dev->mutex);

	if (input_dev->users)
		eeti_ts_start(priv);

	mutex_unlock(&input_dev->mutex);

	return 0;
}
#else
#define eeti_ts_suspend NULL
#define eeti_ts_resume NULL
#endif

static const struct i2c_device_id eeti_ts_id[] = {
	{ "eeti_ts", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, eeti_ts_id);

static struct i2c_driver eeti_ts_driver = {
	.driver = {
		.name = "eeti_ts",
	},
	.probe = eeti_ts_probe,
	.remove = __devexit_p(eeti_ts_remove),
	.suspend = eeti_ts_suspend,
	.resume = eeti_ts_resume,
	.id_table = eeti_ts_id,
};

static int __init eeti_ts_init(void)
{
	return i2c_add_driver(&eeti_ts_driver);
}

static void __exit eeti_ts_exit(void)
{
	i2c_del_driver(&eeti_ts_driver);
}

MODULE_DESCRIPTION("EETI Touchscreen driver");
MODULE_AUTHOR("Daniel Mack <daniel@caiaq.de>");
MODULE_LICENSE("GPL");

module_init(eeti_ts_init);
module_exit(eeti_ts_exit);

