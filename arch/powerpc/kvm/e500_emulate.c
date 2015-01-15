/*
 * Copyright (C) 2008-2011 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Yu Liu, <yu.liu@freescale.com>
 *
 * Description:
 * This file is derived from arch/powerpc/kvm/44x_emulate.c,
 * by Hollis Blanchard <hollisb@us.ibm.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#include <asm/kvm_ppc.h>
#include <asm/disassemble.h>
#include <asm/dbell.h>

#include "booke.h"
#include "e500.h"

#define XOP_DCBTLS  166
#define XOP_MSGSND  206
#define XOP_MSGCLR  238
#define XOP_TLBIVAX 786
#define XOP_TLBSX   914
#define XOP_TLBRE   946
#define XOP_TLBWE   978
#define XOP_TLBILX  18
#define XOP_EHPRIV  270

#ifdef CONFIG_KVM_E500MC
static int dbell2prio(ulong param)
{
	int msg = param & PPC_DBELL_TYPE_MASK;
	int prio = -1;

	switch (msg) {
	case PPC_DBELL_TYPE(PPC_DBELL):
		prio = BOOKE_IRQPRIO_DBELL;
		break;
	case PPC_DBELL_TYPE(PPC_DBELL_CRIT):
		prio = BOOKE_IRQPRIO_DBELL_CRIT;
		break;
	default:
		break;
	}

	return prio;
}

static int kvmppc_e500_emul_msgclr(struct kvm_vcpu *vcpu, int rb)
{
	ulong param = vcpu->arch.gpr[rb];
	int prio = dbell2prio(param);

	if (prio < 0)
		return EMULATE_FAIL;

	clear_bit(prio, &vcpu->arch.pending_exceptions);
	return EMULATE_DONE;
}

static int kvmppc_e500_emul_msgsnd(struct kvm_vcpu *vcpu, int rb)
{
	ulong param = vcpu->arch.gpr[rb];
	int prio = dbell2prio(rb);
	int pir = param & PPC_DBELL_PIR_MASK;
	int i;
	struct kvm_vcpu *cvcpu;

	if (prio < 0)
		return EMULATE_FAIL;

	kvm_for_each_vcpu(i, cvcpu, vcpu->kvm) {
		int cpir = cvcpu->arch.shared->pir;
		if ((param & PPC_DBELL_MSG_BRDCAST) || (cpir == pir)) {
			set_bit(prio, &cvcpu->arch.pending_exceptions);
			kvm_vcpu_kick(cvcpu);
		}
	}

	return EMULATE_DONE;
}
#endif

static int kvmppc_e500_emul_ehpriv(struct kvm_run *run, struct kvm_vcpu *vcpu,
				   unsigned int inst, int *advance)
{
	int emulated = EMULATE_DONE;

	switch (get_oc(inst)) {
	case EHPRIV_OC_DEBUG:
		run->exit_reason = KVM_EXIT_DEBUG;
		run->debug.arch.address = vcpu->arch.pc;
		run->debug.arch.status = 0;
		kvmppc_account_exit(vcpu, DEBUG_EXITS);
		emulated = EMULATE_EXIT_USER;
		*advance = 0;
		break;
	default:
		emulated = EMULATE_FAIL;
	}
	return emulated;
}

static int kvmppc_e500_emul_dcbtls(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);

	/* Always fail to lock the cache */
	vcpu_e500->l1csr0 |= L1CSR0_CUL;
	return EMULATE_DONE;
}

int kvmppc_core_emulate_op_e500(struct kvm_run *run, struct kvm_vcpu *vcpu,
				unsigned int inst, int *advance)
{
	int emulated = EMULATE_DONE;
	int ra = get_ra(inst);
	int rb = get_rb(inst);
	int rt = get_rt(inst);
	gva_t ea;

	switch (get_op(inst)) {
	case 31:
		switch (get_xop(inst)) {

		case XOP_DCBTLS:
			emulated = kvmppc_e500_emul_dcbtls(vcpu);
			break;

#ifdef CONFIG_KVM_E500MC
		case XOP_MSGSND:
			emulated = kvmppc_e500_emul_msgsnd(vcpu, rb);
			break;

		case XOP_MSGCLR:
			emulated = kvmppc_e500_emul_msgclr(vcpu, rb);
			break;
#endif

		case XOP_TLBRE:
			emulated = kvmppc_e500_emul_tlbre(vcpu);
			break;

		case XOP_TLBWE:
			emulated = kvmppc_e500_emul_tlbwe(vcpu);
			break;

		case XOP_TLBSX:
			ea = kvmppc_get_ea_indexed(vcpu, ra, rb);
			emulated = kvmppc_e500_emul_tlbsx(vcpu, ea);
			break;

		case XOP_TLBILX: {
			int type = rt & 0x3;
			ea = kvmppc_get_ea_indexed(vcpu, ra, rb);
			emulated = kvmppc_e500_emul_tlbilx(vcpu, type, ea);
			break;
		}

		case XOP_TLBIVAX:
			ea = kvmppc_get_ea_indexed(vcpu, ra, rb);
			emulated = kvmppc_e500_emul_tlbivax(vcpu, ea);
			break;

		case XOP_EHPRIV:
			emulated = kvmppc_e500_emul_ehpriv(run, vcpu, inst,
							   advance);
			break;

		default:
			emulated = EMULATE_FAIL;
		}

		break;

	default:
		emulated = EMULATE_FAIL;
	}

	if (emulated == EMULATE_FAIL)
		emulated = kvmppc_booke_emulate_op(run, vcpu, inst, advance);

	return emulated;
}

