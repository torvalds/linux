// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2016, Linux Foundation. All rights reserved.
 */

#include <linux/acpi.h>
#include <linux/time.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/gpio/consumer.h>
#include <linux/reset-controller.h>
#include <linux/devfreq.h>

#include <soc/qcom/ice.h>

#include <ufs/ufshcd.h>
#include "ufshcd-pltfrm.h"
#include <ufs/unipro.h>
#include "ufs-qcom.h"
#include <ufs/ufshci.h>
#include <ufs/ufs_quirks.h>

#define MCQ_QCFGPTR_MASK	GENMASK(7, 0)
#define MCQ_QCFGPTR_UNIT	0x200
#define MCQ_SQATTR_OFFSET(c) \
	((((c) >> 16) & MCQ_QCFGPTR_MASK) * MCQ_QCFGPTR_UNIT)
#define MCQ_QCFG_SIZE	0x40

enum {
	TSTBUS_UAWM,
	TSTBUS_UARM,
	TSTBUS_TXUC,
	TSTBUS_RXUC,
	TSTBUS_DFC,
	TSTBUS_TRLUT,
	TSTBUS_TMRLUT,
	TSTBUS_OCSC,
	TSTBUS_UTP_HCI,
	TSTBUS_COMBINED,
	TSTBUS_WRAPPER,
	TSTBUS_UNIPRO,
	TSTBUS_MAX,
};

static struct ufs_qcom_host *ufs_qcom_hosts[MAX_UFS_QCOM_HOSTS];

static void ufs_qcom_get_default_testbus_cfg(struct ufs_qcom_host *host);
static int ufs_qcom_set_dme_vs_core_clk_ctrl_clear_div(struct ufs_hba *hba,
						       u32 clk_cycles);

static struct ufs_qcom_host *rcdev_to_ufs_host(struct reset_controller_dev *rcd)
{
	return container_of(rcd, struct ufs_qcom_host, rcdev);
}

#ifdef CONFIG_SCSI_UFS_CRYPTO

static inline void ufs_qcom_ice_enable(struct ufs_qcom_host *host)
{
	if (host->hba->caps & UFSHCD_CAP_CRYPTO)
		qcom_ice_enable(host->ice);
}

static int ufs_qcom_ice_init(struct ufs_qcom_host *host)
{
	struct ufs_hba *hba = host->hba;
	struct device *dev = hba->dev;
	struct qcom_ice *ice;

	ice = of_qcom_ice_get(dev);
	if (ice == ERR_PTR(-EOPNOTSUPP)) {
		dev_warn(dev, "Disabling inline encryption support\n");
		ice = NULL;
	}

	if (IS_ERR_OR_NULL(ice))
		return PTR_ERR_OR_ZERO(ice);

	host->ice = ice;
	hba->caps |= UFSHCD_CAP_CRYPTO;

	return 0;
}

static inline int ufs_qcom_ice_resume(struct ufs_qcom_host *host)
{
	if (host->hba->caps & UFSHCD_CAP_CRYPTO)
		return qcom_ice_resume(host->ice);

	return 0;
}

static inline int ufs_qcom_ice_suspend(struct ufs_qcom_host *host)
{
	if (host->hba->caps & UFSHCD_CAP_CRYPTO)
		return qcom_ice_suspend(host->ice);

	return 0;
}

static int ufs_qcom_ice_program_key(struct ufs_hba *hba,
				    const union ufs_crypto_cfg_entry *cfg,
				    int slot)
{
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);
	union ufs_crypto_cap_entry cap;
	bool config_enable =
		cfg->config_enable & UFS_CRYPTO_CONFIGURATION_ENABLE;

	/* Only AES-256-XTS has been tested so far. */
	cap = hba->crypto_cap_array[cfg->crypto_cap_idx];
	if (cap.algorithm_id != UFS_CRYPTO_ALG_AES_XTS ||
	    cap.key_size != UFS_CRYPTO_KEY_SIZE_256)
		return -EINVAL;

	if (config_enable)
		return qcom_ice_program_key(host->ice,
					    QCOM_ICE_CRYPTO_ALG_AES_XTS,
					    QCOM_ICE_CRYPTO_KEY_SIZE_256,
					    cfg->crypto_key,
					    cfg->data_unit_size, slot);
	else
		return qcom_ice_evict_key(host->ice, slot);
}

#else

#define ufs_qcom_ice_program_key NULL

static inline void ufs_qcom_ice_enable(struct ufs_qcom_host *host)
{
}

static int ufs_qcom_ice_init(struct ufs_qcom_host *host)
{
	return 0;
}

static inline int ufs_qcom_ice_resume(struct ufs_qcom_host *host)
{
	return 0;
}

static inline int ufs_qcom_ice_suspend(struct ufs_qcom_host *host)
{
	return 0;
}
#endif

static int ufs_qcom_host_clk_get(struct device *dev,
		const char *name, struct clk **clk_out, bool optional)
{
	struct clk *clk;
	int err = 0;

	clk = devm_clk_get(dev, name);
	if (!IS_ERR(clk)) {
		*clk_out = clk;
		return 0;
	}

	err = PTR_ERR(clk);

	if (optional && err == -ENOENT) {
		*clk_out = NULL;
		return 0;
	}

	if (err != -EPROBE_DEFER)
		dev_err(dev, "failed to get %s err %d\n", name, err);

	return err;
}

static int ufs_qcom_host_clk_enable(struct device *dev,
		const char *name, struct clk *clk)
{
	int err = 0;

	err = clk_prepare_enable(clk);
	if (err)
		dev_err(dev, "%s: %s enable failed %d\n", __func__, name, err);

	return err;
}

static void ufs_qcom_disable_lane_clks(struct ufs_qcom_host *host)
{
	if (!host->is_lane_clks_enabled)
		return;

	clk_disable_unprepare(host->tx_l1_sync_clk);
	clk_disable_unprepare(host->tx_l0_sync_clk);
	clk_disable_unprepare(host->rx_l1_sync_clk);
	clk_disable_unprepare(host->rx_l0_sync_clk);

	host->is_lane_clks_enabled = false;
}

static int ufs_qcom_enable_lane_clks(struct ufs_qcom_host *host)
{
	int err;
	struct device *dev = host->hba->dev;

	if (host->is_lane_clks_enabled)
		return 0;

	err = ufs_qcom_host_clk_enable(dev, "rx_lane0_sync_clk",
		host->rx_l0_sync_clk);
	if (err)
		return err;

	err = ufs_qcom_host_clk_enable(dev, "tx_lane0_sync_clk",
		host->tx_l0_sync_clk);
	if (err)
		goto disable_rx_l0;

	err = ufs_qcom_host_clk_enable(dev, "rx_lane1_sync_clk",
			host->rx_l1_sync_clk);
	if (err)
		goto disable_tx_l0;

	err = ufs_qcom_host_clk_enable(dev, "tx_lane1_sync_clk",
			host->tx_l1_sync_clk);
	if (err)
		goto disable_rx_l1;

	host->is_lane_clks_enabled = true;

	return 0;

disable_rx_l1:
	clk_disable_unprepare(host->rx_l1_sync_clk);
disable_tx_l0:
	clk_disable_unprepare(host->tx_l0_sync_clk);
disable_rx_l0:
	clk_disable_unprepare(host->rx_l0_sync_clk);

	return err;
}

