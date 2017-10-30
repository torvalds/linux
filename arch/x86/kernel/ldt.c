/*
 * Copyright (C) 1992 Krishna Balasubramanian and Linus Torvalds
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 * Copyright (C) 2002 Andi Kleen
 *
 * This handles calls from both 32bit and 64bit mode.
 */

#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>

#include <asm/ldt.h>
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

/* context.lock is held for us, so we don't need any locking. */
static void flush_ldt(void *__mm)
{
	struct mm_struct *mm = __mm;
	mm_context_t *pc;

	if (this_cpu_read(cpu_tlbstate.loaded_mm) != mm)
		return;

	pc = &mm->context;
	set_ldt(pc->ldt->entries, pc->ldt->nr_entries);

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

	new_ldt->nr_entries = num_entries;
	return new_ldt;
}

/* After calling this, the LDT is immutable. */
static void finalize_ldt_struct(struct ldt_struct *ldt)
{
	paravirt_alloc_ldt(ldt->entries, ldt->nr_entries);
}

/* context.lock is held */
static void install_ldt(struct mm_struct *current_mm,
			struct ldt_struct *ldt)
{
	/* Synchronizes with lockless_dereference in load_mm_ldt. */
	smp_store_release(&current_mm->context.ldt, ldt);

	/* Activate the LDT for all CPUs using current_mm. */
	on_each_cpu_mask(mm_cpumask(current_mm), flush_ldt, current_mm, true);
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
 * we do not have to muck with descriptors here, that is
 * done in switch_mm() as needed.
 */
int init_new_context_ldt(struct task_struct *tsk, struct mm_struct *mm)
{
	struct ldt_struct *new_ldt;
	struct mm_struct *old_mm;
	int retval = 0;

	mutex_init(&mm->context.lock);
	old_mm = current->mm;
	if (!old_mm) {
		mm->context.ldt = NULL;
		return 0;
	}

	mutex_lock(&old_mm->context.lock);
	if (!old_mm->context.ldt) {
		mm->context.ldt = NULL;
		goto out_unlock;
	}

	new_ldt = alloc_ldt_struct(old_mm->context.ldt->nr_entries);
	if (!new_ldt) {
		retval = -ENOMEM;
		goto out_unlock;
	}

	memcpy(new_ldt->entries, old_mm->context.ldt->entries,
	       new_ldt->nr_entries * LDT_ENTRY_SIZE);
	finalize_ldt_struct(new_ldt);

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

static int read_ldt(void __user *ptr, unsigned long bytecount)
{
	struct mm_struct *mm = current->mm;
	unsigned long entries_size;
	int retval;

	mutex_lock(&mm->context.lock);

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
	mutex_unlock(&mm->context.lock);
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

	mutex_lock(&mm->context.lock);

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

	install_ldt(mm, new_ldt);
	free_ldt_struct(old_ldt);
	error = 0;

out_unlock:
	mutex_unlock(&mm->context.lock);
out:
	return error;
}

asmlinkage int sys_modify_ldt(int func, void __user *ptr,
			      unsigned long bytecount)
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
	return ret;
}
