/* SPDX-License-Identifier: GPL-2.0 */
/* Texas Instruments ICSSG Ethernet driver
 *
 * Copyright (C) 2018-2022 Texas Instruments Incorporated - https://www.ti.com/
 *
 */

#ifndef __NET_TI_ICSSG_STATS_H
#define __NET_TI_ICSSG_STATS_H

#include "icssg_prueth.h"

#define STATS_TIME_LIMIT_1G_MS    25000    /* 25 seconds @ 1G */

struct miig_stats_regs {
	/* Rx */
	u32 rx_packets;
	u32 rx_broadcast_frames;
	u32 rx_multicast_frames;
	u32 rx_crc_errors;
	u32 rx_mii_error_frames;
	u32 rx_odd_nibble_frames;
	u32 rx_frame_max_size;
	u32 rx_max_size_error_frames;
	u32 rx_frame_min_size;
	u32 rx_min_size_error_frames;
	u32 rx_over_errors;
	u32 rx_class0_hits;
	u32 rx_class1_hits;
	u32 rx_class2_hits;
	u32 rx_class3_hits;
	u32 rx_class4_hits;
	u32 rx_class5_hits;
	u32 rx_class6_hits;
	u32 rx_class7_hits;
	u32 rx_class8_hits;
	u32 rx_class9_hits;
	u32 rx_class10_hits;
	u32 rx_class11_hits;
	u32 rx_class12_hits;
	u32 rx_class13_hits;
	u32 rx_class14_hits;
	u32 rx_class15_hits;
	u32 rx_smd_frags;
	u32 rx_bucket1_size;
	u32 rx_bucket2_size;
	u32 rx_bucket3_size;
	u32 rx_bucket4_size;
	u32 rx_64B_frames;
	u32 rx_bucket1_frames;
	u32 rx_bucket2_frames;
	u32 rx_bucket3_frames;
	u32 rx_bucket4_frames;
	u32 rx_bucket5_frames;
	u32 rx_bytes;
	u32 rx_tx_total_bytes;
	/* Tx */
	u32 tx_packets;
	u32 tx_broadcast_frames;
	u32 tx_multicast_frames;
	u32 tx_odd_nibble_frames;
	u32 tx_underflow_errors;
	u32 tx_frame_max_size;
	u32 tx_max_size_error_frames;
	u32 tx_frame_min_size;
	u32 tx_min_size_error_frames;
	u32 tx_bucket1_size;
	u32 tx_bucket2_size;
	u32 tx_bucket3_size;
	u32 tx_bucket4_size;
	u32 tx_64B_frames;
	u32 tx_bucket1_frames;
	u32 tx_bucket2_frames;
	u32 tx_bucket3_frames;
	u32 tx_bucket4_frames;
	u32 tx_bucket5_frames;
	u32 tx_bytes;
};

#define ICSSG_MIIG_STATS(field, stats_type)			\
{							\
	#field,						\
	offsetof(struct miig_stats_regs, field),	\
	stats_type					\
}

struct icssg_miig_stats {
	char name[ETH_GSTRING_LEN];
	u32 offset;
	bool standard_stats;
};

