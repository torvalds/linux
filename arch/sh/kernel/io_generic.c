/*
 * arch/sh/kernel/io_generic.c
 *
 * Copyright (C) 2000  Niibe Yutaka
 * Copyright (C) 2005 - 2007 Paul Mundt
 *
 * Generic I/O routine. These can be used where a machine specific version
 * is not required.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/module.h>
#include <linux/io.h>
#include <asm/machvec.h>

#ifdef CONFIG_CPU_SH3
/* SH3 has a PCMCIA bug that needs a dummy read from area 6 for a
 * workaround. */
/* I'm not sure SH7709 has this kind of bug */
#define dummy_read()	__raw_readb(0xba000000)
#else
#define dummy_read()
#endif

unsigned long generic_io_base = 0;

u8 generic_inb(unsigned long port)
{
	return __raw_readb(__ioport_map(port, 1));
}

u16 generic_inw(unsigned long port)
{
	return __raw_readw(__ioport_map(port, 2));
}

u32 generic_inl(unsigned long port)
{
	return __raw_readl(__ioport_map(port, 4));
}

u8 generic_inb_p(unsigned long port)
{
	unsigned long v = generic_inb(port);

	ctrl_delay();
	return v;
}

u16 generic_inw_p(unsigned long port)
{
	unsigned long v = generic_inw(port);

	ctrl_delay();
	return v;
}

u32 generic_inl_p(unsigned long port)
{
	unsigned long v = generic_inl(port);

	ctrl_delay();
	return v;
}

/*
 * insb/w/l all read a series of bytes/words/longs from a fixed port
 * address. However as the port address doesn't change we only need to
 * convert the port address to real address once.
 */

void generic_insb(unsigned long port, void *dst, unsigned long count)
{
	__raw_readsb(__ioport_map(port, 1), dst, count);
	dummy_read();
}

void generic_insw(unsigned long port, void *dst, unsigned long count)
{
	__raw_readsw(__ioport_map(port, 2), dst, count);
	dummy_read();
}

void generic_insl(unsigned long port, void *dst, unsigned long count)
{
	__raw_readsl(__ioport_map(port, 4), dst, count);
	dummy_read();
}

void generic_outb(u8 b, unsigned long port)
{
	__raw_writeb(b, __ioport_map(port, 1));
}

void generic_outw(u16 b, unsigned long port)
{
	__raw_writew(b, __ioport_map(port, 2));
}

void generic_outl(u32 b, unsigned long port)
{
	__raw_writel(b, __ioport_map(port, 4));
}

void generic_outb_p(u8 b, unsigned long port)
{
	generic_outb(b, port);
	ctrl_delay();
}

void generic_outw_p(u16 b, unsigned long port)
{
	generic_outw(b, port);
	ctrl_delay();
}

void generic_outl_p(u32 b, unsigned long port)
{
	generic_outl(b, port);
	ctrl_delay();
}

/*
 * outsb/w/l all write a series of bytes/words/longs to a fixed port
 * address. However as the port address doesn't change we only need to
 * convert the port address to real address once.
 */
void generic_outsb(unsigned long port, const void *src, unsigned long count)
{
	__raw_writesb(__ioport_map(port, 1), src, count);
	dummy_read();
}

void generic_outsw(unsigned long port, const void *src, unsigned long count)
{
	__raw_writesw(__ioport_map(port, 2), src, count);
	dummy_read();
}

void generic_outsl(unsigned long port, const void *src, unsigned long count)
{
	__raw_writesl(__ioport_map(port, 4), src, count);
	dummy_read();
}

void __iomem *generic_ioport_map(unsigned long addr, unsigned int size)
{
#ifdef P1SEG
	if (PXSEG(addr) >= P1SEG)
		return (void __iomem *)addr;
#endif

	return (void __iomem *)(addr + generic_io_base);
}

void generic_ioport_unmap(void __iomem *addr)
{
}

#ifndef CONFIG_GENERIC_IOMAP
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
#endif /* CONFIG_GENERIC_IOMAP */
