// SPDX-License-Identifier: GPL-2.0
/*
 * Lantiq / Intel GSWIP switch driver for VRX200, xRX300 and xRX330 SoCs
 *
 * Copyright (C) 2025 Daniel Golle <daniel@makrotopia.org>
 * Copyright (C) 2017 - 2019 Hauke Mehrtens <hauke@hauke-m.de>
 * Copyright (C) 2012 John Crispin <john@phrozen.org>
 * Copyright (C) 2010 Lantiq Deutschland
 */

#include "lantiq_gswip.h"
#include "lantiq_pce.h"

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <dt-bindings/mips/lantiq_rcu_gphy.h>

#include <net/dsa.h>

struct xway_gphy_match_data {
	char *fe_firmware_name;
	char *ge_firmware_name;
};

static void gswip_xrx200_phylink_get_caps(struct dsa_switch *ds, int port,
					  struct phylink_config *config)
{
	switch (port) {
	case 0 ... 1:
		phy_interface_set_rgmii(config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_MII,
			  config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_REVMII,
			  config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_RMII,
			  config->supported_interfaces);
		break;

	case 2 ... 4:
	case 6:
		__set_bit(PHY_INTERFACE_MODE_INTERNAL,
			  config->supported_interfaces);
		break;

	case 5:
		phy_interface_set_rgmii(config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_INTERNAL,
			  config->supported_interfaces);
		break;
	}

	config->mac_capabilities = MAC_ASYM_PAUSE | MAC_SYM_PAUSE |
		MAC_10 | MAC_100 | MAC_1000;
}

static void gswip_xrx300_phylink_get_caps(struct dsa_switch *ds, int port,
					  struct phylink_config *config)
{
	switch (port) {
	case 0:
		phy_interface_set_rgmii(config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_GMII,
			  config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_RMII,
			  config->supported_interfaces);
		break;

	case 1 ... 4:
	case 6:
		__set_bit(PHY_INTERFACE_MODE_INTERNAL,
			  config->supported_interfaces);
		break;

	case 5:
		phy_interface_set_rgmii(config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_INTERNAL,
			  config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_RMII,
			  config->supported_interfaces);
		break;
	}

	config->mac_capabilities = MAC_ASYM_PAUSE | MAC_SYM_PAUSE |
		MAC_10 | MAC_100 | MAC_1000;
}

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

static const struct of_device_id xway_gphy_match[] __maybe_unused = {
	{ .compatible = "lantiq,xrx200-gphy-fw", .data = NULL },
	{ .compatible = "lantiq,xrx200a1x-gphy-fw", .data = &xrx200a1x_gphy_data },
	{ .compatible = "lantiq,xrx200a2x-gphy-fw", .data = &xrx200a2x_gphy_data },
	{ .compatible = "lantiq,xrx300-gphy-fw", .data = &xrx300_gphy_data },
	{ .compatible = "lantiq,xrx330-gphy-fw", .data = &xrx300_gphy_data },
	{},
};

static int gswip_gphy_fw_load(struct gswip_priv *priv, struct gswip_gphy_fw *gphy_fw)
{
	struct device *dev = priv->dev;
	const struct firmware *fw;
	void *fw_addr;
	dma_addr_t dma_addr;
	dma_addr_t dev_addr;
	size_t size;
	int ret;

	ret = clk_prepare_enable(gphy_fw->clk_gate);
	if (ret)
		return ret;

	reset_control_assert(gphy_fw->reset);

	/* The vendor BSP uses a 200ms delay after asserting the reset line.
	 * Without this some users are observing that the PHY is not coming up
	 * on the MDIO bus.
	 */
	msleep(200);

	ret = request_firmware(&fw, gphy_fw->fw_name, dev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to load firmware: %s\n",
				     gphy_fw->fw_name);

	/* GPHY cores need the firmware code in a persistent and contiguous
	 * memory area with a 16 kB boundary aligned start address.
	 */
	size = fw->size + XRX200_GPHY_FW_ALIGN;

	fw_addr = dmam_alloc_coherent(dev, size, &dma_addr, GFP_KERNEL);
	if (fw_addr) {
		fw_addr = PTR_ALIGN(fw_addr, XRX200_GPHY_FW_ALIGN);
		dev_addr = ALIGN(dma_addr, XRX200_GPHY_FW_ALIGN);
		memcpy(fw_addr, fw->data, fw->size);
	} else {
		release_firmware(fw);
		return -ENOMEM;
	}

	release_firmware(fw);

	ret = regmap_write(priv->rcu_regmap, gphy_fw->fw_addr_offset, dev_addr);
	if (ret)
		return ret;

	reset_control_deassert(gphy_fw->reset);

	return ret;
}

