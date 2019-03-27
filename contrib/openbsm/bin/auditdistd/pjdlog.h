/*-
 * Copyright (c) 2009-2010 The FreeBSD Foundation
 * Copyright (c) 2011 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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

#ifndef	_PJDLOG_H_
#define	_PJDLOG_H_

#include <sys/cdefs.h>

#include <stdarg.h>
#include <sysexits.h>
#include <syslog.h>

#include <compat/compat.h>

#define	PJDLOG_MODE_STD		0
#define	PJDLOG_MODE_SYSLOG	1

void pjdlog_init(int mode);
void pjdlog_fini(void);

void pjdlog_mode_set(int mode);
int pjdlog_mode_get(void);

void pjdlog_debug_set(int level);
int pjdlog_debug_get(void);

void pjdlog_prefix_set(const char *fmt, ...) __printflike(1, 2);
void pjdlogv_prefix_set(const char *fmt, va_list ap) __printflike(1, 0);

void pjdlog_common(int loglevel, int debuglevel, int error, const char *fmt,
    ...) __printflike(4, 5);
void pjdlogv_common(int loglevel, int debuglevel, int error, const char *fmt,
    va_list ap) __printflike(4, 0);

void pjdlog(int loglevel, const char *fmt, ...) __printflike(2, 3);
void pjdlogv(int loglevel, const char *fmt, va_list ap) __printflike(2, 0);

#define	pjdlogv_emergency(fmt, ap)	pjdlogv(LOG_EMERG, (fmt), (ap))
#define	pjdlog_emergency(...)		pjdlog(LOG_EMERG, __VA_ARGS__)
#define	pjdlogv_alert(fmt, ap)		pjdlogv(LOG_ALERT, (fmt), (ap))
#define	pjdlog_alert(...)		pjdlog(LOG_ALERT, __VA_ARGS__)
#define	pjdlogv_critical(fmt, ap)	pjdlogv(LOG_CRIT, (fmt), (ap))
#define	pjdlog_critical(...)		pjdlog(LOG_CRIT, __VA_ARGS__)
#define	pjdlogv_error(fmt, ap)		pjdlogv(LOG_ERR, (fmt), (ap))
#define	pjdlog_error(...)		pjdlog(LOG_ERR, __VA_ARGS__)
#define	pjdlogv_warning(fmt, ap)	pjdlogv(LOG_WARNING, (fmt), (ap))
#define	pjdlog_warning(...)		pjdlog(LOG_WARNING, __VA_ARGS__)
#define	pjdlogv_notice(fmt, ap)		pjdlogv(LOG_NOTICE, (fmt), (ap))
#define	pjdlog_notice(...)		pjdlog(LOG_NOTICE, __VA_ARGS__)
#define	pjdlogv_info(fmt, ap)		pjdlogv(LOG_INFO, (fmt), (ap))
#define	pjdlog_info(...)		pjdlog(LOG_INFO, __VA_ARGS__)

void pjdlog_debug(int debuglevel, const char *fmt, ...) __printflike(2, 3);
void pjdlogv_debug(int debuglevel, const char *fmt, va_list ap) __printflike(2, 0);

void pjdlog_errno(int loglevel, const char *fmt, ...) __printflike(2, 3);
void pjdlogv_errno(int loglevel, const char *fmt, va_list ap) __printflike(2, 0);

void pjdlog_exit(int exitcode, const char *fmt, ...) __printflike(2, 3) __dead2;
void pjdlogv_exit(int exitcode, const char *fmt, va_list ap) __printflike(2, 0) __dead2;

void pjdlog_exitx(int exitcode, const char *fmt, ...) __printflike(2, 3) __dead2;
void pjdlogv_exitx(int exitcode, const char *fmt, va_list ap) __printflike(2, 0) __dead2;

void pjdlog_abort(const char *func, const char *file, int line,
    const char *failedexpr, const char *fmt, ...) __printflike(5, 6) __dead2;

#define	PJDLOG_VERIFY(expr)	do {					\
	if (!(expr)) {							\
		pjdlog_abort(__func__, __FILE__, __LINE__, #expr,	\
		    "%s", __func__);					\
	}								\
} while (0)
#define	PJDLOG_RVERIFY(expr, ...)	do {				\
	if (!(expr)) {							\
		pjdlog_abort(__func__, __FILE__, __LINE__, #expr,	\
		    __VA_ARGS__);					\
	}								\
} while (0)
#define	PJDLOG_ABORT(...)	pjdlog_abort(__func__, __FILE__,	\
				    __LINE__, NULL, __VA_ARGS__)
#ifdef NDEBUG
#define	PJDLOG_ASSERT(expr)	do { } while (0)
#define	PJDLOG_RASSERT(...)	do { } while (0)
#else
#define	PJDLOG_ASSERT(expr)	PJDLOG_VERIFY(expr)
#define	PJDLOG_RASSERT(...)	PJDLOG_RVERIFY(__VA_ARGS__)
#endif

#endif	/* !_PJDLOG_H_ */
