/*
 * Copyright (c) 2008-2012 Niels Provos and Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef BUFFEREVENT_INTERNAL_H_INCLUDED_
#define BUFFEREVENT_INTERNAL_H_INCLUDED_

#ifdef __cplusplus
extern "C" {
#endif

#include "event2/event-config.h"
#include "event2/event_struct.h"
#include "evconfig-private.h"
#include "event2/util.h"
#include "defer-internal.h"
#include "evthread-internal.h"
#include "event2/thread.h"
#include "ratelim-internal.h"
#include "event2/bufferevent_struct.h"

/* These flags are reasons that we might be declining to actually enable
   reading or writing on a bufferevent.
 */

/* On a all bufferevents, for reading: used when we have read up to the
   watermark value.

   On a filtering bufferevent, for writing: used when the underlying
   bufferevent's write buffer has been filled up to its watermark
   value.
*/
#define BEV_SUSPEND_WM 0x01
/* On a base bufferevent: when we have emptied a bandwidth buckets */
#define BEV_SUSPEND_BW 0x02
/* On a base bufferevent: when we have emptied the group's bandwidth bucket. */
#define BEV_SUSPEND_BW_GROUP 0x04
/* On a socket bufferevent: can't do any operations while we're waiting for
 * name lookup to finish. */
#define BEV_SUSPEND_LOOKUP 0x08
/* On a base bufferevent, for reading: used when a filter has choked this
 * (underlying) bufferevent because it has stopped reading from it. */
#define BEV_SUSPEND_FILT_READ 0x10

typedef ev_uint16_t bufferevent_suspend_flags;

struct bufferevent_rate_limit_group {
	/** List of all members in the group */
	LIST_HEAD(rlim_group_member_list, bufferevent_private) members;
	/** Current limits for the group. */
	struct ev_token_bucket rate_limit;
	struct ev_token_bucket_cfg rate_limit_cfg;

	/** True iff we don't want to read from any member of the group.until
	 * the token bucket refills.  */
	unsigned read_suspended : 1;
	/** True iff we don't want to write from any member of the group.until
	 * the token bucket refills.  */
	unsigned write_suspended : 1;
	/** True iff we were unable to suspend one of the bufferevents in the
	 * group for reading the last time we tried, and we should try
	 * again. */
	unsigned pending_unsuspend_read : 1;
	/** True iff we were unable to suspend one of the bufferevents in the
	 * group for writing the last time we tried, and we should try
	 * again. */
	unsigned pending_unsuspend_write : 1;

	/*@{*/
	/** Total number of bytes read or written in this group since last
	 * reset. */
	ev_uint64_t total_read;
	ev_uint64_t total_written;
	/*@}*/

	/** The number of bufferevents in the group. */
	int n_members;

	/** The smallest number of bytes that any member of the group should
	 * be limited to read or write at a time. */
	ev_ssize_t min_share;
	ev_ssize_t configured_min_share;

	/** Timeout event that goes off once a tick, when the bucket is ready
	 * to refill. */
	struct event master_refill_event;

	/** Seed for weak random number generator. Protected by 'lock' */
	struct evutil_weakrand_state weakrand_seed;

	/** Lock to protect the members of this group.  This lock should nest
	 * within every bufferevent lock: if you are holding this lock, do
	 * not assume you can lock another bufferevent. */
	void *lock;
};

/** Fields for rate-limiting a single bufferevent. */
struct bufferevent_rate_limit {
	/* Linked-list elements for storing this bufferevent_private in a
	 * group.
	 *
	 * Note that this field is supposed to be protected by the group
	 * lock */
	LIST_ENTRY(bufferevent_private) next_in_group;
	/** The rate-limiting group for this bufferevent, or NULL if it is
	 * only rate-limited on its own. */
	struct bufferevent_rate_limit_group *group;

	/* This bufferevent's current limits. */
	struct ev_token_bucket limit;
	/* Pointer to the rate-limit configuration for this bufferevent.
	 * Can be shared.  XXX reference-count this? */
	struct ev_token_bucket_cfg *cfg;

	/* Timeout event used when one this bufferevent's buckets are
	 * empty. */
	struct event refill_bucket_event;
};

/** Parts of the bufferevent structure that are shared among all bufferevent
 * types, but not exposed in bufferevent_struct.h. */
struct bufferevent_private {
	/** The underlying bufferevent structure. */
	struct bufferevent bev;

