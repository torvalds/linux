/*
 * User address space access functions.
 * The non inlined parts of asm-i386/uaccess.h are here.
 *
 * Copyright 1997 Andi Kleen <ak@muc.de>
 * Copyright 1997 Linus Torvalds
 */
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/backing-dev.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>
#include <asm/mmx.h>
#include <asm/asm.h>

#ifdef CONFIG_X86_INTEL_USERCOPY
/*
 * Alignment at which movsl is preferred for bulk memory copies.
 */
struct movsl_mask movsl_mask __read_mostly;
#endif

static inline int __movsl_is_ok(unsigned long a1, unsigned long a2, unsigned long n)
{
#ifdef CONFIG_X86_INTEL_USERCOPY
	if (n >= 64 && ((a1 ^ a2) & movsl_mask.mask))
		return 0;
#endif
	return 1;
}
#define movsl_is_ok(a1, a2, n) \
	__movsl_is_ok((unsigned long)(a1), (unsigned long)(a2), (n))

/*
 * Zero Userspace
 */

#define __do_clear_user(addr,size)					\
do {									\
	int __d0;							\
	might_fault();							\
	__asm__ __volatile__(						\
		ASM_STAC "\n"						\
		"0:	rep; stosl\n"					\
		"	movl %2,%0\n"					\
		"1:	rep; stosb\n"					\
		"2: " ASM_CLAC "\n"					\
		".section .fixup,\"ax\"\n"				\
		"3:	lea 0(%2,%0,4),%0\n"				\
		"	jmp 2b\n"					\
		".previous\n"						\
		_ASM_EXTABLE(0b,3b)					\
		_ASM_EXTABLE(1b,2b)					\
		: "=&c"(size), "=&D" (__d0)				\
		: "r"(size & 3), "0"(size / 4), "1"(addr), "a"(0));	\
} while (0)

