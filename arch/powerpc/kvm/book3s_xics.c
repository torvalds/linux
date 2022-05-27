// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2012 Michael Ellerman, IBM Corporation.
 * Copyright 2012 Benjamin Herrenschmidt, IBM Corporation.
 */

#include <linux/kernel.h>
#include <linux/kvm_host.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/anon_inodes.h>
#include <linux/spinlock.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>

#include <asm/kvm_book3s.h>
#include <asm/kvm_ppc.h>
#include <asm/hvcall.h>
#include <asm/xics.h>
#include <asm/time.h>

#include <linux/seq_file.h>

#include "book3s_xics.h"

#if 1
#define XICS_DBG(fmt...) do { } while (0)
#else
#define XICS_DBG(fmt...) trace_printk(fmt)
#endif

#define ENABLE_REALMODE	true
#define DEBUG_REALMODE	false

/*
 * LOCKING
 * =======
 *
 * Each ICS has a spin lock protecting the information about the IRQ
 * sources and avoiding simultaneous deliveries of the same interrupt.
 *
 * ICP operations are done via a single compare & swap transaction
 * (most ICP state fits in the union kvmppc_icp_state)
 */

/*
 * TODO
 * ====
 *
 * - To speed up resends, keep a bitmap of "resend" set bits in the
 *   ICS
 *
 * - Speed up server# -> ICP lookup (array ? hash table ?)
 *
 * - Make ICS lockless as well, or at least a per-interrupt lock or hashed
 *   locks array to improve scalability
 */

/* -- ICS routines -- */

static void icp_deliver_irq(struct kvmppc_xics *xics, struct kvmppc_icp *icp,
			    u32 new_irq, bool check_resend);

/*
 * Return value ideally indicates how the interrupt was handled, but no
 * callers look at it (given that we don't implement KVM_IRQ_LINE_STATUS),
 * so just return 0.
 */
static int ics_deliver_irq(struct kvmppc_xics *xics, u32 irq, u32 level)
{
	struct ics_irq_state *state;
	struct kvmppc_ics *ics;
	u16 src;
	u32 pq_old, pq_new;

	XICS_DBG("ics deliver %#x (level: %d)\n", irq, level);

	ics = kvmppc_xics_find_ics(xics, irq, &src);
	if (!ics) {
		XICS_DBG("ics_deliver_irq: IRQ 0x%06x not found !\n", irq);
		return -EINVAL;
	}
	state = &ics->irq_state[src];
	if (!state->exists)
		return -EINVAL;

	if (level == KVM_INTERRUPT_SET_LEVEL || level == KVM_INTERRUPT_SET)
		level = 1;
	else if (level == KVM_INTERRUPT_UNSET)
		level = 0;
	/*
	 * Take other values the same as 1, consistent with original code.
	 * maybe WARN here?
	 */

	if (!state->lsi && level == 0) /* noop for MSI */
		return 0;

	do {
		pq_old = state->pq_state;
		if (state->lsi) {
			if (level) {
				if (pq_old & PQ_PRESENTED)
					/* Setting already set LSI ... */
					return 0;

				pq_new = PQ_PRESENTED;
			} else
				pq_new = 0;
		} else
			pq_new = ((pq_old << 1) & 3) | PQ_PRESENTED;
	} while (cmpxchg(&state->pq_state, pq_old, pq_new) != pq_old);

	/* Test P=1, Q=0, this is the only case where we present */
	if (pq_new == PQ_PRESENTED)
		icp_deliver_irq(xics, NULL, irq, false);

	/* Record which CPU this arrived on for passed-through interrupts */
	if (state->host_irq)
		state->intr_cpu = raw_smp_processor_id();

	return 0;
}

static void ics_check_resend(struct kvmppc_xics *xics, struct kvmppc_ics *ics,
			     struct kvmppc_icp *icp)
{
	int i;

	for (i = 0; i < KVMPPC_XICS_IRQ_PER_ICS; i++) {
		struct ics_irq_state *state = &ics->irq_state[i];
		if (state->resend) {
			XICS_DBG("resend %#x prio %#x\n", state->number,
				      state->priority);
			icp_deliver_irq(xics, icp, state->number, true);
		}
	}
}

static bool write_xive(struct kvmppc_xics *xics, struct kvmppc_ics *ics,
		       struct ics_irq_state *state,
		       u32 server, u32 priority, u32 saved_priority)
{
	bool deliver;
	unsigned long flags;

	local_irq_save(flags);
	arch_spin_lock(&ics->lock);

	state->server = server;
	state->priority = priority;
	state->saved_priority = saved_priority;
	deliver = false;
	if ((state->masked_pending || state->resend) && priority != MASKED) {
		state->masked_pending = 0;
		state->resend = 0;
		deliver = true;
	}

	arch_spin_unlock(&ics->lock);
	local_irq_restore(flags);

	return deliver;
}

