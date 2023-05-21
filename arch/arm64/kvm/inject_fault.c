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

#include <linux/kvm_host.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_nested.h>
#include <asm/esr.h>

static void pend_sync_exception(struct kvm_vcpu *vcpu)
{
	/* If not nesting, EL1 is the only possible exception target */
	if (likely(!vcpu_has_nv(vcpu))) {
		kvm_pend_exception(vcpu, EXCEPT_AA64_EL1_SYNC);
		return;
	}

	/*
	 * With NV, we need to pick between EL1 and EL2. Note that we
	 * never deal with a nesting exception here, hence never
	 * changing context, and the exception itself can be delayed
	 * until the next entry.
	 */
	switch(*vcpu_cpsr(vcpu) & PSR_MODE_MASK) {
	case PSR_MODE_EL2h:
	case PSR_MODE_EL2t:
		kvm_pend_exception(vcpu, EXCEPT_AA64_EL2_SYNC);
		break;
	case PSR_MODE_EL1h:
	case PSR_MODE_EL1t:
		kvm_pend_exception(vcpu, EXCEPT_AA64_EL1_SYNC);
		break;
	case PSR_MODE_EL0t:
		if (vcpu_el2_tge_is_set(vcpu))
			kvm_pend_exception(vcpu, EXCEPT_AA64_EL2_SYNC);
		else
			kvm_pend_exception(vcpu, EXCEPT_AA64_EL1_SYNC);
		break;
	default:
		BUG();
	}
}

static bool match_target_el(struct kvm_vcpu *vcpu, unsigned long target)
{
	return (vcpu_get_flag(vcpu, EXCEPT_MASK) == target);
}

static void inject_abt64(struct kvm_vcpu *vcpu, bool is_iabt, unsigned long addr)
{
	unsigned long cpsr = *vcpu_cpsr(vcpu);
	bool is_aarch32 = vcpu_mode_is_32bit(vcpu);
	u64 esr = 0;

	pend_sync_exception(vcpu);

	/*
	 * Build an {i,d}abort, depending on the level and the
	 * instruction set. Report an external synchronous abort.
	 */
	if (kvm_vcpu_trap_il_is32bit(vcpu))
		esr |= ESR_ELx_IL;

	/*
	 * Here, the guest runs in AArch64 mode when in EL1. If we get
	 * an AArch32 fault, it means we managed to trap an EL0 fault.
	 */
	if (is_aarch32 || (cpsr & PSR_MODE_MASK) == PSR_MODE_EL0t)
		esr |= (ESR_ELx_EC_IABT_LOW << ESR_ELx_EC_SHIFT);
	else
		esr |= (ESR_ELx_EC_IABT_CUR << ESR_ELx_EC_SHIFT);

	if (!is_iabt)
		esr |= ESR_ELx_EC_DABT_LOW << ESR_ELx_EC_SHIFT;

	esr |= ESR_ELx_FSC_EXTABT;

	if (match_target_el(vcpu, unpack_vcpu_flag(EXCEPT_AA64_EL1_SYNC))) {
		vcpu_write_sys_reg(vcpu, addr, FAR_EL1);
		vcpu_write_sys_reg(vcpu, esr, ESR_EL1);
	} else {
		vcpu_write_sys_reg(vcpu, addr, FAR_EL2);
		vcpu_write_sys_reg(vcpu, esr, ESR_EL2);
	}
}

static void inject_undef64(struct kvm_vcpu *vcpu)
{
	u64 esr = (ESR_ELx_EC_UNKNOWN << ESR_ELx_EC_SHIFT);

	pend_sync_exception(vcpu);

	/*
	 * Build an unknown exception, depending on the instruction
	 * set.
	 */
	if (kvm_vcpu_trap_il_is32bit(vcpu))
		esr |= ESR_ELx_IL;

	if (match_target_el(vcpu, unpack_vcpu_flag(EXCEPT_AA64_EL1_SYNC)))
		vcpu_write_sys_reg(vcpu, esr, ESR_EL1);
	else
		vcpu_write_sys_reg(vcpu, esr, ESR_EL2);
}

#define DFSR_FSC_EXTABT_LPAE	0x10
#define DFSR_FSC_EXTABT_nLPAE	0x08
#define DFSR_LPAE		BIT(9)
#define TTBCR_EAE		BIT(31)

static void inject_undef32(struct kvm_vcpu *vcpu)
{
	kvm_pend_exception(vcpu, EXCEPT_AA32_UND);
}

/*
 * Modelled after TakeDataAbortException() and TakePrefetchAbortException
 * pseudocode.
 */
