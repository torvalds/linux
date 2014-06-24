/*
 * arch/arm64/mm/hugetlbpage.c
 *
 * Copyright (C) 2013 Linaro Ltd.
 *
 * Based on arch/x86/mm/hugetlbpage.c.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/err.h>
#include <linux/sysctl.h>
#include <asm/mman.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/pgalloc.h>

#ifndef CONFIG_ARCH_WANT_HUGE_PMD_SHARE
int huge_pmd_unshare(struct mm_struct *mm, unsigned long *addr, pte_t *ptep)
{
	return 0;
}
#endif

struct page *follow_huge_addr(struct mm_struct *mm, unsigned long address,
			      int write)
{
	return ERR_PTR(-EINVAL);
}

int pmd_huge(pmd_t pmd)
{
	return !(pmd_val(pmd) & PMD_TABLE_BIT);
}

int pud_huge(pud_t pud)
{
#ifndef __PAGETABLE_PMD_FOLDED
	return !(pud_val(pud) & PUD_TABLE_BIT);
#else
	return 0;
#endif
}

int pmd_huge_support(void)
{
	return 1;
}

static __init int setup_hugepagesz(char *opt)
{
	unsigned long ps = memparse(opt, &opt);
	if (ps == PMD_SIZE) {
		hugetlb_add_hstate(PMD_SHIFT - PAGE_SHIFT);
	} else if (ps == PUD_SIZE) {
		hugetlb_add_hstate(PUD_SHIFT - PAGE_SHIFT);
	} else {
		pr_err("hugepagesz: Unsupported page size %lu M\n", ps >> 20);
		return 0;
	}
	return 1;
}
__setup("hugepagesz=", setup_hugepagesz);
