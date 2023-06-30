// SPDX-License-Identifier: GPL-2.0
/*
 * arch/sh/mm/hugetlbpage.c
 *
 * SuperH HugeTLB page support.
 *
 * Cloned from sparc64 by Paul Mundt.
 *
 * Copyright (C) 2002, 2003 David S. Miller (davem@redhat.com)
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/sysctl.h>

#include <asm/mman.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>

pte_t *huge_pte_alloc(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long addr, unsigned long sz)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, addr);
	if (pgd) {
		p4d = p4d_alloc(mm, pgd, addr);
		if (p4d) {
			pud = pud_alloc(mm, p4d, addr);
			if (pud) {
				pmd = pmd_alloc(mm, pud, addr);
				if (pmd)
					pte = pte_alloc_huge(mm, pmd, addr);
			}
		}
	}

	return pte;
}

pte_t *huge_pte_offset(struct mm_struct *mm,
		       unsigned long addr, unsigned long sz)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, addr);
	if (pgd) {
		p4d = p4d_offset(pgd, addr);
		if (p4d) {
			pud = pud_offset(p4d, addr);
			if (pud) {
				pmd = pmd_offset(pud, addr);
				if (pmd)
					pte = pte_offset_huge(pmd, addr);
			}
		}
	}

	return pte;
}

int pmd_huge(pmd_t pmd)
{
	return 0;
}

int pud_huge(pud_t pud)
{
	return 0;
}
