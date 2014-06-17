/*
 * mac80211 glue code for mac80211 ST-Ericsson CW1200 drivers
 * DebugFS code
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@lockless.no>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include "cw1200.h"
#include "debug.h"
#include "fwio.h"

/* join_status */
static const char * const cw1200_debug_join_status[] = {
	"passive",
	"monitor",
	"station (joining)",
	"station (not authenticated yet)",
	"station",
	"adhoc",
	"access point",
};

/* WSM_JOIN_PREAMBLE_... */
static const char * const cw1200_debug_preamble[] = {
	"long",
	"short",
	"long on 1 and 2 Mbps",
};


static const char * const cw1200_debug_link_id[] = {
	"OFF",
	"REQ",
	"SOFT",
	"HARD",
	"RESET",
	"RESET_REMAP",
};

static const char *cw1200_debug_mode(int mode)
{
	switch (mode) {
	case NL80211_IFTYPE_UNSPECIFIED:
		return "unspecified";
	case NL80211_IFTYPE_MONITOR:
		return "monitor";
	case NL80211_IFTYPE_STATION:
		return "station";
	case NL80211_IFTYPE_ADHOC:
		return "adhoc";
	case NL80211_IFTYPE_MESH_POINT:
		return "mesh point";
	case NL80211_IFTYPE_AP:
		return "access point";
	case NL80211_IFTYPE_P2P_CLIENT:
		return "p2p client";
	case NL80211_IFTYPE_P2P_GO:
		return "p2p go";
	default:
		return "unsupported";
	}
}

static void cw1200_queue_status_show(struct seq_file *seq,
				     struct cw1200_queue *q)
{
	int i;
	seq_printf(seq, "Queue       %d:\n", q->queue_id);
	seq_printf(seq, "  capacity: %zu\n", q->capacity);
	seq_printf(seq, "  queued:   %zu\n", q->num_queued);
	seq_printf(seq, "  pending:  %zu\n", q->num_pending);
	seq_printf(seq, "  sent:     %zu\n", q->num_sent);
	seq_printf(seq, "  locked:   %s\n", q->tx_locked_cnt ? "yes" : "no");
	seq_printf(seq, "  overfull: %s\n", q->overfull ? "yes" : "no");
	seq_puts(seq,   "  link map: 0-> ");
	for (i = 0; i < q->stats->map_capacity; ++i)
		seq_printf(seq, "%.2d ", q->link_map_cache[i]);
	seq_printf(seq, "<-%zu\n", q->stats->map_capacity);
}

static void cw1200_debug_print_map(struct seq_file *seq,
				   struct cw1200_common *priv,
				   const char *label,
				   u32 map)
{
	int i;
	seq_printf(seq, "%s0-> ", label);
	for (i = 0; i < priv->tx_queue_stats.map_capacity; ++i)
		seq_printf(seq, "%s ", (map & BIT(i)) ? "**" : "..");
	seq_printf(seq, "<-%zu\n", priv->tx_queue_stats.map_capacity - 1);
}

