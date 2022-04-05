// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/types.h>

#include <asm/io.h>

/*
 * Copy data from IO memory space to "real" memory space.
 * This needs to be optimized.
 */
void memcpy_fromio(void *to, const volatile void __iomem *from, long count)
{
	char *dst = to;

	while (count) {
		count--;
		*dst++ = readb(from++);
	}
}
EXPORT_SYMBOL(memcpy_fromio);

/*
 * Copy data from "real" memory space to IO memory space.
 * This needs to be optimized.
 */
void memcpy_toio(volatile void __iomem *to, const void *from, long count)
{
	const char *src = from;

	while (count) {
		count--;
		writeb(*src++, to++);
	}
}
EXPORT_SYMBOL(memcpy_toio);

/*
 * "memset" on IO memory space.
 * This needs to be optimized.
 */
void memset_io(volatile void __iomem *dst, int c, long count)
{
	unsigned char ch = (char)(c & 0xff);

	while (count) {
		count--;
		writeb(ch, dst);
		dst++;
	}
}
EXPORT_SYMBOL(memset_io);
