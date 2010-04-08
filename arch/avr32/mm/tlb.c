/*
 * AVR32 TLB operations
 *
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/mm.h>

#include <asm/mmu_context.h>

/* TODO: Get the correct number from the CONFIG1 system register */
#define NR_TLB_ENTRIES 32

static void show_dtlb_entry(unsigned int index)
{
	u32 tlbehi, tlbehi_save, tlbelo, mmucr, mmucr_save;
	unsigned long flags;

	local_irq_save(flags);
	mmucr_save = sysreg_read(MMUCR);
	tlbehi_save = sysreg_read(TLBEHI);
	mmucr = SYSREG_BFINS(DRP, index, mmucr_save);
	sysreg_write(MMUCR, mmucr);

	__builtin_tlbr();
	cpu_sync_pipeline();

	tlbehi = sysreg_read(TLBEHI);
	tlbelo = sysreg_read(TLBELO);

	printk("%2u: %c %c %02x   %05x %05x %o  %o  %c %c %c %c\n",
	       index,
	       SYSREG_BFEXT(TLBEHI_V, tlbehi) ? '1' : '0',
	       SYSREG_BFEXT(G, tlbelo) ? '1' : '0',
	       SYSREG_BFEXT(ASID, tlbehi),
	       SYSREG_BFEXT(VPN, tlbehi) >> 2,
	       SYSREG_BFEXT(PFN, tlbelo) >> 2,
	       SYSREG_BFEXT(AP, tlbelo),
	       SYSREG_BFEXT(SZ, tlbelo),
	       SYSREG_BFEXT(TLBELO_C, tlbelo) ? 'C' : ' ',
	       SYSREG_BFEXT(B, tlbelo) ? 'B' : ' ',
	       SYSREG_BFEXT(W, tlbelo) ? 'W' : ' ',
	       SYSREG_BFEXT(TLBELO_D, tlbelo) ? 'D' : ' ');

	sysreg_write(MMUCR, mmucr_save);
	sysreg_write(TLBEHI, tlbehi_save);
	cpu_sync_pipeline();
	local_irq_restore(flags);
}

void dump_dtlb(void)
{
	unsigned int i;

	printk("ID  V G ASID VPN   PFN   AP SZ C B W D\n");
	for (i = 0; i < NR_TLB_ENTRIES; i++)
		show_dtlb_entry(i);
}

static void update_dtlb(unsigned long address, pte_t pte)
{
	u32 tlbehi;
	u32 mmucr;

	/*
	 * We're not changing the ASID here, so no need to flush the
	 * pipeline.
	 */
	tlbehi = sysreg_read(TLBEHI);
	tlbehi = SYSREG_BF(ASID, SYSREG_BFEXT(ASID, tlbehi));
	tlbehi |= address & MMU_VPN_MASK;
	tlbehi |= SYSREG_BIT(TLBEHI_V);
	sysreg_write(TLBEHI, tlbehi);

	/* Does this mapping already exist? */
	__builtin_tlbs();
	mmucr = sysreg_read(MMUCR);

	if (mmucr & SYSREG_BIT(MMUCR_N)) {
		/* Not found -- pick a not-recently-accessed entry */
		unsigned int rp;
		u32 tlbar = sysreg_read(TLBARLO);

		rp = 32 - fls(tlbar);
		if (rp == 32) {
			rp = 0;
			sysreg_write(TLBARLO, -1L);
		}

		mmucr = SYSREG_BFINS(DRP, rp, mmucr);
		sysreg_write(MMUCR, mmucr);
	}

	sysreg_write(TLBELO, pte_val(pte) & _PAGE_FLAGS_HARDWARE_MASK);

	/* Let's go */
	__builtin_tlbw();
}

void update_mmu_cache(struct vm_area_struct *vma,
		      unsigned long address, pte_t *ptep)
{
	unsigned long flags;

	/* ptrace may call this routine */
	if (vma && current->active_mm != vma->vm_mm)
		return;

	local_irq_save(flags);
	update_dtlb(address, *ptep);
	local_irq_restore(flags);
}

static void __flush_tlb_page(unsigned long asid, unsigned long page)
{
	u32 mmucr, tlbehi;

	/*
	 * Caller is responsible for masking out non-PFN bits in page
	 * and changing the current ASID if necessary. This means that
	 * we don't need to flush the pipeline after writing TLBEHI.
	 */
	tlbehi = page | asid;
	sysreg_write(TLBEHI, tlbehi);

	__builtin_tlbs();
	mmucr = sysreg_read(MMUCR);

	if (!(mmucr & SYSREG_BIT(MMUCR_N))) {
		unsigned int entry;
		u32 tlbarlo;

		/* Clear the "valid" bit */
		sysreg_write(TLBEHI, tlbehi);

		/* mark the entry as "not accessed" */
		entry = SYSREG_BFEXT(DRP, mmucr);
		tlbarlo = sysreg_read(TLBARLO);
		tlbarlo |= (0x80000000UL >> entry);
		sysreg_write(TLBARLO, tlbarlo);

		/* update the entry with valid bit clear */
		__builtin_tlbw();
	}
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	if (vma->vm_mm && vma->vm_mm->context != NO_CONTEXT) {
		unsigned long flags, asid;
		unsigned long saved_asid = MMU_NO_ASID;

		asid = vma->vm_mm->context & MMU_CONTEXT_ASID_MASK;
		page &= PAGE_MASK;

		local_irq_save(flags);
		if (vma->vm_mm != current->mm) {
			saved_asid = get_asid();
			set_asid(asid);
		}

		__flush_tlb_page(asid, page);

		if (saved_asid != MMU_NO_ASID)
			set_asid(saved_asid);
		local_irq_restore(flags);
	}
}

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		     unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;

	if (mm->context != NO_CONTEXT) {
		unsigned long flags;
		int size;

		local_irq_save(flags);
		size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;

		if (size > (MMU_DTLB_ENTRIES / 4)) { /* Too many entries to flush */
			mm->context = NO_CONTEXT;
			if (mm == current->mm)
				activate_context(mm);
		} else {
			unsigned long asid;
			unsigned long saved_asid;

			asid = mm->context & MMU_CONTEXT_ASID_MASK;
			saved_asid = MMU_NO_ASID;

			start &= PAGE_MASK;
			end += (PAGE_SIZE - 1);
			end &= PAGE_MASK;

			if (mm != current->mm) {
				saved_asid = get_asid();
				set_asid(asid);
			}

			while (start < end) {
				__flush_tlb_page(asid, start);
				start += PAGE_SIZE;
			}
			if (saved_asid != MMU_NO_ASID)
				set_asid(saved_asid);
		}
		local_irq_restore(flags);
	}
}

