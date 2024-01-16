/*
 * Hisilicon PMIC powerkey driver
 *
 * Copyright (C) 2013 Hisilicon Ltd.
 * Copyright (C) 2015, 2016 Linaro Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/input.h>
#include <linux/slab.h>

/* the held interrupt will trigger after 4 seconds */
#define MAX_HELD_TIME	(4 * MSEC_PER_SEC)

static irqreturn_t hi65xx_power_press_isr(int irq, void *q)
{
	struct input_dev *input = q;

	pm_wakeup_event(input->dev.parent, MAX_HELD_TIME);
	input_report_key(input, KEY_POWER, 1);
	input_sync(input);

	return IRQ_HANDLED;
}

static irqreturn_t hi65xx_power_release_isr(int irq, void *q)
{
	struct input_dev *input = q;

	pm_wakeup_event(input->dev.parent, MAX_HELD_TIME);
	input_report_key(input, KEY_POWER, 0);
	input_sync(input);

	return IRQ_HANDLED;
}

static irqreturn_t hi65xx_restart_toggle_isr(int irq, void *q)
{
	struct input_dev *input = q;
	int value = test_bit(KEY_RESTART, input->key);

	pm_wakeup_event(input->dev.parent, MAX_HELD_TIME);
	input_report_key(input, KEY_RESTART, !value);
	input_sync(input);

	return IRQ_HANDLED;
}

static const struct {
	const char *name;
	irqreturn_t (*handler)(int irq, void *q);
} hi65xx_irq_info[] = {
	{ "down", hi65xx_power_press_isr },
	{ "up", hi65xx_power_release_isr },
	{ "hold 4s", hi65xx_restart_toggle_isr },
};

static int hi65xx_powerkey_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct input_dev *input;
	int irq, i, error;

	input = devm_input_allocate_device(dev);
	if (!input) {
		dev_err(dev, "failed to allocate input device\n");
		return -ENOMEM;
	}

	input->phys = "hisi_on/input0";
	input->name = "HISI 65xx PowerOn Key";

	input_set_capability(input, EV_KEY, KEY_POWER);
	input_set_capability(input, EV_KEY, KEY_RESTART);

	for (i = 0; i < ARRAY_SIZE(hi65xx_irq_info); i++) {

		irq = platform_get_irq_byname(pdev, hi65xx_irq_info[i].name);
		if (irq < 0)
			return irq;

		error = devm_request_any_context_irq(dev, irq,
						     hi65xx_irq_info[i].handler,
						     IRQF_ONESHOT,
						     hi65xx_irq_info[i].name,
						     input);
		if (error < 0) {
			dev_err(dev, "couldn't request irq %s: %d\n",
				hi65xx_irq_info[i].name, error);
			return error;
		}
	}

	error = input_register_device(input);
	if (error) {
		dev_err(dev, "failed to register input device: %d\n", error);
		return error;
	}

	device_init_wakeup(dev, 1);

	return 0;
}

static struct platform_driver hi65xx_powerkey_driver = {
	.driver = {
		.name = "hi65xx-powerkey",
	},
	.probe = hi65xx_powerkey_probe,
};
module_platform_driver(hi65xx_powerkey_driver);

MODULE_AUTHOR("Zhiliang Xue <xuezhiliang@huawei.com");
MODULE_DESCRIPTION("Hisi PMIC Power key driver");
MODULE_LICENSE("GPL v2");
