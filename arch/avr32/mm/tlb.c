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

#define _TLBEHI_I	0x100

void show_dtlb_entry(unsigned int index)
{
	unsigned int tlbehi, tlbehi_save, tlbelo, mmucr, mmucr_save;
	unsigned long flags;

	local_irq_save(flags);
	mmucr_save = sysreg_read(MMUCR);
	tlbehi_save = sysreg_read(TLBEHI);
	mmucr = mmucr_save & 0x13;
	mmucr |= index << 14;
	sysreg_write(MMUCR, mmucr);

	asm volatile("tlbr" : : : "memory");
	cpu_sync_pipeline();

	tlbehi = sysreg_read(TLBEHI);
	tlbelo = sysreg_read(TLBELO);

	printk("%2u: %c %c %02x   %05x %05x %o  %o  %c %c %c %c\n",
	       index,
	       (tlbehi & 0x200)?'1':'0',
	       (tlbelo & 0x100)?'1':'0',
	       (tlbehi & 0xff),
	       (tlbehi >> 12), (tlbelo >> 12),
	       (tlbelo >> 4) & 7, (tlbelo >> 2) & 3,
	       (tlbelo & 0x200)?'1':'0',
	       (tlbelo & 0x080)?'1':'0',
	       (tlbelo & 0x001)?'1':'0',
	       (tlbelo & 0x002)?'1':'0');

	sysreg_write(MMUCR, mmucr_save);
	sysreg_write(TLBEHI, tlbehi_save);
	cpu_sync_pipeline();
	local_irq_restore(flags);
}

void dump_dtlb(void)
{
	unsigned int i;

	printk("ID  V G ASID VPN   PFN   AP SZ C B W D\n");
	for (i = 0; i < 32; i++)
		show_dtlb_entry(i);
}

static unsigned long last_mmucr;

static inline void set_replacement_pointer(unsigned shift)
{
	unsigned long mmucr, mmucr_save;

	mmucr = mmucr_save = sysreg_read(MMUCR);

	/* Does this mapping already exist? */
	__asm__ __volatile__(
		"	tlbs\n"
		"	mfsr %0, %1"
		: "=r"(mmucr)
		: "i"(SYSREG_MMUCR));

	if (mmucr & SYSREG_BIT(MMUCR_N)) {
		/* Not found -- pick a not-recently-accessed entry */
		unsigned long rp;
		unsigned long tlbar = sysreg_read(TLBARLO);

		rp = 32 - fls(tlbar);
		if (rp == 32) {
			rp = 0;
			sysreg_write(TLBARLO, -1L);
		}

		mmucr &= 0x13;
		mmucr |= (rp << shift);

		sysreg_write(MMUCR, mmucr);
	}

	last_mmucr = mmucr;
}

static void update_dtlb(unsigned long address, pte_t pte, unsigned long asid)
{
	unsigned long vpn;

	vpn = (address & MMU_VPN_MASK) | _TLBEHI_VALID | asid;
	sysreg_write(TLBEHI, vpn);
	cpu_sync_pipeline();

	set_replacement_pointer(14);

	sysreg_write(TLBELO, pte_val(pte) & _PAGE_FLAGS_HARDWARE_MASK);

	/* Let's go */
	asm volatile("nop\n\ttlbw" : : : "memory");
	cpu_sync_pipeline();
}

void update_mmu_cache(struct vm_area_struct *vma,
		      unsigned long address, pte_t pte)
{
	unsigned long flags;

	/* ptrace may call this routine */
	if (vma && current->active_mm != vma->vm_mm)
		return;

	local_irq_save(flags);
	update_dtlb(address, pte, get_asid());
	local_irq_restore(flags);
}

void __flush_tlb_page(unsigned long asid, unsigned long page)
{
	unsigned long mmucr, tlbehi;

	page |= asid;
	sysreg_write(TLBEHI, page);
	cpu_sync_pipeline();
	asm volatile("tlbs");
	mmucr = sysreg_read(MMUCR);

	if (!(mmucr & SYSREG_BIT(MMUCR_N))) {
		unsigned long tlbarlo;
		unsigned long entry;

		/* Clear the "valid" bit */
		tlbehi = sysreg_read(TLBEHI);
		tlbehi &= ~_TLBEHI_VALID;
		sysreg_write(TLBEHI, tlbehi);
		cpu_sync_pipeline();

		/* mark the entry as "not accessed" */
		entry = (mmucr >> 14) & 0x3f;
		tlbarlo = sysreg_read(TLBARLO);
		tlbarlo |= (0x80000000 >> entry);
		sysreg_write(TLBARLO, tlbarlo);

		/* update the entry with valid bit clear */
		asm volatile("tlbw");
		cpu_sync_pipeline();
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
			unsigned long asid = mm->context & MMU_CONTEXT_ASID_MASK;
			unsigned long saved_asid = MMU_NO_ASID;

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
 * TODO: If this is only called for addresses > TASK_SIZE, we can probably
 * skip the ASID stuff and just use the Global bit...
 */
void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	unsigned long flags;
	int size;

	local_irq_save(flags);
	size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
	if (size > (MMU_DTLB_ENTRIES / 4)) { /* Too many entries to flush */
		flush_tlb_all();
	} else {
		unsigned long asid = init_mm.context & MMU_CONTEXT_ASID_MASK;
		unsigned long saved_asid = get_asid();

		start &= PAGE_MASK;
		end += (PAGE_SIZE - 1);
		end &= PAGE_MASK;
		set_asid(asid);
		while (start < end) {
			__flush_tlb_page(asid, start);
			start += PAGE_SIZE;
		}
		set_asid(saved_asid);
	}
	local_irq_restore(flags);
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

	if (*pos >= 32)
		return NULL;

	tlb_index = 0;
	return &tlb_index;
}

static void *tlb_next(struct seq_file *tlb, void *v, loff_t *pos)
{
	unsigned long *index = v;

	if (*index >= 31)
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

	BUG_ON(*index >= 32);

	local_irq_save(flags);
	mmucr_save = sysreg_read(MMUCR);
	tlbehi_save = sysreg_read(TLBEHI);
	mmucr = mmucr_save & 0x13;
	mmucr |= *index << 14;
	sysreg_write(MMUCR, mmucr);

	asm volatile("tlbr" : : : "memory");
	cpu_sync_pipeline();

	tlbehi = sysreg_read(TLBEHI);
	tlbelo = sysreg_read(TLBELO);

	sysreg_write(MMUCR, mmucr_save);
	sysreg_write(TLBEHI, tlbehi_save);
	cpu_sync_pipeline();
	local_irq_restore(flags);

	seq_printf(tlb, "%2lu: %c %c %02x   %05x %05x %o  %o  %c %c %c %c\n",
	       *index,
	       (tlbehi & 0x200)?'1':'0',
	       (tlbelo & 0x100)?'1':'0',
	       (tlbehi & 0xff),
	       (tlbehi >> 12), (tlbelo >> 12),
	       (tlbelo >> 4) & 7, (tlbelo >> 2) & 3,
	       (tlbelo & 0x200)?'1':'0',
	       (tlbelo & 0x080)?'1':'0',
	       (tlbelo & 0x001)?'1':'0',
	       (tlbelo & 0x002)?'1':'0');

	return 0;
}

static struct seq_operations tlb_ops = {
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
	struct proc_dir_entry *entry;

	entry = create_proc_entry("tlb", 0, NULL);
	if (entry)
		entry->proc_fops = &proc_tlb_operations;
	return 0;
}
late_initcall(proctlb_init);
#endif /* CONFIG_PROC_FS */
