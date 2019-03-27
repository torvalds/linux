/* Copyright (c) 2013, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "ucl_internal.h"
#include "ucl_hash.h"
#include "khash.h"
#include "kvec.h"
#include "mum.h"

#include <time.h>
#include <limits.h>

struct ucl_hash_elt {
	const ucl_object_t *obj;
	size_t ar_idx;
};

struct ucl_hash_struct {
	void *hash;
	kvec_t(const ucl_object_t *) ar;
	bool caseless;
};

static uint64_t
ucl_hash_seed (void)
{
	static uint64_t seed;

	if (seed == 0) {
#ifdef UCL_RANDOM_FUNCTION
		seed = UCL_RANDOM_FUNCTION;
#else
		/* Not very random but can be useful for our purposes */
		seed = time (NULL);
#endif
	}

	return seed;
}

static const unsigned char lc_map[256] = {
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

#if (defined(WORD_BIT) && WORD_BIT == 64) || \
	(defined(__WORDSIZE) && __WORDSIZE == 64) || \
	defined(__x86_64__) || \
	defined(__amd64__)
#define UCL64_BIT_HASH 1
#endif

static inline uint32_t
ucl_hash_func (const ucl_object_t *o)
{
	return mum_hash (o->key, o->keylen, ucl_hash_seed ());
}
static inline int
ucl_hash_equal (const ucl_object_t *k1, const ucl_object_t *k2)
{
	if (k1->keylen == k2->keylen) {
		return memcmp (k1->key, k2->key, k1->keylen) == 0;
	}

	return 0;
}

KHASH_INIT (ucl_hash_node, const ucl_object_t *, struct ucl_hash_elt, 1,
		ucl_hash_func, ucl_hash_equal)

static inline uint32_t
ucl_hash_caseless_func (const ucl_object_t *o)
{
	unsigned len = o->keylen;
	unsigned leftover = o->keylen % 8;
	unsigned fp, i;
	const uint8_t* s = (const uint8_t*)o->key;
	union {
		struct {
			unsigned char c1, c2, c3, c4, c5, c6, c7, c8;
		} c;
		uint64_t pp;
	} u;
	uint64_t r;

	fp = len - leftover;
	r = ucl_hash_seed ();

	for (i = 0; i != fp; i += 8) {
		u.c.c1 = s[i], u.c.c2 = s[i + 1], u.c.c3 = s[i + 2], u.c.c4 = s[i + 3];
		u.c.c5 = s[i + 4], u.c.c6 = s[i + 5], u.c.c7 = s[i + 6], u.c.c8 = s[i + 7];
		u.c.c1 = lc_map[u.c.c1];
		u.c.c2 = lc_map[u.c.c2];
		u.c.c3 = lc_map[u.c.c3];
		u.c.c4 = lc_map[u.c.c4];
		u.c.c1 = lc_map[u.c.c5];
		u.c.c2 = lc_map[u.c.c6];
		u.c.c3 = lc_map[u.c.c7];
		u.c.c4 = lc_map[u.c.c8];
		r = mum_hash_step (r, u.pp);
	}

	u.pp = 0;
	switch (leftover) {
	case 7:
		u.c.c7 = lc_map[(unsigned char)s[i++]];
	case 6:
		u.c.c6 = lc_map[(unsigned char)s[i++]];
	case 5:
		u.c.c5 = lc_map[(unsigned char)s[i++]];
	case 4:
		u.c.c4 = lc_map[(unsigned char)s[i++]];
	case 3:
		u.c.c3 = lc_map[(unsigned char)s[i++]];
	case 2:
		u.c.c2 = lc_map[(unsigned char)s[i++]];
	case 1:
		u.c.c1 = lc_map[(unsigned char)s[i]];
		r = mum_hash_step (r, u.pp);
		break;
	}

	return mum_hash_finish (r);
}

static inline int
ucl_hash_caseless_equal (const ucl_object_t *k1, const ucl_object_t *k2)
{
	if (k1->keylen == k2->keylen) {
		return memcmp (k1->key, k2->key, k1->keylen) == 0;
	}

	return 0;
}

KHASH_INIT (ucl_hash_caseless_node, const ucl_object_t *, struct ucl_hash_elt, 1,
		ucl_hash_caseless_func, ucl_hash_caseless_equal)

ucl_hash_t*
ucl_hash_create (bool ignore_case)
{
	ucl_hash_t *new;

	new = UCL_ALLOC (sizeof (ucl_hash_t));
	if (new != NULL) {
		kv_init (new->ar);

		new->caseless = ignore_case;
		if (ignore_case) {
			khash_t(ucl_hash_caseless_node) *h = kh_init (ucl_hash_caseless_node);
			new->hash = (void *)h;
		}
		else {
			khash_t(ucl_hash_node) *h = kh_init (ucl_hash_node);
			new->hash = (void *)h;
		}
	}
	return new;
}

void ucl_hash_destroy (ucl_hash_t* hashlin, ucl_hash_free_func func)
{
	const ucl_object_t *cur, *tmp;

	if (hashlin == NULL) {
		return;
	}

	if (func != NULL) {
		/* Iterate over the hash first */
		khash_t(ucl_hash_node) *h = (khash_t(ucl_hash_node) *)
				hashlin->hash;
		khiter_t k;

		for (k = kh_begin (h); k != kh_end (h); ++k) {
			if (kh_exist (h, k)) {
				cur = (kh_value (h, k)).obj;
				while (cur != NULL) {
					tmp = cur->next;
					func (__DECONST (ucl_object_t *, cur));
					cur = tmp;
				}
			}
		}
	}

	if (hashlin->caseless) {
		khash_t(ucl_hash_caseless_node) *h = (khash_t(ucl_hash_caseless_node) *)
			hashlin->hash;
		kh_destroy (ucl_hash_caseless_node, h);
	}
	else {
		khash_t(ucl_hash_node) *h = (khash_t(ucl_hash_node) *)
			hashlin->hash;
		kh_destroy (ucl_hash_node, h);
	}

	kv_destroy (hashlin->ar);
	UCL_FREE (sizeof (*hashlin), hashlin);
}

void
ucl_hash_insert (ucl_hash_t* hashlin, const ucl_object_t *obj,
		const char *key, unsigned keylen)
{
	khiter_t k;
	int ret;
	struct ucl_hash_elt *elt;

	if (hashlin == NULL) {
		return;
	}

	if (hashlin->caseless) {
		khash_t(ucl_hash_caseless_node) *h = (khash_t(ucl_hash_caseless_node) *)
				hashlin->hash;
		k = kh_put (ucl_hash_caseless_node, h, obj, &ret);
		if (ret > 0) {
			elt = &kh_value (h, k);
			kv_push (const ucl_object_t *, hashlin->ar, obj);
			elt->obj = obj;
			elt->ar_idx = kv_size (hashlin->ar) - 1;
		}
	}
	else {
		khash_t(ucl_hash_node) *h = (khash_t(ucl_hash_node) *)
				hashlin->hash;
		k = kh_put (ucl_hash_node, h, obj, &ret);
		if (ret > 0) {
			elt = &kh_value (h, k);
			kv_push (const ucl_object_t *, hashlin->ar, obj);
			elt->obj = obj;
			elt->ar_idx = kv_size (hashlin->ar) - 1;
		}
	}
}

void ucl_hash_replace (ucl_hash_t* hashlin, const ucl_object_t *old,
		const ucl_object_t *new)
{
	khiter_t k;
	int ret;
	struct ucl_hash_elt elt, *pelt;

	if (hashlin == NULL) {
		return;
	}

	if (hashlin->caseless) {
		khash_t(ucl_hash_caseless_node) *h = (khash_t(ucl_hash_caseless_node) *)
				hashlin->hash;
		k = kh_put (ucl_hash_caseless_node, h, old, &ret);
		if (ret == 0) {
			elt = kh_value (h, k);
			kh_del (ucl_hash_caseless_node, h, k);
			k = kh_put (ucl_hash_caseless_node, h, new, &ret);
			pelt = &kh_value (h, k);
			pelt->obj = new;
			pelt->ar_idx = elt.ar_idx;
			kv_A (hashlin->ar, elt.ar_idx) = new;
		}
	}
	else {
		khash_t(ucl_hash_node) *h = (khash_t(ucl_hash_node) *)
				hashlin->hash;
		k = kh_put (ucl_hash_node, h, old, &ret);
		if (ret == 0) {
			elt = kh_value (h, k);
			kh_del (ucl_hash_node, h, k);
			k = kh_put (ucl_hash_node, h, new, &ret);
			pelt = &kh_value (h, k);
			pelt->obj = new;
			pelt->ar_idx = elt.ar_idx;
			kv_A (hashlin->ar, elt.ar_idx) = new;
		}
	}
}

struct ucl_hash_real_iter {
	const ucl_object_t **cur;
	const ucl_object_t **end;
};

const void*
ucl_hash_iterate (ucl_hash_t *hashlin, ucl_hash_iter_t *iter)
{
	struct ucl_hash_real_iter *it = (struct ucl_hash_real_iter *)(*iter);
	const ucl_object_t *ret = NULL;

	if (hashlin == NULL) {
		return NULL;
	}

	if (it == NULL) {
		it = UCL_ALLOC (sizeof (*it));

		if (it == NULL) {
			return NULL;
		}

		it->cur = &hashlin->ar.a[0];
		it->end = it->cur + hashlin->ar.n;
	}

	if (it->cur < it->end) {
		ret = *it->cur++;
	}
	else {
		UCL_FREE (sizeof (*it), it);
		*iter = NULL;
		return NULL;
	}

	*iter = it;

	return ret;
}

bool
ucl_hash_iter_has_next (ucl_hash_t *hashlin, ucl_hash_iter_t iter)
{
	struct ucl_hash_real_iter *it = (struct ucl_hash_real_iter *)(iter);

	return it->cur < it->end - 1;
}


const ucl_object_t*
ucl_hash_search (ucl_hash_t* hashlin, const char *key, unsigned keylen)
{
	khiter_t k;
	const ucl_object_t *ret = NULL;
	ucl_object_t search;
	struct ucl_hash_elt *elt;

	search.key = key;
	search.keylen = keylen;

	if (hashlin == NULL) {
		return NULL;
	}

	if (hashlin->caseless) {
		khash_t(ucl_hash_caseless_node) *h = (khash_t(ucl_hash_caseless_node) *)
						hashlin->hash;

		k = kh_get (ucl_hash_caseless_node, h, &search);
		if (k != kh_end (h)) {
			elt = &kh_value (h, k);
			ret = elt->obj;
		}
	}
	else {
		khash_t(ucl_hash_node) *h = (khash_t(ucl_hash_node) *)
						hashlin->hash;
		k = kh_get (ucl_hash_node, h, &search);
		if (k != kh_end (h)) {
			elt = &kh_value (h, k);
			ret = elt->obj;
		}
	}

	return ret;
}

void
ucl_hash_delete (ucl_hash_t* hashlin, const ucl_object_t *obj)
{
	khiter_t k;
	struct ucl_hash_elt *elt;

	if (hashlin == NULL) {
		return;
	}

	if (hashlin->caseless) {
		khash_t(ucl_hash_caseless_node) *h = (khash_t(ucl_hash_caseless_node) *)
			hashlin->hash;

		k = kh_get (ucl_hash_caseless_node, h, obj);
		if (k != kh_end (h)) {
			elt = &kh_value (h, k);
			kv_del (const ucl_object_t *, hashlin->ar, elt->ar_idx);
			kh_del (ucl_hash_caseless_node, h, k);
		}
	}
	else {
		khash_t(ucl_hash_node) *h = (khash_t(ucl_hash_node) *)
			hashlin->hash;
		k = kh_get (ucl_hash_node, h, obj);
		if (k != kh_end (h)) {
			elt = &kh_value (h, k);
			kv_del (const ucl_object_t *, hashlin->ar, elt->ar_idx);
			kh_del (ucl_hash_node, h, k);
		}
	}
}
