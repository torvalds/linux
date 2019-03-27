/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Routines for manipulating hash tables
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/sysmacros.h>

#include "hash.h"
#include "memory.h"
#include "list.h"

struct hash {
	int h_nbuckets;
	list_t **h_buckets;

	int (*h_hashfn)(int, void *);
	int (*h_cmp)(void *, void *);
};

struct hash_data {
	hash_t *hd_hash;
	int (*hd_fun)(void *, void *);
	void *hd_key;
	void *hd_private;

	void *hd_ret;
};

static int
hash_def_hash(int nbuckets, void *arg)
{
	uintptr_t data = (uintptr_t) arg;
	return (data % nbuckets);
}

static int
hash_def_cmp(void *d1, void *d2)
{
	return (d1 != d2);
}


int
hash_name(int nbuckets, const char *name)
{
	const char *c;
	ulong_t g;
	int h = 0;

	for (c = name; *c; c++) {
		h = (h << 4) + *c;
		if ((g = (h & 0xf0000000)) != 0) {
			h ^= (g >> 24);
			h ^= g;
		}
	}

	return (h % nbuckets);
}

hash_t *
hash_new(int nbuckets, int (*hashfn)(int, void *), int (*cmp)(void *, void *))
{
	hash_t *hash;

	hash = xmalloc(sizeof (hash_t));
	hash->h_buckets = xcalloc(sizeof (list_t *) * nbuckets);
	hash->h_nbuckets = nbuckets;
	hash->h_hashfn = hashfn ? hashfn : hash_def_hash;
	hash->h_cmp = cmp ? cmp : hash_def_cmp;

	return (hash);
}

void
hash_add(hash_t *hash, void *key)
{
	int bucket = hash->h_hashfn(hash->h_nbuckets, key);

	list_add(&hash->h_buckets[bucket], key);
}

static int
hash_add_cb(void *node, void *private)
{
	hash_add((hash_t *)private, node);
	return (0);
}

void
hash_merge(hash_t *to, hash_t *from)
{
	(void) hash_iter(from, hash_add_cb, to);
}

static int
hash_remove_cb(void *key1, void *key2, void *arg)
{
	hash_t *hash = arg;
	return (hash->h_cmp(key1, key2));
}

void
hash_remove(hash_t *hash, void *key)
{
	int bucket = hash->h_hashfn(hash->h_nbuckets, key);

	(void) list_remove(&hash->h_buckets[bucket], key,
	    hash_remove_cb, hash);
}

int
hash_match(hash_t *hash, void *key, int (*fun)(void *, void *),
    void *private)
{
	int bucket = hash->h_hashfn(hash->h_nbuckets, key);

	return (list_iter(hash->h_buckets[bucket], fun, private) < 0);
}

static int
hash_find_list_cb(void *node, void *arg)
{
	struct hash_data *hd = arg;
	int cbrc;
	int rc = 0;

	if (hd->hd_hash->h_cmp(hd->hd_key, node) == 0) {
		if ((cbrc = hd->hd_fun(node, hd->hd_private)) < 0)
			return (cbrc);
		rc += cbrc;
	}

	return (rc);
}

int
hash_find_iter(hash_t *hash, void *key, int (*fun)(void *, void *),
    void *private)
{
	int bucket = hash->h_hashfn(hash->h_nbuckets, key);
	struct hash_data hd;

	hd.hd_hash = hash;
	hd.hd_fun = fun;
	hd.hd_key = key;
	hd.hd_private = private;

	return (list_iter(hash->h_buckets[bucket], hash_find_list_cb,
	    &hd));
}

/* stop on first match */
static int
hash_find_first_cb(void *node, void *arg)
{
	struct hash_data *hd = arg;
	if (hd->hd_hash->h_cmp(hd->hd_key, node) == 0) {
		hd->hd_ret = node;
		return (-1);
	}

	return (0);
}

int
hash_find(hash_t *hash, void *key, void **value)
{
	int ret;
	struct hash_data hd;

	hd.hd_hash = hash;
	hd.hd_fun = hash_find_first_cb;
	hd.hd_key = key;

	ret = hash_match(hash, key, hash_find_first_cb, &hd);
	if (ret && value)
		*value = hd.hd_ret;

	return (ret);
}

int
hash_iter(hash_t *hash, int (*fun)(void *, void *), void *private)
{
	int cumrc = 0;
	int cbrc;
	int i;

	for (i = 0; i < hash->h_nbuckets; i++) {
		if (hash->h_buckets[i] != NULL) {
			if ((cbrc = list_iter(hash->h_buckets[i], fun,
			    private)) < 0)
				return (cbrc);
			cumrc += cbrc;
		}
	}

	return (cumrc);
}

int
hash_count(hash_t *hash)
{
	int num, i;

	for (num = 0, i = 0; i < hash->h_nbuckets; i++)
		num += list_count(hash->h_buckets[i]);

	return (num);
}

void
hash_free(hash_t *hash, void (*datafree)(void *, void *), void *private)
{
	int i;

	if (hash == NULL)
		return;

	for (i = 0; i < hash->h_nbuckets; i++)
		list_free(hash->h_buckets[i], datafree, private);
	free(hash->h_buckets);
	free(hash);
}

void
hash_stats(hash_t *hash, int verbose)
{
	int min = list_count(hash->h_buckets[0]);
	int minidx = 0;
	int max = min;
	int maxidx = 0;
	int tot = min;
	int count;
	int i;

	if (min && verbose)
		printf("%3d: %d\n", 0, min);
	for (i = 1; i < hash->h_nbuckets; i++) {
		count = list_count(hash->h_buckets[i]);
		if (min > count) {
			min = count;
			minidx = i;
		}
		if (max < count) {
			max = count;
			maxidx = i;
		}
		if (count && verbose)
			printf("%3d: %d\n", i, count);
		tot += count;
	}

	printf("Hash statistics:\n");
	printf(" Buckets: %d\n", hash->h_nbuckets);
	printf(" Items  : %d\n", tot);
	printf(" Min/Max: %d in #%d, %d in #%d\n", min, minidx, max, maxidx);
	printf(" Average: %5.2f\n", (float)tot / (float)hash->h_nbuckets);
}
