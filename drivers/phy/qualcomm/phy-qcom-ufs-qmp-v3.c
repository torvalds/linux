// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "phy-qcom-ufs-qmp-v3.h"

#define UFS_PHY_NAME "ufs_phy_qmp_v3"

static inline void ufs_qcom_phy_qmp_v3_start_serdes(struct ufs_qcom_phy *phy);
static int ufs_qcom_phy_qmp_v3_is_pcs_ready(struct ufs_qcom_phy *phy_common);

static int ufs_qcom_phy_qmp_v3_phy_calibrate(struct phy *generic_phy)
{
	struct ufs_qcom_phy *ufs_qcom_phy = get_ufs_qcom_phy(generic_phy);
	struct device *dev = ufs_qcom_phy->dev;
	bool is_rate_B;
	int err;

	err = reset_control_assert(ufs_qcom_phy->ufs_reset);
	if (err) {
		dev_err(dev, "Failed to assert UFS PHY reset %d\n", err);
		goto out;
	}

	is_rate_B = (ufs_qcom_phy->mode == PHY_MODE_UFS_HS_B) ? true : false;

	/*
	 * Writing PHY calibration in this order:
	 * 1. Write Rate-A calibration first (1-lane mode).
	 * 2. Write 2nd lane configuration if needed.
	 * 3. Write Rate-B calibration overrides
	 */
	ufs_qcom_phy_write_tbl(ufs_qcom_phy, phy_cal_table_rate_A,
		ARRAY_SIZE(phy_cal_table_rate_A));
	if (ufs_qcom_phy->lanes_per_direction == 2)
		ufs_qcom_phy_write_tbl(ufs_qcom_phy,
				phy_cal_table_2nd_lane,
				ARRAY_SIZE(phy_cal_table_2nd_lane));

	if (is_rate_B)
		ufs_qcom_phy_write_tbl(ufs_qcom_phy, phy_cal_table_rate_B,
				       ARRAY_SIZE(phy_cal_table_rate_B));

	/* flush buffered writes */
	mb();

	err = reset_control_deassert(ufs_qcom_phy->ufs_reset);
	if (err) {
		dev_err(dev, "Failed to deassert UFS PHY reset %d\n", err);
		goto out;
	}

	ufs_qcom_phy_qmp_v3_start_serdes(ufs_qcom_phy);

	err = ufs_qcom_phy_qmp_v3_is_pcs_ready(ufs_qcom_phy);

out:
	return err;
}

static int ufs_qcom_phy_qmp_v3_init(struct phy *generic_phy)
{
	struct ufs_qcom_phy_qmp_v3 *phy = phy_get_drvdata(generic_phy);
	struct ufs_qcom_phy *phy_common = &phy->common_cfg;
	int err;

	err = ufs_qcom_phy_init_clks(phy_common);
	if (err) {
		dev_err(phy_common->dev, "%s: ufs_qcom_phy_init_clks() failed %d\n",
			__func__, err);
		goto out;
	}

	err = ufs_qcom_phy_init_vregulators(phy_common);
	if (err) {
		dev_err(phy_common->dev, "%s: ufs_qcom_phy_init_vregulators() failed %d\n",
			__func__, err);
		goto out;
	}

	/* Optional */
	ufs_qcom_phy_get_reset(phy_common);

out:
	return err;
}

static int ufs_qcom_phy_qmp_v3_exit(struct phy *generic_phy)
{
	return 0;
}

static
int ufs_qcom_phy_qmp_v3_set_mode(struct phy *generic_phy,
				   enum phy_mode mode, int submode)
{
	struct ufs_qcom_phy *phy_common = get_ufs_qcom_phy(generic_phy);

	phy_common->mode = PHY_MODE_INVALID;

	if (mode > 0)
		phy_common->mode = mode;

	phy_common->submode = submode;

	return 0;
}
static
void ufs_qcom_phy_qmp_v3_power_control(struct ufs_qcom_phy *phy,
					 bool power_ctrl)
{
	if (!power_ctrl) {
		/* apply analog power collapse */
		writel_relaxed(0x0, phy->mmio + UFS_PHY_POWER_DOWN_CONTROL);
		/*
		 * Make sure that PHY knows its analog rail is going to be
		 * powered OFF.
		 */
		mb();
	} else {
		/* bring PHY out of analog power collapse */
		writel_relaxed(0x1, phy->mmio + UFS_PHY_POWER_DOWN_CONTROL);

		/*
		 * Before any transactions involving PHY, ensure PHY knows
		 * that it's analog rail is powered ON.
		 */
		mb();
	}
}

static inline
void ufs_qcom_phy_qmp_v3_set_tx_lane_enable(struct ufs_qcom_phy *phy, u32 val)
{
	/*
	 * v3 PHY does not have TX_LANE_ENABLE register.
	 * Implement this function so as not to propagate error to caller.
	 */
}

