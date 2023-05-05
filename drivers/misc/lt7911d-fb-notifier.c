// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2023 Rockchip Electronics Co. Ltd.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/notifier.h>
#include <linux/fb.h>

struct lt7911d {
	struct device *dev;
	struct gpio_descs *gpios;
	struct notifier_block fb_notif;
	int fb_blank;
};

static int lt7911d_fb_notifier_callback(struct notifier_block *self,
					unsigned long event, void *data)
{
	struct lt7911d *lt7911d = container_of(self, struct lt7911d, fb_notif);
	struct fb_event *evdata = data;
	int fb_blank = *(int *)evdata->data;
	int i;

	if (event != FB_EVENT_BLANK)
		return 0;

	if (lt7911d->fb_blank == fb_blank)
		return 0;

	if (fb_blank == FB_BLANK_UNBLANK) {
		for (i = 0; i < lt7911d->gpios->ndescs; i++)
			gpiod_direction_output(lt7911d->gpios->desc[i], 1);
		msleep(20);
		for (i = 0; i < lt7911d->gpios->ndescs; i++)
			gpiod_direction_output(lt7911d->gpios->desc[i], 0);
		msleep(500);
	}

	lt7911d->fb_blank = fb_blank;

	return 0;
}

static int lt7911d_fb_notifier_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lt7911d *lt7911d;
	int i, ret;

	lt7911d = devm_kzalloc(dev, sizeof(*lt7911d), GFP_KERNEL);
	if (!lt7911d)
		return -ENOMEM;

	lt7911d->dev = dev;
	platform_set_drvdata(pdev, lt7911d);

	lt7911d->gpios = devm_gpiod_get_array(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(lt7911d->gpios))
		return dev_err_probe(dev, PTR_ERR(lt7911d->gpios),
				     "failed to acquire reset gpio\n");

	for (i = 0; i < lt7911d->gpios->ndescs; i++)
		gpiod_set_consumer_name(lt7911d->gpios->desc[i], "lt7911d-reset");

	lt7911d->fb_blank = FB_BLANK_UNBLANK;
	lt7911d->fb_notif.notifier_call = lt7911d_fb_notifier_callback;
	ret = fb_register_client(&lt7911d->fb_notif);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register fb client\n");

	return 0;
}

static int lt7911d_fb_notifier_remove(struct platform_device *pdev)
{
	struct lt7911d *lt7911d = platform_get_drvdata(pdev);

	fb_unregister_client(&lt7911d->fb_notif);

	return 0;
}

static void lt7911d_fb_notifier_shutdown(struct platform_device *pdev)
{
	struct lt7911d *lt7911d = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < lt7911d->gpios->ndescs; i++)
		gpiod_direction_output(lt7911d->gpios->desc[i], 1);
	msleep(20);
}

static const struct of_device_id lt7911d_fb_notifier_of_match[] = {
	{ .compatible = "lontium,lt7911d-fb-notifier" },
	{}
};
MODULE_DEVICE_TABLE(of, lt7911d_fb_notifier_of_match);

static struct platform_driver lt7911d_fb_notifier_driver = {
	.driver = {
		.name = "lt7911d-fb-notifier",
		.of_match_table = lt7911d_fb_notifier_of_match,
	},
	.probe = lt7911d_fb_notifier_probe,
	.remove = lt7911d_fb_notifier_remove,
	.shutdown = lt7911d_fb_notifier_shutdown,
};

module_platform_driver(lt7911d_fb_notifier_driver);

MODULE_DESCRIPTION("Lontium LT7911D FB Notifier");
MODULE_LICENSE("GPL");
