// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon Hixxxx UFS Driver
 *
 * Copyright (c) 2016-2017 Linaro Ltd.
 * Copyright (c) 2016-2017 HiSilicon Technologies Co., Ltd.
 */

#include <linux/time.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include "ufshcd.h"
#include "ufshcd-pltfrm.h"
#include "unipro.h"
#include "ufs-hisi.h"
#include "ufshci.h"
#include "ufs_quirks.h"

static int ufs_hisi_check_hibern8(struct ufs_hba *hba)
{
	int err = 0;
	u32 tx_fsm_val_0 = 0;
	u32 tx_fsm_val_1 = 0;
	unsigned long timeout = jiffies + msecs_to_jiffies(HBRN8_POLL_TOUT_MS);

	do {
		err = ufshcd_dme_get(hba, UIC_ARG_MIB_SEL(MPHY_TX_FSM_STATE, 0),
				      &tx_fsm_val_0);
		err |= ufshcd_dme_get(hba,
		    UIC_ARG_MIB_SEL(MPHY_TX_FSM_STATE, 1), &tx_fsm_val_1);
		if (err || (tx_fsm_val_0 == TX_FSM_HIBERN8 &&
			tx_fsm_val_1 == TX_FSM_HIBERN8))
			break;

		/* sleep for max. 200us */
		usleep_range(100, 200);
	} while (time_before(jiffies, timeout));

	/*
	 * we might have scheduled out for long during polling so
	 * check the state again.
	 */
	if (time_after(jiffies, timeout)) {
		err = ufshcd_dme_get(hba, UIC_ARG_MIB_SEL(MPHY_TX_FSM_STATE, 0),
				     &tx_fsm_val_0);
		err |= ufshcd_dme_get(hba,
		 UIC_ARG_MIB_SEL(MPHY_TX_FSM_STATE, 1), &tx_fsm_val_1);
	}

	if (err) {
		dev_err(hba->dev, "%s: unable to get TX_FSM_STATE, err %d\n",
			__func__, err);
	} else if (tx_fsm_val_0 != TX_FSM_HIBERN8 ||
			 tx_fsm_val_1 != TX_FSM_HIBERN8) {
		err = -1;
		dev_err(hba->dev, "%s: invalid TX_FSM_STATE, lane0 = %d, lane1 = %d\n",
			__func__, tx_fsm_val_0, tx_fsm_val_1);
	}

	return err;
}

static void ufs_hisi_clk_init(struct ufs_hba *hba)
{
	struct ufs_hisi_host *host = ufshcd_get_variant(hba);

	ufs_sys_ctrl_clr_bits(host, BIT_SYSCTRL_REF_CLOCK_EN, PHY_CLK_CTRL);
	if (ufs_sys_ctrl_readl(host, PHY_CLK_CTRL) & BIT_SYSCTRL_REF_CLOCK_EN)
		mdelay(1);
	/* use abb clk */
	ufs_sys_ctrl_clr_bits(host, BIT_UFS_REFCLK_SRC_SEl, UFS_SYSCTRL);
	ufs_sys_ctrl_clr_bits(host, BIT_UFS_REFCLK_ISO_EN, PHY_ISO_EN);
	/* open mphy ref clk */
	ufs_sys_ctrl_set_bits(host, BIT_SYSCTRL_REF_CLOCK_EN, PHY_CLK_CTRL);
}

