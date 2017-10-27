/*
 * The Kyber I/O scheduler. Controls latency by throttling queue depths using
 * scalable techniques.
 *
 * Copyright (C) 2017 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/elevator.h>
#include <linux/module.h>
#include <linux/sbitmap.h>

#include "blk.h"
#include "blk-mq.h"
#include "blk-mq-debugfs.h"
#include "blk-mq-sched.h"
#include "blk-mq-tag.h"
#include "blk-stat.h"

/* Scheduling domains. */
enum {
	KYBER_READ,
	KYBER_SYNC_WRITE,
	KYBER_OTHER, /* Async writes, discard, etc. */
	KYBER_NUM_DOMAINS,
};

enum {
	KYBER_MIN_DEPTH = 256,

	/*
	 * In order to prevent starvation of synchronous requests by a flood of
	 * asynchronous requests, we reserve 25% of requests for synchronous
	 * operations.
	 */
	KYBER_ASYNC_PERCENT = 75,
};

/*
 * Initial device-wide depths for each scheduling domain.
 *
 * Even for fast devices with lots of tags like NVMe, you can saturate
 * the device with only a fraction of the maximum possible queue depth.
 * So, we cap these to a reasonable value.
 */
static const unsigned int kyber_depth[] = {
	[KYBER_READ] = 256,
	[KYBER_SYNC_WRITE] = 128,
	[KYBER_OTHER] = 64,
};

/*
 * Scheduling domain batch sizes. We favor reads.
 */
static const unsigned int kyber_batch_size[] = {
	[KYBER_READ] = 16,
	[KYBER_SYNC_WRITE] = 8,
	[KYBER_OTHER] = 8,
};

struct kyber_queue_data {
	struct request_queue *q;

	struct blk_stat_callback *cb;

	/*
	 * The device is divided into multiple scheduling domains based on the
	 * request type. Each domain has a fixed number of in-flight requests of
	 * that type device-wide, limited by these tokens.
	 */
	struct sbitmap_queue domain_tokens[KYBER_NUM_DOMAINS];

	/*
	 * Async request percentage, converted to per-word depth for
	 * sbitmap_get_shallow().
	 */
	unsigned int async_depth;

	/* Target latencies in nanoseconds. */
	u64 read_lat_nsec, write_lat_nsec;
};

struct kyber_hctx_data {
	spinlock_t lock;
	struct list_head rqs[KYBER_NUM_DOMAINS];
	unsigned int cur_domain;
	unsigned int batching;
	wait_queue_entry_t domain_wait[KYBER_NUM_DOMAINS];
	atomic_t wait_index[KYBER_NUM_DOMAINS];
};

static int rq_sched_domain(const struct request *rq)
{
	unsigned int op = rq->cmd_flags;

	if ((op & REQ_OP_MASK) == REQ_OP_READ)
		return KYBER_READ;
	else if ((op & REQ_OP_MASK) == REQ_OP_WRITE && op_is_sync(op))
		return KYBER_SYNC_WRITE;
	else
		return KYBER_OTHER;
}

enum {
	NONE = 0,
	GOOD = 1,
	GREAT = 2,
	BAD = -1,
	AWFUL = -2,
};

#define IS_GOOD(status) ((status) > 0)
#define IS_BAD(status) ((status) < 0)

static int kyber_lat_status(struct blk_stat_callback *cb,
			    unsigned int sched_domain, u64 target)
{
	u64 latency;

	if (!cb->stat[sched_domain].nr_samples)
		return NONE;

	latency = cb->stat[sched_domain].mean;
	if (latency >= 2 * target)
		return AWFUL;
	else if (latency > target)
		return BAD;
	else if (latency <= target / 2)
		return GREAT;
	else /* (latency <= target) */
		return GOOD;
}

/*
 * Adjust the read or synchronous write depth given the status of reads and
 * writes. The goal is that the latencies of the two domains are fair (i.e., if
 * one is good, then the other is good).
 */
