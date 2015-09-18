/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Derived from arch/arm/kvm/emulate.c:
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
	unsigned long *reg_array = (unsigned long *)&vcpu->arch.ctxt.gp_regs.regs;
	unsigned long mode = *vcpu_cpsr(vcpu) & COMPAT_PSR_MODE_MASK;

	switch (mode) {
	case COMPAT_PSR_MODE_USR ... COMPAT_PSR_MODE_SVC:
		mode &= ~PSR_MODE32_BIT; /* 0 ... 3 */
		break;

	case COMPAT_PSR_MODE_ABT:
		mode = 4;
		break;

	case COMPAT_PSR_MODE_UND:
		mode = 5;
		break;

	case COMPAT_PSR_MODE_SYS:
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
unsigned long *vcpu_spsr32(const struct kvm_vcpu *vcpu)
{
	unsigned long mode = *vcpu_cpsr(vcpu) & COMPAT_PSR_MODE_MASK;
	switch (mode) {
	case COMPAT_PSR_MODE_SVC:
		mode = KVM_SPSR_SVC;
		break;
	case COMPAT_PSR_MODE_ABT:
		mode = KVM_SPSR_ABT;
		break;
	case COMPAT_PSR_MODE_UND:
		mode = KVM_SPSR_UND;
		break;
	case COMPAT_PSR_MODE_IRQ:
		mode = KVM_SPSR_IRQ;
		break;
	case COMPAT_PSR_MODE_FIQ:
		mode = KVM_SPSR_FIQ;
		break;
	default:
		BUG();
	}

	return (unsigned long *)&vcpu_gp_regs(vcpu)->spsr[mode];
}
