// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/arch/arm/mach-ebsa110/isamem.c
 *
 *  Copyright (C) 2001 Russell King
 *
 * Perform "ISA" memory and IO accesses.  The EBSA110 has some "peculiarities"
 * in the way it handles accesses to odd IO ports on 16-bit devices.  These
 * devices have their D0-D15 lines connected to the processors D0-D15 lines.
 * Since they expect all byte IO operations to be performed on D0-D7, and the
 * StrongARM expects to transfer the byte to these odd addresses on D8-D15,
 * we must use a trick to get the required behaviour.
 *
 * The trick employed here is to use long word stores to odd address -1.  The
 * glue logic picks this up as a "trick" access, and asserts the LSB of the
 * peripherals address bus, thereby accessing the odd IO port.  Meanwhile, the
 * StrongARM transfers its data on D0-D7 as expected.
 *
 * Things get more interesting on the pass-1 EBSA110 - the PCMCIA controller
 * wiring was screwed in such a way that it had limited memory space access.
 * Luckily, the work-around for this is not too horrible.  See
 * __isamem_convert_addr for the details.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/page.h>

static void __iomem *__isamem_convert_addr(const volatile void __iomem *addr)
{
	u32 ret, a = (u32 __force) addr;

	/*
	 * The PCMCIA controller is wired up as follows:
	 *        +---------+---------+---------+---------+---------+---------+
	 * PCMCIA | 2 2 2 2 | 1 1 1 1 | 1 1 1 1 | 1 1     |         |         |
	 *        | 3 2 1 0 | 9 8 7 6 | 5 4 3 2 | 1 0 9 8 | 7 6 5 4 | 3 2 1 0 |
	 *        +---------+---------+---------+---------+---------+---------+
	 *  CPU   | 2 2 2 2 | 2 1 1 1 | 1 1 1 1 | 1 1 1   |         |         |
	 *        | 4 3 2 1 | 0 9 9 8 | 7 6 5 4 | 3 2 0 9 | 8 7 6 5 | 4 3 2 x |
	 *        +---------+---------+---------+---------+---------+---------+
	 *
	 * This means that we can access PCMCIA regions as follows:
	 *	0x*10000 -> 0x*1ffff
	 *	0x*70000 -> 0x*7ffff
	 *	0x*90000 -> 0x*9ffff
	 *	0x*f0000 -> 0x*fffff
	 */
	ret  = (a & 0xf803fe) << 1;
	ret |= (a & 0x03fc00) << 2;

	ret += 0xe8000000;

	if ((a & 0x20000) == (a & 0x40000) >> 1)
		return (void __iomem *)ret;

	BUG();
	return NULL;
}

/*
 * read[bwl] and write[bwl]
 */
u8 __readb(const volatile void __iomem *addr)
{
	void __iomem *a = __isamem_convert_addr(addr);
	u32 ret;

	if ((unsigned long)addr & 1)
		ret = __raw_readl(a);
	else
		ret = __raw_readb(a);
	return ret;
}

u16 __readw(const volatile void __iomem *addr)
{
	void __iomem *a = __isamem_convert_addr(addr);

	if ((unsigned long)addr & 1)
		BUG();

	return __raw_readw(a);
}

u32 __readl(const volatile void __iomem *addr)
{
	void __iomem *a = __isamem_convert_addr(addr);
	u32 ret;

	if ((unsigned long)addr & 3)
		BUG();

	ret = __raw_readw(a);
	ret |= __raw_readw(a + 4) << 16;
	return ret;
}

EXPORT_SYMBOL(__readb);
EXPORT_SYMBOL(__readw);
EXPORT_SYMBOL(__readl);

void readsw(const volatile void __iomem *addr, void *data, int len)
{
	void __iomem *a = __isamem_convert_addr(addr);

	BUG_ON((unsigned long)addr & 1);

	__raw_readsw(a, data, len);
}
EXPORT_SYMBOL(readsw);

void readsl(const volatile void __iomem *addr, void *data, int len)
{
	void __iomem *a = __isamem_convert_addr(addr);

	BUG_ON((unsigned long)addr & 3);

	__raw_readsl(a, data, len);
}
EXPORT_SYMBOL(readsl);

