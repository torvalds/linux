// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include "main.h"
#include "sec.h"
#include "tx.h"
#include "fw.h"
#include "mac.h"
#include "coex.h"
#include "ps.h"
#include "reg.h"
#include "bf.h"
#include "debug.h"
#include "wow.h"

static void rtw_ops_tx(struct ieee80211_hw *hw,
		       struct ieee80211_tx_control *control,
		       struct sk_buff *skb)
{
	struct rtw_dev *rtwdev = hw->priv;

	if (!test_bit(RTW_FLAG_RUNNING, rtwdev->flags)) {
		ieee80211_free_txskb(hw, skb);
		return;
	}

	rtw_tx(rtwdev, control, skb);
}

static void rtw_ops_wake_tx_queue(struct ieee80211_hw *hw,
				  struct ieee80211_txq *txq)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_txq *rtwtxq = (struct rtw_txq *)txq->drv_priv;

	if (!test_bit(RTW_FLAG_RUNNING, rtwdev->flags))
		return;

	spin_lock_bh(&rtwdev->txq_lock);
	if (list_empty(&rtwtxq->list))
		list_add_tail(&rtwtxq->list, &rtwdev->txqs);
	spin_unlock_bh(&rtwdev->txq_lock);

	tasklet_schedule(&rtwdev->tx_tasklet);
}

static int rtw_ops_start(struct ieee80211_hw *hw)
{
	struct rtw_dev *rtwdev = hw->priv;
	int ret;

	mutex_lock(&rtwdev->mutex);
	ret = rtw_core_start(rtwdev);
	mutex_unlock(&rtwdev->mutex);

	return ret;
}

static void rtw_ops_stop(struct ieee80211_hw *hw)
{
	struct rtw_dev *rtwdev = hw->priv;

	mutex_lock(&rtwdev->mutex);
	rtw_core_stop(rtwdev);
	mutex_unlock(&rtwdev->mutex);
}

