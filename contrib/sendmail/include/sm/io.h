/*
 * Copyright (c) 2000-2002, 2004, 2013 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 * Copyright (c) 1990
 * 	 The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: io.h,v 1.26 2013-11-22 20:51:31 ca Exp $
 */

/*-
 *	@(#)stdio.h	5.17 (Berkeley) 6/3/91
 */

#ifndef	SM_IO_H
#define SM_IO_H

#include <stdio.h>
#include <sm/gen.h>
#include <sm/varargs.h>

/* mode for sm io (exposed) */
#define SM_IO_RDWR	1	/* read-write */
#define SM_IO_RDONLY	2	/* read-only */
#define SM_IO_WRONLY	3	/* write-only */
#define SM_IO_APPEND	4	/* write-only from eof */
#define SM_IO_APPENDRW	5	/* read-write from eof */
#define SM_IO_RDWRTR	6	/* read-write with truncation indicated */

# define SM_IO_BINARY	0x0	/* binary mode: not used in Unix */
#define SM_IS_BINARY(mode)	(((mode) & SM_IO_BINARY) != 0)
#define SM_IO_MODE(mode)	((mode) & 0x0f)

#define SM_IO_RDWR_B	(SM_IO_RDWR|SM_IO_BINARY)
#define SM_IO_RDONLY_B	(SM_IO_RDONLY|SM_IO_BINARY)
#define SM_IO_WRONLY_B	(SM_IO_WRONLY|SM_IO_BINARY)
#define SM_IO_APPEND_B	(SM_IO_APPEND|SM_IO_BINARY)
#define SM_IO_APPENDRW_B	(SM_IO_APPENDRW|SM_IO_BINARY)
#define SM_IO_RDWRTR_B	(SM_IO_RDWRTR|SM_IO_BINARY)

/* for sm_io_fseek, et al api's (exposed) */
#define SM_IO_SEEK_SET	0
#define SM_IO_SEEK_CUR	1
#define SM_IO_SEEK_END	2

/* flags for info what's with different types (exposed) */
#define SM_IO_WHAT_MODE		1
#define SM_IO_WHAT_VECTORS	2
#define SM_IO_WHAT_FD		3
#define SM_IO_WHAT_TYPE		4
#define SM_IO_WHAT_ISTYPE	5
#define SM_IO_IS_READABLE	6
#define SM_IO_WHAT_TIMEOUT	7
#define SM_IO_WHAT_SIZE		8

/* info flags (exposed) */
#define SM_IO_FTYPE_CREATE	1
#define SM_IO_FTYPE_MODIFY	2
#define SM_IO_FTYPE_DELETE	3

#define SM_IO_SL_PRIO		1

#define SM_IO_OPEN_MAX		20

/* for internal buffers */
struct smbuf
{
	unsigned char	*smb_base;
	int		smb_size;
};

/*
**  sm I/O state variables (internal only).
**
**	The following always hold:
**
**		if (flags&(SMLBF|SMWR)) == (SMLBF|SMWR),
**			lbfsize is -bf.size, else lbfsize is 0
**		if flags&SMRD, w is 0
**		if flags&SMWR, r is 0
**
**	This ensures that the getc and putc macros (or inline functions) never
**	try to write or read from a file that is in `read' or `write' mode.
**	(Moreover, they can, and do, automatically switch from read mode to
**	write mode, and back, on "r+" and "w+" files.)
**
**	lbfsize is used only to make the inline line-buffered output stream
**	code as compact as possible.
**
**	ub, up, and ur are used when ungetc() pushes back more characters
**	than fit in the current bf, or when ungetc() pushes back a character
**	that does not match the previous one in bf.  When this happens,
**	ub.base becomes non-nil (i.e., a stream has ungetc() data iff
**	ub.base!=NULL) and up and ur save the current values of p and r.
*/

typedef struct sm_file SM_FILE_T;

