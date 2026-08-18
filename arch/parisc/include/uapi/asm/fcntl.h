/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _PARISC_FCNTL_H
#define _PARISC_FCNTL_H

#define O_APPEND	(1 << 3)
#define O_CREAT		(1 << 8)	/* not fcntl */
#define O_EXCL		(1 << 10)	/* not fcntl */
#define O_LARGEFILE	(1 << 11)
#define __O_SYNC	(1 << 15)
#define O_SYNC		(__O_SYNC|O_DSYNC)
#define O_NONBLOCK	(1 << 16)
#define O_NOCTTY	(1 << 17)	/* not fcntl */
#define O_DSYNC		(1 << 18)
#define O_NOATIME	(1 << 20)
#define O_CLOEXEC	(1 << 21)	/* set close_on_exec */

#define O_DIRECTORY	(1 << 12)	/* must be a directory */
#define O_NOFOLLOW	(1 << 7)	/* don't follow links */

#define O_PATH		(1 << 22)
#define __O_TMPFILE	(1 << 23)

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
