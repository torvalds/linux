/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2013 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

 #define sys_mmap2 sys_mmap_pgoff

#define __ARCH_WANT_RENAMEAT

/* Use the standard ABI for syscalls */
#include <asm-generic/unistd.h>

/* Additional Nios II specific syscalls. */
#define __NR_cacheflush (__NR_arch_specific_syscall)
__SYSCALL(__NR_cacheflush, sys_cacheflush)
