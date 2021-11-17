// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Notification support
 *
 * Copyright (C) 2020-2021 ARM Ltd.
 */
/**
 * DOC: Theory of operation
 *
 * SCMI Protocol specification allows the platform to signal events to
 * interested agents via notification messages: this is an implementation
 * of the dispatch and delivery of such notifications to the interested users
 * inside the Linux kernel.
 *
 * An SCMI Notification core instance is initialized for each active platform
 * instance identified by the means of the usual &struct scmi_handle.
 *
 * Each SCMI Protocol implementation, during its initialization, registers with
 * this core its set of supported events using scmi_register_protocol_events():
 * all the needed descriptors are stored in the &struct registered_protocols and
 * &struct registered_events arrays.
 *
 * Kernel users interested in some specific event can register their callbacks
 * providing the usual notifier_block descriptor, since this core implements
 * events' delivery using the standard Kernel notification chains machinery.
 *
 * Given the number of possible events defined by SCMI and the extensibility
 * of the SCMI Protocol itself, the underlying notification chains are created
 * and destroyed dynamically on demand depending on the number of users
 * effectively registered for an event, so that no support structures or chains
 * are allocated until at least one user has registered a notifier_block for
 * such event. Similarly, events' generation itself is enabled at the platform
 * level only after at least one user has registered, and it is shutdown after
 * the last user for that event has gone.
 *
 * All users provided callbacks and allocated notification-chains are stored in
 * the @registered_events_handlers hashtable. Callbacks' registration requests
 * for still to be registered events are instead kept in the dedicated common
 * hashtable @pending_events_handlers.
 *
 * An event is identified univocally by the tuple (proto_id, evt_id, src_id)
 * and is served by its own dedicated notification chain; information contained
 * in such tuples is used, in a few different ways, to generate the needed
 * hash-keys.
 *
 * Here proto_id and evt_id are simply the protocol_id and message_id numbers
 * as described in the SCMI Protocol specification, while src_id represents an
 * optional, protocol dependent, source identifier (like domain_id, perf_id
 * or sensor_id and so forth).
 *
 * Upon reception of a notification message from the platform the SCMI RX ISR
 * passes the received message payload and some ancillary information (including
 * an arrival timestamp in nanoseconds) to the core via @scmi_notify() which
 * pushes the event-data itself on a protocol-dedicated kfifo queue for further
 * deferred processing as specified in @scmi_events_dispatcher().
 *
 * Each protocol has it own dedicated work_struct and worker which, once kicked
 * by the ISR, takes care to empty its own dedicated queue, deliverying the
 * queued items into the proper notification-chain: notifications processing can
 * proceed concurrently on distinct workers only between events belonging to
 * different protocols while delivery of events within the same protocol is
 * still strictly sequentially ordered by time of arrival.
 *
 * Events' information is then extracted from the SCMI Notification messages and
 * conveyed, converted into a custom per-event report struct, as the void *data
 * param to the user callback provided by the registered notifier_block, so that
 * from the user perspective his callback will look invoked like:
 *
 * int user_cb(struct notifier_block *nb, unsigned long event_id, void *report)
 *
 */

#define dev_fmt(fmt) "SCMI Notifications - " fmt
#define pr_fmt(fmt) "SCMI Notifications - " fmt

#include <linux/bitfield.h>
#include <linux/bug.h>
#include <linux/compiler.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/hashtable.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/kfifo.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/refcount.h>
#include <linux/scmi_protocol.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "common.h"
#include "notify.h"

#define SCMI_MAX_PROTO		256

#define PROTO_ID_MASK		GENMASK(31, 24)
#define EVT_ID_MASK		GENMASK(23, 16)
#define SRC_ID_MASK		GENMASK(15, 0)

/*
 * Builds an unsigned 32bit key from the given input tuple to be used
 * as a key in hashtables.
 */
#define MAKE_HASH_KEY(p, e, s)			\
	(FIELD_PREP(PROTO_ID_MASK, (p)) |	\
	   FIELD_PREP(EVT_ID_MASK, (e)) |	\
	   FIELD_PREP(SRC_ID_MASK, (s)))

#define MAKE_ALL_SRCS_KEY(p, e)		MAKE_HASH_KEY((p), (e), SRC_ID_MASK)

/*
 * Assumes that the stored obj includes its own hash-key in a field named 'key':
 * with this simplification this macro can be equally used for all the objects'
 * types hashed by this implementation.
 *
 * @__ht: The hashtable name
 * @__obj: A pointer to the object type to be retrieved from the hashtable;
 *	   it will be used as a cursor while scanning the hastable and it will
 *	   be possibly left as NULL when @__k is not found
 * @__k: The key to search for
 */
#define KEY_FIND(__ht, __obj, __k)				\
({								\
	typeof(__k) k_ = __k;					\
	typeof(__obj) obj_;					\
								\
	hash_for_each_possible((__ht), obj_, hash, k_)		\
		if (obj_->key == k_)				\
			break;					\
	__obj = obj_;						\
})

#define KEY_XTRACT_PROTO_ID(key)	FIELD_GET(PROTO_ID_MASK, (key))
#define KEY_XTRACT_EVT_ID(key)		FIELD_GET(EVT_ID_MASK, (key))
#define KEY_XTRACT_SRC_ID(key)		FIELD_GET(SRC_ID_MASK, (key))

/*
 * A set of macros used to access safely @registered_protocols and
 * @registered_events arrays; these are fixed in size and each entry is possibly
 * populated at protocols' registration time and then only read but NEVER
 * modified or removed.
 */
#define SCMI_GET_PROTO(__ni, __pid)					\
({									\
	typeof(__ni) ni_ = __ni;					\
	struct scmi_registered_events_desc *__pd = NULL;		\
									\
	if (ni_)							\
		__pd = READ_ONCE(ni_->registered_protocols[(__pid)]);	\
	__pd;								\
})

#define SCMI_GET_REVT_FROM_PD(__pd, __eid)				\
({									\
	typeof(__pd) pd_ = __pd;					\
	typeof(__eid) eid_ = __eid;					\
	struct scmi_registered_event *__revt = NULL;			\
									\
	if (pd_ && eid_ < pd_->num_events)				\
		__revt = READ_ONCE(pd_->registered_events[eid_]);	\
	__revt;								\
})

#define SCMI_GET_REVT(__ni, __pid, __eid)				\
({									\
	struct scmi_registered_event *__revt;				\
	struct scmi_registered_events_desc *__pd;			\
									\
	__pd = SCMI_GET_PROTO((__ni), (__pid));				\
	__revt = SCMI_GET_REVT_FROM_PD(__pd, (__eid));			\
	__revt;								\
})

/* A couple of utility macros to limit cruft when calling protocols' helpers */
#define REVT_NOTIFY_SET_STATUS(revt, eid, sid, state)		\
({								\
	typeof(revt) r = revt;					\
	r->proto->ops->set_notify_enabled(r->proto->ph,		\
					(eid), (sid), (state));	\
})

#define REVT_NOTIFY_ENABLE(revt, eid, sid)			\
	REVT_NOTIFY_SET_STATUS((revt), (eid), (sid), true)

#define REVT_NOTIFY_DISABLE(revt, eid, sid)			\
	REVT_NOTIFY_SET_STATUS((revt), (eid), (sid), false)

