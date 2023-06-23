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
	isb();
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
	isb();

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

/*
 * The Tag Check Flag (TCF) mode for MTE is per EL, hence TCF0
 * affects EL0 and TCF affects EL1 irrespective of which TTBR is
 * used.
 * The kernel accesses TTBR0 usually with LDTR/STTR instructions
 * when UAO is available, so these would act as EL0 accesses using
 * TCF0.
 * However futex.h code uses exclusives which would be executed as
 * EL1, this can potentially cause a tag check fault even if the
 * user disables TCF0.
 *
 * To address the problem we set the PSTATE.TCO bit in uaccess_enable()
 * and reset it in uaccess_disable().
 *
 * The Tag check override (TCO) bit disables temporarily the tag checking
 * preventing the issue.
 */
static inline void __uaccess_disable_tco(void)
{
	asm volatile(ALTERNATIVE("nop", SET_PSTATE_TCO(0),
				 ARM64_MTE, CONFIG_KASAN_HW_TAGS));
}

static inline void __uaccess_enable_tco(void)
{
	asm volatile(ALTERNATIVE("nop", SET_PSTATE_TCO(1),
				 ARM64_MTE, CONFIG_KASAN_HW_TAGS));
}

/*
 * These functions disable tag checking only if in MTE async mode
 * since the sync mode generates exceptions synchronously and the
 * nofault or load_unaligned_zeropad can handle them.
 */
static inline void __uaccess_disable_tco_async(void)
{
	if (system_uses_mte_async_or_asymm_mode())
		 __uaccess_disable_tco();
}

static inline void __uaccess_enable_tco_async(void)
{
	if (system_uses_mte_async_or_asymm_mode())
		__uaccess_enable_tco();
}

static inline void uaccess_disable_privileged(void)
{
	__uaccess_disable_tco();

	if (uaccess_ttbr0_disable())
		return;

	__uaccess_enable_hw_pan();
}

static inline void uaccess_enable_privileged(void)
{
	__uaccess_enable_tco();

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
#define __get_mem_asm(load, reg, x, addr, err, type)			\
	asm volatile(							\
	"1:	" load "	" reg "1, [%2]\n"			\
	"2:\n"								\
	_ASM_EXTABLE_##type##ACCESS_ERR_ZERO(1b, 2b, %w0, %w1)		\
	: "+r" (err), "=&r" (x)						\
	: "r" (addr))

#define __raw_get_mem(ldr, x, ptr, err, type)					\
do {										\
	unsigned long __gu_val;							\
	switch (sizeof(*(ptr))) {						\
	case 1:									\
		__get_mem_asm(ldr "b", "%w", __gu_val, (ptr), (err), type);	\
		break;								\
	case 2:									\
		__get_mem_asm(ldr "h", "%w", __gu_val, (ptr), (err), type);	\
		break;								\
	case 4:									\
		__get_mem_asm(ldr, "%w", __gu_val, (ptr), (err), type);		\
		break;								\
	case 8:									\
		__get_mem_asm(ldr, "%x",  __gu_val, (ptr), (err), type);	\
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
#define __raw_get_user(x, ptr, err)					\
do {									\
	__typeof__(*(ptr)) __user *__rgu_ptr = (ptr);			\
	__typeof__(x) __rgu_val;					\
	__chk_user_ptr(ptr);						\
									\
	uaccess_ttbr0_enable();						\
	__raw_get_mem("ldtr", __rgu_val, __rgu_ptr, err, U);		\
	uaccess_ttbr0_disable();					\
									\
	(x) = __rgu_val;						\
} while (0)

#define __get_user_error(x, ptr, err)					\
do {									\
	__typeof__(*(ptr)) __user *__p = (ptr);				\
	might_fault();							\
	if (access_ok(__p, sizeof(*__p))) {				\
		__p = uaccess_mask_ptr(__p);				\
		__raw_get_user((x), __p, (err));			\
	} else {							\
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
 * We must not call into the scheduler between __uaccess_enable_tco_async() and
 * __uaccess_disable_tco_async(). As `dst` and `src` may contain blocking
 * functions, we must evaluate these outside of the critical section.
 */
#define __get_kernel_nofault(dst, src, type, err_label)			\
do {									\
	__typeof__(dst) __gkn_dst = (dst);				\
	__typeof__(src) __gkn_src = (src);				\
	int __gkn_err = 0;						\
									\
	__uaccess_enable_tco_async();					\
	__raw_get_mem("ldr", *((type *)(__gkn_dst)),			\
		      (__force type *)(__gkn_src), __gkn_err, K);	\
	__uaccess_disable_tco_async();					\
									\
	if (unlikely(__gkn_err))					\
		goto err_label;						\
} while (0)

#define __put_mem_asm(store, reg, x, addr, err, type)			\
	asm volatile(							\
	"1:	" store "	" reg "1, [%2]\n"			\
	"2:\n"								\
	_ASM_EXTABLE_##type##ACCESS_ERR(1b, 2b, %w0)			\
	: "+r" (err)							\
	: "r" (x), "r" (addr))

#define __raw_put_mem(str, x, ptr, err, type)					\
do {										\
	__typeof__(*(ptr)) __pu_val = (x);					\
	switch (sizeof(*(ptr))) {						\
	case 1:									\
		__put_mem_asm(str "b", "%w", __pu_val, (ptr), (err), type);	\
		break;								\
	case 2:									\
		__put_mem_asm(str "h", "%w", __pu_val, (ptr), (err), type);	\
		break;								\
	case 4:									\
		__put_mem_asm(str, "%w", __pu_val, (ptr), (err), type);		\
		break;								\
	case 8:									\
		__put_mem_asm(str, "%x", __pu_val, (ptr), (err), type);		\
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
#define __raw_put_user(x, ptr, err)					\
do {									\
	__typeof__(*(ptr)) __user *__rpu_ptr = (ptr);			\
	__typeof__(*(ptr)) __rpu_val = (x);				\
	__chk_user_ptr(__rpu_ptr);					\
									\
	uaccess_ttbr0_enable();						\
	__raw_put_mem("sttr", __rpu_val, __rpu_ptr, err, U);		\
	uaccess_ttbr0_disable();					\
} while (0)

#define __put_user_error(x, ptr, err)					\
do {									\
	__typeof__(*(ptr)) __user *__p = (ptr);				\
	might_fault();							\
	if (access_ok(__p, sizeof(*__p))) {				\
		__p = uaccess_mask_ptr(__p);				\
		__raw_put_user((x), __p, (err));			\
	} else	{							\
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
 * We must not call into the scheduler between __uaccess_enable_tco_async() and
 * __uaccess_disable_tco_async(). As `dst` and `src` may contain blocking
 * functions, we must evaluate these outside of the critical section.
 */
#define __put_kernel_nofault(dst, src, type, err_label)			\
do {									\
	__typeof__(dst) __pkn_dst = (dst);				\
	__typeof__(src) __pkn_src = (src);				\
	int __pkn_err = 0;						\
									\
	__uaccess_enable_tco_async();					\
	__raw_put_mem("str", *((type *)(__pkn_src)),			\
		      (__force type *)(__pkn_dst), __pkn_err, K);	\
	__uaccess_disable_tco_async();					\
									\
	if (unlikely(__pkn_err))					\
		goto err_label;						\
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
struct page;
void memcpy_page_flushcache(char *to, struct page *page, size_t offset, size_t len);
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

#endif /* __ASM_UACCESS_H */
