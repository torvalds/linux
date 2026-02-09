// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 *
 * Authors: Sai Krishna Potthuri <sai.krishna.potthuri@amd.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/irqreturn.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <ufs/unipro.h>

#include "ufshcd-dwc.h"
#include "ufshcd-pltfrm.h"
#include "ufshci-dwc.h"

/* PHY modes */
#define UFSHCD_DWC_PHY_MODE_ROM         0

#define MPHY_FAST_RX_AFE_CAL		BIT(2)
#define MPHY_FW_CALIB_CFG_VAL		BIT(8)

#define MPHY_RX_OVRD_EN			BIT(3)
#define MPHY_RX_OVRD_VAL		BIT(2)
#define MPHY_RX_ACK_MASK		BIT(0)

#define TIMEOUT_MICROSEC	1000000

struct ufs_versal2_host {
	struct ufs_hba *hba;
	struct reset_control *rstc;
	struct reset_control *rstphy;
	u32 phy_mode;
	unsigned long host_clk;
	u8 attcompval0;
	u8 attcompval1;
	u8 ctlecompval0;
	u8 ctlecompval1;
};

static int ufs_versal2_phy_reg_write(struct ufs_hba *hba, u32 addr, u32 val)
{
	static struct ufshcd_dme_attr_val phy_write_attrs[] = {
		{ UIC_ARG_MIB(CBCREGADDRLSB), 0, DME_LOCAL },
		{ UIC_ARG_MIB(CBCREGADDRMSB), 0, DME_LOCAL },
		{ UIC_ARG_MIB(CBCREGWRLSB), 0, DME_LOCAL },
		{ UIC_ARG_MIB(CBCREGWRMSB), 0, DME_LOCAL },
		{ UIC_ARG_MIB(CBCREGRDWRSEL), 1, DME_LOCAL },
		{ UIC_ARG_MIB(VS_MPHYCFGUPDT), 1, DME_LOCAL }
	};

	phy_write_attrs[0].mib_val = (u8)addr;
	phy_write_attrs[1].mib_val = (u8)(addr >> 8);
	phy_write_attrs[2].mib_val = (u8)val;
	phy_write_attrs[3].mib_val = (u8)(val >> 8);

	return ufshcd_dwc_dme_set_attrs(hba, phy_write_attrs, ARRAY_SIZE(phy_write_attrs));
}

static int ufs_versal2_phy_reg_read(struct ufs_hba *hba, u32 addr, u32 *val)
{
	u32 mib_val;
	int ret;
	static struct ufshcd_dme_attr_val phy_read_attrs[] = {
		{ UIC_ARG_MIB(CBCREGADDRLSB), 0, DME_LOCAL },
		{ UIC_ARG_MIB(CBCREGADDRMSB), 0, DME_LOCAL },
		{ UIC_ARG_MIB(CBCREGRDWRSEL), 0, DME_LOCAL },
		{ UIC_ARG_MIB(VS_MPHYCFGUPDT), 1, DME_LOCAL }
	};

	phy_read_attrs[0].mib_val = (u8)addr;
	phy_read_attrs[1].mib_val = (u8)(addr >> 8);

	ret = ufshcd_dwc_dme_set_attrs(hba, phy_read_attrs, ARRAY_SIZE(phy_read_attrs));
	if (ret)
		return ret;

	ret = ufshcd_dme_get(hba, UIC_ARG_MIB(CBCREGRDLSB), &mib_val);
	if (ret)
		return ret;

	*val = mib_val;
	ret = ufshcd_dme_get(hba, UIC_ARG_MIB(CBCREGRDMSB), &mib_val);
	if (ret)
		return ret;

	*val |= (mib_val << 8);

	return 0;
}

