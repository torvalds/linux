/*
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __ARM_KVM_EMULATE_H__
#define __ARM_KVM_EMULATE_H__

#include <linux/kvm_host.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_mmio.h>

u32 *vcpu_reg(struct kvm_vcpu *vcpu, u8 reg_num);
u32 *vcpu_spsr(struct kvm_vcpu *vcpu);

int kvm_handle_wfi(struct kvm_vcpu *vcpu, struct kvm_run *run);
void kvm_skip_instr(struct kvm_vcpu *vcpu, bool is_wide_instr);
void kvm_inject_undefined(struct kvm_vcpu *vcpu);
void kvm_inject_dabt(struct kvm_vcpu *vcpu, unsigned long addr);
void kvm_inject_pabt(struct kvm_vcpu *vcpu, unsigned long addr);

static inline bool vcpu_mode_is_32bit(struct kvm_vcpu *vcpu)
{
	return 1;
}

static inline u32 *vcpu_pc(struct kvm_vcpu *vcpu)
{
	return (u32 *)&vcpu->arch.regs.usr_regs.ARM_pc;
}

static inline u32 *vcpu_cpsr(struct kvm_vcpu *vcpu)
{
	return (u32 *)&vcpu->arch.regs.usr_regs.ARM_cpsr;
}

static inline void vcpu_set_thumb(struct kvm_vcpu *vcpu)
{
	*vcpu_cpsr(vcpu) |= PSR_T_BIT;
}

static inline bool mode_has_spsr(struct kvm_vcpu *vcpu)
{
	unsigned long cpsr_mode = vcpu->arch.regs.usr_regs.ARM_cpsr & MODE_MASK;
	return (cpsr_mode > USR_MODE && cpsr_mode < SYSTEM_MODE);
}

static inline bool vcpu_mode_priv(struct kvm_vcpu *vcpu)
{
	unsigned long cpsr_mode = vcpu->arch.regs.usr_regs.ARM_cpsr & MODE_MASK;
	return cpsr_mode > USR_MODE;;
}

static inline bool kvm_vcpu_reg_is_pc(struct kvm_vcpu *vcpu, int reg)
{
	return reg == 15;
}

#endif /* __ARM_KVM_EMULATE_H__ */
