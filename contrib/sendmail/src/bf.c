/*
 * Copyright (c) 1999-2002, 2004, 2006 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 * Contributed by Exactis.com, Inc.
 *
 */

/*
**  This is in transition. Changed from the original bf_torek.c code
**  to use sm_io function calls directly rather than through stdio
**  translation layer. Will be made a built-in file type of libsm
**  next (once safeopen() linkable from libsm).
*/

#include <sm/gen.h>
SM_RCSID("@(#)$Id: bf.c,v 8.63 2013-11-22 20:51:55 ca Exp $")

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "sendmail.h"
#include "bf.h"

#include <syslog.h>

/* bf io functions */
static ssize_t	sm_bfread __P((SM_FILE_T *, char *, size_t));
static ssize_t	sm_bfwrite __P((SM_FILE_T *, const char *, size_t));
static off_t	sm_bfseek __P((SM_FILE_T *, off_t, int));
static int	sm_bfclose __P((SM_FILE_T *));
static int	sm_bfcommit __P((SM_FILE_T *));
static int	sm_bftruncate __P((SM_FILE_T *));

static int	sm_bfopen __P((SM_FILE_T *, const void *, int, const void *));
static int	sm_bfsetinfo __P((SM_FILE_T *, int , void *));
static int	sm_bfgetinfo __P((SM_FILE_T *, int , void *));

/*
**  Data structure for storing information about each buffered file
**  (Originally in sendmail/bf_torek.h for the curious.)
*/

struct bf
{
	bool	bf_committed;	/* Has this buffered file been committed? */
	bool	bf_ondisk;	/* On disk: committed or buffer overflow */
	long	bf_flags;
	int	bf_disk_fd;	/* If on disk, associated file descriptor */
	char	*bf_buf;	/* Memory buffer */
	int	bf_bufsize;	/* Length of above buffer */
	int	bf_buffilled;	/* Bytes of buffer actually filled */
	char	*bf_filename;	/* Name of buffered file, if ever committed */
	MODE_T	bf_filemode;	/* Mode of buffered file, if ever committed */
	off_t	bf_offset;	/* Currect file offset */
	int	bf_size;	/* Total current size of file */
};

#ifdef BF_STANDALONE
# define OPEN(fn, omode, cmode, sff) open(fn, omode, cmode)
#else /* BF_STANDALONE */
# define OPEN(fn, omode, cmode, sff) safeopen(fn, omode, cmode, sff)
#endif /* BF_STANDALONE */

struct bf_info
{
	char	*bi_filename;
	MODE_T	bi_fmode;
	size_t	bi_bsize;
	long	bi_flags;
};

/*
**  SM_BFOPEN -- the "base" open function called by sm_io_open() for the
**		internal, file-type-specific info setup.
**
**	Parameters:
**		fp -- file pointer being filled-in for file being open'd
**		info -- information about file being opened
**		flags -- ignored
**		rpool -- ignored (currently)
**
**	Returns:
**		Failure: -1 and sets errno
**		Success: 0 (zero)
*/

