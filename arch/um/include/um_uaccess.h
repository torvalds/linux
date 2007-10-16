/* 
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __ARCH_UM_UACCESS_H
#define __ARCH_UM_UACCESS_H

#include "asm/fixmap.h"

#define __under_task_size(addr, size) \
	(((unsigned long) (addr) < TASK_SIZE) && \
	 (((unsigned long) (addr) + (size)) < TASK_SIZE))

#define __access_ok_vsyscall(type, addr, size) \
	 ((type == VERIFY_READ) && \
	  ((unsigned long) (addr) >= FIXADDR_USER_START) && \
	  ((unsigned long) (addr) + (size) <= FIXADDR_USER_END) && \
	  ((unsigned long) (addr) + (size) >= (unsigned long)(addr)))

#define __addr_range_nowrap(addr, size) \
	((unsigned long) (addr) <= ((unsigned long) (addr) + (size)))

#define access_ok(type, addr, size) \
	(__addr_range_nowrap(addr, size) && \
	 (__under_task_size(addr, size) || \
	  __access_ok_vsyscall(type, addr, size) || \
	  segment_eq(get_fs(), KERNEL_DS)))

extern int copy_from_user(void *to, const void __user *from, int n);
extern int copy_to_user(void __user *to, const void *from, int n);

extern int __do_copy_to_user(void *to, const void *from, int n,
			     void **fault_addr, jmp_buf **fault_catcher);
extern void __do_copy(void *to, const void *from, int n);

/*
 * strncpy_from_user: - Copy a NUL terminated string from userspace.
 * @dst:   Destination address, in kernel space.  This buffer must be at
 *         least @count bytes long.
 * @src:   Source address, in user space.
 * @count: Maximum number of bytes to copy, including the trailing NUL.
 *
 * Copies a NUL-terminated string from userspace to kernel space.
 *
 * On success, returns the length of the string (not including the trailing
 * NUL).
 *
 * If access to userspace fails, returns -EFAULT (some data may have been
 * copied).
 *
 * If @count is smaller than the length of the string, copies @count bytes
 * and returns @count.
 */

extern int strncpy_from_user(char *dst, const char __user *src, int count);

/*
 * __clear_user: - Zero a block of memory in user space, with less checking.
 * @to:   Destination address, in user space.
 * @n:    Number of bytes to zero.
 *
 * Zero a block of memory in user space.  Caller must check
 * the specified block with access_ok() before calling this function.
 *
 * Returns number of bytes that could not be cleared.
 * On success, this will be zero.
 */
extern int __clear_user(void __user *mem, int len);

/*
 * clear_user: - Zero a block of memory in user space.
 * @to:   Destination address, in user space.
 * @n:    Number of bytes to zero.
 *
 * Zero a block of memory in user space.
 *
 * Returns number of bytes that could not be cleared.
 * On success, this will be zero.
 */
extern int clear_user(void __user *mem, int len);

/*
 * strlen_user: - Get the size of a string in user space.
 * @str: The string to measure.
 * @n:   The maximum valid length
 *
 * Get the size of a NUL-terminated string in user space.
 *
 * Returns the size of the string INCLUDING the terminating NUL.
 * On exception, returns 0.
 * If the string is too long, returns a value greater than @n.
 */
extern int strnlen_user(const void __user *str, int len);

#endif
