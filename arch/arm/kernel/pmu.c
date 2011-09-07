/*
 *  linux/arch/arm/kernel/pmu.c
 *
 *  Copyright (C) 2009 picoChip Designs Ltd, Jamie Iles
 *  Copyright (C) 2010 ARM Ltd, Will Deacon
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define pr_fmt(fmt) "PMU: " fmt

#include <linux/cpumask.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <asm/pmu.h>

static volatile long pmu_lock;

static struct platform_device *pmu_devices[ARM_NUM_PMU_DEVICES];

static int __devinit pmu_register(struct platform_device *pdev,
					enum arm_pmu_type type)
{
	if (type < 0 || type >= ARM_NUM_PMU_DEVICES) {
		pr_warning("received registration request for unknown "
				"device %d\n", type);
		return -EINVAL;
	}

	if (pmu_devices[type]) {
		pr_warning("rejecting duplicate registration of PMU device "
			"type %d.", type);
		return -ENOSPC;
	}

	pr_info("registered new PMU device of type %d\n", type);
	pmu_devices[type] = pdev;
	return 0;
}

#define OF_MATCH_PMU(_name, _type) {	\
	.compatible = _name,		\
	.data = (void *)_type,		\
}

#define OF_MATCH_CPU(name)	OF_MATCH_PMU(name, ARM_PMU_DEVICE_CPU)

static struct of_device_id armpmu_of_device_ids[] = {
	OF_MATCH_CPU("arm,cortex-a9-pmu"),
	OF_MATCH_CPU("arm,cortex-a8-pmu"),
	OF_MATCH_CPU("arm,arm1136-pmu"),
	OF_MATCH_CPU("arm,arm1176-pmu"),
	{},
};

#define PLAT_MATCH_PMU(_name, _type) {	\
	.name		= _name,	\
	.driver_data	= _type,	\
}

#define PLAT_MATCH_CPU(_name)	PLAT_MATCH_PMU(_name, ARM_PMU_DEVICE_CPU)

static struct platform_device_id armpmu_plat_device_ids[] = {
	PLAT_MATCH_CPU("arm-pmu"),
	{},
};

enum arm_pmu_type armpmu_device_type(struct platform_device *pdev)
{
	const struct of_device_id	*of_id;
	const struct platform_device_id *pdev_id;

	/* provided by of_device_id table */
	if (pdev->dev.of_node) {
		of_id = of_match_device(armpmu_of_device_ids, &pdev->dev);
		BUG_ON(!of_id);
		return (enum arm_pmu_type)of_id->data;
	}

	/* Provided by platform_device_id table */
	pdev_id = platform_get_device_id(pdev);
	BUG_ON(!pdev_id);
	return pdev_id->driver_data;
}

static int __devinit armpmu_device_probe(struct platform_device *pdev)
{
	return pmu_register(pdev, armpmu_device_type(pdev));
}

static struct platform_driver armpmu_driver = {
	.driver		= {
		.name	= "arm-pmu",
		.of_match_table = armpmu_of_device_ids,
	},
	.probe		= armpmu_device_probe,
	.id_table	= armpmu_plat_device_ids,
};

static int __init register_pmu_driver(void)
{
	return platform_driver_register(&armpmu_driver);
}
device_initcall(register_pmu_driver);

struct platform_device *
reserve_pmu(enum arm_pmu_type device)
{
	struct platform_device *pdev;

	if (test_and_set_bit_lock(device, &pmu_lock)) {
		pdev = ERR_PTR(-EBUSY);
	} else if (pmu_devices[device] == NULL) {
		clear_bit_unlock(device, &pmu_lock);
		pdev = ERR_PTR(-ENODEV);
	} else {
		pdev = pmu_devices[device];
	}

	return pdev;
}
EXPORT_SYMBOL_GPL(reserve_pmu);

int
release_pmu(enum arm_pmu_type device)
{
	if (WARN_ON(!pmu_devices[device]))
		return -EINVAL;
	clear_bit_unlock(device, &pmu_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(release_pmu);

static int
set_irq_affinity(int irq,
		 unsigned int cpu)
{
#ifdef CONFIG_SMP
	int err = irq_set_affinity(irq, cpumask_of(cpu));
	if (err)
		pr_warning("unable to set irq affinity (irq=%d, cpu=%u)\n",
			   irq, cpu);
	return err;
#else
	return -EINVAL;
#endif
}

static int
init_cpu_pmu(void)
{
	int i, irqs, err = 0;
	struct platform_device *pdev = pmu_devices[ARM_PMU_DEVICE_CPU];

	if (!pdev)
		return -ENODEV;

	irqs = pdev->num_resources;

	/*
	 * If we have a single PMU interrupt that we can't shift, assume that
	 * we're running on a uniprocessor machine and continue.
	 */
	if (irqs == 1 && !irq_can_set_affinity(platform_get_irq(pdev, 0)))
		return 0;

	for (i = 0; i < irqs; ++i) {
		err = set_irq_affinity(platform_get_irq(pdev, i), i);
		if (err)
			break;
	}

	return err;
}

int
init_pmu(enum arm_pmu_type device)
{
	int err = 0;

	switch (device) {
	case ARM_PMU_DEVICE_CPU:
		err = init_cpu_pmu();
		break;
	default:
		pr_warning("attempt to initialise unknown device %d\n",
				device);
		err = -EINVAL;
	}

	return err;
}
EXPORT_SYMBOL_GPL(init_pmu);
