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
SM_RCSID("@(#)$Id: sscanf.c,v 1.26 2013-11-22 20:51:43 ca Exp $")
#include <string.h>
#include <sm/varargs.h>
#include <sm/io.h>
#include "local.h"

/*
**  SM_EOFREAD -- dummy read function for faked file below
**
**	Parameters:
**		fp -- file pointer
**		buf -- location to place read data
**		len -- number of bytes to read
**
**	Returns:
**		0 (zero) always
*/

static ssize_t
sm_eofread __P((
	SM_FILE_T *fp,
	char *buf,
	size_t len));

/* ARGSUSED0 */
static ssize_t
sm_eofread(fp, buf, len)
	SM_FILE_T *fp;
	char *buf;
	size_t len;
{
	return 0;
}

/*
**  SM_IO_SSCANF -- scan a string to find data units
**
**	Parameters:
**		str -- strings containing data
**		fmt -- format directive for finding data units
**		... -- memory locations to place format found data units
**
**	Returns:
**		Failure: SM_IO_EOF
**		Success: number of data units found
**
**	Side Effects:
**		Attempts to strlen() 'str'; if not a '\0' terminated string
**			then the call may SEGV/fail.
**		Faking the string 'str' as a file.
*/

int
#if SM_VA_STD
sm_io_sscanf(const char *str, char const *fmt, ...)
#else /* SM_VA_STD */
sm_io_sscanf(str, fmt, va_alist)
	const char *str;
	char *fmt;
	va_dcl
#endif /* SM_VA_STD */
{
	int ret;
	SM_FILE_T fake;
	SM_VA_LOCAL_DECL

	fake.sm_magic = SmFileMagic;
	fake.f_flags = SMRD;
	fake.f_bf.smb_base = fake.f_p = (unsigned char *) str;
	fake.f_bf.smb_size = fake.f_r = strlen(str);
	fake.f_file = -1;
	fake.f_read = sm_eofread;
	fake.f_write = NULL;
	fake.f_close = NULL;
	fake.f_open = NULL;
	fake.f_seek = NULL;
	fake.f_setinfo = fake.f_getinfo = NULL;
	fake.f_type = "sm_io_sscanf:fake";
	fake.f_flushfp = NULL;
	fake.f_ub.smb_base = NULL;
	fake.f_timeout = SM_TIME_FOREVER;
	fake.f_timeoutstate = SM_TIME_BLOCK;
	SM_VA_START(ap, fmt);
	ret = sm_vfscanf(&fake, SM_TIME_FOREVER, fmt, ap);
	SM_VA_END(ap);
	return ret;
}
