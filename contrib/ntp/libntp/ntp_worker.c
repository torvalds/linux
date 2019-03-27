/*
 * ntp_worker.c
 */
#include <config.h>
#include "ntp_workimpl.h"

#ifdef WORKER

#include <stdio.h>
#include <ctype.h>
#include <signal.h>

#include "iosignal.h"
#include "ntp_stdlib.h"
#include "ntp_malloc.h"
#include "ntp_syslog.h"
#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_assert.h"
#include "ntp_unixtime.h"
#include "intreswork.h"


#define CHILD_MAX_IDLE	(3 * 60)	/* seconds, idle worker limit */

blocking_child **	blocking_children;
size_t			blocking_children_alloc;
int			worker_per_query;	/* boolean */
int			intres_req_pending;
volatile u_int		blocking_child_ready_seen;
volatile u_int		blocking_child_ready_done;


#ifndef HAVE_IO_COMPLETION_PORT
/*
 * pipe_socketpair()
 *
 * Provides an AF_UNIX socketpair on systems which have them, otherwise
 * pair of unidirectional pipes.
 */
int
pipe_socketpair(
	int	caller_fds[2],
	int *	is_pipe
	)
{
	int	rc;
	int	fds[2];
	int	called_pipe;

#ifdef HAVE_SOCKETPAIR
	rc = socketpair(AF_UNIX, SOCK_STREAM, 0, &fds[0]);
#else
	rc = -1;
#endif

	if (-1 == rc) {
		rc = pipe(&fds[0]);
		called_pipe = TRUE;
	} else {
		called_pipe = FALSE;
	}

	if (-1 == rc)
		return rc;

	caller_fds[0] = fds[0];
	caller_fds[1] = fds[1];
	if (is_pipe != NULL)
		*is_pipe = called_pipe;

	return 0;
}


/*
 * close_all_except()
 *
 * Close all file descriptors except the given keep_fd.
 */
void
close_all_except(
	int keep_fd
	)
{
	int fd;

	for (fd = 0; fd < keep_fd; fd++)
		close(fd);

	close_all_beyond(keep_fd);
}


/*
 * close_all_beyond()
 *
 * Close all file descriptors after the given keep_fd, which is the
 * highest fd to keep open.
 */
void
close_all_beyond(
	int keep_fd
	)
{
# ifdef HAVE_CLOSEFROM
	closefrom(keep_fd + 1);
# elif defined(F_CLOSEM)
	/*
	 * From 'Writing Reliable AIX Daemons,' SG24-4946-00,
	 * by Eric Agar (saves us from doing 32767 system
	 * calls)
	 */
	if (fcntl(keep_fd + 1, F_CLOSEM, 0) == -1)
		msyslog(LOG_ERR, "F_CLOSEM(%d): %m", keep_fd + 1);
# else	/* !HAVE_CLOSEFROM && !F_CLOSEM follows */
	int fd;
	int max_fd;

	max_fd = GETDTABLESIZE();
	for (fd = keep_fd + 1; fd < max_fd; fd++)
		close(fd);
# endif	/* !HAVE_CLOSEFROM && !F_CLOSEM */
}
#endif	/* HAVE_IO_COMPLETION_PORT */


u_int
available_blocking_child_slot(void)
{
	const size_t	each = sizeof(blocking_children[0]);
	u_int		slot;
	size_t		prev_alloc;
	size_t		new_alloc;
	size_t		prev_octets;
	size_t		octets;

	for (slot = 0; slot < blocking_children_alloc; slot++) {
		if (NULL == blocking_children[slot])
			return slot;
		if (blocking_children[slot]->reusable) {
			blocking_children[slot]->reusable = FALSE;
			return slot;
		}
	}

	prev_alloc = blocking_children_alloc;
	prev_octets = prev_alloc * each;
	new_alloc = blocking_children_alloc + 4;
	octets = new_alloc * each;
	blocking_children = erealloc_zero(blocking_children, octets,
					  prev_octets);
	blocking_children_alloc = new_alloc;

	/* assume we'll never have enough workers to overflow u_int */
	return (u_int)prev_alloc;
}