static int ufs_qcom_init_lane_clks(struct ufs_qcom_host *host)
{
	int err = 0;
	struct device *dev = host->hba->dev;

	if (has_acpi_companion(dev))
		return 0;

	err = ufs_qcom_host_clk_get(dev, "rx_lane0_sync_clk",
					&host->rx_l0_sync_clk, false);
	if (err)
		return err;

	err = ufs_qcom_host_clk_get(dev, "tx_lane0_sync_clk",
					&host->tx_l0_sync_clk, false);
	if (err)
		return err;

	/* In case of single lane per direction, don't read lane1 clocks */
	if (host->hba->lanes_per_direction > 1) {
		err = ufs_qcom_host_clk_get(dev, "rx_lane1_sync_clk",
			&host->rx_l1_sync_clk, false);
		if (err)
			return err;

		err = ufs_qcom_host_clk_get(dev, "tx_lane1_sync_clk",
			&host->tx_l1_sync_clk, true);
	}

	return 0;
}

static int ufs_qcom_check_hibern8(struct ufs_hba *hba)
{
	int err;
	u32 tx_fsm_val = 0;
	unsigned long timeout = jiffies + msecs_to_jiffies(HBRN8_POLL_TOUT_MS);

	do {
		err = ufshcd_dme_get(hba,
				UIC_ARG_MIB_SEL(MPHY_TX_FSM_STATE,
					UIC_ARG_MPHY_TX_GEN_SEL_INDEX(0)),
				&tx_fsm_val);
		if (err || tx_fsm_val == TX_FSM_HIBERN8)
			break;

		/* sleep for max. 200us */
		usleep_range(100, 200);
	} while (time_before(jiffies, timeout));

	/*
	 * we might have scheduled out for long during polling so
	 * check the state again.
	 */
	if (time_after(jiffies, timeout))
		err = ufshcd_dme_get(hba,
				UIC_ARG_MIB_SEL(MPHY_TX_FSM_STATE,
					UIC_ARG_MPHY_TX_GEN_SEL_INDEX(0)),
				&tx_fsm_val);

	if (err) {
		dev_err(hba->dev, "%s: unable to get TX_FSM_STATE, err %d\n",
				__func__, err);
	} else if (tx_fsm_val != TX_FSM_HIBERN8) {
		err = tx_fsm_val;
		dev_err(hba->dev, "%s: invalid TX_FSM_STATE = %d\n",
				__func__, err);
	}

	return err;
}

static void ufs_qcom_select_unipro_mode(struct ufs_qcom_host *host)
{
	ufshcd_rmwl(host->hba, QUNIPRO_SEL,
		   ufs_qcom_cap_qunipro(host) ? QUNIPRO_SEL : 0,
		   REG_UFS_CFG1);

	if (host->hw_ver.major == 0x05)
		ufshcd_rmwl(host->hba, QUNIPRO_G4_SEL, 0, REG_UFS_CFG0);

	/* make sure above configuration is applied before we return */
	mb();
}

/*
 * ufs_qcom_host_reset - reset host controller and PHY
 */
static int ufs_qcom_host_reset(struct ufs_hba *hba)
{
	int ret = 0;
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);
	bool reenable_intr = false;

	if (!host->core_reset) {
		dev_warn(hba->dev, "%s: reset control not set\n", __func__);
		return 0;
	}

	reenable_intr = hba->is_irq_enabled;
	disable_irq(hba->irq);
	hba->is_irq_enabled = false;

	ret = reset_control_assert(host->core_reset);
	if (ret) {
		dev_err(hba->dev, "%s: core_reset assert failed, err = %d\n",
				 __func__, ret);
		return ret;
	}

	/*
	 * The hardware requirement for delay between assert/deassert
	 * is at least 3-4 sleep clock (32.7KHz) cycles, which comes to
	 * ~125us (4/32768). To be on the safe side add 200us delay.
	 */
	usleep_range(200, 210);

	ret = reset_control_deassert(host->core_reset);
	if (ret)
		dev_err(hba->dev, "%s: core_reset deassert failed, err = %d\n",
				 __func__, ret);

	usleep_range(1000, 1100);

	if (reenable_intr) {
		enable_irq(hba->irq);
		hba->is_irq_enabled = true;
	}

	return 0;
}

static u32 ufs_qcom_get_hs_gear(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);

	if (host->hw_ver.major == 0x1) {
		/*
		 * HS-G3 operations may not reliably work on legacy QCOM
		 * UFS host controller hardware even though capability
		 * exchange during link startup phase may end up
		 * negotiating maximum supported gear as G3.
		 * Hence downgrade the maximum supported gear to HS-G2.
		 */
		return UFS_HS_G2;
	} else if (host->hw_ver.major >= 0x4) {
		return UFS_QCOM_MAX_GEAR(ufshcd_readl(hba, REG_UFS_PARAM0));
	}

	/* Default is HS-G3 */
	return UFS_HS_G3;
}

static int ufs_qcom_power_up_sequence(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);
	struct phy *phy = host->generic_phy;
	int ret;

	/* Reset UFS Host Controller and PHY */
	ret = ufs_qcom_host_reset(hba);
	if (ret)
		dev_warn(hba->dev, "%s: host reset returned %d\n",
				  __func__, ret);

	/* phy initialization - calibrate the phy */
	ret = phy_init(phy);
	if (ret) {
		dev_err(hba->dev, "%s: phy init failed, ret = %d\n",
			__func__, ret);
		return ret;
	}

	phy_set_mode_ext(phy, PHY_MODE_UFS_HS_B, host->hs_gear);

	/* power on phy - start serdes and phy's power and clocks */
	ret = phy_power_on(phy);
	if (ret) {
		dev_err(hba->dev, "%s: phy power on failed, ret = %d\n",
			__func__, ret);
		goto out_disable_phy;
	}

	ufs_qcom_select_unipro_mode(host);

	return 0;

out_disable_phy:
	phy_exit(phy);

	return ret;
}

/*
 * The UTP controller has a number of internal clock gating cells (CGCs).
 * Internal hardware sub-modules within the UTP controller control the CGCs.
 * Hardware CGCs disable the clock to inactivate UTP sub-modules not involved
 * in a specific operation, UTP controller CGCs are by default disabled and
 * this function enables them (after every UFS link startup) to save some power
 * leakage.
 */
static void ufs_qcom_enable_hw_clk_gating(struct ufs_hba *hba)
{
	ufshcd_writel(hba,
		ufshcd_readl(hba, REG_UFS_CFG2) | REG_UFS_CFG2_CGC_EN_ALL,
		REG_UFS_CFG2);

	/* Ensure that HW clock gating is enabled before next operations */
	mb();
}

static int ufs_qcom_hce_enable_notify(struct ufs_hba *hba,
				      enum ufs_notify_change_status status)
{
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);
	int err = 0;

	switch (status) {
	case PRE_CHANGE:
		ufs_qcom_power_up_sequence(hba);
		/*
		 * The PHY PLL output is the source of tx/rx lane symbol
		 * clocks, hence, enable the lane clocks only after PHY
		 * is initialized.
		 */
		err = ufs_qcom_enable_lane_clks(host);
		break;
	case POST_CHANGE:
		/* check if UFS PHY moved from DISABLED to HIBERN8 */
		err = ufs_qcom_check_hibern8(hba);
		ufs_qcom_enable_hw_clk_gating(hba);
		ufs_qcom_ice_enable(host);
		break;
	default:
		dev_err(hba->dev, "%s: invalid status %d\n", __func__, status);
		err = -EINVAL;
		break;
	}
	return err;
}

/*
 * Returns zero for success and non-zero in case of a failure
 */
