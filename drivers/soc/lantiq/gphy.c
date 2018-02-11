/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2012 John Crispin <blogic@phrozen.org>
 *  Copyright (C) 2016 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 *  Copyright (C) 2017 Hauke Mehrtens <hauke@hauke-m.de>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/property.h>
#include <dt-bindings/mips/lantiq_rcu_gphy.h>

#include <lantiq_soc.h>

#define XRX200_GPHY_FW_ALIGN	(16 * 1024)

struct xway_gphy_priv {
	struct clk *gphy_clk_gate;
	struct reset_control *gphy_reset;
	struct reset_control *gphy_reset2;
	struct notifier_block gphy_reboot_nb;
	void __iomem *membase;
	char *fw_name;
};

struct xway_gphy_match_data {
	char *fe_firmware_name;
	char *ge_firmware_name;
};

static const struct xway_gphy_match_data xrx200a1x_gphy_data = {
	.fe_firmware_name = "lantiq/xrx200_phy22f_a14.bin",
	.ge_firmware_name = "lantiq/xrx200_phy11g_a14.bin",
};

static const struct xway_gphy_match_data xrx200a2x_gphy_data = {
	.fe_firmware_name = "lantiq/xrx200_phy22f_a22.bin",
	.ge_firmware_name = "lantiq/xrx200_phy11g_a22.bin",
};

static const struct xway_gphy_match_data xrx300_gphy_data = {
	.fe_firmware_name = "lantiq/xrx300_phy22f_a21.bin",
	.ge_firmware_name = "lantiq/xrx300_phy11g_a21.bin",
};

static const struct of_device_id xway_gphy_match[] = {
	{ .compatible = "lantiq,xrx200a1x-gphy", .data = &xrx200a1x_gphy_data },
	{ .compatible = "lantiq,xrx200a2x-gphy", .data = &xrx200a2x_gphy_data },
	{ .compatible = "lantiq,xrx300-gphy", .data = &xrx300_gphy_data },
	{ .compatible = "lantiq,xrx330-gphy", .data = &xrx300_gphy_data },
	{},
};
MODULE_DEVICE_TABLE(of, xway_gphy_match);

static struct xway_gphy_priv *to_xway_gphy_priv(struct notifier_block *nb)
{
	return container_of(nb, struct xway_gphy_priv, gphy_reboot_nb);
}

static int xway_gphy_reboot_notify(struct notifier_block *reboot_nb,
				   unsigned long code, void *unused)
{
	struct xway_gphy_priv *priv = to_xway_gphy_priv(reboot_nb);

	if (priv) {
		reset_control_assert(priv->gphy_reset);
		reset_control_assert(priv->gphy_reset2);
	}

	return NOTIFY_DONE;
}

static int xway_gphy_load(struct device *dev, struct xway_gphy_priv *priv,
			  dma_addr_t *dev_addr)
{
	const struct firmware *fw;
	void *fw_addr;
	dma_addr_t dma_addr;
	size_t size;
	int ret;

	ret = request_firmware(&fw, priv->fw_name, dev);
	if (ret) {
		dev_err(dev, "failed to load firmware: %s, error: %i\n",
			priv->fw_name, ret);
		return ret;
	}

	/*
	 * GPHY cores need the firmware code in a persistent and contiguous
	 * memory area with a 16 kB boundary aligned start address.
	 */
	size = fw->size + XRX200_GPHY_FW_ALIGN;

	fw_addr = dmam_alloc_coherent(dev, size, &dma_addr, GFP_KERNEL);
	if (fw_addr) {
		fw_addr = PTR_ALIGN(fw_addr, XRX200_GPHY_FW_ALIGN);
		*dev_addr = ALIGN(dma_addr, XRX200_GPHY_FW_ALIGN);
		memcpy(fw_addr, fw->data, fw->size);
	} else {
		dev_err(dev, "failed to alloc firmware memory\n");
		ret = -ENOMEM;
	}

	release_firmware(fw);

	return ret;
}

static int xway_gphy_of_probe(struct platform_device *pdev,
			      struct xway_gphy_priv *priv)
{
	struct device *dev = &pdev->dev;
	const struct xway_gphy_match_data *gphy_fw_name_cfg;
	u32 gphy_mode;
	int ret;
	struct resource *res_gphy;

	gphy_fw_name_cfg = of_device_get_match_data(dev);

