/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_M32R_IO_H
#define _ASM_M32R_IO_H

#include <linux/string.h>
#include <linux/compiler.h>
#include <asm/page.h>  /* __va */

#ifdef __KERNEL__

#define IO_SPACE_LIMIT  0xFFFFFFFF

/**
 *	virt_to_phys	-	map virtual addresses to physical
 *	@address: address to remap
 *
 *	The returned physical address is the physical (CPU) mapping for
 *	the memory address given. It is only valid to use this function on
 *	addresses directly mapped or allocated via kmalloc.
 *
 *	This function does not give bus mappings for DMA transfers. In
 *	almost all conceivable cases a device driver should not be using
 *	this function
 */

static inline unsigned long virt_to_phys(volatile void * address)
{
	return __pa(address);
}

/**
 *	phys_to_virt	-	map physical address to virtual
 *	@address: address to remap
 *
 *	The returned virtual address is a current CPU mapping for
 *	the memory address given. It is only valid to use this function on
 *	addresses that have a kernel mapping
 *
 *	This function does not handle bus mappings for DMA transfers. In
 *	almost all conceivable cases a device driver should not be using
 *	this function
 */

static inline void *phys_to_virt(unsigned long address)
{
	return __va(address);
}

extern void __iomem *
__ioremap(unsigned long offset, unsigned long size, unsigned long flags);

/**
 *	ioremap		-	map bus memory into CPU space
 *	@offset:	bus address of the memory
 *	@size:		size of the resource to map
 *
 *	ioremap performs a platform specific sequence of operations to
 *	make bus memory CPU accessible via the readb/readw/readl/writeb/
 *	writew/writel functions and the other mmio helpers. The returned
 *	address is not guaranteed to be usable directly as a virtual
 *	address.
 */

static inline void __iomem *ioremap(unsigned long offset, unsigned long size)
{
	return __ioremap(offset, size, 0);
}

extern void iounmap(volatile void __iomem *addr);
#define ioremap_nocache(off,size) ioremap(off,size)
#define ioremap_wc ioremap_nocache
#define ioremap_wt ioremap_nocache
#define ioremap_uc ioremap_nocache

/*
 * IO bus memory addresses are also 1:1 with the physical address
 */
#define page_to_phys(page)	(page_to_pfn(page) << PAGE_SHIFT)
#define page_to_bus	page_to_phys
#define virt_to_bus	virt_to_phys

extern unsigned char _inb(unsigned long);
extern unsigned short _inw(unsigned long);
extern unsigned long _inl(unsigned long);
extern unsigned char _inb_p(unsigned long);
extern unsigned short _inw_p(unsigned long);
extern unsigned long _inl_p(unsigned long);
extern void _outb(unsigned char, unsigned long);
extern void _outw(unsigned short, unsigned long);
extern void _outl(unsigned long, unsigned long);
extern void _outb_p(unsigned char, unsigned long);
extern void _outw_p(unsigned short, unsigned long);
extern void _outl_p(unsigned long, unsigned long);
extern void _insb(unsigned int, void *, unsigned long);
extern void _insw(unsigned int, void *, unsigned long);
extern void _insl(unsigned int, void *, unsigned long);
extern void _outsb(unsigned int, const void *, unsigned long);
extern void _outsw(unsigned int, const void *, unsigned long);
extern void _outsl(unsigned int, const void *, unsigned long);

static inline unsigned char _readb(unsigned long addr)
{
	return *(volatile unsigned char __force *)addr;
}

static inline unsigned short _readw(unsigned long addr)
{
	return *(volatile unsigned short __force *)addr;
}

static inline unsigned long _readl(unsigned long addr)
{
	return *(volatile unsigned long __force *)addr;
}

static inline void _writeb(unsigned char b, unsigned long addr)
{
	*(volatile unsigned char __force *)addr = b;
}

static inline void _writew(unsigned short w, unsigned long addr)
{
	*(volatile unsigned short __force *)addr = w;
}

static inline void _writel(unsigned long l, unsigned long addr)
{
	*(volatile unsigned long __force *)addr = l;
}

#define inb     _inb
#define inw     _inw
#define inl     _inl
#define outb    _outb
#define outw    _outw
#define outl    _outl

#define inb_p   _inb_p
#define inw_p   _inw_p
#define inl_p   _inl_p
#define outb_p  _outb_p
#define outw_p  _outw_p
#define outl_p  _outl_p

#define insb    _insb
#define insw    _insw
#define insl    _insl
#define outsb   _outsb
#define outsw   _outsw
#define outsl   _outsl

#define readb(addr)   _readb((unsigned long)(addr))
#define readw(addr)   _readw((unsigned long)(addr))
#define readl(addr)   _readl((unsigned long)(addr))
#define __raw_readb readb
#define __raw_readw readw
#define __raw_readl readl
#define readb_relaxed readb
#define readw_relaxed readw
#define readl_relaxed readl

#define writeb(val, addr)  _writeb((val), (unsigned long)(addr))
#define writew(val, addr)  _writew((val), (unsigned long)(addr))
#define writel(val, addr)  _writel((val), (unsigned long)(addr))
#define __raw_writeb writeb
#define __raw_writew writew
#define __raw_writel writel
#define writeb_relaxed writeb
#define writew_relaxed writew
#define writel_relaxed writel

#define ioread8 readb
#define ioread16 readw
#define ioread32 readl
#define iowrite8 writeb
#define iowrite16 writew
#define iowrite32 writel

#define ioread8_rep(p, dst, count) insb((unsigned long)(p), (dst), (count))
#define ioread16_rep(p, dst, count) insw((unsigned long)(p), (dst), (count))
#define ioread32_rep(p, dst, count) insl((unsigned long)(p), (dst), (count))

#define iowrite8_rep(p, src, count) outsb((unsigned long)(p), (src), (count))
#define iowrite16_rep(p, src, count) outsw((unsigned long)(p), (src), (count))
#define iowrite32_rep(p, src, count) outsl((unsigned long)(p), (src), (count))

#define ioread16be(addr)	be16_to_cpu(readw(addr))
#define ioread32be(addr)	be32_to_cpu(readl(addr))
#define iowrite16be(v, addr)	writew(cpu_to_be16(v), (addr))
#define iowrite32be(v, addr)	writel(cpu_to_be32(v), (addr))

#define mmiowb()

#define flush_write_buffers() do { } while (0)  /* M32R_FIXME */

static inline void
memset_io(volatile void __iomem *addr, unsigned char val, int count)
{
	memset((void __force *) addr, val, count);
}

static inline void
memcpy_fromio(void *dst, volatile void __iomem *src, int count)
{
	memcpy(dst, (void __force *) src, count);
}

static inline void
memcpy_toio(volatile void __iomem *dst, const void *src, int count)
{
	memcpy((void __force *) dst, src, count);
}

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)	__va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)	p

#endif  /* __KERNEL__ */

#endif  /* _ASM_M32R_IO_H */
