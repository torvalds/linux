/*-
 * Copyright (c) 2003-2011 Tim Kientzle
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

/*
 * This file contains the "essential" portions of the read API, that
 * is, stuff that will probably always be used by any client that
 * actually needs to read an archive.  Optional pieces have been, as
 * far as possible, separated out into separate files to avoid
 * needlessly bloating statically-linked clients.
 */

#include "archive_platform.h"
__FBSDID("$FreeBSD$");

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "archive.h"
#include "archive_entry.h"
#include "archive_private.h"
#include "archive_read_private.h"

#define minimum(a, b) (a < b ? a : b)

static int	choose_filters(struct archive_read *);
static int	choose_format(struct archive_read *);
static int	close_filters(struct archive_read *);
static struct archive_vtable *archive_read_vtable(void);
static int64_t	_archive_filter_bytes(struct archive *, int);
static int	_archive_filter_code(struct archive *, int);
static const char *_archive_filter_name(struct archive *, int);
static int  _archive_filter_count(struct archive *);
static int	_archive_read_close(struct archive *);
static int	_archive_read_data_block(struct archive *,
		    const void **, size_t *, int64_t *);
static int	_archive_read_free(struct archive *);
static int	_archive_read_next_header(struct archive *,
		    struct archive_entry **);
static int	_archive_read_next_header2(struct archive *,
		    struct archive_entry *);
static int64_t  advance_file_pointer(struct archive_read_filter *, int64_t);

static struct archive_vtable *
archive_read_vtable(void)
{
	static struct archive_vtable av;
	static int inited = 0;

	if (!inited) {
		av.archive_filter_bytes = _archive_filter_bytes;
		av.archive_filter_code = _archive_filter_code;
		av.archive_filter_name = _archive_filter_name;
		av.archive_filter_count = _archive_filter_count;
		av.archive_read_data_block = _archive_read_data_block;
		av.archive_read_next_header = _archive_read_next_header;
		av.archive_read_next_header2 = _archive_read_next_header2;
		av.archive_free = _archive_read_free;
		av.archive_close = _archive_read_close;
		inited = 1;
	}
	return (&av);
}

/*
 * Allocate, initialize and return a struct archive object.
 */
struct archive *
archive_read_new(void)
{
	struct archive_read *a;

	a = (struct archive_read *)calloc(1, sizeof(*a));
	if (a == NULL)
		return (NULL);
	a->archive.magic = ARCHIVE_READ_MAGIC;

	a->archive.state = ARCHIVE_STATE_NEW;
	a->entry = archive_entry_new2(&a->archive);
	a->archive.vtable = archive_read_vtable();

	a->passphrases.last = &a->passphrases.first;

	return (&a->archive);
}

/*
 * Record the do-not-extract-to file. This belongs in archive_read_extract.c.
 */
void
archive_read_extract_set_skip_file(struct archive *_a, la_int64_t d,
    la_int64_t i)
{
	struct archive_read *a = (struct archive_read *)_a;

	if (ARCHIVE_OK != __archive_check_magic(_a, ARCHIVE_READ_MAGIC,
		ARCHIVE_STATE_ANY, "archive_read_extract_set_skip_file"))
		return;
	a->skip_file_set = 1;
	a->skip_file_dev = d;
	a->skip_file_ino = i;
}

/*
 * Open the archive
 */
int
archive_read_open(struct archive *a, void *client_data,
    archive_open_callback *client_opener, archive_read_callback *client_reader,
    archive_close_callback *client_closer)
{
	/* Old archive_read_open() is just a thin shell around
	 * archive_read_open1. */
	archive_read_set_open_callback(a, client_opener);
	archive_read_set_read_callback(a, client_reader);
	archive_read_set_close_callback(a, client_closer);
	archive_read_set_callback_data(a, client_data);
	return archive_read_open1(a);
}


int
archive_read_open2(struct archive *a, void *client_data,
    archive_open_callback *client_opener,
    archive_read_callback *client_reader,
    archive_skip_callback *client_skipper,
    archive_close_callback *client_closer)
{
	/* Old archive_read_open2() is just a thin shell around
	 * archive_read_open1. */
	archive_read_set_callback_data(a, client_data);
	archive_read_set_open_callback(a, client_opener);
	archive_read_set_read_callback(a, client_reader);
	archive_read_set_skip_callback(a, client_skipper);
	archive_read_set_close_callback(a, client_closer);
	return archive_read_open1(a);
}

static ssize_t
client_read_proxy(struct archive_read_filter *self, const void **buff)
{
	ssize_t r;
	r = (self->archive->client.reader)(&self->archive->archive,
	    self->data, buff);
	return (r);
}

static int64_t
client_skip_proxy(struct archive_read_filter *self, int64_t request)
{
	if (request < 0)
		__archive_errx(1, "Negative skip requested.");
	if (request == 0)
		return 0;

	if (self->archive->client.skipper != NULL) {
		/* Seek requests over 1GiB are broken down into
		 * multiple seeks.  This avoids overflows when the
		 * requests get passed through 32-bit arguments. */
		int64_t skip_limit = (int64_t)1 << 30;
		int64_t total = 0;
		for (;;) {
			int64_t get, ask = request;
			if (ask > skip_limit)
				ask = skip_limit;
			get = (self->archive->client.skipper)
				(&self->archive->archive, self->data, ask);
			total += get;
			if (get == 0 || get == request)
				return (total);
			if (get > request)
				return ARCHIVE_FATAL;
			request -= get;
		}
	} else if (self->archive->client.seeker != NULL
		&& request > 64 * 1024) {
		/* If the client provided a seeker but not a skipper,
		 * we can use the seeker to skip forward.
		 *
		 * Note: This isn't always a good idea.  The client
		 * skipper is allowed to skip by less than requested
		 * if it needs to maintain block alignment.  The
		 * seeker is not allowed to play such games, so using
		 * the seeker here may be a performance loss compared
		 * to just reading and discarding.  That's why we
		 * only do this for skips of over 64k.
		 */
		int64_t before = self->position;
		int64_t after = (self->archive->client.seeker)
		    (&self->archive->archive, self->data, request, SEEK_CUR);
		if (after != before + request)
			return ARCHIVE_FATAL;
		return after - before;
	}
	return 0;
}

static int64_t
client_seek_proxy(struct archive_read_filter *self, int64_t offset, int whence)
{
	/* DO NOT use the skipper here!  If we transparently handled
	 * forward seek here by using the skipper, that will break
	 * other libarchive code that assumes a successful forward
	 * seek means it can also seek backwards.
	 */
	if (self->archive->client.seeker == NULL) {
		archive_set_error(&self->archive->archive, ARCHIVE_ERRNO_MISC,
		    "Current client reader does not support seeking a device");
		return (ARCHIVE_FAILED);
	}
	return (self->archive->client.seeker)(&self->archive->archive,
	    self->data, offset, whence);
}