static void kyber_adjust_rw_depth(struct kyber_queue_data *kqd,
				  unsigned int sched_domain, int this_status,
				  int other_status)
{
	unsigned int orig_depth, depth;

	/*
	 * If this domain had no samples, or reads and writes are both good or
	 * both bad, don't adjust the depth.
	 */
	if (this_status == NONE ||
	    (IS_GOOD(this_status) && IS_GOOD(other_status)) ||
	    (IS_BAD(this_status) && IS_BAD(other_status)))
		return;

	orig_depth = depth = kqd->domain_tokens[sched_domain].sb.depth;

	if (other_status == NONE) {
		depth++;
	} else {
		switch (this_status) {
		case GOOD:
			if (other_status == AWFUL)
				depth -= max(depth / 4, 1U);
			else
				depth -= max(depth / 8, 1U);
			break;
		case GREAT:
			if (other_status == AWFUL)
				depth /= 2;
			else
				depth -= max(depth / 4, 1U);
			break;
		case BAD:
			depth++;
			break;
		case AWFUL:
			if (other_status == GREAT)
				depth += 2;
			else
				depth++;
			break;
		}
	}

	depth = clamp(depth, 1U, kyber_depth[sched_domain]);
	if (depth != orig_depth)
		sbitmap_queue_resize(&kqd->domain_tokens[sched_domain], depth);
}

/*
 * Adjust the depth of other requests given the status of reads and synchronous
 * writes. As long as either domain is doing fine, we don't throttle, but if
 * both domains are doing badly, we throttle heavily.
 */
static void kyber_adjust_other_depth(struct kyber_queue_data *kqd,
				     int read_status, int write_status,
				     bool have_samples)
{
	unsigned int orig_depth, depth;
	int status;

	orig_depth = depth = kqd->domain_tokens[KYBER_OTHER].sb.depth;

	if (read_status == NONE && write_status == NONE) {
		depth += 2;
	} else if (have_samples) {
		if (read_status == NONE)
			status = write_status;
		else if (write_status == NONE)
			status = read_status;
		else
			status = max(read_status, write_status);
		switch (status) {
		case GREAT:
			depth += 2;
			break;
		case GOOD:
			depth++;
			break;
		case BAD:
			depth -= max(depth / 4, 1U);
			break;
		case AWFUL:
			depth /= 2;
			break;
		}
	}

	depth = clamp(depth, 1U, kyber_depth[KYBER_OTHER]);
	if (depth != orig_depth)
		sbitmap_queue_resize(&kqd->domain_tokens[KYBER_OTHER], depth);
}

/*
 * Apply heuristics for limiting queue depths based on gathered latency
 * statistics.
 */
static void kyber_stat_timer_fn(struct blk_stat_callback *cb)
{
	struct kyber_queue_data *kqd = cb->data;
	int read_status, write_status;

	read_status = kyber_lat_status(cb, KYBER_READ, kqd->read_lat_nsec);
	write_status = kyber_lat_status(cb, KYBER_SYNC_WRITE, kqd->write_lat_nsec);

	kyber_adjust_rw_depth(kqd, KYBER_READ, read_status, write_status);
	kyber_adjust_rw_depth(kqd, KYBER_SYNC_WRITE, write_status, read_status);
	kyber_adjust_other_depth(kqd, read_status, write_status,
				 cb->stat[KYBER_OTHER].nr_samples != 0);

	/*
	 * Continue monitoring latencies if we aren't hitting the targets or
	 * we're still throttling other requests.
	 */
	if (!blk_stat_is_active(kqd->cb) &&
	    ((IS_BAD(read_status) || IS_BAD(write_status) ||
	      kqd->domain_tokens[KYBER_OTHER].sb.depth < kyber_depth[KYBER_OTHER])))
		blk_stat_activate_msecs(kqd->cb, 100);
}

