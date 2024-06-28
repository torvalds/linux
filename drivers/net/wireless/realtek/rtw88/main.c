// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include <linux/devcoredump.h>

#include "main.h"
#include "regd.h"
#include "fw.h"
#include "ps.h"
#include "sec.h"
#include "mac.h"
#include "coex.h"
#include "phy.h"
#include "reg.h"
#include "efuse.h"
#include "tx.h"
#include "debug.h"
#include "bf.h"
#include "sar.h"

bool rtw_disable_lps_deep_mode;
EXPORT_SYMBOL(rtw_disable_lps_deep_mode);
bool rtw_bf_support = true;
unsigned int rtw_debug_mask;
EXPORT_SYMBOL(rtw_debug_mask);
/* EDCCA is enabled during normal behavior. For debugging purpose in
 * a noisy environment, it can be disabled via edcca debugfs. Because
 * all rtw88 devices will probably be affected if environment is noisy,
 * rtw_edcca_enabled is just declared by driver instead of by device.
 * So, turning it off will take effect for all rtw88 devices before
 * there is a tough reason to maintain rtw_edcca_enabled by device.
 */
bool rtw_edcca_enabled = true;

module_param_named(disable_lps_deep, rtw_disable_lps_deep_mode, bool, 0644);
module_param_named(support_bf, rtw_bf_support, bool, 0644);
module_param_named(debug_mask, rtw_debug_mask, uint, 0644);

MODULE_PARM_DESC(disable_lps_deep, "Set Y to disable Deep PS");
MODULE_PARM_DESC(support_bf, "Set Y to enable beamformee support");
MODULE_PARM_DESC(debug_mask, "Debugging mask");

static struct ieee80211_channel rtw_channeltable_2g[] = {
	{.center_freq = 2412, .hw_value = 1,},
	{.center_freq = 2417, .hw_value = 2,},
	{.center_freq = 2422, .hw_value = 3,},
	{.center_freq = 2427, .hw_value = 4,},
	{.center_freq = 2432, .hw_value = 5,},
	{.center_freq = 2437, .hw_value = 6,},
	{.center_freq = 2442, .hw_value = 7,},
	{.center_freq = 2447, .hw_value = 8,},
	{.center_freq = 2452, .hw_value = 9,},
	{.center_freq = 2457, .hw_value = 10,},
	{.center_freq = 2462, .hw_value = 11,},
	{.center_freq = 2467, .hw_value = 12,},
	{.center_freq = 2472, .hw_value = 13,},
	{.center_freq = 2484, .hw_value = 14,},
};

static struct ieee80211_channel rtw_channeltable_5g[] = {
	{.center_freq = 5180, .hw_value = 36,},
	{.center_freq = 5200, .hw_value = 40,},
	{.center_freq = 5220, .hw_value = 44,},
	{.center_freq = 5240, .hw_value = 48,},
	{.center_freq = 5260, .hw_value = 52,},
	{.center_freq = 5280, .hw_value = 56,},
	{.center_freq = 5300, .hw_value = 60,},
	{.center_freq = 5320, .hw_value = 64,},
	{.center_freq = 5500, .hw_value = 100,},
	{.center_freq = 5520, .hw_value = 104,},
	{.center_freq = 5540, .hw_value = 108,},
	{.center_freq = 5560, .hw_value = 112,},
	{.center_freq = 5580, .hw_value = 116,},
	{.center_freq = 5600, .hw_value = 120,},
	{.center_freq = 5620, .hw_value = 124,},
	{.center_freq = 5640, .hw_value = 128,},
	{.center_freq = 5660, .hw_value = 132,},
	{.center_freq = 5680, .hw_value = 136,},
	{.center_freq = 5700, .hw_value = 140,},
	{.center_freq = 5720, .hw_value = 144,},
	{.center_freq = 5745, .hw_value = 149,},
	{.center_freq = 5765, .hw_value = 153,},
	{.center_freq = 5785, .hw_value = 157,},
	{.center_freq = 5805, .hw_value = 161,},
	{.center_freq = 5825, .hw_value = 165,
	 .flags = IEEE80211_CHAN_NO_HT40MINUS},
};

static struct ieee80211_rate rtw_ratetable[] = {
	{.bitrate = 10, .hw_value = 0x00,},
	{.bitrate = 20, .hw_value = 0x01,},
	{.bitrate = 55, .hw_value = 0x02,},
	{.bitrate = 110, .hw_value = 0x03,},
	{.bitrate = 60, .hw_value = 0x04,},
	{.bitrate = 90, .hw_value = 0x05,},
	{.bitrate = 120, .hw_value = 0x06,},
	{.bitrate = 180, .hw_value = 0x07,},
	{.bitrate = 240, .hw_value = 0x08,},
	{.bitrate = 360, .hw_value = 0x09,},
	{.bitrate = 480, .hw_value = 0x0a,},
	{.bitrate = 540, .hw_value = 0x0b,},
};

u16 rtw_desc_to_bitrate(u8 desc_rate)
{
	struct ieee80211_rate rate;

	if (WARN(desc_rate >= ARRAY_SIZE(rtw_ratetable), "invalid desc rate\n"))
		return 0;

	rate = rtw_ratetable[desc_rate];

	return rate.bitrate;
}

static struct ieee80211_supported_band rtw_band_2ghz = {
	.band = NL80211_BAND_2GHZ,

	.channels = rtw_channeltable_2g,
	.n_channels = ARRAY_SIZE(rtw_channeltable_2g),

	.bitrates = rtw_ratetable,
	.n_bitrates = ARRAY_SIZE(rtw_ratetable),

	.ht_cap = {0},
	.vht_cap = {0},
};

static struct ieee80211_supported_band rtw_band_5ghz = {
	.band = NL80211_BAND_5GHZ,

	.channels = rtw_channeltable_5g,
	.n_channels = ARRAY_SIZE(rtw_channeltable_5g),

	/* 5G has no CCK rates */
	.bitrates = rtw_ratetable + 4,
	.n_bitrates = ARRAY_SIZE(rtw_ratetable) - 4,

	.ht_cap = {0},
	.vht_cap = {0},
};

struct rtw_watch_dog_iter_data {
	struct rtw_dev *rtwdev;
	struct rtw_vif *rtwvif;
};

static void rtw_dynamic_csi_rate(struct rtw_dev *rtwdev, struct rtw_vif *rtwvif)
{
	struct rtw_bf_info *bf_info = &rtwdev->bf_info;
	u8 fix_rate_enable = 0;
	u8 new_csi_rate_idx;

	if (rtwvif->bfee.role != RTW_BFEE_SU &&
	    rtwvif->bfee.role != RTW_BFEE_MU)
		return;

	rtw_chip_cfg_csi_rate(rtwdev, rtwdev->dm_info.min_rssi,
			      bf_info->cur_csi_rpt_rate,
			      fix_rate_enable, &new_csi_rate_idx);

	if (new_csi_rate_idx != bf_info->cur_csi_rpt_rate)
		bf_info->cur_csi_rpt_rate = new_csi_rate_idx;
}

static void rtw_vif_watch_dog_iter(void *data, u8 *mac,
				   struct ieee80211_vif *vif)
{
	struct rtw_watch_dog_iter_data *iter_data = data;
	struct rtw_vif *rtwvif = (struct rtw_vif *)vif->drv_priv;

	if (vif->type == NL80211_IFTYPE_STATION)
		if (vif->cfg.assoc)
			iter_data->rtwvif = rtwvif;

	rtw_dynamic_csi_rate(iter_data->rtwdev, rtwvif);

	rtwvif->stats.tx_unicast = 0;
	rtwvif->stats.rx_unicast = 0;
	rtwvif->stats.tx_cnt = 0;
	rtwvif->stats.rx_cnt = 0;
}

/* process TX/RX statistics periodically for hardware,
 * the information helps hardware to enhance performance
 */
static void rtw_watch_dog_work(struct work_struct *work)
{
	struct rtw_dev *rtwdev = container_of(work, struct rtw_dev,
					      watch_dog_work.work);
	struct rtw_traffic_stats *stats = &rtwdev->stats;
	struct rtw_watch_dog_iter_data data = {};
	bool busy_traffic = test_bit(RTW_FLAG_BUSY_TRAFFIC, rtwdev->flags);
	bool ps_active;

	mutex_lock(&rtwdev->mutex);

	if (!test_bit(RTW_FLAG_RUNNING, rtwdev->flags))
		goto unlock;

	ieee80211_queue_delayed_work(rtwdev->hw, &rtwdev->watch_dog_work,
				     RTW_WATCH_DOG_DELAY_TIME);

	if (rtwdev->stats.tx_cnt > 100 || rtwdev->stats.rx_cnt > 100)
		set_bit(RTW_FLAG_BUSY_TRAFFIC, rtwdev->flags);
	else
		clear_bit(RTW_FLAG_BUSY_TRAFFIC, rtwdev->flags);

	rtw_coex_wl_status_check(rtwdev);
	rtw_coex_query_bt_hid_list(rtwdev);

	if (busy_traffic != test_bit(RTW_FLAG_BUSY_TRAFFIC, rtwdev->flags))
		rtw_coex_wl_status_change_notify(rtwdev, 0);

	if (stats->tx_cnt > RTW_LPS_THRESHOLD ||
	    stats->rx_cnt > RTW_LPS_THRESHOLD)
		ps_active = true;
	else
		ps_active = false;

	ewma_tp_add(&stats->tx_ewma_tp,
		    (u32)(stats->tx_unicast >> RTW_TP_SHIFT));
	ewma_tp_add(&stats->rx_ewma_tp,
		    (u32)(stats->rx_unicast >> RTW_TP_SHIFT));
	stats->tx_throughput = ewma_tp_read(&stats->tx_ewma_tp);
	stats->rx_throughput = ewma_tp_read(&stats->rx_ewma_tp);

	/* reset tx/rx statictics */
	stats->tx_unicast = 0;
	stats->rx_unicast = 0;
	stats->tx_cnt = 0;
	stats->rx_cnt = 0;

	if (test_bit(RTW_FLAG_SCANNING, rtwdev->flags))
		goto unlock;

	/* make sure BB/RF is working for dynamic mech */
	rtw_leave_lps(rtwdev);

	rtw_phy_dynamic_mechanism(rtwdev);

	data.rtwdev = rtwdev;
	/* use atomic version to avoid taking local->iflist_mtx mutex */
	rtw_iterate_vifs_atomic(rtwdev, rtw_vif_watch_dog_iter, &data);

	/* fw supports only one station associated to enter lps, if there are
	 * more than two stations associated to the AP, then we can not enter
	 * lps, because fw does not handle the overlapped beacon interval
	 *
	 * rtw_recalc_lps() iterate vifs and determine if driver can enter
	 * ps by vif->type and vif->cfg.ps, all we need to do here is to
	 * get that vif and check if device is having traffic more than the
	 * threshold.
	 */
	if (rtwdev->ps_enabled && data.rtwvif && !ps_active &&
	    !rtwdev->beacon_loss)
		rtw_enter_lps(rtwdev, data.rtwvif->port);

	rtwdev->watch_dog_cnt++;

unlock:
	mutex_unlock(&rtwdev->mutex);
}