#define REVT_FILL_REPORT(revt, ...)				\
({								\
	typeof(revt) r = revt;					\
	r->proto->ops->fill_custom_report(r->proto->ph,		\
					  __VA_ARGS__);		\
})

#define SCMI_PENDING_HASH_SZ		4
#define SCMI_REGISTERED_HASH_SZ		6

struct scmi_registered_events_desc;

/**
 * struct scmi_notify_instance  - Represents an instance of the notification
 * core
 * @gid: GroupID used for devres
 * @handle: A reference to the platform instance
 * @init_work: A work item to perform final initializations of pending handlers
 * @notify_wq: A reference to the allocated Kernel cmwq
 * @pending_mtx: A mutex to protect @pending_events_handlers
 * @registered_protocols: A statically allocated array containing pointers to
 *			  all the registered protocol-level specific information
 *			  related to events' handling
 * @pending_events_handlers: An hashtable containing all pending events'
 *			     handlers descriptors
 *
 * Each platform instance, represented by a handle, has its own instance of
 * the notification subsystem represented by this structure.
 */
struct scmi_notify_instance {
	void			*gid;
	struct scmi_handle	*handle;
	struct work_struct	init_work;
	struct workqueue_struct	*notify_wq;
	/* lock to protect pending_events_handlers */
	struct mutex		pending_mtx;
	struct scmi_registered_events_desc	**registered_protocols;
	DECLARE_HASHTABLE(pending_events_handlers, SCMI_PENDING_HASH_SZ);
};

/**
 * struct events_queue  - Describes a queue and its associated worker
 * @sz: Size in bytes of the related kfifo
 * @kfifo: A dedicated Kernel kfifo descriptor
 * @notify_work: A custom work item bound to this queue
 * @wq: A reference to the associated workqueue
 *
 * Each protocol has its own dedicated events_queue descriptor.
 */
struct events_queue {
	size_t			sz;
	struct kfifo		kfifo;
	struct work_struct	notify_work;
	struct workqueue_struct	*wq;
};

/**
 * struct scmi_event_header  - A utility header
 * @timestamp: The timestamp, in nanoseconds (boottime), which was associated
 *	       to this event as soon as it entered the SCMI RX ISR
 * @payld_sz: Effective size of the embedded message payload which follows
 * @evt_id: Event ID (corresponds to the Event MsgID for this Protocol)
 * @payld: A reference to the embedded event payload
 *
 * This header is prepended to each received event message payload before
 * queueing it on the related &struct events_queue.
 */
struct scmi_event_header {
	ktime_t timestamp;
	size_t payld_sz;
	unsigned char evt_id;
	unsigned char payld[];
};

struct scmi_registered_event;

/**
 * struct scmi_registered_events_desc  - Protocol Specific information
 * @id: Protocol ID
 * @ops: Protocol specific and event-related operations
 * @equeue: The embedded per-protocol events_queue
 * @ni: A reference to the initialized instance descriptor
 * @eh: A reference to pre-allocated buffer to be used as a scratch area by the
 *	deferred worker when fetching data from the kfifo
 * @eh_sz: Size of the pre-allocated buffer @eh
 * @in_flight: A reference to an in flight &struct scmi_registered_event
 * @num_events: Number of events in @registered_events
 * @registered_events: A dynamically allocated array holding all the registered
 *		       events' descriptors, whose fixed-size is determined at
 *		       compile time.
 * @registered_mtx: A mutex to protect @registered_events_handlers
 * @ph: SCMI protocol handle reference
 * @registered_events_handlers: An hashtable containing all events' handlers
 *				descriptors registered for this protocol
 *
 * All protocols that register at least one event have their protocol-specific
 * information stored here, together with the embedded allocated events_queue.
 * These descriptors are stored in the @registered_protocols array at protocol
 * registration time.
 *
 * Once these descriptors are successfully registered, they are NEVER again
 * removed or modified since protocols do not unregister ever, so that, once
 * we safely grab a NON-NULL reference from the array we can keep it and use it.
 */
struct scmi_registered_events_desc {
	u8				id;
	const struct scmi_event_ops	*ops;
	struct events_queue		equeue;
	struct scmi_notify_instance	*ni;
	struct scmi_event_header	*eh;
	size_t				eh_sz;
	void				*in_flight;
	int				num_events;
	struct scmi_registered_event	**registered_events;
	/* mutex to protect registered_events_handlers */
	struct mutex			registered_mtx;
	const struct scmi_protocol_handle	*ph;
	DECLARE_HASHTABLE(registered_events_handlers, SCMI_REGISTERED_HASH_SZ);
};

/**
 * struct scmi_registered_event  - Event Specific Information
 * @proto: A reference to the associated protocol descriptor
 * @evt: A reference to the associated event descriptor (as provided at
 *       registration time)
 * @report: A pre-allocated buffer used by the deferred worker to fill a
 *	    customized event report
 * @num_sources: The number of possible sources for this event as stated at
 *		 events' registration time
 * @sources: A reference to a dynamically allocated array used to refcount the
 *	     events' enable requests for all the existing sources
 * @sources_mtx: A mutex to serialize the access to @sources
 *
 * All registered events are represented by one of these structures that are
 * stored in the @registered_events array at protocol registration time.
 *
 * Once these descriptors are successfully registered, they are NEVER again
 * removed or modified since protocols do not unregister ever, so that once we
 * safely grab a NON-NULL reference from the table we can keep it and use it.
 */
struct scmi_registered_event {
	struct scmi_registered_events_desc *proto;
	const struct scmi_event	*evt;
	void		*report;
	u32		num_sources;
	refcount_t	*sources;
	/* locking to serialize the access to sources */
	struct mutex	sources_mtx;
};

/**
 * struct scmi_event_handler  - Event handler information
 * @key: The used hashkey
 * @users: A reference count for number of active users for this handler
 * @r_evt: A reference to the associated registered event; when this is NULL
 *	   this handler is pending, which means that identifies a set of
 *	   callbacks intended to be attached to an event which is still not
 *	   known nor registered by any protocol at that point in time
 * @chain: The notification chain dedicated to this specific event tuple
 * @hash: The hlist_node used for collision handling
 * @enabled: A boolean which records if event's generation has been already
 *	     enabled for this handler as a whole
 *
 * This structure collects all the information needed to process a received
 * event identified by the tuple (proto_id, evt_id, src_id).
 * These descriptors are stored in a per-protocol @registered_events_handlers
 * table using as a key a value derived from that tuple.
 */
struct scmi_event_handler {
	u32				key;
	refcount_t			users;
	struct scmi_registered_event	*r_evt;
	struct blocking_notifier_head	chain;
	struct hlist_node		hash;
	bool				enabled;
};

#define IS_HNDL_PENDING(hndl)	(!(hndl)->r_evt)

static struct scmi_event_handler *
scmi_get_active_handler(struct scmi_notify_instance *ni, u32 evt_key);
static void scmi_put_active_handler(struct scmi_notify_instance *ni,
				    struct scmi_event_handler *hndl);
static bool scmi_put_handler_unlocked(struct scmi_notify_instance *ni,
				      struct scmi_event_handler *hndl);

/**
 * scmi_lookup_and_call_event_chain()  - Lookup the proper chain and call it
 * @ni: A reference to the notification instance to use
 * @evt_key: The key to use to lookup the related notification chain
 * @report: The customized event-specific report to pass down to the callbacks
 *	    as their *data parameter.
 */
