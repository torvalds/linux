/*
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
#include <linux/debugfs.h>

#include <linux/blk-mq.h>
#include "blk.h"
#include "blk-mq.h"
#include "blk-mq-debugfs.h"
#include "blk-mq-tag.h"

static int blk_flags_show(struct seq_file *m, const unsigned long flags,
			  const char *const *flag_name, int flag_name_count)
{
	bool sep = false;
	int i;

	for (i = 0; i < sizeof(flags) * BITS_PER_BYTE; i++) {
		if (!(flags & BIT(i)))
			continue;
		if (sep)
			seq_puts(m, "|");
		sep = true;
		if (i < flag_name_count && flag_name[i])
			seq_puts(m, flag_name[i]);
		else
			seq_printf(m, "%d", i);
	}
	return 0;
}

#define QUEUE_FLAG_NAME(name) [QUEUE_FLAG_##name] = #name
static const char *const blk_queue_flag_name[] = {
	QUEUE_FLAG_NAME(QUEUED),
	QUEUE_FLAG_NAME(STOPPED),
	QUEUE_FLAG_NAME(DYING),
	QUEUE_FLAG_NAME(BYPASS),
	QUEUE_FLAG_NAME(BIDI),
	QUEUE_FLAG_NAME(NOMERGES),
	QUEUE_FLAG_NAME(SAME_COMP),
	QUEUE_FLAG_NAME(FAIL_IO),
	QUEUE_FLAG_NAME(NONROT),
	QUEUE_FLAG_NAME(IO_STAT),
	QUEUE_FLAG_NAME(DISCARD),
	QUEUE_FLAG_NAME(NOXMERGES),
	QUEUE_FLAG_NAME(ADD_RANDOM),
	QUEUE_FLAG_NAME(SECERASE),
	QUEUE_FLAG_NAME(SAME_FORCE),
	QUEUE_FLAG_NAME(DEAD),
	QUEUE_FLAG_NAME(INIT_DONE),
	QUEUE_FLAG_NAME(NO_SG_MERGE),
	QUEUE_FLAG_NAME(POLL),
	QUEUE_FLAG_NAME(WC),
	QUEUE_FLAG_NAME(FUA),
	QUEUE_FLAG_NAME(FLUSH_NQ),
	QUEUE_FLAG_NAME(DAX),
	QUEUE_FLAG_NAME(STATS),
	QUEUE_FLAG_NAME(POLL_STATS),
	QUEUE_FLAG_NAME(REGISTERED),
	QUEUE_FLAG_NAME(SCSI_PASSTHROUGH),
	QUEUE_FLAG_NAME(QUIESCED),
	QUEUE_FLAG_NAME(PREEMPT_ONLY),
};
#undef QUEUE_FLAG_NAME

static int queue_state_show(void *data, struct seq_file *m)
{
	struct request_queue *q = data;

	blk_flags_show(m, q->queue_flags, blk_queue_flag_name,
		       ARRAY_SIZE(blk_queue_flag_name));
	seq_puts(m, "\n");
	return 0;
}

static ssize_t queue_state_write(void *data, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct request_queue *q = data;
	char opbuf[16] = { }, *op;

	/*
	 * The "state" attribute is removed after blk_cleanup_queue() has called
	 * blk_mq_free_queue(). Return if QUEUE_FLAG_DEAD has been set to avoid
	 * triggering a use-after-free.
	 */
	if (blk_queue_dead(q))
		return -ENOENT;

	if (count >= sizeof(opbuf)) {
		pr_err("%s: operation too long\n", __func__);
		goto inval;
	}

	if (copy_from_user(opbuf, buf, count))
		return -EFAULT;
	op = strstrip(opbuf);
	if (strcmp(op, "run") == 0) {
		blk_mq_run_hw_queues(q, true);
	} else if (strcmp(op, "start") == 0) {
		blk_mq_start_stopped_hw_queues(q, true);
	} else if (strcmp(op, "kick") == 0) {
		blk_mq_kick_requeue_list(q);
	} else {
		pr_err("%s: unsupported operation '%s'\n", __func__, op);
inval:
		pr_err("%s: use 'run', 'start' or 'kick'\n", __func__);
		return -EINVAL;
	}
	return count;
}