static int
client_close_proxy(struct archive_read_filter *self)
{
	int r = ARCHIVE_OK, r2;
	unsigned int i;

	if (self->archive->client.closer == NULL)
		return (r);
	for (i = 0; i < self->archive->client.nodes; i++)
	{
		r2 = (self->archive->client.closer)
			((struct archive *)self->archive,
				self->archive->client.dataset[i].data);
		if (r > r2)
			r = r2;
	}
	return (r);
}

static int
client_open_proxy(struct archive_read_filter *self)
{
  int r = ARCHIVE_OK;
	if (self->archive->client.opener != NULL)
		r = (self->archive->client.opener)(
		    (struct archive *)self->archive, self->data);
	return (r);
}

static int
client_switch_proxy(struct archive_read_filter *self, unsigned int iindex)
{
  int r1 = ARCHIVE_OK, r2 = ARCHIVE_OK;
	void *data2 = NULL;

	/* Don't do anything if already in the specified data node */
	if (self->archive->client.cursor == iindex)
		return (ARCHIVE_OK);

	self->archive->client.cursor = iindex;
	data2 = self->archive->client.dataset[self->archive->client.cursor].data;
	if (self->archive->client.switcher != NULL)
	{
		r1 = r2 = (self->archive->client.switcher)
			((struct archive *)self->archive, self->data, data2);
		self->data = data2;
	}
	else
	{
		/* Attempt to call close and open instead */
		if (self->archive->client.closer != NULL)
			r1 = (self->archive->client.closer)
				((struct archive *)self->archive, self->data);
		self->data = data2;
		if (self->archive->client.opener != NULL)
			r2 = (self->archive->client.opener)
				((struct archive *)self->archive, self->data);
	}
	return (r1 < r2) ? r1 : r2;
}

int
archive_read_set_open_callback(struct archive *_a,
    archive_open_callback *client_opener)
{
	struct archive_read *a = (struct archive_read *)_a;
	archive_check_magic(_a, ARCHIVE_READ_MAGIC, ARCHIVE_STATE_NEW,
	    "archive_read_set_open_callback");
	a->client.opener = client_opener;
	return ARCHIVE_OK;
}

int
archive_read_set_read_callback(struct archive *_a,
    archive_read_callback *client_reader)
{
	struct archive_read *a = (struct archive_read *)_a;
	archive_check_magic(_a, ARCHIVE_READ_MAGIC, ARCHIVE_STATE_NEW,
	    "archive_read_set_read_callback");
	a->client.reader = client_reader;
	return ARCHIVE_OK;
}

int
archive_read_set_skip_callback(struct archive *_a,
    archive_skip_callback *client_skipper)
{
	struct archive_read *a = (struct archive_read *)_a;
	archive_check_magic(_a, ARCHIVE_READ_MAGIC, ARCHIVE_STATE_NEW,
	    "archive_read_set_skip_callback");
	a->client.skipper = client_skipper;
	return ARCHIVE_OK;
}

int
archive_read_set_seek_callback(struct archive *_a,
    archive_seek_callback *client_seeker)
{
	struct archive_read *a = (struct archive_read *)_a;
	archive_check_magic(_a, ARCHIVE_READ_MAGIC, ARCHIVE_STATE_NEW,
	    "archive_read_set_seek_callback");
	a->client.seeker = client_seeker;
	return ARCHIVE_OK;
}

int
archive_read_set_close_callback(struct archive *_a,
    archive_close_callback *client_closer)
{
	struct archive_read *a = (struct archive_read *)_a;
	archive_check_magic(_a, ARCHIVE_READ_MAGIC, ARCHIVE_STATE_NEW,
	    "archive_read_set_close_callback");
	a->client.closer = client_closer;
	return ARCHIVE_OK;
}

int
archive_read_set_switch_callback(struct archive *_a,
    archive_switch_callback *client_switcher)
{
	struct archive_read *a = (struct archive_read *)_a;
	archive_check_magic(_a, ARCHIVE_READ_MAGIC, ARCHIVE_STATE_NEW,
	    "archive_read_set_switch_callback");
	a->client.switcher = client_switcher;
	return ARCHIVE_OK;
}

int
archive_read_set_callback_data(struct archive *_a, void *client_data)
{
	return archive_read_set_callback_data2(_a, client_data, 0);
}

int
archive_read_set_callback_data2(struct archive *_a, void *client_data,
    unsigned int iindex)
{
	struct archive_read *a = (struct archive_read *)_a;
	archive_check_magic(_a, ARCHIVE_READ_MAGIC, ARCHIVE_STATE_NEW,
	    "archive_read_set_callback_data2");

	if (a->client.nodes == 0)
	{
		a->client.dataset = (struct archive_read_data_node *)
		    calloc(1, sizeof(*a->client.dataset));
		if (a->client.dataset == NULL)
		{
			archive_set_error(&a->archive, ENOMEM,
				"No memory.");
			return ARCHIVE_FATAL;
		}
		a->client.nodes = 1;
	}

	if (iindex > a->client.nodes - 1)
	{
		archive_set_error(&a->archive, EINVAL,
			"Invalid index specified.");
		return ARCHIVE_FATAL;
	}
	a->client.dataset[iindex].data = client_data;
	a->client.dataset[iindex].begin_position = -1;
	a->client.dataset[iindex].total_size = -1;
	return ARCHIVE_OK;
}

int
archive_read_add_callback_data(struct archive *_a, void *client_data,
    unsigned int iindex)
{
	struct archive_read *a = (struct archive_read *)_a;
	void *p;
	unsigned int i;

	archive_check_magic(_a, ARCHIVE_READ_MAGIC, ARCHIVE_STATE_NEW,
	    "archive_read_add_callback_data");
	if (iindex > a->client.nodes) {
		archive_set_error(&a->archive, EINVAL,
			"Invalid index specified.");
		return ARCHIVE_FATAL;
	}
	p = realloc(a->client.dataset, sizeof(*a->client.dataset)
		* (++(a->client.nodes)));
	if (p == NULL) {
		archive_set_error(&a->archive, ENOMEM,
			"No memory.");
		return ARCHIVE_FATAL;
	}
	a->client.dataset = (struct archive_read_data_node *)p;
	for (i = a->client.nodes - 1; i > iindex && i > 0; i--) {
		a->client.dataset[i].data = a->client.dataset[i-1].data;
		a->client.dataset[i].begin_position = -1;
		a->client.dataset[i].total_size = -1;
	}
	a->client.dataset[iindex].data = client_data;
	a->client.dataset[iindex].begin_position = -1;
	a->client.dataset[iindex].total_size = -1;
	return ARCHIVE_OK;
}

