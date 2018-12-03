// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/uaccess.h>
#include <linux/types.h>

unsigned long raw_copy_from_user(void *to, const void *from,
			unsigned long n)
{
	if (access_ok(VERIFY_READ, from, n))
		__copy_user_zeroing(to, from, n);
	else
		memset(to, 0, n);
	return n;
}
EXPORT_SYMBOL(raw_copy_from_user);

unsigned long raw_copy_to_user(void *to, const void *from,
			unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		__copy_user(to, from, n);
	return n;
}
EXPORT_SYMBOL(raw_copy_to_user);


/*
 * copy a null terminated string from userspace.
 */
#define __do_strncpy_from_user(dst, src, count, res)	\
do {							\
	int tmp;					\
	long faultres;					\
	asm volatile(					\
	"       cmpnei  %3, 0           \n"		\
	"       bf      4f              \n"		\
	"1:     cmpnei  %1, 0          	\n"		\
	"       bf      5f              \n"		\
	"2:     ldb     %4, (%3, 0)     \n"		\
	"       stb     %4, (%2, 0)     \n"		\
	"       cmpnei  %4, 0           \n"		\
	"       bf      3f              \n"		\
	"       addi    %3,  1          \n"		\
	"       addi    %2,  1          \n"		\
	"       subi    %1,  1          \n"		\
	"       br      1b              \n"		\
	"3:     subu	%0, %1          \n"		\
	"       br      5f              \n"		\
	"4:     mov     %0, %5          \n"		\
	"       br      5f              \n"		\
	".section __ex_table, \"a\"     \n"		\
	".align   2                     \n"		\
	".long    2b, 4b                \n"		\
	".previous                      \n"		\
	"5:                             \n"		\
	: "=r"(res), "=r"(count), "=r"(dst),		\
	  "=r"(src), "=r"(tmp),   "=r"(faultres)	\
	: "5"(-EFAULT), "0"(count), "1"(count),		\
	  "2"(dst), "3"(src)				\
	: "memory", "cc");				\
} while (0)

/*
 * __strncpy_from_user: - Copy a NUL terminated string from userspace,
 * with less checking.
 * @dst:   Destination address, in kernel space.  This buffer must be at
 *         least @count bytes long.
 * @src:   Source address, in user space.
 * @count: Maximum number of bytes to copy, including the trailing NUL.
 *
 * Copies a NUL-terminated string from userspace to kernel space.
 * Caller must check the specified block with access_ok() before calling
 * this function.
 *
 * On success, returns the length of the string (not including the trailing
 * NUL).
 *
 * If access to userspace fails, returns -EFAULT (some data may have been
 * copied).
 *
 * If @count is smaller than the length of the string, copies @count bytes
 * and returns @count.
 */
long __strncpy_from_user(char *dst, const char *src, long count)
{
	long res;

	__do_strncpy_from_user(dst, src, count, res);
	return res;
}
EXPORT_SYMBOL(__strncpy_from_user);

/*
 * strncpy_from_user: - Copy a NUL terminated string from userspace.
 * @dst:   Destination address, in kernel space.  This buffer must be at
 *         least @count bytes long.
 * @src:   Source address, in user space.
 * @count: Maximum number of bytes to copy, including the trailing NUL.
 *
 * Copies a NUL-terminated string from userspace to kernel space.
 *
 * On success, returns the length of the string (not including the trailing
 * NUL).
 *
 * If access to userspace fails, returns -EFAULT (some data may have been
 * copied).
 *
 * If @count is smaller than the length of the string, copies @count bytes
 * and returns @count.
 */
long strncpy_from_user(char *dst, const char *src, long count)
{
	long res = -EFAULT;

	if (access_ok(VERIFY_READ, src, 1))
		__do_strncpy_from_user(dst, src, count, res);
	return res;
}
EXPORT_SYMBOL(strncpy_from_user);

/*
 * strlen_user: - Get the size of a string in user space.
 * @str: The string to measure.
 * @n:   The maximum valid length
 *
 * Get the size of a NUL-terminated string in user space.
 *
 * Returns the size of the string INCLUDING the terminating NUL.
 * On exception, returns 0.
 * If the string is too long, returns a value greater than @n.
 */
