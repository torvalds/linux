/*
 * Copyright (c) 2000-2002, 2004-2006 Proofpoint, Inc. and its suppliers.
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
 *
 *	$Id: local.h,v 1.59 2013-11-22 20:51:43 ca Exp $
 */

/*
**  Information local to this implementation of stdio,
**  in particular, macros and private variables.
*/

#include <sm/time.h>
#include <sm/fdset.h>
#if !SM_CONF_MEMCHR
# include <memory.h>
#endif /* !SM_CONF_MEMCHR */
#include <sm/heap.h>

int	sm_flush __P((SM_FILE_T *, int *));
SM_FILE_T	*smfp __P((void));
int	sm_refill __P((SM_FILE_T *, int));
void	sm_init __P((void));
void	sm_cleanup __P((void));
void	sm_makebuf __P((SM_FILE_T *));
int	sm_whatbuf __P((SM_FILE_T *, size_t *, int *));
int	sm_fwalk __P((int (*)(SM_FILE_T *, int *), int *));
int	sm_wsetup __P((SM_FILE_T *));
int	sm_flags __P((int));
SM_FILE_T	*sm_fp __P((const SM_FILE_T *, const int, SM_FILE_T *));
int	sm_vprintf __P((int, char const *, va_list));

/* std io functions */
ssize_t	sm_stdread __P((SM_FILE_T *, char *, size_t));
ssize_t	sm_stdwrite __P((SM_FILE_T *, char const *, size_t));
off_t	sm_stdseek __P((SM_FILE_T *, off_t, int));
int	sm_stdclose __P((SM_FILE_T *));
int	sm_stdopen __P((SM_FILE_T *, const void *, int, const void *));
int	sm_stdfdopen __P((SM_FILE_T *, const void *, int, const void *));
int	sm_stdsetinfo __P((SM_FILE_T *, int , void *));
int	sm_stdgetinfo __P((SM_FILE_T *, int , void *));

/* stdio io functions */
ssize_t	sm_stdioread __P((SM_FILE_T *, char *, size_t));
ssize_t	sm_stdiowrite __P((SM_FILE_T *, char const *, size_t));
off_t	sm_stdioseek __P((SM_FILE_T *, off_t, int));
int	sm_stdioclose __P((SM_FILE_T *));
int	sm_stdioopen __P((SM_FILE_T *, const void *, int, const void *));
int	sm_stdiosetinfo __P((SM_FILE_T *, int , void *));
int	sm_stdiogetinfo __P((SM_FILE_T *, int , void *));

/* string io functions */
ssize_t	sm_strread __P((SM_FILE_T *, char *, size_t));
ssize_t	sm_strwrite __P((SM_FILE_T *, char const *, size_t));
off_t	sm_strseek __P((SM_FILE_T *, off_t, int));
int	sm_strclose __P((SM_FILE_T *));
int	sm_stropen __P((SM_FILE_T *, const void *, int, const void *));
int	sm_strsetinfo __P((SM_FILE_T *, int , void *));
int	sm_strgetinfo __P((SM_FILE_T *, int , void *));

/* syslog io functions */
ssize_t	sm_syslogread __P((SM_FILE_T *, char *, size_t));
ssize_t	sm_syslogwrite __P((SM_FILE_T *, char const *, size_t));
off_t	sm_syslogseek __P((SM_FILE_T *, off_t, int));
int	sm_syslogclose __P((SM_FILE_T *));
int	sm_syslogopen __P((SM_FILE_T *, const void *, int, const void *));
int	sm_syslogsetinfo __P((SM_FILE_T *, int , void *));
int	sm_sysloggetinfo __P((SM_FILE_T *, int , void *));

extern bool Sm_IO_DidInit;

/* Return true iff the given SM_FILE_T cannot be written now. */
#define cantwrite(fp) \
	((((fp)->f_flags & SMWR) == 0 || (fp)->f_bf.smb_base == NULL) && \
	 sm_wsetup(fp))

/*
**  Test whether the given stdio file has an active ungetc buffer;
**   release such a buffer, without restoring ordinary unread data.
*/

