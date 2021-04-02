// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2014-2016 Freescale Semiconductor Inc.
 * Copyright 2016 NXP
 * Copyright 2020 NXP
 */

#include <linux/net_tstamp.h>
#include <linux/nospec.h>

#include "dpni.h"	/* DPNI_LINK_OPT_* */
#include "dpaa2-eth.h"

/* To be kept in sync with DPNI statistics */
static char dpaa2_ethtool_stats[][ETH_GSTRING_LEN] = {
	"[hw] rx frames",
	"[hw] rx bytes",
	"[hw] rx mcast frames",
	"[hw] rx mcast bytes",
	"[hw] rx bcast frames",
	"[hw] rx bcast bytes",
	"[hw] tx frames",
	"[hw] tx bytes",
	"[hw] tx mcast frames",
	"[hw] tx mcast bytes",
	"[hw] tx bcast frames",
	"[hw] tx bcast bytes",
	"[hw] rx filtered frames",
	"[hw] rx discarded frames",
	"[hw] rx nobuffer discards",
	"[hw] tx discarded frames",
	"[hw] tx confirmed frames",
	"[hw] tx dequeued bytes",
	"[hw] tx dequeued frames",
	"[hw] tx rejected bytes",
	"[hw] tx rejected frames",
	"[hw] tx pending frames",
};

#define DPAA2_ETH_NUM_STATS	ARRAY_SIZE(dpaa2_ethtool_stats)

static char dpaa2_ethtool_extras[][ETH_GSTRING_LEN] = {
	/* per-cpu stats */
	"[drv] tx conf frames",
	"[drv] tx conf bytes",
	"[drv] tx sg frames",
	"[drv] tx sg bytes",
	"[drv] rx sg frames",
	"[drv] rx sg bytes",
	"[drv] tx converted sg frames",
	"[drv] tx converted sg bytes",
	"[drv] enqueue portal busy",
	/* Channel stats */
	"[drv] dequeue portal busy",
	"[drv] channel pull errors",
	"[drv] cdan",
	"[drv] xdp drop",
	"[drv] xdp tx",
	"[drv] xdp tx errors",
	"[drv] xdp redirect",
	/* FQ stats */
	"[qbman] rx pending frames",
	"[qbman] rx pending bytes",
	"[qbman] tx conf pending frames",
	"[qbman] tx conf pending bytes",
	"[qbman] buffer count",
};

#define DPAA2_ETH_NUM_EXTRA_STATS	ARRAY_SIZE(dpaa2_ethtool_extras)

static void dpaa2_eth_get_drvinfo(struct net_device *net_dev,
				  struct ethtool_drvinfo *drvinfo)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);

	strlcpy(drvinfo->driver, KBUILD_MODNAME, sizeof(drvinfo->driver));

	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
		 "%u.%u", priv->dpni_ver_major, priv->dpni_ver_minor);

	strlcpy(drvinfo->bus_info, dev_name(net_dev->dev.parent->parent),
		sizeof(drvinfo->bus_info));
}

static int dpaa2_eth_nway_reset(struct net_device *net_dev)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);

	if (dpaa2_eth_is_type_phy(priv))
		return phylink_ethtool_nway_reset(priv->mac->phylink);

	return -EOPNOTSUPP;
}

static int
dpaa2_eth_get_link_ksettings(struct net_device *net_dev,
			     struct ethtool_link_ksettings *link_settings)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);

	if (dpaa2_eth_is_type_phy(priv))
		return phylink_ethtool_ksettings_get(priv->mac->phylink,
						     link_settings);

	link_settings->base.autoneg = AUTONEG_DISABLE;
	if (!(priv->link_state.options & DPNI_LINK_OPT_HALF_DUPLEX))
		link_settings->base.duplex = DUPLEX_FULL;
	link_settings->base.speed = priv->link_state.rate;

	return 0;
}

