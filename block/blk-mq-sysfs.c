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
#include "blk-mq.h"
#include "blk-mq-tag.h"

static void blk_mq_sysfs_release(struct kobject *kobj)
{
}

struct blk_mq_ctx_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct blk_mq_ctx *, char *);
	ssize_t (*store)(struct blk_mq_ctx *, const char *, size_t);
};

struct blk_mq_hw_ctx_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct blk_mq_hw_ctx *, char *);
	ssize_t (*store)(struct blk_mq_hw_ctx *, const char *, size_t);
};

static ssize_t blk_mq_sysfs_show(struct kobject *kobj, struct attribute *attr,
				 char *page)
{
	struct blk_mq_ctx_sysfs_entry *entry;
	struct blk_mq_ctx *ctx;
	struct request_queue *q;
	ssize_t res;

	entry = container_of(attr, struct blk_mq_ctx_sysfs_entry, attr);
	ctx = container_of(kobj, struct blk_mq_ctx, kobj);
	q = ctx->queue;

	if (!entry->show)
		return -EIO;

	res = -ENOENT;
	mutex_lock(&q->sysfs_lock);
	if (!blk_queue_dying(q))
		res = entry->show(ctx, page);
	mutex_unlock(&q->sysfs_lock);
	return res;
}

static ssize_t blk_mq_sysfs_store(struct kobject *kobj, struct attribute *attr,
				  const char *page, size_t length)
{
	struct blk_mq_ctx_sysfs_entry *entry;
	struct blk_mq_ctx *ctx;
	struct request_queue *q;
	ssize_t res;

	entry = container_of(attr, struct blk_mq_ctx_sysfs_entry, attr);
	ctx = container_of(kobj, struct blk_mq_ctx, kobj);
	q = ctx->queue;

	if (!entry->store)
		return -EIO;

	res = -ENOENT;
	mutex_lock(&q->sysfs_lock);
	if (!blk_queue_dying(q))
		res = entry->store(ctx, page, length);
	mutex_unlock(&q->sysfs_lock);
	return res;
}

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

	res = -ENOENT;
	mutex_lock(&q->sysfs_lock);
	if (!blk_queue_dying(q))
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

	res = -ENOENT;
	mutex_lock(&q->sysfs_lock);
	if (!blk_queue_dying(q))
		res = entry->store(hctx, page, length);
	mutex_unlock(&q->sysfs_lock);
	return res;
}

static ssize_t blk_mq_sysfs_dispatched_show(struct blk_mq_ctx *ctx, char *page)
{
	return sprintf(page, "%lu %lu\n", ctx->rq_dispatched[1],
				ctx->rq_dispatched[0]);
}

static ssize_t blk_mq_sysfs_merged_show(struct blk_mq_ctx *ctx, char *page)
{
	return sprintf(page, "%lu\n", ctx->rq_merged);
}

static ssize_t blk_mq_sysfs_completed_show(struct blk_mq_ctx *ctx, char *page)
{
	return sprintf(page, "%lu %lu\n", ctx->rq_completed[1],
				ctx->rq_completed[0]);
}

static ssize_t sysfs_list_show(char *page, struct list_head *list, char *msg)
{
	char *start_page = page;
	struct request *rq;

	page += sprintf(page, "%s:\n", msg);

	list_for_each_entry(rq, list, queuelist)
		page += sprintf(page, "\t%p\n", rq);

	return page - start_page;
}

static ssize_t blk_mq_sysfs_rq_list_show(struct blk_mq_ctx *ctx, char *page)
{
	ssize_t ret;

	spin_lock(&ctx->lock);
	ret = sysfs_list_show(page, &ctx->rq_list, "CTX pending");
	spin_unlock(&ctx->lock);

	return ret;
}

static ssize_t blk_mq_hw_sysfs_queued_show(struct blk_mq_hw_ctx *hctx,
					   char *page)
{
	return sprintf(page, "%lu\n", hctx->queued);
}

static ssize_t blk_mq_hw_sysfs_run_show(struct blk_mq_hw_ctx *hctx, char *page)
{
	return sprintf(page, "%lu\n", hctx->run);
}

