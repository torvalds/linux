/*
 * Copyright (c) 2002-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright (c) 2007-2012 Niels Provos, Nick Mathewson
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

#include "event2/event-config.h"
#include "evconfig-private.h"

#include <sys/types.h>

#ifdef EVENT__HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef EVENT__HAVE_STDARG_H
#include <stdarg.h>
#endif

#ifdef _WIN32
#include <winsock2.h>
#endif

#include "event2/util.h"
#include "event2/buffer.h"
#include "event2/buffer_compat.h"
#include "event2/bufferevent.h"
#include "event2/bufferevent_struct.h"
#include "event2/bufferevent_compat.h"
#include "event2/event.h"
#include "event-internal.h"
#include "log-internal.h"
#include "mm-internal.h"
#include "bufferevent-internal.h"
#include "evbuffer-internal.h"
#include "util-internal.h"

static void bufferevent_cancel_all_(struct bufferevent *bev);
static void bufferevent_finalize_cb_(struct event_callback *evcb, void *arg_);

void
bufferevent_suspend_read_(struct bufferevent *bufev, bufferevent_suspend_flags what)
{
	struct bufferevent_private *bufev_private =
	    EVUTIL_UPCAST(bufev, struct bufferevent_private, bev);
	BEV_LOCK(bufev);
	if (!bufev_private->read_suspended)
		bufev->be_ops->disable(bufev, EV_READ);
	bufev_private->read_suspended |= what;
	BEV_UNLOCK(bufev);
}

void
bufferevent_unsuspend_read_(struct bufferevent *bufev, bufferevent_suspend_flags what)
{
	struct bufferevent_private *bufev_private =
	    EVUTIL_UPCAST(bufev, struct bufferevent_private, bev);
	BEV_LOCK(bufev);
	bufev_private->read_suspended &= ~what;
	if (!bufev_private->read_suspended && (bufev->enabled & EV_READ))
		bufev->be_ops->enable(bufev, EV_READ);
	BEV_UNLOCK(bufev);
}

void
bufferevent_suspend_write_(struct bufferevent *bufev, bufferevent_suspend_flags what)
{
	struct bufferevent_private *bufev_private =
	    EVUTIL_UPCAST(bufev, struct bufferevent_private, bev);
	BEV_LOCK(bufev);
	if (!bufev_private->write_suspended)
		bufev->be_ops->disable(bufev, EV_WRITE);
	bufev_private->write_suspended |= what;
	BEV_UNLOCK(bufev);
}

void
bufferevent_unsuspend_write_(struct bufferevent *bufev, bufferevent_suspend_flags what)
{
	struct bufferevent_private *bufev_private =
	    EVUTIL_UPCAST(bufev, struct bufferevent_private, bev);
	BEV_LOCK(bufev);
	bufev_private->write_suspended &= ~what;
	if (!bufev_private->write_suspended && (bufev->enabled & EV_WRITE))
		bufev->be_ops->enable(bufev, EV_WRITE);
	BEV_UNLOCK(bufev);
}


/* Callback to implement watermarks on the input buffer.  Only enabled
 * if the watermark is set. */
static void
bufferevent_inbuf_wm_cb(struct evbuffer *buf,
    const struct evbuffer_cb_info *cbinfo,
    void *arg)
{
	struct bufferevent *bufev = arg;
	size_t size;

	size = evbuffer_get_length(buf);

	if (size >= bufev->wm_read.high)
		bufferevent_wm_suspend_read(bufev);
	else
		bufferevent_wm_unsuspend_read(bufev);
}

