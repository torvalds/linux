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
SM_RCSID("@(#)$Id: fscanf.c,v 1.18 2013-11-22 20:51:42 ca Exp $")
#include <sm/varargs.h>
#include <sm/assert.h>
#include <sm/io.h>
#include "local.h"

/*
**  SM_IO_FSCANF -- convert input data to translated format
**
**	Parameters:
**		fp -- the file pointer to obtain the data from
**		timeout -- time to complete scan
**		fmt -- the format to translate the data to
**		... -- memory locations to place the formated data
**
**	Returns:
**		Failure: returns SM_IO_EOF
**		Success: returns the number of data units translated
*/

int
#if SM_VA_STD
sm_io_fscanf(SM_FILE_T *fp, int timeout, char const *fmt, ...)
#else /* SM_VA_STD */
sm_io_fscanf(fp, timeout, fmt, va_alist)
	SM_FILE_T *fp;
	int timeout;
	char *fmt;
	va_dcl
#endif /* SM_VA_STD */
{
	int ret;
	SM_VA_LOCAL_DECL

	SM_REQUIRE_ISA(fp, SmFileMagic);
	SM_VA_START(ap, fmt);
	ret = sm_vfscanf(fp, timeout, fmt, ap);
	SM_VA_END(ap);
	return ret;
}
