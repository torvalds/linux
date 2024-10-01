/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef _ASM_ARC_IO_H
#define _ASM_ARC_IO_H

#include <linux/types.h>
#include <asm/byteorder.h>
#include <asm/page.h>
#include <asm/unaligned.h>

#ifdef CONFIG_ISA_ARCV2
#include <asm/barrier.h>
#define __iormb()		rmb()
#define __iowmb()		wmb()
#else
#define __iormb()		do { } while (0)
#define __iowmb()		do { } while (0)
#endif

extern void __iomem *ioremap(phys_addr_t paddr, unsigned long size);
#define ioremap ioremap
#define ioremap_prot ioremap_prot
#define iounmap iounmap
static inline void __iomem *ioport_map(unsigned long port, unsigned int nr)
{
	return (void __iomem *)port;
}

static inline void ioport_unmap(void __iomem *addr)
{
}

/*
 * io{read,write}{16,32}be() macros
 */
#define ioread16be(p)		({ u16 __v = be16_to_cpu((__force __be16)__raw_readw(p)); __iormb(); __v; })
#define ioread32be(p)		({ u32 __v = be32_to_cpu((__force __be32)__raw_readl(p)); __iormb(); __v; })

#define iowrite16be(v,p)	({ __iowmb(); __raw_writew((__force u16)cpu_to_be16(v), p); })
#define iowrite32be(v,p)	({ __iowmb(); __raw_writel((__force u32)cpu_to_be32(v), p); })

/* Change struct page to physical address */
#define page_to_phys(page)		(page_to_pfn(page) << PAGE_SHIFT)

#define __raw_readb __raw_readb
static inline u8 __raw_readb(const volatile void __iomem *addr)
{
	u8 b;

	__asm__ __volatile__(
	"	ldb%U1 %0, %1	\n"
	: "=r" (b)
	: "m" (*(volatile u8 __force *)addr)
	: "memory");

	return b;
}

#define __raw_readw __raw_readw
static inline u16 __raw_readw(const volatile void __iomem *addr)
{
	u16 s;

	__asm__ __volatile__(
	"	ldw%U1 %0, %1	\n"
	: "=r" (s)
	: "m" (*(volatile u16 __force *)addr)
	: "memory");

	return s;
}

#define __raw_readl __raw_readl
static inline u32 __raw_readl(const volatile void __iomem *addr)
{
	u32 w;

	__asm__ __volatile__(
	"	ld%U1 %0, %1	\n"
	: "=r" (w)
	: "m" (*(volatile u32 __force *)addr)
	: "memory");

	return w;
}

/*
 * {read,write}s{b,w,l}() repeatedly access the same IO address in
 * native endianness in 8-, 16-, 32-bit chunks {into,from} memory,
 * @count times
 */
#define __raw_readsx(t,f) \
static inline void __raw_reads##f(const volatile void __iomem *addr,	\
				  void *ptr, unsigned int count)	\
{									\
	bool is_aligned = ((unsigned long)ptr % ((t) / 8)) == 0;	\
	u##t *buf = ptr;						\
									\
	if (!count)							\
		return;							\
									\
	/* Some ARC CPU's don't support unaligned accesses */		\
	if (is_aligned) {						\
		do {							\
			u##t x = __raw_read##f(addr);			\
			*buf++ = x;					\
		} while (--count);					\
	} else {							\
		do {							\
			u##t x = __raw_read##f(addr);			\
			put_unaligned(x, buf++);			\
		} while (--count);					\
	}								\
}

#define __raw_readsb __raw_readsb
__raw_readsx(8, b)
#define __raw_readsw __raw_readsw
__raw_readsx(16, w)
#define __raw_readsl __raw_readsl
__raw_readsx(32, l)

#define __raw_writeb __raw_writeb
static inline void __raw_writeb(u8 b, volatile void __iomem *addr)
{
	__asm__ __volatile__(
	"	stb%U1 %0, %1	\n"
	:
	: "r" (b), "m" (*(volatile u8 __force *)addr)
	: "memory");
}