static int rtw_ops_config(struct ieee80211_hw *hw, u32 changed)
{
	struct rtw_dev *rtwdev = hw->priv;
	int ret = 0;

	mutex_lock(&rtwdev->mutex);

	rtw_leave_lps_deep(rtwdev);

	if ((changed & IEEE80211_CONF_CHANGE_IDLE) &&
	    !(hw->conf.flags & IEEE80211_CONF_IDLE)) {
		ret = rtw_leave_ips(rtwdev);
		if (ret) {
			rtw_err(rtwdev, "failed to leave idle state\n");
			goto out;
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_PS) {
		if (hw->conf.flags & IEEE80211_CONF_PS) {
			rtwdev->ps_enabled = true;
		} else {
			rtwdev->ps_enabled = false;
			rtw_leave_lps(rtwdev);
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL)
		rtw_set_channel(rtwdev);

	if ((changed & IEEE80211_CONF_CHANGE_IDLE) &&
	    (hw->conf.flags & IEEE80211_CONF_IDLE))
		rtw_enter_ips(rtwdev);

out:
	mutex_unlock(&rtwdev->mutex);
	return ret;
}

static const struct rtw_vif_port rtw_vif_port[] = {
	[0] = {
		.mac_addr	= {.addr = 0x0610},
		.bssid		= {.addr = 0x0618},
		.net_type	= {.addr = 0x0100, .mask = 0x30000},
		.aid		= {.addr = 0x06a8, .mask = 0x7ff},
		.bcn_ctrl	= {.addr = 0x0550, .mask = 0xff},
	},
	[1] = {
		.mac_addr	= {.addr = 0x0700},
		.bssid		= {.addr = 0x0708},
		.net_type	= {.addr = 0x0100, .mask = 0xc0000},
		.aid		= {.addr = 0x0710, .mask = 0x7ff},
		.bcn_ctrl	= {.addr = 0x0551, .mask = 0xff},
	},
	[2] = {
		.mac_addr	= {.addr = 0x1620},
		.bssid		= {.addr = 0x1628},
		.net_type	= {.addr = 0x1100, .mask = 0x3},
		.aid		= {.addr = 0x1600, .mask = 0x7ff},
		.bcn_ctrl	= {.addr = 0x0578, .mask = 0xff},
	},
	[3] = {
		.mac_addr	= {.addr = 0x1630},
		.bssid		= {.addr = 0x1638},
		.net_type	= {.addr = 0x1100, .mask = 0xc},
		.aid		= {.addr = 0x1604, .mask = 0x7ff},
		.bcn_ctrl	= {.addr = 0x0579, .mask = 0xff},
	},
	[4] = {
		.mac_addr	= {.addr = 0x1640},
		.bssid		= {.addr = 0x1648},
		.net_type	= {.addr = 0x1100, .mask = 0x30},
		.aid		= {.addr = 0x1608, .mask = 0x7ff},
		.bcn_ctrl	= {.addr = 0x057a, .mask = 0xff},
	},
};

static int rtw_ops_add_interface(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_vif *rtwvif = (struct rtw_vif *)vif->drv_priv;
	enum rtw_net_type net_type;
	u32 config = 0;
	u8 port = 0;
	u8 bcn_ctrl = 0;

	rtwvif->port = port;
	rtwvif->stats.tx_unicast = 0;
	rtwvif->stats.rx_unicast = 0;
	rtwvif->stats.tx_cnt = 0;
	rtwvif->stats.rx_cnt = 0;
	memset(&rtwvif->bfee, 0, sizeof(struct rtw_bfee));
	rtwvif->conf = &rtw_vif_port[port];
	rtw_txq_init(rtwdev, vif->txq);
	INIT_LIST_HEAD(&rtwvif->rsvd_page_list);

	mutex_lock(&rtwdev->mutex);

	rtw_leave_lps_deep(rtwdev);

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
		rtw_add_rsvd_page_bcn(rtwdev, rtwvif);
		net_type = RTW_NET_AP_MODE;
		bcn_ctrl = BIT_EN_BCN_FUNCTION | BIT_DIS_TSF_UDT;
		break;
	case NL80211_IFTYPE_ADHOC:
		rtw_add_rsvd_page_bcn(rtwdev, rtwvif);
		net_type = RTW_NET_AD_HOC;
		bcn_ctrl = BIT_EN_BCN_FUNCTION | BIT_DIS_TSF_UDT;
		break;
	case NL80211_IFTYPE_STATION:
		rtw_add_rsvd_page_sta(rtwdev, rtwvif);
		net_type = RTW_NET_NO_LINK;
		bcn_ctrl = BIT_EN_BCN_FUNCTION;
		break;
	default:
		WARN_ON(1);
		mutex_unlock(&rtwdev->mutex);
		return -EINVAL;
	}

	ether_addr_copy(rtwvif->mac_addr, vif->addr);
	config |= PORT_SET_MAC_ADDR;
	rtwvif->net_type = net_type;
	config |= PORT_SET_NET_TYPE;
	rtwvif->bcn_ctrl = bcn_ctrl;
	config |= PORT_SET_BCN_CTRL;
	rtw_vif_port_config(rtwdev, rtwvif, config);

	mutex_unlock(&rtwdev->mutex);

	rtw_info(rtwdev, "start vif %pM on port %d\n", vif->addr, rtwvif->port);
	return 0;
}

static void rtw_ops_remove_interface(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_vif *rtwvif = (struct rtw_vif *)vif->drv_priv;
	u32 config = 0;

	rtw_info(rtwdev, "stop vif %pM on port %d\n", vif->addr, rtwvif->port);

	mutex_lock(&rtwdev->mutex);

	rtw_leave_lps_deep(rtwdev);

	rtw_txq_cleanup(rtwdev, vif->txq);
	rtw_remove_rsvd_page(rtwdev, rtwvif);

	eth_zero_addr(rtwvif->mac_addr);
	config |= PORT_SET_MAC_ADDR;
	rtwvif->net_type = RTW_NET_NO_LINK;
	config |= PORT_SET_NET_TYPE;
	rtwvif->bcn_ctrl = 0;
	config |= PORT_SET_BCN_CTRL;
	rtw_vif_port_config(rtwdev, rtwvif, config);

	mutex_unlock(&rtwdev->mutex);
}

static void rtw_ops_configure_filter(struct ieee80211_hw *hw,
				     unsigned int changed_flags,
				     unsigned int *new_flags,
				     u64 multicast)
{
	struct rtw_dev *rtwdev = hw->priv;

	*new_flags &= FIF_ALLMULTI | FIF_OTHER_BSS | FIF_FCSFAIL |
		      FIF_BCN_PRBRESP_PROMISC;

	mutex_lock(&rtwdev->mutex);

	rtw_leave_lps_deep(rtwdev);

	if (changed_flags & FIF_ALLMULTI) {
		if (*new_flags & FIF_ALLMULTI)
			rtwdev->hal.rcr |= BIT_AM | BIT_AB;
		else
			rtwdev->hal.rcr &= ~(BIT_AM | BIT_AB);
	}
	if (changed_flags & FIF_FCSFAIL) {
		if (*new_flags & FIF_FCSFAIL)
			rtwdev->hal.rcr |= BIT_ACRC32;
		else
			rtwdev->hal.rcr &= ~(BIT_ACRC32);
	}
	if (changed_flags & FIF_OTHER_BSS) {
		if (*new_flags & FIF_OTHER_BSS)
			rtwdev->hal.rcr |= BIT_AAP;
		else
			rtwdev->hal.rcr &= ~(BIT_AAP);
	}
	if (changed_flags & FIF_BCN_PRBRESP_PROMISC) {
		if (*new_flags & FIF_BCN_PRBRESP_PROMISC)
			rtwdev->hal.rcr &= ~(BIT_CBSSID_BCN | BIT_CBSSID_DATA);
		else
			rtwdev->hal.rcr |= BIT_CBSSID_BCN;
	}

	rtw_dbg(rtwdev, RTW_DBG_RX,
		"config rx filter, changed=0x%08x, new=0x%08x, rcr=0x%08x\n",
		changed_flags, *new_flags, rtwdev->hal.rcr);

	rtw_write32(rtwdev, REG_RCR, rtwdev->hal.rcr);

	mutex_unlock(&rtwdev->mutex);
}

/* Only have one group of EDCA parameters now */
static const u32 ac_to_edca_param[IEEE80211_NUM_ACS] = {
	[IEEE80211_AC_VO] = REG_EDCA_VO_PARAM,
	[IEEE80211_AC_VI] = REG_EDCA_VI_PARAM,
	[IEEE80211_AC_BE] = REG_EDCA_BE_PARAM,
	[IEEE80211_AC_BK] = REG_EDCA_BK_PARAM,
};

static u8 rtw_aifsn_to_aifs(struct rtw_dev *rtwdev,
			    struct rtw_vif *rtwvif, u8 aifsn)
{
	struct ieee80211_vif *vif = rtwvif_to_vif(rtwvif);
	u8 slot_time;
	u8 sifs;

	slot_time = vif->bss_conf.use_short_slot ? 9 : 20;
	sifs = rtwdev->hal.current_band_type == RTW_BAND_5G ? 16 : 10;

	return aifsn * slot_time + sifs;
}

static void __rtw_conf_tx(struct rtw_dev *rtwdev,
			  struct rtw_vif *rtwvif, u16 ac)
{
	struct ieee80211_tx_queue_params *params = &rtwvif->tx_params[ac];
	u32 edca_param = ac_to_edca_param[ac];
	u8 ecw_max, ecw_min;
	u8 aifs;

	/* 2^ecw - 1 = cw; ecw = log2(cw + 1) */
	ecw_max = ilog2(params->cw_max + 1);
	ecw_min = ilog2(params->cw_min + 1);
	aifs = rtw_aifsn_to_aifs(rtwdev, rtwvif, params->aifs);
	rtw_write32_mask(rtwdev, edca_param, BIT_MASK_TXOP_LMT, params->txop);
	rtw_write32_mask(rtwdev, edca_param, BIT_MASK_CWMAX, ecw_max);
	rtw_write32_mask(rtwdev, edca_param, BIT_MASK_CWMIN, ecw_min);
	rtw_write32_mask(rtwdev, edca_param, BIT_MASK_AIFS, aifs);
}

static void rtw_conf_tx(struct rtw_dev *rtwdev,
			struct rtw_vif *rtwvif)
{
	u16 ac;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
		__rtw_conf_tx(rtwdev, rtwvif, ac);
}

static void rtw_ops_bss_info_changed(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *conf,
				     u32 changed)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_vif *rtwvif = (struct rtw_vif *)vif->drv_priv;
	u32 config = 0;

	mutex_lock(&rtwdev->mutex);

	rtw_leave_lps_deep(rtwdev);

	if (changed & BSS_CHANGED_ASSOC) {
		enum rtw_net_type net_type;

		if (conf->assoc) {
			rtw_coex_connect_notify(rtwdev, COEX_ASSOCIATE_FINISH);
			net_type = RTW_NET_MGD_LINKED;

			rtwvif->aid = conf->aid;
			rtw_fw_download_rsvd_page(rtwdev);
			rtw_send_rsvd_page_h2c(rtwdev);
			rtw_coex_media_status_notify(rtwdev, conf->assoc);
			if (rtw_bf_support)
				rtw_bf_assoc(rtwdev, vif, conf);
		} else {
			rtw_leave_lps(rtwdev);
			net_type = RTW_NET_NO_LINK;
			rtwvif->aid = 0;
			rtw_bf_disassoc(rtwdev, vif, conf);
		}

		rtwvif->net_type = net_type;
		config |= PORT_SET_NET_TYPE;
		config |= PORT_SET_AID;
	}

	if (changed & BSS_CHANGED_BSSID) {
		ether_addr_copy(rtwvif->bssid, conf->bssid);
		config |= PORT_SET_BSSID;
	}

	if (changed & BSS_CHANGED_BEACON)
		rtw_fw_download_rsvd_page(rtwdev);

	if (changed & BSS_CHANGED_MU_GROUPS)
		rtw_chip_set_gid_table(rtwdev, vif, conf);

	if (changed & BSS_CHANGED_ERP_SLOT)
		rtw_conf_tx(rtwdev, rtwvif);

	rtw_vif_port_config(rtwdev, rtwvif, config);

	mutex_unlock(&rtwdev->mutex);
}

