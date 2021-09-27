// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Anup Patel <anup.patel@wdc.com>
 */

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/hugetlb.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/kvm_host.h>
#include <linux/sched/signal.h>
#include <asm/page.h>
#include <asm/pgtable.h>

void kvm_arch_sync_dirty_log(struct kvm *kvm, struct kvm_memory_slot *memslot)
{
}

void kvm_arch_free_memslot(struct kvm *kvm, struct kvm_memory_slot *free)
{
}

void kvm_arch_memslots_updated(struct kvm *kvm, u64 gen)
{
}

void kvm_arch_flush_shadow_all(struct kvm *kvm)
{
	/* TODO: */
}

void kvm_arch_flush_shadow_memslot(struct kvm *kvm,
				   struct kvm_memory_slot *slot)
{
}

void kvm_arch_commit_memory_region(struct kvm *kvm,
				const struct kvm_userspace_memory_region *mem,
				struct kvm_memory_slot *old,
				const struct kvm_memory_slot *new,
				enum kvm_mr_change change)
{
	/* TODO: */
}

int kvm_arch_prepare_memory_region(struct kvm *kvm,
				struct kvm_memory_slot *memslot,
				const struct kvm_userspace_memory_region *mem,
				enum kvm_mr_change change)
{
	/* TODO: */
	return 0;
}

void kvm_riscv_stage2_flush_cache(struct kvm_vcpu *vcpu)
{
	/* TODO: */
}

int kvm_riscv_stage2_alloc_pgd(struct kvm *kvm)
{
	/* TODO: */
	return 0;
}

void kvm_riscv_stage2_free_pgd(struct kvm *kvm)
{
	/* TODO: */
}

void kvm_riscv_stage2_update_hgatp(struct kvm_vcpu *vcpu)
{
	/* TODO: */
}
