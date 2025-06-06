// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "chan.h"
#include "coex.h"
#include "core.h"
#include "debug.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "ps.h"
#include "reg.h"
#include "util.h"

static int rtw89_fw_leave_lps_check(struct rtw89_dev *rtwdev, u8 macid)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	u32 pwr_en_bit = 0xE;
	u32 chk_msk = pwr_en_bit << (4 * macid);
	u32 polling;
	int ret;

	ret = read_poll_timeout_atomic(rtw89_read32_mask, polling, !polling,
				       1000, 50000, false, rtwdev,
				       mac->ps_status, chk_msk);
	if (ret) {
		rtw89_info(rtwdev, "rtw89: failed to leave lps state\n");
		return -EBUSY;
	}

	return 0;
}

static void rtw89_ps_power_mode_change_with_hci(struct rtw89_dev *rtwdev,
						bool enter)
{
	ieee80211_stop_queues(rtwdev->hw);
	rtwdev->hci.paused = true;
	flush_work(&rtwdev->txq_work);
	ieee80211_wake_queues(rtwdev->hw);

	rtw89_hci_pause(rtwdev, true);
	rtw89_mac_power_mode_change(rtwdev, enter);
	rtw89_hci_switch_mode(rtwdev, enter);
	rtw89_hci_pause(rtwdev, false);

	rtwdev->hci.paused = false;

	if (!enter) {
		local_bh_disable();
		napi_schedule(&rtwdev->napi);
		local_bh_enable();
	}
}

static void rtw89_ps_power_mode_change(struct rtw89_dev *rtwdev, bool enter)
{
	if (rtwdev->chip->low_power_hci_modes & BIT(rtwdev->ps_mode) &&
	    !test_bit(RTW89_FLAG_WOWLAN, rtwdev->flags))
		rtw89_ps_power_mode_change_with_hci(rtwdev, enter);
	else
		rtw89_mac_power_mode_change(rtwdev, enter);
}

void __rtw89_enter_ps_mode(struct rtw89_dev *rtwdev)
{
	if (!rtwdev->ps_mode)
		return;

	if (test_and_set_bit(RTW89_FLAG_LOW_POWER_MODE, rtwdev->flags))
		return;

	rtw89_ps_power_mode_change(rtwdev, true);
}

void __rtw89_leave_ps_mode(struct rtw89_dev *rtwdev)
{
	if (!rtwdev->ps_mode)
		return;

	if (test_and_clear_bit(RTW89_FLAG_LOW_POWER_MODE, rtwdev->flags))
		rtw89_ps_power_mode_change(rtwdev, false);
}

static void __rtw89_enter_lps_link(struct rtw89_dev *rtwdev,
				   struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_lps_parm lps_param = {
		.macid = rtwvif_link->mac_id,
		.psmode = RTW89_MAC_AX_PS_MODE_LEGACY,
		.lastrpwm = RTW89_LAST_RPWM_PS,
	};

	rtw89_btc_ntfy_radio_state(rtwdev, BTC_RFCTRL_FW_CTRL);
	rtw89_fw_h2c_lps_parm(rtwdev, &lps_param);
}

static void __rtw89_leave_lps(struct rtw89_dev *rtwdev,
			      struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_lps_parm lps_param = {
		.macid = rtwvif_link->mac_id,
		.psmode = RTW89_MAC_AX_PS_MODE_ACTIVE,
		.lastrpwm = RTW89_LAST_RPWM_ACTIVE,
	};

	rtw89_fw_h2c_lps_parm(rtwdev, &lps_param);
	rtw89_fw_leave_lps_check(rtwdev, 0);
	rtw89_btc_ntfy_radio_state(rtwdev, BTC_RFCTRL_WL_ON);
	rtw89_chip_digital_pwr_comp(rtwdev, rtwvif_link->phy_idx);
}

void rtw89_leave_ps_mode(struct rtw89_dev *rtwdev)
{
	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	__rtw89_leave_ps_mode(rtwdev);
}