static void print_stat(struct seq_file *m, struct blk_rq_stat *stat)
{
	if (stat->nr_samples) {
		seq_printf(m, "samples=%d, mean=%lld, min=%llu, max=%llu",
			   stat->nr_samples, stat->mean, stat->min, stat->max);
	} else {
		seq_puts(m, "samples=0");
	}
}

static int queue_write_hint_show(void *data, struct seq_file *m)
{
	struct request_queue *q = data;
	int i;

	for (i = 0; i < BLK_MAX_WRITE_HINTS; i++)
		seq_printf(m, "hint%d: %llu\n", i, q->write_hints[i]);

	return 0;
}

static ssize_t queue_write_hint_store(void *data, const char __user *buf,
				      size_t count, loff_t *ppos)
{
	struct request_queue *q = data;
	int i;

	for (i = 0; i < BLK_MAX_WRITE_HINTS; i++)
		q->write_hints[i] = 0;

	return count;
}

static int queue_poll_stat_show(void *data, struct seq_file *m)
{
	struct request_queue *q = data;
	int bucket;

	for (bucket = 0; bucket < BLK_MQ_POLL_STATS_BKTS/2; bucket++) {
		seq_printf(m, "read  (%d Bytes): ", 1 << (9+bucket));
		print_stat(m, &q->poll_stat[2*bucket]);
		seq_puts(m, "\n");

		seq_printf(m, "write (%d Bytes): ",  1 << (9+bucket));
		print_stat(m, &q->poll_stat[2*bucket+1]);
		seq_puts(m, "\n");
	}
	return 0;
}

#define HCTX_STATE_NAME(name) [BLK_MQ_S_##name] = #name
static const char *const hctx_state_name[] = {
	HCTX_STATE_NAME(STOPPED),
	HCTX_STATE_NAME(TAG_ACTIVE),
	HCTX_STATE_NAME(SCHED_RESTART),
	HCTX_STATE_NAME(START_ON_RUN),
};
#undef HCTX_STATE_NAME

static int hctx_state_show(void *data, struct seq_file *m)
{
	struct blk_mq_hw_ctx *hctx = data;

	blk_flags_show(m, hctx->state, hctx_state_name,
		       ARRAY_SIZE(hctx_state_name));
	seq_puts(m, "\n");
	return 0;
}

#define BLK_TAG_ALLOC_NAME(name) [BLK_TAG_ALLOC_##name] = #name
static const char *const alloc_policy_name[] = {
	BLK_TAG_ALLOC_NAME(FIFO),
	BLK_TAG_ALLOC_NAME(RR),
};
#undef BLK_TAG_ALLOC_NAME

#define HCTX_FLAG_NAME(name) [ilog2(BLK_MQ_F_##name)] = #name
static const char *const hctx_flag_name[] = {
	HCTX_FLAG_NAME(SHOULD_MERGE),
	HCTX_FLAG_NAME(TAG_SHARED),
	HCTX_FLAG_NAME(SG_MERGE),
	HCTX_FLAG_NAME(BLOCKING),
	HCTX_FLAG_NAME(NO_SCHED),
};
#undef HCTX_FLAG_NAME

static int hctx_flags_show(void *data, struct seq_file *m)
{
	struct blk_mq_hw_ctx *hctx = data;
	const int alloc_policy = BLK_MQ_FLAG_TO_ALLOC_POLICY(hctx->flags);

	seq_puts(m, "alloc_policy=");
	if (alloc_policy < ARRAY_SIZE(alloc_policy_name) &&
	    alloc_policy_name[alloc_policy])
		seq_puts(m, alloc_policy_name[alloc_policy]);
	else
		seq_printf(m, "%d", alloc_policy);
	seq_puts(m, " ");
	blk_flags_show(m,
		       hctx->flags ^ BLK_ALLOC_POLICY_TO_MQ_FLAG(alloc_policy),
		       hctx_flag_name, ARRAY_SIZE(hctx_flag_name));
	seq_puts(m, "\n");
	return 0;
}

