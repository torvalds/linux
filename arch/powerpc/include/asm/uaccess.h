/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ARCH_POWERPC_UACCESS_H
#define _ARCH_POWERPC_UACCESS_H

#include <asm/ppc_asm.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/extable.h>
#include <asm/kup.h>

#ifdef __powerpc64__
/* We use TASK_SIZE_USER64 as TASK_SIZE is not constant */
#define TASK_SIZE_MAX		TASK_SIZE_USER64
#else
#define TASK_SIZE_MAX		TASK_SIZE
#endif

static inline bool __access_ok(unsigned long addr, unsigned long size)
{
	return addr < TASK_SIZE_MAX && size <= TASK_SIZE_MAX - addr;
}

#define access_ok(addr, size)		\
	(__chk_user_ptr(addr),		\
	 __access_ok((unsigned long)(addr), (size)))

/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 *
 * This gets kind of ugly. We want to return _two_ values in "get_user()"
 * and yet we don't want to do any pointers, because that is too much
 * of a performance impact. Thus we have a few rather ugly macros here,
 * and hide all the ugliness from the user.
 *
 * The "__xxx" versions of the user access functions are versions that
 * do not verify the address space, that must have been done previously
 * with a separate "access_ok()" call (this is used when we do multiple
 * accesses to the same area of user memory).
 *
 * As we use the same address space for kernel and user data on the
 * PowerPC, we can just do these as direct assignments.  (Of course, the
 * exception handling means that it's no longer "just"...)
 *
 */
#define get_user(x, ptr) \
	__get_user_check((x), (ptr), sizeof(*(ptr)))
#define put_user(x, ptr) \
	__put_user_check((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)))

#define __get_user(x, ptr) \
	__get_user_nocheck((x), (ptr), sizeof(*(ptr)), true)
#define __put_user(x, ptr) \
	__put_user_nocheck((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)))
#define __put_user_goto(x, ptr, label) \
	__put_user_nocheck_goto((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)), label)

#define __get_user_allowed(x, ptr) \
	__get_user_nocheck((x), (ptr), sizeof(*(ptr)), false)

#define __get_user_inatomic(x, ptr) \
	__get_user_nosleep((x), (ptr), sizeof(*(ptr)))
#define __put_user_inatomic(x, ptr) \
	__put_user_nosleep((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)))

#ifdef CONFIG_PPC64

#define ___get_user_instr(gu_op, dest, ptr)				\
({									\
	long __gui_ret = 0;						\
	unsigned long __gui_ptr = (unsigned long)ptr;			\
	struct ppc_inst __gui_inst;					\
	unsigned int __prefix, __suffix;				\
	__gui_ret = gu_op(__prefix, (unsigned int __user *)__gui_ptr);	\
	if (__gui_ret == 0) {						\
		if ((__prefix >> 26) == OP_PREFIX) {			\
			__gui_ret = gu_op(__suffix,			\
				(unsigned int __user *)__gui_ptr + 1);	\
			__gui_inst = ppc_inst_prefix(__prefix,		\
						     __suffix);		\
		} else {						\
			__gui_inst = ppc_inst(__prefix);		\
		}							\
		if (__gui_ret == 0)					\
			(dest) = __gui_inst;				\
	}								\
	__gui_ret;							\
})

#define get_user_instr(x, ptr) \
	___get_user_instr(get_user, x, ptr)

#define __get_user_instr(x, ptr) \
	___get_user_instr(__get_user, x, ptr)

#define __get_user_instr_inatomic(x, ptr) \
	___get_user_instr(__get_user_inatomic, x, ptr)

#else /* !CONFIG_PPC64 */
#define get_user_instr(x, ptr) \
	get_user((x).val, (u32 __user *)(ptr))

#define __get_user_instr(x, ptr) \
	__get_user_nocheck((x).val, (u32 __user *)(ptr), sizeof(u32), true)

#define __get_user_instr_inatomic(x, ptr) \
	__get_user_nosleep((x).val, (u32 __user *)(ptr), sizeof(u32))

#endif /* CONFIG_PPC64 */

extern long __put_user_bad(void);

