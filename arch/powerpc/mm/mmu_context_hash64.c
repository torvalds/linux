/*
 *  MMU context allocation for 64-bit kernels.
 *
 *  Copyright (C) 2004 Anton Blanchard, IBM Corp. <anton@samba.org>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/slab.h>

#include <asm/mmu_context.h>

#ifdef CONFIG_PPC_ICSWX
/*
 * The processor and its L2 cache cause the icswx instruction to
 * generate a COP_REQ transaction on PowerBus. The transaction has
 * no address, and the processor does not perform an MMU access
 * to authenticate the transaction. The command portion of the
 * PowerBus COP_REQ transaction includes the LPAR_ID (LPID) and
 * the coprocessor Process ID (PID), which the coprocessor compares
 * to the authorized LPID and PID held in the coprocessor, to determine
 * if the process is authorized to generate the transaction.
 * The data of the COP_REQ transaction is 128-byte or less and is
 * placed in cacheable memory on a 128-byte cache line boundary.
 *
 * The task to use a coprocessor should use use_cop() to allocate
 * a coprocessor PID before executing icswx instruction. use_cop()
 * also enables the coprocessor context switching. Drop_cop() is
 * used to free the coprocessor PID.
 *
 * Example:
 * Host Fabric Interface (HFI) is a PowerPC network coprocessor.
 * Each HFI have multiple windows. Each HFI window serves as a
 * network device sending to and receiving from HFI network.
 * HFI immediate send function uses icswx instruction. The immediate
 * send function allows small (single cache-line) packets be sent
 * without using the regular HFI send FIFO and doorbell, which are
 * much slower than immediate send.
 *
 * For each task intending to use HFI immediate send, the HFI driver
 * calls use_cop() to obtain a coprocessor PID for the task.
 * The HFI driver then allocate a free HFI window and save the
 * coprocessor PID to the HFI window to allow the task to use the
 * HFI window.
 *
 * The HFI driver repeatedly creates immediate send packets and
 * issues icswx instruction to send data through the HFI window.
 * The HFI compares the coprocessor PID in the CPU PID register
 * to the PID held in the HFI window to determine if the transaction
 * is allowed.
 *
 * When the task to release the HFI window, the HFI driver calls
 * drop_cop() to release the coprocessor PID.
 */

#define COP_PID_NONE 0
#define COP_PID_MIN (COP_PID_NONE + 1)
#define COP_PID_MAX (0xFFFF)

static DEFINE_SPINLOCK(mmu_context_acop_lock);
static DEFINE_IDA(cop_ida);

void switch_cop(struct mm_struct *next)
{
	mtspr(SPRN_PID, next->context.cop_pid);
	mtspr(SPRN_ACOP, next->context.acop);
}

static int new_cop_pid(struct ida *ida, int min_id, int max_id,
		       spinlock_t *lock)
{
	int index;
	int err;

again:
	if (!ida_pre_get(ida, GFP_KERNEL))
		return -ENOMEM;

	spin_lock(lock);
	err = ida_get_new_above(ida, min_id, &index);
	spin_unlock(lock);

	if (err == -EAGAIN)
		goto again;
	else if (err)
		return err;

	if (index > max_id) {
		spin_lock(lock);
		ida_remove(ida, index);
		spin_unlock(lock);
		return -ENOMEM;
	}

	return index;
}

static void sync_cop(void *arg)
{
	struct mm_struct *mm = arg;

	if (mm == current->active_mm)
		switch_cop(current->active_mm);
}

/**
 * Start using a coprocessor.
 * @acop: mask of coprocessor to be used.
 * @mm: The mm the coprocessor to associate with. Most likely current mm.
 *
 * Return a positive PID if successful. Negative errno otherwise.
 * The returned PID will be fed to the coprocessor to determine if an
 * icswx transaction is authenticated.
 */
