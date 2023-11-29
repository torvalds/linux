// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (C) 2019 ASPEED Technology Inc. */
/* Copyright (C) 2019 IBM Corp. */

#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/math64.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include "ufshcd-pltfrm.h"

#define UFS_MPHY_RST_REG		0x0
#define UFS_MPHY_RST_N			BIT(0)
#define UFS_MPHY_RST_N_PCS		BIT(4)

#define ASPEED_UFS_REG_HCLKDIV		0xFC

struct aspeed_ufscnr {
	struct clk *clk;
	struct resource *res;
	struct reset_control *rst;
	struct device *dev;

	void __iomem *regs;
};

struct aspeed_ufshc {
	/**
	 * cdns_ufs_dme_attr_val - for storing L4 attributes
	 */
	//u32 cdns_ufs_dme_attr_val[CDNS_UFS_MAX_L4_ATTRS];
	u32 temp;
};

static int aspeed_ufscnr_probe(struct platform_device *pdev)
{
	struct device_node *parent, *child;
	struct aspeed_ufscnr *cnr;
	u32 reg;
	int ret;

	cnr = devm_kzalloc(&pdev->dev, sizeof(struct aspeed_ufscnr), GFP_KERNEL);
	if (!cnr)
		return -ENOMEM;

	cnr->dev = &pdev->dev;

	cnr->rst = devm_reset_control_get(cnr->dev, NULL);
	if (IS_ERR(cnr->rst)) {
		dev_err(&pdev->dev, "Unable to get reset\n");
		return PTR_ERR(cnr->rst);
	}

	ret = reset_control_assert(cnr->rst);
	mdelay(1);
	ret = reset_control_deassert(cnr->rst);
	mdelay(1);

	cnr->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(cnr->clk)) {
		dev_err(&pdev->dev, "Unable to get clock\n");
		return PTR_ERR(cnr->clk);
	}

	ret = clk_prepare_enable(cnr->clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable UFS CLK\n");
		return ret;
	}

	cnr->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cnr->regs = devm_ioremap_resource(&pdev->dev, cnr->res);
	if (IS_ERR(cnr->regs)) {
		ret = PTR_ERR(cnr->regs);
		goto err_clk;
	}

	/* mphy reset deassert */
	reg = readl(cnr->regs + UFS_MPHY_RST_REG);
	reg &= ~(UFS_MPHY_RST_N | UFS_MPHY_RST_N_PCS);

	writel(reg, cnr->regs + UFS_MPHY_RST_REG);
	mdelay(1);
	writel(reg | UFS_MPHY_RST_N, cnr->regs + UFS_MPHY_RST_REG);
	mdelay(1);
	writel(reg | UFS_MPHY_RST_N | UFS_MPHY_RST_N_PCS, cnr->regs + UFS_MPHY_RST_REG);

	dev_set_drvdata(&pdev->dev, cnr);

	parent = pdev->dev.of_node;
	for_each_available_child_of_node(parent, child) {
		struct platform_device *cpdev;

		cpdev = of_platform_device_create(child, NULL, &pdev->dev);
		if (!cpdev) {
			of_node_put(child);
			ret = -ENODEV;
			goto err_clk;
		}
	}

	return 0;

err_clk:
	clk_disable_unprepare(cnr->clk);
	return ret;
}

/**
 * aspeed_ufscnr_remove - removes the ufscnr driver
 * @pdev: pointer to platform device handle
 *
 * Always returns 0
 */
static int aspeed_ufscnr_remove(struct platform_device *pdev)
{
	struct aspeed_ufscnr *cnr = dev_get_drvdata(&pdev->dev);

	clk_disable_unprepare(cnr->clk);

	return 0;
}

/**
 * aspeed_ufshc_init - performs additional ufs initialization
 * @hba: host controller instance
 *
 * Returns status of initialization
 */
static int aspeed_ufshc_init(struct ufs_hba *hba)
{
	int status = 0;
	struct aspeed_ufshc *ufshc;
	struct device *dev = hba->dev;

	ufshc = devm_kzalloc(dev, sizeof(*ufshc), GFP_KERNEL);

	if (!ufshc)
		return -ENOMEM;

	ufshcd_set_variant(hba, ufshc);

	status = ufshcd_vops_phy_initialization(hba);

	return status;
}

/**
 * aspeed_ufshc_link_startup_notify()
 * Called before and after Link startup is carried out.
 * @hba: host controller instance
 * @status: notify stage (pre, post change)
 *
 * Return zero for success and non-zero for failure
 */
