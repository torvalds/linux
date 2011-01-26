/* Normally compiler builtins are used, but sometimes the compiler calls out
   of line code. Based on asm-i386/string.h.
 */
#define _STRING_C
#include <linux/string.h>
#include <linux/module.h>

#undef memmove
void *memmove(void *dest, const void *src, size_t count)
{
	unsigned long d0,d1,d2,d3,d4,d5,d6,d7;
	char *ret;

	__asm__ __volatile__(
		/* Handle more 32bytes in loop */
		"mov %2, %3\n\t"
		"cmp $0x20, %0\n\t"
		"jb	1f\n\t"

		/* Decide forward/backward copy mode */
		"cmp %2, %1\n\t"
		"jb	2f\n\t"

		/*
		 * movsq instruction have many startup latency
		 * so we handle small size by general register.
		 */
		"cmp  $680, %0\n\t"
		"jb 3f\n\t"
		/*
		 * movsq instruction is only good for aligned case.
		 */
		"cmpb %%dil, %%sil\n\t"
		"je 4f\n\t"
		"3:\n\t"
		"sub $0x20, %0\n\t"
		/*
		 * We gobble 32byts forward in each loop.
		 */
		"5:\n\t"
		"sub $0x20, %0\n\t"
		"movq 0*8(%1), %4\n\t"
		"movq 1*8(%1), %5\n\t"
		"movq 2*8(%1), %6\n\t"
		"movq 3*8(%1), %7\n\t"
		"leaq 4*8(%1), %1\n\t"

		"movq %4, 0*8(%2)\n\t"
		"movq %5, 1*8(%2)\n\t"
		"movq %6, 2*8(%2)\n\t"
		"movq %7, 3*8(%2)\n\t"
		"leaq 4*8(%2), %2\n\t"
		"jae 5b\n\t"
		"addq $0x20, %0\n\t"
		"jmp 1f\n\t"
		/*
		 * Handle data forward by movsq.
		 */
		".p2align 4\n\t"
		"4:\n\t"
		"movq %0, %8\n\t"
		"movq -8(%1, %0), %4\n\t"
		"lea -8(%2, %0), %5\n\t"
		"shrq $3, %8\n\t"
		"rep movsq\n\t"
		"movq %4, (%5)\n\t"
		"jmp 13f\n\t"
		/*
		 * Handle data backward by movsq.
		 */
		".p2align 4\n\t"
		"7:\n\t"
		"movq %0, %8\n\t"
		"movq (%1), %4\n\t"
		"movq %2, %5\n\t"
		"leaq -8(%1, %0), %1\n\t"
		"leaq -8(%2, %0), %2\n\t"
		"shrq $3, %8\n\t"
		"std\n\t"
		"rep movsq\n\t"
		"cld\n\t"
		"movq %4, (%5)\n\t"
		"jmp 13f\n\t"

		/*
		 * Start to prepare for backward copy.
		 */
		".p2align 4\n\t"
		"2:\n\t"
		"cmp $680, %0\n\t"
		"jb 6f \n\t"
		"cmp %%dil, %%sil\n\t"
		"je 7b \n\t"
		"6:\n\t"
		/*
		 * Calculate copy position to tail.
		 */
		"addq %0, %1\n\t"
		"addq %0, %2\n\t"
		"subq $0x20, %0\n\t"
		/*
		 * We gobble 32byts backward in each loop.
		 */
		"8:\n\t"
		"subq $0x20, %0\n\t"
		"movq -1*8(%1), %4\n\t"
		"movq -2*8(%1), %5\n\t"
		"movq -3*8(%1), %6\n\t"
		"movq -4*8(%1), %7\n\t"
		"leaq -4*8(%1), %1\n\t"

		"movq %4, -1*8(%2)\n\t"
		"movq %5, -2*8(%2)\n\t"
		"movq %6, -3*8(%2)\n\t"
		"movq %7, -4*8(%2)\n\t"
		"leaq -4*8(%2), %2\n\t"
		"jae 8b\n\t"
		/*
		 * Calculate copy position to head.
		 */
		"addq $0x20, %0\n\t"
		"subq %0, %1\n\t"
		"subq %0, %2\n\t"
		"1:\n\t"
		"cmpq $16, %0\n\t"
		"jb 9f\n\t"
		/*
		 * Move data from 16 bytes to 31 bytes.
		 */
		"movq 0*8(%1), %4\n\t"
		"movq 1*8(%1), %5\n\t"
		"movq -2*8(%1, %0), %6\n\t"
		"movq -1*8(%1, %0), %7\n\t"
		"movq %4, 0*8(%2)\n\t"
		"movq %5, 1*8(%2)\n\t"
		"movq %6, -2*8(%2, %0)\n\t"
		"movq %7, -1*8(%2, %0)\n\t"
		"jmp 13f\n\t"
		".p2align 4\n\t"
		"9:\n\t"
		"cmpq $8, %0\n\t"
		"jb 10f\n\t"
		/*
		 * Move data from 8 bytes to 15 bytes.
		 */
		"movq 0*8(%1), %4\n\t"
		"movq -1*8(%1, %0), %5\n\t"
		"movq %4, 0*8(%2)\n\t"
		"movq %5, -1*8(%2, %0)\n\t"
		"jmp 13f\n\t"
		"10:\n\t"
		"cmpq $4, %0\n\t"
		"jb 11f\n\t"
		/*
		 * Move data from 4 bytes to 7 bytes.
		 */
		"movl (%1), %4d\n\t"
		"movl -4(%1, %0), %5d\n\t"
		"movl %4d, (%2)\n\t"
		"movl %5d, -4(%2, %0)\n\t"
		"jmp 13f\n\t"
		"11:\n\t"
		"cmp $2, %0\n\t"
		"jb 12f\n\t"
		/*
		 * Move data from 2 bytes to 3 bytes.
		 */
		"movw (%1), %4w\n\t"
		"movw -2(%1, %0), %5w\n\t"
		"movw %4w, (%2)\n\t"
		"movw %5w, -2(%2, %0)\n\t"
		"jmp 13f\n\t"
		"12:\n\t"
		"cmp $1, %0\n\t"
		"jb 13f\n\t"
		/*
		 * Move data for 1 byte.
		 */
		"movb (%1), %4b\n\t"
		"movb %4b, (%2)\n\t"
		"13:\n\t"
		: "=&d" (d0), "=&S" (d1), "=&D" (d2), "=&a" (ret) ,
		  "=r"(d3), "=r"(d4), "=r"(d5), "=r"(d6), "=&c" (d7)
		:"0" (count),
		 "1" (src),
		 "2" (dest)
		:"memory");

		return ret;

}
EXPORT_SYMBOL(memmove);
