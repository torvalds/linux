/*
 * Universal Flash Storage Host controller Platform bus based glue driver
 *
 * This code is based on drivers/scsi/ufs/ufshcd-pltfrm.c
 * Copyright (C) 2011-2013 Samsung India Software Operations
 *
 * Authors:
 *	Santosh Yaraganavi <santosh.sy@samsung.com>
 *	Vinayak Holikatti <h.vinayak@samsung.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * See the COPYING file in the top-level directory or visit
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This program is provided "AS IS" and "WITH ALL FAULTS" and
 * without warranty of any kind. You are solely responsible for
 * determining the appropriateness of using and distributing
 * the program and assume all risks associated with your exercise
 * of rights with respect to the program, including but not limited
 * to infringement of third party rights, the risks and costs of
 * program errors, damage to or loss of data, programs or equipment,
 * and unavailability or interruption of operations. Under no
 * circumstances will the contributor of this Program be liable for
 * any damages of any kind arising from your use or distribution of
 * this program.
 */

#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>

#include "ufshcd.h"

static const struct of_device_id ufs_of_match[];
static struct ufs_hba_variant_ops *get_variant_ops(struct device *dev)
{
	if (dev->of_node) {
		const struct of_device_id *match;

		match = of_match_node(ufs_of_match, dev->of_node);
		if (match)
			return (struct ufs_hba_variant_ops *)match->data;
	}

	return NULL;
}

static int ufshcd_parse_clock_info(struct ufs_hba *hba)
{
	int ret = 0;
	int cnt;
	int i;
	struct device *dev = hba->dev;
	struct device_node *np = dev->of_node;
	char *name;
	u32 *clkfreq = NULL;
	struct ufs_clk_info *clki;

	if (!np)
		goto out;

	INIT_LIST_HEAD(&hba->clk_list_head);

	cnt = of_property_count_strings(np, "clock-names");
	if (!cnt || (cnt == -EINVAL)) {
		dev_info(dev, "%s: Unable to find clocks, assuming enabled\n",
				__func__);
	} else if (cnt < 0) {
		dev_err(dev, "%s: count clock strings failed, err %d\n",
				__func__, cnt);
		ret = cnt;
	}

	if (cnt <= 0)
		goto out;

	clkfreq = kzalloc(cnt * sizeof(*clkfreq), GFP_KERNEL);
	if (!clkfreq) {
		ret = -ENOMEM;
		dev_err(dev, "%s: memory alloc failed\n", __func__);
		goto out;
	}

	ret = of_property_read_u32_array(np,
			"max-clock-frequency-hz", clkfreq, cnt);
	if (ret && (ret != -EINVAL)) {
		dev_err(dev, "%s: invalid max-clock-frequency-hz property, %d\n",
				__func__, ret);
		goto out;
	}

	for (i = 0; i < cnt; i++) {
		ret = of_property_read_string_index(np,
				"clock-names", i, (const char **)&name);
		if (ret)
			goto out;

		clki = devm_kzalloc(dev, sizeof(*clki), GFP_KERNEL);
		if (!clki) {
			ret = -ENOMEM;
			goto out;
		}

		clki->max_freq = clkfreq[i];
		clki->name = kstrdup(name, GFP_KERNEL);
		list_add_tail(&clki->list, &hba->clk_list_head);
	}
out:
	kfree(clkfreq);
	return ret;
}

#define MAX_PROP_SIZE 32
static int ufshcd_populate_vreg(struct device *dev, const char *name,
		struct ufs_vreg **out_vreg)
{
	int ret = 0;
	char prop_name[MAX_PROP_SIZE];
	struct ufs_vreg *vreg = NULL;
	struct device_node *np = dev->of_node;

	if (!np) {
		dev_err(dev, "%s: non DT initialization\n", __func__);
		goto out;
	}

	snprintf(prop_name, MAX_PROP_SIZE, "%s-supply", name);
	if (!of_parse_phandle(np, prop_name, 0)) {
		dev_info(dev, "%s: Unable to find %s regulator, assuming enabled\n",
				__func__, prop_name);
		goto out;
	}

