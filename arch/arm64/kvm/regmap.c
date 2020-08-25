// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Derived from arch/arm/kvm/emulate.c:
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 */

#include <linux/mm.h>
#include <linux/kvm_host.h>
#include <asm/kvm_emulate.h>
#include <asm/ptrace.h>

#define VCPU_NR_MODES 6
#define REG_OFFSET(_reg) \
	(offsetof(struct user_pt_regs, _reg) / sizeof(unsigned long))

#define USR_REG_OFFSET(R) REG_OFFSET(compat_usr(R))

static const unsigned long vcpu_reg_offsets[VCPU_NR_MODES][16] = {
	/* USR Registers */
	{
		USR_REG_OFFSET(0), USR_REG_OFFSET(1), USR_REG_OFFSET(2),
		USR_REG_OFFSET(3), USR_REG_OFFSET(4), USR_REG_OFFSET(5),
		USR_REG_OFFSET(6), USR_REG_OFFSET(7), USR_REG_OFFSET(8),
		USR_REG_OFFSET(9), USR_REG_OFFSET(10), USR_REG_OFFSET(11),
		USR_REG_OFFSET(12), USR_REG_OFFSET(13),	USR_REG_OFFSET(14),
		REG_OFFSET(pc)
	},

	/* FIQ Registers */
	{
		USR_REG_OFFSET(0), USR_REG_OFFSET(1), USR_REG_OFFSET(2),
		USR_REG_OFFSET(3), USR_REG_OFFSET(4), USR_REG_OFFSET(5),
		USR_REG_OFFSET(6), USR_REG_OFFSET(7),
		REG_OFFSET(compat_r8_fiq),  /* r8 */
		REG_OFFSET(compat_r9_fiq),  /* r9 */
		REG_OFFSET(compat_r10_fiq), /* r10 */
		REG_OFFSET(compat_r11_fiq), /* r11 */
		REG_OFFSET(compat_r12_fiq), /* r12 */
		REG_OFFSET(compat_sp_fiq),  /* r13 */
		REG_OFFSET(compat_lr_fiq),  /* r14 */
		REG_OFFSET(pc)
	},

	/* IRQ Registers */
	{
		USR_REG_OFFSET(0), USR_REG_OFFSET(1), USR_REG_OFFSET(2),
		USR_REG_OFFSET(3), USR_REG_OFFSET(4), USR_REG_OFFSET(5),
		USR_REG_OFFSET(6), USR_REG_OFFSET(7), USR_REG_OFFSET(8),
		USR_REG_OFFSET(9), USR_REG_OFFSET(10), USR_REG_OFFSET(11),
		USR_REG_OFFSET(12),
		REG_OFFSET(compat_sp_irq), /* r13 */
		REG_OFFSET(compat_lr_irq), /* r14 */
		REG_OFFSET(pc)
	},

	/* SVC Registers */
	{
		USR_REG_OFFSET(0), USR_REG_OFFSET(1), USR_REG_OFFSET(2),
		USR_REG_OFFSET(3), USR_REG_OFFSET(4), USR_REG_OFFSET(5),
		USR_REG_OFFSET(6), USR_REG_OFFSET(7), USR_REG_OFFSET(8),
		USR_REG_OFFSET(9), USR_REG_OFFSET(10), USR_REG_OFFSET(11),
		USR_REG_OFFSET(12),
		REG_OFFSET(compat_sp_svc), /* r13 */
		REG_OFFSET(compat_lr_svc), /* r14 */
		REG_OFFSET(pc)
	},

	/* ABT Registers */
	{
		USR_REG_OFFSET(0), USR_REG_OFFSET(1), USR_REG_OFFSET(2),
		USR_REG_OFFSET(3), USR_REG_OFFSET(4), USR_REG_OFFSET(5),
		USR_REG_OFFSET(6), USR_REG_OFFSET(7), USR_REG_OFFSET(8),
		USR_REG_OFFSET(9), USR_REG_OFFSET(10), USR_REG_OFFSET(11),
		USR_REG_OFFSET(12),
		REG_OFFSET(compat_sp_abt), /* r13 */
		REG_OFFSET(compat_lr_abt), /* r14 */
		REG_OFFSET(pc)
	},

	/* UND Registers */
	{
		USR_REG_OFFSET(0), USR_REG_OFFSET(1), USR_REG_OFFSET(2),
		USR_REG_OFFSET(3), USR_REG_OFFSET(4), USR_REG_OFFSET(5),
		USR_REG_OFFSET(6), USR_REG_OFFSET(7), USR_REG_OFFSET(8),
		USR_REG_OFFSET(9), USR_REG_OFFSET(10), USR_REG_OFFSET(11),
		USR_REG_OFFSET(12),
		REG_OFFSET(compat_sp_und), /* r13 */
		REG_OFFSET(compat_lr_und), /* r14 */
		REG_OFFSET(pc)
	},
};

