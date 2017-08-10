/*
 * Copyright 2017 Benjamin Herrenschmidt, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

/* File to be included by other .c files */

#define XGLUE(a,b) a##b
#define GLUE(a,b) XGLUE(a,b)

static void GLUE(X_PFX,ack_pending)(struct kvmppc_xive_vcpu *xc)
{
	u8 cppr;
	u16 ack;

	/* XXX DD1 bug workaround: Check PIPR vs. CPPR first ! */

	/* Perform the acknowledge OS to register cycle. */
	ack = be16_to_cpu(__x_readw(__x_tima + TM_SPC_ACK_OS_REG));

	/* Synchronize subsequent queue accesses */
	mb();

	/* XXX Check grouping level */

	/* Anything ? */
	if (!((ack >> 8) & TM_QW1_NSR_EO))
		return;

	/* Grab CPPR of the most favored pending interrupt */
	cppr = ack & 0xff;
	if (cppr < 8)
		xc->pending |= 1 << cppr;

#ifdef XIVE_RUNTIME_CHECKS
	/* Check consistency */
	if (cppr >= xc->hw_cppr)
		pr_warn("KVM-XIVE: CPU %d odd ack CPPR, got %d at %d\n",
			smp_processor_id(), cppr, xc->hw_cppr);
#endif

	/*
	 * Update our image of the HW CPPR. We don't yet modify
	 * xc->cppr, this will be done as we scan for interrupts
	 * in the queues.
	 */
	xc->hw_cppr = cppr;
}

static u8 GLUE(X_PFX,esb_load)(struct xive_irq_data *xd, u32 offset)
{
	u64 val;

	if (xd->flags & XIVE_IRQ_FLAG_SHIFT_BUG)
		offset |= offset << 4;

	val =__x_readq(__x_eoi_page(xd) + offset);
#ifdef __LITTLE_ENDIAN__
	val >>= 64-8;
#endif
	return (u8)val;
}


static void GLUE(X_PFX,source_eoi)(u32 hw_irq, struct xive_irq_data *xd)
{
	/* If the XIVE supports the new "store EOI facility, use it */
	if (xd->flags & XIVE_IRQ_FLAG_STORE_EOI)
		__x_writeq(0, __x_eoi_page(xd) + XIVE_ESB_STORE_EOI);
	else if (hw_irq && xd->flags & XIVE_IRQ_FLAG_EOI_FW) {
		opal_int_eoi(hw_irq);
	} else {
		uint64_t eoi_val;

		/*
		 * Otherwise for EOI, we use the special MMIO that does
		 * a clear of both P and Q and returns the old Q,
		 * except for LSIs where we use the "EOI cycle" special
		 * load.
		 *
		 * This allows us to then do a re-trigger if Q was set
		 * rather than synthetizing an interrupt in software
		 *
		 * For LSIs, using the HW EOI cycle works around a problem
		 * on P9 DD1 PHBs where the other ESB accesses don't work
		 * properly.
		 */
		if (xd->flags & XIVE_IRQ_FLAG_LSI)
			__x_readq(__x_eoi_page(xd) + XIVE_ESB_LOAD_EOI);
		else {
			eoi_val = GLUE(X_PFX,esb_load)(xd, XIVE_ESB_SET_PQ_00);

			/* Re-trigger if needed */
			if ((eoi_val & 1) && __x_trig_page(xd))
				__x_writeq(0, __x_trig_page(xd));
		}
	}
}

enum {
	scan_fetch,
	scan_poll,
	scan_eoi,
};