#define HASUB(fp) ((fp)->f_ub.smb_base != NULL)
#define FREEUB(fp)					\
{							\
	if ((fp)->f_ub.smb_base != (fp)->f_ubuf)	\
		sm_free((char *)(fp)->f_ub.smb_base);	\
	(fp)->f_ub.smb_base = NULL;			\
}

extern const char SmFileMagic[];

#define SM_ALIGN(p)	(((unsigned long)(p) + SM_ALIGN_BITS) & ~SM_ALIGN_BITS)

#define sm_io_flockfile(fp)	((void) 0)
#define sm_io_funlockfile(fp)	((void) 0)

int sm_flags __P((int));

#ifndef FDSET_CAST
# define FDSET_CAST		/* empty cast for fd_set arg to select */
#endif

/*
**  SM_CONVERT_TIME -- convert the API timeout flag for select() usage.
**
**	This takes a 'fp' (a file type pointer) and obtains the "raw"
**	file descriptor (fd) if possible. The 'fd' is needed to possibly
**	switch the mode of the file (blocking/non-blocking) to match
**	the type of timeout. If timeout is SM_TIME_FOREVER then the
**	timeout using select won't be needed and the file is best placed
**	in blocking mode. If there is to be a finite timeout then the file
**	is best placed in non-blocking mode. Then, if not enough can be
**	written, select() can be used to test when something can be written
**	yet still timeout if the wait is too long.
**	If the mode is already in the correct state we don't change it.
**	Iff (yes "iff") the 'fd' is "-1" in value then the mode change
**	will not happen. This situation arises when a late-binding-to-disk
**	file type is in use. An example of this is the sendmail buffered
**	file type (in sendmail/bf.c).
**
**	Parameters
**		fp -- the file pointer the timeout is for
**		fd -- to become the file descriptor value from 'fp'
**		val -- the timeout value to be converted
**		time -- a struct timeval holding the converted value
**
**	Returns
**		nothing, this is flow-through code
**
**	Side Effects:
**		May or may not change the mode of a currently open file.
**		The file mode may be changed to O_NONBLOCK or ~O_NONBLOCK
**		(meaning block). This is done to best match the type of
**		timeout and for (possible) use with select().
*/

# define SM_CONVERT_TIME(fp, fd, val, time) { \
	if (((fd) = sm_io_getinfo(fp, SM_IO_WHAT_FD, NULL)) == -1) \
	{ \
		/* can't get an fd, likely internal 'fake' fp */ \
		errno = 0; \
	} \
	if ((val) == SM_TIME_DEFAULT) \
		(val) = (fp)->f_timeout; \
	if ((val) == SM_TIME_IMMEDIATE || (val) == SM_TIME_FOREVER) \
	{ \
		(time)->tv_sec = 0; \
		(time)->tv_usec = 0; \
	} \
	else \
	{ \
		(time)->tv_sec = (val) / 1000; \
		(time)->tv_usec = ((val) - ((time)->tv_sec * 1000)) * 1000; \
	} \
	if ((val) == SM_TIME_FOREVER) \
	{ \
		if ((fp)->f_timeoutstate == SM_TIME_NONBLOCK && (fd) != -1) \
		{ \
			int ret; \
			ret = fcntl((fd), F_GETFL, 0); \
			if (ret == -1 || fcntl((fd), F_SETFL, \
					       ret & ~O_NONBLOCK) == -1) \
			{ \
				/* errno should be set */ \
				return SM_IO_EOF; \
			} \
			(fp)->f_timeoutstate = SM_TIME_BLOCK; \
			if ((fp)->f_modefp != NULL) \
				(fp)->f_modefp->f_timeoutstate = SM_TIME_BLOCK; \
		} \
	} \
	else { \
		if ((fp)->f_timeoutstate == SM_TIME_BLOCK && (fd) != -1) \
		{ \
			int ret; \
			ret = fcntl((fd), F_GETFL, 0); \
			if (ret == -1 || fcntl((fd), F_SETFL, \
					       ret | O_NONBLOCK) == -1) \
			{ \
				/* errno should be set */ \
				return SM_IO_EOF; \
			} \
			(fp)->f_timeoutstate = SM_TIME_NONBLOCK; \
			if ((fp)->f_modefp != NULL) \
				(fp)->f_modefp->f_timeoutstate = SM_TIME_NONBLOCK; \
		} \
	} \
}

