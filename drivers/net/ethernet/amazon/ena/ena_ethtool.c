/*
 * Copyright 2015 Amazon.com, Inc. or its affiliates.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/pci.h>

#include "ena_netdev.h"

struct ena_stats {
	char name[ETH_GSTRING_LEN];
	int stat_offset;
};

#define ENA_STAT_ENA_COM_ENTRY(stat) { \
	.name = #stat, \
	.stat_offset = offsetof(struct ena_com_stats_admin, stat) \
}

#define ENA_STAT_ENTRY(stat, stat_type) { \
	.name = #stat, \
	.stat_offset = offsetof(struct ena_stats_##stat_type, stat) \
}

#define ENA_STAT_RX_ENTRY(stat) \
	ENA_STAT_ENTRY(stat, rx)

#define ENA_STAT_TX_ENTRY(stat) \
	ENA_STAT_ENTRY(stat, tx)

#define ENA_STAT_GLOBAL_ENTRY(stat) \
	ENA_STAT_ENTRY(stat, dev)

static const struct ena_stats ena_stats_global_strings[] = {
	ENA_STAT_GLOBAL_ENTRY(tx_timeout),
	ENA_STAT_GLOBAL_ENTRY(suspend),
	ENA_STAT_GLOBAL_ENTRY(resume),
	ENA_STAT_GLOBAL_ENTRY(wd_expired),
	ENA_STAT_GLOBAL_ENTRY(interface_up),
	ENA_STAT_GLOBAL_ENTRY(interface_down),
	ENA_STAT_GLOBAL_ENTRY(admin_q_pause),
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
};

static const struct ena_stats ena_stats_rx_strings[] = {
	ENA_STAT_RX_ENTRY(cnt),
	ENA_STAT_RX_ENTRY(bytes),
	ENA_STAT_RX_ENTRY(rx_copybreak_pkt),
	ENA_STAT_RX_ENTRY(csum_good),
	ENA_STAT_RX_ENTRY(refil_partial),
	ENA_STAT_RX_ENTRY(bad_csum),
	ENA_STAT_RX_ENTRY(page_alloc_fail),
	ENA_STAT_RX_ENTRY(skb_alloc_fail),
	ENA_STAT_RX_ENTRY(dma_mapping_err),
	ENA_STAT_RX_ENTRY(bad_desc_num),
	ENA_STAT_RX_ENTRY(bad_req_id),
	ENA_STAT_RX_ENTRY(empty_rx_ring),
	ENA_STAT_RX_ENTRY(csum_unchecked),
};

static const struct ena_stats ena_stats_ena_com_strings[] = {
	ENA_STAT_ENA_COM_ENTRY(aborted_cmd),
	ENA_STAT_ENA_COM_ENTRY(submitted_cmd),
	ENA_STAT_ENA_COM_ENTRY(completed_cmd),
	ENA_STAT_ENA_COM_ENTRY(out_of_space),
	ENA_STAT_ENA_COM_ENTRY(no_completion),
};

#define ENA_STATS_ARRAY_GLOBAL	ARRAY_SIZE(ena_stats_global_strings)
#define ENA_STATS_ARRAY_TX	ARRAY_SIZE(ena_stats_tx_strings)
#define ENA_STATS_ARRAY_RX	ARRAY_SIZE(ena_stats_rx_strings)
#define ENA_STATS_ARRAY_ENA_COM	ARRAY_SIZE(ena_stats_ena_com_strings)

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

	for (i = 0; i < adapter->num_queues; i++) {
		/* Tx stats */
		ring = &adapter->tx_ring[i];

		for (j = 0; j < ENA_STATS_ARRAY_TX; j++) {
			ena_stats = &ena_stats_tx_strings[j];

			ptr = (u64 *)((uintptr_t)&ring->tx_stats +
				(uintptr_t)ena_stats->stat_offset);

			ena_safe_update_stat(ptr, (*data)++, &ring->syncp);
		}

		/* Rx stats */
		ring = &adapter->rx_ring[i];

		for (j = 0; j < ENA_STATS_ARRAY_RX; j++) {
			ena_stats = &ena_stats_rx_strings[j];

			ptr = (u64 *)((uintptr_t)&ring->rx_stats +
				(uintptr_t)ena_stats->stat_offset);

			ena_safe_update_stat(ptr, (*data)++, &ring->syncp);
		}
	}
}

