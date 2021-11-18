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

const struct _kvm_stats_desc kvm_vm_stats_desc[] = {
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

	r = kvm_riscv_stage2_alloc_pgd(kvm);
	if (r)
		return r;

	r = kvm_riscv_stage2_vmid_init(kvm);
	if (r) {
		kvm_riscv_stage2_free_pgd(kvm);
		return r;
	}

	return kvm_riscv_guest_timer_init(kvm);
}

void kvm_arch_destroy_vm(struct kvm *kvm)
{
	int i;

	for (i = 0; i < KVM_MAX_VCPUS; ++i) {
		if (kvm->vcpus[i]) {
			kvm_vcpu_destroy(kvm->vcpus[i]);
			kvm->vcpus[i] = NULL;
		}
	}
	atomic_set(&kvm->online_vcpus, 0);
}

int kvm_vm_ioctl_check_extension(struct kvm *kvm, long ext)
{
	int r;

	switch (ext) {
	case KVM_CAP_IOEVENTFD:
	case KVM_CAP_DEVICE_CTRL:
	case KVM_CAP_USER_MEMORY:
	case KVM_CAP_SYNC_MMU:
	case KVM_CAP_DESTROY_MEMORY_REGION_WORKS:
	case KVM_CAP_ONE_REG:
	case KVM_CAP_READONLY_MEM:
	case KVM_CAP_MP_STATE:
	case KVM_CAP_IMMEDIATE_EXIT:
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
	default:
		r = 0;
		break;
	}

	return r;
}

long kvm_arch_vm_ioctl(struct file *filp,
		       unsigned int ioctl, unsigned long arg)
{
	return -EINVAL;
}
