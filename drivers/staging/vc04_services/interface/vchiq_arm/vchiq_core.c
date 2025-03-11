// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2010-2012 Broadcom. All rights reserved. */

#include <linux/types.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/highmem.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/kref.h>
#include <linux/rcupdate.h>
#include <linux/sched/signal.h>

#include "vchiq_arm.h"
#include "vchiq_core.h"

#define VCHIQ_SLOT_HANDLER_STACK 8192

#define VCHIQ_MSG_PADDING            0  /* -                                 */
#define VCHIQ_MSG_CONNECT            1  /* -                                 */
#define VCHIQ_MSG_OPEN               2  /* + (srcport, -), fourcc, client_id */
#define VCHIQ_MSG_OPENACK            3  /* + (srcport, dstport)              */
#define VCHIQ_MSG_CLOSE              4  /* + (srcport, dstport)              */
#define VCHIQ_MSG_DATA               5  /* + (srcport, dstport)              */
#define VCHIQ_MSG_BULK_RX            6  /* + (srcport, dstport), data, size  */
#define VCHIQ_MSG_BULK_TX            7  /* + (srcport, dstport), data, size  */
#define VCHIQ_MSG_BULK_RX_DONE       8  /* + (srcport, dstport), actual      */
#define VCHIQ_MSG_BULK_TX_DONE       9  /* + (srcport, dstport), actual      */
#define VCHIQ_MSG_PAUSE             10  /* -                                 */
#define VCHIQ_MSG_RESUME            11  /* -                                 */
#define VCHIQ_MSG_REMOTE_USE        12  /* -                                 */
#define VCHIQ_MSG_REMOTE_RELEASE    13  /* -                                 */
#define VCHIQ_MSG_REMOTE_USE_ACTIVE 14  /* -                                 */

#define TYPE_SHIFT 24

#define VCHIQ_PORT_MAX                 (VCHIQ_MAX_SERVICES - 1)
#define VCHIQ_PORT_FREE                0x1000
#define VCHIQ_PORT_IS_VALID(port)      ((port) < VCHIQ_PORT_FREE)
#define VCHIQ_MAKE_MSG(type, srcport, dstport) \
	(((type) << TYPE_SHIFT) | ((srcport) << 12) | ((dstport) << 0))
#define VCHIQ_MSG_TYPE(msgid)          ((unsigned int)(msgid) >> TYPE_SHIFT)
#define VCHIQ_MSG_SRCPORT(msgid) \
	((unsigned short)(((unsigned int)(msgid) >> 12) & 0xfff))
#define VCHIQ_MSG_DSTPORT(msgid) \
	((unsigned short)(msgid) & 0xfff)

#define MAKE_CONNECT			(VCHIQ_MSG_CONNECT << TYPE_SHIFT)
#define MAKE_OPEN(srcport) \
	((VCHIQ_MSG_OPEN << TYPE_SHIFT) | ((srcport) << 12))
#define MAKE_OPENACK(srcport, dstport) \
	((VCHIQ_MSG_OPENACK << TYPE_SHIFT) | ((srcport) << 12) | ((dstport) << 0))
#define MAKE_CLOSE(srcport, dstport) \
	((VCHIQ_MSG_CLOSE << TYPE_SHIFT) | ((srcport) << 12) | ((dstport) << 0))
#define MAKE_DATA(srcport, dstport) \
	((VCHIQ_MSG_DATA << TYPE_SHIFT) | ((srcport) << 12) | ((dstport) << 0))
#define MAKE_PAUSE			(VCHIQ_MSG_PAUSE << TYPE_SHIFT)
#define MAKE_RESUME			(VCHIQ_MSG_RESUME << TYPE_SHIFT)
#define MAKE_REMOTE_USE			(VCHIQ_MSG_REMOTE_USE << TYPE_SHIFT)
#define MAKE_REMOTE_USE_ACTIVE		(VCHIQ_MSG_REMOTE_USE_ACTIVE << TYPE_SHIFT)

#define PAGELIST_WRITE			0
#define PAGELIST_READ			1
#define PAGELIST_READ_WITH_FRAGMENTS	2

#define BELL2	0x08

/* Ensure the fields are wide enough */
static_assert(VCHIQ_MSG_SRCPORT(VCHIQ_MAKE_MSG(0, 0, VCHIQ_PORT_MAX)) == 0);
static_assert(VCHIQ_MSG_TYPE(VCHIQ_MAKE_MSG(0, VCHIQ_PORT_MAX, 0)) == 0);
static_assert((unsigned int)VCHIQ_PORT_MAX < (unsigned int)VCHIQ_PORT_FREE);

#define VCHIQ_MSGID_PADDING            VCHIQ_MAKE_MSG(VCHIQ_MSG_PADDING, 0, 0)
#define VCHIQ_MSGID_CLAIMED            0x40000000

#define VCHIQ_FOURCC_INVALID           0x00000000
#define VCHIQ_FOURCC_IS_LEGAL(fourcc)  ((fourcc) != VCHIQ_FOURCC_INVALID)

#define VCHIQ_BULK_ACTUAL_ABORTED -1

#if VCHIQ_ENABLE_STATS
#define VCHIQ_STATS_INC(state, stat) (state->stats. stat++)
#define VCHIQ_SERVICE_STATS_INC(service, stat) (service->stats. stat++)
#define VCHIQ_SERVICE_STATS_ADD(service, stat, addend) \
	(service->stats. stat += addend)
#else
#define VCHIQ_STATS_INC(state, stat) ((void)0)
#define VCHIQ_SERVICE_STATS_INC(service, stat) ((void)0)
#define VCHIQ_SERVICE_STATS_ADD(service, stat, addend) ((void)0)
#endif

#define HANDLE_STATE_SHIFT 12

#define SLOT_INFO_FROM_INDEX(state, index) (state->slot_info + (index))
#define SLOT_DATA_FROM_INDEX(state, index) (state->slot_data + (index))
#define SLOT_INDEX_FROM_DATA(state, data) \
	(((unsigned int)((char *)data - (char *)state->slot_data)) / \
	VCHIQ_SLOT_SIZE)
#define SLOT_INDEX_FROM_INFO(state, info) \
	((unsigned int)(info - state->slot_info))
#define SLOT_QUEUE_INDEX_FROM_POS(pos) \
	((int)((unsigned int)(pos) / VCHIQ_SLOT_SIZE))
#define SLOT_QUEUE_INDEX_FROM_POS_MASKED(pos) \
	(SLOT_QUEUE_INDEX_FROM_POS(pos) & VCHIQ_SLOT_QUEUE_MASK)

#define BULK_INDEX(x) ((x) & (VCHIQ_NUM_SERVICE_BULKS - 1))

#define NO_CLOSE_RECVD	0
#define CLOSE_RECVD	1

#define NO_RETRY_POLL	0
#define RETRY_POLL	1

struct vchiq_open_payload {
	int fourcc;
	int client_id;
	short version;
	short version_min;
};

struct vchiq_openack_payload {
	short version;
};

enum {
	QMFLAGS_IS_BLOCKING     = BIT(0),
	QMFLAGS_NO_MUTEX_LOCK   = BIT(1),
	QMFLAGS_NO_MUTEX_UNLOCK = BIT(2)
};

enum {
	VCHIQ_POLL_TERMINATE,
	VCHIQ_POLL_REMOVE,
	VCHIQ_POLL_TXNOTIFY,
	VCHIQ_POLL_RXNOTIFY,
	VCHIQ_POLL_COUNT
};

/* we require this for consistency between endpoints */
static_assert(sizeof(struct vchiq_header) == 8);
static_assert(VCHIQ_VERSION >= VCHIQ_VERSION_MIN);

static inline void check_sizes(void)
{
	BUILD_BUG_ON_NOT_POWER_OF_2(VCHIQ_SLOT_SIZE);
	BUILD_BUG_ON_NOT_POWER_OF_2(VCHIQ_MAX_SLOTS);
	BUILD_BUG_ON_NOT_POWER_OF_2(VCHIQ_MAX_SLOTS_PER_SIDE);
	BUILD_BUG_ON_NOT_POWER_OF_2(sizeof(struct vchiq_header));
	BUILD_BUG_ON_NOT_POWER_OF_2(VCHIQ_NUM_CURRENT_BULKS);
	BUILD_BUG_ON_NOT_POWER_OF_2(VCHIQ_NUM_SERVICE_BULKS);
	BUILD_BUG_ON_NOT_POWER_OF_2(VCHIQ_MAX_SERVICES);
}

static unsigned int handle_seq;

static const char *const srvstate_names[] = {
	"FREE",
	"HIDDEN",
	"LISTENING",
	"OPENING",
	"OPEN",
	"OPENSYNC",
	"CLOSESENT",
	"CLOSERECVD",
	"CLOSEWAIT",
	"CLOSED"
};

static const char *const reason_names[] = {
	"SERVICE_OPENED",
	"SERVICE_CLOSED",
	"MESSAGE_AVAILABLE",
	"BULK_TRANSMIT_DONE",
	"BULK_RECEIVE_DONE",
	"BULK_TRANSMIT_ABORTED",
	"BULK_RECEIVE_ABORTED"
};

static const char *const conn_state_names[] = {
	"DISCONNECTED",
	"CONNECTING",
	"CONNECTED",
	"PAUSING",
	"PAUSE_SENT",
	"PAUSED",
	"RESUMING",
	"PAUSE_TIMEOUT",
	"RESUME_TIMEOUT"
};

static void
release_message_sync(struct vchiq_state *state, struct vchiq_header *header);

static const char *msg_type_str(unsigned int msg_type)
{
	switch (msg_type) {
	case VCHIQ_MSG_PADDING:			return "PADDING";
	case VCHIQ_MSG_CONNECT:			return "CONNECT";
	case VCHIQ_MSG_OPEN:			return "OPEN";
	case VCHIQ_MSG_OPENACK:			return "OPENACK";
	case VCHIQ_MSG_CLOSE:			return "CLOSE";
	case VCHIQ_MSG_DATA:			return "DATA";
	case VCHIQ_MSG_BULK_RX:			return "BULK_RX";
	case VCHIQ_MSG_BULK_TX:			return "BULK_TX";
	case VCHIQ_MSG_BULK_RX_DONE:		return "BULK_RX_DONE";
	case VCHIQ_MSG_BULK_TX_DONE:		return "BULK_TX_DONE";
	case VCHIQ_MSG_PAUSE:			return "PAUSE";
	case VCHIQ_MSG_RESUME:			return "RESUME";
	case VCHIQ_MSG_REMOTE_USE:		return "REMOTE_USE";
	case VCHIQ_MSG_REMOTE_RELEASE:		return "REMOTE_RELEASE";
	case VCHIQ_MSG_REMOTE_USE_ACTIVE:	return "REMOTE_USE_ACTIVE";
	}
	return "???";
}

static inline void
set_service_state(struct vchiq_service *service, int newstate)
{
	dev_dbg(service->state->dev, "core: %d: srv:%d %s->%s\n",
		service->state->id, service->localport,
		srvstate_names[service->srvstate],
		srvstate_names[newstate]);
	service->srvstate = newstate;
}

struct vchiq_service *handle_to_service(struct vchiq_instance *instance, unsigned int handle)
{
	int idx = handle & (VCHIQ_MAX_SERVICES - 1);

	return rcu_dereference(instance->state->services[idx]);
}

struct vchiq_service *
find_service_by_handle(struct vchiq_instance *instance, unsigned int handle)
{
	struct vchiq_service *service;

	rcu_read_lock();
	service = handle_to_service(instance, handle);
	if (service && service->srvstate != VCHIQ_SRVSTATE_FREE &&
	    service->handle == handle &&
	    kref_get_unless_zero(&service->ref_count)) {
		service = rcu_pointer_handoff(service);
		rcu_read_unlock();
		return service;
	}
	rcu_read_unlock();
	dev_dbg(instance->state->dev, "core: Invalid service handle 0x%x\n", handle);
	return NULL;
}

struct vchiq_service *
find_service_by_port(struct vchiq_state *state, unsigned int localport)
{
	if (localport <= VCHIQ_PORT_MAX) {
		struct vchiq_service *service;

		rcu_read_lock();
		service = rcu_dereference(state->services[localport]);
		if (service && service->srvstate != VCHIQ_SRVSTATE_FREE &&
		    kref_get_unless_zero(&service->ref_count)) {
			service = rcu_pointer_handoff(service);
			rcu_read_unlock();
			return service;
		}
		rcu_read_unlock();
	}
	dev_dbg(state->dev, "core: Invalid port %u\n", localport);
	return NULL;
}

struct vchiq_service *
find_service_for_instance(struct vchiq_instance *instance, unsigned int handle)
{
	struct vchiq_service *service;

	rcu_read_lock();
	service = handle_to_service(instance, handle);
	if (service && service->srvstate != VCHIQ_SRVSTATE_FREE &&
	    service->handle == handle &&
	    service->instance == instance &&
	    kref_get_unless_zero(&service->ref_count)) {
		service = rcu_pointer_handoff(service);
		rcu_read_unlock();
		return service;
	}
	rcu_read_unlock();
	dev_dbg(instance->state->dev, "core: Invalid service handle 0x%x\n", handle);
	return NULL;
}

struct vchiq_service *
find_closed_service_for_instance(struct vchiq_instance *instance, unsigned int handle)
{
	struct vchiq_service *service;

	rcu_read_lock();
	service = handle_to_service(instance, handle);
	if (service &&
	    (service->srvstate == VCHIQ_SRVSTATE_FREE ||
	     service->srvstate == VCHIQ_SRVSTATE_CLOSED) &&
	    service->handle == handle &&
	    service->instance == instance &&
	    kref_get_unless_zero(&service->ref_count)) {
		service = rcu_pointer_handoff(service);
		rcu_read_unlock();
		return service;
	}
	rcu_read_unlock();
	dev_dbg(instance->state->dev, "core: Invalid service handle 0x%x\n", handle);
	return service;
}

struct vchiq_service *
__next_service_by_instance(struct vchiq_state *state,
			   struct vchiq_instance *instance,
			   int *pidx)
{
	struct vchiq_service *service = NULL;
	int idx = *pidx;

	while (idx < state->unused_service) {
		struct vchiq_service *srv;

		srv = rcu_dereference(state->services[idx]);
		idx++;
		if (srv && srv->srvstate != VCHIQ_SRVSTATE_FREE &&
		    srv->instance == instance) {
			service = srv;
			break;
		}
	}

	*pidx = idx;
	return service;
}

struct vchiq_service *
next_service_by_instance(struct vchiq_state *state,
			 struct vchiq_instance *instance,
			 int *pidx)
{
	struct vchiq_service *service;

	rcu_read_lock();
	while (1) {
		service = __next_service_by_instance(state, instance, pidx);
		if (!service)
			break;
		if (kref_get_unless_zero(&service->ref_count)) {
			service = rcu_pointer_handoff(service);
			break;
		}
	}
	rcu_read_unlock();
	return service;
}

void
vchiq_service_get(struct vchiq_service *service)
{
	if (!service) {
		WARN(1, "%s service is NULL\n", __func__);
		return;
	}
	kref_get(&service->ref_count);
}

static void service_release(struct kref *kref)
{
	struct vchiq_service *service =
		container_of(kref, struct vchiq_service, ref_count);
	struct vchiq_state *state = service->state;

	WARN_ON(service->srvstate != VCHIQ_SRVSTATE_FREE);
	rcu_assign_pointer(state->services[service->localport], NULL);
	if (service->userdata_term)
		service->userdata_term(service->base.userdata);
	kfree_rcu(service, rcu);
}

void
vchiq_service_put(struct vchiq_service *service)
{
	if (!service) {
		WARN(1, "%s: service is NULL\n", __func__);
		return;
	}
	kref_put(&service->ref_count, service_release);
}

int
vchiq_get_client_id(struct vchiq_instance *instance, unsigned int handle)
{
	struct vchiq_service *service;
	int id;

	rcu_read_lock();
	service = handle_to_service(instance, handle);
	id = service ? service->client_id : 0;
	rcu_read_unlock();
	return id;
}

void *
vchiq_get_service_userdata(struct vchiq_instance *instance, unsigned int handle)
{
	void *userdata;
	struct vchiq_service *service;

	rcu_read_lock();
	service = handle_to_service(instance, handle);
	userdata = service ? service->base.userdata : NULL;
	rcu_read_unlock();
	return userdata;
}
EXPORT_SYMBOL(vchiq_get_service_userdata);

static void
mark_service_closing_internal(struct vchiq_service *service, int sh_thread)
{
	struct vchiq_state *state = service->state;
	struct vchiq_service_quota *quota;

	service->closing = 1;

	/* Synchronise with other threads. */
	mutex_lock(&state->recycle_mutex);
	mutex_unlock(&state->recycle_mutex);
	if (!sh_thread || (state->conn_state != VCHIQ_CONNSTATE_PAUSE_SENT)) {
		/*
		 * If we're pausing then the slot_mutex is held until resume
		 * by the slot handler.  Therefore don't try to acquire this
		 * mutex if we're the slot handler and in the pause sent state.
		 * We don't need to in this case anyway.
		 */
		mutex_lock(&state->slot_mutex);
		mutex_unlock(&state->slot_mutex);
	}

	/* Unblock any sending thread. */
	quota = &state->service_quotas[service->localport];
	complete(&quota->quota_event);
}