static void ena_dev_admin_queue_stats(struct ena_adapter *adapter, u64 **data)
{
	const struct ena_stats *ena_stats;
	u32 *ptr;
	int i;

	for (i = 0; i < ENA_STATS_ARRAY_ENA_COM; i++) {
		ena_stats = &ena_stats_ena_com_strings[i];

		ptr = (u32 *)((uintptr_t)&adapter->ena_dev->admin_queue.stats +
			(uintptr_t)ena_stats->stat_offset);

		*(*data)++ = *ptr;
	}
}

static void ena_get_ethtool_stats(struct net_device *netdev,
				  struct ethtool_stats *stats,
				  u64 *data)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	const struct ena_stats *ena_stats;
	u64 *ptr;
	int i;

	for (i = 0; i < ENA_STATS_ARRAY_GLOBAL; i++) {
		ena_stats = &ena_stats_global_strings[i];

		ptr = (u64 *)((uintptr_t)&adapter->dev_stats +
			(uintptr_t)ena_stats->stat_offset);

		ena_safe_update_stat(ptr, data++, &adapter->syncp);
	}

	ena_queue_stats(adapter, &data);
	ena_dev_admin_queue_stats(adapter, &data);
}

int ena_get_sset_count(struct net_device *netdev, int sset)
{
	struct ena_adapter *adapter = netdev_priv(netdev);

	if (sset != ETH_SS_STATS)
		return -EOPNOTSUPP;

	return  adapter->num_queues * (ENA_STATS_ARRAY_TX + ENA_STATS_ARRAY_RX)
		+ ENA_STATS_ARRAY_GLOBAL + ENA_STATS_ARRAY_ENA_COM;
}

static void ena_queue_strings(struct ena_adapter *adapter, u8 **data)
{
	const struct ena_stats *ena_stats;
	int i, j;

	for (i = 0; i < adapter->num_queues; i++) {
		/* Tx stats */
		for (j = 0; j < ENA_STATS_ARRAY_TX; j++) {
			ena_stats = &ena_stats_tx_strings[j];

			snprintf(*data, ETH_GSTRING_LEN,
				 "queue_%u_tx_%s", i, ena_stats->name);
			 (*data) += ETH_GSTRING_LEN;
		}
		/* Rx stats */
		for (j = 0; j < ENA_STATS_ARRAY_RX; j++) {
			ena_stats = &ena_stats_rx_strings[j];

			snprintf(*data, ETH_GSTRING_LEN,
				 "queue_%u_rx_%s", i, ena_stats->name);
			(*data) += ETH_GSTRING_LEN;
		}
	}
}

static void ena_com_dev_strings(u8 **data)
{
	const struct ena_stats *ena_stats;
	int i;

	for (i = 0; i < ENA_STATS_ARRAY_ENA_COM; i++) {
		ena_stats = &ena_stats_ena_com_strings[i];

		snprintf(*data, ETH_GSTRING_LEN,
			 "ena_admin_q_%s", ena_stats->name);
		(*data) += ETH_GSTRING_LEN;
	}
}

static void ena_get_strings(struct net_device *netdev, u32 sset, u8 *data)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	const struct ena_stats *ena_stats;
	int i;

	if (sset != ETH_SS_STATS)
		return;

	for (i = 0; i < ENA_STATS_ARRAY_GLOBAL; i++) {
		ena_stats = &ena_stats_global_strings[i];

		memcpy(data, ena_stats->name, ETH_GSTRING_LEN);
		data += ETH_GSTRING_LEN;
	}

	ena_queue_strings(adapter, &data);
	ena_com_dev_strings(&data);
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
			    struct ethtool_coalesce *coalesce)
{
	struct ena_adapter *adapter = netdev_priv(net_dev);
	struct ena_com_dev *ena_dev = adapter->ena_dev;