static
void ufs_qcom_phy_qmp_v3_ctrl_rx_linecfg(struct ufs_qcom_phy *phy, bool ctrl)
{
	u32 temp;

	temp = readl_relaxed(phy->mmio + UFS_PHY_LINECFG_DISABLE);

	if (ctrl) /* enable RX LineCfg */
		temp &= ~UFS_PHY_RX_LINECFG_DISABLE_BIT;
	else /* disable RX LineCfg */
		temp |= UFS_PHY_RX_LINECFG_DISABLE_BIT;

	writel_relaxed(temp, phy->mmio + UFS_PHY_LINECFG_DISABLE);
	/* make sure that RX LineCfg config applied before we return */
	mb();
}

static inline void ufs_qcom_phy_qmp_v3_start_serdes(struct ufs_qcom_phy *phy)
{
	u32 tmp;

	tmp = readl_relaxed(phy->mmio + UFS_PHY_PHY_START);
	tmp &= ~MASK_SERDES_START;
	tmp |= (1 << OFFSET_SERDES_START);
	writel_relaxed(tmp, phy->mmio + UFS_PHY_PHY_START);
	/* Ensure register value is committed */
	mb();
}

static int ufs_qcom_phy_qmp_v3_is_pcs_ready(struct ufs_qcom_phy *phy_common)
{
	int err = 0;
	u32 val;

	err = readl_poll_timeout(phy_common->mmio + UFS_PHY_PCS_READY_STATUS,
		val, (val & MASK_PCS_READY), 10, 1000000);
	if (err) {
		dev_err(phy_common->dev, "%s: poll for pcs failed err = %d\n",
			__func__, err);
		goto out;
	}

out:
	return err;
}

static void ufs_qcom_phy_qmp_v3_dbg_register_dump(struct ufs_qcom_phy *phy)
{
	ufs_qcom_phy_dump_regs(phy, COM_BASE, COM_SIZE,
					"PHY QSERDES COM Registers ");
	ufs_qcom_phy_dump_regs(phy, PHY_BASE, PHY_SIZE,
					"PHY Registers ");
	ufs_qcom_phy_dump_regs(phy, RX_BASE(0), RX_SIZE,
					"PHY RX0 Registers ");
	ufs_qcom_phy_dump_regs(phy, TX_BASE(0), TX_SIZE,
					"PHY TX0 Registers ");
	ufs_qcom_phy_dump_regs(phy, RX_BASE(1), RX_SIZE,
					"PHY RX1 Registers ");
	ufs_qcom_phy_dump_regs(phy, TX_BASE(1), TX_SIZE,
					"PHY TX1 Registers ");
}

static const struct phy_ops ufs_qcom_phy_qmp_v3_phy_ops = {
	.init		= ufs_qcom_phy_qmp_v3_init,
	.exit		= ufs_qcom_phy_qmp_v3_exit,
	.power_on	= ufs_qcom_phy_power_on,
	.power_off	= ufs_qcom_phy_power_off,
	.set_mode	= ufs_qcom_phy_qmp_v3_set_mode,
	.calibrate	= ufs_qcom_phy_qmp_v3_phy_calibrate,
	.owner		= THIS_MODULE,
};

static struct ufs_qcom_phy_specific_ops phy_v3_ops = {
	.start_serdes		= ufs_qcom_phy_qmp_v3_start_serdes,
	.is_physical_coding_sublayer_ready = ufs_qcom_phy_qmp_v3_is_pcs_ready,
	.set_tx_lane_enable	= ufs_qcom_phy_qmp_v3_set_tx_lane_enable,
	.ctrl_rx_linecfg	= ufs_qcom_phy_qmp_v3_ctrl_rx_linecfg,
	.power_control		= ufs_qcom_phy_qmp_v3_power_control,
	.dbg_register_dump	= ufs_qcom_phy_qmp_v3_dbg_register_dump,
};

static int ufs_qcom_phy_qmp_v3_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy *generic_phy;
	struct ufs_qcom_phy_qmp_v3 *phy;
	int err = 0;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy) {
		err = -ENOMEM;
		goto out;
	}

	generic_phy = ufs_qcom_phy_generic_probe(pdev, &phy->common_cfg,
				&ufs_qcom_phy_qmp_v3_phy_ops, &phy_v3_ops);

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

static const struct of_device_id ufs_qcom_phy_qmp_v3_of_match[] = {
	{.compatible = "qcom,ufs-phy-qmp-v3"},
	{},
};
MODULE_DEVICE_TABLE(of, ufs_qcom_phy_qmp_v3_of_match);

static struct platform_driver ufs_qcom_phy_qmp_v3_driver = {
	.probe = ufs_qcom_phy_qmp_v3_probe,
	.driver = {
		.of_match_table = ufs_qcom_phy_qmp_v3_of_match,
		.name = "ufs_qcom_phy_qmp_v3",
	},
};

module_platform_driver(ufs_qcom_phy_qmp_v3_driver);

MODULE_DESCRIPTION("Universal Flash Storage (UFS) QCOM PHY QMP v3");
MODULE_LICENSE("GPL");
