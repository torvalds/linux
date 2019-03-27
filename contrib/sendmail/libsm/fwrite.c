/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: fwrite.c,v 1.25 2013-11-22 20:51:43 ca Exp $")
#include <errno.h>
#include <sm/io.h>
#include <sm/assert.h>
#include "local.h"
#include "fvwrite.h"

/*
**  SM_IO_WRITE -- write to a file pointer
**
**	Parameters:
**		fp -- file pointer writing to
**		timeout -- time to complete the write
**		buf -- location of data to be written
**		size -- number of bytes to be written
**
**	Result:
**		Failure: returns 0 _and_ sets errno
**		Success: returns >=0 with errno unchanged, where the
**			number returned is the number of bytes written.
*/

size_t
sm_io_write(fp, timeout, buf, size)
	SM_FILE_T *fp;
	int timeout;
	const void *buf;
	size_t size;
{
	struct sm_uio uio;
	struct sm_iov iov;

	SM_REQUIRE_ISA(fp, SmFileMagic);

	if (fp->f_write == NULL)
	{
		errno = ENODEV;
		return 0;
	}

	iov.iov_base = (void *) buf;
	uio.uio_resid = iov.iov_len = size;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;

	/* The usual case is success (sm_fvwrite returns 0) */
	if (sm_fvwrite(fp, timeout, &uio) == 0)
		return size;

	/* else return number of bytes actually written */
	return size - uio.uio_resid;
}