#define REQ_OP_NAME(name) [REQ_OP_##name] = #name
static const char *const op_name[] = {
	REQ_OP_NAME(READ),
	REQ_OP_NAME(WRITE),
	REQ_OP_NAME(FLUSH),
	REQ_OP_NAME(DISCARD),
	REQ_OP_NAME(ZONE_REPORT),
	REQ_OP_NAME(SECURE_ERASE),
	REQ_OP_NAME(ZONE_RESET),
	REQ_OP_NAME(WRITE_SAME),
	REQ_OP_NAME(WRITE_ZEROES),
	REQ_OP_NAME(SCSI_IN),
	REQ_OP_NAME(SCSI_OUT),
	REQ_OP_NAME(DRV_IN),
	REQ_OP_NAME(DRV_OUT),
};
#undef REQ_OP_NAME

#define CMD_FLAG_NAME(name) [__REQ_##name] = #name
static const char *const cmd_flag_name[] = {
	CMD_FLAG_NAME(FAILFAST_DEV),
	CMD_FLAG_NAME(FAILFAST_TRANSPORT),
	CMD_FLAG_NAME(FAILFAST_DRIVER),
	CMD_FLAG_NAME(SYNC),
	CMD_FLAG_NAME(META),
	CMD_FLAG_NAME(PRIO),
	CMD_FLAG_NAME(NOMERGE),
	CMD_FLAG_NAME(IDLE),
	CMD_FLAG_NAME(INTEGRITY),
	CMD_FLAG_NAME(FUA),
	CMD_FLAG_NAME(PREFLUSH),
	CMD_FLAG_NAME(RAHEAD),
	CMD_FLAG_NAME(BACKGROUND),
	CMD_FLAG_NAME(NOUNMAP),
	CMD_FLAG_NAME(NOWAIT),
};
#undef CMD_FLAG_NAME

#define RQF_NAME(name) [ilog2((__force u32)RQF_##name)] = #name
static const char *const rqf_name[] = {
	RQF_NAME(SORTED),
	RQF_NAME(STARTED),
	RQF_NAME(QUEUED),
	RQF_NAME(SOFTBARRIER),
	RQF_NAME(FLUSH_SEQ),
	RQF_NAME(MIXED_MERGE),
	RQF_NAME(MQ_INFLIGHT),
	RQF_NAME(DONTPREP),
	RQF_NAME(PREEMPT),
	RQF_NAME(COPY_USER),
	RQF_NAME(FAILED),
	RQF_NAME(QUIET),
	RQF_NAME(ELVPRIV),
	RQF_NAME(IO_STAT),
	RQF_NAME(ALLOCED),
	RQF_NAME(PM),
	RQF_NAME(HASHED),
	RQF_NAME(STATS),
	RQF_NAME(SPECIAL_PAYLOAD),
};
#undef RQF_NAME

#define RQAF_NAME(name) [REQ_ATOM_##name] = #name
static const char *const rqaf_name[] = {
	RQAF_NAME(COMPLETE),
	RQAF_NAME(STARTED),
	RQAF_NAME(POLL_SLEPT),
};
#undef RQAF_NAME

int __blk_mq_debugfs_rq_show(struct seq_file *m, struct request *rq)
{
	const struct blk_mq_ops *const mq_ops = rq->q->mq_ops;
	const unsigned int op = rq->cmd_flags & REQ_OP_MASK;

	seq_printf(m, "%p {.op=", rq);
	if (op < ARRAY_SIZE(op_name) && op_name[op])
		seq_printf(m, "%s", op_name[op]);
	else
		seq_printf(m, "%d", op);
	seq_puts(m, ", .cmd_flags=");
	blk_flags_show(m, rq->cmd_flags & ~REQ_OP_MASK, cmd_flag_name,
		       ARRAY_SIZE(cmd_flag_name));
	seq_puts(m, ", .rq_flags=");
	blk_flags_show(m, (__force unsigned int)rq->rq_flags, rqf_name,
		       ARRAY_SIZE(rqf_name));
	seq_puts(m, ", .atomic_flags=");
	blk_flags_show(m, rq->atomic_flags, rqaf_name, ARRAY_SIZE(rqaf_name));
	seq_printf(m, ", .tag=%d, .internal_tag=%d", rq->tag,
		   rq->internal_tag);
	if (mq_ops->show_rq)
		mq_ops->show_rq(m, rq);
	seq_puts(m, "}\n");
	return 0;
}
EXPORT_SYMBOL_GPL(__blk_mq_debugfs_rq_show);

