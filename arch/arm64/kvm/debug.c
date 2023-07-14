// SPDX-License-Identifier: GPL-2.0-only
/*
 * Debug and Guest Debug support
 *
 * Copyright (C) 2015 - Linaro Ltd
 * Author: Alex Bennée <alex.bennee@linaro.org>
 */

#include <linux/kvm_host.h>
#include <linux/hw_breakpoint.h>

#include <asm/debug-monitors.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_emulate.h>

#include "trace.h"

/* These are the bits of MDSCR_EL1 we may manipulate */
#define MDSCR_EL1_DEBUG_MASK	(DBG_MDSCR_SS | \
				DBG_MDSCR_KDE | \
				DBG_MDSCR_MDE)

static DEFINE_PER_CPU(u64, mdcr_el2);

/**
 * save/restore_guest_debug_regs
 *
 * For some debug operations we need to tweak some guest registers. As
 * a result we need to save the state of those registers before we
 * make those modifications.
 *
 * Guest access to MDSCR_EL1 is trapped by the hypervisor and handled
 * after we have restored the preserved value to the main context.
 *
 * When single-step is enabled by userspace, we tweak PSTATE.SS on every
 * guest entry. Preserve PSTATE.SS so we can restore the original value
 * for the vcpu after the single-step is disabled.
 */
static void save_guest_debug_regs(struct kvm_vcpu *vcpu)
{
	u64 val = vcpu_read_sys_reg(vcpu, MDSCR_EL1);

	vcpu->arch.guest_debug_preserved.mdscr_el1 = val;

	trace_kvm_arm_set_dreg32("Saved MDSCR_EL1",
				vcpu->arch.guest_debug_preserved.mdscr_el1);

	vcpu->arch.guest_debug_preserved.pstate_ss =
					(*vcpu_cpsr(vcpu) & DBG_SPSR_SS);
}

static void restore_guest_debug_regs(struct kvm_vcpu *vcpu)
{
	u64 val = vcpu->arch.guest_debug_preserved.mdscr_el1;

	vcpu_write_sys_reg(vcpu, val, MDSCR_EL1);

	trace_kvm_arm_set_dreg32("Restored MDSCR_EL1",
				vcpu_read_sys_reg(vcpu, MDSCR_EL1));

	if (vcpu->arch.guest_debug_preserved.pstate_ss)
		*vcpu_cpsr(vcpu) |= DBG_SPSR_SS;
	else
		*vcpu_cpsr(vcpu) &= ~DBG_SPSR_SS;
}

/**
 * kvm_arm_init_debug - grab what we need for debug
 *
 * Currently the sole task of this function is to retrieve the initial
 * value of mdcr_el2 so we can preserve MDCR_EL2.HPMN which has
 * presumably been set-up by some knowledgeable bootcode.
 *
 * It is called once per-cpu during CPU hyp initialisation.
 */

void kvm_arm_init_debug(void)
{
	__this_cpu_write(mdcr_el2, kvm_call_hyp_ret(__kvm_get_mdcr_el2));
}

/**
 * kvm_arm_setup_mdcr_el2 - configure vcpu mdcr_el2 value
 *
 * @vcpu:	the vcpu pointer
 *
 * This ensures we will trap access to:
 *  - Performance monitors (MDCR_EL2_TPM/MDCR_EL2_TPMCR)
 *  - Debug ROM Address (MDCR_EL2_TDRA)
 *  - OS related registers (MDCR_EL2_TDOSA)
 *  - Statistical profiler (MDCR_EL2_TPMS/MDCR_EL2_E2PB)
 *  - Self-hosted Trace Filter controls (MDCR_EL2_TTRF)
 *  - Self-hosted Trace (MDCR_EL2_TTRF/MDCR_EL2_E2TB)
 */
static void kvm_arm_setup_mdcr_el2(struct kvm_vcpu *vcpu)
{
	/*
	 * This also clears MDCR_EL2_E2PB_MASK and MDCR_EL2_E2TB_MASK
	 * to disable guest access to the profiling and trace buffers
	 */
	vcpu->arch.mdcr_el2 = __this_cpu_read(mdcr_el2) & MDCR_EL2_HPMN_MASK;
	vcpu->arch.mdcr_el2 |= (MDCR_EL2_TPM |
				MDCR_EL2_TPMS |
				MDCR_EL2_TTRF |
				MDCR_EL2_TPMCR |
				MDCR_EL2_TDRA |
				MDCR_EL2_TDOSA);

	/* Is the VM being debugged by userspace? */
	if (vcpu->guest_debug)
		/* Route all software debug exceptions to EL2 */
		vcpu->arch.mdcr_el2 |= MDCR_EL2_TDE;

	/*
	 * Trap debug register access when one of the following is true:
	 *  - Userspace is using the hardware to debug the guest
	 *  (KVM_GUESTDBG_USE_HW is set).
	 *  - The guest is not using debug (DEBUG_DIRTY clear).
	 *  - The guest has enabled the OS Lock (debug exceptions are blocked).
	 */
	if ((vcpu->guest_debug & KVM_GUESTDBG_USE_HW) ||
	    !vcpu_get_flag(vcpu, DEBUG_DIRTY) ||
	    kvm_vcpu_os_lock_enabled(vcpu))
		vcpu->arch.mdcr_el2 |= MDCR_EL2_TDA;

	trace_kvm_arm_set_dreg32("MDCR_EL2", vcpu->arch.mdcr_el2);
}

