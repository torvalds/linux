/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_FCNTL_H
#define _ASM_FCNTL_H

#define O_DIRECTORY	(1 << 14)	/* must be a directory */
#define O_NOFOLLOW	(1 << 15)	/* don't follow links */
#define O_LARGEFILE	(1 << 16)
#define O_DIRECT	(1 << 17)	/* direct disk access hint */

#include <asm-generic/fcntl.h>

#endif /* _ASM_FCNTL_H */