#define __raw_writew __raw_writew
static inline void __raw_writew(u16 s, volatile void __iomem *addr)
{
	__asm__ __volatile__(
	"	stw%U1 %0, %1	\n"
	:
	: "r" (s), "m" (*(volatile u16 __force *)addr)
	: "memory");

}

#define __raw_writel __raw_writel
static inline void __raw_writel(u32 w, volatile void __iomem *addr)
{
	__asm__ __volatile__(
	"	st%U1 %0, %1	\n"
	:
	: "r" (w), "m" (*(volatile u32 __force *)addr)
	: "memory");

}

#define __raw_writesx(t,f)						\
static inline void __raw_writes##f(volatile void __iomem *addr, 	\
				   const void *ptr, unsigned int count)	\
{									\
	bool is_aligned = ((unsigned long)ptr % ((t) / 8)) == 0;	\
	const u##t *buf = ptr;						\
									\
	if (!count)							\
		return;							\
									\
	/* Some ARC CPU's don't support unaligned accesses */		\
	if (is_aligned) {						\
		do {							\
			__raw_write##f(*buf++, addr);			\
		} while (--count);					\
	} else {							\
		do {							\
			__raw_write##f(get_unaligned(buf++), addr);	\
		} while (--count);					\
	}								\
}

#define __raw_writesb __raw_writesb
__raw_writesx(8, b)
#define __raw_writesw __raw_writesw
__raw_writesx(16, w)
#define __raw_writesl __raw_writesl
__raw_writesx(32, l)

/*
 * MMIO can also get buffered/optimized in micro-arch, so barriers needed
 * Based on ARM model for the typical use case
 *
 *	<ST [DMA buffer]>
 *	<writel MMIO "go" reg>
 *  or:
 *	<readl MMIO "status" reg>
 *	<LD [DMA buffer]>
 *
 * http://lkml.kernel.org/r/20150622133656.GG1583@arm.com
 */
#define readb(c)		({ u8  __v = readb_relaxed(c); __iormb(); __v; })
#define readw(c)		({ u16 __v = readw_relaxed(c); __iormb(); __v; })
#define readl(c)		({ u32 __v = readl_relaxed(c); __iormb(); __v; })
#define readsb(p,d,l)		({ __raw_readsb(p,d,l); __iormb(); })
#define readsw(p,d,l)		({ __raw_readsw(p,d,l); __iormb(); })
#define readsl(p,d,l)		({ __raw_readsl(p,d,l); __iormb(); })

#define writeb(v,c)		({ __iowmb(); writeb_relaxed(v,c); })
#define writew(v,c)		({ __iowmb(); writew_relaxed(v,c); })
#define writel(v,c)		({ __iowmb(); writel_relaxed(v,c); })
#define writesb(p,d,l)		({ __iowmb(); __raw_writesb(p,d,l); })
#define writesw(p,d,l)		({ __iowmb(); __raw_writesw(p,d,l); })
#define writesl(p,d,l)		({ __iowmb(); __raw_writesl(p,d,l); })

/*
 * Relaxed API for drivers which can handle barrier ordering themselves
 *
 * Also these are defined to perform little endian accesses.
 * To provide the typical device register semantics of fixed endian,
 * swap the byte order for Big Endian
 *
 * http://lkml.kernel.org/r/201603100845.30602.arnd@arndb.de
 */
#define readb_relaxed(c)	__raw_readb(c)
#define readw_relaxed(c) ({ u16 __r = le16_to_cpu((__force __le16) \
					__raw_readw(c)); __r; })
#define readl_relaxed(c) ({ u32 __r = le32_to_cpu((__force __le32) \
					__raw_readl(c)); __r; })

#define writeb_relaxed(v,c)	__raw_writeb(v,c)
#define writew_relaxed(v,c)	__raw_writew((__force u16) cpu_to_le16(v),c)
#define writel_relaxed(v,c)	__raw_writel((__force u32) cpu_to_le32(v),c)

#include <asm-generic/io.h>

#endif /* _ASM_ARC_IO_H */
