// SPDX-License-Identifier: GPL-2.0
/*
 *  Block device elevator/IO-scheduler.
 *
 *  Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 *
 * 30042000 Jens Axboe <axboe@kernel.dk> :
 *
 * Split the elevator a bit so that it is possible to choose a different
 * one or even write a new "plug in". There are three pieces:
 * - elevator_fn, inserts a new request in the queue list
 * - elevator_merge_fn, decides whether a new buffer can be merged with
 *   an existing request
 * - elevator_dequeue_fn, called when a request is taken off the active list
 *
 * 20082000 Dave Jones <davej@suse.de> :
 * Removed tests for max-bomb-segments, which was breaking elvtune
 *  when run without -bN
 *
 * Jens:
 * - Rework again to work with bio instead of buffer_heads
 * - loose bi_dev comparisons, partition handling is right now
 * - completely modularize elevator setup and teardown
 *
 */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/blktrace_api.h>
#include <linux/hash.h>
#include <linux/uaccess.h>
#include <linux/pm_runtime.h>

#include <trace/events/block.h>

#include "elevator.h"
#include "blk.h"
#include "blk-mq-sched.h"
#include "blk-pm.h"
#include "blk-wbt.h"
#include "blk-cgroup.h"

/* Holding context data for changing elevator */
struct elv_change_ctx {
	const char *name;
	bool no_uevent;

	/* for unregistering old elevator */
	struct elevator_queue *old;
	/* for registering new elevator */
	struct elevator_queue *new;
	/* holds sched tags data */
	struct elevator_tags *et;
};

static DEFINE_SPINLOCK(elv_list_lock);
static LIST_HEAD(elv_list);

/*
 * Merge hash stuff.
 */
#define rq_hash_key(rq)		(blk_rq_pos(rq) + blk_rq_sectors(rq))

/*
 * Query io scheduler to see if the current process issuing bio may be
 * merged with rq.
 */
static bool elv_iosched_allow_bio_merge(struct request *rq, struct bio *bio)
{
	struct request_queue *q = rq->q;
	struct elevator_queue *e = q->elevator;

	if (e->type->ops.allow_merge)
		return e->type->ops.allow_merge(q, rq, bio);

	return true;
}

/*
 * can we safely merge with this request?
 */
bool elv_bio_merge_ok(struct request *rq, struct bio *bio)
{
	if (!blk_rq_merge_ok(rq, bio))
		return false;

	if (!elv_iosched_allow_bio_merge(rq, bio))
		return false;

	return true;
}
EXPORT_SYMBOL(elv_bio_merge_ok);

/**
 * elevator_match - Check whether @e's name or alias matches @name
 * @e: Scheduler to test
 * @name: Elevator name to test
 *
 * Return true if the elevator @e's name or alias matches @name.
 */
static bool elevator_match(const struct elevator_type *e, const char *name)
{
	return !strcmp(e->elevator_name, name) ||
		(e->elevator_alias && !strcmp(e->elevator_alias, name));
}

static struct elevator_type *__elevator_find(const char *name)
{
	struct elevator_type *e;

	list_for_each_entry(e, &elv_list, list)
		if (elevator_match(e, name))
			return e;
	return NULL;
}

static struct elevator_type *elevator_find_get(const char *name)
{
	struct elevator_type *e;

	spin_lock(&elv_list_lock);
	e = __elevator_find(name);
	if (e && (!elevator_tryget(e)))
		e = NULL;
	spin_unlock(&elv_list_lock);
	return e;
}

static const struct kobj_type elv_ktype;

struct elevator_queue *elevator_alloc(struct request_queue *q,
		struct elevator_type *e, struct elevator_tags *et)
{
	struct elevator_queue *eq;

	eq = kzalloc_node(sizeof(*eq), GFP_KERNEL, q->node);
	if (unlikely(!eq))
		return NULL;

	__elevator_get(e);
	eq->type = e;
	kobject_init(&eq->kobj, &elv_ktype);
	mutex_init(&eq->sysfs_lock);
	hash_init(eq->hash);
	eq->et = et;

	return eq;
}

static void elevator_release(struct kobject *kobj)
{
	struct elevator_queue *e;

	e = container_of(kobj, struct elevator_queue, kobj);
	elevator_put(e->type);
	kfree(e);
}

