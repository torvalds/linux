// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "cam.h"
#include "coex.h"
#include "debug.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "ps.h"
#include "reg.h"
#include "sar.h"
#include "ser.h"

static void rtw89_ops_tx(struct ieee80211_hw *hw,
			 struct ieee80211_tx_control *control,
			 struct sk_buff *skb)
{
	struct rtw89_dev *rtwdev = hw->priv;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif;
	struct ieee80211_sta *sta = control->sta;
	int ret, qsel;

	ret = rtw89_core_tx_write(rtwdev, vif, sta, skb, &qsel);
	if (ret) {
		rtw89_err(rtwdev, "failed to transmit skb: %d\n", ret);
		ieee80211_free_txskb(hw, skb);
		return;
	}
	rtw89_core_tx_kick_off(rtwdev, qsel);
}

static void rtw89_ops_wake_tx_queue(struct ieee80211_hw *hw,
				    struct ieee80211_txq *txq)
{
	struct rtw89_dev *rtwdev = hw->priv;

	ieee80211_schedule_txq(hw, txq);
	queue_work(rtwdev->txq_wq, &rtwdev->txq_work);
}

static int rtw89_ops_start(struct ieee80211_hw *hw)
{
	struct rtw89_dev *rtwdev = hw->priv;
	int ret;

	mutex_lock(&rtwdev->mutex);
	ret = rtw89_core_start(rtwdev);
	mutex_unlock(&rtwdev->mutex);

	return ret;
}

static void rtw89_ops_stop(struct ieee80211_hw *hw)
{
	struct rtw89_dev *rtwdev = hw->priv;

	mutex_lock(&rtwdev->mutex);
	rtw89_core_stop(rtwdev);
	mutex_unlock(&rtwdev->mutex);
}

