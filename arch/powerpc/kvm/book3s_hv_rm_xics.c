/*
 * Copyright 2012 Michael Ellerman, IBM Corporation.
 * Copyright 2012 Benjamin Herrenschmidt, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/kvm_host.h>
#include <linux/err.h>
#include <linux/kernel_stat.h>

#include <asm/kvm_book3s.h>
#include <asm/kvm_ppc.h>
#include <asm/hvcall.h>
#include <asm/xics.h>
#include <asm/synch.h>
#include <asm/cputhreads.h>
#include <asm/pgtable.h>
#include <asm/ppc-opcode.h>
#include <asm/pnv-pci.h>
#include <asm/opal.h>
#include <asm/smp.h>

#include "book3s_xics.h"

#define DEBUG_PASSUP

int h_ipi_redirect = 1;
EXPORT_SYMBOL(h_ipi_redirect);
int kvm_irq_bypass = 1;
EXPORT_SYMBOL(kvm_irq_bypass);

static void icp_rm_deliver_irq(struct kvmppc_xics *xics, struct kvmppc_icp *icp,
			    u32 new_irq, bool check_resend);
static int xics_opal_set_server(unsigned int hw_irq, int server_cpu);

/* -- ICS routines -- */
static void ics_rm_check_resend(struct kvmppc_xics *xics,
				struct kvmppc_ics *ics, struct kvmppc_icp *icp)
{
	int i;

	for (i = 0; i < KVMPPC_XICS_IRQ_PER_ICS; i++) {
		struct ics_irq_state *state = &ics->irq_state[i];
		if (state->resend)
			icp_rm_deliver_irq(xics, icp, state->number, true);
	}

}

/* -- ICP routines -- */

#ifdef CONFIG_SMP
static inline void icp_send_hcore_msg(int hcore, struct kvm_vcpu *vcpu)
{
	int hcpu;

	hcpu = hcore << threads_shift;
	kvmppc_host_rm_ops_hv->rm_core[hcore].rm_data = vcpu;
	smp_muxed_ipi_set_message(hcpu, PPC_MSG_RM_HOST_ACTION);
	kvmppc_set_host_ipi(hcpu, 1);
	smp_mb();
	kvmhv_rm_send_ipi(hcpu);
}
#else
static inline void icp_send_hcore_msg(int hcore, struct kvm_vcpu *vcpu) { }
#endif

/*
 * We start the search from our current CPU Id in the core map
 * and go in a circle until we get back to our ID looking for a
 * core that is running in host context and that hasn't already
 * been targeted for another rm_host_ops.
 *
 * In the future, could consider using a fairer algorithm (one
 * that distributes the IPIs better)
 *
 * Returns -1, if no CPU could be found in the host
 * Else, returns a CPU Id which has been reserved for use
 */
static inline int grab_next_hostcore(int start,
		struct kvmppc_host_rm_core *rm_core, int max, int action)
{
	bool success;
	int core;
	union kvmppc_rm_state old, new;

	for (core = start + 1; core < max; core++)  {
		old = new = READ_ONCE(rm_core[core].rm_state);

		if (!old.in_host || old.rm_action)
			continue;

		/* Try to grab this host core if not taken already. */
		new.rm_action = action;

		success = cmpxchg64(&rm_core[core].rm_state.raw,
						old.raw, new.raw) == old.raw;
		if (success) {
			/*
			 * Make sure that the store to the rm_action is made
			 * visible before we return to caller (and the
			 * subsequent store to rm_data) to synchronize with
			 * the IPI handler.
			 */
			smp_wmb();
			return core;
		}
	}

	return -1;
}

static inline int find_available_hostcore(int action)
{
	int core;
	int my_core = smp_processor_id() >> threads_shift;
	struct kvmppc_host_rm_core *rm_core = kvmppc_host_rm_ops_hv->rm_core;

	core = grab_next_hostcore(my_core, rm_core, cpu_nr_cores(), action);
	if (core == -1)
		core = grab_next_hostcore(core, rm_core, my_core, action);

	return core;
}

static void icp_rm_set_vcpu_irq(struct kvm_vcpu *vcpu,
				struct kvm_vcpu *this_vcpu)
{
	struct kvmppc_icp *this_icp = this_vcpu->arch.icp;
	int cpu;
	int hcore;