static void elevator_exit(struct request_queue *q)
{
	struct elevator_queue *e = q->elevator;

	lockdep_assert_held(&q->elevator_lock);

	ioc_clear_queue(q);

	mutex_lock(&e->sysfs_lock);
	blk_mq_exit_sched(q, e);
	mutex_unlock(&e->sysfs_lock);
}

static inline void __elv_rqhash_del(struct request *rq)
{
	hash_del(&rq->hash);
	rq->rq_flags &= ~RQF_HASHED;
}

void elv_rqhash_del(struct request_queue *q, struct request *rq)
{
	if (ELV_ON_HASH(rq))
		__elv_rqhash_del(rq);
}
EXPORT_SYMBOL_GPL(elv_rqhash_del);

void elv_rqhash_add(struct request_queue *q, struct request *rq)
{
	struct elevator_queue *e = q->elevator;

	BUG_ON(ELV_ON_HASH(rq));
	hash_add(e->hash, &rq->hash, rq_hash_key(rq));
	rq->rq_flags |= RQF_HASHED;
}
EXPORT_SYMBOL_GPL(elv_rqhash_add);

void elv_rqhash_reposition(struct request_queue *q, struct request *rq)
{
	__elv_rqhash_del(rq);
	elv_rqhash_add(q, rq);
}

struct request *elv_rqhash_find(struct request_queue *q, sector_t offset)
{
	struct elevator_queue *e = q->elevator;
	struct hlist_node *next;
	struct request *rq;

	hash_for_each_possible_safe(e->hash, rq, next, hash, offset) {
		BUG_ON(!ELV_ON_HASH(rq));

		if (unlikely(!rq_mergeable(rq))) {
			__elv_rqhash_del(rq);
			continue;
		}

		if (rq_hash_key(rq) == offset)
			return rq;
	}

	return NULL;
}

/*
 * RB-tree support functions for inserting/lookup/removal of requests
 * in a sorted RB tree.
 */
void elv_rb_add(struct rb_root *root, struct request *rq)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct request *__rq;

	while (*p) {
		parent = *p;
		__rq = rb_entry(parent, struct request, rb_node);

		if (blk_rq_pos(rq) < blk_rq_pos(__rq))
			p = &(*p)->rb_left;
		else if (blk_rq_pos(rq) >= blk_rq_pos(__rq))
			p = &(*p)->rb_right;
	}

	rb_link_node(&rq->rb_node, parent, p);
	rb_insert_color(&rq->rb_node, root);
}
EXPORT_SYMBOL(elv_rb_add);

void elv_rb_del(struct rb_root *root, struct request *rq)
{
	BUG_ON(RB_EMPTY_NODE(&rq->rb_node));
	rb_erase(&rq->rb_node, root);
	RB_CLEAR_NODE(&rq->rb_node);
}
EXPORT_SYMBOL(elv_rb_del);

struct request *elv_rb_find(struct rb_root *root, sector_t sector)
{
	struct rb_node *n = root->rb_node;
	struct request *rq;

	while (n) {
		rq = rb_entry(n, struct request, rb_node);

		if (sector < blk_rq_pos(rq))
			n = n->rb_left;
		else if (sector > blk_rq_pos(rq))
			n = n->rb_right;
		else
			return rq;
	}

	return NULL;
}
EXPORT_SYMBOL(elv_rb_find);

enum elv_merge elv_merge(struct request_queue *q, struct request **req,
		struct bio *bio)
{
	struct elevator_queue *e = q->elevator;
	struct request *__rq;

	/*
	 * Levels of merges:
	 * 	nomerges:  No merges at all attempted
	 * 	noxmerges: Only simple one-hit cache try
	 * 	merges:	   All merge tries attempted
	 */
	if (blk_queue_nomerges(q) || !bio_mergeable(bio))
		return ELEVATOR_NO_MERGE;

	/*
	 * First try one-hit cache.
	 */
	if (q->last_merge && elv_bio_merge_ok(q->last_merge, bio)) {
		enum elv_merge ret = blk_try_merge(q->last_merge, bio);

		if (ret != ELEVATOR_NO_MERGE) {
			*req = q->last_merge;
			return ret;
		}
	}

	if (blk_queue_noxmerges(q))
		return ELEVATOR_NO_MERGE;

	/*
	 * See if our hash lookup can find a potential backmerge.
	 */
	__rq = elv_rqhash_find(q, bio->bi_iter.bi_sector);
	if (__rq && elv_bio_merge_ok(__rq, bio)) {
		*req = __rq;

		if (blk_discard_mergable(__rq))
			return ELEVATOR_DISCARD_MERGE;
		return ELEVATOR_BACK_MERGE;
	}