static int
dpaa2_eth_set_link_ksettings(struct net_device *net_dev,
			     const struct ethtool_link_ksettings *link_settings)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);

	if (!dpaa2_eth_is_type_phy(priv))
		return -ENOTSUPP;

	return phylink_ethtool_ksettings_set(priv->mac->phylink, link_settings);
}

static void dpaa2_eth_get_pauseparam(struct net_device *net_dev,
				     struct ethtool_pauseparam *pause)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);
	u64 link_options = priv->link_state.options;

	if (dpaa2_eth_is_type_phy(priv)) {
		phylink_ethtool_get_pauseparam(priv->mac->phylink, pause);
		return;
	}

	pause->rx_pause = dpaa2_eth_rx_pause_enabled(link_options);
	pause->tx_pause = dpaa2_eth_tx_pause_enabled(link_options);
	pause->autoneg = AUTONEG_DISABLE;
}

static int dpaa2_eth_set_pauseparam(struct net_device *net_dev,
				    struct ethtool_pauseparam *pause)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);
	struct dpni_link_cfg cfg = {0};
	int err;

	if (!dpaa2_eth_has_pause_support(priv)) {
		netdev_info(net_dev, "No pause frame support for DPNI version < %d.%d\n",
			    DPNI_PAUSE_VER_MAJOR, DPNI_PAUSE_VER_MINOR);
		return -EOPNOTSUPP;
	}

	if (dpaa2_eth_is_type_phy(priv))
		return phylink_ethtool_set_pauseparam(priv->mac->phylink,
						      pause);
	if (pause->autoneg)
		return -EOPNOTSUPP;

	cfg.rate = priv->link_state.rate;
	cfg.options = priv->link_state.options;
	if (pause->rx_pause)
		cfg.options |= DPNI_LINK_OPT_PAUSE;
	else
		cfg.options &= ~DPNI_LINK_OPT_PAUSE;
	if (!!pause->rx_pause ^ !!pause->tx_pause)
		cfg.options |= DPNI_LINK_OPT_ASYM_PAUSE;
	else
		cfg.options &= ~DPNI_LINK_OPT_ASYM_PAUSE;

	if (cfg.options == priv->link_state.options)
		return 0;

	err = dpni_set_link_cfg(priv->mc_io, 0, priv->mc_token, &cfg);
	if (err) {
		netdev_err(net_dev, "dpni_set_link_state failed\n");
		return err;
	}

	priv->link_state.options = cfg.options;

	return 0;
}

static void dpaa2_eth_get_strings(struct net_device *netdev, u32 stringset,
				  u8 *data)
{
	struct dpaa2_eth_priv *priv = netdev_priv(netdev);
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < DPAA2_ETH_NUM_STATS; i++) {
			strlcpy(p, dpaa2_ethtool_stats[i], ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < DPAA2_ETH_NUM_EXTRA_STATS; i++) {
			strlcpy(p, dpaa2_ethtool_extras[i], ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		if (dpaa2_eth_has_mac(priv))
			dpaa2_mac_get_strings(p);
		break;
	}
}

static int dpaa2_eth_get_sset_count(struct net_device *net_dev, int sset)
{
	int num_ss_stats = DPAA2_ETH_NUM_STATS + DPAA2_ETH_NUM_EXTRA_STATS;
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);

	switch (sset) {
	case ETH_SS_STATS: /* ethtool_get_stats(), ethtool_get_drvinfo() */
		if (dpaa2_eth_has_mac(priv))
			num_ss_stats += dpaa2_mac_get_sset_count();
		return num_ss_stats;
	default:
		return -EOPNOTSUPP;
	}
}

/** Fill in hardware counters, as returned by MC.
 */
