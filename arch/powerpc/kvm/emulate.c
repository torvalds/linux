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
 * Copyright IBM Corp. 2007
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kvm_host.h>

#include <asm/reg.h>
#include <asm/time.h>
#include <asm/byteorder.h>
#include <asm/kvm_ppc.h>
#include <asm/disassemble.h>
#include "timing.h"

void kvmppc_emulate_dec(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.tcr & TCR_DIE) {
		/* The decrementer ticks at the same rate as the timebase, so
		 * that's how we convert the guest DEC value to the number of
		 * host ticks. */
		unsigned long nr_jiffies;

		nr_jiffies = vcpu->arch.dec / tb_ticks_per_jiffy;
		mod_timer(&vcpu->arch.dec_timer,
		          get_jiffies_64() + nr_jiffies);
	} else {
		del_timer(&vcpu->arch.dec_timer);
	}
}

/* XXX to do:
 * lhax
 * lhaux
 * lswx
 * lswi
 * stswx
 * stswi
 * lha
 * lhau
 * lmw
 * stmw
 *
 * XXX is_bigendian should depend on MMU mapping or MSR[LE]
 */
/* XXX Should probably auto-generate instruction decoding for a particular core
 * from opcode tables in the future. */
int kvmppc_emulate_instruction(struct kvm_run *run, struct kvm_vcpu *vcpu)
{
	u32 inst = vcpu->arch.last_inst;
	u32 ea;
	int ra;
	int rb;
	int rs;
	int rt;
	int sprn;
	enum emulation_result emulated = EMULATE_DONE;
	int advance = 1;

	/* this default type might be overwritten by subcategories */
	kvmppc_set_exit_type(vcpu, EMULATED_INST_EXITS);

	switch (get_op(inst)) {
	case 3:                                             /* trap */
		vcpu->arch.esr |= ESR_PTR;
		kvmppc_core_queue_program(vcpu);
		advance = 0;
		break;

	case 31:
		switch (get_xop(inst)) {

		case 23:                                        /* lwzx */
			rt = get_rt(inst);
			emulated = kvmppc_handle_load(run, vcpu, rt, 4, 1);
			break;

		case 87:                                        /* lbzx */
			rt = get_rt(inst);
			emulated = kvmppc_handle_load(run, vcpu, rt, 1, 1);
			break;

		case 151:                                       /* stwx */
			rs = get_rs(inst);
			emulated = kvmppc_handle_store(run, vcpu,
			                               vcpu->arch.gpr[rs],
			                               4, 1);
			break;

		case 215:                                       /* stbx */
			rs = get_rs(inst);
			emulated = kvmppc_handle_store(run, vcpu,
			                               vcpu->arch.gpr[rs],
			                               1, 1);
			break;

		case 247:                                       /* stbux */
			rs = get_rs(inst);
			ra = get_ra(inst);
			rb = get_rb(inst);

			ea = vcpu->arch.gpr[rb];
			if (ra)
				ea += vcpu->arch.gpr[ra];

			emulated = kvmppc_handle_store(run, vcpu,
			                               vcpu->arch.gpr[rs],
			                               1, 1);
			vcpu->arch.gpr[rs] = ea;
			break;

		case 279:                                       /* lhzx */
			rt = get_rt(inst);
			emulated = kvmppc_handle_load(run, vcpu, rt, 2, 1);
			break;

		case 311:                                       /* lhzux */
			rt = get_rt(inst);
			ra = get_ra(inst);
			rb = get_rb(inst);

			ea = vcpu->arch.gpr[rb];
			if (ra)
				ea += vcpu->arch.gpr[ra];

			emulated = kvmppc_handle_load(run, vcpu, rt, 2, 1);
			vcpu->arch.gpr[ra] = ea;
			break;

		case 339:                                       /* mfspr */
			sprn = get_sprn(inst);
			rt = get_rt(inst);

			switch (sprn) {
			case SPRN_SRR0:
				vcpu->arch.gpr[rt] = vcpu->arch.srr0; break;
			case SPRN_SRR1:
				vcpu->arch.gpr[rt] = vcpu->arch.srr1; break;
			case SPRN_PVR:
				vcpu->arch.gpr[rt] = vcpu->arch.pvr; break;

			/* Note: mftb and TBRL/TBWL are user-accessible, so
			 * the guest can always access the real TB anyways.
			 * In fact, we probably will never see these traps. */
			case SPRN_TBWL:
				vcpu->arch.gpr[rt] = mftbl(); break;
			case SPRN_TBWU:
				vcpu->arch.gpr[rt] = mftbu(); break;

			case SPRN_SPRG0:
				vcpu->arch.gpr[rt] = vcpu->arch.sprg0; break;
			case SPRN_SPRG1:
				vcpu->arch.gpr[rt] = vcpu->arch.sprg1; break;
			case SPRN_SPRG2:
				vcpu->arch.gpr[rt] = vcpu->arch.sprg2; break;
			case SPRN_SPRG3:
				vcpu->arch.gpr[rt] = vcpu->arch.sprg3; break;
			/* Note: SPRG4-7 are user-readable, so we don't get
			 * a trap. */

			default:
				emulated = kvmppc_core_emulate_mfspr(vcpu, sprn, rt);
				if (emulated == EMULATE_FAIL) {
					printk("mfspr: unknown spr %x\n", sprn);
					vcpu->arch.gpr[rt] = 0;
				}
				break;
			}
			break;

		case 407:                                       /* sthx */
			rs = get_rs(inst);
			ra = get_ra(inst);
			rb = get_rb(inst);

			emulated = kvmppc_handle_store(run, vcpu,
			                               vcpu->arch.gpr[rs],
			                               2, 1);
			break;

		case 439:                                       /* sthux */
			rs = get_rs(inst);
			ra = get_ra(inst);
			rb = get_rb(inst);

			ea = vcpu->arch.gpr[rb];
			if (ra)
				ea += vcpu->arch.gpr[ra];

			emulated = kvmppc_handle_store(run, vcpu,
			                               vcpu->arch.gpr[rs],
			                               2, 1);
			vcpu->arch.gpr[ra] = ea;
			break;

		case 467:                                       /* mtspr */
			sprn = get_sprn(inst);
			rs = get_rs(inst);
			switch (sprn) {
			case SPRN_SRR0:
				vcpu->arch.srr0 = vcpu->arch.gpr[rs]; break;
			case SPRN_SRR1:
				vcpu->arch.srr1 = vcpu->arch.gpr[rs]; break;

			/* XXX We need to context-switch the timebase for
			 * watchdog and FIT. */
			case SPRN_TBWL: break;
			case SPRN_TBWU: break;

			case SPRN_DEC:
				vcpu->arch.dec = vcpu->arch.gpr[rs];
				kvmppc_emulate_dec(vcpu);
				break;

			case SPRN_SPRG0:
				vcpu->arch.sprg0 = vcpu->arch.gpr[rs]; break;
			case SPRN_SPRG1:
				vcpu->arch.sprg1 = vcpu->arch.gpr[rs]; break;
			case SPRN_SPRG2:
				vcpu->arch.sprg2 = vcpu->arch.gpr[rs]; break;
			case SPRN_SPRG3:
				vcpu->arch.sprg3 = vcpu->arch.gpr[rs]; break;

			default:
				emulated = kvmppc_core_emulate_mtspr(vcpu, sprn, rs);
				if (emulated == EMULATE_FAIL)
					printk("mtspr: unknown spr %x\n", sprn);
				break;
			}
			break;

		case 470:                                       /* dcbi */
			/* Do nothing. The guest is performing dcbi because
			 * hardware DMA is not snooped by the dcache, but
			 * emulated DMA either goes through the dcache as
			 * normal writes, or the host kernel has handled dcache
			 * coherence. */
			break;

		case 534:                                       /* lwbrx */
			rt = get_rt(inst);
			emulated = kvmppc_handle_load(run, vcpu, rt, 4, 0);
			break;

		case 566:                                       /* tlbsync */
			break;

		case 662:                                       /* stwbrx */
			rs = get_rs(inst);
			ra = get_ra(inst);
			rb = get_rb(inst);

			emulated = kvmppc_handle_store(run, vcpu,
			                               vcpu->arch.gpr[rs],
			                               4, 0);
			break;

		case 790:                                       /* lhbrx */
			rt = get_rt(inst);
			emulated = kvmppc_handle_load(run, vcpu, rt, 2, 0);
			break;

		case 918:                                       /* sthbrx */
			rs = get_rs(inst);
			ra = get_ra(inst);
			rb = get_rb(inst);

			emulated = kvmppc_handle_store(run, vcpu,
			                               vcpu->arch.gpr[rs],
			                               2, 0);
			break;

		default:
			/* Attempt core-specific emulation below. */
			emulated = EMULATE_FAIL;
		}
		break;

	case 32:                                                /* lwz */
		rt = get_rt(inst);
		emulated = kvmppc_handle_load(run, vcpu, rt, 4, 1);
		break;

	case 33:                                                /* lwzu */
		ra = get_ra(inst);
		rt = get_rt(inst);
		emulated = kvmppc_handle_load(run, vcpu, rt, 4, 1);
		vcpu->arch.gpr[ra] = vcpu->arch.paddr_accessed;
		break;

	case 34:                                                /* lbz */
		rt = get_rt(inst);
		emulated = kvmppc_handle_load(run, vcpu, rt, 1, 1);
		break;

	case 35:                                                /* lbzu */
		ra = get_ra(inst);
		rt = get_rt(inst);
		emulated = kvmppc_handle_load(run, vcpu, rt, 1, 1);
		vcpu->arch.gpr[ra] = vcpu->arch.paddr_accessed;
		break;

	case 36:                                                /* stw */
		rs = get_rs(inst);
		emulated = kvmppc_handle_store(run, vcpu, vcpu->arch.gpr[rs],
		                               4, 1);
		break;

	case 37:                                                /* stwu */
		ra = get_ra(inst);
		rs = get_rs(inst);
		emulated = kvmppc_handle_store(run, vcpu, vcpu->arch.gpr[rs],
		                               4, 1);
		vcpu->arch.gpr[ra] = vcpu->arch.paddr_accessed;
		break;

	case 38:                                                /* stb */
		rs = get_rs(inst);
		emulated = kvmppc_handle_store(run, vcpu, vcpu->arch.gpr[rs],
		                               1, 1);
		break;

	case 39:                                                /* stbu */
		ra = get_ra(inst);
		rs = get_rs(inst);
		emulated = kvmppc_handle_store(run, vcpu, vcpu->arch.gpr[rs],
		                               1, 1);
		vcpu->arch.gpr[ra] = vcpu->arch.paddr_accessed;
		break;

	case 40:                                                /* lhz */
		rt = get_rt(inst);
		emulated = kvmppc_handle_load(run, vcpu, rt, 2, 1);
		break;

	case 41:                                                /* lhzu */
		ra = get_ra(inst);
		rt = get_rt(inst);
		emulated = kvmppc_handle_load(run, vcpu, rt, 2, 1);
		vcpu->arch.gpr[ra] = vcpu->arch.paddr_accessed;
		break;

	case 44:                                                /* sth */
		rs = get_rs(inst);
		emulated = kvmppc_handle_store(run, vcpu, vcpu->arch.gpr[rs],
		                               2, 1);
		break;

	case 45:                                                /* sthu */
		ra = get_ra(inst);
		rs = get_rs(inst);
		emulated = kvmppc_handle_store(run, vcpu, vcpu->arch.gpr[rs],
		                               2, 1);
		vcpu->arch.gpr[ra] = vcpu->arch.paddr_accessed;
		break;

	default:
		emulated = EMULATE_FAIL;
	}

	if (emulated == EMULATE_FAIL) {
		emulated = kvmppc_core_emulate_op(run, vcpu, inst, &advance);
		if (emulated == EMULATE_FAIL) {
			advance = 0;
			printk(KERN_ERR "Couldn't emulate instruction 0x%08x "
			       "(op %d xop %d)\n", inst, get_op(inst), get_xop(inst));
		}
	}

	KVMTRACE_3D(PPC_INSTR, vcpu, inst, (int)vcpu->arch.pc, emulated, entryexit);

	if (advance)
		vcpu->arch.pc += 4; /* Advance past emulated instruction. */

	return emulated;
}
