// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2017 Benjamin Herrenschmidt, IBM Corporation.
 */

#define pr_fmt(fmt) "xive-kvm: " fmt

#include <linux/kernel.h>
#include <linux/kvm_host.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <linux/uaccess.h>
#include <asm/kvm_book3s.h>
#include <asm/kvm_ppc.h>
#include <asm/hvcall.h>
#include <asm/xics.h>
#include <asm/xive.h>
#include <asm/xive-regs.h>
#include <asm/debug.h>
#include <asm/debugfs.h>
#include <asm/time.h>
#include <asm/opal.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include "book3s_xive.h"


/*
 * Virtual mode variants of the hcalls for use on radix/radix
 * with AIL. They require the VCPU's VP to be "pushed"
 *
 * We still instantiate them here because we use some of the
 * generated utility functions as well in this file.
 */
#define XIVE_RUNTIME_CHECKS
#define X_PFX xive_vm_
#define X_STATIC static
#define X_STAT_PFX stat_vm_
#define __x_tima		xive_tima
#define __x_eoi_page(xd)	((void __iomem *)((xd)->eoi_mmio))
#define __x_trig_page(xd)	((void __iomem *)((xd)->trig_mmio))
#define __x_writeb	__raw_writeb
#define __x_readw	__raw_readw
#define __x_readq	__raw_readq
#define __x_writeq	__raw_writeq

#include "book3s_xive_template.c"

/*
 * We leave a gap of a couple of interrupts in the queue to
 * account for the IPI and additional safety guard.
 */
#define XIVE_Q_GAP	2

/*
 * Push a vcpu's context to the XIVE on guest entry.
 * This assumes we are in virtual mode (MMU on)
 */
void kvmppc_xive_push_vcpu(struct kvm_vcpu *vcpu)
{
	void __iomem *tima = local_paca->kvm_hstate.xive_tima_virt;
	u64 pq;

	if (!tima)
		return;
	eieio();
	__raw_writeq(vcpu->arch.xive_saved_state.w01, tima + TM_QW1_OS);
	__raw_writel(vcpu->arch.xive_cam_word, tima + TM_QW1_OS + TM_WORD2);
	vcpu->arch.xive_pushed = 1;
	eieio();

	/*
	 * We clear the irq_pending flag. There is a small chance of a
	 * race vs. the escalation interrupt happening on another
	 * processor setting it again, but the only consequence is to
	 * cause a spurious wakeup on the next H_CEDE, which is not an
	 * issue.
	 */
	vcpu->arch.irq_pending = 0;

	/*
	 * In single escalation mode, if the escalation interrupt is
	 * on, we mask it.
	 */
	if (vcpu->arch.xive_esc_on) {
		pq = __raw_readq((void __iomem *)(vcpu->arch.xive_esc_vaddr +
						  XIVE_ESB_SET_PQ_01));
		mb();

		/*
		 * We have a possible subtle race here: The escalation
		 * interrupt might have fired and be on its way to the
		 * host queue while we mask it, and if we unmask it
		 * early enough (re-cede right away), there is a
		 * theorical possibility that it fires again, thus
		 * landing in the target queue more than once which is
		 * a big no-no.
		 *
		 * Fortunately, solving this is rather easy. If the
		 * above load setting PQ to 01 returns a previous
		 * value where P is set, then we know the escalation
		 * interrupt is somewhere on its way to the host. In
		 * that case we simply don't clear the xive_esc_on
		 * flag below. It will be eventually cleared by the
		 * handler for the escalation interrupt.
		 *
		 * Then, when doing a cede, we check that flag again
		 * before re-enabling the escalation interrupt, and if
		 * set, we abort the cede.
		 */
		if (!(pq & XIVE_ESB_VAL_P))
			/* Now P is 0, we can clear the flag */
			vcpu->arch.xive_esc_on = 0;
	}
}
EXPORT_SYMBOL_GPL(kvmppc_xive_push_vcpu);

/*
 * This is a simple trigger for a generic XIVE IRQ. This must
 * only be called for interrupts that support a trigger page
 */
static bool xive_irq_trigger(struct xive_irq_data *xd)
{
	/* This should be only for MSIs */
	if (WARN_ON(xd->flags & XIVE_IRQ_FLAG_LSI))
		return false;

	/* Those interrupts should always have a trigger page */
	if (WARN_ON(!xd->trig_mmio))
		return false;

	out_be64(xd->trig_mmio, 0);

	return true;
}

static irqreturn_t xive_esc_irq(int irq, void *data)
{
	struct kvm_vcpu *vcpu = data;

	vcpu->arch.irq_pending = 1;
	smp_mb();
	if (vcpu->arch.ceded)
		kvmppc_fast_vcpu_kick(vcpu);

	/* Since we have the no-EOI flag, the interrupt is effectively
	 * disabled now. Clearing xive_esc_on means we won't bother
	 * doing so on the next entry.
	 *
	 * This also allows the entry code to know that if a PQ combination
	 * of 10 is observed while xive_esc_on is true, it means the queue
	 * contains an unprocessed escalation interrupt. We don't make use of
	 * that knowledge today but might (see comment in book3s_hv_rmhandler.S)
	 */
	vcpu->arch.xive_esc_on = false;

	return IRQ_HANDLED;
}

int kvmppc_xive_attach_escalation(struct kvm_vcpu *vcpu, u8 prio,
				  bool single_escalation)
{
	struct kvmppc_xive_vcpu *xc = vcpu->arch.xive_vcpu;
	struct xive_q *q = &xc->queues[prio];
	char *name = NULL;
	int rc;

	/* Already there ? */
	if (xc->esc_virq[prio])
		return 0;

	/* Hook up the escalation interrupt */
	xc->esc_virq[prio] = irq_create_mapping(NULL, q->esc_irq);
	if (!xc->esc_virq[prio]) {
		pr_err("Failed to map escalation interrupt for queue %d of VCPU %d\n",
		       prio, xc->server_num);
		return -EIO;
	}

	if (single_escalation)
		name = kasprintf(GFP_KERNEL, "kvm-%d-%d",
				 vcpu->kvm->arch.lpid, xc->server_num);
	else
		name = kasprintf(GFP_KERNEL, "kvm-%d-%d-%d",
				 vcpu->kvm->arch.lpid, xc->server_num, prio);
	if (!name) {
		pr_err("Failed to allocate escalation irq name for queue %d of VCPU %d\n",
		       prio, xc->server_num);
		rc = -ENOMEM;
		goto error;
	}

	pr_devel("Escalation %s irq %d (prio %d)\n", name, xc->esc_virq[prio], prio);

	rc = request_irq(xc->esc_virq[prio], xive_esc_irq,
			 IRQF_NO_THREAD, name, vcpu);
	if (rc) {
		pr_err("Failed to request escalation interrupt for queue %d of VCPU %d\n",
		       prio, xc->server_num);
		goto error;
	}
	xc->esc_virq_names[prio] = name;

	/* In single escalation mode, we grab the ESB MMIO of the
	 * interrupt and mask it. Also populate the VCPU v/raddr
	 * of the ESB page for use by asm entry/exit code. Finally
	 * set the XIVE_IRQ_NO_EOI flag which will prevent the
	 * core code from performing an EOI on the escalation
	 * interrupt, thus leaving it effectively masked after
	 * it fires once.
	 */
	if (single_escalation) {
		struct irq_data *d = irq_get_irq_data(xc->esc_virq[prio]);
		struct xive_irq_data *xd = irq_data_get_irq_handler_data(d);

		xive_vm_esb_load(xd, XIVE_ESB_SET_PQ_01);
		vcpu->arch.xive_esc_raddr = xd->eoi_page;
		vcpu->arch.xive_esc_vaddr = (__force u64)xd->eoi_mmio;
		xd->flags |= XIVE_IRQ_NO_EOI;
	}

	return 0;
error:
	irq_dispose_mapping(xc->esc_virq[prio]);
	xc->esc_virq[prio] = 0;
	kfree(name);
	return rc;
}

static int xive_provision_queue(struct kvm_vcpu *vcpu, u8 prio)
{
	struct kvmppc_xive_vcpu *xc = vcpu->arch.xive_vcpu;
	struct kvmppc_xive *xive = xc->xive;
	struct xive_q *q =  &xc->queues[prio];
	void *qpage;
	int rc;

	if (WARN_ON(q->qpage))
		return 0;

	/* Allocate the queue and retrieve infos on current node for now */
	qpage = (__be32 *)__get_free_pages(GFP_KERNEL, xive->q_page_order);
	if (!qpage) {
		pr_err("Failed to allocate queue %d for VCPU %d\n",
		       prio, xc->server_num);
		return -ENOMEM;
	}
	memset(qpage, 0, 1 << xive->q_order);

	/*
	 * Reconfigure the queue. This will set q->qpage only once the
	 * queue is fully configured. This is a requirement for prio 0
	 * as we will stop doing EOIs for every IPI as soon as we observe
	 * qpage being non-NULL, and instead will only EOI when we receive
	 * corresponding queue 0 entries
	 */
	rc = xive_native_configure_queue(xc->vp_id, q, prio, qpage,
					 xive->q_order, true);
	if (rc)
		pr_err("Failed to configure queue %d for VCPU %d\n",
		       prio, xc->server_num);
	return rc;
}

