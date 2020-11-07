// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016-2018 Broadcom
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

enum bcm_usb_phy_version {
	BCM_SR_USB_COMBO_PHY,
	BCM_SR_USB_HS_PHY,
};

enum bcm_usb_phy_reg {
	PLL_CTRL,
	PHY_CTRL,
	PHY_PLL_CTRL,
};

/* USB PHY registers */

static const u8 bcm_usb_combo_phy_ss[] = {
	[PLL_CTRL]		= 0x18,
	[PHY_CTRL]		= 0x14,
};

static const u8 bcm_usb_combo_phy_hs[] = {
	[PLL_CTRL]	= 0x0c,
	[PHY_CTRL]	= 0x10,
};

static const u8 bcm_usb_hs_phy[] = {
	[PLL_CTRL]	= 0x8,
	[PHY_CTRL]	= 0xc,
};

enum pll_ctrl_bits {
	PLL_RESETB,
	SSPLL_SUSPEND_EN,
	PLL_SEQ_START,
	PLL_LOCK,
};

static const u8 u3pll_ctrl[] = {
	[PLL_RESETB]		= 0,
	[SSPLL_SUSPEND_EN]	= 1,
	[PLL_SEQ_START]		= 2,
	[PLL_LOCK]		= 3,
};

#define HSPLL_PDIV_MASK		0xF
#define HSPLL_PDIV_VAL		0x1

static const u8 u2pll_ctrl[] = {
	[PLL_RESETB]	= 5,
	[PLL_LOCK]	= 6,
};

enum bcm_usb_phy_ctrl_bits {
	CORERDY,
	PHY_RESETB,
	PHY_PCTL,
};

#define PHY_PCTL_MASK	0xffff
#define SSPHY_PCTL_VAL	0x0006

static const u8 u3phy_ctrl[] = {
	[PHY_RESETB]	= 1,
	[PHY_PCTL]	= 2,
};

static const u8 u2phy_ctrl[] = {
	[CORERDY]		= 0,
	[PHY_RESETB]		= 5,
	[PHY_PCTL]		= 6,
};

struct bcm_usb_phy_cfg {
	uint32_t type;
	uint32_t version;
	void __iomem *regs;
	struct phy *phy;
	const u8 *offset;
};

#define PLL_LOCK_RETRY_COUNT	1000

enum bcm_usb_phy_type {
	USB_HS_PHY,
	USB_SS_PHY,
};

#define NUM_BCM_SR_USB_COMBO_PHYS	2

static inline void bcm_usb_reg32_clrbits(void __iomem *addr, uint32_t clear)
{
	writel(readl(addr) & ~clear, addr);
}

static inline void bcm_usb_reg32_setbits(void __iomem *addr, uint32_t set)
{
	writel(readl(addr) | set, addr);
}

static int bcm_usb_pll_lock_check(void __iomem *addr, u32 bit)
{
	u32 data;
	int ret;

	ret = readl_poll_timeout_atomic(addr, data, (data & bit), 1,
					PLL_LOCK_RETRY_COUNT);
	if (ret)
		pr_err("%s: FAIL\n", __func__);

	return ret;
}

static int bcm_usb_ss_phy_init(struct bcm_usb_phy_cfg *phy_cfg)
{
	int ret = 0;
	void __iomem *regs = phy_cfg->regs;
	const u8 *offset;
	u32 rd_data;

	offset = phy_cfg->offset;

	/* Set pctl with mode and soft reset */
	rd_data = readl(regs + offset[PHY_CTRL]);
	rd_data &= ~(PHY_PCTL_MASK << u3phy_ctrl[PHY_PCTL]);
	rd_data |= (SSPHY_PCTL_VAL << u3phy_ctrl[PHY_PCTL]);
	writel(rd_data, regs + offset[PHY_CTRL]);

	bcm_usb_reg32_clrbits(regs + offset[PLL_CTRL],
			      BIT(u3pll_ctrl[SSPLL_SUSPEND_EN]));
	bcm_usb_reg32_setbits(regs + offset[PLL_CTRL],
			      BIT(u3pll_ctrl[PLL_SEQ_START]));
	bcm_usb_reg32_setbits(regs + offset[PLL_CTRL],
			      BIT(u3pll_ctrl[PLL_RESETB]));

	/* Maximum timeout for PLL reset done */
	msleep(30);

	ret = bcm_usb_pll_lock_check(regs + offset[PLL_CTRL],
				     BIT(u3pll_ctrl[PLL_LOCK]));

	return ret;
}

static int bcm_usb_hs_phy_init(struct bcm_usb_phy_cfg *phy_cfg)
{
	int ret = 0;
	void __iomem *regs = phy_cfg->regs;
	const u8 *offset;

	offset = phy_cfg->offset;

	bcm_usb_reg32_clrbits(regs + offset[PLL_CTRL],
			      BIT(u2pll_ctrl[PLL_RESETB]));
	bcm_usb_reg32_setbits(regs + offset[PLL_CTRL],
			      BIT(u2pll_ctrl[PLL_RESETB]));

	ret = bcm_usb_pll_lock_check(regs + offset[PLL_CTRL],
				     BIT(u2pll_ctrl[PLL_LOCK]));

	return ret;
}