static inline void
scmi_lookup_and_call_event_chain(struct scmi_notify_instance *ni,
				 u32 evt_key, void *report)
{
	int ret;
	struct scmi_event_handler *hndl;

	/*
	 * Here ensure the event handler cannot vanish while using it.
	 * It is legitimate, though, for an handler not to be found at all here,
	 * e.g. when it has been unregistered by the user after some events had
	 * already been queued.
	 */
	hndl = scmi_get_active_handler(ni, evt_key);
	if (!hndl)
		return;

	ret = blocking_notifier_call_chain(&hndl->chain,
					   KEY_XTRACT_EVT_ID(evt_key),
					   report);
	/* Notifiers are NOT supposed to cut the chain ... */
	WARN_ON_ONCE(ret & NOTIFY_STOP_MASK);

	scmi_put_active_handler(ni, hndl);
}

/**
 * scmi_process_event_header()  - Dequeue and process an event header
 * @eq: The queue to use
 * @pd: The protocol descriptor to use
 *
 * Read an event header from the protocol queue into the dedicated scratch
 * buffer and looks for a matching registered event; in case an anomalously
 * sized read is detected just flush the queue.
 *
 * Return:
 * * a reference to the matching registered event when found
 * * ERR_PTR(-EINVAL) when NO registered event could be found
 * * NULL when the queue is empty
 */
static inline struct scmi_registered_event *
scmi_process_event_header(struct events_queue *eq,
			  struct scmi_registered_events_desc *pd)
{
	unsigned int outs;
	struct scmi_registered_event *r_evt;

	outs = kfifo_out(&eq->kfifo, pd->eh,
			 sizeof(struct scmi_event_header));
	if (!outs)
		return NULL;
	if (outs != sizeof(struct scmi_event_header)) {
		dev_err(pd->ni->handle->dev, "corrupted EVT header. Flush.\n");
		kfifo_reset_out(&eq->kfifo);
		return NULL;
	}

	r_evt = SCMI_GET_REVT_FROM_PD(pd, pd->eh->evt_id);
	if (!r_evt)
		r_evt = ERR_PTR(-EINVAL);

	return r_evt;
}

/**
 * scmi_process_event_payload()  - Dequeue and process an event payload
 * @eq: The queue to use
 * @pd: The protocol descriptor to use
 * @r_evt: The registered event descriptor to use
 *
 * Read an event payload from the protocol queue into the dedicated scratch
 * buffer, fills a custom report and then look for matching event handlers and
 * call them; skip any unknown event (as marked by scmi_process_event_header())
 * and in case an anomalously sized read is detected just flush the queue.
 *
 * Return: False when the queue is empty
 */
static inline bool
scmi_process_event_payload(struct events_queue *eq,
			   struct scmi_registered_events_desc *pd,
			   struct scmi_registered_event *r_evt)
{
	u32 src_id, key;
	unsigned int outs;
	void *report = NULL;

	outs = kfifo_out(&eq->kfifo, pd->eh->payld, pd->eh->payld_sz);
	if (!outs)
		return false;

	/* Any in-flight event has now been officially processed */
	pd->in_flight = NULL;

	if (outs != pd->eh->payld_sz) {
		dev_err(pd->ni->handle->dev, "corrupted EVT Payload. Flush.\n");
		kfifo_reset_out(&eq->kfifo);
		return false;
	}

	if (IS_ERR(r_evt)) {
		dev_warn(pd->ni->handle->dev,
			 "SKIP UNKNOWN EVT - proto:%X  evt:%d\n",
			 pd->id, pd->eh->evt_id);
		return true;
	}

	report = REVT_FILL_REPORT(r_evt, pd->eh->evt_id, pd->eh->timestamp,
				  pd->eh->payld, pd->eh->payld_sz,
				  r_evt->report, &src_id);
	if (!report) {
		dev_err(pd->ni->handle->dev,
			"report not available - proto:%X  evt:%d\n",
			pd->id, pd->eh->evt_id);
		return true;
	}

	/* At first search for a generic ALL src_ids handler... */
	key = MAKE_ALL_SRCS_KEY(pd->id, pd->eh->evt_id);
	scmi_lookup_and_call_event_chain(pd->ni, key, report);

	/* ...then search for any specific src_id */
	key = MAKE_HASH_KEY(pd->id, pd->eh->evt_id, src_id);
	scmi_lookup_and_call_event_chain(pd->ni, key, report);

	return true;
}

/**
 * scmi_events_dispatcher()  - Common worker logic for all work items.
 * @work: The work item to use, which is associated to a dedicated events_queue
 *
 * Logic:
 *  1. dequeue one pending RX notification (queued in SCMI RX ISR context)
 *  2. generate a custom event report from the received event message
 *  3. lookup for any registered ALL_SRC_IDs handler:
 *    - > call the related notification chain passing in the report
 *  4. lookup for any registered specific SRC_ID handler:
 *    - > call the related notification chain passing in the report
 *
 * Note that:
 * * a dedicated per-protocol kfifo queue is used: in this way an anomalous
 *   flood of events cannot saturate other protocols' queues.
 * * each per-protocol queue is associated to a distinct work_item, which
 *   means, in turn, that:
 *   + all protocols can process their dedicated queues concurrently
 *     (since notify_wq:max_active != 1)
 *   + anyway at most one worker instance is allowed to run on the same queue
 *     concurrently: this ensures that we can have only one concurrent
 *     reader/writer on the associated kfifo, so that we can use it lock-less
 *
 * Context: Process context.
 */
static void scmi_events_dispatcher(struct work_struct *work)
{
	struct events_queue *eq;
	struct scmi_registered_events_desc *pd;
	struct scmi_registered_event *r_evt;

	eq = container_of(work, struct events_queue, notify_work);
	pd = container_of(eq, struct scmi_registered_events_desc, equeue);
	/*
	 * In order to keep the queue lock-less and the number of memcopies
	 * to the bare minimum needed, the dispatcher accounts for the
	 * possibility of per-protocol in-flight events: i.e. an event whose
	 * reception could end up being split across two subsequent runs of this
	 * worker, first the header, then the payload.
	 */
	do {
		if (!pd->in_flight) {
			r_evt = scmi_process_event_header(eq, pd);
			if (!r_evt)
				break;
			pd->in_flight = r_evt;
		} else {
			r_evt = pd->in_flight;
		}
	} while (scmi_process_event_payload(eq, pd, r_evt));
}

/**
 * scmi_notify()  - Queues a notification for further deferred processing
 * @handle: The handle identifying the platform instance from which the
 *	    dispatched event is generated
 * @proto_id: Protocol ID
 * @evt_id: Event ID (msgID)
 * @buf: Event Message Payload (without the header)
 * @len: Event Message Payload size
 * @ts: RX Timestamp in nanoseconds (boottime)
 *
 * Context: Called in interrupt context to queue a received event for
 * deferred processing.
 *
 * Return: 0 on Success
 */