/**
 * kvm_arm_vcpu_init_debug - setup vcpu debug traps
 *
 * @vcpu:	the vcpu pointer
 *
 * Set vcpu initial mdcr_el2 value.
 */
void kvm_arm_vcpu_init_debug(struct kvm_vcpu *vcpu)
{
	preempt_disable();
	kvm_arm_setup_mdcr_el2(vcpu);
	preempt_enable();
}

/**
 * kvm_arm_reset_debug_ptr - reset the debug ptr to point to the vcpu state
 */

void kvm_arm_reset_debug_ptr(struct kvm_vcpu *vcpu)
{
	vcpu->arch.debug_ptr = &vcpu->arch.vcpu_debug_state;
}

/**
 * kvm_arm_setup_debug - set up debug related stuff
 *
 * @vcpu:	the vcpu pointer
 *
 * This is called before each entry into the hypervisor to setup any
 * debug related registers.
 *
 * Additionally, KVM only traps guest accesses to the debug registers if
 * the guest is not actively using them (see the DEBUG_DIRTY
 * flag on vcpu->arch.iflags).  Since the guest must not interfere
 * with the hardware state when debugging the guest, we must ensure that
 * trapping is enabled whenever we are debugging the guest using the
 * debug registers.
 */

void kvm_arm_setup_debug(struct kvm_vcpu *vcpu)
{
	unsigned long mdscr, orig_mdcr_el2 = vcpu->arch.mdcr_el2;

	trace_kvm_arm_setup_debug(vcpu, vcpu->guest_debug);

	kvm_arm_setup_mdcr_el2(vcpu);

	/* Check if we need to use the debug registers. */
	if (vcpu->guest_debug || kvm_vcpu_os_lock_enabled(vcpu)) {
		/* Save guest debug state */
		save_guest_debug_regs(vcpu);

		/*
		 * Single Step (ARM ARM D2.12.3 The software step state
		 * machine)
		 *
		 * If we are doing Single Step we need to manipulate
		 * the guest's MDSCR_EL1.SS and PSTATE.SS. Once the
		 * step has occurred the hypervisor will trap the
		 * debug exception and we return to userspace.
		 *
		 * If the guest attempts to single step its userspace
		 * we would have to deal with a trapped exception
		 * while in the guest kernel. Because this would be
		 * hard to unwind we suppress the guest's ability to
		 * do so by masking MDSCR_EL.SS.
		 *
		 * This confuses guest debuggers which use
		 * single-step behind the scenes but everything
		 * returns to normal once the host is no longer
		 * debugging the system.
		 */
		if (vcpu->guest_debug & KVM_GUESTDBG_SINGLESTEP) {
			/*
			 * If the software step state at the last guest exit
			 * was Active-pending, we don't set DBG_SPSR_SS so
			 * that the state is maintained (to not run another
			 * single-step until the pending Software Step
			 * exception is taken).
			 */
			if (!vcpu_get_flag(vcpu, DBG_SS_ACTIVE_PENDING))
				*vcpu_cpsr(vcpu) |= DBG_SPSR_SS;
			else
				*vcpu_cpsr(vcpu) &= ~DBG_SPSR_SS;

			mdscr = vcpu_read_sys_reg(vcpu, MDSCR_EL1);
			mdscr |= DBG_MDSCR_SS;
			vcpu_write_sys_reg(vcpu, mdscr, MDSCR_EL1);
		} else {
			mdscr = vcpu_read_sys_reg(vcpu, MDSCR_EL1);
			mdscr &= ~DBG_MDSCR_SS;
			vcpu_write_sys_reg(vcpu, mdscr, MDSCR_EL1);
		}

		trace_kvm_arm_set_dreg32("SPSR_EL2", *vcpu_cpsr(vcpu));

		/*
		 * HW Breakpoints and watchpoints
		 *
		 * We simply switch the debug_ptr to point to our new
		 * external_debug_state which has been populated by the
		 * debug ioctl. The existing DEBUG_DIRTY mechanism ensures
		 * the registers are updated on the world switch.
		 */
		if (vcpu->guest_debug & KVM_GUESTDBG_USE_HW) {
			/* Enable breakpoints/watchpoints */
			mdscr = vcpu_read_sys_reg(vcpu, MDSCR_EL1);
			mdscr |= DBG_MDSCR_MDE;
			vcpu_write_sys_reg(vcpu, mdscr, MDSCR_EL1);

			vcpu->arch.debug_ptr = &vcpu->arch.external_debug_state;
			vcpu_set_flag(vcpu, DEBUG_DIRTY);

			trace_kvm_arm_set_regset("BKPTS", get_num_brps(),
						&vcpu->arch.debug_ptr->dbg_bcr[0],
						&vcpu->arch.debug_ptr->dbg_bvr[0]);

			trace_kvm_arm_set_regset("WAPTS", get_num_wrps(),
						&vcpu->arch.debug_ptr->dbg_wcr[0],
						&vcpu->arch.debug_ptr->dbg_wvr[0]);

		/*
		 * The OS Lock blocks debug exceptions in all ELs when it is
		 * enabled. If the guest has enabled the OS Lock, constrain its
		 * effects to the guest. Emulate the behavior by clearing
		 * MDSCR_EL1.MDE. In so doing, we ensure that host debug
		 * exceptions are unaffected by guest configuration of the OS
		 * Lock.
		 */
		} else if (kvm_vcpu_os_lock_enabled(vcpu)) {
			mdscr = vcpu_read_sys_reg(vcpu, MDSCR_EL1);
			mdscr &= ~DBG_MDSCR_MDE;
			vcpu_write_sys_reg(vcpu, mdscr, MDSCR_EL1);
		}
	}

	BUG_ON(!vcpu->guest_debug &&
		vcpu->arch.debug_ptr != &vcpu->arch.vcpu_debug_state);

	/* If KDE or MDE are set, perform a full save/restore cycle. */
	if (vcpu_read_sys_reg(vcpu, MDSCR_EL1) & (DBG_MDSCR_KDE | DBG_MDSCR_MDE))
		vcpu_set_flag(vcpu, DEBUG_DIRTY);

	/* Write mdcr_el2 changes since vcpu_load on VHE systems */
	if (has_vhe() && orig_mdcr_el2 != vcpu->arch.mdcr_el2)
		write_sysreg(vcpu->arch.mdcr_el2, mdcr_el2);

	trace_kvm_arm_set_dreg32("MDSCR_EL1", vcpu_read_sys_reg(vcpu, MDSCR_EL1));
}

