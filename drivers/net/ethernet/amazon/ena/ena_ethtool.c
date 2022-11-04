// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright 2015-2020 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include <linux/ethtool.h>
#include <linux/pci.h>

#include "ena_netdev.h"

struct ena_stats {
	char name[ETH_GSTRING_LEN];
	int stat_offset;
};

#define ENA_STAT_ENA_COM_ENTRY(stat) { \
	.name = #stat, \
	.stat_offset = offsetof(struct ena_com_stats_admin, stat) / sizeof(u64) \
}

#define ENA_STAT_ENTRY(stat, stat_type) { \
	.name = #stat, \
	.stat_offset = offsetof(struct ena_stats_##stat_type, stat) / sizeof(u64) \
}

#define ENA_STAT_HW_ENTRY(stat, stat_type) { \
	.name = #stat, \
	.stat_offset = offsetof(struct ena_admin_##stat_type, stat) / sizeof(u64) \
}

#define ENA_STAT_RX_ENTRY(stat) \
	ENA_STAT_ENTRY(stat, rx)

#define ENA_STAT_TX_ENTRY(stat) \
	ENA_STAT_ENTRY(stat, tx)

#define ENA_STAT_GLOBAL_ENTRY(stat) \
	ENA_STAT_ENTRY(stat, dev)

#define ENA_STAT_ENI_ENTRY(stat) \
	ENA_STAT_HW_ENTRY(stat, eni_stats)

static const struct ena_stats ena_stats_global_strings[] = {
	ENA_STAT_GLOBAL_ENTRY(tx_timeout),
	ENA_STAT_GLOBAL_ENTRY(suspend),
	ENA_STAT_GLOBAL_ENTRY(resume),
	ENA_STAT_GLOBAL_ENTRY(wd_expired),
	ENA_STAT_GLOBAL_ENTRY(interface_up),
	ENA_STAT_GLOBAL_ENTRY(interface_down),
	ENA_STAT_GLOBAL_ENTRY(admin_q_pause),
};

static const struct ena_stats ena_stats_eni_strings[] = {
	ENA_STAT_ENI_ENTRY(bw_in_allowance_exceeded),
	ENA_STAT_ENI_ENTRY(bw_out_allowance_exceeded),
	ENA_STAT_ENI_ENTRY(pps_allowance_exceeded),
	ENA_STAT_ENI_ENTRY(conntrack_allowance_exceeded),
	ENA_STAT_ENI_ENTRY(linklocal_allowance_exceeded),
};

static const struct ena_stats ena_stats_tx_strings[] = {
	ENA_STAT_TX_ENTRY(cnt),
	ENA_STAT_TX_ENTRY(bytes),
	ENA_STAT_TX_ENTRY(queue_stop),
	ENA_STAT_TX_ENTRY(queue_wakeup),
	ENA_STAT_TX_ENTRY(dma_mapping_err),
	ENA_STAT_TX_ENTRY(linearize),
	ENA_STAT_TX_ENTRY(linearize_failed),
	ENA_STAT_TX_ENTRY(napi_comp),
	ENA_STAT_TX_ENTRY(tx_poll),
	ENA_STAT_TX_ENTRY(doorbells),
	ENA_STAT_TX_ENTRY(prepare_ctx_err),
	ENA_STAT_TX_ENTRY(bad_req_id),
	ENA_STAT_TX_ENTRY(llq_buffer_copy),
	ENA_STAT_TX_ENTRY(missed_tx),
	ENA_STAT_TX_ENTRY(unmask_interrupt),
};

static const struct ena_stats ena_stats_rx_strings[] = {
	ENA_STAT_RX_ENTRY(cnt),
	ENA_STAT_RX_ENTRY(bytes),
	ENA_STAT_RX_ENTRY(rx_copybreak_pkt),
	ENA_STAT_RX_ENTRY(csum_good),
	ENA_STAT_RX_ENTRY(refil_partial),
	ENA_STAT_RX_ENTRY(csum_bad),
	ENA_STAT_RX_ENTRY(page_alloc_fail),
	ENA_STAT_RX_ENTRY(skb_alloc_fail),
	ENA_STAT_RX_ENTRY(dma_mapping_err),
	ENA_STAT_RX_ENTRY(bad_desc_num),
	ENA_STAT_RX_ENTRY(bad_req_id),
	ENA_STAT_RX_ENTRY(empty_rx_ring),
	ENA_STAT_RX_ENTRY(csum_unchecked),
	ENA_STAT_RX_ENTRY(xdp_aborted),
	ENA_STAT_RX_ENTRY(xdp_drop),
	ENA_STAT_RX_ENTRY(xdp_pass),
	ENA_STAT_RX_ENTRY(xdp_tx),
	ENA_STAT_RX_ENTRY(xdp_invalid),
	ENA_STAT_RX_ENTRY(xdp_redirect),
};

