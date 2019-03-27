/*-
 * Copyright (c) 2012 Michihiro NAKAJIMA
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

#include "bsdtar_platform.h"
__FBSDID("$FreeBSD$");

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "bsdtar.h"
#include "err.h"

struct creation_set {
	char		 *create_format;
	struct filter_set {
		int	  program;	/* Set 1 if filter is a program name */
		char	 *filter_name;
	}		 *filters;
	int		  filter_count;
};

struct suffix_code_t {
	const char *suffix;
	const char *form;
};

static const char *
get_suffix_code(const struct suffix_code_t *tbl, const char *suffix)
{
	int i;

	if (suffix == NULL)
		return (NULL);
	for (i = 0; tbl[i].suffix != NULL; i++) {
		if (strcmp(tbl[i].suffix, suffix) == 0)
			return (tbl[i].form);
	}
	return (NULL);
}

static const char *
get_filter_code(const char *suffix)
{
	/* A pair of suffix and compression/filter. */
	static const struct suffix_code_t filters[] = {
		{ ".Z",		"compress" },
		{ ".bz2",	"bzip2" },
		{ ".gz",	"gzip" },
		{ ".grz",	"grzip" },
		{ ".lrz",	"lrzip" },
		{ ".lz",	"lzip" },
		{ ".lz4",	"lz4" },
		{ ".lzo",	"lzop" },
		{ ".lzma",	"lzma" },
		{ ".uu",	"uuencode" },
		{ ".xz",	"xz" },
		{ ".zst",	"zstd"},
		{ NULL,		NULL }
	};

	return get_suffix_code(filters, suffix);
}

static const char *
get_format_code(const char *suffix)
{
	/* A pair of suffix and format. */
	static const struct suffix_code_t formats[] = {
		{ ".7z",	"7zip" },
		{ ".ar",	"arbsd" },
		{ ".cpio",	"cpio" },
		{ ".iso",	"iso9960" },
		{ ".mtree",	"mtree" },
		{ ".shar",	"shar" },
		{ ".tar",	"paxr" },
		{ ".warc",	"warc" },
		{ ".xar",	"xar" },
		{ ".zip",	"zip" },
		{ NULL,		NULL }
	};

	return get_suffix_code(formats, suffix);
}

static const char *
decompose_alias(const char *suffix)
{
	static const struct suffix_code_t alias[] = {
		{ ".taz",	".tar.gz" },
		{ ".tgz",	".tar.gz" },
		{ ".tbz",	".tar.bz2" },
		{ ".tbz2",	".tar.bz2" },
		{ ".tz2",	".tar.bz2" },
		{ ".tlz",	".tar.lzma" },
		{ ".txz",	".tar.xz" },
		{ ".tzo",	".tar.lzo" },
		{ ".taZ",	".tar.Z" },
		{ ".tZ",	".tar.Z" },
		{ ".tzst",	".tar.zst" },
		{ NULL,		NULL }
	};

	return get_suffix_code(alias, suffix);
}

static void
_cset_add_filter(struct creation_set *cset, int program, const char *filter)
{
	struct filter_set *new_ptr;
	char *new_filter;

	new_ptr = (struct filter_set *)realloc(cset->filters,
	    sizeof(*cset->filters) * (cset->filter_count + 1));
	if (new_ptr == NULL)
		lafe_errc(1, 0, "No memory");
	new_filter = strdup(filter);
	if (new_filter == NULL)
		lafe_errc(1, 0, "No memory");
	cset->filters = new_ptr;
	cset->filters[cset->filter_count].program = program;
	cset->filters[cset->filter_count].filter_name = new_filter;
	cset->filter_count++;
}

void
cset_add_filter(struct creation_set *cset, const char *filter)
{
	_cset_add_filter(cset, 0, filter);
}

void
cset_add_filter_program(struct creation_set *cset, const char *filter)
{
	_cset_add_filter(cset, 1, filter);
}

int
cset_read_support_filter_program(struct creation_set *cset, struct archive *a)
{
	int cnt = 0, i;

	for (i = 0; i < cset->filter_count; i++) {
		if (cset->filters[i].program) {
			archive_read_support_filter_program(a,
			    cset->filters[i].filter_name);
			++cnt;
		}
	}
	return (cnt);
}

int
cset_write_add_filters(struct creation_set *cset, struct archive *a,
    const void **filter_name)
{
	int cnt = 0, i, r;

	for (i = 0; i < cset->filter_count; i++) {
		if (cset->filters[i].program)
			r = archive_write_add_filter_program(a,
				cset->filters[i].filter_name);
		else
			r = archive_write_add_filter_by_name(a,
				cset->filters[i].filter_name);
		if (r < ARCHIVE_WARN) {
			*filter_name = cset->filters[i].filter_name;
			return (r);
		}
		++cnt;
	}
	return (cnt);
}

void
cset_set_format(struct creation_set *cset, const char *format)
{
	char *f;

	f = strdup(format);
	if (f == NULL)
		lafe_errc(1, 0, "No memory");
	free(cset->create_format);
	cset->create_format = f;
}

const char *
cset_get_format(struct creation_set *cset)
{
	return (cset->create_format);
}

static void
_cleanup_filters(struct filter_set *filters, int count)
{
	int i;

	for (i = 0; i < count; i++)
		free(filters[i].filter_name);
	free(filters);
}

/*
 * Clean up a creation set.
 */
void
cset_free(struct creation_set *cset)
{
	_cleanup_filters(cset->filters, cset->filter_count);
	free(cset->create_format);
	free(cset);
}

struct creation_set *
cset_new(void)
{
	return calloc(1, sizeof(struct creation_set));
}

/*
 * Build a creation set by a file name suffix.
 */
int
cset_auto_compress(struct creation_set *cset, const char *filename)
{
	struct filter_set *old_filters;
	char *name, *p;
	const char *code;
	int old_filter_count;

	name = strdup(filename);
	if (name == NULL)
		lafe_errc(1, 0, "No memory");
	/* Save previous filters. */
	old_filters = cset->filters;
	old_filter_count = cset->filter_count;
	cset->filters = NULL;
	cset->filter_count = 0;

	for (;;) {
		/* Get the suffix. */
		p = strrchr(name, '.');
		if (p == NULL)
			break;
		/* Suppose it indicates compression/filter type
		 * such as ".gz". */
		code = get_filter_code(p);
		if (code != NULL) {
			cset_add_filter(cset, code);
			*p = '\0';
			continue;
		}
		/* Suppose it indicates format type such as ".tar". */
		code = get_format_code(p);
		if (code != NULL) {
			cset_set_format(cset, code);
			break;
		}
		/* Suppose it indicates alias such as ".tgz". */
		code = decompose_alias(p);
		if (code == NULL)
			break;
		/* Replace the suffix. */
		*p = '\0';
		name = realloc(name, strlen(name) + strlen(code) + 1);
		if (name == NULL)
			lafe_errc(1, 0, "No memory");
		strcat(name, code);
	}
	free(name);
	if (cset->filters) {
		struct filter_set *v;
		int i, r;

		/* Release previous filters. */
		_cleanup_filters(old_filters, old_filter_count);

		v = malloc(sizeof(*v) * cset->filter_count);
		if (v == NULL)
			lafe_errc(1, 0, "No memory");
		/* Reverse filter sequence. */
		for (i = 0, r = cset->filter_count; r > 0; )
			v[i++] = cset->filters[--r];
		free(cset->filters);
		cset->filters = v;
		return (1);
	} else {
		/* Put previous filters back. */
		cset->filters = old_filters;
		cset->filter_count = old_filter_count;
		return (0);
	}
}
