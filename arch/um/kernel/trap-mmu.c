// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/hardirq.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/sched/debug.h>
#include <asm/current.h>
#include <asm/tlbflush.h>
#include <arch.h>
#include <as-layout.h>
#include <kern_util.h>
#include <os.h>
#include <skas.h>

/*
 * NOTE: UML does not have exception tables. As such, this is almost a copy
 * of the code in mm/memory.c, only adjusting the logic to simply check whether
 * we are coming from the kernel instead of doing an additional lookup in the
 * exception table.
 * We can do this simplification because we never get here if the exception was
 * fixable.
 */
static inline bool get_mmap_lock_carefully(struct mm_struct *mm, bool is_user)
{
	if (likely(mmap_read_trylock(mm)))
		return true;

	if (!is_user)
		return false;

	return !mmap_read_lock_killable(mm);
}

static inline bool mmap_upgrade_trylock(struct mm_struct *mm)
{
	/*
	 * We don't have this operation yet.
	 *
	 * It should be easy enough to do: it's basically a
	 *    atomic_long_try_cmpxchg_acquire()
	 * from RWSEM_READER_BIAS -> RWSEM_WRITER_LOCKED, but
	 * it also needs the proper lockdep magic etc.
	 */
	return false;
}

static inline bool upgrade_mmap_lock_carefully(struct mm_struct *mm, bool is_user)
{
	mmap_read_unlock(mm);
	if (!is_user)
		return false;

	return !mmap_write_lock_killable(mm);
}

/*
 * Helper for page fault handling.
 *
 * This is kind of equivalend to "mmap_read_lock()" followed
 * by "find_extend_vma()", except it's a lot more careful about
 * the locking (and will drop the lock on failure).
 *
 * For example, if we have a kernel bug that causes a page
 * fault, we don't want to just use mmap_read_lock() to get
 * the mm lock, because that would deadlock if the bug were
 * to happen while we're holding the mm lock for writing.
 *
 * So this checks the exception tables on kernel faults in
 * order to only do this all for instructions that are actually
 * expected to fault.
 *
 * We can also actually take the mm lock for writing if we
 * need to extend the vma, which helps the VM layer a lot.
 */
static struct vm_area_struct *
um_lock_mm_and_find_vma(struct mm_struct *mm,
			unsigned long addr, bool is_user)
{
	struct vm_area_struct *vma;

	if (!get_mmap_lock_carefully(mm, is_user))
		return NULL;

	vma = find_vma(mm, addr);
	if (likely(vma && (vma->vm_start <= addr)))
		return vma;

	/*
	 * Well, dang. We might still be successful, but only
	 * if we can extend a vma to do so.
	 */
	if (!vma || !(vma->vm_flags & VM_GROWSDOWN)) {
		mmap_read_unlock(mm);
		return NULL;
	}

	/*
	 * We can try to upgrade the mmap lock atomically,
	 * in which case we can continue to use the vma
	 * we already looked up.
	 *
	 * Otherwise we'll have to drop the mmap lock and
	 * re-take it, and also look up the vma again,
	 * re-checking it.
	 */
	if (!mmap_upgrade_trylock(mm)) {
		if (!upgrade_mmap_lock_carefully(mm, is_user))
			return NULL;

		vma = find_vma(mm, addr);
		if (!vma)
			goto fail;
		if (vma->vm_start <= addr)
			goto success;
		if (!(vma->vm_flags & VM_GROWSDOWN))
			goto fail;
	}

	if (expand_stack_locked(vma, addr))
		goto fail;

success:
	mmap_write_downgrade(mm);
	return vma;

fail:
	mmap_write_unlock(mm);
	return NULL;
}

/*
 * Note this is constrained to return 0, -EFAULT, -EACCES, -ENOMEM by
 * segv().
 */
int handle_page_fault(unsigned long address, unsigned long ip,
		      int is_write, int is_user, int *code_out)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	pmd_t *pmd;
	pte_t *pte;
	int err = -EFAULT;
	unsigned int flags = FAULT_FLAG_DEFAULT;

	*code_out = SEGV_MAPERR;

	/*
	 * If the fault was with pagefaults disabled, don't take the fault, just
	 * fail.
	 */
	if (faulthandler_disabled())
		goto out_nosemaphore;

	if (is_user)
		flags |= FAULT_FLAG_USER;
retry:
	vma = um_lock_mm_and_find_vma(mm, address, is_user);
	if (!vma)
		goto out_nosemaphore;

	*code_out = SEGV_ACCERR;
	if (is_write) {
		if (!(vma->vm_flags & VM_WRITE))
			goto out;
		flags |= FAULT_FLAG_WRITE;
	} else {
		/* Don't require VM_READ|VM_EXEC for write faults! */
		if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
			goto out;
	}

	do {
		vm_fault_t fault;

		fault = handle_mm_fault(vma, address, flags, NULL);

		if ((fault & VM_FAULT_RETRY) && fatal_signal_pending(current))
			goto out_nosemaphore;

		/* The fault is fully completed (including releasing mmap lock) */
		if (fault & VM_FAULT_COMPLETED)
			return 0;

		if (unlikely(fault & VM_FAULT_ERROR)) {
			if (fault & VM_FAULT_OOM) {
				goto out_of_memory;
			} else if (fault & VM_FAULT_SIGSEGV) {
				goto out;
			} else if (fault & VM_FAULT_SIGBUS) {
				err = -EACCES;
				goto out;
			}
			BUG();
		}
		if (fault & VM_FAULT_RETRY) {
			flags |= FAULT_FLAG_TRIED;

			goto retry;
		}

		pmd = pmd_off(mm, address);
		pte = pte_offset_kernel(pmd, address);
	} while (!pte_present(*pte));
	err = 0;
	/*
	 * The below warning was added in place of
	 *	pte_mkyoung(); if (is_write) pte_mkdirty();
	 * If it's triggered, we'd see normally a hang here (a clean pte is
	 * marked read-only to emulate the dirty bit).
	 * However, the generic code can mark a PTE writable but clean on a
	 * concurrent read fault, triggering this harmlessly. So comment it out.
	 */
#if 0
	WARN_ON(!pte_young(*pte) || (is_write && !pte_dirty(*pte)));
#endif

out:
	mmap_read_unlock(mm);
out_nosemaphore:
	return err;

out_of_memory:
	/*
	 * We ran out of memory, call the OOM killer, and return the userspace
	 * (which will retry the fault, or kill us if we got oom-killed).
	 */
	mmap_read_unlock(mm);
	if (!is_user)
		goto out_nosemaphore;
	pagefault_out_of_memory();
	return 0;
}
