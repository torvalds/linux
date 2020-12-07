// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2019 Google, Inc.
 */

#include <linux/ethtool.h>
#include <linux/rtnetlink.h>
#include "gve.h"
#include "gve_adminq.h"

static void gve_get_drvinfo(struct net_device *netdev,
			    struct ethtool_drvinfo *info)
{
	struct gve_priv *priv = netdev_priv(netdev);

	strlcpy(info->driver, "gve", sizeof(info->driver));
	strlcpy(info->version, gve_version_str, sizeof(info->version));
	strlcpy(info->bus_info, pci_name(priv->pdev), sizeof(info->bus_info));
}

static void gve_set_msglevel(struct net_device *netdev, u32 value)
{
	struct gve_priv *priv = netdev_priv(netdev);

	priv->msg_enable = value;
}

static u32 gve_get_msglevel(struct net_device *netdev)
{
	struct gve_priv *priv = netdev_priv(netdev);

	return priv->msg_enable;
}

static const char gve_gstrings_main_stats[][ETH_GSTRING_LEN] = {
	"rx_packets", "tx_packets", "rx_bytes", "tx_bytes",
	"rx_dropped", "tx_dropped", "tx_timeouts",
	"rx_skb_alloc_fail", "rx_buf_alloc_fail", "rx_desc_err_dropped_pkt",
	"interface_up_cnt", "interface_down_cnt", "reset_cnt",
	"page_alloc_fail", "dma_mapping_error", "stats_report_trigger_cnt",
};

static const char gve_gstrings_rx_stats[][ETH_GSTRING_LEN] = {
	"rx_posted_desc[%u]", "rx_completed_desc[%u]", "rx_bytes[%u]",
	"rx_dropped_pkt[%u]", "rx_copybreak_pkt[%u]", "rx_copied_pkt[%u]",
	"rx_queue_drop_cnt[%u]", "rx_no_buffers_posted[%u]",
	"rx_drops_packet_over_mru[%u]", "rx_drops_invalid_checksum[%u]",
};

static const char gve_gstrings_tx_stats[][ETH_GSTRING_LEN] = {
	"tx_posted_desc[%u]", "tx_completed_desc[%u]", "tx_bytes[%u]",
	"tx_wake[%u]", "tx_stop[%u]", "tx_event_counter[%u]",
	"tx_dma_mapping_error[%u]",
};

static const char gve_gstrings_adminq_stats[][ETH_GSTRING_LEN] = {
	"adminq_prod_cnt", "adminq_cmd_fail", "adminq_timeouts",
	"adminq_describe_device_cnt", "adminq_cfg_device_resources_cnt",
	"adminq_register_page_list_cnt", "adminq_unregister_page_list_cnt",
	"adminq_create_tx_queue_cnt", "adminq_create_rx_queue_cnt",
	"adminq_destroy_tx_queue_cnt", "adminq_destroy_rx_queue_cnt",
	"adminq_dcfg_device_resources_cnt", "adminq_set_driver_parameter_cnt",
	"adminq_report_stats_cnt", "adminq_report_link_speed_cnt"
};

static const char gve_gstrings_priv_flags[][ETH_GSTRING_LEN] = {
	"report-stats",
};

#define GVE_MAIN_STATS_LEN  ARRAY_SIZE(gve_gstrings_main_stats)
#define GVE_ADMINQ_STATS_LEN  ARRAY_SIZE(gve_gstrings_adminq_stats)
#define NUM_GVE_TX_CNTS	ARRAY_SIZE(gve_gstrings_tx_stats)
#define NUM_GVE_RX_CNTS	ARRAY_SIZE(gve_gstrings_rx_stats)
#define GVE_PRIV_FLAGS_STR_LEN ARRAY_SIZE(gve_gstrings_priv_flags)

