/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver for Alibaba Elastic Ethernet Adapter.
 *
 * Copyright (C) 2025 Alibaba Inc.
 */

#ifndef __EEA_ETHTOOL_H__
#define __EEA_ETHTOOL_H__

struct eea_tx_stats {
	struct u64_stats_sync syncp;
	u64_stats_t descs;
	u64_stats_t packets;
	u64_stats_t bytes;
	u64_stats_t drops;
	u64_stats_t kicks;
};

struct eea_rx_ctx_stats {
	u64 descs;
	u64 packets;
	u64 bytes;
	u64 drops;
	u64 split_hdr_bytes;
	u64 split_hdr_packets;
	u64 kicks;
	u64 length_errors;
};

struct eea_rx_stats {
	struct u64_stats_sync syncp;
	u64_stats_t descs;
	u64_stats_t packets;
	u64_stats_t bytes;
	u64_stats_t drops;
	u64_stats_t kicks;
	u64_stats_t split_hdr_bytes;
	u64_stats_t split_hdr_packets;

	u64_stats_t length_errors;
};

void eea_update_rx_stats(struct eea_rx_stats *rx_stats,
			 struct eea_rx_ctx_stats *stats);

extern const struct ethtool_ops eea_ethtool_ops;
#endif
