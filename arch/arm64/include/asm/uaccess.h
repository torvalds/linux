/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/uaccess.h
 *
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_UACCESS_H
#define __ASM_UACCESS_H

#include <asm/alternative.h>
#include <asm/kernel-pgtable.h>
#include <asm/sysreg.h>

/*
 * User space memory access functions
 */
#include <linux/bitops.h>
#include <linux/kasan-checks.h>
#include <linux/string.h>

#include <asm/asm-extable.h>
#include <asm/cpufeature.h>
#include <asm/mmu.h>
#include <asm/mte.h>
#include <asm/ptrace.h>
#include <asm/memory.h>
#include <asm/extable.h>

static inline int __access_ok(const void __user *ptr, unsigned long size);

/*
 * Test whether a block of memory is a valid user space address.
 * Returns 1 if the range is valid, 0 otherwise.
 *
 * This is equivalent to the following test:
 * (u65)addr + (u65)size <= (u65)TASK_SIZE_MAX
 */
static inline int access_ok(const void __user *addr, unsigned long size)
{
	/*
	 * Asynchronous I/O running in a kernel thread does not have the
	 * TIF_TAGGED_ADDR flag of the process owning the mm, so always untag
	 * the user address before checking.
	 */
	if (IS_ENABLED(CONFIG_ARM64_TAGGED_ADDR_ABI) &&
	    (current->flags & PF_KTHREAD || test_thread_flag(TIF_TAGGED_ADDR)))
		addr = untagged_addr(addr);

	return likely(__access_ok(addr, size));
}
#define access_ok access_ok

#include <asm-generic/access_ok.h>

/*
 * User access enabling/disabling.
 */
#ifdef CONFIG_ARM64_SW_TTBR0_PAN
static inline void __uaccess_ttbr0_disable(void)
{
	unsigned long flags, ttbr;

	local_irq_save(flags);
	ttbr = read_sysreg(ttbr1_el1);
	ttbr &= ~TTBR_ASID_MASK;
	/* reserved_pg_dir placed before swapper_pg_dir */
	write_sysreg(ttbr - RESERVED_SWAPPER_OFFSET, ttbr0_el1);
	/* Set reserved ASID */
	write_sysreg(ttbr, ttbr1_el1);
	isb();
	local_irq_restore(flags);
}

static inline void __uaccess_ttbr0_enable(void)
{
	unsigned long flags, ttbr0, ttbr1;

	/*
	 * Disable interrupts to avoid preemption between reading the 'ttbr0'
	 * variable and the MSR. A context switch could trigger an ASID
	 * roll-over and an update of 'ttbr0'.
	 */
	local_irq_save(flags);
	ttbr0 = READ_ONCE(current_thread_info()->ttbr0);

	/* Restore active ASID */
	ttbr1 = read_sysreg(ttbr1_el1);
	ttbr1 &= ~TTBR_ASID_MASK;		/* safety measure */
	ttbr1 |= ttbr0 & TTBR_ASID_MASK;
	write_sysreg(ttbr1, ttbr1_el1);

	/* Restore user page table */
	write_sysreg(ttbr0, ttbr0_el1);
	isb();
	local_irq_restore(flags);
}

static inline bool uaccess_ttbr0_disable(void)
{
	if (!system_uses_ttbr0_pan())
		return false;
	__uaccess_ttbr0_disable();
	return true;
}

static inline bool uaccess_ttbr0_enable(void)
{
	if (!system_uses_ttbr0_pan())
		return false;
	__uaccess_ttbr0_enable();
	return true;
}
#else
static inline bool uaccess_ttbr0_disable(void)
{
	return false;
}

static inline bool uaccess_ttbr0_enable(void)
{
	return false;
}
#endif

static inline void __uaccess_disable_hw_pan(void)
{
	asm(ALTERNATIVE("nop", SET_PSTATE_PAN(0), ARM64_HAS_PAN,
			CONFIG_ARM64_PAN));
}

static inline void __uaccess_enable_hw_pan(void)
{
	asm(ALTERNATIVE("nop", SET_PSTATE_PAN(1), ARM64_HAS_PAN,
			CONFIG_ARM64_PAN));
}

static inline void uaccess_disable_privileged(void)
{
	mte_disable_tco();

	if (uaccess_ttbr0_disable())
		return;

	__uaccess_enable_hw_pan();
}

static inline void uaccess_enable_privileged(void)
{
	mte_enable_tco();

	if (uaccess_ttbr0_enable())
		return;

	__uaccess_disable_hw_pan();
}

/*
 * Sanitize a uaccess pointer such that it cannot reach any kernel address.
 *
 * Clearing bit 55 ensures the pointer cannot address any portion of the TTBR1
 * address range (i.e. any kernel address), and either the pointer falls within
 * the TTBR0 address range or must cause a fault.
 */
