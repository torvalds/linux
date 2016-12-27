/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Copyright (C) 2015 Richard Weinberger (richard@nod.at)
 * Licensed under the GPL
 */

#ifndef __UM_UACCESS_H
#define __UM_UACCESS_H

#include <asm/elf.h>

#define __under_task_size(addr, size) \
	(((unsigned long) (addr) < TASK_SIZE) && \
	 (((unsigned long) (addr) + (size)) < TASK_SIZE))

#define __access_ok_vsyscall(addr, size) \
	  (((unsigned long) (addr) >= FIXADDR_USER_START) && \
	  ((unsigned long) (addr) + (size) <= FIXADDR_USER_END) && \
	  ((unsigned long) (addr) + (size) >= (unsigned long)(addr)))

#define __addr_range_nowrap(addr, size) \
	((unsigned long) (addr) <= ((unsigned long) (addr) + (size)))

extern long __copy_from_user(void *to, const void __user *from, unsigned long n);
extern long __copy_to_user(void __user *to, const void *from, unsigned long n);
extern long __strncpy_from_user(char *dst, const char __user *src, long count);
extern long __strnlen_user(const void __user *str, long len);
extern unsigned long __clear_user(void __user *mem, unsigned long len);
static inline int __access_ok(unsigned long addr, unsigned long size);

/* Teach asm-generic/uaccess.h that we have C functions for these. */
#define __access_ok __access_ok
#define __clear_user __clear_user
#define __copy_to_user __copy_to_user
#define __copy_from_user __copy_from_user
#define __strnlen_user __strnlen_user
#define __strncpy_from_user __strncpy_from_user
#define __copy_to_user_inatomic __copy_to_user
#define __copy_from_user_inatomic __copy_from_user

#include <asm-generic/uaccess.h>

static inline int __access_ok(unsigned long addr, unsigned long size)
{
	return __addr_range_nowrap(addr, size) &&
		(__under_task_size(addr, size) ||
		__access_ok_vsyscall(addr, size) ||
		segment_eq(get_fs(), KERNEL_DS));
}

#endif