static void dpaa2_eth_get_ethtool_stats(struct net_device *net_dev,
					struct ethtool_stats *stats,
					u64 *data)
{
	int i = 0;
	int j, k, err;
	int num_cnt;
	union dpni_statistics dpni_stats;
	u32 fcnt, bcnt;
	u32 fcnt_rx_total = 0, fcnt_tx_total = 0;
	u32 bcnt_rx_total = 0, bcnt_tx_total = 0;
	u32 buf_cnt;
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);
	struct dpaa2_eth_drv_stats *extras;
	struct dpaa2_eth_ch_stats *ch_stats;
	int dpni_stats_page_size[DPNI_STATISTICS_CNT] = {
		sizeof(dpni_stats.page_0),
		sizeof(dpni_stats.page_1),
		sizeof(dpni_stats.page_2),
		sizeof(dpni_stats.page_3),
		sizeof(dpni_stats.page_4),
		sizeof(dpni_stats.page_5),
		sizeof(dpni_stats.page_6),
	};

	memset(data, 0,
	       sizeof(u64) * (DPAA2_ETH_NUM_STATS + DPAA2_ETH_NUM_EXTRA_STATS));

	/* Print standard counters, from DPNI statistics */
	for (j = 0; j <= 6; j++) {
		/* We're not interested in pages 4 & 5 for now */
		if (j == 4 || j == 5)
			continue;
		err = dpni_get_statistics(priv->mc_io, 0, priv->mc_token,
					  j, &dpni_stats);
		if (err == -EINVAL)
			/* Older firmware versions don't support all pages */
			memset(&dpni_stats, 0, sizeof(dpni_stats));
		else if (err)
			netdev_warn(net_dev, "dpni_get_stats(%d) failed\n", j);

		num_cnt = dpni_stats_page_size[j] / sizeof(u64);
		for (k = 0; k < num_cnt; k++)
			*(data + i++) = dpni_stats.raw.counter[k];
	}

	/* Print per-cpu extra stats */
	for_each_online_cpu(k) {
		extras = per_cpu_ptr(priv->percpu_extras, k);
		for (j = 0; j < sizeof(*extras) / sizeof(__u64); j++)
			*((__u64 *)data + i + j) += *((__u64 *)extras + j);
	}
	i += j;

	/* Per-channel stats */
	for (k = 0; k < priv->num_channels; k++) {
		ch_stats = &priv->channel[k]->stats;
		for (j = 0; j < sizeof(*ch_stats) / sizeof(__u64) - 1; j++)
			*((__u64 *)data + i + j) += *((__u64 *)ch_stats + j);
	}
	i += j;

	for (j = 0; j < priv->num_fqs; j++) {
		/* Print FQ instantaneous counts */
		err = dpaa2_io_query_fq_count(NULL, priv->fq[j].fqid,
					      &fcnt, &bcnt);
		if (err) {
			netdev_warn(net_dev, "FQ query error %d", err);
			return;
		}

		if (priv->fq[j].type == DPAA2_TX_CONF_FQ) {
			fcnt_tx_total += fcnt;
			bcnt_tx_total += bcnt;
		} else {
			fcnt_rx_total += fcnt;
			bcnt_rx_total += bcnt;
		}
	}

	*(data + i++) = fcnt_rx_total;
	*(data + i++) = bcnt_rx_total;
	*(data + i++) = fcnt_tx_total;
	*(data + i++) = bcnt_tx_total;

	err = dpaa2_io_query_bp_count(NULL, priv->bpid, &buf_cnt);
	if (err) {
		netdev_warn(net_dev, "Buffer count query error %d\n", err);
		return;
	}
	*(data + i++) = buf_cnt;

	if (dpaa2_eth_has_mac(priv))
		dpaa2_mac_get_ethtool_stats(priv->mac, data + i);
}