static int rtw89_ops_config(struct ieee80211_hw *hw, u32 changed)
{
	struct rtw89_dev *rtwdev = hw->priv;

	/* let previous ips work finish to ensure we don't leave ips twice */
	cancel_work_sync(&rtwdev->ips_work);

	mutex_lock(&rtwdev->mutex);
	rtw89_leave_ps_mode(rtwdev);

	if ((changed & IEEE80211_CONF_CHANGE_IDLE) &&
	    !(hw->conf.flags & IEEE80211_CONF_IDLE))
		rtw89_leave_ips(rtwdev);

	if (changed & IEEE80211_CONF_CHANGE_PS) {
		if (hw->conf.flags & IEEE80211_CONF_PS) {
			rtwdev->lps_enabled = true;
		} else {
			rtw89_leave_lps(rtwdev);
			rtwdev->lps_enabled = false;
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL)
		rtw89_set_channel(rtwdev);

	if ((changed & IEEE80211_CONF_CHANGE_IDLE) &&
	    (hw->conf.flags & IEEE80211_CONF_IDLE))
		rtw89_enter_ips(rtwdev);

	mutex_unlock(&rtwdev->mutex);

	return 0;
}

static int rtw89_ops_add_interface(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif)
{
	struct rtw89_dev *rtwdev = hw->priv;
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	int ret = 0;

	mutex_lock(&rtwdev->mutex);
	rtwvif->rtwdev = rtwdev;
	list_add_tail(&rtwvif->list, &rtwdev->rtwvifs_list);
	INIT_WORK(&rtwvif->update_beacon_work, rtw89_core_update_beacon_work);
	rtw89_leave_ps_mode(rtwdev);

	rtw89_traffic_stats_init(rtwdev, &rtwvif->stats);
	rtw89_vif_type_mapping(vif, false);
	rtwvif->port = rtw89_core_acquire_bit_map(rtwdev->hw_port,
						  RTW89_PORT_NUM);
	if (rtwvif->port == RTW89_PORT_NUM) {
		ret = -ENOSPC;
		goto out;
	}

	rtwvif->bcn_hit_cond = 0;
	rtwvif->mac_idx = RTW89_MAC_0;
	rtwvif->phy_idx = RTW89_PHY_0;
	rtwvif->hit_rule = 0;
	ether_addr_copy(rtwvif->mac_addr, vif->addr);

	ret = rtw89_mac_add_vif(rtwdev, rtwvif);
	if (ret) {
		rtw89_core_release_bit_map(rtwdev->hw_port, rtwvif->port);
		goto out;
	}

	rtw89_core_txq_init(rtwdev, vif->txq);

	rtw89_btc_ntfy_role_info(rtwdev, rtwvif, NULL, BTC_ROLE_START);
out:
	mutex_unlock(&rtwdev->mutex);

	return ret;
}

static void rtw89_ops_remove_interface(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
	struct rtw89_dev *rtwdev = hw->priv;
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;

	cancel_work_sync(&rtwvif->update_beacon_work);

	mutex_lock(&rtwdev->mutex);
	rtw89_leave_ps_mode(rtwdev);
	rtw89_btc_ntfy_role_info(rtwdev, rtwvif, NULL, BTC_ROLE_STOP);
	rtw89_mac_remove_vif(rtwdev, rtwvif);
	rtw89_core_release_bit_map(rtwdev->hw_port, rtwvif->port);
	list_del_init(&rtwvif->list);
	mutex_unlock(&rtwdev->mutex);
}

static void rtw89_ops_configure_filter(struct ieee80211_hw *hw,
				       unsigned int changed_flags,
				       unsigned int *new_flags,
				       u64 multicast)
{
	struct rtw89_dev *rtwdev = hw->priv;

	mutex_lock(&rtwdev->mutex);
	rtw89_leave_ps_mode(rtwdev);

	*new_flags &= FIF_ALLMULTI | FIF_OTHER_BSS | FIF_FCSFAIL |
		      FIF_BCN_PRBRESP_PROMISC | FIF_PROBE_REQ;

	if (changed_flags & FIF_ALLMULTI) {
		if (*new_flags & FIF_ALLMULTI)
			rtwdev->hal.rx_fltr &= ~B_AX_A_MC;
		else
			rtwdev->hal.rx_fltr |= B_AX_A_MC;
	}
	if (changed_flags & FIF_FCSFAIL) {
		if (*new_flags & FIF_FCSFAIL)
			rtwdev->hal.rx_fltr |= B_AX_A_CRC32_ERR;
		else
			rtwdev->hal.rx_fltr &= ~B_AX_A_CRC32_ERR;
	}
	if (changed_flags & FIF_OTHER_BSS) {
		if (*new_flags & FIF_OTHER_BSS)
			rtwdev->hal.rx_fltr &= ~B_AX_A_A1_MATCH;
		else
			rtwdev->hal.rx_fltr |= B_AX_A_A1_MATCH;
	}
	if (changed_flags & FIF_BCN_PRBRESP_PROMISC) {
		if (*new_flags & FIF_BCN_PRBRESP_PROMISC) {
			rtwdev->hal.rx_fltr &= ~B_AX_A_BCN_CHK_EN;
			rtwdev->hal.rx_fltr &= ~B_AX_A_BC;
			rtwdev->hal.rx_fltr &= ~B_AX_A_A1_MATCH;
		} else {
			rtwdev->hal.rx_fltr |= B_AX_A_BCN_CHK_EN;
			rtwdev->hal.rx_fltr |= B_AX_A_BC;
			rtwdev->hal.rx_fltr |= B_AX_A_A1_MATCH;
		}
	}
	if (changed_flags & FIF_PROBE_REQ) {
		if (*new_flags & FIF_PROBE_REQ) {
			rtwdev->hal.rx_fltr &= ~B_AX_A_BC_CAM_MATCH;
			rtwdev->hal.rx_fltr &= ~B_AX_A_UC_CAM_MATCH;
		} else {
			rtwdev->hal.rx_fltr |= B_AX_A_BC_CAM_MATCH;
			rtwdev->hal.rx_fltr |= B_AX_A_UC_CAM_MATCH;
		}
	}

	rtw89_write32_mask(rtwdev,
			   rtw89_mac_reg_by_idx(R_AX_RX_FLTR_OPT, RTW89_MAC_0),
			   B_AX_RX_FLTR_CFG_MASK,
			   rtwdev->hal.rx_fltr);
	if (!rtwdev->dbcc_en)
		goto out;
	rtw89_write32_mask(rtwdev,
			   rtw89_mac_reg_by_idx(R_AX_RX_FLTR_OPT, RTW89_MAC_1),
			   B_AX_RX_FLTR_CFG_MASK,
			   rtwdev->hal.rx_fltr);

out:
	mutex_unlock(&rtwdev->mutex);
}

static const u8 ac_to_fw_idx[IEEE80211_NUM_ACS] = {
	[IEEE80211_AC_VO] = 3,
	[IEEE80211_AC_VI] = 2,
	[IEEE80211_AC_BE] = 0,
	[IEEE80211_AC_BK] = 1,
};

static u8 rtw89_aifsn_to_aifs(struct rtw89_dev *rtwdev,
			      struct rtw89_vif *rtwvif, u8 aifsn)
{
	struct ieee80211_vif *vif = rtwvif_to_vif(rtwvif);
	u8 slot_time;
	u8 sifs;

	slot_time = vif->bss_conf.use_short_slot ? 9 : 20;
	sifs = rtwdev->hal.current_band_type == RTW89_BAND_5G ? 16 : 10;

	return aifsn * slot_time + sifs;
}

static void ____rtw89_conf_tx_edca(struct rtw89_dev *rtwdev,
				   struct rtw89_vif *rtwvif, u16 ac)
{
	struct ieee80211_tx_queue_params *params = &rtwvif->tx_params[ac];
	u32 val;
	u8 ecw_max, ecw_min;
	u8 aifs;

	/* 2^ecw - 1 = cw; ecw = log2(cw + 1) */
	ecw_max = ilog2(params->cw_max + 1);
	ecw_min = ilog2(params->cw_min + 1);
	aifs = rtw89_aifsn_to_aifs(rtwdev, rtwvif, params->aifs);
	val = FIELD_PREP(FW_EDCA_PARAM_TXOPLMT_MSK, params->txop) |
	      FIELD_PREP(FW_EDCA_PARAM_CWMAX_MSK, ecw_max) |
	      FIELD_PREP(FW_EDCA_PARAM_CWMIN_MSK, ecw_min) |
	      FIELD_PREP(FW_EDCA_PARAM_AIFS_MSK, aifs);
	rtw89_fw_h2c_set_edca(rtwdev, rtwvif, ac_to_fw_idx[ac], val);
}

static const u32 ac_to_mu_edca_param[IEEE80211_NUM_ACS] = {
	[IEEE80211_AC_VO] = R_AX_MUEDCA_VO_PARAM_0,
	[IEEE80211_AC_VI] = R_AX_MUEDCA_VI_PARAM_0,
	[IEEE80211_AC_BE] = R_AX_MUEDCA_BE_PARAM_0,
	[IEEE80211_AC_BK] = R_AX_MUEDCA_BK_PARAM_0,
};

static void ____rtw89_conf_tx_mu_edca(struct rtw89_dev *rtwdev,
				      struct rtw89_vif *rtwvif, u16 ac)
{
	struct ieee80211_tx_queue_params *params = &rtwvif->tx_params[ac];
	struct ieee80211_he_mu_edca_param_ac_rec *mu_edca;
	u8 aifs, aifsn;
	u16 timer_32us;
	u32 reg;
	u32 val;

	if (!params->mu_edca)
		return;

	mu_edca = &params->mu_edca_param_rec;
	aifsn = FIELD_GET(GENMASK(3, 0), mu_edca->aifsn);
	aifs = aifsn ? rtw89_aifsn_to_aifs(rtwdev, rtwvif, aifsn) : 0;
	timer_32us = mu_edca->mu_edca_timer << 8;

	val = FIELD_PREP(B_AX_MUEDCA_BE_PARAM_0_TIMER_MASK, timer_32us) |
	      FIELD_PREP(B_AX_MUEDCA_BE_PARAM_0_CW_MASK, mu_edca->ecw_min_max) |
	      FIELD_PREP(B_AX_MUEDCA_BE_PARAM_0_AIFS_MASK, aifs);
	reg = rtw89_mac_reg_by_idx(ac_to_mu_edca_param[ac], rtwvif->mac_idx);
	rtw89_write32(rtwdev, reg, val);

	rtw89_mac_set_hw_muedca_ctrl(rtwdev, rtwvif, true);
}

static void __rtw89_conf_tx(struct rtw89_dev *rtwdev,
			    struct rtw89_vif *rtwvif, u16 ac)
{
	____rtw89_conf_tx_edca(rtwdev, rtwvif, ac);
	____rtw89_conf_tx_mu_edca(rtwdev, rtwvif, ac);
}

static void rtw89_conf_tx(struct rtw89_dev *rtwdev,
			  struct rtw89_vif *rtwvif)
{
	u16 ac;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
		__rtw89_conf_tx(rtwdev, rtwvif, ac);
}

static void rtw89_station_mode_sta_assoc(struct rtw89_dev *rtwdev,
					 struct ieee80211_vif *vif,
					 struct ieee80211_bss_conf *conf)
{
	struct ieee80211_sta *sta;

	if (vif->type != NL80211_IFTYPE_STATION)
		return;

	sta = ieee80211_find_sta(vif, conf->bssid);
	if (!sta) {
		rtw89_err(rtwdev, "can't find sta to set sta_assoc state\n");
		return;
	}

	rtw89_vif_type_mapping(vif, true);

	rtw89_core_sta_assoc(rtwdev, vif, sta);
}

static void rtw89_ops_bss_info_changed(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_bss_conf *conf,
				       u32 changed)
{
	struct rtw89_dev *rtwdev = hw->priv;
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;

	mutex_lock(&rtwdev->mutex);
	rtw89_leave_ps_mode(rtwdev);

	if (changed & BSS_CHANGED_ASSOC) {
		if (conf->assoc) {
			rtw89_station_mode_sta_assoc(rtwdev, vif, conf);
			rtw89_phy_set_bss_color(rtwdev, vif);
			rtw89_chip_cfg_txpwr_ul_tb_offset(rtwdev, vif);
			rtw89_mac_port_update(rtwdev, rtwvif);
			rtw89_store_op_chan(rtwdev);
		} else {
			/* Abort ongoing scan if cancel_scan isn't issued
			 * when disconnected by peer
			 */
			if (rtwdev->scanning)
				rtw89_hw_scan_abort(rtwdev, vif);
		}
	}

	if (changed & BSS_CHANGED_BSSID) {
		ether_addr_copy(rtwvif->bssid, conf->bssid);
		rtw89_cam_bssid_changed(rtwdev, rtwvif);
		rtw89_fw_h2c_cam(rtwdev, rtwvif, NULL, NULL);
	}

	if (changed & BSS_CHANGED_BEACON)
		rtw89_fw_h2c_update_beacon(rtwdev, rtwvif);

	if (changed & BSS_CHANGED_ERP_SLOT)
		rtw89_conf_tx(rtwdev, rtwvif);

	if (changed & BSS_CHANGED_HE_BSS_COLOR)
		rtw89_phy_set_bss_color(rtwdev, vif);

	if (changed & BSS_CHANGED_MU_GROUPS)
		rtw89_mac_bf_set_gid_table(rtwdev, vif, conf);

	mutex_unlock(&rtwdev->mutex);
}

static int rtw89_ops_start_ap(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct rtw89_dev *rtwdev = hw->priv;
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;

	mutex_lock(&rtwdev->mutex);
	ether_addr_copy(rtwvif->bssid, vif->bss_conf.bssid);
	rtw89_cam_bssid_changed(rtwdev, rtwvif);
	rtw89_mac_port_update(rtwdev, rtwvif);
	rtw89_fw_h2c_assoc_cmac_tbl(rtwdev, vif, NULL);
	rtw89_fw_h2c_role_maintain(rtwdev, rtwvif, NULL, RTW89_ROLE_TYPE_CHANGE);
	rtw89_fw_h2c_join_info(rtwdev, rtwvif, NULL, true);
	rtw89_fw_h2c_cam(rtwdev, rtwvif, NULL, NULL);
	rtw89_chip_rfk_channel(rtwdev);
	mutex_unlock(&rtwdev->mutex);

	return 0;
}

static
void rtw89_ops_stop_ap(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct rtw89_dev *rtwdev = hw->priv;
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;

	mutex_lock(&rtwdev->mutex);
	rtw89_fw_h2c_assoc_cmac_tbl(rtwdev, vif, NULL);
	rtw89_fw_h2c_join_info(rtwdev, rtwvif, NULL, true);
	mutex_unlock(&rtwdev->mutex);
}

static int rtw89_ops_set_tim(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
			     bool set)
{
	struct rtw89_dev *rtwdev = hw->priv;
	struct rtw89_sta *rtwsta = (struct rtw89_sta *)sta->drv_priv;
	struct rtw89_vif *rtwvif = rtwsta->rtwvif;

	ieee80211_queue_work(rtwdev->hw, &rtwvif->update_beacon_work);

	return 0;
}

static int rtw89_ops_conf_tx(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif, u16 ac,
			     const struct ieee80211_tx_queue_params *params)
{
	struct rtw89_dev *rtwdev = hw->priv;
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;

	mutex_lock(&rtwdev->mutex);
	rtw89_leave_ps_mode(rtwdev);
	rtwvif->tx_params[ac] = *params;
	__rtw89_conf_tx(rtwdev, rtwvif, ac);
	mutex_unlock(&rtwdev->mutex);

	return 0;
}

static int __rtw89_ops_sta_state(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta,
				 enum ieee80211_sta_state old_state,
				 enum ieee80211_sta_state new_state)
{
	struct rtw89_dev *rtwdev = hw->priv;

	if (old_state == IEEE80211_STA_NOTEXIST &&
	    new_state == IEEE80211_STA_NONE)
		return rtw89_core_sta_add(rtwdev, vif, sta);

	if (old_state == IEEE80211_STA_AUTH &&
	    new_state == IEEE80211_STA_ASSOC) {
		if (vif->type == NL80211_IFTYPE_STATION)
			return 0; /* defer to bss_info_changed to have vif info */
		return rtw89_core_sta_assoc(rtwdev, vif, sta);
	}

	if (old_state == IEEE80211_STA_ASSOC &&
	    new_state == IEEE80211_STA_AUTH)
		return rtw89_core_sta_disassoc(rtwdev, vif, sta);

	if (old_state == IEEE80211_STA_AUTH &&
	    new_state == IEEE80211_STA_NONE)
		return rtw89_core_sta_disconnect(rtwdev, vif, sta);

	if (old_state == IEEE80211_STA_NONE &&
	    new_state == IEEE80211_STA_NOTEXIST)
		return rtw89_core_sta_remove(rtwdev, vif, sta);

	return 0;
}

static int rtw89_ops_sta_state(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta,
			       enum ieee80211_sta_state old_state,
			       enum ieee80211_sta_state new_state)
{
	struct rtw89_dev *rtwdev = hw->priv;
	int ret;

	mutex_lock(&rtwdev->mutex);
	rtw89_leave_ps_mode(rtwdev);
	ret = __rtw89_ops_sta_state(hw, vif, sta, old_state, new_state);
	mutex_unlock(&rtwdev->mutex);

	return ret;
}

static int rtw89_ops_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta,
			     struct ieee80211_key_conf *key)
{
	struct rtw89_dev *rtwdev = hw->priv;
	int ret = 0;

