// SPDX-License-Identifier: GPL-2.0+
/*
 * VAS Fault handling.
 * Copyright 2019, IBM Corporation
 */

#define pr_fmt(fmt) "vas: " fmt

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/sched/signal.h>
#include <linux/mmu_context.h>
#include <asm/icswx.h>

#include "vas.h"

/*
 * The maximum FIFO size for fault window can be 8MB
 * (VAS_RX_FIFO_SIZE_MAX). Using 4MB FIFO since each VAS
 * instance will be having fault window.
 * 8MB FIFO can be used if expects more faults for each VAS
 * instance.
 */
#define VAS_FAULT_WIN_FIFO_SIZE	(4 << 20)

static void dump_crb(struct coprocessor_request_block *crb)
{
	struct data_descriptor_entry *dde;
	struct nx_fault_stamp *nx;

	dde = &crb->source;
	pr_devel("SrcDDE: addr 0x%llx, len %d, count %d, idx %d, flags %d\n",
		be64_to_cpu(dde->address), be32_to_cpu(dde->length),
		dde->count, dde->index, dde->flags);

	dde = &crb->target;
	pr_devel("TgtDDE: addr 0x%llx, len %d, count %d, idx %d, flags %d\n",
		be64_to_cpu(dde->address), be32_to_cpu(dde->length),
		dde->count, dde->index, dde->flags);

	nx = &crb->stamp.nx;
	pr_devel("NX Stamp: PSWID 0x%x, FSA 0x%llx, flags 0x%x, FS 0x%x\n",
		be32_to_cpu(nx->pswid),
		be64_to_cpu(crb->stamp.nx.fault_storage_addr),
		nx->flags, nx->fault_status);
}

/*
 * Update the CSB to indicate a translation error.
 *
 * User space will be polling on CSB after the request is issued.
 * If NX can handle the request without any issues, it updates CSB.
 * Whereas if NX encounters page fault, the kernel will handle the
 * fault and update CSB with translation error.
 *
 * If we are unable to update the CSB means copy_to_user failed due to
 * invalid csb_addr, send a signal to the process.
 */
static void update_csb(struct vas_window *window,
			struct coprocessor_request_block *crb)
{
	struct coprocessor_status_block csb;
	struct kernel_siginfo info;
	struct task_struct *tsk;
	void __user *csb_addr;
	struct pid *pid;
	int rc;

	/*
	 * NX user space windows can not be opened for task->mm=NULL
	 * and faults will not be generated for kernel requests.
	 */
	if (WARN_ON_ONCE(!window->mm || !window->user_win))
		return;

	csb_addr = (void __user *)be64_to_cpu(crb->csb_addr);

	memset(&csb, 0, sizeof(csb));
	csb.cc = CSB_CC_FAULT_ADDRESS;
	csb.ce = CSB_CE_TERMINATION;
	csb.cs = 0;
	csb.count = 0;

	/*
	 * NX operates and returns in BE format as defined CRB struct.
	 * So saves fault_storage_addr in BE as NX pastes in FIFO and
	 * expects user space to convert to CPU format.
	 */
	csb.address = crb->stamp.nx.fault_storage_addr;
	csb.flags = 0;

	pid = window->pid;
	tsk = get_pid_task(pid, PIDTYPE_PID);
	/*
	 * Process closes send window after all pending NX requests are
	 * completed. In multi-thread applications, a child thread can
	 * open a window and can exit without closing it. May be some
	 * requests are pending or this window can be used by other
	 * threads later. We should handle faults if NX encounters
	 * pages faults on these requests. Update CSB with translation
	 * error and fault address. If csb_addr passed by user space is
	 * invalid, send SEGV signal to pid saved in window. If the
	 * child thread is not running, send the signal to tgid.
	 * Parent thread (tgid) will close this window upon its exit.
	 *
	 * pid and mm references are taken when window is opened by
	 * process (pid). So tgid is used only when child thread opens
	 * a window and exits without closing it.
	 */
	if (!tsk) {
		pid = window->tgid;
		tsk = get_pid_task(pid, PIDTYPE_PID);
		/*
		 * Parent thread (tgid) will be closing window when it
		 * exits. So should not get here.
		 */
		if (WARN_ON_ONCE(!tsk))
			return;
	}

	/* Return if the task is exiting. */
	if (tsk->flags & PF_EXITING) {
		put_task_struct(tsk);
		return;
	}

	kthread_use_mm(window->mm);
	rc = copy_to_user(csb_addr, &csb, sizeof(csb));
	/*
	 * User space polls on csb.flags (first byte). So add barrier
	 * then copy first byte with csb flags update.
	 */
	if (!rc) {
		csb.flags = CSB_V;
		/* Make sure update to csb.flags is visible now */
		smp_mb();
		rc = copy_to_user(csb_addr, &csb, sizeof(u8));
	}
	kthread_unuse_mm(window->mm);
	put_task_struct(tsk);