#define uaccess_mask_ptr(ptr) (__typeof__(ptr))__uaccess_mask_ptr(ptr)
static inline void __user *__uaccess_mask_ptr(const void __user *ptr)
{
	void __user *safe_ptr;

	asm volatile(
	"	bic	%0, %1, %2\n"
	: "=r" (safe_ptr)
	: "r" (ptr),
	  "i" (BIT(55))
	);

	return safe_ptr;
}

/*
 * The "__xxx" versions of the user access functions do not verify the address
 * space - it must have been done previously with a separate "access_ok()"
 * call.
 *
 * The "__xxx_error" versions set the third argument to -EFAULT if an error
 * occurs, and leave it unchanged on success.
 */
#ifdef CONFIG_CC_HAS_ASM_GOTO_OUTPUT
#define __get_mem_asm(load, reg, x, addr, label, type)			\
	asm_goto_output(						\
	"1:	" load "	" reg "0, [%1]\n"			\
	_ASM_EXTABLE_##type##ACCESS(1b, %l2)				\
	: "=r" (x)							\
	: "r" (addr) : : label)
#else
#define __get_mem_asm(load, reg, x, addr, label, type) do {		\
	int __gma_err = 0;						\
	asm volatile(							\
	"1:	" load "	" reg "1, [%2]\n"			\
	"2:\n"								\
	_ASM_EXTABLE_##type##ACCESS_ERR_ZERO(1b, 2b, %w0, %w1)		\
	: "+r" (__gma_err), "=r" (x)					\
	: "r" (addr));							\
	if (__gma_err) goto label; } while (0)
#endif

#define __raw_get_mem(ldr, x, ptr, label, type)					\
do {										\
	unsigned long __gu_val;							\
	switch (sizeof(*(ptr))) {						\
	case 1:									\
		__get_mem_asm(ldr "b", "%w", __gu_val, (ptr), label, type);	\
		break;								\
	case 2:									\
		__get_mem_asm(ldr "h", "%w", __gu_val, (ptr), label, type);	\
		break;								\
	case 4:									\
		__get_mem_asm(ldr, "%w", __gu_val, (ptr), label, type);		\
		break;								\
	case 8:									\
		__get_mem_asm(ldr, "%x",  __gu_val, (ptr), label, type);	\
		break;								\
	default:								\
		BUILD_BUG();							\
	}									\
	(x) = (__force __typeof__(*(ptr)))__gu_val;				\
} while (0)

/*
 * We must not call into the scheduler between uaccess_ttbr0_enable() and
 * uaccess_ttbr0_disable(). As `x` and `ptr` could contain blocking functions,
 * we must evaluate these outside of the critical section.
 */
#define __raw_get_user(x, ptr, label)					\
do {									\
	__typeof__(*(ptr)) __user *__rgu_ptr = (ptr);			\
	__typeof__(x) __rgu_val;					\
	__chk_user_ptr(ptr);						\
	do {								\
		__label__ __rgu_failed;					\
		uaccess_ttbr0_enable();					\
		__raw_get_mem("ldtr", __rgu_val, __rgu_ptr, __rgu_failed, U);	\
		uaccess_ttbr0_disable();				\
		(x) = __rgu_val;					\
		break;							\
	__rgu_failed:							\
		uaccess_ttbr0_disable();				\
		goto label;						\
	} while (0);							\
} while (0)

#define __get_user_error(x, ptr, err)					\
do {									\
	__label__ __gu_failed;						\
	__typeof__(*(ptr)) __user *__p = (ptr);				\
	might_fault();							\
	if (access_ok(__p, sizeof(*__p))) {				\
		__p = uaccess_mask_ptr(__p);				\
		__raw_get_user((x), __p, __gu_failed);			\
	} else {							\
	__gu_failed:							\
		(x) = (__force __typeof__(x))0; (err) = -EFAULT;	\
	}								\
} while (0)

#define __get_user(x, ptr)						\
({									\
	int __gu_err = 0;						\
	__get_user_error((x), (ptr), __gu_err);				\
	__gu_err;							\
})

#define get_user	__get_user

/*
 * We must not call into the scheduler between __mte_enable_tco_async() and
 * __mte_disable_tco_async(). As `dst` and `src` may contain blocking
 * functions, we must evaluate these outside of the critical section.
 */
