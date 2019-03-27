/*-
 * Copyright (c) 2012-2017 Dag-Erling Smørgrav
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
 * $OpenPAM: openpam_readword.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <security/pam_appl.h>

#include "openpam_impl.h"
#include "openpam_ctype.h"

#define MIN_WORD_SIZE	32

/*
 * OpenPAM extension
 *
 * Read a word from a file, respecting shell quoting rules.
 */

char *
openpam_readword(FILE *f, int *lineno, size_t *lenp)
{
	char *word;
	size_t size, len;
	int ch, escape, quote;
	int serrno;

	errno = 0;

	/* skip initial whitespace */
	escape = quote = 0;
	while ((ch = getc(f)) != EOF) {
		if (ch == '\n') {
			/* either EOL or line continuation */
			if (!escape)
				break;
			if (lineno != NULL)
				++*lineno;
			escape = 0;
		} else if (escape) {
			/* escaped something else */
			break;
		} else if (ch == '#') {
			/* comment: until EOL, no continuation */
			while ((ch = getc(f)) != EOF)
				if (ch == '\n')
					break;
			break;
		} else if (ch == '\\') {
			escape = 1;
		} else if (!is_ws(ch)) {
			break;
		}
	}
	if (ch == EOF)
		return (NULL);
	ungetc(ch, f);
	if (ch == '\n')
		return (NULL);

	word = NULL;
	size = len = 0;
	while ((ch = fgetc(f)) != EOF && (!is_ws(ch) || quote || escape)) {
		if (ch == '\\' && !escape && quote != '\'') {
			/* escape next character */
			escape = ch;
		} else if ((ch == '\'' || ch == '"') && !quote && !escape) {
			/* begin quote */
			quote = ch;
			/* edge case: empty quoted string */
			if (openpam_straddch(&word, &size, &len, 0) != 0)
				return (NULL);
		} else if (ch == quote && !escape) {
			/* end quote */
			quote = 0;
		} else if (ch == '\n' && escape) {
			/* line continuation */
			escape = 0;
		} else {
			if (escape && quote && ch != '\\' && ch != quote &&
			    openpam_straddch(&word, &size, &len, '\\') != 0) {
				free(word);
				errno = ENOMEM;
				return (NULL);
			}
			if (openpam_straddch(&word, &size, &len, ch) != 0) {
				free(word);
				errno = ENOMEM;
				return (NULL);
			}
			escape = 0;
		}
		if (lineno != NULL && ch == '\n')
			++*lineno;
	}
	if (ch == EOF && ferror(f)) {
		serrno = errno;
		free(word);
		errno = serrno;
		return (NULL);
	}
	if (ch == EOF && (escape || quote)) {
		/* Missing escaped character or closing quote. */
		openpam_log(PAM_LOG_DEBUG, "unexpected end of file");
		free(word);
		errno = EINVAL;
		return (NULL);
	}
	ungetc(ch, f);
	if (lenp != NULL)
		*lenp = len;
	return (word);
}

/**
 * The =openpam_readword function reads the next word from a file, and
 * returns it in a NUL-terminated buffer allocated with =!malloc.
 *
 * A word is a sequence of non-whitespace characters.
 * However, whitespace characters can be included in a word if quoted or
 * escaped according to the following rules:
 *
 *  - An unescaped single or double quote introduces a quoted string,
 *    which ends when the same quote character is encountered a second
 *    time.
 *    The quotes themselves are stripped.
 *
 *  - Within a single- or double-quoted string, all whitespace characters,
 *    including the newline character, are preserved as-is.
 *
 *  - Outside a quoted string, a backslash escapes the next character,
 *    which is preserved as-is, unless that character is a newline, in
 *    which case it is discarded and reading continues at the beginning of
 *    the next line as if the backslash and newline had not been there.
 *    In all cases, the backslash itself is discarded.
 *
 *  - Within a single-quoted string, double quotes and backslashes are
 *    preserved as-is.
 *
 *  - Within a double-quoted string, a single quote is preserved as-is,
 *    and a backslash is preserved as-is unless used to escape a double
 *    quote.
 *
 * In addition, if the first non-whitespace character on the line is a
 * hash character (#), the rest of the line is discarded.
 * If a hash character occurs within a word, however, it is preserved
 * as-is.
 * A backslash at the end of a comment does cause line continuation.
 *
 * If =lineno is not =NULL, the integer variable it points to is
 * incremented every time a quoted or escaped newline character is read.
 *
 * If =lenp is not =NULL, the length of the word (after quotes and
 * backslashes have been removed) is stored in the variable it points to.
 *
 * RETURN VALUES
 *
 * If successful, the =openpam_readword function returns a pointer to a
 * dynamically allocated NUL-terminated string containing the first word
 * encountered on the line.
 *
 * The caller is responsible for releasing the returned buffer by passing
 * it to =!free.
 *
 * If =openpam_readword reaches the end of the line or file before any
 * characters are copied to the word, it returns =NULL.  In the former
 * case, the newline is pushed back to the file.
 *
 * If =openpam_readword reaches the end of the file while a quote or
 * backslash escape is in effect, it sets :errno to =EINVAL and returns
 * =NULL.
 *
 * IMPLEMENTATION NOTES
 *
 * The parsing rules are intended to be equivalent to the normal POSIX
 * shell quoting rules.
 * Any discrepancy is a bug and should be reported to the author along
 * with sample input that can be used to reproduce the error.
 *
 * >openpam_readline
 * >openpam_readlinev
 *
 * AUTHOR DES
 */
