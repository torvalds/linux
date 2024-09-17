// SPDX-License-Identifier: GPL-2.0+
/*
 * Main SSAM/SSH controller structure and functionality.
 *
 * Copyright (C) 2019-2022 Maximilian Luz <luzmaximilian@gmail.com>
 */

#include <linux/acpi.h>
#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kref.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/mutex.h>
#include <linux/rculist.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/serdev.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/srcu.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <linux/surface_aggregator/controller.h>
#include <linux/surface_aggregator/serial_hub.h>

#include "controller.h"
#include "ssh_msgb.h"
#include "ssh_request_layer.h"

#include "trace.h"


/* -- Safe counters. -------------------------------------------------------- */

/**
 * ssh_seq_reset() - Reset/initialize sequence ID counter.
 * @c: The counter to reset.
 */
static void ssh_seq_reset(struct ssh_seq_counter *c)
{
	WRITE_ONCE(c->value, 0);
}

/**
 * ssh_seq_next() - Get next sequence ID.
 * @c: The counter providing the sequence IDs.
 *
 * Return: Returns the next sequence ID of the counter.
 */
static u8 ssh_seq_next(struct ssh_seq_counter *c)
{
	u8 old = READ_ONCE(c->value);
	u8 new = old + 1;
	u8 ret;

	while (unlikely((ret = cmpxchg(&c->value, old, new)) != old)) {
		old = ret;
		new = old + 1;
	}

	return old;
}

/**
 * ssh_rqid_reset() - Reset/initialize request ID counter.
 * @c: The counter to reset.
 */
static void ssh_rqid_reset(struct ssh_rqid_counter *c)
{
	WRITE_ONCE(c->value, 0);
}

/**
 * ssh_rqid_next() - Get next request ID.
 * @c: The counter providing the request IDs.
 *
 * Return: Returns the next request ID of the counter, skipping any reserved
 * request IDs.
 */
static u16 ssh_rqid_next(struct ssh_rqid_counter *c)
{
	u16 old = READ_ONCE(c->value);
	u16 new = ssh_rqid_next_valid(old);
	u16 ret;

	while (unlikely((ret = cmpxchg(&c->value, old, new)) != old)) {
		old = ret;
		new = ssh_rqid_next_valid(old);
	}

	return old;
}


/* -- Event notifier/callbacks. --------------------------------------------- */
/*
 * The notifier system is based on linux/notifier.h, specifically the SRCU
 * implementation. The difference to that is, that some bits of the notifier
 * call return value can be tracked across multiple calls. This is done so
 * that handling of events can be tracked and a warning can be issued in case
 * an event goes unhandled. The idea of that warning is that it should help
 * discover and identify new/currently unimplemented features.
 */

/**
 * ssam_event_matches_notifier() - Test if an event matches a notifier.
 * @n: The event notifier to test against.
 * @event: The event to test.
 *
 * Return: Returns %true if the given event matches the given notifier
 * according to the rules set in the notifier's event mask, %false otherwise.
 */
static bool ssam_event_matches_notifier(const struct ssam_event_notifier *n,
					const struct ssam_event *event)
{
	bool match = n->event.id.target_category == event->target_category;

	if (n->event.mask & SSAM_EVENT_MASK_TARGET)
		match &= n->event.reg.target_id == event->target_id;

	if (n->event.mask & SSAM_EVENT_MASK_INSTANCE)
		match &= n->event.id.instance == event->instance_id;

	return match;
}

/**
 * ssam_nfblk_call_chain() - Call event notifier callbacks of the given chain.
 * @nh:    The notifier head for which the notifier callbacks should be called.
 * @event: The event data provided to the callbacks.
 *
 * Call all registered notifier callbacks in order of their priority until
 * either no notifier is left or a notifier returns a value with the
 * %SSAM_NOTIF_STOP bit set. Note that this bit is automatically set via
 * ssam_notifier_from_errno() on any non-zero error value.
 *
 * Return: Returns the notifier status value, which contains the notifier
 * status bits (%SSAM_NOTIF_HANDLED and %SSAM_NOTIF_STOP) as well as a
 * potential error value returned from the last executed notifier callback.
 * Use ssam_notifier_to_errno() to convert this value to the original error
 * value.
 */
static int ssam_nfblk_call_chain(struct ssam_nf_head *nh, struct ssam_event *event)
{
	struct ssam_event_notifier *nf;
	int ret = 0, idx;

	idx = srcu_read_lock(&nh->srcu);

	list_for_each_entry_rcu(nf, &nh->head, base.node,
				srcu_read_lock_held(&nh->srcu)) {
		if (ssam_event_matches_notifier(nf, event)) {
			ret = (ret & SSAM_NOTIF_STATE_MASK) | nf->base.fn(nf, event);
			if (ret & SSAM_NOTIF_STOP)
				break;
		}
	}

	srcu_read_unlock(&nh->srcu, idx);
	return ret;
}

/**
 * ssam_nfblk_insert() - Insert a new notifier block into the given notifier
 * list.
 * @nh: The notifier head into which the block should be inserted.
 * @nb: The notifier block to add.
 *
 * Note: This function must be synchronized by the caller with respect to other
 * insert, find, and/or remove calls by holding ``struct ssam_nf.lock``.
 *
 * Return: Returns zero on success, %-EEXIST if the notifier block has already
 * been registered.
 */
static int ssam_nfblk_insert(struct ssam_nf_head *nh, struct ssam_notifier_block *nb)
{
	struct ssam_notifier_block *p;
	struct list_head *h;

	/* Runs under lock, no need for RCU variant. */
	list_for_each(h, &nh->head) {
		p = list_entry(h, struct ssam_notifier_block, node);

		if (unlikely(p == nb)) {
			WARN(1, "double register detected");
			return -EEXIST;
		}

		if (nb->priority > p->priority)
			break;
	}

	list_add_tail_rcu(&nb->node, h);
	return 0;
}

/**
 * ssam_nfblk_find() - Check if a notifier block is registered on the given
 * notifier head.
 * list.
 * @nh: The notifier head on which to search.
 * @nb: The notifier block to search for.
 *
 * Note: This function must be synchronized by the caller with respect to other
 * insert, find, and/or remove calls by holding ``struct ssam_nf.lock``.
 *
 * Return: Returns true if the given notifier block is registered on the given
 * notifier head, false otherwise.
 */
static bool ssam_nfblk_find(struct ssam_nf_head *nh, struct ssam_notifier_block *nb)
{
	struct ssam_notifier_block *p;

	/* Runs under lock, no need for RCU variant. */
	list_for_each_entry(p, &nh->head, node) {
		if (p == nb)
			return true;
	}

	return false;
}

/**
 * ssam_nfblk_remove() - Remove a notifier block from its notifier list.
 * @nb: The notifier block to be removed.
 *
 * Note: This function must be synchronized by the caller with respect to
 * other insert, find, and/or remove calls by holding ``struct ssam_nf.lock``.
 * Furthermore, the caller _must_ ensure SRCU synchronization by calling
 * synchronize_srcu() with ``nh->srcu`` after leaving the critical section, to
 * ensure that the removed notifier block is not in use any more.
 */
static void ssam_nfblk_remove(struct ssam_notifier_block *nb)
{
	list_del_rcu(&nb->node);
}

/**
 * ssam_nf_head_init() - Initialize the given notifier head.
 * @nh: The notifier head to initialize.
 */
static int ssam_nf_head_init(struct ssam_nf_head *nh)
{
	int status;

	status = init_srcu_struct(&nh->srcu);
	if (status)
		return status;

	INIT_LIST_HEAD(&nh->head);
	return 0;
}

/**
 * ssam_nf_head_destroy() - Deinitialize the given notifier head.
 * @nh: The notifier head to deinitialize.
 */
static void ssam_nf_head_destroy(struct ssam_nf_head *nh)
{
	cleanup_srcu_struct(&nh->srcu);
}


/* -- Event/notification registry. ------------------------------------------ */

/**
 * struct ssam_nf_refcount_key - Key used for event activation reference
 * counting.
 * @reg: The registry via which the event is enabled/disabled.
 * @id:  The ID uniquely describing the event.
 */
struct ssam_nf_refcount_key {
	struct ssam_event_registry reg;
	struct ssam_event_id id;
};

/**
 * struct ssam_nf_refcount_entry - RB-tree entry for reference counting event
 * activations.
 * @node:     The node of this entry in the rb-tree.
 * @key:      The key of the event.
 * @refcount: The reference-count of the event.
 * @flags:    The flags used when enabling the event.
 */
struct ssam_nf_refcount_entry {
	struct rb_node node;
	struct ssam_nf_refcount_key key;
	int refcount;
	u8 flags;
};

/**
 * ssam_nf_refcount_inc() - Increment reference-/activation-count of the given
 * event.
 * @nf:  The notifier system reference.
 * @reg: The registry used to enable/disable the event.
 * @id:  The event ID.
 *
 * Increments the reference-/activation-count associated with the specified
 * event type/ID, allocating a new entry for this event ID if necessary. A
 * newly allocated entry will have a refcount of one.
 *
 * Note: ``nf->lock`` must be held when calling this function.
 *
 * Return: Returns the refcount entry on success. Returns an error pointer
 * with %-ENOSPC if there have already been %INT_MAX events of the specified
 * ID and type registered, or %-ENOMEM if the entry could not be allocated.
 */
static struct ssam_nf_refcount_entry *
ssam_nf_refcount_inc(struct ssam_nf *nf, struct ssam_event_registry reg,
		     struct ssam_event_id id)
{
	struct ssam_nf_refcount_entry *entry;
	struct ssam_nf_refcount_key key;
	struct rb_node **link = &nf->refcount.rb_node;
	struct rb_node *parent = NULL;
	int cmp;

	lockdep_assert_held(&nf->lock);

	key.reg = reg;
	key.id = id;

	while (*link) {
		entry = rb_entry(*link, struct ssam_nf_refcount_entry, node);
		parent = *link;

		cmp = memcmp(&key, &entry->key, sizeof(key));
		if (cmp < 0) {
			link = &(*link)->rb_left;
		} else if (cmp > 0) {
			link = &(*link)->rb_right;
		} else if (entry->refcount < INT_MAX) {
			entry->refcount++;
			return entry;
		} else {
			WARN_ON(1);
			return ERR_PTR(-ENOSPC);
		}
	}

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return ERR_PTR(-ENOMEM);

	entry->key = key;
	entry->refcount = 1;

	rb_link_node(&entry->node, parent, link);
	rb_insert_color(&entry->node, &nf->refcount);

	return entry;
}

/**
 * ssam_nf_refcount_dec() - Decrement reference-/activation-count of the given
 * event.
 * @nf:  The notifier system reference.
 * @reg: The registry used to enable/disable the event.
 * @id:  The event ID.
 *
 * Decrements the reference-/activation-count of the specified event,
 * returning its entry. If the returned entry has a refcount of zero, the
 * caller is responsible for freeing it using kfree().
 *
 * Note: ``nf->lock`` must be held when calling this function.
 *
 * Return: Returns the refcount entry on success or %NULL if the entry has not
 * been found.
 */
static struct ssam_nf_refcount_entry *
ssam_nf_refcount_dec(struct ssam_nf *nf, struct ssam_event_registry reg,
		     struct ssam_event_id id)
{
	struct ssam_nf_refcount_entry *entry;
	struct ssam_nf_refcount_key key;
	struct rb_node *node = nf->refcount.rb_node;
	int cmp;

	lockdep_assert_held(&nf->lock);

	key.reg = reg;
	key.id = id;