	if (e->type->ops.request_merge)
		return e->type->ops.request_merge(q, req, bio);

	return ELEVATOR_NO_MERGE;
}

/*
 * Attempt to do an insertion back merge. Only check for the case where
 * we can append 'rq' to an existing request, so we can throw 'rq' away
 * afterwards.
 *
 * Returns true if we merged, false otherwise. 'free' will contain all
 * requests that need to be freed.
 */
bool elv_attempt_insert_merge(struct request_queue *q, struct request *rq,
			      struct list_head *free)
{
	struct request *__rq;
	bool ret;

	if (blk_queue_nomerges(q))
		return false;

	/*
	 * First try one-hit cache.
	 */
	if (q->last_merge && blk_attempt_req_merge(q, q->last_merge, rq)) {
		list_add(&rq->queuelist, free);
		return true;
	}

	if (blk_queue_noxmerges(q))
		return false;

	ret = false;
	/*
	 * See if our hash lookup can find a potential backmerge.
	 */
	while (1) {
		__rq = elv_rqhash_find(q, blk_rq_pos(rq));
		if (!__rq || !blk_attempt_req_merge(q, __rq, rq))
			break;

		list_add(&rq->queuelist, free);
		/* The merged request could be merged with others, try again */
		ret = true;
		rq = __rq;
	}

	return ret;
}

void elv_merged_request(struct request_queue *q, struct request *rq,
		enum elv_merge type)
{
	struct elevator_queue *e = q->elevator;

	if (e->type->ops.request_merged)
		e->type->ops.request_merged(q, rq, type);

	if (type == ELEVATOR_BACK_MERGE)
		elv_rqhash_reposition(q, rq);

	q->last_merge = rq;
}

void elv_merge_requests(struct request_queue *q, struct request *rq,
			     struct request *next)
{
	struct elevator_queue *e = q->elevator;

	if (e->type->ops.requests_merged)
		e->type->ops.requests_merged(q, rq, next);

	elv_rqhash_reposition(q, rq);
	q->last_merge = rq;
}

struct request *elv_latter_request(struct request_queue *q, struct request *rq)
{
	struct elevator_queue *e = q->elevator;

	if (e->type->ops.next_request)
		return e->type->ops.next_request(q, rq);

	return NULL;
}

struct request *elv_former_request(struct request_queue *q, struct request *rq)
{
	struct elevator_queue *e = q->elevator;

	if (e->type->ops.former_request)
		return e->type->ops.former_request(q, rq);

	return NULL;
}

#define to_elv(atr) container_of_const((atr), struct elv_fs_entry, attr)

static ssize_t
elv_attr_show(struct kobject *kobj, struct attribute *attr, char *page)
{
	const struct elv_fs_entry *entry = to_elv(attr);
	struct elevator_queue *e;
	ssize_t error = -ENODEV;

	if (!entry->show)
		return -EIO;

	e = container_of(kobj, struct elevator_queue, kobj);
	mutex_lock(&e->sysfs_lock);
	if (!test_bit(ELEVATOR_FLAG_DYING, &e->flags))
		error = entry->show(e, page);
	mutex_unlock(&e->sysfs_lock);
	return error;
}

static ssize_t
elv_attr_store(struct kobject *kobj, struct attribute *attr,
	       const char *page, size_t length)
{
	const struct elv_fs_entry *entry = to_elv(attr);
	struct elevator_queue *e;
	ssize_t error = -ENODEV;

	if (!entry->store)
		return -EIO;

	e = container_of(kobj, struct elevator_queue, kobj);
	mutex_lock(&e->sysfs_lock);
	if (!test_bit(ELEVATOR_FLAG_DYING, &e->flags))
		error = entry->store(e, page, length);
	mutex_unlock(&e->sysfs_lock);
	return error;
}

static const struct sysfs_ops elv_sysfs_ops = {
	.show	= elv_attr_show,
	.store	= elv_attr_store,
};

static const struct kobj_type elv_ktype = {
	.sysfs_ops	= &elv_sysfs_ops,
	.release	= elevator_release,
};

