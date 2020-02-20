/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IO definitions for the Hexagon architecture
 *
 * Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_IO_H
#define _ASM_IO_H

#ifdef __KERNEL__

#include <linux/types.h>
#include <asm/iomap.h>
#include <asm/page.h>
#include <asm/cacheflush.h>

/*
 * We don't have PCI yet.
 * _IO_BASE is pointing at what should be unused virtual space.
 */
#define IO_SPACE_LIMIT 0xffff
#define _IO_BASE ((void __iomem *)0xfe000000)

#define IOMEM(x)        ((void __force __iomem *)(x))

extern int remap_area_pages(unsigned long start, unsigned long phys_addr,
				unsigned long end, unsigned long flags);

extern void iounmap(const volatile void __iomem *addr);

/* Defined in lib/io.c, needed for smc91x driver. */
extern void __raw_readsw(const void __iomem *addr, void *data, int wordlen);
extern void __raw_writesw(void __iomem *addr, const void *data, int wordlen);

extern void __raw_readsl(const void __iomem *addr, void *data, int wordlen);
extern void __raw_writesl(void __iomem *addr, const void *data, int wordlen);

#define readsw(p, d, l)	__raw_readsw(p, d, l)
#define writesw(p, d, l) __raw_writesw(p, d, l)

#define readsl(p, d, l)   __raw_readsl(p, d, l)
#define writesl(p, d, l)  __raw_writesl(p, d, l)

/*
 * virt_to_phys - map virtual address to physical
 * @address:  address to map
 */
static inline unsigned long virt_to_phys(volatile void *address)
{
	return __pa(address);
}

/*
 * phys_to_virt - map physical address to virtual
 * @address: address to map
 */
static inline void *phys_to_virt(unsigned long address)
{
	return __va(address);
}

/*
 * convert a physical pointer to a virtual kernel pointer for
 * /dev/mem access.
 */
#define xlate_dev_kmem_ptr(p)    __va(p)
#define xlate_dev_mem_ptr(p)    __va(p)

/*
 * IO port access primitives.  Hexagon doesn't have special IO access
 * instructions; all I/O is memory mapped.
 *
 * in/out are used for "ports", but we don't have "port instructions",
 * so these are really just memory mapped too.
 */

/*
 * readb - read byte from memory mapped device
 * @addr:  pointer to memory
 *
 * Operates on "I/O bus memory space"
 */
static inline u8 readb(const volatile void __iomem *addr)
{
	u8 val;
	asm volatile(
		"%0 = memb(%1);"
		: "=&r" (val)
		: "r" (addr)
	);
	return val;
}

static inline u16 readw(const volatile void __iomem *addr)
{
	u16 val;
	asm volatile(
		"%0 = memh(%1);"
		: "=&r" (val)
		: "r" (addr)
	);
	return val;
}

static inline u32 readl(const volatile void __iomem *addr)
{
	u32 val;
	asm volatile(
		"%0 = memw(%1);"
		: "=&r" (val)
		: "r" (addr)
	);
	return val;
}

/*
 * writeb - write a byte to a memory location
 * @data: data to write to
 * @addr:  pointer to memory
 *
 */
static inline void writeb(u8 data, volatile void __iomem *addr)
{
	asm volatile(
		"memb(%0) = %1;"
		:
		: "r" (addr), "r" (data)
		: "memory"
	);
}

static inline void writew(u16 data, volatile void __iomem *addr)
{
	asm volatile(
		"memh(%0) = %1;"
		:
		: "r" (addr), "r" (data)
		: "memory"
	);

}

static inline void writel(u32 data, volatile void __iomem *addr)
{
	asm volatile(
		"memw(%0) = %1;"
		:
		: "r" (addr), "r" (data)
		: "memory"
	);
}

#define __raw_writeb writeb
#define __raw_writew writew
#define __raw_writel writel

#define __raw_readb readb
#define __raw_readw readw
#define __raw_readl readl

/*
 * http://comments.gmane.org/gmane.linux.ports.arm.kernel/117626
 */

#define readb_relaxed __raw_readb
#define readw_relaxed __raw_readw
#define readl_relaxed __raw_readl

#define writeb_relaxed __raw_writeb
#define writew_relaxed __raw_writew
#define writel_relaxed __raw_writel

void __iomem *ioremap(unsigned long phys_addr, unsigned long size);
#define ioremap_nocache ioremap
#define ioremap_uc(X, Y) ioremap((X), (Y))


#define __raw_writel writel

static inline void memcpy_fromio(void *dst, const volatile void __iomem *src,
	int count)
{
	memcpy(dst, (void *) src, count);
}

static inline void memcpy_toio(volatile void __iomem *dst, const void *src,
	int count)
{
	memcpy((void *) dst, src, count);
}

static inline void memset_io(volatile void __iomem *addr, int value,
			     size_t size)
{
	memset((void __force *)addr, value, size);
}

#define PCI_IO_ADDR	(volatile void __iomem *)

/*
 * inb - read byte from I/O port or something
 * @port:  address in I/O space
 *
 * Operates on "I/O bus I/O space"
 */
static inline u8 inb(unsigned long port)
{
	return readb(_IO_BASE + (port & IO_SPACE_LIMIT));
}

static inline u16 inw(unsigned long port)
{
	return readw(_IO_BASE + (port & IO_SPACE_LIMIT));
}

static inline u32 inl(unsigned long port)
{
	return readl(_IO_BASE + (port & IO_SPACE_LIMIT));
}

/*
 * outb - write a byte to a memory location
 * @data: data to write to
 * @addr:  address in I/O space
 */
static inline void outb(u8 data, unsigned long port)
{
	writeb(data, _IO_BASE + (port & IO_SPACE_LIMIT));
}

static inline void outw(u16 data, unsigned long port)
{
	writew(data, _IO_BASE + (port & IO_SPACE_LIMIT));
}

static inline void outl(u32 data, unsigned long port)
{
	writel(data, _IO_BASE + (port & IO_SPACE_LIMIT));
}

#define outb_p outb
#define outw_p outw
#define outl_p outl

#define inb_p inb
#define inw_p inw
#define inl_p inl

static inline void insb(unsigned long port, void *buffer, int count)
{
	if (count) {
		u8 *buf = buffer;
		do {
			u8 x = inb(port);
			*buf++ = x;
		} while (--count);
	}
}

static inline void insw(unsigned long port, void *buffer, int count)
{
	if (count) {
		u16 *buf = buffer;
		do {
			u16 x = inw(port);
			*buf++ = x;
		} while (--count);
	}
}

static inline void insl(unsigned long port, void *buffer, int count)
{
	if (count) {
		u32 *buf = buffer;
		do {
			u32 x = inw(port);
			*buf++ = x;
		} while (--count);
	}
}

static inline void outsb(unsigned long port, const void *buffer, int count)
{
	if (count) {
		const u8 *buf = buffer;
		do {
			outb(*buf++, port);
		} while (--count);
	}
}

static inline void outsw(unsigned long port, const void *buffer, int count)
{
	if (count) {
		const u16 *buf = buffer;
		do {
			outw(*buf++, port);
		} while (--count);
	}
}

static inline void outsl(unsigned long port, const void *buffer, int count)
{
	if (count) {
		const u32 *buf = buffer;
		do {
			outl(*buf++, port);
		} while (--count);
	}
}

#endif /* __KERNEL__ */

#endif
