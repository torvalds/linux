/*
 * Copyright (C) 2016 MediaTek Inc.
 *
 * Author: Chunfeng Yun <chunfeng.yun@mediatek.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

#include "mtu3.h"

/* u2-port0 should be powered on and enabled; */
int ssusb_check_clocks(struct mtu3 *mtu, u32 ex_clks)
{
	void __iomem *ibase = mtu->ippc_base;
	u32 value, check_val;
	int ret;

	check_val = ex_clks | SSUSB_SYS125_RST_B_STS | SSUSB_SYSPLL_STABLE |
			SSUSB_REF_RST_B_STS;

	ret = readl_poll_timeout(ibase + U3D_SSUSB_IP_PW_STS1, value,
			(check_val == (value & check_val)), 100, 20000);
	if (ret) {
		dev_err(mtu->dev, "clks of sts1 are not stable!\n");
		return ret;
	}

	ret = readl_poll_timeout(ibase + U3D_SSUSB_IP_PW_STS2, value,
			(value & SSUSB_U2_MAC_SYS_RST_B_STS), 100, 10000);
	if (ret) {
		dev_err(mtu->dev, "mac2 clock is not stable\n");
		return ret;
	}

	return 0;
}

static int ssusb_rscs_init(struct mtu3 *mtu)
{
	int ret = 0;

	ret = regulator_enable(mtu->vusb33);
	if (ret) {
		dev_err(mtu->dev, "failed to enable vusb33\n");
		goto vusb33_err;
	}

	ret = clk_prepare_enable(mtu->sys_clk);
	if (ret) {
		dev_err(mtu->dev, "failed to enable sys_clk\n");
		goto clk_err;
	}

	ret = phy_init(mtu->phy);
	if (ret) {
		dev_err(mtu->dev, "failed to init phy\n");
		goto phy_init_err;
	}

	ret = phy_power_on(mtu->phy);
	if (ret) {
		dev_err(mtu->dev, "failed to power on phy\n");
		goto phy_err;
	}

	return 0;

phy_err:
	phy_exit(mtu->phy);

phy_init_err:
	clk_disable_unprepare(mtu->sys_clk);

clk_err:
	regulator_disable(mtu->vusb33);

vusb33_err:

	return ret;
}

static void ssusb_rscs_exit(struct mtu3 *mtu)
{
	clk_disable_unprepare(mtu->sys_clk);
	regulator_disable(mtu->vusb33);
	phy_power_off(mtu->phy);
	phy_exit(mtu->phy);
}

static void ssusb_ip_sw_reset(struct mtu3 *mtu)
{
	mtu3_setbits(mtu->ippc_base, U3D_SSUSB_IP_PW_CTRL0, SSUSB_IP_SW_RST);
	udelay(1);
	mtu3_clrbits(mtu->ippc_base, U3D_SSUSB_IP_PW_CTRL0, SSUSB_IP_SW_RST);
}

static int get_ssusb_rscs(struct platform_device *pdev, struct mtu3 *mtu)
{
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct resource *res;

	mtu->phy = devm_of_phy_get_by_index(dev, node, 0);
	if (IS_ERR(mtu->phy)) {
		dev_err(dev, "failed to get phy\n");
		return PTR_ERR(mtu->phy);
	}

	mtu->irq = platform_get_irq(pdev, 0);
	if (mtu->irq <= 0) {
		dev_err(dev, "fail to get irq number\n");
		return -ENODEV;
	}
	dev_info(dev, "irq %d\n", mtu->irq);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mac");
	mtu->mac_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(mtu->mac_base)) {
		dev_err(dev, "error mapping memory for dev mac\n");
		return PTR_ERR(mtu->mac_base);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ippc");
	mtu->ippc_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(mtu->ippc_base)) {
		dev_err(dev, "failed to map memory for ippc\n");
		return PTR_ERR(mtu->ippc_base);
	}

	mtu->vusb33 = devm_regulator_get(&pdev->dev, "vusb33");
	if (IS_ERR(mtu->vusb33)) {
		dev_err(dev, "failed to get vusb33\n");
		return PTR_ERR(mtu->vusb33);
	}

	mtu->sys_clk = devm_clk_get(dev, "sys_ck");
	if (IS_ERR(mtu->sys_clk)) {
		dev_err(dev, "failed to get sys clock\n");
		return PTR_ERR(mtu->sys_clk);
	}

	return 0;
}

static int mtu3_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtu3 *mtu;
	int ret = -ENOMEM;

	/* all elements are set to ZERO as default value */
	mtu = devm_kzalloc(dev, sizeof(struct mtu3), GFP_KERNEL);
	if (!mtu)
		return -ENOMEM;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(dev, "No suitable DMA config available\n");
		return -ENOTSUPP;
	}

	platform_set_drvdata(pdev, mtu);
	mtu->dev = dev;
	spin_lock_init(&mtu->lock);

	ret = get_ssusb_rscs(pdev, mtu);
	if (ret)
		return ret;

	/* enable power domain */
	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);
	device_enable_async_suspend(dev);

	ret = ssusb_rscs_init(mtu);
	if (ret)
		goto comm_init_err;

	ssusb_ip_sw_reset(mtu);

	ret = ssusb_gadget_init(mtu);
	if (ret) {
		dev_err(dev, "failed to initialize gadget\n");
		goto comm_exit;
	}

	return 0;

comm_exit:
	ssusb_rscs_exit(mtu);

comm_init_err:
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	return ret;
}

static int mtu3_remove(struct platform_device *pdev)
{
	struct mtu3 *mtu = platform_get_drvdata(pdev);

	ssusb_gadget_exit(mtu);
	ssusb_rscs_exit(mtu);
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

#ifdef CONFIG_OF

static const struct of_device_id mtu3_of_match[] = {
	{.compatible = "mediatek,mt8173-mtu3",},
	{},
};

MODULE_DEVICE_TABLE(of, mtu3_of_match);

#endif

static struct platform_driver mtu3_driver = {
	.probe = mtu3_probe,
	.remove = mtu3_remove,
	.driver = {
		.name = MTU3_DRIVER_NAME,
		.of_match_table = of_match_ptr(mtu3_of_match),
	},
};
module_platform_driver(mtu3_driver);

MODULE_AUTHOR("Chunfeng Yun <chunfeng.yun@mediatek.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MediaTek USB3 DRD Controller Driver");
