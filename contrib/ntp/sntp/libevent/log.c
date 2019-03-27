/*	$OpenBSD: err.c,v 1.2 2002/06/25 15:50:15 mickey Exp $	*/

/*
 * log.c
 *
 * Based on err.c, which was adapted from OpenBSD libc *err* *warn* code.
 *
 * Copyright (c) 2005-2012 Niels Provos and Nick Mathewson
 *
 * Copyright (c) 2000 Dug Song <dugsong@monkey.org>
 *
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "event2/event-config.h"
#include "evconfig-private.h"

#ifdef _WIN32
#include <winsock2.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#endif
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include "event2/event.h"
#include "event2/util.h"

#include "log-internal.h"

static void event_log(int severity, const char *msg);
static void event_exit(int errcode) EV_NORETURN;

static event_fatal_cb fatal_fn = NULL;

#ifdef EVENT_DEBUG_LOGGING_ENABLED
#ifdef USE_DEBUG
#define DEFAULT_MASK EVENT_DBG_ALL
#else
#define DEFAULT_MASK 0
#endif

#ifdef USE_GLOBAL_FOR_DEBUG_LOGGING
ev_uint32_t event_debug_logging_mask_ = DEFAULT_MASK;
#else
static ev_uint32_t event_debug_logging_mask_ = DEFAULT_MASK;
ev_uint32_t
event_debug_get_logging_mask_(void)
{
	return event_debug_logging_mask_;
}
#endif
#endif /* EVENT_DEBUG_LOGGING_ENABLED */

void
event_enable_debug_logging(ev_uint32_t which)
{
#ifdef EVENT_DEBUG_LOGGING_ENABLED
	event_debug_logging_mask_ = which;
#endif
}

void
event_set_fatal_callback(event_fatal_cb cb)
{
	fatal_fn = cb;
}

static void
event_exit(int errcode)
{
	if (fatal_fn) {
		fatal_fn(errcode);
		exit(errcode); /* should never be reached */
	} else if (errcode == EVENT_ERR_ABORT_)
		abort();
	else
		exit(errcode);
}

void
event_err(int eval, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	event_logv_(EVENT_LOG_ERR, strerror(errno), fmt, ap);
	va_end(ap);
	event_exit(eval);
}

void
event_warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	event_logv_(EVENT_LOG_WARN, strerror(errno), fmt, ap);
	va_end(ap);
}

void
event_sock_err(int eval, evutil_socket_t sock, const char *fmt, ...)
{
	va_list ap;
	int err = evutil_socket_geterror(sock);

	va_start(ap, fmt);
	event_logv_(EVENT_LOG_ERR, evutil_socket_error_to_string(err), fmt, ap);
	va_end(ap);
	event_exit(eval);
}

void
event_sock_warn(evutil_socket_t sock, const char *fmt, ...)
{
	va_list ap;
	int err = evutil_socket_geterror(sock);

	va_start(ap, fmt);
	event_logv_(EVENT_LOG_WARN, evutil_socket_error_to_string(err), fmt, ap);
	va_end(ap);
}

void
event_errx(int eval, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	event_logv_(EVENT_LOG_ERR, NULL, fmt, ap);
	va_end(ap);
	event_exit(eval);
}

void
event_warnx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	event_logv_(EVENT_LOG_WARN, NULL, fmt, ap);
	va_end(ap);
}

void
event_msgx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	event_logv_(EVENT_LOG_MSG, NULL, fmt, ap);
	va_end(ap);
}

void
event_debugx_(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	event_logv_(EVENT_LOG_DEBUG, NULL, fmt, ap);
	va_end(ap);
}

void
event_logv_(int severity, const char *errstr, const char *fmt, va_list ap)
{
	char buf[1024];
	size_t len;

	if (severity == EVENT_LOG_DEBUG && !event_debug_get_logging_mask_())
		return;

	if (fmt != NULL)
		evutil_vsnprintf(buf, sizeof(buf), fmt, ap);
	else
		buf[0] = '\0';

	if (errstr) {
		len = strlen(buf);
		if (len < sizeof(buf) - 3) {
			evutil_snprintf(buf + len, sizeof(buf) - len, ": %s", errstr);
		}
	}

	event_log(severity, buf);
}

static event_log_cb log_fn = NULL;

void
event_set_log_callback(event_log_cb cb)
{
	log_fn = cb;
}

static void
event_log(int severity, const char *msg)
{
	if (log_fn)
		log_fn(severity, msg);
	else {
		const char *severity_str;
		switch (severity) {
		case EVENT_LOG_DEBUG:
			severity_str = "debug";
			break;
		case EVENT_LOG_MSG:
			severity_str = "msg";
			break;
		case EVENT_LOG_WARN:
			severity_str = "warn";
			break;
		case EVENT_LOG_ERR:
			severity_str = "err";
			break;
		default:
			severity_str = "???";
			break;
		}
		(void)fprintf(stderr, "[%s] %s\n", severity_str, msg);
	}
}
