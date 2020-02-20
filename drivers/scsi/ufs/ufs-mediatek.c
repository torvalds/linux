// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 MediaTek Inc.
 * Authors:
 *	Stanley Chu <stanley.chu@mediatek.com>
 *	Peter Wang <peter.wang@mediatek.com>
 */

#include <linux/arm-smccc.h>
#include <linux/bitfield.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/soc/mediatek/mtk_sip_svc.h>

#include "ufshcd.h"
#include "ufshcd-pltfrm.h"
#include "ufs_quirks.h"
#include "unipro.h"
#include "ufs-mediatek.h"

#define ufs_mtk_smc(cmd, val, res) \
	arm_smccc_smc(MTK_SIP_UFS_CONTROL, \
		      cmd, val, 0, 0, 0, 0, 0, &(res))

#define ufs_mtk_ref_clk_notify(on, res) \
	ufs_mtk_smc(UFS_MTK_SIP_REF_CLK_NOTIFICATION, on, res)

#define ufs_mtk_device_reset_ctrl(high, res) \
	ufs_mtk_smc(UFS_MTK_SIP_DEVICE_RESET, high, res)

#define ufs_mtk_unipro_powerdown(hba, powerdown) \
	ufshcd_dme_set(hba, \
		       UIC_ARG_MIB_SEL(VS_UNIPROPOWERDOWNCONTROL, 0), \
		       powerdown)

static void ufs_mtk_cfg_unipro_cg(struct ufs_hba *hba, bool enable)
{
	u32 tmp;

	if (enable) {
		ufshcd_dme_get(hba,
			       UIC_ARG_MIB(VS_SAVEPOWERCONTROL), &tmp);
		tmp = tmp |
		      (1 << RX_SYMBOL_CLK_GATE_EN) |
		      (1 << SYS_CLK_GATE_EN) |
		      (1 << TX_CLK_GATE_EN);
		ufshcd_dme_set(hba,
			       UIC_ARG_MIB(VS_SAVEPOWERCONTROL), tmp);

		ufshcd_dme_get(hba,
			       UIC_ARG_MIB(VS_DEBUGCLOCKENABLE), &tmp);
		tmp = tmp & ~(1 << TX_SYMBOL_CLK_REQ_FORCE);
		ufshcd_dme_set(hba,
			       UIC_ARG_MIB(VS_DEBUGCLOCKENABLE), tmp);
	} else {
		ufshcd_dme_get(hba,
			       UIC_ARG_MIB(VS_SAVEPOWERCONTROL), &tmp);
		tmp = tmp & ~((1 << RX_SYMBOL_CLK_GATE_EN) |
			      (1 << SYS_CLK_GATE_EN) |
			      (1 << TX_CLK_GATE_EN));
		ufshcd_dme_set(hba,
			       UIC_ARG_MIB(VS_SAVEPOWERCONTROL), tmp);

		ufshcd_dme_get(hba,
			       UIC_ARG_MIB(VS_DEBUGCLOCKENABLE), &tmp);
		tmp = tmp | (1 << TX_SYMBOL_CLK_REQ_FORCE);
		ufshcd_dme_set(hba,
			       UIC_ARG_MIB(VS_DEBUGCLOCKENABLE), tmp);
	}
}

static int ufs_mtk_bind_mphy(struct ufs_hba *hba)
{
	struct ufs_mtk_host *host = ufshcd_get_variant(hba);
	struct device *dev = hba->dev;
	struct device_node *np = dev->of_node;
	int err = 0;

	host->mphy = devm_of_phy_get_by_index(dev, np, 0);

	if (host->mphy == ERR_PTR(-EPROBE_DEFER)) {
		/*
		 * UFS driver might be probed before the phy driver does.
		 * In that case we would like to return EPROBE_DEFER code.
		 */
		err = -EPROBE_DEFER;
		dev_info(dev,
			 "%s: required phy hasn't probed yet. err = %d\n",
			__func__, err);
	} else if (IS_ERR(host->mphy)) {
		err = PTR_ERR(host->mphy);
		dev_info(dev, "%s: PHY get failed %d\n", __func__, err);
	}

	if (err)
		host->mphy = NULL;

	return err;
}

static void ufs_mtk_udelay(unsigned long us)
{
	if (!us)
		return;

	if (us < 10)
		udelay(us);
	else
		usleep_range(us, us + 10);
}

