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
	const s32 nr_batch = READ_ONCE(stat->nr_batch);
	const s32 nr_samples = READ_ONCE(stat->nr_samples);

	if (!nr_batch)
		return;
	if (!nr_samples)
		stat->mean = div64_s64(stat->batch, nr_batch);
	else {
		stat->mean = div64_s64((stat->mean * nr_samples) +
					stat->batch,
					nr_batch + nr_samples);
	}

	stat->nr_samples += nr_batch;
	stat->nr_batch = stat->batch = 0;
}

static void blk_stat_sum(struct blk_rq_stat *dst, struct blk_rq_stat *src)
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

	blk_stat_init(&dst[BLK_STAT_READ]);
	blk_stat_init(&dst[BLK_STAT_WRITE]);

	nr = 0;
	do {
		uint64_t newest = 0;

		queue_for_each_hw_ctx(q, hctx, i) {
			hctx_for_each_ctx(hctx, ctx, j) {
				blk_stat_flush_batch(&ctx->stat[BLK_STAT_READ]);
				blk_stat_flush_batch(&ctx->stat[BLK_STAT_WRITE]);

				if (!ctx->stat[BLK_STAT_READ].nr_samples &&
				    !ctx->stat[BLK_STAT_WRITE].nr_samples)
					continue;
				if (ctx->stat[BLK_STAT_READ].time > newest)
					newest = ctx->stat[BLK_STAT_READ].time;
				if (ctx->stat[BLK_STAT_WRITE].time > newest)
					newest = ctx->stat[BLK_STAT_WRITE].time;
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
				if (ctx->stat[BLK_STAT_READ].time == newest) {
					blk_stat_sum(&dst[BLK_STAT_READ],
						     &ctx->stat[BLK_STAT_READ]);
					nr++;
				}
				if (ctx->stat[BLK_STAT_WRITE].time == newest) {
					blk_stat_sum(&dst[BLK_STAT_WRITE],
						     &ctx->stat[BLK_STAT_WRITE]);
					nr++;
				}
			}
		}
		/*
		 * If we race on finding an entry, just loop back again.
		 * Should be very rare.
		 */
	} while (!nr);

	dst[BLK_STAT_READ].time = dst[BLK_STAT_WRITE].time = latest;
}

void blk_queue_stat_get(struct request_queue *q, struct blk_rq_stat *dst)
{
	if (q->mq_ops)
		blk_mq_stat_get(q, dst);
	else {
		blk_stat_flush_batch(&q->rq_stats[BLK_STAT_READ]);
		blk_stat_flush_batch(&q->rq_stats[BLK_STAT_WRITE]);
		memcpy(&dst[BLK_STAT_READ], &q->rq_stats[BLK_STAT_READ],
				sizeof(struct blk_rq_stat));
		memcpy(&dst[BLK_STAT_WRITE], &q->rq_stats[BLK_STAT_WRITE],
				sizeof(struct blk_rq_stat));
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
			blk_stat_flush_batch(&ctx->stat[BLK_STAT_READ]);
			blk_stat_flush_batch(&ctx->stat[BLK_STAT_WRITE]);

			if (!ctx->stat[BLK_STAT_READ].nr_samples &&
			    !ctx->stat[BLK_STAT_WRITE].nr_samples)
				continue;

			if (ctx->stat[BLK_STAT_READ].time > newest)
				newest = ctx->stat[BLK_STAT_READ].time;
			if (ctx->stat[BLK_STAT_WRITE].time > newest)
				newest = ctx->stat[BLK_STAT_WRITE].time;
		}

		if (!newest)
			break;

		hctx_for_each_ctx(hctx, ctx, i) {
			if (ctx->stat[BLK_STAT_READ].time == newest) {
				blk_stat_sum(&dst[BLK_STAT_READ],
						&ctx->stat[BLK_STAT_READ]);
				nr++;
			}
			if (ctx->stat[BLK_STAT_WRITE].time == newest) {
				blk_stat_sum(&dst[BLK_STAT_WRITE],
						&ctx->stat[BLK_STAT_WRITE]);
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
	stat->time = time_now & BLK_STAT_NSEC_MASK;
}

void blk_stat_init(struct blk_rq_stat *stat)
{
	__blk_stat_init(stat, ktime_to_ns(ktime_get()));
}

static bool __blk_stat_is_current(struct blk_rq_stat *stat, s64 now)
{
	return (now & BLK_STAT_NSEC_MASK) == (stat->time & BLK_STAT_NSEC_MASK);
}

bool blk_stat_is_current(struct blk_rq_stat *stat)
{
	return __blk_stat_is_current(stat, ktime_to_ns(ktime_get()));
}

void blk_stat_add(struct blk_rq_stat *stat, struct request *rq)
{
	s64 now, value;

	now = __blk_stat_time(ktime_to_ns(ktime_get()));
	if (now < blk_stat_time(&rq->issue_stat))
		return;

	if (!__blk_stat_is_current(stat, now))
		__blk_stat_init(stat, now);

	value = now - blk_stat_time(&rq->issue_stat);
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
				blk_stat_init(&ctx->stat[BLK_STAT_READ]);
				blk_stat_init(&ctx->stat[BLK_STAT_WRITE]);
			}
		}
	} else {
		blk_stat_init(&q->rq_stats[BLK_STAT_READ]);
		blk_stat_init(&q->rq_stats[BLK_STAT_WRITE]);
	}
}

void blk_stat_set_issue_time(struct blk_issue_stat *stat)
{
	stat->time = (stat->time & BLK_STAT_MASK) |
			(ktime_to_ns(ktime_get()) & BLK_STAT_TIME_MASK);
}

/*
 * Enable stat tracking, return whether it was enabled
 */
bool blk_stat_enable(struct request_queue *q)
{
	if (!test_bit(QUEUE_FLAG_STATS, &q->queue_flags)) {
		set_bit(QUEUE_FLAG_STATS, &q->queue_flags);
		return false;
	}

	return true;
}