/**
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
	might_fault();
	if (access_ok(VERIFY_WRITE, to, n))
		__do_clear_user(to, n);
	return n;
}
EXPORT_SYMBOL(clear_user);

/**
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

#ifdef CONFIG_X86_INTEL_USERCOPY
static unsigned long
__copy_user_intel(void __user *to, const void *from, unsigned long size)
{
	int d0, d1;
	__asm__ __volatile__(
		       "       .align 2,0x90\n"
		       "1:     movl 32(%4), %%eax\n"
		       "       cmpl $67, %0\n"
		       "       jbe 3f\n"
		       "2:     movl 64(%4), %%eax\n"
		       "       .align 2,0x90\n"
		       "3:     movl 0(%4), %%eax\n"
		       "4:     movl 4(%4), %%edx\n"
		       "5:     movl %%eax, 0(%3)\n"
		       "6:     movl %%edx, 4(%3)\n"
		       "7:     movl 8(%4), %%eax\n"
		       "8:     movl 12(%4),%%edx\n"
		       "9:     movl %%eax, 8(%3)\n"
		       "10:    movl %%edx, 12(%3)\n"
		       "11:    movl 16(%4), %%eax\n"
		       "12:    movl 20(%4), %%edx\n"
		       "13:    movl %%eax, 16(%3)\n"
		       "14:    movl %%edx, 20(%3)\n"
		       "15:    movl 24(%4), %%eax\n"
		       "16:    movl 28(%4), %%edx\n"
		       "17:    movl %%eax, 24(%3)\n"
		       "18:    movl %%edx, 28(%3)\n"
		       "19:    movl 32(%4), %%eax\n"
		       "20:    movl 36(%4), %%edx\n"
		       "21:    movl %%eax, 32(%3)\n"
		       "22:    movl %%edx, 36(%3)\n"
		       "23:    movl 40(%4), %%eax\n"
		       "24:    movl 44(%4), %%edx\n"
		       "25:    movl %%eax, 40(%3)\n"
		       "26:    movl %%edx, 44(%3)\n"
		       "27:    movl 48(%4), %%eax\n"
		       "28:    movl 52(%4), %%edx\n"
		       "29:    movl %%eax, 48(%3)\n"
		       "30:    movl %%edx, 52(%3)\n"
		       "31:    movl 56(%4), %%eax\n"
		       "32:    movl 60(%4), %%edx\n"
		       "33:    movl %%eax, 56(%3)\n"
		       "34:    movl %%edx, 60(%3)\n"
		       "       addl $-64, %0\n"
		       "       addl $64, %4\n"
		       "       addl $64, %3\n"
		       "       cmpl $63, %0\n"
		       "       ja  1b\n"
		       "35:    movl  %0, %%eax\n"
		       "       shrl  $2, %0\n"
		       "       andl  $3, %%eax\n"
		       "       cld\n"
		       "99:    rep; movsl\n"
		       "36:    movl %%eax, %0\n"
		       "37:    rep; movsb\n"
		       "100:\n"
		       ".section .fixup,\"ax\"\n"
		       "101:   lea 0(%%eax,%0,4),%0\n"
		       "       jmp 100b\n"
		       ".previous\n"
		       _ASM_EXTABLE(1b,100b)
		       _ASM_EXTABLE(2b,100b)
		       _ASM_EXTABLE(3b,100b)
		       _ASM_EXTABLE(4b,100b)
		       _ASM_EXTABLE(5b,100b)
		       _ASM_EXTABLE(6b,100b)
		       _ASM_EXTABLE(7b,100b)
		       _ASM_EXTABLE(8b,100b)
		       _ASM_EXTABLE(9b,100b)
		       _ASM_EXTABLE(10b,100b)
		       _ASM_EXTABLE(11b,100b)
		       _ASM_EXTABLE(12b,100b)
		       _ASM_EXTABLE(13b,100b)
		       _ASM_EXTABLE(14b,100b)
		       _ASM_EXTABLE(15b,100b)
		       _ASM_EXTABLE(16b,100b)
		       _ASM_EXTABLE(17b,100b)
		       _ASM_EXTABLE(18b,100b)
		       _ASM_EXTABLE(19b,100b)
		       _ASM_EXTABLE(20b,100b)
		       _ASM_EXTABLE(21b,100b)
		       _ASM_EXTABLE(22b,100b)
		       _ASM_EXTABLE(23b,100b)
		       _ASM_EXTABLE(24b,100b)
		       _ASM_EXTABLE(25b,100b)
		       _ASM_EXTABLE(26b,100b)
		       _ASM_EXTABLE(27b,100b)
		       _ASM_EXTABLE(28b,100b)
		       _ASM_EXTABLE(29b,100b)
		       _ASM_EXTABLE(30b,100b)
		       _ASM_EXTABLE(31b,100b)
		       _ASM_EXTABLE(32b,100b)
		       _ASM_EXTABLE(33b,100b)
		       _ASM_EXTABLE(34b,100b)
		       _ASM_EXTABLE(35b,100b)
		       _ASM_EXTABLE(36b,100b)
		       _ASM_EXTABLE(37b,100b)
		       _ASM_EXTABLE(99b,101b)
		       : "=&c"(size), "=&D" (d0), "=&S" (d1)
		       :  "1"(to), "2"(from), "0"(size)
		       : "eax", "edx", "memory");
	return size;
}

static unsigned long
__copy_user_zeroing_intel(void *to, const void __user *from, unsigned long size)
{
	int d0, d1;
	__asm__ __volatile__(
		       "        .align 2,0x90\n"
		       "0:      movl 32(%4), %%eax\n"
		       "        cmpl $67, %0\n"
		       "        jbe 2f\n"
		       "1:      movl 64(%4), %%eax\n"
		       "        .align 2,0x90\n"
		       "2:      movl 0(%4), %%eax\n"
		       "21:     movl 4(%4), %%edx\n"
		       "        movl %%eax, 0(%3)\n"
		       "        movl %%edx, 4(%3)\n"
		       "3:      movl 8(%4), %%eax\n"
		       "31:     movl 12(%4),%%edx\n"
		       "        movl %%eax, 8(%3)\n"
		       "        movl %%edx, 12(%3)\n"
		       "4:      movl 16(%4), %%eax\n"
		       "41:     movl 20(%4), %%edx\n"
		       "        movl %%eax, 16(%3)\n"
		       "        movl %%edx, 20(%3)\n"
		       "10:     movl 24(%4), %%eax\n"
		       "51:     movl 28(%4), %%edx\n"
		       "        movl %%eax, 24(%3)\n"
		       "        movl %%edx, 28(%3)\n"
		       "11:     movl 32(%4), %%eax\n"
		       "61:     movl 36(%4), %%edx\n"
		       "        movl %%eax, 32(%3)\n"
		       "        movl %%edx, 36(%3)\n"
		       "12:     movl 40(%4), %%eax\n"
		       "71:     movl 44(%4), %%edx\n"
		       "        movl %%eax, 40(%3)\n"
		       "        movl %%edx, 44(%3)\n"
		       "13:     movl 48(%4), %%eax\n"
		       "81:     movl 52(%4), %%edx\n"
		       "        movl %%eax, 48(%3)\n"
		       "        movl %%edx, 52(%3)\n"
		       "14:     movl 56(%4), %%eax\n"
		       "91:     movl 60(%4), %%edx\n"
		       "        movl %%eax, 56(%3)\n"
		       "        movl %%edx, 60(%3)\n"
		       "        addl $-64, %0\n"
		       "        addl $64, %4\n"
		       "        addl $64, %3\n"
		       "        cmpl $63, %0\n"
		       "        ja  0b\n"
		       "5:      movl  %0, %%eax\n"
		       "        shrl  $2, %0\n"
		       "        andl $3, %%eax\n"
		       "        cld\n"
		       "6:      rep; movsl\n"
		       "        movl %%eax,%0\n"
		       "7:      rep; movsb\n"
		       "8:\n"
		       ".section .fixup,\"ax\"\n"
		       "9:      lea 0(%%eax,%0,4),%0\n"
		       "16:     pushl %0\n"
		       "        pushl %%eax\n"
		       "        xorl %%eax,%%eax\n"
		       "        rep; stosb\n"
		       "        popl %%eax\n"
		       "        popl %0\n"
		       "        jmp 8b\n"
		       ".previous\n"
		       _ASM_EXTABLE(0b,16b)
		       _ASM_EXTABLE(1b,16b)
		       _ASM_EXTABLE(2b,16b)
		       _ASM_EXTABLE(21b,16b)
		       _ASM_EXTABLE(3b,16b)
		       _ASM_EXTABLE(31b,16b)
		       _ASM_EXTABLE(4b,16b)
		       _ASM_EXTABLE(41b,16b)
		       _ASM_EXTABLE(10b,16b)
		       _ASM_EXTABLE(51b,16b)
		       _ASM_EXTABLE(11b,16b)
		       _ASM_EXTABLE(61b,16b)
		       _ASM_EXTABLE(12b,16b)
		       _ASM_EXTABLE(71b,16b)
		       _ASM_EXTABLE(13b,16b)
		       _ASM_EXTABLE(81b,16b)
		       _ASM_EXTABLE(14b,16b)
		       _ASM_EXTABLE(91b,16b)
		       _ASM_EXTABLE(6b,9b)
		       _ASM_EXTABLE(7b,16b)
		       : "=&c"(size), "=&D" (d0), "=&S" (d1)
		       :  "1"(to), "2"(from), "0"(size)
		       : "eax", "edx", "memory");
	return size;
}

/*
 * Non Temporal Hint version of __copy_user_zeroing_intel.  It is cache aware.
 * hyoshiok@miraclelinux.com
 */