static void
bufferevent_run_deferred_callbacks_locked(struct event_callback *cb, void *arg)
{
	struct bufferevent_private *bufev_private = arg;
	struct bufferevent *bufev = &bufev_private->bev;

	BEV_LOCK(bufev);
	if ((bufev_private->eventcb_pending & BEV_EVENT_CONNECTED) &&
	    bufev->errorcb) {
		/* The "connected" happened before any reads or writes, so
		   send it first. */
		bufev_private->eventcb_pending &= ~BEV_EVENT_CONNECTED;
		bufev->errorcb(bufev, BEV_EVENT_CONNECTED, bufev->cbarg);
	}
	if (bufev_private->readcb_pending && bufev->readcb) {
		bufev_private->readcb_pending = 0;
		bufev->readcb(bufev, bufev->cbarg);
	}
	if (bufev_private->writecb_pending && bufev->writecb) {
		bufev_private->writecb_pending = 0;
		bufev->writecb(bufev, bufev->cbarg);
	}
	if (bufev_private->eventcb_pending && bufev->errorcb) {
		short what = bufev_private->eventcb_pending;
		int err = bufev_private->errno_pending;
		bufev_private->eventcb_pending = 0;
		bufev_private->errno_pending = 0;
		EVUTIL_SET_SOCKET_ERROR(err);
		bufev->errorcb(bufev, what, bufev->cbarg);
	}
	bufferevent_decref_and_unlock_(bufev);
}

static void
bufferevent_run_deferred_callbacks_unlocked(struct event_callback *cb, void *arg)
{
	struct bufferevent_private *bufev_private = arg;
	struct bufferevent *bufev = &bufev_private->bev;

	BEV_LOCK(bufev);
#define UNLOCKED(stmt) \
	do { BEV_UNLOCK(bufev); stmt; BEV_LOCK(bufev); } while(0)

	if ((bufev_private->eventcb_pending & BEV_EVENT_CONNECTED) &&
	    bufev->errorcb) {
		/* The "connected" happened before any reads or writes, so
		   send it first. */
		bufferevent_event_cb errorcb = bufev->errorcb;
		void *cbarg = bufev->cbarg;
		bufev_private->eventcb_pending &= ~BEV_EVENT_CONNECTED;
		UNLOCKED(errorcb(bufev, BEV_EVENT_CONNECTED, cbarg));
	}
	if (bufev_private->readcb_pending && bufev->readcb) {
		bufferevent_data_cb readcb = bufev->readcb;
		void *cbarg = bufev->cbarg;
		bufev_private->readcb_pending = 0;
		UNLOCKED(readcb(bufev, cbarg));
	}
	if (bufev_private->writecb_pending && bufev->writecb) {
		bufferevent_data_cb writecb = bufev->writecb;
		void *cbarg = bufev->cbarg;
		bufev_private->writecb_pending = 0;
		UNLOCKED(writecb(bufev, cbarg));
	}
	if (bufev_private->eventcb_pending && bufev->errorcb) {
		bufferevent_event_cb errorcb = bufev->errorcb;
		void *cbarg = bufev->cbarg;
		short what = bufev_private->eventcb_pending;
		int err = bufev_private->errno_pending;
		bufev_private->eventcb_pending = 0;
		bufev_private->errno_pending = 0;
		EVUTIL_SET_SOCKET_ERROR(err);
		UNLOCKED(errorcb(bufev,what,cbarg));
	}
	bufferevent_decref_and_unlock_(bufev);
#undef UNLOCKED
}

#define SCHEDULE_DEFERRED(bevp)						\
	do {								\
		if (event_deferred_cb_schedule_(			\
			    (bevp)->bev.ev_base,			\
			&(bevp)->deferred))				\
			bufferevent_incref_(&(bevp)->bev);		\
	} while (0)


void
bufferevent_run_readcb_(struct bufferevent *bufev, int options)
{
	/* Requires that we hold the lock and a reference */
	struct bufferevent_private *p =
	    EVUTIL_UPCAST(bufev, struct bufferevent_private, bev);
	if (bufev->readcb == NULL)
		return;
	if ((p->options|options) & BEV_OPT_DEFER_CALLBACKS) {
		p->readcb_pending = 1;
		SCHEDULE_DEFERRED(p);
	} else {
		bufev->readcb(bufev, bufev->cbarg);
	}
}

void
bufferevent_run_writecb_(struct bufferevent *bufev, int options)
{
	/* Requires that we hold the lock and a reference */
	struct bufferevent_private *p =
	    EVUTIL_UPCAST(bufev, struct bufferevent_private, bev);
	if (bufev->writecb == NULL)
		return;
	if ((p->options|options) & BEV_OPT_DEFER_CALLBACKS) {
		p->writecb_pending = 1;
		SCHEDULE_DEFERRED(p);
	} else {
		bufev->writecb(bufev, bufev->cbarg);
	}
}

