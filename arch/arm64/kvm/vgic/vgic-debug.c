// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Linaro
 * Author: Christoffer Dall <christoffer.dall@linaro.org>
 */

#include <linux/cpu.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/kvm_host.h>
#include <linux/seq_file.h>
#include <kvm/arm_vgic.h>
#include <asm/kvm_mmu.h>
#include "vgic.h"

/*
 * Structure to control looping through the entire vgic state.  We start at
 * zero for each field and move upwards.  So, if dist_id is 0 we print the
 * distributor info.  When dist_id is 1, we have already printed it and move
 * on.
 *
 * When vcpu_id < nr_cpus we print the vcpu info until vcpu_id == nr_cpus and
 * so on.
 */
struct vgic_state_iter {
	int nr_cpus;
	int nr_spis;
	int nr_lpis;
	int dist_id;
	int vcpu_id;
	unsigned long intid;
	int lpi_idx;
};

static void iter_next(struct kvm *kvm, struct vgic_state_iter *iter)
{
	struct vgic_dist *dist = &kvm->arch.vgic;

	if (iter->dist_id == 0) {
		iter->dist_id++;
		return;
	}

	/*
	 * Let the xarray drive the iterator after the last SPI, as the iterator
	 * has exhausted the sequentially-allocated INTID space.
	 */
	if (iter->intid >= (iter->nr_spis + VGIC_NR_PRIVATE_IRQS - 1) &&
	    iter->nr_lpis) {
		if (iter->lpi_idx < iter->nr_lpis)
			xa_find_after(&dist->lpi_xa, &iter->intid,
				      VGIC_LPI_MAX_INTID,
				      LPI_XA_MARK_DEBUG_ITER);
		iter->lpi_idx++;
		return;
	}

	iter->intid++;
	if (iter->intid == VGIC_NR_PRIVATE_IRQS &&
	    ++iter->vcpu_id < iter->nr_cpus)
		iter->intid = 0;
}

static int iter_mark_lpis(struct kvm *kvm)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	struct vgic_irq *irq;
	unsigned long intid;
	int nr_lpis = 0;

	xa_for_each(&dist->lpi_xa, intid, irq) {
		if (!vgic_try_get_irq_kref(irq))
			continue;

		xa_set_mark(&dist->lpi_xa, intid, LPI_XA_MARK_DEBUG_ITER);
		nr_lpis++;
	}

	return nr_lpis;
}

static void iter_unmark_lpis(struct kvm *kvm)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	struct vgic_irq *irq;
	unsigned long intid;

	xa_for_each_marked(&dist->lpi_xa, intid, irq, LPI_XA_MARK_DEBUG_ITER) {
		xa_clear_mark(&dist->lpi_xa, intid, LPI_XA_MARK_DEBUG_ITER);
		vgic_put_irq(kvm, irq);
	}
}

static void iter_init(struct kvm *kvm, struct vgic_state_iter *iter,
		      loff_t pos)
{
	int nr_cpus = atomic_read(&kvm->online_vcpus);

	memset(iter, 0, sizeof(*iter));

	iter->nr_cpus = nr_cpus;
	iter->nr_spis = kvm->arch.vgic.nr_spis;
	if (kvm->arch.vgic.vgic_model == KVM_DEV_TYPE_ARM_VGIC_V3)
		iter->nr_lpis = iter_mark_lpis(kvm);

	/* Fast forward to the right position if needed */
	while (pos--)
		iter_next(kvm, iter);
}

static bool end_of_vgic(struct vgic_state_iter *iter)
{
	return iter->dist_id > 0 &&
		iter->vcpu_id == iter->nr_cpus &&
		iter->intid >= (iter->nr_spis + VGIC_NR_PRIVATE_IRQS) &&
		(!iter->nr_lpis || iter->lpi_idx > iter->nr_lpis);
}

static void *vgic_debug_start(struct seq_file *s, loff_t *pos)
{
	struct kvm *kvm = s->private;
	struct vgic_state_iter *iter;

	mutex_lock(&kvm->arch.config_lock);
	iter = kvm->arch.vgic.iter;
	if (iter) {
		iter = ERR_PTR(-EBUSY);
		goto out;
	}

	iter = kmalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter) {
		iter = ERR_PTR(-ENOMEM);
		goto out;
	}

	iter_init(kvm, iter, *pos);
	kvm->arch.vgic.iter = iter;

	if (end_of_vgic(iter))
		iter = NULL;
out:
	mutex_unlock(&kvm->arch.config_lock);
	return iter;
}

static void *vgic_debug_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct kvm *kvm = s->private;
	struct vgic_state_iter *iter = kvm->arch.vgic.iter;

	++*pos;
	iter_next(kvm, iter);
	if (end_of_vgic(iter))
		iter = NULL;
	return iter;
}