/*
**  SM_IO_WR_TIMEOUT -- setup the timeout for the write
**
**  This #define uses a select() to wait for the 'fd' to become writable.
**  The select() can be active for up to 'to' time. The select may not
**  use all of the the 'to' time. Hence, the amount of "wall-clock" time is
**  measured to decide how much to subtract from 'to' to update it. On some
**  BSD-based/like systems the timeout for a select is updated for the
**  amount of time used. On many/most systems this does not happen. Therefore
**  the updating of 'to' must be done ourselves; a copy of 'to' is passed
**  since a BSD-like system will have updated it and we don't want to
**  double the time used!
**  Note: if a valid 'fd' doesn't exist yet, don't use this (e.g. the
**  sendmail buffered file type in sendmail/bf.c; see fvwrite.c).
**
**	Parameters
**		fd -- a file descriptor for doing select() with
**		timeout -- the original user set value.
**
**	Returns
**		nothing, this is flow through code
**
**	Side Effects:
**		adjusts 'timeout' for time used
*/

#define SM_IO_WR_TIMEOUT(fp, fd, to) { \
	struct timeval sm_io_to_before, sm_io_to_after, sm_io_to_diff; \
	struct timeval sm_io_to; \
	int sm_io_to_sel; \
	fd_set sm_io_to_mask, sm_io_x_mask; \
	errno = 0; \
	if ((to) == SM_TIME_DEFAULT) \
		(to) = (fp)->f_timeout; \
	if ((to) == SM_TIME_IMMEDIATE) \
	{ \
		errno = EAGAIN; \
		return SM_IO_EOF; \
	} \
	else if ((to) == SM_TIME_FOREVER) \
	{ \
		errno = EINVAL; \
		return SM_IO_EOF; \
	} \
	else \
	{ \
		sm_io_to.tv_sec = (to) / 1000; \
		sm_io_to.tv_usec = ((to) - (sm_io_to.tv_sec * 1000)) * 1000; \
	} \
	if (!SM_FD_OK_SELECT(fd)) \
	{ \
		errno = EINVAL; \
		return SM_IO_EOF; \
	} \
	FD_ZERO(&sm_io_to_mask); \
	FD_SET((fd), &sm_io_to_mask); \
	FD_ZERO(&sm_io_x_mask); \
	FD_SET((fd), &sm_io_x_mask); \
	if (gettimeofday(&sm_io_to_before, NULL) < 0) \
		return SM_IO_EOF; \
	do \
	{	\
		sm_io_to_sel = select((fd) + 1, NULL, &sm_io_to_mask, \
					&sm_io_x_mask, &sm_io_to); \
	} while (sm_io_to_sel < 0 && errno == EINTR); \
	if (sm_io_to_sel < 0) \
	{ \
		/* something went wrong, errno set */ \
		return SM_IO_EOF; \
	} \
	else if (sm_io_to_sel == 0) \
	{ \
		/* timeout */ \
		errno = EAGAIN; \
		return SM_IO_EOF; \
	} \
	/* else loop again */ \
	if (gettimeofday(&sm_io_to_after, NULL) < 0) \
		return SM_IO_EOF; \
	timersub(&sm_io_to_after, &sm_io_to_before, &sm_io_to_diff); \
	(to) -= (sm_io_to_diff.tv_sec * 1000); \
	(to) -= (sm_io_to_diff.tv_usec / 1000); \
	if ((to) < 0) \
		(to) = 0; \
}

/*
**  If there is no 'fd' just error (we can't timeout). If the timeout
**  is SM_TIME_FOREVER then there is no need to do a timeout with
**  select since this will be a real error.  If the error is not
**  EAGAIN/EWOULDBLOCK (from a nonblocking) then it's a real error.
**  Specify the condition here as macro so it can be used in several places.
*/

#define IS_IO_ERROR(fd, ret, to) \
	((fd) < 0 ||	\
	 ((ret) < 0 && errno != EAGAIN && errno != EWOULDBLOCK) ||	\
	 (to) == SM_TIME_FOREVER)