#define __get_kernel_nofault(dst, src, type, err_label)			\
do {									\
	__typeof__(dst) __gkn_dst = (dst);				\
	__typeof__(src) __gkn_src = (src);				\
	do { 								\
		__label__ __gkn_label;					\
									\
		__mte_enable_tco_async();				\
		__raw_get_mem("ldr", *((type *)(__gkn_dst)),		\
		      (__force type *)(__gkn_src), __gkn_label, K);	\
		__mte_disable_tco_async();				\
		break;							\
	__gkn_label:							\
		__mte_disable_tco_async();				\
		goto err_label;						\
	} while (0);							\
} while (0)

#define __put_mem_asm(store, reg, x, addr, label, type)			\
	asm goto(							\
	"1:	" store "	" reg "0, [%1]\n"			\
	"2:\n"								\
	_ASM_EXTABLE_##type##ACCESS(1b, %l2)				\
	: : "rZ" (x), "r" (addr) : : label)

#define __raw_put_mem(str, x, ptr, label, type)					\
do {										\
	__typeof__(*(ptr)) __pu_val = (x);					\
	switch (sizeof(*(ptr))) {						\
	case 1:									\
		__put_mem_asm(str "b", "%w", __pu_val, (ptr), label, type);	\
		break;								\
	case 2:									\
		__put_mem_asm(str "h", "%w", __pu_val, (ptr), label, type);	\
		break;								\
	case 4:									\
		__put_mem_asm(str, "%w", __pu_val, (ptr), label, type);		\
		break;								\
	case 8:									\
		__put_mem_asm(str, "%x", __pu_val, (ptr), label, type);		\
		break;								\
	default:								\
		BUILD_BUG();							\
	}									\
} while (0)

/*
 * We must not call into the scheduler between uaccess_ttbr0_enable() and
 * uaccess_ttbr0_disable(). As `x` and `ptr` could contain blocking functions,
 * we must evaluate these outside of the critical section.
 */
#define __raw_put_user(x, ptr, label)					\
do {									\
	__label__ __rpu_failed;						\
	__typeof__(*(ptr)) __user *__rpu_ptr = (ptr);			\
	__typeof__(*(ptr)) __rpu_val = (x);				\
	__chk_user_ptr(__rpu_ptr);					\
									\
	do {								\
		uaccess_ttbr0_enable();					\
		__raw_put_mem("sttr", __rpu_val, __rpu_ptr, __rpu_failed, U);	\
		uaccess_ttbr0_disable();				\
		break;							\
	__rpu_failed:							\
		uaccess_ttbr0_disable();				\
		goto label;						\
	} while (0);							\
} while (0)

#define __put_user_error(x, ptr, err)					\
do {									\
	__label__ __pu_failed;						\
	__typeof__(*(ptr)) __user *__p = (ptr);				\
	might_fault();							\
	if (access_ok(__p, sizeof(*__p))) {				\
		__p = uaccess_mask_ptr(__p);				\
		__raw_put_user((x), __p, __pu_failed);			\
	} else	{							\
	__pu_failed:							\
		(err) = -EFAULT;					\
	}								\
} while (0)

#define __put_user(x, ptr)						\
({									\
	int __pu_err = 0;						\
	__put_user_error((x), (ptr), __pu_err);				\
	__pu_err;							\
})

#define put_user	__put_user

/*
 * We must not call into the scheduler between __mte_enable_tco_async() and
 * __mte_disable_tco_async(). As `dst` and `src` may contain blocking
 * functions, we must evaluate these outside of the critical section.
 */
#define __put_kernel_nofault(dst, src, type, err_label)			\
do {									\
	__typeof__(dst) __pkn_dst = (dst);				\
	__typeof__(src) __pkn_src = (src);				\
									\
	do {								\
		__label__ __pkn_err;					\
		__mte_enable_tco_async();				\
		__raw_put_mem("str", *((type *)(__pkn_src)),		\
			      (__force type *)(__pkn_dst), __pkn_err, K);	\
		__mte_disable_tco_async();				\
		break;							\
	__pkn_err:							\
		__mte_disable_tco_async();				\
		goto err_label;						\
	} while (0);							\
} while(0)

extern unsigned long __must_check __arch_copy_from_user(void *to, const void __user *from, unsigned long n);
#define raw_copy_from_user(to, from, n)					\
({									\
	unsigned long __acfu_ret;					\
	uaccess_ttbr0_enable();						\
	__acfu_ret = __arch_copy_from_user((to),			\
				      __uaccess_mask_ptr(from), (n));	\
	uaccess_ttbr0_disable();					\
	__acfu_ret;							\
})

extern unsigned long __must_check __arch_copy_to_user(void __user *to, const void *from, unsigned long n);
#define raw_copy_to_user(to, from, n)					\
({									\
	unsigned long __actu_ret;					\
	uaccess_ttbr0_enable();						\
	__actu_ret = __arch_copy_to_user(__uaccess_mask_ptr(to),	\
				    (from), (n));			\
	uaccess_ttbr0_disable();					\
	__actu_ret;							\
})