void kvm_arm_clear_debug(struct kvm_vcpu *vcpu)
{
	trace_kvm_arm_clear_debug(vcpu->guest_debug);

	/*
	 * Restore the guest's debug registers if we were using them.
	 */
	if (vcpu->guest_debug || kvm_vcpu_os_lock_enabled(vcpu)) {
		if (vcpu->guest_debug & KVM_GUESTDBG_SINGLESTEP) {
			if (!(*vcpu_cpsr(vcpu) & DBG_SPSR_SS))
				/*
				 * Mark the vcpu as ACTIVE_PENDING
				 * until Software Step exception is taken.
				 */
				vcpu_set_flag(vcpu, DBG_SS_ACTIVE_PENDING);
		}

		restore_guest_debug_regs(vcpu);

		/*
		 * If we were using HW debug we need to restore the
		 * debug_ptr to the guest debug state.
		 */
		if (vcpu->guest_debug & KVM_GUESTDBG_USE_HW) {
			kvm_arm_reset_debug_ptr(vcpu);

			trace_kvm_arm_set_regset("BKPTS", get_num_brps(),
						&vcpu->arch.debug_ptr->dbg_bcr[0],
						&vcpu->arch.debug_ptr->dbg_bvr[0]);

			trace_kvm_arm_set_regset("WAPTS", get_num_wrps(),
						&vcpu->arch.debug_ptr->dbg_wcr[0],
						&vcpu->arch.debug_ptr->dbg_wvr[0]);
		}
	}
}

void kvm_arch_vcpu_load_debug_state_flags(struct kvm_vcpu *vcpu)
{
	u64 dfr0;

	/* For VHE, there is nothing to do */
	if (has_vhe())
		return;

	dfr0 = read_sysreg(id_aa64dfr0_el1);
	/*
	 * If SPE is present on this CPU and is available at current EL,
	 * we may need to check if the host state needs to be saved.
	 */
	if (cpuid_feature_extract_unsigned_field(dfr0, ID_AA64DFR0_EL1_PMSVer_SHIFT) &&
	    !(read_sysreg_s(SYS_PMBIDR_EL1) & BIT(PMBIDR_EL1_P_SHIFT)))
		vcpu_set_flag(vcpu, DEBUG_STATE_SAVE_SPE);

	/* Check if we have TRBE implemented and available at the host */
	if (cpuid_feature_extract_unsigned_field(dfr0, ID_AA64DFR0_EL1_TraceBuffer_SHIFT) &&
	    !(read_sysreg_s(SYS_TRBIDR_EL1) & TRBIDR_EL1_P))
		vcpu_set_flag(vcpu, DEBUG_STATE_SAVE_TRBE);
}

void kvm_arch_vcpu_put_debug_state_flags(struct kvm_vcpu *vcpu)
{
	vcpu_clear_flag(vcpu, DEBUG_STATE_SAVE_SPE);
	vcpu_clear_flag(vcpu, DEBUG_STATE_SAVE_TRBE);
}