int kvmppc_xics_set_xive(struct kvm *kvm, u32 irq, u32 server, u32 priority)
{
	struct kvmppc_xics *xics = kvm->arch.xics;
	struct kvmppc_icp *icp;
	struct kvmppc_ics *ics;
	struct ics_irq_state *state;
	u16 src;

	if (!xics)
		return -ENODEV;

	ics = kvmppc_xics_find_ics(xics, irq, &src);
	if (!ics)
		return -EINVAL;
	state = &ics->irq_state[src];

	icp = kvmppc_xics_find_server(kvm, server);
	if (!icp)
		return -EINVAL;

	XICS_DBG("set_xive %#x server %#x prio %#x MP:%d RS:%d\n",
		 irq, server, priority,
		 state->masked_pending, state->resend);

	if (write_xive(xics, ics, state, server, priority, priority))
		icp_deliver_irq(xics, icp, irq, false);

	return 0;
}

int kvmppc_xics_get_xive(struct kvm *kvm, u32 irq, u32 *server, u32 *priority)
{
	struct kvmppc_xics *xics = kvm->arch.xics;
	struct kvmppc_ics *ics;
	struct ics_irq_state *state;
	u16 src;
	unsigned long flags;

	if (!xics)
		return -ENODEV;

	ics = kvmppc_xics_find_ics(xics, irq, &src);
	if (!ics)
		return -EINVAL;
	state = &ics->irq_state[src];

	local_irq_save(flags);
	arch_spin_lock(&ics->lock);
	*server = state->server;
	*priority = state->priority;
	arch_spin_unlock(&ics->lock);
	local_irq_restore(flags);

	return 0;
}

int kvmppc_xics_int_on(struct kvm *kvm, u32 irq)
{
	struct kvmppc_xics *xics = kvm->arch.xics;
	struct kvmppc_icp *icp;
	struct kvmppc_ics *ics;
	struct ics_irq_state *state;
	u16 src;

	if (!xics)
		return -ENODEV;

	ics = kvmppc_xics_find_ics(xics, irq, &src);
	if (!ics)
		return -EINVAL;
	state = &ics->irq_state[src];

	icp = kvmppc_xics_find_server(kvm, state->server);
	if (!icp)
		return -EINVAL;

	if (write_xive(xics, ics, state, state->server, state->saved_priority,
		       state->saved_priority))
		icp_deliver_irq(xics, icp, irq, false);

	return 0;
}

int kvmppc_xics_int_off(struct kvm *kvm, u32 irq)
{
	struct kvmppc_xics *xics = kvm->arch.xics;
	struct kvmppc_ics *ics;
	struct ics_irq_state *state;
	u16 src;

	if (!xics)
		return -ENODEV;

	ics = kvmppc_xics_find_ics(xics, irq, &src);
	if (!ics)
		return -EINVAL;
	state = &ics->irq_state[src];

	write_xive(xics, ics, state, state->server, MASKED, state->priority);

	return 0;
}

/* -- ICP routines, including hcalls -- */

static inline bool icp_try_update(struct kvmppc_icp *icp,
				  union kvmppc_icp_state old,
				  union kvmppc_icp_state new,
				  bool change_self)
{
	bool success;

	/* Calculate new output value */
	new.out_ee = (new.xisr && (new.pending_pri < new.cppr));

	/* Attempt atomic update */
	success = cmpxchg64(&icp->state.raw, old.raw, new.raw) == old.raw;
	if (!success)
		goto bail;

	XICS_DBG("UPD [%04lx] - C:%02x M:%02x PP: %02x PI:%06x R:%d O:%d\n",
		 icp->server_num,
		 old.cppr, old.mfrr, old.pending_pri, old.xisr,
		 old.need_resend, old.out_ee);
	XICS_DBG("UPD        - C:%02x M:%02x PP: %02x PI:%06x R:%d O:%d\n",
		 new.cppr, new.mfrr, new.pending_pri, new.xisr,
		 new.need_resend, new.out_ee);
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
	if (new.out_ee) {
		kvmppc_book3s_queue_irqprio(icp->vcpu,
					    BOOK3S_INTERRUPT_EXTERNAL);
		if (!change_self)
			kvmppc_fast_vcpu_kick(icp->vcpu);
	}
 bail:
	return success;
}

static void icp_check_resend(struct kvmppc_xics *xics,
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
		ics_check_resend(xics, ics, icp);
	}
}

static bool icp_try_to_deliver(struct kvmppc_icp *icp, u32 irq, u8 priority,
			       u32 *reject)
{
	union kvmppc_icp_state old_state, new_state;
	bool success;

	XICS_DBG("try deliver %#x(P:%#x) to server %#lx\n", irq, priority,
		 icp->server_num);

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

	} while (!icp_try_update(icp, old_state, new_state, false));

	return success;
}

