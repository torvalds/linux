/*
 * Block stat tracking code
 *
 * Copyright (C) 2016 Jens Axboe
 */
#include <linux/kernel.h>
#include <linux/blk-mq.h>

#include "blk-stat.h"
#include "blk-mq.h"

static void blk_stat_flush_batch(struct blk_rq_stat *stat)
{
	if (!stat->nr_batch)
		return;
	if (!stat->nr_samples)
		stat->mean = div64_s64(stat->batch, stat->nr_batch);
	else {
		stat->mean = div64_s64((stat->mean * stat->nr_samples) +
					stat->batch,
					stat->nr_samples + stat->nr_batch);
	}

	stat->nr_samples += stat->nr_batch;
	stat->nr_batch = stat->batch = 0;
}

void blk_stat_sum(struct blk_rq_stat *dst, struct blk_rq_stat *src)
{
	if (!src->nr_samples)
		return;

	blk_stat_flush_batch(src);

	dst->min = min(dst->min, src->min);
	dst->max = max(dst->max, src->max);

	if (!dst->nr_samples)
		dst->mean = src->mean;
	else {
		dst->mean = div64_s64((src->mean * src->nr_samples) +
					(dst->mean * dst->nr_samples),
					dst->nr_samples + src->nr_samples);
	}
	dst->nr_samples += src->nr_samples;
}

static void blk_mq_stat_get(struct request_queue *q, struct blk_rq_stat *dst)
{
	struct blk_mq_hw_ctx *hctx;
	struct blk_mq_ctx *ctx;
	uint64_t latest = 0;
	int i, j, nr;

	blk_stat_init(&dst[0]);
	blk_stat_init(&dst[1]);

	nr = 0;
	do {
		uint64_t newest = 0;

		queue_for_each_hw_ctx(q, hctx, i) {
			hctx_for_each_ctx(hctx, ctx, j) {
				if (!ctx->stat[0].nr_samples &&
				    !ctx->stat[1].nr_samples)
					continue;
				if (ctx->stat[0].time > newest)
					newest = ctx->stat[0].time;
				if (ctx->stat[1].time > newest)
					newest = ctx->stat[1].time;
			}
		}

		/*
		 * No samples
		 */
		if (!newest)
			break;

		if (newest > latest)
			latest = newest;

		queue_for_each_hw_ctx(q, hctx, i) {
			hctx_for_each_ctx(hctx, ctx, j) {
				if (ctx->stat[0].time == newest) {
					blk_stat_sum(&dst[0], &ctx->stat[0]);
					nr++;
				}
				if (ctx->stat[1].time == newest) {
					blk_stat_sum(&dst[1], &ctx->stat[1]);
					nr++;
				}
			}
		}
		/*
		 * If we race on finding an entry, just loop back again.
		 * Should be very rare.
		 */
	} while (!nr);

	dst[0].time = dst[1].time = latest;
}

void blk_queue_stat_get(struct request_queue *q, struct blk_rq_stat *dst)
{
	if (q->mq_ops)
		blk_mq_stat_get(q, dst);
	else {
		memcpy(&dst[0], &q->rq_stats[0], sizeof(struct blk_rq_stat));
		memcpy(&dst[1], &q->rq_stats[1], sizeof(struct blk_rq_stat));
	}
}

void blk_hctx_stat_get(struct blk_mq_hw_ctx *hctx, struct blk_rq_stat *dst)
{
	struct blk_mq_ctx *ctx;
	unsigned int i, nr;

	nr = 0;
	do {
		uint64_t newest = 0;

		hctx_for_each_ctx(hctx, ctx, i) {
			if (!ctx->stat[0].nr_samples &&
			    !ctx->stat[1].nr_samples)
				continue;

			if (ctx->stat[0].time > newest)
				newest = ctx->stat[0].time;
			if (ctx->stat[1].time > newest)
				newest = ctx->stat[1].time;
		}

		if (!newest)
			break;

		hctx_for_each_ctx(hctx, ctx, i) {
			if (ctx->stat[0].time == newest) {
				blk_stat_sum(&dst[0], &ctx->stat[0]);
				nr++;
			}
			if (ctx->stat[1].time == newest) {
				blk_stat_sum(&dst[1], &ctx->stat[1]);
				nr++;
			}
		}
		/*
		 * If we race on finding an entry, just loop back again.
		 * Should be very rare, as the window is only updated
		 * occasionally
		 */
	} while (!nr);
}

static void __blk_stat_init(struct blk_rq_stat *stat, s64 time_now)
{
	stat->min = -1ULL;
	stat->max = stat->nr_samples = stat->mean = 0;
	stat->batch = stat->nr_batch = 0;
	stat->time = time_now & BLK_STAT_MASK;
}

void blk_stat_init(struct blk_rq_stat *stat)
{
	__blk_stat_init(stat, ktime_to_ns(ktime_get()));
}

static bool __blk_stat_is_current(struct blk_rq_stat *stat, s64 now)
{
	return (now & BLK_STAT_MASK) == (stat->time & BLK_STAT_MASK);
}

bool blk_stat_is_current(struct blk_rq_stat *stat)
{
	return __blk_stat_is_current(stat, ktime_to_ns(ktime_get()));
}

void blk_stat_add(struct blk_rq_stat *stat, struct request *rq)
{
	s64 now, value;
	u64 rq_time = wbt_issue_stat_get_time(&rq->wb_stat);

	now = ktime_to_ns(ktime_get());
	if (now < rq_time)
		return;

	if (!__blk_stat_is_current(stat, now))
		__blk_stat_init(stat, now);

	value = now - rq_time;
	if (value > stat->max)
		stat->max = value;
	if (value < stat->min)
		stat->min = value;

	if (stat->batch + value < stat->batch ||
	    stat->nr_batch + 1 == BLK_RQ_STAT_BATCH)
		blk_stat_flush_batch(stat);

	stat->batch += value;
	stat->nr_batch++;
}

void blk_stat_clear(struct request_queue *q)
{
	if (q->mq_ops) {
		struct blk_mq_hw_ctx *hctx;
		struct blk_mq_ctx *ctx;
		int i, j;

		queue_for_each_hw_ctx(q, hctx, i) {
			hctx_for_each_ctx(hctx, ctx, j) {
				blk_stat_init(&ctx->stat[0]);
				blk_stat_init(&ctx->stat[1]);
			}
		}
	} else {
		blk_stat_init(&q->rq_stats[0]);
		blk_stat_init(&q->rq_stats[1]);
	}
}
