// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Derived from arch/arm/kvm/handle_exit.c:
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 */

#include <linux/kvm.h>
#include <linux/kvm_host.h>

#include <asm/esr.h>
#include <asm/exception.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_coproc.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_mmu.h>
#include <asm/debug-monitors.h>
#include <asm/traps.h>

#include <kvm/arm_hypercalls.h>

#define CREATE_TRACE_POINTS
#include "trace_handle_exit.h"

typedef int (*exit_handle_fn)(struct kvm_vcpu *);

static void kvm_handle_guest_serror(struct kvm_vcpu *vcpu, u32 esr)
{
	if (!arm64_is_ras_serror(esr) || arm64_is_fatal_ras_serror(NULL, esr))
		kvm_inject_vabt(vcpu);
}

static int handle_hvc(struct kvm_vcpu *vcpu)
{
	int ret;

	trace_kvm_hvc_arm64(*vcpu_pc(vcpu), vcpu_get_reg(vcpu, 0),
			    kvm_vcpu_hvc_get_imm(vcpu));
	vcpu->stat.hvc_exit_stat++;

	ret = kvm_hvc_call_handler(vcpu);
	if (ret < 0) {
		vcpu_set_reg(vcpu, 0, ~0UL);
		return 1;
	}

	return ret;
}

static int handle_smc(struct kvm_vcpu *vcpu)
{
	/*
	 * "If an SMC instruction executed at Non-secure EL1 is
	 * trapped to EL2 because HCR_EL2.TSC is 1, the exception is a
	 * Trap exception, not a Secure Monitor Call exception [...]"
	 *
	 * We need to advance the PC after the trap, as it would
	 * otherwise return to the same address...
	 */
	vcpu_set_reg(vcpu, 0, ~0UL);
	kvm_skip_instr(vcpu, kvm_vcpu_trap_il_is32bit(vcpu));
	return 1;
}

/*
 * Guest access to FP/ASIMD registers are routed to this handler only
 * when the system doesn't support FP/ASIMD.
 */
static int handle_no_fpsimd(struct kvm_vcpu *vcpu)
{
	kvm_inject_undefined(vcpu);
	return 1;
}

/**
 * kvm_handle_wfx - handle a wait-for-interrupts or wait-for-event
 *		    instruction executed by a guest
 *
 * @vcpu:	the vcpu pointer
 *
 * WFE: Yield the CPU and come back to this vcpu when the scheduler
 * decides to.
 * WFI: Simply call kvm_vcpu_block(), which will halt execution of
 * world-switches and schedule other host processes until there is an
 * incoming IRQ or FIQ to the VM.
 */
static int kvm_handle_wfx(struct kvm_vcpu *vcpu)
{
	if (kvm_vcpu_get_esr(vcpu) & ESR_ELx_WFx_ISS_WFE) {
		trace_kvm_wfx_arm64(*vcpu_pc(vcpu), true);
		vcpu->stat.wfe_exit_stat++;
		kvm_vcpu_on_spin(vcpu, vcpu_mode_priv(vcpu));
	} else {
		trace_kvm_wfx_arm64(*vcpu_pc(vcpu), false);
		vcpu->stat.wfi_exit_stat++;
		kvm_vcpu_block(vcpu);
		kvm_clear_request(KVM_REQ_UNHALT, vcpu);
	}

	kvm_skip_instr(vcpu, kvm_vcpu_trap_il_is32bit(vcpu));

	return 1;
}

/**
 * kvm_handle_guest_debug - handle a debug exception instruction
 *
 * @vcpu:	the vcpu pointer
 *
 * We route all debug exceptions through the same handler. If both the
 * guest and host are using the same debug facilities it will be up to
 * userspace to re-inject the correct exception for guest delivery.
 *
 * @return: 0 (while setting vcpu->run->exit_reason), -1 for error
 */
static int kvm_handle_guest_debug(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	u32 esr = kvm_vcpu_get_esr(vcpu);
	int ret = 0;

	run->exit_reason = KVM_EXIT_DEBUG;
	run->debug.arch.hsr = esr;

	switch (ESR_ELx_EC(esr)) {
	case ESR_ELx_EC_WATCHPT_LOW:
		run->debug.arch.far = vcpu->arch.fault.far_el2;
		fallthrough;
	case ESR_ELx_EC_SOFTSTP_LOW:
	case ESR_ELx_EC_BREAKPT_LOW:
	case ESR_ELx_EC_BKPT32:
	case ESR_ELx_EC_BRK64:
		break;
	default:
		kvm_err("%s: un-handled case esr: %#08x\n",
			__func__, (unsigned int) esr);
		ret = -1;
		break;
	}

	return ret;
}

static int kvm_handle_unknown_ec(struct kvm_vcpu *vcpu)
{
	u32 esr = kvm_vcpu_get_esr(vcpu);

	kvm_pr_unimpl("Unknown exception class: esr: %#08x -- %s\n",
		      esr, esr_get_class_string(esr));

	kvm_inject_undefined(vcpu);
	return 1;
}

static int handle_sve(struct kvm_vcpu *vcpu)
{
	/* Until SVE is supported for guests: */
	kvm_inject_undefined(vcpu);
	return 1;
}

/*
 * Guest usage of a ptrauth instruction (which the guest EL1 did not turn into
 * a NOP). If we get here, it is that we didn't fixup ptrauth on exit, and all
 * that we can do is give the guest an UNDEF.
 */
static int kvm_handle_ptrauth(struct kvm_vcpu *vcpu)
{
	kvm_inject_undefined(vcpu);
	return 1;
}

