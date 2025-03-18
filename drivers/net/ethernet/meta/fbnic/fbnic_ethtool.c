// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <net/ipv6.h>

#include "fbnic.h"
#include "fbnic_netdev.h"
#include "fbnic_tlv.h"

struct fbnic_stat {
	u8 string[ETH_GSTRING_LEN];
	unsigned int size;
	unsigned int offset;
};

#define FBNIC_STAT_FIELDS(type, name, stat) { \
	.string = name, \
	.size = sizeof_field(struct type, stat), \
	.offset = offsetof(struct type, stat), \
}

/* Hardware statistics not captured in rtnl_link_stats */
#define FBNIC_HW_STAT(name, stat) \
	FBNIC_STAT_FIELDS(fbnic_hw_stats, name, stat)

static const struct fbnic_stat fbnic_gstrings_hw_stats[] = {
	/* RPC */
	FBNIC_HW_STAT("rpc_unkn_etype", rpc.unkn_etype),
	FBNIC_HW_STAT("rpc_unkn_ext_hdr", rpc.unkn_ext_hdr),
	FBNIC_HW_STAT("rpc_ipv4_frag", rpc.ipv4_frag),
	FBNIC_HW_STAT("rpc_ipv6_frag", rpc.ipv6_frag),
	FBNIC_HW_STAT("rpc_ipv4_esp", rpc.ipv4_esp),
	FBNIC_HW_STAT("rpc_ipv6_esp", rpc.ipv6_esp),
	FBNIC_HW_STAT("rpc_tcp_opt_err", rpc.tcp_opt_err),
	FBNIC_HW_STAT("rpc_out_of_hdr_err", rpc.out_of_hdr_err),
};

#define FBNIC_HW_FIXED_STATS_LEN ARRAY_SIZE(fbnic_gstrings_hw_stats)
#define FBNIC_HW_STATS_LEN	FBNIC_HW_FIXED_STATS_LEN

static void
fbnic_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_dev *fbd = fbn->fbd;

	fbnic_get_fw_ver_commit_str(fbd, drvinfo->fw_version,
				    sizeof(drvinfo->fw_version));
}

static int fbnic_get_regs_len(struct net_device *netdev)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	return fbnic_csr_regs_len(fbn->fbd) * sizeof(u32);
}

static void fbnic_get_regs(struct net_device *netdev,
			   struct ethtool_regs *regs, void *data)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	fbnic_csr_get_regs(fbn->fbd, data, &regs->version);
}

static struct fbnic_net *fbnic_clone_create(struct fbnic_net *orig)
{
	struct fbnic_net *clone;

	clone = kmemdup(orig, sizeof(*orig), GFP_KERNEL);
	if (!clone)
		return NULL;

	memset(clone->tx, 0, sizeof(clone->tx));
	memset(clone->rx, 0, sizeof(clone->rx));
	memset(clone->napi, 0, sizeof(clone->napi));
	return clone;
}

static void fbnic_clone_swap_cfg(struct fbnic_net *orig,
				 struct fbnic_net *clone)
{
	swap(clone->rcq_size, orig->rcq_size);
	swap(clone->hpq_size, orig->hpq_size);
	swap(clone->ppq_size, orig->ppq_size);
	swap(clone->txq_size, orig->txq_size);
	swap(clone->num_rx_queues, orig->num_rx_queues);
	swap(clone->num_tx_queues, orig->num_tx_queues);
	swap(clone->num_napi, orig->num_napi);
}

static void fbnic_aggregate_vector_counters(struct fbnic_net *fbn,
					    struct fbnic_napi_vector *nv)
{
	int i, j;

	for (i = 0; i < nv->txt_count; i++) {
		fbnic_aggregate_ring_tx_counters(fbn, &nv->qt[i].sub0);
		fbnic_aggregate_ring_tx_counters(fbn, &nv->qt[i].sub1);
		fbnic_aggregate_ring_tx_counters(fbn, &nv->qt[i].cmpl);
	}

	for (j = 0; j < nv->rxt_count; j++, i++) {
		fbnic_aggregate_ring_rx_counters(fbn, &nv->qt[i].sub0);
		fbnic_aggregate_ring_rx_counters(fbn, &nv->qt[i].sub1);
		fbnic_aggregate_ring_rx_counters(fbn, &nv->qt[i].cmpl);
	}
}

static void fbnic_clone_swap(struct fbnic_net *orig,
			     struct fbnic_net *clone)
{
	struct fbnic_dev *fbd = orig->fbd;
	unsigned int i;

	for (i = 0; i < max(clone->num_napi, orig->num_napi); i++)
		fbnic_synchronize_irq(fbd, FBNIC_NON_NAPI_VECTORS + i);
	for (i = 0; i < orig->num_napi; i++)
		fbnic_aggregate_vector_counters(orig, orig->napi[i]);

	fbnic_clone_swap_cfg(orig, clone);

	for (i = 0; i < ARRAY_SIZE(orig->napi); i++)
		swap(clone->napi[i], orig->napi[i]);
	for (i = 0; i < ARRAY_SIZE(orig->tx); i++)
		swap(clone->tx[i], orig->tx[i]);
	for (i = 0; i < ARRAY_SIZE(orig->rx); i++)
		swap(clone->rx[i], orig->rx[i]);
}

static void fbnic_clone_free(struct fbnic_net *clone)
{
	kfree(clone);
}

static int fbnic_get_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *ec,
			      struct kernel_ethtool_coalesce *kernel_coal,
			      struct netlink_ext_ack *extack)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	ec->tx_coalesce_usecs = fbn->tx_usecs;
	ec->rx_coalesce_usecs = fbn->rx_usecs;
	ec->rx_max_coalesced_frames = fbn->rx_max_frames;

	return 0;
}

static int fbnic_set_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *ec,
			      struct kernel_ethtool_coalesce *kernel_coal,
			      struct netlink_ext_ack *extack)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	/* Verify against hardware limits */
	if (ec->rx_coalesce_usecs > FIELD_MAX(FBNIC_INTR_CQ_REARM_RCQ_TIMEOUT)) {
		NL_SET_ERR_MSG_MOD(extack, "rx_usecs is above device max");
		return -EINVAL;
	}
	if (ec->tx_coalesce_usecs > FIELD_MAX(FBNIC_INTR_CQ_REARM_TCQ_TIMEOUT)) {
		NL_SET_ERR_MSG_MOD(extack, "tx_usecs is above device max");
		return -EINVAL;
	}
	if (ec->rx_max_coalesced_frames >
	    FIELD_MAX(FBNIC_QUEUE_RIM_THRESHOLD_RCD_MASK) /
	    FBNIC_MIN_RXD_PER_FRAME) {
		NL_SET_ERR_MSG_MOD(extack, "rx_frames is above device max");
		return -EINVAL;
	}

	fbn->tx_usecs = ec->tx_coalesce_usecs;
	fbn->rx_usecs = ec->rx_coalesce_usecs;
	fbn->rx_max_frames = ec->rx_max_coalesced_frames;

	if (netif_running(netdev)) {
		int i;

		for (i = 0; i < fbn->num_napi; i++) {
			struct fbnic_napi_vector *nv = fbn->napi[i];

			fbnic_config_txrx_usecs(nv, 0);
			fbnic_config_rx_frames(nv);
		}
	}

	return 0;
}

