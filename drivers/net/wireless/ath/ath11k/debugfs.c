// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2020 The Linux Foundation. All rights reserved.
 */

#include <linux/vmalloc.h>

#include "debugfs.h"

#include "core.h"
#include "debug.h"
#include "wmi.h"
#include "hal_rx.h"
#include "dp_tx.h"
#include "debugfs_htt_stats.h"
#include "peer.h"
#include "hif.h"

static const char *htt_bp_umac_ring[HTT_SW_UMAC_RING_IDX_MAX] = {
	"REO2SW1_RING",
	"REO2SW2_RING",
	"REO2SW3_RING",
	"REO2SW4_RING",
	"WBM2REO_LINK_RING",
	"REO2TCL_RING",
	"REO2FW_RING",
	"RELEASE_RING",
	"PPE_RELEASE_RING",
	"TCL2TQM_RING",
	"TQM_RELEASE_RING",
	"REO_RELEASE_RING",
	"WBM2SW0_RELEASE_RING",
	"WBM2SW1_RELEASE_RING",
	"WBM2SW2_RELEASE_RING",
	"WBM2SW3_RELEASE_RING",
	"REO_CMD_RING",
	"REO_STATUS_RING",
};

static const char *htt_bp_lmac_ring[HTT_SW_LMAC_RING_IDX_MAX] = {
	"FW2RXDMA_BUF_RING",
	"FW2RXDMA_STATUS_RING",
	"FW2RXDMA_LINK_RING",
	"SW2RXDMA_BUF_RING",
	"WBM2RXDMA_LINK_RING",
	"RXDMA2FW_RING",
	"RXDMA2SW_RING",
	"RXDMA2RELEASE_RING",
	"RXDMA2REO_RING",
	"MONITOR_STATUS_RING",
	"MONITOR_BUF_RING",
	"MONITOR_DESC_RING",
	"MONITOR_DEST_RING",
};

void ath11k_debugfs_add_dbring_entry(struct ath11k *ar,
				     enum wmi_direct_buffer_module id,
				     enum ath11k_dbg_dbr_event event,
				     struct hal_srng *srng)
{
	struct ath11k_debug_dbr *dbr_debug;
	struct ath11k_dbg_dbr_data *dbr_data;
	struct ath11k_dbg_dbr_entry *entry;

	if (id >= WMI_DIRECT_BUF_MAX || event >= ATH11K_DBG_DBR_EVENT_MAX)
		return;

	dbr_debug = ar->debug.dbr_debug[id];
	if (!dbr_debug)
		return;

	if (!dbr_debug->dbr_debug_enabled)
		return;

	dbr_data = &dbr_debug->dbr_dbg_data;

	spin_lock_bh(&dbr_data->lock);

	if (dbr_data->entries) {
		entry = &dbr_data->entries[dbr_data->dbr_debug_idx];
		entry->hp = srng->u.src_ring.hp;
		entry->tp = *srng->u.src_ring.tp_addr;
		entry->timestamp = jiffies;
		entry->event = event;

		dbr_data->dbr_debug_idx++;
		if (dbr_data->dbr_debug_idx ==
		    dbr_data->num_ring_debug_entries)
			dbr_data->dbr_debug_idx = 0;
	}

	spin_unlock_bh(&dbr_data->lock);
}

static void ath11k_debugfs_fw_stats_reset(struct ath11k *ar)
{
	spin_lock_bh(&ar->data_lock);
	ar->fw_stats_done = false;
	ath11k_fw_stats_pdevs_free(&ar->fw_stats.pdevs);
	ath11k_fw_stats_vdevs_free(&ar->fw_stats.vdevs);
	spin_unlock_bh(&ar->data_lock);
}

void ath11k_debugfs_fw_stats_process(struct ath11k *ar, struct ath11k_fw_stats *stats)
{
	struct ath11k_base *ab = ar->ab;
	struct ath11k_pdev *pdev;
	bool is_end;
	static unsigned int num_vdev, num_bcn;
	size_t total_vdevs_started = 0;
	int i;

	/* WMI_REQUEST_PDEV_STAT request has been already processed */

	if (stats->stats_id == WMI_REQUEST_RSSI_PER_CHAIN_STAT) {
		ar->fw_stats_done = true;
		return;
	}

	if (stats->stats_id == WMI_REQUEST_VDEV_STAT) {
		if (list_empty(&stats->vdevs)) {
			ath11k_warn(ab, "empty vdev stats");
			return;
		}
		/* FW sends all the active VDEV stats irrespective of PDEV,
		 * hence limit until the count of all VDEVs started
		 */
		for (i = 0; i < ab->num_radios; i++) {
			pdev = rcu_dereference(ab->pdevs_active[i]);
			if (pdev && pdev->ar)
				total_vdevs_started += ar->num_started_vdevs;
		}

		is_end = ((++num_vdev) == total_vdevs_started);

		list_splice_tail_init(&stats->vdevs,
				      &ar->fw_stats.vdevs);

		if (is_end) {
			ar->fw_stats_done = true;
			num_vdev = 0;
		}
		return;
	}

	if (stats->stats_id == WMI_REQUEST_BCN_STAT) {
		if (list_empty(&stats->bcn)) {
			ath11k_warn(ab, "empty bcn stats");
			return;
		}
		/* Mark end until we reached the count of all started VDEVs
		 * within the PDEV
		 */
		is_end = ((++num_bcn) == ar->num_started_vdevs);

		list_splice_tail_init(&stats->bcn,
				      &ar->fw_stats.bcn);

		if (is_end) {
			ar->fw_stats_done = true;
			num_bcn = 0;
		}
	}
}

static int ath11k_debugfs_fw_stats_request(struct ath11k *ar,
					   struct stats_request_params *req_param)
{
	struct ath11k_base *ab = ar->ab;
	unsigned long timeout, time_left;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	/* FW stats can get split when exceeding the stats data buffer limit.
	 * In that case, since there is no end marking for the back-to-back
	 * received 'update stats' event, we keep a 3 seconds timeout in case,
	 * fw_stats_done is not marked yet
	 */
	timeout = jiffies + msecs_to_jiffies(3 * 1000);

	ath11k_debugfs_fw_stats_reset(ar);

	reinit_completion(&ar->fw_stats_complete);

	ret = ath11k_wmi_send_stats_request_cmd(ar, req_param);

	if (ret) {
		ath11k_warn(ab, "could not request fw stats (%d)\n",
			    ret);
		return ret;
	}

	time_left = wait_for_completion_timeout(&ar->fw_stats_complete, 1 * HZ);

	if (!time_left)
		return -ETIMEDOUT;

	for (;;) {
		if (time_after(jiffies, timeout))
			break;

		spin_lock_bh(&ar->data_lock);
		if (ar->fw_stats_done) {
			spin_unlock_bh(&ar->data_lock);
			break;
		}
		spin_unlock_bh(&ar->data_lock);
	}
	return 0;
}

int ath11k_debugfs_get_fw_stats(struct ath11k *ar, u32 pdev_id,
				u32 vdev_id, u32 stats_id)
{
	struct ath11k_base *ab = ar->ab;
	struct stats_request_params req_param;
	int ret;

	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH11K_STATE_ON) {
		ret = -ENETDOWN;
		goto err_unlock;
	}

	req_param.pdev_id = pdev_id;
	req_param.vdev_id = vdev_id;
	req_param.stats_id = stats_id;

	ret = ath11k_debugfs_fw_stats_request(ar, &req_param);
	if (ret)
		ath11k_warn(ab, "failed to request fw stats: %d\n", ret);

	ath11k_dbg(ab, ATH11K_DBG_WMI,
		   "debug get fw stat pdev id %d vdev id %d stats id 0x%x\n",
		   pdev_id, vdev_id, stats_id);

