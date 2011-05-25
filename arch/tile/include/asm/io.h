/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_IO_H
#define _ASM_TILE_IO_H

#include <linux/kernel.h>
#include <linux/bug.h>
#include <asm/page.h>

#define IO_SPACE_LIMIT 0xfffffffful

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access.
 */
#define xlate_dev_mem_ptr(p)	__va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer.
 */
#define xlate_dev_kmem_ptr(p)	p

/*
 * Change "struct page" to physical address.
 */
#define page_to_phys(page)    ((dma_addr_t)page_to_pfn(page) << PAGE_SHIFT)

/*
 * Some places try to pass in an loff_t for PHYSADDR (?!), so we cast it to
 * long before casting it to a pointer to avoid compiler warnings.
 */
#if CHIP_HAS_MMIO()
extern void __iomem *ioremap(resource_size_t offset, unsigned long size);
extern void __iomem *ioremap_prot(resource_size_t offset, unsigned long size,
	pgprot_t pgprot);
extern void iounmap(volatile void __iomem *addr);
#else
#define ioremap(physaddr, size)	((void __iomem *)(unsigned long)(physaddr))
#define iounmap(addr)		((void)0)
#endif

#define ioremap_nocache(physaddr, size)		ioremap(physaddr, size)
#define ioremap_wc(physaddr, size)		ioremap(physaddr, size)
#define ioremap_writethrough(physaddr, size)	ioremap(physaddr, size)
#define ioremap_fullcache(physaddr, size)	ioremap(physaddr, size)

#define mmiowb()

/* Conversion between virtual and physical mappings.  */
#define mm_ptov(addr)		((void *)phys_to_virt(addr))
#define mm_vtop(addr)		((unsigned long)virt_to_phys(addr))

#ifdef CONFIG_PCI

extern u8 _tile_readb(unsigned long addr);
extern u16 _tile_readw(unsigned long addr);
extern u32 _tile_readl(unsigned long addr);
extern u64 _tile_readq(unsigned long addr);
extern void _tile_writeb(u8  val, unsigned long addr);
extern void _tile_writew(u16 val, unsigned long addr);
extern void _tile_writel(u32 val, unsigned long addr);
extern void _tile_writeq(u64 val, unsigned long addr);

#else

/*
 * The Tile architecture does not support IOMEM unless PCI is enabled.
 * Unfortunately we can't yet simply not declare these methods,
 * since some generic code that compiles into the kernel, but
 * we never run, uses them unconditionally.
 */

static inline int iomem_panic(void)
{
	panic("readb/writeb and friends do not exist on tile without PCI");
	return 0;
}

static inline u8 _tile_readb(unsigned long addr)
{
	return iomem_panic();
}

static inline u16 _tile_readw(unsigned long addr)
{
	return iomem_panic();
}

static inline u32 _tile_readl(unsigned long addr)
{
	return iomem_panic();
}

static inline u64 _tile_readq(unsigned long addr)
{
	return iomem_panic();
}

static inline void _tile_writeb(u8  val, unsigned long addr)
{
	iomem_panic();
}

static inline void _tile_writew(u16 val, unsigned long addr)
{
	iomem_panic();
}

static inline void _tile_writel(u32 val, unsigned long addr)
{
	iomem_panic();
}

static inline void _tile_writeq(u64 val, unsigned long addr)
{
	iomem_panic();
}

#endif

#define readb(addr) _tile_readb((unsigned long)addr)
#define readw(addr) _tile_readw((unsigned long)addr)
#define readl(addr) _tile_readl((unsigned long)addr)
#define readq(addr) _tile_readq((unsigned long)addr)
#define writeb(val, addr) _tile_writeb(val, (unsigned long)addr)
#define writew(val, addr) _tile_writew(val, (unsigned long)addr)
#define writel(val, addr) _tile_writel(val, (unsigned long)addr)
#define writeq(val, addr) _tile_writeq(val, (unsigned long)addr)

#define __raw_readb readb
#define __raw_readw readw
#define __raw_readl readl
#define __raw_readq readq
#define __raw_writeb writeb
#define __raw_writew writew
#define __raw_writel writel
#define __raw_writeq writeq

