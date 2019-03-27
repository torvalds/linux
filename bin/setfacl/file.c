/*-
 * Copyright (c) 2001 Chris D. Faulhaber
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/acl.h>

#include <err.h>
#include <stdio.h>
#include <string.h>

#include "setfacl.h"

/*
 * read acl text from a file and return the corresponding acl
 */
acl_t
get_acl_from_file(const char *filename)
{
	FILE *file;
	size_t len;
	char buf[BUFSIZ+1];

	if (filename == NULL)
		err(1, "(null) filename in get_acl_from_file()");

	if (strcmp(filename, "-") == 0) {
		if (have_stdin)
			err(1, "cannot specify more than one stdin");
		file = stdin;
		have_stdin = true;
	} else {
		file = fopen(filename, "r");
		if (file == NULL)
			err(1, "fopen() %s failed", filename);
	}

	len = fread(buf, (size_t)1, sizeof(buf) - 1, file);
	buf[len] = '\0';
	if (ferror(file) != 0) {
		fclose(file);
		err(1, "error reading from %s", filename);
	} else if (feof(file) == 0) {
		fclose(file);
		errx(1, "line too long in %s", filename);
	}

	fclose(file);

	return (acl_from_text(buf));
}