#define BEV_TRIG_ALL_OPTS (			\
		BEV_TRIG_IGNORE_WATERMARKS|	\
		BEV_TRIG_DEFER_CALLBACKS	\
	)

void
bufferevent_trigger(struct bufferevent *bufev, short iotype, int options)
{
	bufferevent_incref_and_lock_(bufev);
	bufferevent_trigger_nolock_(bufev, iotype, options&BEV_TRIG_ALL_OPTS);
	bufferevent_decref_and_unlock_(bufev);
}

void
bufferevent_run_eventcb_(struct bufferevent *bufev, short what, int options)
{
	/* Requires that we hold the lock and a reference */
	struct bufferevent_private *p =
	    EVUTIL_UPCAST(bufev, struct bufferevent_private, bev);
	if (bufev->errorcb == NULL)
		return;
	if ((p->options|options) & BEV_OPT_DEFER_CALLBACKS) {
		p->eventcb_pending |= what;
		p->errno_pending = EVUTIL_SOCKET_ERROR();
		SCHEDULE_DEFERRED(p);
	} else {
		bufev->errorcb(bufev, what, bufev->cbarg);
	}
}

void
bufferevent_trigger_event(struct bufferevent *bufev, short what, int options)
{
	bufferevent_incref_and_lock_(bufev);
	bufferevent_run_eventcb_(bufev, what, options&BEV_TRIG_ALL_OPTS);
	bufferevent_decref_and_unlock_(bufev);
}

int
bufferevent_init_common_(struct bufferevent_private *bufev_private,
    struct event_base *base,
    const struct bufferevent_ops *ops,
    enum bufferevent_options options)
{
	struct bufferevent *bufev = &bufev_private->bev;

	if (!bufev->input) {
		if ((bufev->input = evbuffer_new()) == NULL)
			return -1;
	}

	if (!bufev->output) {
		if ((bufev->output = evbuffer_new()) == NULL) {
			evbuffer_free(bufev->input);
			return -1;
		}
	}

	bufev_private->refcnt = 1;
	bufev->ev_base = base;

	/* Disable timeouts. */
	evutil_timerclear(&bufev->timeout_read);
	evutil_timerclear(&bufev->timeout_write);

	bufev->be_ops = ops;

	bufferevent_ratelim_init_(bufev_private);

	/*
	 * Set to EV_WRITE so that using bufferevent_write is going to
	 * trigger a callback.  Reading needs to be explicitly enabled
	 * because otherwise no data will be available.
	 */
	bufev->enabled = EV_WRITE;

#ifndef EVENT__DISABLE_THREAD_SUPPORT
	if (options & BEV_OPT_THREADSAFE) {
		if (bufferevent_enable_locking_(bufev, NULL) < 0) {
			/* cleanup */
			evbuffer_free(bufev->input);
			evbuffer_free(bufev->output);
			bufev->input = NULL;
			bufev->output = NULL;
			return -1;
		}
	}
#endif
	if ((options & (BEV_OPT_DEFER_CALLBACKS|BEV_OPT_UNLOCK_CALLBACKS))
	    == BEV_OPT_UNLOCK_CALLBACKS) {
		event_warnx("UNLOCK_CALLBACKS requires DEFER_CALLBACKS");
		return -1;
	}
	if (options & BEV_OPT_UNLOCK_CALLBACKS)
		event_deferred_cb_init_(
		    &bufev_private->deferred,
		    event_base_get_npriorities(base) / 2,
		    bufferevent_run_deferred_callbacks_unlocked,
		    bufev_private);
	else
		event_deferred_cb_init_(
		    &bufev_private->deferred,
		    event_base_get_npriorities(base) / 2,
		    bufferevent_run_deferred_callbacks_locked,
		    bufev_private);

	bufev_private->options = options;

	evbuffer_set_parent_(bufev->input, bufev);
	evbuffer_set_parent_(bufev->output, bufev);