#define __put_user_size_allowed(x, ptr, size, retval)		\
do {								\
	__label__ __pu_failed;					\
								\
	retval = 0;						\
	__put_user_size_goto(x, ptr, size, __pu_failed);	\
	break;							\
								\
__pu_failed:							\
	retval = -EFAULT;					\
} while (0)

#define __put_user_size(x, ptr, size, retval)			\
do {								\
	allow_write_to_user(ptr, size);				\
	__put_user_size_allowed(x, ptr, size, retval);		\
	prevent_write_to_user(ptr, size);			\
} while (0)

#define __put_user_nocheck(x, ptr, size)			\
({								\
	long __pu_err;						\
	__typeof__(*(ptr)) __user *__pu_addr = (ptr);		\
	__typeof__(*(ptr)) __pu_val = (x);			\
	__typeof__(size) __pu_size = (size);			\
								\
	if (!is_kernel_addr((unsigned long)__pu_addr))		\
		might_fault();					\
	__chk_user_ptr(__pu_addr);				\
	__put_user_size(__pu_val, __pu_addr, __pu_size, __pu_err);	\
								\
	__pu_err;						\
})

#define __put_user_check(x, ptr, size)					\
({									\
	long __pu_err = -EFAULT;					\
	__typeof__(*(ptr)) __user *__pu_addr = (ptr);			\
	__typeof__(*(ptr)) __pu_val = (x);				\
	__typeof__(size) __pu_size = (size);				\
									\
	might_fault();							\
	if (access_ok(__pu_addr, __pu_size))				\
		__put_user_size(__pu_val, __pu_addr, __pu_size, __pu_err); \
									\
	__pu_err;							\
})

#define __put_user_nosleep(x, ptr, size)			\
({								\
	long __pu_err;						\
	__typeof__(*(ptr)) __user *__pu_addr = (ptr);		\
	__typeof__(*(ptr)) __pu_val = (x);			\
	__typeof__(size) __pu_size = (size);			\
								\
	__chk_user_ptr(__pu_addr);				\
	__put_user_size(__pu_val, __pu_addr, __pu_size, __pu_err); \
								\
	__pu_err;						\
})


/*
 * We don't tell gcc that we are accessing memory, but this is OK
 * because we do not write to any memory gcc knows about, so there
 * are no aliasing issues.
 */
#define __put_user_asm_goto(x, addr, label, op)			\
	asm_volatile_goto(					\
		"1:	" op "%U1%X1 %0,%1	# put_user\n"	\
		EX_TABLE(1b, %l2)				\
		:						\
		: "r" (x), "m"UPD_CONSTR (*addr)		\
		:						\
		: label)

#ifdef __powerpc64__
#define __put_user_asm2_goto(x, ptr, label)			\
	__put_user_asm_goto(x, ptr, label, "std")
#else /* __powerpc64__ */
#define __put_user_asm2_goto(x, addr, label)			\
	asm_volatile_goto(					\
		"1:	stw%X1 %0, %1\n"			\
		"2:	stw%X1 %L0, %L1\n"			\
		EX_TABLE(1b, %l2)				\
		EX_TABLE(2b, %l2)				\
		:						\
		: "r" (x), "m" (*addr)				\
		:						\
		: label)
#endif /* __powerpc64__ */

#define __put_user_size_goto(x, ptr, size, label)		\
do {								\
	switch (size) {						\
	case 1: __put_user_asm_goto(x, ptr, label, "stb"); break;	\
	case 2: __put_user_asm_goto(x, ptr, label, "sth"); break;	\
	case 4: __put_user_asm_goto(x, ptr, label, "stw"); break;	\
	case 8: __put_user_asm2_goto(x, ptr, label); break;	\
	default: __put_user_bad();				\
	}							\
} while (0)

#define __put_user_nocheck_goto(x, ptr, size, label)		\
do {								\
	__typeof__(*(ptr)) __user *__pu_addr = (ptr);		\
	__chk_user_ptr(ptr);					\
	__put_user_size_goto((x), __pu_addr, (size), label);	\
} while (0)


extern long __get_user_bad(void);

/*
 * This does an atomic 128 byte aligned load from userspace.
 * Upto caller to do enable_kernel_vmx() before calling!
 */
