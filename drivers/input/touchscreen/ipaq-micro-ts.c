// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * h3600 atmel micro companion support, touchscreen subdevice
 * Author : Alessandro Gardich <gremlin@gremlin.it>
 * Author : Dmitry Artamonow <mad_soft@inbox.ru>
 * Author : Linus Walleij <linus.walleij@linaro.org>
 */

#include <asm/byteorder.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mfd/ipaq-micro.h>

struct touchscreen_data {
	struct input_dev *input;
	struct ipaq_micro *micro;
};

static void micro_ts_receive(void *data, int len, unsigned char *msg)
{
	struct touchscreen_data *ts = data;

	if (len == 4) {
		input_report_abs(ts->input, ABS_X,
				 be16_to_cpup((__be16 *) &msg[2]));
		input_report_abs(ts->input, ABS_Y,
				 be16_to_cpup((__be16 *) &msg[0]));
		input_report_key(ts->input, BTN_TOUCH, 1);
		input_sync(ts->input);
	} else if (len == 0) {
		input_report_abs(ts->input, ABS_X, 0);
		input_report_abs(ts->input, ABS_Y, 0);
		input_report_key(ts->input, BTN_TOUCH, 0);
		input_sync(ts->input);
	}
}

static void micro_ts_toggle_receive(struct touchscreen_data *ts, bool enable)
{
	struct ipaq_micro *micro = ts->micro;

	spin_lock_irq(&micro->lock);

	if (enable) {
		micro->ts = micro_ts_receive;
		micro->ts_data = ts;
	} else {
		micro->ts = NULL;
		micro->ts_data = NULL;
	}

	spin_unlock_irq(&ts->micro->lock);
}

static int micro_ts_open(struct input_dev *input)
{
	struct touchscreen_data *ts = input_get_drvdata(input);

	micro_ts_toggle_receive(ts, true);

	return 0;
}

static void micro_ts_close(struct input_dev *input)
{
	struct touchscreen_data *ts = input_get_drvdata(input);

	micro_ts_toggle_receive(ts, false);
}

static int micro_ts_probe(struct platform_device *pdev)
{
	struct ipaq_micro *micro = dev_get_drvdata(pdev->dev.parent);
	struct touchscreen_data *ts;
	int error;

	ts = devm_kzalloc(&pdev->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->micro = micro;

	ts->input = devm_input_allocate_device(&pdev->dev);
	if (!ts->input) {
		dev_err(&pdev->dev, "failed to allocate input device\n");
		return -ENOMEM;
	}

	ts->input->name = "ipaq micro ts";
	ts->input->open = micro_ts_open;
	ts->input->close = micro_ts_close;

	input_set_drvdata(ts->input, ts);

	input_set_capability(ts->input, EV_KEY, BTN_TOUCH);
	input_set_capability(ts->input, EV_ABS, ABS_X);
	input_set_capability(ts->input, EV_ABS, ABS_Y);
	input_set_abs_params(ts->input, ABS_X, 0, 1023, 0, 0);
	input_set_abs_params(ts->input, ABS_Y, 0, 1023, 0, 0);

	error = input_register_device(ts->input);
	if (error) {
		dev_err(&pdev->dev, "error registering touch input\n");
		return error;
	}

	platform_set_drvdata(pdev, ts);

	dev_info(&pdev->dev, "iPAQ micro touchscreen\n");

	return 0;
}

static int __maybe_unused micro_ts_suspend(struct device *dev)
{
	struct touchscreen_data *ts = dev_get_drvdata(dev);

	micro_ts_toggle_receive(ts, false);

	return 0;
}

static int __maybe_unused micro_ts_resume(struct device *dev)
{
	struct touchscreen_data *ts = dev_get_drvdata(dev);
	struct input_dev *input = ts->input;

	mutex_lock(&input->mutex);

	if (input->users)
		micro_ts_toggle_receive(ts, true);

	mutex_unlock(&input->mutex);

	return 0;
}

static const struct dev_pm_ops micro_ts_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(micro_ts_suspend, micro_ts_resume)
};

static struct platform_driver micro_ts_device_driver = {
	.driver	= {
		.name	= "ipaq-micro-ts",
		.pm	= &micro_ts_dev_pm_ops,
	},
	.probe	= micro_ts_probe,
};
module_platform_driver(micro_ts_device_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("driver for iPAQ Atmel micro touchscreen");
MODULE_ALIAS("platform:ipaq-micro-ts");
