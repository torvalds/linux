// SPDX-License-Identifier: GPL-2.0-only
/*Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.*/
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/phy.h>
#include <linux/regulator/consumer.h>

#include "stmmac.h"
#include "dwmac-qcom-ethqos.h"

#define EMAC_GDSC_EMAC_NAME "gdsc_emac"
#define EMAC_VREG_RGMII_NAME "vreg_rgmii"
#define EMAC_VREG_EMAC_PHY_NAME "vreg_emac_phy"
#define EMAC_VREG_RGMII_IO_PADS_NAME "vreg_rgmii_io_pads"

int ethqos_init_reqgulators(struct qcom_ethqos *ethqos)
{
	int ret = 0;

	if (of_property_read_bool(ethqos->pdev->dev.of_node,
				  "gdsc_emac-supply")) {
		ethqos->gdsc_emac =
		devm_regulator_get(&ethqos->pdev->dev, EMAC_GDSC_EMAC_NAME);
		if (IS_ERR(ethqos->gdsc_emac)) {
			ETHQOSERR("Can not get <%s>\n", EMAC_GDSC_EMAC_NAME);
			return PTR_ERR(ethqos->gdsc_emac);
		}

		ret = regulator_enable(ethqos->gdsc_emac);
		if (ret) {
			ETHQOSERR("Can not enable <%s>\n", EMAC_GDSC_EMAC_NAME);
			goto reg_error;
		}

		ETHQOSDBG("Enabled <%s>\n", EMAC_GDSC_EMAC_NAME);
	}

	if (of_property_read_bool(ethqos->pdev->dev.of_node,
				  "vreg_rgmii-supply")) {
		ethqos->reg_rgmii =
		devm_regulator_get(&ethqos->pdev->dev, EMAC_VREG_RGMII_NAME);
		if (IS_ERR(ethqos->reg_rgmii)) {
			ETHQOSERR("Can not get <%s>\n", EMAC_VREG_RGMII_NAME);
			return PTR_ERR(ethqos->reg_rgmii);
		}

		ret = regulator_enable(ethqos->reg_rgmii);
		if (ret) {
			ETHQOSERR("Can not enable <%s>\n",
				  EMAC_VREG_RGMII_NAME);
			goto reg_error;
		}

		ETHQOSDBG("Enabled <%s>\n", EMAC_VREG_RGMII_NAME);
	}

	if (of_property_read_bool(ethqos->pdev->dev.of_node,
				  "vreg_emac_phy-supply")) {
		ethqos->reg_emac_phy =
		devm_regulator_get(&ethqos->pdev->dev, EMAC_VREG_EMAC_PHY_NAME);
		if (IS_ERR(ethqos->reg_emac_phy)) {
			ETHQOSERR("Can not get <%s>\n",
				  EMAC_VREG_EMAC_PHY_NAME);
			return PTR_ERR(ethqos->reg_emac_phy);
		}

		ret = regulator_enable(ethqos->reg_emac_phy);
		if (ret) {
			ETHQOSERR("Can not enable <%s>\n",
				  EMAC_VREG_EMAC_PHY_NAME);
			goto reg_error;
		}

		ETHQOSDBG("Enabled <%s>\n", EMAC_VREG_EMAC_PHY_NAME);
	}

	if (of_property_read_bool(ethqos->pdev->dev.of_node,
				  "vreg_rgmii_io_pads-supply")) {
		ethqos->reg_rgmii_io_pads = devm_regulator_get
		(&ethqos->pdev->dev, EMAC_VREG_RGMII_IO_PADS_NAME);
		if (IS_ERR(ethqos->reg_rgmii_io_pads)) {
			ETHQOSERR("Can not get <%s>\n",
				  EMAC_VREG_RGMII_IO_PADS_NAME);
			return PTR_ERR(ethqos->reg_rgmii_io_pads);
		}

		ret = regulator_enable(ethqos->reg_rgmii_io_pads);
		if (ret) {
			ETHQOSERR("Can not enable <%s>\n",
				  EMAC_VREG_RGMII_IO_PADS_NAME);
			goto reg_error;
		}

		ETHQOSDBG("Enabled <%s>\n", EMAC_VREG_RGMII_IO_PADS_NAME);
	}

	return ret;

reg_error:
	ETHQOSERR("%s failed\n", __func__);
	ethqos_disable_regulators(ethqos);
	return ret;
}

void ethqos_disable_regulators(struct qcom_ethqos *ethqos)
{
	if (ethqos->reg_rgmii) {
		regulator_disable(ethqos->reg_rgmii);
		ethqos->reg_rgmii = NULL;
	}

	if (ethqos->reg_emac_phy) {
		regulator_disable(ethqos->reg_emac_phy);
		ethqos->reg_emac_phy = NULL;
	}

	if (ethqos->reg_rgmii_io_pads) {
		regulator_disable(ethqos->reg_rgmii_io_pads);
		ethqos->reg_rgmii_io_pads = NULL;
	}

	if (ethqos->gdsc_emac) {
		regulator_disable(ethqos->gdsc_emac);
		ethqos->gdsc_emac = NULL;
	}
}