static int ufs_qcom_cfg_timers(struct ufs_hba *hba, u32 gear,
			       u32 hs, u32 rate, bool update_link_startup_timer)
{
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);
	struct ufs_clk_info *clki;
	u32 core_clk_period_in_ns;
	u32 tx_clk_cycles_per_us = 0;
	unsigned long core_clk_rate = 0;
	u32 core_clk_cycles_per_us = 0;

	static u32 pwm_fr_table[][2] = {
		{UFS_PWM_G1, 0x1},
		{UFS_PWM_G2, 0x1},
		{UFS_PWM_G3, 0x1},
		{UFS_PWM_G4, 0x1},
	};

	static u32 hs_fr_table_rA[][2] = {
		{UFS_HS_G1, 0x1F},
		{UFS_HS_G2, 0x3e},
		{UFS_HS_G3, 0x7D},
	};

	static u32 hs_fr_table_rB[][2] = {
		{UFS_HS_G1, 0x24},
		{UFS_HS_G2, 0x49},
		{UFS_HS_G3, 0x92},
	};

	/*
	 * The Qunipro controller does not use following registers:
	 * SYS1CLK_1US_REG, TX_SYMBOL_CLK_1US_REG, CLK_NS_REG &
	 * UFS_REG_PA_LINK_STARTUP_TIMER
	 * But UTP controller uses SYS1CLK_1US_REG register for Interrupt
	 * Aggregation logic.
	*/
	if (ufs_qcom_cap_qunipro(host) && !ufshcd_is_intr_aggr_allowed(hba))
		return 0;

	if (gear == 0) {
		dev_err(hba->dev, "%s: invalid gear = %d\n", __func__, gear);
		return -EINVAL;
	}

	list_for_each_entry(clki, &hba->clk_list_head, list) {
		if (!strcmp(clki->name, "core_clk"))
			core_clk_rate = clk_get_rate(clki->clk);
	}

	/* If frequency is smaller than 1MHz, set to 1MHz */
	if (core_clk_rate < DEFAULT_CLK_RATE_HZ)
		core_clk_rate = DEFAULT_CLK_RATE_HZ;

	core_clk_cycles_per_us = core_clk_rate / USEC_PER_SEC;
	if (ufshcd_readl(hba, REG_UFS_SYS1CLK_1US) != core_clk_cycles_per_us) {
		ufshcd_writel(hba, core_clk_cycles_per_us, REG_UFS_SYS1CLK_1US);
		/*
		 * make sure above write gets applied before we return from
		 * this function.
		 */
		mb();
	}

	if (ufs_qcom_cap_qunipro(host))
		return 0;

	core_clk_period_in_ns = NSEC_PER_SEC / core_clk_rate;
	core_clk_period_in_ns <<= OFFSET_CLK_NS_REG;
	core_clk_period_in_ns &= MASK_CLK_NS_REG;

	switch (hs) {
	case FASTAUTO_MODE:
	case FAST_MODE:
		if (rate == PA_HS_MODE_A) {
			if (gear > ARRAY_SIZE(hs_fr_table_rA)) {
				dev_err(hba->dev,
					"%s: index %d exceeds table size %zu\n",
					__func__, gear,
					ARRAY_SIZE(hs_fr_table_rA));
				return -EINVAL;
			}
			tx_clk_cycles_per_us = hs_fr_table_rA[gear-1][1];
		} else if (rate == PA_HS_MODE_B) {
			if (gear > ARRAY_SIZE(hs_fr_table_rB)) {
				dev_err(hba->dev,
					"%s: index %d exceeds table size %zu\n",
					__func__, gear,
					ARRAY_SIZE(hs_fr_table_rB));
				return -EINVAL;
			}
			tx_clk_cycles_per_us = hs_fr_table_rB[gear-1][1];
		} else {
			dev_err(hba->dev, "%s: invalid rate = %d\n",
				__func__, rate);
			return -EINVAL;
		}
		break;
	case SLOWAUTO_MODE:
	case SLOW_MODE:
		if (gear > ARRAY_SIZE(pwm_fr_table)) {
			dev_err(hba->dev,
					"%s: index %d exceeds table size %zu\n",
					__func__, gear,
					ARRAY_SIZE(pwm_fr_table));
			return -EINVAL;
		}
		tx_clk_cycles_per_us = pwm_fr_table[gear-1][1];
		break;
	case UNCHANGED:
	default:
		dev_err(hba->dev, "%s: invalid mode = %d\n", __func__, hs);
		return -EINVAL;
	}

	if (ufshcd_readl(hba, REG_UFS_TX_SYMBOL_CLK_NS_US) !=
	    (core_clk_period_in_ns | tx_clk_cycles_per_us)) {
		/* this register 2 fields shall be written at once */
		ufshcd_writel(hba, core_clk_period_in_ns | tx_clk_cycles_per_us,
			      REG_UFS_TX_SYMBOL_CLK_NS_US);
		/*
		 * make sure above write gets applied before we return from
		 * this function.
		 */
		mb();
	}

	if (update_link_startup_timer && host->hw_ver.major != 0x5) {
		ufshcd_writel(hba, ((core_clk_rate / MSEC_PER_SEC) * 100),
			      REG_UFS_CFG0);
		/*
		 * make sure that this configuration is applied before
		 * we return
		 */
		mb();
	}

	return 0;
}

static int ufs_qcom_link_startup_notify(struct ufs_hba *hba,
					enum ufs_notify_change_status status)
{
	int err = 0;
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);

	switch (status) {
	case PRE_CHANGE:
		if (ufs_qcom_cfg_timers(hba, UFS_PWM_G1, SLOWAUTO_MODE,
					0, true)) {
			dev_err(hba->dev, "%s: ufs_qcom_cfg_timers() failed\n",
				__func__);
			return -EINVAL;
		}

		if (ufs_qcom_cap_qunipro(host))
			/*
			 * set unipro core clock cycles to 150 & clear clock
			 * divider
			 */
			err = ufs_qcom_set_dme_vs_core_clk_ctrl_clear_div(hba,
									  150);

		/*
		 * Some UFS devices (and may be host) have issues if LCC is
		 * enabled. So we are setting PA_Local_TX_LCC_Enable to 0
		 * before link startup which will make sure that both host
		 * and device TX LCC are disabled once link startup is
		 * completed.
		 */
		if (ufshcd_get_local_unipro_ver(hba) != UFS_UNIPRO_VER_1_41)
			err = ufshcd_disable_host_tx_lcc(hba);

		break;
	default:
		break;
	}

	return err;
}

static void ufs_qcom_device_reset_ctrl(struct ufs_hba *hba, bool asserted)
{
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);

	/* reset gpio is optional */
	if (!host->device_reset)
		return;

	gpiod_set_value_cansleep(host->device_reset, asserted);
}

static int ufs_qcom_suspend(struct ufs_hba *hba, enum ufs_pm_op pm_op,
	enum ufs_notify_change_status status)
{
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);
	struct phy *phy = host->generic_phy;

	if (status == PRE_CHANGE)
		return 0;

	if (ufs_qcom_is_link_off(hba)) {
		/*
		 * Disable the tx/rx lane symbol clocks before PHY is
		 * powered down as the PLL source should be disabled
		 * after downstream clocks are disabled.
		 */
		ufs_qcom_disable_lane_clks(host);
		phy_power_off(phy);

		/* reset the connected UFS device during power down */
		ufs_qcom_device_reset_ctrl(hba, true);

	} else if (!ufs_qcom_is_link_active(hba)) {
		ufs_qcom_disable_lane_clks(host);
	}

	return ufs_qcom_ice_suspend(host);
}

static int ufs_qcom_resume(struct ufs_hba *hba, enum ufs_pm_op pm_op)
{
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);
	struct phy *phy = host->generic_phy;
	int err;

	if (ufs_qcom_is_link_off(hba)) {
		err = phy_power_on(phy);
		if (err) {
			dev_err(hba->dev, "%s: failed PHY power on: %d\n",
				__func__, err);
			return err;
		}

		err = ufs_qcom_enable_lane_clks(host);
		if (err)
			return err;

	} else if (!ufs_qcom_is_link_active(hba)) {
		err = ufs_qcom_enable_lane_clks(host);
		if (err)
			return err;
	}

	return ufs_qcom_ice_resume(host);
}

