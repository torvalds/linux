// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/**
 * Copyright (c) 2010-2012 Broadcom. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2, as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "vchiq_core.h"

#define VCHIQ_SLOT_HANDLER_STACK 8192

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

#define BULK_INDEX(x) (x & (VCHIQ_NUM_SERVICE_BULKS - 1))

#define SRVTRACE_LEVEL(srv) \
	(((srv) && (srv)->trace) ? VCHIQ_LOG_TRACE : vchiq_core_msg_log_level)
#define SRVTRACE_ENABLED(srv, lev) \
	(((srv) && (srv)->trace) || (vchiq_core_msg_log_level >= (lev)))

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
	QMFLAGS_IS_BLOCKING     = (1 << 0),
	QMFLAGS_NO_MUTEX_LOCK   = (1 << 1),
	QMFLAGS_NO_MUTEX_UNLOCK = (1 << 2)
};

/* we require this for consistency between endpoints */
vchiq_static_assert(sizeof(struct vchiq_header) == 8);
vchiq_static_assert(IS_POW2(sizeof(struct vchiq_header)));
vchiq_static_assert(IS_POW2(VCHIQ_NUM_CURRENT_BULKS));
vchiq_static_assert(IS_POW2(VCHIQ_NUM_SERVICE_BULKS));
vchiq_static_assert(IS_POW2(VCHIQ_MAX_SERVICES));
vchiq_static_assert(VCHIQ_VERSION >= VCHIQ_VERSION_MIN);

/* Run time control of log level, based on KERN_XXX level. */
int vchiq_core_log_level = VCHIQ_LOG_DEFAULT;
int vchiq_core_msg_log_level = VCHIQ_LOG_DEFAULT;
int vchiq_sync_log_level = VCHIQ_LOG_DEFAULT;

static DEFINE_SPINLOCK(service_spinlock);
DEFINE_SPINLOCK(bulk_waiter_spinlock);
static DEFINE_SPINLOCK(quota_spinlock);

struct vchiq_state *vchiq_states[VCHIQ_MAX_STATES];
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
	case VCHIQ_MSG_PADDING:       return "PADDING";
	case VCHIQ_MSG_CONNECT:       return "CONNECT";
	case VCHIQ_MSG_OPEN:          return "OPEN";
	case VCHIQ_MSG_OPENACK:       return "OPENACK";
	case VCHIQ_MSG_CLOSE:         return "CLOSE";
	case VCHIQ_MSG_DATA:          return "DATA";
	case VCHIQ_MSG_BULK_RX:       return "BULK_RX";
	case VCHIQ_MSG_BULK_TX:       return "BULK_TX";
	case VCHIQ_MSG_BULK_RX_DONE:  return "BULK_RX_DONE";
	case VCHIQ_MSG_BULK_TX_DONE:  return "BULK_TX_DONE";
	case VCHIQ_MSG_PAUSE:         return "PAUSE";
	case VCHIQ_MSG_RESUME:        return "RESUME";
	case VCHIQ_MSG_REMOTE_USE:    return "REMOTE_USE";
	case VCHIQ_MSG_REMOTE_RELEASE:      return "REMOTE_RELEASE";
	case VCHIQ_MSG_REMOTE_USE_ACTIVE:   return "REMOTE_USE_ACTIVE";
	}
	return "???";
}

static inline void
vchiq_set_service_state(struct vchiq_service *service, int newstate)
{
	vchiq_log_info(vchiq_core_log_level, "%d: srv:%d %s->%s",
		service->state->id, service->localport,
		srvstate_names[service->srvstate],
		srvstate_names[newstate]);
	service->srvstate = newstate;
}

struct vchiq_service *
find_service_by_handle(VCHIQ_SERVICE_HANDLE_T handle)
{
	struct vchiq_service *service;

	spin_lock(&service_spinlock);
	service = handle_to_service(handle);
	if (service && (service->srvstate != VCHIQ_SRVSTATE_FREE) &&
		(service->handle == handle)) {
		WARN_ON(service->ref_count == 0);
		service->ref_count++;
	} else
		service = NULL;
	spin_unlock(&service_spinlock);

	if (!service)
		vchiq_log_info(vchiq_core_log_level,
			"Invalid service handle 0x%x", handle);

	return service;
}

struct vchiq_service *
find_service_by_port(struct vchiq_state *state, int localport)
{
	struct vchiq_service *service = NULL;

	if ((unsigned int)localport <= VCHIQ_PORT_MAX) {
		spin_lock(&service_spinlock);
		service = state->services[localport];
		if (service && (service->srvstate != VCHIQ_SRVSTATE_FREE)) {
			WARN_ON(service->ref_count == 0);
			service->ref_count++;
		} else
			service = NULL;
		spin_unlock(&service_spinlock);
	}

	if (!service)
		vchiq_log_info(vchiq_core_log_level,
			"Invalid port %d", localport);

	return service;
}

struct vchiq_service *
find_service_for_instance(VCHIQ_INSTANCE_T instance,
	VCHIQ_SERVICE_HANDLE_T handle)
{
	struct vchiq_service *service;

	spin_lock(&service_spinlock);
	service = handle_to_service(handle);
	if (service && (service->srvstate != VCHIQ_SRVSTATE_FREE) &&
		(service->handle == handle) &&
		(service->instance == instance)) {
		WARN_ON(service->ref_count == 0);
		service->ref_count++;
	} else
		service = NULL;
	spin_unlock(&service_spinlock);

	if (!service)
		vchiq_log_info(vchiq_core_log_level,
			"Invalid service handle 0x%x", handle);

	return service;
}

struct vchiq_service *
find_closed_service_for_instance(VCHIQ_INSTANCE_T instance,
	VCHIQ_SERVICE_HANDLE_T handle)
{
	struct vchiq_service *service;

	spin_lock(&service_spinlock);
	service = handle_to_service(handle);
	if (service &&
		((service->srvstate == VCHIQ_SRVSTATE_FREE) ||
		 (service->srvstate == VCHIQ_SRVSTATE_CLOSED)) &&
		(service->handle == handle) &&
		(service->instance == instance)) {
		WARN_ON(service->ref_count == 0);
		service->ref_count++;
	} else
		service = NULL;
	spin_unlock(&service_spinlock);

	if (!service)
		vchiq_log_info(vchiq_core_log_level,
			"Invalid service handle 0x%x", handle);

	return service;
}

struct vchiq_service *
next_service_by_instance(struct vchiq_state *state, VCHIQ_INSTANCE_T instance,
			 int *pidx)
{
	struct vchiq_service *service = NULL;
	int idx = *pidx;

	spin_lock(&service_spinlock);
	while (idx < state->unused_service) {
		struct vchiq_service *srv = state->services[idx++];

		if (srv && (srv->srvstate != VCHIQ_SRVSTATE_FREE) &&
			(srv->instance == instance)) {
			service = srv;
			WARN_ON(service->ref_count == 0);
			service->ref_count++;
			break;
		}
	}
	spin_unlock(&service_spinlock);

	*pidx = idx;

	return service;
}

void
lock_service(struct vchiq_service *service)
{
	spin_lock(&service_spinlock);
	WARN_ON(!service);
	if (service) {
		WARN_ON(service->ref_count == 0);
		service->ref_count++;
	}
	spin_unlock(&service_spinlock);
}

void
unlock_service(struct vchiq_service *service)
{
	spin_lock(&service_spinlock);
	if (!service) {
		WARN(1, "%s: service is NULL\n", __func__);
		goto unlock;
	}
	if (!service->ref_count) {
		WARN(1, "%s: ref_count is zero\n", __func__);
		goto unlock;
	}
	service->ref_count--;
	if (!service->ref_count) {
		struct vchiq_state *state = service->state;

		WARN_ON(service->srvstate != VCHIQ_SRVSTATE_FREE);
		state->services[service->localport] = NULL;
	} else {
		service = NULL;
	}
unlock:
	spin_unlock(&service_spinlock);

	if (service && service->userdata_term)
		service->userdata_term(service->base.userdata);

	kfree(service);
}

int
vchiq_get_client_id(VCHIQ_SERVICE_HANDLE_T handle)
{
	struct vchiq_service *service = find_service_by_handle(handle);
	int id;

	id = service ? service->client_id : 0;
	if (service)
		unlock_service(service);

	return id;
}

void *
vchiq_get_service_userdata(VCHIQ_SERVICE_HANDLE_T handle)
{
	struct vchiq_service *service = handle_to_service(handle);

	return service ? service->base.userdata : NULL;
}

int
vchiq_get_service_fourcc(VCHIQ_SERVICE_HANDLE_T handle)
{
	struct vchiq_service *service = handle_to_service(handle);

	return service ? service->base.fourcc : 0;
}

static void
mark_service_closing_internal(struct vchiq_service *service, int sh_thread)
{
	struct vchiq_state *state = service->state;
	struct vchiq_service_quota *service_quota;

	service->closing = 1;

	/* Synchronise with other threads. */
	mutex_lock(&state->recycle_mutex);
	mutex_unlock(&state->recycle_mutex);
	if (!sh_thread || (state->conn_state != VCHIQ_CONNSTATE_PAUSE_SENT)) {
		/* If we're pausing then the slot_mutex is held until resume
		 * by the slot handler.  Therefore don't try to acquire this
		 * mutex if we're the slot handler and in the pause sent state.
		 * We don't need to in this case anyway. */
		mutex_lock(&state->slot_mutex);
		mutex_unlock(&state->slot_mutex);
	}

	/* Unblock any sending thread. */
	service_quota = &state->service_quotas[service->localport];
	complete(&service_quota->quota_event);
}

static void
mark_service_closing(struct vchiq_service *service)
{
	mark_service_closing_internal(service, 0);
}

static inline VCHIQ_STATUS_T
make_service_callback(struct vchiq_service *service, VCHIQ_REASON_T reason,
		      struct vchiq_header *header, void *bulk_userdata)
{
	VCHIQ_STATUS_T status;

	vchiq_log_trace(vchiq_core_log_level, "%d: callback:%d (%s, %pK, %pK)",
		service->state->id, service->localport, reason_names[reason],
		header, bulk_userdata);
	status = service->base.callback(reason, header, service->handle,
		bulk_userdata);
	if (status == VCHIQ_ERROR) {
		vchiq_log_warning(vchiq_core_log_level,
			"%d: ignoring ERROR from callback to service %x",
			service->state->id, service->handle);
		status = VCHIQ_SUCCESS;
	}
	return status;
}

inline void
vchiq_set_conn_state(struct vchiq_state *state, VCHIQ_CONNSTATE_T newstate)
{
	VCHIQ_CONNSTATE_T oldstate = state->conn_state;

	vchiq_log_info(vchiq_core_log_level, "%d: %s->%s", state->id,
		conn_state_names[oldstate],
		conn_state_names[newstate]);
	state->conn_state = newstate;
	vchiq_platform_conn_state_changed(state, oldstate, newstate);
}

static inline void
remote_event_create(wait_queue_head_t *wq, struct remote_event *event)
{
	event->armed = 0;
	/* Don't clear the 'fired' flag because it may already have been set
	** by the other side. */
	init_waitqueue_head(wq);
}

static inline int
remote_event_wait(wait_queue_head_t *wq, struct remote_event *event)
{
	if (!event->fired) {
		event->armed = 1;
		dsb(sy);
		if (wait_event_killable(*wq, event->fired)) {
			event->armed = 0;
			return 0;
		}
		event->armed = 0;
		wmb();
	}

	event->fired = 0;
	return 1;
}

static inline void
remote_event_signal_local(wait_queue_head_t *wq, struct remote_event *event)
{
	event->fired = 1;
	event->armed = 0;
	wake_up_all(wq);
}

static inline void
remote_event_poll(wait_queue_head_t *wq, struct remote_event *event)
{
	if (event->fired && event->armed)
		remote_event_signal_local(wq, event);
}

void
remote_event_pollall(struct vchiq_state *state)
{
	remote_event_poll(&state->sync_trigger_event, &state->local->sync_trigger);
	remote_event_poll(&state->sync_release_event, &state->local->sync_release);
	remote_event_poll(&state->trigger_event, &state->local->trigger);
	remote_event_poll(&state->recycle_event, &state->local->recycle);
}

/* Round up message sizes so that any space at the end of a slot is always big
** enough for a header. This relies on header size being a power of two, which
** has been verified earlier by a static assertion. */

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

	for (i = 0; i < state->unused_service; i++) {
		struct vchiq_service *service = state->services[i];

		if (service &&
			(service->public_fourcc == fourcc) &&
			((service->srvstate == VCHIQ_SRVSTATE_LISTENING) ||
			((service->srvstate == VCHIQ_SRVSTATE_OPEN) &&
			(service->remoteport == VCHIQ_PORT_FREE)))) {
			lock_service(service);
			return service;
		}
	}

	return NULL;
}

/* Called by the slot handler thread */
static struct vchiq_service *
get_connected_service(struct vchiq_state *state, unsigned int port)
{
	int i;

	for (i = 0; i < state->unused_service; i++) {
		struct vchiq_service *service = state->services[i];

		if (service && (service->srvstate == VCHIQ_SRVSTATE_OPEN)
			&& (service->remoteport == port)) {
			lock_service(service);
			return service;
		}
	}
	return NULL;
}

