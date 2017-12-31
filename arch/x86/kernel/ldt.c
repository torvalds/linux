// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 1992 Krishna Balasubramanian and Linus Torvalds
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 * Copyright (C) 2002 Andi Kleen
 *
 * This handles calls from both 32bit and 64bit mode.
 *
 * Lock order:
 *	contex.ldt_usr_sem
 *	  mmap_sem
 *	    context.lock
 */

#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>

#include <asm/ldt.h>
#include <asm/tlb.h>
#include <asm/desc.h>
#include <asm/mmu_context.h>
#include <asm/syscalls.h>

static void refresh_ldt_segments(void)
{
#ifdef CONFIG_X86_64
	unsigned short sel;

	/*
	 * Make sure that the cached DS and ES descriptors match the updated
	 * LDT.
	 */
	savesegment(ds, sel);
	if ((sel & SEGMENT_TI_MASK) == SEGMENT_LDT)
		loadsegment(ds, sel);

	savesegment(es, sel);
	if ((sel & SEGMENT_TI_MASK) == SEGMENT_LDT)
		loadsegment(es, sel);
#endif
}

/* context.lock is held by the task which issued the smp function call */
static void flush_ldt(void *__mm)
{
	struct mm_struct *mm = __mm;

	if (this_cpu_read(cpu_tlbstate.loaded_mm) != mm)
		return;

	load_mm_ldt(mm);

	refresh_ldt_segments();
}

/* The caller must call finalize_ldt_struct on the result. LDT starts zeroed. */
static struct ldt_struct *alloc_ldt_struct(unsigned int num_entries)
{
	struct ldt_struct *new_ldt;
	unsigned int alloc_size;

	if (num_entries > LDT_ENTRIES)
		return NULL;

	new_ldt = kmalloc(sizeof(struct ldt_struct), GFP_KERNEL);
	if (!new_ldt)
		return NULL;

	BUILD_BUG_ON(LDT_ENTRY_SIZE != sizeof(struct desc_struct));
	alloc_size = num_entries * LDT_ENTRY_SIZE;

	/*
	 * Xen is very picky: it requires a page-aligned LDT that has no
	 * trailing nonzero bytes in any page that contains LDT descriptors.
	 * Keep it simple: zero the whole allocation and never allocate less
	 * than PAGE_SIZE.
	 */
	if (alloc_size > PAGE_SIZE)
		new_ldt->entries = vzalloc(alloc_size);
	else
		new_ldt->entries = (void *)get_zeroed_page(GFP_KERNEL);

	if (!new_ldt->entries) {
		kfree(new_ldt);
		return NULL;
	}

	/* The new LDT isn't aliased for PTI yet. */
	new_ldt->slot = -1;

	new_ldt->nr_entries = num_entries;
	return new_ldt;
}

/*
 * If PTI is enabled, this maps the LDT into the kernelmode and
 * usermode tables for the given mm.
 *
 * There is no corresponding unmap function.  Even if the LDT is freed, we
 * leave the PTEs around until the slot is reused or the mm is destroyed.
 * This is harmless: the LDT is always in ordinary memory, and no one will
 * access the freed slot.
 *
 * If we wanted to unmap freed LDTs, we'd also need to do a flush to make
 * it useful, and the flush would slow down modify_ldt().
 */
static int
map_ldt_struct(struct mm_struct *mm, struct ldt_struct *ldt, int slot)
{
#ifdef CONFIG_PAGE_TABLE_ISOLATION
	bool is_vmalloc, had_top_level_entry;
	unsigned long va;
	spinlock_t *ptl;
	pgd_t *pgd;
	int i;

	if (!static_cpu_has(X86_FEATURE_PTI))
		return 0;

	/*
	 * Any given ldt_struct should have map_ldt_struct() called at most
	 * once.
	 */
	WARN_ON(ldt->slot != -1);

	/*
	 * Did we already have the top level entry allocated?  We can't
	 * use pgd_none() for this because it doens't do anything on
	 * 4-level page table kernels.
	 */
	pgd = pgd_offset(mm, LDT_BASE_ADDR);
	had_top_level_entry = (pgd->pgd != 0);

	is_vmalloc = is_vmalloc_addr(ldt->entries);

	for (i = 0; i * PAGE_SIZE < ldt->nr_entries * LDT_ENTRY_SIZE; i++) {
		unsigned long offset = i << PAGE_SHIFT;
		const void *src = (char *)ldt->entries + offset;
		unsigned long pfn;
		pte_t pte, *ptep;

		va = (unsigned long)ldt_slot_va(slot) + offset;
		pfn = is_vmalloc ? vmalloc_to_pfn(src) :
			page_to_pfn(virt_to_page(src));
		/*
		 * Treat the PTI LDT range as a *userspace* range.
		 * get_locked_pte() will allocate all needed pagetables
		 * and account for them in this mm.
		 */
		ptep = get_locked_pte(mm, va, &ptl);
		if (!ptep)
			return -ENOMEM;
		/*
		 * Map it RO so the easy to find address is not a primary
		 * target via some kernel interface which misses a
		 * permission check.
		 */
		pte = pfn_pte(pfn, __pgprot(__PAGE_KERNEL_RO & ~_PAGE_GLOBAL));
		set_pte_at(mm, va, ptep, pte);
		pte_unmap_unlock(ptep, ptl);
	}

	if (mm->context.ldt) {
		/*
		 * We already had an LDT.  The top-level entry should already
		 * have been allocated and synchronized with the usermode
		 * tables.
		 */
		WARN_ON(!had_top_level_entry);
		if (static_cpu_has(X86_FEATURE_PTI))
			WARN_ON(!kernel_to_user_pgdp(pgd)->pgd);
	} else {
		/*
		 * This is the first time we're mapping an LDT for this process.
		 * Sync the pgd to the usermode tables.
		 */
		WARN_ON(had_top_level_entry);
		if (static_cpu_has(X86_FEATURE_PTI)) {
			WARN_ON(kernel_to_user_pgdp(pgd)->pgd);
			set_pgd(kernel_to_user_pgdp(pgd), *pgd);
		}
	}

	va = (unsigned long)ldt_slot_va(slot);
	flush_tlb_mm_range(mm, va, va + LDT_SLOT_STRIDE, 0);

	ldt->slot = slot;
#endif
	return 0;
}

