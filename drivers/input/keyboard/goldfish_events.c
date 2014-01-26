/*
 * Copyright (C) 2007 Google, Inc.
 * Copyright (C) 2012 Intel, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/io.h>

enum {
	REG_READ        = 0x00,
	REG_SET_PAGE    = 0x00,
	REG_LEN         = 0x04,
	REG_DATA        = 0x08,

	PAGE_NAME       = 0x00000,
	PAGE_EVBITS     = 0x10000,
	PAGE_ABSDATA    = 0x20000 | EV_ABS,
};

struct event_dev {
	struct input_dev *input;
	int irq;
	void __iomem *addr;
	char name[0];
};

static irqreturn_t events_interrupt(int irq, void *dev_id)
{
	struct event_dev *edev = dev_id;
	unsigned type, code, value;

	type = __raw_readl(edev->addr + REG_READ);
	code = __raw_readl(edev->addr + REG_READ);
	value = __raw_readl(edev->addr + REG_READ);

	input_event(edev->input, type, code, value);
	input_sync(edev->input);
	return IRQ_HANDLED;
}

static void events_import_bits(struct event_dev *edev,
			unsigned long bits[], unsigned type, size_t count)
{
	void __iomem *addr = edev->addr;
	int i, j;
	size_t size;
	uint8_t val;

	__raw_writel(PAGE_EVBITS | type, addr + REG_SET_PAGE);

	size = __raw_readl(addr + REG_LEN) * 8;
	if (size < count)
		count = size;

	addr += REG_DATA;
	for (i = 0; i < count; i += 8) {
		val = __raw_readb(addr++);
		for (j = 0; j < 8; j++)
			if (val & 1 << j)
				set_bit(i + j, bits);
	}
}

static void events_import_abs_params(struct event_dev *edev)
{
	struct input_dev *input_dev = edev->input;
	void __iomem *addr = edev->addr;
	u32 val[4];
	int count;
	int i, j;

	__raw_writel(PAGE_ABSDATA, addr + REG_SET_PAGE);

	count = __raw_readl(addr + REG_LEN) / sizeof(val);
	if (count > ABS_MAX)
		count = ABS_MAX;

	for (i = 0; i < count; i++) {
		if (!test_bit(i, input_dev->absbit))
			continue;

		for (j = 0; j < ARRAY_SIZE(val); j++) {
			int offset = (i * ARRAY_SIZE(val) + j) * sizeof(u32);
			val[j] = __raw_readl(edev->addr + REG_DATA + offset);
		}

		input_set_abs_params(input_dev, i,
				     val[0], val[1], val[2], val[3]);
	}
}

static int events_probe(struct platform_device *pdev)
{
	struct input_dev *input_dev;
	struct event_dev *edev;
	struct resource *res;
	unsigned keymapnamelen;
	void __iomem *addr;
	int irq;
	int i;
	int error;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	addr = devm_ioremap(&pdev->dev, res->start, 4096);
	if (!addr)
		return -ENOMEM;

	__raw_writel(PAGE_NAME, addr + REG_SET_PAGE);
	keymapnamelen = __raw_readl(addr + REG_LEN);

	edev = devm_kzalloc(&pdev->dev,
			    sizeof(struct event_dev) + keymapnamelen + 1,
			    GFP_KERNEL);
	if (!edev)
		return -ENOMEM;

	input_dev = devm_input_allocate_device(&pdev->dev);
	if (!input_dev)
		return -ENOMEM;

	edev->input = input_dev;
	edev->addr = addr;
	edev->irq = irq;

	for (i = 0; i < keymapnamelen; i++)
		edev->name[i] = __raw_readb(edev->addr + REG_DATA + i);

	pr_debug("events_probe() keymap=%s\n", edev->name);

	input_dev->name = edev->name;
	input_dev->id.bustype = BUS_HOST;

	events_import_bits(edev, input_dev->evbit, EV_SYN, EV_MAX);
	events_import_bits(edev, input_dev->keybit, EV_KEY, KEY_MAX);
	events_import_bits(edev, input_dev->relbit, EV_REL, REL_MAX);
	events_import_bits(edev, input_dev->absbit, EV_ABS, ABS_MAX);
	events_import_bits(edev, input_dev->mscbit, EV_MSC, MSC_MAX);
	events_import_bits(edev, input_dev->ledbit, EV_LED, LED_MAX);
	events_import_bits(edev, input_dev->sndbit, EV_SND, SND_MAX);
	events_import_bits(edev, input_dev->ffbit, EV_FF, FF_MAX);
	events_import_bits(edev, input_dev->swbit, EV_SW, SW_MAX);

	events_import_abs_params(edev);

	error = devm_request_irq(&pdev->dev, edev->irq, events_interrupt, 0,
				 "goldfish-events-keypad", edev);
	if (error)
		return error;

	error = input_register_device(input_dev);
	if (error)
		return error;

	return 0;
}

static struct platform_driver events_driver = {
	.probe	= events_probe,
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "goldfish_events",
	},
};

module_platform_driver(events_driver);

MODULE_AUTHOR("Brian Swetland");
MODULE_DESCRIPTION("Goldfish Event Device");
MODULE_LICENSE("GPL");