static unsigned int kyber_sched_tags_shift(struct kyber_queue_data *kqd)
{
	/*
	 * All of the hardware queues have the same depth, so we can just grab
	 * the shift of the first one.
	 */
	return kqd->q->queue_hw_ctx[0]->sched_tags->bitmap_tags.sb.shift;
}

static struct kyber_queue_data *kyber_queue_data_alloc(struct request_queue *q)
{
	struct kyber_queue_data *kqd;
	unsigned int max_tokens;
	unsigned int shift;
	int ret = -ENOMEM;
	int i;

	kqd = kmalloc_node(sizeof(*kqd), GFP_KERNEL, q->node);
	if (!kqd)
		goto err;
	kqd->q = q;

	kqd->cb = blk_stat_alloc_callback(kyber_stat_timer_fn, rq_sched_domain,
					  KYBER_NUM_DOMAINS, kqd);
	if (!kqd->cb)
		goto err_kqd;

	/*
	 * The maximum number of tokens for any scheduling domain is at least
	 * the queue depth of a single hardware queue. If the hardware doesn't
	 * have many tags, still provide a reasonable number.
	 */
	max_tokens = max_t(unsigned int, q->tag_set->queue_depth,
			   KYBER_MIN_DEPTH);
	for (i = 0; i < KYBER_NUM_DOMAINS; i++) {
		WARN_ON(!kyber_depth[i]);
		WARN_ON(!kyber_batch_size[i]);
		ret = sbitmap_queue_init_node(&kqd->domain_tokens[i],
					      max_tokens, -1, false, GFP_KERNEL,
					      q->node);
		if (ret) {
			while (--i >= 0)
				sbitmap_queue_free(&kqd->domain_tokens[i]);
			goto err_cb;
		}
		sbitmap_queue_resize(&kqd->domain_tokens[i], kyber_depth[i]);
	}

	shift = kyber_sched_tags_shift(kqd);
	kqd->async_depth = (1U << shift) * KYBER_ASYNC_PERCENT / 100U;

	kqd->read_lat_nsec = 2000000ULL;
	kqd->write_lat_nsec = 10000000ULL;

	return kqd;

err_cb:
	blk_stat_free_callback(kqd->cb);
err_kqd:
	kfree(kqd);
err:
	return ERR_PTR(ret);
}

static int kyber_init_sched(struct request_queue *q, struct elevator_type *e)
{
	struct kyber_queue_data *kqd;
	struct elevator_queue *eq;

	eq = elevator_alloc(q, e);
	if (!eq)
		return -ENOMEM;

	kqd = kyber_queue_data_alloc(q);
	if (IS_ERR(kqd)) {
		kobject_put(&eq->kobj);
		return PTR_ERR(kqd);
	}

	eq->elevator_data = kqd;
	q->elevator = eq;

	blk_stat_add_callback(q, kqd->cb);

	return 0;
}

static void kyber_exit_sched(struct elevator_queue *e)
{
	struct kyber_queue_data *kqd = e->elevator_data;
	struct request_queue *q = kqd->q;
	int i;

	blk_stat_remove_callback(q, kqd->cb);

	for (i = 0; i < KYBER_NUM_DOMAINS; i++)
		sbitmap_queue_free(&kqd->domain_tokens[i]);
	blk_stat_free_callback(kqd->cb);
	kfree(kqd);
}

static int kyber_init_hctx(struct blk_mq_hw_ctx *hctx, unsigned int hctx_idx)
{
	struct kyber_hctx_data *khd;
	int i;

	khd = kmalloc_node(sizeof(*khd), GFP_KERNEL, hctx->numa_node);
	if (!khd)
		return -ENOMEM;

	spin_lock_init(&khd->lock);

	for (i = 0; i < KYBER_NUM_DOMAINS; i++) {
		INIT_LIST_HEAD(&khd->rqs[i]);
		INIT_LIST_HEAD(&khd->domain_wait[i].entry);
		atomic_set(&khd->wait_index[i], 0);
	}

	khd->cur_domain = 0;
	khd->batching = 0;

	hctx->sched_data = khd;

	return 0;
}

