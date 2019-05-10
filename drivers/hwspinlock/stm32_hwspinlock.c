// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2018
 * Author: Benjamin Gaignard <benjamin.gaignard@st.com> for STMicroelectronics.
 */

#include <linux/clk.h>
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

static const struct hwspinlock_ops stm32_hwspinlock_ops = {
	.trylock	= stm32_hwspinlock_trylock,
	.unlock		= stm32_hwspinlock_unlock,
};

static int stm32_hwspinlock_probe(struct platform_device *pdev)
{
	struct stm32_hwspinlock *hw;
	void __iomem *io_base;
	struct resource *res;
	size_t array_size;
	int i, ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	io_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(io_base))
		return PTR_ERR(io_base);

	array_size = STM32_MUTEX_NUM_LOCKS * sizeof(struct hwspinlock);
	hw = devm_kzalloc(&pdev->dev, sizeof(*hw) + array_size, GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	hw->clk = devm_clk_get(&pdev->dev, "hsem");
	if (IS_ERR(hw->clk))
		return PTR_ERR(hw->clk);

	for (i = 0; i < STM32_MUTEX_NUM_LOCKS; i++)
		hw->bank.lock[i].priv = io_base + i * sizeof(u32);

	platform_set_drvdata(pdev, hw);
	pm_runtime_enable(&pdev->dev);

	ret = hwspin_lock_register(&hw->bank, &pdev->dev, &stm32_hwspinlock_ops,
				   0, STM32_MUTEX_NUM_LOCKS);

	if (ret)
		pm_runtime_disable(&pdev->dev);

	return ret;
}

static int stm32_hwspinlock_remove(struct platform_device *pdev)
{
	struct stm32_hwspinlock *hw = platform_get_drvdata(pdev);
	int ret;

	ret = hwspin_lock_unregister(&hw->bank);
	if (ret)
		dev_err(&pdev->dev, "%s failed: %d\n", __func__, ret);

	pm_runtime_disable(&pdev->dev);

	return 0;
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
	.remove		= stm32_hwspinlock_remove,
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