static const struct ena_stats ena_stats_ena_com_strings[] = {
	ENA_STAT_ENA_COM_ENTRY(aborted_cmd),
	ENA_STAT_ENA_COM_ENTRY(submitted_cmd),
	ENA_STAT_ENA_COM_ENTRY(completed_cmd),
	ENA_STAT_ENA_COM_ENTRY(out_of_space),
	ENA_STAT_ENA_COM_ENTRY(no_completion),
};

#define ENA_STATS_ARRAY_GLOBAL		ARRAY_SIZE(ena_stats_global_strings)
#define ENA_STATS_ARRAY_TX		ARRAY_SIZE(ena_stats_tx_strings)
#define ENA_STATS_ARRAY_RX		ARRAY_SIZE(ena_stats_rx_strings)
#define ENA_STATS_ARRAY_ENA_COM		ARRAY_SIZE(ena_stats_ena_com_strings)
#define ENA_STATS_ARRAY_ENI(adapter)	ARRAY_SIZE(ena_stats_eni_strings)

static void ena_safe_update_stat(u64 *src, u64 *dst,
				 struct u64_stats_sync *syncp)
{
	unsigned int start;

	do {
		start = u64_stats_fetch_begin_irq(syncp);
		*(dst) = *src;
	} while (u64_stats_fetch_retry_irq(syncp, start));
}

static void ena_queue_stats(struct ena_adapter *adapter, u64 **data)
{
	const struct ena_stats *ena_stats;
	struct ena_ring *ring;

	u64 *ptr;
	int i, j;

	for (i = 0; i < adapter->num_io_queues + adapter->xdp_num_queues; i++) {
		/* Tx stats */
		ring = &adapter->tx_ring[i];

		for (j = 0; j < ENA_STATS_ARRAY_TX; j++) {
			ena_stats = &ena_stats_tx_strings[j];

			ptr = (u64 *)&ring->tx_stats + ena_stats->stat_offset;

			ena_safe_update_stat(ptr, (*data)++, &ring->syncp);
		}
		/* XDP TX queues don't have a RX queue counterpart */
		if (!ENA_IS_XDP_INDEX(adapter, i)) {
			/* Rx stats */
			ring = &adapter->rx_ring[i];

			for (j = 0; j < ENA_STATS_ARRAY_RX; j++) {
				ena_stats = &ena_stats_rx_strings[j];

				ptr = (u64 *)&ring->rx_stats +
					ena_stats->stat_offset;

				ena_safe_update_stat(ptr, (*data)++, &ring->syncp);
			}
		}
	}
}

static void ena_dev_admin_queue_stats(struct ena_adapter *adapter, u64 **data)
{
	const struct ena_stats *ena_stats;
	u64 *ptr;
	int i;

	for (i = 0; i < ENA_STATS_ARRAY_ENA_COM; i++) {
		ena_stats = &ena_stats_ena_com_strings[i];

		ptr = (u64 *)&adapter->ena_dev->admin_queue.stats +
			ena_stats->stat_offset;

		*(*data)++ = *ptr;
	}
}

