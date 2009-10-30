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

#define OP_19_XOP_RFID		18
#define OP_19_XOP_RFI		50

#define OP_31_XOP_MFMSR		83
#define OP_31_XOP_MTMSR		146
#define OP_31_XOP_MTMSRD	178
#define OP_31_XOP_MTSRIN	242
#define OP_31_XOP_TLBIEL	274
#define OP_31_XOP_TLBIE		306
#define OP_31_XOP_SLBMTE	402
#define OP_31_XOP_SLBIE		434
#define OP_31_XOP_SLBIA		498
#define OP_31_XOP_MFSRIN	659
#define OP_31_XOP_SLBMFEV	851
#define OP_31_XOP_EIOIO		854
#define OP_31_XOP_SLBMFEE	915

/* DCBZ is actually 1014, but we patch it to 1010 so we get a trap */
#define OP_31_XOP_DCBZ		1010

int kvmppc_core_emulate_op(struct kvm_run *run, struct kvm_vcpu *vcpu,
                           unsigned int inst, int *advance)
{
	int emulated = EMULATE_DONE;

	switch (get_op(inst)) {
	case 19:
		switch (get_xop(inst)) {
		case OP_19_XOP_RFID:
		case OP_19_XOP_RFI:
			vcpu->arch.pc = vcpu->arch.srr0;
			kvmppc_set_msr(vcpu, vcpu->arch.srr1);
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
			vcpu->arch.gpr[get_rt(inst)] = vcpu->arch.msr;
			break;
		case OP_31_XOP_MTMSRD:
		{
			ulong rs = vcpu->arch.gpr[get_rs(inst)];
			if (inst & 0x10000) {
				vcpu->arch.msr &= ~(MSR_RI | MSR_EE);
				vcpu->arch.msr |= rs & (MSR_RI | MSR_EE);
			} else
				kvmppc_set_msr(vcpu, rs);
			break;
		}
		case OP_31_XOP_MTMSR:
			kvmppc_set_msr(vcpu, vcpu->arch.gpr[get_rs(inst)]);
			break;
		case OP_31_XOP_MFSRIN:
		{
			int srnum;

			srnum = (vcpu->arch.gpr[get_rb(inst)] >> 28) & 0xf;
			if (vcpu->arch.mmu.mfsrin) {
				u32 sr;
				sr = vcpu->arch.mmu.mfsrin(vcpu, srnum);
				vcpu->arch.gpr[get_rt(inst)] = sr;
			}
			break;
		}
		case OP_31_XOP_MTSRIN:
			vcpu->arch.mmu.mtsrin(vcpu,
				(vcpu->arch.gpr[get_rb(inst)] >> 28) & 0xf,
				vcpu->arch.gpr[get_rs(inst)]);
			break;
		case OP_31_XOP_TLBIE:
		case OP_31_XOP_TLBIEL:
		{
			bool large = (inst & 0x00200000) ? true : false;
			ulong addr = vcpu->arch.gpr[get_rb(inst)];
			vcpu->arch.mmu.tlbie(vcpu, addr, large);
			break;
		}
		case OP_31_XOP_EIOIO:
			break;
		case OP_31_XOP_SLBMTE:
			if (!vcpu->arch.mmu.slbmte)
				return EMULATE_FAIL;

			vcpu->arch.mmu.slbmte(vcpu, vcpu->arch.gpr[get_rs(inst)],
						vcpu->arch.gpr[get_rb(inst)]);
			break;
		case OP_31_XOP_SLBIE:
			if (!vcpu->arch.mmu.slbie)
				return EMULATE_FAIL;

			vcpu->arch.mmu.slbie(vcpu, vcpu->arch.gpr[get_rb(inst)]);
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
				ulong t, rb;

				rb = vcpu->arch.gpr[get_rb(inst)];
				t = vcpu->arch.mmu.slbmfee(vcpu, rb);
				vcpu->arch.gpr[get_rt(inst)] = t;
			}
			break;
		case OP_31_XOP_SLBMFEV:
			if (!vcpu->arch.mmu.slbmfev) {
				emulated = EMULATE_FAIL;
			} else {
				ulong t, rb;

				rb = vcpu->arch.gpr[get_rb(inst)];
				t = vcpu->arch.mmu.slbmfev(vcpu, rb);
				vcpu->arch.gpr[get_rt(inst)] = t;
			}
			break;
		case OP_31_XOP_DCBZ:
		{
			ulong rb =  vcpu->arch.gpr[get_rb(inst)];
			ulong ra = 0;
			ulong addr;
			u32 zeros[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

			if (get_ra(inst))
				ra = vcpu->arch.gpr[get_ra(inst)];

			addr = (ra + rb) & ~31ULL;
			if (!(vcpu->arch.msr & MSR_SF))
				addr &= 0xffffffff;

			if (kvmppc_st(vcpu, addr, 32, zeros)) {
				vcpu->arch.dear = addr;
				vcpu->arch.fault_dear = addr;
				to_book3s(vcpu)->dsisr = DSISR_PROTFAULT |
						      DSISR_ISSTORE;
				kvmppc_book3s_queue_irqprio(vcpu,
					BOOK3S_INTERRUPT_DATA_STORAGE);
				kvmppc_mmu_pte_flush(vcpu, addr, ~0xFFFULL);
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

	return emulated;
}

static void kvmppc_write_bat(struct kvm_vcpu *vcpu, int sprn, u64 val)
{
	struct kvmppc_vcpu_book3s *vcpu_book3s = to_book3s(vcpu);
	struct kvmppc_bat *bat;

	switch (sprn) {
	case SPRN_IBAT0U ... SPRN_IBAT3L:
		bat = &vcpu_book3s->ibat[(sprn - SPRN_IBAT0U) / 2];
		break;
	case SPRN_IBAT4U ... SPRN_IBAT7L:
		bat = &vcpu_book3s->ibat[(sprn - SPRN_IBAT4U) / 2];
		break;
	case SPRN_DBAT0U ... SPRN_DBAT3L:
		bat = &vcpu_book3s->dbat[(sprn - SPRN_DBAT0U) / 2];
		break;
	case SPRN_DBAT4U ... SPRN_DBAT7L:
		bat = &vcpu_book3s->dbat[(sprn - SPRN_DBAT4U) / 2];
		break;
	default:
		BUG();
	}

	if (!(sprn % 2)) {
		/* Upper BAT */
		u32 bl = (val >> 2) & 0x7ff;
		bat->bepi_mask = (~bl << 17);
		bat->bepi = val & 0xfffe0000;
		bat->vs = (val & 2) ? 1 : 0;
		bat->vp = (val & 1) ? 1 : 0;
	} else {
		/* Lower BAT */
		bat->brpn = val & 0xfffe0000;
		bat->wimg = (val >> 3) & 0xf;
		bat->pp = val & 3;
	}
}

int kvmppc_core_emulate_mtspr(struct kvm_vcpu *vcpu, int sprn, int rs)
{
	int emulated = EMULATE_DONE;

	switch (sprn) {
	case SPRN_SDR1:
		to_book3s(vcpu)->sdr1 = vcpu->arch.gpr[rs];
		break;
	case SPRN_DSISR:
		to_book3s(vcpu)->dsisr = vcpu->arch.gpr[rs];
		break;
	case SPRN_DAR:
		vcpu->arch.dear = vcpu->arch.gpr[rs];
		break;
	case SPRN_HIOR:
		to_book3s(vcpu)->hior = vcpu->arch.gpr[rs];
		break;
	case SPRN_IBAT0U ... SPRN_IBAT3L:
	case SPRN_IBAT4U ... SPRN_IBAT7L:
	case SPRN_DBAT0U ... SPRN_DBAT3L:
	case SPRN_DBAT4U ... SPRN_DBAT7L:
		kvmppc_write_bat(vcpu, sprn, vcpu->arch.gpr[rs]);
		/* BAT writes happen so rarely that we're ok to flush
		 * everything here */
		kvmppc_mmu_pte_flush(vcpu, 0, 0);
		break;
	case SPRN_HID0:
		to_book3s(vcpu)->hid[0] = vcpu->arch.gpr[rs];
		break;
	case SPRN_HID1:
		to_book3s(vcpu)->hid[1] = vcpu->arch.gpr[rs];
		break;
	case SPRN_HID2:
		to_book3s(vcpu)->hid[2] = vcpu->arch.gpr[rs];
		break;
	case SPRN_HID4:
		to_book3s(vcpu)->hid[4] = vcpu->arch.gpr[rs];
		break;
	case SPRN_HID5:
		to_book3s(vcpu)->hid[5] = vcpu->arch.gpr[rs];
		/* guest HID5 set can change is_dcbz32 */
		if (vcpu->arch.mmu.is_dcbz32(vcpu) &&
		    (mfmsr() & MSR_HV))
			vcpu->arch.hflags |= BOOK3S_HFLAG_DCBZ32;
		break;
	case SPRN_ICTC:
	case SPRN_THRM1:
	case SPRN_THRM2:
	case SPRN_THRM3:
	case SPRN_CTRLF:
	case SPRN_CTRLT:
		break;
	default:
		printk(KERN_INFO "KVM: invalid SPR write: %d\n", sprn);
#ifndef DEBUG_SPR
		emulated = EMULATE_FAIL;
#endif
		break;
	}

	return emulated;
}

int kvmppc_core_emulate_mfspr(struct kvm_vcpu *vcpu, int sprn, int rt)
{
	int emulated = EMULATE_DONE;

	switch (sprn) {
	case SPRN_SDR1:
		vcpu->arch.gpr[rt] = to_book3s(vcpu)->sdr1;
		break;
	case SPRN_DSISR:
		vcpu->arch.gpr[rt] = to_book3s(vcpu)->dsisr;
		break;
	case SPRN_DAR:
		vcpu->arch.gpr[rt] = vcpu->arch.dear;
		break;
	case SPRN_HIOR:
		vcpu->arch.gpr[rt] = to_book3s(vcpu)->hior;
		break;
	case SPRN_HID0:
		vcpu->arch.gpr[rt] = to_book3s(vcpu)->hid[0];
		break;
	case SPRN_HID1:
		vcpu->arch.gpr[rt] = to_book3s(vcpu)->hid[1];
		break;
	case SPRN_HID2:
		vcpu->arch.gpr[rt] = to_book3s(vcpu)->hid[2];
		break;
	case SPRN_HID4:
		vcpu->arch.gpr[rt] = to_book3s(vcpu)->hid[4];
		break;
	case SPRN_HID5:
		vcpu->arch.gpr[rt] = to_book3s(vcpu)->hid[5];
		break;
	case SPRN_THRM1:
	case SPRN_THRM2:
	case SPRN_THRM3:
	case SPRN_CTRLF:
	case SPRN_CTRLT:
		vcpu->arch.gpr[rt] = 0;
		break;
	default:
		printk(KERN_INFO "KVM: invalid SPR read: %d\n", sprn);
#ifndef DEBUG_SPR
		emulated = EMULATE_FAIL;
#endif
		break;
	}

	return emulated;
}

