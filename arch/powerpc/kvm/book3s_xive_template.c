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

/* Dummy interrupt used when taking interrupts out of a queue in H_CPPR */
#define XICS_DUMMY	1

static void GLUE(X_PFX,ack_pending)(struct kvmppc_xive_vcpu *xc)
{
	u8 cppr;
	u16 ack;

	/*
	 * Ensure any previous store to CPPR is ordered vs.
	 * the subsequent loads from PIPR or ACK.
	 */
	eieio();

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
	else if (hw_irq && xd->flags & XIVE_IRQ_FLAG_EOI_FW)
		opal_int_eoi(hw_irq);
	else if (xd->flags & XIVE_IRQ_FLAG_LSI) {
		/*
		 * For LSIs the HW EOI cycle is used rather than PQ bits,
		 * as they are automatically re-triggred in HW when still
		 * pending.
		 */
		__x_readq(__x_eoi_page(xd) + XIVE_ESB_LOAD_EOI);
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
		 */
		eoi_val = GLUE(X_PFX,esb_load)(xd, XIVE_ESB_SET_PQ_00);

		/* Re-trigger if needed */
		if ((eoi_val & 1) && __x_trig_page(xd))
			__x_writeq(0, __x_trig_page(xd));
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

		/* Don't scan past the guest cppr */
		if (prio >= xc->cppr || prio > 7) {
			if (xc->mfrr < xc->cppr) {
				prio = xc->mfrr;
				hirq = XICS_IPI;
			}
			break;
		}

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
			if (scan_type == scan_fetch) {
				GLUE(X_PFX,source_eoi)(xc->vp_ipi,
						       &xc->vp_ipi_data);
				q->idx = idx;
				q->toggle = toggle;
			}
			/* Loop back on same queue with updated idx/toggle */
#ifdef XIVE_RUNTIME_CHECKS
			WARN_ON(hirq && hirq != XICS_IPI);
#endif
			if (hirq)
				goto skip_ipi;
		}

		/* If it's the dummy interrupt, continue searching */
		if (hirq == XICS_DUMMY)
			goto skip_ipi;

		/* Clear the pending bit if the queue is now empty */
		if (!hirq) {
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

		/*
		 * If the most favoured prio we found pending is less
		 * favored (or equal) than a pending IPI, we return
		 * the IPI instead.
		 */
		if (prio >= xc->mfrr && xc->mfrr < xc->cppr) {
			prio = xc->mfrr;
			hirq = XICS_IPI;
			break;
		}

		/* If fetching, update queue pointers */
		if (scan_type == scan_fetch) {
			q->idx = idx;
			q->toggle = toggle;
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
	 *
	 * Note: This can only make xc->cppr smaller as the previous
	 * loop will only exit with hirq != 0 if prio is lower than
	 * the current xc->cppr. Thus we don't need to re-check xc->mfrr
	 * for pending IPIs.
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
	vcpu->arch.regs.gpr[4] = hirq | (old_cppr << 24);

	return H_SUCCESS;
}

X_STATIC unsigned long GLUE(X_PFX,h_ipoll)(struct kvm_vcpu *vcpu, unsigned long server)
{
	struct kvmppc_xive_vcpu *xc = vcpu->arch.xive_vcpu;
	u8 pending = xc->pending;
	u32 hirq;

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
		__be64 qw1 = __x_readq(__x_tima + TM_QW1_OS);
		u8 pipr = be64_to_cpu(qw1) & 0xff;
		if (pipr < 8)
			pending |= 1 << pipr;
	}

	hirq = GLUE(X_PFX,scan_interrupts)(xc, pending, scan_poll);

	/* Return interrupt and old CPPR in GPR4 */
	vcpu->arch.regs.gpr[4] = hirq | (xc->cppr << 24);

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

static void GLUE(X_PFX,scan_for_rerouted_irqs)(struct kvmppc_xive *xive,
					       struct kvmppc_xive_vcpu *xc)
{
	unsigned int prio;

	/* For each priority that is now masked */
	for (prio = xc->cppr; prio < KVMPPC_XIVE_Q_COUNT; prio++) {
		struct xive_q *q = &xc->queues[prio];
		struct kvmppc_xive_irq_state *state;
		struct kvmppc_xive_src_block *sb;
		u32 idx, toggle, entry, irq, hw_num;
		struct xive_irq_data *xd;
		__be32 *qpage;
		u16 src;

		idx = q->idx;
		toggle = q->toggle;
		qpage = READ_ONCE(q->qpage);
		if (!qpage)
			continue;

		/* For each interrupt in the queue */
		for (;;) {
			entry = be32_to_cpup(qpage + idx);

			/* No more ? */
			if ((entry >> 31) == toggle)
				break;
			irq = entry & 0x7fffffff;

			/* Skip dummies and IPIs */
			if (irq == XICS_DUMMY || irq == XICS_IPI)
				goto next;
			sb = kvmppc_xive_find_source(xive, irq, &src);
			if (!sb)
				goto next;
			state = &sb->irq_state[src];

			/* Has it been rerouted ? */
			if (xc->server_num == state->act_server)
				goto next;

			/*
			 * Allright, it *has* been re-routed, kill it from
			 * the queue.
			 */
			qpage[idx] = cpu_to_be32((entry & 0x80000000) | XICS_DUMMY);

			/* Find the HW interrupt */
			kvmppc_xive_select_irq(state, &hw_num, &xd);

			/* If it's not an LSI, set PQ to 11 the EOI will force a resend */
			if (!(xd->flags & XIVE_IRQ_FLAG_LSI))
				GLUE(X_PFX,esb_load)(xd, XIVE_ESB_SET_PQ_11);

			/* EOI the source */
			GLUE(X_PFX,source_eoi)(hw_num, xd);

		next:
			idx = (idx + 1) & q->msk;
			if (idx == 0)
				toggle ^= 1;
		}
	}
}

X_STATIC int GLUE(X_PFX,h_cppr)(struct kvm_vcpu *vcpu, unsigned long cppr)
{
	struct kvmppc_xive_vcpu *xc = vcpu->arch.xive_vcpu;
	struct kvmppc_xive *xive = vcpu->kvm->arch.xive;
	u8 old_cppr;

	pr_devel("H_CPPR(cppr=%ld)\n", cppr);

	xc->GLUE(X_STAT_PFX,h_cppr)++;

	/* Map CPPR */
	cppr = xive_prio_from_guest(cppr);

	/* Remember old and update SW state */
	old_cppr = xc->cppr;
	xc->cppr = cppr;

	/*
	 * Order the above update of xc->cppr with the subsequent
	 * read of xc->mfrr inside push_pending_to_hw()
	 */
	smp_mb();

	if (cppr > old_cppr) {
		/*
		 * We are masking less, we need to look for pending things
		 * to deliver and set VP pending bits accordingly to trigger
		 * a new interrupt otherwise we might miss MFRR changes for
		 * which we have optimized out sending an IPI signal.
		 */
		GLUE(X_PFX,push_pending_to_hw)(xc);
	} else {
		/*
		 * We are masking more, we need to check the queue for any
		 * interrupt that has been routed to another CPU, take
		 * it out (replace it with the dummy) and retrigger it.
		 *
		 * This is necessary since those interrupts may otherwise
		 * never be processed, at least not until this CPU restores
		 * its CPPR.
		 *
		 * This is in theory racy vs. HW adding new interrupts to
		 * the queue. In practice this works because the interesting
		 * cases are when the guest has done a set_xive() to move the
		 * interrupt away, which flushes the xive, followed by the
		 * target CPU doing a H_CPPR. So any new interrupt coming into
		 * the queue must still be routed to us and isn't a source
		 * of concern.
		 */
		GLUE(X_PFX,scan_for_rerouted_irqs)(xive, xc);
	}

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
	if (irq == XICS_IPI || irq == 0) {
		/*
		 * This barrier orders the setting of xc->cppr vs.
		 * subsquent test of xc->mfrr done inside
		 * scan_interrupts and push_pending_to_hw
		 */
		smp_mb();
		goto bail;
	}

	/* Find interrupt source */
	sb = kvmppc_xive_find_source(xive, irq, &src);
	if (!sb) {
		pr_devel(" source not found !\n");
		rc = H_PARAMETER;
		/* Same as above */
		smp_mb();
		goto bail;
	}
	state = &sb->irq_state[src];
	kvmppc_xive_select_irq(state, &hw_num, &xd);

	state->in_eoi = true;

	/*
	 * This barrier orders both setting of in_eoi above vs,
	 * subsequent test of guest_priority, and the setting
	 * of xc->cppr vs. subsquent test of xc->mfrr done inside
	 * scan_interrupts and push_pending_to_hw
	 */
	smp_mb();

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

	/*
	 * This barrier orders the above guest_priority check
	 * and spin_lock/unlock with clearing in_eoi below.
	 *
	 * It also has to be a full mb() as it must ensure
	 * the MMIOs done in source_eoi() are completed before
	 * state->in_eoi is visible.
	 */
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

	/*
	 * The load of xc->cppr below and the subsequent MMIO store
	 * to the IPI must happen after the above mfrr update is
	 * globally visible so that:
	 *
	 * - Synchronize with another CPU doing an H_EOI or a H_CPPR
	 *   updating xc->cppr then reading xc->mfrr.
	 *
	 * - The target of the IPI sees the xc->mfrr update
	 */
	mb();

	/* Shoot the IPI if most favored than target cppr */
	if (mfrr < xc->cppr)
		__x_writeq(0, __x_trig_page(&xc->vp_ipi_data));

	return H_SUCCESS;
}
