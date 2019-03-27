/*
 * Copyright (c) 2000-2002, 2004, 2005 Proofpoint, Inc. and its suppliers.
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
SM_IDSTR(id, "@(#)$Id: strio.c,v 1.45 2013-11-22 20:51:43 ca Exp $")
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sm/rpool.h>
#include <sm/io.h>
#include <sm/heap.h>
#include <sm/conf.h>
#include "local.h"

static int	sm_strsetmode __P((SM_FILE_T*, const int *));
static int	sm_strgetmode __P((SM_FILE_T*, int *));

/*
**  Cookie structure for the "strio" file type
*/

struct sm_str_obj
{
	char		*strio_base;
	char		*strio_end;
	size_t		strio_size;
	size_t		strio_offset;
	int		strio_flags;
	const void	*strio_rpool;
};

typedef struct sm_str_obj SM_STR_OBJ_T;

/*
**  SM_STRGROW -- increase storage space for string
**
**	Parameters:
**		s -- current cookie
**		size -- new storage size request
**
**	Returns:
**		true iff successful.
*/

static bool sm_strgrow __P((SM_STR_OBJ_T *, size_t));

static bool
sm_strgrow(s, size)
	SM_STR_OBJ_T *s;
	size_t size;
{
	register void *p;

	if (s->strio_size >= size)
		return true;
	p = sm_realloc(s->strio_base, size);
	if (p == NULL)
		return false;
	s->strio_base = p;
	s->strio_end = s->strio_base + size;
	s->strio_size = size;
	return true;
}

/*
**  SM_STRREAD -- read a portion of the string
**
**	Parameters:
**		fp -- the file pointer
**		buf -- location to place read data
**		n -- number of bytes to read
**
**	Returns:
**		Failure: -1 and sets errno
**		Success: >=0, number of bytes read
*/

ssize_t
sm_strread(fp, buf, n)
	SM_FILE_T *fp;
	char *buf;
	size_t n;
{
	register SM_STR_OBJ_T *s = fp->f_cookie;
	int len;

	if (!(s->strio_flags & SMRD) && !(s->strio_flags & SMRW))
	{
		errno = EBADF;
		return -1;
	}
	len = SM_MIN(s->strio_size - s->strio_offset, n);
	(void) memmove(buf, s->strio_base + s->strio_offset, len);
	s->strio_offset += len;
	return len;
}

/*
**  SM_STRWRITE -- write a portion of the string
**
**	Parameters:
**		fp -- the file pointer
**		buf -- location of data for writing
**		n -- number of bytes to write
**
**	Returns:
**		Failure: -1 and sets errno
**		Success: >=0, number of bytes written
*/

ssize_t
sm_strwrite(fp, buf, n)
	SM_FILE_T *fp;
	char const *buf;
	size_t n;
{
	register SM_STR_OBJ_T *s = fp->f_cookie;

	if (!(s->strio_flags & SMWR) && !(s->strio_flags & SMRW))
	{
		errno = EBADF;
		return -1;
	}
	if (n + s->strio_offset > s->strio_size)
	{
		if (!sm_strgrow(s, n + s->strio_offset))
			return 0;
	}
	(void) memmove(s->strio_base + s->strio_offset, buf, n);
	s->strio_offset += n;
	return n;
}

/*
**  SM_STRSEEK -- position the offset pointer for the string
**
**	Only SM_IO_SEEK_SET, SM_IO_SEEK_CUR and SM_IO_SEEK_END are valid
**	values for whence.
**
**	Parameters:
**		fp -- the file pointer
**		offset -- number of bytes offset from "base"
**		whence -- determines "base" for 'offset'
**
**	Returns:
**		Failure: -1 and sets errno
**		Success: >=0, number of bytes read
*/

