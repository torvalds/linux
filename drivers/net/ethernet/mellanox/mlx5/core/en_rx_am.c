/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
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

#include "en.h"

/* Adaptive moderation profiles */
#define MLX5E_AM_DEFAULT_RX_CQ_MODERATION_PKTS_FROM_EQE 256
#define MLX5E_RX_AM_DEF_PROFILE_CQE 1
#define MLX5E_RX_AM_DEF_PROFILE_EQE 1
#define MLX5E_PARAMS_AM_NUM_PROFILES 5

/* All profiles sizes must be MLX5E_PARAMS_AM_NUM_PROFILES */
#define MLX5_AM_EQE_PROFILES { \
	{1,   MLX5E_AM_DEFAULT_RX_CQ_MODERATION_PKTS_FROM_EQE}, \
	{8,   MLX5E_AM_DEFAULT_RX_CQ_MODERATION_PKTS_FROM_EQE}, \
	{64,  MLX5E_AM_DEFAULT_RX_CQ_MODERATION_PKTS_FROM_EQE}, \
	{128, MLX5E_AM_DEFAULT_RX_CQ_MODERATION_PKTS_FROM_EQE}, \
	{256, MLX5E_AM_DEFAULT_RX_CQ_MODERATION_PKTS_FROM_EQE}, \
}

#define MLX5_AM_CQE_PROFILES { \
	{2,  256},             \
	{8,  128},             \
	{16, 64},              \
	{32, 64},              \
	{64, 64}               \
}

static const struct mlx5e_cq_moder
profile[MLX5_CQ_PERIOD_NUM_MODES][MLX5E_PARAMS_AM_NUM_PROFILES] = {
	MLX5_AM_EQE_PROFILES,
	MLX5_AM_CQE_PROFILES,
};

static inline struct mlx5e_cq_moder mlx5e_am_get_profile(u8 cq_period_mode, int ix)
{
	return profile[cq_period_mode][ix];
}

struct mlx5e_cq_moder mlx5e_am_get_def_profile(u8 rx_cq_period_mode)
{
	int default_profile_ix;

	if (rx_cq_period_mode == MLX5_CQ_PERIOD_MODE_START_FROM_CQE)
		default_profile_ix = MLX5E_RX_AM_DEF_PROFILE_CQE;
	else /* MLX5_CQ_PERIOD_MODE_START_FROM_EQE */
		default_profile_ix = MLX5E_RX_AM_DEF_PROFILE_EQE;

	return profile[rx_cq_period_mode][default_profile_ix];
}

/* Adaptive moderation logic */
enum {
	MLX5E_AM_START_MEASURE,
	MLX5E_AM_MEASURE_IN_PROGRESS,
	MLX5E_AM_APPLY_NEW_PROFILE,
};

enum {
	MLX5E_AM_PARKING_ON_TOP,
	MLX5E_AM_PARKING_TIRED,
	MLX5E_AM_GOING_RIGHT,
	MLX5E_AM_GOING_LEFT,
};

enum {
	MLX5E_AM_STATS_WORSE,
	MLX5E_AM_STATS_SAME,
	MLX5E_AM_STATS_BETTER,
};

enum {
	MLX5E_AM_STEPPED,
	MLX5E_AM_TOO_TIRED,
	MLX5E_AM_ON_EDGE,
};

static bool mlx5e_am_on_top(struct mlx5e_rx_am *am)
{
	switch (am->tune_state) {
	case MLX5E_AM_PARKING_ON_TOP:
	case MLX5E_AM_PARKING_TIRED:
		return true;
	case MLX5E_AM_GOING_RIGHT:
		return (am->steps_left > 1) && (am->steps_right == 1);
	default: /* MLX5E_AM_GOING_LEFT */
		return (am->steps_right > 1) && (am->steps_left == 1);
	}
}

static void mlx5e_am_turn(struct mlx5e_rx_am *am)
{
	switch (am->tune_state) {
	case MLX5E_AM_PARKING_ON_TOP:
	case MLX5E_AM_PARKING_TIRED:
		break;
	case MLX5E_AM_GOING_RIGHT:
		am->tune_state = MLX5E_AM_GOING_LEFT;
		am->steps_left = 0;
		break;
	case MLX5E_AM_GOING_LEFT:
		am->tune_state = MLX5E_AM_GOING_RIGHT;
		am->steps_right = 0;
		break;
	}
}