#define __get_user_atomic_128_aligned(kaddr, uaddr, err)		\
	__asm__ __volatile__(				\
		"1:	lvx  0,0,%1	# get user\n"	\
		" 	stvx 0,0,%2	# put kernel\n"	\
		"2:\n"					\
		".section .fixup,\"ax\"\n"		\
		"3:	li %0,%3\n"			\
		"	b 2b\n"				\
		".previous\n"				\
		EX_TABLE(1b, 3b)			\
		: "=r" (err)			\
		: "b" (uaddr), "b" (kaddr), "i" (-EFAULT), "0" (err))

#define __get_user_asm(x, addr, err, op)		\
	__asm__ __volatile__(				\
		"1:	"op"%U2%X2 %1, %2	# get_user\n"	\
		"2:\n"					\
		".section .fixup,\"ax\"\n"		\
		"3:	li %0,%3\n"			\
		"	li %1,0\n"			\
		"	b 2b\n"				\
		".previous\n"				\
		EX_TABLE(1b, 3b)			\
		: "=r" (err), "=r" (x)			\
		: "m"UPD_CONSTR (*addr), "i" (-EFAULT), "0" (err))

#ifdef __powerpc64__
#define __get_user_asm2(x, addr, err)			\
	__get_user_asm(x, addr, err, "ld")
#else /* __powerpc64__ */
#define __get_user_asm2(x, addr, err)			\
	__asm__ __volatile__(				\
		"1:	lwz%X2 %1, %2\n"			\
		"2:	lwz%X2 %L1, %L2\n"		\
		"3:\n"					\
		".section .fixup,\"ax\"\n"		\
		"4:	li %0,%3\n"			\
		"	li %1,0\n"			\
		"	li %1+1,0\n"			\
		"	b 3b\n"				\
		".previous\n"				\
		EX_TABLE(1b, 4b)			\
		EX_TABLE(2b, 4b)			\
		: "=r" (err), "=&r" (x)			\
		: "m" (*addr), "i" (-EFAULT), "0" (err))
#endif /* __powerpc64__ */

#define __get_user_size_allowed(x, ptr, size, retval)		\
do {								\
	retval = 0;						\
	__chk_user_ptr(ptr);					\
	if (size > sizeof(x))					\
		(x) = __get_user_bad();				\
	switch (size) {						\
	case 1: __get_user_asm(x, (u8 __user *)ptr, retval, "lbz"); break;	\
	case 2: __get_user_asm(x, (u16 __user *)ptr, retval, "lhz"); break;	\
	case 4: __get_user_asm(x, (u32 __user *)ptr, retval, "lwz"); break;	\
	case 8: __get_user_asm2(x, (u64 __user *)ptr, retval);  break;	\
	default: (x) = __get_user_bad();			\
	}							\
} while (0)

#define __get_user_size(x, ptr, size, retval)			\
do {								\
	allow_read_from_user(ptr, size);			\
	__get_user_size_allowed(x, ptr, size, retval);		\
	prevent_read_from_user(ptr, size);			\
} while (0)

/*
 * This is a type: either unsigned long, if the argument fits into
 * that type, or otherwise unsigned long long.
 */
#define __long_type(x) \
	__typeof__(__builtin_choose_expr(sizeof(x) > sizeof(0UL), 0ULL, 0UL))

#define __get_user_nocheck(x, ptr, size, do_allow)			\
({								\
	long __gu_err;						\
	__long_type(*(ptr)) __gu_val;				\
	__typeof__(*(ptr)) __user *__gu_addr = (ptr);	\
	__typeof__(size) __gu_size = (size);			\
								\
	__chk_user_ptr(__gu_addr);				\
	if (do_allow && !is_kernel_addr((unsigned long)__gu_addr)) \
		might_fault();					\
	barrier_nospec();					\
	if (do_allow)								\
		__get_user_size(__gu_val, __gu_addr, __gu_size, __gu_err);	\
	else									\
		__get_user_size_allowed(__gu_val, __gu_addr, __gu_size, __gu_err); \
	(x) = (__typeof__(*(ptr)))__gu_val;			\
								\
	__gu_err;						\
})