struct sm_file
{
	const char	*sm_magic;	/* This SM_FILE_T is free when NULL */
	unsigned char	*f_p;		/* current position in (some) buffer */
	int		f_r;		/* read space left for getc() */
	int		f_w;		/* write space left for putc() */
	long		f_flags;	/* flags, below */
	short		f_file;		/* fileno, if Unix fd, else -1 */
	struct smbuf	f_bf;		/* the buffer (>= 1 byte, if !NULL) */
	int		f_lbfsize;	/* 0 or -bf.size, for inline putc */

	/* These can be used for any purpose by a file type implementation: */
	void		*f_cookie;
	int		f_ival;

	/* operations */
	int		(*f_close) __P((SM_FILE_T *));
	ssize_t		(*f_read)  __P((SM_FILE_T *, char *, size_t));
	off_t		(*f_seek)  __P((SM_FILE_T *, off_t, int));
	ssize_t		(*f_write) __P((SM_FILE_T *, const char *, size_t));
	int		(*f_open) __P((SM_FILE_T *, const void *, int,
					const void *));
	int		(*f_setinfo) __P((SM_FILE_T *, int , void *));
	int		(*f_getinfo) __P((SM_FILE_T *, int , void *));
	int		f_timeout;
	int		f_timeoutstate;   /* either blocking or non-blocking */
	char		*f_type;	/* for by-type lookups */
	struct sm_file	*f_flushfp;	/* flush this before reading parent */
	struct sm_file	*f_modefp;	/* sync mode with this fp */

	/* separate buffer for long sequences of ungetc() */
	struct smbuf	f_ub;	/* ungetc buffer */
	unsigned char	*f_up;	/* saved f_p when f_p is doing ungetc */
	int		f_ur;	/* saved f_r when f_r is counting ungetc */

	/* tricks to meet minimum requirements even when malloc() fails */
	unsigned char	f_ubuf[3];	/* guarantee an ungetc() buffer */
	unsigned char	f_nbuf[1];	/* guarantee a getc() buffer */

	/* Unix stdio files get aligned to block boundaries on fseek() */
	int		f_blksize;	/* stat.st_blksize (may be != bf.size) */
	off_t		f_lseekoff;	/* current lseek offset */
	int		f_dup_cnt;	/* count file dup'd */
};

__BEGIN_DECLS
extern SM_FILE_T	SmIoF[];
extern const char	SmFileMagic[];
extern SM_FILE_T	SmFtStdio_def;
extern SM_FILE_T	SmFtStdiofd_def;
extern SM_FILE_T	SmFtString_def;
extern SM_FILE_T	SmFtSyslog_def;
extern SM_FILE_T	SmFtRealStdio_def;

#define SMIOIN_FILENO		0
#define SMIOOUT_FILENO		1
#define SMIOERR_FILENO		2
#define SMIOSTDIN_FILENO	3
#define SMIOSTDOUT_FILENO	4
#define SMIOSTDERR_FILENO	5

/* Common predefined and already (usually) open files (exposed) */
#define smioin		(&SmIoF[SMIOIN_FILENO])
#define smioout		(&SmIoF[SMIOOUT_FILENO])
#define smioerr		(&SmIoF[SMIOERR_FILENO])
#define smiostdin	(&SmIoF[SMIOSTDIN_FILENO])
#define smiostdout	(&SmIoF[SMIOSTDOUT_FILENO])
#define smiostderr	(&SmIoF[SMIOSTDERR_FILENO])

#define SmFtStdio	(&SmFtStdio_def)
#define SmFtStdiofd	(&SmFtStdiofd_def)
#define SmFtString	(&SmFtString_def)
#define SmFtSyslog	(&SmFtSyslog_def)
#define SmFtRealStdio	(&SmFtRealStdio_def)

#ifdef __STDC__
# define SM_IO_SET_TYPE(f, name, open, close, read, write, seek, get, set, timeout) \
    (f) = {SmFileMagic, (unsigned char *) 0, 0, 0, 0L, -1, {0}, 0, (void *) 0,\
	0, (close), (read), (seek), (write), (open), (set), (get), (timeout),\
	0, (name)}
# define SM_IO_INIT_TYPE(f, name, open, close, read, write, seek, get, set, timeout)

