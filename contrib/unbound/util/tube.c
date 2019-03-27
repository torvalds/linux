/*
 * util/tube.c - pipe service
 *
 * Copyright (c) 2008, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains pipe service functions.
 */
#include "config.h"
#include "util/tube.h"
#include "util/log.h"
#include "util/net_help.h"
#include "util/netevent.h"
#include "util/fptr_wlist.h"
#include "util/ub_event.h"

#ifndef USE_WINSOCK
/* on unix */

#ifndef HAVE_SOCKETPAIR
/** no socketpair() available, like on Minix 3.1.7, use pipe */
#define socketpair(f, t, p, sv) pipe(sv) 
#endif /* HAVE_SOCKETPAIR */

struct tube* tube_create(void)
{
	struct tube* tube = (struct tube*)calloc(1, sizeof(*tube));
	int sv[2];
	if(!tube) {
		int err = errno;
		log_err("tube_create: out of memory");
		errno = err;
		return NULL;
	}
	tube->sr = -1;
	tube->sw = -1;
	if(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
		int err = errno;
		log_err("socketpair: %s", strerror(errno));
		free(tube);
		errno = err;
		return NULL;
	}
	tube->sr = sv[0];
	tube->sw = sv[1];
	if(!fd_set_nonblock(tube->sr) || !fd_set_nonblock(tube->sw)) {
		int err = errno;
		log_err("tube: cannot set nonblocking");
		tube_delete(tube);
		errno = err;
		return NULL;
	}
	return tube;
}

void tube_delete(struct tube* tube)
{
	if(!tube) return;
	tube_remove_bg_listen(tube);
	tube_remove_bg_write(tube);
	/* close fds after deleting commpoints, to be sure.
	 *            Also epoll does not like closing fd before event_del */
	tube_close_read(tube);
	tube_close_write(tube);
	free(tube);
}

void tube_close_read(struct tube* tube)
{
	if(tube->sr != -1) {
		close(tube->sr);
		tube->sr = -1;
	}
}

void tube_close_write(struct tube* tube)
{
	if(tube->sw != -1) {
		close(tube->sw);
		tube->sw = -1;
	}
}

void tube_remove_bg_listen(struct tube* tube)
{
	if(tube->listen_com) {
		comm_point_delete(tube->listen_com);
		tube->listen_com = NULL;
	}
	free(tube->cmd_msg);
	tube->cmd_msg = NULL;
}

void tube_remove_bg_write(struct tube* tube)
{
	if(tube->res_com) {
		comm_point_delete(tube->res_com);
		tube->res_com = NULL;
	}
	if(tube->res_list) {
		struct tube_res_list* np, *p = tube->res_list;
		tube->res_list = NULL;
		tube->res_last = NULL;
		while(p) {
			np = p->next;
			free(p->buf);
			free(p);
			p = np;
		}
	}
}

int
tube_handle_listen(struct comm_point* c, void* arg, int error,
        struct comm_reply* ATTR_UNUSED(reply_info))
{
	struct tube* tube = (struct tube*)arg;
	ssize_t r;
	if(error != NETEVENT_NOERROR) {
		fptr_ok(fptr_whitelist_tube_listen(tube->listen_cb));
		(*tube->listen_cb)(tube, NULL, 0, error, tube->listen_arg);
		return 0;
	}

