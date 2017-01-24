/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * Copyright 2012 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/kernel.h>
#include <asm/opal.h>
#include <asm/mce.h>
#include <asm/machdep.h>
#include <asm/cputhreads.h>
#include <asm/hmi.h>
#include <asm/kvm_ppc.h>

/* SRR1 bits for machine check on POWER7 */
#define SRR1_MC_LDSTERR		(1ul << (63-42))
#define SRR1_MC_IFETCH_SH	(63-45)
#define SRR1_MC_IFETCH_MASK	0x7
#define SRR1_MC_IFETCH_SLBPAR		2	/* SLB parity error */
#define SRR1_MC_IFETCH_SLBMULTI		3	/* SLB multi-hit */
#define SRR1_MC_IFETCH_SLBPARMULTI	4	/* SLB parity + multi-hit */
#define SRR1_MC_IFETCH_TLBMULTI		5	/* I-TLB multi-hit */

/* DSISR bits for machine check on POWER7 */
#define DSISR_MC_DERAT_MULTI	0x800		/* D-ERAT multi-hit */
#define DSISR_MC_TLB_MULTI	0x400		/* D-TLB multi-hit */
#define DSISR_MC_SLB_PARITY	0x100		/* SLB parity error */
#define DSISR_MC_SLB_MULTI	0x080		/* SLB multi-hit */
#define DSISR_MC_SLB_PARMULTI	0x040		/* SLB parity + multi-hit */

/* POWER7 SLB flush and reload */
static void reload_slb(struct kvm_vcpu *vcpu)
{
	struct slb_shadow *slb;
	unsigned long i, n;

	/* First clear out SLB */
	asm volatile("slbmte %0,%0; slbia" : : "r" (0));

	/* Do they have an SLB shadow buffer registered? */
	slb = vcpu->arch.slb_shadow.pinned_addr;
	if (!slb)
		return;

	/* Sanity check */
	n = min_t(u32, be32_to_cpu(slb->persistent), SLB_MIN_SIZE);
	if ((void *) &slb->save_area[n] > vcpu->arch.slb_shadow.pinned_end)
		return;

	/* Load up the SLB from that */
	for (i = 0; i < n; ++i) {
		unsigned long rb = be64_to_cpu(slb->save_area[i].esid);
		unsigned long rs = be64_to_cpu(slb->save_area[i].vsid);

		rb = (rb & ~0xFFFul) | i;	/* insert entry number */
		asm volatile("slbmte %0,%1" : : "r" (rs), "r" (rb));
	}
}

/*
 * On POWER7, see if we can handle a machine check that occurred inside
 * the guest in real mode, without switching to the host partition.
 *
 * Returns: 0 => exit guest, 1 => deliver machine check to guest
 */
