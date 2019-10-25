// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 * Copyright (C) 2011 Google, Inc.
 *
 * Author:
 *	Jay Cheng <jacheng@nvidia.com>
 *	James Wylder <james.wylder@motorola.com>
 *	Benoit Goby <benoit@android.com>
 *	Colin Cross <ccross@android.com>
 *	Hiroshi DOYU <hdoyu@nvidia.com>
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>

#include <soc/tegra/ahb.h>

#define DRV_NAME "tegra-ahb"

#define AHB_ARBITRATION_DISABLE		0x04
#define AHB_ARBITRATION_PRIORITY_CTRL	0x08
#define   AHB_PRIORITY_WEIGHT(x)	(((x) & 0x7) << 29)
#define   PRIORITY_SELECT_USB BIT(6)
#define   PRIORITY_SELECT_USB2 BIT(18)
#define   PRIORITY_SELECT_USB3 BIT(17)

#define AHB_GIZMO_AHB_MEM		0x10
#define   ENB_FAST_REARBITRATE BIT(2)
#define   DONT_SPLIT_AHB_WR     BIT(7)

#define AHB_GIZMO_APB_DMA		0x14
#define AHB_GIZMO_IDE			0x1c
#define AHB_GIZMO_USB			0x20
#define AHB_GIZMO_AHB_XBAR_BRIDGE	0x24
#define AHB_GIZMO_CPU_AHB_BRIDGE	0x28
#define AHB_GIZMO_COP_AHB_BRIDGE	0x2c
#define AHB_GIZMO_XBAR_APB_CTLR		0x30
#define AHB_GIZMO_VCP_AHB_BRIDGE	0x34
#define AHB_GIZMO_NAND			0x40
#define AHB_GIZMO_SDMMC4		0x48
#define AHB_GIZMO_XIO			0x4c
#define AHB_GIZMO_BSEV			0x64
#define AHB_GIZMO_BSEA			0x74
#define AHB_GIZMO_NOR			0x78
#define AHB_GIZMO_USB2			0x7c
#define AHB_GIZMO_USB3			0x80
#define   IMMEDIATE	BIT(18)

#define AHB_GIZMO_SDMMC1		0x84
#define AHB_GIZMO_SDMMC2		0x88
#define AHB_GIZMO_SDMMC3		0x8c
#define AHB_MEM_PREFETCH_CFG_X		0xdc
#define AHB_ARBITRATION_XBAR_CTRL	0xe0
#define AHB_MEM_PREFETCH_CFG3		0xe4
#define AHB_MEM_PREFETCH_CFG4		0xe8
#define AHB_MEM_PREFETCH_CFG1		0xf0
#define AHB_MEM_PREFETCH_CFG2		0xf4
#define   PREFETCH_ENB	BIT(31)
#define   MST_ID(x)	(((x) & 0x1f) << 26)
#define   AHBDMA_MST_ID	MST_ID(5)
#define   USB_MST_ID	MST_ID(6)
#define   USB2_MST_ID	MST_ID(18)
#define   USB3_MST_ID	MST_ID(17)
#define   ADDR_BNDRY(x)	(((x) & 0xf) << 21)
#define   INACTIVITY_TIMEOUT(x)	(((x) & 0xffff) << 0)

#define AHB_ARBITRATION_AHB_MEM_WRQUE_MST_ID	0xfc

#define AHB_ARBITRATION_XBAR_CTRL_SMMU_INIT_DONE BIT(17)

/*
 * INCORRECT_BASE_ADDR_LOW_BYTE: Legacy kernel DT files for Tegra SoCs
 * prior to Tegra124 generally use a physical base address ending in
 * 0x4 for the AHB IP block.  According to the TRM, the low byte
 * should be 0x0.  During device probing, this macro is used to detect
 * whether the passed-in physical address is incorrect, and if so, to
 * correct it.
 */
#define INCORRECT_BASE_ADDR_LOW_BYTE		0x4

static struct platform_driver tegra_ahb_driver;