int
queue_blocking_request(
	blocking_work_req	rtype,
	void *			req,
	size_t			reqsize,
	blocking_work_callback	done_func,
	void *			context
	)
{
	static u_int		intres_slot = UINT_MAX;
	u_int			child_slot;
	blocking_child *	c;
	blocking_pipe_header	req_hdr;

	req_hdr.octets = sizeof(req_hdr) + reqsize;
	req_hdr.magic_sig = BLOCKING_REQ_MAGIC;
	req_hdr.rtype = rtype;
	req_hdr.done_func = done_func;
	req_hdr.context = context;

	child_slot = UINT_MAX;
	if (worker_per_query || UINT_MAX == intres_slot ||
	    blocking_children[intres_slot]->reusable)
		child_slot = available_blocking_child_slot();
	if (!worker_per_query) {
		if (UINT_MAX == intres_slot)
			intres_slot = child_slot;
		else
			child_slot = intres_slot;
		if (0 == intres_req_pending)
			intres_timeout_req(0);
	}
	intres_req_pending++;
	INSIST(UINT_MAX != child_slot);
	c = blocking_children[child_slot];
	if (NULL == c) {
		c = emalloc_zero(sizeof(*c));
#ifdef WORK_FORK
		c->req_read_pipe = -1;
		c->req_write_pipe = -1;
#endif
#ifdef WORK_PIPE
		c->resp_read_pipe = -1;
		c->resp_write_pipe = -1;
#endif
		blocking_children[child_slot] = c;
	}
	req_hdr.child_idx = child_slot;

	return send_blocking_req_internal(c, &req_hdr, req);
}


int queue_blocking_response(
	blocking_child *		c,
	blocking_pipe_header *		resp,
	size_t				respsize,
	const blocking_pipe_header *	req
	)
{
	resp->octets = respsize;
	resp->magic_sig = BLOCKING_RESP_MAGIC;
	resp->rtype = req->rtype;
	resp->context = req->context;
	resp->done_func = req->done_func;

	return send_blocking_resp_internal(c, resp);
}


void
process_blocking_resp(
	blocking_child *	c
	)
{
	blocking_pipe_header *	resp;
	void *			data;

	/*
	 * On Windows send_blocking_resp_internal() may signal the
	 * blocking_response_ready event multiple times while we're
	 * processing a response, so always consume all available
	 * responses before returning to test the event again.
	 */
#ifdef WORK_THREAD
	do {
#endif
		resp = receive_blocking_resp_internal(c);
		if (NULL != resp) {
			DEBUG_REQUIRE(BLOCKING_RESP_MAGIC ==
				      resp->magic_sig);
			data = (char *)resp + sizeof(*resp);
			intres_req_pending--;
			(*resp->done_func)(resp->rtype, resp->context,
					   resp->octets - sizeof(*resp),
					   data);
			free(resp);
		}
#ifdef WORK_THREAD
	} while (NULL != resp);
#endif
	if (!worker_per_query && 0 == intres_req_pending)
		intres_timeout_req(CHILD_MAX_IDLE);
	else if (worker_per_query)
		req_child_exit(c);
}

void
harvest_blocking_responses(void)
{
	size_t		idx;
	blocking_child*	cp;
	u_int		scseen, scdone;

	scseen = blocking_child_ready_seen;
	scdone = blocking_child_ready_done;
	if (scdone != scseen) {
		blocking_child_ready_done = scseen;
		for (idx = 0; idx < blocking_children_alloc; idx++) {
			cp = blocking_children[idx];
			if (NULL == cp)
				continue;
			scseen = cp->resp_ready_seen;
			scdone = cp->resp_ready_done;
			if (scdone != scseen) {
				cp->resp_ready_done = scseen;
				process_blocking_resp(cp);
			}
		}
	}
}


/*
 * blocking_child_common runs as a forked child or a thread
 */
int
blocking_child_common(
	blocking_child	*c
	)
{
	int say_bye;
	blocking_pipe_header *req;

	say_bye = FALSE;
	while (!say_bye) {
		req = receive_blocking_req_internal(c);
		if (NULL == req) {
			say_bye = TRUE;
			continue;
		}

		DEBUG_REQUIRE(BLOCKING_REQ_MAGIC == req->magic_sig);

		switch (req->rtype) {
		case BLOCKING_GETADDRINFO:
			if (blocking_getaddrinfo(c, req))
				say_bye = TRUE;
			break;

		case BLOCKING_GETNAMEINFO:
			if (blocking_getnameinfo(c, req))
				say_bye = TRUE;
			break;

		default:
			msyslog(LOG_ERR, "unknown req %d to blocking worker", req->rtype);
			say_bye = TRUE;
		}

		free(req);
	}

	return 0;
}


/*
 * worker_idle_timer_fired()
 *
 * The parent starts this timer when the last pending response has been
 * received from the child, making it idle, and clears the timer when a
 * request is dispatched to the child.  Once the timer expires, the
 * child is sent packing.
 *
 * This is called when worker_idle_timer is nonzero and less than or
 * equal to current_time.
 */
void
worker_idle_timer_fired(void)
{
	u_int			idx;
	blocking_child *	c;

	DEBUG_REQUIRE(0 == intres_req_pending);

	intres_timeout_req(0);
	for (idx = 0; idx < blocking_children_alloc; idx++) {
		c = blocking_children[idx];
		if (NULL == c)
			continue;
		req_child_exit(c);
	}
}


#else	/* !WORKER follows */
int ntp_worker_nonempty_compilation_unit;
#endif
