/*-
 * Copyright (c) 2013, Joseph Koshy
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/queue.h>

#include <assert.h>
#include <errno.h>
#include <gelf.h>
#include <stdlib.h>
#include <string.h>

#include "libelftc.h"
#include "_libelftc.h"

ELFTC_VCSID("$Id: elftc_string_table.c 2869 2013-01-06 13:29:18Z jkoshy $");

#define	ELFTC_STRING_TABLE_DEFAULT_SIZE			(4*1024)
#define ELFTC_STRING_TABLE_EXPECTED_STRING_SIZE		16
#define ELFTC_STRING_TABLE_EXPECTED_CHAIN_LENGTH	8
#define	ELFTC_STRING_TABLE_POOL_SIZE_INCREMENT		(4*1024)

struct _Elftc_String_Table_Entry {
	int		ste_idx;
	SLIST_ENTRY(_Elftc_String_Table_Entry) ste_next;
};

#define	ELFTC_STRING_TABLE_COMPACTION_FLAG	0x1
#define	ELFTC_STRING_TABLE_LENGTH(st)		((st)->st_len >> 1)
#define	ELFTC_STRING_TABLE_CLEAR_COMPACTION_FLAG(st) do {		\
		(st)->st_len &= ~ELFTC_STRING_TABLE_COMPACTION_FLAG;	\
	} while (0)
#define	ELFTC_STRING_TABLE_SET_COMPACTION_FLAG(st) do {			\
		(st)->st_len |= ELFTC_STRING_TABLE_COMPACTION_FLAG;	\
	} while (0)
#define	ELFTC_STRING_TABLE_UPDATE_LENGTH(st, len) do {		\
		(st)->st_len =					\
		    ((st)->st_len &				\
			ELFTC_STRING_TABLE_COMPACTION_FLAG) |	\
		    ((len) << 1);				\
	} while (0)

struct _Elftc_String_Table {
	unsigned int		st_len; /* length and flags */
	int		st_nbuckets;
	int		st_string_pool_size;
	char		*st_string_pool;
	SLIST_HEAD(_Elftc_String_Table_Bucket,
	    _Elftc_String_Table_Entry) st_buckets[];
};

static struct _Elftc_String_Table_Entry *
elftc_string_table_find_hash_entry(Elftc_String_Table *st, const char *string,
    int *rhashindex)
{
	struct _Elftc_String_Table_Entry *ste;
	int hashindex;
	char *s;

	hashindex = libelftc_hash_string(string) % st->st_nbuckets;

	if (rhashindex)
		*rhashindex = hashindex;

	SLIST_FOREACH(ste, &st->st_buckets[hashindex], ste_next) {
		s = st->st_string_pool + abs(ste->ste_idx);

		assert(s > st->st_string_pool &&
		    s < st->st_string_pool + st->st_string_pool_size);

		if (strcmp(s, string) == 0)
			return (ste);
	}

	return (NULL);
}

static int
elftc_string_table_add_to_pool(Elftc_String_Table *st, const char *string)
{
	char *newpool;
	int len, newsize, stlen;

	len = strlen(string) + 1; /* length, including the trailing NUL */
	stlen = ELFTC_STRING_TABLE_LENGTH(st);

	/* Resize the pool, if needed. */
	if (stlen + len >= st->st_string_pool_size) {
		newsize = roundup(st->st_string_pool_size +
		    ELFTC_STRING_TABLE_POOL_SIZE_INCREMENT,
		    ELFTC_STRING_TABLE_POOL_SIZE_INCREMENT);
		if ((newpool = realloc(st->st_string_pool, newsize)) ==
		    NULL)
			return (0);
		st->st_string_pool = newpool;
		st->st_string_pool_size = newsize;
	}

	strcpy(st->st_string_pool + stlen, string);
	ELFTC_STRING_TABLE_UPDATE_LENGTH(st, stlen + len);

	return (stlen);
}

Elftc_String_Table *
elftc_string_table_create(int sizehint)
{
	int n, nbuckets, tablesize;
	struct _Elftc_String_Table *st;

	if (sizehint < ELFTC_STRING_TABLE_DEFAULT_SIZE)
		sizehint = ELFTC_STRING_TABLE_DEFAULT_SIZE;

	nbuckets = sizehint / (ELFTC_STRING_TABLE_EXPECTED_CHAIN_LENGTH *
	    ELFTC_STRING_TABLE_EXPECTED_STRING_SIZE);

	tablesize = sizeof(struct _Elftc_String_Table) +
	    nbuckets * sizeof(struct _Elftc_String_Table_Bucket);

	if ((st = malloc(tablesize)) == NULL)
		return (NULL);
	if ((st->st_string_pool = malloc(sizehint)) == NULL) {
		free(st);
		return (NULL);
	}

	for (n = 0; n < nbuckets; n++)
		SLIST_INIT(&st->st_buckets[n]);

	st->st_len = 0;
	st->st_nbuckets = nbuckets;
	st->st_string_pool_size = sizehint;
	*st->st_string_pool = '\0';
	ELFTC_STRING_TABLE_UPDATE_LENGTH(st, 1);

	return (st);
}

void
elftc_string_table_destroy(Elftc_String_Table *st)
{
	int n;
	struct _Elftc_String_Table_Entry *s, *t;

	for (n = 0; n < st->st_nbuckets; n++)
		SLIST_FOREACH_SAFE(s, &st->st_buckets[n], ste_next, t)
		    free(s);
	free(st->st_string_pool);
	free(st);

	return;
}