	return 0;
}

void
bufferevent_setcb(struct bufferevent *bufev,
    bufferevent_data_cb readcb, bufferevent_data_cb writecb,
    bufferevent_event_cb eventcb, void *cbarg)
{
	BEV_LOCK(bufev);

	bufev->readcb = readcb;
	bufev->writecb = writecb;
	bufev->errorcb = eventcb;

	bufev->cbarg = cbarg;
	BEV_UNLOCK(bufev);
}

void
bufferevent_getcb(struct bufferevent *bufev,
    bufferevent_data_cb *readcb_ptr,
    bufferevent_data_cb *writecb_ptr,
    bufferevent_event_cb *eventcb_ptr,
    void **cbarg_ptr)
{
	BEV_LOCK(bufev);
	if (readcb_ptr)
		*readcb_ptr = bufev->readcb;
	if (writecb_ptr)
		*writecb_ptr = bufev->writecb;
	if (eventcb_ptr)
		*eventcb_ptr = bufev->errorcb;
	if (cbarg_ptr)
		*cbarg_ptr = bufev->cbarg;

	BEV_UNLOCK(bufev);
}

struct evbuffer *
bufferevent_get_input(struct bufferevent *bufev)
{
	return bufev->input;
}

struct evbuffer *
bufferevent_get_output(struct bufferevent *bufev)
{
	return bufev->output;
}

struct event_base *
bufferevent_get_base(struct bufferevent *bufev)
{
	return bufev->ev_base;
}

int
bufferevent_get_priority(const struct bufferevent *bufev)
{
	if (event_initialized(&bufev->ev_read)) {
		return event_get_priority(&bufev->ev_read);
	} else {
		return event_base_get_npriorities(bufev->ev_base) / 2;
	}
}

int
bufferevent_write(struct bufferevent *bufev, const void *data, size_t size)
{
	if (evbuffer_add(bufev->output, data, size) == -1)
		return (-1);

	return 0;
}

int
bufferevent_write_buffer(struct bufferevent *bufev, struct evbuffer *buf)
{
	if (evbuffer_add_buffer(bufev->output, buf) == -1)
		return (-1);

	return 0;
}

size_t
bufferevent_read(struct bufferevent *bufev, void *data, size_t size)
{
	return (evbuffer_remove(bufev->input, data, size));
}

int
bufferevent_read_buffer(struct bufferevent *bufev, struct evbuffer *buf)
{
	return (evbuffer_add_buffer(buf, bufev->input));
}

int
bufferevent_enable(struct bufferevent *bufev, short event)
{
	struct bufferevent_private *bufev_private =
	    EVUTIL_UPCAST(bufev, struct bufferevent_private, bev);
	short impl_events = event;
	int r = 0;

	bufferevent_incref_and_lock_(bufev);
	if (bufev_private->read_suspended)
		impl_events &= ~EV_READ;
	if (bufev_private->write_suspended)
		impl_events &= ~EV_WRITE;

	bufev->enabled |= event;

	if (impl_events && bufev->be_ops->enable(bufev, impl_events) < 0)
		r = -1;

	bufferevent_decref_and_unlock_(bufev);
	return r;
}

int
bufferevent_set_timeouts(struct bufferevent *bufev,
			 const struct timeval *tv_read,
			 const struct timeval *tv_write)
{
	int r = 0;
	BEV_LOCK(bufev);
	if (tv_read) {
		bufev->timeout_read = *tv_read;
	} else {
		evutil_timerclear(&bufev->timeout_read);
	}
	if (tv_write) {
		bufev->timeout_write = *tv_write;
	} else {
		evutil_timerclear(&bufev->timeout_write);
	}

	if (bufev->be_ops->adj_timeouts)
		r = bufev->be_ops->adj_timeouts(bufev);
	BEV_UNLOCK(bufev);

	return r;
}