	if(tube->cmd_read < sizeof(tube->cmd_len)) {
		/* complete reading the length of control msg */
		r = read(c->fd, ((uint8_t*)&tube->cmd_len) + tube->cmd_read,
			sizeof(tube->cmd_len) - tube->cmd_read);
		if(r==0) {
			/* error has happened or */
			/* parent closed pipe, must have exited somehow */
			fptr_ok(fptr_whitelist_tube_listen(tube->listen_cb));
			(*tube->listen_cb)(tube, NULL, 0, NETEVENT_CLOSED, 
				tube->listen_arg);
			return 0;
		}
		if(r==-1) {
			if(errno != EAGAIN && errno != EINTR) {
				log_err("rpipe error: %s", strerror(errno));
			}
			/* nothing to read now, try later */
			return 0;
		}
		tube->cmd_read += r;
		if(tube->cmd_read < sizeof(tube->cmd_len)) {
			/* not complete, try later */
			return 0;
		}
		tube->cmd_msg = (uint8_t*)calloc(1, tube->cmd_len);
		if(!tube->cmd_msg) {
			log_err("malloc failure");
			tube->cmd_read = 0;
			return 0;
		}
	}
	/* cmd_len has been read, read remainder */
	r = read(c->fd, tube->cmd_msg+tube->cmd_read-sizeof(tube->cmd_len),
		tube->cmd_len - (tube->cmd_read - sizeof(tube->cmd_len)));
	if(r==0) {
		/* error has happened or */
		/* parent closed pipe, must have exited somehow */
		fptr_ok(fptr_whitelist_tube_listen(tube->listen_cb));
		(*tube->listen_cb)(tube, NULL, 0, NETEVENT_CLOSED, 
			tube->listen_arg);
		return 0;
	}
	if(r==-1) {
		/* nothing to read now, try later */
		if(errno != EAGAIN && errno != EINTR) {
			log_err("rpipe error: %s", strerror(errno));
		}
		return 0;
	}
	tube->cmd_read += r;
	if(tube->cmd_read < sizeof(tube->cmd_len) + tube->cmd_len) {
		/* not complete, try later */
		return 0;
	}
	tube->cmd_read = 0;

	fptr_ok(fptr_whitelist_tube_listen(tube->listen_cb));
	(*tube->listen_cb)(tube, tube->cmd_msg, tube->cmd_len, 
		NETEVENT_NOERROR, tube->listen_arg);
		/* also frees the buf */
	tube->cmd_msg = NULL;
	return 0;
}

int
tube_handle_write(struct comm_point* c, void* arg, int error,
        struct comm_reply* ATTR_UNUSED(reply_info))
{
	struct tube* tube = (struct tube*)arg;
	struct tube_res_list* item = tube->res_list;
	ssize_t r;
	if(error != NETEVENT_NOERROR) {
		log_err("tube_handle_write net error %d", error);
		return 0;
	}

	if(!item) {
		comm_point_stop_listening(c);
		return 0;
	}

	if(tube->res_write < sizeof(item->len)) {
		r = write(c->fd, ((uint8_t*)&item->len) + tube->res_write,
			sizeof(item->len) - tube->res_write);
		if(r == -1) {
			if(errno != EAGAIN && errno != EINTR) {
				log_err("wpipe error: %s", strerror(errno));
			}
			return 0; /* try again later */
		}
		if(r == 0) {
			/* error on pipe, must have exited somehow */
			/* cannot signal this to pipe user */
			return 0;
		}
		tube->res_write += r;
		if(tube->res_write < sizeof(item->len))
			return 0;
	}
	r = write(c->fd, item->buf + tube->res_write - sizeof(item->len),
		item->len - (tube->res_write - sizeof(item->len)));
	if(r == -1) {
		if(errno != EAGAIN && errno != EINTR) {
			log_err("wpipe error: %s", strerror(errno));
		}
		return 0; /* try again later */
	}
	if(r == 0) {
		/* error on pipe, must have exited somehow */
		/* cannot signal this to pipe user */
		return 0;
	}
	tube->res_write += r;
	if(tube->res_write < sizeof(item->len) + item->len)
		return 0;
	/* done this result, remove it */
	free(item->buf);
	item->buf = NULL;
	tube->res_list = tube->res_list->next;
	free(item);
	if(!tube->res_list) {
		tube->res_last = NULL;
		comm_point_stop_listening(c);
	}
	tube->res_write = 0;
	return 0;
}

int tube_write_msg(struct tube* tube, uint8_t* buf, uint32_t len, 
        int nonblock)
{
	ssize_t r, d;
	int fd = tube->sw;

	/* test */
	if(nonblock) {
		r = write(fd, &len, sizeof(len));
		if(r == -1) {
			if(errno==EINTR || errno==EAGAIN)
				return -1;
			log_err("tube msg write failed: %s", strerror(errno));
			return -1; /* can still continue, perhaps */
		}
	} else r = 0;
	if(!fd_set_block(fd))
		return 0;
	/* write remainder */
	d = r;
	while(d != (ssize_t)sizeof(len)) {
		if((r=write(fd, ((char*)&len)+d, sizeof(len)-d)) == -1) {
			if(errno == EAGAIN)
				continue; /* temporarily unavail: try again*/
			log_err("tube msg write failed: %s", strerror(errno));
			(void)fd_set_nonblock(fd);
			return 0;
		}
		d += r;
	}
	d = 0;
	while(d != (ssize_t)len) {
		if((r=write(fd, buf+d, len-d)) == -1) {
			if(errno == EAGAIN)
				continue; /* temporarily unavail: try again*/
			log_err("tube msg write failed: %s", strerror(errno));
			(void)fd_set_nonblock(fd);
			return 0;
		}
		d += r;
	}
	if(!fd_set_nonblock(fd))
		return 0;
	return 1;
}