static void rtw_c2h_work(struct work_struct *work)
{
	struct rtw_dev *rtwdev = container_of(work, struct rtw_dev, c2h_work);
	struct sk_buff *skb, *tmp;

	skb_queue_walk_safe(&rtwdev->c2h_queue, skb, tmp) {
		skb_unlink(skb, &rtwdev->c2h_queue);
		rtw_fw_c2h_cmd_handle(rtwdev, skb);
		dev_kfree_skb_any(skb);
	}
}

static void rtw_ips_work(struct work_struct *work)
{
	struct rtw_dev *rtwdev = container_of(work, struct rtw_dev, ips_work);

	mutex_lock(&rtwdev->mutex);
	if (rtwdev->hw->conf.flags & IEEE80211_CONF_IDLE)
		rtw_enter_ips(rtwdev);
	mutex_unlock(&rtwdev->mutex);
}

static u8 rtw_acquire_macid(struct rtw_dev *rtwdev)
{
	unsigned long mac_id;

	mac_id = find_first_zero_bit(rtwdev->mac_id_map, RTW_MAX_MAC_ID_NUM);
	if (mac_id < RTW_MAX_MAC_ID_NUM)
		set_bit(mac_id, rtwdev->mac_id_map);

	return mac_id;
}

static void rtw_sta_rc_work(struct work_struct *work)
{
	struct rtw_sta_info *si = container_of(work, struct rtw_sta_info,
					       rc_work);
	struct rtw_dev *rtwdev = si->rtwdev;

	mutex_lock(&rtwdev->mutex);
	rtw_update_sta_info(rtwdev, si, true);
	mutex_unlock(&rtwdev->mutex);
}

int rtw_sta_add(struct rtw_dev *rtwdev, struct ieee80211_sta *sta,
		struct ieee80211_vif *vif)
{
	struct rtw_sta_info *si = (struct rtw_sta_info *)sta->drv_priv;
	int i;

	si->mac_id = rtw_acquire_macid(rtwdev);
	if (si->mac_id >= RTW_MAX_MAC_ID_NUM)
		return -ENOSPC;

	si->rtwdev = rtwdev;
	si->sta = sta;
	si->vif = vif;
	si->init_ra_lv = 1;
	ewma_rssi_init(&si->avg_rssi);
	for (i = 0; i < ARRAY_SIZE(sta->txq); i++)
		rtw_txq_init(rtwdev, sta->txq[i]);
	INIT_WORK(&si->rc_work, rtw_sta_rc_work);

	rtw_update_sta_info(rtwdev, si, true);
	rtw_fw_media_status_report(rtwdev, si->mac_id, true);

	rtwdev->sta_cnt++;
	rtwdev->beacon_loss = false;
	rtw_dbg(rtwdev, RTW_DBG_STATE, "sta %pM joined with macid %d\n",
		sta->addr, si->mac_id);

	return 0;
}

void rtw_sta_remove(struct rtw_dev *rtwdev, struct ieee80211_sta *sta,
		    bool fw_exist)
{
	struct rtw_sta_info *si = (struct rtw_sta_info *)sta->drv_priv;
	int i;

	cancel_work_sync(&si->rc_work);

	rtw_release_macid(rtwdev, si->mac_id);
	if (fw_exist)
		rtw_fw_media_status_report(rtwdev, si->mac_id, false);

	for (i = 0; i < ARRAY_SIZE(sta->txq); i++)
		rtw_txq_cleanup(rtwdev, sta->txq[i]);

	kfree(si->mask);

	rtwdev->sta_cnt--;
	rtw_dbg(rtwdev, RTW_DBG_STATE, "sta %pM with macid %d left\n",
		sta->addr, si->mac_id);
}

struct rtw_fwcd_hdr {
	u32 item;
	u32 size;
	u32 padding1;
	u32 padding2;
} __packed;

static int rtw_fwcd_prep(struct rtw_dev *rtwdev)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_fwcd_desc *desc = &rtwdev->fw.fwcd_desc;
	const struct rtw_fwcd_segs *segs = chip->fwcd_segs;
	u32 prep_size = chip->fw_rxff_size + sizeof(struct rtw_fwcd_hdr);
	u8 i;

	if (segs) {
		prep_size += segs->num * sizeof(struct rtw_fwcd_hdr);

		for (i = 0; i < segs->num; i++)
			prep_size += segs->segs[i];
	}

	desc->data = vmalloc(prep_size);
	if (!desc->data)
		return -ENOMEM;

	desc->size = prep_size;
	desc->next = desc->data;

	return 0;
}

static u8 *rtw_fwcd_next(struct rtw_dev *rtwdev, u32 item, u32 size)
{
	struct rtw_fwcd_desc *desc = &rtwdev->fw.fwcd_desc;
	struct rtw_fwcd_hdr *hdr;
	u8 *next;

	if (!desc->data) {
		rtw_dbg(rtwdev, RTW_DBG_FW, "fwcd isn't prepared successfully\n");
		return NULL;
	}

	next = desc->next + sizeof(struct rtw_fwcd_hdr);
	if (next - desc->data + size > desc->size) {
		rtw_dbg(rtwdev, RTW_DBG_FW, "fwcd isn't prepared enough\n");
		return NULL;
	}

	hdr = (struct rtw_fwcd_hdr *)(desc->next);
	hdr->item = item;
	hdr->size = size;
	hdr->padding1 = 0x01234567;
	hdr->padding2 = 0x89abcdef;
	desc->next = next + size;

	return next;
}

static void rtw_fwcd_dump(struct rtw_dev *rtwdev)
{
	struct rtw_fwcd_desc *desc = &rtwdev->fw.fwcd_desc;

	rtw_dbg(rtwdev, RTW_DBG_FW, "dump fwcd\n");

	/* Data will be freed after lifetime of device coredump. After calling
	 * dev_coredump, data is supposed to be handled by the device coredump
	 * framework. Note that a new dump will be discarded if a previous one
	 * hasn't been released yet.
	 */
	dev_coredumpv(rtwdev->dev, desc->data, desc->size, GFP_KERNEL);
}

static void rtw_fwcd_free(struct rtw_dev *rtwdev, bool free_self)
{
	struct rtw_fwcd_desc *desc = &rtwdev->fw.fwcd_desc;

	if (free_self) {
		rtw_dbg(rtwdev, RTW_DBG_FW, "free fwcd by self\n");
		vfree(desc->data);
	}

	desc->data = NULL;
	desc->next = NULL;
}

static int rtw_fw_dump_crash_log(struct rtw_dev *rtwdev)
{
	u32 size = rtwdev->chip->fw_rxff_size;
	u32 *buf;
	u8 seq;

	buf = (u32 *)rtw_fwcd_next(rtwdev, RTW_FWCD_TLV, size);
	if (!buf)
		return -ENOMEM;

	if (rtw_fw_dump_fifo(rtwdev, RTW_FW_FIFO_SEL_RXBUF_FW, 0, size, buf)) {
		rtw_dbg(rtwdev, RTW_DBG_FW, "dump fw fifo fail\n");
		return -EINVAL;
	}

	if (GET_FW_DUMP_LEN(buf) == 0) {
		rtw_dbg(rtwdev, RTW_DBG_FW, "fw crash dump's length is 0\n");
		return -EINVAL;
	}

	seq = GET_FW_DUMP_SEQ(buf);
	if (seq > 0) {
		rtw_dbg(rtwdev, RTW_DBG_FW,
			"fw crash dump's seq is wrong: %d\n", seq);
		return -EINVAL;
	}

	return 0;
}

int rtw_dump_fw(struct rtw_dev *rtwdev, const u32 ocp_src, u32 size,
		u32 fwcd_item)
{
	u32 rxff = rtwdev->chip->fw_rxff_size;
	u32 dump_size, done_size = 0;
	u8 *buf;
	int ret;

	buf = rtw_fwcd_next(rtwdev, fwcd_item, size);
	if (!buf)
		return -ENOMEM;

	while (size) {
		dump_size = size > rxff ? rxff : size;

		ret = rtw_ddma_to_fw_fifo(rtwdev, ocp_src + done_size,
					  dump_size);
		if (ret) {
			rtw_err(rtwdev,
				"ddma fw 0x%x [+0x%x] to fw fifo fail\n",
				ocp_src, done_size);
			return ret;
		}

		ret = rtw_fw_dump_fifo(rtwdev, RTW_FW_FIFO_SEL_RXBUF_FW, 0,
				       dump_size, (u32 *)(buf + done_size));
		if (ret) {
			rtw_err(rtwdev,
				"dump fw 0x%x [+0x%x] from fw fifo fail\n",
				ocp_src, done_size);
			return ret;
		}

		size -= dump_size;
		done_size += dump_size;
	}

	return 0;
}
EXPORT_SYMBOL(rtw_dump_fw);

int rtw_dump_reg(struct rtw_dev *rtwdev, const u32 addr, const u32 size)
{
	u8 *buf;
	u32 i;

	if (addr & 0x3) {
		WARN(1, "should be 4-byte aligned, addr = 0x%08x\n", addr);
		return -EINVAL;
	}

	buf = rtw_fwcd_next(rtwdev, RTW_FWCD_REG, size);
	if (!buf)
		return -ENOMEM;

	for (i = 0; i < size; i += 4)
		*(u32 *)(buf + i) = rtw_read32(rtwdev, addr + i);

	return 0;
}
EXPORT_SYMBOL(rtw_dump_reg);

void rtw_vif_assoc_changed(struct rtw_vif *rtwvif,
			   struct ieee80211_bss_conf *conf)
{
	struct ieee80211_vif *vif = NULL;

	if (conf)
		vif = container_of(conf, struct ieee80211_vif, bss_conf);

	if (conf && vif->cfg.assoc) {
		rtwvif->aid = vif->cfg.aid;
		rtwvif->net_type = RTW_NET_MGD_LINKED;
	} else {
		rtwvif->aid = 0;
		rtwvif->net_type = RTW_NET_NO_LINK;
	}
}

static void rtw_reset_key_iter(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta,
			       struct ieee80211_key_conf *key,
			       void *data)
{
	struct rtw_dev *rtwdev = (struct rtw_dev *)data;
	struct rtw_sec_desc *sec = &rtwdev->sec;

	rtw_sec_clear_cam(rtwdev, sec, key->hw_key_idx);
}

static void rtw_reset_sta_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw_dev *rtwdev = (struct rtw_dev *)data;

	if (rtwdev->sta_cnt == 0) {
		rtw_warn(rtwdev, "sta count before reset should not be 0\n");
		return;
	}
	rtw_sta_remove(rtwdev, sta, false);
}