/* Obsolete; use bufferevent_set_timeouts */
void
bufferevent_settimeout(struct bufferevent *bufev,
		       int timeout_read, int timeout_write)
{
	struct timeval tv_read, tv_write;
	struct timeval *ptv_read = NULL, *ptv_write = NULL;

	memset(&tv_read, 0, sizeof(tv_read));
	memset(&tv_write, 0, sizeof(tv_write));

	if (timeout_read) {
		tv_read.tv_sec = timeout_read;
		ptv_read = &tv_read;
	}
	if (timeout_write) {
		tv_write.tv_sec = timeout_write;
		ptv_write = &tv_write;
	}

	bufferevent_set_timeouts(bufev, ptv_read, ptv_write);
}


int
bufferevent_disable_hard_(struct bufferevent *bufev, short event)
{
	int r = 0;
	struct bufferevent_private *bufev_private =
	    EVUTIL_UPCAST(bufev, struct bufferevent_private, bev);

	BEV_LOCK(bufev);
	bufev->enabled &= ~event;

	bufev_private->connecting = 0;
	if (bufev->be_ops->disable(bufev, event) < 0)
		r = -1;

	BEV_UNLOCK(bufev);
	return r;
}

int
bufferevent_disable(struct bufferevent *bufev, short event)
{
	int r = 0;

	BEV_LOCK(bufev);
	bufev->enabled &= ~event;

	if (bufev->be_ops->disable(bufev, event) < 0)
		r = -1;

	BEV_UNLOCK(bufev);
	return r;
}

/*
 * Sets the water marks
 */

void
bufferevent_setwatermark(struct bufferevent *bufev, short events,
    size_t lowmark, size_t highmark)
{
	struct bufferevent_private *bufev_private =
	    EVUTIL_UPCAST(bufev, struct bufferevent_private, bev);

	BEV_LOCK(bufev);
	if (events & EV_WRITE) {
		bufev->wm_write.low = lowmark;
		bufev->wm_write.high = highmark;
	}

	if (events & EV_READ) {
		bufev->wm_read.low = lowmark;
		bufev->wm_read.high = highmark;

		if (highmark) {
			/* There is now a new high-water mark for read.
			   enable the callback if needed, and see if we should
			   suspend/bufferevent_wm_unsuspend. */

			if (bufev_private->read_watermarks_cb == NULL) {
				bufev_private->read_watermarks_cb =
				    evbuffer_add_cb(bufev->input,
						    bufferevent_inbuf_wm_cb,
						    bufev);
			}
			evbuffer_cb_set_flags(bufev->input,
				      bufev_private->read_watermarks_cb,
				      EVBUFFER_CB_ENABLED|EVBUFFER_CB_NODEFER);

			if (evbuffer_get_length(bufev->input) >= highmark)
				bufferevent_wm_suspend_read(bufev);
			else if (evbuffer_get_length(bufev->input) < highmark)
				bufferevent_wm_unsuspend_read(bufev);
		} else {
			/* There is now no high-water mark for read. */
			if (bufev_private->read_watermarks_cb)
				evbuffer_cb_clear_flags(bufev->input,
				    bufev_private->read_watermarks_cb,
				    EVBUFFER_CB_ENABLED);
			bufferevent_wm_unsuspend_read(bufev);
		}
	}
	BEV_UNLOCK(bufev);
}

int
bufferevent_getwatermark(struct bufferevent *bufev, short events,
    size_t *lowmark, size_t *highmark)
{
	if (events == EV_WRITE) {
		BEV_LOCK(bufev);
		if (lowmark)
			*lowmark = bufev->wm_write.low;
		if (highmark)
			*highmark = bufev->wm_write.high;
		BEV_UNLOCK(bufev);
		return 0;
	}

	if (events == EV_READ) {
		BEV_LOCK(bufev);
		if (lowmark)
			*lowmark = bufev->wm_read.low;
		if (highmark)
			*highmark = bufev->wm_read.high;
		BEV_UNLOCK(bufev);
		return 0;
	}
	return -1;
}

int
bufferevent_flush(struct bufferevent *bufev,
    short iotype,
    enum bufferevent_flush_mode mode)
{
	int r = -1;
	BEV_LOCK(bufev);
	if (bufev->be_ops->flush)
		r = bufev->be_ops->flush(bufev, iotype, mode);
	BEV_UNLOCK(bufev);
	return r;
}

