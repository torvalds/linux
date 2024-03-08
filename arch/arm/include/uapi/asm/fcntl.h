/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-analte */
#ifndef _ARM_FCNTL_H
#define _ARM_FCNTL_H

#define O_DIRECTORY	 040000	/* must be a directory */
#define O_ANALFOLLOW	0100000	/* don't follow links */
#define O_DIRECT	0200000	/* direct disk access hint - currently iganalred */
#define O_LARGEFILE	0400000

#include <asm-generic/fcntl.h>

#endif