static void icp_deliver_irq(struct kvmppc_xics *xics, struct kvmppc_icp *icp,
			    u32 new_irq, bool check_resend)
{
	struct ics_irq_state *state;
	struct kvmppc_ics *ics;
	u32 reject;
	u16 src;
	unsigned long flags;

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
		XICS_DBG("icp_deliver_irq: IRQ 0x%06x not found !\n", new_irq);
		return;
	}
	state = &ics->irq_state[src];

	/* Get a lock on the ICS */
	local_irq_save(flags);
	arch_spin_lock(&ics->lock);

	/* Get our server */
	if (!icp || state->server != icp->server_num) {
		icp = kvmppc_xics_find_server(xics->kvm, state->server);
		if (!icp) {
			pr_warn("icp_deliver_irq: IRQ 0x%06x server 0x%x not found !\n",
				new_irq, state->server);
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
		XICS_DBG("irq %#x masked pending\n", new_irq);
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
	 * because by the time we exit icp_try_to_deliver() the target
	 * processor may well have alrady consumed & completed it, and thus
	 * the rejected interrupt might actually be already acceptable.
	 */
	if (icp_try_to_deliver(icp, new_irq, state->priority, &reject)) {
		/*
		 * Delivery was successful, did we reject somebody else ?
		 */
		if (reject && reject != XICS_IPI) {
			arch_spin_unlock(&ics->lock);
			local_irq_restore(flags);
			new_irq = reject;
			check_resend = false;
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
		 * between icp_try_to_deliver() atomic update and now, then
		 * we know it might have missed the resend_map bit. So we
		 * retry
		 */
		smp_mb();
		if (!icp->state.need_resend) {
			state->resend = 0;
			arch_spin_unlock(&ics->lock);
			local_irq_restore(flags);
			check_resend = false;
			goto again;
		}
	}
 out:
	arch_spin_unlock(&ics->lock);
	local_irq_restore(flags);
}

static void icp_down_cppr(struct kvmppc_xics *xics, struct kvmppc_icp *icp,
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
			WARN_ON(new_state.xisr != XICS_IPI &&
				new_state.xisr != 0);
			new_state.pending_pri = new_state.mfrr;
			new_state.xisr = XICS_IPI;
		}

		/* Latch/clear resend bit */
		resend = new_state.need_resend;
		new_state.need_resend = 0;

	} while (!icp_try_update(icp, old_state, new_state, true));

	/*
	 * Now handle resend checks. Those are asynchronous to the ICP
	 * state update in HW (ie bus transactions) so we can handle them
	 * separately here too
	 */
	if (resend)
		icp_check_resend(xics, icp);
}

static noinline unsigned long kvmppc_h_xirr(struct kvm_vcpu *vcpu)
{
	union kvmppc_icp_state old_state, new_state;
	struct kvmppc_icp *icp = vcpu->arch.icp;
	u32 xirr;

	/* First, remove EE from the processor */
	kvmppc_book3s_dequeue_irqprio(icp->vcpu, BOOK3S_INTERRUPT_EXTERNAL);

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

	} while (!icp_try_update(icp, old_state, new_state, true));

	XICS_DBG("h_xirr vcpu %d xirr %#x\n", vcpu->vcpu_id, xirr);

	return xirr;
}

static noinline int kvmppc_h_ipi(struct kvm_vcpu *vcpu, unsigned long server,
				 unsigned long mfrr)
{
	union kvmppc_icp_state old_state, new_state;
	struct kvmppc_xics *xics = vcpu->kvm->arch.xics;
	struct kvmppc_icp *icp;
	u32 reject;
	bool resend;
	bool local;

	XICS_DBG("h_ipi vcpu %d to server %lu mfrr %#lx\n",
		 vcpu->vcpu_id, server, mfrr);

	icp = vcpu->arch.icp;
	local = icp->server_num == server;
	if (!local) {
		icp = kvmppc_xics_find_server(vcpu->kvm, server);
		if (!icp)
			return H_PARAMETER;
	}

	/*
	 * ICP state: Set_MFRR
	 *
	 * If the CPPR is more favored than the new MFRR, then
	 * nothing needs to be rejected as there can be no XISR to
	 * reject.  If the MFRR is being made less favored then
	 * there might be a previously-rejected interrupt needing
	 * to be resent.
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
	} while (!icp_try_update(icp, old_state, new_state, local));

	/* Handle reject */
	if (reject && reject != XICS_IPI)
		icp_deliver_irq(xics, icp, reject, false);

	/* Handle resend */
	if (resend)
		icp_check_resend(xics, icp);

	return H_SUCCESS;
}

static int kvmppc_h_ipoll(struct kvm_vcpu *vcpu, unsigned long server)
{
	union kvmppc_icp_state state;
	struct kvmppc_icp *icp;

	icp = vcpu->arch.icp;
	if (icp->server_num != server) {
		icp = kvmppc_xics_find_server(vcpu->kvm, server);
		if (!icp)
			return H_PARAMETER;
	}
	state = READ_ONCE(icp->state);
	kvmppc_set_gpr(vcpu, 4, ((u32)state.cppr << 24) | state.xisr);
	kvmppc_set_gpr(vcpu, 5, state.mfrr);
	return H_SUCCESS;
}