static void
fbnic_get_ringparam(struct net_device *netdev, struct ethtool_ringparam *ring,
		    struct kernel_ethtool_ringparam *kernel_ring,
		    struct netlink_ext_ack *extack)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	ring->rx_max_pending = FBNIC_QUEUE_SIZE_MAX;
	ring->rx_mini_max_pending = FBNIC_QUEUE_SIZE_MAX;
	ring->rx_jumbo_max_pending = FBNIC_QUEUE_SIZE_MAX;
	ring->tx_max_pending = FBNIC_QUEUE_SIZE_MAX;

	ring->rx_pending = fbn->rcq_size;
	ring->rx_mini_pending = fbn->hpq_size;
	ring->rx_jumbo_pending = fbn->ppq_size;
	ring->tx_pending = fbn->txq_size;
}

static void fbnic_set_rings(struct fbnic_net *fbn,
			    struct ethtool_ringparam *ring)
{
	fbn->rcq_size = ring->rx_pending;
	fbn->hpq_size = ring->rx_mini_pending;
	fbn->ppq_size = ring->rx_jumbo_pending;
	fbn->txq_size = ring->tx_pending;
}

static int
fbnic_set_ringparam(struct net_device *netdev, struct ethtool_ringparam *ring,
		    struct kernel_ethtool_ringparam *kernel_ring,
		    struct netlink_ext_ack *extack)

{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_net *clone;
	int err;

	ring->rx_pending	= roundup_pow_of_two(ring->rx_pending);
	ring->rx_mini_pending	= roundup_pow_of_two(ring->rx_mini_pending);
	ring->rx_jumbo_pending	= roundup_pow_of_two(ring->rx_jumbo_pending);
	ring->tx_pending	= roundup_pow_of_two(ring->tx_pending);

	/* These are absolute minimums allowing the device and driver to operate
	 * but not necessarily guarantee reasonable performance. Settings below
	 * Rx queue size of 128 and BDQs smaller than 64 are likely suboptimal
	 * at best.
	 */
	if (ring->rx_pending < max(FBNIC_QUEUE_SIZE_MIN, FBNIC_RX_DESC_MIN) ||
	    ring->rx_mini_pending < FBNIC_QUEUE_SIZE_MIN ||
	    ring->rx_jumbo_pending < FBNIC_QUEUE_SIZE_MIN ||
	    ring->tx_pending < max(FBNIC_QUEUE_SIZE_MIN, FBNIC_TX_DESC_MIN)) {
		NL_SET_ERR_MSG_MOD(extack, "requested ring size too small");
		return -EINVAL;
	}

	if (!netif_running(netdev)) {
		fbnic_set_rings(fbn, ring);
		return 0;
	}

	clone = fbnic_clone_create(fbn);
	if (!clone)
		return -ENOMEM;

	fbnic_set_rings(clone, ring);

	err = fbnic_alloc_napi_vectors(clone);
	if (err)
		goto err_free_clone;

	err = fbnic_alloc_resources(clone);
	if (err)
		goto err_free_napis;

	fbnic_down_noidle(fbn);
	err = fbnic_wait_all_queues_idle(fbn->fbd, true);
	if (err)
		goto err_start_stack;

	err = fbnic_set_netif_queues(clone);
	if (err)
		goto err_start_stack;

	/* Nothing can fail past this point */
	fbnic_flush(fbn);

	fbnic_clone_swap(fbn, clone);

	fbnic_up(fbn);

	fbnic_free_resources(clone);
	fbnic_free_napi_vectors(clone);
	fbnic_clone_free(clone);

	return 0;

err_start_stack:
	fbnic_flush(fbn);
	fbnic_up(fbn);
	fbnic_free_resources(clone);
err_free_napis:
	fbnic_free_napi_vectors(clone);
err_free_clone:
	fbnic_clone_free(clone);
	return err;
}

static void fbnic_get_strings(struct net_device *dev, u32 sset, u8 *data)
{
	int i;

	switch (sset) {
	case ETH_SS_STATS:
		for (i = 0; i < FBNIC_HW_STATS_LEN; i++)
			ethtool_puts(&data, fbnic_gstrings_hw_stats[i].string);
		break;
	}
}

static void fbnic_get_ethtool_stats(struct net_device *dev,
				    struct ethtool_stats *stats, u64 *data)
{
	struct fbnic_net *fbn = netdev_priv(dev);
	const struct fbnic_stat *stat;
	int i;

	fbnic_get_hw_stats(fbn->fbd);

	for (i = 0; i < FBNIC_HW_STATS_LEN; i++) {
		stat = &fbnic_gstrings_hw_stats[i];
		data[i] = *(u64 *)((u8 *)&fbn->fbd->hw_stats + stat->offset);
	}
}

static int fbnic_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return FBNIC_HW_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static int fbnic_get_rss_hash_idx(u32 flow_type)
{
	switch (flow_type & ~(FLOW_EXT | FLOW_MAC_EXT | FLOW_RSS)) {
	case TCP_V4_FLOW:
		return FBNIC_TCP4_HASH_OPT;
	case TCP_V6_FLOW:
		return FBNIC_TCP6_HASH_OPT;
	case UDP_V4_FLOW:
		return FBNIC_UDP4_HASH_OPT;
	case UDP_V6_FLOW:
		return FBNIC_UDP6_HASH_OPT;
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case AH_ESP_V4_FLOW:
	case SCTP_V4_FLOW:
	case IPV4_FLOW:
	case IPV4_USER_FLOW:
		return FBNIC_IPV4_HASH_OPT;
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case AH_ESP_V6_FLOW:
	case SCTP_V6_FLOW:
	case IPV6_FLOW:
	case IPV6_USER_FLOW:
		return FBNIC_IPV6_HASH_OPT;
	case ETHER_FLOW:
		return FBNIC_ETHER_HASH_OPT;
	}

	return -1;
}

static int
fbnic_get_rss_hash_opts(struct fbnic_net *fbn, struct ethtool_rxnfc *cmd)
{
	int hash_opt_idx = fbnic_get_rss_hash_idx(cmd->flow_type);

	if (hash_opt_idx < 0)
		return -EINVAL;

	/* Report options from rss_en table in fbn */
	cmd->data = fbn->rss_flow_hash[hash_opt_idx];

	return 0;
}

static int fbnic_get_cls_rule_all(struct fbnic_net *fbn,
				  struct ethtool_rxnfc *cmd,
				  u32 *rule_locs)
{
	struct fbnic_dev *fbd = fbn->fbd;
	int i, cnt = 0;

	/* Report maximum rule count */
	cmd->data = FBNIC_RPC_ACT_TBL_NFC_ENTRIES;

	for (i = 0; i < FBNIC_RPC_ACT_TBL_NFC_ENTRIES; i++) {
		int idx = i + FBNIC_RPC_ACT_TBL_NFC_OFFSET;
		struct fbnic_act_tcam *act_tcam;

		act_tcam = &fbd->act_tcam[idx];
		if (act_tcam->state != FBNIC_TCAM_S_VALID)
			continue;

		if (rule_locs) {
			if (cnt == cmd->rule_cnt)
				return -EMSGSIZE;

			rule_locs[cnt] = i;
		}

		cnt++;
	}

	return cnt;
}