int
archive_read_append_callback_data(struct archive *_a, void *client_data)
{
	struct archive_read *a = (struct archive_read *)_a;
	return archive_read_add_callback_data(_a, client_data, a->client.nodes);
}

int
archive_read_prepend_callback_data(struct archive *_a, void *client_data)
{
	return archive_read_add_callback_data(_a, client_data, 0);
}

int
archive_read_open1(struct archive *_a)
{
	struct archive_read *a = (struct archive_read *)_a;
	struct archive_read_filter *filter, *tmp;
	int slot, e = ARCHIVE_OK;
	unsigned int i;

	archive_check_magic(_a, ARCHIVE_READ_MAGIC, ARCHIVE_STATE_NEW,
	    "archive_read_open");
	archive_clear_error(&a->archive);

	if (a->client.reader == NULL) {
		archive_set_error(&a->archive, EINVAL,
		    "No reader function provided to archive_read_open");
		a->archive.state = ARCHIVE_STATE_FATAL;
		return (ARCHIVE_FATAL);
	}

	/* Open data source. */
	if (a->client.opener != NULL) {
		e = (a->client.opener)(&a->archive, a->client.dataset[0].data);
		if (e != 0) {
			/* If the open failed, call the closer to clean up. */
			if (a->client.closer) {
				for (i = 0; i < a->client.nodes; i++)
					(a->client.closer)(&a->archive,
					    a->client.dataset[i].data);
			}
			return (e);
		}
	}

	filter = calloc(1, sizeof(*filter));
	if (filter == NULL)
		return (ARCHIVE_FATAL);
	filter->bidder = NULL;
	filter->upstream = NULL;
	filter->archive = a;
	filter->data = a->client.dataset[0].data;
	filter->open = client_open_proxy;
	filter->read = client_read_proxy;
	filter->skip = client_skip_proxy;
	filter->seek = client_seek_proxy;
	filter->close = client_close_proxy;
	filter->sswitch = client_switch_proxy;
	filter->name = "none";
	filter->code = ARCHIVE_FILTER_NONE;

	a->client.dataset[0].begin_position = 0;
	if (!a->filter || !a->bypass_filter_bidding)
	{
		a->filter = filter;
		/* Build out the input pipeline. */
		e = choose_filters(a);
		if (e < ARCHIVE_WARN) {
			a->archive.state = ARCHIVE_STATE_FATAL;
			return (ARCHIVE_FATAL);
		}
	}
	else
	{
		/* Need to add "NONE" type filter at the end of the filter chain */
		tmp = a->filter;
		while (tmp->upstream)
			tmp = tmp->upstream;
		tmp->upstream = filter;
	}

	if (!a->format)
	{
		slot = choose_format(a);
		if (slot < 0) {
			close_filters(a);
			a->archive.state = ARCHIVE_STATE_FATAL;
			return (ARCHIVE_FATAL);
		}
		a->format = &(a->formats[slot]);
	}

	a->archive.state = ARCHIVE_STATE_HEADER;

	/* Ensure libarchive starts from the first node in a multivolume set */
	client_switch_proxy(a->filter, 0);
	return (e);
}

/*
 * Allow each registered stream transform to bid on whether
 * it wants to handle this stream.  Repeat until we've finished
 * building the pipeline.
 */

/* We won't build a filter pipeline with more stages than this. */
#define MAX_NUMBER_FILTERS 25

static int
choose_filters(struct archive_read *a)
{
	int number_bidders, i, bid, best_bid, number_filters;
	struct archive_read_filter_bidder *bidder, *best_bidder;
	struct archive_read_filter *filter;
	ssize_t avail;
	int r;

	for (number_filters = 0; number_filters < MAX_NUMBER_FILTERS; ++number_filters) {
		number_bidders = sizeof(a->bidders) / sizeof(a->bidders[0]);

		best_bid = 0;
		best_bidder = NULL;

		bidder = a->bidders;
		for (i = 0; i < number_bidders; i++, bidder++) {
			if (bidder->bid != NULL) {
				bid = (bidder->bid)(bidder, a->filter);
				if (bid > best_bid) {
					best_bid = bid;
					best_bidder = bidder;
				}
			}
		}

		/* If no bidder, we're done. */
		if (best_bidder == NULL) {
			/* Verify the filter by asking it for some data. */
			__archive_read_filter_ahead(a->filter, 1, &avail);
			if (avail < 0) {
				__archive_read_free_filters(a);
				return (ARCHIVE_FATAL);
			}
			a->archive.compression_name = a->filter->name;
			a->archive.compression_code = a->filter->code;
			return (ARCHIVE_OK);
		}

		filter
		    = (struct archive_read_filter *)calloc(1, sizeof(*filter));
		if (filter == NULL)
			return (ARCHIVE_FATAL);
		filter->bidder = best_bidder;
		filter->archive = a;
		filter->upstream = a->filter;
		a->filter = filter;
		r = (best_bidder->init)(a->filter);
		if (r != ARCHIVE_OK) {
			__archive_read_free_filters(a);
			return (ARCHIVE_FATAL);
		}
	}
	archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
	    "Input requires too many filters for decoding");
	return (ARCHIVE_FATAL);
}

/*
 * Read header of next entry.
 */
static int
_archive_read_next_header2(struct archive *_a, struct archive_entry *entry)
{
	struct archive_read *a = (struct archive_read *)_a;
	int r1 = ARCHIVE_OK, r2;

	archive_check_magic(_a, ARCHIVE_READ_MAGIC,
	    ARCHIVE_STATE_HEADER | ARCHIVE_STATE_DATA,
	    "archive_read_next_header");

	archive_entry_clear(entry);
	archive_clear_error(&a->archive);

	/*
	 * If client didn't consume entire data, skip any remainder
	 * (This is especially important for GNU incremental directories.)
	 */
	if (a->archive.state == ARCHIVE_STATE_DATA) {
		r1 = archive_read_data_skip(&a->archive);
		if (r1 == ARCHIVE_EOF)
			archive_set_error(&a->archive, EIO,
			    "Premature end-of-file.");
		if (r1 == ARCHIVE_EOF || r1 == ARCHIVE_FATAL) {
			a->archive.state = ARCHIVE_STATE_FATAL;
			return (ARCHIVE_FATAL);
		}
	}

	/* Record start-of-header offset in uncompressed stream. */
	a->header_position = a->filter->position;

	++_a->file_count;
	r2 = (a->format->read_header)(a, entry);

	/*
	 * EOF and FATAL are persistent at this layer.  By
	 * modifying the state, we guarantee that future calls to
	 * read a header or read data will fail.
	 */
	switch (r2) {
	case ARCHIVE_EOF:
		a->archive.state = ARCHIVE_STATE_EOF;
		--_a->file_count;/* Revert a file counter. */
		break;
	case ARCHIVE_OK:
		a->archive.state = ARCHIVE_STATE_DATA;
		break;
	case ARCHIVE_WARN:
		a->archive.state = ARCHIVE_STATE_DATA;
		break;
	case ARCHIVE_RETRY:
		break;
	case ARCHIVE_FATAL:
		a->archive.state = ARCHIVE_STATE_FATAL;
		break;
	}

	__archive_reset_read_data(&a->archive);

	a->data_start_node = a->client.cursor;
	/* EOF always wins; otherwise return the worst error. */
	return (r2 < r1 || r2 == ARCHIVE_EOF) ? r2 : r1;
}