int kvmppc_core_emulate_mtspr_e500(struct kvm_vcpu *vcpu, int sprn, ulong spr_val)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	int emulated = EMULATE_DONE;

	switch (sprn) {
#ifndef CONFIG_KVM_BOOKE_HV
	case SPRN_PID:
		kvmppc_set_pid(vcpu, spr_val);
		break;
	case SPRN_PID1:
		if (spr_val != 0)
			return EMULATE_FAIL;
		vcpu_e500->pid[1] = spr_val;
		break;
	case SPRN_PID2:
		if (spr_val != 0)
			return EMULATE_FAIL;
		vcpu_e500->pid[2] = spr_val;
		break;
	case SPRN_MAS0:
		vcpu->arch.shared->mas0 = spr_val;
		break;
	case SPRN_MAS1:
		vcpu->arch.shared->mas1 = spr_val;
		break;
	case SPRN_MAS2:
		vcpu->arch.shared->mas2 = spr_val;
		break;
	case SPRN_MAS3:
		vcpu->arch.shared->mas7_3 &= ~(u64)0xffffffff;
		vcpu->arch.shared->mas7_3 |= spr_val;
		break;
	case SPRN_MAS4:
		vcpu->arch.shared->mas4 = spr_val;
		break;
	case SPRN_MAS6:
		vcpu->arch.shared->mas6 = spr_val;
		break;
	case SPRN_MAS7:
		vcpu->arch.shared->mas7_3 &= (u64)0xffffffff;
		vcpu->arch.shared->mas7_3 |= (u64)spr_val << 32;
		break;
#endif
	case SPRN_L1CSR0:
		vcpu_e500->l1csr0 = spr_val;
		vcpu_e500->l1csr0 &= ~(L1CSR0_DCFI | L1CSR0_CLFC);
		break;
	case SPRN_L1CSR1:
		vcpu_e500->l1csr1 = spr_val;
		vcpu_e500->l1csr1 &= ~(L1CSR1_ICFI | L1CSR1_ICLFR);
		break;
	case SPRN_HID0:
		vcpu_e500->hid0 = spr_val;
		break;
	case SPRN_HID1:
		vcpu_e500->hid1 = spr_val;
		break;

	case SPRN_MMUCSR0:
		emulated = kvmppc_e500_emul_mt_mmucsr0(vcpu_e500,
				spr_val);
		break;

	case SPRN_PWRMGTCR0:
		/*
		 * Guest relies on host power management configurations
		 * Treat the request as a general store
		 */
		vcpu->arch.pwrmgtcr0 = spr_val;
		break;

	/* extra exceptions */
#ifdef CONFIG_SPE_POSSIBLE
	case SPRN_IVOR32:
		vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_UNAVAIL] = spr_val;
		break;
	case SPRN_IVOR33:
		vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_FP_DATA] = spr_val;
		break;
	case SPRN_IVOR34:
		vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_FP_ROUND] = spr_val;
		break;
#endif
#ifdef CONFIG_ALTIVEC
	case SPRN_IVOR32:
		vcpu->arch.ivor[BOOKE_IRQPRIO_ALTIVEC_UNAVAIL] = spr_val;
		break;
	case SPRN_IVOR33:
		vcpu->arch.ivor[BOOKE_IRQPRIO_ALTIVEC_ASSIST] = spr_val;
		break;
#endif
	case SPRN_IVOR35:
		vcpu->arch.ivor[BOOKE_IRQPRIO_PERFORMANCE_MONITOR] = spr_val;
		break;
#ifdef CONFIG_KVM_BOOKE_HV
	case SPRN_IVOR36:
		vcpu->arch.ivor[BOOKE_IRQPRIO_DBELL] = spr_val;
		break;
	case SPRN_IVOR37:
		vcpu->arch.ivor[BOOKE_IRQPRIO_DBELL_CRIT] = spr_val;
		break;