	/** Evbuffer callback to enforce watermarks on input. */
	struct evbuffer_cb_entry *read_watermarks_cb;

	/** If set, we should free the lock when we free the bufferevent. */
	unsigned own_lock : 1;

	/** Flag: set if we have deferred callbacks and a read callback is
	 * pending. */
	unsigned readcb_pending : 1;
	/** Flag: set if we have deferred callbacks and a write callback is
	 * pending. */
	unsigned writecb_pending : 1;
	/** Flag: set if we are currently busy connecting. */
	unsigned connecting : 1;
	/** Flag: set if a connect failed prematurely; this is a hack for
	 * getting around the bufferevent abstraction. */
	unsigned connection_refused : 1;
	/** Set to the events pending if we have deferred callbacks and
	 * an events callback is pending. */
	short eventcb_pending;

	/** If set, read is suspended until one or more conditions are over.
	 * The actual value here is a bitfield of those conditions; see the
	 * BEV_SUSPEND_* flags above. */
	bufferevent_suspend_flags read_suspended;

	/** If set, writing is suspended until one or more conditions are over.
	 * The actual value here is a bitfield of those conditions; see the
	 * BEV_SUSPEND_* flags above. */
	bufferevent_suspend_flags write_suspended;

	/** Set to the current socket errno if we have deferred callbacks and
	 * an events callback is pending. */
	int errno_pending;

	/** The DNS error code for bufferevent_socket_connect_hostname */
	int dns_error;

	/** Used to implement deferred callbacks */
	struct event_callback deferred;

	/** The options this bufferevent was constructed with */
	enum bufferevent_options options;

	/** Current reference count for this bufferevent. */
	int refcnt;

	/** Lock for this bufferevent.  Shared by the inbuf and the outbuf.
	 * If NULL, locking is disabled. */
	void *lock;

	/** No matter how big our bucket gets, don't try to read more than this
	 * much in a single read operation. */
	ev_ssize_t max_single_read;

	/** No matter how big our bucket gets, don't try to write more than this
	 * much in a single write operation. */
	ev_ssize_t max_single_write;

	/** Rate-limiting information for this bufferevent */
	struct bufferevent_rate_limit *rate_limiting;
};

/** Possible operations for a control callback. */
enum bufferevent_ctrl_op {
	BEV_CTRL_SET_FD,
	BEV_CTRL_GET_FD,
	BEV_CTRL_GET_UNDERLYING,
	BEV_CTRL_CANCEL_ALL
};

/** Possible data types for a control callback */
union bufferevent_ctrl_data {
	void *ptr;
	evutil_socket_t fd;
};

/**
   Implementation table for a bufferevent: holds function pointers and other
   information to make the various bufferevent types work.
*/
struct bufferevent_ops {
	/** The name of the bufferevent's type. */
	const char *type;
	/** At what offset into the implementation type will we find a
	    bufferevent structure?

	    Example: if the type is implemented as
	    struct bufferevent_x {
	       int extra_data;
	       struct bufferevent bev;
	    }
	    then mem_offset should be offsetof(struct bufferevent_x, bev)
	*/
	off_t mem_offset;

	/** Enables one or more of EV_READ|EV_WRITE on a bufferevent.  Does
	    not need to adjust the 'enabled' field.  Returns 0 on success, -1
	    on failure.
	 */
	int (*enable)(struct bufferevent *, short);

	/** Disables one or more of EV_READ|EV_WRITE on a bufferevent.  Does
	    not need to adjust the 'enabled' field.  Returns 0 on success, -1
	    on failure.
	 */
	int (*disable)(struct bufferevent *, short);

	/** Detatches the bufferevent from related data structures. Called as
	 * soon as its reference count reaches 0. */
	void (*unlink)(struct bufferevent *);

	/** Free any storage and deallocate any extra data or structures used
	    in this implementation. Called when the bufferevent is
	    finalized.
	 */
	void (*destruct)(struct bufferevent *);

	/** Called when the timeouts on the bufferevent have changed.*/
	int (*adj_timeouts)(struct bufferevent *);

	/** Called to flush data. */
	int (*flush)(struct bufferevent *, short, enum bufferevent_flush_mode);

	/** Called to access miscellaneous fields. */
	int (*ctrl)(struct bufferevent *, enum bufferevent_ctrl_op, union bufferevent_ctrl_data *);

};

extern const struct bufferevent_ops bufferevent_ops_socket;
extern const struct bufferevent_ops bufferevent_ops_filter;
extern const struct bufferevent_ops bufferevent_ops_pair;