static int ufs_mtk_setup_ref_clk(struct ufs_hba *hba, bool on)
{
	struct ufs_mtk_host *host = ufshcd_get_variant(hba);
	struct arm_smccc_res res;
	unsigned long timeout;
	u32 value;

	if (host->ref_clk_enabled == on)
		return 0;

	if (on) {
		ufs_mtk_ref_clk_notify(on, res);
		ufs_mtk_udelay(host->ref_clk_ungating_wait_us);
		ufshcd_writel(hba, REFCLK_REQUEST, REG_UFS_REFCLK_CTRL);
	} else {
		ufshcd_writel(hba, REFCLK_RELEASE, REG_UFS_REFCLK_CTRL);
	}

	/* Wait for ack */
	timeout = jiffies + msecs_to_jiffies(REFCLK_REQ_TIMEOUT_MS);
	do {
		value = ufshcd_readl(hba, REG_UFS_REFCLK_CTRL);

		/* Wait until ack bit equals to req bit */
		if (((value & REFCLK_ACK) >> 1) == (value & REFCLK_REQUEST))
			goto out;

		usleep_range(100, 200);
	} while (time_before(jiffies, timeout));

	dev_err(hba->dev, "missing ack of refclk req, reg: 0x%x\n", value);

	ufs_mtk_ref_clk_notify(host->ref_clk_enabled, res);

	return -ETIMEDOUT;

out:
	host->ref_clk_enabled = on;
	if (!on) {
		ufs_mtk_udelay(host->ref_clk_gating_wait_us);
		ufs_mtk_ref_clk_notify(on, res);
	}

	return 0;
}

static void ufs_mtk_setup_ref_clk_wait_us(struct ufs_hba *hba,
					  u16 gating_us, u16 ungating_us)
{
	struct ufs_mtk_host *host = ufshcd_get_variant(hba);

	if (hba->dev_info.clk_gating_wait_us) {
		host->ref_clk_gating_wait_us =
			hba->dev_info.clk_gating_wait_us;
	} else {
		host->ref_clk_gating_wait_us = gating_us;
	}

	host->ref_clk_ungating_wait_us = ungating_us;
}

static u32 ufs_mtk_link_get_state(struct ufs_hba *hba)
{
	u32 val;

	ufshcd_writel(hba, 0x20, REG_UFS_DEBUG_SEL);
	val = ufshcd_readl(hba, REG_UFS_PROBE);
	val = val >> 28;

	return val;
}

/**
 * ufs_mtk_setup_clocks - enables/disable clocks
 * @hba: host controller instance
 * @on: If true, enable clocks else disable them.
 * @status: PRE_CHANGE or POST_CHANGE notify
 *
 * Returns 0 on success, non-zero on failure.
 */
static int ufs_mtk_setup_clocks(struct ufs_hba *hba, bool on,
				enum ufs_notify_change_status status)
{
	struct ufs_mtk_host *host = ufshcd_get_variant(hba);
	int ret = 0;

	/*
	 * In case ufs_mtk_init() is not yet done, simply ignore.
	 * This ufs_mtk_setup_clocks() shall be called from
	 * ufs_mtk_init() after init is done.
	 */
	if (!host)
		return 0;

	if (!on && status == PRE_CHANGE) {
		if (!ufshcd_is_link_active(hba)) {
			ufs_mtk_setup_ref_clk(hba, on);
			ret = phy_power_off(host->mphy);
		} else {
			/*
			 * Gate ref-clk if link state is in Hibern8
			 * triggered by Auto-Hibern8.
			 */
			if (!ufshcd_can_hibern8_during_gating(hba) &&
			    ufshcd_is_auto_hibern8_enabled(hba) &&
			    ufs_mtk_link_get_state(hba) ==
			    VS_LINK_HIBERN8)
				ufs_mtk_setup_ref_clk(hba, on);
		}
	} else if (on && status == POST_CHANGE) {
		ret = phy_power_on(host->mphy);
		ufs_mtk_setup_ref_clk(hba, on);
	}

	return ret;
}

/**
 * ufs_mtk_init - find other essential mmio bases
 * @hba: host controller instance
 *
 * Binds PHY with controller and powers up PHY enabling clocks
 * and regulators.
 *
 * Returns -EPROBE_DEFER if binding fails, returns negative error
 * on phy power up failure and returns zero on success.
 */
