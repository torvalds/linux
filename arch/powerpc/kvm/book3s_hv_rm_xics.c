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

#include <asm/kvm_book3s.h>
#include <asm/kvm_ppc.h>
#include <asm/hvcall.h>
#include <asm/xics.h>
#include <asm/debug.h>
#include <asm/synch.h>
#include <asm/ppc-opcode.h>

#include "book3s_xics.h"

#define DEBUG_PASSUP

static inline void rm_writeb(unsigned long paddr, u8 val)
{
	__asm__ __volatile__("sync; stbcix %0,0,%1"
		: : "r" (val), "r" (paddr) : "memory");
}

static void icp_rm_set_vcpu_irq(struct kvm_vcpu *vcpu,
				struct kvm_vcpu *this_vcpu)
{
	struct kvmppc_icp *this_icp = this_vcpu->arch.icp;
	unsigned long xics_phys;
	int cpu;

	/* Mark the target VCPU as having an interrupt pending */
	vcpu->stat.queue_intr++;
	set_bit(BOOK3S_IRQPRIO_EXTERNAL_LEVEL, &vcpu->arch.pending_exceptions);

	/* Kick self ? Just set MER and return */
	if (vcpu == this_vcpu) {
		mtspr(SPRN_LPCR, mfspr(SPRN_LPCR) | LPCR_MER);
		return;
	}

	/* Check if the core is loaded, if not, too hard */
	cpu = vcpu->cpu;
	if (cpu < 0 || cpu >= nr_cpu_ids) {
		this_icp->rm_action |= XICS_RM_KICK_VCPU;
		this_icp->rm_kick_target = vcpu;
		return;
	}
	/* In SMT cpu will always point to thread 0, we adjust it */
	cpu += vcpu->arch.ptid;

	/* Not too hard, then poke the target */
	xics_phys = paca[cpu].kvm_hstate.xics_phys;
	rm_writeb(xics_phys + XICS_MFRR, IPI_PRIORITY);
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
		old_state = new_state = ACCESS_ONCE(icp->state);

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
	if (resend)
		icp->rm_action |= XICS_RM_CHECK_RESEND;
}


unsigned long kvmppc_rm_h_xirr(struct kvm_vcpu *vcpu)
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
		old_state = new_state = ACCESS_ONCE(icp->state);

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

int kvmppc_rm_h_ipi(struct kvm_vcpu *vcpu, unsigned long server,
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
	 * If the CPPR is less favored, then we might be replacing
	 * an interrupt, and thus need to possibly reject it as in
	 *
	 * ICP state: Check_IPI
	 */
	do {
		old_state = new_state = ACCESS_ONCE(icp->state);

		/* Set_MFRR */
		new_state.mfrr = mfrr;

		/* Check_IPI */
		reject = 0;
		resend = false;
		if (mfrr < new_state.cppr) {
			/* Reject a pending interrupt if not an IPI */
			if (mfrr <= new_state.pending_pri)
				reject = new_state.xisr;
			new_state.pending_pri = mfrr;
			new_state.xisr = XICS_IPI;
		}

		if (mfrr > old_state.mfrr && mfrr > new_state.cppr) {
			resend = new_state.need_resend;
			new_state.need_resend = 0;
		}
	} while (!icp_rm_try_update(icp, old_state, new_state));

	/* Pass rejects to virtual mode */
	if (reject && reject != XICS_IPI) {
		this_icp->rm_action |= XICS_RM_REJECT;
		this_icp->rm_reject = reject;
	}

	/* Pass resends to virtual mode */
	if (resend)
		this_icp->rm_action |= XICS_RM_CHECK_RESEND;

	return check_too_hard(xics, this_icp);
}

int kvmppc_rm_h_cppr(struct kvm_vcpu *vcpu, unsigned long cppr)
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
		old_state = new_state = ACCESS_ONCE(icp->state);

		reject = 0;
		new_state.cppr = cppr;

		if (cppr <= new_state.pending_pri) {
			reject = new_state.xisr;
			new_state.xisr = 0;
			new_state.pending_pri = 0xff;
		}

	} while (!icp_rm_try_update(icp, old_state, new_state));

	/* Pass rejects to virtual mode */
	if (reject && reject != XICS_IPI) {
		icp->rm_action |= XICS_RM_REJECT;
		icp->rm_reject = reject;
	}
 bail:
	return check_too_hard(xics, icp);
}

int kvmppc_rm_h_eoi(struct kvm_vcpu *vcpu, unsigned long xirr)
{
	struct kvmppc_xics *xics = vcpu->kvm->arch.xics;
	struct kvmppc_icp *icp = vcpu->arch.icp;
	struct kvmppc_ics *ics;
	struct ics_irq_state *state;
	u32 irq = xirr & 0x00ffffff;
	u16 src;

	if (!xics || !xics->real_mode)
		return H_TOO_HARD;

	/*
	 * ICP State: EOI
	 *
	 * Note: If EOI is incorrectly used by SW to lower the CPPR
	 * value (ie more favored), we do not check for rejection of
	 * a pending interrupt, this is a SW error and PAPR sepcifies
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
		goto bail;
	/*
	 * EOI handling: If the interrupt is still asserted, we need to
	 * resend it. We can take a lockless "peek" at the ICS state here.
	 *
	 * "Message" interrupts will never have "asserted" set
	 */
	ics = kvmppc_xics_find_ics(xics, irq, &src);
	if (!ics)
		goto bail;
	state = &ics->irq_state[src];

	/* Still asserted, resend it, we make it look like a reject */
	if (state->asserted) {
		icp->rm_action |= XICS_RM_REJECT;
		icp->rm_reject = irq;
	}
 bail:
	return check_too_hard(xics, icp);
}
