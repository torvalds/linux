// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/uaccess.h>
#include <linux/types.h>

unsigned long raw_copy_from_user(void *to, const void *from,
			unsigned long n)
{
	int tmp, nsave;

	__asm__ __volatile__(
	"0:     cmpnei  %1, 0           \n"
	"       bf      7f              \n"
	"       mov     %3, %1          \n"
	"       or      %3, %2          \n"
	"       andi    %3, 3           \n"
	"       cmpnei  %3, 0           \n"
	"       bf      1f              \n"
	"       br      5f              \n"
	"1:     cmplti  %0, 16          \n"
	"       bt      3f              \n"
	"2:     ldw     %3, (%2, 0)     \n"
	"10:    ldw     %4, (%2, 4)     \n"
	"       stw     %3, (%1, 0)     \n"
	"       stw     %4, (%1, 4)     \n"
	"11:    ldw     %3, (%2, 8)     \n"
	"12:    ldw     %4, (%2, 12)    \n"
	"       stw     %3, (%1, 8)     \n"
	"       stw     %4, (%1, 12)    \n"
	"       addi    %2, 16          \n"
	"       addi    %1, 16          \n"
	"       subi    %0, 16          \n"
	"       br      1b              \n"
	"3:     cmplti  %0, 4           \n"
	"       bt      5f              \n"
	"4:     ldw     %3, (%2, 0)     \n"
	"       stw     %3, (%1, 0)     \n"
	"       addi    %2, 4           \n"
	"       addi    %1, 4           \n"
	"       subi    %0, 4           \n"
	"       br      3b              \n"
	"5:     cmpnei  %0, 0           \n"
	"       bf      7f              \n"
	"6:     ldb     %3, (%2, 0)     \n"
	"       stb     %3, (%1, 0)     \n"
	"       addi    %2,  1          \n"
	"       addi    %1,  1          \n"
	"       subi    %0,  1          \n"
	"       br      5b              \n"
	"8:     stw     %3, (%1, 0)     \n"
	"       subi    %0, 4           \n"
	"       bf      7f              \n"
	"9:     subi    %0, 8           \n"
	"       bf      7f              \n"
	"13:    stw     %3, (%1, 8)     \n"
	"       subi    %0, 12          \n"
	"       bf      7f              \n"
	".section __ex_table, \"a\"     \n"
	".align   2                     \n"
	".long    2b, 7f                \n"
	".long    4b, 7f                \n"
	".long    6b, 7f                \n"
	".long   10b, 8b                \n"
	".long   11b, 9b                \n"
	".long   12b,13b                \n"
	".previous                      \n"
	"7:                             \n"
	: "=r"(n), "=r"(to), "=r"(from), "=r"(nsave),
	  "=r"(tmp)
	: "0"(n), "1"(to), "2"(from)
	: "memory");

	return n;
}
EXPORT_SYMBOL(raw_copy_from_user);

