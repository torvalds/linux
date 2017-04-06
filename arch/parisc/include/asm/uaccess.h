#ifndef __PARISC_UACCESS_H
#define __PARISC_UACCESS_H

/*
 * User space memory access functions
 */
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/errno.h>
#include <asm-generic/uaccess-unaligned.h>

#include <linux/bug.h>
#include <linux/string.h>
#include <linux/thread_info.h>

#define VERIFY_READ 0
#define VERIFY_WRITE 1

#define KERNEL_DS	((mm_segment_t){0})
#define USER_DS 	((mm_segment_t){1})

#define segment_eq(a, b) ((a).seg == (b).seg)

#define get_ds()	(KERNEL_DS)
#define get_fs()	(current_thread_info()->addr_limit)
#define set_fs(x)	(current_thread_info()->addr_limit = (x))

/*
 * Note that since kernel addresses are in a separate address space on
 * parisc, we don't need to do anything for access_ok().
 * We just let the page fault handler do the right thing. This also means
 * that put_user is the same as __put_user, etc.
 */

#define access_ok(type, uaddr, size) (1)

#define put_user __put_user
#define get_user __get_user

#if !defined(CONFIG_64BIT)
#define LDD_USER(ptr)		__get_user_asm64(ptr)
#define STD_USER(x, ptr)	__put_user_asm64(x, ptr)
#else
#define LDD_USER(ptr)		__get_user_asm("ldd", ptr)
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
 * The page fault handler stores, in a per-cpu area, the following information
 * if a fixup routine is available.
 */
struct exception_data {
	unsigned long fault_ip;
	unsigned long fault_gp;
	unsigned long fault_space;
	unsigned long fault_addr;
};

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

#define __get_user(x, ptr)                               \
({                                                       \
	register long __gu_err __asm__ ("r8") = 0;       \
	register long __gu_val __asm__ ("r9") = 0;       \
							 \
	load_sr2();					 \
	switch (sizeof(*(ptr))) {			 \
	    case 1: __get_user_asm("ldb", ptr); break;   \
	    case 2: __get_user_asm("ldh", ptr); break;   \
	    case 4: __get_user_asm("ldw", ptr); break;   \
	    case 8: LDD_USER(ptr);  break;		 \
	    default: BUILD_BUG(); break;		 \
	}                                                \
							 \
	(x) = (__force __typeof__(*(ptr))) __gu_val;	 \
	__gu_err;                                        \
})

#define __get_user_asm(ldx, ptr)                        \
	__asm__("\n1:\t" ldx "\t0(%%sr2,%2),%0\n\t"	\
		ASM_EXCEPTIONTABLE_ENTRY(1b, fixup_get_user_skip_1)\
		: "=r"(__gu_val), "=r"(__gu_err)        \
		: "r"(ptr), "1"(__gu_err)		\
		: "r1");

#if !defined(CONFIG_64BIT)

#define __get_user_asm64(ptr) 				\
	__asm__("\n1:\tldw 0(%%sr2,%2),%0"		\
		"\n2:\tldw 4(%%sr2,%2),%R0\n\t"		\
		ASM_EXCEPTIONTABLE_ENTRY(1b, fixup_get_user_skip_2)\
		ASM_EXCEPTIONTABLE_ENTRY(2b, fixup_get_user_skip_1)\
		: "=r"(__gu_val), "=r"(__gu_err)	\
		: "r"(ptr), "1"(__gu_err)		\
		: "r1");

#endif /* !defined(CONFIG_64BIT) */


#define __put_user(x, ptr)                                      \
({								\
	register long __pu_err __asm__ ("r8") = 0;      	\
        __typeof__(*(ptr)) __x = (__typeof__(*(ptr)))(x);	\
								\
	load_sr2();						\
	switch (sizeof(*(ptr))) {				\
	    case 1: __put_user_asm("stb", __x, ptr); break;     \
	    case 2: __put_user_asm("sth", __x, ptr); break;     \
	    case 4: __put_user_asm("stw", __x, ptr); break;     \
	    case 8: STD_USER(__x, ptr); break;			\
	    default: BUILD_BUG(); break;			\
	}                                                       \
								\
	__pu_err;						\
})