	/* Mark the target VCPU as having an interrupt pending */
	vcpu->stat.queue_intr++;
	set_bit(BOOK3S_IRQPRIO_EXTERNAL_LEVEL, &vcpu->arch.pending_exceptions);

	/* Kick self ? Just set MER and return */
	if (vcpu == this_vcpu) {
		mtspr(SPRN_LPCR, mfspr(SPRN_LPCR) | LPCR_MER);
		return;
	}

	/*
	 * Check if the core is loaded,
	 * if not, find an available host core to post to wake the VCPU,
	 * if we can't find one, set up state to eventually return too hard.
	 */
	cpu = vcpu->arch.thread_cpu;
	if (cpu < 0 || cpu >= nr_cpu_ids) {
		hcore = -1;
		if (kvmppc_host_rm_ops_hv && h_ipi_redirect)
			hcore = find_available_hostcore(XICS_RM_KICK_VCPU);
		if (hcore != -1) {
			icp_send_hcore_msg(hcore, vcpu);
		} else {
			this_icp->rm_action |= XICS_RM_KICK_VCPU;
			this_icp->rm_kick_target = vcpu;
		}
		return;
	}

	smp_mb();
	kvmhv_rm_send_ipi(cpu);
}

static void icp_rm_clr_vcpu_irq(struct kvm_vcpu *vcpu)
{
	/* Note: Only called on self ! */
	clear_bit(BOOK3S_IRQPRIO_EXTERNAL_LEVEL,
		  &vcpu->arch.pending_exceptions);
	mtspr(SPRN_LPCR, mfspr(SPRN_LPCR) & ~LPCR_MER);
}

static inline bool icp_rm_try_update(struct kvmppc_icp *icp,
				     union kvmppc_icp_state old,
				     union kvmppc_icp_state new)
{
	struct kvm_vcpu *this_vcpu = local_paca->kvm_hstate.kvm_vcpu;
	bool success;

	/* Calculate new output value */
	new.out_ee = (new.xisr && (new.pending_pri < new.cppr));

	/* Attempt atomic update */
	success = cmpxchg64(&icp->state.raw, old.raw, new.raw) == old.raw;
	if (!success)
		goto bail;

	/*
	 * Check for output state update
	 *
	 * Note that this is racy since another processor could be updating
	 * the state already. This is why we never clear the interrupt output
	 * here, we only ever set it. The clear only happens prior to doing
	 * an update and only by the processor itself. Currently we do it
	 * in Accept (H_XIRR) and Up_Cppr (H_XPPR).
	 *
	 * We also do not try to figure out whether the EE state has changed,
	 * we unconditionally set it if the new state calls for it. The reason
	 * for that is that we opportunistically remove the pending interrupt
	 * flag when raising CPPR, so we need to set it back here if an
	 * interrupt is still pending.
	 */
	if (new.out_ee)
		icp_rm_set_vcpu_irq(icp->vcpu, this_vcpu);

	/* Expose the state change for debug purposes */
	this_vcpu->arch.icp->rm_dbgstate = new;
	this_vcpu->arch.icp->rm_dbgtgt = icp->vcpu;

 bail:
	return success;
}

static inline int check_too_hard(struct kvmppc_xics *xics,
				 struct kvmppc_icp *icp)
{
	return (xics->real_mode_dbg || icp->rm_action) ? H_TOO_HARD : H_SUCCESS;
}

static void icp_rm_check_resend(struct kvmppc_xics *xics,
			     struct kvmppc_icp *icp)
{
	u32 icsid;

	/* Order this load with the test for need_resend in the caller */
	smp_rmb();
	for_each_set_bit(icsid, icp->resend_map, xics->max_icsid + 1) {
		struct kvmppc_ics *ics = xics->ics[icsid];

		if (!test_and_clear_bit(icsid, icp->resend_map))
			continue;
		if (!ics)
			continue;
		ics_rm_check_resend(xics, ics, icp);
	}
}

