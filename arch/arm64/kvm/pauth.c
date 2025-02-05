// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 - Google LLC
 * Author: Marc Zyngier <maz@kernel.org>
 *
 * Primitive PAuth emulation for ERETAA/ERETAB.
 *
 * This code assumes that is is run from EL2, and that it is part of
 * the emulation of ERETAx for a guest hypervisor. That's a lot of
 * baked-in assumptions and shortcuts.
 *
 * Do no reuse for anything else!
 */

#include <linux/kvm_host.h>

#include <asm/gpr-num.h>
#include <asm/kvm_emulate.h>
#include <asm/pointer_auth.h>

/* PACGA Xd, Xn, Xm */
#define PACGA(d,n,m)					\
	asm volatile(__DEFINE_ASM_GPR_NUMS		\
		     ".inst 0x9AC03000          |"	\
		     "(.L__gpr_num_%[Rd] << 0)  |"	\
		     "(.L__gpr_num_%[Rn] << 5)  |"	\
		     "(.L__gpr_num_%[Rm] << 16)\n"	\
		     : [Rd] "=r" ((d))			\
		     : [Rn] "r" ((n)), [Rm] "r" ((m)))

static u64 compute_pac(struct kvm_vcpu *vcpu, u64 ptr,
		       struct ptrauth_key ikey)
{
	struct ptrauth_key gkey;
	u64 mod, pac = 0;

	preempt_disable();

	if (!vcpu_get_flag(vcpu, SYSREGS_ON_CPU))
		mod = __vcpu_sys_reg(vcpu, SP_EL2);
	else
		mod = read_sysreg(sp_el1);

	gkey.lo = read_sysreg_s(SYS_APGAKEYLO_EL1);
	gkey.hi = read_sysreg_s(SYS_APGAKEYHI_EL1);

	__ptrauth_key_install_nosync(APGA, ikey);
	isb();

	PACGA(pac, ptr, mod);
	isb();

	__ptrauth_key_install_nosync(APGA, gkey);

	preempt_enable();

	/* PAC in the top 32bits */
	return pac;
}

static bool effective_tbi(struct kvm_vcpu *vcpu, bool bit55)
{
	u64 tcr = vcpu_read_sys_reg(vcpu, TCR_EL2);
	bool tbi, tbid;

	/*
	 * Since we are authenticating an instruction address, we have
	 * to take TBID into account. If E2H==0, ignore VA[55], as
	 * TCR_EL2 only has a single TBI/TBID. If VA[55] was set in
	 * this case, this is likely a guest bug...
	 */
	if (!vcpu_el2_e2h_is_set(vcpu)) {
		tbi = tcr & BIT(20);
		tbid = tcr & BIT(29);
	} else if (bit55) {
		tbi = tcr & TCR_TBI1;
		tbid = tcr & TCR_TBID1;
	} else {
		tbi = tcr & TCR_TBI0;
		tbid = tcr & TCR_TBID0;
	}

	return tbi && !tbid;
}

static int compute_bottom_pac(struct kvm_vcpu *vcpu, bool bit55)
{
	static const int maxtxsz = 39; // Revisit these two values once
	static const int mintxsz = 16; // (if) we support TTST/LVA/LVA2
	u64 tcr = vcpu_read_sys_reg(vcpu, TCR_EL2);
	int txsz;

	if (!vcpu_el2_e2h_is_set(vcpu) || !bit55)
		txsz = FIELD_GET(TCR_T0SZ_MASK, tcr);
	else
		txsz = FIELD_GET(TCR_T1SZ_MASK, tcr);

	return 64 - clamp(txsz, mintxsz, maxtxsz);
}

static u64 compute_pac_mask(struct kvm_vcpu *vcpu, bool bit55)
{
	int bottom_pac;
	u64 mask;

	bottom_pac = compute_bottom_pac(vcpu, bit55);

	mask = GENMASK(54, bottom_pac);
	if (!effective_tbi(vcpu, bit55))
		mask |= GENMASK(63, 56);

	return mask;
}

static u64 to_canonical_addr(struct kvm_vcpu *vcpu, u64 ptr, u64 mask)
{
	bool bit55 = !!(ptr & BIT(55));

	if (bit55)
		return ptr | mask;

	return ptr & ~mask;
}

static u64 corrupt_addr(struct kvm_vcpu *vcpu, u64 ptr)
{
	bool bit55 = !!(ptr & BIT(55));
	u64 mask, error_code;
	int shift;

	if (effective_tbi(vcpu, bit55)) {
		mask = GENMASK(54, 53);
		shift = 53;
	} else {
		mask = GENMASK(62, 61);
		shift = 61;
	}

	if (esr_iss_is_eretab(kvm_vcpu_get_esr(vcpu)))
		error_code = 2 << shift;
	else
		error_code = 1 << shift;

	ptr &= ~mask;
	ptr |= error_code;

	return ptr;
}

/*
 * Authenticate an ERETAA/ERETAB instruction, returning true if the
 * authentication succeeded and false otherwise. In all cases, *elr
 * contains the VA to ERET to. Potential exception injection is left
 * to the caller.
 */
bool kvm_auth_eretax(struct kvm_vcpu *vcpu, u64 *elr)
{
	u64 sctlr = vcpu_read_sys_reg(vcpu, SCTLR_EL2);
	u64 esr = kvm_vcpu_get_esr(vcpu);
	u64 ptr, cptr, pac, mask;
	struct ptrauth_key ikey;

	*elr = ptr = vcpu_read_sys_reg(vcpu, ELR_EL2);

	/* We assume we're already in the context of an ERETAx */
	if (esr_iss_is_eretab(esr)) {
		if (!(sctlr & SCTLR_EL1_EnIB))
			return true;

		ikey.lo = __vcpu_sys_reg(vcpu, APIBKEYLO_EL1);
		ikey.hi = __vcpu_sys_reg(vcpu, APIBKEYHI_EL1);
	} else {
		if (!(sctlr & SCTLR_EL1_EnIA))
			return true;

		ikey.lo = __vcpu_sys_reg(vcpu, APIAKEYLO_EL1);
		ikey.hi = __vcpu_sys_reg(vcpu, APIAKEYHI_EL1);
	}

	mask = compute_pac_mask(vcpu, !!(ptr & BIT(55)));
	cptr = to_canonical_addr(vcpu, ptr, mask);

	pac = compute_pac(vcpu, cptr, ikey);

	/*
	 * Slightly deviate from the pseudocode: if we have a PAC
	 * match with the signed pointer, then it must be good.
	 * Anything after this point is pure error handling.
	 */
	if ((pac & mask) == (ptr & mask)) {
		*elr = cptr;
		return true;
	}

	/*
	 * Authentication failed, corrupt the canonical address if
	 * PAuth2 isn't implemented, or some XORing if it is.
	 */
	if (!kvm_has_pauth(vcpu->kvm, PAuth2))
		cptr = corrupt_addr(vcpu, cptr);
	else
		cptr = ptr ^ (pac & mask);

	*elr = cptr;
	return false;
}
