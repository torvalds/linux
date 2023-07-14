// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Jianjun Wang <jianjun.wang@mediatek.com>
 */

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "phy-mtk-io.h"

#define PEXTP_ANA_GLB_00_REG		0x9000
/* Internal Resistor Selection of TX Bias Current */
#define EFUSE_GLB_INTR_SEL		GENMASK(28, 24)

#define PEXTP_ANA_LN0_TRX_REG		0xa000

#define PEXTP_ANA_TX_REG		0x04
/* TX PMOS impedance selection */
#define EFUSE_LN_TX_PMOS_SEL		GENMASK(5, 2)
/* TX NMOS impedance selection */
#define EFUSE_LN_TX_NMOS_SEL		GENMASK(11, 8)

#define PEXTP_ANA_RX_REG		0x3c
/* RX impedance selection */
#define EFUSE_LN_RX_SEL			GENMASK(3, 0)

#define PEXTP_ANA_LANE_OFFSET		0x100

/**
 * struct mtk_pcie_lane_efuse - eFuse data for each lane
 * @tx_pmos: TX PMOS impedance selection data
 * @tx_nmos: TX NMOS impedance selection data
 * @rx_data: RX impedance selection data
 * @lane_efuse_supported: software eFuse data is supported for this lane
 */
struct mtk_pcie_lane_efuse {
	u32 tx_pmos;
	u32 tx_nmos;
	u32 rx_data;
	bool lane_efuse_supported;
};

/**
 * struct mtk_pcie_phy_data - phy data for each SoC
 * @num_lanes: supported lane numbers
 * @sw_efuse_supported: support software to load eFuse data
 */
struct mtk_pcie_phy_data {
	int num_lanes;
	bool sw_efuse_supported;
};

/**
 * struct mtk_pcie_phy - PCIe phy driver main structure
 * @dev: pointer to device
 * @phy: pointer to generic phy
 * @sif_base: IO mapped register base address of system interface
 * @data: pointer to SoC dependent data
 * @sw_efuse_en: software eFuse enable status
 * @efuse_glb_intr: internal resistor selection of TX bias current data
 * @efuse: pointer to eFuse data for each lane
 */
struct mtk_pcie_phy {
	struct device *dev;
	struct phy *phy;
	void __iomem *sif_base;
	const struct mtk_pcie_phy_data *data;

	bool sw_efuse_en;
	u32 efuse_glb_intr;
	struct mtk_pcie_lane_efuse *efuse;
};

static void mtk_pcie_efuse_set_lane(struct mtk_pcie_phy *pcie_phy,
				    unsigned int lane)
{
	struct mtk_pcie_lane_efuse *data = &pcie_phy->efuse[lane];
	void __iomem *addr;

	if (!data->lane_efuse_supported)
		return;

	addr = pcie_phy->sif_base + PEXTP_ANA_LN0_TRX_REG +
	       lane * PEXTP_ANA_LANE_OFFSET;

	mtk_phy_update_field(addr + PEXTP_ANA_TX_REG, EFUSE_LN_TX_PMOS_SEL,
			     data->tx_pmos);

	mtk_phy_update_field(addr + PEXTP_ANA_TX_REG, EFUSE_LN_TX_NMOS_SEL,
			     data->tx_nmos);

	mtk_phy_update_field(addr + PEXTP_ANA_RX_REG, EFUSE_LN_RX_SEL,
			     data->rx_data);
}

/**
 * mtk_pcie_phy_init() - Initialize the phy
 * @phy: the phy to be initialized
 *
 * Initialize the phy by setting the efuse data.
 * The hardware settings will be reset during suspend, it should be
 * reinitialized when the consumer calls phy_init() again on resume.
 */
static int mtk_pcie_phy_init(struct phy *phy)
{
	struct mtk_pcie_phy *pcie_phy = phy_get_drvdata(phy);
	int i;

	if (!pcie_phy->sw_efuse_en)
		return 0;

	/* Set global data */
	mtk_phy_update_field(pcie_phy->sif_base + PEXTP_ANA_GLB_00_REG,
			     EFUSE_GLB_INTR_SEL, pcie_phy->efuse_glb_intr);

	for (i = 0; i < pcie_phy->data->num_lanes; i++)
		mtk_pcie_efuse_set_lane(pcie_phy, i);

	return 0;
}

static const struct phy_ops mtk_pcie_phy_ops = {
	.init	= mtk_pcie_phy_init,
	.owner	= THIS_MODULE,
};

