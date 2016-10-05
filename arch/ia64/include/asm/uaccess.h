#ifndef _ASM_IA64_UACCESS_H
#define _ASM_IA64_UACCESS_H

/*
 * This file defines various macros to transfer memory areas across
 * the user/kernel boundary.  This needs to be done carefully because
 * this code is executed in kernel mode and uses user-specified
 * addresses.  Thus, we need to be careful not to let the user to
 * trick us into accessing kernel memory that would normally be
 * inaccessible.  This code is also fairly performance sensitive,
 * so we want to spend as little time doing safety checks as
 * possible.
 *
 * To make matters a bit more interesting, these macros sometimes also
 * called from within the kernel itself, in which case the address
 * validity check must be skipped.  The get_fs() macro tells us what
 * to do: if get_fs()==USER_DS, checking is performed, if
 * get_fs()==KERNEL_DS, checking is bypassed.
 *
 * Note that even if the memory area specified by the user is in a
 * valid address range, it is still possible that we'll get a page
 * fault while accessing it.  This is handled by filling out an
 * exception handler fixup entry for each instruction that has the
 * potential to fault.  When such a fault occurs, the page fault
 * handler checks to see whether the faulting instruction has a fixup
 * associated and, if so, sets r8 to -EFAULT and clears r9 to 0 and
 * then resumes execution at the continuation point.
 *
 * Based on <asm-alpha/uaccess.h>.
 *
 * Copyright (C) 1998, 1999, 2001-2004 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/page-flags.h>
#include <linux/mm.h>

#include <asm/intrinsics.h>
#include <asm/pgtable.h>
#include <asm/io.h>

/*
 * For historical reasons, the following macros are grossly misnamed:
 */
#define KERNEL_DS	((mm_segment_t) { ~0UL })		/* cf. access_ok() */
#define USER_DS		((mm_segment_t) { TASK_SIZE-1 })	/* cf. access_ok() */

#define VERIFY_READ	0
#define VERIFY_WRITE	1

#define get_ds()  (KERNEL_DS)
#define get_fs()  (current_thread_info()->addr_limit)
#define set_fs(x) (current_thread_info()->addr_limit = (x))

#define segment_eq(a, b)	((a).seg == (b).seg)

/*
 * When accessing user memory, we need to make sure the entire area really is in
 * user-level space.  In order to do this efficiently, we make sure that the page at
 * address TASK_SIZE is never valid.  We also need to make sure that the address doesn't
 * point inside the virtually mapped linear page table.
 */
#define __access_ok(addr, size, segment)						\
({											\
	__chk_user_ptr(addr);								\
	(likely((unsigned long) (addr) <= (segment).seg)				\
	 && ((segment).seg == KERNEL_DS.seg						\
	     || likely(REGION_OFFSET((unsigned long) (addr)) < RGN_MAP_LIMIT)));	\
})
#define access_ok(type, addr, size)	__access_ok((addr), (size), get_fs())

/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 *
 * Careful to not
 * (a) re-use the arguments for side effects (sizeof/typeof is ok)
 * (b) require any knowledge of processes at this stage
 */
#define put_user(x, ptr)	__put_user_check((__typeof__(*(ptr))) (x), (ptr), sizeof(*(ptr)), get_fs())
#define get_user(x, ptr)	__get_user_check((x), (ptr), sizeof(*(ptr)), get_fs())

/*
 * The "__xxx" versions do not do address space checking, useful when
 * doing multiple accesses to the same area (the programmer has to do the
 * checks by hand with "access_ok()")
 */
#define __put_user(x, ptr)	__put_user_nocheck((__typeof__(*(ptr))) (x), (ptr), sizeof(*(ptr)))
#define __get_user(x, ptr)	__get_user_nocheck((x), (ptr), sizeof(*(ptr)))

extern long __put_user_unaligned_unknown (void);

