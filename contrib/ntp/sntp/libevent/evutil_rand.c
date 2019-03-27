/*
 * Copyright (c) 2007-2012 Niels Provos and Nick Mathewson
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

/* This file has our secure PRNG code.  On platforms that have arc4random(),
 * we just use that.  Otherwise, we include arc4random.c as a bunch of static
 * functions, and wrap it lightly.  We don't expose the arc4random*() APIs
 * because A) they aren't in our namespace, and B) it's not nice to name your
 * APIs after their implementations.  We keep them in a separate file
 * so that other people can rip it out and use it for whatever.
 */

#include "event2/event-config.h"
#include "evconfig-private.h"

#include <limits.h>

#include "util-internal.h"
#include "evthread-internal.h"

#ifdef EVENT__HAVE_ARC4RANDOM
#include <stdlib.h>
#include <string.h>
int
evutil_secure_rng_set_urandom_device_file(char *fname)
{
	(void) fname;
	return -1;
}
int
evutil_secure_rng_init(void)
{
	/* call arc4random() now to force it to self-initialize */
	(void) arc4random();
	return 0;
}
#ifndef EVENT__DISABLE_THREAD_SUPPORT
int
evutil_secure_rng_global_setup_locks_(const int enable_locks)
{
	return 0;
}
#endif
static void
evutil_free_secure_rng_globals_locks(void)
{
}

static void
ev_arc4random_buf(void *buf, size_t n)
{
#if defined(EVENT__HAVE_ARC4RANDOM_BUF) && !defined(__APPLE__)
	arc4random_buf(buf, n);
	return;
#else
	unsigned char *b = buf;

#if defined(EVENT__HAVE_ARC4RANDOM_BUF)
	/* OSX 10.7 introducd arc4random_buf, so if you build your program
	 * there, you'll get surprised when older versions of OSX fail to run.
	 * To solve this, we can check whether the function pointer is set,
	 * and fall back otherwise.  (OSX does this using some linker
	 * trickery.)
	 */
	{
		void (*tptr)(void *,size_t) =
		    (void (*)(void*,size_t))arc4random_buf;
		if (tptr != NULL) {
			arc4random_buf(buf, n);
			return;
		}
	}
#endif
	/* Make sure that we start out with b at a 4-byte alignment; plenty
	 * of CPUs care about this for 32-bit access. */
	if (n >= 4 && ((ev_uintptr_t)b) & 3) {
		ev_uint32_t u = arc4random();
		int n_bytes = 4 - (((ev_uintptr_t)b) & 3);
		memcpy(b, &u, n_bytes);
		b += n_bytes;
		n -= n_bytes;
	}
	while (n >= 4) {
		*(ev_uint32_t*)b = arc4random();
		b += 4;
		n -= 4;
	}
	if (n) {
		ev_uint32_t u = arc4random();
		memcpy(b, &u, n);
	}
#endif
}

#else /* !EVENT__HAVE_ARC4RANDOM { */

#ifdef EVENT__ssize_t
#define ssize_t EVENT__ssize_t
#endif
#define ARC4RANDOM_EXPORT static
#define ARC4_LOCK_() EVLOCK_LOCK(arc4rand_lock, 0)
#define ARC4_UNLOCK_() EVLOCK_UNLOCK(arc4rand_lock, 0)
#ifndef EVENT__DISABLE_THREAD_SUPPORT
static void *arc4rand_lock;
#endif

#define ARC4RANDOM_UINT32 ev_uint32_t
#define ARC4RANDOM_NOSTIR
#define ARC4RANDOM_NORANDOM
#define ARC4RANDOM_NOUNIFORM

#include "./arc4random.c"

#ifndef EVENT__DISABLE_THREAD_SUPPORT
int
evutil_secure_rng_global_setup_locks_(const int enable_locks)
{
	EVTHREAD_SETUP_GLOBAL_LOCK(arc4rand_lock, 0);
	return 0;
}
#endif

static void
evutil_free_secure_rng_globals_locks(void)
{
#ifndef EVENT__DISABLE_THREAD_SUPPORT
	if (arc4rand_lock != NULL) {
		EVTHREAD_FREE_LOCK(arc4rand_lock, 0);
		arc4rand_lock = NULL;
	}
#endif
	return;
}

int
evutil_secure_rng_set_urandom_device_file(char *fname)
{
#ifdef TRY_SEED_URANDOM
	ARC4_LOCK_();
	arc4random_urandom_filename = fname;
	ARC4_UNLOCK_();
#endif
	return 0;
}

int
evutil_secure_rng_init(void)
{
	int val;

	ARC4_LOCK_();
	if (!arc4_seeded_ok)
		arc4_stir();
	val = arc4_seeded_ok ? 0 : -1;
	ARC4_UNLOCK_();
	return val;
}

static void
ev_arc4random_buf(void *buf, size_t n)
{
	arc4random_buf(buf, n);
}

#endif /* } !EVENT__HAVE_ARC4RANDOM */

void
evutil_secure_rng_get_bytes(void *buf, size_t n)
{
	ev_arc4random_buf(buf, n);
}

void
evutil_secure_rng_add_bytes(const char *buf, size_t n)
{
}

void
evutil_free_secure_rng_globals_(void)
{
    evutil_free_secure_rng_globals_locks();
}