	while (node) {
		entry = rb_entry(node, struct ssam_nf_refcount_entry, node);

		cmp = memcmp(&key, &entry->key, sizeof(key));
		if (cmp < 0) {
			node = node->rb_left;
		} else if (cmp > 0) {
			node = node->rb_right;
		} else {
			entry->refcount--;
			if (entry->refcount == 0)
				rb_erase(&entry->node, &nf->refcount);

			return entry;
		}
	}

	return NULL;
}

/**
 * ssam_nf_refcount_dec_free() - Decrement reference-/activation-count of the
 * given event and free its entry if the reference count reaches zero.
 * @nf:  The notifier system reference.
 * @reg: The registry used to enable/disable the event.
 * @id:  The event ID.
 *
 * Decrements the reference-/activation-count of the specified event, freeing
 * its entry if it reaches zero.
 *
 * Note: ``nf->lock`` must be held when calling this function.
 */
static void ssam_nf_refcount_dec_free(struct ssam_nf *nf,
				      struct ssam_event_registry reg,
				      struct ssam_event_id id)
{
	struct ssam_nf_refcount_entry *entry;

	lockdep_assert_held(&nf->lock);

	entry = ssam_nf_refcount_dec(nf, reg, id);
	if (entry && entry->refcount == 0)
		kfree(entry);
}

/**
 * ssam_nf_refcount_empty() - Test if the notification system has any
 * enabled/active events.
 * @nf: The notification system.
 */
static bool ssam_nf_refcount_empty(struct ssam_nf *nf)
{
	return RB_EMPTY_ROOT(&nf->refcount);
}

/**
 * ssam_nf_call() - Call notification callbacks for the provided event.
 * @nf:    The notifier system
 * @dev:   The associated device, only used for logging.
 * @rqid:  The request ID of the event.
 * @event: The event provided to the callbacks.
 *
 * Execute registered callbacks in order of their priority until either no
 * callback is left or a callback returns a value with the %SSAM_NOTIF_STOP
 * bit set. Note that this bit is set automatically when converting non-zero
 * error values via ssam_notifier_from_errno() to notifier values.
 *
 * Also note that any callback that could handle an event should return a value
 * with bit %SSAM_NOTIF_HANDLED set, indicating that the event does not go
 * unhandled/ignored. In case no registered callback could handle an event,
 * this function will emit a warning.
 *
 * In case a callback failed, this function will emit an error message.
 */
static void ssam_nf_call(struct ssam_nf *nf, struct device *dev, u16 rqid,
			 struct ssam_event *event)
{
	struct ssam_nf_head *nf_head;
	int status, nf_ret;

	if (!ssh_rqid_is_event(rqid)) {
		dev_warn(dev, "event: unsupported rqid: %#06x\n", rqid);
		return;
	}

	nf_head = &nf->head[ssh_rqid_to_event(rqid)];
	nf_ret = ssam_nfblk_call_chain(nf_head, event);
	status = ssam_notifier_to_errno(nf_ret);

	if (status < 0) {
		dev_err(dev,
			"event: error handling event: %d (tc: %#04x, tid: %#04x, cid: %#04x, iid: %#04x)\n",
			status, event->target_category, event->target_id,
			event->command_id, event->instance_id);
	} else if (!(nf_ret & SSAM_NOTIF_HANDLED)) {
		dev_warn(dev,
			 "event: unhandled event (rqid: %#04x, tc: %#04x, tid: %#04x, cid: %#04x, iid: %#04x)\n",
			 rqid, event->target_category, event->target_id,
			 event->command_id, event->instance_id);
	}
}

/**
 * ssam_nf_init() - Initialize the notifier system.
 * @nf: The notifier system to initialize.
 */
static int ssam_nf_init(struct ssam_nf *nf)
{
	int i, status;

	for (i = 0; i < SSH_NUM_EVENTS; i++) {
		status = ssam_nf_head_init(&nf->head[i]);
		if (status)
			break;
	}

	if (status) {
		while (i--)
			ssam_nf_head_destroy(&nf->head[i]);

		return status;
	}

	mutex_init(&nf->lock);
	return 0;
}

/**
 * ssam_nf_destroy() - Deinitialize the notifier system.
 * @nf: The notifier system to deinitialize.
 */
static void ssam_nf_destroy(struct ssam_nf *nf)
{
	int i;

	for (i = 0; i < SSH_NUM_EVENTS; i++)
		ssam_nf_head_destroy(&nf->head[i]);

	mutex_destroy(&nf->lock);
}


/* -- Event/async request completion system. -------------------------------- */

#define SSAM_CPLT_WQ_NAME	"ssam_cpltq"

/*
 * SSAM_CPLT_WQ_BATCH - Maximum number of event item completions executed per
 * work execution. Used to prevent livelocking of the workqueue. Value chosen
 * via educated guess, may be adjusted.
 */
#define SSAM_CPLT_WQ_BATCH	10

/*
 * SSAM_EVENT_ITEM_CACHE_PAYLOAD_LEN - Maximum payload length for a cached
 * &struct ssam_event_item.
 *
 * This length has been chosen to be accommodate standard touchpad and
 * keyboard input events. Events with larger payloads will be allocated
 * separately.
 */
#define SSAM_EVENT_ITEM_CACHE_PAYLOAD_LEN	32

static struct kmem_cache *ssam_event_item_cache;

/**
 * ssam_event_item_cache_init() - Initialize the event item cache.
 */
int ssam_event_item_cache_init(void)
{
	const unsigned int size = sizeof(struct ssam_event_item)
				  + SSAM_EVENT_ITEM_CACHE_PAYLOAD_LEN;
	const unsigned int align = __alignof__(struct ssam_event_item);
	struct kmem_cache *cache;

	cache = kmem_cache_create("ssam_event_item", size, align, 0, NULL);
	if (!cache)
		return -ENOMEM;

	ssam_event_item_cache = cache;
	return 0;
}

/**
 * ssam_event_item_cache_destroy() - Deinitialize the event item cache.
 */
void ssam_event_item_cache_destroy(void)
{
	kmem_cache_destroy(ssam_event_item_cache);
	ssam_event_item_cache = NULL;
}

static void __ssam_event_item_free_cached(struct ssam_event_item *item)
{
	kmem_cache_free(ssam_event_item_cache, item);
}

static void __ssam_event_item_free_generic(struct ssam_event_item *item)
{
	kfree(item);
}

/**
 * ssam_event_item_free() - Free the provided event item.
 * @item: The event item to free.
 */
static void ssam_event_item_free(struct ssam_event_item *item)
{
	trace_ssam_event_item_free(item);
	item->ops.free(item);
}

/**
 * ssam_event_item_alloc() - Allocate an event item with the given payload size.
 * @len:   The event payload length.
 * @flags: The flags used for allocation.
 *
 * Allocate an event item with the given payload size, preferring allocation
 * from the event item cache if the payload is small enough (i.e. smaller than
 * %SSAM_EVENT_ITEM_CACHE_PAYLOAD_LEN). Sets the item operations and payload
 * length values. The item free callback (``ops.free``) should not be
 * overwritten after this call.
 *
 * Return: Returns the newly allocated event item.
 */
static struct ssam_event_item *ssam_event_item_alloc(size_t len, gfp_t flags)
{
	struct ssam_event_item *item;

	if (len <= SSAM_EVENT_ITEM_CACHE_PAYLOAD_LEN) {
		item = kmem_cache_alloc(ssam_event_item_cache, flags);
		if (!item)
			return NULL;

		item->ops.free = __ssam_event_item_free_cached;
	} else {
		item = kzalloc(struct_size(item, event.data, len), flags);
		if (!item)
			return NULL;

		item->ops.free = __ssam_event_item_free_generic;
	}

	item->event.length = len;

	trace_ssam_event_item_alloc(item, len);
	return item;
}

/**
 * ssam_event_queue_push() - Push an event item to the event queue.
 * @q:    The event queue.
 * @item: The item to add.
 */
static void ssam_event_queue_push(struct ssam_event_queue *q,
				  struct ssam_event_item *item)
{
	spin_lock(&q->lock);
	list_add_tail(&item->node, &q->head);
	spin_unlock(&q->lock);
}

/**
 * ssam_event_queue_pop() - Pop the next event item from the event queue.
 * @q: The event queue.
 *
 * Returns and removes the next event item from the queue. Returns %NULL If
 * there is no event item left.
 */
static struct ssam_event_item *ssam_event_queue_pop(struct ssam_event_queue *q)
{
	struct ssam_event_item *item;

	spin_lock(&q->lock);
	item = list_first_entry_or_null(&q->head, struct ssam_event_item, node);
	if (item)
		list_del(&item->node);
	spin_unlock(&q->lock);

	return item;
}

/**
 * ssam_event_queue_is_empty() - Check if the event queue is empty.
 * @q: The event queue.
 */
static bool ssam_event_queue_is_empty(struct ssam_event_queue *q)
{
	bool empty;

	spin_lock(&q->lock);
	empty = list_empty(&q->head);
	spin_unlock(&q->lock);

	return empty;
}

/**
 * ssam_cplt_get_event_queue() - Get the event queue for the given parameters.
 * @cplt: The completion system on which to look for the queue.
 * @tid:  The target ID of the queue.
 * @rqid: The request ID representing the event ID for which to get the queue.
 *
 * Return: Returns the event queue corresponding to the event type described
 * by the given parameters. If the request ID does not represent an event,
 * this function returns %NULL. If the target ID is not supported, this
 * function will fall back to the default target ID (``tid = 1``).
 */
static
struct ssam_event_queue *ssam_cplt_get_event_queue(struct ssam_cplt *cplt,
						   u8 tid, u16 rqid)
{
	u16 event = ssh_rqid_to_event(rqid);
	u16 tidx = ssh_tid_to_index(tid);

	if (!ssh_rqid_is_event(rqid)) {
		dev_err(cplt->dev, "event: unsupported request ID: %#06x\n", rqid);
		return NULL;
	}

	if (!ssh_tid_is_valid(tid)) {
		dev_warn(cplt->dev, "event: unsupported target ID: %u\n", tid);
		tidx = 0;
	}

	return &cplt->event.target[tidx].queue[event];
}

/**
 * ssam_cplt_submit() - Submit a work item to the completion system workqueue.
 * @cplt: The completion system.
 * @work: The work item to submit.
 */
static bool ssam_cplt_submit(struct ssam_cplt *cplt, struct work_struct *work)
{
	return queue_work(cplt->wq, work);
}

/**
 * ssam_cplt_submit_event() - Submit an event to the completion system.
 * @cplt: The completion system.
 * @item: The event item to submit.
 *
 * Submits the event to the completion system by queuing it on the event item
 * queue and queuing the respective event queue work item on the completion
 * workqueue, which will eventually complete the event.
 *
 * Return: Returns zero on success, %-EINVAL if there is no event queue that
 * can handle the given event item.
 */
static int ssam_cplt_submit_event(struct ssam_cplt *cplt,
				  struct ssam_event_item *item)
{
	struct ssam_event_queue *evq;

	evq = ssam_cplt_get_event_queue(cplt, item->event.target_id, item->rqid);
	if (!evq)
		return -EINVAL;

	ssam_event_queue_push(evq, item);
	ssam_cplt_submit(cplt, &evq->work);
	return 0;
}

/**
 * ssam_cplt_flush() - Flush the completion system.
 * @cplt: The completion system.
 *
 * Flush the completion system by waiting until all currently submitted work
 * items have been completed.
 *
 * Note: This function does not guarantee that all events will have been
 * handled once this call terminates. In case of a larger number of
 * to-be-completed events, the event queue work function may re-schedule its
 * work item, which this flush operation will ignore.
 *
 * This operation is only intended to, during normal operation prior to
 * shutdown, try to complete most events and requests to get them out of the
 * system while the system is still fully operational. It does not aim to
 * provide any guarantee that all of them have been handled.
 */
