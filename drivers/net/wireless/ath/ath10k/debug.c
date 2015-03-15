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
#include <linux/vmalloc.h>
#include <linux/utsname.h>

#include "core.h"
#include "debug.h"
#include "hif.h"
#include "wmi-ops.h"

/* ms */
#define ATH10K_DEBUG_HTT_STATS_INTERVAL 1000

#define ATH10K_FW_CRASH_DUMP_VERSION 1

/**
 * enum ath10k_fw_crash_dump_type - types of data in the dump file
 * @ATH10K_FW_CRASH_DUMP_REGDUMP: Register crash dump in binary format
 */
enum ath10k_fw_crash_dump_type {
	ATH10K_FW_CRASH_DUMP_REGISTERS = 0,

	ATH10K_FW_CRASH_DUMP_MAX,
};

struct ath10k_tlv_dump_data {
	/* see ath10k_fw_crash_dump_type above */
	__le32 type;

	/* in bytes */
	__le32 tlv_len;

	/* pad to 32-bit boundaries as needed */
	u8 tlv_data[];
} __packed;

struct ath10k_dump_file_data {
	/* dump file information */

	/* "ATH10K-FW-DUMP" */
	char df_magic[16];

	__le32 len;

	/* file dump version */
	__le32 version;

	/* some info we can get from ath10k struct that might help */

	u8 uuid[16];

	__le32 chip_id;

	/* 0 for now, in place for later hardware */
	__le32 bus_type;

	__le32 target_version;
	__le32 fw_version_major;
	__le32 fw_version_minor;
	__le32 fw_version_release;
	__le32 fw_version_build;
	__le32 phy_capability;
	__le32 hw_min_tx_power;
	__le32 hw_max_tx_power;
	__le32 ht_cap_info;
	__le32 vht_cap_info;
	__le32 num_rf_chains;

	/* firmware version string */
	char fw_ver[ETHTOOL_FWVERS_LEN];

	/* Kernel related information */

	/* time-of-day stamp */
	__le64 tv_sec;

	/* time-of-day stamp, nano-seconds */
	__le64 tv_nsec;

	/* LINUX_VERSION_CODE */
	__le32 kernel_ver_code;

	/* VERMAGIC_STRING */
	char kernel_ver[64];

	/* room for growth w/out changing binary format */
	u8 unused[128];

	/* struct ath10k_tlv_dump_data + more */
	u8 data[0];
} __packed;

void ath10k_info(struct ath10k *ar, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	dev_info(ar->dev, "%pV", &vaf);
	trace_ath10k_log_info(ar, &vaf);
	va_end(args);
}
EXPORT_SYMBOL(ath10k_info);

void ath10k_print_driver_info(struct ath10k *ar)
{
	ath10k_info(ar, "%s (0x%08x, 0x%08x) fw %s api %d htt %d.%d wmi %d cal %s max_sta %d\n",
		    ar->hw_params.name,
		    ar->target_version,
		    ar->chip_id,
		    ar->hw->wiphy->fw_version,
		    ar->fw_api,
		    ar->htt.target_version_major,
		    ar->htt.target_version_minor,
		    ar->wmi.op_version,
		    ath10k_cal_mode_str(ar->cal_mode),
		    ar->max_num_stations);
	ath10k_info(ar, "debug %d debugfs %d tracing %d dfs %d testmode %d\n",
		    config_enabled(CONFIG_ATH10K_DEBUG),
		    config_enabled(CONFIG_ATH10K_DEBUGFS),
		    config_enabled(CONFIG_ATH10K_TRACING),
		    config_enabled(CONFIG_ATH10K_DFS_CERTIFIED),
		    config_enabled(CONFIG_NL80211_TESTMODE));
}
EXPORT_SYMBOL(ath10k_print_driver_info);

void ath10k_err(struct ath10k *ar, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	dev_err(ar->dev, "%pV", &vaf);
	trace_ath10k_log_err(ar, &vaf);
	va_end(args);
}
EXPORT_SYMBOL(ath10k_err);

void ath10k_warn(struct ath10k *ar, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	dev_warn_ratelimited(ar->dev, "%pV", &vaf);
	trace_ath10k_log_warn(ar, &vaf);

	va_end(args);
}
EXPORT_SYMBOL(ath10k_warn);

#ifdef CONFIG_ATH10K_DEBUGFS

static ssize_t ath10k_read_wmi_services(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	char *buf;
	unsigned int len = 0, buf_len = 4096;
	const char *name;
	ssize_t ret_cnt;
	bool enabled;
	int i;

	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&ar->conf_mutex);

	if (len > buf_len)
		len = buf_len;

	spin_lock_bh(&ar->data_lock);
	for (i = 0; i < WMI_SERVICE_MAX; i++) {
		enabled = test_bit(i, ar->wmi.svc_map);
		name = wmi_service_name(i);

		if (!name) {
			if (enabled)
				len += scnprintf(buf + len, buf_len - len,
						 "%-40s %s (bit %d)\n",
						 "unknown", "enabled", i);

			continue;
		}

		len += scnprintf(buf + len, buf_len - len,
				 "%-40s %s\n",
				 name, enabled ? "enabled" : "-");
	}
	spin_unlock_bh(&ar->data_lock);

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

static void ath10k_debug_fw_stats_pdevs_free(struct list_head *head)
{
	struct ath10k_fw_stats_pdev *i, *tmp;

	list_for_each_entry_safe(i, tmp, head, list) {
		list_del(&i->list);
		kfree(i);
	}
}

static void ath10k_debug_fw_stats_vdevs_free(struct list_head *head)
{
	struct ath10k_fw_stats_vdev *i, *tmp;

	list_for_each_entry_safe(i, tmp, head, list) {
		list_del(&i->list);
		kfree(i);
	}
}

static void ath10k_debug_fw_stats_peers_free(struct list_head *head)
{
	struct ath10k_fw_stats_peer *i, *tmp;

	list_for_each_entry_safe(i, tmp, head, list) {
		list_del(&i->list);
		kfree(i);
	}
}

static void ath10k_debug_fw_stats_reset(struct ath10k *ar)
{
	spin_lock_bh(&ar->data_lock);
	ar->debug.fw_stats_done = false;
	ath10k_debug_fw_stats_pdevs_free(&ar->debug.fw_stats.pdevs);
	ath10k_debug_fw_stats_vdevs_free(&ar->debug.fw_stats.vdevs);
	ath10k_debug_fw_stats_peers_free(&ar->debug.fw_stats.peers);
	spin_unlock_bh(&ar->data_lock);
}

static size_t ath10k_debug_fw_stats_num_peers(struct list_head *head)
{
	struct ath10k_fw_stats_peer *i;
	size_t num = 0;

	list_for_each_entry(i, head, list)
		++num;

	return num;
}

static size_t ath10k_debug_fw_stats_num_vdevs(struct list_head *head)
{
	struct ath10k_fw_stats_vdev *i;
	size_t num = 0;

	list_for_each_entry(i, head, list)
		++num;

	return num;
}