#define BEV_IS_SOCKET(bevp) ((bevp)->be_ops == &bufferevent_ops_socket)
#define BEV_IS_FILTER(bevp) ((bevp)->be_ops == &bufferevent_ops_filter)
#define BEV_IS_PAIR(bevp) ((bevp)->be_ops == &bufferevent_ops_pair)

#ifdef _WIN32
extern const struct bufferevent_ops bufferevent_ops_async;
#define BEV_IS_ASYNC(bevp) ((bevp)->be_ops == &bufferevent_ops_async)
#else
#define BEV_IS_ASYNC(bevp) 0
#endif

/** Initialize the shared parts of a bufferevent. */
int bufferevent_init_common_(struct bufferevent_private *, struct event_base *, const struct bufferevent_ops *, enum bufferevent_options options);

/** For internal use: temporarily stop all reads on bufev, until the conditions
 * in 'what' are over. */
void bufferevent_suspend_read_(struct bufferevent *bufev, bufferevent_suspend_flags what);
/** For internal use: clear the conditions 'what' on bufev, and re-enable
 * reading if there are no conditions left. */
void bufferevent_unsuspend_read_(struct bufferevent *bufev, bufferevent_suspend_flags what);

/** For internal use: temporarily stop all writes on bufev, until the conditions
 * in 'what' are over. */
void bufferevent_suspend_write_(struct bufferevent *bufev, bufferevent_suspend_flags what);
/** For internal use: clear the conditions 'what' on bufev, and re-enable
 * writing if there are no conditions left. */
void bufferevent_unsuspend_write_(struct bufferevent *bufev, bufferevent_suspend_flags what);

#define bufferevent_wm_suspend_read(b) \
	bufferevent_suspend_read_((b), BEV_SUSPEND_WM)
#define bufferevent_wm_unsuspend_read(b) \
	bufferevent_unsuspend_read_((b), BEV_SUSPEND_WM)

/*
  Disable a bufferevent.  Equivalent to bufferevent_disable(), but
  first resets 'connecting' flag to force EV_WRITE down for sure.

  XXXX this method will go away in the future; try not to add new users.
    See comment in evhttp_connection_reset_() for discussion.

  @param bufev the bufferevent to be disabled
  @param event any combination of EV_READ | EV_WRITE.
  @return 0 if successful, or -1 if an error occurred
  @see bufferevent_disable()
 */
int bufferevent_disable_hard_(struct bufferevent *bufev, short event);

/** Internal: Set up locking on a bufferevent.  If lock is set, use it.
 * Otherwise, use a new lock. */
int bufferevent_enable_locking_(struct bufferevent *bufev, void *lock);
/** Internal: Increment the reference count on bufev. */
void bufferevent_incref_(struct bufferevent *bufev);
/** Internal: Lock bufev and increase its reference count.
 * unlocking it otherwise. */
void bufferevent_incref_and_lock_(struct bufferevent *bufev);
/** Internal: Decrement the reference count on bufev.  Returns 1 if it freed
 * the bufferevent.*/
int bufferevent_decref_(struct bufferevent *bufev);
/** Internal: Drop the reference count on bufev, freeing as necessary, and
 * unlocking it otherwise.  Returns 1 if it freed the bufferevent. */
int bufferevent_decref_and_unlock_(struct bufferevent *bufev);

/** Internal: If callbacks are deferred and we have a read callback, schedule
 * a readcb.  Otherwise just run the readcb. Ignores watermarks. */
void bufferevent_run_readcb_(struct bufferevent *bufev, int options);
/** Internal: If callbacks are deferred and we have a write callback, schedule
 * a writecb.  Otherwise just run the writecb. Ignores watermarks. */
void bufferevent_run_writecb_(struct bufferevent *bufev, int options);
/** Internal: If callbacks are deferred and we have an eventcb, schedule
 * it to run with events "what".  Otherwise just run the eventcb.
 * See bufferevent_trigger_event for meaning of "options". */
void bufferevent_run_eventcb_(struct bufferevent *bufev, short what, int options);

/** Internal: Run or schedule (if deferred or options contain
 * BEV_TRIG_DEFER_CALLBACKS) I/O callbacks specified in iotype.
 * Must already hold the bufev lock. Honors watermarks unless
 * BEV_TRIG_IGNORE_WATERMARKS is in options. */
