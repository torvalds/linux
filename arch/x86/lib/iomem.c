#include <linux/string.h>
#include <linux/module.h>
#include <linux/io.h>

#define movs(type,to,from) \
	asm volatile("movs" type:"=&D" (to), "=&S" (from):"0" (to), "1" (from):"memory")

/* Originally from i386/string.h */
static __always_inline void rep_movs(void *to, const void *from, size_t n)
{
	unsigned long d0, d1, d2;
	asm volatile("rep ; movsl\n\t"
		     "testb $2,%b4\n\t"
		     "je 1f\n\t"
		     "movsw\n"
		     "1:\ttestb $1,%b4\n\t"
		     "je 2f\n\t"
		     "movsb\n"
		     "2:"
		     : "=&c" (d0), "=&D" (d1), "=&S" (d2)
		     : "0" (n / 4), "q" (n), "1" ((long)to), "2" ((long)from)
		     : "memory");
}

void memcpy_fromio(void *to, const volatile void __iomem *from, size_t n)
{
	if (unlikely(!n))
		return;

	/* Align any unaligned source IO */
	if (unlikely(1 & (unsigned long)from)) {
		movs("b", to, from);
		n--;
	}
	if (n > 1 && unlikely(2 & (unsigned long)from)) {
		movs("w", to, from);
		n-=2;
	}
	rep_movs(to, (const void *)from, n);
}
EXPORT_SYMBOL(memcpy_fromio);

void memcpy_toio(volatile void __iomem *to, const void *from, size_t n)
{
	if (unlikely(!n))
		return;

	/* Align any unaligned destination IO */
	if (unlikely(1 & (unsigned long)to)) {
		movs("b", to, from);
		n--;
	}
	if (n > 1 && unlikely(2 & (unsigned long)to)) {
		movs("w", to, from);
		n-=2;
	}
	rep_movs((void *)to, (const void *) from, n);
}
EXPORT_SYMBOL(memcpy_toio);

void memset_io(volatile void __iomem *a, int b, size_t c)
{
	/*
	 * TODO: memset can mangle the IO patterns quite a bit.
	 * perhaps it would be better to use a dumb one:
	 */
	memset((void *)a, b, c);
}
EXPORT_SYMBOL(memset_io);