/* Called with xive->lock held */
static int xive_check_provisioning(struct kvm *kvm, u8 prio)
{
	struct kvmppc_xive *xive = kvm->arch.xive;
	struct kvm_vcpu *vcpu;
	int i, rc;

	lockdep_assert_held(&xive->lock);

	/* Already provisioned ? */
	if (xive->qmap & (1 << prio))
		return 0;

	pr_devel("Provisioning prio... %d\n", prio);

	/* Provision each VCPU and enable escalations if needed */
	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (!vcpu->arch.xive_vcpu)
			continue;
		rc = xive_provision_queue(vcpu, prio);
		if (rc == 0 && !xive->single_escalation)
			kvmppc_xive_attach_escalation(vcpu, prio,
						      xive->single_escalation);
		if (rc)
			return rc;
	}

	/* Order previous stores and mark it as provisioned */
	mb();
	xive->qmap |= (1 << prio);
	return 0;
}

static void xive_inc_q_pending(struct kvm *kvm, u32 server, u8 prio)
{
	struct kvm_vcpu *vcpu;
	struct kvmppc_xive_vcpu *xc;
	struct xive_q *q;

	/* Locate target server */
	vcpu = kvmppc_xive_find_server(kvm, server);
	if (!vcpu) {
		pr_warn("%s: Can't find server %d\n", __func__, server);
		return;
	}
	xc = vcpu->arch.xive_vcpu;
	if (WARN_ON(!xc))
		return;

	q = &xc->queues[prio];
	atomic_inc(&q->pending_count);
}

static int xive_try_pick_queue(struct kvm_vcpu *vcpu, u8 prio)
{
	struct kvmppc_xive_vcpu *xc = vcpu->arch.xive_vcpu;
	struct xive_q *q;
	u32 max;

	if (WARN_ON(!xc))
		return -ENXIO;
	if (!xc->valid)
		return -ENXIO;

	q = &xc->queues[prio];
	if (WARN_ON(!q->qpage))
		return -ENXIO;

	/* Calculate max number of interrupts in that queue. */
	max = (q->msk + 1) - XIVE_Q_GAP;
	return atomic_add_unless(&q->count, 1, max) ? 0 : -EBUSY;
}

int kvmppc_xive_select_target(struct kvm *kvm, u32 *server, u8 prio)
{
	struct kvm_vcpu *vcpu;
	int i, rc;

	/* Locate target server */
	vcpu = kvmppc_xive_find_server(kvm, *server);
	if (!vcpu) {
		pr_devel("Can't find server %d\n", *server);
		return -EINVAL;
	}

	pr_devel("Finding irq target on 0x%x/%d...\n", *server, prio);

	/* Try pick it */
	rc = xive_try_pick_queue(vcpu, prio);
	if (rc == 0)
		return rc;

	pr_devel(" .. failed, looking up candidate...\n");

	/* Failed, pick another VCPU */
	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (!vcpu->arch.xive_vcpu)
			continue;
		rc = xive_try_pick_queue(vcpu, prio);
		if (rc == 0) {
			*server = vcpu->arch.xive_vcpu->server_num;
			pr_devel("  found on 0x%x/%d\n", *server, prio);
			return rc;
		}
	}
	pr_devel("  no available target !\n");

	/* No available target ! */
	return -EBUSY;
}

static u8 xive_lock_and_mask(struct kvmppc_xive *xive,
			     struct kvmppc_xive_src_block *sb,
			     struct kvmppc_xive_irq_state *state)
{
	struct xive_irq_data *xd;
	u32 hw_num;
	u8 old_prio;
	u64 val;

	/*
	 * Take the lock, set masked, try again if racing
	 * with H_EOI
	 */
	for (;;) {
		arch_spin_lock(&sb->lock);
		old_prio = state->guest_priority;
		state->guest_priority = MASKED;
		mb();
		if (!state->in_eoi)
			break;
		state->guest_priority = old_prio;
		arch_spin_unlock(&sb->lock);
	}

	/* No change ? Bail */
	if (old_prio == MASKED)
		return old_prio;

	/* Get the right irq */
	kvmppc_xive_select_irq(state, &hw_num, &xd);

	/*
	 * If the interrupt is marked as needing masking via
	 * firmware, we do it here. Firmware masking however
	 * is "lossy", it won't return the old p and q bits
	 * and won't set the interrupt to a state where it will
	 * record queued ones. If this is an issue we should do
	 * lazy masking instead.
	 *
	 * For now, we work around this in unmask by forcing
	 * an interrupt whenever we unmask a non-LSI via FW
	 * (if ever).
	 */
	if (xd->flags & OPAL_XIVE_IRQ_MASK_VIA_FW) {
		xive_native_configure_irq(hw_num,
				kvmppc_xive_vp(xive, state->act_server),
				MASKED, state->number);
		/* set old_p so we can track if an H_EOI was done */
		state->old_p = true;
		state->old_q = false;
	} else {
		/* Set PQ to 10, return old P and old Q and remember them */
		val = xive_vm_esb_load(xd, XIVE_ESB_SET_PQ_10);
		state->old_p = !!(val & 2);
		state->old_q = !!(val & 1);

		/*
		 * Synchronize hardware to sensure the queues are updated
		 * when masking
		 */
		xive_native_sync_source(hw_num);
	}

	return old_prio;
}

static void xive_lock_for_unmask(struct kvmppc_xive_src_block *sb,
				 struct kvmppc_xive_irq_state *state)
{
	/*
	 * Take the lock try again if racing with H_EOI
	 */
	for (;;) {
		arch_spin_lock(&sb->lock);
		if (!state->in_eoi)
			break;
		arch_spin_unlock(&sb->lock);
	}
}

static void xive_finish_unmask(struct kvmppc_xive *xive,
			       struct kvmppc_xive_src_block *sb,
			       struct kvmppc_xive_irq_state *state,
			       u8 prio)
{
	struct xive_irq_data *xd;
	u32 hw_num;

	/* If we aren't changing a thing, move on */
	if (state->guest_priority != MASKED)
		goto bail;

	/* Get the right irq */
	kvmppc_xive_select_irq(state, &hw_num, &xd);

	/*
	 * See command in xive_lock_and_mask() concerning masking
	 * via firmware.
	 */
	if (xd->flags & OPAL_XIVE_IRQ_MASK_VIA_FW) {
		xive_native_configure_irq(hw_num,
				kvmppc_xive_vp(xive, state->act_server),
				state->act_priority, state->number);
		/* If an EOI is needed, do it here */
		if (!state->old_p)
			xive_vm_source_eoi(hw_num, xd);
		/* If this is not an LSI, force a trigger */
		if (!(xd->flags & OPAL_XIVE_IRQ_LSI))
			xive_irq_trigger(xd);
		goto bail;
	}

	/* Old Q set, set PQ to 11 */
	if (state->old_q)
		xive_vm_esb_load(xd, XIVE_ESB_SET_PQ_11);

	/*
	 * If not old P, then perform an "effective" EOI,
	 * on the source. This will handle the cases where
	 * FW EOI is needed.
	 */
	if (!state->old_p)
		xive_vm_source_eoi(hw_num, xd);

	/* Synchronize ordering and mark unmasked */
	mb();
bail:
	state->guest_priority = prio;
}

/*
 * Target an interrupt to a given server/prio, this will fallback
 * to another server if necessary and perform the HW targetting
 * updates as needed
 *
 * NOTE: Must be called with the state lock held
 */
static int xive_target_interrupt(struct kvm *kvm,
				 struct kvmppc_xive_irq_state *state,
				 u32 server, u8 prio)
{
	struct kvmppc_xive *xive = kvm->arch.xive;
	u32 hw_num;
	int rc;

	/*
	 * This will return a tentative server and actual
	 * priority. The count for that new target will have
	 * already been incremented.
	 */
	rc = kvmppc_xive_select_target(kvm, &server, prio);

	/*
	 * We failed to find a target ? Not much we can do
	 * at least until we support the GIQ.
	 */
	if (rc)
		return rc;

	/*
	 * Increment the old queue pending count if there
	 * was one so that the old queue count gets adjusted later
	 * when observed to be empty.
	 */
	if (state->act_priority != MASKED)
		xive_inc_q_pending(kvm,
				   state->act_server,
				   state->act_priority);
	/*
	 * Update state and HW
	 */
	state->act_priority = prio;
	state->act_server = server;

	/* Get the right irq */
	kvmppc_xive_select_irq(state, &hw_num, NULL);

	return xive_native_configure_irq(hw_num,
					 kvmppc_xive_vp(xive, server),
					 prio, state->number);
}

