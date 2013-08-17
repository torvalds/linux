#ifndef __PARISC_UACCESS_H
#define __PARISC_UACCESS_H

/*
 * User space memory access functions
 */
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/errno.h>
#include <asm-generic/uaccess-unaligned.h>

#define VERIFY_READ 0
#define VERIFY_WRITE 1

#define KERNEL_DS	((mm_segment_t){0})
#define USER_DS 	((mm_segment_t){1})

#define segment_eq(a,b)	((a).seg == (b).seg)

#define get_ds()	(KERNEL_DS)
#define get_fs()	(current_thread_info()->addr_limit)
#define set_fs(x)	(current_thread_info()->addr_limit = (x))

/*
 * Note that since kernel addresses are in a separate address space on
 * parisc, we don't need to do anything for access_ok().
 * We just let the page fault handler do the right thing. This also means
 * that put_user is the same as __put_user, etc.
 */

extern int __get_kernel_bad(void);
extern int __get_user_bad(void);
extern int __put_kernel_bad(void);
extern int __put_user_bad(void);

static inline long access_ok(int type, const void __user * addr,
		unsigned long size)
{
	return 1;
}

#define put_user __put_user
#define get_user __get_user

#if !defined(CONFIG_64BIT)
#define LDD_KERNEL(ptr)		__get_kernel_bad();
#define LDD_USER(ptr)		__get_user_bad();
#define STD_KERNEL(x, ptr)	__put_kernel_asm64(x,ptr)
#define STD_USER(x, ptr)	__put_user_asm64(x,ptr)
#define ASM_WORD_INSN		".word\t"
#else
#define LDD_KERNEL(ptr)		__get_kernel_asm("ldd",ptr)
#define LDD_USER(ptr)		__get_user_asm("ldd",ptr)
#define STD_KERNEL(x, ptr)	__put_kernel_asm("std",x,ptr)
#define STD_USER(x, ptr)	__put_user_asm("std",x,ptr)
#define ASM_WORD_INSN		".dword\t"
#endif

/*
 * The exception table contains two values: the first is an address
 * for an instruction that is allowed to fault, and the second is
 * the address to the fixup routine. 
 */

struct exception_table_entry {
	unsigned long insn;  /* address of insn that is allowed to fault.   */
	long fixup;          /* fixup routine */
};

#define ASM_EXCEPTIONTABLE_ENTRY( fault_addr, except_addr )\
	".section __ex_table,\"aw\"\n"			   \
	ASM_WORD_INSN #fault_addr ", " #except_addr "\n\t" \
	".previous\n"

/*
 * The page fault handler stores, in a per-cpu area, the following information
 * if a fixup routine is available.
 */
struct exception_data {
	unsigned long fault_ip;
	unsigned long fault_space;
	unsigned long fault_addr;
};

#define __get_user(x,ptr)                               \
({                                                      \
	register long __gu_err __asm__ ("r8") = 0;      \
	register long __gu_val __asm__ ("r9") = 0;      \
							\
	if (segment_eq(get_fs(),KERNEL_DS)) {           \
	    switch (sizeof(*(ptr))) {                   \
	    case 1: __get_kernel_asm("ldb",ptr); break; \
	    case 2: __get_kernel_asm("ldh",ptr); break; \
	    case 4: __get_kernel_asm("ldw",ptr); break; \
	    case 8: LDD_KERNEL(ptr); break;		\
	    default: __get_kernel_bad(); break;         \
	    }                                           \
	}                                               \
	else {                                          \
	    switch (sizeof(*(ptr))) {                   \
	    case 1: __get_user_asm("ldb",ptr); break;   \
	    case 2: __get_user_asm("ldh",ptr); break;   \
	    case 4: __get_user_asm("ldw",ptr); break;   \
	    case 8: LDD_USER(ptr);  break;		\
	    default: __get_user_bad(); break;           \
	    }                                           \
	}                                               \
							\
	(x) = (__typeof__(*(ptr))) __gu_val;            \
	__gu_err;                                       \
})

#define __get_kernel_asm(ldx,ptr)                       \
	__asm__("\n1:\t" ldx "\t0(%2),%0\n\t"		\
		ASM_EXCEPTIONTABLE_ENTRY(1b, fixup_get_user_skip_1)\
		: "=r"(__gu_val), "=r"(__gu_err)        \
		: "r"(ptr), "1"(__gu_err)		\
		: "r1");

#define __get_user_asm(ldx,ptr)                         \
	__asm__("\n1:\t" ldx "\t0(%%sr3,%2),%0\n\t"	\
		ASM_EXCEPTIONTABLE_ENTRY(1b,fixup_get_user_skip_1)\
		: "=r"(__gu_val), "=r"(__gu_err)        \
		: "r"(ptr), "1"(__gu_err)		\
		: "r1");