static unsigned long __copy_user_zeroing_intel_nocache(void *to,
				const void __user *from, unsigned long size)
{
	int d0, d1;

	__asm__ __volatile__(
	       "        .align 2,0x90\n"
	       "0:      movl 32(%4), %%eax\n"
	       "        cmpl $67, %0\n"
	       "        jbe 2f\n"
	       "1:      movl 64(%4), %%eax\n"
	       "        .align 2,0x90\n"
	       "2:      movl 0(%4), %%eax\n"
	       "21:     movl 4(%4), %%edx\n"
	       "        movnti %%eax, 0(%3)\n"
	       "        movnti %%edx, 4(%3)\n"
	       "3:      movl 8(%4), %%eax\n"
	       "31:     movl 12(%4),%%edx\n"
	       "        movnti %%eax, 8(%3)\n"
	       "        movnti %%edx, 12(%3)\n"
	       "4:      movl 16(%4), %%eax\n"
	       "41:     movl 20(%4), %%edx\n"
	       "        movnti %%eax, 16(%3)\n"
	       "        movnti %%edx, 20(%3)\n"
	       "10:     movl 24(%4), %%eax\n"
	       "51:     movl 28(%4), %%edx\n"
	       "        movnti %%eax, 24(%3)\n"
	       "        movnti %%edx, 28(%3)\n"
	       "11:     movl 32(%4), %%eax\n"
	       "61:     movl 36(%4), %%edx\n"
	       "        movnti %%eax, 32(%3)\n"
	       "        movnti %%edx, 36(%3)\n"
	       "12:     movl 40(%4), %%eax\n"
	       "71:     movl 44(%4), %%edx\n"
	       "        movnti %%eax, 40(%3)\n"
	       "        movnti %%edx, 44(%3)\n"
	       "13:     movl 48(%4), %%eax\n"
	       "81:     movl 52(%4), %%edx\n"
	       "        movnti %%eax, 48(%3)\n"
	       "        movnti %%edx, 52(%3)\n"
	       "14:     movl 56(%4), %%eax\n"
	       "91:     movl 60(%4), %%edx\n"
	       "        movnti %%eax, 56(%3)\n"
	       "        movnti %%edx, 60(%3)\n"
	       "        addl $-64, %0\n"
	       "        addl $64, %4\n"
	       "        addl $64, %3\n"
	       "        cmpl $63, %0\n"
	       "        ja  0b\n"
	       "        sfence \n"
	       "5:      movl  %0, %%eax\n"
	       "        shrl  $2, %0\n"
	       "        andl $3, %%eax\n"
	       "        cld\n"
	       "6:      rep; movsl\n"
	       "        movl %%eax,%0\n"
	       "7:      rep; movsb\n"
	       "8:\n"
	       ".section .fixup,\"ax\"\n"
	       "9:      lea 0(%%eax,%0,4),%0\n"
	       "16:     pushl %0\n"
	       "        pushl %%eax\n"
	       "        xorl %%eax,%%eax\n"
	       "        rep; stosb\n"
	       "        popl %%eax\n"
	       "        popl %0\n"
	       "        jmp 8b\n"
	       ".previous\n"
	       _ASM_EXTABLE(0b,16b)
	       _ASM_EXTABLE(1b,16b)
	       _ASM_EXTABLE(2b,16b)
	       _ASM_EXTABLE(21b,16b)
	       _ASM_EXTABLE(3b,16b)
	       _ASM_EXTABLE(31b,16b)
	       _ASM_EXTABLE(4b,16b)
	       _ASM_EXTABLE(41b,16b)
	       _ASM_EXTABLE(10b,16b)
	       _ASM_EXTABLE(51b,16b)
	       _ASM_EXTABLE(11b,16b)
	       _ASM_EXTABLE(61b,16b)
	       _ASM_EXTABLE(12b,16b)
	       _ASM_EXTABLE(71b,16b)
	       _ASM_EXTABLE(13b,16b)
	       _ASM_EXTABLE(81b,16b)
	       _ASM_EXTABLE(14b,16b)
	       _ASM_EXTABLE(91b,16b)
	       _ASM_EXTABLE(6b,9b)
	       _ASM_EXTABLE(7b,16b)
	       : "=&c"(size), "=&D" (d0), "=&S" (d1)
	       :  "1"(to), "2"(from), "0"(size)
	       : "eax", "edx", "memory");
	return size;
}

