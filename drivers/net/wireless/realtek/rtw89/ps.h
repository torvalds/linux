/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#ifndef __RTW89_PS_H_
#define __RTW89_PS_H_

void rtw89_enter_lps(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
		     bool ps_mode);
void rtw89_leave_lps(struct rtw89_dev *rtwdev);
void __rtw89_leave_ps_mode(struct rtw89_dev *rtwdev);
void __rtw89_enter_ps_mode(struct rtw89_dev *rtwdev);
void rtw89_leave_ps_mode(struct rtw89_dev *rtwdev);
void rtw89_enter_ips(struct rtw89_dev *rtwdev);
void rtw89_leave_ips(struct rtw89_dev *rtwdev);
void rtw89_set_coex_ctrl_lps(struct rtw89_dev *rtwdev, bool btc_ctrl);
void rtw89_process_p2p_ps(struct rtw89_dev *rtwdev,
			  struct rtw89_vif_link *rtwvif_link,
			  struct ieee80211_bss_conf *bss_conf);
void rtw89_recalc_lps(struct rtw89_dev *rtwdev);
void rtw89_p2p_noa_renew(struct rtw89_vif_link *rtwvif_link);
void rtw89_p2p_noa_append(struct rtw89_vif_link *rtwvif_link,
			  const struct ieee80211_p2p_noa_desc *desc);
u8 rtw89_p2p_noa_fetch(struct rtw89_vif_link *rtwvif_link, void **data);
void rtw89_p2p_noa_once_init(struct rtw89_vif_link *rtwvif_link);
void rtw89_p2p_noa_once_deinit(struct rtw89_vif_link *rtwvif_link);
void rtw89_p2p_noa_once_recalc(struct rtw89_vif_link *rtwvif_link);
void rtw89_p2p_disable_all_noa(struct rtw89_dev *rtwdev,
			       struct rtw89_vif_link *rtwvif_link,
			       struct ieee80211_bss_conf *bss_conf);

static inline void rtw89_leave_ips_by_hwflags(struct rtw89_dev *rtwdev)
{
	struct ieee80211_hw *hw = rtwdev->hw;

	if (hw->conf.flags & IEEE80211_CONF_IDLE)
		rtw89_leave_ips(rtwdev);
}

static inline void rtw89_enter_ips_by_hwflags(struct rtw89_dev *rtwdev)
{
	struct ieee80211_hw *hw = rtwdev->hw;

	/* prevent entering IPS after ROC, but it is scanning */
	if (rtwdev->scanning)
		return;

	if (hw->conf.flags & IEEE80211_CONF_IDLE)
		rtw89_enter_ips(rtwdev);
}

#endif