static int elv_register_queue(struct request_queue *q,
			      struct elevator_queue *e,
			      bool uevent)
{
	int error;

	error = kobject_add(&e->kobj, &q->disk->queue_kobj, "iosched");
	if (!error) {
		const struct elv_fs_entry *attr = e->type->elevator_attrs;
		if (attr) {
			while (attr->attr.name) {
				if (sysfs_create_file(&e->kobj, &attr->attr))
					break;
				attr++;
			}
		}
		if (uevent)
			kobject_uevent(&e->kobj, KOBJ_ADD);

		/*
		 * Sched is initialized, it is ready to export it via
		 * debugfs
		 */
		blk_mq_sched_reg_debugfs(q);
		set_bit(ELEVATOR_FLAG_REGISTERED, &e->flags);
	}
	return error;
}

static void elv_unregister_queue(struct request_queue *q,
				 struct elevator_queue *e)
{
	if (e && test_and_clear_bit(ELEVATOR_FLAG_REGISTERED, &e->flags)) {
		kobject_uevent(&e->kobj, KOBJ_REMOVE);
		kobject_del(&e->kobj);

		/* unexport via debugfs before exiting sched */
		blk_mq_sched_unreg_debugfs(q);
	}
}

int elv_register(struct elevator_type *e)
{
	/* finish request is mandatory */
	if (WARN_ON_ONCE(!e->ops.finish_request))
		return -EINVAL;
	/* insert_requests and dispatch_request are mandatory */
	if (WARN_ON_ONCE(!e->ops.insert_requests || !e->ops.dispatch_request))
		return -EINVAL;

	/* create icq_cache if requested */
	if (e->icq_size) {
		if (WARN_ON(e->icq_size < sizeof(struct io_cq)) ||
		    WARN_ON(e->icq_align < __alignof__(struct io_cq)))
			return -EINVAL;

		snprintf(e->icq_cache_name, sizeof(e->icq_cache_name),
			 "%s_io_cq", e->elevator_name);
		e->icq_cache = kmem_cache_create(e->icq_cache_name, e->icq_size,
						 e->icq_align, 0, NULL);
		if (!e->icq_cache)
			return -ENOMEM;
	}

	/* register, don't allow duplicate names */
	spin_lock(&elv_list_lock);
	if (__elevator_find(e->elevator_name)) {
		spin_unlock(&elv_list_lock);
		kmem_cache_destroy(e->icq_cache);
		return -EBUSY;
	}
	list_add_tail(&e->list, &elv_list);
	spin_unlock(&elv_list_lock);

	printk(KERN_INFO "io scheduler %s registered\n", e->elevator_name);

	return 0;
}
EXPORT_SYMBOL_GPL(elv_register);

void elv_unregister(struct elevator_type *e)
{
	/* unregister */
	spin_lock(&elv_list_lock);
	list_del_init(&e->list);
	spin_unlock(&elv_list_lock);

	/*
	 * Destroy icq_cache if it exists.  icq's are RCU managed.  Make
	 * sure all RCU operations are complete before proceeding.
	 */
	if (e->icq_cache) {
		rcu_barrier();
		kmem_cache_destroy(e->icq_cache);
		e->icq_cache = NULL;
	}
}
EXPORT_SYMBOL_GPL(elv_unregister);

/*
 * Switch to new_e io scheduler.
 *
 * If switching fails, we are most likely running out of memory and not able
 * to restore the old io scheduler, so leaving the io scheduler being none.
 */
static int elevator_switch(struct request_queue *q, struct elv_change_ctx *ctx)
{
	struct elevator_type *new_e = NULL;
	int ret = 0;

	WARN_ON_ONCE(q->mq_freeze_depth == 0);
	lockdep_assert_held(&q->elevator_lock);

	if (strncmp(ctx->name, "none", 4)) {
		new_e = elevator_find_get(ctx->name);
		if (!new_e)
			return -EINVAL;
	}

	blk_mq_quiesce_queue(q);

	if (q->elevator) {
		ctx->old = q->elevator;
		elevator_exit(q);
	}

	if (new_e) {
		ret = blk_mq_init_sched(q, new_e, ctx->et);
		if (ret)
			goto out_unfreeze;
		ctx->new = q->elevator;
	} else {
		blk_queue_flag_clear(QUEUE_FLAG_SQ_SCHED, q);
		q->elevator = NULL;
		q->nr_requests = q->tag_set->queue_depth;
	}
	blk_add_trace_msg(q, "elv switch: %s", ctx->name);

out_unfreeze:
	blk_mq_unquiesce_queue(q);

	if (ret) {
		pr_warn("elv: switch to \"%s\" failed, falling back to \"none\"\n",
			new_e->elevator_name);
	}

	if (new_e)
		elevator_put(new_e);
	return ret;
}