static void ufs_qcom_dev_ref_clk_ctrl(struct ufs_qcom_host *host, bool enable)
{
	if (host->dev_ref_clk_ctrl_mmio &&
	    (enable ^ host->is_dev_ref_clk_enabled)) {
		u32 temp = readl_relaxed(host->dev_ref_clk_ctrl_mmio);

		if (enable)
			temp |= host->dev_ref_clk_en_mask;
		else
			temp &= ~host->dev_ref_clk_en_mask;

		/*
		 * If we are here to disable this clock it might be immediately
		 * after entering into hibern8 in which case we need to make
		 * sure that device ref_clk is active for specific time after
		 * hibern8 enter.
		 */
		if (!enable) {
			unsigned long gating_wait;

			gating_wait = host->hba->dev_info.clk_gating_wait_us;
			if (!gating_wait) {
				udelay(1);
			} else {
				/*
				 * bRefClkGatingWaitTime defines the minimum
				 * time for which the reference clock is
				 * required by device during transition from
				 * HS-MODE to LS-MODE or HIBERN8 state. Give it
				 * more delay to be on the safe side.
				 */
				gating_wait += 10;
				usleep_range(gating_wait, gating_wait + 10);
			}
		}

		writel_relaxed(temp, host->dev_ref_clk_ctrl_mmio);

		/*
		 * Make sure the write to ref_clk reaches the destination and
		 * not stored in a Write Buffer (WB).
		 */
		readl(host->dev_ref_clk_ctrl_mmio);

		/*
		 * If we call hibern8 exit after this, we need to make sure that
		 * device ref_clk is stable for at least 1us before the hibern8
		 * exit command.
		 */
		if (enable)
			udelay(1);

		host->is_dev_ref_clk_enabled = enable;
	}
}

static int ufs_qcom_pwr_change_notify(struct ufs_hba *hba,
				enum ufs_notify_change_status status,
				struct ufs_pa_layer_attr *dev_max_params,
				struct ufs_pa_layer_attr *dev_req_params)
{
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);
	struct ufs_dev_params ufs_qcom_cap;
	int ret = 0;

	if (!dev_req_params) {
		pr_err("%s: incoming dev_req_params is NULL\n", __func__);
		return -EINVAL;
	}

	switch (status) {
	case PRE_CHANGE:
		ufshcd_init_pwr_dev_param(&ufs_qcom_cap);
		ufs_qcom_cap.hs_rate = UFS_QCOM_LIMIT_HS_RATE;

		/* This driver only supports symmetic gear setting i.e., hs_tx_gear == hs_rx_gear */
		ufs_qcom_cap.hs_tx_gear = ufs_qcom_cap.hs_rx_gear = ufs_qcom_get_hs_gear(hba);

		ret = ufshcd_get_pwr_dev_param(&ufs_qcom_cap,
					       dev_max_params,
					       dev_req_params);
		if (ret) {
			dev_err(hba->dev, "%s: failed to determine capabilities\n",
					__func__);
			return ret;
		}

		/* Use the agreed gear */
		host->hs_gear = dev_req_params->gear_tx;

		/* enable the device ref clock before changing to HS mode */
		if (!ufshcd_is_hs_mode(&hba->pwr_info) &&
			ufshcd_is_hs_mode(dev_req_params))
			ufs_qcom_dev_ref_clk_ctrl(host, true);

		if (host->hw_ver.major >= 0x4) {
			ufshcd_dme_configure_adapt(hba,
						dev_req_params->gear_tx,
						PA_INITIAL_ADAPT);
		}
		break;
	case POST_CHANGE:
		if (ufs_qcom_cfg_timers(hba, dev_req_params->gear_rx,
					dev_req_params->pwr_rx,
					dev_req_params->hs_rate, false)) {
			dev_err(hba->dev, "%s: ufs_qcom_cfg_timers() failed\n",
				__func__);
			/*
			 * we return error code at the end of the routine,
			 * but continue to configure UFS_PHY_TX_LANE_ENABLE
			 * and bus voting as usual
			 */
			ret = -EINVAL;
		}

		/* cache the power mode parameters to use internally */
		memcpy(&host->dev_req_params,
				dev_req_params, sizeof(*dev_req_params));

		/* disable the device ref clock if entered PWM mode */
		if (ufshcd_is_hs_mode(&hba->pwr_info) &&
			!ufshcd_is_hs_mode(dev_req_params))
			ufs_qcom_dev_ref_clk_ctrl(host, false);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int ufs_qcom_quirk_host_pa_saveconfigtime(struct ufs_hba *hba)
{
	int err;
	u32 pa_vs_config_reg1;

	err = ufshcd_dme_get(hba, UIC_ARG_MIB(PA_VS_CONFIG_REG1),
			     &pa_vs_config_reg1);
	if (err)
		return err;

	/* Allow extension of MSB bits of PA_SaveConfigTime attribute */
	return ufshcd_dme_set(hba, UIC_ARG_MIB(PA_VS_CONFIG_REG1),
			    (pa_vs_config_reg1 | (1 << 12)));
}

static int ufs_qcom_apply_dev_quirks(struct ufs_hba *hba)
{
	int err = 0;

	if (hba->dev_quirks & UFS_DEVICE_QUIRK_HOST_PA_SAVECONFIGTIME)
		err = ufs_qcom_quirk_host_pa_saveconfigtime(hba);

	if (hba->dev_info.wmanufacturerid == UFS_VENDOR_WDC)
		hba->dev_quirks |= UFS_DEVICE_QUIRK_HOST_PA_TACTIVATE;

	return err;
}

static u32 ufs_qcom_get_ufs_hci_version(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);

	if (host->hw_ver.major == 0x1)
		return ufshci_version(1, 1);
	else
		return ufshci_version(2, 0);
}

/**
 * ufs_qcom_advertise_quirks - advertise the known QCOM UFS controller quirks
 * @hba: host controller instance
 *
 * QCOM UFS host controller might have some non standard behaviours (quirks)
 * than what is specified by UFSHCI specification. Advertise all such
 * quirks to standard UFS host controller driver so standard takes them into
 * account.
 */
static void ufs_qcom_advertise_quirks(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);

	if (host->hw_ver.major == 0x01) {
		hba->quirks |= UFSHCD_QUIRK_DELAY_BEFORE_DME_CMDS
			    | UFSHCD_QUIRK_BROKEN_PA_RXHSUNTERMCAP
			    | UFSHCD_QUIRK_DME_PEER_ACCESS_AUTO_MODE;

		if (host->hw_ver.minor == 0x0001 && host->hw_ver.step == 0x0001)
			hba->quirks |= UFSHCD_QUIRK_BROKEN_INTR_AGGR;

		hba->quirks |= UFSHCD_QUIRK_BROKEN_LCC;
	}

	if (host->hw_ver.major == 0x2) {
		hba->quirks |= UFSHCD_QUIRK_BROKEN_UFS_HCI_VERSION;

		if (!ufs_qcom_cap_qunipro(host))
			/* Legacy UniPro mode still need following quirks */
			hba->quirks |= (UFSHCD_QUIRK_DELAY_BEFORE_DME_CMDS
				| UFSHCD_QUIRK_DME_PEER_ACCESS_AUTO_MODE
				| UFSHCD_QUIRK_BROKEN_PA_RXHSUNTERMCAP);
	}

	if (host->hw_ver.major > 0x3)
		hba->quirks |= UFSHCD_QUIRK_REINIT_AFTER_MAX_GEAR_SWITCH;
}