static void
mark_service_closing(struct vchiq_service *service)
{
	mark_service_closing_internal(service, 0);
}

static inline int
make_service_callback(struct vchiq_service *service, enum vchiq_reason reason,
		      struct vchiq_header *header, struct vchiq_bulk *bulk)
{
	void *cb_data = NULL;
	void __user *cb_userdata = NULL;
	int status;

	/*
	 * If a bulk transfer is in progress, pass bulk->cb_*data to the
	 * callback function.
	 */
	if (bulk) {
		cb_data = bulk->cb_data;
		cb_userdata = bulk->cb_userdata;
	}

	dev_dbg(service->state->dev, "core: %d: callback:%d (%s, %p, %p %p)\n",
		service->state->id, service->localport, reason_names[reason],
		header, cb_data, cb_userdata);
	status = service->base.callback(service->instance, reason, header, service->handle,
					cb_data, cb_userdata);
	if (status && (status != -EAGAIN)) {
		dev_warn(service->state->dev,
			 "core: %d: ignoring ERROR from callback to service %x\n",
			 service->state->id, service->handle);
		status = 0;
	}

	if (reason != VCHIQ_MESSAGE_AVAILABLE)
		vchiq_release_message(service->instance, service->handle, header);

	return status;
}

inline void
vchiq_set_conn_state(struct vchiq_state *state, enum vchiq_connstate newstate)
{
	enum vchiq_connstate oldstate = state->conn_state;

	dev_dbg(state->dev, "core: %d: %s->%s\n",
		state->id, conn_state_names[oldstate], conn_state_names[newstate]);
	state->conn_state = newstate;
	vchiq_platform_conn_state_changed(state, oldstate, newstate);
}

/* This initialises a single remote_event, and the associated wait_queue. */
static inline void
remote_event_create(wait_queue_head_t *wq, struct remote_event *event)
{
	event->armed = 0;
	/*
	 * Don't clear the 'fired' flag because it may already have been set
	 * by the other side.
	 */
	init_waitqueue_head(wq);
}

/*
 * All the event waiting routines in VCHIQ used a custom semaphore
 * implementation that filtered most signals. This achieved a behaviour similar
 * to the "killable" family of functions. While cleaning up this code all the
 * routines where switched to the "interruptible" family of functions, as the
 * former was deemed unjustified and the use "killable" set all VCHIQ's
 * threads in D state.
 *
 * Returns: 0 on success, a negative error code on failure
 */
static inline int
remote_event_wait(wait_queue_head_t *wq, struct remote_event *event)
{
	int ret = 0;

	if (!event->fired) {
		event->armed = 1;
		dsb(sy);
		ret = wait_event_interruptible(*wq, event->fired);
		if (ret) {
			event->armed = 0;
			return ret;
		}
		event->armed = 0;
		/* Ensure that the peer sees that we are not waiting (armed == 0). */
		wmb();
	}

	event->fired = 0;
	return ret;
}

static void
remote_event_signal(struct vchiq_state *state, struct remote_event *event)
{
	struct vchiq_drv_mgmt *mgmt = dev_get_drvdata(state->dev);

	/*
	 * Ensure that all writes to shared data structures have completed
	 * before signalling the peer.
	 */
	wmb();

	event->fired = 1;

	dsb(sy);         /* data barrier operation */

	if (event->armed)
		writel(0, mgmt->regs + BELL2); /* trigger vc interrupt */
}

/*
 * Acknowledge that the event has been signalled, and wake any waiters. Usually
 * called as a result of the doorbell being rung.
 */
static inline void
remote_event_signal_local(wait_queue_head_t *wq, struct remote_event *event)
{
	event->fired = 1;
	event->armed = 0;
	wake_up_all(wq);
}

/* Check if a single event has been signalled, waking the waiters if it has. */
static inline void
remote_event_poll(wait_queue_head_t *wq, struct remote_event *event)
{
	if (event->fired && event->armed)
		remote_event_signal_local(wq, event);
}

/*
 * VCHIQ used a small, fixed number of remote events. It is simplest to
 * enumerate them here for polling.
 */
void
remote_event_pollall(struct vchiq_state *state)
{
	remote_event_poll(&state->sync_trigger_event, &state->local->sync_trigger);
	remote_event_poll(&state->sync_release_event, &state->local->sync_release);
	remote_event_poll(&state->trigger_event, &state->local->trigger);
	remote_event_poll(&state->recycle_event, &state->local->recycle);
}

/*
 * Round up message sizes so that any space at the end of a slot is always big
 * enough for a header. This relies on header size being a power of two, which
 * has been verified earlier by a static assertion.
 */

static inline size_t
calc_stride(size_t size)
{
	/* Allow room for the header */
	size += sizeof(struct vchiq_header);

	/* Round up */
	return (size + sizeof(struct vchiq_header) - 1) &
		~(sizeof(struct vchiq_header) - 1);
}

/* Called by the slot handler thread */
static struct vchiq_service *
get_listening_service(struct vchiq_state *state, int fourcc)
{
	int i;

	WARN_ON(fourcc == VCHIQ_FOURCC_INVALID);

	rcu_read_lock();
	for (i = 0; i < state->unused_service; i++) {
		struct vchiq_service *service;

		service = rcu_dereference(state->services[i]);
		if (service &&
		    service->public_fourcc == fourcc &&
		    (service->srvstate == VCHIQ_SRVSTATE_LISTENING ||
		     (service->srvstate == VCHIQ_SRVSTATE_OPEN &&
		      service->remoteport == VCHIQ_PORT_FREE)) &&
		    kref_get_unless_zero(&service->ref_count)) {
			service = rcu_pointer_handoff(service);
			rcu_read_unlock();
			return service;
		}
	}
	rcu_read_unlock();
	return NULL;
}

/* Called by the slot handler thread */
static struct vchiq_service *
get_connected_service(struct vchiq_state *state, unsigned int port)
{
	int i;

	rcu_read_lock();
	for (i = 0; i < state->unused_service; i++) {
		struct vchiq_service *service =
			rcu_dereference(state->services[i]);

		if (service && service->srvstate == VCHIQ_SRVSTATE_OPEN &&
		    service->remoteport == port &&
		    kref_get_unless_zero(&service->ref_count)) {
			service = rcu_pointer_handoff(service);
			rcu_read_unlock();
			return service;
		}
	}
	rcu_read_unlock();
	return NULL;
}

inline void
request_poll(struct vchiq_state *state, struct vchiq_service *service,
	     int poll_type)
{
	u32 value;
	int index;

	if (!service)
		goto skip_service;

	do {
		value = atomic_read(&service->poll_flags);
	} while (atomic_cmpxchg(&service->poll_flags, value,
		 value | BIT(poll_type)) != value);

	index = BITSET_WORD(service->localport);
	do {
		value = atomic_read(&state->poll_services[index]);
	} while (atomic_cmpxchg(&state->poll_services[index],
		 value, value | BIT(service->localport & 0x1f)) != value);

skip_service:
	state->poll_needed = 1;
	/* Ensure the slot handler thread sees the poll_needed flag. */
	wmb();

	/* ... and ensure the slot handler runs. */
	remote_event_signal_local(&state->trigger_event, &state->local->trigger);
}

/*
 * Called from queue_message, by the slot handler and application threads,
 * with slot_mutex held
 */
static struct vchiq_header *
reserve_space(struct vchiq_state *state, size_t space, int is_blocking)
{
	struct vchiq_shared_state *local = state->local;
	int tx_pos = state->local_tx_pos;
	int slot_space = VCHIQ_SLOT_SIZE - (tx_pos & VCHIQ_SLOT_MASK);

	if (space > slot_space) {
		struct vchiq_header *header;
		/* Fill the remaining space with padding */
		WARN_ON(!state->tx_data);
		header = (struct vchiq_header *)
			(state->tx_data + (tx_pos & VCHIQ_SLOT_MASK));
		header->msgid = VCHIQ_MSGID_PADDING;
		header->size = slot_space - sizeof(struct vchiq_header);

		tx_pos += slot_space;
	}

	/* If necessary, get the next slot. */
	if ((tx_pos & VCHIQ_SLOT_MASK) == 0) {
		int slot_index;

		/* If there is no free slot... */

		if (!try_wait_for_completion(&state->slot_available_event)) {
			/* ...wait for one. */

			VCHIQ_STATS_INC(state, slot_stalls);

			/* But first, flush through the last slot. */
			state->local_tx_pos = tx_pos;
			local->tx_pos = tx_pos;
			remote_event_signal(state, &state->remote->trigger);

			if (!is_blocking ||
			    (wait_for_completion_interruptible(&state->slot_available_event)))
				return NULL; /* No space available */
		}

		if (tx_pos == (state->slot_queue_available * VCHIQ_SLOT_SIZE)) {
			complete(&state->slot_available_event);
			dev_warn(state->dev, "%s: invalid tx_pos: %d\n",
				 __func__, tx_pos);
			return NULL;
		}

		slot_index = local->slot_queue[SLOT_QUEUE_INDEX_FROM_POS_MASKED(tx_pos)];
		state->tx_data =
			(char *)SLOT_DATA_FROM_INDEX(state, slot_index);
	}

	state->local_tx_pos = tx_pos + space;

	return (struct vchiq_header *)(state->tx_data +
						(tx_pos & VCHIQ_SLOT_MASK));
}

static void
process_free_data_message(struct vchiq_state *state, u32 *service_found,
			  struct vchiq_header *header)
{
	int msgid = header->msgid;
	int port = VCHIQ_MSG_SRCPORT(msgid);
	struct vchiq_service_quota *quota = &state->service_quotas[port];
	int count;

	spin_lock(&state->quota_spinlock);
	count = quota->message_use_count;
	if (count > 0)
		quota->message_use_count = count - 1;
	spin_unlock(&state->quota_spinlock);

	if (count == quota->message_quota) {
		/*
		 * Signal the service that it
		 * has dropped below its quota
		 */
		complete(&quota->quota_event);
	} else if (count == 0) {
		dev_err(state->dev,
			"core: service %d message_use_count=%d (header %p, msgid %x, header->msgid %x, header->size %x)\n",
			port, quota->message_use_count, header, msgid,
			header->msgid, header->size);
		WARN(1, "invalid message use count\n");
	}
	if (!BITSET_IS_SET(service_found, port)) {
		/* Set the found bit for this service */
		BITSET_SET(service_found, port);

		spin_lock(&state->quota_spinlock);
		count = quota->slot_use_count;
		if (count > 0)
			quota->slot_use_count = count - 1;
		spin_unlock(&state->quota_spinlock);

		if (count > 0) {
			/*
			 * Signal the service in case
			 * it has dropped below its quota
			 */
			complete(&quota->quota_event);
			dev_dbg(state->dev, "core: %d: pfq:%d %x@%p - slot_use->%d\n",
				state->id, port, header->size, header, count - 1);
		} else {
			dev_err(state->dev,
				"core: service %d slot_use_count=%d (header %p, msgid %x, header->msgid %x, header->size %x)\n",
				port, count, header, msgid, header->msgid, header->size);
			WARN(1, "bad slot use count\n");
		}
	}
}

/* Called by the recycle thread. */
static void
process_free_queue(struct vchiq_state *state, u32 *service_found,
		   size_t length)
{
	struct vchiq_shared_state *local = state->local;
	int slot_queue_available;

	/*
	 * Find slots which have been freed by the other side, and return them
	 * to the available queue.
	 */
	slot_queue_available = state->slot_queue_available;

	/*
	 * Use a memory barrier to ensure that any state that may have been
	 * modified by another thread is not masked by stale prefetched
	 * values.
	 */
	mb();

	while (slot_queue_available != local->slot_queue_recycle) {
		unsigned int pos;
		int slot_index = local->slot_queue[slot_queue_available &
			VCHIQ_SLOT_QUEUE_MASK];
		char *data = (char *)SLOT_DATA_FROM_INDEX(state, slot_index);
		int data_found = 0;

		slot_queue_available++;
		/*
		 * Beware of the address dependency - data is calculated
		 * using an index written by the other side.
		 */
		rmb();

		dev_dbg(state->dev, "core: %d: pfq %d=%p %x %x\n",
			state->id, slot_index, data, local->slot_queue_recycle,
			slot_queue_available);

		/* Initialise the bitmask for services which have used this slot */
		memset(service_found, 0, length);

		pos = 0;

		while (pos < VCHIQ_SLOT_SIZE) {
			struct vchiq_header *header =
				(struct vchiq_header *)(data + pos);
			int msgid = header->msgid;

			if (VCHIQ_MSG_TYPE(msgid) == VCHIQ_MSG_DATA) {
				process_free_data_message(state, service_found,
							  header);
				data_found = 1;
			}

			pos += calc_stride(header->size);
			if (pos > VCHIQ_SLOT_SIZE) {
				dev_err(state->dev,
					"core: pfq - pos %x: header %p, msgid %x, header->msgid %x, header->size %x\n",
					pos, header, msgid, header->msgid, header->size);
				WARN(1, "invalid slot position\n");
			}
		}

		if (data_found) {
			int count;

			spin_lock(&state->quota_spinlock);
			count = state->data_use_count;
			if (count > 0)
				state->data_use_count = count - 1;
			spin_unlock(&state->quota_spinlock);
			if (count == state->data_quota)
				complete(&state->data_quota_event);
		}

		/*
		 * Don't allow the slot to be reused until we are no
		 * longer interested in it.
		 */
		mb();

		state->slot_queue_available = slot_queue_available;
		complete(&state->slot_available_event);
	}
}

static ssize_t
memcpy_copy_callback(void *context, void *dest, size_t offset, size_t maxsize)
{
	memcpy(dest + offset, context + offset, maxsize);
	return maxsize;
}

static ssize_t
copy_message_data(ssize_t (*copy_callback)(void *context, void *dest, size_t offset,
					   size_t maxsize),
	void *context,
	void *dest,
	size_t size)
{
	size_t pos = 0;

	while (pos < size) {
		ssize_t callback_result;
		size_t max_bytes = size - pos;

		callback_result = copy_callback(context, dest + pos, pos,
						max_bytes);

		if (callback_result < 0)
			return callback_result;

		if (!callback_result)
			return -EIO;

		if (callback_result > max_bytes)
			return -EIO;

		pos += callback_result;
	}

	return size;
}

