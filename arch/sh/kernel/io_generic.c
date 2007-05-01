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
#define dummy_read()	ctrl_inb(0xba000000)
#else
#define dummy_read()
#endif

unsigned long generic_io_base;

static inline void delay(void)
{
	ctrl_inw(0xa0000000);
}

u8 generic_inb(unsigned long port)
{
	return ctrl_inb((unsigned long __force)ioport_map(port, 1));
}

u16 generic_inw(unsigned long port)
{
	return ctrl_inw((unsigned long __force)ioport_map(port, 2));
}

u32 generic_inl(unsigned long port)
{
	return ctrl_inl((unsigned long __force)ioport_map(port, 4));
}

u8 generic_inb_p(unsigned long port)
{
	unsigned long v = generic_inb(port);

	delay();
	return v;
}

u16 generic_inw_p(unsigned long port)
{
	unsigned long v = generic_inw(port);

	delay();
	return v;
}

u32 generic_inl_p(unsigned long port)
{
	unsigned long v = generic_inl(port);

	delay();
	return v;
}

/*
 * insb/w/l all read a series of bytes/words/longs from a fixed port
 * address. However as the port address doesn't change we only need to
 * convert the port address to real address once.
 */

void generic_insb(unsigned long port, void *dst, unsigned long count)
{
	volatile u8 *port_addr;
	u8 *buf = dst;

	port_addr = (volatile u8 *)ioport_map(port, 1);
	while (count--)
		*buf++ = *port_addr;
}

void generic_insw(unsigned long port, void *dst, unsigned long count)
{
	volatile u16 *port_addr;
	u16 *buf = dst;

	port_addr = (volatile u16 *)ioport_map(port, 2);
	while (count--)
		*buf++ = *port_addr;

	dummy_read();
}

void generic_insl(unsigned long port, void *dst, unsigned long count)
{
	volatile u32 *port_addr;
	u32 *buf = dst;

	port_addr = (volatile u32 *)ioport_map(port, 4);
	while (count--)
		*buf++ = *port_addr;

	dummy_read();
}

void generic_outb(u8 b, unsigned long port)
{
	ctrl_outb(b, (unsigned long __force)ioport_map(port, 1));
}

void generic_outw(u16 b, unsigned long port)
{
	ctrl_outw(b, (unsigned long __force)ioport_map(port, 2));
}

void generic_outl(u32 b, unsigned long port)
{
	ctrl_outl(b, (unsigned long __force)ioport_map(port, 4));
}

void generic_outb_p(u8 b, unsigned long port)
{
	generic_outb(b, port);
	delay();
}

void generic_outw_p(u16 b, unsigned long port)
{
	generic_outw(b, port);
	delay();
}

void generic_outl_p(u32 b, unsigned long port)
{
	generic_outl(b, port);
	delay();
}

/*
 * outsb/w/l all write a series of bytes/words/longs to a fixed port
 * address. However as the port address doesn't change we only need to
 * convert the port address to real address once.
 */
void generic_outsb(unsigned long port, const void *src, unsigned long count)
{
	volatile u8 *port_addr;
	const u8 *buf = src;

	port_addr = (volatile u8 __force *)ioport_map(port, 1);

	while (count--)
		*port_addr = *buf++;
}

void generic_outsw(unsigned long port, const void *src, unsigned long count)
{
	volatile u16 *port_addr;
	const u16 *buf = src;

	port_addr = (volatile u16 __force *)ioport_map(port, 2);

	while (count--)
		*port_addr = *buf++;

	dummy_read();
}

void generic_outsl(unsigned long port, const void *src, unsigned long count)
{
	volatile u32 *port_addr;
	const u32 *buf = src;

	port_addr = (volatile u32 __force *)ioport_map(port, 4);
	while (count--)
		*port_addr = *buf++;

	dummy_read();
}

u8 generic_readb(void __iomem *addr)
{
	return ctrl_inb((unsigned long __force)addr);
}

u16 generic_readw(void __iomem *addr)
{
	return ctrl_inw((unsigned long __force)addr);
}

u32 generic_readl(void __iomem *addr)
{
	return ctrl_inl((unsigned long __force)addr);
}

void generic_writeb(u8 b, void __iomem *addr)
{
	ctrl_outb(b, (unsigned long __force)addr);
}

void generic_writew(u16 b, void __iomem *addr)
{
	ctrl_outw(b, (unsigned long __force)addr);
}

void generic_writel(u32 b, void __iomem *addr)
{
	ctrl_outl(b, (unsigned long __force)addr);
}

void __iomem *generic_ioport_map(unsigned long addr, unsigned int size)
{
	return (void __iomem *)(addr + generic_io_base);
}

void generic_ioport_unmap(void __iomem *addr)
{
}
