/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
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

#include <linux/module.h>
#include <linux/debugfs.h>

#include "core.h"
#include "debug.h"

/* ms */
#define ATH10K_DEBUG_HTT_STATS_INTERVAL 1000

static int ath10k_printk(const char *level, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;
	int rtn;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	rtn = printk("%sath10k: %pV", level, &vaf);

	va_end(args);

	return rtn;
}

int ath10k_info(const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;
	int ret;

	va_start(args, fmt);
	vaf.va = &args;
	ret = ath10k_printk(KERN_INFO, "%pV", &vaf);
	trace_ath10k_log_info(&vaf);
	va_end(args);

	return ret;
}
EXPORT_SYMBOL(ath10k_info);

int ath10k_err(const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;
	int ret;

	va_start(args, fmt);
	vaf.va = &args;
	ret = ath10k_printk(KERN_ERR, "%pV", &vaf);
	trace_ath10k_log_err(&vaf);
	va_end(args);

	return ret;
}
EXPORT_SYMBOL(ath10k_err);

int ath10k_warn(const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;
	int ret = 0;

	va_start(args, fmt);
	vaf.va = &args;

	if (net_ratelimit())
		ret = ath10k_printk(KERN_WARNING, "%pV", &vaf);

	trace_ath10k_log_warn(&vaf);

	va_end(args);

	return ret;
}
EXPORT_SYMBOL(ath10k_warn);

#ifdef CONFIG_ATH10K_DEBUGFS

void ath10k_debug_read_service_map(struct ath10k *ar,
				   void *service_map,
				   size_t map_size)
{
	memcpy(ar->debug.wmi_service_bitmap, service_map, map_size);
}

static ssize_t ath10k_read_wmi_services(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	char *buf;
	unsigned int len = 0, buf_len = 1500;
	const char *status;
	ssize_t ret_cnt;
	int i;

	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&ar->conf_mutex);

	if (len > buf_len)
		len = buf_len;

	for (i = 0; i < WMI_SERVICE_LAST; i++) {
		if (WMI_SERVICE_IS_ENABLED(ar->debug.wmi_service_bitmap, i))
			status = "enabled";
		else
			status = "disabled";

		len += scnprintf(buf + len, buf_len - len,
				 "0x%02x - %20s - %s\n",
				 i, wmi_service_name(i), status);
	}

	ret_cnt = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	mutex_unlock(&ar->conf_mutex);

	kfree(buf);
	return ret_cnt;
}