#define __put_user(x,ptr)                                       \
({								\
	register long __pu_err __asm__ ("r8") = 0;      	\
        __typeof__(*(ptr)) __x = (__typeof__(*(ptr)))(x);	\
								\
	if (segment_eq(get_fs(),KERNEL_DS)) {                   \
	    switch (sizeof(*(ptr))) {                           \
	    case 1: __put_kernel_asm("stb",__x,ptr); break;     \
	    case 2: __put_kernel_asm("sth",__x,ptr); break;     \
	    case 4: __put_kernel_asm("stw",__x,ptr); break;     \
	    case 8: STD_KERNEL(__x,ptr); break;			\
	    default: __put_kernel_bad(); break;			\
	    }                                                   \
	}                                                       \
	else {                                                  \
	    switch (sizeof(*(ptr))) {                           \
	    case 1: __put_user_asm("stb",__x,ptr); break;       \
	    case 2: __put_user_asm("sth",__x,ptr); break;       \
	    case 4: __put_user_asm("stw",__x,ptr); break;       \
	    case 8: STD_USER(__x,ptr); break;			\
	    default: __put_user_bad(); break;			\
	    }                                                   \
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

#define __put_kernel_asm(stx,x,ptr)                         \
	__asm__ __volatile__ (                              \
		"\n1:\t" stx "\t%2,0(%1)\n\t"		    \
		ASM_EXCEPTIONTABLE_ENTRY(1b,fixup_put_user_skip_1)\
		: "=r"(__pu_err)                            \
		: "r"(ptr), "r"(x), "0"(__pu_err)	    \
	    	: "r1")

#define __put_user_asm(stx,x,ptr)                           \
	__asm__ __volatile__ (                              \
		"\n1:\t" stx "\t%2,0(%%sr3,%1)\n\t"	    \
		ASM_EXCEPTIONTABLE_ENTRY(1b,fixup_put_user_skip_1)\
		: "=r"(__pu_err)                            \
		: "r"(ptr), "r"(x), "0"(__pu_err)	    \
		: "r1")


#if !defined(CONFIG_64BIT)

#define __put_kernel_asm64(__val,ptr) do {		    \
	u64 __val64 = (u64)(__val);			    \
	u32 hi = (__val64) >> 32;			    \
	u32 lo = (__val64) & 0xffffffff;		    \
	__asm__ __volatile__ (				    \
		"\n1:\tstw %2,0(%1)"			    \
		"\n2:\tstw %3,4(%1)\n\t"		    \
		ASM_EXCEPTIONTABLE_ENTRY(1b,fixup_put_user_skip_2)\
		ASM_EXCEPTIONTABLE_ENTRY(2b,fixup_put_user_skip_1)\
		: "=r"(__pu_err)                            \
		: "r"(ptr), "r"(hi), "r"(lo), "0"(__pu_err) \
		: "r1");				    \
} while (0)

#define __put_user_asm64(__val,ptr) do {	    	    \
	u64 __val64 = (u64)(__val);			    \
	u32 hi = (__val64) >> 32;			    \
	u32 lo = (__val64) & 0xffffffff;		    \
	__asm__ __volatile__ (				    \
		"\n1:\tstw %2,0(%%sr3,%1)"		    \
		"\n2:\tstw %3,4(%%sr3,%1)\n\t"		    \
		ASM_EXCEPTIONTABLE_ENTRY(1b,fixup_put_user_skip_2)\
		ASM_EXCEPTIONTABLE_ENTRY(2b,fixup_put_user_skip_1)\
		: "=r"(__pu_err)                            \
		: "r"(ptr), "r"(hi), "r"(lo), "0"(__pu_err) \
		: "r1");				    \
} while (0)

#endif /* !defined(CONFIG_64BIT) */


/*
 * Complex access routines -- external declarations
 */

extern unsigned long lcopy_to_user(void __user *, const void *, unsigned long);
extern unsigned long lcopy_from_user(void *, const void __user *, unsigned long);
extern unsigned long lcopy_in_user(void __user *, const void __user *, unsigned long);
extern long lstrncpy_from_user(char *, const char __user *, long);
extern unsigned lclear_user(void __user *,unsigned long);
extern long lstrnlen_user(const char __user *,long);

/*
 * Complex access routines -- macros
 */

#define strncpy_from_user lstrncpy_from_user
#define strnlen_user lstrnlen_user
#define strlen_user(str) lstrnlen_user(str, 0x7fffffffL)
#define clear_user lclear_user
#define __clear_user lclear_user

unsigned long copy_to_user(void __user *dst, const void *src, unsigned long len);
#define __copy_to_user copy_to_user
unsigned long __copy_from_user(void *dst, const void __user *src, unsigned long len);
unsigned long copy_in_user(void __user *dst, const void __user *src, unsigned long len);
#define __copy_in_user copy_in_user
#define __copy_to_user_inatomic __copy_to_user
#define __copy_from_user_inatomic __copy_from_user

extern void copy_from_user_overflow(void)
#ifdef CONFIG_DEBUG_STRICT_USER_COPY_CHECKS
        __compiletime_error("copy_from_user() buffer size is not provably correct")
#else
        __compiletime_warning("copy_from_user() buffer size is not provably correct")
#endif
;

static inline unsigned long __must_check copy_from_user(void *to,
                                          const void __user *from,
                                          unsigned long n)
{
        int sz = __compiletime_object_size(to);
        int ret = -EFAULT;

        if (likely(sz == -1 || !__builtin_constant_p(n) || sz >= n))
                ret = __copy_from_user(to, from, n);
        else
                copy_from_user_overflow();

        return ret;
}

struct pt_regs;
int fixup_exception(struct pt_regs *regs);

#endif /* __PARISC_UACCESS_H */
