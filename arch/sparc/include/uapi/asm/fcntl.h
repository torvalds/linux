/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _SPARC_FCNTL_H
#define _SPARC_FCNTL_H

#define O_APPEND	(1 << 3)
#define FASYNC		(1 << 6)	/* fcntl, for BSD compatibility */
#define O_CREAT		(1 << 9)	/* not fcntl */
#define O_TRUNC		(1 << 10)	/* not fcntl */
#define O_EXCL		(1 << 11)	/* not fcntl */
#define O_DSYNC		(1 << 13)	/* used to be O_SYNC, see below */
#define O_NONBLOCK	(1 << 14)
#if defined(__sparc__) && defined(__arch64__)
#define O_NDELAY	(1 << 2)
#else
#define O_NDELAY	((1 << 2) | O_NONBLOCK)
#endif
#define O_NOCTTY	(1 << 15)	/* not fcntl */
#define O_LARGEFILE	(1 << 18)
#define O_DIRECT	(1 << 20)	/* direct disk access hint */
#define O_NOATIME	(1 << 21)
#define O_CLOEXEC	(1 << 22)
/*
 * Before Linux 2.6.33 only O_DSYNC semantics were implemented, but using
 * the O_SYNC flag.  We continue to use the existing numerical value
 * for O_DSYNC semantics now, but using the correct symbolic name for it.
 * This new value is used to request true Posix O_SYNC semantics.  It is
 * defined in this strange way to make sure applications compiled against
 * new headers get at least O_DSYNC semantics on older kernels.
 *
 * This has the nice side-effect that we can simply test for O_DSYNC
 * wherever we do not care if O_DSYNC or O_SYNC is used.
 *
 * Note: __O_SYNC must never be used directly.
 */
#define __O_SYNC	(1 << 23)
#define O_SYNC		(__O_SYNC|O_DSYNC)

#define O_PATH		(1 << 24)
#define __O_TMPFILE	(1 << 25)

#define F_GETOWN	5	/*  for sockets. */
#define F_SETOWN	6	/*  for sockets. */
#define F_GETLK		7
#define F_SETLK		8
#define F_SETLKW	9

/* for posix fcntl() and lockf() */
#define F_RDLCK		1
#define F_WRLCK		2
#define F_UNLCK		3

#define __ARCH_FLOCK_PAD	short __unused;
#define __ARCH_FLOCK64_PAD	short __unused;

#include <asm-generic/fcntl.h>

#endif
