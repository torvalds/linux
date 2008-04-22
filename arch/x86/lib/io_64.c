#include <linux/string.h>
#include <linux/module.h>
#include <asm/io.h>

void __memcpy_toio(unsigned long dst, const void *src, unsigned len)
{
	__inline_memcpy((void *)dst, src, len);
}
EXPORT_SYMBOL(__memcpy_toio);

void __memcpy_fromio(void *dst, unsigned long src, unsigned len)
{
	__inline_memcpy(dst, (const void *)src, len);
}
EXPORT_SYMBOL(__memcpy_fromio);

void memset_io(volatile void __iomem *a, int b, size_t c)
{
	/*
	 * TODO: memset can mangle the IO patterns quite a bit.
	 * perhaps it would be better to use a dumb one:
	 */
	memset((void *)a, b, c);
}
EXPORT_SYMBOL(memset_io);