static void ufs_hisi_soc_init(struct ufs_hba *hba)
{
	struct ufs_hisi_host *host = ufshcd_get_variant(hba);
	u32 reg;

	if (!IS_ERR(host->rst))
		reset_control_assert(host->rst);

	/* HC_PSW powerup */
	ufs_sys_ctrl_set_bits(host, BIT_UFS_PSW_MTCMOS_EN, PSW_POWER_CTRL);
	udelay(10);
	/* notify PWR ready */
	ufs_sys_ctrl_set_bits(host, BIT_SYSCTRL_PWR_READY, HC_LP_CTRL);
	ufs_sys_ctrl_writel(host, MASK_UFS_DEVICE_RESET | 0,
		UFS_DEVICE_RESET_CTRL);

	reg = ufs_sys_ctrl_readl(host, PHY_CLK_CTRL);
	reg = (reg & ~MASK_SYSCTRL_CFG_CLOCK_FREQ) | UFS_FREQ_CFG_CLK;
	/* set cfg clk freq */
	ufs_sys_ctrl_writel(host, reg, PHY_CLK_CTRL);
	/* set ref clk freq */
	ufs_sys_ctrl_clr_bits(host, MASK_SYSCTRL_REF_CLOCK_SEL, PHY_CLK_CTRL);
	/* bypass ufs clk gate */
	ufs_sys_ctrl_set_bits(host, MASK_UFS_CLK_GATE_BYPASS,
						 CLOCK_GATE_BYPASS);
	ufs_sys_ctrl_set_bits(host, MASK_UFS_SYSCRTL_BYPASS, UFS_SYSCTRL);

	/* open psw clk */
	ufs_sys_ctrl_set_bits(host, BIT_SYSCTRL_PSW_CLK_EN, PSW_CLK_CTRL);
	/* disable ufshc iso */
	ufs_sys_ctrl_clr_bits(host, BIT_UFS_PSW_ISO_CTRL, PSW_POWER_CTRL);
	/* disable phy iso */
	ufs_sys_ctrl_clr_bits(host, BIT_UFS_PHY_ISO_CTRL, PHY_ISO_EN);
	/* notice iso disable */
	ufs_sys_ctrl_clr_bits(host, BIT_SYSCTRL_LP_ISOL_EN, HC_LP_CTRL);

	/* disable lp_reset_n */
	ufs_sys_ctrl_set_bits(host, BIT_SYSCTRL_LP_RESET_N, RESET_CTRL_EN);
	mdelay(1);

	ufs_sys_ctrl_writel(host, MASK_UFS_DEVICE_RESET | BIT_UFS_DEVICE_RESET,
		UFS_DEVICE_RESET_CTRL);

	msleep(20);

	/*
	 * enable the fix of linereset recovery,
	 * and enable rx_reset/tx_rest beat
	 * enable ref_clk_en override(bit5) &
	 * override value = 1(bit4), with mask
	 */
	ufs_sys_ctrl_writel(host, 0x03300330, UFS_DEVICE_RESET_CTRL);

	if (!IS_ERR(host->rst))
		reset_control_deassert(host->rst);
}