int scmi_notify(const struct scmi_handle *handle, u8 proto_id, u8 evt_id,
		const void *buf, size_t len, ktime_t ts)
{
	struct scmi_registered_event *r_evt;
	struct scmi_event_header eh;
	struct scmi_notify_instance *ni;

	ni = scmi_notification_instance_data_get(handle);
	if (!ni)
		return 0;

	r_evt = SCMI_GET_REVT(ni, proto_id, evt_id);
	if (!r_evt)
		return -EINVAL;

	if (len > r_evt->evt->max_payld_sz) {
		dev_err(handle->dev, "discard badly sized message\n");
		return -EINVAL;
	}
	if (kfifo_avail(&r_evt->proto->equeue.kfifo) < sizeof(eh) + len) {
		dev_warn(handle->dev,
			 "queue full, dropping proto_id:%d  evt_id:%d  ts:%lld\n",
			 proto_id, evt_id, ktime_to_ns(ts));
		return -ENOMEM;
	}

	eh.timestamp = ts;
	eh.evt_id = evt_id;
	eh.payld_sz = len;
	/*
	 * Header and payload are enqueued with two distinct kfifo_in() (so non
	 * atomic), but this situation is handled properly on the consumer side
	 * with in-flight events tracking.
	 */
	kfifo_in(&r_evt->proto->equeue.kfifo, &eh, sizeof(eh));
	kfifo_in(&r_evt->proto->equeue.kfifo, buf, len);
	/*
	 * Don't care about return value here since we just want to ensure that
	 * a work is queued all the times whenever some items have been pushed
	 * on the kfifo:
	 * - if work was already queued it will simply fail to queue a new one
	 *   since it is not needed
	 * - if work was not queued already it will be now, even in case work
	 *   was in fact already running: this behavior avoids any possible race
	 *   when this function pushes new items onto the kfifos after the
	 *   related executing worker had already determined the kfifo to be
	 *   empty and it was terminating.
	 */
	queue_work(r_evt->proto->equeue.wq,
		   &r_evt->proto->equeue.notify_work);

	return 0;
}

/**
 * scmi_kfifo_free()  - Devres action helper to free the kfifo
 * @kfifo: The kfifo to free
 */
static void scmi_kfifo_free(void *kfifo)
{
	kfifo_free((struct kfifo *)kfifo);
}

/**
 * scmi_initialize_events_queue()  - Allocate/Initialize a kfifo buffer
 * @ni: A reference to the notification instance to use
 * @equeue: The events_queue to initialize
 * @sz: Size of the kfifo buffer to allocate
 *
 * Allocate a buffer for the kfifo and initialize it.
 *
 * Return: 0 on Success
 */
static int scmi_initialize_events_queue(struct scmi_notify_instance *ni,
					struct events_queue *equeue, size_t sz)
{
	int ret;

	if (kfifo_alloc(&equeue->kfifo, sz, GFP_KERNEL))
		return -ENOMEM;
	/* Size could have been roundup to power-of-two */
	equeue->sz = kfifo_size(&equeue->kfifo);

	ret = devm_add_action_or_reset(ni->handle->dev, scmi_kfifo_free,
				       &equeue->kfifo);
	if (ret)
		return ret;

	INIT_WORK(&equeue->notify_work, scmi_events_dispatcher);
	equeue->wq = ni->notify_wq;

	return ret;
}

/**
 * scmi_allocate_registered_events_desc()  - Allocate a registered events'
 * descriptor
 * @ni: A reference to the &struct scmi_notify_instance notification instance
 *	to use
 * @proto_id: Protocol ID
 * @queue_sz: Size of the associated queue to allocate
 * @eh_sz: Size of the event header scratch area to pre-allocate
 * @num_events: Number of events to support (size of @registered_events)
 * @ops: Pointer to a struct holding references to protocol specific helpers
 *	 needed during events handling
 *
 * It is supposed to be called only once for each protocol at protocol
 * initialization time, so it warns if the requested protocol is found already
 * registered.
 *
 * Return: The allocated and registered descriptor on Success
 */
