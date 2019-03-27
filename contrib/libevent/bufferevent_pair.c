/*
 * Copyright (c) 2009-2012 Niels Provos, Nick Mathewson
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

#ifdef _WIN32
#include <winsock2.h>
#endif

#include "event2/util.h"
#include "event2/buffer.h"
#include "event2/bufferevent.h"
#include "event2/bufferevent_struct.h"
#include "event2/event.h"
#include "defer-internal.h"
#include "bufferevent-internal.h"
#include "mm-internal.h"
#include "util-internal.h"

struct bufferevent_pair {
	struct bufferevent_private bev;
	struct bufferevent_pair *partner;
	/* For ->destruct() lock checking */
	struct bufferevent_pair *unlinked_partner;
};


/* Given a bufferevent that's really a bev part of a bufferevent_pair,
 * return that bufferevent_filtered. Returns NULL otherwise.*/
static inline struct bufferevent_pair *
upcast(struct bufferevent *bev)
{
	struct bufferevent_pair *bev_p;
	if (bev->be_ops != &bufferevent_ops_pair)
		return NULL;
	bev_p = EVUTIL_UPCAST(bev, struct bufferevent_pair, bev.bev);
	EVUTIL_ASSERT(bev_p->bev.bev.be_ops == &bufferevent_ops_pair);
	return bev_p;
}

#define downcast(bev_pair) (&(bev_pair)->bev.bev)

static inline void
incref_and_lock(struct bufferevent *b)
{
	struct bufferevent_pair *bevp;
	bufferevent_incref_and_lock_(b);
	bevp = upcast(b);
	if (bevp->partner)
		bufferevent_incref_and_lock_(downcast(bevp->partner));
}

static inline void
decref_and_unlock(struct bufferevent *b)
{
	struct bufferevent_pair *bevp = upcast(b);
	if (bevp->partner)
		bufferevent_decref_and_unlock_(downcast(bevp->partner));
	bufferevent_decref_and_unlock_(b);
}

/* XXX Handle close */

static void be_pair_outbuf_cb(struct evbuffer *,
    const struct evbuffer_cb_info *, void *);

static struct bufferevent_pair *
bufferevent_pair_elt_new(struct event_base *base,
    int options)
{
	struct bufferevent_pair *bufev;
	if (! (bufev = mm_calloc(1, sizeof(struct bufferevent_pair))))
		return NULL;
	if (bufferevent_init_common_(&bufev->bev, base, &bufferevent_ops_pair,
		options)) {
		mm_free(bufev);
		return NULL;
	}
	if (!evbuffer_add_cb(bufev->bev.bev.output, be_pair_outbuf_cb, bufev)) {
		bufferevent_free(downcast(bufev));
		return NULL;
	}

	bufferevent_init_generic_timeout_cbs_(&bufev->bev.bev);

	return bufev;
}

int
bufferevent_pair_new(struct event_base *base, int options,
    struct bufferevent *pair[2])
{
	struct bufferevent_pair *bufev1 = NULL, *bufev2 = NULL;
	int tmp_options;

	options |= BEV_OPT_DEFER_CALLBACKS;
	tmp_options = options & ~BEV_OPT_THREADSAFE;

	bufev1 = bufferevent_pair_elt_new(base, options);
	if (!bufev1)
		return -1;
	bufev2 = bufferevent_pair_elt_new(base, tmp_options);
	if (!bufev2) {
		bufferevent_free(downcast(bufev1));
		return -1;
	}

	if (options & BEV_OPT_THREADSAFE) {
		/*XXXX check return */
		bufferevent_enable_locking_(downcast(bufev2), bufev1->bev.lock);
	}

	bufev1->partner = bufev2;
	bufev2->partner = bufev1;

	evbuffer_freeze(downcast(bufev1)->input, 0);
	evbuffer_freeze(downcast(bufev1)->output, 1);
	evbuffer_freeze(downcast(bufev2)->input, 0);
	evbuffer_freeze(downcast(bufev2)->output, 1);

	pair[0] = downcast(bufev1);
	pair[1] = downcast(bufev2);

	return 0;
}

static void
be_pair_transfer(struct bufferevent *src, struct bufferevent *dst,
    int ignore_wm)
{
	size_t dst_size;
	size_t n;

	evbuffer_unfreeze(src->output, 1);
	evbuffer_unfreeze(dst->input, 0);

	if (dst->wm_read.high) {
		dst_size = evbuffer_get_length(dst->input);
		if (dst_size < dst->wm_read.high) {
			n = dst->wm_read.high - dst_size;
			evbuffer_remove_buffer(src->output, dst->input, n);
		} else {
			if (!ignore_wm)
				goto done;
			n = evbuffer_get_length(src->output);
			evbuffer_add_buffer(dst->input, src->output);
		}
	} else {
		n = evbuffer_get_length(src->output);
		evbuffer_add_buffer(dst->input, src->output);
	}

	if (n) {
		BEV_RESET_GENERIC_READ_TIMEOUT(dst);

		if (evbuffer_get_length(dst->output))
			BEV_RESET_GENERIC_WRITE_TIMEOUT(dst);
		else
			BEV_DEL_GENERIC_WRITE_TIMEOUT(dst);
	}

	bufferevent_trigger_nolock_(dst, EV_READ, 0);
	bufferevent_trigger_nolock_(src, EV_WRITE, 0);
done:
	evbuffer_freeze(src->output, 1);
	evbuffer_freeze(dst->input, 0);
}