static void free_ldt_pgtables(struct mm_struct *mm)
{
#ifdef CONFIG_PAGE_TABLE_ISOLATION
	struct mmu_gather tlb;
	unsigned long start = LDT_BASE_ADDR;
	unsigned long end = start + (1UL << PGDIR_SHIFT);

	if (!static_cpu_has(X86_FEATURE_PTI))
		return;

	tlb_gather_mmu(&tlb, mm, start, end);
	free_pgd_range(&tlb, start, end, start, end);
	tlb_finish_mmu(&tlb, start, end);
#endif
}

/* After calling this, the LDT is immutable. */
static void finalize_ldt_struct(struct ldt_struct *ldt)
{
	paravirt_alloc_ldt(ldt->entries, ldt->nr_entries);
}

static void install_ldt(struct mm_struct *mm, struct ldt_struct *ldt)
{
	mutex_lock(&mm->context.lock);

	/* Synchronizes with READ_ONCE in load_mm_ldt. */
	smp_store_release(&mm->context.ldt, ldt);

	/* Activate the LDT for all CPUs using currents mm. */
	on_each_cpu_mask(mm_cpumask(mm), flush_ldt, mm, true);

	mutex_unlock(&mm->context.lock);
}

static void free_ldt_struct(struct ldt_struct *ldt)
{
	if (likely(!ldt))
		return;

	paravirt_free_ldt(ldt->entries, ldt->nr_entries);
	if (ldt->nr_entries * LDT_ENTRY_SIZE > PAGE_SIZE)
		vfree_atomic(ldt->entries);
	else
		free_page((unsigned long)ldt->entries);
	kfree(ldt);
}

/*
 * Called on fork from arch_dup_mmap(). Just copy the current LDT state,
 * the new task is not running, so nothing can be installed.
 */
int ldt_dup_context(struct mm_struct *old_mm, struct mm_struct *mm)
{
	struct ldt_struct *new_ldt;
	int retval = 0;

	if (!old_mm)
		return 0;

	mutex_lock(&old_mm->context.lock);
	if (!old_mm->context.ldt)
		goto out_unlock;

	new_ldt = alloc_ldt_struct(old_mm->context.ldt->nr_entries);
	if (!new_ldt) {
		retval = -ENOMEM;
		goto out_unlock;
	}

	memcpy(new_ldt->entries, old_mm->context.ldt->entries,
	       new_ldt->nr_entries * LDT_ENTRY_SIZE);
	finalize_ldt_struct(new_ldt);

	retval = map_ldt_struct(mm, new_ldt, 0);
	if (retval) {
		free_ldt_pgtables(mm);
		free_ldt_struct(new_ldt);
		goto out_unlock;
	}
	mm->context.ldt = new_ldt;

out_unlock:
	mutex_unlock(&old_mm->context.lock);
	return retval;
}

/*
 * No need to lock the MM as we are the last user
 *
 * 64bit: Don't touch the LDT register - we're already in the next thread.
 */
void destroy_context_ldt(struct mm_struct *mm)
{
	free_ldt_struct(mm->context.ldt);
	mm->context.ldt = NULL;
}

void ldt_arch_exit_mmap(struct mm_struct *mm)
{
	free_ldt_pgtables(mm);
}

