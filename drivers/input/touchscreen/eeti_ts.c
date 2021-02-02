// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Touch Screen driver for EETI's I2C connected touch screen panels
 *   Copyright (c) 2009,2018 Daniel Mack <daniel@zonque.org>
 *
 * See EETI's software guide for the protocol specification:
 *   http://home.eeti.com.tw/documentation.html
 *
 * Based on migor_ts.c
 *   Copyright (c) 2008 Magnus Damm
 *   Copyright (c) 2007 Ujjwal Pande <ujjwal@kenati.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/timer.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

struct eeti_ts {
	struct i2c_client *client;
	struct input_dev *input;
	struct gpio_desc *attn_gpio;
	struct touchscreen_properties props;
	struct mutex mutex;
	bool running;
};

#define EETI_TS_BITDEPTH	(11)
#define EETI_MAXVAL		((1 << (EETI_TS_BITDEPTH + 1)) - 1)

#define REPORT_BIT_PRESSED	BIT(0)
#define REPORT_BIT_AD0		BIT(1)
#define REPORT_BIT_AD1		BIT(2)
#define REPORT_BIT_HAS_PRESSURE	BIT(6)
#define REPORT_RES_BITS(v)	(((v) >> 1) + EETI_TS_BITDEPTH)

static void eeti_ts_report_event(struct eeti_ts *eeti, u8 *buf)
{
	unsigned int res;
	u16 x, y;

	res = REPORT_RES_BITS(buf[0] & (REPORT_BIT_AD0 | REPORT_BIT_AD1));

	x = get_unaligned_be16(&buf[1]);
	y = get_unaligned_be16(&buf[3]);

	/* fix the range to 11 bits */
	x >>= res - EETI_TS_BITDEPTH;
	y >>= res - EETI_TS_BITDEPTH;

	if (buf[0] & REPORT_BIT_HAS_PRESSURE)
		input_report_abs(eeti->input, ABS_PRESSURE, buf[5]);

	touchscreen_report_pos(eeti->input, &eeti->props, x, y, false);
	input_report_key(eeti->input, BTN_TOUCH, buf[0] & REPORT_BIT_PRESSED);
	input_sync(eeti->input);
}

static int eeti_ts_read(struct eeti_ts *eeti)
{
	int len, error;
	char buf[6];

	len = i2c_master_recv(eeti->client, buf, sizeof(buf));
	if (len != sizeof(buf)) {
		error = len < 0 ? len : -EIO;
		dev_err(&eeti->client->dev,
			"failed to read touchscreen data: %d\n",
			error);
		return error;
	}

	/* Motion packet */
	if (buf[0] & 0x80)
		eeti_ts_report_event(eeti, buf);

	return 0;
}

static irqreturn_t eeti_ts_isr(int irq, void *dev_id)
{
	struct eeti_ts *eeti = dev_id;
	int error;

	mutex_lock(&eeti->mutex);

	do {
		/*
		 * If we have attention GPIO, trust it. Otherwise we'll read
		 * once and exit. We assume that in this case we are using
		 * level triggered interrupt and it will get raised again
		 * if/when there is more data.
		 */
		if (eeti->attn_gpio &&
		    !gpiod_get_value_cansleep(eeti->attn_gpio)) {
			break;
		}

		error = eeti_ts_read(eeti);
		if (error)
			break;

	} while (eeti->running && eeti->attn_gpio);

	mutex_unlock(&eeti->mutex);
	return IRQ_HANDLED;
}

static void eeti_ts_start(struct eeti_ts *eeti)
{
	mutex_lock(&eeti->mutex);

	eeti->running = true;
	enable_irq(eeti->client->irq);

	/*
	 * Kick the controller in case we are using edge interrupt and
	 * we missed our edge while interrupt was disabled. We expect
	 * the attention GPIO to be wired in this case.
	 */
	if (eeti->attn_gpio && gpiod_get_value_cansleep(eeti->attn_gpio))
		eeti_ts_read(eeti);

	mutex_unlock(&eeti->mutex);
}

static void eeti_ts_stop(struct eeti_ts *eeti)
{
	/*
	 * Not locking here, just setting a flag and expect that the
	 * interrupt thread will notice the flag eventually.
	 */
	eeti->running = false;
	wmb();
	disable_irq(eeti->client->irq);
}