static u32 GLUE(X_PFX,scan_interrupts)(struct kvmppc_xive_vcpu *xc,
				       u8 pending, int scan_type)
{
	u32 hirq = 0;
	u8 prio = 0xff;

	/* Find highest pending priority */
	while ((xc->mfrr != 0xff || pending != 0) && hirq == 0) {
		struct xive_q *q;
		u32 idx, toggle;
		__be32 *qpage;

		/*
		 * If pending is 0 this will return 0xff which is what
		 * we want
		 */
		prio = ffs(pending) - 1;

		/*
		 * If the most favoured prio we found pending is less
		 * favored (or equal) than a pending IPI, we return
		 * the IPI instead.
		 *
		 * Note: If pending was 0 and mfrr is 0xff, we will
		 * not spurriously take an IPI because mfrr cannot
		 * then be smaller than cppr.
		 */
		if (prio >= xc->mfrr && xc->mfrr < xc->cppr) {
			prio = xc->mfrr;
			hirq = XICS_IPI;
			break;
		}

		/* Don't scan past the guest cppr */
		if (prio >= xc->cppr || prio > 7)
			break;

		/* Grab queue and pointers */
		q = &xc->queues[prio];
		idx = q->idx;
		toggle = q->toggle;

		/*
		 * Snapshot the queue page. The test further down for EOI
		 * must use the same "copy" that was used by __xive_read_eq
		 * since qpage can be set concurrently and we don't want
		 * to miss an EOI.
		 */
		qpage = READ_ONCE(q->qpage);

skip_ipi:
		/*
		 * Try to fetch from the queue. Will return 0 for a
		 * non-queueing priority (ie, qpage = 0).
		 */
		hirq = __xive_read_eq(qpage, q->msk, &idx, &toggle);

		/*
		 * If this was a signal for an MFFR change done by
		 * H_IPI we skip it. Additionally, if we were fetching
		 * we EOI it now, thus re-enabling reception of a new
		 * such signal.
		 *
		 * We also need to do that if prio is 0 and we had no
		 * page for the queue. In this case, we have non-queued
		 * IPI that needs to be EOId.
		 *
		 * This is safe because if we have another pending MFRR
		 * change that wasn't observed above, the Q bit will have
		 * been set and another occurrence of the IPI will trigger.
		 */
		if (hirq == XICS_IPI || (prio == 0 && !qpage)) {
			if (scan_type == scan_fetch)
				GLUE(X_PFX,source_eoi)(xc->vp_ipi,
						       &xc->vp_ipi_data);
			/* Loop back on same queue with updated idx/toggle */
#ifdef XIVE_RUNTIME_CHECKS
			WARN_ON(hirq && hirq != XICS_IPI);
#endif
			if (hirq)
				goto skip_ipi;
		}

		/* If fetching, update queue pointers */
		if (scan_type == scan_fetch) {
			q->idx = idx;
			q->toggle = toggle;
		}

		/* Something found, stop searching */
		if (hirq)
			break;

		/* Clear the pending bit on the now empty queue */
		pending &= ~(1 << prio);

		/*
		 * Check if the queue count needs adjusting due to
		 * interrupts being moved away.
		 */
		if (atomic_read(&q->pending_count)) {
			int p = atomic_xchg(&q->pending_count, 0);
			if (p) {
#ifdef XIVE_RUNTIME_CHECKS
				WARN_ON(p > atomic_read(&q->count));
#endif
				atomic_sub(p, &q->count);
			}
		}
	}

	/* If we are just taking a "peek", do nothing else */
	if (scan_type == scan_poll)
		return hirq;

	/* Update the pending bits */
	xc->pending = pending;

	/*
	 * If this is an EOI that's it, no CPPR adjustment done here,
	 * all we needed was cleanup the stale pending bits and check
	 * if there's anything left.
	 */
	if (scan_type == scan_eoi)
		return hirq;

	/*
	 * If we found an interrupt, adjust what the guest CPPR should
	 * be as if we had just fetched that interrupt from HW.
	 */
	if (hirq)
		xc->cppr = prio;
	/*
	 * If it was an IPI the HW CPPR might have been lowered too much
	 * as the HW interrupt we use for IPIs is routed to priority 0.
	 *
	 * We re-sync it here.
	 */
	if (xc->cppr != xc->hw_cppr) {
		xc->hw_cppr = xc->cppr;
		__x_writeb(xc->cppr, __x_tima + TM_QW1_OS + TM_CPPR);
	}

	return hirq;
}