static void ufs_qcom_set_caps(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);

	hba->caps |= UFSHCD_CAP_CLK_GATING | UFSHCD_CAP_HIBERN8_WITH_CLK_GATING;
	hba->caps |= UFSHCD_CAP_CLK_SCALING | UFSHCD_CAP_WB_WITH_CLK_SCALING;
	hba->caps |= UFSHCD_CAP_AUTO_BKOPS_SUSPEND;
	hba->caps |= UFSHCD_CAP_WB_EN;
	hba->caps |= UFSHCD_CAP_AGGR_POWER_COLLAPSE;
	hba->caps |= UFSHCD_CAP_RPM_AUTOSUSPEND;

	if (host->hw_ver.major >= 0x2) {
		host->caps = UFS_QCOM_CAP_QUNIPRO |
			     UFS_QCOM_CAP_RETAIN_SEC_CFG_AFTER_PWR_COLLAPSE;
	}
}

/**
 * ufs_qcom_setup_clocks - enables/disable clocks
 * @hba: host controller instance
 * @on: If true, enable clocks else disable them.
 * @status: PRE_CHANGE or POST_CHANGE notify
 *
 * Returns 0 on success, non-zero on failure.
 */
static int ufs_qcom_setup_clocks(struct ufs_hba *hba, bool on,
				 enum ufs_notify_change_status status)
{
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);

	/*
	 * In case ufs_qcom_init() is not yet done, simply ignore.
	 * This ufs_qcom_setup_clocks() shall be called from
	 * ufs_qcom_init() after init is done.
	 */
	if (!host)
		return 0;

	switch (status) {
	case PRE_CHANGE:
		if (!on) {
			if (!ufs_qcom_is_link_active(hba)) {
				/* disable device ref_clk */
				ufs_qcom_dev_ref_clk_ctrl(host, false);
			}
		}
		break;
	case POST_CHANGE:
		if (on) {
			/* enable the device ref clock for HS mode*/
			if (ufshcd_is_hs_mode(&hba->pwr_info))
				ufs_qcom_dev_ref_clk_ctrl(host, true);
		}
		break;
	}

	return 0;
}

static int
ufs_qcom_reset_assert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct ufs_qcom_host *host = rcdev_to_ufs_host(rcdev);

	ufs_qcom_assert_reset(host->hba);
	/* provide 1ms delay to let the reset pulse propagate. */
	usleep_range(1000, 1100);
	return 0;
}

static int
ufs_qcom_reset_deassert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct ufs_qcom_host *host = rcdev_to_ufs_host(rcdev);

	ufs_qcom_deassert_reset(host->hba);

	/*
	 * after reset deassertion, phy will need all ref clocks,
	 * voltage, current to settle down before starting serdes.
	 */
	usleep_range(1000, 1100);
	return 0;
}

static const struct reset_control_ops ufs_qcom_reset_ops = {
	.assert = ufs_qcom_reset_assert,
	.deassert = ufs_qcom_reset_deassert,
};

/**
 * ufs_qcom_init - bind phy with controller
 * @hba: host controller instance
 *
 * Binds PHY with controller and powers up PHY enabling clocks
 * and regulators.
 *
 * Returns -EPROBE_DEFER if binding fails, returns negative error
 * on phy power up failure and returns zero on success.
 */
static int ufs_qcom_init(struct ufs_hba *hba)
{
	int err;
	struct device *dev = hba->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct ufs_qcom_host *host;
	struct resource *res;
	struct ufs_clk_info *clki;

	host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
	if (!host) {
		dev_err(dev, "%s: no memory for qcom ufs host\n", __func__);
		return -ENOMEM;
	}

	/* Make a two way bind between the qcom host and the hba */
	host->hba = hba;
	ufshcd_set_variant(hba, host);

	/* Setup the optional reset control of HCI */
	host->core_reset = devm_reset_control_get_optional(hba->dev, "rst");
	if (IS_ERR(host->core_reset)) {
		err = dev_err_probe(dev, PTR_ERR(host->core_reset),
				    "Failed to get reset control\n");
		goto out_variant_clear;
	}

	/* Fire up the reset controller. Failure here is non-fatal. */
	host->rcdev.of_node = dev->of_node;
	host->rcdev.ops = &ufs_qcom_reset_ops;
	host->rcdev.owner = dev->driver->owner;
	host->rcdev.nr_resets = 1;
	err = devm_reset_controller_register(dev, &host->rcdev);
	if (err)
		dev_warn(dev, "Failed to register reset controller\n");

	if (!has_acpi_companion(dev)) {
		host->generic_phy = devm_phy_get(dev, "ufsphy");
		if (IS_ERR(host->generic_phy)) {
			err = dev_err_probe(dev, PTR_ERR(host->generic_phy), "Failed to get PHY\n");
			goto out_variant_clear;
		}
	}

	host->device_reset = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(host->device_reset)) {
		err = PTR_ERR(host->device_reset);
		if (err != -EPROBE_DEFER)
			dev_err(dev, "failed to acquire reset gpio: %d\n", err);
		goto out_variant_clear;
	}

	ufs_qcom_get_controller_revision(hba, &host->hw_ver.major,
		&host->hw_ver.minor, &host->hw_ver.step);

	/*
	 * for newer controllers, device reference clock control bit has
	 * moved inside UFS controller register address space itself.
	 */
	if (host->hw_ver.major >= 0x02) {
		host->dev_ref_clk_ctrl_mmio = hba->mmio_base + REG_UFS_CFG1;
		host->dev_ref_clk_en_mask = BIT(26);
	} else {
		/* "dev_ref_clk_ctrl_mem" is optional resource */
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   "dev_ref_clk_ctrl_mem");
		if (res) {
			host->dev_ref_clk_ctrl_mmio =
					devm_ioremap_resource(dev, res);
			if (IS_ERR(host->dev_ref_clk_ctrl_mmio))
				host->dev_ref_clk_ctrl_mmio = NULL;
			host->dev_ref_clk_en_mask = BIT(5);
		}
	}

	list_for_each_entry(clki, &hba->clk_list_head, list) {
		if (!strcmp(clki->name, "core_clk_unipro"))
			clki->keep_link_active = true;
	}

	err = ufs_qcom_init_lane_clks(host);
	if (err)
		goto out_variant_clear;

	ufs_qcom_set_caps(hba);
	ufs_qcom_advertise_quirks(hba);

	err = ufs_qcom_ice_init(host);
	if (err)
		goto out_variant_clear;

	ufs_qcom_setup_clocks(hba, true, POST_CHANGE);

	if (hba->dev->id < MAX_UFS_QCOM_HOSTS)
		ufs_qcom_hosts[hba->dev->id] = host;

	ufs_qcom_get_default_testbus_cfg(host);
	err = ufs_qcom_testbus_config(host);
	if (err)
		/* Failure is non-fatal */
		dev_warn(dev, "%s: failed to configure the testbus %d\n",
				__func__, err);

	/*
	 * Power up the PHY using the minimum supported gear (UFS_HS_G2).
	 * Switching to max gear will be performed during reinit if supported.
	 */
	host->hs_gear = UFS_HS_G2;

	return 0;

out_variant_clear:
	ufshcd_set_variant(hba, NULL);

	return err;
}

static void ufs_qcom_exit(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);

	ufs_qcom_disable_lane_clks(host);
	phy_power_off(host->generic_phy);
	phy_exit(host->generic_phy);
}

static int ufs_qcom_set_dme_vs_core_clk_ctrl_clear_div(struct ufs_hba *hba,
						       u32 clk_cycles)
{
	int err;
	u32 core_clk_ctrl_reg;

	if (clk_cycles > DME_VS_CORE_CLK_CTRL_MAX_CORE_CLK_1US_CYCLES_MASK)
		return -EINVAL;

	err = ufshcd_dme_get(hba,
			    UIC_ARG_MIB(DME_VS_CORE_CLK_CTRL),
			    &core_clk_ctrl_reg);
	if (err)
		return err;

	core_clk_ctrl_reg &= ~DME_VS_CORE_CLK_CTRL_MAX_CORE_CLK_1US_CYCLES_MASK;
	core_clk_ctrl_reg |= clk_cycles;

	/* Clear CORE_CLK_DIV_EN */
	core_clk_ctrl_reg &= ~DME_VS_CORE_CLK_CTRL_CORE_CLK_DIV_EN_BIT;

	return ufshcd_dme_set(hba,
			    UIC_ARG_MIB(DME_VS_CORE_CLK_CTRL),
			    core_clk_ctrl_reg);
}

