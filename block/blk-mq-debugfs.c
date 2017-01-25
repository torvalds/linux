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
#include "blk-mq.h"
#include "blk-mq-tag.h"

struct blk_mq_debugfs_attr {
	const char *name;
	umode_t mode;
	const struct file_operations *fops;
};

static struct dentry *block_debugfs_root;

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

static int hctx_state_show(struct seq_file *m, void *v)
{
	struct blk_mq_hw_ctx *hctx = m->private;

	seq_printf(m, "0x%lx\n", hctx->state);
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

static int hctx_flags_show(struct seq_file *m, void *v)
{
	struct blk_mq_hw_ctx *hctx = m->private;

	seq_printf(m, "0x%lx\n", hctx->flags);
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

	seq_printf(m, "%p {.cmd_type=%u, .cmd_flags=0x%x, .rq_flags=0x%x, .tag=%d, .internal_tag=%d}\n",
		   rq, rq->cmd_type, rq->cmd_flags, (unsigned int)rq->rq_flags,
		   rq->tag, rq->internal_tag);
	return 0;
}

static void *hctx_dispatch_start(struct seq_file *m, loff_t *pos)
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

	mutex_lock(&q->sysfs_lock);
	if (hctx->tags)
		blk_mq_debugfs_tags_show(m, hctx->tags);
	mutex_unlock(&q->sysfs_lock);

	return 0;
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

	mutex_lock(&q->sysfs_lock);
	if (hctx->tags)
		sbitmap_bitmap_show(&hctx->tags->bitmap_tags.sb, m);
	mutex_unlock(&q->sysfs_lock);
	return 0;
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

	mutex_lock(&q->sysfs_lock);
	if (hctx->sched_tags)
		blk_mq_debugfs_tags_show(m, hctx->sched_tags);
	mutex_unlock(&q->sysfs_lock);

	return 0;
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

	mutex_lock(&q->sysfs_lock);
	if (hctx->sched_tags)
		sbitmap_bitmap_show(&hctx->sched_tags->bitmap_tags.sb, m);
	mutex_unlock(&q->sysfs_lock);
	return 0;
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

static void print_stat(struct seq_file *m, struct blk_rq_stat *stat)
{
	seq_printf(m, "samples=%d, mean=%lld, min=%llu, max=%llu",
		   stat->nr_samples, stat->mean, stat->min, stat->max);
}

static int hctx_stats_show(struct seq_file *m, void *v)
{
	struct blk_mq_hw_ctx *hctx = m->private;
	struct blk_rq_stat stat[2];

	blk_stat_init(&stat[BLK_STAT_READ]);
	blk_stat_init(&stat[BLK_STAT_WRITE]);

	blk_hctx_stat_get(hctx, stat);

	seq_puts(m, "read: ");
	print_stat(m, &stat[BLK_STAT_READ]);
	seq_puts(m, "\n");

	seq_puts(m, "write: ");
	print_stat(m, &stat[BLK_STAT_WRITE]);
	seq_puts(m, "\n");
	return 0;
}

static int hctx_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, hctx_stats_show, inode->i_private);
}

static ssize_t hctx_stats_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	struct seq_file *m = file->private_data;
	struct blk_mq_hw_ctx *hctx = m->private;
	struct blk_mq_ctx *ctx;
	int i;

	hctx_for_each_ctx(hctx, ctx, i) {
		blk_stat_init(&ctx->stat[BLK_STAT_READ]);
		blk_stat_init(&ctx->stat[BLK_STAT_WRITE]);
	}
	return count;
}

static const struct file_operations hctx_stats_fops = {
	.open		= hctx_stats_open,
	.read		= seq_read,
	.write		= hctx_stats_write,
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
	{"stats", 0600, &hctx_stats_fops},
	{"dispatched", 0600, &hctx_dispatched_fops},
	{"queued", 0600, &hctx_queued_fops},
	{"run", 0600, &hctx_run_fops},
	{"active", 0400, &hctx_active_fops},
};

static const struct blk_mq_debugfs_attr blk_mq_debugfs_ctx_attrs[] = {
	{"rq_list", 0400, &ctx_rq_list_fops},
	{"dispatched", 0600, &ctx_dispatched_fops},
	{"merged", 0600, &ctx_merged_fops},
	{"completed", 0600, &ctx_completed_fops},
};

int blk_mq_debugfs_register(struct request_queue *q, const char *name)
{
	if (!block_debugfs_root)
		return -ENOENT;

	q->debugfs_dir = debugfs_create_dir(name, block_debugfs_root);
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

static int blk_mq_debugfs_register_ctx(struct request_queue *q,
				       struct blk_mq_ctx *ctx,
				       struct dentry *hctx_dir)
{
	struct dentry *ctx_dir;
	char name[20];
	int i;

	snprintf(name, sizeof(name), "cpu%u", ctx->cpu);
	ctx_dir = debugfs_create_dir(name, hctx_dir);
	if (!ctx_dir)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(blk_mq_debugfs_ctx_attrs); i++) {
		const struct blk_mq_debugfs_attr *attr;

		attr = &blk_mq_debugfs_ctx_attrs[i];
		if (!debugfs_create_file(attr->name, attr->mode, ctx_dir, ctx,
					 attr->fops))
			return -ENOMEM;
	}

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

	for (i = 0; i < ARRAY_SIZE(blk_mq_debugfs_hctx_attrs); i++) {
		const struct blk_mq_debugfs_attr *attr;

		attr = &blk_mq_debugfs_hctx_attrs[i];
		if (!debugfs_create_file(attr->name, attr->mode, hctx_dir, hctx,
					 attr->fops))
			return -ENOMEM;
	}

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

	q->mq_debugfs_dir = debugfs_create_dir("mq", q->debugfs_dir);
	if (!q->mq_debugfs_dir)
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

void blk_mq_debugfs_init(void)
{
	block_debugfs_root = debugfs_create_dir("block", NULL);
}