void ath10k_debug_fw_stats_process(struct ath10k *ar, struct sk_buff *skb)
{
	struct ath10k_fw_stats stats = {};
	bool is_start, is_started, is_end;
	size_t num_peers;
	size_t num_vdevs;
	int ret;

	INIT_LIST_HEAD(&stats.pdevs);
	INIT_LIST_HEAD(&stats.vdevs);
	INIT_LIST_HEAD(&stats.peers);

	spin_lock_bh(&ar->data_lock);
	ret = ath10k_wmi_pull_fw_stats(ar, skb, &stats);
	if (ret) {
		ath10k_warn(ar, "failed to pull fw stats: %d\n", ret);
		goto unlock;
	}

	/* Stat data may exceed htc-wmi buffer limit. In such case firmware
	 * splits the stats data and delivers it in a ping-pong fashion of
	 * request cmd-update event.
	 *
	 * However there is no explicit end-of-data. Instead start-of-data is
	 * used as an implicit one. This works as follows:
	 *  a) discard stat update events until one with pdev stats is
	 *     delivered - this skips session started at end of (b)
	 *  b) consume stat update events until another one with pdev stats is
	 *     delivered which is treated as end-of-data and is itself discarded
	 */

	if (ar->debug.fw_stats_done) {
		ath10k_warn(ar, "received unsolicited stats update event\n");
		goto free;
	}

	num_peers = ath10k_debug_fw_stats_num_peers(&ar->debug.fw_stats.peers);
	num_vdevs = ath10k_debug_fw_stats_num_vdevs(&ar->debug.fw_stats.vdevs);
	is_start = (list_empty(&ar->debug.fw_stats.pdevs) &&
		    !list_empty(&stats.pdevs));
	is_end = (!list_empty(&ar->debug.fw_stats.pdevs) &&
		  !list_empty(&stats.pdevs));

	if (is_start)
		list_splice_tail_init(&stats.pdevs, &ar->debug.fw_stats.pdevs);

	if (is_end)
		ar->debug.fw_stats_done = true;

	is_started = !list_empty(&ar->debug.fw_stats.pdevs);

	if (is_started && !is_end) {
		if (num_peers >= ATH10K_MAX_NUM_PEER_IDS) {
			/* Although this is unlikely impose a sane limit to
			 * prevent firmware from DoS-ing the host.
			 */
			ath10k_warn(ar, "dropping fw peer stats\n");
			goto free;
		}

		if (num_vdevs >= BITS_PER_LONG) {
			ath10k_warn(ar, "dropping fw vdev stats\n");
			goto free;
		}

		list_splice_tail_init(&stats.peers, &ar->debug.fw_stats.peers);
		list_splice_tail_init(&stats.vdevs, &ar->debug.fw_stats.vdevs);
	}

	complete(&ar->debug.fw_stats_complete);

free:
	/* In some cases lists have been spliced and cleared. Free up
	 * resources if that is not the case.
	 */
	ath10k_debug_fw_stats_pdevs_free(&stats.pdevs);
	ath10k_debug_fw_stats_vdevs_free(&stats.vdevs);
	ath10k_debug_fw_stats_peers_free(&stats.peers);

unlock:
	spin_unlock_bh(&ar->data_lock);
}

static int ath10k_debug_fw_stats_request(struct ath10k *ar)
{
	unsigned long timeout;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	timeout = jiffies + msecs_to_jiffies(1*HZ);

	ath10k_debug_fw_stats_reset(ar);

	for (;;) {
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;

		reinit_completion(&ar->debug.fw_stats_complete);

		ret = ath10k_wmi_request_stats(ar,
					       WMI_STAT_PDEV |
					       WMI_STAT_VDEV |
					       WMI_STAT_PEER);
		if (ret) {
			ath10k_warn(ar, "could not request stats (%d)\n", ret);
			return ret;
		}

		ret = wait_for_completion_timeout(&ar->debug.fw_stats_complete,
						  1*HZ);
		if (ret == 0)
			return -ETIMEDOUT;

		spin_lock_bh(&ar->data_lock);
		if (ar->debug.fw_stats_done) {
			spin_unlock_bh(&ar->data_lock);
			break;
		}
		spin_unlock_bh(&ar->data_lock);
	}

	return 0;
}

/* FIXME: How to calculate the buffer size sanely? */
#define ATH10K_FW_STATS_BUF_SIZE (1024*1024)

