/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Derived from arch/arm/kvm/reset.c
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/errno.h>
#include <linux/kvm_host.h>
#include <linux/kvm.h>
#include <linux/hw_breakpoint.h>

#include <kvm/arm_arch_timer.h>

#include <asm/cputype.h>
#include <asm/ptrace.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_coproc.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_mmu.h>

/*
 * ARMv8 Reset Values
 */
static const struct kvm_regs default_regs_reset = {
	.regs.pstate = (PSR_MODE_EL1h | PSR_A_BIT | PSR_I_BIT |
			PSR_F_BIT | PSR_D_BIT),
};

static const struct kvm_regs default_regs_reset32 = {
	.regs.pstate = (PSR_AA32_MODE_SVC | PSR_AA32_A_BIT |
			PSR_AA32_I_BIT | PSR_AA32_F_BIT),
};

static bool cpu_has_32bit_el1(void)
{
	u64 pfr0;

	pfr0 = read_sanitised_ftr_reg(SYS_ID_AA64PFR0_EL1);
	return !!(pfr0 & 0x20);
}

/**
 * kvm_arch_dev_ioctl_check_extension
 *
 * We currently assume that the number of HW registers is uniform
 * across all CPUs (see cpuinfo_sanity_check).
 */
int kvm_arch_dev_ioctl_check_extension(struct kvm *kvm, long ext)
{
	int r;

	switch (ext) {
	case KVM_CAP_ARM_EL1_32BIT:
		r = cpu_has_32bit_el1();
		break;
	case KVM_CAP_GUEST_DEBUG_HW_BPS:
		r = get_num_brps();
		break;
	case KVM_CAP_GUEST_DEBUG_HW_WPS:
		r = get_num_wrps();
		break;
	case KVM_CAP_ARM_PMU_V3:
		r = kvm_arm_support_pmu_v3();
		break;
	case KVM_CAP_ARM_INJECT_SERROR_ESR:
		r = cpus_have_const_cap(ARM64_HAS_RAS_EXTN);
		break;
	case KVM_CAP_SET_GUEST_DEBUG:
	case KVM_CAP_VCPU_ATTRIBUTES:
	case KVM_CAP_VCPU_EVENTS:
		r = 1;
		break;
	default:
		r = 0;
	}

	return r;
}

/**
 * kvm_reset_vcpu - sets core registers and sys_regs to reset value
 * @vcpu: The VCPU pointer
 *
 * This function finds the right table above and sets the registers on
 * the virtual CPU struct to their architecturally defined reset
 * values.
 *
 * Note: This function can be called from two paths: The KVM_ARM_VCPU_INIT
 * ioctl or as part of handling a request issued by another VCPU in the PSCI
 * handling code.  In the first case, the VCPU will not be loaded, and in the
 * second case the VCPU will be loaded.  Because this function operates purely
 * on the memory-backed valus of system registers, we want to do a full put if
 * we were loaded (handling a request) and load the values back at the end of
 * the function.  Otherwise we leave the state alone.  In both cases, we
 * disable preemption around the vcpu reset as we would otherwise race with
 * preempt notifiers which also call put/load.
 */
int kvm_reset_vcpu(struct kvm_vcpu *vcpu)
{
	const struct kvm_regs *cpu_reset;
	int ret = -EINVAL;
	bool loaded;

	/* Reset PMU outside of the non-preemptible section */
	kvm_pmu_vcpu_reset(vcpu);

	preempt_disable();
	loaded = (vcpu->cpu != -1);
	if (loaded)
		kvm_arch_vcpu_put(vcpu);

	switch (vcpu->arch.target) {
	default:
		if (test_bit(KVM_ARM_VCPU_EL1_32BIT, vcpu->arch.features)) {
			if (!cpu_has_32bit_el1())
				goto out;
			cpu_reset = &default_regs_reset32;
		} else {
			cpu_reset = &default_regs_reset;
		}

		break;
	}

	/* Reset core registers */
	memcpy(vcpu_gp_regs(vcpu), cpu_reset, sizeof(*cpu_reset));

	/* Reset system registers */
	kvm_reset_sys_regs(vcpu);

	/*
	 * Additional reset state handling that PSCI may have imposed on us.
	 * Must be done after all the sys_reg reset.
	 */
	if (vcpu->arch.reset_state.reset) {
		unsigned long target_pc = vcpu->arch.reset_state.pc;

		/* Gracefully handle Thumb2 entry point */
		if (vcpu_mode_is_32bit(vcpu) && (target_pc & 1)) {
			target_pc &= ~1UL;
			vcpu_set_thumb(vcpu);
		}

		/* Propagate caller endianness */
		if (vcpu->arch.reset_state.be)
			kvm_vcpu_set_be(vcpu);

		*vcpu_pc(vcpu) = target_pc;
		vcpu_set_reg(vcpu, 0, vcpu->arch.reset_state.r0);

		vcpu->arch.reset_state.reset = false;
	}

	/* Default workaround setup is enabled (if supported) */
	if (kvm_arm_have_ssbd() == KVM_SSBD_KERNEL)
		vcpu->arch.workaround_flags |= VCPU_WORKAROUND_2_FLAG;

	/* Reset timer */
	ret = kvm_timer_vcpu_reset(vcpu);
out:
	if (loaded)
		kvm_arch_vcpu_load(vcpu, smp_processor_id());
	preempt_enable();
	return ret;
}