static int gswip_gphy_fw_probe(struct gswip_priv *priv,
			       struct gswip_gphy_fw *gphy_fw,
			       struct device_node *gphy_fw_np, int i)
{
	struct device *dev = priv->dev;
	u32 gphy_mode;
	int ret;
	char gphyname[10];

	snprintf(gphyname, sizeof(gphyname), "gphy%d", i);

	gphy_fw->clk_gate = devm_clk_get(dev, gphyname);
	if (IS_ERR(gphy_fw->clk_gate)) {
		return dev_err_probe(dev, PTR_ERR(gphy_fw->clk_gate),
				     "Failed to lookup gate clock\n");
	}

	ret = of_property_read_u32(gphy_fw_np, "reg", &gphy_fw->fw_addr_offset);
	if (ret)
		return ret;

	ret = of_property_read_u32(gphy_fw_np, "lantiq,gphy-mode", &gphy_mode);
	/* Default to GE mode */
	if (ret)
		gphy_mode = GPHY_MODE_GE;

	switch (gphy_mode) {
	case GPHY_MODE_FE:
		gphy_fw->fw_name = priv->gphy_fw_name_cfg->fe_firmware_name;
		break;
	case GPHY_MODE_GE:
		gphy_fw->fw_name = priv->gphy_fw_name_cfg->ge_firmware_name;
		break;
	default:
		return dev_err_probe(dev, -EINVAL, "Unknown GPHY mode %d\n",
				     gphy_mode);
	}

	gphy_fw->reset = of_reset_control_array_get_exclusive(gphy_fw_np);
	if (IS_ERR(gphy_fw->reset))
		return dev_err_probe(dev, PTR_ERR(gphy_fw->reset),
				     "Failed to lookup gphy reset\n");

	return gswip_gphy_fw_load(priv, gphy_fw);
}

static void gswip_gphy_fw_remove(struct gswip_priv *priv,
				 struct gswip_gphy_fw *gphy_fw)
{
	int ret;

	/* check if the device was fully probed */
	if (!gphy_fw->fw_name)
		return;

	ret = regmap_write(priv->rcu_regmap, gphy_fw->fw_addr_offset, 0);
	if (ret)
		dev_err(priv->dev, "can not reset GPHY FW pointer\n");

	clk_disable_unprepare(gphy_fw->clk_gate);

	reset_control_put(gphy_fw->reset);
}

static int gswip_gphy_fw_list(struct gswip_priv *priv,
			      struct device_node *gphy_fw_list_np, u32 version)
{
	struct device *dev = priv->dev;
	struct device_node *gphy_fw_np;
	const struct of_device_id *match;
	int err;
	int i = 0;

	/* The VRX200 rev 1.1 uses the GSWIP 2.0 and needs the older
	 * GPHY firmware. The VRX200 rev 1.2 uses the GSWIP 2.1 and also
	 * needs a different GPHY firmware.
	 */
	if (of_device_is_compatible(gphy_fw_list_np, "lantiq,xrx200-gphy-fw")) {
		switch (version) {
		case GSWIP_VERSION_2_0:
			priv->gphy_fw_name_cfg = &xrx200a1x_gphy_data;
			break;
		case GSWIP_VERSION_2_1:
			priv->gphy_fw_name_cfg = &xrx200a2x_gphy_data;
			break;
		default:
			return dev_err_probe(dev, -ENOENT,
					     "unknown GSWIP version: 0x%x\n",
					     version);
		}
	}

	match = of_match_node(xway_gphy_match, gphy_fw_list_np);
	if (match && match->data)
		priv->gphy_fw_name_cfg = match->data;

	if (!priv->gphy_fw_name_cfg)
		return dev_err_probe(dev, -ENOENT,
				     "GPHY compatible type not supported\n");

	priv->num_gphy_fw = of_get_available_child_count(gphy_fw_list_np);
	if (!priv->num_gphy_fw)
		return -ENOENT;

	priv->rcu_regmap = syscon_regmap_lookup_by_phandle(gphy_fw_list_np,
							   "lantiq,rcu");
	if (IS_ERR(priv->rcu_regmap))
		return PTR_ERR(priv->rcu_regmap);

