/*
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
 *
 * Copyright IBM Corp. 2008
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#include <asm/kvm_ppc.h>
#include <asm/dcr.h>
#include <asm/dcr-regs.h>
#include <asm/disassemble.h>

#include "booke.h"
#include "44x_tlb.h"

#define OP_RFI      19

#define XOP_RFI     50
#define XOP_MFMSR   83
#define XOP_WRTEE   131
#define XOP_MTMSR   146
#define XOP_WRTEEI  163
#define XOP_MFDCR   323
#define XOP_MTDCR   451
#define XOP_TLBSX   914
#define XOP_ICCCI   966
#define XOP_TLBWE   978

static inline void kvmppc_set_pid(struct kvm_vcpu *vcpu, u32 new_pid)
{
	if (vcpu->arch.pid != new_pid) {
		vcpu->arch.pid = new_pid;
		vcpu->arch.swap_pid = 1;
	}
}

static void kvmppc_emul_rfi(struct kvm_vcpu *vcpu)
{
	vcpu->arch.pc = vcpu->arch.srr0;
	kvmppc_set_msr(vcpu, vcpu->arch.srr1);
}

int kvmppc_core_emulate_op(struct kvm_run *run, struct kvm_vcpu *vcpu,
                           unsigned int inst, int *advance)
{
	int emulated = EMULATE_DONE;
	int dcrn;
	int ra;
	int rb;
	int rc;
	int rs;
	int rt;
	int ws;

	switch (get_op(inst)) {

	case OP_RFI:
		switch (get_xop(inst)) {
		case XOP_RFI:
			kvmppc_emul_rfi(vcpu);
			*advance = 0;
			break;

		default:
			emulated = EMULATE_FAIL;
			break;
		}
		break;

	case 31:
		switch (get_xop(inst)) {

		case XOP_MFMSR:
			rt = get_rt(inst);
			vcpu->arch.gpr[rt] = vcpu->arch.msr;
			break;

		case XOP_MTMSR:
			rs = get_rs(inst);
			kvmppc_set_msr(vcpu, vcpu->arch.gpr[rs]);
			break;

		case XOP_WRTEE:
			rs = get_rs(inst);
			vcpu->arch.msr = (vcpu->arch.msr & ~MSR_EE)
							 | (vcpu->arch.gpr[rs] & MSR_EE);
			break;

		case XOP_WRTEEI:
			vcpu->arch.msr = (vcpu->arch.msr & ~MSR_EE)
							 | (inst & MSR_EE);
			break;

		case XOP_MFDCR:
			dcrn = get_dcrn(inst);
			rt = get_rt(inst);

			/* The guest may access CPR0 registers to determine the timebase
			 * frequency, and it must know the real host frequency because it
			 * can directly access the timebase registers.
			 *
			 * It would be possible to emulate those accesses in userspace,
			 * but userspace can really only figure out the end frequency.
			 * We could decompose that into the factors that compute it, but
			 * that's tricky math, and it's easier to just report the real
			 * CPR0 values.
			 */
			switch (dcrn) {
			case DCRN_CPR0_CONFIG_ADDR:
				vcpu->arch.gpr[rt] = vcpu->arch.cpr0_cfgaddr;
				break;
			case DCRN_CPR0_CONFIG_DATA:
				local_irq_disable();
				mtdcr(DCRN_CPR0_CONFIG_ADDR,
					  vcpu->arch.cpr0_cfgaddr);
				vcpu->arch.gpr[rt] = mfdcr(DCRN_CPR0_CONFIG_DATA);
				local_irq_enable();
				break;
			default:
				run->dcr.dcrn = dcrn;
				run->dcr.data =  0;
				run->dcr.is_write = 0;
				vcpu->arch.io_gpr = rt;
				vcpu->arch.dcr_needed = 1;
				emulated = EMULATE_DO_DCR;
			}

			break;

		case XOP_MTDCR:
			dcrn = get_dcrn(inst);
			rs = get_rs(inst);

			/* emulate some access in kernel */
			switch (dcrn) {
			case DCRN_CPR0_CONFIG_ADDR:
				vcpu->arch.cpr0_cfgaddr = vcpu->arch.gpr[rs];
				break;
			default:
				run->dcr.dcrn = dcrn;
				run->dcr.data = vcpu->arch.gpr[rs];
				run->dcr.is_write = 1;
				vcpu->arch.dcr_needed = 1;
				emulated = EMULATE_DO_DCR;
			}

			break;

		case XOP_TLBWE:
			ra = get_ra(inst);
			rs = get_rs(inst);
			ws = get_ws(inst);
			emulated = kvmppc_44x_emul_tlbwe(vcpu, ra, rs, ws);
			break;

		case XOP_TLBSX:
			rt = get_rt(inst);
			ra = get_ra(inst);
			rb = get_rb(inst);
			rc = get_rc(inst);
			emulated = kvmppc_44x_emul_tlbsx(vcpu, rt, ra, rb, rc);
			break;

		case XOP_ICCCI:
			break;

		default:
			emulated = EMULATE_FAIL;
		}

		break;

	default:
		emulated = EMULATE_FAIL;
	}

	return emulated;
}

