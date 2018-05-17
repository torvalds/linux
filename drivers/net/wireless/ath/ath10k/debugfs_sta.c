/*
 * Copyright (c) 2014-2017 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "core.h"
#include "wmi-ops.h"
#include "debug.h"

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
	char buf[64];

	simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, user_buf, count);

	/* make sure that buf is null terminated */
	buf[sizeof(buf) - 1] = '\0';

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
	char buf[64];

	simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, user_buf, count);

	/* make sure that buf is null terminated */
	buf[sizeof(buf) - 1] = '\0';

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
	char buf[64];

	simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, user_buf, count);

	/* make sure that buf is null terminated */
	buf[sizeof(buf) - 1] = '\0';

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
					WMI_PEER_DEBUG, peer_debug_trigger);
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

void ath10k_sta_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, struct dentry *dir)
{
	debugfs_create_file("aggr_mode", 0644, dir, sta, &fops_aggr_mode);
	debugfs_create_file("addba", 0200, dir, sta, &fops_addba);
	debugfs_create_file("addba_resp", 0200, dir, sta, &fops_addba_resp);
	debugfs_create_file("delba", 0200, dir, sta, &fops_delba);
	debugfs_create_file("peer_debug_trigger", 0600, dir, sta,
			    &fops_peer_debug_trigger);
}
