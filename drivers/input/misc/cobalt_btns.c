// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Cobalt button interface driver.
 *
 *  Copyright (C) 2007-2008  Yoichi Yuasa <yuasa@linux-mips.org>
 */
#include <linux/input-polldev.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define BUTTONS_POLL_INTERVAL	30	/* msec */
#define BUTTONS_COUNT_THRESHOLD	3
#define BUTTONS_STATUS_MASK	0xfe000000

static const unsigned short cobalt_map[] = {
	KEY_RESERVED,
	KEY_RESTART,
	KEY_LEFT,
	KEY_UP,
	KEY_DOWN,
	KEY_RIGHT,
	KEY_ENTER,
	KEY_SELECT
};

struct buttons_dev {
	struct input_polled_dev *poll_dev;
	unsigned short keymap[ARRAY_SIZE(cobalt_map)];
	int count[ARRAY_SIZE(cobalt_map)];
	void __iomem *reg;
};

static void handle_buttons(struct input_polled_dev *dev)
{
	struct buttons_dev *bdev = dev->private;
	struct input_dev *input = dev->input;
	uint32_t status;
	int i;

	status = ~readl(bdev->reg) >> 24;

	for (i = 0; i < ARRAY_SIZE(bdev->keymap); i++) {
		if (status & (1U << i)) {
			if (++bdev->count[i] == BUTTONS_COUNT_THRESHOLD) {
				input_event(input, EV_MSC, MSC_SCAN, i);
				input_report_key(input, bdev->keymap[i], 1);
				input_sync(input);
			}
		} else {
			if (bdev->count[i] >= BUTTONS_COUNT_THRESHOLD) {
				input_event(input, EV_MSC, MSC_SCAN, i);
				input_report_key(input, bdev->keymap[i], 0);
				input_sync(input);
			}
			bdev->count[i] = 0;
		}
	}
}

static int cobalt_buttons_probe(struct platform_device *pdev)
{
	struct buttons_dev *bdev;
	struct input_polled_dev *poll_dev;
	struct input_dev *input;
	struct resource *res;
	int error, i;

	bdev = kzalloc(sizeof(struct buttons_dev), GFP_KERNEL);
	poll_dev = input_allocate_polled_device();
	if (!bdev || !poll_dev) {
		error = -ENOMEM;
		goto err_free_mem;
	}

	memcpy(bdev->keymap, cobalt_map, sizeof(bdev->keymap));

	poll_dev->private = bdev;
	poll_dev->poll = handle_buttons;
	poll_dev->poll_interval = BUTTONS_POLL_INTERVAL;

	input = poll_dev->input;
	input->name = "Cobalt buttons";
	input->phys = "cobalt/input0";
	input->id.bustype = BUS_HOST;
	input->dev.parent = &pdev->dev;

	input->keycode = bdev->keymap;
	input->keycodemax = ARRAY_SIZE(bdev->keymap);
	input->keycodesize = sizeof(unsigned short);

	input_set_capability(input, EV_MSC, MSC_SCAN);
	__set_bit(EV_KEY, input->evbit);
	for (i = 0; i < ARRAY_SIZE(cobalt_map); i++)
		__set_bit(bdev->keymap[i], input->keybit);
	__clear_bit(KEY_RESERVED, input->keybit);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		error = -EBUSY;
		goto err_free_mem;
	}

	bdev->poll_dev = poll_dev;
	bdev->reg = ioremap(res->start, resource_size(res));
	dev_set_drvdata(&pdev->dev, bdev);

	error = input_register_polled_device(poll_dev);
	if (error)
		goto err_iounmap;

	return 0;

 err_iounmap:
	iounmap(bdev->reg);
 err_free_mem:
	input_free_polled_device(poll_dev);
	kfree(bdev);
	return error;
}

static int cobalt_buttons_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct buttons_dev *bdev = dev_get_drvdata(dev);

	input_unregister_polled_device(bdev->poll_dev);
	input_free_polled_device(bdev->poll_dev);
	iounmap(bdev->reg);
	kfree(bdev);

	return 0;
}

MODULE_AUTHOR("Yoichi Yuasa <yuasa@linux-mips.org>");
MODULE_DESCRIPTION("Cobalt button interface driver");
MODULE_LICENSE("GPL");
/* work with hotplug and coldplug */
MODULE_ALIAS("platform:Cobalt buttons");

static struct platform_driver cobalt_buttons_driver = {
	.probe	= cobalt_buttons_probe,
	.remove	= cobalt_buttons_remove,
	.driver	= {
		.name	= "Cobalt buttons",
	},
};
module_platform_driver(cobalt_buttons_driver);