static void rtw_reset_vif_iter(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	struct rtw_dev *rtwdev = (struct rtw_dev *)data;
	struct rtw_vif *rtwvif = (struct rtw_vif *)vif->drv_priv;

	rtw_bf_disassoc(rtwdev, vif, NULL);
	rtw_vif_assoc_changed(rtwvif, NULL);
	rtw_txq_cleanup(rtwdev, vif->txq);
}

void rtw_fw_recovery(struct rtw_dev *rtwdev)
{
	if (!test_bit(RTW_FLAG_RESTARTING, rtwdev->flags))
		ieee80211_queue_work(rtwdev->hw, &rtwdev->fw_recovery_work);
}

static void __fw_recovery_work(struct rtw_dev *rtwdev)
{
	int ret = 0;

	set_bit(RTW_FLAG_RESTARTING, rtwdev->flags);
	clear_bit(RTW_FLAG_RESTART_TRIGGERING, rtwdev->flags);

	ret = rtw_fwcd_prep(rtwdev);
	if (ret)
		goto free;
	ret = rtw_fw_dump_crash_log(rtwdev);
	if (ret)
		goto free;
	ret = rtw_chip_dump_fw_crash(rtwdev);
	if (ret)
		goto free;

	rtw_fwcd_dump(rtwdev);
free:
	rtw_fwcd_free(rtwdev, !!ret);
	rtw_write8(rtwdev, REG_MCU_TST_CFG, 0);

	WARN(1, "firmware crash, start reset and recover\n");

	rcu_read_lock();
	rtw_iterate_keys_rcu(rtwdev, NULL, rtw_reset_key_iter, rtwdev);
	rcu_read_unlock();
	rtw_iterate_stas_atomic(rtwdev, rtw_reset_sta_iter, rtwdev);
	rtw_iterate_vifs_atomic(rtwdev, rtw_reset_vif_iter, rtwdev);
	rtw_enter_ips(rtwdev);
}

static void rtw_fw_recovery_work(struct work_struct *work)
{
	struct rtw_dev *rtwdev = container_of(work, struct rtw_dev,
					      fw_recovery_work);

	mutex_lock(&rtwdev->mutex);
	__fw_recovery_work(rtwdev);
	mutex_unlock(&rtwdev->mutex);

	ieee80211_restart_hw(rtwdev->hw);
}

struct rtw_txq_ba_iter_data {
};

static void rtw_txq_ba_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw_sta_info *si = (struct rtw_sta_info *)sta->drv_priv;
	int ret;
	u8 tid;

	tid = find_first_bit(si->tid_ba, IEEE80211_NUM_TIDS);
	while (tid != IEEE80211_NUM_TIDS) {
		clear_bit(tid, si->tid_ba);
		ret = ieee80211_start_tx_ba_session(sta, tid, 0);
		if (ret == -EINVAL) {
			struct ieee80211_txq *txq;
			struct rtw_txq *rtwtxq;

			txq = sta->txq[tid];
			rtwtxq = (struct rtw_txq *)txq->drv_priv;
			set_bit(RTW_TXQ_BLOCK_BA, &rtwtxq->flags);
		}

		tid = find_first_bit(si->tid_ba, IEEE80211_NUM_TIDS);
	}
}

static void rtw_txq_ba_work(struct work_struct *work)
{
	struct rtw_dev *rtwdev = container_of(work, struct rtw_dev, ba_work);
	struct rtw_txq_ba_iter_data data;

	rtw_iterate_stas_atomic(rtwdev, rtw_txq_ba_iter, &data);
}

void rtw_set_rx_freq_band(struct rtw_rx_pkt_stat *pkt_stat, u8 channel)
{
	if (IS_CH_2G_BAND(channel))
		pkt_stat->band = NL80211_BAND_2GHZ;
	else if (IS_CH_5G_BAND(channel))
		pkt_stat->band = NL80211_BAND_5GHZ;
	else
		return;

	pkt_stat->freq = ieee80211_channel_to_frequency(channel, pkt_stat->band);
}
EXPORT_SYMBOL(rtw_set_rx_freq_band);

void rtw_set_dtim_period(struct rtw_dev *rtwdev, int dtim_period)
{
	rtw_write32_set(rtwdev, REG_TCR, BIT_TCR_UPDATE_TIMIE);
	rtw_write8(rtwdev, REG_DTIM_COUNTER_ROOT, dtim_period - 1);
}

void rtw_update_channel(struct rtw_dev *rtwdev, u8 center_channel,
			u8 primary_channel, enum rtw_supported_band band,
			enum rtw_bandwidth bandwidth)
{
	enum nl80211_band nl_band = rtw_hw_to_nl80211_band(band);
	struct rtw_hal *hal = &rtwdev->hal;
	u8 *cch_by_bw = hal->cch_by_bw;
	u32 center_freq, primary_freq;
	enum rtw_sar_bands sar_band;
	u8 primary_channel_idx;

	center_freq = ieee80211_channel_to_frequency(center_channel, nl_band);
	primary_freq = ieee80211_channel_to_frequency(primary_channel, nl_band);

	/* assign the center channel used while 20M bw is selected */
	cch_by_bw[RTW_CHANNEL_WIDTH_20] = primary_channel;

	/* assign the center channel used while current bw is selected */
	cch_by_bw[bandwidth] = center_channel;

	switch (bandwidth) {
	case RTW_CHANNEL_WIDTH_20:
	default:
		primary_channel_idx = RTW_SC_DONT_CARE;
		break;
	case RTW_CHANNEL_WIDTH_40:
		if (primary_freq > center_freq)
			primary_channel_idx = RTW_SC_20_UPPER;
		else
			primary_channel_idx = RTW_SC_20_LOWER;
		break;
	case RTW_CHANNEL_WIDTH_80:
		if (primary_freq > center_freq) {
			if (primary_freq - center_freq == 10)
				primary_channel_idx = RTW_SC_20_UPPER;
			else
				primary_channel_idx = RTW_SC_20_UPMOST;

			/* assign the center channel used
			 * while 40M bw is selected
			 */
			cch_by_bw[RTW_CHANNEL_WIDTH_40] = center_channel + 4;
		} else {
			if (center_freq - primary_freq == 10)
				primary_channel_idx = RTW_SC_20_LOWER;
			else
				primary_channel_idx = RTW_SC_20_LOWEST;

			/* assign the center channel used
			 * while 40M bw is selected
			 */
			cch_by_bw[RTW_CHANNEL_WIDTH_40] = center_channel - 4;
		}
		break;
	}

	switch (center_channel) {
	case 1 ... 14:
		sar_band = RTW_SAR_BAND_0;
		break;
	case 36 ... 64:
		sar_band = RTW_SAR_BAND_1;
		break;
	case 100 ... 144:
		sar_band = RTW_SAR_BAND_3;
		break;
	case 149 ... 177:
		sar_band = RTW_SAR_BAND_4;
		break;
	default:
		WARN(1, "unknown ch(%u) to SAR band\n", center_channel);
		sar_band = RTW_SAR_BAND_0;
		break;
	}

	hal->current_primary_channel_index = primary_channel_idx;
	hal->current_band_width = bandwidth;
	hal->primary_channel = primary_channel;
	hal->current_channel = center_channel;
	hal->current_band_type = band;
	hal->sar_band = sar_band;
}

void rtw_get_channel_params(struct cfg80211_chan_def *chandef,
			    struct rtw_channel_params *chan_params)
{
	struct ieee80211_channel *channel = chandef->chan;
	enum nl80211_chan_width width = chandef->width;
	u32 primary_freq, center_freq;
	u8 center_chan;
	u8 bandwidth = RTW_CHANNEL_WIDTH_20;

	center_chan = channel->hw_value;
	primary_freq = channel->center_freq;
	center_freq = chandef->center_freq1;

	switch (width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
		bandwidth = RTW_CHANNEL_WIDTH_20;
		break;
	case NL80211_CHAN_WIDTH_40:
		bandwidth = RTW_CHANNEL_WIDTH_40;
		if (primary_freq > center_freq)
			center_chan -= 2;
		else
			center_chan += 2;
		break;
	case NL80211_CHAN_WIDTH_80:
		bandwidth = RTW_CHANNEL_WIDTH_80;
		if (primary_freq > center_freq) {
			if (primary_freq - center_freq == 10)
				center_chan -= 2;
			else
				center_chan -= 6;
		} else {
			if (center_freq - primary_freq == 10)
				center_chan += 2;
			else
				center_chan += 6;
		}
		break;
	default:
		center_chan = 0;
		break;
	}

	chan_params->center_chan = center_chan;
	chan_params->bandwidth = bandwidth;
	chan_params->primary_chan = channel->hw_value;
}

void rtw_set_channel(struct rtw_dev *rtwdev)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct ieee80211_hw *hw = rtwdev->hw;
	struct rtw_hal *hal = &rtwdev->hal;
	struct rtw_channel_params ch_param;
	u8 center_chan, primary_chan, bandwidth, band;

	rtw_get_channel_params(&hw->conf.chandef, &ch_param);
	if (WARN(ch_param.center_chan == 0, "Invalid channel\n"))
		return;

	center_chan = ch_param.center_chan;
	primary_chan = ch_param.primary_chan;
	bandwidth = ch_param.bandwidth;
	band = ch_param.center_chan > 14 ? RTW_BAND_5G : RTW_BAND_2G;

	rtw_update_channel(rtwdev, center_chan, primary_chan, band, bandwidth);

	chip->ops->set_channel(rtwdev, center_chan, bandwidth,
			       hal->current_primary_channel_index);

	if (hal->current_band_type == RTW_BAND_5G) {
		rtw_coex_switchband_notify(rtwdev, COEX_SWITCH_TO_5G);
	} else {
		if (test_bit(RTW_FLAG_SCANNING, rtwdev->flags))
			rtw_coex_switchband_notify(rtwdev, COEX_SWITCH_TO_24G);
		else
			rtw_coex_switchband_notify(rtwdev, COEX_SWITCH_TO_24G_NOFORSCAN);
	}

	rtw_phy_set_tx_power_level(rtwdev, center_chan);

	/* if the channel isn't set for scanning, we will do RF calibration
	 * in ieee80211_ops::mgd_prepare_tx(). Performing the calibration
	 * during scanning on each channel takes too long.
	 */
	if (!test_bit(RTW_FLAG_SCANNING, rtwdev->flags))
		rtwdev->need_rfk = true;
}

void rtw_chip_prepare_tx(struct rtw_dev *rtwdev)
{
	const struct rtw_chip_info *chip = rtwdev->chip;

	if (rtwdev->need_rfk) {
		rtwdev->need_rfk = false;
		chip->ops->phy_calibration(rtwdev);
	}
}

static void rtw_vif_write_addr(struct rtw_dev *rtwdev, u32 start, u8 *addr)
{
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		rtw_write8(rtwdev, start + i, addr[i]);
}