X_STATIC unsigned long GLUE(X_PFX,h_xirr)(struct kvm_vcpu *vcpu)
{
	struct kvmppc_xive_vcpu *xc = vcpu->arch.xive_vcpu;
	u8 old_cppr;
	u32 hirq;

	pr_devel("H_XIRR\n");

	xc->GLUE(X_STAT_PFX,h_xirr)++;

	/* First collect pending bits from HW */
	GLUE(X_PFX,ack_pending)(xc);

	/*
	 * Cleanup the old-style bits if needed (they may have been
	 * set by pull or an escalation interrupts).
	 */
	if (test_bit(BOOK3S_IRQPRIO_EXTERNAL, &vcpu->arch.pending_exceptions))
		clear_bit(BOOK3S_IRQPRIO_EXTERNAL_LEVEL,
			  &vcpu->arch.pending_exceptions);

	pr_devel(" new pending=0x%02x hw_cppr=%d cppr=%d\n",
		 xc->pending, xc->hw_cppr, xc->cppr);

	/* Grab previous CPPR and reverse map it */
	old_cppr = xive_prio_to_guest(xc->cppr);

	/* Scan for actual interrupts */
	hirq = GLUE(X_PFX,scan_interrupts)(xc, xc->pending, scan_fetch);

	pr_devel(" got hirq=0x%x hw_cppr=%d cppr=%d\n",
		 hirq, xc->hw_cppr, xc->cppr);

#ifdef XIVE_RUNTIME_CHECKS
	/* That should never hit */
	if (hirq & 0xff000000)
		pr_warn("XIVE: Weird guest interrupt number 0x%08x\n", hirq);
#endif

	/*
	 * XXX We could check if the interrupt is masked here and
	 * filter it. If we chose to do so, we would need to do:
	 *
	 *    if (masked) {
	 *        lock();
	 *        if (masked) {
	 *            old_Q = true;
	 *            hirq = 0;
	 *        }
	 *        unlock();
	 *    }
	 */

	/* Return interrupt and old CPPR in GPR4 */
	vcpu->arch.gpr[4] = hirq | (old_cppr << 24);

	return H_SUCCESS;
}

X_STATIC unsigned long GLUE(X_PFX,h_ipoll)(struct kvm_vcpu *vcpu, unsigned long server)
{
	struct kvmppc_xive_vcpu *xc = vcpu->arch.xive_vcpu;
	u8 pending = xc->pending;
	u32 hirq;
	u8 pipr;

	pr_devel("H_IPOLL(server=%ld)\n", server);

	xc->GLUE(X_STAT_PFX,h_ipoll)++;

	/* Grab the target VCPU if not the current one */
	if (xc->server_num != server) {
		vcpu = kvmppc_xive_find_server(vcpu->kvm, server);
		if (!vcpu)
			return H_PARAMETER;
		xc = vcpu->arch.xive_vcpu;

		/* Scan all priorities */
		pending = 0xff;
	} else {
		/* Grab pending interrupt if any */
		pipr = __x_readb(__x_tima + TM_QW1_OS + TM_PIPR);
		if (pipr < 8)
			pending |= 1 << pipr;
	}

	hirq = GLUE(X_PFX,scan_interrupts)(xc, pending, scan_poll);

	/* Return interrupt and old CPPR in GPR4 */
	vcpu->arch.gpr[4] = hirq | (xc->cppr << 24);

	return H_SUCCESS;
}

static void GLUE(X_PFX,push_pending_to_hw)(struct kvmppc_xive_vcpu *xc)
{
	u8 pending, prio;

	pending = xc->pending;
	if (xc->mfrr != 0xff) {
		if (xc->mfrr < 8)
			pending |= 1 << xc->mfrr;
		else
			pending |= 0x80;
	}
	if (!pending)
		return;
	prio = ffs(pending) - 1;

	__x_writeb(prio, __x_tima + TM_SPC_SET_OS_PENDING);
}

X_STATIC int GLUE(X_PFX,h_cppr)(struct kvm_vcpu *vcpu, unsigned long cppr)
{
	struct kvmppc_xive_vcpu *xc = vcpu->arch.xive_vcpu;
	u8 old_cppr;

	pr_devel("H_CPPR(cppr=%ld)\n", cppr);

	xc->GLUE(X_STAT_PFX,h_cppr)++;

	/* Map CPPR */
	cppr = xive_prio_from_guest(cppr);

	/* Remember old and update SW state */
	old_cppr = xc->cppr;
	xc->cppr = cppr;

	/*
	 * We are masking less, we need to look for pending things
	 * to deliver and set VP pending bits accordingly to trigger
	 * a new interrupt otherwise we might miss MFRR changes for
	 * which we have optimized out sending an IPI signal.
	 */
	if (cppr > old_cppr)
		GLUE(X_PFX,push_pending_to_hw)(xc);

	/* Apply new CPPR */
	xc->hw_cppr = cppr;
	__x_writeb(cppr, __x_tima + TM_QW1_OS + TM_CPPR);

	return H_SUCCESS;
}