void
bufferevent_incref_and_lock_(struct bufferevent *bufev)
{
	struct bufferevent_private *bufev_private =
	    BEV_UPCAST(bufev);
	BEV_LOCK(bufev);
	++bufev_private->refcnt;
}

#if 0
static void
bufferevent_transfer_lock_ownership_(struct bufferevent *donor,
    struct bufferevent *recipient)
{
	struct bufferevent_private *d = BEV_UPCAST(donor);
	struct bufferevent_private *r = BEV_UPCAST(recipient);
	if (d->lock != r->lock)
		return;
	if (r->own_lock)
		return;
	if (d->own_lock) {
		d->own_lock = 0;
		r->own_lock = 1;
	}
}
#endif

int
bufferevent_decref_and_unlock_(struct bufferevent *bufev)
{
	struct bufferevent_private *bufev_private =
	    EVUTIL_UPCAST(bufev, struct bufferevent_private, bev);
	int n_cbs = 0;
#define MAX_CBS 16
	struct event_callback *cbs[MAX_CBS];

	EVUTIL_ASSERT(bufev_private->refcnt > 0);

	if (--bufev_private->refcnt) {
		BEV_UNLOCK(bufev);
		return 0;
	}

	if (bufev->be_ops->unlink)
		bufev->be_ops->unlink(bufev);

	/* Okay, we're out of references. Let's finalize this once all the
	 * callbacks are done running. */
	cbs[0] = &bufev->ev_read.ev_evcallback;
	cbs[1] = &bufev->ev_write.ev_evcallback;
	cbs[2] = &bufev_private->deferred;
	n_cbs = 3;
	if (bufev_private->rate_limiting) {
		struct event *e = &bufev_private->rate_limiting->refill_bucket_event;
		if (event_initialized(e))
			cbs[n_cbs++] = &e->ev_evcallback;
	}
	n_cbs += evbuffer_get_callbacks_(bufev->input, cbs+n_cbs, MAX_CBS-n_cbs);
	n_cbs += evbuffer_get_callbacks_(bufev->output, cbs+n_cbs, MAX_CBS-n_cbs);

	event_callback_finalize_many_(bufev->ev_base, n_cbs, cbs,
	    bufferevent_finalize_cb_);

#undef MAX_CBS
	BEV_UNLOCK(bufev);

	return 1;
}

static void
bufferevent_finalize_cb_(struct event_callback *evcb, void *arg_)
{
	struct bufferevent *bufev = arg_;
	struct bufferevent *underlying;
	struct bufferevent_private *bufev_private =
	    EVUTIL_UPCAST(bufev, struct bufferevent_private, bev);

	BEV_LOCK(bufev);
	underlying = bufferevent_get_underlying(bufev);

	/* Clean up the shared info */
	if (bufev->be_ops->destruct)
		bufev->be_ops->destruct(bufev);

	/* XXX what happens if refcnt for these buffers is > 1?
	 * The buffers can share a lock with this bufferevent object,
	 * but the lock might be destroyed below. */
	/* evbuffer will free the callbacks */
	evbuffer_free(bufev->input);
	evbuffer_free(bufev->output);

	if (bufev_private->rate_limiting) {
		if (bufev_private->rate_limiting->group)
			bufferevent_remove_from_rate_limit_group_internal_(bufev,0);
		mm_free(bufev_private->rate_limiting);
		bufev_private->rate_limiting = NULL;
	}


	BEV_UNLOCK(bufev);

	if (bufev_private->own_lock)
		EVTHREAD_FREE_LOCK(bufev_private->lock,
		    EVTHREAD_LOCKTYPE_RECURSIVE);

	/* Free the actual allocated memory. */
	mm_free(((char*)bufev) - bufev->be_ops->mem_offset);

	/* Release the reference to underlying now that we no longer need the
	 * reference to it.  We wait this long mainly in case our lock is
	 * shared with underlying.
	 *
	 * The 'destruct' function will also drop a reference to underlying
	 * if BEV_OPT_CLOSE_ON_FREE is set.
	 *
	 * XXX Should we/can we just refcount evbuffer/bufferevent locks?
	 * It would probably save us some headaches.
	 */
	if (underlying)
		bufferevent_decref_(underlying);
}