void rtw_vif_port_config(struct rtw_dev *rtwdev,
			 struct rtw_vif *rtwvif,
			 u32 config)
{
	u32 addr, mask;

	if (config & PORT_SET_MAC_ADDR) {
		addr = rtwvif->conf->mac_addr.addr;
		rtw_vif_write_addr(rtwdev, addr, rtwvif->mac_addr);
	}
	if (config & PORT_SET_BSSID) {
		addr = rtwvif->conf->bssid.addr;
		rtw_vif_write_addr(rtwdev, addr, rtwvif->bssid);
	}
	if (config & PORT_SET_NET_TYPE) {
		addr = rtwvif->conf->net_type.addr;
		mask = rtwvif->conf->net_type.mask;
		rtw_write32_mask(rtwdev, addr, mask, rtwvif->net_type);
	}
	if (config & PORT_SET_AID) {
		addr = rtwvif->conf->aid.addr;
		mask = rtwvif->conf->aid.mask;
		rtw_write32_mask(rtwdev, addr, mask, rtwvif->aid);
	}
	if (config & PORT_SET_BCN_CTRL) {
		addr = rtwvif->conf->bcn_ctrl.addr;
		mask = rtwvif->conf->bcn_ctrl.mask;
		rtw_write8_mask(rtwdev, addr, mask, rtwvif->bcn_ctrl);
	}
}

static u8 hw_bw_cap_to_bitamp(u8 bw_cap)
{
	u8 bw = 0;

	switch (bw_cap) {
	case EFUSE_HW_CAP_IGNORE:
	case EFUSE_HW_CAP_SUPP_BW80:
		bw |= BIT(RTW_CHANNEL_WIDTH_80);
		fallthrough;
	case EFUSE_HW_CAP_SUPP_BW40:
		bw |= BIT(RTW_CHANNEL_WIDTH_40);
		fallthrough;
	default:
		bw |= BIT(RTW_CHANNEL_WIDTH_20);
		break;
	}

	return bw;
}

static void rtw_hw_config_rf_ant_num(struct rtw_dev *rtwdev, u8 hw_ant_num)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_hal *hal = &rtwdev->hal;

	if (hw_ant_num == EFUSE_HW_CAP_IGNORE ||
	    hw_ant_num >= hal->rf_path_num)
		return;

	switch (hw_ant_num) {
	case 1:
		hal->rf_type = RF_1T1R;
		hal->rf_path_num = 1;
		if (!chip->fix_rf_phy_num)
			hal->rf_phy_num = hal->rf_path_num;
		hal->antenna_tx = BB_PATH_A;
		hal->antenna_rx = BB_PATH_A;
		break;
	default:
		WARN(1, "invalid hw configuration from efuse\n");
		break;
	}
}

static u64 get_vht_ra_mask(struct ieee80211_sta *sta)
{
	u64 ra_mask = 0;
	u16 mcs_map = le16_to_cpu(sta->deflink.vht_cap.vht_mcs.rx_mcs_map);
	u8 vht_mcs_cap;
	int i, nss;

	/* 4SS, every two bits for MCS7/8/9 */
	for (i = 0, nss = 12; i < 4; i++, mcs_map >>= 2, nss += 10) {
		vht_mcs_cap = mcs_map & 0x3;
		switch (vht_mcs_cap) {
		case 2: /* MCS9 */
			ra_mask |= 0x3ffULL << nss;
			break;
		case 1: /* MCS8 */
			ra_mask |= 0x1ffULL << nss;
			break;
		case 0: /* MCS7 */
			ra_mask |= 0x0ffULL << nss;
			break;
		default:
			break;
		}
	}

	return ra_mask;
}

static u8 get_rate_id(u8 wireless_set, enum rtw_bandwidth bw_mode, u8 tx_num)
{
	u8 rate_id = 0;

	switch (wireless_set) {
	case WIRELESS_CCK:
		rate_id = RTW_RATEID_B_20M;
		break;
	case WIRELESS_OFDM:
		rate_id = RTW_RATEID_G;
		break;
	case WIRELESS_CCK | WIRELESS_OFDM:
		rate_id = RTW_RATEID_BG;
		break;
	case WIRELESS_OFDM | WIRELESS_HT:
		if (tx_num == 1)
			rate_id = RTW_RATEID_GN_N1SS;
		else if (tx_num == 2)
			rate_id = RTW_RATEID_GN_N2SS;
		else if (tx_num == 3)
			rate_id = RTW_RATEID_ARFR5_N_3SS;
		break;
	case WIRELESS_CCK | WIRELESS_OFDM | WIRELESS_HT:
		if (bw_mode == RTW_CHANNEL_WIDTH_40) {
			if (tx_num == 1)
				rate_id = RTW_RATEID_BGN_40M_1SS;
			else if (tx_num == 2)
				rate_id = RTW_RATEID_BGN_40M_2SS;
			else if (tx_num == 3)
				rate_id = RTW_RATEID_ARFR5_N_3SS;
			else if (tx_num == 4)
				rate_id = RTW_RATEID_ARFR7_N_4SS;
		} else {
			if (tx_num == 1)
				rate_id = RTW_RATEID_BGN_20M_1SS;
			else if (tx_num == 2)
				rate_id = RTW_RATEID_BGN_20M_2SS;
			else if (tx_num == 3)
				rate_id = RTW_RATEID_ARFR5_N_3SS;
			else if (tx_num == 4)
				rate_id = RTW_RATEID_ARFR7_N_4SS;
		}
		break;
	case WIRELESS_OFDM | WIRELESS_VHT:
		if (tx_num == 1)
			rate_id = RTW_RATEID_ARFR1_AC_1SS;
		else if (tx_num == 2)
			rate_id = RTW_RATEID_ARFR0_AC_2SS;
		else if (tx_num == 3)
			rate_id = RTW_RATEID_ARFR4_AC_3SS;
		else if (tx_num == 4)
			rate_id = RTW_RATEID_ARFR6_AC_4SS;
		break;
	case WIRELESS_CCK | WIRELESS_OFDM | WIRELESS_VHT:
		if (bw_mode >= RTW_CHANNEL_WIDTH_80) {
			if (tx_num == 1)
				rate_id = RTW_RATEID_ARFR1_AC_1SS;
			else if (tx_num == 2)
				rate_id = RTW_RATEID_ARFR0_AC_2SS;
			else if (tx_num == 3)
				rate_id = RTW_RATEID_ARFR4_AC_3SS;
			else if (tx_num == 4)
				rate_id = RTW_RATEID_ARFR6_AC_4SS;
		} else {
			if (tx_num == 1)
				rate_id = RTW_RATEID_ARFR2_AC_2G_1SS;
			else if (tx_num == 2)
				rate_id = RTW_RATEID_ARFR3_AC_2G_2SS;
			else if (tx_num == 3)
				rate_id = RTW_RATEID_ARFR4_AC_3SS;
			else if (tx_num == 4)
				rate_id = RTW_RATEID_ARFR6_AC_4SS;
		}
		break;
	default:
		break;
	}

	return rate_id;
}

#define RA_MASK_CCK_RATES	0x0000f
#define RA_MASK_OFDM_RATES	0x00ff0
#define RA_MASK_HT_RATES_1SS	(0xff000ULL << 0)
#define RA_MASK_HT_RATES_2SS	(0xff000ULL << 8)
#define RA_MASK_HT_RATES_3SS	(0xff000ULL << 16)
#define RA_MASK_HT_RATES	(RA_MASK_HT_RATES_1SS | \
				 RA_MASK_HT_RATES_2SS | \
				 RA_MASK_HT_RATES_3SS)
#define RA_MASK_VHT_RATES_1SS	(0x3ff000ULL << 0)
#define RA_MASK_VHT_RATES_2SS	(0x3ff000ULL << 10)
#define RA_MASK_VHT_RATES_3SS	(0x3ff000ULL << 20)
#define RA_MASK_VHT_RATES	(RA_MASK_VHT_RATES_1SS | \
				 RA_MASK_VHT_RATES_2SS | \
				 RA_MASK_VHT_RATES_3SS)
#define RA_MASK_CCK_IN_BG	0x00005
#define RA_MASK_CCK_IN_HT	0x00005
#define RA_MASK_CCK_IN_VHT	0x00005
#define RA_MASK_OFDM_IN_VHT	0x00010
#define RA_MASK_OFDM_IN_HT_2G	0x00010
#define RA_MASK_OFDM_IN_HT_5G	0x00030

static u64 rtw_rate_mask_rssi(struct rtw_sta_info *si, u8 wireless_set)
{
	u8 rssi_level = si->rssi_level;

	if (wireless_set == WIRELESS_CCK)
		return 0xffffffffffffffffULL;

	if (rssi_level == 0)
		return 0xffffffffffffffffULL;
	else if (rssi_level == 1)
		return 0xfffffffffffffff0ULL;
	else if (rssi_level == 2)
		return 0xffffffffffffefe0ULL;
	else if (rssi_level == 3)
		return 0xffffffffffffcfc0ULL;
	else if (rssi_level == 4)
		return 0xffffffffffff8f80ULL;
	else
		return 0xffffffffffff0f00ULL;
}

static u64 rtw_rate_mask_recover(u64 ra_mask, u64 ra_mask_bak)
{
	if ((ra_mask & ~(RA_MASK_CCK_RATES | RA_MASK_OFDM_RATES)) == 0)
		ra_mask |= (ra_mask_bak & ~(RA_MASK_CCK_RATES | RA_MASK_OFDM_RATES));

	if (ra_mask == 0)
		ra_mask |= (ra_mask_bak & (RA_MASK_CCK_RATES | RA_MASK_OFDM_RATES));

	return ra_mask;
}

static u64 rtw_rate_mask_cfg(struct rtw_dev *rtwdev, struct rtw_sta_info *si,
			     u64 ra_mask, bool is_vht_enable)
{
	struct rtw_hal *hal = &rtwdev->hal;
	const struct cfg80211_bitrate_mask *mask = si->mask;
	u64 cfg_mask = GENMASK_ULL(63, 0);
	u8 band;

	if (!si->use_cfg_mask)
		return ra_mask;

	band = hal->current_band_type;
	if (band == RTW_BAND_2G) {
		band = NL80211_BAND_2GHZ;
		cfg_mask = mask->control[band].legacy;
	} else if (band == RTW_BAND_5G) {
		band = NL80211_BAND_5GHZ;
		cfg_mask = u64_encode_bits(mask->control[band].legacy,
					   RA_MASK_OFDM_RATES);
	}

	if (!is_vht_enable) {
		if (ra_mask & RA_MASK_HT_RATES_1SS)
			cfg_mask |= u64_encode_bits(mask->control[band].ht_mcs[0],
						    RA_MASK_HT_RATES_1SS);
		if (ra_mask & RA_MASK_HT_RATES_2SS)
			cfg_mask |= u64_encode_bits(mask->control[band].ht_mcs[1],
						    RA_MASK_HT_RATES_2SS);
	} else {
		if (ra_mask & RA_MASK_VHT_RATES_1SS)
			cfg_mask |= u64_encode_bits(mask->control[band].vht_mcs[0],
						    RA_MASK_VHT_RATES_1SS);
		if (ra_mask & RA_MASK_VHT_RATES_2SS)
			cfg_mask |= u64_encode_bits(mask->control[band].vht_mcs[1],
						    RA_MASK_VHT_RATES_2SS);
	}

	ra_mask &= cfg_mask;

	return ra_mask;
}

