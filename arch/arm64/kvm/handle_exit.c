/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Derived from arch/arm/kvm/handle_exit.c:
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
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

#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_coproc.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_psci.h>

typedef int (*exit_handle_fn)(struct kvm_vcpu *, struct kvm_run *);

static int handle_hvc(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	if (kvm_psci_call(vcpu))
		return 1;

	kvm_inject_undefined(vcpu);
	return 1;
}

static int handle_smc(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	if (kvm_psci_call(vcpu))
		return 1;

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
static int kvm_handle_wfx(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	if (kvm_vcpu_get_hsr(vcpu) & ESR_EL2_EC_WFI_ISS_WFE)
		kvm_vcpu_on_spin(vcpu);
	else
		kvm_vcpu_block(vcpu);

	return 1;
}

static exit_handle_fn arm_exit_handlers[] = {
	[ESR_EL2_EC_WFI]	= kvm_handle_wfx,
	[ESR_EL2_EC_CP15_32]	= kvm_handle_cp15_32,
	[ESR_EL2_EC_CP15_64]	= kvm_handle_cp15_64,
	[ESR_EL2_EC_CP14_MR]	= kvm_handle_cp14_access,
	[ESR_EL2_EC_CP14_LS]	= kvm_handle_cp14_load_store,
	[ESR_EL2_EC_CP14_64]	= kvm_handle_cp14_access,
	[ESR_EL2_EC_HVC32]	= handle_hvc,
	[ESR_EL2_EC_SMC32]	= handle_smc,
	[ESR_EL2_EC_HVC64]	= handle_hvc,
	[ESR_EL2_EC_SMC64]	= handle_smc,
	[ESR_EL2_EC_SYS64]	= kvm_handle_sys_reg,
	[ESR_EL2_EC_IABT]	= kvm_handle_guest_abort,
	[ESR_EL2_EC_DABT]	= kvm_handle_guest_abort,
};

static exit_handle_fn kvm_get_exit_handler(struct kvm_vcpu *vcpu)
{
	u8 hsr_ec = kvm_vcpu_trap_get_class(vcpu);

	if (hsr_ec >= ARRAY_SIZE(arm_exit_handlers) ||
	    !arm_exit_handlers[hsr_ec]) {
		kvm_err("Unkown exception class: hsr: %#08x\n",
			(unsigned int)kvm_vcpu_get_hsr(vcpu));
		BUG();
	}

	return arm_exit_handlers[hsr_ec];
}

/*
 * Return > 0 to return to guest, < 0 on error, 0 (and set exit_reason) on
 * proper exit to userspace.
 */
int handle_exit(struct kvm_vcpu *vcpu, struct kvm_run *run,
		       int exception_index)
{
	exit_handle_fn exit_handler;

	switch (exception_index) {
	case ARM_EXCEPTION_IRQ:
		return 1;
	case ARM_EXCEPTION_TRAP:
		/*
		 * See ARM ARM B1.14.1: "Hyp traps on instructions
		 * that fail their condition code check"
		 */
		if (!kvm_condition_valid(vcpu)) {
			kvm_skip_instr(vcpu, kvm_vcpu_trap_il_is32bit(vcpu));
			return 1;
		}

		exit_handler = kvm_get_exit_handler(vcpu);

		return exit_handler(vcpu, run);
	default:
		kvm_pr_unimpl("Unsupported exception type: %d",
			      exception_index);
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		return 0;
	}
}
