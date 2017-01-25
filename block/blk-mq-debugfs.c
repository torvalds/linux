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

struct blk_mq_debugfs_attr {
	const char *name;
	umode_t mode;
	const struct file_operations *fops;
};

static struct dentry *block_debugfs_root;

static const struct blk_mq_debugfs_attr blk_mq_debugfs_hctx_attrs[] = {
};

static const struct blk_mq_debugfs_attr blk_mq_debugfs_ctx_attrs[] = {
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
