/*
 * Based on arch/arm/include/asm/uaccess.h
 *
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_UACCESS_H
#define __ASM_UACCESS_H

#include <asm/alternative.h>
#include <asm/kernel-pgtable.h>
#include <asm/mmu.h>
#include <asm/sysreg.h>

#ifndef __ASSEMBLY__

/*
 * User space memory access functions
 */
#include <linux/bitops.h>
#include <linux/string.h>
#include <linux/thread_info.h>

#include <asm/cpufeature.h>
#include <asm/ptrace.h>
#include <asm/errno.h>
#include <asm/memory.h>
#include <asm/compiler.h>

#define VERIFY_READ 0
#define VERIFY_WRITE 1

/*
 * The exception table consists of pairs of relative offsets: the first
 * is the relative offset to an instruction that is allowed to fault,
 * and the second is the relative offset at which the program should
 * continue. No registers are modified, so it is entirely up to the
 * continuation code to figure out what to do.
 *
 * All the routines below use bits of fixup code that are out of line
 * with the main instruction path.  This means when everything is well,
 * we don't even have to jump over them.  Further, they do not intrude
 * on our cache or tlb entries.
 */

struct exception_table_entry
{
	int insn, fixup;
};

#define ARCH_HAS_RELATIVE_EXTABLE

extern int fixup_exception(struct pt_regs *regs);

#define KERNEL_DS	(-1UL)
#define get_ds()	(KERNEL_DS)

#define USER_DS		TASK_SIZE_64
#define get_fs()	(current_thread_info()->addr_limit)

static inline void set_fs(mm_segment_t fs)
{
	current_thread_info()->addr_limit = fs;

	/*
	 * Enable/disable UAO so that copy_to_user() etc can access
	 * kernel memory with the unprivileged instructions.
	 */
	if (IS_ENABLED(CONFIG_ARM64_UAO) && fs == KERNEL_DS)
		asm(ALTERNATIVE("nop", SET_PSTATE_UAO(1), ARM64_HAS_UAO));
	else
		asm(ALTERNATIVE("nop", SET_PSTATE_UAO(0), ARM64_HAS_UAO,
				CONFIG_ARM64_UAO));
}

#define segment_eq(a, b)	((a) == (b))

/*
 * Return 1 if addr < current->addr_limit, 0 otherwise.
 */
#define __addr_ok(addr)							\
({									\
	unsigned long flag;						\
	asm("cmp %1, %0; cset %0, lo"					\
		: "=&r" (flag)						\
		: "r" (addr), "0" (current_thread_info()->addr_limit)	\
		: "cc");						\
	flag;								\
})

/*
 * Test whether a block of memory is a valid user space address.
 * Returns 1 if the range is valid, 0 otherwise.
 *
 * This is equivalent to the following test:
 * (u65)addr + (u65)size <= current->addr_limit
 *
 * This needs 65-bit arithmetic.
 */
#define __range_ok(addr, size)						\
({									\
	unsigned long __addr = (unsigned long __force)(addr);		\
	unsigned long flag, roksum;					\
	__chk_user_ptr(addr);						\
	asm("adds %1, %1, %3; ccmp %1, %4, #2, cc; cset %0, ls"		\
		: "=&r" (flag), "=&r" (roksum)				\
		: "1" (__addr), "Ir" (size),				\
		  "r" (current_thread_info()->addr_limit)		\
		: "cc");						\
	flag;								\
})

/*
 * When dealing with data aborts, watchpoints, or instruction traps we may end
 * up with a tagged userland pointer. Clear the tag to get a sane pointer to
 * pass on to access_ok(), for instance.
 */
#define untagged_addr(addr)		sign_extend64(addr, 55)

#define access_ok(type, addr, size)	__range_ok(addr, size)
#define user_addr_max			get_fs

#define _ASM_EXTABLE(from, to)						\
	"	.pushsection	__ex_table, \"a\"\n"			\
	"	.align		3\n"					\
	"	.long		(" #from " - .), (" #to " - .)\n"	\
	"	.popsection\n"