static noinline void kvmppc_h_cppr(struct kvm_vcpu *vcpu, unsigned long cppr)
{
	union kvmppc_icp_state old_state, new_state;
	struct kvmppc_xics *xics = vcpu->kvm->arch.xics;
	struct kvmppc_icp *icp = vcpu->arch.icp;
	u32 reject;

	XICS_DBG("h_cppr vcpu %d cppr %#lx\n", vcpu->vcpu_id, cppr);

	/*
	 * ICP State: Set_CPPR
	 *
	 * We can safely compare the new value with the current
	 * value outside of the transaction as the CPPR is only
	 * ever changed by the processor on itself
	 */
	if (cppr > icp->state.cppr)
		icp_down_cppr(xics, icp, cppr);
	else if (cppr == icp->state.cppr)
		return;

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
	kvmppc_book3s_dequeue_irqprio(icp->vcpu, BOOK3S_INTERRUPT_EXTERNAL);

	do {
		old_state = new_state = READ_ONCE(icp->state);

		reject = 0;
		new_state.cppr = cppr;

		if (cppr <= new_state.pending_pri) {
			reject = new_state.xisr;
			new_state.xisr = 0;
			new_state.pending_pri = 0xff;
		}

	} while (!icp_try_update(icp, old_state, new_state, true));

	/*
	 * Check for rejects. They are handled by doing a new delivery
	 * attempt (see comments in icp_deliver_irq).
	 */
	if (reject && reject != XICS_IPI)
		icp_deliver_irq(xics, icp, reject, false);
}

static int ics_eoi(struct kvm_vcpu *vcpu, u32 irq)
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
	if (!ics) {
		XICS_DBG("ios_eoi: IRQ 0x%06x not found !\n", irq);
		return H_PARAMETER;
	}
	state = &ics->irq_state[src];

	if (state->lsi)
		pq_new = state->pq_state;
	else
		do {
			pq_old = state->pq_state;
			pq_new = pq_old >> 1;
		} while (cmpxchg(&state->pq_state, pq_old, pq_new) != pq_old);

	if (pq_new & PQ_PRESENTED)
		icp_deliver_irq(xics, icp, irq, false);

	kvm_notify_acked_irq(vcpu->kvm, 0, irq);

	return H_SUCCESS;
}

static noinline int kvmppc_h_eoi(struct kvm_vcpu *vcpu, unsigned long xirr)
{
	struct kvmppc_xics *xics = vcpu->kvm->arch.xics;
	struct kvmppc_icp *icp = vcpu->arch.icp;
	u32 irq = xirr & 0x00ffffff;

	XICS_DBG("h_eoi vcpu %d eoi %#lx\n", vcpu->vcpu_id, xirr);

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
	icp_down_cppr(xics, icp, xirr >> 24);

	/* IPIs have no EOI */
	if (irq == XICS_IPI)
		return H_SUCCESS;

	return ics_eoi(vcpu, irq);
}

int kvmppc_xics_rm_complete(struct kvm_vcpu *vcpu, u32 hcall)
{
	struct kvmppc_xics *xics = vcpu->kvm->arch.xics;
	struct kvmppc_icp *icp = vcpu->arch.icp;

	XICS_DBG("XICS_RM: H_%x completing, act: %x state: %lx tgt: %p\n",
		 hcall, icp->rm_action, icp->rm_dbgstate.raw, icp->rm_dbgtgt);

	if (icp->rm_action & XICS_RM_KICK_VCPU) {
		icp->n_rm_kick_vcpu++;
		kvmppc_fast_vcpu_kick(icp->rm_kick_target);
	}
	if (icp->rm_action & XICS_RM_CHECK_RESEND) {
		icp->n_rm_check_resend++;
		icp_check_resend(xics, icp->rm_resend_icp);
	}
	if (icp->rm_action & XICS_RM_NOTIFY_EOI) {
		icp->n_rm_notify_eoi++;
		kvm_notify_acked_irq(vcpu->kvm, 0, icp->rm_eoied_irq);
	}

	icp->rm_action = 0;

	return H_SUCCESS;
}
EXPORT_SYMBOL_GPL(kvmppc_xics_rm_complete);

int kvmppc_xics_hcall(struct kvm_vcpu *vcpu, u32 req)
{
	struct kvmppc_xics *xics = vcpu->kvm->arch.xics;
	unsigned long res;
	int rc = H_SUCCESS;

	/* Check if we have an ICP */
	if (!xics || !vcpu->arch.icp)
		return H_HARDWARE;

	/* These requests don't have real-mode implementations at present */
	switch (req) {
	case H_XIRR_X:
		res = kvmppc_h_xirr(vcpu);
		kvmppc_set_gpr(vcpu, 4, res);
		kvmppc_set_gpr(vcpu, 5, get_tb());
		return rc;
	case H_IPOLL:
		rc = kvmppc_h_ipoll(vcpu, kvmppc_get_gpr(vcpu, 4));
		return rc;
	}

	/* Check for real mode returning too hard */
	if (xics->real_mode && is_kvmppc_hv_enabled(vcpu->kvm))
		return kvmppc_xics_rm_complete(vcpu, req);

	switch (req) {
	case H_XIRR:
		res = kvmppc_h_xirr(vcpu);
		kvmppc_set_gpr(vcpu, 4, res);
		break;
	case H_CPPR:
		kvmppc_h_cppr(vcpu, kvmppc_get_gpr(vcpu, 4));
		break;
	case H_EOI:
		rc = kvmppc_h_eoi(vcpu, kvmppc_get_gpr(vcpu, 4));
		break;
	case H_IPI:
		rc = kvmppc_h_ipi(vcpu, kvmppc_get_gpr(vcpu, 4),
				  kvmppc_get_gpr(vcpu, 5));
		break;
	}

	return rc;
}
EXPORT_SYMBOL_GPL(kvmppc_xics_hcall);


