/*
 * mmio.c: MMIO emulation components.
 * Copyright (c) 2004, Intel Corporation.
 *  Yaozu Dong (Eddie Dong) (Eddie.dong@intel.com)
 *  Kun Tian (Kevin Tian) (Kevin.tian@intel.com)
 *
 * Copyright (c) 2007 Intel Corporation  KVM support.
 * Xuefei Xu (Anthony Xu) (anthony.xu@intel.com)
 * Xiantao Zhang  (xiantao.zhang@intel.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 */

#include <linux/kvm_host.h>

#include "vcpu.h"

static void vlsapic_write_xtp(struct kvm_vcpu *v, uint8_t val)
{
	VLSAPIC_XTP(v) = val;
}

/*
 * LSAPIC OFFSET
 */
#define PIB_LOW_HALF(ofst)     !(ofst & (1 << 20))
#define PIB_OFST_INTA          0x1E0000
#define PIB_OFST_XTP           0x1E0008

/*
 * execute write IPI op.
 */
static void vlsapic_write_ipi(struct kvm_vcpu *vcpu,
					uint64_t addr, uint64_t data)
{
	struct exit_ctl_data *p = &current_vcpu->arch.exit_data;
	unsigned long psr;

	local_irq_save(psr);

	p->exit_reason = EXIT_REASON_IPI;
	p->u.ipi_data.addr.val = addr;
	p->u.ipi_data.data.val = data;
	vmm_transition(current_vcpu);

	local_irq_restore(psr);

}

void lsapic_write(struct kvm_vcpu *v, unsigned long addr,
			unsigned long length, unsigned long val)
{
	addr &= (PIB_SIZE - 1);

	switch (addr) {
	case PIB_OFST_INTA:
		/*panic_domain(NULL, "Undefined write on PIB INTA\n");*/
		panic_vm(v);
		break;
	case PIB_OFST_XTP:
		if (length == 1) {
			vlsapic_write_xtp(v, val);
		} else {
			/*panic_domain(NULL,
			"Undefined write on PIB XTP\n");*/
			panic_vm(v);
		}
		break;
	default:
		if (PIB_LOW_HALF(addr)) {
			/*lower half */
			if (length != 8)
				/*panic_domain(NULL,
				"Can't LHF write with size %ld!\n",
				length);*/
				panic_vm(v);
			else
				vlsapic_write_ipi(v, addr, val);
		} else {   /*	upper half
				printk("IPI-UHF write %lx\n",addr);*/
			panic_vm(v);
		}
		break;
	}
}

unsigned long lsapic_read(struct kvm_vcpu *v, unsigned long addr,
		unsigned long length)
{
	uint64_t result = 0;

	addr &= (PIB_SIZE - 1);

	switch (addr) {
	case PIB_OFST_INTA:
		if (length == 1) /* 1 byte load */
			; /* There is no i8259, there is no INTA access*/
		else
			/*panic_domain(NULL,"Undefined read on PIB INTA\n"); */
			panic_vm(v);

		break;
	case PIB_OFST_XTP:
		if (length == 1) {
			result = VLSAPIC_XTP(v);
			/* printk("read xtp %lx\n", result); */
		} else {
			/*panic_domain(NULL,
			"Undefined read on PIB XTP\n");*/
			panic_vm(v);
		}
		break;
	default:
		panic_vm(v);
		break;
	}
	return result;
}

static void mmio_access(struct kvm_vcpu *vcpu, u64 src_pa, u64 *dest,
					u16 s, int ma, int dir)
{
	unsigned long iot;
	struct exit_ctl_data *p = &vcpu->arch.exit_data;
	unsigned long psr;

	iot = __gpfn_is_io(src_pa >> PAGE_SHIFT);

	local_irq_save(psr);

	/*Intercept the acces for PIB range*/
	if (iot == GPFN_PIB) {
		if (!dir)
			lsapic_write(vcpu, src_pa, s, *dest);
		else
			*dest = lsapic_read(vcpu, src_pa, s);
		goto out;
	}
	p->exit_reason = EXIT_REASON_MMIO_INSTRUCTION;
	p->u.ioreq.addr = src_pa;
	p->u.ioreq.size = s;
	p->u.ioreq.dir = dir;
	if (dir == IOREQ_WRITE)
		p->u.ioreq.data = *dest;
	p->u.ioreq.state = STATE_IOREQ_READY;
	vmm_transition(vcpu);

	if (p->u.ioreq.state == STATE_IORESP_READY) {
		if (dir == IOREQ_READ)
			*dest = p->u.ioreq.data;
	} else
		panic_vm(vcpu);
out:
	local_irq_restore(psr);
	return ;
}

