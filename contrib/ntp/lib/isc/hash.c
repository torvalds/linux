/*
 * Copyright (C) 2004-2007, 2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2003  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: hash.c,v 1.16 2009/09/01 00:22:28 jinmei Exp $ */

/*! \file
 * Some portion of this code was derived from universal hash function
 * libraries of Rice University.
\section license UH Universal Hashing Library

Copyright ((c)) 2002, Rice University
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
    copyright notice, this list of conditions and the following
    disclaimer in the documentation and/or other materials provided
    with the distribution.

    * Neither the name of Rice University (RICE) nor the names of its
    contributors may be used to endorse or promote products derived
    from this software without specific prior written permission.


This software is provided by RICE and the contributors on an "as is"
basis, without any representations or warranties of any kind, express
or implied including, but not limited to, representations or
warranties of non-infringement, merchantability or fitness for a
particular purpose. In no event shall RICE or contributors be liable
for any direct, indirect, incidental, special, exemplary, or
consequential damages (including, but not limited to, procurement of
substitute goods or services; loss of use, data, or profits; or
business interruption) however caused and on any theory of liability,
whether in contract, strict liability, or tort (including negligence
or otherwise) arising in any way out of the use of this software, even
if advised of the possibility of such damage.
*/

#include <config.h>

#include <isc/entropy.h>
#include <isc/hash.h>
#include <isc/mem.h>
#include <isc/magic.h>
#include <isc/mutex.h>
#include <isc/once.h>
#include <isc/random.h>
#include <isc/refcount.h>
#include <isc/string.h>
#include <isc/util.h>

#define HASH_MAGIC		ISC_MAGIC('H', 'a', 's', 'h')
#define VALID_HASH(h)		ISC_MAGIC_VALID((h), HASH_MAGIC)

/*%
 * A large 32-bit prime number that specifies the range of the hash output.
 */
#define PRIME32 0xFFFFFFFB              /* 2^32 -  5 */

/*@{*/
/*%
 * Types of random seed and hash accumulator.  Perhaps they can be system
 * dependent.
 */
typedef isc_uint32_t hash_accum_t;
typedef isc_uint16_t hash_random_t;
/*@}*/

/*% isc hash structure */
struct isc_hash {
	unsigned int	magic;
	isc_mem_t	*mctx;
	isc_mutex_t	lock;
	isc_boolean_t	initialized;
	isc_refcount_t	refcnt;
	isc_entropy_t	*entropy; /*%< entropy source */
	unsigned int	limit;	/*%< upper limit of key length */
	size_t		vectorlen; /*%< size of the vector below */
	hash_random_t	*rndvector; /*%< random vector for universal hashing */
};

static isc_mutex_t createlock;
static isc_once_t once = ISC_ONCE_INIT;
static isc_hash_t *hash = NULL;

static unsigned char maptolower[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
	0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
	0x40, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
	0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
	0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
	0x78, 0x79, 0x7a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
	0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
	0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
	0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
	0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
	0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
	0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
	0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
	0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
	0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
	0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
	0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
	0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
	0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
	0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
	0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
	0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
	0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
	0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};

isc_result_t
isc_hash_ctxcreate(isc_mem_t *mctx, isc_entropy_t *entropy,
		   unsigned int limit, isc_hash_t **hctxp)
{
	isc_result_t result;
	isc_hash_t *hctx;
	size_t vlen;
	hash_random_t *rv;
	hash_accum_t overflow_limit;

	REQUIRE(mctx != NULL);
	REQUIRE(hctxp != NULL && *hctxp == NULL);

	/*
	 * Overflow check.  Since our implementation only does a modulo
	 * operation at the last stage of hash calculation, the accumulator
	 * must not overflow.
	 */
	overflow_limit =
		1 << (((sizeof(hash_accum_t) - sizeof(hash_random_t))) * 8);
	if (overflow_limit < (limit + 1) * 0xff)
		return (ISC_R_RANGE);

	hctx = isc_mem_get(mctx, sizeof(isc_hash_t));
	if (hctx == NULL)
		return (ISC_R_NOMEMORY);

	vlen = sizeof(hash_random_t) * (limit + 1);
	rv = isc_mem_get(mctx, vlen);
	if (rv == NULL) {
		result = ISC_R_NOMEMORY;
		goto errout;
	}

	/*
	 * We need a lock.
	 */
	result = isc_mutex_init(&hctx->lock);
	if (result != ISC_R_SUCCESS)
		goto errout;

	/*
	 * From here down, no failures will/can occur.
	 */
	hctx->magic = HASH_MAGIC;
	hctx->mctx = NULL;
	isc_mem_attach(mctx, &hctx->mctx);
	hctx->initialized = ISC_FALSE;
	result = isc_refcount_init(&hctx->refcnt, 1);
	if (result != ISC_R_SUCCESS)
		goto cleanup_lock;
	hctx->entropy = NULL;
	hctx->limit = limit;
	hctx->vectorlen = vlen;
	hctx->rndvector = rv;

#ifdef BIND9
	if (entropy != NULL)
		isc_entropy_attach(entropy, &hctx->entropy);
#else
	UNUSED(entropy);
#endif

	*hctxp = hctx;
	return (ISC_R_SUCCESS);

 cleanup_lock:
	DESTROYLOCK(&hctx->lock);
 errout:
	isc_mem_put(mctx, hctx, sizeof(isc_hash_t));
	if (rv != NULL)
		isc_mem_put(mctx, rv, vlen);

	return (result);
}

static void
initialize_lock(void) {
	RUNTIME_CHECK(isc_mutex_init(&createlock) == ISC_R_SUCCESS);
}

