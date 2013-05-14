/*
 * Copyright (C) 2009 Thomas Chou <thomas@wytron.com.tw>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#ifndef _ASM_NIOS2_POSIX_TYPES_H
#define _ASM_NIOS2_POSIX_TYPES_H

typedef unsigned short __kernel_mode_t;
#define __kernel_mode_t __kernel_mode_t

typedef unsigned short __kernel_nlink_t;
#define __kernel_nlink_t __kernel_nlink_t

#ifdef CONFIG_MMU
typedef unsigned int __kernel_ipc_pid_t;
#define __kernel_ipc_pid_t __kernel_ipc_pid_t

typedef unsigned long __kernel_size_t;
typedef long __kernel_ssize_t;
typedef int __kernel_ptrdiff_t;
#define __kernel_size_t __kernel_size_t

#else
typedef unsigned short __kernel_ipc_pid_t;
#define __kernel_ipc_pid_t __kernel_ipc_pid_t

typedef unsigned short __kernel_uid_t;
typedef unsigned short __kernel_gid_t;
#define __kernel_uid_t __kernel_uid_t

typedef unsigned int __kernel_uid32_t;
typedef unsigned int __kernel_gid32_t;
#define __kernel_uid32_t __kernel_uid32_t
#endif /* CONFIG_MMU */

typedef unsigned short __kernel_old_uid_t;
typedef unsigned short __kernel_old_gid_t;
#define __kernel_old_uid_t __kernel_old_uid_t

typedef unsigned short __kernel_old_dev_t;
#define __kernel_old_dev_t __kernel_old_dev_t

#include <asm-generic/posix_types.h>

#endif /* _ASM_NIOS2_POSIX_TYPES_H */
