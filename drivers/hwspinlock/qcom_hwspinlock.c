/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 * Copyright (c) 2015, Sony Mobile Communications AB
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/hwspinlock.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include "hwspinlock_internal.h"

#define QCOM_MUTEX_APPS_PROC_ID	1
#define QCOM_MUTEX_NUM_LOCKS	32

static int qcom_hwspinlock_trylock(struct hwspinlock *lock)
{
	struct regmap_field *field = lock->priv;
	u32 lock_owner;
	int ret;

	ret = regmap_field_write(field, QCOM_MUTEX_APPS_PROC_ID);
	if (ret)
		return ret;

	ret = regmap_field_read(field, &lock_owner);
	if (ret)
		return ret;

	return lock_owner == QCOM_MUTEX_APPS_PROC_ID;
}

static void qcom_hwspinlock_unlock(struct hwspinlock *lock)
{
	struct regmap_field *field = lock->priv;
	u32 lock_owner;
	int ret;

	ret = regmap_field_read(field, &lock_owner);
	if (ret) {
		pr_err("%s: unable to query spinlock owner\n", __func__);
		return;
	}

	if (lock_owner != QCOM_MUTEX_APPS_PROC_ID) {
		pr_err("%s: spinlock not owned by us (actual owner is %d)\n",
				__func__, lock_owner);
	}

	ret = regmap_field_write(field, 0);
	if (ret)
		pr_err("%s: failed to unlock spinlock\n", __func__);
}

static const struct hwspinlock_ops qcom_hwspinlock_ops = {
	.trylock	= qcom_hwspinlock_trylock,
	.unlock		= qcom_hwspinlock_unlock,
};

static const struct of_device_id qcom_hwspinlock_of_match[] = {
	{ .compatible = "qcom,sfpb-mutex" },
	{ .compatible = "qcom,tcsr-mutex" },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_hwspinlock_of_match);

static int qcom_hwspinlock_probe(struct platform_device *pdev)
{
	struct hwspinlock_device *bank;
	struct device_node *syscon;
	struct reg_field field;
	struct regmap *regmap;
	size_t array_size;
	u32 stride;
	u32 base;
	int ret;
	int i;

	syscon = of_parse_phandle(pdev->dev.of_node, "syscon", 0);
	if (!syscon) {
		dev_err(&pdev->dev, "no syscon property\n");
		return -ENODEV;
	}

	regmap = syscon_node_to_regmap(syscon);
	of_node_put(syscon);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = of_property_read_u32_index(pdev->dev.of_node, "syscon", 1, &base);
	if (ret < 0) {
		dev_err(&pdev->dev, "no offset in syscon\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_index(pdev->dev.of_node, "syscon", 2, &stride);
	if (ret < 0) {
		dev_err(&pdev->dev, "no stride syscon\n");
		return -EINVAL;
	}

	array_size = QCOM_MUTEX_NUM_LOCKS * sizeof(struct hwspinlock);
	bank = devm_kzalloc(&pdev->dev, sizeof(*bank) + array_size, GFP_KERNEL);
	if (!bank)
		return -ENOMEM;

	platform_set_drvdata(pdev, bank);

	for (i = 0; i < QCOM_MUTEX_NUM_LOCKS; i++) {
		field.reg = base + i * stride;
		field.lsb = 0;
		field.msb = 31;

		bank->lock[i].priv = devm_regmap_field_alloc(&pdev->dev,
							     regmap, field);
	}

	pm_runtime_enable(&pdev->dev);

	ret = hwspin_lock_register(bank, &pdev->dev, &qcom_hwspinlock_ops,
				   0, QCOM_MUTEX_NUM_LOCKS);
	if (ret)
		pm_runtime_disable(&pdev->dev);

	return ret;
}

static int qcom_hwspinlock_remove(struct platform_device *pdev)
{
	struct hwspinlock_device *bank = platform_get_drvdata(pdev);
	int ret;

	ret = hwspin_lock_unregister(bank);
	if (ret) {
		dev_err(&pdev->dev, "%s failed: %d\n", __func__, ret);
		return ret;
	}

	pm_runtime_disable(&pdev->dev);

	return 0;
}

static struct platform_driver qcom_hwspinlock_driver = {
	.probe		= qcom_hwspinlock_probe,
	.remove		= qcom_hwspinlock_remove,
	.driver		= {
		.name	= "qcom_hwspinlock",
		.of_match_table = qcom_hwspinlock_of_match,
	},
};

static int __init qcom_hwspinlock_init(void)
{
	return platform_driver_register(&qcom_hwspinlock_driver);
}
/* board init code might need to reserve hwspinlocks for predefined purposes */
postcore_initcall(qcom_hwspinlock_init);

static void __exit qcom_hwspinlock_exit(void)
{
	platform_driver_unregister(&qcom_hwspinlock_driver);
}
module_exit(qcom_hwspinlock_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Hardware spinlock driver for Qualcomm SoCs");