/*
 * User access enabling/disabling.
 */
#ifdef CONFIG_ARM64_SW_TTBR0_PAN
static inline void __uaccess_ttbr0_disable(void)
{
	unsigned long ttbr;

	ttbr = read_sysreg(ttbr1_el1);
	/* reserved_ttbr0 placed at the end of swapper_pg_dir */
	write_sysreg(ttbr + SWAPPER_DIR_SIZE, ttbr0_el1);
	isb();
	/* Set reserved ASID */
	ttbr &= ~TTBR_ASID_MASK;
	write_sysreg(ttbr, ttbr1_el1);
	isb();
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
	ttbr0 = current_thread_info()->ttbr0;

	/* Restore active ASID */
	ttbr1 = read_sysreg(ttbr1_el1);
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

#define __uaccess_disable(alt)						\
do {									\
	if (!uaccess_ttbr0_disable())					\
		asm(ALTERNATIVE("nop", SET_PSTATE_PAN(1), alt,		\
				CONFIG_ARM64_PAN));			\
} while (0)

#define __uaccess_enable(alt)						\
do {									\
	if (!uaccess_ttbr0_enable())					\
		asm(ALTERNATIVE("nop", SET_PSTATE_PAN(0), alt,		\
				CONFIG_ARM64_PAN));			\
} while (0)

static inline void uaccess_disable(void)
{
	__uaccess_disable(ARM64_HAS_PAN);
}

static inline void uaccess_enable(void)
{
	__uaccess_enable(ARM64_HAS_PAN);
}

/*
 * These functions are no-ops when UAO is present.
 */
static inline void uaccess_disable_not_uao(void)
{
	__uaccess_disable(ARM64_ALT_PAN_NOT_UAO);
}

static inline void uaccess_enable_not_uao(void)
{
	__uaccess_enable(ARM64_ALT_PAN_NOT_UAO);
}

/*
 * The "__xxx" versions of the user access functions do not verify the address
 * space - it must have been done previously with a separate "access_ok()"
 * call.
 *
 * The "__xxx_error" versions set the third argument to -EFAULT if an error
 * occurs, and leave it unchanged on success.
 */
#define __get_user_asm(instr, alt_instr, reg, x, addr, err, feature)	\
	asm volatile(							\
	"1:"ALTERNATIVE(instr "     " reg "1, [%2]\n",			\
			alt_instr " " reg "1, [%2]\n", feature)		\
	"2:\n"								\
	"	.section .fixup, \"ax\"\n"				\
	"	.align	2\n"						\
	"3:	mov	%w0, %3\n"					\
	"	mov	%1, #0\n"					\
	"	b	2b\n"						\
	"	.previous\n"						\
	_ASM_EXTABLE(1b, 3b)						\
	: "+r" (err), "=&r" (x)						\
	: "r" (addr), "i" (-EFAULT))

#define __get_user_err(x, ptr, err)					\
do {									\
	unsigned long __gu_val;						\
	__chk_user_ptr(ptr);						\
	uaccess_enable_not_uao();					\
	switch (sizeof(*(ptr))) {					\
	case 1:								\
		__get_user_asm("ldrb", "ldtrb", "%w", __gu_val, (ptr),  \
			       (err), ARM64_HAS_UAO);			\
		break;							\
	case 2:								\
		__get_user_asm("ldrh", "ldtrh", "%w", __gu_val, (ptr),  \
			       (err), ARM64_HAS_UAO);			\
		break;							\
	case 4:								\
		__get_user_asm("ldr", "ldtr", "%w", __gu_val, (ptr),	\
			       (err), ARM64_HAS_UAO);			\
		break;							\
	case 8:								\
		__get_user_asm("ldr", "ldtr", "%",  __gu_val, (ptr),	\
			       (err), ARM64_HAS_UAO);			\
		break;							\
	default:							\
		BUILD_BUG();						\
	}								\
	uaccess_disable_not_uao();					\
	(x) = (__force __typeof__(*(ptr)))__gu_val;			\
} while (0)

#define __get_user(x, ptr)						\
({									\
	int __gu_err = 0;						\
	__get_user_err((x), (ptr), __gu_err);				\
	__gu_err;							\
})

#define __get_user_error(x, ptr, err)					\
({									\
	__get_user_err((x), (ptr), (err));				\
	(void)0;							\
})

#define __get_user_unaligned __get_user

#define get_user(x, ptr)						\
({									\
	__typeof__(*(ptr)) __user *__p = (ptr);				\
	might_fault();							\
	access_ok(VERIFY_READ, __p, sizeof(*__p)) ?			\
		__get_user((x), __p) :					\
		((x) = 0, -EFAULT);					\
})

#define __put_user_asm(instr, alt_instr, reg, x, addr, err, feature)	\
	asm volatile(							\
	"1:"ALTERNATIVE(instr "     " reg "1, [%2]\n",			\
			alt_instr " " reg "1, [%2]\n", feature)		\
	"2:\n"								\
	"	.section .fixup,\"ax\"\n"				\
	"	.align	2\n"						\
	"3:	mov	%w0, %3\n"					\
	"	b	2b\n"						\
	"	.previous\n"						\
	_ASM_EXTABLE(1b, 3b)						\
	: "+r" (err)							\
	: "r" (x), "r" (addr), "i" (-EFAULT))

#define __put_user_err(x, ptr, err)					\
do {									\
	__typeof__(*(ptr)) __pu_val = (x);				\
	__chk_user_ptr(ptr);						\
	uaccess_enable_not_uao();					\
	switch (sizeof(*(ptr))) {					\
	case 1:								\
		__put_user_asm("strb", "sttrb", "%w", __pu_val, (ptr),	\
			       (err), ARM64_HAS_UAO);			\
		break;							\
	case 2:								\
		__put_user_asm("strh", "sttrh", "%w", __pu_val, (ptr),	\
			       (err), ARM64_HAS_UAO);			\
		break;							\
	case 4:								\
		__put_user_asm("str", "sttr", "%w", __pu_val, (ptr),	\
			       (err), ARM64_HAS_UAO);			\
		break;							\
	case 8:								\
		__put_user_asm("str", "sttr", "%", __pu_val, (ptr),	\
			       (err), ARM64_HAS_UAO);			\
		break;							\
	default:							\
		BUILD_BUG();						\
	}								\
	uaccess_disable_not_uao();					\
} while (0)

#define __put_user(x, ptr)						\
({									\
	int __pu_err = 0;						\
	__put_user_err((x), (ptr), __pu_err);				\
	__pu_err;							\
})

#define __put_user_error(x, ptr, err)					\
({									\
	__put_user_err((x), (ptr), (err));				\
	(void)0;							\
})

#define __put_user_unaligned __put_user

#define put_user(x, ptr)						\
({									\
	__typeof__(*(ptr)) __user *__p = (ptr);				\
	might_fault();							\
	access_ok(VERIFY_WRITE, __p, sizeof(*__p)) ?			\
		__put_user((x), __p) :					\
		-EFAULT;						\
})

extern unsigned long __must_check __arch_copy_from_user(void *to, const void __user *from, unsigned long n);
extern unsigned long __must_check __arch_copy_to_user(void __user *to, const void *from, unsigned long n);
extern unsigned long __must_check __copy_in_user(void __user *to, const void __user *from, unsigned long n);
extern unsigned long __must_check __clear_user(void __user *addr, unsigned long n);

static inline unsigned long __must_check __copy_from_user(void *to, const void __user *from, unsigned long n)
{
	check_object_size(to, n, false);
	return __arch_copy_from_user(to, from, n);
}

static inline unsigned long __must_check __copy_to_user(void __user *to, const void *from, unsigned long n)
{
	check_object_size(from, n, true);
	return __arch_copy_to_user(to, from, n);
}

static inline unsigned long __must_check copy_from_user(void *to, const void __user *from, unsigned long n)
{
	if (access_ok(VERIFY_READ, from, n)) {
		check_object_size(to, n, false);
		n = __arch_copy_from_user(to, from, n);
	} else /* security hole - plug it */
		memset(to, 0, n);
	return n;
}

static inline unsigned long __must_check copy_to_user(void __user *to, const void *from, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n)) {
		check_object_size(from, n, true);
		n = __arch_copy_to_user(to, from, n);
	}
	return n;
}

