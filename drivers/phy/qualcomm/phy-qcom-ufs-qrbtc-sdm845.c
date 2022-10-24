// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016, 2019-2021, Linux Foundation. All rights reserved.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "phy-qcom-ufs-qrbtc-sdm845.h"

#define UFS_PHY_NAME "ufs_phy_qrbtc_sdm845"

static
int ufs_qcom_phy_qrbtc_sdm845_phy_calibrate(struct phy *generic_phy)
{
	int err;
	int tbl_size_A, tbl_size_B;
	struct ufs_qcom_phy_calibration *tbl_A, *tbl_B;
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(generic_phy);
	bool is_rate_B;

	tbl_A = phy_cal_table_rate_A;
	tbl_size_A = ARRAY_SIZE(phy_cal_table_rate_A);

	tbl_size_B = ARRAY_SIZE(phy_cal_table_rate_B);
	tbl_B = phy_cal_table_rate_B;

	is_rate_B = (ufs_qcom_phy->mode == PHY_MODE_UFS_HS_B) ? true : false;

	err = ufs_qcom_phy_calibrate(ufs_qcom_phy,
				     tbl_A, tbl_size_A,
				     tbl_B, tbl_size_B,
				     is_rate_B);

	if (err)
		dev_err(ufs_qcom_phy->dev,
			"%s: ufs_qcom_phy_calibrate() failed %d\n",
			__func__, err);

	return err;
}

static int
ufs_qcom_phy_qrbtc_sdm845_is_pcs_ready(struct ufs_qcom_phy *phy_common)
{
	int err = 0;
	u32 val;

	/*
	 * The value we are polling for is 0x3D which represents the
	 * following masks:
	 * RESET_SM field: 0x5
	 * RESTRIMDONE bit: BIT(3)
	 * PLLLOCK bit: BIT(4)
	 * READY bit: BIT(5)
	 */
	#define QSERDES_COM_RESET_SM_REG_POLL_VAL	0x3D
	err = readl_poll_timeout(phy_common->mmio + QSERDES_COM_RESET_SM,
		val, (val == QSERDES_COM_RESET_SM_REG_POLL_VAL), 10, 1000000);

	if (err)
		dev_err(phy_common->dev, "%s: poll for pcs failed err = %d\n",
			__func__, err);

	return err;
}

static void ufs_qcom_phy_qrbtc_sdm845_start_serdes(struct ufs_qcom_phy *phy)
{
	u32 temp;

	writel_relaxed(0x01, phy->mmio + UFS_PHY_POWER_DOWN_CONTROL);

	temp = readl_relaxed(phy->mmio + UFS_PHY_PHY_START);
	temp |= 0x1;
	writel_relaxed(temp, phy->mmio + UFS_PHY_PHY_START);

	/* Ensure register value is committed */
	mb();
}

static int ufs_qcom_phy_qrbtc_sdm845_init(struct phy *generic_phy)
{
	struct ufs_qcom_phy *phy_common = get_ufs_qcom_phy(generic_phy);
	int ret;

	ret = ufs_qcom_phy_get_reset(phy_common);
	if (ret)
		dev_err(phy_common->dev, "Failed to get reset control %d\n", ret);

	return ret;
}

static int ufs_qcom_phy_qrbtc_sdm845_exit(struct phy *generic_phy)
{
	return 0;
}

static
int ufs_qcom_phy_qrbtc_sdm845_set_mode(struct phy *generic_phy,
				   enum phy_mode mode, int submode)
{
	struct ufs_qcom_phy *phy_common = get_ufs_qcom_phy(generic_phy);

	phy_common->mode = PHY_MODE_INVALID;

	if (mode > 0)
		phy_common->mode = mode;

	phy_common->submode = submode;

	return 0;
}

static const struct phy_ops ufs_qcom_phy_qrbtc_sdm845_phy_ops = {
	.init		= ufs_qcom_phy_qrbtc_sdm845_init,
	.exit		= ufs_qcom_phy_qrbtc_sdm845_exit,
	.set_mode	= ufs_qcom_phy_qrbtc_sdm845_set_mode,
	.calibrate	= ufs_qcom_phy_qrbtc_sdm845_phy_calibrate,
	.owner		= THIS_MODULE,
};

static struct ufs_qcom_phy_specific_ops phy_qrbtc_sdm845_ops = {
	.start_serdes		= ufs_qcom_phy_qrbtc_sdm845_start_serdes,
	.is_physical_coding_sublayer_ready =
				ufs_qcom_phy_qrbtc_sdm845_is_pcs_ready,
};

static int ufs_qcom_phy_qrbtc_sdm845_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy *generic_phy;
	struct ufs_qcom_phy_qrbtc_sdm845 *phy;
	int err = 0;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy) {
		err = -ENOMEM;
		goto out;
	}

	generic_phy = ufs_qcom_phy_generic_probe(pdev, &phy->common_cfg,
		&ufs_qcom_phy_qrbtc_sdm845_phy_ops, &phy_qrbtc_sdm845_ops);

	if (!generic_phy) {
		dev_err(dev, "%s: ufs_qcom_phy_generic_probe() failed\n",
			__func__);
		err = -EIO;
		goto out;
	}

	phy_set_drvdata(generic_phy, phy);

	strscpy(phy->common_cfg.name, UFS_PHY_NAME,
		sizeof(phy->common_cfg.name));

out:
	return err;
}

static const struct of_device_id ufs_qcom_phy_qrbtc_sdm845_of_match[] = {
	{.compatible = "qcom,ufs-phy-qrbtc-sdm845"},
	{},
};
MODULE_DEVICE_TABLE(of, ufs_qcom_phy_qrbtc_sdm845_of_match);

static struct platform_driver ufs_qcom_phy_qrbtc_sdm845_driver = {
	.probe = ufs_qcom_phy_qrbtc_sdm845_probe,
	.driver = {
		.of_match_table = ufs_qcom_phy_qrbtc_sdm845_of_match,
		.name = "ufs_qcom_phy_qrbtc_sdm845",
	},
};

module_platform_driver(ufs_qcom_phy_qrbtc_sdm845_driver);

MODULE_DESCRIPTION("Universal Flash Storage (UFS) QCOM PHY QRBTC SDM845");
MODULE_LICENSE("GPL v2");