static bool icp_rm_try_to_deliver(struct kvmppc_icp *icp, u32 irq, u8 priority,
			       u32 *reject)
{
	union kvmppc_icp_state old_state, new_state;
	bool success;

	do {
		old_state = new_state = READ_ONCE(icp->state);

		*reject = 0;

		/* See if we can deliver */
		success = new_state.cppr > priority &&
			new_state.mfrr > priority &&
			new_state.pending_pri > priority;

		/*
		 * If we can, check for a rejection and perform the
		 * delivery
		 */
		if (success) {
			*reject = new_state.xisr;
			new_state.xisr = irq;
			new_state.pending_pri = priority;
		} else {
			/*
			 * If we failed to deliver we set need_resend
			 * so a subsequent CPPR state change causes us
			 * to try a new delivery.
			 */
			new_state.need_resend = true;
		}

	} while (!icp_rm_try_update(icp, old_state, new_state));

	return success;
}

static void icp_rm_deliver_irq(struct kvmppc_xics *xics, struct kvmppc_icp *icp,
			    u32 new_irq, bool check_resend)
{
	struct ics_irq_state *state;
	struct kvmppc_ics *ics;
	u32 reject;
	u16 src;

	/*
	 * This is used both for initial delivery of an interrupt and
	 * for subsequent rejection.
	 *
	 * Rejection can be racy vs. resends. We have evaluated the
	 * rejection in an atomic ICP transaction which is now complete,
	 * so potentially the ICP can already accept the interrupt again.
	 *
	 * So we need to retry the delivery. Essentially the reject path
	 * boils down to a failed delivery. Always.
	 *
	 * Now the interrupt could also have moved to a different target,
	 * thus we may need to re-do the ICP lookup as well
	 */

 again:
	/* Get the ICS state and lock it */
	ics = kvmppc_xics_find_ics(xics, new_irq, &src);
	if (!ics) {
		/* Unsafe increment, but this does not need to be accurate */
		xics->err_noics++;
		return;
	}
	state = &ics->irq_state[src];

	/* Get a lock on the ICS */
	arch_spin_lock(&ics->lock);

	/* Get our server */
	if (!icp || state->server != icp->server_num) {
		icp = kvmppc_xics_find_server(xics->kvm, state->server);
		if (!icp) {
			/* Unsafe increment again*/
			xics->err_noicp++;
			goto out;
		}
	}

	if (check_resend)
		if (!state->resend)
			goto out;

	/* Clear the resend bit of that interrupt */
	state->resend = 0;

	/*
	 * If masked, bail out
	 *
	 * Note: PAPR doesn't mention anything about masked pending
	 * when doing a resend, only when doing a delivery.
	 *
	 * However that would have the effect of losing a masked
	 * interrupt that was rejected and isn't consistent with
	 * the whole masked_pending business which is about not
	 * losing interrupts that occur while masked.
	 *
	 * I don't differentiate normal deliveries and resends, this
	 * implementation will differ from PAPR and not lose such
	 * interrupts.
	 */
	if (state->priority == MASKED) {
		state->masked_pending = 1;
		goto out;
	}

	/*
	 * Try the delivery, this will set the need_resend flag
	 * in the ICP as part of the atomic transaction if the
	 * delivery is not possible.
	 *
	 * Note that if successful, the new delivery might have itself
	 * rejected an interrupt that was "delivered" before we took the
	 * ics spin lock.
	 *
	 * In this case we do the whole sequence all over again for the
	 * new guy. We cannot assume that the rejected interrupt is less
	 * favored than the new one, and thus doesn't need to be delivered,
	 * because by the time we exit icp_rm_try_to_deliver() the target
	 * processor may well have already consumed & completed it, and thus
	 * the rejected interrupt might actually be already acceptable.
	 */
	if (icp_rm_try_to_deliver(icp, new_irq, state->priority, &reject)) {
		/*
		 * Delivery was successful, did we reject somebody else ?
		 */
		if (reject && reject != XICS_IPI) {
			arch_spin_unlock(&ics->lock);
			icp->n_reject++;
			new_irq = reject;
			check_resend = 0;
			goto again;
		}
	} else {
		/*
		 * We failed to deliver the interrupt we need to set the
		 * resend map bit and mark the ICS state as needing a resend
		 */
		state->resend = 1;

		/*
		 * Make sure when checking resend, we don't miss the resend
		 * if resend_map bit is seen and cleared.
		 */
		smp_wmb();
		set_bit(ics->icsid, icp->resend_map);

		/*
		 * If the need_resend flag got cleared in the ICP some time
		 * between icp_rm_try_to_deliver() atomic update and now, then
		 * we know it might have missed the resend_map bit. So we
		 * retry
		 */
		smp_mb();
		if (!icp->state.need_resend) {
			state->resend = 0;
			arch_spin_unlock(&ics->lock);
			check_resend = 0;
			goto again;
		}
	}
 out:
	arch_spin_unlock(&ics->lock);
}

