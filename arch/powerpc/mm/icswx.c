/*
 *  ICSWX and ACOP Management
 *
 *  Copyright (C) 2011 Anton Blanchard, IBM Corp. <anton@samba.org>
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
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include "icswx.h"

/*
 * The processor and its L2 cache cause the icswx instruction to
 * generate a COP_REQ transaction on PowerBus. The transaction has no
 * address, and the processor does not perform an MMU access to
 * authenticate the transaction. The command portion of the PowerBus
 * COP_REQ transaction includes the LPAR_ID (LPID) and the coprocessor
 * Process ID (PID), which the coprocessor compares to the authorized
 * LPID and PID held in the coprocessor, to determine if the process
 * is authorized to generate the transaction.  The data of the COP_REQ
 * transaction is 128-byte or less in size and is placed in cacheable
 * memory on a 128-byte cache line boundary.
 *
 * The task to use a coprocessor should use use_cop() to mark the use
 * of the Coprocessor Type (CT) and context switching. On a server
 * class processor, the PID register is used only for coprocessor
 * management + * and so a coprocessor PID is allocated before
 * executing icswx + * instruction. Drop_cop() is used to free the
 * coprocessor PID.
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

void switch_cop(struct mm_struct *next)
{
#ifdef CONFIG_PPC_ICSWX_PID
	mtspr(SPRN_PID, next->context.cop_pid);
#endif
	mtspr(SPRN_ACOP, next->context.acop);
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

	ret = get_cop_pid(mm);
	if (ret < 0)
		goto out;

	/* update acop */
	mm->context.acop |= acop;

	sync_cop(mm);

	/*
	 * If this is a threaded process then there might be other threads
	 * running. We need to send an IPI to force them to pick up any
	 * change in PID and ACOP.
	 */
	if (atomic_read(&mm->mm_users) > 1)
		smp_call_function(sync_cop, mm, 1);

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
	int free_pid;

	if (!cpu_has_feature(CPU_FTR_ICSWX))
		return;

	if (WARN_ON_ONCE(!mm))
		return;

	/* The page_table_lock ensures mm_users won't change under us */
	spin_lock(&mm->page_table_lock);
	spin_lock(mm->context.cop_lockp);

	mm->context.acop &= ~acop;

	free_pid = disable_cop_pid(mm);
	sync_cop(mm);

	/*
	 * If this is a threaded process then there might be other threads
	 * running. We need to send an IPI to force them to pick up any
	 * change in PID and ACOP.
	 */
	if (atomic_read(&mm->mm_users) > 1)
		smp_call_function(sync_cop, mm, 1);

	if (free_pid != COP_PID_NONE)
		free_cop_pid(free_pid);

	spin_unlock(mm->context.cop_lockp);
	spin_unlock(&mm->page_table_lock);
}
EXPORT_SYMBOL_GPL(drop_cop);

static int acop_use_cop(int ct)
{
	/* There is no alternate policy, yet */
	return -1;
}

/*
 * Get the instruction word at the NIP
 */
static u32 acop_get_inst(struct pt_regs *regs)
{
	u32 inst;
	u32 __user *p;

	p = (u32 __user *)regs->nip;
	if (!access_ok(VERIFY_READ, p, sizeof(*p)))
		return 0;

	if (__get_user(inst, p))
		return 0;

	return inst;
}

/**
 * @regs: regsiters at time of interrupt
 * @address: storage address
 * @error_code: Fault code, usually the DSISR or ESR depending on
 *		processor type
 *
 * Return 0 if we are able to resolve the data storage fault that
 * results from a CT miss in the ACOP register.
 */
int acop_handle_fault(struct pt_regs *regs, unsigned long address,
		      unsigned long error_code)
{
	int ct;
	u32 inst = 0;

	if (!cpu_has_feature(CPU_FTR_ICSWX)) {
		pr_info("No coprocessors available");
		_exception(SIGILL, regs, ILL_ILLOPN, address);
	}

	if (!user_mode(regs)) {
		/* this could happen if the HV denies the
		 * kernel access, for now we just die */
		die("ICSWX from kernel failed", regs, SIGSEGV);
	}

	/* Some implementations leave us a hint for the CT */
	ct = ICSWX_GET_CT_HINT(error_code);
	if (ct < 0) {
		/* we have to peek at the instruction word to figure out CT */
		u32 ccw;
		u32 rs;

		inst = acop_get_inst(regs);
		if (inst == 0)
			return -1;

		rs = (inst >> (31 - 10)) & 0x1f;
		ccw = regs->gpr[rs];
		ct = (ccw >> 16) & 0x3f;
	}

	/*
	 * We could be here because another thread has enabled acop
	 * but the ACOP register has yet to be updated.
	 *
	 * This should have been taken care of by the IPI to sync all
	 * the threads (see smp_call_function(sync_cop, mm, 1)), but
	 * that could take forever if there are a significant amount
	 * of threads.
	 *
	 * Given the number of threads on some of these systems,
	 * perhaps this is the best way to sync ACOP rather than whack
	 * every thread with an IPI.
	 */
	if ((acop_copro_type_bit(ct) & current->active_mm->context.acop) != 0) {
		sync_cop(current->active_mm);
		return 0;
	}

	/* check for alternate policy */
	if (!acop_use_cop(ct))
		return 0;

	/* at this point the CT is unknown to the system */
	pr_warn("%s[%d]: Coprocessor %d is unavailable\n",
		current->comm, current->pid, ct);

	/* get inst if we don't already have it */
	if (inst == 0) {
		inst = acop_get_inst(regs);
		if (inst == 0)
			return -1;
	}

	/* Check if the instruction is the "record form" */
	if (inst & 1) {
		/*
		 * the instruction is "record" form so we can reject
		 * using CR0
		 */
		regs->ccr &= ~(0xful << 28);
		regs->ccr |= ICSWX_RC_NOT_FOUND << 28;

		/* Move on to the next instruction */
		regs->nip += 4;
	} else {
		/*
		 * There is no architected mechanism to report a bad
		 * CT so we could either SIGILL or report nothing.
		 * Since the non-record version should only bu used
		 * for "hints" or "don't care" we should probably do
		 * nothing.  However, I could see how some people
		 * might want an SIGILL so it here if you want it.
		 */
#ifdef CONFIG_PPC_ICSWX_USE_SIGILL
		_exception(SIGILL, regs, ILL_ILLOPN, address);
#else
		regs->nip += 4;
#endif
	}

	return 0;
}
EXPORT_SYMBOL_GPL(acop_handle_fault);