static void ath10k_fw_stats_fill(struct ath10k *ar,
				 struct ath10k_fw_stats *fw_stats,
				 char *buf)
{
	unsigned int len = 0;
	unsigned int buf_len = ATH10K_FW_STATS_BUF_SIZE;
	const struct ath10k_fw_stats_pdev *pdev;
	const struct ath10k_fw_stats_vdev *vdev;
	const struct ath10k_fw_stats_peer *peer;
	size_t num_peers;
	size_t num_vdevs;
	int i;

	spin_lock_bh(&ar->data_lock);

	pdev = list_first_entry_or_null(&fw_stats->pdevs,
					struct ath10k_fw_stats_pdev, list);
	if (!pdev) {
		ath10k_warn(ar, "failed to get pdev stats\n");
		goto unlock;
	}

	num_peers = ath10k_debug_fw_stats_num_peers(&fw_stats->peers);
	num_vdevs = ath10k_debug_fw_stats_num_vdevs(&fw_stats->vdevs);

	len += scnprintf(buf + len, buf_len - len, "\n");
	len += scnprintf(buf + len, buf_len - len, "%30s\n",
			 "ath10k PDEV stats");
	len += scnprintf(buf + len, buf_len - len, "%30s\n\n",
				 "=================");

	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Channel noise floor", pdev->ch_noise_floor);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "Channel TX power", pdev->chan_tx_power);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "TX frame count", pdev->tx_frame_count);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "RX frame count", pdev->rx_frame_count);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "RX clear count", pdev->rx_clear_count);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "Cycle count", pdev->cycle_count);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "PHY error count", pdev->phy_err_count);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "RTS bad count", pdev->rts_bad);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "RTS good count", pdev->rts_good);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "FCS bad count", pdev->fcs_bad);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "No beacon count", pdev->no_beacons);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "MIB int count", pdev->mib_int_count);

	len += scnprintf(buf + len, buf_len - len, "\n");
	len += scnprintf(buf + len, buf_len - len, "%30s\n",
			 "ath10k PDEV TX stats");
	len += scnprintf(buf + len, buf_len - len, "%30s\n\n",
				 "=================");

	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "HTT cookies queued", pdev->comp_queued);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "HTT cookies disp.", pdev->comp_delivered);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MSDU queued", pdev->msdu_enqued);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDU queued", pdev->mpdu_enqued);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MSDUs dropped", pdev->wmm_drop);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Local enqued", pdev->local_enqued);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Local freed", pdev->local_freed);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "HW queued", pdev->hw_queued);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "PPDUs reaped", pdev->hw_reaped);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Num underruns", pdev->underrun);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "PPDUs cleaned", pdev->tx_abort);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDUs requed", pdev->mpdus_requed);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Excessive retries", pdev->tx_ko);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "HW rate", pdev->data_rc);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Sched self tiggers", pdev->self_triggers);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Dropped due to SW retries",
			 pdev->sw_retry_failure);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Illegal rate phy errors",
			 pdev->illgl_rate_phy_err);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Pdev continous xretry", pdev->pdev_cont_xretry);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "TX timeout", pdev->pdev_tx_timeout);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "PDEV resets", pdev->pdev_resets);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "PHY underrun", pdev->phy_underrun);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDU is more than txop limit", pdev->txop_ovf);

	len += scnprintf(buf + len, buf_len - len, "\n");
	len += scnprintf(buf + len, buf_len - len, "%30s\n",
			 "ath10k PDEV RX stats");
	len += scnprintf(buf + len, buf_len - len, "%30s\n\n",
				 "=================");

	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Mid PPDU route change",
			 pdev->mid_ppdu_route_change);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Tot. number of statuses", pdev->status_rcvd);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Extra frags on rings 0", pdev->r0_frags);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Extra frags on rings 1", pdev->r1_frags);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Extra frags on rings 2", pdev->r2_frags);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Extra frags on rings 3", pdev->r3_frags);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MSDUs delivered to HTT", pdev->htt_msdus);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDUs delivered to HTT", pdev->htt_mpdus);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MSDUs delivered to stack", pdev->loc_msdus);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDUs delivered to stack", pdev->loc_mpdus);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Oversized AMSUs", pdev->oversize_amsdu);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "PHY errors", pdev->phy_errs);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "PHY errors drops", pdev->phy_err_drop);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDU errors (FCS, MIC, ENC)", pdev->mpdu_errs);

	len += scnprintf(buf + len, buf_len - len, "\n");
	len += scnprintf(buf + len, buf_len - len, "%30s (%zu)\n",
			 "ath10k VDEV stats", num_vdevs);
	len += scnprintf(buf + len, buf_len - len, "%30s\n\n",
				 "=================");

	list_for_each_entry(vdev, &fw_stats->vdevs, list) {
		len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
				 "vdev id", vdev->vdev_id);
		len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
				 "beacon snr", vdev->beacon_snr);
		len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
				 "data snr", vdev->data_snr);
		len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
				 "num rx frames", vdev->num_rx_frames);
		len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
				 "num rts fail", vdev->num_rts_fail);
		len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
				 "num rts success", vdev->num_rts_success);
		len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
				 "num rx err", vdev->num_rx_err);
		len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
				 "num rx discard", vdev->num_rx_discard);
		len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
				 "num tx not acked", vdev->num_tx_not_acked);

		for (i = 0 ; i < ARRAY_SIZE(vdev->num_tx_frames); i++)
			len += scnprintf(buf + len, buf_len - len,
					"%25s [%02d] %u\n",
					 "num tx frames", i,
					 vdev->num_tx_frames[i]);

		for (i = 0 ; i < ARRAY_SIZE(vdev->num_tx_frames_retries); i++)
			len += scnprintf(buf + len, buf_len - len,
					"%25s [%02d] %u\n",
					 "num tx frames retries", i,
					 vdev->num_tx_frames_retries[i]);

		for (i = 0 ; i < ARRAY_SIZE(vdev->num_tx_frames_failures); i++)
			len += scnprintf(buf + len, buf_len - len,
					"%25s [%02d] %u\n",
					 "num tx frames failures", i,
					 vdev->num_tx_frames_failures[i]);

		for (i = 0 ; i < ARRAY_SIZE(vdev->tx_rate_history); i++)
			len += scnprintf(buf + len, buf_len - len,
					"%25s [%02d] 0x%08x\n",
					 "tx rate history", i,
					 vdev->tx_rate_history[i]);

		for (i = 0 ; i < ARRAY_SIZE(vdev->beacon_rssi_history); i++)
			len += scnprintf(buf + len, buf_len - len,
					"%25s [%02d] %u\n",
					 "beacon rssi history", i,
					 vdev->beacon_rssi_history[i]);

		len += scnprintf(buf + len, buf_len - len, "\n");
	}

	len += scnprintf(buf + len, buf_len - len, "\n");
	len += scnprintf(buf + len, buf_len - len, "%30s (%zu)\n",
			 "ath10k PEER stats", num_peers);
	len += scnprintf(buf + len, buf_len - len, "%30s\n\n",
				 "=================");

	list_for_each_entry(peer, &fw_stats->peers, list) {
		len += scnprintf(buf + len, buf_len - len, "%30s %pM\n",
				 "Peer MAC address", peer->peer_macaddr);
		len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
				 "Peer RSSI", peer->peer_rssi);
		len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
				 "Peer TX rate", peer->peer_tx_rate);
		len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
				 "Peer RX rate", peer->peer_rx_rate);
		len += scnprintf(buf + len, buf_len - len, "\n");
	}

unlock:
	spin_unlock_bh(&ar->data_lock);

	if (len >= buf_len)
		buf[len - 1] = 0;
	else
		buf[len] = 0;
}

static int ath10k_fw_stats_open(struct inode *inode, struct file *file)
{
	struct ath10k *ar = inode->i_private;
	void *buf = NULL;
	int ret;

	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH10K_STATE_ON) {
		ret = -ENETDOWN;
		goto err_unlock;
	}

	buf = vmalloc(ATH10K_FW_STATS_BUF_SIZE);
	if (!buf) {
		ret = -ENOMEM;
		goto err_unlock;
	}

	ret = ath10k_debug_fw_stats_request(ar);
	if (ret) {
		ath10k_warn(ar, "failed to request fw stats: %d\n", ret);
		goto err_free;
	}

	ath10k_fw_stats_fill(ar, &ar->debug.fw_stats, buf);
	file->private_data = buf;

	mutex_unlock(&ar->conf_mutex);
	return 0;

err_free:
	vfree(buf);

err_unlock:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static int ath10k_fw_stats_release(struct inode *inode, struct file *file)
{
	vfree(file->private_data);

	return 0;
}

