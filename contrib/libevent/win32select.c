/*
 * Copyright 2007-2012 Niels Provos and Nick Mathewson
 * Copyright 2000-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright 2003 Michael A. Davis <mike@datanerds.net>
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

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "event2/util.h"
#include "util-internal.h"
#include "log-internal.h"
#include "event2/event.h"
#include "event-internal.h"
#include "evmap-internal.h"
#include "event2/thread.h"
#include "evthread-internal.h"
#include "time-internal.h"

#define XFREE(ptr) do { if (ptr) mm_free(ptr); } while (0)

extern struct event_list timequeue;
extern struct event_list addqueue;

struct win_fd_set {
	unsigned int fd_count;
	SOCKET fd_array[1];
};

/* MSDN says this is required to handle SIGFPE */
volatile double SIGFPE_REQ = 0.0f;

struct idx_info {
	int read_pos_plus1;
	int write_pos_plus1;
};

struct win32op {
	unsigned num_fds_in_fd_sets;
	int resize_out_sets;
	struct win_fd_set *readset_in;
	struct win_fd_set *writeset_in;
	struct win_fd_set *readset_out;
	struct win_fd_set *writeset_out;
	struct win_fd_set *exset_out;
	unsigned signals_are_broken : 1;
};

static void *win32_init(struct event_base *);
static int win32_add(struct event_base *, evutil_socket_t, short old, short events, void *idx_);
static int win32_del(struct event_base *, evutil_socket_t, short old, short events, void *idx_);
static int win32_dispatch(struct event_base *base, struct timeval *);
static void win32_dealloc(struct event_base *);

struct eventop win32ops = {
	"win32",
	win32_init,
	win32_add,
	win32_del,
	win32_dispatch,
	win32_dealloc,
	0, /* doesn't need reinit */
	0, /* No features supported. */
	sizeof(struct idx_info),
};

#define FD_SET_ALLOC_SIZE(n) ((sizeof(struct win_fd_set) + ((n)-1)*sizeof(SOCKET)))

static int
grow_fd_sets(struct win32op *op, unsigned new_num_fds)
{
	size_t size;

	EVUTIL_ASSERT(new_num_fds >= op->readset_in->fd_count &&
	       new_num_fds >= op->writeset_in->fd_count);
	EVUTIL_ASSERT(new_num_fds >= 1);

	size = FD_SET_ALLOC_SIZE(new_num_fds);
	if (!(op->readset_in = mm_realloc(op->readset_in, size)))
		return (-1);
	if (!(op->writeset_in = mm_realloc(op->writeset_in, size)))
		return (-1);
	op->resize_out_sets = 1;
	op->num_fds_in_fd_sets = new_num_fds;
	return (0);
}

static int
do_fd_set(struct win32op *op, struct idx_info *ent, evutil_socket_t s, int read)
{
	struct win_fd_set *set = read ? op->readset_in : op->writeset_in;
	if (read) {
		if (ent->read_pos_plus1 > 0)
			return (0);
	} else {
		if (ent->write_pos_plus1 > 0)
			return (0);
	}
	if (set->fd_count == op->num_fds_in_fd_sets) {
		if (grow_fd_sets(op, op->num_fds_in_fd_sets*2))
			return (-1);
		/* set pointer will have changed and needs reiniting! */
		set = read ? op->readset_in : op->writeset_in;
	}
	set->fd_array[set->fd_count] = s;
	if (read)
		ent->read_pos_plus1 = set->fd_count+1;
	else
		ent->write_pos_plus1 = set->fd_count+1;
	return (set->fd_count++);
}

static int
do_fd_clear(struct event_base *base,
			struct win32op *op, struct idx_info *ent, int read)
{
	int i;
	struct win_fd_set *set = read ? op->readset_in : op->writeset_in;
	if (read) {
		i = ent->read_pos_plus1 - 1;
		ent->read_pos_plus1 = 0;
	} else {
		i = ent->write_pos_plus1 - 1;
		ent->write_pos_plus1 = 0;
	}
	if (i < 0)
		return (0);
	if (--set->fd_count != (unsigned)i) {
		struct idx_info *ent2;
		SOCKET s2;
		s2 = set->fd_array[i] = set->fd_array[set->fd_count];

		ent2 = evmap_io_get_fdinfo_(&base->io, s2);

		if (!ent2) /* This indicates a bug. */
			return (0);
		if (read)
			ent2->read_pos_plus1 = i+1;
		else
			ent2->write_pos_plus1 = i+1;
	}
	return (0);
}

#define NEVENT 32
void *
win32_init(struct event_base *base)
{
	struct win32op *winop;
	size_t size;
	if (!(winop = mm_calloc(1, sizeof(struct win32op))))
		return NULL;
	winop->num_fds_in_fd_sets = NEVENT;
	size = FD_SET_ALLOC_SIZE(NEVENT);
	if (!(winop->readset_in = mm_malloc(size)))
		goto err;
	if (!(winop->writeset_in = mm_malloc(size)))
		goto err;
	if (!(winop->readset_out = mm_malloc(size)))
		goto err;
	if (!(winop->writeset_out = mm_malloc(size)))
		goto err;
	if (!(winop->exset_out = mm_malloc(size)))
		goto err;
	winop->readset_in->fd_count = winop->writeset_in->fd_count = 0;
	winop->readset_out->fd_count = winop->writeset_out->fd_count
		= winop->exset_out->fd_count = 0;

	if (evsig_init_(base) < 0)
		winop->signals_are_broken = 1;

	evutil_weakrand_seed_(&base->weakrand_seed, 0);

	return (winop);
 err:
	XFREE(winop->readset_in);
	XFREE(winop->writeset_in);
	XFREE(winop->readset_out);
	XFREE(winop->writeset_out);
	XFREE(winop->exset_out);
	XFREE(winop);
	return (NULL);
}