/*
 * The "__put_user/kernel_asm()" macros tell gcc they read from memory
 * instead of writing. This is because they do not write to any memory
 * gcc knows about, so there are no aliasing issues. These macros must
 * also be aware that "fixup_put_user_skip_[12]" are executed in the
 * context of the fault, and any registers used there must be listed
 * as clobbers. In this case only "r1" is used by the current routines.
 * r8/r9 are already listed as err/val.
 */

#define __put_user_asm(stx, x, ptr)                         \
	__asm__ __volatile__ (                              \
		"\n1:\t" stx "\t%2,0(%%sr2,%1)\n\t"	    \
		ASM_EXCEPTIONTABLE_ENTRY(1b, fixup_put_user_skip_1)\
		: "=r"(__pu_err)                            \
		: "r"(ptr), "r"(x), "0"(__pu_err)	    \
		: "r1")


#if !defined(CONFIG_64BIT)

#define __put_user_asm64(__val, ptr) do {	    	    \
	__asm__ __volatile__ (				    \
		"\n1:\tstw %2,0(%%sr2,%1)"		    \
		"\n2:\tstw %R2,4(%%sr2,%1)\n\t"		    \
		ASM_EXCEPTIONTABLE_ENTRY(1b, fixup_put_user_skip_2)\
		ASM_EXCEPTIONTABLE_ENTRY(2b, fixup_put_user_skip_1)\
		: "=r"(__pu_err)                            \
		: "r"(ptr), "r"(__val), "0"(__pu_err) \
		: "r1");				    \
} while (0)

#endif /* !defined(CONFIG_64BIT) */


/*
 * Complex access routines -- external declarations
 */

extern unsigned long lcopy_to_user(void __user *, const void *, unsigned long);
extern unsigned long lcopy_from_user(void *, const void __user *, unsigned long);
extern unsigned long lcopy_in_user(void __user *, const void __user *, unsigned long);
extern long strncpy_from_user(char *, const char __user *, long);
extern unsigned lclear_user(void __user *, unsigned long);
extern long lstrnlen_user(const char __user *, long);
/*
 * Complex access routines -- macros
 */
#define user_addr_max() (~0UL)

#define strnlen_user lstrnlen_user
#define strlen_user(str) lstrnlen_user(str, 0x7fffffffL)
#define clear_user lclear_user
#define __clear_user lclear_user

unsigned long __must_check __copy_to_user(void __user *dst, const void *src,
					  unsigned long len);
unsigned long __must_check __copy_from_user(void *dst, const void __user *src,
					  unsigned long len);
unsigned long copy_in_user(void __user *dst, const void __user *src,
			   unsigned long len);
#define __copy_in_user copy_in_user
#define __copy_to_user_inatomic __copy_to_user
#define __copy_from_user_inatomic __copy_from_user

extern void __compiletime_error("usercopy buffer size is too small")
__bad_copy_user(void);

static inline void copy_user_overflow(int size, unsigned long count)
{
	WARN(1, "Buffer overflow detected (%d < %lu)!\n", size, count);
}

static __always_inline unsigned long __must_check
copy_from_user(void *to, const void __user *from, unsigned long n)
{
	int sz = __compiletime_object_size(to);
	unsigned long ret = n;

	if (likely(sz < 0 || sz >= n)) {
		check_object_size(to, n, false);
		ret = __copy_from_user(to, from, n);
	} else if (!__builtin_constant_p(n))
		copy_user_overflow(sz, n);
	else
		__bad_copy_user();

	if (unlikely(ret))
		memset(to + (n - ret), 0, ret);

	return ret;
}

static __always_inline unsigned long __must_check
copy_to_user(void __user *to, const void *from, unsigned long n)
{
	int sz = __compiletime_object_size(from);

	if (likely(sz < 0 || sz >= n)) {
		check_object_size(from, n, true);
		n = __copy_to_user(to, from, n);
	} else if (!__builtin_constant_p(n))
		copy_user_overflow(sz, n);
	else
		__bad_copy_user();

	return n;
}

struct pt_regs;
int fixup_exception(struct pt_regs *regs);

#endif /* __PARISC_UACCESS_H */