	mutex_lock(&rtwdev->mutex);
	rtw89_leave_ps_mode(rtwdev);

	switch (cmd) {
	case SET_KEY:
		rtw89_btc_ntfy_specific_packet(rtwdev, PACKET_EAPOL_END);
		ret = rtw89_cam_sec_key_add(rtwdev, vif, sta, key);
		if (ret && ret != -EOPNOTSUPP) {
			rtw89_err(rtwdev, "failed to add key to sec cam\n");
			goto out;
		}
		break;
	case DISABLE_KEY:
		rtw89_hci_flush_queues(rtwdev, BIT(rtwdev->hw->queues) - 1,
				       false);
		rtw89_mac_flush_txq(rtwdev, BIT(rtwdev->hw->queues) - 1, false);
		ret = rtw89_cam_sec_key_del(rtwdev, vif, sta, key, true);
		if (ret) {
			rtw89_err(rtwdev, "failed to remove key from sec cam\n");
			goto out;
		}
		break;
	}

out:
	mutex_unlock(&rtwdev->mutex);

	return ret;
}

static int rtw89_ops_ampdu_action(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_ampdu_params *params)
{
	struct rtw89_dev *rtwdev = hw->priv;
	struct ieee80211_sta *sta = params->sta;
	struct rtw89_sta *rtwsta = (struct rtw89_sta *)sta->drv_priv;
	u16 tid = params->tid;
	struct ieee80211_txq *txq = sta->txq[tid];
	struct rtw89_txq *rtwtxq = (struct rtw89_txq *)txq->drv_priv;

	switch (params->action) {
	case IEEE80211_AMPDU_TX_START:
		return IEEE80211_AMPDU_TX_START_IMMEDIATE;
	case IEEE80211_AMPDU_TX_STOP_CONT:
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		mutex_lock(&rtwdev->mutex);
		clear_bit(RTW89_TXQ_F_AMPDU, &rtwtxq->flags);
		mutex_unlock(&rtwdev->mutex);
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		mutex_lock(&rtwdev->mutex);
		set_bit(RTW89_TXQ_F_AMPDU, &rtwtxq->flags);
		rtwsta->ampdu_params[tid].agg_num = params->buf_size;
		rtwsta->ampdu_params[tid].amsdu = params->amsdu;
		rtw89_leave_ps_mode(rtwdev);
		mutex_unlock(&rtwdev->mutex);
		break;
	case IEEE80211_AMPDU_RX_START:
		mutex_lock(&rtwdev->mutex);
		rtw89_fw_h2c_ba_cam(rtwdev, rtwsta, true, params);
		mutex_unlock(&rtwdev->mutex);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		mutex_lock(&rtwdev->mutex);
		rtw89_fw_h2c_ba_cam(rtwdev, rtwsta, false, params);
		mutex_unlock(&rtwdev->mutex);
		break;
	default:
		WARN_ON(1);
		return -ENOTSUPP;
	}

	return 0;
}