int blk_mq_debugfs_rq_show(struct seq_file *m, void *v)
{
	return __blk_mq_debugfs_rq_show(m, list_entry_rq(v));
}
EXPORT_SYMBOL_GPL(blk_mq_debugfs_rq_show);

static void *queue_requeue_list_start(struct seq_file *m, loff_t *pos)
	__acquires(&q->requeue_lock)
{
	struct request_queue *q = m->private;

	spin_lock_irq(&q->requeue_lock);
	return seq_list_start(&q->requeue_list, *pos);
}

static void *queue_requeue_list_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct request_queue *q = m->private;

	return seq_list_next(v, &q->requeue_list, pos);
}

static void queue_requeue_list_stop(struct seq_file *m, void *v)
	__releases(&q->requeue_lock)
{
	struct request_queue *q = m->private;

	spin_unlock_irq(&q->requeue_lock);
}

static const struct seq_operations queue_requeue_list_seq_ops = {
	.start	= queue_requeue_list_start,
	.next	= queue_requeue_list_next,
	.stop	= queue_requeue_list_stop,
	.show	= blk_mq_debugfs_rq_show,
};

static void *hctx_dispatch_start(struct seq_file *m, loff_t *pos)
	__acquires(&hctx->lock)
{
	struct blk_mq_hw_ctx *hctx = m->private;

	spin_lock(&hctx->lock);
	return seq_list_start(&hctx->dispatch, *pos);
}

static void *hctx_dispatch_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct blk_mq_hw_ctx *hctx = m->private;

	return seq_list_next(v, &hctx->dispatch, pos);
}

static void hctx_dispatch_stop(struct seq_file *m, void *v)
	__releases(&hctx->lock)
{
	struct blk_mq_hw_ctx *hctx = m->private;

	spin_unlock(&hctx->lock);
}

static const struct seq_operations hctx_dispatch_seq_ops = {
	.start	= hctx_dispatch_start,
	.next	= hctx_dispatch_next,
	.stop	= hctx_dispatch_stop,
	.show	= blk_mq_debugfs_rq_show,
};

struct show_busy_params {
	struct seq_file		*m;
	struct blk_mq_hw_ctx	*hctx;
};

/*
 * Note: the state of a request may change while this function is in progress,
 * e.g. due to a concurrent blk_mq_finish_request() call.
 */
static void hctx_show_busy_rq(struct request *rq, void *data, bool reserved)
{
	const struct show_busy_params *params = data;

	if (blk_mq_map_queue(rq->q, rq->mq_ctx->cpu) == params->hctx &&
	    test_bit(REQ_ATOM_STARTED, &rq->atomic_flags))
		__blk_mq_debugfs_rq_show(params->m,
					 list_entry_rq(&rq->queuelist));
}

static int hctx_busy_show(void *data, struct seq_file *m)
{
	struct blk_mq_hw_ctx *hctx = data;
	struct show_busy_params params = { .m = m, .hctx = hctx };

	blk_mq_tagset_busy_iter(hctx->queue->tag_set, hctx_show_busy_rq,
				&params);

	return 0;
}

static int hctx_ctx_map_show(void *data, struct seq_file *m)
{
	struct blk_mq_hw_ctx *hctx = data;

	sbitmap_bitmap_show(&hctx->ctx_map, m);
	return 0;
}

static void blk_mq_debugfs_tags_show(struct seq_file *m,
				     struct blk_mq_tags *tags)
{
	seq_printf(m, "nr_tags=%u\n", tags->nr_tags);
	seq_printf(m, "nr_reserved_tags=%u\n", tags->nr_reserved_tags);
	seq_printf(m, "active_queues=%d\n",
		   atomic_read(&tags->active_queues));

	seq_puts(m, "\nbitmap_tags:\n");
	sbitmap_queue_show(&tags->bitmap_tags, m);

	if (tags->nr_reserved_tags) {
		seq_puts(m, "\nbreserved_tags:\n");
		sbitmap_queue_show(&tags->breserved_tags, m);
	}
}