void __writeb(u8 val, volatile void __iomem *addr)
{
	void __iomem *a = __isamem_convert_addr(addr);

	if ((unsigned long)addr & 1)
		__raw_writel(val, a);
	else
		__raw_writeb(val, a);
}

void __writew(u16 val, volatile void __iomem *addr)
{
	void __iomem *a = __isamem_convert_addr(addr);

	if ((unsigned long)addr & 1)
		BUG();

	__raw_writew(val, a);
}

void __writel(u32 val, volatile void __iomem *addr)
{
	void __iomem *a = __isamem_convert_addr(addr);

	if ((unsigned long)addr & 3)
		BUG();

	__raw_writew(val, a);
	__raw_writew(val >> 16, a + 4);
}

EXPORT_SYMBOL(__writeb);
EXPORT_SYMBOL(__writew);
EXPORT_SYMBOL(__writel);

void writesw(volatile void __iomem *addr, const void *data, int len)
{
	void __iomem *a = __isamem_convert_addr(addr);

	BUG_ON((unsigned long)addr & 1);

	__raw_writesw(a, data, len);
}
EXPORT_SYMBOL(writesw);

void writesl(volatile void __iomem *addr, const void *data, int len)
{
	void __iomem *a = __isamem_convert_addr(addr);

	BUG_ON((unsigned long)addr & 3);

	__raw_writesl(a, data, len);
}
EXPORT_SYMBOL(writesl);

/*
 * The EBSA110 has a weird "ISA IO" region:
 *
 * Region 0 (addr = 0xf0000000 + io << 2)
 * --------------------------------------------------------
 * Physical region	IO region
 * f0000fe0 - f0000ffc	3f8 - 3ff  ttyS0
 * f0000e60 - f0000e64	398 - 399
 * f0000de0 - f0000dfc	378 - 37f  lp0
 * f0000be0 - f0000bfc	2f8 - 2ff  ttyS1
 *
 * Region 1 (addr = 0xf0000000 + (io & ~1) << 1 + (io & 1))
 * --------------------------------------------------------
 * Physical region	IO region
 * f00014f1             a79        pnp write data
 * f00007c0 - f00007c1	3e0 - 3e1  pcmcia
 * f00004f1		279        pnp address
 * f0000440 - f000046c  220 - 236  eth0
 * f0000405		203        pnp read data
 */
#define SUPERIO_PORT(p) \
	(((p) >> 3) == (0x3f8 >> 3) || \
	 ((p) >> 3) == (0x2f8 >> 3) || \
	 ((p) >> 3) == (0x378 >> 3))

/*
 * We're addressing an 8 or 16-bit peripheral which tranfers
 * odd addresses on the low ISA byte lane.
 */
u8 __inb8(unsigned int port)
{
	u32 ret;

	/*
	 * The SuperIO registers use sane addressing techniques...
	 */
	if (SUPERIO_PORT(port))
		ret = __raw_readb((void __iomem *)ISAIO_BASE + (port << 2));
	else {
		void __iomem *a = (void __iomem *)ISAIO_BASE + ((port & ~1) << 1);

		/*
		 * Shame nothing else does
		 */
		if (port & 1)
			ret = __raw_readl(a);
		else
			ret = __raw_readb(a);
	}
	return ret;
}

/*
 * We're addressing a 16-bit peripheral which transfers odd
 * addresses on the high ISA byte lane.
 */
u8 __inb16(unsigned int port)
{
	unsigned int offset;

	/*
	 * The SuperIO registers use sane addressing techniques...
	 */
	if (SUPERIO_PORT(port))
		offset = port << 2;
	else
		offset = (port & ~1) << 1 | (port & 1);

	return __raw_readb((void __iomem *)ISAIO_BASE + offset);
}

u16 __inw(unsigned int port)
{
	unsigned int offset;

	/*
	 * The SuperIO registers use sane addressing techniques...
	 */
	if (SUPERIO_PORT(port))
		offset = port << 2;
	else {
		offset = port << 1;
		BUG_ON(port & 1);
	}
	return __raw_readw((void __iomem *)ISAIO_BASE + offset);
}

/*
 * Fake a 32-bit read with two 16-bit reads.  Needed for 3c589.
 */
