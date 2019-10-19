/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_UACCESS_H
#define __ASM_SH_UACCESS_H

#include <asm/segment.h>
#include <asm/extable.h>

#define __addr_ok(addr) \
	((unsigned long __force)(addr) < current_thread_info()->addr_limit.seg)

/*
 * __access_ok: Check if address with size is OK or not.
 *
 * Uhhuh, this needs 33-bit arithmetic. We have a carry..
 *
 * sum := addr + size;  carry? --> flag = true;
 * if (sum >= addr_limit) flag = true;
 */
#define __access_ok(addr, size)	({				\
	unsigned long __ao_a = (addr), __ao_b = (size);		\
	unsigned long __ao_end = __ao_a + __ao_b - !!__ao_b;	\
	__ao_end >= __ao_a && __addr_ok(__ao_end); })

#define access_ok(addr, size)	\
	(__chk_user_ptr(addr),		\
	 __access_ok((unsigned long __force)(addr), (size)))

#define user_addr_max()	(current_thread_info()->addr_limit.seg)

/*
 * Uh, these should become the main single-value transfer routines ...
 * They automatically use the right size if we just have the right
 * pointer type ...
 *
 * As SuperH uses the same address space for kernel and user data, we
 * can just do these as direct assignments.
 *
 * Careful to not
 * (a) re-use the arguments for side effects (sizeof is ok)
 * (b) require any knowledge of processes at this stage
 */
#define put_user(x,ptr)		__put_user_check((x), (ptr), sizeof(*(ptr)))
#define get_user(x,ptr)		__get_user_check((x), (ptr), sizeof(*(ptr)))

/*
 * The "__xxx" versions do not do address space checking, useful when
 * doing multiple accesses to the same area (the user has to do the
 * checks by hand with "access_ok()")
 */
#define __put_user(x,ptr)	__put_user_nocheck((x), (ptr), sizeof(*(ptr)))
#define __get_user(x,ptr)	__get_user_nocheck((x), (ptr), sizeof(*(ptr)))

struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct __user *)(x))

#define __get_user_nocheck(x,ptr,size)				\
({								\
	long __gu_err;						\
	unsigned long __gu_val;					\
	const __typeof__(*(ptr)) __user *__gu_addr = (ptr);	\
	__chk_user_ptr(ptr);					\
	__get_user_size(__gu_val, __gu_addr, (size), __gu_err);	\
	(x) = (__force __typeof__(*(ptr)))__gu_val;		\
	__gu_err;						\
})

#define __get_user_check(x,ptr,size)					\
({									\
	long __gu_err = -EFAULT;					\
	unsigned long __gu_val = 0;					\
	const __typeof__(*(ptr)) *__gu_addr = (ptr);			\
	if (likely(access_ok(__gu_addr, (size))))		\
		__get_user_size(__gu_val, __gu_addr, (size), __gu_err);	\
	(x) = (__force __typeof__(*(ptr)))__gu_val;			\
	__gu_err;							\
})

#define __put_user_nocheck(x,ptr,size)				\
({								\
	long __pu_err;						\
	__typeof__(*(ptr)) __user *__pu_addr = (ptr);		\
	__typeof__(*(ptr)) __pu_val = x;			\
	__chk_user_ptr(ptr);					\
	__put_user_size(__pu_val, __pu_addr, (size), __pu_err);	\
	__pu_err;						\
})

#define __put_user_check(x,ptr,size)				\
({								\
	long __pu_err = -EFAULT;				\
	__typeof__(*(ptr)) __user *__pu_addr = (ptr);		\
	__typeof__(*(ptr)) __pu_val = x;			\
	if (likely(access_ok(__pu_addr, size)))	\
		__put_user_size(__pu_val, __pu_addr, (size),	\
				__pu_err);			\
	__pu_err;						\
})

#ifdef CONFIG_SUPERH32
# include <asm/uaccess_32.h>
#else
# include <asm/uaccess_64.h>
#endif

extern long strncpy_from_user(char *dest, const char __user *src, long count);

extern __must_check long strnlen_user(const char __user *str, long n);

/* Generic arbitrary sized copy.  */
/* Return the number of bytes NOT copied */
__kernel_size_t __copy_user(void *to, const void *from, __kernel_size_t n);

static __always_inline unsigned long
raw_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	return __copy_user(to, (__force void *)from, n);
}

static __always_inline unsigned long __must_check
raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	return __copy_user((__force void *)to, from, n);
}
#define INLINE_COPY_FROM_USER
#define INLINE_COPY_TO_USER

/*
 * Clear the area and return remaining number of bytes
 * (on failure.  Usually it's 0.)
 */
__kernel_size_t __clear_user(void *addr, __kernel_size_t size);

#define clear_user(addr,n)						\
({									\
	void __user * __cl_addr = (addr);				\
	unsigned long __cl_size = (n);					\
									\
	if (__cl_size && access_ok(__cl_addr, __cl_size))		\
		__cl_size = __clear_user(__cl_addr, __cl_size);		\
									\
	__cl_size;							\
})

extern void *set_exception_table_vec(unsigned int vec, void *handler);

static inline void *set_exception_table_evt(unsigned int evt, void *handler)
{
	return set_exception_table_vec(evt >> 5, handler);
}

struct mem_access {
	unsigned long (*from)(void *dst, const void __user *src, unsigned long cnt);
	unsigned long (*to)(void __user *dst, const void *src, unsigned long cnt);
};

int handle_unaligned_access(insn_size_t instruction, struct pt_regs *regs,
			    struct mem_access *ma, int, unsigned long address);

#endif /* __ASM_SH_UACCESS_H */