static void ena_get_stats(struct ena_adapter *adapter,
			  u64 *data,
			  bool eni_stats_needed)
{
	const struct ena_stats *ena_stats;
	u64 *ptr;
	int i;

	for (i = 0; i < ENA_STATS_ARRAY_GLOBAL; i++) {
		ena_stats = &ena_stats_global_strings[i];

		ptr = (u64 *)&adapter->dev_stats + ena_stats->stat_offset;

		ena_safe_update_stat(ptr, data++, &adapter->syncp);
	}

	if (eni_stats_needed) {
		ena_update_hw_stats(adapter);
		for (i = 0; i < ENA_STATS_ARRAY_ENI(adapter); i++) {
			ena_stats = &ena_stats_eni_strings[i];

			ptr = (u64 *)&adapter->eni_stats +
				ena_stats->stat_offset;

			ena_safe_update_stat(ptr, data++, &adapter->syncp);
		}
	}

	ena_queue_stats(adapter, &data);
	ena_dev_admin_queue_stats(adapter, &data);
}

static void ena_get_ethtool_stats(struct net_device *netdev,
				  struct ethtool_stats *stats,
				  u64 *data)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	struct ena_com_dev *dev = adapter->ena_dev;

	ena_get_stats(adapter, data, ena_com_get_cap(dev, ENA_ADMIN_ENI_STATS));
}

static int ena_get_sw_stats_count(struct ena_adapter *adapter)
{
	return adapter->num_io_queues * (ENA_STATS_ARRAY_TX + ENA_STATS_ARRAY_RX)
		+ adapter->xdp_num_queues * ENA_STATS_ARRAY_TX
		+ ENA_STATS_ARRAY_GLOBAL + ENA_STATS_ARRAY_ENA_COM;
}

static int ena_get_hw_stats_count(struct ena_adapter *adapter)
{
	bool supported = ena_com_get_cap(adapter->ena_dev, ENA_ADMIN_ENI_STATS);

	return ENA_STATS_ARRAY_ENI(adapter) * supported;
}

int ena_get_sset_count(struct net_device *netdev, int sset)
{
	struct ena_adapter *adapter = netdev_priv(netdev);

	switch (sset) {
	case ETH_SS_STATS:
		return ena_get_sw_stats_count(adapter) +
		       ena_get_hw_stats_count(adapter);
	}

	return -EOPNOTSUPP;
}

static void ena_queue_strings(struct ena_adapter *adapter, u8 **data)
{
	const struct ena_stats *ena_stats;
	bool is_xdp;
	int i, j;

	for (i = 0; i < adapter->num_io_queues + adapter->xdp_num_queues; i++) {
		is_xdp = ENA_IS_XDP_INDEX(adapter, i);
		/* Tx stats */
		for (j = 0; j < ENA_STATS_ARRAY_TX; j++) {
			ena_stats = &ena_stats_tx_strings[j];

			ethtool_sprintf(data,
					"queue_%u_%s_%s", i,
					is_xdp ? "xdp_tx" : "tx",
					ena_stats->name);
		}

		if (!is_xdp) {
			/* RX stats, in XDP there isn't a RX queue
			 * counterpart
			 */
			for (j = 0; j < ENA_STATS_ARRAY_RX; j++) {
				ena_stats = &ena_stats_rx_strings[j];

				ethtool_sprintf(data,
						"queue_%u_rx_%s", i,
						ena_stats->name);
			}
		}
	}
}

static void ena_com_dev_strings(u8 **data)
{
	const struct ena_stats *ena_stats;
	int i;

	for (i = 0; i < ENA_STATS_ARRAY_ENA_COM; i++) {
		ena_stats = &ena_stats_ena_com_strings[i];

		ethtool_sprintf(data,
				"ena_admin_q_%s", ena_stats->name);
	}
}

static void ena_get_strings(struct ena_adapter *adapter,
			    u8 *data,
			    bool eni_stats_needed)
{
	const struct ena_stats *ena_stats;
	int i;

	for (i = 0; i < ENA_STATS_ARRAY_GLOBAL; i++) {
		ena_stats = &ena_stats_global_strings[i];
		ethtool_sprintf(&data, ena_stats->name);
	}

	if (eni_stats_needed) {
		for (i = 0; i < ENA_STATS_ARRAY_ENI(adapter); i++) {
			ena_stats = &ena_stats_eni_strings[i];
			ethtool_sprintf(&data, ena_stats->name);
		}
	}

	ena_queue_strings(adapter, &data);
	ena_com_dev_strings(&data);
}