#endif
	default:
		emulated = kvmppc_booke_emulate_mtspr(vcpu, sprn, spr_val);
	}

	return emulated;
}

int kvmppc_core_emulate_mfspr_e500(struct kvm_vcpu *vcpu, int sprn, ulong *spr_val)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	int emulated = EMULATE_DONE;

	switch (sprn) {
#ifndef CONFIG_KVM_BOOKE_HV
	case SPRN_PID:
		*spr_val = vcpu_e500->pid[0];
		break;
	case SPRN_PID1:
		*spr_val = vcpu_e500->pid[1];
		break;
	case SPRN_PID2:
		*spr_val = vcpu_e500->pid[2];
		break;
	case SPRN_MAS0:
		*spr_val = vcpu->arch.shared->mas0;
		break;
	case SPRN_MAS1:
		*spr_val = vcpu->arch.shared->mas1;
		break;
	case SPRN_MAS2:
		*spr_val = vcpu->arch.shared->mas2;
		break;
	case SPRN_MAS3:
		*spr_val = (u32)vcpu->arch.shared->mas7_3;
		break;
	case SPRN_MAS4:
		*spr_val = vcpu->arch.shared->mas4;
		break;
	case SPRN_MAS6:
		*spr_val = vcpu->arch.shared->mas6;
		break;
	case SPRN_MAS7:
		*spr_val = vcpu->arch.shared->mas7_3 >> 32;
		break;
#endif
	case SPRN_DECAR:
		*spr_val = vcpu->arch.decar;
		break;
	case SPRN_TLB0CFG:
		*spr_val = vcpu->arch.tlbcfg[0];
		break;
	case SPRN_TLB1CFG:
		*spr_val = vcpu->arch.tlbcfg[1];
		break;
	case SPRN_TLB0PS:
		if (!has_feature(vcpu, VCPU_FTR_MMU_V2))
			return EMULATE_FAIL;
		*spr_val = vcpu->arch.tlbps[0];
		break;
	case SPRN_TLB1PS:
		if (!has_feature(vcpu, VCPU_FTR_MMU_V2))
			return EMULATE_FAIL;
		*spr_val = vcpu->arch.tlbps[1];
		break;
	case SPRN_L1CSR0:
		*spr_val = vcpu_e500->l1csr0;
		break;
	case SPRN_L1CSR1:
		*spr_val = vcpu_e500->l1csr1;
		break;
	case SPRN_HID0:
		*spr_val = vcpu_e500->hid0;
		break;
	case SPRN_HID1:
		*spr_val = vcpu_e500->hid1;
		break;
	case SPRN_SVR:
		*spr_val = vcpu_e500->svr;
		break;

	case SPRN_MMUCSR0:
		*spr_val = 0;
		break;

	case SPRN_MMUCFG:
		*spr_val = vcpu->arch.mmucfg;
		break;
	case SPRN_EPTCFG:
		if (!has_feature(vcpu, VCPU_FTR_MMU_V2))
			return EMULATE_FAIL;
		/*
		 * Legacy Linux guests access EPTCFG register even if the E.PT
		 * category is disabled in the VM. Give them a chance to live.
		 */
		*spr_val = vcpu->arch.eptcfg;
		break;

	case SPRN_PWRMGTCR0:
		*spr_val = vcpu->arch.pwrmgtcr0;
		break;

	/* extra exceptions */
#ifdef CONFIG_SPE_POSSIBLE
	case SPRN_IVOR32:
		*spr_val = vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_UNAVAIL];
		break;
	case SPRN_IVOR33:
		*spr_val = vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_FP_DATA];
		break;
	case SPRN_IVOR34:
		*spr_val = vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_FP_ROUND];
		break;
#endif
#ifdef CONFIG_ALTIVEC
	case SPRN_IVOR32:
		*spr_val = vcpu->arch.ivor[BOOKE_IRQPRIO_ALTIVEC_UNAVAIL];
		break;
	case SPRN_IVOR33:
		*spr_val = vcpu->arch.ivor[BOOKE_IRQPRIO_ALTIVEC_ASSIST];
		break;
#endif
	case SPRN_IVOR35:
		*spr_val = vcpu->arch.ivor[BOOKE_IRQPRIO_PERFORMANCE_MONITOR];
		break;
#ifdef CONFIG_KVM_BOOKE_HV
	case SPRN_IVOR36:
		*spr_val = vcpu->arch.ivor[BOOKE_IRQPRIO_DBELL];
		break;
	case SPRN_IVOR37:
		*spr_val = vcpu->arch.ivor[BOOKE_IRQPRIO_DBELL_CRIT];
		break;
#endif
	default:
		emulated = kvmppc_booke_emulate_mfspr(vcpu, sprn, spr_val);
	}

	return emulated;
}