static int ufs_qcom_clk_scale_up_pre_change(struct ufs_hba *hba)
{
	/* nothing to do as of now */
	return 0;
}

static int ufs_qcom_clk_scale_up_post_change(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);

	if (!ufs_qcom_cap_qunipro(host))
		return 0;

	/* set unipro core clock cycles to 150 and clear clock divider */
	return ufs_qcom_set_dme_vs_core_clk_ctrl_clear_div(hba, 150);
}

static int ufs_qcom_clk_scale_down_pre_change(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);
	int err;
	u32 core_clk_ctrl_reg;

	if (!ufs_qcom_cap_qunipro(host))
		return 0;

	err = ufshcd_dme_get(hba,
			    UIC_ARG_MIB(DME_VS_CORE_CLK_CTRL),
			    &core_clk_ctrl_reg);

	/* make sure CORE_CLK_DIV_EN is cleared */
	if (!err &&
	    (core_clk_ctrl_reg & DME_VS_CORE_CLK_CTRL_CORE_CLK_DIV_EN_BIT)) {
		core_clk_ctrl_reg &= ~DME_VS_CORE_CLK_CTRL_CORE_CLK_DIV_EN_BIT;
		err = ufshcd_dme_set(hba,
				    UIC_ARG_MIB(DME_VS_CORE_CLK_CTRL),
				    core_clk_ctrl_reg);
	}

	return err;
}

static int ufs_qcom_clk_scale_down_post_change(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);

	if (!ufs_qcom_cap_qunipro(host))
		return 0;

	/* set unipro core clock cycles to 75 and clear clock divider */
	return ufs_qcom_set_dme_vs_core_clk_ctrl_clear_div(hba, 75);
}

static int ufs_qcom_clk_scale_notify(struct ufs_hba *hba,
		bool scale_up, enum ufs_notify_change_status status)
{
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);
	struct ufs_pa_layer_attr *dev_req_params = &host->dev_req_params;
	int err = 0;

	if (status == PRE_CHANGE) {
		err = ufshcd_uic_hibern8_enter(hba);
		if (err)
			return err;
		if (scale_up)
			err = ufs_qcom_clk_scale_up_pre_change(hba);
		else
			err = ufs_qcom_clk_scale_down_pre_change(hba);
		if (err)
			ufshcd_uic_hibern8_exit(hba);

	} else {
		if (scale_up)
			err = ufs_qcom_clk_scale_up_post_change(hba);
		else
			err = ufs_qcom_clk_scale_down_post_change(hba);


		if (err) {
			ufshcd_uic_hibern8_exit(hba);
			return err;
		}

		ufs_qcom_cfg_timers(hba,
				    dev_req_params->gear_rx,
				    dev_req_params->pwr_rx,
				    dev_req_params->hs_rate,
				    false);
		ufshcd_uic_hibern8_exit(hba);
	}

	return 0;
}

static void ufs_qcom_enable_test_bus(struct ufs_qcom_host *host)
{
	ufshcd_rmwl(host->hba, UFS_REG_TEST_BUS_EN,
			UFS_REG_TEST_BUS_EN, REG_UFS_CFG1);
	ufshcd_rmwl(host->hba, TEST_BUS_EN, TEST_BUS_EN, REG_UFS_CFG1);
}

static void ufs_qcom_get_default_testbus_cfg(struct ufs_qcom_host *host)
{
	/* provide a legal default configuration */
	host->testbus.select_major = TSTBUS_UNIPRO;
	host->testbus.select_minor = 37;
}

static bool ufs_qcom_testbus_cfg_is_ok(struct ufs_qcom_host *host)
{
	if (host->testbus.select_major >= TSTBUS_MAX) {
		dev_err(host->hba->dev,
			"%s: UFS_CFG1[TEST_BUS_SEL} may not equal 0x%05X\n",
			__func__, host->testbus.select_major);
		return false;
	}

	return true;
}

int ufs_qcom_testbus_config(struct ufs_qcom_host *host)
{
	int reg;
	int offset;
	u32 mask = TEST_BUS_SUB_SEL_MASK;

	if (!host)
		return -EINVAL;

	if (!ufs_qcom_testbus_cfg_is_ok(host))
		return -EPERM;

	switch (host->testbus.select_major) {
	case TSTBUS_UAWM:
		reg = UFS_TEST_BUS_CTRL_0;
		offset = 24;
		break;
	case TSTBUS_UARM:
		reg = UFS_TEST_BUS_CTRL_0;
		offset = 16;
		break;
	case TSTBUS_TXUC:
		reg = UFS_TEST_BUS_CTRL_0;
		offset = 8;
		break;
	case TSTBUS_RXUC:
		reg = UFS_TEST_BUS_CTRL_0;
		offset = 0;
		break;
	case TSTBUS_DFC:
		reg = UFS_TEST_BUS_CTRL_1;
		offset = 24;
		break;
	case TSTBUS_TRLUT:
		reg = UFS_TEST_BUS_CTRL_1;
		offset = 16;
		break;
	case TSTBUS_TMRLUT:
		reg = UFS_TEST_BUS_CTRL_1;
		offset = 8;
		break;
	case TSTBUS_OCSC:
		reg = UFS_TEST_BUS_CTRL_1;
		offset = 0;
		break;
	case TSTBUS_WRAPPER:
		reg = UFS_TEST_BUS_CTRL_2;
		offset = 16;
		break;
	case TSTBUS_COMBINED:
		reg = UFS_TEST_BUS_CTRL_2;
		offset = 8;
		break;
	case TSTBUS_UTP_HCI:
		reg = UFS_TEST_BUS_CTRL_2;
		offset = 0;
		break;
	case TSTBUS_UNIPRO:
		reg = UFS_UNIPRO_CFG;
		offset = 20;
		mask = 0xFFF;
		break;
	/*
	 * No need for a default case, since
	 * ufs_qcom_testbus_cfg_is_ok() checks that the configuration
	 * is legal
	 */
	}
	mask <<= offset;
	ufshcd_rmwl(host->hba, TEST_BUS_SEL,
		    (u32)host->testbus.select_major << 19,
		    REG_UFS_CFG1);
	ufshcd_rmwl(host->hba, mask,
		    (u32)host->testbus.select_minor << offset,
		    reg);
	ufs_qcom_enable_test_bus(host);
	/*
	 * Make sure the test bus configuration is
	 * committed before returning.
	 */
	mb();

	return 0;
}