int
win32_add(struct event_base *base, evutil_socket_t fd,
			 short old, short events, void *idx_)
{
	struct win32op *win32op = base->evbase;
	struct idx_info *idx = idx_;

	if ((events & EV_SIGNAL) && win32op->signals_are_broken)
		return (-1);

	if (!(events & (EV_READ|EV_WRITE)))
		return (0);

	event_debug(("%s: adding event for %d", __func__, (int)fd));
	if (events & EV_READ) {
		if (do_fd_set(win32op, idx, fd, 1)<0)
			return (-1);
	}
	if (events & EV_WRITE) {
		if (do_fd_set(win32op, idx, fd, 0)<0)
			return (-1);
	}
	return (0);
}

int
win32_del(struct event_base *base, evutil_socket_t fd, short old, short events,
		  void *idx_)
{
	struct win32op *win32op = base->evbase;
	struct idx_info *idx = idx_;

	event_debug(("%s: Removing event for "EV_SOCK_FMT,
		__func__, EV_SOCK_ARG(fd)));
	if (events & EV_READ)
		do_fd_clear(base, win32op, idx, 1);
	if (events & EV_WRITE)
		do_fd_clear(base, win32op, idx, 0);

	return 0;
}

static void
fd_set_copy(struct win_fd_set *out, const struct win_fd_set *in)
{
	out->fd_count = in->fd_count;
	memcpy(out->fd_array, in->fd_array, in->fd_count * (sizeof(SOCKET)));
}

/*
  static void dump_fd_set(struct win_fd_set *s)
  {
  unsigned int i;
  printf("[ ");
  for(i=0;i<s->fd_count;++i)
  printf("%d ",(int)s->fd_array[i]);
  printf("]\n");
  }
*/

int
win32_dispatch(struct event_base *base, struct timeval *tv)
{
	struct win32op *win32op = base->evbase;
	int res = 0;
	unsigned j, i;
	int fd_count;
	SOCKET s;

	if (win32op->resize_out_sets) {
		size_t size = FD_SET_ALLOC_SIZE(win32op->num_fds_in_fd_sets);
		if (!(win32op->readset_out = mm_realloc(win32op->readset_out, size)))
			return (-1);
		if (!(win32op->exset_out = mm_realloc(win32op->exset_out, size)))
			return (-1);
		if (!(win32op->writeset_out = mm_realloc(win32op->writeset_out, size)))
			return (-1);
		win32op->resize_out_sets = 0;
	}

	fd_set_copy(win32op->readset_out, win32op->readset_in);
	fd_set_copy(win32op->exset_out, win32op->writeset_in);
	fd_set_copy(win32op->writeset_out, win32op->writeset_in);

	fd_count =
	    (win32op->readset_out->fd_count > win32op->writeset_out->fd_count) ?
	    win32op->readset_out->fd_count : win32op->writeset_out->fd_count;

	if (!fd_count) {
		long msec = tv ? evutil_tv_to_msec_(tv) : LONG_MAX;
		/* Sleep's DWORD argument is unsigned long */
		if (msec < 0)
			msec = LONG_MAX;
		/* Windows doesn't like you to call select() with no sockets */
		Sleep(msec);
		return (0);
	}

	EVBASE_RELEASE_LOCK(base, th_base_lock);

	res = select(fd_count,
		     (struct fd_set*)win32op->readset_out,
		     (struct fd_set*)win32op->writeset_out,
		     (struct fd_set*)win32op->exset_out, tv);

	EVBASE_ACQUIRE_LOCK(base, th_base_lock);

	event_debug(("%s: select returned %d", __func__, res));

	if (res <= 0) {
		return res;
	}

	if (win32op->readset_out->fd_count) {
		i = evutil_weakrand_range_(&base->weakrand_seed,
		    win32op->readset_out->fd_count);
		for (j=0; j<win32op->readset_out->fd_count; ++j) {
			if (++i >= win32op->readset_out->fd_count)
				i = 0;
			s = win32op->readset_out->fd_array[i];
			evmap_io_active_(base, s, EV_READ);
		}
	}
	if (win32op->exset_out->fd_count) {
		i = evutil_weakrand_range_(&base->weakrand_seed,
		    win32op->exset_out->fd_count);
		for (j=0; j<win32op->exset_out->fd_count; ++j) {
			if (++i >= win32op->exset_out->fd_count)
				i = 0;
			s = win32op->exset_out->fd_array[i];
			evmap_io_active_(base, s, EV_WRITE);
		}
	}
	if (win32op->writeset_out->fd_count) {
		SOCKET s;
		i = evutil_weakrand_range_(&base->weakrand_seed,
		    win32op->writeset_out->fd_count);
		for (j=0; j<win32op->writeset_out->fd_count; ++j) {
			if (++i >= win32op->writeset_out->fd_count)
				i = 0;
			s = win32op->writeset_out->fd_array[i];
			evmap_io_active_(base, s, EV_WRITE);
		}
	}
	return (0);
}

void
win32_dealloc(struct event_base *base)
{
	struct win32op *win32op = base->evbase;

	evsig_dealloc_(base);
	if (win32op->readset_in)
		mm_free(win32op->readset_in);
	if (win32op->writeset_in)
		mm_free(win32op->writeset_in);
	if (win32op->readset_out)
		mm_free(win32op->readset_out);
	if (win32op->writeset_out)
		mm_free(win32op->writeset_out);
	if (win32op->exset_out)
		mm_free(win32op->exset_out);
	/* XXXXX free the tree. */

	memset(win32op, 0, sizeof(*win32op));
	mm_free(win32op);
}

#endif
