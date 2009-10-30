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
#include "trace.h"

#define OP_TRAP 3

#define OP_31_XOP_LWZX      23
#define OP_31_XOP_LBZX      87
#define OP_31_XOP_STWX      151
#define OP_31_XOP_STBX      215
#define OP_31_XOP_STBUX     247
#define OP_31_XOP_LHZX      279
#define OP_31_XOP_LHZUX     311
#define OP_31_XOP_MFSPR     339
#define OP_31_XOP_STHX      407
#define OP_31_XOP_STHUX     439
#define OP_31_XOP_MTSPR     467
#define OP_31_XOP_DCBI      470
#define OP_31_XOP_LWBRX     534
#define OP_31_XOP_TLBSYNC   566
#define OP_31_XOP_STWBRX    662
#define OP_31_XOP_LHBRX     790
#define OP_31_XOP_STHBRX    918

#define OP_LWZ  32
#define OP_LWZU 33
#define OP_LBZ  34
#define OP_LBZU 35
#define OP_STW  36
#define OP_STWU 37
#define OP_STB  38
#define OP_STBU 39
#define OP_LHZ  40
#define OP_LHZU 41
#define OP_STH  44
#define OP_STHU 45

void kvmppc_emulate_dec(struct kvm_vcpu *vcpu)
{
	unsigned long nr_jiffies;

	if (vcpu->arch.tcr & TCR_DIE) {
		/* The decrementer ticks at the same rate as the timebase, so
		 * that's how we convert the guest DEC value to the number of
		 * host ticks. */

		vcpu->arch.dec_jiffies = mftb();
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
	case OP_TRAP:
		vcpu->arch.esr |= ESR_PTR;
		kvmppc_core_queue_program(vcpu);
		advance = 0;
		break;

	case 31:
		switch (get_xop(inst)) {

		case OP_31_XOP_LWZX:
			rt = get_rt(inst);
			emulated = kvmppc_handle_load(run, vcpu, rt, 4, 1);
			break;

		case OP_31_XOP_LBZX:
			rt = get_rt(inst);
			emulated = kvmppc_handle_load(run, vcpu, rt, 1, 1);
			break;

		case OP_31_XOP_STWX:
			rs = get_rs(inst);
			emulated = kvmppc_handle_store(run, vcpu,
			                               vcpu->arch.gpr[rs],
			                               4, 1);
			break;

		case OP_31_XOP_STBX:
			rs = get_rs(inst);
			emulated = kvmppc_handle_store(run, vcpu,
			                               vcpu->arch.gpr[rs],
			                               1, 1);
			break;

		case OP_31_XOP_STBUX:
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

		case OP_31_XOP_LHZX:
			rt = get_rt(inst);
			emulated = kvmppc_handle_load(run, vcpu, rt, 2, 1);
			break;

		case OP_31_XOP_LHZUX:
			rt = get_rt(inst);
			ra = get_ra(inst);
			rb = get_rb(inst);

			ea = vcpu->arch.gpr[rb];
			if (ra)
				ea += vcpu->arch.gpr[ra];

			emulated = kvmppc_handle_load(run, vcpu, rt, 2, 1);
			vcpu->arch.gpr[ra] = ea;
			break;

		case OP_31_XOP_MFSPR:
			sprn = get_sprn(inst);
			rt = get_rt(inst);

			switch (sprn) {
			case SPRN_SRR0:
				vcpu->arch.gpr[rt] = vcpu->arch.srr0; break;
			case SPRN_SRR1:
				vcpu->arch.gpr[rt] = vcpu->arch.srr1; break;
			case SPRN_PVR:
				vcpu->arch.gpr[rt] = mfspr(SPRN_PVR); break;
			case SPRN_PIR:
				vcpu->arch.gpr[rt] = mfspr(SPRN_PIR); break;

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

			case SPRN_DEC:
			{
				u64 jd = mftb() - vcpu->arch.dec_jiffies;
				vcpu->arch.gpr[rt] = vcpu->arch.dec - jd;
#ifdef DEBUG_EMUL
				printk(KERN_INFO "mfDEC: %x - %llx = %lx\n", vcpu->arch.dec, jd, vcpu->arch.gpr[rt]);
#endif
				break;
			}
			default:
				emulated = kvmppc_core_emulate_mfspr(vcpu, sprn, rt);
				if (emulated == EMULATE_FAIL) {
					printk("mfspr: unknown spr %x\n", sprn);
					vcpu->arch.gpr[rt] = 0;
				}
				break;
			}
			break;

		case OP_31_XOP_STHX:
			rs = get_rs(inst);
			ra = get_ra(inst);
			rb = get_rb(inst);

			emulated = kvmppc_handle_store(run, vcpu,
			                               vcpu->arch.gpr[rs],
			                               2, 1);
			break;

		case OP_31_XOP_STHUX:
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

		case OP_31_XOP_MTSPR:
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

		case OP_31_XOP_DCBI:
			/* Do nothing. The guest is performing dcbi because
			 * hardware DMA is not snooped by the dcache, but
			 * emulated DMA either goes through the dcache as
			 * normal writes, or the host kernel has handled dcache
			 * coherence. */
			break;

		case OP_31_XOP_LWBRX:
			rt = get_rt(inst);
			emulated = kvmppc_handle_load(run, vcpu, rt, 4, 0);
			break;

		case OP_31_XOP_TLBSYNC:
			break;

		case OP_31_XOP_STWBRX:
			rs = get_rs(inst);
			ra = get_ra(inst);
			rb = get_rb(inst);

			emulated = kvmppc_handle_store(run, vcpu,
			                               vcpu->arch.gpr[rs],
			                               4, 0);
			break;

		case OP_31_XOP_LHBRX:
			rt = get_rt(inst);
			emulated = kvmppc_handle_load(run, vcpu, rt, 2, 0);
			break;

		case OP_31_XOP_STHBRX:
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

	case OP_LWZ:
		rt = get_rt(inst);
		emulated = kvmppc_handle_load(run, vcpu, rt, 4, 1);
		break;

	case OP_LWZU:
		ra = get_ra(inst);
		rt = get_rt(inst);
		emulated = kvmppc_handle_load(run, vcpu, rt, 4, 1);
		vcpu->arch.gpr[ra] = vcpu->arch.paddr_accessed;
		break;

	case OP_LBZ:
		rt = get_rt(inst);
		emulated = kvmppc_handle_load(run, vcpu, rt, 1, 1);
		break;

	case OP_LBZU:
		ra = get_ra(inst);
		rt = get_rt(inst);
		emulated = kvmppc_handle_load(run, vcpu, rt, 1, 1);
		vcpu->arch.gpr[ra] = vcpu->arch.paddr_accessed;
		break;

	case OP_STW:
		rs = get_rs(inst);
		emulated = kvmppc_handle_store(run, vcpu, vcpu->arch.gpr[rs],
		                               4, 1);
		break;

	case OP_STWU:
		ra = get_ra(inst);
		rs = get_rs(inst);
		emulated = kvmppc_handle_store(run, vcpu, vcpu->arch.gpr[rs],
		                               4, 1);
		vcpu->arch.gpr[ra] = vcpu->arch.paddr_accessed;
		break;

	case OP_STB:
		rs = get_rs(inst);
		emulated = kvmppc_handle_store(run, vcpu, vcpu->arch.gpr[rs],
		                               1, 1);
		break;

	case OP_STBU:
		ra = get_ra(inst);
		rs = get_rs(inst);
		emulated = kvmppc_handle_store(run, vcpu, vcpu->arch.gpr[rs],
		                               1, 1);
		vcpu->arch.gpr[ra] = vcpu->arch.paddr_accessed;
		break;

	case OP_LHZ:
		rt = get_rt(inst);
		emulated = kvmppc_handle_load(run, vcpu, rt, 2, 1);
		break;

	case OP_LHZU:
		ra = get_ra(inst);
		rt = get_rt(inst);
		emulated = kvmppc_handle_load(run, vcpu, rt, 2, 1);
		vcpu->arch.gpr[ra] = vcpu->arch.paddr_accessed;
		break;

	case OP_STH:
		rs = get_rs(inst);
		emulated = kvmppc_handle_store(run, vcpu, vcpu->arch.gpr[rs],
		                               2, 1);
		break;

	case OP_STHU:
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

	trace_kvm_ppc_instr(inst, vcpu->arch.pc, emulated);

	if (advance)
		vcpu->arch.pc += 4; /* Advance past emulated instruction. */

	return emulated;
}