	if (!ena_com_interrupt_moderation_supported(ena_dev)) {
		/* the devie doesn't support interrupt moderation */
		return -EOPNOTSUPP;
	}

	coalesce->tx_coalesce_usecs =
		ena_com_get_nonadaptive_moderation_interval_tx(ena_dev) *
			ena_dev->intr_delay_resolution;

	if (!ena_com_get_adaptive_moderation_enabled(ena_dev))
		coalesce->rx_coalesce_usecs =
			ena_com_get_nonadaptive_moderation_interval_rx(ena_dev)
			* ena_dev->intr_delay_resolution;

	coalesce->use_adaptive_rx_coalesce =
		ena_com_get_adaptive_moderation_enabled(ena_dev);

	return 0;
}

static void ena_update_tx_rings_intr_moderation(struct ena_adapter *adapter)
{
	unsigned int val;
	int i;

	val = ena_com_get_nonadaptive_moderation_interval_tx(adapter->ena_dev);

	for (i = 0; i < adapter->num_queues; i++)
		adapter->tx_ring[i].smoothed_interval = val;
}

static void ena_update_rx_rings_intr_moderation(struct ena_adapter *adapter)
{
	unsigned int val;
	int i;

	val = ena_com_get_nonadaptive_moderation_interval_rx(adapter->ena_dev);

	for (i = 0; i < adapter->num_queues; i++)
		adapter->rx_ring[i].smoothed_interval = val;
}

static int ena_set_coalesce(struct net_device *net_dev,
			    struct ethtool_coalesce *coalesce)
{
	struct ena_adapter *adapter = netdev_priv(net_dev);
	struct ena_com_dev *ena_dev = adapter->ena_dev;
	int rc;

	if (!ena_com_interrupt_moderation_supported(ena_dev)) {
		/* the devie doesn't support interrupt moderation */
		return -EOPNOTSUPP;
	}

	rc = ena_com_update_nonadaptive_moderation_interval_tx(ena_dev,
							       coalesce->tx_coalesce_usecs);
	if (rc)
		return rc;

	ena_update_tx_rings_intr_moderation(adapter);

	if (coalesce->use_adaptive_rx_coalesce) {
		if (!ena_com_get_adaptive_moderation_enabled(ena_dev))
			ena_com_enable_adaptive_moderation(ena_dev);
		return 0;
	}

	rc = ena_com_update_nonadaptive_moderation_interval_rx(ena_dev,
							       coalesce->rx_coalesce_usecs);
	if (rc)
		return rc;

	ena_update_rx_rings_intr_moderation(adapter);

	if (!coalesce->use_adaptive_rx_coalesce) {
		if (ena_com_get_adaptive_moderation_enabled(ena_dev))
			ena_com_disable_adaptive_moderation(ena_dev);
	}

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

	strlcpy(info->driver, DRV_MODULE_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_MODULE_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, pci_name(adapter->pdev),
		sizeof(info->bus_info));
}

static void ena_get_ringparam(struct net_device *netdev,
			      struct ethtool_ringparam *ring)
{
	struct ena_adapter *adapter = netdev_priv(netdev);

	ring->tx_max_pending = adapter->max_tx_ring_size;
	ring->rx_max_pending = adapter->max_rx_ring_size;
	ring->tx_pending = adapter->tx_ring[0].ring_size;
	ring->rx_pending = adapter->rx_ring[0].ring_size;
}

static int ena_set_ringparam(struct net_device *netdev,
			     struct ethtool_ringparam *ring)
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
		info->data = adapter->num_queues;
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

