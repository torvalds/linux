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
 * Copyright (c) 2001 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

#include "strtab.h"
#include "memory.h"

#define	STRTAB_HASHSZ	211		/* use a prime number of hash buckets */
#define	STRTAB_BUFSZ	(64 * 1024)	/* use 64K data buffers by default */

static void
strtab_grow(strtab_t *sp)
{
	sp->str_nbufs++;
	sp->str_bufs = xrealloc(sp->str_bufs, sp->str_nbufs * sizeof (char *));
	sp->str_ptr = xmalloc(sp->str_bufsz);
	sp->str_bufs[sp->str_nbufs - 1] = sp->str_ptr;
}

void
strtab_create(strtab_t *sp)
{
	sp->str_hash = xcalloc(STRTAB_HASHSZ * sizeof (strhash_t *));
	sp->str_hashsz = STRTAB_HASHSZ;
	sp->str_bufs = NULL;
	sp->str_ptr = NULL;
	sp->str_nbufs = 0;
	sp->str_bufsz = STRTAB_BUFSZ;
	sp->str_nstrs = 1;
	sp->str_size = 1;

	strtab_grow(sp);
	*sp->str_ptr++ = '\0';
}

void
strtab_destroy(strtab_t *sp)
{
	strhash_t *hp, *hq;
	ulong_t i;

	for (i = 0; i < sp->str_hashsz; i++) {
		for (hp = sp->str_hash[i]; hp != NULL; hp = hq) {
			hq = hp->str_next;
			free(hp);
		}
	}

	for (i = 0; i < sp->str_nbufs; i++)
		free(sp->str_bufs[i]);

	free(sp->str_hash);
	free(sp->str_bufs);
}

static ulong_t
strtab_hash(const char *key, size_t *len)
{
	ulong_t g, h = 0;
	const char *p;
	size_t n = 0;

	for (p = key; *p != '\0'; p++, n++) {
		h = (h << 4) + *p;

		if ((g = (h & 0xf0000000)) != 0) {
			h ^= (g >> 24);
			h ^= g;
		}
	}

	*len = n;
	return (h);
}

static int
strtab_compare(strtab_t *sp, strhash_t *hp, const char *str, size_t len)
{
	ulong_t b = hp->str_buf;
	const char *buf = hp->str_data;
	size_t resid, n;
	int rv;

	while (len != 0) {
		if (buf == sp->str_bufs[b] + sp->str_bufsz)
			buf = sp->str_bufs[++b];

		resid = sp->str_bufs[b] + sp->str_bufsz - buf;
		n = MIN(resid, len);

		if ((rv = strncmp(buf, str, n)) != 0)
			return (rv);

		buf += n;
		str += n;
		len -= n;
	}

	return (0);
}

static void
strtab_copyin(strtab_t *sp, const char *str, size_t len)
{
	ulong_t b = sp->str_nbufs - 1;
	size_t resid, n;

	while (len != 0) {
		if (sp->str_ptr == sp->str_bufs[b] + sp->str_bufsz) {
			strtab_grow(sp);
			b++;
		}

		resid = sp->str_bufs[b] + sp->str_bufsz - sp->str_ptr;
		n = MIN(resid, len);
		bcopy(str, sp->str_ptr, n);

		sp->str_ptr += n;
		str += n;
		len -= n;
	}
}

size_t
strtab_insert(strtab_t *sp, const char *str)
{
	strhash_t *hp;
	size_t len;
	ulong_t h;

	if (str == NULL || str[0] == '\0')
		return (0); /* we keep a \0 at offset 0 to simplify things */

	h = strtab_hash(str, &len) % sp->str_hashsz;

	/*
	 * If the string is already in our hash table, just return the offset
	 * of the existing string element and do not add a duplicate string.
	 */
	for (hp = sp->str_hash[h]; hp != NULL; hp = hp->str_next) {
		if (strtab_compare(sp, hp, str, len + 1) == 0)
			return (hp->str_off);
	}

	/*
	 * Create a new hash bucket, initialize it, and insert it at the front
	 * of the hash chain for the appropriate bucket.
	 */
	hp = xmalloc(sizeof (strhash_t));

	hp->str_data = sp->str_ptr;
	hp->str_buf = sp->str_nbufs - 1;
	hp->str_off = sp->str_size;
	hp->str_len = len;
	hp->str_next = sp->str_hash[h];

	sp->str_hash[h] = hp;

	/*
	 * Now copy the string data into our buffer list, and then update
	 * the global counts of strings and bytes.  Return str's byte offset.
	 */
	strtab_copyin(sp, str, len + 1);
	sp->str_nstrs++;
	sp->str_size += len + 1;

	return (hp->str_off);
}

size_t
strtab_size(const strtab_t *sp)
{
	return (sp->str_size);
}

ssize_t
strtab_write(const strtab_t *sp,
    ssize_t (*func)(void *, size_t, void *), void *priv)
{
	ssize_t res, total = 0;
	ulong_t i;
	size_t n;

	for (i = 0; i < sp->str_nbufs; i++, total += res) {
		if (i == sp->str_nbufs - 1)
			n = sp->str_ptr - sp->str_bufs[i];
		else
			n = sp->str_bufsz;

		if ((res = func(sp->str_bufs[i], n, priv)) <= 0)
			break;
	}

	if (total == 0 && sp->str_size != 0)
		return (-1);

	return (total);
}

void
strtab_print(const strtab_t *sp)
{
	const strhash_t *hp;
	ulong_t i;

	for (i = 0; i < sp->str_hashsz; i++) {
		for (hp = sp->str_hash[i]; hp != NULL; hp = hp->str_next) {
			const char *buf = hp->str_data;
			ulong_t b = hp->str_buf;
			size_t resid, len, n;

			(void) printf("[%lu] %lu \"", (ulong_t)hp->str_off, b);

			for (len = hp->str_len; len != 0; len -= n) {
				if (buf == sp->str_bufs[b] + sp->str_bufsz)
					buf = sp->str_bufs[++b];

				resid = sp->str_bufs[b] + sp->str_bufsz - buf;
				n = MIN(resid, len);

				(void) printf("%.*s", (int)n, buf);
				buf += n;
			}

			(void) printf("\"\n");
		}
	}
}
