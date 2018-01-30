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

#include <asm/esr.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_mmio.h>
#include <asm/ptrace.h>
#include <asm/cputype.h>
#include <asm/virt.h>

unsigned long *vcpu_reg32(const struct kvm_vcpu *vcpu, u8 reg_num);
unsigned long *vcpu_spsr32(const struct kvm_vcpu *vcpu);

bool kvm_condition_valid32(const struct kvm_vcpu *vcpu);
void kvm_skip_instr32(struct kvm_vcpu *vcpu, bool is_wide_instr);

void kvm_inject_undefined(struct kvm_vcpu *vcpu);
void kvm_inject_vabt(struct kvm_vcpu *vcpu);
void kvm_inject_dabt(struct kvm_vcpu *vcpu, unsigned long addr);
void kvm_inject_pabt(struct kvm_vcpu *vcpu, unsigned long addr);
void kvm_inject_undef32(struct kvm_vcpu *vcpu);
void kvm_inject_dabt32(struct kvm_vcpu *vcpu, unsigned long addr);
void kvm_inject_pabt32(struct kvm_vcpu *vcpu, unsigned long addr);

static inline void vcpu_reset_hcr(struct kvm_vcpu *vcpu)
{
	vcpu->arch.hcr_el2 = HCR_GUEST_FLAGS;
	if (is_kernel_in_hyp_mode())
		vcpu->arch.hcr_el2 |= HCR_E2H;
	if (cpus_have_const_cap(ARM64_HAS_RAS_EXTN)) {
		/* route synchronous external abort exceptions to EL2 */
		vcpu->arch.hcr_el2 |= HCR_TEA;
		/* trap error record accesses */
		vcpu->arch.hcr_el2 |= HCR_TERR;
	}

	if (test_bit(KVM_ARM_VCPU_EL1_32BIT, vcpu->arch.features))
		vcpu->arch.hcr_el2 &= ~HCR_RW;
}

static inline unsigned long vcpu_get_hcr(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.hcr_el2;
}

static inline void vcpu_set_hcr(struct kvm_vcpu *vcpu, unsigned long hcr)
{
	vcpu->arch.hcr_el2 = hcr;
}

static inline void vcpu_set_vsesr(struct kvm_vcpu *vcpu, u64 vsesr)
{
	vcpu->arch.vsesr_el2 = vsesr;
}

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

/*
 * vcpu_get_reg and vcpu_set_reg should always be passed a register number
 * coming from a read of ESR_EL2. Otherwise, it may give the wrong result on
 * AArch32 with banked registers.
 */
static inline unsigned long vcpu_get_reg(const struct kvm_vcpu *vcpu,
					 u8 reg_num)
{
	return (reg_num == 31) ? 0 : vcpu_gp_regs(vcpu)->regs.regs[reg_num];
}

static inline void vcpu_set_reg(struct kvm_vcpu *vcpu, u8 reg_num,
				unsigned long val)
{
	if (reg_num != 31)
		vcpu_gp_regs(vcpu)->regs.regs[reg_num] = val;
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
	u32 mode;

	if (vcpu_mode_is_32bit(vcpu)) {
		mode = *vcpu_cpsr(vcpu) & COMPAT_PSR_MODE_MASK;
		return mode > COMPAT_PSR_MODE_USR;
	}

	mode = *vcpu_cpsr(vcpu) & PSR_MODE_MASK;

	return mode != PSR_MODE_EL0t;
}

static inline u32 kvm_vcpu_get_hsr(const struct kvm_vcpu *vcpu)
{
	return vcpu->arch.fault.esr_el2;
}

static inline int kvm_vcpu_get_condition(const struct kvm_vcpu *vcpu)
{
	u32 esr = kvm_vcpu_get_hsr(vcpu);

	if (esr & ESR_ELx_CV)
		return (esr & ESR_ELx_COND_MASK) >> ESR_ELx_COND_SHIFT;

	return -1;
}

static inline unsigned long kvm_vcpu_get_hfar(const struct kvm_vcpu *vcpu)
{
	return vcpu->arch.fault.far_el2;
}

static inline phys_addr_t kvm_vcpu_get_fault_ipa(const struct kvm_vcpu *vcpu)
{
	return ((phys_addr_t)vcpu->arch.fault.hpfar_el2 & HPFAR_MASK) << 8;
}

static inline u64 kvm_vcpu_get_disr(const struct kvm_vcpu *vcpu)
{
	return vcpu->arch.fault.disr_el1;
}

static inline u32 kvm_vcpu_hvc_get_imm(const struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_get_hsr(vcpu) & ESR_ELx_xVC_IMM_MASK;
}

static inline bool kvm_vcpu_dabt_isvalid(const struct kvm_vcpu *vcpu)
{
	return !!(kvm_vcpu_get_hsr(vcpu) & ESR_ELx_ISV);
}

static inline bool kvm_vcpu_dabt_issext(const struct kvm_vcpu *vcpu)
{
	return !!(kvm_vcpu_get_hsr(vcpu) & ESR_ELx_SSE);
}