static int
sm_bfopen(fp, info, flags, rpool)
	SM_FILE_T *fp;
	const void *info;
	int flags;
	const void *rpool;
{
	char *filename;
	MODE_T fmode;
	size_t bsize;
	long sflags;
	struct bf *bfp;
	int l;
	struct stat st;

	filename = ((struct bf_info *) info)->bi_filename;
	fmode = ((struct bf_info *) info)->bi_fmode;
	bsize = ((struct bf_info *) info)->bi_bsize;
	sflags = ((struct bf_info *) info)->bi_flags;

	/* Sanity checks */
	if (*filename == '\0')
	{
		/* Empty filename string */
		errno = ENOENT;
		return -1;
	}
	if (stat(filename, &st) == 0)
	{
		/* File already exists on disk */
		errno = EEXIST;
		return -1;
	}

	/* Allocate memory */
	bfp = (struct bf *) sm_malloc(sizeof(struct bf));
	if (bfp == NULL)
	{
		errno = ENOMEM;
		return -1;
	}

	/* Assign data buffer */
	/* A zero bsize is valid, just don't allocate memory */
	if (bsize > 0)
	{
		bfp->bf_buf = (char *) sm_malloc(bsize);
		if (bfp->bf_buf == NULL)
		{
			bfp->bf_bufsize = 0;
			sm_free(bfp);
			errno = ENOMEM;
			return -1;
		}
	}
	else
		bfp->bf_buf = NULL;

	/* Nearly home free, just set all the parameters now */
	bfp->bf_committed = false;
	bfp->bf_ondisk = false;
	bfp->bf_flags = sflags;
	bfp->bf_bufsize = bsize;
	bfp->bf_buffilled = 0;
	l = strlen(filename) + 1;
	bfp->bf_filename = (char *) sm_malloc(l);
	if (bfp->bf_filename == NULL)
	{
		if (bfp->bf_buf != NULL)
			sm_free(bfp->bf_buf);
		sm_free(bfp);
		errno = ENOMEM;
		return -1;
	}
	(void) sm_strlcpy(bfp->bf_filename, filename, l);
	bfp->bf_filemode = fmode;
	bfp->bf_offset = 0;
	bfp->bf_size = 0;
	bfp->bf_disk_fd = -1;
	fp->f_cookie = bfp;

	if (tTd(58, 8))
		sm_dprintf("sm_bfopen(%s)\n", filename);

	return 0;
}

/*
**  BFOPEN -- create a new buffered file
**
**	Parameters:
**		filename -- the file's name
**		fmode -- what mode the file should be created as
**		bsize -- amount of buffer space to allocate (may be 0)
**		flags -- if running under sendmail, passed directly to safeopen
**
**	Returns:
**		a SM_FILE_T * which may then be used with stdio functions,
**		or NULL	on failure. SM_FILE_T * is opened for writing
**		"SM_IO_WHAT_VECTORS").
**
**	Side Effects:
**		none.
**
**	Sets errno:
**		any value of errno specified by sm_io_setinfo_type()
**		any value of errno specified by sm_io_open()
**		any value of errno specified by sm_io_setinfo()
*/

#ifdef __STDC__
/*
**  XXX This is a temporary hack since MODE_T on HP-UX 10.x is short.
**	If we use K&R here, the compiler will complain about
**	Inconsistent parameter list declaration
**	due to the change from short to int.
*/

SM_FILE_T *
bfopen(char *filename, MODE_T fmode, size_t bsize, long flags)
#else /* __STDC__ */
SM_FILE_T *
bfopen(filename, fmode, bsize, flags)
	char *filename;
	MODE_T fmode;
	size_t bsize;
	long flags;
#endif /* __STDC__ */
{
	MODE_T omask;
	SM_FILE_T SM_IO_SET_TYPE(vector, BF_FILE_TYPE, sm_bfopen, sm_bfclose,
		sm_bfread, sm_bfwrite, sm_bfseek, sm_bfgetinfo, sm_bfsetinfo,
		SM_TIME_FOREVER);
	struct bf_info info;

	/*
	**  Apply current umask to fmode as it may change by the time
	**  the file is actually created.  fmode becomes the true
	**  permissions of the file, which OPEN() must obey.
	*/

	omask = umask(0);
	fmode &= ~omask;
	(void) umask(omask);

	SM_IO_INIT_TYPE(vector, BF_FILE_TYPE, sm_bfopen, sm_bfclose,
		sm_bfread, sm_bfwrite, sm_bfseek, sm_bfgetinfo, sm_bfsetinfo,
		SM_TIME_FOREVER);
	info.bi_filename = filename;
	info.bi_fmode = fmode;
	info.bi_bsize = bsize;
	info.bi_flags = flags;

	return sm_io_open(&vector, SM_TIME_DEFAULT, &info, SM_IO_RDWR, NULL);
}