static int rtw89_ops_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	struct rtw89_dev *rtwdev = hw->priv;

	mutex_lock(&rtwdev->mutex);
	rtw89_leave_ps_mode(rtwdev);
	if (test_bit(RTW89_FLAG_POWERON, rtwdev->flags))
		rtw89_mac_update_rts_threshold(rtwdev, RTW89_MAC_0);
	mutex_unlock(&rtwdev->mutex);

	return 0;
}

static void rtw89_ops_sta_statistics(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_sta *sta,
				     struct station_info *sinfo)
{
	struct rtw89_sta *rtwsta = (struct rtw89_sta *)sta->drv_priv;

	sinfo->txrate = rtwsta->ra_report.txrate;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);
}

static void rtw89_ops_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    u32 queues, bool drop)
{
	struct rtw89_dev *rtwdev = hw->priv;

	mutex_lock(&rtwdev->mutex);
	rtw89_leave_lps(rtwdev);
	rtw89_hci_flush_queues(rtwdev, queues, drop);
	rtw89_mac_flush_txq(rtwdev, queues, drop);
	mutex_unlock(&rtwdev->mutex);
}

struct rtw89_iter_bitrate_mask_data {
	struct rtw89_dev *rtwdev;
	struct ieee80211_vif *vif;
	const struct cfg80211_bitrate_mask *mask;
};

