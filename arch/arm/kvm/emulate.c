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

#include <linux/mm.h>
#include <linux/kvm_host.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_emulate.h>
#include <asm/opcodes.h>
#include <trace/events/kvm.h>

#include "trace.h"

#define VCPU_NR_MODES		6
#define VCPU_REG_OFFSET_USR	0
#define VCPU_REG_OFFSET_FIQ	1
#define VCPU_REG_OFFSET_IRQ	2
#define VCPU_REG_OFFSET_SVC	3
#define VCPU_REG_OFFSET_ABT	4
#define VCPU_REG_OFFSET_UND	5
#define REG_OFFSET(_reg) \
	(offsetof(struct kvm_regs, _reg) / sizeof(u32))

#define USR_REG_OFFSET(_num) REG_OFFSET(usr_regs.uregs[_num])

static const unsigned long vcpu_reg_offsets[VCPU_NR_MODES][15] = {
	/* USR/SYS Registers */
	[VCPU_REG_OFFSET_USR] = {
		USR_REG_OFFSET(0), USR_REG_OFFSET(1), USR_REG_OFFSET(2),
		USR_REG_OFFSET(3), USR_REG_OFFSET(4), USR_REG_OFFSET(5),
		USR_REG_OFFSET(6), USR_REG_OFFSET(7), USR_REG_OFFSET(8),
		USR_REG_OFFSET(9), USR_REG_OFFSET(10), USR_REG_OFFSET(11),
		USR_REG_OFFSET(12), USR_REG_OFFSET(13),	USR_REG_OFFSET(14),
	},

	/* FIQ Registers */
	[VCPU_REG_OFFSET_FIQ] = {
		USR_REG_OFFSET(0), USR_REG_OFFSET(1), USR_REG_OFFSET(2),
		USR_REG_OFFSET(3), USR_REG_OFFSET(4), USR_REG_OFFSET(5),
		USR_REG_OFFSET(6), USR_REG_OFFSET(7),
		REG_OFFSET(fiq_regs[0]), /* r8 */
		REG_OFFSET(fiq_regs[1]), /* r9 */
		REG_OFFSET(fiq_regs[2]), /* r10 */
		REG_OFFSET(fiq_regs[3]), /* r11 */
		REG_OFFSET(fiq_regs[4]), /* r12 */
		REG_OFFSET(fiq_regs[5]), /* r13 */
		REG_OFFSET(fiq_regs[6]), /* r14 */
	},

	/* IRQ Registers */
	[VCPU_REG_OFFSET_IRQ] = {
		USR_REG_OFFSET(0), USR_REG_OFFSET(1), USR_REG_OFFSET(2),
		USR_REG_OFFSET(3), USR_REG_OFFSET(4), USR_REG_OFFSET(5),
		USR_REG_OFFSET(6), USR_REG_OFFSET(7), USR_REG_OFFSET(8),
		USR_REG_OFFSET(9), USR_REG_OFFSET(10), USR_REG_OFFSET(11),
		USR_REG_OFFSET(12),
		REG_OFFSET(irq_regs[0]), /* r13 */
		REG_OFFSET(irq_regs[1]), /* r14 */
	},

	/* SVC Registers */
	[VCPU_REG_OFFSET_SVC] = {
		USR_REG_OFFSET(0), USR_REG_OFFSET(1), USR_REG_OFFSET(2),
		USR_REG_OFFSET(3), USR_REG_OFFSET(4), USR_REG_OFFSET(5),
		USR_REG_OFFSET(6), USR_REG_OFFSET(7), USR_REG_OFFSET(8),
		USR_REG_OFFSET(9), USR_REG_OFFSET(10), USR_REG_OFFSET(11),
		USR_REG_OFFSET(12),
		REG_OFFSET(svc_regs[0]), /* r13 */
		REG_OFFSET(svc_regs[1]), /* r14 */
	},

	/* ABT Registers */
	[VCPU_REG_OFFSET_ABT] = {
		USR_REG_OFFSET(0), USR_REG_OFFSET(1), USR_REG_OFFSET(2),
		USR_REG_OFFSET(3), USR_REG_OFFSET(4), USR_REG_OFFSET(5),
		USR_REG_OFFSET(6), USR_REG_OFFSET(7), USR_REG_OFFSET(8),
		USR_REG_OFFSET(9), USR_REG_OFFSET(10), USR_REG_OFFSET(11),
		USR_REG_OFFSET(12),
		REG_OFFSET(abt_regs[0]), /* r13 */
		REG_OFFSET(abt_regs[1]), /* r14 */
	},

	/* UND Registers */
	[VCPU_REG_OFFSET_UND] = {
		USR_REG_OFFSET(0), USR_REG_OFFSET(1), USR_REG_OFFSET(2),
		USR_REG_OFFSET(3), USR_REG_OFFSET(4), USR_REG_OFFSET(5),
		USR_REG_OFFSET(6), USR_REG_OFFSET(7), USR_REG_OFFSET(8),
		USR_REG_OFFSET(9), USR_REG_OFFSET(10), USR_REG_OFFSET(11),
		USR_REG_OFFSET(12),
		REG_OFFSET(und_regs[0]), /* r13 */
		REG_OFFSET(und_regs[1]), /* r14 */
	},
};