static const struct icssg_miig_stats icssg_all_miig_stats[] = {
	/* Rx */
	ICSSG_MIIG_STATS(rx_packets, true),
	ICSSG_MIIG_STATS(rx_broadcast_frames, false),
	ICSSG_MIIG_STATS(rx_multicast_frames, true),
	ICSSG_MIIG_STATS(rx_crc_errors, true),
	ICSSG_MIIG_STATS(rx_mii_error_frames, false),
	ICSSG_MIIG_STATS(rx_odd_nibble_frames, false),
	ICSSG_MIIG_STATS(rx_frame_max_size, true),
	ICSSG_MIIG_STATS(rx_max_size_error_frames, false),
	ICSSG_MIIG_STATS(rx_frame_min_size, true),
	ICSSG_MIIG_STATS(rx_min_size_error_frames, false),
	ICSSG_MIIG_STATS(rx_over_errors, true),
	ICSSG_MIIG_STATS(rx_class0_hits, false),
	ICSSG_MIIG_STATS(rx_class1_hits, false),
	ICSSG_MIIG_STATS(rx_class2_hits, false),
	ICSSG_MIIG_STATS(rx_class3_hits, false),
	ICSSG_MIIG_STATS(rx_class4_hits, false),
	ICSSG_MIIG_STATS(rx_class5_hits, false),
	ICSSG_MIIG_STATS(rx_class6_hits, false),
	ICSSG_MIIG_STATS(rx_class7_hits, false),
	ICSSG_MIIG_STATS(rx_class8_hits, false),
	ICSSG_MIIG_STATS(rx_class9_hits, false),
	ICSSG_MIIG_STATS(rx_class10_hits, false),
	ICSSG_MIIG_STATS(rx_class11_hits, false),
	ICSSG_MIIG_STATS(rx_class12_hits, false),
	ICSSG_MIIG_STATS(rx_class13_hits, false),
	ICSSG_MIIG_STATS(rx_class14_hits, false),
	ICSSG_MIIG_STATS(rx_class15_hits, false),
	ICSSG_MIIG_STATS(rx_smd_frags, false),
	ICSSG_MIIG_STATS(rx_bucket1_size, true),
	ICSSG_MIIG_STATS(rx_bucket2_size, true),
	ICSSG_MIIG_STATS(rx_bucket3_size, true),
	ICSSG_MIIG_STATS(rx_bucket4_size, true),
	ICSSG_MIIG_STATS(rx_64B_frames, true),
	ICSSG_MIIG_STATS(rx_bucket1_frames, true),
	ICSSG_MIIG_STATS(rx_bucket2_frames, true),
	ICSSG_MIIG_STATS(rx_bucket3_frames, true),
	ICSSG_MIIG_STATS(rx_bucket4_frames, true),
	ICSSG_MIIG_STATS(rx_bucket5_frames, true),
	ICSSG_MIIG_STATS(rx_bytes, true),
	ICSSG_MIIG_STATS(rx_tx_total_bytes, false),
	/* Tx */
	ICSSG_MIIG_STATS(tx_packets, true),
	ICSSG_MIIG_STATS(tx_broadcast_frames, false),
	ICSSG_MIIG_STATS(tx_multicast_frames, false),
	ICSSG_MIIG_STATS(tx_odd_nibble_frames, false),
	ICSSG_MIIG_STATS(tx_underflow_errors, false),
	ICSSG_MIIG_STATS(tx_frame_max_size, true),
	ICSSG_MIIG_STATS(tx_max_size_error_frames, false),
	ICSSG_MIIG_STATS(tx_frame_min_size, true),
	ICSSG_MIIG_STATS(tx_min_size_error_frames, false),
	ICSSG_MIIG_STATS(tx_bucket1_size, true),
	ICSSG_MIIG_STATS(tx_bucket2_size, true),
	ICSSG_MIIG_STATS(tx_bucket3_size, true),
	ICSSG_MIIG_STATS(tx_bucket4_size, true),
	ICSSG_MIIG_STATS(tx_64B_frames, true),
	ICSSG_MIIG_STATS(tx_bucket1_frames, true),
	ICSSG_MIIG_STATS(tx_bucket2_frames, true),
	ICSSG_MIIG_STATS(tx_bucket3_frames, true),
	ICSSG_MIIG_STATS(tx_bucket4_frames, true),
	ICSSG_MIIG_STATS(tx_bucket5_frames, true),
	ICSSG_MIIG_STATS(tx_bytes, true),
};

#define ICSSG_PA_STATS(field)	\
{				\
	#field,			\
	field,			\
}

struct icssg_pa_stats {
	char name[ETH_GSTRING_LEN];
	u32 offset;
};

static const struct icssg_pa_stats icssg_all_pa_stats[] = {
	ICSSG_PA_STATS(FW_RTU_PKT_DROP),
	ICSSG_PA_STATS(FW_Q0_OVERFLOW),
	ICSSG_PA_STATS(FW_Q1_OVERFLOW),
	ICSSG_PA_STATS(FW_Q2_OVERFLOW),
	ICSSG_PA_STATS(FW_Q3_OVERFLOW),
	ICSSG_PA_STATS(FW_Q4_OVERFLOW),
	ICSSG_PA_STATS(FW_Q5_OVERFLOW),
	ICSSG_PA_STATS(FW_Q6_OVERFLOW),
	ICSSG_PA_STATS(FW_Q7_OVERFLOW),
	ICSSG_PA_STATS(FW_DROPPED_PKT),
	ICSSG_PA_STATS(FW_RX_ERROR),
	ICSSG_PA_STATS(FW_RX_DS_INVALID),
	ICSSG_PA_STATS(FW_TX_DROPPED_PACKET),
	ICSSG_PA_STATS(FW_TX_TS_DROPPED_PACKET),
	ICSSG_PA_STATS(FW_INF_PORT_DISABLED),
	ICSSG_PA_STATS(FW_INF_SAV),
	ICSSG_PA_STATS(FW_INF_SA_DL),
	ICSSG_PA_STATS(FW_INF_PORT_BLOCKED),
	ICSSG_PA_STATS(FW_INF_DROP_TAGGED),
	ICSSG_PA_STATS(FW_INF_DROP_PRIOTAGGED),
	ICSSG_PA_STATS(FW_INF_DROP_NOTAG),
	ICSSG_PA_STATS(FW_INF_DROP_NOTMEMBER),
	ICSSG_PA_STATS(FW_RX_EOF_SHORT_FRMERR),
	ICSSG_PA_STATS(FW_RX_B0_DROP_EARLY_EOF),
	ICSSG_PA_STATS(FW_TX_JUMBO_FRM_CUTOFF),
	ICSSG_PA_STATS(FW_RX_EXP_FRAG_Q_DROP),
	ICSSG_PA_STATS(FW_RX_FIFO_OVERRUN),
	ICSSG_PA_STATS(FW_CUT_THR_PKT),
	ICSSG_PA_STATS(FW_HOST_RX_PKT_CNT),
	ICSSG_PA_STATS(FW_HOST_TX_PKT_CNT),
	ICSSG_PA_STATS(FW_HOST_EGRESS_Q_PRE_OVERFLOW),
	ICSSG_PA_STATS(FW_HOST_EGRESS_Q_EXP_OVERFLOW),
};

#endif /* __NET_TI_ICSSG_STATS_H */
