/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * Copyright 2016 Red Hat, Inc. and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */
#include <linux/kvm_host.h>
#include <linux/debugfs.h>

bool kvm_arch_has_vcpu_debugfs(void)
{
	return true;
}

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

int kvm_arch_create_vcpu_debugfs(struct kvm_vcpu *vcpu)
{
	struct dentry *ret;

	ret = debugfs_create_file("tsc-offset", 0444,
							vcpu->debugfs_dentry,
							vcpu, &vcpu_tsc_offset_fops);
	if (!ret)
		return -ENOMEM;

	if (kvm_has_tsc_control) {
		ret = debugfs_create_file("tsc-scaling-ratio", 0444,
							vcpu->debugfs_dentry,
							vcpu, &vcpu_tsc_scaling_fops);
		if (!ret)
			return -ENOMEM;
		ret = debugfs_create_file("tsc-scaling-ratio-frac-bits", 0444,
							vcpu->debugfs_dentry,
							vcpu, &vcpu_tsc_scaling_frac_fops);
		if (!ret)
			return -ENOMEM;

	}

	return 0;
}