static unsigned long __copy_user_intel_nocache(void *to,
				const void __user *from, unsigned long size)
{
	int d0, d1;

	__asm__ __volatile__(
	       "        .align 2,0x90\n"
	       "0:      movl 32(%4), %%eax\n"
	       "        cmpl $67, %0\n"
	       "        jbe 2f\n"
	       "1:      movl 64(%4), %%eax\n"
	       "        .align 2,0x90\n"
	       "2:      movl 0(%4), %%eax\n"
	       "21:     movl 4(%4), %%edx\n"
	       "        movnti %%eax, 0(%3)\n"
	       "        movnti %%edx, 4(%3)\n"
	       "3:      movl 8(%4), %%eax\n"
	       "31:     movl 12(%4),%%edx\n"
	       "        movnti %%eax, 8(%3)\n"
	       "        movnti %%edx, 12(%3)\n"
	       "4:      movl 16(%4), %%eax\n"
	       "41:     movl 20(%4), %%edx\n"
	       "        movnti %%eax, 16(%3)\n"
	       "        movnti %%edx, 20(%3)\n"
	       "10:     movl 24(%4), %%eax\n"
	       "51:     movl 28(%4), %%edx\n"
	       "        movnti %%eax, 24(%3)\n"
	       "        movnti %%edx, 28(%3)\n"
	       "11:     movl 32(%4), %%eax\n"
	       "61:     movl 36(%4), %%edx\n"
	       "        movnti %%eax, 32(%3)\n"
	       "        movnti %%edx, 36(%3)\n"
	       "12:     movl 40(%4), %%eax\n"
	       "71:     movl 44(%4), %%edx\n"
	       "        movnti %%eax, 40(%3)\n"
	       "        movnti %%edx, 44(%3)\n"
	       "13:     movl 48(%4), %%eax\n"
	       "81:     movl 52(%4), %%edx\n"
	       "        movnti %%eax, 48(%3)\n"
	       "        movnti %%edx, 52(%3)\n"
	       "14:     movl 56(%4), %%eax\n"
	       "91:     movl 60(%4), %%edx\n"
	       "        movnti %%eax, 56(%3)\n"
	       "        movnti %%edx, 60(%3)\n"
	       "        addl $-64, %0\n"
	       "        addl $64, %4\n"
	       "        addl $64, %3\n"
	       "        cmpl $63, %0\n"
	       "        ja  0b\n"
	       "        sfence \n"
	       "5:      movl  %0, %%eax\n"
	       "        shrl  $2, %0\n"
	       "        andl $3, %%eax\n"
	       "        cld\n"
	       "6:      rep; movsl\n"
	       "        movl %%eax,%0\n"
	       "7:      rep; movsb\n"
	       "8:\n"
	       ".section .fixup,\"ax\"\n"
	       "9:      lea 0(%%eax,%0,4),%0\n"
	       "16:     jmp 8b\n"
	       ".previous\n"
	       _ASM_EXTABLE(0b,16b)
	       _ASM_EXTABLE(1b,16b)
	       _ASM_EXTABLE(2b,16b)
	       _ASM_EXTABLE(21b,16b)
	       _ASM_EXTABLE(3b,16b)
	       _ASM_EXTABLE(31b,16b)
	       _ASM_EXTABLE(4b,16b)
	       _ASM_EXTABLE(41b,16b)
	       _ASM_EXTABLE(10b,16b)
	       _ASM_EXTABLE(51b,16b)
	       _ASM_EXTABLE(11b,16b)
	       _ASM_EXTABLE(61b,16b)
	       _ASM_EXTABLE(12b,16b)
	       _ASM_EXTABLE(71b,16b)
	       _ASM_EXTABLE(13b,16b)
	       _ASM_EXTABLE(81b,16b)
	       _ASM_EXTABLE(14b,16b)
	       _ASM_EXTABLE(91b,16b)
	       _ASM_EXTABLE(6b,9b)
	       _ASM_EXTABLE(7b,16b)
	       : "=&c"(size), "=&D" (d0), "=&S" (d1)
	       :  "1"(to), "2"(from), "0"(size)
	       : "eax", "edx", "memory");
	return size;
}