static const struct file_operations fops_wmi_services = {
	.read = ath10k_read_wmi_services,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

void ath10k_debug_read_target_stats(struct ath10k *ar,
				    struct wmi_stats_event *ev)
{
	u8 *tmp = ev->data;
	struct ath10k_target_stats *stats;
	int num_pdev_stats, num_vdev_stats, num_peer_stats;
	struct wmi_pdev_stats *ps;
	int i;

	spin_lock_bh(&ar->data_lock);

	stats = &ar->debug.target_stats;

	num_pdev_stats = __le32_to_cpu(ev->num_pdev_stats); /* 0 or 1 */
	num_vdev_stats = __le32_to_cpu(ev->num_vdev_stats); /* 0 or max vdevs */
	num_peer_stats = __le32_to_cpu(ev->num_peer_stats); /* 0 or max peers */

	if (num_pdev_stats) {
		ps = (struct wmi_pdev_stats *)tmp;

		stats->ch_noise_floor = __le32_to_cpu(ps->chan_nf);
		stats->tx_frame_count = __le32_to_cpu(ps->tx_frame_count);
		stats->rx_frame_count = __le32_to_cpu(ps->rx_frame_count);
		stats->rx_clear_count = __le32_to_cpu(ps->rx_clear_count);
		stats->cycle_count = __le32_to_cpu(ps->cycle_count);
		stats->phy_err_count = __le32_to_cpu(ps->phy_err_count);
		stats->chan_tx_power = __le32_to_cpu(ps->chan_tx_pwr);

		stats->comp_queued = __le32_to_cpu(ps->wal.tx.comp_queued);
		stats->comp_delivered =
			__le32_to_cpu(ps->wal.tx.comp_delivered);
		stats->msdu_enqued = __le32_to_cpu(ps->wal.tx.msdu_enqued);
		stats->mpdu_enqued = __le32_to_cpu(ps->wal.tx.mpdu_enqued);
		stats->wmm_drop = __le32_to_cpu(ps->wal.tx.wmm_drop);
		stats->local_enqued = __le32_to_cpu(ps->wal.tx.local_enqued);
		stats->local_freed = __le32_to_cpu(ps->wal.tx.local_freed);
		stats->hw_queued = __le32_to_cpu(ps->wal.tx.hw_queued);
		stats->hw_reaped = __le32_to_cpu(ps->wal.tx.hw_reaped);
		stats->underrun = __le32_to_cpu(ps->wal.tx.underrun);
		stats->tx_abort = __le32_to_cpu(ps->wal.tx.tx_abort);
		stats->mpdus_requed = __le32_to_cpu(ps->wal.tx.mpdus_requed);
		stats->tx_ko = __le32_to_cpu(ps->wal.tx.tx_ko);
		stats->data_rc = __le32_to_cpu(ps->wal.tx.data_rc);
		stats->self_triggers = __le32_to_cpu(ps->wal.tx.self_triggers);
		stats->sw_retry_failure =
			__le32_to_cpu(ps->wal.tx.sw_retry_failure);
		stats->illgl_rate_phy_err =
			__le32_to_cpu(ps->wal.tx.illgl_rate_phy_err);
		stats->pdev_cont_xretry =
			__le32_to_cpu(ps->wal.tx.pdev_cont_xretry);
		stats->pdev_tx_timeout =
			__le32_to_cpu(ps->wal.tx.pdev_tx_timeout);
		stats->pdev_resets = __le32_to_cpu(ps->wal.tx.pdev_resets);
		stats->phy_underrun = __le32_to_cpu(ps->wal.tx.phy_underrun);
		stats->txop_ovf = __le32_to_cpu(ps->wal.tx.txop_ovf);

		stats->mid_ppdu_route_change =
			__le32_to_cpu(ps->wal.rx.mid_ppdu_route_change);
		stats->status_rcvd = __le32_to_cpu(ps->wal.rx.status_rcvd);
		stats->r0_frags = __le32_to_cpu(ps->wal.rx.r0_frags);
		stats->r1_frags = __le32_to_cpu(ps->wal.rx.r1_frags);
		stats->r2_frags = __le32_to_cpu(ps->wal.rx.r2_frags);
		stats->r3_frags = __le32_to_cpu(ps->wal.rx.r3_frags);
		stats->htt_msdus = __le32_to_cpu(ps->wal.rx.htt_msdus);
		stats->htt_mpdus = __le32_to_cpu(ps->wal.rx.htt_mpdus);
		stats->loc_msdus = __le32_to_cpu(ps->wal.rx.loc_msdus);
		stats->loc_mpdus = __le32_to_cpu(ps->wal.rx.loc_mpdus);
		stats->oversize_amsdu =
			__le32_to_cpu(ps->wal.rx.oversize_amsdu);
		stats->phy_errs = __le32_to_cpu(ps->wal.rx.phy_errs);
		stats->phy_err_drop = __le32_to_cpu(ps->wal.rx.phy_err_drop);
		stats->mpdu_errs = __le32_to_cpu(ps->wal.rx.mpdu_errs);

		tmp += sizeof(struct wmi_pdev_stats);
	}

	/* 0 or max vdevs */
	/* Currently firmware does not support VDEV stats */
	if (num_vdev_stats) {
		struct wmi_vdev_stats *vdev_stats;

		for (i = 0; i < num_vdev_stats; i++) {
			vdev_stats = (struct wmi_vdev_stats *)tmp;
			tmp += sizeof(struct wmi_vdev_stats);
		}
	}

	if (num_peer_stats) {
		struct wmi_peer_stats *peer_stats;
		struct ath10k_peer_stat *s;

		stats->peers = num_peer_stats;

		for (i = 0; i < num_peer_stats; i++) {
			peer_stats = (struct wmi_peer_stats *)tmp;
			s = &stats->peer_stat[i];

			WMI_MAC_ADDR_TO_CHAR_ARRAY(&peer_stats->peer_macaddr,
						   s->peer_macaddr);
			s->peer_rssi = __le32_to_cpu(peer_stats->peer_rssi);
			s->peer_tx_rate =
				__le32_to_cpu(peer_stats->peer_tx_rate);

			tmp += sizeof(struct wmi_peer_stats);
		}
	}

	spin_unlock_bh(&ar->data_lock);
	complete(&ar->debug.event_stats_compl);
}

static ssize_t ath10k_read_fw_stats(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	struct ath10k_target_stats *fw_stats;
	char *buf = NULL;
	unsigned int len = 0, buf_len = 2500;
	ssize_t ret_cnt = 0;
	long left;
	int i;
	int ret;

	fw_stats = &ar->debug.target_stats;

	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH10K_STATE_ON)
		goto exit;

	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf)
		goto exit;

	ret = ath10k_wmi_request_stats(ar, WMI_REQUEST_PEER_STAT);
	if (ret) {
		ath10k_warn("could not request stats (%d)\n", ret);
		goto exit;
	}

	left = wait_for_completion_timeout(&ar->debug.event_stats_compl, 1*HZ);
	if (left <= 0)
		goto exit;

	spin_lock_bh(&ar->data_lock);
	len += scnprintf(buf + len, buf_len - len, "\n");
	len += scnprintf(buf + len, buf_len - len, "%30s\n",
			 "ath10k PDEV stats");
	len += scnprintf(buf + len, buf_len - len, "%30s\n\n",
				 "=================");

	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Channel noise floor", fw_stats->ch_noise_floor);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "Channel TX power", fw_stats->chan_tx_power);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "TX frame count", fw_stats->tx_frame_count);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "RX frame count", fw_stats->rx_frame_count);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "RX clear count", fw_stats->rx_clear_count);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "Cycle count", fw_stats->cycle_count);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "PHY error count", fw_stats->phy_err_count);

	len += scnprintf(buf + len, buf_len - len, "\n");
	len += scnprintf(buf + len, buf_len - len, "%30s\n",
			 "ath10k PDEV TX stats");
	len += scnprintf(buf + len, buf_len - len, "%30s\n\n",
				 "=================");

	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "HTT cookies queued", fw_stats->comp_queued);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "HTT cookies disp.", fw_stats->comp_delivered);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MSDU queued", fw_stats->msdu_enqued);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDU queued", fw_stats->mpdu_enqued);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MSDUs dropped", fw_stats->wmm_drop);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Local enqued", fw_stats->local_enqued);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Local freed", fw_stats->local_freed);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "HW queued", fw_stats->hw_queued);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "PPDUs reaped", fw_stats->hw_reaped);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Num underruns", fw_stats->underrun);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "PPDUs cleaned", fw_stats->tx_abort);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDUs requed", fw_stats->mpdus_requed);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Excessive retries", fw_stats->tx_ko);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "HW rate", fw_stats->data_rc);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Sched self tiggers", fw_stats->self_triggers);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Dropped due to SW retries",
			 fw_stats->sw_retry_failure);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Illegal rate phy errors",
			 fw_stats->illgl_rate_phy_err);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Pdev continous xretry", fw_stats->pdev_cont_xretry);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "TX timeout", fw_stats->pdev_tx_timeout);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "PDEV resets", fw_stats->pdev_resets);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "PHY underrun", fw_stats->phy_underrun);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDU is more than txop limit", fw_stats->txop_ovf);

	len += scnprintf(buf + len, buf_len - len, "\n");
	len += scnprintf(buf + len, buf_len - len, "%30s\n",
			 "ath10k PDEV RX stats");
	len += scnprintf(buf + len, buf_len - len, "%30s\n\n",
				 "=================");

	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Mid PPDU route change",
			 fw_stats->mid_ppdu_route_change);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Tot. number of statuses", fw_stats->status_rcvd);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Extra frags on rings 0", fw_stats->r0_frags);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Extra frags on rings 1", fw_stats->r1_frags);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Extra frags on rings 2", fw_stats->r2_frags);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Extra frags on rings 3", fw_stats->r3_frags);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MSDUs delivered to HTT", fw_stats->htt_msdus);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDUs delivered to HTT", fw_stats->htt_mpdus);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MSDUs delivered to stack", fw_stats->loc_msdus);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDUs delivered to stack", fw_stats->loc_mpdus);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Oversized AMSUs", fw_stats->oversize_amsdu);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "PHY errors", fw_stats->phy_errs);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "PHY errors drops", fw_stats->phy_err_drop);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDU errors (FCS, MIC, ENC)", fw_stats->mpdu_errs);

	len += scnprintf(buf + len, buf_len - len, "\n");
	len += scnprintf(buf + len, buf_len - len, "%30s\n",
			 "ath10k PEER stats");
	len += scnprintf(buf + len, buf_len - len, "%30s\n\n",
				 "=================");

	for (i = 0; i < fw_stats->peers; i++) {
		len += scnprintf(buf + len, buf_len - len, "%30s %pM\n",
				 "Peer MAC address",
				 fw_stats->peer_stat[i].peer_macaddr);
		len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
				 "Peer RSSI", fw_stats->peer_stat[i].peer_rssi);
		len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
				 "Peer TX rate",
				 fw_stats->peer_stat[i].peer_tx_rate);
		len += scnprintf(buf + len, buf_len - len, "\n");
	}
	spin_unlock_bh(&ar->data_lock);

	if (len > buf_len)
		len = buf_len;

	ret_cnt = simple_read_from_buffer(user_buf, count, ppos, buf, len);

