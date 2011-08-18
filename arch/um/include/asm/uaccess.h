/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __UM_UACCESS_H
#define __UM_UACCESS_H

/* thread_info has a mm_segment_t in it, so put the definition up here */
typedef struct {
	unsigned long seg;
} mm_segment_t;

#include <linux/thread_info.h>
#include <linux/errno.h>
#include <asm/processor.h>
#include <asm/elf.h>

#define VERIFY_READ 0
#define VERIFY_WRITE 1

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */

#define MAKE_MM_SEG(s)	((mm_segment_t) { (s) })

#define KERNEL_DS	MAKE_MM_SEG(0xFFFFFFFF)
#define USER_DS		MAKE_MM_SEG(TASK_SIZE)

#define get_ds()	(KERNEL_DS)
#define get_fs()	(current_thread_info()->addr_limit)
#define set_fs(x)	(current_thread_info()->addr_limit = (x))

#define segment_eq(a, b) ((a).seg == (b).seg)

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

#define __copy_from_user(to, from, n) copy_from_user(to, from, n)

#define __copy_to_user(to, from, n) copy_to_user(to, from, n)

#define __copy_to_user_inatomic __copy_to_user
#define __copy_from_user_inatomic __copy_from_user

#define __get_user(x, ptr) \
({ \
	const __typeof__(*(ptr)) __user *__private_ptr = (ptr);	\
	__typeof__(x) __private_val;			\
	int __private_ret = -EFAULT;			\
	(x) = (__typeof__(*(__private_ptr)))0;				\
	if (__copy_from_user((__force void *)&__private_val, (__private_ptr),\
			     sizeof(*(__private_ptr))) == 0) {		\
		(x) = (__typeof__(*(__private_ptr))) __private_val;	\
		__private_ret = 0;					\
	}								\
	__private_ret;							\
}) 

#define get_user(x, ptr) \
({ \
        const __typeof__((*(ptr))) __user *private_ptr = (ptr); \
        (access_ok(VERIFY_READ, private_ptr, sizeof(*private_ptr)) ? \
	 __get_user(x, private_ptr) : ((x) = (__typeof__(*ptr))0, -EFAULT)); \
})

#define __put_user(x, ptr) \
({ \
        __typeof__(*(ptr)) __user *__private_ptr = ptr; \
        __typeof__(*(__private_ptr)) __private_val; \
        int __private_ret = -EFAULT; \
        __private_val = (__typeof__(*(__private_ptr))) (x); \
        if (__copy_to_user((__private_ptr), &__private_val, \
			   sizeof(*(__private_ptr))) == 0) { \
		__private_ret = 0; \
	} \
        __private_ret; \
})

#define put_user(x, ptr) \
({ \
        __typeof__(*(ptr)) __user *private_ptr = (ptr); \
        (access_ok(VERIFY_WRITE, private_ptr, sizeof(*private_ptr)) ? \
	 __put_user(x, private_ptr) : -EFAULT); \
})

#define strlen_user(str) strnlen_user(str, ~0U >> 1)

struct exception_table_entry
{
        unsigned long insn;
	unsigned long fixup;
};

#endif