X_STATIC int GLUE(X_PFX,h_eoi)(struct kvm_vcpu *vcpu, unsigned long xirr)
{
	struct kvmppc_xive *xive = vcpu->kvm->arch.xive;
	struct kvmppc_xive_src_block *sb;
	struct kvmppc_xive_irq_state *state;
	struct kvmppc_xive_vcpu *xc = vcpu->arch.xive_vcpu;
	struct xive_irq_data *xd;
	u8 new_cppr = xirr >> 24;
	u32 irq = xirr & 0x00ffffff, hw_num;
	u16 src;
	int rc = 0;

	pr_devel("H_EOI(xirr=%08lx)\n", xirr);

	xc->GLUE(X_STAT_PFX,h_eoi)++;

	xc->cppr = xive_prio_from_guest(new_cppr);

	/*
	 * IPIs are synthetized from MFRR and thus don't need
	 * any special EOI handling. The underlying interrupt
	 * used to signal MFRR changes is EOId when fetched from
	 * the queue.
	 */
	if (irq == XICS_IPI || irq == 0)
		goto bail;

	/* Find interrupt source */
	sb = kvmppc_xive_find_source(xive, irq, &src);
	if (!sb) {
		pr_devel(" source not found !\n");
		rc = H_PARAMETER;
		goto bail;
	}
	state = &sb->irq_state[src];
	kvmppc_xive_select_irq(state, &hw_num, &xd);

	state->in_eoi = true;
	mb();

again:
	if (state->guest_priority == MASKED) {
		arch_spin_lock(&sb->lock);
		if (state->guest_priority != MASKED) {
			arch_spin_unlock(&sb->lock);
			goto again;
		}
		pr_devel(" EOI on saved P...\n");

		/* Clear old_p, that will cause unmask to perform an EOI */
		state->old_p = false;

		arch_spin_unlock(&sb->lock);
	} else {
		pr_devel(" EOI on source...\n");

		/* Perform EOI on the source */
		GLUE(X_PFX,source_eoi)(hw_num, xd);

		/* If it's an emulated LSI, check level and resend */
		if (state->lsi && state->asserted)
			__x_writeq(0, __x_trig_page(xd));

	}

	mb();
	state->in_eoi = false;
bail:

	/* Re-evaluate pending IRQs and update HW */
	GLUE(X_PFX,scan_interrupts)(xc, xc->pending, scan_eoi);
	GLUE(X_PFX,push_pending_to_hw)(xc);
	pr_devel(" after scan pending=%02x\n", xc->pending);

	/* Apply new CPPR */
	xc->hw_cppr = xc->cppr;
	__x_writeb(xc->cppr, __x_tima + TM_QW1_OS + TM_CPPR);

	return rc;
}

X_STATIC int GLUE(X_PFX,h_ipi)(struct kvm_vcpu *vcpu, unsigned long server,
			       unsigned long mfrr)
{
	struct kvmppc_xive_vcpu *xc = vcpu->arch.xive_vcpu;

	pr_devel("H_IPI(server=%08lx,mfrr=%ld)\n", server, mfrr);

	xc->GLUE(X_STAT_PFX,h_ipi)++;

	/* Find target */
	vcpu = kvmppc_xive_find_server(vcpu->kvm, server);
	if (!vcpu)
		return H_PARAMETER;
	xc = vcpu->arch.xive_vcpu;

	/* Locklessly write over MFRR */
	xc->mfrr = mfrr;

	/* Shoot the IPI if most favored than target cppr */
	if (mfrr < xc->cppr)
		__x_writeq(0, __x_trig_page(&xc->vp_ipi_data));

	return H_SUCCESS;
}