static void gve_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	struct gve_priv *priv = netdev_priv(netdev);
	char *s = (char *)data;
	int i, j;

	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(s, *gve_gstrings_main_stats,
		       sizeof(gve_gstrings_main_stats));
		s += sizeof(gve_gstrings_main_stats);

		for (i = 0; i < priv->rx_cfg.num_queues; i++) {
			for (j = 0; j < NUM_GVE_RX_CNTS; j++) {
				snprintf(s, ETH_GSTRING_LEN,
					 gve_gstrings_rx_stats[j], i);
				s += ETH_GSTRING_LEN;
			}
		}

		for (i = 0; i < priv->tx_cfg.num_queues; i++) {
			for (j = 0; j < NUM_GVE_TX_CNTS; j++) {
				snprintf(s, ETH_GSTRING_LEN,
					 gve_gstrings_tx_stats[j], i);
				s += ETH_GSTRING_LEN;
			}
		}

		memcpy(s, *gve_gstrings_adminq_stats,
		       sizeof(gve_gstrings_adminq_stats));
		s += sizeof(gve_gstrings_adminq_stats);
		break;

	case ETH_SS_PRIV_FLAGS:
		memcpy(s, *gve_gstrings_priv_flags,
		       sizeof(gve_gstrings_priv_flags));
		s += sizeof(gve_gstrings_priv_flags);
		break;

	default:
		break;
	}
}