static int dpaa2_eth_prep_eth_rule(struct ethhdr *eth_value, struct ethhdr *eth_mask,
				   void *key, void *mask, u64 *fields)
{
	int off;

	if (eth_mask->h_proto) {
		off = dpaa2_eth_cls_fld_off(NET_PROT_ETH, NH_FLD_ETH_TYPE);
		*(__be16 *)(key + off) = eth_value->h_proto;
		*(__be16 *)(mask + off) = eth_mask->h_proto;
		*fields |= DPAA2_ETH_DIST_ETHTYPE;
	}

	if (!is_zero_ether_addr(eth_mask->h_source)) {
		off = dpaa2_eth_cls_fld_off(NET_PROT_ETH, NH_FLD_ETH_SA);
		ether_addr_copy(key + off, eth_value->h_source);
		ether_addr_copy(mask + off, eth_mask->h_source);
		*fields |= DPAA2_ETH_DIST_ETHSRC;
	}

	if (!is_zero_ether_addr(eth_mask->h_dest)) {
		off = dpaa2_eth_cls_fld_off(NET_PROT_ETH, NH_FLD_ETH_DA);
		ether_addr_copy(key + off, eth_value->h_dest);
		ether_addr_copy(mask + off, eth_mask->h_dest);
		*fields |= DPAA2_ETH_DIST_ETHDST;
	}

	return 0;
}

static int dpaa2_eth_prep_uip_rule(struct ethtool_usrip4_spec *uip_value,
				   struct ethtool_usrip4_spec *uip_mask,
				   void *key, void *mask, u64 *fields)
{
	int off;
	u32 tmp_value, tmp_mask;

	if (uip_mask->tos || uip_mask->ip_ver)
		return -EOPNOTSUPP;

	if (uip_mask->ip4src) {
		off = dpaa2_eth_cls_fld_off(NET_PROT_IP, NH_FLD_IP_SRC);
		*(__be32 *)(key + off) = uip_value->ip4src;
		*(__be32 *)(mask + off) = uip_mask->ip4src;
		*fields |= DPAA2_ETH_DIST_IPSRC;
	}

	if (uip_mask->ip4dst) {
		off = dpaa2_eth_cls_fld_off(NET_PROT_IP, NH_FLD_IP_DST);
		*(__be32 *)(key + off) = uip_value->ip4dst;
		*(__be32 *)(mask + off) = uip_mask->ip4dst;
		*fields |= DPAA2_ETH_DIST_IPDST;
	}

	if (uip_mask->proto) {
		off = dpaa2_eth_cls_fld_off(NET_PROT_IP, NH_FLD_IP_PROTO);
		*(u8 *)(key + off) = uip_value->proto;
		*(u8 *)(mask + off) = uip_mask->proto;
		*fields |= DPAA2_ETH_DIST_IPPROTO;
	}

	if (uip_mask->l4_4_bytes) {
		tmp_value = be32_to_cpu(uip_value->l4_4_bytes);
		tmp_mask = be32_to_cpu(uip_mask->l4_4_bytes);

		off = dpaa2_eth_cls_fld_off(NET_PROT_UDP, NH_FLD_UDP_PORT_SRC);
		*(__be16 *)(key + off) = htons(tmp_value >> 16);
		*(__be16 *)(mask + off) = htons(tmp_mask >> 16);
		*fields |= DPAA2_ETH_DIST_L4SRC;

		off = dpaa2_eth_cls_fld_off(NET_PROT_UDP, NH_FLD_UDP_PORT_DST);
		*(__be16 *)(key + off) = htons(tmp_value & 0xFFFF);
		*(__be16 *)(mask + off) = htons(tmp_mask & 0xFFFF);
		*fields |= DPAA2_ETH_DIST_L4DST;
	}

	/* Only apply the rule for IPv4 frames */
	off = dpaa2_eth_cls_fld_off(NET_PROT_ETH, NH_FLD_ETH_TYPE);
	*(__be16 *)(key + off) = htons(ETH_P_IP);
	*(__be16 *)(mask + off) = htons(0xFFFF);
	*fields |= DPAA2_ETH_DIST_ETHTYPE;

	return 0;
}

