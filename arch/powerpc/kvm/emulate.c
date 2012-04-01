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
 * Copyright 2011 Freescale Semiconductor, Inc.
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#include <linux/jiffies.h>
#include <linux/hrtimer.h>
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
#define OP_TRAP_64 2

#define OP_31_XOP_LWZX      23
#define OP_31_XOP_LBZX      87
#define OP_31_XOP_STWX      151
#define OP_31_XOP_STBX      215
#define OP_31_XOP_LBZUX     119
#define OP_31_XOP_STBUX     247
#define OP_31_XOP_LHZX      279
#define OP_31_XOP_LHZUX     311
#define OP_31_XOP_MFSPR     339
#define OP_31_XOP_LHAX      343
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
#define OP_LHA  42
#define OP_LHAU 43
#define OP_STH  44
#define OP_STHU 45

void kvmppc_emulate_dec(struct kvm_vcpu *vcpu)
{
	unsigned long dec_nsec;
	unsigned long long dec_time;

	pr_debug("mtDEC: %x\n", vcpu->arch.dec);
	hrtimer_try_to_cancel(&vcpu->arch.dec_timer);

#ifdef CONFIG_PPC_BOOK3S
	/* mtdec lowers the interrupt line when positive. */
	kvmppc_core_dequeue_dec(vcpu);

	/* POWER4+ triggers a dec interrupt if the value is < 0 */
	if (vcpu->arch.dec & 0x80000000) {
		kvmppc_core_queue_dec(vcpu);
		return;
	}
#endif

#ifdef CONFIG_BOOKE
	/* On BOOKE, DEC = 0 is as good as decrementer not enabled */
	if (vcpu->arch.dec == 0)
		return;
#endif

	/*
	 * The decrementer ticks at the same rate as the timebase, so
	 * that's how we convert the guest DEC value to the number of
	 * host ticks.
	 */

	dec_time = vcpu->arch.dec;
	dec_time *= 1000;
	do_div(dec_time, tb_ticks_per_usec);
	dec_nsec = do_div(dec_time, NSEC_PER_SEC);
	hrtimer_start(&vcpu->arch.dec_timer,
		ktime_set(dec_time, dec_nsec), HRTIMER_MODE_REL);
	vcpu->arch.dec_jiffies = get_tb();
}

