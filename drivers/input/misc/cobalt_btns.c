/*
 *  Cobalt button interface driver.
 *
 *  Copyright (C) 2007-2008  Yoichi Yuasa <yoichi_yuasa@tripeaks.co.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include <linux/init.h>
#include <linux/input-polldev.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>

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

static int __devinit cobalt_buttons_probe(struct platform_device *pdev)
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
	bdev->reg = ioremap(res->start, res->end - res->start + 1);
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
	dev_set_drvdata(&pdev->dev, NULL);
	return error;
}

static int __devexit cobalt_buttons_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct buttons_dev *bdev = dev_get_drvdata(dev);

	input_unregister_polled_device(bdev->poll_dev);
	input_free_polled_device(bdev->poll_dev);
	iounmap(bdev->reg);
	kfree(bdev);
	dev_set_drvdata(dev, NULL);

	return 0;
}

MODULE_AUTHOR("Yoichi Yuasa <yoichi_yuasa@tripeaks.co.jp>");
MODULE_DESCRIPTION("Cobalt button interface driver");
MODULE_LICENSE("GPL");
/* work with hotplug and coldplug */
MODULE_ALIAS("platform:Cobalt buttons");

static struct platform_driver cobalt_buttons_driver = {
	.probe	= cobalt_buttons_probe,
	.remove	= __devexit_p(cobalt_buttons_remove),
	.driver	= {
		.name	= "Cobalt buttons",
		.owner	= THIS_MODULE,
	},
};

static int __init cobalt_buttons_init(void)
{
	return platform_driver_register(&cobalt_buttons_driver);
}

static void __exit cobalt_buttons_exit(void)
{
	platform_driver_unregister(&cobalt_buttons_driver);
}

module_init(cobalt_buttons_init);
module_exit(cobalt_buttons_exit);