static void rtw89_ra_mask_info_update_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw89_iter_bitrate_mask_data *br_data = data;
	struct rtw89_sta *rtwsta = (struct rtw89_sta *)sta->drv_priv;
	struct ieee80211_vif *vif = rtwvif_to_vif(rtwsta->rtwvif);

	if (vif != br_data->vif)
		return;

	rtwsta->use_cfg_mask = true;
	rtwsta->mask = *br_data->mask;
	rtw89_phy_ra_updata_sta(br_data->rtwdev, sta);
}

static void rtw89_ra_mask_info_update(struct rtw89_dev *rtwdev,
				      struct ieee80211_vif *vif,
				      const struct cfg80211_bitrate_mask *mask)
{
	struct rtw89_iter_bitrate_mask_data br_data = { .rtwdev = rtwdev,
							.vif = vif,
							.mask = mask};

	ieee80211_iterate_stations_atomic(rtwdev->hw, rtw89_ra_mask_info_update_iter,
					  &br_data);
}

static int rtw89_ops_set_bitrate_mask(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      const struct cfg80211_bitrate_mask *mask)
{
	struct rtw89_dev *rtwdev = hw->priv;

	mutex_lock(&rtwdev->mutex);
	rtw89_phy_rate_pattern_vif(rtwdev, vif, mask);
	rtw89_ra_mask_info_update(rtwdev, vif, mask);
	mutex_unlock(&rtwdev->mutex);

	return 0;
}