/*
**  SM_BFGETINFO -- returns info about an open file pointer
**
**	Parameters:
**		fp -- file pointer to get info about
**		what -- type of info to obtain
**		valp -- thing to return the info in
*/

static int
sm_bfgetinfo(fp, what, valp)
	SM_FILE_T *fp;
	int what;
	void *valp;
{
	struct bf *bfp;

	bfp = (struct bf *) fp->f_cookie;
	switch (what)
	{
	  case SM_IO_WHAT_FD:
		return bfp->bf_disk_fd;
	  case SM_IO_WHAT_SIZE:
		return bfp->bf_size;
	  default:
		return -1;
	}
}

/*
**  SM_BFCLOSE -- close a buffered file
**
**	Parameters:
**		fp -- cookie of file to close
**
**	Returns:
**		0 to indicate success
**
**	Side Effects:
**		deletes backing file, sm_frees memory.
**
**	Sets errno:
**		never.
*/

static int
sm_bfclose(fp)
	SM_FILE_T *fp;
{
	struct bf *bfp;

	/* Cast cookie back to correct type */
	bfp = (struct bf *) fp->f_cookie;

	/* Need to clean up the file */
	if (bfp->bf_ondisk && !bfp->bf_committed)
		unlink(bfp->bf_filename);
	sm_free(bfp->bf_filename);

	if (bfp->bf_disk_fd != -1)
		close(bfp->bf_disk_fd);

	/* Need to sm_free the buffer */
	if (bfp->bf_bufsize > 0)
		sm_free(bfp->bf_buf);

	/* Finally, sm_free the structure */
	sm_free(bfp);
	return 0;
}

/*
**  SM_BFREAD -- read a buffered file
**
**	Parameters:
**		cookie -- cookie of file to read
**		buf -- buffer to fill
**		nbytes -- how many bytes to read
**
**	Returns:
**		number of bytes read or -1 indicate failure
**
**	Side Effects:
**		none.
**
*/

static ssize_t
sm_bfread(fp, buf, nbytes)
	SM_FILE_T *fp;
	char *buf;
	size_t nbytes;
{
	struct bf *bfp;
	ssize_t count = 0;	/* Number of bytes put in buf so far */
	int retval;

	/* Cast cookie back to correct type */
	bfp = (struct bf *) fp->f_cookie;

	if (bfp->bf_offset < bfp->bf_buffilled)
	{
		/* Need to grab some from buffer */
		count = nbytes;
		if ((bfp->bf_offset + count) > bfp->bf_buffilled)
			count = bfp->bf_buffilled - bfp->bf_offset;

		memcpy(buf, bfp->bf_buf + bfp->bf_offset, count);
	}

	if ((bfp->bf_offset + nbytes) > bfp->bf_buffilled)
	{
		/* Need to grab some from file */
		if (!bfp->bf_ondisk)
		{
			/* Oops, the file doesn't exist. EOF. */
			if (tTd(58, 8))
				sm_dprintf("sm_bfread(%s): to disk\n",
					   bfp->bf_filename);
			goto finished;
		}

		/* Catch a read() on an earlier failed write to disk */
		if (bfp->bf_disk_fd < 0)
		{
			errno = EIO;
			return -1;
		}

		if (lseek(bfp->bf_disk_fd,
			  bfp->bf_offset + count, SEEK_SET) < 0)
		{
			if ((errno == EINVAL) || (errno == ESPIPE))
			{
				/*
				**  stdio won't be expecting these
				**  errnos from read()! Change them
				**  into something it can understand.
				*/

				errno = EIO;
			}
			return -1;
		}

		while (count < nbytes)
		{
			retval = read(bfp->bf_disk_fd,
				      buf + count,
				      nbytes - count);
			if (retval < 0)
			{
				/* errno is set implicitly by read() */
				return -1;
			}
			else if (retval == 0)
				goto finished;
			else
				count += retval;
		}
	}

finished:
	bfp->bf_offset += count;
	return count;
}