int tube_read_msg(struct tube* tube, uint8_t** buf, uint32_t* len, 
        int nonblock)
{
	ssize_t r, d;
	int fd = tube->sr;

	/* test */
	*len = 0;
	if(nonblock) {
		r = read(fd, len, sizeof(*len));
		if(r == -1) {
			if(errno==EINTR || errno==EAGAIN)
				return -1;
			log_err("tube msg read failed: %s", strerror(errno));
			return -1; /* we can still continue, perhaps */
		}
		if(r == 0) /* EOF */
			return 0;
	} else r = 0;
	if(!fd_set_block(fd))
		return 0;
	/* read remainder */
	d = r;
	while(d != (ssize_t)sizeof(*len)) {
		if((r=read(fd, ((char*)len)+d, sizeof(*len)-d)) == -1) {
			log_err("tube msg read failed: %s", strerror(errno));
			(void)fd_set_nonblock(fd);
			return 0;
		}
		if(r == 0) /* EOF */ {
			(void)fd_set_nonblock(fd);
			return 0;
		}
		d += r;
	}
	log_assert(*len < 65536*2);
	*buf = (uint8_t*)malloc(*len);
	if(!*buf) {
		log_err("tube read out of memory");
		(void)fd_set_nonblock(fd);
		return 0;
	}
	d = 0;
	while(d < (ssize_t)*len) {
		if((r=read(fd, (*buf)+d, (size_t)((ssize_t)*len)-d)) == -1) {
			log_err("tube msg read failed: %s", strerror(errno));
			(void)fd_set_nonblock(fd);
			free(*buf);
			return 0;
		}
		if(r == 0) { /* EOF */
			(void)fd_set_nonblock(fd);
			free(*buf);
			return 0;
		}
		d += r;
	}
	if(!fd_set_nonblock(fd)) {
		free(*buf);
		return 0;
	}
	return 1;
}

/** perform a select() on the fd */
static int
pollit(int fd, struct timeval* t)
{
	fd_set r;
#ifndef S_SPLINT_S
	FD_ZERO(&r);
	FD_SET(FD_SET_T fd, &r);
#endif
	if(select(fd+1, &r, NULL, NULL, t) == -1) {
		return 0;
	}
	errno = 0;
	return (int)(FD_ISSET(fd, &r));
}

int tube_poll(struct tube* tube)
{
	struct timeval t;
	memset(&t, 0, sizeof(t));
	return pollit(tube->sr, &t);
}

int tube_wait(struct tube* tube)
{
	return pollit(tube->sr, NULL);
}

int tube_read_fd(struct tube* tube)
{
	return tube->sr;
}

int tube_setup_bg_listen(struct tube* tube, struct comm_base* base,
        tube_callback_type* cb, void* arg)
{
	tube->listen_cb = cb;
	tube->listen_arg = arg;
	if(!(tube->listen_com = comm_point_create_raw(base, tube->sr, 
		0, tube_handle_listen, tube))) {
		int err = errno;
		log_err("tube_setup_bg_l: commpoint creation failed");
		errno = err;
		return 0;
	}
	return 1;
}

int tube_setup_bg_write(struct tube* tube, struct comm_base* base)
{
	if(!(tube->res_com = comm_point_create_raw(base, tube->sw, 
		1, tube_handle_write, tube))) {
		int err = errno;
		log_err("tube_setup_bg_w: commpoint creation failed");
		errno = err;
		return 0;
	}
	return 1;
}

int tube_queue_item(struct tube* tube, uint8_t* msg, size_t len)
{
	struct tube_res_list* item;
	if(!tube || !tube->res_com) return 0;
	item = (struct tube_res_list*)malloc(sizeof(*item));
	if(!item) {
		free(msg);
		log_err("out of memory for async answer");
		return 0;
	}
	item->buf = msg;
	item->len = len;
	item->next = NULL;
	/* add at back of list, since the first one may be partially written */
	if(tube->res_last)
		tube->res_last->next = item;
	else    tube->res_list = item;
	tube->res_last = item;
	if(tube->res_list == tube->res_last) {
		/* first added item, start the write process */
		comm_point_start_listening(tube->res_com, -1, -1);
	}
	return 1;
}