static int cw1200_status_show(struct seq_file *seq, void *v)
{
	int i;
	struct list_head *item;
	struct cw1200_common *priv = seq->private;
	struct cw1200_debug_priv *d = priv->debug;

	seq_puts(seq,   "CW1200 Wireless LAN driver status\n");
	seq_printf(seq, "Hardware:   %d.%d\n",
		   priv->wsm_caps.hw_id,
		   priv->wsm_caps.hw_subid);
	seq_printf(seq, "Firmware:   %s %d.%d\n",
		   cw1200_fw_types[priv->wsm_caps.fw_type],
		   priv->wsm_caps.fw_ver,
		   priv->wsm_caps.fw_build);
	seq_printf(seq, "FW API:     %d\n",
		   priv->wsm_caps.fw_api);
	seq_printf(seq, "FW caps:    0x%.4X\n",
		   priv->wsm_caps.fw_cap);
	seq_printf(seq, "FW label:  '%s'\n",
		   priv->wsm_caps.fw_label);
	seq_printf(seq, "Mode:       %s%s\n",
		   cw1200_debug_mode(priv->mode),
		   priv->listening ? " (listening)" : "");
	seq_printf(seq, "Join state: %s\n",
		   cw1200_debug_join_status[priv->join_status]);
	if (priv->channel)
		seq_printf(seq, "Channel:    %d%s\n",
			   priv->channel->hw_value,
			   priv->channel_switch_in_progress ?
			   " (switching)" : "");
	if (priv->rx_filter.promiscuous)
		seq_puts(seq,   "Filter:     promisc\n");
	else if (priv->rx_filter.fcs)
		seq_puts(seq,   "Filter:     fcs\n");
	if (priv->rx_filter.bssid)
		seq_puts(seq,   "Filter:     bssid\n");
	if (!priv->disable_beacon_filter)
		seq_puts(seq,   "Filter:     beacons\n");

	if (priv->enable_beacon ||
	    priv->mode == NL80211_IFTYPE_AP ||
	    priv->mode == NL80211_IFTYPE_ADHOC ||
	    priv->mode == NL80211_IFTYPE_MESH_POINT ||
	    priv->mode == NL80211_IFTYPE_P2P_GO)
		seq_printf(seq, "Beaconing:  %s\n",
			   priv->enable_beacon ?
			   "enabled" : "disabled");

	for (i = 0; i < 4; ++i)
		seq_printf(seq, "EDCA(%d):    %d, %d, %d, %d, %d\n", i,
			   priv->edca.params[i].cwmin,
			   priv->edca.params[i].cwmax,
			   priv->edca.params[i].aifns,
			   priv->edca.params[i].txop_limit,
			   priv->edca.params[i].max_rx_lifetime);

	if (priv->join_status == CW1200_JOIN_STATUS_STA) {
		static const char *pm_mode = "unknown";
		switch (priv->powersave_mode.mode) {
		case WSM_PSM_ACTIVE:
			pm_mode = "off";
			break;
		case WSM_PSM_PS:
			pm_mode = "on";
			break;
		case WSM_PSM_FAST_PS:
			pm_mode = "dynamic";
			break;
		}
		seq_printf(seq, "Preamble:   %s\n",
			   cw1200_debug_preamble[priv->association_mode.preamble]);
		seq_printf(seq, "AMPDU spcn: %d\n",
			   priv->association_mode.mpdu_start_spacing);
		seq_printf(seq, "Basic rate: 0x%.8X\n",
			   le32_to_cpu(priv->association_mode.basic_rate_set));
		seq_printf(seq, "Bss lost:   %d beacons\n",
			   priv->bss_params.beacon_lost_count);
		seq_printf(seq, "AID:        %d\n",
			   priv->bss_params.aid);
		seq_printf(seq, "Rates:      0x%.8X\n",
			   priv->bss_params.operational_rate_set);
		seq_printf(seq, "Powersave:  %s\n", pm_mode);
	}
	seq_printf(seq, "HT:         %s\n",
		   cw1200_is_ht(&priv->ht_info) ? "on" : "off");
	if (cw1200_is_ht(&priv->ht_info)) {
		seq_printf(seq, "Greenfield: %s\n",
			   cw1200_ht_greenfield(&priv->ht_info) ? "yes" : "no");
		seq_printf(seq, "AMPDU dens: %d\n",
			   cw1200_ht_ampdu_density(&priv->ht_info));
	}
	seq_printf(seq, "RSSI thold: %d\n",
		   priv->cqm_rssi_thold);
	seq_printf(seq, "RSSI hyst:  %d\n",
		   priv->cqm_rssi_hyst);
	seq_printf(seq, "Long retr:  %d\n",
		   priv->long_frame_max_tx_count);
	seq_printf(seq, "Short retr: %d\n",
		   priv->short_frame_max_tx_count);
	spin_lock_bh(&priv->tx_policy_cache.lock);
	i = 0;
	list_for_each(item, &priv->tx_policy_cache.used)
		++i;
	spin_unlock_bh(&priv->tx_policy_cache.lock);
	seq_printf(seq, "RC in use:  %d\n", i);

	seq_puts(seq, "\n");
	for (i = 0; i < 4; ++i) {
		cw1200_queue_status_show(seq, &priv->tx_queue[i]);
		seq_puts(seq, "\n");
	}

	cw1200_debug_print_map(seq, priv, "Link map:   ",
			       priv->link_id_map);
	cw1200_debug_print_map(seq, priv, "Asleep map: ",
			       priv->sta_asleep_mask);
	cw1200_debug_print_map(seq, priv, "PSPOLL map: ",
			       priv->pspoll_mask);

	seq_puts(seq, "\n");

	for (i = 0; i < CW1200_MAX_STA_IN_AP_MODE; ++i) {
		if (priv->link_id_db[i].status) {
			seq_printf(seq, "Link %d:     %s, %pM\n",
				   i + 1,
				   cw1200_debug_link_id[priv->link_id_db[i].status],
				   priv->link_id_db[i].mac);
		}
	}

	seq_puts(seq, "\n");

	seq_printf(seq, "BH status:  %s\n",
		   atomic_read(&priv->bh_term) ? "terminated" : "alive");
	seq_printf(seq, "Pending RX: %d\n",
		   atomic_read(&priv->bh_rx));
	seq_printf(seq, "Pending TX: %d\n",
		   atomic_read(&priv->bh_tx));
	if (priv->bh_error)
		seq_printf(seq, "BH errcode: %d\n",
			   priv->bh_error);
	seq_printf(seq, "TX bufs:    %d x %d bytes\n",
		   priv->wsm_caps.input_buffers,
		   priv->wsm_caps.input_buffer_size);
	seq_printf(seq, "Used bufs:  %d\n",
		   priv->hw_bufs_used);
	seq_printf(seq, "Powermgmt:  %s\n",
		   priv->powersave_enabled ? "on" : "off");
	seq_printf(seq, "Device:     %s\n",
		   priv->device_can_sleep ? "asleep" : "awake");

	spin_lock(&priv->wsm_cmd.lock);
	seq_printf(seq, "WSM status: %s\n",
		   priv->wsm_cmd.done ? "idle" : "active");
	seq_printf(seq, "WSM cmd:    0x%.4X (%td bytes)\n",
		   priv->wsm_cmd.cmd, priv->wsm_cmd.len);
	seq_printf(seq, "WSM retval: %d\n",
		   priv->wsm_cmd.ret);
	spin_unlock(&priv->wsm_cmd.lock);

	seq_printf(seq, "Datapath:   %s\n",
		   atomic_read(&priv->tx_lock) ? "locked" : "unlocked");
	if (atomic_read(&priv->tx_lock))
		seq_printf(seq, "TXlock cnt: %d\n",
			   atomic_read(&priv->tx_lock));

	seq_printf(seq, "TXed:       %d\n",
		   d->tx);
	seq_printf(seq, "AGG TXed:   %d\n",
		   d->tx_agg);
	seq_printf(seq, "MULTI TXed: %d (%d)\n",
		   d->tx_multi, d->tx_multi_frames);
	seq_printf(seq, "RXed:       %d\n",
		   d->rx);
	seq_printf(seq, "AGG RXed:   %d\n",
		   d->rx_agg);
	seq_printf(seq, "TX miss:    %d\n",
		   d->tx_cache_miss);
	seq_printf(seq, "TX align:   %d\n",
		   d->tx_align);
	seq_printf(seq, "TX burst:   %d\n",
		   d->tx_burst);
	seq_printf(seq, "TX TTL:     %d\n",
		   d->tx_ttl);
	seq_printf(seq, "Scan:       %s\n",
		   atomic_read(&priv->scan.in_progress) ? "active" : "idle");

	return 0;
}

