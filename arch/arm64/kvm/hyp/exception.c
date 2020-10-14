// SPDX-License-Identifier: GPL-2.0-only
/*
 * Fault injection for both 32 and 64bit guests.
 *
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Based on arch/arm/kvm/emulate.c
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 */

#include <hyp/adjust_pc.h>
#include <linux/kvm_host.h>
#include <asm/kvm_emulate.h>

#if !defined (__KVM_NVHE_HYPERVISOR__) && !defined (__KVM_VHE_HYPERVISOR__)
#error Hypervisor code only!
#endif

static inline u64 __vcpu_read_sys_reg(const struct kvm_vcpu *vcpu, int reg)
{
	u64 val;

	if (__vcpu_read_sys_reg_from_cpu(reg, &val))
		return val;

	return __vcpu_sys_reg(vcpu, reg);
}

static inline void __vcpu_write_sys_reg(struct kvm_vcpu *vcpu, u64 val, int reg)
{
	if (__vcpu_write_sys_reg_to_cpu(val, reg))
		return;

	 __vcpu_sys_reg(vcpu, reg) = val;
}

static void __vcpu_write_spsr(struct kvm_vcpu *vcpu, u64 val)
{
	write_sysreg_el1(val, SYS_SPSR);
}

/*
 * This performs the exception entry at a given EL (@target_mode), stashing PC
 * and PSTATE into ELR and SPSR respectively, and compute the new PC/PSTATE.
 * The EL passed to this function *must* be a non-secure, privileged mode with
 * bit 0 being set (PSTATE.SP == 1).
 *
 * When an exception is taken, most PSTATE fields are left unchanged in the
 * handler. However, some are explicitly overridden (e.g. M[4:0]). Luckily all
 * of the inherited bits have the same position in the AArch64/AArch32 SPSR_ELx
 * layouts, so we don't need to shuffle these for exceptions from AArch32 EL0.
 *
 * For the SPSR_ELx layout for AArch64, see ARM DDI 0487E.a page C5-429.
 * For the SPSR_ELx layout for AArch32, see ARM DDI 0487E.a page C5-426.
 *
 * Here we manipulate the fields in order of the AArch64 SPSR_ELx layout, from
 * MSB to LSB.
 */
static void enter_exception64(struct kvm_vcpu *vcpu, unsigned long target_mode,
			      enum exception_type type)
{
	unsigned long sctlr, vbar, old, new, mode;
	u64 exc_offset;

	mode = *vcpu_cpsr(vcpu) & (PSR_MODE_MASK | PSR_MODE32_BIT);

	if      (mode == target_mode)
		exc_offset = CURRENT_EL_SP_ELx_VECTOR;
	else if ((mode | PSR_MODE_THREAD_BIT) == target_mode)
		exc_offset = CURRENT_EL_SP_EL0_VECTOR;
	else if (!(mode & PSR_MODE32_BIT))
		exc_offset = LOWER_EL_AArch64_VECTOR;
	else
		exc_offset = LOWER_EL_AArch32_VECTOR;

	switch (target_mode) {
	case PSR_MODE_EL1h:
		vbar = __vcpu_read_sys_reg(vcpu, VBAR_EL1);
		sctlr = __vcpu_read_sys_reg(vcpu, SCTLR_EL1);
		__vcpu_write_sys_reg(vcpu, *vcpu_pc(vcpu), ELR_EL1);
		break;
	default:
		/* Don't do that */
		BUG();
	}

	*vcpu_pc(vcpu) = vbar + exc_offset + type;

	old = *vcpu_cpsr(vcpu);
	new = 0;

	new |= (old & PSR_N_BIT);
	new |= (old & PSR_Z_BIT);
	new |= (old & PSR_C_BIT);
	new |= (old & PSR_V_BIT);

	// TODO: TCO (if/when ARMv8.5-MemTag is exposed to guests)

	new |= (old & PSR_DIT_BIT);

	// PSTATE.UAO is set to zero upon any exception to AArch64
	// See ARM DDI 0487E.a, page D5-2579.

	// PSTATE.PAN is unchanged unless SCTLR_ELx.SPAN == 0b0
	// SCTLR_ELx.SPAN is RES1 when ARMv8.1-PAN is not implemented
	// See ARM DDI 0487E.a, page D5-2578.
	new |= (old & PSR_PAN_BIT);
	if (!(sctlr & SCTLR_EL1_SPAN))
		new |= PSR_PAN_BIT;

	// PSTATE.SS is set to zero upon any exception to AArch64
	// See ARM DDI 0487E.a, page D2-2452.

	// PSTATE.IL is set to zero upon any exception to AArch64
	// See ARM DDI 0487E.a, page D1-2306.

	// PSTATE.SSBS is set to SCTLR_ELx.DSSBS upon any exception to AArch64
	// See ARM DDI 0487E.a, page D13-3258
	if (sctlr & SCTLR_ELx_DSSBS)
		new |= PSR_SSBS_BIT;

	// PSTATE.BTYPE is set to zero upon any exception to AArch64
	// See ARM DDI 0487E.a, pages D1-2293 to D1-2294.

	new |= PSR_D_BIT;
	new |= PSR_A_BIT;
	new |= PSR_I_BIT;
	new |= PSR_F_BIT;

	new |= target_mode;

	*vcpu_cpsr(vcpu) = new;
	__vcpu_write_spsr(vcpu, old);
}

void kvm_inject_exception(struct kvm_vcpu *vcpu)
{
	switch (vcpu->arch.flags & KVM_ARM64_EXCEPT_MASK) {
	case (KVM_ARM64_EXCEPT_AA64_ELx_SYNC |
	      KVM_ARM64_EXCEPT_AA64_EL1):
		enter_exception64(vcpu, PSR_MODE_EL1h, except_type_sync);
		break;
	default:
		/*
		 * Only EL1_SYNC makes sense so far, EL2_{SYNC,IRQ}
		 * will be implemented at some point. Everything
		 * else gets silently ignored.
		 */
		break;
	}
}
