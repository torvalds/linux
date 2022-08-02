/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PARISC_UACCESS_H
#define __PARISC_UACCESS_H

/*
 * User space memory access functions
 */
#include <asm/page.h>
#include <asm/cache.h>

#include <linux/bug.h>
#include <linux/string.h>

#define TASK_SIZE_MAX DEFAULT_TASK_SIZE
#include <asm/pgtable.h>
#include <asm-generic/access_ok.h>

#define put_user __put_user
#define get_user __get_user

#if !defined(CONFIG_64BIT)
#define LDD_USER(sr, val, ptr)	__get_user_asm64(sr, val, ptr)
#define STD_USER(sr, x, ptr)	__put_user_asm64(sr, x, ptr)
#else
#define LDD_USER(sr, val, ptr)	__get_user_asm(sr, val, "ldd", ptr)
#define STD_USER(sr, x, ptr)	__put_user_asm(sr, "std", x, ptr)
#endif

/*
 * The exception table contains two values: the first is the relative offset to
 * the address of the instruction that is allowed to fault, and the second is
 * the relative offset to the address of the fixup routine. Since relative
 * addresses are used, 32bit values are sufficient even on 64bit kernel.
 */

#define ARCH_HAS_RELATIVE_EXTABLE
struct exception_table_entry {
	int insn;	/* relative address of insn that is allowed to fault. */
	int fixup;	/* relative address of fixup routine */
};

#define ASM_EXCEPTIONTABLE_ENTRY( fault_addr, except_addr )\
	".section __ex_table,\"aw\"\n"			   \
	".word (" #fault_addr " - .), (" #except_addr " - .)\n\t" \
	".previous\n"

/*
 * ASM_EXCEPTIONTABLE_ENTRY_EFAULT() creates a special exception table entry
 * (with lowest bit set) for which the fault handler in fixup_exception() will
 * load -EFAULT into %r29 for a read or write fault, and zeroes the target
 * register in case of a read fault in get_user().
 */
#define ASM_EXCEPTIONTABLE_REG	29
#define ASM_EXCEPTIONTABLE_VAR(__variable)		\
	register long __variable __asm__ ("r29") = 0
#define ASM_EXCEPTIONTABLE_ENTRY_EFAULT( fault_addr, except_addr )\
	ASM_EXCEPTIONTABLE_ENTRY( fault_addr, except_addr + 1)

#define __get_user_internal(sr, val, ptr)		\
({							\
	ASM_EXCEPTIONTABLE_VAR(__gu_err);		\
							\
	switch (sizeof(*(ptr))) {			\
	case 1: __get_user_asm(sr, val, "ldb", ptr); break; \
	case 2: __get_user_asm(sr, val, "ldh", ptr); break; \
	case 4: __get_user_asm(sr, val, "ldw", ptr); break; \
	case 8: LDD_USER(sr, val, ptr); break;		\
	default: BUILD_BUG();				\
	}						\
							\
	__gu_err;					\
})

#define __get_user(val, ptr)				\
({							\
	__get_user_internal(SR_USER, val, ptr);	\
})

#define __get_user_asm(sr, val, ldx, ptr)		\
{							\
	register long __gu_val;				\
							\
	__asm__("1: " ldx " 0(%%sr%2,%3),%0\n"		\
		"9:\n"					\
		ASM_EXCEPTIONTABLE_ENTRY_EFAULT(1b, 9b)	\
		: "=r"(__gu_val), "+r"(__gu_err)        \
		: "i"(sr), "r"(ptr));			\
							\
	(val) = (__force __typeof__(*(ptr))) __gu_val;	\
}

#define __get_kernel_nofault(dst, src, type, err_label)	\
{							\
	type __z;					\
	long __err;					\
	__err = __get_user_internal(SR_KERNEL, __z, (type *)(src)); \
	if (unlikely(__err))				\
		goto err_label;				\
	else						\
		*(type *)(dst) = __z;			\
}


#if !defined(CONFIG_64BIT)

