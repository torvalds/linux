/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Derived from arch/arm/kvm/guest.c:
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/kvm_host.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <asm/cputype.h>
#include <asm/uaccess.h>
#include <asm/kvm.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_coproc.h>

struct kvm_stats_debugfs_item debugfs_entries[] = {
	{ NULL }
};

int kvm_arch_vcpu_setup(struct kvm_vcpu *vcpu)
{
	vcpu->arch.hcr_el2 = HCR_GUEST_FLAGS;
	return 0;
}

static u64 core_reg_offset_from_id(u64 id)
{
	return id & ~(KVM_REG_ARCH_MASK | KVM_REG_SIZE_MASK | KVM_REG_ARM_CORE);
}

static int get_core_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	/*
	 * Because the kvm_regs structure is a mix of 32, 64 and
	 * 128bit fields, we index it as if it was a 32bit
	 * array. Hence below, nr_regs is the number of entries, and
	 * off the index in the "array".
	 */
	__u32 __user *uaddr = (__u32 __user *)(unsigned long)reg->addr;
	struct kvm_regs *regs = vcpu_gp_regs(vcpu);
	int nr_regs = sizeof(*regs) / sizeof(__u32);
	u32 off;

	/* Our ID is an index into the kvm_regs struct. */
	off = core_reg_offset_from_id(reg->id);
	if (off >= nr_regs ||
	    (off + (KVM_REG_SIZE(reg->id) / sizeof(__u32))) >= nr_regs)
		return -ENOENT;

	if (copy_to_user(uaddr, ((u32 *)regs) + off, KVM_REG_SIZE(reg->id)))
		return -EFAULT;

	return 0;
}

static int set_core_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	__u32 __user *uaddr = (__u32 __user *)(unsigned long)reg->addr;
	struct kvm_regs *regs = vcpu_gp_regs(vcpu);
	int nr_regs = sizeof(*regs) / sizeof(__u32);
	__uint128_t tmp;
	void *valp = &tmp;
	u64 off;
	int err = 0;

	/* Our ID is an index into the kvm_regs struct. */
	off = core_reg_offset_from_id(reg->id);
	if (off >= nr_regs ||
	    (off + (KVM_REG_SIZE(reg->id) / sizeof(__u32))) >= nr_regs)
		return -ENOENT;

	if (KVM_REG_SIZE(reg->id) > sizeof(tmp))
		return -EINVAL;

	if (copy_from_user(valp, uaddr, KVM_REG_SIZE(reg->id))) {
		err = -EFAULT;
		goto out;
	}

	if (off == KVM_REG_ARM_CORE_REG(regs.pstate)) {
		u32 mode = (*(u32 *)valp) & COMPAT_PSR_MODE_MASK;
		switch (mode) {
		case COMPAT_PSR_MODE_USR:
		case COMPAT_PSR_MODE_FIQ:
		case COMPAT_PSR_MODE_IRQ:
		case COMPAT_PSR_MODE_SVC:
		case COMPAT_PSR_MODE_ABT:
		case COMPAT_PSR_MODE_UND:
		case PSR_MODE_EL0t:
		case PSR_MODE_EL1t:
		case PSR_MODE_EL1h:
			break;
		default:
			err = -EINVAL;
			goto out;
		}
	}

	memcpy((u32 *)regs + off, valp, KVM_REG_SIZE(reg->id));
out:
	return err;
}

int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	return -EINVAL;
}

int kvm_arch_vcpu_ioctl_set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	return -EINVAL;
}

static unsigned long num_core_regs(void)
{
	return sizeof(struct kvm_regs) / sizeof(__u32);
}

/**
 * ARM64 versions of the TIMER registers, always available on arm64
 */

#define NUM_TIMER_REGS 3

static bool is_timer_reg(u64 index)
{
	switch (index) {
	case KVM_REG_ARM_TIMER_CTL:
	case KVM_REG_ARM_TIMER_CNT:
	case KVM_REG_ARM_TIMER_CVAL:
		return true;
	}
	return false;
}

static int copy_timer_indices(struct kvm_vcpu *vcpu, u64 __user *uindices)
{
	if (put_user(KVM_REG_ARM_TIMER_CTL, uindices))
		return -EFAULT;
	uindices++;
	if (put_user(KVM_REG_ARM_TIMER_CNT, uindices))
		return -EFAULT;
	uindices++;
	if (put_user(KVM_REG_ARM_TIMER_CVAL, uindices))
		return -EFAULT;

	return 0;
}

static int set_timer_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	void __user *uaddr = (void __user *)(long)reg->addr;
	u64 val;
	int ret;

	ret = copy_from_user(&val, uaddr, KVM_REG_SIZE(reg->id));
	if (ret != 0)
		return -EFAULT;

	return kvm_arm_timer_set_reg(vcpu, reg->id, val);
}

static int get_timer_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	void __user *uaddr = (void __user *)(long)reg->addr;
	u64 val;

	val = kvm_arm_timer_get_reg(vcpu, reg->id);
	return copy_to_user(uaddr, &val, KVM_REG_SIZE(reg->id));
}