/*
 * Return a pointer to the register number valid in the current mode of
 * the virtual CPU.
 */
unsigned long *vcpu_reg(struct kvm_vcpu *vcpu, u8 reg_num)
{
	unsigned long *reg_array = (unsigned long *)&vcpu->arch.ctxt.gp_regs;
	unsigned long mode = *vcpu_cpsr(vcpu) & MODE_MASK;

	switch (mode) {
	case USR_MODE...SVC_MODE:
		mode &= ~MODE32_BIT; /* 0 ... 3 */
		break;

	case ABT_MODE:
		mode = VCPU_REG_OFFSET_ABT;
		break;

	case UND_MODE:
		mode = VCPU_REG_OFFSET_UND;
		break;

	case SYSTEM_MODE:
		mode = VCPU_REG_OFFSET_USR;
		break;

	default:
		BUG();
	}

	return reg_array + vcpu_reg_offsets[mode][reg_num];
}

/*
 * Return the SPSR for the current mode of the virtual CPU.
 */
unsigned long *vcpu_spsr(struct kvm_vcpu *vcpu)
{
	unsigned long mode = *vcpu_cpsr(vcpu) & MODE_MASK;
	switch (mode) {
	case SVC_MODE:
		return &vcpu->arch.ctxt.gp_regs.KVM_ARM_SVC_spsr;
	case ABT_MODE:
		return &vcpu->arch.ctxt.gp_regs.KVM_ARM_ABT_spsr;
	case UND_MODE:
		return &vcpu->arch.ctxt.gp_regs.KVM_ARM_UND_spsr;
	case IRQ_MODE:
		return &vcpu->arch.ctxt.gp_regs.KVM_ARM_IRQ_spsr;
	case FIQ_MODE:
		return &vcpu->arch.ctxt.gp_regs.KVM_ARM_FIQ_spsr;
	default:
		BUG();
	}
}

/******************************************************************************
 * Inject exceptions into the guest
 */

static u32 exc_vector_base(struct kvm_vcpu *vcpu)
{
	u32 sctlr = vcpu_cp15(vcpu, c1_SCTLR);
	u32 vbar = vcpu_cp15(vcpu, c12_VBAR);

	if (sctlr & SCTLR_V)
		return 0xffff0000;
	else /* always have security exceptions */
		return vbar;
}

/*
 * Switch to an exception mode, updating both CPSR and SPSR. Follow
 * the logic described in AArch32.EnterMode() from the ARMv8 ARM.
 */
