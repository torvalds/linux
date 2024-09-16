// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "chan.h"
#include "coex.h"
#include "core.h"
#include "debug.h"
#include "fw.h"
#include "mac.h"
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

void __rtw89_enter_ps_mode(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link)
{
	if (rtwvif_link->wifi_role == RTW89_WIFI_ROLE_P2P_CLIENT)
		return;

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

static void __rtw89_enter_lps(struct rtw89_dev *rtwdev,
			      struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_lps_parm lps_param = {
		.macid = rtwvif_link->mac_id,
		.psmode = RTW89_MAC_AX_PS_MODE_LEGACY,
		.lastrpwm = RTW89_LAST_RPWM_PS,
	};

	rtw89_btc_ntfy_radio_state(rtwdev, BTC_RFCTRL_FW_CTRL);
	rtw89_fw_h2c_lps_parm(rtwdev, &lps_param);
	rtw89_fw_h2c_lps_ch_info(rtwdev, rtwvif_link);
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
	lockdep_assert_held(&rtwdev->mutex);

	__rtw89_leave_ps_mode(rtwdev);
}

void rtw89_enter_lps(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link,
		     bool ps_mode)
{
	lockdep_assert_held(&rtwdev->mutex);

	if (test_and_set_bit(RTW89_FLAG_LEISURE_PS, rtwdev->flags))
		return;

	__rtw89_enter_lps(rtwdev, rtwvif_link);
	if (ps_mode)
		__rtw89_enter_ps_mode(rtwdev, rtwvif_link);
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

	lockdep_assert_held(&rtwdev->mutex);

	if (!test_and_clear_bit(RTW89_FLAG_LEISURE_PS, rtwdev->flags))
		return;

	__rtw89_leave_ps_mode(rtwdev);

	rtw89_for_each_rtwvif(rtwdev, rtwvif_link)
		rtw89_leave_lps_vif(rtwdev, rtwvif_link);
}

void rtw89_enter_ips(struct rtw89_dev *rtwdev)
{
	struct rtw89_vif_link *rtwvif_link;

	set_bit(RTW89_FLAG_INACTIVE_PS, rtwdev->flags);

	if (!test_bit(RTW89_FLAG_POWERON, rtwdev->flags))
		return;

	rtw89_for_each_rtwvif(rtwdev, rtwvif_link)
		rtw89_mac_vif_deinit(rtwdev, rtwvif_link);

	rtw89_core_stop(rtwdev);
}

void rtw89_leave_ips(struct rtw89_dev *rtwdev)
{
	struct rtw89_vif_link *rtwvif_link;
	int ret;

	if (test_bit(RTW89_FLAG_POWERON, rtwdev->flags))
		return;

	ret = rtw89_core_start(rtwdev);
	if (ret)
		rtw89_err(rtwdev, "failed to leave idle state\n");

	rtw89_set_channel(rtwdev);

	rtw89_for_each_rtwvif(rtwdev, rtwvif_link)
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
				      struct ieee80211_vif *vif)
{
	struct rtw89_vif_link *rtwvif_link = (struct rtw89_vif_link *)vif->drv_priv;
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
		rtw89_fw_h2c_p2p_act(rtwdev, vif, NULL, act, noa_id);
	}
}

static void rtw89_p2p_update_noa(struct rtw89_dev *rtwdev,
				 struct ieee80211_vif *vif)
{
	struct rtw89_vif_link *rtwvif_link = (struct rtw89_vif_link *)vif->drv_priv;
	struct ieee80211_p2p_noa_desc *desc;
	enum rtw89_p2pps_action act;
	u8 noa_id;

	for (noa_id = 0; noa_id < RTW89_P2P_MAX_NOA_NUM; noa_id++) {
		desc = &vif->bss_conf.p2p_noa_attr.desc[noa_id];
		if (!desc->count || !desc->duration)
			break;

		if (noa_id == 0)
			act = RTW89_P2P_ACT_INIT;
		else
			act = RTW89_P2P_ACT_UPDATE;
		rtw89_tsf32_toggle(rtwdev, rtwvif_link, act);
		rtw89_fw_h2c_p2p_act(rtwdev, vif, desc, act, noa_id);
	}
	rtwvif_link->last_noa_nr = noa_id;
}

void rtw89_process_p2p_ps(struct rtw89_dev *rtwdev, struct ieee80211_vif *vif)
{
	rtw89_p2p_disable_all_noa(rtwdev, vif);
	rtw89_p2p_update_noa(rtwdev, vif);
}

void rtw89_recalc_lps(struct rtw89_dev *rtwdev)
{
	struct ieee80211_vif *vif, *found_vif = NULL;
	struct rtw89_vif_link *rtwvif_link;
	enum rtw89_entity_mode mode;
	int count = 0;

	mode = rtw89_get_entity_mode(rtwdev);
	if (mode == RTW89_ENTITY_MODE_MCC)
		goto disable_lps;

	rtw89_for_each_rtwvif(rtwdev, rtwvif_link) {
		vif = rtwvif_to_vif(rtwvif_link);

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
