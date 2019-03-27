/*
 * Copyright (c) 2000-2005 Proofpoint, Inc. and its suppliers.
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
SM_RCSID("@(#)$Id: stdio.c,v 1.72 2013-11-22 20:51:43 ca Exp $")
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>	/* FreeBSD: FD_ZERO needs <string.h> */
#include <sys/stat.h>
#include <sm/time.h>
#include <sm/heap.h>
#include <sm/assert.h>
#include <sm/varargs.h>
#include <sm/io.h>
#include <sm/setjmp.h>
#include <sm/conf.h>
#include <sm/fdset.h>
#include "local.h"

static int	sm_stdsetmode __P((SM_FILE_T *, const int *));
static int	sm_stdgetmode __P((SM_FILE_T *, int *));

/*
**  Overall:
**  Small standard I/O/seek/close functions.
**  These maintain the `known seek offset' for seek optimization.
*/

/*
**  SM_STDOPEN -- open a file with stdio behavior
**
**  Not associated with the system's stdio in libc.
**
**	Parameters:
**		fp -- file pointer to be associated with the open
**		info -- pathname of the file to be opened
**		flags -- indicates type of access methods
**		rpool -- ignored
**
**	Returns:
**		Failure: -1 and set errno
**		Success: 0 or greater (fd of file from open(2)).
**
*/