static void kyber_exit_hctx(struct blk_mq_hw_ctx *hctx, unsigned int hctx_idx)
{
	kfree(hctx->sched_data);
}

static int rq_get_domain_token(struct request *rq)
{
	return (long)rq->elv.priv[0];
}

static void rq_set_domain_token(struct request *rq, int token)
{
	rq->elv.priv[0] = (void *)(long)token;
}

static void rq_clear_domain_token(struct kyber_queue_data *kqd,
				  struct request *rq)
{
	unsigned int sched_domain;
	int nr;

	nr = rq_get_domain_token(rq);
	if (nr != -1) {
		sched_domain = rq_sched_domain(rq);
		sbitmap_queue_clear(&kqd->domain_tokens[sched_domain], nr,
				    rq->mq_ctx->cpu);
	}
}

static void kyber_limit_depth(unsigned int op, struct blk_mq_alloc_data *data)
{
	/*
	 * We use the scheduler tags as per-hardware queue queueing tokens.
	 * Async requests can be limited at this stage.
	 */
	if (!op_is_sync(op)) {
		struct kyber_queue_data *kqd = data->q->elevator->elevator_data;

		data->shallow_depth = kqd->async_depth;
	}
}

static void kyber_prepare_request(struct request *rq, struct bio *bio)
{
	rq_set_domain_token(rq, -1);
}

static void kyber_finish_request(struct request *rq)
{
	struct kyber_queue_data *kqd = rq->q->elevator->elevator_data;

	rq_clear_domain_token(kqd, rq);
}

static void kyber_completed_request(struct request *rq)
{
	struct request_queue *q = rq->q;
	struct kyber_queue_data *kqd = q->elevator->elevator_data;
	unsigned int sched_domain;
	u64 now, latency, target;

	/*
	 * Check if this request met our latency goal. If not, quickly gather
	 * some statistics and start throttling.
	 */
	sched_domain = rq_sched_domain(rq);
	switch (sched_domain) {
	case KYBER_READ:
		target = kqd->read_lat_nsec;
		break;
	case KYBER_SYNC_WRITE:
		target = kqd->write_lat_nsec;
		break;
	default:
		return;
	}

	/* If we are already monitoring latencies, don't check again. */
	if (blk_stat_is_active(kqd->cb))
		return;

	now = __blk_stat_time(ktime_to_ns(ktime_get()));
	if (now < blk_stat_time(&rq->issue_stat))
		return;

	latency = now - blk_stat_time(&rq->issue_stat);

	if (latency > target)
		blk_stat_activate_msecs(kqd->cb, 10);
}

static void kyber_flush_busy_ctxs(struct kyber_hctx_data *khd,
				  struct blk_mq_hw_ctx *hctx)
{
	LIST_HEAD(rq_list);
	struct request *rq, *next;

	blk_mq_flush_busy_ctxs(hctx, &rq_list);
	list_for_each_entry_safe(rq, next, &rq_list, queuelist) {
		unsigned int sched_domain;

		sched_domain = rq_sched_domain(rq);
		list_move_tail(&rq->queuelist, &khd->rqs[sched_domain]);
	}
}

static int kyber_domain_wake(wait_queue_entry_t *wait, unsigned mode, int flags,
			     void *key)
{
	struct blk_mq_hw_ctx *hctx = READ_ONCE(wait->private);

	list_del_init(&wait->entry);
	blk_mq_run_hw_queue(hctx, true);
	return 1;
}

static int kyber_get_domain_token(struct kyber_queue_data *kqd,
				  struct kyber_hctx_data *khd,
				  struct blk_mq_hw_ctx *hctx)
{
	unsigned int sched_domain = khd->cur_domain;
	struct sbitmap_queue *domain_tokens = &kqd->domain_tokens[sched_domain];
	wait_queue_entry_t *wait = &khd->domain_wait[sched_domain];
	struct sbq_wait_state *ws;
	int nr;