static void ena_get_ethtool_strings(struct net_device *netdev,
				    u32 sset,
				    u8 *data)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	struct ena_com_dev *dev = adapter->ena_dev;

	switch (sset) {
	case ETH_SS_STATS:
		ena_get_strings(adapter, data, ena_com_get_cap(dev, ENA_ADMIN_ENI_STATS));
		break;
	}
}

static int ena_get_link_ksettings(struct net_device *netdev,
				  struct ethtool_link_ksettings *link_ksettings)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	struct ena_com_dev *ena_dev = adapter->ena_dev;
	struct ena_admin_get_feature_link_desc *link;
	struct ena_admin_get_feat_resp feat_resp;
	int rc;

	rc = ena_com_get_link_params(ena_dev, &feat_resp);
	if (rc)
		return rc;

	link = &feat_resp.u.link;
	link_ksettings->base.speed = link->speed;

	if (link->flags & ENA_ADMIN_GET_FEATURE_LINK_DESC_AUTONEG_MASK) {
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     supported, Autoneg);
	}

	link_ksettings->base.autoneg =
		(link->flags & ENA_ADMIN_GET_FEATURE_LINK_DESC_AUTONEG_MASK) ?
		AUTONEG_ENABLE : AUTONEG_DISABLE;

	link_ksettings->base.duplex = DUPLEX_FULL;

	return 0;
}

static int ena_get_coalesce(struct net_device *net_dev,
			    struct ethtool_coalesce *coalesce,
			    struct kernel_ethtool_coalesce *kernel_coal,
			    struct netlink_ext_ack *extack)
{
	struct ena_adapter *adapter = netdev_priv(net_dev);
	struct ena_com_dev *ena_dev = adapter->ena_dev;

	if (!ena_com_interrupt_moderation_supported(ena_dev))
		return -EOPNOTSUPP;

	coalesce->tx_coalesce_usecs =
		ena_com_get_nonadaptive_moderation_interval_tx(ena_dev) *
			ena_dev->intr_delay_resolution;

	coalesce->rx_coalesce_usecs =
		ena_com_get_nonadaptive_moderation_interval_rx(ena_dev)
		* ena_dev->intr_delay_resolution;

	coalesce->use_adaptive_rx_coalesce =
		ena_com_get_adaptive_moderation_enabled(ena_dev);

	return 0;
}

static void ena_update_tx_rings_nonadaptive_intr_moderation(struct ena_adapter *adapter)
{
	unsigned int val;
	int i;

	val = ena_com_get_nonadaptive_moderation_interval_tx(adapter->ena_dev);

	for (i = 0; i < adapter->num_io_queues; i++)
		adapter->tx_ring[i].smoothed_interval = val;
}

static void ena_update_rx_rings_nonadaptive_intr_moderation(struct ena_adapter *adapter)
{
	unsigned int val;
	int i;

	val = ena_com_get_nonadaptive_moderation_interval_rx(adapter->ena_dev);

	for (i = 0; i < adapter->num_io_queues; i++)
		adapter->rx_ring[i].smoothed_interval = val;
}

static int ena_set_coalesce(struct net_device *net_dev,
			    struct ethtool_coalesce *coalesce,
			    struct kernel_ethtool_coalesce *kernel_coal,
			    struct netlink_ext_ack *extack)
{
	struct ena_adapter *adapter = netdev_priv(net_dev);
	struct ena_com_dev *ena_dev = adapter->ena_dev;
	int rc;

	if (!ena_com_interrupt_moderation_supported(ena_dev))
		return -EOPNOTSUPP;

	rc = ena_com_update_nonadaptive_moderation_interval_tx(ena_dev,
							       coalesce->tx_coalesce_usecs);
	if (rc)
		return rc;

	ena_update_tx_rings_nonadaptive_intr_moderation(adapter);

	rc = ena_com_update_nonadaptive_moderation_interval_rx(ena_dev,
							       coalesce->rx_coalesce_usecs);
	if (rc)
		return rc;

	ena_update_rx_rings_nonadaptive_intr_moderation(adapter);

	if (coalesce->use_adaptive_rx_coalesce &&
	    !ena_com_get_adaptive_moderation_enabled(ena_dev))
		ena_com_enable_adaptive_moderation(ena_dev);

