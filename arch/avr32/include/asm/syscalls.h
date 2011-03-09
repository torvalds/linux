/*
 * syscalls.h - Linux syscall interfaces (arch-specific)
 *
 * Copyright (c) 2008 Jaswinder Singh
 *
 * This file is released under the GPLv2.
 * See the file COPYING for more details.
 */

#ifndef _ASM_AVR32_SYSCALLS_H
#define _ASM_AVR32_SYSCALLS_H

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/signal.h>

/* mm/cache.c */
asmlinkage int sys_cacheflush(int, void __user *, size_t);

#endif /* _ASM_AVR32_SYSCALLS_H */