static int hctx_tags_show(void *data, struct seq_file *m)
{
	struct blk_mq_hw_ctx *hctx = data;
	struct request_queue *q = hctx->queue;
	int res;

	res = mutex_lock_interruptible(&q->sysfs_lock);
	if (res)
		goto out;
	if (hctx->tags)
		blk_mq_debugfs_tags_show(m, hctx->tags);
	mutex_unlock(&q->sysfs_lock);

out:
	return res;
}

static int hctx_tags_bitmap_show(void *data, struct seq_file *m)
{
	struct blk_mq_hw_ctx *hctx = data;
	struct request_queue *q = hctx->queue;
	int res;

	res = mutex_lock_interruptible(&q->sysfs_lock);
	if (res)
		goto out;
	if (hctx->tags)
		sbitmap_bitmap_show(&hctx->tags->bitmap_tags.sb, m);
	mutex_unlock(&q->sysfs_lock);

out:
	return res;
}

static int hctx_sched_tags_show(void *data, struct seq_file *m)
{
	struct blk_mq_hw_ctx *hctx = data;
	struct request_queue *q = hctx->queue;
	int res;

	res = mutex_lock_interruptible(&q->sysfs_lock);
	if (res)
		goto out;
	if (hctx->sched_tags)
		blk_mq_debugfs_tags_show(m, hctx->sched_tags);
	mutex_unlock(&q->sysfs_lock);

out:
	return res;
}

static int hctx_sched_tags_bitmap_show(void *data, struct seq_file *m)
{
	struct blk_mq_hw_ctx *hctx = data;
	struct request_queue *q = hctx->queue;
	int res;

	res = mutex_lock_interruptible(&q->sysfs_lock);
	if (res)
		goto out;
	if (hctx->sched_tags)
		sbitmap_bitmap_show(&hctx->sched_tags->bitmap_tags.sb, m);
	mutex_unlock(&q->sysfs_lock);

out:
	return res;
}

static int hctx_io_poll_show(void *data, struct seq_file *m)
{
	struct blk_mq_hw_ctx *hctx = data;

	seq_printf(m, "considered=%lu\n", hctx->poll_considered);
	seq_printf(m, "invoked=%lu\n", hctx->poll_invoked);
	seq_printf(m, "success=%lu\n", hctx->poll_success);
	return 0;
}

static ssize_t hctx_io_poll_write(void *data, const char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct blk_mq_hw_ctx *hctx = data;

	hctx->poll_considered = hctx->poll_invoked = hctx->poll_success = 0;
	return count;
}

static int hctx_dispatched_show(void *data, struct seq_file *m)
{
	struct blk_mq_hw_ctx *hctx = data;
	int i;

	seq_printf(m, "%8u\t%lu\n", 0U, hctx->dispatched[0]);

	for (i = 1; i < BLK_MQ_MAX_DISPATCH_ORDER - 1; i++) {
		unsigned int d = 1U << (i - 1);

		seq_printf(m, "%8u\t%lu\n", d, hctx->dispatched[i]);
	}

	seq_printf(m, "%8u+\t%lu\n", 1U << (i - 1), hctx->dispatched[i]);
	return 0;
}

static ssize_t hctx_dispatched_write(void *data, const char __user *buf,
				     size_t count, loff_t *ppos)
{
	struct blk_mq_hw_ctx *hctx = data;
	int i;

	for (i = 0; i < BLK_MQ_MAX_DISPATCH_ORDER; i++)
		hctx->dispatched[i] = 0;
	return count;
}

static int hctx_queued_show(void *data, struct seq_file *m)
{
	struct blk_mq_hw_ctx *hctx = data;

	seq_printf(m, "%lu\n", hctx->queued);
	return 0;
}

static ssize_t hctx_queued_write(void *data, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct blk_mq_hw_ctx *hctx = data;

	hctx->queued = 0;
	return count;
}

static int hctx_run_show(void *data, struct seq_file *m)
{
	struct blk_mq_hw_ctx *hctx = data;

	seq_printf(m, "%lu\n", hctx->run);
	return 0;
}

