/*
 * Copyright 2017 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/kvm_host.h>

#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/kvm_book3s_64.h>
#include <asm/reg.h>
#include <asm/ppc-opcode.h>

static void emulate_tx_failure(struct kvm_vcpu *vcpu, u64 failure_cause)
{
	u64 texasr, tfiar;
	u64 msr = vcpu->arch.shregs.msr;

	tfiar = vcpu->arch.regs.nip & ~0x3ull;
	texasr = (failure_cause << 56) | TEXASR_ABORT | TEXASR_FS | TEXASR_EXACT;
	if (MSR_TM_SUSPENDED(vcpu->arch.shregs.msr))
		texasr |= TEXASR_SUSP;
	if (msr & MSR_PR) {
		texasr |= TEXASR_PR;
		tfiar |= 1;
	}
	vcpu->arch.tfiar = tfiar;
	/* Preserve ROT and TL fields of existing TEXASR */
	vcpu->arch.texasr = (vcpu->arch.texasr & 0x3ffffff) | texasr;
}

/*
 * This gets called on a softpatch interrupt on POWER9 DD2.2 processors.
 * We expect to find a TM-related instruction to be emulated.  The
 * instruction image is in vcpu->arch.emul_inst.  If the guest was in
 * TM suspended or transactional state, the checkpointed state has been
 * reclaimed and is in the vcpu struct.  The CPU is in virtual mode in
 * host context.
 */
