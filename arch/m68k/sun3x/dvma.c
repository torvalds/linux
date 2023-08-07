// SPDX-License-Identifier: GPL-2.0
/*
 * Virtual DMA allocation
 *
 * (C) 1999 Thomas Bogendoerfer (tsbogend@alpha.franken.de)
 *
 * 11/26/2000 -- disabled the existing code because it didn't work for
 * me in 2.4.  Replaced with a significantly more primitive version
 * similar to the sun3 code.  the old functionality was probably more
 * desirable, but....   -- Sam Creasey (sammy@oh.verio.com)
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/vmalloc.h>

#include <asm/sun3x.h>
#include <asm/dvma.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/tlbflush.h>

/* IOMMU support */

#define IOMMU_ADDR_MASK            0x03ffe000
#define IOMMU_CACHE_INHIBIT        0x00000040
#define IOMMU_FULL_BLOCK           0x00000020
#define IOMMU_MODIFIED             0x00000010
#define IOMMU_USED                 0x00000008
#define IOMMU_WRITE_PROTECT        0x00000004
#define IOMMU_DT_MASK              0x00000003
#define IOMMU_DT_INVALID           0x00000000
#define IOMMU_DT_VALID             0x00000001
#define IOMMU_DT_BAD               0x00000002


static volatile unsigned long *iommu_pte = (unsigned long *)SUN3X_IOMMU;


#define dvma_entry_paddr(index)		(iommu_pte[index] & IOMMU_ADDR_MASK)
#define dvma_entry_vaddr(index,paddr)	((index << DVMA_PAGE_SHIFT) |  \
					 (paddr & (DVMA_PAGE_SIZE-1)))
#if 0
#define dvma_entry_set(index,addr)	(iommu_pte[index] =            \
					    (addr & IOMMU_ADDR_MASK) | \
				             IOMMU_DT_VALID | IOMMU_CACHE_INHIBIT)
#else
#define dvma_entry_set(index,addr)	(iommu_pte[index] =            \
					    (addr & IOMMU_ADDR_MASK) | \
				             IOMMU_DT_VALID)
#endif
#define dvma_entry_clr(index)		(iommu_pte[index] = IOMMU_DT_INVALID)
#define dvma_entry_hash(addr)		((addr >> DVMA_PAGE_SHIFT) ^ \
					 ((addr & 0x03c00000) >>     \
						(DVMA_PAGE_SHIFT+4)))

#ifdef DEBUG
/* code to print out a dvma mapping for debugging purposes */
void dvma_print (unsigned long dvma_addr)
{

	unsigned long index;

	index = dvma_addr >> DVMA_PAGE_SHIFT;

	pr_info("idx %lx dvma_addr %08lx paddr %08lx\n", index, dvma_addr,
		dvma_entry_paddr(index));
}
#endif


/* create a virtual mapping for a page assigned within the IOMMU
   so that the cpu can reach it easily */
inline int dvma_map_cpu(unsigned long kaddr,
			       unsigned long vaddr, int len)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	unsigned long end;
	int ret = 0;

	kaddr &= PAGE_MASK;
	vaddr &= PAGE_MASK;

	end = PAGE_ALIGN(vaddr + len);

	pr_debug("dvma: mapping kern %08lx to virt %08lx\n", kaddr, vaddr);
	pgd = pgd_offset_k(vaddr);
	p4d = p4d_offset(pgd, vaddr);
	pud = pud_offset(p4d, vaddr);

	do {
		pmd_t *pmd;
		unsigned long end2;

		if((pmd = pmd_alloc(&init_mm, pud, vaddr)) == NULL) {
			ret = -ENOMEM;
			goto out;
		}

		if((end & PGDIR_MASK) > (vaddr & PGDIR_MASK))
			end2 = (vaddr + (PGDIR_SIZE-1)) & PGDIR_MASK;
		else
			end2 = end;

		do {
			pte_t *pte;
			unsigned long end3;

			if((pte = pte_alloc_kernel(pmd, vaddr)) == NULL) {
				ret = -ENOMEM;
				goto out;
			}

			if((end2 & PMD_MASK) > (vaddr & PMD_MASK))
				end3 = (vaddr + (PMD_SIZE-1)) & PMD_MASK;
			else
				end3 = end2;

			do {
				pr_debug("mapping %08lx phys to %08lx\n",
					 __pa(kaddr), vaddr);
				set_pte(pte, pfn_pte(virt_to_pfn((void *)kaddr),
						     PAGE_KERNEL));
				pte++;
				kaddr += PAGE_SIZE;
				vaddr += PAGE_SIZE;
			} while(vaddr < end3);

		} while(vaddr < end2);

	} while(vaddr < end);

	flush_tlb_all();

 out:
	return ret;
}


inline int dvma_map_iommu(unsigned long kaddr, unsigned long baddr,
				 int len)
{
	unsigned long end, index;

	index = baddr >> DVMA_PAGE_SHIFT;
	end = ((baddr+len) >> DVMA_PAGE_SHIFT);

	if(len & ~DVMA_PAGE_MASK)
		end++;

	for(; index < end ; index++) {
//		if(dvma_entry_use(index))
//			BUG();
//		pr_info("mapping pa %lx to ba %lx\n", __pa(kaddr),
//			index << DVMA_PAGE_SHIFT);

		dvma_entry_set(index, __pa(kaddr));

		iommu_pte[index] |= IOMMU_FULL_BLOCK;
//		dvma_entry_inc(index);

		kaddr += DVMA_PAGE_SIZE;
	}

#ifdef DEBUG
	for(index = (baddr >> DVMA_PAGE_SHIFT); index < end; index++)
		dvma_print(index << DVMA_PAGE_SHIFT);
#endif
	return 0;

}

void dvma_unmap_iommu(unsigned long baddr, int len)
{

	int index, end;


	index = baddr >> DVMA_PAGE_SHIFT;
	end = (DVMA_PAGE_ALIGN(baddr+len) >> DVMA_PAGE_SHIFT);

	for(; index < end ; index++) {
		pr_debug("freeing bus mapping %08x\n",
			 index << DVMA_PAGE_SHIFT);
#if 0
		if(!dvma_entry_use(index))
			pr_info("dvma_unmap freeing unused entry %04x\n",
				index);
		else
			dvma_entry_dec(index);
#endif
		dvma_entry_clr(index);
	}

}