static
int rtw89_ops_set_antenna(struct ieee80211_hw *hw, u32 tx_ant, u32 rx_ant)
{
	struct rtw89_dev *rtwdev = hw->priv;
	struct rtw89_hal *hal = &rtwdev->hal;

	if (rx_ant != hw->wiphy->available_antennas_rx)
		return -EINVAL;

	mutex_lock(&rtwdev->mutex);
	hal->antenna_tx = tx_ant;
	hal->antenna_rx = rx_ant;
	mutex_unlock(&rtwdev->mutex);

	return 0;
}

static
int rtw89_ops_get_antenna(struct ieee80211_hw *hw,  u32 *tx_ant, u32 *rx_ant)
{
	struct rtw89_dev *rtwdev = hw->priv;
	struct rtw89_hal *hal = &rtwdev->hal;

	*tx_ant = hal->antenna_tx;
	*rx_ant = hal->antenna_rx;

	return 0;
}

static void rtw89_ops_sw_scan_start(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    const u8 *mac_addr)
{
	struct rtw89_dev *rtwdev = hw->priv;
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;

	mutex_lock(&rtwdev->mutex);
	rtw89_core_scan_start(rtwdev, rtwvif, mac_addr, false);
	mutex_unlock(&rtwdev->mutex);
}

static void rtw89_ops_sw_scan_complete(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
	struct rtw89_dev *rtwdev = hw->priv;

	mutex_lock(&rtwdev->mutex);
	rtw89_core_scan_complete(rtwdev, vif, false);
	mutex_unlock(&rtwdev->mutex);
}

