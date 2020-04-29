/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef __ASM_CSKY_SYSCALLS_H
#define __ASM_CSKY_SYSCALLS_H

#include <asm-generic/syscalls.h>

long sys_cacheflush(void __user *, unsigned long, int);

long sys_set_thread_area(unsigned long addr);

long sys_csky_fadvise64_64(int fd, int advice, loff_t offset, loff_t len);

#endif /* __ASM_CSKY_SYSCALLS_H */
