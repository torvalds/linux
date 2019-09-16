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

/*
 * This handles the cases where the guest is in real suspend mode
 * and we want to get back to the guest without dooming the transaction.
 * The caller has checked that the guest is in real-suspend mode
 * (MSR[TS] = S and the fake-suspend flag is not set).
 */
int kvmhv_p9_tm_emulation_early(struct kvm_vcpu *vcpu)
{
	u32 instr = vcpu->arch.emul_inst;
	u64 newmsr, msr, bescr;
	int rs;

	switch (instr & 0xfc0007ff) {
	case PPC_INST_RFID:
		/* XXX do we need to check for PR=0 here? */
		newmsr = vcpu->arch.shregs.srr1;
		/* should only get here for Sx -> T1 transition */
		if (!(MSR_TM_TRANSACTIONAL(newmsr) && (newmsr & MSR_TM)))
			return 0;
		newmsr = sanitize_msr(newmsr);
		vcpu->arch.shregs.msr = newmsr;
		vcpu->arch.cfar = vcpu->arch.regs.nip - 4;
		vcpu->arch.regs.nip = vcpu->arch.shregs.srr0;
		return 1;

	case PPC_INST_RFEBB:
		/* check for PR=1 and arch 2.06 bit set in PCR */
		msr = vcpu->arch.shregs.msr;
		if ((msr & MSR_PR) && (vcpu->arch.vcore->pcr & PCR_ARCH_206))
			return 0;
		/* check EBB facility is available */
		if (!(vcpu->arch.hfscr & HFSCR_EBB) ||
		    ((msr & MSR_PR) && !(mfspr(SPRN_FSCR) & FSCR_EBB)))
			return 0;
		bescr = mfspr(SPRN_BESCR);
		/* expect to see a S->T transition requested */
		if (((bescr >> 30) & 3) != 2)
			return 0;
		bescr &= ~BESCR_GE;
		if (instr & (1 << 11))
			bescr |= BESCR_GE;
		mtspr(SPRN_BESCR, bescr);
		msr = (msr & ~MSR_TS_MASK) | MSR_TS_T;
		vcpu->arch.shregs.msr = msr;
		vcpu->arch.cfar = vcpu->arch.regs.nip - 4;
		vcpu->arch.regs.nip = mfspr(SPRN_EBBRR);
		return 1;

	case PPC_INST_MTMSRD:
		/* XXX do we need to check for PR=0 here? */
		rs = (instr >> 21) & 0x1f;
		newmsr = kvmppc_get_gpr(vcpu, rs);
		msr = vcpu->arch.shregs.msr;
		/* check this is a Sx -> T1 transition */
		if (!(MSR_TM_TRANSACTIONAL(newmsr) && (newmsr & MSR_TM)))
			return 0;
		/* mtmsrd doesn't change LE */
		newmsr = (newmsr & ~MSR_LE) | (msr & MSR_LE);
		newmsr = sanitize_msr(newmsr);
		vcpu->arch.shregs.msr = newmsr;
		return 1;

	case PPC_INST_TSR:
		/* we know the MSR has the TS field = S (0b01) here */
		msr = vcpu->arch.shregs.msr;
		/* check for PR=1 and arch 2.06 bit set in PCR */
		if ((msr & MSR_PR) && (vcpu->arch.vcore->pcr & PCR_ARCH_206))
			return 0;
		/* check for TM disabled in the HFSCR or MSR */
		if (!(vcpu->arch.hfscr & HFSCR_TM) || !(msr & MSR_TM))
			return 0;
		/* L=1 => tresume => set TS to T (0b10) */
		if (instr & (1 << 21))
			vcpu->arch.shregs.msr = (msr & ~MSR_TS_MASK) | MSR_TS_T;
		/* Set CR0 to 0b0010 */
		vcpu->arch.regs.ccr = (vcpu->arch.regs.ccr & 0x0fffffff) |
			0x20000000;
		return 1;
	}

	return 0;
}

/*
 * This is called when we are returning to a guest in TM transactional
 * state.  We roll the guest state back to the checkpointed state.
 */
void kvmhv_emulate_tm_rollback(struct kvm_vcpu *vcpu)
{
	vcpu->arch.shregs.msr &= ~MSR_TS_MASK;	/* go to N state */
	vcpu->arch.regs.nip = vcpu->arch.tfhar;
	copy_from_checkpoint(vcpu);
	vcpu->arch.regs.ccr = (vcpu->arch.regs.ccr & 0x0fffffff) | 0xa0000000;
}