/* Called by the slot handler and application threads */
static int
queue_message(struct vchiq_state *state, struct vchiq_service *service,
	      int msgid,
	      ssize_t (*copy_callback)(void *context, void *dest,
				       size_t offset, size_t maxsize),
	      void *context, size_t size, int flags)
{
	struct vchiq_shared_state *local;
	struct vchiq_service_quota *quota = NULL;
	struct vchiq_header *header;
	int type = VCHIQ_MSG_TYPE(msgid);
	int svc_fourcc;

	size_t stride;

	local = state->local;

	stride = calc_stride(size);

	WARN_ON(stride > VCHIQ_SLOT_SIZE);

	if (!(flags & QMFLAGS_NO_MUTEX_LOCK) &&
	    mutex_lock_killable(&state->slot_mutex))
		return -EINTR;

	if (type == VCHIQ_MSG_DATA) {
		int tx_end_index;

		if (!service) {
			WARN(1, "%s: service is NULL\n", __func__);
			mutex_unlock(&state->slot_mutex);
			return -EINVAL;
		}

		WARN_ON(flags & (QMFLAGS_NO_MUTEX_LOCK |
				 QMFLAGS_NO_MUTEX_UNLOCK));

		if (service->closing) {
			/* The service has been closed */
			mutex_unlock(&state->slot_mutex);
			return -EHOSTDOWN;
		}

		quota = &state->service_quotas[service->localport];

		spin_lock(&state->quota_spinlock);

		/*
		 * Ensure this service doesn't use more than its quota of
		 * messages or slots
		 */
		tx_end_index = SLOT_QUEUE_INDEX_FROM_POS(state->local_tx_pos + stride - 1);

		/*
		 * Ensure data messages don't use more than their quota of
		 * slots
		 */
		while ((tx_end_index != state->previous_data_index) &&
		       (state->data_use_count == state->data_quota)) {
			VCHIQ_STATS_INC(state, data_stalls);
			spin_unlock(&state->quota_spinlock);
			mutex_unlock(&state->slot_mutex);

			if (wait_for_completion_killable(&state->data_quota_event))
				return -EINTR;

			mutex_lock(&state->slot_mutex);
			spin_lock(&state->quota_spinlock);
			tx_end_index = SLOT_QUEUE_INDEX_FROM_POS(state->local_tx_pos + stride - 1);
			if ((tx_end_index == state->previous_data_index) ||
			    (state->data_use_count < state->data_quota)) {
				/* Pass the signal on to other waiters */
				complete(&state->data_quota_event);
				break;
			}
		}

		while ((quota->message_use_count == quota->message_quota) ||
		       ((tx_end_index != quota->previous_tx_index) &&
			(quota->slot_use_count == quota->slot_quota))) {
			spin_unlock(&state->quota_spinlock);
			dev_dbg(state->dev,
				"core: %d: qm:%d %s,%zx - quota stall (msg %d, slot %d)\n",
				state->id, service->localport, msg_type_str(type), size,
				quota->message_use_count, quota->slot_use_count);
			VCHIQ_SERVICE_STATS_INC(service, quota_stalls);
			mutex_unlock(&state->slot_mutex);
			if (wait_for_completion_killable(&quota->quota_event))
				return -EINTR;
			if (service->closing)
				return -EHOSTDOWN;
			if (mutex_lock_killable(&state->slot_mutex))
				return -EINTR;
			if (service->srvstate != VCHIQ_SRVSTATE_OPEN) {
				/* The service has been closed */
				mutex_unlock(&state->slot_mutex);
				return -EHOSTDOWN;
			}
			spin_lock(&state->quota_spinlock);
			tx_end_index = SLOT_QUEUE_INDEX_FROM_POS(state->local_tx_pos + stride - 1);
		}

		spin_unlock(&state->quota_spinlock);
	}

	header = reserve_space(state, stride, flags & QMFLAGS_IS_BLOCKING);

	if (!header) {
		if (service)
			VCHIQ_SERVICE_STATS_INC(service, slot_stalls);
		/*
		 * In the event of a failure, return the mutex to the
		 * state it was in
		 */
		if (!(flags & QMFLAGS_NO_MUTEX_LOCK))
			mutex_unlock(&state->slot_mutex);
		return -EAGAIN;
	}

	if (type == VCHIQ_MSG_DATA) {
		ssize_t callback_result;
		int tx_end_index;
		int slot_use_count;

		dev_dbg(state->dev, "core: %d: qm %s@%p,%zx (%d->%d)\n",
			state->id, msg_type_str(VCHIQ_MSG_TYPE(msgid)), header, size,
			VCHIQ_MSG_SRCPORT(msgid), VCHIQ_MSG_DSTPORT(msgid));

		WARN_ON(flags & (QMFLAGS_NO_MUTEX_LOCK |
				 QMFLAGS_NO_MUTEX_UNLOCK));

		callback_result =
			copy_message_data(copy_callback, context,
					  header->data, size);

		if (callback_result < 0) {
			mutex_unlock(&state->slot_mutex);
			VCHIQ_SERVICE_STATS_INC(service, error_count);
			return -EINVAL;
		}

		vchiq_log_dump_mem(state->dev, "Sent", 0,
				   header->data,
				   min_t(size_t, 16, callback_result));

		spin_lock(&state->quota_spinlock);
		quota->message_use_count++;

		tx_end_index =
			SLOT_QUEUE_INDEX_FROM_POS(state->local_tx_pos - 1);

		/*
		 * If this transmission can't fit in the last slot used by any
		 * service, the data_use_count must be increased.
		 */
		if (tx_end_index != state->previous_data_index) {
			state->previous_data_index = tx_end_index;
			state->data_use_count++;
		}

		/*
		 * If this isn't the same slot last used by this service,
		 * the service's slot_use_count must be increased.
		 */
		if (tx_end_index != quota->previous_tx_index) {
			quota->previous_tx_index = tx_end_index;
			slot_use_count = ++quota->slot_use_count;
		} else {
			slot_use_count = 0;
		}

		spin_unlock(&state->quota_spinlock);

		if (slot_use_count)
			dev_dbg(state->dev, "core: %d: qm:%d %s,%zx - slot_use->%d (hdr %p)\n",
				state->id, service->localport, msg_type_str(VCHIQ_MSG_TYPE(msgid)),
				size, slot_use_count, header);

		VCHIQ_SERVICE_STATS_INC(service, ctrl_tx_count);
		VCHIQ_SERVICE_STATS_ADD(service, ctrl_tx_bytes, size);
	} else {
		dev_dbg(state->dev, "core: %d: qm %s@%p,%zx (%d->%d)\n",
			state->id, msg_type_str(VCHIQ_MSG_TYPE(msgid)), header, size,
			VCHIQ_MSG_SRCPORT(msgid), VCHIQ_MSG_DSTPORT(msgid));
		if (size != 0) {
			/*
			 * It is assumed for now that this code path
			 * only happens from calls inside this file.
			 *
			 * External callers are through the vchiq_queue_message
			 * path which always sets the type to be VCHIQ_MSG_DATA
			 *
			 * At first glance this appears to be correct but
			 * more review is needed.
			 */
			copy_message_data(copy_callback, context,
					  header->data, size);
		}
		VCHIQ_STATS_INC(state, ctrl_tx_count);
	}

	header->msgid = msgid;
	header->size = size;

	svc_fourcc = service ? service->base.fourcc
			     : VCHIQ_MAKE_FOURCC('?', '?', '?', '?');

	dev_dbg(state->dev, "core_msg: Sent Msg %s(%u) to %p4cc s:%u d:%d len:%zu\n",
		msg_type_str(VCHIQ_MSG_TYPE(msgid)),
		VCHIQ_MSG_TYPE(msgid), &svc_fourcc,
		VCHIQ_MSG_SRCPORT(msgid), VCHIQ_MSG_DSTPORT(msgid), size);

	/* Make sure the new header is visible to the peer. */
	wmb();

	/* Make the new tx_pos visible to the peer. */
	local->tx_pos = state->local_tx_pos;
	wmb();

	if (service && (type == VCHIQ_MSG_CLOSE))
		set_service_state(service, VCHIQ_SRVSTATE_CLOSESENT);

	if (!(flags & QMFLAGS_NO_MUTEX_UNLOCK))
		mutex_unlock(&state->slot_mutex);

	remote_event_signal(state, &state->remote->trigger);

	return 0;
}

/* Called by the slot handler and application threads */
static int
queue_message_sync(struct vchiq_state *state, struct vchiq_service *service,
		   int msgid,
		   ssize_t (*copy_callback)(void *context, void *dest,
					    size_t offset, size_t maxsize),
		   void *context, int size)
{
	struct vchiq_shared_state *local;
	struct vchiq_header *header;
	ssize_t callback_result;
	int svc_fourcc;
	int ret;

	local = state->local;

	if (VCHIQ_MSG_TYPE(msgid) != VCHIQ_MSG_RESUME &&
	    mutex_lock_killable(&state->sync_mutex))
		return -EAGAIN;

	ret = remote_event_wait(&state->sync_release_event, &local->sync_release);
	if (ret)
		return ret;

	/* Ensure that reads don't overtake the remote_event_wait. */
	rmb();

	header = (struct vchiq_header *)SLOT_DATA_FROM_INDEX(state,
		local->slot_sync);

	{
		int oldmsgid = header->msgid;

		if (oldmsgid != VCHIQ_MSGID_PADDING)
			dev_err(state->dev, "core: %d: qms - msgid %x, not PADDING\n",
				state->id, oldmsgid);
	}

	dev_dbg(state->dev, "sync: %d: qms %s@%p,%x (%d->%d)\n",
		state->id, msg_type_str(VCHIQ_MSG_TYPE(msgid)), header, size,
		VCHIQ_MSG_SRCPORT(msgid), VCHIQ_MSG_DSTPORT(msgid));

	callback_result = copy_message_data(copy_callback, context,
					    header->data, size);

	if (callback_result < 0) {
		mutex_unlock(&state->slot_mutex);
		VCHIQ_SERVICE_STATS_INC(service, error_count);
		return -EINVAL;
	}

	if (service) {
		vchiq_log_dump_mem(state->dev, "Sent", 0,
				   header->data,
				   min_t(size_t, 16, callback_result));

		VCHIQ_SERVICE_STATS_INC(service, ctrl_tx_count);
		VCHIQ_SERVICE_STATS_ADD(service, ctrl_tx_bytes, size);
	} else {
		VCHIQ_STATS_INC(state, ctrl_tx_count);
	}

	header->size = size;
	header->msgid = msgid;

	svc_fourcc = service ? service->base.fourcc
			     : VCHIQ_MAKE_FOURCC('?', '?', '?', '?');

	dev_dbg(state->dev,
		"sync: Sent Sync Msg %s(%u) to %p4cc s:%u d:%d len:%d\n",
		msg_type_str(VCHIQ_MSG_TYPE(msgid)), VCHIQ_MSG_TYPE(msgid),
		&svc_fourcc, VCHIQ_MSG_SRCPORT(msgid),
		VCHIQ_MSG_DSTPORT(msgid), size);

	remote_event_signal(state, &state->remote->sync_trigger);

	if (VCHIQ_MSG_TYPE(msgid) != VCHIQ_MSG_PAUSE)
		mutex_unlock(&state->sync_mutex);

	return 0;
}

static inline void
claim_slot(struct vchiq_slot_info *slot)
{
	slot->use_count++;
}

static void
release_slot(struct vchiq_state *state, struct vchiq_slot_info *slot_info,
	     struct vchiq_header *header, struct vchiq_service *service)
{
	mutex_lock(&state->recycle_mutex);

	if (header) {
		int msgid = header->msgid;

		if (((msgid & VCHIQ_MSGID_CLAIMED) == 0) || (service && service->closing)) {
			mutex_unlock(&state->recycle_mutex);
			return;
		}

		/* Rewrite the message header to prevent a double release */
		header->msgid = msgid & ~VCHIQ_MSGID_CLAIMED;
	}

	slot_info->release_count++;

	if (slot_info->release_count == slot_info->use_count) {
		int slot_queue_recycle;
		/* Add to the freed queue */

		/*
		 * A read barrier is necessary here to prevent speculative
		 * fetches of remote->slot_queue_recycle from overtaking the
		 * mutex.
		 */
		rmb();

		slot_queue_recycle = state->remote->slot_queue_recycle;
		state->remote->slot_queue[slot_queue_recycle &
			VCHIQ_SLOT_QUEUE_MASK] =
			SLOT_INDEX_FROM_INFO(state, slot_info);
		state->remote->slot_queue_recycle = slot_queue_recycle + 1;
		dev_dbg(state->dev, "core: %d: %d - recycle->%x\n",
			state->id, SLOT_INDEX_FROM_INFO(state, slot_info),
			state->remote->slot_queue_recycle);

		/*
		 * A write barrier is necessary, but remote_event_signal
		 * contains one.
		 */
		remote_event_signal(state, &state->remote->recycle);
	}

	mutex_unlock(&state->recycle_mutex);
}

static inline enum vchiq_reason
get_bulk_reason(struct vchiq_bulk *bulk)
{
	if (bulk->dir == VCHIQ_BULK_TRANSMIT) {
		if (bulk->actual == VCHIQ_BULK_ACTUAL_ABORTED)
			return VCHIQ_BULK_TRANSMIT_ABORTED;

		return VCHIQ_BULK_TRANSMIT_DONE;
	}

	if (bulk->actual == VCHIQ_BULK_ACTUAL_ABORTED)
		return VCHIQ_BULK_RECEIVE_ABORTED;

	return VCHIQ_BULK_RECEIVE_DONE;
}

static int service_notify_bulk(struct vchiq_service *service,
			       struct vchiq_bulk *bulk)
{
	if (bulk->actual != VCHIQ_BULK_ACTUAL_ABORTED) {
		if (bulk->dir == VCHIQ_BULK_TRANSMIT) {
			VCHIQ_SERVICE_STATS_INC(service, bulk_tx_count);
			VCHIQ_SERVICE_STATS_ADD(service, bulk_tx_bytes,
						bulk->actual);
		} else {
			VCHIQ_SERVICE_STATS_INC(service, bulk_rx_count);
			VCHIQ_SERVICE_STATS_ADD(service, bulk_rx_bytes,
						bulk->actual);
		}
	} else {
		VCHIQ_SERVICE_STATS_INC(service, bulk_aborted_count);
	}

	if (bulk->mode == VCHIQ_BULK_MODE_BLOCKING) {
		struct bulk_waiter *waiter;

		spin_lock(&service->state->bulk_waiter_spinlock);
		waiter = bulk->waiter;
		if (waiter) {
			waiter->actual = bulk->actual;
			complete(&waiter->event);
		}
		spin_unlock(&service->state->bulk_waiter_spinlock);
	} else if (bulk->mode == VCHIQ_BULK_MODE_CALLBACK) {
		enum vchiq_reason reason = get_bulk_reason(bulk);

		return make_service_callback(service, reason, NULL, bulk);
	}

	return 0;
}

/* Called by the slot handler - don't hold the bulk mutex */
static int
notify_bulks(struct vchiq_service *service, struct vchiq_bulk_queue *queue,
	     int retry_poll)
{
	int status = 0;

	dev_dbg(service->state->dev,
		"core: %d: nb:%d %cx - p=%x rn=%x r=%x\n",
		service->state->id, service->localport,
		(queue == &service->bulk_tx) ? 't' : 'r',
		queue->process, queue->remote_notify, queue->remove);

	queue->remote_notify = queue->process;

	while (queue->remove != queue->remote_notify) {
		struct vchiq_bulk *bulk =
			&queue->bulks[BULK_INDEX(queue->remove)];

		/*
		 * Only generate callbacks for non-dummy bulk
		 * requests, and non-terminated services
		 */
		if (bulk->dma_addr && service->instance) {
			status = service_notify_bulk(service, bulk);
			if (status == -EAGAIN)
				break;
		}

		queue->remove++;
		complete(&service->bulk_remove_event);
	}
	if (!retry_poll)
		status = 0;

	if (status == -EAGAIN)
		request_poll(service->state, service, (queue == &service->bulk_tx) ?
			     VCHIQ_POLL_TXNOTIFY : VCHIQ_POLL_RXNOTIFY);

	return status;
}

static void
poll_services_of_group(struct vchiq_state *state, int group)
{
	u32 flags = atomic_xchg(&state->poll_services[group], 0);
	int i;

	for (i = 0; flags; i++) {
		struct vchiq_service *service;
		u32 service_flags;

		if ((flags & BIT(i)) == 0)
			continue;

		service = find_service_by_port(state, (group << 5) + i);
		flags &= ~BIT(i);

		if (!service)
			continue;

		service_flags = atomic_xchg(&service->poll_flags, 0);
		if (service_flags & BIT(VCHIQ_POLL_REMOVE)) {
			dev_dbg(state->dev, "core: %d: ps - remove %d<->%d\n",
				state->id, service->localport, service->remoteport);

			/*
			 * Make it look like a client, because
			 * it must be removed and not left in
			 * the LISTENING state.
			 */
			service->public_fourcc = VCHIQ_FOURCC_INVALID;

			if (vchiq_close_service_internal(service, NO_CLOSE_RECVD))
				request_poll(state, service, VCHIQ_POLL_REMOVE);
		} else if (service_flags & BIT(VCHIQ_POLL_TERMINATE)) {
			dev_dbg(state->dev, "core: %d: ps - terminate %d<->%d\n",
				state->id, service->localport, service->remoteport);
			if (vchiq_close_service_internal(service, NO_CLOSE_RECVD))
				request_poll(state, service, VCHIQ_POLL_TERMINATE);
		}
		if (service_flags & BIT(VCHIQ_POLL_TXNOTIFY))
			notify_bulks(service, &service->bulk_tx, RETRY_POLL);
		if (service_flags & BIT(VCHIQ_POLL_RXNOTIFY))
			notify_bulks(service, &service->bulk_rx, RETRY_POLL);
		vchiq_service_put(service);
	}
}

/* Called by the slot handler thread */
static void
poll_services(struct vchiq_state *state)
{
	int group;

	for (group = 0; group < BITSET_SIZE(state->unused_service); group++)
		poll_services_of_group(state, group);
}

static void
cleanup_pagelistinfo(struct vchiq_instance *instance, struct vchiq_pagelist_info *pagelistinfo)
{
	if (pagelistinfo->scatterlist_mapped) {
		dma_unmap_sg(instance->state->dev, pagelistinfo->scatterlist,
			     pagelistinfo->num_pages, pagelistinfo->dma_dir);
	}

	if (pagelistinfo->pages_need_release)
		unpin_user_pages(pagelistinfo->pages, pagelistinfo->num_pages);

	dma_free_coherent(instance->state->dev, pagelistinfo->pagelist_buffer_size,
			  pagelistinfo->pagelist, pagelistinfo->dma_addr);
}

static inline bool
is_adjacent_block(u32 *addrs, dma_addr_t addr, unsigned int k)
{
	u32 tmp;

	if (!k)
		return false;

	tmp = (addrs[k - 1] & PAGE_MASK) +
	      (((addrs[k - 1] & ~PAGE_MASK) + 1) << PAGE_SHIFT);

	return tmp == (addr & PAGE_MASK);
}