void rtw_update_sta_info(struct rtw_dev *rtwdev, struct rtw_sta_info *si,
			 bool reset_ra_mask)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct ieee80211_sta *sta = si->sta;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_hal *hal = &rtwdev->hal;
	u8 wireless_set;
	u8 bw_mode;
	u8 rate_id;
	u8 rf_type = RF_1T1R;
	u8 stbc_en = 0;
	u8 ldpc_en = 0;
	u8 tx_num = 1;
	u64 ra_mask = 0;
	u64 ra_mask_bak = 0;
	bool is_vht_enable = false;
	bool is_support_sgi = false;

	if (sta->deflink.vht_cap.vht_supported) {
		is_vht_enable = true;
		ra_mask |= get_vht_ra_mask(sta);
		if (sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_RXSTBC_MASK)
			stbc_en = VHT_STBC_EN;
		if (sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_RXLDPC)
			ldpc_en = VHT_LDPC_EN;
	} else if (sta->deflink.ht_cap.ht_supported) {
		ra_mask |= (sta->deflink.ht_cap.mcs.rx_mask[1] << 20) |
			   (sta->deflink.ht_cap.mcs.rx_mask[0] << 12);
		if (sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_RX_STBC)
			stbc_en = HT_STBC_EN;
		if (sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_LDPC_CODING)
			ldpc_en = HT_LDPC_EN;
	}

	if (efuse->hw_cap.nss == 1 || rtwdev->hal.txrx_1ss)
		ra_mask &= RA_MASK_VHT_RATES_1SS | RA_MASK_HT_RATES_1SS;

	if (hal->current_band_type == RTW_BAND_5G) {
		ra_mask |= (u64)sta->deflink.supp_rates[NL80211_BAND_5GHZ] << 4;
		ra_mask_bak = ra_mask;
		if (sta->deflink.vht_cap.vht_supported) {
			ra_mask &= RA_MASK_VHT_RATES | RA_MASK_OFDM_IN_VHT;
			wireless_set = WIRELESS_OFDM | WIRELESS_VHT;
		} else if (sta->deflink.ht_cap.ht_supported) {
			ra_mask &= RA_MASK_HT_RATES | RA_MASK_OFDM_IN_HT_5G;
			wireless_set = WIRELESS_OFDM | WIRELESS_HT;
		} else {
			wireless_set = WIRELESS_OFDM;
		}
		dm_info->rrsr_val_init = RRSR_INIT_5G;
	} else if (hal->current_band_type == RTW_BAND_2G) {
		ra_mask |= sta->deflink.supp_rates[NL80211_BAND_2GHZ];
		ra_mask_bak = ra_mask;
		if (sta->deflink.vht_cap.vht_supported) {
			ra_mask &= RA_MASK_VHT_RATES | RA_MASK_CCK_IN_VHT |
				   RA_MASK_OFDM_IN_VHT;
			wireless_set = WIRELESS_CCK | WIRELESS_OFDM |
				       WIRELESS_HT | WIRELESS_VHT;
		} else if (sta->deflink.ht_cap.ht_supported) {
			ra_mask &= RA_MASK_HT_RATES | RA_MASK_CCK_IN_HT |
				   RA_MASK_OFDM_IN_HT_2G;
			wireless_set = WIRELESS_CCK | WIRELESS_OFDM |
				       WIRELESS_HT;
		} else if (sta->deflink.supp_rates[0] <= 0xf) {
			wireless_set = WIRELESS_CCK;
		} else {
			ra_mask &= RA_MASK_OFDM_RATES | RA_MASK_CCK_IN_BG;
			wireless_set = WIRELESS_CCK | WIRELESS_OFDM;
		}
		dm_info->rrsr_val_init = RRSR_INIT_2G;
	} else {
		rtw_err(rtwdev, "Unknown band type\n");
		ra_mask_bak = ra_mask;
		wireless_set = 0;
	}

	switch (sta->deflink.bandwidth) {
	case IEEE80211_STA_RX_BW_80:
		bw_mode = RTW_CHANNEL_WIDTH_80;
		is_support_sgi = sta->deflink.vht_cap.vht_supported &&
				 (sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_80);
		break;
	case IEEE80211_STA_RX_BW_40:
		bw_mode = RTW_CHANNEL_WIDTH_40;
		is_support_sgi = sta->deflink.ht_cap.ht_supported &&
				 (sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_SGI_40);
		break;
	default:
		bw_mode = RTW_CHANNEL_WIDTH_20;
		is_support_sgi = sta->deflink.ht_cap.ht_supported &&
				 (sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_SGI_20);
		break;
	}

	if (sta->deflink.vht_cap.vht_supported && ra_mask & 0xffc00000) {
		tx_num = 2;
		rf_type = RF_2T2R;
	} else if (sta->deflink.ht_cap.ht_supported && ra_mask & 0xfff00000) {
		tx_num = 2;
		rf_type = RF_2T2R;
	}

	rate_id = get_rate_id(wireless_set, bw_mode, tx_num);

	ra_mask &= rtw_rate_mask_rssi(si, wireless_set);
	ra_mask = rtw_rate_mask_recover(ra_mask, ra_mask_bak);
	ra_mask = rtw_rate_mask_cfg(rtwdev, si, ra_mask, is_vht_enable);

	si->bw_mode = bw_mode;
	si->stbc_en = stbc_en;
	si->ldpc_en = ldpc_en;
	si->rf_type = rf_type;
	si->wireless_set = wireless_set;
	si->sgi_enable = is_support_sgi;
	si->vht_enable = is_vht_enable;
	si->ra_mask = ra_mask;
	si->rate_id = rate_id;

	rtw_fw_send_ra_info(rtwdev, si, reset_ra_mask);
}

static int rtw_wait_firmware_completion(struct rtw_dev *rtwdev)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_fw_state *fw;

	fw = &rtwdev->fw;
	wait_for_completion(&fw->completion);
	if (!fw->firmware)
		return -EINVAL;

	if (chip->wow_fw_name) {
		fw = &rtwdev->wow_fw;
		wait_for_completion(&fw->completion);
		if (!fw->firmware)
			return -EINVAL;
	}

	return 0;
}

static enum rtw_lps_deep_mode rtw_update_lps_deep_mode(struct rtw_dev *rtwdev,
						       struct rtw_fw_state *fw)
{
	const struct rtw_chip_info *chip = rtwdev->chip;

	if (rtw_disable_lps_deep_mode || !chip->lps_deep_mode_supported ||
	    !fw->feature)
		return LPS_DEEP_MODE_NONE;

	if ((chip->lps_deep_mode_supported & BIT(LPS_DEEP_MODE_PG)) &&
	    rtw_fw_feature_check(fw, FW_FEATURE_PG))
		return LPS_DEEP_MODE_PG;

	if ((chip->lps_deep_mode_supported & BIT(LPS_DEEP_MODE_LCLK)) &&
	    rtw_fw_feature_check(fw, FW_FEATURE_LCLK))
		return LPS_DEEP_MODE_LCLK;

	return LPS_DEEP_MODE_NONE;
}

static int rtw_power_on(struct rtw_dev *rtwdev)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_fw_state *fw = &rtwdev->fw;
	bool wifi_only;
	int ret;

	ret = rtw_hci_setup(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to setup hci\n");
		goto err;
	}

	/* power on MAC before firmware downloaded */
	ret = rtw_mac_power_on(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to power on mac\n");
		goto err;
	}

	ret = rtw_wait_firmware_completion(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to wait firmware completion\n");
		goto err_off;
	}

	ret = rtw_download_firmware(rtwdev, fw);
	if (ret) {
		rtw_err(rtwdev, "failed to download firmware\n");
		goto err_off;
	}

	/* config mac after firmware downloaded */
	ret = rtw_mac_init(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to configure mac\n");
		goto err_off;
	}

	chip->ops->phy_set_param(rtwdev);

	ret = rtw_hci_start(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to start hci\n");
		goto err_off;
	}

	/* send H2C after HCI has started */
	rtw_fw_send_general_info(rtwdev);
	rtw_fw_send_phydm_info(rtwdev);

	wifi_only = !rtwdev->efuse.btcoex;
	rtw_coex_power_on_setting(rtwdev);
	rtw_coex_init_hw_config(rtwdev, wifi_only);

	return 0;

err_off:
	rtw_mac_power_off(rtwdev);

err:
	return ret;
}

void rtw_core_fw_scan_notify(struct rtw_dev *rtwdev, bool start)
{
	if (!rtw_fw_feature_check(&rtwdev->fw, FW_FEATURE_NOTIFY_SCAN))
		return;

	if (start) {
		rtw_fw_scan_notify(rtwdev, true);
	} else {
		reinit_completion(&rtwdev->fw_scan_density);
		rtw_fw_scan_notify(rtwdev, false);
		if (!wait_for_completion_timeout(&rtwdev->fw_scan_density,
						 SCAN_NOTIFY_TIMEOUT))
			rtw_warn(rtwdev, "firmware failed to report density after scan\n");
	}
}

void rtw_core_scan_start(struct rtw_dev *rtwdev, struct rtw_vif *rtwvif,
			 const u8 *mac_addr, bool hw_scan)
{
	u32 config = 0;
	int ret = 0;

	rtw_leave_lps(rtwdev);

	if (hw_scan && (rtwdev->hw->conf.flags & IEEE80211_CONF_IDLE)) {
		ret = rtw_leave_ips(rtwdev);
		if (ret) {
			rtw_err(rtwdev, "failed to leave idle state\n");
			return;
		}
	}

	ether_addr_copy(rtwvif->mac_addr, mac_addr);
	config |= PORT_SET_MAC_ADDR;
	rtw_vif_port_config(rtwdev, rtwvif, config);

	rtw_coex_scan_notify(rtwdev, COEX_SCAN_START);
	rtw_core_fw_scan_notify(rtwdev, true);

	set_bit(RTW_FLAG_DIG_DISABLE, rtwdev->flags);
	set_bit(RTW_FLAG_SCANNING, rtwdev->flags);
}