/*
**  SM_BFSEEK -- seek to a position in a buffered file
**
**	Parameters:
**		fp     -- fp of file to seek
**		offset -- position to seek to
**		whence -- how to seek
**
**	Returns:
**		new file offset or -1 indicate failure
**
**	Side Effects:
**		none.
**
*/

static off_t
sm_bfseek(fp, offset, whence)
	SM_FILE_T *fp;
	off_t offset;
	int whence;

{
	struct bf *bfp;

	/* Cast cookie back to correct type */
	bfp = (struct bf *) fp->f_cookie;

	switch (whence)
	{
	  case SEEK_SET:
		bfp->bf_offset = offset;
		break;

	  case SEEK_CUR:
		bfp->bf_offset += offset;
		break;

	  case SEEK_END:
		bfp->bf_offset = bfp->bf_size + offset;
		break;

	  default:
		errno = EINVAL;
		return -1;
	}
	return bfp->bf_offset;
}

/*
**  SM_BFWRITE -- write to a buffered file
**
**	Parameters:
**		fp -- fp of file to write
**		buf -- data buffer
**		nbytes -- how many bytes to write
**
**	Returns:
**		number of bytes written or -1 indicate failure
**
**	Side Effects:
**		may create backing file if over memory limit for file.
**
*/

static ssize_t
sm_bfwrite(fp, buf, nbytes)
	SM_FILE_T *fp;
	const char *buf;
	size_t nbytes;
{
	struct bf *bfp;
	ssize_t count = 0;	/* Number of bytes written so far */
	int retval;

	/* Cast cookie back to correct type */
	bfp = (struct bf *) fp->f_cookie;

	/* If committed, go straight to disk */
	if (bfp->bf_committed)
	{
		if (lseek(bfp->bf_disk_fd, bfp->bf_offset, SEEK_SET) < 0)
		{
			if ((errno == EINVAL) || (errno == ESPIPE))
			{
				/*
				**  stdio won't be expecting these
				**  errnos from write()! Change them
				**  into something it can understand.
				*/

				errno = EIO;
			}
			return -1;
		}

		count = write(bfp->bf_disk_fd, buf, nbytes);
		if (count < 0)
		{
			/* errno is set implicitly by write() */
			return -1;
		}
		goto finished;
	}

	if (bfp->bf_offset < bfp->bf_bufsize)
	{
		/* Need to put some in buffer */
		count = nbytes;
		if ((bfp->bf_offset + count) > bfp->bf_bufsize)
			count = bfp->bf_bufsize - bfp->bf_offset;

		memcpy(bfp->bf_buf + bfp->bf_offset, buf, count);
		if ((bfp->bf_offset + count) > bfp->bf_buffilled)
			bfp->bf_buffilled = bfp->bf_offset + count;
	}

	if ((bfp->bf_offset + nbytes) > bfp->bf_bufsize)
	{
		/* Need to put some in file */
		if (!bfp->bf_ondisk)
		{
			MODE_T omask;
			int save_errno;

			/* Clear umask as bf_filemode are the true perms */
			omask = umask(0);
			retval = OPEN(bfp->bf_filename,
				      O_RDWR | O_CREAT | O_TRUNC | QF_O_EXTRA,
				      bfp->bf_filemode, bfp->bf_flags);
			save_errno = errno;
			(void) umask(omask);
			errno = save_errno;

			/* Couldn't create file: failure */
			if (retval < 0)
			{
				/*
				**  stdio may not be expecting these
				**  errnos from write()! Change to
				**  something which it can understand.
				**  Note that ENOSPC and EDQUOT are saved
				**  because they are actually valid for
				**  write().
				*/

				if (!(errno == ENOSPC
#ifdef EDQUOT
				      || errno == EDQUOT
#endif /* EDQUOT */
				     ))
					errno = EIO;

				return -1;
			}
			bfp->bf_disk_fd = retval;
			bfp->bf_ondisk = true;
		}

		/* Catch a write() on an earlier failed write to disk */
		if (bfp->bf_ondisk && bfp->bf_disk_fd < 0)
		{
			errno = EIO;
			return -1;
		}

		if (lseek(bfp->bf_disk_fd,
			  bfp->bf_offset + count, SEEK_SET) < 0)
		{
			if ((errno == EINVAL) || (errno == ESPIPE))
			{
				/*
				**  stdio won't be expecting these
				**  errnos from write()! Change them into
				**  something which it can understand.
				*/

				errno = EIO;
			}
			return -1;
		}

		while (count < nbytes)
		{
			retval = write(bfp->bf_disk_fd, buf + count,
				       nbytes - count);
			if (retval < 0)
			{
				/* errno is set implicitly by write() */
				return -1;
			}
			else
				count += retval;
		}
	}

finished:
	bfp->bf_offset += count;
	if (bfp->bf_offset > bfp->bf_size)
		bfp->bf_size = bfp->bf_offset;
	return count;
}

