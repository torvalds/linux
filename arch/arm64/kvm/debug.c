/*
 * Debug and Guest Debug support
 *
 * Copyright (C) 2015 - Linaro Ltd
 * Author: Alex Bennée <alex.bennee@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
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

static DEFINE_PER_CPU(u32, mdcr_el2);

/**
 * save/restore_guest_debug_regs
 *
 * For some debug operations we need to tweak some guest registers. As
 * a result we need to save the state of those registers before we
 * make those modifications.
 *
 * Guest access to MDSCR_EL1 is trapped by the hypervisor and handled
 * after we have restored the preserved value to the main context.
 */
static void save_guest_debug_regs(struct kvm_vcpu *vcpu)
{
	vcpu->arch.guest_debug_preserved.mdscr_el1 = vcpu_sys_reg(vcpu, MDSCR_EL1);

	trace_kvm_arm_set_dreg32("Saved MDSCR_EL1",
				vcpu->arch.guest_debug_preserved.mdscr_el1);
}

static void restore_guest_debug_regs(struct kvm_vcpu *vcpu)
{
	vcpu_sys_reg(vcpu, MDSCR_EL1) = vcpu->arch.guest_debug_preserved.mdscr_el1;

	trace_kvm_arm_set_dreg32("Restored MDSCR_EL1",
				vcpu_sys_reg(vcpu, MDSCR_EL1));
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
	__this_cpu_write(mdcr_el2, kvm_call_hyp(__kvm_get_mdcr_el2));
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
 * debug related registers. Currently this just ensures we will trap
 * access to:
 *  - Performance monitors (MDCR_EL2_TPM/MDCR_EL2_TPMCR)
 *  - Debug ROM Address (MDCR_EL2_TDRA)
 *  - OS related registers (MDCR_EL2_TDOSA)
 *
 * Additionally, KVM only traps guest accesses to the debug registers if
 * the guest is not actively using them (see the KVM_ARM64_DEBUG_DIRTY
 * flag on vcpu->arch.debug_flags).  Since the guest must not interfere
 * with the hardware state when debugging the guest, we must ensure that
 * trapping is enabled whenever we are debugging the guest using the
 * debug registers.
 */

void kvm_arm_setup_debug(struct kvm_vcpu *vcpu)
{
	bool trap_debug = !(vcpu->arch.debug_flags & KVM_ARM64_DEBUG_DIRTY);

	trace_kvm_arm_setup_debug(vcpu, vcpu->guest_debug);

	vcpu->arch.mdcr_el2 = __this_cpu_read(mdcr_el2) & MDCR_EL2_HPMN_MASK;
	vcpu->arch.mdcr_el2 |= (MDCR_EL2_TPM |
				MDCR_EL2_TPMCR |
				MDCR_EL2_TDRA |
				MDCR_EL2_TDOSA);

	/* Is Guest debugging in effect? */
	if (vcpu->guest_debug) {
		/* Route all software debug exceptions to EL2 */
		vcpu->arch.mdcr_el2 |= MDCR_EL2_TDE;

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
			*vcpu_cpsr(vcpu) |=  DBG_SPSR_SS;
			vcpu_sys_reg(vcpu, MDSCR_EL1) |= DBG_MDSCR_SS;
		} else {
			vcpu_sys_reg(vcpu, MDSCR_EL1) &= ~DBG_MDSCR_SS;
		}

		trace_kvm_arm_set_dreg32("SPSR_EL2", *vcpu_cpsr(vcpu));

		/*
		 * HW Breakpoints and watchpoints
		 *
		 * We simply switch the debug_ptr to point to our new
		 * external_debug_state which has been populated by the
		 * debug ioctl. The existing KVM_ARM64_DEBUG_DIRTY
		 * mechanism ensures the registers are updated on the
		 * world switch.
		 */
		if (vcpu->guest_debug & KVM_GUESTDBG_USE_HW) {
			/* Enable breakpoints/watchpoints */
			vcpu_sys_reg(vcpu, MDSCR_EL1) |= DBG_MDSCR_MDE;

			vcpu->arch.debug_ptr = &vcpu->arch.external_debug_state;
			vcpu->arch.debug_flags |= KVM_ARM64_DEBUG_DIRTY;
			trap_debug = true;

			trace_kvm_arm_set_regset("BKPTS", get_num_brps(),
						&vcpu->arch.debug_ptr->dbg_bcr[0],
						&vcpu->arch.debug_ptr->dbg_bvr[0]);

			trace_kvm_arm_set_regset("WAPTS", get_num_wrps(),
						&vcpu->arch.debug_ptr->dbg_wcr[0],
						&vcpu->arch.debug_ptr->dbg_wvr[0]);
		}
	}

	BUG_ON(!vcpu->guest_debug &&
		vcpu->arch.debug_ptr != &vcpu->arch.vcpu_debug_state);

	/* Trap debug register access */
	if (trap_debug)
		vcpu->arch.mdcr_el2 |= MDCR_EL2_TDA;

	trace_kvm_arm_set_dreg32("MDCR_EL2", vcpu->arch.mdcr_el2);
	trace_kvm_arm_set_dreg32("MDSCR_EL1", vcpu_sys_reg(vcpu, MDSCR_EL1));
}

void kvm_arm_clear_debug(struct kvm_vcpu *vcpu)
{
	trace_kvm_arm_clear_debug(vcpu->guest_debug);

	if (vcpu->guest_debug) {
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