static int bcm_usb_phy_reset(struct phy *phy)
{
	struct bcm_usb_phy_cfg *phy_cfg = phy_get_drvdata(phy);
	void __iomem *regs = phy_cfg->regs;
	const u8 *offset;

	offset = phy_cfg->offset;

	if (phy_cfg->type == USB_HS_PHY) {
		bcm_usb_reg32_clrbits(regs + offset[PHY_CTRL],
				      BIT(u2phy_ctrl[CORERDY]));
		bcm_usb_reg32_setbits(regs + offset[PHY_CTRL],
				      BIT(u2phy_ctrl[CORERDY]));
	}

	return 0;
}

static int bcm_usb_phy_init(struct phy *phy)
{
	struct bcm_usb_phy_cfg *phy_cfg = phy_get_drvdata(phy);
	int ret = -EINVAL;

	if (phy_cfg->type == USB_SS_PHY)
		ret = bcm_usb_ss_phy_init(phy_cfg);
	else if (phy_cfg->type == USB_HS_PHY)
		ret = bcm_usb_hs_phy_init(phy_cfg);

	return ret;
}

static const struct phy_ops sr_phy_ops = {
	.init		= bcm_usb_phy_init,
	.reset		= bcm_usb_phy_reset,
	.owner		= THIS_MODULE,
};

static struct phy *bcm_usb_phy_xlate(struct device *dev,
				     struct of_phandle_args *args)
{
	struct bcm_usb_phy_cfg *phy_cfg;
	int phy_idx;

	phy_cfg = dev_get_drvdata(dev);
	if (!phy_cfg)
		return ERR_PTR(-EINVAL);

	if (phy_cfg->version == BCM_SR_USB_COMBO_PHY) {
		phy_idx = args->args[0];

		if (WARN_ON(phy_idx > 1))
			return ERR_PTR(-ENODEV);

		return phy_cfg[phy_idx].phy;
	} else
		return phy_cfg->phy;
}

static int bcm_usb_phy_create(struct device *dev, struct device_node *node,
			      void __iomem *regs, uint32_t version)
{
	struct bcm_usb_phy_cfg *phy_cfg;
	int idx;

	if (version == BCM_SR_USB_COMBO_PHY) {
		phy_cfg = devm_kzalloc(dev, NUM_BCM_SR_USB_COMBO_PHYS *
				       sizeof(struct bcm_usb_phy_cfg),
				       GFP_KERNEL);
		if (!phy_cfg)
			return -ENOMEM;

		for (idx = 0; idx < NUM_BCM_SR_USB_COMBO_PHYS; idx++) {
			phy_cfg[idx].regs = regs;
			phy_cfg[idx].version = version;
			if (idx == 0) {
				phy_cfg[idx].offset = bcm_usb_combo_phy_hs;
				phy_cfg[idx].type = USB_HS_PHY;
			} else {
				phy_cfg[idx].offset = bcm_usb_combo_phy_ss;
				phy_cfg[idx].type = USB_SS_PHY;
			}
			phy_cfg[idx].phy = devm_phy_create(dev, node,
							   &sr_phy_ops);
			if (IS_ERR(phy_cfg[idx].phy))
				return PTR_ERR(phy_cfg[idx].phy);

			phy_set_drvdata(phy_cfg[idx].phy, &phy_cfg[idx]);
		}
	} else if (version == BCM_SR_USB_HS_PHY) {
		phy_cfg = devm_kzalloc(dev, sizeof(struct bcm_usb_phy_cfg),
				       GFP_KERNEL);
		if (!phy_cfg)
			return -ENOMEM;

		phy_cfg->regs = regs;
		phy_cfg->version = version;
		phy_cfg->offset = bcm_usb_hs_phy;
		phy_cfg->type = USB_HS_PHY;
		phy_cfg->phy = devm_phy_create(dev, node, &sr_phy_ops);
		if (IS_ERR(phy_cfg->phy))
			return PTR_ERR(phy_cfg->phy);

		phy_set_drvdata(phy_cfg->phy, phy_cfg);
	} else
		return -ENODEV;

	dev_set_drvdata(dev, phy_cfg);

	return 0;
}

static const struct of_device_id bcm_usb_phy_of_match[] = {
	{
		.compatible = "brcm,sr-usb-combo-phy",
		.data = (void *)BCM_SR_USB_COMBO_PHY,
	},
	{
		.compatible = "brcm,sr-usb-hs-phy",
		.data = (void *)BCM_SR_USB_HS_PHY,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, bcm_usb_phy_of_match);

static int bcm_usb_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *dn = dev->of_node;
	const struct of_device_id *of_id;
	struct resource *res;
	void __iomem *regs;
	int ret;
	enum bcm_usb_phy_version version;
	struct phy_provider *phy_provider;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	of_id = of_match_node(bcm_usb_phy_of_match, dn);
	if (of_id)
		version = (enum bcm_usb_phy_version)of_id->data;
	else
		return -ENODEV;

	ret = bcm_usb_phy_create(dev, dn, regs, version);
	if (ret)
		return ret;

	phy_provider = devm_of_phy_provider_register(dev, bcm_usb_phy_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static struct platform_driver bcm_usb_phy_driver = {
	.driver = {
		.name = "phy-bcm-sr-usb",
		.of_match_table = bcm_usb_phy_of_match,
	},
	.probe = bcm_usb_phy_probe,
};
module_platform_driver(bcm_usb_phy_driver);

MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("Broadcom stingray USB Phy driver");
MODULE_LICENSE("GPL v2");