/* There is a potential problem with partial cache lines (pages?)
 * at the ends of the block when reading. If the CPU accessed anything in
 * the same line (page?) then it may have pulled old data into the cache,
 * obscuring the new data underneath. We can solve this by transferring the
 * partial cache lines separately, and allowing the ARM to copy into the
 * cached area.
 */
static struct vchiq_pagelist_info *
create_pagelist(struct vchiq_instance *instance, struct vchiq_bulk *bulk)
{
	struct vchiq_drv_mgmt *drv_mgmt;
	struct pagelist *pagelist;
	struct vchiq_pagelist_info *pagelistinfo;
	struct page **pages;
	u32 *addrs;
	unsigned int num_pages, offset, i, k;
	int actual_pages;
	size_t pagelist_size;
	struct scatterlist *scatterlist, *sg;
	int dma_buffers;
	unsigned int cache_line_size;
	dma_addr_t dma_addr;
	size_t count = bulk->size;
	unsigned short type = (bulk->dir == VCHIQ_BULK_RECEIVE)
			      ? PAGELIST_READ : PAGELIST_WRITE;

	if (count >= INT_MAX - PAGE_SIZE)
		return NULL;

	drv_mgmt = dev_get_drvdata(instance->state->dev);

	if (bulk->offset)
		offset = (uintptr_t)bulk->offset & (PAGE_SIZE - 1);
	else
		offset = (uintptr_t)bulk->uoffset & (PAGE_SIZE - 1);
	num_pages = DIV_ROUND_UP(count + offset, PAGE_SIZE);

	if ((size_t)num_pages > (SIZE_MAX - sizeof(struct pagelist) -
			 sizeof(struct vchiq_pagelist_info)) /
			(sizeof(u32) + sizeof(pages[0]) +
			 sizeof(struct scatterlist)))
		return NULL;

	pagelist_size = sizeof(struct pagelist) +
			(num_pages * sizeof(u32)) +
			(num_pages * sizeof(pages[0]) +
			(num_pages * sizeof(struct scatterlist))) +
			sizeof(struct vchiq_pagelist_info);

	/* Allocate enough storage to hold the page pointers and the page
	 * list
	 */
	pagelist = dma_alloc_coherent(instance->state->dev, pagelist_size, &dma_addr,
				      GFP_KERNEL);

	dev_dbg(instance->state->dev, "arm: %p\n", pagelist);

	if (!pagelist)
		return NULL;

	addrs		= pagelist->addrs;
	pages		= (struct page **)(addrs + num_pages);
	scatterlist	= (struct scatterlist *)(pages + num_pages);
	pagelistinfo	= (struct vchiq_pagelist_info *)
			  (scatterlist + num_pages);

	pagelist->length = count;
	pagelist->type = type;
	pagelist->offset = offset;

	/* Populate the fields of the pagelistinfo structure */
	pagelistinfo->pagelist = pagelist;
	pagelistinfo->pagelist_buffer_size = pagelist_size;
	pagelistinfo->dma_addr = dma_addr;
	pagelistinfo->dma_dir =  (type == PAGELIST_WRITE) ?
				  DMA_TO_DEVICE : DMA_FROM_DEVICE;
	pagelistinfo->num_pages = num_pages;
	pagelistinfo->pages_need_release = 0;
	pagelistinfo->pages = pages;
	pagelistinfo->scatterlist = scatterlist;
	pagelistinfo->scatterlist_mapped = 0;

	if (bulk->offset) {
		unsigned long length = count;
		unsigned int off = offset;

		for (actual_pages = 0; actual_pages < num_pages;
		     actual_pages++) {
			struct page *pg =
				vmalloc_to_page(((unsigned int *)bulk->offset +
						 (actual_pages * PAGE_SIZE)));
			size_t bytes = PAGE_SIZE - off;

			if (!pg) {
				cleanup_pagelistinfo(instance, pagelistinfo);
				return NULL;
			}

			if (bytes > length)
				bytes = length;
			pages[actual_pages] = pg;
			length -= bytes;
			off = 0;
		}
		/* do not try and release vmalloc pages */
	} else {
		actual_pages =
			pin_user_pages_fast((unsigned long)bulk->uoffset & PAGE_MASK, num_pages,
					    type == PAGELIST_READ, pages);

		if (actual_pages != num_pages) {
			dev_dbg(instance->state->dev, "arm: Only %d/%d pages locked\n",
				actual_pages, num_pages);

			/* This is probably due to the process being killed */
			if (actual_pages > 0)
				unpin_user_pages(pages, actual_pages);
			cleanup_pagelistinfo(instance, pagelistinfo);
			return NULL;
		}
		 /* release user pages */
		pagelistinfo->pages_need_release = 1;
	}

	/*
	 * Initialize the scatterlist so that the magic cookie
	 *  is filled if debugging is enabled
	 */
	sg_init_table(scatterlist, num_pages);
	/* Now set the pages for each scatterlist */
	for (i = 0; i < num_pages; i++)	{
		unsigned int len = PAGE_SIZE - offset;

		if (len > count)
			len = count;
		sg_set_page(scatterlist + i, pages[i], len, offset);
		offset = 0;
		count -= len;
	}

	dma_buffers = dma_map_sg(instance->state->dev,
				 scatterlist,
				 num_pages,
				 pagelistinfo->dma_dir);

	if (dma_buffers == 0) {
		cleanup_pagelistinfo(instance, pagelistinfo);
		return NULL;
	}

	pagelistinfo->scatterlist_mapped = 1;

	/* Combine adjacent blocks for performance */
	k = 0;
	for_each_sg(scatterlist, sg, dma_buffers, i) {
		unsigned int len = sg_dma_len(sg);
		dma_addr_t addr = sg_dma_address(sg);

		/* Note: addrs is the address + page_count - 1
		 * The firmware expects blocks after the first to be page-
		 * aligned and a multiple of the page size
		 */
		WARN_ON(len == 0);
		WARN_ON(i && (i != (dma_buffers - 1)) && (len & ~PAGE_MASK));
		WARN_ON(i && (addr & ~PAGE_MASK));
		if (is_adjacent_block(addrs, addr, k))
			addrs[k - 1] += ((len + PAGE_SIZE - 1) >> PAGE_SHIFT);
		else
			addrs[k++] = (addr & PAGE_MASK) |
				(((len + PAGE_SIZE - 1) >> PAGE_SHIFT) - 1);
	}

	/* Partial cache lines (fragments) require special measures */
	cache_line_size = drv_mgmt->info->cache_line_size;
	if ((type == PAGELIST_READ) &&
	    ((pagelist->offset & (cache_line_size - 1)) ||
	    ((pagelist->offset + pagelist->length) & (cache_line_size - 1)))) {
		char *fragments;

		if (down_interruptible(&drv_mgmt->free_fragments_sema)) {
			cleanup_pagelistinfo(instance, pagelistinfo);
			return NULL;
		}

		WARN_ON(!drv_mgmt->free_fragments);

		down(&drv_mgmt->free_fragments_mutex);
		fragments = drv_mgmt->free_fragments;
		WARN_ON(!fragments);
		drv_mgmt->free_fragments = *(char **)drv_mgmt->free_fragments;
		up(&drv_mgmt->free_fragments_mutex);
		pagelist->type = PAGELIST_READ_WITH_FRAGMENTS +
			(fragments - drv_mgmt->fragments_base) / drv_mgmt->fragments_size;
	}

	return pagelistinfo;
}

static void
free_pagelist(struct vchiq_instance *instance, struct vchiq_pagelist_info *pagelistinfo,
	      int actual)
{
	struct vchiq_drv_mgmt *drv_mgmt;
	struct pagelist *pagelist = pagelistinfo->pagelist;
	struct page **pages = pagelistinfo->pages;
	unsigned int num_pages = pagelistinfo->num_pages;
	unsigned int cache_line_size;

	dev_dbg(instance->state->dev, "arm: %p, %d\n", pagelistinfo->pagelist, actual);

	drv_mgmt = dev_get_drvdata(instance->state->dev);

	/*
	 * NOTE: dma_unmap_sg must be called before the
	 * cpu can touch any of the data/pages.
	 */
	dma_unmap_sg(instance->state->dev, pagelistinfo->scatterlist,
		     pagelistinfo->num_pages, pagelistinfo->dma_dir);
	pagelistinfo->scatterlist_mapped = 0;

	/* Deal with any partial cache lines (fragments) */
	cache_line_size = drv_mgmt->info->cache_line_size;
	if (pagelist->type >= PAGELIST_READ_WITH_FRAGMENTS && drv_mgmt->fragments_base) {
		char *fragments = drv_mgmt->fragments_base +
			(pagelist->type - PAGELIST_READ_WITH_FRAGMENTS) *
			drv_mgmt->fragments_size;
		int head_bytes, tail_bytes;

		head_bytes = (cache_line_size - pagelist->offset) &
			     (cache_line_size - 1);
		tail_bytes = (pagelist->offset + actual) &
			     (cache_line_size - 1);

		if ((actual >= 0) && (head_bytes != 0)) {
			if (head_bytes > actual)
				head_bytes = actual;

			memcpy_to_page(pages[0], pagelist->offset,
				       fragments, head_bytes);
		}
		if ((actual >= 0) && (head_bytes < actual) &&
		    (tail_bytes != 0))
			memcpy_to_page(pages[num_pages - 1],
				       (pagelist->offset + actual) &
				       (PAGE_SIZE - 1) & ~(cache_line_size - 1),
				       fragments + cache_line_size,
				       tail_bytes);

		down(&drv_mgmt->free_fragments_mutex);
		*(char **)fragments = drv_mgmt->free_fragments;
		drv_mgmt->free_fragments = fragments;
		up(&drv_mgmt->free_fragments_mutex);
		up(&drv_mgmt->free_fragments_sema);
	}

	/* Need to mark all the pages dirty. */
	if (pagelist->type != PAGELIST_WRITE &&
	    pagelistinfo->pages_need_release) {
		unsigned int i;

		for (i = 0; i < num_pages; i++)
			set_page_dirty(pages[i]);
	}

	cleanup_pagelistinfo(instance, pagelistinfo);
}

static int
vchiq_prepare_bulk_data(struct vchiq_instance *instance, struct vchiq_bulk *bulk)
{
	struct vchiq_pagelist_info *pagelistinfo;

	pagelistinfo = create_pagelist(instance, bulk);

	if (!pagelistinfo)
		return -ENOMEM;

	bulk->dma_addr = pagelistinfo->dma_addr;

	/*
	 * Store the pagelistinfo address in remote_data,
	 * which isn't used by the slave.
	 */
	bulk->remote_data = pagelistinfo;

	return 0;
}

static void
vchiq_complete_bulk(struct vchiq_instance *instance, struct vchiq_bulk *bulk)
{
	if (bulk && bulk->remote_data && bulk->actual)
		free_pagelist(instance, (struct vchiq_pagelist_info *)bulk->remote_data,
			      bulk->actual);
}

/* Called with the bulk_mutex held */
static void
abort_outstanding_bulks(struct vchiq_service *service,
			struct vchiq_bulk_queue *queue)
{
	int is_tx = (queue == &service->bulk_tx);

	dev_dbg(service->state->dev,
		"core: %d: aob:%d %cx - li=%x ri=%x p=%x\n",
		service->state->id, service->localport,
		is_tx ? 't' : 'r', queue->local_insert,
		queue->remote_insert, queue->process);

	WARN_ON((int)(queue->local_insert - queue->process) < 0);
	WARN_ON((int)(queue->remote_insert - queue->process) < 0);

	while ((queue->process != queue->local_insert) ||
	       (queue->process != queue->remote_insert)) {
		struct vchiq_bulk *bulk = &queue->bulks[BULK_INDEX(queue->process)];

		if (queue->process == queue->remote_insert) {
			/* fabricate a matching dummy bulk */
			bulk->remote_data = NULL;
			bulk->remote_size = 0;
			queue->remote_insert++;
		}

		if (queue->process != queue->local_insert) {
			vchiq_complete_bulk(service->instance, bulk);

			dev_dbg(service->state->dev,
				"core_msg: %s %p4cc d:%d ABORTED - tx len:%d, rx len:%d\n",
				is_tx ? "Send Bulk to" : "Recv Bulk from",
				&service->base.fourcc,
				service->remoteport, bulk->size, bulk->remote_size);
		} else {
			/* fabricate a matching dummy bulk */
			bulk->dma_addr = 0;
			bulk->size = 0;
			bulk->actual = VCHIQ_BULK_ACTUAL_ABORTED;
			bulk->dir = is_tx ? VCHIQ_BULK_TRANSMIT :
				VCHIQ_BULK_RECEIVE;
			queue->local_insert++;
		}

		queue->process++;
	}
}

static int
parse_open(struct vchiq_state *state, struct vchiq_header *header)
{
	const struct vchiq_open_payload *payload;
	struct vchiq_openack_payload ack_payload;
	struct vchiq_service *service = NULL;
	int msgid, size;
	int openack_id;
	unsigned int localport, remoteport, fourcc;
	short version, version_min;

	msgid = header->msgid;
	size = header->size;
	localport = VCHIQ_MSG_DSTPORT(msgid);
	remoteport = VCHIQ_MSG_SRCPORT(msgid);
	if (size < sizeof(struct vchiq_open_payload))
		goto fail_open;

	payload = (struct vchiq_open_payload *)header->data;
	fourcc = payload->fourcc;
	dev_dbg(state->dev, "core: %d: prs OPEN@%p (%d->'%p4cc')\n",
		state->id, header, localport, &fourcc);

	service = get_listening_service(state, fourcc);
	if (!service)
		goto fail_open;

	/* A matching service exists */
	version = payload->version;
	version_min = payload->version_min;

	if ((service->version < version_min) || (version < service->version_min)) {
		/* Version mismatch */
		dev_err(state->dev, "%d: service %d (%p4cc) version mismatch - local (%d, min %d) vs. remote (%d, min %d)",
			state->id, service->localport, &fourcc,
			service->version, service->version_min, version, version_min);
		vchiq_service_put(service);
		service = NULL;
		goto fail_open;
	}
	service->peer_version = version;

	if (service->srvstate != VCHIQ_SRVSTATE_LISTENING)
		goto done;

	ack_payload.version = service->version;
	openack_id = MAKE_OPENACK(service->localport, remoteport);

	if (state->version_common < VCHIQ_VERSION_SYNCHRONOUS_MODE)
		service->sync = 0;

	/* Acknowledge the OPEN */
	if (service->sync) {
		if (queue_message_sync(state, NULL, openack_id,
				       memcpy_copy_callback,
				       &ack_payload,
				       sizeof(ack_payload)) == -EAGAIN)
			goto bail_not_ready;

		/* The service is now open */
		set_service_state(service, VCHIQ_SRVSTATE_OPENSYNC);
	} else {
		if (queue_message(state, NULL, openack_id,
				  memcpy_copy_callback, &ack_payload,
				  sizeof(ack_payload), 0) == -EINTR)
			goto bail_not_ready;

		/* The service is now open */
		set_service_state(service, VCHIQ_SRVSTATE_OPEN);
	}

done:
	/* Success - the message has been dealt with */
	vchiq_service_put(service);
	return 1;

fail_open:
	/* No available service, or an invalid request - send a CLOSE */
	if (queue_message(state, NULL, MAKE_CLOSE(0, VCHIQ_MSG_SRCPORT(msgid)),
			  NULL, NULL, 0, 0) == -EINTR)
		goto bail_not_ready;

	return 1;

bail_not_ready:
	if (service)
		vchiq_service_put(service);

	return 0;
}

/**
 * parse_message() - parses a single message from the rx slot
 * @state:  vchiq state struct
 * @header: message header
 *
 * Context: Process context
 *
 * Return:
 * * >= 0     - size of the parsed message payload (without header)
 * * -EINVAL  - fatal error occurred, bail out is required
 */
