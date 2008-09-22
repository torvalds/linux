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

#include <asm/dcr.h>
#include <asm/dcr-regs.h>
#include <asm/time.h>
#include <asm/byteorder.h>
#include <asm/kvm_ppc.h>

#include "44x_tlb.h"

/* Instruction decoding */
static inline unsigned int get_op(u32 inst)
{
	return inst >> 26;
}

static inline unsigned int get_xop(u32 inst)
{
	return (inst >> 1) & 0x3ff;
}

static inline unsigned int get_sprn(u32 inst)
{
	return ((inst >> 16) & 0x1f) | ((inst >> 6) & 0x3e0);
}

static inline unsigned int get_dcrn(u32 inst)
{
	return ((inst >> 16) & 0x1f) | ((inst >> 6) & 0x3e0);
}

static inline unsigned int get_rt(u32 inst)
{
	return (inst >> 21) & 0x1f;
}

static inline unsigned int get_rs(u32 inst)
{
	return (inst >> 21) & 0x1f;
}

static inline unsigned int get_ra(u32 inst)
{
	return (inst >> 16) & 0x1f;
}

static inline unsigned int get_rb(u32 inst)
{
	return (inst >> 11) & 0x1f;
}

static inline unsigned int get_rc(u32 inst)
{
	return inst & 0x1;
}

static inline unsigned int get_ws(u32 inst)
{
	return (inst >> 11) & 0x1f;
}

static inline unsigned int get_d(u32 inst)
{
	return inst & 0xffff;
}

static int tlbe_is_host_safe(const struct kvm_vcpu *vcpu,
                             const struct tlbe *tlbe)
{
	gpa_t gpa;

	if (!get_tlb_v(tlbe))
		return 0;

	/* Does it match current guest AS? */
	/* XXX what about IS != DS? */
	if (get_tlb_ts(tlbe) != !!(vcpu->arch.msr & MSR_IS))
		return 0;

	gpa = get_tlb_raddr(tlbe);
	if (!gfn_to_memslot(vcpu->kvm, gpa >> PAGE_SHIFT))
		/* Mapping is not for RAM. */
		return 0;

	return 1;
}

static int kvmppc_emul_tlbwe(struct kvm_vcpu *vcpu, u32 inst)
{
	u64 eaddr;
	u64 raddr;
	u64 asid;
	u32 flags;
	struct tlbe *tlbe;
	unsigned int ra;
	unsigned int rs;
	unsigned int ws;
	unsigned int index;

	ra = get_ra(inst);
	rs = get_rs(inst);
	ws = get_ws(inst);

	index = vcpu->arch.gpr[ra];
	if (index > PPC44x_TLB_SIZE) {
		printk("%s: index %d\n", __func__, index);
		kvmppc_dump_vcpu(vcpu);
		return EMULATE_FAIL;
	}

	tlbe = &vcpu->arch.guest_tlb[index];

	/* Invalidate shadow mappings for the about-to-be-clobbered TLBE. */
	if (tlbe->word0 & PPC44x_TLB_VALID) {
		eaddr = get_tlb_eaddr(tlbe);
		asid = (tlbe->word0 & PPC44x_TLB_TS) | tlbe->tid;
		kvmppc_mmu_invalidate(vcpu, eaddr, get_tlb_end(tlbe), asid);
	}

	switch (ws) {
	case PPC44x_TLB_PAGEID:
		tlbe->tid = vcpu->arch.mmucr & 0xff;
		tlbe->word0 = vcpu->arch.gpr[rs];
		break;

	case PPC44x_TLB_XLAT:
		tlbe->word1 = vcpu->arch.gpr[rs];
		break;

	case PPC44x_TLB_ATTRIB:
		tlbe->word2 = vcpu->arch.gpr[rs];
		break;

	default:
		return EMULATE_FAIL;
	}

	if (tlbe_is_host_safe(vcpu, tlbe)) {
		eaddr = get_tlb_eaddr(tlbe);
		raddr = get_tlb_raddr(tlbe);
		asid = (tlbe->word0 & PPC44x_TLB_TS) | tlbe->tid;
		flags = tlbe->word2 & 0xffff;

		/* Create a 4KB mapping on the host. If the guest wanted a
		 * large page, only the first 4KB is mapped here and the rest
		 * are mapped on the fly. */
		kvmppc_mmu_map(vcpu, eaddr, raddr >> PAGE_SHIFT, asid, flags);
	}

	return EMULATE_DONE;
}

static void kvmppc_emulate_dec(struct kvm_vcpu *vcpu)
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

