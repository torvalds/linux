/*
 * Fault injection for 64bit guests.
 *
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Based on arch/arm/kvm/emulate.c
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 *
 * This program is free software: you can redistribute it and/or modify
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
#include <asm/kvm_emulate.h>
#include <asm/esr.h>

#define PSTATE_FAULT_BITS_64 	(PSR_MODE_EL1h | PSR_A_BIT | PSR_F_BIT | \
				 PSR_I_BIT | PSR_D_BIT)
#define EL1_EXCEPT_SYNC_OFFSET	0x200

static void inject_abt64(struct kvm_vcpu *vcpu, bool is_iabt, unsigned long addr)
{
	unsigned long cpsr = *vcpu_cpsr(vcpu);
	bool is_aarch32;
	u32 esr = 0;

	is_aarch32 = vcpu_mode_is_32bit(vcpu);

	*vcpu_spsr(vcpu) = cpsr;
	*vcpu_elr_el1(vcpu) = *vcpu_pc(vcpu);

	*vcpu_cpsr(vcpu) = PSTATE_FAULT_BITS_64;
	*vcpu_pc(vcpu) = vcpu_sys_reg(vcpu, VBAR_EL1) + EL1_EXCEPT_SYNC_OFFSET;

	vcpu_sys_reg(vcpu, FAR_EL1) = addr;

	/*
	 * Build an {i,d}abort, depending on the level and the
	 * instruction set. Report an external synchronous abort.
	 */
	if (kvm_vcpu_trap_il_is32bit(vcpu))
		esr |= ESR_EL1_IL;

	/*
	 * Here, the guest runs in AArch64 mode when in EL1. If we get
	 * an AArch32 fault, it means we managed to trap an EL0 fault.
	 */
	if (is_aarch32 || (cpsr & PSR_MODE_MASK) == PSR_MODE_EL0t)
		esr |= (ESR_EL1_EC_IABT_EL0 << ESR_EL1_EC_SHIFT);
	else
		esr |= (ESR_EL1_EC_IABT_EL1 << ESR_EL1_EC_SHIFT);

	if (!is_iabt)
		esr |= ESR_EL1_EC_DABT_EL0;

	vcpu_sys_reg(vcpu, ESR_EL1) = esr | ESR_EL2_EC_xABT_xFSR_EXTABT;
}

static void inject_undef64(struct kvm_vcpu *vcpu)
{
	unsigned long cpsr = *vcpu_cpsr(vcpu);
	u32 esr = (ESR_EL1_EC_UNKNOWN << ESR_EL1_EC_SHIFT);

	*vcpu_spsr(vcpu) = cpsr;
	*vcpu_elr_el1(vcpu) = *vcpu_pc(vcpu);

	*vcpu_cpsr(vcpu) = PSTATE_FAULT_BITS_64;
	*vcpu_pc(vcpu) = vcpu_sys_reg(vcpu, VBAR_EL1) + EL1_EXCEPT_SYNC_OFFSET;

	/*
	 * Build an unknown exception, depending on the instruction
	 * set.
	 */
	if (kvm_vcpu_trap_il_is32bit(vcpu))
		esr |= ESR_EL1_IL;

	vcpu_sys_reg(vcpu, ESR_EL1) = esr;
}

/**
 * kvm_inject_dabt - inject a data abort into the guest
 * @vcpu: The VCPU to receive the undefined exception
 * @addr: The address to report in the DFAR
 *
 * It is assumed that this code is called from the VCPU thread and that the
 * VCPU therefore is not currently executing guest code.
 */
void kvm_inject_dabt(struct kvm_vcpu *vcpu, unsigned long addr)
{
	inject_abt64(vcpu, false, addr);
}

/**
 * kvm_inject_pabt - inject a prefetch abort into the guest
 * @vcpu: The VCPU to receive the undefined exception
 * @addr: The address to report in the DFAR
 *
 * It is assumed that this code is called from the VCPU thread and that the
 * VCPU therefore is not currently executing guest code.
 */
void kvm_inject_pabt(struct kvm_vcpu *vcpu, unsigned long addr)
{
	inject_abt64(vcpu, true, addr);
}

/**
 * kvm_inject_undefined - inject an undefined instruction into the guest
 *
 * It is assumed that this code is called from the VCPU thread and that the
 * VCPU therefore is not currently executing guest code.
 */
void kvm_inject_undefined(struct kvm_vcpu *vcpu)
{
	inject_undef64(vcpu);
}