static int
parse_message(struct vchiq_state *state, struct vchiq_header *header)
{
	struct vchiq_service *service = NULL;
	unsigned int localport, remoteport;
	int msgid, size, type, ret = -EINVAL;
	int svc_fourcc;

	DEBUG_INITIALISE(state->local);

	DEBUG_VALUE(PARSE_HEADER, (int)(long)header);
	msgid = header->msgid;
	DEBUG_VALUE(PARSE_MSGID, msgid);
	size = header->size;
	type = VCHIQ_MSG_TYPE(msgid);
	localport = VCHIQ_MSG_DSTPORT(msgid);
	remoteport = VCHIQ_MSG_SRCPORT(msgid);

	if (type != VCHIQ_MSG_DATA)
		VCHIQ_STATS_INC(state, ctrl_rx_count);

	switch (type) {
	case VCHIQ_MSG_OPENACK:
	case VCHIQ_MSG_CLOSE:
	case VCHIQ_MSG_DATA:
	case VCHIQ_MSG_BULK_RX:
	case VCHIQ_MSG_BULK_TX:
	case VCHIQ_MSG_BULK_RX_DONE:
	case VCHIQ_MSG_BULK_TX_DONE:
		service = find_service_by_port(state, localport);
		if ((!service ||
		     ((service->remoteport != remoteport) &&
		      (service->remoteport != VCHIQ_PORT_FREE))) &&
		    (localport == 0) &&
		    (type == VCHIQ_MSG_CLOSE)) {
			/*
			 * This could be a CLOSE from a client which
			 * hadn't yet received the OPENACK - look for
			 * the connected service
			 */
			if (service)
				vchiq_service_put(service);
			service = get_connected_service(state, remoteport);
			if (service)
				dev_warn(state->dev,
					 "core: %d: prs %s@%p (%d->%d) - found connected service %d\n",
					 state->id, msg_type_str(type), header,
					 remoteport, localport, service->localport);
		}

		if (!service) {
			dev_err(state->dev,
				"core: %d: prs %s@%p (%d->%d) - invalid/closed service %d\n",
				state->id, msg_type_str(type), header, remoteport,
				localport, localport);
			goto skip_message;
		}
		break;
	default:
		break;
	}

	svc_fourcc = service ? service->base.fourcc
			     : VCHIQ_MAKE_FOURCC('?', '?', '?', '?');

	dev_dbg(state->dev, "core_msg: Rcvd Msg %s(%u) from %p4cc s:%d d:%d len:%d\n",
		msg_type_str(type), type, &svc_fourcc, remoteport, localport, size);
	if (size > 0)
		vchiq_log_dump_mem(state->dev, "Rcvd", 0, header->data, min(16, size));

	if (((unsigned long)header & VCHIQ_SLOT_MASK) +
	    calc_stride(size) > VCHIQ_SLOT_SIZE) {
		dev_err(state->dev, "core: header %p (msgid %x) - size %x too big for slot\n",
			header, (unsigned int)msgid, (unsigned int)size);
		WARN(1, "oversized for slot\n");
	}

	switch (type) {
	case VCHIQ_MSG_OPEN:
		WARN_ON(VCHIQ_MSG_DSTPORT(msgid));
		if (!parse_open(state, header))
			goto bail_not_ready;
		break;
	case VCHIQ_MSG_OPENACK:
		if (size >= sizeof(struct vchiq_openack_payload)) {
			const struct vchiq_openack_payload *payload =
				(struct vchiq_openack_payload *)
				header->data;
			service->peer_version = payload->version;
		}
		dev_dbg(state->dev,
			"core: %d: prs OPENACK@%p,%x (%d->%d) v:%d\n",
			state->id, header, size, remoteport, localport,
			service->peer_version);
		if (service->srvstate == VCHIQ_SRVSTATE_OPENING) {
			service->remoteport = remoteport;
			set_service_state(service, VCHIQ_SRVSTATE_OPEN);
			complete(&service->remove_event);
		} else {
			dev_err(state->dev, "core: OPENACK received in state %s\n",
				srvstate_names[service->srvstate]);
		}
		break;
	case VCHIQ_MSG_CLOSE:
		WARN_ON(size); /* There should be no data */

		dev_dbg(state->dev, "core: %d: prs CLOSE@%p (%d->%d)\n",
			state->id, header, remoteport, localport);

		mark_service_closing_internal(service, 1);

		if (vchiq_close_service_internal(service, CLOSE_RECVD) == -EAGAIN)
			goto bail_not_ready;

		dev_dbg(state->dev, "core: Close Service %p4cc s:%u d:%d\n",
			&service->base.fourcc, service->localport, service->remoteport);
		break;
	case VCHIQ_MSG_DATA:
		dev_dbg(state->dev, "core: %d: prs DATA@%p,%x (%d->%d)\n",
			state->id, header, size, remoteport, localport);

		if ((service->remoteport == remoteport) &&
		    (service->srvstate == VCHIQ_SRVSTATE_OPEN)) {
			header->msgid = msgid | VCHIQ_MSGID_CLAIMED;
			claim_slot(state->rx_info);
			DEBUG_TRACE(PARSE_LINE);
			if (make_service_callback(service, VCHIQ_MESSAGE_AVAILABLE, header,
						  NULL) == -EAGAIN) {
				DEBUG_TRACE(PARSE_LINE);
				goto bail_not_ready;
			}
			VCHIQ_SERVICE_STATS_INC(service, ctrl_rx_count);
			VCHIQ_SERVICE_STATS_ADD(service, ctrl_rx_bytes, size);
		} else {
			VCHIQ_STATS_INC(state, error_count);
		}
		break;
	case VCHIQ_MSG_CONNECT:
		dev_dbg(state->dev, "core: %d: prs CONNECT@%p\n",
			state->id, header);
		state->version_common =	((struct vchiq_slot_zero *)
					 state->slot_data)->version;
		complete(&state->connect);
		break;
	case VCHIQ_MSG_BULK_RX:
	case VCHIQ_MSG_BULK_TX:
		/*
		 * We should never receive a bulk request from the
		 * other side since we're not setup to perform as the
		 * master.
		 */
		WARN_ON(1);
		break;
	case VCHIQ_MSG_BULK_RX_DONE:
	case VCHIQ_MSG_BULK_TX_DONE:
		if ((service->remoteport == remoteport) &&
		    (service->srvstate != VCHIQ_SRVSTATE_FREE)) {
			struct vchiq_bulk_queue *queue;
			struct vchiq_bulk *bulk;

			queue = (type == VCHIQ_MSG_BULK_RX_DONE) ?
				&service->bulk_rx : &service->bulk_tx;

			DEBUG_TRACE(PARSE_LINE);
			if (mutex_lock_killable(&service->bulk_mutex)) {
				DEBUG_TRACE(PARSE_LINE);
				goto bail_not_ready;
			}
			if ((int)(queue->remote_insert -
				queue->local_insert) >= 0) {
				dev_err(state->dev,
					"core: %d: prs %s@%p (%d->%d) unexpected (ri=%d,li=%d)\n",
					state->id, msg_type_str(type), header, remoteport,
					localport, queue->remote_insert, queue->local_insert);
				mutex_unlock(&service->bulk_mutex);
				break;
			}
			if (queue->process != queue->remote_insert) {
				dev_err(state->dev, "%s: p %x != ri %x\n",
					__func__, queue->process,
					queue->remote_insert);
				mutex_unlock(&service->bulk_mutex);
				goto bail_not_ready;
			}

			bulk = &queue->bulks[BULK_INDEX(queue->remote_insert)];
			bulk->actual = *(int *)header->data;
			queue->remote_insert++;

			dev_dbg(state->dev, "core: %d: prs %s@%p (%d->%d) %x@%pad\n",
				state->id, msg_type_str(type), header, remoteport,
				localport, bulk->actual, &bulk->dma_addr);

			dev_dbg(state->dev, "core: %d: prs:%d %cx li=%x ri=%x p=%x\n",
				state->id, localport,
				(type == VCHIQ_MSG_BULK_RX_DONE) ? 'r' : 't',
				queue->local_insert, queue->remote_insert, queue->process);

			DEBUG_TRACE(PARSE_LINE);
			WARN_ON(queue->process == queue->local_insert);
			vchiq_complete_bulk(service->instance, bulk);
			queue->process++;
			mutex_unlock(&service->bulk_mutex);
			DEBUG_TRACE(PARSE_LINE);
			notify_bulks(service, queue, RETRY_POLL);
			DEBUG_TRACE(PARSE_LINE);
		}
		break;
	case VCHIQ_MSG_PADDING:
		dev_dbg(state->dev, "core: %d: prs PADDING@%p,%x\n",
			state->id, header, size);
		break;
	case VCHIQ_MSG_PAUSE:
		/* If initiated, signal the application thread */
		dev_dbg(state->dev, "core: %d: prs PAUSE@%p,%x\n",
			state->id, header, size);
		if (state->conn_state == VCHIQ_CONNSTATE_PAUSED) {
			dev_err(state->dev, "core: %d: PAUSE received in state PAUSED\n",
				state->id);
			break;
		}
		if (state->conn_state != VCHIQ_CONNSTATE_PAUSE_SENT) {
			/* Send a PAUSE in response */
			if (queue_message(state, NULL, MAKE_PAUSE, NULL, NULL, 0,
					  QMFLAGS_NO_MUTEX_UNLOCK) == -EINTR)
				goto bail_not_ready;
		}
		/* At this point slot_mutex is held */
		vchiq_set_conn_state(state, VCHIQ_CONNSTATE_PAUSED);
		break;
	case VCHIQ_MSG_RESUME:
		dev_dbg(state->dev, "core: %d: prs RESUME@%p,%x\n",
			state->id, header, size);
		/* Release the slot mutex */
		mutex_unlock(&state->slot_mutex);
		vchiq_set_conn_state(state, VCHIQ_CONNSTATE_CONNECTED);
		break;

	case VCHIQ_MSG_REMOTE_USE:
		vchiq_on_remote_use(state);
		break;
	case VCHIQ_MSG_REMOTE_RELEASE:
		vchiq_on_remote_release(state);
		break;
	case VCHIQ_MSG_REMOTE_USE_ACTIVE:
		break;

	default:
		dev_err(state->dev, "core: %d: prs invalid msgid %x@%p,%x\n",
			state->id, msgid, header, size);
		WARN(1, "invalid message\n");
		break;
	}

skip_message:
	ret = size;

bail_not_ready:
	if (service)
		vchiq_service_put(service);

	return ret;
}

/* Called by the slot handler thread */
static void
parse_rx_slots(struct vchiq_state *state)
{
	struct vchiq_shared_state *remote = state->remote;
	int tx_pos;

	DEBUG_INITIALISE(state->local);

	tx_pos = remote->tx_pos;

	while (state->rx_pos != tx_pos) {
		struct vchiq_header *header;
		int size;

		DEBUG_TRACE(PARSE_LINE);
		if (!state->rx_data) {
			int rx_index;

			WARN_ON(state->rx_pos & VCHIQ_SLOT_MASK);
			rx_index = remote->slot_queue[
				SLOT_QUEUE_INDEX_FROM_POS_MASKED(state->rx_pos)];
			state->rx_data = (char *)SLOT_DATA_FROM_INDEX(state,
				rx_index);
			state->rx_info = SLOT_INFO_FROM_INDEX(state, rx_index);

			/*
			 * Initialise use_count to one, and increment
			 * release_count at the end of the slot to avoid
			 * releasing the slot prematurely.
			 */
			state->rx_info->use_count = 1;
			state->rx_info->release_count = 0;
		}

		header = (struct vchiq_header *)(state->rx_data +
			(state->rx_pos & VCHIQ_SLOT_MASK));
		size = parse_message(state, header);
		if (size < 0)
			return;

		state->rx_pos += calc_stride(size);

		DEBUG_TRACE(PARSE_LINE);
		/*
		 * Perform some housekeeping when the end of the slot is
		 * reached.
		 */
		if ((state->rx_pos & VCHIQ_SLOT_MASK) == 0) {
			/* Remove the extra reference count. */
			release_slot(state, state->rx_info, NULL, NULL);
			state->rx_data = NULL;
		}
	}
}

/**
 * handle_poll() - handle service polling and other rare conditions
 * @state:  vchiq state struct
 *
 * Context: Process context
 *
 * Return:
 * * 0        - poll handled successful
 * * -EAGAIN  - retry later
 */
static int
handle_poll(struct vchiq_state *state)
{
	switch (state->conn_state) {
	case VCHIQ_CONNSTATE_CONNECTED:
		/* Poll the services as requested */
		poll_services(state);
		break;

	case VCHIQ_CONNSTATE_PAUSING:
		if (queue_message(state, NULL, MAKE_PAUSE, NULL, NULL, 0,
				  QMFLAGS_NO_MUTEX_UNLOCK) != -EINTR) {
			vchiq_set_conn_state(state, VCHIQ_CONNSTATE_PAUSE_SENT);
		} else {
			/* Retry later */
			return -EAGAIN;
		}
		break;

	case VCHIQ_CONNSTATE_RESUMING:
		if (queue_message(state, NULL, MAKE_RESUME, NULL, NULL, 0,
				  QMFLAGS_NO_MUTEX_LOCK) != -EINTR) {
			vchiq_set_conn_state(state, VCHIQ_CONNSTATE_CONNECTED);
		} else {
			/*
			 * This should really be impossible,
			 * since the PAUSE should have flushed
			 * through outstanding messages.
			 */
			dev_err(state->dev, "core: Failed to send RESUME message\n");
		}
		break;
	default:
		break;
	}

	return 0;
}

/* Called by the slot handler thread */
static int
slot_handler_func(void *v)
{
	struct vchiq_state *state = v;
	struct vchiq_shared_state *local = state->local;
	int ret;

	DEBUG_INITIALISE(local);

	while (!kthread_should_stop()) {
		DEBUG_COUNT(SLOT_HANDLER_COUNT);
		DEBUG_TRACE(SLOT_HANDLER_LINE);
		ret = remote_event_wait(&state->trigger_event, &local->trigger);
		if (ret)
			return ret;

		/* Ensure that reads don't overtake the remote_event_wait. */
		rmb();

		DEBUG_TRACE(SLOT_HANDLER_LINE);
		if (state->poll_needed) {
			state->poll_needed = 0;

			/*
			 * Handle service polling and other rare conditions here
			 * out of the mainline code
			 */
			if (handle_poll(state) == -EAGAIN)
				state->poll_needed = 1;
		}

		DEBUG_TRACE(SLOT_HANDLER_LINE);
		parse_rx_slots(state);
	}
	return 0;
}

/* Called by the recycle thread */
static int
recycle_func(void *v)
{
	struct vchiq_state *state = v;
	struct vchiq_shared_state *local = state->local;
	u32 *found;
	size_t length;
	int ret;

	length = sizeof(*found) * BITSET_SIZE(VCHIQ_MAX_SERVICES);

	found = kmalloc_array(BITSET_SIZE(VCHIQ_MAX_SERVICES), sizeof(*found),
			      GFP_KERNEL);
	if (!found)
		return -ENOMEM;

	while (!kthread_should_stop()) {
		ret = remote_event_wait(&state->recycle_event, &local->recycle);
		if (ret)
			return ret;

		process_free_queue(state, found, length);
	}
	return 0;
}

/* Called by the sync thread */
static int
sync_func(void *v)
{
	struct vchiq_state *state = v;
	struct vchiq_shared_state *local = state->local;
	struct vchiq_header *header =
		(struct vchiq_header *)SLOT_DATA_FROM_INDEX(state,
			state->remote->slot_sync);
	int svc_fourcc;
	int ret;

	while (!kthread_should_stop()) {
		struct vchiq_service *service;
		int msgid, size;
		int type;
		unsigned int localport, remoteport;

		ret = remote_event_wait(&state->sync_trigger_event, &local->sync_trigger);
		if (ret)
			return ret;

		/* Ensure that reads don't overtake the remote_event_wait. */
		rmb();

		msgid = header->msgid;
		size = header->size;
		type = VCHIQ_MSG_TYPE(msgid);
		localport = VCHIQ_MSG_DSTPORT(msgid);
		remoteport = VCHIQ_MSG_SRCPORT(msgid);

		service = find_service_by_port(state, localport);

		if (!service) {
			dev_err(state->dev,
				"sync: %d: sf %s@%p (%d->%d) - invalid/closed service %d\n",
				state->id, msg_type_str(type), header, remoteport,
				localport, localport);
			release_message_sync(state, header);
			continue;
		}

		svc_fourcc = service->base.fourcc;

		dev_dbg(state->dev, "sync: Rcvd Msg %s from %p4cc s:%d d:%d len:%d\n",
			msg_type_str(type), &svc_fourcc, remoteport, localport, size);
		if (size > 0)
			vchiq_log_dump_mem(state->dev, "Rcvd", 0, header->data, min(16, size));

		switch (type) {
		case VCHIQ_MSG_OPENACK:
			if (size >= sizeof(struct vchiq_openack_payload)) {
				const struct vchiq_openack_payload *payload =
					(struct vchiq_openack_payload *)
					header->data;
				service->peer_version = payload->version;
			}
			dev_err(state->dev, "sync: %d: sf OPENACK@%p,%x (%d->%d) v:%d\n",
				state->id, header, size, remoteport, localport,
				service->peer_version);
			if (service->srvstate == VCHIQ_SRVSTATE_OPENING) {
				service->remoteport = remoteport;
				set_service_state(service, VCHIQ_SRVSTATE_OPENSYNC);
				service->sync = 1;
				complete(&service->remove_event);
			}
			release_message_sync(state, header);
			break;

		case VCHIQ_MSG_DATA:
			dev_dbg(state->dev, "sync: %d: sf DATA@%p,%x (%d->%d)\n",
				state->id, header, size, remoteport, localport);

			if ((service->remoteport == remoteport) &&
			    (service->srvstate == VCHIQ_SRVSTATE_OPENSYNC)) {
				if (make_service_callback(service, VCHIQ_MESSAGE_AVAILABLE, header,
							  NULL) == -EAGAIN)
					dev_err(state->dev,
						"sync: error: synchronous callback to service %d returns -EAGAIN\n",
						localport);
			}
			break;

		default:
			dev_err(state->dev, "sync: error: %d: sf unexpected msgid %x@%p,%x\n",
				state->id, msgid, header, size);
			release_message_sync(state, header);
			break;
		}

		vchiq_service_put(service);
	}

	return 0;
}