static void ssam_cplt_flush(struct ssam_cplt *cplt)
{
	flush_workqueue(cplt->wq);
}

static void ssam_event_queue_work_fn(struct work_struct *work)
{
	struct ssam_event_queue *queue;
	struct ssam_event_item *item;
	struct ssam_nf *nf;
	struct device *dev;
	unsigned int iterations = SSAM_CPLT_WQ_BATCH;

	queue = container_of(work, struct ssam_event_queue, work);
	nf = &queue->cplt->event.notif;
	dev = queue->cplt->dev;

	/* Limit number of processed events to avoid livelocking. */
	do {
		item = ssam_event_queue_pop(queue);
		if (!item)
			return;

		ssam_nf_call(nf, dev, item->rqid, &item->event);
		ssam_event_item_free(item);
	} while (--iterations);

	if (!ssam_event_queue_is_empty(queue))
		ssam_cplt_submit(queue->cplt, &queue->work);
}

/**
 * ssam_event_queue_init() - Initialize an event queue.
 * @cplt: The completion system on which the queue resides.
 * @evq:  The event queue to initialize.
 */
static void ssam_event_queue_init(struct ssam_cplt *cplt,
				  struct ssam_event_queue *evq)
{
	evq->cplt = cplt;
	spin_lock_init(&evq->lock);
	INIT_LIST_HEAD(&evq->head);
	INIT_WORK(&evq->work, ssam_event_queue_work_fn);
}

/**
 * ssam_cplt_init() - Initialize completion system.
 * @cplt: The completion system to initialize.
 * @dev:  The device used for logging.
 */
static int ssam_cplt_init(struct ssam_cplt *cplt, struct device *dev)
{
	struct ssam_event_target *target;
	int status, c, i;

	cplt->dev = dev;

	cplt->wq = alloc_workqueue(SSAM_CPLT_WQ_NAME, WQ_UNBOUND | WQ_MEM_RECLAIM, 0);
	if (!cplt->wq)
		return -ENOMEM;

	for (c = 0; c < ARRAY_SIZE(cplt->event.target); c++) {
		target = &cplt->event.target[c];

		for (i = 0; i < ARRAY_SIZE(target->queue); i++)
			ssam_event_queue_init(cplt, &target->queue[i]);
	}

	status = ssam_nf_init(&cplt->event.notif);
	if (status)
		destroy_workqueue(cplt->wq);

	return status;
}

/**
 * ssam_cplt_destroy() - Deinitialize the completion system.
 * @cplt: The completion system to deinitialize.
 *
 * Deinitialize the given completion system and ensure that all pending, i.e.
 * yet-to-be-completed, event items and requests have been handled.
 */
static void ssam_cplt_destroy(struct ssam_cplt *cplt)
{
	/*
	 * Note: destroy_workqueue ensures that all currently queued work will
	 * be fully completed and the workqueue drained. This means that this
	 * call will inherently also free any queued ssam_event_items, thus we
	 * don't have to take care of that here explicitly.
	 */
	destroy_workqueue(cplt->wq);
	ssam_nf_destroy(&cplt->event.notif);
}


/* -- Main SSAM device structures. ------------------------------------------ */

/**
 * ssam_controller_device() - Get the &struct device associated with this
 * controller.
 * @c: The controller for which to get the device.
 *
 * Return: Returns the &struct device associated with this controller,
 * providing its lower-level transport.
 */
struct device *ssam_controller_device(struct ssam_controller *c)
{
	return ssh_rtl_get_device(&c->rtl);
}
EXPORT_SYMBOL_GPL(ssam_controller_device);

static void __ssam_controller_release(struct kref *kref)
{
	struct ssam_controller *ctrl = to_ssam_controller(kref, kref);

	/*
	 * The lock-call here is to satisfy lockdep. At this point we really
	 * expect this to be the last remaining reference to the controller.
	 * Anything else is a bug.
	 */
	ssam_controller_lock(ctrl);
	ssam_controller_destroy(ctrl);
	ssam_controller_unlock(ctrl);

	kfree(ctrl);
}

/**
 * ssam_controller_get() - Increment reference count of controller.
 * @c: The controller.
 *
 * Return: Returns the controller provided as input.
 */
struct ssam_controller *ssam_controller_get(struct ssam_controller *c)
{
	if (c)
		kref_get(&c->kref);
	return c;
}
EXPORT_SYMBOL_GPL(ssam_controller_get);

/**
 * ssam_controller_put() - Decrement reference count of controller.
 * @c: The controller.
 */
void ssam_controller_put(struct ssam_controller *c)
{
	if (c)
		kref_put(&c->kref, __ssam_controller_release);
}
EXPORT_SYMBOL_GPL(ssam_controller_put);

/**
 * ssam_controller_statelock() - Lock the controller against state transitions.
 * @c: The controller to lock.
 *
 * Lock the controller against state transitions. Holding this lock guarantees
 * that the controller will not transition between states, i.e. if the
 * controller is in state "started", when this lock has been acquired, it will
 * remain in this state at least until the lock has been released.
 *
 * Multiple clients may concurrently hold this lock. In other words: The
 * ``statelock`` functions represent the read-lock part of a r/w-semaphore.
 * Actions causing state transitions of the controller must be executed while
 * holding the write-part of this r/w-semaphore (see ssam_controller_lock()
 * and ssam_controller_unlock() for that).
 *
 * See ssam_controller_stateunlock() for the corresponding unlock function.
 */
void ssam_controller_statelock(struct ssam_controller *c)
{
	down_read(&c->lock);
}
EXPORT_SYMBOL_GPL(ssam_controller_statelock);

/**
 * ssam_controller_stateunlock() - Unlock controller state transitions.
 * @c: The controller to unlock.
 *
 * See ssam_controller_statelock() for the corresponding lock function.
 */
void ssam_controller_stateunlock(struct ssam_controller *c)
{
	up_read(&c->lock);
}
EXPORT_SYMBOL_GPL(ssam_controller_stateunlock);

/**
 * ssam_controller_lock() - Acquire the main controller lock.
 * @c: The controller to lock.
 *
 * This lock must be held for any state transitions, including transition to
 * suspend/resumed states and during shutdown. See ssam_controller_statelock()
 * for more details on controller locking.
 *
 * See ssam_controller_unlock() for the corresponding unlock function.
 */
void ssam_controller_lock(struct ssam_controller *c)
{
	down_write(&c->lock);
}

/*
 * ssam_controller_unlock() - Release the main controller lock.
 * @c: The controller to unlock.
 *
 * See ssam_controller_lock() for the corresponding lock function.
 */
void ssam_controller_unlock(struct ssam_controller *c)
{
	up_write(&c->lock);
}

static void ssam_handle_event(struct ssh_rtl *rtl,
			      const struct ssh_command *cmd,
			      const struct ssam_span *data)
{
	struct ssam_controller *ctrl = to_ssam_controller(rtl, rtl);
	struct ssam_event_item *item;

	item = ssam_event_item_alloc(data->len, GFP_KERNEL);
	if (!item)
		return;

	item->rqid = get_unaligned_le16(&cmd->rqid);
	item->event.target_category = cmd->tc;
	item->event.target_id = cmd->tid_in;
	item->event.command_id = cmd->cid;
	item->event.instance_id = cmd->iid;
	memcpy(&item->event.data[0], data->ptr, data->len);

	if (WARN_ON(ssam_cplt_submit_event(&ctrl->cplt, item)))
		ssam_event_item_free(item);
}

static const struct ssh_rtl_ops ssam_rtl_ops = {
	.handle_event = ssam_handle_event,
};

static bool ssam_notifier_is_empty(struct ssam_controller *ctrl);
static void ssam_notifier_unregister_all(struct ssam_controller *ctrl);

#define SSAM_SSH_DSM_REVISION	0

/* d5e383e1-d892-4a76-89fc-f6aaae7ed5b5 */
static const guid_t SSAM_SSH_DSM_GUID =
	GUID_INIT(0xd5e383e1, 0xd892, 0x4a76,
		  0x89, 0xfc, 0xf6, 0xaa, 0xae, 0x7e, 0xd5, 0xb5);

enum ssh_dsm_fn {
	SSH_DSM_FN_SSH_POWER_PROFILE             = 0x05,
	SSH_DSM_FN_SCREEN_ON_SLEEP_IDLE_TIMEOUT  = 0x06,
	SSH_DSM_FN_SCREEN_OFF_SLEEP_IDLE_TIMEOUT = 0x07,
	SSH_DSM_FN_D3_CLOSES_HANDLE              = 0x08,
	SSH_DSM_FN_SSH_BUFFER_SIZE               = 0x09,
};

static int ssam_dsm_get_functions(acpi_handle handle, u64 *funcs)
{
	union acpi_object *obj;
	u64 mask = 0;
	int i;

	*funcs = 0;

	/*
	 * The _DSM function is only present on newer models. It is not
	 * present on 5th and 6th generation devices (i.e. up to and including
	 * Surface Pro 6, Surface Laptop 2, Surface Book 2).
	 *
	 * If the _DSM is not present, indicate that no function is supported.
	 * This will result in default values being set.
	 */
	if (!acpi_has_method(handle, "_DSM"))
		return 0;

	obj = acpi_evaluate_dsm_typed(handle, &SSAM_SSH_DSM_GUID,
				      SSAM_SSH_DSM_REVISION, 0, NULL,
				      ACPI_TYPE_BUFFER);
	if (!obj)
		return -EIO;

	for (i = 0; i < obj->buffer.length && i < 8; i++)
		mask |= (((u64)obj->buffer.pointer[i]) << (i * 8));

	if (mask & BIT(0))
		*funcs = mask;

	ACPI_FREE(obj);
	return 0;
}

static int ssam_dsm_load_u32(acpi_handle handle, u64 funcs, u64 func, u32 *ret)
{
	union acpi_object *obj;
	u64 val;

	if (!(funcs & BIT_ULL(func)))
		return 0; /* Not supported, leave *ret at its default value */

	obj = acpi_evaluate_dsm_typed(handle, &SSAM_SSH_DSM_GUID,
				      SSAM_SSH_DSM_REVISION, func, NULL,
				      ACPI_TYPE_INTEGER);
	if (!obj)
		return -EIO;

	val = obj->integer.value;
	ACPI_FREE(obj);

	if (val > U32_MAX)
		return -ERANGE;

	*ret = val;
	return 0;
}

/**
 * ssam_controller_caps_load_from_acpi() - Load controller capabilities from
 * ACPI _DSM.
 * @handle: The handle of the ACPI controller/SSH device.
 * @caps:   Where to store the capabilities in.
 *
 * Initializes the given controller capabilities with default values, then
 * checks and, if the respective _DSM functions are available, loads the
 * actual capabilities from the _DSM.
 *
 * Return: Returns zero on success, a negative error code on failure.
 */
static
int ssam_controller_caps_load_from_acpi(acpi_handle handle,
					struct ssam_controller_caps *caps)
{
	u32 d3_closes_handle = false;
	u64 funcs;
	int status;

	/* Set defaults. */
	caps->ssh_power_profile = U32_MAX;
	caps->screen_on_sleep_idle_timeout = U32_MAX;
	caps->screen_off_sleep_idle_timeout = U32_MAX;
	caps->d3_closes_handle = false;
	caps->ssh_buffer_size = U32_MAX;

	/* Pre-load supported DSM functions. */
	status = ssam_dsm_get_functions(handle, &funcs);
	if (status)
		return status;

	/* Load actual values from ACPI, if present. */
	status = ssam_dsm_load_u32(handle, funcs, SSH_DSM_FN_SSH_POWER_PROFILE,
				   &caps->ssh_power_profile);
	if (status)
		return status;

