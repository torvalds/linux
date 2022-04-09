// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2018, 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/usb/phy.h>

/* QSCRATCH registers */
#define HS_PHY_CTRL_REG		0x10
#define SW_SESSVLD_SEL		BIT(28)

/* HSPHY registers */
#define HS2_LOCAL_RESET_REG_ADDR	0x04
#define HS2_CLK_STATUS_ADDR		0x10
#define HS2_CLK_STATUS_SEL_ADDR		0x14
#define HS2_USB30_CTRL_ADDR		0x34
#define HS2_USB30_PHY_POWER_OFF		BIT(25)

struct qcusb_emu_phy {
	struct usb_phy	phy;
	struct device	*dev;
	void __iomem	*base;
	void __iomem	*qscratch_base;
	int		*emu_init_seq;
	int		emu_init_seq_len;
};

static int qcusb_emu_phy_init(struct usb_phy *phy)
{
	struct qcusb_emu_phy *qphy = container_of(phy,
			struct qcusb_emu_phy, phy);
	u32 tmp;
	int i;

	/* reset everything */
	writel_relaxed(0xffffffff, qphy->base + HS2_LOCAL_RESET_REG_ADDR);
	usleep_range(10000, 12000);

	/* power down HS phy */
	tmp = readl_relaxed(qphy->base + HS2_USB30_CTRL_ADDR) |
				HS2_USB30_PHY_POWER_OFF;
	writel_relaxed(tmp, qphy->base + HS2_USB30_CTRL_ADDR);
	usleep_range(10000, 12000);

	/* power up HS phy */
	tmp = readl_relaxed(qphy->base + HS2_USB30_CTRL_ADDR) &
				(~HS2_USB30_PHY_POWER_OFF);
	writel_relaxed(tmp, qphy->base + HS2_USB30_CTRL_ADDR);
	usleep_range(10000, 12000);

	writel_relaxed(0xfffffff3, qphy->base + HS2_LOCAL_RESET_REG_ADDR);
	usleep_range(10000, 12000);

	/* put phy out of reset */
	writel_relaxed(0xfffffff0, qphy->base + HS2_LOCAL_RESET_REG_ADDR);
	usleep_range(10000, 12000);

	/* selection of HS phy clock MMCM value */
	for (i = 0; i < qphy->emu_init_seq_len; i = i+2) {
		dev_dbg(phy->dev, "write 0x%02x to 0x%02x\n",
				qphy->emu_init_seq[i], qphy->emu_init_seq[i+1]);
		writel_relaxed(qphy->emu_init_seq[i],
				qphy->base + qphy->emu_init_seq[i+1]);
		/* 10ms to ensure write propagates across bus */
		usleep_range(10000, 12000);
	}

	/* clear other reset */
	writel_relaxed(0x0, qphy->base + HS2_LOCAL_RESET_REG_ADDR);
	usleep_range(10000, 12000);

	/* clock select to read UTMI/ULPI clock */
	writel_relaxed(0x9, qphy->base + HS2_CLK_STATUS_SEL_ADDR);
	usleep_range(10000, 12000);
	dev_info(phy->dev, "PHY UTMI/ULPI CLK frequency:%d MHz\n",
		(readl_relaxed(qphy->base + HS2_CLK_STATUS_ADDR) / 1000));

	if (qphy->qscratch_base) {
		/* Use UTMI VBUS signal from HW */
		tmp = readl_relaxed(qphy->qscratch_base + HS_PHY_CTRL_REG);
		tmp &= ~SW_SESSVLD_SEL;
		writel_relaxed(tmp, qphy->qscratch_base + HS_PHY_CTRL_REG);
	}

	return 0;
}

static int qcusb_emu_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct qcusb_emu_phy *qphy;
	struct resource *res;
	int ret, size;

	qphy = devm_kzalloc(dev, sizeof(*qphy), GFP_KERNEL);
	if (!qphy)
		return -ENOMEM;

	qphy->phy.dev = dev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	qphy->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(qphy->base))
		return PTR_ERR(qphy->base);

	of_get_property(dev->of_node, "qcom,emu-init-seq", &size);
	if (!size) {
		dev_err(dev, "emu-init-seq not specified\n");
		return -EINVAL;
	}

	qphy->emu_init_seq = devm_kzalloc(dev, size, GFP_KERNEL);
	if (!qphy->emu_init_seq)
		return -ENOMEM;

	qphy->emu_init_seq_len = (size / sizeof(*qphy->emu_init_seq));
	if (qphy->emu_init_seq_len % 2) {
		dev_err(dev, "invalid emu_init_seq_len, must be in <data,addr> pairs\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_array(dev->of_node, "qcom,emu-init-seq",
			qphy->emu_init_seq, qphy->emu_init_seq_len);
	if (ret) {
		dev_err(dev, "could not read emu-init-seq, returned %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, qphy);

	qphy->phy.label			= "qcom-usb-emu-phy";
	qphy->phy.init			= qcusb_emu_phy_init;
	qphy->phy.type			= USB_PHY_TYPE_USB2;

	ret = usb_add_phy_dev(&qphy->phy);
	if (ret)
		return ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
							"qscratch_base");
	if (res) {
		qphy->qscratch_base = devm_ioremap(dev, res->start,
						resource_size(res));
		if (IS_ERR(qphy->qscratch_base)) {
			dev_dbg(dev, "error mapping qscratch\n");
			qphy->qscratch_base = NULL;
		}
	}

	return 0;
}

static int qcusb_emu_phy_remove(struct platform_device *pdev)
{
	struct qcusb_emu_phy *qcphy = platform_get_drvdata(pdev);

	usb_remove_phy(&qcphy->phy);

	return 0;
}

static const struct of_device_id emu_phy_dt_ids[] = {
	{ .compatible = "qcom,usb-emu-phy" },
	{ }
};

MODULE_DEVICE_TABLE(of, emu_phy_dt_ids);

static struct platform_driver qcusb_emu_phy_driver = {
	.probe		= qcusb_emu_phy_probe,
	.remove		= qcusb_emu_phy_remove,
	.driver		= {
		.name	= "usb_emu_phy",
		.of_match_table = emu_phy_dt_ids,
	},
};

module_platform_driver(qcusb_emu_phy_driver);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. USB Emulation PHY driver");
MODULE_LICENSE("GPL v2");