static void ufs_qcom_dump_dbg_regs(struct ufs_hba *hba)
{
	u32 reg;
	struct ufs_qcom_host *host;

	host = ufshcd_get_variant(hba);

	ufshcd_dump_regs(hba, REG_UFS_SYS1CLK_1US, 16 * 4,
			 "HCI Vendor Specific Registers ");

	reg = ufs_qcom_get_debug_reg_offset(host, UFS_UFS_DBG_RD_REG_OCSC);
	ufshcd_dump_regs(hba, reg, 44 * 4, "UFS_UFS_DBG_RD_REG_OCSC ");

	reg = ufshcd_readl(hba, REG_UFS_CFG1);
	reg |= UTP_DBG_RAMS_EN;
	ufshcd_writel(hba, reg, REG_UFS_CFG1);

	reg = ufs_qcom_get_debug_reg_offset(host, UFS_UFS_DBG_RD_EDTL_RAM);
	ufshcd_dump_regs(hba, reg, 32 * 4, "UFS_UFS_DBG_RD_EDTL_RAM ");

	reg = ufs_qcom_get_debug_reg_offset(host, UFS_UFS_DBG_RD_DESC_RAM);
	ufshcd_dump_regs(hba, reg, 128 * 4, "UFS_UFS_DBG_RD_DESC_RAM ");

	reg = ufs_qcom_get_debug_reg_offset(host, UFS_UFS_DBG_RD_PRDT_RAM);
	ufshcd_dump_regs(hba, reg, 64 * 4, "UFS_UFS_DBG_RD_PRDT_RAM ");

	/* clear bit 17 - UTP_DBG_RAMS_EN */
	ufshcd_rmwl(hba, UTP_DBG_RAMS_EN, 0, REG_UFS_CFG1);

	reg = ufs_qcom_get_debug_reg_offset(host, UFS_DBG_RD_REG_UAWM);
	ufshcd_dump_regs(hba, reg, 4 * 4, "UFS_DBG_RD_REG_UAWM ");

	reg = ufs_qcom_get_debug_reg_offset(host, UFS_DBG_RD_REG_UARM);
	ufshcd_dump_regs(hba, reg, 4 * 4, "UFS_DBG_RD_REG_UARM ");

	reg = ufs_qcom_get_debug_reg_offset(host, UFS_DBG_RD_REG_TXUC);
	ufshcd_dump_regs(hba, reg, 48 * 4, "UFS_DBG_RD_REG_TXUC ");

	reg = ufs_qcom_get_debug_reg_offset(host, UFS_DBG_RD_REG_RXUC);
	ufshcd_dump_regs(hba, reg, 27 * 4, "UFS_DBG_RD_REG_RXUC ");

	reg = ufs_qcom_get_debug_reg_offset(host, UFS_DBG_RD_REG_DFC);
	ufshcd_dump_regs(hba, reg, 19 * 4, "UFS_DBG_RD_REG_DFC ");

	reg = ufs_qcom_get_debug_reg_offset(host, UFS_DBG_RD_REG_TRLUT);
	ufshcd_dump_regs(hba, reg, 34 * 4, "UFS_DBG_RD_REG_TRLUT ");

	reg = ufs_qcom_get_debug_reg_offset(host, UFS_DBG_RD_REG_TMRLUT);
	ufshcd_dump_regs(hba, reg, 9 * 4, "UFS_DBG_RD_REG_TMRLUT ");
}

/**
 * ufs_qcom_device_reset() - toggle the (optional) device reset line
 * @hba: per-adapter instance
 *
 * Toggles the (optional) reset line to reset the attached device.
 */
static int ufs_qcom_device_reset(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);

	/* reset gpio is optional */
	if (!host->device_reset)
		return -EOPNOTSUPP;

	/*
	 * The UFS device shall detect reset pulses of 1us, sleep for 10us to
	 * be on the safe side.
	 */
	ufs_qcom_device_reset_ctrl(hba, true);
	usleep_range(10, 15);

	ufs_qcom_device_reset_ctrl(hba, false);
	usleep_range(10, 15);

	return 0;
}

#if IS_ENABLED(CONFIG_DEVFREQ_GOV_SIMPLE_ONDEMAND)
static void ufs_qcom_config_scaling_param(struct ufs_hba *hba,
					struct devfreq_dev_profile *p,
					struct devfreq_simple_ondemand_data *d)
{
	p->polling_ms = 60;
	d->upthreshold = 70;
	d->downdifferential = 5;
}
#else
static void ufs_qcom_config_scaling_param(struct ufs_hba *hba,
		struct devfreq_dev_profile *p,
		struct devfreq_simple_ondemand_data *data)
{
}
#endif

static void ufs_qcom_reinit_notify(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);

	phy_power_off(host->generic_phy);
}

/* Resources */
static const struct ufshcd_res_info ufs_res_info[RES_MAX] = {
	{.name = "ufs_mem",},
	{.name = "mcq",},
	/* Submission Queue DAO */
	{.name = "mcq_sqd",},
	/* Submission Queue Interrupt Status */
	{.name = "mcq_sqis",},
	/* Completion Queue DAO */
	{.name = "mcq_cqd",},
	/* Completion Queue Interrupt Status */
	{.name = "mcq_cqis",},
	/* MCQ vendor specific */
	{.name = "mcq_vs",},
};

static int ufs_qcom_mcq_config_resource(struct ufs_hba *hba)
{
	struct platform_device *pdev = to_platform_device(hba->dev);
	struct ufshcd_res_info *res;
	struct resource *res_mem, *res_mcq;
	int i, ret = 0;

	memcpy(hba->res, ufs_res_info, sizeof(ufs_res_info));

	for (i = 0; i < RES_MAX; i++) {
		res = &hba->res[i];
		res->resource = platform_get_resource_byname(pdev,
							     IORESOURCE_MEM,
							     res->name);
		if (!res->resource) {
			dev_info(hba->dev, "Resource %s not provided\n", res->name);
			if (i == RES_UFS)
				return -ENOMEM;
			continue;
		} else if (i == RES_UFS) {
			res_mem = res->resource;
			res->base = hba->mmio_base;
			continue;
		}

		res->base = devm_ioremap_resource(hba->dev, res->resource);
		if (IS_ERR(res->base)) {
			dev_err(hba->dev, "Failed to map res %s, err=%d\n",
					 res->name, (int)PTR_ERR(res->base));
			ret = PTR_ERR(res->base);
			res->base = NULL;
			return ret;
		}
	}

	/* MCQ resource provided in DT */
	res = &hba->res[RES_MCQ];
	/* Bail if MCQ resource is provided */
	if (res->base)
		goto out;

	/* Explicitly allocate MCQ resource from ufs_mem */
	res_mcq = devm_kzalloc(hba->dev, sizeof(*res_mcq), GFP_KERNEL);
	if (!res_mcq)
		return -ENOMEM;

	res_mcq->start = res_mem->start +
			 MCQ_SQATTR_OFFSET(hba->mcq_capabilities);
	res_mcq->end = res_mcq->start + hba->nr_hw_queues * MCQ_QCFG_SIZE - 1;
	res_mcq->flags = res_mem->flags;
	res_mcq->name = "mcq";

	ret = insert_resource(&iomem_resource, res_mcq);
	if (ret) {
		dev_err(hba->dev, "Failed to insert MCQ resource, err=%d\n",
			ret);
		return ret;
	}

	res->base = devm_ioremap_resource(hba->dev, res_mcq);
	if (IS_ERR(res->base)) {
		dev_err(hba->dev, "MCQ registers mapping failed, err=%d\n",
			(int)PTR_ERR(res->base));
		ret = PTR_ERR(res->base);
		goto ioremap_err;
	}

out:
	hba->mcq_base = res->base;
	return 0;
ioremap_err:
	res->base = NULL;
	remove_resource(res_mcq);
	return ret;
}

static int ufs_qcom_op_runtime_config(struct ufs_hba *hba)
{
	struct ufshcd_res_info *mem_res, *sqdao_res;
	struct ufshcd_mcq_opr_info_t *opr;
	int i;

	mem_res = &hba->res[RES_UFS];
	sqdao_res = &hba->res[RES_MCQ_SQD];

	if (!mem_res->base || !sqdao_res->base)
		return -EINVAL;

	for (i = 0; i < OPR_MAX; i++) {
		opr = &hba->mcq_opr[i];
		opr->offset = sqdao_res->resource->start -
			      mem_res->resource->start + 0x40 * i;
		opr->stride = 0x100;
		opr->base = sqdao_res->base + 0x40 * i;
	}

	return 0;
}

