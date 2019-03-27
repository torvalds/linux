/*-
 * Copyright (c) 2012-2016 Dag-Erling Smørgrav
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
 * $OpenPAM: openpam_readlinev.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <security/pam_appl.h>

#include "openpam_impl.h"

#define MIN_WORDV_SIZE	32

/*
 * OpenPAM extension
 *
 * Read a line from a file and split it into words.
 */

char **
openpam_readlinev(FILE *f, int *lineno, int *lenp)
{
	char *word, **wordv, **tmp;
	size_t wordlen, wordvsize;
	int ch, serrno, wordvlen;

	wordvsize = MIN_WORDV_SIZE;
	wordvlen = 0;
	if ((wordv = malloc(wordvsize * sizeof *wordv)) == NULL) {
		openpam_log(PAM_LOG_ERROR, "malloc(): %m");
		errno = ENOMEM;
		return (NULL);
	}
	wordv[wordvlen] = NULL;
	while ((word = openpam_readword(f, lineno, &wordlen)) != NULL) {
		if ((unsigned int)wordvlen + 1 >= wordvsize) {
			/* need to expand the array */
			wordvsize *= 2;
			tmp = realloc(wordv, wordvsize * sizeof *wordv);
			if (tmp == NULL) {
				openpam_log(PAM_LOG_ERROR, "malloc(): %m");
				errno = ENOMEM;
				break;
			}
			wordv = tmp;
		}
		/* insert our word */
		wordv[wordvlen++] = word;
		wordv[wordvlen] = NULL;
		word = NULL;
	}
	if (errno != 0) {
		/* I/O error or out of memory */
		serrno = errno;
		while (wordvlen--)
			free(wordv[wordvlen]);
		free(wordv);
		free(word);
		errno = serrno;
		return (NULL);
	}
	/* assert(!ferror(f)) */
	ch = fgetc(f);
	/* assert(ch == EOF || ch == '\n') */
	if (ch == EOF && wordvlen == 0) {
		free(wordv);
		return (NULL);
	}
	if (ch == '\n' && lineno != NULL)
		++*lineno;
	if (lenp != NULL)
		*lenp = wordvlen;
	return (wordv);
}

/**
 * The =openpam_readlinev function reads a line from a file, splits it
 * into words according to the rules described in the =openpam_readword
 * manual page, and returns a list of those words.
 *
 * If =lineno is not =NULL, the integer variable it points to is
 * incremented every time a newline character is read.
 * This includes quoted or escaped newline characters and the newline
 * character at the end of the line.
 *
 * If =lenp is not =NULL, the number of words on the line is stored in the
 * variable to which it points.
 *
 * RETURN VALUES
 *
 * If successful, the =openpam_readlinev function returns a pointer to a
 * dynamically allocated array of pointers to individual dynamically
 * allocated NUL-terminated strings, each containing a single word, in the
 * order in which they were encountered on the line.
 * The array is terminated by a =NULL pointer.
 *
 * The caller is responsible for freeing both the array and the individual
 * strings by passing each of them to =!free.
 *
 * If the end of the line was reached before any words were read,
 * =openpam_readlinev returns a pointer to a dynamically allocated array
 * containing a single =NULL pointer.
 *
 * The =openpam_readlinev function can fail and return =NULL for one of
 * four reasons:
 *
 *  - The end of the file was reached before any words were read; :errno is
 *    zero, =!ferror returns zero, and =!feof returns a non-zero value.
 *
 *  - The end of the file was reached while a quote or backslash escape
 *    was in effect; :errno is set to =EINVAL, =!ferror returns zero, and
 *    =!feof returns a non-zero value.
 *
 *  - An error occurred while reading from the file; :errno is non-zero,
 *    =!ferror returns a non-zero value and =!feof returns zero.
 *
 *  - A =!malloc or =!realloc call failed; :errno is set to =ENOMEM,
 *    =!ferror returns a non-zero value, and =!feof may or may not return
 *    a non-zero value.
 *
 * >openpam_readline
 * >openpam_readword
 *
 * AUTHOR DES
 */
