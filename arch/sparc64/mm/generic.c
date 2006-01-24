/* $Id: generic.c,v 1.18 2001/12/21 04:56:15 davem Exp $
 * generic.c: Generic Sparc mm routines that are not dependent upon
 *            MMU type but are Sparc specific.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/pagemap.h>

#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/tlbflush.h>

static inline pte_t mk_pte_io(unsigned long page, pgprot_t prot, int space)
{
	pte_t pte;
	pte_val(pte) = (((page) | pgprot_val(prot) | _PAGE_E) &
			~(unsigned long)_PAGE_CACHE);
	pte_val(pte) |= (((unsigned long)space) << 32);
	return pte;
}

/* Remap IO memory, the same way as remap_pfn_range(), but use
 * the obio memory space.
 *
 * They use a pgprot that sets PAGE_IO and does not check the
 * mem_map table as this is independent of normal memory.
 */
static inline void io_remap_pte_range(struct mm_struct *mm, pte_t * pte,
				      unsigned long address,
				      unsigned long size,
				      unsigned long offset, pgprot_t prot,
				      int space)
{
	unsigned long end;

	/* clear hack bit that was used as a write_combine side-effect flag */
	offset &= ~0x1UL;
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		pte_t entry;
		unsigned long curend = address + PAGE_SIZE;
		
		entry = mk_pte_io(offset, prot, space);
		if (!(address & 0xffff)) {
			if (!(address & 0x3fffff) && !(offset & 0x3ffffe) && end >= address + 0x400000) {
				entry = mk_pte_io(offset,
						  __pgprot(pgprot_val (prot) | _PAGE_SZ4MB),
						  space);
				curend = address + 0x400000;
				offset += 0x400000;
			} else if (!(address & 0x7ffff) && !(offset & 0x7fffe) && end >= address + 0x80000) {
				entry = mk_pte_io(offset,
						  __pgprot(pgprot_val (prot) | _PAGE_SZ512K),
						  space);
				curend = address + 0x80000;
				offset += 0x80000;
			} else if (!(offset & 0xfffe) && end >= address + 0x10000) {
				entry = mk_pte_io(offset,
						  __pgprot(pgprot_val (prot) | _PAGE_SZ64K),
						  space);
				curend = address + 0x10000;
				offset += 0x10000;
			} else
				offset += PAGE_SIZE;
		} else
			offset += PAGE_SIZE;

		do {
			BUG_ON(!pte_none(*pte));
			set_pte_at(mm, address, pte, entry);
			address += PAGE_SIZE;
			pte_val(entry) += PAGE_SIZE;
			pte++;
		} while (address < curend);
	} while (address < end);
}

static inline int io_remap_pmd_range(struct mm_struct *mm, pmd_t * pmd, unsigned long address, unsigned long size,
	unsigned long offset, pgprot_t prot, int space)
{
	unsigned long end;

	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	offset -= address;
	do {
		pte_t * pte = pte_alloc_map(mm, pmd, address);
		if (!pte)
			return -ENOMEM;
		io_remap_pte_range(mm, pte, address, end - address, address + offset, prot, space);
		pte_unmap(pte);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
	return 0;
}

static inline int io_remap_pud_range(struct mm_struct *mm, pud_t * pud, unsigned long address, unsigned long size,
	unsigned long offset, pgprot_t prot, int space)
{
	unsigned long end;

	address &= ~PUD_MASK;
	end = address + size;
	if (end > PUD_SIZE)
		end = PUD_SIZE;
	offset -= address;
	do {
		pmd_t *pmd = pmd_alloc(mm, pud, address);
		if (!pud)
			return -ENOMEM;
		io_remap_pmd_range(mm, pmd, address, end - address, address + offset, prot, space);
		address = (address + PUD_SIZE) & PUD_MASK;
		pud++;
	} while (address < end);
	return 0;
}

int io_remap_pfn_range(struct vm_area_struct *vma, unsigned long from,
		unsigned long pfn, unsigned long size, pgprot_t prot)
{
	int error = 0;
	pgd_t * dir;
	unsigned long beg = from;
	unsigned long end = from + size;
	struct mm_struct *mm = vma->vm_mm;
	int space = GET_IOSPACE(pfn);
	unsigned long offset = GET_PFN(pfn) << PAGE_SHIFT;
	unsigned long phys_base;

	phys_base = offset | (((unsigned long) space) << 32UL);

	/* See comment in mm/memory.c remap_pfn_range */
	vma->vm_flags |= VM_IO | VM_RESERVED | VM_PFNMAP;
	vma->vm_pgoff = phys_base >> PAGE_SHIFT;

	prot = __pgprot(pg_iobits);
	offset -= from;
	dir = pgd_offset(mm, from);
	flush_cache_range(vma, beg, end);

	while (from < end) {
		pud_t *pud = pud_alloc(mm, dir, from);
		error = -ENOMEM;
		if (!pud)
			break;
		error = io_remap_pud_range(mm, pud, from, end - from, offset + from, prot, space);
		if (error)
			break;
		from = (from + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	}

	flush_tlb_range(vma, beg, end);
	return error;
}