static int dpaa2_eth_prep_l4_rule(struct ethtool_tcpip4_spec *l4_value,
				  struct ethtool_tcpip4_spec *l4_mask,
				  void *key, void *mask, u8 l4_proto, u64 *fields)
{
	int off;

	if (l4_mask->tos)
		return -EOPNOTSUPP;

	if (l4_mask->ip4src) {
		off = dpaa2_eth_cls_fld_off(NET_PROT_IP, NH_FLD_IP_SRC);
		*(__be32 *)(key + off) = l4_value->ip4src;
		*(__be32 *)(mask + off) = l4_mask->ip4src;
		*fields |= DPAA2_ETH_DIST_IPSRC;
	}

	if (l4_mask->ip4dst) {
		off = dpaa2_eth_cls_fld_off(NET_PROT_IP, NH_FLD_IP_DST);
		*(__be32 *)(key + off) = l4_value->ip4dst;
		*(__be32 *)(mask + off) = l4_mask->ip4dst;
		*fields |= DPAA2_ETH_DIST_IPDST;
	}

	if (l4_mask->psrc) {
		off = dpaa2_eth_cls_fld_off(NET_PROT_UDP, NH_FLD_UDP_PORT_SRC);
		*(__be16 *)(key + off) = l4_value->psrc;
		*(__be16 *)(mask + off) = l4_mask->psrc;
		*fields |= DPAA2_ETH_DIST_L4SRC;
	}

	if (l4_mask->pdst) {
		off = dpaa2_eth_cls_fld_off(NET_PROT_UDP, NH_FLD_UDP_PORT_DST);
		*(__be16 *)(key + off) = l4_value->pdst;
		*(__be16 *)(mask + off) = l4_mask->pdst;
		*fields |= DPAA2_ETH_DIST_L4DST;
	}

	/* Only apply the rule for IPv4 frames with the specified L4 proto */
	off = dpaa2_eth_cls_fld_off(NET_PROT_ETH, NH_FLD_ETH_TYPE);
	*(__be16 *)(key + off) = htons(ETH_P_IP);
	*(__be16 *)(mask + off) = htons(0xFFFF);
	*fields |= DPAA2_ETH_DIST_ETHTYPE;

	off = dpaa2_eth_cls_fld_off(NET_PROT_IP, NH_FLD_IP_PROTO);
	*(u8 *)(key + off) = l4_proto;
	*(u8 *)(mask + off) = 0xFF;
	*fields |= DPAA2_ETH_DIST_IPPROTO;

	return 0;
}

static int dpaa2_eth_prep_ext_rule(struct ethtool_flow_ext *ext_value,
				   struct ethtool_flow_ext *ext_mask,
				   void *key, void *mask, u64 *fields)
{
	int off;

	if (ext_mask->vlan_etype)
		return -EOPNOTSUPP;

	if (ext_mask->vlan_tci) {
		off = dpaa2_eth_cls_fld_off(NET_PROT_VLAN, NH_FLD_VLAN_TCI);
		*(__be16 *)(key + off) = ext_value->vlan_tci;
		*(__be16 *)(mask + off) = ext_mask->vlan_tci;
		*fields |= DPAA2_ETH_DIST_VLAN;
	}

	return 0;
}

static int dpaa2_eth_prep_mac_ext_rule(struct ethtool_flow_ext *ext_value,
				       struct ethtool_flow_ext *ext_mask,
				       void *key, void *mask, u64 *fields)
{
	int off;

	if (!is_zero_ether_addr(ext_mask->h_dest)) {
		off = dpaa2_eth_cls_fld_off(NET_PROT_ETH, NH_FLD_ETH_DA);
		ether_addr_copy(key + off, ext_value->h_dest);
		ether_addr_copy(mask + off, ext_mask->h_dest);
		*fields |= DPAA2_ETH_DIST_ETHDST;
	}

	return 0;
}