/*
 * This function depends on the pages to be flushed having the G
 * (global) bit set in their pte. This is true for all
 * PAGE_KERNEL(_RO) pages.
 */
void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	unsigned long flags;
	int size;

	size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
	if (size > (MMU_DTLB_ENTRIES / 4)) { /* Too many entries to flush */
		flush_tlb_all();
	} else {
		unsigned long asid;

		local_irq_save(flags);
		asid = get_asid();

		start &= PAGE_MASK;
		end += (PAGE_SIZE - 1);
		end &= PAGE_MASK;

		while (start < end) {
			__flush_tlb_page(asid, start);
			start += PAGE_SIZE;
		}
		local_irq_restore(flags);
	}
}

void flush_tlb_mm(struct mm_struct *mm)
{
	/* Invalidate all TLB entries of this process by getting a new ASID */
	if (mm->context != NO_CONTEXT) {
		unsigned long flags;

		local_irq_save(flags);
		mm->context = NO_CONTEXT;
		if (mm == current->mm)
			activate_context(mm);
		local_irq_restore(flags);
	}
}

void flush_tlb_all(void)
{
	unsigned long flags;

	local_irq_save(flags);
	sysreg_write(MMUCR, sysreg_read(MMUCR) | SYSREG_BIT(MMUCR_I));
	local_irq_restore(flags);
}

#ifdef CONFIG_PROC_FS

#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/init.h>

static void *tlb_start(struct seq_file *tlb, loff_t *pos)
{
	static unsigned long tlb_index;

	if (*pos >= NR_TLB_ENTRIES)
		return NULL;

	tlb_index = 0;
	return &tlb_index;
}

static void *tlb_next(struct seq_file *tlb, void *v, loff_t *pos)
{
	unsigned long *index = v;

	if (*index >= NR_TLB_ENTRIES - 1)
		return NULL;

	++*pos;
	++*index;
	return index;
}

static void tlb_stop(struct seq_file *tlb, void *v)
{

}

static int tlb_show(struct seq_file *tlb, void *v)
{
	unsigned int tlbehi, tlbehi_save, tlbelo, mmucr, mmucr_save;
	unsigned long flags;
	unsigned long *index = v;

	if (*index == 0)
		seq_puts(tlb, "ID  V G ASID VPN   PFN   AP SZ C B W D\n");

	BUG_ON(*index >= NR_TLB_ENTRIES);

	local_irq_save(flags);
	mmucr_save = sysreg_read(MMUCR);
	tlbehi_save = sysreg_read(TLBEHI);
	mmucr = SYSREG_BFINS(DRP, *index, mmucr_save);
	sysreg_write(MMUCR, mmucr);

	/* TLBR might change the ASID */
	__builtin_tlbr();
	cpu_sync_pipeline();

	tlbehi = sysreg_read(TLBEHI);
	tlbelo = sysreg_read(TLBELO);

	sysreg_write(MMUCR, mmucr_save);
	sysreg_write(TLBEHI, tlbehi_save);
	cpu_sync_pipeline();
	local_irq_restore(flags);

	seq_printf(tlb, "%2lu: %c %c %02x   %05x %05x %o  %o  %c %c %c %c\n",
		   *index,
		   SYSREG_BFEXT(TLBEHI_V, tlbehi) ? '1' : '0',
		   SYSREG_BFEXT(G, tlbelo) ? '1' : '0',
		   SYSREG_BFEXT(ASID, tlbehi),
		   SYSREG_BFEXT(VPN, tlbehi) >> 2,
		   SYSREG_BFEXT(PFN, tlbelo) >> 2,
		   SYSREG_BFEXT(AP, tlbelo),
		   SYSREG_BFEXT(SZ, tlbelo),
		   SYSREG_BFEXT(TLBELO_C, tlbelo) ? '1' : '0',
		   SYSREG_BFEXT(B, tlbelo) ? '1' : '0',
		   SYSREG_BFEXT(W, tlbelo) ? '1' : '0',
		   SYSREG_BFEXT(TLBELO_D, tlbelo) ? '1' : '0');

	return 0;
}

static const struct seq_operations tlb_ops = {
	.start		= tlb_start,
	.next		= tlb_next,
	.stop		= tlb_stop,
	.show		= tlb_show,
};

static int tlb_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &tlb_ops);
}

static const struct file_operations proc_tlb_operations = {
	.open		= tlb_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init proctlb_init(void)
{
	proc_create("tlb", 0, NULL, &proc_tlb_operations);
	return 0;
}
late_initcall(proctlb_init);
#endif /* CONFIG_PROC_FS */