	status = ssam_dsm_load_u32(handle, funcs,
				   SSH_DSM_FN_SCREEN_ON_SLEEP_IDLE_TIMEOUT,
				   &caps->screen_on_sleep_idle_timeout);
	if (status)
		return status;

	status = ssam_dsm_load_u32(handle, funcs,
				   SSH_DSM_FN_SCREEN_OFF_SLEEP_IDLE_TIMEOUT,
				   &caps->screen_off_sleep_idle_timeout);
	if (status)
		return status;

	status = ssam_dsm_load_u32(handle, funcs, SSH_DSM_FN_D3_CLOSES_HANDLE,
				   &d3_closes_handle);
	if (status)
		return status;

	caps->d3_closes_handle = !!d3_closes_handle;

	status = ssam_dsm_load_u32(handle, funcs, SSH_DSM_FN_SSH_BUFFER_SIZE,
				   &caps->ssh_buffer_size);
	if (status)
		return status;

	return 0;
}

/**
 * ssam_controller_init() - Initialize SSAM controller.
 * @ctrl:   The controller to initialize.
 * @serdev: The serial device representing the underlying data transport.
 *
 * Initializes the given controller. Does neither start receiver nor
 * transmitter threads. After this call, the controller has to be hooked up to
 * the serdev core separately via &struct serdev_device_ops, relaying calls to
 * ssam_controller_receive_buf() and ssam_controller_write_wakeup(). Once the
 * controller has been hooked up, transmitter and receiver threads may be
 * started via ssam_controller_start(). These setup steps need to be completed
 * before controller can be used for requests.
 */
int ssam_controller_init(struct ssam_controller *ctrl,
			 struct serdev_device *serdev)
{
	acpi_handle handle = ACPI_HANDLE(&serdev->dev);
	int status;

	init_rwsem(&ctrl->lock);
	kref_init(&ctrl->kref);

	status = ssam_controller_caps_load_from_acpi(handle, &ctrl->caps);
	if (status)
		return status;

	dev_dbg(&serdev->dev,
		"device capabilities:\n"
		"  ssh_power_profile:             %u\n"
		"  ssh_buffer_size:               %u\n"
		"  screen_on_sleep_idle_timeout:  %u\n"
		"  screen_off_sleep_idle_timeout: %u\n"
		"  d3_closes_handle:              %u\n",
		ctrl->caps.ssh_power_profile,
		ctrl->caps.ssh_buffer_size,
		ctrl->caps.screen_on_sleep_idle_timeout,
		ctrl->caps.screen_off_sleep_idle_timeout,
		ctrl->caps.d3_closes_handle);

	ssh_seq_reset(&ctrl->counter.seq);
	ssh_rqid_reset(&ctrl->counter.rqid);

	/* Initialize event/request completion system. */
	status = ssam_cplt_init(&ctrl->cplt, &serdev->dev);
	if (status)
		return status;

	/* Initialize request and packet transport layers. */
	status = ssh_rtl_init(&ctrl->rtl, serdev, &ssam_rtl_ops);
	if (status) {
		ssam_cplt_destroy(&ctrl->cplt);
		return status;
	}

	/*
	 * Set state via write_once even though we expect to be in an
	 * exclusive context, due to smoke-testing in
	 * ssam_request_sync_submit().
	 */
	WRITE_ONCE(ctrl->state, SSAM_CONTROLLER_INITIALIZED);
	return 0;
}

/**
 * ssam_controller_start() - Start the receiver and transmitter threads of the
 * controller.
 * @ctrl: The controller.
 *
 * Note: When this function is called, the controller should be properly
 * hooked up to the serdev core via &struct serdev_device_ops. Please refer
 * to ssam_controller_init() for more details on controller initialization.
 *
 * This function must be called with the main controller lock held (i.e. by
 * calling ssam_controller_lock()).
 */
int ssam_controller_start(struct ssam_controller *ctrl)
{
	int status;

	lockdep_assert_held_write(&ctrl->lock);

	if (ctrl->state != SSAM_CONTROLLER_INITIALIZED)
		return -EINVAL;

	status = ssh_rtl_start(&ctrl->rtl);
	if (status)
		return status;

	/*
	 * Set state via write_once even though we expect to be locked/in an
	 * exclusive context, due to smoke-testing in
	 * ssam_request_sync_submit().
	 */
	WRITE_ONCE(ctrl->state, SSAM_CONTROLLER_STARTED);
	return 0;
}

/*
 * SSAM_CTRL_SHUTDOWN_FLUSH_TIMEOUT - Timeout for flushing requests during
 * shutdown.
 *
 * Chosen to be larger than one full request timeout, including packets timing
 * out. This value should give ample time to complete any outstanding requests
 * during normal operation and account for the odd package timeout.
 */
#define SSAM_CTRL_SHUTDOWN_FLUSH_TIMEOUT	msecs_to_jiffies(5000)

/**
 * ssam_controller_shutdown() - Shut down the controller.
 * @ctrl: The controller.
 *
 * Shuts down the controller by flushing all pending requests and stopping the
 * transmitter and receiver threads. All requests submitted after this call
 * will fail with %-ESHUTDOWN. While it is discouraged to do so, this function
 * is safe to use in parallel with ongoing request submission.
 *
 * In the course of this shutdown procedure, all currently registered
 * notifiers will be unregistered. It is, however, strongly recommended to not
 * rely on this behavior, and instead the party registering the notifier
 * should unregister it before the controller gets shut down, e.g. via the
 * SSAM bus which guarantees client devices to be removed before a shutdown.
 *
 * Note that events may still be pending after this call, but, due to the
 * notifiers being unregistered, these events will be dropped when the
 * controller is subsequently destroyed via ssam_controller_destroy().
 *
 * This function must be called with the main controller lock held (i.e. by
 * calling ssam_controller_lock()).
 */
void ssam_controller_shutdown(struct ssam_controller *ctrl)
{
	enum ssam_controller_state s = ctrl->state;
	int status;

	lockdep_assert_held_write(&ctrl->lock);

	if (s == SSAM_CONTROLLER_UNINITIALIZED || s == SSAM_CONTROLLER_STOPPED)
		return;

	/*
	 * Try to flush pending events and requests while everything still
	 * works. Note: There may still be packets and/or requests in the
	 * system after this call (e.g. via control packets submitted by the
	 * packet transport layer or flush timeout / failure, ...). Those will
	 * be handled with the ssh_rtl_shutdown() call below.
	 */
	status = ssh_rtl_flush(&ctrl->rtl, SSAM_CTRL_SHUTDOWN_FLUSH_TIMEOUT);
	if (status) {
		ssam_err(ctrl, "failed to flush request transport layer: %d\n",
			 status);
	}

	/* Try to flush all currently completing requests and events. */
	ssam_cplt_flush(&ctrl->cplt);

	/*
	 * We expect all notifiers to have been removed by the respective client
	 * driver that set them up at this point. If this warning occurs, some
	 * client driver has not done that...
	 */
	WARN_ON(!ssam_notifier_is_empty(ctrl));

	/*
	 * Nevertheless, we should still take care of drivers that don't behave
	 * well. Thus disable all enabled events, unregister all notifiers.
	 */
	ssam_notifier_unregister_all(ctrl);

	/*
	 * Cancel remaining requests. Ensure no new ones can be queued and stop
	 * threads.
	 */
	ssh_rtl_shutdown(&ctrl->rtl);

	/*
	 * Set state via write_once even though we expect to be locked/in an
	 * exclusive context, due to smoke-testing in
	 * ssam_request_sync_submit().
	 */
	WRITE_ONCE(ctrl->state, SSAM_CONTROLLER_STOPPED);
	ctrl->rtl.ptl.serdev = NULL;
}

/**
 * ssam_controller_destroy() - Destroy the controller and free its resources.
 * @ctrl: The controller.
 *
 * Ensures that all resources associated with the controller get freed. This
 * function should only be called after the controller has been stopped via
 * ssam_controller_shutdown(). In general, this function should not be called
 * directly. The only valid place to call this function directly is during
 * initialization, before the controller has been fully initialized and passed
 * to other processes. This function is called automatically when the
 * reference count of the controller reaches zero.
 *
 * This function must be called with the main controller lock held (i.e. by
 * calling ssam_controller_lock()).
 */
void ssam_controller_destroy(struct ssam_controller *ctrl)
{
	lockdep_assert_held_write(&ctrl->lock);

	if (ctrl->state == SSAM_CONTROLLER_UNINITIALIZED)
		return;

	WARN_ON(ctrl->state != SSAM_CONTROLLER_STOPPED);

	/*
	 * Note: New events could still have been received after the previous
	 * flush in ssam_controller_shutdown, before the request transport layer
	 * has been shut down. At this point, after the shutdown, we can be sure
	 * that no new events will be queued. The call to ssam_cplt_destroy will
	 * ensure that those remaining are being completed and freed.
	 */

	/* Actually free resources. */
	ssam_cplt_destroy(&ctrl->cplt);
	ssh_rtl_destroy(&ctrl->rtl);

	/*
	 * Set state via write_once even though we expect to be locked/in an
	 * exclusive context, due to smoke-testing in
	 * ssam_request_sync_submit().
	 */
	WRITE_ONCE(ctrl->state, SSAM_CONTROLLER_UNINITIALIZED);
}

/**
 * ssam_controller_suspend() - Suspend the controller.
 * @ctrl: The controller to suspend.
 *
 * Marks the controller as suspended. Note that display-off and D0-exit
 * notifications have to be sent manually before transitioning the controller
 * into the suspended state via this function.
 *
 * See ssam_controller_resume() for the corresponding resume function.
 *
 * Return: Returns %-EINVAL if the controller is currently not in the
 * "started" state.
 */
int ssam_controller_suspend(struct ssam_controller *ctrl)
{
	ssam_controller_lock(ctrl);

	if (ctrl->state != SSAM_CONTROLLER_STARTED) {
		ssam_controller_unlock(ctrl);
		return -EINVAL;
	}

	ssam_dbg(ctrl, "pm: suspending controller\n");

	/*
	 * Set state via write_once even though we're locked, due to
	 * smoke-testing in ssam_request_sync_submit().
	 */
	WRITE_ONCE(ctrl->state, SSAM_CONTROLLER_SUSPENDED);

	ssam_controller_unlock(ctrl);
	return 0;
}

/**
 * ssam_controller_resume() - Resume the controller from suspend.
 * @ctrl: The controller to resume.
 *
 * Resume the controller from the suspended state it was put into via
 * ssam_controller_suspend(). This function does not issue display-on and
 * D0-entry notifications. If required, those have to be sent manually after
 * this call.
 *
 * Return: Returns %-EINVAL if the controller is currently not suspended.
 */
int ssam_controller_resume(struct ssam_controller *ctrl)
{
	ssam_controller_lock(ctrl);

	if (ctrl->state != SSAM_CONTROLLER_SUSPENDED) {
		ssam_controller_unlock(ctrl);
		return -EINVAL;
	}

	ssam_dbg(ctrl, "pm: resuming controller\n");

	/*
	 * Set state via write_once even though we're locked, due to
	 * smoke-testing in ssam_request_sync_submit().
	 */
	WRITE_ONCE(ctrl->state, SSAM_CONTROLLER_STARTED);

	ssam_controller_unlock(ctrl);
	return 0;
}


/* -- Top-level request interface ------------------------------------------- */

/**
 * ssam_request_write_data() - Construct and write SAM request message to
 * buffer.
 * @buf:  The buffer to write the data to.
 * @ctrl: The controller via which the request will be sent.
 * @spec: The request data and specification.
 *
 * Constructs a SAM/SSH request message and writes it to the provided buffer.
 * The request and transport counters, specifically RQID and SEQ, will be set
 * in this call. These counters are obtained from the controller. It is thus
 * only valid to send the resulting message via the controller specified here.
 *
 * For calculation of the required buffer size, refer to the
 * SSH_COMMAND_MESSAGE_LENGTH() macro.
 *
 * Return: Returns the number of bytes used in the buffer on success. Returns
 * %-EINVAL if the payload length provided in the request specification is too
 * large (larger than %SSH_COMMAND_MAX_PAYLOAD_SIZE) or if the provided buffer
 * is too small.
 */
