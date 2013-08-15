#ifndef _PARISC_FCNTL_H
#define _PARISC_FCNTL_H

#define O_APPEND	000000010
#define O_BLKSEEK	000000100 /* HPUX only */
#define O_CREAT		000000400 /* not fcntl */
#define O_EXCL		000002000 /* not fcntl */
#define O_LARGEFILE	000004000
#define __O_SYNC	000100000
#define O_SYNC		(__O_SYNC|O_DSYNC)
#define O_NONBLOCK	000200004 /* HPUX has separate NDELAY & NONBLOCK */
#define O_NOCTTY	000400000 /* not fcntl */
#define O_DSYNC		001000000 /* HPUX only */
#define O_RSYNC		002000000 /* HPUX only */
#define O_NOATIME	004000000
#define O_CLOEXEC	010000000 /* set close_on_exec */

#define O_DIRECTORY	000010000 /* must be a directory */
#define O_NOFOLLOW	000000200 /* don't follow links */
#define O_INVISIBLE	004000000 /* invisible I/O, for DMAPI/XDSM */

#define O_PATH		020000000
#define __O_TMPFILE	040000000

#define F_GETLK64	8
#define F_SETLK64	9
#define F_SETLKW64	10

#define F_GETOWN	11	/*  for sockets. */
#define F_SETOWN	12	/*  for sockets. */
#define F_SETSIG	13	/*  for sockets. */
#define F_GETSIG	14	/*  for sockets. */

/* for posix fcntl() and lockf() */
#define F_RDLCK		01
#define F_WRLCK		02
#define F_UNLCK		03

#include <asm-generic/fcntl.h>

#endif
