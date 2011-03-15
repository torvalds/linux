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
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/blktrace_api.h>
#include <linux/hash.h>
#include <linux/uaccess.h>

#include <trace/events/block.h>

#include "blk.h"

static DEFINE_SPINLOCK(elv_list_lock);
static LIST_HEAD(elv_list);

/*
 * Merge hash stuff.
 */
static const int elv_hash_shift = 6;
#define ELV_HASH_BLOCK(sec)	((sec) >> 3)
#define ELV_HASH_FN(sec)	\
		(hash_long(ELV_HASH_BLOCK((sec)), elv_hash_shift))
#define ELV_HASH_ENTRIES	(1 << elv_hash_shift)
#define rq_hash_key(rq)		(blk_rq_pos(rq) + blk_rq_sectors(rq))

/*
 * Query io scheduler to see if the current process issuing bio may be
 * merged with rq.
 */
static int elv_iosched_allow_merge(struct request *rq, struct bio *bio)
{
	struct request_queue *q = rq->q;
	struct elevator_queue *e = q->elevator;

	if (e->ops->elevator_allow_merge_fn)
		return e->ops->elevator_allow_merge_fn(q, rq, bio);

	return 1;
}

/*
 * can we safely merge with this request?
 */
int elv_rq_merge_ok(struct request *rq, struct bio *bio)
{
	if (!rq_mergeable(rq))
		return 0;

	/*
	 * Don't merge file system requests and discard requests
	 */
	if ((bio->bi_rw & REQ_DISCARD) != (rq->bio->bi_rw & REQ_DISCARD))
		return 0;

	/*
	 * Don't merge discard requests and secure discard requests
	 */
	if ((bio->bi_rw & REQ_SECURE) != (rq->bio->bi_rw & REQ_SECURE))
		return 0;

	/*
	 * different data direction or already started, don't merge
	 */
	if (bio_data_dir(bio) != rq_data_dir(rq))
		return 0;

	/*
	 * must be same device and not a special request
	 */
	if (rq->rq_disk != bio->bi_bdev->bd_disk || rq->special)
		return 0;

	/*
	 * only merge integrity protected bio into ditto rq
	 */
	if (bio_integrity(bio) != blk_integrity_rq(rq))
		return 0;

	if (!elv_iosched_allow_merge(rq, bio))
		return 0;

	return 1;
}
EXPORT_SYMBOL(elv_rq_merge_ok);

static inline int elv_try_merge(struct request *__rq, struct bio *bio)
{
	int ret = ELEVATOR_NO_MERGE;

	/*
	 * we can merge and sequence is ok, check if it's possible
	 */
	if (elv_rq_merge_ok(__rq, bio)) {
		if (blk_rq_pos(__rq) + blk_rq_sectors(__rq) == bio->bi_sector)
			ret = ELEVATOR_BACK_MERGE;
		else if (blk_rq_pos(__rq) - bio_sectors(bio) == bio->bi_sector)
			ret = ELEVATOR_FRONT_MERGE;
	}

	return ret;
}

static struct elevator_type *elevator_find(const char *name)
{
	struct elevator_type *e;

	list_for_each_entry(e, &elv_list, list) {
		if (!strcmp(e->elevator_name, name))
			return e;
	}

	return NULL;
}

static void elevator_put(struct elevator_type *e)
{
	module_put(e->elevator_owner);
}

static struct elevator_type *elevator_get(const char *name)
{
	struct elevator_type *e;

	spin_lock(&elv_list_lock);

	e = elevator_find(name);
	if (!e) {
		char elv[ELV_NAME_MAX + strlen("-iosched")];

		spin_unlock(&elv_list_lock);

		snprintf(elv, sizeof(elv), "%s-iosched", name);

		request_module("%s", elv);
		spin_lock(&elv_list_lock);
		e = elevator_find(name);
	}

	if (e && !try_module_get(e->elevator_owner))
		e = NULL;

	spin_unlock(&elv_list_lock);

	return e;
}