static exit_handle_fn arm_exit_handlers[] = {
	[0 ... ESR_ELx_EC_MAX]	= kvm_handle_unknown_ec,
	[ESR_ELx_EC_WFx]	= kvm_handle_wfx,
	[ESR_ELx_EC_CP15_32]	= kvm_handle_cp15_32,
	[ESR_ELx_EC_CP15_64]	= kvm_handle_cp15_64,
	[ESR_ELx_EC_CP14_MR]	= kvm_handle_cp14_32,
	[ESR_ELx_EC_CP14_LS]	= kvm_handle_cp14_load_store,
	[ESR_ELx_EC_CP14_64]	= kvm_handle_cp14_64,
	[ESR_ELx_EC_HVC32]	= handle_hvc,
	[ESR_ELx_EC_SMC32]	= handle_smc,
	[ESR_ELx_EC_HVC64]	= handle_hvc,
	[ESR_ELx_EC_SMC64]	= handle_smc,
	[ESR_ELx_EC_SYS64]	= kvm_handle_sys_reg,
	[ESR_ELx_EC_SVE]	= handle_sve,
	[ESR_ELx_EC_IABT_LOW]	= kvm_handle_guest_abort,
	[ESR_ELx_EC_DABT_LOW]	= kvm_handle_guest_abort,
	[ESR_ELx_EC_SOFTSTP_LOW]= kvm_handle_guest_debug,
	[ESR_ELx_EC_WATCHPT_LOW]= kvm_handle_guest_debug,
	[ESR_ELx_EC_BREAKPT_LOW]= kvm_handle_guest_debug,
	[ESR_ELx_EC_BKPT32]	= kvm_handle_guest_debug,
	[ESR_ELx_EC_BRK64]	= kvm_handle_guest_debug,
	[ESR_ELx_EC_FP_ASIMD]	= handle_no_fpsimd,
	[ESR_ELx_EC_PAC]	= kvm_handle_ptrauth,
};

static exit_handle_fn kvm_get_exit_handler(struct kvm_vcpu *vcpu)
{
	u32 esr = kvm_vcpu_get_esr(vcpu);
	u8 esr_ec = ESR_ELx_EC(esr);

	return arm_exit_handlers[esr_ec];
}

/*
 * We may be single-stepping an emulated instruction. If the emulation
 * has been completed in the kernel, we can return to userspace with a
 * KVM_EXIT_DEBUG, otherwise userspace needs to complete its
 * emulation first.
 */
static int handle_trap_exceptions(struct kvm_vcpu *vcpu)
{
	int handled;

	/*
	 * See ARM ARM B1.14.1: "Hyp traps on instructions
	 * that fail their condition code check"
	 */
	if (!kvm_condition_valid(vcpu)) {
		kvm_skip_instr(vcpu, kvm_vcpu_trap_il_is32bit(vcpu));
		handled = 1;
	} else {
		exit_handle_fn exit_handler;

		exit_handler = kvm_get_exit_handler(vcpu);
		handled = exit_handler(vcpu);
	}

	return handled;
}

/*
 * Return > 0 to return to guest, < 0 on error, 0 (and set exit_reason) on
 * proper exit to userspace.
 */
int handle_exit(struct kvm_vcpu *vcpu, int exception_index)
{
	struct kvm_run *run = vcpu->run;

	if (ARM_SERROR_PENDING(exception_index)) {
		u8 esr_ec = ESR_ELx_EC(kvm_vcpu_get_esr(vcpu));

		/*
		 * HVC/SMC already have an adjusted PC, which we need
		 * to correct in order to return to after having
		 * injected the SError.
		 */
		if (esr_ec == ESR_ELx_EC_HVC32 || esr_ec == ESR_ELx_EC_HVC64 ||
		    esr_ec == ESR_ELx_EC_SMC32 || esr_ec == ESR_ELx_EC_SMC64) {
			u32 adj =  kvm_vcpu_trap_il_is32bit(vcpu) ? 4 : 2;
			*vcpu_pc(vcpu) -= adj;
		}

		return 1;
	}

	exception_index = ARM_EXCEPTION_CODE(exception_index);

	switch (exception_index) {
	case ARM_EXCEPTION_IRQ:
		return 1;
	case ARM_EXCEPTION_EL1_SERROR:
		return 1;
	case ARM_EXCEPTION_TRAP:
		return handle_trap_exceptions(vcpu);
	case ARM_EXCEPTION_HYP_GONE:
		/*
		 * EL2 has been reset to the hyp-stub. This happens when a guest
		 * is pre-empted by kvm_reboot()'s shutdown call.
		 */
		run->exit_reason = KVM_EXIT_FAIL_ENTRY;
		return 0;
	case ARM_EXCEPTION_IL:
		/*
		 * We attempted an illegal exception return.  Guest state must
		 * have been corrupted somehow.  Give up.
		 */
		run->exit_reason = KVM_EXIT_FAIL_ENTRY;
		return -EINVAL;
	default:
		kvm_pr_unimpl("Unsupported exception type: %d",
			      exception_index);
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		return 0;
	}
}

/* For exit types that need handling before we can be preempted */
void handle_exit_early(struct kvm_vcpu *vcpu, int exception_index)
{
	if (ARM_SERROR_PENDING(exception_index)) {
		if (this_cpu_has_cap(ARM64_HAS_RAS_EXTN)) {
			u64 disr = kvm_vcpu_get_disr(vcpu);

			kvm_handle_guest_serror(vcpu, disr_to_esr(disr));
		} else {
			kvm_inject_vabt(vcpu);
		}

		return;
	}

	exception_index = ARM_EXCEPTION_CODE(exception_index);

	if (exception_index == ARM_EXCEPTION_EL1_SERROR)
		kvm_handle_guest_serror(vcpu, kvm_vcpu_get_esr(vcpu));
}