static int fbnic_get_cls_rule(struct fbnic_net *fbn, struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp;
	struct fbnic_dev *fbd = fbn->fbd;
	struct fbnic_act_tcam *act_tcam;
	int idx;

	fsp = (struct ethtool_rx_flow_spec *)&cmd->fs;

	if (fsp->location >= FBNIC_RPC_ACT_TBL_NFC_ENTRIES)
		return -EINVAL;

	idx = fsp->location + FBNIC_RPC_ACT_TBL_NFC_OFFSET;
	act_tcam = &fbd->act_tcam[idx];

	if (act_tcam->state != FBNIC_TCAM_S_VALID)
		return -EINVAL;

	/* Report maximum rule count */
	cmd->data = FBNIC_RPC_ACT_TBL_NFC_ENTRIES;

	/* Set flow type field */
	if (!(act_tcam->value.tcam[1] & FBNIC_RPC_TCAM_ACT1_IP_VALID)) {
		fsp->flow_type = ETHER_FLOW;
		if (!FIELD_GET(FBNIC_RPC_TCAM_ACT1_L2_MACDA_IDX,
			       act_tcam->mask.tcam[1])) {
			struct fbnic_mac_addr *mac_addr;

			idx = FIELD_GET(FBNIC_RPC_TCAM_ACT1_L2_MACDA_IDX,
					act_tcam->value.tcam[1]);
			mac_addr = &fbd->mac_addr[idx];

			ether_addr_copy(fsp->h_u.ether_spec.h_dest,
					mac_addr->value.addr8);
			eth_broadcast_addr(fsp->m_u.ether_spec.h_dest);
		}
	} else if (act_tcam->value.tcam[1] &
		   FBNIC_RPC_TCAM_ACT1_OUTER_IP_VALID) {
		fsp->flow_type = IPV6_USER_FLOW;
		fsp->h_u.usr_ip6_spec.l4_proto = IPPROTO_IPV6;
		fsp->m_u.usr_ip6_spec.l4_proto = 0xff;

		if (!FIELD_GET(FBNIC_RPC_TCAM_ACT0_OUTER_IPSRC_IDX,
			       act_tcam->mask.tcam[0])) {
			struct fbnic_ip_addr *ip_addr;
			int i;

			idx = FIELD_GET(FBNIC_RPC_TCAM_ACT0_OUTER_IPSRC_IDX,
					act_tcam->value.tcam[0]);
			ip_addr = &fbd->ipo_src[idx];

			for (i = 0; i < 4; i++) {
				fsp->h_u.usr_ip6_spec.ip6src[i] =
					ip_addr->value.s6_addr32[i];
				fsp->m_u.usr_ip6_spec.ip6src[i] =
					~ip_addr->mask.s6_addr32[i];
			}
		}

		if (!FIELD_GET(FBNIC_RPC_TCAM_ACT0_OUTER_IPDST_IDX,
			       act_tcam->mask.tcam[0])) {
			struct fbnic_ip_addr *ip_addr;
			int i;

			idx = FIELD_GET(FBNIC_RPC_TCAM_ACT0_OUTER_IPDST_IDX,
					act_tcam->value.tcam[0]);
			ip_addr = &fbd->ipo_dst[idx];

			for (i = 0; i < 4; i++) {
				fsp->h_u.usr_ip6_spec.ip6dst[i] =
					ip_addr->value.s6_addr32[i];
				fsp->m_u.usr_ip6_spec.ip6dst[i] =
					~ip_addr->mask.s6_addr32[i];
			}
		}
	} else if ((act_tcam->value.tcam[1] & FBNIC_RPC_TCAM_ACT1_IP_IS_V6)) {
		if (act_tcam->value.tcam[1] & FBNIC_RPC_TCAM_ACT1_L4_VALID) {
			if (act_tcam->value.tcam[1] &
			    FBNIC_RPC_TCAM_ACT1_L4_IS_UDP)
				fsp->flow_type = UDP_V6_FLOW;
			else
				fsp->flow_type = TCP_V6_FLOW;
			fsp->h_u.tcp_ip6_spec.psrc =
				cpu_to_be16(act_tcam->value.tcam[3]);
			fsp->m_u.tcp_ip6_spec.psrc =
				cpu_to_be16(~act_tcam->mask.tcam[3]);
			fsp->h_u.tcp_ip6_spec.pdst =
				cpu_to_be16(act_tcam->value.tcam[4]);
			fsp->m_u.tcp_ip6_spec.pdst =
				cpu_to_be16(~act_tcam->mask.tcam[4]);
		} else {
			fsp->flow_type = IPV6_USER_FLOW;
		}

		if (!FIELD_GET(FBNIC_RPC_TCAM_ACT0_IPSRC_IDX,
			       act_tcam->mask.tcam[0])) {
			struct fbnic_ip_addr *ip_addr;
			int i;

			idx = FIELD_GET(FBNIC_RPC_TCAM_ACT0_IPSRC_IDX,
					act_tcam->value.tcam[0]);
			ip_addr = &fbd->ip_src[idx];

			for (i = 0; i < 4; i++) {
				fsp->h_u.usr_ip6_spec.ip6src[i] =
					ip_addr->value.s6_addr32[i];
				fsp->m_u.usr_ip6_spec.ip6src[i] =
					~ip_addr->mask.s6_addr32[i];
			}
		}

		if (!FIELD_GET(FBNIC_RPC_TCAM_ACT0_IPDST_IDX,
			       act_tcam->mask.tcam[0])) {
			struct fbnic_ip_addr *ip_addr;
			int i;

			idx = FIELD_GET(FBNIC_RPC_TCAM_ACT0_IPDST_IDX,
					act_tcam->value.tcam[0]);
			ip_addr = &fbd->ip_dst[idx];

			for (i = 0; i < 4; i++) {
				fsp->h_u.usr_ip6_spec.ip6dst[i] =
					ip_addr->value.s6_addr32[i];
				fsp->m_u.usr_ip6_spec.ip6dst[i] =
					~ip_addr->mask.s6_addr32[i];
			}
		}
	} else {
		if (act_tcam->value.tcam[1] & FBNIC_RPC_TCAM_ACT1_L4_VALID) {
			if (act_tcam->value.tcam[1] &
			    FBNIC_RPC_TCAM_ACT1_L4_IS_UDP)
				fsp->flow_type = UDP_V4_FLOW;
			else
				fsp->flow_type = TCP_V4_FLOW;
			fsp->h_u.tcp_ip4_spec.psrc =
				cpu_to_be16(act_tcam->value.tcam[3]);
			fsp->m_u.tcp_ip4_spec.psrc =
				cpu_to_be16(~act_tcam->mask.tcam[3]);
			fsp->h_u.tcp_ip4_spec.pdst =
				cpu_to_be16(act_tcam->value.tcam[4]);
			fsp->m_u.tcp_ip4_spec.pdst =
				cpu_to_be16(~act_tcam->mask.tcam[4]);
		} else {
			fsp->flow_type = IPV4_USER_FLOW;
			fsp->h_u.usr_ip4_spec.ip_ver = ETH_RX_NFC_IP4;
		}

		if (!FIELD_GET(FBNIC_RPC_TCAM_ACT0_IPSRC_IDX,
			       act_tcam->mask.tcam[0])) {
			struct fbnic_ip_addr *ip_addr;

			idx = FIELD_GET(FBNIC_RPC_TCAM_ACT0_IPSRC_IDX,
					act_tcam->value.tcam[0]);
			ip_addr = &fbd->ip_src[idx];

			fsp->h_u.usr_ip4_spec.ip4src =
				ip_addr->value.s6_addr32[3];
			fsp->m_u.usr_ip4_spec.ip4src =
				~ip_addr->mask.s6_addr32[3];
		}

		if (!FIELD_GET(FBNIC_RPC_TCAM_ACT0_IPDST_IDX,
			       act_tcam->mask.tcam[0])) {
			struct fbnic_ip_addr *ip_addr;

			idx = FIELD_GET(FBNIC_RPC_TCAM_ACT0_IPDST_IDX,
					act_tcam->value.tcam[0]);
			ip_addr = &fbd->ip_dst[idx];

			fsp->h_u.usr_ip4_spec.ip4dst =
				ip_addr->value.s6_addr32[3];
			fsp->m_u.usr_ip4_spec.ip4dst =
				~ip_addr->mask.s6_addr32[3];
		}
	}

	/* Record action */
	if (act_tcam->dest & FBNIC_RPC_ACT_TBL0_DROP)
		fsp->ring_cookie = RX_CLS_FLOW_DISC;
	else if (act_tcam->dest & FBNIC_RPC_ACT_TBL0_Q_SEL)
		fsp->ring_cookie = FIELD_GET(FBNIC_RPC_ACT_TBL0_Q_ID,
					     act_tcam->dest);
	else
		fsp->flow_type |= FLOW_RSS;

	cmd->rss_context = FIELD_GET(FBNIC_RPC_ACT_TBL0_RSS_CTXT_ID,
				     act_tcam->dest);

	return 0;
}