/**
 * kvm_arm_num_regs - how many registers do we present via KVM_GET_ONE_REG
 *
 * This is for all registers.
 */
unsigned long kvm_arm_num_regs(struct kvm_vcpu *vcpu)
{
	return num_core_regs() + kvm_arm_num_sys_reg_descs(vcpu)
                + NUM_TIMER_REGS;
}

/**
 * kvm_arm_copy_reg_indices - get indices of all registers.
 *
 * We do core registers right here, then we apppend system regs.
 */
int kvm_arm_copy_reg_indices(struct kvm_vcpu *vcpu, u64 __user *uindices)
{
	unsigned int i;
	const u64 core_reg = KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE;
	int ret;

	for (i = 0; i < sizeof(struct kvm_regs) / sizeof(__u32); i++) {
		if (put_user(core_reg | i, uindices))
			return -EFAULT;
		uindices++;
	}

	ret = copy_timer_indices(vcpu, uindices);
	if (ret)
		return ret;
	uindices += NUM_TIMER_REGS;

	return kvm_arm_copy_sys_reg_indices(vcpu, uindices);
}

int kvm_arm_get_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	/* We currently use nothing arch-specific in upper 32 bits */
	if ((reg->id & ~KVM_REG_SIZE_MASK) >> 32 != KVM_REG_ARM64 >> 32)
		return -EINVAL;

	/* Register group 16 means we want a core register. */
	if ((reg->id & KVM_REG_ARM_COPROC_MASK) == KVM_REG_ARM_CORE)
		return get_core_reg(vcpu, reg);

	if (is_timer_reg(reg->id))
		return get_timer_reg(vcpu, reg);

	return kvm_arm_sys_reg_get_reg(vcpu, reg);
}

int kvm_arm_set_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	/* We currently use nothing arch-specific in upper 32 bits */
	if ((reg->id & ~KVM_REG_SIZE_MASK) >> 32 != KVM_REG_ARM64 >> 32)
		return -EINVAL;

	/* Register group 16 means we set a core register. */
	if ((reg->id & KVM_REG_ARM_COPROC_MASK) == KVM_REG_ARM_CORE)
		return set_core_reg(vcpu, reg);

	if (is_timer_reg(reg->id))
		return set_timer_reg(vcpu, reg);

	return kvm_arm_sys_reg_set_reg(vcpu, reg);
}

int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	return -EINVAL;
}

int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	return -EINVAL;
}

int __attribute_const__ kvm_target_cpu(void)
{
	unsigned long implementor = read_cpuid_implementor();
	unsigned long part_number = read_cpuid_part_number();

	switch (implementor) {
	case ARM_CPU_IMP_ARM:
		switch (part_number) {
		case ARM_CPU_PART_AEM_V8:
			return KVM_ARM_TARGET_AEM_V8;
		case ARM_CPU_PART_FOUNDATION:
			return KVM_ARM_TARGET_FOUNDATION_V8;
		case ARM_CPU_PART_CORTEX_A53:
			return KVM_ARM_TARGET_CORTEX_A53;
		case ARM_CPU_PART_CORTEX_A57:
			return KVM_ARM_TARGET_CORTEX_A57;
		};
		break;
	case ARM_CPU_IMP_APM:
		switch (part_number) {
		case APM_CPU_PART_POTENZA:
			return KVM_ARM_TARGET_XGENE_POTENZA;
		};
		break;
	};

	return -EINVAL;
}

int kvm_vcpu_set_target(struct kvm_vcpu *vcpu,
			const struct kvm_vcpu_init *init)
{
	unsigned int i;
	int phys_target = kvm_target_cpu();

	if (init->target != phys_target)
		return -EINVAL;

	vcpu->arch.target = phys_target;
	bitmap_zero(vcpu->arch.features, KVM_VCPU_MAX_FEATURES);

	/* -ENOENT for unknown features, -EINVAL for invalid combinations. */
	for (i = 0; i < sizeof(init->features) * 8; i++) {
		if (init->features[i / 32] & (1 << (i % 32))) {
			if (i >= KVM_VCPU_MAX_FEATURES)
				return -ENOENT;
			set_bit(i, vcpu->arch.features);
		}
	}

	/* Now we know what it is, we can reset it. */
	return kvm_reset_vcpu(vcpu);
}

int kvm_vcpu_preferred_target(struct kvm_vcpu_init *init)
{
	int target = kvm_target_cpu();

	if (target < 0)
		return -ENODEV;

	memset(init, 0, sizeof(*init));

	/*
	 * For now, we don't return any features.
	 * In future, we might use features to return target
	 * specific features available for the preferred
	 * target type.
	 */
	init->target = (__u32)target;

	return 0;
}

int kvm_arch_vcpu_ioctl_get_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	return -EINVAL;
}

int kvm_arch_vcpu_ioctl_set_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	return -EINVAL;
}

int kvm_arch_vcpu_ioctl_translate(struct kvm_vcpu *vcpu,
				  struct kvm_translation *tr)
{
	return -EINVAL;
}