int
bufferevent_decref(struct bufferevent *bufev)
{
	BEV_LOCK(bufev);
	return bufferevent_decref_and_unlock_(bufev);
}

void
bufferevent_free(struct bufferevent *bufev)
{
	BEV_LOCK(bufev);
	bufferevent_setcb(bufev, NULL, NULL, NULL, NULL);
	bufferevent_cancel_all_(bufev);
	bufferevent_decref_and_unlock_(bufev);
}

void
bufferevent_incref(struct bufferevent *bufev)
{
	struct bufferevent_private *bufev_private =
	    EVUTIL_UPCAST(bufev, struct bufferevent_private, bev);

	/* XXX: now that this function is public, we might want to
	 * - return the count from this function
	 * - create a new function to atomically grab the current refcount
	 */
	BEV_LOCK(bufev);
	++bufev_private->refcnt;
	BEV_UNLOCK(bufev);
}

int
bufferevent_enable_locking_(struct bufferevent *bufev, void *lock)
{
#ifdef EVENT__DISABLE_THREAD_SUPPORT
	return -1;
#else
	struct bufferevent *underlying;

	if (BEV_UPCAST(bufev)->lock)
		return -1;
	underlying = bufferevent_get_underlying(bufev);

	if (!lock && underlying && BEV_UPCAST(underlying)->lock) {
		lock = BEV_UPCAST(underlying)->lock;
		BEV_UPCAST(bufev)->lock = lock;
		BEV_UPCAST(bufev)->own_lock = 0;
	} else if (!lock) {
		EVTHREAD_ALLOC_LOCK(lock, EVTHREAD_LOCKTYPE_RECURSIVE);
		if (!lock)
			return -1;
		BEV_UPCAST(bufev)->lock = lock;
		BEV_UPCAST(bufev)->own_lock = 1;
	} else {
		BEV_UPCAST(bufev)->lock = lock;
		BEV_UPCAST(bufev)->own_lock = 0;
	}
	evbuffer_enable_locking(bufev->input, lock);
	evbuffer_enable_locking(bufev->output, lock);

	if (underlying && !BEV_UPCAST(underlying)->lock)
		bufferevent_enable_locking_(underlying, lock);

	return 0;
#endif
}

int
bufferevent_setfd(struct bufferevent *bev, evutil_socket_t fd)
{
	union bufferevent_ctrl_data d;
	int res = -1;
	d.fd = fd;
	BEV_LOCK(bev);
	if (bev->be_ops->ctrl)
		res = bev->be_ops->ctrl(bev, BEV_CTRL_SET_FD, &d);
	BEV_UNLOCK(bev);
	return res;
}

evutil_socket_t
bufferevent_getfd(struct bufferevent *bev)
{
	union bufferevent_ctrl_data d;
	int res = -1;
	d.fd = -1;
	BEV_LOCK(bev);
	if (bev->be_ops->ctrl)
		res = bev->be_ops->ctrl(bev, BEV_CTRL_GET_FD, &d);
	BEV_UNLOCK(bev);
	return (res<0) ? -1 : d.fd;
}

enum bufferevent_options
bufferevent_get_options_(struct bufferevent *bev)
{
	struct bufferevent_private *bev_p =
	    EVUTIL_UPCAST(bev, struct bufferevent_private, bev);
	enum bufferevent_options options;

	BEV_LOCK(bev);
	options = bev_p->options;
	BEV_UNLOCK(bev);
	return options;
}


static void
bufferevent_cancel_all_(struct bufferevent *bev)
{
	union bufferevent_ctrl_data d;
	memset(&d, 0, sizeof(d));
	BEV_LOCK(bev);
	if (bev->be_ops->ctrl)
		bev->be_ops->ctrl(bev, BEV_CTRL_CANCEL_ALL, &d);
	BEV_UNLOCK(bev);
}

short
bufferevent_get_enabled(struct bufferevent *bufev)
{
	short r;
	BEV_LOCK(bufev);
	r = bufev->enabled;
	BEV_UNLOCK(bufev);
	return r;
}

