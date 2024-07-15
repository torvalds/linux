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

static struct elevator_type *elevator_find_get(struct request_queue *q,
		const char *name)
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
				  struct elevator_type *e)
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

	return eq;
}
EXPORT_SYMBOL(elevator_alloc);

static void elevator_release(struct kobject *kobj)
{
	struct elevator_queue *e;

	e = container_of(kobj, struct elevator_queue, kobj);
	elevator_put(e->type);
	kfree(e);
}

void elevator_exit(struct request_queue *q)
{
	struct elevator_queue *e = q->elevator;

	ioc_clear_queue(q);
	blk_mq_sched_free_rqs(q);

	mutex_lock(&e->sysfs_lock);
	blk_mq_exit_sched(q, e);
	mutex_unlock(&e->sysfs_lock);

	kobject_put(&e->kobj);
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

#define to_elv(atr) container_of((atr), struct elv_fs_entry, attr)

static ssize_t
elv_attr_show(struct kobject *kobj, struct attribute *attr, char *page)
{
	struct elv_fs_entry *entry = to_elv(attr);
	struct elevator_queue *e;
	ssize_t error;

	if (!entry->show)
		return -EIO;

	e = container_of(kobj, struct elevator_queue, kobj);
	mutex_lock(&e->sysfs_lock);
	error = e->type ? entry->show(e, page) : -ENOENT;
	mutex_unlock(&e->sysfs_lock);
	return error;
}

static ssize_t
elv_attr_store(struct kobject *kobj, struct attribute *attr,
	       const char *page, size_t length)
{
	struct elv_fs_entry *entry = to_elv(attr);
	struct elevator_queue *e;
	ssize_t error;

	if (!entry->store)
		return -EIO;

	e = container_of(kobj, struct elevator_queue, kobj);
	mutex_lock(&e->sysfs_lock);
	error = e->type ? entry->store(e, page, length) : -ENOENT;
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

int elv_register_queue(struct request_queue *q, bool uevent)
{
	struct elevator_queue *e = q->elevator;
	int error;

	lockdep_assert_held(&q->sysfs_lock);

	error = kobject_add(&e->kobj, &q->disk->queue_kobj, "iosched");
	if (!error) {
		struct elv_fs_entry *attr = e->type->elevator_attrs;
		if (attr) {
			while (attr->attr.name) {
				if (sysfs_create_file(&e->kobj, &attr->attr))
					break;
				attr++;
			}
		}
		if (uevent)
			kobject_uevent(&e->kobj, KOBJ_ADD);

		set_bit(ELEVATOR_FLAG_REGISTERED, &e->flags);
	}
	return error;
}

void elv_unregister_queue(struct request_queue *q)
{
	struct elevator_queue *e = q->elevator;

	lockdep_assert_held(&q->sysfs_lock);

	if (e && test_and_clear_bit(ELEVATOR_FLAG_REGISTERED, &e->flags)) {
		kobject_uevent(&e->kobj, KOBJ_REMOVE);
		kobject_del(&e->kobj);
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

static inline bool elv_support_iosched(struct request_queue *q)
{
	if (!queue_is_mq(q) ||
	    (q->tag_set && (q->tag_set->flags & BLK_MQ_F_NO_SCHED)))
		return false;
	return true;
}

/*
 * For single queue devices, default to using mq-deadline. If we have multiple
 * queues or mq-deadline is not available, default to "none".
 */
static struct elevator_type *elevator_get_default(struct request_queue *q)
{
	if (q->tag_set && q->tag_set->flags & BLK_MQ_F_NO_SCHED_BY_DEFAULT)
		return NULL;

	if (q->nr_hw_queues != 1 &&
	    !blk_mq_is_shared_tags(q->tag_set->flags))
		return NULL;

	return elevator_find_get(q, "mq-deadline");
}

/*
 * Use the default elevator settings. If the chosen elevator initialization
 * fails, fall back to the "none" elevator (no elevator).
 */
void elevator_init_mq(struct request_queue *q)
{
	struct elevator_type *e;
	int err;

	if (!elv_support_iosched(q))
		return;

	WARN_ON_ONCE(blk_queue_registered(q));

	if (unlikely(q->elevator))
		return;

	e = elevator_get_default(q);
	if (!e)
		return;

	/*
	 * We are called before adding disk, when there isn't any FS I/O,
	 * so freezing queue plus canceling dispatch work is enough to
	 * drain any dispatch activities originated from passthrough
	 * requests, then no need to quiesce queue which may add long boot
	 * latency, especially when lots of disks are involved.
	 */
	blk_mq_freeze_queue(q);
	blk_mq_cancel_work_sync(q);

	err = blk_mq_init_sched(q, e);

	blk_mq_unfreeze_queue(q);

	if (err) {
		pr_warn("\"%s\" elevator initialization failed, "
			"falling back to \"none\"\n", e->elevator_name);
	}

	elevator_put(e);
}

/*
 * Switch to new_e io scheduler.
 *
 * If switching fails, we are most likely running out of memory and not able
 * to restore the old io scheduler, so leaving the io scheduler being none.
 */
int elevator_switch(struct request_queue *q, struct elevator_type *new_e)
{
	int ret;

	lockdep_assert_held(&q->sysfs_lock);

	blk_mq_freeze_queue(q);
	blk_mq_quiesce_queue(q);

	if (q->elevator) {
		elv_unregister_queue(q);
		elevator_exit(q);
	}

	ret = blk_mq_init_sched(q, new_e);
	if (ret)
		goto out_unfreeze;

	ret = elv_register_queue(q, true);
	if (ret) {
		elevator_exit(q);
		goto out_unfreeze;
	}
	blk_add_trace_msg(q, "elv switch: %s", new_e->elevator_name);

out_unfreeze:
	blk_mq_unquiesce_queue(q);
	blk_mq_unfreeze_queue(q);

	if (ret) {
		pr_warn("elv: switch to \"%s\" failed, falling back to \"none\"\n",
			new_e->elevator_name);
	}

	return ret;
}

void elevator_disable(struct request_queue *q)
{
	lockdep_assert_held(&q->sysfs_lock);

	blk_mq_freeze_queue(q);
	blk_mq_quiesce_queue(q);

	elv_unregister_queue(q);
	elevator_exit(q);
	blk_queue_flag_clear(QUEUE_FLAG_SQ_SCHED, q);
	q->elevator = NULL;
	q->nr_requests = q->tag_set->queue_depth;
	blk_add_trace_msg(q, "elv switch: none");

	blk_mq_unquiesce_queue(q);
	blk_mq_unfreeze_queue(q);
}

/*
 * Switch this queue to the given IO scheduler.
 */
static int elevator_change(struct request_queue *q, const char *elevator_name)
{
	struct elevator_type *e;
	int ret;

	/* Make sure queue is not in the middle of being removed */
	if (!blk_queue_registered(q))
		return -ENOENT;

	if (!strncmp(elevator_name, "none", 4)) {
		if (q->elevator)
			elevator_disable(q);
		return 0;
	}

	if (q->elevator && elevator_match(q->elevator->type, elevator_name))
		return 0;

	e = elevator_find_get(q, elevator_name);
	if (!e) {
		request_module("%s-iosched", elevator_name);
		e = elevator_find_get(q, elevator_name);
		if (!e)
			return -EINVAL;
	}
	ret = elevator_switch(q, e);
	elevator_put(e);
	return ret;
}

ssize_t elv_iosched_store(struct request_queue *q, const char *buf,
			  size_t count)
{
	char elevator_name[ELV_NAME_MAX];
	int ret;

	if (!elv_support_iosched(q))
		return count;

	strscpy(elevator_name, buf, sizeof(elevator_name));
	ret = elevator_change(q, strstrip(elevator_name));
	if (!ret)
		return count;
	return ret;
}

ssize_t elv_iosched_show(struct request_queue *q, char *name)
{
	struct elevator_queue *eq = q->elevator;
	struct elevator_type *cur = NULL, *e;
	int len = 0;

	if (!elv_support_iosched(q))
		return sprintf(name, "none\n");

	if (!q->elevator) {
		len += sprintf(name+len, "[none] ");
	} else {
		len += sprintf(name+len, "none ");
		cur = eq->type;
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