	priv->gphy_fw = devm_kmalloc_array(dev, priv->num_gphy_fw,
					   sizeof(*priv->gphy_fw),
					   GFP_KERNEL | __GFP_ZERO);
	if (!priv->gphy_fw)
		return -ENOMEM;

	for_each_available_child_of_node(gphy_fw_list_np, gphy_fw_np) {
		err = gswip_gphy_fw_probe(priv, &priv->gphy_fw[i],
					  gphy_fw_np, i);
		if (err) {
			of_node_put(gphy_fw_np);
			goto remove_gphy;
		}
		i++;
	}

	/* The standalone PHY11G requires 300ms to be fully
	 * initialized and ready for any MDIO communication after being
	 * taken out of reset. For the SoC-internal GPHY variant there
	 * is no (known) documentation for the minimum time after a
	 * reset. Use the same value as for the standalone variant as
	 * some users have reported internal PHYs not being detected
	 * without any delay.
	 */
	msleep(300);

	return 0;

remove_gphy:
	for (i = 0; i < priv->num_gphy_fw; i++)
		gswip_gphy_fw_remove(priv, &priv->gphy_fw[i]);
	return err;
}

static const struct regmap_config sw_regmap_config = {
	.name = "switch",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_shift = REGMAP_UPSHIFT(2),
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.max_register = GSWIP_SDMA_PCTRLp(6),
};

static const struct regmap_config mdio_regmap_config = {
	.name = "mdio",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_shift = REGMAP_UPSHIFT(2),
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.max_register = GSWIP_MDIO_PHYp(0),
};

static const struct regmap_config mii_regmap_config = {
	.name = "mii",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_shift = REGMAP_UPSHIFT(2),
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.max_register = GSWIP_MII_CFGp(6),
};

static int gswip_probe(struct platform_device *pdev)
{
	struct device_node *np, *gphy_fw_np;
	__iomem void *gswip, *mdio, *mii;
	struct device *dev = &pdev->dev;
	struct gswip_priv *priv;
	int err;
	int i;
	u32 version;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	gswip = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gswip))
		return PTR_ERR(gswip);

	mdio = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(mdio))
		return PTR_ERR(mdio);

	mii = devm_platform_ioremap_resource(pdev, 2);
	if (IS_ERR(mii))
		return PTR_ERR(mii);

	priv->gswip = devm_regmap_init_mmio(dev, gswip, &sw_regmap_config);
	if (IS_ERR(priv->gswip))
		return PTR_ERR(priv->gswip);

	priv->mdio = devm_regmap_init_mmio(dev, mdio, &mdio_regmap_config);
	if (IS_ERR(priv->mdio))
		return PTR_ERR(priv->mdio);

	priv->mii = devm_regmap_init_mmio(dev, mii, &mii_regmap_config);
	if (IS_ERR(priv->mii))
		return PTR_ERR(priv->mii);

	priv->hw_info = of_device_get_match_data(dev);
	if (!priv->hw_info)
		return -EINVAL;

	priv->ds = devm_kzalloc(dev, sizeof(*priv->ds), GFP_KERNEL);
	if (!priv->ds)
		return -ENOMEM;

	priv->dev = dev;

	regmap_read(priv->gswip, GSWIP_VERSION, &version);

	np = dev->of_node;
	switch (version) {
	case GSWIP_VERSION_2_0:
	case GSWIP_VERSION_2_1:
		if (!of_device_is_compatible(np, "lantiq,xrx200-gswip"))
			return -EINVAL;
		break;
	case GSWIP_VERSION_2_2:
	case GSWIP_VERSION_2_2_ETC:
		if (!of_device_is_compatible(np, "lantiq,xrx300-gswip") &&
		    !of_device_is_compatible(np, "lantiq,xrx330-gswip"))
			return -EINVAL;
		break;
	default:
		return dev_err_probe(dev, -ENOENT,
				     "unknown GSWIP version: 0x%x\n", version);
	}

	/* bring up the mdio bus */
	gphy_fw_np = of_get_compatible_child(dev->of_node, "lantiq,gphy-fw");
	if (gphy_fw_np) {
		err = gswip_gphy_fw_list(priv, gphy_fw_np, version);
		of_node_put(gphy_fw_np);
		if (err)
			return dev_err_probe(dev, err,
					     "gphy fw probe failed\n");
	}

	err = gswip_probe_common(priv, version);
	if (err)
		goto gphy_fw_remove;

	platform_set_drvdata(pdev, priv);

	return 0;