	if (!coalesce->use_adaptive_rx_coalesce &&
	    ena_com_get_adaptive_moderation_enabled(ena_dev))
		ena_com_disable_adaptive_moderation(ena_dev);

	return 0;
}

static u32 ena_get_msglevel(struct net_device *netdev)
{
	struct ena_adapter *adapter = netdev_priv(netdev);

	return adapter->msg_enable;
}

static void ena_set_msglevel(struct net_device *netdev, u32 value)
{
	struct ena_adapter *adapter = netdev_priv(netdev);

	adapter->msg_enable = value;
}

static void ena_get_drvinfo(struct net_device *dev,
			    struct ethtool_drvinfo *info)
{
	struct ena_adapter *adapter = netdev_priv(dev);

	strscpy(info->driver, DRV_MODULE_NAME, sizeof(info->driver));
	strscpy(info->bus_info, pci_name(adapter->pdev),
		sizeof(info->bus_info));
}

static void ena_get_ringparam(struct net_device *netdev,
			      struct ethtool_ringparam *ring,
			      struct kernel_ethtool_ringparam *kernel_ring,
			      struct netlink_ext_ack *extack)
{
	struct ena_adapter *adapter = netdev_priv(netdev);

	ring->tx_max_pending = adapter->max_tx_ring_size;
	ring->rx_max_pending = adapter->max_rx_ring_size;
	ring->tx_pending = adapter->tx_ring[0].ring_size;
	ring->rx_pending = adapter->rx_ring[0].ring_size;
}

static int ena_set_ringparam(struct net_device *netdev,
			     struct ethtool_ringparam *ring,
			     struct kernel_ethtool_ringparam *kernel_ring,
			     struct netlink_ext_ack *extack)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	u32 new_tx_size, new_rx_size;

	new_tx_size = ring->tx_pending < ENA_MIN_RING_SIZE ?
			ENA_MIN_RING_SIZE : ring->tx_pending;
	new_tx_size = rounddown_pow_of_two(new_tx_size);

	new_rx_size = ring->rx_pending < ENA_MIN_RING_SIZE ?
			ENA_MIN_RING_SIZE : ring->rx_pending;
	new_rx_size = rounddown_pow_of_two(new_rx_size);

	if (new_tx_size == adapter->requested_tx_ring_size &&
	    new_rx_size == adapter->requested_rx_ring_size)
		return 0;

	return ena_update_queue_sizes(adapter, new_tx_size, new_rx_size);
}

static u32 ena_flow_hash_to_flow_type(u16 hash_fields)
{
	u32 data = 0;

	if (hash_fields & ENA_ADMIN_RSS_L2_DA)
		data |= RXH_L2DA;

	if (hash_fields & ENA_ADMIN_RSS_L3_DA)
		data |= RXH_IP_DST;

	if (hash_fields & ENA_ADMIN_RSS_L3_SA)
		data |= RXH_IP_SRC;

	if (hash_fields & ENA_ADMIN_RSS_L4_DP)
		data |= RXH_L4_B_2_3;

	if (hash_fields & ENA_ADMIN_RSS_L4_SP)
		data |= RXH_L4_B_0_1;

	return data;
}

static u16 ena_flow_data_to_flow_hash(u32 hash_fields)
{
	u16 data = 0;

	if (hash_fields & RXH_L2DA)
		data |= ENA_ADMIN_RSS_L2_DA;

	if (hash_fields & RXH_IP_DST)
		data |= ENA_ADMIN_RSS_L3_DA;

	if (hash_fields & RXH_IP_SRC)
		data |= ENA_ADMIN_RSS_L3_SA;

	if (hash_fields & RXH_L4_B_2_3)
		data |= ENA_ADMIN_RSS_L4_DP;

	if (hash_fields & RXH_L4_B_0_1)
		data |= ENA_ADMIN_RSS_L4_SP;

	return data;
}

static int ena_get_rss_hash(struct ena_com_dev *ena_dev,
			    struct ethtool_rxnfc *cmd)
{
	enum ena_admin_flow_hash_proto proto;
	u16 hash_fields;
	int rc;

