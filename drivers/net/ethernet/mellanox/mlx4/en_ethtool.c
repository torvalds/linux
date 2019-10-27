/*
 * Copyright (c) 2007 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
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
 *
 */

#include <linux/kernel.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/mlx4/driver.h>
#include <linux/mlx4/device.h>
#include <linux/in.h>
#include <net/ip.h>
#include <linux/bitmap.h>

#include "mlx4_en.h"
#include "en_port.h"

#define EN_ETHTOOL_QP_ATTACH (1ull << 63)
#define EN_ETHTOOL_SHORT_MASK cpu_to_be16(0xffff)
#define EN_ETHTOOL_WORD_MASK  cpu_to_be32(0xffffffff)

static int mlx4_en_moderation_update(struct mlx4_en_priv *priv)
{
	int i, t;
	int err = 0;

	for (t = 0 ; t < MLX4_EN_NUM_TX_TYPES; t++) {
		for (i = 0; i < priv->tx_ring_num[t]; i++) {
			priv->tx_cq[t][i]->moder_cnt = priv->tx_frames;
			priv->tx_cq[t][i]->moder_time = priv->tx_usecs;
			if (priv->port_up) {
				err = mlx4_en_set_cq_moder(priv,
							   priv->tx_cq[t][i]);
				if (err)
					return err;
			}
		}
	}

	if (priv->adaptive_rx_coal)
		return 0;

	for (i = 0; i < priv->rx_ring_num; i++) {
		priv->rx_cq[i]->moder_cnt = priv->rx_frames;
		priv->rx_cq[i]->moder_time = priv->rx_usecs;
		priv->last_moder_time[i] = MLX4_EN_AUTO_CONF;
		if (priv->port_up) {
			err = mlx4_en_set_cq_moder(priv, priv->rx_cq[i]);
			if (err)
				return err;
		}
	}

	return err;
}

static void
mlx4_en_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *drvinfo)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;

	strlcpy(drvinfo->driver, DRV_NAME, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, DRV_VERSION,
		sizeof(drvinfo->version));
	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
		"%d.%d.%d",
		(u16) (mdev->dev->caps.fw_ver >> 32),
		(u16) ((mdev->dev->caps.fw_ver >> 16) & 0xffff),
		(u16) (mdev->dev->caps.fw_ver & 0xffff));
	strlcpy(drvinfo->bus_info, pci_name(mdev->dev->persist->pdev),
		sizeof(drvinfo->bus_info));
}

static const char mlx4_en_priv_flags[][ETH_GSTRING_LEN] = {
	"blueflame",
	"phv-bit"
};

static const char main_strings[][ETH_GSTRING_LEN] = {
	/* main statistics */
	"rx_packets", "tx_packets", "rx_bytes", "tx_bytes", "rx_errors",
	"tx_errors", "rx_dropped", "tx_dropped", "multicast", "collisions",
	"rx_length_errors", "rx_over_errors", "rx_crc_errors",
	"rx_frame_errors", "rx_fifo_errors", "rx_missed_errors",
	"tx_aborted_errors", "tx_carrier_errors", "tx_fifo_errors",
	"tx_heartbeat_errors", "tx_window_errors",

	/* port statistics */
	"tso_packets",
	"xmit_more",
	"queue_stopped", "wake_queue", "tx_timeout", "rx_alloc_pages",
	"rx_csum_good", "rx_csum_none", "rx_csum_complete", "tx_chksum_offload",

	/* pf statistics */
	"pf_rx_packets",
	"pf_rx_bytes",
	"pf_tx_packets",
	"pf_tx_bytes",

	/* priority flow control statistics rx */
	"rx_pause_prio_0", "rx_pause_duration_prio_0",
	"rx_pause_transition_prio_0",
	"rx_pause_prio_1", "rx_pause_duration_prio_1",
	"rx_pause_transition_prio_1",
	"rx_pause_prio_2", "rx_pause_duration_prio_2",
	"rx_pause_transition_prio_2",
	"rx_pause_prio_3", "rx_pause_duration_prio_3",
	"rx_pause_transition_prio_3",
	"rx_pause_prio_4", "rx_pause_duration_prio_4",
	"rx_pause_transition_prio_4",
	"rx_pause_prio_5", "rx_pause_duration_prio_5",
	"rx_pause_transition_prio_5",
	"rx_pause_prio_6", "rx_pause_duration_prio_6",
	"rx_pause_transition_prio_6",
	"rx_pause_prio_7", "rx_pause_duration_prio_7",
	"rx_pause_transition_prio_7",

	/* flow control statistics rx */
	"rx_pause", "rx_pause_duration", "rx_pause_transition",

	/* priority flow control statistics tx */
	"tx_pause_prio_0", "tx_pause_duration_prio_0",
	"tx_pause_transition_prio_0",
	"tx_pause_prio_1", "tx_pause_duration_prio_1",
	"tx_pause_transition_prio_1",
	"tx_pause_prio_2", "tx_pause_duration_prio_2",
	"tx_pause_transition_prio_2",
	"tx_pause_prio_3", "tx_pause_duration_prio_3",
	"tx_pause_transition_prio_3",
	"tx_pause_prio_4", "tx_pause_duration_prio_4",
	"tx_pause_transition_prio_4",
	"tx_pause_prio_5", "tx_pause_duration_prio_5",
	"tx_pause_transition_prio_5",
	"tx_pause_prio_6", "tx_pause_duration_prio_6",
	"tx_pause_transition_prio_6",
	"tx_pause_prio_7", "tx_pause_duration_prio_7",
	"tx_pause_transition_prio_7",

	/* flow control statistics tx */
	"tx_pause", "tx_pause_duration", "tx_pause_transition",

	/* packet statistics */
	"rx_multicast_packets",
	"rx_broadcast_packets",
	"rx_jabbers",
	"rx_in_range_length_error",
	"rx_out_range_length_error",
	"tx_multicast_packets",
	"tx_broadcast_packets",
	"rx_prio_0_packets", "rx_prio_0_bytes",
	"rx_prio_1_packets", "rx_prio_1_bytes",
	"rx_prio_2_packets", "rx_prio_2_bytes",
	"rx_prio_3_packets", "rx_prio_3_bytes",
	"rx_prio_4_packets", "rx_prio_4_bytes",
	"rx_prio_5_packets", "rx_prio_5_bytes",
	"rx_prio_6_packets", "rx_prio_6_bytes",
	"rx_prio_7_packets", "rx_prio_7_bytes",
	"rx_novlan_packets", "rx_novlan_bytes",
	"tx_prio_0_packets", "tx_prio_0_bytes",
	"tx_prio_1_packets", "tx_prio_1_bytes",
	"tx_prio_2_packets", "tx_prio_2_bytes",
	"tx_prio_3_packets", "tx_prio_3_bytes",
	"tx_prio_4_packets", "tx_prio_4_bytes",
	"tx_prio_5_packets", "tx_prio_5_bytes",
	"tx_prio_6_packets", "tx_prio_6_bytes",
	"tx_prio_7_packets", "tx_prio_7_bytes",
	"tx_novlan_packets", "tx_novlan_bytes",

	/* xdp statistics */
	"rx_xdp_drop",
	"rx_xdp_tx",
	"rx_xdp_tx_full",

	/* phy statistics */
	"rx_packets_phy", "rx_bytes_phy",
	"tx_packets_phy", "tx_bytes_phy",
};

static const char mlx4_en_test_names[][ETH_GSTRING_LEN]= {
	"Interrupt Test",
	"Link Test",
	"Speed Test",
	"Register Test",
	"Loopback Test",
};

static u32 mlx4_en_get_msglevel(struct net_device *dev)
{
	return ((struct mlx4_en_priv *) netdev_priv(dev))->msg_enable;
}

static void mlx4_en_set_msglevel(struct net_device *dev, u32 val)
{
	((struct mlx4_en_priv *) netdev_priv(dev))->msg_enable = val;
}

static void mlx4_en_get_wol(struct net_device *netdev,
			    struct ethtool_wolinfo *wol)
{
	struct mlx4_en_priv *priv = netdev_priv(netdev);
	struct mlx4_caps *caps = &priv->mdev->dev->caps;
	int err = 0;
	u64 config = 0;
	u64 mask;

	if ((priv->port < 1) || (priv->port > 2)) {
		en_err(priv, "Failed to get WoL information\n");
		return;
	}

	mask = (priv->port == 1) ? MLX4_DEV_CAP_FLAG_WOL_PORT1 :
		MLX4_DEV_CAP_FLAG_WOL_PORT2;

	if (!(caps->flags & mask)) {
		wol->supported = 0;
		wol->wolopts = 0;
		return;
	}

	if (caps->wol_port[priv->port])
		wol->supported = WAKE_MAGIC;
	else
		wol->supported = 0;

	err = mlx4_wol_read(priv->mdev->dev, &config, priv->port);
	if (err) {
		en_err(priv, "Failed to get WoL information\n");
		return;
	}

	if ((config & MLX4_EN_WOL_ENABLED) && (config & MLX4_EN_WOL_MAGIC))
		wol->wolopts = WAKE_MAGIC;
	else
		wol->wolopts = 0;
}

static int mlx4_en_set_wol(struct net_device *netdev,
			    struct ethtool_wolinfo *wol)
{
	struct mlx4_en_priv *priv = netdev_priv(netdev);
	u64 config = 0;
	int err = 0;
	u64 mask;

	if ((priv->port < 1) || (priv->port > 2))
		return -EOPNOTSUPP;

	mask = (priv->port == 1) ? MLX4_DEV_CAP_FLAG_WOL_PORT1 :
		MLX4_DEV_CAP_FLAG_WOL_PORT2;

	if (!(priv->mdev->dev->caps.flags & mask))
		return -EOPNOTSUPP;

	if (wol->supported & ~WAKE_MAGIC)
		return -EINVAL;

	err = mlx4_wol_read(priv->mdev->dev, &config, priv->port);
	if (err) {
		en_err(priv, "Failed to get WoL info, unable to modify\n");
		return err;
	}

	if (wol->wolopts & WAKE_MAGIC) {
		config |= MLX4_EN_WOL_DO_MODIFY | MLX4_EN_WOL_ENABLED |
				MLX4_EN_WOL_MAGIC;
	} else {
		config &= ~(MLX4_EN_WOL_ENABLED | MLX4_EN_WOL_MAGIC);
		config |= MLX4_EN_WOL_DO_MODIFY;
	}