void rtw_core_scan_complete(struct rtw_dev *rtwdev, struct ieee80211_vif *vif,
			    bool hw_scan)
{
	struct rtw_vif *rtwvif = vif ? (struct rtw_vif *)vif->drv_priv : NULL;
	u32 config = 0;

	if (!rtwvif)
		return;

	clear_bit(RTW_FLAG_SCANNING, rtwdev->flags);
	clear_bit(RTW_FLAG_DIG_DISABLE, rtwdev->flags);

	rtw_core_fw_scan_notify(rtwdev, false);

	ether_addr_copy(rtwvif->mac_addr, vif->addr);
	config |= PORT_SET_MAC_ADDR;
	rtw_vif_port_config(rtwdev, rtwvif, config);

	rtw_coex_scan_notify(rtwdev, COEX_SCAN_FINISH);

	if (hw_scan && (rtwdev->hw->conf.flags & IEEE80211_CONF_IDLE))
		ieee80211_queue_work(rtwdev->hw, &rtwdev->ips_work);
}

int rtw_core_start(struct rtw_dev *rtwdev)
{
	int ret;

	ret = rtw_power_on(rtwdev);
	if (ret)
		return ret;

	rtw_sec_enable_sec_engine(rtwdev);

	rtwdev->lps_conf.deep_mode = rtw_update_lps_deep_mode(rtwdev, &rtwdev->fw);
	rtwdev->lps_conf.wow_deep_mode = rtw_update_lps_deep_mode(rtwdev, &rtwdev->wow_fw);

	/* rcr reset after powered on */
	rtw_write32(rtwdev, REG_RCR, rtwdev->hal.rcr);

	ieee80211_queue_delayed_work(rtwdev->hw, &rtwdev->watch_dog_work,
				     RTW_WATCH_DOG_DELAY_TIME);

	set_bit(RTW_FLAG_RUNNING, rtwdev->flags);

	return 0;
}

static void rtw_power_off(struct rtw_dev *rtwdev)
{
	rtw_hci_stop(rtwdev);
	rtw_coex_power_off_setting(rtwdev);
	rtw_mac_power_off(rtwdev);
}

void rtw_core_stop(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;

	clear_bit(RTW_FLAG_RUNNING, rtwdev->flags);
	clear_bit(RTW_FLAG_FW_RUNNING, rtwdev->flags);

	mutex_unlock(&rtwdev->mutex);

	cancel_work_sync(&rtwdev->c2h_work);
	cancel_work_sync(&rtwdev->update_beacon_work);
	cancel_delayed_work_sync(&rtwdev->watch_dog_work);
	cancel_delayed_work_sync(&coex->bt_relink_work);
	cancel_delayed_work_sync(&coex->bt_reenable_work);
	cancel_delayed_work_sync(&coex->defreeze_work);
	cancel_delayed_work_sync(&coex->wl_remain_work);
	cancel_delayed_work_sync(&coex->bt_remain_work);
	cancel_delayed_work_sync(&coex->wl_connecting_work);
	cancel_delayed_work_sync(&coex->bt_multi_link_remain_work);
	cancel_delayed_work_sync(&coex->wl_ccklock_work);

	mutex_lock(&rtwdev->mutex);

	rtw_power_off(rtwdev);
}

static void rtw_init_ht_cap(struct rtw_dev *rtwdev,
			    struct ieee80211_sta_ht_cap *ht_cap)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_efuse *efuse = &rtwdev->efuse;

	ht_cap->ht_supported = true;
	ht_cap->cap = 0;
	ht_cap->cap |= IEEE80211_HT_CAP_SGI_20 |
			IEEE80211_HT_CAP_MAX_AMSDU |
			(1 << IEEE80211_HT_CAP_RX_STBC_SHIFT);

	if (rtw_chip_has_rx_ldpc(rtwdev))
		ht_cap->cap |= IEEE80211_HT_CAP_LDPC_CODING;
	if (rtw_chip_has_tx_stbc(rtwdev))
		ht_cap->cap |= IEEE80211_HT_CAP_TX_STBC;

	if (efuse->hw_cap.bw & BIT(RTW_CHANNEL_WIDTH_40))
		ht_cap->cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
				IEEE80211_HT_CAP_DSSSCCK40 |
				IEEE80211_HT_CAP_SGI_40;
	ht_cap->ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
	ht_cap->ampdu_density = chip->ampdu_density;
	ht_cap->mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
	if (efuse->hw_cap.nss > 1) {
		ht_cap->mcs.rx_mask[0] = 0xFF;
		ht_cap->mcs.rx_mask[1] = 0xFF;
		ht_cap->mcs.rx_mask[4] = 0x01;
		ht_cap->mcs.rx_highest = cpu_to_le16(300);
	} else {
		ht_cap->mcs.rx_mask[0] = 0xFF;
		ht_cap->mcs.rx_mask[1] = 0x00;
		ht_cap->mcs.rx_mask[4] = 0x01;
		ht_cap->mcs.rx_highest = cpu_to_le16(150);
	}
}

static void rtw_init_vht_cap(struct rtw_dev *rtwdev,
			     struct ieee80211_sta_vht_cap *vht_cap)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	u16 mcs_map;
	__le16 highest;

	if (efuse->hw_cap.ptcl != EFUSE_HW_CAP_IGNORE &&
	    efuse->hw_cap.ptcl != EFUSE_HW_CAP_PTCL_VHT)
		return;

	vht_cap->vht_supported = true;
	vht_cap->cap = IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454 |
		       IEEE80211_VHT_CAP_SHORT_GI_80 |
		       IEEE80211_VHT_CAP_RXSTBC_1 |
		       IEEE80211_VHT_CAP_HTC_VHT |
		       IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK |
		       0;
	if (rtwdev->hal.rf_path_num > 1)
		vht_cap->cap |= IEEE80211_VHT_CAP_TXSTBC;
	vht_cap->cap |= IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE |
			IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE;
	vht_cap->cap |= (rtwdev->hal.bfee_sts_cap <<
			IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT);

	if (rtw_chip_has_rx_ldpc(rtwdev))
		vht_cap->cap |= IEEE80211_VHT_CAP_RXLDPC;

	mcs_map = IEEE80211_VHT_MCS_SUPPORT_0_9 << 0 |
		  IEEE80211_VHT_MCS_NOT_SUPPORTED << 4 |
		  IEEE80211_VHT_MCS_NOT_SUPPORTED << 6 |
		  IEEE80211_VHT_MCS_NOT_SUPPORTED << 8 |
		  IEEE80211_VHT_MCS_NOT_SUPPORTED << 10 |
		  IEEE80211_VHT_MCS_NOT_SUPPORTED << 12 |
		  IEEE80211_VHT_MCS_NOT_SUPPORTED << 14;
	if (efuse->hw_cap.nss > 1) {
		highest = cpu_to_le16(780);
		mcs_map |= IEEE80211_VHT_MCS_SUPPORT_0_9 << 2;
	} else {
		highest = cpu_to_le16(390);
		mcs_map |= IEEE80211_VHT_MCS_NOT_SUPPORTED << 2;
	}

	vht_cap->vht_mcs.rx_mcs_map = cpu_to_le16(mcs_map);
	vht_cap->vht_mcs.tx_mcs_map = cpu_to_le16(mcs_map);
	vht_cap->vht_mcs.rx_highest = highest;
	vht_cap->vht_mcs.tx_highest = highest;
}

static u16 rtw_get_max_scan_ie_len(struct rtw_dev *rtwdev)
{
	u16 len;

	len = rtwdev->chip->max_scan_ie_len;

	if (!rtw_fw_feature_check(&rtwdev->fw, FW_FEATURE_SCAN_OFFLOAD) &&
	    rtwdev->chip->id == RTW_CHIP_TYPE_8822C)
		len = IEEE80211_MAX_DATA_LEN;
	else if (rtw_fw_feature_ext_check(&rtwdev->fw, FW_FEATURE_EXT_OLD_PAGE_NUM))
		len -= RTW_OLD_PROBE_PG_CNT * TX_PAGE_SIZE;

	return len;
}

static void rtw_set_supported_band(struct ieee80211_hw *hw,
				   const struct rtw_chip_info *chip)
{
	struct rtw_dev *rtwdev = hw->priv;
	struct ieee80211_supported_band *sband;

	if (chip->band & RTW_BAND_2G) {
		sband = kmemdup(&rtw_band_2ghz, sizeof(*sband), GFP_KERNEL);
		if (!sband)
			goto err_out;
		if (chip->ht_supported)
			rtw_init_ht_cap(rtwdev, &sband->ht_cap);
		hw->wiphy->bands[NL80211_BAND_2GHZ] = sband;
	}

	if (chip->band & RTW_BAND_5G) {
		sband = kmemdup(&rtw_band_5ghz, sizeof(*sband), GFP_KERNEL);
		if (!sband)
			goto err_out;
		if (chip->ht_supported)
			rtw_init_ht_cap(rtwdev, &sband->ht_cap);
		if (chip->vht_supported)
			rtw_init_vht_cap(rtwdev, &sband->vht_cap);
		hw->wiphy->bands[NL80211_BAND_5GHZ] = sband;
	}

	return;

err_out:
	rtw_err(rtwdev, "failed to set supported band\n");
}

static void rtw_unset_supported_band(struct ieee80211_hw *hw,
				     const struct rtw_chip_info *chip)
{
	kfree(hw->wiphy->bands[NL80211_BAND_2GHZ]);
	kfree(hw->wiphy->bands[NL80211_BAND_5GHZ]);
}

static void rtw_vif_smps_iter(void *data, u8 *mac,
			      struct ieee80211_vif *vif)
{
	struct rtw_dev *rtwdev = (struct rtw_dev *)data;

	if (vif->type != NL80211_IFTYPE_STATION || !vif->cfg.assoc)
		return;

	if (rtwdev->hal.txrx_1ss)
		ieee80211_request_smps(vif, 0, IEEE80211_SMPS_STATIC);
	else
		ieee80211_request_smps(vif, 0, IEEE80211_SMPS_OFF);
}

void rtw_set_txrx_1ss(struct rtw_dev *rtwdev, bool txrx_1ss)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_hal *hal = &rtwdev->hal;

	if (!chip->ops->config_txrx_mode || rtwdev->hal.txrx_1ss == txrx_1ss)
		return;

	rtwdev->hal.txrx_1ss = txrx_1ss;
	if (txrx_1ss)
		chip->ops->config_txrx_mode(rtwdev, BB_PATH_A, BB_PATH_A, false);
	else
		chip->ops->config_txrx_mode(rtwdev, hal->antenna_tx,
					    hal->antenna_rx, false);
	rtw_iterate_vifs_atomic(rtwdev, rtw_vif_smps_iter, rtwdev);
}

static void __update_firmware_feature(struct rtw_dev *rtwdev,
				      struct rtw_fw_state *fw)
{
	u32 feature;
	const struct rtw_fw_hdr *fw_hdr =
				(const struct rtw_fw_hdr *)fw->firmware->data;

	feature = le32_to_cpu(fw_hdr->feature);
	fw->feature = feature & FW_FEATURE_SIG ? feature : 0;

