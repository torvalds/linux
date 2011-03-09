#include <linux/string.h>
#include <linux/module.h>

#undef memcpy
#undef memset

void *memcpy(void *to, const void *from, size_t n)
{
#ifdef CONFIG_X86_USE_3DNOW
	return __memcpy3d(to, from, n);
#else
	return __memcpy(to, from, n);
#endif
}
EXPORT_SYMBOL(memcpy);

void *memset(void *s, int c, size_t count)
{
	return __memset(s, c, count);
}
EXPORT_SYMBOL(memset);

void *memmove(void *dest, const void *src, size_t n)
{
	int d0,d1,d2,d3,d4,d5;
	char *ret = dest;

	__asm__ __volatile__(
		/* Handle more 16bytes in loop */
		"cmp $0x10, %0\n\t"
		"jb	1f\n\t"

		/* Decide forward/backward copy mode */
		"cmp %2, %1\n\t"
		"jb	2f\n\t"

		/*
		 * movs instruction have many startup latency
		 * so we handle small size by general register.
		 */
		"cmp  $680, %0\n\t"
		"jb 3f\n\t"
		/*
		 * movs instruction is only good for aligned case.
		 */
		"mov %1, %3\n\t"
		"xor %2, %3\n\t"
		"and $0xff, %3\n\t"
		"jz 4f\n\t"
		"3:\n\t"
		"sub $0x10, %0\n\t"

		/*
		 * We gobble 16byts forward in each loop.
		 */
		"3:\n\t"
		"sub $0x10, %0\n\t"
		"mov 0*4(%1), %3\n\t"
		"mov 1*4(%1), %4\n\t"
		"mov  %3, 0*4(%2)\n\t"
		"mov  %4, 1*4(%2)\n\t"
		"mov 2*4(%1), %3\n\t"
		"mov 3*4(%1), %4\n\t"
		"mov  %3, 2*4(%2)\n\t"
		"mov  %4, 3*4(%2)\n\t"
		"lea  0x10(%1), %1\n\t"
		"lea  0x10(%2), %2\n\t"
		"jae 3b\n\t"
		"add $0x10, %0\n\t"
		"jmp 1f\n\t"

		/*
		 * Handle data forward by movs.
		 */
		".p2align 4\n\t"
		"4:\n\t"
		"mov -4(%1, %0), %3\n\t"
		"lea -4(%2, %0), %4\n\t"
		"shr $2, %0\n\t"
		"rep movsl\n\t"
		"mov %3, (%4)\n\t"
		"jmp 11f\n\t"
		/*
		 * Handle data backward by movs.
		 */
		".p2align 4\n\t"
		"6:\n\t"
		"mov (%1), %3\n\t"
		"mov %2, %4\n\t"
		"lea -4(%1, %0), %1\n\t"
		"lea -4(%2, %0), %2\n\t"
		"shr $2, %0\n\t"
		"std\n\t"
		"rep movsl\n\t"
		"mov %3,(%4)\n\t"
		"cld\n\t"
		"jmp 11f\n\t"

		/*
		 * Start to prepare for backward copy.
		 */
		".p2align 4\n\t"
		"2:\n\t"
		"cmp  $680, %0\n\t"
		"jb 5f\n\t"
		"mov %1, %3\n\t"
		"xor %2, %3\n\t"
		"and $0xff, %3\n\t"
		"jz 6b\n\t"

		/*
		 * Calculate copy position to tail.
		 */
		"5:\n\t"
		"add %0, %1\n\t"
		"add %0, %2\n\t"
		"sub $0x10, %0\n\t"

		/*
		 * We gobble 16byts backward in each loop.
		 */
		"7:\n\t"
		"sub $0x10, %0\n\t"

		"mov -1*4(%1), %3\n\t"
		"mov -2*4(%1), %4\n\t"
		"mov  %3, -1*4(%2)\n\t"
		"mov  %4, -2*4(%2)\n\t"
		"mov -3*4(%1), %3\n\t"
		"mov -4*4(%1), %4\n\t"
		"mov  %3, -3*4(%2)\n\t"
		"mov  %4, -4*4(%2)\n\t"
		"lea  -0x10(%1), %1\n\t"
		"lea  -0x10(%2), %2\n\t"
		"jae 7b\n\t"
		/*
		 * Calculate copy position to head.
		 */
		"add $0x10, %0\n\t"
		"sub %0, %1\n\t"
		"sub %0, %2\n\t"

		/*
		 * Move data from 8 bytes to 15 bytes.
		 */
		".p2align 4\n\t"
		"1:\n\t"
		"cmp $8, %0\n\t"
		"jb 8f\n\t"
		"mov 0*4(%1), %3\n\t"
		"mov 1*4(%1), %4\n\t"
		"mov -2*4(%1, %0), %5\n\t"
		"mov -1*4(%1, %0), %1\n\t"

		"mov  %3, 0*4(%2)\n\t"
		"mov  %4, 1*4(%2)\n\t"
		"mov  %5, -2*4(%2, %0)\n\t"
		"mov  %1, -1*4(%2, %0)\n\t"
		"jmp 11f\n\t"

		/*
		 * Move data from 4 bytes to 7 bytes.
		 */
		".p2align 4\n\t"
		"8:\n\t"
		"cmp $4, %0\n\t"
		"jb 9f\n\t"
		"mov 0*4(%1), %3\n\t"
		"mov -1*4(%1, %0), %4\n\t"
		"mov  %3, 0*4(%2)\n\t"
		"mov  %4, -1*4(%2, %0)\n\t"
		"jmp 11f\n\t"

		/*
		 * Move data from 2 bytes to 3 bytes.
		 */
		".p2align 4\n\t"
		"9:\n\t"
		"cmp $2, %0\n\t"
		"jb 10f\n\t"
		"movw 0*2(%1), %%dx\n\t"
		"movw -1*2(%1, %0), %%bx\n\t"
		"movw %%dx, 0*2(%2)\n\t"
		"movw %%bx, -1*2(%2, %0)\n\t"
		"jmp 11f\n\t"

		/*
		 * Move data for 1 byte.
		 */
		".p2align 4\n\t"
		"10:\n\t"
		"cmp $1, %0\n\t"
		"jb 11f\n\t"
		"movb (%1), %%cl\n\t"
		"movb %%cl, (%2)\n\t"
		".p2align 4\n\t"
		"11:"
		: "=&c" (d0), "=&S" (d1), "=&D" (d2),
		  "=r" (d3),"=r" (d4), "=r"(d5)
		:"0" (n),
		 "1" (src),
		 "2" (dest)
		:"memory");

	return ret;

}
EXPORT_SYMBOL(memmove);