static void kvm_update_psr(struct kvm_vcpu *vcpu, unsigned long mode)
{
	unsigned long cpsr = *vcpu_cpsr(vcpu);
	u32 sctlr = vcpu_cp15(vcpu, c1_SCTLR);

	*vcpu_cpsr(vcpu) = (cpsr & ~MODE_MASK) | mode;

	switch (mode) {
	case FIQ_MODE:
		*vcpu_cpsr(vcpu) |= PSR_F_BIT;
		/* Fall through */
	case ABT_MODE:
	case IRQ_MODE:
		*vcpu_cpsr(vcpu) |= PSR_A_BIT;
		/* Fall through */
	default:
		*vcpu_cpsr(vcpu) |= PSR_I_BIT;
	}

	*vcpu_cpsr(vcpu) &= ~(PSR_IT_MASK | PSR_J_BIT | PSR_E_BIT | PSR_T_BIT);

	if (sctlr & SCTLR_TE)
		*vcpu_cpsr(vcpu) |= PSR_T_BIT;
	if (sctlr & SCTLR_EE)
		*vcpu_cpsr(vcpu) |= PSR_E_BIT;

	/* Note: These now point to the mode banked copies */
	*vcpu_spsr(vcpu) = cpsr;
}

/**
 * kvm_inject_undefined - inject an undefined exception into the guest
 * @vcpu: The VCPU to receive the undefined exception
 *
 * It is assumed that this code is called from the VCPU thread and that the
 * VCPU therefore is not currently executing guest code.
 *
 * Modelled after TakeUndefInstrException() pseudocode.
 */
void kvm_inject_undefined(struct kvm_vcpu *vcpu)
{
	unsigned long cpsr = *vcpu_cpsr(vcpu);
	bool is_thumb = (cpsr & PSR_T_BIT);
	u32 vect_offset = 4;
	u32 return_offset = (is_thumb) ? 2 : 4;

	kvm_update_psr(vcpu, UND_MODE);
	*vcpu_reg(vcpu, 14) = *vcpu_pc(vcpu) + return_offset;

	/* Branch to exception vector */
	*vcpu_pc(vcpu) = exc_vector_base(vcpu) + vect_offset;
}

/*
 * Modelled after TakeDataAbortException() and TakePrefetchAbortException
 * pseudocode.
 */
static void inject_abt(struct kvm_vcpu *vcpu, bool is_pabt, unsigned long addr)
{
	u32 vect_offset;
	u32 return_offset = (is_pabt) ? 4 : 8;
	bool is_lpae;

	kvm_update_psr(vcpu, ABT_MODE);
	*vcpu_reg(vcpu, 14) = *vcpu_pc(vcpu) + return_offset;

	if (is_pabt)
		vect_offset = 12;
	else
		vect_offset = 16;

	/* Branch to exception vector */
	*vcpu_pc(vcpu) = exc_vector_base(vcpu) + vect_offset;

	if (is_pabt) {
		/* Set IFAR and IFSR */
		vcpu_cp15(vcpu, c6_IFAR) = addr;
		is_lpae = (vcpu_cp15(vcpu, c2_TTBCR) >> 31);
		/* Always give debug fault for now - should give guest a clue */
		if (is_lpae)
			vcpu_cp15(vcpu, c5_IFSR) = 1 << 9 | 0x22;
		else
			vcpu_cp15(vcpu, c5_IFSR) = 2;
	} else { /* !iabt */
		/* Set DFAR and DFSR */
		vcpu_cp15(vcpu, c6_DFAR) = addr;
		is_lpae = (vcpu_cp15(vcpu, c2_TTBCR) >> 31);
		/* Always give debug fault for now - should give guest a clue */
		if (is_lpae)
			vcpu_cp15(vcpu, c5_DFSR) = 1 << 9 | 0x22;
		else
			vcpu_cp15(vcpu, c5_DFSR) = 2;
	}

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
	inject_abt(vcpu, false, addr);
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
	inject_abt(vcpu, true, addr);
}

/**
 * kvm_inject_vabt - inject an async abort / SError into the guest
 * @vcpu: The VCPU to receive the exception
 *
 * It is assumed that this code is called from the VCPU thread and that the
 * VCPU therefore is not currently executing guest code.
 */
void kvm_inject_vabt(struct kvm_vcpu *vcpu)
{
	vcpu_set_hcr(vcpu, vcpu_get_hcr(vcpu) | HCR_VA);
}