static ssize_t blk_mq_hw_sysfs_dispatched_show(struct blk_mq_hw_ctx *hctx,
					       char *page)
{
	char *start_page = page;
	int i;

	page += sprintf(page, "%8u\t%lu\n", 0U, hctx->dispatched[0]);

	for (i = 1; i < BLK_MQ_MAX_DISPATCH_ORDER; i++) {
		unsigned long d = 1U << (i - 1);

		page += sprintf(page, "%8lu\t%lu\n", d, hctx->dispatched[i]);
	}

	return page - start_page;
}

static ssize_t blk_mq_hw_sysfs_rq_list_show(struct blk_mq_hw_ctx *hctx,
					    char *page)
{
	ssize_t ret;

	spin_lock(&hctx->lock);
	ret = sysfs_list_show(page, &hctx->dispatch, "HCTX pending");
	spin_unlock(&hctx->lock);

	return ret;
}

static ssize_t blk_mq_hw_sysfs_ipi_show(struct blk_mq_hw_ctx *hctx, char *page)
{
	ssize_t ret;

	spin_lock(&hctx->lock);
	ret = sprintf(page, "%u\n", !!(hctx->flags & BLK_MQ_F_SHOULD_IPI));
	spin_unlock(&hctx->lock);

	return ret;
}

static ssize_t blk_mq_hw_sysfs_ipi_store(struct blk_mq_hw_ctx *hctx,
					 const char *page, size_t len)
{
	struct blk_mq_ctx *ctx;
	unsigned long ret;
	unsigned int i;

	if (kstrtoul(page, 10, &ret)) {
		pr_err("blk-mq-sysfs: invalid input '%s'\n", page);
		return -EINVAL;
	}

	spin_lock(&hctx->lock);
	if (ret)
		hctx->flags |= BLK_MQ_F_SHOULD_IPI;
	else
		hctx->flags &= ~BLK_MQ_F_SHOULD_IPI;
	spin_unlock(&hctx->lock);

	hctx_for_each_ctx(hctx, ctx, i)
		ctx->ipi_redirect = !!ret;

	return len;
}

static ssize_t blk_mq_hw_sysfs_tags_show(struct blk_mq_hw_ctx *hctx, char *page)
{
	return blk_mq_tag_sysfs_show(hctx->tags, page);
}

static struct blk_mq_ctx_sysfs_entry blk_mq_sysfs_dispatched = {
	.attr = {.name = "dispatched", .mode = S_IRUGO },
	.show = blk_mq_sysfs_dispatched_show,
};
static struct blk_mq_ctx_sysfs_entry blk_mq_sysfs_merged = {
	.attr = {.name = "merged", .mode = S_IRUGO },
	.show = blk_mq_sysfs_merged_show,
};
static struct blk_mq_ctx_sysfs_entry blk_mq_sysfs_completed = {
	.attr = {.name = "completed", .mode = S_IRUGO },
	.show = blk_mq_sysfs_completed_show,
};
static struct blk_mq_ctx_sysfs_entry blk_mq_sysfs_rq_list = {
	.attr = {.name = "rq_list", .mode = S_IRUGO },
	.show = blk_mq_sysfs_rq_list_show,
};

static struct attribute *default_ctx_attrs[] = {
	&blk_mq_sysfs_dispatched.attr,
	&blk_mq_sysfs_merged.attr,
	&blk_mq_sysfs_completed.attr,
	&blk_mq_sysfs_rq_list.attr,
	NULL,
};

static struct blk_mq_hw_ctx_sysfs_entry blk_mq_hw_sysfs_queued = {
	.attr = {.name = "queued", .mode = S_IRUGO },
	.show = blk_mq_hw_sysfs_queued_show,
};
static struct blk_mq_hw_ctx_sysfs_entry blk_mq_hw_sysfs_run = {
	.attr = {.name = "run", .mode = S_IRUGO },
	.show = blk_mq_hw_sysfs_run_show,
};
static struct blk_mq_hw_ctx_sysfs_entry blk_mq_hw_sysfs_dispatched = {
	.attr = {.name = "dispatched", .mode = S_IRUGO },
	.show = blk_mq_hw_sysfs_dispatched_show,
};
static struct blk_mq_hw_ctx_sysfs_entry blk_mq_hw_sysfs_pending = {
	.attr = {.name = "pending", .mode = S_IRUGO },
	.show = blk_mq_hw_sysfs_rq_list_show,
};
static struct blk_mq_hw_ctx_sysfs_entry blk_mq_hw_sysfs_ipi = {
	.attr = {.name = "ipi_redirect", .mode = S_IRUGO | S_IWUSR},
	.show = blk_mq_hw_sysfs_ipi_show,
	.store = blk_mq_hw_sysfs_ipi_store,
};
static struct blk_mq_hw_ctx_sysfs_entry blk_mq_hw_sysfs_tags = {
	.attr = {.name = "tags", .mode = S_IRUGO },
	.show = blk_mq_hw_sysfs_tags_show,
};