u32 __inl(unsigned int port)
{
	void __iomem *a;

	if (SUPERIO_PORT(port) || port & 3)
		BUG();

	a = (void __iomem *)ISAIO_BASE + ((port & ~1) << 1);

	return __raw_readw(a) | __raw_readw(a + 4) << 16;
}

EXPORT_SYMBOL(__inb8);
EXPORT_SYMBOL(__inb16);
EXPORT_SYMBOL(__inw);
EXPORT_SYMBOL(__inl);

void __outb8(u8 val, unsigned int port)
{
	/*
	 * The SuperIO registers use sane addressing techniques...
	 */
	if (SUPERIO_PORT(port))
		__raw_writeb(val, (void __iomem *)ISAIO_BASE + (port << 2));
	else {
		void __iomem *a = (void __iomem *)ISAIO_BASE + ((port & ~1) << 1);

		/*
		 * Shame nothing else does
		 */
		if (port & 1)
			__raw_writel(val, a);
		else
			__raw_writeb(val, a);
	}
}

void __outb16(u8 val, unsigned int port)
{
	unsigned int offset;

	/*
	 * The SuperIO registers use sane addressing techniques...
	 */
	if (SUPERIO_PORT(port))
		offset = port << 2;
	else
		offset = (port & ~1) << 1 | (port & 1);

	__raw_writeb(val, (void __iomem *)ISAIO_BASE + offset);
}

void __outw(u16 val, unsigned int port)
{
	unsigned int offset;

	/*
	 * The SuperIO registers use sane addressing techniques...
	 */
	if (SUPERIO_PORT(port))
		offset = port << 2;
	else {
		offset = port << 1;
		BUG_ON(port & 1);
	}
	__raw_writew(val, (void __iomem *)ISAIO_BASE + offset);
}

void __outl(u32 val, unsigned int port)
{
	BUG();
}

EXPORT_SYMBOL(__outb8);
EXPORT_SYMBOL(__outb16);
EXPORT_SYMBOL(__outw);
EXPORT_SYMBOL(__outl);

void outsb(unsigned int port, const void *from, int len)
{
	u32 off;

	if (SUPERIO_PORT(port))
		off = port << 2;
	else {
		off = (port & ~1) << 1;
		if (port & 1)
			BUG();
	}

	__raw_writesb((void __iomem *)ISAIO_BASE + off, from, len);
}

void insb(unsigned int port, void *from, int len)
{
	u32 off;

	if (SUPERIO_PORT(port))
		off = port << 2;
	else {
		off = (port & ~1) << 1;
		if (port & 1)
			BUG();
	}

	__raw_readsb((void __iomem *)ISAIO_BASE + off, from, len);
}

EXPORT_SYMBOL(outsb);
EXPORT_SYMBOL(insb);

void outsw(unsigned int port, const void *from, int len)
{
	u32 off;

	if (SUPERIO_PORT(port))
		off = port << 2;
	else {
		off = (port & ~1) << 1;
		if (port & 1)
			BUG();
	}

	__raw_writesw((void __iomem *)ISAIO_BASE + off, from, len);
}

void insw(unsigned int port, void *from, int len)
{
	u32 off;

	if (SUPERIO_PORT(port))
		off = port << 2;
	else {
		off = (port & ~1) << 1;
		if (port & 1)
			BUG();
	}

	__raw_readsw((void __iomem *)ISAIO_BASE + off, from, len);
}

EXPORT_SYMBOL(outsw);
EXPORT_SYMBOL(insw);

/*
 * We implement these as 16-bit insw/outsw, mainly for
 * 3c589 cards.
 */
void outsl(unsigned int port, const void *from, int len)
{
	u32 off = port << 1;

	if (SUPERIO_PORT(port) || port & 3)
		BUG();

	__raw_writesw((void __iomem *)ISAIO_BASE + off, from, len << 1);
}

void insl(unsigned int port, void *from, int len)
{
	u32 off = port << 1;

	if (SUPERIO_PORT(port) || port & 3)
		BUG();

	__raw_readsw((void __iomem *)ISAIO_BASE + off, from, len << 1);
}

EXPORT_SYMBOL(outsl);
EXPORT_SYMBOL(insl);