int kvmhv_p9_tm_emulation(struct kvm_vcpu *vcpu)
{
	u32 instr = vcpu->arch.emul_inst;
	u64 msr = vcpu->arch.shregs.msr;
	u64 newmsr, bescr;
	int ra, rs;

	switch (instr & 0xfc0007ff) {
	case PPC_INST_RFID:
		/* XXX do we need to check for PR=0 here? */
		newmsr = vcpu->arch.shregs.srr1;
		/* should only get here for Sx -> T1 transition */
		WARN_ON_ONCE(!(MSR_TM_SUSPENDED(msr) &&
			       MSR_TM_TRANSACTIONAL(newmsr) &&
			       (newmsr & MSR_TM)));
		newmsr = sanitize_msr(newmsr);
		vcpu->arch.shregs.msr = newmsr;
		vcpu->arch.cfar = vcpu->arch.regs.nip - 4;
		vcpu->arch.regs.nip = vcpu->arch.shregs.srr0;
		return RESUME_GUEST;

	case PPC_INST_RFEBB:
		if ((msr & MSR_PR) && (vcpu->arch.vcore->pcr & PCR_ARCH_206)) {
			/* generate an illegal instruction interrupt */
			kvmppc_core_queue_program(vcpu, SRR1_PROGILL);
			return RESUME_GUEST;
		}
		/* check EBB facility is available */
		if (!(vcpu->arch.hfscr & HFSCR_EBB)) {
			/* generate an illegal instruction interrupt */
			kvmppc_core_queue_program(vcpu, SRR1_PROGILL);
			return RESUME_GUEST;
		}
		if ((msr & MSR_PR) && !(vcpu->arch.fscr & FSCR_EBB)) {
			/* generate a facility unavailable interrupt */
			vcpu->arch.fscr = (vcpu->arch.fscr & ~(0xffull << 56)) |
				((u64)FSCR_EBB_LG << 56);
			kvmppc_book3s_queue_irqprio(vcpu, BOOK3S_INTERRUPT_FAC_UNAVAIL);
			return RESUME_GUEST;
		}
		bescr = vcpu->arch.bescr;
		/* expect to see a S->T transition requested */
		WARN_ON_ONCE(!(MSR_TM_SUSPENDED(msr) &&
			       ((bescr >> 30) & 3) == 2));
		bescr &= ~BESCR_GE;
		if (instr & (1 << 11))
			bescr |= BESCR_GE;
		vcpu->arch.bescr = bescr;
		msr = (msr & ~MSR_TS_MASK) | MSR_TS_T;
		vcpu->arch.shregs.msr = msr;
		vcpu->arch.cfar = vcpu->arch.regs.nip - 4;
		vcpu->arch.regs.nip = vcpu->arch.ebbrr;
		return RESUME_GUEST;

	case PPC_INST_MTMSRD:
		/* XXX do we need to check for PR=0 here? */
		rs = (instr >> 21) & 0x1f;
		newmsr = kvmppc_get_gpr(vcpu, rs);
		/* check this is a Sx -> T1 transition */
		WARN_ON_ONCE(!(MSR_TM_SUSPENDED(msr) &&
			       MSR_TM_TRANSACTIONAL(newmsr) &&
			       (newmsr & MSR_TM)));
		/* mtmsrd doesn't change LE */
		newmsr = (newmsr & ~MSR_LE) | (msr & MSR_LE);
		newmsr = sanitize_msr(newmsr);
		vcpu->arch.shregs.msr = newmsr;
		return RESUME_GUEST;

	case PPC_INST_TSR:
		/* check for PR=1 and arch 2.06 bit set in PCR */
		if ((msr & MSR_PR) && (vcpu->arch.vcore->pcr & PCR_ARCH_206)) {
			/* generate an illegal instruction interrupt */
			kvmppc_core_queue_program(vcpu, SRR1_PROGILL);
			return RESUME_GUEST;
		}
		/* check for TM disabled in the HFSCR or MSR */
		if (!(vcpu->arch.hfscr & HFSCR_TM)) {
			/* generate an illegal instruction interrupt */
			kvmppc_core_queue_program(vcpu, SRR1_PROGILL);
			return RESUME_GUEST;
		}
		if (!(msr & MSR_TM)) {
			/* generate a facility unavailable interrupt */
			vcpu->arch.fscr = (vcpu->arch.fscr & ~(0xffull << 56)) |
				((u64)FSCR_TM_LG << 56);
			kvmppc_book3s_queue_irqprio(vcpu,
						BOOK3S_INTERRUPT_FAC_UNAVAIL);
			return RESUME_GUEST;
		}
		/* Set CR0 to indicate previous transactional state */
		vcpu->arch.regs.ccr = (vcpu->arch.regs.ccr & 0x0fffffff) |
			(((msr & MSR_TS_MASK) >> MSR_TS_S_LG) << 29);
		/* L=1 => tresume, L=0 => tsuspend */
		if (instr & (1 << 21)) {
			if (MSR_TM_SUSPENDED(msr))
				msr = (msr & ~MSR_TS_MASK) | MSR_TS_T;
		} else {
			if (MSR_TM_TRANSACTIONAL(msr))
				msr = (msr & ~MSR_TS_MASK) | MSR_TS_S;
		}
		vcpu->arch.shregs.msr = msr;
		return RESUME_GUEST;

	case PPC_INST_TRECLAIM:
		/* check for TM disabled in the HFSCR or MSR */
		if (!(vcpu->arch.hfscr & HFSCR_TM)) {
			/* generate an illegal instruction interrupt */
			kvmppc_core_queue_program(vcpu, SRR1_PROGILL);
			return RESUME_GUEST;
		}
		if (!(msr & MSR_TM)) {
			/* generate a facility unavailable interrupt */
			vcpu->arch.fscr = (vcpu->arch.fscr & ~(0xffull << 56)) |
				((u64)FSCR_TM_LG << 56);
			kvmppc_book3s_queue_irqprio(vcpu,
						BOOK3S_INTERRUPT_FAC_UNAVAIL);
			return RESUME_GUEST;
		}
		/* If no transaction active, generate TM bad thing */
		if (!MSR_TM_ACTIVE(msr)) {
			kvmppc_core_queue_program(vcpu, SRR1_PROGTM);
			return RESUME_GUEST;
		}
		/* If failure was not previously recorded, recompute TEXASR */
		if (!(vcpu->arch.orig_texasr & TEXASR_FS)) {
			ra = (instr >> 16) & 0x1f;
			if (ra)
				ra = kvmppc_get_gpr(vcpu, ra) & 0xff;
			emulate_tx_failure(vcpu, ra);
		}

		copy_from_checkpoint(vcpu);

		/* Set CR0 to indicate previous transactional state */
		vcpu->arch.regs.ccr = (vcpu->arch.regs.ccr & 0x0fffffff) |
			(((msr & MSR_TS_MASK) >> MSR_TS_S_LG) << 29);
		vcpu->arch.shregs.msr &= ~MSR_TS_MASK;
		return RESUME_GUEST;

	case PPC_INST_TRECHKPT:
		/* XXX do we need to check for PR=0 here? */
		/* check for TM disabled in the HFSCR or MSR */
		if (!(vcpu->arch.hfscr & HFSCR_TM)) {
			/* generate an illegal instruction interrupt */
			kvmppc_core_queue_program(vcpu, SRR1_PROGILL);
			return RESUME_GUEST;
		}
		if (!(msr & MSR_TM)) {
			/* generate a facility unavailable interrupt */
			vcpu->arch.fscr = (vcpu->arch.fscr & ~(0xffull << 56)) |
				((u64)FSCR_TM_LG << 56);
			kvmppc_book3s_queue_irqprio(vcpu,
						BOOK3S_INTERRUPT_FAC_UNAVAIL);
			return RESUME_GUEST;
		}
		/* If transaction active or TEXASR[FS] = 0, bad thing */
		if (MSR_TM_ACTIVE(msr) || !(vcpu->arch.texasr & TEXASR_FS)) {
			kvmppc_core_queue_program(vcpu, SRR1_PROGTM);
			return RESUME_GUEST;
		}

		copy_to_checkpoint(vcpu);

		/* Set CR0 to indicate previous transactional state */
		vcpu->arch.regs.ccr = (vcpu->arch.regs.ccr & 0x0fffffff) |
			(((msr & MSR_TS_MASK) >> MSR_TS_S_LG) << 29);
		vcpu->arch.shregs.msr = msr | MSR_TS_S;
		return RESUME_GUEST;
	}

	/* What should we do here? We didn't recognize the instruction */
	WARN_ON_ONCE(1);
	return RESUME_GUEST;
}
