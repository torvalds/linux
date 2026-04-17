// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Anup Patel <anup.patel@wdc.com>
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/kvm_host.h>
#include <asm/kvm_mmu.h>

const struct kvm_stats_desc kvm_vm_stats_desc[] = {
	KVM_GENERIC_VM_STATS()
};
static_assert(ARRAY_SIZE(kvm_vm_stats_desc) ==
		sizeof(struct kvm_vm_stat) / sizeof(u64));

const struct kvm_stats_header kvm_vm_stats_header = {
	.name_size = KVM_STATS_NAME_SIZE,
	.num_desc = ARRAY_SIZE(kvm_vm_stats_desc),
	.id_offset =  sizeof(struct kvm_stats_header),
	.desc_offset = sizeof(struct kvm_stats_header) + KVM_STATS_NAME_SIZE,
	.data_offset = sizeof(struct kvm_stats_header) + KVM_STATS_NAME_SIZE +
		       sizeof(kvm_vm_stats_desc),
};

int kvm_arch_init_vm(struct kvm *kvm, unsigned long type)
{
	int r;

	r = kvm_riscv_mmu_alloc_pgd(kvm);
	if (r)
		return r;

	r = kvm_riscv_gstage_vmid_init(kvm);
	if (r) {
		kvm_riscv_mmu_free_pgd(kvm);
		return r;
	}

	kvm_riscv_aia_init_vm(kvm);

	kvm_riscv_guest_timer_init(kvm);

	return 0;
}

void kvm_arch_destroy_vm(struct kvm *kvm)
{
	kvm_destroy_vcpus(kvm);

	kvm_riscv_aia_destroy_vm(kvm);
}

int kvm_vm_ioctl_irq_line(struct kvm *kvm, struct kvm_irq_level *irql,
			  bool line_status)
{
	if (!irqchip_in_kernel(kvm))
		return -ENXIO;

	return kvm_riscv_aia_inject_irq(kvm, irql->irq, irql->level);
}

int kvm_set_msi(struct kvm_kernel_irq_routing_entry *e,
		struct kvm *kvm, int irq_source_id,
		int level, bool line_status)
{
	struct kvm_msi msi;

	if (!level)
		return -1;

	msi.address_lo = e->msi.address_lo;
	msi.address_hi = e->msi.address_hi;
	msi.data = e->msi.data;
	msi.flags = e->msi.flags;
	msi.devid = e->msi.devid;

	return kvm_riscv_aia_inject_msi(kvm, &msi);
}

static int kvm_riscv_set_irq(struct kvm_kernel_irq_routing_entry *e,
			     struct kvm *kvm, int irq_source_id,
			     int level, bool line_status)
{
	return kvm_riscv_aia_inject_irq(kvm, e->irqchip.pin, level);
}

int kvm_riscv_setup_default_irq_routing(struct kvm *kvm, u32 lines)
{
	struct kvm_irq_routing_entry *ents;
	int i, rc;

	ents = kzalloc_objs(*ents, lines);
	if (!ents)
		return -ENOMEM;

	for (i = 0; i < lines; i++) {
		ents[i].gsi = i;
		ents[i].type = KVM_IRQ_ROUTING_IRQCHIP;
		ents[i].u.irqchip.irqchip = 0;
		ents[i].u.irqchip.pin = i;
	}
	rc = kvm_set_irq_routing(kvm, ents, lines, 0);
	kfree(ents);

	return rc;
}

bool kvm_arch_can_set_irq_routing(struct kvm *kvm)
{
	return irqchip_in_kernel(kvm);
}

int kvm_set_routing_entry(struct kvm *kvm,
			  struct kvm_kernel_irq_routing_entry *e,
			  const struct kvm_irq_routing_entry *ue)
{
	int r = -EINVAL;

	switch (ue->type) {
	case KVM_IRQ_ROUTING_IRQCHIP:
		e->set = kvm_riscv_set_irq;
		e->irqchip.irqchip = ue->u.irqchip.irqchip;
		e->irqchip.pin = ue->u.irqchip.pin;
		if ((e->irqchip.pin >= KVM_IRQCHIP_NUM_PINS) ||
		    (e->irqchip.irqchip >= KVM_NR_IRQCHIPS))
			goto out;
		break;
	case KVM_IRQ_ROUTING_MSI:
		e->set = kvm_set_msi;
		e->msi.address_lo = ue->u.msi.address_lo;
		e->msi.address_hi = ue->u.msi.address_hi;
		e->msi.data = ue->u.msi.data;
		e->msi.flags = ue->flags;
		e->msi.devid = ue->u.msi.devid;
		break;
	default:
		goto out;
	}
	r = 0;
out:
	return r;
}

