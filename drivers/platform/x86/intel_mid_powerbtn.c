// SPDX-License-Identifier: GPL-2.0
/*
 * Power button driver for Intel MID platforms.
 *
 * Copyright (C) 2010,2017 Intel Corp
 *
 * Author: Hong Liu <hong.liu@intel.com>
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */

#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/mfd/intel_msic.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/slab.h>

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/intel_scu_ipc.h>

#define DRIVER_NAME "msic_power_btn"

#define MSIC_PB_LEVEL	(1 << 3) /* 1 - release, 0 - press */

/*
 * MSIC document ti_datasheet defines the 1st bit reg 0x21 is used to mask
 * power button interrupt
 */
#define MSIC_PWRBTNM    (1 << 0)

/* Intel Tangier */
#define BCOVE_PB_LEVEL		(1 << 4)	/* 1 - release, 0 - press */

/* Basin Cove PMIC */
#define BCOVE_PBIRQ		0x02
#define BCOVE_IRQLVL1MSK	0x0c
#define BCOVE_PBIRQMASK		0x0d
#define BCOVE_PBSTATUS		0x27

struct mid_pb_ddata {
	struct device *dev;
	int irq;
	struct input_dev *input;
	unsigned short mirqlvl1_addr;
	unsigned short pbstat_addr;
	u8 pbstat_mask;
	int (*setup)(struct mid_pb_ddata *ddata);
};

static int mid_pbstat(struct mid_pb_ddata *ddata, int *value)
{
	struct input_dev *input = ddata->input;
	int ret;
	u8 pbstat;

	ret = intel_scu_ipc_ioread8(ddata->pbstat_addr, &pbstat);
	if (ret)
		return ret;

	dev_dbg(input->dev.parent, "PB_INT status= %d\n", pbstat);

	*value = !(pbstat & ddata->pbstat_mask);
	return 0;
}

static int mid_irq_ack(struct mid_pb_ddata *ddata)
{
	return intel_scu_ipc_update_register(ddata->mirqlvl1_addr, 0, MSIC_PWRBTNM);
}

static int mrfld_setup(struct mid_pb_ddata *ddata)
{
	/* Unmask the PBIRQ and MPBIRQ on Tangier */
	intel_scu_ipc_update_register(BCOVE_PBIRQ, 0, MSIC_PWRBTNM);
	intel_scu_ipc_update_register(BCOVE_PBIRQMASK, 0, MSIC_PWRBTNM);

	return 0;
}

static irqreturn_t mid_pb_isr(int irq, void *dev_id)
{
	struct mid_pb_ddata *ddata = dev_id;
	struct input_dev *input = ddata->input;
	int value = 0;
	int ret;

	ret = mid_pbstat(ddata, &value);
	if (ret < 0) {
		dev_err(input->dev.parent,
			"Read error %d while reading MSIC_PB_STATUS\n", ret);
	} else {
		input_event(input, EV_KEY, KEY_POWER, value);
		input_sync(input);
	}

	mid_irq_ack(ddata);
	return IRQ_HANDLED;
}

static const struct mid_pb_ddata mfld_ddata = {
	.mirqlvl1_addr	= INTEL_MSIC_IRQLVL1MSK,
	.pbstat_addr	= INTEL_MSIC_PBSTATUS,
	.pbstat_mask	= MSIC_PB_LEVEL,
};

static const struct mid_pb_ddata mrfld_ddata = {
	.mirqlvl1_addr	= BCOVE_IRQLVL1MSK,
	.pbstat_addr	= BCOVE_PBSTATUS,
	.pbstat_mask	= BCOVE_PB_LEVEL,
	.setup	= mrfld_setup,
};

static const struct x86_cpu_id mid_pb_cpu_ids[] = {
	INTEL_CPU_FAM6(ATOM_SALTWELL_MID,	mfld_ddata),
	INTEL_CPU_FAM6(ATOM_SILVERMONT_MID,	mrfld_ddata),
	{}
};

static int mid_pb_probe(struct platform_device *pdev)
{
	const struct x86_cpu_id *id;
	struct mid_pb_ddata *ddata;
	struct input_dev *input;
	int irq = platform_get_irq(pdev, 0);
	int error;

	id = x86_match_cpu(mid_pb_cpu_ids);
	if (!id)
		return -ENODEV;

	if (irq < 0) {
		dev_err(&pdev->dev, "Failed to get IRQ: %d\n", irq);
		return irq;
	}

	input = devm_input_allocate_device(&pdev->dev);
	if (!input)
		return -ENOMEM;

	input->name = pdev->name;
	input->phys = "power-button/input0";
	input->id.bustype = BUS_HOST;
	input->dev.parent = &pdev->dev;

	input_set_capability(input, EV_KEY, KEY_POWER);

	ddata = devm_kmemdup(&pdev->dev, (void *)id->driver_data,
			     sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	ddata->dev = &pdev->dev;
	ddata->irq = irq;
	ddata->input = input;

	if (ddata->setup) {
		error = ddata->setup(ddata);
		if (error)
			return error;
	}

	error = devm_request_threaded_irq(&pdev->dev, irq, NULL, mid_pb_isr,
					  IRQF_ONESHOT, DRIVER_NAME, ddata);
	if (error) {
		dev_err(&pdev->dev,
			"Unable to request irq %d for MID power button\n", irq);
		return error;
	}

	error = input_register_device(input);
	if (error) {
		dev_err(&pdev->dev,
			"Unable to register input dev, error %d\n", error);
		return error;
	}

	platform_set_drvdata(pdev, ddata);

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
	error = mid_irq_ack(ddata);
	if (error) {
		dev_err(&pdev->dev,
			"Unable to clear power button interrupt, error: %d\n",
			error);
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