/* ARGSUSED3 */
int
sm_stdopen(fp, info, flags, rpool)
	SM_FILE_T *fp;
	const void *info;
	int flags;
	const void *rpool;
{
	char *path = (char *) info;
	int oflags;

	switch (SM_IO_MODE(flags))
	{
	  case SM_IO_RDWR:
		oflags = O_RDWR;
		break;
	  case SM_IO_RDWRTR:
		oflags = O_RDWR | O_CREAT | O_TRUNC;
		break;
	  case SM_IO_RDONLY:
		oflags = O_RDONLY;
		break;
	  case SM_IO_WRONLY:
		oflags = O_WRONLY | O_CREAT | O_TRUNC;
		break;
	  case SM_IO_APPEND:
		oflags = O_APPEND | O_WRONLY | O_CREAT;
		break;
	  case SM_IO_APPENDRW:
		oflags = O_APPEND | O_RDWR | O_CREAT;
		break;
	  default:
		errno = EINVAL;
		return -1;
	}
#ifdef O_BINARY
	if (SM_IS_BINARY(flags))
		oflags |= O_BINARY;
#endif /* O_BINARY */
	fp->f_file = open(path, oflags,
			  S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
	if (fp->f_file < 0)
		return -1; /* errno set by open() */

	if (oflags & O_APPEND)
		(void) (*fp->f_seek)((void *)fp, (off_t)0, SEEK_END);

	return fp->f_file;
}

/*
**  SM_STDREAD -- read from the file
**
**	Parameters:
**		fp -- file pointer to read from
**		buf -- location to place read data
**		n -- number of bytes to read
**
**	Returns:
**		Failure: -1 and sets errno
**		Success: number of bytes read
**
**	Side Effects:
**		Updates internal offset into file.
*/

ssize_t
sm_stdread(fp, buf, n)
	SM_FILE_T *fp;
	char *buf;
	size_t n;
{
	register int ret;

	ret = read(fp->f_file, buf, n);

	/* if the read succeeded, update the current offset */
	if (ret > 0)
		fp->f_lseekoff += ret;
	return ret;
}

/*
**  SM_STDWRITE -- write to the file
**
**	Parameters:
**		fp -- file pointer ro write to
**		buf -- location of data to be written
**		n - number of bytes to write
**
**	Returns:
**		Failure: -1 and sets errno
**		Success: number of bytes written
*/

ssize_t
sm_stdwrite(fp, buf, n)
	SM_FILE_T *fp;
	char const *buf;
	size_t n;
{
	return write(fp->f_file, buf, n);
}

/*
**  SM_STDSEEK -- set the file offset position
**
**	Parmeters:
**		fp -- file pointer to position
**		offset -- how far to position from "base" (set by 'whence')
**		whence -- indicates where the "base" of the 'offset' to start
**
**	Results:
**		Failure: -1 and sets errno
**		Success: the current offset
**
**	Side Effects:
**		Updates the internal value of the offset.
*/

off_t
sm_stdseek(fp, offset, whence)
	SM_FILE_T *fp;
	off_t offset;
	int whence;
{
	register off_t ret;

	ret = lseek(fp->f_file, (off_t) offset, whence);
	if (ret != (off_t) -1)
		fp->f_lseekoff = ret;
	return ret;
}

/*
**  SM_STDCLOSE -- close the file
**
**	Parameters:
**		fp -- the file pointer to close
**
**	Returns:
**		Success: 0 (zero)
**		Failure: -1 and sets errno
*/

int
sm_stdclose(fp)
	SM_FILE_T *fp;
{
	return close(fp->f_file);
}

/*
**  SM_STDSETMODE -- set the access mode for the file
**
**  Called by sm_stdsetinfo().
**
**	Parameters:
**		fp -- file pointer
**		mode -- new mode to set the file access to
**
**	Results:
**		Success: 0 (zero);
**		Failure: -1 and sets errno
*/

static int
sm_stdsetmode(fp, mode)
	SM_FILE_T *fp;
	const int *mode;
{
	int flags = 0;

	switch (SM_IO_MODE(*mode))
	{
	  case SM_IO_RDWR:
		flags |= SMRW;
		break;
	  case SM_IO_RDONLY:
		flags |= SMRD;
		break;
	  case SM_IO_WRONLY:
		flags |= SMWR;
		break;
	  case SM_IO_APPEND:
	  default:
		errno = EINVAL;
		return -1;
	}
	fp->f_flags = fp->f_flags & ~SMMODEMASK;
	fp->f_flags |= flags;
	return 0;
}

/*
**  SM_STDGETMODE -- for getinfo determine open mode
**
**  Called by sm_stdgetinfo().
**
**	Parameters:
**		fp -- the file mode being determined
**		mode -- internal mode to map to external value
**
**	Results:
**		Failure: -1 and sets errno
**		Success: external mode value
*/

static int
sm_stdgetmode(fp, mode)
	SM_FILE_T *fp;
	int *mode;
{
	switch (fp->f_flags & SMMODEMASK)
	{
	  case SMRW:
		*mode = SM_IO_RDWR;
		break;
	  case SMRD:
		*mode = SM_IO_RDONLY;
		break;
	  case SMWR:
		*mode = SM_IO_WRONLY;
		break;
	  default:
		errno = EINVAL;
		return -1;
	}
	return 0;
}

/*
**  SM_STDSETINFO -- set/modify information for a file
**
**	Parameters:
**		fp -- file to set info for
**		what -- type of info to set
**		valp -- location of data used for setting
**
**	Returns:
**		Failure: -1 and sets errno
**		Success: >=0
*/

int
sm_stdsetinfo(fp, what, valp)
	SM_FILE_T *fp;
	int what;
	void *valp;
{
	switch (what)
	{
	  case SM_IO_WHAT_MODE:
		return sm_stdsetmode(fp, (const int *)valp);

	  default:
		errno = EINVAL;
		return -1;
	}
}

/*
**  SM_STDGETINFO -- get information about the open file
**
**	Parameters:
**		fp -- file to get info for
**		what -- type of info to get
**		valp -- location to place found info
**
**	Returns:
**		Success: may or may not place info in 'valp' depending
**			on 'what' value, and returns values >=0. Return
**			value may be the obtained info
**		Failure: -1 and sets errno
*/

int
sm_stdgetinfo(fp, what, valp)
	SM_FILE_T *fp;
	int what;
	void *valp;
{
	switch (what)
	{
	  case SM_IO_WHAT_MODE:
		return sm_stdgetmode(fp, (int *)valp);

	  case SM_IO_WHAT_FD:
		return fp->f_file;

	  case SM_IO_WHAT_SIZE:
	  {
		  struct stat st;

		  if (fstat(fp->f_file, &st) == 0)
			  return st.st_size;
		  else
			  return -1;
	  }

	  case SM_IO_IS_READABLE:
	  {
		  fd_set readfds;
		  struct timeval timeout;

		  if (SM_FD_SETSIZE > 0 && fp->f_file >= SM_FD_SETSIZE)
		  {
			  errno = EINVAL;
			  return -1;
		  }
		  FD_ZERO(&readfds);
		  SM_FD_SET(fp->f_file, &readfds);
		  timeout.tv_sec = 0;
		  timeout.tv_usec = 0;
		  if (select(fp->f_file + 1, FDSET_CAST &readfds,
			     NULL, NULL, &timeout) > 0 &&
		      SM_FD_ISSET(fp->f_file, &readfds))
			  return 1;
		  return 0;
	  }

	  default:
		errno = EINVAL;
		return -1;
	}
}

/*
**  SM_STDFDOPEN -- open file by primitive 'fd' rather than pathname
**
**	I/O function to handle fdopen() stdio equivalence. The rest of
**	the functions are the same as the sm_stdopen() above.
**
**	Parameters:
**		fp -- the file pointer to be associated with the open
**		name -- the primitive file descriptor for association
**		flags -- indicates type of access methods
**		rpool -- ignored
**
**	Results:
**		Success: primitive file descriptor value
**		Failure: -1 and sets errno
*/

/* ARGSUSED3 */
int
sm_stdfdopen(fp, info, flags, rpool)
	SM_FILE_T *fp;
	const void *info;
	int flags;
	const void *rpool;
{
	int oflags, tmp, fdflags, fd = *((int *) info);

	switch (SM_IO_MODE(flags))
	{
	  case SM_IO_RDWR:
		oflags = O_RDWR | O_CREAT;
		break;
	  case SM_IO_RDONLY:
		oflags = O_RDONLY;
		break;
	  case SM_IO_WRONLY:
		oflags = O_WRONLY | O_CREAT | O_TRUNC;
		break;
	  case SM_IO_APPEND:
		oflags = O_APPEND | O_WRONLY | O_CREAT;
		break;
	  case SM_IO_APPENDRW:
		oflags = O_APPEND | O_RDWR | O_CREAT;
		break;
	  default:
		errno = EINVAL;
		return -1;
	}
#ifdef O_BINARY
	if (SM_IS_BINARY(flags))
		oflags |= O_BINARY;
#endif /* O_BINARY */

	/* Make sure the mode the user wants is a subset of the actual mode. */
	if ((fdflags = fcntl(fd, F_GETFL, 0)) < 0)
		return -1;
	tmp = fdflags & O_ACCMODE;
	if (tmp != O_RDWR && (tmp != (oflags & O_ACCMODE)))
	{
		errno = EINVAL;
		return -1;
	}
	fp->f_file = fd;
	if (oflags & O_APPEND)
		(void) (*fp->f_seek)(fp, (off_t)0, SEEK_END);
	return fp->f_file;
}

/*
**  SM_IO_FOPEN -- open a file
**
**	Same interface and semantics as the open() system call,
**	except that it returns SM_FILE_T* instead of a file descriptor.
**
**	Parameters:
**		pathname -- path of file to open
**		flags -- flags controlling the open
**		...  -- option "mode" for opening the file
**
**	Returns:
**		Raises an exception on heap exhaustion.
**		Returns NULL and sets errno if open() fails.
**		Returns an SM_FILE_T pointer on success.
*/

SM_FILE_T *
#if SM_VA_STD
sm_io_fopen(char *pathname, int flags, ...)
#else /* SM_VA_STD */
sm_io_fopen(pathname, flags, va_alist)
	char *pathname;
	int flags;
	va_dcl
#endif /* SM_VA_STD */
{
	MODE_T mode;
	SM_FILE_T *fp;
	int ioflags;

	if (flags & O_CREAT)
	{
		SM_VA_LOCAL_DECL

		SM_VA_START(ap, flags);
		mode = (MODE_T) SM_VA_ARG(ap, int);
		SM_VA_END(ap);
	}
	else
		mode = 0;

	switch (flags & O_ACCMODE)
	{
	  case O_RDONLY:
		ioflags = SMRD;
		break;
	  case O_WRONLY:
		ioflags = SMWR;
		break;
	  case O_RDWR:
		ioflags = SMRW;
		break;
	  default:
		sm_abort("sm_io_fopen: bad flags 0%o", flags);
	}

	fp = sm_fp(SmFtStdio, ioflags, NULL);
	fp->f_file = open(pathname, flags, mode);
	if (fp->f_file == -1)
	{
		fp->f_flags = 0;
		fp->sm_magic = NULL;
		return NULL;
	}
	return fp;
}