static int mlx5e_am_step(struct mlx5e_rx_am *am)
{
	if (am->tired == (MLX5E_PARAMS_AM_NUM_PROFILES * 2))
		return MLX5E_AM_TOO_TIRED;

	switch (am->tune_state) {
	case MLX5E_AM_PARKING_ON_TOP:
	case MLX5E_AM_PARKING_TIRED:
		break;
	case MLX5E_AM_GOING_RIGHT:
		if (am->profile_ix == (MLX5E_PARAMS_AM_NUM_PROFILES - 1))
			return MLX5E_AM_ON_EDGE;
		am->profile_ix++;
		am->steps_right++;
		break;
	case MLX5E_AM_GOING_LEFT:
		if (am->profile_ix == 0)
			return MLX5E_AM_ON_EDGE;
		am->profile_ix--;
		am->steps_left++;
		break;
	}

	am->tired++;
	return MLX5E_AM_STEPPED;
}

static void mlx5e_am_park_on_top(struct mlx5e_rx_am *am)
{
	am->steps_right  = 0;
	am->steps_left   = 0;
	am->tired        = 0;
	am->tune_state   = MLX5E_AM_PARKING_ON_TOP;
}

static void mlx5e_am_park_tired(struct mlx5e_rx_am *am)
{
	am->steps_right  = 0;
	am->steps_left   = 0;
	am->tune_state   = MLX5E_AM_PARKING_TIRED;
}

static void mlx5e_am_exit_parking(struct mlx5e_rx_am *am)
{
	am->tune_state = am->profile_ix ? MLX5E_AM_GOING_LEFT :
					  MLX5E_AM_GOING_RIGHT;
	mlx5e_am_step(am);
}

#define IS_SIGNIFICANT_DIFF(val, ref) \
	(((100 * abs((val) - (ref))) / (ref)) > 10) /* more than 10% difference */

static int mlx5e_am_stats_compare(struct mlx5e_rx_am_stats *curr,
				  struct mlx5e_rx_am_stats *prev)
{
	if (!prev->bpms)
		return curr->bpms ? MLX5E_AM_STATS_BETTER :
				    MLX5E_AM_STATS_SAME;

	if (IS_SIGNIFICANT_DIFF(curr->bpms, prev->bpms))
		return (curr->bpms > prev->bpms) ? MLX5E_AM_STATS_BETTER :
						   MLX5E_AM_STATS_WORSE;

	if (!prev->ppms)
		return curr->ppms ? MLX5E_AM_STATS_BETTER :
				    MLX5E_AM_STATS_SAME;

	if (IS_SIGNIFICANT_DIFF(curr->ppms, prev->ppms))
		return (curr->ppms > prev->ppms) ? MLX5E_AM_STATS_BETTER :
						   MLX5E_AM_STATS_WORSE;
	if (!prev->epms)
		return MLX5E_AM_STATS_SAME;

	if (IS_SIGNIFICANT_DIFF(curr->epms, prev->epms))
		return (curr->epms < prev->epms) ? MLX5E_AM_STATS_BETTER :
						   MLX5E_AM_STATS_WORSE;

	return MLX5E_AM_STATS_SAME;
}

static bool mlx5e_am_decision(struct mlx5e_rx_am_stats *curr_stats,
			      struct mlx5e_rx_am *am)
{
	int prev_state = am->tune_state;
	int prev_ix = am->profile_ix;
	int stats_res;
	int step_res;

	switch (am->tune_state) {
	case MLX5E_AM_PARKING_ON_TOP:
		stats_res = mlx5e_am_stats_compare(curr_stats, &am->prev_stats);
		if (stats_res != MLX5E_AM_STATS_SAME)
			mlx5e_am_exit_parking(am);
		break;

	case MLX5E_AM_PARKING_TIRED:
		am->tired--;
		if (!am->tired)
			mlx5e_am_exit_parking(am);
		break;

	case MLX5E_AM_GOING_RIGHT:
	case MLX5E_AM_GOING_LEFT:
		stats_res = mlx5e_am_stats_compare(curr_stats, &am->prev_stats);
		if (stats_res != MLX5E_AM_STATS_BETTER)
			mlx5e_am_turn(am);

		if (mlx5e_am_on_top(am)) {
			mlx5e_am_park_on_top(am);
			break;
		}

		step_res = mlx5e_am_step(am);
		switch (step_res) {
		case MLX5E_AM_ON_EDGE:
			mlx5e_am_park_on_top(am);
			break;
		case MLX5E_AM_TOO_TIRED:
			mlx5e_am_park_tired(am);
			break;
		}

		break;
	}