static int fbnic_get_rxnfc(struct net_device *netdev,
			   struct ethtool_rxnfc *cmd, u32 *rule_locs)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	int ret = -EOPNOTSUPP;
	u32 special = 0;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = fbn->num_rx_queues;
		ret = 0;
		break;
	case ETHTOOL_GRXFH:
		ret = fbnic_get_rss_hash_opts(fbn, cmd);
		break;
	case ETHTOOL_GRXCLSRULE:
		ret = fbnic_get_cls_rule(fbn, cmd);
		break;
	case ETHTOOL_GRXCLSRLCNT:
		rule_locs = NULL;
		special = RX_CLS_LOC_SPECIAL;
		fallthrough;
	case ETHTOOL_GRXCLSRLALL:
		ret = fbnic_get_cls_rule_all(fbn, cmd, rule_locs);
		if (ret < 0)
			break;

		cmd->data |= special;
		cmd->rule_cnt = ret;
		ret = 0;
		break;
	}

	return ret;
}

#define FBNIC_L2_HASH_OPTIONS \
	(RXH_L2DA | RXH_DISCARD)
#define FBNIC_L3_HASH_OPTIONS \
	(FBNIC_L2_HASH_OPTIONS | RXH_IP_SRC | RXH_IP_DST)
#define FBNIC_L4_HASH_OPTIONS \
	(FBNIC_L3_HASH_OPTIONS | RXH_L4_B_0_1 | RXH_L4_B_2_3)

static int
fbnic_set_rss_hash_opts(struct fbnic_net *fbn, const struct ethtool_rxnfc *cmd)
{
	int hash_opt_idx;

	/* Verify the type requested is correct */
	hash_opt_idx = fbnic_get_rss_hash_idx(cmd->flow_type);
	if (hash_opt_idx < 0)
		return -EINVAL;

	/* Verify the fields asked for can actually be assigned based on type */
	if (cmd->data & ~FBNIC_L4_HASH_OPTIONS ||
	    (hash_opt_idx > FBNIC_L4_HASH_OPT &&
	     cmd->data & ~FBNIC_L3_HASH_OPTIONS) ||
	    (hash_opt_idx > FBNIC_IP_HASH_OPT &&
	     cmd->data & ~FBNIC_L2_HASH_OPTIONS))
		return -EINVAL;

	fbn->rss_flow_hash[hash_opt_idx] = cmd->data;

	if (netif_running(fbn->netdev)) {
		fbnic_rss_reinit(fbn->fbd, fbn);
		fbnic_write_rules(fbn->fbd);
	}

	return 0;
}

static int fbnic_cls_rule_any_loc(struct fbnic_dev *fbd)
{
	int i;

	for (i = FBNIC_RPC_ACT_TBL_NFC_ENTRIES; i--;) {
		int idx = i + FBNIC_RPC_ACT_TBL_NFC_OFFSET;

		if (fbd->act_tcam[idx].state != FBNIC_TCAM_S_VALID)
			return i;
	}

	return -ENOSPC;
}

