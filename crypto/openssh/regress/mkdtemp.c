/*
 * Copyright (c) 2017 Colin Watson <cjwatson@debian.org>
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
 */

/* Roughly equivalent to "mktemp -d -t TEMPLATE", but portable. */

#include "includes.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "log.h"

static void
usage(void)
{
	fprintf(stderr, "mkdtemp template\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	const char *base;
	const char *tmpdir;
	char template[PATH_MAX];
	int r;
	char *dir;

	if (argc != 2)
		usage();
	base = argv[1];

	if ((tmpdir = getenv("TMPDIR")) == NULL)
		tmpdir = "/tmp";
	r = snprintf(template, sizeof(template), "%s/%s", tmpdir, base);
	if (r < 0 || (size_t)r >= sizeof(template))
		fatal("template string too long");
	dir = mkdtemp(template);
	if (dir == NULL) {
		perror("mkdtemp");
		exit(1);
	}
	puts(dir);
	return 0;
}