/* -- Initialisation code etc. -- */

static void xics_debugfs_irqmap(struct seq_file *m,
				struct kvmppc_passthru_irqmap *pimap)
{
	int i;

	if (!pimap)
		return;
	seq_printf(m, "========\nPIRQ mappings: %d maps\n===========\n",
				pimap->n_mapped);
	for (i = 0; i < pimap->n_mapped; i++)  {
		seq_printf(m, "r_hwirq=%x, v_hwirq=%x\n",
			pimap->mapped[i].r_hwirq, pimap->mapped[i].v_hwirq);
	}
}

static int xics_debug_show(struct seq_file *m, void *private)
{
	struct kvmppc_xics *xics = m->private;
	struct kvm *kvm = xics->kvm;
	struct kvm_vcpu *vcpu;
	int icsid;
	unsigned long flags, i;
	unsigned long t_rm_kick_vcpu, t_rm_check_resend;
	unsigned long t_rm_notify_eoi;
	unsigned long t_reject, t_check_resend;

	if (!kvm)
		return 0;

	t_rm_kick_vcpu = 0;
	t_rm_notify_eoi = 0;
	t_rm_check_resend = 0;
	t_check_resend = 0;
	t_reject = 0;

	xics_debugfs_irqmap(m, kvm->arch.pimap);

	seq_printf(m, "=========\nICP state\n=========\n");

	kvm_for_each_vcpu(i, vcpu, kvm) {
		struct kvmppc_icp *icp = vcpu->arch.icp;
		union kvmppc_icp_state state;

		if (!icp)
			continue;

		state.raw = READ_ONCE(icp->state.raw);
		seq_printf(m, "cpu server %#lx XIRR:%#x PPRI:%#x CPPR:%#x MFRR:%#x OUT:%d NR:%d\n",
			   icp->server_num, state.xisr,
			   state.pending_pri, state.cppr, state.mfrr,
			   state.out_ee, state.need_resend);
		t_rm_kick_vcpu += icp->n_rm_kick_vcpu;
		t_rm_notify_eoi += icp->n_rm_notify_eoi;
		t_rm_check_resend += icp->n_rm_check_resend;
		t_check_resend += icp->n_check_resend;
		t_reject += icp->n_reject;
	}

	seq_printf(m, "ICP Guest->Host totals: kick_vcpu=%lu check_resend=%lu notify_eoi=%lu\n",
			t_rm_kick_vcpu, t_rm_check_resend,
			t_rm_notify_eoi);
	seq_printf(m, "ICP Real Mode totals: check_resend=%lu resend=%lu\n",
			t_check_resend, t_reject);
	for (icsid = 0; icsid <= KVMPPC_XICS_MAX_ICS_ID; icsid++) {
		struct kvmppc_ics *ics = xics->ics[icsid];

		if (!ics)
			continue;

		seq_printf(m, "=========\nICS state for ICS 0x%x\n=========\n",
			   icsid);

		local_irq_save(flags);
		arch_spin_lock(&ics->lock);

		for (i = 0; i < KVMPPC_XICS_IRQ_PER_ICS; i++) {
			struct ics_irq_state *irq = &ics->irq_state[i];

			seq_printf(m, "irq 0x%06x: server %#x prio %#x save prio %#x pq_state %d resend %d masked pending %d\n",
				   irq->number, irq->server, irq->priority,
				   irq->saved_priority, irq->pq_state,
				   irq->resend, irq->masked_pending);

		}
		arch_spin_unlock(&ics->lock);
		local_irq_restore(flags);
	}
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(xics_debug);

static void xics_debugfs_init(struct kvmppc_xics *xics)
{
	xics->dentry = debugfs_create_file("xics", 0444, xics->kvm->debugfs_dentry,
					   xics, &xics_debug_fops);

	pr_debug("%s: created\n", __func__);
}

static struct kvmppc_ics *kvmppc_xics_create_ics(struct kvm *kvm,
					struct kvmppc_xics *xics, int irq)
{
	struct kvmppc_ics *ics;
	int i, icsid;

	icsid = irq >> KVMPPC_XICS_ICS_SHIFT;

	mutex_lock(&kvm->lock);

	/* ICS already exists - somebody else got here first */
	if (xics->ics[icsid])
		goto out;

	/* Create the ICS */
	ics = kzalloc(sizeof(struct kvmppc_ics), GFP_KERNEL);
	if (!ics)
		goto out;

	ics->icsid = icsid;

	for (i = 0; i < KVMPPC_XICS_IRQ_PER_ICS; i++) {
		ics->irq_state[i].number = (icsid << KVMPPC_XICS_ICS_SHIFT) | i;
		ics->irq_state[i].priority = MASKED;
		ics->irq_state[i].saved_priority = MASKED;
	}
	smp_wmb();
	xics->ics[icsid] = ics;

	if (icsid > xics->max_icsid)
		xics->max_icsid = icsid;

 out:
	mutex_unlock(&kvm->lock);
	return xics->ics[icsid];
}

static int kvmppc_xics_create_icp(struct kvm_vcpu *vcpu, unsigned long server_num)
{
	struct kvmppc_icp *icp;

	if (!vcpu->kvm->arch.xics)
		return -ENODEV;

	if (kvmppc_xics_find_server(vcpu->kvm, server_num))
		return -EEXIST;

	icp = kzalloc(sizeof(struct kvmppc_icp), GFP_KERNEL);
	if (!icp)
		return -ENOMEM;

	icp->vcpu = vcpu;
	icp->server_num = server_num;
	icp->state.mfrr = MASKED;
	icp->state.pending_pri = MASKED;
	vcpu->arch.icp = icp;

	XICS_DBG("created server for vcpu %d\n", vcpu->vcpu_id);

	return 0;
}

u64 kvmppc_xics_get_icp(struct kvm_vcpu *vcpu)
{
	struct kvmppc_icp *icp = vcpu->arch.icp;
	union kvmppc_icp_state state;

	if (!icp)
		return 0;
	state = icp->state;
	return ((u64)state.cppr << KVM_REG_PPC_ICP_CPPR_SHIFT) |
		((u64)state.xisr << KVM_REG_PPC_ICP_XISR_SHIFT) |
		((u64)state.mfrr << KVM_REG_PPC_ICP_MFRR_SHIFT) |
		((u64)state.pending_pri << KVM_REG_PPC_ICP_PPRI_SHIFT);
}

int kvmppc_xics_set_icp(struct kvm_vcpu *vcpu, u64 icpval)
{
	struct kvmppc_icp *icp = vcpu->arch.icp;
	struct kvmppc_xics *xics = vcpu->kvm->arch.xics;
	union kvmppc_icp_state old_state, new_state;
	struct kvmppc_ics *ics;
	u8 cppr, mfrr, pending_pri;
	u32 xisr;
	u16 src;
	bool resend;

	if (!icp || !xics)
		return -ENOENT;

	cppr = icpval >> KVM_REG_PPC_ICP_CPPR_SHIFT;
	xisr = (icpval >> KVM_REG_PPC_ICP_XISR_SHIFT) &
		KVM_REG_PPC_ICP_XISR_MASK;
	mfrr = icpval >> KVM_REG_PPC_ICP_MFRR_SHIFT;
	pending_pri = icpval >> KVM_REG_PPC_ICP_PPRI_SHIFT;

	/* Require the new state to be internally consistent */
	if (xisr == 0) {
		if (pending_pri != 0xff)
			return -EINVAL;
	} else if (xisr == XICS_IPI) {
		if (pending_pri != mfrr || pending_pri >= cppr)
			return -EINVAL;
	} else {
		if (pending_pri >= mfrr || pending_pri >= cppr)
			return -EINVAL;
		ics = kvmppc_xics_find_ics(xics, xisr, &src);
		if (!ics)
			return -EINVAL;
	}

	new_state.raw = 0;
	new_state.cppr = cppr;
	new_state.xisr = xisr;
	new_state.mfrr = mfrr;
	new_state.pending_pri = pending_pri;

	/*
	 * Deassert the CPU interrupt request.
	 * icp_try_update will reassert it if necessary.
	 */
	kvmppc_book3s_dequeue_irqprio(icp->vcpu, BOOK3S_INTERRUPT_EXTERNAL);

	/*
	 * Note that if we displace an interrupt from old_state.xisr,
	 * we don't mark it as rejected.  We expect userspace to set
	 * the state of the interrupt sources to be consistent with
	 * the ICP states (either before or afterwards, which doesn't
	 * matter).  We do handle resends due to CPPR becoming less
	 * favoured because that is necessary to end up with a
	 * consistent state in the situation where userspace restores
	 * the ICS states before the ICP states.
	 */
	do {
		old_state = READ_ONCE(icp->state);

		if (new_state.mfrr <= old_state.mfrr) {
			resend = false;
			new_state.need_resend = old_state.need_resend;
		} else {
			resend = old_state.need_resend;
			new_state.need_resend = 0;
		}
	} while (!icp_try_update(icp, old_state, new_state, false));

	if (resend)
		icp_check_resend(xics, icp);

	return 0;
}

static int xics_get_source(struct kvmppc_xics *xics, long irq, u64 addr)
{
	int ret;
	struct kvmppc_ics *ics;
	struct ics_irq_state *irqp;
	u64 __user *ubufp = (u64 __user *) addr;
	u16 idx;
	u64 val, prio;
	unsigned long flags;

	ics = kvmppc_xics_find_ics(xics, irq, &idx);
	if (!ics)
		return -ENOENT;

	irqp = &ics->irq_state[idx];
	local_irq_save(flags);
	arch_spin_lock(&ics->lock);
	ret = -ENOENT;
	if (irqp->exists) {
		val = irqp->server;
		prio = irqp->priority;
		if (prio == MASKED) {
			val |= KVM_XICS_MASKED;
			prio = irqp->saved_priority;
		}
		val |= prio << KVM_XICS_PRIORITY_SHIFT;
		if (irqp->lsi) {
			val |= KVM_XICS_LEVEL_SENSITIVE;
			if (irqp->pq_state & PQ_PRESENTED)
				val |= KVM_XICS_PENDING;
		} else if (irqp->masked_pending || irqp->resend)
			val |= KVM_XICS_PENDING;

		if (irqp->pq_state & PQ_PRESENTED)
			val |= KVM_XICS_PRESENTED;

		if (irqp->pq_state & PQ_QUEUED)
			val |= KVM_XICS_QUEUED;

		ret = 0;
	}
	arch_spin_unlock(&ics->lock);
	local_irq_restore(flags);

	if (!ret && put_user(val, ubufp))
		ret = -EFAULT;

	return ret;
}

static int xics_set_source(struct kvmppc_xics *xics, long irq, u64 addr)
{
	struct kvmppc_ics *ics;
	struct ics_irq_state *irqp;
	u64 __user *ubufp = (u64 __user *) addr;
	u16 idx;
	u64 val;
	u8 prio;
	u32 server;
	unsigned long flags;

	if (irq < KVMPPC_XICS_FIRST_IRQ || irq >= KVMPPC_XICS_NR_IRQS)
		return -ENOENT;

	ics = kvmppc_xics_find_ics(xics, irq, &idx);
	if (!ics) {
		ics = kvmppc_xics_create_ics(xics->kvm, xics, irq);
		if (!ics)
			return -ENOMEM;
	}
	irqp = &ics->irq_state[idx];
	if (get_user(val, ubufp))
		return -EFAULT;

	server = val & KVM_XICS_DESTINATION_MASK;
	prio = val >> KVM_XICS_PRIORITY_SHIFT;
	if (prio != MASKED &&
	    kvmppc_xics_find_server(xics->kvm, server) == NULL)
		return -EINVAL;

	local_irq_save(flags);
	arch_spin_lock(&ics->lock);
	irqp->server = server;
	irqp->saved_priority = prio;
	if (val & KVM_XICS_MASKED)
		prio = MASKED;
	irqp->priority = prio;
	irqp->resend = 0;
	irqp->masked_pending = 0;
	irqp->lsi = 0;
	irqp->pq_state = 0;
	if (val & KVM_XICS_LEVEL_SENSITIVE)
		irqp->lsi = 1;
	/* If PENDING, set P in case P is not saved because of old code */
	if (val & KVM_XICS_PRESENTED || val & KVM_XICS_PENDING)
		irqp->pq_state |= PQ_PRESENTED;
	if (val & KVM_XICS_QUEUED)
		irqp->pq_state |= PQ_QUEUED;
	irqp->exists = 1;
	arch_spin_unlock(&ics->lock);
	local_irq_restore(flags);

	if (val & KVM_XICS_PENDING)
		icp_deliver_irq(xics, NULL, irqp->number, false);

	return 0;
}

int kvmppc_xics_set_irq(struct kvm *kvm, int irq_source_id, u32 irq, int level,
			bool line_status)
{
	struct kvmppc_xics *xics = kvm->arch.xics;

	if (!xics)
		return -ENODEV;
	return ics_deliver_irq(xics, irq, level);
}

static int xics_set_attr(struct kvm_device *dev, struct kvm_device_attr *attr)
{
	struct kvmppc_xics *xics = dev->private;

	switch (attr->group) {
	case KVM_DEV_XICS_GRP_SOURCES:
		return xics_set_source(xics, attr->attr, attr->addr);
	}
	return -ENXIO;
}

static int xics_get_attr(struct kvm_device *dev, struct kvm_device_attr *attr)
{
	struct kvmppc_xics *xics = dev->private;

	switch (attr->group) {
	case KVM_DEV_XICS_GRP_SOURCES:
		return xics_get_source(xics, attr->attr, attr->addr);
	}
	return -ENXIO;
}

static int xics_has_attr(struct kvm_device *dev, struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_XICS_GRP_SOURCES:
		if (attr->attr >= KVMPPC_XICS_FIRST_IRQ &&
		    attr->attr < KVMPPC_XICS_NR_IRQS)
			return 0;
		break;
	}
	return -ENXIO;
}

