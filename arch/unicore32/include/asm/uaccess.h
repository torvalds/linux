/*
 * linux/arch/unicore32/include/asm/uaccess.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __UNICORE_UACCESS_H__
#define __UNICORE_UACCESS_H__

#include <linux/thread_info.h>
#include <linux/errno.h>

#include <asm/memory.h>
#include <asm/system.h>

#define __copy_from_user	__copy_from_user
#define __copy_to_user		__copy_to_user
#define __strncpy_from_user	__strncpy_from_user
#define __strnlen_user		__strnlen_user
#define __clear_user		__clear_user

#define __kernel_ok		(segment_eq(get_fs(), KERNEL_DS))
#define __user_ok(addr, size)	(((size) <= TASK_SIZE)			\
				&& ((addr) <= TASK_SIZE - (size)))
#define __access_ok(addr, size)	(__kernel_ok || __user_ok((addr), (size)))

extern unsigned long __must_check
__copy_from_user(void *to, const void __user *from, unsigned long n);
extern unsigned long __must_check
__copy_to_user(void __user *to, const void *from, unsigned long n);
extern unsigned long __must_check
__clear_user(void __user *addr, unsigned long n);
extern unsigned long __must_check
__strncpy_from_user(char *to, const char __user *from, unsigned long count);
extern unsigned long
__strnlen_user(const char __user *s, long n);

#include <asm-generic/uaccess.h>

extern int fixup_exception(struct pt_regs *regs);

#endif /* __UNICORE_UACCESS_H__ */