static int rtw_ops_conf_tx(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif, u16 ac,
			   const struct ieee80211_tx_queue_params *params)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_vif *rtwvif = (struct rtw_vif *)vif->drv_priv;

	mutex_lock(&rtwdev->mutex);

	rtw_leave_lps_deep(rtwdev);

	rtwvif->tx_params[ac] = *params;
	__rtw_conf_tx(rtwdev, rtwvif, ac);

	mutex_unlock(&rtwdev->mutex);

	return 0;
}

static u8 rtw_acquire_macid(struct rtw_dev *rtwdev)
{
	unsigned long mac_id;

	mac_id = find_first_zero_bit(rtwdev->mac_id_map, RTW_MAX_MAC_ID_NUM);
	if (mac_id < RTW_MAX_MAC_ID_NUM)
		set_bit(mac_id, rtwdev->mac_id_map);

	return mac_id;
}

static void rtw_release_macid(struct rtw_dev *rtwdev, u8 mac_id)
{
	clear_bit(mac_id, rtwdev->mac_id_map);
}

static int rtw_ops_sta_add(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_sta_info *si = (struct rtw_sta_info *)sta->drv_priv;
	int i;
	int ret = 0;

	mutex_lock(&rtwdev->mutex);

	si->mac_id = rtw_acquire_macid(rtwdev);
	if (si->mac_id >= RTW_MAX_MAC_ID_NUM) {
		ret = -ENOSPC;
		goto out;
	}

	si->sta = sta;
	si->vif = vif;
	si->init_ra_lv = 1;
	ewma_rssi_init(&si->avg_rssi);
	for (i = 0; i < ARRAY_SIZE(sta->txq); i++)
		rtw_txq_init(rtwdev, sta->txq[i]);

	rtw_update_sta_info(rtwdev, si);
	rtw_fw_media_status_report(rtwdev, si->mac_id, true);

	rtwdev->sta_cnt++;

	rtw_info(rtwdev, "sta %pM joined with macid %d\n",
		 sta->addr, si->mac_id);

out:
	mutex_unlock(&rtwdev->mutex);
	return ret;
}