	/* Success */
	if (!rc)
		return;

	pr_debug("Invalid CSB address 0x%p signalling pid(%d)\n",
			csb_addr, pid_vnr(pid));

	clear_siginfo(&info);
	info.si_signo = SIGSEGV;
	info.si_errno = EFAULT;
	info.si_code = SEGV_MAPERR;
	info.si_addr = csb_addr;

	/*
	 * process will be polling on csb.flags after request is sent to
	 * NX. So generally CSB update should not fail except when an
	 * application passes invalid csb_addr. So an error message will
	 * be displayed and leave it to user space whether to ignore or
	 * handle this signal.
	 */
	rcu_read_lock();
	rc = kill_pid_info(SIGSEGV, &info, pid);
	rcu_read_unlock();

	pr_devel("%s(): pid %d kill_proc_info() rc %d\n", __func__,
			pid_vnr(pid), rc);
}

static void dump_fifo(struct vas_instance *vinst, void *entry)
{
	unsigned long *end = vinst->fault_fifo + vinst->fault_fifo_size;
	unsigned long *fifo = entry;
	int i;

	pr_err("Fault fifo size %d, Max crbs %d\n", vinst->fault_fifo_size,
			vinst->fault_fifo_size / CRB_SIZE);

	/* Dump 10 CRB entries or until end of FIFO */
	pr_err("Fault FIFO Dump:\n");
	for (i = 0; i < 10*(CRB_SIZE/8) && fifo < end; i += 4, fifo += 4) {
		pr_err("[%.3d, %p]: 0x%.16lx 0x%.16lx 0x%.16lx 0x%.16lx\n",
			i, fifo, *fifo, *(fifo+1), *(fifo+2), *(fifo+3));
	}
}

/*
 * Process valid CRBs in fault FIFO.
 * NX process user space requests, return credit and update the status
 * in CRB. If it encounters transalation error when accessing CRB or
 * request buffers, raises interrupt on the CPU to handle the fault.
 * It takes credit on fault window, updates nx_fault_stamp in CRB with
 * the following information and pastes CRB in fault FIFO.
 *
 * pswid - window ID of the window on which the request is sent.
 * fault_storage_addr - fault address
 *
 * It can raise a single interrupt for multiple faults. Expects OS to
 * process all valid faults and return credit for each fault on user
 * space and fault windows. This fault FIFO control will be done with
 * credit mechanism. NX can continuously paste CRBs until credits are not
 * available on fault window. Otherwise, returns with RMA_reject.
 *
 * Total credits available on fault window: FIFO_SIZE(4MB)/CRBS_SIZE(128)
 *
 */
irqreturn_t vas_fault_thread_fn(int irq, void *data)
{
	struct vas_instance *vinst = data;
	struct coprocessor_request_block *crb, *entry;
	struct coprocessor_request_block buf;
	struct vas_window *window;
	unsigned long flags;
	void *fifo;

	crb = &buf;

	/*
	 * VAS can interrupt with multiple page faults. So process all
	 * valid CRBs within fault FIFO until reaches invalid CRB.
	 * We use CCW[0] and pswid to validate validate CRBs:
	 *
	 * CCW[0]	Reserved bit. When NX pastes CRB, CCW[0]=0
	 *		OS sets this bit to 1 after reading CRB.
	 * pswid	NX assigns window ID. Set pswid to -1 after
	 *		reading CRB from fault FIFO.
	 *
	 * We exit this function if no valid CRBs are available to process.
	 * So acquire fault_lock and reset fifo_in_progress to 0 before
	 * exit.
	 * In case kernel receives another interrupt with different page
	 * fault, interrupt handler returns with IRQ_HANDLED if
	 * fifo_in_progress is set. Means these new faults will be
	 * handled by the current thread. Otherwise set fifo_in_progress
	 * and return IRQ_WAKE_THREAD to wake up thread.
	 */
	while (true) {
		spin_lock_irqsave(&vinst->fault_lock, flags);
		/*
		 * Advance the fault fifo pointer to next CRB.
		 * Use CRB_SIZE rather than sizeof(*crb) since the latter is
		 * aligned to CRB_ALIGN (256) but the CRB written to by VAS is
		 * only CRB_SIZE in len.
		 */
		fifo = vinst->fault_fifo + (vinst->fault_crbs * CRB_SIZE);
		entry = fifo;

		if ((entry->stamp.nx.pswid == cpu_to_be32(FIFO_INVALID_ENTRY))
			|| (entry->ccw & cpu_to_be32(CCW0_INVALID))) {
			vinst->fifo_in_progress = 0;
			spin_unlock_irqrestore(&vinst->fault_lock, flags);
			return IRQ_HANDLED;
		}

		spin_unlock_irqrestore(&vinst->fault_lock, flags);
		vinst->fault_crbs++;
		if (vinst->fault_crbs == (vinst->fault_fifo_size / CRB_SIZE))
			vinst->fault_crbs = 0;

		memcpy(crb, fifo, CRB_SIZE);
		entry->stamp.nx.pswid = cpu_to_be32(FIFO_INVALID_ENTRY);
		entry->ccw |= cpu_to_be32(CCW0_INVALID);
		/*
		 * Return credit for the fault window.
		 */
		vas_return_credit(vinst->fault_win, false);

		pr_devel("VAS[%d] fault_fifo %p, fifo %p, fault_crbs %d\n",
				vinst->vas_id, vinst->fault_fifo, fifo,
				vinst->fault_crbs);

		dump_crb(crb);
		window = vas_pswid_to_window(vinst,
				be32_to_cpu(crb->stamp.nx.pswid));

		if (IS_ERR(window)) {
			/*
			 * We got an interrupt about a specific send
			 * window but we can't find that window and we can't
			 * even clean it up (return credit on user space
			 * window).
			 * But we should not get here.
			 * TODO: Disable IRQ.
			 */
			dump_fifo(vinst, (void *)entry);
			pr_err("VAS[%d] fault_fifo %p, fifo %p, pswid 0x%x, fault_crbs %d bad CRB?\n",
				vinst->vas_id, vinst->fault_fifo, fifo,
				be32_to_cpu(crb->stamp.nx.pswid),
				vinst->fault_crbs);

			WARN_ON_ONCE(1);
		} else {
			update_csb(window, crb);
			/*
			 * Return credit for send window after processing
			 * fault CRB.
			 */
			vas_return_credit(window, true);
		}
	}
}

