/*
 *			ioremap.c
 *
 * Support for mapping between dma_addr_t values a phys_addr_t values.
 *
 * Copyright (C) 2005-2009 Scientific-Atlanta, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Author:       David VomLehn <dvomlehn@cisco.com>
 *
 * Description:  Defines the platform resources for the SA settop.
 *
 * NOTE: The bootloader allocates persistent memory at an address which is
 * 16 MiB below the end of the highest address in KSEG0. All fixed
 * address memory reservations must avoid this region.
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include <asm/mach-powertv/ioremap.h>

/*
 * Define the sizes of and masks for grains in physical and DMA space. The
 * values are the same but the types are not.
 */
#define IOR_PHYS_GRAIN		((phys_addr_t) 1 << IOR_LSBITS)
#define IOR_PHYS_GRAIN_MASK	(IOR_PHYS_GRAIN - 1)

#define IOR_DMA_GRAIN		((dma_addr_t) 1 << IOR_LSBITS)
#define IOR_DMA_GRAIN_MASK	(IOR_DMA_GRAIN - 1)

/*
 * Values that, when accessed by an index derived from a phys_addr_t and
 * added to phys_addr_t value, yield a DMA address
 */
struct ior_phys_to_dma _ior_phys_to_dma[IOR_NUM_PHYS_TO_DMA];
EXPORT_SYMBOL(_ior_phys_to_dma);

/*
 * Values that, when accessed by an index derived from a dma_addr_t and
 * added to that dma_addr_t value, yield a physical address
 */
struct ior_dma_to_phys _ior_dma_to_phys[IOR_NUM_DMA_TO_PHYS];
EXPORT_SYMBOL(_ior_dma_to_phys);

/**
 * setup_dma_to_phys - set up conversion from DMA to physical addresses
 * @dma_idx:	Top IOR_LSBITS bits of the DMA address, i.e. an index
 *		into the array _dma_to_phys.
 * @delta:	Value that, when added to the DMA address, will yield the
 *		physical address
 * @s:		Number of bytes in the section of memory with the given delta
 *		between DMA and physical addresses.
 */
static void setup_dma_to_phys(dma_addr_t dma, phys_addr_t delta, dma_addr_t s)
{
	int dma_idx, first_idx, last_idx;
	phys_addr_t first, last;

	/*
	 * Calculate the first and last indices, rounding the first up and
	 * the second down.
	 */
	first = dma & ~IOR_DMA_GRAIN_MASK;
	last = (dma + s - 1) & ~IOR_DMA_GRAIN_MASK;
	first_idx = first >> IOR_LSBITS;		/* Convert to indices */
	last_idx = last >> IOR_LSBITS;

	for (dma_idx = first_idx; dma_idx <= last_idx; dma_idx++)
		_ior_dma_to_phys[dma_idx].offset = delta >> IOR_DMA_SHIFT;
}

/**
 * setup_phys_to_dma - set up conversion from DMA to physical addresses
 * @phys_idx:	Top IOR_LSBITS bits of the DMA address, i.e. an index
 *		into the array _phys_to_dma.
 * @delta:	Value that, when added to the DMA address, will yield the
 *		physical address
 * @s:		Number of bytes in the section of memory with the given delta
 *		between DMA and physical addresses.
 */
static void setup_phys_to_dma(phys_addr_t phys, dma_addr_t delta, phys_addr_t s)
{
	int phys_idx, first_idx, last_idx;
	phys_addr_t first, last;

	/*
	 * Calculate the first and last indices, rounding the first up and
	 * the second down.
	 */
	first = phys & ~IOR_PHYS_GRAIN_MASK;
	last = (phys + s - 1) & ~IOR_PHYS_GRAIN_MASK;
	first_idx = first >> IOR_LSBITS;		/* Convert to indices */
	last_idx = last >> IOR_LSBITS;

	for (phys_idx = first_idx; phys_idx <= last_idx; phys_idx++)
		_ior_phys_to_dma[phys_idx].offset = delta >> IOR_PHYS_SHIFT;
}

/**
 * ioremap_add_map - add to the physical and DMA address conversion arrays
 * @phys:	Process's view of the address of the start of the memory chunk
 * @dma:	DMA address of the start of the memory chunk
 * @size:	Size, in bytes, of the chunk of memory
 *
 * NOTE: It might be obvious, but the assumption is that all @size bytes have
 * the same offset between the physical address and the DMA address.
 */
void ioremap_add_map(phys_addr_t phys, phys_addr_t dma, phys_addr_t size)
{
	if (size == 0)
		return;

	if ((dma & IOR_DMA_GRAIN_MASK) != 0 ||
		(phys & IOR_PHYS_GRAIN_MASK) != 0 ||
		(size & IOR_PHYS_GRAIN_MASK) != 0)
		pr_crit("Memory allocation must be in chunks of 0x%x bytes\n",
			IOR_PHYS_GRAIN);

	setup_dma_to_phys(dma, phys - dma, size);
	setup_phys_to_dma(phys, dma - phys, size);
}
