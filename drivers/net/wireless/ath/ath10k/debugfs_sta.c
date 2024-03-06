// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2014-2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "core.h"
#include "wmi-ops.h"
#include "txrx.h"
#include "debug.h"

static void ath10k_rx_stats_update_amsdu_subfrm(struct ath10k *ar,
						struct ath10k_sta_tid_stats *stats,
						u32 msdu_count)
{
	if (msdu_count == 1)
		stats->rx_pkt_amsdu[ATH10K_AMSDU_SUBFRM_NUM_1]++;
	else if (msdu_count == 2)
		stats->rx_pkt_amsdu[ATH10K_AMSDU_SUBFRM_NUM_2]++;
	else if (msdu_count == 3)
		stats->rx_pkt_amsdu[ATH10K_AMSDU_SUBFRM_NUM_3]++;
	else if (msdu_count == 4)
		stats->rx_pkt_amsdu[ATH10K_AMSDU_SUBFRM_NUM_4]++;
	else if (msdu_count > 4)
		stats->rx_pkt_amsdu[ATH10K_AMSDU_SUBFRM_NUM_MORE]++;
}

static void ath10k_rx_stats_update_ampdu_subfrm(struct ath10k *ar,
						struct ath10k_sta_tid_stats *stats,
						u32 mpdu_count)
{
	if (mpdu_count <= 10)
		stats->rx_pkt_ampdu[ATH10K_AMPDU_SUBFRM_NUM_10]++;
	else if (mpdu_count <= 20)
		stats->rx_pkt_ampdu[ATH10K_AMPDU_SUBFRM_NUM_20]++;
	else if (mpdu_count <= 30)
		stats->rx_pkt_ampdu[ATH10K_AMPDU_SUBFRM_NUM_30]++;
	else if (mpdu_count <= 40)
		stats->rx_pkt_ampdu[ATH10K_AMPDU_SUBFRM_NUM_40]++;
	else if (mpdu_count <= 50)
		stats->rx_pkt_ampdu[ATH10K_AMPDU_SUBFRM_NUM_50]++;
	else if (mpdu_count <= 60)
		stats->rx_pkt_ampdu[ATH10K_AMPDU_SUBFRM_NUM_60]++;
	else if (mpdu_count > 60)
		stats->rx_pkt_ampdu[ATH10K_AMPDU_SUBFRM_NUM_MORE]++;
}

void ath10k_sta_update_rx_tid_stats_ampdu(struct ath10k *ar, u16 peer_id, u8 tid,
					  struct htt_rx_indication_mpdu_range *ranges,
					  int num_ranges)
{
	struct ath10k_sta *arsta;
	struct ath10k_peer *peer;
	int i;

	if (tid > IEEE80211_NUM_TIDS || !(ar->sta_tid_stats_mask & BIT(tid)))
		return;

	rcu_read_lock();
	spin_lock_bh(&ar->data_lock);

	peer = ath10k_peer_find_by_id(ar, peer_id);
	if (!peer || !peer->sta)
		goto out;

	arsta = (struct ath10k_sta *)peer->sta->drv_priv;

	for (i = 0; i < num_ranges; i++)
		ath10k_rx_stats_update_ampdu_subfrm(ar,
						    &arsta->tid_stats[tid],
						    ranges[i].mpdu_count);

out:
	spin_unlock_bh(&ar->data_lock);
	rcu_read_unlock();
}