	cmd->data = 0;

	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
		proto = ENA_ADMIN_RSS_TCP4;
		break;
	case UDP_V4_FLOW:
		proto = ENA_ADMIN_RSS_UDP4;
		break;
	case TCP_V6_FLOW:
		proto = ENA_ADMIN_RSS_TCP6;
		break;
	case UDP_V6_FLOW:
		proto = ENA_ADMIN_RSS_UDP6;
		break;
	case IPV4_FLOW:
		proto = ENA_ADMIN_RSS_IP4;
		break;
	case IPV6_FLOW:
		proto = ENA_ADMIN_RSS_IP6;
		break;
	case ETHER_FLOW:
		proto = ENA_ADMIN_RSS_NOT_IP;
		break;
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case SCTP_V4_FLOW:
	case AH_ESP_V4_FLOW:
		return -EOPNOTSUPP;
	default:
		return -EINVAL;
	}

	rc = ena_com_get_hash_ctrl(ena_dev, proto, &hash_fields);
	if (rc)
		return rc;

	cmd->data = ena_flow_hash_to_flow_type(hash_fields);

	return 0;
}

static int ena_set_rss_hash(struct ena_com_dev *ena_dev,
			    struct ethtool_rxnfc *cmd)
{
	enum ena_admin_flow_hash_proto proto;
	u16 hash_fields;

	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
		proto = ENA_ADMIN_RSS_TCP4;
		break;
	case UDP_V4_FLOW:
		proto = ENA_ADMIN_RSS_UDP4;
		break;
	case TCP_V6_FLOW:
		proto = ENA_ADMIN_RSS_TCP6;
		break;
	case UDP_V6_FLOW:
		proto = ENA_ADMIN_RSS_UDP6;
		break;
	case IPV4_FLOW:
		proto = ENA_ADMIN_RSS_IP4;
		break;
	case IPV6_FLOW:
		proto = ENA_ADMIN_RSS_IP6;
		break;
	case ETHER_FLOW:
		proto = ENA_ADMIN_RSS_NOT_IP;
		break;
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case SCTP_V4_FLOW:
	case AH_ESP_V4_FLOW:
		return -EOPNOTSUPP;
	default:
		return -EINVAL;
	}

	hash_fields = ena_flow_data_to_flow_hash(cmd->data);

	return ena_com_fill_hash_ctrl(ena_dev, proto, hash_fields);
}

static int ena_set_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *info)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	int rc = 0;

	switch (info->cmd) {
	case ETHTOOL_SRXFH:
		rc = ena_set_rss_hash(adapter->ena_dev, info);
		break;
	case ETHTOOL_SRXCLSRLDEL:
	case ETHTOOL_SRXCLSRLINS:
	default:
		netif_err(adapter, drv, netdev,
			  "Command parameter %d is not supported\n", info->cmd);
		rc = -EOPNOTSUPP;
	}

	return rc;
}

static int ena_get_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *info,
			 u32 *rules)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	int rc = 0;

	switch (info->cmd) {
	case ETHTOOL_GRXRINGS:
		info->data = adapter->num_io_queues;
		rc = 0;
		break;
	case ETHTOOL_GRXFH:
		rc = ena_get_rss_hash(adapter->ena_dev, info);
		break;
	case ETHTOOL_GRXCLSRLCNT:
	case ETHTOOL_GRXCLSRULE:
	case ETHTOOL_GRXCLSRLALL:
	default:
		netif_err(adapter, drv, netdev,
			  "Command parameter %d is not supported\n", info->cmd);
		rc = -EOPNOTSUPP;
	}

	return rc;
}

static u32 ena_get_rxfh_indir_size(struct net_device *netdev)
{
	return ENA_RX_RSS_TABLE_SIZE;
}

static u32 ena_get_rxfh_key_size(struct net_device *netdev)
{
	return ENA_HASH_KEY_SIZE;
}

static int ena_indirection_table_set(struct ena_adapter *adapter,
				     const u32 *indir)
{
	struct ena_com_dev *ena_dev = adapter->ena_dev;
	int i, rc;

	for (i = 0; i < ENA_RX_RSS_TABLE_SIZE; i++) {
		rc = ena_com_indirect_table_fill_entry(ena_dev,
						       i,
						       ENA_IO_RXQ_IDX(indir[i]));
		if (unlikely(rc)) {
			netif_err(adapter, drv, adapter->netdev,
				  "Cannot fill indirect table (index is too large)\n");
			return rc;
		}
	}

	rc = ena_com_indirect_table_set(ena_dev);
	if (rc) {
		netif_err(adapter, drv, adapter->netdev,
			  "Cannot set indirect table\n");
		return rc == -EPERM ? -EOPNOTSUPP : rc;
	}
	return rc;
}