/*
 * Targetting rules: In order to avoid losing track of
 * pending interrupts accross mask and unmask, which would
 * allow queue overflows, we implement the following rules:
 *
 *  - Unless it was never enabled (or we run out of capacity)
 *    an interrupt is always targetted at a valid server/queue
 *    pair even when "masked" by the guest. This pair tends to
 *    be the last one used but it can be changed under some
 *    circumstances. That allows us to separate targetting
 *    from masking, we only handle accounting during (re)targetting,
 *    this also allows us to let an interrupt drain into its target
 *    queue after masking, avoiding complex schemes to remove
 *    interrupts out of remote processor queues.
 *
 *  - When masking, we set PQ to 10 and save the previous value
 *    of P and Q.
 *
 *  - When unmasking, if saved Q was set, we set PQ to 11
 *    otherwise we leave PQ to the HW state which will be either
 *    10 if nothing happened or 11 if the interrupt fired while
 *    masked. Effectively we are OR'ing the previous Q into the
 *    HW Q.
 *
 *    Then if saved P is clear, we do an effective EOI (Q->P->Trigger)
 *    which will unmask the interrupt and shoot a new one if Q was
 *    set.
 *
 *    Otherwise (saved P is set) we leave PQ unchanged (so 10 or 11,
 *    effectively meaning an H_EOI from the guest is still expected
 *    for that interrupt).
 *
 *  - If H_EOI occurs while masked, we clear the saved P.
 *
 *  - When changing target, we account on the new target and
 *    increment a separate "pending" counter on the old one.
 *    This pending counter will be used to decrement the old
 *    target's count when its queue has been observed empty.
 */

int kvmppc_xive_set_xive(struct kvm *kvm, u32 irq, u32 server,
			 u32 priority)
{
	struct kvmppc_xive *xive = kvm->arch.xive;
	struct kvmppc_xive_src_block *sb;
	struct kvmppc_xive_irq_state *state;
	u8 new_act_prio;
	int rc = 0;
	u16 idx;

	if (!xive)
		return -ENODEV;

	pr_devel("set_xive ! irq 0x%x server 0x%x prio %d\n",
		 irq, server, priority);

	/* First, check provisioning of queues */
	if (priority != MASKED) {
		mutex_lock(&xive->lock);
		rc = xive_check_provisioning(xive->kvm,
			      xive_prio_from_guest(priority));
		mutex_unlock(&xive->lock);
	}
	if (rc) {
		pr_devel("  provisioning failure %d !\n", rc);
		return rc;
	}

	sb = kvmppc_xive_find_source(xive, irq, &idx);
	if (!sb)
		return -EINVAL;
	state = &sb->irq_state[idx];

	/*
	 * We first handle masking/unmasking since the locking
	 * might need to be retried due to EOIs, we'll handle
	 * targetting changes later. These functions will return
	 * with the SB lock held.
	 *
	 * xive_lock_and_mask() will also set state->guest_priority
	 * but won't otherwise change other fields of the state.
	 *
	 * xive_lock_for_unmask will not actually unmask, this will
	 * be done later by xive_finish_unmask() once the targetting
	 * has been done, so we don't try to unmask an interrupt
	 * that hasn't yet been targetted.
	 */
	if (priority == MASKED)
		xive_lock_and_mask(xive, sb, state);
	else
		xive_lock_for_unmask(sb, state);


	/*
	 * Then we handle targetting.
	 *
	 * First calculate a new "actual priority"
	 */
	new_act_prio = state->act_priority;
	if (priority != MASKED)
		new_act_prio = xive_prio_from_guest(priority);

	pr_devel(" new_act_prio=%x act_server=%x act_prio=%x\n",
		 new_act_prio, state->act_server, state->act_priority);

	/*
	 * Then check if we actually need to change anything,
	 *
	 * The condition for re-targetting the interrupt is that
	 * we have a valid new priority (new_act_prio is not 0xff)
	 * and either the server or the priority changed.
	 *
	 * Note: If act_priority was ff and the new priority is
	 *       also ff, we don't do anything and leave the interrupt
	 *       untargetted. An attempt of doing an int_on on an
	 *       untargetted interrupt will fail. If that is a problem
	 *       we could initialize interrupts with valid default
	 */

	if (new_act_prio != MASKED &&
	    (state->act_server != server ||
	     state->act_priority != new_act_prio))
		rc = xive_target_interrupt(kvm, state, server, new_act_prio);

	/*
	 * Perform the final unmasking of the interrupt source
	 * if necessary
	 */
	if (priority != MASKED)
		xive_finish_unmask(xive, sb, state, priority);

	/*
	 * Finally Update saved_priority to match. Only int_on/off
	 * set this field to a different value.
	 */
	state->saved_priority = priority;

	arch_spin_unlock(&sb->lock);
	return rc;
}

int kvmppc_xive_get_xive(struct kvm *kvm, u32 irq, u32 *server,
			 u32 *priority)
{
	struct kvmppc_xive *xive = kvm->arch.xive;
	struct kvmppc_xive_src_block *sb;
	struct kvmppc_xive_irq_state *state;
	u16 idx;

	if (!xive)
		return -ENODEV;

	sb = kvmppc_xive_find_source(xive, irq, &idx);
	if (!sb)
		return -EINVAL;
	state = &sb->irq_state[idx];
	arch_spin_lock(&sb->lock);
	*server = state->act_server;
	*priority = state->guest_priority;
	arch_spin_unlock(&sb->lock);

	return 0;
}

int kvmppc_xive_int_on(struct kvm *kvm, u32 irq)
{
	struct kvmppc_xive *xive = kvm->arch.xive;
	struct kvmppc_xive_src_block *sb;
	struct kvmppc_xive_irq_state *state;
	u16 idx;

	if (!xive)
		return -ENODEV;

	sb = kvmppc_xive_find_source(xive, irq, &idx);
	if (!sb)
		return -EINVAL;
	state = &sb->irq_state[idx];

	pr_devel("int_on(irq=0x%x)\n", irq);

	/*
	 * Check if interrupt was not targetted
	 */
	if (state->act_priority == MASKED) {
		pr_devel("int_on on untargetted interrupt\n");
		return -EINVAL;
	}

	/* If saved_priority is 0xff, do nothing */
	if (state->saved_priority == MASKED)
		return 0;

	/*
	 * Lock and unmask it.
	 */
	xive_lock_for_unmask(sb, state);
	xive_finish_unmask(xive, sb, state, state->saved_priority);
	arch_spin_unlock(&sb->lock);

	return 0;
}

int kvmppc_xive_int_off(struct kvm *kvm, u32 irq)
{
	struct kvmppc_xive *xive = kvm->arch.xive;
	struct kvmppc_xive_src_block *sb;
	struct kvmppc_xive_irq_state *state;
	u16 idx;

	if (!xive)
		return -ENODEV;

	sb = kvmppc_xive_find_source(xive, irq, &idx);
	if (!sb)
		return -EINVAL;
	state = &sb->irq_state[idx];

	pr_devel("int_off(irq=0x%x)\n", irq);

	/*
	 * Lock and mask
	 */
	state->saved_priority = xive_lock_and_mask(xive, sb, state);
	arch_spin_unlock(&sb->lock);

	return 0;
}

static bool xive_restore_pending_irq(struct kvmppc_xive *xive, u32 irq)
{
	struct kvmppc_xive_src_block *sb;
	struct kvmppc_xive_irq_state *state;
	u16 idx;

	sb = kvmppc_xive_find_source(xive, irq, &idx);
	if (!sb)
		return false;
	state = &sb->irq_state[idx];
	if (!state->valid)
		return false;

	/*
	 * Trigger the IPI. This assumes we never restore a pass-through
	 * interrupt which should be safe enough
	 */
	xive_irq_trigger(&state->ipi_data);

	return true;
}

u64 kvmppc_xive_get_icp(struct kvm_vcpu *vcpu)
{
	struct kvmppc_xive_vcpu *xc = vcpu->arch.xive_vcpu;

	if (!xc)
		return 0;

	/* Return the per-cpu state for state saving/migration */
	return (u64)xc->cppr << KVM_REG_PPC_ICP_CPPR_SHIFT |
	       (u64)xc->mfrr << KVM_REG_PPC_ICP_MFRR_SHIFT |
	       (u64)0xff << KVM_REG_PPC_ICP_PPRI_SHIFT;
}

