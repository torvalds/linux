/*
 * Copyright (C) 2012-2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
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

#include <linux/compiler.h>
#include <linux/kvm_host.h>

#include <asm/kvm_mmu.h>

#include "hyp.h"

/* ctxt is already in the HYP VA space */
void __hyp_text __sysreg_save_state(struct kvm_cpu_context *ctxt)
{
	ctxt->sys_regs[MPIDR_EL1]	= read_sysreg(vmpidr_el2);
	ctxt->sys_regs[CSSELR_EL1]	= read_sysreg(csselr_el1);
	ctxt->sys_regs[SCTLR_EL1]	= read_sysreg(sctlr_el1);
	ctxt->sys_regs[ACTLR_EL1]	= read_sysreg(actlr_el1);
	ctxt->sys_regs[CPACR_EL1]	= read_sysreg(cpacr_el1);
	ctxt->sys_regs[TTBR0_EL1]	= read_sysreg(ttbr0_el1);
	ctxt->sys_regs[TTBR1_EL1]	= read_sysreg(ttbr1_el1);
	ctxt->sys_regs[TCR_EL1]		= read_sysreg(tcr_el1);
	ctxt->sys_regs[ESR_EL1]		= read_sysreg(esr_el1);
	ctxt->sys_regs[AFSR0_EL1]	= read_sysreg(afsr0_el1);
	ctxt->sys_regs[AFSR1_EL1]	= read_sysreg(afsr1_el1);
	ctxt->sys_regs[FAR_EL1]		= read_sysreg(far_el1);
	ctxt->sys_regs[MAIR_EL1]	= read_sysreg(mair_el1);
	ctxt->sys_regs[VBAR_EL1]	= read_sysreg(vbar_el1);
	ctxt->sys_regs[CONTEXTIDR_EL1]	= read_sysreg(contextidr_el1);
	ctxt->sys_regs[TPIDR_EL0]	= read_sysreg(tpidr_el0);
	ctxt->sys_regs[TPIDRRO_EL0]	= read_sysreg(tpidrro_el0);
	ctxt->sys_regs[TPIDR_EL1]	= read_sysreg(tpidr_el1);
	ctxt->sys_regs[AMAIR_EL1]	= read_sysreg(amair_el1);
	ctxt->sys_regs[CNTKCTL_EL1]	= read_sysreg(cntkctl_el1);
	ctxt->sys_regs[PAR_EL1]		= read_sysreg(par_el1);
	ctxt->sys_regs[MDSCR_EL1]	= read_sysreg(mdscr_el1);

	ctxt->gp_regs.regs.sp		= read_sysreg(sp_el0);
	ctxt->gp_regs.regs.pc		= read_sysreg(elr_el2);
	ctxt->gp_regs.regs.pstate	= read_sysreg(spsr_el2);
	ctxt->gp_regs.sp_el1		= read_sysreg(sp_el1);
	ctxt->gp_regs.elr_el1		= read_sysreg(elr_el1);
	ctxt->gp_regs.spsr[KVM_SPSR_EL1]= read_sysreg(spsr_el1);
}

void __hyp_text __sysreg_restore_state(struct kvm_cpu_context *ctxt)
{
	write_sysreg(ctxt->sys_regs[MPIDR_EL1],	  vmpidr_el2);
	write_sysreg(ctxt->sys_regs[CSSELR_EL1],  csselr_el1);
	write_sysreg(ctxt->sys_regs[SCTLR_EL1],	  sctlr_el1);
	write_sysreg(ctxt->sys_regs[ACTLR_EL1],	  actlr_el1);
	write_sysreg(ctxt->sys_regs[CPACR_EL1],	  cpacr_el1);
	write_sysreg(ctxt->sys_regs[TTBR0_EL1],	  ttbr0_el1);
	write_sysreg(ctxt->sys_regs[TTBR1_EL1],	  ttbr1_el1);
	write_sysreg(ctxt->sys_regs[TCR_EL1],	  tcr_el1);
	write_sysreg(ctxt->sys_regs[ESR_EL1],	  esr_el1);
	write_sysreg(ctxt->sys_regs[AFSR0_EL1],	  afsr0_el1);
	write_sysreg(ctxt->sys_regs[AFSR1_EL1],	  afsr1_el1);
	write_sysreg(ctxt->sys_regs[FAR_EL1],	  far_el1);
	write_sysreg(ctxt->sys_regs[MAIR_EL1],	  mair_el1);
	write_sysreg(ctxt->sys_regs[VBAR_EL1],	  vbar_el1);
	write_sysreg(ctxt->sys_regs[CONTEXTIDR_EL1], contextidr_el1);
	write_sysreg(ctxt->sys_regs[TPIDR_EL0],	  tpidr_el0);
	write_sysreg(ctxt->sys_regs[TPIDRRO_EL0], tpidrro_el0);
	write_sysreg(ctxt->sys_regs[TPIDR_EL1],	  tpidr_el1);
	write_sysreg(ctxt->sys_regs[AMAIR_EL1],	  amair_el1);
	write_sysreg(ctxt->sys_regs[CNTKCTL_EL1], cntkctl_el1);
	write_sysreg(ctxt->sys_regs[PAR_EL1],	  par_el1);
	write_sysreg(ctxt->sys_regs[MDSCR_EL1],	  mdscr_el1);

	write_sysreg(ctxt->gp_regs.regs.sp,	sp_el0);
	write_sysreg(ctxt->gp_regs.regs.pc,	elr_el2);
	write_sysreg(ctxt->gp_regs.regs.pstate,	spsr_el2);
	write_sysreg(ctxt->gp_regs.sp_el1,	sp_el1);
	write_sysreg(ctxt->gp_regs.elr_el1,	elr_el1);
	write_sysreg(ctxt->gp_regs.spsr[KVM_SPSR_EL1], spsr_el1);
}
