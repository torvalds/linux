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

#include <asm/cpufeature.h>
#include <asm/cputype.h>
#include <asm/ptrace.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_coproc.h>
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
 */
int kvm_reset_vcpu(struct kvm_vcpu *vcpu)
{
	const struct kvm_regs *cpu_reset;

	switch (vcpu->arch.target) {
	default:
		if (test_bit(KVM_ARM_VCPU_EL1_32BIT, vcpu->arch.features)) {
			if (!cpu_has_32bit_el1())
				return -EINVAL;
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

	/* Reset PMU */
	kvm_pmu_vcpu_reset(vcpu);

	/* Default workaround setup is enabled (if supported) */
	if (kvm_arm_have_ssbd() == KVM_SSBD_KERNEL)
		vcpu->arch.workaround_flags |= VCPU_WORKAROUND_2_FLAG;

	/* Reset timer */
	return kvm_timer_vcpu_reset(vcpu);
}

/*
 * Configure the VTCR_EL2 for this VM. The VTCR value is common
 * across all the physical CPUs on the system. We use system wide
 * sanitised values to fill in different fields, except for Hardware
 * Management of Access Flags. HA Flag is set unconditionally on
 * all CPUs, as it is safe to run with or without the feature and
 * the bit is RES0 on CPUs that don't support it.
 */
int kvm_arm_config_vm(struct kvm *kvm, unsigned long type)
{
	u64 vtcr = VTCR_EL2_FLAGS;
	u32 parange, phys_shift;

	if (type)
		return -EINVAL;

	parange = read_sanitised_ftr_reg(SYS_ID_AA64MMFR0_EL1) & 7;
	if (parange > ID_AA64MMFR0_PARANGE_MAX)
		parange = ID_AA64MMFR0_PARANGE_MAX;
	vtcr |= parange << VTCR_EL2_PS_SHIFT;

	phys_shift = id_aa64mmfr0_parange_to_phys_shift(parange);
	if (phys_shift > KVM_PHYS_SHIFT)
		phys_shift = KVM_PHYS_SHIFT;
	vtcr |= VTCR_EL2_T0SZ(phys_shift);
	vtcr |= VTCR_EL2_LVLS_TO_SL0(kvm_stage2_levels(kvm));

	/*
	 * Enable the Hardware Access Flag management, unconditionally
	 * on all CPUs. The features is RES0 on CPUs without the support
	 * and must be ignored by the CPUs.
	 */
	vtcr |= VTCR_EL2_HA;

	/* Set the vmid bits */
	vtcr |= (kvm_get_vmid_bits() == 16) ?
		VTCR_EL2_VS_16BIT :
		VTCR_EL2_VS_8BIT;
	kvm->arch.vtcr = vtcr;
	return 0;
}
