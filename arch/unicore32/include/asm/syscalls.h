/*
 * linux/arch/unicore32/include/asm/syscalls.h
 *
 * Code specific to UniCore ISA
 *
 * Copyright (C) 2014 GUAN Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __UNICORE32_ASM_SYSCALLS_H__
#define __UNICORE32_ASM_SYSCALLS_H__

#include <asm-generic/syscalls.h>

#ifdef CONFIG_UNICORE32_OLDABI
/* Wrapper functions */
extern asmlinkage long sys_clone_wrapper(unsigned long clone_flags,
		unsigned long newsp, int __user *parent_tidptr,
		int tls_val, int __user *child_tidptr);
extern asmlinkage long sys_sigreturn_wrapper(struct pt_regs *regs);
#endif /* CONFIG_UNICORE32_OLDABI */

#endif /* __UNICORE32_ASM_SYSCALLS_H__ */