static inline void bufferevent_trigger_nolock_(struct bufferevent *bufev, short iotype, int options);

/* Making this inline since all of the common-case calls to this function in
 * libevent use constant arguments. */
static inline void
bufferevent_trigger_nolock_(struct bufferevent *bufev, short iotype, int options)
{
	if ((iotype & EV_READ) && ((options & BEV_TRIG_IGNORE_WATERMARKS) ||
	    evbuffer_get_length(bufev->input) >= bufev->wm_read.low))
		bufferevent_run_readcb_(bufev, options);
	if ((iotype & EV_WRITE) && ((options & BEV_TRIG_IGNORE_WATERMARKS) ||
	    evbuffer_get_length(bufev->output) <= bufev->wm_write.low))
		bufferevent_run_writecb_(bufev, options);
}

/** Internal: Add the event 'ev' with timeout tv, unless tv is set to 0, in
 * which case add ev with no timeout. */
int bufferevent_add_event_(struct event *ev, const struct timeval *tv);

/* =========
 * These next functions implement timeouts for bufferevents that aren't doing
 * anything else with ev_read and ev_write, to handle timeouts.
 * ========= */
/** Internal use: Set up the ev_read and ev_write callbacks so that
 * the other "generic_timeout" functions will work on it.  Call this from
 * the constructor function. */
void bufferevent_init_generic_timeout_cbs_(struct bufferevent *bev);
/** Internal use: Add or delete the generic timeout events as appropriate.
 * (If an event is enabled and a timeout is set, we add the event.  Otherwise
 * we delete it.)  Call this from anything that changes the timeout values,
 * that enabled EV_READ or EV_WRITE, or that disables EV_READ or EV_WRITE. */
int bufferevent_generic_adj_timeouts_(struct bufferevent *bev);

enum bufferevent_options bufferevent_get_options_(struct bufferevent *bev);

/** Internal use: We have just successfully read data into an inbuf, so
 * reset the read timeout (if any). */
#define BEV_RESET_GENERIC_READ_TIMEOUT(bev)				\
	do {								\
		if (evutil_timerisset(&(bev)->timeout_read))		\
			event_add(&(bev)->ev_read, &(bev)->timeout_read); \
	} while (0)
/** Internal use: We have just successfully written data from an inbuf, so
 * reset the read timeout (if any). */
#define BEV_RESET_GENERIC_WRITE_TIMEOUT(bev)				\
	do {								\
		if (evutil_timerisset(&(bev)->timeout_write))		\
			event_add(&(bev)->ev_write, &(bev)->timeout_write); \
	} while (0)
#define BEV_DEL_GENERIC_READ_TIMEOUT(bev)	\
		event_del(&(bev)->ev_read)
#define BEV_DEL_GENERIC_WRITE_TIMEOUT(bev)	\
		event_del(&(bev)->ev_write)


/** Internal: Given a bufferevent, return its corresponding
 * bufferevent_private. */
#define BEV_UPCAST(b) EVUTIL_UPCAST((b), struct bufferevent_private, bev)

#ifdef EVENT__DISABLE_THREAD_SUPPORT
#define BEV_LOCK(b) EVUTIL_NIL_STMT_
#define BEV_UNLOCK(b) EVUTIL_NIL_STMT_
#else
/** Internal: Grab the lock (if any) on a bufferevent */
#define BEV_LOCK(b) do {						\
		struct bufferevent_private *locking =  BEV_UPCAST(b);	\
		EVLOCK_LOCK(locking->lock, 0);				\
	} while (0)

/** Internal: Release the lock (if any) on a bufferevent */
#define BEV_UNLOCK(b) do {						\
		struct bufferevent_private *locking =  BEV_UPCAST(b);	\
		EVLOCK_UNLOCK(locking->lock, 0);			\
	} while (0)
#endif


/* ==== For rate-limiting. */

int bufferevent_decrement_write_buckets_(struct bufferevent_private *bev,
    ev_ssize_t bytes);
int bufferevent_decrement_read_buckets_(struct bufferevent_private *bev,
    ev_ssize_t bytes);
ev_ssize_t bufferevent_get_read_max_(struct bufferevent_private *bev);
ev_ssize_t bufferevent_get_write_max_(struct bufferevent_private *bev);

int bufferevent_ratelim_init_(struct bufferevent_private *bev);

#ifdef __cplusplus
}
#endif


#endif /* BUFFEREVENT_INTERNAL_H_INCLUDED_ */