/*
 * Called when device fd is closed. kvm->lock is held.
 */
static void kvmppc_xics_release(struct kvm_device *dev)
{
	struct kvmppc_xics *xics = dev->private;
	unsigned long i;
	struct kvm *kvm = xics->kvm;
	struct kvm_vcpu *vcpu;

	pr_devel("Releasing xics device\n");

	/*
	 * Since this is the device release function, we know that
	 * userspace does not have any open fd referring to the
	 * device.  Therefore there can not be any of the device
	 * attribute set/get functions being executed concurrently,
	 * and similarly, the connect_vcpu and set/clr_mapped
	 * functions also cannot be being executed.
	 */

	debugfs_remove(xics->dentry);

	/*
	 * We should clean up the vCPU interrupt presenters first.
	 */
	kvm_for_each_vcpu(i, vcpu, kvm) {
		/*
		 * Take vcpu->mutex to ensure that no one_reg get/set ioctl
		 * (i.e. kvmppc_xics_[gs]et_icp) can be done concurrently.
		 * Holding the vcpu->mutex also means that execution is
		 * excluded for the vcpu until the ICP was freed. When the vcpu
		 * can execute again, vcpu->arch.icp and vcpu->arch.irq_type
		 * have been cleared and the vcpu will not be going into the
		 * XICS code anymore.
		 */
		mutex_lock(&vcpu->mutex);
		kvmppc_xics_free_icp(vcpu);
		mutex_unlock(&vcpu->mutex);
	}

	if (kvm)
		kvm->arch.xics = NULL;

	for (i = 0; i <= xics->max_icsid; i++) {
		kfree(xics->ics[i]);
		xics->ics[i] = NULL;
	}
	/*
	 * A reference of the kvmppc_xics pointer is now kept under
	 * the xics_device pointer of the machine for reuse. It is
	 * freed when the VM is destroyed for now until we fix all the
	 * execution paths.
	 */
	kfree(dev);
}