static const u32 tegra_ahb_gizmo[] = {
	AHB_ARBITRATION_DISABLE,
	AHB_ARBITRATION_PRIORITY_CTRL,
	AHB_GIZMO_AHB_MEM,
	AHB_GIZMO_APB_DMA,
	AHB_GIZMO_IDE,
	AHB_GIZMO_USB,
	AHB_GIZMO_AHB_XBAR_BRIDGE,
	AHB_GIZMO_CPU_AHB_BRIDGE,
	AHB_GIZMO_COP_AHB_BRIDGE,
	AHB_GIZMO_XBAR_APB_CTLR,
	AHB_GIZMO_VCP_AHB_BRIDGE,
	AHB_GIZMO_NAND,
	AHB_GIZMO_SDMMC4,
	AHB_GIZMO_XIO,
	AHB_GIZMO_BSEV,
	AHB_GIZMO_BSEA,
	AHB_GIZMO_NOR,
	AHB_GIZMO_USB2,
	AHB_GIZMO_USB3,
	AHB_GIZMO_SDMMC1,
	AHB_GIZMO_SDMMC2,
	AHB_GIZMO_SDMMC3,
	AHB_MEM_PREFETCH_CFG_X,
	AHB_ARBITRATION_XBAR_CTRL,
	AHB_MEM_PREFETCH_CFG3,
	AHB_MEM_PREFETCH_CFG4,
	AHB_MEM_PREFETCH_CFG1,
	AHB_MEM_PREFETCH_CFG2,
	AHB_ARBITRATION_AHB_MEM_WRQUE_MST_ID,
};

struct tegra_ahb {
	void __iomem	*regs;
	struct device	*dev;
	u32		ctx[0];
};

static inline u32 gizmo_readl(struct tegra_ahb *ahb, u32 offset)
{
	return readl(ahb->regs + offset);
}

static inline void gizmo_writel(struct tegra_ahb *ahb, u32 value, u32 offset)
{
	writel(value, ahb->regs + offset);
}

#ifdef CONFIG_TEGRA_IOMMU_SMMU
int tegra_ahb_enable_smmu(struct device_node *dn)
{
	struct device *dev;
	u32 val;
	struct tegra_ahb *ahb;

	dev = driver_find_device_by_of_node(&tegra_ahb_driver.driver, dn);
	if (!dev)
		return -EPROBE_DEFER;
	ahb = dev_get_drvdata(dev);
	val = gizmo_readl(ahb, AHB_ARBITRATION_XBAR_CTRL);
	val |= AHB_ARBITRATION_XBAR_CTRL_SMMU_INIT_DONE;
	gizmo_writel(ahb, val, AHB_ARBITRATION_XBAR_CTRL);
	return 0;
}
EXPORT_SYMBOL(tegra_ahb_enable_smmu);
#endif

static int __maybe_unused tegra_ahb_suspend(struct device *dev)
{
	int i;
	struct tegra_ahb *ahb = dev_get_drvdata(dev);

	for (i = 0; i < ARRAY_SIZE(tegra_ahb_gizmo); i++)
		ahb->ctx[i] = gizmo_readl(ahb, tegra_ahb_gizmo[i]);
	return 0;
}

static int __maybe_unused tegra_ahb_resume(struct device *dev)
{
	int i;
	struct tegra_ahb *ahb = dev_get_drvdata(dev);

	for (i = 0; i < ARRAY_SIZE(tegra_ahb_gizmo); i++)
		gizmo_writel(ahb, ahb->ctx[i], tegra_ahb_gizmo[i]);
	return 0;
}

static UNIVERSAL_DEV_PM_OPS(tegra_ahb_pm,
			    tegra_ahb_suspend,
			    tegra_ahb_resume, NULL);