#else

/*
 * Leave these declared but undefined.  They should not be any references to
 * them
 */
unsigned long __copy_user_zeroing_intel(void *to, const void __user *from,
					unsigned long size);
unsigned long __copy_user_intel(void __user *to, const void *from,
					unsigned long size);
unsigned long __copy_user_zeroing_intel_nocache(void *to,
				const void __user *from, unsigned long size);
#endif /* CONFIG_X86_INTEL_USERCOPY */

/* Generic arbitrary sized copy.  */
#define __copy_user(to, from, size)					\
do {									\
	int __d0, __d1, __d2;						\
	__asm__ __volatile__(						\
		"	cmp  $7,%0\n"					\
		"	jbe  1f\n"					\
		"	movl %1,%0\n"					\
		"	negl %0\n"					\
		"	andl $7,%0\n"					\
		"	subl %0,%3\n"					\
		"4:	rep; movsb\n"					\
		"	movl %3,%0\n"					\
		"	shrl $2,%0\n"					\
		"	andl $3,%3\n"					\
		"	.align 2,0x90\n"				\
		"0:	rep; movsl\n"					\
		"	movl %3,%0\n"					\
		"1:	rep; movsb\n"					\
		"2:\n"							\
		".section .fixup,\"ax\"\n"				\
		"5:	addl %3,%0\n"					\
		"	jmp 2b\n"					\
		"3:	lea 0(%3,%0,4),%0\n"				\
		"	jmp 2b\n"					\
		".previous\n"						\
		_ASM_EXTABLE(4b,5b)					\
		_ASM_EXTABLE(0b,3b)					\
		_ASM_EXTABLE(1b,2b)					\
		: "=&c"(size), "=&D" (__d0), "=&S" (__d1), "=r"(__d2)	\
		: "3"(size), "0"(size), "1"(to), "2"(from)		\
		: "memory");						\
} while (0)

