/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: syslogio.c,v 1.30 2013-11-22 20:51:43 ca Exp $")
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>
#ifdef SM_RPOOL
# include <sm/rpool.h>
#endif /* SM_RPOOL */
#include <sm/io.h>
#include "local.h"

/*
**  Overall:
**  This is a output file type that copies its output to the syslog daemon.
**  Each line of output is written as a separate syslog message.
**  The client is responsible for calling openlog() before writing to
**  any syslog file, and calling closelog() after all syslog output is complete.
**  The only state associated with a syslog file is 'int priority',
**  which we store in fp->f_ival.
*/

/*
**  SM_SYSLOGOPEN -- open a file pointer to syslog
**
**	Parameters:
**		fp -- file pointer assigned for the open
**		info -- priority level of the syslog messages
**		flags -- not used
**		rpool -- ignored
**
**	Returns:
**		0 (zero) success always (see Overall)
*/

int
sm_syslogopen(fp, info, flags, rpool)
	SM_FILE_T *fp;
	const void *info;
	int flags;
	const void *rpool;
{
	int *priority = (int *)info;

	fp->f_ival = *priority;
	return 0;
}

/*
**  SM_SYSLOGREAD -- read function for syslog
**
**  This is a "stub" function (placeholder) that always returns an error.
**  It is an error to read syslog.
**
**	Parameters:
**		fp -- the file pointer
**		buf -- buffer to place the data read
**		n -- number of bytes to read
**
**	Returns:
**		-1 (error) always and sets errno
*/

ssize_t
sm_syslogread(fp, buf, n)
	SM_FILE_T *fp;
	char *buf;
	size_t n;
{
	/* an error to read */
	errno = ENODEV;
	return -1;
}

/*
**  SM_SYSLOGWRITE -- write function for syslog
**
**  Send output to syslog.
**
**	Parameters:
**		fp -- the file pointer
**		buf -- buffer that the write data comes from
**		n -- number of bytes to write
**
**	Returns:
**		0 (zero) for success always
*/

/*
**  XXX TODO: more work needs to be done to ensure that each line of output
**  XXX written to a syslog file is mapped to exactly one syslog message.
*/
ssize_t
sm_syslogwrite(fp, buf, n)
	SM_FILE_T *fp;
	char const *buf;
	size_t n;
{
	syslog(fp->f_ival, "%s", buf);
	return 0;
}

/*
**  SM_SYSLOGSEEK -- position the syslog file offset
**
**  This is a "stub" function (placeholder) that always returns an error.
**  It is an error to seek syslog.
**
**	Parameters:
**		fp -- the file pointer
**		offset -- the new offset position relative to 'whence'
**		whence -- flag indicating start of 'offset'
**
**	Returns:
**		-1 (error) always.
*/

off_t
sm_syslogseek(fp, offset, whence)
	SM_FILE_T *fp;
	off_t offset;
	int whence;
{
	errno = ENODEV;
	return -1;
}

/*
**  SM_SYSLOGCLOSE -- close the syslog file pointer
**
**	Parameters:
**		fp -- the file pointer
**
**	Returns:
**		0 (zero) success always (see Overall)
**
*/

int
sm_syslogclose(fp)
	SM_FILE_T *fp;
{
	return 0;
}

/*
**  SM_SYSLOGSETINFO -- set information for the file pointer
**
**	Parameters:
**		fp -- the file pointer being set
**		what -- what information is being set
**		valp -- information content being set to
**
**	Returns:
**		-1 on failure
**		0 (zero) on success
**
**	Side Effects:
**		Sets internal file pointer data
*/

int
sm_syslogsetinfo(fp, what, valp)
	SM_FILE_T *fp;
	int what;
	void *valp;
{
	switch (what)
	{
	  case SM_IO_SL_PRIO:
		fp->f_ival = *((int *)(valp));
		return 0;
	  default:
		errno = EINVAL;
		return -1;
	}
}

/*
**  SM_SYSLOGGETINFO -- get information relating to the file pointer
**
**	Parameters:
**		fp -- the file pointer being queried
**		what -- the information type being queried
**		valp -- location to placed queried information
**
**	Returns:
**		0 (zero) on success
**		-1 on failure
**
**	Side Effects:
**		Fills in 'valp' with data.
*/

int
sm_sysloggetinfo(fp, what, valp)
	SM_FILE_T *fp;
	int what;
	void *valp;
{
	switch (what)
	{
	  case SM_IO_SL_PRIO:
		*((int *)(valp)) = fp->f_ival;
		return 0;
	  default:
		errno = EINVAL;
		return -1;
	}
}