int kvmppc_core_emulate_mtspr(struct kvm_vcpu *vcpu, int sprn, int rs)
{
	switch (sprn) {
	case SPRN_MMUCR:
		vcpu->arch.mmucr = vcpu->arch.gpr[rs]; break;
	case SPRN_PID:
		kvmppc_set_pid(vcpu, vcpu->arch.gpr[rs]); break;
	case SPRN_CCR0:
		vcpu->arch.ccr0 = vcpu->arch.gpr[rs]; break;
	case SPRN_CCR1:
		vcpu->arch.ccr1 = vcpu->arch.gpr[rs]; break;
	case SPRN_DEAR:
		vcpu->arch.dear = vcpu->arch.gpr[rs]; break;
	case SPRN_ESR:
		vcpu->arch.esr = vcpu->arch.gpr[rs]; break;
	case SPRN_DBCR0:
		vcpu->arch.dbcr0 = vcpu->arch.gpr[rs]; break;
	case SPRN_DBCR1:
		vcpu->arch.dbcr1 = vcpu->arch.gpr[rs]; break;
	case SPRN_TSR:
		vcpu->arch.tsr &= ~vcpu->arch.gpr[rs]; break;
	case SPRN_TCR:
		vcpu->arch.tcr = vcpu->arch.gpr[rs];
		kvmppc_emulate_dec(vcpu);
		break;

	/* Note: SPRG4-7 are user-readable. These values are
	 * loaded into the real SPRGs when resuming the
	 * guest. */
	case SPRN_SPRG4:
		vcpu->arch.sprg4 = vcpu->arch.gpr[rs]; break;
	case SPRN_SPRG5:
		vcpu->arch.sprg5 = vcpu->arch.gpr[rs]; break;
	case SPRN_SPRG6:
		vcpu->arch.sprg6 = vcpu->arch.gpr[rs]; break;
	case SPRN_SPRG7:
		vcpu->arch.sprg7 = vcpu->arch.gpr[rs]; break;

	case SPRN_IVPR:
		vcpu->arch.ivpr = vcpu->arch.gpr[rs]; break;
	case SPRN_IVOR0:
		vcpu->arch.ivor[0] = vcpu->arch.gpr[rs]; break;
	case SPRN_IVOR1:
		vcpu->arch.ivor[1] = vcpu->arch.gpr[rs]; break;
	case SPRN_IVOR2:
		vcpu->arch.ivor[2] = vcpu->arch.gpr[rs]; break;
	case SPRN_IVOR3:
		vcpu->arch.ivor[3] = vcpu->arch.gpr[rs]; break;
	case SPRN_IVOR4:
		vcpu->arch.ivor[4] = vcpu->arch.gpr[rs]; break;
	case SPRN_IVOR5:
		vcpu->arch.ivor[5] = vcpu->arch.gpr[rs]; break;
	case SPRN_IVOR6:
		vcpu->arch.ivor[6] = vcpu->arch.gpr[rs]; break;
	case SPRN_IVOR7:
		vcpu->arch.ivor[7] = vcpu->arch.gpr[rs]; break;
	case SPRN_IVOR8:
		vcpu->arch.ivor[8] = vcpu->arch.gpr[rs]; break;
	case SPRN_IVOR9:
		vcpu->arch.ivor[9] = vcpu->arch.gpr[rs]; break;
	case SPRN_IVOR10:
		vcpu->arch.ivor[10] = vcpu->arch.gpr[rs]; break;
	case SPRN_IVOR11:
		vcpu->arch.ivor[11] = vcpu->arch.gpr[rs]; break;
	case SPRN_IVOR12:
		vcpu->arch.ivor[12] = vcpu->arch.gpr[rs]; break;
	case SPRN_IVOR13:
		vcpu->arch.ivor[13] = vcpu->arch.gpr[rs]; break;
	case SPRN_IVOR14:
		vcpu->arch.ivor[14] = vcpu->arch.gpr[rs]; break;
	case SPRN_IVOR15:
		vcpu->arch.ivor[15] = vcpu->arch.gpr[rs]; break;

	default:
		return EMULATE_FAIL;
	}

	return EMULATE_DONE;
}