irqreturn_t vas_fault_handler(int irq, void *dev_id)
{
	struct vas_instance *vinst = dev_id;
	irqreturn_t ret = IRQ_WAKE_THREAD;
	unsigned long flags;

	/*
	 * NX can generate an interrupt for multiple faults. So the
	 * fault handler thread process all CRBs until finds invalid
	 * entry. In case if NX sees continuous faults, it is possible
	 * that the thread function entered with the first interrupt
	 * can execute and process all valid CRBs.
	 * So wake up thread only if the fault thread is not in progress.
	 */
	spin_lock_irqsave(&vinst->fault_lock, flags);

	if (vinst->fifo_in_progress)
		ret = IRQ_HANDLED;
	else
		vinst->fifo_in_progress = 1;

	spin_unlock_irqrestore(&vinst->fault_lock, flags);

	return ret;
}

/*
 * Fault window is opened per VAS instance. NX pastes fault CRB in fault
 * FIFO upon page faults.
 */
int vas_setup_fault_window(struct vas_instance *vinst)
{
	struct vas_rx_win_attr attr;

	vinst->fault_fifo_size = VAS_FAULT_WIN_FIFO_SIZE;
	vinst->fault_fifo = kzalloc(vinst->fault_fifo_size, GFP_KERNEL);
	if (!vinst->fault_fifo) {
		pr_err("Unable to alloc %d bytes for fault_fifo\n",
				vinst->fault_fifo_size);
		return -ENOMEM;
	}

	/*
	 * Invalidate all CRB entries. NX pastes valid entry for each fault.
	 */
	memset(vinst->fault_fifo, FIFO_INVALID_ENTRY, vinst->fault_fifo_size);
	vas_init_rx_win_attr(&attr, VAS_COP_TYPE_FAULT);

	attr.rx_fifo_size = vinst->fault_fifo_size;
	attr.rx_fifo = vinst->fault_fifo;

	/*
	 * Max creds is based on number of CRBs can fit in the FIFO.
	 * (fault_fifo_size/CRB_SIZE). If 8MB FIFO is used, max creds
	 * will be 0xffff since the receive creds field is 16bits wide.
	 */
	attr.wcreds_max = vinst->fault_fifo_size / CRB_SIZE;
	attr.lnotify_lpid = 0;
	attr.lnotify_pid = mfspr(SPRN_PID);
	attr.lnotify_tid = mfspr(SPRN_PID);

	vinst->fault_win = vas_rx_win_open(vinst->vas_id, VAS_COP_TYPE_FAULT,
					&attr);

	if (IS_ERR(vinst->fault_win)) {
		pr_err("VAS: Error %ld opening FaultWin\n",
			PTR_ERR(vinst->fault_win));
		kfree(vinst->fault_fifo);
		return PTR_ERR(vinst->fault_win);
	}

	pr_devel("VAS: Created FaultWin %d, LPID/PID/TID [%d/%d/%d]\n",
			vinst->fault_win->winid, attr.lnotify_lpid,
			attr.lnotify_pid, attr.lnotify_tid);

	return 0;
}
