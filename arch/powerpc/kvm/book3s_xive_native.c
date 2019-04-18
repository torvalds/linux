// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017-2019, IBM Corporation.
 */

#define pr_fmt(fmt) "xive-kvm: " fmt

#include <linux/kernel.h>
#include <linux/kvm_host.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <asm/kvm_book3s.h>
#include <asm/kvm_ppc.h>
#include <asm/hvcall.h>
#include <asm/xive.h>
#include <asm/xive-regs.h>
#include <asm/debug.h>
#include <asm/debugfs.h>
#include <asm/opal.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include "book3s_xive.h"

static u8 xive_vm_esb_load(struct xive_irq_data *xd, u32 offset)
{
	u64 val;

	if (xd->flags & XIVE_IRQ_FLAG_SHIFT_BUG)
		offset |= offset << 4;

	val = in_be64(xd->eoi_mmio + offset);
	return (u8)val;
}

static void kvmppc_xive_native_cleanup_queue(struct kvm_vcpu *vcpu, int prio)
{
	struct kvmppc_xive_vcpu *xc = vcpu->arch.xive_vcpu;
	struct xive_q *q = &xc->queues[prio];

	xive_native_disable_queue(xc->vp_id, q, prio);
	if (q->qpage) {
		put_page(virt_to_page(q->qpage));
		q->qpage = NULL;
	}
}

void kvmppc_xive_native_cleanup_vcpu(struct kvm_vcpu *vcpu)
{
	struct kvmppc_xive_vcpu *xc = vcpu->arch.xive_vcpu;
	int i;

	if (!kvmppc_xive_enabled(vcpu))
		return;

	if (!xc)
		return;

	pr_devel("native_cleanup_vcpu(cpu=%d)\n", xc->server_num);

	/* Ensure no interrupt is still routed to that VP */
	xc->valid = false;
	kvmppc_xive_disable_vcpu_interrupts(vcpu);

	/* Disable the VP */
	xive_native_disable_vp(xc->vp_id);

	/* Free the queues & associated interrupts */
	for (i = 0; i < KVMPPC_XIVE_Q_COUNT; i++) {
		/* Free the escalation irq */
		if (xc->esc_virq[i]) {
			free_irq(xc->esc_virq[i], vcpu);
			irq_dispose_mapping(xc->esc_virq[i]);
			kfree(xc->esc_virq_names[i]);
			xc->esc_virq[i] = 0;
		}

		/* Free the queue */
		kvmppc_xive_native_cleanup_queue(vcpu, i);
	}

	/* Free the VP */
	kfree(xc);

	/* Cleanup the vcpu */
	vcpu->arch.irq_type = KVMPPC_IRQ_DEFAULT;
	vcpu->arch.xive_vcpu = NULL;
}

int kvmppc_xive_native_connect_vcpu(struct kvm_device *dev,
				    struct kvm_vcpu *vcpu, u32 server_num)
{
	struct kvmppc_xive *xive = dev->private;
	struct kvmppc_xive_vcpu *xc = NULL;
	int rc;

	pr_devel("native_connect_vcpu(server=%d)\n", server_num);

	if (dev->ops != &kvm_xive_native_ops) {
		pr_devel("Wrong ops !\n");
		return -EPERM;
	}
	if (xive->kvm != vcpu->kvm)
		return -EPERM;
	if (vcpu->arch.irq_type != KVMPPC_IRQ_DEFAULT)
		return -EBUSY;
	if (server_num >= KVM_MAX_VCPUS) {
		pr_devel("Out of bounds !\n");
		return -EINVAL;
	}

	mutex_lock(&vcpu->kvm->lock);

	if (kvmppc_xive_find_server(vcpu->kvm, server_num)) {
		pr_devel("Duplicate !\n");
		rc = -EEXIST;
		goto bail;
	}

	xc = kzalloc(sizeof(*xc), GFP_KERNEL);
	if (!xc) {
		rc = -ENOMEM;
		goto bail;
	}

	vcpu->arch.xive_vcpu = xc;
	xc->xive = xive;
	xc->vcpu = vcpu;
	xc->server_num = server_num;

	xc->vp_id = kvmppc_xive_vp(xive, server_num);
	xc->valid = true;
	vcpu->arch.irq_type = KVMPPC_IRQ_XIVE;