	err = mlx4_wol_write(priv->mdev->dev, config, priv->port);
	if (err)
		en_err(priv, "Failed to set WoL information\n");

	return err;
}

struct bitmap_iterator {
	unsigned long *stats_bitmap;
	unsigned int count;
	unsigned int iterator;
	bool advance_array; /* if set, force no increments */
};

static inline void bitmap_iterator_init(struct bitmap_iterator *h,
					unsigned long *stats_bitmap,
					int count)
{
	h->iterator = 0;
	h->advance_array = !bitmap_empty(stats_bitmap, count);
	h->count = h->advance_array ? bitmap_weight(stats_bitmap, count)
		: count;
	h->stats_bitmap = stats_bitmap;
}

static inline int bitmap_iterator_test(struct bitmap_iterator *h)
{
	return !h->advance_array ? 1 : test_bit(h->iterator, h->stats_bitmap);
}

static inline int bitmap_iterator_inc(struct bitmap_iterator *h)
{
	return h->iterator++;
}

static inline unsigned int
bitmap_iterator_count(struct bitmap_iterator *h)
{
	return h->count;
}

static int mlx4_en_get_sset_count(struct net_device *dev, int sset)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct bitmap_iterator it;

	bitmap_iterator_init(&it, priv->stats_bitmap.bitmap, NUM_ALL_STATS);

	switch (sset) {
	case ETH_SS_STATS:
		return bitmap_iterator_count(&it) +
			(priv->tx_ring_num[TX] * 2) +
			(priv->rx_ring_num * (3 + NUM_XDP_STATS));
	case ETH_SS_TEST:
		return MLX4_EN_NUM_SELF_TEST - !(priv->mdev->dev->caps.flags
					& MLX4_DEV_CAP_FLAG_UC_LOOPBACK) * 2;
	case ETH_SS_PRIV_FLAGS:
		return ARRAY_SIZE(mlx4_en_priv_flags);
	default:
		return -EOPNOTSUPP;
	}
}

static void mlx4_en_get_ethtool_stats(struct net_device *dev,
		struct ethtool_stats *stats, uint64_t *data)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	int index = 0;
	int i;
	struct bitmap_iterator it;

	bitmap_iterator_init(&it, priv->stats_bitmap.bitmap, NUM_ALL_STATS);

	spin_lock_bh(&priv->stats_lock);

	mlx4_en_fold_software_stats(dev);

	for (i = 0; i < NUM_MAIN_STATS; i++, bitmap_iterator_inc(&it))
		if (bitmap_iterator_test(&it))
			data[index++] = ((unsigned long *)&dev->stats)[i];

	for (i = 0; i < NUM_PORT_STATS; i++, bitmap_iterator_inc(&it))
		if (bitmap_iterator_test(&it))
			data[index++] = ((unsigned long *)&priv->port_stats)[i];

	for (i = 0; i < NUM_PF_STATS; i++, bitmap_iterator_inc(&it))
		if (bitmap_iterator_test(&it))
			data[index++] =
				((unsigned long *)&priv->pf_stats)[i];

	for (i = 0; i < NUM_FLOW_PRIORITY_STATS_RX;
	     i++, bitmap_iterator_inc(&it))
		if (bitmap_iterator_test(&it))
			data[index++] =
				((u64 *)&priv->rx_priority_flowstats)[i];

	for (i = 0; i < NUM_FLOW_STATS_RX; i++, bitmap_iterator_inc(&it))
		if (bitmap_iterator_test(&it))
			data[index++] = ((u64 *)&priv->rx_flowstats)[i];

	for (i = 0; i < NUM_FLOW_PRIORITY_STATS_TX;
	     i++, bitmap_iterator_inc(&it))
		if (bitmap_iterator_test(&it))
			data[index++] =
				((u64 *)&priv->tx_priority_flowstats)[i];

	for (i = 0; i < NUM_FLOW_STATS_TX; i++, bitmap_iterator_inc(&it))
		if (bitmap_iterator_test(&it))
			data[index++] = ((u64 *)&priv->tx_flowstats)[i];

	for (i = 0; i < NUM_PKT_STATS; i++, bitmap_iterator_inc(&it))
		if (bitmap_iterator_test(&it))
			data[index++] = ((unsigned long *)&priv->pkstats)[i];

	for (i = 0; i < NUM_XDP_STATS; i++, bitmap_iterator_inc(&it))
		if (bitmap_iterator_test(&it))
			data[index++] = ((unsigned long *)&priv->xdp_stats)[i];

	for (i = 0; i < NUM_PHY_STATS; i++, bitmap_iterator_inc(&it))
		if (bitmap_iterator_test(&it))
			data[index++] = ((unsigned long *)&priv->phy_stats)[i];

	for (i = 0; i < priv->tx_ring_num[TX]; i++) {
		data[index++] = priv->tx_ring[TX][i]->packets;
		data[index++] = priv->tx_ring[TX][i]->bytes;
	}
	for (i = 0; i < priv->rx_ring_num; i++) {
		data[index++] = priv->rx_ring[i]->packets;
		data[index++] = priv->rx_ring[i]->bytes;
		data[index++] = priv->rx_ring[i]->dropped;
		data[index++] = priv->rx_ring[i]->xdp_drop;
		data[index++] = priv->rx_ring[i]->xdp_tx;
		data[index++] = priv->rx_ring[i]->xdp_tx_full;
	}
	spin_unlock_bh(&priv->stats_lock);

}

static void mlx4_en_self_test(struct net_device *dev,
			      struct ethtool_test *etest, u64 *buf)
{
	mlx4_en_ex_selftest(dev, &etest->flags, buf);
}

static void mlx4_en_get_strings(struct net_device *dev,
				uint32_t stringset, uint8_t *data)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	int index = 0;
	int i, strings = 0;
	struct bitmap_iterator it;

	bitmap_iterator_init(&it, priv->stats_bitmap.bitmap, NUM_ALL_STATS);

	switch (stringset) {
	case ETH_SS_TEST:
		for (i = 0; i < MLX4_EN_NUM_SELF_TEST - 2; i++)
			strcpy(data + i * ETH_GSTRING_LEN, mlx4_en_test_names[i]);
		if (priv->mdev->dev->caps.flags & MLX4_DEV_CAP_FLAG_UC_LOOPBACK)
			for (; i < MLX4_EN_NUM_SELF_TEST; i++)
				strcpy(data + i * ETH_GSTRING_LEN, mlx4_en_test_names[i]);
		break;

	case ETH_SS_STATS:
		/* Add main counters */
		for (i = 0; i < NUM_MAIN_STATS; i++, strings++,
		     bitmap_iterator_inc(&it))
			if (bitmap_iterator_test(&it))
				strcpy(data + (index++) * ETH_GSTRING_LEN,
				       main_strings[strings]);

		for (i = 0; i < NUM_PORT_STATS; i++, strings++,
		     bitmap_iterator_inc(&it))
			if (bitmap_iterator_test(&it))
				strcpy(data + (index++) * ETH_GSTRING_LEN,
				       main_strings[strings]);

		for (i = 0; i < NUM_PF_STATS; i++, strings++,
		     bitmap_iterator_inc(&it))
			if (bitmap_iterator_test(&it))
				strcpy(data + (index++) * ETH_GSTRING_LEN,
				       main_strings[strings]);

		for (i = 0; i < NUM_FLOW_STATS; i++, strings++,
		     bitmap_iterator_inc(&it))
			if (bitmap_iterator_test(&it))
				strcpy(data + (index++) * ETH_GSTRING_LEN,
				       main_strings[strings]);

		for (i = 0; i < NUM_PKT_STATS; i++, strings++,
		     bitmap_iterator_inc(&it))
			if (bitmap_iterator_test(&it))
				strcpy(data + (index++) * ETH_GSTRING_LEN,
				       main_strings[strings]);

		for (i = 0; i < NUM_XDP_STATS; i++, strings++,
		     bitmap_iterator_inc(&it))
			if (bitmap_iterator_test(&it))
				strcpy(data + (index++) * ETH_GSTRING_LEN,
				       main_strings[strings]);

		for (i = 0; i < NUM_PHY_STATS; i++, strings++,
		     bitmap_iterator_inc(&it))
			if (bitmap_iterator_test(&it))
				strcpy(data + (index++) * ETH_GSTRING_LEN,
				       main_strings[strings]);

		for (i = 0; i < priv->tx_ring_num[TX]; i++) {
			sprintf(data + (index++) * ETH_GSTRING_LEN,
				"tx%d_packets", i);
			sprintf(data + (index++) * ETH_GSTRING_LEN,
				"tx%d_bytes", i);
		}
		for (i = 0; i < priv->rx_ring_num; i++) {
			sprintf(data + (index++) * ETH_GSTRING_LEN,
				"rx%d_packets", i);
			sprintf(data + (index++) * ETH_GSTRING_LEN,
				"rx%d_bytes", i);
			sprintf(data + (index++) * ETH_GSTRING_LEN,
				"rx%d_dropped", i);
			sprintf(data + (index++) * ETH_GSTRING_LEN,
				"rx%d_xdp_drop", i);
			sprintf(data + (index++) * ETH_GSTRING_LEN,
				"rx%d_xdp_tx", i);
			sprintf(data + (index++) * ETH_GSTRING_LEN,
				"rx%d_xdp_tx_full", i);
		}
		break;
	case ETH_SS_PRIV_FLAGS:
		for (i = 0; i < ARRAY_SIZE(mlx4_en_priv_flags); i++)
			strcpy(data + i * ETH_GSTRING_LEN,
			       mlx4_en_priv_flags[i]);
		break;

	}
}

static u32 mlx4_en_autoneg_get(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	u32 autoneg = AUTONEG_DISABLE;

	if ((mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_ETH_BACKPL_AN_REP) &&
	    (priv->port_state.flags & MLX4_EN_PORT_ANE))
		autoneg = AUTONEG_ENABLE;

	return autoneg;
}

static void ptys2ethtool_update_supported_port(unsigned long *mask,
					       struct mlx4_ptys_reg *ptys_reg)
{
	u32 eth_proto = be32_to_cpu(ptys_reg->eth_proto_cap);