	nr = __sbitmap_queue_get(domain_tokens);
	if (nr >= 0)
		return nr;

	/*
	 * If we failed to get a domain token, make sure the hardware queue is
	 * run when one becomes available. Note that this is serialized on
	 * khd->lock, but we still need to be careful about the waker.
	 */
	if (list_empty_careful(&wait->entry)) {
		init_waitqueue_func_entry(wait, kyber_domain_wake);
		wait->private = hctx;
		ws = sbq_wait_ptr(domain_tokens,
				  &khd->wait_index[sched_domain]);
		add_wait_queue(&ws->wait, wait);

		/*
		 * Try again in case a token was freed before we got on the wait
		 * queue.
		 */
		nr = __sbitmap_queue_get(domain_tokens);
	}
	return nr;
}

static struct request *
kyber_dispatch_cur_domain(struct kyber_queue_data *kqd,
			  struct kyber_hctx_data *khd,
			  struct blk_mq_hw_ctx *hctx,
			  bool *flushed)
{
	struct list_head *rqs;
	struct request *rq;
	int nr;

	rqs = &khd->rqs[khd->cur_domain];
	rq = list_first_entry_or_null(rqs, struct request, queuelist);

	/*
	 * If there wasn't already a pending request and we haven't flushed the
	 * software queues yet, flush the software queues and check again.
	 */
	if (!rq && !*flushed) {
		kyber_flush_busy_ctxs(khd, hctx);
		*flushed = true;
		rq = list_first_entry_or_null(rqs, struct request, queuelist);
	}

	if (rq) {
		nr = kyber_get_domain_token(kqd, khd, hctx);
		if (nr >= 0) {
			khd->batching++;
			rq_set_domain_token(rq, nr);
			list_del_init(&rq->queuelist);
			return rq;
		}
	}

	/* There were either no pending requests or no tokens. */
	return NULL;
}

static struct request *kyber_dispatch_request(struct blk_mq_hw_ctx *hctx)
{
	struct kyber_queue_data *kqd = hctx->queue->elevator->elevator_data;
	struct kyber_hctx_data *khd = hctx->sched_data;
	bool flushed = false;
	struct request *rq;
	int i;

	spin_lock(&khd->lock);

	/*
	 * First, if we are still entitled to batch, try to dispatch a request
	 * from the batch.
	 */
	if (khd->batching < kyber_batch_size[khd->cur_domain]) {
		rq = kyber_dispatch_cur_domain(kqd, khd, hctx, &flushed);
		if (rq)
			goto out;
	}

	/*
	 * Either,
	 * 1. We were no longer entitled to a batch.
	 * 2. The domain we were batching didn't have any requests.
	 * 3. The domain we were batching was out of tokens.
	 *
	 * Start another batch. Note that this wraps back around to the original
	 * domain if no other domains have requests or tokens.
	 */
	khd->batching = 0;
	for (i = 0; i < KYBER_NUM_DOMAINS; i++) {
		if (khd->cur_domain == KYBER_NUM_DOMAINS - 1)
			khd->cur_domain = 0;
		else
			khd->cur_domain++;

		rq = kyber_dispatch_cur_domain(kqd, khd, hctx, &flushed);
		if (rq)
			goto out;
	}

	rq = NULL;
out:
	spin_unlock(&khd->lock);
	return rq;
}

static bool kyber_has_work(struct blk_mq_hw_ctx *hctx)
{
	struct kyber_hctx_data *khd = hctx->sched_data;
	int i;

	for (i = 0; i < KYBER_NUM_DOMAINS; i++) {
		if (!list_empty_careful(&khd->rqs[i]))
			return true;
	}
	return false;
}

