/*
 * Copyright (c) 2000-2002, 2004 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_IDSTR(id, "@(#)$Id: smstdio.c,v 1.35 2013-11-22 20:51:43 ca Exp $")
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sm/assert.h>
#include <sm/io.h>
#include <sm/string.h>
#include "local.h"

static void	setup __P((SM_FILE_T *));

/*
** Overall:
**	This is a file type which implements a layer on top of the system
**	stdio. fp->f_cookie is the FILE* of stdio. The cookie may be
**	"bound late" because of the manner which Linux implements stdio.
**	When binding late  (when fp->f_cookie==NULL) then the value of
**	fp->f_ival is used (0, 1 or 2) to map to stdio's stdin, stdout or
**	stderr.
*/

/*
**  SM_STDIOOPEN -- open a file to system stdio implementation
**
**	Parameters:
**		fp -- file pointer assign for this open
**		info -- info about file to open
**		flags -- indicating method of opening
**		rpool -- ignored
**
**	Returns:
**		Failure: -1
**		Success: 0 (zero)
*/

/* ARGSUSED3 */
int
sm_stdioopen(fp, info, flags, rpool)
	SM_FILE_T *fp;
	const void *info;
	int flags;
	const void *rpool;
{
	register FILE *s;
	char *stdiomode;

	switch (flags)
	{
	  case SM_IO_RDONLY:
		stdiomode = "r";
		break;
	  case SM_IO_WRONLY:
		stdiomode = "w";
		break;
	  case SM_IO_APPEND:
		stdiomode = "a";
		break;
	  case SM_IO_APPENDRW:
		stdiomode = "a+";
		break;
#if SM_IO_BINARY != 0
	  case SM_IO_RDONLY_B:
		stdiomode = "rb";
		break;
	  case SM_IO_WRONLY_B:
		stdiomode = "wb";
		break;
	  case SM_IO_APPEND_B:
		stdiomode = "ab";
		break;
	  case SM_IO_APPENDRW_B:
		stdiomode = "a+b";
		break;
	  case SM_IO_RDWR_B:
		stdiomode = "r+b";
		break;
#endif /* SM_IO_BINARY != 0 */
	  case SM_IO_RDWR:
	  default:
		stdiomode = "r+";
		break;
	}

	if ((s = fopen((char *)info, stdiomode)) == NULL)
		return -1;
	fp->f_cookie = s;
	return 0;
}

/*
**  SETUP -- assign file type cookie when not already assigned
**
**	Parameters:
**		fp - the file pointer to get the cookie assigned
**
**	Return:
**		none.
*/

static void
setup(fp)
	SM_FILE_T *fp;
{
	if (fp->f_cookie == NULL)
	{
		switch (fp->f_ival)
		{
		  case 0:
			fp->f_cookie = stdin;
			break;
		  case 1:
			fp->f_cookie = stdout;
			break;
		  case 2:
			fp->f_cookie = stderr;
			break;
		  default:
			sm_abort("fp->f_ival=%d: out of range (0...2)", fp->f_ival);
			break;
		}
	}
}

/*
**  SM_STDIOREAD -- read from the file
**
**	Parameters:
**		fp -- the file pointer
**		buf -- location to place the read data
**		n - number of bytes to read
**
**	Returns:
**		result from fread().
*/

ssize_t
sm_stdioread(fp, buf, n)
	SM_FILE_T *fp;
	char *buf;
	size_t n;
{
	register FILE *s;

	if (fp->f_cookie == NULL)
		setup(fp);
	s = fp->f_cookie;
	return fread(buf, 1, n, s);
}

/*
**  SM_STDIOWRITE -- write to the file
**
**	Parameters:
**		fp -- the file pointer
**		buf -- location of data to write
**		n - number of bytes to write
**
**	Returns:
**		result from fwrite().
*/

ssize_t
sm_stdiowrite(fp, buf, n)
	SM_FILE_T *fp;
	char const *buf;
	size_t n;
{
	register FILE *s;

	if (fp->f_cookie == NULL)
		setup(fp);
	s = fp->f_cookie;
	return fwrite(buf, 1, n, s);
}