void rtw89_enter_lps(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
		     bool ps_mode)
{
	struct rtw89_vif_link *rtwvif_link;
	bool can_ps_mode = true;
	unsigned int link_id;

	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	if (test_and_set_bit(RTW89_FLAG_LEISURE_PS, rtwdev->flags))
		return;

	rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id) {
		__rtw89_enter_lps_link(rtwdev, rtwvif_link);

		if (rtwvif_link->wifi_role == RTW89_WIFI_ROLE_P2P_CLIENT)
			can_ps_mode = false;
	}

	rtw89_fw_h2c_rf_ps_info(rtwdev, rtwvif);

	if (RTW89_CHK_FW_FEATURE(LPS_CH_INFO, &rtwdev->fw))
		rtw89_fw_h2c_lps_ch_info(rtwdev, rtwvif);
	else
		rtw89_fw_h2c_lps_ml_cmn_info(rtwdev, rtwvif);

	if (ps_mode && can_ps_mode)
		__rtw89_enter_ps_mode(rtwdev);
}

static void rtw89_leave_lps_vif(struct rtw89_dev *rtwdev,
				struct rtw89_vif_link *rtwvif_link)
{
	if (rtwvif_link->wifi_role != RTW89_WIFI_ROLE_STATION &&
	    rtwvif_link->wifi_role != RTW89_WIFI_ROLE_P2P_CLIENT)
		return;

	__rtw89_leave_lps(rtwdev, rtwvif_link);
}

void rtw89_leave_lps(struct rtw89_dev *rtwdev)
{
	struct rtw89_vif_link *rtwvif_link;
	struct rtw89_vif *rtwvif;
	unsigned int link_id;

	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	if (!test_and_clear_bit(RTW89_FLAG_LEISURE_PS, rtwdev->flags))
		return;

	__rtw89_leave_ps_mode(rtwdev);

	rtw89_phy_dm_reinit(rtwdev);

	rtw89_for_each_rtwvif(rtwdev, rtwvif)
		rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id)
			rtw89_leave_lps_vif(rtwdev, rtwvif_link);
}

void rtw89_enter_ips(struct rtw89_dev *rtwdev)
{
	struct rtw89_vif_link *rtwvif_link;
	struct rtw89_vif *rtwvif;
	unsigned int link_id;

	set_bit(RTW89_FLAG_INACTIVE_PS, rtwdev->flags);

	if (!test_bit(RTW89_FLAG_POWERON, rtwdev->flags))
		return;

	rtw89_for_each_rtwvif(rtwdev, rtwvif)
		rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id)
			rtw89_mac_vif_deinit(rtwdev, rtwvif_link);

	rtw89_core_stop(rtwdev);
}

void rtw89_leave_ips(struct rtw89_dev *rtwdev)
{
	struct rtw89_vif_link *rtwvif_link;
	struct rtw89_vif *rtwvif;
	unsigned int link_id;
	int ret;

	if (test_bit(RTW89_FLAG_POWERON, rtwdev->flags))
		return;

	ret = rtw89_core_start(rtwdev);
	if (ret)
		rtw89_err(rtwdev, "failed to leave idle state\n");

	rtw89_set_channel(rtwdev);

	rtw89_for_each_rtwvif(rtwdev, rtwvif)
		rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id)
			rtw89_mac_vif_init(rtwdev, rtwvif_link);

	clear_bit(RTW89_FLAG_INACTIVE_PS, rtwdev->flags);
}

void rtw89_set_coex_ctrl_lps(struct rtw89_dev *rtwdev, bool btc_ctrl)
{
	if (btc_ctrl)
		rtw89_leave_lps(rtwdev);
}

static void rtw89_tsf32_toggle(struct rtw89_dev *rtwdev,
			       struct rtw89_vif_link *rtwvif_link,
			       enum rtw89_p2pps_action act)
{
	if (act == RTW89_P2P_ACT_UPDATE || act == RTW89_P2P_ACT_REMOVE)
		return;