off_t
sm_strseek(fp, offset, whence)
	SM_FILE_T *fp;
	off_t offset;
	int whence;
{
	register off_t ret;
	register SM_STR_OBJ_T *s = fp->f_cookie;

reseek:
	switch (whence)
	{
	  case SM_IO_SEEK_SET:
		ret = offset;
		break;
	  case SM_IO_SEEK_CUR:
		ret = s->strio_offset + offset;
		break;
	  case SM_IO_SEEK_END:
		ret = s->strio_size;
		break;
	  default:
		errno = EINVAL;
		return -1;
	}
	if (ret < 0 || ret > (off_t)(size_t)(-1))	/* XXX ugly */
		return -1;
	if ((size_t) ret > s->strio_size)
	{
		if (sm_strgrow(s, (size_t)ret))
			goto reseek;

		/* errno set by sm_strgrow */
		return -1;
	}
	s->strio_offset = (size_t) ret;
	return ret;
}

/*
**  SM_STROPEN -- open a string file type
**
**	Parameters:
**		fp -- file pointer open to be associated with
**		info -- initial contents (NULL for none)
**		flags -- flags for methods of access (was mode)
**		rpool -- resource pool to use memory from (if applicable)
**
**	Results:
**		Success: 0 (zero)
**		Failure: -1 and sets errno
*/

int
sm_stropen(fp, info, flags, rpool)
	SM_FILE_T *fp;
	const void *info;
	int flags;
	const void *rpool;
{
	register SM_STR_OBJ_T *s;

#if SM_RPOOL
	s = sm_rpool_malloc_x(rpool, sizeof(SM_STR_OBJ_T));
#else /* SM_RPOOL */
	s = sm_malloc(sizeof(SM_STR_OBJ_T));
	if (s == NULL)
		return -1;
#endif /* SM_RPOOL */

	fp->f_cookie = s;
	s->strio_rpool = rpool;
	s->strio_offset = 0;
	s->strio_size = 0;
	s->strio_base = NULL;
	s->strio_end = 0;

	switch (flags)
	{
	  case SM_IO_RDWR:
		s->strio_flags = SMRW;
		break;
	  case SM_IO_RDONLY:
		s->strio_flags = SMRD;
		break;
	  case SM_IO_WRONLY:
		s->strio_flags = SMWR;
		break;
	  case SM_IO_APPEND:
		if (s->strio_rpool == NULL)
			sm_free(s);
		errno = EINVAL;
		return -1;
	  default:
		if (s->strio_rpool == NULL)
			sm_free(s);
		errno = EINVAL;
		return -1;
	}

	if (info != NULL)
	{
		s->strio_base = sm_strdup_x(info);
		if (s->strio_base == NULL)
		{
			int save_errno = errno;

			if (s->strio_rpool == NULL)
				sm_free(s);
			errno = save_errno;
			return -1;
		}
		s->strio_size = strlen(info);
		s->strio_end = s->strio_base + s->strio_size;
	}
	return 0;
}

/*
**  SM_STRCLOSE -- close the string file type and free resources
**
**	Parameters:
**		fp -- file pointer
**
**	Results:
**		Success: 0 (zero)
*/

int
sm_strclose(fp)
	SM_FILE_T *fp;
{
	SM_STR_OBJ_T *s = fp->f_cookie;

#if !SM_RPOOL
	sm_free(s->strio_base);
	s->strio_base = NULL;
#endif /* !SM_RPOOL */
	return 0;
}

/*
**  SM_STRSETMODE -- set mode info for the file
**
**	 Note: changing the mode can be a safe way to have the "parent"
**	 set up a string that the "child" is not to modify
**
**	Parameters:
**		fp -- the file pointer
**		mode -- location of new mode to set
**
**	Results:
**		Success: 0 (zero)
**		Failure: -1 and sets errno
*/

