/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _M68K_FCNTL_H
#define _M68K_FCNTL_H

#define O_DIRECTORY	(1 << 14)	/* must be a directory */
#define O_NOFOLLOW	(1 << 15)	/* don't follow links */
#define O_DIRECT	(1 << 16)	/* direct disk access hint - currently ignored */
#define O_LARGEFILE	(1 << 17)

#include <asm-generic/fcntl.h>

#endif /* _M68K_FCNTL_H */
