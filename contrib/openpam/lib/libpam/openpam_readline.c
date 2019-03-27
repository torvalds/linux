/*-
 * Copyright (c) 2003 Networks Associates Technology, Inc.
 * Copyright (c) 2004-2011 Dag-Erling Smørgrav
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by ThinkSec AS and
 * Network Associates Laboratories, the Security Research Division of
 * Network Associates, Inc.  under DARPA/SPAWAR contract N66001-01-C-8035
 * ("CBOSS"), as part of the DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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
 *
 * $OpenPAM: openpam_readline.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>

#include <security/pam_appl.h>

#include "openpam_impl.h"

#define MIN_LINE_LENGTH 128

/*
 * OpenPAM extension
 *
 * Read a line from a file.
 */

char *
openpam_readline(FILE *f, int *lineno, size_t *lenp)
{
	char *line;
	size_t len, size;
	int ch;

	line = NULL;
	if (openpam_straddch(&line, &size, &len, 0) != 0)
		return (NULL);
	for (;;) {
		ch = fgetc(f);
		/* strip comment */
		if (ch == '#') {
			do {
				ch = fgetc(f);
			} while (ch != EOF && ch != '\n');
		}
		/* eof */
		if (ch == EOF) {
			/* done */
			break;
		}
		/* eol */
		if (ch == '\n') {
			if (lineno != NULL)
				++*lineno;
			/* skip blank lines */
			if (len == 0)
				continue;
			/* continuation */
			if (line[len - 1] == '\\') {
				line[--len] = '\0';
				continue;
			}
			/* done */
			break;
		}
		/* anything else */
		if (openpam_straddch(&line, &size, &len, ch) != 0)
			goto fail;
	}
	if (len == 0)
		goto fail;
	if (lenp != NULL)
		*lenp = len;
	return (line);
fail:
	FREE(line);
	return (NULL);
}

/**
 * DEPRECATED openpam_readlinev
 *
 * The =openpam_readline function reads a line from a file, and returns it
 * in a NUL-terminated buffer allocated with =!malloc.
 *
 * The =openpam_readline function performs a certain amount of processing
 * on the data it reads:
 *
 *  - Comments (introduced by a hash sign) are stripped.
 *
 *  - Blank lines are ignored.
 *
 *  - If a line ends in a backslash, the backslash is stripped and the
 *    next line is appended.
 *
 * If =lineno is not =NULL, the integer variable it points to is
 * incremented every time a newline character is read.
 *
 * If =lenp is not =NULL, the length of the line (not including the
 * terminating NUL character) is stored in the variable it points to.
 *
 * The caller is responsible for releasing the returned buffer by passing
 * it to =!free.
 *
 * >openpam_readlinev
 * >openpam_readword
 */
