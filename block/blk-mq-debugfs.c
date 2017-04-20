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
#include "blk-mq-tag.h"

struct blk_mq_debugfs_attr {
	const char *name;
	umode_t mode;
	const struct file_operations *fops;
};

static int blk_mq_debugfs_seq_open(struct inode *inode, struct file *file,
				   const struct seq_operations *ops)
{
	struct seq_file *m;
	int ret;

	ret = seq_open(file, ops);
	if (!ret) {
		m = file->private_data;
		m->private = inode->i_private;
	}
	return ret;
}

static int blk_flags_show(struct seq_file *m, const unsigned long flags,
			  const char *const *flag_name, int flag_name_count)
{
	bool sep = false;
	int i;

	for (i = 0; i < sizeof(flags) * BITS_PER_BYTE; i++) {
		if (!(flags & BIT(i)))
			continue;
		if (sep)
			seq_puts(m, " ");
		sep = true;
		if (i < flag_name_count && flag_name[i])
			seq_puts(m, flag_name[i]);
		else
			seq_printf(m, "%d", i);
	}
	seq_puts(m, "\n");
	return 0;
}

static const char *const blk_queue_flag_name[] = {
	[QUEUE_FLAG_QUEUED]	 = "QUEUED",
	[QUEUE_FLAG_STOPPED]	 = "STOPPED",
	[QUEUE_FLAG_SYNCFULL]	 = "SYNCFULL",
	[QUEUE_FLAG_ASYNCFULL]	 = "ASYNCFULL",
	[QUEUE_FLAG_DYING]	 = "DYING",
	[QUEUE_FLAG_BYPASS]	 = "BYPASS",
	[QUEUE_FLAG_BIDI]	 = "BIDI",
	[QUEUE_FLAG_NOMERGES]	 = "NOMERGES",
	[QUEUE_FLAG_SAME_COMP]	 = "SAME_COMP",
	[QUEUE_FLAG_FAIL_IO]	 = "FAIL_IO",
	[QUEUE_FLAG_STACKABLE]	 = "STACKABLE",
	[QUEUE_FLAG_NONROT]	 = "NONROT",
	[QUEUE_FLAG_IO_STAT]	 = "IO_STAT",
	[QUEUE_FLAG_DISCARD]	 = "DISCARD",
	[QUEUE_FLAG_NOXMERGES]	 = "NOXMERGES",
	[QUEUE_FLAG_ADD_RANDOM]	 = "ADD_RANDOM",
	[QUEUE_FLAG_SECERASE]	 = "SECERASE",
	[QUEUE_FLAG_SAME_FORCE]	 = "SAME_FORCE",
	[QUEUE_FLAG_DEAD]	 = "DEAD",
	[QUEUE_FLAG_INIT_DONE]	 = "INIT_DONE",
	[QUEUE_FLAG_NO_SG_MERGE] = "NO_SG_MERGE",
	[QUEUE_FLAG_POLL]	 = "POLL",
	[QUEUE_FLAG_WC]		 = "WC",
	[QUEUE_FLAG_FUA]	 = "FUA",
	[QUEUE_FLAG_FLUSH_NQ]	 = "FLUSH_NQ",
	[QUEUE_FLAG_DAX]	 = "DAX",
	[QUEUE_FLAG_STATS]	 = "STATS",
	[QUEUE_FLAG_POLL_STATS]	 = "POLL_STATS",
	[QUEUE_FLAG_REGISTERED]	 = "REGISTERED",
};

static int blk_queue_flags_show(struct seq_file *m, void *v)
{
	struct request_queue *q = m->private;

	blk_flags_show(m, q->queue_flags, blk_queue_flag_name,
		       ARRAY_SIZE(blk_queue_flag_name));
	return 0;
}

static ssize_t blk_queue_flags_store(struct file *file, const char __user *ubuf,
				     size_t len, loff_t *offp)
{
	struct request_queue *q = file_inode(file)->i_private;
	char op[16] = { }, *s;

	len = min(len, sizeof(op) - 1);
	if (copy_from_user(op, ubuf, len))
		return -EFAULT;
	s = op;
	strsep(&s, " \t\n"); /* strip trailing whitespace */
	if (strcmp(op, "run") == 0) {
		blk_mq_run_hw_queues(q, true);
	} else if (strcmp(op, "start") == 0) {
		blk_mq_start_stopped_hw_queues(q, true);
	} else {
		pr_err("%s: unsupported operation %s. Use either 'run' or 'start'\n",
		       __func__, op);
		return -EINVAL;
	}
	return len;
}