void ath10k_sta_update_rx_tid_stats(struct ath10k *ar, u8 *first_hdr,
				    unsigned long num_msdus,
				    enum ath10k_pkt_rx_err err,
				    unsigned long unchain_cnt,
				    unsigned long drop_cnt,
				    unsigned long drop_cnt_filter,
				    unsigned long queued_msdus)
{
	struct ieee80211_sta *sta;
	struct ath10k_sta *arsta;
	struct ieee80211_hdr *hdr;
	struct ath10k_sta_tid_stats *stats;
	u8 tid = IEEE80211_NUM_TIDS;
	bool non_data_frm = false;

	hdr = (struct ieee80211_hdr *)first_hdr;
	if (!ieee80211_is_data(hdr->frame_control))
		non_data_frm = true;

	if (ieee80211_is_data_qos(hdr->frame_control))
		tid = *ieee80211_get_qos_ctl(hdr) & IEEE80211_QOS_CTL_TID_MASK;

	if (!(ar->sta_tid_stats_mask & BIT(tid)) || non_data_frm)
		return;

	rcu_read_lock();

	sta = ieee80211_find_sta_by_ifaddr(ar->hw, hdr->addr2, NULL);
	if (!sta)
		goto exit;

	arsta = (struct ath10k_sta *)sta->drv_priv;

	spin_lock_bh(&ar->data_lock);
	stats = &arsta->tid_stats[tid];
	stats->rx_pkt_from_fw += num_msdus;
	stats->rx_pkt_unchained += unchain_cnt;
	stats->rx_pkt_drop_chained += drop_cnt;
	stats->rx_pkt_drop_filter += drop_cnt_filter;
	if (err != ATH10K_PKT_RX_ERR_MAX)
		stats->rx_pkt_err[err] += queued_msdus;
	stats->rx_pkt_queued_for_mac += queued_msdus;
	ath10k_rx_stats_update_amsdu_subfrm(ar, &arsta->tid_stats[tid],
					    num_msdus);
	spin_unlock_bh(&ar->data_lock);

exit:
	rcu_read_unlock();
}

static void ath10k_sta_update_extd_stats_rx_duration(struct ath10k *ar,
						     struct ath10k_fw_stats *stats)
{
	struct ath10k_fw_extd_stats_peer *peer;
	struct ieee80211_sta *sta;
	struct ath10k_sta *arsta;

	rcu_read_lock();
	list_for_each_entry(peer, &stats->peers_extd, list) {
		sta = ieee80211_find_sta_by_ifaddr(ar->hw, peer->peer_macaddr,
						   NULL);
		if (!sta)
			continue;
		arsta = (struct ath10k_sta *)sta->drv_priv;
		arsta->rx_duration += (u64)peer->rx_duration;
	}
	rcu_read_unlock();
}

static void ath10k_sta_update_stats_rx_duration(struct ath10k *ar,
						struct ath10k_fw_stats *stats)
{
	struct ath10k_fw_stats_peer *peer;
	struct ieee80211_sta *sta;
	struct ath10k_sta *arsta;

	rcu_read_lock();
	list_for_each_entry(peer, &stats->peers, list) {
		sta = ieee80211_find_sta_by_ifaddr(ar->hw, peer->peer_macaddr,
						   NULL);
		if (!sta)
			continue;
		arsta = (struct ath10k_sta *)sta->drv_priv;
		arsta->rx_duration += (u64)peer->rx_duration;
	}
	rcu_read_unlock();
}

void ath10k_sta_update_rx_duration(struct ath10k *ar,
				   struct ath10k_fw_stats *stats)
{
	if (stats->extended)
		ath10k_sta_update_extd_stats_rx_duration(ar, stats);
	else
		ath10k_sta_update_stats_rx_duration(ar, stats);
}