#else /* __STDC__ */
# define SM_IO_SET_TYPE(f, name, open, close, read, write, seek, get, set, timeout) (f)
# define SM_IO_INIT_TYPE(f, name, open, close, read, write, seek, get, set, timeout) \
	(f).sm_magic = SmFileMagic;	\
	(f).f_p = (unsigned char *) 0;	\
	(f).f_r = 0;	\
	(f).f_w = 0;	\
	(f).f_flags = 0L;	\
	(f).f_file = 0;	\
	(f).f_bf.smb_base = (unsigned char *) 0;	\
	(f).f_bf.smb_size = 0;	\
	(f).f_lbfsize = 0;	\
	(f).f_cookie = (void *) 0;	\
	(f).f_ival = 0;	\
	(f).f_close = (close);	\
	(f).f_read = (read);	\
	(f).f_seek = (seek);	\
	(f).f_write = (write);	\
	(f).f_open = (open);	\
	(f).f_setinfo = (set);	\
	(f).f_getinfo = (get);	\
	(f).f_timeout = (timeout);	\
	(f).f_timeoutstate = 0;	\
	(f).f_type = (name);

#endif /* __STDC__ */

__END_DECLS

/* Internal flags */
#define SMFBF		0x000001	/* XXXX fully buffered */
#define SMLBF		0x000002	/* line buffered */
#define SMNBF		0x000004	/* unbuffered */
#define SMNOW		0x000008	/* Flush each write; take read now */
#define SMRD		0x000010	/* OK to read */
#define SMWR		0x000020	/* OK to write */
	/* RD and WR are never simultaneously asserted */
#define SMRW		0x000040	/* open for reading & writing */
#define SMFEOF		0x000080	/* found EOF */
#define SMERR		0x000100	/* found error */
#define SMMBF		0x000200	/* buf is from malloc */
#define SMAPP		0x000400	/* fdopen()ed in append mode */
#define SMSTR		0x000800	/* this is an snprintf string */
#define SMOPT		0x001000	/* do fseek() optimisation */
#define SMNPT		0x002000	/* do not do fseek() optimisation */
#define SMOFF		0x004000	/* set iff offset is in fact correct */
#define SMALC		0x010000	/* allocate string space dynamically */

#define SMMODEMASK	0x0070	/* read/write mode */

/* defines for timeout constants */
#define SM_TIME_IMMEDIATE	(0)
#define SM_TIME_FOREVER		(-1)
#define SM_TIME_DEFAULT		(-2)

/* timeout state for blocking */
#define SM_TIME_BLOCK		(0)	/* XXX just bool? */
#define SM_TIME_NONBLOCK	(1)

/* Exposed buffering type flags */
#define SM_IO_FBF	0	/* setvbuf should set fully buffered */
#define SM_IO_LBF	1	/* setvbuf should set line buffered */
#define SM_IO_NBF	2	/* setvbuf should set unbuffered */

/* setvbuf buffered, but through at lower file type layers */
#define SM_IO_NOW	3

/*
**  size of buffer used by setbuf.
**  If underlying filesystem blocksize is discoverable that is used instead
*/

#define SM_IO_BUFSIZ	4096

#define SM_IO_EOF	(-1)

/* Functions defined in ANSI C standard.  */
__BEGIN_DECLS
SM_FILE_T *sm_io_autoflush __P((SM_FILE_T *, SM_FILE_T *));
void	 sm_io_automode __P((SM_FILE_T *, SM_FILE_T *));
void	 sm_io_clearerr __P((SM_FILE_T *));
int	 sm_io_close __P((SM_FILE_T *, int SM_NONVOLATILE));
SM_FILE_T *sm_io_dup __P((SM_FILE_T *));
int	 sm_io_eof __P((SM_FILE_T *));
int	 sm_io_error __P((SM_FILE_T *));
int	 sm_io_fgets __P((SM_FILE_T *, int, char *, int));
int	 sm_io_flush __P((SM_FILE_T *, int SM_NONVOLATILE));

int PRINTFLIKE(3, 4)
sm_io_fprintf __P((SM_FILE_T *, int, const char *, ...));

int	 sm_io_fputs __P((SM_FILE_T *, int, const char *));