static int eeti_ts_open(struct input_dev *dev)
{
	struct eeti_ts *eeti = input_get_drvdata(dev);

	eeti_ts_start(eeti);

	return 0;
}

static void eeti_ts_close(struct input_dev *dev)
{
	struct eeti_ts *eeti = input_get_drvdata(dev);

	eeti_ts_stop(eeti);
}

static int eeti_ts_probe(struct i2c_client *client,
			 const struct i2c_device_id *idp)
{
	struct device *dev = &client->dev;
	struct eeti_ts *eeti;
	struct input_dev *input;
	int error;

	/*
	 * In contrast to what's described in the datasheet, there seems
	 * to be no way of probing the presence of that device using I2C
	 * commands. So we need to blindly believe it is there, and wait
	 * for interrupts to occur.
	 */

	eeti = devm_kzalloc(dev, sizeof(*eeti), GFP_KERNEL);
	if (!eeti) {
		dev_err(dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}

	mutex_init(&eeti->mutex);

	input = devm_input_allocate_device(dev);
	if (!input) {
		dev_err(dev, "Failed to allocate input device.\n");
		return -ENOMEM;
	}

	input_set_capability(input, EV_KEY, BTN_TOUCH);

	input_set_abs_params(input, ABS_X, 0, EETI_MAXVAL, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, EETI_MAXVAL, 0, 0);
	input_set_abs_params(input, ABS_PRESSURE, 0, 0xff, 0, 0);

	touchscreen_parse_properties(input, false, &eeti->props);

	input->name = client->name;
	input->id.bustype = BUS_I2C;
	input->open = eeti_ts_open;
	input->close = eeti_ts_close;

	eeti->client = client;
	eeti->input = input;

	eeti->attn_gpio = devm_gpiod_get_optional(dev, "attn", GPIOD_IN);
	if (IS_ERR(eeti->attn_gpio))
		return PTR_ERR(eeti->attn_gpio);

	i2c_set_clientdata(client, eeti);
	input_set_drvdata(input, eeti);

	error = devm_request_threaded_irq(dev, client->irq,
					  NULL, eeti_ts_isr,
					  IRQF_ONESHOT,
					  client->name, eeti);
	if (error) {
		dev_err(dev, "Unable to request touchscreen IRQ: %d\n",
			error);
		return error;
	}

	/*
	 * Disable the device for now. It will be enabled once the
	 * input device is opened.
	 */
	eeti_ts_stop(eeti);

	error = input_register_device(input);
	if (error)
		return error;

	return 0;
}

static int __maybe_unused eeti_ts_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct eeti_ts *eeti = i2c_get_clientdata(client);
	struct input_dev *input_dev = eeti->input;

	mutex_lock(&input_dev->mutex);

	if (input_device_enabled(input_dev))
		eeti_ts_stop(eeti);

	mutex_unlock(&input_dev->mutex);

	if (device_may_wakeup(&client->dev))
		enable_irq_wake(client->irq);

	return 0;
}

static int __maybe_unused eeti_ts_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct eeti_ts *eeti = i2c_get_clientdata(client);
	struct input_dev *input_dev = eeti->input;

	if (device_may_wakeup(&client->dev))
		disable_irq_wake(client->irq);

	mutex_lock(&input_dev->mutex);

	if (input_device_enabled(input_dev))
		eeti_ts_start(eeti);

	mutex_unlock(&input_dev->mutex);

	return 0;
}

static SIMPLE_DEV_PM_OPS(eeti_ts_pm, eeti_ts_suspend, eeti_ts_resume);

static const struct i2c_device_id eeti_ts_id[] = {
	{ "eeti_ts", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, eeti_ts_id);

#ifdef CONFIG_OF
static const struct of_device_id of_eeti_ts_match[] = {
	{ .compatible = "eeti,exc3000-i2c", },
	{ }
};
#endif

static struct i2c_driver eeti_ts_driver = {
	.driver = {
		.name = "eeti_ts",
		.pm = &eeti_ts_pm,
		.of_match_table = of_match_ptr(of_eeti_ts_match),
	},
	.probe = eeti_ts_probe,
	.id_table = eeti_ts_id,
};

module_i2c_driver(eeti_ts_driver);

MODULE_DESCRIPTION("EETI Touchscreen driver");
MODULE_AUTHOR("Daniel Mack <daniel@zonque.org>");
MODULE_LICENSE("GPL");