static int
_archive_read_next_header(struct archive *_a, struct archive_entry **entryp)
{
	int ret;
	struct archive_read *a = (struct archive_read *)_a;
	*entryp = NULL;
	ret = _archive_read_next_header2(_a, a->entry);
	*entryp = a->entry;
	return ret;
}

/*
 * Allow each registered format to bid on whether it wants to handle
 * the next entry.  Return index of winning bidder.
 */
static int
choose_format(struct archive_read *a)
{
	int slots;
	int i;
	int bid, best_bid;
	int best_bid_slot;

	slots = sizeof(a->formats) / sizeof(a->formats[0]);
	best_bid = -1;
	best_bid_slot = -1;

	/* Set up a->format for convenience of bidders. */
	a->format = &(a->formats[0]);
	for (i = 0; i < slots; i++, a->format++) {
		if (a->format->bid) {
			bid = (a->format->bid)(a, best_bid);
			if (bid == ARCHIVE_FATAL)
				return (ARCHIVE_FATAL);
			if (a->filter->position != 0)
				__archive_read_seek(a, 0, SEEK_SET);
			if ((bid > best_bid) || (best_bid_slot < 0)) {
				best_bid = bid;
				best_bid_slot = i;
			}
		}
	}

	/*
	 * There were no bidders; this is a serious programmer error
	 * and demands a quick and definitive abort.
	 */
	if (best_bid_slot < 0) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "No formats registered");
		return (ARCHIVE_FATAL);
	}

	/*
	 * There were bidders, but no non-zero bids; this means we
	 * can't support this stream.
	 */
	if (best_bid < 1) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Unrecognized archive format");
		return (ARCHIVE_FATAL);
	}

	return (best_bid_slot);
}

/*
 * Return the file offset (within the uncompressed data stream) where
 * the last header started.
 */
la_int64_t
archive_read_header_position(struct archive *_a)
{
	struct archive_read *a = (struct archive_read *)_a;
	archive_check_magic(_a, ARCHIVE_READ_MAGIC,
	    ARCHIVE_STATE_ANY, "archive_read_header_position");
	return (a->header_position);
}

/*
 * Returns 1 if the archive contains at least one encrypted entry.
 * If the archive format not support encryption at all
 * ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED is returned.
 * If for any other reason (e.g. not enough data read so far)
 * we cannot say whether there are encrypted entries, then
 * ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW is returned.
 * In general, this function will return values below zero when the
 * reader is uncertain or totally incapable of encryption support.
 * When this function returns 0 you can be sure that the reader
 * supports encryption detection but no encrypted entries have
 * been found yet.
 *
 * NOTE: If the metadata/header of an archive is also encrypted, you
 * cannot rely on the number of encrypted entries. That is why this
 * function does not return the number of encrypted entries but#
 * just shows that there are some.
 */
int
archive_read_has_encrypted_entries(struct archive *_a)
{
	struct archive_read *a = (struct archive_read *)_a;
	int format_supports_encryption = archive_read_format_capabilities(_a)
			& (ARCHIVE_READ_FORMAT_CAPS_ENCRYPT_DATA | ARCHIVE_READ_FORMAT_CAPS_ENCRYPT_METADATA);

	if (!_a || !format_supports_encryption) {
		/* Format in general doesn't support encryption */
		return ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED;
	}

	/* A reader potentially has read enough data now. */
	if (a->format && a->format->has_encrypted_entries) {
		return (a->format->has_encrypted_entries)(a);
	}

	/* For any other reason we cannot say how many entries are there. */
	return ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW;
}

/*
 * Returns a bitmask of capabilities that are supported by the archive format reader.
 * If the reader has no special capabilities, ARCHIVE_READ_FORMAT_CAPS_NONE is returned.
 */
int
archive_read_format_capabilities(struct archive *_a)
{
	struct archive_read *a = (struct archive_read *)_a;
	if (a && a->format && a->format->format_capabilties) {
		return (a->format->format_capabilties)(a);
	}
	return ARCHIVE_READ_FORMAT_CAPS_NONE;
}

/*
 * Read data from an archive entry, using a read(2)-style interface.
 * This is a convenience routine that just calls
 * archive_read_data_block and copies the results into the client
 * buffer, filling any gaps with zero bytes.  Clients using this
 * API can be completely ignorant of sparse-file issues; sparse files
 * will simply be padded with nulls.
 *
 * DO NOT intermingle calls to this function and archive_read_data_block
 * to read a single entry body.
 */
la_ssize_t
archive_read_data(struct archive *_a, void *buff, size_t s)
{
	struct archive *a = (struct archive *)_a;
	char	*dest;
	const void *read_buf;
	size_t	 bytes_read;
	size_t	 len;
	int	 r;

	bytes_read = 0;
	dest = (char *)buff;

	while (s > 0) {
		if (a->read_data_remaining == 0) {
			read_buf = a->read_data_block;
			a->read_data_is_posix_read = 1;
			a->read_data_requested = s;
			r = archive_read_data_block(a, &read_buf,
			    &a->read_data_remaining, &a->read_data_offset);
			a->read_data_block = read_buf;
			if (r == ARCHIVE_EOF)
				return (bytes_read);
			/*
			 * Error codes are all negative, so the status
			 * return here cannot be confused with a valid
			 * byte count.  (ARCHIVE_OK is zero.)
			 */
			if (r < ARCHIVE_OK)
				return (r);
		}

		if (a->read_data_offset < a->read_data_output_offset) {
			archive_set_error(a, ARCHIVE_ERRNO_FILE_FORMAT,
			    "Encountered out-of-order sparse blocks");
			return (ARCHIVE_RETRY);
		}

		/* Compute the amount of zero padding needed. */
		if (a->read_data_output_offset + (int64_t)s <
		    a->read_data_offset) {
			len = s;
		} else if (a->read_data_output_offset <
		    a->read_data_offset) {
			len = (size_t)(a->read_data_offset -
			    a->read_data_output_offset);
		} else
			len = 0;

		/* Add zeroes. */
		memset(dest, 0, len);
		s -= len;
		a->read_data_output_offset += len;
		dest += len;
		bytes_read += len;

		/* Copy data if there is any space left. */
		if (s > 0) {
			len = a->read_data_remaining;
			if (len > s)
				len = s;
			if (len)
				memcpy(dest, a->read_data_block, len);
			s -= len;
			a->read_data_block += len;
			a->read_data_remaining -= len;
			a->read_data_output_offset += len;
			a->read_data_offset += len;
			dest += len;
			bytes_read += len;
		}
	}
	a->read_data_is_posix_read = 0;
	a->read_data_requested = 0;
	return (bytes_read);
}