	rc = xive_native_get_vp_info(xc->vp_id, &xc->vp_cam, &xc->vp_chip_id);
	if (rc) {
		pr_err("Failed to get VP info from OPAL: %d\n", rc);
		goto bail;
	}

	/*
	 * Enable the VP first as the single escalation mode will
	 * affect escalation interrupts numbering
	 */
	rc = xive_native_enable_vp(xc->vp_id, xive->single_escalation);
	if (rc) {
		pr_err("Failed to enable VP in OPAL: %d\n", rc);
		goto bail;
	}

	/* Configure VCPU fields for use by assembly push/pull */
	vcpu->arch.xive_saved_state.w01 = cpu_to_be64(0xff000000);
	vcpu->arch.xive_cam_word = cpu_to_be32(xc->vp_cam | TM_QW1W2_VO);

	/* TODO: reset all queues to a clean state ? */
bail:
	mutex_unlock(&vcpu->kvm->lock);
	if (rc)
		kvmppc_xive_native_cleanup_vcpu(vcpu);

	return rc;
}

static int kvmppc_xive_native_set_source(struct kvmppc_xive *xive, long irq,
					 u64 addr)
{
	struct kvmppc_xive_src_block *sb;
	struct kvmppc_xive_irq_state *state;
	u64 __user *ubufp = (u64 __user *) addr;
	u64 val;
	u16 idx;
	int rc;

	pr_devel("%s irq=0x%lx\n", __func__, irq);

	if (irq < KVMPPC_XIVE_FIRST_IRQ || irq >= KVMPPC_XIVE_NR_IRQS)
		return -E2BIG;

	sb = kvmppc_xive_find_source(xive, irq, &idx);
	if (!sb) {
		pr_debug("No source, creating source block...\n");
		sb = kvmppc_xive_create_src_block(xive, irq);
		if (!sb) {
			pr_err("Failed to create block...\n");
			return -ENOMEM;
		}
	}
	state = &sb->irq_state[idx];

	if (get_user(val, ubufp)) {
		pr_err("fault getting user info !\n");
		return -EFAULT;
	}

	arch_spin_lock(&sb->lock);

	/*
	 * If the source doesn't already have an IPI, allocate
	 * one and get the corresponding data
	 */
	if (!state->ipi_number) {
		state->ipi_number = xive_native_alloc_irq();
		if (state->ipi_number == 0) {
			pr_err("Failed to allocate IRQ !\n");
			rc = -ENXIO;
			goto unlock;
		}
		xive_native_populate_irq_data(state->ipi_number,
					      &state->ipi_data);
		pr_debug("%s allocated hw_irq=0x%x for irq=0x%lx\n", __func__,
			 state->ipi_number, irq);
	}

	/* Restore LSI state */
	if (val & KVM_XIVE_LEVEL_SENSITIVE) {
		state->lsi = true;
		if (val & KVM_XIVE_LEVEL_ASSERTED)
			state->asserted = true;
		pr_devel("  LSI ! Asserted=%d\n", state->asserted);
	}

	/* Mask IRQ to start with */
	state->act_server = 0;
	state->act_priority = MASKED;
	xive_vm_esb_load(&state->ipi_data, XIVE_ESB_SET_PQ_01);
	xive_native_configure_irq(state->ipi_number, 0, MASKED, 0);

	/* Increment the number of valid sources and mark this one valid */
	if (!state->valid)
		xive->src_count++;
	state->valid = true;

	rc = 0;

unlock:
	arch_spin_unlock(&sb->lock);

	return rc;
}

static int kvmppc_xive_native_update_source_config(struct kvmppc_xive *xive,
					struct kvmppc_xive_src_block *sb,
					struct kvmppc_xive_irq_state *state,
					u32 server, u8 priority, bool masked,
					u32 eisn)
{
	struct kvm *kvm = xive->kvm;
	u32 hw_num;
	int rc = 0;

	arch_spin_lock(&sb->lock);

	if (state->act_server == server && state->act_priority == priority &&
	    state->eisn == eisn)
		goto unlock;

	pr_devel("new_act_prio=%d new_act_server=%d mask=%d act_server=%d act_prio=%d\n",
		 priority, server, masked, state->act_server,
		 state->act_priority);

	kvmppc_xive_select_irq(state, &hw_num, NULL);

	if (priority != MASKED && !masked) {
		rc = kvmppc_xive_select_target(kvm, &server, priority);
		if (rc)
			goto unlock;

		state->act_priority = priority;
		state->act_server = server;
		state->eisn = eisn;

		rc = xive_native_configure_irq(hw_num,
					       kvmppc_xive_vp(xive, server),
					       priority, eisn);
	} else {
		state->act_priority = MASKED;
		state->act_server = 0;
		state->eisn = 0;

		rc = xive_native_configure_irq(hw_num, 0, MASKED, 0);
	}

unlock:
	arch_spin_unlock(&sb->lock);
	return rc;
}