static ssize_t ath10k_dbg_sta_read_aggr_mode(struct file *file,
					     char __user *user_buf,
					     size_t count, loff_t *ppos)
{
	struct ieee80211_sta *sta = file->private_data;
	struct ath10k_sta *arsta = (struct ath10k_sta *)sta->drv_priv;
	struct ath10k *ar = arsta->arvif->ar;
	char buf[32];
	int len = 0;

	mutex_lock(&ar->conf_mutex);
	len = scnprintf(buf, sizeof(buf) - len, "aggregation mode: %s\n",
			(arsta->aggr_mode == ATH10K_DBG_AGGR_MODE_AUTO) ?
			"auto" : "manual");
	mutex_unlock(&ar->conf_mutex);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath10k_dbg_sta_write_aggr_mode(struct file *file,
					      const char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	struct ieee80211_sta *sta = file->private_data;
	struct ath10k_sta *arsta = (struct ath10k_sta *)sta->drv_priv;
	struct ath10k *ar = arsta->arvif->ar;
	u32 aggr_mode;
	int ret;

	if (kstrtouint_from_user(user_buf, count, 0, &aggr_mode))
		return -EINVAL;

	if (aggr_mode >= ATH10K_DBG_AGGR_MODE_MAX)
		return -EINVAL;

	mutex_lock(&ar->conf_mutex);
	if ((ar->state != ATH10K_STATE_ON) ||
	    (aggr_mode == arsta->aggr_mode)) {
		ret = count;
		goto out;
	}

	ret = ath10k_wmi_addba_clear_resp(ar, arsta->arvif->vdev_id, sta->addr);
	if (ret) {
		ath10k_warn(ar, "failed to clear addba session ret: %d\n", ret);
		goto out;
	}

	arsta->aggr_mode = aggr_mode;
out:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static const struct file_operations fops_aggr_mode = {
	.read = ath10k_dbg_sta_read_aggr_mode,
	.write = ath10k_dbg_sta_write_aggr_mode,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath10k_dbg_sta_write_addba(struct file *file,
					  const char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct ieee80211_sta *sta = file->private_data;
	struct ath10k_sta *arsta = (struct ath10k_sta *)sta->drv_priv;
	struct ath10k *ar = arsta->arvif->ar;
	u32 tid, buf_size;
	int ret;
	char buf[64] = {0};

	ret = simple_write_to_buffer(buf, sizeof(buf) - 1, ppos,
				     user_buf, count);
	if (ret <= 0)
		return ret;

	ret = sscanf(buf, "%u %u", &tid, &buf_size);
	if (ret != 2)
		return -EINVAL;

	/* Valid TID values are 0 through 15 */
	if (tid > HTT_DATA_TX_EXT_TID_MGMT - 2)
		return -EINVAL;

	mutex_lock(&ar->conf_mutex);
	if ((ar->state != ATH10K_STATE_ON) ||
	    (arsta->aggr_mode != ATH10K_DBG_AGGR_MODE_MANUAL)) {
		ret = count;
		goto out;
	}

	ret = ath10k_wmi_addba_send(ar, arsta->arvif->vdev_id, sta->addr,
				    tid, buf_size);
	if (ret) {
		ath10k_warn(ar, "failed to send addba request: vdev_id %u peer %pM tid %u buf_size %u\n",
			    arsta->arvif->vdev_id, sta->addr, tid, buf_size);
	}

	ret = count;
out:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static const struct file_operations fops_addba = {
	.write = ath10k_dbg_sta_write_addba,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath10k_dbg_sta_write_addba_resp(struct file *file,
					       const char __user *user_buf,
					       size_t count, loff_t *ppos)
{
	struct ieee80211_sta *sta = file->private_data;
	struct ath10k_sta *arsta = (struct ath10k_sta *)sta->drv_priv;
	struct ath10k *ar = arsta->arvif->ar;
	u32 tid, status;
	int ret;
	char buf[64] = {0};

	ret = simple_write_to_buffer(buf, sizeof(buf) - 1, ppos,
				     user_buf, count);
	if (ret <= 0)
		return ret;

	ret = sscanf(buf, "%u %u", &tid, &status);
	if (ret != 2)
		return -EINVAL;

	/* Valid TID values are 0 through 15 */
	if (tid > HTT_DATA_TX_EXT_TID_MGMT - 2)
		return -EINVAL;

	mutex_lock(&ar->conf_mutex);
	if ((ar->state != ATH10K_STATE_ON) ||
	    (arsta->aggr_mode != ATH10K_DBG_AGGR_MODE_MANUAL)) {
		ret = count;
		goto out;
	}

	ret = ath10k_wmi_addba_set_resp(ar, arsta->arvif->vdev_id, sta->addr,
					tid, status);
	if (ret) {
		ath10k_warn(ar, "failed to send addba response: vdev_id %u peer %pM tid %u status%u\n",
			    arsta->arvif->vdev_id, sta->addr, tid, status);
	}
	ret = count;
out:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static const struct file_operations fops_addba_resp = {
	.write = ath10k_dbg_sta_write_addba_resp,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath10k_dbg_sta_write_delba(struct file *file,
					  const char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct ieee80211_sta *sta = file->private_data;
	struct ath10k_sta *arsta = (struct ath10k_sta *)sta->drv_priv;
	struct ath10k *ar = arsta->arvif->ar;
	u32 tid, initiator, reason;
	int ret;
	char buf[64] = {0};

	ret = simple_write_to_buffer(buf, sizeof(buf) - 1, ppos,
				     user_buf, count);
	if (ret <= 0)
		return ret;

	ret = sscanf(buf, "%u %u %u", &tid, &initiator, &reason);
	if (ret != 3)
		return -EINVAL;

	/* Valid TID values are 0 through 15 */
	if (tid > HTT_DATA_TX_EXT_TID_MGMT - 2)
		return -EINVAL;

	mutex_lock(&ar->conf_mutex);
	if ((ar->state != ATH10K_STATE_ON) ||
	    (arsta->aggr_mode != ATH10K_DBG_AGGR_MODE_MANUAL)) {
		ret = count;
		goto out;
	}

	ret = ath10k_wmi_delba_send(ar, arsta->arvif->vdev_id, sta->addr,
				    tid, initiator, reason);
	if (ret) {
		ath10k_warn(ar, "failed to send delba: vdev_id %u peer %pM tid %u initiator %u reason %u\n",
			    arsta->arvif->vdev_id, sta->addr, tid, initiator,
			    reason);
	}
	ret = count;
out:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static const struct file_operations fops_delba = {
	.write = ath10k_dbg_sta_write_delba,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath10k_dbg_sta_read_peer_debug_trigger(struct file *file,
						      char __user *user_buf,
						      size_t count,
						      loff_t *ppos)
{
	struct ieee80211_sta *sta = file->private_data;
	struct ath10k_sta *arsta = (struct ath10k_sta *)sta->drv_priv;
	struct ath10k *ar = arsta->arvif->ar;
	char buf[8];
	int len = 0;

	mutex_lock(&ar->conf_mutex);
	len = scnprintf(buf, sizeof(buf) - len,
			"Write 1 to once trigger the debug logs\n");
	mutex_unlock(&ar->conf_mutex);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t
ath10k_dbg_sta_write_peer_debug_trigger(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct ieee80211_sta *sta = file->private_data;
	struct ath10k_sta *arsta = (struct ath10k_sta *)sta->drv_priv;
	struct ath10k *ar = arsta->arvif->ar;
	u8 peer_debug_trigger;
	int ret;

	if (kstrtou8_from_user(user_buf, count, 0, &peer_debug_trigger))
		return -EINVAL;

	if (peer_debug_trigger != 1)
		return -EINVAL;

	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH10K_STATE_ON) {
		ret = -ENETDOWN;
		goto out;
	}

	ret = ath10k_wmi_peer_set_param(ar, arsta->arvif->vdev_id, sta->addr,
					ar->wmi.peer_param->debug, peer_debug_trigger);
	if (ret) {
		ath10k_warn(ar, "failed to set param to trigger peer tid logs for station ret: %d\n",
			    ret);
		goto out;
	}
out:
	mutex_unlock(&ar->conf_mutex);
	return count;
}

static const struct file_operations fops_peer_debug_trigger = {
	.open = simple_open,
	.read = ath10k_dbg_sta_read_peer_debug_trigger,
	.write = ath10k_dbg_sta_write_peer_debug_trigger,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath10k_dbg_sta_read_peer_ps_state(struct file *file,
						 char __user *user_buf,
						 size_t count, loff_t *ppos)
{
	struct ieee80211_sta *sta = file->private_data;
	struct ath10k_sta *arsta = (struct ath10k_sta *)sta->drv_priv;
	struct ath10k *ar = arsta->arvif->ar;
	char buf[20];
	int len = 0;

	spin_lock_bh(&ar->data_lock);

	len = scnprintf(buf, sizeof(buf) - len, "%d\n",
			arsta->peer_ps_state);

	spin_unlock_bh(&ar->data_lock);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations fops_peer_ps_state = {
	.open = simple_open,
	.read = ath10k_dbg_sta_read_peer_ps_state,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static char *get_err_str(enum ath10k_pkt_rx_err i)
{
	switch (i) {
	case ATH10K_PKT_RX_ERR_FCS:
		return "fcs_err";
	case ATH10K_PKT_RX_ERR_TKIP:
		return "tkip_err";
	case ATH10K_PKT_RX_ERR_CRYPT:
		return "crypt_err";
	case ATH10K_PKT_RX_ERR_PEER_IDX_INVAL:
		return "peer_idx_inval";
	case ATH10K_PKT_RX_ERR_MAX:
		return "unknown";
	}

	return "unknown";
}

static char *get_num_ampdu_subfrm_str(enum ath10k_ampdu_subfrm_num i)
{
	switch (i) {
	case ATH10K_AMPDU_SUBFRM_NUM_10:
		return "up to 10";
	case ATH10K_AMPDU_SUBFRM_NUM_20:
		return "11-20";
	case ATH10K_AMPDU_SUBFRM_NUM_30:
		return "21-30";
	case ATH10K_AMPDU_SUBFRM_NUM_40:
		return "31-40";
	case ATH10K_AMPDU_SUBFRM_NUM_50:
		return "41-50";
	case ATH10K_AMPDU_SUBFRM_NUM_60:
		return "51-60";
	case ATH10K_AMPDU_SUBFRM_NUM_MORE:
		return ">60";
	case ATH10K_AMPDU_SUBFRM_NUM_MAX:
		return "0";
	}

	return "0";
}

static char *get_num_amsdu_subfrm_str(enum ath10k_amsdu_subfrm_num i)
{
	switch (i) {
	case ATH10K_AMSDU_SUBFRM_NUM_1:
		return "1";
	case ATH10K_AMSDU_SUBFRM_NUM_2:
		return "2";
	case ATH10K_AMSDU_SUBFRM_NUM_3:
		return "3";
	case ATH10K_AMSDU_SUBFRM_NUM_4:
		return "4";
	case ATH10K_AMSDU_SUBFRM_NUM_MORE:
		return ">4";
	case ATH10K_AMSDU_SUBFRM_NUM_MAX:
		return "0";
	}

	return "0";
}

#define PRINT_TID_STATS(_field, _tabs) \
	do { \
		int k = 0; \
		for (j = 0; j <= IEEE80211_NUM_TIDS; j++) { \
			if (ar->sta_tid_stats_mask & BIT(j))  { \
				len += scnprintf(buf + len, buf_len - len, \
						 "[%02d] %-10lu  ", \
						 j, stats[j]._field); \
				k++; \
				if (k % 8 == 0)  { \
					len += scnprintf(buf + len, \
							 buf_len - len, "\n"); \
					len += scnprintf(buf + len, \
							 buf_len - len, \
							 _tabs); \
				} \
			} \
		} \
		len += scnprintf(buf + len, buf_len - len, "\n"); \
	} while (0)

static ssize_t ath10k_dbg_sta_read_tid_stats(struct file *file,
					     char __user *user_buf,
					     size_t count, loff_t *ppos)
{
	struct ieee80211_sta *sta = file->private_data;
	struct ath10k_sta *arsta = (struct ath10k_sta *)sta->drv_priv;
	struct ath10k *ar = arsta->arvif->ar;
	struct ath10k_sta_tid_stats *stats = arsta->tid_stats;
	size_t len = 0, buf_len = 1048 * IEEE80211_NUM_TIDS;
	char *buf;
	int i, j;
	ssize_t ret;

	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&ar->conf_mutex);

	spin_lock_bh(&ar->data_lock);

	len += scnprintf(buf + len, buf_len - len,
			 "\n\t\tDriver Rx pkt stats per tid, ([tid] count)\n");
	len += scnprintf(buf + len, buf_len - len,
			 "\t\t------------------------------------------\n");
	len += scnprintf(buf + len, buf_len - len, "MSDUs from FW\t\t\t");
	PRINT_TID_STATS(rx_pkt_from_fw, "\t\t\t\t");

	len += scnprintf(buf + len, buf_len - len, "MSDUs unchained\t\t\t");
	PRINT_TID_STATS(rx_pkt_unchained, "\t\t\t\t");

	len += scnprintf(buf + len, buf_len - len,
			 "MSDUs locally dropped:chained\t");
	PRINT_TID_STATS(rx_pkt_drop_chained, "\t\t\t\t");

	len += scnprintf(buf + len, buf_len - len,
			 "MSDUs locally dropped:filtered\t");
	PRINT_TID_STATS(rx_pkt_drop_filter, "\t\t\t\t");

	len += scnprintf(buf + len, buf_len - len,
			 "MSDUs queued for mac80211\t");
	PRINT_TID_STATS(rx_pkt_queued_for_mac, "\t\t\t\t");

	for (i = 0; i < ATH10K_PKT_RX_ERR_MAX; i++) {
		len += scnprintf(buf + len, buf_len - len,
				 "MSDUs with error:%s\t", get_err_str(i));
		PRINT_TID_STATS(rx_pkt_err[i], "\t\t\t\t");
	}

	len += scnprintf(buf + len, buf_len - len, "\n");
	for (i = 0; i < ATH10K_AMPDU_SUBFRM_NUM_MAX; i++) {
		len += scnprintf(buf + len, buf_len - len,
				 "A-MPDU num subframes %s\t",
				 get_num_ampdu_subfrm_str(i));
		PRINT_TID_STATS(rx_pkt_ampdu[i], "\t\t\t\t");
	}

	len += scnprintf(buf + len, buf_len - len, "\n");
	for (i = 0; i < ATH10K_AMSDU_SUBFRM_NUM_MAX; i++) {
		len += scnprintf(buf + len, buf_len - len,
				 "A-MSDU num subframes %s\t\t",
				 get_num_amsdu_subfrm_str(i));
		PRINT_TID_STATS(rx_pkt_amsdu[i], "\t\t\t\t");
	}

	spin_unlock_bh(&ar->data_lock);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	kfree(buf);

	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static const struct file_operations fops_tid_stats_dump = {
	.open = simple_open,
	.read = ath10k_dbg_sta_read_tid_stats,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath10k_dbg_sta_dump_tx_stats(struct file *file,
					    char __user *user_buf,
					    size_t count, loff_t *ppos)
{
	struct ieee80211_sta *sta = file->private_data;
	struct ath10k_sta *arsta = (struct ath10k_sta *)sta->drv_priv;
	struct ath10k *ar = arsta->arvif->ar;
	struct ath10k_htt_data_stats *stats;
	const char *str_name[ATH10K_STATS_TYPE_MAX] = {"succ", "fail",
						       "retry", "ampdu"};
	const char *str[ATH10K_COUNTER_TYPE_MAX] = {"bytes", "packets"};
	int len = 0, i, j, k, retval = 0;
	const int size = 16 * 4096;
	char *buf;

	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&ar->conf_mutex);

	if (!arsta->tx_stats) {
		ath10k_warn(ar, "failed to get tx stats");
		mutex_unlock(&ar->conf_mutex);
		kfree(buf);
		return 0;
	}

	spin_lock_bh(&ar->data_lock);
	for (k = 0; k < ATH10K_STATS_TYPE_MAX; k++) {
		for (j = 0; j < ATH10K_COUNTER_TYPE_MAX; j++) {
			stats = &arsta->tx_stats->stats[k];
			len += scnprintf(buf + len, size - len, "%s_%s\n",
					 str_name[k],
					 str[j]);
			len += scnprintf(buf + len, size - len,
					 " VHT MCS %s\n",
					 str[j]);
			for (i = 0; i < ATH10K_VHT_MCS_NUM; i++)
				len += scnprintf(buf + len, size - len,
						 "  %llu ",
						 stats->vht[j][i]);
			len += scnprintf(buf + len, size - len, "\n");
			len += scnprintf(buf + len, size - len, " HT MCS %s\n",
					 str[j]);
			for (i = 0; i < ATH10K_HT_MCS_NUM; i++)
				len += scnprintf(buf + len, size - len,
						 "  %llu ", stats->ht[j][i]);
			len += scnprintf(buf + len, size - len, "\n");
			len += scnprintf(buf + len, size - len,
					" BW %s (20,5,10,40,80,160 MHz)\n", str[j]);
			len += scnprintf(buf + len, size - len,
					 "  %llu %llu %llu %llu %llu %llu\n",
					 stats->bw[j][0], stats->bw[j][1],
					 stats->bw[j][2], stats->bw[j][3],
					 stats->bw[j][4], stats->bw[j][5]);
			len += scnprintf(buf + len, size - len,
					 " NSS %s (1x1,2x2,3x3,4x4)\n", str[j]);
			len += scnprintf(buf + len, size - len,
					 "  %llu %llu %llu %llu\n",
					 stats->nss[j][0], stats->nss[j][1],
					 stats->nss[j][2], stats->nss[j][3]);
			len += scnprintf(buf + len, size - len,
					 " GI %s (LGI,SGI)\n",
					 str[j]);
			len += scnprintf(buf + len, size - len, "  %llu %llu\n",
					 stats->gi[j][0], stats->gi[j][1]);
			len += scnprintf(buf + len, size - len,
					 " legacy rate %s (1,2 ... Mbps)\n  ",
					 str[j]);
			for (i = 0; i < ATH10K_LEGACY_NUM; i++)
				len += scnprintf(buf + len, size - len, "%llu ",
						 stats->legacy[j][i]);
			len += scnprintf(buf + len, size - len, "\n");
			len += scnprintf(buf + len, size - len,
					 " Rate table %s (1,2 ... Mbps)\n  ",
					 str[j]);
			for (i = 0; i < ATH10K_RATE_TABLE_NUM; i++) {
				len += scnprintf(buf + len, size - len, "%llu ",
						 stats->rate_table[j][i]);
				if (!((i + 1) % 8))
					len +=
					scnprintf(buf + len, size - len, "\n  ");
			}
		}
	}

	len += scnprintf(buf + len, size - len,
			 "\nTX duration\n %llu usecs\n",
			 arsta->tx_stats->tx_duration);
	len += scnprintf(buf + len, size - len,
			"BA fails\n %llu\n", arsta->tx_stats->ba_fails);
	len += scnprintf(buf + len, size - len,
			"ack fails\n %llu\n", arsta->tx_stats->ack_fails);
	spin_unlock_bh(&ar->data_lock);

	if (len > size)
		len = size;
	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	mutex_unlock(&ar->conf_mutex);
	return retval;
}

static const struct file_operations fops_tx_stats = {
	.read = ath10k_dbg_sta_dump_tx_stats,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

void ath10k_sta_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, struct dentry *dir)
{
	struct ath10k *ar = hw->priv;

	debugfs_create_file("aggr_mode", 0644, dir, sta, &fops_aggr_mode);
	debugfs_create_file("addba", 0200, dir, sta, &fops_addba);
	debugfs_create_file("addba_resp", 0200, dir, sta, &fops_addba_resp);
	debugfs_create_file("delba", 0200, dir, sta, &fops_delba);
	debugfs_create_file("peer_debug_trigger", 0600, dir, sta,
			    &fops_peer_debug_trigger);
	debugfs_create_file("dump_tid_stats", 0400, dir, sta,
			    &fops_tid_stats_dump);

	if (ath10k_peer_stats_enabled(ar) &&
	    ath10k_debug_is_extd_tx_stats_enabled(ar))
		debugfs_create_file("tx_stats", 0400, dir, sta,
				    &fops_tx_stats);
	debugfs_create_file("peer_ps_state", 0400, dir, sta,
			    &fops_peer_ps_state);
}
