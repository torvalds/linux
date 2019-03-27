/*
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Begemot: libunimsg/sscop/common.c,v 1.5 2005/05/23 11:46:16 brandt_h Exp $
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <assert.h>
#include <fcntl.h>
#include <err.h>

#include <netnatm/unimsg.h>
#include <netnatm/saal/sscop.h>
#include "common.h"

struct timer {
	evTimerID	id;
	struct sscop	*sscop;
	void		(*func)(void *);
};

int useframe;
int sscopframe;
u_int sscop_vflag;
int sscop_fd;
int user_fd;
int loose;
int user_out_fd;
u_int verbose;
#ifndef USE_LIBBEGEMOT
evContext evctx;
#endif
evFileID sscop_h;
evFileID user_h;

/*
 * This function get's called from sscop to put out verbose messages
 */
void 
sscop_verbose(struct sscop *sscop __unused, void *u __unused,
    const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}
void
verb(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

/*
 * Dump a buffer in hex to stderr.
 */
void
dump_buf(const char *w, const u_char *buf, size_t len)
{
	u_int i;

	fprintf(stderr, "%s %zu: ", w, len);
	for(i = 0; i < len; i++) {
		if (i % 4 == 0 && i != 0)
			fprintf(stderr, " ");
		fprintf(stderr, "%02x", *buf++);
	}
	fprintf(stderr, "\n");
}

/*
 * SSCOP file descriptor is ready. Allocate and read one message
 * and dispatch a signal.
 */
struct uni_msg *
proto_msgin(int fd __unused)
{
	struct uni_msg *m = NULL;
	ssize_t size;
	u_int32_t flen;
	u_int got;

	if (sscopframe) {
		if ((size = read(sscop_fd, &flen, 4)) == -1)
			err(1, "error reading frame hdr");
		if (size == 0) {
			got = 0;
			goto eof;
		}
		if (size != 4)
			errx(1, "short frame header: %zd", size);
		if ((m = uni_msg_alloc(flen)) == NULL)
			err(1, NULL);
		for (got = 0; got < flen; got += (size_t)size) {
			size = read(sscop_fd, m->b_rptr + got, flen - got);
			if (size == -1)
				err(1, "error reading frame");
			if (size == 0) {
				got = 0;
				break;
			}
		}

	} else {
		if ((m = uni_msg_alloc(MAXMSG)) == NULL)
			err(1, NULL);
		if ((size = read(sscop_fd, m->b_rptr, MAXMSG)) == -1)
			err(1, "error reading message");
		got = size;
	}

	if (got == 0) {
  eof:
#ifdef USE_LIBBEGEMOT
		poll_unregister(sscop_h);
#else
		evDeselectFD(evctx, sscop_h);
#endif
		(void)close(sscop_fd);
		sscop_fd = -1;
		if (m != NULL)
			uni_msg_destroy(m);
		VERBOSE(("EOF on sscop file descriptor"));
		return (NULL);
	}
	m->b_wptr = m->b_rptr + got;

	if(verbose & 0x0002)
		dump_buf("SSCOP INPUT", m->b_rptr, got);

	return (m);
}

/*
 * User file descriptor ready - read a message
 */
struct uni_msg *
user_msgin(int fd __unused)
{
	struct uni_msg *m = NULL;
	ssize_t size;
	u_int32_t flen;
	u_int got;

	if (useframe) {
		if ((size = read(user_fd, &flen, 4)) == -1)
			err(1, "error reading frame hdr");
		if (size == 0) {
			got = 0;
			goto eof;
		}
		if (size != 4)
			errx(1, "short frame header: %zd", size);
		if ((m = uni_msg_alloc(flen)) == NULL)
			err(1, NULL);
		for (got = 0; got < flen; got++) {
			size = read(user_fd, m->b_rptr + got, flen - got);
			if (size == -1)
				err(1, "error reading frame");
			if (size == 0) {
				got = 0;
				break;
			}
			got += (size_t)size;
		}

	} else {
		if ((m = uni_msg_alloc(MAXMSG)) == NULL)
			err(1, NULL);
		if ((size = read(user_fd, m->b_rptr, MAXMSG)) == -1)
			err(1, "error reading message");
		got = size;
	}

	if (size == 0) {
  eof:
#ifdef USE_LIBBEGEMOT
		poll_unregister(user_h);
#else
		evDeselectFD(evctx, user_h);
#endif
		if (m != NULL)
			uni_msg_destroy(m);
		VERBOSE(("EOF on user connection"));
		return (NULL);
	}
	m->b_wptr = m->b_rptr + size;

	return (m);
}

/*
 * Write message to the SSCOP file descriptor.
 * Here we have a problem: we should have a means to check how much space
 * we have. If the pipe is full, we could declare the lower layer busy and
 * drop the message. However, how do we know, when a message will fit?
 * Selecting for WRITE doesn't help, because it will return even if a single
 * byte can be written. For this reason, we switch the file descriptor to
 * blocking mode, and hope everything is fast enough to not timeout us.
 * Alternatively we could just drop the message. Using kevent would help here.
 */
void
proto_msgout(struct uni_msg *m)
{
	struct iovec iov[2];
	u_int32_t flen;
	ssize_t size;
	static int sent;
	int fl;

	if (verbose & 0x0002)
		dump_buf("send", m->b_rptr, uni_msg_len(m));
	if (loose > 0 && (sent++ % loose) == loose - 1) {
		VERBOSE(("loosing message"));
		uni_msg_destroy(m);
		return;
	}

	flen = uni_msg_len(m);

	iov[0].iov_len = sscopframe ? 4 : 0;
	iov[0].iov_base = (caddr_t)&flen;
	iov[1].iov_len = uni_msg_len(m);
	iov[1].iov_base = m->b_rptr;

	if ((fl = fcntl(sscop_fd, F_GETFL, 0)) == -1)
		err(1, "cannot get flags for sscop fd");
	fl &= ~O_NONBLOCK;
	if (fcntl(sscop_fd, F_SETFL, fl) == -1)
		err(1, "cannot set flags for sscop fd");

	if ((size = writev(sscop_fd, iov, 2)) == -1)
		err(1, "write sscop");
	if ((size_t)size != iov[0].iov_len + iov[1].iov_len)
		err(1, "short sscop write %zu %zu %zd",
		    iov[0].iov_len, iov[1].iov_len, size);

	fl |= O_NONBLOCK;
	if (fcntl(sscop_fd, F_SETFL, fl) == -1)
		err(1, "cannot restore flags for sscop fd");

	uni_msg_destroy(m);
}

/*
 * output a message to the user
 */
void
user_msgout(struct uni_msg *m)
{
	struct iovec iov[2];
	u_int32_t flen;
	ssize_t size;

	flen = uni_msg_len(m);

	iov[0].iov_len = useframe ? 4 : 0;
	iov[0].iov_base = (caddr_t)&flen;
	iov[1].iov_len = uni_msg_len(m);
	iov[1].iov_base = m->b_rptr;

	if ((size = writev(user_out_fd, iov, 2)) == -1)
		err(1, "write sscop");
	if ((size_t)size != iov[0].iov_len + iov[1].iov_len)
		errx(1, "short sscop write");

	uni_msg_destroy(m);
}

void
parse_param(struct sscop_param *param, u_int *pmask, int opt, char *arg)
{
	u_int val;
	char *end, *p;

	if(opt == 'b') {
		param->flags |= SSCOP_ROBUST;
		*pmask |= SSCOP_SET_ROBUST;
		return;
	}
	if(opt == 'x') {
		param->flags |= SSCOP_POLLREX;
		*pmask |= SSCOP_SET_POLLREX;
		return;
	}
	if(opt == 'W') {
		val = (u_int)strtoul(optarg, &end, 0);

		if(*end != '\0')
			errx(1, "bad number to -W '%s'", optarg);
		if(val >= (1 << 24) - 1)
			errx(1, "window too large: 0x%x", val);
		param->mr = val;
		*pmask |= SSCOP_SET_MR;
		return;
	}

	if((p = strchr(arg, '=')) == NULL)
		errx(1, "need '=' in argument to -%c", opt);
	*p++ = 0;
	if(*p == 0)
		errx(1, "argument to -%c %s empty", opt, arg);
	val = strtoul(p, &end, 0);
	if(*end != 0)
		errx(1, "bad number in -%c %s=%s", opt, arg, p);

	if(opt == 't') {
		if(strcmp(arg, "cc") == 0) {
			param->timer_cc = val;
			*pmask |= SSCOP_SET_TCC;
		} else if(strcmp(arg, "poll") == 0) {
			param->timer_poll = val;
			*pmask |= SSCOP_SET_TPOLL;
		} else if(strcmp(arg, "ka") == 0) {
			param->timer_keep_alive = val;
			*pmask |= SSCOP_SET_TKA;
		} else if(strcmp(arg, "nr") == 0) {
			param->timer_no_response = val;
			*pmask |= SSCOP_SET_TNR;
		} else if(strcmp(arg, "idle") == 0) {
			param->timer_idle = val;
			*pmask |= SSCOP_SET_TIDLE;
		} else
			errx(1, "bad timer name '%s'", arg);
		return;
	}

	if(opt == 'a') {
		if(strcmp(arg, "j") == 0) {
			param->maxj = val;
			*pmask |= SSCOP_SET_MAXJ;
		} else if(strcmp(arg, "k") == 0) {
			param->maxk = val;
			*pmask |= SSCOP_SET_MAXK;
		} else if(strcmp(arg, "cc") == 0) {
			param->maxcc = val;
			*pmask |= SSCOP_SET_MAXCC;
		} else if(strcmp(arg, "pd") == 0) {
			param->maxpd = val;
			*pmask |= SSCOP_SET_MAXPD;
		} else if(strcmp(arg, "stat") == 0) {
			param->maxstat = val;
			*pmask |= SSCOP_SET_MAXSTAT;
		} else
			errx(1, "bad parameter '%s'", arg);
		return;
	}

	verb("bad flag");
	abort();
}

#ifdef USE_LIBBEGEMOT
static void
tfunc(int tid __unused, void *uap)
#else
static void
tfunc(evContext ctx __unused, void *uap, struct timespec due __unused,
    struct timespec inter __unused)
#endif
{
	struct timer *t = uap;

	t->func(t->sscop);
	free(t);
}

/*
 * Start a timer
 */
void *
sscop_start_timer(struct sscop *sscop, void *arg __unused, u_int msec,
    void (*func)(void *))
{
	struct timer *t;
#ifndef USE_LIBBEGEMOT
	struct timespec due;
#endif

	if ((t = malloc(sizeof(*t))) == NULL)
		err(1, NULL);
	t->sscop = sscop;
	t->func = func;

#ifdef USE_LIBBEGEMOT
	if ((t->id = poll_start_timer(msec, 0, tfunc, t)) == -1)
		err(1, "cannot start timer");
#else
	due = evAddTime(evNowTime(),
	    evConsTime((time_t)msec/1000, (long)(msec%1000)*1000));

	if (evSetTimer(evctx, tfunc, t, due, evConsTime(0, 0), &t->id))
		err(1, "cannot start timer");
#endif

	return (t);
}

/*
 * Stop a timer
 */
void
sscop_stop_timer(struct sscop *sscop __unused, void *arg __unused, void *tp)
{
	struct timer *t = tp;

#ifdef USE_LIBBEGEMOT
	poll_stop_timer(t->id);
#else
	evClearTimer(evctx, t->id);
#endif
	free(t);
}