	if (eth_proto & (MLX4_PROT_MASK(MLX4_10GBASE_T)
			 | MLX4_PROT_MASK(MLX4_1000BASE_T)
			 | MLX4_PROT_MASK(MLX4_100BASE_TX))) {
		__set_bit(ETHTOOL_LINK_MODE_TP_BIT, mask);
	} else if (eth_proto & (MLX4_PROT_MASK(MLX4_10GBASE_CR)
			 | MLX4_PROT_MASK(MLX4_10GBASE_SR)
			 | MLX4_PROT_MASK(MLX4_56GBASE_SR4)
			 | MLX4_PROT_MASK(MLX4_40GBASE_CR4)
			 | MLX4_PROT_MASK(MLX4_40GBASE_SR4)
			 | MLX4_PROT_MASK(MLX4_1000BASE_CX_SGMII))) {
		__set_bit(ETHTOOL_LINK_MODE_FIBRE_BIT, mask);
	} else if (eth_proto & (MLX4_PROT_MASK(MLX4_56GBASE_KR4)
			 | MLX4_PROT_MASK(MLX4_40GBASE_KR4)
			 | MLX4_PROT_MASK(MLX4_20GBASE_KR2)
			 | MLX4_PROT_MASK(MLX4_10GBASE_KR)
			 | MLX4_PROT_MASK(MLX4_10GBASE_KX4)
			 | MLX4_PROT_MASK(MLX4_1000BASE_KX))) {
		__set_bit(ETHTOOL_LINK_MODE_Backplane_BIT, mask);
	}
}

static u32 ptys_get_active_port(struct mlx4_ptys_reg *ptys_reg)
{
	u32 eth_proto = be32_to_cpu(ptys_reg->eth_proto_oper);

	if (!eth_proto) /* link down */
		eth_proto = be32_to_cpu(ptys_reg->eth_proto_cap);

	if (eth_proto & (MLX4_PROT_MASK(MLX4_10GBASE_T)
			 | MLX4_PROT_MASK(MLX4_1000BASE_T)
			 | MLX4_PROT_MASK(MLX4_100BASE_TX))) {
			return PORT_TP;
	}

	if (eth_proto & (MLX4_PROT_MASK(MLX4_10GBASE_SR)
			 | MLX4_PROT_MASK(MLX4_56GBASE_SR4)
			 | MLX4_PROT_MASK(MLX4_40GBASE_SR4)
			 | MLX4_PROT_MASK(MLX4_1000BASE_CX_SGMII))) {
			return PORT_FIBRE;
	}

	if (eth_proto & (MLX4_PROT_MASK(MLX4_10GBASE_CR)
			 | MLX4_PROT_MASK(MLX4_56GBASE_CR4)
			 | MLX4_PROT_MASK(MLX4_40GBASE_CR4))) {
			return PORT_DA;
	}

	if (eth_proto & (MLX4_PROT_MASK(MLX4_56GBASE_KR4)
			 | MLX4_PROT_MASK(MLX4_40GBASE_KR4)
			 | MLX4_PROT_MASK(MLX4_20GBASE_KR2)
			 | MLX4_PROT_MASK(MLX4_10GBASE_KR)
			 | MLX4_PROT_MASK(MLX4_10GBASE_KX4)
			 | MLX4_PROT_MASK(MLX4_1000BASE_KX))) {
			return PORT_NONE;
	}
	return PORT_OTHER;
}

#define MLX4_LINK_MODES_SZ \
	(FIELD_SIZEOF(struct mlx4_ptys_reg, eth_proto_cap) * 8)

enum ethtool_report {
	SUPPORTED = 0,
	ADVERTISED = 1,
};

struct ptys2ethtool_config {
	__ETHTOOL_DECLARE_LINK_MODE_MASK(supported);
	__ETHTOOL_DECLARE_LINK_MODE_MASK(advertised);
	u32 speed;
};

static unsigned long *ptys2ethtool_link_mode(struct ptys2ethtool_config *cfg,
					     enum ethtool_report report)
{
	switch (report) {
	case SUPPORTED:
		return cfg->supported;
	case ADVERTISED:
		return cfg->advertised;
	}
	return NULL;
}

#define MLX4_BUILD_PTYS2ETHTOOL_CONFIG(reg_, speed_, ...)		\
	({								\
		struct ptys2ethtool_config *cfg;			\
		static const unsigned int modes[] = { __VA_ARGS__ };	\
		unsigned int i;						\
		cfg = &ptys2ethtool_map[reg_];				\
		cfg->speed = speed_;					\
		bitmap_zero(cfg->supported,				\
			    __ETHTOOL_LINK_MODE_MASK_NBITS);		\
		bitmap_zero(cfg->advertised,				\
			    __ETHTOOL_LINK_MODE_MASK_NBITS);		\
		for (i = 0 ; i < ARRAY_SIZE(modes) ; ++i) {		\
			__set_bit(modes[i], cfg->supported);		\
			__set_bit(modes[i], cfg->advertised);		\
		}							\
	})

/* Translates mlx4 link mode to equivalent ethtool Link modes/speed */
static struct ptys2ethtool_config ptys2ethtool_map[MLX4_LINK_MODES_SZ];

void __init mlx4_en_init_ptys2ethtool_map(void)
{
	MLX4_BUILD_PTYS2ETHTOOL_CONFIG(MLX4_100BASE_TX, SPEED_100,
				       ETHTOOL_LINK_MODE_100baseT_Full_BIT);
	MLX4_BUILD_PTYS2ETHTOOL_CONFIG(MLX4_1000BASE_T, SPEED_1000,
				       ETHTOOL_LINK_MODE_1000baseT_Full_BIT);
	MLX4_BUILD_PTYS2ETHTOOL_CONFIG(MLX4_1000BASE_CX_SGMII, SPEED_1000,
				       ETHTOOL_LINK_MODE_1000baseKX_Full_BIT);
	MLX4_BUILD_PTYS2ETHTOOL_CONFIG(MLX4_1000BASE_KX, SPEED_1000,
				       ETHTOOL_LINK_MODE_1000baseKX_Full_BIT);
	MLX4_BUILD_PTYS2ETHTOOL_CONFIG(MLX4_10GBASE_T, SPEED_10000,
				       ETHTOOL_LINK_MODE_10000baseT_Full_BIT);
	MLX4_BUILD_PTYS2ETHTOOL_CONFIG(MLX4_10GBASE_CX4, SPEED_10000,
				       ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT);
	MLX4_BUILD_PTYS2ETHTOOL_CONFIG(MLX4_10GBASE_KX4, SPEED_10000,
				       ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT);
	MLX4_BUILD_PTYS2ETHTOOL_CONFIG(MLX4_10GBASE_KR, SPEED_10000,
				       ETHTOOL_LINK_MODE_10000baseKR_Full_BIT);
	MLX4_BUILD_PTYS2ETHTOOL_CONFIG(MLX4_10GBASE_CR, SPEED_10000,
				       ETHTOOL_LINK_MODE_10000baseKR_Full_BIT);
	MLX4_BUILD_PTYS2ETHTOOL_CONFIG(MLX4_10GBASE_SR, SPEED_10000,
				       ETHTOOL_LINK_MODE_10000baseKR_Full_BIT);
	MLX4_BUILD_PTYS2ETHTOOL_CONFIG(MLX4_20GBASE_KR2, SPEED_20000,
				       ETHTOOL_LINK_MODE_20000baseMLD2_Full_BIT,
				       ETHTOOL_LINK_MODE_20000baseKR2_Full_BIT);
	MLX4_BUILD_PTYS2ETHTOOL_CONFIG(MLX4_40GBASE_CR4, SPEED_40000,
				       ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT);
	MLX4_BUILD_PTYS2ETHTOOL_CONFIG(MLX4_40GBASE_KR4, SPEED_40000,
				       ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT);
	MLX4_BUILD_PTYS2ETHTOOL_CONFIG(MLX4_40GBASE_SR4, SPEED_40000,
				       ETHTOOL_LINK_MODE_40000baseSR4_Full_BIT);
	MLX4_BUILD_PTYS2ETHTOOL_CONFIG(MLX4_56GBASE_KR4, SPEED_56000,
				       ETHTOOL_LINK_MODE_56000baseKR4_Full_BIT);
	MLX4_BUILD_PTYS2ETHTOOL_CONFIG(MLX4_56GBASE_CR4, SPEED_56000,
				       ETHTOOL_LINK_MODE_56000baseCR4_Full_BIT);
	MLX4_BUILD_PTYS2ETHTOOL_CONFIG(MLX4_56GBASE_SR4, SPEED_56000,
				       ETHTOOL_LINK_MODE_56000baseSR4_Full_BIT);
};

static void ptys2ethtool_update_link_modes(unsigned long *link_modes,
					   u32 eth_proto,
					   enum ethtool_report report)
{
	int i;
	for (i = 0; i < MLX4_LINK_MODES_SZ; i++) {
		if (eth_proto & MLX4_PROT_MASK(i))
			bitmap_or(link_modes, link_modes,
				  ptys2ethtool_link_mode(&ptys2ethtool_map[i],
							 report),
				  __ETHTOOL_LINK_MODE_MASK_NBITS);
	}
}

static u32 ethtool2ptys_link_modes(const unsigned long *link_modes,
				   enum ethtool_report report)
{
	int i;
	u32 ptys_modes = 0;

	for (i = 0; i < MLX4_LINK_MODES_SZ; i++) {
		if (bitmap_intersects(
			    ptys2ethtool_link_mode(&ptys2ethtool_map[i],
						   report),
			    link_modes,
			    __ETHTOOL_LINK_MODE_MASK_NBITS))
			ptys_modes |= 1 << i;
	}
	return ptys_modes;
}

/* Convert actual speed (SPEED_XXX) to ptys link modes */
static u32 speed2ptys_link_modes(u32 speed)
{
	int i;
	u32 ptys_modes = 0;

	for (i = 0; i < MLX4_LINK_MODES_SZ; i++) {
		if (ptys2ethtool_map[i].speed == speed)
			ptys_modes |= 1 << i;
	}
	return ptys_modes;
}