static long kvmppc_realmode_mc_power7(struct kvm_vcpu *vcpu)
{
	unsigned long srr1 = vcpu->arch.shregs.msr;
	struct machine_check_event mce_evt;
	long handled = 1;

	if (srr1 & SRR1_MC_LDSTERR) {
		/* error on load/store */
		unsigned long dsisr = vcpu->arch.shregs.dsisr;

		if (dsisr & (DSISR_MC_SLB_PARMULTI | DSISR_MC_SLB_MULTI |
			     DSISR_MC_SLB_PARITY | DSISR_MC_DERAT_MULTI)) {
			/* flush and reload SLB; flushes D-ERAT too */
			reload_slb(vcpu);
			dsisr &= ~(DSISR_MC_SLB_PARMULTI | DSISR_MC_SLB_MULTI |
				   DSISR_MC_SLB_PARITY | DSISR_MC_DERAT_MULTI);
		}
		if (dsisr & DSISR_MC_TLB_MULTI) {
			if (cur_cpu_spec && cur_cpu_spec->flush_tlb)
				cur_cpu_spec->flush_tlb(TLB_INVAL_SCOPE_LPID);
			dsisr &= ~DSISR_MC_TLB_MULTI;
		}
		/* Any other errors we don't understand? */
		if (dsisr & 0xffffffffUL)
			handled = 0;
	}

	switch ((srr1 >> SRR1_MC_IFETCH_SH) & SRR1_MC_IFETCH_MASK) {
	case 0:
		break;
	case SRR1_MC_IFETCH_SLBPAR:
	case SRR1_MC_IFETCH_SLBMULTI:
	case SRR1_MC_IFETCH_SLBPARMULTI:
		reload_slb(vcpu);
		break;
	case SRR1_MC_IFETCH_TLBMULTI:
		if (cur_cpu_spec && cur_cpu_spec->flush_tlb)
			cur_cpu_spec->flush_tlb(TLB_INVAL_SCOPE_LPID);
		break;
	default:
		handled = 0;
	}

	/*
	 * See if we have already handled the condition in the linux host.
	 * We assume that if the condition is recovered then linux host
	 * will have generated an error log event that we will pick
	 * up and log later.
	 * Don't release mce event now. We will queue up the event so that
	 * we can log the MCE event info on host console.
	 */
	if (!get_mce_event(&mce_evt, MCE_EVENT_DONTRELEASE))
		goto out;

	if (mce_evt.version == MCE_V1 &&
	    (mce_evt.severity == MCE_SEV_NO_ERROR ||
	     mce_evt.disposition == MCE_DISPOSITION_RECOVERED))
		handled = 1;

out:
	/*
	 * We are now going enter guest either through machine check
	 * interrupt (for unhandled errors) or will continue from
	 * current HSRR0 (for handled errors) in guest. Hence
	 * queue up the event so that we can log it from host console later.
	 */
	machine_check_queue_event();

	return handled;
}

long kvmppc_realmode_machine_check(struct kvm_vcpu *vcpu)
{
	return kvmppc_realmode_mc_power7(vcpu);
}

/* Check if dynamic split is in force and return subcore size accordingly. */
static inline int kvmppc_cur_subcore_size(void)
{
	if (local_paca->kvm_hstate.kvm_split_mode)
		return local_paca->kvm_hstate.kvm_split_mode->subcore_size;

	return threads_per_subcore;
}

void kvmppc_subcore_enter_guest(void)
{
	int thread_id, subcore_id;

	thread_id = cpu_thread_in_core(local_paca->paca_index);
	subcore_id = thread_id / kvmppc_cur_subcore_size();

	local_paca->sibling_subcore_state->in_guest[subcore_id] = 1;
}

void kvmppc_subcore_exit_guest(void)
{
	int thread_id, subcore_id;

	thread_id = cpu_thread_in_core(local_paca->paca_index);
	subcore_id = thread_id / kvmppc_cur_subcore_size();

	local_paca->sibling_subcore_state->in_guest[subcore_id] = 0;
}

static bool kvmppc_tb_resync_required(void)
{
	if (test_and_set_bit(CORE_TB_RESYNC_REQ_BIT,
				&local_paca->sibling_subcore_state->flags))
		return false;

	return true;
}

static void kvmppc_tb_resync_done(void)
{
	clear_bit(CORE_TB_RESYNC_REQ_BIT,
			&local_paca->sibling_subcore_state->flags);
}

