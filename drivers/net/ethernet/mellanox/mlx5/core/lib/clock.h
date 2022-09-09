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

static inline bool mlx5_is_real_time_rq(struct mlx5_core_dev *mdev)
{
	u8 rq_ts_format_cap = MLX5_CAP_GEN(mdev, rq_ts_format);

	return (rq_ts_format_cap == MLX5_TIMESTAMP_FORMAT_CAP_REAL_TIME ||
		rq_ts_format_cap ==
			MLX5_TIMESTAMP_FORMAT_CAP_FREE_RUNNING_AND_REAL_TIME);
}

static inline bool mlx5_is_real_time_sq(struct mlx5_core_dev *mdev)
{
	u8 sq_ts_format_cap = MLX5_CAP_GEN(mdev, sq_ts_format);

	return (sq_ts_format_cap == MLX5_TIMESTAMP_FORMAT_CAP_REAL_TIME ||
		sq_ts_format_cap ==
			MLX5_TIMESTAMP_FORMAT_CAP_FREE_RUNNING_AND_REAL_TIME);
}

typedef ktime_t (*cqe_ts_to_ns)(struct mlx5_clock *, u64);

#if IS_ENABLED(CONFIG_PTP_1588_CLOCK)
void mlx5_init_clock(struct mlx5_core_dev *mdev);
void mlx5_cleanup_clock(struct mlx5_core_dev *mdev);

static inline int mlx5_clock_get_ptp_index(struct mlx5_core_dev *mdev)
{
	return mdev->clock.ptp ? ptp_clock_index(mdev->clock.ptp) : -1;
}

static inline ktime_t mlx5_timecounter_cyc2time(struct mlx5_clock *clock,
						u64 timestamp)
{
	struct mlx5_timer *timer = &clock->timer;
	unsigned int seq;
	u64 nsec;

	do {
		seq = read_seqbegin(&clock->lock);
		nsec = timecounter_cyc2time(&timer->tc, timestamp);
	} while (read_seqretry(&clock->lock, seq));

	return ns_to_ktime(nsec);
}

#define REAL_TIME_TO_NS(hi, low) (((u64)hi) * NSEC_PER_SEC + ((u64)low))

static inline ktime_t mlx5_real_time_cyc2time(struct mlx5_clock *clock,
					      u64 timestamp)
{
	u64 time = REAL_TIME_TO_NS(timestamp >> 32, timestamp & 0xFFFFFFFF);

	return ns_to_ktime(time);
}
#else
static inline void mlx5_init_clock(struct mlx5_core_dev *mdev) {}
static inline void mlx5_cleanup_clock(struct mlx5_core_dev *mdev) {}
static inline int mlx5_clock_get_ptp_index(struct mlx5_core_dev *mdev)
{
	return -1;
}

static inline ktime_t mlx5_timecounter_cyc2time(struct mlx5_clock *clock,
						u64 timestamp)
{
	return 0;
}

static inline ktime_t mlx5_real_time_cyc2time(struct mlx5_clock *clock,
					      u64 timestamp)
{
	return 0;
}
#endif

static inline cqe_ts_to_ns mlx5_rq_ts_translator(struct mlx5_core_dev *mdev)
{
	return mlx5_is_real_time_rq(mdev) ? mlx5_real_time_cyc2time :
					    mlx5_timecounter_cyc2time;
}

static inline cqe_ts_to_ns mlx5_sq_ts_translator(struct mlx5_core_dev *mdev)
{
	return mlx5_is_real_time_sq(mdev) ? mlx5_real_time_cyc2time :
					    mlx5_timecounter_cyc2time;
}
#endif