static int gve_get_sset_count(struct net_device *netdev, int sset)
{
	struct gve_priv *priv = netdev_priv(netdev);

	switch (sset) {
	case ETH_SS_STATS:
		return GVE_MAIN_STATS_LEN + GVE_ADMINQ_STATS_LEN +
		       (priv->rx_cfg.num_queues * NUM_GVE_RX_CNTS) +
		       (priv->tx_cfg.num_queues * NUM_GVE_TX_CNTS);
	case ETH_SS_PRIV_FLAGS:
		return GVE_PRIV_FLAGS_STR_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static void
gve_get_ethtool_stats(struct net_device *netdev,
		      struct ethtool_stats *stats, u64 *data)
{
	u64 tmp_rx_pkts, tmp_rx_bytes, tmp_rx_skb_alloc_fail,	tmp_rx_buf_alloc_fail,
		tmp_rx_desc_err_dropped_pkt, tmp_tx_pkts, tmp_tx_bytes;
	u64 rx_buf_alloc_fail, rx_desc_err_dropped_pkt, rx_pkts,
		rx_skb_alloc_fail, rx_bytes, tx_pkts, tx_bytes;
	int stats_idx, base_stats_idx, max_stats_idx;
	struct stats *report_stats;
	int *rx_qid_to_stats_idx;
	int *tx_qid_to_stats_idx;
	struct gve_priv *priv;
	bool skip_nic_stats;
	unsigned int start;
	int ring;
	int i, j;

	ASSERT_RTNL();

	priv = netdev_priv(netdev);
	report_stats = priv->stats_report->stats;
	rx_qid_to_stats_idx = kmalloc_array(priv->rx_cfg.num_queues,
					    sizeof(int), GFP_KERNEL);
	if (!rx_qid_to_stats_idx)
		return;
	tx_qid_to_stats_idx = kmalloc_array(priv->tx_cfg.num_queues,
					    sizeof(int), GFP_KERNEL);
	if (!tx_qid_to_stats_idx) {
		kfree(rx_qid_to_stats_idx);
		return;
	}
	for (rx_pkts = 0, rx_bytes = 0, rx_skb_alloc_fail = 0,
	     rx_buf_alloc_fail = 0, rx_desc_err_dropped_pkt = 0, ring = 0;
	     ring < priv->rx_cfg.num_queues; ring++) {
		if (priv->rx) {
			do {
				struct gve_rx_ring *rx = &priv->rx[ring];

				start =
				  u64_stats_fetch_begin(&priv->rx[ring].statss);
				tmp_rx_pkts = rx->rpackets;
				tmp_rx_bytes = rx->rbytes;
				tmp_rx_skb_alloc_fail = rx->rx_skb_alloc_fail;
				tmp_rx_buf_alloc_fail = rx->rx_buf_alloc_fail;
				tmp_rx_desc_err_dropped_pkt =
					rx->rx_desc_err_dropped_pkt;
			} while (u64_stats_fetch_retry(&priv->rx[ring].statss,
						       start));
			rx_pkts += tmp_rx_pkts;
			rx_bytes += tmp_rx_bytes;
			rx_skb_alloc_fail += tmp_rx_skb_alloc_fail;
			rx_buf_alloc_fail += tmp_rx_buf_alloc_fail;
			rx_desc_err_dropped_pkt += tmp_rx_desc_err_dropped_pkt;
		}
	}
	for (tx_pkts = 0, tx_bytes = 0, ring = 0;
	     ring < priv->tx_cfg.num_queues; ring++) {
		if (priv->tx) {
			do {
				start =
				  u64_stats_fetch_begin(&priv->tx[ring].statss);
				tmp_tx_pkts = priv->tx[ring].pkt_done;
				tmp_tx_bytes = priv->tx[ring].bytes_done;
			} while (u64_stats_fetch_retry(&priv->tx[ring].statss,
						       start));
			tx_pkts += tmp_tx_pkts;
			tx_bytes += tmp_tx_bytes;
		}
	}

	i = 0;
	data[i++] = rx_pkts;
	data[i++] = tx_pkts;
	data[i++] = rx_bytes;
	data[i++] = tx_bytes;
	/* total rx dropped packets */
	data[i++] = rx_skb_alloc_fail + rx_buf_alloc_fail +
		    rx_desc_err_dropped_pkt;
	/* Skip tx_dropped */
	i++;

	data[i++] = priv->tx_timeo_cnt;
	data[i++] = rx_skb_alloc_fail;
	data[i++] = rx_buf_alloc_fail;
	data[i++] = rx_desc_err_dropped_pkt;
	data[i++] = priv->interface_up_cnt;
	data[i++] = priv->interface_down_cnt;
	data[i++] = priv->reset_cnt;
	data[i++] = priv->page_alloc_fail;
	data[i++] = priv->dma_mapping_error;
	data[i++] = priv->stats_report_trigger_cnt;
	i = GVE_MAIN_STATS_LEN;

	/* For rx cross-reporting stats, start from nic rx stats in report */
	base_stats_idx = GVE_TX_STATS_REPORT_NUM * priv->tx_cfg.num_queues +
		GVE_RX_STATS_REPORT_NUM * priv->rx_cfg.num_queues;
	max_stats_idx = NIC_RX_STATS_REPORT_NUM * priv->rx_cfg.num_queues +
		base_stats_idx;
	/* Preprocess the stats report for rx, map queue id to start index */
	skip_nic_stats = false;
	for (stats_idx = base_stats_idx; stats_idx < max_stats_idx;
		stats_idx += NIC_RX_STATS_REPORT_NUM) {
		u32 stat_name = be32_to_cpu(report_stats[stats_idx].stat_name);
		u32 queue_id = be32_to_cpu(report_stats[stats_idx].queue_id);

		if (stat_name == 0) {
			/* no stats written by NIC yet */
			skip_nic_stats = true;
			break;
		}
		rx_qid_to_stats_idx[queue_id] = stats_idx;
	}
	/* walk RX rings */
	if (priv->rx) {
		for (ring = 0; ring < priv->rx_cfg.num_queues; ring++) {
			struct gve_rx_ring *rx = &priv->rx[ring];

			data[i++] = rx->fill_cnt;
			data[i++] = rx->cnt;
			do {
				start =
				  u64_stats_fetch_begin(&priv->rx[ring].statss);
				tmp_rx_bytes = rx->rbytes;
				tmp_rx_skb_alloc_fail = rx->rx_skb_alloc_fail;
				tmp_rx_buf_alloc_fail = rx->rx_buf_alloc_fail;
				tmp_rx_desc_err_dropped_pkt =
					rx->rx_desc_err_dropped_pkt;
			} while (u64_stats_fetch_retry(&priv->rx[ring].statss,
						       start));
			data[i++] = tmp_rx_bytes;
			/* rx dropped packets */
			data[i++] = tmp_rx_skb_alloc_fail +
				tmp_rx_buf_alloc_fail +
				tmp_rx_desc_err_dropped_pkt;
			data[i++] = rx->rx_copybreak_pkt;
			data[i++] = rx->rx_copied_pkt;
			/* stats from NIC */
			if (skip_nic_stats) {
				/* skip NIC rx stats */
				i += NIC_RX_STATS_REPORT_NUM;
				continue;
			}
			for (j = 0; j < NIC_RX_STATS_REPORT_NUM; j++) {
				u64 value =
				be64_to_cpu(report_stats[rx_qid_to_stats_idx[ring] + j].value);

				data[i++] = value;
			}
		}
	} else {
		i += priv->rx_cfg.num_queues * NUM_GVE_RX_CNTS;
	}

	/* For tx cross-reporting stats, start from nic tx stats in report */
	base_stats_idx = max_stats_idx;
	max_stats_idx = NIC_TX_STATS_REPORT_NUM * priv->tx_cfg.num_queues +
		max_stats_idx;
	/* Preprocess the stats report for tx, map queue id to start index */
	skip_nic_stats = false;
	for (stats_idx = base_stats_idx; stats_idx < max_stats_idx;
		stats_idx += NIC_TX_STATS_REPORT_NUM) {
		u32 stat_name = be32_to_cpu(report_stats[stats_idx].stat_name);
		u32 queue_id = be32_to_cpu(report_stats[stats_idx].queue_id);

		if (stat_name == 0) {
			/* no stats written by NIC yet */
			skip_nic_stats = true;
			break;
		}
		tx_qid_to_stats_idx[queue_id] = stats_idx;
	}
	/* walk TX rings */
	if (priv->tx) {
		for (ring = 0; ring < priv->tx_cfg.num_queues; ring++) {
			struct gve_tx_ring *tx = &priv->tx[ring];

			data[i++] = tx->req;
			data[i++] = tx->done;
			do {
				start =
				  u64_stats_fetch_begin(&priv->tx[ring].statss);
				tmp_tx_bytes = tx->bytes_done;
			} while (u64_stats_fetch_retry(&priv->tx[ring].statss,
						       start));
			data[i++] = tmp_tx_bytes;
			data[i++] = tx->wake_queue;
			data[i++] = tx->stop_queue;
			data[i++] = be32_to_cpu(gve_tx_load_event_counter(priv,
									  tx));
			data[i++] = tx->dma_mapping_error;
			/* stats from NIC */
			if (skip_nic_stats) {
				/* skip NIC tx stats */
				i += NIC_TX_STATS_REPORT_NUM;
				continue;
			}
			for (j = 0; j < NIC_TX_STATS_REPORT_NUM; j++) {
				u64 value =
				be64_to_cpu(report_stats[tx_qid_to_stats_idx[ring] + j].value);
				data[i++] = value;
			}
		}
	} else {
		i += priv->tx_cfg.num_queues * NUM_GVE_TX_CNTS;
	}

	kfree(rx_qid_to_stats_idx);
	kfree(tx_qid_to_stats_idx);
	/* AQ Stats */
	data[i++] = priv->adminq_prod_cnt;
	data[i++] = priv->adminq_cmd_fail;
	data[i++] = priv->adminq_timeouts;
	data[i++] = priv->adminq_describe_device_cnt;
	data[i++] = priv->adminq_cfg_device_resources_cnt;
	data[i++] = priv->adminq_register_page_list_cnt;
	data[i++] = priv->adminq_unregister_page_list_cnt;
	data[i++] = priv->adminq_create_tx_queue_cnt;
	data[i++] = priv->adminq_create_rx_queue_cnt;
	data[i++] = priv->adminq_destroy_tx_queue_cnt;
	data[i++] = priv->adminq_destroy_rx_queue_cnt;
	data[i++] = priv->adminq_dcfg_device_resources_cnt;
	data[i++] = priv->adminq_set_driver_parameter_cnt;
	data[i++] = priv->adminq_report_stats_cnt;
	data[i++] = priv->adminq_report_link_speed_cnt;
}

static void gve_get_channels(struct net_device *netdev,
			     struct ethtool_channels *cmd)
{
	struct gve_priv *priv = netdev_priv(netdev);

	cmd->max_rx = priv->rx_cfg.max_queues;
	cmd->max_tx = priv->tx_cfg.max_queues;
	cmd->max_other = 0;
	cmd->max_combined = 0;
	cmd->rx_count = priv->rx_cfg.num_queues;
	cmd->tx_count = priv->tx_cfg.num_queues;
	cmd->other_count = 0;
	cmd->combined_count = 0;
}

static int gve_set_channels(struct net_device *netdev,
			    struct ethtool_channels *cmd)
{
	struct gve_priv *priv = netdev_priv(netdev);
	struct gve_queue_config new_tx_cfg = priv->tx_cfg;
	struct gve_queue_config new_rx_cfg = priv->rx_cfg;
	struct ethtool_channels old_settings;
	int new_tx = cmd->tx_count;
	int new_rx = cmd->rx_count;

	gve_get_channels(netdev, &old_settings);

	/* Changing combined is not allowed allowed */
	if (cmd->combined_count != old_settings.combined_count)
		return -EINVAL;

	if (!new_rx || !new_tx)
		return -EINVAL;

	if (!netif_carrier_ok(netdev)) {
		priv->tx_cfg.num_queues = new_tx;
		priv->rx_cfg.num_queues = new_rx;
		return 0;
	}

	new_tx_cfg.num_queues = new_tx;
	new_rx_cfg.num_queues = new_rx;

	return gve_adjust_queues(priv, new_rx_cfg, new_tx_cfg);
}

static void gve_get_ringparam(struct net_device *netdev,
			      struct ethtool_ringparam *cmd)
{
	struct gve_priv *priv = netdev_priv(netdev);

	cmd->rx_max_pending = priv->rx_desc_cnt;
	cmd->tx_max_pending = priv->tx_desc_cnt;
	cmd->rx_pending = priv->rx_desc_cnt;
	cmd->tx_pending = priv->tx_desc_cnt;
}

static int gve_user_reset(struct net_device *netdev, u32 *flags)
{
	struct gve_priv *priv = netdev_priv(netdev);

	if (*flags == ETH_RESET_ALL) {
		*flags = 0;
		return gve_reset(priv, true);
	}

	return -EOPNOTSUPP;
}

static int gve_get_tunable(struct net_device *netdev,
			   const struct ethtool_tunable *etuna, void *value)
{
	struct gve_priv *priv = netdev_priv(netdev);

	switch (etuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		*(u32 *)value = priv->rx_copybreak;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int gve_set_tunable(struct net_device *netdev,
			   const struct ethtool_tunable *etuna,
			   const void *value)
{
	struct gve_priv *priv = netdev_priv(netdev);
	u32 len;

	switch (etuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		len = *(u32 *)value;
		if (len > PAGE_SIZE / 2)
			return -EINVAL;
		priv->rx_copybreak = len;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static u32 gve_get_priv_flags(struct net_device *netdev)
{
	struct gve_priv *priv = netdev_priv(netdev);
	u32 ret_flags = 0;

	/* Only 1 flag exists currently: report-stats (BIT(O)), so set that flag. */
	if (priv->ethtool_flags & BIT(0))
		ret_flags |= BIT(0);
	return ret_flags;
}

static int gve_set_priv_flags(struct net_device *netdev, u32 flags)
{
	struct gve_priv *priv = netdev_priv(netdev);
	u64 ori_flags, new_flags;

	ori_flags = READ_ONCE(priv->ethtool_flags);
	new_flags = ori_flags;

	/* Only one priv flag exists: report-stats (BIT(0))*/
	if (flags & BIT(0))
		new_flags |= BIT(0);
	else
		new_flags &= ~(BIT(0));
	priv->ethtool_flags = new_flags;
	/* start report-stats timer when user turns report stats on. */
	if (flags & BIT(0)) {
		mod_timer(&priv->stats_report_timer,
			  round_jiffies(jiffies +
					msecs_to_jiffies(priv->stats_report_timer_period)));
	}
	/* Zero off gve stats when report-stats turned off and */
	/* delete report stats timer. */
	if (!(flags & BIT(0)) && (ori_flags & BIT(0))) {
		int tx_stats_num = GVE_TX_STATS_REPORT_NUM *
			priv->tx_cfg.num_queues;
		int rx_stats_num = GVE_RX_STATS_REPORT_NUM *
			priv->rx_cfg.num_queues;

		memset(priv->stats_report->stats, 0, (tx_stats_num + rx_stats_num) *
				   sizeof(struct stats));
		del_timer_sync(&priv->stats_report_timer);
	}
	return 0;
}

static int gve_get_link_ksettings(struct net_device *netdev,
				  struct ethtool_link_ksettings *cmd)
{
	struct gve_priv *priv = netdev_priv(netdev);
	int err = gve_adminq_report_link_speed(priv);

	cmd->base.speed = priv->link_speed;
	return err;
}

const struct ethtool_ops gve_ethtool_ops = {
	.get_drvinfo = gve_get_drvinfo,
	.get_strings = gve_get_strings,
	.get_sset_count = gve_get_sset_count,
	.get_ethtool_stats = gve_get_ethtool_stats,
	.set_msglevel = gve_set_msglevel,
	.get_msglevel = gve_get_msglevel,
	.set_channels = gve_set_channels,
	.get_channels = gve_get_channels,
	.get_link = ethtool_op_get_link,
	.get_ringparam = gve_get_ringparam,
	.reset = gve_user_reset,
	.get_tunable = gve_get_tunable,
	.set_tunable = gve_set_tunable,
	.get_priv_flags = gve_get_priv_flags,
	.set_priv_flags = gve_set_priv_flags,
	.get_link_ksettings = gve_get_link_ksettings
};