static int mtk_pcie_efuse_read_for_lane(struct mtk_pcie_phy *pcie_phy,
					unsigned int lane)
{
	struct mtk_pcie_lane_efuse *efuse = &pcie_phy->efuse[lane];
	struct device *dev = pcie_phy->dev;
	char efuse_id[16];
	int ret;

	snprintf(efuse_id, sizeof(efuse_id), "tx_ln%d_pmos", lane);
	ret = nvmem_cell_read_variable_le_u32(dev, efuse_id, &efuse->tx_pmos);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read %s\n", efuse_id);

	snprintf(efuse_id, sizeof(efuse_id), "tx_ln%d_nmos", lane);
	ret = nvmem_cell_read_variable_le_u32(dev, efuse_id, &efuse->tx_nmos);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read %s\n", efuse_id);

	snprintf(efuse_id, sizeof(efuse_id), "rx_ln%d", lane);
	ret = nvmem_cell_read_variable_le_u32(dev, efuse_id, &efuse->rx_data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read %s\n", efuse_id);

	if (!(efuse->tx_pmos || efuse->tx_nmos || efuse->rx_data))
		return dev_err_probe(dev, -EINVAL,
				     "No eFuse data found for lane%d, but dts enable it\n",
				     lane);

	efuse->lane_efuse_supported = true;

	return 0;
}

static int mtk_pcie_read_efuse(struct mtk_pcie_phy *pcie_phy)
{
	struct device *dev = pcie_phy->dev;
	bool nvmem_enabled;
	int ret, i;

	/* nvmem data is optional */
	nvmem_enabled = device_property_present(dev, "nvmem-cells");
	if (!nvmem_enabled)
		return 0;

	ret = nvmem_cell_read_variable_le_u32(dev, "glb_intr",
					      &pcie_phy->efuse_glb_intr);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read glb_intr\n");

	pcie_phy->sw_efuse_en = true;

	pcie_phy->efuse = devm_kzalloc(dev, pcie_phy->data->num_lanes *
				       sizeof(*pcie_phy->efuse), GFP_KERNEL);
	if (!pcie_phy->efuse)
		return -ENOMEM;

	for (i = 0; i < pcie_phy->data->num_lanes; i++) {
		ret = mtk_pcie_efuse_read_for_lane(pcie_phy, i);
		if (ret)
			return ret;
	}

	return 0;
}

static int mtk_pcie_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy_provider *provider;
	struct mtk_pcie_phy *pcie_phy;
	int ret;

	pcie_phy = devm_kzalloc(dev, sizeof(*pcie_phy), GFP_KERNEL);
	if (!pcie_phy)
		return -ENOMEM;

	pcie_phy->sif_base = devm_platform_ioremap_resource_byname(pdev, "sif");
	if (IS_ERR(pcie_phy->sif_base))
		return dev_err_probe(dev, PTR_ERR(pcie_phy->sif_base),
				     "Failed to map phy-sif base\n");

	pcie_phy->phy = devm_phy_create(dev, dev->of_node, &mtk_pcie_phy_ops);
	if (IS_ERR(pcie_phy->phy))
		return dev_err_probe(dev, PTR_ERR(pcie_phy->phy),
				     "Failed to create PCIe phy\n");

	pcie_phy->dev = dev;
	pcie_phy->data = of_device_get_match_data(dev);
	if (!pcie_phy->data)
		return dev_err_probe(dev, -EINVAL, "Failed to get phy data\n");

	if (pcie_phy->data->sw_efuse_supported) {
		/*
		 * Failed to read the efuse data is not a fatal problem,
		 * ignore the failure and keep going.
		 */
		ret = mtk_pcie_read_efuse(pcie_phy);
		if (ret == -EPROBE_DEFER || ret == -ENOMEM)
			return ret;
	}

	phy_set_drvdata(pcie_phy->phy, pcie_phy);

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(provider))
		return dev_err_probe(dev, PTR_ERR(provider),
				     "PCIe phy probe failed\n");

	return 0;
}

static const struct mtk_pcie_phy_data mt8195_data = {
	.num_lanes = 2,
	.sw_efuse_supported = true,
};

static const struct of_device_id mtk_pcie_phy_of_match[] = {
	{ .compatible = "mediatek,mt8195-pcie-phy", .data = &mt8195_data },
	{ },
};
MODULE_DEVICE_TABLE(of, mtk_pcie_phy_of_match);

static struct platform_driver mtk_pcie_phy_driver = {
	.probe	= mtk_pcie_phy_probe,
	.driver	= {
		.name = "mtk-pcie-phy",
		.of_match_table = mtk_pcie_phy_of_match,
	},
};
module_platform_driver(mtk_pcie_phy_driver);

MODULE_DESCRIPTION("MediaTek PCIe PHY driver");
MODULE_AUTHOR("Jianjun Wang <jianjun.wang@mediatek.com>");
MODULE_LICENSE("GPL");