err_unlock:
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static int ath11k_open_pdev_stats(struct inode *inode, struct file *file)
{
	struct ath11k *ar = inode->i_private;
	struct ath11k_base *ab = ar->ab;
	struct stats_request_params req_param;
	void *buf = NULL;
	int ret;

	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH11K_STATE_ON) {
		ret = -ENETDOWN;
		goto err_unlock;
	}

	buf = vmalloc(ATH11K_FW_STATS_BUF_SIZE);
	if (!buf) {
		ret = -ENOMEM;
		goto err_unlock;
	}

	req_param.pdev_id = ar->pdev->pdev_id;
	req_param.vdev_id = 0;
	req_param.stats_id = WMI_REQUEST_PDEV_STAT;

	ret = ath11k_debugfs_fw_stats_request(ar, &req_param);
	if (ret) {
		ath11k_warn(ab, "failed to request fw pdev stats: %d\n", ret);
		goto err_free;
	}

	ath11k_wmi_fw_stats_fill(ar, &ar->fw_stats, req_param.stats_id, buf);

	file->private_data = buf;

	mutex_unlock(&ar->conf_mutex);
	return 0;

err_free:
	vfree(buf);

err_unlock:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static int ath11k_release_pdev_stats(struct inode *inode, struct file *file)
{
	vfree(file->private_data);

	return 0;
}

