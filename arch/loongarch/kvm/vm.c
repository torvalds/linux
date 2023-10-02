// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#include <linux/kvm_host.h>
#include <asm/kvm_mmu.h>

const struct _kvm_stats_desc kvm_vm_stats_desc[] = {
	KVM_GENERIC_VM_STATS(),
	STATS_DESC_ICOUNTER(VM, pages),
	STATS_DESC_ICOUNTER(VM, hugepages),
};

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
	int i;

	/* Allocate page table to map GPA -> RPA */
	kvm->arch.pgd = kvm_pgd_alloc();
	if (!kvm->arch.pgd)
		return -ENOMEM;

	kvm_init_vmcs(kvm);
	kvm->arch.gpa_size = BIT(cpu_vabits - 1);
	kvm->arch.root_level = CONFIG_PGTABLE_LEVELS - 1;
	kvm->arch.invalid_ptes[0] = 0;
	kvm->arch.invalid_ptes[1] = (unsigned long)invalid_pte_table;
#if CONFIG_PGTABLE_LEVELS > 2
	kvm->arch.invalid_ptes[2] = (unsigned long)invalid_pmd_table;
#endif
#if CONFIG_PGTABLE_LEVELS > 3
	kvm->arch.invalid_ptes[3] = (unsigned long)invalid_pud_table;
#endif
	for (i = 0; i <= kvm->arch.root_level; i++)
		kvm->arch.pte_shifts[i] = PAGE_SHIFT + i * (PAGE_SHIFT - 3);

	return 0;
}

void kvm_arch_destroy_vm(struct kvm *kvm)
{
	kvm_destroy_vcpus(kvm);
	free_page((unsigned long)kvm->arch.pgd);
	kvm->arch.pgd = NULL;
}

int kvm_vm_ioctl_check_extension(struct kvm *kvm, long ext)
{
	int r;

	switch (ext) {
	case KVM_CAP_ONE_REG:
	case KVM_CAP_ENABLE_CAP:
	case KVM_CAP_READONLY_MEM:
	case KVM_CAP_SYNC_MMU:
	case KVM_CAP_IMMEDIATE_EXIT:
	case KVM_CAP_IOEVENTFD:
	case KVM_CAP_MP_STATE:
		r = 1;
		break;
	case KVM_CAP_NR_VCPUS:
		r = num_online_cpus();
		break;
	case KVM_CAP_MAX_VCPUS:
		r = KVM_MAX_VCPUS;
		break;
	case KVM_CAP_MAX_VCPU_ID:
		r = KVM_MAX_VCPU_IDS;
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

int kvm_arch_vm_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	return -ENOIOCTLCMD;
}