static void tegra_ahb_gizmo_init(struct tegra_ahb *ahb)
{
	u32 val;

	val = gizmo_readl(ahb, AHB_GIZMO_AHB_MEM);
	val |= ENB_FAST_REARBITRATE | IMMEDIATE | DONT_SPLIT_AHB_WR;
	gizmo_writel(ahb, val, AHB_GIZMO_AHB_MEM);

	val = gizmo_readl(ahb, AHB_GIZMO_USB);
	val |= IMMEDIATE;
	gizmo_writel(ahb, val, AHB_GIZMO_USB);

	val = gizmo_readl(ahb, AHB_GIZMO_USB2);
	val |= IMMEDIATE;
	gizmo_writel(ahb, val, AHB_GIZMO_USB2);

	val = gizmo_readl(ahb, AHB_GIZMO_USB3);
	val |= IMMEDIATE;
	gizmo_writel(ahb, val, AHB_GIZMO_USB3);

	val = gizmo_readl(ahb, AHB_ARBITRATION_PRIORITY_CTRL);
	val |= PRIORITY_SELECT_USB |
		PRIORITY_SELECT_USB2 |
		PRIORITY_SELECT_USB3 |
		AHB_PRIORITY_WEIGHT(7);
	gizmo_writel(ahb, val, AHB_ARBITRATION_PRIORITY_CTRL);

	val = gizmo_readl(ahb, AHB_MEM_PREFETCH_CFG1);
	val &= ~MST_ID(~0);
	val |= PREFETCH_ENB |
		AHBDMA_MST_ID |
		ADDR_BNDRY(0xc) |
		INACTIVITY_TIMEOUT(0x1000);
	gizmo_writel(ahb, val, AHB_MEM_PREFETCH_CFG1);

	val = gizmo_readl(ahb, AHB_MEM_PREFETCH_CFG2);
	val &= ~MST_ID(~0);
	val |= PREFETCH_ENB |
		USB_MST_ID |
		ADDR_BNDRY(0xc) |
		INACTIVITY_TIMEOUT(0x1000);
	gizmo_writel(ahb, val, AHB_MEM_PREFETCH_CFG2);

	val = gizmo_readl(ahb, AHB_MEM_PREFETCH_CFG3);
	val &= ~MST_ID(~0);
	val |= PREFETCH_ENB |
		USB3_MST_ID |
		ADDR_BNDRY(0xc) |
		INACTIVITY_TIMEOUT(0x1000);
	gizmo_writel(ahb, val, AHB_MEM_PREFETCH_CFG3);

	val = gizmo_readl(ahb, AHB_MEM_PREFETCH_CFG4);
	val &= ~MST_ID(~0);
	val |= PREFETCH_ENB |
		USB2_MST_ID |
		ADDR_BNDRY(0xc) |
		INACTIVITY_TIMEOUT(0x1000);
	gizmo_writel(ahb, val, AHB_MEM_PREFETCH_CFG4);
}

static int tegra_ahb_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct tegra_ahb *ahb;
	size_t bytes;

	bytes = sizeof(*ahb) + sizeof(u32) * ARRAY_SIZE(tegra_ahb_gizmo);
	ahb = devm_kzalloc(&pdev->dev, bytes, GFP_KERNEL);
	if (!ahb)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	/* Correct the IP block base address if necessary */
	if (res &&
	    (res->start & INCORRECT_BASE_ADDR_LOW_BYTE) ==
	    INCORRECT_BASE_ADDR_LOW_BYTE) {
		dev_warn(&pdev->dev, "incorrect AHB base address in DT data - enabling workaround\n");
		res->start -= INCORRECT_BASE_ADDR_LOW_BYTE;
	}

	ahb->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ahb->regs))
		return PTR_ERR(ahb->regs);

	ahb->dev = &pdev->dev;
	platform_set_drvdata(pdev, ahb);
	tegra_ahb_gizmo_init(ahb);
	return 0;
}

static const struct of_device_id tegra_ahb_of_match[] = {
	{ .compatible = "nvidia,tegra30-ahb", },
	{ .compatible = "nvidia,tegra20-ahb", },
	{},
};

static struct platform_driver tegra_ahb_driver = {
	.probe = tegra_ahb_probe,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = tegra_ahb_of_match,
		.pm = &tegra_ahb_pm,
	},
};
module_platform_driver(tegra_ahb_driver);

MODULE_AUTHOR("Hiroshi DOYU <hdoyu@nvidia.com>");
MODULE_DESCRIPTION("Tegra AHB driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