#define __put_user_unaligned(x, ptr)								\
({												\
	long __ret;										\
	switch (sizeof(*(ptr))) {								\
		case 1: __ret = __put_user((x), (ptr)); break;					\
		case 2: __ret = (__put_user((x), (u8 __user *)(ptr)))				\
			| (__put_user((x) >> 8, ((u8 __user *)(ptr) + 1))); break;		\
		case 4: __ret = (__put_user((x), (u16 __user *)(ptr)))				\
			| (__put_user((x) >> 16, ((u16 __user *)(ptr) + 1))); break;		\
		case 8: __ret = (__put_user((x), (u32 __user *)(ptr)))				\
			| (__put_user((x) >> 32, ((u32 __user *)(ptr) + 1))); break;		\
		default: __ret = __put_user_unaligned_unknown();				\
	}											\
	__ret;											\
})

extern long __get_user_unaligned_unknown (void);

#define __get_user_unaligned(x, ptr)								\
({												\
	long __ret;										\
	switch (sizeof(*(ptr))) {								\
		case 1: __ret = __get_user((x), (ptr)); break;					\
		case 2: __ret = (__get_user((x), (u8 __user *)(ptr)))				\
			| (__get_user((x) >> 8, ((u8 __user *)(ptr) + 1))); break;		\
		case 4: __ret = (__get_user((x), (u16 __user *)(ptr)))				\
			| (__get_user((x) >> 16, ((u16 __user *)(ptr) + 1))); break;		\
		case 8: __ret = (__get_user((x), (u32 __user *)(ptr)))				\
			| (__get_user((x) >> 32, ((u32 __user *)(ptr) + 1))); break;		\
		default: __ret = __get_user_unaligned_unknown();				\
	}											\
	__ret;											\
})

#ifdef ASM_SUPPORTED
  struct __large_struct { unsigned long buf[100]; };
# define __m(x) (*(struct __large_struct __user *)(x))

/* We need to declare the __ex_table section before we can use it in .xdata.  */
asm (".section \"__ex_table\", \"a\"\n\t.previous");

# define __get_user_size(val, addr, n, err)							\
do {												\
	register long __gu_r8 asm ("r8") = 0;							\
	register long __gu_r9 asm ("r9");							\
	asm ("\n[1:]\tld"#n" %0=%2%P2\t// %0 and %1 get overwritten by exception handler\n"	\
	     "\t.xdata4 \"__ex_table\", 1b-., 1f-.+4\n"						\
	     "[1:]"										\
	     : "=r"(__gu_r9), "=r"(__gu_r8) : "m"(__m(addr)), "1"(__gu_r8));			\
	(err) = __gu_r8;									\
	(val) = __gu_r9;									\
} while (0)

/*
 * The "__put_user_size()" macro tells gcc it reads from memory instead of writing it.  This
 * is because they do not write to any memory gcc knows about, so there are no aliasing
 * issues.
 */
# define __put_user_size(val, addr, n, err)							\
do {												\
	register long __pu_r8 asm ("r8") = 0;							\
	asm volatile ("\n[1:]\tst"#n" %1=%r2%P1\t// %0 gets overwritten by exception handler\n"	\
		      "\t.xdata4 \"__ex_table\", 1b-., 1f-.\n"					\
		      "[1:]"									\
		      : "=r"(__pu_r8) : "m"(__m(addr)), "rO"(val), "0"(__pu_r8));		\
	(err) = __pu_r8;									\
} while (0)

#else /* !ASM_SUPPORTED */
# define RELOC_TYPE	2	/* ip-rel */
# define __get_user_size(val, addr, n, err)				\
do {									\
	__ld_user("__ex_table", (unsigned long) addr, n, RELOC_TYPE);	\
	(err) = ia64_getreg(_IA64_REG_R8);				\
	(val) = ia64_getreg(_IA64_REG_R9);				\
} while (0)
# define __put_user_size(val, addr, n, err)				\
do {									\
	__st_user("__ex_table", (unsigned long) addr, n, RELOC_TYPE,	\
		  (__force unsigned long) (val));			\
	(err) = ia64_getreg(_IA64_REG_R8);				\
} while (0)
#endif /* !ASM_SUPPORTED */

extern void __get_user_unknown (void);

