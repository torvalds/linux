// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/backing-dev.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/smp.h>

#include <linux/blk-mq.h>
#include "blk.h"
#include "blk-mq.h"
#include "blk-mq-tag.h"

static void blk_mq_sysfs_release(struct kobject *kobj)
{
	struct blk_mq_ctxs *ctxs = container_of(kobj, struct blk_mq_ctxs, kobj);

	free_percpu(ctxs->queue_ctx);
	kfree(ctxs);
}

static void blk_mq_ctx_sysfs_release(struct kobject *kobj)
{
	struct blk_mq_ctx *ctx = container_of(kobj, struct blk_mq_ctx, kobj);

	/* ctx->ctxs won't be released until all ctx are freed */
	kobject_put(&ctx->ctxs->kobj);
}

static void blk_mq_hw_sysfs_release(struct kobject *kobj)
{
	struct blk_mq_hw_ctx *hctx = container_of(kobj, struct blk_mq_hw_ctx,
						  kobj);

	blk_free_flush_queue(hctx->fq);
	sbitmap_free(&hctx->ctx_map);
	free_cpumask_var(hctx->cpumask);
	kfree(hctx->ctxs);
	kfree(hctx);
}

struct blk_mq_hw_ctx_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct blk_mq_hw_ctx *, char *);
	ssize_t (*store)(struct blk_mq_hw_ctx *, const char *, size_t);
};

static ssize_t blk_mq_hw_sysfs_show(struct kobject *kobj,
				    struct attribute *attr, char *page)
{
	struct blk_mq_hw_ctx_sysfs_entry *entry;
	struct blk_mq_hw_ctx *hctx;
	struct request_queue *q;
	ssize_t res;

	entry = container_of(attr, struct blk_mq_hw_ctx_sysfs_entry, attr);
	hctx = container_of(kobj, struct blk_mq_hw_ctx, kobj);
	q = hctx->queue;

	if (!entry->show)
		return -EIO;

	mutex_lock(&q->sysfs_lock);
	res = entry->show(hctx, page);
	mutex_unlock(&q->sysfs_lock);
	return res;
}

static ssize_t blk_mq_hw_sysfs_store(struct kobject *kobj,
				     struct attribute *attr, const char *page,
				     size_t length)
{
	struct blk_mq_hw_ctx_sysfs_entry *entry;
	struct blk_mq_hw_ctx *hctx;
	struct request_queue *q;
	ssize_t res;

	entry = container_of(attr, struct blk_mq_hw_ctx_sysfs_entry, attr);
	hctx = container_of(kobj, struct blk_mq_hw_ctx, kobj);
	q = hctx->queue;

	if (!entry->store)
		return -EIO;

	mutex_lock(&q->sysfs_lock);
	res = entry->store(hctx, page, length);
	mutex_unlock(&q->sysfs_lock);
	return res;
}

static ssize_t blk_mq_hw_sysfs_nr_tags_show(struct blk_mq_hw_ctx *hctx,
					    char *page)
{
	return sprintf(page, "%u\n", hctx->tags->nr_tags);
}

static ssize_t blk_mq_hw_sysfs_nr_reserved_tags_show(struct blk_mq_hw_ctx *hctx,
						     char *page)
{
	return sprintf(page, "%u\n", hctx->tags->nr_reserved_tags);
}

static ssize_t blk_mq_hw_sysfs_cpus_show(struct blk_mq_hw_ctx *hctx, char *page)
{
	const size_t size = PAGE_SIZE - 1;
	unsigned int i, first = 1;
	int ret = 0, pos = 0;

	for_each_cpu(i, hctx->cpumask) {
		if (first)
			ret = snprintf(pos + page, size - pos, "%u", i);
		else
			ret = snprintf(pos + page, size - pos, ", %u", i);

		if (ret >= size - pos)
			break;

		first = 0;
		pos += ret;
	}

	ret = snprintf(pos + page, size + 1 - pos, "\n");
	return pos + ret;
}

static struct blk_mq_hw_ctx_sysfs_entry blk_mq_hw_sysfs_nr_tags = {
	.attr = {.name = "nr_tags", .mode = 0444 },
	.show = blk_mq_hw_sysfs_nr_tags_show,
};
static struct blk_mq_hw_ctx_sysfs_entry blk_mq_hw_sysfs_nr_reserved_tags = {
	.attr = {.name = "nr_reserved_tags", .mode = 0444 },
	.show = blk_mq_hw_sysfs_nr_reserved_tags_show,
};
static struct blk_mq_hw_ctx_sysfs_entry blk_mq_hw_sysfs_cpus = {
	.attr = {.name = "cpu_list", .mode = 0444 },
	.show = blk_mq_hw_sysfs_cpus_show,
};

static struct attribute *default_hw_ctx_attrs[] = {
	&blk_mq_hw_sysfs_nr_tags.attr,
	&blk_mq_hw_sysfs_nr_reserved_tags.attr,
	&blk_mq_hw_sysfs_cpus.attr,
	NULL,
};
ATTRIBUTE_GROUPS(default_hw_ctx);

static const struct sysfs_ops blk_mq_hw_sysfs_ops = {
	.show	= blk_mq_hw_sysfs_show,
	.store	= blk_mq_hw_sysfs_store,
};

