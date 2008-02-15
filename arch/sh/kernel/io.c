/*
 * linux/arch/sh/kernel/io.c
 *
 * Copyright (C) 2000  Stuart Menefy
 * Copyright (C) 2005  Paul Mundt
 *
 * Provide real functions which expand to whatever the header file defined.
 * Also definitions of machine independent IO functions.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/module.h>
#include <asm/machvec.h>
#include <asm/io.h>

/*
 * Copy data from IO memory space to "real" memory space.
 * This needs to be optimized.
 */
void memcpy_fromio(void *to, volatile void __iomem *from, unsigned long count)
{
	char *p = to;
        while (count) {
                count--;
                *p = readb((void __iomem *)from);
                p++;
                from++;
        }
}
EXPORT_SYMBOL(memcpy_fromio);

/*
 * Copy data from "real" memory space to IO memory space.
 * This needs to be optimized.
 */
void memcpy_toio(volatile void __iomem *to, const void *from, unsigned long count)
{
	const char *p = from;
        while (count) {
                count--;
                writeb(*p, (void __iomem *)to);
                p++;
                to++;
        }
}
EXPORT_SYMBOL(memcpy_toio);

/*
 * "memset" on IO memory space.
 * This needs to be optimized.
 */
void memset_io(volatile void __iomem *dst, int c, unsigned long count)
{
        while (count) {
                count--;
                writeb(c, (void __iomem *)dst);
                dst++;
        }
}
EXPORT_SYMBOL(memset_io);

void __iomem *ioport_map(unsigned long port, unsigned int nr)
{
	void __iomem *ret;

	ret = __ioport_map_trapped(port, nr);
	if (ret)
		return ret;

	return __ioport_map(port, nr);
}
EXPORT_SYMBOL(ioport_map);

void ioport_unmap(void __iomem *addr)
{
	sh_mv.mv_ioport_unmap(addr);
}
EXPORT_SYMBOL(ioport_unmap);
