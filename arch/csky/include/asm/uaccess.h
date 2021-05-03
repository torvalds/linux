/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_UACCESS_H
#define __ASM_CSKY_UACCESS_H

#define user_addr_max() \
	(uaccess_kernel() ? KERNEL_DS.seg : get_fs().seg)

static inline int __access_ok(unsigned long addr, unsigned long size)
{
	unsigned long limit = current_thread_info()->addr_limit.seg;

	return ((addr < limit) && ((addr + size) < limit));
}
#define __access_ok __access_ok

/*
 * __put_user_fn
 */
extern int __put_user_bad(void);

#define __put_user_asm_b(x, ptr, err)			\
do {							\
	int errcode;					\
	__asm__ __volatile__(				\
	"1:     stb   %1, (%2,0)	\n"		\
	"       br    3f		\n"		\
	"2:     mov   %0, %3		\n"		\
	"       br    3f		\n"		\
	".section __ex_table, \"a\"	\n"		\
	".align   2			\n"		\
	".long    1b,2b			\n"		\
	".previous			\n"		\
	"3:				\n"		\
	: "=r"(err), "=r"(x), "=r"(ptr), "=r"(errcode)	\
	: "0"(err), "1"(x), "2"(ptr), "3"(-EFAULT)	\
	: "memory");					\
} while (0)

#define __put_user_asm_h(x, ptr, err)			\
do {							\
	int errcode;					\
	__asm__ __volatile__(				\
	"1:     sth   %1, (%2,0)	\n"		\
	"       br    3f		\n"		\
	"2:     mov   %0, %3		\n"		\
	"       br    3f		\n"		\
	".section __ex_table, \"a\"	\n"		\
	".align   2			\n"		\
	".long    1b,2b			\n"		\
	".previous			\n"		\
	"3:				\n"		\
	: "=r"(err), "=r"(x), "=r"(ptr), "=r"(errcode)	\
	: "0"(err), "1"(x), "2"(ptr), "3"(-EFAULT)	\
	: "memory");					\
} while (0)

#define __put_user_asm_w(x, ptr, err)			\
do {							\
	int errcode;					\
	__asm__ __volatile__(				\
	"1:     stw   %1, (%2,0)	\n"		\
	"       br    3f		\n"		\
	"2:     mov   %0, %3		\n"		\
	"       br    3f		\n"		\
	".section __ex_table,\"a\"	\n"		\
	".align   2			\n"		\
	".long    1b, 2b		\n"		\
	".previous			\n"		\
	"3:				\n"		\
	: "=r"(err), "=r"(x), "=r"(ptr), "=r"(errcode)	\
	: "0"(err), "1"(x), "2"(ptr), "3"(-EFAULT)	\
	: "memory");					\
} while (0)

#define __put_user_asm_64(x, ptr, err)			\
do {							\
	int tmp;					\
	int errcode;					\
							\
	__asm__ __volatile__(				\
	"     ldw     %3, (%1, 0)     \n"		\
	"1:   stw     %3, (%2, 0)     \n"		\
	"     ldw     %3, (%1, 4)     \n"		\
	"2:   stw     %3, (%2, 4)     \n"		\
	"     br      4f              \n"		\
	"3:   mov     %0, %4          \n"		\
	"     br      4f              \n"		\
	".section __ex_table, \"a\"   \n"		\
	".align   2                   \n"		\
	".long    1b, 3b              \n"		\
	".long    2b, 3b              \n"		\
	".previous                    \n"		\
	"4:                           \n"		\
	: "=r"(err), "=r"(x), "=r"(ptr),		\
	  "=r"(tmp), "=r"(errcode)			\
	: "0"(err), "1"(x), "2"(ptr), "3"(0),		\
	  "4"(-EFAULT)					\
	: "memory");					\
} while (0)

static inline int __put_user_fn(size_t size, void __user *ptr, void *x)
{
	int retval = 0;
	u32 tmp;

	switch (size) {
	case 1:
		tmp = *(u8 *)x;
		__put_user_asm_b(tmp, ptr, retval);
		break;
	case 2:
		tmp = *(u16 *)x;
		__put_user_asm_h(tmp, ptr, retval);
		break;
	case 4:
		tmp = *(u32 *)x;
		__put_user_asm_w(tmp, ptr, retval);
		break;
	case 8:
		__put_user_asm_64(x, (u64 *)ptr, retval);
		break;
	}

	return retval;
}
#define __put_user_fn __put_user_fn

/*
 * __get_user_fn
 */
extern int __get_user_bad(void);

#define __get_user_asm_common(x, ptr, ins, err)		\
do {							\
	int errcode;					\
	__asm__ __volatile__(				\
	"1:   " ins " %1, (%4, 0)	\n"		\
	"       br    3f		\n"		\
	"2:     mov   %0, %2		\n"		\
	"       movi  %1, 0		\n"		\
	"       br    3f		\n"		\
	".section __ex_table,\"a\"      \n"		\
	".align   2			\n"		\
	".long    1b, 2b		\n"		\
	".previous			\n"		\
	"3:				\n" 		\
	: "=r"(err), "=r"(x), "=r"(errcode)		\
	: "0"(0), "r"(ptr), "2"(-EFAULT)		\
	: "memory");					\
} while (0)

#define __get_user_asm_64(x, ptr, err)			\
do {							\
	int tmp;					\
	int errcode;					\
							\
	__asm__ __volatile__(				\
	"1:   ldw     %3, (%2, 0)     \n"		\
	"     stw     %3, (%1, 0)     \n"		\
	"2:   ldw     %3, (%2, 4)     \n"		\
	"     stw     %3, (%1, 4)     \n"		\
	"     br      4f              \n"		\
	"3:   mov     %0, %4          \n"		\
	"     br      4f              \n"		\
	".section __ex_table, \"a\"   \n"		\
	".align   2                   \n"		\
	".long    1b, 3b              \n"		\
	".long    2b, 3b              \n"		\
	".previous                    \n"		\
	"4:                           \n"		\
	: "=r"(err), "=r"(x), "=r"(ptr),		\
	  "=r"(tmp), "=r"(errcode)			\
	: "0"(err), "1"(x), "2"(ptr), "3"(0),		\
	  "4"(-EFAULT)					\
	: "memory");					\
} while (0)

static inline int __get_user_fn(size_t size, const void __user *ptr, void *x)
{
	int retval;
	u32 tmp;

	switch (size) {
	case 1:
		__get_user_asm_common(tmp, ptr, "ldb", retval);
		*(u8 *)x = (u8)tmp;
		break;
	case 2:
		__get_user_asm_common(tmp, ptr, "ldh", retval);
		*(u16 *)x = (u16)tmp;
		break;
	case 4:
		__get_user_asm_common(tmp, ptr, "ldw", retval);
		*(u32 *)x = (u32)tmp;
		break;
	case 8:
		__get_user_asm_64(x, ptr, retval);
		break;
	}

	return retval;
}
#define __get_user_fn __get_user_fn

unsigned long raw_copy_from_user(void *to, const void *from, unsigned long n);
unsigned long raw_copy_to_user(void *to, const void *from, unsigned long n);

unsigned long __clear_user(void __user *to, unsigned long n);
#define __clear_user __clear_user

long __strncpy_from_user(char *dst, const char *src, long count);
#define __strncpy_from_user __strncpy_from_user

long __strnlen_user(const char *s, long n);
#define __strnlen_user __strnlen_user

#include <asm/segment.h>
#include <asm-generic/uaccess.h>

#endif /* __ASM_CSKY_UACCESS_H */