/*
 * kvmppc_realmode_hmi_handler() is called only by primary thread during
 * guest exit path.
 *
 * There are multiple reasons why HMI could occur, one of them is
 * Timebase (TB) error. If this HMI is due to TB error, then TB would
 * have been in stopped state. The opal hmi handler Will fix it and
 * restore the TB value with host timebase value. For HMI caused due
 * to non-TB errors, opal hmi handler will not touch/restore TB register
 * and hence there won't be any change in TB value.
 *
 * Since we are not sure about the cause of this HMI, we can't be sure
 * about the content of TB register whether it holds guest or host timebase
 * value. Hence the idea is to resync the TB on every HMI, so that we
 * know about the exact state of the TB value. Resync TB call will
 * restore TB to host timebase.
 *
 * Things to consider:
 * - On TB error, HMI interrupt is reported on all the threads of the core
 *   that has encountered TB error irrespective of split-core mode.
 * - The very first thread on the core that get chance to fix TB error
 *   would rsync the TB with local chipTOD value.
 * - The resync TB is a core level action i.e. it will sync all the TBs
 *   in that core independent of split-core mode. This means if we trigger
 *   TB sync from a thread from one subcore, it would affect TB values of
 *   sibling subcores of the same core.
 *
 * All threads need to co-ordinate before making opal hmi handler.
 * All threads will use sibling_subcore_state->in_guest[] (shared by all
 * threads in the core) in paca which holds information about whether
 * sibling subcores are in Guest mode or host mode. The in_guest[] array
 * is of size MAX_SUBCORE_PER_CORE=4, indexed using subcore id to set/unset
 * subcore status. Only primary threads from each subcore is responsible
 * to set/unset its designated array element while entering/exiting the
 * guset.
 *
 * After invoking opal hmi handler call, one of the thread (of entire core)
 * will need to resync the TB. Bit 63 from subcore state bitmap flags
 * (sibling_subcore_state->flags) will be used to co-ordinate between
 * primary threads to decide who takes up the responsibility.
 *
 * This is what we do:
 * - Primary thread from each subcore tries to set resync required bit[63]
 *   of paca->sibling_subcore_state->flags.
 * - The first primary thread that is able to set the flag takes the
 *   responsibility of TB resync. (Let us call it as thread leader)
 * - All other threads which are in host will call
 *   wait_for_subcore_guest_exit() and wait for in_guest[0-3] from
 *   paca->sibling_subcore_state to get cleared.
 * - All the primary thread will clear its subcore status from subcore
 *   state in_guest[] array respectively.
 * - Once all primary threads clear in_guest[0-3], all of them will invoke
 *   opal hmi handler.
 * - Now all threads will wait for TB resync to complete by invoking
 *   wait_for_tb_resync() except the thread leader.
 * - Thread leader will do a TB resync by invoking opal_resync_timebase()
 *   call and the it will clear the resync required bit.
 * - All other threads will now come out of resync wait loop and proceed
 *   with individual execution.
 * - On return of this function, primary thread will signal all
 *   secondary threads to proceed.
 * - All secondary threads will eventually call opal hmi handler on
 *   their exit path.
 */

long kvmppc_realmode_hmi_handler(void)
{
	int ptid = local_paca->kvm_hstate.ptid;
	bool resync_req;

	/* This is only called on primary thread. */
	BUG_ON(ptid != 0);
	__this_cpu_inc(irq_stat.hmi_exceptions);

	/*
	 * By now primary thread has already completed guest->host
	 * partition switch but haven't signaled secondaries yet.
	 * All the secondary threads on this subcore is waiting
	 * for primary thread to signal them to go ahead.
	 *
	 * For threads from subcore which isn't in guest, they all will
	 * wait until all other subcores on this core exit the guest.
	 *
	 * Now set the resync required bit. If you are the first to
	 * set this bit then kvmppc_tb_resync_required() function will
	 * return true. For rest all other subcores
	 * kvmppc_tb_resync_required() will return false.
	 *
	 * If resync_req == true, then this thread is responsible to
	 * initiate TB resync after hmi handler has completed.
	 * All other threads on this core will wait until this thread
	 * clears the resync required bit flag.
	 */
	resync_req = kvmppc_tb_resync_required();

	/* Reset the subcore status to indicate it has exited guest */
	kvmppc_subcore_exit_guest();

	/*
	 * Wait for other subcores on this core to exit the guest.
	 * All the primary threads and threads from subcore that are
	 * not in guest will wait here until all subcores are out
	 * of guest context.
	 */
	wait_for_subcore_guest_exit();

	/*
	 * At this point we are sure that primary threads from each
	 * subcore on this core have completed guest->host partition
	 * switch. Now it is safe to call HMI handler.
	 */
	if (ppc_md.hmi_exception_early)
		ppc_md.hmi_exception_early(NULL);

	/*
	 * Check if this thread is responsible to resync TB.
	 * All other threads will wait until this thread completes the
	 * TB resync.
	 */
	if (resync_req) {
		opal_resync_timebase();
		/* Reset TB resync req bit */
		kvmppc_tb_resync_done();
	} else {
		wait_for_tb_resync();
	}
	return 0;
}
