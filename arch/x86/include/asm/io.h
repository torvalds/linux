#ifndef _ASM_X86_IO_H
#define _ASM_X86_IO_H

#define ARCH_HAS_IOREMAP_WC

#include <linux/compiler.h>
#include <asm-generic/int-ll64.h>
#include <asm/page.h>

#define build_mmio_read(name, size, type, reg, barrier) \
static inline type name(const volatile void __iomem *addr) \
{ type ret; asm volatile("mov" size " %1,%0":reg (ret) \
:"m" (*(volatile type __force *)addr) barrier); return ret; }

#define build_mmio_write(name, size, type, reg, barrier) \
static inline void name(type val, volatile void __iomem *addr) \
{ asm volatile("mov" size " %0,%1": :reg (val), \
"m" (*(volatile type __force *)addr) barrier); }

build_mmio_read(readb, "b", unsigned char, "=q", :"memory")
build_mmio_read(readw, "w", unsigned short, "=r", :"memory")
build_mmio_read(readl, "l", unsigned int, "=r", :"memory")

build_mmio_read(__readb, "b", unsigned char, "=q", )
build_mmio_read(__readw, "w", unsigned short, "=r", )
build_mmio_read(__readl, "l", unsigned int, "=r", )

build_mmio_write(writeb, "b", unsigned char, "q", :"memory")
build_mmio_write(writew, "w", unsigned short, "r", :"memory")
build_mmio_write(writel, "l", unsigned int, "r", :"memory")

build_mmio_write(__writeb, "b", unsigned char, "q", )
build_mmio_write(__writew, "w", unsigned short, "r", )
build_mmio_write(__writel, "l", unsigned int, "r", )

#define readb_relaxed(a) __readb(a)
#define readw_relaxed(a) __readw(a)
#define readl_relaxed(a) __readl(a)
#define __raw_readb __readb
#define __raw_readw __readw
#define __raw_readl __readl

#define __raw_writeb __writeb
#define __raw_writew __writew
#define __raw_writel __writel

#define mmiowb() barrier()

#ifdef CONFIG_X86_64

build_mmio_read(readq, "q", unsigned long, "=r", :"memory")
build_mmio_write(writeq, "q", unsigned long, "r", :"memory")

#else

static inline __u64 readq(const volatile void __iomem *addr)
{
	const volatile u32 __iomem *p = addr;
	u32 low, high;

	low = readl(p);
	high = readl(p + 1);

	return low + ((u64)high << 32);
}

static inline void writeq(__u64 val, volatile void __iomem *addr)
{
	writel(val, addr);
	writel(val >> 32, addr+4);
}

#endif

#define readq_relaxed(a)	readq(a)

#define __raw_readq(a)		readq(a)
#define __raw_writeq(val, addr)	writeq(val, addr)

/* Let people know that we have them */
#define readq			readq
#define writeq			writeq

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

static inline phys_addr_t virt_to_phys(volatile void *address)
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

static inline void *phys_to_virt(phys_addr_t address)
{
	return __va(address);
}

/*
 * Change "struct page" to physical address.
 */
#define page_to_phys(page)    ((dma_addr_t)page_to_pfn(page) << PAGE_SHIFT)

/*
 * ISA I/O bus memory addresses are 1:1 with the physical address.
 * However, we truncate the address to unsigned int to avoid undesirable
 * promitions in legacy drivers.
 */
static inline unsigned int isa_virt_to_bus(volatile void *address)
{
	return (unsigned int)virt_to_phys(address);
}
#define isa_page_to_bus(page)	((unsigned int)page_to_phys(page))
#define isa_bus_to_virt		phys_to_virt

/*
 * However PCI ones are not necessarily 1:1 and therefore these interfaces
 * are forbidden in portable PCI drivers.
 *
 * Allow them on x86 for legacy drivers, though.
 */
#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt

/**
 * ioremap     -   map bus memory into CPU space
 * @offset:    bus address of the memory
 * @size:      size of the resource to map
 *
 * ioremap performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 *
 * If the area you are trying to map is a PCI BAR you should have a
 * look at pci_iomap().
 */
extern void __iomem *ioremap_nocache(resource_size_t offset, unsigned long size);
extern void __iomem *ioremap_cache(resource_size_t offset, unsigned long size);
extern void __iomem *ioremap_prot(resource_size_t offset, unsigned long size,
				unsigned long prot_val);

/*
 * The default ioremap() behavior is non-cached:
 */
static inline void __iomem *ioremap(resource_size_t offset, unsigned long size)
{
	return ioremap_nocache(offset, size);
}

extern void iounmap(volatile void __iomem *addr);


#ifdef CONFIG_X86_32
# include "io_32.h"
#else
# include "io_64.h"
#endif

extern void *xlate_dev_mem_ptr(unsigned long phys);
extern void unxlate_dev_mem_ptr(unsigned long phys, void *addr);

extern int ioremap_change_attr(unsigned long vaddr, unsigned long size,
				unsigned long prot_val);
extern void __iomem *ioremap_wc(resource_size_t offset, unsigned long size);

/*
 * early_ioremap() and early_iounmap() are for temporary early boot-time
 * mappings, before the real ioremap() is functional.
 * A boot-time mapping is currently limited to at most 16 pages.
 */
extern void early_ioremap_init(void);
extern void early_ioremap_reset(void);
extern void __iomem *early_ioremap(resource_size_t phys_addr,
				   unsigned long size);
extern void __iomem *early_memremap(resource_size_t phys_addr,
				    unsigned long size);
extern void early_iounmap(void __iomem *addr, unsigned long size);

#define IO_SPACE_LIMIT 0xffff

#endif /* _ASM_X86_IO_H */