struct bufferevent *
bufferevent_get_underlying(struct bufferevent *bev)
{
	union bufferevent_ctrl_data d;
	int res = -1;
	d.ptr = NULL;
	BEV_LOCK(bev);
	if (bev->be_ops->ctrl)
		res = bev->be_ops->ctrl(bev, BEV_CTRL_GET_UNDERLYING, &d);
	BEV_UNLOCK(bev);
	return (res<0) ? NULL : d.ptr;
}

static void
bufferevent_generic_read_timeout_cb(evutil_socket_t fd, short event, void *ctx)
{
	struct bufferevent *bev = ctx;
	bufferevent_incref_and_lock_(bev);
	bufferevent_disable(bev, EV_READ);
	bufferevent_run_eventcb_(bev, BEV_EVENT_TIMEOUT|BEV_EVENT_READING, 0);
	bufferevent_decref_and_unlock_(bev);
}
static void
bufferevent_generic_write_timeout_cb(evutil_socket_t fd, short event, void *ctx)
{
	struct bufferevent *bev = ctx;
	bufferevent_incref_and_lock_(bev);
	bufferevent_disable(bev, EV_WRITE);
	bufferevent_run_eventcb_(bev, BEV_EVENT_TIMEOUT|BEV_EVENT_WRITING, 0);
	bufferevent_decref_and_unlock_(bev);
}

void
bufferevent_init_generic_timeout_cbs_(struct bufferevent *bev)
{
	event_assign(&bev->ev_read, bev->ev_base, -1, EV_FINALIZE,
	    bufferevent_generic_read_timeout_cb, bev);
	event_assign(&bev->ev_write, bev->ev_base, -1, EV_FINALIZE,
	    bufferevent_generic_write_timeout_cb, bev);
}

int
bufferevent_generic_adj_timeouts_(struct bufferevent *bev)
{
	const short enabled = bev->enabled;
	struct bufferevent_private *bev_p =
	    EVUTIL_UPCAST(bev, struct bufferevent_private, bev);
	int r1=0, r2=0;
	if ((enabled & EV_READ) && !bev_p->read_suspended &&
	    evutil_timerisset(&bev->timeout_read))
		r1 = event_add(&bev->ev_read, &bev->timeout_read);
	else
		r1 = event_del(&bev->ev_read);

	if ((enabled & EV_WRITE) && !bev_p->write_suspended &&
	    evutil_timerisset(&bev->timeout_write) &&
	    evbuffer_get_length(bev->output))
		r2 = event_add(&bev->ev_write, &bev->timeout_write);
	else
		r2 = event_del(&bev->ev_write);
	if (r1 < 0 || r2 < 0)
		return -1;
	return 0;
}

int
bufferevent_generic_adj_existing_timeouts_(struct bufferevent *bev)
{
	int r = 0;
	if (event_pending(&bev->ev_read, EV_READ, NULL)) {
		if (evutil_timerisset(&bev->timeout_read)) {
			    if (bufferevent_add_event_(&bev->ev_read, &bev->timeout_read) < 0)
				    r = -1;
		} else {
			event_remove_timer(&bev->ev_read);
		}
	}
	if (event_pending(&bev->ev_write, EV_WRITE, NULL)) {
		if (evutil_timerisset(&bev->timeout_write)) {
			if (bufferevent_add_event_(&bev->ev_write, &bev->timeout_write) < 0)
				r = -1;
		} else {
			event_remove_timer(&bev->ev_write);
		}
	}
	return r;
}

int
bufferevent_add_event_(struct event *ev, const struct timeval *tv)
{
	if (!evutil_timerisset(tv))
		return event_add(ev, NULL);
	else
		return event_add(ev, tv);
}

/* For use by user programs only; internally, we should be calling
   either bufferevent_incref_and_lock_(), or BEV_LOCK. */
void
bufferevent_lock(struct bufferevent *bev)
{
	bufferevent_incref_and_lock_(bev);
}

void
bufferevent_unlock(struct bufferevent *bev)
{
	bufferevent_decref_and_unlock_(bev);
}
