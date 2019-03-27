#include "config.h"

#if HAVE_GETLINE

int dummy;

#else

/*	$Id: compat_getline.c,v 1.1 2015/11/07 20:52:52 schwarze Exp $ */
/*
 * Copyright (c) 2015 Ingo Schwarze <schwarze@openbsd.org>
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

#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

ssize_t
getline(char **buf, size_t *bufsz, FILE *fp)
{
	char	*nbuf;
	size_t	 nbufsz, pos;
	int	 c;

	if (buf == NULL || bufsz == NULL) {
		errno = EINVAL;
		return -1;
	}

	if (*buf == NULL)
		*bufsz = 0;
	else
		**buf = '\0';

	pos = 0;
	for (;;) {
		if (pos + 1 >= *bufsz) {
			nbufsz = *bufsz ? *bufsz * 2 : BUFSIZ;
			if ((nbuf = realloc(*buf, nbufsz)) == NULL)
				return -1;
			*buf = nbuf;
			*bufsz = nbufsz;
		}
		if ((c = fgetc(fp)) == EOF) {
			(*buf)[pos] = '\0';
			return pos > 0 && feof(fp) ? (ssize_t)pos : -1;
		}
		(*buf)[pos++] = c;
		(*buf)[pos] = '\0';
		if (c == '\n')
			return pos;
	}
}

#endif