static int read_ldt(void __user *ptr, unsigned long bytecount)
{
	struct mm_struct *mm = current->mm;
	unsigned long entries_size;
	int retval;

	down_read(&mm->context.ldt_usr_sem);

	if (!mm->context.ldt) {
		retval = 0;
		goto out_unlock;
	}

	if (bytecount > LDT_ENTRY_SIZE * LDT_ENTRIES)
		bytecount = LDT_ENTRY_SIZE * LDT_ENTRIES;

	entries_size = mm->context.ldt->nr_entries * LDT_ENTRY_SIZE;
	if (entries_size > bytecount)
		entries_size = bytecount;

	if (copy_to_user(ptr, mm->context.ldt->entries, entries_size)) {
		retval = -EFAULT;
		goto out_unlock;
	}

	if (entries_size != bytecount) {
		/* Zero-fill the rest and pretend we read bytecount bytes. */
		if (clear_user(ptr + entries_size, bytecount - entries_size)) {
			retval = -EFAULT;
			goto out_unlock;
		}
	}
	retval = bytecount;

out_unlock:
	up_read(&mm->context.ldt_usr_sem);
	return retval;
}

static int read_default_ldt(void __user *ptr, unsigned long bytecount)
{
	/* CHECKME: Can we use _one_ random number ? */
#ifdef CONFIG_X86_32
	unsigned long size = 5 * sizeof(struct desc_struct);
#else
	unsigned long size = 128;
#endif
	if (bytecount > size)
		bytecount = size;
	if (clear_user(ptr, bytecount))
		return -EFAULT;
	return bytecount;
}

static int write_ldt(void __user *ptr, unsigned long bytecount, int oldmode)
{
	struct mm_struct *mm = current->mm;
	struct ldt_struct *new_ldt, *old_ldt;
	unsigned int old_nr_entries, new_nr_entries;
	struct user_desc ldt_info;
	struct desc_struct ldt;
	int error;

	error = -EINVAL;
	if (bytecount != sizeof(ldt_info))
		goto out;
	error = -EFAULT;
	if (copy_from_user(&ldt_info, ptr, sizeof(ldt_info)))
		goto out;

	error = -EINVAL;
	if (ldt_info.entry_number >= LDT_ENTRIES)
		goto out;
	if (ldt_info.contents == 3) {
		if (oldmode)
			goto out;
		if (ldt_info.seg_not_present == 0)
			goto out;
	}

	if ((oldmode && !ldt_info.base_addr && !ldt_info.limit) ||
	    LDT_empty(&ldt_info)) {
		/* The user wants to clear the entry. */
		memset(&ldt, 0, sizeof(ldt));
	} else {
		if (!IS_ENABLED(CONFIG_X86_16BIT) && !ldt_info.seg_32bit) {
			error = -EINVAL;
			goto out;
		}

		fill_ldt(&ldt, &ldt_info);
		if (oldmode)
			ldt.avl = 0;
	}

	if (down_write_killable(&mm->context.ldt_usr_sem))
		return -EINTR;

	old_ldt       = mm->context.ldt;
	old_nr_entries = old_ldt ? old_ldt->nr_entries : 0;
	new_nr_entries = max(ldt_info.entry_number + 1, old_nr_entries);

	error = -ENOMEM;
	new_ldt = alloc_ldt_struct(new_nr_entries);
	if (!new_ldt)
		goto out_unlock;

	if (old_ldt)
		memcpy(new_ldt->entries, old_ldt->entries, old_nr_entries * LDT_ENTRY_SIZE);

	new_ldt->entries[ldt_info.entry_number] = ldt;
	finalize_ldt_struct(new_ldt);

	/*
	 * If we are using PTI, map the new LDT into the userspace pagetables.
	 * If there is already an LDT, use the other slot so that other CPUs
	 * will continue to use the old LDT until install_ldt() switches
	 * them over to the new LDT.
	 */
	error = map_ldt_struct(mm, new_ldt, old_ldt ? !old_ldt->slot : 0);
	if (error) {
		/*
		 * This only can fail for the first LDT setup. If an LDT is
		 * already installed then the PTE page is already
		 * populated. Mop up a half populated page table.
		 */
		free_ldt_pgtables(mm);
		free_ldt_struct(new_ldt);
		goto out_unlock;
	}

	install_ldt(mm, new_ldt);
	free_ldt_struct(old_ldt);
	error = 0;

out_unlock:
	up_write(&mm->context.ldt_usr_sem);
out:
	return error;
}

SYSCALL_DEFINE3(modify_ldt, int , func , void __user * , ptr ,
		unsigned long , bytecount)
{
	int ret = -ENOSYS;

	switch (func) {
	case 0:
		ret = read_ldt(ptr, bytecount);
		break;
	case 1:
		ret = write_ldt(ptr, bytecount, 1);
		break;
	case 2:
		ret = read_default_ldt(ptr, bytecount);
		break;
	case 0x11:
		ret = write_ldt(ptr, bytecount, 0);
		break;
	}
	/*
	 * The SYSCALL_DEFINE() macros give us an 'unsigned long'
	 * return type, but tht ABI for sys_modify_ldt() expects
	 * 'int'.  This cast gives us an int-sized value in %rax
	 * for the return code.  The 'unsigned' is necessary so
	 * the compiler does not try to sign-extend the negative
	 * return codes into the high half of the register when
	 * taking the value from int->long.
	 */
	return (unsigned int)ret;
}