	if (rtwdev->chip->id == RTW_CHIP_TYPE_8822C &&
	    RTW_FW_SUIT_VER_CODE(rtwdev->fw) < RTW_FW_VER_CODE(9, 9, 13))
		fw->feature_ext |= FW_FEATURE_EXT_OLD_PAGE_NUM;
}

static void __update_firmware_info(struct rtw_dev *rtwdev,
				   struct rtw_fw_state *fw)
{
	const struct rtw_fw_hdr *fw_hdr =
				(const struct rtw_fw_hdr *)fw->firmware->data;

	fw->h2c_version = le16_to_cpu(fw_hdr->h2c_fmt_ver);
	fw->version = le16_to_cpu(fw_hdr->version);
	fw->sub_version = fw_hdr->subversion;
	fw->sub_index = fw_hdr->subindex;

	__update_firmware_feature(rtwdev, fw);
}

static void __update_firmware_info_legacy(struct rtw_dev *rtwdev,
					  struct rtw_fw_state *fw)
{
	struct rtw_fw_hdr_legacy *legacy =
				(struct rtw_fw_hdr_legacy *)fw->firmware->data;

	fw->h2c_version = 0;
	fw->version = le16_to_cpu(legacy->version);
	fw->sub_version = legacy->subversion1;
	fw->sub_index = legacy->subversion2;
}

static void update_firmware_info(struct rtw_dev *rtwdev,
				 struct rtw_fw_state *fw)
{
	if (rtw_chip_wcpu_11n(rtwdev))
		__update_firmware_info_legacy(rtwdev, fw);
	else
		__update_firmware_info(rtwdev, fw);
}

static void rtw_load_firmware_cb(const struct firmware *firmware, void *context)
{
	struct rtw_fw_state *fw = context;
	struct rtw_dev *rtwdev = fw->rtwdev;

	if (!firmware || !firmware->data) {
		rtw_err(rtwdev, "failed to request firmware\n");
		complete_all(&fw->completion);
		return;
	}

	fw->firmware = firmware;
	update_firmware_info(rtwdev, fw);
	complete_all(&fw->completion);

	rtw_info(rtwdev, "Firmware version %u.%u.%u, H2C version %u\n",
		 fw->version, fw->sub_version, fw->sub_index, fw->h2c_version);
}

static int rtw_load_firmware(struct rtw_dev *rtwdev, enum rtw_fw_type type)
{
	const char *fw_name;
	struct rtw_fw_state *fw;
	int ret;

	switch (type) {
	case RTW_WOWLAN_FW:
		fw = &rtwdev->wow_fw;
		fw_name = rtwdev->chip->wow_fw_name;
		break;

	case RTW_NORMAL_FW:
		fw = &rtwdev->fw;
		fw_name = rtwdev->chip->fw_name;
		break;

	default:
		rtw_warn(rtwdev, "unsupported firmware type\n");
		return -ENOENT;
	}

	fw->rtwdev = rtwdev;
	init_completion(&fw->completion);

	ret = request_firmware_nowait(THIS_MODULE, true, fw_name, rtwdev->dev,
				      GFP_KERNEL, fw, rtw_load_firmware_cb);
	if (ret) {
		rtw_err(rtwdev, "failed to async firmware request\n");
		return ret;
	}

	return 0;
}

static int rtw_chip_parameter_setup(struct rtw_dev *rtwdev)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_hal *hal = &rtwdev->hal;
	struct rtw_efuse *efuse = &rtwdev->efuse;

	switch (rtw_hci_type(rtwdev)) {
	case RTW_HCI_TYPE_PCIE:
		rtwdev->hci.rpwm_addr = 0x03d9;
		rtwdev->hci.cpwm_addr = 0x03da;
		break;
	default:
		rtw_err(rtwdev, "unsupported hci type\n");
		return -EINVAL;
	}

	hal->chip_version = rtw_read32(rtwdev, REG_SYS_CFG1);
	hal->cut_version = BIT_GET_CHIP_VER(hal->chip_version);
	hal->mp_chip = (hal->chip_version & BIT_RTL_ID) ? 0 : 1;
	if (hal->chip_version & BIT_RF_TYPE_ID) {
		hal->rf_type = RF_2T2R;
		hal->rf_path_num = 2;
		hal->antenna_tx = BB_PATH_AB;
		hal->antenna_rx = BB_PATH_AB;
	} else {
		hal->rf_type = RF_1T1R;
		hal->rf_path_num = 1;
		hal->antenna_tx = BB_PATH_A;
		hal->antenna_rx = BB_PATH_A;
	}
	hal->rf_phy_num = chip->fix_rf_phy_num ? chip->fix_rf_phy_num :
			  hal->rf_path_num;

	efuse->physical_size = chip->phy_efuse_size;
	efuse->logical_size = chip->log_efuse_size;
	efuse->protect_size = chip->ptct_efuse_size;

	/* default use ack */
	rtwdev->hal.rcr |= BIT_VHT_DACK;

	hal->bfee_sts_cap = 3;

	return 0;
}

static int rtw_chip_efuse_enable(struct rtw_dev *rtwdev)
{
	struct rtw_fw_state *fw = &rtwdev->fw;
	int ret;

	ret = rtw_hci_setup(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to setup hci\n");
		goto err;
	}

	ret = rtw_mac_power_on(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to power on mac\n");
		goto err;
	}

	rtw_write8(rtwdev, REG_C2HEVT, C2H_HW_FEATURE_DUMP);

	wait_for_completion(&fw->completion);
	if (!fw->firmware) {
		ret = -EINVAL;
		rtw_err(rtwdev, "failed to load firmware\n");
		goto err;
	}

	ret = rtw_download_firmware(rtwdev, fw);
	if (ret) {
		rtw_err(rtwdev, "failed to download firmware\n");
		goto err_off;
	}

	return 0;

err_off:
	rtw_mac_power_off(rtwdev);

err:
	return ret;
}

static int rtw_dump_hw_feature(struct rtw_dev *rtwdev)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	u8 hw_feature[HW_FEATURE_LEN];
	u8 id;
	u8 bw;
	int i;

	id = rtw_read8(rtwdev, REG_C2HEVT);
	if (id != C2H_HW_FEATURE_REPORT) {
		rtw_err(rtwdev, "failed to read hw feature report\n");
		return -EBUSY;
	}

	for (i = 0; i < HW_FEATURE_LEN; i++)
		hw_feature[i] = rtw_read8(rtwdev, REG_C2HEVT + 2 + i);

	rtw_write8(rtwdev, REG_C2HEVT, 0);

	bw = GET_EFUSE_HW_CAP_BW(hw_feature);
	efuse->hw_cap.bw = hw_bw_cap_to_bitamp(bw);
	efuse->hw_cap.hci = GET_EFUSE_HW_CAP_HCI(hw_feature);
	efuse->hw_cap.nss = GET_EFUSE_HW_CAP_NSS(hw_feature);
	efuse->hw_cap.ptcl = GET_EFUSE_HW_CAP_PTCL(hw_feature);
	efuse->hw_cap.ant_num = GET_EFUSE_HW_CAP_ANT_NUM(hw_feature);

	rtw_hw_config_rf_ant_num(rtwdev, efuse->hw_cap.ant_num);

	if (efuse->hw_cap.nss == EFUSE_HW_CAP_IGNORE ||
	    efuse->hw_cap.nss > rtwdev->hal.rf_path_num)
		efuse->hw_cap.nss = rtwdev->hal.rf_path_num;

	rtw_dbg(rtwdev, RTW_DBG_EFUSE,
		"hw cap: hci=0x%02x, bw=0x%02x, ptcl=0x%02x, ant_num=%d, nss=%d\n",
		efuse->hw_cap.hci, efuse->hw_cap.bw, efuse->hw_cap.ptcl,
		efuse->hw_cap.ant_num, efuse->hw_cap.nss);

	return 0;
}

static void rtw_chip_efuse_disable(struct rtw_dev *rtwdev)
{
	rtw_hci_stop(rtwdev);
	rtw_mac_power_off(rtwdev);
}

static int rtw_chip_efuse_info_setup(struct rtw_dev *rtwdev)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	int ret;

	mutex_lock(&rtwdev->mutex);

	/* power on mac to read efuse */
	ret = rtw_chip_efuse_enable(rtwdev);
	if (ret)
		goto out_unlock;

	ret = rtw_parse_efuse_map(rtwdev);
	if (ret)
		goto out_disable;

	ret = rtw_dump_hw_feature(rtwdev);
	if (ret)
		goto out_disable;

	ret = rtw_check_supported_rfe(rtwdev);
	if (ret)
		goto out_disable;

	if (efuse->crystal_cap == 0xff)
		efuse->crystal_cap = 0;
	if (efuse->pa_type_2g == 0xff)
		efuse->pa_type_2g = 0;
	if (efuse->pa_type_5g == 0xff)
		efuse->pa_type_5g = 0;
	if (efuse->lna_type_2g == 0xff)
		efuse->lna_type_2g = 0;
	if (efuse->lna_type_5g == 0xff)
		efuse->lna_type_5g = 0;
	if (efuse->channel_plan == 0xff)
		efuse->channel_plan = 0x7f;
	if (efuse->rf_board_option == 0xff)
		efuse->rf_board_option = 0;
	if (efuse->bt_setting & BIT(0))
		efuse->share_ant = true;
	if (efuse->regd == 0xff)
		efuse->regd = 0;
	if (efuse->tx_bb_swing_setting_2g == 0xff)
		efuse->tx_bb_swing_setting_2g = 0;
	if (efuse->tx_bb_swing_setting_5g == 0xff)
		efuse->tx_bb_swing_setting_5g = 0;

	efuse->btcoex = (efuse->rf_board_option & 0xe0) == 0x20;
	efuse->ext_pa_2g = efuse->pa_type_2g & BIT(4) ? 1 : 0;
	efuse->ext_lna_2g = efuse->lna_type_2g & BIT(3) ? 1 : 0;
	efuse->ext_pa_5g = efuse->pa_type_5g & BIT(0) ? 1 : 0;
	efuse->ext_lna_2g = efuse->lna_type_5g & BIT(3) ? 1 : 0;

out_disable:
	rtw_chip_efuse_disable(rtwdev);

out_unlock:
	mutex_unlock(&rtwdev->mutex);
	return ret;
}

static int rtw_chip_board_info_setup(struct rtw_dev *rtwdev)
{
	struct rtw_hal *hal = &rtwdev->hal;
	const struct rtw_rfe_def *rfe_def = rtw_get_rfe_def(rtwdev);

	if (!rfe_def)
		return -ENODEV;

	rtw_phy_setup_phy_cond(rtwdev, 0);

	rtw_phy_init_tx_power(rtwdev);
	rtw_load_table(rtwdev, rfe_def->phy_pg_tbl);
	rtw_load_table(rtwdev, rfe_def->txpwr_lmt_tbl);
	rtw_phy_tx_power_by_rate_config(hal);
	rtw_phy_tx_power_limit_config(hal);

	return 0;
}

