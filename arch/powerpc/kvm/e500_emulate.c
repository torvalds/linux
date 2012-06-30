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

#define XOP_MSGSND  206
#define XOP_MSGCLR  238
#define XOP_TLBIVAX 786
#define XOP_TLBSX   914
#define XOP_TLBRE   946
#define XOP_TLBWE   978
#define XOP_TLBILX  18

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

int kvmppc_core_emulate_op(struct kvm_run *run, struct kvm_vcpu *vcpu,
                           unsigned int inst, int *advance)
{
	int emulated = EMULATE_DONE;
	int ra = get_ra(inst);
	int rb = get_rb(inst);
	int rt = get_rt(inst);

	switch (get_op(inst)) {
	case 31:
		switch (get_xop(inst)) {

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
			emulated = kvmppc_e500_emul_tlbsx(vcpu,rb);
			break;

		case XOP_TLBILX:
			emulated = kvmppc_e500_emul_tlbilx(vcpu, rt, ra, rb);
			break;

		case XOP_TLBIVAX:
			emulated = kvmppc_e500_emul_tlbivax(vcpu, ra, rb);
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

int kvmppc_core_emulate_mtspr(struct kvm_vcpu *vcpu, int sprn, ulong spr_val)
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

	/* extra exceptions */
	case SPRN_IVOR32:
		vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_UNAVAIL] = spr_val;
		break;
	case SPRN_IVOR33:
		vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_FP_DATA] = spr_val;
		break;
	case SPRN_IVOR34:
		vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_FP_ROUND] = spr_val;
		break;
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

int kvmppc_core_emulate_mfspr(struct kvm_vcpu *vcpu, int sprn, ulong *spr_val)
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
	case SPRN_TLB0CFG:
		*spr_val = vcpu->arch.tlbcfg[0];
		break;
	case SPRN_TLB1CFG:
		*spr_val = vcpu->arch.tlbcfg[1];
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

	/* extra exceptions */
	case SPRN_IVOR32:
		*spr_val = vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_UNAVAIL];
		break;
	case SPRN_IVOR33:
		*spr_val = vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_FP_DATA];
		break;
	case SPRN_IVOR34:
		*spr_val = vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_FP_ROUND];
		break;
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