inline const char *
get_conn_state_name(enum vchiq_connstate conn_state)
{
	return conn_state_names[conn_state];
}

struct vchiq_slot_zero *
vchiq_init_slots(struct device *dev, void *mem_base, int mem_size)
{
	int mem_align =
		(int)((VCHIQ_SLOT_SIZE - (long)mem_base) & VCHIQ_SLOT_MASK);
	struct vchiq_slot_zero *slot_zero =
		(struct vchiq_slot_zero *)(mem_base + mem_align);
	int num_slots = (mem_size - mem_align) / VCHIQ_SLOT_SIZE;
	int first_data_slot = VCHIQ_SLOT_ZERO_SLOTS;

	check_sizes();

	/* Ensure there is enough memory to run an absolutely minimum system */
	num_slots -= first_data_slot;

	if (num_slots < 4) {
		dev_err(dev, "core: %s: Insufficient memory %x bytes\n",
			__func__, mem_size);
		return NULL;
	}

	memset(slot_zero, 0, sizeof(struct vchiq_slot_zero));

	slot_zero->magic = VCHIQ_MAGIC;
	slot_zero->version = VCHIQ_VERSION;
	slot_zero->version_min = VCHIQ_VERSION_MIN;
	slot_zero->slot_zero_size = sizeof(struct vchiq_slot_zero);
	slot_zero->slot_size = VCHIQ_SLOT_SIZE;
	slot_zero->max_slots = VCHIQ_MAX_SLOTS;
	slot_zero->max_slots_per_side = VCHIQ_MAX_SLOTS_PER_SIDE;

	slot_zero->master.slot_sync = first_data_slot;
	slot_zero->master.slot_first = first_data_slot + 1;
	slot_zero->master.slot_last = first_data_slot + (num_slots / 2) - 1;
	slot_zero->slave.slot_sync = first_data_slot + (num_slots / 2);
	slot_zero->slave.slot_first = first_data_slot + (num_slots / 2) + 1;
	slot_zero->slave.slot_last = first_data_slot + num_slots - 1;

	return slot_zero;
}

int
vchiq_init_state(struct vchiq_state *state, struct vchiq_slot_zero *slot_zero, struct device *dev)
{
	struct vchiq_shared_state *local;
	struct vchiq_shared_state *remote;
	char threadname[16];
	int i, ret;

	local = &slot_zero->slave;
	remote = &slot_zero->master;

	if (local->initialised) {
		if (remote->initialised)
			dev_err(dev, "local state has already been initialised\n");
		else
			dev_err(dev, "master/slave mismatch two slaves\n");

		return -EINVAL;
	}

	memset(state, 0, sizeof(struct vchiq_state));

	state->dev = dev;

	/*
	 * initialize shared state pointers
	 */

	state->local = local;
	state->remote = remote;
	state->slot_data = (struct vchiq_slot *)slot_zero;

	/*
	 * initialize events and mutexes
	 */

	init_completion(&state->connect);
	mutex_init(&state->mutex);
	mutex_init(&state->slot_mutex);
	mutex_init(&state->recycle_mutex);
	mutex_init(&state->sync_mutex);

	spin_lock_init(&state->msg_queue_spinlock);
	spin_lock_init(&state->bulk_waiter_spinlock);
	spin_lock_init(&state->quota_spinlock);

	init_completion(&state->slot_available_event);
	init_completion(&state->data_quota_event);

	state->slot_queue_available = 0;

	for (i = 0; i < VCHIQ_MAX_SERVICES; i++) {
		struct vchiq_service_quota *quota = &state->service_quotas[i];

		init_completion(&quota->quota_event);
	}

	for (i = local->slot_first; i <= local->slot_last; i++) {
		local->slot_queue[state->slot_queue_available] = i;
		state->slot_queue_available++;
		complete(&state->slot_available_event);
	}

	state->default_slot_quota = state->slot_queue_available / 2;
	state->default_message_quota =
		min_t(unsigned short, state->default_slot_quota * 256, ~0);

	state->previous_data_index = -1;
	state->data_use_count = 0;
	state->data_quota = state->slot_queue_available - 1;

	remote_event_create(&state->trigger_event, &local->trigger);
	local->tx_pos = 0;
	remote_event_create(&state->recycle_event, &local->recycle);
	local->slot_queue_recycle = state->slot_queue_available;
	remote_event_create(&state->sync_trigger_event, &local->sync_trigger);
	remote_event_create(&state->sync_release_event, &local->sync_release);

	/* At start-of-day, the slot is empty and available */
	((struct vchiq_header *)
		SLOT_DATA_FROM_INDEX(state, local->slot_sync))->msgid =
							VCHIQ_MSGID_PADDING;
	remote_event_signal_local(&state->sync_release_event, &local->sync_release);

	local->debug[DEBUG_ENTRIES] = DEBUG_MAX;

	ret = vchiq_platform_init_state(state);
	if (ret)
		return ret;

	/*
	 * bring up slot handler thread
	 */
	snprintf(threadname, sizeof(threadname), "vchiq-slot/%d", state->id);
	state->slot_handler_thread = kthread_create(&slot_handler_func, (void *)state, threadname);

	if (IS_ERR(state->slot_handler_thread)) {
		dev_err(state->dev, "couldn't create thread %s\n", threadname);
		return PTR_ERR(state->slot_handler_thread);
	}
	set_user_nice(state->slot_handler_thread, -19);

	snprintf(threadname, sizeof(threadname), "vchiq-recy/%d", state->id);
	state->recycle_thread = kthread_create(&recycle_func, (void *)state, threadname);
	if (IS_ERR(state->recycle_thread)) {
		dev_err(state->dev, "couldn't create thread %s\n", threadname);
		ret = PTR_ERR(state->recycle_thread);
		goto fail_free_handler_thread;
	}
	set_user_nice(state->recycle_thread, -19);

	snprintf(threadname, sizeof(threadname), "vchiq-sync/%d", state->id);
	state->sync_thread = kthread_create(&sync_func, (void *)state, threadname);
	if (IS_ERR(state->sync_thread)) {
		dev_err(state->dev, "couldn't create thread %s\n", threadname);
		ret = PTR_ERR(state->sync_thread);
		goto fail_free_recycle_thread;
	}
	set_user_nice(state->sync_thread, -20);

	wake_up_process(state->slot_handler_thread);
	wake_up_process(state->recycle_thread);
	wake_up_process(state->sync_thread);

	/* Indicate readiness to the other side */
	local->initialised = 1;

	return 0;

fail_free_recycle_thread:
	kthread_stop(state->recycle_thread);
fail_free_handler_thread:
	kthread_stop(state->slot_handler_thread);

	return ret;
}

void vchiq_msg_queue_push(struct vchiq_instance *instance, unsigned int handle,
			  struct vchiq_header *header)
{
	struct vchiq_service *service = find_service_by_handle(instance, handle);
	int pos;

	if (!service)
		return;

	while (service->msg_queue_write == service->msg_queue_read +
		VCHIQ_MAX_SLOTS) {
		if (wait_for_completion_interruptible(&service->msg_queue_pop))
			flush_signals(current);
	}

	pos = service->msg_queue_write & (VCHIQ_MAX_SLOTS - 1);
	service->msg_queue_write++;
	service->msg_queue[pos] = header;

	complete(&service->msg_queue_push);
}
EXPORT_SYMBOL(vchiq_msg_queue_push);

struct vchiq_header *vchiq_msg_hold(struct vchiq_instance *instance, unsigned int handle)
{
	struct vchiq_service *service = find_service_by_handle(instance, handle);
	struct vchiq_header *header;
	int pos;

	if (!service)
		return NULL;

	if (service->msg_queue_write == service->msg_queue_read)
		return NULL;

	while (service->msg_queue_write == service->msg_queue_read) {
		if (wait_for_completion_interruptible(&service->msg_queue_push))
			flush_signals(current);
	}

	pos = service->msg_queue_read & (VCHIQ_MAX_SLOTS - 1);
	service->msg_queue_read++;
	header = service->msg_queue[pos];

	complete(&service->msg_queue_pop);

	return header;
}
EXPORT_SYMBOL(vchiq_msg_hold);

static int vchiq_validate_params(struct vchiq_state *state,
				 const struct vchiq_service_params_kernel *params)
{
	if (!params->callback || !params->fourcc) {
		dev_err(state->dev, "Can't add service, invalid params\n");
		return -EINVAL;
	}

	return 0;
}

/* Called from application thread when a client or server service is created. */
struct vchiq_service *
vchiq_add_service_internal(struct vchiq_state *state,
			   const struct vchiq_service_params_kernel *params,
			   int srvstate, struct vchiq_instance *instance,
			   void (*userdata_term)(void *userdata))
{
	struct vchiq_service *service;
	struct vchiq_service __rcu **pservice = NULL;
	struct vchiq_service_quota *quota;
	int ret;
	int i;

	ret = vchiq_validate_params(state, params);
	if (ret)
		return NULL;

	service = kzalloc(sizeof(*service), GFP_KERNEL);
	if (!service)
		return service;

	service->base.fourcc   = params->fourcc;
	service->base.callback = params->callback;
	service->base.userdata = params->userdata;
	service->handle        = VCHIQ_SERVICE_HANDLE_INVALID;
	kref_init(&service->ref_count);
	service->srvstate      = VCHIQ_SRVSTATE_FREE;
	service->userdata_term = userdata_term;
	service->localport     = VCHIQ_PORT_FREE;
	service->remoteport    = VCHIQ_PORT_FREE;

	service->public_fourcc = (srvstate == VCHIQ_SRVSTATE_OPENING) ?
		VCHIQ_FOURCC_INVALID : params->fourcc;
	service->auto_close    = 1;
	atomic_set(&service->poll_flags, 0);
	service->version       = params->version;
	service->version_min   = params->version_min;
	service->state         = state;
	service->instance      = instance;
	init_completion(&service->remove_event);
	init_completion(&service->bulk_remove_event);
	init_completion(&service->msg_queue_pop);
	init_completion(&service->msg_queue_push);
	mutex_init(&service->bulk_mutex);

	/*
	 * Although it is perfectly possible to use a spinlock
	 * to protect the creation of services, it is overkill as it
	 * disables interrupts while the array is searched.
	 * The only danger is of another thread trying to create a
	 * service - service deletion is safe.
	 * Therefore it is preferable to use state->mutex which,
	 * although slower to claim, doesn't block interrupts while
	 * it is held.
	 */

	mutex_lock(&state->mutex);

	/* Prepare to use a previously unused service */
	if (state->unused_service < VCHIQ_MAX_SERVICES)
		pservice = &state->services[state->unused_service];

	if (srvstate == VCHIQ_SRVSTATE_OPENING) {
		for (i = 0; i < state->unused_service; i++) {
			if (!rcu_access_pointer(state->services[i])) {
				pservice = &state->services[i];
				break;
			}
		}
	} else {
		rcu_read_lock();
		for (i = (state->unused_service - 1); i >= 0; i--) {
			struct vchiq_service *srv;

			srv = rcu_dereference(state->services[i]);
			if (!srv) {
				pservice = &state->services[i];
			} else if ((srv->public_fourcc == params->fourcc) &&
				   ((srv->instance != instance) ||
				   (srv->base.callback != params->callback))) {
				/*
				 * There is another server using this
				 * fourcc which doesn't match.
				 */
				pservice = NULL;
				break;
			}
		}
		rcu_read_unlock();
	}

	if (pservice) {
		service->localport = (pservice - state->services);
		if (!handle_seq)
			handle_seq = VCHIQ_MAX_STATES *
				 VCHIQ_MAX_SERVICES;
		service->handle = handle_seq |
			(state->id * VCHIQ_MAX_SERVICES) |
			service->localport;
		handle_seq += VCHIQ_MAX_STATES * VCHIQ_MAX_SERVICES;
		rcu_assign_pointer(*pservice, service);
		if (pservice == &state->services[state->unused_service])
			state->unused_service++;
	}

	mutex_unlock(&state->mutex);

	if (!pservice) {
		kfree(service);
		return NULL;
	}

	quota = &state->service_quotas[service->localport];
	quota->slot_quota = state->default_slot_quota;
	quota->message_quota = state->default_message_quota;
	if (quota->slot_use_count == 0)
		quota->previous_tx_index =
			SLOT_QUEUE_INDEX_FROM_POS(state->local_tx_pos)
			- 1;

	/* Bring this service online */
	set_service_state(service, srvstate);

	dev_dbg(state->dev, "core_msg: %s Service %p4cc SrcPort:%d\n",
		(srvstate == VCHIQ_SRVSTATE_OPENING) ? "Open" : "Add",
		&params->fourcc, service->localport);

	/* Don't unlock the service - leave it with a ref_count of 1. */

	return service;
}

int
vchiq_open_service_internal(struct vchiq_service *service, int client_id)
{
	struct vchiq_open_payload payload = {
		service->base.fourcc,
		client_id,
		service->version,
		service->version_min
	};
	int status = 0;

	service->client_id = client_id;
	vchiq_use_service_internal(service);
	status = queue_message(service->state,
			       NULL, MAKE_OPEN(service->localport),
			       memcpy_copy_callback,
			       &payload,
			       sizeof(payload),
			       QMFLAGS_IS_BLOCKING);

	if (status)
		return status;

	/* Wait for the ACK/NAK */
	if (wait_for_completion_interruptible(&service->remove_event)) {
		status = -EAGAIN;
		vchiq_release_service_internal(service);
	} else if ((service->srvstate != VCHIQ_SRVSTATE_OPEN) &&
		   (service->srvstate != VCHIQ_SRVSTATE_OPENSYNC)) {
		if (service->srvstate != VCHIQ_SRVSTATE_CLOSEWAIT)
			dev_err(service->state->dev,
				"core: %d: osi - srvstate = %s (ref %u)\n",
				service->state->id, srvstate_names[service->srvstate],
				kref_read(&service->ref_count));
		status = -EINVAL;
		VCHIQ_SERVICE_STATS_INC(service, error_count);
		vchiq_release_service_internal(service);
	}

	return status;
}

static void
release_service_messages(struct vchiq_service *service)
{
	struct vchiq_state *state = service->state;
	int slot_last = state->remote->slot_last;
	int i;

	/* Release any claimed messages aimed at this service */

	if (service->sync) {
		struct vchiq_header *header =
			(struct vchiq_header *)SLOT_DATA_FROM_INDEX(state,
						state->remote->slot_sync);
		if (VCHIQ_MSG_DSTPORT(header->msgid) == service->localport)
			release_message_sync(state, header);

		return;
	}

	for (i = state->remote->slot_first; i <= slot_last; i++) {
		struct vchiq_slot_info *slot_info =
			SLOT_INFO_FROM_INDEX(state, i);
		unsigned int pos, end;
		char *data;

		if (slot_info->release_count == slot_info->use_count)
			continue;

		data = (char *)SLOT_DATA_FROM_INDEX(state, i);
		end = VCHIQ_SLOT_SIZE;
		if (data == state->rx_data)
			/*
			 * This buffer is still being read from - stop
			 * at the current read position
			 */
			end = state->rx_pos & VCHIQ_SLOT_MASK;

		pos = 0;

		while (pos < end) {
			struct vchiq_header *header =
				(struct vchiq_header *)(data + pos);
			int msgid = header->msgid;
			int port = VCHIQ_MSG_DSTPORT(msgid);

			if ((port == service->localport) && (msgid & VCHIQ_MSGID_CLAIMED)) {
				dev_dbg(state->dev, "core:  fsi - hdr %p\n", header);
				release_slot(state, slot_info, header, NULL);
			}
			pos += calc_stride(header->size);
			if (pos > VCHIQ_SLOT_SIZE) {
				dev_err(state->dev,
					"core: fsi - pos %x: header %p, msgid %x, header->msgid %x, header->size %x\n",
					pos, header, msgid, header->msgid, header->size);
				WARN(1, "invalid slot position\n");
			}
		}
	}
}

static int
do_abort_bulks(struct vchiq_service *service)
{
	int status;

	/* Abort any outstanding bulk transfers */
	if (mutex_lock_killable(&service->bulk_mutex))
		return 0;
	abort_outstanding_bulks(service, &service->bulk_tx);
	abort_outstanding_bulks(service, &service->bulk_rx);
	mutex_unlock(&service->bulk_mutex);

	status = notify_bulks(service, &service->bulk_tx, NO_RETRY_POLL);
	if (status)
		return 0;

	status = notify_bulks(service, &service->bulk_rx, NO_RETRY_POLL);
	return !status;
}