static ssize_t ath10k_fw_stats_read(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	const char *buf = file->private_data;
	unsigned int len = strlen(buf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations fops_fw_stats = {
	.open = ath10k_fw_stats_open,
	.release = ath10k_fw_stats_release,
	.read = ath10k_fw_stats_read,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath10k_debug_fw_reset_stats_read(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	int ret, len, buf_len;
	char *buf;

	buf_len = 500;
	buf = kmalloc(buf_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	spin_lock_bh(&ar->data_lock);

	len = 0;
	len += scnprintf(buf + len, buf_len - len,
			 "fw_crash_counter\t\t%d\n", ar->stats.fw_crash_counter);
	len += scnprintf(buf + len, buf_len - len,
			 "fw_warm_reset_counter\t\t%d\n",
			 ar->stats.fw_warm_reset_counter);
	len += scnprintf(buf + len, buf_len - len,
			 "fw_cold_reset_counter\t\t%d\n",
			 ar->stats.fw_cold_reset_counter);

	spin_unlock_bh(&ar->data_lock);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	kfree(buf);

	return ret;
}

static const struct file_operations fops_fw_reset_stats = {
	.open = simple_open,
	.read = ath10k_debug_fw_reset_stats_read,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

/* This is a clean assert crash in firmware. */
static int ath10k_debug_fw_assert(struct ath10k *ar)
{
	struct wmi_vdev_install_key_cmd *cmd;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd) + 16);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_vdev_install_key_cmd *)skb->data;
	memset(cmd, 0, sizeof(*cmd));

	/* big enough number so that firmware asserts */
	cmd->vdev_id = __cpu_to_le32(0x7ffe);

	return ath10k_wmi_cmd_send(ar, skb,
				   ar->wmi.cmd->vdev_install_key_cmdid);
}

static ssize_t ath10k_read_simulate_fw_crash(struct file *file,
					     char __user *user_buf,
					     size_t count, loff_t *ppos)
{
	const char buf[] =
		"To simulate firmware crash write one of the keywords to this file:\n"
		"`soft` - this will send WMI_FORCE_FW_HANG_ASSERT to firmware if FW supports that command.\n"
		"`hard` - this will send to firmware command with illegal parameters causing firmware crash.\n"
		"`assert` - this will send special illegal parameter to firmware to cause assert failure and crash.\n"
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
static ssize_t ath10k_write_simulate_fw_crash(struct file *file,
					      const char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	char buf[32];
	int ret;

	mutex_lock(&ar->conf_mutex);

	simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, user_buf, count);

	/* make sure that buf is null terminated */
	buf[sizeof(buf) - 1] = 0;

	if (ar->state != ATH10K_STATE_ON &&
	    ar->state != ATH10K_STATE_RESTARTED) {
		ret = -ENETDOWN;
		goto exit;
	}

	/* drop the possible '\n' from the end */
	if (buf[count - 1] == '\n') {
		buf[count - 1] = 0;
		count--;
	}

	if (!strcmp(buf, "soft")) {
		ath10k_info(ar, "simulating soft firmware crash\n");
		ret = ath10k_wmi_force_fw_hang(ar, WMI_FORCE_FW_HANG_ASSERT, 0);
	} else if (!strcmp(buf, "hard")) {
		ath10k_info(ar, "simulating hard firmware crash\n");
		/* 0x7fff is vdev id, and it is always out of range for all
		 * firmware variants in order to force a firmware crash.
		 */
		ret = ath10k_wmi_vdev_set_param(ar, 0x7fff,
						ar->wmi.vdev_param->rts_threshold,
						0);
	} else if (!strcmp(buf, "assert")) {
		ath10k_info(ar, "simulating firmware assert crash\n");
		ret = ath10k_debug_fw_assert(ar);
	} else if (!strcmp(buf, "hw-restart")) {
		ath10k_info(ar, "user requested hw restart\n");
		queue_work(ar->workqueue, &ar->restart_work);
		ret = 0;
	} else {
		ret = -EINVAL;
		goto exit;
	}

	if (ret) {
		ath10k_warn(ar, "failed to simulate firmware crash: %d\n", ret);
		goto exit;
	}

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

struct ath10k_fw_crash_data *
ath10k_debug_get_new_fw_crash_data(struct ath10k *ar)
{
	struct ath10k_fw_crash_data *crash_data = ar->debug.fw_crash_data;

	lockdep_assert_held(&ar->data_lock);

	crash_data->crashed_since_read = true;
	uuid_le_gen(&crash_data->uuid);
	getnstimeofday(&crash_data->timestamp);

	return crash_data;
}
EXPORT_SYMBOL(ath10k_debug_get_new_fw_crash_data);

static struct ath10k_dump_file_data *ath10k_build_dump_file(struct ath10k *ar)
{
	struct ath10k_fw_crash_data *crash_data = ar->debug.fw_crash_data;
	struct ath10k_dump_file_data *dump_data;
	struct ath10k_tlv_dump_data *dump_tlv;
	int hdr_len = sizeof(*dump_data);
	unsigned int len, sofar = 0;
	unsigned char *buf;

	len = hdr_len;
	len += sizeof(*dump_tlv) + sizeof(crash_data->registers);

	sofar += hdr_len;

	/* This is going to get big when we start dumping FW RAM and such,
	 * so go ahead and use vmalloc.
	 */
	buf = vzalloc(len);
	if (!buf)
		return NULL;

	spin_lock_bh(&ar->data_lock);

	if (!crash_data->crashed_since_read) {
		spin_unlock_bh(&ar->data_lock);
		vfree(buf);
		return NULL;
	}

	dump_data = (struct ath10k_dump_file_data *)(buf);
	strlcpy(dump_data->df_magic, "ATH10K-FW-DUMP",
		sizeof(dump_data->df_magic));
	dump_data->len = cpu_to_le32(len);

	dump_data->version = cpu_to_le32(ATH10K_FW_CRASH_DUMP_VERSION);

	memcpy(dump_data->uuid, &crash_data->uuid, sizeof(dump_data->uuid));
	dump_data->chip_id = cpu_to_le32(ar->chip_id);
	dump_data->bus_type = cpu_to_le32(0);
	dump_data->target_version = cpu_to_le32(ar->target_version);
	dump_data->fw_version_major = cpu_to_le32(ar->fw_version_major);
	dump_data->fw_version_minor = cpu_to_le32(ar->fw_version_minor);
	dump_data->fw_version_release = cpu_to_le32(ar->fw_version_release);
	dump_data->fw_version_build = cpu_to_le32(ar->fw_version_build);
	dump_data->phy_capability = cpu_to_le32(ar->phy_capability);
	dump_data->hw_min_tx_power = cpu_to_le32(ar->hw_min_tx_power);
	dump_data->hw_max_tx_power = cpu_to_le32(ar->hw_max_tx_power);
	dump_data->ht_cap_info = cpu_to_le32(ar->ht_cap_info);
	dump_data->vht_cap_info = cpu_to_le32(ar->vht_cap_info);
	dump_data->num_rf_chains = cpu_to_le32(ar->num_rf_chains);

	strlcpy(dump_data->fw_ver, ar->hw->wiphy->fw_version,
		sizeof(dump_data->fw_ver));

	dump_data->kernel_ver_code = 0;
	strlcpy(dump_data->kernel_ver, init_utsname()->release,
		sizeof(dump_data->kernel_ver));

	dump_data->tv_sec = cpu_to_le64(crash_data->timestamp.tv_sec);
	dump_data->tv_nsec = cpu_to_le64(crash_data->timestamp.tv_nsec);

	/* Gather crash-dump */
	dump_tlv = (struct ath10k_tlv_dump_data *)(buf + sofar);
	dump_tlv->type = cpu_to_le32(ATH10K_FW_CRASH_DUMP_REGISTERS);
	dump_tlv->tlv_len = cpu_to_le32(sizeof(crash_data->registers));
	memcpy(dump_tlv->tlv_data, &crash_data->registers,
	       sizeof(crash_data->registers));
	sofar += sizeof(*dump_tlv) + sizeof(crash_data->registers);

	ar->debug.fw_crash_data->crashed_since_read = false;

	spin_unlock_bh(&ar->data_lock);

	return dump_data;
}

static int ath10k_fw_crash_dump_open(struct inode *inode, struct file *file)
{
	struct ath10k *ar = inode->i_private;
	struct ath10k_dump_file_data *dump;

	dump = ath10k_build_dump_file(ar);
	if (!dump)
		return -ENODATA;

	file->private_data = dump;

	return 0;
}

static ssize_t ath10k_fw_crash_dump_read(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct ath10k_dump_file_data *dump_file = file->private_data;

	return simple_read_from_buffer(user_buf, count, ppos,
				       dump_file,
				       le32_to_cpu(dump_file->len));
}

static int ath10k_fw_crash_dump_release(struct inode *inode,
					struct file *file)
{
	vfree(file->private_data);

	return 0;
}

static const struct file_operations fops_fw_crash_dump = {
	.open = ath10k_fw_crash_dump_open,
	.read = ath10k_fw_crash_dump_read,
	.release = ath10k_fw_crash_dump_release,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath10k_reg_addr_read(struct file *file,
				    char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	u8 buf[32];
	unsigned int len = 0;
	u32 reg_addr;

	mutex_lock(&ar->conf_mutex);
	reg_addr = ar->debug.reg_addr;
	mutex_unlock(&ar->conf_mutex);

	len += scnprintf(buf + len, sizeof(buf) - len, "0x%x\n", reg_addr);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath10k_reg_addr_write(struct file *file,
				     const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	u32 reg_addr;
	int ret;

	ret = kstrtou32_from_user(user_buf, count, 0, &reg_addr);
	if (ret)
		return ret;

	if (!IS_ALIGNED(reg_addr, 4))
		return -EFAULT;

	mutex_lock(&ar->conf_mutex);
	ar->debug.reg_addr = reg_addr;
	mutex_unlock(&ar->conf_mutex);

	return count;
}

static const struct file_operations fops_reg_addr = {
	.read = ath10k_reg_addr_read,
	.write = ath10k_reg_addr_write,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath10k_reg_value_read(struct file *file,
				     char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	u8 buf[48];
	unsigned int len;
	u32 reg_addr, reg_val;
	int ret;

	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH10K_STATE_ON &&
	    ar->state != ATH10K_STATE_UTF) {
		ret = -ENETDOWN;
		goto exit;
	}

	reg_addr = ar->debug.reg_addr;

	reg_val = ath10k_hif_read32(ar, reg_addr);
	len = scnprintf(buf, sizeof(buf), "0x%08x:0x%08x\n", reg_addr, reg_val);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, len);

exit:
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static ssize_t ath10k_reg_value_write(struct file *file,
				      const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	u32 reg_addr, reg_val;
	int ret;

	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH10K_STATE_ON &&
	    ar->state != ATH10K_STATE_UTF) {
		ret = -ENETDOWN;
		goto exit;
	}

	reg_addr = ar->debug.reg_addr;

	ret = kstrtou32_from_user(user_buf, count, 0, &reg_val);
	if (ret)
		goto exit;

	ath10k_hif_write32(ar, reg_addr, reg_val);

	ret = count;

exit:
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static const struct file_operations fops_reg_value = {
	.read = ath10k_reg_value_read,
	.write = ath10k_reg_value_write,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath10k_mem_value_read(struct file *file,
				     char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	u8 *buf;
	int ret;

	if (*ppos < 0)
		return -EINVAL;

	if (!count)
		return 0;

	mutex_lock(&ar->conf_mutex);

	buf = vmalloc(count);
	if (!buf) {
		ret = -ENOMEM;
		goto exit;
	}

	if (ar->state != ATH10K_STATE_ON &&
	    ar->state != ATH10K_STATE_UTF) {
		ret = -ENETDOWN;
		goto exit;
	}

	ret = ath10k_hif_diag_read(ar, *ppos, buf, count);
	if (ret) {
		ath10k_warn(ar, "failed to read address 0x%08x via diagnose window fnrom debugfs: %d\n",
			    (u32)(*ppos), ret);
		goto exit;
	}

	ret = copy_to_user(user_buf, buf, count);
	if (ret) {
		ret = -EFAULT;
		goto exit;
	}

	count -= ret;
	*ppos += count;
	ret = count;

exit:
	vfree(buf);
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static ssize_t ath10k_mem_value_write(struct file *file,
				      const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	u8 *buf;
	int ret;

	if (*ppos < 0)
		return -EINVAL;

	if (!count)
		return 0;

	mutex_lock(&ar->conf_mutex);

	buf = vmalloc(count);
	if (!buf) {
		ret = -ENOMEM;
		goto exit;
	}

	if (ar->state != ATH10K_STATE_ON &&
	    ar->state != ATH10K_STATE_UTF) {
		ret = -ENETDOWN;
		goto exit;
	}

	ret = copy_from_user(buf, user_buf, count);
	if (ret) {
		ret = -EFAULT;
		goto exit;
	}

	ret = ath10k_hif_diag_write(ar, *ppos, buf, count);
	if (ret) {
		ath10k_warn(ar, "failed to write address 0x%08x via diagnose window from debugfs: %d\n",
			    (u32)(*ppos), ret);
		goto exit;
	}

	*ppos += count;
	ret = count;

exit:
	vfree(buf);
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static const struct file_operations fops_mem_value = {
	.read = ath10k_mem_value_read,
	.write = ath10k_mem_value_write,
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
		ath10k_warn(ar, "failed to send htt stats request: %d\n", ret);
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

static ssize_t ath10k_read_htt_max_amsdu_ampdu(struct file *file,
					       char __user *user_buf,
					       size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	char buf[64];
	u8 amsdu = 3, ampdu = 64;
	unsigned int len;

	mutex_lock(&ar->conf_mutex);

	if (ar->debug.htt_max_amsdu)
		amsdu = ar->debug.htt_max_amsdu;

	if (ar->debug.htt_max_ampdu)
		ampdu = ar->debug.htt_max_ampdu;

	mutex_unlock(&ar->conf_mutex);

	len = scnprintf(buf, sizeof(buf), "%u %u\n", amsdu, ampdu);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath10k_write_htt_max_amsdu_ampdu(struct file *file,
						const char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	int res;
	char buf[64];
	unsigned int amsdu, ampdu;

	simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, user_buf, count);

	/* make sure that buf is null terminated */
	buf[sizeof(buf) - 1] = 0;

	res = sscanf(buf, "%u %u", &amsdu, &ampdu);

	if (res != 2)
		return -EINVAL;

	mutex_lock(&ar->conf_mutex);

	res = ath10k_htt_h2t_aggr_cfg_msg(&ar->htt, ampdu, amsdu);
	if (res)
		goto out;

	res = count;
	ar->debug.htt_max_amsdu = amsdu;
	ar->debug.htt_max_ampdu = ampdu;

out:
	mutex_unlock(&ar->conf_mutex);
	return res;
}

static const struct file_operations fops_htt_max_amsdu_ampdu = {
	.read = ath10k_read_htt_max_amsdu_ampdu,
	.write = ath10k_write_htt_max_amsdu_ampdu,
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
	char buf[64];

	len = scnprintf(buf, sizeof(buf), "0x%08x %u\n",
			ar->debug.fw_dbglog_mask, ar->debug.fw_dbglog_level);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath10k_write_fw_dbglog(struct file *file,
				      const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	int ret;
	char buf[64];
	unsigned int log_level, mask;

	simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, user_buf, count);

	/* make sure that buf is null terminated */
	buf[sizeof(buf) - 1] = 0;

	ret = sscanf(buf, "%x %u", &mask, &log_level);

	if (!ret)
		return -EINVAL;

	if (ret == 1)
		/* default if user did not specify */
		log_level = ATH10K_DBGLOG_LEVEL_WARN;

	mutex_lock(&ar->conf_mutex);

	ar->debug.fw_dbglog_mask = mask;
	ar->debug.fw_dbglog_level = log_level;

	if (ar->state == ATH10K_STATE_ON) {
		ret = ath10k_wmi_dbglog_cfg(ar, ar->debug.fw_dbglog_mask,
					    ar->debug.fw_dbglog_level);
		if (ret) {
			ath10k_warn(ar, "dbglog cfg failed from debugfs: %d\n",
				    ret);
			goto exit;
		}
	}

	ret = count;

exit:
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

/* TODO:  Would be nice to always support ethtool stats, would need to
 * move the stats storage out of ath10k_debug, or always have ath10k_debug
 * struct available..
 */

/* This generally cooresponds to the debugfs fw_stats file */
static const char ath10k_gstrings_stats[][ETH_GSTRING_LEN] = {
	"tx_pkts_nic",
	"tx_bytes_nic",
	"rx_pkts_nic",
	"rx_bytes_nic",
	"d_noise_floor",
	"d_cycle_count",
	"d_phy_error",
	"d_rts_bad",
	"d_rts_good",
	"d_tx_power", /* in .5 dbM I think */
	"d_rx_crc_err", /* fcs_bad */
	"d_no_beacon",
	"d_tx_mpdus_queued",
	"d_tx_msdu_queued",
	"d_tx_msdu_dropped",
	"d_local_enqued",
	"d_local_freed",
	"d_tx_ppdu_hw_queued",
	"d_tx_ppdu_reaped",
	"d_tx_fifo_underrun",
	"d_tx_ppdu_abort",
	"d_tx_mpdu_requed",
	"d_tx_excessive_retries",
	"d_tx_hw_rate",
	"d_tx_dropped_sw_retries",
	"d_tx_illegal_rate",
	"d_tx_continuous_xretries",
	"d_tx_timeout",
	"d_tx_mpdu_txop_limit",
	"d_pdev_resets",
	"d_rx_mid_ppdu_route_change",
	"d_rx_status",
	"d_rx_extra_frags_ring0",
	"d_rx_extra_frags_ring1",
	"d_rx_extra_frags_ring2",
	"d_rx_extra_frags_ring3",
	"d_rx_msdu_htt",
	"d_rx_mpdu_htt",
	"d_rx_msdu_stack",
	"d_rx_mpdu_stack",
	"d_rx_phy_err",
	"d_rx_phy_err_drops",
	"d_rx_mpdu_errors", /* FCS, MIC, ENC */
	"d_fw_crash_count",
	"d_fw_warm_reset_count",
	"d_fw_cold_reset_count",
};

#define ATH10K_SSTATS_LEN ARRAY_SIZE(ath10k_gstrings_stats)

void ath10k_debug_get_et_strings(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 u32 sset, u8 *data)
{
	if (sset == ETH_SS_STATS)
		memcpy(data, *ath10k_gstrings_stats,
		       sizeof(ath10k_gstrings_stats));
}

int ath10k_debug_get_et_sset_count(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif, int sset)
{
	if (sset == ETH_SS_STATS)
		return ATH10K_SSTATS_LEN;

	return 0;
}

void ath10k_debug_get_et_stats(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ethtool_stats *stats, u64 *data)
{
	struct ath10k *ar = hw->priv;
	static const struct ath10k_fw_stats_pdev zero_stats = {};
	const struct ath10k_fw_stats_pdev *pdev_stats;
	int i = 0, ret;

	mutex_lock(&ar->conf_mutex);

	if (ar->state == ATH10K_STATE_ON) {
		ret = ath10k_debug_fw_stats_request(ar);
		if (ret) {
			/* just print a warning and try to use older results */
			ath10k_warn(ar,
				    "failed to get fw stats for ethtool: %d\n",
				    ret);
		}
	}

	pdev_stats = list_first_entry_or_null(&ar->debug.fw_stats.pdevs,
					      struct ath10k_fw_stats_pdev,
					      list);
	if (!pdev_stats) {
		/* no results available so just return zeroes */
		pdev_stats = &zero_stats;
	}

	spin_lock_bh(&ar->data_lock);

	data[i++] = pdev_stats->hw_reaped; /* ppdu reaped */
	data[i++] = 0; /* tx bytes */
	data[i++] = pdev_stats->htt_mpdus;
	data[i++] = 0; /* rx bytes */
	data[i++] = pdev_stats->ch_noise_floor;
	data[i++] = pdev_stats->cycle_count;
	data[i++] = pdev_stats->phy_err_count;
	data[i++] = pdev_stats->rts_bad;
	data[i++] = pdev_stats->rts_good;
	data[i++] = pdev_stats->chan_tx_power;
	data[i++] = pdev_stats->fcs_bad;
	data[i++] = pdev_stats->no_beacons;
	data[i++] = pdev_stats->mpdu_enqued;
	data[i++] = pdev_stats->msdu_enqued;
	data[i++] = pdev_stats->wmm_drop;
	data[i++] = pdev_stats->local_enqued;
	data[i++] = pdev_stats->local_freed;
	data[i++] = pdev_stats->hw_queued;
	data[i++] = pdev_stats->hw_reaped;
	data[i++] = pdev_stats->underrun;
	data[i++] = pdev_stats->tx_abort;
	data[i++] = pdev_stats->mpdus_requed;
	data[i++] = pdev_stats->tx_ko;
	data[i++] = pdev_stats->data_rc;
	data[i++] = pdev_stats->sw_retry_failure;
	data[i++] = pdev_stats->illgl_rate_phy_err;
	data[i++] = pdev_stats->pdev_cont_xretry;
	data[i++] = pdev_stats->pdev_tx_timeout;
	data[i++] = pdev_stats->txop_ovf;
	data[i++] = pdev_stats->pdev_resets;
	data[i++] = pdev_stats->mid_ppdu_route_change;
	data[i++] = pdev_stats->status_rcvd;
	data[i++] = pdev_stats->r0_frags;
	data[i++] = pdev_stats->r1_frags;
	data[i++] = pdev_stats->r2_frags;
	data[i++] = pdev_stats->r3_frags;
	data[i++] = pdev_stats->htt_msdus;
	data[i++] = pdev_stats->htt_mpdus;
	data[i++] = pdev_stats->loc_msdus;
	data[i++] = pdev_stats->loc_mpdus;
	data[i++] = pdev_stats->phy_errs;
	data[i++] = pdev_stats->phy_err_drop;
	data[i++] = pdev_stats->mpdu_errs;
	data[i++] = ar->stats.fw_crash_counter;
	data[i++] = ar->stats.fw_warm_reset_counter;
	data[i++] = ar->stats.fw_cold_reset_counter;

	spin_unlock_bh(&ar->data_lock);

	mutex_unlock(&ar->conf_mutex);

	WARN_ON(i != ATH10K_SSTATS_LEN);
}

static const struct file_operations fops_fw_dbglog = {
	.read = ath10k_read_fw_dbglog,
	.write = ath10k_write_fw_dbglog,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int ath10k_debug_cal_data_open(struct inode *inode, struct file *file)
{
	struct ath10k *ar = inode->i_private;
	void *buf;
	u32 hi_addr;
	__le32 addr;
	int ret;

	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH10K_STATE_ON &&
	    ar->state != ATH10K_STATE_UTF) {
		ret = -ENETDOWN;
		goto err;
	}

	buf = vmalloc(QCA988X_CAL_DATA_LEN);
	if (!buf) {
		ret = -ENOMEM;
		goto err;
	}

	hi_addr = host_interest_item_address(HI_ITEM(hi_board_data));

	ret = ath10k_hif_diag_read(ar, hi_addr, &addr, sizeof(addr));
	if (ret) {
		ath10k_warn(ar, "failed to read hi_board_data address: %d\n", ret);
		goto err_vfree;
	}

	ret = ath10k_hif_diag_read(ar, le32_to_cpu(addr), buf,
				   QCA988X_CAL_DATA_LEN);
	if (ret) {
		ath10k_warn(ar, "failed to read calibration data: %d\n", ret);
		goto err_vfree;
	}

	file->private_data = buf;

	mutex_unlock(&ar->conf_mutex);

	return 0;

err_vfree:
	vfree(buf);

err:
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static ssize_t ath10k_debug_cal_data_read(struct file *file,
					  char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	void *buf = file->private_data;

	return simple_read_from_buffer(user_buf, count, ppos,
				       buf, QCA988X_CAL_DATA_LEN);
}

static int ath10k_debug_cal_data_release(struct inode *inode,
					 struct file *file)
{
	vfree(file->private_data);

	return 0;
}

static const struct file_operations fops_cal_data = {
	.open = ath10k_debug_cal_data_open,
	.read = ath10k_debug_cal_data_read,
	.release = ath10k_debug_cal_data_release,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath10k_read_nf_cal_period(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	unsigned int len;
	char buf[32];

	len = scnprintf(buf, sizeof(buf), "%d\n",
			ar->debug.nf_cal_period);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath10k_write_nf_cal_period(struct file *file,
					  const char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	unsigned long period;
	int ret;

	ret = kstrtoul_from_user(user_buf, count, 0, &period);
	if (ret)
		return ret;

	if (period > WMI_PDEV_PARAM_CAL_PERIOD_MAX)
		return -EINVAL;

	/* there's no way to switch back to the firmware default */
	if (period == 0)
		return -EINVAL;

	mutex_lock(&ar->conf_mutex);

	ar->debug.nf_cal_period = period;

	if (ar->state != ATH10K_STATE_ON) {
		/* firmware is not running, nothing else to do */
		ret = count;
		goto exit;
	}

	ret = ath10k_wmi_pdev_set_param(ar, ar->wmi.pdev_param->cal_period,
					ar->debug.nf_cal_period);
	if (ret) {
		ath10k_warn(ar, "cal period cfg failed from debugfs: %d\n",
			    ret);
		goto exit;
	}

	ret = count;

exit:
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static const struct file_operations fops_nf_cal_period = {
	.read = ath10k_read_nf_cal_period,
	.write = ath10k_write_nf_cal_period,
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
		ath10k_warn(ar, "failed to start htt stats workqueue: %d\n",
			    ret);

	if (ar->debug.fw_dbglog_mask) {
		ret = ath10k_wmi_dbglog_cfg(ar, ar->debug.fw_dbglog_mask,
					    ATH10K_DBGLOG_LEVEL_WARN);
		if (ret)
			/* not serious */
			ath10k_warn(ar, "failed to enable dbglog during start: %d",
				    ret);
	}

	if (ar->debug.pktlog_filter) {
		ret = ath10k_wmi_pdev_pktlog_enable(ar,
						    ar->debug.pktlog_filter);
		if (ret)
			/* not serious */
			ath10k_warn(ar,
				    "failed to enable pktlog filter %x: %d\n",
				    ar->debug.pktlog_filter, ret);
	} else {
		ret = ath10k_wmi_pdev_pktlog_disable(ar);
		if (ret)
			/* not serious */
			ath10k_warn(ar, "failed to disable pktlog: %d\n", ret);
	}

	if (ar->debug.nf_cal_period) {
		ret = ath10k_wmi_pdev_set_param(ar,
						ar->wmi.pdev_param->cal_period,
						ar->debug.nf_cal_period);
		if (ret)
			/* not serious */
			ath10k_warn(ar, "cal period cfg failed from debug start: %d\n",
				    ret);
	}

	return ret;
}

void ath10k_debug_stop(struct ath10k *ar)
{
	lockdep_assert_held(&ar->conf_mutex);

	/* Must not use _sync to avoid deadlock, we do that in
	 * ath10k_debug_destroy(). The check for htt_stats_mask is to avoid
	 * warning from del_timer(). */
	if (ar->debug.htt_stats_mask != 0)
		cancel_delayed_work(&ar->debug.htt_stats_dwork);

	ar->debug.htt_max_amsdu = 0;
	ar->debug.htt_max_ampdu = 0;

	ath10k_wmi_pdev_pktlog_disable(ar);
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

static ssize_t ath10k_write_pktlog_filter(struct file *file,
					  const char __user *ubuf,
					  size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	u32 filter;
	int ret;

	if (kstrtouint_from_user(ubuf, count, 0, &filter))
		return -EINVAL;

	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH10K_STATE_ON) {
		ar->debug.pktlog_filter = filter;
		ret = count;
		goto out;
	}

	if (filter && (filter != ar->debug.pktlog_filter)) {
		ret = ath10k_wmi_pdev_pktlog_enable(ar, filter);
		if (ret) {
			ath10k_warn(ar, "failed to enable pktlog filter %x: %d\n",
				    ar->debug.pktlog_filter, ret);
			goto out;
		}
	} else {
		ret = ath10k_wmi_pdev_pktlog_disable(ar);
		if (ret) {
			ath10k_warn(ar, "failed to disable pktlog: %d\n", ret);
			goto out;
		}
	}

	ar->debug.pktlog_filter = filter;
	ret = count;

out:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static ssize_t ath10k_read_pktlog_filter(struct file *file, char __user *ubuf,
					 size_t count, loff_t *ppos)
{
	char buf[32];
	struct ath10k *ar = file->private_data;
	int len = 0;

	mutex_lock(&ar->conf_mutex);
	len = scnprintf(buf, sizeof(buf) - len, "%08x\n",
			ar->debug.pktlog_filter);
	mutex_unlock(&ar->conf_mutex);

	return simple_read_from_buffer(ubuf, count, ppos, buf, len);
}

static const struct file_operations fops_pktlog_filter = {
	.read = ath10k_read_pktlog_filter,
	.write = ath10k_write_pktlog_filter,
	.open = simple_open
};

static ssize_t ath10k_write_quiet_period(struct file *file,
					 const char __user *ubuf,
					 size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	u32 period;

	if (kstrtouint_from_user(ubuf, count, 0, &period))
		return -EINVAL;

	if (period < ATH10K_QUIET_PERIOD_MIN) {
		ath10k_warn(ar, "Quiet period %u can not be lesser than 25ms\n",
			    period);
		return -EINVAL;
	}
	mutex_lock(&ar->conf_mutex);
	ar->thermal.quiet_period = period;
	mutex_unlock(&ar->conf_mutex);

	return count;
}

static ssize_t ath10k_read_quiet_period(struct file *file, char __user *ubuf,
					size_t count, loff_t *ppos)
{
	char buf[32];
	struct ath10k *ar = file->private_data;
	int len = 0;

	mutex_lock(&ar->conf_mutex);
	len = scnprintf(buf, sizeof(buf) - len, "%d\n",
			ar->thermal.quiet_period);
	mutex_unlock(&ar->conf_mutex);

	return simple_read_from_buffer(ubuf, count, ppos, buf, len);
}

static const struct file_operations fops_quiet_period = {
	.read = ath10k_read_quiet_period,
	.write = ath10k_write_quiet_period,
	.open = simple_open
};

int ath10k_debug_create(struct ath10k *ar)
{
	ar->debug.fw_crash_data = vzalloc(sizeof(*ar->debug.fw_crash_data));
	if (!ar->debug.fw_crash_data)
		return -ENOMEM;

	INIT_LIST_HEAD(&ar->debug.fw_stats.pdevs);
	INIT_LIST_HEAD(&ar->debug.fw_stats.vdevs);
	INIT_LIST_HEAD(&ar->debug.fw_stats.peers);

	return 0;
}

void ath10k_debug_destroy(struct ath10k *ar)
{
	vfree(ar->debug.fw_crash_data);
	ar->debug.fw_crash_data = NULL;

	ath10k_debug_fw_stats_reset(ar);
}

int ath10k_debug_register(struct ath10k *ar)
{
	ar->debug.debugfs_phy = debugfs_create_dir("ath10k",
						   ar->hw->wiphy->debugfsdir);
	if (IS_ERR_OR_NULL(ar->debug.debugfs_phy)) {
		if (IS_ERR(ar->debug.debugfs_phy))
			return PTR_ERR(ar->debug.debugfs_phy);

		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&ar->debug.htt_stats_dwork,
			  ath10k_debug_htt_stats_dwork);

	init_completion(&ar->debug.fw_stats_complete);

	debugfs_create_file("fw_stats", S_IRUSR, ar->debug.debugfs_phy, ar,
			    &fops_fw_stats);

	debugfs_create_file("fw_reset_stats", S_IRUSR, ar->debug.debugfs_phy,
			    ar, &fops_fw_reset_stats);

	debugfs_create_file("wmi_services", S_IRUSR, ar->debug.debugfs_phy, ar,
			    &fops_wmi_services);

	debugfs_create_file("simulate_fw_crash", S_IRUSR, ar->debug.debugfs_phy,
			    ar, &fops_simulate_fw_crash);

	debugfs_create_file("fw_crash_dump", S_IRUSR, ar->debug.debugfs_phy,
			    ar, &fops_fw_crash_dump);

	debugfs_create_file("reg_addr", S_IRUSR | S_IWUSR,
			    ar->debug.debugfs_phy, ar, &fops_reg_addr);

	debugfs_create_file("reg_value", S_IRUSR | S_IWUSR,
			    ar->debug.debugfs_phy, ar, &fops_reg_value);

	debugfs_create_file("mem_value", S_IRUSR | S_IWUSR,
			    ar->debug.debugfs_phy, ar, &fops_mem_value);

	debugfs_create_file("chip_id", S_IRUSR, ar->debug.debugfs_phy,
			    ar, &fops_chip_id);

	debugfs_create_file("htt_stats_mask", S_IRUSR, ar->debug.debugfs_phy,
			    ar, &fops_htt_stats_mask);

	debugfs_create_file("htt_max_amsdu_ampdu", S_IRUSR | S_IWUSR,
			    ar->debug.debugfs_phy, ar,
			    &fops_htt_max_amsdu_ampdu);

	debugfs_create_file("fw_dbglog", S_IRUSR, ar->debug.debugfs_phy,
			    ar, &fops_fw_dbglog);

	debugfs_create_file("cal_data", S_IRUSR, ar->debug.debugfs_phy,
			    ar, &fops_cal_data);

	debugfs_create_file("nf_cal_period", S_IRUSR | S_IWUSR,
			    ar->debug.debugfs_phy, ar, &fops_nf_cal_period);

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

	debugfs_create_file("pktlog_filter", S_IRUGO | S_IWUSR,
			    ar->debug.debugfs_phy, ar, &fops_pktlog_filter);

	debugfs_create_file("quiet_period", S_IRUGO | S_IWUSR,
			    ar->debug.debugfs_phy, ar, &fops_quiet_period);

	return 0;
}

void ath10k_debug_unregister(struct ath10k *ar)
{
	cancel_delayed_work_sync(&ar->debug.htt_stats_dwork);
}

#endif /* CONFIG_ATH10K_DEBUGFS */

#ifdef CONFIG_ATH10K_DEBUG
void ath10k_dbg(struct ath10k *ar, enum ath10k_debug_mask mask,
		const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	if (ath10k_debug_mask & mask)
		dev_printk(KERN_DEBUG, ar->dev, "%pV", &vaf);

	trace_ath10k_log_dbg(ar, mask, &vaf);

	va_end(args);
}
EXPORT_SYMBOL(ath10k_dbg);

void ath10k_dbg_dump(struct ath10k *ar,
		     enum ath10k_debug_mask mask,
		     const char *msg, const char *prefix,
		     const void *buf, size_t len)
{
	char linebuf[256];
	unsigned int linebuflen;
	const void *ptr;

	if (ath10k_debug_mask & mask) {
		if (msg)
			ath10k_dbg(ar, mask, "%s\n", msg);

		for (ptr = buf; (ptr - buf) < len; ptr += 16) {
			linebuflen = 0;
			linebuflen += scnprintf(linebuf + linebuflen,
						sizeof(linebuf) - linebuflen,
						"%s%08x: ",
						(prefix ? prefix : ""),
						(unsigned int)(ptr - buf));
			hex_dump_to_buffer(ptr, len - (ptr - buf), 16, 1,
					   linebuf + linebuflen,
					   sizeof(linebuf) - linebuflen, true);
			dev_printk(KERN_DEBUG, ar->dev, "%s\n", linebuf);
		}
	}

	/* tracing code doesn't like null strings :/ */
	trace_ath10k_log_dbg_dump(ar, msg ? msg : "", prefix ? prefix : "",
				  buf, len);
}
EXPORT_SYMBOL(ath10k_dbg_dump);

#endif /* CONFIG_ATH10K_DEBUG */