int kvm_arch_set_irq_inatomic(struct kvm_kernel_irq_routing_entry *e,
			      struct kvm *kvm, int irq_source_id, int level,
			      bool line_status)
{
	if (!level)
		return -EWOULDBLOCK;

	switch (e->type) {
	case KVM_IRQ_ROUTING_MSI:
		return kvm_set_msi(e, kvm, irq_source_id, level, line_status);

	case KVM_IRQ_ROUTING_IRQCHIP:
		return kvm_riscv_set_irq(e, kvm, irq_source_id,
					 level, line_status);
	}

	return -EWOULDBLOCK;
}

bool kvm_arch_irqchip_in_kernel(struct kvm *kvm)
{
	return irqchip_in_kernel(kvm);
}

int kvm_vm_ioctl_check_extension(struct kvm *kvm, long ext)
{
	int r;

	switch (ext) {
	case KVM_CAP_IRQCHIP:
		r = kvm_riscv_aia_available();
		break;
	case KVM_CAP_IOEVENTFD:
	case KVM_CAP_USER_MEMORY:
	case KVM_CAP_DESTROY_MEMORY_REGION_WORKS:
	case KVM_CAP_ONE_REG:
	case KVM_CAP_READONLY_MEM:
	case KVM_CAP_MP_STATE:
	case KVM_CAP_IMMEDIATE_EXIT:
	case KVM_CAP_SET_GUEST_DEBUG:
		r = 1;
		break;
	case KVM_CAP_NR_VCPUS:
		r = min_t(unsigned int, num_online_cpus(), KVM_MAX_VCPUS);
		break;
	case KVM_CAP_MAX_VCPUS:
		r = KVM_MAX_VCPUS;
		break;
	case KVM_CAP_NR_MEMSLOTS:
		r = KVM_USER_MEM_SLOTS;
		break;
	case KVM_CAP_VM_GPA_BITS:
		if (!kvm)
			r = kvm_riscv_gstage_gpa_bits(kvm_riscv_gstage_max_pgd_levels);
		else
			r = kvm_riscv_gstage_gpa_bits(kvm->arch.pgd_levels);
		break;
	default:
		r = 0;
		break;
	}

	return r;
}

int kvm_vm_ioctl_enable_cap(struct kvm *kvm, struct kvm_enable_cap *cap)
{
	if (cap->flags)
		return -EINVAL;

	switch (cap->cap) {
	case KVM_CAP_RISCV_MP_STATE_RESET:
		kvm->arch.mp_state_reset = true;
		return 0;
	case KVM_CAP_VM_GPA_BITS: {
		unsigned long gpa_bits = cap->args[0];
		unsigned long new_levels;
		int r = 0;

		/* Decide target pgd levels from requested gpa_bits */
#ifdef CONFIG_64BIT
		if (gpa_bits <= 41)
			new_levels = 3;        /* Sv39x4 */
		else if (gpa_bits <= 50)
			new_levels = 4;        /* Sv48x4 */
		else if (gpa_bits <= 59)
			new_levels = 5;        /* Sv57x4 */
		else
			return -EINVAL;
#else
		/* 32-bit: only Sv32x4*/
		if (gpa_bits <= 34)
			new_levels = 2;
		else
			return -EINVAL;
#endif
		if (new_levels > kvm_riscv_gstage_max_pgd_levels)
			return -EINVAL;

		/* Follow KVM's lock ordering: kvm->lock -> kvm->slots_lock. */
		mutex_lock(&kvm->lock);
		mutex_lock(&kvm->slots_lock);

		if (kvm->created_vcpus || !kvm_are_all_memslots_empty(kvm))
			r = -EBUSY;
		else
			kvm->arch.pgd_levels = new_levels;

		mutex_unlock(&kvm->slots_lock);
		mutex_unlock(&kvm->lock);

		return r;
	}
	default:
		return -EINVAL;
	}
}

int kvm_arch_vm_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	return -EINVAL;
}
