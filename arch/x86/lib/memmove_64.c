/* Normally compiler builtins are used, but sometimes the compiler calls out
   of line code. Based on asm-i386/string.h.
 */
#define _STRING_C
#include <linux/string.h>
#include <linux/module.h>

#undef memmove
void *memmove(void *dest, const void *src, size_t count)
{
	unsigned long d0, d1, d2, d3;
	if (dest < src) {
		if ((dest + count) < src)
			 return memcpy(dest, src, count);
		else
			__asm__ __volatile__(
				"movq %0, %3\n\t"
				"shr $3, %0\n\t"
				"andq $7, %3\n\t"
				"rep\n\t"
				"movsq\n\t"
				"movq %3, %0\n\t"
				"rep\n\t"
				"movsb"
				: "=&c" (d0), "=&S" (d1), "=&D" (d2), "=r" (d3)
				:"0" (count),
				 "1" (src),
				 "2" (dest)
				:"memory");
	} else {
		if((src + count) < dest)
			return memcpy(dest, src, count);
		else
			__asm__ __volatile__(
				"movq %0, %3\n\t"
				"lea -8(%1, %0), %1\n\t"
				"lea -8(%2, %0), %2\n\t"
				"shr $3, %0\n\t"
				"andq $7, %3\n\t"
				"std\n\t"
				"rep\n\t"
				"movsq\n\t"
				"lea 7(%1), %1\n\t"
				"lea 7(%2), %2\n\t"
				"movq %3, %0\n\t"
				"rep\n\t"
				"movsb\n\t"
				"cld"
				: "=&c" (d0), "=&S" (d1), "=&D" (d2), "=r" (d3)
				:"0" (count),
				 "1" (src),
				 "2" (dest)
				:"memory");
	}
	return dest;
}
EXPORT_SYMBOL(memmove);