static int dpaa2_eth_prep_cls_rule(struct ethtool_rx_flow_spec *fs, void *key,
				   void *mask, u64 *fields)
{
	int err;

	switch (fs->flow_type & 0xFF) {
	case ETHER_FLOW:
		err = dpaa2_eth_prep_eth_rule(&fs->h_u.ether_spec, &fs->m_u.ether_spec,
					      key, mask, fields);
		break;
	case IP_USER_FLOW:
		err = dpaa2_eth_prep_uip_rule(&fs->h_u.usr_ip4_spec,
					      &fs->m_u.usr_ip4_spec, key, mask, fields);
		break;
	case TCP_V4_FLOW:
		err = dpaa2_eth_prep_l4_rule(&fs->h_u.tcp_ip4_spec, &fs->m_u.tcp_ip4_spec,
					     key, mask, IPPROTO_TCP, fields);
		break;
	case UDP_V4_FLOW:
		err = dpaa2_eth_prep_l4_rule(&fs->h_u.udp_ip4_spec, &fs->m_u.udp_ip4_spec,
					     key, mask, IPPROTO_UDP, fields);
		break;
	case SCTP_V4_FLOW:
		err = dpaa2_eth_prep_l4_rule(&fs->h_u.sctp_ip4_spec,
					     &fs->m_u.sctp_ip4_spec, key, mask,
					     IPPROTO_SCTP, fields);
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (err)
		return err;

	if (fs->flow_type & FLOW_EXT) {
		err = dpaa2_eth_prep_ext_rule(&fs->h_ext, &fs->m_ext, key, mask, fields);
		if (err)
			return err;
	}

	if (fs->flow_type & FLOW_MAC_EXT) {
		err = dpaa2_eth_prep_mac_ext_rule(&fs->h_ext, &fs->m_ext, key,
						  mask, fields);
		if (err)
			return err;
	}

	return 0;
}

static int dpaa2_eth_do_cls_rule(struct net_device *net_dev,
				 struct ethtool_rx_flow_spec *fs,
				 bool add)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);
	struct device *dev = net_dev->dev.parent;
	struct dpni_rule_cfg rule_cfg = { 0 };
	struct dpni_fs_action_cfg fs_act = { 0 };
	dma_addr_t key_iova;
	u64 fields = 0;
	void *key_buf;
	int i, err;

	if (fs->ring_cookie != RX_CLS_FLOW_DISC &&
	    fs->ring_cookie >= dpaa2_eth_queue_count(priv))
		return -EINVAL;

	rule_cfg.key_size = dpaa2_eth_cls_key_size(DPAA2_ETH_DIST_ALL);

	/* allocate twice the key size, for the actual key and for mask */
	key_buf = kzalloc(rule_cfg.key_size * 2, GFP_KERNEL);
	if (!key_buf)
		return -ENOMEM;

	/* Fill the key and mask memory areas */
	err = dpaa2_eth_prep_cls_rule(fs, key_buf, key_buf + rule_cfg.key_size, &fields);
	if (err)
		goto free_mem;

	if (!dpaa2_eth_fs_mask_enabled(priv)) {
		/* Masking allows us to configure a maximal key during init and
		 * use it for all flow steering rules. Without it, we include
		 * in the key only the fields actually used, so we need to
		 * extract the others from the final key buffer.
		 *
		 * Program the FS key if needed, or return error if previously
		 * set key can't be used for the current rule. User needs to
		 * delete existing rules in this case to allow for the new one.
		 */
		if (!priv->rx_cls_fields) {
			err = dpaa2_eth_set_cls(net_dev, fields);
			if (err)
				goto free_mem;

			priv->rx_cls_fields = fields;
		} else if (priv->rx_cls_fields != fields) {
			netdev_err(net_dev, "No support for multiple FS keys, need to delete existing rules\n");
			err = -EOPNOTSUPP;
			goto free_mem;
		}

		dpaa2_eth_cls_trim_rule(key_buf, fields);
		rule_cfg.key_size = dpaa2_eth_cls_key_size(fields);
	}

	key_iova = dma_map_single(dev, key_buf, rule_cfg.key_size * 2,
				  DMA_TO_DEVICE);
	if (dma_mapping_error(dev, key_iova)) {
		err = -ENOMEM;
		goto free_mem;
	}

	rule_cfg.key_iova = key_iova;
	if (dpaa2_eth_fs_mask_enabled(priv))
		rule_cfg.mask_iova = key_iova + rule_cfg.key_size;

	if (add) {
		if (fs->ring_cookie == RX_CLS_FLOW_DISC)
			fs_act.options |= DPNI_FS_OPT_DISCARD;
		else
			fs_act.flow_id = fs->ring_cookie;
	}
	for (i = 0; i < dpaa2_eth_tc_count(priv); i++) {
		if (add)
			err = dpni_add_fs_entry(priv->mc_io, 0, priv->mc_token,
						i, fs->location, &rule_cfg,
						&fs_act);
		else
			err = dpni_remove_fs_entry(priv->mc_io, 0,
						   priv->mc_token, i,
						   &rule_cfg);
		if (err || priv->dpni_attrs.options & DPNI_OPT_SHARED_FS)
			break;
	}

	dma_unmap_single(dev, key_iova, rule_cfg.key_size * 2, DMA_TO_DEVICE);