/*
 * Reset the read_data_* variables, used for starting a new entry.
 */
void __archive_reset_read_data(struct archive * a)
{
	a->read_data_output_offset = 0;
	a->read_data_remaining = 0;
	a->read_data_is_posix_read = 0;
	a->read_data_requested = 0;

   /* extra resets, from rar.c */
   a->read_data_block = NULL;
   a->read_data_offset = 0;
}

/*
 * Skip over all remaining data in this entry.
 */
int
archive_read_data_skip(struct archive *_a)
{
	struct archive_read *a = (struct archive_read *)_a;
	int r;
	const void *buff;
	size_t size;
	int64_t offset;

	archive_check_magic(_a, ARCHIVE_READ_MAGIC, ARCHIVE_STATE_DATA,
	    "archive_read_data_skip");

	if (a->format->read_data_skip != NULL)
		r = (a->format->read_data_skip)(a);
	else {
		while ((r = archive_read_data_block(&a->archive,
			    &buff, &size, &offset))
		    == ARCHIVE_OK)
			;
	}

	if (r == ARCHIVE_EOF)
		r = ARCHIVE_OK;

	a->archive.state = ARCHIVE_STATE_HEADER;
	return (r);
}

la_int64_t
archive_seek_data(struct archive *_a, int64_t offset, int whence)
{
	struct archive_read *a = (struct archive_read *)_a;
	archive_check_magic(_a, ARCHIVE_READ_MAGIC, ARCHIVE_STATE_DATA,
	    "archive_seek_data_block");

	if (a->format->seek_data == NULL) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER,
		    "Internal error: "
		    "No format_seek_data_block function registered");
		return (ARCHIVE_FATAL);
	}

	return (a->format->seek_data)(a, offset, whence);
}

/*
 * Read the next block of entry data from the archive.
 * This is a zero-copy interface; the client receives a pointer,
 * size, and file offset of the next available block of data.
 *
 * Returns ARCHIVE_OK if the operation is successful, ARCHIVE_EOF if
 * the end of entry is encountered.
 */
static int
_archive_read_data_block(struct archive *_a,
    const void **buff, size_t *size, int64_t *offset)
{
	struct archive_read *a = (struct archive_read *)_a;
	archive_check_magic(_a, ARCHIVE_READ_MAGIC, ARCHIVE_STATE_DATA,
	    "archive_read_data_block");

	if (a->format->read_data == NULL) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER,
		    "Internal error: "
		    "No format->read_data function registered");
		return (ARCHIVE_FATAL);
	}

	return (a->format->read_data)(a, buff, size, offset);
}

static int
close_filters(struct archive_read *a)
{
	struct archive_read_filter *f = a->filter;
	int r = ARCHIVE_OK;
	/* Close each filter in the pipeline. */
	while (f != NULL) {
		struct archive_read_filter *t = f->upstream;
		if (!f->closed && f->close != NULL) {
			int r1 = (f->close)(f);
			f->closed = 1;
			if (r1 < r)
				r = r1;
		}
		free(f->buffer);
		f->buffer = NULL;
		f = t;
	}
	return r;
}

void
__archive_read_free_filters(struct archive_read *a)
{
	/* Make sure filters are closed and their buffers are freed */
	close_filters(a);

	while (a->filter != NULL) {
		struct archive_read_filter *t = a->filter->upstream;
		free(a->filter);
		a->filter = t;
	}
}

/*
 * return the count of # of filters in use
 */
static int
_archive_filter_count(struct archive *_a)
{
	struct archive_read *a = (struct archive_read *)_a;
	struct archive_read_filter *p = a->filter;
	int count = 0;
	while(p) {
		count++;
		p = p->upstream;
	}
	return count;
}

/*
 * Close the file and all I/O.
 */
static int
_archive_read_close(struct archive *_a)
{
	struct archive_read *a = (struct archive_read *)_a;
	int r = ARCHIVE_OK, r1 = ARCHIVE_OK;

	archive_check_magic(&a->archive, ARCHIVE_READ_MAGIC,
	    ARCHIVE_STATE_ANY | ARCHIVE_STATE_FATAL, "archive_read_close");
	if (a->archive.state == ARCHIVE_STATE_CLOSED)
		return (ARCHIVE_OK);
	archive_clear_error(&a->archive);
	a->archive.state = ARCHIVE_STATE_CLOSED;

	/* TODO: Clean up the formatters. */

	/* Release the filter objects. */
	r1 = close_filters(a);
	if (r1 < r)
		r = r1;

	return (r);
}

/*
 * Release memory and other resources.
 */
static int
_archive_read_free(struct archive *_a)
{
	struct archive_read *a = (struct archive_read *)_a;
	struct archive_read_passphrase *p;
	int i, n;
	int slots;
	int r = ARCHIVE_OK;

	if (_a == NULL)
		return (ARCHIVE_OK);
	archive_check_magic(_a, ARCHIVE_READ_MAGIC,
	    ARCHIVE_STATE_ANY | ARCHIVE_STATE_FATAL, "archive_read_free");
	if (a->archive.state != ARCHIVE_STATE_CLOSED
	    && a->archive.state != ARCHIVE_STATE_FATAL)
		r = archive_read_close(&a->archive);

	/* Call cleanup functions registered by optional components. */
	if (a->cleanup_archive_extract != NULL)
		r = (a->cleanup_archive_extract)(a);

	/* Cleanup format-specific data. */
	slots = sizeof(a->formats) / sizeof(a->formats[0]);
	for (i = 0; i < slots; i++) {
		a->format = &(a->formats[i]);
		if (a->formats[i].cleanup)
			(a->formats[i].cleanup)(a);
	}

	/* Free the filters */
	__archive_read_free_filters(a);

	/* Release the bidder objects. */
	n = sizeof(a->bidders)/sizeof(a->bidders[0]);
	for (i = 0; i < n; i++) {
		if (a->bidders[i].free != NULL) {
			int r1 = (a->bidders[i].free)(&a->bidders[i]);
			if (r1 < r)
				r = r1;
		}
	}

	/* Release passphrase list. */
	p = a->passphrases.first;
	while (p != NULL) {
		struct archive_read_passphrase *np = p->next;

		/* A passphrase should be cleaned. */
		memset(p->passphrase, 0, strlen(p->passphrase));
		free(p->passphrase);
		free(p);
		p = np;
	}

	archive_string_free(&a->archive.error_string);
	archive_entry_free(a->entry);
	a->archive.magic = 0;
	__archive_clean(&a->archive);
	free(a->client.dataset);
	free(a);
	return (r);
}