int rtw_chip_info_setup(struct rtw_dev *rtwdev)
{
	int ret;

	ret = rtw_chip_parameter_setup(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to setup chip parameters\n");
		goto err_out;
	}

	ret = rtw_chip_efuse_info_setup(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to setup chip efuse info\n");
		goto err_out;
	}

	ret = rtw_chip_board_info_setup(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to setup chip board info\n");
		goto err_out;
	}

	return 0;

err_out:
	return ret;
}
EXPORT_SYMBOL(rtw_chip_info_setup);

static void rtw_stats_init(struct rtw_dev *rtwdev)
{
	struct rtw_traffic_stats *stats = &rtwdev->stats;
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	int i;

	ewma_tp_init(&stats->tx_ewma_tp);
	ewma_tp_init(&stats->rx_ewma_tp);

	for (i = 0; i < RTW_EVM_NUM; i++)
		ewma_evm_init(&dm_info->ewma_evm[i]);
	for (i = 0; i < RTW_SNR_NUM; i++)
		ewma_snr_init(&dm_info->ewma_snr[i]);
}

int rtw_core_init(struct rtw_dev *rtwdev)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_coex *coex = &rtwdev->coex;
	int ret;

	INIT_LIST_HEAD(&rtwdev->rsvd_page_list);
	INIT_LIST_HEAD(&rtwdev->txqs);

	timer_setup(&rtwdev->tx_report.purge_timer,
		    rtw_tx_report_purge_timer, 0);
	rtwdev->tx_wq = alloc_workqueue("rtw_tx_wq", WQ_UNBOUND | WQ_HIGHPRI, 0);
	if (!rtwdev->tx_wq) {
		rtw_warn(rtwdev, "alloc_workqueue rtw_tx_wq failed\n");
		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&rtwdev->watch_dog_work, rtw_watch_dog_work);
	INIT_DELAYED_WORK(&coex->bt_relink_work, rtw_coex_bt_relink_work);
	INIT_DELAYED_WORK(&coex->bt_reenable_work, rtw_coex_bt_reenable_work);
	INIT_DELAYED_WORK(&coex->defreeze_work, rtw_coex_defreeze_work);
	INIT_DELAYED_WORK(&coex->wl_remain_work, rtw_coex_wl_remain_work);
	INIT_DELAYED_WORK(&coex->bt_remain_work, rtw_coex_bt_remain_work);
	INIT_DELAYED_WORK(&coex->wl_connecting_work, rtw_coex_wl_connecting_work);
	INIT_DELAYED_WORK(&coex->bt_multi_link_remain_work,
			  rtw_coex_bt_multi_link_remain_work);
	INIT_DELAYED_WORK(&coex->wl_ccklock_work, rtw_coex_wl_ccklock_work);
	INIT_WORK(&rtwdev->tx_work, rtw_tx_work);
	INIT_WORK(&rtwdev->c2h_work, rtw_c2h_work);
	INIT_WORK(&rtwdev->ips_work, rtw_ips_work);
	INIT_WORK(&rtwdev->fw_recovery_work, rtw_fw_recovery_work);
	INIT_WORK(&rtwdev->update_beacon_work, rtw_fw_update_beacon_work);
	INIT_WORK(&rtwdev->ba_work, rtw_txq_ba_work);
	skb_queue_head_init(&rtwdev->c2h_queue);
	skb_queue_head_init(&rtwdev->coex.queue);
	skb_queue_head_init(&rtwdev->tx_report.queue);

	spin_lock_init(&rtwdev->rf_lock);
	spin_lock_init(&rtwdev->h2c.lock);
	spin_lock_init(&rtwdev->txq_lock);
	spin_lock_init(&rtwdev->tx_report.q_lock);

	mutex_init(&rtwdev->mutex);
	mutex_init(&rtwdev->coex.mutex);
	mutex_init(&rtwdev->hal.tx_power_mutex);

	init_waitqueue_head(&rtwdev->coex.wait);
	init_completion(&rtwdev->lps_leave_check);
	init_completion(&rtwdev->fw_scan_density);

	rtwdev->sec.total_cam_num = 32;
	rtwdev->hal.current_channel = 1;
	rtwdev->dm_info.fix_rate = U8_MAX;
	set_bit(RTW_BC_MC_MACID, rtwdev->mac_id_map);

	rtw_stats_init(rtwdev);

	/* default rx filter setting */
	rtwdev->hal.rcr = BIT_APP_FCS | BIT_APP_MIC | BIT_APP_ICV |
			  BIT_PKTCTL_DLEN | BIT_HTC_LOC_CTRL | BIT_APP_PHYSTS |
			  BIT_AB | BIT_AM | BIT_APM;

	ret = rtw_load_firmware(rtwdev, RTW_NORMAL_FW);
	if (ret) {
		rtw_warn(rtwdev, "no firmware loaded\n");
		goto out;
	}

	if (chip->wow_fw_name) {
		ret = rtw_load_firmware(rtwdev, RTW_WOWLAN_FW);
		if (ret) {
			rtw_warn(rtwdev, "no wow firmware loaded\n");
			wait_for_completion(&rtwdev->fw.completion);
			if (rtwdev->fw.firmware)
				release_firmware(rtwdev->fw.firmware);
			goto out;
		}
	}

	return 0;

out:
	destroy_workqueue(rtwdev->tx_wq);
	return ret;
}
EXPORT_SYMBOL(rtw_core_init);

void rtw_core_deinit(struct rtw_dev *rtwdev)
{
	struct rtw_fw_state *fw = &rtwdev->fw;
	struct rtw_fw_state *wow_fw = &rtwdev->wow_fw;
	struct rtw_rsvd_page *rsvd_pkt, *tmp;
	unsigned long flags;

	rtw_wait_firmware_completion(rtwdev);

	if (fw->firmware)
		release_firmware(fw->firmware);

	if (wow_fw->firmware)
		release_firmware(wow_fw->firmware);

	destroy_workqueue(rtwdev->tx_wq);
	spin_lock_irqsave(&rtwdev->tx_report.q_lock, flags);
	skb_queue_purge(&rtwdev->tx_report.queue);
	skb_queue_purge(&rtwdev->coex.queue);
	spin_unlock_irqrestore(&rtwdev->tx_report.q_lock, flags);

	list_for_each_entry_safe(rsvd_pkt, tmp, &rtwdev->rsvd_page_list,
				 build_list) {
		list_del(&rsvd_pkt->build_list);
		kfree(rsvd_pkt);
	}

	mutex_destroy(&rtwdev->mutex);
	mutex_destroy(&rtwdev->coex.mutex);
	mutex_destroy(&rtwdev->hal.tx_power_mutex);
}
EXPORT_SYMBOL(rtw_core_deinit);

int rtw_register_hw(struct rtw_dev *rtwdev, struct ieee80211_hw *hw)
{
	struct rtw_hal *hal = &rtwdev->hal;
	int max_tx_headroom = 0;
	int ret;

	/* TODO: USB & SDIO may need extra room? */
	max_tx_headroom = rtwdev->chip->tx_pkt_desc_sz;

	hw->extra_tx_headroom = max_tx_headroom;
	hw->queues = IEEE80211_NUM_ACS;
	hw->txq_data_size = sizeof(struct rtw_txq);
	hw->sta_data_size = sizeof(struct rtw_sta_info);
	hw->vif_data_size = sizeof(struct rtw_vif);

	ieee80211_hw_set(hw, SIGNAL_DBM);
	ieee80211_hw_set(hw, RX_INCLUDES_FCS);
	ieee80211_hw_set(hw, AMPDU_AGGREGATION);
	ieee80211_hw_set(hw, MFP_CAPABLE);
	ieee80211_hw_set(hw, REPORTS_TX_ACK_STATUS);
	ieee80211_hw_set(hw, SUPPORTS_PS);
	ieee80211_hw_set(hw, SUPPORTS_DYNAMIC_PS);
	ieee80211_hw_set(hw, SUPPORT_FAST_XMIT);
	ieee80211_hw_set(hw, SUPPORTS_AMSDU_IN_AMPDU);
	ieee80211_hw_set(hw, HAS_RATE_CONTROL);
	ieee80211_hw_set(hw, TX_AMSDU);
	ieee80211_hw_set(hw, SINGLE_SCAN_ON_ALL_BANDS);

	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				     BIT(NL80211_IFTYPE_AP) |
				     BIT(NL80211_IFTYPE_ADHOC) |
				     BIT(NL80211_IFTYPE_MESH_POINT);
	hw->wiphy->available_antennas_tx = hal->antenna_tx;
	hw->wiphy->available_antennas_rx = hal->antenna_rx;

	hw->wiphy->flags |= WIPHY_FLAG_SUPPORTS_TDLS |
			    WIPHY_FLAG_TDLS_EXTERNAL_SETUP;

	hw->wiphy->features |= NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR;
	hw->wiphy->max_scan_ssids = RTW_SCAN_MAX_SSIDS;
	hw->wiphy->max_scan_ie_len = rtw_get_max_scan_ie_len(rtwdev);

	wiphy_ext_feature_set(hw->wiphy, NL80211_EXT_FEATURE_CAN_REPLACE_PTK0);
	wiphy_ext_feature_set(hw->wiphy, NL80211_EXT_FEATURE_SCAN_RANDOM_SN);
	wiphy_ext_feature_set(hw->wiphy, NL80211_EXT_FEATURE_SET_SCAN_DWELL);

#ifdef CONFIG_PM
	hw->wiphy->wowlan = rtwdev->chip->wowlan_stub;
	hw->wiphy->max_sched_scan_ssids = rtwdev->chip->max_sched_scan_ssids;
#endif
	rtw_set_supported_band(hw, rtwdev->chip);
	SET_IEEE80211_PERM_ADDR(hw, rtwdev->efuse.addr);

	hw->wiphy->sar_capa = &rtw_sar_capa;

	ret = rtw_regd_init(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to init regd\n");
		return ret;
	}

	ret = ieee80211_register_hw(hw);
	if (ret) {
		rtw_err(rtwdev, "failed to register hw\n");
		return ret;
	}

	ret = rtw_regd_hint(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to hint regd\n");
		return ret;
	}

	rtw_debugfs_init(rtwdev);

	rtwdev->bf_info.bfer_mu_cnt = 0;
	rtwdev->bf_info.bfer_su_cnt = 0;

	return 0;
}
EXPORT_SYMBOL(rtw_register_hw);

void rtw_unregister_hw(struct rtw_dev *rtwdev, struct ieee80211_hw *hw)
{
	const struct rtw_chip_info *chip = rtwdev->chip;

	ieee80211_unregister_hw(hw);
	rtw_unset_supported_band(hw, chip);
}
EXPORT_SYMBOL(rtw_unregister_hw);

MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ac wireless core module");
MODULE_LICENSE("Dual BSD/GPL");