gphy_fw_remove:
	for (i = 0; i < priv->num_gphy_fw; i++)
		gswip_gphy_fw_remove(priv, &priv->gphy_fw[i]);
	return err;
}

static void gswip_remove(struct platform_device *pdev)
{
	struct gswip_priv *priv = platform_get_drvdata(pdev);
	int i;

	if (!priv)
		return;

	dsa_unregister_switch(priv->ds);

	for (i = 0; i < priv->num_gphy_fw; i++)
		gswip_gphy_fw_remove(priv, &priv->gphy_fw[i]);
}

static void gswip_shutdown(struct platform_device *pdev)
{
	struct gswip_priv *priv = platform_get_drvdata(pdev);

	if (!priv)
		return;

	dsa_switch_shutdown(priv->ds);

	platform_set_drvdata(pdev, NULL);
}

static const struct gswip_hw_info gswip_xrx200 = {
	.max_ports = GSWIP_MAX_PORTS,
	.allowed_cpu_ports = BIT(6),
	.mii_cfg = {
		[0] = GSWIP_MII_CFGp(0),
		[1] = GSWIP_MII_CFGp(1),
		[2 ... 4] = -1,
		[5] = GSWIP_MII_CFGp(5),
		[6] = -1,
	},
	.mii_pcdu = {
		[0] = GSWIP_MII_PCDU0,
		[1] = GSWIP_MII_PCDU1,
		[2 ... 4] = -1,
		[5] = GSWIP_MII_PCDU5,
		[6] = -1,
	},
	.phylink_get_caps = gswip_xrx200_phylink_get_caps,
	.pce_microcode = &gswip_pce_microcode,
	.pce_microcode_size = ARRAY_SIZE(gswip_pce_microcode),
	.tag_protocol = DSA_TAG_PROTO_GSWIP,
};

static const struct gswip_hw_info gswip_xrx300 = {
	.max_ports = GSWIP_MAX_PORTS,
	.allowed_cpu_ports = BIT(6),
	.mii_cfg = {
		[0] = GSWIP_MII_CFGp(0),
		[1 ... 4] = -1,
		[5] = GSWIP_MII_CFGp(5),
		[6] = -1,
	},
	.mii_pcdu = {
		[0] = GSWIP_MII_PCDU0,
		[1 ... 4] = -1,
		[5] = GSWIP_MII_PCDU5,
		[6] = -1,
	},
	.phylink_get_caps = gswip_xrx300_phylink_get_caps,
	.pce_microcode = &gswip_pce_microcode,
	.pce_microcode_size = ARRAY_SIZE(gswip_pce_microcode),
	.tag_protocol = DSA_TAG_PROTO_GSWIP,
};

static const struct of_device_id gswip_of_match[] = {
	{ .compatible = "lantiq,xrx200-gswip", .data = &gswip_xrx200 },
	{ .compatible = "lantiq,xrx300-gswip", .data = &gswip_xrx300 },
	{ .compatible = "lantiq,xrx330-gswip", .data = &gswip_xrx300 },
	{},
};
MODULE_DEVICE_TABLE(of, gswip_of_match);

static struct platform_driver gswip_driver = {
	.probe = gswip_probe,
	.remove = gswip_remove,
	.shutdown = gswip_shutdown,
	.driver = {
		.name = "gswip",
		.of_match_table = gswip_of_match,
	},
};

module_platform_driver(gswip_driver);

MODULE_FIRMWARE("lantiq/xrx300_phy11g_a21.bin");
MODULE_FIRMWARE("lantiq/xrx300_phy22f_a21.bin");
MODULE_FIRMWARE("lantiq/xrx200_phy11g_a14.bin");
MODULE_FIRMWARE("lantiq/xrx200_phy11g_a22.bin");
MODULE_FIRMWARE("lantiq/xrx200_phy22f_a14.bin");
MODULE_FIRMWARE("lantiq/xrx200_phy22f_a22.bin");
MODULE_AUTHOR("Hauke Mehrtens <hauke@hauke-m.de>");
MODULE_DESCRIPTION("Lantiq / Intel GSWIP driver");
MODULE_LICENSE("GPL v2");