static struct archive_read_filter *
get_filter(struct archive *_a, int n)
{
	struct archive_read *a = (struct archive_read *)_a;
	struct archive_read_filter *f = a->filter;
	/* We use n == -1 for 'the last filter', which is always the
	 * client proxy. */
	if (n == -1 && f != NULL) {
		struct archive_read_filter *last = f;
		f = f->upstream;
		while (f != NULL) {
			last = f;
			f = f->upstream;
		}
		return (last);
	}
	if (n < 0)
		return NULL;
	while (n > 0 && f != NULL) {
		f = f->upstream;
		--n;
	}
	return (f);
}

static int
_archive_filter_code(struct archive *_a, int n)
{
	struct archive_read_filter *f = get_filter(_a, n);
	return f == NULL ? -1 : f->code;
}

static const char *
_archive_filter_name(struct archive *_a, int n)
{
	struct archive_read_filter *f = get_filter(_a, n);
	return f != NULL ? f->name : NULL;
}

static int64_t
_archive_filter_bytes(struct archive *_a, int n)
{
	struct archive_read_filter *f = get_filter(_a, n);
	return f == NULL ? -1 : f->position;
}

/*
 * Used internally by read format handlers to register their bid and
 * initialization functions.
 */
int
__archive_read_register_format(struct archive_read *a,
    void *format_data,
    const char *name,
    int (*bid)(struct archive_read *, int),
    int (*options)(struct archive_read *, const char *, const char *),
    int (*read_header)(struct archive_read *, struct archive_entry *),
    int (*read_data)(struct archive_read *, const void **, size_t *, int64_t *),
    int (*read_data_skip)(struct archive_read *),
    int64_t (*seek_data)(struct archive_read *, int64_t, int),
    int (*cleanup)(struct archive_read *),
    int (*format_capabilities)(struct archive_read *),
    int (*has_encrypted_entries)(struct archive_read *))
{
	int i, number_slots;

	archive_check_magic(&a->archive,
	    ARCHIVE_READ_MAGIC, ARCHIVE_STATE_NEW,
	    "__archive_read_register_format");

	number_slots = sizeof(a->formats) / sizeof(a->formats[0]);

	for (i = 0; i < number_slots; i++) {
		if (a->formats[i].bid == bid)
			return (ARCHIVE_WARN); /* We've already installed */
		if (a->formats[i].bid == NULL) {
			a->formats[i].bid = bid;
			a->formats[i].options = options;
			a->formats[i].read_header = read_header;
			a->formats[i].read_data = read_data;
			a->formats[i].read_data_skip = read_data_skip;
			a->formats[i].seek_data = seek_data;
			a->formats[i].cleanup = cleanup;
			a->formats[i].data = format_data;
			a->formats[i].name = name;
			a->formats[i].format_capabilties = format_capabilities;
			a->formats[i].has_encrypted_entries = has_encrypted_entries;
			return (ARCHIVE_OK);
		}
	}

	archive_set_error(&a->archive, ENOMEM,
	    "Not enough slots for format registration");
	return (ARCHIVE_FATAL);
}

/*
 * Used internally by decompression routines to register their bid and
 * initialization functions.
 */
int
__archive_read_get_bidder(struct archive_read *a,
    struct archive_read_filter_bidder **bidder)
{
	int i, number_slots;

	number_slots = sizeof(a->bidders) / sizeof(a->bidders[0]);

	for (i = 0; i < number_slots; i++) {
		if (a->bidders[i].bid == NULL) {
			memset(a->bidders + i, 0, sizeof(a->bidders[0]));
			*bidder = (a->bidders + i);
			return (ARCHIVE_OK);
		}
	}

	archive_set_error(&a->archive, ENOMEM,
	    "Not enough slots for filter registration");
	return (ARCHIVE_FATAL);
}

/*
 * The next section implements the peek/consume internal I/O
 * system used by archive readers.  This system allows simple
 * read-ahead for consumers while preserving zero-copy operation
 * most of the time.
 *
 * The two key operations:
 *  * The read-ahead function returns a pointer to a block of data
 *    that satisfies a minimum request.
 *  * The consume function advances the file pointer.
 *
 * In the ideal case, filters generate blocks of data
 * and __archive_read_ahead() just returns pointers directly into
 * those blocks.  Then __archive_read_consume() just bumps those
 * pointers.  Only if your request would span blocks does the I/O
 * layer use a copy buffer to provide you with a contiguous block of
 * data.
 *
 * A couple of useful idioms:
 *  * "I just want some data."  Ask for 1 byte and pay attention to
 *    the "number of bytes available" from __archive_read_ahead().
 *    Consume whatever you actually use.
 *  * "I want to output a large block of data."  As above, ask for 1 byte,
 *    emit all that's available (up to whatever limit you have), consume
 *    it all, then repeat until you're done.  This effectively means that
 *    you're passing along the blocks that came from your provider.
 *  * "I want to peek ahead by a large amount."  Ask for 4k or so, then
 *    double and repeat until you get an error or have enough.  Note
 *    that the I/O layer will likely end up expanding its copy buffer
 *    to fit your request, so use this technique cautiously.  This
 *    technique is used, for example, by some of the format tasting
 *    code that has uncertain look-ahead needs.
 */

/*
 * Looks ahead in the input stream:
 *  * If 'avail' pointer is provided, that returns number of bytes available
 *    in the current buffer, which may be much larger than requested.
 *  * If end-of-file, *avail gets set to zero.
 *  * If error, *avail gets error code.
 *  * If request can be met, returns pointer to data.
 *  * If minimum request cannot be met, returns NULL.
 *
 * Note: If you just want "some data", ask for 1 byte and pay attention
 * to *avail, which will have the actual amount available.  If you
 * know exactly how many bytes you need, just ask for that and treat
 * a NULL return as an error.
 *
 * Important:  This does NOT move the file pointer.  See
 * __archive_read_consume() below.
 */