static int rtw_ops_sta_remove(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_sta_info *si = (struct rtw_sta_info *)sta->drv_priv;
	int i;

	mutex_lock(&rtwdev->mutex);

	rtw_release_macid(rtwdev, si->mac_id);
	rtw_fw_media_status_report(rtwdev, si->mac_id, false);

	for (i = 0; i < ARRAY_SIZE(sta->txq); i++)
		rtw_txq_cleanup(rtwdev, sta->txq[i]);

	kfree(si->mask);

	rtwdev->sta_cnt--;

	rtw_info(rtwdev, "sta %pM with macid %d left\n",
		 sta->addr, si->mac_id);

	mutex_unlock(&rtwdev->mutex);
	return 0;
}

static int rtw_ops_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			   struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			   struct ieee80211_key_conf *key)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_sec_desc *sec = &rtwdev->sec;
	u8 hw_key_type;
	u8 hw_key_idx;
	int ret = 0;

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		hw_key_type = RTW_CAM_WEP40;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		hw_key_type = RTW_CAM_WEP104;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		hw_key_type = RTW_CAM_TKIP;
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		hw_key_type = RTW_CAM_AES;
		key->flags |= IEEE80211_KEY_FLAG_SW_MGMT_TX;
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
	case WLAN_CIPHER_SUITE_CCMP_256:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		/* suppress error messages */
		return -EOPNOTSUPP;
	default:
		return -ENOTSUPP;
	}

	mutex_lock(&rtwdev->mutex);

	rtw_leave_lps_deep(rtwdev);

	if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) {
		hw_key_idx = rtw_sec_get_free_cam(sec);
	} else {
		/* multiple interfaces? */
		hw_key_idx = key->keyidx;
	}

	if (hw_key_idx > sec->total_cam_num) {
		ret = -ENOSPC;
		goto out;
	}

	switch (cmd) {
	case SET_KEY:
		/* need sw generated IV */
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
		key->hw_key_idx = hw_key_idx;
		rtw_sec_write_cam(rtwdev, sec, sta, key,
				  hw_key_type, hw_key_idx);
		break;
	case DISABLE_KEY:
		rtw_mac_flush_all_queues(rtwdev, false);
		rtw_sec_clear_cam(rtwdev, sec, key->hw_key_idx);
		break;
	}

	/* download new cam settings for PG to backup */
	if (rtw_fw_lps_deep_mode == LPS_DEEP_MODE_PG)
		rtw_fw_download_rsvd_page(rtwdev);