#define KYBER_LAT_SHOW_STORE(op)					\
static ssize_t kyber_##op##_lat_show(struct elevator_queue *e,		\
				     char *page)			\
{									\
	struct kyber_queue_data *kqd = e->elevator_data;		\
									\
	return sprintf(page, "%llu\n", kqd->op##_lat_nsec);		\
}									\
									\
static ssize_t kyber_##op##_lat_store(struct elevator_queue *e,		\
				      const char *page, size_t count)	\
{									\
	struct kyber_queue_data *kqd = e->elevator_data;		\
	unsigned long long nsec;					\
	int ret;							\
									\
	ret = kstrtoull(page, 10, &nsec);				\
	if (ret)							\
		return ret;						\
									\
	kqd->op##_lat_nsec = nsec;					\
									\
	return count;							\
}
KYBER_LAT_SHOW_STORE(read);
KYBER_LAT_SHOW_STORE(write);
#undef KYBER_LAT_SHOW_STORE

#define KYBER_LAT_ATTR(op) __ATTR(op##_lat_nsec, 0644, kyber_##op##_lat_show, kyber_##op##_lat_store)
static struct elv_fs_entry kyber_sched_attrs[] = {
	KYBER_LAT_ATTR(read),
	KYBER_LAT_ATTR(write),
	__ATTR_NULL
};
#undef KYBER_LAT_ATTR

#ifdef CONFIG_BLK_DEBUG_FS
#define KYBER_DEBUGFS_DOMAIN_ATTRS(domain, name)			\
static int kyber_##name##_tokens_show(void *data, struct seq_file *m)	\
{									\
	struct request_queue *q = data;					\
	struct kyber_queue_data *kqd = q->elevator->elevator_data;	\
									\
	sbitmap_queue_show(&kqd->domain_tokens[domain], m);		\
	return 0;							\
}									\
									\
static void *kyber_##name##_rqs_start(struct seq_file *m, loff_t *pos)	\
	__acquires(&khd->lock)						\
{									\
	struct blk_mq_hw_ctx *hctx = m->private;			\
	struct kyber_hctx_data *khd = hctx->sched_data;			\
									\
	spin_lock(&khd->lock);						\
	return seq_list_start(&khd->rqs[domain], *pos);			\
}									\
									\
static void *kyber_##name##_rqs_next(struct seq_file *m, void *v,	\
				     loff_t *pos)			\
{									\
	struct blk_mq_hw_ctx *hctx = m->private;			\
	struct kyber_hctx_data *khd = hctx->sched_data;			\
									\
	return seq_list_next(v, &khd->rqs[domain], pos);		\
}									\
									\
static void kyber_##name##_rqs_stop(struct seq_file *m, void *v)	\
	__releases(&khd->lock)						\
{									\
	struct blk_mq_hw_ctx *hctx = m->private;			\
	struct kyber_hctx_data *khd = hctx->sched_data;			\
									\
	spin_unlock(&khd->lock);					\
}									\
									\
static const struct seq_operations kyber_##name##_rqs_seq_ops = {	\
	.start	= kyber_##name##_rqs_start,				\
	.next	= kyber_##name##_rqs_next,				\
	.stop	= kyber_##name##_rqs_stop,				\
	.show	= blk_mq_debugfs_rq_show,				\
};									\
									\
static int kyber_##name##_waiting_show(void *data, struct seq_file *m)	\
{									\
	struct blk_mq_hw_ctx *hctx = data;				\
	struct kyber_hctx_data *khd = hctx->sched_data;			\
	wait_queue_entry_t *wait = &khd->domain_wait[domain];		\
									\
	seq_printf(m, "%d\n", !list_empty_careful(&wait->entry));	\
	return 0;							\
}
KYBER_DEBUGFS_DOMAIN_ATTRS(KYBER_READ, read)
KYBER_DEBUGFS_DOMAIN_ATTRS(KYBER_SYNC_WRITE, sync_write)
KYBER_DEBUGFS_DOMAIN_ATTRS(KYBER_OTHER, other)
#undef KYBER_DEBUGFS_DOMAIN_ATTRS

