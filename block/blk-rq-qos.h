/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RQ_QOS_H
#define RQ_QOS_H

#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/blk_types.h>
#include <linux/atomic.h>
#include <linux/wait.h>
#include <linux/blk-mq.h>

#include "blk-mq-debugfs.h"

struct blk_mq_debugfs_attr;

enum rq_qos_id {
	RQ_QOS_WBT,
	RQ_QOS_LATENCY,
	RQ_QOS_COST,
};

struct rq_wait {
	wait_queue_head_t wait;
	atomic_t inflight;
};

struct rq_qos {
	const struct rq_qos_ops *ops;
	struct gendisk *disk;
	enum rq_qos_id id;
	struct rq_qos *next;
#ifdef CONFIG_BLK_DEBUG_FS
	struct dentry *debugfs_dir;
#endif
};

struct rq_qos_ops {
	void (*throttle)(struct rq_qos *, struct bio *);
	void (*track)(struct rq_qos *, struct request *, struct bio *);
	void (*merge)(struct rq_qos *, struct request *, struct bio *);
	void (*issue)(struct rq_qos *, struct request *);
	void (*requeue)(struct rq_qos *, struct request *);
	void (*done)(struct rq_qos *, struct request *);
	void (*done_bio)(struct rq_qos *, struct bio *);
	void (*cleanup)(struct rq_qos *, struct bio *);
	void (*queue_depth_changed)(struct rq_qos *);
	void (*exit)(struct rq_qos *);
	const struct blk_mq_debugfs_attr *debugfs_attrs;
};

struct rq_depth {
	unsigned int max_depth;

	int scale_step;
	bool scaled_max;

	unsigned int queue_depth;
	unsigned int default_depth;
};

static inline struct rq_qos *rq_qos_id(struct request_queue *q,
				       enum rq_qos_id id)
{
	struct rq_qos *rqos;
	for (rqos = q->rq_qos; rqos; rqos = rqos->next) {
		if (rqos->id == id)
			break;
	}
	return rqos;
}

static inline struct rq_qos *wbt_rq_qos(struct request_queue *q)
{
	return rq_qos_id(q, RQ_QOS_WBT);
}

static inline struct rq_qos *iolat_rq_qos(struct request_queue *q)
{
	return rq_qos_id(q, RQ_QOS_LATENCY);
}

static inline void rq_wait_init(struct rq_wait *rq_wait)
{
	atomic_set(&rq_wait->inflight, 0);
	init_waitqueue_head(&rq_wait->wait);
}

int rq_qos_add(struct rq_qos *rqos, struct gendisk *disk, enum rq_qos_id id,
		const struct rq_qos_ops *ops);
void rq_qos_del(struct rq_qos *rqos);

typedef bool (acquire_inflight_cb_t)(struct rq_wait *rqw, void *private_data);
typedef void (cleanup_cb_t)(struct rq_wait *rqw, void *private_data);

void rq_qos_wait(struct rq_wait *rqw, void *private_data,
		 acquire_inflight_cb_t *acquire_inflight_cb,
		 cleanup_cb_t *cleanup_cb);
bool rq_wait_inc_below(struct rq_wait *rq_wait, unsigned int limit);
bool rq_depth_scale_up(struct rq_depth *rqd);
bool rq_depth_scale_down(struct rq_depth *rqd, bool hard_throttle);
bool rq_depth_calc_max_depth(struct rq_depth *rqd);

void __rq_qos_cleanup(struct rq_qos *rqos, struct bio *bio);
void __rq_qos_done(struct rq_qos *rqos, struct request *rq);
void __rq_qos_issue(struct rq_qos *rqos, struct request *rq);
void __rq_qos_requeue(struct rq_qos *rqos, struct request *rq);
void __rq_qos_throttle(struct rq_qos *rqos, struct bio *bio);
void __rq_qos_track(struct rq_qos *rqos, struct request *rq, struct bio *bio);
void __rq_qos_merge(struct rq_qos *rqos, struct request *rq, struct bio *bio);
void __rq_qos_done_bio(struct rq_qos *rqos, struct bio *bio);
void __rq_qos_queue_depth_changed(struct rq_qos *rqos);