static void inject_abt32(struct kvm_vcpu *vcpu, bool is_pabt, u32 addr)
{
	u64 far;
	u32 fsr;

	/* Give the guest an IMPLEMENTATION DEFINED exception */
	if (vcpu_read_sys_reg(vcpu, TCR_EL1) & TTBCR_EAE) {
		fsr = DFSR_LPAE | DFSR_FSC_EXTABT_LPAE;
	} else {
		/* no need to shuffle FS[4] into DFSR[10] as its 0 */
		fsr = DFSR_FSC_EXTABT_nLPAE;
	}

	far = vcpu_read_sys_reg(vcpu, FAR_EL1);

	if (is_pabt) {
		kvm_pend_exception(vcpu, EXCEPT_AA32_IABT);
		far &= GENMASK(31, 0);
		far |= (u64)addr << 32;
		vcpu_write_sys_reg(vcpu, fsr, IFSR32_EL2);
	} else { /* !iabt */
		kvm_pend_exception(vcpu, EXCEPT_AA32_DABT);
		far &= GENMASK(63, 32);
		far |= addr;
		vcpu_write_sys_reg(vcpu, fsr, ESR_EL1);
	}

	vcpu_write_sys_reg(vcpu, far, FAR_EL1);
}

/**
 * kvm_inject_dabt - inject a data abort into the guest
 * @vcpu: The VCPU to receive the data abort
 * @addr: The address to report in the DFAR
 *
 * It is assumed that this code is called from the VCPU thread and that the
 * VCPU therefore is not currently executing guest code.
 */
void kvm_inject_dabt(struct kvm_vcpu *vcpu, unsigned long addr)
{
	if (vcpu_el1_is_32bit(vcpu))
		inject_abt32(vcpu, false, addr);
	else
		inject_abt64(vcpu, false, addr);
}

/**
 * kvm_inject_pabt - inject a prefetch abort into the guest
 * @vcpu: The VCPU to receive the prefetch abort
 * @addr: The address to report in the DFAR
 *
 * It is assumed that this code is called from the VCPU thread and that the
 * VCPU therefore is not currently executing guest code.
 */
void kvm_inject_pabt(struct kvm_vcpu *vcpu, unsigned long addr)
{
	if (vcpu_el1_is_32bit(vcpu))
		inject_abt32(vcpu, true, addr);
	else
		inject_abt64(vcpu, true, addr);
}

void kvm_inject_size_fault(struct kvm_vcpu *vcpu)
{
	unsigned long addr, esr;

	addr  = kvm_vcpu_get_fault_ipa(vcpu);
	addr |= kvm_vcpu_get_hfar(vcpu) & GENMASK(11, 0);

	if (kvm_vcpu_trap_is_iabt(vcpu))
		kvm_inject_pabt(vcpu, addr);
	else
		kvm_inject_dabt(vcpu, addr);

	/*
	 * If AArch64 or LPAE, set FSC to 0 to indicate an Address
	 * Size Fault at level 0, as if exceeding PARange.
	 *
	 * Non-LPAE guests will only get the external abort, as there
	 * is no way to describe the ASF.
	 */
	if (vcpu_el1_is_32bit(vcpu) &&
	    !(vcpu_read_sys_reg(vcpu, TCR_EL1) & TTBCR_EAE))
		return;

	esr = vcpu_read_sys_reg(vcpu, ESR_EL1);
	esr &= ~GENMASK_ULL(5, 0);
	vcpu_write_sys_reg(vcpu, esr, ESR_EL1);
}

/**
 * kvm_inject_undefined - inject an undefined instruction into the guest
 * @vcpu: The vCPU in which to inject the exception
 *
 * It is assumed that this code is called from the VCPU thread and that the
 * VCPU therefore is not currently executing guest code.
 */
void kvm_inject_undefined(struct kvm_vcpu *vcpu)
{
	if (vcpu_el1_is_32bit(vcpu))
		inject_undef32(vcpu);
	else
		inject_undef64(vcpu);
}

void kvm_set_sei_esr(struct kvm_vcpu *vcpu, u64 esr)
{
	vcpu_set_vsesr(vcpu, esr & ESR_ELx_ISS_MASK);
	*vcpu_hcr(vcpu) |= HCR_VSE;
}

/**
 * kvm_inject_vabt - inject an async abort / SError into the guest
 * @vcpu: The VCPU to receive the exception
 *
 * It is assumed that this code is called from the VCPU thread and that the
 * VCPU therefore is not currently executing guest code.
 *
 * Systems with the RAS Extensions specify an imp-def ESR (ISV/IDS = 1) with
 * the remaining ISS all-zeros so that this error is not interpreted as an
 * uncategorized RAS error. Without the RAS Extensions we can't specify an ESR
 * value, so the CPU generates an imp-def value.
 */
void kvm_inject_vabt(struct kvm_vcpu *vcpu)
{
	kvm_set_sei_esr(vcpu, ESR_ELx_ISV);
}
