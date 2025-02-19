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
#include <asm/kvm_emulate.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_nested.h>
#include <asm/debug-monitors.h>
#include <asm/stacktrace/nvhe.h>
#include <asm/traps.h>

#include <kvm/arm_hypercalls.h>

#define CREATE_TRACE_POINTS
#include "trace_handle_exit.h"

typedef int (*exit_handle_fn)(struct kvm_vcpu *);

static void kvm_handle_guest_serror(struct kvm_vcpu *vcpu, u64 esr)
{
	if (!arm64_is_ras_serror(esr) || arm64_is_fatal_ras_serror(NULL, esr))
		kvm_inject_vabt(vcpu);
}

static int handle_hvc(struct kvm_vcpu *vcpu)
{
	trace_kvm_hvc_arm64(*vcpu_pc(vcpu), vcpu_get_reg(vcpu, 0),
			    kvm_vcpu_hvc_get_imm(vcpu));
	vcpu->stat.hvc_exit_stat++;

	/* Forward hvc instructions to the virtual EL2 if the guest has EL2. */
	if (vcpu_has_nv(vcpu)) {
		if (vcpu_read_sys_reg(vcpu, HCR_EL2) & HCR_HCD)
			kvm_inject_undefined(vcpu);
		else
			kvm_inject_nested_sync(vcpu, kvm_vcpu_get_esr(vcpu));

		return 1;
	}

	return kvm_smccc_call_handler(vcpu);
}

static int handle_smc(struct kvm_vcpu *vcpu)
{
	/*
	 * Forward this trapped smc instruction to the virtual EL2 if
	 * the guest has asked for it.
	 */
	if (forward_smc_trap(vcpu))
		return 1;

	/*
	 * "If an SMC instruction executed at Non-secure EL1 is
	 * trapped to EL2 because HCR_EL2.TSC is 1, the exception is a
	 * Trap exception, not a Secure Monitor Call exception [...]"
	 *
	 * We need to advance the PC after the trap, as it would
	 * otherwise return to the same address. Furthermore, pre-incrementing
	 * the PC before potentially exiting to userspace maintains the same
	 * abstraction for both SMCs and HVCs.
	 */
	kvm_incr_pc(vcpu);

	/*
	 * SMCs with a nonzero immediate are reserved according to DEN0028E 2.9
	 * "SMC and HVC immediate value".
	 */
	if (kvm_vcpu_hvc_get_imm(vcpu)) {
		vcpu_set_reg(vcpu, 0, ~0UL);
		return 1;
	}

	/*
	 * If imm is zero then it is likely an SMCCC call.
	 *
	 * Note that on ARMv8.3, even if EL3 is not implemented, SMC executed
	 * at Non-secure EL1 is trapped to EL2 if HCR_EL2.TSC==1, rather than
	 * being treated as UNDEFINED.
	 */
	return kvm_smccc_call_handler(vcpu);
}

/*
 * This handles the cases where the system does not support FP/ASIMD or when
 * we are running nested virtualization and the guest hypervisor is trapping
 * FP/ASIMD accesses by its guest guest.
 *
 * All other handling of guest vs. host FP/ASIMD register state is handled in
 * fixup_guest_exit().
 */
static int kvm_handle_fpasimd(struct kvm_vcpu *vcpu)
{
	if (guest_hyp_fpsimd_traps_enabled(vcpu))
		return kvm_inject_nested_sync(vcpu, kvm_vcpu_get_esr(vcpu));

	/* This is the case when the system doesn't support FP/ASIMD. */
	kvm_inject_undefined(vcpu);
	return 1;
}