#define readb_relaxed readb
#define readw_relaxed readw
#define readl_relaxed readl
#define readq_relaxed readq

#define ioread8 readb
#define ioread16 readw
#define ioread32 readl
#define ioread64 readq
#define iowrite8 writeb
#define iowrite16 writew
#define iowrite32 writel
#define iowrite64 writeq

static inline void memset_io(void *dst, int val, size_t len)
{
	int x;
	BUG_ON((unsigned long)dst & 0x3);
	val = (val & 0xff) * 0x01010101;
	for (x = 0; x < len; x += 4)
		writel(val, dst + x);
}

static inline void memcpy_fromio(void *dst, const volatile void __iomem *src,
				 size_t len)
{
	int x;
	BUG_ON((unsigned long)src & 0x3);
	for (x = 0; x < len; x += 4)
		*(u32 *)(dst + x) = readl(src + x);
}

static inline void memcpy_toio(volatile void __iomem *dst, const void *src,
				size_t len)
{
	int x;
	BUG_ON((unsigned long)dst & 0x3);
	for (x = 0; x < len; x += 4)
		writel(*(u32 *)(src + x), dst + x);
}

/*
 * The Tile architecture does not support IOPORT, even with PCI.
 * Unfortunately we can't yet simply not declare these methods,
 * since some generic code that compiles into the kernel, but
 * we never run, uses them unconditionally.
 */

static inline long ioport_panic(void)
{
	panic("inb/outb and friends do not exist on tile");
	return 0;
}

static inline void __iomem *ioport_map(unsigned long port, unsigned int len)
{
	return (void __iomem *) ioport_panic();
}

static inline void ioport_unmap(void __iomem *addr)
{
	ioport_panic();
}

static inline u8 inb(unsigned long addr)
{
	return ioport_panic();
}

static inline u16 inw(unsigned long addr)
{
	return ioport_panic();
}

static inline u32 inl(unsigned long addr)
{
	return ioport_panic();
}

static inline void outb(u8 b, unsigned long addr)
{
	ioport_panic();
}

static inline void outw(u16 b, unsigned long addr)
{
	ioport_panic();
}

static inline void outl(u32 b, unsigned long addr)
{
	ioport_panic();
}

#define inb_p(addr)	inb(addr)
#define inw_p(addr)	inw(addr)
#define inl_p(addr)	inl(addr)
#define outb_p(x, addr)	outb((x), (addr))
#define outw_p(x, addr)	outw((x), (addr))
#define outl_p(x, addr)	outl((x), (addr))

static inline void insb(unsigned long addr, void *buffer, int count)
{
	ioport_panic();
}

static inline void insw(unsigned long addr, void *buffer, int count)
{
	ioport_panic();
}

static inline void insl(unsigned long addr, void *buffer, int count)
{
	ioport_panic();
}

static inline void outsb(unsigned long addr, const void *buffer, int count)
{
	ioport_panic();
}

static inline void outsw(unsigned long addr, const void *buffer, int count)
{
	ioport_panic();
}

static inline void outsl(unsigned long addr, const void *buffer, int count)
{
	ioport_panic();
}

#define ioread16be(addr)	be16_to_cpu(ioread16(addr))
#define ioread32be(addr)	be32_to_cpu(ioread32(addr))
#define iowrite16be(v, addr)	iowrite16(be16_to_cpu(v), (addr))
#define iowrite32be(v, addr)	iowrite32(be32_to_cpu(v), (addr))

#define ioread8_rep(p, dst, count) \
	insb((unsigned long) (p), (dst), (count))
#define ioread16_rep(p, dst, count) \
	insw((unsigned long) (p), (dst), (count))
#define ioread32_rep(p, dst, count) \
	insl((unsigned long) (p), (dst), (count))

#define iowrite8_rep(p, src, count) \
	outsb((unsigned long) (p), (src), (count))
#define iowrite16_rep(p, src, count) \
	outsw((unsigned long) (p), (src), (count))
#define iowrite32_rep(p, src, count) \
	outsl((unsigned long) (p), (src), (count))

#define virt_to_bus     virt_to_phys
#define bus_to_virt     phys_to_virt

#endif /* _ASM_TILE_IO_H */