static int ena_indirection_table_get(struct ena_adapter *adapter, u32 *indir)
{
	struct ena_com_dev *ena_dev = adapter->ena_dev;
	int i, rc;

	if (!indir)
		return 0;

	rc = ena_com_indirect_table_get(ena_dev, indir);
	if (rc)
		return rc;

	/* Our internal representation of the indices is: even indices
	 * for Tx and uneven indices for Rx. We need to convert the Rx
	 * indices to be consecutive
	 */
	for (i = 0; i < ENA_RX_RSS_TABLE_SIZE; i++)
		indir[i] = ENA_IO_RXQ_IDX_TO_COMBINED_IDX(indir[i]);

	return rc;
}

static int ena_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key,
			u8 *hfunc)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	enum ena_admin_hash_functions ena_func;
	u8 func;
	int rc;

	rc = ena_indirection_table_get(adapter, indir);
	if (rc)
		return rc;

	/* We call this function in order to check if the device
	 * supports getting/setting the hash function.
	 */
	rc = ena_com_get_hash_function(adapter->ena_dev, &ena_func);
	if (rc) {
		if (rc == -EOPNOTSUPP)
			rc = 0;

		return rc;
	}

	rc = ena_com_get_hash_key(adapter->ena_dev, key);
	if (rc)
		return rc;

	switch (ena_func) {
	case ENA_ADMIN_TOEPLITZ:
		func = ETH_RSS_HASH_TOP;
		break;
	case ENA_ADMIN_CRC32:
		func = ETH_RSS_HASH_CRC32;
		break;
	default:
		netif_err(adapter, drv, netdev,
			  "Command parameter is not supported\n");
		return -EOPNOTSUPP;
	}

	if (hfunc)
		*hfunc = func;

	return 0;
}

static int ena_set_rxfh(struct net_device *netdev, const u32 *indir,
			const u8 *key, const u8 hfunc)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	struct ena_com_dev *ena_dev = adapter->ena_dev;
	enum ena_admin_hash_functions func = 0;
	int rc;

	if (indir) {
		rc = ena_indirection_table_set(adapter, indir);
		if (rc)
			return rc;
	}

	switch (hfunc) {
	case ETH_RSS_HASH_NO_CHANGE:
		func = ena_com_get_current_hash_function(ena_dev);
		break;
	case ETH_RSS_HASH_TOP:
		func = ENA_ADMIN_TOEPLITZ;
		break;
	case ETH_RSS_HASH_CRC32:
		func = ENA_ADMIN_CRC32;
		break;
	default:
		netif_err(adapter, drv, netdev, "Unsupported hfunc %d\n",
			  hfunc);
		return -EOPNOTSUPP;
	}

	if (key || func) {
		rc = ena_com_fill_hash_function(ena_dev, func, key,
						ENA_HASH_KEY_SIZE,
						0xFFFFFFFF);
		if (unlikely(rc)) {
			netif_err(adapter, drv, netdev, "Cannot fill key\n");
			return rc == -EPERM ? -EOPNOTSUPP : rc;
		}
	}

	return 0;
}

static void ena_get_channels(struct net_device *netdev,
			     struct ethtool_channels *channels)
{
	struct ena_adapter *adapter = netdev_priv(netdev);

	channels->max_combined = adapter->max_num_io_queues;
	channels->combined_count = adapter->num_io_queues;
}

static int ena_set_channels(struct net_device *netdev,
			    struct ethtool_channels *channels)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	u32 count = channels->combined_count;
	/* The check for max value is already done in ethtool */
	if (count < ENA_MIN_NUM_IO_QUEUES ||
	    (ena_xdp_present(adapter) &&
	    !ena_xdp_legal_queue_count(adapter, count)))
		return -EINVAL;

	return ena_update_queue_count(adapter, count);
}

