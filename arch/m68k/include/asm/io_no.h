/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _M68KNOMMU_IO_H
#define _M68KNOMMU_IO_H

/*
 * Convert a physical memory address into a IO memory address.
 * For us this is trivially a type cast.
 */
#define iomem(a)	((void __iomem *) (a))

/*
 * The non-MMU m68k and ColdFire IO and memory mapped hardware access
 * functions have always worked in CPU native endian. We need to define
 * that behavior here first before we include asm-generic/io.h.
 */
#define __raw_readb(addr) \
    ({ u8 __v = (*(__force volatile u8 *) (addr)); __v; })
#define __raw_readw(addr) \
    ({ u16 __v = (*(__force volatile u16 *) (addr)); __v; })
#define __raw_readl(addr) \
    ({ u32 __v = (*(__force volatile u32 *) (addr)); __v; })

#define __raw_writeb(b, addr) (void)((*(__force volatile u8 *) (addr)) = (b))
#define __raw_writew(b, addr) (void)((*(__force volatile u16 *) (addr)) = (b))
#define __raw_writel(b, addr) (void)((*(__force volatile u32 *) (addr)) = (b))

#if defined(CONFIG_COLDFIRE)
/*
 * For ColdFire platforms we may need to do some extra checks for what
 * type of address range we are accessing. Include the ColdFire platform
 * definitions so we can figure out if need to do something special.
 */
#include <asm/byteorder.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#endif /* CONFIG_COLDFIRE */

#if defined(IOMEMBASE)
/*
 * The ColdFire SoC internal peripherals are mapped into virtual address
 * space using the ACR registers of the cache control unit. This means we
 * are using a 1:1 physical:virtual mapping for them. We can quickly
 * determine if we are accessing an internal peripheral device given the
 * physical or vitrual address using the same range check. This check logic
 * applies just the same of there is no MMU but something like a PCI bus
 * is present.
 */
static int __cf_internalio(unsigned long addr)
{
	return (addr >= IOMEMBASE) && (addr <= IOMEMBASE + IOMEMSIZE - 1);
}

static int cf_internalio(const volatile void __iomem *addr)
{
	return __cf_internalio((unsigned long) addr);
}

/*
 * We need to treat built-in peripherals and bus based address ranges
 * differently. Local built-in peripherals (and the ColdFire SoC parts
 * have quite a lot of them) are always native endian - which is big
 * endian on m68k/ColdFire. Bus based address ranges, like the PCI bus,
 * are accessed little endian - so we need to byte swap those.
 */
#define readw readw
static inline u16 readw(const volatile void __iomem *addr)
{
	if (cf_internalio(addr))
		return __raw_readw(addr);
	return swab16(__raw_readw(addr));
}

#define readl readl
static inline u32 readl(const volatile void __iomem *addr)
{
	if (cf_internalio(addr))
		return __raw_readl(addr);
	return swab32(__raw_readl(addr));
}

#define writew writew
static inline void writew(u16 value, volatile void __iomem *addr)
{
	if (cf_internalio(addr))
		__raw_writew(value, addr);
	else
		__raw_writew(swab16(value), addr);
}

#define writel writel
static inline void writel(u32 value, volatile void __iomem *addr)
{
	if (cf_internalio(addr))
		__raw_writel(value, addr);
	else
		__raw_writel(swab32(value), addr);
}

#else

#define readb __raw_readb
#define readw __raw_readw
#define readl __raw_readl
#define writeb __raw_writeb
#define writew __raw_writew
#define writel __raw_writel

#endif /* IOMEMBASE */

#if defined(CONFIG_PCI)
/*
 * Support for PCI bus access uses the asm-generic access functions.
 * We need to supply the base address and masks for the normal memory
 * and IO address space mappings.
 */
#define PCI_MEM_PA	0xf0000000		/* Host physical address */
#define PCI_MEM_BA	0xf0000000		/* Bus physical address */
#define PCI_MEM_SIZE	0x08000000		/* 128 MB */
#define PCI_MEM_MASK	(PCI_MEM_SIZE - 1)

#define PCI_IO_PA	0xf8000000		/* Host physical address */
#define PCI_IO_BA	0x00000000		/* Bus physical address */
#define PCI_IO_SIZE	0x00010000		/* 64k */
#define PCI_IO_MASK	(PCI_IO_SIZE - 1)

#define PCI_IOBASE	((void __iomem *) PCI_IO_PA)
#define PCI_SPACE_LIMIT	PCI_IO_MASK
#endif /* CONFIG_PCI */

#include <asm/kmap.h>
#include <asm/virtconvert.h>

#endif /* _M68KNOMMU_IO_H */