/*
**  SM_STDIOSEEK -- set position within file
**
**	Parameters:
**		fp -- the file pointer
**		offset -- new location based on 'whence'
**		whence -- indicates "base" for 'offset'
**
**	Returns:
**		result from fseek().
*/

off_t
sm_stdioseek(fp, offset, whence)
	SM_FILE_T *fp;
	off_t offset;
	int whence;
{
	register FILE *s;

	if (fp->f_cookie == NULL)
		setup(fp);
	s = fp->f_cookie;
	return fseek(s, offset, whence);
}

/*
**  SM_STDIOCLOSE -- close the file
**
**	Parameters:
**		fp -- close file pointer
**
**	Return:
**		status from fclose()
*/

int
sm_stdioclose(fp)
	SM_FILE_T *fp;
{
	register FILE *s;

	if (fp->f_cookie == NULL)
		setup(fp);
	s = fp->f_cookie;
	return fclose(s);
}

/*
**  SM_STDIOSETINFO -- set info for this open file
**
**	Parameters:
**		fp -- the file pointer
**		what -- type of information setting
**		valp -- memory location of info to set
**
**	Return:
**		Failure: -1 and sets errno
**		Success: none (currently).
*/

/* ARGSUSED2 */
int
sm_stdiosetinfo(fp, what, valp)
	SM_FILE_T *fp;
	int what;
	void *valp;
{
	switch (what)
	{
	  case SM_IO_WHAT_MODE:
	  default:
		errno = EINVAL;
		return -1;
	}
}

/*
**  SM_STDIOGETINFO -- get info for this open file
**
**	Parameters:
**		fp -- the file pointer
**		what -- type of information request
**		valp -- memory location to place info
**
**	Return:
**		Failure: -1 and sets errno
**		Success: none (currently).
*/

/* ARGSUSED2 */
int
sm_stdiogetinfo(fp, what, valp)
	SM_FILE_T *fp;
	int what;
	void *valp;
{
	switch (what)
	{
	  case SM_IO_WHAT_SIZE:
	  {
		  int fd;
		  struct stat st;

		  if (fp->f_cookie == NULL)
			  setup(fp);
		  fd = fileno((FILE *) fp->f_cookie);
		  if (fd < 0)
			  return -1;
		  if (fstat(fd, &st) == 0)
			  return st.st_size;
		  else
			  return -1;
	  }

	  case SM_IO_WHAT_MODE:
	  default:
		errno = EINVAL;
		return -1;
	}
}

/*
**  SM_IO_STDIOOPEN -- create an SM_FILE which interfaces to a stdio FILE
**
**	Parameters:
**		stream -- an open stdio stream, as returned by fopen()
**		mode -- the mode argument to fopen() which describes stream
**
**	Return:
**		On success, return a pointer to an SM_FILE object which
**		can be used for reading and writing 'stream'.
**		Abort if mode is gibberish or stream is bad.
**		Raise an exception if we can't allocate memory.
*/

SM_FILE_T *
sm_io_stdioopen(stream, mode)
	FILE *stream;
	char *mode;
{
	int fd;
	bool r, w;
	int ioflags;
	SM_FILE_T *fp;

	fd = fileno(stream);
	SM_REQUIRE(fd >= 0);

	r = w = false;
	switch (mode[0])
	{
	  case 'r':
		r = true;
		break;
	  case 'w':
	  case 'a':
		w = true;
		break;
	  default:
		sm_abort("sm_io_stdioopen: mode '%s' is bad", mode);
	}
	if (strchr(&mode[1], '+') != NULL)
		r = w = true;
	if (r && w)
		ioflags = SMRW;
	else if (r)
		ioflags = SMRD;
	else
		ioflags = SMWR;

	fp = sm_fp(SmFtRealStdio, ioflags, NULL);
	fp->f_file = fd;
	fp->f_cookie = stream;
	return fp;
}
