/*-
 * Copyright (c) 2011, Joseph Koshy
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
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

#include <sys/types.h>
#include <sys/stat.h>

#include <stdlib.h>
#include <unistd.h>

#include "libelftc.h"
#include "_libelftc.h"

#if	ELFTC_HAVE_MMAP
#include <sys/mman.h>
#endif

ELFTC_VCSID("$Id: elftc_copyfile.c 3297 2016-01-09 15:26:34Z jkoshy $");

/*
 * Copy the contents referenced by 'ifd' to 'ofd'.  Returns 0 on
 * success and -1 on error.
 */

int
elftc_copyfile(int ifd, int ofd)
{
	size_t file_size, n;
	int buf_mmapped;
	struct stat sb;
	char *b, *buf;
	ssize_t nr, nw;

	/* Determine the input file's size. */
	if (fstat(ifd, &sb) < 0)
		return (-1);

	/* Skip files without content. */
	if (sb.st_size == 0)
		return (0);

	buf = NULL;
	buf_mmapped = 0;
	file_size = (size_t) sb.st_size;

#if	ELFTC_HAVE_MMAP
	/*
	 * Prefer mmap() if it is available.
	 */
	buf = mmap(NULL, file_size, PROT_READ, MAP_SHARED, ifd, (off_t) 0);
	if (buf != MAP_FAILED)
		buf_mmapped = 1;
	else
		buf = NULL;
#endif

	/*
	 * If mmap() is not available, or if the mmap() operation
	 * failed, allocate a buffer, and read in input data.
	 */
	if (buf_mmapped == false) {
		if ((buf = malloc(file_size)) == NULL)
			return (-1);
		b = buf;
		for (n = file_size; n > 0; n -= (size_t) nr, b += nr) {
			if ((nr = read(ifd, b, n)) < 0) {
				free(buf);
				return (-1);
			}
		}
	}

	/*
	 * Write data to the output file descriptor.
	 */
	for (n = file_size, b = buf; n > 0; n -= (size_t) nw, b += nw)
		if ((nw = write(ofd, b, n)) <= 0)
			break;

	/* Release the input buffer. */
#if	ELFTC_HAVE_MMAP
	if (buf_mmapped && munmap(buf, file_size) < 0)
		return (-1);
#endif

	if (!buf_mmapped)
		free(buf);

	return (n > 0 ? -1 : 0);
}