#define __get_user_check(x, ptr, size)					\
({									\
	long __gu_err = -EFAULT;					\
	__long_type(*(ptr)) __gu_val = 0;				\
	__typeof__(*(ptr)) __user *__gu_addr = (ptr);		\
	__typeof__(size) __gu_size = (size);				\
									\
	might_fault();							\
	if (access_ok(__gu_addr, __gu_size)) {				\
		barrier_nospec();					\
		__get_user_size(__gu_val, __gu_addr, __gu_size, __gu_err); \
	}								\
	(x) = (__force __typeof__(*(ptr)))__gu_val;				\
									\
	__gu_err;							\
})

#define __get_user_nosleep(x, ptr, size)			\
({								\
	long __gu_err;						\
	__long_type(*(ptr)) __gu_val;				\
	__typeof__(*(ptr)) __user *__gu_addr = (ptr);	\
	__typeof__(size) __gu_size = (size);			\
								\
	__chk_user_ptr(__gu_addr);				\
	barrier_nospec();					\
	__get_user_size(__gu_val, __gu_addr, __gu_size, __gu_err); \
	(x) = (__force __typeof__(*(ptr)))__gu_val;			\
								\
	__gu_err;						\
})


/* more complex routines */

extern unsigned long __copy_tofrom_user(void __user *to,
		const void __user *from, unsigned long size);

#ifdef CONFIG_ARCH_HAS_COPY_MC
unsigned long __must_check
copy_mc_generic(void *to, const void *from, unsigned long size);

static inline unsigned long __must_check
copy_mc_to_kernel(void *to, const void *from, unsigned long size)
{
	return copy_mc_generic(to, from, size);
}
#define copy_mc_to_kernel copy_mc_to_kernel

static inline unsigned long __must_check
copy_mc_to_user(void __user *to, const void *from, unsigned long n)
{
	if (likely(check_copy_size(from, n, true))) {
		if (access_ok(to, n)) {
			allow_write_to_user(to, n);
			n = copy_mc_generic((void *)to, from, n);
			prevent_write_to_user(to, n);
		}
	}

	return n;
}
#endif

#ifdef __powerpc64__
static inline unsigned long
raw_copy_in_user(void __user *to, const void __user *from, unsigned long n)
{
	unsigned long ret;

	barrier_nospec();
	allow_read_write_user(to, from, n);
	ret = __copy_tofrom_user(to, from, n);
	prevent_read_write_user(to, from, n);
	return ret;
}
#endif /* __powerpc64__ */

static inline unsigned long raw_copy_from_user(void *to,
		const void __user *from, unsigned long n)
{
	unsigned long ret;
	if (__builtin_constant_p(n) && (n <= 8)) {
		ret = 1;

		switch (n) {
		case 1:
			barrier_nospec();
			__get_user_size(*(u8 *)to, from, 1, ret);
			break;
		case 2:
			barrier_nospec();
			__get_user_size(*(u16 *)to, from, 2, ret);
			break;
		case 4:
			barrier_nospec();
			__get_user_size(*(u32 *)to, from, 4, ret);
			break;
		case 8:
			barrier_nospec();
			__get_user_size(*(u64 *)to, from, 8, ret);
			break;
		}
		if (ret == 0)
			return 0;
	}

	barrier_nospec();
	allow_read_from_user(from, n);
	ret = __copy_tofrom_user((__force void __user *)to, from, n);
	prevent_read_from_user(from, n);
	return ret;
}

static inline unsigned long
raw_copy_to_user_allowed(void __user *to, const void *from, unsigned long n)
{
	if (__builtin_constant_p(n) && (n <= 8)) {
		unsigned long ret = 1;

		switch (n) {
		case 1:
			__put_user_size_allowed(*(u8 *)from, (u8 __user *)to, 1, ret);
			break;
		case 2:
			__put_user_size_allowed(*(u16 *)from, (u16 __user *)to, 2, ret);
			break;
		case 4:
			__put_user_size_allowed(*(u32 *)from, (u32 __user *)to, 4, ret);
			break;
		case 8:
			__put_user_size_allowed(*(u64 *)from, (u64 __user *)to, 8, ret);
			break;
		}
		if (ret == 0)
			return 0;
	}

	return __copy_tofrom_user(to, (__force const void __user *)from, n);
}