static void icp_rm_down_cppr(struct kvmppc_xics *xics, struct kvmppc_icp *icp,
			     u8 new_cppr)
{
	union kvmppc_icp_state old_state, new_state;
	bool resend;

	/*
	 * This handles several related states in one operation:
	 *
	 * ICP State: Down_CPPR
	 *
	 * Load CPPR with new value and if the XISR is 0
	 * then check for resends:
	 *
	 * ICP State: Resend
	 *
	 * If MFRR is more favored than CPPR, check for IPIs
	 * and notify ICS of a potential resend. This is done
	 * asynchronously (when used in real mode, we will have
	 * to exit here).
	 *
	 * We do not handle the complete Check_IPI as documented
	 * here. In the PAPR, this state will be used for both
	 * Set_MFRR and Down_CPPR. However, we know that we aren't
	 * changing the MFRR state here so we don't need to handle
	 * the case of an MFRR causing a reject of a pending irq,
	 * this will have been handled when the MFRR was set in the
	 * first place.
	 *
	 * Thus we don't have to handle rejects, only resends.
	 *
	 * When implementing real mode for HV KVM, resend will lead to
	 * a H_TOO_HARD return and the whole transaction will be handled
	 * in virtual mode.
	 */
	do {
		old_state = new_state = READ_ONCE(icp->state);

		/* Down_CPPR */
		new_state.cppr = new_cppr;

		/*
		 * Cut down Resend / Check_IPI / IPI
		 *
		 * The logic is that we cannot have a pending interrupt
		 * trumped by an IPI at this point (see above), so we
		 * know that either the pending interrupt is already an
		 * IPI (in which case we don't care to override it) or
		 * it's either more favored than us or non existent
		 */
		if (new_state.mfrr < new_cppr &&
		    new_state.mfrr <= new_state.pending_pri) {
			new_state.pending_pri = new_state.mfrr;
			new_state.xisr = XICS_IPI;
		}

		/* Latch/clear resend bit */
		resend = new_state.need_resend;
		new_state.need_resend = 0;

	} while (!icp_rm_try_update(icp, old_state, new_state));

	/*
	 * Now handle resend checks. Those are asynchronous to the ICP
	 * state update in HW (ie bus transactions) so we can handle them
	 * separately here as well.
	 */
	if (resend) {
		icp->n_check_resend++;
		icp_rm_check_resend(xics, icp);
	}
}


unsigned long xics_rm_h_xirr(struct kvm_vcpu *vcpu)
{
	union kvmppc_icp_state old_state, new_state;
	struct kvmppc_xics *xics = vcpu->kvm->arch.xics;
	struct kvmppc_icp *icp = vcpu->arch.icp;
	u32 xirr;

	if (!xics || !xics->real_mode)
		return H_TOO_HARD;

	/* First clear the interrupt */
	icp_rm_clr_vcpu_irq(icp->vcpu);

	/*
	 * ICP State: Accept_Interrupt
	 *
	 * Return the pending interrupt (if any) along with the
	 * current CPPR, then clear the XISR & set CPPR to the
	 * pending priority
	 */
	do {
		old_state = new_state = READ_ONCE(icp->state);

		xirr = old_state.xisr | (((u32)old_state.cppr) << 24);
		if (!old_state.xisr)
			break;
		new_state.cppr = new_state.pending_pri;
		new_state.pending_pri = 0xff;
		new_state.xisr = 0;

	} while (!icp_rm_try_update(icp, old_state, new_state));

	/* Return the result in GPR4 */
	vcpu->arch.gpr[4] = xirr;

	return check_too_hard(xics, icp);
}