static inline int
be_pair_wants_to_talk(struct bufferevent_pair *src,
    struct bufferevent_pair *dst)
{
	return (downcast(src)->enabled & EV_WRITE) &&
	    (downcast(dst)->enabled & EV_READ) &&
	    !dst->bev.read_suspended &&
	    evbuffer_get_length(downcast(src)->output);
}

static void
be_pair_outbuf_cb(struct evbuffer *outbuf,
    const struct evbuffer_cb_info *info, void *arg)
{
	struct bufferevent_pair *bev_pair = arg;
	struct bufferevent_pair *partner = bev_pair->partner;

	incref_and_lock(downcast(bev_pair));

	if (info->n_added > info->n_deleted && partner) {
		/* We got more data.  If the other side's reading, then
		   hand it over. */
		if (be_pair_wants_to_talk(bev_pair, partner)) {
			be_pair_transfer(downcast(bev_pair), downcast(partner), 0);
		}
	}

	decref_and_unlock(downcast(bev_pair));
}

static int
be_pair_enable(struct bufferevent *bufev, short events)
{
	struct bufferevent_pair *bev_p = upcast(bufev);
	struct bufferevent_pair *partner = bev_p->partner;

	incref_and_lock(bufev);

	if (events & EV_READ) {
		BEV_RESET_GENERIC_READ_TIMEOUT(bufev);
	}
	if ((events & EV_WRITE) && evbuffer_get_length(bufev->output))
		BEV_RESET_GENERIC_WRITE_TIMEOUT(bufev);

	/* We're starting to read! Does the other side have anything to write?*/
	if ((events & EV_READ) && partner &&
	    be_pair_wants_to_talk(partner, bev_p)) {
		be_pair_transfer(downcast(partner), bufev, 0);
	}
	/* We're starting to write! Does the other side want to read? */
	if ((events & EV_WRITE) && partner &&
	    be_pair_wants_to_talk(bev_p, partner)) {
		be_pair_transfer(bufev, downcast(partner), 0);
	}
	decref_and_unlock(bufev);
	return 0;
}

static int
be_pair_disable(struct bufferevent *bev, short events)
{
	if (events & EV_READ) {
		BEV_DEL_GENERIC_READ_TIMEOUT(bev);
	}
	if (events & EV_WRITE) {
		BEV_DEL_GENERIC_WRITE_TIMEOUT(bev);
	}
	return 0;
}

static void
be_pair_unlink(struct bufferevent *bev)
{
	struct bufferevent_pair *bev_p = upcast(bev);

	if (bev_p->partner) {
		bev_p->unlinked_partner = bev_p->partner;
		bev_p->partner->partner = NULL;
		bev_p->partner = NULL;
	}
}

/* Free *shared* lock in the latest be (since we share it between two of them). */
static void
be_pair_destruct(struct bufferevent *bev)
{
	struct bufferevent_pair *bev_p = upcast(bev);

	/* Transfer ownership of the lock into partner, otherwise we will use
	 * already free'd lock during freeing second bev, see next example:
	 *
	 * bev1->own_lock = 1
	 * bev2->own_lock = 0
	 * bev2->lock = bev1->lock
	 *
	 * bufferevent_free(bev1) # refcnt == 0 -> unlink
	 * bufferevent_free(bev2) # refcnt == 0 -> unlink
	 *
	 * event_base_free() -> finilizers -> EVTHREAD_FREE_LOCK(bev1->lock)
	 *                                 -> BEV_LOCK(bev2->lock) <-- already freed
	 *
	 * Where bev1 == pair[0], bev2 == pair[1].
	 */
	if (bev_p->unlinked_partner && bev_p->bev.own_lock) {
		bev_p->unlinked_partner->bev.own_lock = 1;
		bev_p->bev.own_lock = 0;
	}
	bev_p->unlinked_partner = NULL;
}

static int
be_pair_flush(struct bufferevent *bev, short iotype,
    enum bufferevent_flush_mode mode)
{
	struct bufferevent_pair *bev_p = upcast(bev);
	struct bufferevent *partner;

	if (!bev_p->partner)
		return -1;

	if (mode == BEV_NORMAL)
		return 0;

	incref_and_lock(bev);

	partner = downcast(bev_p->partner);

	if ((iotype & EV_READ) != 0)
		be_pair_transfer(partner, bev, 1);

	if ((iotype & EV_WRITE) != 0)
		be_pair_transfer(bev, partner, 1);

	if (mode == BEV_FINISHED) {
		short what = BEV_EVENT_EOF;
		if (iotype & EV_READ)
			what |= BEV_EVENT_WRITING;
		if (iotype & EV_WRITE)
			what |= BEV_EVENT_READING;
		bufferevent_run_eventcb_(partner, what, 0);
	}
	decref_and_unlock(bev);
	return 0;
}

struct bufferevent *
bufferevent_pair_get_partner(struct bufferevent *bev)
{
	struct bufferevent_pair *bev_p;
	struct bufferevent *partner = NULL;
	bev_p = upcast(bev);
	if (! bev_p)
		return NULL;

	incref_and_lock(bev);
	if (bev_p->partner)
		partner = downcast(bev_p->partner);
	decref_and_unlock(bev);
	return partner;
}

const struct bufferevent_ops bufferevent_ops_pair = {
	"pair_elt",
	evutil_offsetof(struct bufferevent_pair, bev.bev),
	be_pair_enable,
	be_pair_disable,
	be_pair_unlink,
	be_pair_destruct,
	bufferevent_generic_adj_timeouts_,
	be_pair_flush,
	NULL, /* ctrl */
};
