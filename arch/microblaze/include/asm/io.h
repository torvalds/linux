/*
 * Copyright (C) 2007-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_IO_H
#define _ASM_MICROBLAZE_IO_H

#include <asm/byteorder.h>
#include <asm/page.h>
#include <linux/types.h>
#include <linux/mm.h>          /* Get struct page {...} */


#define IO_SPACE_LIMIT (0xFFFFFFFF)

static inline unsigned char __raw_readb(const volatile void __iomem *addr)
{
	return *(volatile unsigned char __force *)addr;
}
static inline unsigned short __raw_readw(const volatile void __iomem *addr)
{
	return *(volatile unsigned short __force *)addr;
}
static inline unsigned int __raw_readl(const volatile void __iomem *addr)
{
	return *(volatile unsigned int __force *)addr;
}
static inline unsigned long __raw_readq(const volatile void __iomem *addr)
{
	return *(volatile unsigned long __force *)addr;
}
static inline void __raw_writeb(unsigned char v, volatile void __iomem *addr)
{
	*(volatile unsigned char __force *)addr = v;
}
static inline void __raw_writew(unsigned short v, volatile void __iomem *addr)
{
	*(volatile unsigned short __force *)addr = v;
}
static inline void __raw_writel(unsigned int v, volatile void __iomem *addr)
{
	*(volatile unsigned int __force *)addr = v;
}
static inline void __raw_writeq(unsigned long v, volatile void __iomem *addr)
{
	*(volatile unsigned long __force *)addr = v;
}

/*
 * read (readb, readw, readl, readq) and write (writeb, writew,
 * writel, writeq) accessors are for PCI and thus littel endian.
 * Linux 2.4 for Microblaze had this wrong.
 */
static inline unsigned char readb(const volatile void __iomem *addr)
{
	return *(volatile unsigned char __force *)addr;
}
static inline unsigned short readw(const volatile void __iomem *addr)
{
	return le16_to_cpu(*(volatile unsigned short __force *)addr);
}
static inline unsigned int readl(const volatile void __iomem *addr)
{
	return le32_to_cpu(*(volatile unsigned int __force *)addr);
}
static inline void writeb(unsigned char v, volatile void __iomem *addr)
{
	*(volatile unsigned char __force *)addr = v;
}
static inline void writew(unsigned short v, volatile void __iomem *addr)
{
	*(volatile unsigned short __force *)addr = cpu_to_le16(v);
}
static inline void writel(unsigned int v, volatile void __iomem *addr)
{
	*(volatile unsigned int __force *)addr = cpu_to_le32(v);
}

/* ioread and iowrite variants. thease are for now same as __raw_
 * variants of accessors. we might check for endianess in the feature
 */
#define ioread8(addr)		__raw_readb((u8 *)(addr))
#define ioread16(addr)		__raw_readw((u16 *)(addr))
#define ioread32(addr)		__raw_readl((u32 *)(addr))
#define iowrite8(v, addr)	__raw_writeb((u8)(v), (u8 *)(addr))
#define iowrite16(v, addr)	__raw_writew((u16)(v), (u16 *)(addr))
#define iowrite32(v, addr)	__raw_writel((u32)(v), (u32 *)(addr))

/* These are the definitions for the x86 IO instructions
 * inb/inw/inl/outb/outw/outl, the "string" versions
 * insb/insw/insl/outsb/outsw/outsl, and the "pausing" versions
 * inb_p/inw_p/...
 * The macros don't do byte-swapping.
 */
#define inb(port)		readb((u8 *)((port)))
#define outb(val, port)		writeb((val), (u8 *)((unsigned long)(port)))
#define inw(port)		readw((u16 *)((port)))
#define outw(val, port)		writew((val), (u16 *)((unsigned long)(port)))
#define inl(port)		readl((u32 *)((port)))
#define outl(val, port)		writel((val), (u32 *)((unsigned long)(port)))

#define inb_p(port)		inb((port))
#define outb_p(val, port)	outb((val), (port))
#define inw_p(port)		inw((port))
#define outw_p(val, port)	outw((val), (port))
#define inl_p(port)		inl((port))
#define outl_p(val, port)	outl((val), (port))

#define memset_io(a, b, c)	memset((void *)(a), (b), (c))
#define memcpy_fromio(a, b, c)	memcpy((a), (void *)(b), (c))
#define memcpy_toio(a, b, c)	memcpy((void *)(a), (b), (c))

#ifdef CONFIG_MMU

#define mm_ptov(addr)		((void *)__phys_to_virt(addr))
#define mm_vtop(addr)		((unsigned long)__virt_to_phys(addr))
#define phys_to_virt(addr)	((void *)__phys_to_virt(addr))
#define virt_to_phys(addr)	((unsigned long)__virt_to_phys(addr))
#define virt_to_bus(addr)	((unsigned long)__virt_to_phys(addr))

#define __page_address(page) \
		(PAGE_OFFSET + (((page) - mem_map) << PAGE_SHIFT))
#define page_to_phys(page)	virt_to_phys((void *)__page_address(page))
#define page_to_bus(page)	(page_to_phys(page))
#define bus_to_virt(addr)	(phys_to_virt(addr))

extern void iounmap(void *addr);
/*extern void *__ioremap(phys_addr_t address, unsigned long size,
		unsigned long flags);*/
extern void __iomem *ioremap(phys_addr_t address, unsigned long size);
#define ioremap_writethrough(addr, size) ioremap((addr), (size))
#define ioremap_nocache(addr, size)      ioremap((addr), (size))
#define ioremap_fullcache(addr, size)    ioremap((addr), (size))

#else /* CONFIG_MMU */

/**
 *	virt_to_phys - map virtual addresses to physical
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
static inline unsigned long __iomem virt_to_phys(volatile void *address)
{
	return __pa((unsigned long)address);
}

#define virt_to_bus virt_to_phys

/**
 *	phys_to_virt - map physical address to virtual
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
	return (void *)__va(address);
}

#define bus_to_virt(a) phys_to_virt(a)

static inline void __iomem *__ioremap(phys_addr_t address, unsigned long size,
			unsigned long flags)
{
	return (void *)address;
}

#define ioremap(physaddr, size)	((void __iomem *)(unsigned long)(physaddr))
#define iounmap(addr)		((void)0)
#define ioremap_nocache(physaddr, size)	ioremap(physaddr, size)

#endif /* CONFIG_MMU */

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)	__va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)	p

/*
 * Big Endian
 */
#define out_be32(a, v) __raw_writel((v), (void __iomem __force *)(a))
#define out_be16(a, v) __raw_writew((v), (a))

#define in_be32(a) __raw_readl((const void __iomem __force *)(a))
#define in_be16(a) __raw_readw(a)

#define writel_be(v, a)	out_be32((__force unsigned *)a, v)
#define readl_be(a)	in_be32((__force unsigned *)a)

/*
 * Little endian
 */

#define out_le32(a, v) __raw_writel(__cpu_to_le32(v), (a));
#define out_le16(a, v) __raw_writew(__cpu_to_le16(v), (a))

#define in_le32(a) __le32_to_cpu(__raw_readl(a))
#define in_le16(a) __le16_to_cpu(__raw_readw(a))

/* Byte ops */
#define out_8(a, v) __raw_writeb((v), (a))
#define in_8(a) __raw_readb(a)

/* FIXME */
static inline void __iomem *ioport_map(unsigned long port, unsigned int len)
{
	return (void __iomem *) (port);
}

static inline void ioport_unmap(void __iomem *addr)
{
	/* Nothing to do */
}

#endif /* _ASM_MICROBLAZE_IO_H */
