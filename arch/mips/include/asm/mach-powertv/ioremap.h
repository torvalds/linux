/*
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 * Portions Copyright (C)  Cisco Systems, Inc.
 */
#ifndef __ASM_MACH_POWERTV_IOREMAP_H
#define __ASM_MACH_POWERTV_IOREMAP_H

#include <linux/types.h>
#include <linux/log2.h>
#include <linux/compiler.h>

#include <asm/pgtable-bits.h>
#include <asm/addrspace.h>

/* We're going to mess with bits, so get sizes */
#define IOR_BPC			8			/* Bits per char */
#define IOR_PHYS_BITS		(IOR_BPC * sizeof(phys_addr_t))
#define IOR_DMA_BITS		(IOR_BPC * sizeof(dma_addr_t))

/*
 * Define the granularity of physical/DMA mapping in terms of the number
 * of bits that defines the offset within a grain. These will be the
 * least significant bits of the address. The rest of a physical or DMA
 * address will be used to index into an appropriate table to find the
 * offset to add to the address to yield the corresponding DMA or physical
 * address, respectively.
 */
#define IOR_LSBITS		22			/* Bits in a grain */

/*
 * Compute the number of most significant address bits after removing those
 * used for the offset within a grain and then compute the number of table
 * entries for the conversion.
 */
#define IOR_PHYS_MSBITS		(IOR_PHYS_BITS - IOR_LSBITS)
#define IOR_NUM_PHYS_TO_DMA	((phys_addr_t) 1 << IOR_PHYS_MSBITS)

#define IOR_DMA_MSBITS		(IOR_DMA_BITS - IOR_LSBITS)
#define IOR_NUM_DMA_TO_PHYS	((dma_addr_t) 1 << IOR_DMA_MSBITS)

/*
 * Define data structures used as elements in the arrays for the conversion
 * between physical and DMA addresses. We do some slightly fancy math to
 * compute the width of the offset element of the conversion tables so
 * that we can have the smallest conversion tables. Next, round up the
 * sizes to the next higher power of two, i.e. the offset element will have
 * 8, 16, 32, 64, etc. bits. This eliminates the need to mask off any
 * bits.  Finally, we compute a shift value that puts the most significant
 * bits of the offset into the most significant bits of the offset element.
 * This makes it more efficient on processors without barrel shifters and
 * easier to see the values if the conversion table is dumped in binary.
 */
#define _IOR_OFFSET_WIDTH(n)	(1 << order_base_2(n))
#define IOR_OFFSET_WIDTH(n) \
	(_IOR_OFFSET_WIDTH(n) < 8 ? 8 : _IOR_OFFSET_WIDTH(n))

#define IOR_PHYS_OFFSET_BITS	IOR_OFFSET_WIDTH(IOR_PHYS_MSBITS)
#define IOR_PHYS_SHIFT		(IOR_PHYS_BITS - IOR_PHYS_OFFSET_BITS)

#define IOR_DMA_OFFSET_BITS	IOR_OFFSET_WIDTH(IOR_DMA_MSBITS)
#define IOR_DMA_SHIFT		(IOR_DMA_BITS - IOR_DMA_OFFSET_BITS)

struct ior_phys_to_dma {
	dma_addr_t offset:IOR_DMA_OFFSET_BITS __packed
		__aligned((IOR_DMA_OFFSET_BITS / IOR_BPC));
};

struct ior_dma_to_phys {
	dma_addr_t offset:IOR_PHYS_OFFSET_BITS __packed
		__aligned((IOR_PHYS_OFFSET_BITS / IOR_BPC));
};

extern struct ior_phys_to_dma _ior_phys_to_dma[IOR_NUM_PHYS_TO_DMA];
extern struct ior_dma_to_phys _ior_dma_to_phys[IOR_NUM_DMA_TO_PHYS];

static inline dma_addr_t _phys_to_dma_offset_raw(phys_addr_t phys)
{
	return (dma_addr_t)_ior_phys_to_dma[phys >> IOR_LSBITS].offset;
}

static inline dma_addr_t _dma_to_phys_offset_raw(dma_addr_t dma)
{
	return (dma_addr_t)_ior_dma_to_phys[dma >> IOR_LSBITS].offset;
}

/* These are not portable and should not be used in drivers. Drivers should
 * be using ioremap() and friends to map physical addreses to virtual
 * addresses and dma_map*() and friends to map virtual addresses into DMA
 * addresses and back.
 */
static inline dma_addr_t phys_to_dma(phys_addr_t phys)
{
	return phys + (_phys_to_dma_offset_raw(phys) << IOR_PHYS_SHIFT);
}

static inline phys_addr_t dma_to_phys(dma_addr_t dma)
{
	return dma + (_dma_to_phys_offset_raw(dma) << IOR_DMA_SHIFT);
}

extern void ioremap_add_map(dma_addr_t phys, phys_addr_t alias,
	dma_addr_t size);

/*
 * Allow physical addresses to be fixed up to help peripherals located
 * outside the low 32-bit range -- generic pass-through version.
 */
static inline phys_t fixup_bigphys_addr(phys_t phys_addr, phys_t size)
{
	return phys_addr;
}

/*
 * Handle the special case of addresses the area aliased into the first
 * 512 MiB of the processor's physical address space. These turn into either
 * kseg0 or kseg1 addresses, depending on flags.
 */
static inline void __iomem *plat_ioremap(phys_t start, unsigned long size,
	unsigned long flags)
{
	phys_addr_t start_offset;
	void __iomem *result = NULL;

	/* Start by checking to see whether this is an aliased address */
	start_offset = _dma_to_phys_offset_raw(start);

	/*
	 * If:
	 * o	the memory is aliased into the first 512 MiB, and
	 * o	the start and end are in the same RAM bank, and
	 * o	we don't have a zero size or wrap around, and
	 * o	we are supposed to create an uncached mapping,
	 *	handle this is a kseg0 or kseg1 address
	 */
	if (start_offset != 0) {
		phys_addr_t last;
		dma_addr_t dma_to_phys_offset;

		last = start + size - 1;
		dma_to_phys_offset =
			_dma_to_phys_offset_raw(last) << IOR_DMA_SHIFT;

		if (dma_to_phys_offset == start_offset &&
			size != 0 && start <= last) {
			phys_t adjusted_start;
			adjusted_start = start + start_offset;
			if (flags == _CACHE_UNCACHED)
				result = (void __iomem *) (unsigned long)
					CKSEG1ADDR(adjusted_start);
			else
				result = (void __iomem *) (unsigned long)
					CKSEG0ADDR(adjusted_start);
		}
	}

	return result;
}

static inline int plat_iounmap(const volatile void __iomem *addr)
{
	return 0;
}
#endif /* __ASM_MACH_POWERTV_IOREMAP_H */