static __must_check __always_inline bool user_access_begin(const void __user *ptr, size_t len)
{
	if (unlikely(!access_ok(ptr,len)))
		return 0;
	uaccess_ttbr0_enable();
	return 1;
}
#define user_access_begin(a,b)	user_access_begin(a,b)
#define user_access_end()	uaccess_ttbr0_disable()
#define unsafe_put_user(x, ptr, label) \
	__raw_put_mem("sttr", x, uaccess_mask_ptr(ptr), label, U)
#define unsafe_get_user(x, ptr, label) \
	__raw_get_mem("ldtr", x, uaccess_mask_ptr(ptr), label, U)

/*
 * KCSAN uses these to save and restore ttbr state.
 * We do not support KCSAN with ARM64_SW_TTBR0_PAN, so
 * they are no-ops.
 */
static inline unsigned long user_access_save(void) { return 0; }
static inline void user_access_restore(unsigned long enabled) { }

/*
 * We want the unsafe accessors to always be inlined and use
 * the error labels - thus the macro games.
 */
#define unsafe_copy_loop(dst, src, len, type, label)				\
	while (len >= sizeof(type)) {						\
		unsafe_put_user(*(type *)(src),(type __user *)(dst),label);	\
		dst += sizeof(type);						\
		src += sizeof(type);						\
		len -= sizeof(type);						\
	}

#define unsafe_copy_to_user(_dst,_src,_len,label)			\
do {									\
	char __user *__ucu_dst = (_dst);				\
	const char *__ucu_src = (_src);					\
	size_t __ucu_len = (_len);					\
	unsafe_copy_loop(__ucu_dst, __ucu_src, __ucu_len, u64, label);	\
	unsafe_copy_loop(__ucu_dst, __ucu_src, __ucu_len, u32, label);	\
	unsafe_copy_loop(__ucu_dst, __ucu_src, __ucu_len, u16, label);	\
	unsafe_copy_loop(__ucu_dst, __ucu_src, __ucu_len, u8, label);	\
} while (0)

#define INLINE_COPY_TO_USER
#define INLINE_COPY_FROM_USER

extern unsigned long __must_check __arch_clear_user(void __user *to, unsigned long n);
static inline unsigned long __must_check __clear_user(void __user *to, unsigned long n)
{
	if (access_ok(to, n)) {
		uaccess_ttbr0_enable();
		n = __arch_clear_user(__uaccess_mask_ptr(to), n);
		uaccess_ttbr0_disable();
	}
	return n;
}
#define clear_user	__clear_user

extern long strncpy_from_user(char *dest, const char __user *src, long count);

extern __must_check long strnlen_user(const char __user *str, long n);

#ifdef CONFIG_ARCH_HAS_UACCESS_FLUSHCACHE
extern unsigned long __must_check __copy_user_flushcache(void *to, const void __user *from, unsigned long n);

static inline int __copy_from_user_flushcache(void *dst, const void __user *src, unsigned size)
{
	kasan_check_write(dst, size);
	return __copy_user_flushcache(dst, __uaccess_mask_ptr(src), size);
}
#endif

#ifdef CONFIG_ARCH_HAS_SUBPAGE_FAULTS

/*
 * Return 0 on success, the number of bytes not probed otherwise.
 */
static inline size_t probe_subpage_writeable(const char __user *uaddr,
					     size_t size)
{
	if (!system_supports_mte())
		return 0;
	return mte_probe_user_range(uaddr, size);
}

#endif /* CONFIG_ARCH_HAS_SUBPAGE_FAULTS */

#ifdef CONFIG_ARM64_GCS

static inline int gcssttr(unsigned long __user *addr, unsigned long val)
{
	register unsigned long __user *_addr __asm__ ("x0") = addr;
	register unsigned long _val __asm__ ("x1") = val;
	int err = 0;

	/* GCSSTTR x1, x0 */
	asm volatile(
		"1: .inst 0xd91f1c01\n"
		"2: \n"
		_ASM_EXTABLE_UACCESS_ERR(1b, 2b, %w0)
		: "+r" (err)
		: "rZ" (_val), "r" (_addr)
		: "memory");

	return err;
}

static inline void put_user_gcs(unsigned long val, unsigned long __user *addr,
				int *err)
{
	int ret;

	if (!access_ok((char __user *)addr, sizeof(u64))) {
		*err = -EFAULT;
		return;
	}

	uaccess_ttbr0_enable();
	ret = gcssttr(addr, val);
	if (ret != 0)
		*err = ret;
	uaccess_ttbr0_disable();
}


#endif /* CONFIG_ARM64_GCS */

#endif /* __ASM_UACCESS_H */