static void *elevator_init_queue(struct request_queue *q,
				 struct elevator_queue *eq)
{
	return eq->ops->elevator_init_fn(q);
}

static void elevator_attach(struct request_queue *q, struct elevator_queue *eq,
			   void *data)
{
	q->elevator = eq;
	eq->elevator_data = data;
}

static char chosen_elevator[16];

static int __init elevator_setup(char *str)
{
	/*
	 * Be backwards-compatible with previous kernels, so users
	 * won't get the wrong elevator.
	 */
	strncpy(chosen_elevator, str, sizeof(chosen_elevator) - 1);
	return 1;
}

__setup("elevator=", elevator_setup);

static struct kobj_type elv_ktype;

static struct elevator_queue *elevator_alloc(struct request_queue *q,
				  struct elevator_type *e)
{
	struct elevator_queue *eq;
	int i;

	eq = kmalloc_node(sizeof(*eq), GFP_KERNEL | __GFP_ZERO, q->node);
	if (unlikely(!eq))
		goto err;

	eq->ops = &e->ops;
	eq->elevator_type = e;
	kobject_init(&eq->kobj, &elv_ktype);
	mutex_init(&eq->sysfs_lock);

	eq->hash = kmalloc_node(sizeof(struct hlist_head) * ELV_HASH_ENTRIES,
					GFP_KERNEL, q->node);
	if (!eq->hash)
		goto err;

	for (i = 0; i < ELV_HASH_ENTRIES; i++)
		INIT_HLIST_HEAD(&eq->hash[i]);

	return eq;
err:
	kfree(eq);
	elevator_put(e);
	return NULL;
}

static void elevator_release(struct kobject *kobj)
{
	struct elevator_queue *e;

	e = container_of(kobj, struct elevator_queue, kobj);
	elevator_put(e->elevator_type);
	kfree(e->hash);
	kfree(e);
}

int elevator_init(struct request_queue *q, char *name)
{
	struct elevator_type *e = NULL;
	struct elevator_queue *eq;
	void *data;

	if (unlikely(q->elevator))
		return 0;

	INIT_LIST_HEAD(&q->queue_head);
	q->last_merge = NULL;
	q->end_sector = 0;
	q->boundary_rq = NULL;

	if (name) {
		e = elevator_get(name);
		if (!e)
			return -EINVAL;
	}

	if (!e && *chosen_elevator) {
		e = elevator_get(chosen_elevator);
		if (!e)
			printk(KERN_ERR "I/O scheduler %s not found\n",
							chosen_elevator);
	}

	if (!e) {
		e = elevator_get(CONFIG_DEFAULT_IOSCHED);
		if (!e) {
			printk(KERN_ERR
				"Default I/O scheduler not found. " \
				"Using noop.\n");
			e = elevator_get("noop");
		}
	}

	eq = elevator_alloc(q, e);
	if (!eq)
		return -ENOMEM;

	data = elevator_init_queue(q, eq);
	if (!data) {
		kobject_put(&eq->kobj);
		return -ENOMEM;
	}

	elevator_attach(q, eq, data);
	return 0;
}
EXPORT_SYMBOL(elevator_init);

void elevator_exit(struct elevator_queue *e)
{
	mutex_lock(&e->sysfs_lock);
	if (e->ops->elevator_exit_fn)
		e->ops->elevator_exit_fn(e);
	e->ops = NULL;
	mutex_unlock(&e->sysfs_lock);

	kobject_put(&e->kobj);
}
EXPORT_SYMBOL(elevator_exit);

static inline void __elv_rqhash_del(struct request *rq)
{
	hlist_del_init(&rq->hash);
}

static void elv_rqhash_del(struct request_queue *q, struct request *rq)
{
	if (ELV_ON_HASH(rq))
		__elv_rqhash_del(rq);
}

static void elv_rqhash_add(struct request_queue *q, struct request *rq)
{
	struct elevator_queue *e = q->elevator;

	BUG_ON(ELV_ON_HASH(rq));
	hlist_add_head(&rq->hash, &e->hash[ELV_HASH_FN(rq_hash_key(rq))]);
}

