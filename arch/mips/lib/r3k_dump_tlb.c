/*
 * Dump R3000 TLB for debugging purposes.
 *
 * Copyright (C) 1994, 1995 by Waldorf Electronics, written by Ralf Baechle.
 * Copyright (C) 1999 by Silicon Graphics, Inc.
 * Copyright (C) 1999 by Harald Koerfgen
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/string.h>

#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/cpu.h>
#include <asm/mipsregs.h>
#include <asm/page.h>
#include <asm/pgtable.h>

extern int r3k_have_wired_reg;	/* defined in tlb-r3k.c */

void dump_tlb(int first, int last)
{
	int	i;
	unsigned int asid;
	unsigned long entryhi, entrylo0;

	asid = read_c0_entryhi() & 0xfc0;

	for (i = first; i <= last; i++) {
		write_c0_index(i<<8);
		__asm__ __volatile__(
			".set\tnoreorder\n\t"
			"tlbr\n\t"
			"nop\n\t"
			".set\treorder");
		entryhi  = read_c0_entryhi();
		entrylo0 = read_c0_entrylo0();

		/* Unused entries have a virtual address of KSEG0.  */
		if ((entryhi & 0xffffe000) != 0x80000000
		    && (entryhi & 0xfc0) == asid) {
			/*
			 * Only print entries in use
			 */
			printk("Index: %2d ", i);

			printk("va=%08lx asid=%08lx"
			       "  [pa=%06lx n=%d d=%d v=%d g=%d]",
			       (entryhi & 0xffffe000),
			       entryhi & 0xfc0,
			       entrylo0 & PAGE_MASK,
			       (entrylo0 & (1 << 11)) ? 1 : 0,
			       (entrylo0 & (1 << 10)) ? 1 : 0,
			       (entrylo0 & (1 << 9)) ? 1 : 0,
			       (entrylo0 & (1 << 8)) ? 1 : 0);
		}
	}
	printk("\n");

	write_c0_entryhi(asid);
}

void dump_tlb_all(void)
{
	dump_tlb(0, current_cpu_data.tlbsize - 1);
}

void dump_tlb_wired(void)
{
	int wired = r3k_have_wired_reg ? read_c0_wired() : 8;

	printk("Wired: %d", wired);
	dump_tlb(0, wired - 1);
}

void dump_tlb_addr(unsigned long addr)
{
	unsigned long flags, oldpid;
	int index;

	local_irq_save(flags);
	oldpid = read_c0_entryhi() & 0xff;
	write_c0_entryhi((addr & PAGE_MASK) | oldpid);
	tlb_probe();
	index = read_c0_index();
	write_c0_entryhi(oldpid);
	local_irq_restore(flags);

	if (index < 0) {
		printk("No entry for address 0x%08lx in TLB\n", addr);
		return;
	}

	printk("Entry %d maps address 0x%08lx\n", index, addr);
	dump_tlb(index, index);
}

void dump_tlb_nonwired(void)
{
	int wired = r3k_have_wired_reg ? read_c0_wired() : 8;
	dump_tlb(wired, current_cpu_data.tlbsize - 1);
}

void dump_list_process(struct task_struct *t, void *address)
{
	pgd_t	*page_dir, *pgd;
	pud_t	*pud;
	pmd_t	*pmd;
	pte_t	*pte, page;
	unsigned int addr;
	unsigned long val;

	addr = (unsigned int) address;

	printk("Addr                 == %08x\n", addr);
	printk("tasks->mm.pgd        == %08x\n", (unsigned int) t->mm->pgd);

	page_dir = pgd_offset(t->mm, 0);
	printk("page_dir == %08x\n", (unsigned int) page_dir);

	pgd = pgd_offset(t->mm, addr);
	printk("pgd == %08x, ", (unsigned int) pgd);

	pud = pud_offset(pgd, addr);
	printk("pud == %08x, ", (unsigned int) pud);

	pmd = pmd_offset(pud, addr);
	printk("pmd == %08x, ", (unsigned int) pmd);

	pte = pte_offset(pmd, addr);
	printk("pte == %08x, ", (unsigned int) pte);

	page = *pte;
	printk("page == %08x\n", (unsigned int) pte_val(page));

	val = pte_val(page);
	if (val & _PAGE_PRESENT) printk("present ");
	if (val & _PAGE_READ) printk("read ");
	if (val & _PAGE_WRITE) printk("write ");
	if (val & _PAGE_ACCESSED) printk("accessed ");
	if (val & _PAGE_MODIFIED) printk("modified ");
	if (val & _PAGE_GLOBAL) printk("global ");
	if (val & _PAGE_VALID) printk("valid ");
	printk("\n");
}

void dump_list_current(void *address)
{
	dump_list_process(current, address);
}

unsigned int vtop(void *address)
{
	pgd_t	*pgd;
	pud_t	*pud;
	pmd_t	*pmd;
	pte_t	*pte;
	unsigned int addr, paddr;

	addr = (unsigned long) address;
	pgd = pgd_offset(current->mm, addr);
	pud = pud_offset(pgd, addr);
	pmd = pmd_offset(pud, addr);
	pte = pte_offset(pmd, addr);
	paddr = (KSEG1 | (unsigned int) pte_val(*pte)) & PAGE_MASK;
	paddr |= (addr & ~PAGE_MASK);

	return paddr;
}

void dump16(unsigned long *p)
{
	int i;

	for (i = 0; i < 8; i++) {
		printk("*%08lx == %08lx, ", (unsigned long)p, *p);
		p++;
		printk("*%08lx == %08lx\n", (unsigned long)p, *p);
		p++;
	}
}