void tube_handle_signal(int ATTR_UNUSED(fd), short ATTR_UNUSED(events), 
	void* ATTR_UNUSED(arg))
{
	log_assert(0);
}

#else /* USE_WINSOCK */
/* on windows */


struct tube* tube_create(void)
{
	/* windows does not have forks like unix, so we only support
	 * threads on windows. And thus the pipe need only connect
	 * threads. We use a mutex and a list of datagrams. */
	struct tube* tube = (struct tube*)calloc(1, sizeof(*tube));
	if(!tube) {
		int err = errno;
		log_err("tube_create: out of memory");
		errno = err;
		return NULL;
	}
	tube->event = WSACreateEvent();
	if(tube->event == WSA_INVALID_EVENT) {
		free(tube);
		log_err("WSACreateEvent: %s", wsa_strerror(WSAGetLastError()));
	}
	if(!WSAResetEvent(tube->event)) {
		log_err("WSAResetEvent: %s", wsa_strerror(WSAGetLastError()));
	}
	lock_basic_init(&tube->res_lock);
	verbose(VERB_ALGO, "tube created");
	return tube;
}

void tube_delete(struct tube* tube)
{
	if(!tube) return;
	tube_remove_bg_listen(tube);
	tube_remove_bg_write(tube);
	tube_close_read(tube);
	tube_close_write(tube);
	if(!WSACloseEvent(tube->event))
		log_err("WSACloseEvent: %s", wsa_strerror(WSAGetLastError()));
	lock_basic_destroy(&tube->res_lock);
	verbose(VERB_ALGO, "tube deleted");
	free(tube);
}

void tube_close_read(struct tube* ATTR_UNUSED(tube))
{
	verbose(VERB_ALGO, "tube close_read");
}

void tube_close_write(struct tube* ATTR_UNUSED(tube))
{
	verbose(VERB_ALGO, "tube close_write");
	/* wake up waiting reader with an empty queue */
	if(!WSASetEvent(tube->event)) {
		log_err("WSASetEvent: %s", wsa_strerror(WSAGetLastError()));
	}
}

void tube_remove_bg_listen(struct tube* tube)
{
	verbose(VERB_ALGO, "tube remove_bg_listen");
	ub_winsock_unregister_wsaevent(tube->ev_listen);
}

void tube_remove_bg_write(struct tube* tube)
{
	verbose(VERB_ALGO, "tube remove_bg_write");
	if(tube->res_list) {
		struct tube_res_list* np, *p = tube->res_list;
		tube->res_list = NULL;
		tube->res_last = NULL;
		while(p) {
			np = p->next;
			free(p->buf);
			free(p);
			p = np;
		}
	}
}

int tube_write_msg(struct tube* tube, uint8_t* buf, uint32_t len, 
        int ATTR_UNUSED(nonblock))
{
	uint8_t* a;
	verbose(VERB_ALGO, "tube write_msg len %d", (int)len);
	a = (uint8_t*)memdup(buf, len);
	if(!a) {
		log_err("out of memory in tube_write_msg");
		return 0;
	}
	/* always nonblocking, this pipe cannot get full */
	return tube_queue_item(tube, a, len);
}

int tube_read_msg(struct tube* tube, uint8_t** buf, uint32_t* len, 
        int nonblock)
{
	struct tube_res_list* item = NULL;
	verbose(VERB_ALGO, "tube read_msg %s", nonblock?"nonblock":"blocking");
	*buf = NULL;
	if(!tube_poll(tube)) {
		verbose(VERB_ALGO, "tube read_msg nodata");
		/* nothing ready right now, wait if we want to */
		if(nonblock)
			return -1; /* would block waiting for items */
		if(!tube_wait(tube))
			return 0;
	}
	lock_basic_lock(&tube->res_lock);
	if(tube->res_list) {
		item = tube->res_list;
		tube->res_list = item->next;
		if(tube->res_last == item) {
			/* the list is now empty */
			tube->res_last = NULL;
			verbose(VERB_ALGO, "tube read_msg lastdata");
			if(!WSAResetEvent(tube->event)) {
				log_err("WSAResetEvent: %s", 
					wsa_strerror(WSAGetLastError()));
			}
		}
	}
	lock_basic_unlock(&tube->res_lock);
	if(!item)
		return 0; /* would block waiting for items */
	*buf = item->buf;
	*len = item->len;
	free(item);
	verbose(VERB_ALGO, "tube read_msg len %d", (int)*len);
	return 1;
}