static int kvmppc_xive_native_set_source_config(struct kvmppc_xive *xive,
						long irq, u64 addr)
{
	struct kvmppc_xive_src_block *sb;
	struct kvmppc_xive_irq_state *state;
	u64 __user *ubufp = (u64 __user *) addr;
	u16 src;
	u64 kvm_cfg;
	u32 server;
	u8 priority;
	bool masked;
	u32 eisn;

	sb = kvmppc_xive_find_source(xive, irq, &src);
	if (!sb)
		return -ENOENT;

	state = &sb->irq_state[src];

	if (!state->valid)
		return -EINVAL;

	if (get_user(kvm_cfg, ubufp))
		return -EFAULT;

	pr_devel("%s irq=0x%lx cfg=%016llx\n", __func__, irq, kvm_cfg);

	priority = (kvm_cfg & KVM_XIVE_SOURCE_PRIORITY_MASK) >>
		KVM_XIVE_SOURCE_PRIORITY_SHIFT;
	server = (kvm_cfg & KVM_XIVE_SOURCE_SERVER_MASK) >>
		KVM_XIVE_SOURCE_SERVER_SHIFT;
	masked = (kvm_cfg & KVM_XIVE_SOURCE_MASKED_MASK) >>
		KVM_XIVE_SOURCE_MASKED_SHIFT;
	eisn = (kvm_cfg & KVM_XIVE_SOURCE_EISN_MASK) >>
		KVM_XIVE_SOURCE_EISN_SHIFT;

	if (priority != xive_prio_from_guest(priority)) {
		pr_err("invalid priority for queue %d for VCPU %d\n",
		       priority, server);
		return -EINVAL;
	}

	return kvmppc_xive_native_update_source_config(xive, sb, state, server,
						       priority, masked, eisn);
}

static int xive_native_validate_queue_size(u32 qshift)
{
	/*
	 * We only support 64K pages for the moment. This is also
	 * advertised in the DT property "ibm,xive-eq-sizes"
	 */
	switch (qshift) {
	case 0: /* EQ reset */
	case 16:
		return 0;
	case 12:
	case 21:
	case 24:
	default:
		return -EINVAL;
	}
}

