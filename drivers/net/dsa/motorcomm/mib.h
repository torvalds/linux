/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2026 David Yang
 */

#ifndef _YT_MIB_H
#define _YT_MIB_H

#include <linux/u64_stats_sync.h>

#include <net/dsa.h>

#define YT921X_MIB_CTRL			0xc0004
#define  YT921X_MIB_CTRL_CLEAN			BIT(30)
#define  YT921X_MIB_CTRL_PORT_M			GENMASK(6, 3)
#define   YT921X_MIB_CTRL_PORT(x)			FIELD_PREP(YT921X_MIB_CTRL_PORT_M, (x))
#define  YT921X_MIB_CTRL_ONE_PORT		BIT(1)
#define  YT921X_MIB_CTRL_ALL_PORT		BIT(0)
#define YT921X_MIBn_DATA0(port)		(0xc0100 + 0x100 * (port))
#define YT921X_MIBn_DATAm(port, x)	(YT921X_MIBn_DATA0(port) + 4 * (x))
#define  YT921X_MIB_DATA_RX_BROADCAST		0x00
#define  YT921X_MIB_DATA_RX_PAUSE		0x04
#define  YT921X_MIB_DATA_RX_MULTICAST		0x08
#define  YT921X_MIB_DATA_RX_CRC_ERR		0x0c

#define  YT921X_MIB_DATA_RX_ALIGN_ERR		0x10
#define  YT921X_MIB_DATA_RX_UNDERSIZE_ERR	0x14
#define  YT921X_MIB_DATA_RX_FRAG_ERR		0x18
#define  YT921X_MIB_DATA_RX_PKT_SZ_64		0x1c

#define  YT921X_MIB_DATA_RX_PKT_SZ_65_TO_127	0x20
#define  YT921X_MIB_DATA_RX_PKT_SZ_128_TO_255	0x24
#define  YT921X_MIB_DATA_RX_PKT_SZ_256_TO_511	0x28
#define  YT921X_MIB_DATA_RX_PKT_SZ_512_TO_1023	0x2c

#define  YT921X_MIB_DATA_RX_PKT_SZ_1024_TO_1518	0x30
#define  YT921X_MIB_DATA_RX_PKT_SZ_1519_TO_MAX	0x34
/* 0x38: unused */
#define  YT921X_MIB_DATA_RX_GOOD_BYTES		0x3c

/* 0x40: 64 bytes */
#define  YT921X_MIB_DATA_RX_BAD_BYTES		0x44
/* 0x48: 64 bytes */
#define  YT921X_MIB_DATA_RX_OVERSIZE_ERR	0x4c

#define  YT921X_MIB_DATA_RX_DROPPED		0x50
#define  YT921X_MIB_DATA_TX_BROADCAST		0x54
#define  YT921X_MIB_DATA_TX_PAUSE		0x58
#define  YT921X_MIB_DATA_TX_MULTICAST		0x5c

#define  YT921X_MIB_DATA_TX_UNDERSIZE_ERR	0x60
#define  YT921X_MIB_DATA_TX_PKT_SZ_64		0x64
#define  YT921X_MIB_DATA_TX_PKT_SZ_65_TO_127	0x68
#define  YT921X_MIB_DATA_TX_PKT_SZ_128_TO_255	0x6c

#define  YT921X_MIB_DATA_TX_PKT_SZ_256_TO_511	0x70
#define  YT921X_MIB_DATA_TX_PKT_SZ_512_TO_1023	0x74
#define  YT921X_MIB_DATA_TX_PKT_SZ_1024_TO_1518	0x78
#define  YT921X_MIB_DATA_TX_PKT_SZ_1519_TO_MAX	0x7c

/* 0x80: unused */
#define  YT921X_MIB_DATA_TX_GOOD_BYTES		0x84
/* 0x88: 64 bytes */
#define  YT921X_MIB_DATA_TX_COLLISION		0x8c

