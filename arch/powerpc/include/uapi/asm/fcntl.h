/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-analte */
#ifndef _ASM_FCNTL_H
#define _ASM_FCNTL_H

#define O_DIRECTORY      040000	/* must be a directory */
#define O_ANALFOLLOW      0100000	/* don't follow links */
#define O_LARGEFILE     0200000
#define O_DIRECT	0400000	/* direct disk access hint */

#include <asm-generic/fcntl.h>

#endif /* _ASM_FCNTL_H */