static int ufs_hisi_link_startup_pre_change(struct ufs_hba *hba)
{
	struct ufs_hisi_host *host = ufshcd_get_variant(hba);
	int err;
	uint32_t value;
	uint32_t reg;

	/* Unipro VS_mphy_disable */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xD0C1, 0x0), 0x1);
	/* PA_HSSeries */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x156A, 0x0), 0x2);
	/* MPHY CBRATESEL */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8114, 0x0), 0x1);
	/* MPHY CBOVRCTRL2 */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8121, 0x0), 0x2D);
	/* MPHY CBOVRCTRL3 */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8122, 0x0), 0x1);

	if (host->caps & UFS_HISI_CAP_PHY10nm) {
		/* MPHY CBOVRCTRL4 */
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8127, 0x0), 0x98);
		/* MPHY CBOVRCTRL5 */
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8128, 0x0), 0x1);
	}

	/* Unipro VS_MphyCfgUpdt */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xD085, 0x0), 0x1);
	/* MPHY RXOVRCTRL4 rx0 */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x800D, 0x4), 0x58);
	/* MPHY RXOVRCTRL4 rx1 */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x800D, 0x5), 0x58);
	/* MPHY RXOVRCTRL5 rx0 */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x800E, 0x4), 0xB);
	/* MPHY RXOVRCTRL5 rx1 */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x800E, 0x5), 0xB);
	/* MPHY RXSQCONTROL rx0 */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8009, 0x4), 0x1);
	/* MPHY RXSQCONTROL rx1 */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8009, 0x5), 0x1);
	/* Unipro VS_MphyCfgUpdt */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xD085, 0x0), 0x1);

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8113, 0x0), 0x1);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xD085, 0x0), 0x1);

	if (host->caps & UFS_HISI_CAP_PHY10nm) {
		/* RX_Hibern8Time_Capability*/
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x0092, 0x4), 0xA);
		/* RX_Hibern8Time_Capability*/
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x0092, 0x5), 0xA);
		/* RX_Min_ActivateTime */
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x008f, 0x4), 0xA);
		/* RX_Min_ActivateTime*/
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x008f, 0x5), 0xA);
	} else {
		/* Tactive RX */
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x008F, 0x4), 0x7);
		/* Tactive RX */
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x008F, 0x5), 0x7);
	}

	/* Gear3 Synclength */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x0095, 0x4), 0x4F);
	/* Gear3 Synclength */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x0095, 0x5), 0x4F);
	/* Gear2 Synclength */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x0094, 0x4), 0x4F);
	/* Gear2 Synclength */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x0094, 0x5), 0x4F);
	/* Gear1 Synclength */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x008B, 0x4), 0x4F);
	/* Gear1 Synclength */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x008B, 0x5), 0x4F);
	/* Thibernate Tx */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x000F, 0x0), 0x5);
	/* Thibernate Tx */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x000F, 0x1), 0x5);

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xD085, 0x0), 0x1);
	/* Unipro VS_mphy_disable */
	ufshcd_dme_get(hba, UIC_ARG_MIB_SEL(0xD0C1, 0x0), &value);
	if (value != 0x1)
		dev_info(hba->dev,
		    "Warring!!! Unipro VS_mphy_disable is 0x%x\n", value);

	/* Unipro VS_mphy_disable */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xD0C1, 0x0), 0x0);
	err = ufs_hisi_check_hibern8(hba);
	if (err)
		dev_err(hba->dev, "ufs_hisi_check_hibern8 error\n");

	if (!(host->caps & UFS_HISI_CAP_PHY10nm))
		ufshcd_writel(hba, UFS_HCLKDIV_NORMAL_VALUE, UFS_REG_HCLKDIV);

	/* disable auto H8 */
	reg = ufshcd_readl(hba, REG_AUTO_HIBERNATE_IDLE_TIMER);
	reg = reg & (~UFS_AHIT_AH8ITV_MASK);
	ufshcd_writel(hba, reg, REG_AUTO_HIBERNATE_IDLE_TIMER);

	/* Unipro PA_Local_TX_LCC_Enable */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x155E, 0x0), 0x0);
	/* close Unipro VS_Mk2ExtnSupport */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xD0AB, 0x0), 0x0);
	ufshcd_dme_get(hba, UIC_ARG_MIB_SEL(0xD0AB, 0x0), &value);
	if (value != 0) {
		/* Ensure close success */
		dev_info(hba->dev, "WARN: close VS_Mk2ExtnSupport failed\n");
	}

	return err;
}

static int ufs_hisi_link_startup_post_change(struct ufs_hba *hba)
{
	struct ufs_hisi_host *host = ufshcd_get_variant(hba);

	/* Unipro DL_AFC0CreditThreshold */
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x2044), 0x0);
	/* Unipro DL_TC0OutAckThreshold */
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x2045), 0x0);
	/* Unipro DL_TC0TXFCThreshold */
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x2040), 0x9);

	/* not bypass ufs clk gate */
	ufs_sys_ctrl_clr_bits(host, MASK_UFS_CLK_GATE_BYPASS,
						CLOCK_GATE_BYPASS);
	ufs_sys_ctrl_clr_bits(host, MASK_UFS_SYSCRTL_BYPASS,
						UFS_SYSCTRL);

	/* select received symbol cnt */
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd09a), 0x80000000);
	 /* reset counter0 and enable */
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd09c), 0x00000005);

	return 0;
}

static int ufs_hisi_link_startup_notify(struct ufs_hba *hba,
					  enum ufs_notify_change_status status)
{
	int err = 0;