static inline unsigned long __must_check copy_in_user(void __user *to, const void __user *from, unsigned long n)
{
	if (access_ok(VERIFY_READ, from, n) && access_ok(VERIFY_WRITE, to, n))
		n = __copy_in_user(to, from, n);
	return n;
}

#define __copy_to_user_inatomic __copy_to_user
#define __copy_from_user_inatomic __copy_from_user

static inline unsigned long __must_check clear_user(void __user *to, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		n = __clear_user(to, n);
	return n;
}

extern long strncpy_from_user(char *dest, const char __user *src, long count);

extern __must_check long strlen_user(const char __user *str);
extern __must_check long strnlen_user(const char __user *str, long n);

#else	/* __ASSEMBLY__ */

#include <asm/assembler.h>

/*
 * User access enabling/disabling macros.
 */
#ifdef CONFIG_ARM64_SW_TTBR0_PAN
	.macro	__uaccess_ttbr0_disable, tmp1
	mrs	\tmp1, ttbr1_el1		// swapper_pg_dir
	add	\tmp1, \tmp1, #SWAPPER_DIR_SIZE	// reserved_ttbr0 at the end of swapper_pg_dir
	msr	ttbr0_el1, \tmp1		// set reserved TTBR0_EL1
	isb
	sub	\tmp1, \tmp1, #SWAPPER_DIR_SIZE
	bic	\tmp1, \tmp1, #TTBR_ASID_MASK
	msr	ttbr1_el1, \tmp1		// set reserved ASID
	isb
	.endm

	.macro	__uaccess_ttbr0_enable, tmp1, tmp2
	get_thread_info \tmp1
	ldr	\tmp1, [\tmp1, #TSK_TI_TTBR0]	// load saved TTBR0_EL1
	mrs	\tmp2, ttbr1_el1
	extr    \tmp2, \tmp2, \tmp1, #48
	ror     \tmp2, \tmp2, #16
	msr	ttbr1_el1, \tmp2		// set the active ASID
	isb
	msr	ttbr0_el1, \tmp1		// set the non-PAN TTBR0_EL1
	isb
	.endm

	.macro	uaccess_ttbr0_disable, tmp1
alternative_if_not ARM64_HAS_PAN
	__uaccess_ttbr0_disable \tmp1
alternative_else_nop_endif
	.endm

	.macro	uaccess_ttbr0_enable, tmp1, tmp2, tmp3
alternative_if_not ARM64_HAS_PAN
	save_and_disable_irq \tmp3		// avoid preemption
	__uaccess_ttbr0_enable \tmp1, \tmp2
	restore_irq \tmp3
alternative_else_nop_endif
	.endm
#else
	.macro	uaccess_ttbr0_disable, tmp1
	.endm

	.macro	uaccess_ttbr0_enable, tmp1, tmp2, tmp3
	.endm
#endif

/*
 * These macros are no-ops when UAO is present.
 */
	.macro	uaccess_disable_not_uao, tmp1
	uaccess_ttbr0_disable \tmp1
alternative_if ARM64_ALT_PAN_NOT_UAO
	SET_PSTATE_PAN(1)
alternative_else_nop_endif
	.endm

	.macro	uaccess_enable_not_uao, tmp1, tmp2, tmp3
	uaccess_ttbr0_enable \tmp1, \tmp2, \tmp3
alternative_if ARM64_ALT_PAN_NOT_UAO
	SET_PSTATE_PAN(0)
alternative_else_nop_endif
	.endm

#endif	/* __ASSEMBLY__ */

#endif /* __ASM_UACCESS_H */
