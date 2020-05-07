// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Cobalt button interface driver.
 *
 *  Copyright (C) 2007-2008  Yoichi Yuasa <yuasa@linux-mips.org>
 */
#include <linux/input.h>
#include <linux/io.h>
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
	unsigned short keymap[ARRAY_SIZE(cobalt_map)];
	int count[ARRAY_SIZE(cobalt_map)];
	void __iomem *reg;
};

static void handle_buttons(struct input_dev *input)
{
	struct buttons_dev *bdev = input_get_drvdata(input);
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
	struct input_dev *input;
	struct resource *res;
	int error, i;

	bdev = devm_kzalloc(&pdev->dev, sizeof(*bdev), GFP_KERNEL);
	if (!bdev)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EBUSY;

	bdev->reg = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!bdev->reg)
		return -ENOMEM;

	memcpy(bdev->keymap, cobalt_map, sizeof(bdev->keymap));

	input = devm_input_allocate_device(&pdev->dev);
	if (!input)
		return -ENOMEM;

	input_set_drvdata(input, bdev);

	input->name = "Cobalt buttons";
	input->phys = "cobalt/input0";
	input->id.bustype = BUS_HOST;

	input->keycode = bdev->keymap;
	input->keycodemax = ARRAY_SIZE(bdev->keymap);
	input->keycodesize = sizeof(unsigned short);

	input_set_capability(input, EV_MSC, MSC_SCAN);
	__set_bit(EV_KEY, input->evbit);
	for (i = 0; i < ARRAY_SIZE(cobalt_map); i++)
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

MODULE_AUTHOR("Yoichi Yuasa <yuasa@linux-mips.org>");
MODULE_DESCRIPTION("Cobalt button interface driver");
MODULE_LICENSE("GPL");
/* work with hotplug and coldplug */
MODULE_ALIAS("platform:Cobalt buttons");

static struct platform_driver cobalt_buttons_driver = {
	.probe	= cobalt_buttons_probe,
	.driver	= {
		.name	= "Cobalt buttons",
	},
};
module_platform_driver(cobalt_buttons_driver);