isc_result_t
isc_hash_create(isc_mem_t *mctx, isc_entropy_t *entropy, size_t limit) {
	isc_result_t result = ISC_R_SUCCESS;

	REQUIRE(mctx != NULL);
	INSIST(hash == NULL);

	RUNTIME_CHECK(isc_once_do(&once, initialize_lock) == ISC_R_SUCCESS);

	LOCK(&createlock);

	if (hash == NULL)
		result = isc_hash_ctxcreate(mctx, entropy, limit, &hash);

	UNLOCK(&createlock);

	return (result);
}

void
isc_hash_ctxinit(isc_hash_t *hctx) {
	LOCK(&hctx->lock);

	if (hctx->initialized == ISC_TRUE)
		goto out;

	if (hctx->entropy) {
#ifdef BIND9
		isc_result_t result;

		result = isc_entropy_getdata(hctx->entropy,
					     hctx->rndvector, hctx->vectorlen,
					     NULL, 0);
		INSIST(result == ISC_R_SUCCESS);
#else
		INSIST(0);
#endif
	} else {
		isc_uint32_t pr;
		unsigned int i, copylen;
		unsigned char *p;

		p = (unsigned char *)hctx->rndvector;
		for (i = 0; i < hctx->vectorlen; i += copylen, p += copylen) {
			isc_random_get(&pr);
			if (i + sizeof(pr) <= hctx->vectorlen)
				copylen = sizeof(pr);
			else
				copylen = hctx->vectorlen - i;

			memcpy(p, &pr, copylen);
		}
		INSIST(p == (unsigned char *)hctx->rndvector +
		       hctx->vectorlen);
	}

	hctx->initialized = ISC_TRUE;

 out:
	UNLOCK(&hctx->lock);
}

void
isc_hash_init() {
	INSIST(hash != NULL && VALID_HASH(hash));

	isc_hash_ctxinit(hash);
}

void
isc_hash_ctxattach(isc_hash_t *hctx, isc_hash_t **hctxp) {
	REQUIRE(VALID_HASH(hctx));
	REQUIRE(hctxp != NULL && *hctxp == NULL);

	isc_refcount_increment(&hctx->refcnt, NULL);
	*hctxp = hctx;
}

static void
destroy(isc_hash_t **hctxp) {
	isc_hash_t *hctx;
	isc_mem_t *mctx;
	unsigned char canary0[4], canary1[4];

	REQUIRE(hctxp != NULL && *hctxp != NULL);
	hctx = *hctxp;
	*hctxp = NULL;

	LOCK(&hctx->lock);

	isc_refcount_destroy(&hctx->refcnt);

	mctx = hctx->mctx;
#ifdef BIND9
	if (hctx->entropy != NULL)
		isc_entropy_detach(&hctx->entropy);
#endif
	if (hctx->rndvector != NULL)
		isc_mem_put(mctx, hctx->rndvector, hctx->vectorlen);

	UNLOCK(&hctx->lock);

	DESTROYLOCK(&hctx->lock);

	memcpy(canary0, hctx + 1, sizeof(canary0));
	memset(hctx, 0, sizeof(isc_hash_t));
	memcpy(canary1, hctx + 1, sizeof(canary1));
	INSIST(memcmp(canary0, canary1, sizeof(canary0)) == 0);
	isc_mem_put(mctx, hctx, sizeof(isc_hash_t));
	isc_mem_detach(&mctx);
}

void
isc_hash_ctxdetach(isc_hash_t **hctxp) {
	isc_hash_t *hctx;
	unsigned int refs;

	REQUIRE(hctxp != NULL && VALID_HASH(*hctxp));
	hctx = *hctxp;

	isc_refcount_decrement(&hctx->refcnt, &refs);
	if (refs == 0)
		destroy(&hctx);

	*hctxp = NULL;
}

void
isc_hash_destroy() {
	unsigned int refs;

	INSIST(hash != NULL && VALID_HASH(hash));

	isc_refcount_decrement(&hash->refcnt, &refs);
	INSIST(refs == 0);

	destroy(&hash);
}

static inline unsigned int
hash_calc(isc_hash_t *hctx, const unsigned char *key, unsigned int keylen,
	  isc_boolean_t case_sensitive)
{
	hash_accum_t partial_sum = 0;
	hash_random_t *p = hctx->rndvector;
	unsigned int i = 0;

	/* Make it sure that the hash context is initialized. */
	if (hctx->initialized == ISC_FALSE)
		isc_hash_ctxinit(hctx);

	if (case_sensitive) {
		for (i = 0; i < keylen; i++)
			partial_sum += key[i] * (hash_accum_t)p[i];
	} else {
		for (i = 0; i < keylen; i++)
			partial_sum += maptolower[key[i]] * (hash_accum_t)p[i];
	}

	partial_sum += p[i];

	return ((unsigned int)(partial_sum % PRIME32));
}

unsigned int
isc_hash_ctxcalc(isc_hash_t *hctx, const unsigned char *key,
		 unsigned int keylen, isc_boolean_t case_sensitive)
{
	REQUIRE(hctx != NULL && VALID_HASH(hctx));
	REQUIRE(keylen <= hctx->limit);

	return (hash_calc(hctx, key, keylen, case_sensitive));
}

unsigned int
isc_hash_calc(const unsigned char *key, unsigned int keylen,
	      isc_boolean_t case_sensitive)
{
	INSIST(hash != NULL && VALID_HASH(hash));
	REQUIRE(keylen <= hash->limit);

	return (hash_calc(hash, key, keylen, case_sensitive));
}
