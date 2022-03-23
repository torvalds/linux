// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2018
 * Author: Benjamin Gaignard <benjamin.gaignard@st.com> for STMicroelectronics.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/hwspinlock.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "hwspinlock_internal.h"

#define STM32_MUTEX_COREID	BIT(8)
#define STM32_MUTEX_LOCK_BIT	BIT(31)
#define STM32_MUTEX_NUM_LOCKS	32

struct stm32_hwspinlock {
	struct clk *clk;
	struct hwspinlock_device bank;
};

static int stm32_hwspinlock_trylock(struct hwspinlock *lock)
{
	void __iomem *lock_addr = lock->priv;
	u32 status;

	writel(STM32_MUTEX_LOCK_BIT | STM32_MUTEX_COREID, lock_addr);
	status = readl(lock_addr);

	return status == (STM32_MUTEX_LOCK_BIT | STM32_MUTEX_COREID);
}

static void stm32_hwspinlock_unlock(struct hwspinlock *lock)
{
	void __iomem *lock_addr = lock->priv;

	writel(STM32_MUTEX_COREID, lock_addr);
}

static void stm32_hwspinlock_relax(struct hwspinlock *lock)
{
	ndelay(50);
}

static const struct hwspinlock_ops stm32_hwspinlock_ops = {
	.trylock	= stm32_hwspinlock_trylock,
	.unlock		= stm32_hwspinlock_unlock,
	.relax		= stm32_hwspinlock_relax,
};

static void stm32_hwspinlock_disable_clk(void *data)
{
	struct platform_device *pdev = data;
	struct stm32_hwspinlock *hw = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	pm_runtime_get_sync(dev);
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_put_noidle(dev);

	clk_disable_unprepare(hw->clk);
}

static int stm32_hwspinlock_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct stm32_hwspinlock *hw;
	void __iomem *io_base;
	size_t array_size;
	int i, ret;

	io_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(io_base))
		return PTR_ERR(io_base);

	array_size = STM32_MUTEX_NUM_LOCKS * sizeof(struct hwspinlock);
	hw = devm_kzalloc(dev, sizeof(*hw) + array_size, GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	hw->clk = devm_clk_get(dev, "hsem");
	if (IS_ERR(hw->clk))
		return PTR_ERR(hw->clk);

	ret = clk_prepare_enable(hw->clk);
	if (ret) {
		dev_err(dev, "Failed to prepare_enable clock\n");
		return ret;
	}

	platform_set_drvdata(pdev, hw);

	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_put(dev);

	ret = devm_add_action_or_reset(dev, stm32_hwspinlock_disable_clk, pdev);
	if (ret) {
		dev_err(dev, "Failed to register action\n");
		return ret;
	}

	for (i = 0; i < STM32_MUTEX_NUM_LOCKS; i++)
		hw->bank.lock[i].priv = io_base + i * sizeof(u32);

	ret = devm_hwspin_lock_register(dev, &hw->bank, &stm32_hwspinlock_ops,
					0, STM32_MUTEX_NUM_LOCKS);

	if (ret)
		dev_err(dev, "Failed to register hwspinlock\n");

	return ret;
}

static int __maybe_unused stm32_hwspinlock_runtime_suspend(struct device *dev)
{
	struct stm32_hwspinlock *hw = dev_get_drvdata(dev);

	clk_disable_unprepare(hw->clk);

	return 0;
}

static int __maybe_unused stm32_hwspinlock_runtime_resume(struct device *dev)
{
	struct stm32_hwspinlock *hw = dev_get_drvdata(dev);

	clk_prepare_enable(hw->clk);

	return 0;
}

static const struct dev_pm_ops stm32_hwspinlock_pm_ops = {
	SET_RUNTIME_PM_OPS(stm32_hwspinlock_runtime_suspend,
			   stm32_hwspinlock_runtime_resume,
			   NULL)
};

static const struct of_device_id stm32_hwpinlock_ids[] = {
	{ .compatible = "st,stm32-hwspinlock", },
	{},
};
MODULE_DEVICE_TABLE(of, stm32_hwpinlock_ids);

static struct platform_driver stm32_hwspinlock_driver = {
	.probe		= stm32_hwspinlock_probe,
	.driver		= {
		.name	= "stm32_hwspinlock",
		.of_match_table = stm32_hwpinlock_ids,
		.pm	= &stm32_hwspinlock_pm_ops,
	},
};

static int __init stm32_hwspinlock_init(void)
{
	return platform_driver_register(&stm32_hwspinlock_driver);
}
/* board init code might need to reserve hwspinlocks for predefined purposes */
postcore_initcall(stm32_hwspinlock_init);

static void __exit stm32_hwspinlock_exit(void)
{
	platform_driver_unregister(&stm32_hwspinlock_driver);
}
module_exit(stm32_hwspinlock_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Hardware spinlock driver for STM32 SoCs");
MODULE_AUTHOR("Benjamin Gaignard <benjamin.gaignard@st.com>");
