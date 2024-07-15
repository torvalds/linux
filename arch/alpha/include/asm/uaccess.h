/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ALPHA_UACCESS_H
#define __ALPHA_UACCESS_H

#include <asm-generic/access_ok.h>
/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 *
 * As the alpha uses the same address space for kernel and user
 * data, we can just do these as direct assignments.  (Of course, the
 * exception handling means that it's no longer "just"...)
 *
 * Careful to not
 * (a) re-use the arguments for side effects (sizeof/typeof is ok)
 * (b) require any knowledge of processes at this stage
 */
#define put_user(x, ptr) \
  __put_user_check((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)))
#define get_user(x, ptr) \
  __get_user_check((x), (ptr), sizeof(*(ptr)))

/*
 * The "__xxx" versions do not do address space checking, useful when
 * doing multiple accesses to the same area (the programmer has to do the
 * checks by hand with "access_ok()")
 */
#define __put_user(x, ptr) \
  __put_user_nocheck((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)))
#define __get_user(x, ptr) \
  __get_user_nocheck((x), (ptr), sizeof(*(ptr)))
  
/*
 * The "lda %1, 2b-1b(%0)" bits are magic to get the assembler to
 * encode the bits we need for resolving the exception.  See the
 * more extensive comments with fixup_inline_exception below for
 * more information.
 */
#define EXC(label,cont,res,err)				\
	".section __ex_table,\"a\"\n"			\
	"	.long "#label"-.\n"			\
	"	lda "#res","#cont"-"#label"("#err")\n"	\
	".previous\n"

extern void __get_user_unknown(void);

#define __get_user_nocheck(x, ptr, size)			\
({								\
	long __gu_err = 0;					\
	unsigned long __gu_val;					\
	__chk_user_ptr(ptr);					\
	switch (size) {						\
	  case 1: __get_user_8(ptr); break;			\
	  case 2: __get_user_16(ptr); break;			\
	  case 4: __get_user_32(ptr); break;			\
	  case 8: __get_user_64(ptr); break;			\
	  default: __get_user_unknown(); break;			\
	}							\
	(x) = (__force __typeof__(*(ptr))) __gu_val;		\
	__gu_err;						\
})

#define __get_user_check(x, ptr, size)				\
({								\
	long __gu_err = -EFAULT;				\
	unsigned long __gu_val = 0;				\
	const __typeof__(*(ptr)) __user *__gu_addr = (ptr);	\
	if (__access_ok(__gu_addr, size)) {			\
		__gu_err = 0;					\
		switch (size) {					\
		  case 1: __get_user_8(__gu_addr); break;	\
		  case 2: __get_user_16(__gu_addr); break;	\
		  case 4: __get_user_32(__gu_addr); break;	\
		  case 8: __get_user_64(__gu_addr); break;	\
		  default: __get_user_unknown(); break;		\
		}						\
	}							\
	(x) = (__force __typeof__(*(ptr))) __gu_val;		\
	__gu_err;						\
})

struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct __user *)(x))

#define __get_user_64(addr)				\
	__asm__("1: ldq %0,%2\n"			\
	"2:\n"						\
	EXC(1b,2b,%0,%1)				\
		: "=r"(__gu_val), "=r"(__gu_err)	\
		: "m"(__m(addr)), "1"(__gu_err))

#define __get_user_32(addr)				\
	__asm__("1: ldl %0,%2\n"			\
	"2:\n"						\
	EXC(1b,2b,%0,%1)				\
		: "=r"(__gu_val), "=r"(__gu_err)	\
		: "m"(__m(addr)), "1"(__gu_err))

#define __get_user_16(addr)				\
	__asm__("1: ldwu %0,%2\n"			\
	"2:\n"						\
	EXC(1b,2b,%0,%1)				\
		: "=r"(__gu_val), "=r"(__gu_err)	\
		: "m"(__m(addr)), "1"(__gu_err))

#define __get_user_8(addr)				\
	__asm__("1: ldbu %0,%2\n"			\
	"2:\n"						\
	EXC(1b,2b,%0,%1)				\
		: "=r"(__gu_val), "=r"(__gu_err)	\
		: "m"(__m(addr)), "1"(__gu_err))

extern void __put_user_unknown(void);

#define __put_user_nocheck(x, ptr, size)			\
({								\
	long __pu_err = 0;					\
	__chk_user_ptr(ptr);					\
	switch (size) {						\
	  case 1: __put_user_8(x, ptr); break;			\
	  case 2: __put_user_16(x, ptr); break;			\
	  case 4: __put_user_32(x, ptr); break;			\
	  case 8: __put_user_64(x, ptr); break;			\
	  default: __put_user_unknown(); break;			\
	}							\
	__pu_err;						\
})

#define __put_user_check(x, ptr, size)				\
({								\
	long __pu_err = -EFAULT;				\
	__typeof__(*(ptr)) __user *__pu_addr = (ptr);		\
	if (__access_ok(__pu_addr, size)) {			\
		__pu_err = 0;					\
		switch (size) {					\
		  case 1: __put_user_8(x, __pu_addr); break;	\
		  case 2: __put_user_16(x, __pu_addr); break;	\
		  case 4: __put_user_32(x, __pu_addr); break;	\
		  case 8: __put_user_64(x, __pu_addr); break;	\
		  default: __put_user_unknown(); break;		\
		}						\
	}							\
	__pu_err;						\
})

/*
 * The "__put_user_xx()" macros tell gcc they read from memory
 * instead of writing: this is because they do not write to
 * any memory gcc knows about, so there are no aliasing issues
 */
#define __put_user_64(x, addr)					\
__asm__ __volatile__("1: stq %r2,%1\n"				\
	"2:\n"							\
	EXC(1b,2b,$31,%0)					\
		: "=r"(__pu_err)				\
		: "m" (__m(addr)), "rJ" (x), "0"(__pu_err))

#define __put_user_32(x, addr)					\
__asm__ __volatile__("1: stl %r2,%1\n"				\
	"2:\n"							\
	EXC(1b,2b,$31,%0)					\
		: "=r"(__pu_err)				\
		: "m"(__m(addr)), "rJ"(x), "0"(__pu_err))

#define __put_user_16(x, addr)					\
__asm__ __volatile__("1: stw %r2,%1\n"				\
	"2:\n"							\
	EXC(1b,2b,$31,%0)					\
		: "=r"(__pu_err)				\
		: "m"(__m(addr)), "rJ"(x), "0"(__pu_err))

#define __put_user_8(x, addr)					\
__asm__ __volatile__("1: stb %r2,%1\n"				\
	"2:\n"							\
	EXC(1b,2b,$31,%0)					\
		: "=r"(__pu_err)				\
		: "m"(__m(addr)), "rJ"(x), "0"(__pu_err))

/*
 * Complex access routines
 */

extern long __copy_user(void *to, const void *from, long len);

static inline unsigned long
raw_copy_from_user(void *to, const void __user *from, unsigned long len)
{
	return __copy_user(to, (__force const void *)from, len);
}

static inline unsigned long
raw_copy_to_user(void __user *to, const void *from, unsigned long len)
{
	return __copy_user((__force void *)to, from, len);
}

extern long __clear_user(void __user *to, long len);

static inline long
clear_user(void __user *to, long len)
{
	if (__access_ok(to, len))
		len = __clear_user(to, len);
	return len;
}

extern long strncpy_from_user(char *dest, const char __user *src, long count);
extern __must_check long strnlen_user(const char __user *str, long n);

#include <asm/extable.h>

#endif /* __ALPHA_UACCESS_H */