#define __copy_user_zeroing(to, from, size)				\
do {									\
	int __d0, __d1, __d2;						\
	__asm__ __volatile__(						\
		"	cmp  $7,%0\n"					\
		"	jbe  1f\n"					\
		"	movl %1,%0\n"					\
		"	negl %0\n"					\
		"	andl $7,%0\n"					\
		"	subl %0,%3\n"					\
		"4:	rep; movsb\n"					\
		"	movl %3,%0\n"					\
		"	shrl $2,%0\n"					\
		"	andl $3,%3\n"					\
		"	.align 2,0x90\n"				\
		"0:	rep; movsl\n"					\
		"	movl %3,%0\n"					\
		"1:	rep; movsb\n"					\
		"2:\n"							\
		".section .fixup,\"ax\"\n"				\
		"5:	addl %3,%0\n"					\
		"	jmp 6f\n"					\
		"3:	lea 0(%3,%0,4),%0\n"				\
		"6:	pushl %0\n"					\
		"	pushl %%eax\n"					\
		"	xorl %%eax,%%eax\n"				\
		"	rep; stosb\n"					\
		"	popl %%eax\n"					\
		"	popl %0\n"					\
		"	jmp 2b\n"					\
		".previous\n"						\
		_ASM_EXTABLE(4b,5b)					\
		_ASM_EXTABLE(0b,3b)					\
		_ASM_EXTABLE(1b,6b)					\
		: "=&c"(size), "=&D" (__d0), "=&S" (__d1), "=r"(__d2)	\
		: "3"(size), "0"(size), "1"(to), "2"(from)		\
		: "memory");						\
} while (0)