static int kvmppc_xive_native_set_queue_config(struct kvmppc_xive *xive,
					       long eq_idx, u64 addr)
{
	struct kvm *kvm = xive->kvm;
	struct kvm_vcpu *vcpu;
	struct kvmppc_xive_vcpu *xc;
	void __user *ubufp = (void __user *) addr;
	u32 server;
	u8 priority;
	struct kvm_ppc_xive_eq kvm_eq;
	int rc;
	__be32 *qaddr = 0;
	struct page *page;
	struct xive_q *q;
	gfn_t gfn;
	unsigned long page_size;

	/*
	 * Demangle priority/server tuple from the EQ identifier
	 */
	priority = (eq_idx & KVM_XIVE_EQ_PRIORITY_MASK) >>
		KVM_XIVE_EQ_PRIORITY_SHIFT;
	server = (eq_idx & KVM_XIVE_EQ_SERVER_MASK) >>
		KVM_XIVE_EQ_SERVER_SHIFT;

	if (copy_from_user(&kvm_eq, ubufp, sizeof(kvm_eq)))
		return -EFAULT;

	vcpu = kvmppc_xive_find_server(kvm, server);
	if (!vcpu) {
		pr_err("Can't find server %d\n", server);
		return -ENOENT;
	}
	xc = vcpu->arch.xive_vcpu;

	if (priority != xive_prio_from_guest(priority)) {
		pr_err("Trying to restore invalid queue %d for VCPU %d\n",
		       priority, server);
		return -EINVAL;
	}
	q = &xc->queues[priority];

	pr_devel("%s VCPU %d priority %d fl:%x shift:%d addr:%llx g:%d idx:%d\n",
		 __func__, server, priority, kvm_eq.flags,
		 kvm_eq.qshift, kvm_eq.qaddr, kvm_eq.qtoggle, kvm_eq.qindex);

	/*
	 * sPAPR specifies a "Unconditional Notify (n) flag" for the
	 * H_INT_SET_QUEUE_CONFIG hcall which forces notification
	 * without using the coalescing mechanisms provided by the
	 * XIVE END ESBs. This is required on KVM as notification
	 * using the END ESBs is not supported.
	 */
	if (kvm_eq.flags != KVM_XIVE_EQ_ALWAYS_NOTIFY) {
		pr_err("invalid flags %d\n", kvm_eq.flags);
		return -EINVAL;
	}

	rc = xive_native_validate_queue_size(kvm_eq.qshift);
	if (rc) {
		pr_err("invalid queue size %d\n", kvm_eq.qshift);
		return rc;
	}

	/* reset queue and disable queueing */
	if (!kvm_eq.qshift) {
		q->guest_qaddr  = 0;
		q->guest_qshift = 0;

		rc = xive_native_configure_queue(xc->vp_id, q, priority,
						 NULL, 0, true);
		if (rc) {
			pr_err("Failed to reset queue %d for VCPU %d: %d\n",
			       priority, xc->server_num, rc);
			return rc;
		}

		if (q->qpage) {
			put_page(virt_to_page(q->qpage));
			q->qpage = NULL;
		}

		return 0;
	}

	if (kvm_eq.qaddr & ((1ull << kvm_eq.qshift) - 1)) {
		pr_err("queue page is not aligned %llx/%llx\n", kvm_eq.qaddr,
		       1ull << kvm_eq.qshift);
		return -EINVAL;
	}

	gfn = gpa_to_gfn(kvm_eq.qaddr);
	page = gfn_to_page(kvm, gfn);
	if (is_error_page(page)) {
		pr_err("Couldn't get queue page %llx!\n", kvm_eq.qaddr);
		return -EINVAL;
	}

	page_size = kvm_host_page_size(kvm, gfn);
	if (1ull << kvm_eq.qshift > page_size) {
		pr_warn("Incompatible host page size %lx!\n", page_size);
		return -EINVAL;
	}

	qaddr = page_to_virt(page) + (kvm_eq.qaddr & ~PAGE_MASK);

	/*
	 * Backup the queue page guest address to the mark EQ page
	 * dirty for migration.
	 */
	q->guest_qaddr  = kvm_eq.qaddr;
	q->guest_qshift = kvm_eq.qshift;

	 /*
	  * Unconditional Notification is forced by default at the
	  * OPAL level because the use of END ESBs is not supported by
	  * Linux.
	  */
	rc = xive_native_configure_queue(xc->vp_id, q, priority,
					 (__be32 *) qaddr, kvm_eq.qshift, true);
	if (rc) {
		pr_err("Failed to configure queue %d for VCPU %d: %d\n",
		       priority, xc->server_num, rc);
		put_page(page);
		return rc;
	}

	/*
	 * Only restore the queue state when needed. When doing the
	 * H_INT_SET_SOURCE_CONFIG hcall, it should not.
	 */
	if (kvm_eq.qtoggle != 1 || kvm_eq.qindex != 0) {
		rc = xive_native_set_queue_state(xc->vp_id, priority,
						 kvm_eq.qtoggle,
						 kvm_eq.qindex);
		if (rc)
			goto error;
	}

	rc = kvmppc_xive_attach_escalation(vcpu, priority,
					   xive->single_escalation);
error:
	if (rc)
		kvmppc_xive_native_cleanup_queue(vcpu, priority);
	return rc;
}

