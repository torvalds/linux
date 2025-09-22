/*	$OpenBSD: pthread_once.c,v 1.3 2018/03/19 03:48:17 guenther Exp $ */
/*
 * Copyright (c) 2004,2005 Ted Unangst <tedu@openbsd.org>s
 * Copyright (c) 2018 Bob Beck <beck@openbsd.org>
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
pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
	if (once_control->state == PTHREAD_NEEDS_INIT) {
		init_routine();
		once_control->state = PTHREAD_DONE_INIT;
	}
	return (0);
}

int
pthread_equal(pthread_t t1, pthread_t t2)
{
	return (t1 == t2);
}

pthread_t
pthread_self(void)
{
	/* needs to differ from 0 inited value. */
	return (pthread_t) 1;
}
DEF_STRONG(pthread_self);