static int
ethtool_get_ptys_link_ksettings(struct net_device *dev,
				struct ethtool_link_ksettings *link_ksettings)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_ptys_reg ptys_reg;
	u32 eth_proto;
	int ret;

	memset(&ptys_reg, 0, sizeof(ptys_reg));
	ptys_reg.local_port = priv->port;
	ptys_reg.proto_mask = MLX4_PTYS_EN;
	ret = mlx4_ACCESS_PTYS_REG(priv->mdev->dev,
				   MLX4_ACCESS_REG_QUERY, &ptys_reg);
	if (ret) {
		en_warn(priv, "Failed to run mlx4_ACCESS_PTYS_REG status(%x)",
			ret);
		return ret;
	}
	en_dbg(DRV, priv, "ptys_reg.proto_mask       %x\n",
	       ptys_reg.proto_mask);
	en_dbg(DRV, priv, "ptys_reg.eth_proto_cap    %x\n",
	       be32_to_cpu(ptys_reg.eth_proto_cap));
	en_dbg(DRV, priv, "ptys_reg.eth_proto_admin  %x\n",
	       be32_to_cpu(ptys_reg.eth_proto_admin));
	en_dbg(DRV, priv, "ptys_reg.eth_proto_oper   %x\n",
	       be32_to_cpu(ptys_reg.eth_proto_oper));
	en_dbg(DRV, priv, "ptys_reg.eth_proto_lp_adv %x\n",
	       be32_to_cpu(ptys_reg.eth_proto_lp_adv));

	/* reset supported/advertising masks */
	ethtool_link_ksettings_zero_link_mode(link_ksettings, supported);
	ethtool_link_ksettings_zero_link_mode(link_ksettings, advertising);

	ptys2ethtool_update_supported_port(link_ksettings->link_modes.supported,
					   &ptys_reg);

	eth_proto = be32_to_cpu(ptys_reg.eth_proto_cap);
	ptys2ethtool_update_link_modes(link_ksettings->link_modes.supported,
				       eth_proto, SUPPORTED);

	eth_proto = be32_to_cpu(ptys_reg.eth_proto_admin);
	ptys2ethtool_update_link_modes(link_ksettings->link_modes.advertising,
				       eth_proto, ADVERTISED);

	ethtool_link_ksettings_add_link_mode(link_ksettings, supported,
					     Pause);
	ethtool_link_ksettings_add_link_mode(link_ksettings, supported,
					     Asym_Pause);

	if (priv->prof->tx_pause)
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     advertising, Pause);
	if (priv->prof->tx_pause ^ priv->prof->rx_pause)
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     advertising, Asym_Pause);

	link_ksettings->base.port = ptys_get_active_port(&ptys_reg);

	if (mlx4_en_autoneg_get(dev)) {
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     advertising, Autoneg);
	}

	link_ksettings->base.autoneg
		= (priv->port_state.flags & MLX4_EN_PORT_ANC) ?
		AUTONEG_ENABLE : AUTONEG_DISABLE;

	eth_proto = be32_to_cpu(ptys_reg.eth_proto_lp_adv);

	ethtool_link_ksettings_zero_link_mode(link_ksettings, lp_advertising);
	ptys2ethtool_update_link_modes(
		link_ksettings->link_modes.lp_advertising,
		eth_proto, ADVERTISED);
	if (priv->port_state.flags & MLX4_EN_PORT_ANC)
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     lp_advertising, Autoneg);

	link_ksettings->base.phy_address = 0;
	link_ksettings->base.mdio_support = 0;
	link_ksettings->base.eth_tp_mdix = ETH_TP_MDI_INVALID;
	link_ksettings->base.eth_tp_mdix_ctrl = ETH_TP_MDI_AUTO;

	return ret;
}

static void
ethtool_get_default_link_ksettings(
	struct net_device *dev, struct ethtool_link_ksettings *link_ksettings)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	int trans_type;

	link_ksettings->base.autoneg = AUTONEG_DISABLE;

	ethtool_link_ksettings_zero_link_mode(link_ksettings, supported);
	ethtool_link_ksettings_add_link_mode(link_ksettings, supported,
					     10000baseT_Full);

	ethtool_link_ksettings_zero_link_mode(link_ksettings, advertising);
	ethtool_link_ksettings_add_link_mode(link_ksettings, advertising,
					     10000baseT_Full);

	trans_type = priv->port_state.transceiver;
	if (trans_type > 0 && trans_type <= 0xC) {
		link_ksettings->base.port = PORT_FIBRE;
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     supported, FIBRE);
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     advertising, FIBRE);
	} else if (trans_type == 0x80 || trans_type == 0) {
		link_ksettings->base.port = PORT_TP;
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     supported, TP);
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     advertising, TP);
	} else  {
		link_ksettings->base.port = -1;
	}
}

static int
mlx4_en_get_link_ksettings(struct net_device *dev,
			   struct ethtool_link_ksettings *link_ksettings)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	int ret = -EINVAL;

	if (mlx4_en_QUERY_PORT(priv->mdev, priv->port))
		return -ENOMEM;

	en_dbg(DRV, priv, "query port state.flags ANC(%x) ANE(%x)\n",
	       priv->port_state.flags & MLX4_EN_PORT_ANC,
	       priv->port_state.flags & MLX4_EN_PORT_ANE);

	if (priv->mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_ETH_PROT_CTRL)
		ret = ethtool_get_ptys_link_ksettings(dev, link_ksettings);
	if (ret) /* ETH PROT CRTL is not supported or PTYS CMD failed */
		ethtool_get_default_link_ksettings(dev, link_ksettings);

	if (netif_carrier_ok(dev)) {
		link_ksettings->base.speed = priv->port_state.link_speed;
		link_ksettings->base.duplex = DUPLEX_FULL;
	} else {
		link_ksettings->base.speed = SPEED_UNKNOWN;
		link_ksettings->base.duplex = DUPLEX_UNKNOWN;
	}
	return 0;
}

/* Calculate PTYS admin according ethtool speed (SPEED_XXX) */
static __be32 speed_set_ptys_admin(struct mlx4_en_priv *priv, u32 speed,
				   __be32 proto_cap)
{
	__be32 proto_admin = 0;

	if (!speed) { /* Speed = 0 ==> Reset Link modes */
		proto_admin = proto_cap;
		en_info(priv, "Speed was set to 0, Reset advertised Link Modes to default (%x)\n",
			be32_to_cpu(proto_cap));
	} else {
		u32 ptys_link_modes = speed2ptys_link_modes(speed);

		proto_admin = cpu_to_be32(ptys_link_modes) & proto_cap;
		en_info(priv, "Setting Speed to %d\n", speed);
	}
	return proto_admin;
}

static int
mlx4_en_set_link_ksettings(struct net_device *dev,
			   const struct ethtool_link_ksettings *link_ksettings)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_ptys_reg ptys_reg;
	__be32 proto_admin;
	u8 cur_autoneg;
	int ret;

	u32 ptys_adv = ethtool2ptys_link_modes(
		link_ksettings->link_modes.advertising, ADVERTISED);
	const int speed = link_ksettings->base.speed;

	en_dbg(DRV, priv,
	       "Set Speed=%d adv={%*pbl} autoneg=%d duplex=%d\n",
	       speed, __ETHTOOL_LINK_MODE_MASK_NBITS,
	       link_ksettings->link_modes.advertising,
	       link_ksettings->base.autoneg,
	       link_ksettings->base.duplex);

	if (!(priv->mdev->dev->caps.flags2 &
	      MLX4_DEV_CAP_FLAG2_ETH_PROT_CTRL) ||
	    (link_ksettings->base.duplex == DUPLEX_HALF))
		return -EINVAL;

	memset(&ptys_reg, 0, sizeof(ptys_reg));
	ptys_reg.local_port = priv->port;
	ptys_reg.proto_mask = MLX4_PTYS_EN;
	ret = mlx4_ACCESS_PTYS_REG(priv->mdev->dev,
				   MLX4_ACCESS_REG_QUERY, &ptys_reg);
	if (ret) {
		en_warn(priv, "Failed to QUERY mlx4_ACCESS_PTYS_REG status(%x)\n",
			ret);
		return 0;
	}

	cur_autoneg = ptys_reg.flags & MLX4_PTYS_AN_DISABLE_ADMIN ?
				AUTONEG_DISABLE : AUTONEG_ENABLE;

	if (link_ksettings->base.autoneg == AUTONEG_DISABLE) {
		proto_admin = speed_set_ptys_admin(priv, speed,
						   ptys_reg.eth_proto_cap);
		if ((be32_to_cpu(proto_admin) &
		     (MLX4_PROT_MASK(MLX4_1000BASE_CX_SGMII) |
		      MLX4_PROT_MASK(MLX4_1000BASE_KX))) &&
		    (ptys_reg.flags & MLX4_PTYS_AN_DISABLE_CAP))
			ptys_reg.flags |= MLX4_PTYS_AN_DISABLE_ADMIN;
	} else {
		proto_admin = cpu_to_be32(ptys_adv);
		ptys_reg.flags &= ~MLX4_PTYS_AN_DISABLE_ADMIN;
	}

	proto_admin &= ptys_reg.eth_proto_cap;
	if (!proto_admin) {
		en_warn(priv, "Not supported link mode(s) requested, check supported link modes.\n");
		return -EINVAL; /* nothing to change due to bad input */
	}

	if ((proto_admin == ptys_reg.eth_proto_admin) &&
	    ((ptys_reg.flags & MLX4_PTYS_AN_DISABLE_CAP) &&
	     (link_ksettings->base.autoneg == cur_autoneg)))
		return 0; /* Nothing to change */

	en_dbg(DRV, priv, "mlx4_ACCESS_PTYS_REG SET: ptys_reg.eth_proto_admin = 0x%x\n",
	       be32_to_cpu(proto_admin));

	ptys_reg.eth_proto_admin = proto_admin;
	ret = mlx4_ACCESS_PTYS_REG(priv->mdev->dev, MLX4_ACCESS_REG_WRITE,
				   &ptys_reg);
	if (ret) {
		en_warn(priv, "Failed to write mlx4_ACCESS_PTYS_REG eth_proto_admin(0x%x) status(0x%x)",
			be32_to_cpu(ptys_reg.eth_proto_admin), ret);
		return ret;
	}

	mutex_lock(&priv->mdev->state_lock);
	if (priv->port_up) {
		en_warn(priv, "Port link mode changed, restarting port...\n");
		mlx4_en_stop_port(dev, 1);
		if (mlx4_en_start_port(dev))
			en_err(priv, "Failed restarting port %d\n", priv->port);
	}
	mutex_unlock(&priv->mdev->state_lock);
	return 0;
}