int kvmppc_xive_set_icp(struct kvm_vcpu *vcpu, u64 icpval)
{
	struct kvmppc_xive_vcpu *xc = vcpu->arch.xive_vcpu;
	struct kvmppc_xive *xive = vcpu->kvm->arch.xive;
	u8 cppr, mfrr;
	u32 xisr;

	if (!xc || !xive)
		return -ENOENT;

	/* Grab individual state fields. We don't use pending_pri */
	cppr = icpval >> KVM_REG_PPC_ICP_CPPR_SHIFT;
	xisr = (icpval >> KVM_REG_PPC_ICP_XISR_SHIFT) &
		KVM_REG_PPC_ICP_XISR_MASK;
	mfrr = icpval >> KVM_REG_PPC_ICP_MFRR_SHIFT;

	pr_devel("set_icp vcpu %d cppr=0x%x mfrr=0x%x xisr=0x%x\n",
		 xc->server_num, cppr, mfrr, xisr);

	/*
	 * We can't update the state of a "pushed" VCPU, but that
	 * shouldn't happen because the vcpu->mutex makes running a
	 * vcpu mutually exclusive with doing one_reg get/set on it.
	 */
	if (WARN_ON(vcpu->arch.xive_pushed))
		return -EIO;

	/* Update VCPU HW saved state */
	vcpu->arch.xive_saved_state.cppr = cppr;
	xc->hw_cppr = xc->cppr = cppr;

	/*
	 * Update MFRR state. If it's not 0xff, we mark the VCPU as
	 * having a pending MFRR change, which will re-evaluate the
	 * target. The VCPU will thus potentially get a spurious
	 * interrupt but that's not a big deal.
	 */
	xc->mfrr = mfrr;
	if (mfrr < cppr)
		xive_irq_trigger(&xc->vp_ipi_data);

	/*
	 * Now saved XIRR is "interesting". It means there's something in
	 * the legacy "1 element" queue... for an IPI we simply ignore it,
	 * as the MFRR restore will handle that. For anything else we need
	 * to force a resend of the source.
	 * However the source may not have been setup yet. If that's the
	 * case, we keep that info and increment a counter in the xive to
	 * tell subsequent xive_set_source() to go look.
	 */
	if (xisr > XICS_IPI && !xive_restore_pending_irq(xive, xisr)) {
		xc->delayed_irq = xisr;
		xive->delayed_irqs++;
		pr_devel("  xisr restore delayed\n");
	}

	return 0;
}

int kvmppc_xive_set_mapped(struct kvm *kvm, unsigned long guest_irq,
			   struct irq_desc *host_desc)
{
	struct kvmppc_xive *xive = kvm->arch.xive;
	struct kvmppc_xive_src_block *sb;
	struct kvmppc_xive_irq_state *state;
	struct irq_data *host_data = irq_desc_get_irq_data(host_desc);
	unsigned int host_irq = irq_desc_get_irq(host_desc);
	unsigned int hw_irq = (unsigned int)irqd_to_hwirq(host_data);
	u16 idx;
	u8 prio;
	int rc;

	if (!xive)
		return -ENODEV;

	pr_devel("set_mapped girq 0x%lx host HW irq 0x%x...\n",guest_irq, hw_irq);

	sb = kvmppc_xive_find_source(xive, guest_irq, &idx);
	if (!sb)
		return -EINVAL;
	state = &sb->irq_state[idx];

	/*
	 * Mark the passed-through interrupt as going to a VCPU,
	 * this will prevent further EOIs and similar operations
	 * from the XIVE code. It will also mask the interrupt
	 * to either PQ=10 or 11 state, the latter if the interrupt
	 * is pending. This will allow us to unmask or retrigger it
	 * after routing it to the guest with a simple EOI.
	 *
	 * The "state" argument is a "token", all it needs is to be
	 * non-NULL to switch to passed-through or NULL for the
	 * other way around. We may not yet have an actual VCPU
	 * target here and we don't really care.
	 */
	rc = irq_set_vcpu_affinity(host_irq, state);
	if (rc) {
		pr_err("Failed to set VCPU affinity for irq %d\n", host_irq);
		return rc;
	}

	/*
	 * Mask and read state of IPI. We need to know if its P bit
	 * is set as that means it's potentially already using a
	 * queue entry in the target
	 */
	prio = xive_lock_and_mask(xive, sb, state);
	pr_devel(" old IPI prio %02x P:%d Q:%d\n", prio,
		 state->old_p, state->old_q);

	/* Turn the IPI hard off */
	xive_vm_esb_load(&state->ipi_data, XIVE_ESB_SET_PQ_01);

	/*
	 * Reset ESB guest mapping. Needed when ESB pages are exposed
	 * to the guest in XIVE native mode
	 */
	if (xive->ops && xive->ops->reset_mapped)
		xive->ops->reset_mapped(kvm, guest_irq);

	/* Grab info about irq */
	state->pt_number = hw_irq;
	state->pt_data = irq_data_get_irq_handler_data(host_data);

	/*
	 * Configure the IRQ to match the existing configuration of
	 * the IPI if it was already targetted. Otherwise this will
	 * mask the interrupt in a lossy way (act_priority is 0xff)
	 * which is fine for a never started interrupt.
	 */
	xive_native_configure_irq(hw_irq,
				  kvmppc_xive_vp(xive, state->act_server),
				  state->act_priority, state->number);

	/*
	 * We do an EOI to enable the interrupt (and retrigger if needed)
	 * if the guest has the interrupt unmasked and the P bit was *not*
	 * set in the IPI. If it was set, we know a slot may still be in
	 * use in the target queue thus we have to wait for a guest
	 * originated EOI
	 */
	if (prio != MASKED && !state->old_p)
		xive_vm_source_eoi(hw_irq, state->pt_data);

	/* Clear old_p/old_q as they are no longer relevant */
	state->old_p = state->old_q = false;

