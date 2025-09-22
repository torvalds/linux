/*	$OpenBSD: pthread_mutex.c,v 1.2 2020/12/22 13:07:54 bcook Exp $ */
/*
 * Copyright (c) 2018 Brent Cook <bcook@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <pthread.h>

int
pthread_mutex_init(pthread_mutex_t *mutex,
    const pthread_mutexattr_t *attr)
{
	return (0);
}
DEF_STRONG(pthread_mutex_init);

int
pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	return (0);
}
DEF_STRONG(pthread_mutex_destroy);

int
pthread_mutex_lock(pthread_mutex_t *mutex)
{
	return (0);
}
DEF_STRONG(pthread_mutex_lock);

int
pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	return (0);
}
DEF_STRONG(pthread_mutex_unlock);
