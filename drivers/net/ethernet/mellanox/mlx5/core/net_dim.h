/*
 * Copyright (c) 2013-2015, Mellanox Technologies, Ltd.  All rights reserved.
 * Copyright (c) 2017-2018, Broadcom Limited.  All rights reserved.
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
 */

#ifndef NET_DIM_H
#define NET_DIM_H

struct net_dim_cq_moder {
	u16 usec;
	u16 pkts;
	u8 cq_period_mode;
};

struct net_dim_sample {
	ktime_t time;
	u32     pkt_ctr;
	u32     byte_ctr;
	u16     event_ctr;
};

struct net_dim_stats {
	int ppms; /* packets per msec */
	int bpms; /* bytes per msec */
	int epms; /* events per msec */
};

struct net_dim { /* Adaptive Moderation */
	u8                                      state;
	struct net_dim_stats                    prev_stats;
	struct net_dim_sample                   start_sample;
	struct work_struct                      work;
	u8                                      profile_ix;
	u8                                      mode;
	u8                                      tune_state;
	u8                                      steps_right;
	u8                                      steps_left;
	u8                                      tired;
};

enum {
	NET_DIM_CQ_PERIOD_MODE_START_FROM_EQE = 0x0,
	NET_DIM_CQ_PERIOD_MODE_START_FROM_CQE = 0x1,
	NET_DIM_CQ_PERIOD_NUM_MODES
};

/* Adaptive moderation logic */
enum {
	NET_DIM_START_MEASURE,
	NET_DIM_MEASURE_IN_PROGRESS,
	NET_DIM_APPLY_NEW_PROFILE,
};

enum {
	NET_DIM_PARKING_ON_TOP,
	NET_DIM_PARKING_TIRED,
	NET_DIM_GOING_RIGHT,
	NET_DIM_GOING_LEFT,
};

enum {
	NET_DIM_STATS_WORSE,
	NET_DIM_STATS_SAME,
	NET_DIM_STATS_BETTER,
};

enum {
	NET_DIM_STEPPED,
	NET_DIM_TOO_TIRED,
	NET_DIM_ON_EDGE,
};

void net_dim(struct net_dim *dim,
	     u16 event_ctr,
	     u64 packets,
	     u64 bytes);
struct net_dim_cq_moder net_dim_get_def_profile(u8 rx_cq_period_mode);
struct net_dim_cq_moder net_dim_get_profile(u8 cq_period_mode, int ix);

#endif /* NET_DIM_H */