#define __get_user_asm64(sr, val, ptr)			\
{							\
	union {						\
		unsigned long long	l;		\
		__typeof__(*(ptr))	t;		\
	} __gu_tmp;					\
							\
	__asm__("   copy %%r0,%R0\n"			\
		"1: ldw 0(%%sr%2,%3),%0\n"		\
		"2: ldw 4(%%sr%2,%3),%R0\n"		\
		"9:\n"					\
		ASM_EXCEPTIONTABLE_ENTRY_EFAULT(1b, 9b)	\
		ASM_EXCEPTIONTABLE_ENTRY_EFAULT(2b, 9b)	\
		: "=&r"(__gu_tmp.l), "+r"(__gu_err)	\
		: "i"(sr), "r"(ptr));			\
							\
	(val) = __gu_tmp.t;				\
}

#endif /* !defined(CONFIG_64BIT) */


#define __put_user_internal(sr, x, ptr)				\
({								\
	ASM_EXCEPTIONTABLE_VAR(__pu_err);		      	\
								\
	switch (sizeof(*(ptr))) {				\
	case 1: __put_user_asm(sr, "stb", x, ptr); break;	\
	case 2: __put_user_asm(sr, "sth", x, ptr); break;	\
	case 4: __put_user_asm(sr, "stw", x, ptr); break;	\
	case 8: STD_USER(sr, x, ptr); break;			\
	default: BUILD_BUG();					\
	}							\
								\
	__pu_err;						\
})

#define __put_user(x, ptr)					\
({								\
	__typeof__(&*(ptr)) __ptr = ptr;			\
	__typeof__(*(__ptr)) __x = (__typeof__(*(__ptr)))(x);	\
	__put_user_internal(SR_USER, __x, __ptr);		\
})

#define __put_kernel_nofault(dst, src, type, err_label)		\
{								\
	type __z = *(type *)(src);				\
	long __err;						\
	__err = __put_user_internal(SR_KERNEL, __z, (type *)(dst)); \
	if (unlikely(__err))					\
		goto err_label;					\
}




/*
 * The "__put_user/kernel_asm()" macros tell gcc they read from memory
 * instead of writing. This is because they do not write to any memory
 * gcc knows about, so there are no aliasing issues. These macros must
 * also be aware that fixups are executed in the context of the fault,
 * and any registers used there must be listed as clobbers.
 * The register holding the possible EFAULT error (ASM_EXCEPTIONTABLE_REG)
 * is already listed as input and output register.
 */

#define __put_user_asm(sr, stx, x, ptr)				\
	__asm__ __volatile__ (					\
		"1: " stx " %1,0(%%sr%2,%3)\n"			\
		"9:\n"						\
		ASM_EXCEPTIONTABLE_ENTRY_EFAULT(1b, 9b)		\
		: "+r"(__pu_err)				\
		: "r"(x), "i"(sr), "r"(ptr))


#if !defined(CONFIG_64BIT)

#define __put_user_asm64(sr, __val, ptr) do {			\
	__asm__ __volatile__ (					\
		"1: stw %1,0(%%sr%2,%3)\n"			\
		"2: stw %R1,4(%%sr%2,%3)\n"			\
		"9:\n"						\
		ASM_EXCEPTIONTABLE_ENTRY_EFAULT(1b, 9b)		\
		ASM_EXCEPTIONTABLE_ENTRY_EFAULT(2b, 9b)		\
		: "+r"(__pu_err)				\
		: "r"(__val), "i"(sr), "r"(ptr));		\
} while (0)

#endif /* !defined(CONFIG_64BIT) */


/*
 * Complex access routines -- external declarations
 */

extern long strncpy_from_user(char *, const char __user *, long);
extern __must_check unsigned lclear_user(void __user *, unsigned long);
extern __must_check long strnlen_user(const char __user *src, long n);
/*
 * Complex access routines -- macros
 */

#define clear_user lclear_user
#define __clear_user lclear_user

unsigned long __must_check raw_copy_to_user(void __user *dst, const void *src,
					    unsigned long len);
unsigned long __must_check raw_copy_from_user(void *dst, const void __user *src,
					    unsigned long len);
#define INLINE_COPY_TO_USER
#define INLINE_COPY_FROM_USER

struct pt_regs;
int fixup_exception(struct pt_regs *regs);

#endif /* __PARISC_UACCESS_H */