/*
 * Evaluating arguments X, PTR, SIZE, and SEGMENT may involve subroutine-calls, which
 * could clobber r8 and r9 (among others).  Thus, be careful not to evaluate it while
 * using r8/r9.
 */
#define __do_get_user(check, x, ptr, size, segment)					\
({											\
	const __typeof__(*(ptr)) __user *__gu_ptr = (ptr);				\
	__typeof__ (size) __gu_size = (size);						\
	long __gu_err = -EFAULT;							\
	unsigned long __gu_val = 0;							\
	if (!check || __access_ok(__gu_ptr, size, segment))				\
		switch (__gu_size) {							\
		      case 1: __get_user_size(__gu_val, __gu_ptr, 1, __gu_err); break;	\
		      case 2: __get_user_size(__gu_val, __gu_ptr, 2, __gu_err); break;	\
		      case 4: __get_user_size(__gu_val, __gu_ptr, 4, __gu_err); break;	\
		      case 8: __get_user_size(__gu_val, __gu_ptr, 8, __gu_err); break;	\
		      default: __get_user_unknown(); break;				\
		}									\
	(x) = (__force __typeof__(*(__gu_ptr))) __gu_val;				\
	__gu_err;									\
})

#define __get_user_nocheck(x, ptr, size)	__do_get_user(0, x, ptr, size, KERNEL_DS)
#define __get_user_check(x, ptr, size, segment)	__do_get_user(1, x, ptr, size, segment)

extern void __put_user_unknown (void);

/*
 * Evaluating arguments X, PTR, SIZE, and SEGMENT may involve subroutine-calls, which
 * could clobber r8 (among others).  Thus, be careful not to evaluate them while using r8.
 */
#define __do_put_user(check, x, ptr, size, segment)					\
({											\
	__typeof__ (x) __pu_x = (x);							\
	__typeof__ (*(ptr)) __user *__pu_ptr = (ptr);					\
	__typeof__ (size) __pu_size = (size);						\
	long __pu_err = -EFAULT;							\
											\
	if (!check || __access_ok(__pu_ptr, __pu_size, segment))			\
		switch (__pu_size) {							\
		      case 1: __put_user_size(__pu_x, __pu_ptr, 1, __pu_err); break;	\
		      case 2: __put_user_size(__pu_x, __pu_ptr, 2, __pu_err); break;	\
		      case 4: __put_user_size(__pu_x, __pu_ptr, 4, __pu_err); break;	\
		      case 8: __put_user_size(__pu_x, __pu_ptr, 8, __pu_err); break;	\
		      default: __put_user_unknown(); break;				\
		}									\
	__pu_err;									\
})

#define __put_user_nocheck(x, ptr, size)	__do_put_user(0, x, ptr, size, KERNEL_DS)
#define __put_user_check(x, ptr, size, segment)	__do_put_user(1, x, ptr, size, segment)

/*
 * Complex access routines
 */
extern unsigned long __must_check __copy_user (void __user *to, const void __user *from,
					       unsigned long count);

static inline unsigned long
__copy_to_user (void __user *to, const void *from, unsigned long count)
{
	check_object_size(from, count, true);

	return __copy_user(to, (__force void __user *) from, count);
}

static inline unsigned long
__copy_from_user (void *to, const void __user *from, unsigned long count)
{
	check_object_size(to, count, false);

	return __copy_user((__force void __user *) to, from, count);
}

#define __copy_to_user_inatomic		__copy_to_user
#define __copy_from_user_inatomic	__copy_from_user
#define copy_to_user(to, from, n)							\
({											\
	void __user *__cu_to = (to);							\
	const void *__cu_from = (from);							\
	long __cu_len = (n);								\
											\
	if (__access_ok(__cu_to, __cu_len, get_fs())) {					\
		check_object_size(__cu_from, __cu_len, true);			\
		__cu_len = __copy_user(__cu_to, (__force void __user *)  __cu_from, __cu_len);	\
	}										\
	__cu_len;									\
})

static inline unsigned long
copy_from_user(void *to, const void __user *from, unsigned long n)
{
	if (likely(__access_ok(from, n, get_fs())))
		n = __copy_user((__force void __user *) to, from, n);
	else
		memset(to, 0, n);
	return n;
}