const void *
__archive_read_ahead(struct archive_read *a, size_t min, ssize_t *avail)
{
	return (__archive_read_filter_ahead(a->filter, min, avail));
}

const void *
__archive_read_filter_ahead(struct archive_read_filter *filter,
    size_t min, ssize_t *avail)
{
	ssize_t bytes_read;
	size_t tocopy;

	if (filter->fatal) {
		if (avail)
			*avail = ARCHIVE_FATAL;
		return (NULL);
	}

	/*
	 * Keep pulling more data until we can satisfy the request.
	 */
	for (;;) {

		/*
		 * If we can satisfy from the copy buffer (and the
		 * copy buffer isn't empty), we're done.  In particular,
		 * note that min == 0 is a perfectly well-defined
		 * request.
		 */
		if (filter->avail >= min && filter->avail > 0) {
			if (avail != NULL)
				*avail = filter->avail;
			return (filter->next);
		}

		/*
		 * We can satisfy directly from client buffer if everything
		 * currently in the copy buffer is still in the client buffer.
		 */
		if (filter->client_total >= filter->client_avail + filter->avail
		    && filter->client_avail + filter->avail >= min) {
			/* "Roll back" to client buffer. */
			filter->client_avail += filter->avail;
			filter->client_next -= filter->avail;
			/* Copy buffer is now empty. */
			filter->avail = 0;
			filter->next = filter->buffer;
			/* Return data from client buffer. */
			if (avail != NULL)
				*avail = filter->client_avail;
			return (filter->client_next);
		}

		/* Move data forward in copy buffer if necessary. */
		if (filter->next > filter->buffer &&
		    filter->next + min > filter->buffer + filter->buffer_size) {
			if (filter->avail > 0)
				memmove(filter->buffer, filter->next,
				    filter->avail);
			filter->next = filter->buffer;
		}

		/* If we've used up the client data, get more. */
		if (filter->client_avail <= 0) {
			if (filter->end_of_file) {
				if (avail != NULL)
					*avail = 0;
				return (NULL);
			}
			bytes_read = (filter->read)(filter,
			    &filter->client_buff);
			if (bytes_read < 0) {		/* Read error. */
				filter->client_total = filter->client_avail = 0;
				filter->client_next =
				    filter->client_buff = NULL;
				filter->fatal = 1;
				if (avail != NULL)
					*avail = ARCHIVE_FATAL;
				return (NULL);
			}
			if (bytes_read == 0) {
				/* Check for another client object first */
				if (filter->archive->client.cursor !=
				      filter->archive->client.nodes - 1) {
					if (client_switch_proxy(filter,
					    filter->archive->client.cursor + 1)
					    == ARCHIVE_OK)
						continue;
				}
				/* Premature end-of-file. */
				filter->client_total = filter->client_avail = 0;
				filter->client_next =
				    filter->client_buff = NULL;
				filter->end_of_file = 1;
				/* Return whatever we do have. */
				if (avail != NULL)
					*avail = filter->avail;
				return (NULL);
			}
			filter->client_total = bytes_read;
			filter->client_avail = filter->client_total;
			filter->client_next = filter->client_buff;
		} else {
			/*
			 * We can't satisfy the request from the copy
			 * buffer or the existing client data, so we
			 * need to copy more client data over to the
			 * copy buffer.
			 */

			/* Ensure the buffer is big enough. */
			if (min > filter->buffer_size) {
				size_t s, t;
				char *p;

				/* Double the buffer; watch for overflow. */
				s = t = filter->buffer_size;
				if (s == 0)
					s = min;
				while (s < min) {
					t *= 2;
					if (t <= s) { /* Integer overflow! */
						archive_set_error(
						    &filter->archive->archive,
						    ENOMEM,
						    "Unable to allocate copy"
						    " buffer");
						filter->fatal = 1;
						if (avail != NULL)
							*avail = ARCHIVE_FATAL;
						return (NULL);
					}
					s = t;
				}
				/* Now s >= min, so allocate a new buffer. */
				p = (char *)malloc(s);
				if (p == NULL) {
					archive_set_error(
						&filter->archive->archive,
						ENOMEM,
					    "Unable to allocate copy buffer");
					filter->fatal = 1;
					if (avail != NULL)
						*avail = ARCHIVE_FATAL;
					return (NULL);
				}
				/* Move data into newly-enlarged buffer. */
				if (filter->avail > 0)
					memmove(p, filter->next, filter->avail);
				free(filter->buffer);
				filter->next = filter->buffer = p;
				filter->buffer_size = s;
			}

			/* We can add client data to copy buffer. */
			/* First estimate: copy to fill rest of buffer. */
			tocopy = (filter->buffer + filter->buffer_size)
			    - (filter->next + filter->avail);
			/* Don't waste time buffering more than we need to. */
			if (tocopy + filter->avail > min)
				tocopy = min - filter->avail;
			/* Don't copy more than is available. */
			if (tocopy > filter->client_avail)
				tocopy = filter->client_avail;

			memcpy(filter->next + filter->avail,
			    filter->client_next, tocopy);
			/* Remove this data from client buffer. */
			filter->client_next += tocopy;
			filter->client_avail -= tocopy;
			/* add it to copy buffer. */
			filter->avail += tocopy;
		}
	}
}

/*
 * Move the file pointer forward.
 */
int64_t
__archive_read_consume(struct archive_read *a, int64_t request)
{
	return (__archive_read_filter_consume(a->filter, request));
}

int64_t
__archive_read_filter_consume(struct archive_read_filter * filter,
    int64_t request)
{
	int64_t skipped;

	if (request < 0)
		return ARCHIVE_FATAL;
	if (request == 0)
		return 0;

	skipped = advance_file_pointer(filter, request);
	if (skipped == request)
		return (skipped);
	/* We hit EOF before we satisfied the skip request. */
	if (skipped < 0)  /* Map error code to 0 for error message below. */
		skipped = 0;
	archive_set_error(&filter->archive->archive,
	    ARCHIVE_ERRNO_MISC,
	    "Truncated input file (needed %jd bytes, only %jd available)",
	    (intmax_t)request, (intmax_t)skipped);
	return (ARCHIVE_FATAL);
}

/*
 * Advance the file pointer by the amount requested.
 * Returns the amount actually advanced, which may be less than the
 * request if EOF is encountered first.
 * Returns a negative value if there's an I/O error.
 */