static int ena_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key,
			u8 *hfunc)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	enum ena_admin_hash_functions ena_func;
	u8 func;
	int rc;

	rc = ena_com_indirect_table_get(adapter->ena_dev, indir);
	if (rc)
		return rc;

	rc = ena_com_get_hash_function(adapter->ena_dev, &ena_func, key);
	if (rc)
		return rc;

	switch (ena_func) {
	case ENA_ADMIN_TOEPLITZ:
		func = ETH_RSS_HASH_TOP;
		break;
	case ENA_ADMIN_CRC32:
		func = ETH_RSS_HASH_XOR;
		break;
	default:
		netif_err(adapter, drv, netdev,
			  "Command parameter is not supported\n");
		return -EOPNOTSUPP;
	}

	if (hfunc)
		*hfunc = func;

	return rc;
}

static int ena_set_rxfh(struct net_device *netdev, const u32 *indir,
			const u8 *key, const u8 hfunc)
{
	struct ena_adapter *adapter = netdev_priv(netdev);
	struct ena_com_dev *ena_dev = adapter->ena_dev;
	enum ena_admin_hash_functions func;
	int rc, i;

	if (indir) {
		for (i = 0; i < ENA_RX_RSS_TABLE_SIZE; i++) {
			rc = ena_com_indirect_table_fill_entry(ena_dev,
							       i,
							       ENA_IO_RXQ_IDX(indir[i]));
			if (unlikely(rc)) {
				netif_err(adapter, drv, netdev,
					  "Cannot fill indirect table (index is too large)\n");
				return rc;
			}
		}

		rc = ena_com_indirect_table_set(ena_dev);
		if (rc) {
			netif_err(adapter, drv, netdev,
				  "Cannot set indirect table\n");
			return rc == -EPERM ? -EOPNOTSUPP : rc;
		}
	}

	switch (hfunc) {
	case ETH_RSS_HASH_TOP:
		func = ENA_ADMIN_TOEPLITZ;
		break;
	case ETH_RSS_HASH_XOR:
		func = ENA_ADMIN_CRC32;
		break;
	default:
		netif_err(adapter, drv, netdev, "Unsupported hfunc %d\n",
			  hfunc);
		return -EOPNOTSUPP;
	}

	if (key) {
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

	channels->max_rx = adapter->num_queues;
	channels->max_tx = adapter->num_queues;
	channels->max_other = 0;
	channels->max_combined = 0;
	channels->rx_count = adapter->num_queues;
	channels->tx_count = adapter->num_queues;
	channels->other_count = 0;
	channels->combined_count = 0;
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
	.get_strings		= ena_get_strings,
	.get_ethtool_stats      = ena_get_ethtool_stats,
	.get_rxnfc		= ena_get_rxnfc,
	.set_rxnfc		= ena_set_rxnfc,
	.get_rxfh_indir_size    = ena_get_rxfh_indir_size,
	.get_rxfh_key_size	= ena_get_rxfh_key_size,
	.get_rxfh		= ena_get_rxfh,
	.set_rxfh		= ena_set_rxfh,
	.get_channels		= ena_get_channels,
	.get_tunable		= ena_get_tunable,
	.set_tunable		= ena_set_tunable,
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

	strings_num = ena_get_sset_count(netdev, ETH_SS_STATS);
	if (strings_num <= 0) {
		netif_err(adapter, drv, netdev, "Can't get stats num\n");
		return;
	}

	strings_buf = devm_kcalloc(&adapter->pdev->dev,
				   ETH_GSTRING_LEN, strings_num,
				   GFP_ATOMIC);
	if (!strings_buf) {
		netif_err(adapter, drv, netdev,
			  "failed to alloc strings_buf\n");
		return;
	}

	data_buf = devm_kcalloc(&adapter->pdev->dev,
				strings_num, sizeof(u64),
				GFP_ATOMIC);
	if (!data_buf) {
		netif_err(adapter, drv, netdev,
			  "failed to allocate data buf\n");
		devm_kfree(&adapter->pdev->dev, strings_buf);
		return;
	}

	ena_get_strings(netdev, ETH_SS_STATS, strings_buf);
	ena_get_ethtool_stats(netdev, NULL, data_buf);

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
