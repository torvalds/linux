/*
 * Copyright (c) 2013-2015, Mellanox Technologies, Ltd.  All rights reserved.
 * Copyright (c) 2017-2018, Broadcom Limited
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

#ifndef MLX5_AM_H
#define MLX5_AM_H

struct mlx5e_cq_moder {
	u16 usec;
	u16 pkts;
	u8 cq_period_mode;
};

struct mlx5e_rx_am_sample {
	ktime_t time;
	u32     pkt_ctr;
	u32     byte_ctr;
	u16     event_ctr;
};

struct mlx5e_rx_am_stats {
	int ppms; /* packets per msec */
	int bpms; /* bytes per msec */
	int epms; /* events per msec */
};

struct mlx5e_rx_am { /* Adaptive Moderation */
	u8                                      state;
	struct mlx5e_rx_am_stats                prev_stats;
	struct mlx5e_rx_am_sample               start_sample;
	struct work_struct                      work;
	u8                                      profile_ix;
	u8                                      mode;
	u8                                      tune_state;
	u8                                      steps_right;
	u8                                      steps_left;
	u8                                      tired;
};

#endif /* MLX5_AM_H */
