/*
 * Copyright (c) Ian F. Darwin 1986-1995.
 * Software written by Ian F. Darwin and others;
 * maintained 1995-present by Christos Zoulas and others.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * is_tar() -- figure out whether file is a tar archive.
 *
 * Stolen (by the author!) from the public domain tar program:
 * Public Domain version written 26 Aug 1985 John Gilmore (ihnp4!hoptoad!gnu).
 *
 * @(#)list.c 1.18 9/23/86 Public Domain - gnu
 *
 * Comments changed and some code/comments reformatted
 * for file command by Ian Darwin.
 */

#include "file.h"

#ifndef lint
FILE_RCSID("@(#)$File: is_tar.c,v 1.41 2017/11/02 20:25:39 christos Exp $")
#endif

#include "magic.h"
#include <string.h>
#include <ctype.h>
#include "tar.h"

#define	isodigit(c)	( ((c) >= '0') && ((c) <= '7') )

private int is_tar(const unsigned char *, size_t);
private int from_oct(const char *, size_t);	/* Decode octal number */

static const char tartype[][32] = {	/* should be equal to messages */
	"tar archive",			/* found in ../magic/Magdir/archive */
	"POSIX tar archive",
	"POSIX tar archive (GNU)",	/*  */
};

protected int
file_is_tar(struct magic_set *ms, const struct buffer *b)
{
	const unsigned char *buf = b->fbuf;
	size_t nbytes = b->flen;
	/*
	 * Do the tar test first, because if the first file in the tar
	 * archive starts with a dot, we can confuse it with an nroff file.
	 */
	int tar;
	int mime = ms->flags & MAGIC_MIME;

	if ((ms->flags & (MAGIC_APPLE|MAGIC_EXTENSION)) != 0)
		return 0;

	tar = is_tar(buf, nbytes);
	if (tar < 1 || tar > 3)
		return 0;

	if (file_printf(ms, "%s", mime ? "application/x-tar" :
	    tartype[tar - 1]) == -1)
		return -1;
	return 1;
}

/*
 * Return
 *	0 if the checksum is bad (i.e., probably not a tar archive),
 *	1 for old UNIX tar file,
 *	2 for Unix Std (POSIX) tar file,
 *	3 for GNU tar file.
 */
private int
is_tar(const unsigned char *buf, size_t nbytes)
{
	const union record *header = (const union record *)(const void *)buf;
	size_t i;
	int sum, recsum;
	const unsigned char *p, *ep;

	if (nbytes < sizeof(*header))
		return 0;

	recsum = from_oct(header->header.chksum, sizeof(header->header.chksum));

	sum = 0;
	p = header->charptr;
	ep = header->charptr + sizeof(*header);
	while (p < ep)
		sum += *p++;

	/* Adjust checksum to count the "chksum" field as blanks. */
	for (i = 0; i < sizeof(header->header.chksum); i++)
		sum -= header->header.chksum[i];
	sum += ' ' * sizeof(header->header.chksum);

	if (sum != recsum)
		return 0;	/* Not a tar archive */

	if (strncmp(header->header.magic, GNUTMAGIC,
	    sizeof(header->header.magic)) == 0)
		return 3;		/* GNU Unix Standard tar archive */

	if (strncmp(header->header.magic, TMAGIC,
	    sizeof(header->header.magic)) == 0)
		return 2;		/* Unix Standard tar archive */

	return 1;			/* Old fashioned tar archive */
}


/*
 * Quick and dirty octal conversion.
 *
 * Result is -1 if the field is invalid (all blank, or non-octal).
 */
private int
from_oct(const char *where, size_t digs)
{
	int	value;

	if (digs == 0)
		return -1;

	while (isspace((unsigned char)*where)) {	/* Skip spaces */
		where++;
		if (digs-- == 0)
			return -1;		/* All blank field */
	}
	value = 0;
	while (digs > 0 && isodigit(*where)) {	/* Scan til non-octal */
		value = (value << 3) | (*where++ - '0');
		digs--;
	}

	if (digs > 0 && *where && !isspace((unsigned char)*where))
		return -1;			/* Ended on non-(space/NUL) */

	return value;
}
