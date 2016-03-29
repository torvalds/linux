/*
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
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
#include <asm/kvm_emulate.h>
#include <asm/kvm_coproc.h>

#define VM_STAT(x) { #x, offsetof(struct kvm, stat.x), KVM_STAT_VM }
#define VCPU_STAT(x) { #x, offsetof(struct kvm_vcpu, stat.x), KVM_STAT_VCPU }

struct kvm_stats_debugfs_item debugfs_entries[] = {
	VCPU_STAT(hvc_exit_stat),
	VCPU_STAT(wfe_exit_stat),
	VCPU_STAT(wfi_exit_stat),
	VCPU_STAT(mmio_exit_user),
	VCPU_STAT(mmio_exit_kernel),
	VCPU_STAT(exits),
	{ NULL }
};

int kvm_arch_vcpu_setup(struct kvm_vcpu *vcpu)
{
	return 0;
}

static u64 core_reg_offset_from_id(u64 id)
{
	return id & ~(KVM_REG_ARCH_MASK | KVM_REG_SIZE_MASK | KVM_REG_ARM_CORE);
}

static int get_core_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	u32 __user *uaddr = (u32 __user *)(long)reg->addr;
	struct kvm_regs *regs = &vcpu->arch.ctxt.gp_regs;
	u64 off;

	if (KVM_REG_SIZE(reg->id) != 4)
		return -ENOENT;

	/* Our ID is an index into the kvm_regs struct. */
	off = core_reg_offset_from_id(reg->id);
	if (off >= sizeof(*regs) / KVM_REG_SIZE(reg->id))
		return -ENOENT;

	return put_user(((u32 *)regs)[off], uaddr);
}

static int set_core_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	u32 __user *uaddr = (u32 __user *)(long)reg->addr;
	struct kvm_regs *regs = &vcpu->arch.ctxt.gp_regs;
	u64 off, val;

	if (KVM_REG_SIZE(reg->id) != 4)
		return -ENOENT;

	/* Our ID is an index into the kvm_regs struct. */
	off = core_reg_offset_from_id(reg->id);
	if (off >= sizeof(*regs) / KVM_REG_SIZE(reg->id))
		return -ENOENT;

	if (get_user(val, uaddr) != 0)
		return -EFAULT;

	if (off == KVM_REG_ARM_CORE_REG(usr_regs.ARM_cpsr)) {
		unsigned long mode = val & MODE_MASK;
		switch (mode) {
		case USR_MODE:
		case FIQ_MODE:
		case IRQ_MODE:
		case SVC_MODE:
		case ABT_MODE:
		case UND_MODE:
			break;
		default:
			return -EINVAL;
		}
	}

	((u32 *)regs)[off] = val;
	return 0;
}

int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	return -EINVAL;
}

int kvm_arch_vcpu_ioctl_set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	return -EINVAL;
}

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
	return copy_to_user(uaddr, &val, KVM_REG_SIZE(reg->id)) ? -EFAULT : 0;
}

static unsigned long num_core_regs(void)
{
	return sizeof(struct kvm_regs) / sizeof(u32);
}

/**
 * kvm_arm_num_regs - how many registers do we present via KVM_GET_ONE_REG
 *
 * This is for all registers.
 */
unsigned long kvm_arm_num_regs(struct kvm_vcpu *vcpu)
{
	return num_core_regs() + kvm_arm_num_coproc_regs(vcpu)
		+ NUM_TIMER_REGS;
}

/**
 * kvm_arm_copy_reg_indices - get indices of all registers.
 *
 * We do core registers right here, then we apppend coproc regs.
 */
int kvm_arm_copy_reg_indices(struct kvm_vcpu *vcpu, u64 __user *uindices)
{
	unsigned int i;
	const u64 core_reg = KVM_REG_ARM | KVM_REG_SIZE_U32 | KVM_REG_ARM_CORE;
	int ret;

	for (i = 0; i < sizeof(struct kvm_regs)/sizeof(u32); i++) {
		if (put_user(core_reg | i, uindices))
			return -EFAULT;
		uindices++;
	}

	ret = copy_timer_indices(vcpu, uindices);
	if (ret)
		return ret;
	uindices += NUM_TIMER_REGS;

	return kvm_arm_copy_coproc_indices(vcpu, uindices);
}

int kvm_arm_get_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	/* We currently use nothing arch-specific in upper 32 bits */
	if ((reg->id & ~KVM_REG_SIZE_MASK) >> 32 != KVM_REG_ARM >> 32)
		return -EINVAL;

	/* Register group 16 means we want a core register. */
	if ((reg->id & KVM_REG_ARM_COPROC_MASK) == KVM_REG_ARM_CORE)
		return get_core_reg(vcpu, reg);

	if (is_timer_reg(reg->id))
		return get_timer_reg(vcpu, reg);

	return kvm_arm_coproc_get_reg(vcpu, reg);
}

int kvm_arm_set_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	/* We currently use nothing arch-specific in upper 32 bits */
	if ((reg->id & ~KVM_REG_SIZE_MASK) >> 32 != KVM_REG_ARM >> 32)
		return -EINVAL;

	/* Register group 16 means we set a core register. */
	if ((reg->id & KVM_REG_ARM_COPROC_MASK) == KVM_REG_ARM_CORE)
		return set_core_reg(vcpu, reg);

	if (is_timer_reg(reg->id))
		return set_timer_reg(vcpu, reg);

	return kvm_arm_coproc_set_reg(vcpu, reg);
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
	switch (read_cpuid_part()) {
	case ARM_CPU_PART_CORTEX_A7:
		return KVM_ARM_TARGET_CORTEX_A7;
	case ARM_CPU_PART_CORTEX_A15:
		return KVM_ARM_TARGET_CORTEX_A15;
	default:
		return -EINVAL;
	}
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

int kvm_arch_vcpu_ioctl_set_guest_debug(struct kvm_vcpu *vcpu,
					struct kvm_guest_debug *dbg)
{
	return -EINVAL;
}
