/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Copyright (C) 2015 Richard Weinberger (richard@nod.at)
 */

#ifndef __UM_UACCESS_H
#define __UM_UACCESS_H

#include <asm/elf.h>
#include <linux/unaligned.h>

#define __under_task_size(addr, size) \
	(((unsigned long) (addr) < TASK_SIZE) && \
	 (((unsigned long) (addr) + (size)) < TASK_SIZE))

#define __access_ok_vsyscall(addr, size) \
	  (((unsigned long) (addr) >= FIXADDR_USER_START) && \
	  ((unsigned long) (addr) + (size) <= FIXADDR_USER_END) && \
	  ((unsigned long) (addr) + (size) >= (unsigned long)(addr)))

#define __addr_range_nowrap(addr, size) \
	((unsigned long) (addr) <= ((unsigned long) (addr) + (size)))

extern unsigned long raw_copy_from_user(void *to, const void __user *from, unsigned long n);
extern unsigned long raw_copy_to_user(void __user *to, const void *from, unsigned long n);
extern unsigned long __clear_user(void __user *mem, unsigned long len);
static inline int __access_ok(const void __user *ptr, unsigned long size);

/* Teach asm-generic/uaccess.h that we have C functions for these. */
#define __access_ok __access_ok
#define __clear_user __clear_user

#define INLINE_COPY_FROM_USER
#define INLINE_COPY_TO_USER

#include <asm-generic/uaccess.h>

static inline int __access_ok(const void __user *ptr, unsigned long size)
{
	unsigned long addr = (unsigned long)ptr;
	return __addr_range_nowrap(addr, size) &&
		(__under_task_size(addr, size) ||
		 __access_ok_vsyscall(addr, size));
}

/* no pagefaults for kernel addresses in um */
#define __get_kernel_nofault(dst, src, type, err_label)			\
do {									\
	*((type *)dst) = get_unaligned((type *)(src));			\
	if (0) /* make sure the label looks used to the compiler */	\
		goto err_label;						\
} while (0)

#define __put_kernel_nofault(dst, src, type, err_label)			\
do {									\
	put_unaligned(*((type *)src), (type *)(dst));			\
	if (0) /* make sure the label looks used to the compiler */	\
		goto err_label;						\
} while (0)

#endif