static void elv_exit_and_release(struct request_queue *q)
{
	struct elevator_queue *e;
	unsigned memflags;

	memflags = blk_mq_freeze_queue(q);
	mutex_lock(&q->elevator_lock);
	e = q->elevator;
	elevator_exit(q);
	mutex_unlock(&q->elevator_lock);
	blk_mq_unfreeze_queue(q, memflags);
	if (e) {
		blk_mq_free_sched_tags(e->et, q->tag_set);
		kobject_put(&e->kobj);
	}
}

static int elevator_change_done(struct request_queue *q,
				struct elv_change_ctx *ctx)
{
	int ret = 0;

	if (ctx->old) {
		bool enable_wbt = test_bit(ELEVATOR_FLAG_ENABLE_WBT_ON_EXIT,
				&ctx->old->flags);

		elv_unregister_queue(q, ctx->old);
		blk_mq_free_sched_tags(ctx->old->et, q->tag_set);
		kobject_put(&ctx->old->kobj);
		if (enable_wbt)
			wbt_enable_default(q->disk);
	}
	if (ctx->new) {
		ret = elv_register_queue(q, ctx->new, !ctx->no_uevent);
		if (ret)
			elv_exit_and_release(q);
	}
	return ret;
}

/*
 * Switch this queue to the given IO scheduler.
 */
static int elevator_change(struct request_queue *q, struct elv_change_ctx *ctx)
{
	unsigned int memflags;
	struct blk_mq_tag_set *set = q->tag_set;
	int ret = 0;

	lockdep_assert_held(&set->update_nr_hwq_lock);

	if (strncmp(ctx->name, "none", 4)) {
		ctx->et = blk_mq_alloc_sched_tags(set, set->nr_hw_queues,
				blk_mq_default_nr_requests(set));
		if (!ctx->et)
			return -ENOMEM;
	}

	memflags = blk_mq_freeze_queue(q);
	/*
	 * May be called before adding disk, when there isn't any FS I/O,
	 * so freezing queue plus canceling dispatch work is enough to
	 * drain any dispatch activities originated from passthrough
	 * requests, then no need to quiesce queue which may add long boot
	 * latency, especially when lots of disks are involved.
	 *
	 * Disk isn't added yet, so verifying queue lock only manually.
	 */
	blk_mq_cancel_work_sync(q);
	mutex_lock(&q->elevator_lock);
	if (!(q->elevator && elevator_match(q->elevator->type, ctx->name)))
		ret = elevator_switch(q, ctx);
	mutex_unlock(&q->elevator_lock);
	blk_mq_unfreeze_queue(q, memflags);
	if (!ret)
		ret = elevator_change_done(q, ctx);
	/*
	 * Free sched tags if it's allocated but we couldn't switch elevator.
	 */
	if (ctx->et && !ctx->new)
		blk_mq_free_sched_tags(ctx->et, set);

	return ret;
}

/*
 * The I/O scheduler depends on the number of hardware queues, this forces a
 * reattachment when nr_hw_queues changes.
 */
void elv_update_nr_hw_queues(struct request_queue *q, struct elevator_type *e,
		struct elevator_tags *t)
{
	struct blk_mq_tag_set *set = q->tag_set;
	struct elv_change_ctx ctx = {};
	int ret = -ENODEV;

	WARN_ON_ONCE(q->mq_freeze_depth == 0);

	if (e && !blk_queue_dying(q) && blk_queue_registered(q)) {
		ctx.name = e->elevator_name;
		ctx.et = t;

		mutex_lock(&q->elevator_lock);
		/* force to reattach elevator after nr_hw_queue is updated */
		ret = elevator_switch(q, &ctx);
		mutex_unlock(&q->elevator_lock);
	}
	blk_mq_unfreeze_queue_nomemrestore(q);
	if (!ret)
		WARN_ON_ONCE(elevator_change_done(q, &ctx));
	/*
	 * Free sched tags if it's allocated but we couldn't switch elevator.
	 */
	if (t && !ctx.new)
		blk_mq_free_sched_tags(t, set);
}

/*
 * Use the default elevator settings. If the chosen elevator initialization
 * fails, fall back to the "none" elevator (no elevator).
 */