ssize_t ssam_request_write_data(struct ssam_span *buf,
				struct ssam_controller *ctrl,
				const struct ssam_request *spec)
{
	struct msgbuf msgb;
	u16 rqid;
	u8 seq;

	if (spec->length > SSH_COMMAND_MAX_PAYLOAD_SIZE)
		return -EINVAL;

	if (SSH_COMMAND_MESSAGE_LENGTH(spec->length) > buf->len)
		return -EINVAL;

	msgb_init(&msgb, buf->ptr, buf->len);
	seq = ssh_seq_next(&ctrl->counter.seq);
	rqid = ssh_rqid_next(&ctrl->counter.rqid);
	msgb_push_cmd(&msgb, seq, rqid, spec);

	return msgb_bytes_used(&msgb);
}
EXPORT_SYMBOL_GPL(ssam_request_write_data);

static void ssam_request_sync_complete(struct ssh_request *rqst,
				       const struct ssh_command *cmd,
				       const struct ssam_span *data, int status)
{
	struct ssh_rtl *rtl = ssh_request_rtl(rqst);
	struct ssam_request_sync *r;

	r = container_of(rqst, struct ssam_request_sync, base);
	r->status = status;

	if (r->resp)
		r->resp->length = 0;

	if (status) {
		rtl_dbg_cond(rtl, "rsp: request failed: %d\n", status);
		return;
	}

	if (!data)	/* Handle requests without a response. */
		return;

	if (!r->resp || !r->resp->pointer) {
		if (data->len)
			rtl_warn(rtl, "rsp: no response buffer provided, dropping data\n");
		return;
	}

	if (data->len > r->resp->capacity) {
		rtl_err(rtl,
			"rsp: response buffer too small, capacity: %zu bytes, got: %zu bytes\n",
			r->resp->capacity, data->len);
		r->status = -ENOSPC;
		return;
	}

	r->resp->length = data->len;
	memcpy(r->resp->pointer, data->ptr, data->len);
}

static void ssam_request_sync_release(struct ssh_request *rqst)
{
	complete_all(&container_of(rqst, struct ssam_request_sync, base)->comp);
}

static const struct ssh_request_ops ssam_request_sync_ops = {
	.release = ssam_request_sync_release,
	.complete = ssam_request_sync_complete,
};

/**
 * ssam_request_sync_alloc() - Allocate a synchronous request.
 * @payload_len: The length of the request payload.
 * @flags:       Flags used for allocation.
 * @rqst:        Where to store the pointer to the allocated request.
 * @buffer:      Where to store the buffer descriptor for the message buffer of
 *               the request.
 *
 * Allocates a synchronous request with corresponding message buffer. The
 * request still needs to be initialized ssam_request_sync_init() before
 * it can be submitted, and the message buffer data must still be set to the
 * returned buffer via ssam_request_sync_set_data() after it has been filled,
 * if need be with adjusted message length.
 *
 * After use, the request and its corresponding message buffer should be freed
 * via ssam_request_sync_free(). The buffer must not be freed separately.
 *
 * Return: Returns zero on success, %-ENOMEM if the request could not be
 * allocated.
 */
int ssam_request_sync_alloc(size_t payload_len, gfp_t flags,
			    struct ssam_request_sync **rqst,
			    struct ssam_span *buffer)
{
	size_t msglen = SSH_COMMAND_MESSAGE_LENGTH(payload_len);

	*rqst = kzalloc(sizeof(**rqst) + msglen, flags);
	if (!*rqst)
		return -ENOMEM;

	buffer->ptr = (u8 *)(*rqst + 1);
	buffer->len = msglen;

	return 0;
}
EXPORT_SYMBOL_GPL(ssam_request_sync_alloc);

/**
 * ssam_request_sync_free() - Free a synchronous request.
 * @rqst: The request to be freed.
 *
 * Free a synchronous request and its corresponding buffer allocated with
 * ssam_request_sync_alloc(). Do not use for requests allocated on the stack
 * or via any other function.
 *
 * Warning: The caller must ensure that the request is not in use any more.
 * I.e. the caller must ensure that it has the only reference to the request
 * and the request is not currently pending. This means that the caller has
 * either never submitted the request, request submission has failed, or the
 * caller has waited until the submitted request has been completed via
 * ssam_request_sync_wait().
 */
void ssam_request_sync_free(struct ssam_request_sync *rqst)
{
	kfree(rqst);
}
EXPORT_SYMBOL_GPL(ssam_request_sync_free);

/**
 * ssam_request_sync_init() - Initialize a synchronous request struct.
 * @rqst:  The request to initialize.
 * @flags: The request flags.
 *
 * Initializes the given request struct. Does not initialize the request
 * message data. This has to be done explicitly after this call via
 * ssam_request_sync_set_data() and the actual message data has to be written
 * via ssam_request_write_data().
 *
 * Return: Returns zero on success or %-EINVAL if the given flags are invalid.
 */
int ssam_request_sync_init(struct ssam_request_sync *rqst,
			   enum ssam_request_flags flags)
{
	int status;

	status = ssh_request_init(&rqst->base, flags, &ssam_request_sync_ops);
	if (status)
		return status;

	init_completion(&rqst->comp);
	rqst->resp = NULL;
	rqst->status = 0;

	return 0;
}
EXPORT_SYMBOL_GPL(ssam_request_sync_init);

/**
 * ssam_request_sync_submit() - Submit a synchronous request.
 * @ctrl: The controller with which to submit the request.
 * @rqst: The request to submit.
 *
 * Submit a synchronous request. The request has to be initialized and
 * properly set up, including response buffer (may be %NULL if no response is
 * expected) and command message data. This function does not wait for the
 * request to be completed.
 *
 * If this function succeeds, ssam_request_sync_wait() must be used to ensure
 * that the request has been completed before the response data can be
 * accessed and/or the request can be freed. On failure, the request may
 * immediately be freed.
 *
 * This function may only be used if the controller is active, i.e. has been
 * initialized and not suspended.
 */
int ssam_request_sync_submit(struct ssam_controller *ctrl,
			     struct ssam_request_sync *rqst)
{
	int status;

	/*
	 * This is only a superficial check. In general, the caller needs to
	 * ensure that the controller is initialized and is not (and does not
	 * get) suspended during use, i.e. until the request has been completed
	 * (if _absolutely_ necessary, by use of ssam_controller_statelock/
	 * ssam_controller_stateunlock, but something like ssam_client_link
	 * should be preferred as this needs to last until the request has been
	 * completed).
	 *
	 * Note that it is actually safe to use this function while the
	 * controller is in the process of being shut down (as ssh_rtl_submit
	 * is safe with regards to this), but it is generally discouraged to do
	 * so.
	 */
	if (WARN_ON(READ_ONCE(ctrl->state) != SSAM_CONTROLLER_STARTED)) {
		ssh_request_put(&rqst->base);
		return -ENODEV;
	}

	status = ssh_rtl_submit(&ctrl->rtl, &rqst->base);
	ssh_request_put(&rqst->base);

	return status;
}
EXPORT_SYMBOL_GPL(ssam_request_sync_submit);

/**
 * ssam_request_sync() - Execute a synchronous request.
 * @ctrl: The controller via which the request will be submitted.
 * @spec: The request specification and payload.
 * @rsp:  The response buffer.
 *
 * Allocates a synchronous request with its message data buffer on the heap
 * via ssam_request_sync_alloc(), fully initializes it via the provided
 * request specification, submits it, and finally waits for its completion
 * before freeing it and returning its status.
 *
 * Return: Returns the status of the request or any failure during setup.
 */
int ssam_request_sync(struct ssam_controller *ctrl,
		      const struct ssam_request *spec,
		      struct ssam_response *rsp)
{
	struct ssam_request_sync *rqst;
	struct ssam_span buf;
	ssize_t len;
	int status;

	status = ssam_request_sync_alloc(spec->length, GFP_KERNEL, &rqst, &buf);
	if (status)
		return status;

	status = ssam_request_sync_init(rqst, spec->flags);
	if (status) {
		ssam_request_sync_free(rqst);
		return status;
	}

	ssam_request_sync_set_resp(rqst, rsp);

	len = ssam_request_write_data(&buf, ctrl, spec);
	if (len < 0) {
		ssam_request_sync_free(rqst);
		return len;
	}

	ssam_request_sync_set_data(rqst, buf.ptr, len);

	status = ssam_request_sync_submit(ctrl, rqst);
	if (!status)
		status = ssam_request_sync_wait(rqst);

	ssam_request_sync_free(rqst);
	return status;
}
EXPORT_SYMBOL_GPL(ssam_request_sync);

/**
 * ssam_request_sync_with_buffer() - Execute a synchronous request with the
 * provided buffer as back-end for the message buffer.
 * @ctrl: The controller via which the request will be submitted.
 * @spec: The request specification and payload.
 * @rsp:  The response buffer.
 * @buf:  The buffer for the request message data.
 *
 * Allocates a synchronous request struct on the stack, fully initializes it
 * using the provided buffer as message data buffer, submits it, and then
 * waits for its completion before returning its status. The
 * SSH_COMMAND_MESSAGE_LENGTH() macro can be used to compute the required
 * message buffer size.
 *
 * This function does essentially the same as ssam_request_sync(), but instead
 * of dynamically allocating the request and message data buffer, it uses the
 * provided message data buffer and stores the (small) request struct on the
 * heap.
 *
 * Return: Returns the status of the request or any failure during setup.
 */
int ssam_request_sync_with_buffer(struct ssam_controller *ctrl,
				  const struct ssam_request *spec,
				  struct ssam_response *rsp,
				  struct ssam_span *buf)
{
	struct ssam_request_sync rqst;
	ssize_t len;
	int status;

	status = ssam_request_sync_init(&rqst, spec->flags);
	if (status)
		return status;

	ssam_request_sync_set_resp(&rqst, rsp);

	len = ssam_request_write_data(buf, ctrl, spec);
	if (len < 0)
		return len;

	ssam_request_sync_set_data(&rqst, buf->ptr, len);

	status = ssam_request_sync_submit(ctrl, &rqst);
	if (!status)
		status = ssam_request_sync_wait(&rqst);

	return status;
}
EXPORT_SYMBOL_GPL(ssam_request_sync_with_buffer);


/* -- Internal SAM requests. ------------------------------------------------ */

SSAM_DEFINE_SYNC_REQUEST_R(ssam_ssh_get_firmware_version, __le32, {
	.target_category = SSAM_SSH_TC_SAM,
	.target_id       = 0x01,
	.command_id      = 0x13,
	.instance_id     = 0x00,
});

SSAM_DEFINE_SYNC_REQUEST_R(ssam_ssh_notif_display_off, u8, {
	.target_category = SSAM_SSH_TC_SAM,
	.target_id       = 0x01,
	.command_id      = 0x15,
	.instance_id     = 0x00,
});

SSAM_DEFINE_SYNC_REQUEST_R(ssam_ssh_notif_display_on, u8, {
	.target_category = SSAM_SSH_TC_SAM,
	.target_id       = 0x01,
	.command_id      = 0x16,
	.instance_id     = 0x00,
});

SSAM_DEFINE_SYNC_REQUEST_R(ssam_ssh_notif_d0_exit, u8, {
	.target_category = SSAM_SSH_TC_SAM,
	.target_id       = 0x01,
	.command_id      = 0x33,
	.instance_id     = 0x00,
});