static struct kvmppc_xics *kvmppc_xics_get_device(struct kvm *kvm)
{
	struct kvmppc_xics **kvm_xics_device = &kvm->arch.xics_device;
	struct kvmppc_xics *xics = *kvm_xics_device;

	if (!xics) {
		xics = kzalloc(sizeof(*xics), GFP_KERNEL);
		*kvm_xics_device = xics;
	} else {
		memset(xics, 0, sizeof(*xics));
	}

	return xics;
}

static int kvmppc_xics_create(struct kvm_device *dev, u32 type)
{
	struct kvmppc_xics *xics;
	struct kvm *kvm = dev->kvm;

	pr_devel("Creating xics for partition\n");

	/* Already there ? */
	if (kvm->arch.xics)
		return -EEXIST;

	xics = kvmppc_xics_get_device(kvm);
	if (!xics)
		return -ENOMEM;

	dev->private = xics;
	xics->dev = dev;
	xics->kvm = kvm;
	kvm->arch.xics = xics;

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	if (cpu_has_feature(CPU_FTR_ARCH_206) &&
	    cpu_has_feature(CPU_FTR_HVMODE)) {
		/* Enable real mode support */
		xics->real_mode = ENABLE_REALMODE;
		xics->real_mode_dbg = DEBUG_REALMODE;
	}
#endif /* CONFIG_KVM_BOOK3S_HV_POSSIBLE */

	return 0;
}

