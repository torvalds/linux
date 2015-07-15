/*
 * SIRF hardware spinlock driver
 *
 * Copyright (c) 2015 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/hwspinlock.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "hwspinlock_internal.h"

struct sirf_hwspinlock {
	void __iomem *io_base;
	struct hwspinlock_device bank;
};

/* Number of Hardware Spinlocks*/
#define	HW_SPINLOCK_NUMBER	30

/* Hardware spinlock register offsets */
#define HW_SPINLOCK_BASE	0x404
#define HW_SPINLOCK_OFFSET(x)	(HW_SPINLOCK_BASE + 0x4 * (x))

static int sirf_hwspinlock_trylock(struct hwspinlock *lock)
{
	void __iomem *lock_addr = lock->priv;

	/* attempt to acquire the lock by reading value == 1 from it */
	return !!readl(lock_addr);
}

static void sirf_hwspinlock_unlock(struct hwspinlock *lock)
{
	void __iomem *lock_addr = lock->priv;

	/* release the lock by writing 0 to it */
	writel(0, lock_addr);
}

static const struct hwspinlock_ops sirf_hwspinlock_ops = {
	.trylock = sirf_hwspinlock_trylock,
	.unlock = sirf_hwspinlock_unlock,
};

static int sirf_hwspinlock_probe(struct platform_device *pdev)
{
	struct sirf_hwspinlock *hwspin;
	struct hwspinlock *hwlock;
	int idx, ret;

	if (!pdev->dev.of_node)
		return -ENODEV;

	hwspin = devm_kzalloc(&pdev->dev, sizeof(*hwspin) +
			sizeof(*hwlock) * HW_SPINLOCK_NUMBER, GFP_KERNEL);
	if (!hwspin)
		return -ENOMEM;

	/* retrieve io base */
	hwspin->io_base = of_iomap(pdev->dev.of_node, 0);
	if (!hwspin->io_base)
		return -ENOMEM;

	for (idx = 0; idx < HW_SPINLOCK_NUMBER; idx++) {
		hwlock = &hwspin->bank.lock[idx];
		hwlock->priv = hwspin->io_base + HW_SPINLOCK_OFFSET(idx);
	}

	platform_set_drvdata(pdev, hwspin);

	pm_runtime_enable(&pdev->dev);

	ret = hwspin_lock_register(&hwspin->bank, &pdev->dev,
				   &sirf_hwspinlock_ops, 0,
				   HW_SPINLOCK_NUMBER);
	if (ret)
		goto reg_failed;

	return 0;

reg_failed:
	pm_runtime_disable(&pdev->dev);
	iounmap(hwspin->io_base);

	return ret;
}

static int sirf_hwspinlock_remove(struct platform_device *pdev)
{
	struct sirf_hwspinlock *hwspin = platform_get_drvdata(pdev);
	int ret;

	ret = hwspin_lock_unregister(&hwspin->bank);
	if (ret) {
		dev_err(&pdev->dev, "%s failed: %d\n", __func__, ret);
		return ret;
	}

	pm_runtime_disable(&pdev->dev);

	iounmap(hwspin->io_base);

	return 0;
}

static const struct of_device_id sirf_hwpinlock_ids[] = {
	{ .compatible = "sirf,hwspinlock", },
	{},
};
MODULE_DEVICE_TABLE(of, sirf_hwpinlock_ids);

static struct platform_driver sirf_hwspinlock_driver = {
	.probe = sirf_hwspinlock_probe,
	.remove = sirf_hwspinlock_remove,
	.driver = {
		.name = "atlas7_hwspinlock",
		.of_match_table = of_match_ptr(sirf_hwpinlock_ids),
	},
};

module_platform_driver(sirf_hwspinlock_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("SIRF Hardware spinlock driver");
MODULE_AUTHOR("Wei Chen <wei.chen@csr.com>");
