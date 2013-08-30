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
 * Copyright SUSE Linux Products GmbH 2009
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#include <asm/kvm_ppc.h>
#include <asm/disassemble.h>
#include <asm/kvm_book3s.h>
#include <asm/reg.h>
#include <asm/switch_to.h>
#include <asm/time.h>

#define OP_19_XOP_RFID		18
#define OP_19_XOP_RFI		50

#define OP_31_XOP_MFMSR		83
#define OP_31_XOP_MTMSR		146
#define OP_31_XOP_MTMSRD	178
#define OP_31_XOP_MTSR		210
#define OP_31_XOP_MTSRIN	242
#define OP_31_XOP_TLBIEL	274
#define OP_31_XOP_TLBIE		306
/* Opcode is officially reserved, reuse it as sc 1 when sc 1 doesn't trap */
#define OP_31_XOP_FAKE_SC1	308
#define OP_31_XOP_SLBMTE	402
#define OP_31_XOP_SLBIE		434
#define OP_31_XOP_SLBIA		498
#define OP_31_XOP_MFSR		595
#define OP_31_XOP_MFSRIN	659
#define OP_31_XOP_DCBA		758
#define OP_31_XOP_SLBMFEV	851
#define OP_31_XOP_EIOIO		854
#define OP_31_XOP_SLBMFEE	915

/* DCBZ is actually 1014, but we patch it to 1010 so we get a trap */
#define OP_31_XOP_DCBZ		1010

#define OP_LFS			48
#define OP_LFD			50
#define OP_STFS			52
#define OP_STFD			54

#define SPRN_GQR0		912
#define SPRN_GQR1		913
#define SPRN_GQR2		914
#define SPRN_GQR3		915
#define SPRN_GQR4		916
#define SPRN_GQR5		917
#define SPRN_GQR6		918
#define SPRN_GQR7		919

/* Book3S_32 defines mfsrin(v) - but that messes up our abstract
 * function pointers, so let's just disable the define. */
#undef mfsrin

enum priv_level {
	PRIV_PROBLEM = 0,
	PRIV_SUPER = 1,
	PRIV_HYPER = 2,
};

static bool spr_allowed(struct kvm_vcpu *vcpu, enum priv_level level)
{
	/* PAPR VMs only access supervisor SPRs */
	if (vcpu->arch.papr_enabled && (level > PRIV_SUPER))
		return false;

	/* Limit user space to its own small SPR set */
	if ((vcpu->arch.shared->msr & MSR_PR) && level > PRIV_PROBLEM)
		return false;

	return true;
}