	priv->gphy_clk_gate = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->gphy_clk_gate)) {
		dev_err(dev, "Failed to lookup gate clock\n");
		return PTR_ERR(priv->gphy_clk_gate);
	}

	res_gphy = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->membase = devm_ioremap_resource(dev, res_gphy);
	if (IS_ERR(priv->membase))
		return PTR_ERR(priv->membase);

	priv->gphy_reset = devm_reset_control_get(dev, "gphy");
	if (IS_ERR(priv->gphy_reset)) {
		if (PTR_ERR(priv->gphy_reset) != -EPROBE_DEFER)
			dev_err(dev, "Failed to lookup gphy reset\n");
		return PTR_ERR(priv->gphy_reset);
	}

	priv->gphy_reset2 = devm_reset_control_get_optional(dev, "gphy2");
	if (IS_ERR(priv->gphy_reset2))
		return PTR_ERR(priv->gphy_reset2);

	ret = device_property_read_u32(dev, "lantiq,gphy-mode", &gphy_mode);
	/* Default to GE mode */
	if (ret)
		gphy_mode = GPHY_MODE_GE;

	switch (gphy_mode) {
	case GPHY_MODE_FE:
		priv->fw_name = gphy_fw_name_cfg->fe_firmware_name;
		break;
	case GPHY_MODE_GE:
		priv->fw_name = gphy_fw_name_cfg->ge_firmware_name;
		break;
	default:
		dev_err(dev, "Unknown GPHY mode %d\n", gphy_mode);
		return -EINVAL;
	}

	return 0;
}

static int xway_gphy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct xway_gphy_priv *priv;
	dma_addr_t fw_addr = 0;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = xway_gphy_of_probe(pdev, priv);
	if (ret)
		return ret;

	ret = clk_prepare_enable(priv->gphy_clk_gate);
	if (ret)
		return ret;

	ret = xway_gphy_load(dev, priv, &fw_addr);
	if (ret) {
		clk_disable_unprepare(priv->gphy_clk_gate);
		return ret;
	}

	reset_control_assert(priv->gphy_reset);
	reset_control_assert(priv->gphy_reset2);

	iowrite32be(fw_addr, priv->membase);

	reset_control_deassert(priv->gphy_reset);
	reset_control_deassert(priv->gphy_reset2);

	/* assert the gphy reset because it can hang after a reboot: */
	priv->gphy_reboot_nb.notifier_call = xway_gphy_reboot_notify;
	priv->gphy_reboot_nb.priority = -1;

	ret = register_reboot_notifier(&priv->gphy_reboot_nb);
	if (ret)
		dev_warn(dev, "Failed to register reboot notifier\n");

	platform_set_drvdata(pdev, priv);

	return ret;
}

static int xway_gphy_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct xway_gphy_priv *priv = platform_get_drvdata(pdev);
	int ret;

	reset_control_assert(priv->gphy_reset);
	reset_control_assert(priv->gphy_reset2);

	iowrite32be(0, priv->membase);

	clk_disable_unprepare(priv->gphy_clk_gate);

	ret = unregister_reboot_notifier(&priv->gphy_reboot_nb);
	if (ret)
		dev_warn(dev, "Failed to unregister reboot notifier\n");

	return 0;
}

static struct platform_driver xway_gphy_driver = {
	.probe = xway_gphy_probe,
	.remove = xway_gphy_remove,
	.driver = {
		.name = "xway-rcu-gphy",
		.of_match_table = xway_gphy_match,
	},
};

module_platform_driver(xway_gphy_driver);

MODULE_FIRMWARE("lantiq/xrx300_phy11g_a21.bin");
MODULE_FIRMWARE("lantiq/xrx300_phy22f_a21.bin");
MODULE_FIRMWARE("lantiq/xrx200_phy11g_a14.bin");
MODULE_FIRMWARE("lantiq/xrx200_phy11g_a22.bin");
MODULE_FIRMWARE("lantiq/xrx200_phy22f_a14.bin");
MODULE_FIRMWARE("lantiq/xrx200_phy22f_a22.bin");
MODULE_AUTHOR("Martin Blumenstingl <martin.blumenstingl@googlemail.com>");
MODULE_DESCRIPTION("Lantiq XWAY GPHY Firmware Loader");
MODULE_LICENSE("GPL");