	if (act == RTW89_P2P_ACT_INIT)
		rtw89_fw_h2c_tsf32_toggle(rtwdev, rtwvif_link, true);
	else if (act == RTW89_P2P_ACT_TERMINATE)
		rtw89_fw_h2c_tsf32_toggle(rtwdev, rtwvif_link, false);
}

static void rtw89_p2p_disable_all_noa(struct rtw89_dev *rtwdev,
				      struct rtw89_vif_link *rtwvif_link,
				      struct ieee80211_bss_conf *bss_conf)
{
	enum rtw89_p2pps_action act;
	u8 noa_id;

	if (rtwvif_link->last_noa_nr == 0)
		return;

	for (noa_id = 0; noa_id < rtwvif_link->last_noa_nr; noa_id++) {
		if (noa_id == rtwvif_link->last_noa_nr - 1)
			act = RTW89_P2P_ACT_TERMINATE;
		else
			act = RTW89_P2P_ACT_REMOVE;
		rtw89_tsf32_toggle(rtwdev, rtwvif_link, act);
		rtw89_fw_h2c_p2p_act(rtwdev, rtwvif_link, bss_conf,
				     NULL, act, noa_id);
	}
}

static void rtw89_p2p_update_noa(struct rtw89_dev *rtwdev,
				 struct rtw89_vif_link *rtwvif_link,
				 struct ieee80211_bss_conf *bss_conf)
{
	struct ieee80211_p2p_noa_desc *desc;
	enum rtw89_p2pps_action act;
	u8 noa_id;

	for (noa_id = 0; noa_id < RTW89_P2P_MAX_NOA_NUM; noa_id++) {
		desc = &bss_conf->p2p_noa_attr.desc[noa_id];
		if (!desc->count || !desc->duration)
			break;

		if (noa_id == 0)
			act = RTW89_P2P_ACT_INIT;
		else
			act = RTW89_P2P_ACT_UPDATE;
		rtw89_tsf32_toggle(rtwdev, rtwvif_link, act);
		rtw89_fw_h2c_p2p_act(rtwdev, rtwvif_link, bss_conf,
				     desc, act, noa_id);
	}
	rtwvif_link->last_noa_nr = noa_id;
}

void rtw89_process_p2p_ps(struct rtw89_dev *rtwdev,
			  struct rtw89_vif_link *rtwvif_link,
			  struct ieee80211_bss_conf *bss_conf)
{
	rtw89_p2p_disable_all_noa(rtwdev, rtwvif_link, bss_conf);
	rtw89_p2p_update_noa(rtwdev, rtwvif_link, bss_conf);
}

void rtw89_recalc_lps(struct rtw89_dev *rtwdev)
{
	struct ieee80211_vif *vif, *found_vif = NULL;
	struct rtw89_vif *rtwvif;
	enum rtw89_entity_mode mode;
	int count = 0;

	mode = rtw89_get_entity_mode(rtwdev);
	if (mode == RTW89_ENTITY_MODE_MCC)
		goto disable_lps;

	rtw89_for_each_rtwvif(rtwdev, rtwvif) {
		vif = rtwvif_to_vif(rtwvif);

		if (vif->type != NL80211_IFTYPE_STATION) {
			count = 0;
			break;
		}

		count++;
		found_vif = vif;
	}

	if (count == 1 && found_vif->cfg.ps) {
		rtwdev->lps_enabled = true;
		return;
	}

disable_lps:
	rtw89_leave_lps(rtwdev);
	rtwdev->lps_enabled = false;
}