out:
	mutex_unlock(&rtwdev->mutex);

	return ret;
}

static int rtw_ops_ampdu_action(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_ampdu_params *params)
{
	struct ieee80211_sta *sta = params->sta;
	u16 tid = params->tid;
	struct ieee80211_txq *txq = sta->txq[tid];
	struct rtw_txq *rtwtxq = (struct rtw_txq *)txq->drv_priv;

	switch (params->action) {
	case IEEE80211_AMPDU_TX_START:
		return IEEE80211_AMPDU_TX_START_IMMEDIATE;
	case IEEE80211_AMPDU_TX_STOP_CONT:
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		clear_bit(RTW_TXQ_AMPDU, &rtwtxq->flags);
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		set_bit(RTW_TXQ_AMPDU, &rtwtxq->flags);
		break;
	case IEEE80211_AMPDU_RX_START:
	case IEEE80211_AMPDU_RX_STOP:
		break;
	default:
		WARN_ON(1);
		return -ENOTSUPP;
	}

	return 0;
}

static bool rtw_ops_can_aggregate_in_amsdu(struct ieee80211_hw *hw,
					   struct sk_buff *head,
					   struct sk_buff *skb)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_hal *hal = &rtwdev->hal;

	/* we don't want to enable TX AMSDU on 2.4G */
	if (hal->current_band_type == RTW_BAND_2G)
		return false;

	return true;
}