inline void
request_poll(struct vchiq_state *state, struct vchiq_service *service,
	     int poll_type)
{
	u32 value;

	if (service) {
		do {
			value = atomic_read(&service->poll_flags);
		} while (atomic_cmpxchg(&service->poll_flags, value,
			value | (1 << poll_type)) != value);

		do {
			value = atomic_read(&state->poll_services[
				service->localport>>5]);
		} while (atomic_cmpxchg(
			&state->poll_services[service->localport>>5],
			value, value | (1 << (service->localport & 0x1f)))
			!= value);
	}

	state->poll_needed = 1;
	wmb();

	/* ... and ensure the slot handler runs. */
	remote_event_signal_local(&state->trigger_event, &state->local->trigger);
}

/* Called from queue_message, by the slot handler and application threads,
** with slot_mutex held */
static struct vchiq_header *
reserve_space(struct vchiq_state *state, size_t space, int is_blocking)
{
	struct vchiq_shared_state *local = state->local;
	int tx_pos = state->local_tx_pos;
	int slot_space = VCHIQ_SLOT_SIZE - (tx_pos & VCHIQ_SLOT_MASK);

	if (space > slot_space) {
		struct vchiq_header *header;
		/* Fill the remaining space with padding */
		WARN_ON(state->tx_data == NULL);
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
			remote_event_signal(&state->remote->trigger);

			if (!is_blocking ||
				(wait_for_completion_killable(
				&state->slot_available_event)))
				return NULL; /* No space available */
		}

		if (tx_pos == (state->slot_queue_available * VCHIQ_SLOT_SIZE)) {
			complete(&state->slot_available_event);
			pr_warn("%s: invalid tx_pos: %d\n", __func__, tx_pos);
			return NULL;
		}

		slot_index = local->slot_queue[
			SLOT_QUEUE_INDEX_FROM_POS(tx_pos) &
			VCHIQ_SLOT_QUEUE_MASK];
		state->tx_data =
			(char *)SLOT_DATA_FROM_INDEX(state, slot_index);
	}

	state->local_tx_pos = tx_pos + space;

	return (struct vchiq_header *)(state->tx_data +
						(tx_pos & VCHIQ_SLOT_MASK));
}