static int
close_service_complete(struct vchiq_service *service, int failstate)
{
	int status;
	int is_server = (service->public_fourcc != VCHIQ_FOURCC_INVALID);
	int newstate;

	switch (service->srvstate) {
	case VCHIQ_SRVSTATE_OPEN:
	case VCHIQ_SRVSTATE_CLOSESENT:
	case VCHIQ_SRVSTATE_CLOSERECVD:
		if (is_server) {
			if (service->auto_close) {
				service->client_id = 0;
				service->remoteport = VCHIQ_PORT_FREE;
				newstate = VCHIQ_SRVSTATE_LISTENING;
			} else {
				newstate = VCHIQ_SRVSTATE_CLOSEWAIT;
			}
		} else {
			newstate = VCHIQ_SRVSTATE_CLOSED;
		}
		set_service_state(service, newstate);
		break;
	case VCHIQ_SRVSTATE_LISTENING:
		break;
	default:
		dev_err(service->state->dev, "core: (%x) called in state %s\n",
			service->handle, srvstate_names[service->srvstate]);
		WARN(1, "%s in unexpected state\n", __func__);
		return -EINVAL;
	}

	status = make_service_callback(service, VCHIQ_SERVICE_CLOSED, NULL, NULL);

	if (status != -EAGAIN) {
		int uc = service->service_use_count;
		int i;
		/* Complete the close process */
		for (i = 0; i < uc; i++)
			/*
			 * cater for cases where close is forced and the
			 * client may not close all it's handles
			 */
			vchiq_release_service_internal(service);

		service->client_id = 0;
		service->remoteport = VCHIQ_PORT_FREE;

		if (service->srvstate == VCHIQ_SRVSTATE_CLOSED) {
			vchiq_free_service_internal(service);
		} else if (service->srvstate != VCHIQ_SRVSTATE_CLOSEWAIT) {
			if (is_server)
				service->closing = 0;

			complete(&service->remove_event);
		}
	} else {
		set_service_state(service, failstate);
	}

	return status;
}

/*
 * Prepares a bulk transfer to be queued. The function is interruptible and is
 * intended to be called from user threads. It may return -EAGAIN to indicate
 * that a signal has been received and the call should be retried after being
 * returned to user context.
 */
static int
vchiq_bulk_xfer_queue_msg_killable(struct vchiq_service *service,
				   struct vchiq_bulk *bulk_params)
{
	struct vchiq_bulk_queue *queue;
	struct bulk_waiter *bulk_waiter = NULL;
	struct vchiq_bulk *bulk;
	struct vchiq_state *state = service->state;
	const char dir_char = (bulk_params->dir == VCHIQ_BULK_TRANSMIT) ? 't' : 'r';
	const int dir_msgtype = (bulk_params->dir == VCHIQ_BULK_TRANSMIT) ?
		VCHIQ_MSG_BULK_TX : VCHIQ_MSG_BULK_RX;
	int status = -EINVAL;
	int payload[2];

	if (bulk_params->mode == VCHIQ_BULK_MODE_BLOCKING) {
		bulk_waiter = bulk_params->waiter;
		init_completion(&bulk_waiter->event);
		bulk_waiter->actual = 0;
		bulk_waiter->bulk = NULL;
	}

	queue = (bulk_params->dir == VCHIQ_BULK_TRANSMIT) ?
		&service->bulk_tx : &service->bulk_rx;

	if (mutex_lock_killable(&service->bulk_mutex))
		return -EINTR;

	if (queue->local_insert == queue->remove + VCHIQ_NUM_SERVICE_BULKS) {
		VCHIQ_SERVICE_STATS_INC(service, bulk_stalls);
		do {
			mutex_unlock(&service->bulk_mutex);
			if (wait_for_completion_killable(&service->bulk_remove_event))
				return -EINTR;
			if (mutex_lock_killable(&service->bulk_mutex))
				return -EINTR;
		} while (queue->local_insert == queue->remove +
				VCHIQ_NUM_SERVICE_BULKS);
	}

	bulk = &queue->bulks[BULK_INDEX(queue->local_insert)];

	/* Initiliaze the 'bulk' slot with bulk parameters passed in. */
	bulk->mode = bulk_params->mode;
	bulk->dir = bulk_params->dir;
	bulk->waiter = bulk_params->waiter;
	bulk->cb_data = bulk_params->cb_data;
	bulk->cb_userdata = bulk_params->cb_userdata;
	bulk->size = bulk_params->size;
	bulk->offset = bulk_params->offset;
	bulk->uoffset = bulk_params->uoffset;
	bulk->actual = VCHIQ_BULK_ACTUAL_ABORTED;

	if (vchiq_prepare_bulk_data(service->instance, bulk))
		goto unlock_error_exit;

	/*
	 * Ensure that the bulk data record is visible to the peer
	 * before proceeding.
	 */
	wmb();

	dev_dbg(state->dev, "core: %d: bt (%d->%d) %cx %x@%pad %p\n",
		state->id, service->localport, service->remoteport,
		dir_char, bulk->size, &bulk->dma_addr, bulk->cb_data);

	/*
	 * The slot mutex must be held when the service is being closed, so
	 * claim it here to ensure that isn't happening
	 */
	if (mutex_lock_killable(&state->slot_mutex)) {
		status = -EINTR;
		goto cancel_bulk_error_exit;
	}

	if (service->srvstate != VCHIQ_SRVSTATE_OPEN)
		goto unlock_both_error_exit;

	payload[0] = lower_32_bits(bulk->dma_addr);
	payload[1] = bulk->size;
	status = queue_message(state,
			       NULL,
			       VCHIQ_MAKE_MSG(dir_msgtype,
					      service->localport,
					      service->remoteport),
			       memcpy_copy_callback,
			       &payload,
			       sizeof(payload),
			       QMFLAGS_IS_BLOCKING |
			       QMFLAGS_NO_MUTEX_LOCK |
			       QMFLAGS_NO_MUTEX_UNLOCK);
	if (status)
		goto unlock_both_error_exit;

	queue->local_insert++;

	mutex_unlock(&state->slot_mutex);
	mutex_unlock(&service->bulk_mutex);

	dev_dbg(state->dev, "core: %d: bt:%d %cx li=%x ri=%x p=%x\n",
		state->id, service->localport, dir_char, queue->local_insert,
		queue->remote_insert, queue->process);

	if (bulk_waiter) {
		bulk_waiter->bulk = bulk;
		if (wait_for_completion_killable(&bulk_waiter->event))
			status = -EINTR;
		else if (bulk_waiter->actual == VCHIQ_BULK_ACTUAL_ABORTED)
			status = -EINVAL;
	}

	return status;

unlock_both_error_exit:
	mutex_unlock(&state->slot_mutex);
cancel_bulk_error_exit:
	vchiq_complete_bulk(service->instance, bulk);
unlock_error_exit:
	mutex_unlock(&service->bulk_mutex);

	return status;
}

/* Called by the slot handler */
int
vchiq_close_service_internal(struct vchiq_service *service, int close_recvd)
{
	struct vchiq_state *state = service->state;
	int status = 0;
	int is_server = (service->public_fourcc != VCHIQ_FOURCC_INVALID);
	int close_id = MAKE_CLOSE(service->localport,
				  VCHIQ_MSG_DSTPORT(service->remoteport));

	dev_dbg(state->dev, "core: %d: csi:%d,%d (%s)\n",
		service->state->id, service->localport, close_recvd,
		srvstate_names[service->srvstate]);

	switch (service->srvstate) {
	case VCHIQ_SRVSTATE_CLOSED:
	case VCHIQ_SRVSTATE_HIDDEN:
	case VCHIQ_SRVSTATE_LISTENING:
	case VCHIQ_SRVSTATE_CLOSEWAIT:
		if (close_recvd) {
			dev_err(state->dev, "core: (1) called in state %s\n",
				srvstate_names[service->srvstate]);
			break;
		} else if (!is_server) {
			vchiq_free_service_internal(service);
			break;
		}

		if (service->srvstate == VCHIQ_SRVSTATE_LISTENING) {
			status = -EINVAL;
		} else {
			service->client_id = 0;
			service->remoteport = VCHIQ_PORT_FREE;
			if (service->srvstate == VCHIQ_SRVSTATE_CLOSEWAIT)
				set_service_state(service, VCHIQ_SRVSTATE_LISTENING);
		}
		complete(&service->remove_event);
		break;
	case VCHIQ_SRVSTATE_OPENING:
		if (close_recvd) {
			/* The open was rejected - tell the user */
			set_service_state(service, VCHIQ_SRVSTATE_CLOSEWAIT);
			complete(&service->remove_event);
		} else {
			/* Shutdown mid-open - let the other side know */
			status = queue_message(state, service, close_id, NULL, NULL, 0, 0);
		}
		break;

	case VCHIQ_SRVSTATE_OPENSYNC:
		mutex_lock(&state->sync_mutex);
		fallthrough;
	case VCHIQ_SRVSTATE_OPEN:
		if (close_recvd) {
			if (!do_abort_bulks(service))
				status = -EAGAIN;
		}

		release_service_messages(service);

		if (!status)
			status = queue_message(state, service, close_id, NULL,
					       NULL, 0, QMFLAGS_NO_MUTEX_UNLOCK);

		if (status) {
			if (service->srvstate == VCHIQ_SRVSTATE_OPENSYNC)
				mutex_unlock(&state->sync_mutex);
			break;
		}

		if (!close_recvd) {
			/* Change the state while the mutex is still held */
			set_service_state(service, VCHIQ_SRVSTATE_CLOSESENT);
			mutex_unlock(&state->slot_mutex);
			if (service->sync)
				mutex_unlock(&state->sync_mutex);
			break;
		}

		/* Change the state while the mutex is still held */
		set_service_state(service, VCHIQ_SRVSTATE_CLOSERECVD);
		mutex_unlock(&state->slot_mutex);
		if (service->sync)
			mutex_unlock(&state->sync_mutex);

		status = close_service_complete(service, VCHIQ_SRVSTATE_CLOSERECVD);
		break;

	case VCHIQ_SRVSTATE_CLOSESENT:
		if (!close_recvd)
			/* This happens when a process is killed mid-close */
			break;

		if (!do_abort_bulks(service)) {
			status = -EAGAIN;
			break;
		}

		if (!status)
			status = close_service_complete(service, VCHIQ_SRVSTATE_CLOSERECVD);
		break;

	case VCHIQ_SRVSTATE_CLOSERECVD:
		if (!close_recvd && is_server)
			/* Force into LISTENING mode */
			set_service_state(service, VCHIQ_SRVSTATE_LISTENING);
		status = close_service_complete(service, VCHIQ_SRVSTATE_CLOSERECVD);
		break;

	default:
		dev_err(state->dev, "core: (%d) called in state %s\n",
			close_recvd, srvstate_names[service->srvstate]);
		break;
	}

	return status;
}

/* Called from the application process upon process death */
void
vchiq_terminate_service_internal(struct vchiq_service *service)
{
	struct vchiq_state *state = service->state;

	dev_dbg(state->dev, "core: %d: tsi - (%d<->%d)\n",
		state->id, service->localport, service->remoteport);

	mark_service_closing(service);

	/* Mark the service for removal by the slot handler */
	request_poll(state, service, VCHIQ_POLL_REMOVE);
}

/* Called from the slot handler */
void
vchiq_free_service_internal(struct vchiq_service *service)
{
	struct vchiq_state *state = service->state;

	dev_dbg(state->dev, "core: %d: fsi - (%d)\n", state->id, service->localport);

	switch (service->srvstate) {
	case VCHIQ_SRVSTATE_OPENING:
	case VCHIQ_SRVSTATE_CLOSED:
	case VCHIQ_SRVSTATE_HIDDEN:
	case VCHIQ_SRVSTATE_LISTENING:
	case VCHIQ_SRVSTATE_CLOSEWAIT:
		break;
	default:
		dev_err(state->dev, "core: %d: fsi - (%d) in state %s\n",
			state->id, service->localport, srvstate_names[service->srvstate]);
		return;
	}

	set_service_state(service, VCHIQ_SRVSTATE_FREE);

	complete(&service->remove_event);

	/* Release the initial lock */
	vchiq_service_put(service);
}

int
vchiq_connect_internal(struct vchiq_state *state, struct vchiq_instance *instance)
{
	struct vchiq_service *service;
	int status = 0;
	int i;

	/* Find all services registered to this client and enable them. */
	i = 0;
	while ((service = next_service_by_instance(state, instance, &i)) != NULL) {
		if (service->srvstate == VCHIQ_SRVSTATE_HIDDEN)
			set_service_state(service, VCHIQ_SRVSTATE_LISTENING);
		vchiq_service_put(service);
	}

	if (state->conn_state == VCHIQ_CONNSTATE_DISCONNECTED) {
		status = queue_message(state, NULL, MAKE_CONNECT, NULL, NULL, 0,
				       QMFLAGS_IS_BLOCKING);
		if (status)
			return status;

		vchiq_set_conn_state(state, VCHIQ_CONNSTATE_CONNECTING);
	}

	if (state->conn_state == VCHIQ_CONNSTATE_CONNECTING) {
		if (wait_for_completion_interruptible(&state->connect))
			return -EAGAIN;

		vchiq_set_conn_state(state, VCHIQ_CONNSTATE_CONNECTED);
		complete(&state->connect);
	}

	return status;
}

void
vchiq_shutdown_internal(struct vchiq_state *state, struct vchiq_instance *instance)
{
	struct vchiq_service *service;
	int i;

	/* Find all services registered to this client and remove them. */
	i = 0;
	while ((service = next_service_by_instance(state, instance, &i)) != NULL) {
		(void)vchiq_remove_service(instance, service->handle);
		vchiq_service_put(service);
	}
}

int
vchiq_close_service(struct vchiq_instance *instance, unsigned int handle)
{
	/* Unregister the service */
	struct vchiq_service *service = find_service_by_handle(instance, handle);
	int status = 0;

	if (!service)
		return -EINVAL;

	dev_dbg(service->state->dev, "core: %d: close_service:%d\n",
		service->state->id, service->localport);

	if ((service->srvstate == VCHIQ_SRVSTATE_FREE) ||
	    (service->srvstate == VCHIQ_SRVSTATE_LISTENING) ||
	    (service->srvstate == VCHIQ_SRVSTATE_HIDDEN)) {
		vchiq_service_put(service);
		return -EINVAL;
	}

	mark_service_closing(service);

	if (current == service->state->slot_handler_thread) {
		status = vchiq_close_service_internal(service, NO_CLOSE_RECVD);
		WARN_ON(status == -EAGAIN);
	} else {
		/* Mark the service for termination by the slot handler */
		request_poll(service->state, service, VCHIQ_POLL_TERMINATE);
	}

	while (1) {
		if (wait_for_completion_interruptible(&service->remove_event)) {
			status = -EAGAIN;
			break;
		}

		if ((service->srvstate == VCHIQ_SRVSTATE_FREE) ||
		    (service->srvstate == VCHIQ_SRVSTATE_LISTENING) ||
		    (service->srvstate == VCHIQ_SRVSTATE_OPEN))
			break;

		dev_warn(service->state->dev,
			 "core: %d: close_service:%d - waiting in state %s\n",
			 service->state->id, service->localport,
			 srvstate_names[service->srvstate]);
	}

	if (!status &&
	    (service->srvstate != VCHIQ_SRVSTATE_FREE) &&
	    (service->srvstate != VCHIQ_SRVSTATE_LISTENING))
		status = -EINVAL;

	vchiq_service_put(service);

	return status;
}
EXPORT_SYMBOL(vchiq_close_service);

int
vchiq_remove_service(struct vchiq_instance *instance, unsigned int handle)
{
	/* Unregister the service */
	struct vchiq_service *service = find_service_by_handle(instance, handle);
	int status = 0;

	if (!service)
		return -EINVAL;

	dev_dbg(service->state->dev, "core: %d: remove_service:%d\n",
		service->state->id, service->localport);

	if (service->srvstate == VCHIQ_SRVSTATE_FREE) {
		vchiq_service_put(service);
		return -EINVAL;
	}

	mark_service_closing(service);

	if ((service->srvstate == VCHIQ_SRVSTATE_HIDDEN) ||
	    (current == service->state->slot_handler_thread)) {
		/*
		 * Make it look like a client, because it must be removed and
		 * not left in the LISTENING state.
		 */
		service->public_fourcc = VCHIQ_FOURCC_INVALID;

		status = vchiq_close_service_internal(service, NO_CLOSE_RECVD);
		WARN_ON(status == -EAGAIN);
	} else {
		/* Mark the service for removal by the slot handler */
		request_poll(service->state, service, VCHIQ_POLL_REMOVE);
	}
	while (1) {
		if (wait_for_completion_interruptible(&service->remove_event)) {
			status = -EAGAIN;
			break;
		}

		if ((service->srvstate == VCHIQ_SRVSTATE_FREE) ||
		    (service->srvstate == VCHIQ_SRVSTATE_OPEN))
			break;

		dev_warn(service->state->dev,
			 "core: %d: remove_service:%d - waiting in state %s\n",
			 service->state->id, service->localport,
			 srvstate_names[service->srvstate]);
	}

	if (!status && (service->srvstate != VCHIQ_SRVSTATE_FREE))
		status = -EINVAL;

	vchiq_service_put(service);

	return status;
}

