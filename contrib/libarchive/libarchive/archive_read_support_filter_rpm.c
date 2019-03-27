/*-
 * Copyright (c) 2009 Michihiro NAKAJIMA
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

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include "archive.h"
#include "archive_endian.h"
#include "archive_private.h"
#include "archive_read_private.h"

struct rpm {
	int64_t		 total_in;
	size_t		 hpos;
	size_t		 hlen;
	unsigned char	 header[16];
	enum {
		ST_LEAD,	/* Skipping 'Lead' section. */
		ST_HEADER,	/* Reading 'Header' section;
				 * first 16 bytes. */
		ST_HEADER_DATA,	/* Skipping 'Header' section. */
		ST_PADDING,	/* Skipping padding data after the
				 * 'Header' section. */
		ST_ARCHIVE	/* Reading 'Archive' section. */
	}		 state;
	int		 first_header;
};
#define RPM_LEAD_SIZE	96	/* Size of 'Lead' section. */

static int	rpm_bidder_bid(struct archive_read_filter_bidder *,
		    struct archive_read_filter *);
static int	rpm_bidder_init(struct archive_read_filter *);

static ssize_t	rpm_filter_read(struct archive_read_filter *,
		    const void **);
static int	rpm_filter_close(struct archive_read_filter *);

#if ARCHIVE_VERSION_NUMBER < 4000000
/* Deprecated; remove in libarchive 4.0 */
int
archive_read_support_compression_rpm(struct archive *a)
{
	return archive_read_support_filter_rpm(a);
}
#endif

int
archive_read_support_filter_rpm(struct archive *_a)
{
	struct archive_read *a = (struct archive_read *)_a;
	struct archive_read_filter_bidder *bidder;

	archive_check_magic(_a, ARCHIVE_READ_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_read_support_filter_rpm");

	if (__archive_read_get_bidder(a, &bidder) != ARCHIVE_OK)
		return (ARCHIVE_FATAL);

	bidder->data = NULL;
	bidder->name = "rpm";
	bidder->bid = rpm_bidder_bid;
	bidder->init = rpm_bidder_init;
	bidder->options = NULL;
	bidder->free = NULL;
	return (ARCHIVE_OK);
}

static int
rpm_bidder_bid(struct archive_read_filter_bidder *self,
    struct archive_read_filter *filter)
{
	const unsigned char *b;
	ssize_t avail;
	int bits_checked;

	(void)self; /* UNUSED */

	b = __archive_read_filter_ahead(filter, 8, &avail);
	if (b == NULL)
		return (0);

	bits_checked = 0;
	/*
	 * Verify Header Magic Bytes : 0XED 0XAB 0XEE 0XDB
	 */
	if (memcmp(b, "\xED\xAB\xEE\xDB", 4) != 0)
		return (0);
	bits_checked += 32;
	/*
	 * Check major version.
	 */
	if (b[4] != 3 && b[4] != 4)
		return (0);
	bits_checked += 8;
	/*
	 * Check package type; binary or source.
	 */
	if (b[6] != 0)
		return (0);
	bits_checked += 8;
	if (b[7] != 0 && b[7] != 1)
		return (0);
	bits_checked += 8;

	return (bits_checked);
}

static int
rpm_bidder_init(struct archive_read_filter *self)
{
	struct rpm   *rpm;

	self->code = ARCHIVE_FILTER_RPM;
	self->name = "rpm";
	self->read = rpm_filter_read;
	self->skip = NULL; /* not supported */
	self->close = rpm_filter_close;

	rpm = (struct rpm *)calloc(sizeof(*rpm), 1);
	if (rpm == NULL) {
		archive_set_error(&self->archive->archive, ENOMEM,
		    "Can't allocate data for rpm");
		return (ARCHIVE_FATAL);
	}

	self->data = rpm;
	rpm->state = ST_LEAD;

	return (ARCHIVE_OK);
}

static ssize_t
rpm_filter_read(struct archive_read_filter *self, const void **buff)
{
	struct rpm *rpm;
	const unsigned char *b;
	ssize_t avail_in, total;
	size_t used, n;
	uint32_t section;
	uint32_t bytes;

	rpm = (struct rpm *)self->data;
	*buff = NULL;
	total = avail_in = 0;
	b = NULL;
	used = 0;
	do {
		if (b == NULL) {
			b = __archive_read_filter_ahead(self->upstream, 1,
			    &avail_in);
			if (b == NULL) {
				if (avail_in < 0)
					return (ARCHIVE_FATAL);
				else
					break;
			}
		}

		switch (rpm->state) {
		case ST_LEAD:
			if (rpm->total_in + avail_in < RPM_LEAD_SIZE)
				used += avail_in;
			else {
				n = (size_t)(RPM_LEAD_SIZE - rpm->total_in);
				used += n;
				b += n;
				rpm->state = ST_HEADER;
				rpm->hpos = 0;
				rpm->hlen = 0;
				rpm->first_header = 1;
			}
			break;
		case ST_HEADER:
			n = 16 - rpm->hpos;
			if (n > avail_in - used)
				n = avail_in - used;
			memcpy(rpm->header+rpm->hpos, b, n);
			b += n;
			used += n;
			rpm->hpos += n;

			if (rpm->hpos == 16) {
				if (rpm->header[0] != 0x8e ||
				    rpm->header[1] != 0xad ||
				    rpm->header[2] != 0xe8 ||
				    rpm->header[3] != 0x01) {
					if (rpm->first_header) {
						archive_set_error(
						    &self->archive->archive,
						    ARCHIVE_ERRNO_FILE_FORMAT,
						    "Unrecoginized rpm header");
						return (ARCHIVE_FATAL);
					}
					rpm->state = ST_ARCHIVE;
					*buff = rpm->header;
					total = rpm->hpos;
					break;
				}
				/* Calculate 'Header' length. */
				section = archive_be32dec(rpm->header+8);
				bytes = archive_be32dec(rpm->header+12);
				rpm->hlen = 16 + section * 16 + bytes;
				rpm->state = ST_HEADER_DATA;
				rpm->first_header = 0;
			}
			break;
		case ST_HEADER_DATA:
			n = rpm->hlen - rpm->hpos;
			if (n > avail_in - used)
				n = avail_in - used;
			b += n;
			used += n;
			rpm->hpos += n;
			if (rpm->hpos == rpm->hlen)
				rpm->state = ST_PADDING;
			break;
		case ST_PADDING:
			while (used < (size_t)avail_in) {
				if (*b != 0) {
					/* Read next header. */
					rpm->state = ST_HEADER;
					rpm->hpos = 0;
					rpm->hlen = 0;
					break;
				}
				b++;
				used++;
			}
			break;
		case ST_ARCHIVE:
			*buff = b;
			total = avail_in;
			used = avail_in;
			break;
		}
		if (used == (size_t)avail_in) {
			rpm->total_in += used;
			__archive_read_filter_consume(self->upstream, used);
			b = NULL;
			used = 0;
		}
	} while (total == 0 && avail_in > 0);

	if (used > 0 && b != NULL) {
		rpm->total_in += used;
		__archive_read_filter_consume(self->upstream, used);
	}
	return (total);
}

static int
rpm_filter_close(struct archive_read_filter *self)
{
	struct rpm *rpm;

	rpm = (struct rpm *)self->data;
	free(rpm);

	return (ARCHIVE_OK);
}

