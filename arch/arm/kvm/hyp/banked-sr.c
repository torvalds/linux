/*
 * Original code:
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 *
 * Mostly rewritten in C by Marc Zyngier <marc.zyngier@arm.com>
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

#include <asm/kvm_hyp.h>

__asm__(".arch_extension     virt");

void __hyp_text __banked_save_state(struct kvm_cpu_context *ctxt)
{
	ctxt->gp_regs.usr_regs.ARM_sp	= read_special(SP_usr);
	ctxt->gp_regs.usr_regs.ARM_pc	= read_special(ELR_hyp);
	ctxt->gp_regs.usr_regs.ARM_cpsr	= read_special(SPSR);
	ctxt->gp_regs.KVM_ARM_SVC_sp	= read_special(SP_svc);
	ctxt->gp_regs.KVM_ARM_SVC_lr	= read_special(LR_svc);
	ctxt->gp_regs.KVM_ARM_SVC_spsr	= read_special(SPSR_svc);
	ctxt->gp_regs.KVM_ARM_ABT_sp	= read_special(SP_abt);
	ctxt->gp_regs.KVM_ARM_ABT_lr	= read_special(LR_abt);
	ctxt->gp_regs.KVM_ARM_ABT_spsr	= read_special(SPSR_abt);
	ctxt->gp_regs.KVM_ARM_UND_sp	= read_special(SP_und);
	ctxt->gp_regs.KVM_ARM_UND_lr	= read_special(LR_und);
	ctxt->gp_regs.KVM_ARM_UND_spsr	= read_special(SPSR_und);
	ctxt->gp_regs.KVM_ARM_IRQ_sp	= read_special(SP_irq);
	ctxt->gp_regs.KVM_ARM_IRQ_lr	= read_special(LR_irq);
	ctxt->gp_regs.KVM_ARM_IRQ_spsr	= read_special(SPSR_irq);
	ctxt->gp_regs.KVM_ARM_FIQ_r8	= read_special(R8_fiq);
	ctxt->gp_regs.KVM_ARM_FIQ_r9	= read_special(R9_fiq);
	ctxt->gp_regs.KVM_ARM_FIQ_r10	= read_special(R10_fiq);
	ctxt->gp_regs.KVM_ARM_FIQ_fp	= read_special(R11_fiq);
	ctxt->gp_regs.KVM_ARM_FIQ_ip	= read_special(R12_fiq);
	ctxt->gp_regs.KVM_ARM_FIQ_sp	= read_special(SP_fiq);
	ctxt->gp_regs.KVM_ARM_FIQ_lr	= read_special(LR_fiq);
	ctxt->gp_regs.KVM_ARM_FIQ_spsr	= read_special(SPSR_fiq);
}

void __hyp_text __banked_restore_state(struct kvm_cpu_context *ctxt)
{
	write_special(ctxt->gp_regs.usr_regs.ARM_sp,	SP_usr);
	write_special(ctxt->gp_regs.usr_regs.ARM_pc,	ELR_hyp);
	write_special(ctxt->gp_regs.usr_regs.ARM_cpsr,	SPSR_cxsf);
	write_special(ctxt->gp_regs.KVM_ARM_SVC_sp,	SP_svc);
	write_special(ctxt->gp_regs.KVM_ARM_SVC_lr,	LR_svc);
	write_special(ctxt->gp_regs.KVM_ARM_SVC_spsr,	SPSR_svc);
	write_special(ctxt->gp_regs.KVM_ARM_ABT_sp,	SP_abt);
	write_special(ctxt->gp_regs.KVM_ARM_ABT_lr,	LR_abt);
	write_special(ctxt->gp_regs.KVM_ARM_ABT_spsr,	SPSR_abt);
	write_special(ctxt->gp_regs.KVM_ARM_UND_sp,	SP_und);
	write_special(ctxt->gp_regs.KVM_ARM_UND_lr,	LR_und);
	write_special(ctxt->gp_regs.KVM_ARM_UND_spsr,	SPSR_und);
	write_special(ctxt->gp_regs.KVM_ARM_IRQ_sp,	SP_irq);
	write_special(ctxt->gp_regs.KVM_ARM_IRQ_lr,	LR_irq);
	write_special(ctxt->gp_regs.KVM_ARM_IRQ_spsr,	SPSR_irq);
	write_special(ctxt->gp_regs.KVM_ARM_FIQ_r8,	R8_fiq);
	write_special(ctxt->gp_regs.KVM_ARM_FIQ_r9,	R9_fiq);
	write_special(ctxt->gp_regs.KVM_ARM_FIQ_r10,	R10_fiq);
	write_special(ctxt->gp_regs.KVM_ARM_FIQ_fp,	R11_fiq);
	write_special(ctxt->gp_regs.KVM_ARM_FIQ_ip,	R12_fiq);
	write_special(ctxt->gp_regs.KVM_ARM_FIQ_sp,	SP_fiq);
	write_special(ctxt->gp_regs.KVM_ARM_FIQ_lr,	LR_fiq);
	write_special(ctxt->gp_regs.KVM_ARM_FIQ_spsr,	SPSR_fiq);
}
