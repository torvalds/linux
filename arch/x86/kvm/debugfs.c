// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * Copyright 2016 Red Hat, Inc. and/or its affiliates.
 */
#include <linux/kvm_host.h>
#include <linux/debugfs.h>
#include "lapic.h"
#include "mmu.h"
#include "mmu/mmu_internal.h"

static int vcpu_get_timer_advance_ns(void *data, u64 *val)
{
	struct kvm_vcpu *vcpu = (struct kvm_vcpu *) data;
	*val = vcpu->arch.apic->lapic_timer.timer_advance_ns;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(vcpu_timer_advance_ns_fops, vcpu_get_timer_advance_ns, NULL, "%llu\n");

static int vcpu_get_guest_mode(void *data, u64 *val)
{
	struct kvm_vcpu *vcpu = (struct kvm_vcpu *) data;
	*val = vcpu->stat.guest_mode;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(vcpu_guest_mode_fops, vcpu_get_guest_mode, NULL, "%lld\n");

static int vcpu_get_tsc_offset(void *data, u64 *val)
{
	struct kvm_vcpu *vcpu = (struct kvm_vcpu *) data;
	*val = vcpu->arch.tsc_offset;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(vcpu_tsc_offset_fops, vcpu_get_tsc_offset, NULL, "%lld\n");

static int vcpu_get_tsc_scaling_ratio(void *data, u64 *val)
{
	struct kvm_vcpu *vcpu = (struct kvm_vcpu *) data;
	*val = vcpu->arch.tsc_scaling_ratio;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(vcpu_tsc_scaling_fops, vcpu_get_tsc_scaling_ratio, NULL, "%llu\n");

static int vcpu_get_tsc_scaling_frac_bits(void *data, u64 *val)
{
	*val = kvm_tsc_scaling_ratio_frac_bits;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(vcpu_tsc_scaling_frac_fops, vcpu_get_tsc_scaling_frac_bits, NULL, "%llu\n");

void kvm_arch_create_vcpu_debugfs(struct kvm_vcpu *vcpu, struct dentry *debugfs_dentry)
{
	debugfs_create_file("guest_mode", 0444, debugfs_dentry, vcpu,
			    &vcpu_guest_mode_fops);
	debugfs_create_file("tsc-offset", 0444, debugfs_dentry, vcpu,
			    &vcpu_tsc_offset_fops);

	if (lapic_in_kernel(vcpu))
		debugfs_create_file("lapic_timer_advance_ns", 0444,
				    debugfs_dentry, vcpu,
				    &vcpu_timer_advance_ns_fops);

	if (kvm_has_tsc_control) {
		debugfs_create_file("tsc-scaling-ratio", 0444,
				    debugfs_dentry, vcpu,
				    &vcpu_tsc_scaling_fops);
		debugfs_create_file("tsc-scaling-ratio-frac-bits", 0444,
				    debugfs_dentry, vcpu,
				    &vcpu_tsc_scaling_frac_fops);
	}
}

/*
 * This covers statistics <1024 (11=log(1024)+1), which should be enough to
 * cover RMAP_RECYCLE_THRESHOLD.
 */
#define  RMAP_LOG_SIZE  11

static const char *kvm_lpage_str[KVM_NR_PAGE_SIZES] = { "4K", "2M", "1G" };

static int kvm_mmu_rmaps_stat_show(struct seq_file *m, void *v)
{
	struct kvm_rmap_head *rmap;
	struct kvm *kvm = m->private;
	struct kvm_memory_slot *slot;
	struct kvm_memslots *slots;
	unsigned int lpage_size, index;
	/* Still small enough to be on the stack */
	unsigned int *log[KVM_NR_PAGE_SIZES], *cur;
	int i, j, k, l, ret;

	if (!kvm_memslots_have_rmaps(kvm))
		return 0;

	ret = -ENOMEM;
	memset(log, 0, sizeof(log));
	for (i = 0; i < KVM_NR_PAGE_SIZES; i++) {
		log[i] = kcalloc(RMAP_LOG_SIZE, sizeof(unsigned int), GFP_KERNEL);
		if (!log[i])
			goto out;
	}

	mutex_lock(&kvm->slots_lock);
	write_lock(&kvm->mmu_lock);

	for (i = 0; i < KVM_ADDRESS_SPACE_NUM; i++) {
		slots = __kvm_memslots(kvm, i);
		for (j = 0; j < slots->used_slots; j++) {
			slot = &slots->memslots[j];
			for (k = 0; k < KVM_NR_PAGE_SIZES; k++) {
				rmap = slot->arch.rmap[k];
				lpage_size = kvm_mmu_slot_lpages(slot, k + 1);
				cur = log[k];
				for (l = 0; l < lpage_size; l++) {
					index = ffs(pte_list_count(&rmap[l]));
					if (WARN_ON_ONCE(index >= RMAP_LOG_SIZE))
						index = RMAP_LOG_SIZE - 1;
					cur[index]++;
				}
			}
		}
	}

	write_unlock(&kvm->mmu_lock);
	mutex_unlock(&kvm->slots_lock);

	/* index=0 counts no rmap; index=1 counts 1 rmap */
	seq_printf(m, "Rmap_Count:\t0\t1\t");
	for (i = 2; i < RMAP_LOG_SIZE; i++) {
		j = 1 << (i - 1);
		k = (1 << i) - 1;
		seq_printf(m, "%d-%d\t", j, k);
	}
	seq_printf(m, "\n");

	for (i = 0; i < KVM_NR_PAGE_SIZES; i++) {
		seq_printf(m, "Level=%s:\t", kvm_lpage_str[i]);
		cur = log[i];
		for (j = 0; j < RMAP_LOG_SIZE; j++)
			seq_printf(m, "%d\t", cur[j]);
		seq_printf(m, "\n");
	}

	ret = 0;
out:
	for (i = 0; i < KVM_NR_PAGE_SIZES; i++)
		kfree(log[i]);

	return ret;
}

static int kvm_mmu_rmaps_stat_open(struct inode *inode, struct file *file)
{
	struct kvm *kvm = inode->i_private;

	if (!kvm_get_kvm_safe(kvm))
		return -ENOENT;

	return single_open(file, kvm_mmu_rmaps_stat_show, kvm);
}

static int kvm_mmu_rmaps_stat_release(struct inode *inode, struct file *file)
{
	struct kvm *kvm = inode->i_private;

	kvm_put_kvm(kvm);

	return single_release(inode, file);
}

static const struct file_operations mmu_rmaps_stat_fops = {
	.open		= kvm_mmu_rmaps_stat_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= kvm_mmu_rmaps_stat_release,
};

int kvm_arch_create_vm_debugfs(struct kvm *kvm)
{
	debugfs_create_file("mmu_rmaps_stat", 0644, kvm->debugfs_dentry, kvm,
			    &mmu_rmaps_stat_fops);
	return 0;
}