static int ufs_mtk_init(struct ufs_hba *hba)
{
	struct ufs_mtk_host *host;
	struct device *dev = hba->dev;
	int err = 0;

	host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
	if (!host) {
		err = -ENOMEM;
		dev_info(dev, "%s: no memory for mtk ufs host\n", __func__);
		goto out;
	}

	host->hba = hba;
	ufshcd_set_variant(hba, host);

	err = ufs_mtk_bind_mphy(hba);
	if (err)
		goto out_variant_clear;

	/* Enable runtime autosuspend */
	hba->caps |= UFSHCD_CAP_RPM_AUTOSUSPEND;

	/* Enable clock-gating */
	hba->caps |= UFSHCD_CAP_CLK_GATING;

	/*
	 * ufshcd_vops_init() is invoked after
	 * ufshcd_setup_clock(true) in ufshcd_hba_init() thus
	 * phy clock setup is skipped.
	 *
	 * Enable phy clocks specifically here.
	 */
	ufs_mtk_setup_clocks(hba, true, POST_CHANGE);

	goto out;

out_variant_clear:
	ufshcd_set_variant(hba, NULL);
out:
	return err;
}

static int ufs_mtk_pre_pwr_change(struct ufs_hba *hba,
				  struct ufs_pa_layer_attr *dev_max_params,
				  struct ufs_pa_layer_attr *dev_req_params)
{
	struct ufs_dev_params host_cap;
	int ret;

	host_cap.tx_lanes = UFS_MTK_LIMIT_NUM_LANES_TX;
	host_cap.rx_lanes = UFS_MTK_LIMIT_NUM_LANES_RX;
	host_cap.hs_rx_gear = UFS_MTK_LIMIT_HSGEAR_RX;
	host_cap.hs_tx_gear = UFS_MTK_LIMIT_HSGEAR_TX;
	host_cap.pwm_rx_gear = UFS_MTK_LIMIT_PWMGEAR_RX;
	host_cap.pwm_tx_gear = UFS_MTK_LIMIT_PWMGEAR_TX;
	host_cap.rx_pwr_pwm = UFS_MTK_LIMIT_RX_PWR_PWM;
	host_cap.tx_pwr_pwm = UFS_MTK_LIMIT_TX_PWR_PWM;
	host_cap.rx_pwr_hs = UFS_MTK_LIMIT_RX_PWR_HS;
	host_cap.tx_pwr_hs = UFS_MTK_LIMIT_TX_PWR_HS;
	host_cap.hs_rate = UFS_MTK_LIMIT_HS_RATE;
	host_cap.desired_working_mode =
				UFS_MTK_LIMIT_DESIRED_MODE;

	ret = ufshcd_get_pwr_dev_param(&host_cap,
				       dev_max_params,
				       dev_req_params);
	if (ret) {
		pr_info("%s: failed to determine capabilities\n",
			__func__);
	}

	return ret;
}

static int ufs_mtk_pwr_change_notify(struct ufs_hba *hba,
				     enum ufs_notify_change_status stage,
				     struct ufs_pa_layer_attr *dev_max_params,
				     struct ufs_pa_layer_attr *dev_req_params)
{
	int ret = 0;