#define  YT921X_MIB_DATA_TX_EXCESSIVE_COLLISION	0x90
#define  YT921X_MIB_DATA_TX_MULTIPLE_COLLISION	0x94
#define  YT921X_MIB_DATA_TX_SINGLE_COLLISION	0x98
#define  YT921X_MIB_DATA_TX_PKT			0x9c

#define  YT921X_MIB_DATA_TX_DEFERRED		0xa0
#define  YT921X_MIB_DATA_TX_LATE_COLLISION	0xa4
#define  YT921X_MIB_DATA_RX_OAM			0xa8
#define  YT921X_MIB_DATA_TX_OAM			0xac

struct yt921x_mib_stats {
	u64_stats_t rx_broadcast;
	u64_stats_t rx_pause;
	u64_stats_t rx_multicast;
	u64_stats_t rx_crc_errors;

	u64_stats_t rx_alignment_errors;
	u64_stats_t rx_undersize_errors;
	u64_stats_t rx_fragment_errors;
	u64_stats_t rx_64byte;

	u64_stats_t rx_65_127byte;
	u64_stats_t rx_128_255byte;
	u64_stats_t rx_256_511byte;
	u64_stats_t rx_512_1023byte;

	u64_stats_t rx_1024_1518byte;
	u64_stats_t rx_jumbo;
	u64_stats_t rx_good_bytes;

	u64_stats_t rx_bad_bytes;
	u64_stats_t rx_oversize_errors;

	u64_stats_t rx_dropped;
	u64_stats_t tx_broadcast;
	u64_stats_t tx_pause;
	u64_stats_t tx_multicast;

	u64_stats_t tx_undersize_errors;
	u64_stats_t tx_64byte;
	u64_stats_t tx_65_127byte;
	u64_stats_t tx_128_255byte;

	u64_stats_t tx_256_511byte;
	u64_stats_t tx_512_1023byte;
	u64_stats_t tx_1024_1518byte;
	u64_stats_t tx_jumbo;

	u64_stats_t tx_good_bytes;
	u64_stats_t tx_collisions;

	u64_stats_t tx_aborted_errors;
	u64_stats_t tx_multiple_collisions;
	u64_stats_t tx_single_collisions;
	u64_stats_t tx_good;

	u64_stats_t tx_deferred;
	u64_stats_t tx_late_collisions;
	u64_stats_t rx_oam;
	u64_stats_t tx_oam;
};

#define YT921X_MIB_STAT_NUM	(sizeof(struct yt921x_mib_stats) / sizeof(u64_stats_t))

struct yt921x_mib {
	struct yt921x_port *port;

	struct delayed_work work;
	struct u64_stats_sync syncp;
	/* writers: priv->reg_lock + syncp; readers: priv->reg_lock or syncp */
	struct yt921x_mib_stats stats;
	u64_stats_t rx_frames;
	u64_stats_t tx_frames;
	/* protected by priv->reg_lock */
	u64 data[YT921X_MIB_STAT_NUM];
};

void yt921x_mib_poll(struct work_struct *work);
void
yt921x_dsa_get_strings(struct dsa_switch *ds, int port, u32 stringset,
		       uint8_t *data);
void
yt921x_dsa_get_ethtool_stats(struct dsa_switch *ds, int port, uint64_t *data);
int yt921x_dsa_get_sset_count(struct dsa_switch *ds, int port, int sset);
void
yt921x_dsa_get_eth_mac_stats(struct dsa_switch *ds, int port,
			     struct ethtool_eth_mac_stats *mac_stats);
void
yt921x_dsa_get_eth_ctrl_stats(struct dsa_switch *ds, int port,
			      struct ethtool_eth_ctrl_stats *ctrl_stats);
void
yt921x_dsa_get_rmon_stats(struct dsa_switch *ds, int port,
			  struct ethtool_rmon_stats *rmon_stats,
			  const struct ethtool_rmon_hist_range **ranges);
void
yt921x_dsa_get_stats64(struct dsa_switch *ds, int port,
		       struct rtnl_link_stats64 *stats);
void
yt921x_dsa_get_pause_stats(struct dsa_switch *ds, int port,
			   struct ethtool_pause_stats *pause_stats);

#endif
