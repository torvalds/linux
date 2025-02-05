/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL92D_PHY_COMMON_H__
#define __RTL92D_PHY_COMMON_H__

#define TARGET_CHNL_NUM_5G			221
#define TARGET_CHNL_NUM_2G			14
#define CV_CURVE_CNT				64
#define RT_CANNOT_IO(hw)			false
#define RX_INDEX_MAPPING_NUM			15
#define IQK_BB_REG_NUM				10

#define IQK_DELAY_TIME				1
#define MAX_TOLERANCE				5
#define MAX_TOLERANCE_92D			3

enum baseband_config_type {
	BASEBAND_CONFIG_PHY_REG = 0,
	BASEBAND_CONFIG_AGC_TAB = 1,
};

enum rf_content {
	radioa_txt = 0,
	radiob_txt = 1,
	radioc_txt = 2,
	radiod_txt = 3
};

static inline void rtl92d_acquire_cckandrw_pagea_ctl(struct ieee80211_hw *hw,
						     unsigned long *flag)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->rtlhal.interface == INTF_USB)
		return;

	if (rtlpriv->rtlhal.interfaceindex == 1)
		spin_lock_irqsave(&rtlpriv->locks.cck_and_rw_pagea_lock, *flag);
}

static inline void rtl92d_release_cckandrw_pagea_ctl(struct ieee80211_hw *hw,
						     unsigned long *flag)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->rtlhal.interface == INTF_USB)
		return;

	if (rtlpriv->rtlhal.interfaceindex == 1)
		spin_unlock_irqrestore(&rtlpriv->locks.cck_and_rw_pagea_lock,
				       *flag);
}

u32 rtl92d_phy_query_rf_reg(struct ieee80211_hw *hw, enum radio_path rfpath,
			    u32 regaddr, u32 bitmask);
void rtl92d_phy_set_rf_reg(struct ieee80211_hw *hw, enum radio_path rfpath,
			   u32 regaddr, u32 bitmask, u32 data);
void rtl92d_phy_init_bb_rf_register_definition(struct ieee80211_hw *hw);
void rtl92d_store_pwrindex_diffrate_offset(struct ieee80211_hw *hw,
					   u32 regaddr, u32 bitmask, u32 data);
void rtl92d_phy_get_hw_reg_originalvalue(struct ieee80211_hw *hw);
void rtl92d_phy_set_txpower_level(struct ieee80211_hw *hw, u8 channel);
void rtl92d_phy_enable_rf_env(struct ieee80211_hw *hw, u8 rfpath,
			      u32 *pu4_regval);
void rtl92d_phy_restore_rf_env(struct ieee80211_hw *hw, u8 rfpath,
			       u32 *pu4_regval);
u8 rtl92d_get_rightchnlplace_for_iqk(u8 chnl);
void rtl92d_phy_save_adda_registers(struct ieee80211_hw *hw, const u32 *adda_reg,
				    u32 *adda_backup, u32 regnum);
void rtl92d_phy_save_mac_registers(struct ieee80211_hw *hw,
				   const u32 *macreg, u32 *macbackup);
void rtl92d_phy_path_adda_on(struct ieee80211_hw *hw,
			     const u32 *adda_reg, bool patha_on, bool is2t);
void rtl92d_phy_mac_setting_calibration(struct ieee80211_hw *hw,
					const u32 *macreg, u32 *macbackup);
void rtl92d_phy_calc_curvindex(struct ieee80211_hw *hw,
			       const u32 *targetchnl, u32 *curvecount_val,
			       bool is5g, u32 *curveindex);
void rtl92d_phy_reset_iqk_result(struct ieee80211_hw *hw);
bool rtl92d_phy_set_io_cmd(struct ieee80211_hw *hw, enum io_type iotype);
void rtl92d_phy_config_macphymode(struct ieee80211_hw *hw);
void rtl92d_phy_config_macphymode_info(struct ieee80211_hw *hw);
u8 rtl92d_get_chnlgroup_fromarray(u8 chnl);
u8 rtl92d_phy_get_chnlgroup_bypg(u8 chnlindex);
void rtl92d_phy_config_maccoexist_rfpage(struct ieee80211_hw *hw);
/* Without these declarations sparse warns about context imbalance. */
void rtl92d_acquire_cckandrw_pagea_ctl(struct ieee80211_hw *hw,
				       unsigned long *flag);
void rtl92d_release_cckandrw_pagea_ctl(struct ieee80211_hw *hw,
				       unsigned long *flag);

/* Without these helpers and the declarations sparse warns about
 * context imbalance.
 */
static inline void rtl92d_pci_lock(struct rtl_priv *rtlpriv)
{
	if (rtlpriv->rtlhal.interface == INTF_PCI)
		spin_lock(&rtlpriv->locks.rf_lock);
}

static inline void rtl92d_pci_unlock(struct rtl_priv *rtlpriv)
{
	if (rtlpriv->rtlhal.interface == INTF_PCI)
		spin_unlock(&rtlpriv->locks.rf_lock);
}

void rtl92d_pci_lock(struct rtl_priv *rtlpriv);
void rtl92d_pci_unlock(struct rtl_priv *rtlpriv);

#endif