	if ((prev_state     != MLX5E_AM_PARKING_ON_TOP) ||
	    (am->tune_state != MLX5E_AM_PARKING_ON_TOP))
		am->prev_stats = *curr_stats;

	return am->profile_ix != prev_ix;
}

static void mlx5e_am_sample(struct mlx5e_rq *rq,
			    struct mlx5e_rx_am_sample *s)
{
	s->time	     = ktime_get();
	s->pkt_ctr   = rq->stats.packets;
	s->byte_ctr  = rq->stats.bytes;
	s->event_ctr = rq->cq.event_ctr;
}

#define MLX5E_AM_NEVENTS 64
#define BITS_PER_TYPE(type) (sizeof(type) * BITS_PER_BYTE)
#define BIT_GAP(bits, end, start) ((((end) - (start)) + BIT_ULL(bits)) & (BIT_ULL(bits) - 1))

static void mlx5e_am_calc_stats(struct mlx5e_rx_am_sample *start,
				struct mlx5e_rx_am_sample *end,
				struct mlx5e_rx_am_stats *curr_stats)
{
	/* u32 holds up to 71 minutes, should be enough */
	u32 delta_us = ktime_us_delta(end->time, start->time);
	u32 npkts = BIT_GAP(BITS_PER_TYPE(u32), end->pkt_ctr, start->pkt_ctr);
	u32 nbytes = BIT_GAP(BITS_PER_TYPE(u32), end->byte_ctr,
			     start->byte_ctr);

	if (!delta_us)
		return;

	curr_stats->ppms = DIV_ROUND_UP(npkts * USEC_PER_MSEC, delta_us);
	curr_stats->bpms = DIV_ROUND_UP(nbytes * USEC_PER_MSEC, delta_us);
	curr_stats->epms = DIV_ROUND_UP(MLX5E_AM_NEVENTS * USEC_PER_MSEC,
					delta_us);
}

void mlx5e_rx_am_work(struct work_struct *work)
{
	struct mlx5e_rx_am *am = container_of(work, struct mlx5e_rx_am,
					      work);
	struct mlx5e_rq *rq = container_of(am, struct mlx5e_rq, am);
	struct mlx5e_cq_moder cur_profile = profile[am->mode][am->profile_ix];

	mlx5_core_modify_cq_moderation(rq->mdev, &rq->cq.mcq,
				       cur_profile.usec, cur_profile.pkts);

	am->state = MLX5E_AM_START_MEASURE;
}

void mlx5e_rx_am(struct mlx5e_rq *rq)
{
	struct mlx5e_rx_am *am = &rq->am;
	struct mlx5e_rx_am_sample end_sample;
	struct mlx5e_rx_am_stats curr_stats;
	u16 nevents;

	switch (am->state) {
	case MLX5E_AM_MEASURE_IN_PROGRESS:
		nevents = BIT_GAP(BITS_PER_TYPE(u16), rq->cq.event_ctr,
				  am->start_sample.event_ctr);
		if (nevents < MLX5E_AM_NEVENTS)
			break;
		mlx5e_am_sample(rq, &end_sample);
		mlx5e_am_calc_stats(&am->start_sample, &end_sample,
				    &curr_stats);
		if (mlx5e_am_decision(&curr_stats, am)) {
			am->state = MLX5E_AM_APPLY_NEW_PROFILE;
			schedule_work(&am->work);
			break;
		}
		/* fall through */
	case MLX5E_AM_START_MEASURE:
		mlx5e_am_sample(rq, &am->start_sample);
		am->state = MLX5E_AM_MEASURE_IN_PROGRESS;
		break;
	case MLX5E_AM_APPLY_NEW_PROFILE:
		break;
	}
}
