/*
 * Samsung EXYNOS4x12 FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *
 * Author: Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include "fimc-is-i2c.h"

struct fimc_is_i2c {
	struct i2c_adapter adapter;
	struct clk *clock;
};

/*
 * An empty algorithm is used as the actual I2C bus controller driver
 * is implemented in the FIMC-IS subsystem firmware and the host CPU
 * doesn't access the I2C bus controller.
 */
static const struct i2c_algorithm fimc_is_i2c_algorithm;

static int fimc_is_i2c_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct fimc_is_i2c *isp_i2c;
	struct i2c_adapter *i2c_adap;
	int ret;

	isp_i2c = devm_kzalloc(&pdev->dev, sizeof(*isp_i2c), GFP_KERNEL);
	if (!isp_i2c)
		return -ENOMEM;

	isp_i2c->clock = devm_clk_get(&pdev->dev, "i2c_isp");
	if (IS_ERR(isp_i2c->clock)) {
		dev_err(&pdev->dev, "failed to get the clock\n");
		return PTR_ERR(isp_i2c->clock);
	}

	i2c_adap = &isp_i2c->adapter;
	i2c_adap->dev.of_node = node;
	i2c_adap->dev.parent = &pdev->dev;
	strlcpy(i2c_adap->name, "exynos4x12-isp-i2c", sizeof(i2c_adap->name));
	i2c_adap->owner = THIS_MODULE;
	i2c_adap->algo = &fimc_is_i2c_algorithm;
	i2c_adap->class = I2C_CLASS_SPD;

	platform_set_drvdata(pdev, isp_i2c);
	pm_runtime_enable(&pdev->dev);

	ret = i2c_add_adapter(i2c_adap);
	if (ret < 0)
		goto err_pm_dis;
	/*
	 * Client drivers of this adapter don't do any I2C transfers as that
	 * is handled by the ISP firmware.  But we rely on the runtime PM
	 * state propagation from the clients up to the adapter driver so
	 * clear the ignore_children flags here.  PM rutnime calls are not
	 * used in probe() handler of clients of this adapter so there is
	 * no issues with clearing the flag right after registering the I2C
	 * adapter.
	 */
	pm_suspend_ignore_children(&i2c_adap->dev, false);
	return 0;

err_pm_dis:
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static int fimc_is_i2c_remove(struct platform_device *pdev)
{
	struct fimc_is_i2c *isp_i2c = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	i2c_del_adapter(&isp_i2c->adapter);

	return 0;
}

#ifdef CONFIG_PM
static int fimc_is_i2c_runtime_suspend(struct device *dev)
{
	struct fimc_is_i2c *isp_i2c = dev_get_drvdata(dev);

	clk_disable_unprepare(isp_i2c->clock);
	return 0;
}

static int fimc_is_i2c_runtime_resume(struct device *dev)
{
	struct fimc_is_i2c *isp_i2c = dev_get_drvdata(dev);

	return clk_prepare_enable(isp_i2c->clock);
}
#endif

#ifdef CONFIG_PM_SLEEP
static int fimc_is_i2c_suspend(struct device *dev)
{
	if (pm_runtime_suspended(dev))
		return 0;

	return fimc_is_i2c_runtime_suspend(dev);
}

static int fimc_is_i2c_resume(struct device *dev)
{
	if (pm_runtime_suspended(dev))
		return 0;

	return fimc_is_i2c_runtime_resume(dev);
}
#endif

static struct dev_pm_ops fimc_is_i2c_pm_ops = {
	SET_RUNTIME_PM_OPS(fimc_is_i2c_runtime_suspend,
					fimc_is_i2c_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(fimc_is_i2c_suspend, fimc_is_i2c_resume)
};

static const struct of_device_id fimc_is_i2c_of_match[] = {
	{ .compatible = FIMC_IS_I2C_COMPATIBLE },
	{ },
};

static struct platform_driver fimc_is_i2c_driver = {
	.probe		= fimc_is_i2c_probe,
	.remove		= fimc_is_i2c_remove,
	.driver = {
		.of_match_table = fimc_is_i2c_of_match,
		.name		= "fimc-isp-i2c",
		.pm		= &fimc_is_i2c_pm_ops,
	}
};

int fimc_is_register_i2c_driver(void)
{
	return platform_driver_register(&fimc_is_i2c_driver);
}

void fimc_is_unregister_i2c_driver(void)
{
	platform_driver_unregister(&fimc_is_i2c_driver);
}
