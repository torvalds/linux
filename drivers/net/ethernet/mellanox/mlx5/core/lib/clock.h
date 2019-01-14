/*
 * Copyright (c) 2017, Mellanox Technologies, Ltd.  All rights reserved.
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

#ifndef __LIB_CLOCK_H__
#define __LIB_CLOCK_H__

#if IS_ENABLED(CONFIG_PTP_1588_CLOCK)
void mlx5_init_clock(struct mlx5_core_dev *mdev);
void mlx5_cleanup_clock(struct mlx5_core_dev *mdev);
void mlx5_pps_event(struct mlx5_core_dev *dev, struct mlx5_eqe *eqe);

static inline int mlx5_clock_get_ptp_index(struct mlx5_core_dev *mdev)
{
	return mdev->clock.ptp ? ptp_clock_index(mdev->clock.ptp) : -1;
}

static inline ktime_t mlx5_timecounter_cyc2time(struct mlx5_clock *clock,
						u64 timestamp)
{
	u64 nsec;

	read_lock(&clock->lock);
	nsec = timecounter_cyc2time(&clock->tc, timestamp);
	read_unlock(&clock->lock);

	return ns_to_ktime(nsec);
}

#else
static inline void mlx5_init_clock(struct mlx5_core_dev *mdev) {}
static inline void mlx5_cleanup_clock(struct mlx5_core_dev *mdev) {}
static inline void mlx5_pps_event(struct mlx5_core_dev *dev, struct mlx5_eqe *eqe) {}

static inline int mlx5_clock_get_ptp_index(struct mlx5_core_dev *mdev)
{
	return -1;
}

static inline ktime_t mlx5_timecounter_cyc2time(struct mlx5_clock *clock,
						u64 timestamp)
{
	return 0;
}
#endif

#endif
