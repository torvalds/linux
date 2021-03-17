// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  SGI Volume Button interface driver
 *
 *  Copyright (C) 2008  Thomas Bogendoerfer <tsbogend@alpha.franken.de>
 */
#include <linux/input.h>
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
	unsigned short keymap[ARRAY_SIZE(sgi_map)];
	int count[ARRAY_SIZE(sgi_map)];
};

static void handle_buttons(struct input_dev *input)
{
	struct buttons_dev *bdev = input_get_drvdata(input);
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

static int sgi_buttons_probe(struct platform_device *pdev)
{
	struct buttons_dev *bdev;
	struct input_dev *input;
	int error, i;

	bdev = devm_kzalloc(&pdev->dev, sizeof(*bdev), GFP_KERNEL);
	if (!bdev)
		return -ENOMEM;

	input = devm_input_allocate_device(&pdev->dev);
	if (!input)
		return -ENOMEM;

	memcpy(bdev->keymap, sgi_map, sizeof(bdev->keymap));

	input_set_drvdata(input, bdev);

	input->name = "SGI buttons";
	input->phys = "sgi/input0";
	input->id.bustype = BUS_HOST;

	input->keycode = bdev->keymap;
	input->keycodemax = ARRAY_SIZE(bdev->keymap);
	input->keycodesize = sizeof(unsigned short);

	input_set_capability(input, EV_MSC, MSC_SCAN);
	__set_bit(EV_KEY, input->evbit);
	for (i = 0; i < ARRAY_SIZE(sgi_map); i++)
		__set_bit(bdev->keymap[i], input->keybit);
	__clear_bit(KEY_RESERVED, input->keybit);

	error = input_setup_polling(input, handle_buttons);
	if (error)
		return error;

	input_set_poll_interval(input, BUTTONS_POLL_INTERVAL);

	error = input_register_device(input);
	if (error)
		return error;

	return 0;
}

static struct platform_driver sgi_buttons_driver = {
	.probe	= sgi_buttons_probe,
	.driver	= {
		.name	= "sgibtns",
	},
};
module_platform_driver(sgi_buttons_driver);

MODULE_LICENSE("GPL");