exit:
	mutex_unlock(&ar->conf_mutex);
	kfree(buf);
	return ret_cnt;
}

static const struct file_operations fops_fw_stats = {
	.read = ath10k_read_fw_stats,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath10k_read_simulate_fw_crash(struct file *file,
					     char __user *user_buf,
					     size_t count, loff_t *ppos)
{
	const char buf[] = "To simulate firmware crash write the keyword"
			   " `crash` to this file.\nThis will force firmware"
			   " to report a crash to the host system.\n";
	return simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
}

static ssize_t ath10k_write_simulate_fw_crash(struct file *file,
					      const char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	char buf[32] = {};
	int ret;

	mutex_lock(&ar->conf_mutex);

	simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, user_buf, count);
	if (strcmp(buf, "crash") && strcmp(buf, "crash\n")) {
		ret = -EINVAL;
		goto exit;
	}

	if (ar->state != ATH10K_STATE_ON &&
	    ar->state != ATH10K_STATE_RESTARTED) {
		ret = -ENETDOWN;
		goto exit;
	}

	ath10k_info("simulating firmware crash\n");

	ret = ath10k_wmi_force_fw_hang(ar, WMI_FORCE_FW_HANG_ASSERT, 0);
	if (ret)
		ath10k_warn("failed to force fw hang (%d)\n", ret);

	if (ret == 0)
		ret = count;

exit:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static const struct file_operations fops_simulate_fw_crash = {
	.read = ath10k_read_simulate_fw_crash,
	.write = ath10k_write_simulate_fw_crash,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath10k_read_chip_id(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	unsigned int len;
	char buf[50];

	len = scnprintf(buf, sizeof(buf), "0x%08x\n", ar->chip_id);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations fops_chip_id = {
	.read = ath10k_read_chip_id,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int ath10k_debug_htt_stats_req(struct ath10k *ar)
{
	u64 cookie;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	if (ar->debug.htt_stats_mask == 0)
		/* htt stats are disabled */
		return 0;

	if (ar->state != ATH10K_STATE_ON)
		return 0;

	cookie = get_jiffies_64();

	ret = ath10k_htt_h2t_stats_req(&ar->htt, ar->debug.htt_stats_mask,
				       cookie);
	if (ret) {
		ath10k_warn("failed to send htt stats request: %d\n", ret);
		return ret;
	}

	queue_delayed_work(ar->workqueue, &ar->debug.htt_stats_dwork,
			   msecs_to_jiffies(ATH10K_DEBUG_HTT_STATS_INTERVAL));

	return 0;
}

static void ath10k_debug_htt_stats_dwork(struct work_struct *work)
{
	struct ath10k *ar = container_of(work, struct ath10k,
					 debug.htt_stats_dwork.work);

	mutex_lock(&ar->conf_mutex);

	ath10k_debug_htt_stats_req(ar);

	mutex_unlock(&ar->conf_mutex);
}

static ssize_t ath10k_read_htt_stats_mask(struct file *file,
					    char __user *user_buf,
					    size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	char buf[32];
	unsigned int len;

	len = scnprintf(buf, sizeof(buf), "%lu\n", ar->debug.htt_stats_mask);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath10k_write_htt_stats_mask(struct file *file,
					     const char __user *user_buf,
					     size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	unsigned long mask;
	int ret;

	ret = kstrtoul_from_user(user_buf, count, 0, &mask);
	if (ret)
		return ret;

	/* max 8 bit masks (for now) */
	if (mask > 0xff)
		return -E2BIG;

	mutex_lock(&ar->conf_mutex);

	ar->debug.htt_stats_mask = mask;

	ret = ath10k_debug_htt_stats_req(ar);
	if (ret)
		goto out;

	ret = count;

out:
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static const struct file_operations fops_htt_stats_mask = {
	.read = ath10k_read_htt_stats_mask,
	.write = ath10k_write_htt_stats_mask,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath10k_read_fw_dbglog(struct file *file,
					    char __user *user_buf,
					    size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	unsigned int len;
	char buf[32];

	len = scnprintf(buf, sizeof(buf), "0x%08x\n",
			ar->debug.fw_dbglog_mask);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath10k_write_fw_dbglog(struct file *file,
				      const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	unsigned long mask;
	int ret;

	ret = kstrtoul_from_user(user_buf, count, 0, &mask);
	if (ret)
		return ret;

	mutex_lock(&ar->conf_mutex);

	ar->debug.fw_dbglog_mask = mask;

	if (ar->state == ATH10K_STATE_ON) {
		ret = ath10k_wmi_dbglog_cfg(ar, ar->debug.fw_dbglog_mask);
		if (ret) {
			ath10k_warn("dbglog cfg failed from debugfs: %d\n",
				    ret);
			goto exit;
		}
	}

	ret = count;

exit:
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static const struct file_operations fops_fw_dbglog = {
	.read = ath10k_read_fw_dbglog,
	.write = ath10k_write_fw_dbglog,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

int ath10k_debug_start(struct ath10k *ar)
{
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	ret = ath10k_debug_htt_stats_req(ar);
	if (ret)
		/* continue normally anyway, this isn't serious */
		ath10k_warn("failed to start htt stats workqueue: %d\n", ret);

	if (ar->debug.fw_dbglog_mask) {
		ret = ath10k_wmi_dbglog_cfg(ar, ar->debug.fw_dbglog_mask);
		if (ret)
			/* not serious */
			ath10k_warn("failed to enable dbglog during start: %d",
				    ret);
	}

	return 0;
}

void ath10k_debug_stop(struct ath10k *ar)
{
	lockdep_assert_held(&ar->conf_mutex);

	/* Must not use _sync to avoid deadlock, we do that in
	 * ath10k_debug_destroy(). The check for htt_stats_mask is to avoid
	 * warning from del_timer(). */
	if (ar->debug.htt_stats_mask != 0)
		cancel_delayed_work(&ar->debug.htt_stats_dwork);
}

static ssize_t ath10k_write_simulate_radar(struct file *file,
					   const char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;

	ieee80211_radar_detected(ar->hw);

	return count;
}

static const struct file_operations fops_simulate_radar = {
	.write = ath10k_write_simulate_radar,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

#define ATH10K_DFS_STAT(s, p) (\
	len += scnprintf(buf + len, size - len, "%-28s : %10u\n", s, \
			 ar->debug.dfs_stats.p))

#define ATH10K_DFS_POOL_STAT(s, p) (\
	len += scnprintf(buf + len, size - len, "%-28s : %10u\n", s, \
			 ar->debug.dfs_pool_stats.p))

static ssize_t ath10k_read_dfs_stats(struct file *file, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	int retval = 0, len = 0;
	const int size = 8000;
	struct ath10k *ar = file->private_data;
	char *buf;

	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (!ar->dfs_detector) {
		len += scnprintf(buf + len, size - len, "DFS not enabled\n");
		goto exit;
	}

	ar->debug.dfs_pool_stats =
			ar->dfs_detector->get_stats(ar->dfs_detector);

	len += scnprintf(buf + len, size - len, "Pulse detector statistics:\n");

	ATH10K_DFS_STAT("reported phy errors", phy_errors);
	ATH10K_DFS_STAT("pulse events reported", pulses_total);
	ATH10K_DFS_STAT("DFS pulses detected", pulses_detected);
	ATH10K_DFS_STAT("DFS pulses discarded", pulses_discarded);
	ATH10K_DFS_STAT("Radars detected", radar_detected);

	len += scnprintf(buf + len, size - len, "Global Pool statistics:\n");
	ATH10K_DFS_POOL_STAT("Pool references", pool_reference);
	ATH10K_DFS_POOL_STAT("Pulses allocated", pulse_allocated);
	ATH10K_DFS_POOL_STAT("Pulses alloc error", pulse_alloc_error);
	ATH10K_DFS_POOL_STAT("Pulses in use", pulse_used);
	ATH10K_DFS_POOL_STAT("Seqs. allocated", pseq_allocated);
	ATH10K_DFS_POOL_STAT("Seqs. alloc error", pseq_alloc_error);
	ATH10K_DFS_POOL_STAT("Seqs. in use", pseq_used);

exit:
	if (len > size)
		len = size;

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

static const struct file_operations fops_dfs_stats = {
	.read = ath10k_read_dfs_stats,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

int ath10k_debug_create(struct ath10k *ar)
{
	ar->debug.debugfs_phy = debugfs_create_dir("ath10k",
						   ar->hw->wiphy->debugfsdir);

	if (!ar->debug.debugfs_phy)
		return -ENOMEM;

	INIT_DELAYED_WORK(&ar->debug.htt_stats_dwork,
			  ath10k_debug_htt_stats_dwork);

	init_completion(&ar->debug.event_stats_compl);

	debugfs_create_file("fw_stats", S_IRUSR, ar->debug.debugfs_phy, ar,
			    &fops_fw_stats);

	debugfs_create_file("wmi_services", S_IRUSR, ar->debug.debugfs_phy, ar,
			    &fops_wmi_services);

	debugfs_create_file("simulate_fw_crash", S_IRUSR, ar->debug.debugfs_phy,
			    ar, &fops_simulate_fw_crash);

	debugfs_create_file("chip_id", S_IRUSR, ar->debug.debugfs_phy,
			    ar, &fops_chip_id);

	debugfs_create_file("htt_stats_mask", S_IRUSR, ar->debug.debugfs_phy,
			    ar, &fops_htt_stats_mask);

	debugfs_create_file("fw_dbglog", S_IRUSR, ar->debug.debugfs_phy,
			    ar, &fops_fw_dbglog);

	if (config_enabled(CONFIG_ATH10K_DFS_CERTIFIED)) {
		debugfs_create_file("dfs_simulate_radar", S_IWUSR,
				    ar->debug.debugfs_phy, ar,
				    &fops_simulate_radar);

		debugfs_create_bool("dfs_block_radar_events", S_IWUSR,
				    ar->debug.debugfs_phy,
				    &ar->dfs_block_radar_events);

		debugfs_create_file("dfs_stats", S_IRUSR,
				    ar->debug.debugfs_phy, ar,
				    &fops_dfs_stats);
	}

	return 0;
}

void ath10k_debug_destroy(struct ath10k *ar)
{
	cancel_delayed_work_sync(&ar->debug.htt_stats_dwork);
}

#endif /* CONFIG_ATH10K_DEBUGFS */

#ifdef CONFIG_ATH10K_DEBUG
void ath10k_dbg(enum ath10k_debug_mask mask, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	if (ath10k_debug_mask & mask)
		ath10k_printk(KERN_DEBUG, "%pV", &vaf);

	trace_ath10k_log_dbg(mask, &vaf);

	va_end(args);
}
EXPORT_SYMBOL(ath10k_dbg);

void ath10k_dbg_dump(enum ath10k_debug_mask mask,
		     const char *msg, const char *prefix,
		     const void *buf, size_t len)
{
	if (ath10k_debug_mask & mask) {
		if (msg)
			ath10k_dbg(mask, "%s\n", msg);

		print_hex_dump_bytes(prefix, DUMP_PREFIX_OFFSET, buf, len);
	}

	/* tracing code doesn't like null strings :/ */
	trace_ath10k_log_dbg_dump(msg ? msg : "", prefix ? prefix : "",
				  buf, len);
}
EXPORT_SYMBOL(ath10k_dbg_dump);

#endif /* CONFIG_ATH10K_DEBUG */