free_mem:
	kfree(key_buf);

	return err;
}

static int dpaa2_eth_num_cls_rules(struct dpaa2_eth_priv *priv)
{
	int i, rules = 0;

	for (i = 0; i < dpaa2_eth_fs_count(priv); i++)
		if (priv->cls_rules[i].in_use)
			rules++;

	return rules;
}

static int dpaa2_eth_update_cls_rule(struct net_device *net_dev,
				     struct ethtool_rx_flow_spec *new_fs,
				     unsigned int location)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);
	struct dpaa2_eth_cls_rule *rule;
	int err = -EINVAL;

	if (!priv->rx_cls_enabled)
		return -EOPNOTSUPP;

	if (location >= dpaa2_eth_fs_count(priv))
		return -EINVAL;

	rule = &priv->cls_rules[location];

	/* If a rule is present at the specified location, delete it. */
	if (rule->in_use) {
		err = dpaa2_eth_do_cls_rule(net_dev, &rule->fs, false);
		if (err)
			return err;

		rule->in_use = 0;

		if (!dpaa2_eth_fs_mask_enabled(priv) &&
		    !dpaa2_eth_num_cls_rules(priv))
			priv->rx_cls_fields = 0;
	}

	/* If no new entry to add, return here */
	if (!new_fs)
		return err;

	err = dpaa2_eth_do_cls_rule(net_dev, new_fs, true);
	if (err)
		return err;

	rule->in_use = 1;
	rule->fs = *new_fs;

	return 0;
}

