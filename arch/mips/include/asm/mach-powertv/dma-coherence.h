/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Version from mach-generic modified to support PowerTV port
 * Portions Copyright (C) 2009	Cisco Systems, Inc.
 * Copyright (C) 2006  Ralf Baechle <ralf@linux-mips.org>
 *
 */

#ifndef __ASM_MACH_POWERTV_DMA_COHERENCE_H
#define __ASM_MACH_POWERTV_DMA_COHERENCE_H

#include <linux/sched.h>
#include <linux/device.h>
#include <asm/mach-powertv/asic.h>

static inline bool is_kseg2(void *addr)
{
	return (unsigned long)addr >= KSEG2;
}

static inline unsigned long virt_to_phys_from_pte(void *addr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep, pte;

	unsigned long virt_addr = (unsigned long)addr;
	unsigned long phys_addr = 0UL;

	/* get the page global directory. */
	pgd = pgd_offset_k(virt_addr);

	if (!pgd_none(*pgd)) {
		/* get the page upper directory */
		pud = pud_offset(pgd, virt_addr);
		if (!pud_none(*pud)) {
			/* get the page middle directory */
			pmd = pmd_offset(pud, virt_addr);
			if (!pmd_none(*pmd)) {
				/* get a pointer to the page table entry */
				ptep = pte_offset(pmd, virt_addr);
				pte = *ptep;
				/* check for a valid page */
				if (pte_present(pte)) {
					/* get the physical address the page is
					 * referring to */
					phys_addr = (unsigned long)
						page_to_phys(pte_page(pte));
					/* add the offset within the page */
					phys_addr |= (virt_addr & ~PAGE_MASK);
				}
			}
		}
	}

	return phys_addr;
}

static inline dma_addr_t plat_map_dma_mem(struct device *dev, void *addr,
	size_t size)
{
	if (is_kseg2(addr))
		return phys_to_dma(virt_to_phys_from_pte(addr));
	else
		return phys_to_dma(virt_to_phys(addr));
}

static inline dma_addr_t plat_map_dma_mem_page(struct device *dev,
	struct page *page)
{
	return phys_to_dma(page_to_phys(page));
}

static inline unsigned long plat_dma_addr_to_phys(struct device *dev,
	dma_addr_t dma_addr)
{
	return dma_to_phys(dma_addr);
}

static inline void plat_unmap_dma_mem(struct device *dev, dma_addr_t dma_addr,
	size_t size, enum dma_data_direction direction)
{
}

static inline int plat_dma_supported(struct device *dev, u64 mask)
{
	/*
	 * we fall back to GFP_DMA when the mask isn't all 1s,
	 * so we can't guarantee allocations that must be
	 * within a tighter range than GFP_DMA..
	 */
	if (mask < DMA_BIT_MASK(24))
		return 0;

	return 1;
}

static inline void plat_extra_sync_for_device(struct device *dev)
{
}

static inline int plat_dma_mapping_error(struct device *dev,
					 dma_addr_t dma_addr)
{
	return 0;
}

static inline int plat_device_is_coherent(struct device *dev)
{
	return 0;
}

#endif /* __ASM_MACH_POWERTV_DMA_COHERENCE_H */
