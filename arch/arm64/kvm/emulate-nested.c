// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 - Linaro and Columbia University
 * Author: Jintack Lim <jintack.lim@linaro.org>
 */

#include <linux/kvm.h>
#include <linux/kvm_host.h>

#include <asm/kvm_emulate.h>
#include <asm/kvm_nested.h>

#include "hyp/include/hyp/adjust_pc.h"

#include "trace.h"

static u64 kvm_check_illegal_exception_return(struct kvm_vcpu *vcpu, u64 spsr)
{
	u64 mode = spsr & PSR_MODE_MASK;

	/*
	 * Possible causes for an Illegal Exception Return from EL2:
	 * - trying to return to EL3
	 * - trying to return to an illegal M value
	 * - trying to return to a 32bit EL
	 * - trying to return to EL1 with HCR_EL2.TGE set
	 */
	if (mode == PSR_MODE_EL3t   || mode == PSR_MODE_EL3h ||
	    mode == 0b00001         || (mode & BIT(1))       ||
	    (spsr & PSR_MODE32_BIT) ||
	    (vcpu_el2_tge_is_set(vcpu) && (mode == PSR_MODE_EL1t ||
					   mode == PSR_MODE_EL1h))) {
		/*
		 * The guest is playing with our nerves. Preserve EL, SP,
		 * masks, flags from the existing PSTATE, and set IL.
		 * The HW will then generate an Illegal State Exception
		 * immediately after ERET.
		 */
		spsr = *vcpu_cpsr(vcpu);

		spsr &= (PSR_D_BIT | PSR_A_BIT | PSR_I_BIT | PSR_F_BIT |
			 PSR_N_BIT | PSR_Z_BIT | PSR_C_BIT | PSR_V_BIT |
			 PSR_MODE_MASK | PSR_MODE32_BIT);
		spsr |= PSR_IL_BIT;
	}

	return spsr;
}

void kvm_emulate_nested_eret(struct kvm_vcpu *vcpu)
{
	u64 spsr, elr, mode;
	bool direct_eret;

	/*
	 * Going through the whole put/load motions is a waste of time
	 * if this is a VHE guest hypervisor returning to its own
	 * userspace, or the hypervisor performing a local exception
	 * return. No need to save/restore registers, no need to
	 * switch S2 MMU. Just do the canonical ERET.
	 */
	spsr = vcpu_read_sys_reg(vcpu, SPSR_EL2);
	spsr = kvm_check_illegal_exception_return(vcpu, spsr);

	mode = spsr & (PSR_MODE_MASK | PSR_MODE32_BIT);

	direct_eret  = (mode == PSR_MODE_EL0t &&
			vcpu_el2_e2h_is_set(vcpu) &&
			vcpu_el2_tge_is_set(vcpu));
	direct_eret |= (mode == PSR_MODE_EL2h || mode == PSR_MODE_EL2t);

	if (direct_eret) {
		*vcpu_pc(vcpu) = vcpu_read_sys_reg(vcpu, ELR_EL2);
		*vcpu_cpsr(vcpu) = spsr;
		trace_kvm_nested_eret(vcpu, *vcpu_pc(vcpu), spsr);
		return;
	}

	preempt_disable();
	kvm_arch_vcpu_put(vcpu);

	elr = __vcpu_sys_reg(vcpu, ELR_EL2);

	trace_kvm_nested_eret(vcpu, elr, spsr);

	/*
	 * Note that the current exception level is always the virtual EL2,
	 * since we set HCR_EL2.NV bit only when entering the virtual EL2.
	 */
	*vcpu_pc(vcpu) = elr;
	*vcpu_cpsr(vcpu) = spsr;

	kvm_arch_vcpu_load(vcpu, smp_processor_id());
	preempt_enable();
}

static void kvm_inject_el2_exception(struct kvm_vcpu *vcpu, u64 esr_el2,
				     enum exception_type type)
{
	trace_kvm_inject_nested_exception(vcpu, esr_el2, type);

	switch (type) {
	case except_type_sync:
		kvm_pend_exception(vcpu, EXCEPT_AA64_EL2_SYNC);
		vcpu_write_sys_reg(vcpu, esr_el2, ESR_EL2);
		break;
	case except_type_irq:
		kvm_pend_exception(vcpu, EXCEPT_AA64_EL2_IRQ);
		break;
	default:
		WARN_ONCE(1, "Unsupported EL2 exception injection %d\n", type);
	}
}

/*
 * Emulate taking an exception to EL2.
 * See ARM ARM J8.1.2 AArch64.TakeException()
 */
static int kvm_inject_nested(struct kvm_vcpu *vcpu, u64 esr_el2,
			     enum exception_type type)
{
	u64 pstate, mode;
	bool direct_inject;

	if (!vcpu_has_nv(vcpu)) {
		kvm_err("Unexpected call to %s for the non-nesting configuration\n",
				__func__);
		return -EINVAL;
	}

	/*
	 * As for ERET, we can avoid doing too much on the injection path by
	 * checking that we either took the exception from a VHE host
	 * userspace or from vEL2. In these cases, there is no change in
	 * translation regime (or anything else), so let's do as little as
	 * possible.
	 */
	pstate = *vcpu_cpsr(vcpu);
	mode = pstate & (PSR_MODE_MASK | PSR_MODE32_BIT);

	direct_inject  = (mode == PSR_MODE_EL0t &&
			  vcpu_el2_e2h_is_set(vcpu) &&
			  vcpu_el2_tge_is_set(vcpu));
	direct_inject |= (mode == PSR_MODE_EL2h || mode == PSR_MODE_EL2t);

	if (direct_inject) {
		kvm_inject_el2_exception(vcpu, esr_el2, type);
		return 1;
	}

	preempt_disable();

	/*
	 * We may have an exception or PC update in the EL0/EL1 context.
	 * Commit it before entering EL2.
	 */
	__kvm_adjust_pc(vcpu);

	kvm_arch_vcpu_put(vcpu);

	kvm_inject_el2_exception(vcpu, esr_el2, type);

	/*
	 * A hard requirement is that a switch between EL1 and EL2
	 * contexts has to happen between a put/load, so that we can
	 * pick the correct timer and interrupt configuration, among
	 * other things.
	 *
	 * Make sure the exception actually took place before we load
	 * the new context.
	 */
	__kvm_adjust_pc(vcpu);

	kvm_arch_vcpu_load(vcpu, smp_processor_id());
	preempt_enable();

	return 1;
}

int kvm_inject_nested_sync(struct kvm_vcpu *vcpu, u64 esr_el2)
{
	return kvm_inject_nested(vcpu, esr_el2, except_type_sync);
}

int kvm_inject_nested_irq(struct kvm_vcpu *vcpu)
{
	/*
	 * Do not inject an irq if the:
	 *  - Current exception level is EL2, and
	 *  - virtual HCR_EL2.TGE == 0
	 *  - virtual HCR_EL2.IMO == 0
	 *
	 * See Table D1-17 "Physical interrupt target and masking when EL3 is
	 * not implemented and EL2 is implemented" in ARM DDI 0487C.a.
	 */

	if (vcpu_is_el2(vcpu) && !vcpu_el2_tge_is_set(vcpu) &&
	    !(__vcpu_sys_reg(vcpu, HCR_EL2) & HCR_IMO))
		return 1;

	/* esr_el2 value doesn't matter for exits due to irqs. */
	return kvm_inject_nested(vcpu, 0, except_type_irq);
}