static int64_t
advance_file_pointer(struct archive_read_filter *filter, int64_t request)
{
	int64_t bytes_skipped, total_bytes_skipped = 0;
	ssize_t bytes_read;
	size_t min;

	if (filter->fatal)
		return (-1);

	/* Use up the copy buffer first. */
	if (filter->avail > 0) {
		min = (size_t)minimum(request, (int64_t)filter->avail);
		filter->next += min;
		filter->avail -= min;
		request -= min;
		filter->position += min;
		total_bytes_skipped += min;
	}

	/* Then use up the client buffer. */
	if (filter->client_avail > 0) {
		min = (size_t)minimum(request, (int64_t)filter->client_avail);
		filter->client_next += min;
		filter->client_avail -= min;
		request -= min;
		filter->position += min;
		total_bytes_skipped += min;
	}
	if (request == 0)
		return (total_bytes_skipped);

	/* If there's an optimized skip function, use it. */
	if (filter->skip != NULL) {
		bytes_skipped = (filter->skip)(filter, request);
		if (bytes_skipped < 0) {	/* error */
			filter->fatal = 1;
			return (bytes_skipped);
		}
		filter->position += bytes_skipped;
		total_bytes_skipped += bytes_skipped;
		request -= bytes_skipped;
		if (request == 0)
			return (total_bytes_skipped);
	}

	/* Use ordinary reads as necessary to complete the request. */
	for (;;) {
		bytes_read = (filter->read)(filter, &filter->client_buff);
		if (bytes_read < 0) {
			filter->client_buff = NULL;
			filter->fatal = 1;
			return (bytes_read);
		}

		if (bytes_read == 0) {
			if (filter->archive->client.cursor !=
			      filter->archive->client.nodes - 1) {
				if (client_switch_proxy(filter,
				    filter->archive->client.cursor + 1)
				    == ARCHIVE_OK)
					continue;
			}
			filter->client_buff = NULL;
			filter->end_of_file = 1;
			return (total_bytes_skipped);
		}

		if (bytes_read >= request) {
			filter->client_next =
			    ((const char *)filter->client_buff) + request;
			filter->client_avail = (size_t)(bytes_read - request);
			filter->client_total = bytes_read;
			total_bytes_skipped += request;
			filter->position += request;
			return (total_bytes_skipped);
		}

		filter->position += bytes_read;
		total_bytes_skipped += bytes_read;
		request -= bytes_read;
	}
}

/**
 * Returns ARCHIVE_FAILED if seeking isn't supported.
 */
int64_t
__archive_read_seek(struct archive_read *a, int64_t offset, int whence)
{
	return __archive_read_filter_seek(a->filter, offset, whence);
}

int64_t
__archive_read_filter_seek(struct archive_read_filter *filter, int64_t offset,
    int whence)
{
	struct archive_read_client *client;
	int64_t r;
	unsigned int cursor;

	if (filter->closed || filter->fatal)
		return (ARCHIVE_FATAL);
	if (filter->seek == NULL)
		return (ARCHIVE_FAILED);

	client = &(filter->archive->client);
	switch (whence) {
	case SEEK_CUR:
		/* Adjust the offset and use SEEK_SET instead */
		offset += filter->position;
		__LA_FALLTHROUGH;
	case SEEK_SET:
		cursor = 0;
		while (1)
		{
			if (client->dataset[cursor].begin_position < 0 ||
			    client->dataset[cursor].total_size < 0 ||
			    client->dataset[cursor].begin_position +
			      client->dataset[cursor].total_size - 1 > offset ||
			    cursor + 1 >= client->nodes)
				break;
			r = client->dataset[cursor].begin_position +
				client->dataset[cursor].total_size;
			client->dataset[++cursor].begin_position = r;
		}
		while (1) {
			r = client_switch_proxy(filter, cursor);
			if (r != ARCHIVE_OK)
				return r;
			if ((r = client_seek_proxy(filter, 0, SEEK_END)) < 0)
				return r;
			client->dataset[cursor].total_size = r;
			if (client->dataset[cursor].begin_position +
			    client->dataset[cursor].total_size - 1 > offset ||
			    cursor + 1 >= client->nodes)
				break;
			r = client->dataset[cursor].begin_position +
				client->dataset[cursor].total_size;
			client->dataset[++cursor].begin_position = r;
		}
		offset -= client->dataset[cursor].begin_position;
		if (offset < 0
		    || offset > client->dataset[cursor].total_size)
			return ARCHIVE_FATAL;
		if ((r = client_seek_proxy(filter, offset, SEEK_SET)) < 0)
			return r;
		break;

	case SEEK_END:
		cursor = 0;
		while (1) {
			if (client->dataset[cursor].begin_position < 0 ||
			    client->dataset[cursor].total_size < 0 ||
			    cursor + 1 >= client->nodes)
				break;
			r = client->dataset[cursor].begin_position +
				client->dataset[cursor].total_size;
			client->dataset[++cursor].begin_position = r;
		}
		while (1) {
			r = client_switch_proxy(filter, cursor);
			if (r != ARCHIVE_OK)
				return r;
			if ((r = client_seek_proxy(filter, 0, SEEK_END)) < 0)
				return r;
			client->dataset[cursor].total_size = r;
			r = client->dataset[cursor].begin_position +
				client->dataset[cursor].total_size;
			if (cursor + 1 >= client->nodes)
				break;
			client->dataset[++cursor].begin_position = r;
		}
		while (1) {
			if (r + offset >=
			    client->dataset[cursor].begin_position)
				break;
			offset += client->dataset[cursor].total_size;
			if (cursor == 0)
				break;
			cursor--;
			r = client->dataset[cursor].begin_position +
				client->dataset[cursor].total_size;
		}
		offset = (r + offset) - client->dataset[cursor].begin_position;
		if ((r = client_switch_proxy(filter, cursor)) != ARCHIVE_OK)
			return r;
		r = client_seek_proxy(filter, offset, SEEK_SET);
		if (r < ARCHIVE_OK)
			return r;
		break;

	default:
		return (ARCHIVE_FATAL);
	}
	r += client->dataset[cursor].begin_position;

	if (r >= 0) {
		/*
		 * Ouch.  Clearing the buffer like this hurts, especially
		 * at bid time.  A lot of our efficiency at bid time comes
		 * from having bidders reuse the data we've already read.
		 *
		 * TODO: If the seek request is in data we already
		 * have, then don't call the seek callback.
		 *
		 * TODO: Zip seeks to end-of-file at bid time.  If
		 * other formats also start doing this, we may need to
		 * find a way for clients to fudge the seek offset to
		 * a block boundary.
		 *
		 * Hmmm... If whence was SEEK_END, we know the file
		 * size is (r - offset).  Can we use that to simplify
		 * the TODO items above?
		 */
		filter->avail = filter->client_avail = 0;
		filter->next = filter->buffer;
		filter->position = r;
		filter->end_of_file = 0;
	}
	return r;
}
