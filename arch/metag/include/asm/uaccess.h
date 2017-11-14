/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __METAG_UACCESS_H
#define __METAG_UACCESS_H

/*
 * User space memory access functions
 */

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */

#define MAKE_MM_SEG(s)  ((mm_segment_t) { (s) })

#define KERNEL_DS       MAKE_MM_SEG(0xFFFFFFFF)
#define USER_DS		MAKE_MM_SEG(PAGE_OFFSET)

#define get_ds()	(KERNEL_DS)
#define get_fs()        (current_thread_info()->addr_limit)
#define set_fs(x)       (current_thread_info()->addr_limit = (x))

#define segment_eq(a, b)	((a).seg == (b).seg)

static inline int __access_ok(unsigned long addr, unsigned long size)
{
	/*
	 * Allow access to the user mapped memory area, but not the system area
	 * before it. The check extends to the top of the address space when
	 * kernel access is allowed (there's no real reason to user copy to the
	 * system area in any case).
	 */
	if (likely(addr >= META_MEMORY_BASE && addr < get_fs().seg &&
		   size <= get_fs().seg - addr))
		return true;
	/*
	 * Explicitly allow NULL pointers here. Parts of the kernel such
	 * as readv/writev use access_ok to validate pointers, but want
	 * to allow NULL pointers for various reasons. NULL pointers are
	 * safe to allow through because the first page is not mappable on
	 * Meta.
	 */
	if (!addr)
		return true;
	/* Allow access to core code memory area... */
	if (addr >= LINCORE_CODE_BASE && addr <= LINCORE_CODE_LIMIT &&
	    size <= LINCORE_CODE_LIMIT + 1 - addr)
		return true;
	/* ... but no other areas. */
	return false;
}

#define access_ok(type, addr, size) __access_ok((unsigned long)(addr),	\
						(unsigned long)(size))

#include <asm/extable.h>

/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 */

#define put_user(x, ptr) \
	__put_user_check((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)))
#define __put_user(x, ptr) \
	__put_user_nocheck((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)))

extern void __put_user_bad(void);

#define __put_user_nocheck(x, ptr, size)		\
({                                                      \
	long __pu_err;                                  \
	__put_user_size((x), (ptr), (size), __pu_err);	\
	__pu_err;                                       \
})

#define __put_user_check(x, ptr, size)				\
({                                                              \
	long __pu_err = -EFAULT;                                \
	__typeof__(*(ptr)) __user *__pu_addr = (ptr);           \
	if (access_ok(VERIFY_WRITE, __pu_addr, size))		\
		__put_user_size((x), __pu_addr, (size), __pu_err);	\
	__pu_err;                                               \
})

extern long __put_user_asm_b(unsigned int x, void __user *addr);
extern long __put_user_asm_w(unsigned int x, void __user *addr);
extern long __put_user_asm_d(unsigned int x, void __user *addr);
extern long __put_user_asm_l(unsigned long long x, void __user *addr);

#define __put_user_size(x, ptr, size, retval)				\
do {                                                                    \
	retval = 0;                                                     \
	switch (size) {                                                 \
	case 1:								\
		retval = __put_user_asm_b((__force unsigned int)x, ptr);\
		break;							\
	case 2:								\
		retval = __put_user_asm_w((__force unsigned int)x, ptr);\
		break;							\
	case 4:								\
		retval = __put_user_asm_d((__force unsigned int)x, ptr);\
		break;							\
	case 8:								\
		retval = __put_user_asm_l((__force unsigned long long)x,\
					  ptr);				\
		break;							\
	default:							\
		__put_user_bad();					\
	}								\
} while (0)

#define get_user(x, ptr) \
	__get_user_check((x), (ptr), sizeof(*(ptr)))
#define __get_user(x, ptr) \
	__get_user_nocheck((x), (ptr), sizeof(*(ptr)))

extern long __get_user_bad(void);

#define __get_user_nocheck(x, ptr, size)			\
({                                                              \
	long __gu_err;						\
	long long __gu_val;					\
	__get_user_size(__gu_val, (ptr), (size), __gu_err);	\
	(x) = (__force __typeof__(*(ptr)))__gu_val;             \
	__gu_err;                                               \
})

#define __get_user_check(x, ptr, size)					\
({                                                                      \
	long __gu_err = -EFAULT;					\
	long long __gu_val = 0;						\
	const __typeof__(*(ptr)) __user *__gu_addr = (ptr);		\
	if (access_ok(VERIFY_READ, __gu_addr, size))			\
		__get_user_size(__gu_val, __gu_addr, (size), __gu_err);	\
	(x) = (__force __typeof__(*(ptr)))__gu_val;                     \
	__gu_err;                                                       \
})

extern unsigned char __get_user_asm_b(const void __user *addr, long *err);
extern unsigned short __get_user_asm_w(const void __user *addr, long *err);
extern unsigned int __get_user_asm_d(const void __user *addr, long *err);
extern unsigned long long __get_user_asm_l(const void __user *addr, long *err);

#define __get_user_size(x, ptr, size, retval)			\
do {                                                            \
	retval = 0;                                             \
	switch (size) {                                         \
	case 1:							\
		x = __get_user_asm_b(ptr, &retval); break;	\
	case 2:							\
		x = __get_user_asm_w(ptr, &retval); break;	\
	case 4:							\
		x = __get_user_asm_d(ptr, &retval); break;	\
	case 8:							\
		x = __get_user_asm_l(ptr, &retval); break;	\
	default:						\
		(x) = __get_user_bad();				\
	}                                                       \
} while (0)

/*
 * Copy a null terminated string from userspace.
 *
 * Must return:
 * -EFAULT		for an exception
 * count		if we hit the buffer limit
 * bytes copied		if we hit a null byte
 * (without the null byte)
 */

extern long __must_check __strncpy_from_user(char *dst, const char __user *src,
					     long count);

static inline long
strncpy_from_user(char *dst, const char __user *src, long count)
{
	if (!access_ok(VERIFY_READ, src, 1))
		return -EFAULT;
	return __strncpy_from_user(dst, src, count);
}
/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 on exception, a value greater than N if too long
 */
extern long __must_check strnlen_user(const char __user *src, long count);

extern unsigned long raw_copy_from_user(void *to, const void __user *from,
					unsigned long n);
extern unsigned long raw_copy_to_user(void __user *to, const void *from,
				      unsigned long n);

/*
 * Zero Userspace
 */

extern unsigned long __must_check __do_clear_user(void __user *to,
						  unsigned long n);

static inline unsigned long clear_user(void __user *to, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		return __do_clear_user(to, n);
	return n;
}

#define __clear_user(to, n)            __do_clear_user(to, n)

#endif /* _METAG_UACCESS_H */