static int mlx4_en_get_coalesce(struct net_device *dev,
			      struct ethtool_coalesce *coal)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);

	coal->tx_coalesce_usecs = priv->tx_usecs;
	coal->tx_max_coalesced_frames = priv->tx_frames;
	coal->tx_max_coalesced_frames_irq = priv->tx_work_limit;

	coal->rx_coalesce_usecs = priv->rx_usecs;
	coal->rx_max_coalesced_frames = priv->rx_frames;

	coal->pkt_rate_low = priv->pkt_rate_low;
	coal->rx_coalesce_usecs_low = priv->rx_usecs_low;
	coal->pkt_rate_high = priv->pkt_rate_high;
	coal->rx_coalesce_usecs_high = priv->rx_usecs_high;
	coal->rate_sample_interval = priv->sample_interval;
	coal->use_adaptive_rx_coalesce = priv->adaptive_rx_coal;

	return 0;
}

static int mlx4_en_set_coalesce(struct net_device *dev,
			      struct ethtool_coalesce *coal)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);

	if (!coal->tx_max_coalesced_frames_irq)
		return -EINVAL;

	if (coal->tx_coalesce_usecs > MLX4_EN_MAX_COAL_TIME ||
	    coal->rx_coalesce_usecs > MLX4_EN_MAX_COAL_TIME ||
	    coal->rx_coalesce_usecs_low > MLX4_EN_MAX_COAL_TIME ||
	    coal->rx_coalesce_usecs_high > MLX4_EN_MAX_COAL_TIME) {
		netdev_info(dev, "%s: maximum coalesce time supported is %d usecs\n",
			    __func__, MLX4_EN_MAX_COAL_TIME);
		return -ERANGE;
	}

	if (coal->tx_max_coalesced_frames > MLX4_EN_MAX_COAL_PKTS ||
	    coal->rx_max_coalesced_frames > MLX4_EN_MAX_COAL_PKTS) {
		netdev_info(dev, "%s: maximum coalesced frames supported is %d\n",
			    __func__, MLX4_EN_MAX_COAL_PKTS);
		return -ERANGE;
	}

	priv->rx_frames = (coal->rx_max_coalesced_frames ==
			   MLX4_EN_AUTO_CONF) ?
				MLX4_EN_RX_COAL_TARGET :
				coal->rx_max_coalesced_frames;
	priv->rx_usecs = (coal->rx_coalesce_usecs ==
			  MLX4_EN_AUTO_CONF) ?
				MLX4_EN_RX_COAL_TIME :
				coal->rx_coalesce_usecs;

	/* Setting TX coalescing parameters */
	if (coal->tx_coalesce_usecs != priv->tx_usecs ||
	    coal->tx_max_coalesced_frames != priv->tx_frames) {
		priv->tx_usecs = coal->tx_coalesce_usecs;
		priv->tx_frames = coal->tx_max_coalesced_frames;
	}

	/* Set adaptive coalescing params */
	priv->pkt_rate_low = coal->pkt_rate_low;
	priv->rx_usecs_low = coal->rx_coalesce_usecs_low;
	priv->pkt_rate_high = coal->pkt_rate_high;
	priv->rx_usecs_high = coal->rx_coalesce_usecs_high;
	priv->sample_interval = coal->rate_sample_interval;
	priv->adaptive_rx_coal = coal->use_adaptive_rx_coalesce;
	priv->tx_work_limit = coal->tx_max_coalesced_frames_irq;

	return mlx4_en_moderation_update(priv);
}

static int mlx4_en_set_pauseparam(struct net_device *dev,
				struct ethtool_pauseparam *pause)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	u8 tx_pause, tx_ppp, rx_pause, rx_ppp;
	int err;

	if (pause->autoneg)
		return -EINVAL;

	tx_pause = !!(pause->tx_pause);
	rx_pause = !!(pause->rx_pause);
	rx_ppp = (tx_pause || rx_pause) ? 0 : priv->prof->rx_ppp;
	tx_ppp = (tx_pause || rx_pause) ? 0 : priv->prof->tx_ppp;

	err = mlx4_SET_PORT_general(mdev->dev, priv->port,
				    priv->rx_skb_size + ETH_FCS_LEN,
				    tx_pause, tx_ppp, rx_pause, rx_ppp);
	if (err) {
		en_err(priv, "Failed setting pause params, err = %d\n", err);
		return err;
	}

	mlx4_en_update_pfc_stats_bitmap(mdev->dev, &priv->stats_bitmap,
					rx_ppp, rx_pause, tx_ppp, tx_pause);

	priv->prof->tx_pause = tx_pause;
	priv->prof->rx_pause = rx_pause;
	priv->prof->tx_ppp = tx_ppp;
	priv->prof->rx_ppp = rx_ppp;

	return err;
}

static void mlx4_en_get_pauseparam(struct net_device *dev,
				 struct ethtool_pauseparam *pause)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);

	pause->tx_pause = priv->prof->tx_pause;
	pause->rx_pause = priv->prof->rx_pause;
}

static int mlx4_en_set_ringparam(struct net_device *dev,
				 struct ethtool_ringparam *param)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_port_profile new_prof;
	struct mlx4_en_priv *tmp;
	u32 rx_size, tx_size;
	int port_up = 0;
	int err = 0;

	if (param->rx_jumbo_pending || param->rx_mini_pending)
		return -EINVAL;

	if (param->rx_pending < MLX4_EN_MIN_RX_SIZE) {
		en_warn(priv, "%s: rx_pending (%d) < min (%d)\n",
			__func__, param->rx_pending,
			MLX4_EN_MIN_RX_SIZE);
		return -EINVAL;
	}
	if (param->tx_pending < MLX4_EN_MIN_TX_SIZE) {
		en_warn(priv, "%s: tx_pending (%d) < min (%lu)\n",
			__func__, param->tx_pending,
			MLX4_EN_MIN_TX_SIZE);
		return -EINVAL;
	}

	rx_size = roundup_pow_of_two(param->rx_pending);
	tx_size = roundup_pow_of_two(param->tx_pending);

	if (rx_size == (priv->port_up ? priv->rx_ring[0]->actual_size :
					priv->rx_ring[0]->size) &&
	    tx_size == priv->tx_ring[TX][0]->size)
		return 0;

	tmp = kzalloc(sizeof(*tmp), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	mutex_lock(&mdev->state_lock);
	memcpy(&new_prof, priv->prof, sizeof(struct mlx4_en_port_profile));
	new_prof.tx_ring_size = tx_size;
	new_prof.rx_ring_size = rx_size;
	err = mlx4_en_try_alloc_resources(priv, tmp, &new_prof, true);
	if (err)
		goto out;

	if (priv->port_up) {
		port_up = 1;
		mlx4_en_stop_port(dev, 1);
	}

	mlx4_en_safe_replace_resources(priv, tmp);

	if (port_up) {
		err = mlx4_en_start_port(dev);
		if (err)
			en_err(priv, "Failed starting port\n");
	}

	err = mlx4_en_moderation_update(priv);
out:
	kfree(tmp);
	mutex_unlock(&mdev->state_lock);
	return err;
}

static void mlx4_en_get_ringparam(struct net_device *dev,
				  struct ethtool_ringparam *param)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);

	memset(param, 0, sizeof(*param));
	param->rx_max_pending = MLX4_EN_MAX_RX_SIZE;
	param->tx_max_pending = MLX4_EN_MAX_TX_SIZE;
	param->rx_pending = priv->port_up ?
		priv->rx_ring[0]->actual_size : priv->rx_ring[0]->size;
	param->tx_pending = priv->tx_ring[TX][0]->size;
}

static u32 mlx4_en_get_rxfh_indir_size(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);

	return rounddown_pow_of_two(priv->rx_ring_num);
}

static u32 mlx4_en_get_rxfh_key_size(struct net_device *netdev)
{
	return MLX4_EN_RSS_KEY_SIZE;
}

static int mlx4_en_check_rxfh_func(struct net_device *dev, u8 hfunc)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);

	/* check if requested function is supported by the device */
	if (hfunc == ETH_RSS_HASH_TOP) {
		if (!(priv->mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_RSS_TOP))
			return -EINVAL;
		if (!(dev->features & NETIF_F_RXHASH))
			en_warn(priv, "Toeplitz hash function should be used in conjunction with RX hashing for optimal performance\n");
		return 0;
	} else if (hfunc == ETH_RSS_HASH_XOR) {
		if (!(priv->mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_RSS_XOR))
			return -EINVAL;
		if (dev->features & NETIF_F_RXHASH)
			en_warn(priv, "Enabling both XOR Hash function and RX Hashing can limit RPS functionality\n");
		return 0;
	}

	return -EINVAL;
}

static int mlx4_en_get_rxfh(struct net_device *dev, u32 *ring_index, u8 *key,
			    u8 *hfunc)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	u32 n = mlx4_en_get_rxfh_indir_size(dev);
	u32 i, rss_rings;
	int err = 0;

	rss_rings = priv->prof->rss_rings ?: n;
	rss_rings = rounddown_pow_of_two(rss_rings);

	for (i = 0; i < n; i++) {
		if (!ring_index)
			break;
		ring_index[i] = i % rss_rings;
	}
	if (key)
		memcpy(key, priv->rss_key, MLX4_EN_RSS_KEY_SIZE);
	if (hfunc)
		*hfunc = priv->rss_hash_fn;
	return err;
}

static int mlx4_en_set_rxfh(struct net_device *dev, const u32 *ring_index,
			    const u8 *key, const u8 hfunc)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	u32 n = mlx4_en_get_rxfh_indir_size(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	int port_up = 0;
	int err = 0;
	int i;
	int rss_rings = 0;

	/* Calculate RSS table size and make sure flows are spread evenly
	 * between rings
	 */
	for (i = 0; i < n; i++) {
		if (!ring_index)
			break;
		if (i > 0 && !ring_index[i] && !rss_rings)
			rss_rings = i;

		if (ring_index[i] != (i % (rss_rings ?: n)))
			return -EINVAL;
	}

	if (!rss_rings)
		rss_rings = n;

	/* RSS table size must be an order of 2 */
	if (!is_power_of_2(rss_rings))
		return -EINVAL;

	if (hfunc != ETH_RSS_HASH_NO_CHANGE) {
		err = mlx4_en_check_rxfh_func(dev, hfunc);
		if (err)
			return err;
	}

	mutex_lock(&mdev->state_lock);
	if (priv->port_up) {
		port_up = 1;
		mlx4_en_stop_port(dev, 1);
	}

	if (ring_index)
		priv->prof->rss_rings = rss_rings;
	if (key)
		memcpy(priv->rss_key, key, MLX4_EN_RSS_KEY_SIZE);
	if (hfunc !=  ETH_RSS_HASH_NO_CHANGE)
		priv->rss_hash_fn = hfunc;

	if (port_up) {
		err = mlx4_en_start_port(dev);
		if (err)
			en_err(priv, "Failed starting port\n");
	}

	mutex_unlock(&mdev->state_lock);
	return err;
}