static inline unsigned long
raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	unsigned long ret;

	allow_write_to_user(to, n);
	ret = raw_copy_to_user_allowed(to, from, n);
	prevent_write_to_user(to, n);
	return ret;
}

unsigned long __arch_clear_user(void __user *addr, unsigned long size);

static inline unsigned long clear_user(void __user *addr, unsigned long size)
{
	unsigned long ret = size;
	might_fault();
	if (likely(access_ok(addr, size))) {
		allow_write_to_user(addr, size);
		ret = __arch_clear_user(addr, size);
		prevent_write_to_user(addr, size);
	}
	return ret;
}

static inline unsigned long __clear_user(void __user *addr, unsigned long size)
{
	return clear_user(addr, size);
}

extern long strncpy_from_user(char *dst, const char __user *src, long count);
extern __must_check long strnlen_user(const char __user *str, long n);

extern long __copy_from_user_flushcache(void *dst, const void __user *src,
		unsigned size);
extern void memcpy_page_flushcache(char *to, struct page *page, size_t offset,
			   size_t len);

static __must_check inline bool user_access_begin(const void __user *ptr, size_t len)
{
	if (unlikely(!access_ok(ptr, len)))
		return false;

	might_fault();

	allow_read_write_user((void __user *)ptr, ptr, len);
	return true;
}
#define user_access_begin	user_access_begin
#define user_access_end		prevent_current_access_user
#define user_access_save	prevent_user_access_return
#define user_access_restore	restore_user_access

static __must_check inline bool
user_read_access_begin(const void __user *ptr, size_t len)
{
	if (unlikely(!access_ok(ptr, len)))
		return false;

	might_fault();

	allow_read_from_user(ptr, len);
	return true;
}
#define user_read_access_begin	user_read_access_begin
#define user_read_access_end		prevent_current_read_from_user

static __must_check inline bool
user_write_access_begin(const void __user *ptr, size_t len)
{
	if (unlikely(!access_ok(ptr, len)))
		return false;

	might_fault();

	allow_write_to_user((void __user *)ptr, len);
	return true;
}
#define user_write_access_begin	user_write_access_begin
#define user_write_access_end		prevent_current_write_to_user

#define unsafe_op_wrap(op, err) do { if (unlikely(op)) goto err; } while (0)
#define unsafe_get_user(x, p, e) unsafe_op_wrap(__get_user_allowed(x, p), e)
#define unsafe_put_user(x, p, e) __put_user_goto(x, p, e)

#define unsafe_copy_to_user(d, s, l, e) \
do {									\
	u8 __user *_dst = (u8 __user *)(d);				\
	const u8 *_src = (const u8 *)(s);				\
	size_t _len = (l);						\
	int _i;								\
									\
	for (_i = 0; _i < (_len & ~(sizeof(long) - 1)); _i += sizeof(long))		\
		__put_user_goto(*(long*)(_src + _i), (long __user *)(_dst + _i), e);\
	if (IS_ENABLED(CONFIG_PPC64) && (_len & 4)) {			\
		__put_user_goto(*(u32*)(_src + _i), (u32 __user *)(_dst + _i), e);	\
		_i += 4;						\
	}								\
	if (_len & 2) {							\
		__put_user_goto(*(u16*)(_src + _i), (u16 __user *)(_dst + _i), e);	\
		_i += 2;						\
	}								\
	if (_len & 1) \
		__put_user_goto(*(u8*)(_src + _i), (u8 __user *)(_dst + _i), e);\
} while (0)

#define HAVE_GET_KERNEL_NOFAULT

#define __get_kernel_nofault(dst, src, type, err_label)			\
do {									\
	int __kr_err;							\
									\
	__get_user_size_allowed(*((type *)(dst)), (__force type __user *)(src),\
			sizeof(type), __kr_err);			\
	if (unlikely(__kr_err))						\
		goto err_label;						\
} while (0)

#define __put_kernel_nofault(dst, src, type, err_label)			\
	__put_user_size_goto(*((type *)(src)),				\
		(__force type __user *)(dst), sizeof(type), err_label)

#endif	/* _ARCH_POWERPC_UACCESS_H */