static int fbnic_set_cls_rule_ins(struct fbnic_net *fbn,
				  const struct ethtool_rxnfc *cmd)
{
	u16 flow_value = 0, flow_mask = 0xffff, ip_value = 0, ip_mask = 0xffff;
	u16 sport = 0, sport_mask = ~0, dport = 0, dport_mask = ~0;
	u16 misc = 0, misc_mask = ~0;
	u32 dest = FIELD_PREP(FBNIC_RPC_ACT_TBL0_DEST_MASK,
			      FBNIC_RPC_ACT_TBL0_DEST_HOST);
	struct fbnic_ip_addr *ip_src = NULL, *ip_dst = NULL;
	struct fbnic_mac_addr *mac_addr = NULL;
	struct ethtool_rx_flow_spec *fsp;
	struct fbnic_dev *fbd = fbn->fbd;
	struct fbnic_act_tcam *act_tcam;
	struct in6_addr *addr6, *mask6;
	struct in_addr *addr4, *mask4;
	int hash_idx, location;
	u32 flow_type;
	int idx, j;

	fsp = (struct ethtool_rx_flow_spec *)&cmd->fs;

	if (fsp->location != RX_CLS_LOC_ANY)
		return -EINVAL;
	location = fbnic_cls_rule_any_loc(fbd);
	if (location < 0)
		return location;

	if (fsp->ring_cookie == RX_CLS_FLOW_DISC) {
		dest = FBNIC_RPC_ACT_TBL0_DROP;
	} else if (fsp->flow_type & FLOW_RSS) {
		if (cmd->rss_context == 1)
			dest |= FBNIC_RPC_ACT_TBL0_RSS_CTXT_ID;
	} else {
		u32 ring_idx = ethtool_get_flow_spec_ring(fsp->ring_cookie);

		if (ring_idx >= fbn->num_rx_queues)
			return -EINVAL;

		dest |= FBNIC_RPC_ACT_TBL0_Q_SEL |
			FIELD_PREP(FBNIC_RPC_ACT_TBL0_Q_ID, ring_idx);
	}

	idx = location + FBNIC_RPC_ACT_TBL_NFC_OFFSET;
	act_tcam = &fbd->act_tcam[idx];

	/* Do not allow overwriting for now.
	 * To support overwriting rules we will need to add logic to free
	 * any IP or MACDA TCAMs that may be associated with the old rule.
	 */
	if (act_tcam->state != FBNIC_TCAM_S_DISABLED)
		return -EBUSY;

	flow_type = fsp->flow_type & ~(FLOW_EXT | FLOW_RSS);
	hash_idx = fbnic_get_rss_hash_idx(flow_type);

	switch (flow_type) {
	case UDP_V4_FLOW:
udp4_flow:
		flow_value |= FBNIC_RPC_TCAM_ACT1_L4_IS_UDP;
		fallthrough;
	case TCP_V4_FLOW:
tcp4_flow:
		flow_value |= FBNIC_RPC_TCAM_ACT1_L4_VALID;
		flow_mask &= ~(FBNIC_RPC_TCAM_ACT1_L4_IS_UDP |
			       FBNIC_RPC_TCAM_ACT1_L4_VALID);

		sport = be16_to_cpu(fsp->h_u.tcp_ip4_spec.psrc);
		sport_mask = ~be16_to_cpu(fsp->m_u.tcp_ip4_spec.psrc);
		dport = be16_to_cpu(fsp->h_u.tcp_ip4_spec.pdst);
		dport_mask = ~be16_to_cpu(fsp->m_u.tcp_ip4_spec.pdst);
		goto ip4_flow;
	case IP_USER_FLOW:
		if (!fsp->m_u.usr_ip4_spec.proto)
			goto ip4_flow;
		if (fsp->m_u.usr_ip4_spec.proto != 0xff)
			return -EINVAL;
		if (fsp->h_u.usr_ip4_spec.proto == IPPROTO_UDP)
			goto udp4_flow;
		if (fsp->h_u.usr_ip4_spec.proto == IPPROTO_TCP)
			goto tcp4_flow;
		return -EINVAL;
ip4_flow:
		addr4 = (struct in_addr *)&fsp->h_u.usr_ip4_spec.ip4src;
		mask4 = (struct in_addr *)&fsp->m_u.usr_ip4_spec.ip4src;
		if (mask4->s_addr) {
			ip_src = __fbnic_ip4_sync(fbd, fbd->ip_src,
						  addr4, mask4);
			if (!ip_src)
				return -ENOSPC;

			set_bit(idx, ip_src->act_tcam);
			ip_value |= FBNIC_RPC_TCAM_ACT0_IPSRC_VALID |
				    FIELD_PREP(FBNIC_RPC_TCAM_ACT0_IPSRC_IDX,
					       ip_src - fbd->ip_src);
			ip_mask &= ~(FBNIC_RPC_TCAM_ACT0_IPSRC_VALID |
				     FBNIC_RPC_TCAM_ACT0_IPSRC_IDX);
		}

		addr4 = (struct in_addr *)&fsp->h_u.usr_ip4_spec.ip4dst;
		mask4 = (struct in_addr *)&fsp->m_u.usr_ip4_spec.ip4dst;
		if (mask4->s_addr) {
			ip_dst = __fbnic_ip4_sync(fbd, fbd->ip_dst,
						  addr4, mask4);
			if (!ip_dst) {
				if (ip_src && ip_src->state == FBNIC_TCAM_S_ADD)
					memset(ip_src, 0, sizeof(*ip_src));
				return -ENOSPC;
			}

			set_bit(idx, ip_dst->act_tcam);
			ip_value |= FBNIC_RPC_TCAM_ACT0_IPDST_VALID |
				    FIELD_PREP(FBNIC_RPC_TCAM_ACT0_IPDST_IDX,
					       ip_dst - fbd->ip_dst);
			ip_mask &= ~(FBNIC_RPC_TCAM_ACT0_IPDST_VALID |
				     FBNIC_RPC_TCAM_ACT0_IPDST_IDX);
		}
		flow_value |= FBNIC_RPC_TCAM_ACT1_IP_VALID |
			      FBNIC_RPC_TCAM_ACT1_L2_MACDA_VALID;
		flow_mask &= ~(FBNIC_RPC_TCAM_ACT1_IP_IS_V6 |
			       FBNIC_RPC_TCAM_ACT1_IP_VALID |
			       FBNIC_RPC_TCAM_ACT1_L2_MACDA_VALID);
		break;
	case UDP_V6_FLOW:
udp6_flow:
		flow_value |= FBNIC_RPC_TCAM_ACT1_L4_IS_UDP;
		fallthrough;
	case TCP_V6_FLOW:
tcp6_flow:
		flow_value |= FBNIC_RPC_TCAM_ACT1_L4_VALID;
		flow_mask &= ~(FBNIC_RPC_TCAM_ACT1_L4_IS_UDP |
			  FBNIC_RPC_TCAM_ACT1_L4_VALID);

		sport = be16_to_cpu(fsp->h_u.tcp_ip6_spec.psrc);
		sport_mask = ~be16_to_cpu(fsp->m_u.tcp_ip6_spec.psrc);
		dport = be16_to_cpu(fsp->h_u.tcp_ip6_spec.pdst);
		dport_mask = ~be16_to_cpu(fsp->m_u.tcp_ip6_spec.pdst);
		goto ipv6_flow;
	case IPV6_USER_FLOW:
		if (!fsp->m_u.usr_ip6_spec.l4_proto)
			goto ipv6_flow;

		if (fsp->m_u.usr_ip6_spec.l4_proto != 0xff)
			return -EINVAL;
		if (fsp->h_u.usr_ip6_spec.l4_proto == IPPROTO_UDP)
			goto udp6_flow;
		if (fsp->h_u.usr_ip6_spec.l4_proto == IPPROTO_TCP)
			goto tcp6_flow;
		if (fsp->h_u.usr_ip6_spec.l4_proto != IPPROTO_IPV6)
			return -EINVAL;

		addr6 = (struct in6_addr *)fsp->h_u.usr_ip6_spec.ip6src;
		mask6 = (struct in6_addr *)fsp->m_u.usr_ip6_spec.ip6src;
		if (!ipv6_addr_any(mask6)) {
			ip_src = __fbnic_ip6_sync(fbd, fbd->ipo_src,
						  addr6, mask6);
			if (!ip_src)
				return -ENOSPC;

			set_bit(idx, ip_src->act_tcam);
			ip_value |=
				FBNIC_RPC_TCAM_ACT0_OUTER_IPSRC_VALID |
				FIELD_PREP(FBNIC_RPC_TCAM_ACT0_OUTER_IPSRC_IDX,
					   ip_src - fbd->ipo_src);
			ip_mask &=
				~(FBNIC_RPC_TCAM_ACT0_OUTER_IPSRC_VALID |
				  FBNIC_RPC_TCAM_ACT0_OUTER_IPSRC_IDX);
		}

		addr6 = (struct in6_addr *)fsp->h_u.usr_ip6_spec.ip6dst;
		mask6 = (struct in6_addr *)fsp->m_u.usr_ip6_spec.ip6dst;
		if (!ipv6_addr_any(mask6)) {
			ip_dst = __fbnic_ip6_sync(fbd, fbd->ipo_dst,
						  addr6, mask6);
			if (!ip_dst) {
				if (ip_src && ip_src->state == FBNIC_TCAM_S_ADD)
					memset(ip_src, 0, sizeof(*ip_src));
				return -ENOSPC;
			}

			set_bit(idx, ip_dst->act_tcam);
			ip_value |=
				FBNIC_RPC_TCAM_ACT0_OUTER_IPDST_VALID |
				FIELD_PREP(FBNIC_RPC_TCAM_ACT0_OUTER_IPDST_IDX,
					   ip_dst - fbd->ipo_dst);
			ip_mask &= ~(FBNIC_RPC_TCAM_ACT0_OUTER_IPDST_VALID |
				     FBNIC_RPC_TCAM_ACT0_OUTER_IPDST_IDX);
		}

		flow_value |= FBNIC_RPC_TCAM_ACT1_OUTER_IP_VALID;
		flow_mask &= FBNIC_RPC_TCAM_ACT1_OUTER_IP_VALID;
ipv6_flow:
		addr6 = (struct in6_addr *)fsp->h_u.usr_ip6_spec.ip6src;
		mask6 = (struct in6_addr *)fsp->m_u.usr_ip6_spec.ip6src;
		if (!ip_src && !ipv6_addr_any(mask6)) {
			ip_src = __fbnic_ip6_sync(fbd, fbd->ip_src,
						  addr6, mask6);
			if (!ip_src)
				return -ENOSPC;

			set_bit(idx, ip_src->act_tcam);
			ip_value |= FBNIC_RPC_TCAM_ACT0_IPSRC_VALID |
				    FIELD_PREP(FBNIC_RPC_TCAM_ACT0_IPSRC_IDX,
					       ip_src - fbd->ip_src);
			ip_mask &= ~(FBNIC_RPC_TCAM_ACT0_IPSRC_VALID |
				       FBNIC_RPC_TCAM_ACT0_IPSRC_IDX);
		}

		addr6 = (struct in6_addr *)fsp->h_u.usr_ip6_spec.ip6dst;
		mask6 = (struct in6_addr *)fsp->m_u.usr_ip6_spec.ip6dst;
		if (!ip_dst && !ipv6_addr_any(mask6)) {
			ip_dst = __fbnic_ip6_sync(fbd, fbd->ip_dst,
						  addr6, mask6);
			if (!ip_dst) {
				if (ip_src && ip_src->state == FBNIC_TCAM_S_ADD)
					memset(ip_src, 0, sizeof(*ip_src));
				return -ENOSPC;
			}

			set_bit(idx, ip_dst->act_tcam);
			ip_value |= FBNIC_RPC_TCAM_ACT0_IPDST_VALID |
				    FIELD_PREP(FBNIC_RPC_TCAM_ACT0_IPDST_IDX,
					       ip_dst - fbd->ip_dst);
			ip_mask &= ~(FBNIC_RPC_TCAM_ACT0_IPDST_VALID |
				       FBNIC_RPC_TCAM_ACT0_IPDST_IDX);
		}

		flow_value |= FBNIC_RPC_TCAM_ACT1_IP_IS_V6 |
			      FBNIC_RPC_TCAM_ACT1_IP_VALID |
			      FBNIC_RPC_TCAM_ACT1_L2_MACDA_VALID;
		flow_mask &= ~(FBNIC_RPC_TCAM_ACT1_IP_IS_V6 |
			       FBNIC_RPC_TCAM_ACT1_IP_VALID |
			       FBNIC_RPC_TCAM_ACT1_L2_MACDA_VALID);
		break;
	case ETHER_FLOW:
		if (!is_zero_ether_addr(fsp->m_u.ether_spec.h_dest)) {
			u8 *addr = fsp->h_u.ether_spec.h_dest;
			u8 *mask = fsp->m_u.ether_spec.h_dest;

			/* Do not allow MAC addr of 0 */
			if (is_zero_ether_addr(addr))
				return -EINVAL;

			/* Only support full MAC address to avoid
			 * conflicts with other MAC addresses.
			 */
			if (!is_broadcast_ether_addr(mask))
				return -EINVAL;

			if (is_multicast_ether_addr(addr))
				mac_addr = __fbnic_mc_sync(fbd, addr);
			else
				mac_addr = __fbnic_uc_sync(fbd, addr);

			if (!mac_addr)
				return -ENOSPC;

			set_bit(idx, mac_addr->act_tcam);
			flow_value |=
				FIELD_PREP(FBNIC_RPC_TCAM_ACT1_L2_MACDA_IDX,
					   mac_addr - fbd->mac_addr);
			flow_mask &= ~FBNIC_RPC_TCAM_ACT1_L2_MACDA_IDX;
		}

		flow_value |= FBNIC_RPC_TCAM_ACT1_L2_MACDA_VALID;
		flow_mask &= ~FBNIC_RPC_TCAM_ACT1_L2_MACDA_VALID;
		break;
	default:
		return -EINVAL;
	}

	/* Write action table values */
	act_tcam->dest = dest;
	act_tcam->rss_en_mask = fbnic_flow_hash_2_rss_en_mask(fbn, hash_idx);

	/* Write IP Match value/mask to action_tcam[0] */
	act_tcam->value.tcam[0] = ip_value;
	act_tcam->mask.tcam[0] = ip_mask;

	/* Write flow type value/mask to action_tcam[1] */
	act_tcam->value.tcam[1] = flow_value;
	act_tcam->mask.tcam[1] = flow_mask;

	/* Write error, DSCP, extra L4 matches to action_tcam[2] */
	act_tcam->value.tcam[2] = misc;
	act_tcam->mask.tcam[2] = misc_mask;

	/* Write source/destination port values */
	act_tcam->value.tcam[3] = sport;
	act_tcam->mask.tcam[3] = sport_mask;
	act_tcam->value.tcam[4] = dport;
	act_tcam->mask.tcam[4] = dport_mask;

	for (j = 5; j < FBNIC_RPC_TCAM_ACT_WORD_LEN; j++)
		act_tcam->mask.tcam[j] = 0xffff;

	act_tcam->state = FBNIC_TCAM_S_UPDATE;
	fsp->location = location;

	if (netif_running(fbn->netdev)) {
		fbnic_write_rules(fbd);
		if (ip_src || ip_dst)
			fbnic_write_ip_addr(fbd);
		if (mac_addr)
			fbnic_write_macda(fbd);
	}

	return 0;
}