static ssize_t hctx_run_write(void *data, const char __user *buf, size_t count,
			      loff_t *ppos)
{
	struct blk_mq_hw_ctx *hctx = data;

	hctx->run = 0;
	return count;
}

static int hctx_active_show(void *data, struct seq_file *m)
{
	struct blk_mq_hw_ctx *hctx = data;

	seq_printf(m, "%d\n", atomic_read(&hctx->nr_active));
	return 0;
}

static void *ctx_rq_list_start(struct seq_file *m, loff_t *pos)
	__acquires(&ctx->lock)
{
	struct blk_mq_ctx *ctx = m->private;

	spin_lock(&ctx->lock);
	return seq_list_start(&ctx->rq_list, *pos);
}

static void *ctx_rq_list_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct blk_mq_ctx *ctx = m->private;

	return seq_list_next(v, &ctx->rq_list, pos);
}

static void ctx_rq_list_stop(struct seq_file *m, void *v)
	__releases(&ctx->lock)
{
	struct blk_mq_ctx *ctx = m->private;

	spin_unlock(&ctx->lock);
}

static const struct seq_operations ctx_rq_list_seq_ops = {
	.start	= ctx_rq_list_start,
	.next	= ctx_rq_list_next,
	.stop	= ctx_rq_list_stop,
	.show	= blk_mq_debugfs_rq_show,
};
static int ctx_dispatched_show(void *data, struct seq_file *m)
{
	struct blk_mq_ctx *ctx = data;

	seq_printf(m, "%lu %lu\n", ctx->rq_dispatched[1], ctx->rq_dispatched[0]);
	return 0;
}

static ssize_t ctx_dispatched_write(void *data, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	struct blk_mq_ctx *ctx = data;

	ctx->rq_dispatched[0] = ctx->rq_dispatched[1] = 0;
	return count;
}

static int ctx_merged_show(void *data, struct seq_file *m)
{
	struct blk_mq_ctx *ctx = data;

	seq_printf(m, "%lu\n", ctx->rq_merged);
	return 0;
}

static ssize_t ctx_merged_write(void *data, const char __user *buf,
				size_t count, loff_t *ppos)
{
	struct blk_mq_ctx *ctx = data;

	ctx->rq_merged = 0;
	return count;
}

static int ctx_completed_show(void *data, struct seq_file *m)
{
	struct blk_mq_ctx *ctx = data;

	seq_printf(m, "%lu %lu\n", ctx->rq_completed[1], ctx->rq_completed[0]);
	return 0;
}

static ssize_t ctx_completed_write(void *data, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	struct blk_mq_ctx *ctx = data;

	ctx->rq_completed[0] = ctx->rq_completed[1] = 0;
	return count;
}

static int blk_mq_debugfs_show(struct seq_file *m, void *v)
{
	const struct blk_mq_debugfs_attr *attr = m->private;
	void *data = d_inode(m->file->f_path.dentry->d_parent)->i_private;

	return attr->show(data, m);
}

static ssize_t blk_mq_debugfs_write(struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	struct seq_file *m = file->private_data;
	const struct blk_mq_debugfs_attr *attr = m->private;
	void *data = d_inode(file->f_path.dentry->d_parent)->i_private;

	if (!attr->write)
		return -EPERM;

	return attr->write(data, buf, count, ppos);
}

static int blk_mq_debugfs_open(struct inode *inode, struct file *file)
{
	const struct blk_mq_debugfs_attr *attr = inode->i_private;
	void *data = d_inode(file->f_path.dentry->d_parent)->i_private;
	struct seq_file *m;
	int ret;

	if (attr->seq_ops) {
		ret = seq_open(file, attr->seq_ops);
		if (!ret) {
			m = file->private_data;
			m->private = data;
		}
		return ret;
	}

	if (WARN_ON_ONCE(!attr->show))
		return -EPERM;

	return single_open(file, blk_mq_debugfs_show, inode->i_private);
}

static int blk_mq_debugfs_release(struct inode *inode, struct file *file)
{
	const struct blk_mq_debugfs_attr *attr = inode->i_private;

	if (attr->show)
		return single_release(inode, file);
	else
		return seq_release(inode, file);
}

