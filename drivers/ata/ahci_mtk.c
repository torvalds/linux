// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek AHCI SATA driver
 *
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Ryder Lee <ryder.lee@mediatek.com>
 */

#include <linux/ahci_platform.h>
#include <linux/kernel.h>
#include <linux/libata.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include "ahci.h"

#define DRV_NAME		"ahci-mtk"

#define SYS_CFG			0x14
#define SYS_CFG_SATA_MSK	GENMASK(31, 30)
#define SYS_CFG_SATA_EN		BIT(31)

struct mtk_ahci_plat {
	struct regmap *mode;
	struct reset_control *axi_rst;
	struct reset_control *sw_rst;
	struct reset_control *reg_rst;
};

static const struct ata_port_info ahci_port_info = {
	.flags		= AHCI_FLAG_COMMON,
	.pio_mask	= ATA_PIO4,
	.udma_mask	= ATA_UDMA6,
	.port_ops	= &ahci_platform_ops,
};

static struct scsi_host_template ahci_platform_sht = {
	AHCI_SHT(DRV_NAME),
};

static int mtk_ahci_platform_resets(struct ahci_host_priv *hpriv,
				    struct device *dev)
{
	struct mtk_ahci_plat *plat = hpriv->plat_data;
	int err;

	/* reset AXI bus and PHY part */
	plat->axi_rst = devm_reset_control_get_optional_exclusive(dev, "axi");
	if (PTR_ERR(plat->axi_rst) == -EPROBE_DEFER)
		return PTR_ERR(plat->axi_rst);

	plat->sw_rst = devm_reset_control_get_optional_exclusive(dev, "sw");
	if (PTR_ERR(plat->sw_rst) == -EPROBE_DEFER)
		return PTR_ERR(plat->sw_rst);

	plat->reg_rst = devm_reset_control_get_optional_exclusive(dev, "reg");
	if (PTR_ERR(plat->reg_rst) == -EPROBE_DEFER)
		return PTR_ERR(plat->reg_rst);

	err = reset_control_assert(plat->axi_rst);
	if (err) {
		dev_err(dev, "failed to assert AXI bus\n");
		return err;
	}

	err = reset_control_assert(plat->sw_rst);
	if (err) {
		dev_err(dev, "failed to assert PHY digital part\n");
		return err;
	}

	err = reset_control_assert(plat->reg_rst);
	if (err) {
		dev_err(dev, "failed to assert PHY register part\n");
		return err;
	}

	err = reset_control_deassert(plat->reg_rst);
	if (err) {
		dev_err(dev, "failed to deassert PHY register part\n");
		return err;
	}

	err = reset_control_deassert(plat->sw_rst);
	if (err) {
		dev_err(dev, "failed to deassert PHY digital part\n");
		return err;
	}

	err = reset_control_deassert(plat->axi_rst);
	if (err) {
		dev_err(dev, "failed to deassert AXI bus\n");
		return err;
	}

	return 0;
}

static int mtk_ahci_parse_property(struct ahci_host_priv *hpriv,
				   struct device *dev)
{
	struct mtk_ahci_plat *plat = hpriv->plat_data;
	struct device_node *np = dev->of_node;

	/* enable SATA function if needed */
	if (of_find_property(np, "mediatek,phy-mode", NULL)) {
		plat->mode = syscon_regmap_lookup_by_phandle(
					np, "mediatek,phy-mode");
		if (IS_ERR(plat->mode)) {
			dev_err(dev, "missing phy-mode phandle\n");
			return PTR_ERR(plat->mode);
		}

		regmap_update_bits(plat->mode, SYS_CFG, SYS_CFG_SATA_MSK,
				   SYS_CFG_SATA_EN);
	}

	of_property_read_u32(np, "ports-implemented", &hpriv->force_port_map);

	return 0;
}

static int mtk_ahci_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_ahci_plat *plat;
	struct ahci_host_priv *hpriv;
	int err;

	plat = devm_kzalloc(dev, sizeof(*plat), GFP_KERNEL);
	if (!plat)
		return -ENOMEM;

	hpriv = ahci_platform_get_resources(pdev, 0);
	if (IS_ERR(hpriv))
		return PTR_ERR(hpriv);

	hpriv->plat_data = plat;

	err = mtk_ahci_parse_property(hpriv, dev);
	if (err)
		return err;

	err = mtk_ahci_platform_resets(hpriv, dev);
	if (err)
		return err;

	err = ahci_platform_enable_resources(hpriv);
	if (err)
		return err;

	err = ahci_platform_init_host(pdev, hpriv, &ahci_port_info,
				      &ahci_platform_sht);
	if (err)
		goto disable_resources;

	return 0;

disable_resources:
	ahci_platform_disable_resources(hpriv);
	return err;
}

static SIMPLE_DEV_PM_OPS(ahci_pm_ops, ahci_platform_suspend,
			 ahci_platform_resume);

static const struct of_device_id ahci_of_match[] = {
	{ .compatible = "mediatek,mtk-ahci", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ahci_of_match);

static struct platform_driver mtk_ahci_driver = {
	.probe = mtk_ahci_probe,
	.remove = ata_platform_remove_one,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = ahci_of_match,
		.pm = &ahci_pm_ops,
	},
};
module_platform_driver(mtk_ahci_driver);

MODULE_DESCRIPTION("MediaTek SATA AHCI Driver");
MODULE_LICENSE("GPL v2");