static void fbnic_clear_nfc_macda(struct fbnic_net *fbn,
				  unsigned int tcam_idx)
{
	struct fbnic_dev *fbd = fbn->fbd;
	int idx;

	for (idx = ARRAY_SIZE(fbd->mac_addr); idx--;)
		__fbnic_xc_unsync(&fbd->mac_addr[idx], tcam_idx);

	/* Write updates to hardware */
	if (netif_running(fbn->netdev))
		fbnic_write_macda(fbd);
}

static void fbnic_clear_nfc_ip_addr(struct fbnic_net *fbn,
				    unsigned int tcam_idx)
{
	struct fbnic_dev *fbd = fbn->fbd;
	int idx;

	for (idx = ARRAY_SIZE(fbd->ip_src); idx--;)
		__fbnic_ip_unsync(&fbd->ip_src[idx], tcam_idx);
	for (idx = ARRAY_SIZE(fbd->ip_dst); idx--;)
		__fbnic_ip_unsync(&fbd->ip_dst[idx], tcam_idx);
	for (idx = ARRAY_SIZE(fbd->ipo_src); idx--;)
		__fbnic_ip_unsync(&fbd->ipo_src[idx], tcam_idx);
	for (idx = ARRAY_SIZE(fbd->ipo_dst); idx--;)
		__fbnic_ip_unsync(&fbd->ipo_dst[idx], tcam_idx);

	/* Write updates to hardware */
	if (netif_running(fbn->netdev))
		fbnic_write_ip_addr(fbd);
}

static int fbnic_set_cls_rule_del(struct fbnic_net *fbn,
				  const struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp;
	struct fbnic_dev *fbd = fbn->fbd;
	struct fbnic_act_tcam *act_tcam;
	int idx;

	fsp = (struct ethtool_rx_flow_spec *)&cmd->fs;

	if (fsp->location >= FBNIC_RPC_ACT_TBL_NFC_ENTRIES)
		return -EINVAL;

	idx = fsp->location + FBNIC_RPC_ACT_TBL_NFC_OFFSET;
	act_tcam = &fbd->act_tcam[idx];

	if (act_tcam->state != FBNIC_TCAM_S_VALID)
		return -EINVAL;

	act_tcam->state = FBNIC_TCAM_S_DELETE;

	if ((act_tcam->value.tcam[1] & FBNIC_RPC_TCAM_ACT1_L2_MACDA_VALID) &&
	    (~act_tcam->mask.tcam[1] & FBNIC_RPC_TCAM_ACT1_L2_MACDA_IDX))
		fbnic_clear_nfc_macda(fbn, idx);

	if ((act_tcam->value.tcam[0] &
	     (FBNIC_RPC_TCAM_ACT0_IPSRC_VALID |
	      FBNIC_RPC_TCAM_ACT0_IPDST_VALID |
	      FBNIC_RPC_TCAM_ACT0_OUTER_IPSRC_VALID |
	      FBNIC_RPC_TCAM_ACT0_OUTER_IPDST_VALID)) &&
	    (~act_tcam->mask.tcam[0] &
	     (FBNIC_RPC_TCAM_ACT0_IPSRC_IDX |
	      FBNIC_RPC_TCAM_ACT0_IPDST_IDX |
	      FBNIC_RPC_TCAM_ACT0_OUTER_IPSRC_IDX |
	      FBNIC_RPC_TCAM_ACT0_OUTER_IPDST_IDX)))
		fbnic_clear_nfc_ip_addr(fbn, idx);

	if (netif_running(fbn->netdev))
		fbnic_write_rules(fbd);

	return 0;
}