	switch (stage) {
	case PRE_CHANGE:
		ret = ufs_mtk_pre_pwr_change(hba, dev_max_params,
					     dev_req_params);
		break;
	case POST_CHANGE:
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int ufs_mtk_pre_link(struct ufs_hba *hba)
{
	int ret;
	u32 tmp;

	ufs_mtk_unipro_powerdown(hba, 0);

	/*
	 * Setting PA_Local_TX_LCC_Enable to 0 before link startup
	 * to make sure that both host and device TX LCC are disabled
	 * once link startup is completed.
	 */
	ret = ufshcd_disable_host_tx_lcc(hba);
	if (ret)
		return ret;

	/* disable deep stall */
	ret = ufshcd_dme_get(hba, UIC_ARG_MIB(VS_SAVEPOWERCONTROL), &tmp);
	if (ret)
		return ret;

	tmp &= ~(1 << 6);

	ret = ufshcd_dme_set(hba, UIC_ARG_MIB(VS_SAVEPOWERCONTROL), tmp);

	return ret;
}

static void ufs_mtk_setup_clk_gating(struct ufs_hba *hba)
{
	unsigned long flags;
	u32 ah_ms;

	if (ufshcd_is_clkgating_allowed(hba)) {
		if (ufshcd_is_auto_hibern8_supported(hba) && hba->ahit)
			ah_ms = FIELD_GET(UFSHCI_AHIBERN8_TIMER_MASK,
					  hba->ahit);
		else
			ah_ms = 10;
		spin_lock_irqsave(hba->host->host_lock, flags);
		hba->clk_gating.delay_ms = ah_ms + 5;
		spin_unlock_irqrestore(hba->host->host_lock, flags);
	}
}

static int ufs_mtk_post_link(struct ufs_hba *hba)
{
	/* enable unipro clock gating feature */
	ufs_mtk_cfg_unipro_cg(hba, true);

	/* configure auto-hibern8 timer to 10ms */
	if (ufshcd_is_auto_hibern8_supported(hba)) {
		ufshcd_auto_hibern8_update(hba,
			FIELD_PREP(UFSHCI_AHIBERN8_TIMER_MASK, 10) |
			FIELD_PREP(UFSHCI_AHIBERN8_SCALE_MASK, 3));
	}

	ufs_mtk_setup_clk_gating(hba);

	return 0;
}

static int ufs_mtk_link_startup_notify(struct ufs_hba *hba,
				       enum ufs_notify_change_status stage)
{
	int ret = 0;

	switch (stage) {
	case PRE_CHANGE:
		ret = ufs_mtk_pre_link(hba);
		break;
	case POST_CHANGE:
		ret = ufs_mtk_post_link(hba);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static void ufs_mtk_device_reset(struct ufs_hba *hba)
{
	struct arm_smccc_res res;

	ufs_mtk_device_reset_ctrl(0, res);

	/*
	 * The reset signal is active low. UFS devices shall detect
	 * more than or equal to 1us of positive or negative RST_n
	 * pulse width.
	 *
	 * To be on safe side, keep the reset low for at least 10us.
	 */
	usleep_range(10, 15);

	ufs_mtk_device_reset_ctrl(1, res);

	/* Some devices may need time to respond to rst_n */
	usleep_range(10000, 15000);

	dev_info(hba->dev, "device reset done\n");
}

static int ufs_mtk_link_set_hpm(struct ufs_hba *hba)
{
	int err;

	err = ufshcd_hba_enable(hba);
	if (err)
		return err;

	err = ufs_mtk_unipro_powerdown(hba, 0);
	if (err)
		return err;

	err = ufshcd_uic_hibern8_exit(hba);
	if (!err)
		ufshcd_set_link_active(hba);
	else
		return err;

	err = ufshcd_make_hba_operational(hba);
	if (err)
		return err;

	return 0;
}

static int ufs_mtk_link_set_lpm(struct ufs_hba *hba)
{
	int err;

	err = ufs_mtk_unipro_powerdown(hba, 1);
	if (err) {
		/* Resume UniPro state for following error recovery */
		ufs_mtk_unipro_powerdown(hba, 0);
		return err;
	}

	return 0;
}

static int ufs_mtk_suspend(struct ufs_hba *hba, enum ufs_pm_op pm_op)
{
	int err;
	struct ufs_mtk_host *host = ufshcd_get_variant(hba);

	if (ufshcd_is_link_hibern8(hba)) {
		err = ufs_mtk_link_set_lpm(hba);
		if (err)
			return -EAGAIN;
	}

	if (!ufshcd_is_link_active(hba))
		phy_power_off(host->mphy);

	return 0;
}

static int ufs_mtk_resume(struct ufs_hba *hba, enum ufs_pm_op pm_op)
{
	struct ufs_mtk_host *host = ufshcd_get_variant(hba);
	int err;

	if (!ufshcd_is_link_active(hba))
		phy_power_on(host->mphy);

	if (ufshcd_is_link_hibern8(hba)) {
		err = ufs_mtk_link_set_hpm(hba);
		if (err)
			return err;
	}

	return 0;
}

static void ufs_mtk_dbg_register_dump(struct ufs_hba *hba)
{
	ufshcd_dump_regs(hba, REG_UFS_REFCLK_CTRL, 0x4, "Ref-Clk Ctrl ");

	ufshcd_dump_regs(hba, REG_UFS_EXTREG, 0x4, "Ext Reg ");

	ufshcd_dump_regs(hba, REG_UFS_MPHYCTRL,
			 REG_UFS_REJECT_MON - REG_UFS_MPHYCTRL + 4,
			 "MPHY Ctrl ");

	/* Direct debugging information to REG_MTK_PROBE */
	ufshcd_writel(hba, 0x20, REG_UFS_DEBUG_SEL);
	ufshcd_dump_regs(hba, REG_UFS_PROBE, 0x4, "Debug Probe ");
}

static int ufs_mtk_apply_dev_quirks(struct ufs_hba *hba)
{
	struct ufs_dev_info *dev_info = &hba->dev_info;
	u16 mid = dev_info->wmanufacturerid;

	if (mid == UFS_VENDOR_SAMSUNG)
		ufshcd_dme_set(hba, UIC_ARG_MIB(PA_TACTIVATE), 6);

	/*
	 * Decide waiting time before gating reference clock and
	 * after ungating reference clock according to vendors'
	 * requirements.
	 */
	if (mid == UFS_VENDOR_SAMSUNG)
		ufs_mtk_setup_ref_clk_wait_us(hba, 1, 1);
	else if (mid == UFS_VENDOR_SKHYNIX)
		ufs_mtk_setup_ref_clk_wait_us(hba, 30, 30);
	else if (mid == UFS_VENDOR_TOSHIBA)
		ufs_mtk_setup_ref_clk_wait_us(hba, 100, 32);

	return 0;
}

/**
 * struct ufs_hba_mtk_vops - UFS MTK specific variant operations
 *
 * The variant operations configure the necessary controller and PHY
 * handshake during initialization.
 */
static struct ufs_hba_variant_ops ufs_hba_mtk_vops = {
	.name                = "mediatek.ufshci",
	.init                = ufs_mtk_init,
	.setup_clocks        = ufs_mtk_setup_clocks,
	.link_startup_notify = ufs_mtk_link_startup_notify,
	.pwr_change_notify   = ufs_mtk_pwr_change_notify,
	.apply_dev_quirks    = ufs_mtk_apply_dev_quirks,
	.suspend             = ufs_mtk_suspend,
	.resume              = ufs_mtk_resume,
	.dbg_register_dump   = ufs_mtk_dbg_register_dump,
	.device_reset        = ufs_mtk_device_reset,
};

/**
 * ufs_mtk_probe - probe routine of the driver
 * @pdev: pointer to Platform device handle
 *
 * Return zero for success and non-zero for failure
 */
static int ufs_mtk_probe(struct platform_device *pdev)
{
	int err;
	struct device *dev = &pdev->dev;

	/* perform generic probe */
	err = ufshcd_pltfrm_init(pdev, &ufs_hba_mtk_vops);
	if (err)
		dev_info(dev, "probe failed %d\n", err);

	return err;
}

/**
 * ufs_mtk_remove - set driver_data of the device to NULL
 * @pdev: pointer to platform device handle
 *
 * Always return 0
 */
static int ufs_mtk_remove(struct platform_device *pdev)
{
	struct ufs_hba *hba =  platform_get_drvdata(pdev);

	pm_runtime_get_sync(&(pdev)->dev);
	ufshcd_remove(hba);
	return 0;
}

static const struct of_device_id ufs_mtk_of_match[] = {
	{ .compatible = "mediatek,mt8183-ufshci"},
	{},
};

static const struct dev_pm_ops ufs_mtk_pm_ops = {
	.suspend         = ufshcd_pltfrm_suspend,
	.resume          = ufshcd_pltfrm_resume,
	.runtime_suspend = ufshcd_pltfrm_runtime_suspend,
	.runtime_resume  = ufshcd_pltfrm_runtime_resume,
	.runtime_idle    = ufshcd_pltfrm_runtime_idle,
};

static struct platform_driver ufs_mtk_pltform = {
	.probe      = ufs_mtk_probe,
	.remove     = ufs_mtk_remove,
	.shutdown   = ufshcd_pltfrm_shutdown,
	.driver = {
		.name   = "ufshcd-mtk",
		.pm     = &ufs_mtk_pm_ops,
		.of_match_table = ufs_mtk_of_match,
	},
};

MODULE_AUTHOR("Stanley Chu <stanley.chu@mediatek.com>");
MODULE_AUTHOR("Peter Wang <peter.wang@mediatek.com>");
MODULE_DESCRIPTION("MediaTek UFS Host Driver");
MODULE_LICENSE("GPL v2");

module_platform_driver(ufs_mtk_pltform);
