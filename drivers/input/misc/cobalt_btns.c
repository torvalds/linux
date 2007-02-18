/*
 *  Cobalt button interface driver.
 *
 *  Copyright (C) 2007  Yoichi Yuasa <yoichi_yuasa@tripeaks.co.jp>
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/init.h>
#include <linux/input.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/jiffies.h>
#include <linux/timer.h>

#define BUTTONS_POLL_INTERVAL	30	/* msec */
#define BUTTONS_COUNT_THRESHOLD	3
#define BUTTONS_STATUS_MASK	0xfe000000

struct buttons_dev {
	struct input_dev *input;
	void __iomem *reg;
};

struct buttons_map {
	uint32_t mask;
	int keycode;
	int count;
};

static struct buttons_map buttons_map[] = {
	{ 0x02000000, KEY_RESTART, },
	{ 0x04000000, KEY_LEFT, },
	{ 0x08000000, KEY_UP, },
	{ 0x10000000, KEY_DOWN, },
	{ 0x20000000, KEY_RIGHT, },
	{ 0x40000000, KEY_ENTER, },
	{ 0x80000000, KEY_SELECT, },
};

static struct resource cobalt_buttons_resource __initdata = {
	.start	= 0x1d000000,
	.end	= 0x1d000003,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device *cobalt_buttons_device;

static struct timer_list buttons_timer;

static void handle_buttons(unsigned long data)
{
	struct buttons_map *button = buttons_map;
	struct buttons_dev *bdev;
	uint32_t status;
	int i;

	bdev = (struct buttons_dev *)data;
	status = readl(bdev->reg);
	status = ~status & BUTTONS_STATUS_MASK;

	for (i = 0; i < ARRAY_SIZE(buttons_map); i++) {
		if (status & button->mask) {
			button->count++;
		} else {
			if (button->count >= BUTTONS_COUNT_THRESHOLD) {
				input_report_key(bdev->input, button->keycode, 0);
				input_sync(bdev->input);
			}
			button->count = 0;
		}

		if (button->count == BUTTONS_COUNT_THRESHOLD) {
			input_report_key(bdev->input, button->keycode, 1);
			input_sync(bdev->input);
		}

		button++;
	}

	mod_timer(&buttons_timer, jiffies + msecs_to_jiffies(BUTTONS_POLL_INTERVAL));
}

static int cobalt_buttons_open(struct input_dev *dev)
{
	mod_timer(&buttons_timer, jiffies + msecs_to_jiffies(BUTTONS_POLL_INTERVAL));

	return 0;
}

static void cobalt_buttons_close(struct input_dev *dev)
{
	del_timer_sync(&buttons_timer);
}

static int __devinit cobalt_buttons_probe(struct platform_device *pdev)
{
	struct buttons_dev *bdev;
	struct input_dev *input;
	struct resource *res;
	int error, i;

	bdev = kzalloc(sizeof(struct buttons_dev), GFP_KERNEL);
	input = input_allocate_device();
	if (!bdev || !input) {
		error = -ENOMEM;
		goto err_free_mem;
	}

	input->name = "Cobalt buttons";
	input->phys = "cobalt/input0";
	input->id.bustype = BUS_HOST;
	input->cdev.dev = &pdev->dev;
	input->open = cobalt_buttons_open;
	input->close = cobalt_buttons_close;

	input->evbit[0] = BIT(EV_KEY);
	for (i = 0; i < ARRAY_SIZE(buttons_map); i++) {
		set_bit(buttons_map[i].keycode, input->keybit);
		buttons_map[i].count = 0;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		error = -EBUSY;
		goto err_free_mem;
	}

	bdev->input = input;
	bdev->reg = ioremap(res->start, res->end - res->start + 1);
	dev_set_drvdata(&pdev->dev, bdev);

	setup_timer(&buttons_timer, handle_buttons, (unsigned long)bdev);

	error = input_register_device(input);
	if (error)
		goto err_iounmap;

	return 0;

 err_iounmap:
	iounmap(bdev->reg);
 err_free_mem:
	input_free_device(input);
	kfree(bdev);
	dev_set_drvdata(&pdev->dev, NULL);
	return error;
}

static int __devexit cobalt_buttons_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct buttons_dev *bdev = dev_get_drvdata(dev);

	input_unregister_device(bdev->input);
	iounmap(bdev->reg);
	kfree(bdev);
	dev_set_drvdata(dev, NULL);

	return 0;
}

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
	int retval;

	cobalt_buttons_device = platform_device_register_simple("Cobalt buttons", -1,
	                                                        &cobalt_buttons_resource, 1);
	if (IS_ERR(cobalt_buttons_device)) {
		retval = PTR_ERR(cobalt_buttons_device);
		return retval;
	}

	retval = platform_driver_register(&cobalt_buttons_driver);
	if (retval < 0)
		platform_device_unregister(cobalt_buttons_device);

	return retval;
}

static void __exit cobalt_buttons_exit(void)
{
	platform_driver_unregister(&cobalt_buttons_driver);
	platform_device_unregister(cobalt_buttons_device);
}

module_init(cobalt_buttons_init);
module_exit(cobalt_buttons_exit);
