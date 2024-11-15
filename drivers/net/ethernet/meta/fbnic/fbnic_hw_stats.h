/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef _FBNIC_HW_STATS_H_
#define _FBNIC_HW_STATS_H_

#include <linux/ethtool.h>

#include "fbnic_csr.h"

struct fbnic_stat_counter {
	u64 value;
	union {
		u32 old_reg_value_32;
		u64 old_reg_value_64;
	} u;
	bool reported;
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

struct fbnic_mac_stats {
	struct fbnic_eth_mac_stats eth_mac;
};

struct fbnic_rpc_stats {
	struct fbnic_stat_counter unkn_etype, unkn_ext_hdr;
	struct fbnic_stat_counter ipv4_frag, ipv6_frag, ipv4_esp, ipv6_esp;
	struct fbnic_stat_counter tcp_opt_err, out_of_hdr_err, ovr_size_err;
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
	struct fbnic_mac_stats mac;
	struct fbnic_rpc_stats rpc;
	struct fbnic_pcie_stats pcie;
};

u64 fbnic_stat_rd64(struct fbnic_dev *fbd, u32 reg, u32 offset);

void fbnic_reset_hw_stats(struct fbnic_dev *fbd);
void fbnic_get_hw_stats32(struct fbnic_dev *fbd);
void fbnic_get_hw_stats(struct fbnic_dev *fbd);

#endif /* _FBNIC_HW_STATS_H_ */