static inline int kvm_vcpu_dabt_get_rd(const struct kvm_vcpu *vcpu)
{
	return (kvm_vcpu_get_hsr(vcpu) & ESR_ELx_SRT_MASK) >> ESR_ELx_SRT_SHIFT;
}

static inline bool kvm_vcpu_dabt_iss1tw(const struct kvm_vcpu *vcpu)
{
	return !!(kvm_vcpu_get_hsr(vcpu) & ESR_ELx_S1PTW);
}

static inline bool kvm_vcpu_dabt_iswrite(const struct kvm_vcpu *vcpu)
{
	return !!(kvm_vcpu_get_hsr(vcpu) & ESR_ELx_WNR) ||
		kvm_vcpu_dabt_iss1tw(vcpu); /* AF/DBM update */
}

static inline bool kvm_vcpu_dabt_is_cm(const struct kvm_vcpu *vcpu)
{
	return !!(kvm_vcpu_get_hsr(vcpu) & ESR_ELx_CM);
}

static inline int kvm_vcpu_dabt_get_as(const struct kvm_vcpu *vcpu)
{
	return 1 << ((kvm_vcpu_get_hsr(vcpu) & ESR_ELx_SAS) >> ESR_ELx_SAS_SHIFT);
}

/* This one is not specific to Data Abort */
static inline bool kvm_vcpu_trap_il_is32bit(const struct kvm_vcpu *vcpu)
{
	return !!(kvm_vcpu_get_hsr(vcpu) & ESR_ELx_IL);
}

static inline u8 kvm_vcpu_trap_get_class(const struct kvm_vcpu *vcpu)
{
	return ESR_ELx_EC(kvm_vcpu_get_hsr(vcpu));
}

static inline bool kvm_vcpu_trap_is_iabt(const struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_trap_get_class(vcpu) == ESR_ELx_EC_IABT_LOW;
}

static inline u8 kvm_vcpu_trap_get_fault(const struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_get_hsr(vcpu) & ESR_ELx_FSC;
}

static inline u8 kvm_vcpu_trap_get_fault_type(const struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_get_hsr(vcpu) & ESR_ELx_FSC_TYPE;
}

static inline bool kvm_vcpu_dabt_isextabt(const struct kvm_vcpu *vcpu)
{
	switch (kvm_vcpu_trap_get_fault(vcpu)) {
	case FSC_SEA:
	case FSC_SEA_TTW0:
	case FSC_SEA_TTW1:
	case FSC_SEA_TTW2:
	case FSC_SEA_TTW3:
	case FSC_SECC:
	case FSC_SECC_TTW0:
	case FSC_SECC_TTW1:
	case FSC_SECC_TTW2:
	case FSC_SECC_TTW3:
		return true;
	default:
		return false;
	}
}

static inline int kvm_vcpu_sys_get_rt(struct kvm_vcpu *vcpu)
{
	u32 esr = kvm_vcpu_get_hsr(vcpu);
	return (esr & ESR_ELx_SYS64_ISS_RT_MASK) >> ESR_ELx_SYS64_ISS_RT_SHIFT;
}

static inline unsigned long kvm_vcpu_get_mpidr_aff(struct kvm_vcpu *vcpu)
{
	return vcpu_sys_reg(vcpu, MPIDR_EL1) & MPIDR_HWID_BITMASK;
}

static inline void kvm_vcpu_set_be(struct kvm_vcpu *vcpu)
{
	if (vcpu_mode_is_32bit(vcpu))
		*vcpu_cpsr(vcpu) |= COMPAT_PSR_E_BIT;
	else
		vcpu_sys_reg(vcpu, SCTLR_EL1) |= (1 << 25);
}

static inline bool kvm_vcpu_is_be(struct kvm_vcpu *vcpu)
{
	if (vcpu_mode_is_32bit(vcpu))
		return !!(*vcpu_cpsr(vcpu) & COMPAT_PSR_E_BIT);

	return !!(vcpu_sys_reg(vcpu, SCTLR_EL1) & (1 << 25));
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
		case 4:
			return be32_to_cpu(data & 0xffffffff);
		default:
			return be64_to_cpu(data);
		}
	} else {
		switch (len) {
		case 1:
			return data & 0xff;
		case 2:
			return le16_to_cpu(data & 0xffff);
		case 4:
			return le32_to_cpu(data & 0xffffffff);
		default:
			return le64_to_cpu(data);
		}
	}

	return data;		/* Leave LE untouched */
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
		case 4:
			return cpu_to_be32(data & 0xffffffff);
		default:
			return cpu_to_be64(data);
		}
	} else {
		switch (len) {
		case 1:
			return data & 0xff;
		case 2:
			return cpu_to_le16(data & 0xffff);
		case 4:
			return cpu_to_le32(data & 0xffffffff);
		default:
			return cpu_to_le64(data);
		}
	}

	return data;		/* Leave LE untouched */
}

#endif /* __ARM64_KVM_EMULATE_H__ */
