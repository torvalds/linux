// SPDX-License-Identifier: GPL-2.0-only
/*
 * Original code:
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 *
 * Mostly rewritten in C by Marc Zyngier <marc.zyngier@arm.com>
 */

#include <asm/kvm_hyp.h>

static u64 *cp15_64(struct kvm_cpu_context *ctxt, int idx)
{
	return (u64 *)(ctxt->cp15 + idx);
}

void __hyp_text __sysreg_save_state(struct kvm_cpu_context *ctxt)
{
	ctxt->cp15[c0_CSSELR]		= read_sysreg(CSSELR);
	ctxt->cp15[c1_SCTLR]		= read_sysreg(SCTLR);
	ctxt->cp15[c1_CPACR]		= read_sysreg(CPACR);
	*cp15_64(ctxt, c2_TTBR0)	= read_sysreg(TTBR0);
	*cp15_64(ctxt, c2_TTBR1)	= read_sysreg(TTBR1);
	ctxt->cp15[c2_TTBCR]		= read_sysreg(TTBCR);
	ctxt->cp15[c3_DACR]		= read_sysreg(DACR);
	ctxt->cp15[c5_DFSR]		= read_sysreg(DFSR);
	ctxt->cp15[c5_IFSR]		= read_sysreg(IFSR);
	ctxt->cp15[c5_ADFSR]		= read_sysreg(ADFSR);
	ctxt->cp15[c5_AIFSR]		= read_sysreg(AIFSR);
	ctxt->cp15[c6_DFAR]		= read_sysreg(DFAR);
	ctxt->cp15[c6_IFAR]		= read_sysreg(IFAR);
	*cp15_64(ctxt, c7_PAR)		= read_sysreg(PAR);
	ctxt->cp15[c10_PRRR]		= read_sysreg(PRRR);
	ctxt->cp15[c10_NMRR]		= read_sysreg(NMRR);
	ctxt->cp15[c10_AMAIR0]		= read_sysreg(AMAIR0);
	ctxt->cp15[c10_AMAIR1]		= read_sysreg(AMAIR1);
	ctxt->cp15[c12_VBAR]		= read_sysreg(VBAR);
	ctxt->cp15[c13_CID]		= read_sysreg(CID);
	ctxt->cp15[c13_TID_URW]		= read_sysreg(TID_URW);
	ctxt->cp15[c13_TID_URO]		= read_sysreg(TID_URO);
	ctxt->cp15[c13_TID_PRIV]	= read_sysreg(TID_PRIV);
	ctxt->cp15[c14_CNTKCTL]		= read_sysreg(CNTKCTL);
}

void __hyp_text __sysreg_restore_state(struct kvm_cpu_context *ctxt)
{
	write_sysreg(ctxt->cp15[c0_MPIDR],	VMPIDR);
	write_sysreg(ctxt->cp15[c0_CSSELR],	CSSELR);
	write_sysreg(ctxt->cp15[c1_SCTLR],	SCTLR);
	write_sysreg(ctxt->cp15[c1_CPACR],	CPACR);
	write_sysreg(*cp15_64(ctxt, c2_TTBR0),	TTBR0);
	write_sysreg(*cp15_64(ctxt, c2_TTBR1),	TTBR1);
	write_sysreg(ctxt->cp15[c2_TTBCR],	TTBCR);
	write_sysreg(ctxt->cp15[c3_DACR],	DACR);
	write_sysreg(ctxt->cp15[c5_DFSR],	DFSR);
	write_sysreg(ctxt->cp15[c5_IFSR],	IFSR);
	write_sysreg(ctxt->cp15[c5_ADFSR],	ADFSR);
	write_sysreg(ctxt->cp15[c5_AIFSR],	AIFSR);
	write_sysreg(ctxt->cp15[c6_DFAR],	DFAR);
	write_sysreg(ctxt->cp15[c6_IFAR],	IFAR);
	write_sysreg(*cp15_64(ctxt, c7_PAR),	PAR);
	write_sysreg(ctxt->cp15[c10_PRRR],	PRRR);
	write_sysreg(ctxt->cp15[c10_NMRR],	NMRR);
	write_sysreg(ctxt->cp15[c10_AMAIR0],	AMAIR0);
	write_sysreg(ctxt->cp15[c10_AMAIR1],	AMAIR1);
	write_sysreg(ctxt->cp15[c12_VBAR],	VBAR);
	write_sysreg(ctxt->cp15[c13_CID],	CID);
	write_sysreg(ctxt->cp15[c13_TID_URW],	TID_URW);
	write_sysreg(ctxt->cp15[c13_TID_URO],	TID_URO);
	write_sysreg(ctxt->cp15[c13_TID_PRIV],	TID_PRIV);
	write_sysreg(ctxt->cp15[c14_CNTKCTL],	CNTKCTL);
}