	switch (status) {
	case PRE_CHANGE:
		err = ufs_hisi_link_startup_pre_change(hba);
		break;
	case POST_CHANGE:
		err = ufs_hisi_link_startup_post_change(hba);
		break;
	default:
		break;
	}

	return err;
}

static void ufs_hisi_set_dev_cap(struct ufs_dev_params *hisi_param)
{
	hisi_param->rx_lanes = UFS_HISI_LIMIT_NUM_LANES_RX;
	hisi_param->tx_lanes = UFS_HISI_LIMIT_NUM_LANES_TX;
	hisi_param->hs_rx_gear = UFS_HISI_LIMIT_HSGEAR_RX;
	hisi_param->hs_tx_gear = UFS_HISI_LIMIT_HSGEAR_TX;
	hisi_param->pwm_rx_gear = UFS_HISI_LIMIT_PWMGEAR_RX;
	hisi_param->pwm_tx_gear = UFS_HISI_LIMIT_PWMGEAR_TX;
	hisi_param->rx_pwr_pwm = UFS_HISI_LIMIT_RX_PWR_PWM;
	hisi_param->tx_pwr_pwm = UFS_HISI_LIMIT_TX_PWR_PWM;
	hisi_param->rx_pwr_hs = UFS_HISI_LIMIT_RX_PWR_HS;
	hisi_param->tx_pwr_hs = UFS_HISI_LIMIT_TX_PWR_HS;
	hisi_param->hs_rate = UFS_HISI_LIMIT_HS_RATE;
	hisi_param->desired_working_mode = UFS_HISI_LIMIT_DESIRED_MODE;
}

static void ufs_hisi_pwr_change_pre_change(struct ufs_hba *hba)
{
	struct ufs_hisi_host *host = ufshcd_get_variant(hba);

	if (host->caps & UFS_HISI_CAP_PHY10nm) {
		/*
		 * Boston platform need to set SaveConfigTime to 0x13,
		 * and change sync length to maximum value
		 */
		/* VS_DebugSaveConfigTime */
		ufshcd_dme_set(hba, UIC_ARG_MIB((u32)0xD0A0), 0x13);
		/* g1 sync length */
		ufshcd_dme_set(hba, UIC_ARG_MIB((u32)0x1552), 0x4f);
		/* g2 sync length */
		ufshcd_dme_set(hba, UIC_ARG_MIB((u32)0x1554), 0x4f);
		/* g3 sync length */
		ufshcd_dme_set(hba, UIC_ARG_MIB((u32)0x1556), 0x4f);
		/* PA_Hibern8Time */
		ufshcd_dme_set(hba, UIC_ARG_MIB((u32)0x15a7), 0xA);
		/* PA_Tactivate */
		ufshcd_dme_set(hba, UIC_ARG_MIB((u32)0x15a8), 0xA);
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xd085, 0x0), 0x01);
	}

	if (hba->dev_quirks & UFS_DEVICE_QUIRK_HOST_VS_DEBUGSAVECONFIGTIME) {
		pr_info("ufs flash device must set VS_DebugSaveConfigTime 0x10\n");
		/* VS_DebugSaveConfigTime */
		ufshcd_dme_set(hba, UIC_ARG_MIB(0xD0A0), 0x10);
		/* sync length */
		ufshcd_dme_set(hba, UIC_ARG_MIB(0x1556), 0x48);
	}

	/* update */
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x15A8), 0x1);
	/* PA_TxSkip */
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x155c), 0x0);
	/*PA_PWRModeUserData0 = 8191, default is 0*/
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x15b0), 8191);
	/*PA_PWRModeUserData1 = 65535, default is 0*/
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x15b1), 65535);
	/*PA_PWRModeUserData2 = 32767, default is 0*/
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x15b2), 32767);
	/*DME_FC0ProtectionTimeOutVal = 8191, default is 0*/
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd041), 8191);
	/*DME_TC0ReplayTimeOutVal = 65535, default is 0*/
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd042), 65535);
	/*DME_AFC0ReqTimeOutVal = 32767, default is 0*/
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd043), 32767);
	/*PA_PWRModeUserData3 = 8191, default is 0*/
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x15b3), 8191);
	/*PA_PWRModeUserData4 = 65535, default is 0*/
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x15b4), 65535);
	/*PA_PWRModeUserData5 = 32767, default is 0*/
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x15b5), 32767);
	/*DME_FC1ProtectionTimeOutVal = 8191, default is 0*/
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd044), 8191);
	/*DME_TC1ReplayTimeOutVal = 65535, default is 0*/
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd045), 65535);
	/*DME_AFC1ReqTimeOutVal = 32767, default is 0*/
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd046), 32767);
}

