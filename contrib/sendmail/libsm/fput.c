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
SM_RCSID("@(#)$Id: fput.c,v 1.21 2013-11-22 20:51:42 ca Exp $")
#include <string.h>
#include <errno.h>
#include <sm/io.h>
#include <sm/assert.h>
#include "local.h"
#include "fvwrite.h"

/*
**  SM_IO_FPUTS -- add a string to the buffer for the file pointer
**
**	Parameters:
**		fp -- the file pointer for the buffer to be written to
**		timeout -- time to complete the put-string
**		s -- string to be placed in the buffer
**
**	Returns:
**		Failure: returns SM_IO_EOF
**		Success: returns 0 (zero)
*/

int
sm_io_fputs(fp, timeout, s)
	SM_FILE_T *fp;
	int timeout;
	const char *s;
{
	struct sm_uio uio;
	struct sm_iov iov;

	SM_REQUIRE_ISA(fp, SmFileMagic);
	iov.iov_base = (void *) s;
	iov.iov_len = uio.uio_resid = strlen(s);
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	return sm_fvwrite(fp, timeout, &uio);
}