/*
**  BFREWIND -- rewinds the SM_FILE_T *
**
**	Parameters:
**		fp -- SM_FILE_T * to rewind
**
**	Returns:
**		0 on success, -1 on error
**
**	Side Effects:
**		rewinds the SM_FILE_T * and puts it into read mode. Normally
**		one would bfopen() a file, write to it, then bfrewind() and
**		fread(). If fp is not a buffered file, this is equivalent to
**		rewind().
**
**	Sets errno:
**		any value of errno specified by sm_io_rewind()
*/

int
bfrewind(fp)
	SM_FILE_T *fp;
{
	(void) sm_io_flush(fp, SM_TIME_DEFAULT);
	sm_io_clearerr(fp); /* quicker just to do it */
	return sm_io_seek(fp, SM_TIME_DEFAULT, 0, SM_IO_SEEK_SET);
}

/*
**  SM_BFCOMMIT -- "commits" the buffered file
**
**	Parameters:
**		fp -- SM_FILE_T * to commit to disk
**
**	Returns:
**		0 on success, -1 on error
**
**	Side Effects:
**		Forces the given SM_FILE_T * to be written to disk if it is not
**		already, and ensures that it will be kept after closing. If
**		fp is not a buffered file, this is a no-op.
**
**	Sets errno:
**		any value of errno specified by open()
**		any value of errno specified by write()
**		any value of errno specified by lseek()
*/

