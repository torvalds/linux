/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Derived from arch/arm/include/kvm_emulate.h
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

#ifndef __ARM64_KVM_EMULATE_H__
#define __ARM64_KVM_EMULATE_H__

#include <linux/kvm_host.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_mmio.h>
#include <asm/ptrace.h>

unsigned long *vcpu_reg32(const struct kvm_vcpu *vcpu, u8 reg_num);
unsigned long *vcpu_spsr32(const struct kvm_vcpu *vcpu);

bool kvm_condition_valid32(const struct kvm_vcpu *vcpu);
void kvm_skip_instr32(struct kvm_vcpu *vcpu, bool is_wide_instr);

void kvm_inject_undefined(struct kvm_vcpu *vcpu);
void kvm_inject_dabt(struct kvm_vcpu *vcpu, unsigned long addr);
void kvm_inject_pabt(struct kvm_vcpu *vcpu, unsigned long addr);

static inline unsigned long *vcpu_pc(const struct kvm_vcpu *vcpu)
{
	return (unsigned long *)&vcpu_gp_regs(vcpu)->regs.pc;
}

static inline unsigned long *vcpu_elr_el1(const struct kvm_vcpu *vcpu)
{
	return (unsigned long *)&vcpu_gp_regs(vcpu)->elr_el1;
}

static inline unsigned long *vcpu_cpsr(const struct kvm_vcpu *vcpu)
{
	return (unsigned long *)&vcpu_gp_regs(vcpu)->regs.pstate;
}

static inline bool vcpu_mode_is_32bit(const struct kvm_vcpu *vcpu)
{
	return !!(*vcpu_cpsr(vcpu) & PSR_MODE32_BIT);
}

static inline bool kvm_condition_valid(const struct kvm_vcpu *vcpu)
{
	if (vcpu_mode_is_32bit(vcpu))
		return kvm_condition_valid32(vcpu);

	return true;
}

static inline void kvm_skip_instr(struct kvm_vcpu *vcpu, bool is_wide_instr)
{
	if (vcpu_mode_is_32bit(vcpu))
		kvm_skip_instr32(vcpu, is_wide_instr);
	else
		*vcpu_pc(vcpu) += 4;
}

static inline void vcpu_set_thumb(struct kvm_vcpu *vcpu)
{
	*vcpu_cpsr(vcpu) |= COMPAT_PSR_T_BIT;
}

static inline unsigned long *vcpu_reg(const struct kvm_vcpu *vcpu, u8 reg_num)
{
	if (vcpu_mode_is_32bit(vcpu))
		return vcpu_reg32(vcpu, reg_num);

	return (unsigned long *)&vcpu_gp_regs(vcpu)->regs.regs[reg_num];
}

/* Get vcpu SPSR for current mode */
static inline unsigned long *vcpu_spsr(const struct kvm_vcpu *vcpu)
{
	if (vcpu_mode_is_32bit(vcpu))
		return vcpu_spsr32(vcpu);

	return (unsigned long *)&vcpu_gp_regs(vcpu)->spsr[KVM_SPSR_EL1];
}

static inline bool vcpu_mode_priv(const struct kvm_vcpu *vcpu)
{
	u32 mode = *vcpu_cpsr(vcpu) & PSR_MODE_MASK;

	if (vcpu_mode_is_32bit(vcpu))
		return mode > COMPAT_PSR_MODE_USR;

	return mode != PSR_MODE_EL0t;
}

static inline u32 kvm_vcpu_get_hsr(const struct kvm_vcpu *vcpu)
{
	return vcpu->arch.fault.esr_el2;
}

static inline unsigned long kvm_vcpu_get_hfar(const struct kvm_vcpu *vcpu)
{
	return vcpu->arch.fault.far_el2;
}

static inline phys_addr_t kvm_vcpu_get_fault_ipa(const struct kvm_vcpu *vcpu)
{
	return ((phys_addr_t)vcpu->arch.fault.hpfar_el2 & HPFAR_MASK) << 8;
}

static inline bool kvm_vcpu_dabt_isvalid(const struct kvm_vcpu *vcpu)
{
	return !!(kvm_vcpu_get_hsr(vcpu) & ESR_EL2_ISV);
}

static inline bool kvm_vcpu_dabt_iswrite(const struct kvm_vcpu *vcpu)
{
	return !!(kvm_vcpu_get_hsr(vcpu) & ESR_EL2_WNR);
}

static inline bool kvm_vcpu_dabt_issext(const struct kvm_vcpu *vcpu)
{
	return !!(kvm_vcpu_get_hsr(vcpu) & ESR_EL2_SSE);
}

static inline int kvm_vcpu_dabt_get_rd(const struct kvm_vcpu *vcpu)
{
	return (kvm_vcpu_get_hsr(vcpu) & ESR_EL2_SRT_MASK) >> ESR_EL2_SRT_SHIFT;
}

static inline bool kvm_vcpu_dabt_isextabt(const struct kvm_vcpu *vcpu)
{
	return !!(kvm_vcpu_get_hsr(vcpu) & ESR_EL2_EA);
}

static inline bool kvm_vcpu_dabt_iss1tw(const struct kvm_vcpu *vcpu)
{
	return !!(kvm_vcpu_get_hsr(vcpu) & ESR_EL2_S1PTW);
}

static inline int kvm_vcpu_dabt_get_as(const struct kvm_vcpu *vcpu)
{
	return 1 << ((kvm_vcpu_get_hsr(vcpu) & ESR_EL2_SAS) >> ESR_EL2_SAS_SHIFT);
}

/* This one is not specific to Data Abort */
static inline bool kvm_vcpu_trap_il_is32bit(const struct kvm_vcpu *vcpu)
{
	return !!(kvm_vcpu_get_hsr(vcpu) & ESR_EL2_IL);
}

static inline u8 kvm_vcpu_trap_get_class(const struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_get_hsr(vcpu) >> ESR_EL2_EC_SHIFT;
}

static inline bool kvm_vcpu_trap_is_iabt(const struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_trap_get_class(vcpu) == ESR_EL2_EC_IABT;
}

static inline u8 kvm_vcpu_trap_get_fault(const struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_get_hsr(vcpu) & ESR_EL2_FSC_TYPE;
}

static inline unsigned long kvm_vcpu_get_mpidr(struct kvm_vcpu *vcpu)
{
	return vcpu_sys_reg(vcpu, MPIDR_EL1);
}

#endif /* __ARM64_KVM_EMULATE_H__ */