int use_cop(unsigned long acop, struct mm_struct *mm)
{
	int ret;

	if (!cpu_has_feature(CPU_FTR_ICSWX))
		return -ENODEV;

	if (!mm || !acop)
		return -EINVAL;

	/* The page_table_lock ensures mm_users won't change under us */
	spin_lock(&mm->page_table_lock);
	spin_lock(mm->context.cop_lockp);

	if (mm->context.cop_pid == COP_PID_NONE) {
		ret = new_cop_pid(&cop_ida, COP_PID_MIN, COP_PID_MAX,
				  &mmu_context_acop_lock);
		if (ret < 0)
			goto out;

		mm->context.cop_pid = ret;
	}
	mm->context.acop |= acop;

	sync_cop(mm);

	/*
	 * If this is a threaded process then there might be other threads
	 * running. We need to send an IPI to force them to pick up any
	 * change in PID and ACOP.
	 */
	if (atomic_read(&mm->mm_users) > 1)
		smp_call_function(sync_cop, mm, 1);

	ret = mm->context.cop_pid;

out:
	spin_unlock(mm->context.cop_lockp);
	spin_unlock(&mm->page_table_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(use_cop);

/**
 * Stop using a coprocessor.
 * @acop: mask of coprocessor to be stopped.
 * @mm: The mm the coprocessor associated with.
 */
void drop_cop(unsigned long acop, struct mm_struct *mm)
{
	int free_pid = COP_PID_NONE;

	if (!cpu_has_feature(CPU_FTR_ICSWX))
		return;

	if (WARN_ON_ONCE(!mm))
		return;

	/* The page_table_lock ensures mm_users won't change under us */
	spin_lock(&mm->page_table_lock);
	spin_lock(mm->context.cop_lockp);

	mm->context.acop &= ~acop;

	if ((!mm->context.acop) && (mm->context.cop_pid != COP_PID_NONE)) {
		free_pid = mm->context.cop_pid;
		mm->context.cop_pid = COP_PID_NONE;
	}

	sync_cop(mm);

	/*
	 * If this is a threaded process then there might be other threads
	 * running. We need to send an IPI to force them to pick up any
	 * change in PID and ACOP.
	 */
	if (atomic_read(&mm->mm_users) > 1)
		smp_call_function(sync_cop, mm, 1);

	if (free_pid != COP_PID_NONE) {
		spin_lock(&mmu_context_acop_lock);
		ida_remove(&cop_ida, free_pid);
		spin_unlock(&mmu_context_acop_lock);
	}

	spin_unlock(mm->context.cop_lockp);
	spin_unlock(&mm->page_table_lock);
}
EXPORT_SYMBOL_GPL(drop_cop);

#endif /* CONFIG_PPC_ICSWX */

static DEFINE_SPINLOCK(mmu_context_lock);
static DEFINE_IDA(mmu_context_ida);

/*
 * The proto-VSID space has 2^35 - 1 segments available for user mappings.
 * Each segment contains 2^28 bytes.  Each context maps 2^44 bytes,
 * so we can support 2^19-1 contexts (19 == 35 + 28 - 44).
 */
#define MAX_CONTEXT	((1UL << 19) - 1)

int __init_new_context(void)
{
	int index;
	int err;

again:
	if (!ida_pre_get(&mmu_context_ida, GFP_KERNEL))
		return -ENOMEM;

	spin_lock(&mmu_context_lock);
	err = ida_get_new_above(&mmu_context_ida, 1, &index);
	spin_unlock(&mmu_context_lock);

	if (err == -EAGAIN)
		goto again;
	else if (err)
		return err;

	if (index > MAX_CONTEXT) {
		spin_lock(&mmu_context_lock);
		ida_remove(&mmu_context_ida, index);
		spin_unlock(&mmu_context_lock);
		return -ENOMEM;
	}

	return index;
}
EXPORT_SYMBOL_GPL(__init_new_context);

int init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	int index;

	index = __init_new_context();
	if (index < 0)
		return index;

	/* The old code would re-promote on fork, we don't do that
	 * when using slices as it could cause problem promoting slices
	 * that have been forced down to 4K
	 */
	if (slice_mm_new_context(mm))
		slice_set_user_psize(mm, mmu_virtual_psize);
	subpage_prot_init_new_context(mm);
	mm->context.id = index;
#ifdef CONFIG_PPC_ICSWX
	mm->context.cop_lockp = kmalloc(sizeof(spinlock_t), GFP_KERNEL);
	if (!mm->context.cop_lockp) {
		__destroy_context(index);
		subpage_prot_free(mm);
		mm->context.id = MMU_NO_CONTEXT;
		return -ENOMEM;
	}
	spin_lock_init(mm->context.cop_lockp);
#endif /* CONFIG_PPC_ICSWX */

	return 0;
}

void __destroy_context(int context_id)
{
	spin_lock(&mmu_context_lock);
	ida_remove(&mmu_context_ida, context_id);
	spin_unlock(&mmu_context_lock);
}
EXPORT_SYMBOL_GPL(__destroy_context);

void destroy_context(struct mm_struct *mm)
{
#ifdef CONFIG_PPC_ICSWX
	drop_cop(mm->context.acop, mm);
	kfree(mm->context.cop_lockp);
	mm->context.cop_lockp = NULL;
#endif /* CONFIG_PPC_ICSWX */
	__destroy_context(mm->context.id);
	subpage_prot_free(mm);
	mm->context.id = MMU_NO_CONTEXT;
}