void elevator_set_default(struct request_queue *q)
{
	struct elv_change_ctx ctx = {
		.name = "mq-deadline",
		.no_uevent = true,
	};
	int err;
	struct elevator_type *e;

	/* now we allow to switch elevator */
	blk_queue_flag_clear(QUEUE_FLAG_NO_ELV_SWITCH, q);

	if (q->tag_set->flags & BLK_MQ_F_NO_SCHED_BY_DEFAULT)
		return;

	/*
	 * For single queue devices, default to using mq-deadline. If we
	 * have multiple queues or mq-deadline is not available, default
	 * to "none".
	 */
	e = elevator_find_get(ctx.name);
	if (!e)
		return;

	if ((q->nr_hw_queues == 1 ||
			blk_mq_is_shared_tags(q->tag_set->flags))) {
		err = elevator_change(q, &ctx);
		if (err < 0)
			pr_warn("\"%s\" elevator initialization, failed %d, falling back to \"none\"\n",
					ctx.name, err);
	}
	elevator_put(e);
}

void elevator_set_none(struct request_queue *q)
{
	struct elv_change_ctx ctx = {
		.name	= "none",
	};
	int err;

	err = elevator_change(q, &ctx);
	if (err < 0)
		pr_warn("%s: set none elevator failed %d\n", __func__, err);
}

static void elv_iosched_load_module(const char *elevator_name)
{
	struct elevator_type *found;

	spin_lock(&elv_list_lock);
	found = __elevator_find(elevator_name);
	spin_unlock(&elv_list_lock);

	if (!found)
		request_module("%s-iosched", elevator_name);
}

ssize_t elv_iosched_store(struct gendisk *disk, const char *buf,
			  size_t count)
{
	char elevator_name[ELV_NAME_MAX];
	struct elv_change_ctx ctx = {};
	int ret;
	struct request_queue *q = disk->queue;
	struct blk_mq_tag_set *set = q->tag_set;

	/* Make sure queue is not in the middle of being removed */
	if (!blk_queue_registered(q))
		return -ENOENT;

	/*
	 * If the attribute needs to load a module, do it before freezing the
	 * queue to ensure that the module file can be read when the request
	 * queue is the one for the device storing the module file.
	 */
	strscpy(elevator_name, buf, sizeof(elevator_name));
	ctx.name = strstrip(elevator_name);

	elv_iosched_load_module(ctx.name);

	down_read(&set->update_nr_hwq_lock);
	if (!blk_queue_no_elv_switch(q)) {
		ret = elevator_change(q, &ctx);
		if (!ret)
			ret = count;
	} else {
		ret = -ENOENT;
	}
	up_read(&set->update_nr_hwq_lock);
	return ret;
}

ssize_t elv_iosched_show(struct gendisk *disk, char *name)
{
	struct request_queue *q = disk->queue;
	struct elevator_type *cur = NULL, *e;
	int len = 0;

	mutex_lock(&q->elevator_lock);
	if (!q->elevator) {
		len += sprintf(name+len, "[none] ");
	} else {
		len += sprintf(name+len, "none ");
		cur = q->elevator->type;
	}

	spin_lock(&elv_list_lock);
	list_for_each_entry(e, &elv_list, list) {
		if (e == cur)
			len += sprintf(name+len, "[%s] ", e->elevator_name);
		else
			len += sprintf(name+len, "%s ", e->elevator_name);
	}
	spin_unlock(&elv_list_lock);

	len += sprintf(name+len, "\n");
	mutex_unlock(&q->elevator_lock);

	return len;
}

struct request *elv_rb_former_request(struct request_queue *q,
				      struct request *rq)
{
	struct rb_node *rbprev = rb_prev(&rq->rb_node);

	if (rbprev)
		return rb_entry_rq(rbprev);

	return NULL;
}
EXPORT_SYMBOL(elv_rb_former_request);

struct request *elv_rb_latter_request(struct request_queue *q,
				      struct request *rq)
{
	struct rb_node *rbnext = rb_next(&rq->rb_node);

	if (rbnext)
		return rb_entry_rq(rbnext);

	return NULL;
}
EXPORT_SYMBOL(elv_rb_latter_request);

static int __init elevator_setup(char *str)
{
	pr_warn("Kernel parameter elevator= does not have any effect anymore.\n"
		"Please use sysfs to set IO scheduler for individual devices.\n");
	return 1;
}

__setup("elevator=", elevator_setup);