void rtw89_p2p_noa_renew(struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_p2p_noa_setter *setter = &rtwvif_link->p2p_noa;
	struct rtw89_p2p_noa_ie *ie = &setter->ie;
	struct rtw89_p2p_ie_head *p2p_head = &ie->p2p_head;
	struct rtw89_noa_attr_head *noa_head = &ie->noa_head;

	if (setter->noa_count) {
		setter->noa_index++;
		setter->noa_count = 0;
	}

	memset(ie, 0, sizeof(*ie));

	p2p_head->eid = WLAN_EID_VENDOR_SPECIFIC;
	p2p_head->ie_len = 4 + sizeof(*noa_head);
	p2p_head->oui[0] = (WLAN_OUI_WFA >> 16) & 0xff;
	p2p_head->oui[1] = (WLAN_OUI_WFA >> 8) & 0xff;
	p2p_head->oui[2] = (WLAN_OUI_WFA >> 0) & 0xff;
	p2p_head->oui_type = WLAN_OUI_TYPE_WFA_P2P;

	noa_head->attr_type = IEEE80211_P2P_ATTR_ABSENCE_NOTICE;
	noa_head->attr_len = cpu_to_le16(2);
	noa_head->index = setter->noa_index;
	noa_head->oppps_ctwindow = 0;
}

void rtw89_p2p_noa_append(struct rtw89_vif_link *rtwvif_link,
			  const struct ieee80211_p2p_noa_desc *desc)
{
	struct rtw89_p2p_noa_setter *setter = &rtwvif_link->p2p_noa;
	struct rtw89_p2p_noa_ie *ie = &setter->ie;
	struct rtw89_p2p_ie_head *p2p_head = &ie->p2p_head;
	struct rtw89_noa_attr_head *noa_head = &ie->noa_head;

	if (!desc->count || !desc->duration)
		return;

	if (setter->noa_count >= RTW89_P2P_MAX_NOA_NUM)
		return;

	p2p_head->ie_len += sizeof(*desc);
	le16_add_cpu(&noa_head->attr_len, sizeof(*desc));

	ie->noa_desc[setter->noa_count++] = *desc;
}

u8 rtw89_p2p_noa_fetch(struct rtw89_vif_link *rtwvif_link, void **data)
{
	struct rtw89_p2p_noa_setter *setter = &rtwvif_link->p2p_noa;
	struct rtw89_p2p_noa_ie *ie = &setter->ie;
	void *tail;

	if (!setter->noa_count)
		return 0;

	*data = ie;
	tail = ie->noa_desc + setter->noa_count;
	return tail - *data;
}

static void rtw89_ps_noa_once_set_work(struct wiphy *wiphy, struct wiphy_work *work)
{
	struct rtw89_ps_noa_once_handler *noa_once =
		container_of(work, struct rtw89_ps_noa_once_handler, set_work.work);

	lockdep_assert_wiphy(wiphy);

	noa_once->in_duration = true;
}

static void rtw89_ps_noa_once_clr_work(struct wiphy *wiphy, struct wiphy_work *work)
{
	struct rtw89_ps_noa_once_handler *noa_once =
		container_of(work, struct rtw89_ps_noa_once_handler, clr_work.work);
	struct rtw89_vif_link *rtwvif_link =
		container_of(noa_once, struct rtw89_vif_link, noa_once);
	struct rtw89_dev *rtwdev = rtwvif_link->rtwvif->rtwdev;

	lockdep_assert_wiphy(wiphy);

	rtw89_fw_h2c_set_bcn_fltr_cfg(rtwdev, rtwvif_link, true);
	noa_once->in_duration = false;
}

void rtw89_p2p_noa_once_init(struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_ps_noa_once_handler *noa_once = &rtwvif_link->noa_once;

	noa_once->in_duration = false;
	noa_once->tsf_begin = 0;
	noa_once->tsf_end = 0;

	wiphy_delayed_work_init(&noa_once->set_work, rtw89_ps_noa_once_set_work);
	wiphy_delayed_work_init(&noa_once->clr_work, rtw89_ps_noa_once_clr_work);
}

static void rtw89_p2p_noa_once_cancel(struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_ps_noa_once_handler *noa_once = &rtwvif_link->noa_once;
	struct rtw89_dev *rtwdev = rtwvif_link->rtwvif->rtwdev;
	struct wiphy *wiphy = rtwdev->hw->wiphy;

	wiphy_delayed_work_cancel(wiphy, &noa_once->set_work);
	wiphy_delayed_work_cancel(wiphy, &noa_once->clr_work);
}