static struct scmi_registered_events_desc *
scmi_allocate_registered_events_desc(struct scmi_notify_instance *ni,
				     u8 proto_id, size_t queue_sz, size_t eh_sz,
				     int num_events,
				     const struct scmi_event_ops *ops)
{
	int ret;
	struct scmi_registered_events_desc *pd;

	/* Ensure protocols are up to date */
	smp_rmb();
	if (WARN_ON(ni->registered_protocols[proto_id]))
		return ERR_PTR(-EINVAL);

	pd = devm_kzalloc(ni->handle->dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return ERR_PTR(-ENOMEM);
	pd->id = proto_id;
	pd->ops = ops;
	pd->ni = ni;

	ret = scmi_initialize_events_queue(ni, &pd->equeue, queue_sz);
	if (ret)
		return ERR_PTR(ret);

	pd->eh = devm_kzalloc(ni->handle->dev, eh_sz, GFP_KERNEL);
	if (!pd->eh)
		return ERR_PTR(-ENOMEM);
	pd->eh_sz = eh_sz;

	pd->registered_events = devm_kcalloc(ni->handle->dev, num_events,
					     sizeof(char *), GFP_KERNEL);
	if (!pd->registered_events)
		return ERR_PTR(-ENOMEM);
	pd->num_events = num_events;

	/* Initialize per protocol handlers table */
	mutex_init(&pd->registered_mtx);
	hash_init(pd->registered_events_handlers);

	return pd;
}

/**
 * scmi_register_protocol_events()  - Register Protocol Events with the core
 * @handle: The handle identifying the platform instance against which the
 *	    protocol's events are registered
 * @proto_id: Protocol ID
 * @ph: SCMI protocol handle.
 * @ee: A structure describing the events supported by this protocol.
 *
 * Used by SCMI Protocols initialization code to register with the notification
 * core the list of supported events and their descriptors: takes care to
 * pre-allocate and store all needed descriptors, scratch buffers and event
 * queues.
 *
 * Return: 0 on Success
 */
int scmi_register_protocol_events(const struct scmi_handle *handle, u8 proto_id,
				  const struct scmi_protocol_handle *ph,
				  const struct scmi_protocol_events *ee)
{
	int i;
	unsigned int num_sources;
	size_t payld_sz = 0;
	struct scmi_registered_events_desc *pd;
	struct scmi_notify_instance *ni;
	const struct scmi_event *evt;

	if (!ee || !ee->ops || !ee->evts || !ph ||
	    (!ee->num_sources && !ee->ops->get_num_sources))
		return -EINVAL;

	ni = scmi_notification_instance_data_get(handle);
	if (!ni)
		return -ENOMEM;

	/* num_sources cannot be <= 0 */
	if (ee->num_sources) {
		num_sources = ee->num_sources;
	} else {
		int nsrc = ee->ops->get_num_sources(ph);

		if (nsrc <= 0)
			return -EINVAL;
		num_sources = nsrc;
	}

	evt = ee->evts;
	for (i = 0; i < ee->num_events; i++)
		payld_sz = max_t(size_t, payld_sz, evt[i].max_payld_sz);
	payld_sz += sizeof(struct scmi_event_header);

	pd = scmi_allocate_registered_events_desc(ni, proto_id, ee->queue_sz,
						  payld_sz, ee->num_events,
						  ee->ops);
	if (IS_ERR(pd))
		return PTR_ERR(pd);

	pd->ph = ph;
	for (i = 0; i < ee->num_events; i++, evt++) {
		struct scmi_registered_event *r_evt;

		r_evt = devm_kzalloc(ni->handle->dev, sizeof(*r_evt),
				     GFP_KERNEL);
		if (!r_evt)
			return -ENOMEM;
		r_evt->proto = pd;
		r_evt->evt = evt;

		r_evt->sources = devm_kcalloc(ni->handle->dev, num_sources,
					      sizeof(refcount_t), GFP_KERNEL);
		if (!r_evt->sources)
			return -ENOMEM;
		r_evt->num_sources = num_sources;
		mutex_init(&r_evt->sources_mtx);

		r_evt->report = devm_kzalloc(ni->handle->dev,
					     evt->max_report_sz, GFP_KERNEL);
		if (!r_evt->report)
			return -ENOMEM;

		pd->registered_events[i] = r_evt;
		/* Ensure events are updated */
		smp_wmb();
		dev_dbg(handle->dev, "registered event - %lX\n",
			MAKE_ALL_SRCS_KEY(r_evt->proto->id, r_evt->evt->id));
	}

	/* Register protocol and events...it will never be removed */
	ni->registered_protocols[proto_id] = pd;
	/* Ensure protocols are updated */
	smp_wmb();

	/*
	 * Finalize any pending events' handler which could have been waiting
	 * for this protocol's events registration.
	 */
	schedule_work(&ni->init_work);

	return 0;
}

/**
 * scmi_deregister_protocol_events  - Deregister protocol events with the core
 * @handle: The handle identifying the platform instance against which the
 *	    protocol's events are registered
 * @proto_id: Protocol ID
 */
void scmi_deregister_protocol_events(const struct scmi_handle *handle,
				     u8 proto_id)
{
	struct scmi_notify_instance *ni;
	struct scmi_registered_events_desc *pd;

	ni = scmi_notification_instance_data_get(handle);
	if (!ni)
		return;

	pd = ni->registered_protocols[proto_id];
	if (!pd)
		return;

	ni->registered_protocols[proto_id] = NULL;
	/* Ensure protocols are updated */
	smp_wmb();

	cancel_work_sync(&pd->equeue.notify_work);
}

/**
 * scmi_allocate_event_handler()  - Allocate Event handler
 * @ni: A reference to the notification instance to use
 * @evt_key: 32bit key uniquely bind to the event identified by the tuple
 *	     (proto_id, evt_id, src_id)
 *
 * Allocate an event handler and related notification chain associated with
 * the provided event handler key.
 * Note that, at this point, a related registered_event is still to be
 * associated to this handler descriptor (hndl->r_evt == NULL), so the handler
 * is initialized as pending.
 *
 * Context: Assumes to be called with @pending_mtx already acquired.
 * Return: the freshly allocated structure on Success
 */
static struct scmi_event_handler *
scmi_allocate_event_handler(struct scmi_notify_instance *ni, u32 evt_key)
{
	struct scmi_event_handler *hndl;

	hndl = kzalloc(sizeof(*hndl), GFP_KERNEL);
	if (!hndl)
		return NULL;
	hndl->key = evt_key;
	BLOCKING_INIT_NOTIFIER_HEAD(&hndl->chain);
	refcount_set(&hndl->users, 1);
	/* New handlers are created pending */
	hash_add(ni->pending_events_handlers, &hndl->hash, hndl->key);

	return hndl;
}

/**
 * scmi_free_event_handler()  - Free the provided Event handler
 * @hndl: The event handler structure to free
 *
 * Context: Assumes to be called with proper locking acquired depending
 *	    on the situation.
 */
static void scmi_free_event_handler(struct scmi_event_handler *hndl)
{
	hash_del(&hndl->hash);
	kfree(hndl);
}

/**
 * scmi_bind_event_handler()  - Helper to attempt binding an handler to an event
 * @ni: A reference to the notification instance to use
 * @hndl: The event handler to bind
 *
 * If an associated registered event is found, move the handler from the pending
 * into the registered table.
 *
 * Context: Assumes to be called with @pending_mtx already acquired.
 *
 * Return: 0 on Success
 */
static inline int scmi_bind_event_handler(struct scmi_notify_instance *ni,
					  struct scmi_event_handler *hndl)
{
	struct scmi_registered_event *r_evt;

	r_evt = SCMI_GET_REVT(ni, KEY_XTRACT_PROTO_ID(hndl->key),
			      KEY_XTRACT_EVT_ID(hndl->key));
	if (!r_evt)
		return -EINVAL;

	/*
	 * Remove from pending and insert into registered while getting hold
	 * of protocol instance.
	 */
	hash_del(&hndl->hash);
	/*
	 * Acquire protocols only for NON pending handlers, so as NOT to trigger
	 * protocol initialization when a notifier is registered against a still
	 * not registered protocol, since it would make little sense to force init
	 * protocols for which still no SCMI driver user exists: they wouldn't
	 * emit any event anyway till some SCMI driver starts using it.
	 */
	scmi_protocol_acquire(ni->handle, KEY_XTRACT_PROTO_ID(hndl->key));
	hndl->r_evt = r_evt;

	mutex_lock(&r_evt->proto->registered_mtx);
	hash_add(r_evt->proto->registered_events_handlers,
		 &hndl->hash, hndl->key);
	mutex_unlock(&r_evt->proto->registered_mtx);

	return 0;
}

/**
 * scmi_valid_pending_handler()  - Helper to check pending status of handlers
 * @ni: A reference to the notification instance to use
 * @hndl: The event handler to check
 *
 * An handler is considered pending when its r_evt == NULL, because the related
 * event was still unknown at handler's registration time; anyway, since all
 * protocols register their supported events once for all at protocols'
 * initialization time, a pending handler cannot be considered valid anymore if
 * the underlying event (which it is waiting for), belongs to an already
 * initialized and registered protocol.
 *
 * Return: 0 on Success
 */
static inline int scmi_valid_pending_handler(struct scmi_notify_instance *ni,
					     struct scmi_event_handler *hndl)
{
	struct scmi_registered_events_desc *pd;

	if (!IS_HNDL_PENDING(hndl))
		return -EINVAL;

	pd = SCMI_GET_PROTO(ni, KEY_XTRACT_PROTO_ID(hndl->key));
	if (pd)
		return -EINVAL;

	return 0;
}

/**
 * scmi_register_event_handler()  - Register whenever possible an Event handler
 * @ni: A reference to the notification instance to use
 * @hndl: The event handler to register
 *
 * At first try to bind an event handler to its associated event, then check if
 * it was at least a valid pending handler: if it was not bound nor valid return
 * false.
 *
 * Valid pending incomplete bindings will be periodically retried by a dedicated
 * worker which is kicked each time a new protocol completes its own
 * registration phase.
 *
 * Context: Assumes to be called with @pending_mtx acquired.
 *
 * Return: 0 on Success
 */
static int scmi_register_event_handler(struct scmi_notify_instance *ni,
				       struct scmi_event_handler *hndl)
{
	int ret;

	ret = scmi_bind_event_handler(ni, hndl);
	if (!ret) {
		dev_dbg(ni->handle->dev, "registered NEW handler - key:%X\n",
			hndl->key);
	} else {
		ret = scmi_valid_pending_handler(ni, hndl);
		if (!ret)
			dev_dbg(ni->handle->dev,
				"registered PENDING handler - key:%X\n",
				hndl->key);
	}

	return ret;
}

/**
 * __scmi_event_handler_get_ops()  - Utility to get or create an event handler
 * @ni: A reference to the notification instance to use
 * @evt_key: The event key to use
 * @create: A boolean flag to specify if a handler must be created when
 *	    not already existent
 *
 * Search for the desired handler matching the key in both the per-protocol
 * registered table and the common pending table:
 * * if found adjust users refcount
 * * if not found and @create is true, create and register the new handler:
 *   handler could end up being registered as pending if no matching event
 *   could be found.
 *
 * An handler is guaranteed to reside in one and only one of the tables at
 * any one time; to ensure this the whole search and create is performed
 * holding the @pending_mtx lock, with @registered_mtx additionally acquired
 * if needed.
 *
 * Note that when a nested acquisition of these mutexes is needed the locking
 * order is always (same as in @init_work):
 * 1. pending_mtx
 * 2. registered_mtx
 *
 * Events generation is NOT enabled right after creation within this routine
 * since at creation time we usually want to have all setup and ready before
 * events really start flowing.
 *
 * Return: A properly refcounted handler on Success, NULL on Failure
 */
static inline struct scmi_event_handler *
__scmi_event_handler_get_ops(struct scmi_notify_instance *ni,
			     u32 evt_key, bool create)
{
	struct scmi_registered_event *r_evt;
	struct scmi_event_handler *hndl = NULL;

	r_evt = SCMI_GET_REVT(ni, KEY_XTRACT_PROTO_ID(evt_key),
			      KEY_XTRACT_EVT_ID(evt_key));

	mutex_lock(&ni->pending_mtx);
	/* Search registered events at first ... if possible at all */
	if (r_evt) {
		mutex_lock(&r_evt->proto->registered_mtx);
		hndl = KEY_FIND(r_evt->proto->registered_events_handlers,
				hndl, evt_key);
		if (hndl)
			refcount_inc(&hndl->users);
		mutex_unlock(&r_evt->proto->registered_mtx);
	}

	/* ...then amongst pending. */
	if (!hndl) {
		hndl = KEY_FIND(ni->pending_events_handlers, hndl, evt_key);
		if (hndl)
			refcount_inc(&hndl->users);
	}

	/* Create if still not found and required */
	if (!hndl && create) {
		hndl = scmi_allocate_event_handler(ni, evt_key);
		if (hndl && scmi_register_event_handler(ni, hndl)) {
			dev_dbg(ni->handle->dev,
				"purging UNKNOWN handler - key:%X\n",
				hndl->key);
			/* this hndl can be only a pending one */
			scmi_put_handler_unlocked(ni, hndl);
			hndl = NULL;
		}
	}
	mutex_unlock(&ni->pending_mtx);

	return hndl;
}

static struct scmi_event_handler *
scmi_get_handler(struct scmi_notify_instance *ni, u32 evt_key)
{
	return __scmi_event_handler_get_ops(ni, evt_key, false);
}

static struct scmi_event_handler *
scmi_get_or_create_handler(struct scmi_notify_instance *ni, u32 evt_key)
{
	return __scmi_event_handler_get_ops(ni, evt_key, true);
}

/**
 * scmi_get_active_handler()  - Helper to get active handlers only
 * @ni: A reference to the notification instance to use
 * @evt_key: The event key to use
 *
 * Search for the desired handler matching the key only in the per-protocol
 * table of registered handlers: this is called only from the dispatching path
 * so want to be as quick as possible and do not care about pending.
 *
 * Return: A properly refcounted active handler
 */
static struct scmi_event_handler *
scmi_get_active_handler(struct scmi_notify_instance *ni, u32 evt_key)
{
	struct scmi_registered_event *r_evt;
	struct scmi_event_handler *hndl = NULL;

	r_evt = SCMI_GET_REVT(ni, KEY_XTRACT_PROTO_ID(evt_key),
			      KEY_XTRACT_EVT_ID(evt_key));
	if (r_evt) {
		mutex_lock(&r_evt->proto->registered_mtx);
		hndl = KEY_FIND(r_evt->proto->registered_events_handlers,
				hndl, evt_key);
		if (hndl)
			refcount_inc(&hndl->users);
		mutex_unlock(&r_evt->proto->registered_mtx);
	}

	return hndl;
}

/**
 * __scmi_enable_evt()  - Enable/disable events generation
 * @r_evt: The registered event to act upon
 * @src_id: The src_id to act upon
 * @enable: The action to perform: true->Enable, false->Disable
 *
 * Takes care of proper refcounting while performing enable/disable: handles
 * the special case of ALL sources requests by itself.
 * Returns successfully if at least one of the required src_id has been
 * successfully enabled/disabled.
 *
 * Return: 0 on Success
 */
static inline int __scmi_enable_evt(struct scmi_registered_event *r_evt,
				    u32 src_id, bool enable)
{
	int retvals = 0;
	u32 num_sources;
	refcount_t *sid;

	if (src_id == SRC_ID_MASK) {
		src_id = 0;
		num_sources = r_evt->num_sources;
	} else if (src_id < r_evt->num_sources) {
		num_sources = 1;
	} else {
		return -EINVAL;
	}

	mutex_lock(&r_evt->sources_mtx);
	if (enable) {
		for (; num_sources; src_id++, num_sources--) {
			int ret = 0;

			sid = &r_evt->sources[src_id];
			if (refcount_read(sid) == 0) {
				ret = REVT_NOTIFY_ENABLE(r_evt, r_evt->evt->id,
							 src_id);
				if (!ret)
					refcount_set(sid, 1);
			} else {
				refcount_inc(sid);
			}
			retvals += !ret;
		}
	} else {
		for (; num_sources; src_id++, num_sources--) {
			sid = &r_evt->sources[src_id];
			if (refcount_dec_and_test(sid))
				REVT_NOTIFY_DISABLE(r_evt,
						    r_evt->evt->id, src_id);
		}
		retvals = 1;
	}
	mutex_unlock(&r_evt->sources_mtx);

	return retvals ? 0 : -EINVAL;
}

static int scmi_enable_events(struct scmi_event_handler *hndl)
{
	int ret = 0;

	if (!hndl->enabled) {
		ret = __scmi_enable_evt(hndl->r_evt,
					KEY_XTRACT_SRC_ID(hndl->key), true);
		if (!ret)
			hndl->enabled = true;
	}

	return ret;
}

static int scmi_disable_events(struct scmi_event_handler *hndl)
{
	int ret = 0;

	if (hndl->enabled) {
		ret = __scmi_enable_evt(hndl->r_evt,
					KEY_XTRACT_SRC_ID(hndl->key), false);
		if (!ret)
			hndl->enabled = false;
	}

	return ret;
}

/**
 * scmi_put_handler_unlocked()  - Put an event handler
 * @ni: A reference to the notification instance to use
 * @hndl: The event handler to act upon
 *
 * After having got exclusive access to the registered handlers hashtable,
 * update the refcount and if @hndl is no more in use by anyone:
 * * ask for events' generation disabling
 * * unregister and free the handler itself
 *
 * Context: Assumes all the proper locking has been managed by the caller.
 *
 * Return: True if handler was freed (users dropped to zero)
 */
static bool scmi_put_handler_unlocked(struct scmi_notify_instance *ni,
				      struct scmi_event_handler *hndl)
{
	bool freed = false;

	if (refcount_dec_and_test(&hndl->users)) {
		if (!IS_HNDL_PENDING(hndl))
			scmi_disable_events(hndl);
		scmi_free_event_handler(hndl);
		freed = true;
	}

	return freed;
}

static void scmi_put_handler(struct scmi_notify_instance *ni,
			     struct scmi_event_handler *hndl)
{
	bool freed;
	u8 protocol_id;
	struct scmi_registered_event *r_evt = hndl->r_evt;

	mutex_lock(&ni->pending_mtx);
	if (r_evt) {
		protocol_id = r_evt->proto->id;
		mutex_lock(&r_evt->proto->registered_mtx);
	}

	freed = scmi_put_handler_unlocked(ni, hndl);

	if (r_evt) {
		mutex_unlock(&r_evt->proto->registered_mtx);
		/*
		 * Only registered handler acquired protocol; must be here
		 * released only AFTER unlocking registered_mtx, since
		 * releasing a protocol can trigger its de-initialization
		 * (ie. including r_evt and registered_mtx)
		 */
		if (freed)
			scmi_protocol_release(ni->handle, protocol_id);
	}
	mutex_unlock(&ni->pending_mtx);
}

static void scmi_put_active_handler(struct scmi_notify_instance *ni,
				    struct scmi_event_handler *hndl)
{
	bool freed;
	struct scmi_registered_event *r_evt = hndl->r_evt;
	u8 protocol_id = r_evt->proto->id;

	mutex_lock(&r_evt->proto->registered_mtx);
	freed = scmi_put_handler_unlocked(ni, hndl);
	mutex_unlock(&r_evt->proto->registered_mtx);
	if (freed)
		scmi_protocol_release(ni->handle, protocol_id);
}

/**
 * scmi_event_handler_enable_events()  - Enable events associated to an handler
 * @hndl: The Event handler to act upon
 *
 * Return: 0 on Success
 */
static int scmi_event_handler_enable_events(struct scmi_event_handler *hndl)
{
	if (scmi_enable_events(hndl)) {
		pr_err("Failed to ENABLE events for key:%X !\n", hndl->key);
		return -EINVAL;
	}

	return 0;
}

/**
 * scmi_notifier_register()  - Register a notifier_block for an event
 * @handle: The handle identifying the platform instance against which the
 *	    callback is registered
 * @proto_id: Protocol ID
 * @evt_id: Event ID
 * @src_id: Source ID, when NULL register for events coming form ALL possible
 *	    sources
 * @nb: A standard notifier block to register for the specified event
 *
 * Generic helper to register a notifier_block against a protocol event.
 *
 * A notifier_block @nb will be registered for each distinct event identified
 * by the tuple (proto_id, evt_id, src_id) on a dedicated notification chain
 * so that:
 *
 *	(proto_X, evt_Y, src_Z) --> chain_X_Y_Z
 *
 * @src_id meaning is protocol specific and identifies the origin of the event
 * (like domain_id, sensor_id and so forth).
 *
 * @src_id can be NULL to signify that the caller is interested in receiving
 * notifications from ALL the available sources for that protocol OR simply that
 * the protocol does not support distinct sources.
 *
 * As soon as one user for the specified tuple appears, an handler is created,
 * and that specific event's generation is enabled at the platform level, unless
 * an associated registered event is found missing, meaning that the needed
 * protocol is still to be initialized and the handler has just been registered
 * as still pending.
 *
 * Return: 0 on Success
 */
static int scmi_notifier_register(const struct scmi_handle *handle,
				  u8 proto_id, u8 evt_id, const u32 *src_id,
				  struct notifier_block *nb)
{
	int ret = 0;
	u32 evt_key;
	struct scmi_event_handler *hndl;
	struct scmi_notify_instance *ni;

	ni = scmi_notification_instance_data_get(handle);
	if (!ni)
		return -ENODEV;

	evt_key = MAKE_HASH_KEY(proto_id, evt_id,
				src_id ? *src_id : SRC_ID_MASK);
	hndl = scmi_get_or_create_handler(ni, evt_key);
	if (!hndl)
		return -EINVAL;

	blocking_notifier_chain_register(&hndl->chain, nb);

	/* Enable events for not pending handlers */
	if (!IS_HNDL_PENDING(hndl)) {
		ret = scmi_event_handler_enable_events(hndl);
		if (ret)
			scmi_put_handler(ni, hndl);
	}

	return ret;
}

/**
 * scmi_notifier_unregister()  - Unregister a notifier_block for an event
 * @handle: The handle identifying the platform instance against which the
 *	    callback is unregistered
 * @proto_id: Protocol ID
 * @evt_id: Event ID
 * @src_id: Source ID
 * @nb: The notifier_block to unregister
 *
 * Takes care to unregister the provided @nb from the notification chain
 * associated to the specified event and, if there are no more users for the
 * event handler, frees also the associated event handler structures.
 * (this could possibly cause disabling of event's generation at platform level)
 *
 * Return: 0 on Success
 */
static int scmi_notifier_unregister(const struct scmi_handle *handle,
				    u8 proto_id, u8 evt_id, const u32 *src_id,
				    struct notifier_block *nb)
{
	u32 evt_key;
	struct scmi_event_handler *hndl;
	struct scmi_notify_instance *ni;

	ni = scmi_notification_instance_data_get(handle);
	if (!ni)
		return -ENODEV;

	evt_key = MAKE_HASH_KEY(proto_id, evt_id,
				src_id ? *src_id : SRC_ID_MASK);
	hndl = scmi_get_handler(ni, evt_key);
	if (!hndl)
		return -EINVAL;

	/*
	 * Note that this chain unregistration call is safe on its own
	 * being internally protected by an rwsem.
	 */
	blocking_notifier_chain_unregister(&hndl->chain, nb);
	scmi_put_handler(ni, hndl);

	/*
	 * This balances the initial get issued in @scmi_notifier_register.
	 * If this notifier_block happened to be the last known user callback
	 * for this event, the handler is here freed and the event's generation
	 * stopped.
	 *
	 * Note that, an ongoing concurrent lookup on the delivery workqueue
	 * path could still hold the refcount to 1 even after this routine
	 * completes: in such a case it will be the final put on the delivery
	 * path which will finally free this unused handler.
	 */
	scmi_put_handler(ni, hndl);

	return 0;
}

struct scmi_notifier_devres {
	const struct scmi_handle *handle;
	u8 proto_id;
	u8 evt_id;
	u32 __src_id;
	u32 *src_id;
	struct notifier_block *nb;
};

static void scmi_devm_release_notifier(struct device *dev, void *res)
{
	struct scmi_notifier_devres *dres = res;

	scmi_notifier_unregister(dres->handle, dres->proto_id, dres->evt_id,
				 dres->src_id, dres->nb);
}

/**
 * scmi_devm_notifier_register()  - Managed registration of a notifier_block
 * for an event
 * @sdev: A reference to an scmi_device whose embedded struct device is to
 *	  be used for devres accounting.
 * @proto_id: Protocol ID
 * @evt_id: Event ID
 * @src_id: Source ID, when NULL register for events coming form ALL possible
 *	    sources
 * @nb: A standard notifier block to register for the specified event
 *
 * Generic devres managed helper to register a notifier_block against a
 * protocol event.
 *
 * Return: 0 on Success
 */
static int scmi_devm_notifier_register(struct scmi_device *sdev,
				       u8 proto_id, u8 evt_id,
				       const u32 *src_id,
				       struct notifier_block *nb)
{
	int ret;
	struct scmi_notifier_devres *dres;

	dres = devres_alloc(scmi_devm_release_notifier,
			    sizeof(*dres), GFP_KERNEL);
	if (!dres)
		return -ENOMEM;

	ret = scmi_notifier_register(sdev->handle, proto_id,
				     evt_id, src_id, nb);
	if (ret) {
		devres_free(dres);
		return ret;
	}

	dres->handle = sdev->handle;
	dres->proto_id = proto_id;
	dres->evt_id = evt_id;
	dres->nb = nb;
	if (src_id) {
		dres->__src_id = *src_id;
		dres->src_id = &dres->__src_id;
	} else {
		dres->src_id = NULL;
	}
	devres_add(&sdev->dev, dres);

	return ret;
}

static int scmi_devm_notifier_match(struct device *dev, void *res, void *data)
{
	struct scmi_notifier_devres *dres = res;
	struct scmi_notifier_devres *xres = data;

	if (WARN_ON(!dres || !xres))
		return 0;

	return dres->proto_id == xres->proto_id &&
		dres->evt_id == xres->evt_id &&
		dres->nb == xres->nb &&
		((!dres->src_id && !xres->src_id) ||
		  (dres->src_id && xres->src_id &&
		   dres->__src_id == xres->__src_id));
}

/**
 * scmi_devm_notifier_unregister()  - Managed un-registration of a
 * notifier_block for an event
 * @sdev: A reference to an scmi_device whose embedded struct device is to
 *	  be used for devres accounting.
 * @proto_id: Protocol ID
 * @evt_id: Event ID
 * @src_id: Source ID, when NULL register for events coming form ALL possible
 *	    sources
 * @nb: A standard notifier block to register for the specified event
 *
 * Generic devres managed helper to explicitly un-register a notifier_block
 * against a protocol event, which was previously registered using the above
 * @scmi_devm_notifier_register.
 *
 * Return: 0 on Success
 */
static int scmi_devm_notifier_unregister(struct scmi_device *sdev,
					 u8 proto_id, u8 evt_id,
					 const u32 *src_id,
					 struct notifier_block *nb)
{
	int ret;
	struct scmi_notifier_devres dres;

	dres.handle = sdev->handle;
	dres.proto_id = proto_id;
	dres.evt_id = evt_id;
	if (src_id) {
		dres.__src_id = *src_id;
		dres.src_id = &dres.__src_id;
	} else {
		dres.src_id = NULL;
	}

	ret = devres_release(&sdev->dev, scmi_devm_release_notifier,
			     scmi_devm_notifier_match, &dres);

	WARN_ON(ret);

	return ret;
}

/**
 * scmi_protocols_late_init()  - Worker for late initialization
 * @work: The work item to use associated to the proper SCMI instance
 *
 * This kicks in whenever a new protocol has completed its own registration via
 * scmi_register_protocol_events(): it is in charge of scanning the table of
 * pending handlers (registered by users while the related protocol was still
 * not initialized) and finalizing their initialization whenever possible;
 * invalid pending handlers are purged at this point in time.
 */
static void scmi_protocols_late_init(struct work_struct *work)
{
	int bkt;
	struct scmi_event_handler *hndl;
	struct scmi_notify_instance *ni;
	struct hlist_node *tmp;

	ni = container_of(work, struct scmi_notify_instance, init_work);

	/* Ensure protocols and events are up to date */
	smp_rmb();

	mutex_lock(&ni->pending_mtx);
	hash_for_each_safe(ni->pending_events_handlers, bkt, tmp, hndl, hash) {
		int ret;

		ret = scmi_bind_event_handler(ni, hndl);
		if (!ret) {
			dev_dbg(ni->handle->dev,
				"finalized PENDING handler - key:%X\n",
				hndl->key);
			ret = scmi_event_handler_enable_events(hndl);
			if (ret) {
				dev_dbg(ni->handle->dev,
					"purging INVALID handler - key:%X\n",
					hndl->key);
				scmi_put_active_handler(ni, hndl);
			}
		} else {
			ret = scmi_valid_pending_handler(ni, hndl);
			if (ret) {
				dev_dbg(ni->handle->dev,
					"purging PENDING handler - key:%X\n",
					hndl->key);
				/* this hndl can be only a pending one */
				scmi_put_handler_unlocked(ni, hndl);
			}
		}
	}
	mutex_unlock(&ni->pending_mtx);
}

/*
 * notify_ops are attached to the handle so that can be accessed
 * directly from an scmi_driver to register its own notifiers.
 */
static const struct scmi_notify_ops notify_ops = {
	.devm_event_notifier_register = scmi_devm_notifier_register,
	.devm_event_notifier_unregister = scmi_devm_notifier_unregister,
	.event_notifier_register = scmi_notifier_register,
	.event_notifier_unregister = scmi_notifier_unregister,
};

/**
 * scmi_notification_init()  - Initializes Notification Core Support
 * @handle: The handle identifying the platform instance to initialize
 *
 * This function lays out all the basic resources needed by the notification
 * core instance identified by the provided handle: once done, all of the
 * SCMI Protocols can register their events with the core during their own
 * initializations.
 *
 * Note that failing to initialize the core notifications support does not
 * cause the whole SCMI Protocols stack to fail its initialization.
 *
 * SCMI Notification Initialization happens in 2 steps:
 * * initialization: basic common allocations (this function)
 * * registration: protocols asynchronously come into life and registers their
 *		   own supported list of events with the core; this causes
 *		   further per-protocol allocations
 *
 * Any user's callback registration attempt, referring a still not registered
 * event, will be registered as pending and finalized later (if possible)
 * by scmi_protocols_late_init() work.
 * This allows for lazy initialization of SCMI Protocols due to late (or
 * missing) SCMI drivers' modules loading.
 *
 * Return: 0 on Success
 */
int scmi_notification_init(struct scmi_handle *handle)
{
	void *gid;
	struct scmi_notify_instance *ni;

	gid = devres_open_group(handle->dev, NULL, GFP_KERNEL);
	if (!gid)
		return -ENOMEM;

	ni = devm_kzalloc(handle->dev, sizeof(*ni), GFP_KERNEL);
	if (!ni)
		goto err;

	ni->gid = gid;
	ni->handle = handle;

	ni->registered_protocols = devm_kcalloc(handle->dev, SCMI_MAX_PROTO,
						sizeof(char *), GFP_KERNEL);
	if (!ni->registered_protocols)
		goto err;

	ni->notify_wq = alloc_workqueue(dev_name(handle->dev),
					WQ_UNBOUND | WQ_FREEZABLE | WQ_SYSFS,
					0);
	if (!ni->notify_wq)
		goto err;

	mutex_init(&ni->pending_mtx);
	hash_init(ni->pending_events_handlers);

	INIT_WORK(&ni->init_work, scmi_protocols_late_init);

	scmi_notification_instance_data_set(handle, ni);
	handle->notify_ops = &notify_ops;
	/* Ensure handle is up to date */
	smp_wmb();

	dev_info(handle->dev, "Core Enabled.\n");

	devres_close_group(handle->dev, ni->gid);

	return 0;

err:
	dev_warn(handle->dev, "Initialization Failed.\n");
	devres_release_group(handle->dev, gid);
	return -ENOMEM;
}

/**
 * scmi_notification_exit()  - Shutdown and clean Notification core
 * @handle: The handle identifying the platform instance to shutdown
 */
void scmi_notification_exit(struct scmi_handle *handle)
{
	struct scmi_notify_instance *ni;

	ni = scmi_notification_instance_data_get(handle);
	if (!ni)
		return;
	scmi_notification_instance_data_set(handle, NULL);

	/* Destroy while letting pending work complete */
	destroy_workqueue(ni->notify_wq);

	devres_release_group(ni->handle->dev, ni->gid);
}