static const struct file_operations blk_mq_debugfs_fops = {
	.open		= blk_mq_debugfs_open,
	.read		= seq_read,
	.write		= blk_mq_debugfs_write,
	.llseek		= seq_lseek,
	.release	= blk_mq_debugfs_release,
};

static const struct blk_mq_debugfs_attr blk_mq_debugfs_queue_attrs[] = {
	{"poll_stat", 0400, queue_poll_stat_show},
	{"requeue_list", 0400, .seq_ops = &queue_requeue_list_seq_ops},
	{"state", 0600, queue_state_show, queue_state_write},
	{"write_hints", 0600, queue_write_hint_show, queue_write_hint_store},
	{},
};

static const struct blk_mq_debugfs_attr blk_mq_debugfs_hctx_attrs[] = {
	{"state", 0400, hctx_state_show},
	{"flags", 0400, hctx_flags_show},
	{"dispatch", 0400, .seq_ops = &hctx_dispatch_seq_ops},
	{"busy", 0400, hctx_busy_show},
	{"ctx_map", 0400, hctx_ctx_map_show},
	{"tags", 0400, hctx_tags_show},
	{"tags_bitmap", 0400, hctx_tags_bitmap_show},
	{"sched_tags", 0400, hctx_sched_tags_show},
	{"sched_tags_bitmap", 0400, hctx_sched_tags_bitmap_show},
	{"io_poll", 0600, hctx_io_poll_show, hctx_io_poll_write},
	{"dispatched", 0600, hctx_dispatched_show, hctx_dispatched_write},
	{"queued", 0600, hctx_queued_show, hctx_queued_write},
	{"run", 0600, hctx_run_show, hctx_run_write},
	{"active", 0400, hctx_active_show},
	{},
};

static const struct blk_mq_debugfs_attr blk_mq_debugfs_ctx_attrs[] = {
	{"rq_list", 0400, .seq_ops = &ctx_rq_list_seq_ops},
	{"dispatched", 0600, ctx_dispatched_show, ctx_dispatched_write},
	{"merged", 0600, ctx_merged_show, ctx_merged_write},
	{"completed", 0600, ctx_completed_show, ctx_completed_write},
	{},
};

static bool debugfs_create_files(struct dentry *parent, void *data,
				 const struct blk_mq_debugfs_attr *attr)
{
	d_inode(parent)->i_private = data;

	for (; attr->name; attr++) {
		if (!debugfs_create_file(attr->name, attr->mode, parent,
					 (void *)attr, &blk_mq_debugfs_fops))
			return false;
	}
	return true;
}

int blk_mq_debugfs_register(struct request_queue *q)
{
	struct blk_mq_hw_ctx *hctx;
	int i;

	if (!blk_debugfs_root)
		return -ENOENT;

	q->debugfs_dir = debugfs_create_dir(kobject_name(q->kobj.parent),
					    blk_debugfs_root);
	if (!q->debugfs_dir)
		return -ENOMEM;

	if (!debugfs_create_files(q->debugfs_dir, q,
				  blk_mq_debugfs_queue_attrs))
		goto err;

	/*
	 * blk_mq_init_sched() attempted to do this already, but q->debugfs_dir
	 * didn't exist yet (because we don't know what to name the directory
	 * until the queue is registered to a gendisk).
	 */
	if (q->elevator && !q->sched_debugfs_dir)
		blk_mq_debugfs_register_sched(q);

	/* Similarly, blk_mq_init_hctx() couldn't do this previously. */
	queue_for_each_hw_ctx(q, hctx, i) {
		if (!hctx->debugfs_dir && blk_mq_debugfs_register_hctx(q, hctx))
			goto err;
		if (q->elevator && !hctx->sched_debugfs_dir &&
		    blk_mq_debugfs_register_sched_hctx(q, hctx))
			goto err;
	}

	return 0;

err:
	blk_mq_debugfs_unregister(q);
	return -ENOMEM;
}

void blk_mq_debugfs_unregister(struct request_queue *q)
{
	debugfs_remove_recursive(q->debugfs_dir);
	q->sched_debugfs_dir = NULL;
	q->debugfs_dir = NULL;
}

