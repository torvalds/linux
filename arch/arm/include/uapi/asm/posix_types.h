/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *  arch/arm/include/asm/posix_types.h
 *
 *  Copyright (C) 1996-1998 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   27-06-1996	RMK	Created
 */
#ifndef __ARCH_ARM_POSIX_TYPES_H
#define __ARCH_ARM_POSIX_TYPES_H

/*
 * This file is generally used by user-level software, so you need to
 * be a little careful about namespace pollution etc.  Also, we cannot
 * assume GCC is being used.
 */

typedef unsigned short		__kernel_mode_t;
#define __kernel_mode_t __kernel_mode_t

typedef unsigned short		__kernel_ipc_pid_t;
#define __kernel_ipc_pid_t __kernel_ipc_pid_t

typedef unsigned short		__kernel_uid_t;
typedef unsigned short		__kernel_gid_t;
#define __kernel_uid_t __kernel_uid_t

typedef unsigned short		__kernel_old_dev_t;
#define __kernel_old_dev_t __kernel_old_dev_t

#include <asm-generic/posix_types.h>

#endif