int kvmppc_core_emulate_mfspr(struct kvm_vcpu *vcpu, int sprn, int rt)
{
	switch (sprn) {
	/* 440 */
	case SPRN_MMUCR:
		vcpu->arch.gpr[rt] = vcpu->arch.mmucr; break;
	case SPRN_CCR0:
		vcpu->arch.gpr[rt] = vcpu->arch.ccr0; break;
	case SPRN_CCR1:
		vcpu->arch.gpr[rt] = vcpu->arch.ccr1; break;

	/* Book E */
	case SPRN_PID:
		vcpu->arch.gpr[rt] = vcpu->arch.pid; break;
	case SPRN_IVPR:
		vcpu->arch.gpr[rt] = vcpu->arch.ivpr; break;
	case SPRN_DEAR:
		vcpu->arch.gpr[rt] = vcpu->arch.dear; break;
	case SPRN_ESR:
		vcpu->arch.gpr[rt] = vcpu->arch.esr; break;
	case SPRN_DBCR0:
		vcpu->arch.gpr[rt] = vcpu->arch.dbcr0; break;
	case SPRN_DBCR1:
		vcpu->arch.gpr[rt] = vcpu->arch.dbcr1; break;

	case SPRN_IVOR0:
		vcpu->arch.gpr[rt] = vcpu->arch.ivor[0]; break;
	case SPRN_IVOR1:
		vcpu->arch.gpr[rt] = vcpu->arch.ivor[1]; break;
	case SPRN_IVOR2:
		vcpu->arch.gpr[rt] = vcpu->arch.ivor[2]; break;
	case SPRN_IVOR3:
		vcpu->arch.gpr[rt] = vcpu->arch.ivor[3]; break;
	case SPRN_IVOR4:
		vcpu->arch.gpr[rt] = vcpu->arch.ivor[4]; break;
	case SPRN_IVOR5:
		vcpu->arch.gpr[rt] = vcpu->arch.ivor[5]; break;
	case SPRN_IVOR6:
		vcpu->arch.gpr[rt] = vcpu->arch.ivor[6]; break;
	case SPRN_IVOR7:
		vcpu->arch.gpr[rt] = vcpu->arch.ivor[7]; break;
	case SPRN_IVOR8:
		vcpu->arch.gpr[rt] = vcpu->arch.ivor[8]; break;
	case SPRN_IVOR9:
		vcpu->arch.gpr[rt] = vcpu->arch.ivor[9]; break;
	case SPRN_IVOR10:
		vcpu->arch.gpr[rt] = vcpu->arch.ivor[10]; break;
	case SPRN_IVOR11:
		vcpu->arch.gpr[rt] = vcpu->arch.ivor[11]; break;
	case SPRN_IVOR12:
		vcpu->arch.gpr[rt] = vcpu->arch.ivor[12]; break;
	case SPRN_IVOR13:
		vcpu->arch.gpr[rt] = vcpu->arch.ivor[13]; break;
	case SPRN_IVOR14:
		vcpu->arch.gpr[rt] = vcpu->arch.ivor[14]; break;
	case SPRN_IVOR15:
		vcpu->arch.gpr[rt] = vcpu->arch.ivor[15]; break;
	default:
		return EMULATE_FAIL;
	}

	return EMULATE_DONE;
}