/**
 * kvm_handle_wfx - handle a wait-for-interrupts or wait-for-event
 *		    instruction executed by a guest
 *
 * @vcpu:	the vcpu pointer
 *
 * WFE[T]: Yield the CPU and come back to this vcpu when the scheduler
 * decides to.
 * WFI: Simply call kvm_vcpu_halt(), which will halt execution of
 * world-switches and schedule other host processes until there is an
 * incoming IRQ or FIQ to the VM.
 * WFIT: Same as WFI, with a timed wakeup implemented as a background timer
 *
 * WF{I,E}T can immediately return if the deadline has already expired.
 */
static int kvm_handle_wfx(struct kvm_vcpu *vcpu)
{
	u64 esr = kvm_vcpu_get_esr(vcpu);

	if (esr & ESR_ELx_WFx_ISS_WFE) {
		trace_kvm_wfx_arm64(*vcpu_pc(vcpu), true);
		vcpu->stat.wfe_exit_stat++;
	} else {
		trace_kvm_wfx_arm64(*vcpu_pc(vcpu), false);
		vcpu->stat.wfi_exit_stat++;
	}

	if (esr & ESR_ELx_WFx_ISS_WFxT) {
		if (esr & ESR_ELx_WFx_ISS_RV) {
			u64 val, now;

			now = kvm_arm_timer_get_reg(vcpu, KVM_REG_ARM_TIMER_CNT);
			val = vcpu_get_reg(vcpu, kvm_vcpu_sys_get_rt(vcpu));

			if (now >= val)
				goto out;
		} else {
			/* Treat WFxT as WFx if RN is invalid */
			esr &= ~ESR_ELx_WFx_ISS_WFxT;
		}
	}

	if (esr & ESR_ELx_WFx_ISS_WFE) {
		kvm_vcpu_on_spin(vcpu, vcpu_mode_priv(vcpu));
	} else {
		if (esr & ESR_ELx_WFx_ISS_WFxT)
			vcpu_set_flag(vcpu, IN_WFIT);

		kvm_vcpu_wfi(vcpu);
	}
out:
	kvm_incr_pc(vcpu);

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
 * @return: 0 (while setting vcpu->run->exit_reason)
 */
static int kvm_handle_guest_debug(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	u64 esr = kvm_vcpu_get_esr(vcpu);

	if (!vcpu->guest_debug && forward_debug_exception(vcpu))
		return 1;

	run->exit_reason = KVM_EXIT_DEBUG;
	run->debug.arch.hsr = lower_32_bits(esr);
	run->debug.arch.hsr_high = upper_32_bits(esr);
	run->flags = KVM_DEBUG_ARCH_HSR_HIGH_VALID;

	switch (ESR_ELx_EC(esr)) {
	case ESR_ELx_EC_WATCHPT_LOW:
		run->debug.arch.far = vcpu->arch.fault.far_el2;
		break;
	case ESR_ELx_EC_SOFTSTP_LOW:
		*vcpu_cpsr(vcpu) |= DBG_SPSR_SS;
		break;
	}

	return 0;
}

static int kvm_handle_unknown_ec(struct kvm_vcpu *vcpu)
{
	u64 esr = kvm_vcpu_get_esr(vcpu);

	kvm_pr_unimpl("Unknown exception class: esr: %#016llx -- %s\n",
		      esr, esr_get_class_string(esr));

	kvm_inject_undefined(vcpu);
	return 1;
}

/*
 * Guest access to SVE registers should be routed to this handler only
 * when the system doesn't support SVE.
 */
static int handle_sve(struct kvm_vcpu *vcpu)
{
	if (guest_hyp_sve_traps_enabled(vcpu))
		return kvm_inject_nested_sync(vcpu, kvm_vcpu_get_esr(vcpu));

	kvm_inject_undefined(vcpu);
	return 1;
}

/*
 * Two possibilities to handle a trapping ptrauth instruction:
 *
 * - Guest usage of a ptrauth instruction (which the guest EL1 did not
 *   turn into a NOP). If we get here, it is because we didn't enable
 *   ptrauth for the guest. This results in an UNDEF, as it isn't
 *   supposed to use ptrauth without being told it could.
 *
 * - Running an L2 NV guest while L1 has left HCR_EL2.API==0, and for
 *   which we reinject the exception into L1.
 *
 * Anything else is an emulation bug (hence the WARN_ON + UNDEF).
 */
static int kvm_handle_ptrauth(struct kvm_vcpu *vcpu)
{
	if (!vcpu_has_ptrauth(vcpu)) {
		kvm_inject_undefined(vcpu);
		return 1;
	}

	if (vcpu_has_nv(vcpu) && !is_hyp_ctxt(vcpu)) {
		kvm_inject_nested_sync(vcpu, kvm_vcpu_get_esr(vcpu));
		return 1;
	}

	/* Really shouldn't be here! */
	WARN_ON_ONCE(1);
	kvm_inject_undefined(vcpu);
	return 1;
}

static int kvm_handle_eret(struct kvm_vcpu *vcpu)
{
	if (esr_iss_is_eretax(kvm_vcpu_get_esr(vcpu)) &&
	    !vcpu_has_ptrauth(vcpu))
		return kvm_handle_ptrauth(vcpu);

	/*
	 * If we got here, two possibilities:
	 *
	 * - the guest is in EL2, and we need to fully emulate ERET
	 *
	 * - the guest is in EL1, and we need to reinject the
         *   exception into the L1 hypervisor.
	 *
	 * If KVM ever traps ERET for its own use, we'll have to
	 * revisit this.
	 */
	if (is_hyp_ctxt(vcpu))
		kvm_emulate_nested_eret(vcpu);
	else
		kvm_inject_nested_sync(vcpu, kvm_vcpu_get_esr(vcpu));

	return 1;
}

static int handle_svc(struct kvm_vcpu *vcpu)
{
	/*
	 * So far, SVC traps only for NV via HFGITR_EL2. A SVC from a
	 * 32bit guest would be caught by vpcu_mode_is_bad_32bit(), so
	 * we should only have to deal with a 64 bit exception.
	 */
	kvm_inject_nested_sync(vcpu, kvm_vcpu_get_esr(vcpu));
	return 1;
}

static exit_handle_fn arm_exit_handlers[] = {
	[0 ... ESR_ELx_EC_MAX]	= kvm_handle_unknown_ec,
	[ESR_ELx_EC_WFx]	= kvm_handle_wfx,
	[ESR_ELx_EC_CP15_32]	= kvm_handle_cp15_32,
	[ESR_ELx_EC_CP15_64]	= kvm_handle_cp15_64,
	[ESR_ELx_EC_CP14_MR]	= kvm_handle_cp14_32,
	[ESR_ELx_EC_CP14_LS]	= kvm_handle_cp14_load_store,
	[ESR_ELx_EC_CP10_ID]	= kvm_handle_cp10_id,
	[ESR_ELx_EC_CP14_64]	= kvm_handle_cp14_64,
	[ESR_ELx_EC_HVC32]	= handle_hvc,
	[ESR_ELx_EC_SMC32]	= handle_smc,
	[ESR_ELx_EC_HVC64]	= handle_hvc,
	[ESR_ELx_EC_SMC64]	= handle_smc,
	[ESR_ELx_EC_SVC64]	= handle_svc,
	[ESR_ELx_EC_SYS64]	= kvm_handle_sys_reg,
	[ESR_ELx_EC_SVE]	= handle_sve,
	[ESR_ELx_EC_ERET]	= kvm_handle_eret,
	[ESR_ELx_EC_IABT_LOW]	= kvm_handle_guest_abort,
	[ESR_ELx_EC_DABT_LOW]	= kvm_handle_guest_abort,
	[ESR_ELx_EC_SOFTSTP_LOW]= kvm_handle_guest_debug,
	[ESR_ELx_EC_WATCHPT_LOW]= kvm_handle_guest_debug,
	[ESR_ELx_EC_BREAKPT_LOW]= kvm_handle_guest_debug,
	[ESR_ELx_EC_BKPT32]	= kvm_handle_guest_debug,
	[ESR_ELx_EC_BRK64]	= kvm_handle_guest_debug,
	[ESR_ELx_EC_FP_ASIMD]	= kvm_handle_fpasimd,
	[ESR_ELx_EC_PAC]	= kvm_handle_ptrauth,
};

static exit_handle_fn kvm_get_exit_handler(struct kvm_vcpu *vcpu)
{
	u64 esr = kvm_vcpu_get_esr(vcpu);
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
		kvm_incr_pc(vcpu);
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
		/*
		 * The SError is handled by handle_exit_early(). If the guest
		 * survives it will re-execute the original instruction.
		 */
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
		 * is pre-emptied by kvm_reboot()'s shutdown call.
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

static void print_nvhe_hyp_panic(const char *name, u64 panic_addr)
{
	kvm_err("nVHE hyp %s at: [<%016llx>] %pB!\n", name, panic_addr,
		(void *)(panic_addr + kaslr_offset()));
}

static void kvm_nvhe_report_cfi_failure(u64 panic_addr)
{
	print_nvhe_hyp_panic("CFI failure", panic_addr);

	if (IS_ENABLED(CONFIG_CFI_PERMISSIVE))
		kvm_err(" (CONFIG_CFI_PERMISSIVE ignored for hyp failures)\n");
}

void __noreturn __cold nvhe_hyp_panic_handler(u64 esr, u64 spsr,
					      u64 elr_virt, u64 elr_phys,
					      u64 par, uintptr_t vcpu,
					      u64 far, u64 hpfar) {
	u64 elr_in_kimg = __phys_to_kimg(elr_phys);
	u64 hyp_offset = elr_in_kimg - kaslr_offset() - elr_virt;
	u64 mode = spsr & PSR_MODE_MASK;
	u64 panic_addr = elr_virt + hyp_offset;

	if (mode != PSR_MODE_EL2t && mode != PSR_MODE_EL2h) {
		kvm_err("Invalid host exception to nVHE hyp!\n");
	} else if (ESR_ELx_EC(esr) == ESR_ELx_EC_BRK64 &&
		   esr_brk_comment(esr) == BUG_BRK_IMM) {
		const char *file = NULL;
		unsigned int line = 0;

		/* All hyp bugs, including warnings, are treated as fatal. */
		if (!is_protected_kvm_enabled() ||
		    IS_ENABLED(CONFIG_NVHE_EL2_DEBUG)) {
			struct bug_entry *bug = find_bug(elr_in_kimg);

			if (bug)
				bug_get_file_line(bug, &file, &line);
		}

		if (file)
			kvm_err("nVHE hyp BUG at: %s:%u!\n", file, line);
		else
			print_nvhe_hyp_panic("BUG", panic_addr);
	} else if (IS_ENABLED(CONFIG_CFI_CLANG) && esr_is_cfi_brk(esr)) {
		kvm_nvhe_report_cfi_failure(panic_addr);
	} else {
		print_nvhe_hyp_panic("panic", panic_addr);
	}

	/* Dump the nVHE hypervisor backtrace */
	kvm_nvhe_dump_backtrace(hyp_offset);

	/*
	 * Hyp has panicked and we're going to handle that by panicking the
	 * kernel. The kernel offset will be revealed in the panic so we're
	 * also safe to reveal the hyp offset as a debugging aid for translating
	 * hyp VAs to vmlinux addresses.
	 */
	kvm_err("Hyp Offset: 0x%llx\n", hyp_offset);

	panic("HYP panic:\nPS:%08llx PC:%016llx ESR:%016llx\nFAR:%016llx HPFAR:%016llx PAR:%016llx\nVCPU:%016lx\n",
	      spsr, elr_virt, esr, far, hpfar, par, vcpu);
}
