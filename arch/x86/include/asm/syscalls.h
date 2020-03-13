/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * syscalls.h - Linux syscall interfaces (arch-specific)
 *
 * Copyright (c) 2008 Jaswinder Singh Rajput
 */

#ifndef _ASM_X86_SYSCALLS_H
#define _ASM_X86_SYSCALLS_H

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/signal.h>
#include <linux/types.h>

/* Common in X86_32 and X86_64 */
/* kernel/ioport.c */
long ksys_ioperm(unsigned long from, unsigned long num, int turn_on);

#endif /* _ASM_X86_SYSCALLS_H */