	vreg = devm_kzalloc(dev, sizeof(*vreg), GFP_KERNEL);
	if (!vreg) {
		dev_err(dev, "No memory for %s regulator\n", name);
		goto out;
	}

	vreg->name = kstrdup(name, GFP_KERNEL);

	snprintf(prop_name, MAX_PROP_SIZE, "%s-max-microamp", name);
	ret = of_property_read_u32(np, prop_name, &vreg->max_uA);
	if (ret) {
		dev_err(dev, "%s: unable to find %s err %d\n",
				__func__, prop_name, ret);
		goto out_free;
	}

	vreg->min_uA = 0;
	if (!strcmp(name, "vcc")) {
		if (of_property_read_bool(np, "vcc-supply-1p8")) {
			vreg->min_uV = UFS_VREG_VCC_1P8_MIN_UV;
			vreg->max_uV = UFS_VREG_VCC_1P8_MAX_UV;
		} else {
			vreg->min_uV = UFS_VREG_VCC_MIN_UV;
			vreg->max_uV = UFS_VREG_VCC_MAX_UV;
		}
	} else if (!strcmp(name, "vccq")) {
		vreg->min_uV = UFS_VREG_VCCQ_MIN_UV;
		vreg->max_uV = UFS_VREG_VCCQ_MAX_UV;
	} else if (!strcmp(name, "vccq2")) {
		vreg->min_uV = UFS_VREG_VCCQ2_MIN_UV;
		vreg->max_uV = UFS_VREG_VCCQ2_MAX_UV;
	}

	goto out;

out_free:
	devm_kfree(dev, vreg);
	vreg = NULL;
out:
	if (!ret)
		*out_vreg = vreg;
	return ret;
}

/**
 * ufshcd_parse_regulator_info - get regulator info from device tree
 * @hba: per adapter instance
 *
 * Get regulator info from device tree for vcc, vccq, vccq2 power supplies.
 * If any of the supplies are not defined it is assumed that they are always-on
 * and hence return zero. If the property is defined but parsing is failed
 * then return corresponding error.
 */
static int ufshcd_parse_regulator_info(struct ufs_hba *hba)
{
	int err;
	struct device *dev = hba->dev;
	struct ufs_vreg_info *info = &hba->vreg_info;

	err = ufshcd_populate_vreg(dev, "vcc", &info->vcc);
	if (err)
		goto out;

	err = ufshcd_populate_vreg(dev, "vccq", &info->vccq);
	if (err)
		goto out;

	err = ufshcd_populate_vreg(dev, "vccq2", &info->vccq2);
out:
	return err;
}

#ifdef CONFIG_PM
/**
 * ufshcd_pltfrm_suspend - suspend power management function
 * @dev: pointer to device handle
 *
 *
 * Returns 0
 */
static int ufshcd_pltfrm_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ufs_hba *hba =  platform_get_drvdata(pdev);

	/*
	 * TODO:
	 * 1. Call ufshcd_suspend
	 * 2. Do bus specific power management
	 */

	disable_irq(hba->irq);

	return 0;
}

/**
 * ufshcd_pltfrm_resume - resume power management function
 * @dev: pointer to device handle
 *
 * Returns 0
 */
static int ufshcd_pltfrm_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ufs_hba *hba =  platform_get_drvdata(pdev);

	/*
	 * TODO:
	 * 1. Call ufshcd_resume.
	 * 2. Do bus specific wake up
	 */

	enable_irq(hba->irq);

	return 0;
}
#else
#define ufshcd_pltfrm_suspend	NULL
#define ufshcd_pltfrm_resume	NULL
#endif