static int kvmppc_xive_native_get_queue_config(struct kvmppc_xive *xive,
					       long eq_idx, u64 addr)
{
	struct kvm *kvm = xive->kvm;
	struct kvm_vcpu *vcpu;
	struct kvmppc_xive_vcpu *xc;
	struct xive_q *q;
	void __user *ubufp = (u64 __user *) addr;
	u32 server;
	u8 priority;
	struct kvm_ppc_xive_eq kvm_eq;
	u64 qaddr;
	u64 qshift;
	u64 qeoi_page;
	u32 escalate_irq;
	u64 qflags;
	int rc;

	/*
	 * Demangle priority/server tuple from the EQ identifier
	 */
	priority = (eq_idx & KVM_XIVE_EQ_PRIORITY_MASK) >>
		KVM_XIVE_EQ_PRIORITY_SHIFT;
	server = (eq_idx & KVM_XIVE_EQ_SERVER_MASK) >>
		KVM_XIVE_EQ_SERVER_SHIFT;

	vcpu = kvmppc_xive_find_server(kvm, server);
	if (!vcpu) {
		pr_err("Can't find server %d\n", server);
		return -ENOENT;
	}
	xc = vcpu->arch.xive_vcpu;

	if (priority != xive_prio_from_guest(priority)) {
		pr_err("invalid priority for queue %d for VCPU %d\n",
		       priority, server);
		return -EINVAL;
	}
	q = &xc->queues[priority];

	memset(&kvm_eq, 0, sizeof(kvm_eq));

	if (!q->qpage)
		return 0;

	rc = xive_native_get_queue_info(xc->vp_id, priority, &qaddr, &qshift,
					&qeoi_page, &escalate_irq, &qflags);
	if (rc)
		return rc;

	kvm_eq.flags = 0;
	if (qflags & OPAL_XIVE_EQ_ALWAYS_NOTIFY)
		kvm_eq.flags |= KVM_XIVE_EQ_ALWAYS_NOTIFY;

	kvm_eq.qshift = q->guest_qshift;
	kvm_eq.qaddr  = q->guest_qaddr;

	rc = xive_native_get_queue_state(xc->vp_id, priority, &kvm_eq.qtoggle,
					 &kvm_eq.qindex);
	if (rc)
		return rc;

	pr_devel("%s VCPU %d priority %d fl:%x shift:%d addr:%llx g:%d idx:%d\n",
		 __func__, server, priority, kvm_eq.flags,
		 kvm_eq.qshift, kvm_eq.qaddr, kvm_eq.qtoggle, kvm_eq.qindex);

	if (copy_to_user(ubufp, &kvm_eq, sizeof(kvm_eq)))
		return -EFAULT;

	return 0;
}

static int kvmppc_xive_native_set_attr(struct kvm_device *dev,
				       struct kvm_device_attr *attr)
{
	struct kvmppc_xive *xive = dev->private;

	switch (attr->group) {
	case KVM_DEV_XIVE_GRP_CTRL:
		break;
	case KVM_DEV_XIVE_GRP_SOURCE:
		return kvmppc_xive_native_set_source(xive, attr->attr,
						     attr->addr);
	case KVM_DEV_XIVE_GRP_SOURCE_CONFIG:
		return kvmppc_xive_native_set_source_config(xive, attr->attr,
							    attr->addr);
	case KVM_DEV_XIVE_GRP_EQ_CONFIG:
		return kvmppc_xive_native_set_queue_config(xive, attr->attr,
							   attr->addr);
	}
	return -ENXIO;
}

static int kvmppc_xive_native_get_attr(struct kvm_device *dev,
				       struct kvm_device_attr *attr)
{
	struct kvmppc_xive *xive = dev->private;

	switch (attr->group) {
	case KVM_DEV_XIVE_GRP_EQ_CONFIG:
		return kvmppc_xive_native_get_queue_config(xive, attr->attr,
							   attr->addr);
	}
	return -ENXIO;
}

static int kvmppc_xive_native_has_attr(struct kvm_device *dev,
				       struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_XIVE_GRP_CTRL:
		break;
	case KVM_DEV_XIVE_GRP_SOURCE:
	case KVM_DEV_XIVE_GRP_SOURCE_CONFIG:
		if (attr->attr >= KVMPPC_XIVE_FIRST_IRQ &&
		    attr->attr < KVMPPC_XIVE_NR_IRQS)
			return 0;
		break;
	case KVM_DEV_XIVE_GRP_EQ_CONFIG:
		return 0;
	}
	return -ENXIO;
}