static void kvmppc_xics_init(struct kvm_device *dev)
{
	struct kvmppc_xics *xics = dev->private;

	xics_debugfs_init(xics);
}

struct kvm_device_ops kvm_xics_ops = {
	.name = "kvm-xics",
	.create = kvmppc_xics_create,
	.init = kvmppc_xics_init,
	.release = kvmppc_xics_release,
	.set_attr = xics_set_attr,
	.get_attr = xics_get_attr,
	.has_attr = xics_has_attr,
};

int kvmppc_xics_connect_vcpu(struct kvm_device *dev, struct kvm_vcpu *vcpu,
			     u32 xcpu)
{
	struct kvmppc_xics *xics = dev->private;
	int r = -EBUSY;

	if (dev->ops != &kvm_xics_ops)
		return -EPERM;
	if (xics->kvm != vcpu->kvm)
		return -EPERM;
	if (vcpu->arch.irq_type != KVMPPC_IRQ_DEFAULT)
		return -EBUSY;

	r = kvmppc_xics_create_icp(vcpu, xcpu);
	if (!r)
		vcpu->arch.irq_type = KVMPPC_IRQ_XICS;

	return r;
}

void kvmppc_xics_free_icp(struct kvm_vcpu *vcpu)
{
	if (!vcpu->arch.icp)
		return;
	kfree(vcpu->arch.icp);
	vcpu->arch.icp = NULL;
	vcpu->arch.irq_type = KVMPPC_IRQ_DEFAULT;
}

void kvmppc_xics_set_mapped(struct kvm *kvm, unsigned long irq,
			    unsigned long host_irq)
{
	struct kvmppc_xics *xics = kvm->arch.xics;
	struct kvmppc_ics *ics;
	u16 idx;

	ics = kvmppc_xics_find_ics(xics, irq, &idx);
	if (!ics)
		return;

	ics->irq_state[idx].host_irq = host_irq;
	ics->irq_state[idx].intr_cpu = -1;
}
EXPORT_SYMBOL_GPL(kvmppc_xics_set_mapped);

void kvmppc_xics_clr_mapped(struct kvm *kvm, unsigned long irq,
			    unsigned long host_irq)
{
	struct kvmppc_xics *xics = kvm->arch.xics;
	struct kvmppc_ics *ics;
	u16 idx;

	ics = kvmppc_xics_find_ics(xics, irq, &idx);
	if (!ics)
		return;

	ics->irq_state[idx].host_irq = 0;
}
EXPORT_SYMBOL_GPL(kvmppc_xics_clr_mapped);