static int ufs_hisi_pwr_change_notify(struct ufs_hba *hba,
				       enum ufs_notify_change_status status,
				       struct ufs_pa_layer_attr *dev_max_params,
				       struct ufs_pa_layer_attr *dev_req_params)
{
	struct ufs_dev_params ufs_hisi_cap;
	int ret = 0;

	if (!dev_req_params) {
		dev_err(hba->dev,
			    "%s: incoming dev_req_params is NULL\n", __func__);
		ret = -EINVAL;
		goto out;
	}

	switch (status) {
	case PRE_CHANGE:
		ufs_hisi_set_dev_cap(&ufs_hisi_cap);
		ret = ufshcd_get_pwr_dev_param(&ufs_hisi_cap,
					       dev_max_params, dev_req_params);
		if (ret) {
			dev_err(hba->dev,
			    "%s: failed to determine capabilities\n", __func__);
			goto out;
		}

		ufs_hisi_pwr_change_pre_change(hba);
		break;
	case POST_CHANGE:
		break;
	default:
		ret = -EINVAL;
		break;
	}
out:
	return ret;
}

static int ufs_hisi_suspend(struct ufs_hba *hba, enum ufs_pm_op pm_op)
{
	struct ufs_hisi_host *host = ufshcd_get_variant(hba);

	if (ufshcd_is_runtime_pm(pm_op))
		return 0;

	if (host->in_suspend) {
		WARN_ON(1);
		return 0;
	}

	ufs_sys_ctrl_clr_bits(host, BIT_SYSCTRL_REF_CLOCK_EN, PHY_CLK_CTRL);
	udelay(10);
	/* set ref_dig_clk override of PHY PCS to 0 */
	ufs_sys_ctrl_writel(host, 0x00100000, UFS_DEVICE_RESET_CTRL);

	host->in_suspend = true;

	return 0;
}

static int ufs_hisi_resume(struct ufs_hba *hba, enum ufs_pm_op pm_op)
{
	struct ufs_hisi_host *host = ufshcd_get_variant(hba);

	if (!host->in_suspend)
		return 0;

	/* set ref_dig_clk override of PHY PCS to 1 */
	ufs_sys_ctrl_writel(host, 0x00100010, UFS_DEVICE_RESET_CTRL);
	udelay(10);
	ufs_sys_ctrl_set_bits(host, BIT_SYSCTRL_REF_CLOCK_EN, PHY_CLK_CTRL);

	host->in_suspend = false;
	return 0;
}

static int ufs_hisi_get_resource(struct ufs_hisi_host *host)
{
	struct device *dev = host->hba->dev;
	struct platform_device *pdev = to_platform_device(dev);

	/* get resource of ufs sys ctrl */
	host->ufs_sys_ctrl = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(host->ufs_sys_ctrl))
		return PTR_ERR(host->ufs_sys_ctrl);

	return 0;
}

static void ufs_hisi_set_pm_lvl(struct ufs_hba *hba)
{
	hba->rpm_lvl = UFS_PM_LVL_1;
	hba->spm_lvl = UFS_PM_LVL_3;
}

/**
 * ufs_hisi_init_common
 * @hba: host controller instance
 */