#define __copy_in_user(to, from, size)	__copy_user((to), (from), (size))

static inline unsigned long
copy_in_user (void __user *to, const void __user *from, unsigned long n)
{
	if (likely(access_ok(VERIFY_READ, from, n) && access_ok(VERIFY_WRITE, to, n)))
		n = __copy_user(to, from, n);
	return n;
}

extern unsigned long __do_clear_user (void __user *, unsigned long);

#define __clear_user(to, n)		__do_clear_user(to, n)

#define clear_user(to, n)					\
({								\
	unsigned long __cu_len = (n);				\
	if (__access_ok(to, __cu_len, get_fs()))		\
		__cu_len = __do_clear_user(to, __cu_len);	\
	__cu_len;						\
})


/*
 * Returns: -EFAULT if exception before terminator, N if the entire buffer filled, else
 * strlen.
 */
extern long __must_check __strncpy_from_user (char *to, const char __user *from, long to_len);

#define strncpy_from_user(to, from, n)					\
({									\
	const char __user * __sfu_from = (from);			\
	long __sfu_ret = -EFAULT;					\
	if (__access_ok(__sfu_from, 0, get_fs()))			\
		__sfu_ret = __strncpy_from_user((to), __sfu_from, (n));	\
	__sfu_ret;							\
})

/* Returns: 0 if bad, string length+1 (memory size) of string if ok */
extern unsigned long __strlen_user (const char __user *);

#define strlen_user(str)				\
({							\
	const char __user *__su_str = (str);		\
	unsigned long __su_ret = 0;			\
	if (__access_ok(__su_str, 0, get_fs()))		\
		__su_ret = __strlen_user(__su_str);	\
	__su_ret;					\
})

/*
 * Returns: 0 if exception before NUL or reaching the supplied limit
 * (N), a value greater than N if the limit would be exceeded, else
 * strlen.
 */
extern unsigned long __strnlen_user (const char __user *, long);

#define strnlen_user(str, len)					\
({								\
	const char __user *__su_str = (str);			\
	unsigned long __su_ret = 0;				\
	if (__access_ok(__su_str, 0, get_fs()))			\
		__su_ret = __strnlen_user(__su_str, len);	\
	__su_ret;						\
})

/* Generic code can't deal with the location-relative format that we use for compactness.  */
#define ARCH_HAS_SORT_EXTABLE
#define ARCH_HAS_SEARCH_EXTABLE

struct exception_table_entry {
	int addr;	/* location-relative address of insn this fixup is for */
	int cont;	/* location-relative continuation addr.; if bit 2 is set, r9 is set to 0 */
};

extern void ia64_handle_exception (struct pt_regs *regs, const struct exception_table_entry *e);
extern const struct exception_table_entry *search_exception_tables (unsigned long addr);

static inline int
ia64_done_with_exception (struct pt_regs *regs)
{
	const struct exception_table_entry *e;
	e = search_exception_tables(regs->cr_iip + ia64_psr(regs)->ri);
	if (e) {
		ia64_handle_exception(regs, e);
		return 1;
	}
	return 0;
}

#define ARCH_HAS_TRANSLATE_MEM_PTR	1
static __inline__ void *
xlate_dev_mem_ptr(phys_addr_t p)
{
	struct page *page;
	void *ptr;

	page = pfn_to_page(p >> PAGE_SHIFT);
	if (PageUncached(page))
		ptr = (void *)p + __IA64_UNCACHED_OFFSET;
	else
		ptr = __va(p);

	return ptr;
}

/*
 * Convert a virtual cached kernel memory pointer to an uncached pointer
 */
static __inline__ void *
xlate_dev_kmem_ptr(void *p)
{
	struct page *page;
	void *ptr;

	page = virt_to_page((unsigned long)p);
	if (PageUncached(page))
		ptr = (void *)__pa(p) + __IA64_UNCACHED_OFFSET;
	else
		ptr = p;

	return ptr;
}

#endif /* _ASM_IA64_UACCESS_H */