static int fbnic_set_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *cmd)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_SRXFH:
		ret = fbnic_set_rss_hash_opts(fbn, cmd);
		break;
	case ETHTOOL_SRXCLSRLINS:
		ret = fbnic_set_cls_rule_ins(fbn, cmd);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		ret = fbnic_set_cls_rule_del(fbn, cmd);
		break;
	}

	return ret;
}

static u32 fbnic_get_rxfh_key_size(struct net_device *netdev)
{
	return FBNIC_RPC_RSS_KEY_BYTE_LEN;
}

static u32 fbnic_get_rxfh_indir_size(struct net_device *netdev)
{
	return FBNIC_RPC_RSS_TBL_SIZE;
}

static int
fbnic_get_rxfh(struct net_device *netdev, struct ethtool_rxfh_param *rxfh)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	unsigned int i;

	rxfh->hfunc = ETH_RSS_HASH_TOP;

	if (rxfh->key) {
		for (i = 0; i < FBNIC_RPC_RSS_KEY_BYTE_LEN; i++) {
			u32 rss_key = fbn->rss_key[i / 4] << ((i % 4) * 8);

			rxfh->key[i] = rss_key >> 24;
		}
	}

	if (rxfh->indir) {
		for (i = 0; i < FBNIC_RPC_RSS_TBL_SIZE; i++)
			rxfh->indir[i] = fbn->indir_tbl[0][i];
	}

	return 0;
}

static unsigned int
fbnic_set_indir(struct fbnic_net *fbn, unsigned int idx, const u32 *indir)
{
	unsigned int i, changes = 0;

	for (i = 0; i < FBNIC_RPC_RSS_TBL_SIZE; i++) {
		if (fbn->indir_tbl[idx][i] == indir[i])
			continue;

		fbn->indir_tbl[idx][i] = indir[i];
		changes++;
	}

	return changes;
}

static int
fbnic_set_rxfh(struct net_device *netdev, struct ethtool_rxfh_param *rxfh,
	       struct netlink_ext_ack *extack)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	unsigned int i, changes = 0;

	if (rxfh->hfunc != ETH_RSS_HASH_NO_CHANGE &&
	    rxfh->hfunc != ETH_RSS_HASH_TOP)
		return -EINVAL;

	if (rxfh->key) {
		u32 rss_key = 0;

		for (i = FBNIC_RPC_RSS_KEY_BYTE_LEN; i--;) {
			rss_key >>= 8;
			rss_key |= (u32)(rxfh->key[i]) << 24;

			if (i % 4)
				continue;

			if (fbn->rss_key[i / 4] == rss_key)
				continue;

			fbn->rss_key[i / 4] = rss_key;
			changes++;
		}
	}

	if (rxfh->indir)
		changes += fbnic_set_indir(fbn, 0, rxfh->indir);

	if (changes && netif_running(netdev))
		fbnic_rss_reinit_hw(fbn->fbd, fbn);

	return 0;
}

static int
fbnic_modify_rxfh_context(struct net_device *netdev,
			  struct ethtool_rxfh_context *ctx,
			  const struct ethtool_rxfh_param *rxfh,
			  struct netlink_ext_ack *extack)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	const u32 *indir = rxfh->indir;
	unsigned int changes;

	if (!indir)
		indir = ethtool_rxfh_context_indir(ctx);

	changes = fbnic_set_indir(fbn, rxfh->rss_context, indir);
	if (changes && netif_running(netdev))
		fbnic_rss_reinit_hw(fbn->fbd, fbn);

	return 0;
}

static int
fbnic_create_rxfh_context(struct net_device *netdev,
			  struct ethtool_rxfh_context *ctx,
			  const struct ethtool_rxfh_param *rxfh,
			  struct netlink_ext_ack *extack)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	if (rxfh->hfunc && rxfh->hfunc != ETH_RSS_HASH_TOP) {
		NL_SET_ERR_MSG_MOD(extack, "RSS hash function not supported");
		return -EOPNOTSUPP;
	}
	ctx->hfunc = ETH_RSS_HASH_TOP;

	if (!rxfh->indir) {
		u32 *indir = ethtool_rxfh_context_indir(ctx);
		unsigned int num_rx = fbn->num_rx_queues;
		unsigned int i;

		for (i = 0; i < FBNIC_RPC_RSS_TBL_SIZE; i++)
			indir[i] = ethtool_rxfh_indir_default(i, num_rx);
	}

	return fbnic_modify_rxfh_context(netdev, ctx, rxfh, extack);
}

static int
fbnic_remove_rxfh_context(struct net_device *netdev,
			  struct ethtool_rxfh_context *ctx, u32 rss_context,
			  struct netlink_ext_ack *extack)
{
	/* Nothing to do, contexts are allocated statically */
	return 0;
}

static void fbnic_get_channels(struct net_device *netdev,
			       struct ethtool_channels *ch)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_dev *fbd = fbn->fbd;

	ch->max_rx = fbd->max_num_queues;
	ch->max_tx = fbd->max_num_queues;
	ch->max_combined = min(ch->max_rx, ch->max_tx);
	ch->max_other =	FBNIC_NON_NAPI_VECTORS;

	if (fbn->num_rx_queues > fbn->num_napi ||
	    fbn->num_tx_queues > fbn->num_napi)
		ch->combined_count = min(fbn->num_rx_queues,
					 fbn->num_tx_queues);
	else
		ch->combined_count =
			fbn->num_rx_queues + fbn->num_tx_queues - fbn->num_napi;
	ch->rx_count = fbn->num_rx_queues - ch->combined_count;
	ch->tx_count = fbn->num_tx_queues - ch->combined_count;
	ch->other_count = FBNIC_NON_NAPI_VECTORS;
}

static void fbnic_set_queues(struct fbnic_net *fbn, struct ethtool_channels *ch,
			     unsigned int max_napis)
{
	fbn->num_rx_queues = ch->rx_count + ch->combined_count;
	fbn->num_tx_queues = ch->tx_count + ch->combined_count;
	fbn->num_napi = min(ch->rx_count + ch->tx_count + ch->combined_count,
			    max_napis);
}

static int fbnic_set_channels(struct net_device *netdev,
			      struct ethtool_channels *ch)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	unsigned int max_napis, standalone;
	struct fbnic_dev *fbd = fbn->fbd;
	struct fbnic_net *clone;
	int err;

	max_napis = fbd->num_irqs - FBNIC_NON_NAPI_VECTORS;
	standalone = ch->rx_count + ch->tx_count;

	/* Limits for standalone queues:
	 *  - each queue has it's own NAPI (num_napi >= rx + tx + combined)
	 *  - combining queues (combined not 0, rx or tx must be 0)
	 */
	if ((ch->rx_count && ch->tx_count && ch->combined_count) ||
	    (standalone && standalone + ch->combined_count > max_napis) ||
	    ch->rx_count + ch->combined_count > fbd->max_num_queues ||
	    ch->tx_count + ch->combined_count > fbd->max_num_queues ||
	    ch->other_count != FBNIC_NON_NAPI_VECTORS)
		return -EINVAL;

	if (!netif_running(netdev)) {
		fbnic_set_queues(fbn, ch, max_napis);
		fbnic_reset_indir_tbl(fbn);
		return 0;
	}

	clone = fbnic_clone_create(fbn);
	if (!clone)
		return -ENOMEM;

	fbnic_set_queues(clone, ch, max_napis);

	err = fbnic_alloc_napi_vectors(clone);
	if (err)
		goto err_free_clone;

	err = fbnic_alloc_resources(clone);
	if (err)
		goto err_free_napis;

	fbnic_down_noidle(fbn);
	err = fbnic_wait_all_queues_idle(fbn->fbd, true);
	if (err)
		goto err_start_stack;

	err = fbnic_set_netif_queues(clone);
	if (err)
		goto err_start_stack;

	/* Nothing can fail past this point */
	fbnic_flush(fbn);

	fbnic_clone_swap(fbn, clone);

	/* Reset RSS indirection table */
	fbnic_reset_indir_tbl(fbn);

	fbnic_up(fbn);

	fbnic_free_resources(clone);
	fbnic_free_napi_vectors(clone);
	fbnic_clone_free(clone);

	return 0;