SSAM_DEFINE_SYNC_REQUEST_R(ssam_ssh_notif_d0_entry, u8, {
	.target_category = SSAM_SSH_TC_SAM,
	.target_id       = 0x01,
	.command_id      = 0x34,
	.instance_id     = 0x00,
});

/**
 * struct ssh_notification_params - Command payload to enable/disable SSH
 * notifications.
 * @target_category: The target category for which notifications should be
 *                   enabled/disabled.
 * @flags:           Flags determining how notifications are being sent.
 * @request_id:      The request ID that is used to send these notifications.
 * @instance_id:     The specific instance in the given target category for
 *                   which notifications should be enabled.
 */
struct ssh_notification_params {
	u8 target_category;
	u8 flags;
	__le16 request_id;
	u8 instance_id;
} __packed;

static_assert(sizeof(struct ssh_notification_params) == 5);

static int __ssam_ssh_event_request(struct ssam_controller *ctrl,
				    struct ssam_event_registry reg, u8 cid,
				    struct ssam_event_id id, u8 flags)
{
	struct ssh_notification_params params;
	struct ssam_request rqst;
	struct ssam_response result;
	int status;

	u16 rqid = ssh_tc_to_rqid(id.target_category);
	u8 buf = 0;

	/* Only allow RQIDs that lie within the event spectrum. */
	if (!ssh_rqid_is_event(rqid))
		return -EINVAL;

	params.target_category = id.target_category;
	params.instance_id = id.instance;
	params.flags = flags;
	put_unaligned_le16(rqid, &params.request_id);

	rqst.target_category = reg.target_category;
	rqst.target_id = reg.target_id;
	rqst.command_id = cid;
	rqst.instance_id = 0x00;
	rqst.flags = SSAM_REQUEST_HAS_RESPONSE;
	rqst.length = sizeof(params);
	rqst.payload = (u8 *)&params;

	result.capacity = sizeof(buf);
	result.length = 0;
	result.pointer = &buf;

	status = ssam_retry(ssam_request_sync_onstack, ctrl, &rqst, &result,
			    sizeof(params));

	return status < 0 ? status : buf;
}

/**
 * ssam_ssh_event_enable() - Enable SSH event.
 * @ctrl:  The controller for which to enable the event.
 * @reg:   The event registry describing what request to use for enabling and
 *         disabling the event.
 * @id:    The event identifier.
 * @flags: The event flags.
 *
 * Enables the specified event on the EC. This function does not manage
 * reference counting of enabled events and is basically only a wrapper for
 * the raw EC request. If the specified event is already enabled, the EC will
 * ignore this request.
 *
 * Return: Returns the status of the executed SAM request (zero on success and
 * negative on direct failure) or %-EPROTO if the request response indicates a
 * failure.
 */
static int ssam_ssh_event_enable(struct ssam_controller *ctrl,
				 struct ssam_event_registry reg,
				 struct ssam_event_id id, u8 flags)
{
	int status;

	status = __ssam_ssh_event_request(ctrl, reg, reg.cid_enable, id, flags);

	if (status < 0 && status != -EINVAL) {
		ssam_err(ctrl,
			 "failed to enable event source (tc: %#04x, iid: %#04x, reg: %#04x)\n",
			 id.target_category, id.instance, reg.target_category);
	}

	if (status > 0) {
		ssam_err(ctrl,
			 "unexpected result while enabling event source: %#04x (tc: %#04x, iid: %#04x, reg: %#04x)\n",
			 status, id.target_category, id.instance, reg.target_category);
		return -EPROTO;
	}

	return status;
}

/**
 * ssam_ssh_event_disable() - Disable SSH event.
 * @ctrl:  The controller for which to disable the event.
 * @reg:   The event registry describing what request to use for enabling and
 *         disabling the event (must be same as used when enabling the event).
 * @id:    The event identifier.
 * @flags: The event flags (likely ignored for disabling of events).
 *
 * Disables the specified event on the EC. This function does not manage
 * reference counting of enabled events and is basically only a wrapper for
 * the raw EC request. If the specified event is already disabled, the EC will
 * ignore this request.
 *
 * Return: Returns the status of the executed SAM request (zero on success and
 * negative on direct failure) or %-EPROTO if the request response indicates a
 * failure.
 */
static int ssam_ssh_event_disable(struct ssam_controller *ctrl,
				  struct ssam_event_registry reg,
				  struct ssam_event_id id, u8 flags)
{
	int status;

	status = __ssam_ssh_event_request(ctrl, reg, reg.cid_disable, id, flags);

	if (status < 0 && status != -EINVAL) {
		ssam_err(ctrl,
			 "failed to disable event source (tc: %#04x, iid: %#04x, reg: %#04x)\n",
			 id.target_category, id.instance, reg.target_category);
	}

	if (status > 0) {
		ssam_err(ctrl,
			 "unexpected result while disabling event source: %#04x (tc: %#04x, iid: %#04x, reg: %#04x)\n",
			 status, id.target_category, id.instance, reg.target_category);
		return -EPROTO;
	}

	return status;
}


/* -- Wrappers for internal SAM requests. ----------------------------------- */

/**
 * ssam_get_firmware_version() - Get the SAM/EC firmware version.
 * @ctrl:    The controller.
 * @version: Where to store the version number.
 *
 * Return: Returns zero on success or the status of the executed SAM request
 * if that request failed.
 */
int ssam_get_firmware_version(struct ssam_controller *ctrl, u32 *version)
{
	__le32 __version;
	int status;

	status = ssam_retry(ssam_ssh_get_firmware_version, ctrl, &__version);
	if (status)
		return status;

	*version = le32_to_cpu(__version);
	return 0;
}

/**
 * ssam_ctrl_notif_display_off() - Notify EC that the display has been turned
 * off.
 * @ctrl: The controller.
 *
 * Notify the EC that the display has been turned off and the driver may enter
 * a lower-power state. This will prevent events from being sent directly.
 * Rather, the EC signals an event by pulling the wakeup GPIO high for as long
 * as there are pending events. The events then need to be manually released,
 * one by one, via the GPIO callback request. All pending events accumulated
 * during this state can also be released by issuing the display-on
 * notification, e.g. via ssam_ctrl_notif_display_on(), which will also reset
 * the GPIO.
 *
 * On some devices, specifically ones with an integrated keyboard, the keyboard
 * backlight will be turned off by this call.
 *
 * This function will only send the display-off notification command if
 * display notifications are supported by the EC. Currently all known devices
 * support these notifications.
 *
 * Use ssam_ctrl_notif_display_on() to reverse the effects of this function.
 *
 * Return: Returns zero on success or if no request has been executed, the
 * status of the executed SAM request if that request failed, or %-EPROTO if
 * an unexpected response has been received.
 */
int ssam_ctrl_notif_display_off(struct ssam_controller *ctrl)
{
	int status;
	u8 response;

	ssam_dbg(ctrl, "pm: notifying display off\n");

	status = ssam_retry(ssam_ssh_notif_display_off, ctrl, &response);
	if (status)
		return status;

	if (response != 0) {
		ssam_err(ctrl, "unexpected response from display-off notification: %#04x\n",
			 response);
		return -EPROTO;
	}

	return 0;
}

/**
 * ssam_ctrl_notif_display_on() - Notify EC that the display has been turned on.
 * @ctrl: The controller.
 *
 * Notify the EC that the display has been turned back on and the driver has
 * exited its lower-power state. This notification is the counterpart to the
 * display-off notification sent via ssam_ctrl_notif_display_off() and will
 * reverse its effects, including resetting events to their default behavior.
 *
 * This function will only send the display-on notification command if display
 * notifications are supported by the EC. Currently all known devices support
 * these notifications.
 *
 * See ssam_ctrl_notif_display_off() for more details.
 *
 * Return: Returns zero on success or if no request has been executed, the
 * status of the executed SAM request if that request failed, or %-EPROTO if
 * an unexpected response has been received.
 */
int ssam_ctrl_notif_display_on(struct ssam_controller *ctrl)
{
	int status;
	u8 response;

	ssam_dbg(ctrl, "pm: notifying display on\n");

	status = ssam_retry(ssam_ssh_notif_display_on, ctrl, &response);
	if (status)
		return status;

	if (response != 0) {
		ssam_err(ctrl, "unexpected response from display-on notification: %#04x\n",
			 response);
		return -EPROTO;
	}

	return 0;
}

/**
 * ssam_ctrl_notif_d0_exit() - Notify EC that the driver/device exits the D0
 * power state.
 * @ctrl: The controller
 *
 * Notifies the EC that the driver prepares to exit the D0 power state in
 * favor of a lower-power state. Exact effects of this function related to the
 * EC are currently unknown.
 *
 * This function will only send the D0-exit notification command if D0-state
 * notifications are supported by the EC. Only newer Surface generations
 * support these notifications.
 *
 * Use ssam_ctrl_notif_d0_entry() to reverse the effects of this function.
 *
 * Return: Returns zero on success or if no request has been executed, the
 * status of the executed SAM request if that request failed, or %-EPROTO if
 * an unexpected response has been received.
 */
int ssam_ctrl_notif_d0_exit(struct ssam_controller *ctrl)
{
	int status;
	u8 response;

	if (!ctrl->caps.d3_closes_handle)
		return 0;

	ssam_dbg(ctrl, "pm: notifying D0 exit\n");

	status = ssam_retry(ssam_ssh_notif_d0_exit, ctrl, &response);
	if (status)
		return status;

	if (response != 0) {
		ssam_err(ctrl, "unexpected response from D0-exit notification: %#04x\n",
			 response);
		return -EPROTO;
	}

	return 0;
}

/**
 * ssam_ctrl_notif_d0_entry() - Notify EC that the driver/device enters the D0
 * power state.
 * @ctrl: The controller
 *
 * Notifies the EC that the driver has exited a lower-power state and entered
 * the D0 power state. Exact effects of this function related to the EC are
 * currently unknown.
 *
 * This function will only send the D0-entry notification command if D0-state
 * notifications are supported by the EC. Only newer Surface generations
 * support these notifications.
 *
 * See ssam_ctrl_notif_d0_exit() for more details.
 *
 * Return: Returns zero on success or if no request has been executed, the
 * status of the executed SAM request if that request failed, or %-EPROTO if
 * an unexpected response has been received.
 */
int ssam_ctrl_notif_d0_entry(struct ssam_controller *ctrl)
{
	int status;
	u8 response;

	if (!ctrl->caps.d3_closes_handle)
		return 0;

	ssam_dbg(ctrl, "pm: notifying D0 entry\n");

	status = ssam_retry(ssam_ssh_notif_d0_entry, ctrl, &response);
	if (status)
		return status;

	if (response != 0) {
		ssam_err(ctrl, "unexpected response from D0-entry notification: %#04x\n",
			 response);
		return -EPROTO;
	}

	return 0;
}


/* -- Top-level event registry interface. ----------------------------------- */

/**
 * ssam_nf_refcount_enable() - Enable event for reference count entry if it has
 * not already been enabled.
 * @ctrl:  The controller to enable the event on.
 * @entry: The reference count entry for the event to be enabled.
 * @flags: The flags used for enabling the event on the EC.
 *
 * Enable the event associated with the given reference count entry if the
 * reference count equals one, i.e. the event has not previously been enabled.
 * If the event has already been enabled (i.e. reference count not equal to
 * one), check that the flags used for enabling match and warn about this if
 * they do not.
 *
 * This does not modify the reference count itself, which is done with
 * ssam_nf_refcount_inc() / ssam_nf_refcount_dec().
 *
 * Note: ``nf->lock`` must be held when calling this function.
 *
 * Return: Returns zero on success. If the event is enabled by this call,
 * returns the status of the event-enable EC command.
 */