static struct kobj_type blk_mq_ktype = {
	.release	= blk_mq_sysfs_release,
};

static struct kobj_type blk_mq_ctx_ktype = {
	.release	= blk_mq_ctx_sysfs_release,
};

static struct kobj_type blk_mq_hw_ktype = {
	.sysfs_ops	= &blk_mq_hw_sysfs_ops,
	.default_groups = default_hw_ctx_groups,
	.release	= blk_mq_hw_sysfs_release,
};

static void blk_mq_unregister_hctx(struct blk_mq_hw_ctx *hctx)
{
	struct blk_mq_ctx *ctx;
	int i;

	if (!hctx->nr_ctx)
		return;

	hctx_for_each_ctx(hctx, ctx, i)
		kobject_del(&ctx->kobj);

	kobject_del(&hctx->kobj);
}

static int blk_mq_register_hctx(struct blk_mq_hw_ctx *hctx)
{
	struct request_queue *q = hctx->queue;
	struct blk_mq_ctx *ctx;
	int i, j, ret;

	if (!hctx->nr_ctx)
		return 0;

	ret = kobject_add(&hctx->kobj, q->mq_kobj, "%u", hctx->queue_num);
	if (ret)
		return ret;

	hctx_for_each_ctx(hctx, ctx, i) {
		ret = kobject_add(&ctx->kobj, &hctx->kobj, "cpu%u", ctx->cpu);
		if (ret)
			goto out;
	}

	return 0;
out:
	hctx_for_each_ctx(hctx, ctx, j) {
		if (j < i)
			kobject_del(&ctx->kobj);
	}
	kobject_del(&hctx->kobj);
	return ret;
}

void blk_mq_hctx_kobj_init(struct blk_mq_hw_ctx *hctx)
{
	kobject_init(&hctx->kobj, &blk_mq_hw_ktype);
}

void blk_mq_sysfs_deinit(struct request_queue *q)
{
	struct blk_mq_ctx *ctx;
	int cpu;

	for_each_possible_cpu(cpu) {
		ctx = per_cpu_ptr(q->queue_ctx, cpu);
		kobject_put(&ctx->kobj);
	}
	kobject_put(q->mq_kobj);
}

void blk_mq_sysfs_init(struct request_queue *q)
{
	struct blk_mq_ctx *ctx;
	int cpu;

	kobject_init(q->mq_kobj, &blk_mq_ktype);

	for_each_possible_cpu(cpu) {
		ctx = per_cpu_ptr(q->queue_ctx, cpu);

		kobject_get(q->mq_kobj);
		kobject_init(&ctx->kobj, &blk_mq_ctx_ktype);
	}
}

int blk_mq_sysfs_register(struct gendisk *disk)
{
	struct request_queue *q = disk->queue;
	struct blk_mq_hw_ctx *hctx;
	unsigned long i, j;
	int ret;

	lockdep_assert_held(&q->sysfs_dir_lock);

	ret = kobject_add(q->mq_kobj, &disk_to_dev(disk)->kobj, "mq");
	if (ret < 0)
		goto out;

	kobject_uevent(q->mq_kobj, KOBJ_ADD);

	queue_for_each_hw_ctx(q, hctx, i) {
		ret = blk_mq_register_hctx(hctx);
		if (ret)
			goto unreg;
	}

	q->mq_sysfs_init_done = true;

out:
	return ret;

unreg:
	queue_for_each_hw_ctx(q, hctx, j) {
		if (j < i)
			blk_mq_unregister_hctx(hctx);
	}

	kobject_uevent(q->mq_kobj, KOBJ_REMOVE);
	kobject_del(q->mq_kobj);
	return ret;
}

void blk_mq_sysfs_unregister(struct gendisk *disk)
{
	struct request_queue *q = disk->queue;
	struct blk_mq_hw_ctx *hctx;
	unsigned long i;

	lockdep_assert_held(&q->sysfs_dir_lock);

	queue_for_each_hw_ctx(q, hctx, i)
		blk_mq_unregister_hctx(hctx);

	kobject_uevent(q->mq_kobj, KOBJ_REMOVE);
	kobject_del(q->mq_kobj);

	q->mq_sysfs_init_done = false;
}

void blk_mq_sysfs_unregister_hctxs(struct request_queue *q)
{
	struct blk_mq_hw_ctx *hctx;
	unsigned long i;

	mutex_lock(&q->sysfs_dir_lock);
	if (!q->mq_sysfs_init_done)
		goto unlock;

	queue_for_each_hw_ctx(q, hctx, i)
		blk_mq_unregister_hctx(hctx);

unlock:
	mutex_unlock(&q->sysfs_dir_lock);
}

int blk_mq_sysfs_register_hctxs(struct request_queue *q)
{
	struct blk_mq_hw_ctx *hctx;
	unsigned long i;
	int ret = 0;

	mutex_lock(&q->sysfs_dir_lock);
	if (!q->mq_sysfs_init_done)
		goto unlock;

	queue_for_each_hw_ctx(q, hctx, i) {
		ret = blk_mq_register_hctx(hctx);
		if (ret)
			break;
	}

unlock:
	mutex_unlock(&q->sysfs_dir_lock);

	return ret;
}
