// SPDX-License-Identifier: GPL-2.0
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/nmi.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/spinlock.h>

#include <asm/cpu_entry_area.h>
#include <asm/pgalloc.h>
#include <asm/fixmap.h>
#include <asm/e820/api.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/io.h>
#include <linux/vmalloc.h>

unsigned int __VMALLOC_RESERVE = 128 << 20;

/*
 * Associate a virtual page frame with a given physical page frame 
 * and protection flags for that frame.
 */ 
void set_pte_vaddr(unsigned long vaddr, pte_t pteval)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pgd = swapper_pg_dir + pgd_index(vaddr);
	if (pgd_none(*pgd)) {
		BUG();
		return;
	}
	p4d = p4d_offset(pgd, vaddr);
	if (p4d_none(*p4d)) {
		BUG();
		return;
	}
	pud = pud_offset(p4d, vaddr);
	if (pud_none(*pud)) {
		BUG();
		return;
	}
	pmd = pmd_offset(pud, vaddr);
	if (pmd_none(*pmd)) {
		BUG();
		return;
	}
	pte = pte_offset_kernel(pmd, vaddr);
	if (!pte_none(pteval))
		set_pte_at(&init_mm, vaddr, pte, pteval);
	else
		pte_clear(&init_mm, vaddr, pte);

	/*
	 * It's enough to flush this one mapping.
	 * (PGE mappings get flushed as well)
	 */
	flush_tlb_one_kernel(vaddr);
}

unsigned long __FIXADDR_TOP = 0xfffff000;
EXPORT_SYMBOL(__FIXADDR_TOP);

/*
 * vmalloc=size forces the vmalloc area to be exactly 'size'
 * bytes. This can be used to increase (or decrease) the
 * vmalloc area - the default is 128m.
 */
static int __init parse_vmalloc(char *arg)
{
	if (!arg)
		return -EINVAL;

	/* Add VMALLOC_OFFSET to the parsed value due to vm area guard hole*/
	__VMALLOC_RESERVE = memparse(arg, &arg) + VMALLOC_OFFSET;
	return 0;
}
early_param("vmalloc", parse_vmalloc);

/*
 * reservetop=size reserves a hole at the top of the kernel address space which
 * a hypervisor can load into later.  Needed for dynamically loaded hypervisors,
 * so relocating the fixmap can be done before paging initialization.
 */
static int __init parse_reservetop(char *arg)
{
	unsigned long address;

	if (!arg)
		return -EINVAL;

	address = memparse(arg, &arg);
	reserve_top_address(address);
	early_ioremap_init();
	return 0;
}
early_param("reservetop", parse_reservetop);
