/*-
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 */

#include "config.h"

#ifndef lint
static const char sccsid[] = "$Id: ex_source.c,v 10.17 2011/12/19 16:17:06 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stat.h>

#include <bitstring.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/common.h"

/*
 * ex_source -- :source file
 *	Execute ex commands from a file.
 *
 * PUBLIC: int ex_source(SCR *, EXCMD *);
 */
int
ex_source(SCR *sp, EXCMD *cmdp)
{
	struct stat sb;
	int fd, len;
	char *bp;
	char *name, *np;
	size_t nlen;
	CHAR_T *wp;
	size_t wlen;
	int rc;

	INT2CHAR(sp, cmdp->argv[0]->bp, cmdp->argv[0]->len + 1, name, nlen);
	if ((fd = open(name, O_RDONLY, 0)) < 0 || fstat(fd, &sb))
		goto err;

	/*
	 * XXX
	 * I'd like to test to see if the file is too large to malloc.  Since
	 * we don't know what size or type off_t's or size_t's are, what the
	 * largest unsigned integral type is, or what random insanity the local
	 * C compiler will perpetrate, doing the comparison in a portable way
	 * is flatly impossible.  So, put an fairly unreasonable limit on it,
	 * I don't want to be dropping core here.
	 */
#define	MEGABYTE	1048576
	if (sb.st_size > MEGABYTE) {
		errno = ENOMEM;
		goto err;
	}

	MALLOC(sp, bp, char *, (size_t)sb.st_size + 1);
	if (bp == NULL) {
		(void)close(fd);
		return (1);
	}
	bp[sb.st_size] = '\0';

	/* Read the file into memory. */
	len = read(fd, bp, (int)sb.st_size);
	(void)close(fd);
	if (len == -1 || len != sb.st_size) {
		if (len != sb.st_size)
			errno = EIO;
		free(bp);
err:		msgq_str(sp, M_SYSERR, name, "%s");
		return (1);
	}

	np = strdup(name);
	if (CHAR2INT(sp, bp, (size_t)sb.st_size + 1, wp, wlen))
		msgq(sp, M_ERR, "323|Invalid input. Truncated.");
	/* Put it on the ex queue. */
	rc = ex_run_str(sp, np, wp, wlen - 1, 1, 0);
	if (np != NULL)
		free(np);
	free(bp);
	return (rc);
}