int kvmppc_core_emulate_op(struct kvm_run *run, struct kvm_vcpu *vcpu,
                           unsigned int inst, int *advance)
{
	int emulated = EMULATE_DONE;
	int rt = get_rt(inst);
	int rs = get_rs(inst);
	int ra = get_ra(inst);
	int rb = get_rb(inst);

	switch (get_op(inst)) {
	case 19:
		switch (get_xop(inst)) {
		case OP_19_XOP_RFID:
		case OP_19_XOP_RFI:
			kvmppc_set_pc(vcpu, vcpu->arch.shared->srr0);
			kvmppc_set_msr(vcpu, vcpu->arch.shared->srr1);
			*advance = 0;
			break;

		default:
			emulated = EMULATE_FAIL;
			break;
		}
		break;
	case 31:
		switch (get_xop(inst)) {
		case OP_31_XOP_MFMSR:
			kvmppc_set_gpr(vcpu, rt, vcpu->arch.shared->msr);
			break;
		case OP_31_XOP_MTMSRD:
		{
			ulong rs_val = kvmppc_get_gpr(vcpu, rs);
			if (inst & 0x10000) {
				ulong new_msr = vcpu->arch.shared->msr;
				new_msr &= ~(MSR_RI | MSR_EE);
				new_msr |= rs_val & (MSR_RI | MSR_EE);
				vcpu->arch.shared->msr = new_msr;
			} else
				kvmppc_set_msr(vcpu, rs_val);
			break;
		}
		case OP_31_XOP_MTMSR:
			kvmppc_set_msr(vcpu, kvmppc_get_gpr(vcpu, rs));
			break;
		case OP_31_XOP_MFSR:
		{
			int srnum;

			srnum = kvmppc_get_field(inst, 12 + 32, 15 + 32);
			if (vcpu->arch.mmu.mfsrin) {
				u32 sr;
				sr = vcpu->arch.mmu.mfsrin(vcpu, srnum);
				kvmppc_set_gpr(vcpu, rt, sr);
			}
			break;
		}
		case OP_31_XOP_MFSRIN:
		{
			int srnum;

			srnum = (kvmppc_get_gpr(vcpu, rb) >> 28) & 0xf;
			if (vcpu->arch.mmu.mfsrin) {
				u32 sr;
				sr = vcpu->arch.mmu.mfsrin(vcpu, srnum);
				kvmppc_set_gpr(vcpu, rt, sr);
			}
			break;
		}
		case OP_31_XOP_MTSR:
			vcpu->arch.mmu.mtsrin(vcpu,
				(inst >> 16) & 0xf,
				kvmppc_get_gpr(vcpu, rs));
			break;
		case OP_31_XOP_MTSRIN:
			vcpu->arch.mmu.mtsrin(vcpu,
				(kvmppc_get_gpr(vcpu, rb) >> 28) & 0xf,
				kvmppc_get_gpr(vcpu, rs));
			break;
		case OP_31_XOP_TLBIE:
		case OP_31_XOP_TLBIEL:
		{
			bool large = (inst & 0x00200000) ? true : false;
			ulong addr = kvmppc_get_gpr(vcpu, rb);
			vcpu->arch.mmu.tlbie(vcpu, addr, large);
			break;
		}
#ifdef CONFIG_KVM_BOOK3S_64_PR
		case OP_31_XOP_FAKE_SC1:
		{
			/* SC 1 papr hypercalls */
			ulong cmd = kvmppc_get_gpr(vcpu, 3);
			int i;

		        if ((vcpu->arch.shared->msr & MSR_PR) ||
			    !vcpu->arch.papr_enabled) {
				emulated = EMULATE_FAIL;
				break;
			}

			if (kvmppc_h_pr(vcpu, cmd) == EMULATE_DONE)
				break;

			run->papr_hcall.nr = cmd;
			for (i = 0; i < 9; ++i) {
				ulong gpr = kvmppc_get_gpr(vcpu, 4 + i);
				run->papr_hcall.args[i] = gpr;
			}

			run->exit_reason = KVM_EXIT_PAPR_HCALL;
			vcpu->arch.hcall_needed = 1;
			emulated = EMULATE_EXIT_USER;
			break;
		}
#endif
		case OP_31_XOP_EIOIO:
			break;
		case OP_31_XOP_SLBMTE:
			if (!vcpu->arch.mmu.slbmte)
				return EMULATE_FAIL;

			vcpu->arch.mmu.slbmte(vcpu,
					kvmppc_get_gpr(vcpu, rs),
					kvmppc_get_gpr(vcpu, rb));
			break;
		case OP_31_XOP_SLBIE:
			if (!vcpu->arch.mmu.slbie)
				return EMULATE_FAIL;

			vcpu->arch.mmu.slbie(vcpu,
					kvmppc_get_gpr(vcpu, rb));
			break;
		case OP_31_XOP_SLBIA:
			if (!vcpu->arch.mmu.slbia)
				return EMULATE_FAIL;

			vcpu->arch.mmu.slbia(vcpu);
			break;
		case OP_31_XOP_SLBMFEE:
			if (!vcpu->arch.mmu.slbmfee) {
				emulated = EMULATE_FAIL;
			} else {
				ulong t, rb_val;

				rb_val = kvmppc_get_gpr(vcpu, rb);
				t = vcpu->arch.mmu.slbmfee(vcpu, rb_val);
				kvmppc_set_gpr(vcpu, rt, t);
			}
			break;
		case OP_31_XOP_SLBMFEV:
			if (!vcpu->arch.mmu.slbmfev) {
				emulated = EMULATE_FAIL;
			} else {
				ulong t, rb_val;

				rb_val = kvmppc_get_gpr(vcpu, rb);
				t = vcpu->arch.mmu.slbmfev(vcpu, rb_val);
				kvmppc_set_gpr(vcpu, rt, t);
			}
			break;
		case OP_31_XOP_DCBA:
			/* Gets treated as NOP */
			break;
		case OP_31_XOP_DCBZ:
		{
			ulong rb_val = kvmppc_get_gpr(vcpu, rb);
			ulong ra_val = 0;
			ulong addr, vaddr;
			u32 zeros[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
			u32 dsisr;
			int r;

			if (ra)
				ra_val = kvmppc_get_gpr(vcpu, ra);

			addr = (ra_val + rb_val) & ~31ULL;
			if (!(vcpu->arch.shared->msr & MSR_SF))
				addr &= 0xffffffff;
			vaddr = addr;

			r = kvmppc_st(vcpu, &addr, 32, zeros, true);
			if ((r == -ENOENT) || (r == -EPERM)) {
				struct kvmppc_book3s_shadow_vcpu *svcpu;

				svcpu = svcpu_get(vcpu);
				*advance = 0;
				vcpu->arch.shared->dar = vaddr;
				svcpu->fault_dar = vaddr;

				dsisr = DSISR_ISSTORE;
				if (r == -ENOENT)
					dsisr |= DSISR_NOHPTE;
				else if (r == -EPERM)
					dsisr |= DSISR_PROTFAULT;

				vcpu->arch.shared->dsisr = dsisr;
				svcpu->fault_dsisr = dsisr;
				svcpu_put(svcpu);

				kvmppc_book3s_queue_irqprio(vcpu,
					BOOK3S_INTERRUPT_DATA_STORAGE);
			}

			break;
		}
		default:
			emulated = EMULATE_FAIL;
		}
		break;
	default:
		emulated = EMULATE_FAIL;
	}

	if (emulated == EMULATE_FAIL)
		emulated = kvmppc_emulate_paired_single(run, vcpu);

	return emulated;
}

void kvmppc_set_bat(struct kvm_vcpu *vcpu, struct kvmppc_bat *bat, bool upper,
                    u32 val)
{
	if (upper) {
		/* Upper BAT */
		u32 bl = (val >> 2) & 0x7ff;
		bat->bepi_mask = (~bl << 17);
		bat->bepi = val & 0xfffe0000;
		bat->vs = (val & 2) ? 1 : 0;
		bat->vp = (val & 1) ? 1 : 0;
		bat->raw = (bat->raw & 0xffffffff00000000ULL) | val;
	} else {
		/* Lower BAT */
		bat->brpn = val & 0xfffe0000;
		bat->wimg = (val >> 3) & 0xf;
		bat->pp = val & 3;
		bat->raw = (bat->raw & 0x00000000ffffffffULL) | ((u64)val << 32);
	}
}

static struct kvmppc_bat *kvmppc_find_bat(struct kvm_vcpu *vcpu, int sprn)
{
	struct kvmppc_vcpu_book3s *vcpu_book3s = to_book3s(vcpu);
	struct kvmppc_bat *bat;

	switch (sprn) {
	case SPRN_IBAT0U ... SPRN_IBAT3L:
		bat = &vcpu_book3s->ibat[(sprn - SPRN_IBAT0U) / 2];
		break;
	case SPRN_IBAT4U ... SPRN_IBAT7L:
		bat = &vcpu_book3s->ibat[4 + ((sprn - SPRN_IBAT4U) / 2)];
		break;
	case SPRN_DBAT0U ... SPRN_DBAT3L:
		bat = &vcpu_book3s->dbat[(sprn - SPRN_DBAT0U) / 2];
		break;
	case SPRN_DBAT4U ... SPRN_DBAT7L:
		bat = &vcpu_book3s->dbat[4 + ((sprn - SPRN_DBAT4U) / 2)];
		break;
	default:
		BUG();
	}

	return bat;
}

int kvmppc_core_emulate_mtspr(struct kvm_vcpu *vcpu, int sprn, ulong spr_val)
{
	int emulated = EMULATE_DONE;

	switch (sprn) {
	case SPRN_SDR1:
		if (!spr_allowed(vcpu, PRIV_HYPER))
			goto unprivileged;
		to_book3s(vcpu)->sdr1 = spr_val;
		break;
	case SPRN_DSISR:
		vcpu->arch.shared->dsisr = spr_val;
		break;
	case SPRN_DAR:
		vcpu->arch.shared->dar = spr_val;
		break;
	case SPRN_HIOR:
		to_book3s(vcpu)->hior = spr_val;
		break;
	case SPRN_IBAT0U ... SPRN_IBAT3L:
	case SPRN_IBAT4U ... SPRN_IBAT7L:
	case SPRN_DBAT0U ... SPRN_DBAT3L:
	case SPRN_DBAT4U ... SPRN_DBAT7L:
	{
		struct kvmppc_bat *bat = kvmppc_find_bat(vcpu, sprn);

		kvmppc_set_bat(vcpu, bat, !(sprn % 2), (u32)spr_val);
		/* BAT writes happen so rarely that we're ok to flush
		 * everything here */
		kvmppc_mmu_pte_flush(vcpu, 0, 0);
		kvmppc_mmu_flush_segments(vcpu);
		break;
	}
	case SPRN_HID0:
		to_book3s(vcpu)->hid[0] = spr_val;
		break;
	case SPRN_HID1:
		to_book3s(vcpu)->hid[1] = spr_val;
		break;
	case SPRN_HID2:
		to_book3s(vcpu)->hid[2] = spr_val;
		break;
	case SPRN_HID2_GEKKO:
		to_book3s(vcpu)->hid[2] = spr_val;
		/* HID2.PSE controls paired single on gekko */
		switch (vcpu->arch.pvr) {
		case 0x00080200:	/* lonestar 2.0 */
		case 0x00088202:	/* lonestar 2.2 */
		case 0x70000100:	/* gekko 1.0 */
		case 0x00080100:	/* gekko 2.0 */
		case 0x00083203:	/* gekko 2.3a */
		case 0x00083213:	/* gekko 2.3b */
		case 0x00083204:	/* gekko 2.4 */
		case 0x00083214:	/* gekko 2.4e (8SE) - retail HW2 */
		case 0x00087200:	/* broadway */
			if (vcpu->arch.hflags & BOOK3S_HFLAG_NATIVE_PS) {
				/* Native paired singles */
			} else if (spr_val & (1 << 29)) { /* HID2.PSE */
				vcpu->arch.hflags |= BOOK3S_HFLAG_PAIRED_SINGLE;
				kvmppc_giveup_ext(vcpu, MSR_FP);
			} else {
				vcpu->arch.hflags &= ~BOOK3S_HFLAG_PAIRED_SINGLE;
			}
			break;
		}
		break;
	case SPRN_HID4:
	case SPRN_HID4_GEKKO:
		to_book3s(vcpu)->hid[4] = spr_val;
		break;
	case SPRN_HID5:
		to_book3s(vcpu)->hid[5] = spr_val;
		/* guest HID5 set can change is_dcbz32 */
		if (vcpu->arch.mmu.is_dcbz32(vcpu) &&
		    (mfmsr() & MSR_HV))
			vcpu->arch.hflags |= BOOK3S_HFLAG_DCBZ32;
		break;
	case SPRN_PURR:
		to_book3s(vcpu)->purr_offset = spr_val - get_tb();
		break;
	case SPRN_SPURR:
		to_book3s(vcpu)->spurr_offset = spr_val - get_tb();
		break;
	case SPRN_GQR0:
	case SPRN_GQR1:
	case SPRN_GQR2:
	case SPRN_GQR3:
	case SPRN_GQR4:
	case SPRN_GQR5:
	case SPRN_GQR6:
	case SPRN_GQR7:
		to_book3s(vcpu)->gqr[sprn - SPRN_GQR0] = spr_val;
		break;
	case SPRN_ICTC:
	case SPRN_THRM1:
	case SPRN_THRM2:
	case SPRN_THRM3:
	case SPRN_CTRLF:
	case SPRN_CTRLT:
	case SPRN_L2CR:
	case SPRN_DSCR:
	case SPRN_MMCR0_GEKKO:
	case SPRN_MMCR1_GEKKO:
	case SPRN_PMC1_GEKKO:
	case SPRN_PMC2_GEKKO:
	case SPRN_PMC3_GEKKO:
	case SPRN_PMC4_GEKKO:
	case SPRN_WPAR_GEKKO:
	case SPRN_MSSSR0:
	case SPRN_DABR:
		break;
unprivileged:
	default:
		printk(KERN_INFO "KVM: invalid SPR write: %d\n", sprn);
#ifndef DEBUG_SPR
		emulated = EMULATE_FAIL;
#endif
		break;
	}

	return emulated;
}

int kvmppc_core_emulate_mfspr(struct kvm_vcpu *vcpu, int sprn, ulong *spr_val)
{
	int emulated = EMULATE_DONE;

	switch (sprn) {
	case SPRN_IBAT0U ... SPRN_IBAT3L:
	case SPRN_IBAT4U ... SPRN_IBAT7L:
	case SPRN_DBAT0U ... SPRN_DBAT3L:
	case SPRN_DBAT4U ... SPRN_DBAT7L:
	{
		struct kvmppc_bat *bat = kvmppc_find_bat(vcpu, sprn);

		if (sprn % 2)
			*spr_val = bat->raw >> 32;
		else
			*spr_val = bat->raw;

		break;
	}
	case SPRN_SDR1:
		if (!spr_allowed(vcpu, PRIV_HYPER))
			goto unprivileged;
		*spr_val = to_book3s(vcpu)->sdr1;
		break;
	case SPRN_DSISR:
		*spr_val = vcpu->arch.shared->dsisr;
		break;
	case SPRN_DAR:
		*spr_val = vcpu->arch.shared->dar;
		break;
	case SPRN_HIOR:
		*spr_val = to_book3s(vcpu)->hior;
		break;
	case SPRN_HID0:
		*spr_val = to_book3s(vcpu)->hid[0];
		break;
	case SPRN_HID1:
		*spr_val = to_book3s(vcpu)->hid[1];
		break;
	case SPRN_HID2:
	case SPRN_HID2_GEKKO:
		*spr_val = to_book3s(vcpu)->hid[2];
		break;
	case SPRN_HID4:
	case SPRN_HID4_GEKKO:
		*spr_val = to_book3s(vcpu)->hid[4];
		break;
	case SPRN_HID5:
		*spr_val = to_book3s(vcpu)->hid[5];
		break;
	case SPRN_CFAR:
	case SPRN_DSCR:
		*spr_val = 0;
		break;
	case SPRN_PURR:
		*spr_val = get_tb() + to_book3s(vcpu)->purr_offset;
		break;
	case SPRN_SPURR:
		*spr_val = get_tb() + to_book3s(vcpu)->purr_offset;
		break;
	case SPRN_GQR0:
	case SPRN_GQR1:
	case SPRN_GQR2:
	case SPRN_GQR3:
	case SPRN_GQR4:
	case SPRN_GQR5:
	case SPRN_GQR6:
	case SPRN_GQR7:
		*spr_val = to_book3s(vcpu)->gqr[sprn - SPRN_GQR0];
		break;
	case SPRN_THRM1:
	case SPRN_THRM2:
	case SPRN_THRM3:
	case SPRN_CTRLF:
	case SPRN_CTRLT:
	case SPRN_L2CR:
	case SPRN_MMCR0_GEKKO:
	case SPRN_MMCR1_GEKKO:
	case SPRN_PMC1_GEKKO:
	case SPRN_PMC2_GEKKO:
	case SPRN_PMC3_GEKKO:
	case SPRN_PMC4_GEKKO:
	case SPRN_WPAR_GEKKO:
	case SPRN_MSSSR0:
	case SPRN_DABR:
		*spr_val = 0;
		break;
	default:
unprivileged:
		printk(KERN_INFO "KVM: invalid SPR read: %d\n", sprn);
#ifndef DEBUG_SPR
		emulated = EMULATE_FAIL;
#endif
		break;
	}

	return emulated;
}

u32 kvmppc_alignment_dsisr(struct kvm_vcpu *vcpu, unsigned int inst)
{
	u32 dsisr = 0;

	/*
	 * This is what the spec says about DSISR bits (not mentioned = 0):
	 *
	 * 12:13		[DS]	Set to bits 30:31
	 * 15:16		[X]	Set to bits 29:30
	 * 17			[X]	Set to bit 25
	 *			[D/DS]	Set to bit 5
	 * 18:21		[X]	Set to bits 21:24
	 *			[D/DS]	Set to bits 1:4
	 * 22:26			Set to bits 6:10 (RT/RS/FRT/FRS)
	 * 27:31			Set to bits 11:15 (RA)
	 */

	switch (get_op(inst)) {
	/* D-form */
	case OP_LFS:
	case OP_LFD:
	case OP_STFD:
	case OP_STFS:
		dsisr |= (inst >> 12) & 0x4000;	/* bit 17 */
		dsisr |= (inst >> 17) & 0x3c00; /* bits 18:21 */
		break;
	/* X-form */
	case 31:
		dsisr |= (inst << 14) & 0x18000; /* bits 15:16 */
		dsisr |= (inst << 8)  & 0x04000; /* bit 17 */
		dsisr |= (inst << 3)  & 0x03c00; /* bits 18:21 */
		break;
	default:
		printk(KERN_INFO "KVM: Unaligned instruction 0x%x\n", inst);
		break;
	}

	dsisr |= (inst >> 16) & 0x03ff; /* bits 22:31 */

	return dsisr;
}

ulong kvmppc_alignment_dar(struct kvm_vcpu *vcpu, unsigned int inst)
{
	ulong dar = 0;
	ulong ra = get_ra(inst);
	ulong rb = get_rb(inst);

	switch (get_op(inst)) {
	case OP_LFS:
	case OP_LFD:
	case OP_STFD:
	case OP_STFS:
		if (ra)
			dar = kvmppc_get_gpr(vcpu, ra);
		dar += (s32)((s16)inst);
		break;
	case 31:
		if (ra)
			dar = kvmppc_get_gpr(vcpu, ra);
		dar += kvmppc_get_gpr(vcpu, rb);
		break;
	default:
		printk(KERN_INFO "KVM: Unaligned instruction 0x%x\n", inst);
		break;
	}

	return dar;
}