static int ufs_hisi_init_common(struct ufs_hba *hba)
{
	int err = 0;
	struct device *dev = hba->dev;
	struct ufs_hisi_host *host;

	host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->hba = hba;
	ufshcd_set_variant(hba, host);

	host->rst  = devm_reset_control_get(dev, "rst");
	if (IS_ERR(host->rst)) {
		dev_err(dev, "%s: failed to get reset control\n", __func__);
		return PTR_ERR(host->rst);
	}

	ufs_hisi_set_pm_lvl(hba);

	err = ufs_hisi_get_resource(host);
	if (err) {
		ufshcd_set_variant(hba, NULL);
		return err;
	}

	return 0;
}

static int ufs_hi3660_init(struct ufs_hba *hba)
{
	int ret = 0;
	struct device *dev = hba->dev;

	ret = ufs_hisi_init_common(hba);
	if (ret) {
		dev_err(dev, "%s: ufs common init fail\n", __func__);
		return ret;
	}

	ufs_hisi_clk_init(hba);

	ufs_hisi_soc_init(hba);

	return 0;
}

static int ufs_hi3670_init(struct ufs_hba *hba)
{
	int ret = 0;
	struct device *dev = hba->dev;
	struct ufs_hisi_host *host;

	ret = ufs_hisi_init_common(hba);
	if (ret) {
		dev_err(dev, "%s: ufs common init fail\n", __func__);
		return ret;
	}

	ufs_hisi_clk_init(hba);

	ufs_hisi_soc_init(hba);

	/* Add cap for 10nm PHY variant on HI3670 SoC */
	host = ufshcd_get_variant(hba);
	host->caps |= UFS_HISI_CAP_PHY10nm;

	return 0;
}

static const struct ufs_hba_variant_ops ufs_hba_hi3660_vops = {
	.name = "hi3660",
	.init = ufs_hi3660_init,
	.link_startup_notify = ufs_hisi_link_startup_notify,
	.pwr_change_notify = ufs_hisi_pwr_change_notify,
	.suspend = ufs_hisi_suspend,
	.resume = ufs_hisi_resume,
};

static const struct ufs_hba_variant_ops ufs_hba_hi3670_vops = {
	.name = "hi3670",
	.init = ufs_hi3670_init,
	.link_startup_notify = ufs_hisi_link_startup_notify,
	.pwr_change_notify = ufs_hisi_pwr_change_notify,
	.suspend = ufs_hisi_suspend,
	.resume = ufs_hisi_resume,
};

static const struct of_device_id ufs_hisi_of_match[] = {
	{ .compatible = "hisilicon,hi3660-ufs", .data = &ufs_hba_hi3660_vops },
	{ .compatible = "hisilicon,hi3670-ufs", .data = &ufs_hba_hi3670_vops },
	{},
};

MODULE_DEVICE_TABLE(of, ufs_hisi_of_match);

static int ufs_hisi_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;

	of_id = of_match_node(ufs_hisi_of_match, pdev->dev.of_node);

	return ufshcd_pltfrm_init(pdev, of_id->data);
}

static int ufs_hisi_remove(struct platform_device *pdev)
{
	struct ufs_hba *hba =  platform_get_drvdata(pdev);

	ufshcd_remove(hba);
	return 0;
}

static const struct dev_pm_ops ufs_hisi_pm_ops = {
	.suspend	= ufshcd_pltfrm_suspend,
	.resume		= ufshcd_pltfrm_resume,
	.runtime_suspend = ufshcd_pltfrm_runtime_suspend,
	.runtime_resume  = ufshcd_pltfrm_runtime_resume,
	.runtime_idle    = ufshcd_pltfrm_runtime_idle,
};

static struct platform_driver ufs_hisi_pltform = {
	.probe	= ufs_hisi_probe,
	.remove	= ufs_hisi_remove,
	.shutdown = ufshcd_pltfrm_shutdown,
	.driver	= {
		.name	= "ufshcd-hisi",
		.pm	= &ufs_hisi_pm_ops,
		.of_match_table = of_match_ptr(ufs_hisi_of_match),
	},
};
module_platform_driver(ufs_hisi_pltform);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ufshcd-hisi");
MODULE_DESCRIPTION("HiSilicon Hixxxx UFS Driver");
