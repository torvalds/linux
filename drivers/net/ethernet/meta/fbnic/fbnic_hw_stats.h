/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef _FBNIC_HW_STATS_H_
#define _FBNIC_HW_STATS_H_

#include <linux/ethtool.h>
#include <linux/spinlock.h>

#include "fbnic_csr.h"

struct fbnic_stat_counter {
	u64 value;
	union {
		u32 old_reg_value_32;
		u64 old_reg_value_64;
	} u;
	bool reported;
};

struct fbnic_hw_stat {
	struct fbnic_stat_counter frames;
	struct fbnic_stat_counter bytes;
};

struct fbnic_fec_stats {
	struct fbnic_stat_counter corrected_blocks, uncorrectable_blocks;
};

struct fbnic_pcs_stats {
	struct {
		struct fbnic_stat_counter lanes[FBNIC_PCS_MAX_LANES];
	} SymbolErrorDuringCarrier;
};

/* Note: not updated by fbnic_get_hw_stats() */
struct fbnic_eth_ctrl_stats {
	struct fbnic_stat_counter MACControlFramesTransmitted;
	struct fbnic_stat_counter MACControlFramesReceived;
};

/* Note: not updated by fbnic_get_hw_stats() */
struct fbnic_rmon_stats {
	struct fbnic_stat_counter undersize_pkts;
	struct fbnic_stat_counter oversize_pkts;
	struct fbnic_stat_counter fragments;
	struct fbnic_stat_counter jabbers;

	struct fbnic_stat_counter hist[ETHTOOL_RMON_HIST_MAX];
	struct fbnic_stat_counter hist_tx[ETHTOOL_RMON_HIST_MAX];
};

/* Note: not updated by fbnic_get_hw_stats() */
struct fbnic_pause_stats {
	struct fbnic_stat_counter tx_pause_frames;
	struct fbnic_stat_counter rx_pause_frames;
};

struct fbnic_eth_mac_stats {
	struct fbnic_stat_counter FramesTransmittedOK;
	struct fbnic_stat_counter FramesReceivedOK;
	struct fbnic_stat_counter FrameCheckSequenceErrors;
	struct fbnic_stat_counter AlignmentErrors;
	struct fbnic_stat_counter OctetsTransmittedOK;
	struct fbnic_stat_counter FramesLostDueToIntMACXmitError;
	struct fbnic_stat_counter OctetsReceivedOK;
	struct fbnic_stat_counter FramesLostDueToIntMACRcvError;
	struct fbnic_stat_counter MulticastFramesXmittedOK;
	struct fbnic_stat_counter BroadcastFramesXmittedOK;
	struct fbnic_stat_counter MulticastFramesReceivedOK;
	struct fbnic_stat_counter BroadcastFramesReceivedOK;
	struct fbnic_stat_counter FrameTooLongErrors;
};

struct fbnic_phy_stats {
	struct fbnic_fec_stats fec;
	struct fbnic_pcs_stats pcs;
};

struct fbnic_mac_stats {
	struct fbnic_eth_mac_stats eth_mac;
	struct fbnic_pause_stats pause;
	struct fbnic_eth_ctrl_stats eth_ctrl;
	struct fbnic_rmon_stats rmon;
};

struct fbnic_tmi_stats {
	struct fbnic_hw_stat drop;
	struct fbnic_stat_counter ptp_illegal_req, ptp_good_ts, ptp_bad_ts;
};

struct fbnic_tti_stats {
	struct fbnic_hw_stat cm_drop, frame_drop, tbi_drop;
};

struct fbnic_rpc_stats {
	struct fbnic_stat_counter unkn_etype, unkn_ext_hdr;
	struct fbnic_stat_counter ipv4_frag, ipv6_frag, ipv4_esp, ipv6_esp;
	struct fbnic_stat_counter tcp_opt_err, out_of_hdr_err, ovr_size_err;
};

struct fbnic_rxb_enqueue_stats {
	struct fbnic_hw_stat drbo;
	struct fbnic_stat_counter integrity_err, mac_err;
	struct fbnic_stat_counter parser_err, frm_err;
};

struct fbnic_rxb_fifo_stats {
	struct fbnic_hw_stat drop, trunc;
	struct fbnic_stat_counter trans_drop, trans_ecn;
	struct fbnic_stat_counter level;
};

struct fbnic_rxb_dequeue_stats {
	struct fbnic_hw_stat intf, pbuf;
};

struct fbnic_rxb_stats {
	struct fbnic_rxb_enqueue_stats enq[FBNIC_RXB_ENQUEUE_INDICES];
	struct fbnic_rxb_fifo_stats fifo[FBNIC_RXB_FIFO_INDICES];
	struct fbnic_rxb_dequeue_stats deq[FBNIC_RXB_DEQUEUE_INDICES];
};

struct fbnic_hw_q_stats {
	struct fbnic_stat_counter rde_pkt_err;
	struct fbnic_stat_counter rde_pkt_cq_drop;
	struct fbnic_stat_counter rde_pkt_bdq_drop;
};

struct fbnic_pcie_stats {
	struct fbnic_stat_counter ob_rd_tlp, ob_rd_dword;
	struct fbnic_stat_counter ob_wr_tlp, ob_wr_dword;
	struct fbnic_stat_counter ob_cpl_tlp, ob_cpl_dword;

	struct fbnic_stat_counter ob_rd_no_tag;
	struct fbnic_stat_counter ob_rd_no_cpl_cred;
	struct fbnic_stat_counter ob_rd_no_np_cred;
};

struct fbnic_hw_stats {
	struct fbnic_phy_stats phy;
	struct fbnic_mac_stats mac;
	struct fbnic_tmi_stats tmi;
	struct fbnic_tti_stats tti;
	struct fbnic_rpc_stats rpc;
	struct fbnic_rxb_stats rxb;
	struct fbnic_hw_q_stats hw_q[FBNIC_MAX_QUEUES];
	struct fbnic_pcie_stats pcie;

	/* Lock protecting the access to hw stats */
	spinlock_t lock;
};

u64 fbnic_stat_rd64(struct fbnic_dev *fbd, u32 reg, u32 offset);

void fbnic_reset_hw_stats(struct fbnic_dev *fbd);
void fbnic_init_hw_stats(struct fbnic_dev *fbd);
void fbnic_get_hw_q_stats(struct fbnic_dev *fbd,
			  struct fbnic_hw_q_stats *hw_q);
void fbnic_get_hw_stats32(struct fbnic_dev *fbd);
void fbnic_get_hw_stats(struct fbnic_dev *fbd);

#endif /* _FBNIC_HW_STATS_H_ */