static int dpaa2_eth_get_rxnfc(struct net_device *net_dev,
			       struct ethtool_rxnfc *rxnfc, u32 *rule_locs)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);
	int max_rules = dpaa2_eth_fs_count(priv);
	int i, j = 0;

	switch (rxnfc->cmd) {
	case ETHTOOL_GRXFH:
		/* we purposely ignore cmd->flow_type for now, because the
		 * classifier only supports a single set of fields for all
		 * protocols
		 */
		rxnfc->data = priv->rx_hash_fields;
		break;
	case ETHTOOL_GRXRINGS:
		rxnfc->data = dpaa2_eth_queue_count(priv);
		break;
	case ETHTOOL_GRXCLSRLCNT:
		rxnfc->rule_cnt = 0;
		rxnfc->rule_cnt = dpaa2_eth_num_cls_rules(priv);
		rxnfc->data = max_rules;
		break;
	case ETHTOOL_GRXCLSRULE:
		if (rxnfc->fs.location >= max_rules)
			return -EINVAL;
		rxnfc->fs.location = array_index_nospec(rxnfc->fs.location,
							max_rules);
		if (!priv->cls_rules[rxnfc->fs.location].in_use)
			return -EINVAL;
		rxnfc->fs = priv->cls_rules[rxnfc->fs.location].fs;
		break;
	case ETHTOOL_GRXCLSRLALL:
		for (i = 0; i < max_rules; i++) {
			if (!priv->cls_rules[i].in_use)
				continue;
			if (j == rxnfc->rule_cnt)
				return -EMSGSIZE;
			rule_locs[j++] = i;
		}
		rxnfc->rule_cnt = j;
		rxnfc->data = max_rules;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int dpaa2_eth_set_rxnfc(struct net_device *net_dev,
			       struct ethtool_rxnfc *rxnfc)
{
	int err = 0;

	switch (rxnfc->cmd) {
	case ETHTOOL_SRXFH:
		if ((rxnfc->data & DPAA2_RXH_SUPPORTED) != rxnfc->data)
			return -EOPNOTSUPP;
		err = dpaa2_eth_set_hash(net_dev, rxnfc->data);
		break;
	case ETHTOOL_SRXCLSRLINS:
		err = dpaa2_eth_update_cls_rule(net_dev, &rxnfc->fs, rxnfc->fs.location);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		err = dpaa2_eth_update_cls_rule(net_dev, NULL, rxnfc->fs.location);
		break;
	default:
		err = -EOPNOTSUPP;
	}

	return err;
}

int dpaa2_phc_index = -1;
EXPORT_SYMBOL(dpaa2_phc_index);

static int dpaa2_eth_get_ts_info(struct net_device *dev,
				 struct ethtool_ts_info *info)
{
	if (!dpaa2_ptp)
		return ethtool_op_get_ts_info(dev, info);

	info->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;

	info->phc_index = dpaa2_phc_index;

	info->tx_types = (1 << HWTSTAMP_TX_OFF) |
			 (1 << HWTSTAMP_TX_ON) |
			 (1 << HWTSTAMP_TX_ONESTEP_SYNC);

	info->rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
			   (1 << HWTSTAMP_FILTER_ALL);
	return 0;
}

static int dpaa2_eth_get_tunable(struct net_device *net_dev,
				 const struct ethtool_tunable *tuna,
				 void *data)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);
	int err = 0;

	switch (tuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		*(u32 *)data = priv->rx_copybreak;
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int dpaa2_eth_set_tunable(struct net_device *net_dev,
				 const struct ethtool_tunable *tuna,
				 const void *data)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);
	int err = 0;

	switch (tuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		priv->rx_copybreak = *(u32 *)data;
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

const struct ethtool_ops dpaa2_ethtool_ops = {
	.get_drvinfo = dpaa2_eth_get_drvinfo,
	.nway_reset = dpaa2_eth_nway_reset,
	.get_link = ethtool_op_get_link,
	.get_link_ksettings = dpaa2_eth_get_link_ksettings,
	.set_link_ksettings = dpaa2_eth_set_link_ksettings,
	.get_pauseparam = dpaa2_eth_get_pauseparam,
	.set_pauseparam = dpaa2_eth_set_pauseparam,
	.get_sset_count = dpaa2_eth_get_sset_count,
	.get_ethtool_stats = dpaa2_eth_get_ethtool_stats,
	.get_strings = dpaa2_eth_get_strings,
	.get_rxnfc = dpaa2_eth_get_rxnfc,
	.set_rxnfc = dpaa2_eth_set_rxnfc,
	.get_ts_info = dpaa2_eth_get_ts_info,
	.get_tunable = dpaa2_eth_get_tunable,
	.set_tunable = dpaa2_eth_set_tunable,
};
