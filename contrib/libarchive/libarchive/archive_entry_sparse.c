/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * Copyright (c) 2010-2011 Michihiro NAKAJIMA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
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

#include "archive_platform.h"
__FBSDID("$FreeBSD$");

#include "archive.h"
#include "archive_entry.h"
#include "archive_private.h"
#include "archive_entry_private.h"

/*
 * sparse handling
 */

void
archive_entry_sparse_clear(struct archive_entry *entry)
{
	struct ae_sparse *sp;

	while (entry->sparse_head != NULL) {
		sp = entry->sparse_head->next;
		free(entry->sparse_head);
		entry->sparse_head = sp;
	}
	entry->sparse_tail = NULL;
}

void
archive_entry_sparse_add_entry(struct archive_entry *entry,
	la_int64_t offset, la_int64_t length)
{
	struct ae_sparse *sp;

	if (offset < 0 || length < 0)
		/* Invalid value */
		return;
	if (offset > INT64_MAX - length ||
	    offset + length > archive_entry_size(entry))
		/* A value of "length" parameter is too large. */
		return;
	if ((sp = entry->sparse_tail) != NULL) {
		if (sp->offset + sp->length > offset)
			/* Invalid value. */
			return;
		if (sp->offset + sp->length == offset) {
			if (sp->offset + sp->length + length < 0)
				/* A value of "length" parameter is
				 * too large. */
				return;
			/* Expand existing sparse block size. */
			sp->length += length;
			return;
		}
	}

	if ((sp = (struct ae_sparse *)malloc(sizeof(*sp))) == NULL)
		/* XXX Error XXX */
		return;

	sp->offset = offset;
	sp->length = length;
	sp->next = NULL;

	if (entry->sparse_head == NULL)
		entry->sparse_head = entry->sparse_tail = sp;
	else {
		/* Add a new sparse block to the tail of list. */
		if (entry->sparse_tail != NULL)
			entry->sparse_tail->next = sp;
		entry->sparse_tail = sp;
	}
}


/*
 * returns number of the sparse entries
 */
int
archive_entry_sparse_count(struct archive_entry *entry)
{
	struct ae_sparse *sp;
	int count = 0;

	for (sp = entry->sparse_head; sp != NULL; sp = sp->next)
		count++;

	/*
	 * Sanity check if this entry is exactly sparse.
	 * If amount of sparse blocks is just one and it indicates the whole
	 * file data, we should remove it and return zero.
	 */
	if (count == 1) {
		sp = entry->sparse_head;
		if (sp->offset == 0 &&
		    sp->length >= archive_entry_size(entry)) {
			count = 0;
			archive_entry_sparse_clear(entry);
		}
	}

	return (count);
}

int
archive_entry_sparse_reset(struct archive_entry * entry)
{
	entry->sparse_p = entry->sparse_head;

	return archive_entry_sparse_count(entry);
}

int
archive_entry_sparse_next(struct archive_entry * entry,
	la_int64_t *offset, la_int64_t *length)
{
	if (entry->sparse_p) {
		*offset = entry->sparse_p->offset;
		*length = entry->sparse_p->length;

		entry->sparse_p = entry->sparse_p->next;

		return (ARCHIVE_OK);
	} else {
		*offset = 0;
		*length = 0;
		return (ARCHIVE_WARN);
	}
}

/*
 * end of sparse handling
 */