static void kvmppc_xive_native_free(struct kvm_device *dev)
{
	struct kvmppc_xive *xive = dev->private;
	struct kvm *kvm = xive->kvm;
	int i;

	debugfs_remove(xive->dentry);

	pr_devel("Destroying xive native device\n");

	if (kvm)
		kvm->arch.xive = NULL;

	for (i = 0; i <= xive->max_sbid; i++) {
		if (xive->src_blocks[i])
			kvmppc_xive_free_sources(xive->src_blocks[i]);
		kfree(xive->src_blocks[i]);
		xive->src_blocks[i] = NULL;
	}

	if (xive->vp_base != XIVE_INVALID_VP)
		xive_native_free_vp_block(xive->vp_base);

	kfree(xive);
	kfree(dev);
}

static int kvmppc_xive_native_create(struct kvm_device *dev, u32 type)
{
	struct kvmppc_xive *xive;
	struct kvm *kvm = dev->kvm;
	int ret = 0;

	pr_devel("Creating xive native device\n");

	if (kvm->arch.xive)
		return -EEXIST;

	xive = kzalloc(sizeof(*xive), GFP_KERNEL);
	if (!xive)
		return -ENOMEM;

	dev->private = xive;
	xive->dev = dev;
	xive->kvm = kvm;
	kvm->arch.xive = xive;

	/*
	 * Allocate a bunch of VPs. KVM_MAX_VCPUS is a large value for
	 * a default. Getting the max number of CPUs the VM was
	 * configured with would improve our usage of the XIVE VP space.
	 */
	xive->vp_base = xive_native_alloc_vp_block(KVM_MAX_VCPUS);
	pr_devel("VP_Base=%x\n", xive->vp_base);

	if (xive->vp_base == XIVE_INVALID_VP)
		ret = -ENXIO;

	xive->single_escalation = xive_native_has_single_escalation();

	if (ret)
		kfree(xive);

	return ret;
}

static int xive_native_debug_show(struct seq_file *m, void *private)
{
	struct kvmppc_xive *xive = m->private;
	struct kvm *kvm = xive->kvm;
	struct kvm_vcpu *vcpu;
	unsigned int i;

	if (!kvm)
		return 0;

	seq_puts(m, "=========\nVCPU state\n=========\n");

	kvm_for_each_vcpu(i, vcpu, kvm) {
		struct kvmppc_xive_vcpu *xc = vcpu->arch.xive_vcpu;

		if (!xc)
			continue;

		seq_printf(m, "cpu server %#x NSR=%02x CPPR=%02x IBP=%02x PIPR=%02x w01=%016llx w2=%08x\n",
			   xc->server_num,
			   vcpu->arch.xive_saved_state.nsr,
			   vcpu->arch.xive_saved_state.cppr,
			   vcpu->arch.xive_saved_state.ipb,
			   vcpu->arch.xive_saved_state.pipr,
			   vcpu->arch.xive_saved_state.w01,
			   (u32) vcpu->arch.xive_cam_word);

		kvmppc_xive_debug_show_queues(m, vcpu);
	}

	return 0;
}

static int xive_native_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, xive_native_debug_show, inode->i_private);
}

static const struct file_operations xive_native_debug_fops = {
	.open = xive_native_debug_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void xive_native_debugfs_init(struct kvmppc_xive *xive)
{
	char *name;

	name = kasprintf(GFP_KERNEL, "kvm-xive-%p", xive);
	if (!name) {
		pr_err("%s: no memory for name\n", __func__);
		return;
	}

	xive->dentry = debugfs_create_file(name, 0444, powerpc_debugfs_root,
					   xive, &xive_native_debug_fops);

	pr_debug("%s: created %s\n", __func__, name);
	kfree(name);
}

static void kvmppc_xive_native_init(struct kvm_device *dev)
{
	struct kvmppc_xive *xive = (struct kvmppc_xive *)dev->private;

	/* Register some debug interfaces */
	xive_native_debugfs_init(xive);
}

struct kvm_device_ops kvm_xive_native_ops = {
	.name = "kvm-xive-native",
	.create = kvmppc_xive_native_create,
	.init = kvmppc_xive_native_init,
	.destroy = kvmppc_xive_native_free,
	.set_attr = kvmppc_xive_native_set_attr,
	.get_attr = kvmppc_xive_native_get_attr,
	.has_attr = kvmppc_xive_native_has_attr,
};

void kvmppc_xive_native_init_module(void)
{
	;
}

void kvmppc_xive_native_exit_module(void)
{
	;
}