void rtw89_p2p_noa_once_deinit(struct rtw89_vif_link *rtwvif_link)
{
	rtw89_p2p_noa_once_cancel(rtwvif_link);
	rtw89_p2p_noa_once_init(rtwvif_link);
}

void rtw89_p2p_noa_once_recalc(struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_ps_noa_once_handler *noa_once = &rtwvif_link->noa_once;
	struct rtw89_dev *rtwdev = rtwvif_link->rtwvif->rtwdev;
	const struct ieee80211_p2p_noa_desc *noa_desc;
	struct wiphy *wiphy = rtwdev->hw->wiphy;
	struct ieee80211_bss_conf *bss_conf;
	u64 tsf_begin = U64_MAX, tsf_end;
	u64 set_delay_us = 0;
	u64 clr_delay_us = 0;
	u32 start_time;
	u32 interval;
	u32 duration;
	u64 tsf;
	int ret;
	int i;

	lockdep_assert_wiphy(wiphy);

	ret = rtw89_mac_port_get_tsf(rtwdev, rtwvif_link, &tsf);
	if (ret) {
		rtw89_warn(rtwdev, "%s: failed to get tsf\n", __func__);
		return;
	}

	rcu_read_lock();

	bss_conf = rtw89_vif_rcu_dereference_link(rtwvif_link, true);

	for (i = 0; i < ARRAY_SIZE(bss_conf->p2p_noa_attr.desc); i++) {
		bool first = tsf_begin == U64_MAX;
		u64 tmp;

		noa_desc = &bss_conf->p2p_noa_attr.desc[i];
		if (noa_desc->count == 0 || noa_desc->count == 255)
			continue;

		start_time = le32_to_cpu(noa_desc->start_time);
		interval = le32_to_cpu(noa_desc->interval);
		duration = le32_to_cpu(noa_desc->duration);

		if (unlikely(duration == 0 ||
			     (noa_desc->count > 1 && interval == 0)))
			continue;

		tmp = start_time + interval * (noa_desc->count - 1) + duration;
		tmp = (tsf & GENMASK_ULL(63, 32)) + tmp;
		if (unlikely(tmp <= tsf))
			continue;
		tsf_end = first ? tmp : max(tsf_end, tmp);

		tmp = (tsf & GENMASK_ULL(63, 32)) | start_time;
		tsf_begin = first ? tmp : min(tsf_begin, tmp);
	}

	rcu_read_unlock();

	if (tsf_begin == U64_MAX)
		return;

	rtw89_p2p_noa_once_cancel(rtwvif_link);

	if (noa_once->tsf_end > tsf) {
		tsf_begin = min(tsf_begin, noa_once->tsf_begin);
		tsf_end = max(tsf_end, noa_once->tsf_end);
	}

	clr_delay_us = min_t(u64, tsf_end - tsf, UINT_MAX);

	if (tsf_begin <= tsf) {
		noa_once->in_duration = true;
		goto out;
	}

	set_delay_us = tsf_begin - tsf;
	if (unlikely(set_delay_us > UINT_MAX)) {
		rtw89_warn(rtwdev, "%s: unhandled begin\n", __func__);
		set_delay_us = 0;
		clr_delay_us = 0;
		rtw89_fw_h2c_set_bcn_fltr_cfg(rtwdev, rtwvif_link, true);
		noa_once->in_duration = false;
	}

out:
	if (set_delay_us)
		wiphy_delayed_work_queue(wiphy, &noa_once->set_work,
					 usecs_to_jiffies(set_delay_us));
	if (clr_delay_us)
		wiphy_delayed_work_queue(wiphy, &noa_once->clr_work,
					 usecs_to_jiffies(clr_delay_us));

	noa_once->tsf_begin = tsf_begin;
	noa_once->tsf_end = tsf_end;
}
