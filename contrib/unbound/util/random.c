/*
 * util/random.c - thread safe random generator, which is reasonably secure.
 * 
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
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
 * Thread safe random functions. Similar to arc4random() with an explicit
 * initialisation routine.
 *
 * The code in this file is based on arc4random from
 * openssh-4.0p1/openbsd-compat/bsd-arc4random.c
 * That code is also BSD licensed. Here is their statement:
 *
 * Copyright (c) 1996, David Mazieres <dm@uun.org>
 * Copyright (c) 2008, Damien Miller <djm@openbsd.org>
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
#include "config.h"
#include "util/random.h"
#include "util/log.h"
#include <time.h>

#ifdef HAVE_NSS
/* nspr4 */
#include "prerror.h"
/* nss3 */
#include "secport.h"
#include "pk11pub.h"
#elif defined(HAVE_NETTLE)
#include "yarrow.h"
#endif

/** 
 * Max random value.  Similar to RAND_MAX, but more portable
 * (mingw uses only 15 bits random).
 */
#define MAX_VALUE 0x7fffffff

#if defined(HAVE_SSL)
void
ub_systemseed(unsigned int ATTR_UNUSED(seed))
{
	/* arc4random_uniform does not need seeds, it gets kernel entropy */
}

struct ub_randstate* 
ub_initstate(unsigned int ATTR_UNUSED(seed),
	struct ub_randstate* ATTR_UNUSED(from))
{
	struct ub_randstate* s = (struct ub_randstate*)malloc(1);
	if(!s) {
		log_err("malloc failure in random init");
		return NULL;
	}
	return s;
}

long int 
ub_random(struct ub_randstate* ATTR_UNUSED(s))
{
	/* This relies on MAX_VALUE being 0x7fffffff. */
	return (long)arc4random() & MAX_VALUE;
}

long int
ub_random_max(struct ub_randstate* state, long int x)
{
	(void)state;
	/* on OpenBSD, this does not need _seed(), or _stir() calls */
	return (long)arc4random_uniform((uint32_t)x);
}

#elif defined(HAVE_NSS)

/* not much to remember for NSS since we use its pk11_random, placeholder */
struct ub_randstate {
	int ready;
};

void ub_systemseed(unsigned int ATTR_UNUSED(seed))
{
}

struct ub_randstate* ub_initstate(unsigned int ATTR_UNUSED(seed), 
	struct ub_randstate* ATTR_UNUSED(from))
{
	struct ub_randstate* s = (struct ub_randstate*)calloc(1, sizeof(*s));
	if(!s) {
		log_err("malloc failure in random init");
		return NULL;
	}
	return s;
}

long int ub_random(struct ub_randstate* ATTR_UNUSED(state))
{
	long int x;
	/* random 31 bit value. */
	SECStatus s = PK11_GenerateRandom((unsigned char*)&x, (int)sizeof(x));
	if(s != SECSuccess) {
		log_err("PK11_GenerateRandom error: %s",
			PORT_ErrorToString(PORT_GetError()));
	}
	return x & MAX_VALUE;
}

#elif defined(HAVE_NETTLE)

/**
 * libnettle implements a Yarrow-256 generator (SHA256 + AES),
 * and we have to ensure it is seeded before use.
 */
struct ub_randstate {
	struct yarrow256_ctx ctx;
	int seeded;
};

void ub_systemseed(unsigned int ATTR_UNUSED(seed))
{
/**
 * We seed on init and not here, as we need the ctx to re-seed.
 * This also means that re-seeding is not supported.
 */
	log_err("Re-seeding not supported, generator untouched");
}

struct ub_randstate* ub_initstate(unsigned int seed,
	struct ub_randstate* ATTR_UNUSED(from))
{
	struct ub_randstate* s = (struct ub_randstate*)calloc(1, sizeof(*s));
	uint8_t buf[YARROW256_SEED_FILE_SIZE];
	if(!s) {
		log_err("malloc failure in random init");
		return NULL;
	}
	/* Setup Yarrow context */
	yarrow256_init(&s->ctx, 0, NULL);

	if(getentropy(buf, sizeof(buf)) != -1) {
		/* got entropy */
		yarrow256_seed(&s->ctx, YARROW256_SEED_FILE_SIZE, buf);
		s->seeded = yarrow256_is_seeded(&s->ctx);
	} else {
		/* Stretch the uint32 input seed and feed it to Yarrow */
		uint32_t v = seed;
		size_t i;
		for(i=0; i < (YARROW256_SEED_FILE_SIZE/sizeof(seed)); i++) {
			memmove(buf+i*sizeof(seed), &v, sizeof(seed));
			v = v*seed + (uint32_t)i;
		}
		yarrow256_seed(&s->ctx, YARROW256_SEED_FILE_SIZE, buf);
		s->seeded = yarrow256_is_seeded(&s->ctx);
	}

	return s;
}

long int ub_random(struct ub_randstate* s)
{
	/* random 31 bit value. */
	long int x = 0;
	if (!s || !s->seeded) {
		log_err("Couldn't generate randomness, Yarrow-256 generator not yet seeded");
	} else {
		yarrow256_random(&s->ctx, sizeof(x), (uint8_t *)&x);
	}
	return x & MAX_VALUE;
}
#endif /* HAVE_SSL or HAVE_NSS or HAVE_NETTLE */


#if defined(HAVE_NSS) || defined(HAVE_NETTLE)
long int
ub_random_max(struct ub_randstate* state, long int x)
{
	/* make sure we fetch in a range that is divisible by x. ignore
	 * values from d .. MAX_VALUE, instead draw a new number */
	long int d = MAX_VALUE - (MAX_VALUE % x); /* d is divisible by x */
	long int v = ub_random(state);
	while(d <= v)
		v = ub_random(state);
	return (v % x);
}
#endif /* HAVE_NSS or HAVE_NETTLE */

void 
ub_randfree(struct ub_randstate* s)
{
	free(s);
	/* user app must do RAND_cleanup(); */
}