#define all_zeros_or_all_ones(field)		\
	((field) == 0 || (field) == (__force typeof(field))-1)

static int mlx4_en_validate_flow(struct net_device *dev,
				 struct ethtool_rxnfc *cmd)
{
	struct ethtool_usrip4_spec *l3_mask;
	struct ethtool_tcpip4_spec *l4_mask;
	struct ethhdr *eth_mask;

	if (cmd->fs.location >= MAX_NUM_OF_FS_RULES)
		return -EINVAL;

	if (cmd->fs.flow_type & FLOW_MAC_EXT) {
		/* dest mac mask must be ff:ff:ff:ff:ff:ff */
		if (!is_broadcast_ether_addr(cmd->fs.m_ext.h_dest))
			return -EINVAL;
	}

	switch (cmd->fs.flow_type & ~(FLOW_EXT | FLOW_MAC_EXT)) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
		if (cmd->fs.m_u.tcp_ip4_spec.tos)
			return -EINVAL;
		l4_mask = &cmd->fs.m_u.tcp_ip4_spec;
		/* don't allow mask which isn't all 0 or 1 */
		if (!all_zeros_or_all_ones(l4_mask->ip4src) ||
		    !all_zeros_or_all_ones(l4_mask->ip4dst) ||
		    !all_zeros_or_all_ones(l4_mask->psrc) ||
		    !all_zeros_or_all_ones(l4_mask->pdst))
			return -EINVAL;
		break;
	case IP_USER_FLOW:
		l3_mask = &cmd->fs.m_u.usr_ip4_spec;
		if (l3_mask->l4_4_bytes || l3_mask->tos || l3_mask->proto ||
		    cmd->fs.h_u.usr_ip4_spec.ip_ver != ETH_RX_NFC_IP4 ||
		    (!l3_mask->ip4src && !l3_mask->ip4dst) ||
		    !all_zeros_or_all_ones(l3_mask->ip4src) ||
		    !all_zeros_or_all_ones(l3_mask->ip4dst))
			return -EINVAL;
		break;
	case ETHER_FLOW:
		eth_mask = &cmd->fs.m_u.ether_spec;
		/* source mac mask must not be set */
		if (!is_zero_ether_addr(eth_mask->h_source))
			return -EINVAL;

		/* dest mac mask must be ff:ff:ff:ff:ff:ff */
		if (!is_broadcast_ether_addr(eth_mask->h_dest))
			return -EINVAL;

		if (!all_zeros_or_all_ones(eth_mask->h_proto))
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	if ((cmd->fs.flow_type & FLOW_EXT)) {
		if (cmd->fs.m_ext.vlan_etype ||
		    !((cmd->fs.m_ext.vlan_tci & cpu_to_be16(VLAN_VID_MASK)) ==
		      0 ||
		      (cmd->fs.m_ext.vlan_tci & cpu_to_be16(VLAN_VID_MASK)) ==
		      cpu_to_be16(VLAN_VID_MASK)))
			return -EINVAL;

		if (cmd->fs.m_ext.vlan_tci) {
			if (be16_to_cpu(cmd->fs.h_ext.vlan_tci) >= VLAN_N_VID)
				return -EINVAL;

		}
	}

	return 0;
}

static int mlx4_en_ethtool_add_mac_rule(struct ethtool_rxnfc *cmd,
					struct list_head *rule_list_h,
					struct mlx4_spec_list *spec_l2,
					unsigned char *mac)
{
	int err = 0;
	__be64 mac_msk = cpu_to_be64(MLX4_MAC_MASK << 16);

	spec_l2->id = MLX4_NET_TRANS_RULE_ID_ETH;
	memcpy(spec_l2->eth.dst_mac_msk, &mac_msk, ETH_ALEN);
	memcpy(spec_l2->eth.dst_mac, mac, ETH_ALEN);

	if ((cmd->fs.flow_type & FLOW_EXT) &&
	    (cmd->fs.m_ext.vlan_tci & cpu_to_be16(VLAN_VID_MASK))) {
		spec_l2->eth.vlan_id = cmd->fs.h_ext.vlan_tci;
		spec_l2->eth.vlan_id_msk = cpu_to_be16(VLAN_VID_MASK);
	}

	list_add_tail(&spec_l2->list, rule_list_h);

	return err;
}

static int mlx4_en_ethtool_add_mac_rule_by_ipv4(struct mlx4_en_priv *priv,
						struct ethtool_rxnfc *cmd,
						struct list_head *rule_list_h,
						struct mlx4_spec_list *spec_l2,
						__be32 ipv4_dst)
{
#ifdef CONFIG_INET
	unsigned char mac[ETH_ALEN];

	if (!ipv4_is_multicast(ipv4_dst)) {
		if (cmd->fs.flow_type & FLOW_MAC_EXT)
			memcpy(&mac, cmd->fs.h_ext.h_dest, ETH_ALEN);
		else
			memcpy(&mac, priv->dev->dev_addr, ETH_ALEN);
	} else {
		ip_eth_mc_map(ipv4_dst, mac);
	}

	return mlx4_en_ethtool_add_mac_rule(cmd, rule_list_h, spec_l2, &mac[0]);
#else
	return -EINVAL;
#endif
}

static int add_ip_rule(struct mlx4_en_priv *priv,
		       struct ethtool_rxnfc *cmd,
		       struct list_head *list_h)
{
	int err;
	struct mlx4_spec_list *spec_l2 = NULL;
	struct mlx4_spec_list *spec_l3 = NULL;
	struct ethtool_usrip4_spec *l3_mask = &cmd->fs.m_u.usr_ip4_spec;

	spec_l3 = kzalloc(sizeof(*spec_l3), GFP_KERNEL);
	spec_l2 = kzalloc(sizeof(*spec_l2), GFP_KERNEL);
	if (!spec_l2 || !spec_l3) {
		err = -ENOMEM;
		goto free_spec;
	}

	err = mlx4_en_ethtool_add_mac_rule_by_ipv4(priv, cmd, list_h, spec_l2,
						   cmd->fs.h_u.
						   usr_ip4_spec.ip4dst);
	if (err)
		goto free_spec;
	spec_l3->id = MLX4_NET_TRANS_RULE_ID_IPV4;
	spec_l3->ipv4.src_ip = cmd->fs.h_u.usr_ip4_spec.ip4src;
	if (l3_mask->ip4src)
		spec_l3->ipv4.src_ip_msk = EN_ETHTOOL_WORD_MASK;
	spec_l3->ipv4.dst_ip = cmd->fs.h_u.usr_ip4_spec.ip4dst;
	if (l3_mask->ip4dst)
		spec_l3->ipv4.dst_ip_msk = EN_ETHTOOL_WORD_MASK;
	list_add_tail(&spec_l3->list, list_h);

	return 0;

free_spec:
	kfree(spec_l2);
	kfree(spec_l3);
	return err;
}

static int add_tcp_udp_rule(struct mlx4_en_priv *priv,
			     struct ethtool_rxnfc *cmd,
			     struct list_head *list_h, int proto)
{
	int err;
	struct mlx4_spec_list *spec_l2 = NULL;
	struct mlx4_spec_list *spec_l3 = NULL;
	struct mlx4_spec_list *spec_l4 = NULL;
	struct ethtool_tcpip4_spec *l4_mask = &cmd->fs.m_u.tcp_ip4_spec;

	spec_l2 = kzalloc(sizeof(*spec_l2), GFP_KERNEL);
	spec_l3 = kzalloc(sizeof(*spec_l3), GFP_KERNEL);
	spec_l4 = kzalloc(sizeof(*spec_l4), GFP_KERNEL);
	if (!spec_l2 || !spec_l3 || !spec_l4) {
		err = -ENOMEM;
		goto free_spec;
	}

	spec_l3->id = MLX4_NET_TRANS_RULE_ID_IPV4;

	if (proto == TCP_V4_FLOW) {
		err = mlx4_en_ethtool_add_mac_rule_by_ipv4(priv, cmd, list_h,
							   spec_l2,
							   cmd->fs.h_u.
							   tcp_ip4_spec.ip4dst);
		if (err)
			goto free_spec;
		spec_l4->id = MLX4_NET_TRANS_RULE_ID_TCP;
		spec_l3->ipv4.src_ip = cmd->fs.h_u.tcp_ip4_spec.ip4src;
		spec_l3->ipv4.dst_ip = cmd->fs.h_u.tcp_ip4_spec.ip4dst;
		spec_l4->tcp_udp.src_port = cmd->fs.h_u.tcp_ip4_spec.psrc;
		spec_l4->tcp_udp.dst_port = cmd->fs.h_u.tcp_ip4_spec.pdst;
	} else {
		err = mlx4_en_ethtool_add_mac_rule_by_ipv4(priv, cmd, list_h,
							   spec_l2,
							   cmd->fs.h_u.
							   udp_ip4_spec.ip4dst);
		if (err)
			goto free_spec;
		spec_l4->id = MLX4_NET_TRANS_RULE_ID_UDP;
		spec_l3->ipv4.src_ip = cmd->fs.h_u.udp_ip4_spec.ip4src;
		spec_l3->ipv4.dst_ip = cmd->fs.h_u.udp_ip4_spec.ip4dst;
		spec_l4->tcp_udp.src_port = cmd->fs.h_u.udp_ip4_spec.psrc;
		spec_l4->tcp_udp.dst_port = cmd->fs.h_u.udp_ip4_spec.pdst;
	}

	if (l4_mask->ip4src)
		spec_l3->ipv4.src_ip_msk = EN_ETHTOOL_WORD_MASK;
	if (l4_mask->ip4dst)
		spec_l3->ipv4.dst_ip_msk = EN_ETHTOOL_WORD_MASK;

