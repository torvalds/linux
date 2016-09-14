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

/*
 * A conditional instruction is allowed to trap, even though it
 * wouldn't be executed.  So let's re-implement the hardware, in
 * software!
 */
bool kvm_condition_valid(struct kvm_vcpu *vcpu)
{
	unsigned long cpsr, cond, insn;

	/*
	 * Exception Code 0 can only happen if we set HCR.TGE to 1, to
	 * catch undefined instructions, and then we won't get past
	 * the arm_exit_handlers test anyway.
	 */
	BUG_ON(!kvm_vcpu_trap_get_class(vcpu));

	/* Top two bits non-zero?  Unconditional. */
	if (kvm_vcpu_get_hsr(vcpu) >> 30)
		return true;

	cpsr = *vcpu_cpsr(vcpu);

	/* Is condition field valid? */
	if ((kvm_vcpu_get_hsr(vcpu) & HSR_CV) >> HSR_CV_SHIFT)
		cond = (kvm_vcpu_get_hsr(vcpu) & HSR_COND) >> HSR_COND_SHIFT;
	else {
		/* This can happen in Thumb mode: examine IT state. */
		unsigned long it;

		it = ((cpsr >> 8) & 0xFC) | ((cpsr >> 25) & 0x3);

		/* it == 0 => unconditional. */
		if (it == 0)
			return true;

		/* The cond for this insn works out as the top 4 bits. */
		cond = (it >> 4);
	}

	/* Shift makes it look like an ARM-mode instruction */
	insn = cond << 28;
	return arm_check_condition(insn, cpsr) != ARM_OPCODE_CONDTEST_FAIL;
}

/**
 * adjust_itstate - adjust ITSTATE when emulating instructions in IT-block
 * @vcpu:	The VCPU pointer
 *
 * When exceptions occur while instructions are executed in Thumb IF-THEN
 * blocks, the ITSTATE field of the CPSR is not advanced (updated), so we have
 * to do this little bit of work manually. The fields map like this:
 *
 * IT[7:0] -> CPSR[26:25],CPSR[15:10]
 */
static void kvm_adjust_itstate(struct kvm_vcpu *vcpu)
{
	unsigned long itbits, cond;
	unsigned long cpsr = *vcpu_cpsr(vcpu);
	bool is_arm = !(cpsr & PSR_T_BIT);

	BUG_ON(is_arm && (cpsr & PSR_IT_MASK));

	if (!(cpsr & PSR_IT_MASK))
		return;

	cond = (cpsr & 0xe000) >> 13;
	itbits = (cpsr & 0x1c00) >> (10 - 2);
	itbits |= (cpsr & (0x3 << 25)) >> 25;

	/* Perform ITAdvance (see page A-52 in ARM DDI 0406C) */
	if ((itbits & 0x7) == 0)
		itbits = cond = 0;
	else
		itbits = (itbits << 1) & 0x1f;

	cpsr &= ~PSR_IT_MASK;
	cpsr |= cond << 13;
	cpsr |= (itbits & 0x1c) << (10 - 2);
	cpsr |= (itbits & 0x3) << 25;
	*vcpu_cpsr(vcpu) = cpsr;
}

/**
 * kvm_skip_instr - skip a trapped instruction and proceed to the next
 * @vcpu: The vcpu pointer
 */
void kvm_skip_instr(struct kvm_vcpu *vcpu, bool is_wide_instr)
{
	bool is_thumb;

	is_thumb = !!(*vcpu_cpsr(vcpu) & PSR_T_BIT);
	if (is_thumb && !is_wide_instr)
		*vcpu_pc(vcpu) += 2;
	else
		*vcpu_pc(vcpu) += 4;
	kvm_adjust_itstate(vcpu);
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
	*vcpu_reg(vcpu, 14) = *vcpu_pc(vcpu) - return_offset;

	/* Branch to exception vector */
	*vcpu_pc(vcpu) = exc_vector_base(vcpu) + vect_offset;
}

/*
 * Modelled after TakeDataAbortException() and TakePrefetchAbortException
 * pseudocode.
 */
static void inject_abt(struct kvm_vcpu *vcpu, bool is_pabt, unsigned long addr)
{
	unsigned long cpsr = *vcpu_cpsr(vcpu);
	bool is_thumb = (cpsr & PSR_T_BIT);
	u32 vect_offset;
	u32 return_offset = (is_thumb) ? 4 : 0;
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