unsigned long raw_copy_to_user(void *to, const void *from,
			unsigned long n)
{
	int w0, w1, w2, w3;

	__asm__ __volatile__(
	"0:     cmpnei  %1, 0           \n"
	"       bf      8f              \n"
	"       mov     %3, %1          \n"
	"       or      %3, %2          \n"
	"       andi    %3, 3           \n"
	"       cmpnei  %3, 0           \n"
	"       bf      1f              \n"
	"       br      5f              \n"
	"1:     cmplti  %0, 16          \n" /* 4W */
	"       bt      3f              \n"
	"       ldw     %3, (%2, 0)     \n"
	"       ldw     %4, (%2, 4)     \n"
	"       ldw     %5, (%2, 8)     \n"
	"       ldw     %6, (%2, 12)    \n"
	"2:     stw     %3, (%1, 0)     \n"
	"9:     stw     %4, (%1, 4)     \n"
	"10:    stw     %5, (%1, 8)     \n"
	"11:    stw     %6, (%1, 12)    \n"
	"       addi    %2, 16          \n"
	"       addi    %1, 16          \n"
	"       subi    %0, 16          \n"
	"       br      1b              \n"
	"3:     cmplti  %0, 4           \n" /* 1W */
	"       bt      5f              \n"
	"       ldw     %3, (%2, 0)     \n"
	"4:     stw     %3, (%1, 0)     \n"
	"       addi    %2, 4           \n"
	"       addi    %1, 4           \n"
	"       subi    %0, 4           \n"
	"       br      3b              \n"
	"5:     cmpnei  %0, 0           \n"  /* 1B */
	"       bf      13f             \n"
	"       ldb     %3, (%2, 0)     \n"
	"6:     stb     %3, (%1, 0)     \n"
	"       addi    %2,  1          \n"
	"       addi    %1,  1          \n"
	"       subi    %0,  1          \n"
	"       br      5b              \n"
	"7:     subi	%0,  4          \n"
	"8:     subi	%0,  4          \n"
	"12:    subi	%0,  4          \n"
	"       br      13f             \n"
	".section __ex_table, \"a\"     \n"
	".align   2                     \n"
	".long    2b, 13f               \n"
	".long    4b, 13f               \n"
	".long    6b, 13f               \n"
	".long    9b, 12b               \n"
	".long   10b, 8b                \n"
	".long   11b, 7b                \n"
	".previous                      \n"
	"13:                            \n"
	: "=r"(n), "=r"(to), "=r"(from), "=r"(w0),
	  "=r"(w1), "=r"(w2), "=r"(w3)
	: "0"(n), "1"(to), "2"(from)
	: "memory");

	return n;
}
EXPORT_SYMBOL(raw_copy_to_user);

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
	long res, faultres;
	int tmp;

	__asm__ __volatile__(
	"       cmpnei  %3, 0           \n"
	"       bf      4f              \n"
	"1:     cmpnei  %1, 0          	\n"
	"       bf      5f              \n"
	"2:     ldb     %4, (%3, 0)     \n"
	"       stb     %4, (%2, 0)     \n"
	"       cmpnei  %4, 0           \n"
	"       bf      3f              \n"
	"       addi    %3,  1          \n"
	"       addi    %2,  1          \n"
	"       subi    %1,  1          \n"
	"       br      1b              \n"
	"3:     subu	%0, %1          \n"
	"       br      5f              \n"
	"4:     mov     %0, %5          \n"
	"       br      5f              \n"
	".section __ex_table, \"a\"     \n"
	".align   2                     \n"
	".long    2b, 4b                \n"
	".previous                      \n"
	"5:                             \n"
	: "=r"(res), "=r"(count), "=r"(dst),
	  "=r"(src), "=r"(tmp), "=r"(faultres)
	: "5"(-EFAULT), "0"(count), "1"(count),
	  "2"(dst), "3"(src)
	: "memory");

	return res;
}
EXPORT_SYMBOL(__strncpy_from_user);

/*
 * strnlen_user: - Get the size of a string in user space.
 * @str: The string to measure.
 * @n:   The maximum valid length
 *
 * Get the size of a NUL-terminated string in user space.
 *
 * Returns the size of the string INCLUDING the terminating NUL.
 * On exception, returns 0.
 * If the string is too long, returns a value greater than @n.
 */
long __strnlen_user(const char *s, long n)
{
	unsigned long res, tmp;

	__asm__ __volatile__(
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
	: "memory");

	return res;
}
EXPORT_SYMBOL(__strnlen_user);

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
	int data, value, tmp;

	__asm__ __volatile__(
	"0:     cmpnei  %1, 0           \n"
	"       bf      7f              \n"
	"       mov     %3, %1          \n"
	"       andi    %3, 3           \n"
	"       cmpnei  %3, 0           \n"
	"       bf      1f              \n"
	"       br      5f              \n"
	"1:     cmplti  %0, 32          \n" /* 4W */
	"       bt      3f              \n"
	"8:     stw     %2, (%1, 0)     \n"
	"10:    stw     %2, (%1, 4)     \n"
	"11:    stw     %2, (%1, 8)     \n"
	"12:    stw     %2, (%1, 12)    \n"
	"13:    stw     %2, (%1, 16)    \n"
	"14:    stw     %2, (%1, 20)    \n"
	"15:    stw     %2, (%1, 24)    \n"
	"16:    stw     %2, (%1, 28)    \n"
	"       addi    %1, 32          \n"
	"       subi    %0, 32          \n"
	"       br      1b              \n"
	"3:     cmplti  %0, 4           \n" /* 1W */
	"       bt      5f              \n"
	"4:     stw     %2, (%1, 0)     \n"
	"       addi    %1, 4           \n"
	"       subi    %0, 4           \n"
	"       br      3b              \n"
	"5:     cmpnei  %0, 0           \n" /* 1B */
	"9:     bf      7f              \n"
	"6:     stb     %2, (%1, 0)     \n"
	"       addi    %1,  1          \n"
	"       subi    %0,  1          \n"
	"       br      5b              \n"
	".section __ex_table,\"a\"      \n"
	".align   2                     \n"
	".long    8b, 9b                \n"
	".long    10b, 9b               \n"
	".long    11b, 9b               \n"
	".long    12b, 9b               \n"
	".long    13b, 9b               \n"
	".long    14b, 9b               \n"
	".long    15b, 9b               \n"
	".long    16b, 9b               \n"
	".long    4b, 9b                \n"
	".long    6b, 9b                \n"
	".previous                      \n"
	"7:                             \n"
	: "=r"(n), "=r" (data), "=r"(value), "=r"(tmp)
	: "0"(n), "1"(to), "2"(0)
	: "memory");

	return n;
}
EXPORT_SYMBOL(__clear_user);