static int ufs_versal2_enable_phy(struct ufs_hba *hba)
{
	u32 offset, reg;
	int ret;

	ret = ufshcd_dme_set(hba, UIC_ARG_MIB(VS_MPHYDISABLE), 0);
	if (ret)
		return ret;

	ret = ufshcd_dme_set(hba, UIC_ARG_MIB(VS_MPHYCFGUPDT), 1);
	if (ret)
		return ret;

	/* Check Tx/Rx FSM states */
	for (offset = 0; offset < 2; offset++) {
		u32 time_left, mibsel;

		time_left = TIMEOUT_MICROSEC;
		mibsel = UIC_ARG_MIB_SEL(MTX_FSM_STATE, UIC_ARG_MPHY_TX_GEN_SEL_INDEX(offset));
		do {
			ret = ufshcd_dme_get(hba, mibsel, &reg);
			if (ret)
				return ret;

			if (reg == TX_STATE_HIBERN8 || reg == TX_STATE_SLEEP ||
			    reg == TX_STATE_LSBURST)
				break;

			time_left--;
			usleep_range(1, 5);
		} while (time_left);

		if (!time_left) {
			dev_err(hba->dev, "Invalid Tx FSM state.\n");
			return -ETIMEDOUT;
		}

		time_left = TIMEOUT_MICROSEC;
		mibsel = UIC_ARG_MIB_SEL(MRX_FSM_STATE, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(offset));
		do {
			ret = ufshcd_dme_get(hba, mibsel, &reg);
			if (ret)
				return ret;

			if (reg == RX_STATE_HIBERN8 || reg == RX_STATE_SLEEP ||
			    reg == RX_STATE_LSBURST)
				break;

			time_left--;
			usleep_range(1, 5);
		} while (time_left);

		if (!time_left) {
			dev_err(hba->dev, "Invalid Rx FSM state.\n");
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static int ufs_versal2_setup_phy(struct ufs_hba *hba)
{
	struct ufs_versal2_host *host = ufshcd_get_variant(hba);
	int ret;
	u32 reg;

	/* Bypass RX-AFE offset calibrations (ATT/CTLE) */
	ret = ufs_versal2_phy_reg_read(hba, FAST_FLAGS(0), &reg);
	if (ret)
		return ret;

	reg |= MPHY_FAST_RX_AFE_CAL;
	ret = ufs_versal2_phy_reg_write(hba, FAST_FLAGS(0), reg);
	if (ret)
		return ret;

	ret = ufs_versal2_phy_reg_read(hba, FAST_FLAGS(1), &reg);
	if (ret)
		return ret;

	reg |= MPHY_FAST_RX_AFE_CAL;
	ret = ufs_versal2_phy_reg_write(hba, FAST_FLAGS(1), reg);
	if (ret)
		return ret;

	/* Program ATT and CTLE compensation values */
	if (host->attcompval0) {
		ret = ufs_versal2_phy_reg_write(hba, RX_AFE_ATT_IDAC(0), host->attcompval0);
		if (ret)
			return ret;
	}

	if (host->attcompval1) {
		ret = ufs_versal2_phy_reg_write(hba, RX_AFE_ATT_IDAC(1), host->attcompval1);
		if (ret)
			return ret;
	}

	if (host->ctlecompval0) {
		ret = ufs_versal2_phy_reg_write(hba, RX_AFE_CTLE_IDAC(0), host->ctlecompval0);
		if (ret)
			return ret;
	}

	if (host->ctlecompval1) {
		ret = ufs_versal2_phy_reg_write(hba, RX_AFE_CTLE_IDAC(1), host->ctlecompval1);
		if (ret)
			return ret;
	}

	ret = ufs_versal2_phy_reg_read(hba, FW_CALIB_CCFG(0), &reg);
	if (ret)
		return ret;

	reg |= MPHY_FW_CALIB_CFG_VAL;
	ret = ufs_versal2_phy_reg_write(hba, FW_CALIB_CCFG(0), reg);
	if (ret)
		return ret;

	ret = ufs_versal2_phy_reg_read(hba, FW_CALIB_CCFG(1), &reg);
	if (ret)
		return ret;

	reg |= MPHY_FW_CALIB_CFG_VAL;
	return ufs_versal2_phy_reg_write(hba, FW_CALIB_CCFG(1), reg);
}

static int ufs_versal2_phy_init(struct ufs_hba *hba)
{
	struct ufs_versal2_host *host = ufshcd_get_variant(hba);
	u32 time_left;
	bool is_ready;
	int ret;
	static const struct ufshcd_dme_attr_val rmmi_attrs[] = {
		{ UIC_ARG_MIB(CBREFCLKCTRL2), CBREFREFCLK_GATE_OVR_EN, DME_LOCAL },
		{ UIC_ARG_MIB(CBCRCTRL), 1, DME_LOCAL },
		{ UIC_ARG_MIB(CBC10DIRECTCONF2), 1, DME_LOCAL },
		{ UIC_ARG_MIB(VS_MPHYCFGUPDT), 1, DME_LOCAL }
	};

	/* Wait for Tx/Rx config_rdy */
	time_left = TIMEOUT_MICROSEC;
	do {
		time_left--;
		ret = zynqmp_pm_is_mphy_tx_rx_config_ready(&is_ready);
		if (ret)
			return ret;

		if (!is_ready)
			break;

		usleep_range(1, 5);
	} while (time_left);

	if (!time_left) {
		dev_err(hba->dev, "Tx/Rx configuration signal busy.\n");
		return -ETIMEDOUT;
	}

	ret = ufshcd_dwc_dme_set_attrs(hba, rmmi_attrs, ARRAY_SIZE(rmmi_attrs));
	if (ret)
		return ret;

	ret = reset_control_deassert(host->rstphy);
	if (ret) {
		dev_err(hba->dev, "ufsphy reset deassert failed, err = %d\n", ret);
		return ret;
	}

	/* Wait for SRAM init done */
	time_left = TIMEOUT_MICROSEC;
	do {
		time_left--;
		ret = zynqmp_pm_is_sram_init_done(&is_ready);
		if (ret)
			return ret;

		if (is_ready)
			break;

		usleep_range(1, 5);
	} while (time_left);

	if (!time_left) {
		dev_err(hba->dev, "SRAM initialization failed.\n");
		return -ETIMEDOUT;
	}

	ret = ufs_versal2_setup_phy(hba);
	if (ret)
		return ret;

	return ufs_versal2_enable_phy(hba);
}

static int ufs_versal2_init(struct ufs_hba *hba)
{
	struct ufs_versal2_host *host;
	struct device *dev = hba->dev;
	struct ufs_clk_info *clki;
	int ret;
	u32 cal;

	host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->hba = hba;
	ufshcd_set_variant(hba, host);

	host->phy_mode = UFSHCD_DWC_PHY_MODE_ROM;

	list_for_each_entry(clki, &hba->clk_list_head, list) {
		if (!strcmp(clki->name, "core"))
			host->host_clk = clk_get_rate(clki->clk);
	}

	host->rstc = devm_reset_control_get_exclusive(dev, "host");
	if (IS_ERR(host->rstc)) {
		dev_err(dev, "failed to get reset ctrl: host\n");
		return PTR_ERR(host->rstc);
	}

	host->rstphy = devm_reset_control_get_exclusive(dev, "phy");
	if (IS_ERR(host->rstphy)) {
		dev_err(dev, "failed to get reset ctrl: phy\n");
		return PTR_ERR(host->rstphy);
	}

	ret = reset_control_assert(host->rstc);
	if (ret) {
		dev_err(hba->dev, "host reset assert failed, err = %d\n", ret);
		return ret;
	}

	ret = reset_control_assert(host->rstphy);
	if (ret) {
		dev_err(hba->dev, "phy reset assert failed, err = %d\n", ret);
		return ret;
	}

	ret = zynqmp_pm_set_sram_bypass();
	if (ret) {
		dev_err(dev, "Bypass SRAM interface failed, err = %d\n", ret);
		return ret;
	}

	ret = reset_control_deassert(host->rstc);
	if (ret)
		dev_err(hba->dev, "host reset deassert failed, err = %d\n", ret);

	ret = zynqmp_pm_get_ufs_calibration_values(&cal);
	if (ret) {
		dev_err(dev, "failed to read calibration values\n");
		return ret;
	}

	host->attcompval0 = (u8)cal;
	host->attcompval1 = (u8)(cal >> 8);
	host->ctlecompval0 = (u8)(cal >> 16);
	host->ctlecompval1 = (u8)(cal >> 24);

	hba->quirks |= UFSHCD_QUIRK_SKIP_DEF_UNIPRO_TIMEOUT_SETTING;

	return 0;
}

static int ufs_versal2_hce_enable_notify(struct ufs_hba *hba,
					 enum ufs_notify_change_status status)
{
	int ret = 0;

	if (status == POST_CHANGE) {
		ret = ufs_versal2_phy_init(hba);
		if (ret)
			dev_err(hba->dev, "Phy init failed (%d)\n", ret);
	}

	return ret;
}

static int ufs_versal2_link_startup_notify(struct ufs_hba *hba,
					   enum ufs_notify_change_status status)
{
	struct ufs_versal2_host *host = ufshcd_get_variant(hba);
	int ret = 0;

	switch (status) {
	case PRE_CHANGE:
		if (host->host_clk)
			ufshcd_writel(hba, host->host_clk / 1000000, DWC_UFS_REG_HCLKDIV);

		break;
	case POST_CHANGE:
		ret = ufshcd_dwc_link_startup_notify(hba, status);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int ufs_versal2_phy_ratesel(struct ufs_hba *hba, u32 activelanes, u32 rx_req)
{
	u32 time_left, reg, lane;
	int ret;

	for (lane = 0; lane < activelanes; lane++) {
		time_left = TIMEOUT_MICROSEC;
		ret = ufs_versal2_phy_reg_read(hba, RX_OVRD_IN_1(lane), &reg);
		if (ret)
			return ret;

		reg |= MPHY_RX_OVRD_EN;
		if (rx_req)
			reg |= MPHY_RX_OVRD_VAL;
		else
			reg &= ~MPHY_RX_OVRD_VAL;

		ret = ufs_versal2_phy_reg_write(hba, RX_OVRD_IN_1(lane), reg);
		if (ret)
			return ret;

		do {
			ret = ufs_versal2_phy_reg_read(hba, RX_PCS_OUT(lane), &reg);
			if (ret)
				return ret;

			reg &= MPHY_RX_ACK_MASK;
			if (reg == rx_req)
				break;

			time_left--;
			usleep_range(1, 5);
		} while (time_left);

		if (!time_left) {
			dev_err(hba->dev, "Invalid Rx Ack value.\n");
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static int ufs_versal2_pwr_change_notify(struct ufs_hba *hba, enum ufs_notify_change_status status,
					 const struct ufs_pa_layer_attr *dev_max_params,
					 struct ufs_pa_layer_attr *dev_req_params)
{
	struct ufs_versal2_host *host = ufshcd_get_variant(hba);
	u32 lane, reg, rate = 0;
	int ret = 0;

	if (status == PRE_CHANGE) {
		memcpy(dev_req_params, dev_max_params, sizeof(struct ufs_pa_layer_attr));

		/* If it is not a calibrated part, switch PWRMODE to SLOW_MODE */
		if (!host->attcompval0 && !host->attcompval1 && !host->ctlecompval0 &&
		    !host->ctlecompval1) {
			dev_req_params->pwr_rx = SLOW_MODE;
			dev_req_params->pwr_tx = SLOW_MODE;
			return 0;
		}

		if (dev_req_params->pwr_rx == SLOW_MODE || dev_req_params->pwr_rx == SLOWAUTO_MODE)
			return 0;

		if (dev_req_params->hs_rate == PA_HS_MODE_B)
			rate = 1;

		 /* Select the rate */
		ret = ufshcd_dme_set(hba, UIC_ARG_MIB(CBRATESEL), rate);
		if (ret)
			return ret;

		ret = ufshcd_dme_set(hba, UIC_ARG_MIB(VS_MPHYCFGUPDT), 1);
		if (ret)
			return ret;

		ret = ufs_versal2_phy_ratesel(hba, dev_req_params->lane_tx, 1);
		if (ret)
			return ret;

		ret = ufs_versal2_phy_ratesel(hba, dev_req_params->lane_tx, 0);
		if (ret)
			return ret;

		/* Remove rx_req override */
		for (lane = 0; lane < dev_req_params->lane_tx; lane++) {
			ret = ufs_versal2_phy_reg_read(hba, RX_OVRD_IN_1(lane), &reg);
			if (ret)
				return ret;

			reg &= ~MPHY_RX_OVRD_EN;
			ret = ufs_versal2_phy_reg_write(hba, RX_OVRD_IN_1(lane), reg);
			if (ret)
				return ret;
		}

		if (dev_req_params->lane_tx == UFS_LANE_2 && dev_req_params->lane_rx == UFS_LANE_2)
			ret = ufshcd_dme_configure_adapt(hba, dev_req_params->gear_tx,
							 PA_INITIAL_ADAPT);
	}

	return ret;
}

static struct ufs_hba_variant_ops ufs_versal2_hba_vops = {
	.name			= "ufs-versal2-pltfm",
	.init			= ufs_versal2_init,
	.link_startup_notify	= ufs_versal2_link_startup_notify,
	.hce_enable_notify	= ufs_versal2_hce_enable_notify,
	.pwr_change_notify	= ufs_versal2_pwr_change_notify,
};

static const struct of_device_id ufs_versal2_pltfm_match[] = {
	{
		.compatible = "amd,versal2-ufs",
		.data = &ufs_versal2_hba_vops,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, ufs_versal2_pltfm_match);

static int ufs_versal2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	/* Perform generic probe */
	ret = ufshcd_pltfrm_init(pdev, &ufs_versal2_hba_vops);
	if (ret)
		dev_err(dev, "ufshcd_pltfrm_init() failed %d\n", ret);

	return ret;
}

static void ufs_versal2_remove(struct platform_device *pdev)
{
	struct ufs_hba *hba = platform_get_drvdata(pdev);

	pm_runtime_get_sync(&(pdev)->dev);
	ufshcd_remove(hba);
}

static const struct dev_pm_ops ufs_versal2_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ufshcd_system_suspend, ufshcd_system_resume)
	SET_RUNTIME_PM_OPS(ufshcd_runtime_suspend, ufshcd_runtime_resume, NULL)
};

static struct platform_driver ufs_versal2_pltfm = {
	.probe		= ufs_versal2_probe,
	.remove		= ufs_versal2_remove,
	.driver		= {
		.name	= "ufshcd-versal2",
		.pm	= &ufs_versal2_pm_ops,
		.of_match_table	= of_match_ptr(ufs_versal2_pltfm_match),
	},
};

module_platform_driver(ufs_versal2_pltfm);

MODULE_AUTHOR("Sai Krishna Potthuri <sai.krishna.potthuri@amd.com>");
MODULE_DESCRIPTION("AMD Versal Gen 2 UFS Host Controller driver");
MODULE_LICENSE("GPL");
