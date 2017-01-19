/*
 * Power button driver for Intel MID platforms.
 *
 * Copyright (C) 2010 Intel Corp
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/mfd/intel_msic.h>
#include <linux/pm_wakeirq.h>

#define DRIVER_NAME "msic_power_btn"

#define MSIC_PB_LEVEL	(1 << 3) /* 1 - release, 0 - press */

/*
 * MSIC document ti_datasheet defines the 1st bit reg 0x21 is used to mask
 * power button interrupt
 */
#define MSIC_PWRBTNM    (1 << 0)

static irqreturn_t mid_pb_isr(int irq, void *dev_id)
{
	struct input_dev *input = dev_id;
	int ret;
	u8 pbstat;

	ret = intel_msic_reg_read(INTEL_MSIC_PBSTATUS, &pbstat);
	dev_dbg(input->dev.parent, "PB_INT status= %d\n", pbstat);

	if (ret < 0) {
		dev_err(input->dev.parent, "Read error %d while reading"
			       " MSIC_PB_STATUS\n", ret);
	} else {
		input_event(input, EV_KEY, KEY_POWER,
			       !(pbstat & MSIC_PB_LEVEL));
		input_sync(input);
	}

	return IRQ_HANDLED;
}

static int mid_pb_probe(struct platform_device *pdev)
{
	struct input_dev *input;
	int irq = platform_get_irq(pdev, 0);
	int error;

	if (irq < 0)
		return -EINVAL;

	input = devm_input_allocate_device(&pdev->dev);
	if (!input)
		return -ENOMEM;

	input->name = pdev->name;
	input->phys = "power-button/input0";
	input->id.bustype = BUS_HOST;
	input->dev.parent = &pdev->dev;

	input_set_capability(input, EV_KEY, KEY_POWER);

	error = devm_request_threaded_irq(&pdev->dev, irq, NULL, mid_pb_isr,
					  IRQF_ONESHOT, DRIVER_NAME, input);
	if (error) {
		dev_err(&pdev->dev, "Unable to request irq %d for MID power"
				"button\n", irq);
		return error;
	}

	error = input_register_device(input);
	if (error) {
		dev_err(&pdev->dev, "Unable to register input dev, error "
				"%d\n", error);
		return error;
	}

	platform_set_drvdata(pdev, input);

	/*
	 * SCU firmware might send power button interrupts to IA core before
	 * kernel boots and doesn't get EOI from IA core. The first bit of
	 * MSIC reg 0x21 is kept masked, and SCU firmware doesn't send new
	 * power interrupt to Android kernel. Unmask the bit when probing
	 * power button in kernel.
	 * There is a very narrow race between irq handler and power button
	 * initialization. The race happens rarely. So we needn't worry
	 * about it.
	 */
	error = intel_msic_reg_update(INTEL_MSIC_IRQLVL1MSK, 0, MSIC_PWRBTNM);
	if (error) {
		dev_err(&pdev->dev, "Unable to clear power button interrupt, "
				"error: %d\n", error);
		return error;
	}

	device_init_wakeup(&pdev->dev, true);
	dev_pm_set_wake_irq(&pdev->dev, irq);

	return 0;
}

static int mid_pb_remove(struct platform_device *pdev)
{
	dev_pm_clear_wake_irq(&pdev->dev);
	device_init_wakeup(&pdev->dev, false);

	return 0;
}

static struct platform_driver mid_pb_driver = {
	.driver = {
		.name = DRIVER_NAME,
	},
	.probe	= mid_pb_probe,
	.remove	= mid_pb_remove,
};

module_platform_driver(mid_pb_driver);

MODULE_AUTHOR("Hong Liu <hong.liu@intel.com>");
MODULE_DESCRIPTION("Intel MID Power Button Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
