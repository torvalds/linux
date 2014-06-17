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
#include <asm/kvm_44x.h>
#include "timing.h"

#include "booke.h"
#include "44x_tlb.h"

#define XOP_MFDCRX  259
#define XOP_MFDCR   323
#define XOP_MTDCRX  387
#define XOP_MTDCR   451
#define XOP_TLBSX   914
#define XOP_ICCCI   966
#define XOP_TLBWE   978

static int emulate_mtdcr(struct kvm_vcpu *vcpu, int rs, int dcrn)
{
	/* emulate some access in kernel */
	switch (dcrn) {
	case DCRN_CPR0_CONFIG_ADDR:
		vcpu->arch.cpr0_cfgaddr = kvmppc_get_gpr(vcpu, rs);
		return EMULATE_DONE;
	default:
		vcpu->run->dcr.dcrn = dcrn;
		vcpu->run->dcr.data = kvmppc_get_gpr(vcpu, rs);
		vcpu->run->dcr.is_write = 1;
		vcpu->arch.dcr_is_write = 1;
		vcpu->arch.dcr_needed = 1;
		kvmppc_account_exit(vcpu, DCR_EXITS);
		return EMULATE_DO_DCR;
	}
}

static int emulate_mfdcr(struct kvm_vcpu *vcpu, int rt, int dcrn)
{
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
		kvmppc_set_gpr(vcpu, rt, vcpu->arch.cpr0_cfgaddr);
		break;
	case DCRN_CPR0_CONFIG_DATA:
		local_irq_disable();
		mtdcr(DCRN_CPR0_CONFIG_ADDR,
			  vcpu->arch.cpr0_cfgaddr);
		kvmppc_set_gpr(vcpu, rt,
			       mfdcr(DCRN_CPR0_CONFIG_DATA));
		local_irq_enable();
		break;
	default:
		vcpu->run->dcr.dcrn = dcrn;
		vcpu->run->dcr.data =  0;
		vcpu->run->dcr.is_write = 0;
		vcpu->arch.dcr_is_write = 0;
		vcpu->arch.io_gpr = rt;
		vcpu->arch.dcr_needed = 1;
		kvmppc_account_exit(vcpu, DCR_EXITS);
		return EMULATE_DO_DCR;
	}

	return EMULATE_DONE;
}

int kvmppc_core_emulate_op_44x(struct kvm_run *run, struct kvm_vcpu *vcpu,
			       unsigned int inst, int *advance)
{
	int emulated = EMULATE_DONE;
	int dcrn = get_dcrn(inst);
	int ra = get_ra(inst);
	int rb = get_rb(inst);
	int rc = get_rc(inst);
	int rs = get_rs(inst);
	int rt = get_rt(inst);
	int ws = get_ws(inst);

	switch (get_op(inst)) {
	case 31:
		switch (get_xop(inst)) {

		case XOP_MFDCR:
			emulated = emulate_mfdcr(vcpu, rt, dcrn);
			break;

		case XOP_MFDCRX:
			emulated = emulate_mfdcr(vcpu, rt,
					kvmppc_get_gpr(vcpu, ra));
			break;

		case XOP_MTDCR:
			emulated = emulate_mtdcr(vcpu, rs, dcrn);
			break;

		case XOP_MTDCRX:
			emulated = emulate_mtdcr(vcpu, rs,
					kvmppc_get_gpr(vcpu, ra));
			break;

		case XOP_TLBWE:
			emulated = kvmppc_44x_emul_tlbwe(vcpu, ra, rs, ws);
			break;

		case XOP_TLBSX:
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

	if (emulated == EMULATE_FAIL)
		emulated = kvmppc_booke_emulate_op(run, vcpu, inst, advance);

	return emulated;
}

int kvmppc_core_emulate_mtspr_44x(struct kvm_vcpu *vcpu, int sprn, ulong spr_val)
{
	int emulated = EMULATE_DONE;

	switch (sprn) {
	case SPRN_PID:
		kvmppc_set_pid(vcpu, spr_val); break;
	case SPRN_MMUCR:
		vcpu->arch.mmucr = spr_val; break;
	case SPRN_CCR0:
		vcpu->arch.ccr0 = spr_val; break;
	case SPRN_CCR1:
		vcpu->arch.ccr1 = spr_val; break;
	default:
		emulated = kvmppc_booke_emulate_mtspr(vcpu, sprn, spr_val);
	}

	return emulated;
}

int kvmppc_core_emulate_mfspr_44x(struct kvm_vcpu *vcpu, int sprn, ulong *spr_val)
{
	int emulated = EMULATE_DONE;

	switch (sprn) {
	case SPRN_PID:
		*spr_val = vcpu->arch.pid; break;
	case SPRN_MMUCR:
		*spr_val = vcpu->arch.mmucr; break;
	case SPRN_CCR0:
		*spr_val = vcpu->arch.ccr0; break;
	case SPRN_CCR1:
		*spr_val = vcpu->arch.ccr1; break;
	default:
		emulated = kvmppc_booke_emulate_mfspr(vcpu, sprn, spr_val);
	}

	return emulated;
}