err_start_stack:
	fbnic_flush(fbn);
	fbnic_up(fbn);
	fbnic_free_resources(clone);
err_free_napis:
	fbnic_free_napi_vectors(clone);
err_free_clone:
	fbnic_clone_free(clone);
	return err;
}

static int
fbnic_get_ts_info(struct net_device *netdev,
		  struct kernel_ethtool_ts_info *tsinfo)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	tsinfo->phc_index = ptp_clock_index(fbn->fbd->ptp);

	tsinfo->so_timestamping =
		SOF_TIMESTAMPING_TX_SOFTWARE |
		SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;

	tsinfo->tx_types =
		BIT(HWTSTAMP_TX_OFF) |
		BIT(HWTSTAMP_TX_ON);

	tsinfo->rx_filters =
		BIT(HWTSTAMP_FILTER_NONE) |
		BIT(HWTSTAMP_FILTER_PTP_V1_L4_EVENT) |
		BIT(HWTSTAMP_FILTER_PTP_V2_L4_EVENT) |
		BIT(HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
		BIT(HWTSTAMP_FILTER_PTP_V2_EVENT) |
		BIT(HWTSTAMP_FILTER_ALL);

	return 0;
}

static void fbnic_get_ts_stats(struct net_device *netdev,
			       struct ethtool_ts_stats *ts_stats)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	u64 ts_packets, ts_lost;
	struct fbnic_ring *ring;
	unsigned int start;
	int i;

	ts_stats->pkts = fbn->tx_stats.twq.ts_packets;
	ts_stats->lost = fbn->tx_stats.twq.ts_lost;
	for (i = 0; i < fbn->num_tx_queues; i++) {
		ring = fbn->tx[i];
		do {
			start = u64_stats_fetch_begin(&ring->stats.syncp);
			ts_packets = ring->stats.twq.ts_packets;
			ts_lost = ring->stats.twq.ts_lost;
		} while (u64_stats_fetch_retry(&ring->stats.syncp, start));
		ts_stats->pkts += ts_packets;
		ts_stats->lost += ts_lost;
	}
}

static void fbnic_set_counter(u64 *stat, struct fbnic_stat_counter *counter)
{
	if (counter->reported)
		*stat = counter->value;
}

static void
fbnic_get_eth_mac_stats(struct net_device *netdev,
			struct ethtool_eth_mac_stats *eth_mac_stats)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_mac_stats *mac_stats;
	struct fbnic_dev *fbd = fbn->fbd;
	const struct fbnic_mac *mac;

	mac_stats = &fbd->hw_stats.mac;
	mac = fbd->mac;

	mac->get_eth_mac_stats(fbd, false, &mac_stats->eth_mac);

	fbnic_set_counter(&eth_mac_stats->FramesTransmittedOK,
			  &mac_stats->eth_mac.FramesTransmittedOK);
	fbnic_set_counter(&eth_mac_stats->FramesReceivedOK,
			  &mac_stats->eth_mac.FramesReceivedOK);
	fbnic_set_counter(&eth_mac_stats->FrameCheckSequenceErrors,
			  &mac_stats->eth_mac.FrameCheckSequenceErrors);
	fbnic_set_counter(&eth_mac_stats->AlignmentErrors,
			  &mac_stats->eth_mac.AlignmentErrors);
	fbnic_set_counter(&eth_mac_stats->OctetsTransmittedOK,
			  &mac_stats->eth_mac.OctetsTransmittedOK);
	fbnic_set_counter(&eth_mac_stats->FramesLostDueToIntMACXmitError,
			  &mac_stats->eth_mac.FramesLostDueToIntMACXmitError);
	fbnic_set_counter(&eth_mac_stats->OctetsReceivedOK,
			  &mac_stats->eth_mac.OctetsReceivedOK);
	fbnic_set_counter(&eth_mac_stats->FramesLostDueToIntMACRcvError,
			  &mac_stats->eth_mac.FramesLostDueToIntMACRcvError);
	fbnic_set_counter(&eth_mac_stats->MulticastFramesXmittedOK,
			  &mac_stats->eth_mac.MulticastFramesXmittedOK);
	fbnic_set_counter(&eth_mac_stats->BroadcastFramesXmittedOK,
			  &mac_stats->eth_mac.BroadcastFramesXmittedOK);
	fbnic_set_counter(&eth_mac_stats->MulticastFramesReceivedOK,
			  &mac_stats->eth_mac.MulticastFramesReceivedOK);
	fbnic_set_counter(&eth_mac_stats->BroadcastFramesReceivedOK,
			  &mac_stats->eth_mac.BroadcastFramesReceivedOK);
	fbnic_set_counter(&eth_mac_stats->FrameTooLongErrors,
			  &mac_stats->eth_mac.FrameTooLongErrors);
}

static const struct ethtool_ops fbnic_ethtool_ops = {
	.supported_coalesce_params	=
				  ETHTOOL_COALESCE_USECS |
				  ETHTOOL_COALESCE_RX_MAX_FRAMES,
	.rxfh_max_num_contexts	= FBNIC_RPC_RSS_TBL_COUNT,
	.get_drvinfo		= fbnic_get_drvinfo,
	.get_regs_len		= fbnic_get_regs_len,
	.get_regs		= fbnic_get_regs,
	.get_coalesce		= fbnic_get_coalesce,
	.set_coalesce		= fbnic_set_coalesce,
	.get_ringparam		= fbnic_get_ringparam,
	.set_ringparam		= fbnic_set_ringparam,
	.get_strings		= fbnic_get_strings,
	.get_ethtool_stats	= fbnic_get_ethtool_stats,
	.get_sset_count		= fbnic_get_sset_count,
	.get_rxnfc		= fbnic_get_rxnfc,
	.set_rxnfc		= fbnic_set_rxnfc,
	.get_rxfh_key_size	= fbnic_get_rxfh_key_size,
	.get_rxfh_indir_size	= fbnic_get_rxfh_indir_size,
	.get_rxfh		= fbnic_get_rxfh,
	.set_rxfh		= fbnic_set_rxfh,
	.create_rxfh_context	= fbnic_create_rxfh_context,
	.modify_rxfh_context	= fbnic_modify_rxfh_context,
	.remove_rxfh_context	= fbnic_remove_rxfh_context,
	.get_channels		= fbnic_get_channels,
	.set_channels		= fbnic_set_channels,
	.get_ts_info		= fbnic_get_ts_info,
	.get_ts_stats		= fbnic_get_ts_stats,
	.get_eth_mac_stats	= fbnic_get_eth_mac_stats,
};

void fbnic_set_ethtool_ops(struct net_device *dev)
{
	dev->ethtool_ops = &fbnic_ethtool_ops;
}
