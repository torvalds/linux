/*
 * OMAP hardware spinlock driver
 *
 * Copyright (C) 2010-2015 Texas Instruments Incorporated - http://www.ti.com
 *
 * Contact: Simon Que <sque@ti.com>
 *          Hari Kanigeri <h-kanigeri2@ti.com>
 *          Ohad Ben-Cohen <ohad@wizery.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/hwspinlock.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "hwspinlock_internal.h"

/* Spinlock register offsets */
#define SYSSTATUS_OFFSET		0x0014
#define LOCK_BASE_OFFSET		0x0800

#define SPINLOCK_NUMLOCKS_BIT_OFFSET	(24)

/* Possible values of SPINLOCK_LOCK_REG */
#define SPINLOCK_NOTTAKEN		(0)	/* free */
#define SPINLOCK_TAKEN			(1)	/* locked */

static int omap_hwspinlock_trylock(struct hwspinlock *lock)
{
	void __iomem *lock_addr = lock->priv;

	/* attempt to acquire the lock by reading its value */
	return (SPINLOCK_NOTTAKEN == readl(lock_addr));
}

static void omap_hwspinlock_unlock(struct hwspinlock *lock)
{
	void __iomem *lock_addr = lock->priv;

	/* release the lock by writing 0 to it */
	writel(SPINLOCK_NOTTAKEN, lock_addr);
}

/*
 * relax the OMAP interconnect while spinning on it.
 *
 * The specs recommended that the retry delay time will be
 * just over half of the time that a requester would be
 * expected to hold the lock.
 *
 * The number below is taken from an hardware specs example,
 * obviously it is somewhat arbitrary.
 */
static void omap_hwspinlock_relax(struct hwspinlock *lock)
{
	ndelay(50);
}

static const struct hwspinlock_ops omap_hwspinlock_ops = {
	.trylock = omap_hwspinlock_trylock,
	.unlock = omap_hwspinlock_unlock,
	.relax = omap_hwspinlock_relax,
};

static int omap_hwspinlock_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct hwspinlock_device *bank;
	struct hwspinlock *hwlock;
	struct resource *res;
	void __iomem *io_base;
	int num_locks, i, ret;
	/* Only a single hwspinlock block device is supported */
	int base_id = 0;

	if (!node)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	io_base = ioremap(res->start, resource_size(res));
	if (!io_base)
		return -ENOMEM;

	/*
	 * make sure the module is enabled and clocked before reading
	 * the module SYSSTATUS register
	 */
	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(&pdev->dev);
		goto iounmap_base;
	}

	/* Determine number of locks */
	i = readl(io_base + SYSSTATUS_OFFSET);
	i >>= SPINLOCK_NUMLOCKS_BIT_OFFSET;

	/*
	 * runtime PM will make sure the clock of this module is
	 * enabled again iff at least one lock is requested
	 */
	ret = pm_runtime_put(&pdev->dev);
	if (ret < 0)
		goto iounmap_base;

	/* one of the four lsb's must be set, and nothing else */
	if (hweight_long(i & 0xf) != 1 || i > 8) {
		ret = -EINVAL;
		goto iounmap_base;
	}

	num_locks = i * 32; /* actual number of locks in this device */

	bank = kzalloc(sizeof(*bank) + num_locks * sizeof(*hwlock), GFP_KERNEL);
	if (!bank) {
		ret = -ENOMEM;
		goto iounmap_base;
	}

	platform_set_drvdata(pdev, bank);

	for (i = 0, hwlock = &bank->lock[0]; i < num_locks; i++, hwlock++)
		hwlock->priv = io_base + LOCK_BASE_OFFSET + sizeof(u32) * i;

	ret = hwspin_lock_register(bank, &pdev->dev, &omap_hwspinlock_ops,
						base_id, num_locks);
	if (ret)
		goto reg_fail;

	return 0;

reg_fail:
	kfree(bank);
iounmap_base:
	pm_runtime_disable(&pdev->dev);
	iounmap(io_base);
	return ret;
}

static int omap_hwspinlock_remove(struct platform_device *pdev)
{
	struct hwspinlock_device *bank = platform_get_drvdata(pdev);
	void __iomem *io_base = bank->lock[0].priv - LOCK_BASE_OFFSET;
	int ret;

	ret = hwspin_lock_unregister(bank);
	if (ret) {
		dev_err(&pdev->dev, "%s failed: %d\n", __func__, ret);
		return ret;
	}

	pm_runtime_disable(&pdev->dev);
	iounmap(io_base);
	kfree(bank);

	return 0;
}

static const struct of_device_id omap_hwspinlock_of_match[] = {
	{ .compatible = "ti,omap4-hwspinlock", },
	{ /* end */ },
};
MODULE_DEVICE_TABLE(of, omap_hwspinlock_of_match);

static struct platform_driver omap_hwspinlock_driver = {
	.probe		= omap_hwspinlock_probe,
	.remove		= omap_hwspinlock_remove,
	.driver		= {
		.name	= "omap_hwspinlock",
		.of_match_table = of_match_ptr(omap_hwspinlock_of_match),
	},
};

static int __init omap_hwspinlock_init(void)
{
	return platform_driver_register(&omap_hwspinlock_driver);
}
/* board init code might need to reserve hwspinlocks for predefined purposes */
postcore_initcall(omap_hwspinlock_init);

static void __exit omap_hwspinlock_exit(void)
{
	platform_driver_unregister(&omap_hwspinlock_driver);
}
module_exit(omap_hwspinlock_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Hardware spinlock driver for OMAP");
MODULE_AUTHOR("Simon Que <sque@ti.com>");
MODULE_AUTHOR("Hari Kanigeri <h-kanigeri2@ti.com>");
MODULE_AUTHOR("Ohad Ben-Cohen <ohad@wizery.com>");