static int ssam_nf_refcount_enable(struct ssam_controller *ctrl,
				   struct ssam_nf_refcount_entry *entry, u8 flags)
{
	const struct ssam_event_registry reg = entry->key.reg;
	const struct ssam_event_id id = entry->key.id;
	struct ssam_nf *nf = &ctrl->cplt.event.notif;
	int status;

	lockdep_assert_held(&nf->lock);

	ssam_dbg(ctrl, "enabling event (reg: %#04x, tc: %#04x, iid: %#04x, rc: %d)\n",
		 reg.target_category, id.target_category, id.instance, entry->refcount);

	if (entry->refcount == 1) {
		status = ssam_ssh_event_enable(ctrl, reg, id, flags);
		if (status)
			return status;

		entry->flags = flags;

	} else if (entry->flags != flags) {
		ssam_warn(ctrl,
			  "inconsistent flags when enabling event: got %#04x, expected %#04x (reg: %#04x, tc: %#04x, iid: %#04x)\n",
			  flags, entry->flags, reg.target_category, id.target_category,
			  id.instance);
	}

	return 0;
}

/**
 * ssam_nf_refcount_disable_free() - Disable event for reference count entry if
 * it is no longer in use and free the corresponding entry.
 * @ctrl:  The controller to disable the event on.
 * @entry: The reference count entry for the event to be disabled.
 * @flags: The flags used for enabling the event on the EC.
 * @ec:    Flag specifying if the event should actually be disabled on the EC.
 *
 * If ``ec`` equals ``true`` and the reference count equals zero (i.e. the
 * event is no longer requested by any client), the specified event will be
 * disabled on the EC via the corresponding request.
 *
 * If ``ec`` equals ``false``, no request will be sent to the EC and the event
 * can be considered in a detached state (i.e. no longer used but still
 * enabled). Disabling an event via this method may be required for
 * hot-removable devices, where event disable requests may time out after the
 * device has been physically removed.
 *
 * In both cases, if the reference count equals zero, the corresponding
 * reference count entry will be freed. The reference count entry must not be
 * used any more after a call to this function.
 *
 * Also checks if the flags used for disabling the event match the flags used
 * for enabling the event and warns if they do not (regardless of reference
 * count).
 *
 * This does not modify the reference count itself, which is done with
 * ssam_nf_refcount_inc() / ssam_nf_refcount_dec().
 *
 * Note: ``nf->lock`` must be held when calling this function.
 *
 * Return: Returns zero on success. If the event is disabled by this call,
 * returns the status of the event-enable EC command.
 */
static int ssam_nf_refcount_disable_free(struct ssam_controller *ctrl,
					 struct ssam_nf_refcount_entry *entry, u8 flags, bool ec)
{
	const struct ssam_event_registry reg = entry->key.reg;
	const struct ssam_event_id id = entry->key.id;
	struct ssam_nf *nf = &ctrl->cplt.event.notif;
	int status = 0;

	lockdep_assert_held(&nf->lock);

	ssam_dbg(ctrl, "%s event (reg: %#04x, tc: %#04x, iid: %#04x, rc: %d)\n",
		 ec ? "disabling" : "detaching", reg.target_category, id.target_category,
		 id.instance, entry->refcount);

	if (entry->flags != flags) {
		ssam_warn(ctrl,
			  "inconsistent flags when disabling event: got %#04x, expected %#04x (reg: %#04x, tc: %#04x, iid: %#04x)\n",
			  flags, entry->flags, reg.target_category, id.target_category,
			  id.instance);
	}

	if (ec && entry->refcount == 0) {
		status = ssam_ssh_event_disable(ctrl, reg, id, flags);
		kfree(entry);
	}

	return status;
}

/**
 * ssam_notifier_register() - Register an event notifier.
 * @ctrl: The controller to register the notifier on.
 * @n:    The event notifier to register.
 *
 * Register an event notifier. Increment the usage counter of the associated
 * SAM event if the notifier is not marked as an observer. If the event is not
 * marked as an observer and is currently not enabled, it will be enabled
 * during this call. If the notifier is marked as an observer, no attempt will
 * be made at enabling any event and no reference count will be modified.
 *
 * Notifiers marked as observers do not need to be associated with one specific
 * event, i.e. as long as no event matching is performed, only the event target
 * category needs to be set.
 *
 * Return: Returns zero on success, %-ENOSPC if there have already been
 * %INT_MAX notifiers for the event ID/type associated with the notifier block
 * registered, %-ENOMEM if the corresponding event entry could not be
 * allocated. If this is the first time that a notifier block is registered
 * for the specific associated event, returns the status of the event-enable
 * EC-command.
 */
int ssam_notifier_register(struct ssam_controller *ctrl, struct ssam_event_notifier *n)
{
	u16 rqid = ssh_tc_to_rqid(n->event.id.target_category);
	struct ssam_nf_refcount_entry *entry = NULL;
	struct ssam_nf_head *nf_head;
	struct ssam_nf *nf;
	int status;

	if (!ssh_rqid_is_event(rqid))
		return -EINVAL;

	nf = &ctrl->cplt.event.notif;
	nf_head = &nf->head[ssh_rqid_to_event(rqid)];

	mutex_lock(&nf->lock);

	if (!(n->flags & SSAM_EVENT_NOTIFIER_OBSERVER)) {
		entry = ssam_nf_refcount_inc(nf, n->event.reg, n->event.id);
		if (IS_ERR(entry)) {
			mutex_unlock(&nf->lock);
			return PTR_ERR(entry);
		}
	}

	status = ssam_nfblk_insert(nf_head, &n->base);
	if (status) {
		if (entry)
			ssam_nf_refcount_dec_free(nf, n->event.reg, n->event.id);

		mutex_unlock(&nf->lock);
		return status;
	}

	if (entry) {
		status = ssam_nf_refcount_enable(ctrl, entry, n->event.flags);
		if (status) {
			ssam_nfblk_remove(&n->base);
			ssam_nf_refcount_dec_free(nf, n->event.reg, n->event.id);
			mutex_unlock(&nf->lock);
			synchronize_srcu(&nf_head->srcu);
			return status;
		}
	}

	mutex_unlock(&nf->lock);
	return 0;
}
EXPORT_SYMBOL_GPL(ssam_notifier_register);

/**
 * __ssam_notifier_unregister() - Unregister an event notifier.
 * @ctrl:    The controller the notifier has been registered on.
 * @n:       The event notifier to unregister.
 * @disable: Whether to disable the corresponding event on the EC.
 *
 * Unregister an event notifier. Decrement the usage counter of the associated
 * SAM event if the notifier is not marked as an observer. If the usage counter
 * reaches zero and ``disable`` equals ``true``, the event will be disabled.
 *
 * Useful for hot-removable devices, where communication may fail once the
 * device has been physically removed. In that case, specifying ``disable`` as
 * ``false`` avoids communication with the EC.
 *
 * Return: Returns zero on success, %-ENOENT if the given notifier block has
 * not been registered on the controller. If the given notifier block was the
 * last one associated with its specific event, returns the status of the
 * event-disable EC-command.
 */
int __ssam_notifier_unregister(struct ssam_controller *ctrl, struct ssam_event_notifier *n,
			       bool disable)
{
	u16 rqid = ssh_tc_to_rqid(n->event.id.target_category);
	struct ssam_nf_refcount_entry *entry;
	struct ssam_nf_head *nf_head;
	struct ssam_nf *nf;
	int status = 0;

	if (!ssh_rqid_is_event(rqid))
		return -EINVAL;

	nf = &ctrl->cplt.event.notif;
	nf_head = &nf->head[ssh_rqid_to_event(rqid)];

	mutex_lock(&nf->lock);

	if (!ssam_nfblk_find(nf_head, &n->base)) {
		mutex_unlock(&nf->lock);
		return -ENOENT;
	}

	/*
	 * If this is an observer notifier, do not attempt to disable the
	 * event, just remove it.
	 */
	if (!(n->flags & SSAM_EVENT_NOTIFIER_OBSERVER)) {
		entry = ssam_nf_refcount_dec(nf, n->event.reg, n->event.id);
		if (WARN_ON(!entry)) {
			/*
			 * If this does not return an entry, there's a logic
			 * error somewhere: The notifier block is registered,
			 * but the event refcount entry is not there. Remove
			 * the notifier block anyways.
			 */
			status = -ENOENT;
			goto remove;
		}

		status = ssam_nf_refcount_disable_free(ctrl, entry, n->event.flags, disable);
	}

remove:
	ssam_nfblk_remove(&n->base);
	mutex_unlock(&nf->lock);
	synchronize_srcu(&nf_head->srcu);

	return status;
}
EXPORT_SYMBOL_GPL(__ssam_notifier_unregister);

/**
 * ssam_controller_event_enable() - Enable the specified event.
 * @ctrl:  The controller to enable the event for.
 * @reg:   The event registry to use for enabling the event.
 * @id:    The event ID specifying the event to be enabled.
 * @flags: The SAM event flags used for enabling the event.
 *
 * Increment the event reference count of the specified event. If the event has
 * not been enabled previously, it will be enabled by this call.
 *
 * Note: In general, ssam_notifier_register() with a non-observer notifier
 * should be preferred for enabling/disabling events, as this will guarantee
 * proper ordering and event forwarding in case of errors during event
 * enabling/disabling.
 *
 * Return: Returns zero on success, %-ENOSPC if the reference count for the
 * specified event has reached its maximum, %-ENOMEM if the corresponding event
 * entry could not be allocated. If this is the first time that this event has
 * been enabled (i.e. the reference count was incremented from zero to one by
 * this call), returns the status of the event-enable EC-command.
 */
int ssam_controller_event_enable(struct ssam_controller *ctrl,
				 struct ssam_event_registry reg,
				 struct ssam_event_id id, u8 flags)
{
	u16 rqid = ssh_tc_to_rqid(id.target_category);
	struct ssam_nf *nf = &ctrl->cplt.event.notif;
	struct ssam_nf_refcount_entry *entry;
	int status;

	if (!ssh_rqid_is_event(rqid))
		return -EINVAL;

	mutex_lock(&nf->lock);

	entry = ssam_nf_refcount_inc(nf, reg, id);
	if (IS_ERR(entry)) {
		mutex_unlock(&nf->lock);
		return PTR_ERR(entry);
	}

	status = ssam_nf_refcount_enable(ctrl, entry, flags);
	if (status) {
		ssam_nf_refcount_dec_free(nf, reg, id);
		mutex_unlock(&nf->lock);
		return status;
	}

	mutex_unlock(&nf->lock);
	return 0;
}
EXPORT_SYMBOL_GPL(ssam_controller_event_enable);

/**
 * ssam_controller_event_disable() - Disable the specified event.
 * @ctrl:  The controller to disable the event for.
 * @reg:   The event registry to use for disabling the event.
 * @id:    The event ID specifying the event to be disabled.
 * @flags: The flags used when enabling the event.
 *
 * Decrement the reference count of the specified event. If the reference count
 * reaches zero, the event will be disabled.
 *
 * Note: In general, ssam_notifier_register()/ssam_notifier_unregister() with a
 * non-observer notifier should be preferred for enabling/disabling events, as
 * this will guarantee proper ordering and event forwarding in case of errors
 * during event enabling/disabling.
 *
 * Return: Returns zero on success, %-ENOENT if the given event has not been
 * enabled on the controller. If the reference count of the event reaches zero
 * during this call, returns the status of the event-disable EC-command.
 */
int ssam_controller_event_disable(struct ssam_controller *ctrl,
				  struct ssam_event_registry reg,
				  struct ssam_event_id id, u8 flags)
{
	u16 rqid = ssh_tc_to_rqid(id.target_category);
	struct ssam_nf *nf = &ctrl->cplt.event.notif;
	struct ssam_nf_refcount_entry *entry;
	int status;

