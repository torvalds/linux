// SPDX-License-Identifier: GPL-2.0+
/*
 * Based on Synopsys DesignWare I2C adapter driver.
 *
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/soc/amd/isp4_misc.h>

#include "i2c-designware-core.h"

#define DRV_NAME		"amd_isp_i2c_designware"
#define AMD_ISP_I2C_INPUT_CLK	100 /* Mhz */

static void amd_isp_dw_i2c_plat_pm_cleanup(struct dw_i2c_dev *i2c_dev)
{
	pm_runtime_disable(i2c_dev->dev);

	if (i2c_dev->shared_with_punit)
		pm_runtime_put_noidle(i2c_dev->dev);
}

static inline u32 amd_isp_dw_i2c_get_clk_rate(struct dw_i2c_dev *i2c_dev)
{
	return AMD_ISP_I2C_INPUT_CLK * 1000;
}

static int amd_isp_dw_i2c_plat_probe(struct platform_device *pdev)
{
	struct dw_i2c_dev *isp_i2c_dev;
	struct i2c_adapter *adap;
	int ret;

	isp_i2c_dev = devm_kzalloc(&pdev->dev, sizeof(*isp_i2c_dev), GFP_KERNEL);
	if (!isp_i2c_dev)
		return -ENOMEM;
	isp_i2c_dev->dev = &pdev->dev;

	pdev->dev.init_name = DRV_NAME;

	/*
	 * Use the polling mode to send/receive the data, because
	 * no IRQ connection from ISP I2C
	 */
	isp_i2c_dev->flags |= ACCESS_POLLING;
	platform_set_drvdata(pdev, isp_i2c_dev);

	isp_i2c_dev->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(isp_i2c_dev->base))
		return dev_err_probe(&pdev->dev, PTR_ERR(isp_i2c_dev->base),
				     "failed to get IOMEM resource\n");

	isp_i2c_dev->get_clk_rate_khz = amd_isp_dw_i2c_get_clk_rate;
	ret = i2c_dw_fw_parse_and_configure(isp_i2c_dev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to parse i2c dw fwnode and configure\n");

	i2c_dw_configure(isp_i2c_dev);

	adap = &isp_i2c_dev->adapter;
	adap->owner = THIS_MODULE;
	scnprintf(adap->name, sizeof(adap->name), AMDISP_I2C_ADAP_NAME);
	ACPI_COMPANION_SET(&adap->dev, ACPI_COMPANION(&pdev->dev));
	adap->dev.of_node = pdev->dev.of_node;
	/* use dynamically allocated adapter id */
	adap->nr = -1;

	if (isp_i2c_dev->flags & ACCESS_NO_IRQ_SUSPEND)
		dev_pm_set_driver_flags(&pdev->dev,
					DPM_FLAG_SMART_PREPARE);
	else
		dev_pm_set_driver_flags(&pdev->dev,
					DPM_FLAG_SMART_PREPARE |
					DPM_FLAG_SMART_SUSPEND);

	device_enable_async_suspend(&pdev->dev);

	if (isp_i2c_dev->shared_with_punit)
		pm_runtime_get_noresume(&pdev->dev);

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	ret = i2c_dw_probe(isp_i2c_dev);
	if (ret) {
		dev_err_probe(&pdev->dev, ret, "i2c_dw_probe failed\n");
		goto error_release_rpm;
	}

	pm_runtime_put_sync(&pdev->dev);

	return 0;

error_release_rpm:
	amd_isp_dw_i2c_plat_pm_cleanup(isp_i2c_dev);
	pm_runtime_put_sync(&pdev->dev);
	return ret;
}

static void amd_isp_dw_i2c_plat_remove(struct platform_device *pdev)
{
	struct dw_i2c_dev *isp_i2c_dev = platform_get_drvdata(pdev);

	pm_runtime_get_sync(&pdev->dev);

	i2c_del_adapter(&isp_i2c_dev->adapter);

	i2c_dw_disable(isp_i2c_dev);

	pm_runtime_put_sync(&pdev->dev);
	amd_isp_dw_i2c_plat_pm_cleanup(isp_i2c_dev);
}

static int amd_isp_dw_i2c_plat_prepare(struct device *dev)
{
	/*
	 * If the ACPI companion device object is present for this device, it
	 * may be accessed during suspend and resume of other devices via I2C
	 * operation regions, so tell the PM core and middle layers to avoid
	 * skipping system suspend/resume callbacks for it in that case.
	 */
	return !has_acpi_companion(dev);
}

static int amd_isp_dw_i2c_plat_runtime_suspend(struct device *dev)
{
	struct dw_i2c_dev *i_dev = dev_get_drvdata(dev);

	if (i_dev->shared_with_punit)
		return 0;

	i2c_dw_disable(i_dev);
	i2c_dw_prepare_clk(i_dev, false);

	return 0;
}

static int amd_isp_dw_i2c_plat_suspend(struct device *dev)
{
	struct dw_i2c_dev *i_dev = dev_get_drvdata(dev);
	int ret;

	if (!i_dev)
		return -ENODEV;

	ret = amd_isp_dw_i2c_plat_runtime_suspend(dev);
	if (!ret)
		i2c_mark_adapter_suspended(&i_dev->adapter);

	return ret;
}

static int amd_isp_dw_i2c_plat_runtime_resume(struct device *dev)
{
	struct dw_i2c_dev *i_dev = dev_get_drvdata(dev);

	if (!i_dev)
		return -ENODEV;

	if (!i_dev->shared_with_punit)
		i2c_dw_prepare_clk(i_dev, true);
	if (i_dev->init)
		i_dev->init(i_dev);

	return 0;
}

static int amd_isp_dw_i2c_plat_resume(struct device *dev)
{
	struct dw_i2c_dev *i_dev = dev_get_drvdata(dev);

	amd_isp_dw_i2c_plat_runtime_resume(dev);
	i2c_mark_adapter_resumed(&i_dev->adapter);

	return 0;
}

static const struct dev_pm_ops amd_isp_dw_i2c_dev_pm_ops = {
	.prepare = pm_sleep_ptr(amd_isp_dw_i2c_plat_prepare),
	LATE_SYSTEM_SLEEP_PM_OPS(amd_isp_dw_i2c_plat_suspend, amd_isp_dw_i2c_plat_resume)
	RUNTIME_PM_OPS(amd_isp_dw_i2c_plat_runtime_suspend, amd_isp_dw_i2c_plat_runtime_resume, NULL)
};

/* Work with hotplug and coldplug */
MODULE_ALIAS("platform:amd_isp_i2c_designware");

static struct platform_driver amd_isp_dw_i2c_driver = {
	.probe = amd_isp_dw_i2c_plat_probe,
	.remove = amd_isp_dw_i2c_plat_remove,
	.driver		= {
		.name	= DRV_NAME,
		.pm	= pm_ptr(&amd_isp_dw_i2c_dev_pm_ops),
	},
};
module_platform_driver(amd_isp_dw_i2c_driver);

MODULE_DESCRIPTION("Synopsys DesignWare I2C bus adapter in AMD ISP");
MODULE_IMPORT_NS("I2C_DW");
MODULE_IMPORT_NS("I2C_DW_COMMON");
MODULE_AUTHOR("Venkata Narendra Kumar Gutta <vengutta@amd.com>");
MODULE_AUTHOR("Pratap Nirujogi <pratap.nirujogi@amd.com>");
MODULE_AUTHOR("Bin Du <bin.du@amd.com>");
MODULE_LICENSE("GPL");
