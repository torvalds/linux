/*
 * LGPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.
 *
 * LGPL HEADER END
 *
 */
/*
 * Copyright (c) 2014, Intel Corporation.
 */
/*
 * Author: Amir Shehata <amir.shehata@intel.com>
 */

#ifndef LNET_DLC_H
#define LNET_DLC_H

#include "../libcfs/libcfs_ioctl.h"
#include "types.h"

#define MAX_NUM_SHOW_ENTRIES	32
#define LNET_MAX_STR_LEN	128
#define LNET_MAX_SHOW_NUM_CPT	128
#define LNET_UNDEFINED_HOPS	((__u32) -1)

struct lnet_ioctl_net_config {
	char ni_interfaces[LNET_MAX_INTERFACES][LNET_MAX_STR_LEN];
	__u32 ni_status;
	__u32 ni_cpts[LNET_MAX_SHOW_NUM_CPT];
};

#define LNET_TINY_BUF_IDX	0
#define LNET_SMALL_BUF_IDX	1
#define LNET_LARGE_BUF_IDX	2

/* # different router buffer pools */
#define LNET_NRBPOOLS		(LNET_LARGE_BUF_IDX + 1)

struct lnet_ioctl_pool_cfg {
	struct {
		__u32 pl_npages;
		__u32 pl_nbuffers;
		__u32 pl_credits;
		__u32 pl_mincredits;
	} pl_pools[LNET_NRBPOOLS];
	__u32 pl_routing;
};

struct lnet_ioctl_config_data {
	struct libcfs_ioctl_hdr cfg_hdr;

	__u32 cfg_net;
	__u32 cfg_count;
	__u64 cfg_nid;
	__u32 cfg_ncpts;

	union {
		struct {
			__u32 rtr_hop;
			__u32 rtr_priority;
			__u32 rtr_flags;
		} cfg_route;
		struct {
			char net_intf[LNET_MAX_STR_LEN];
			__s32 net_peer_timeout;
			__s32 net_peer_tx_credits;
			__s32 net_peer_rtr_credits;
			__s32 net_max_tx_credits;
			__u32 net_cksum_algo;
			__u32 net_pad;
		} cfg_net;
		struct {
			__u32 buf_enable;
			__s32 buf_tiny;
			__s32 buf_small;
			__s32 buf_large;
		} cfg_buffers;
	} cfg_config_u;

	char cfg_bulk[0];
};

struct lnet_ioctl_peer {
	struct libcfs_ioctl_hdr pr_hdr;
	__u32 pr_count;
	__u32 pr_pad;
	__u64 pr_nid;

	union {
		struct {
			char cr_aliveness[LNET_MAX_STR_LEN];
			__u32 cr_refcount;
			__u32 cr_ni_peer_tx_credits;
			__u32 cr_peer_tx_credits;
			__u32 cr_peer_rtr_credits;
			__u32 cr_peer_min_rtr_credits;
			__u32 cr_peer_tx_qnob;
			__u32 cr_ncpt;
		} pr_peer_credits;
	} pr_lnd_u;
};

struct lnet_ioctl_lnet_stats {
	struct libcfs_ioctl_hdr st_hdr;
	struct lnet_counters st_cntrs;
};

#endif /* LNET_DLC_H */
