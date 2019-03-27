/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include "libuutil_common.h"

#define HAVE_ASSFAIL	1

#include <assert.h>
#include <errno.h>
#include <libintl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/debug.h>
#include <thread.h>
#include <unistd.h>
#include <ctype.h>

#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif

/*
 * All of the old code under !defined(PTHREAD_ONCE_KEY_NP)
 * is here to enable the building of a native version of
 * libuutil.so when the build machine has not yet been upgraded
 * to a version of libc that provides pthread_key_create_once_np().
 * It should all be deleted when solaris_nevada ships.
 * The code is not MT-safe in a relaxed memory model.
 */

#if defined(PTHREAD_ONCE_KEY_NP)
static pthread_key_t	uu_error_key = PTHREAD_ONCE_KEY_NP;
#else	/* PTHREAD_ONCE_KEY_NP */
static pthread_key_t	uu_error_key = 0;
static pthread_mutex_t	uu_key_lock = PTHREAD_MUTEX_INITIALIZER;
#endif	/* PTHREAD_ONCE_KEY_NP */

static int		uu_error_key_setup = 0;

static pthread_mutex_t	uu_panic_lock = PTHREAD_MUTEX_INITIALIZER;
/* LINTED static unused */
static const char	*uu_panic_format;
/* LINTED static unused */
static va_list		uu_panic_args;
static pthread_t	uu_panic_thread;

static uint32_t		_uu_main_error;

void
uu_set_error(uint_t code)
{

#if defined(PTHREAD_ONCE_KEY_NP)
	if (pthread_key_create_once_np(&uu_error_key, NULL) != 0)
		uu_error_key_setup = -1;
	else
		uu_error_key_setup = 1;
#else	/* PTHREAD_ONCE_KEY_NP */
	if (uu_error_key_setup == 0) {
		(void) pthread_mutex_lock(&uu_key_lock);
		if (uu_error_key_setup == 0) {
			if (pthread_key_create(&uu_error_key, NULL) != 0)
				uu_error_key_setup = -1;
			else
				uu_error_key_setup = 1;
		}
		(void) pthread_mutex_unlock(&uu_key_lock);
	}
#endif	/* PTHREAD_ONCE_KEY_NP */
	if (uu_error_key_setup > 0)
		(void) pthread_setspecific(uu_error_key,
		    (void *)(uintptr_t)code);
}

uint32_t
uu_error(void)
{

	if (uu_error_key_setup < 0)	/* can't happen? */
		return (UU_ERROR_UNKNOWN);

	/*
	 * Because UU_ERROR_NONE == 0, if uu_set_error() was
	 * never called, then this will return UU_ERROR_NONE:
	 */
	return ((uint32_t)(uintptr_t)pthread_getspecific(uu_error_key));
}

const char *
uu_strerror(uint32_t code)
{
	const char *str;

	switch (code) {
	case UU_ERROR_NONE:
		str = dgettext(TEXT_DOMAIN, "No error");
		break;

	case UU_ERROR_INVALID_ARGUMENT:
		str = dgettext(TEXT_DOMAIN, "Invalid argument");
		break;

	case UU_ERROR_UNKNOWN_FLAG:
		str = dgettext(TEXT_DOMAIN, "Unknown flag passed");
		break;

	case UU_ERROR_NO_MEMORY:
		str = dgettext(TEXT_DOMAIN, "Out of memory");
		break;

	case UU_ERROR_CALLBACK_FAILED:
		str = dgettext(TEXT_DOMAIN, "Callback-initiated failure");
		break;

	case UU_ERROR_NOT_SUPPORTED:
		str = dgettext(TEXT_DOMAIN, "Operation not supported");
		break;

	case UU_ERROR_EMPTY:
		str = dgettext(TEXT_DOMAIN, "No value provided");
		break;

	case UU_ERROR_UNDERFLOW:
		str = dgettext(TEXT_DOMAIN, "Value too small");
		break;

	case UU_ERROR_OVERFLOW:
		str = dgettext(TEXT_DOMAIN, "Value too large");
		break;

	case UU_ERROR_INVALID_CHAR:
		str = dgettext(TEXT_DOMAIN,
		    "Value contains unexpected character");
		break;

	case UU_ERROR_INVALID_DIGIT:
		str = dgettext(TEXT_DOMAIN,
		    "Value contains digit not in base");
		break;

	case UU_ERROR_SYSTEM:
		str = dgettext(TEXT_DOMAIN, "Underlying system error");
		break;

	case UU_ERROR_UNKNOWN:
		str = dgettext(TEXT_DOMAIN, "Error status not known");
		break;

	default:
		errno = ESRCH;
		str = NULL;
		break;
	}
	return (str);
}

void
uu_panic(const char *format, ...)
{
	va_list args;

	va_start(args, format);

	(void) pthread_mutex_lock(&uu_panic_lock);
	if (uu_panic_thread == 0) {
		uu_panic_thread = pthread_self();
		uu_panic_format = format;
		va_copy(uu_panic_args, args);
	}
	(void) pthread_mutex_unlock(&uu_panic_lock);

	(void) vfprintf(stderr, format, args);

	if (uu_panic_thread == pthread_self())
		abort();
	else
		for (;;)
			(void) pause();
}

int
assfail(const char *astring, const char *file, int line)
{
	__assert(astring, file, line);
	/*NOTREACHED*/
	return (0);
}

static void
uu_lockup(void)
{
	(void) pthread_mutex_lock(&uu_panic_lock);
#if !defined(PTHREAD_ONCE_KEY_NP)
	(void) pthread_mutex_lock(&uu_key_lock);
#endif
	uu_avl_lockup();
	uu_list_lockup();
}

static void
uu_release(void)
{
	(void) pthread_mutex_unlock(&uu_panic_lock);
#if !defined(PTHREAD_ONCE_KEY_NP)
	(void) pthread_mutex_unlock(&uu_key_lock);
#endif
	uu_avl_release();
	uu_list_release();
}

static void
uu_release_child(void)
{
	uu_panic_format = NULL;
	uu_panic_thread = 0;

	uu_release();
}

#pragma init(uu_init)
static void
uu_init(void)
{
	(void) pthread_atfork(uu_lockup, uu_release, uu_release_child);
}

/*
 * Dump a block of memory in hex+ascii, for debugging
 */
void
uu_dump(FILE *out, const char *prefix, const void *buf, size_t len)
{
	const unsigned char *p = buf;
	int i;

	for (i = 0; i < len; i += 16) {
		int j;

		(void) fprintf(out, "%s", prefix);
		for (j = 0; j < 16 && i + j < len; j++) {
			(void) fprintf(out, "%2.2x ", p[i + j]);
		}
		for (; j < 16; j++) {
			(void) fprintf(out, "   ");
		}
		for (j = 0; j < 16 && i + j < len; j++) {
			(void) fprintf(out, "%c",
			    isprint(p[i + j]) ? p[i + j] : '.');
		}
		(void) fprintf(out, "\n");
	}
}