static struct attribute *default_hw_ctx_attrs[] = {
	&blk_mq_hw_sysfs_queued.attr,
	&blk_mq_hw_sysfs_run.attr,
	&blk_mq_hw_sysfs_dispatched.attr,
	&blk_mq_hw_sysfs_pending.attr,
	&blk_mq_hw_sysfs_ipi.attr,
	&blk_mq_hw_sysfs_tags.attr,
	NULL,
};

static const struct sysfs_ops blk_mq_sysfs_ops = {
	.show	= blk_mq_sysfs_show,
	.store	= blk_mq_sysfs_store,
};

static const struct sysfs_ops blk_mq_hw_sysfs_ops = {
	.show	= blk_mq_hw_sysfs_show,
	.store	= blk_mq_hw_sysfs_store,
};

static struct kobj_type blk_mq_ktype = {
	.sysfs_ops	= &blk_mq_sysfs_ops,
	.release	= blk_mq_sysfs_release,
};

static struct kobj_type blk_mq_ctx_ktype = {
	.sysfs_ops	= &blk_mq_sysfs_ops,
	.default_attrs	= default_ctx_attrs,
	.release	= blk_mq_sysfs_release,
};

static struct kobj_type blk_mq_hw_ktype = {
	.sysfs_ops	= &blk_mq_hw_sysfs_ops,
	.default_attrs	= default_hw_ctx_attrs,
	.release	= blk_mq_sysfs_release,
};

void blk_mq_unregister_disk(struct gendisk *disk)
{
	struct request_queue *q = disk->queue;
	struct blk_mq_hw_ctx *hctx;
	struct blk_mq_ctx *ctx;
	int i, j;

	queue_for_each_hw_ctx(q, hctx, i) {
		hctx_for_each_ctx(hctx, ctx, j) {
			kobject_del(&ctx->kobj);
			kobject_put(&ctx->kobj);
		}
		kobject_del(&hctx->kobj);
		kobject_put(&hctx->kobj);
	}

	kobject_uevent(&q->mq_kobj, KOBJ_REMOVE);
	kobject_del(&q->mq_kobj);
	kobject_put(&q->mq_kobj);

	kobject_put(&disk_to_dev(disk)->kobj);
}

int blk_mq_register_disk(struct gendisk *disk)
{
	struct device *dev = disk_to_dev(disk);
	struct request_queue *q = disk->queue;
	struct blk_mq_hw_ctx *hctx;
	struct blk_mq_ctx *ctx;
	int ret, i, j;

	kobject_init(&q->mq_kobj, &blk_mq_ktype);

	ret = kobject_add(&q->mq_kobj, kobject_get(&dev->kobj), "%s", "mq");
	if (ret < 0)
		return ret;

	kobject_uevent(&q->mq_kobj, KOBJ_ADD);

	queue_for_each_hw_ctx(q, hctx, i) {
		kobject_init(&hctx->kobj, &blk_mq_hw_ktype);
		ret = kobject_add(&hctx->kobj, &q->mq_kobj, "%u", i);
		if (ret)
			break;

		if (!hctx->nr_ctx)
			continue;

		hctx_for_each_ctx(hctx, ctx, j) {
			kobject_init(&ctx->kobj, &blk_mq_ctx_ktype);
			ret = kobject_add(&ctx->kobj, &hctx->kobj, "cpu%u", ctx->cpu);
			if (ret)
				break;
		}
	}

	if (ret) {
		blk_mq_unregister_disk(disk);
		return ret;
	}

	return 0;
}