static void vgic_debug_stop(struct seq_file *s, void *v)
{
	struct kvm *kvm = s->private;
	struct vgic_state_iter *iter;

	/*
	 * If the seq file wasn't properly opened, there's nothing to clearn
	 * up.
	 */
	if (IS_ERR(v))
		return;

	mutex_lock(&kvm->arch.config_lock);
	iter = kvm->arch.vgic.iter;
	iter_unmark_lpis(kvm);
	kfree(iter);
	kvm->arch.vgic.iter = NULL;
	mutex_unlock(&kvm->arch.config_lock);
}

static void print_dist_state(struct seq_file *s, struct vgic_dist *dist,
			     struct vgic_state_iter *iter)
{
	bool v3 = dist->vgic_model == KVM_DEV_TYPE_ARM_VGIC_V3;

	seq_printf(s, "Distributor\n");
	seq_printf(s, "===========\n");
	seq_printf(s, "vgic_model:\t%s\n", v3 ? "GICv3" : "GICv2");
	seq_printf(s, "nr_spis:\t%d\n", dist->nr_spis);
	if (v3)
		seq_printf(s, "nr_lpis:\t%d\n", iter->nr_lpis);
	seq_printf(s, "enabled:\t%d\n", dist->enabled);
	seq_printf(s, "\n");

	seq_printf(s, "P=pending_latch, L=line_level, A=active\n");
	seq_printf(s, "E=enabled, H=hw, C=config (level=1, edge=0)\n");
	seq_printf(s, "G=group\n");
}

static void print_header(struct seq_file *s, struct vgic_irq *irq,
			 struct kvm_vcpu *vcpu)
{
	int id = 0;
	char *hdr = "SPI ";

	if (vcpu) {
		hdr = "VCPU";
		id = vcpu->vcpu_idx;
	}

	seq_printf(s, "\n");
	seq_printf(s, "%s%2d TYP   ID TGT_ID PLAEHCG     HWID   TARGET SRC PRI VCPU_ID\n", hdr, id);
	seq_printf(s, "----------------------------------------------------------------\n");
}

static void print_irq_state(struct seq_file *s, struct vgic_irq *irq,
			    struct kvm_vcpu *vcpu)
{
	char *type;
	bool pending;

	if (irq->intid < VGIC_NR_SGIS)
		type = "SGI";
	else if (irq->intid < VGIC_NR_PRIVATE_IRQS)
		type = "PPI";
	else if (irq->intid < VGIC_MAX_SPI)
		type = "SPI";
	else
		type = "LPI";

	if (irq->intid ==0 || irq->intid == VGIC_NR_PRIVATE_IRQS)
		print_header(s, irq, vcpu);

	pending = irq->pending_latch;
	if (irq->hw && vgic_irq_is_sgi(irq->intid)) {
		int err;

		err = irq_get_irqchip_state(irq->host_irq,
					    IRQCHIP_STATE_PENDING,
					    &pending);
		WARN_ON_ONCE(err);
	}

	seq_printf(s, "       %s %4d "
		      "    %2d "
		      "%d%d%d%d%d%d%d "
		      "%8d "
		      "%8x "
		      " %2x "
		      "%3d "
		      "     %2d "
		      "\n",
			type, irq->intid,
			(irq->target_vcpu) ? irq->target_vcpu->vcpu_idx : -1,
			pending,
			irq->line_level,
			irq->active,
			irq->enabled,
			irq->hw,
			irq->config == VGIC_CONFIG_LEVEL,
			irq->group,
			irq->hwintid,
			irq->mpidr,
			irq->source,
			irq->priority,
			(irq->vcpu) ? irq->vcpu->vcpu_idx : -1);
}

static int vgic_debug_show(struct seq_file *s, void *v)
{
	struct kvm *kvm = s->private;
	struct vgic_state_iter *iter = v;
	struct vgic_irq *irq;
	struct kvm_vcpu *vcpu = NULL;
	unsigned long flags;

	if (iter->dist_id == 0) {
		print_dist_state(s, &kvm->arch.vgic, iter);
		return 0;
	}

	if (!kvm->arch.vgic.initialized)
		return 0;

	if (iter->vcpu_id < iter->nr_cpus)
		vcpu = kvm_get_vcpu(kvm, iter->vcpu_id);

	/*
	 * Expect this to succeed, as iter_mark_lpis() takes a reference on
	 * every LPI to be visited.
	 */
	irq = vgic_get_irq(kvm, vcpu, iter->intid);
	if (WARN_ON_ONCE(!irq))
		return -EINVAL;

	raw_spin_lock_irqsave(&irq->irq_lock, flags);
	print_irq_state(s, irq, vcpu);
	raw_spin_unlock_irqrestore(&irq->irq_lock, flags);

	vgic_put_irq(kvm, irq);
	return 0;
}

static const struct seq_operations vgic_debug_sops = {
	.start = vgic_debug_start,
	.next  = vgic_debug_next,
	.stop  = vgic_debug_stop,
	.show  = vgic_debug_show
};

DEFINE_SEQ_ATTRIBUTE(vgic_debug);

void vgic_debug_init(struct kvm *kvm)
{
	debugfs_create_file("vgic-state", 0444, kvm->debugfs_dentry, kvm,
			    &vgic_debug_fops);
}

void vgic_debug_destroy(struct kvm *kvm)
{
}