int xics_rm_h_ipi(struct kvm_vcpu *vcpu, unsigned long server,
		  unsigned long mfrr)
{
	union kvmppc_icp_state old_state, new_state;
	struct kvmppc_xics *xics = vcpu->kvm->arch.xics;
	struct kvmppc_icp *icp, *this_icp = vcpu->arch.icp;
	u32 reject;
	bool resend;
	bool local;

	if (!xics || !xics->real_mode)
		return H_TOO_HARD;

	local = this_icp->server_num == server;
	if (local)
		icp = this_icp;
	else
		icp = kvmppc_xics_find_server(vcpu->kvm, server);
	if (!icp)
		return H_PARAMETER;

	/*
	 * ICP state: Set_MFRR
	 *
	 * If the CPPR is more favored than the new MFRR, then
	 * nothing needs to be done as there can be no XISR to
	 * reject.
	 *
	 * ICP state: Check_IPI
	 *
	 * If the CPPR is less favored, then we might be replacing
	 * an interrupt, and thus need to possibly reject it.
	 *
	 * ICP State: IPI
	 *
	 * Besides rejecting any pending interrupts, we also
	 * update XISR and pending_pri to mark IPI as pending.
	 *
	 * PAPR does not describe this state, but if the MFRR is being
	 * made less favored than its earlier value, there might be
	 * a previously-rejected interrupt needing to be resent.
	 * Ideally, we would want to resend only if
	 *	prio(pending_interrupt) < mfrr &&
	 *	prio(pending_interrupt) < cppr
	 * where pending interrupt is the one that was rejected. But
	 * we don't have that state, so we simply trigger a resend
	 * whenever the MFRR is made less favored.
	 */
	do {
		old_state = new_state = READ_ONCE(icp->state);

		/* Set_MFRR */
		new_state.mfrr = mfrr;

		/* Check_IPI */
		reject = 0;
		resend = false;
		if (mfrr < new_state.cppr) {
			/* Reject a pending interrupt if not an IPI */
			if (mfrr <= new_state.pending_pri) {
				reject = new_state.xisr;
				new_state.pending_pri = mfrr;
				new_state.xisr = XICS_IPI;
			}
		}

		if (mfrr > old_state.mfrr) {
			resend = new_state.need_resend;
			new_state.need_resend = 0;
		}
	} while (!icp_rm_try_update(icp, old_state, new_state));

	/* Handle reject in real mode */
	if (reject && reject != XICS_IPI) {
		this_icp->n_reject++;
		icp_rm_deliver_irq(xics, icp, reject, false);
	}

	/* Handle resends in real mode */
	if (resend) {
		this_icp->n_check_resend++;
		icp_rm_check_resend(xics, icp);
	}

	return check_too_hard(xics, this_icp);
}

int xics_rm_h_cppr(struct kvm_vcpu *vcpu, unsigned long cppr)
{
	union kvmppc_icp_state old_state, new_state;
	struct kvmppc_xics *xics = vcpu->kvm->arch.xics;
	struct kvmppc_icp *icp = vcpu->arch.icp;
	u32 reject;

	if (!xics || !xics->real_mode)
		return H_TOO_HARD;

	/*
	 * ICP State: Set_CPPR
	 *
	 * We can safely compare the new value with the current
	 * value outside of the transaction as the CPPR is only
	 * ever changed by the processor on itself
	 */
	if (cppr > icp->state.cppr) {
		icp_rm_down_cppr(xics, icp, cppr);
		goto bail;
	} else if (cppr == icp->state.cppr)
		return H_SUCCESS;

	/*
	 * ICP State: Up_CPPR
	 *
	 * The processor is raising its priority, this can result
	 * in a rejection of a pending interrupt:
	 *
	 * ICP State: Reject_Current
	 *
	 * We can remove EE from the current processor, the update
	 * transaction will set it again if needed
	 */
	icp_rm_clr_vcpu_irq(icp->vcpu);

	do {
		old_state = new_state = READ_ONCE(icp->state);

		reject = 0;
		new_state.cppr = cppr;

		if (cppr <= new_state.pending_pri) {
			reject = new_state.xisr;
			new_state.xisr = 0;
			new_state.pending_pri = 0xff;
		}

	} while (!icp_rm_try_update(icp, old_state, new_state));

	/*
	 * Check for rejects. They are handled by doing a new delivery
	 * attempt (see comments in icp_rm_deliver_irq).
	 */
	if (reject && reject != XICS_IPI) {
		icp->n_reject++;
		icp_rm_deliver_irq(xics, icp, reject, false);
	}
 bail:
	return check_too_hard(xics, icp);
}

