// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Fuzhou Rockchip Electronics Co., Ltd

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

struct rockchip_hwspinlock {
	void __iomem *io_base;
	struct hwspinlock_device bank;
};

/* Number of Hardware Spinlocks*/
#define	HWSPINLOCK_NUMBER	64

/* Hardware spinlock register offsets */
#define HWSPINLOCK_OFFSET(x)	(0x4 * (x))

#define HWSPINLOCK_OWNER_ID	0x01

static int rockchip_hwspinlock_trylock(struct hwspinlock *lock)
{
	void __iomem *lock_addr = lock->priv;

	writel(HWSPINLOCK_OWNER_ID, lock_addr);

	/*
	 * Get only first 4 bits and compare to HWSPINLOCK_OWNER_ID,
	 * if equal, we attempt to acquire the lock, otherwise,
	 * someone else has it.
	 */
	return (HWSPINLOCK_OWNER_ID == (0x0F & readl(lock_addr)));
}

static void rockchip_hwspinlock_unlock(struct hwspinlock *lock)
{
	void __iomem *lock_addr = lock->priv;

	/* Release the lock by writing 0 to it */
	writel(0, lock_addr);
}

static const struct hwspinlock_ops rockchip_hwspinlock_ops = {
	.trylock = rockchip_hwspinlock_trylock,
	.unlock = rockchip_hwspinlock_unlock,
};

static int rockchip_hwspinlock_probe(struct platform_device *pdev)
{
	struct rockchip_hwspinlock *hwspin;
	struct hwspinlock *hwlock;
	struct resource *res;
	int idx, ret;

	if (!pdev->dev.of_node)
		return -ENODEV;

	hwspin = devm_kzalloc(&pdev->dev, sizeof(*hwspin) +
			      sizeof(*hwlock) * HWSPINLOCK_NUMBER, GFP_KERNEL);
	if (!hwspin)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hwspin->io_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hwspin->io_base))
		return PTR_ERR(hwspin->io_base);

	for (idx = 0; idx < HWSPINLOCK_NUMBER; idx++) {
		hwlock = &hwspin->bank.lock[idx];
		hwlock->priv = hwspin->io_base + HWSPINLOCK_OFFSET(idx);
	}

	platform_set_drvdata(pdev, hwspin);

	pm_runtime_enable(&pdev->dev);

	ret = hwspin_lock_register(&hwspin->bank, &pdev->dev,
				   &rockchip_hwspinlock_ops, 0,
				   HWSPINLOCK_NUMBER);
	if (ret)
		goto reg_fail;

	return 0;

reg_fail:
	pm_runtime_disable(&pdev->dev);
	iounmap(hwspin->io_base);

	return ret;
}

static int rockchip_hwspinlock_remove(struct platform_device *pdev)
{
	struct rockchip_hwspinlock *hwspin = platform_get_drvdata(pdev);
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

static const struct of_device_id rockchip_hwpinlock_ids[] = {
	{ .compatible = "rockchip,hwspinlock", },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_hwpinlock_ids);

static struct platform_driver rockchip_hwspinlock_driver = {
	.probe = rockchip_hwspinlock_probe,
	.remove = rockchip_hwspinlock_remove,
	.driver = {
		.name = "rockchip_hwspinlock",
		.of_match_table = of_match_ptr(rockchip_hwpinlock_ids),
	},
};

static int __init rockchip_hwspinlock_init(void)
{
	return platform_driver_register(&rockchip_hwspinlock_driver);
}
postcore_initcall(rockchip_hwspinlock_init);

static void __exit rockchip_hwspinlock_exit(void)
{
	platform_driver_unregister(&rockchip_hwspinlock_driver);
}
module_exit(rockchip_hwspinlock_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Rockchip Hardware spinlock driver");
MODULE_AUTHOR("Frank Wang <frank.wang@rock-chips.com>");
