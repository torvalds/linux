// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 * Copyright (c) 2015, Sony Mobile Communications AB
 */

#include <linux/hwspinlock.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "hwspinlock_internal.h"

#define QCOM_MUTEX_APPS_PROC_ID	1
#define QCOM_MUTEX_NUM_LOCKS	32

struct qcom_hwspinlock_of_data {
	u32 offset;
	u32 stride;
	const struct regmap_config *regmap_config;
};

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

static const struct regmap_config sfpb_mutex_config = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= 0x100,
	.fast_io		= true,
};

static const struct qcom_hwspinlock_of_data of_sfpb_mutex = {
	.offset = 0x4,
	.stride = 0x4,
	.regmap_config = &sfpb_mutex_config,
};

static const struct regmap_config tcsr_msm8226_mutex_config = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= 0x1000,
	.fast_io		= true,
};

static const struct qcom_hwspinlock_of_data of_msm8226_tcsr_mutex = {
	.offset = 0,
	.stride = 0x80,
	.regmap_config = &tcsr_msm8226_mutex_config,
};

static const struct regmap_config tcsr_mutex_config = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= 0x20000,
	.fast_io		= true,
};

static const struct qcom_hwspinlock_of_data of_tcsr_mutex = {
	.offset = 0,
	.stride = 0x1000,
	.regmap_config = &tcsr_mutex_config,
};

static const struct of_device_id qcom_hwspinlock_of_match[] = {
	{ .compatible = "qcom,sfpb-mutex", .data = &of_sfpb_mutex },
	{ .compatible = "qcom,tcsr-mutex", .data = &of_tcsr_mutex },
	{ .compatible = "qcom,apq8084-tcsr-mutex", .data = &of_msm8226_tcsr_mutex },
	{ .compatible = "qcom,ipq6018-tcsr-mutex", .data = &of_msm8226_tcsr_mutex },
	{ .compatible = "qcom,msm8226-tcsr-mutex", .data = &of_msm8226_tcsr_mutex },
	{ .compatible = "qcom,msm8974-tcsr-mutex", .data = &of_msm8226_tcsr_mutex },
	{ .compatible = "qcom,msm8994-tcsr-mutex", .data = &of_msm8226_tcsr_mutex },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_hwspinlock_of_match);

static struct regmap *qcom_hwspinlock_probe_syscon(struct platform_device *pdev,
						   u32 *base, u32 *stride)
{
	struct device_node *syscon;
	struct regmap *regmap;
	int ret;

	syscon = of_parse_phandle(pdev->dev.of_node, "syscon", 0);
	if (!syscon)
		return ERR_PTR(-ENODEV);

	regmap = syscon_node_to_regmap(syscon);
	of_node_put(syscon);
	if (IS_ERR(regmap))
		return regmap;

	ret = of_property_read_u32_index(pdev->dev.of_node, "syscon", 1, base);
	if (ret < 0) {
		dev_err(&pdev->dev, "no offset in syscon\n");
		return ERR_PTR(-EINVAL);
	}

	ret = of_property_read_u32_index(pdev->dev.of_node, "syscon", 2, stride);
	if (ret < 0) {
		dev_err(&pdev->dev, "no stride syscon\n");
		return ERR_PTR(-EINVAL);
	}

	return regmap;
}

static struct regmap *qcom_hwspinlock_probe_mmio(struct platform_device *pdev,
						 u32 *offset, u32 *stride)
{
	const struct qcom_hwspinlock_of_data *data;
	struct device *dev = &pdev->dev;
	void __iomem *base;

	data = of_device_get_match_data(dev);
	if (!data->regmap_config)
		return ERR_PTR(-EINVAL);

	*offset = data->offset;
	*stride = data->stride;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return ERR_CAST(base);

	return devm_regmap_init_mmio(dev, base, data->regmap_config);
}

static int qcom_hwspinlock_probe(struct platform_device *pdev)
{
	struct hwspinlock_device *bank;
	struct reg_field field;
	struct regmap *regmap;
	size_t array_size;
	u32 stride;
	u32 base;
	int i;

	regmap = qcom_hwspinlock_probe_syscon(pdev, &base, &stride);
	if (IS_ERR(regmap) && PTR_ERR(regmap) == -ENODEV)
		regmap = qcom_hwspinlock_probe_mmio(pdev, &base, &stride);

	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

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

	return devm_hwspin_lock_register(&pdev->dev, bank, &qcom_hwspinlock_ops,
					 0, QCOM_MUTEX_NUM_LOCKS);
}

static struct platform_driver qcom_hwspinlock_driver = {
	.probe		= qcom_hwspinlock_probe,
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