static int ufs_qcom_get_hba_mac(struct ufs_hba *hba)
{
	/* Qualcomm HC supports up to 64 */
	return MAX_SUPP_MAC;
}

static int ufs_qcom_get_outstanding_cqs(struct ufs_hba *hba,
					unsigned long *ocqs)
{
	struct ufshcd_res_info *mcq_vs_res = &hba->res[RES_MCQ_VS];

	if (!mcq_vs_res->base)
		return -EINVAL;

	*ocqs = readl(mcq_vs_res->base + UFS_MEM_CQIS_VS);

	return 0;
}

static void ufs_qcom_write_msi_msg(struct msi_desc *desc, struct msi_msg *msg)
{
	struct device *dev = msi_desc_to_dev(desc);
	struct ufs_hba *hba = dev_get_drvdata(dev);

	ufshcd_mcq_config_esi(hba, msg);
}

static irqreturn_t ufs_qcom_mcq_esi_handler(int irq, void *__hba)
{
	struct ufs_hba *hba = __hba;
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);
	u32 id = irq - host->esi_base;
	struct ufs_hw_queue *hwq = &hba->uhq[id];

	ufshcd_mcq_write_cqis(hba, 0x1, id);
	ufshcd_mcq_poll_cqe_lock(hba, hwq);

	return IRQ_HANDLED;
}

static int ufs_qcom_config_esi(struct ufs_hba *hba)
{
	struct ufs_qcom_host *host = ufshcd_get_variant(hba);
	struct msi_desc *desc;
	struct msi_desc *failed_desc = NULL;
	int nr_irqs, ret;

	if (host->esi_enabled)
		return 0;
	else if (host->esi_base < 0)
		return -EINVAL;

	/*
	 * 1. We only handle CQs as of now.
	 * 2. Poll queues do not need ESI.
	 */
	nr_irqs = hba->nr_hw_queues - hba->nr_queues[HCTX_TYPE_POLL];
	ret = platform_msi_domain_alloc_irqs(hba->dev, nr_irqs,
					     ufs_qcom_write_msi_msg);
	if (ret)
		goto out;

	msi_for_each_desc(desc, hba->dev, MSI_DESC_ALL) {
		if (!desc->msi_index)
			host->esi_base = desc->irq;

		ret = devm_request_irq(hba->dev, desc->irq,
				       ufs_qcom_mcq_esi_handler,
				       IRQF_SHARED, "qcom-mcq-esi", hba);
		if (ret) {
			dev_err(hba->dev, "%s: Fail to request IRQ for %d, err = %d\n",
				__func__, desc->irq, ret);
			failed_desc = desc;
			break;
		}
	}

	if (ret) {
		/* Rewind */
		msi_for_each_desc(desc, hba->dev, MSI_DESC_ALL) {
			if (desc == failed_desc)
				break;
			devm_free_irq(hba->dev, desc->irq, hba);
		}
		platform_msi_domain_free_irqs(hba->dev);
	} else {
		if (host->hw_ver.major == 6 && host->hw_ver.minor == 0 &&
		    host->hw_ver.step == 0) {
			ufshcd_writel(hba,
				      ufshcd_readl(hba, REG_UFS_CFG3) | 0x1F000,
				      REG_UFS_CFG3);
		}
		ufshcd_mcq_enable_esi(hba);
	}

out:
	if (ret) {
		host->esi_base = -1;
		dev_warn(hba->dev, "Failed to request Platform MSI %d\n", ret);
	} else {
		host->esi_enabled = true;
	}

	return ret;
}

/*
 * struct ufs_hba_qcom_vops - UFS QCOM specific variant operations
 *
 * The variant operations configure the necessary controller and PHY
 * handshake during initialization.
 */
static const struct ufs_hba_variant_ops ufs_hba_qcom_vops = {
	.name                   = "qcom",
	.init                   = ufs_qcom_init,
	.exit                   = ufs_qcom_exit,
	.get_ufs_hci_version	= ufs_qcom_get_ufs_hci_version,
	.clk_scale_notify	= ufs_qcom_clk_scale_notify,
	.setup_clocks           = ufs_qcom_setup_clocks,
	.hce_enable_notify      = ufs_qcom_hce_enable_notify,
	.link_startup_notify    = ufs_qcom_link_startup_notify,
	.pwr_change_notify	= ufs_qcom_pwr_change_notify,
	.apply_dev_quirks	= ufs_qcom_apply_dev_quirks,
	.suspend		= ufs_qcom_suspend,
	.resume			= ufs_qcom_resume,
	.dbg_register_dump	= ufs_qcom_dump_dbg_regs,
	.device_reset		= ufs_qcom_device_reset,
	.config_scaling_param = ufs_qcom_config_scaling_param,
	.program_key		= ufs_qcom_ice_program_key,
	.reinit_notify		= ufs_qcom_reinit_notify,
	.mcq_config_resource	= ufs_qcom_mcq_config_resource,
	.get_hba_mac		= ufs_qcom_get_hba_mac,
	.op_runtime_config	= ufs_qcom_op_runtime_config,
	.get_outstanding_cqs	= ufs_qcom_get_outstanding_cqs,
	.config_esi		= ufs_qcom_config_esi,
};

/**
 * ufs_qcom_probe - probe routine of the driver
 * @pdev: pointer to Platform device handle
 *
 * Return zero for success and non-zero for failure
 */
static int ufs_qcom_probe(struct platform_device *pdev)
{
	int err;
	struct device *dev = &pdev->dev;

	/* Perform generic probe */
	err = ufshcd_pltfrm_init(pdev, &ufs_hba_qcom_vops);
	if (err)
		return dev_err_probe(dev, err, "ufshcd_pltfrm_init() failed\n");

	return 0;
}

/**
 * ufs_qcom_remove - set driver_data of the device to NULL
 * @pdev: pointer to platform device handle
 *
 * Always returns 0
 */
static int ufs_qcom_remove(struct platform_device *pdev)
{
	struct ufs_hba *hba =  platform_get_drvdata(pdev);

	pm_runtime_get_sync(&(pdev)->dev);
	ufshcd_remove(hba);
	platform_msi_domain_free_irqs(hba->dev);
	return 0;
}

static const struct of_device_id ufs_qcom_of_match[] __maybe_unused = {
	{ .compatible = "qcom,ufshc"},
	{},
};
MODULE_DEVICE_TABLE(of, ufs_qcom_of_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id ufs_qcom_acpi_match[] = {
	{ "QCOM24A5" },
	{ },
};
MODULE_DEVICE_TABLE(acpi, ufs_qcom_acpi_match);
#endif

static const struct dev_pm_ops ufs_qcom_pm_ops = {
	SET_RUNTIME_PM_OPS(ufshcd_runtime_suspend, ufshcd_runtime_resume, NULL)
	.prepare	 = ufshcd_suspend_prepare,
	.complete	 = ufshcd_resume_complete,
#ifdef CONFIG_PM_SLEEP
	.suspend         = ufshcd_system_suspend,
	.resume          = ufshcd_system_resume,
	.freeze          = ufshcd_system_freeze,
	.restore         = ufshcd_system_restore,
	.thaw            = ufshcd_system_thaw,
#endif
};

static struct platform_driver ufs_qcom_pltform = {
	.probe	= ufs_qcom_probe,
	.remove	= ufs_qcom_remove,
	.driver	= {
		.name	= "ufshcd-qcom",
		.pm	= &ufs_qcom_pm_ops,
		.of_match_table = of_match_ptr(ufs_qcom_of_match),
		.acpi_match_table = ACPI_PTR(ufs_qcom_acpi_match),
	},
};
module_platform_driver(ufs_qcom_pltform);

MODULE_LICENSE("GPL v2");