int
vchiq_bulk_xfer_blocking(struct vchiq_instance *instance, unsigned int handle,
			 struct vchiq_bulk *bulk_params)
{
	struct vchiq_service *service = find_service_by_handle(instance, handle);
	int status = -EINVAL;

	if (!service)
		return -EINVAL;

	if (service->srvstate != VCHIQ_SRVSTATE_OPEN)
		goto error_exit;

	if (!bulk_params->offset && !bulk_params->uoffset)
		goto error_exit;

	if (vchiq_check_service(service))
		goto error_exit;

	status = vchiq_bulk_xfer_queue_msg_killable(service, bulk_params);

error_exit:
	vchiq_service_put(service);

	return status;
}

int
vchiq_bulk_xfer_callback(struct vchiq_instance *instance, unsigned int handle,
			 struct vchiq_bulk *bulk_params)
{
	struct vchiq_service *service = find_service_by_handle(instance, handle);
	int status = -EINVAL;

	if (!service)
		return -EINVAL;

	if (bulk_params->mode != VCHIQ_BULK_MODE_CALLBACK &&
	    bulk_params->mode != VCHIQ_BULK_MODE_NOCALLBACK)
		goto error_exit;

	if (service->srvstate != VCHIQ_SRVSTATE_OPEN)
		goto error_exit;

	if (!bulk_params->offset && !bulk_params->uoffset)
		goto error_exit;

	if (vchiq_check_service(service))
		goto error_exit;

	status = vchiq_bulk_xfer_queue_msg_killable(service, bulk_params);

error_exit:
	vchiq_service_put(service);

	return status;
}

/*
 * This function is called by VCHIQ ioctl interface and is interruptible.
 * It may receive -EAGAIN to indicate that a signal has been received
 * and the call should be retried after being returned to user context.
 */
int
vchiq_bulk_xfer_waiting(struct vchiq_instance *instance,
			unsigned int handle, struct bulk_waiter *waiter)
{
	struct vchiq_service *service = find_service_by_handle(instance, handle);
	struct bulk_waiter *bulk_waiter;
	int status = -EINVAL;

	if (!service)
		return -EINVAL;

	if (!waiter)
		goto error_exit;

	if (service->srvstate != VCHIQ_SRVSTATE_OPEN)
		goto error_exit;

	if (vchiq_check_service(service))
		goto error_exit;

	bulk_waiter = waiter;

	vchiq_service_put(service);

	status = 0;

	if (wait_for_completion_killable(&bulk_waiter->event))
		return -EINTR;
	else if (bulk_waiter->actual == VCHIQ_BULK_ACTUAL_ABORTED)
		return -EINVAL;

	return status;

error_exit:
	vchiq_service_put(service);

	return status;
}

int
vchiq_queue_message(struct vchiq_instance *instance, unsigned int handle,
		    ssize_t (*copy_callback)(void *context, void *dest,
					     size_t offset, size_t maxsize),
		    void *context,
		    size_t size)
{
	struct vchiq_service *service = find_service_by_handle(instance, handle);
	int status = -EINVAL;
	int data_id;

	if (!service)
		goto error_exit;

	if (vchiq_check_service(service))
		goto error_exit;

	if (!size) {
		VCHIQ_SERVICE_STATS_INC(service, error_count);
		goto error_exit;
	}

	if (size > VCHIQ_MAX_MSG_SIZE) {
		VCHIQ_SERVICE_STATS_INC(service, error_count);
		goto error_exit;
	}

	data_id = MAKE_DATA(service->localport, service->remoteport);

	switch (service->srvstate) {
	case VCHIQ_SRVSTATE_OPEN:
		status = queue_message(service->state, service, data_id,
				       copy_callback, context, size,
				       QMFLAGS_IS_BLOCKING);
		break;
	case VCHIQ_SRVSTATE_OPENSYNC:
		status = queue_message_sync(service->state, service, data_id,
					    copy_callback, context, size);
		break;
	default:
		status = -EINVAL;
		break;
	}

error_exit:
	if (service)
		vchiq_service_put(service);

	return status;
}

int vchiq_queue_kernel_message(struct vchiq_instance *instance, unsigned int handle, void *data,
			       unsigned int size)
{
	return vchiq_queue_message(instance, handle, memcpy_copy_callback,
				   data, size);
}
EXPORT_SYMBOL(vchiq_queue_kernel_message);

void
vchiq_release_message(struct vchiq_instance *instance, unsigned int handle,
		      struct vchiq_header *header)
{
	struct vchiq_service *service = find_service_by_handle(instance, handle);
	struct vchiq_shared_state *remote;
	struct vchiq_state *state;
	int slot_index;

	if (!service)
		return;

	state = service->state;
	remote = state->remote;

	slot_index = SLOT_INDEX_FROM_DATA(state, (void *)header);

	if ((slot_index >= remote->slot_first) &&
	    (slot_index <= remote->slot_last)) {
		int msgid = header->msgid;

		if (msgid & VCHIQ_MSGID_CLAIMED) {
			struct vchiq_slot_info *slot_info =
				SLOT_INFO_FROM_INDEX(state, slot_index);

			release_slot(state, slot_info, header, service);
		}
	} else if (slot_index == remote->slot_sync) {
		release_message_sync(state, header);
	}

	vchiq_service_put(service);
}
EXPORT_SYMBOL(vchiq_release_message);

static void
release_message_sync(struct vchiq_state *state, struct vchiq_header *header)
{
	header->msgid = VCHIQ_MSGID_PADDING;
	remote_event_signal(state, &state->remote->sync_release);
}

int
vchiq_get_peer_version(struct vchiq_instance *instance, unsigned int handle, short *peer_version)
{
	int status = -EINVAL;
	struct vchiq_service *service = find_service_by_handle(instance, handle);

	if (!service)
		goto exit;

	if (vchiq_check_service(service))
		goto exit;

	if (!peer_version)
		goto exit;

	*peer_version = service->peer_version;
	status = 0;

exit:
	if (service)
		vchiq_service_put(service);
	return status;
}
EXPORT_SYMBOL(vchiq_get_peer_version);

void vchiq_get_config(struct vchiq_config *config)
{
	config->max_msg_size           = VCHIQ_MAX_MSG_SIZE;
	config->bulk_threshold         = VCHIQ_MAX_MSG_SIZE;
	config->max_outstanding_bulks  = VCHIQ_NUM_SERVICE_BULKS;
	config->max_services           = VCHIQ_MAX_SERVICES;
	config->version                = VCHIQ_VERSION;
	config->version_min            = VCHIQ_VERSION_MIN;
}

int
vchiq_set_service_option(struct vchiq_instance *instance, unsigned int handle,
			 enum vchiq_service_option option, int value)
{
	struct vchiq_service *service = find_service_by_handle(instance, handle);
	struct vchiq_service_quota *quota;
	int ret = -EINVAL;

	if (!service)
		return -EINVAL;

	switch (option) {
	case VCHIQ_SERVICE_OPTION_AUTOCLOSE:
		service->auto_close = value;
		ret = 0;
		break;

	case VCHIQ_SERVICE_OPTION_SLOT_QUOTA:
		quota = &service->state->service_quotas[service->localport];
		if (value == 0)
			value = service->state->default_slot_quota;
		if ((value >= quota->slot_use_count) &&
		    (value < (unsigned short)~0)) {
			quota->slot_quota = value;
			if ((value >= quota->slot_use_count) &&
			    (quota->message_quota >= quota->message_use_count))
				/*
				 * Signal the service that it may have
				 * dropped below its quota
				 */
				complete(&quota->quota_event);
			ret = 0;
		}
		break;

	case VCHIQ_SERVICE_OPTION_MESSAGE_QUOTA:
		quota = &service->state->service_quotas[service->localport];
		if (value == 0)
			value = service->state->default_message_quota;
		if ((value >= quota->message_use_count) &&
		    (value < (unsigned short)~0)) {
			quota->message_quota = value;
			if ((value >= quota->message_use_count) &&
			    (quota->slot_quota >= quota->slot_use_count))
				/*
				 * Signal the service that it may have
				 * dropped below its quota
				 */
				complete(&quota->quota_event);
			ret = 0;
		}
		break;

	case VCHIQ_SERVICE_OPTION_SYNCHRONOUS:
		if ((service->srvstate == VCHIQ_SRVSTATE_HIDDEN) ||
		    (service->srvstate == VCHIQ_SRVSTATE_LISTENING)) {
			service->sync = value;
			ret = 0;
		}
		break;

	case VCHIQ_SERVICE_OPTION_TRACE:
		service->trace = value;
		ret = 0;
		break;

	default:
		break;
	}
	vchiq_service_put(service);

	return ret;
}

static void
vchiq_dump_shared_state(struct seq_file *f, struct vchiq_state *state,
			struct vchiq_shared_state *shared, const char *label)
{
	static const char *const debug_names[] = {
		"<entries>",
		"SLOT_HANDLER_COUNT",
		"SLOT_HANDLER_LINE",
		"PARSE_LINE",
		"PARSE_HEADER",
		"PARSE_MSGID",
		"AWAIT_COMPLETION_LINE",
		"DEQUEUE_MESSAGE_LINE",
		"SERVICE_CALLBACK_LINE",
		"MSG_QUEUE_FULL_COUNT",
		"COMPLETION_QUEUE_FULL_COUNT"
	};
	int i;

	seq_printf(f, "  %s: slots %d-%d tx_pos=0x%x recycle=0x%x\n",
		   label, shared->slot_first, shared->slot_last,
		   shared->tx_pos, shared->slot_queue_recycle);

	seq_puts(f, "    Slots claimed:\n");

	for (i = shared->slot_first; i <= shared->slot_last; i++) {
		struct vchiq_slot_info slot_info =
						*SLOT_INFO_FROM_INDEX(state, i);
		if (slot_info.use_count != slot_info.release_count) {
			seq_printf(f, "      %d: %d/%d\n", i, slot_info.use_count,
				   slot_info.release_count);
		}
	}

	for (i = 1; i < shared->debug[DEBUG_ENTRIES]; i++) {
		seq_printf(f, "    DEBUG: %s = %d(0x%x)\n",
			   debug_names[i], shared->debug[i], shared->debug[i]);
	}
}

static void
vchiq_dump_service_state(struct seq_file *f, struct vchiq_service *service)
{
	unsigned int ref_count;

	/*Don't include the lock just taken*/
	ref_count = kref_read(&service->ref_count) - 1;
	seq_printf(f, "Service %u: %s (ref %u)", service->localport,
		   srvstate_names[service->srvstate], ref_count);

	if (service->srvstate != VCHIQ_SRVSTATE_FREE) {
		char remoteport[30];
		struct vchiq_service_quota *quota =
			&service->state->service_quotas[service->localport];
		int fourcc = service->base.fourcc;
		int tx_pending, rx_pending, tx_size = 0, rx_size = 0;

		if (service->remoteport != VCHIQ_PORT_FREE) {
			int len2 = scnprintf(remoteport, sizeof(remoteport),
				"%u", service->remoteport);

			if (service->public_fourcc != VCHIQ_FOURCC_INVALID)
				scnprintf(remoteport + len2, sizeof(remoteport) - len2,
					  " (client 0x%x)", service->client_id);
		} else {
			strscpy(remoteport, "n/a", sizeof(remoteport));
		}

		seq_printf(f, " '%p4cc' remote %s (msg use %d/%d, slot use %d/%d)\n",
			   &fourcc, remoteport,
			   quota->message_use_count, quota->message_quota,
			   quota->slot_use_count, quota->slot_quota);

		tx_pending = service->bulk_tx.local_insert -
			service->bulk_tx.remote_insert;
		if (tx_pending) {
			unsigned int i = BULK_INDEX(service->bulk_tx.remove);

			tx_size = service->bulk_tx.bulks[i].size;
		}

		rx_pending = service->bulk_rx.local_insert -
			service->bulk_rx.remote_insert;
		if (rx_pending) {
			unsigned int i = BULK_INDEX(service->bulk_rx.remove);

			rx_size = service->bulk_rx.bulks[i].size;
		}

		seq_printf(f, "  Bulk: tx_pending=%d (size %d), rx_pending=%d (size %d)\n",
			   tx_pending, tx_size, rx_pending, rx_size);

		if (VCHIQ_ENABLE_STATS) {
			seq_printf(f, "  Ctrl: tx_count=%d, tx_bytes=%llu, rx_count=%d, rx_bytes=%llu\n",
				   service->stats.ctrl_tx_count,
				   service->stats.ctrl_tx_bytes,
				   service->stats.ctrl_rx_count,
				   service->stats.ctrl_rx_bytes);

			seq_printf(f, "  Bulk: tx_count=%d, tx_bytes=%llu, rx_count=%d, rx_bytes=%llu\n",
				   service->stats.bulk_tx_count,
				   service->stats.bulk_tx_bytes,
				   service->stats.bulk_rx_count,
				   service->stats.bulk_rx_bytes);

			seq_printf(f, "  %d quota stalls, %d slot stalls, %d bulk stalls, %d aborted, %d errors\n",
				   service->stats.quota_stalls,
				   service->stats.slot_stalls,
				   service->stats.bulk_stalls,
				   service->stats.bulk_aborted_count,
				   service->stats.error_count);
		}
	}

	vchiq_dump_platform_service_state(f, service);
}

void vchiq_dump_state(struct seq_file *f, struct vchiq_state *state)
{
	int i;

	seq_printf(f, "State %d: %s\n", state->id,
		   conn_state_names[state->conn_state]);

	seq_printf(f, "  tx_pos=0x%x(@%pK), rx_pos=0x%x(@%pK)\n",
		   state->local->tx_pos,
		   state->tx_data + (state->local_tx_pos & VCHIQ_SLOT_MASK),
		   state->rx_pos,
		   state->rx_data + (state->rx_pos & VCHIQ_SLOT_MASK));

	seq_printf(f, "  Version: %d (min %d)\n", VCHIQ_VERSION,
		   VCHIQ_VERSION_MIN);

	if (VCHIQ_ENABLE_STATS) {
		seq_printf(f, "  Stats: ctrl_tx_count=%d, ctrl_rx_count=%d, error_count=%d\n",
			   state->stats.ctrl_tx_count, state->stats.ctrl_rx_count,
			   state->stats.error_count);
	}

	seq_printf(f, "  Slots: %d available (%d data), %d recyclable, %d stalls (%d data)\n",
		   ((state->slot_queue_available * VCHIQ_SLOT_SIZE) -
		   state->local_tx_pos) / VCHIQ_SLOT_SIZE,
		   state->data_quota - state->data_use_count,
		   state->local->slot_queue_recycle - state->slot_queue_available,
		   state->stats.slot_stalls, state->stats.data_stalls);

	vchiq_dump_platform_state(f);

	vchiq_dump_shared_state(f, state, state->local, "Local");

	vchiq_dump_shared_state(f, state, state->remote, "Remote");

	vchiq_dump_platform_instances(state, f);

	for (i = 0; i < state->unused_service; i++) {
		struct vchiq_service *service = find_service_by_port(state, i);

		if (service) {
			vchiq_dump_service_state(f, service);
			vchiq_service_put(service);
		}
	}
}

int vchiq_send_remote_use(struct vchiq_state *state)
{
	if (state->conn_state == VCHIQ_CONNSTATE_DISCONNECTED)
		return -ENOTCONN;

	return queue_message(state, NULL, MAKE_REMOTE_USE, NULL, NULL, 0, 0);
}

int vchiq_send_remote_use_active(struct vchiq_state *state)
{
	if (state->conn_state == VCHIQ_CONNSTATE_DISCONNECTED)
		return -ENOTCONN;

	return queue_message(state, NULL, MAKE_REMOTE_USE_ACTIVE,
			     NULL, NULL, 0, 0);
}

void vchiq_log_dump_mem(struct device *dev, const char *label, u32 addr,
			const void *void_mem, size_t num_bytes)
{
	const u8 *mem = void_mem;
	size_t offset;
	char line_buf[100];
	char *s;

	while (num_bytes > 0) {
		s = line_buf;

		for (offset = 0; offset < 16; offset++) {
			if (offset < num_bytes)
				s += scnprintf(s, 4, "%02x ", mem[offset]);
			else
				s += scnprintf(s, 4, "   ");
		}

		for (offset = 0; offset < 16; offset++) {
			if (offset < num_bytes) {
				u8 ch = mem[offset];

				if ((ch < ' ') || (ch > '~'))
					ch = '.';
				*s++ = (char)ch;
			}
		}
		*s++ = '\0';

		if (label && (*label != '\0'))
			dev_dbg(dev, "core: %s: %08x: %s\n", label, addr, line_buf);
		else
			dev_dbg(dev, "core: %s: %08x: %s\n", label, addr, line_buf);

		addr += 16;
		mem += 16;
		if (num_bytes > 16)
			num_bytes -= 16;
		else
			num_bytes = 0;
	}
}