static void kvmppc_emul_rfi(struct kvm_vcpu *vcpu)
{
	vcpu->arch.pc = vcpu->arch.srr0;
	kvmppc_set_msr(vcpu, vcpu->arch.srr1);
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
int kvmppc_emulate_instruction(struct kvm_run *run, struct kvm_vcpu *vcpu)
{
	u32 inst = vcpu->arch.last_inst;
	u32 ea;
	int ra;
	int rb;
	int rc;
	int rs;
	int rt;
	int sprn;
	int dcrn;
	enum emulation_result emulated = EMULATE_DONE;
	int advance = 1;

	switch (get_op(inst)) {
	case 3:                                                 /* trap */
		printk("trap!\n");
		kvmppc_queue_exception(vcpu, BOOKE_INTERRUPT_PROGRAM);
		advance = 0;
		break;

	case 19:
		switch (get_xop(inst)) {
		case 50:                                        /* rfi */
			kvmppc_emul_rfi(vcpu);
			advance = 0;
			break;

		default:
			emulated = EMULATE_FAIL;
			break;
		}
		break;

	case 31:
		switch (get_xop(inst)) {

		case 23:                                        /* lwzx */
			rt = get_rt(inst);
			emulated = kvmppc_handle_load(run, vcpu, rt, 4, 1);
			break;

		case 83:                                        /* mfmsr */
			rt = get_rt(inst);
			vcpu->arch.gpr[rt] = vcpu->arch.msr;
			break;

		case 87:                                        /* lbzx */
			rt = get_rt(inst);
			emulated = kvmppc_handle_load(run, vcpu, rt, 1, 1);
			break;

		case 131:                                       /* wrtee */
			rs = get_rs(inst);
			vcpu->arch.msr = (vcpu->arch.msr & ~MSR_EE)
			                 | (vcpu->arch.gpr[rs] & MSR_EE);
			break;

		case 146:                                       /* mtmsr */
			rs = get_rs(inst);
			kvmppc_set_msr(vcpu, vcpu->arch.gpr[rs]);
			break;

		case 151:                                       /* stwx */
			rs = get_rs(inst);
			emulated = kvmppc_handle_store(run, vcpu,
			                               vcpu->arch.gpr[rs],
			                               4, 1);
			break;

		case 163:                                       /* wrteei */
			vcpu->arch.msr = (vcpu->arch.msr & ~MSR_EE)
			                 | (inst & MSR_EE);
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

		case 323:                                       /* mfdcr */
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

		case 339:                                       /* mfspr */
			sprn = get_sprn(inst);
			rt = get_rt(inst);

			switch (sprn) {
			case SPRN_SRR0:
				vcpu->arch.gpr[rt] = vcpu->arch.srr0; break;
			case SPRN_SRR1:
				vcpu->arch.gpr[rt] = vcpu->arch.srr1; break;
			case SPRN_MMUCR:
				vcpu->arch.gpr[rt] = vcpu->arch.mmucr; break;
			case SPRN_PID:
				vcpu->arch.gpr[rt] = vcpu->arch.pid; break;
			case SPRN_IVPR:
				vcpu->arch.gpr[rt] = vcpu->arch.ivpr; break;
			case SPRN_CCR0:
				vcpu->arch.gpr[rt] = vcpu->arch.ccr0; break;
			case SPRN_CCR1:
				vcpu->arch.gpr[rt] = vcpu->arch.ccr1; break;
			case SPRN_PVR:
				vcpu->arch.gpr[rt] = vcpu->arch.pvr; break;
			case SPRN_DEAR:
				vcpu->arch.gpr[rt] = vcpu->arch.dear; break;
			case SPRN_ESR:
				vcpu->arch.gpr[rt] = vcpu->arch.esr; break;
			case SPRN_DBCR0:
				vcpu->arch.gpr[rt] = vcpu->arch.dbcr0; break;
			case SPRN_DBCR1:
				vcpu->arch.gpr[rt] = vcpu->arch.dbcr1; break;

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
				printk("mfspr: unknown spr %x\n", sprn);
				vcpu->arch.gpr[rt] = 0;
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

		case 451:                                       /* mtdcr */
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

		case 467:                                       /* mtspr */
			sprn = get_sprn(inst);
			rs = get_rs(inst);
			switch (sprn) {
			case SPRN_SRR0:
				vcpu->arch.srr0 = vcpu->arch.gpr[rs]; break;
			case SPRN_SRR1:
				vcpu->arch.srr1 = vcpu->arch.gpr[rs]; break;
			case SPRN_MMUCR:
				vcpu->arch.mmucr = vcpu->arch.gpr[rs]; break;
			case SPRN_PID:
				vcpu->arch.pid = vcpu->arch.gpr[rs]; break;
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

			/* XXX We need to context-switch the timebase for
			 * watchdog and FIT. */
			case SPRN_TBWL: break;
			case SPRN_TBWU: break;

			case SPRN_DEC:
				vcpu->arch.dec = vcpu->arch.gpr[rs];
				kvmppc_emulate_dec(vcpu);
				break;

			case SPRN_TSR:
				vcpu->arch.tsr &= ~vcpu->arch.gpr[rs]; break;

			case SPRN_TCR:
				vcpu->arch.tcr = vcpu->arch.gpr[rs];
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
				printk("mtspr: unknown spr %x\n", sprn);
				emulated = EMULATE_FAIL;
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

		case 978:                                       /* tlbwe */
			emulated = kvmppc_emul_tlbwe(vcpu, inst);
			break;

		case 914:       {                               /* tlbsx */
			int index;
			unsigned int as = get_mmucr_sts(vcpu);
			unsigned int pid = get_mmucr_stid(vcpu);

			rt = get_rt(inst);
			ra = get_ra(inst);
			rb = get_rb(inst);
			rc = get_rc(inst);

			ea = vcpu->arch.gpr[rb];
			if (ra)
				ea += vcpu->arch.gpr[ra];

			index = kvmppc_44x_tlb_index(vcpu, ea, pid, as);
			if (rc) {
				if (index < 0)
					vcpu->arch.cr &= ~0x20000000;
				else
					vcpu->arch.cr |= 0x20000000;
			}
			vcpu->arch.gpr[rt] = index;

			}
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

		case 966:                                       /* iccci */
			break;

		default:
			printk("unknown: op %d xop %d\n", get_op(inst),
				get_xop(inst));
			emulated = EMULATE_FAIL;
			break;
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
		printk("unknown op %d\n", get_op(inst));
		emulated = EMULATE_FAIL;
		break;
	}

	if (advance)
		vcpu->arch.pc += 4; /* Advance past emulated instruction. */

	return emulated;
}
