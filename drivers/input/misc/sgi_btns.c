/*
 *  SGI Volume Button interface driver
 *
 *  Copyright (C) 2008  Thomas Bogendoerfer <tsbogend@alpha.franken.de>
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
#include <linux/slab.h>

#ifdef CONFIG_SGI_IP22
#include <asm/sgi/ioc.h>

static inline u8 button_status(void)
{
	u8 status;

	status = readb(&sgioc->panel) ^ 0xa0;
	return ((status & 0x80) >> 6) | ((status & 0x20) >> 5);
}
#endif

#ifdef CONFIG_SGI_IP32
#include <asm/ip32/mace.h>

static inline u8 button_status(void)
{
	u64 status;

	status = readq(&mace->perif.audio.control);
	writeq(status & ~(3U << 23), &mace->perif.audio.control);

	return (status >> 23) & 3;
}
#endif

#define BUTTONS_POLL_INTERVAL	30	/* msec */
#define BUTTONS_COUNT_THRESHOLD	3

static const unsigned short sgi_map[] = {
	KEY_VOLUMEDOWN,
	KEY_VOLUMEUP
};

struct buttons_dev {
	struct input_polled_dev *poll_dev;
	unsigned short keymap[ARRAY_SIZE(sgi_map)];
	int count[ARRAY_SIZE(sgi_map)];
};

static void handle_buttons(struct input_polled_dev *dev)
{
	struct buttons_dev *bdev = dev->private;
	struct input_dev *input = dev->input;
	u8 status;
	int i;

	status = button_status();

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

static int __devinit sgi_buttons_probe(struct platform_device *pdev)
{
	struct buttons_dev *bdev;
	struct input_polled_dev *poll_dev;
	struct input_dev *input;
	int error, i;

	bdev = kzalloc(sizeof(struct buttons_dev), GFP_KERNEL);
	poll_dev = input_allocate_polled_device();
	if (!bdev || !poll_dev) {
		error = -ENOMEM;
		goto err_free_mem;
	}

	memcpy(bdev->keymap, sgi_map, sizeof(bdev->keymap));

	poll_dev->private = bdev;
	poll_dev->poll = handle_buttons;
	poll_dev->poll_interval = BUTTONS_POLL_INTERVAL;

	input = poll_dev->input;
	input->name = "SGI buttons";
	input->phys = "sgi/input0";
	input->id.bustype = BUS_HOST;
	input->dev.parent = &pdev->dev;

	input->keycode = bdev->keymap;
	input->keycodemax = ARRAY_SIZE(bdev->keymap);
	input->keycodesize = sizeof(unsigned short);

	input_set_capability(input, EV_MSC, MSC_SCAN);
	__set_bit(EV_KEY, input->evbit);
	for (i = 0; i < ARRAY_SIZE(sgi_map); i++)
		__set_bit(bdev->keymap[i], input->keybit);
	__clear_bit(KEY_RESERVED, input->keybit);

	bdev->poll_dev = poll_dev;
	dev_set_drvdata(&pdev->dev, bdev);

	error = input_register_polled_device(poll_dev);
	if (error)
		goto err_free_mem;

	return 0;

 err_free_mem:
	input_free_polled_device(poll_dev);
	kfree(bdev);
	dev_set_drvdata(&pdev->dev, NULL);
	return error;
}

static int __devexit sgi_buttons_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct buttons_dev *bdev = dev_get_drvdata(dev);

	input_unregister_polled_device(bdev->poll_dev);
	input_free_polled_device(bdev->poll_dev);
	kfree(bdev);
	dev_set_drvdata(dev, NULL);

	return 0;
}

static struct platform_driver sgi_buttons_driver = {
	.probe	= sgi_buttons_probe,
	.remove	= sgi_buttons_remove,
	.driver	= {
		.name	= "sgibtns",
		.owner	= THIS_MODULE,
	},
};
module_platform_driver(sgi_buttons_driver);

MODULE_LICENSE("GPL");