/*
   dir 1: read 0:write
   inst_type 0:integer 1:floating point
 */
#define SL_INTEGER	0	/* store/load interger*/
#define SL_FLOATING	1     	/* store/load floating*/

void emulate_io_inst(struct kvm_vcpu *vcpu, u64 padr, u64 ma)
{
	struct kvm_pt_regs *regs;
	IA64_BUNDLE bundle;
	int slot, dir = 0;
	int inst_type = -1;
	u16 size = 0;
	u64 data, slot1a, slot1b, temp, update_reg;
	s32 imm;
	INST64 inst;

	regs = vcpu_regs(vcpu);

	if (fetch_code(vcpu, regs->cr_iip, &bundle)) {
		/* if fetch code fail, return and try again */
		return;
	}
	slot = ((struct ia64_psr *)&(regs->cr_ipsr))->ri;
	if (!slot)
		inst.inst = bundle.slot0;
	else if (slot == 1) {
		slot1a = bundle.slot1a;
		slot1b = bundle.slot1b;
		inst.inst = slot1a + (slot1b << 18);
	} else if (slot == 2)
		inst.inst = bundle.slot2;

	/* Integer Load/Store */
	if (inst.M1.major == 4 && inst.M1.m == 0 && inst.M1.x == 0) {
		inst_type = SL_INTEGER;
		size = (inst.M1.x6 & 0x3);
		if ((inst.M1.x6 >> 2) > 0xb) {
			/*write*/
			dir = IOREQ_WRITE;
			data = vcpu_get_gr(vcpu, inst.M4.r2);
		} else if ((inst.M1.x6 >> 2) < 0xb) {
			/*read*/
			dir = IOREQ_READ;
		}
	} else if (inst.M2.major == 4 && inst.M2.m == 1 && inst.M2.x == 0) {
		/* Integer Load + Reg update */
		inst_type = SL_INTEGER;
		dir = IOREQ_READ;
		size = (inst.M2.x6 & 0x3);
		temp = vcpu_get_gr(vcpu, inst.M2.r3);
		update_reg = vcpu_get_gr(vcpu, inst.M2.r2);
		temp += update_reg;
		vcpu_set_gr(vcpu, inst.M2.r3, temp, 0);
	} else if (inst.M3.major == 5) {
		/*Integer Load/Store + Imm update*/
		inst_type = SL_INTEGER;
		size = (inst.M3.x6&0x3);
		if ((inst.M5.x6 >> 2) > 0xb) {
			/*write*/
			dir = IOREQ_WRITE;
			data = vcpu_get_gr(vcpu, inst.M5.r2);
			temp = vcpu_get_gr(vcpu, inst.M5.r3);
			imm = (inst.M5.s << 31) | (inst.M5.i << 30) |
				(inst.M5.imm7 << 23);
			temp += imm >> 23;
			vcpu_set_gr(vcpu, inst.M5.r3, temp, 0);

		} else if ((inst.M3.x6 >> 2) < 0xb) {
			/*read*/
			dir = IOREQ_READ;
			temp = vcpu_get_gr(vcpu, inst.M3.r3);
			imm = (inst.M3.s << 31) | (inst.M3.i << 30) |
				(inst.M3.imm7 << 23);
			temp += imm >> 23;
			vcpu_set_gr(vcpu, inst.M3.r3, temp, 0);

		}
	} else if (inst.M9.major == 6 && inst.M9.x6 == 0x3B
				&& inst.M9.m == 0 && inst.M9.x == 0) {
		/* Floating-point spill*/
		struct ia64_fpreg v;

		inst_type = SL_FLOATING;
		dir = IOREQ_WRITE;
		vcpu_get_fpreg(vcpu, inst.M9.f2, &v);
		/* Write high word. FIXME: this is a kludge!  */
		v.u.bits[1] &= 0x3ffff;
		mmio_access(vcpu, padr + 8, &v.u.bits[1], 8, ma, IOREQ_WRITE);
		data = v.u.bits[0];
		size = 3;
	} else if (inst.M10.major == 7 && inst.M10.x6 == 0x3B) {
		/* Floating-point spill + Imm update */
		struct ia64_fpreg v;

		inst_type = SL_FLOATING;
		dir = IOREQ_WRITE;
		vcpu_get_fpreg(vcpu, inst.M10.f2, &v);
		temp = vcpu_get_gr(vcpu, inst.M10.r3);
		imm = (inst.M10.s << 31) | (inst.M10.i << 30) |
			(inst.M10.imm7 << 23);
		temp += imm >> 23;
		vcpu_set_gr(vcpu, inst.M10.r3, temp, 0);

		/* Write high word.FIXME: this is a kludge!  */
		v.u.bits[1] &= 0x3ffff;
		mmio_access(vcpu, padr + 8, &v.u.bits[1], 8, ma, IOREQ_WRITE);
		data = v.u.bits[0];
		size = 3;
	} else if (inst.M10.major == 7 && inst.M10.x6 == 0x31) {
		/* Floating-point stf8 + Imm update */
		struct ia64_fpreg v;
		inst_type = SL_FLOATING;
		dir = IOREQ_WRITE;
		size = 3;
		vcpu_get_fpreg(vcpu, inst.M10.f2, &v);
		data = v.u.bits[0]; /* Significand.  */
		temp = vcpu_get_gr(vcpu, inst.M10.r3);
		imm = (inst.M10.s << 31) | (inst.M10.i << 30) |
			(inst.M10.imm7 << 23);
		temp += imm >> 23;
		vcpu_set_gr(vcpu, inst.M10.r3, temp, 0);
	} else if (inst.M15.major == 7 && inst.M15.x6 >= 0x2c
			&& inst.M15.x6 <= 0x2f) {
		temp = vcpu_get_gr(vcpu, inst.M15.r3);
		imm = (inst.M15.s << 31) | (inst.M15.i << 30) |
			(inst.M15.imm7 << 23);
		temp += imm >> 23;
		vcpu_set_gr(vcpu, inst.M15.r3, temp, 0);

		vcpu_increment_iip(vcpu);
		return;
	} else if (inst.M12.major == 6 && inst.M12.m == 1
			&& inst.M12.x == 1 && inst.M12.x6 == 1) {
		/* Floating-point Load Pair + Imm ldfp8 M12*/
		struct ia64_fpreg v;

		inst_type = SL_FLOATING;
		dir = IOREQ_READ;
		size = 8;     /*ldfd*/
		mmio_access(vcpu, padr, &data, size, ma, dir);
		v.u.bits[0] = data;
		v.u.bits[1] = 0x1003E;
		vcpu_set_fpreg(vcpu, inst.M12.f1, &v);
		padr += 8;
		mmio_access(vcpu, padr, &data, size, ma, dir);
		v.u.bits[0] = data;
		v.u.bits[1] = 0x1003E;
		vcpu_set_fpreg(vcpu, inst.M12.f2, &v);
		padr += 8;
		vcpu_set_gr(vcpu, inst.M12.r3, padr, 0);
		vcpu_increment_iip(vcpu);
		return;
	} else {
		inst_type = -1;
		panic_vm(vcpu);
	}

	size = 1 << size;
	if (dir == IOREQ_WRITE) {
		mmio_access(vcpu, padr, &data, size, ma, dir);
	} else {
		mmio_access(vcpu, padr, &data, size, ma, dir);
		if (inst_type == SL_INTEGER)
			vcpu_set_gr(vcpu, inst.M1.r1, data, 0);
		else
			panic_vm(vcpu);

	}
	vcpu_increment_iip(vcpu);
}