static int aspeed_ufshc_link_startup_notify(struct ufs_hba *hba,
					    enum ufs_notify_change_status status)
{
	if (status != PRE_CHANGE)
		return 0;

	/*
	 * Some UFS devices have issues if LCC is enabled.
	 * So we are setting PA_Local_TX_LCC_Enable to 0
	 * before link startup which will make sure that both host
	 * and device TX LCC are disabled once link startup is
	 * completed.
	 */
	ufshcd_disable_host_tx_lcc(hba);

	/*
	 * Disabling Autohibern8 feature.
	 */
	hba->ahit = 0;

	return 0;
}

static const struct ufs_hba_variant_ops aspeed_ufshc_hba_vops = {
	.name = "aspeed-ufshc",
	.init = aspeed_ufshc_init,
	.link_startup_notify = aspeed_ufshc_link_startup_notify,
};

static const struct of_device_id aspeed_ufshc_of_match[] = {
	{
		.compatible = "aspeed,ast2700-ufshc",
		.data =  &aspeed_ufshc_hba_vops,
	},
	{ },
};

MODULE_DEVICE_TABLE(of, aspeed_ufshc_of_match);

/**
 * aspeed_ufshc_probe - probe routine of the driver
 * @pdev: pointer to platform device handle
 *
 * Return zero for success and non-zero for failure
 */
static int aspeed_ufshc_probe(struct platform_device *pdev)
{
	int err;
	const struct of_device_id *of_id;
	struct ufs_hba_variant_ops *vops;
	struct device *dev = &pdev->dev;

	of_id = of_match_node(aspeed_ufshc_of_match, dev->of_node);
	vops = (struct ufs_hba_variant_ops *)of_id->data;

	/* Perform generic probe */
	err = ufshcd_pltfrm_init(pdev, vops);
	if (err)
		dev_err(dev, "ufshcd_pltfrm_init() failed %d\n", err);

	return err;
}

/**
 * aspeed_ufshc_remove - removes the ufs driver
 * @pdev: pointer to platform device handle
 *
 * Always returns 0
 */
static int aspeed_ufshc_remove(struct platform_device *pdev)
{
	struct ufs_hba *hba =  platform_get_drvdata(pdev);

	ufshcd_remove(hba);
	return 0;
}

static const struct dev_pm_ops aspeed_ufshc_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ufshcd_system_suspend, ufshcd_system_resume)
	SET_RUNTIME_PM_OPS(ufshcd_runtime_suspend, ufshcd_runtime_resume, NULL)
	.prepare	 = ufshcd_suspend_prepare,
	.complete	 = ufshcd_resume_complete,
};

static struct platform_driver aspeed_ufshc_driver = {
	.probe	= aspeed_ufshc_probe,
	.remove	= aspeed_ufshc_remove,
	.driver	= {
		.name	= "aspeed-ufshcd",
		.pm	= &aspeed_ufshc_dev_pm_ops,
		.of_match_table = aspeed_ufshc_of_match,
	},
};

static const struct of_device_id aspeed_ufscnr_of_match[] = {
	{ .compatible = "aspeed,ast2700-ufscnr", },
	{ }
};

MODULE_DEVICE_TABLE(of, aspeed_ufscnr_of_match);

static struct platform_driver aspeed_ufscnr_driver = {
	.driver		= {
		.name	= "aspeed-ufscnr",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = aspeed_ufscnr_of_match,
	},
	.probe		= aspeed_ufscnr_probe,
	.remove		= aspeed_ufscnr_remove,
};

static int __init aspeed_ufs_init(void)
{
	int rc;

	rc = platform_driver_register(&aspeed_ufscnr_driver);
	if (rc < 0)
		return rc;

	rc = platform_driver_register(&aspeed_ufshc_driver);
	if (rc < 0)
		platform_driver_unregister(&aspeed_ufscnr_driver);

	return rc;
}
module_init(aspeed_ufs_init);

static void __exit aspeed_ufs_exit(void)
{
	platform_driver_unregister(&aspeed_ufscnr_driver);
	platform_driver_unregister(&aspeed_ufshc_driver);
}
module_exit(aspeed_ufs_exit);

MODULE_AUTHOR("Cool Lee <cool_lee@aspeedtech.com>");
MODULE_DESCRIPTION("Aspeed UFS host controller platform driver");
MODULE_LICENSE("GPL v2");
