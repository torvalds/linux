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
	if (has_vhe())
		write_sysreg_el1(val, SYS_SPSR);
	else
		__vcpu_sys_reg(vcpu, SPSR_EL1) = val;
}

static void __vcpu_write_spsr_abt(struct kvm_vcpu *vcpu, u64 val)
{
	if (has_vhe())
		write_sysreg(val, spsr_abt);
	else
		vcpu->arch.ctxt.spsr_abt = val;
}

static void __vcpu_write_spsr_und(struct kvm_vcpu *vcpu, u64 val)
{
	if (has_vhe())
		write_sysreg(val, spsr_und);
	else
		vcpu->arch.ctxt.spsr_und = val;
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

	if (kvm_has_mte(vcpu->kvm))
		new |= PSR_TCO_BIT;

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

/*
 * When an exception is taken, most CPSR fields are left unchanged in the
 * handler. However, some are explicitly overridden (e.g. M[4:0]).
 *
 * The SPSR/SPSR_ELx layouts differ, and the below is intended to work with
 * either format. Note: SPSR.J bit doesn't exist in SPSR_ELx, but this bit was
 * obsoleted by the ARMv7 virtualization extensions and is RES0.
 *
 * For the SPSR layout seen from AArch32, see:
 * - ARM DDI 0406C.d, page B1-1148
 * - ARM DDI 0487E.a, page G8-6264
 *
 * For the SPSR_ELx layout for AArch32 seen from AArch64, see:
 * - ARM DDI 0487E.a, page C5-426
 *
 * Here we manipulate the fields in order of the AArch32 SPSR_ELx layout, from
 * MSB to LSB.
 */
static unsigned long get_except32_cpsr(struct kvm_vcpu *vcpu, u32 mode)
{
	u32 sctlr = __vcpu_read_sys_reg(vcpu, SCTLR_EL1);
	unsigned long old, new;

	old = *vcpu_cpsr(vcpu);
	new = 0;

	new |= (old & PSR_AA32_N_BIT);
	new |= (old & PSR_AA32_Z_BIT);
	new |= (old & PSR_AA32_C_BIT);
	new |= (old & PSR_AA32_V_BIT);
	new |= (old & PSR_AA32_Q_BIT);

	// CPSR.IT[7:0] are set to zero upon any exception
	// See ARM DDI 0487E.a, section G1.12.3
	// See ARM DDI 0406C.d, section B1.8.3

	new |= (old & PSR_AA32_DIT_BIT);

	// CPSR.SSBS is set to SCTLR.DSSBS upon any exception
	// See ARM DDI 0487E.a, page G8-6244
	if (sctlr & BIT(31))
		new |= PSR_AA32_SSBS_BIT;

	// CPSR.PAN is unchanged unless SCTLR.SPAN == 0b0
	// SCTLR.SPAN is RES1 when ARMv8.1-PAN is not implemented
	// See ARM DDI 0487E.a, page G8-6246
	new |= (old & PSR_AA32_PAN_BIT);
	if (!(sctlr & BIT(23)))
		new |= PSR_AA32_PAN_BIT;

	// SS does not exist in AArch32, so ignore

	// CPSR.IL is set to zero upon any exception
	// See ARM DDI 0487E.a, page G1-5527

	new |= (old & PSR_AA32_GE_MASK);

	// CPSR.IT[7:0] are set to zero upon any exception
	// See prior comment above

	// CPSR.E is set to SCTLR.EE upon any exception
	// See ARM DDI 0487E.a, page G8-6245
	// See ARM DDI 0406C.d, page B4-1701
	if (sctlr & BIT(25))
		new |= PSR_AA32_E_BIT;

	// CPSR.A is unchanged upon an exception to Undefined, Supervisor
	// CPSR.A is set upon an exception to other modes
	// See ARM DDI 0487E.a, pages G1-5515 to G1-5516
	// See ARM DDI 0406C.d, page B1-1182
	new |= (old & PSR_AA32_A_BIT);
	if (mode != PSR_AA32_MODE_UND && mode != PSR_AA32_MODE_SVC)
		new |= PSR_AA32_A_BIT;

	// CPSR.I is set upon any exception
	// See ARM DDI 0487E.a, pages G1-5515 to G1-5516
	// See ARM DDI 0406C.d, page B1-1182
	new |= PSR_AA32_I_BIT;

	// CPSR.F is set upon an exception to FIQ
	// CPSR.F is unchanged upon an exception to other modes
	// See ARM DDI 0487E.a, pages G1-5515 to G1-5516
	// See ARM DDI 0406C.d, page B1-1182
	new |= (old & PSR_AA32_F_BIT);
	if (mode == PSR_AA32_MODE_FIQ)
		new |= PSR_AA32_F_BIT;

	// CPSR.T is set to SCTLR.TE upon any exception
	// See ARM DDI 0487E.a, page G8-5514
	// See ARM DDI 0406C.d, page B1-1181
	if (sctlr & BIT(30))
		new |= PSR_AA32_T_BIT;

	new |= mode;

	return new;
}

/*
 * Table taken from ARMv8 ARM DDI0487B-B, table G1-10.
 */
static const u8 return_offsets[8][2] = {
	[0] = { 0, 0 },		/* Reset, unused */
	[1] = { 4, 2 },		/* Undefined */
	[2] = { 0, 0 },		/* SVC, unused */
	[3] = { 4, 4 },		/* Prefetch abort */
	[4] = { 8, 8 },		/* Data abort */
	[5] = { 0, 0 },		/* HVC, unused */
	[6] = { 4, 4 },		/* IRQ, unused */
	[7] = { 4, 4 },		/* FIQ, unused */
};

static void enter_exception32(struct kvm_vcpu *vcpu, u32 mode, u32 vect_offset)
{
	unsigned long spsr = *vcpu_cpsr(vcpu);
	bool is_thumb = (spsr & PSR_AA32_T_BIT);
	u32 sctlr = __vcpu_read_sys_reg(vcpu, SCTLR_EL1);
	u32 return_address;

	*vcpu_cpsr(vcpu) = get_except32_cpsr(vcpu, mode);
	return_address   = *vcpu_pc(vcpu);
	return_address  += return_offsets[vect_offset >> 2][is_thumb];

	/* KVM only enters the ABT and UND modes, so only deal with those */
	switch(mode) {
	case PSR_AA32_MODE_ABT:
		__vcpu_write_spsr_abt(vcpu, host_spsr_to_spsr32(spsr));
		vcpu_gp_regs(vcpu)->compat_lr_abt = return_address;
		break;

	case PSR_AA32_MODE_UND:
		__vcpu_write_spsr_und(vcpu, host_spsr_to_spsr32(spsr));
		vcpu_gp_regs(vcpu)->compat_lr_und = return_address;
		break;
	}

	/* Branch to exception vector */
	if (sctlr & (1 << 13))
		vect_offset += 0xffff0000;
	else /* always have security exceptions */
		vect_offset += __vcpu_read_sys_reg(vcpu, VBAR_EL1);

	*vcpu_pc(vcpu) = vect_offset;
}

static void kvm_inject_exception(struct kvm_vcpu *vcpu)
{
	if (vcpu_el1_is_32bit(vcpu)) {
		switch (vcpu->arch.flags & KVM_ARM64_EXCEPT_MASK) {
		case KVM_ARM64_EXCEPT_AA32_UND:
			enter_exception32(vcpu, PSR_AA32_MODE_UND, 4);
			break;
		case KVM_ARM64_EXCEPT_AA32_IABT:
			enter_exception32(vcpu, PSR_AA32_MODE_ABT, 12);
			break;
		case KVM_ARM64_EXCEPT_AA32_DABT:
			enter_exception32(vcpu, PSR_AA32_MODE_ABT, 16);
			break;
		default:
			/* Err... */
			break;
		}
	} else {
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
}

/*
 * Adjust the guest PC (and potentially exception state) depending on
 * flags provided by the emulation code.
 */
void __kvm_adjust_pc(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.flags & KVM_ARM64_PENDING_EXCEPTION) {
		kvm_inject_exception(vcpu);
		vcpu->arch.flags &= ~(KVM_ARM64_PENDING_EXCEPTION |
				      KVM_ARM64_EXCEPT_MASK);
	} else 	if (vcpu->arch.flags & KVM_ARM64_INCREMENT_PC) {
		kvm_skip_instr(vcpu);
		vcpu->arch.flags &= ~KVM_ARM64_INCREMENT_PC;
	}
}
