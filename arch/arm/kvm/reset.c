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
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kvm_host.h>
#include <linux/kvm.h>

#include <asm/unified.h>
#include <asm/ptrace.h>
#include <asm/cputype.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_coproc.h>

#include <kvm/arm_arch_timer.h>

/******************************************************************************
 * Cortex-A15 Reset Values
 */

static const int a15_max_cpu_idx = 3;

static struct kvm_regs a15_regs_reset = {
	.usr_regs.ARM_cpsr = SVC_MODE | PSR_A_BIT | PSR_I_BIT | PSR_F_BIT,
};

static const struct kvm_irq_level a15_vtimer_irq = {
	{ .irq = 27 },
	.level = 1,
};


/*******************************************************************************
 * Exported reset function
 */

/**
 * kvm_reset_vcpu - sets core registers and cp15 registers to reset value
 * @vcpu: The VCPU pointer
 *
 * This function finds the right table above and sets the registers on the
 * virtual CPU struct to their architectually defined reset values.
 */
int kvm_reset_vcpu(struct kvm_vcpu *vcpu)
{
	struct kvm_regs *reset_regs;
	const struct kvm_irq_level *cpu_vtimer_irq;

	switch (vcpu->arch.target) {
	case KVM_ARM_TARGET_CORTEX_A15:
		if (vcpu->vcpu_id > a15_max_cpu_idx)
			return -EINVAL;
		reset_regs = &a15_regs_reset;
		vcpu->arch.midr = read_cpuid_id();
		cpu_vtimer_irq = &a15_vtimer_irq;
		break;
	default:
		return -ENODEV;
	}

	/* Reset core registers */
	memcpy(&vcpu->arch.regs, reset_regs, sizeof(vcpu->arch.regs));

	/* Reset CP15 registers */
	kvm_reset_coprocs(vcpu);

	/* Reset arch_timer context */
	kvm_timer_vcpu_reset(vcpu, cpu_vtimer_irq);

	return 0;
}