static int cw1200_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, &cw1200_status_show,
		inode->i_private);
}

static const struct file_operations fops_status = {
	.open = cw1200_status_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int cw1200_counters_show(struct seq_file *seq, void *v)
{
	int ret;
	struct cw1200_common *priv = seq->private;
	struct wsm_mib_counters_table counters;

	ret = wsm_get_counters_table(priv, &counters);
	if (ret)
		return ret;

#define PUT_COUNTER(tab, name) \
	seq_printf(seq, "%s:" tab "%d\n", #name, \
		__le32_to_cpu(counters.name))

	PUT_COUNTER("\t\t", plcp_errors);
	PUT_COUNTER("\t\t", fcs_errors);
	PUT_COUNTER("\t\t", tx_packets);
	PUT_COUNTER("\t\t", rx_packets);
	PUT_COUNTER("\t\t", rx_packet_errors);
	PUT_COUNTER("\t",   rx_decryption_failures);
	PUT_COUNTER("\t\t", rx_mic_failures);
	PUT_COUNTER("\t",   rx_no_key_failures);
	PUT_COUNTER("\t",   tx_multicast_frames);
	PUT_COUNTER("\t",   tx_frames_success);
	PUT_COUNTER("\t",   tx_frame_failures);
	PUT_COUNTER("\t",   tx_frames_retried);
	PUT_COUNTER("\t",   tx_frames_multi_retried);
	PUT_COUNTER("\t",   rx_frame_duplicates);
	PUT_COUNTER("\t\t", rts_success);
	PUT_COUNTER("\t\t", rts_failures);
	PUT_COUNTER("\t\t", ack_failures);
	PUT_COUNTER("\t",   rx_multicast_frames);
	PUT_COUNTER("\t",   rx_frames_success);
	PUT_COUNTER("\t",   rx_cmac_icv_errors);
	PUT_COUNTER("\t\t", rx_cmac_replays);
	PUT_COUNTER("\t",   rx_mgmt_ccmp_replays);

#undef PUT_COUNTER

	return 0;
}

static int cw1200_counters_open(struct inode *inode, struct file *file)
{
	return single_open(file, &cw1200_counters_show,
		inode->i_private);
}

static const struct file_operations fops_counters = {
	.open = cw1200_counters_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static ssize_t cw1200_wsm_dumps(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct cw1200_common *priv = file->private_data;
	char buf[1];

	if (!count)
		return -EINVAL;
	if (copy_from_user(buf, user_buf, 1))
		return -EFAULT;

	if (buf[0] == '1')
		priv->wsm_enable_wsm_dumps = 1;
	else
		priv->wsm_enable_wsm_dumps = 0;

	return count;
}

static const struct file_operations fops_wsm_dumps = {
	.open = simple_open,
	.write = cw1200_wsm_dumps,
	.llseek = default_llseek,
};

int cw1200_debug_init(struct cw1200_common *priv)
{
	int ret = -ENOMEM;
	struct cw1200_debug_priv *d = kzalloc(sizeof(struct cw1200_debug_priv),
			GFP_KERNEL);
	priv->debug = d;
	if (!d)
		return ret;

	d->debugfs_phy = debugfs_create_dir("cw1200",
					    priv->hw->wiphy->debugfsdir);
	if (!d->debugfs_phy)
		goto err;

	if (!debugfs_create_file("status", S_IRUSR, d->debugfs_phy,
				 priv, &fops_status))
		goto err;

	if (!debugfs_create_file("counters", S_IRUSR, d->debugfs_phy,
				 priv, &fops_counters))
		goto err;

	if (!debugfs_create_file("wsm_dumps", S_IWUSR, d->debugfs_phy,
				 priv, &fops_wsm_dumps))
		goto err;

	return 0;

err:
	priv->debug = NULL;
	debugfs_remove_recursive(d->debugfs_phy);
	kfree(d);
	return ret;
}

void cw1200_debug_release(struct cw1200_common *priv)
{
	struct cw1200_debug_priv *d = priv->debug;
	if (d) {
		debugfs_remove_recursive(d->debugfs_phy);
		priv->debug = NULL;
		kfree(d);
	}
}