	if (!ssh_rqid_is_event(rqid))
		return -EINVAL;

	mutex_lock(&nf->lock);

	entry = ssam_nf_refcount_dec(nf, reg, id);
	if (!entry) {
		mutex_unlock(&nf->lock);
		return -ENOENT;
	}

	status = ssam_nf_refcount_disable_free(ctrl, entry, flags, true);

	mutex_unlock(&nf->lock);
	return status;
}
EXPORT_SYMBOL_GPL(ssam_controller_event_disable);

/**
 * ssam_notifier_disable_registered() - Disable events for all registered
 * notifiers.
 * @ctrl: The controller for which to disable the notifiers/events.
 *
 * Disables events for all currently registered notifiers. In case of an error
 * (EC command failing), all previously disabled events will be restored and
 * the error code returned.
 *
 * This function is intended to disable all events prior to hibernation entry.
 * See ssam_notifier_restore_registered() to restore/re-enable all events
 * disabled with this function.
 *
 * Note that this function will not disable events for notifiers registered
 * after calling this function. It should thus be made sure that no new
 * notifiers are going to be added after this call and before the corresponding
 * call to ssam_notifier_restore_registered().
 *
 * Return: Returns zero on success. In case of failure returns the error code
 * returned by the failed EC command to disable an event.
 */
int ssam_notifier_disable_registered(struct ssam_controller *ctrl)
{
	struct ssam_nf *nf = &ctrl->cplt.event.notif;
	struct rb_node *n;
	int status;

	mutex_lock(&nf->lock);
	for (n = rb_first(&nf->refcount); n; n = rb_next(n)) {
		struct ssam_nf_refcount_entry *e;

		e = rb_entry(n, struct ssam_nf_refcount_entry, node);
		status = ssam_ssh_event_disable(ctrl, e->key.reg,
						e->key.id, e->flags);
		if (status)
			goto err;
	}
	mutex_unlock(&nf->lock);

	return 0;

err:
	for (n = rb_prev(n); n; n = rb_prev(n)) {
		struct ssam_nf_refcount_entry *e;

		e = rb_entry(n, struct ssam_nf_refcount_entry, node);
		ssam_ssh_event_enable(ctrl, e->key.reg, e->key.id, e->flags);
	}
	mutex_unlock(&nf->lock);

	return status;
}

/**
 * ssam_notifier_restore_registered() - Restore/re-enable events for all
 * registered notifiers.
 * @ctrl: The controller for which to restore the notifiers/events.
 *
 * Restores/re-enables all events for which notifiers have been registered on
 * the given controller. In case of a failure, the error is logged and the
 * function continues to try and enable the remaining events.
 *
 * This function is intended to restore/re-enable all registered events after
 * hibernation. See ssam_notifier_disable_registered() for the counter part
 * disabling the events and more details.
 */
void ssam_notifier_restore_registered(struct ssam_controller *ctrl)
{
	struct ssam_nf *nf = &ctrl->cplt.event.notif;
	struct rb_node *n;

	mutex_lock(&nf->lock);
	for (n = rb_first(&nf->refcount); n; n = rb_next(n)) {
		struct ssam_nf_refcount_entry *e;

		e = rb_entry(n, struct ssam_nf_refcount_entry, node);

		/* Ignore errors, will get logged in call. */
		ssam_ssh_event_enable(ctrl, e->key.reg, e->key.id, e->flags);
	}
	mutex_unlock(&nf->lock);
}

/**
 * ssam_notifier_is_empty() - Check if there are any registered notifiers.
 * @ctrl: The controller to check on.
 *
 * Return: Returns %true if there are currently no notifiers registered on the
 * controller, %false otherwise.
 */
static bool ssam_notifier_is_empty(struct ssam_controller *ctrl)
{
	struct ssam_nf *nf = &ctrl->cplt.event.notif;
	bool result;

	mutex_lock(&nf->lock);
	result = ssam_nf_refcount_empty(nf);
	mutex_unlock(&nf->lock);

	return result;
}

/**
 * ssam_notifier_unregister_all() - Unregister all currently registered
 * notifiers.
 * @ctrl: The controller to unregister the notifiers on.
 *
 * Unregisters all currently registered notifiers. This function is used to
 * ensure that all notifiers will be unregistered and associated
 * entries/resources freed when the controller is being shut down.
 */
static void ssam_notifier_unregister_all(struct ssam_controller *ctrl)
{
	struct ssam_nf *nf = &ctrl->cplt.event.notif;
	struct ssam_nf_refcount_entry *e, *n;

	mutex_lock(&nf->lock);
	rbtree_postorder_for_each_entry_safe(e, n, &nf->refcount, node) {
		/* Ignore errors, will get logged in call. */
		ssam_ssh_event_disable(ctrl, e->key.reg, e->key.id, e->flags);
		kfree(e);
	}
	nf->refcount = RB_ROOT;
	mutex_unlock(&nf->lock);
}


/* -- Wakeup IRQ. ----------------------------------------------------------- */

static irqreturn_t ssam_irq_handle(int irq, void *dev_id)
{
	struct ssam_controller *ctrl = dev_id;

	ssam_dbg(ctrl, "pm: wake irq triggered\n");

	/*
	 * Note: Proper wakeup detection is currently unimplemented.
	 *       When the EC is in display-off or any other non-D0 state, it
	 *       does not send events/notifications to the host. Instead it
	 *       signals that there are events available via the wakeup IRQ.
	 *       This driver is responsible for calling back to the EC to
	 *       release these events one-by-one.
	 *
	 *       This IRQ should not cause a full system resume by its own.
	 *       Instead, events should be handled by their respective subsystem
	 *       drivers, which in turn should signal whether a full system
	 *       resume should be performed.
	 *
	 * TODO: Send GPIO callback command repeatedly to EC until callback
	 *       returns 0x00. Return flag of callback is "has more events".
	 *       Each time the command is sent, one event is "released". Once
	 *       all events have been released (return = 0x00), the GPIO is
	 *       re-armed. Detect wakeup events during this process, go back to
	 *       sleep if no wakeup event has been received.
	 */

	return IRQ_HANDLED;
}

/**
 * ssam_irq_setup() - Set up SAM EC wakeup-GPIO interrupt.
 * @ctrl: The controller for which the IRQ should be set up.
 *
 * Set up an IRQ for the wakeup-GPIO pin of the SAM EC. This IRQ can be used
 * to wake the device from a low power state.
 *
 * Note that this IRQ can only be triggered while the EC is in the display-off
 * state. In this state, events are not sent to the host in the usual way.
 * Instead the wakeup-GPIO gets pulled to "high" as long as there are pending
 * events and these events need to be released one-by-one via the GPIO
 * callback request, either until there are no events left and the GPIO is
 * reset, or all at once by transitioning the EC out of the display-off state,
 * which will also clear the GPIO.
 *
 * Not all events, however, should trigger a full system wakeup. Instead the
 * driver should, if necessary, inspect and forward each event to the
 * corresponding subsystem, which in turn should decide if the system needs to
 * be woken up. This logic has not been implemented yet, thus wakeup by this
 * IRQ should be disabled by default to avoid spurious wake-ups, caused, for
 * example, by the remaining battery percentage changing. Refer to comments in
 * this function and comments in the corresponding IRQ handler for more
 * details on how this should be implemented.
 *
 * See also ssam_ctrl_notif_display_off() and ssam_ctrl_notif_display_off()
 * for functions to transition the EC into and out of the display-off state as
 * well as more details on it.
 *
 * The IRQ is disabled by default and has to be enabled before it can wake up
 * the device from suspend via ssam_irq_arm_for_wakeup(). On teardown, the IRQ
 * should be freed via ssam_irq_free().
 */
int ssam_irq_setup(struct ssam_controller *ctrl)
{
	struct device *dev = ssam_controller_device(ctrl);
	struct gpio_desc *gpiod;
	int irq;
	int status;

	/*
	 * The actual GPIO interrupt is declared in ACPI as TRIGGER_HIGH.
	 * However, the GPIO line only gets reset by sending the GPIO callback
	 * command to SAM (or alternatively the display-on notification). As
	 * proper handling for this interrupt is not implemented yet, leaving
	 * the IRQ at TRIGGER_HIGH would cause an IRQ storm (as the callback
	 * never gets sent and thus the line never gets reset). To avoid this,
	 * mark the IRQ as TRIGGER_RISING for now, only creating a single
	 * interrupt, and let the SAM resume callback during the controller
	 * resume process clear it.
	 */
	const int irqf = IRQF_ONESHOT | IRQF_TRIGGER_RISING | IRQF_NO_AUTOEN;

	gpiod = gpiod_get(dev, "ssam_wakeup-int", GPIOD_ASIS);
	if (IS_ERR(gpiod))
		return PTR_ERR(gpiod);

	irq = gpiod_to_irq(gpiod);
	gpiod_put(gpiod);

	if (irq < 0)
		return irq;

	status = request_threaded_irq(irq, NULL, ssam_irq_handle, irqf,
				      "ssam_wakeup", ctrl);
	if (status)
		return status;

	ctrl->irq.num = irq;
	return 0;
}

/**
 * ssam_irq_free() - Free SAM EC wakeup-GPIO interrupt.
 * @ctrl: The controller for which the IRQ should be freed.
 *
 * Free the wakeup-GPIO IRQ previously set-up via ssam_irq_setup().
 */
void ssam_irq_free(struct ssam_controller *ctrl)
{
	free_irq(ctrl->irq.num, ctrl);
	ctrl->irq.num = -1;
}

/**
 * ssam_irq_arm_for_wakeup() - Arm the EC IRQ for wakeup, if enabled.
 * @ctrl: The controller for which the IRQ should be armed.
 *
 * Sets up the IRQ so that it can be used to wake the device. Specifically,
 * this function enables the irq and then, if the device is allowed to wake up
 * the system, calls enable_irq_wake(). See ssam_irq_disarm_wakeup() for the
 * corresponding function to disable the IRQ.
 *
 * This function is intended to arm the IRQ before entering S2idle suspend.
 *
 * Note: calls to ssam_irq_arm_for_wakeup() and ssam_irq_disarm_wakeup() must
 * be balanced.
 */
int ssam_irq_arm_for_wakeup(struct ssam_controller *ctrl)
{
	struct device *dev = ssam_controller_device(ctrl);
	int status;

	enable_irq(ctrl->irq.num);
	if (device_may_wakeup(dev)) {
		status = enable_irq_wake(ctrl->irq.num);
		if (status) {
			ssam_err(ctrl, "failed to enable wake IRQ: %d\n", status);
			disable_irq(ctrl->irq.num);
			return status;
		}

		ctrl->irq.wakeup_enabled = true;
	} else {
		ctrl->irq.wakeup_enabled = false;
	}

	return 0;
}

/**
 * ssam_irq_disarm_wakeup() - Disarm the wakeup IRQ.
 * @ctrl: The controller for which the IRQ should be disarmed.
 *
 * Disarm the IRQ previously set up for wake via ssam_irq_arm_for_wakeup().
 *
 * This function is intended to disarm the IRQ after exiting S2idle suspend.
 *
 * Note: calls to ssam_irq_arm_for_wakeup() and ssam_irq_disarm_wakeup() must
 * be balanced.
 */
void ssam_irq_disarm_wakeup(struct ssam_controller *ctrl)
{
	int status;

	if (ctrl->irq.wakeup_enabled) {
		status = disable_irq_wake(ctrl->irq.num);
		if (status)
			ssam_err(ctrl, "failed to disable wake IRQ: %d\n", status);

		ctrl->irq.wakeup_enabled = false;
	}
	disable_irq(ctrl->irq.num);
}