static int ics_rm_eoi(struct kvm_vcpu *vcpu, u32 irq)
{
	struct kvmppc_xics *xics = vcpu->kvm->arch.xics;
	struct kvmppc_icp *icp = vcpu->arch.icp;
	struct kvmppc_ics *ics;
	struct ics_irq_state *state;
	u16 src;
	u32 pq_old, pq_new;

	/*
	 * ICS EOI handling: For LSI, if P bit is still set, we need to
	 * resend it.
	 *
	 * For MSI, we move Q bit into P (and clear Q). If it is set,
	 * resend it.
	 */

	ics = kvmppc_xics_find_ics(xics, irq, &src);
	if (!ics)
		goto bail;

	state = &ics->irq_state[src];

	if (state->lsi)
		pq_new = state->pq_state;
	else
		do {
			pq_old = state->pq_state;
			pq_new = pq_old >> 1;
		} while (cmpxchg(&state->pq_state, pq_old, pq_new) != pq_old);

	if (pq_new & PQ_PRESENTED)
		icp_rm_deliver_irq(xics, NULL, irq, false);

	if (!hlist_empty(&vcpu->kvm->irq_ack_notifier_list)) {
		icp->rm_action |= XICS_RM_NOTIFY_EOI;
		icp->rm_eoied_irq = irq;
	}

	if (state->host_irq) {
		++vcpu->stat.pthru_all;
		if (state->intr_cpu != -1) {
			int pcpu = raw_smp_processor_id();

			pcpu = cpu_first_thread_sibling(pcpu);
			++vcpu->stat.pthru_host;
			if (state->intr_cpu != pcpu) {
				++vcpu->stat.pthru_bad_aff;
				xics_opal_set_server(state->host_irq, pcpu);
			}
			state->intr_cpu = -1;
		}
	}

 bail:
	return check_too_hard(xics, icp);
}

int xics_rm_h_eoi(struct kvm_vcpu *vcpu, unsigned long xirr)
{
	struct kvmppc_xics *xics = vcpu->kvm->arch.xics;
	struct kvmppc_icp *icp = vcpu->arch.icp;
	u32 irq = xirr & 0x00ffffff;

	if (!xics || !xics->real_mode)
		return H_TOO_HARD;

	/*
	 * ICP State: EOI
	 *
	 * Note: If EOI is incorrectly used by SW to lower the CPPR
	 * value (ie more favored), we do not check for rejection of
	 * a pending interrupt, this is a SW error and PAPR specifies
	 * that we don't have to deal with it.
	 *
	 * The sending of an EOI to the ICS is handled after the
	 * CPPR update
	 *
	 * ICP State: Down_CPPR which we handle
	 * in a separate function as it's shared with H_CPPR.
	 */
	icp_rm_down_cppr(xics, icp, xirr >> 24);

	/* IPIs have no EOI */
	if (irq == XICS_IPI)
		return check_too_hard(xics, icp);

	return ics_rm_eoi(vcpu, irq);
}

unsigned long eoi_rc;

static void icp_eoi(struct irq_chip *c, u32 hwirq, __be32 xirr, bool *again)
{
	void __iomem *xics_phys;
	int64_t rc;

	rc = pnv_opal_pci_msi_eoi(c, hwirq);

	if (rc)
		eoi_rc = rc;

	iosync();

	/* EOI it */
	xics_phys = local_paca->kvm_hstate.xics_phys;
	if (xics_phys) {
		__raw_rm_writel(xirr, xics_phys + XICS_XIRR);
	} else {
		rc = opal_int_eoi(be32_to_cpu(xirr));
		*again = rc > 0;
	}
}

static int xics_opal_set_server(unsigned int hw_irq, int server_cpu)
{
	unsigned int mangle_cpu = get_hard_smp_processor_id(server_cpu) << 2;

	return opal_set_xive(hw_irq, mangle_cpu, DEFAULT_PRIORITY);
}

/*
 * Increment a per-CPU 32-bit unsigned integer variable.
 * Safe to call in real-mode. Handles vmalloc'ed addresses
 *
 * ToDo: Make this work for any integral type
 */