	if (l4_mask->psrc)
		spec_l4->tcp_udp.src_port_msk = EN_ETHTOOL_SHORT_MASK;
	if (l4_mask->pdst)
		spec_l4->tcp_udp.dst_port_msk = EN_ETHTOOL_SHORT_MASK;

	list_add_tail(&spec_l3->list, list_h);
	list_add_tail(&spec_l4->list, list_h);

	return 0;

free_spec:
	kfree(spec_l2);
	kfree(spec_l3);
	kfree(spec_l4);
	return err;
}

static int mlx4_en_ethtool_to_net_trans_rule(struct net_device *dev,
					     struct ethtool_rxnfc *cmd,
					     struct list_head *rule_list_h)
{
	int err;
	struct ethhdr *eth_spec;
	struct mlx4_spec_list *spec_l2;
	struct mlx4_en_priv *priv = netdev_priv(dev);

	err = mlx4_en_validate_flow(dev, cmd);
	if (err)
		return err;

	switch (cmd->fs.flow_type & ~(FLOW_EXT | FLOW_MAC_EXT)) {
	case ETHER_FLOW:
		spec_l2 = kzalloc(sizeof(*spec_l2), GFP_KERNEL);
		if (!spec_l2)
			return -ENOMEM;

		eth_spec = &cmd->fs.h_u.ether_spec;
		mlx4_en_ethtool_add_mac_rule(cmd, rule_list_h, spec_l2,
					     &eth_spec->h_dest[0]);
		spec_l2->eth.ether_type = eth_spec->h_proto;
		if (eth_spec->h_proto)
			spec_l2->eth.ether_type_enable = 1;
		break;
	case IP_USER_FLOW:
		err = add_ip_rule(priv, cmd, rule_list_h);
		break;
	case TCP_V4_FLOW:
		err = add_tcp_udp_rule(priv, cmd, rule_list_h, TCP_V4_FLOW);
		break;
	case UDP_V4_FLOW:
		err = add_tcp_udp_rule(priv, cmd, rule_list_h, UDP_V4_FLOW);
		break;
	}

	return err;
}

static int mlx4_en_flow_replace(struct net_device *dev,
				struct ethtool_rxnfc *cmd)
{
	int err;
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct ethtool_flow_id *loc_rule;
	struct mlx4_spec_list *spec, *tmp_spec;
	u32 qpn;
	u64 reg_id;

	struct mlx4_net_trans_rule rule = {
		.queue_mode = MLX4_NET_TRANS_Q_FIFO,
		.exclusive = 0,
		.allow_loopback = 1,
		.promisc_mode = MLX4_FS_REGULAR,
	};

	rule.port = priv->port;
	rule.priority = MLX4_DOMAIN_ETHTOOL | cmd->fs.location;
	INIT_LIST_HEAD(&rule.list);

	/* Allow direct QP attaches if the EN_ETHTOOL_QP_ATTACH flag is set */
	if (cmd->fs.ring_cookie == RX_CLS_FLOW_DISC)
		qpn = priv->drop_qp.qpn;
	else if (cmd->fs.ring_cookie & EN_ETHTOOL_QP_ATTACH) {
		qpn = cmd->fs.ring_cookie & (EN_ETHTOOL_QP_ATTACH - 1);
	} else {
		if (cmd->fs.ring_cookie >= priv->rx_ring_num) {
			en_warn(priv, "rxnfc: RX ring (%llu) doesn't exist\n",
				cmd->fs.ring_cookie);
			return -EINVAL;
		}
		qpn = priv->rss_map.qps[cmd->fs.ring_cookie].qpn;
		if (!qpn) {
			en_warn(priv, "rxnfc: RX ring (%llu) is inactive\n",
				cmd->fs.ring_cookie);
			return -EINVAL;
		}
	}
	rule.qpn = qpn;
	err = mlx4_en_ethtool_to_net_trans_rule(dev, cmd, &rule.list);
	if (err)
		goto out_free_list;

	loc_rule = &priv->ethtool_rules[cmd->fs.location];
	if (loc_rule->id) {
		err = mlx4_flow_detach(priv->mdev->dev, loc_rule->id);
		if (err) {
			en_err(priv, "Fail to detach network rule at location %d. registration id = %llx\n",
			       cmd->fs.location, loc_rule->id);
			goto out_free_list;
		}
		loc_rule->id = 0;
		memset(&loc_rule->flow_spec, 0,
		       sizeof(struct ethtool_rx_flow_spec));
		list_del(&loc_rule->list);
	}
	err = mlx4_flow_attach(priv->mdev->dev, &rule, &reg_id);
	if (err) {
		en_err(priv, "Fail to attach network rule at location %d\n",
		       cmd->fs.location);
		goto out_free_list;
	}
	loc_rule->id = reg_id;
	memcpy(&loc_rule->flow_spec, &cmd->fs,
	       sizeof(struct ethtool_rx_flow_spec));
	list_add_tail(&loc_rule->list, &priv->ethtool_list);

out_free_list:
	list_for_each_entry_safe(spec, tmp_spec, &rule.list, list) {
		list_del(&spec->list);
		kfree(spec);
	}
	return err;
}

static int mlx4_en_flow_detach(struct net_device *dev,
			       struct ethtool_rxnfc *cmd)
{
	int err = 0;
	struct ethtool_flow_id *rule;
	struct mlx4_en_priv *priv = netdev_priv(dev);

	if (cmd->fs.location >= MAX_NUM_OF_FS_RULES)
		return -EINVAL;

	rule = &priv->ethtool_rules[cmd->fs.location];
	if (!rule->id) {
		err =  -ENOENT;
		goto out;
	}

	err = mlx4_flow_detach(priv->mdev->dev, rule->id);
	if (err) {
		en_err(priv, "Fail to detach network rule at location %d. registration id = 0x%llx\n",
		       cmd->fs.location, rule->id);
		goto out;
	}
	rule->id = 0;
	memset(&rule->flow_spec, 0, sizeof(struct ethtool_rx_flow_spec));
	list_del(&rule->list);
out:
	return err;

}

static int mlx4_en_get_flow(struct net_device *dev, struct ethtool_rxnfc *cmd,
			    int loc)
{
	int err = 0;
	struct ethtool_flow_id *rule;
	struct mlx4_en_priv *priv = netdev_priv(dev);

	if (loc < 0 || loc >= MAX_NUM_OF_FS_RULES)
		return -EINVAL;

	rule = &priv->ethtool_rules[loc];
	if (rule->id)
		memcpy(&cmd->fs, &rule->flow_spec,
		       sizeof(struct ethtool_rx_flow_spec));
	else
		err = -ENOENT;

	return err;
}

static int mlx4_en_get_num_flows(struct mlx4_en_priv *priv)
{

	int i, res = 0;
	for (i = 0; i < MAX_NUM_OF_FS_RULES; i++) {
		if (priv->ethtool_rules[i].id)
			res++;
	}
	return res;

}

static int mlx4_en_get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd,
			     u32 *rule_locs)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	int err = 0;
	int i = 0, priority = 0;

	if ((cmd->cmd == ETHTOOL_GRXCLSRLCNT ||
	     cmd->cmd == ETHTOOL_GRXCLSRULE ||
	     cmd->cmd == ETHTOOL_GRXCLSRLALL) &&
	    (mdev->dev->caps.steering_mode !=
	     MLX4_STEERING_MODE_DEVICE_MANAGED || !priv->port_up))
		return -EINVAL;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = priv->rx_ring_num;
		break;
	case ETHTOOL_GRXCLSRLCNT:
		cmd->rule_cnt = mlx4_en_get_num_flows(priv);
		break;
	case ETHTOOL_GRXCLSRULE:
		err = mlx4_en_get_flow(dev, cmd, cmd->fs.location);
		break;
	case ETHTOOL_GRXCLSRLALL:
		while ((!err || err == -ENOENT) && priority < cmd->rule_cnt) {
			err = mlx4_en_get_flow(dev, cmd, i);
			if (!err)
				rule_locs[priority++] = i;
			i++;
		}
		err = 0;
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int mlx4_en_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd)
{
	int err = 0;
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;

	if (mdev->dev->caps.steering_mode !=
	    MLX4_STEERING_MODE_DEVICE_MANAGED || !priv->port_up)
		return -EINVAL;

	switch (cmd->cmd) {
	case ETHTOOL_SRXCLSRLINS:
		err = mlx4_en_flow_replace(dev, cmd);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		err = mlx4_en_flow_detach(dev, cmd);
		break;
	default:
		en_warn(priv, "Unsupported ethtool command. (%d)\n", cmd->cmd);
		return -EINVAL;
	}

	return err;
}

static int mlx4_en_get_max_num_rx_rings(struct net_device *dev)
{
	return min_t(int, num_online_cpus(), MAX_RX_RINGS);
}

static void mlx4_en_get_channels(struct net_device *dev,
				 struct ethtool_channels *channel)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);

	channel->max_rx = mlx4_en_get_max_num_rx_rings(dev);
	channel->max_tx = priv->mdev->profile.max_num_tx_rings_p_up;

	channel->rx_count = priv->rx_ring_num;
	channel->tx_count = priv->tx_ring_num[TX] /
			    priv->prof->num_up;
}