static int kyber_async_depth_show(void *data, struct seq_file *m)
{
	struct request_queue *q = data;
	struct kyber_queue_data *kqd = q->elevator->elevator_data;

	seq_printf(m, "%u\n", kqd->async_depth);
	return 0;
}

static int kyber_cur_domain_show(void *data, struct seq_file *m)
{
	struct blk_mq_hw_ctx *hctx = data;
	struct kyber_hctx_data *khd = hctx->sched_data;

	switch (khd->cur_domain) {
	case KYBER_READ:
		seq_puts(m, "READ\n");
		break;
	case KYBER_SYNC_WRITE:
		seq_puts(m, "SYNC_WRITE\n");
		break;
	case KYBER_OTHER:
		seq_puts(m, "OTHER\n");
		break;
	default:
		seq_printf(m, "%u\n", khd->cur_domain);
		break;
	}
	return 0;
}

static int kyber_batching_show(void *data, struct seq_file *m)
{
	struct blk_mq_hw_ctx *hctx = data;
	struct kyber_hctx_data *khd = hctx->sched_data;

	seq_printf(m, "%u\n", khd->batching);
	return 0;
}

#define KYBER_QUEUE_DOMAIN_ATTRS(name)	\
	{#name "_tokens", 0400, kyber_##name##_tokens_show}
static const struct blk_mq_debugfs_attr kyber_queue_debugfs_attrs[] = {
	KYBER_QUEUE_DOMAIN_ATTRS(read),
	KYBER_QUEUE_DOMAIN_ATTRS(sync_write),
	KYBER_QUEUE_DOMAIN_ATTRS(other),
	{"async_depth", 0400, kyber_async_depth_show},
	{},
};
#undef KYBER_QUEUE_DOMAIN_ATTRS

#define KYBER_HCTX_DOMAIN_ATTRS(name)					\
	{#name "_rqs", 0400, .seq_ops = &kyber_##name##_rqs_seq_ops},	\
	{#name "_waiting", 0400, kyber_##name##_waiting_show}
static const struct blk_mq_debugfs_attr kyber_hctx_debugfs_attrs[] = {
	KYBER_HCTX_DOMAIN_ATTRS(read),
	KYBER_HCTX_DOMAIN_ATTRS(sync_write),
	KYBER_HCTX_DOMAIN_ATTRS(other),
	{"cur_domain", 0400, kyber_cur_domain_show},
	{"batching", 0400, kyber_batching_show},
	{},
};
#undef KYBER_HCTX_DOMAIN_ATTRS
#endif

static struct elevator_type kyber_sched = {
	.ops.mq = {
		.init_sched = kyber_init_sched,
		.exit_sched = kyber_exit_sched,
		.init_hctx = kyber_init_hctx,
		.exit_hctx = kyber_exit_hctx,
		.limit_depth = kyber_limit_depth,
		.prepare_request = kyber_prepare_request,
		.finish_request = kyber_finish_request,
		.completed_request = kyber_completed_request,
		.dispatch_request = kyber_dispatch_request,
		.has_work = kyber_has_work,
	},
	.uses_mq = true,
#ifdef CONFIG_BLK_DEBUG_FS
	.queue_debugfs_attrs = kyber_queue_debugfs_attrs,
	.hctx_debugfs_attrs = kyber_hctx_debugfs_attrs,
#endif
	.elevator_attrs = kyber_sched_attrs,
	.elevator_name = "kyber",
	.elevator_owner = THIS_MODULE,
};

static int __init kyber_init(void)
{
	return elv_register(&kyber_sched);
}

static void __exit kyber_exit(void)
{
	elv_unregister(&kyber_sched);
}

module_init(kyber_init);
module_exit(kyber_exit);

MODULE_AUTHOR("Omar Sandoval");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kyber I/O scheduler");