static ssize_t ath11k_read_pdev_stats(struct file *file,
				      char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	const char *buf = file->private_data;
	size_t len = strlen(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations fops_pdev_stats = {
	.open = ath11k_open_pdev_stats,
	.release = ath11k_release_pdev_stats,
	.read = ath11k_read_pdev_stats,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int ath11k_open_vdev_stats(struct inode *inode, struct file *file)
{
	struct ath11k *ar = inode->i_private;
	struct stats_request_params req_param;
	void *buf = NULL;
	int ret;

	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH11K_STATE_ON) {
		ret = -ENETDOWN;
		goto err_unlock;
	}

	buf = vmalloc(ATH11K_FW_STATS_BUF_SIZE);
	if (!buf) {
		ret = -ENOMEM;
		goto err_unlock;
	}

	req_param.pdev_id = ar->pdev->pdev_id;
	/* VDEV stats is always sent for all active VDEVs from FW */
	req_param.vdev_id = 0;
	req_param.stats_id = WMI_REQUEST_VDEV_STAT;

	ret = ath11k_debugfs_fw_stats_request(ar, &req_param);
	if (ret) {
		ath11k_warn(ar->ab, "failed to request fw vdev stats: %d\n", ret);
		goto err_free;
	}

	ath11k_wmi_fw_stats_fill(ar, &ar->fw_stats, req_param.stats_id, buf);

	file->private_data = buf;

	mutex_unlock(&ar->conf_mutex);
	return 0;

err_free:
	vfree(buf);

err_unlock:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static int ath11k_release_vdev_stats(struct inode *inode, struct file *file)
{
	vfree(file->private_data);

	return 0;
}

static ssize_t ath11k_read_vdev_stats(struct file *file,
				      char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	const char *buf = file->private_data;
	size_t len = strlen(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations fops_vdev_stats = {
	.open = ath11k_open_vdev_stats,
	.release = ath11k_release_vdev_stats,
	.read = ath11k_read_vdev_stats,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int ath11k_open_bcn_stats(struct inode *inode, struct file *file)
{
	struct ath11k *ar = inode->i_private;
	struct ath11k_vif *arvif;
	struct stats_request_params req_param;
	void *buf = NULL;
	int ret;

	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH11K_STATE_ON) {
		ret = -ENETDOWN;
		goto err_unlock;
	}

	buf = vmalloc(ATH11K_FW_STATS_BUF_SIZE);
	if (!buf) {
		ret = -ENOMEM;
		goto err_unlock;
	}

	req_param.stats_id = WMI_REQUEST_BCN_STAT;
	req_param.pdev_id = ar->pdev->pdev_id;

	/* loop all active VDEVs for bcn stats */
	list_for_each_entry(arvif, &ar->arvifs, list) {
		if (!arvif->is_up)
			continue;

		req_param.vdev_id = arvif->vdev_id;
		ret = ath11k_debugfs_fw_stats_request(ar, &req_param);
		if (ret) {
			ath11k_warn(ar->ab, "failed to request fw bcn stats: %d\n", ret);
			goto err_free;
		}
	}

	ath11k_wmi_fw_stats_fill(ar, &ar->fw_stats, req_param.stats_id, buf);

	/* since beacon stats request is looped for all active VDEVs, saved fw
	 * stats is not freed for each request until done for all active VDEVs
	 */
	spin_lock_bh(&ar->data_lock);
	ath11k_fw_stats_bcn_free(&ar->fw_stats.bcn);
	spin_unlock_bh(&ar->data_lock);

	file->private_data = buf;

	mutex_unlock(&ar->conf_mutex);
	return 0;

err_free:
	vfree(buf);

err_unlock:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static int ath11k_release_bcn_stats(struct inode *inode, struct file *file)
{
	vfree(file->private_data);

	return 0;
}

static ssize_t ath11k_read_bcn_stats(struct file *file,
				     char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	const char *buf = file->private_data;
	size_t len = strlen(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations fops_bcn_stats = {
	.open = ath11k_open_bcn_stats,
	.release = ath11k_release_bcn_stats,
	.read = ath11k_read_bcn_stats,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath11k_read_simulate_fw_crash(struct file *file,
					     char __user *user_buf,
					     size_t count, loff_t *ppos)
{
	const char buf[] =
		"To simulate firmware crash write one of the keywords to this file:\n"
		"`assert` - this will send WMI_FORCE_FW_HANG_CMDID to firmware to cause assert.\n"
		"`hw-restart` - this will simply queue hw restart without fw/hw actually crashing.\n";

	return simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
}

/* Simulate firmware crash:
 * 'soft': Call wmi command causing firmware hang. This firmware hang is
 * recoverable by warm firmware reset.
 * 'hard': Force firmware crash by setting any vdev parameter for not allowed
 * vdev id. This is hard firmware crash because it is recoverable only by cold
 * firmware reset.
 */
static ssize_t ath11k_write_simulate_fw_crash(struct file *file,
					      const char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	struct ath11k_base *ab = file->private_data;
	struct ath11k_pdev *pdev;
	struct ath11k *ar = ab->pdevs[0].ar;
	char buf[32] = {0};
	ssize_t rc;
	int i, ret, radioup = 0;

	for (i = 0; i < ab->num_radios; i++) {
		pdev = &ab->pdevs[i];
		ar = pdev->ar;
		if (ar && ar->state == ATH11K_STATE_ON) {
			radioup = 1;
			break;
		}
	}
	/* filter partial writes and invalid commands */
	if (*ppos != 0 || count >= sizeof(buf) || count == 0)
		return -EINVAL;

	rc = simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, user_buf, count);
	if (rc < 0)
		return rc;

	/* drop the possible '\n' from the end */
	if (buf[*ppos - 1] == '\n')
		buf[*ppos - 1] = '\0';

	if (radioup == 0) {
		ret = -ENETDOWN;
		goto exit;
	}

	if (!strcmp(buf, "assert")) {
		ath11k_info(ab, "simulating firmware assert crash\n");
		ret = ath11k_wmi_force_fw_hang_cmd(ar,
						   ATH11K_WMI_FW_HANG_ASSERT_TYPE,
						   ATH11K_WMI_FW_HANG_DELAY);
	} else if (!strcmp(buf, "hw-restart")) {
		ath11k_info(ab, "user requested hw restart\n");
		queue_work(ab->workqueue_aux, &ab->reset_work);
		ret = 0;
	} else {
		ret = -EINVAL;
		goto exit;
	}

	if (ret) {
		ath11k_warn(ab, "failed to simulate firmware crash: %d\n", ret);
		goto exit;
	}

	ret = count;

exit:
	return ret;
}

static const struct file_operations fops_simulate_fw_crash = {
	.read = ath11k_read_simulate_fw_crash,
	.write = ath11k_write_simulate_fw_crash,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath11k_write_enable_extd_tx_stats(struct file *file,
						 const char __user *ubuf,
						 size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	u32 filter;
	int ret;

	if (kstrtouint_from_user(ubuf, count, 0, &filter))
		return -EINVAL;

	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH11K_STATE_ON) {
		ret = -ENETDOWN;
		goto out;
	}

	if (filter == ar->debug.extd_tx_stats) {
		ret = count;
		goto out;
	}

	ar->debug.extd_tx_stats = filter;
	ret = count;

out:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static ssize_t ath11k_read_enable_extd_tx_stats(struct file *file,
						char __user *ubuf,
						size_t count, loff_t *ppos)

{
	char buf[32] = {0};
	struct ath11k *ar = file->private_data;
	int len = 0;

	mutex_lock(&ar->conf_mutex);
	len = scnprintf(buf, sizeof(buf) - len, "%08x\n",
			ar->debug.extd_tx_stats);
	mutex_unlock(&ar->conf_mutex);

	return simple_read_from_buffer(ubuf, count, ppos, buf, len);
}

static const struct file_operations fops_extd_tx_stats = {
	.read = ath11k_read_enable_extd_tx_stats,
	.write = ath11k_write_enable_extd_tx_stats,
	.open = simple_open
};

static ssize_t ath11k_write_extd_rx_stats(struct file *file,
					  const char __user *ubuf,
					  size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	struct ath11k_base *ab = ar->ab;
	struct htt_rx_ring_tlv_filter tlv_filter = {0};
	u32 enable, rx_filter = 0, ring_id;
	int i;
	int ret;

	if (kstrtouint_from_user(ubuf, count, 0, &enable))
		return -EINVAL;

	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH11K_STATE_ON) {
		ret = -ENETDOWN;
		goto exit;
	}

	if (enable > 1) {
		ret = -EINVAL;
		goto exit;
	}

	if (enable == ar->debug.extd_rx_stats) {
		ret = count;
		goto exit;
	}

	if (test_bit(ATH11K_FLAG_MONITOR_STARTED, &ar->monitor_flags)) {
		ar->debug.extd_rx_stats = enable;
		ret = count;
		goto exit;
	}

	if (enable) {
		rx_filter =  HTT_RX_FILTER_TLV_FLAGS_MPDU_START;
		rx_filter |= HTT_RX_FILTER_TLV_FLAGS_PPDU_START;
		rx_filter |= HTT_RX_FILTER_TLV_FLAGS_PPDU_END;
		rx_filter |= HTT_RX_FILTER_TLV_FLAGS_PPDU_END_USER_STATS;
		rx_filter |= HTT_RX_FILTER_TLV_FLAGS_PPDU_END_USER_STATS_EXT;
		rx_filter |= HTT_RX_FILTER_TLV_FLAGS_PPDU_END_STATUS_DONE;

		tlv_filter.rx_filter = rx_filter;
		tlv_filter.pkt_filter_flags0 = HTT_RX_FP_MGMT_FILTER_FLAGS0;
		tlv_filter.pkt_filter_flags1 = HTT_RX_FP_MGMT_FILTER_FLAGS1;
		tlv_filter.pkt_filter_flags2 = HTT_RX_FP_CTRL_FILTER_FLASG2;
		tlv_filter.pkt_filter_flags3 = HTT_RX_FP_CTRL_FILTER_FLASG3 |
			HTT_RX_FP_DATA_FILTER_FLASG3;
	} else {
		tlv_filter = ath11k_mac_mon_status_filter_default;
	}

	ar->debug.rx_filter = tlv_filter.rx_filter;

	for (i = 0; i < ab->hw_params.num_rxmda_per_pdev; i++) {
		ring_id = ar->dp.rx_mon_status_refill_ring[i].refill_buf_ring.ring_id;
		ret = ath11k_dp_tx_htt_rx_filter_setup(ar->ab, ring_id, ar->dp.mac_id,
						       HAL_RXDMA_MONITOR_STATUS,
						       DP_RX_BUFFER_SIZE, &tlv_filter);

		if (ret) {
			ath11k_warn(ar->ab, "failed to set rx filter for monitor status ring\n");
			goto exit;
		}
	}

	ar->debug.extd_rx_stats = enable;
	ret = count;
exit:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static ssize_t ath11k_read_extd_rx_stats(struct file *file,
					 char __user *ubuf,
					 size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	char buf[32];
	int len = 0;

	mutex_lock(&ar->conf_mutex);
	len = scnprintf(buf, sizeof(buf) - len, "%d\n",
			ar->debug.extd_rx_stats);
	mutex_unlock(&ar->conf_mutex);

	return simple_read_from_buffer(ubuf, count, ppos, buf, len);
}

static const struct file_operations fops_extd_rx_stats = {
	.read = ath11k_read_extd_rx_stats,
	.write = ath11k_write_extd_rx_stats,
	.open = simple_open,
};

static int ath11k_fill_bp_stats(struct ath11k_base *ab,
				struct ath11k_bp_stats *bp_stats,
				char *buf, int len, int size)
{
	lockdep_assert_held(&ab->base_lock);

	len += scnprintf(buf + len, size - len, "count: %u\n",
			 bp_stats->count);
	len += scnprintf(buf + len, size - len, "hp: %u\n",
			 bp_stats->hp);
	len += scnprintf(buf + len, size - len, "tp: %u\n",
			 bp_stats->tp);
	len += scnprintf(buf + len, size - len, "seen before: %ums\n\n",
			 jiffies_to_msecs(jiffies - bp_stats->jiffies));
	return len;
}

static ssize_t ath11k_debugfs_dump_soc_ring_bp_stats(struct ath11k_base *ab,
						     char *buf, int size)
{
	struct ath11k_bp_stats *bp_stats;
	bool stats_rxd = false;
	u8 i, pdev_idx;
	int len = 0;

	len += scnprintf(buf + len, size - len, "\nBackpressure Stats\n");
	len += scnprintf(buf + len, size - len, "==================\n");

	spin_lock_bh(&ab->base_lock);
	for (i = 0; i < HTT_SW_UMAC_RING_IDX_MAX; i++) {
		bp_stats = &ab->soc_stats.bp_stats.umac_ring_bp_stats[i];

		if (!bp_stats->count)
			continue;

		len += scnprintf(buf + len, size - len, "Ring: %s\n",
				 htt_bp_umac_ring[i]);
		len = ath11k_fill_bp_stats(ab, bp_stats, buf, len, size);
		stats_rxd = true;
	}

	for (i = 0; i < HTT_SW_LMAC_RING_IDX_MAX; i++) {
		for (pdev_idx = 0; pdev_idx < MAX_RADIOS; pdev_idx++) {
			bp_stats =
				&ab->soc_stats.bp_stats.lmac_ring_bp_stats[i][pdev_idx];

			if (!bp_stats->count)
				continue;

			len += scnprintf(buf + len, size - len, "Ring: %s\n",
					 htt_bp_lmac_ring[i]);
			len += scnprintf(buf + len, size - len, "pdev: %d\n",
					 pdev_idx);
			len = ath11k_fill_bp_stats(ab, bp_stats, buf, len, size);
			stats_rxd = true;
		}
	}
	spin_unlock_bh(&ab->base_lock);

	if (!stats_rxd)
		len += scnprintf(buf + len, size - len,
				 "No Ring Backpressure stats received\n\n");

	return len;
}

static ssize_t ath11k_debugfs_dump_soc_dp_stats(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct ath11k_base *ab = file->private_data;
	struct ath11k_soc_dp_stats *soc_stats = &ab->soc_stats;
	int len = 0, i, retval;
	const int size = 4096;
	static const char *rxdma_err[HAL_REO_ENTR_RING_RXDMA_ECODE_MAX] = {
			"Overflow", "MPDU len", "FCS", "Decrypt", "TKIP MIC",
			"Unencrypt", "MSDU len", "MSDU limit", "WiFi parse",
			"AMSDU parse", "SA timeout", "DA timeout",
			"Flow timeout", "Flush req"};
	static const char *reo_err[HAL_REO_DEST_RING_ERROR_CODE_MAX] = {
			"Desc addr zero", "Desc inval", "AMPDU in non BA",
			"Non BA dup", "BA dup", "Frame 2k jump", "BAR 2k jump",
			"Frame OOR", "BAR OOR", "No BA session",
			"Frame SN equal SSN", "PN check fail", "2k err",
			"PN err", "Desc blocked"};

	char *buf;

	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, size - len, "SOC RX STATS:\n\n");
	len += scnprintf(buf + len, size - len, "err ring pkts: %u\n",
			 soc_stats->err_ring_pkts);
	len += scnprintf(buf + len, size - len, "Invalid RBM: %u\n\n",
			 soc_stats->invalid_rbm);
	len += scnprintf(buf + len, size - len, "RXDMA errors:\n");
	for (i = 0; i < HAL_REO_ENTR_RING_RXDMA_ECODE_MAX; i++)
		len += scnprintf(buf + len, size - len, "%s: %u\n",
				 rxdma_err[i], soc_stats->rxdma_error[i]);

	len += scnprintf(buf + len, size - len, "\nREO errors:\n");
	for (i = 0; i < HAL_REO_DEST_RING_ERROR_CODE_MAX; i++)
		len += scnprintf(buf + len, size - len, "%s: %u\n",
				 reo_err[i], soc_stats->reo_error[i]);

	len += scnprintf(buf + len, size - len, "\nHAL REO errors:\n");
	len += scnprintf(buf + len, size - len,
			 "ring0: %u\nring1: %u\nring2: %u\nring3: %u\n",
			 soc_stats->hal_reo_error[0],
			 soc_stats->hal_reo_error[1],
			 soc_stats->hal_reo_error[2],
			 soc_stats->hal_reo_error[3]);

	len += scnprintf(buf + len, size - len, "\nSOC TX STATS:\n");
	len += scnprintf(buf + len, size - len, "\nTCL Ring Full Failures:\n");

	for (i = 0; i < ab->hw_params.max_tx_ring; i++)
		len += scnprintf(buf + len, size - len, "ring%d: %u\n",
				 i, soc_stats->tx_err.desc_na[i]);

	len += scnprintf(buf + len, size - len,
			 "\nMisc Transmit Failures: %d\n",
			 atomic_read(&soc_stats->tx_err.misc_fail));

	len += ath11k_debugfs_dump_soc_ring_bp_stats(ab, buf + len, size - len);

	if (len > size)
		len = size;
	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

static const struct file_operations fops_soc_dp_stats = {
	.read = ath11k_debugfs_dump_soc_dp_stats,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath11k_write_fw_dbglog(struct file *file,
				      const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	char buf[128] = {0};
	struct ath11k_fw_dbglog dbglog;
	unsigned int param, mod_id_index, is_end;
	u64 value;
	int ret, num;

	ret = simple_write_to_buffer(buf, sizeof(buf) - 1, ppos,
				     user_buf, count);
	if (ret <= 0)
		return ret;

	num = sscanf(buf, "%u %llx %u %u", &param, &value, &mod_id_index, &is_end);

	if (num < 2)
		return -EINVAL;

	mutex_lock(&ar->conf_mutex);
	if (param == WMI_DEBUG_LOG_PARAM_MOD_ENABLE_BITMAP ||
	    param == WMI_DEBUG_LOG_PARAM_WOW_MOD_ENABLE_BITMAP) {
		if (num != 4 || mod_id_index > (MAX_MODULE_ID_BITMAP_WORDS - 1)) {
			ret = -EINVAL;
			goto out;
		}
		ar->debug.module_id_bitmap[mod_id_index] = upper_32_bits(value);
		if (!is_end) {
			ret = count;
			goto out;
		}
	} else {
		if (num != 2) {
			ret = -EINVAL;
			goto out;
		}
	}

	dbglog.param = param;
	dbglog.value = lower_32_bits(value);
	ret = ath11k_wmi_fw_dbglog_cfg(ar, ar->debug.module_id_bitmap, &dbglog);
	if (ret) {
		ath11k_warn(ar->ab, "fw dbglog config failed from debugfs: %d\n",
			    ret);
		goto out;
	}

	ret = count;

out:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static const struct file_operations fops_fw_dbglog = {
	.write = ath11k_write_fw_dbglog,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int ath11k_open_sram_dump(struct inode *inode, struct file *file)
{
	struct ath11k_base *ab = inode->i_private;
	u8 *buf;
	u32 start, end;
	int ret;

	start = ab->hw_params.sram_dump.start;
	end = ab->hw_params.sram_dump.end;

	buf = vmalloc(end - start + 1);
	if (!buf)
		return -ENOMEM;

	ret = ath11k_hif_read(ab, buf, start, end);
	if (ret) {
		ath11k_warn(ab, "failed to dump sram: %d\n", ret);
		vfree(buf);
		return ret;
	}

	file->private_data = buf;
	return 0;
}

static ssize_t ath11k_read_sram_dump(struct file *file,
				     char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct ath11k_base *ab = file->f_inode->i_private;
	const char *buf = file->private_data;
	int len;
	u32 start, end;

	start = ab->hw_params.sram_dump.start;
	end = ab->hw_params.sram_dump.end;
	len = end - start + 1;

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static int ath11k_release_sram_dump(struct inode *inode, struct file *file)
{
	vfree(file->private_data);
	file->private_data = NULL;

	return 0;
}

static const struct file_operations fops_sram_dump = {
	.open = ath11k_open_sram_dump,
	.read = ath11k_read_sram_dump,
	.release = ath11k_release_sram_dump,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

int ath11k_debugfs_pdev_create(struct ath11k_base *ab)
{
	if (test_bit(ATH11K_FLAG_REGISTERED, &ab->dev_flags))
		return 0;

	debugfs_create_file("simulate_fw_crash", 0600, ab->debugfs_soc, ab,
			    &fops_simulate_fw_crash);

	debugfs_create_file("soc_dp_stats", 0600, ab->debugfs_soc, ab,
			    &fops_soc_dp_stats);

	if (ab->hw_params.sram_dump.start != 0)
		debugfs_create_file("sram", 0400, ab->debugfs_soc, ab,
				    &fops_sram_dump);

	return 0;
}

void ath11k_debugfs_pdev_destroy(struct ath11k_base *ab)
{
	debugfs_remove_recursive(ab->debugfs_soc);
	ab->debugfs_soc = NULL;
}

int ath11k_debugfs_soc_create(struct ath11k_base *ab)
{
	struct dentry *root;
	bool dput_needed;
	char name[64];
	int ret;

	root = debugfs_lookup("ath11k", NULL);
	if (!root) {
		root = debugfs_create_dir("ath11k", NULL);
		if (IS_ERR_OR_NULL(root))
			return PTR_ERR(root);

		dput_needed = false;
	} else {
		/* a dentry from lookup() needs dput() after we don't use it */
		dput_needed = true;
	}

	scnprintf(name, sizeof(name), "%s-%s", ath11k_bus_str(ab->hif.bus),
		  dev_name(ab->dev));

	ab->debugfs_soc = debugfs_create_dir(name, root);
	if (IS_ERR_OR_NULL(ab->debugfs_soc)) {
		ret = PTR_ERR(ab->debugfs_soc);
		goto out;
	}

	ret = 0;

out:
	if (dput_needed)
		dput(root);

	return ret;
}

void ath11k_debugfs_soc_destroy(struct ath11k_base *ab)
{
	debugfs_remove_recursive(ab->debugfs_soc);
	ab->debugfs_soc = NULL;

	/* We are not removing ath11k directory on purpose, even if it
	 * would be empty. This simplifies the directory handling and it's
	 * a minor cosmetic issue to leave an empty ath11k directory to
	 * debugfs.
	 */
}
EXPORT_SYMBOL(ath11k_debugfs_soc_destroy);

void ath11k_debugfs_fw_stats_init(struct ath11k *ar)
{
	struct dentry *fwstats_dir = debugfs_create_dir("fw_stats",
							ar->debug.debugfs_pdev);

	ar->fw_stats.debugfs_fwstats = fwstats_dir;

	/* all stats debugfs files created are under "fw_stats" directory
	 * created per PDEV
	 */
	debugfs_create_file("pdev_stats", 0600, fwstats_dir, ar,
			    &fops_pdev_stats);
	debugfs_create_file("vdev_stats", 0600, fwstats_dir, ar,
			    &fops_vdev_stats);
	debugfs_create_file("beacon_stats", 0600, fwstats_dir, ar,
			    &fops_bcn_stats);
}

static ssize_t ath11k_write_pktlog_filter(struct file *file,
					  const char __user *ubuf,
					  size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	struct ath11k_base *ab = ar->ab;
	struct htt_rx_ring_tlv_filter tlv_filter = {0};
	u32 rx_filter = 0, ring_id, filter, mode;
	u8 buf[128] = {0};
	int i, ret, rx_buf_sz = 0;
	ssize_t rc;

	mutex_lock(&ar->conf_mutex);
	if (ar->state != ATH11K_STATE_ON) {
		ret = -ENETDOWN;
		goto out;
	}

	rc = simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, ubuf, count);
	if (rc < 0) {
		ret = rc;
		goto out;
	}
	buf[rc] = '\0';

	ret = sscanf(buf, "0x%x %u", &filter, &mode);
	if (ret != 2) {
		ret = -EINVAL;
		goto out;
	}

	if (filter) {
		ret = ath11k_wmi_pdev_pktlog_enable(ar, filter);
		if (ret) {
			ath11k_warn(ar->ab,
				    "failed to enable pktlog filter %x: %d\n",
				    ar->debug.pktlog_filter, ret);
			goto out;
		}
	} else {
		ret = ath11k_wmi_pdev_pktlog_disable(ar);
		if (ret) {
			ath11k_warn(ar->ab, "failed to disable pktlog: %d\n", ret);
			goto out;
		}
	}

	/* Clear rx filter set for monitor mode and rx status */
	for (i = 0; i < ab->hw_params.num_rxmda_per_pdev; i++) {
		ring_id = ar->dp.rx_mon_status_refill_ring[i].refill_buf_ring.ring_id;
		ret = ath11k_dp_tx_htt_rx_filter_setup(ar->ab, ring_id, ar->dp.mac_id,
						       HAL_RXDMA_MONITOR_STATUS,
						       rx_buf_sz, &tlv_filter);
		if (ret) {
			ath11k_warn(ar->ab, "failed to set rx filter for monitor status ring\n");
			goto out;
		}
	}
#define HTT_RX_FILTER_TLV_LITE_MODE \
			(HTT_RX_FILTER_TLV_FLAGS_PPDU_START | \
			HTT_RX_FILTER_TLV_FLAGS_PPDU_END | \
			HTT_RX_FILTER_TLV_FLAGS_PPDU_END_USER_STATS | \
			HTT_RX_FILTER_TLV_FLAGS_PPDU_END_USER_STATS_EXT | \
			HTT_RX_FILTER_TLV_FLAGS_PPDU_END_STATUS_DONE | \
			HTT_RX_FILTER_TLV_FLAGS_MPDU_START)

	if (mode == ATH11K_PKTLOG_MODE_FULL) {
		rx_filter = HTT_RX_FILTER_TLV_LITE_MODE |
			    HTT_RX_FILTER_TLV_FLAGS_MSDU_START |
			    HTT_RX_FILTER_TLV_FLAGS_MSDU_END |
			    HTT_RX_FILTER_TLV_FLAGS_MPDU_END |
			    HTT_RX_FILTER_TLV_FLAGS_PACKET_HEADER |
			    HTT_RX_FILTER_TLV_FLAGS_ATTENTION;
		rx_buf_sz = DP_RX_BUFFER_SIZE;
	} else if (mode == ATH11K_PKTLOG_MODE_LITE) {
		ret = ath11k_dp_tx_htt_h2t_ppdu_stats_req(ar,
							  HTT_PPDU_STATS_TAG_PKTLOG);
		if (ret) {
			ath11k_err(ar->ab, "failed to enable pktlog lite: %d\n", ret);
			goto out;
		}

		rx_filter = HTT_RX_FILTER_TLV_LITE_MODE;
		rx_buf_sz = DP_RX_BUFFER_SIZE_LITE;
	} else {
		rx_buf_sz = DP_RX_BUFFER_SIZE;
		tlv_filter = ath11k_mac_mon_status_filter_default;
		rx_filter = tlv_filter.rx_filter;

		ret = ath11k_dp_tx_htt_h2t_ppdu_stats_req(ar,
							  HTT_PPDU_STATS_TAG_DEFAULT);
		if (ret) {
			ath11k_err(ar->ab, "failed to send htt ppdu stats req: %d\n",
				   ret);
			goto out;
		}
	}

	tlv_filter.rx_filter = rx_filter;
	if (rx_filter) {
		tlv_filter.pkt_filter_flags0 = HTT_RX_FP_MGMT_FILTER_FLAGS0;
		tlv_filter.pkt_filter_flags1 = HTT_RX_FP_MGMT_FILTER_FLAGS1;
		tlv_filter.pkt_filter_flags2 = HTT_RX_FP_CTRL_FILTER_FLASG2;
		tlv_filter.pkt_filter_flags3 = HTT_RX_FP_CTRL_FILTER_FLASG3 |
					       HTT_RX_FP_DATA_FILTER_FLASG3;
	}

	for (i = 0; i < ab->hw_params.num_rxmda_per_pdev; i++) {
		ring_id = ar->dp.rx_mon_status_refill_ring[i].refill_buf_ring.ring_id;
		ret = ath11k_dp_tx_htt_rx_filter_setup(ab, ring_id,
						       ar->dp.mac_id + i,
						       HAL_RXDMA_MONITOR_STATUS,
						       rx_buf_sz, &tlv_filter);

		if (ret) {
			ath11k_warn(ab, "failed to set rx filter for monitor status ring\n");
			goto out;
		}
	}

	ath11k_info(ab, "pktlog mode %s\n",
		    ((mode == ATH11K_PKTLOG_MODE_FULL) ? "full" : "lite"));

	ar->debug.pktlog_filter = filter;
	ar->debug.pktlog_mode = mode;
	ret = count;

out:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static ssize_t ath11k_read_pktlog_filter(struct file *file,
					 char __user *ubuf,
					 size_t count, loff_t *ppos)

{
	char buf[32] = {0};
	struct ath11k *ar = file->private_data;
	int len = 0;

	mutex_lock(&ar->conf_mutex);
	len = scnprintf(buf, sizeof(buf) - len, "%08x %08x\n",
			ar->debug.pktlog_filter,
			ar->debug.pktlog_mode);
	mutex_unlock(&ar->conf_mutex);

	return simple_read_from_buffer(ubuf, count, ppos, buf, len);
}

static const struct file_operations fops_pktlog_filter = {
	.read = ath11k_read_pktlog_filter,
	.write = ath11k_write_pktlog_filter,
	.open = simple_open
};

static ssize_t ath11k_write_simulate_radar(struct file *file,
					   const char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	int ret;

	ret = ath11k_wmi_simulate_radar(ar);
	if (ret)
		return ret;

	return count;
}

static const struct file_operations fops_simulate_radar = {
	.write = ath11k_write_simulate_radar,
	.open = simple_open
};

static ssize_t ath11k_debug_dump_dbr_entries(struct file *file,
					     char __user *user_buf,
					     size_t count, loff_t *ppos)
{
	struct ath11k_dbg_dbr_data *dbr_dbg_data = file->private_data;
	static const char * const event_id_to_string[] = {"empty", "Rx", "Replenish"};
	int size = ATH11K_DEBUG_DBR_ENTRIES_MAX * 100;
	char *buf;
	int i, ret;
	int len = 0;

	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, size - len,
			 "-----------------------------------------\n");
	len += scnprintf(buf + len, size - len,
			 "| idx |  hp  |  tp  | timestamp |  event |\n");
	len += scnprintf(buf + len, size - len,
			 "-----------------------------------------\n");

	spin_lock_bh(&dbr_dbg_data->lock);

	for (i = 0; i < dbr_dbg_data->num_ring_debug_entries; i++) {
		len += scnprintf(buf + len, size - len,
				 "|%4u|%8u|%8u|%11llu|%8s|\n", i,
				 dbr_dbg_data->entries[i].hp,
				 dbr_dbg_data->entries[i].tp,
				 dbr_dbg_data->entries[i].timestamp,
				 event_id_to_string[dbr_dbg_data->entries[i].event]);
	}

	spin_unlock_bh(&dbr_dbg_data->lock);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return ret;
}

static const struct file_operations fops_debug_dump_dbr_entries = {
	.read = ath11k_debug_dump_dbr_entries,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static void ath11k_debugfs_dbr_dbg_destroy(struct ath11k *ar, int dbr_id)
{
	struct ath11k_debug_dbr *dbr_debug;
	struct ath11k_dbg_dbr_data *dbr_dbg_data;

	if (!ar->debug.dbr_debug[dbr_id])
		return;

	dbr_debug = ar->debug.dbr_debug[dbr_id];
	dbr_dbg_data = &dbr_debug->dbr_dbg_data;

	debugfs_remove_recursive(dbr_debug->dbr_debugfs);
	kfree(dbr_dbg_data->entries);
	kfree(dbr_debug);
	ar->debug.dbr_debug[dbr_id] = NULL;
}

static int ath11k_debugfs_dbr_dbg_init(struct ath11k *ar, int dbr_id)
{
	struct ath11k_debug_dbr *dbr_debug;
	struct ath11k_dbg_dbr_data *dbr_dbg_data;
	static const char * const dbr_id_to_str[] = {"spectral", "CFR"};

	if (ar->debug.dbr_debug[dbr_id])
		return 0;

	ar->debug.dbr_debug[dbr_id] = kzalloc(sizeof(*dbr_debug),
					      GFP_KERNEL);

	if (!ar->debug.dbr_debug[dbr_id])
		return -ENOMEM;

	dbr_debug = ar->debug.dbr_debug[dbr_id];
	dbr_dbg_data = &dbr_debug->dbr_dbg_data;

	if (dbr_debug->dbr_debugfs)
		return 0;

	dbr_debug->dbr_debugfs = debugfs_create_dir(dbr_id_to_str[dbr_id],
						    ar->debug.debugfs_pdev);
	if (IS_ERR_OR_NULL(dbr_debug->dbr_debugfs)) {
		if (IS_ERR(dbr_debug->dbr_debugfs))
			return PTR_ERR(dbr_debug->dbr_debugfs);
		return -ENOMEM;
	}

	dbr_debug->dbr_debug_enabled = true;
	dbr_dbg_data->num_ring_debug_entries = ATH11K_DEBUG_DBR_ENTRIES_MAX;
	dbr_dbg_data->dbr_debug_idx = 0;
	dbr_dbg_data->entries = kcalloc(ATH11K_DEBUG_DBR_ENTRIES_MAX,
					sizeof(struct ath11k_dbg_dbr_entry),
					GFP_KERNEL);
	if (!dbr_dbg_data->entries)
		return -ENOMEM;

	spin_lock_init(&dbr_dbg_data->lock);

	debugfs_create_file("dump_dbr_debug", 0444, dbr_debug->dbr_debugfs,
			    dbr_dbg_data, &fops_debug_dump_dbr_entries);

	return 0;
}

static ssize_t ath11k_debugfs_write_enable_dbr_dbg(struct file *file,
						   const char __user *ubuf,
						   size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	char buf[32] = {0};
	u32 dbr_id, enable;
	int ret;

	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH11K_STATE_ON) {
		ret = -ENETDOWN;
		goto out;
	}

	ret = simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, ubuf, count);
	if (ret < 0)
		goto out;

	buf[ret] = '\0';
	ret = sscanf(buf, "%u %u", &dbr_id, &enable);
	if (ret != 2 || dbr_id > 1 || enable > 1) {
		ret = -EINVAL;
		ath11k_warn(ar->ab, "usage: echo <dbr_id> <val> dbr_id:0-Spectral 1-CFR val:0-disable 1-enable\n");
		goto out;
	}

	if (enable) {
		ret = ath11k_debugfs_dbr_dbg_init(ar, dbr_id);
		if (ret) {
			ath11k_warn(ar->ab, "db ring module debugfs init failed: %d\n",
				    ret);
			goto out;
		}
	} else {
		ath11k_debugfs_dbr_dbg_destroy(ar, dbr_id);
	}

	ret = count;
out:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static const struct file_operations fops_dbr_debug = {
	.write = ath11k_debugfs_write_enable_dbr_dbg,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath11k_write_ps_timekeeper_enable(struct file *file,
						 const char __user *user_buf,
						 size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	ssize_t ret;
	u8 ps_timekeeper_enable;

	if (kstrtou8_from_user(user_buf, count, 0, &ps_timekeeper_enable))
		return -EINVAL;

	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH11K_STATE_ON) {
		ret = -ENETDOWN;
		goto exit;
	}

	if (!ar->ps_state_enable) {
		ret = -EINVAL;
		goto exit;
	}

	ar->ps_timekeeper_enable = !!ps_timekeeper_enable;
	ret = count;
exit:
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static ssize_t ath11k_read_ps_timekeeper_enable(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	char buf[32];
	int len;

	mutex_lock(&ar->conf_mutex);
	len = scnprintf(buf, sizeof(buf), "%d\n", ar->ps_timekeeper_enable);
	mutex_unlock(&ar->conf_mutex);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations fops_ps_timekeeper_enable = {
	.read = ath11k_read_ps_timekeeper_enable,
	.write = ath11k_write_ps_timekeeper_enable,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static void ath11k_reset_peer_ps_duration(void *data,
					  struct ieee80211_sta *sta)
{
	struct ath11k *ar = data;
	struct ath11k_sta *arsta = (struct ath11k_sta *)sta->drv_priv;

	spin_lock_bh(&ar->data_lock);
	arsta->ps_total_duration = 0;
	spin_unlock_bh(&ar->data_lock);
}

static ssize_t ath11k_write_reset_ps_duration(struct file *file,
					      const  char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	int ret;
	u8 reset_ps_duration;

	if (kstrtou8_from_user(user_buf, count, 0, &reset_ps_duration))
		return -EINVAL;

	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH11K_STATE_ON) {
		ret = -ENETDOWN;
		goto exit;
	}

	if (!ar->ps_state_enable) {
		ret = -EINVAL;
		goto exit;
	}

	ieee80211_iterate_stations_atomic(ar->hw,
					  ath11k_reset_peer_ps_duration,
					  ar);

	ret = count;
exit:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static const struct file_operations fops_reset_ps_duration = {
	.write = ath11k_write_reset_ps_duration,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static void ath11k_peer_ps_state_disable(void *data,
					 struct ieee80211_sta *sta)
{
	struct ath11k *ar = data;
	struct ath11k_sta *arsta = (struct ath11k_sta *)sta->drv_priv;

	spin_lock_bh(&ar->data_lock);
	arsta->peer_ps_state = WMI_PEER_PS_STATE_DISABLED;
	arsta->ps_start_time = 0;
	arsta->ps_total_duration = 0;
	spin_unlock_bh(&ar->data_lock);
}

static ssize_t ath11k_write_ps_state_enable(struct file *file,
					    const char __user *user_buf,
					    size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	struct ath11k_pdev *pdev = ar->pdev;
	int ret;
	u32 param;
	u8 ps_state_enable;

	if (kstrtou8_from_user(user_buf, count, 0, &ps_state_enable))
		return -EINVAL;

	mutex_lock(&ar->conf_mutex);

	ps_state_enable = !!ps_state_enable;

	if (ar->ps_state_enable == ps_state_enable) {
		ret = count;
		goto exit;
	}

	param = WMI_PDEV_PEER_STA_PS_STATECHG_ENABLE;
	ret = ath11k_wmi_pdev_set_param(ar, param, ps_state_enable, pdev->pdev_id);
	if (ret) {
		ath11k_warn(ar->ab, "failed to enable ps_state_enable: %d\n",
			    ret);
		goto exit;
	}
	ar->ps_state_enable = ps_state_enable;

	if (!ar->ps_state_enable) {
		ar->ps_timekeeper_enable = false;
		ieee80211_iterate_stations_atomic(ar->hw,
						  ath11k_peer_ps_state_disable,
						  ar);
	}

	ret = count;

exit:
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static ssize_t ath11k_read_ps_state_enable(struct file *file,
					   char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	char buf[32];
	int len;

	mutex_lock(&ar->conf_mutex);
	len = scnprintf(buf, sizeof(buf), "%d\n", ar->ps_state_enable);
	mutex_unlock(&ar->conf_mutex);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations fops_ps_state_enable = {
	.read = ath11k_read_ps_state_enable,
	.write = ath11k_write_ps_state_enable,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

int ath11k_debugfs_register(struct ath11k *ar)
{
	struct ath11k_base *ab = ar->ab;
	char pdev_name[5];
	char buf[100] = {0};

	snprintf(pdev_name, sizeof(pdev_name), "%s%d", "mac", ar->pdev_idx);

	ar->debug.debugfs_pdev = debugfs_create_dir(pdev_name, ab->debugfs_soc);
	if (IS_ERR(ar->debug.debugfs_pdev))
		return PTR_ERR(ar->debug.debugfs_pdev);

	/* Create a symlink under ieee80211/phy* */
	snprintf(buf, 100, "../../ath11k/%pd2", ar->debug.debugfs_pdev);
	debugfs_create_symlink("ath11k", ar->hw->wiphy->debugfsdir, buf);

	ath11k_debugfs_htt_stats_init(ar);

	ath11k_debugfs_fw_stats_init(ar);

	debugfs_create_file("ext_tx_stats", 0644,
			    ar->debug.debugfs_pdev, ar,
			    &fops_extd_tx_stats);
	debugfs_create_file("ext_rx_stats", 0644,
			    ar->debug.debugfs_pdev, ar,
			    &fops_extd_rx_stats);
	debugfs_create_file("pktlog_filter", 0644,
			    ar->debug.debugfs_pdev, ar,
			    &fops_pktlog_filter);
	debugfs_create_file("fw_dbglog_config", 0600,
			    ar->debug.debugfs_pdev, ar,
			    &fops_fw_dbglog);

	if (ar->hw->wiphy->bands[NL80211_BAND_5GHZ]) {
		debugfs_create_file("dfs_simulate_radar", 0200,
				    ar->debug.debugfs_pdev, ar,
				    &fops_simulate_radar);
		debugfs_create_bool("dfs_block_radar_events", 0200,
				    ar->debug.debugfs_pdev,
				    &ar->dfs_block_radar_events);
	}

	if (ab->hw_params.dbr_debug_support)
		debugfs_create_file("enable_dbr_debug", 0200, ar->debug.debugfs_pdev,
				    ar, &fops_dbr_debug);

	debugfs_create_file("ps_state_enable", 0600, ar->debug.debugfs_pdev, ar,
			    &fops_ps_state_enable);

	if (test_bit(WMI_TLV_SERVICE_PEER_POWER_SAVE_DURATION_SUPPORT,
		     ar->ab->wmi_ab.svc_map)) {
		debugfs_create_file("ps_timekeeper_enable", 0600,
				    ar->debug.debugfs_pdev, ar,
				    &fops_ps_timekeeper_enable);

		debugfs_create_file("reset_ps_duration", 0200,
				    ar->debug.debugfs_pdev, ar,
				    &fops_reset_ps_duration);
	}

	return 0;
}

void ath11k_debugfs_unregister(struct ath11k *ar)
{
	struct ath11k_debug_dbr *dbr_debug;
	struct ath11k_dbg_dbr_data *dbr_dbg_data;
	int i;

	for (i = 0; i < WMI_DIRECT_BUF_MAX; i++) {
		dbr_debug = ar->debug.dbr_debug[i];
		if (!dbr_debug)
			continue;

		dbr_dbg_data = &dbr_debug->dbr_dbg_data;
		kfree(dbr_dbg_data->entries);
		debugfs_remove_recursive(dbr_debug->dbr_debugfs);
		kfree(dbr_debug);
		ar->debug.dbr_debug[i] = NULL;
	}
}

static ssize_t ath11k_write_twt_add_dialog(struct file *file,
					   const char __user *ubuf,
					   size_t count, loff_t *ppos)
{
	struct ath11k_vif *arvif = file->private_data;
	struct wmi_twt_add_dialog_params params = { 0 };
	struct wmi_twt_enable_params twt_params = {0};
	struct ath11k *ar = arvif->ar;
	u8 buf[128] = {0};
	int ret;

	if (ar->twt_enabled == 0) {
		ath11k_err(ar->ab, "twt support is not enabled\n");
		return -EOPNOTSUPP;
	}

	ret = simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, ubuf, count);
	if (ret < 0)
		return ret;

	buf[ret] = '\0';
	ret = sscanf(buf,
		     "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx %u %u %u %u %u %hhu %hhu %hhu %hhu %hhu",
		     &params.peer_macaddr[0],
		     &params.peer_macaddr[1],
		     &params.peer_macaddr[2],
		     &params.peer_macaddr[3],
		     &params.peer_macaddr[4],
		     &params.peer_macaddr[5],
		     &params.dialog_id,
		     &params.wake_intvl_us,
		     &params.wake_intvl_mantis,
		     &params.wake_dura_us,
		     &params.sp_offset_us,
		     &params.twt_cmd,
		     &params.flag_bcast,
		     &params.flag_trigger,
		     &params.flag_flow_type,
		     &params.flag_protection);
	if (ret != 16)
		return -EINVAL;

	/* In the case of station vif, TWT is entirely handled by
	 * the firmware based on the input parameters in the TWT enable
	 * WMI command that is sent to the target during assoc.
	 * For manually testing the TWT feature, we need to first disable
	 * TWT and send enable command again with TWT input parameter
	 * sta_cong_timer_ms set to 0.
	 */
	if (arvif->vif->type == NL80211_IFTYPE_STATION) {
		ath11k_wmi_send_twt_disable_cmd(ar, ar->pdev->pdev_id);

		ath11k_wmi_fill_default_twt_params(&twt_params);
		twt_params.sta_cong_timer_ms = 0;

		ath11k_wmi_send_twt_enable_cmd(ar, ar->pdev->pdev_id, &twt_params);
	}

	params.vdev_id = arvif->vdev_id;

	ret = ath11k_wmi_send_twt_add_dialog_cmd(arvif->ar, &params);
	if (ret)
		goto err_twt_add_dialog;

	return count;

err_twt_add_dialog:
	if (arvif->vif->type == NL80211_IFTYPE_STATION) {
		ath11k_wmi_send_twt_disable_cmd(ar, ar->pdev->pdev_id);
		ath11k_wmi_fill_default_twt_params(&twt_params);
		ath11k_wmi_send_twt_enable_cmd(ar, ar->pdev->pdev_id, &twt_params);
	}

	return ret;
}

static ssize_t ath11k_write_twt_del_dialog(struct file *file,
					   const char __user *ubuf,
					   size_t count, loff_t *ppos)
{
	struct ath11k_vif *arvif = file->private_data;
	struct wmi_twt_del_dialog_params params = { 0 };
	struct wmi_twt_enable_params twt_params = {0};
	struct ath11k *ar = arvif->ar;
	u8 buf[64] = {0};
	int ret;

	if (ar->twt_enabled == 0) {
		ath11k_err(ar->ab, "twt support is not enabled\n");
		return -EOPNOTSUPP;
	}

	ret = simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, ubuf, count);
	if (ret < 0)
		return ret;

	buf[ret] = '\0';
	ret = sscanf(buf, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx %u",
		     &params.peer_macaddr[0],
		     &params.peer_macaddr[1],
		     &params.peer_macaddr[2],
		     &params.peer_macaddr[3],
		     &params.peer_macaddr[4],
		     &params.peer_macaddr[5],
		     &params.dialog_id);
	if (ret != 7)
		return -EINVAL;

	params.vdev_id = arvif->vdev_id;

	ret = ath11k_wmi_send_twt_del_dialog_cmd(arvif->ar, &params);
	if (ret)
		return ret;

	if (arvif->vif->type == NL80211_IFTYPE_STATION) {
		ath11k_wmi_send_twt_disable_cmd(ar, ar->pdev->pdev_id);
		ath11k_wmi_fill_default_twt_params(&twt_params);
		ath11k_wmi_send_twt_enable_cmd(ar, ar->pdev->pdev_id, &twt_params);
	}

	return count;
}

static ssize_t ath11k_write_twt_pause_dialog(struct file *file,
					     const char __user *ubuf,
					     size_t count, loff_t *ppos)
{
	struct ath11k_vif *arvif = file->private_data;
	struct wmi_twt_pause_dialog_params params = { 0 };
	u8 buf[64] = {0};
	int ret;

	if (arvif->ar->twt_enabled == 0) {
		ath11k_err(arvif->ar->ab, "twt support is not enabled\n");
		return -EOPNOTSUPP;
	}

	ret = simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, ubuf, count);
	if (ret < 0)
		return ret;

	buf[ret] = '\0';
	ret = sscanf(buf, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx %u",
		     &params.peer_macaddr[0],
		     &params.peer_macaddr[1],
		     &params.peer_macaddr[2],
		     &params.peer_macaddr[3],
		     &params.peer_macaddr[4],
		     &params.peer_macaddr[5],
		     &params.dialog_id);
	if (ret != 7)
		return -EINVAL;

	params.vdev_id = arvif->vdev_id;

	ret = ath11k_wmi_send_twt_pause_dialog_cmd(arvif->ar, &params);
	if (ret)
		return ret;

	return count;
}

static ssize_t ath11k_write_twt_resume_dialog(struct file *file,
					      const char __user *ubuf,
					      size_t count, loff_t *ppos)
{
	struct ath11k_vif *arvif = file->private_data;
	struct wmi_twt_resume_dialog_params params = { 0 };
	u8 buf[64] = {0};
	int ret;

	if (arvif->ar->twt_enabled == 0) {
		ath11k_err(arvif->ar->ab, "twt support is not enabled\n");
		return -EOPNOTSUPP;
	}

	ret = simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, ubuf, count);
	if (ret < 0)
		return ret;

	buf[ret] = '\0';
	ret = sscanf(buf, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx %u %u %u",
		     &params.peer_macaddr[0],
		     &params.peer_macaddr[1],
		     &params.peer_macaddr[2],
		     &params.peer_macaddr[3],
		     &params.peer_macaddr[4],
		     &params.peer_macaddr[5],
		     &params.dialog_id,
		     &params.sp_offset_us,
		     &params.next_twt_size);
	if (ret != 9)
		return -EINVAL;

	params.vdev_id = arvif->vdev_id;

	ret = ath11k_wmi_send_twt_resume_dialog_cmd(arvif->ar, &params);
	if (ret)
		return ret;

	return count;
}

static const struct file_operations ath11k_fops_twt_add_dialog = {
	.write = ath11k_write_twt_add_dialog,
	.open = simple_open
};

static const struct file_operations ath11k_fops_twt_del_dialog = {
	.write = ath11k_write_twt_del_dialog,
	.open = simple_open
};

static const struct file_operations ath11k_fops_twt_pause_dialog = {
	.write = ath11k_write_twt_pause_dialog,
	.open = simple_open
};

static const struct file_operations ath11k_fops_twt_resume_dialog = {
	.write = ath11k_write_twt_resume_dialog,
	.open = simple_open
};

void ath11k_debugfs_add_interface(struct ath11k_vif *arvif)
{
	struct ath11k_base *ab = arvif->ar->ab;

	if (arvif->vif->type != NL80211_IFTYPE_AP &&
	    !(arvif->vif->type == NL80211_IFTYPE_STATION &&
	      test_bit(WMI_TLV_SERVICE_STA_TWT, ab->wmi_ab.svc_map)))
		return;

	arvif->debugfs_twt = debugfs_create_dir("twt",
						arvif->vif->debugfs_dir);
	debugfs_create_file("add_dialog", 0200, arvif->debugfs_twt,
			    arvif, &ath11k_fops_twt_add_dialog);

	debugfs_create_file("del_dialog", 0200, arvif->debugfs_twt,
			    arvif, &ath11k_fops_twt_del_dialog);

	debugfs_create_file("pause_dialog", 0200, arvif->debugfs_twt,
			    arvif, &ath11k_fops_twt_pause_dialog);

	debugfs_create_file("resume_dialog", 0200, arvif->debugfs_twt,
			    arvif, &ath11k_fops_twt_resume_dialog);
}

void ath11k_debugfs_remove_interface(struct ath11k_vif *arvif)
{
	if (!arvif->debugfs_twt)
		return;

	debugfs_remove_recursive(arvif->debugfs_twt);
	arvif->debugfs_twt = NULL;
}