Elftc_String_Table *
elftc_string_table_from_section(Elf_Scn *scn, int sizehint)
{
	int len;
	Elf_Data *d;
	GElf_Shdr sh;
	const char *s, *end;
	Elftc_String_Table *st;

	/* Verify the type of the section passed in. */
	if (gelf_getshdr(scn, &sh) == NULL ||
	    sh.sh_type != SHT_STRTAB) {
		errno = EINVAL;
		return (NULL);
	}

	if ((d = elf_getdata(scn, NULL)) == NULL ||
	    d->d_size == 0) {
		errno = EINVAL;
		return (NULL);
	}

	if ((st = elftc_string_table_create(sizehint)) == NULL)
		return (NULL);

	s = d->d_buf;

	/*
	 * Verify that the first byte of the data buffer is '\0'.
	 */
	if (*s != '\0') {
		errno = EINVAL;
		goto fail;
	}

	end = s + d->d_size;

	/*
	 * Skip the first '\0' and insert the strings in the buffer,
	 * in order.
	 */
	for (s += 1; s < end; s += len) {
		if (elftc_string_table_insert(st, s) == 0)
			goto fail;

		len = strlen(s) + 1; /* Include space for the trailing NUL. */
	}

	return (st);

fail:
	if (st)
		(void) elftc_string_table_destroy(st);

	return (NULL);
}

const char *
elftc_string_table_image(Elftc_String_Table *st, size_t *size)
{
	char *r, *s, *end;
	struct _Elftc_String_Table_Entry *ste;
	struct _Elftc_String_Table_Bucket *head;
	int copied, hashindex, offset, length, newsize;

	/*
	 * For the common case of a string table has not seen
	 * a string deletion, we can just export the current
	 * pool.
	 */
	if ((st->st_len & ELFTC_STRING_TABLE_COMPACTION_FLAG) == 0) {
		if (size)
			*size = ELFTC_STRING_TABLE_LENGTH(st);
		return (st->st_string_pool);
	}

	/*
	 * Otherwise, compact the string table in-place.
	 */
	assert(*st->st_string_pool == '\0');

	newsize = 1;
	end = st->st_string_pool + ELFTC_STRING_TABLE_LENGTH(st);

	for (r = s = st->st_string_pool + 1;
	     s < end;
	     s += length, r += copied) {

		copied = 0;
		length = strlen(s) + 1;

		ste = elftc_string_table_find_hash_entry(st, s,
		    &hashindex);
		head = &st->st_buckets[hashindex];

		assert(ste != NULL);

		/* Ignore deleted strings. */
		if (ste->ste_idx < 0) {
			SLIST_REMOVE(head, ste, _Elftc_String_Table_Entry,
			    ste_next);
			free(ste);
			continue;
		}

		/* Move 'live' strings up. */
		offset = newsize;
		newsize += length;
		copied = length;

		if (r == s)	/* Nothing removed yet. */
			continue;

		memmove(r, s, copied);

		/* Update the index for this entry. */
		ste->ste_idx = offset;
	}

	ELFTC_STRING_TABLE_CLEAR_COMPACTION_FLAG(st);
	ELFTC_STRING_TABLE_UPDATE_LENGTH(st, newsize);

	if (size)
		*size = newsize;

	return (st->st_string_pool);
}

size_t
elftc_string_table_insert(Elftc_String_Table *st, const char *string)
{
	int hashindex, idx;
	struct _Elftc_String_Table_Entry *ste;

	hashindex = 0;

	ste = elftc_string_table_find_hash_entry(st, string, &hashindex);

	assert(hashindex >= 0 && hashindex < st->st_nbuckets);

	if (ste == NULL) {
		if ((ste = malloc(sizeof(*ste))) == NULL)
			return (0);
		if ((ste->ste_idx = elftc_string_table_add_to_pool(st,
			    string)) == 0) {
			free(ste);
			return (0);
		}

		SLIST_INSERT_HEAD(&st->st_buckets[hashindex], ste, ste_next);
	}

	idx = ste->ste_idx;
	if (idx < 0) 		/* Undelete. */
		ste->ste_idx = idx = (- idx);

	return (idx);
}

size_t
elftc_string_table_lookup(Elftc_String_Table *st, const char *string)
{
	int hashindex, idx;
	struct _Elftc_String_Table_Entry *ste;

	ste = elftc_string_table_find_hash_entry(st, string, &hashindex);

	assert(hashindex >= 0 && hashindex < st->st_nbuckets);

	if (ste == NULL || (idx = ste->ste_idx) < 0)
		return (0);

	return (idx);
}

int
elftc_string_table_remove(Elftc_String_Table *st, const char *string)
{
	int idx;
	struct _Elftc_String_Table_Entry *ste;

	ste = elftc_string_table_find_hash_entry(st, string, NULL);

	if (ste == NULL || (idx = ste->ste_idx) < 0)
		return (ELFTC_FAILURE);

	assert(idx > 0 && idx < (int) ELFTC_STRING_TABLE_LENGTH(st));

	ste->ste_idx = (- idx);

	ELFTC_STRING_TABLE_SET_COMPACTION_FLAG(st);

	return (ELFTC_SUCCESS);
}

const char *
elftc_string_table_to_string(Elftc_String_Table *st, size_t offset)
{
	const char *s;

	s = st->st_string_pool + offset;

	/*
	 * Check for:
	 * - An offset value within pool bounds.
	 * - A non-NUL byte at the specified offset.
	 * - The end of the prior string at offset - 1.
	 */
	if (offset == 0 || offset >= ELFTC_STRING_TABLE_LENGTH(st) ||
	    *s == '\0' || *(s - 1) != '\0') {
		errno = EINVAL;
		return (NULL);
	}

	return (s);
}