static int blk_mq_debugfs_register_ctx(struct blk_mq_hw_ctx *hctx,
				       struct blk_mq_ctx *ctx)
{
	struct dentry *ctx_dir;
	char name[20];

	snprintf(name, sizeof(name), "cpu%u", ctx->cpu);
	ctx_dir = debugfs_create_dir(name, hctx->debugfs_dir);
	if (!ctx_dir)
		return -ENOMEM;

	if (!debugfs_create_files(ctx_dir, ctx, blk_mq_debugfs_ctx_attrs))
		return -ENOMEM;

	return 0;
}

int blk_mq_debugfs_register_hctx(struct request_queue *q,
				 struct blk_mq_hw_ctx *hctx)
{
	struct blk_mq_ctx *ctx;
	char name[20];
	int i;

	if (!q->debugfs_dir)
		return -ENOENT;

	snprintf(name, sizeof(name), "hctx%u", hctx->queue_num);
	hctx->debugfs_dir = debugfs_create_dir(name, q->debugfs_dir);
	if (!hctx->debugfs_dir)
		return -ENOMEM;

	if (!debugfs_create_files(hctx->debugfs_dir, hctx,
				  blk_mq_debugfs_hctx_attrs))
		goto err;

	hctx_for_each_ctx(hctx, ctx, i) {
		if (blk_mq_debugfs_register_ctx(hctx, ctx))
			goto err;
	}

	return 0;

err:
	blk_mq_debugfs_unregister_hctx(hctx);
	return -ENOMEM;
}

void blk_mq_debugfs_unregister_hctx(struct blk_mq_hw_ctx *hctx)
{
	debugfs_remove_recursive(hctx->debugfs_dir);
	hctx->sched_debugfs_dir = NULL;
	hctx->debugfs_dir = NULL;
}

int blk_mq_debugfs_register_hctxs(struct request_queue *q)
{
	struct blk_mq_hw_ctx *hctx;
	int i;

	queue_for_each_hw_ctx(q, hctx, i) {
		if (blk_mq_debugfs_register_hctx(q, hctx))
			return -ENOMEM;
	}

	return 0;
}

void blk_mq_debugfs_unregister_hctxs(struct request_queue *q)
{
	struct blk_mq_hw_ctx *hctx;
	int i;

	queue_for_each_hw_ctx(q, hctx, i)
		blk_mq_debugfs_unregister_hctx(hctx);
}

int blk_mq_debugfs_register_sched(struct request_queue *q)
{
	struct elevator_type *e = q->elevator->type;

	if (!q->debugfs_dir)
		return -ENOENT;

	if (!e->queue_debugfs_attrs)
		return 0;

	q->sched_debugfs_dir = debugfs_create_dir("sched", q->debugfs_dir);
	if (!q->sched_debugfs_dir)
		return -ENOMEM;

	if (!debugfs_create_files(q->sched_debugfs_dir, q,
				  e->queue_debugfs_attrs))
		goto err;

	return 0;

err:
	blk_mq_debugfs_unregister_sched(q);
	return -ENOMEM;
}

void blk_mq_debugfs_unregister_sched(struct request_queue *q)
{
	debugfs_remove_recursive(q->sched_debugfs_dir);
	q->sched_debugfs_dir = NULL;
}

int blk_mq_debugfs_register_sched_hctx(struct request_queue *q,
				       struct blk_mq_hw_ctx *hctx)
{
	struct elevator_type *e = q->elevator->type;

	if (!hctx->debugfs_dir)
		return -ENOENT;

	if (!e->hctx_debugfs_attrs)
		return 0;

	hctx->sched_debugfs_dir = debugfs_create_dir("sched",
						     hctx->debugfs_dir);
	if (!hctx->sched_debugfs_dir)
		return -ENOMEM;

	if (!debugfs_create_files(hctx->sched_debugfs_dir, hctx,
				  e->hctx_debugfs_attrs))
		return -ENOMEM;

	return 0;
}

void blk_mq_debugfs_unregister_sched_hctx(struct blk_mq_hw_ctx *hctx)
{
	debugfs_remove_recursive(hctx->sched_debugfs_dir);
	hctx->sched_debugfs_dir = NULL;
}