long strnlen_user(const char *s, long n)
{
	unsigned long res, tmp;

	if (s == NULL)
		return 0;

	asm volatile(
	"       cmpnei  %1, 0           \n"
	"       bf      3f              \n"
	"1:     cmpnei  %0, 0           \n"
	"       bf      3f              \n"
	"2:     ldb     %3, (%1, 0)     \n"
	"       cmpnei  %3, 0           \n"
	"       bf      3f              \n"
	"       subi    %0,  1          \n"
	"       addi    %1,  1          \n"
	"       br      1b              \n"
	"3:     subu    %2, %0          \n"
	"       addi    %2,  1          \n"
	"       br      5f              \n"
	"4:     movi    %0, 0           \n"
	"       br      5f              \n"
	".section __ex_table, \"a\"     \n"
	".align   2                     \n"
	".long    2b, 4b                \n"
	".previous                      \n"
	"5:                             \n"
	: "=r"(n), "=r"(s), "=r"(res), "=r"(tmp)
	: "0"(n), "1"(s), "2"(n)
	: "memory", "cc");

	return res;
}
EXPORT_SYMBOL(strnlen_user);

#define __do_clear_user(addr, size)			\
do {							\
	int __d0, zvalue, tmp;				\
							\
	asm volatile(					\
	"0:     cmpnei  %1, 0           \n"		\
	"       bf      7f              \n"		\
	"       mov     %3, %1          \n"		\
	"       andi    %3, 3           \n"		\
	"       cmpnei  %3, 0           \n"		\
	"       bf      1f              \n"		\
	"       br      5f              \n"		\
	"1:     cmplti  %0, 32          \n" /* 4W */	\
	"       bt      3f              \n"		\
	"8:     stw     %2, (%1, 0)     \n"		\
	"10:    stw     %2, (%1, 4)     \n"		\
	"11:    stw     %2, (%1, 8)     \n"		\
	"12:    stw     %2, (%1, 12)    \n"		\
	"13:    stw     %2, (%1, 16)    \n"		\
	"14:    stw     %2, (%1, 20)    \n"		\
	"15:    stw     %2, (%1, 24)    \n"		\
	"16:    stw     %2, (%1, 28)    \n"		\
	"       addi    %1, 32          \n"		\
	"       subi    %0, 32          \n"		\
	"       br      1b              \n"		\
	"3:     cmplti  %0, 4           \n" /* 1W */	\
	"       bt      5f              \n"		\
	"4:     stw     %2, (%1, 0)     \n"		\
	"       addi    %1, 4           \n"		\
	"       subi    %0, 4           \n"		\
	"       br      3b              \n"		\
	"5:     cmpnei  %0, 0           \n" /* 1B */	\
	"9:     bf      7f              \n"		\
	"6:     stb     %2, (%1, 0)     \n"		\
	"       addi    %1,  1          \n"		\
	"       subi    %0,  1          \n"		\
	"       br      5b              \n"		\
	".section __ex_table,\"a\"      \n"		\
	".align   2                     \n"		\
	".long    8b, 9b                \n"		\
	".long    10b, 9b               \n"		\
	".long    11b, 9b               \n"		\
	".long    12b, 9b               \n"		\
	".long    13b, 9b               \n"		\
	".long    14b, 9b               \n"		\
	".long    15b, 9b               \n"		\
	".long    16b, 9b               \n"		\
	".long    4b, 9b                \n"		\
	".long    6b, 9b                \n"		\
	".previous                      \n"		\
	"7:                             \n"		\
	: "=r"(size), "=r" (__d0),			\
	  "=r"(zvalue), "=r"(tmp)			\
	: "0"(size), "1"(addr), "2"(0)			\
	: "memory", "cc");				\
} while (0)

/*
 * clear_user: - Zero a block of memory in user space.
 * @to:   Destination address, in user space.
 * @n:    Number of bytes to zero.
 *
 * Zero a block of memory in user space.
 *
 * Returns number of bytes that could not be cleared.
 * On success, this will be zero.
 */
unsigned long
clear_user(void __user *to, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		__do_clear_user(to, n);
	return n;
}
EXPORT_SYMBOL(clear_user);

/*
 * __clear_user: - Zero a block of memory in user space, with less checking.
 * @to:   Destination address, in user space.
 * @n:    Number of bytes to zero.
 *
 * Zero a block of memory in user space.  Caller must check
 * the specified block with access_ok() before calling this function.
 *
 * Returns number of bytes that could not be cleared.
 * On success, this will be zero.
 */
unsigned long
__clear_user(void __user *to, unsigned long n)
{
	__do_clear_user(to, n);
	return n;
}
EXPORT_SYMBOL(__clear_user);
