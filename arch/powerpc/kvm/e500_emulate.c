/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc. All rights reserved.
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
#include <asm/kvm_e500.h>

#include "booke.h"
#include "e500_tlb.h"

#define XOP_TLBIVAX 786
#define XOP_TLBSX   914
#define XOP_TLBRE   946
#define XOP_TLBWE   978

int kvmppc_core_emulate_op(struct kvm_run *run, struct kvm_vcpu *vcpu,
                           unsigned int inst, int *advance)
{
	int emulated = EMULATE_DONE;
	int ra;
	int rb;

	switch (get_op(inst)) {
	case 31:
		switch (get_xop(inst)) {

		case XOP_TLBRE:
			emulated = kvmppc_e500_emul_tlbre(vcpu);
			break;

		case XOP_TLBWE:
			emulated = kvmppc_e500_emul_tlbwe(vcpu);
			break;

		case XOP_TLBSX:
			rb = get_rb(inst);
			emulated = kvmppc_e500_emul_tlbsx(vcpu,rb);
			break;

		case XOP_TLBIVAX:
			ra = get_ra(inst);
			rb = get_rb(inst);
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

int kvmppc_core_emulate_mtspr(struct kvm_vcpu *vcpu, int sprn, int rs)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	int emulated = EMULATE_DONE;

	switch (sprn) {
	case SPRN_PID:
		vcpu_e500->pid[0] = vcpu->arch.shadow_pid =
			vcpu->arch.pid = vcpu->arch.gpr[rs];
		break;
	case SPRN_PID1:
		vcpu_e500->pid[1] = vcpu->arch.gpr[rs]; break;
	case SPRN_PID2:
		vcpu_e500->pid[2] = vcpu->arch.gpr[rs]; break;
	case SPRN_MAS0:
		vcpu_e500->mas0 = vcpu->arch.gpr[rs]; break;
	case SPRN_MAS1:
		vcpu_e500->mas1 = vcpu->arch.gpr[rs]; break;
	case SPRN_MAS2:
		vcpu_e500->mas2 = vcpu->arch.gpr[rs]; break;
	case SPRN_MAS3:
		vcpu_e500->mas3 = vcpu->arch.gpr[rs]; break;
	case SPRN_MAS4:
		vcpu_e500->mas4 = vcpu->arch.gpr[rs]; break;
	case SPRN_MAS6:
		vcpu_e500->mas6 = vcpu->arch.gpr[rs]; break;
	case SPRN_MAS7:
		vcpu_e500->mas7 = vcpu->arch.gpr[rs]; break;
	case SPRN_L1CSR1:
		vcpu_e500->l1csr1 = vcpu->arch.gpr[rs]; break;
	case SPRN_HID0:
		vcpu_e500->hid0 = vcpu->arch.gpr[rs]; break;
	case SPRN_HID1:
		vcpu_e500->hid1 = vcpu->arch.gpr[rs]; break;

	case SPRN_MMUCSR0:
		emulated = kvmppc_e500_emul_mt_mmucsr0(vcpu_e500,
				vcpu->arch.gpr[rs]);
		break;

	/* extra exceptions */
	case SPRN_IVOR32:
		vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_UNAVAIL] = vcpu->arch.gpr[rs];
		break;
	case SPRN_IVOR33:
		vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_FP_DATA] = vcpu->arch.gpr[rs];
		break;
	case SPRN_IVOR34:
		vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_FP_ROUND] = vcpu->arch.gpr[rs];
		break;
	case SPRN_IVOR35:
		vcpu->arch.ivor[BOOKE_IRQPRIO_PERFORMANCE_MONITOR] = vcpu->arch.gpr[rs];
		break;

	default:
		emulated = kvmppc_booke_emulate_mtspr(vcpu, sprn, rs);
	}

	return emulated;
}

int kvmppc_core_emulate_mfspr(struct kvm_vcpu *vcpu, int sprn, int rt)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	int emulated = EMULATE_DONE;

	switch (sprn) {
	case SPRN_PID:
		vcpu->arch.gpr[rt] = vcpu_e500->pid[0]; break;
	case SPRN_PID1:
		vcpu->arch.gpr[rt] = vcpu_e500->pid[1]; break;
	case SPRN_PID2:
		vcpu->arch.gpr[rt] = vcpu_e500->pid[2]; break;
	case SPRN_MAS0:
		vcpu->arch.gpr[rt] = vcpu_e500->mas0; break;
	case SPRN_MAS1:
		vcpu->arch.gpr[rt] = vcpu_e500->mas1; break;
	case SPRN_MAS2:
		vcpu->arch.gpr[rt] = vcpu_e500->mas2; break;
	case SPRN_MAS3:
		vcpu->arch.gpr[rt] = vcpu_e500->mas3; break;
	case SPRN_MAS4:
		vcpu->arch.gpr[rt] = vcpu_e500->mas4; break;
	case SPRN_MAS6:
		vcpu->arch.gpr[rt] = vcpu_e500->mas6; break;
	case SPRN_MAS7:
		vcpu->arch.gpr[rt] = vcpu_e500->mas7; break;

	case SPRN_TLB0CFG:
		vcpu->arch.gpr[rt] = mfspr(SPRN_TLB0CFG);
		vcpu->arch.gpr[rt] &= ~0xfffUL;
		vcpu->arch.gpr[rt] |= vcpu_e500->guest_tlb_size[0];
		break;

	case SPRN_TLB1CFG:
		vcpu->arch.gpr[rt] = mfspr(SPRN_TLB1CFG);
		vcpu->arch.gpr[rt] &= ~0xfffUL;
		vcpu->arch.gpr[rt] |= vcpu_e500->guest_tlb_size[1];
		break;

	case SPRN_L1CSR1:
		vcpu->arch.gpr[rt] = vcpu_e500->l1csr1; break;
	case SPRN_HID0:
		vcpu->arch.gpr[rt] = vcpu_e500->hid0; break;
	case SPRN_HID1:
		vcpu->arch.gpr[rt] = vcpu_e500->hid1; break;

	case SPRN_MMUCSR0:
		vcpu->arch.gpr[rt] = 0; break;

	case SPRN_MMUCFG:
		vcpu->arch.gpr[rt] = mfspr(SPRN_MMUCFG); break;

	/* extra exceptions */
	case SPRN_IVOR32:
		vcpu->arch.gpr[rt] = vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_UNAVAIL];
		break;
	case SPRN_IVOR33:
		vcpu->arch.gpr[rt] = vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_FP_DATA];
		break;
	case SPRN_IVOR34:
		vcpu->arch.gpr[rt] = vcpu->arch.ivor[BOOKE_IRQPRIO_SPE_FP_ROUND];
		break;
	case SPRN_IVOR35:
		vcpu->arch.gpr[rt] = vcpu->arch.ivor[BOOKE_IRQPRIO_PERFORMANCE_MONITOR];
		break;
	default:
		emulated = kvmppc_booke_emulate_mfspr(vcpu, sprn, rt);
	}

	return emulated;
}

