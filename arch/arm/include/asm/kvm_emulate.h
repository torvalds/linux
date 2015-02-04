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
#include <asm/kvm_arm.h>

unsigned long *vcpu_reg(struct kvm_vcpu *vcpu, u8 reg_num);
unsigned long *vcpu_spsr(struct kvm_vcpu *vcpu);

bool kvm_condition_valid(struct kvm_vcpu *vcpu);
void kvm_skip_instr(struct kvm_vcpu *vcpu, bool is_wide_instr);
void kvm_inject_undefined(struct kvm_vcpu *vcpu);
void kvm_inject_dabt(struct kvm_vcpu *vcpu, unsigned long addr);
void kvm_inject_pabt(struct kvm_vcpu *vcpu, unsigned long addr);

static inline void vcpu_reset_hcr(struct kvm_vcpu *vcpu)
{
	vcpu->arch.hcr = HCR_GUEST_MASK;
}

static inline unsigned long vcpu_get_hcr(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.hcr;
}

static inline void vcpu_set_hcr(struct kvm_vcpu *vcpu, unsigned long hcr)
{
	vcpu->arch.hcr = hcr;
}

static inline bool vcpu_mode_is_32bit(struct kvm_vcpu *vcpu)
{
	return 1;
}

static inline unsigned long *vcpu_pc(struct kvm_vcpu *vcpu)
{
	return &vcpu->arch.regs.usr_regs.ARM_pc;
}

static inline unsigned long *vcpu_cpsr(struct kvm_vcpu *vcpu)
{
	return &vcpu->arch.regs.usr_regs.ARM_cpsr;
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

static inline u32 kvm_vcpu_get_hsr(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.fault.hsr;
}

static inline unsigned long kvm_vcpu_get_hfar(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.fault.hxfar;
}

static inline phys_addr_t kvm_vcpu_get_fault_ipa(struct kvm_vcpu *vcpu)
{
	return ((phys_addr_t)vcpu->arch.fault.hpfar & HPFAR_MASK) << 8;
}

static inline unsigned long kvm_vcpu_get_hyp_pc(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.fault.hyp_pc;
}

static inline bool kvm_vcpu_dabt_isvalid(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_get_hsr(vcpu) & HSR_ISV;
}

static inline bool kvm_vcpu_dabt_iswrite(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_get_hsr(vcpu) & HSR_WNR;
}

static inline bool kvm_vcpu_dabt_issext(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_get_hsr(vcpu) & HSR_SSE;
}

static inline int kvm_vcpu_dabt_get_rd(struct kvm_vcpu *vcpu)
{
	return (kvm_vcpu_get_hsr(vcpu) & HSR_SRT_MASK) >> HSR_SRT_SHIFT;
}

static inline bool kvm_vcpu_dabt_isextabt(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_get_hsr(vcpu) & HSR_DABT_EA;
}

static inline bool kvm_vcpu_dabt_iss1tw(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_get_hsr(vcpu) & HSR_DABT_S1PTW;
}

/* Get Access Size from a data abort */
static inline int kvm_vcpu_dabt_get_as(struct kvm_vcpu *vcpu)
{
	switch ((kvm_vcpu_get_hsr(vcpu) >> 22) & 0x3) {
	case 0:
		return 1;
	case 1:
		return 2;
	case 2:
		return 4;
	default:
		kvm_err("Hardware is weird: SAS 0b11 is reserved\n");
		return -EFAULT;
	}
}

/* This one is not specific to Data Abort */
static inline bool kvm_vcpu_trap_il_is32bit(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_get_hsr(vcpu) & HSR_IL;
}

static inline u8 kvm_vcpu_trap_get_class(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_get_hsr(vcpu) >> HSR_EC_SHIFT;
}

static inline bool kvm_vcpu_trap_is_iabt(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_trap_get_class(vcpu) == HSR_EC_IABT;
}

static inline u8 kvm_vcpu_trap_get_fault(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_get_hsr(vcpu) & HSR_FSC;
}

static inline u8 kvm_vcpu_trap_get_fault_type(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_get_hsr(vcpu) & HSR_FSC_TYPE;
}

static inline u32 kvm_vcpu_hvc_get_imm(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_get_hsr(vcpu) & HSR_HVC_IMM_MASK;
}

static inline unsigned long kvm_vcpu_get_mpidr(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.cp15[c0_MPIDR];
}

static inline void kvm_vcpu_set_be(struct kvm_vcpu *vcpu)
{
	*vcpu_cpsr(vcpu) |= PSR_E_BIT;
}

static inline bool kvm_vcpu_is_be(struct kvm_vcpu *vcpu)
{
	return !!(*vcpu_cpsr(vcpu) & PSR_E_BIT);
}

static inline unsigned long vcpu_data_guest_to_host(struct kvm_vcpu *vcpu,
						    unsigned long data,
						    unsigned int len)
{
	if (kvm_vcpu_is_be(vcpu)) {
		switch (len) {
		case 1:
			return data & 0xff;
		case 2:
			return be16_to_cpu(data & 0xffff);
		default:
			return be32_to_cpu(data);
		}
	} else {
		switch (len) {
		case 1:
			return data & 0xff;
		case 2:
			return le16_to_cpu(data & 0xffff);
		default:
			return le32_to_cpu(data);
		}
	}
}

static inline unsigned long vcpu_data_host_to_guest(struct kvm_vcpu *vcpu,
						    unsigned long data,
						    unsigned int len)
{
	if (kvm_vcpu_is_be(vcpu)) {
		switch (len) {
		case 1:
			return data & 0xff;
		case 2:
			return cpu_to_be16(data & 0xffff);
		default:
			return cpu_to_be32(data);
		}
	} else {
		switch (len) {
		case 1:
			return data & 0xff;
		case 2:
			return cpu_to_le16(data & 0xffff);
		default:
			return cpu_to_le32(data);
		}
	}
}

#endif /* __ARM_KVM_EMULATE_H__ */