static int mlx4_en_set_channels(struct net_device *dev,
				struct ethtool_channels *channel)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_port_profile new_prof;
	struct mlx4_en_priv *tmp;
	int port_up = 0;
	int xdp_count;
	int err = 0;
	u8 up;

	if (!channel->tx_count || !channel->rx_count)
		return -EINVAL;

	tmp = kzalloc(sizeof(*tmp), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	mutex_lock(&mdev->state_lock);
	xdp_count = priv->tx_ring_num[TX_XDP] ? channel->rx_count : 0;
	if (channel->tx_count * priv->prof->num_up + xdp_count >
	    priv->mdev->profile.max_num_tx_rings_p_up * priv->prof->num_up) {
		err = -EINVAL;
		en_err(priv,
		       "Total number of TX and XDP rings (%d) exceeds the maximum supported (%d)\n",
		       channel->tx_count * priv->prof->num_up  + xdp_count,
		       MAX_TX_RINGS);
		goto out;
	}

	memcpy(&new_prof, priv->prof, sizeof(struct mlx4_en_port_profile));
	new_prof.num_tx_rings_p_up = channel->tx_count;
	new_prof.tx_ring_num[TX] = channel->tx_count * priv->prof->num_up;
	new_prof.tx_ring_num[TX_XDP] = xdp_count;
	new_prof.rx_ring_num = channel->rx_count;

	err = mlx4_en_try_alloc_resources(priv, tmp, &new_prof, true);
	if (err)
		goto out;

	if (priv->port_up) {
		port_up = 1;
		mlx4_en_stop_port(dev, 1);
	}

	mlx4_en_safe_replace_resources(priv, tmp);

	netif_set_real_num_rx_queues(dev, priv->rx_ring_num);

	up = (priv->prof->num_up == MLX4_EN_NUM_UP_LOW) ?
				    0 : priv->prof->num_up;
	mlx4_en_setup_tc(dev, up);

	en_warn(priv, "Using %d TX rings\n", priv->tx_ring_num[TX]);
	en_warn(priv, "Using %d RX rings\n", priv->rx_ring_num);

	if (port_up) {
		err = mlx4_en_start_port(dev);
		if (err)
			en_err(priv, "Failed starting port\n");
	}

	err = mlx4_en_moderation_update(priv);
out:
	mutex_unlock(&mdev->state_lock);
	kfree(tmp);
	return err;
}

static int mlx4_en_get_ts_info(struct net_device *dev,
			       struct ethtool_ts_info *info)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	int ret;

	ret = ethtool_op_get_ts_info(dev, info);
	if (ret)
		return ret;

	if (mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_TS) {
		info->so_timestamping |=
			SOF_TIMESTAMPING_TX_HARDWARE |
			SOF_TIMESTAMPING_RX_HARDWARE |
			SOF_TIMESTAMPING_RAW_HARDWARE;

		info->tx_types =
			(1 << HWTSTAMP_TX_OFF) |
			(1 << HWTSTAMP_TX_ON);

		info->rx_filters =
			(1 << HWTSTAMP_FILTER_NONE) |
			(1 << HWTSTAMP_FILTER_ALL);

		if (mdev->ptp_clock)
			info->phc_index = ptp_clock_index(mdev->ptp_clock);
	}

	return ret;
}

static int mlx4_en_set_priv_flags(struct net_device *dev, u32 flags)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	bool bf_enabled_new = !!(flags & MLX4_EN_PRIV_FLAGS_BLUEFLAME);
	bool bf_enabled_old = !!(priv->pflags & MLX4_EN_PRIV_FLAGS_BLUEFLAME);
	bool phv_enabled_new = !!(flags & MLX4_EN_PRIV_FLAGS_PHV);
	bool phv_enabled_old = !!(priv->pflags & MLX4_EN_PRIV_FLAGS_PHV);
	int i;
	int ret = 0;

	if (bf_enabled_new != bf_enabled_old) {
		int t;

		if (bf_enabled_new) {
			bool bf_supported = true;

			for (t = 0; t < MLX4_EN_NUM_TX_TYPES; t++)
				for (i = 0; i < priv->tx_ring_num[t]; i++)
					bf_supported &=
						priv->tx_ring[t][i]->bf_alloced;

			if (!bf_supported) {
				en_err(priv, "BlueFlame is not supported\n");
				return -EINVAL;
			}

			priv->pflags |= MLX4_EN_PRIV_FLAGS_BLUEFLAME;
		} else {
			priv->pflags &= ~MLX4_EN_PRIV_FLAGS_BLUEFLAME;
		}

		for (t = 0; t < MLX4_EN_NUM_TX_TYPES; t++)
			for (i = 0; i < priv->tx_ring_num[t]; i++)
				priv->tx_ring[t][i]->bf_enabled =
					bf_enabled_new;

		en_info(priv, "BlueFlame %s\n",
			bf_enabled_new ?  "Enabled" : "Disabled");
	}

	if (phv_enabled_new != phv_enabled_old) {
		ret = set_phv_bit(mdev->dev, priv->port, (int)phv_enabled_new);
		if (ret)
			return ret;
		else if (phv_enabled_new)
			priv->pflags |= MLX4_EN_PRIV_FLAGS_PHV;
		else
			priv->pflags &= ~MLX4_EN_PRIV_FLAGS_PHV;
		en_info(priv, "PHV bit %s\n",
			phv_enabled_new ?  "Enabled" : "Disabled");
	}
	return 0;
}

static u32 mlx4_en_get_priv_flags(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);

	return priv->pflags;
}

static int mlx4_en_get_tunable(struct net_device *dev,
			       const struct ethtool_tunable *tuna,
			       void *data)
{
	const struct mlx4_en_priv *priv = netdev_priv(dev);
	int ret = 0;

	switch (tuna->id) {
	case ETHTOOL_TX_COPYBREAK:
		*(u32 *)data = priv->prof->inline_thold;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int mlx4_en_set_tunable(struct net_device *dev,
			       const struct ethtool_tunable *tuna,
			       const void *data)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	int val, ret = 0;

	switch (tuna->id) {
	case ETHTOOL_TX_COPYBREAK:
		val = *(u32 *)data;
		if (val < MIN_PKT_LEN || val > MAX_INLINE)
			ret = -EINVAL;
		else
			priv->prof->inline_thold = val;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

#define MLX4_EEPROM_PAGE_LEN 256

static int mlx4_en_get_module_info(struct net_device *dev,
				   struct ethtool_modinfo *modinfo)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	int ret;
	u8 data[4];

	/* Read first 2 bytes to get Module & REV ID */
	ret = mlx4_get_module_info(mdev->dev, priv->port,
				   0/*offset*/, 2/*size*/, data);
	if (ret < 2)
		return -EIO;

	switch (data[0] /* identifier */) {
	case MLX4_MODULE_ID_QSFP:
		modinfo->type = ETH_MODULE_SFF_8436;
		modinfo->eeprom_len = ETH_MODULE_SFF_8436_LEN;
		break;
	case MLX4_MODULE_ID_QSFP_PLUS:
		if (data[1] >= 0x3) { /* revision id */
			modinfo->type = ETH_MODULE_SFF_8636;
			modinfo->eeprom_len = ETH_MODULE_SFF_8636_LEN;
		} else {
			modinfo->type = ETH_MODULE_SFF_8436;
			modinfo->eeprom_len = ETH_MODULE_SFF_8436_LEN;
		}
		break;
	case MLX4_MODULE_ID_QSFP28:
		modinfo->type = ETH_MODULE_SFF_8636;
		modinfo->eeprom_len = ETH_MODULE_SFF_8636_LEN;
		break;
	case MLX4_MODULE_ID_SFP:
		modinfo->type = ETH_MODULE_SFF_8472;
		modinfo->eeprom_len = MLX4_EEPROM_PAGE_LEN;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int mlx4_en_get_module_eeprom(struct net_device *dev,
				     struct ethtool_eeprom *ee,
				     u8 *data)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	int offset = ee->offset;
	int i = 0, ret;

	if (ee->len == 0)
		return -EINVAL;

	memset(data, 0, ee->len);

	while (i < ee->len) {
		en_dbg(DRV, priv,
		       "mlx4_get_module_info i(%d) offset(%d) len(%d)\n",
		       i, offset, ee->len - i);

		ret = mlx4_get_module_info(mdev->dev, priv->port,
					   offset, ee->len - i, data + i);

		if (!ret) /* Done reading */
			return 0;

		if (ret < 0) {
			en_err(priv,
			       "mlx4_get_module_info i(%d) offset(%d) bytes_to_read(%d) - FAILED (0x%x)\n",
			       i, offset, ee->len - i, ret);
			return 0;
		}

		i += ret;
		offset += ret;
	}
	return 0;
}

static int mlx4_en_set_phys_id(struct net_device *dev,
			       enum ethtool_phys_id_state state)
{
	int err;
	u16 beacon_duration;
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;

	if (!(mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_PORT_BEACON))
		return -EOPNOTSUPP;

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		beacon_duration = PORT_BEACON_MAX_LIMIT;
		break;
	case ETHTOOL_ID_INACTIVE:
		beacon_duration = 0;
		break;
	default:
		return -EOPNOTSUPP;
	}

	err = mlx4_SET_PORT_BEACON(mdev->dev, priv->port, beacon_duration);
	return err;
}

const struct ethtool_ops mlx4_en_ethtool_ops = {
	.get_drvinfo = mlx4_en_get_drvinfo,
	.get_link_ksettings = mlx4_en_get_link_ksettings,
	.set_link_ksettings = mlx4_en_set_link_ksettings,
	.get_link = ethtool_op_get_link,
	.get_strings = mlx4_en_get_strings,
	.get_sset_count = mlx4_en_get_sset_count,
	.get_ethtool_stats = mlx4_en_get_ethtool_stats,
	.self_test = mlx4_en_self_test,
	.set_phys_id = mlx4_en_set_phys_id,
	.get_wol = mlx4_en_get_wol,
	.set_wol = mlx4_en_set_wol,
	.get_msglevel = mlx4_en_get_msglevel,
	.set_msglevel = mlx4_en_set_msglevel,
	.get_coalesce = mlx4_en_get_coalesce,
	.set_coalesce = mlx4_en_set_coalesce,
	.get_pauseparam = mlx4_en_get_pauseparam,
	.set_pauseparam = mlx4_en_set_pauseparam,
	.get_ringparam = mlx4_en_get_ringparam,
	.set_ringparam = mlx4_en_set_ringparam,
	.get_rxnfc = mlx4_en_get_rxnfc,
	.set_rxnfc = mlx4_en_set_rxnfc,
	.get_rxfh_indir_size = mlx4_en_get_rxfh_indir_size,
	.get_rxfh_key_size = mlx4_en_get_rxfh_key_size,
	.get_rxfh = mlx4_en_get_rxfh,
	.set_rxfh = mlx4_en_set_rxfh,
	.get_channels = mlx4_en_get_channels,
	.set_channels = mlx4_en_set_channels,
	.get_ts_info = mlx4_en_get_ts_info,
	.set_priv_flags = mlx4_en_set_priv_flags,
	.get_priv_flags = mlx4_en_get_priv_flags,
	.get_tunable		= mlx4_en_get_tunable,
	.set_tunable		= mlx4_en_set_tunable,
	.get_module_info = mlx4_en_get_module_info,
	.get_module_eeprom = mlx4_en_get_module_eeprom
};





