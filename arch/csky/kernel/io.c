// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <linux/types.h>
#include <linux/io.h>

/*
 * Copy data from IO memory space to "real" memory space.
 */
void __memcpy_fromio(void *to, const volatile void __iomem *from, size_t count)
{
	while (count && !IS_ALIGNED((unsigned long)from, 4)) {
		*(u8 *)to = __raw_readb(from);
		from++;
		to++;
		count--;
	}

	while (count >= 4) {
		*(u32 *)to = __raw_readl(from);
		from += 4;
		to += 4;
		count -= 4;
	}

	while (count) {
		*(u8 *)to = __raw_readb(from);
		from++;
		to++;
		count--;
	}
}
EXPORT_SYMBOL(__memcpy_fromio);

/*
 * Copy data from "real" memory space to IO memory space.
 */
void __memcpy_toio(volatile void __iomem *to, const void *from, size_t count)
{
	while (count && !IS_ALIGNED((unsigned long)to, 4)) {
		__raw_writeb(*(u8 *)from, to);
		from++;
		to++;
		count--;
	}

	while (count >= 4) {
		__raw_writel(*(u32 *)from, to);
		from += 4;
		to += 4;
		count -= 4;
	}

	while (count) {
		__raw_writeb(*(u8 *)from, to);
		from++;
		to++;
		count--;
	}
}
EXPORT_SYMBOL(__memcpy_toio);

/*
 * "memset" on IO memory space.
 */
void __memset_io(volatile void __iomem *dst, int c, size_t count)
{
	u32 qc = (u8)c;

	qc |= qc << 8;
	qc |= qc << 16;

	while (count && !IS_ALIGNED((unsigned long)dst, 4)) {
		__raw_writeb(c, dst);
		dst++;
		count--;
	}

	while (count >= 4) {
		__raw_writel(qc, dst);
		dst += 4;
		count -= 4;
	}

	while (count) {
		__raw_writeb(c, dst);
		dst++;
		count--;
	}
}
EXPORT_SYMBOL(__memset_io);