static inline void this_cpu_inc_rm(unsigned int __percpu *addr)
{
	unsigned long l;
	unsigned int *raddr;
	int cpu = smp_processor_id();

	raddr = per_cpu_ptr(addr, cpu);
	l = (unsigned long)raddr;

	if (REGION_ID(l) == VMALLOC_REGION_ID) {
		l = vmalloc_to_phys(raddr);
		raddr = (unsigned int *)l;
	}
	++*raddr;
}

/*
 * We don't try to update the flags in the irq_desc 'istate' field in
 * here as would happen in the normal IRQ handling path for several reasons:
 *  - state flags represent internal IRQ state and are not expected to be
 *    updated outside the IRQ subsystem
 *  - more importantly, these are useful for edge triggered interrupts,
 *    IRQ probing, etc., but we are only handling MSI/MSIx interrupts here
 *    and these states shouldn't apply to us.
 *
 * However, we do update irq_stats - we somewhat duplicate the code in
 * kstat_incr_irqs_this_cpu() for this since this function is defined
 * in irq/internal.h which we don't want to include here.
 * The only difference is that desc->kstat_irqs is an allocated per CPU
 * variable and could have been vmalloc'ed, so we can't directly
 * call __this_cpu_inc() on it. The kstat structure is a static
 * per CPU variable and it should be accessible by real-mode KVM.
 *
 */
static void kvmppc_rm_handle_irq_desc(struct irq_desc *desc)
{
	this_cpu_inc_rm(desc->kstat_irqs);
	__this_cpu_inc(kstat.irqs_sum);
}

long kvmppc_deliver_irq_passthru(struct kvm_vcpu *vcpu,
				 __be32 xirr,
				 struct kvmppc_irq_map *irq_map,
				 struct kvmppc_passthru_irqmap *pimap,
				 bool *again)
{
	struct kvmppc_xics *xics;
	struct kvmppc_icp *icp;
	struct kvmppc_ics *ics;
	struct ics_irq_state *state;
	u32 irq;
	u16 src;
	u32 pq_old, pq_new;

	irq = irq_map->v_hwirq;
	xics = vcpu->kvm->arch.xics;
	icp = vcpu->arch.icp;

	kvmppc_rm_handle_irq_desc(irq_map->desc);

	ics = kvmppc_xics_find_ics(xics, irq, &src);
	if (!ics)
		return 2;

	state = &ics->irq_state[src];

	/* only MSIs register bypass producers, so it must be MSI here */
	do {
		pq_old = state->pq_state;
		pq_new = ((pq_old << 1) & 3) | PQ_PRESENTED;
	} while (cmpxchg(&state->pq_state, pq_old, pq_new) != pq_old);

	/* Test P=1, Q=0, this is the only case where we present */
	if (pq_new == PQ_PRESENTED)
		icp_rm_deliver_irq(xics, icp, irq, false);

	/* EOI the interrupt */
	icp_eoi(irq_desc_get_chip(irq_map->desc), irq_map->r_hwirq, xirr,
		again);

	if (check_too_hard(xics, icp) == H_TOO_HARD)
		return 2;
	else
		return -2;
}

/*  --- Non-real mode XICS-related built-in routines ---  */

/**
 * Host Operations poked by RM KVM
 */
static void rm_host_ipi_action(int action, void *data)
{
	switch (action) {
	case XICS_RM_KICK_VCPU:
		kvmppc_host_rm_ops_hv->vcpu_kick(data);
		break;
	default:
		WARN(1, "Unexpected rm_action=%d data=%p\n", action, data);
		break;
	}

}

void kvmppc_xics_ipi_action(void)
{
	int core;
	unsigned int cpu = smp_processor_id();
	struct kvmppc_host_rm_core *rm_corep;

	core = cpu >> threads_shift;
	rm_corep = &kvmppc_host_rm_ops_hv->rm_core[core];

	if (rm_corep->rm_data) {
		rm_host_ipi_action(rm_corep->rm_state.rm_action,
							rm_corep->rm_data);
		/* Order these stores against the real mode KVM */
		rm_corep->rm_data = NULL;
		smp_wmb();
		rm_corep->rm_state.rm_action = 0;
	}
}