#ifdef CONFIG_PM_RUNTIME
static int ufshcd_pltfrm_runtime_suspend(struct device *dev)
{
	struct ufs_hba *hba =  dev_get_drvdata(dev);

	if (!hba)
		return 0;

	return ufshcd_runtime_suspend(hba);
}
static int ufshcd_pltfrm_runtime_resume(struct device *dev)
{
	struct ufs_hba *hba =  dev_get_drvdata(dev);

	if (!hba)
		return 0;

	return ufshcd_runtime_resume(hba);
}
static int ufshcd_pltfrm_runtime_idle(struct device *dev)
{
	struct ufs_hba *hba =  dev_get_drvdata(dev);

	if (!hba)
		return 0;

	return ufshcd_runtime_idle(hba);
}
#else /* !CONFIG_PM_RUNTIME */
#define ufshcd_pltfrm_runtime_suspend	NULL
#define ufshcd_pltfrm_runtime_resume	NULL
#define ufshcd_pltfrm_runtime_idle	NULL
#endif /* CONFIG_PM_RUNTIME */

/**
 * ufshcd_pltfrm_probe - probe routine of the driver
 * @pdev: pointer to Platform device handle
 *
 * Returns 0 on success, non-zero value on failure
 */
static int ufshcd_pltfrm_probe(struct platform_device *pdev)
{
	struct ufs_hba *hba;
	void __iomem *mmio_base;
	struct resource *mem_res;
	int irq, err;
	struct device *dev = &pdev->dev;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mmio_base = devm_ioremap_resource(dev, mem_res);
	if (IS_ERR(*(void **)&mmio_base)) {
		err = PTR_ERR(*(void **)&mmio_base);
		goto out;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "IRQ resource not available\n");
		err = -ENODEV;
		goto out;
	}

	err = ufshcd_alloc_host(dev, &hba);
	if (err) {
		dev_err(&pdev->dev, "Allocation failed\n");
		goto out;
	}

	hba->vops = get_variant_ops(&pdev->dev);

	err = ufshcd_parse_clock_info(hba);
	if (err) {
		dev_err(&pdev->dev, "%s: clock parse failed %d\n",
				__func__, err);
		goto out;
	}
	err = ufshcd_parse_regulator_info(hba);
	if (err) {
		dev_err(&pdev->dev, "%s: regulator init failed %d\n",
				__func__, err);
		goto out;
	}

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	err = ufshcd_init(hba, mmio_base, irq);
	if (err) {
		dev_err(dev, "Intialization failed\n");
		goto out_disable_rpm;
	}

	platform_set_drvdata(pdev, hba);

	return 0;

out_disable_rpm:
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
out:
	return err;
}

/**
 * ufshcd_pltfrm_remove - remove platform driver routine
 * @pdev: pointer to platform device handle
 *
 * Returns 0 on success, non-zero value on failure
 */
static int ufshcd_pltfrm_remove(struct platform_device *pdev)
{
	struct ufs_hba *hba =  platform_get_drvdata(pdev);

	pm_runtime_get_sync(&(pdev)->dev);
	ufshcd_remove(hba);
	return 0;
}

static const struct of_device_id ufs_of_match[] = {
	{ .compatible = "jedec,ufs-1.1"},
	{},
};

static const struct dev_pm_ops ufshcd_dev_pm_ops = {
	.suspend	= ufshcd_pltfrm_suspend,
	.resume		= ufshcd_pltfrm_resume,
	.runtime_suspend = ufshcd_pltfrm_runtime_suspend,
	.runtime_resume  = ufshcd_pltfrm_runtime_resume,
	.runtime_idle    = ufshcd_pltfrm_runtime_idle,
};

static struct platform_driver ufshcd_pltfrm_driver = {
	.probe	= ufshcd_pltfrm_probe,
	.remove	= ufshcd_pltfrm_remove,
	.driver	= {
		.name	= "ufshcd",
		.owner	= THIS_MODULE,
		.pm	= &ufshcd_dev_pm_ops,
		.of_match_table = ufs_of_match,
	},
};

module_platform_driver(ufshcd_pltfrm_driver);

MODULE_AUTHOR("Santosh Yaragnavi <santosh.sy@samsung.com>");
MODULE_AUTHOR("Vinayak Holikatti <h.vinayak@samsung.com>");
MODULE_DESCRIPTION("UFS host controller Pltform bus based glue driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(UFSHCD_DRIVER_VERSION);