int tube_poll(struct tube* tube)
{
	struct tube_res_list* item = NULL;
	lock_basic_lock(&tube->res_lock);
	item = tube->res_list;
	lock_basic_unlock(&tube->res_lock);
	if(item)
		return 1;
	return 0;
}

int tube_wait(struct tube* tube)
{
	/* block on eventhandle */
	DWORD res = WSAWaitForMultipleEvents(
		1 /* one event in array */, 
		&tube->event /* the event to wait for, our pipe signal */, 
		0 /* wait for all events is false */, 
		WSA_INFINITE /* wait, no timeout */,
		0 /* we are not alertable for IO completion routines */
		);
	if(res == WSA_WAIT_TIMEOUT) {
		return 0;
	}
	if(res == WSA_WAIT_IO_COMPLETION) {
		/* a bit unexpected, since we were not alertable */
		return 0;
	}
	return 1;
}

int tube_read_fd(struct tube* ATTR_UNUSED(tube))
{
	/* nothing sensible on Windows */
	return -1;
}

int
tube_handle_listen(struct comm_point* ATTR_UNUSED(c), void* ATTR_UNUSED(arg), 
	int ATTR_UNUSED(error), struct comm_reply* ATTR_UNUSED(reply_info))
{
	log_assert(0);
	return 0;
}

int
tube_handle_write(struct comm_point* ATTR_UNUSED(c), void* ATTR_UNUSED(arg), 
	int ATTR_UNUSED(error), struct comm_reply* ATTR_UNUSED(reply_info))
{
	log_assert(0);
	return 0;
}

int tube_setup_bg_listen(struct tube* tube, struct comm_base* base,
        tube_callback_type* cb, void* arg)
{
	tube->listen_cb = cb;
	tube->listen_arg = arg;
	if(!comm_base_internal(base))
		return 1; /* ignore when no comm base - testing */
	tube->ev_listen = ub_winsock_register_wsaevent(
	    comm_base_internal(base), tube->event, &tube_handle_signal, tube);
	return tube->ev_listen ? 1 : 0;
}

int tube_setup_bg_write(struct tube* ATTR_UNUSED(tube), 
	struct comm_base* ATTR_UNUSED(base))
{
	/* the queue item routine performs the signaling */
	return 1;
}

int tube_queue_item(struct tube* tube, uint8_t* msg, size_t len)
{
	struct tube_res_list* item;
	if(!tube) return 0;
	item = (struct tube_res_list*)malloc(sizeof(*item));
	verbose(VERB_ALGO, "tube queue_item len %d", (int)len);
	if(!item) {
		free(msg);
		log_err("out of memory for async answer");
		return 0;
	}
	item->buf = msg;
	item->len = len;
	item->next = NULL;
	lock_basic_lock(&tube->res_lock);
	/* add at back of list, since the first one may be partially written */
	if(tube->res_last)
		tube->res_last->next = item;
	else    tube->res_list = item;
	tube->res_last = item;
	/* signal the eventhandle */
	if(!WSASetEvent(tube->event)) {
		log_err("WSASetEvent: %s", wsa_strerror(WSAGetLastError()));
	}
	lock_basic_unlock(&tube->res_lock);
	return 1;
}

void tube_handle_signal(int ATTR_UNUSED(fd), short ATTR_UNUSED(events), 
	void* arg)
{
	struct tube* tube = (struct tube*)arg;
	uint8_t* buf;
	uint32_t len = 0;
	verbose(VERB_ALGO, "tube handle_signal");
	while(tube_poll(tube)) {
		if(tube_read_msg(tube, &buf, &len, 1)) {
			fptr_ok(fptr_whitelist_tube_listen(tube->listen_cb));
			(*tube->listen_cb)(tube, buf, len, NETEVENT_NOERROR, 
				tube->listen_arg);
		}
	}
}

#endif /* USE_WINSOCK */