static int ena_get_tunable(struct net_device *netdev,
			   const struct ethtool_tunable *tuna, void *data)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	int ret = 0;

	switch (tuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		*(u32 *)data = adapter->rx_copybreak;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int ena_set_tunable(struct net_device *netdev,
			   const struct ethtool_tunable *tuna,
			   const void *data)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	int ret = 0;
	u32 len;

	switch (tuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		len = *(u32 *)data;
		if (len > adapter->netdev->mtu) {
			ret = -EINVAL;
			break;
		}
		adapter->rx_copybreak = len;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct ethtool_ops ena_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_USE_ADAPTIVE_RX,
	.get_link_ksettings	= ena_get_link_ksettings,
	.get_drvinfo		= ena_get_drvinfo,
	.get_msglevel		= ena_get_msglevel,
	.set_msglevel		= ena_set_msglevel,
	.get_link		= ethtool_op_get_link,
	.get_coalesce		= ena_get_coalesce,
	.set_coalesce		= ena_set_coalesce,
	.get_ringparam		= ena_get_ringparam,
	.set_ringparam		= ena_set_ringparam,
	.get_sset_count         = ena_get_sset_count,
	.get_strings		= ena_get_ethtool_strings,
	.get_ethtool_stats      = ena_get_ethtool_stats,
	.get_rxnfc		= ena_get_rxnfc,
	.set_rxnfc		= ena_set_rxnfc,
	.get_rxfh_indir_size    = ena_get_rxfh_indir_size,
	.get_rxfh_key_size	= ena_get_rxfh_key_size,
	.get_rxfh		= ena_get_rxfh,
	.set_rxfh		= ena_set_rxfh,
	.get_channels		= ena_get_channels,
	.set_channels		= ena_set_channels,
	.get_tunable		= ena_get_tunable,
	.set_tunable		= ena_set_tunable,
	.get_ts_info            = ethtool_op_get_ts_info,
};

void ena_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &ena_ethtool_ops;
}

static void ena_dump_stats_ex(struct ena_adapter *adapter, u8 *buf)
{
	struct net_device *netdev = adapter->netdev;
	u8 *strings_buf;
	u64 *data_buf;
	int strings_num;
	int i, rc;

	strings_num = ena_get_sw_stats_count(adapter);
	if (strings_num <= 0) {
		netif_err(adapter, drv, netdev, "Can't get stats num\n");
		return;
	}

	strings_buf = devm_kcalloc(&adapter->pdev->dev,
				   ETH_GSTRING_LEN, strings_num,
				   GFP_ATOMIC);
	if (!strings_buf) {
		netif_err(adapter, drv, netdev,
			  "Failed to allocate strings_buf\n");
		return;
	}

	data_buf = devm_kcalloc(&adapter->pdev->dev,
				strings_num, sizeof(u64),
				GFP_ATOMIC);
	if (!data_buf) {
		netif_err(adapter, drv, netdev,
			  "Failed to allocate data buf\n");
		devm_kfree(&adapter->pdev->dev, strings_buf);
		return;
	}

	ena_get_strings(adapter, strings_buf, false);
	ena_get_stats(adapter, data_buf, false);

	/* If there is a buffer, dump stats, otherwise print them to dmesg */
	if (buf)
		for (i = 0; i < strings_num; i++) {
			rc = snprintf(buf, ETH_GSTRING_LEN + sizeof(u64),
				      "%s %llu\n",
				      strings_buf + i * ETH_GSTRING_LEN,
				      data_buf[i]);
			buf += rc;
		}
	else
		for (i = 0; i < strings_num; i++)
			netif_err(adapter, drv, netdev, "%s: %llu\n",
				  strings_buf + i * ETH_GSTRING_LEN,
				  data_buf[i]);

	devm_kfree(&adapter->pdev->dev, strings_buf);
	devm_kfree(&adapter->pdev->dev, data_buf);
}

void ena_dump_stats_to_buf(struct ena_adapter *adapter, u8 *buf)
{
	if (!buf)
		return;

	ena_dump_stats_ex(adapter, buf);
}

void ena_dump_stats_to_dmesg(struct ena_adapter *adapter)
{
	ena_dump_stats_ex(adapter, NULL);
}