/* Called by the recycle thread. */
static void
process_free_queue(struct vchiq_state *state, BITSET_T *service_found,
		   size_t length)
{
	struct vchiq_shared_state *local = state->local;
	int slot_queue_available;

	/* Find slots which have been freed by the other side, and return them
	** to the available queue. */
	slot_queue_available = state->slot_queue_available;

	/*
	 * Use a memory barrier to ensure that any state that may have been
	 * modified by another thread is not masked by stale prefetched
	 * values.
	 */
	mb();

	while (slot_queue_available != local->slot_queue_recycle) {
		unsigned int pos;
		int slot_index = local->slot_queue[slot_queue_available++ &
			VCHIQ_SLOT_QUEUE_MASK];
		char *data = (char *)SLOT_DATA_FROM_INDEX(state, slot_index);
		int data_found = 0;

		/*
		 * Beware of the address dependency - data is calculated
		 * using an index written by the other side.
		 */
		rmb();

		vchiq_log_trace(vchiq_core_log_level, "%d: pfq %d=%pK %x %x",
			state->id, slot_index, data,
			local->slot_queue_recycle, slot_queue_available);

		/* Initialise the bitmask for services which have used this
		** slot */
		memset(service_found, 0, length);

		pos = 0;

		while (pos < VCHIQ_SLOT_SIZE) {
			struct vchiq_header *header =
				(struct vchiq_header *)(data + pos);
			int msgid = header->msgid;

			if (VCHIQ_MSG_TYPE(msgid) == VCHIQ_MSG_DATA) {
				int port = VCHIQ_MSG_SRCPORT(msgid);
				struct vchiq_service_quota *service_quota =
					&state->service_quotas[port];
				int count;

				spin_lock(&quota_spinlock);
				count = service_quota->message_use_count;
				if (count > 0)
					service_quota->message_use_count =
						count - 1;
				spin_unlock(&quota_spinlock);

				if (count == service_quota->message_quota)
					/* Signal the service that it
					** has dropped below its quota
					*/
					complete(&service_quota->quota_event);
				else if (count == 0) {
					vchiq_log_error(vchiq_core_log_level,
						"service %d message_use_count=%d (header %pK, msgid %x, header->msgid %x, header->size %x)",
						port,
						service_quota->message_use_count,
						header, msgid, header->msgid,
						header->size);
					WARN(1, "invalid message use count\n");
				}
				if (!BITSET_IS_SET(service_found, port)) {
					/* Set the found bit for this service */
					BITSET_SET(service_found, port);

					spin_lock(&quota_spinlock);
					count = service_quota->slot_use_count;
					if (count > 0)
						service_quota->slot_use_count =
							count - 1;
					spin_unlock(&quota_spinlock);

					if (count > 0) {
						/* Signal the service in case
						** it has dropped below its
						** quota */
						complete(&service_quota->quota_event);
						vchiq_log_trace(
							vchiq_core_log_level,
							"%d: pfq:%d %x@%pK - slot_use->%d",
							state->id, port,
							header->size, header,
							count - 1);
					} else {
						vchiq_log_error(
							vchiq_core_log_level,
								"service %d slot_use_count=%d (header %pK, msgid %x, header->msgid %x, header->size %x)",
							port, count, header,
							msgid, header->msgid,
							header->size);
						WARN(1, "bad slot use count\n");
					}
				}

				data_found = 1;
			}

			pos += calc_stride(header->size);
			if (pos > VCHIQ_SLOT_SIZE) {
				vchiq_log_error(vchiq_core_log_level,
					"pfq - pos %x: header %pK, msgid %x, header->msgid %x, header->size %x",
					pos, header, msgid, header->msgid,
					header->size);
				WARN(1, "invalid slot position\n");
			}
		}

		if (data_found) {
			int count;

			spin_lock(&quota_spinlock);
			count = state->data_use_count;
			if (count > 0)
				state->data_use_count =
					count - 1;
			spin_unlock(&quota_spinlock);
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
memcpy_copy_callback(
	void *context, void *dest,
	size_t offset, size_t maxsize)
{
	memcpy(dest + offset, context + offset, maxsize);
	return maxsize;
}

static ssize_t
copy_message_data(
	ssize_t (*copy_callback)(void *context, void *dest,
				 size_t offset, size_t maxsize),
	void *context,
	void *dest,
	size_t size)
{
	size_t pos = 0;

	while (pos < size) {
		ssize_t callback_result;
		size_t max_bytes = size - pos;

		callback_result =
			copy_callback(context, dest + pos,
				      pos, max_bytes);

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
static VCHIQ_STATUS_T
queue_message(struct vchiq_state *state, struct vchiq_service *service,
	      int msgid,
	      ssize_t (*copy_callback)(void *context, void *dest,
				       size_t offset, size_t maxsize),
	      void *context, size_t size, int flags)
{
	struct vchiq_shared_state *local;
	struct vchiq_service_quota *service_quota = NULL;
	struct vchiq_header *header;
	int type = VCHIQ_MSG_TYPE(msgid);

	size_t stride;

	local = state->local;

	stride = calc_stride(size);

	WARN_ON(!(stride <= VCHIQ_SLOT_SIZE));

	if (!(flags & QMFLAGS_NO_MUTEX_LOCK) &&
		(mutex_lock_killable(&state->slot_mutex) != 0))
		return VCHIQ_RETRY;

	if (type == VCHIQ_MSG_DATA) {
		int tx_end_index;

		if (!service) {
			WARN(1, "%s: service is NULL\n", __func__);
			mutex_unlock(&state->slot_mutex);
			return VCHIQ_ERROR;
		}

		WARN_ON((flags & (QMFLAGS_NO_MUTEX_LOCK |
				  QMFLAGS_NO_MUTEX_UNLOCK)) != 0);

		if (service->closing) {
			/* The service has been closed */
			mutex_unlock(&state->slot_mutex);
			return VCHIQ_ERROR;
		}

		service_quota = &state->service_quotas[service->localport];

		spin_lock(&quota_spinlock);

		/* Ensure this service doesn't use more than its quota of
		** messages or slots */
		tx_end_index = SLOT_QUEUE_INDEX_FROM_POS(
			state->local_tx_pos + stride - 1);

		/* Ensure data messages don't use more than their quota of
		** slots */
		while ((tx_end_index != state->previous_data_index) &&
			(state->data_use_count == state->data_quota)) {
			VCHIQ_STATS_INC(state, data_stalls);
			spin_unlock(&quota_spinlock);
			mutex_unlock(&state->slot_mutex);

			if (wait_for_completion_killable(
						&state->data_quota_event))
				return VCHIQ_RETRY;

			mutex_lock(&state->slot_mutex);
			spin_lock(&quota_spinlock);
			tx_end_index = SLOT_QUEUE_INDEX_FROM_POS(
				state->local_tx_pos + stride - 1);
			if ((tx_end_index == state->previous_data_index) ||
				(state->data_use_count < state->data_quota)) {
				/* Pass the signal on to other waiters */
				complete(&state->data_quota_event);
				break;
			}
		}

		while ((service_quota->message_use_count ==
				service_quota->message_quota) ||
			((tx_end_index != service_quota->previous_tx_index) &&
			(service_quota->slot_use_count ==
				service_quota->slot_quota))) {
			spin_unlock(&quota_spinlock);
			vchiq_log_trace(vchiq_core_log_level,
				"%d: qm:%d %s,%zx - quota stall "
				"(msg %d, slot %d)",
				state->id, service->localport,
				msg_type_str(type), size,
				service_quota->message_use_count,
				service_quota->slot_use_count);
			VCHIQ_SERVICE_STATS_INC(service, quota_stalls);
			mutex_unlock(&state->slot_mutex);
			if (wait_for_completion_killable(
						&service_quota->quota_event))
				return VCHIQ_RETRY;
			if (service->closing)
				return VCHIQ_ERROR;
			if (mutex_lock_killable(&state->slot_mutex) != 0)
				return VCHIQ_RETRY;
			if (service->srvstate != VCHIQ_SRVSTATE_OPEN) {
				/* The service has been closed */
				mutex_unlock(&state->slot_mutex);
				return VCHIQ_ERROR;
			}
			spin_lock(&quota_spinlock);
			tx_end_index = SLOT_QUEUE_INDEX_FROM_POS(
				state->local_tx_pos + stride - 1);
		}

		spin_unlock(&quota_spinlock);
	}

	header = reserve_space(state, stride, flags & QMFLAGS_IS_BLOCKING);

	if (!header) {
		if (service)
			VCHIQ_SERVICE_STATS_INC(service, slot_stalls);
		/* In the event of a failure, return the mutex to the
		   state it was in */
		if (!(flags & QMFLAGS_NO_MUTEX_LOCK))
			mutex_unlock(&state->slot_mutex);
		return VCHIQ_RETRY;
	}

	if (type == VCHIQ_MSG_DATA) {
		ssize_t callback_result;
		int tx_end_index;
		int slot_use_count;

		vchiq_log_info(vchiq_core_log_level,
			"%d: qm %s@%pK,%zx (%d->%d)",
			state->id, msg_type_str(VCHIQ_MSG_TYPE(msgid)),
			header, size, VCHIQ_MSG_SRCPORT(msgid),
			VCHIQ_MSG_DSTPORT(msgid));

		WARN_ON((flags & (QMFLAGS_NO_MUTEX_LOCK |
				  QMFLAGS_NO_MUTEX_UNLOCK)) != 0);

		callback_result =
			copy_message_data(copy_callback, context,
					  header->data, size);

		if (callback_result < 0) {
			mutex_unlock(&state->slot_mutex);
			VCHIQ_SERVICE_STATS_INC(service,
						error_count);
			return VCHIQ_ERROR;
		}

		if (SRVTRACE_ENABLED(service,
				     VCHIQ_LOG_INFO))
			vchiq_log_dump_mem("Sent", 0,
					   header->data,
					   min((size_t)16,
					       (size_t)callback_result));

		spin_lock(&quota_spinlock);
		service_quota->message_use_count++;

		tx_end_index =
			SLOT_QUEUE_INDEX_FROM_POS(state->local_tx_pos - 1);

		/* If this transmission can't fit in the last slot used by any
		** service, the data_use_count must be increased. */
		if (tx_end_index != state->previous_data_index) {
			state->previous_data_index = tx_end_index;
			state->data_use_count++;
		}

		/* If this isn't the same slot last used by this service,
		** the service's slot_use_count must be increased. */
		if (tx_end_index != service_quota->previous_tx_index) {
			service_quota->previous_tx_index = tx_end_index;
			slot_use_count = ++service_quota->slot_use_count;
		} else {
			slot_use_count = 0;
		}

		spin_unlock(&quota_spinlock);

		if (slot_use_count)
			vchiq_log_trace(vchiq_core_log_level,
				"%d: qm:%d %s,%zx - slot_use->%d (hdr %p)",
				state->id, service->localport,
				msg_type_str(VCHIQ_MSG_TYPE(msgid)), size,
				slot_use_count, header);

		VCHIQ_SERVICE_STATS_INC(service, ctrl_tx_count);
		VCHIQ_SERVICE_STATS_ADD(service, ctrl_tx_bytes, size);
	} else {
		vchiq_log_info(vchiq_core_log_level,
			"%d: qm %s@%pK,%zx (%d->%d)", state->id,
			msg_type_str(VCHIQ_MSG_TYPE(msgid)),
			header, size, VCHIQ_MSG_SRCPORT(msgid),
			VCHIQ_MSG_DSTPORT(msgid));
		if (size != 0) {
			/* It is assumed for now that this code path
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

	{
		int svc_fourcc;

		svc_fourcc = service
			? service->base.fourcc
			: VCHIQ_MAKE_FOURCC('?', '?', '?', '?');

		vchiq_log_info(SRVTRACE_LEVEL(service),
			"Sent Msg %s(%u) to %c%c%c%c s:%u d:%d len:%zu",
			msg_type_str(VCHIQ_MSG_TYPE(msgid)),
			VCHIQ_MSG_TYPE(msgid),
			VCHIQ_FOURCC_AS_4CHARS(svc_fourcc),
			VCHIQ_MSG_SRCPORT(msgid),
			VCHIQ_MSG_DSTPORT(msgid),
			size);
	}

	/* Make sure the new header is visible to the peer. */
	wmb();

	/* Make the new tx_pos visible to the peer. */
	local->tx_pos = state->local_tx_pos;
	wmb();

	if (service && (type == VCHIQ_MSG_CLOSE))
		vchiq_set_service_state(service, VCHIQ_SRVSTATE_CLOSESENT);

	if (!(flags & QMFLAGS_NO_MUTEX_UNLOCK))
		mutex_unlock(&state->slot_mutex);

	remote_event_signal(&state->remote->trigger);

	return VCHIQ_SUCCESS;
}

/* Called by the slot handler and application threads */
static VCHIQ_STATUS_T
queue_message_sync(struct vchiq_state *state, struct vchiq_service *service,
		   int msgid,
		   ssize_t (*copy_callback)(void *context, void *dest,
					    size_t offset, size_t maxsize),
		   void *context, int size, int is_blocking)
{
	struct vchiq_shared_state *local;
	struct vchiq_header *header;
	ssize_t callback_result;

	local = state->local;

	if ((VCHIQ_MSG_TYPE(msgid) != VCHIQ_MSG_RESUME) &&
		(mutex_lock_killable(&state->sync_mutex) != 0))
		return VCHIQ_RETRY;

	remote_event_wait(&state->sync_release_event, &local->sync_release);

	rmb();

	header = (struct vchiq_header *)SLOT_DATA_FROM_INDEX(state,
		local->slot_sync);

	{
		int oldmsgid = header->msgid;

		if (oldmsgid != VCHIQ_MSGID_PADDING)
			vchiq_log_error(vchiq_core_log_level,
				"%d: qms - msgid %x, not PADDING",
				state->id, oldmsgid);
	}

	vchiq_log_info(vchiq_sync_log_level,
		       "%d: qms %s@%pK,%x (%d->%d)", state->id,
		       msg_type_str(VCHIQ_MSG_TYPE(msgid)),
		       header, size, VCHIQ_MSG_SRCPORT(msgid),
		       VCHIQ_MSG_DSTPORT(msgid));

	callback_result =
		copy_message_data(copy_callback, context,
				  header->data, size);

	if (callback_result < 0) {
		mutex_unlock(&state->slot_mutex);
		VCHIQ_SERVICE_STATS_INC(service,
					error_count);
		return VCHIQ_ERROR;
	}

	if (service) {
		if (SRVTRACE_ENABLED(service,
				     VCHIQ_LOG_INFO))
			vchiq_log_dump_mem("Sent", 0,
					   header->data,
					   min((size_t)16,
					       (size_t)callback_result));

		VCHIQ_SERVICE_STATS_INC(service, ctrl_tx_count);
		VCHIQ_SERVICE_STATS_ADD(service, ctrl_tx_bytes, size);
	} else {
		VCHIQ_STATS_INC(state, ctrl_tx_count);
	}

	header->size = size;
	header->msgid = msgid;

	if (vchiq_sync_log_level >= VCHIQ_LOG_TRACE) {
		int svc_fourcc;

		svc_fourcc = service
			? service->base.fourcc
			: VCHIQ_MAKE_FOURCC('?', '?', '?', '?');

		vchiq_log_trace(vchiq_sync_log_level,
			"Sent Sync Msg %s(%u) to %c%c%c%c s:%u d:%d len:%d",
			msg_type_str(VCHIQ_MSG_TYPE(msgid)),
			VCHIQ_MSG_TYPE(msgid),
			VCHIQ_FOURCC_AS_4CHARS(svc_fourcc),
			VCHIQ_MSG_SRCPORT(msgid),
			VCHIQ_MSG_DSTPORT(msgid),
			size);
	}

	remote_event_signal(&state->remote->sync_trigger);

	if (VCHIQ_MSG_TYPE(msgid) != VCHIQ_MSG_PAUSE)
		mutex_unlock(&state->sync_mutex);

	return VCHIQ_SUCCESS;
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
	int release_count;

	mutex_lock(&state->recycle_mutex);

	if (header) {
		int msgid = header->msgid;

		if (((msgid & VCHIQ_MSGID_CLAIMED) == 0) ||
			(service && service->closing)) {
			mutex_unlock(&state->recycle_mutex);
			return;
		}

		/* Rewrite the message header to prevent a double
		** release */
		header->msgid = msgid & ~VCHIQ_MSGID_CLAIMED;
	}

	release_count = slot_info->release_count;
	slot_info->release_count = ++release_count;

	if (release_count == slot_info->use_count) {
		int slot_queue_recycle;
		/* Add to the freed queue */

		/* A read barrier is necessary here to prevent speculative
		** fetches of remote->slot_queue_recycle from overtaking the
		** mutex. */
		rmb();

		slot_queue_recycle = state->remote->slot_queue_recycle;
		state->remote->slot_queue[slot_queue_recycle &
			VCHIQ_SLOT_QUEUE_MASK] =
			SLOT_INDEX_FROM_INFO(state, slot_info);
		state->remote->slot_queue_recycle = slot_queue_recycle + 1;
		vchiq_log_info(vchiq_core_log_level,
			"%d: %s %d - recycle->%x", state->id, __func__,
			SLOT_INDEX_FROM_INFO(state, slot_info),
			state->remote->slot_queue_recycle);

		/* A write barrier is necessary, but remote_event_signal
		** contains one. */
		remote_event_signal(&state->remote->recycle);
	}

	mutex_unlock(&state->recycle_mutex);
}

/* Called by the slot handler - don't hold the bulk mutex */
static VCHIQ_STATUS_T
notify_bulks(struct vchiq_service *service, struct vchiq_bulk_queue *queue,
	     int retry_poll)
{
	VCHIQ_STATUS_T status = VCHIQ_SUCCESS;

	vchiq_log_trace(vchiq_core_log_level,
		"%d: nb:%d %cx - p=%x rn=%x r=%x",
		service->state->id, service->localport,
		(queue == &service->bulk_tx) ? 't' : 'r',
		queue->process, queue->remote_notify, queue->remove);

	queue->remote_notify = queue->process;

	if (status == VCHIQ_SUCCESS) {
		while (queue->remove != queue->remote_notify) {
			struct vchiq_bulk *bulk =
				&queue->bulks[BULK_INDEX(queue->remove)];

			/* Only generate callbacks for non-dummy bulk
			** requests, and non-terminated services */
			if (bulk->data && service->instance) {
				if (bulk->actual != VCHIQ_BULK_ACTUAL_ABORTED) {
					if (bulk->dir == VCHIQ_BULK_TRANSMIT) {
						VCHIQ_SERVICE_STATS_INC(service,
							bulk_tx_count);
						VCHIQ_SERVICE_STATS_ADD(service,
							bulk_tx_bytes,
							bulk->actual);
					} else {
						VCHIQ_SERVICE_STATS_INC(service,
							bulk_rx_count);
						VCHIQ_SERVICE_STATS_ADD(service,
							bulk_rx_bytes,
							bulk->actual);
					}
				} else {
					VCHIQ_SERVICE_STATS_INC(service,
						bulk_aborted_count);
				}
				if (bulk->mode == VCHIQ_BULK_MODE_BLOCKING) {
					struct bulk_waiter *waiter;

					spin_lock(&bulk_waiter_spinlock);
					waiter = bulk->userdata;
					if (waiter) {
						waiter->actual = bulk->actual;
						complete(&waiter->event);
					}
					spin_unlock(&bulk_waiter_spinlock);
				} else if (bulk->mode ==
					VCHIQ_BULK_MODE_CALLBACK) {
					VCHIQ_REASON_T reason = (bulk->dir ==
						VCHIQ_BULK_TRANSMIT) ?
						((bulk->actual ==
						VCHIQ_BULK_ACTUAL_ABORTED) ?
						VCHIQ_BULK_TRANSMIT_ABORTED :
						VCHIQ_BULK_TRANSMIT_DONE) :
						((bulk->actual ==
						VCHIQ_BULK_ACTUAL_ABORTED) ?
						VCHIQ_BULK_RECEIVE_ABORTED :
						VCHIQ_BULK_RECEIVE_DONE);
					status = make_service_callback(service,
						reason,	NULL, bulk->userdata);
					if (status == VCHIQ_RETRY)
						break;
				}
			}

			queue->remove++;
			complete(&service->bulk_remove_event);
		}
		if (!retry_poll)
			status = VCHIQ_SUCCESS;
	}

	if (status == VCHIQ_RETRY)
		request_poll(service->state, service,
			(queue == &service->bulk_tx) ?
			VCHIQ_POLL_TXNOTIFY : VCHIQ_POLL_RXNOTIFY);

	return status;
}

/* Called by the slot handler thread */
static void
poll_services(struct vchiq_state *state)
{
	int group, i;

	for (group = 0; group < BITSET_SIZE(state->unused_service); group++) {
		u32 flags;

		flags = atomic_xchg(&state->poll_services[group], 0);
		for (i = 0; flags; i++) {
			if (flags & (1 << i)) {
				struct vchiq_service *service =
					find_service_by_port(state,
						(group<<5) + i);
				u32 service_flags;

				flags &= ~(1 << i);
				if (!service)
					continue;
				service_flags =
					atomic_xchg(&service->poll_flags, 0);
				if (service_flags &
					(1 << VCHIQ_POLL_REMOVE)) {
					vchiq_log_info(vchiq_core_log_level,
						"%d: ps - remove %d<->%d",
						state->id, service->localport,
						service->remoteport);

					/* Make it look like a client, because
					   it must be removed and not left in
					   the LISTENING state. */
					service->public_fourcc =
						VCHIQ_FOURCC_INVALID;

					if (vchiq_close_service_internal(
						service, 0/*!close_recvd*/) !=
						VCHIQ_SUCCESS)
						request_poll(state, service,
							VCHIQ_POLL_REMOVE);
				} else if (service_flags &
					(1 << VCHIQ_POLL_TERMINATE)) {
					vchiq_log_info(vchiq_core_log_level,
						"%d: ps - terminate %d<->%d",
						state->id, service->localport,
						service->remoteport);
					if (vchiq_close_service_internal(
						service, 0/*!close_recvd*/) !=
						VCHIQ_SUCCESS)
						request_poll(state, service,
							VCHIQ_POLL_TERMINATE);
				}
				if (service_flags & (1 << VCHIQ_POLL_TXNOTIFY))
					notify_bulks(service,
						&service->bulk_tx,
						1/*retry_poll*/);
				if (service_flags & (1 << VCHIQ_POLL_RXNOTIFY))
					notify_bulks(service,
						&service->bulk_rx,
						1/*retry_poll*/);
				unlock_service(service);
			}
		}
	}
}

/* Called with the bulk_mutex held */
static void
abort_outstanding_bulks(struct vchiq_service *service,
			struct vchiq_bulk_queue *queue)
{
	int is_tx = (queue == &service->bulk_tx);

	vchiq_log_trace(vchiq_core_log_level,
		"%d: aob:%d %cx - li=%x ri=%x p=%x",
		service->state->id, service->localport, is_tx ? 't' : 'r',
		queue->local_insert, queue->remote_insert, queue->process);

	WARN_ON(!((int)(queue->local_insert - queue->process) >= 0));
	WARN_ON(!((int)(queue->remote_insert - queue->process) >= 0));

	while ((queue->process != queue->local_insert) ||
		(queue->process != queue->remote_insert)) {
		struct vchiq_bulk *bulk =
				&queue->bulks[BULK_INDEX(queue->process)];

		if (queue->process == queue->remote_insert) {
			/* fabricate a matching dummy bulk */
			bulk->remote_data = NULL;
			bulk->remote_size = 0;
			queue->remote_insert++;
		}

		if (queue->process != queue->local_insert) {
			vchiq_complete_bulk(bulk);

			vchiq_log_info(SRVTRACE_LEVEL(service),
				"%s %c%c%c%c d:%d ABORTED - tx len:%d, "
				"rx len:%d",
				is_tx ? "Send Bulk to" : "Recv Bulk from",
				VCHIQ_FOURCC_AS_4CHARS(service->base.fourcc),
				service->remoteport,
				bulk->size,
				bulk->remote_size);
		} else {
			/* fabricate a matching dummy bulk */
			bulk->data = NULL;
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
	struct vchiq_service *service = NULL;
	int msgid, size;
	unsigned int localport, remoteport;

	msgid = header->msgid;
	size = header->size;
	localport = VCHIQ_MSG_DSTPORT(msgid);
	remoteport = VCHIQ_MSG_SRCPORT(msgid);
	if (size >= sizeof(struct vchiq_open_payload)) {
		const struct vchiq_open_payload *payload =
			(struct vchiq_open_payload *)header->data;
		unsigned int fourcc;

		fourcc = payload->fourcc;
		vchiq_log_info(vchiq_core_log_level,
			"%d: prs OPEN@%pK (%d->'%c%c%c%c')",
			state->id, header, localport,
			VCHIQ_FOURCC_AS_4CHARS(fourcc));

		service = get_listening_service(state, fourcc);

		if (service) {
			/* A matching service exists */
			short version = payload->version;
			short version_min = payload->version_min;

			if ((service->version < version_min) ||
				(version < service->version_min)) {
				/* Version mismatch */
				vchiq_loud_error_header();
				vchiq_loud_error("%d: service %d (%c%c%c%c) "
					"version mismatch - local (%d, min %d)"
					" vs. remote (%d, min %d)",
					state->id, service->localport,
					VCHIQ_FOURCC_AS_4CHARS(fourcc),
					service->version, service->version_min,
					version, version_min);
				vchiq_loud_error_footer();
				unlock_service(service);
				service = NULL;
				goto fail_open;
			}
			service->peer_version = version;

			if (service->srvstate == VCHIQ_SRVSTATE_LISTENING) {
				struct vchiq_openack_payload ack_payload = {
					service->version
				};

				if (state->version_common <
				    VCHIQ_VERSION_SYNCHRONOUS_MODE)
					service->sync = 0;

				/* Acknowledge the OPEN */
				if (service->sync) {
					if (queue_message_sync(
						state,
						NULL,
						VCHIQ_MAKE_MSG(
							VCHIQ_MSG_OPENACK,
							service->localport,
							remoteport),
						memcpy_copy_callback,
						&ack_payload,
						sizeof(ack_payload),
						0) == VCHIQ_RETRY)
						goto bail_not_ready;
				} else {
					if (queue_message(state,
							NULL,
							VCHIQ_MAKE_MSG(
							VCHIQ_MSG_OPENACK,
							service->localport,
							remoteport),
						memcpy_copy_callback,
						&ack_payload,
						sizeof(ack_payload),
						0) == VCHIQ_RETRY)
						goto bail_not_ready;
				}

				/* The service is now open */
				vchiq_set_service_state(service,
					service->sync ? VCHIQ_SRVSTATE_OPENSYNC
					: VCHIQ_SRVSTATE_OPEN);
			}

			service->remoteport = remoteport;
			service->client_id = ((int *)header->data)[1];
			if (make_service_callback(service, VCHIQ_SERVICE_OPENED,
				NULL, NULL) == VCHIQ_RETRY) {
				/* Bail out if not ready */
				service->remoteport = VCHIQ_PORT_FREE;
				goto bail_not_ready;
			}

			/* Success - the message has been dealt with */
			unlock_service(service);
			return 1;
		}
	}

fail_open:
	/* No available service, or an invalid request - send a CLOSE */
	if (queue_message(state, NULL,
		VCHIQ_MAKE_MSG(VCHIQ_MSG_CLOSE, 0, VCHIQ_MSG_SRCPORT(msgid)),
		NULL, NULL, 0, 0) == VCHIQ_RETRY)
		goto bail_not_ready;

	return 1;

bail_not_ready:
	if (service)
		unlock_service(service);

	return 0;
}

/* Called by the slot handler thread */
static void
parse_rx_slots(struct vchiq_state *state)
{
	struct vchiq_shared_state *remote = state->remote;
	struct vchiq_service *service = NULL;
	int tx_pos;

	DEBUG_INITIALISE(state->local)

	tx_pos = remote->tx_pos;

	while (state->rx_pos != tx_pos) {
		struct vchiq_header *header;
		int msgid, size;
		int type;
		unsigned int localport, remoteport;

		DEBUG_TRACE(PARSE_LINE);
		if (!state->rx_data) {
			int rx_index;

			WARN_ON(!((state->rx_pos & VCHIQ_SLOT_MASK) == 0));
			rx_index = remote->slot_queue[
				SLOT_QUEUE_INDEX_FROM_POS(state->rx_pos) &
				VCHIQ_SLOT_QUEUE_MASK];
			state->rx_data = (char *)SLOT_DATA_FROM_INDEX(state,
				rx_index);
			state->rx_info = SLOT_INFO_FROM_INDEX(state, rx_index);

			/* Initialise use_count to one, and increment
			** release_count at the end of the slot to avoid
			** releasing the slot prematurely. */
			state->rx_info->use_count = 1;
			state->rx_info->release_count = 0;
		}

		header = (struct vchiq_header *)(state->rx_data +
			(state->rx_pos & VCHIQ_SLOT_MASK));
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
				/* This could be a CLOSE from a client which
				   hadn't yet received the OPENACK - look for
				   the connected service */
				if (service)
					unlock_service(service);
				service = get_connected_service(state,
					remoteport);
				if (service)
					vchiq_log_warning(vchiq_core_log_level,
						"%d: prs %s@%pK (%d->%d) - found connected service %d",
						state->id, msg_type_str(type),
						header, remoteport, localport,
						service->localport);
			}

			if (!service) {
				vchiq_log_error(vchiq_core_log_level,
					"%d: prs %s@%pK (%d->%d) - invalid/closed service %d",
					state->id, msg_type_str(type),
					header, remoteport, localport,
					localport);
				goto skip_message;
			}
			break;
		default:
			break;
		}

		if (SRVTRACE_ENABLED(service, VCHIQ_LOG_INFO)) {
			int svc_fourcc;

			svc_fourcc = service
				? service->base.fourcc
				: VCHIQ_MAKE_FOURCC('?', '?', '?', '?');
			vchiq_log_info(SRVTRACE_LEVEL(service),
				"Rcvd Msg %s(%u) from %c%c%c%c s:%d d:%d "
				"len:%d",
				msg_type_str(type), type,
				VCHIQ_FOURCC_AS_4CHARS(svc_fourcc),
				remoteport, localport, size);
			if (size > 0)
				vchiq_log_dump_mem("Rcvd", 0, header->data,
					min(16, size));
		}

		if (((unsigned long)header & VCHIQ_SLOT_MASK) +
		    calc_stride(size) > VCHIQ_SLOT_SIZE) {
			vchiq_log_error(vchiq_core_log_level,
				"header %pK (msgid %x) - size %x too big for slot",
				header, (unsigned int)msgid,
				(unsigned int)size);
			WARN(1, "oversized for slot\n");
		}

		switch (type) {
		case VCHIQ_MSG_OPEN:
			WARN_ON(!(VCHIQ_MSG_DSTPORT(msgid) == 0));
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
			vchiq_log_info(vchiq_core_log_level,
				"%d: prs OPENACK@%pK,%x (%d->%d) v:%d",
				state->id, header, size, remoteport, localport,
				service->peer_version);
			if (service->srvstate ==
				VCHIQ_SRVSTATE_OPENING) {
				service->remoteport = remoteport;
				vchiq_set_service_state(service,
					VCHIQ_SRVSTATE_OPEN);
				complete(&service->remove_event);
			} else
				vchiq_log_error(vchiq_core_log_level,
					"OPENACK received in state %s",
					srvstate_names[service->srvstate]);
			break;
		case VCHIQ_MSG_CLOSE:
			WARN_ON(size != 0); /* There should be no data */

			vchiq_log_info(vchiq_core_log_level,
				"%d: prs CLOSE@%pK (%d->%d)",
				state->id, header, remoteport, localport);

			mark_service_closing_internal(service, 1);

			if (vchiq_close_service_internal(service,
				1/*close_recvd*/) == VCHIQ_RETRY)
				goto bail_not_ready;

			vchiq_log_info(vchiq_core_log_level,
				"Close Service %c%c%c%c s:%u d:%d",
				VCHIQ_FOURCC_AS_4CHARS(service->base.fourcc),
				service->localport,
				service->remoteport);
			break;
		case VCHIQ_MSG_DATA:
			vchiq_log_info(vchiq_core_log_level,
				"%d: prs DATA@%pK,%x (%d->%d)",
				state->id, header, size, remoteport, localport);

			if ((service->remoteport == remoteport)
				&& (service->srvstate ==
				VCHIQ_SRVSTATE_OPEN)) {
				header->msgid = msgid | VCHIQ_MSGID_CLAIMED;
				claim_slot(state->rx_info);
				DEBUG_TRACE(PARSE_LINE);
				if (make_service_callback(service,
					VCHIQ_MESSAGE_AVAILABLE, header,
					NULL) == VCHIQ_RETRY) {
					DEBUG_TRACE(PARSE_LINE);
					goto bail_not_ready;
				}
				VCHIQ_SERVICE_STATS_INC(service, ctrl_rx_count);
				VCHIQ_SERVICE_STATS_ADD(service, ctrl_rx_bytes,
					size);
			} else {
				VCHIQ_STATS_INC(state, error_count);
			}
			break;
		case VCHIQ_MSG_CONNECT:
			vchiq_log_info(vchiq_core_log_level,
				"%d: prs CONNECT@%pK", state->id, header);
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
			if ((service->remoteport == remoteport)
				&& (service->srvstate !=
				VCHIQ_SRVSTATE_FREE)) {
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
					vchiq_log_error(vchiq_core_log_level,
						"%d: prs %s@%pK (%d->%d) "
						"unexpected (ri=%d,li=%d)",
						state->id, msg_type_str(type),
						header, remoteport, localport,
						queue->remote_insert,
						queue->local_insert);
					mutex_unlock(&service->bulk_mutex);
					break;
				}
				if (queue->process != queue->remote_insert) {
					pr_err("%s: p %x != ri %x\n",
					       __func__,
					       queue->process,
					       queue->remote_insert);
					mutex_unlock(&service->bulk_mutex);
					goto bail_not_ready;
				}

				bulk = &queue->bulks[
					BULK_INDEX(queue->remote_insert)];
				bulk->actual = *(int *)header->data;
				queue->remote_insert++;

				vchiq_log_info(vchiq_core_log_level,
					"%d: prs %s@%pK (%d->%d) %x@%pK",
					state->id, msg_type_str(type),
					header, remoteport, localport,
					bulk->actual, bulk->data);

				vchiq_log_trace(vchiq_core_log_level,
					"%d: prs:%d %cx li=%x ri=%x p=%x",
					state->id, localport,
					(type == VCHIQ_MSG_BULK_RX_DONE) ?
						'r' : 't',
					queue->local_insert,
					queue->remote_insert, queue->process);

				DEBUG_TRACE(PARSE_LINE);
				WARN_ON(queue->process == queue->local_insert);
				vchiq_complete_bulk(bulk);
				queue->process++;
				mutex_unlock(&service->bulk_mutex);
				DEBUG_TRACE(PARSE_LINE);
				notify_bulks(service, queue, 1/*retry_poll*/);
				DEBUG_TRACE(PARSE_LINE);
			}
			break;
		case VCHIQ_MSG_PADDING:
			vchiq_log_trace(vchiq_core_log_level,
				"%d: prs PADDING@%pK,%x",
				state->id, header, size);
			break;
		case VCHIQ_MSG_PAUSE:
			/* If initiated, signal the application thread */
			vchiq_log_trace(vchiq_core_log_level,
				"%d: prs PAUSE@%pK,%x",
				state->id, header, size);
			if (state->conn_state == VCHIQ_CONNSTATE_PAUSED) {
				vchiq_log_error(vchiq_core_log_level,
					"%d: PAUSE received in state PAUSED",
					state->id);
				break;
			}
			if (state->conn_state != VCHIQ_CONNSTATE_PAUSE_SENT) {
				/* Send a PAUSE in response */
				if (queue_message(state, NULL,
					VCHIQ_MAKE_MSG(VCHIQ_MSG_PAUSE, 0, 0),
					NULL, NULL, 0, QMFLAGS_NO_MUTEX_UNLOCK)
				    == VCHIQ_RETRY)
					goto bail_not_ready;
			}
			/* At this point slot_mutex is held */
			vchiq_set_conn_state(state, VCHIQ_CONNSTATE_PAUSED);
			vchiq_platform_paused(state);
			break;
		case VCHIQ_MSG_RESUME:
			vchiq_log_trace(vchiq_core_log_level,
				"%d: prs RESUME@%pK,%x",
				state->id, header, size);
			/* Release the slot mutex */
			mutex_unlock(&state->slot_mutex);
			vchiq_set_conn_state(state, VCHIQ_CONNSTATE_CONNECTED);
			vchiq_platform_resumed(state);
			break;

		case VCHIQ_MSG_REMOTE_USE:
			vchiq_on_remote_use(state);
			break;
		case VCHIQ_MSG_REMOTE_RELEASE:
			vchiq_on_remote_release(state);
			break;
		case VCHIQ_MSG_REMOTE_USE_ACTIVE:
			vchiq_on_remote_use_active(state);
			break;

		default:
			vchiq_log_error(vchiq_core_log_level,
				"%d: prs invalid msgid %x@%pK,%x",
				state->id, msgid, header, size);
			WARN(1, "invalid message\n");
			break;
		}

skip_message:
		if (service) {
			unlock_service(service);
			service = NULL;
		}

		state->rx_pos += calc_stride(size);

		DEBUG_TRACE(PARSE_LINE);
		/* Perform some housekeeping when the end of the slot is
		** reached. */
		if ((state->rx_pos & VCHIQ_SLOT_MASK) == 0) {
			/* Remove the extra reference count. */
			release_slot(state, state->rx_info, NULL, NULL);
			state->rx_data = NULL;
		}
	}

bail_not_ready:
	if (service)
		unlock_service(service);
}

/* Called by the slot handler thread */
static int
slot_handler_func(void *v)
{
	struct vchiq_state *state = v;
	struct vchiq_shared_state *local = state->local;

	DEBUG_INITIALISE(local)

	while (1) {
		DEBUG_COUNT(SLOT_HANDLER_COUNT);
		DEBUG_TRACE(SLOT_HANDLER_LINE);
		remote_event_wait(&state->trigger_event, &local->trigger);

		rmb();

		DEBUG_TRACE(SLOT_HANDLER_LINE);
		if (state->poll_needed) {
			/* Check if we need to suspend - may change our
			 * conn_state */
			vchiq_platform_check_suspend(state);

			state->poll_needed = 0;

			/* Handle service polling and other rare conditions here
			** out of the mainline code */
			switch (state->conn_state) {
			case VCHIQ_CONNSTATE_CONNECTED:
				/* Poll the services as requested */
				poll_services(state);
				break;

			case VCHIQ_CONNSTATE_PAUSING:
				if (queue_message(state, NULL,
					VCHIQ_MAKE_MSG(VCHIQ_MSG_PAUSE, 0, 0),
					NULL, NULL, 0,
					QMFLAGS_NO_MUTEX_UNLOCK)
				    != VCHIQ_RETRY) {
					vchiq_set_conn_state(state,
						VCHIQ_CONNSTATE_PAUSE_SENT);
				} else {
					/* Retry later */
					state->poll_needed = 1;
				}
				break;

			case VCHIQ_CONNSTATE_PAUSED:
				vchiq_platform_resume(state);
				break;

			case VCHIQ_CONNSTATE_RESUMING:
				if (queue_message(state, NULL,
					VCHIQ_MAKE_MSG(VCHIQ_MSG_RESUME, 0, 0),
					NULL, NULL, 0, QMFLAGS_NO_MUTEX_LOCK)
					!= VCHIQ_RETRY) {
					vchiq_set_conn_state(state,
						VCHIQ_CONNSTATE_CONNECTED);
					vchiq_platform_resumed(state);
				} else {
					/* This should really be impossible,
					** since the PAUSE should have flushed
					** through outstanding messages. */
					vchiq_log_error(vchiq_core_log_level,
						"Failed to send RESUME "
						"message");
				}
				break;

			case VCHIQ_CONNSTATE_PAUSE_TIMEOUT:
			case VCHIQ_CONNSTATE_RESUME_TIMEOUT:
				vchiq_platform_handle_timeout(state);
				break;
			default:
				break;
			}

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
	BITSET_T *found;
	size_t length;

	length = sizeof(*found) * BITSET_SIZE(VCHIQ_MAX_SERVICES);

	found = kmalloc_array(BITSET_SIZE(VCHIQ_MAX_SERVICES), sizeof(*found),
			      GFP_KERNEL);
	if (!found)
		return -ENOMEM;

	while (1) {
		remote_event_wait(&state->recycle_event, &local->recycle);

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

	while (1) {
		struct vchiq_service *service;
		int msgid, size;
		int type;
		unsigned int localport, remoteport;

		remote_event_wait(&state->sync_trigger_event, &local->sync_trigger);

		rmb();

		msgid = header->msgid;
		size = header->size;
		type = VCHIQ_MSG_TYPE(msgid);
		localport = VCHIQ_MSG_DSTPORT(msgid);
		remoteport = VCHIQ_MSG_SRCPORT(msgid);

		service = find_service_by_port(state, localport);

		if (!service) {
			vchiq_log_error(vchiq_sync_log_level,
				"%d: sf %s@%pK (%d->%d) - invalid/closed service %d",
				state->id, msg_type_str(type),
				header, remoteport, localport, localport);
			release_message_sync(state, header);
			continue;
		}

		if (vchiq_sync_log_level >= VCHIQ_LOG_TRACE) {
			int svc_fourcc;

			svc_fourcc = service
				? service->base.fourcc
				: VCHIQ_MAKE_FOURCC('?', '?', '?', '?');
			vchiq_log_trace(vchiq_sync_log_level,
				"Rcvd Msg %s from %c%c%c%c s:%d d:%d len:%d",
				msg_type_str(type),
				VCHIQ_FOURCC_AS_4CHARS(svc_fourcc),
				remoteport, localport, size);
			if (size > 0)
				vchiq_log_dump_mem("Rcvd", 0, header->data,
					min(16, size));
		}

		switch (type) {
		case VCHIQ_MSG_OPENACK:
			if (size >= sizeof(struct vchiq_openack_payload)) {
				const struct vchiq_openack_payload *payload =
					(struct vchiq_openack_payload *)
					header->data;
				service->peer_version = payload->version;
			}
			vchiq_log_info(vchiq_sync_log_level,
				"%d: sf OPENACK@%pK,%x (%d->%d) v:%d",
				state->id, header, size, remoteport, localport,
				service->peer_version);
			if (service->srvstate == VCHIQ_SRVSTATE_OPENING) {
				service->remoteport = remoteport;
				vchiq_set_service_state(service,
					VCHIQ_SRVSTATE_OPENSYNC);
				service->sync = 1;
				complete(&service->remove_event);
			}
			release_message_sync(state, header);
			break;

		case VCHIQ_MSG_DATA:
			vchiq_log_trace(vchiq_sync_log_level,
				"%d: sf DATA@%pK,%x (%d->%d)",
				state->id, header, size, remoteport, localport);

			if ((service->remoteport == remoteport) &&
				(service->srvstate ==
				VCHIQ_SRVSTATE_OPENSYNC)) {
				if (make_service_callback(service,
					VCHIQ_MESSAGE_AVAILABLE, header,
					NULL) == VCHIQ_RETRY)
					vchiq_log_error(vchiq_sync_log_level,
						"synchronous callback to "
						"service %d returns "
						"VCHIQ_RETRY",
						localport);
			}
			break;

		default:
			vchiq_log_error(vchiq_sync_log_level,
				"%d: sf unexpected msgid %x@%pK,%x",
				state->id, msgid, header, size);
			release_message_sync(state, header);
			break;
		}

		unlock_service(service);
	}

	return 0;
}

static void
init_bulk_queue(struct vchiq_bulk_queue *queue)
{
	queue->local_insert = 0;
	queue->remote_insert = 0;
	queue->process = 0;
	queue->remote_notify = 0;
	queue->remove = 0;
}

inline const char *
get_conn_state_name(VCHIQ_CONNSTATE_T conn_state)
{
	return conn_state_names[conn_state];
}

struct vchiq_slot_zero *
vchiq_init_slots(void *mem_base, int mem_size)
{
	int mem_align =
		(int)((VCHIQ_SLOT_SIZE - (long)mem_base) & VCHIQ_SLOT_MASK);
	struct vchiq_slot_zero *slot_zero =
		(struct vchiq_slot_zero *)(mem_base + mem_align);
	int num_slots = (mem_size - mem_align)/VCHIQ_SLOT_SIZE;
	int first_data_slot = VCHIQ_SLOT_ZERO_SLOTS;

	/* Ensure there is enough memory to run an absolutely minimum system */
	num_slots -= first_data_slot;

	if (num_slots < 4) {
		vchiq_log_error(vchiq_core_log_level,
			"%s - insufficient memory %x bytes",
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
	slot_zero->master.slot_last = first_data_slot + (num_slots/2) - 1;
	slot_zero->slave.slot_sync = first_data_slot + (num_slots/2);
	slot_zero->slave.slot_first = first_data_slot + (num_slots/2) + 1;
	slot_zero->slave.slot_last = first_data_slot + num_slots - 1;

	return slot_zero;
}

VCHIQ_STATUS_T
vchiq_init_state(struct vchiq_state *state, struct vchiq_slot_zero *slot_zero)
{
	struct vchiq_shared_state *local;
	struct vchiq_shared_state *remote;
	VCHIQ_STATUS_T status;
	char threadname[16];
	int i;

	vchiq_log_warning(vchiq_core_log_level,
		"%s: slot_zero = %pK", __func__, slot_zero);

	if (vchiq_states[0]) {
		pr_err("%s: VCHIQ state already initialized\n", __func__);
		return VCHIQ_ERROR;
	}

	local = &slot_zero->slave;
	remote = &slot_zero->master;

	if (local->initialised) {
		vchiq_loud_error_header();
		if (remote->initialised)
			vchiq_loud_error("local state has already been "
				"initialised");
		else
			vchiq_loud_error("master/slave mismatch two slaves");
		vchiq_loud_error_footer();
		return VCHIQ_ERROR;
	}

	memset(state, 0, sizeof(struct vchiq_state));

	/*
		initialize shared state pointers
	 */

	state->local = local;
	state->remote = remote;
	state->slot_data = (struct vchiq_slot *)slot_zero;

	/*
		initialize events and mutexes
	 */

	init_completion(&state->connect);
	mutex_init(&state->mutex);
	mutex_init(&state->slot_mutex);
	mutex_init(&state->recycle_mutex);
	mutex_init(&state->sync_mutex);
	mutex_init(&state->bulk_transfer_mutex);

	init_completion(&state->slot_available_event);
	init_completion(&state->slot_remove_event);
	init_completion(&state->data_quota_event);

	state->slot_queue_available = 0;

	for (i = 0; i < VCHIQ_MAX_SERVICES; i++) {
		struct vchiq_service_quota *service_quota =
			&state->service_quotas[i];
		init_completion(&service_quota->quota_event);
	}

	for (i = local->slot_first; i <= local->slot_last; i++) {
		local->slot_queue[state->slot_queue_available++] = i;
		complete(&state->slot_available_event);
	}

	state->default_slot_quota = state->slot_queue_available/2;
	state->default_message_quota =
		min((unsigned short)(state->default_slot_quota * 256),
		(unsigned short)~0);

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

	status = vchiq_platform_init_state(state);

	/*
		bring up slot handler thread
	 */
	snprintf(threadname, sizeof(threadname), "vchiq-slot/%d", state->id);
	state->slot_handler_thread = kthread_create(&slot_handler_func,
		(void *)state,
		threadname);

	if (IS_ERR(state->slot_handler_thread)) {
		vchiq_loud_error_header();
		vchiq_loud_error("couldn't create thread %s", threadname);
		vchiq_loud_error_footer();
		return VCHIQ_ERROR;
	}
	set_user_nice(state->slot_handler_thread, -19);

	snprintf(threadname, sizeof(threadname), "vchiq-recy/%d", state->id);
	state->recycle_thread = kthread_create(&recycle_func,
		(void *)state,
		threadname);
	if (IS_ERR(state->recycle_thread)) {
		vchiq_loud_error_header();
		vchiq_loud_error("couldn't create thread %s", threadname);
		vchiq_loud_error_footer();
		goto fail_free_handler_thread;
	}
	set_user_nice(state->recycle_thread, -19);

	snprintf(threadname, sizeof(threadname), "vchiq-sync/%d", state->id);
	state->sync_thread = kthread_create(&sync_func,
		(void *)state,
		threadname);
	if (IS_ERR(state->sync_thread)) {
		vchiq_loud_error_header();
		vchiq_loud_error("couldn't create thread %s", threadname);
		vchiq_loud_error_footer();
		goto fail_free_recycle_thread;
	}
	set_user_nice(state->sync_thread, -20);

	wake_up_process(state->slot_handler_thread);
	wake_up_process(state->recycle_thread);
	wake_up_process(state->sync_thread);

	vchiq_states[0] = state;

	/* Indicate readiness to the other side */
	local->initialised = 1;

	return status;

fail_free_recycle_thread:
	kthread_stop(state->recycle_thread);
fail_free_handler_thread:
	kthread_stop(state->slot_handler_thread);

	return VCHIQ_ERROR;
}

/* Called from application thread when a client or server service is created. */
struct vchiq_service *
vchiq_add_service_internal(struct vchiq_state *state,
			   const struct vchiq_service_params *params,
			   int srvstate, VCHIQ_INSTANCE_T instance,
			   VCHIQ_USERDATA_TERM_T userdata_term)
{
	struct vchiq_service *service;
	struct vchiq_service **pservice = NULL;
	struct vchiq_service_quota *service_quota;
	int i;

	service = kmalloc(sizeof(*service), GFP_KERNEL);
	if (!service)
		return service;

	service->base.fourcc   = params->fourcc;
	service->base.callback = params->callback;
	service->base.userdata = params->userdata;
	service->handle        = VCHIQ_SERVICE_HANDLE_INVALID;
	service->ref_count     = 1;
	service->srvstate      = VCHIQ_SRVSTATE_FREE;
	service->userdata_term = userdata_term;
	service->localport     = VCHIQ_PORT_FREE;
	service->remoteport    = VCHIQ_PORT_FREE;

	service->public_fourcc = (srvstate == VCHIQ_SRVSTATE_OPENING) ?
		VCHIQ_FOURCC_INVALID : params->fourcc;
	service->client_id     = 0;
	service->auto_close    = 1;
	service->sync          = 0;
	service->closing       = 0;
	service->trace         = 0;
	atomic_set(&service->poll_flags, 0);
	service->version       = params->version;
	service->version_min   = params->version_min;
	service->state         = state;
	service->instance      = instance;
	service->service_use_count = 0;
	init_bulk_queue(&service->bulk_tx);
	init_bulk_queue(&service->bulk_rx);
	init_completion(&service->remove_event);
	init_completion(&service->bulk_remove_event);
	mutex_init(&service->bulk_mutex);
	memset(&service->stats, 0, sizeof(service->stats));

	/* Although it is perfectly possible to use service_spinlock
	** to protect the creation of services, it is overkill as it
	** disables interrupts while the array is searched.
	** The only danger is of another thread trying to create a
	** service - service deletion is safe.
	** Therefore it is preferable to use state->mutex which,
	** although slower to claim, doesn't block interrupts while
	** it is held.
	*/

	mutex_lock(&state->mutex);

	/* Prepare to use a previously unused service */
	if (state->unused_service < VCHIQ_MAX_SERVICES)
		pservice = &state->services[state->unused_service];

	if (srvstate == VCHIQ_SRVSTATE_OPENING) {
		for (i = 0; i < state->unused_service; i++) {
			struct vchiq_service *srv = state->services[i];

			if (!srv) {
				pservice = &state->services[i];
				break;
			}
		}
	} else {
		for (i = (state->unused_service - 1); i >= 0; i--) {
			struct vchiq_service *srv = state->services[i];

			if (!srv)
				pservice = &state->services[i];
			else if ((srv->public_fourcc == params->fourcc)
				&& ((srv->instance != instance) ||
				(srv->base.callback !=
				params->callback))) {
				/* There is another server using this
				** fourcc which doesn't match. */
				pservice = NULL;
				break;
			}
		}
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
		*pservice = service;
		if (pservice == &state->services[state->unused_service])
			state->unused_service++;
	}

	mutex_unlock(&state->mutex);

	if (!pservice) {
		kfree(service);
		return NULL;
	}

	service_quota = &state->service_quotas[service->localport];
	service_quota->slot_quota = state->default_slot_quota;
	service_quota->message_quota = state->default_message_quota;
	if (service_quota->slot_use_count == 0)
		service_quota->previous_tx_index =
			SLOT_QUEUE_INDEX_FROM_POS(state->local_tx_pos)
			- 1;

	/* Bring this service online */
	vchiq_set_service_state(service, srvstate);

	vchiq_log_info(vchiq_core_msg_log_level,
		"%s Service %c%c%c%c SrcPort:%d",
		(srvstate == VCHIQ_SRVSTATE_OPENING)
		? "Open" : "Add",
		VCHIQ_FOURCC_AS_4CHARS(params->fourcc),
		service->localport);

	/* Don't unlock the service - leave it with a ref_count of 1. */

	return service;
}

VCHIQ_STATUS_T
vchiq_open_service_internal(struct vchiq_service *service, int client_id)
{
	struct vchiq_open_payload payload = {
		service->base.fourcc,
		client_id,
		service->version,
		service->version_min
	};
	VCHIQ_STATUS_T status = VCHIQ_SUCCESS;

	service->client_id = client_id;
	vchiq_use_service_internal(service);
	status = queue_message(service->state,
			       NULL,
			       VCHIQ_MAKE_MSG(VCHIQ_MSG_OPEN,
					      service->localport,
					      0),
			       memcpy_copy_callback,
			       &payload,
			       sizeof(payload),
			       QMFLAGS_IS_BLOCKING);
	if (status == VCHIQ_SUCCESS) {
		/* Wait for the ACK/NAK */
		if (wait_for_completion_killable(&service->remove_event)) {
			status = VCHIQ_RETRY;
			vchiq_release_service_internal(service);
		} else if ((service->srvstate != VCHIQ_SRVSTATE_OPEN) &&
			(service->srvstate != VCHIQ_SRVSTATE_OPENSYNC)) {
			if (service->srvstate != VCHIQ_SRVSTATE_CLOSEWAIT)
				vchiq_log_error(vchiq_core_log_level,
					"%d: osi - srvstate = %s (ref %d)",
					service->state->id,
					srvstate_names[service->srvstate],
					service->ref_count);
			status = VCHIQ_ERROR;
			VCHIQ_SERVICE_STATS_INC(service, error_count);
			vchiq_release_service_internal(service);
		}
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
		if (slot_info->release_count != slot_info->use_count) {
			char *data =
				(char *)SLOT_DATA_FROM_INDEX(state, i);
			unsigned int pos, end;

			end = VCHIQ_SLOT_SIZE;
			if (data == state->rx_data)
				/* This buffer is still being read from - stop
				** at the current read position */
				end = state->rx_pos & VCHIQ_SLOT_MASK;

			pos = 0;

			while (pos < end) {
				struct vchiq_header *header =
					(struct vchiq_header *)(data + pos);
				int msgid = header->msgid;
				int port = VCHIQ_MSG_DSTPORT(msgid);

				if ((port == service->localport) &&
					(msgid & VCHIQ_MSGID_CLAIMED)) {
					vchiq_log_info(vchiq_core_log_level,
						"  fsi - hdr %pK", header);
					release_slot(state, slot_info, header,
						NULL);
				}
				pos += calc_stride(header->size);
				if (pos > VCHIQ_SLOT_SIZE) {
					vchiq_log_error(vchiq_core_log_level,
						"fsi - pos %x: header %pK, msgid %x, header->msgid %x, header->size %x",
						pos, header, msgid,
						header->msgid, header->size);
					WARN(1, "invalid slot position\n");
				}
			}
		}
	}
}

static int
do_abort_bulks(struct vchiq_service *service)
{
	VCHIQ_STATUS_T status;

	/* Abort any outstanding bulk transfers */
	if (mutex_lock_killable(&service->bulk_mutex) != 0)
		return 0;
	abort_outstanding_bulks(service, &service->bulk_tx);
	abort_outstanding_bulks(service, &service->bulk_rx);
	mutex_unlock(&service->bulk_mutex);

	status = notify_bulks(service, &service->bulk_tx, 0/*!retry_poll*/);
	if (status == VCHIQ_SUCCESS)
		status = notify_bulks(service, &service->bulk_rx,
			0/*!retry_poll*/);
	return (status == VCHIQ_SUCCESS);
}

static VCHIQ_STATUS_T
close_service_complete(struct vchiq_service *service, int failstate)
{
	VCHIQ_STATUS_T status;
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
			} else
				newstate = VCHIQ_SRVSTATE_CLOSEWAIT;
		} else
			newstate = VCHIQ_SRVSTATE_CLOSED;
		vchiq_set_service_state(service, newstate);
		break;
	case VCHIQ_SRVSTATE_LISTENING:
		break;
	default:
		vchiq_log_error(vchiq_core_log_level,
			"%s(%x) called in state %s", __func__,
			service->handle, srvstate_names[service->srvstate]);
		WARN(1, "%s in unexpected state\n", __func__);
		return VCHIQ_ERROR;
	}

	status = make_service_callback(service,
		VCHIQ_SERVICE_CLOSED, NULL, NULL);

	if (status != VCHIQ_RETRY) {
		int uc = service->service_use_count;
		int i;
		/* Complete the close process */
		for (i = 0; i < uc; i++)
			/* cater for cases where close is forced and the
			** client may not close all it's handles */
			vchiq_release_service_internal(service);

		service->client_id = 0;
		service->remoteport = VCHIQ_PORT_FREE;

		if (service->srvstate == VCHIQ_SRVSTATE_CLOSED)
			vchiq_free_service_internal(service);
		else if (service->srvstate != VCHIQ_SRVSTATE_CLOSEWAIT) {
			if (is_server)
				service->closing = 0;

			complete(&service->remove_event);
		}
	} else
		vchiq_set_service_state(service, failstate);

	return status;
}

/* Called by the slot handler */
VCHIQ_STATUS_T
vchiq_close_service_internal(struct vchiq_service *service, int close_recvd)
{
	struct vchiq_state *state = service->state;
	VCHIQ_STATUS_T status = VCHIQ_SUCCESS;
	int is_server = (service->public_fourcc != VCHIQ_FOURCC_INVALID);

	vchiq_log_info(vchiq_core_log_level, "%d: csi:%d,%d (%s)",
		service->state->id, service->localport, close_recvd,
		srvstate_names[service->srvstate]);

	switch (service->srvstate) {
	case VCHIQ_SRVSTATE_CLOSED:
	case VCHIQ_SRVSTATE_HIDDEN:
	case VCHIQ_SRVSTATE_LISTENING:
	case VCHIQ_SRVSTATE_CLOSEWAIT:
		if (close_recvd)
			vchiq_log_error(vchiq_core_log_level,
				"%s(1) called "
				"in state %s",
				__func__, srvstate_names[service->srvstate]);
		else if (is_server) {
			if (service->srvstate == VCHIQ_SRVSTATE_LISTENING) {
				status = VCHIQ_ERROR;
			} else {
				service->client_id = 0;
				service->remoteport = VCHIQ_PORT_FREE;
				if (service->srvstate ==
					VCHIQ_SRVSTATE_CLOSEWAIT)
					vchiq_set_service_state(service,
						VCHIQ_SRVSTATE_LISTENING);
			}
			complete(&service->remove_event);
		} else
			vchiq_free_service_internal(service);
		break;
	case VCHIQ_SRVSTATE_OPENING:
		if (close_recvd) {
			/* The open was rejected - tell the user */
			vchiq_set_service_state(service,
				VCHIQ_SRVSTATE_CLOSEWAIT);
			complete(&service->remove_event);
		} else {
			/* Shutdown mid-open - let the other side know */
			status = queue_message(state, service,
				VCHIQ_MAKE_MSG
				(VCHIQ_MSG_CLOSE,
				service->localport,
				VCHIQ_MSG_DSTPORT(service->remoteport)),
				NULL, NULL, 0, 0);
		}
		break;

	case VCHIQ_SRVSTATE_OPENSYNC:
		mutex_lock(&state->sync_mutex);
		/* fall through */
	case VCHIQ_SRVSTATE_OPEN:
		if (close_recvd) {
			if (!do_abort_bulks(service))
				status = VCHIQ_RETRY;
		}

		release_service_messages(service);

		if (status == VCHIQ_SUCCESS)
			status = queue_message(state, service,
				VCHIQ_MAKE_MSG
				(VCHIQ_MSG_CLOSE,
				service->localport,
				VCHIQ_MSG_DSTPORT(service->remoteport)),
				NULL, NULL, 0, QMFLAGS_NO_MUTEX_UNLOCK);

		if (status == VCHIQ_SUCCESS) {
			if (!close_recvd) {
				/* Change the state while the mutex is
				   still held */
				vchiq_set_service_state(service,
							VCHIQ_SRVSTATE_CLOSESENT);
				mutex_unlock(&state->slot_mutex);
				if (service->sync)
					mutex_unlock(&state->sync_mutex);
				break;
			}
		} else if (service->srvstate == VCHIQ_SRVSTATE_OPENSYNC) {
			mutex_unlock(&state->sync_mutex);
			break;
		} else
			break;

		/* Change the state while the mutex is still held */
		vchiq_set_service_state(service, VCHIQ_SRVSTATE_CLOSERECVD);
		mutex_unlock(&state->slot_mutex);
		if (service->sync)
			mutex_unlock(&state->sync_mutex);

		status = close_service_complete(service,
				VCHIQ_SRVSTATE_CLOSERECVD);
		break;

	case VCHIQ_SRVSTATE_CLOSESENT:
		if (!close_recvd)
			/* This happens when a process is killed mid-close */
			break;

		if (!do_abort_bulks(service)) {
			status = VCHIQ_RETRY;
			break;
		}

		if (status == VCHIQ_SUCCESS)
			status = close_service_complete(service,
				VCHIQ_SRVSTATE_CLOSERECVD);
		break;

	case VCHIQ_SRVSTATE_CLOSERECVD:
		if (!close_recvd && is_server)
			/* Force into LISTENING mode */
			vchiq_set_service_state(service,
				VCHIQ_SRVSTATE_LISTENING);
		status = close_service_complete(service,
			VCHIQ_SRVSTATE_CLOSERECVD);
		break;

	default:
		vchiq_log_error(vchiq_core_log_level,
			"%s(%d) called in state %s", __func__,
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

	vchiq_log_info(vchiq_core_log_level, "%d: tsi - (%d<->%d)",
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

	vchiq_log_info(vchiq_core_log_level, "%d: fsi - (%d)",
		state->id, service->localport);

	switch (service->srvstate) {
	case VCHIQ_SRVSTATE_OPENING:
	case VCHIQ_SRVSTATE_CLOSED:
	case VCHIQ_SRVSTATE_HIDDEN:
	case VCHIQ_SRVSTATE_LISTENING:
	case VCHIQ_SRVSTATE_CLOSEWAIT:
		break;
	default:
		vchiq_log_error(vchiq_core_log_level,
			"%d: fsi - (%d) in state %s",
			state->id, service->localport,
			srvstate_names[service->srvstate]);
		return;
	}

	vchiq_set_service_state(service, VCHIQ_SRVSTATE_FREE);

	complete(&service->remove_event);

	/* Release the initial lock */
	unlock_service(service);
}

VCHIQ_STATUS_T
vchiq_connect_internal(struct vchiq_state *state, VCHIQ_INSTANCE_T instance)
{
	struct vchiq_service *service;
	int i;

	/* Find all services registered to this client and enable them. */
	i = 0;
	while ((service = next_service_by_instance(state, instance,
		&i)) !=	NULL) {
		if (service->srvstate == VCHIQ_SRVSTATE_HIDDEN)
			vchiq_set_service_state(service,
				VCHIQ_SRVSTATE_LISTENING);
		unlock_service(service);
	}

	if (state->conn_state == VCHIQ_CONNSTATE_DISCONNECTED) {
		if (queue_message(state, NULL,
			VCHIQ_MAKE_MSG(VCHIQ_MSG_CONNECT, 0, 0), NULL, NULL,
			0, QMFLAGS_IS_BLOCKING) == VCHIQ_RETRY)
			return VCHIQ_RETRY;

		vchiq_set_conn_state(state, VCHIQ_CONNSTATE_CONNECTING);
	}

	if (state->conn_state == VCHIQ_CONNSTATE_CONNECTING) {
		if (wait_for_completion_killable(&state->connect))
			return VCHIQ_RETRY;

		vchiq_set_conn_state(state, VCHIQ_CONNSTATE_CONNECTED);
		complete(&state->connect);
	}

	return VCHIQ_SUCCESS;
}

VCHIQ_STATUS_T
vchiq_shutdown_internal(struct vchiq_state *state, VCHIQ_INSTANCE_T instance)
{
	struct vchiq_service *service;
	int i;

	/* Find all services registered to this client and enable them. */
	i = 0;
	while ((service = next_service_by_instance(state, instance,
		&i)) !=	NULL) {
		(void)vchiq_remove_service(service->handle);
		unlock_service(service);
	}

	return VCHIQ_SUCCESS;
}

VCHIQ_STATUS_T
vchiq_pause_internal(struct vchiq_state *state)
{
	VCHIQ_STATUS_T status = VCHIQ_SUCCESS;

	switch (state->conn_state) {
	case VCHIQ_CONNSTATE_CONNECTED:
		/* Request a pause */
		vchiq_set_conn_state(state, VCHIQ_CONNSTATE_PAUSING);
		request_poll(state, NULL, 0);
		break;
	default:
		vchiq_log_error(vchiq_core_log_level,
			"%s in state %s\n",
			__func__, conn_state_names[state->conn_state]);
		status = VCHIQ_ERROR;
		VCHIQ_STATS_INC(state, error_count);
		break;
	}

	return status;
}

VCHIQ_STATUS_T
vchiq_resume_internal(struct vchiq_state *state)
{
	VCHIQ_STATUS_T status = VCHIQ_SUCCESS;

	if (state->conn_state == VCHIQ_CONNSTATE_PAUSED) {
		vchiq_set_conn_state(state, VCHIQ_CONNSTATE_RESUMING);
		request_poll(state, NULL, 0);
	} else {
		status = VCHIQ_ERROR;
		VCHIQ_STATS_INC(state, error_count);
	}

	return status;
}

VCHIQ_STATUS_T
vchiq_close_service(VCHIQ_SERVICE_HANDLE_T handle)
{
	/* Unregister the service */
	struct vchiq_service *service = find_service_by_handle(handle);
	VCHIQ_STATUS_T status = VCHIQ_SUCCESS;

	if (!service)
		return VCHIQ_ERROR;

	vchiq_log_info(vchiq_core_log_level,
		"%d: close_service:%d",
		service->state->id, service->localport);

	if ((service->srvstate == VCHIQ_SRVSTATE_FREE) ||
		(service->srvstate == VCHIQ_SRVSTATE_LISTENING) ||
		(service->srvstate == VCHIQ_SRVSTATE_HIDDEN)) {
		unlock_service(service);
		return VCHIQ_ERROR;
	}

	mark_service_closing(service);

	if (current == service->state->slot_handler_thread) {
		status = vchiq_close_service_internal(service,
			0/*!close_recvd*/);
		WARN_ON(status == VCHIQ_RETRY);
	} else {
	/* Mark the service for termination by the slot handler */
		request_poll(service->state, service, VCHIQ_POLL_TERMINATE);
	}

	while (1) {
		if (wait_for_completion_killable(&service->remove_event)) {
			status = VCHIQ_RETRY;
			break;
		}

		if ((service->srvstate == VCHIQ_SRVSTATE_FREE) ||
			(service->srvstate == VCHIQ_SRVSTATE_LISTENING) ||
			(service->srvstate == VCHIQ_SRVSTATE_OPEN))
			break;

		vchiq_log_warning(vchiq_core_log_level,
			"%d: close_service:%d - waiting in state %s",
			service->state->id, service->localport,
			srvstate_names[service->srvstate]);
	}

	if ((status == VCHIQ_SUCCESS) &&
		(service->srvstate != VCHIQ_SRVSTATE_FREE) &&
		(service->srvstate != VCHIQ_SRVSTATE_LISTENING))
		status = VCHIQ_ERROR;

	unlock_service(service);

	return status;
}

VCHIQ_STATUS_T
vchiq_remove_service(VCHIQ_SERVICE_HANDLE_T handle)
{
	/* Unregister the service */
	struct vchiq_service *service = find_service_by_handle(handle);
	VCHIQ_STATUS_T status = VCHIQ_SUCCESS;

	if (!service)
		return VCHIQ_ERROR;

	vchiq_log_info(vchiq_core_log_level,
		"%d: remove_service:%d",
		service->state->id, service->localport);

	if (service->srvstate == VCHIQ_SRVSTATE_FREE) {
		unlock_service(service);
		return VCHIQ_ERROR;
	}

	mark_service_closing(service);

	if ((service->srvstate == VCHIQ_SRVSTATE_HIDDEN) ||
		(current == service->state->slot_handler_thread)) {
		/* Make it look like a client, because it must be removed and
		   not left in the LISTENING state. */
		service->public_fourcc = VCHIQ_FOURCC_INVALID;

		status = vchiq_close_service_internal(service,
			0/*!close_recvd*/);
		WARN_ON(status == VCHIQ_RETRY);
	} else {
		/* Mark the service for removal by the slot handler */
		request_poll(service->state, service, VCHIQ_POLL_REMOVE);
	}
	while (1) {
		if (wait_for_completion_killable(&service->remove_event)) {
			status = VCHIQ_RETRY;
			break;
		}

		if ((service->srvstate == VCHIQ_SRVSTATE_FREE) ||
			(service->srvstate == VCHIQ_SRVSTATE_OPEN))
			break;

		vchiq_log_warning(vchiq_core_log_level,
			"%d: remove_service:%d - waiting in state %s",
			service->state->id, service->localport,
			srvstate_names[service->srvstate]);
	}

	if ((status == VCHIQ_SUCCESS) &&
		(service->srvstate != VCHIQ_SRVSTATE_FREE))
		status = VCHIQ_ERROR;

	unlock_service(service);

	return status;
}

/* This function may be called by kernel threads or user threads.
 * User threads may receive VCHIQ_RETRY to indicate that a signal has been
 * received and the call should be retried after being returned to user
 * context.
 * When called in blocking mode, the userdata field points to a bulk_waiter
 * structure.
 */
VCHIQ_STATUS_T vchiq_bulk_transfer(VCHIQ_SERVICE_HANDLE_T handle,
				   void *offset, int size, void *userdata,
				   VCHIQ_BULK_MODE_T mode,
				   VCHIQ_BULK_DIR_T dir)
{
	struct vchiq_service *service = find_service_by_handle(handle);
	struct vchiq_bulk_queue *queue;
	struct vchiq_bulk *bulk;
	struct vchiq_state *state;
	struct bulk_waiter *bulk_waiter = NULL;
	const char dir_char = (dir == VCHIQ_BULK_TRANSMIT) ? 't' : 'r';
	const int dir_msgtype = (dir == VCHIQ_BULK_TRANSMIT) ?
		VCHIQ_MSG_BULK_TX : VCHIQ_MSG_BULK_RX;
	VCHIQ_STATUS_T status = VCHIQ_ERROR;
	int payload[2];

	if (!service || service->srvstate != VCHIQ_SRVSTATE_OPEN ||
	    !offset || vchiq_check_service(service) != VCHIQ_SUCCESS)
		goto error_exit;

	switch (mode) {
	case VCHIQ_BULK_MODE_NOCALLBACK:
	case VCHIQ_BULK_MODE_CALLBACK:
		break;
	case VCHIQ_BULK_MODE_BLOCKING:
		bulk_waiter = userdata;
		init_completion(&bulk_waiter->event);
		bulk_waiter->actual = 0;
		bulk_waiter->bulk = NULL;
		break;
	case VCHIQ_BULK_MODE_WAITING:
		bulk_waiter = userdata;
		bulk = bulk_waiter->bulk;
		goto waiting;
	default:
		goto error_exit;
	}

	state = service->state;

	queue = (dir == VCHIQ_BULK_TRANSMIT) ?
		&service->bulk_tx : &service->bulk_rx;

	if (mutex_lock_killable(&service->bulk_mutex) != 0) {
		status = VCHIQ_RETRY;
		goto error_exit;
	}

	if (queue->local_insert == queue->remove + VCHIQ_NUM_SERVICE_BULKS) {
		VCHIQ_SERVICE_STATS_INC(service, bulk_stalls);
		do {
			mutex_unlock(&service->bulk_mutex);
			if (wait_for_completion_killable(
						&service->bulk_remove_event)) {
				status = VCHIQ_RETRY;
				goto error_exit;
			}
			if (mutex_lock_killable(&service->bulk_mutex)
				!= 0) {
				status = VCHIQ_RETRY;
				goto error_exit;
			}
		} while (queue->local_insert == queue->remove +
				VCHIQ_NUM_SERVICE_BULKS);
	}

	bulk = &queue->bulks[BULK_INDEX(queue->local_insert)];

	bulk->mode = mode;
	bulk->dir = dir;
	bulk->userdata = userdata;
	bulk->size = size;
	bulk->actual = VCHIQ_BULK_ACTUAL_ABORTED;

	if (vchiq_prepare_bulk_data(bulk, offset, size, dir) != VCHIQ_SUCCESS)
		goto unlock_error_exit;

	wmb();

	vchiq_log_info(vchiq_core_log_level,
		"%d: bt (%d->%d) %cx %x@%pK %pK",
		state->id, service->localport, service->remoteport, dir_char,
		size, bulk->data, userdata);

	/* The slot mutex must be held when the service is being closed, so
	   claim it here to ensure that isn't happening */
	if (mutex_lock_killable(&state->slot_mutex) != 0) {
		status = VCHIQ_RETRY;
		goto cancel_bulk_error_exit;
	}

	if (service->srvstate != VCHIQ_SRVSTATE_OPEN)
		goto unlock_both_error_exit;

	payload[0] = (int)(long)bulk->data;
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
	if (status != VCHIQ_SUCCESS) {
		goto unlock_both_error_exit;
	}

	queue->local_insert++;

	mutex_unlock(&state->slot_mutex);
	mutex_unlock(&service->bulk_mutex);

	vchiq_log_trace(vchiq_core_log_level,
		"%d: bt:%d %cx li=%x ri=%x p=%x",
		state->id,
		service->localport, dir_char,
		queue->local_insert, queue->remote_insert, queue->process);

waiting:
	unlock_service(service);

	status = VCHIQ_SUCCESS;

	if (bulk_waiter) {
		bulk_waiter->bulk = bulk;
		if (wait_for_completion_killable(&bulk_waiter->event))
			status = VCHIQ_RETRY;
		else if (bulk_waiter->actual == VCHIQ_BULK_ACTUAL_ABORTED)
			status = VCHIQ_ERROR;
	}

	return status;

unlock_both_error_exit:
	mutex_unlock(&state->slot_mutex);
cancel_bulk_error_exit:
	vchiq_complete_bulk(bulk);
unlock_error_exit:
	mutex_unlock(&service->bulk_mutex);

error_exit:
	if (service)
		unlock_service(service);
	return status;
}

VCHIQ_STATUS_T
vchiq_queue_message(VCHIQ_SERVICE_HANDLE_T handle,
		    ssize_t (*copy_callback)(void *context, void *dest,
					     size_t offset, size_t maxsize),
		    void *context,
		    size_t size)
{
	struct vchiq_service *service = find_service_by_handle(handle);
	VCHIQ_STATUS_T status = VCHIQ_ERROR;

	if (!service ||
		(vchiq_check_service(service) != VCHIQ_SUCCESS))
		goto error_exit;

	if (!size) {
		VCHIQ_SERVICE_STATS_INC(service, error_count);
		goto error_exit;

	}

	if (size > VCHIQ_MAX_MSG_SIZE) {
		VCHIQ_SERVICE_STATS_INC(service, error_count);
		goto error_exit;
	}

	switch (service->srvstate) {
	case VCHIQ_SRVSTATE_OPEN:
		status = queue_message(service->state, service,
				VCHIQ_MAKE_MSG(VCHIQ_MSG_DATA,
					service->localport,
					service->remoteport),
				copy_callback, context, size, 1);
		break;
	case VCHIQ_SRVSTATE_OPENSYNC:
		status = queue_message_sync(service->state, service,
				VCHIQ_MAKE_MSG(VCHIQ_MSG_DATA,
					service->localport,
					service->remoteport),
				copy_callback, context, size, 1);
		break;
	default:
		status = VCHIQ_ERROR;
		break;
	}

error_exit:
	if (service)
		unlock_service(service);

	return status;
}

void
vchiq_release_message(VCHIQ_SERVICE_HANDLE_T handle,
		      struct vchiq_header *header)
{
	struct vchiq_service *service = find_service_by_handle(handle);
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
	} else if (slot_index == remote->slot_sync)
		release_message_sync(state, header);

	unlock_service(service);
}

static void
release_message_sync(struct vchiq_state *state, struct vchiq_header *header)
{
	header->msgid = VCHIQ_MSGID_PADDING;
	remote_event_signal(&state->remote->sync_release);
}

VCHIQ_STATUS_T
vchiq_get_peer_version(VCHIQ_SERVICE_HANDLE_T handle, short *peer_version)
{
	VCHIQ_STATUS_T status = VCHIQ_ERROR;
	struct vchiq_service *service = find_service_by_handle(handle);

	if (!service ||
	    (vchiq_check_service(service) != VCHIQ_SUCCESS) ||
	    !peer_version)
		goto exit;
	*peer_version = service->peer_version;
	status = VCHIQ_SUCCESS;

exit:
	if (service)
		unlock_service(service);
	return status;
}

void vchiq_get_config(struct vchiq_config *config)
{
	config->max_msg_size           = VCHIQ_MAX_MSG_SIZE;
	config->bulk_threshold         = VCHIQ_MAX_MSG_SIZE;
	config->max_outstanding_bulks  = VCHIQ_NUM_SERVICE_BULKS;
	config->max_services           = VCHIQ_MAX_SERVICES;
	config->version                = VCHIQ_VERSION;
	config->version_min            = VCHIQ_VERSION_MIN;
}

VCHIQ_STATUS_T
vchiq_set_service_option(VCHIQ_SERVICE_HANDLE_T handle,
	VCHIQ_SERVICE_OPTION_T option, int value)
{
	struct vchiq_service *service = find_service_by_handle(handle);
	VCHIQ_STATUS_T status = VCHIQ_ERROR;

	if (service) {
		switch (option) {
		case VCHIQ_SERVICE_OPTION_AUTOCLOSE:
			service->auto_close = value;
			status = VCHIQ_SUCCESS;
			break;

		case VCHIQ_SERVICE_OPTION_SLOT_QUOTA: {
			struct vchiq_service_quota *service_quota =
				&service->state->service_quotas[
					service->localport];
			if (value == 0)
				value = service->state->default_slot_quota;
			if ((value >= service_quota->slot_use_count) &&
				 (value < (unsigned short)~0)) {
				service_quota->slot_quota = value;
				if ((value >= service_quota->slot_use_count) &&
					(service_quota->message_quota >=
					 service_quota->message_use_count)) {
					/* Signal the service that it may have
					** dropped below its quota */
					complete(&service_quota->quota_event);
				}
				status = VCHIQ_SUCCESS;
			}
		} break;

		case VCHIQ_SERVICE_OPTION_MESSAGE_QUOTA: {
			struct vchiq_service_quota *service_quota =
				&service->state->service_quotas[
					service->localport];
			if (value == 0)
				value = service->state->default_message_quota;
			if ((value >= service_quota->message_use_count) &&
				 (value < (unsigned short)~0)) {
				service_quota->message_quota = value;
				if ((value >=
					service_quota->message_use_count) &&
					(service_quota->slot_quota >=
					service_quota->slot_use_count))
					/* Signal the service that it may have
					** dropped below its quota */
					complete(&service_quota->quota_event);
				status = VCHIQ_SUCCESS;
			}
		} break;

		case VCHIQ_SERVICE_OPTION_SYNCHRONOUS:
			if ((service->srvstate == VCHIQ_SRVSTATE_HIDDEN) ||
				(service->srvstate ==
				VCHIQ_SRVSTATE_LISTENING)) {
				service->sync = value;
				status = VCHIQ_SUCCESS;
			}
			break;

		case VCHIQ_SERVICE_OPTION_TRACE:
			service->trace = value;
			status = VCHIQ_SUCCESS;
			break;

		default:
			break;
		}
		unlock_service(service);
	}

	return status;
}

static void
vchiq_dump_shared_state(void *dump_context, struct vchiq_state *state,
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
	char buf[80];
	int len;

	len = snprintf(buf, sizeof(buf),
		"  %s: slots %d-%d tx_pos=%x recycle=%x",
		label, shared->slot_first, shared->slot_last,
		shared->tx_pos, shared->slot_queue_recycle);
	vchiq_dump(dump_context, buf, len + 1);

	len = snprintf(buf, sizeof(buf),
		"    Slots claimed:");
	vchiq_dump(dump_context, buf, len + 1);

	for (i = shared->slot_first; i <= shared->slot_last; i++) {
		struct vchiq_slot_info slot_info =
						*SLOT_INFO_FROM_INDEX(state, i);
		if (slot_info.use_count != slot_info.release_count) {
			len = snprintf(buf, sizeof(buf),
				"      %d: %d/%d", i, slot_info.use_count,
				slot_info.release_count);
			vchiq_dump(dump_context, buf, len + 1);
		}
	}

	for (i = 1; i < shared->debug[DEBUG_ENTRIES]; i++) {
		len = snprintf(buf, sizeof(buf), "    DEBUG: %s = %d(%x)",
			debug_names[i], shared->debug[i], shared->debug[i]);
		vchiq_dump(dump_context, buf, len + 1);
	}
}

void
vchiq_dump_state(void *dump_context, struct vchiq_state *state)
{
	char buf[80];
	int len;
	int i;

	len = snprintf(buf, sizeof(buf), "State %d: %s", state->id,
		conn_state_names[state->conn_state]);
	vchiq_dump(dump_context, buf, len + 1);

	len = snprintf(buf, sizeof(buf),
		"  tx_pos=%x(@%pK), rx_pos=%x(@%pK)",
		state->local->tx_pos,
		state->tx_data + (state->local_tx_pos & VCHIQ_SLOT_MASK),
		state->rx_pos,
		state->rx_data + (state->rx_pos & VCHIQ_SLOT_MASK));
	vchiq_dump(dump_context, buf, len + 1);

	len = snprintf(buf, sizeof(buf),
		"  Version: %d (min %d)",
		VCHIQ_VERSION, VCHIQ_VERSION_MIN);
	vchiq_dump(dump_context, buf, len + 1);

	if (VCHIQ_ENABLE_STATS) {
		len = snprintf(buf, sizeof(buf),
			"  Stats: ctrl_tx_count=%d, ctrl_rx_count=%d, "
			"error_count=%d",
			state->stats.ctrl_tx_count, state->stats.ctrl_rx_count,
			state->stats.error_count);
		vchiq_dump(dump_context, buf, len + 1);
	}

	len = snprintf(buf, sizeof(buf),
		"  Slots: %d available (%d data), %d recyclable, %d stalls "
		"(%d data)",
		((state->slot_queue_available * VCHIQ_SLOT_SIZE) -
			state->local_tx_pos) / VCHIQ_SLOT_SIZE,
		state->data_quota - state->data_use_count,
		state->local->slot_queue_recycle - state->slot_queue_available,
		state->stats.slot_stalls, state->stats.data_stalls);
	vchiq_dump(dump_context, buf, len + 1);

	vchiq_dump_platform_state(dump_context);

	vchiq_dump_shared_state(dump_context, state, state->local, "Local");
	vchiq_dump_shared_state(dump_context, state, state->remote, "Remote");

	vchiq_dump_platform_instances(dump_context);

	for (i = 0; i < state->unused_service; i++) {
		struct vchiq_service *service = find_service_by_port(state, i);

		if (service) {
			vchiq_dump_service_state(dump_context, service);
			unlock_service(service);
		}
	}
}

void
vchiq_dump_service_state(void *dump_context, struct vchiq_service *service)
{
	char buf[80];
	int len;

	len = snprintf(buf, sizeof(buf), "Service %u: %s (ref %u)",
		service->localport, srvstate_names[service->srvstate],
		service->ref_count - 1); /*Don't include the lock just taken*/

	if (service->srvstate != VCHIQ_SRVSTATE_FREE) {
		char remoteport[30];
		struct vchiq_service_quota *service_quota =
			&service->state->service_quotas[service->localport];
		int fourcc = service->base.fourcc;
		int tx_pending, rx_pending;

		if (service->remoteport != VCHIQ_PORT_FREE) {
			int len2 = snprintf(remoteport, sizeof(remoteport),
				"%u", service->remoteport);

			if (service->public_fourcc != VCHIQ_FOURCC_INVALID)
				snprintf(remoteport + len2,
					sizeof(remoteport) - len2,
					" (client %x)", service->client_id);
		} else
			strcpy(remoteport, "n/a");

		len += snprintf(buf + len, sizeof(buf) - len,
			" '%c%c%c%c' remote %s (msg use %d/%d, slot use %d/%d)",
			VCHIQ_FOURCC_AS_4CHARS(fourcc),
			remoteport,
			service_quota->message_use_count,
			service_quota->message_quota,
			service_quota->slot_use_count,
			service_quota->slot_quota);

		vchiq_dump(dump_context, buf, len + 1);

		tx_pending = service->bulk_tx.local_insert -
			service->bulk_tx.remote_insert;

		rx_pending = service->bulk_rx.local_insert -
			service->bulk_rx.remote_insert;

		len = snprintf(buf, sizeof(buf),
			"  Bulk: tx_pending=%d (size %d),"
			" rx_pending=%d (size %d)",
			tx_pending,
			tx_pending ? service->bulk_tx.bulks[
			BULK_INDEX(service->bulk_tx.remove)].size : 0,
			rx_pending,
			rx_pending ? service->bulk_rx.bulks[
			BULK_INDEX(service->bulk_rx.remove)].size : 0);

		if (VCHIQ_ENABLE_STATS) {
			vchiq_dump(dump_context, buf, len + 1);

			len = snprintf(buf, sizeof(buf),
				"  Ctrl: tx_count=%d, tx_bytes=%llu, "
				"rx_count=%d, rx_bytes=%llu",
				service->stats.ctrl_tx_count,
				service->stats.ctrl_tx_bytes,
				service->stats.ctrl_rx_count,
				service->stats.ctrl_rx_bytes);
			vchiq_dump(dump_context, buf, len + 1);

			len = snprintf(buf, sizeof(buf),
				"  Bulk: tx_count=%d, tx_bytes=%llu, "
				"rx_count=%d, rx_bytes=%llu",
				service->stats.bulk_tx_count,
				service->stats.bulk_tx_bytes,
				service->stats.bulk_rx_count,
				service->stats.bulk_rx_bytes);
			vchiq_dump(dump_context, buf, len + 1);

			len = snprintf(buf, sizeof(buf),
				"  %d quota stalls, %d slot stalls, "
				"%d bulk stalls, %d aborted, %d errors",
				service->stats.quota_stalls,
				service->stats.slot_stalls,
				service->stats.bulk_stalls,
				service->stats.bulk_aborted_count,
				service->stats.error_count);
		}
	}

	vchiq_dump(dump_context, buf, len + 1);

	if (service->srvstate != VCHIQ_SRVSTATE_FREE)
		vchiq_dump_platform_service_state(dump_context, service);
}

void
vchiq_loud_error_header(void)
{
	vchiq_log_error(vchiq_core_log_level,
		"============================================================"
		"================");
	vchiq_log_error(vchiq_core_log_level,
		"============================================================"
		"================");
	vchiq_log_error(vchiq_core_log_level, "=====");
}

void
vchiq_loud_error_footer(void)
{
	vchiq_log_error(vchiq_core_log_level, "=====");
	vchiq_log_error(vchiq_core_log_level,
		"============================================================"
		"================");
	vchiq_log_error(vchiq_core_log_level,
		"============================================================"
		"================");
}

VCHIQ_STATUS_T vchiq_send_remote_use(struct vchiq_state *state)
{
	VCHIQ_STATUS_T status = VCHIQ_RETRY;

	if (state->conn_state != VCHIQ_CONNSTATE_DISCONNECTED)
		status = queue_message(state, NULL,
			VCHIQ_MAKE_MSG(VCHIQ_MSG_REMOTE_USE, 0, 0),
			NULL, NULL, 0, 0);
	return status;
}

VCHIQ_STATUS_T vchiq_send_remote_release(struct vchiq_state *state)
{
	VCHIQ_STATUS_T status = VCHIQ_RETRY;

	if (state->conn_state != VCHIQ_CONNSTATE_DISCONNECTED)
		status = queue_message(state, NULL,
			VCHIQ_MAKE_MSG(VCHIQ_MSG_REMOTE_RELEASE, 0, 0),
			NULL, NULL, 0, 0);
	return status;
}

VCHIQ_STATUS_T vchiq_send_remote_use_active(struct vchiq_state *state)
{
	VCHIQ_STATUS_T status = VCHIQ_RETRY;

	if (state->conn_state != VCHIQ_CONNSTATE_DISCONNECTED)
		status = queue_message(state, NULL,
			VCHIQ_MAKE_MSG(VCHIQ_MSG_REMOTE_USE_ACTIVE, 0, 0),
			NULL, NULL, 0, 0);
	return status;
}

void vchiq_log_dump_mem(const char *label, u32 addr, const void *void_mem,
	size_t num_bytes)
{
	const u8  *mem = void_mem;
	size_t          offset;
	char            line_buf[100];
	char           *s;

	while (num_bytes > 0) {
		s = line_buf;

		for (offset = 0; offset < 16; offset++) {
			if (offset < num_bytes)
				s += snprintf(s, 4, "%02x ", mem[offset]);
			else
				s += snprintf(s, 4, "   ");
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

		if ((label != NULL) && (*label != '\0'))
			vchiq_log_trace(VCHIQ_LOG_TRACE,
				"%s: %08x: %s", label, addr, line_buf);
		else
			vchiq_log_trace(VCHIQ_LOG_TRACE,
				"%08x: %s", addr, line_buf);

		addr += 16;
		mem += 16;
		if (num_bytes > 16)
			num_bytes -= 16;
		else
			num_bytes = 0;
	}
}