static int blk_queue_flags_open(struct inode *inode, struct file *file)
{
	return single_open(file, blk_queue_flags_show, inode->i_private);
}

static const struct file_operations blk_queue_flags_fops = {
	.open		= blk_queue_flags_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= blk_queue_flags_store,
};

static const struct blk_mq_debugfs_attr blk_queue_attrs[] = {
	{"state", 0600, &blk_queue_flags_fops},
	{},
};

static void print_stat(struct seq_file *m, struct blk_rq_stat *stat)
{
	if (stat->nr_samples) {
		seq_printf(m, "samples=%d, mean=%lld, min=%llu, max=%llu",
			   stat->nr_samples, stat->mean, stat->min, stat->max);
	} else {
		seq_puts(m, "samples=0");
	}
}

static int queue_poll_stat_show(struct seq_file *m, void *v)
{
	struct request_queue *q = m->private;
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

static int queue_poll_stat_open(struct inode *inode, struct file *file)
{
	return single_open(file, queue_poll_stat_show, inode->i_private);
}

static const struct file_operations queue_poll_stat_fops = {
	.open		= queue_poll_stat_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const char *const hctx_state_name[] = {
	[BLK_MQ_S_STOPPED]	 = "STOPPED",
	[BLK_MQ_S_TAG_ACTIVE]	 = "TAG_ACTIVE",
	[BLK_MQ_S_SCHED_RESTART] = "SCHED_RESTART",
	[BLK_MQ_S_TAG_WAITING]	 = "TAG_WAITING",

};
static int hctx_state_show(struct seq_file *m, void *v)
{
	struct blk_mq_hw_ctx *hctx = m->private;

	blk_flags_show(m, hctx->state, hctx_state_name,
		       ARRAY_SIZE(hctx_state_name));
	return 0;
}

static int hctx_state_open(struct inode *inode, struct file *file)
{
	return single_open(file, hctx_state_show, inode->i_private);
}

static const struct file_operations hctx_state_fops = {
	.open		= hctx_state_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const char *const alloc_policy_name[] = {
	[BLK_TAG_ALLOC_FIFO]	= "fifo",
	[BLK_TAG_ALLOC_RR]	= "rr",
};

static const char *const hctx_flag_name[] = {
	[ilog2(BLK_MQ_F_SHOULD_MERGE)]	= "SHOULD_MERGE",
	[ilog2(BLK_MQ_F_TAG_SHARED)]	= "TAG_SHARED",
	[ilog2(BLK_MQ_F_SG_MERGE)]	= "SG_MERGE",
	[ilog2(BLK_MQ_F_BLOCKING)]	= "BLOCKING",
	[ilog2(BLK_MQ_F_NO_SCHED)]	= "NO_SCHED",
};

static int hctx_flags_show(struct seq_file *m, void *v)
{
	struct blk_mq_hw_ctx *hctx = m->private;
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
	return 0;
}

static int hctx_flags_open(struct inode *inode, struct file *file)
{
	return single_open(file, hctx_flags_show, inode->i_private);
}

static const struct file_operations hctx_flags_fops = {
	.open		= hctx_flags_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int blk_mq_debugfs_rq_show(struct seq_file *m, void *v)
{
	struct request *rq = list_entry_rq(v);

	seq_printf(m, "%p {.cmd_flags=0x%x, .rq_flags=0x%x, .tag=%d, .internal_tag=%d}\n",
		   rq, rq->cmd_flags, (__force unsigned int)rq->rq_flags,
		   rq->tag, rq->internal_tag);
	return 0;
}

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

static int hctx_dispatch_open(struct inode *inode, struct file *file)
{
	return blk_mq_debugfs_seq_open(inode, file, &hctx_dispatch_seq_ops);
}

static const struct file_operations hctx_dispatch_fops = {
	.open		= hctx_dispatch_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int hctx_ctx_map_show(struct seq_file *m, void *v)
{
	struct blk_mq_hw_ctx *hctx = m->private;

	sbitmap_bitmap_show(&hctx->ctx_map, m);
	return 0;
}

static int hctx_ctx_map_open(struct inode *inode, struct file *file)
{
	return single_open(file, hctx_ctx_map_show, inode->i_private);
}

static const struct file_operations hctx_ctx_map_fops = {
	.open		= hctx_ctx_map_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

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

static int hctx_tags_show(struct seq_file *m, void *v)
{
	struct blk_mq_hw_ctx *hctx = m->private;
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

static int hctx_tags_open(struct inode *inode, struct file *file)
{
	return single_open(file, hctx_tags_show, inode->i_private);
}

static const struct file_operations hctx_tags_fops = {
	.open		= hctx_tags_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int hctx_tags_bitmap_show(struct seq_file *m, void *v)
{
	struct blk_mq_hw_ctx *hctx = m->private;
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

static int hctx_tags_bitmap_open(struct inode *inode, struct file *file)
{
	return single_open(file, hctx_tags_bitmap_show, inode->i_private);
}

static const struct file_operations hctx_tags_bitmap_fops = {
	.open		= hctx_tags_bitmap_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int hctx_sched_tags_show(struct seq_file *m, void *v)
{
	struct blk_mq_hw_ctx *hctx = m->private;
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

static int hctx_sched_tags_open(struct inode *inode, struct file *file)
{
	return single_open(file, hctx_sched_tags_show, inode->i_private);
}

static const struct file_operations hctx_sched_tags_fops = {
	.open		= hctx_sched_tags_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int hctx_sched_tags_bitmap_show(struct seq_file *m, void *v)
{
	struct blk_mq_hw_ctx *hctx = m->private;
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

static int hctx_sched_tags_bitmap_open(struct inode *inode, struct file *file)
{
	return single_open(file, hctx_sched_tags_bitmap_show, inode->i_private);
}

static const struct file_operations hctx_sched_tags_bitmap_fops = {
	.open		= hctx_sched_tags_bitmap_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int hctx_io_poll_show(struct seq_file *m, void *v)
{
	struct blk_mq_hw_ctx *hctx = m->private;

	seq_printf(m, "considered=%lu\n", hctx->poll_considered);
	seq_printf(m, "invoked=%lu\n", hctx->poll_invoked);
	seq_printf(m, "success=%lu\n", hctx->poll_success);
	return 0;
}

static int hctx_io_poll_open(struct inode *inode, struct file *file)
{
	return single_open(file, hctx_io_poll_show, inode->i_private);
}

static ssize_t hctx_io_poll_write(struct file *file, const char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct seq_file *m = file->private_data;
	struct blk_mq_hw_ctx *hctx = m->private;

	hctx->poll_considered = hctx->poll_invoked = hctx->poll_success = 0;
	return count;
}

static const struct file_operations hctx_io_poll_fops = {
	.open		= hctx_io_poll_open,
	.read		= seq_read,
	.write		= hctx_io_poll_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int hctx_dispatched_show(struct seq_file *m, void *v)
{
	struct blk_mq_hw_ctx *hctx = m->private;
	int i;

	seq_printf(m, "%8u\t%lu\n", 0U, hctx->dispatched[0]);

	for (i = 1; i < BLK_MQ_MAX_DISPATCH_ORDER - 1; i++) {
		unsigned int d = 1U << (i - 1);

		seq_printf(m, "%8u\t%lu\n", d, hctx->dispatched[i]);
	}

	seq_printf(m, "%8u+\t%lu\n", 1U << (i - 1), hctx->dispatched[i]);
	return 0;
}

static int hctx_dispatched_open(struct inode *inode, struct file *file)
{
	return single_open(file, hctx_dispatched_show, inode->i_private);
}

static ssize_t hctx_dispatched_write(struct file *file, const char __user *buf,
				     size_t count, loff_t *ppos)
{
	struct seq_file *m = file->private_data;
	struct blk_mq_hw_ctx *hctx = m->private;
	int i;

	for (i = 0; i < BLK_MQ_MAX_DISPATCH_ORDER; i++)
		hctx->dispatched[i] = 0;
	return count;
}

static const struct file_operations hctx_dispatched_fops = {
	.open		= hctx_dispatched_open,
	.read		= seq_read,
	.write		= hctx_dispatched_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int hctx_queued_show(struct seq_file *m, void *v)
{
	struct blk_mq_hw_ctx *hctx = m->private;

	seq_printf(m, "%lu\n", hctx->queued);
	return 0;
}

static int hctx_queued_open(struct inode *inode, struct file *file)
{
	return single_open(file, hctx_queued_show, inode->i_private);
}

static ssize_t hctx_queued_write(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct seq_file *m = file->private_data;
	struct blk_mq_hw_ctx *hctx = m->private;

	hctx->queued = 0;
	return count;
}

static const struct file_operations hctx_queued_fops = {
	.open		= hctx_queued_open,
	.read		= seq_read,
	.write		= hctx_queued_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int hctx_run_show(struct seq_file *m, void *v)
{
	struct blk_mq_hw_ctx *hctx = m->private;

	seq_printf(m, "%lu\n", hctx->run);
	return 0;
}

static int hctx_run_open(struct inode *inode, struct file *file)
{
	return single_open(file, hctx_run_show, inode->i_private);
}

static ssize_t hctx_run_write(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct seq_file *m = file->private_data;
	struct blk_mq_hw_ctx *hctx = m->private;

	hctx->run = 0;
	return count;
}

static const struct file_operations hctx_run_fops = {
	.open		= hctx_run_open,
	.read		= seq_read,
	.write		= hctx_run_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int hctx_active_show(struct seq_file *m, void *v)
{
	struct blk_mq_hw_ctx *hctx = m->private;

	seq_printf(m, "%d\n", atomic_read(&hctx->nr_active));
	return 0;
}

static int hctx_active_open(struct inode *inode, struct file *file)
{
	return single_open(file, hctx_active_show, inode->i_private);
}

static const struct file_operations hctx_active_fops = {
	.open		= hctx_active_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

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

static int ctx_rq_list_open(struct inode *inode, struct file *file)
{
	return blk_mq_debugfs_seq_open(inode, file, &ctx_rq_list_seq_ops);
}

static const struct file_operations ctx_rq_list_fops = {
	.open		= ctx_rq_list_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int ctx_dispatched_show(struct seq_file *m, void *v)
{
	struct blk_mq_ctx *ctx = m->private;

	seq_printf(m, "%lu %lu\n", ctx->rq_dispatched[1], ctx->rq_dispatched[0]);
	return 0;
}

static int ctx_dispatched_open(struct inode *inode, struct file *file)
{
	return single_open(file, ctx_dispatched_show, inode->i_private);
}

static ssize_t ctx_dispatched_write(struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	struct seq_file *m = file->private_data;
	struct blk_mq_ctx *ctx = m->private;

	ctx->rq_dispatched[0] = ctx->rq_dispatched[1] = 0;
	return count;
}

static const struct file_operations ctx_dispatched_fops = {
	.open		= ctx_dispatched_open,
	.read		= seq_read,
	.write		= ctx_dispatched_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int ctx_merged_show(struct seq_file *m, void *v)
{
	struct blk_mq_ctx *ctx = m->private;

	seq_printf(m, "%lu\n", ctx->rq_merged);
	return 0;
}

static int ctx_merged_open(struct inode *inode, struct file *file)
{
	return single_open(file, ctx_merged_show, inode->i_private);
}

static ssize_t ctx_merged_write(struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	struct seq_file *m = file->private_data;
	struct blk_mq_ctx *ctx = m->private;

	ctx->rq_merged = 0;
	return count;
}

static const struct file_operations ctx_merged_fops = {
	.open		= ctx_merged_open,
	.read		= seq_read,
	.write		= ctx_merged_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int ctx_completed_show(struct seq_file *m, void *v)
{
	struct blk_mq_ctx *ctx = m->private;

	seq_printf(m, "%lu %lu\n", ctx->rq_completed[1], ctx->rq_completed[0]);
	return 0;
}

static int ctx_completed_open(struct inode *inode, struct file *file)
{
	return single_open(file, ctx_completed_show, inode->i_private);
}

static ssize_t ctx_completed_write(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	struct seq_file *m = file->private_data;
	struct blk_mq_ctx *ctx = m->private;

	ctx->rq_completed[0] = ctx->rq_completed[1] = 0;
	return count;
}

static const struct file_operations ctx_completed_fops = {
	.open		= ctx_completed_open,
	.read		= seq_read,
	.write		= ctx_completed_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct blk_mq_debugfs_attr blk_mq_debugfs_queue_attrs[] = {
	{"poll_stat", 0400, &queue_poll_stat_fops},
	{},
};

static const struct blk_mq_debugfs_attr blk_mq_debugfs_hctx_attrs[] = {
	{"state", 0400, &hctx_state_fops},
	{"flags", 0400, &hctx_flags_fops},
	{"dispatch", 0400, &hctx_dispatch_fops},
	{"ctx_map", 0400, &hctx_ctx_map_fops},
	{"tags", 0400, &hctx_tags_fops},
	{"tags_bitmap", 0400, &hctx_tags_bitmap_fops},
	{"sched_tags", 0400, &hctx_sched_tags_fops},
	{"sched_tags_bitmap", 0400, &hctx_sched_tags_bitmap_fops},
	{"io_poll", 0600, &hctx_io_poll_fops},
	{"dispatched", 0600, &hctx_dispatched_fops},
	{"queued", 0600, &hctx_queued_fops},
	{"run", 0600, &hctx_run_fops},
	{"active", 0400, &hctx_active_fops},
	{},
};

static const struct blk_mq_debugfs_attr blk_mq_debugfs_ctx_attrs[] = {
	{"rq_list", 0400, &ctx_rq_list_fops},
	{"dispatched", 0600, &ctx_dispatched_fops},
	{"merged", 0600, &ctx_merged_fops},
	{"completed", 0600, &ctx_completed_fops},
	{},
};

int blk_mq_debugfs_register(struct request_queue *q, const char *name)
{
	if (!blk_debugfs_root)
		return -ENOENT;

	q->debugfs_dir = debugfs_create_dir(name, blk_debugfs_root);
	if (!q->debugfs_dir)
		goto err;

	if (blk_mq_debugfs_register_hctxs(q))
		goto err;

	return 0;

err:
	blk_mq_debugfs_unregister(q);
	return -ENOMEM;
}

void blk_mq_debugfs_unregister(struct request_queue *q)
{
	debugfs_remove_recursive(q->debugfs_dir);
	q->mq_debugfs_dir = NULL;
	q->debugfs_dir = NULL;
}

static bool debugfs_create_files(struct dentry *parent, void *data,
				const struct blk_mq_debugfs_attr *attr)
{
	for (; attr->name; attr++) {
		if (!debugfs_create_file(attr->name, attr->mode, parent,
					 data, attr->fops))
			return false;
	}
	return true;
}

static int blk_mq_debugfs_register_ctx(struct request_queue *q,
				       struct blk_mq_ctx *ctx,
				       struct dentry *hctx_dir)
{
	struct dentry *ctx_dir;
	char name[20];

	snprintf(name, sizeof(name), "cpu%u", ctx->cpu);
	ctx_dir = debugfs_create_dir(name, hctx_dir);
	if (!ctx_dir)
		return -ENOMEM;

	if (!debugfs_create_files(ctx_dir, ctx, blk_mq_debugfs_ctx_attrs))
		return -ENOMEM;

	return 0;
}

static int blk_mq_debugfs_register_hctx(struct request_queue *q,
					struct blk_mq_hw_ctx *hctx)
{
	struct blk_mq_ctx *ctx;
	struct dentry *hctx_dir;
	char name[20];
	int i;

	snprintf(name, sizeof(name), "%u", hctx->queue_num);
	hctx_dir = debugfs_create_dir(name, q->mq_debugfs_dir);
	if (!hctx_dir)
		return -ENOMEM;

	if (!debugfs_create_files(hctx_dir, hctx, blk_mq_debugfs_hctx_attrs))
		return -ENOMEM;

	hctx_for_each_ctx(hctx, ctx, i) {
		if (blk_mq_debugfs_register_ctx(q, ctx, hctx_dir))
			return -ENOMEM;
	}

	return 0;
}

int blk_mq_debugfs_register_hctxs(struct request_queue *q)
{
	struct blk_mq_hw_ctx *hctx;
	int i;

	if (!q->debugfs_dir)
		return -ENOENT;

	if (!debugfs_create_files(q->debugfs_dir, q, blk_queue_attrs))
		goto err;

	q->mq_debugfs_dir = debugfs_create_dir("mq", q->debugfs_dir);
	if (!q->mq_debugfs_dir)
		goto err;

	if (!debugfs_create_files(q->mq_debugfs_dir, q, blk_mq_debugfs_queue_attrs))
		goto err;

	queue_for_each_hw_ctx(q, hctx, i) {
		if (blk_mq_debugfs_register_hctx(q, hctx))
			goto err;
	}

	return 0;

err:
	blk_mq_debugfs_unregister_hctxs(q);
	return -ENOMEM;
}

void blk_mq_debugfs_unregister_hctxs(struct request_queue *q)
{
	debugfs_remove_recursive(q->mq_debugfs_dir);
	q->mq_debugfs_dir = NULL;
}