	/* Restore guest prio (unlocks EOI) */
	mb();
	state->guest_priority = prio;
	arch_spin_unlock(&sb->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(kvmppc_xive_set_mapped);

int kvmppc_xive_clr_mapped(struct kvm *kvm, unsigned long guest_irq,
			   struct irq_desc *host_desc)
{
	struct kvmppc_xive *xive = kvm->arch.xive;
	struct kvmppc_xive_src_block *sb;
	struct kvmppc_xive_irq_state *state;
	unsigned int host_irq = irq_desc_get_irq(host_desc);
	u16 idx;
	u8 prio;
	int rc;

	if (!xive)
		return -ENODEV;

	pr_devel("clr_mapped girq 0x%lx...\n", guest_irq);

	sb = kvmppc_xive_find_source(xive, guest_irq, &idx);
	if (!sb)
		return -EINVAL;
	state = &sb->irq_state[idx];

	/*
	 * Mask and read state of IRQ. We need to know if its P bit
	 * is set as that means it's potentially already using a
	 * queue entry in the target
	 */
	prio = xive_lock_and_mask(xive, sb, state);
	pr_devel(" old IRQ prio %02x P:%d Q:%d\n", prio,
		 state->old_p, state->old_q);

	/*
	 * If old_p is set, the interrupt is pending, we switch it to
	 * PQ=11. This will force a resend in the host so the interrupt
	 * isn't lost to whatver host driver may pick it up
	 */
	if (state->old_p)
		xive_vm_esb_load(state->pt_data, XIVE_ESB_SET_PQ_11);

	/* Release the passed-through interrupt to the host */
	rc = irq_set_vcpu_affinity(host_irq, NULL);
	if (rc) {
		pr_err("Failed to clr VCPU affinity for irq %d\n", host_irq);
		return rc;
	}

	/* Forget about the IRQ */
	state->pt_number = 0;
	state->pt_data = NULL;

	/*
	 * Reset ESB guest mapping. Needed when ESB pages are exposed
	 * to the guest in XIVE native mode
	 */
	if (xive->ops && xive->ops->reset_mapped) {
		xive->ops->reset_mapped(kvm, guest_irq);
	}

	/* Reconfigure the IPI */
	xive_native_configure_irq(state->ipi_number,
				  kvmppc_xive_vp(xive, state->act_server),
				  state->act_priority, state->number);

	/*
	 * If old_p is set (we have a queue entry potentially
	 * occupied) or the interrupt is masked, we set the IPI
	 * to PQ=10 state. Otherwise we just re-enable it (PQ=00).
	 */
	if (prio == MASKED || state->old_p)
		xive_vm_esb_load(&state->ipi_data, XIVE_ESB_SET_PQ_10);
	else
		xive_vm_esb_load(&state->ipi_data, XIVE_ESB_SET_PQ_00);

	/* Restore guest prio (unlocks EOI) */
	mb();
	state->guest_priority = prio;
	arch_spin_unlock(&sb->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(kvmppc_xive_clr_mapped);

void kvmppc_xive_disable_vcpu_interrupts(struct kvm_vcpu *vcpu)
{
	struct kvmppc_xive_vcpu *xc = vcpu->arch.xive_vcpu;
	struct kvm *kvm = vcpu->kvm;
	struct kvmppc_xive *xive = kvm->arch.xive;
	int i, j;

	for (i = 0; i <= xive->max_sbid; i++) {
		struct kvmppc_xive_src_block *sb = xive->src_blocks[i];

		if (!sb)
			continue;
		for (j = 0; j < KVMPPC_XICS_IRQ_PER_ICS; j++) {
			struct kvmppc_xive_irq_state *state = &sb->irq_state[j];

			if (!state->valid)
				continue;
			if (state->act_priority == MASKED)
				continue;
			if (state->act_server != xc->server_num)
				continue;

			/* Clean it up */
			arch_spin_lock(&sb->lock);
			state->act_priority = MASKED;
			xive_vm_esb_load(&state->ipi_data, XIVE_ESB_SET_PQ_01);
			xive_native_configure_irq(state->ipi_number, 0, MASKED, 0);
			if (state->pt_number) {
				xive_vm_esb_load(state->pt_data, XIVE_ESB_SET_PQ_01);
				xive_native_configure_irq(state->pt_number, 0, MASKED, 0);
			}
			arch_spin_unlock(&sb->lock);
		}
	}

	/* Disable vcpu's escalation interrupt */
	if (vcpu->arch.xive_esc_on) {
		__raw_readq((void __iomem *)(vcpu->arch.xive_esc_vaddr +
					     XIVE_ESB_SET_PQ_01));
		vcpu->arch.xive_esc_on = false;
	}

	/*
	 * Clear pointers to escalation interrupt ESB.
	 * This is safe because the vcpu->mutex is held, preventing
	 * any other CPU from concurrently executing a KVM_RUN ioctl.
	 */
	vcpu->arch.xive_esc_vaddr = 0;
	vcpu->arch.xive_esc_raddr = 0;
}

void kvmppc_xive_cleanup_vcpu(struct kvm_vcpu *vcpu)
{
	struct kvmppc_xive_vcpu *xc = vcpu->arch.xive_vcpu;
	struct kvmppc_xive *xive = vcpu->kvm->arch.xive;
	int i;

	if (!kvmppc_xics_enabled(vcpu))
		return;

	if (!xc)
		return;

	pr_devel("cleanup_vcpu(cpu=%d)\n", xc->server_num);

	/* Ensure no interrupt is still routed to that VP */
	xc->valid = false;
	kvmppc_xive_disable_vcpu_interrupts(vcpu);

	/* Mask the VP IPI */
	xive_vm_esb_load(&xc->vp_ipi_data, XIVE_ESB_SET_PQ_01);

	/* Disable the VP */
	xive_native_disable_vp(xc->vp_id);

	/* Free the queues & associated interrupts */
	for (i = 0; i < KVMPPC_XIVE_Q_COUNT; i++) {
		struct xive_q *q = &xc->queues[i];

		/* Free the escalation irq */
		if (xc->esc_virq[i]) {
			free_irq(xc->esc_virq[i], vcpu);
			irq_dispose_mapping(xc->esc_virq[i]);
			kfree(xc->esc_virq_names[i]);
		}
		/* Free the queue */
		xive_native_disable_queue(xc->vp_id, q, i);
		if (q->qpage) {
			free_pages((unsigned long)q->qpage,
				   xive->q_page_order);
			q->qpage = NULL;
		}
	}

	/* Free the IPI */
	if (xc->vp_ipi) {
		xive_cleanup_irq_data(&xc->vp_ipi_data);
		xive_native_free_irq(xc->vp_ipi);
	}
	/* Free the VP */
	kfree(xc);

	/* Cleanup the vcpu */
	vcpu->arch.irq_type = KVMPPC_IRQ_DEFAULT;
	vcpu->arch.xive_vcpu = NULL;
}

int kvmppc_xive_connect_vcpu(struct kvm_device *dev,
			     struct kvm_vcpu *vcpu, u32 cpu)
{
	struct kvmppc_xive *xive = dev->private;
	struct kvmppc_xive_vcpu *xc;
	int i, r = -EBUSY;

	pr_devel("connect_vcpu(cpu=%d)\n", cpu);

	if (dev->ops != &kvm_xive_ops) {
		pr_devel("Wrong ops !\n");
		return -EPERM;
	}
	if (xive->kvm != vcpu->kvm)
		return -EPERM;
	if (vcpu->arch.irq_type != KVMPPC_IRQ_DEFAULT)
		return -EBUSY;
	if (kvmppc_xive_find_server(vcpu->kvm, cpu)) {
		pr_devel("Duplicate !\n");
		return -EEXIST;
	}
	if (cpu >= (KVM_MAX_VCPUS * vcpu->kvm->arch.emul_smt_mode)) {
		pr_devel("Out of bounds !\n");
		return -EINVAL;
	}
	xc = kzalloc(sizeof(*xc), GFP_KERNEL);
	if (!xc)
		return -ENOMEM;

	/* We need to synchronize with queue provisioning */
	mutex_lock(&xive->lock);
	vcpu->arch.xive_vcpu = xc;
	xc->xive = xive;
	xc->vcpu = vcpu;
	xc->server_num = cpu;
	xc->vp_id = kvmppc_xive_vp(xive, cpu);
	xc->mfrr = 0xff;
	xc->valid = true;

	r = xive_native_get_vp_info(xc->vp_id, &xc->vp_cam, &xc->vp_chip_id);
	if (r)
		goto bail;

	/* Configure VCPU fields for use by assembly push/pull */
	vcpu->arch.xive_saved_state.w01 = cpu_to_be64(0xff000000);
	vcpu->arch.xive_cam_word = cpu_to_be32(xc->vp_cam | TM_QW1W2_VO);

	/* Allocate IPI */
	xc->vp_ipi = xive_native_alloc_irq();
	if (!xc->vp_ipi) {
		pr_err("Failed to allocate xive irq for VCPU IPI\n");
		r = -EIO;
		goto bail;
	}
	pr_devel(" IPI=0x%x\n", xc->vp_ipi);

	r = xive_native_populate_irq_data(xc->vp_ipi, &xc->vp_ipi_data);
	if (r)
		goto bail;

	/*
	 * Enable the VP first as the single escalation mode will
	 * affect escalation interrupts numbering
	 */
	r = xive_native_enable_vp(xc->vp_id, xive->single_escalation);
	if (r) {
		pr_err("Failed to enable VP in OPAL, err %d\n", r);
		goto bail;
	}

	/*
	 * Initialize queues. Initially we set them all for no queueing
	 * and we enable escalation for queue 0 only which we'll use for
	 * our mfrr change notifications. If the VCPU is hot-plugged, we
	 * do handle provisioning however based on the existing "map"
	 * of enabled queues.
	 */
	for (i = 0; i < KVMPPC_XIVE_Q_COUNT; i++) {
		struct xive_q *q = &xc->queues[i];

		/* Single escalation, no queue 7 */
		if (i == 7 && xive->single_escalation)
			break;

		/* Is queue already enabled ? Provision it */
		if (xive->qmap & (1 << i)) {
			r = xive_provision_queue(vcpu, i);
			if (r == 0 && !xive->single_escalation)
				kvmppc_xive_attach_escalation(
					vcpu, i, xive->single_escalation);
			if (r)
				goto bail;
		} else {
			r = xive_native_configure_queue(xc->vp_id,
							q, i, NULL, 0, true);
			if (r) {
				pr_err("Failed to configure queue %d for VCPU %d\n",
				       i, cpu);
				goto bail;
			}
		}
	}

	/* If not done above, attach priority 0 escalation */
	r = kvmppc_xive_attach_escalation(vcpu, 0, xive->single_escalation);
	if (r)
		goto bail;

	/* Route the IPI */
	r = xive_native_configure_irq(xc->vp_ipi, xc->vp_id, 0, XICS_IPI);
	if (!r)
		xive_vm_esb_load(&xc->vp_ipi_data, XIVE_ESB_SET_PQ_00);

bail:
	mutex_unlock(&xive->lock);
	if (r) {
		kvmppc_xive_cleanup_vcpu(vcpu);
		return r;
	}

	vcpu->arch.irq_type = KVMPPC_IRQ_XICS;
	return 0;
}

/*
 * Scanning of queues before/after migration save
 */
static void xive_pre_save_set_queued(struct kvmppc_xive *xive, u32 irq)
{
	struct kvmppc_xive_src_block *sb;
	struct kvmppc_xive_irq_state *state;
	u16 idx;

	sb = kvmppc_xive_find_source(xive, irq, &idx);
	if (!sb)
		return;

	state = &sb->irq_state[idx];

	/* Some sanity checking */
	if (!state->valid) {
		pr_err("invalid irq 0x%x in cpu queue!\n", irq);
		return;
	}

	/*
	 * If the interrupt is in a queue it should have P set.
	 * We warn so that gets reported. A backtrace isn't useful
	 * so no need to use a WARN_ON.
	 */
	if (!state->saved_p)
		pr_err("Interrupt 0x%x is marked in a queue but P not set !\n", irq);

	/* Set flag */
	state->in_queue = true;
}

static void xive_pre_save_mask_irq(struct kvmppc_xive *xive,
				   struct kvmppc_xive_src_block *sb,
				   u32 irq)
{
	struct kvmppc_xive_irq_state *state = &sb->irq_state[irq];

	if (!state->valid)
		return;

	/* Mask and save state, this will also sync HW queues */
	state->saved_scan_prio = xive_lock_and_mask(xive, sb, state);

	/* Transfer P and Q */
	state->saved_p = state->old_p;
	state->saved_q = state->old_q;

	/* Unlock */
	arch_spin_unlock(&sb->lock);
}

static void xive_pre_save_unmask_irq(struct kvmppc_xive *xive,
				     struct kvmppc_xive_src_block *sb,
				     u32 irq)
{
	struct kvmppc_xive_irq_state *state = &sb->irq_state[irq];

	if (!state->valid)
		return;

	/*
	 * Lock / exclude EOI (not technically necessary if the
	 * guest isn't running concurrently. If this becomes a
	 * performance issue we can probably remove the lock.
	 */
	xive_lock_for_unmask(sb, state);

	/* Restore mask/prio if it wasn't masked */
	if (state->saved_scan_prio != MASKED)
		xive_finish_unmask(xive, sb, state, state->saved_scan_prio);

	/* Unlock */
	arch_spin_unlock(&sb->lock);
}

static void xive_pre_save_queue(struct kvmppc_xive *xive, struct xive_q *q)
{
	u32 idx = q->idx;
	u32 toggle = q->toggle;
	u32 irq;

	do {
		irq = __xive_read_eq(q->qpage, q->msk, &idx, &toggle);
		if (irq > XICS_IPI)
			xive_pre_save_set_queued(xive, irq);
	} while(irq);
}

static void xive_pre_save_scan(struct kvmppc_xive *xive)
{
	struct kvm_vcpu *vcpu = NULL;
	int i, j;

	/*
	 * See comment in xive_get_source() about how this
	 * work. Collect a stable state for all interrupts
	 */
	for (i = 0; i <= xive->max_sbid; i++) {
		struct kvmppc_xive_src_block *sb = xive->src_blocks[i];
		if (!sb)
			continue;
		for (j = 0;  j < KVMPPC_XICS_IRQ_PER_ICS; j++)
			xive_pre_save_mask_irq(xive, sb, j);
	}

	/* Then scan the queues and update the "in_queue" flag */
	kvm_for_each_vcpu(i, vcpu, xive->kvm) {
		struct kvmppc_xive_vcpu *xc = vcpu->arch.xive_vcpu;
		if (!xc)
			continue;
		for (j = 0; j < KVMPPC_XIVE_Q_COUNT; j++) {
			if (xc->queues[j].qpage)
				xive_pre_save_queue(xive, &xc->queues[j]);
		}
	}

	/* Finally restore interrupt states */
	for (i = 0; i <= xive->max_sbid; i++) {
		struct kvmppc_xive_src_block *sb = xive->src_blocks[i];
		if (!sb)
			continue;
		for (j = 0;  j < KVMPPC_XICS_IRQ_PER_ICS; j++)
			xive_pre_save_unmask_irq(xive, sb, j);
	}
}

static void xive_post_save_scan(struct kvmppc_xive *xive)
{
	u32 i, j;

	/* Clear all the in_queue flags */
	for (i = 0; i <= xive->max_sbid; i++) {
		struct kvmppc_xive_src_block *sb = xive->src_blocks[i];
		if (!sb)
			continue;
		for (j = 0;  j < KVMPPC_XICS_IRQ_PER_ICS; j++)
			sb->irq_state[j].in_queue = false;
	}

	/* Next get_source() will do a new scan */
	xive->saved_src_count = 0;
}

/*
 * This returns the source configuration and state to user space.
 */
static int xive_get_source(struct kvmppc_xive *xive, long irq, u64 addr)
{
	struct kvmppc_xive_src_block *sb;
	struct kvmppc_xive_irq_state *state;
	u64 __user *ubufp = (u64 __user *) addr;
	u64 val, prio;
	u16 idx;

	sb = kvmppc_xive_find_source(xive, irq, &idx);
	if (!sb)
		return -ENOENT;

	state = &sb->irq_state[idx];

	if (!state->valid)
		return -ENOENT;

	pr_devel("get_source(%ld)...\n", irq);

	/*
	 * So to properly save the state into something that looks like a
	 * XICS migration stream we cannot treat interrupts individually.
	 *
	 * We need, instead, mask them all (& save their previous PQ state)
	 * to get a stable state in the HW, then sync them to ensure that
	 * any interrupt that had already fired hits its queue, and finally
	 * scan all the queues to collect which interrupts are still present
	 * in the queues, so we can set the "pending" flag on them and
	 * they can be resent on restore.
	 *
	 * So we do it all when the "first" interrupt gets saved, all the
	 * state is collected at that point, the rest of xive_get_source()
	 * will merely collect and convert that state to the expected
	 * userspace bit mask.
	 */
	if (xive->saved_src_count == 0)
		xive_pre_save_scan(xive);
	xive->saved_src_count++;

	/* Convert saved state into something compatible with xics */
	val = state->act_server;
	prio = state->saved_scan_prio;

	if (prio == MASKED) {
		val |= KVM_XICS_MASKED;
		prio = state->saved_priority;
	}
	val |= prio << KVM_XICS_PRIORITY_SHIFT;
	if (state->lsi) {
		val |= KVM_XICS_LEVEL_SENSITIVE;
		if (state->saved_p)
			val |= KVM_XICS_PENDING;
	} else {
		if (state->saved_p)
			val |= KVM_XICS_PRESENTED;

		if (state->saved_q)
			val |= KVM_XICS_QUEUED;

		/*
		 * We mark it pending (which will attempt a re-delivery)
		 * if we are in a queue *or* we were masked and had
		 * Q set which is equivalent to the XICS "masked pending"
		 * state
		 */
		if (state->in_queue || (prio == MASKED && state->saved_q))
			val |= KVM_XICS_PENDING;
	}

	/*
	 * If that was the last interrupt saved, reset the
	 * in_queue flags
	 */
	if (xive->saved_src_count == xive->src_count)
		xive_post_save_scan(xive);

	/* Copy the result to userspace */
	if (put_user(val, ubufp))
		return -EFAULT;

	return 0;
}

struct kvmppc_xive_src_block *kvmppc_xive_create_src_block(
	struct kvmppc_xive *xive, int irq)
{
	struct kvmppc_xive_src_block *sb;
	int i, bid;

	bid = irq >> KVMPPC_XICS_ICS_SHIFT;

	mutex_lock(&xive->lock);

	/* block already exists - somebody else got here first */
	if (xive->src_blocks[bid])
		goto out;

	/* Create the ICS */
	sb = kzalloc(sizeof(*sb), GFP_KERNEL);
	if (!sb)
		goto out;

	sb->id = bid;

	for (i = 0; i < KVMPPC_XICS_IRQ_PER_ICS; i++) {
		sb->irq_state[i].number = (bid << KVMPPC_XICS_ICS_SHIFT) | i;
		sb->irq_state[i].eisn = 0;
		sb->irq_state[i].guest_priority = MASKED;
		sb->irq_state[i].saved_priority = MASKED;
		sb->irq_state[i].act_priority = MASKED;
	}
	smp_wmb();
	xive->src_blocks[bid] = sb;

	if (bid > xive->max_sbid)
		xive->max_sbid = bid;

out:
	mutex_unlock(&xive->lock);
	return xive->src_blocks[bid];
}

static bool xive_check_delayed_irq(struct kvmppc_xive *xive, u32 irq)
{
	struct kvm *kvm = xive->kvm;
	struct kvm_vcpu *vcpu = NULL;
	int i;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		struct kvmppc_xive_vcpu *xc = vcpu->arch.xive_vcpu;

		if (!xc)
			continue;

		if (xc->delayed_irq == irq) {
			xc->delayed_irq = 0;
			xive->delayed_irqs--;
			return true;
		}
	}
	return false;
}

static int xive_set_source(struct kvmppc_xive *xive, long irq, u64 addr)
{
	struct kvmppc_xive_src_block *sb;
	struct kvmppc_xive_irq_state *state;
	u64 __user *ubufp = (u64 __user *) addr;
	u16 idx;
	u64 val;
	u8 act_prio, guest_prio;
	u32 server;
	int rc = 0;

	if (irq < KVMPPC_XICS_FIRST_IRQ || irq >= KVMPPC_XICS_NR_IRQS)
		return -ENOENT;

	pr_devel("set_source(irq=0x%lx)\n", irq);

	/* Find the source */
	sb = kvmppc_xive_find_source(xive, irq, &idx);
	if (!sb) {
		pr_devel("No source, creating source block...\n");
		sb = kvmppc_xive_create_src_block(xive, irq);
		if (!sb) {
			pr_devel("Failed to create block...\n");
			return -ENOMEM;
		}
	}
	state = &sb->irq_state[idx];

	/* Read user passed data */
	if (get_user(val, ubufp)) {
		pr_devel("fault getting user info !\n");
		return -EFAULT;
	}

	server = val & KVM_XICS_DESTINATION_MASK;
	guest_prio = val >> KVM_XICS_PRIORITY_SHIFT;

	pr_devel("  val=0x016%llx (server=0x%x, guest_prio=%d)\n",
		 val, server, guest_prio);

	/*
	 * If the source doesn't already have an IPI, allocate
	 * one and get the corresponding data
	 */
	if (!state->ipi_number) {
		state->ipi_number = xive_native_alloc_irq();
		if (state->ipi_number == 0) {
			pr_devel("Failed to allocate IPI !\n");
			return -ENOMEM;
		}
		xive_native_populate_irq_data(state->ipi_number, &state->ipi_data);
		pr_devel(" src_ipi=0x%x\n", state->ipi_number);
	}

	/*
	 * We use lock_and_mask() to set us in the right masked
	 * state. We will override that state from the saved state
	 * further down, but this will handle the cases of interrupts
	 * that need FW masking. We set the initial guest_priority to
	 * 0 before calling it to ensure it actually performs the masking.
	 */
	state->guest_priority = 0;
	xive_lock_and_mask(xive, sb, state);

	/*
	 * Now, we select a target if we have one. If we don't we
	 * leave the interrupt untargetted. It means that an interrupt
	 * can become "untargetted" accross migration if it was masked
	 * by set_xive() but there is little we can do about it.
	 */

	/* First convert prio and mark interrupt as untargetted */
	act_prio = xive_prio_from_guest(guest_prio);
	state->act_priority = MASKED;

	/*
	 * We need to drop the lock due to the mutex below. Hopefully
	 * nothing is touching that interrupt yet since it hasn't been
	 * advertized to a running guest yet
	 */
	arch_spin_unlock(&sb->lock);

	/* If we have a priority target the interrupt */
	if (act_prio != MASKED) {
		/* First, check provisioning of queues */
		mutex_lock(&xive->lock);
		rc = xive_check_provisioning(xive->kvm, act_prio);
		mutex_unlock(&xive->lock);

		/* Target interrupt */
		if (rc == 0)
			rc = xive_target_interrupt(xive->kvm, state,
						   server, act_prio);
		/*
		 * If provisioning or targetting failed, leave it
		 * alone and masked. It will remain disabled until
		 * the guest re-targets it.
		 */
	}

	/*
	 * Find out if this was a delayed irq stashed in an ICP,
	 * in which case, treat it as pending
	 */
	if (xive->delayed_irqs && xive_check_delayed_irq(xive, irq)) {
		val |= KVM_XICS_PENDING;
		pr_devel("  Found delayed ! forcing PENDING !\n");
	}

	/* Cleanup the SW state */
	state->old_p = false;
	state->old_q = false;
	state->lsi = false;
	state->asserted = false;

	/* Restore LSI state */
	if (val & KVM_XICS_LEVEL_SENSITIVE) {
		state->lsi = true;
		if (val & KVM_XICS_PENDING)
			state->asserted = true;
		pr_devel("  LSI ! Asserted=%d\n", state->asserted);
	}

	/*
	 * Restore P and Q. If the interrupt was pending, we
	 * force Q and !P, which will trigger a resend.
	 *
	 * That means that a guest that had both an interrupt
	 * pending (queued) and Q set will restore with only
	 * one instance of that interrupt instead of 2, but that
	 * is perfectly fine as coalescing interrupts that haven't
	 * been presented yet is always allowed.
	 */
	if (val & KVM_XICS_PRESENTED && !(val & KVM_XICS_PENDING))
		state->old_p = true;
	if (val & KVM_XICS_QUEUED || val & KVM_XICS_PENDING)
		state->old_q = true;

	pr_devel("  P=%d, Q=%d\n", state->old_p, state->old_q);

	/*
	 * If the interrupt was unmasked, update guest priority and
	 * perform the appropriate state transition and do a
	 * re-trigger if necessary.
	 */
	if (val & KVM_XICS_MASKED) {
		pr_devel("  masked, saving prio\n");
		state->guest_priority = MASKED;
		state->saved_priority = guest_prio;
	} else {
		pr_devel("  unmasked, restoring to prio %d\n", guest_prio);
		xive_finish_unmask(xive, sb, state, guest_prio);
		state->saved_priority = guest_prio;
	}

	/* Increment the number of valid sources and mark this one valid */
	if (!state->valid)
		xive->src_count++;
	state->valid = true;

	return 0;
}

int kvmppc_xive_set_irq(struct kvm *kvm, int irq_source_id, u32 irq, int level,
			bool line_status)
{
	struct kvmppc_xive *xive = kvm->arch.xive;
	struct kvmppc_xive_src_block *sb;
	struct kvmppc_xive_irq_state *state;
	u16 idx;

	if (!xive)
		return -ENODEV;

	sb = kvmppc_xive_find_source(xive, irq, &idx);
	if (!sb)
		return -EINVAL;

	/* Perform locklessly .... (we need to do some RCUisms here...) */
	state = &sb->irq_state[idx];
	if (!state->valid)
		return -EINVAL;

	/* We don't allow a trigger on a passed-through interrupt */
	if (state->pt_number)
		return -EINVAL;

	if ((level == 1 && state->lsi) || level == KVM_INTERRUPT_SET_LEVEL)
		state->asserted = 1;
	else if (level == 0 || level == KVM_INTERRUPT_UNSET) {
		state->asserted = 0;
		return 0;
	}

	/* Trigger the IPI */
	xive_irq_trigger(&state->ipi_data);

	return 0;
}

static int xive_set_attr(struct kvm_device *dev, struct kvm_device_attr *attr)
{
	struct kvmppc_xive *xive = dev->private;

	/* We honor the existing XICS ioctl */
	switch (attr->group) {
	case KVM_DEV_XICS_GRP_SOURCES:
		return xive_set_source(xive, attr->attr, attr->addr);
	}
	return -ENXIO;
}

static int xive_get_attr(struct kvm_device *dev, struct kvm_device_attr *attr)
{
	struct kvmppc_xive *xive = dev->private;

	/* We honor the existing XICS ioctl */
	switch (attr->group) {
	case KVM_DEV_XICS_GRP_SOURCES:
		return xive_get_source(xive, attr->attr, attr->addr);
	}
	return -ENXIO;
}

static int xive_has_attr(struct kvm_device *dev, struct kvm_device_attr *attr)
{
	/* We honor the same limits as XICS, at least for now */
	switch (attr->group) {
	case KVM_DEV_XICS_GRP_SOURCES:
		if (attr->attr >= KVMPPC_XICS_FIRST_IRQ &&
		    attr->attr < KVMPPC_XICS_NR_IRQS)
			return 0;
		break;
	}
	return -ENXIO;
}

static void kvmppc_xive_cleanup_irq(u32 hw_num, struct xive_irq_data *xd)
{
	xive_vm_esb_load(xd, XIVE_ESB_SET_PQ_01);
	xive_native_configure_irq(hw_num, 0, MASKED, 0);
}

void kvmppc_xive_free_sources(struct kvmppc_xive_src_block *sb)
{
	int i;

	for (i = 0; i < KVMPPC_XICS_IRQ_PER_ICS; i++) {
		struct kvmppc_xive_irq_state *state = &sb->irq_state[i];

		if (!state->valid)
			continue;

		kvmppc_xive_cleanup_irq(state->ipi_number, &state->ipi_data);
		xive_cleanup_irq_data(&state->ipi_data);
		xive_native_free_irq(state->ipi_number);

		/* Pass-through, cleanup too but keep IRQ hw data */
		if (state->pt_number)
			kvmppc_xive_cleanup_irq(state->pt_number, state->pt_data);

		state->valid = false;
	}
}

/*
 * Called when device fd is closed.  kvm->lock is held.
 */
static void kvmppc_xive_release(struct kvm_device *dev)
{
	struct kvmppc_xive *xive = dev->private;
	struct kvm *kvm = xive->kvm;
	struct kvm_vcpu *vcpu;
	int i;

	pr_devel("Releasing xive device\n");

	/*
	 * Since this is the device release function, we know that
	 * userspace does not have any open fd referring to the
	 * device.  Therefore there can not be any of the device
	 * attribute set/get functions being executed concurrently,
	 * and similarly, the connect_vcpu and set/clr_mapped
	 * functions also cannot be being executed.
	 */

	debugfs_remove(xive->dentry);

	/*
	 * We should clean up the vCPU interrupt presenters first.
	 */
	kvm_for_each_vcpu(i, vcpu, kvm) {
		/*
		 * Take vcpu->mutex to ensure that no one_reg get/set ioctl
		 * (i.e. kvmppc_xive_[gs]et_icp) can be done concurrently.
		 * Holding the vcpu->mutex also means that the vcpu cannot
		 * be executing the KVM_RUN ioctl, and therefore it cannot
		 * be executing the XIVE push or pull code or accessing
		 * the XIVE MMIO regions.
		 */
		mutex_lock(&vcpu->mutex);
		kvmppc_xive_cleanup_vcpu(vcpu);
		mutex_unlock(&vcpu->mutex);
	}

	/*
	 * Now that we have cleared vcpu->arch.xive_vcpu, vcpu->arch.irq_type
	 * and vcpu->arch.xive_esc_[vr]addr on each vcpu, we are safe
	 * against xive code getting called during vcpu execution or
	 * set/get one_reg operations.
	 */
	kvm->arch.xive = NULL;

	/* Mask and free interrupts */
	for (i = 0; i <= xive->max_sbid; i++) {
		if (xive->src_blocks[i])
			kvmppc_xive_free_sources(xive->src_blocks[i]);
		kfree(xive->src_blocks[i]);
		xive->src_blocks[i] = NULL;
	}

	if (xive->vp_base != XIVE_INVALID_VP)
		xive_native_free_vp_block(xive->vp_base);

	/*
	 * A reference of the kvmppc_xive pointer is now kept under
	 * the xive_devices struct of the machine for reuse. It is
	 * freed when the VM is destroyed for now until we fix all the
	 * execution paths.
	 */

	kfree(dev);
}

/*
 * When the guest chooses the interrupt mode (XICS legacy or XIVE
 * native), the VM will switch of KVM device. The previous device will
 * be "released" before the new one is created.
 *
 * Until we are sure all execution paths are well protected, provide a
 * fail safe (transitional) method for device destruction, in which
 * the XIVE device pointer is recycled and not directly freed.
 */
struct kvmppc_xive *kvmppc_xive_get_device(struct kvm *kvm, u32 type)
{
	struct kvmppc_xive **kvm_xive_device = type == KVM_DEV_TYPE_XIVE ?
		&kvm->arch.xive_devices.native :
		&kvm->arch.xive_devices.xics_on_xive;
	struct kvmppc_xive *xive = *kvm_xive_device;

	if (!xive) {
		xive = kzalloc(sizeof(*xive), GFP_KERNEL);
		*kvm_xive_device = xive;
	} else {
		memset(xive, 0, sizeof(*xive));
	}

	return xive;
}

/*
 * Create a XICS device with XIVE backend.  kvm->lock is held.
 */
static int kvmppc_xive_create(struct kvm_device *dev, u32 type)
{
	struct kvmppc_xive *xive;
	struct kvm *kvm = dev->kvm;
	int ret = 0;

	pr_devel("Creating xive for partition\n");

	xive = kvmppc_xive_get_device(kvm, type);
	if (!xive)
		return -ENOMEM;

	dev->private = xive;
	xive->dev = dev;
	xive->kvm = kvm;
	mutex_init(&xive->lock);

	/* Already there ? */
	if (kvm->arch.xive)
		ret = -EEXIST;
	else
		kvm->arch.xive = xive;

	/* We use the default queue size set by the host */
	xive->q_order = xive_native_default_eq_shift();
	if (xive->q_order < PAGE_SHIFT)
		xive->q_page_order = 0;
	else
		xive->q_page_order = xive->q_order - PAGE_SHIFT;

	/* Allocate a bunch of VPs */
	xive->vp_base = xive_native_alloc_vp_block(KVM_MAX_VCPUS);
	pr_devel("VP_Base=%x\n", xive->vp_base);

	if (xive->vp_base == XIVE_INVALID_VP)
		ret = -ENOMEM;

	xive->single_escalation = xive_native_has_single_escalation();

	if (ret)
		return ret;

	return 0;
}

int kvmppc_xive_debug_show_queues(struct seq_file *m, struct kvm_vcpu *vcpu)
{
	struct kvmppc_xive_vcpu *xc = vcpu->arch.xive_vcpu;
	unsigned int i;

	for (i = 0; i < KVMPPC_XIVE_Q_COUNT; i++) {
		struct xive_q *q = &xc->queues[i];
		u32 i0, i1, idx;

		if (!q->qpage && !xc->esc_virq[i])
			continue;

		seq_printf(m, " [q%d]: ", i);

		if (q->qpage) {
			idx = q->idx;
			i0 = be32_to_cpup(q->qpage + idx);
			idx = (idx + 1) & q->msk;
			i1 = be32_to_cpup(q->qpage + idx);
			seq_printf(m, "T=%d %08x %08x...\n", q->toggle,
				   i0, i1);
		}
		if (xc->esc_virq[i]) {
			struct irq_data *d = irq_get_irq_data(xc->esc_virq[i]);
			struct xive_irq_data *xd =
				irq_data_get_irq_handler_data(d);
			u64 pq = xive_vm_esb_load(xd, XIVE_ESB_GET);

			seq_printf(m, "E:%c%c I(%d:%llx:%llx)",
				   (pq & XIVE_ESB_VAL_P) ? 'P' : 'p',
				   (pq & XIVE_ESB_VAL_Q) ? 'Q' : 'q',
				   xc->esc_virq[i], pq, xd->eoi_page);
			seq_puts(m, "\n");
		}
	}
	return 0;
}

static int xive_debug_show(struct seq_file *m, void *private)
{
	struct kvmppc_xive *xive = m->private;
	struct kvm *kvm = xive->kvm;
	struct kvm_vcpu *vcpu;
	u64 t_rm_h_xirr = 0;
	u64 t_rm_h_ipoll = 0;
	u64 t_rm_h_cppr = 0;
	u64 t_rm_h_eoi = 0;
	u64 t_rm_h_ipi = 0;
	u64 t_vm_h_xirr = 0;
	u64 t_vm_h_ipoll = 0;
	u64 t_vm_h_cppr = 0;
	u64 t_vm_h_eoi = 0;
	u64 t_vm_h_ipi = 0;
	unsigned int i;

	if (!kvm)
		return 0;

	seq_printf(m, "=========\nVCPU state\n=========\n");

	kvm_for_each_vcpu(i, vcpu, kvm) {
		struct kvmppc_xive_vcpu *xc = vcpu->arch.xive_vcpu;

		if (!xc)
			continue;

		seq_printf(m, "cpu server %#x CPPR:%#x HWCPPR:%#x"
			   " MFRR:%#x PEND:%#x h_xirr: R=%lld V=%lld\n",
			   xc->server_num, xc->cppr, xc->hw_cppr,
			   xc->mfrr, xc->pending,
			   xc->stat_rm_h_xirr, xc->stat_vm_h_xirr);

		kvmppc_xive_debug_show_queues(m, vcpu);

		t_rm_h_xirr += xc->stat_rm_h_xirr;
		t_rm_h_ipoll += xc->stat_rm_h_ipoll;
		t_rm_h_cppr += xc->stat_rm_h_cppr;
		t_rm_h_eoi += xc->stat_rm_h_eoi;
		t_rm_h_ipi += xc->stat_rm_h_ipi;
		t_vm_h_xirr += xc->stat_vm_h_xirr;
		t_vm_h_ipoll += xc->stat_vm_h_ipoll;
		t_vm_h_cppr += xc->stat_vm_h_cppr;
		t_vm_h_eoi += xc->stat_vm_h_eoi;
		t_vm_h_ipi += xc->stat_vm_h_ipi;
	}

	seq_printf(m, "Hcalls totals\n");
	seq_printf(m, " H_XIRR  R=%10lld V=%10lld\n", t_rm_h_xirr, t_vm_h_xirr);
	seq_printf(m, " H_IPOLL R=%10lld V=%10lld\n", t_rm_h_ipoll, t_vm_h_ipoll);
	seq_printf(m, " H_CPPR  R=%10lld V=%10lld\n", t_rm_h_cppr, t_vm_h_cppr);
	seq_printf(m, " H_EOI   R=%10lld V=%10lld\n", t_rm_h_eoi, t_vm_h_eoi);
	seq_printf(m, " H_IPI   R=%10lld V=%10lld\n", t_rm_h_ipi, t_vm_h_ipi);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(xive_debug);

static void xive_debugfs_init(struct kvmppc_xive *xive)
{
	char *name;

	name = kasprintf(GFP_KERNEL, "kvm-xive-%p", xive);
	if (!name) {
		pr_err("%s: no memory for name\n", __func__);
		return;
	}

	xive->dentry = debugfs_create_file(name, S_IRUGO, powerpc_debugfs_root,
					   xive, &xive_debug_fops);

	pr_debug("%s: created %s\n", __func__, name);
	kfree(name);
}

static void kvmppc_xive_init(struct kvm_device *dev)
{
	struct kvmppc_xive *xive = (struct kvmppc_xive *)dev->private;

	/* Register some debug interfaces */
	xive_debugfs_init(xive);
}

struct kvm_device_ops kvm_xive_ops = {
	.name = "kvm-xive",
	.create = kvmppc_xive_create,
	.init = kvmppc_xive_init,
	.release = kvmppc_xive_release,
	.set_attr = xive_set_attr,
	.get_attr = xive_get_attr,
	.has_attr = xive_has_attr,
};

void kvmppc_xive_init_module(void)
{
	__xive_vm_h_xirr = xive_vm_h_xirr;
	__xive_vm_h_ipoll = xive_vm_h_ipoll;
	__xive_vm_h_ipi = xive_vm_h_ipi;
	__xive_vm_h_cppr = xive_vm_h_cppr;
	__xive_vm_h_eoi = xive_vm_h_eoi;
}

void kvmppc_xive_exit_module(void)
{
	__xive_vm_h_xirr = NULL;
	__xive_vm_h_ipoll = NULL;
	__xive_vm_h_ipi = NULL;
	__xive_vm_h_cppr = NULL;
	__xive_vm_h_eoi = NULL;
}