static void rtw_ops_sw_scan_start(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  const u8 *mac_addr)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_vif *rtwvif = (struct rtw_vif *)vif->drv_priv;
	u32 config = 0;

	mutex_lock(&rtwdev->mutex);

	rtw_leave_lps(rtwdev);

	ether_addr_copy(rtwvif->mac_addr, mac_addr);
	config |= PORT_SET_MAC_ADDR;
	rtw_vif_port_config(rtwdev, rtwvif, config);

	rtw_coex_scan_notify(rtwdev, COEX_SCAN_START);

	set_bit(RTW_FLAG_DIG_DISABLE, rtwdev->flags);
	set_bit(RTW_FLAG_SCANNING, rtwdev->flags);

	mutex_unlock(&rtwdev->mutex);
}

static void rtw_ops_sw_scan_complete(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_vif *rtwvif = (struct rtw_vif *)vif->drv_priv;
	u32 config = 0;

	mutex_lock(&rtwdev->mutex);

	clear_bit(RTW_FLAG_SCANNING, rtwdev->flags);
	clear_bit(RTW_FLAG_DIG_DISABLE, rtwdev->flags);

	ether_addr_copy(rtwvif->mac_addr, vif->addr);
	config |= PORT_SET_MAC_ADDR;
	rtw_vif_port_config(rtwdev, rtwvif, config);

	rtw_coex_scan_notify(rtwdev, COEX_SCAN_FINISH);

	mutex_unlock(&rtwdev->mutex);
}

static void rtw_ops_mgd_prepare_tx(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   u16 duration)
{
	struct rtw_dev *rtwdev = hw->priv;

	mutex_lock(&rtwdev->mutex);
	rtw_leave_lps_deep(rtwdev);
	rtw_coex_connect_notify(rtwdev, COEX_ASSOCIATE_START);
	rtw_chip_prepare_tx(rtwdev);
	mutex_unlock(&rtwdev->mutex);
}

static int rtw_ops_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	struct rtw_dev *rtwdev = hw->priv;

	mutex_lock(&rtwdev->mutex);
	rtwdev->rts_threshold = value;
	mutex_unlock(&rtwdev->mutex);

	return 0;
}

static void rtw_ops_sta_statistics(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_sta *sta,
				   struct station_info *sinfo)
{
	struct rtw_sta_info *si = (struct rtw_sta_info *)sta->drv_priv;

	sinfo->txrate = si->ra_report.txrate;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);
}

static void rtw_ops_flush(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif,
			  u32 queues, bool drop)
{
	struct rtw_dev *rtwdev = hw->priv;

	mutex_lock(&rtwdev->mutex);
	rtw_leave_lps_deep(rtwdev);

	rtw_mac_flush_queues(rtwdev, queues, drop);
	mutex_unlock(&rtwdev->mutex);
}

struct rtw_iter_bitrate_mask_data {
	struct rtw_dev *rtwdev;
	struct ieee80211_vif *vif;
	const struct cfg80211_bitrate_mask *mask;
};

static void rtw_ra_mask_info_update_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw_iter_bitrate_mask_data *br_data = data;
	struct rtw_sta_info *si = (struct rtw_sta_info *)sta->drv_priv;

	if (si->vif != br_data->vif)
		return;

	/* free previous mask setting */
	kfree(si->mask);
	si->mask = kmemdup(br_data->mask, sizeof(struct cfg80211_bitrate_mask),
			   GFP_ATOMIC);
	if (!si->mask) {
		si->use_cfg_mask = false;
		return;
	}

	si->use_cfg_mask = true;
	rtw_update_sta_info(br_data->rtwdev, si);
}

static void rtw_ra_mask_info_update(struct rtw_dev *rtwdev,
				    struct ieee80211_vif *vif,
				    const struct cfg80211_bitrate_mask *mask)
{
	struct rtw_iter_bitrate_mask_data br_data;

	br_data.rtwdev = rtwdev;
	br_data.vif = vif;
	br_data.mask = mask;
	rtw_iterate_stas_atomic(rtwdev, rtw_ra_mask_info_update_iter, &br_data);
}

