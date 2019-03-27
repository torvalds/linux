/*-
 * Copyright (c) 2011-2012 Pawel Jakub Dawidek <pawel@dawidek.net>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config/config.h>

#ifdef HAVE_KQUEUE
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef HAVE_ARC4RANDOM
#include <openssl/rand.h>
#endif

#ifndef HAVE_STRLCAT
#include <compat/strlcat.h>
#endif

#include "auditdistd.h"
#include "pjdlog.h"
#include "subr.h"

int
vsnprlcat(char *str, size_t size, const char *fmt, va_list ap)
{
	size_t len;

	len = strlen(str);
	return (vsnprintf(str + len, size - len, fmt, ap));
}

int
snprlcat(char *str, size_t size, const char *fmt, ...)
{
	va_list ap;
	int result;

	va_start(ap, fmt);
	result = vsnprlcat(str, size, fmt, ap);
	va_end(ap);
	return (result);
}

const char *
role2str(int role)
{

	switch (role) {
	case ADIST_ROLE_SENDER:
		return ("sender");
	case ADIST_ROLE_RECEIVER:
		return ("receiver");
	}
	return ("unknown");
}

const char *
adist_errstr(int error)
{

	switch (error) {
	case ADIST_ERROR_WRONG_ORDER:
		return ("wrong operations order");
	case ADIST_ERROR_INVALID_NAME:
		return ("invalid trail file name");
	case ADIST_ERROR_OPEN_OLD:
		return ("attempt to open an old trail file");
	case ADIST_ERROR_CREATE:
		return ("creation of new trail file failed");
	case ADIST_ERROR_OPEN:
		return ("open of existing trail file failed");
	case ADIST_ERROR_READ:
		return ("read failed");
	case ADIST_ERROR_WRITE:
		return ("write failed");
	case ADIST_ERROR_RENAME:
		return ("rename of a trail file failed");
	default:
		return ("unknown error");
	}
}

void
adreq_log(int loglevel, int debuglevel, int error, struct adreq *adreq,
    const char *fmt, ...)
{
	char msg[1024];
	va_list ap;

	va_start(ap, fmt);
	(void)vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	(void)snprlcat(msg, sizeof(msg), "(seq=%ju) ",
	    (uintmax_t)adreq->adr_seq);
	switch (adreq->adr_cmd) {
	case ADIST_CMD_OPEN:
		(void)snprlcat(msg, sizeof(msg), "OPEN(%s)",
		    adreq->adr_data);
		break;
	case ADIST_CMD_APPEND:
		(void)snprlcat(msg, sizeof(msg), "APPEND(%ju)",
		    (uintmax_t)adreq->adr_datasize);
		break;
	case ADIST_CMD_CLOSE:
		(void)snprlcat(msg, sizeof(msg), "CLOSE(%s)",
		    adreq->adr_data);
		break;
	case ADIST_CMD_KEEPALIVE:
		(void)snprlcat(msg, sizeof(msg), "KEEPALIVE");
		break;
	case ADIST_CMD_ERROR:
		(void)snprlcat(msg, sizeof(msg), "ERROR");
		break;
	default:
		(void)snprlcat(msg, sizeof(msg), "UNKNOWN(%hhu)",
		    adreq->adr_cmd);
		break;
	}
	if (error != -1)
		(void)snprlcat(msg, sizeof(msg), ": %s", adist_errstr(error));
	(void)strlcat(msg, ".", sizeof(msg));
	pjdlog_common(loglevel, debuglevel, -1, "%s", msg);
}

int
adist_random(unsigned char *buf, size_t size)
{
#ifdef HAVE_ARC4RANDOM_BUF
	arc4random_buf(buf, size);
	return (0);
#elif defined(HAVE_ARC4RANDOM)
	uint32_t val;

	PJDLOG_ASSERT(size > 0);
	PJDLOG_ASSERT((size % sizeof(val)) == 0);

	do {
		val = arc4random();
		bcopy(&val, buf, sizeof(val));
		buf += sizeof(val);
		size -= sizeof(val);
	} while (size > 0);

	return (0);
#else
	if (RAND_bytes(buf, (int)size) == 0)
		return (-1);
	return (0);
#endif
}

static int wait_for_dir_kq = -1;
static int wait_for_file_kq = -1;

int
wait_for_dir_init(int fd)
{
#ifdef HAVE_KQUEUE
	struct kevent ev;
	int error, kq;

	PJDLOG_ASSERT(wait_for_dir_kq == -1);
#endif

	PJDLOG_ASSERT(fd != -1);

#ifdef HAVE_KQUEUE
	kq = kqueue();
	if (kq == -1) {
		pjdlog_errno(LOG_WARNING, "kqueue() failed");
		return (-1);
	}
	EV_SET(&ev, fd, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR,
	    NOTE_WRITE, 0, 0);
	if (kevent(kq, &ev, 1, NULL, 0, NULL) == -1) {
		error = errno;
		pjdlog_errno(LOG_WARNING, "kevent() failed");
		(void)close(kq);
		errno = error;
		return (-1);
	}
	wait_for_dir_kq = kq;
#endif

	return (0);
}

int
wait_for_file_init(int fd)
{
#ifdef HAVE_KQUEUE
	struct kevent ev[2];
	int error, kq;
#endif

	PJDLOG_ASSERT(fd != -1);

#ifdef HAVE_KQUEUE
	if (wait_for_file_kq != -1) {
		close(wait_for_file_kq);
		wait_for_file_kq = -1;
	}

	kq = kqueue();
	if (kq == -1) {
		pjdlog_errno(LOG_WARNING, "kqueue() failed");
		return (-1);
	}
	EV_SET(&ev[0], fd, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR,
	    NOTE_RENAME, 0, 0);
	EV_SET(&ev[1], fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR,
	    0, 0, 0);
	if (kevent(kq, ev, 2, NULL, 0, NULL) == -1) {
		error = errno;
		pjdlog_errno(LOG_WARNING, "kevent() failed");
		(void)close(kq);
		errno = error;
		return (-1);
	}
	wait_for_file_kq = kq;
#endif

	return (0);
}

/*
 * Wait for new file to appear in directory.
 */
void
wait_for_dir(void)
{
#ifdef HAVE_KQUEUE
	struct kevent ev;
#endif

	if (wait_for_dir_kq == -1) {
		sleep(1);
		return;
	}

#ifdef HAVE_KQUEUE
	PJDLOG_ASSERT(wait_for_dir_kq != -1);

	if (kevent(wait_for_dir_kq, NULL, 0, &ev, 1, NULL) == -1) {
		pjdlog_errno(LOG_WARNING, "kevent() failed");
		sleep(1);
	}
#endif
}

/*
 * Wait for file growth or rename.
 */
void
wait_for_file(void)
{
#ifdef HAVE_KQUEUE
	struct kevent ev[2];
#endif

	if (wait_for_file_kq == -1) {
		sleep(1);
		return;
	}

#ifdef HAVE_KQUEUE
	PJDLOG_ASSERT(wait_for_file_kq != -1);

	if (kevent(wait_for_file_kq, NULL, 0, ev, 2, NULL) == -1) {
		pjdlog_errno(LOG_WARNING, "kevent() failed");
		sleep(1);
	}
#endif
}