u32 kvmppc_get_dec(struct kvm_vcpu *vcpu, u64 tb)
{
	u64 jd = tb - vcpu->arch.dec_jiffies;

#ifdef CONFIG_BOOKE
	if (vcpu->arch.dec < jd)
		return 0;
#endif

	return vcpu->arch.dec - jd;
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
	u32 inst = kvmppc_get_last_inst(vcpu);
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

	pr_debug("Emulating opcode %d / %d\n", get_op(inst), get_xop(inst));

	switch (get_op(inst)) {
	case OP_TRAP:
#ifdef CONFIG_PPC_BOOK3S
	case OP_TRAP_64:
		kvmppc_core_queue_program(vcpu, SRR1_PROGTRAP);
#else
		kvmppc_core_queue_program(vcpu,
					  vcpu->arch.shared->esr | ESR_PTR);
#endif
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

		case OP_31_XOP_LBZUX:
			rt = get_rt(inst);
			ra = get_ra(inst);
			rb = get_rb(inst);

			ea = kvmppc_get_gpr(vcpu, rb);
			if (ra)
				ea += kvmppc_get_gpr(vcpu, ra);

			emulated = kvmppc_handle_load(run, vcpu, rt, 1, 1);
			kvmppc_set_gpr(vcpu, ra, ea);
			break;

		case OP_31_XOP_STWX:
			rs = get_rs(inst);
			emulated = kvmppc_handle_store(run, vcpu,
						       kvmppc_get_gpr(vcpu, rs),
			                               4, 1);
			break;

		case OP_31_XOP_STBX:
			rs = get_rs(inst);
			emulated = kvmppc_handle_store(run, vcpu,
						       kvmppc_get_gpr(vcpu, rs),
			                               1, 1);
			break;

		case OP_31_XOP_STBUX:
			rs = get_rs(inst);
			ra = get_ra(inst);
			rb = get_rb(inst);

			ea = kvmppc_get_gpr(vcpu, rb);
			if (ra)
				ea += kvmppc_get_gpr(vcpu, ra);

			emulated = kvmppc_handle_store(run, vcpu,
						       kvmppc_get_gpr(vcpu, rs),
			                               1, 1);
			kvmppc_set_gpr(vcpu, rs, ea);
			break;

		case OP_31_XOP_LHAX:
			rt = get_rt(inst);
			emulated = kvmppc_handle_loads(run, vcpu, rt, 2, 1);
			break;

		case OP_31_XOP_LHZX:
			rt = get_rt(inst);
			emulated = kvmppc_handle_load(run, vcpu, rt, 2, 1);
			break;

		case OP_31_XOP_LHZUX:
			rt = get_rt(inst);
			ra = get_ra(inst);
			rb = get_rb(inst);

			ea = kvmppc_get_gpr(vcpu, rb);
			if (ra)
				ea += kvmppc_get_gpr(vcpu, ra);

			emulated = kvmppc_handle_load(run, vcpu, rt, 2, 1);
			kvmppc_set_gpr(vcpu, ra, ea);
			break;

		case OP_31_XOP_MFSPR:
			sprn = get_sprn(inst);
			rt = get_rt(inst);

			switch (sprn) {
			case SPRN_SRR0:
				kvmppc_set_gpr(vcpu, rt, vcpu->arch.shared->srr0);
				break;
			case SPRN_SRR1:
				kvmppc_set_gpr(vcpu, rt, vcpu->arch.shared->srr1);
				break;
			case SPRN_PVR:
				kvmppc_set_gpr(vcpu, rt, vcpu->arch.pvr); break;
			case SPRN_PIR:
				kvmppc_set_gpr(vcpu, rt, vcpu->vcpu_id); break;
			case SPRN_MSSSR0:
				kvmppc_set_gpr(vcpu, rt, 0); break;

			/* Note: mftb and TBRL/TBWL are user-accessible, so
			 * the guest can always access the real TB anyways.
			 * In fact, we probably will never see these traps. */
			case SPRN_TBWL:
				kvmppc_set_gpr(vcpu, rt, get_tb() >> 32); break;
			case SPRN_TBWU:
				kvmppc_set_gpr(vcpu, rt, get_tb()); break;

			case SPRN_SPRG0:
				kvmppc_set_gpr(vcpu, rt, vcpu->arch.shared->sprg0);
				break;
			case SPRN_SPRG1:
				kvmppc_set_gpr(vcpu, rt, vcpu->arch.shared->sprg1);
				break;
			case SPRN_SPRG2:
				kvmppc_set_gpr(vcpu, rt, vcpu->arch.shared->sprg2);
				break;
			case SPRN_SPRG3:
				kvmppc_set_gpr(vcpu, rt, vcpu->arch.shared->sprg3);
				break;
			/* Note: SPRG4-7 are user-readable, so we don't get
			 * a trap. */

			case SPRN_DEC:
			{
				kvmppc_set_gpr(vcpu, rt,
					       kvmppc_get_dec(vcpu, get_tb()));
				break;
			}
			default:
				emulated = kvmppc_core_emulate_mfspr(vcpu, sprn, rt);
				if (emulated == EMULATE_FAIL) {
					printk("mfspr: unknown spr %x\n", sprn);
					kvmppc_set_gpr(vcpu, rt, 0);
				}
				break;
			}
			kvmppc_set_exit_type(vcpu, EMULATED_MFSPR_EXITS);
			break;

		case OP_31_XOP_STHX:
			rs = get_rs(inst);
			ra = get_ra(inst);
			rb = get_rb(inst);

			emulated = kvmppc_handle_store(run, vcpu,
						       kvmppc_get_gpr(vcpu, rs),
			                               2, 1);
			break;

		case OP_31_XOP_STHUX:
			rs = get_rs(inst);
			ra = get_ra(inst);
			rb = get_rb(inst);

			ea = kvmppc_get_gpr(vcpu, rb);
			if (ra)
				ea += kvmppc_get_gpr(vcpu, ra);

			emulated = kvmppc_handle_store(run, vcpu,
						       kvmppc_get_gpr(vcpu, rs),
			                               2, 1);
			kvmppc_set_gpr(vcpu, ra, ea);
			break;

		case OP_31_XOP_MTSPR:
			sprn = get_sprn(inst);
			rs = get_rs(inst);
			switch (sprn) {
			case SPRN_SRR0:
				vcpu->arch.shared->srr0 = kvmppc_get_gpr(vcpu, rs);
				break;
			case SPRN_SRR1:
				vcpu->arch.shared->srr1 = kvmppc_get_gpr(vcpu, rs);
				break;

			/* XXX We need to context-switch the timebase for
			 * watchdog and FIT. */
			case SPRN_TBWL: break;
			case SPRN_TBWU: break;

			case SPRN_MSSSR0: break;

			case SPRN_DEC:
				vcpu->arch.dec = kvmppc_get_gpr(vcpu, rs);
				kvmppc_emulate_dec(vcpu);
				break;

			case SPRN_SPRG0:
				vcpu->arch.shared->sprg0 = kvmppc_get_gpr(vcpu, rs);
				break;
			case SPRN_SPRG1:
				vcpu->arch.shared->sprg1 = kvmppc_get_gpr(vcpu, rs);
				break;
			case SPRN_SPRG2:
				vcpu->arch.shared->sprg2 = kvmppc_get_gpr(vcpu, rs);
				break;
			case SPRN_SPRG3:
				vcpu->arch.shared->sprg3 = kvmppc_get_gpr(vcpu, rs);
				break;

			default:
				emulated = kvmppc_core_emulate_mtspr(vcpu, sprn, rs);
				if (emulated == EMULATE_FAIL)
					printk("mtspr: unknown spr %x\n", sprn);
				break;
			}
			kvmppc_set_exit_type(vcpu, EMULATED_MTSPR_EXITS);
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
						       kvmppc_get_gpr(vcpu, rs),
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
						       kvmppc_get_gpr(vcpu, rs),
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
		kvmppc_set_gpr(vcpu, ra, vcpu->arch.paddr_accessed);
		break;

	case OP_LBZ:
		rt = get_rt(inst);
		emulated = kvmppc_handle_load(run, vcpu, rt, 1, 1);
		break;

	case OP_LBZU:
		ra = get_ra(inst);
		rt = get_rt(inst);
		emulated = kvmppc_handle_load(run, vcpu, rt, 1, 1);
		kvmppc_set_gpr(vcpu, ra, vcpu->arch.paddr_accessed);
		break;

	case OP_STW:
		rs = get_rs(inst);
		emulated = kvmppc_handle_store(run, vcpu,
					       kvmppc_get_gpr(vcpu, rs),
		                               4, 1);
		break;

	case OP_STWU:
		ra = get_ra(inst);
		rs = get_rs(inst);
		emulated = kvmppc_handle_store(run, vcpu,
					       kvmppc_get_gpr(vcpu, rs),
		                               4, 1);
		kvmppc_set_gpr(vcpu, ra, vcpu->arch.paddr_accessed);
		break;

	case OP_STB:
		rs = get_rs(inst);
		emulated = kvmppc_handle_store(run, vcpu,
					       kvmppc_get_gpr(vcpu, rs),
		                               1, 1);
		break;

	case OP_STBU:
		ra = get_ra(inst);
		rs = get_rs(inst);
		emulated = kvmppc_handle_store(run, vcpu,
					       kvmppc_get_gpr(vcpu, rs),
		                               1, 1);
		kvmppc_set_gpr(vcpu, ra, vcpu->arch.paddr_accessed);
		break;

	case OP_LHZ:
		rt = get_rt(inst);
		emulated = kvmppc_handle_load(run, vcpu, rt, 2, 1);
		break;

	case OP_LHZU:
		ra = get_ra(inst);
		rt = get_rt(inst);
		emulated = kvmppc_handle_load(run, vcpu, rt, 2, 1);
		kvmppc_set_gpr(vcpu, ra, vcpu->arch.paddr_accessed);
		break;

	case OP_LHA:
		rt = get_rt(inst);
		emulated = kvmppc_handle_loads(run, vcpu, rt, 2, 1);
		break;

	case OP_LHAU:
		ra = get_ra(inst);
		rt = get_rt(inst);
		emulated = kvmppc_handle_loads(run, vcpu, rt, 2, 1);
		kvmppc_set_gpr(vcpu, ra, vcpu->arch.paddr_accessed);
		break;

	case OP_STH:
		rs = get_rs(inst);
		emulated = kvmppc_handle_store(run, vcpu,
					       kvmppc_get_gpr(vcpu, rs),
		                               2, 1);
		break;

	case OP_STHU:
		ra = get_ra(inst);
		rs = get_rs(inst);
		emulated = kvmppc_handle_store(run, vcpu,
					       kvmppc_get_gpr(vcpu, rs),
		                               2, 1);
		kvmppc_set_gpr(vcpu, ra, vcpu->arch.paddr_accessed);
		break;

	default:
		emulated = EMULATE_FAIL;
	}

	if (emulated == EMULATE_FAIL) {
		emulated = kvmppc_core_emulate_op(run, vcpu, inst, &advance);
		if (emulated == EMULATE_AGAIN) {
			advance = 0;
		} else if (emulated == EMULATE_FAIL) {
			advance = 0;
			printk(KERN_ERR "Couldn't emulate instruction 0x%08x "
			       "(op %d xop %d)\n", inst, get_op(inst), get_xop(inst));
			kvmppc_core_queue_program(vcpu, 0);
		}
	}

	trace_kvm_ppc_instr(inst, kvmppc_get_pc(vcpu), emulated);

	/* Advance past emulated instruction. */
	if (advance)
		kvmppc_set_pc(vcpu, kvmppc_get_pc(vcpu) + 4);

	return emulated;
}