static void elv_rqhash_reposition(struct request_queue *q, struct request *rq)
{
	__elv_rqhash_del(rq);
	elv_rqhash_add(q, rq);
}

static struct request *elv_rqhash_find(struct request_queue *q, sector_t offset)
{
	struct elevator_queue *e = q->elevator;
	struct hlist_head *hash_list = &e->hash[ELV_HASH_FN(offset)];
	struct hlist_node *entry, *next;
	struct request *rq;

	hlist_for_each_entry_safe(rq, entry, next, hash_list, hash) {
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
struct request *elv_rb_add(struct rb_root *root, struct request *rq)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct request *__rq;

	while (*p) {
		parent = *p;
		__rq = rb_entry(parent, struct request, rb_node);

		if (blk_rq_pos(rq) < blk_rq_pos(__rq))
			p = &(*p)->rb_left;
		else if (blk_rq_pos(rq) > blk_rq_pos(__rq))
			p = &(*p)->rb_right;
		else
			return __rq;
	}

	rb_link_node(&rq->rb_node, parent, p);
	rb_insert_color(&rq->rb_node, root);
	return NULL;
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

/*
 * Insert rq into dispatch queue of q.  Queue lock must be held on
 * entry.  rq is sort instead into the dispatch queue. To be used by
 * specific elevators.
 */
void elv_dispatch_sort(struct request_queue *q, struct request *rq)
{
	sector_t boundary;
	struct list_head *entry;
	int stop_flags;

	if (q->last_merge == rq)
		q->last_merge = NULL;

	elv_rqhash_del(q, rq);

	q->nr_sorted--;

	boundary = q->end_sector;
	stop_flags = REQ_SOFTBARRIER | REQ_STARTED;
	list_for_each_prev(entry, &q->queue_head) {
		struct request *pos = list_entry_rq(entry);

		if ((rq->cmd_flags & REQ_DISCARD) !=
		    (pos->cmd_flags & REQ_DISCARD))
			break;
		if (rq_data_dir(rq) != rq_data_dir(pos))
			break;
		if (pos->cmd_flags & stop_flags)
			break;
		if (blk_rq_pos(rq) >= boundary) {
			if (blk_rq_pos(pos) < boundary)
				continue;
		} else {
			if (blk_rq_pos(pos) >= boundary)
				break;
		}
		if (blk_rq_pos(rq) >= blk_rq_pos(pos))
			break;
	}

	list_add(&rq->queuelist, entry);
}
EXPORT_SYMBOL(elv_dispatch_sort);

/*
 * Insert rq into dispatch queue of q.  Queue lock must be held on
 * entry.  rq is added to the back of the dispatch queue. To be used by
 * specific elevators.
 */
void elv_dispatch_add_tail(struct request_queue *q, struct request *rq)
{
	if (q->last_merge == rq)
		q->last_merge = NULL;

	elv_rqhash_del(q, rq);

	q->nr_sorted--;

	q->end_sector = rq_end_sector(rq);
	q->boundary_rq = rq;
	list_add_tail(&rq->queuelist, &q->queue_head);
}
EXPORT_SYMBOL(elv_dispatch_add_tail);

int elv_merge(struct request_queue *q, struct request **req, struct bio *bio)
{
	struct elevator_queue *e = q->elevator;
	struct request *__rq;
	int ret;

	/*
	 * Levels of merges:
	 * 	nomerges:  No merges at all attempted
	 * 	noxmerges: Only simple one-hit cache try
	 * 	merges:	   All merge tries attempted
	 */
	if (blk_queue_nomerges(q))
		return ELEVATOR_NO_MERGE;

	/*
	 * First try one-hit cache.
	 */
	if (q->last_merge) {
		ret = elv_try_merge(q->last_merge, bio);
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
	__rq = elv_rqhash_find(q, bio->bi_sector);
	if (__rq && elv_rq_merge_ok(__rq, bio)) {
		*req = __rq;
		return ELEVATOR_BACK_MERGE;
	}

	if (e->ops->elevator_merge_fn)
		return e->ops->elevator_merge_fn(q, req, bio);

	return ELEVATOR_NO_MERGE;
}

void elv_merged_request(struct request_queue *q, struct request *rq, int type)
{
	struct elevator_queue *e = q->elevator;

	if (e->ops->elevator_merged_fn)
		e->ops->elevator_merged_fn(q, rq, type);

	if (type == ELEVATOR_BACK_MERGE)
		elv_rqhash_reposition(q, rq);

	q->last_merge = rq;
}

void elv_merge_requests(struct request_queue *q, struct request *rq,
			     struct request *next)
{
	struct elevator_queue *e = q->elevator;

	if (e->ops->elevator_merge_req_fn)
		e->ops->elevator_merge_req_fn(q, rq, next);

	elv_rqhash_reposition(q, rq);
	elv_rqhash_del(q, next);

	q->nr_sorted--;
	q->last_merge = rq;
}

void elv_bio_merged(struct request_queue *q, struct request *rq,
			struct bio *bio)
{
	struct elevator_queue *e = q->elevator;

	if (e->ops->elevator_bio_merged_fn)
		e->ops->elevator_bio_merged_fn(q, rq, bio);
}

void elv_requeue_request(struct request_queue *q, struct request *rq)
{
	/*
	 * it already went through dequeue, we need to decrement the
	 * in_flight count again
	 */
	if (blk_account_rq(rq)) {
		q->in_flight[rq_is_sync(rq)]--;
		if (rq->cmd_flags & REQ_SORTED)
			elv_deactivate_rq(q, rq);
	}

	rq->cmd_flags &= ~REQ_STARTED;

	elv_insert(q, rq, ELEVATOR_INSERT_REQUEUE);
}

void elv_drain_elevator(struct request_queue *q)
{
	static int printed;
	while (q->elevator->ops->elevator_dispatch_fn(q, 1))
		;
	if (q->nr_sorted == 0)
		return;
	if (printed++ < 10) {
		printk(KERN_ERR "%s: forced dispatching is broken "
		       "(nr_sorted=%u), please report this\n",
		       q->elevator->elevator_type->elevator_name, q->nr_sorted);
	}
}

/*
 * Call with queue lock held, interrupts disabled
 */
void elv_quiesce_start(struct request_queue *q)
{
	if (!q->elevator)
		return;

	queue_flag_set(QUEUE_FLAG_ELVSWITCH, q);

	/*
	 * make sure we don't have any requests in flight
	 */
	elv_drain_elevator(q);
	while (q->rq.elvpriv) {
		__blk_run_queue(q, false);
		spin_unlock_irq(q->queue_lock);
		msleep(10);
		spin_lock_irq(q->queue_lock);
		elv_drain_elevator(q);
	}
}

void elv_quiesce_end(struct request_queue *q)
{
	queue_flag_clear(QUEUE_FLAG_ELVSWITCH, q);
}

void elv_insert(struct request_queue *q, struct request *rq, int where)
{
	int unplug_it = 1;

	trace_block_rq_insert(q, rq);

	rq->q = q;

	switch (where) {
	case ELEVATOR_INSERT_REQUEUE:
		/*
		 * Most requeues happen because of a busy condition,
		 * don't force unplug of the queue for that case.
		 * Clear unplug_it and fall through.
		 */
		unplug_it = 0;

	case ELEVATOR_INSERT_FRONT:
		rq->cmd_flags |= REQ_SOFTBARRIER;
		list_add(&rq->queuelist, &q->queue_head);
		break;

	case ELEVATOR_INSERT_BACK:
		rq->cmd_flags |= REQ_SOFTBARRIER;
		elv_drain_elevator(q);
		list_add_tail(&rq->queuelist, &q->queue_head);
		/*
		 * We kick the queue here for the following reasons.
		 * - The elevator might have returned NULL previously
		 *   to delay requests and returned them now.  As the
		 *   queue wasn't empty before this request, ll_rw_blk
		 *   won't run the queue on return, resulting in hang.
		 * - Usually, back inserted requests won't be merged
		 *   with anything.  There's no point in delaying queue
		 *   processing.
		 */
		__blk_run_queue(q, false);
		break;

	case ELEVATOR_INSERT_SORT:
		BUG_ON(rq->cmd_type != REQ_TYPE_FS &&
		       !(rq->cmd_flags & REQ_DISCARD));
		rq->cmd_flags |= REQ_SORTED;
		q->nr_sorted++;
		if (rq_mergeable(rq)) {
			elv_rqhash_add(q, rq);
			if (!q->last_merge)
				q->last_merge = rq;
		}

		/*
		 * Some ioscheds (cfq) run q->request_fn directly, so
		 * rq cannot be accessed after calling
		 * elevator_add_req_fn.
		 */
		q->elevator->ops->elevator_add_req_fn(q, rq);
		break;

	default:
		printk(KERN_ERR "%s: bad insertion point %d\n",
		       __func__, where);
		BUG();
	}

	if (unplug_it && blk_queue_plugged(q)) {
		int nrq = q->rq.count[BLK_RW_SYNC] + q->rq.count[BLK_RW_ASYNC]
				- queue_in_flight(q);

		if (nrq >= q->unplug_thresh)
			__generic_unplug_device(q);
	}
}

void __elv_add_request(struct request_queue *q, struct request *rq, int where,
		       int plug)
{
	if (rq->cmd_flags & REQ_SOFTBARRIER) {
		/* barriers are scheduling boundary, update end_sector */
		if (rq->cmd_type == REQ_TYPE_FS ||
		    (rq->cmd_flags & REQ_DISCARD)) {
			q->end_sector = rq_end_sector(rq);
			q->boundary_rq = rq;
		}
	} else if (!(rq->cmd_flags & REQ_ELVPRIV) &&
		    where == ELEVATOR_INSERT_SORT)
		where = ELEVATOR_INSERT_BACK;

	if (plug)
		blk_plug_device(q);

	elv_insert(q, rq, where);
}
EXPORT_SYMBOL(__elv_add_request);

void elv_add_request(struct request_queue *q, struct request *rq, int where,
		     int plug)
{
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	__elv_add_request(q, rq, where, plug);
	spin_unlock_irqrestore(q->queue_lock, flags);
}
EXPORT_SYMBOL(elv_add_request);

int elv_queue_empty(struct request_queue *q)
{
	struct elevator_queue *e = q->elevator;

	if (!list_empty(&q->queue_head))
		return 0;

	if (e->ops->elevator_queue_empty_fn)
		return e->ops->elevator_queue_empty_fn(q);

	return 1;
}
EXPORT_SYMBOL(elv_queue_empty);

struct request *elv_latter_request(struct request_queue *q, struct request *rq)
{
	struct elevator_queue *e = q->elevator;

	if (e->ops->elevator_latter_req_fn)
		return e->ops->elevator_latter_req_fn(q, rq);
	return NULL;
}

struct request *elv_former_request(struct request_queue *q, struct request *rq)
{
	struct elevator_queue *e = q->elevator;

	if (e->ops->elevator_former_req_fn)
		return e->ops->elevator_former_req_fn(q, rq);
	return NULL;
}

int elv_set_request(struct request_queue *q, struct request *rq, gfp_t gfp_mask)
{
	struct elevator_queue *e = q->elevator;

	if (e->ops->elevator_set_req_fn)
		return e->ops->elevator_set_req_fn(q, rq, gfp_mask);

	rq->elevator_private = NULL;
	return 0;
}

void elv_put_request(struct request_queue *q, struct request *rq)
{
	struct elevator_queue *e = q->elevator;

	if (e->ops->elevator_put_req_fn)
		e->ops->elevator_put_req_fn(rq);
}

int elv_may_queue(struct request_queue *q, int rw)
{
	struct elevator_queue *e = q->elevator;

	if (e->ops->elevator_may_queue_fn)
		return e->ops->elevator_may_queue_fn(q, rw);

	return ELV_MQUEUE_MAY;
}

void elv_abort_queue(struct request_queue *q)
{
	struct request *rq;

	while (!list_empty(&q->queue_head)) {
		rq = list_entry_rq(q->queue_head.next);
		rq->cmd_flags |= REQ_QUIET;
		trace_block_rq_abort(q, rq);
		/*
		 * Mark this request as started so we don't trigger
		 * any debug logic in the end I/O path.
		 */
		blk_start_request(rq);
		__blk_end_request_all(rq, -EIO);
	}
}
EXPORT_SYMBOL(elv_abort_queue);

void elv_completed_request(struct request_queue *q, struct request *rq)
{
	struct elevator_queue *e = q->elevator;

	/*
	 * request is released from the driver, io must be done
	 */
	if (blk_account_rq(rq)) {
		q->in_flight[rq_is_sync(rq)]--;
		if ((rq->cmd_flags & REQ_SORTED) &&
		    e->ops->elevator_completed_req_fn)
			e->ops->elevator_completed_req_fn(q, rq);
	}
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
	error = e->ops ? entry->show(e, page) : -ENOENT;
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
	error = e->ops ? entry->store(e, page, length) : -ENOENT;
	mutex_unlock(&e->sysfs_lock);
	return error;
}

static const struct sysfs_ops elv_sysfs_ops = {
	.show	= elv_attr_show,
	.store	= elv_attr_store,
};

static struct kobj_type elv_ktype = {
	.sysfs_ops	= &elv_sysfs_ops,
	.release	= elevator_release,
};

int elv_register_queue(struct request_queue *q)
{
	struct elevator_queue *e = q->elevator;
	int error;

	error = kobject_add(&e->kobj, &q->kobj, "%s", "iosched");
	if (!error) {
		struct elv_fs_entry *attr = e->elevator_type->elevator_attrs;
		if (attr) {
			while (attr->attr.name) {
				if (sysfs_create_file(&e->kobj, &attr->attr))
					break;
				attr++;
			}
		}
		kobject_uevent(&e->kobj, KOBJ_ADD);
		e->registered = 1;
	}
	return error;
}
EXPORT_SYMBOL(elv_register_queue);

static void __elv_unregister_queue(struct elevator_queue *e)
{
	kobject_uevent(&e->kobj, KOBJ_REMOVE);
	kobject_del(&e->kobj);
	e->registered = 0;
}

void elv_unregister_queue(struct request_queue *q)
{
	if (q)
		__elv_unregister_queue(q->elevator);
}
EXPORT_SYMBOL(elv_unregister_queue);

void elv_register(struct elevator_type *e)
{
	char *def = "";

	spin_lock(&elv_list_lock);
	BUG_ON(elevator_find(e->elevator_name));
	list_add_tail(&e->list, &elv_list);
	spin_unlock(&elv_list_lock);

	if (!strcmp(e->elevator_name, chosen_elevator) ||
			(!*chosen_elevator &&
			 !strcmp(e->elevator_name, CONFIG_DEFAULT_IOSCHED)))
				def = " (default)";

	printk(KERN_INFO "io scheduler %s registered%s\n", e->elevator_name,
								def);
}
EXPORT_SYMBOL_GPL(elv_register);

void elv_unregister(struct elevator_type *e)
{
	struct task_struct *g, *p;

	/*
	 * Iterate every thread in the process to remove the io contexts.
	 */
	if (e->ops.trim) {
		read_lock(&tasklist_lock);
		do_each_thread(g, p) {
			task_lock(p);
			if (p->io_context)
				e->ops.trim(p->io_context);
			task_unlock(p);
		} while_each_thread(g, p);
		read_unlock(&tasklist_lock);
	}

	spin_lock(&elv_list_lock);
	list_del_init(&e->list);
	spin_unlock(&elv_list_lock);
}
EXPORT_SYMBOL_GPL(elv_unregister);

/*
 * switch to new_e io scheduler. be careful not to introduce deadlocks -
 * we don't free the old io scheduler, before we have allocated what we
 * need for the new one. this way we have a chance of going back to the old
 * one, if the new one fails init for some reason.
 */
static int elevator_switch(struct request_queue *q, struct elevator_type *new_e)
{
	struct elevator_queue *old_elevator, *e;
	void *data;
	int err;

	/*
	 * Allocate new elevator
	 */
	e = elevator_alloc(q, new_e);
	if (!e)
		return -ENOMEM;

	data = elevator_init_queue(q, e);
	if (!data) {
		kobject_put(&e->kobj);
		return -ENOMEM;
	}

	/*
	 * Turn on BYPASS and drain all requests w/ elevator private data
	 */
	spin_lock_irq(q->queue_lock);
	elv_quiesce_start(q);

	/*
	 * Remember old elevator.
	 */
	old_elevator = q->elevator;

	/*
	 * attach and start new elevator
	 */
	elevator_attach(q, e, data);

	spin_unlock_irq(q->queue_lock);

	if (old_elevator->registered) {
		__elv_unregister_queue(old_elevator);

		err = elv_register_queue(q);
		if (err)
			goto fail_register;
	}

	/*
	 * finally exit old elevator and turn off BYPASS.
	 */
	elevator_exit(old_elevator);
	spin_lock_irq(q->queue_lock);
	elv_quiesce_end(q);
	spin_unlock_irq(q->queue_lock);

	blk_add_trace_msg(q, "elv switch: %s", e->elevator_type->elevator_name);

	return 0;

fail_register:
	/*
	 * switch failed, exit the new io scheduler and reattach the old
	 * one again (along with re-adding the sysfs dir)
	 */
	elevator_exit(e);
	q->elevator = old_elevator;
	elv_register_queue(q);

	spin_lock_irq(q->queue_lock);
	queue_flag_clear(QUEUE_FLAG_ELVSWITCH, q);
	spin_unlock_irq(q->queue_lock);

	return err;
}

/*
 * Switch this queue to the given IO scheduler.
 */
int elevator_change(struct request_queue *q, const char *name)
{
	char elevator_name[ELV_NAME_MAX];
	struct elevator_type *e;

	if (!q->elevator)
		return -ENXIO;

	strlcpy(elevator_name, name, sizeof(elevator_name));
	e = elevator_get(strstrip(elevator_name));
	if (!e) {
		printk(KERN_ERR "elevator: type %s not found\n", elevator_name);
		return -EINVAL;
	}

	if (!strcmp(elevator_name, q->elevator->elevator_type->elevator_name)) {
		elevator_put(e);
		return 0;
	}

	return elevator_switch(q, e);
}
EXPORT_SYMBOL(elevator_change);

ssize_t elv_iosched_store(struct request_queue *q, const char *name,
			  size_t count)
{
	int ret;

	if (!q->elevator)
		return count;

	ret = elevator_change(q, name);
	if (!ret)
		return count;

	printk(KERN_ERR "elevator: switch to %s failed\n", name);
	return ret;
}

ssize_t elv_iosched_show(struct request_queue *q, char *name)
{
	struct elevator_queue *e = q->elevator;
	struct elevator_type *elv;
	struct elevator_type *__e;
	int len = 0;

	if (!q->elevator || !blk_queue_stackable(q))
		return sprintf(name, "none\n");

	elv = e->elevator_type;

	spin_lock(&elv_list_lock);
	list_for_each_entry(__e, &elv_list, list) {
		if (!strcmp(elv->elevator_name, __e->elevator_name))
			len += sprintf(name+len, "[%s] ", elv->elevator_name);
		else
			len += sprintf(name+len, "%s ", __e->elevator_name);
	}
	spin_unlock(&elv_list_lock);

	len += sprintf(len+name, "\n");
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