int SCANFLIKE(3, 4)
sm_io_fscanf __P((SM_FILE_T *, int, const char *, ...));

int	 sm_io_getc __P((SM_FILE_T *, int));
int	 sm_io_getinfo __P((SM_FILE_T *, int, void *));
SM_FILE_T *sm_io_open __P((const SM_FILE_T *, int SM_NONVOLATILE, const void *,
			   int, const void *));
int	 sm_io_purge __P((SM_FILE_T *));
int	 sm_io_putc __P((SM_FILE_T *, int, int));
size_t	 sm_io_read __P((SM_FILE_T *, int, void *, size_t));
SM_FILE_T *sm_io_reopen __P((const SM_FILE_T *, int SM_NONVOLATILE,
			     const void *, int, const void *, SM_FILE_T *));
void	 sm_io_rewind __P((SM_FILE_T *, int));
int	 sm_io_seek __P((SM_FILE_T *, int SM_NONVOLATILE, long SM_NONVOLATILE,
			 int SM_NONVOLATILE));
int	 sm_io_setinfo __P((SM_FILE_T *, int, void *));
int	 sm_io_setvbuf __P((SM_FILE_T *, int, char *, int, size_t));

int SCANFLIKE(2, 3)
sm_io_sscanf __P((const char *, char const *, ...));

long	 sm_io_tell __P((SM_FILE_T *, int SM_NONVOLATILE));
int	 sm_io_ungetc __P((SM_FILE_T *, int, int));
int	 sm_io_vfprintf __P((SM_FILE_T *, int, const char *, va_list));
size_t	 sm_io_write __P((SM_FILE_T *, int, const void *, size_t));

void	 sm_strio_init __P((SM_FILE_T *, char *, size_t));

extern SM_FILE_T *
sm_io_fopen __P((
	char *_pathname,
	int _flags,
	...));

extern SM_FILE_T *
sm_io_stdioopen __P((
	FILE *_stream,
	char *_mode));

extern int
sm_vasprintf __P((
	char **_str,
	const char *_fmt,
	va_list _ap));

extern int
sm_vsnprintf __P((
	char *,
	size_t,
	const char *,
	va_list));

extern void
sm_perror __P((
	const char *));

__END_DECLS

/*
** Functions internal to the implementation.
*/

__BEGIN_DECLS
int	sm_rget __P((SM_FILE_T *, int));
int	sm_vfscanf __P((SM_FILE_T *, int SM_NONVOLATILE, const char *,
			va_list SM_NONVOLATILE));
int	sm_wbuf __P((SM_FILE_T *, int, int));
__END_DECLS

/*
**  The macros are here so that we can
**  define function versions in the library.
*/

#define sm_getc(f, t) \
	(--(f)->f_r < 0 ? \
		sm_rget(f, t) : \
		(int)(*(f)->f_p++))

/*
**  This has been tuned to generate reasonable code on the vax using pcc.
**  (It also generates reasonable x86 code using gcc.)
*/

#define sm_putc(f, t, c) \
	(--(f)->f_w < 0 ? \
		(f)->f_w >= (f)->f_lbfsize ? \
			(*(f)->f_p = (c)), *(f)->f_p != '\n' ? \
				(int)*(f)->f_p++ : \
				sm_wbuf(f, t, '\n') : \
			sm_wbuf(f, t, (int)(c)) : \
		(*(f)->f_p = (c), (int)*(f)->f_p++))

#define sm_eof(p)	(((p)->f_flags & SMFEOF) != 0)
#define sm_error(p)	(((p)->f_flags & SMERR) != 0)
#define sm_clearerr(p)	((void)((p)->f_flags &= ~(SMERR|SMFEOF)))

#define sm_io_eof(p)	sm_eof(p)
#define sm_io_error(p)	sm_error(p)

#define sm_io_clearerr(p)	sm_clearerr(p)

#ifndef lint
# ifndef _POSIX_SOURCE
#  define sm_io_getc(fp, t)	sm_getc(fp, t)
#  define sm_io_putc(fp, t, x)	sm_putc(fp, t, x)
# endif /* _POSIX_SOURCE */
#endif /* lint */

#endif /* SM_IO_H */