static int
sm_strsetmode(fp, mode)
	SM_FILE_T *fp;
	const int *mode;
{
	register SM_STR_OBJ_T *s = fp->f_cookie;
	int flags;

	switch (*mode)
	{
	  case SM_IO_RDWR:
		flags = SMRW;
		break;
	  case SM_IO_RDONLY:
		flags = SMRD;
		break;
	  case SM_IO_WRONLY:
		flags = SMWR;
		break;
	  case SM_IO_APPEND:
		errno = EINVAL;
		return -1;
	  default:
		errno = EINVAL;
		return -1;
	}
	s->strio_flags &= ~SMMODEMASK;
	s->strio_flags |= flags;
	return 0;
}

/*
**  SM_STRGETMODE -- get mode info for the file
**
**	Parameters:
**		fp -- the file pointer
**		mode -- location to store current mode
**
**	Results:
**		Success: 0 (zero)
**		Failure: -1 and sets errno
*/

static int
sm_strgetmode(fp, mode)
	SM_FILE_T *fp;
	int *mode;
{
	register SM_STR_OBJ_T *s = fp->f_cookie;

	switch (s->strio_flags & SMMODEMASK)
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
**  SM_STRSETINFO -- set info for the file
**
**	Currently only SM_IO_WHAT_MODE is supported for 'what'.
**
**	Parameters:
**		fp -- the file pointer
**		what -- type of information to set
**		valp -- location to data for doing set
**
**	Results:
**		Failure: -1 and sets errno
**		Success: sm_strsetmode() return [0 (zero)]
*/

int
sm_strsetinfo(fp, what, valp)
	SM_FILE_T *fp;
	int what;
	void *valp;
{
	switch(what)
	{
	  case SM_IO_WHAT_MODE:
		return sm_strsetmode(fp, (int *) valp);
	  default:
		errno = EINVAL;
		return -1;
	}
}

/*
**  SM_STRGETINFO -- get info for the file
**
**	Currently only SM_IO_WHAT_MODE is supported for 'what'.
**
**	Parameters:
**		fp -- the file pointer
**		what -- type of information requested
**		valp -- location to return information in
**
**	Results:
**		Failure: -1 and sets errno
**		Success: sm_strgetmode() return [0 (zero)]
*/

int
sm_strgetinfo(fp, what, valp)
	SM_FILE_T *fp;
	int what;
	void *valp;
{
	switch(what)
	{
	  case SM_IO_WHAT_MODE:
		return sm_strgetmode(fp, (int *) valp);
	  default:
		errno = EINVAL;
		return -1;
	}
}

/*
**  SM_STRIO_INIT -- initializes a write-only string type
**
**  Original comments below. This function does not appear to be used anywhere.
**  The same functionality can be done by changing the mode of the file.
**  ------------
** sm_strio_init initializes an SM_FILE_T structure as a write-only file
** that writes into the specified buffer:
** - Use sm_io_putc, sm_io_fprintf, etc, to write into the buffer.
**   Attempts to write more than size-1 characters into the buffer will fail
**   silently (no error is reported).
** - Use sm_io_fflush to nul terminate the string in the buffer
**   (the write pointer is not advanced).
** No memory is allocated either by sm_strio_init or by sm_io_{putc,write} etc.
**
**	Parameters:
**		fp -- file pointer
**		buf -- memory location for stored data
**		size -- size of 'buf'
**
**	Results:
**		none.
*/

void
sm_strio_init(fp, buf, size)
	SM_FILE_T *fp;
	char *buf;
	size_t size;
{
	fp->sm_magic = SmFileMagic;
	fp->f_flags = SMWR | SMSTR;
	fp->f_file = -1;
	fp->f_bf.smb_base = fp->f_p = (unsigned char *) buf;
	fp->f_bf.smb_size = fp->f_w = (size ? size - 1 : 0);
	fp->f_lbfsize = 0;
	fp->f_r = 0;
	fp->f_read = NULL;
	fp->f_seek = NULL;
	fp->f_getinfo = NULL;
	fp->f_setinfo = NULL;
}