static void rtw89_ops_reconfig_complete(struct ieee80211_hw *hw,
					enum ieee80211_reconfig_type reconfig_type)
{
	struct rtw89_dev *rtwdev = hw->priv;

	if (reconfig_type == IEEE80211_RECONFIG_TYPE_RESTART)
		rtw89_ser_recfg_done(rtwdev);
}

static int rtw89_ops_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			     struct ieee80211_scan_request *req)
{
	struct rtw89_dev *rtwdev = hw->priv;
	int ret = 0;

	if (!rtwdev->fw.scan_offload)
		return 1;

	if (rtwdev->scanning)
		return -EBUSY;

	mutex_lock(&rtwdev->mutex);
	rtw89_hw_scan_start(rtwdev, vif, req);
	ret = rtw89_hw_scan_offload(rtwdev, vif, true);
	if (ret) {
		rtw89_hw_scan_abort(rtwdev, vif);
		rtw89_err(rtwdev, "HW scan failed with status: %d\n", ret);
	}
	mutex_unlock(&rtwdev->mutex);

	return ret;
}

static void rtw89_ops_cancel_hw_scan(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif)
{
	struct rtw89_dev *rtwdev = hw->priv;

	if (!rtwdev->fw.scan_offload)
		return;

	if (!rtwdev->scanning)
		return;

	mutex_lock(&rtwdev->mutex);
	rtw89_hw_scan_abort(rtwdev, vif);
	mutex_unlock(&rtwdev->mutex);
}

const struct ieee80211_ops rtw89_ops = {
	.tx			= rtw89_ops_tx,
	.wake_tx_queue		= rtw89_ops_wake_tx_queue,
	.start			= rtw89_ops_start,
	.stop			= rtw89_ops_stop,
	.config			= rtw89_ops_config,
	.add_interface		= rtw89_ops_add_interface,
	.remove_interface	= rtw89_ops_remove_interface,
	.configure_filter	= rtw89_ops_configure_filter,
	.bss_info_changed	= rtw89_ops_bss_info_changed,
	.start_ap		= rtw89_ops_start_ap,
	.stop_ap		= rtw89_ops_stop_ap,
	.set_tim		= rtw89_ops_set_tim,
	.conf_tx		= rtw89_ops_conf_tx,
	.sta_state		= rtw89_ops_sta_state,
	.set_key		= rtw89_ops_set_key,
	.ampdu_action		= rtw89_ops_ampdu_action,
	.set_rts_threshold	= rtw89_ops_set_rts_threshold,
	.sta_statistics		= rtw89_ops_sta_statistics,
	.flush			= rtw89_ops_flush,
	.set_bitrate_mask	= rtw89_ops_set_bitrate_mask,
	.set_antenna		= rtw89_ops_set_antenna,
	.get_antenna		= rtw89_ops_get_antenna,
	.sw_scan_start		= rtw89_ops_sw_scan_start,
	.sw_scan_complete	= rtw89_ops_sw_scan_complete,
	.reconfig_complete	= rtw89_ops_reconfig_complete,
	.hw_scan		= rtw89_ops_hw_scan,
	.cancel_hw_scan		= rtw89_ops_cancel_hw_scan,
	.set_sar_specs		= rtw89_ops_set_sar_specs,
};
EXPORT_SYMBOL(rtw89_ops);
