/*	$Id: dba_array.c,v 1.1 2016/07/19 21:31:55 schwarze Exp $ */
/*
 * Copyright (c) 2016 Ingo Schwarze <schwarze@openbsd.org>
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
 *
 * Allocation-based arrays for the mandoc database, for read-write access.
 * The interface is defined in "dba_array.h".
 */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mandoc_aux.h"
#include "dba_write.h"
#include "dba_array.h"

struct dba_array {
	void	**ep;	/* Array of entries. */
	int32_t	 *em;	/* Array of map positions. */
	int	  flags;
	int32_t	  ea;	/* Entries allocated. */
	int32_t	  eu;	/* Entries used (including deleted). */
	int32_t	  ed;	/* Entries deleted. */
	int32_t	  ec;	/* Currently active entry. */
	int32_t	  pos;  /* Map position of this array. */
};


struct dba_array *
dba_array_new(int32_t ea, int flags)
{
	struct dba_array	*array;

	assert(ea > 0);
	array = mandoc_malloc(sizeof(*array));
	array->ep = mandoc_reallocarray(NULL, ea, sizeof(*array->ep));
	array->em = mandoc_reallocarray(NULL, ea, sizeof(*array->em));
	array->ea = ea;
	array->eu = 0;
	array->ed = 0;
	array->ec = 0;
	array->flags = flags;
	array->pos = 0;
	return array;
}

void
dba_array_free(struct dba_array *array)
{
	int32_t	 ie;

	if (array == NULL)
		return;
	if (array->flags & DBA_STR)
		for (ie = 0; ie < array->eu; ie++)
			free(array->ep[ie]);
	free(array->ep);
	free(array->em);
	free(array);
}

void
dba_array_set(struct dba_array *array, int32_t ie, void *entry)
{
	assert(ie >= 0);
	assert(ie < array->ea);
	assert(ie <= array->eu);
	if (ie == array->eu)
		array->eu++;
	if (array->flags & DBA_STR)
		entry = mandoc_strdup(entry);
	array->ep[ie] = entry;
	array->em[ie] = 0;
}

void
dba_array_add(struct dba_array *array, void *entry)
{
	if (array->eu == array->ea) {
		assert(array->flags & DBA_GROW);
		array->ep = mandoc_reallocarray(array->ep,
		    2, sizeof(*array->ep) * array->ea);
		array->em = mandoc_reallocarray(array->em,
		    2, sizeof(*array->em) * array->ea);
		array->ea *= 2;
	}
	dba_array_set(array, array->eu, entry);
}

void *
dba_array_get(struct dba_array *array, int32_t ie)
{
	if (ie < 0 || ie >= array->eu || array->em[ie] == -1)
		return NULL;
	return array->ep[ie];
}

void
dba_array_start(struct dba_array *array)
{
	array->ec = array->eu;
}

void *
dba_array_next(struct dba_array *array)
{
	if (array->ec < array->eu)
		array->ec++;
	else
		array->ec = 0;
	while (array->ec < array->eu && array->em[array->ec] == -1)
		array->ec++;
	return array->ec < array->eu ? array->ep[array->ec] : NULL;
}

void
dba_array_del(struct dba_array *array)
{
	if (array->ec < array->eu && array->em[array->ec] != -1) {
		array->em[array->ec] = -1;
		array->ed++;
	}
}

void
dba_array_undel(struct dba_array *array)
{
	memset(array->em, 0, sizeof(*array->em) * array->eu);
}

void
dba_array_setpos(struct dba_array *array, int32_t ie, int32_t pos)
{
	array->em[ie] = pos;
}

int32_t
dba_array_getpos(struct dba_array *array)
{
	return array->pos;
}

void
dba_array_sort(struct dba_array *array, dba_compare_func func)
{
	assert(array->ed == 0);
	qsort(array->ep, array->eu, sizeof(*array->ep), func);
}

int32_t
dba_array_writelen(struct dba_array *array, int32_t nmemb)
{
	dba_int_write(array->eu - array->ed);
	return dba_skip(nmemb, array->eu - array->ed);
}

void
dba_array_writepos(struct dba_array *array)
{
	int32_t	 ie;

	array->pos = dba_tell();
	for (ie = 0; ie < array->eu; ie++)
		if (array->em[ie] != -1)
			dba_int_write(array->em[ie]);
}

void
dba_array_writelst(struct dba_array *array)
{
	const char	*str;

	dba_array_FOREACH(array, str)
		dba_str_write(str);
	dba_char_write('\0');
}