static inline void rq_qos_cleanup(struct request_queue *q, struct bio *bio)
{
	if (unlikely(test_bit(QUEUE_FLAG_QOS_ENABLED, &q->queue_flags)) &&
			q->rq_qos)
		__rq_qos_cleanup(q->rq_qos, bio);
}

static inline void rq_qos_done(struct request_queue *q, struct request *rq)
{
	if (unlikely(test_bit(QUEUE_FLAG_QOS_ENABLED, &q->queue_flags)) &&
			q->rq_qos && !blk_rq_is_passthrough(rq))
		__rq_qos_done(q->rq_qos, rq);
}

static inline void rq_qos_issue(struct request_queue *q, struct request *rq)
{
	if (unlikely(test_bit(QUEUE_FLAG_QOS_ENABLED, &q->queue_flags)) &&
			q->rq_qos)
		__rq_qos_issue(q->rq_qos, rq);
}

static inline void rq_qos_requeue(struct request_queue *q, struct request *rq)
{
	if (unlikely(test_bit(QUEUE_FLAG_QOS_ENABLED, &q->queue_flags)) &&
			q->rq_qos)
		__rq_qos_requeue(q->rq_qos, rq);
}

static inline void rq_qos_done_bio(struct bio *bio)
{
	struct request_queue *q;

	if (!bio->bi_bdev || (!bio_flagged(bio, BIO_QOS_THROTTLED) &&
			     !bio_flagged(bio, BIO_QOS_MERGED)))
		return;

	q = bdev_get_queue(bio->bi_bdev);

	/*
	 * A BIO may carry BIO_QOS_* flags even if the associated request_queue
	 * does not have rq_qos enabled. This can happen with stacked block
	 * devices — for example, NVMe multipath, where it's possible that the
	 * bottom device has QoS enabled but the top device does not. Therefore,
	 * always verify that q->rq_qos is present and QoS is enabled before
	 * calling __rq_qos_done_bio().
	 */
	if (test_bit(QUEUE_FLAG_QOS_ENABLED, &q->queue_flags) && q->rq_qos)
		__rq_qos_done_bio(q->rq_qos, bio);
}

static inline void rq_qos_throttle(struct request_queue *q, struct bio *bio)
{
	if (unlikely(test_bit(QUEUE_FLAG_QOS_ENABLED, &q->queue_flags)) &&
			q->rq_qos) {
		bio_set_flag(bio, BIO_QOS_THROTTLED);
		__rq_qos_throttle(q->rq_qos, bio);
	}
}

static inline void rq_qos_track(struct request_queue *q, struct request *rq,
				struct bio *bio)
{
	if (unlikely(test_bit(QUEUE_FLAG_QOS_ENABLED, &q->queue_flags)) &&
			q->rq_qos)
		__rq_qos_track(q->rq_qos, rq, bio);
}

static inline void rq_qos_merge(struct request_queue *q, struct request *rq,
				struct bio *bio)
{
	if (unlikely(test_bit(QUEUE_FLAG_QOS_ENABLED, &q->queue_flags)) &&
			q->rq_qos) {
		bio_set_flag(bio, BIO_QOS_MERGED);
		__rq_qos_merge(q->rq_qos, rq, bio);
	}
}

static inline void rq_qos_queue_depth_changed(struct request_queue *q)
{
	if (unlikely(test_bit(QUEUE_FLAG_QOS_ENABLED, &q->queue_flags)) &&
			q->rq_qos)
		__rq_qos_queue_depth_changed(q->rq_qos);
}

void rq_qos_exit(struct request_queue *);

#endif
