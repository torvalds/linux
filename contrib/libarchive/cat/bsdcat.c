/*-
 * Copyright (c) 2011-2014, Mike Kazantsev
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

#include "bsdcat_platform.h"
__FBSDID("$FreeBSD$");

#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "bsdcat.h"
#include "err.h"

#define	BYTES_PER_BLOCK	(20*512)

static struct archive *a;
static struct archive_entry *ae;
static const char *bsdcat_current_path;
static int exit_status = 0;


void
usage(FILE *stream, int eval)
{
	const char *p;
	p = lafe_getprogname();
	fprintf(stream,
	    "Usage: %s [-h] [--help] [--version] [--] [filenames...]\n", p);
	exit(eval);
}

static void
version(void)
{
	printf("bsdcat %s - %s \n",
	    BSDCAT_VERSION_STRING,
	    archive_version_details());
	exit(0);
}

void
bsdcat_next(void)
{
	if (a != NULL) {
		if (archive_read_close(a) != ARCHIVE_OK)
			bsdcat_print_error();
		archive_read_free(a);
	}

	a = archive_read_new();
	archive_read_support_filter_all(a);
	archive_read_support_format_empty(a);
	archive_read_support_format_raw(a);
}

void
bsdcat_print_error(void)
{
	lafe_warnc(0, "%s: %s",
	    bsdcat_current_path, archive_error_string(a));
	exit_status = 1;
}

void
bsdcat_read_to_stdout(const char* filename)
{
	int r;

	if (archive_read_open_filename(a, filename, BYTES_PER_BLOCK)
	    != ARCHIVE_OK)
		bsdcat_print_error();
	else if (r = archive_read_next_header(a, &ae),
		 r != ARCHIVE_OK && r != ARCHIVE_EOF)
		bsdcat_print_error();
	else if (r == ARCHIVE_EOF)
		/* for empty payloads don't try and read data */
		;
	else if (archive_read_data_into_fd(a, 1) != ARCHIVE_OK)
		bsdcat_print_error();
	if (archive_read_close(a) != ARCHIVE_OK)
		bsdcat_print_error();
	archive_read_free(a);
	a = NULL;
}

int
main(int argc, char **argv)
{
	struct bsdcat *bsdcat, bsdcat_storage;
	int c;

	bsdcat = &bsdcat_storage;
	memset(bsdcat, 0, sizeof(*bsdcat));

	lafe_setprogname(*argv, "bsdcat");

	bsdcat->argv = argv;
	bsdcat->argc = argc;

	while ((c = bsdcat_getopt(bsdcat)) != -1) {
		switch (c) {
		case 'h':
			usage(stdout, 0);
			break;
		case OPTION_VERSION:
			version();
			break;
		default:
			usage(stderr, 1);
		}
	}

	bsdcat_next();
	if (*bsdcat->argv == NULL) {
		bsdcat_current_path = "<stdin>";
		bsdcat_read_to_stdout(NULL);
	} else {
		while (*bsdcat->argv) {
			bsdcat_current_path = *bsdcat->argv++;
			bsdcat_read_to_stdout(bsdcat_current_path);
			bsdcat_next();
		}
		archive_read_free(a); /* Help valgrind & friends */
	}

	exit(exit_status);
}