static int rtw_ops_set_bitrate_mask(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    const struct cfg80211_bitrate_mask *mask)
{
	struct rtw_dev *rtwdev = hw->priv;

	rtw_ra_mask_info_update(rtwdev, vif, mask);

	return 0;
}

static int rtw_ops_set_antenna(struct ieee80211_hw *hw,
			       u32 tx_antenna,
			       u32 rx_antenna)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_chip_info *chip = rtwdev->chip;
	int ret;

	if (!chip->ops->set_antenna)
		return -EOPNOTSUPP;

	mutex_lock(&rtwdev->mutex);
	ret = chip->ops->set_antenna(rtwdev, tx_antenna, rx_antenna);
	mutex_unlock(&rtwdev->mutex);

	return ret;
}

static int rtw_ops_get_antenna(struct ieee80211_hw *hw,
			       u32 *tx_antenna,
			       u32 *rx_antenna)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct rtw_hal *hal = &rtwdev->hal;

	*tx_antenna = hal->antenna_tx;
	*rx_antenna = hal->antenna_rx;

	return 0;
}

#ifdef CONFIG_PM
static int rtw_ops_suspend(struct ieee80211_hw *hw,
			   struct cfg80211_wowlan *wowlan)
{
	struct rtw_dev *rtwdev = hw->priv;
	int ret;

	mutex_lock(&rtwdev->mutex);
	ret = rtw_wow_suspend(rtwdev, wowlan);
	if (ret)
		rtw_err(rtwdev, "failed to suspend for wow %d\n", ret);
	mutex_unlock(&rtwdev->mutex);

	return ret ? 1 : 0;
}

static int rtw_ops_resume(struct ieee80211_hw *hw)
{
	struct rtw_dev *rtwdev = hw->priv;
	int ret;

	mutex_lock(&rtwdev->mutex);
	ret = rtw_wow_resume(rtwdev);
	if (ret)
		rtw_err(rtwdev, "failed to resume for wow %d\n", ret);
	mutex_unlock(&rtwdev->mutex);

	return ret ? 1 : 0;
}

static void rtw_ops_set_wakeup(struct ieee80211_hw *hw, bool enabled)
{
	struct rtw_dev *rtwdev = hw->priv;

	device_set_wakeup_enable(rtwdev->dev, enabled);
}
#endif

const struct ieee80211_ops rtw_ops = {
	.tx			= rtw_ops_tx,
	.wake_tx_queue		= rtw_ops_wake_tx_queue,
	.start			= rtw_ops_start,
	.stop			= rtw_ops_stop,
	.config			= rtw_ops_config,
	.add_interface		= rtw_ops_add_interface,
	.remove_interface	= rtw_ops_remove_interface,
	.configure_filter	= rtw_ops_configure_filter,
	.bss_info_changed	= rtw_ops_bss_info_changed,
	.conf_tx		= rtw_ops_conf_tx,
	.sta_add		= rtw_ops_sta_add,
	.sta_remove		= rtw_ops_sta_remove,
	.set_key		= rtw_ops_set_key,
	.ampdu_action		= rtw_ops_ampdu_action,
	.can_aggregate_in_amsdu	= rtw_ops_can_aggregate_in_amsdu,
	.sw_scan_start		= rtw_ops_sw_scan_start,
	.sw_scan_complete	= rtw_ops_sw_scan_complete,
	.mgd_prepare_tx		= rtw_ops_mgd_prepare_tx,
	.set_rts_threshold	= rtw_ops_set_rts_threshold,
	.sta_statistics		= rtw_ops_sta_statistics,
	.flush			= rtw_ops_flush,
	.set_bitrate_mask	= rtw_ops_set_bitrate_mask,
	.set_antenna		= rtw_ops_set_antenna,
	.get_antenna		= rtw_ops_get_antenna,
#ifdef CONFIG_PM
	.suspend		= rtw_ops_suspend,
	.resume			= rtw_ops_resume,
	.set_wakeup		= rtw_ops_set_wakeup,
#endif
};
EXPORT_SYMBOL(rtw_ops);
