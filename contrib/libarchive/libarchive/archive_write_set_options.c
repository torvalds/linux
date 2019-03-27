/*-
 * Copyright (c) 2003-2010 Tim Kientzle
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

#include "archive_write_private.h"
#include "archive_options_private.h"

static int	archive_set_format_option(struct archive *a,
		    const char *m, const char *o, const char *v);
static int	archive_set_filter_option(struct archive *a,
		    const char *m, const char *o, const char *v);
static int	archive_set_option(struct archive *a,
		    const char *m, const char *o, const char *v);

int
archive_write_set_format_option(struct archive *a, const char *m, const char *o,
    const char *v)
{
	return _archive_set_option(a, m, o, v,
	    ARCHIVE_WRITE_MAGIC, "archive_write_set_format_option",
	    archive_set_format_option);
}

int
archive_write_set_filter_option(struct archive *a, const char *m, const char *o,
    const char *v)
{
	return _archive_set_option(a, m, o, v,
	    ARCHIVE_WRITE_MAGIC, "archive_write_set_filter_option",
	    archive_set_filter_option);
}

int
archive_write_set_option(struct archive *a, const char *m, const char *o,
    const char *v)
{
	return _archive_set_option(a, m, o, v,
	    ARCHIVE_WRITE_MAGIC, "archive_write_set_option",
	    archive_set_option);
}

int
archive_write_set_options(struct archive *a, const char *options)
{
	return _archive_set_options(a, options,
	    ARCHIVE_WRITE_MAGIC, "archive_write_set_options",
	    archive_set_option);
}

static int
archive_set_format_option(struct archive *_a, const char *m, const char *o,
    const char *v)
{
	struct archive_write *a = (struct archive_write *)_a;

	if (a->format_name == NULL)
		return (m == NULL)?ARCHIVE_FAILED:ARCHIVE_WARN - 1;
	/* If the format name didn't match, return a special code for
	 * _archive_set_option[s]. */
	if (m != NULL && strcmp(m, a->format_name) != 0)
		return (ARCHIVE_WARN - 1);
	if (a->format_options == NULL)
		return (ARCHIVE_WARN);
	return a->format_options(a, o, v);
}

static int
archive_set_filter_option(struct archive *_a, const char *m, const char *o,
    const char *v)
{
	struct archive_write *a = (struct archive_write *)_a;
	struct archive_write_filter *filter;
	int r, rv = ARCHIVE_WARN;

	for (filter = a->filter_first; filter != NULL; filter = filter->next_filter) {
		if (filter->options == NULL)
			continue;
		if (m != NULL && strcmp(filter->name, m) != 0)
			continue;

		r = filter->options(filter, o, v);

		if (r == ARCHIVE_FATAL)
			return (ARCHIVE_FATAL);

		if (m != NULL)
			return (r);

		if (r == ARCHIVE_OK)
			rv = ARCHIVE_OK;
	}
	/* If the filter name didn't match, return a special code for
	 * _archive_set_option[s]. */
	if (rv == ARCHIVE_WARN && m != NULL)
		rv = ARCHIVE_WARN - 1;
	return (rv);
}

static int
archive_set_option(struct archive *a, const char *m, const char *o,
    const char *v)
{
	return _archive_set_either_option(a, m, o, v,
	    archive_set_format_option,
	    archive_set_filter_option);
}