static int
sm_bfcommit(fp)
	SM_FILE_T *fp;
{
	struct bf *bfp;
	int retval;
	int byteswritten;

	/* Get associated bf structure */
	bfp = (struct bf *) fp->f_cookie;

	/* If already committed, noop */
	if (bfp->bf_committed)
		return 0;

	/* Do we need to open a file? */
	if (!bfp->bf_ondisk)
	{
		int save_errno;
		MODE_T omask;
		struct stat st;

		if (tTd(58, 8))
		{
			sm_dprintf("bfcommit(%s): to disk\n", bfp->bf_filename);
			if (tTd(58, 32))
				sm_dprintf("bfcommit(): filemode %o flags %ld\n",
					   (unsigned int) bfp->bf_filemode,
					   bfp->bf_flags);
		}

		if (stat(bfp->bf_filename, &st) == 0)
		{
			errno = EEXIST;
			return -1;
		}

		/* Clear umask as bf_filemode are the true perms */
		omask = umask(0);
		retval = OPEN(bfp->bf_filename,
			      O_RDWR | O_CREAT | O_EXCL | QF_O_EXTRA,
			      bfp->bf_filemode, bfp->bf_flags);
		save_errno = errno;
		(void) umask(omask);

		/* Couldn't create file: failure */
		if (retval < 0)
		{
			/* errno is set implicitly by open() */
			errno = save_errno;
			return -1;
		}

		bfp->bf_disk_fd = retval;
		bfp->bf_ondisk = true;
	}

	/* Write out the contents of our buffer, if we have any */
	if (bfp->bf_buffilled > 0)
	{
		byteswritten = 0;

		if (lseek(bfp->bf_disk_fd, 0, SEEK_SET) < 0)
		{
			/* errno is set implicitly by lseek() */
			return -1;
		}

		while (byteswritten < bfp->bf_buffilled)
		{
			retval = write(bfp->bf_disk_fd,
				       bfp->bf_buf + byteswritten,
				       bfp->bf_buffilled - byteswritten);
			if (retval < 0)
			{
				/* errno is set implicitly by write() */
				return -1;
			}
			else
				byteswritten += retval;
		}
	}
	bfp->bf_committed = true;

	/* Invalidate buf; all goes to file now */
	bfp->bf_buffilled = 0;
	if (bfp->bf_bufsize > 0)
	{
		/* Don't need buffer anymore; free it */
		bfp->bf_bufsize = 0;
		sm_free(bfp->bf_buf);
	}
	return 0;
}

/*
**  SM_BFTRUNCATE -- rewinds and truncates the SM_FILE_T *
**
**	Parameters:
**		fp -- SM_FILE_T * to truncate
**
**	Returns:
**		0 on success, -1 on error
**
**	Side Effects:
**		rewinds the SM_FILE_T *, truncates it to zero length, and puts
**		it into write mode.
**
**	Sets errno:
**		any value of errno specified by fseek()
**		any value of errno specified by ftruncate()
*/

static int
sm_bftruncate(fp)
	SM_FILE_T *fp;
{
	struct bf *bfp;

	if (bfrewind(fp) < 0)
		return -1;

	/* Get bf structure */
	bfp = (struct bf *) fp->f_cookie;
	bfp->bf_buffilled = 0;
	bfp->bf_size = 0;

	/* Need to zero the buffer */
	if (bfp->bf_bufsize > 0)
		memset(bfp->bf_buf, '\0', bfp->bf_bufsize);
	if (bfp->bf_ondisk)
	{
#if NOFTRUNCATE
		/* XXX: Not much we can do except rewind it */
		errno = EINVAL;
		return -1;
#else /* NOFTRUNCATE */
		return ftruncate(bfp->bf_disk_fd, 0);
#endif /* NOFTRUNCATE */
	}
	return 0;
}

/*
**  SM_BFSETINFO -- set/change info for an open file pointer
**
**	Parameters:
**		fp -- file pointer to get info about
**		what -- type of info to set/change
**		valp -- thing to set/change the info to
**
*/

static int
sm_bfsetinfo(fp, what, valp)
	SM_FILE_T *fp;
	int what;
	void *valp;
{
	struct bf *bfp;
	int bsize;

	/* Get bf structure */
	bfp = (struct bf *) fp->f_cookie;
	switch (what)
	{
	  case SM_BF_SETBUFSIZE:
		bsize = *((int *) valp);
		bfp->bf_bufsize = bsize;

		/* A zero bsize is valid, just don't allocate memory */
		if (bsize > 0)
		{
			bfp->bf_buf = (char *) sm_malloc(bsize);
			if (bfp->bf_buf == NULL)
			{
				bfp->bf_bufsize = 0;
				errno = ENOMEM;
				return -1;
			}
		}
		else
			bfp->bf_buf = NULL;
		return 0;
	  case SM_BF_COMMIT:
		return sm_bfcommit(fp);
	  case SM_BF_TRUNCATE:
		return sm_bftruncate(fp);
	  case SM_BF_TEST:
		return 1; /* always */
	  default:
		errno = EINVAL;
		return -1;
	}
}
