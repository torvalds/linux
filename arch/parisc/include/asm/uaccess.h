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

#define KERNEL_DS	((mm_segment_t){0})
#define USER_DS 	((mm_segment_t){1})

#define uaccess_kernel() (get_fs().seg == KERNEL_DS.seg)

#define get_fs()	(current_thread_info()->addr_limit)
#define set_fs(x)	(current_thread_info()->addr_limit = (x))

/*
 * Note that since kernel addresses are in a separate address space on
 * parisc, we don't need to do anything for access_ok().
 * We just let the page fault handler do the right thing. This also means
 * that put_user is the same as __put_user, etc.
 */

#define access_ok(uaddr, size)	\
	( (uaddr) == (uaddr) )

#define put_user __put_user
#define get_user __get_user

#if !defined(CONFIG_64BIT)
#define LDD_USER(val, ptr)	__get_user_asm64(val, ptr)
#define STD_USER(x, ptr)	__put_user_asm64(x, ptr)
#else
#define LDD_USER(val, ptr)	__get_user_asm(val, "ldd", ptr)
#define STD_USER(x, ptr)	__put_user_asm("std", x, ptr)
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
 * load -EFAULT into %r8 for a read or write fault, and zeroes the target
 * register in case of a read fault in get_user().
 */
#define ASM_EXCEPTIONTABLE_ENTRY_EFAULT( fault_addr, except_addr )\
	ASM_EXCEPTIONTABLE_ENTRY( fault_addr, except_addr + 1)

/*
 * load_sr2() preloads the space register %%sr2 - based on the value of
 * get_fs() - with either a value of 0 to access kernel space (KERNEL_DS which
 * is 0), or with the current value of %%sr3 to access user space (USER_DS)
 * memory. The following __get_user_asm() and __put_user_asm() functions have
 * %%sr2 hard-coded to access the requested memory.
 */
#define load_sr2() \
	__asm__(" or,=  %0,%%r0,%%r0\n\t"	\
		" mfsp %%sr3,%0\n\t"		\
		" mtsp %0,%%sr2\n\t"		\
		: : "r"(get_fs()) : )

#define __get_user_internal(val, ptr)			\
({							\
	register long __gu_err __asm__ ("r8") = 0;	\
							\
	switch (sizeof(*(ptr))) {			\
	case 1: __get_user_asm(val, "ldb", ptr); break;	\
	case 2: __get_user_asm(val, "ldh", ptr); break; \
	case 4: __get_user_asm(val, "ldw", ptr); break; \
	case 8: LDD_USER(val, ptr); break;		\
	default: BUILD_BUG();				\
	}						\
							\
	__gu_err;					\
})

#define __get_user(val, ptr)				\
({							\
	load_sr2();					\
	__get_user_internal(val, ptr);			\
})

#define __get_user_asm(val, ldx, ptr)			\
{							\
	register long __gu_val;				\
							\
	__asm__("1: " ldx " 0(%%sr2,%2),%0\n"		\
		"9:\n"					\
		ASM_EXCEPTIONTABLE_ENTRY_EFAULT(1b, 9b)	\
		: "=r"(__gu_val), "=r"(__gu_err)        \
		: "r"(ptr), "1"(__gu_err));		\
							\
	(val) = (__force __typeof__(*(ptr))) __gu_val;	\
}

#if !defined(CONFIG_64BIT)

#define __get_user_asm64(val, ptr)			\
{							\
	union {						\
		unsigned long long	l;		\
		__typeof__(*(ptr))	t;		\
	} __gu_tmp;					\
							\
	__asm__("   copy %%r0,%R0\n"			\
		"1: ldw 0(%%sr2,%2),%0\n"		\
		"2: ldw 4(%%sr2,%2),%R0\n"		\
		"9:\n"					\
		ASM_EXCEPTIONTABLE_ENTRY_EFAULT(1b, 9b)	\
		ASM_EXCEPTIONTABLE_ENTRY_EFAULT(2b, 9b)	\
		: "=&r"(__gu_tmp.l), "=r"(__gu_err)	\
		: "r"(ptr), "1"(__gu_err));		\
							\
	(val) = __gu_tmp.t;				\
}

#endif /* !defined(CONFIG_64BIT) */


#define __put_user_internal(x, ptr)				\
({								\
	register long __pu_err __asm__ ("r8") = 0;      	\
        __typeof__(*(ptr)) __x = (__typeof__(*(ptr)))(x);	\
								\
	switch (sizeof(*(ptr))) {				\
	case 1: __put_user_asm("stb", __x, ptr); break;		\
	case 2: __put_user_asm("sth", __x, ptr); break;		\
	case 4: __put_user_asm("stw", __x, ptr); break;		\
	case 8: STD_USER(__x, ptr); break;			\
	default: BUILD_BUG();					\
	}							\
								\
	__pu_err;						\
})

#define __put_user(x, ptr)					\
({								\
	load_sr2();						\
	__put_user_internal(x, ptr);				\
})


/*
 * The "__put_user/kernel_asm()" macros tell gcc they read from memory
 * instead of writing. This is because they do not write to any memory
 * gcc knows about, so there are no aliasing issues. These macros must
 * also be aware that fixups are executed in the context of the fault,
 * and any registers used there must be listed as clobbers.
 * r8 is already listed as err.
 */

#define __put_user_asm(stx, x, ptr)                         \
	__asm__ __volatile__ (                              \
		"1: " stx " %2,0(%%sr2,%1)\n"		    \
		"9:\n"					    \
		ASM_EXCEPTIONTABLE_ENTRY_EFAULT(1b, 9b)	    \
		: "=r"(__pu_err)                            \
		: "r"(ptr), "r"(x), "0"(__pu_err))


#if !defined(CONFIG_64BIT)

#define __put_user_asm64(__val, ptr) do {	    	    \
	__asm__ __volatile__ (				    \
		"1: stw %2,0(%%sr2,%1)\n"		    \
		"2: stw %R2,4(%%sr2,%1)\n"		    \
		"9:\n"					    \
		ASM_EXCEPTIONTABLE_ENTRY_EFAULT(1b, 9b)	    \
		ASM_EXCEPTIONTABLE_ENTRY_EFAULT(2b, 9b)	    \
		: "=r"(__pu_err)                            \
		: "r"(ptr), "r"(__val), "0"(__pu_err));	    \
} while (0)

#endif /* !defined(CONFIG_64BIT) */


/*
 * Complex access routines -- external declarations
 */

extern long strncpy_from_user(char *, const char __user *, long);
extern unsigned lclear_user(void __user *, unsigned long);
extern long lstrnlen_user(const char __user *, long);
/*
 * Complex access routines -- macros
 */
#define user_addr_max() (~0UL)

#define strnlen_user lstrnlen_user
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
