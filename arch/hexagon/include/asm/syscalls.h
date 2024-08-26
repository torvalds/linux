/* SPDX-License-Identifier: GPL-2.0 */

#include <asm-generic/syscalls.h>

asmlinkage long sys_hexagon_fadvise64_64(int fd, int advice,
	                                  u32 a2, u32 a3, u32 a4, u32 a5);
