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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <ctf_impl.h>

static const ushort_t _CTF_EMPTY[1] = { 0 };

int
ctf_hash_create(ctf_hash_t *hp, ulong_t nelems)
{
	if (nelems > USHRT_MAX)
		return (EOVERFLOW);

	/*
	 * If the hash table is going to be empty, don't bother allocating any
	 * memory and make the only bucket point to a zero so lookups fail.
	 */
	if (nelems == 0) {
		bzero(hp, sizeof (ctf_hash_t));
		hp->h_buckets = (ushort_t *)_CTF_EMPTY;
		hp->h_nbuckets = 1;
		return (0);
	}

	hp->h_nbuckets = 211;		/* use a prime number of hash buckets */
	hp->h_nelems = nelems + 1;	/* we use index zero as a sentinel */
	hp->h_free = 1;			/* first free element is index 1 */

	hp->h_buckets = ctf_alloc(sizeof (ushort_t) * hp->h_nbuckets);
	hp->h_chains = ctf_alloc(sizeof (ctf_helem_t) * hp->h_nelems);

	if (hp->h_buckets == NULL || hp->h_chains == NULL) {
		ctf_hash_destroy(hp);
		return (EAGAIN);
	}

	bzero(hp->h_buckets, sizeof (ushort_t) * hp->h_nbuckets);
	bzero(hp->h_chains, sizeof (ctf_helem_t) * hp->h_nelems);

	return (0);
}

uint_t
ctf_hash_size(const ctf_hash_t *hp)
{
	return (hp->h_nelems ? hp->h_nelems - 1 : 0);
}

static ulong_t
ctf_hash_compute(const char *key, size_t len)
{
	ulong_t g, h = 0;
	const char *p, *q = key + len;
	size_t n = 0;

	for (p = key; p < q; p++, n++) {
		h = (h << 4) + *p;

		if ((g = (h & 0xf0000000)) != 0) {
			h ^= (g >> 24);
			h ^= g;
		}
	}

	return (h);
}

int
ctf_hash_insert(ctf_hash_t *hp, ctf_file_t *fp, ushort_t type, uint_t name)
{
	ctf_strs_t *ctsp = &fp->ctf_str[CTF_NAME_STID(name)];
	const char *str = ctsp->cts_strs + CTF_NAME_OFFSET(name);
	ctf_helem_t *hep = &hp->h_chains[hp->h_free];
	ulong_t h;

	if (type == 0)
		return (EINVAL);

	if (hp->h_free >= hp->h_nelems)
		return (EOVERFLOW);

	if (ctsp->cts_strs == NULL)
		return (ECTF_STRTAB);

	if (ctsp->cts_len <= CTF_NAME_OFFSET(name))
		return (ECTF_BADNAME);

	if (str[0] == '\0')
		return (0); /* just ignore empty strings on behalf of caller */

	hep->h_name = name;
	hep->h_type = type;
	h = ctf_hash_compute(str, strlen(str)) % hp->h_nbuckets;
	hep->h_next = hp->h_buckets[h];
	hp->h_buckets[h] = hp->h_free++;

	return (0);
}

/*
 * Wrapper for ctf_hash_lookup/ctf_hash_insert: if the key is already in the
 * hash, override the previous definition with this new official definition.
 * If the key is not present, then call ctf_hash_insert() and hash it in.
 */
int
ctf_hash_define(ctf_hash_t *hp, ctf_file_t *fp, ushort_t type, uint_t name)
{
	const char *str = ctf_strptr(fp, name);
	ctf_helem_t *hep = ctf_hash_lookup(hp, fp, str, strlen(str));

	if (hep == NULL)
		return (ctf_hash_insert(hp, fp, type, name));

	hep->h_type = type;
	return (0);
}

ctf_helem_t *
ctf_hash_lookup(ctf_hash_t *hp, ctf_file_t *fp, const char *key, size_t len)
{
	ctf_helem_t *hep;
	ctf_strs_t *ctsp;
	const char *str;
	ushort_t i;

	ulong_t h = ctf_hash_compute(key, len) % hp->h_nbuckets;

	for (i = hp->h_buckets[h]; i != 0; i = hep->h_next) {
		hep = &hp->h_chains[i];
		ctsp = &fp->ctf_str[CTF_NAME_STID(hep->h_name)];
		str = ctsp->cts_strs + CTF_NAME_OFFSET(hep->h_name);

		if (strncmp(key, str, len) == 0 && str[len] == '\0')
			return (hep);
	}

	return (NULL);
}

void
ctf_hash_destroy(ctf_hash_t *hp)
{
	if (hp->h_buckets != NULL && hp->h_nbuckets != 1) {
		ctf_free(hp->h_buckets, sizeof (ushort_t) * hp->h_nbuckets);
		hp->h_buckets = NULL;
	}

	if (hp->h_chains != NULL) {
		ctf_free(hp->h_chains, sizeof (ctf_helem_t) * hp->h_nelems);
		hp->h_chains = NULL;
	}
}