unsigned long __copy_to_user_ll(void __user *to, const void *from,
				unsigned long n)
{
	__uaccess_begin_nospec();
	if (movsl_is_ok(to, from, n))
		__copy_user(to, from, n);
	else
		n = __copy_user_intel(to, from, n);
	__uaccess_end();
	return n;
}
EXPORT_SYMBOL(__copy_to_user_ll);

unsigned long __copy_from_user_ll(void *to, const void __user *from,
					unsigned long n)
{
	__uaccess_begin_nospec();
	if (movsl_is_ok(to, from, n))
		__copy_user_zeroing(to, from, n);
	else
		n = __copy_user_zeroing_intel(to, from, n);
	__uaccess_end();
	return n;
}
EXPORT_SYMBOL(__copy_from_user_ll);

unsigned long __copy_from_user_ll_nozero(void *to, const void __user *from,
					 unsigned long n)
{
	__uaccess_begin_nospec();
	if (movsl_is_ok(to, from, n))
		__copy_user(to, from, n);
	else
		n = __copy_user_intel((void __user *)to,
				      (const void *)from, n);
	__uaccess_end();
	return n;
}
EXPORT_SYMBOL(__copy_from_user_ll_nozero);

unsigned long __copy_from_user_ll_nocache(void *to, const void __user *from,
					unsigned long n)
{
	__uaccess_begin_nospec();
#ifdef CONFIG_X86_INTEL_USERCOPY
	if (n > 64 && cpu_has_xmm2)
		n = __copy_user_zeroing_intel_nocache(to, from, n);
	else
		__copy_user_zeroing(to, from, n);
#else
	__copy_user_zeroing(to, from, n);
#endif
	__uaccess_end();
	return n;
}
EXPORT_SYMBOL(__copy_from_user_ll_nocache);

unsigned long __copy_from_user_ll_nocache_nozero(void *to, const void __user *from,
					unsigned long n)
{
	__uaccess_begin_nospec();
#ifdef CONFIG_X86_INTEL_USERCOPY
	if (n > 64 && cpu_has_xmm2)
		n = __copy_user_intel_nocache(to, from, n);
	else
		__copy_user(to, from, n);
#else
	__copy_user(to, from, n);
#endif
	__uaccess_end();
	return n;
}
EXPORT_SYMBOL(__copy_from_user_ll_nocache_nozero);

/**
 * copy_to_user: - Copy a block of data into user space.
 * @to:   Destination address, in user space.
 * @from: Source address, in kernel space.
 * @n:    Number of bytes to copy.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
 *
 * Copy data from kernel space to user space.
 *
 * Returns number of bytes that could not be copied.
 * On success, this will be zero.
 */
unsigned long _copy_to_user(void __user *to, const void *from, unsigned n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		n = __copy_to_user(to, from, n);
	return n;
}
EXPORT_SYMBOL(_copy_to_user);

/**
 * copy_from_user: - Copy a block of data from user space.
 * @to:   Destination address, in kernel space.
 * @from: Source address, in user space.
 * @n:    Number of bytes to copy.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
 *
 * Copy data from user space to kernel space.
 *
 * Returns number of bytes that could not be copied.
 * On success, this will be zero.
 *
 * If some data could not be copied, this function will pad the copied
 * data to the requested size using zero bytes.
 */
unsigned long _copy_from_user(void *to, const void __user *from, unsigned n)
{
	if (access_ok(VERIFY_READ, from, n))
		n = __copy_from_user(to, from, n);
	else
		memset(to, 0, n);
	return n;
}
EXPORT_SYMBOL(_copy_from_user);