/*
 * Return a pointer to the register number valid in the current mode of
 * the virtual CPU.
 */
unsigned long *vcpu_reg32(const struct kvm_vcpu *vcpu, u8 reg_num)
{
	unsigned long *reg_array = (unsigned long *)&vcpu->arch.ctxt.regs;
	unsigned long mode = *vcpu_cpsr(vcpu) & PSR_AA32_MODE_MASK;

	switch (mode) {
	case PSR_AA32_MODE_USR ... PSR_AA32_MODE_SVC:
		mode &= ~PSR_MODE32_BIT; /* 0 ... 3 */
		break;

	case PSR_AA32_MODE_ABT:
		mode = 4;
		break;

	case PSR_AA32_MODE_UND:
		mode = 5;
		break;

	case PSR_AA32_MODE_SYS:
		mode = 0;	/* SYS maps to USR */
		break;

	default:
		BUG();
	}

	return reg_array + vcpu_reg_offsets[mode][reg_num];
}

/*
 * Return the SPSR for the current mode of the virtual CPU.
 */
static int vcpu_spsr32_mode(const struct kvm_vcpu *vcpu)
{
	unsigned long mode = *vcpu_cpsr(vcpu) & PSR_AA32_MODE_MASK;
	switch (mode) {
	case PSR_AA32_MODE_SVC: return KVM_SPSR_SVC;
	case PSR_AA32_MODE_ABT: return KVM_SPSR_ABT;
	case PSR_AA32_MODE_UND: return KVM_SPSR_UND;
	case PSR_AA32_MODE_IRQ: return KVM_SPSR_IRQ;
	case PSR_AA32_MODE_FIQ: return KVM_SPSR_FIQ;
	default: BUG();
	}
}

unsigned long vcpu_read_spsr32(const struct kvm_vcpu *vcpu)
{
	int spsr_idx = vcpu_spsr32_mode(vcpu);

	if (!vcpu->arch.sysregs_loaded_on_cpu) {
		switch (spsr_idx) {
		case KVM_SPSR_SVC:
			return __vcpu_sys_reg(vcpu, SPSR_EL1);
		case KVM_SPSR_ABT:
			return vcpu->arch.ctxt.spsr_abt;
		case KVM_SPSR_UND:
			return vcpu->arch.ctxt.spsr_und;
		case KVM_SPSR_IRQ:
			return vcpu->arch.ctxt.spsr_irq;
		case KVM_SPSR_FIQ:
			return vcpu->arch.ctxt.spsr_fiq;
		}
	}

	switch (spsr_idx) {
	case KVM_SPSR_SVC:
		return read_sysreg_el1(SYS_SPSR);
	case KVM_SPSR_ABT:
		return read_sysreg(spsr_abt);
	case KVM_SPSR_UND:
		return read_sysreg(spsr_und);
	case KVM_SPSR_IRQ:
		return read_sysreg(spsr_irq);
	case KVM_SPSR_FIQ:
		return read_sysreg(spsr_fiq);
	default:
		BUG();
	}
}

void vcpu_write_spsr32(struct kvm_vcpu *vcpu, unsigned long v)
{
	int spsr_idx = vcpu_spsr32_mode(vcpu);

	if (!vcpu->arch.sysregs_loaded_on_cpu) {
		switch (spsr_idx) {
		case KVM_SPSR_SVC:
			__vcpu_sys_reg(vcpu, SPSR_EL1) = v;
			break;
		case KVM_SPSR_ABT:
			vcpu->arch.ctxt.spsr_abt = v;
			break;
		case KVM_SPSR_UND:
			vcpu->arch.ctxt.spsr_und = v;
			break;
		case KVM_SPSR_IRQ:
			vcpu->arch.ctxt.spsr_irq = v;
			break;
		case KVM_SPSR_FIQ:
			vcpu->arch.ctxt.spsr_fiq = v;
			break;
		}

		return;
	}

	switch (spsr_idx) {
	case KVM_SPSR_SVC:
		write_sysreg_el1(v, SYS_SPSR);
		break;
	case KVM_SPSR_ABT:
		write_sysreg(v, spsr_abt);
		break;
	case KVM_SPSR_UND:
		write_sysreg(v, spsr_und);
		break;
	case KVM_SPSR_IRQ:
		write_sysreg(v, spsr_irq);
		break;
	case KVM_SPSR_FIQ:
		write_sysreg(v, spsr_fiq);
		break;
	}
}
