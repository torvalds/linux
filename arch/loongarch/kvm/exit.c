// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kvm_host.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/vmalloc.h>
#include <asm/fpu.h>
#include <asm/inst.h>
#include <asm/loongarch.h>
#include <asm/mmzone.h>
#include <asm/numa.h>
#include <asm/time.h>
#include <asm/tlb.h>
#include <asm/kvm_csr.h>
#include <asm/kvm_vcpu.h>
#include "trace.h"

static unsigned long kvm_emu_read_csr(struct kvm_vcpu *vcpu, int csrid)
{
	unsigned long val = 0;
	struct loongarch_csrs *csr = vcpu->arch.csr;

	/*
	 * From LoongArch Reference Manual Volume 1 Chapter 4.2.1
	 * For undefined CSR id, return value is 0
	 */
	if (get_gcsr_flag(csrid) & SW_GCSR)
		val = kvm_read_sw_gcsr(csr, csrid);
	else
		pr_warn_once("Unsupported csrrd 0x%x with pc %lx\n", csrid, vcpu->arch.pc);

	return val;
}

static unsigned long kvm_emu_write_csr(struct kvm_vcpu *vcpu, int csrid, unsigned long val)
{
	unsigned long old = 0;
	struct loongarch_csrs *csr = vcpu->arch.csr;

	if (get_gcsr_flag(csrid) & SW_GCSR) {
		old = kvm_read_sw_gcsr(csr, csrid);
		kvm_write_sw_gcsr(csr, csrid, val);
	} else
		pr_warn_once("Unsupported csrwr 0x%x with pc %lx\n", csrid, vcpu->arch.pc);

	return old;
}

static unsigned long kvm_emu_xchg_csr(struct kvm_vcpu *vcpu, int csrid,
				unsigned long csr_mask, unsigned long val)
{
	unsigned long old = 0;
	struct loongarch_csrs *csr = vcpu->arch.csr;

	if (get_gcsr_flag(csrid) & SW_GCSR) {
		old = kvm_read_sw_gcsr(csr, csrid);
		val = (old & ~csr_mask) | (val & csr_mask);
		kvm_write_sw_gcsr(csr, csrid, val);
		old = old & csr_mask;
	} else
		pr_warn_once("Unsupported csrxchg 0x%x with pc %lx\n", csrid, vcpu->arch.pc);

	return old;
}

static int kvm_handle_csr(struct kvm_vcpu *vcpu, larch_inst inst)
{
	unsigned int rd, rj, csrid;
	unsigned long csr_mask, val = 0;

	/*
	 * CSR value mask imm
	 * rj = 0 means csrrd
	 * rj = 1 means csrwr
	 * rj != 0,1 means csrxchg
	 */
	rd = inst.reg2csr_format.rd;
	rj = inst.reg2csr_format.rj;
	csrid = inst.reg2csr_format.csr;

	/* Process CSR ops */
	switch (rj) {
	case 0: /* process csrrd */
		val = kvm_emu_read_csr(vcpu, csrid);
		vcpu->arch.gprs[rd] = val;
		break;
	case 1: /* process csrwr */
		val = vcpu->arch.gprs[rd];
		val = kvm_emu_write_csr(vcpu, csrid, val);
		vcpu->arch.gprs[rd] = val;
		break;
	default: /* process csrxchg */
		val = vcpu->arch.gprs[rd];
		csr_mask = vcpu->arch.gprs[rj];
		val = kvm_emu_xchg_csr(vcpu, csrid, csr_mask, val);
		vcpu->arch.gprs[rd] = val;
	}

	return EMULATE_DONE;
}

int kvm_emu_iocsr(larch_inst inst, struct kvm_run *run, struct kvm_vcpu *vcpu)
{
	int ret;
	unsigned long val;
	u32 addr, rd, rj, opcode;

	/*
	 * Each IOCSR with different opcode
	 */
	rd = inst.reg2_format.rd;
	rj = inst.reg2_format.rj;
	opcode = inst.reg2_format.opcode;
	addr = vcpu->arch.gprs[rj];
	ret = EMULATE_DO_IOCSR;
	run->iocsr_io.phys_addr = addr;
	run->iocsr_io.is_write = 0;

	/* LoongArch is Little endian */
	switch (opcode) {
	case iocsrrdb_op:
		run->iocsr_io.len = 1;
		break;
	case iocsrrdh_op:
		run->iocsr_io.len = 2;
		break;
	case iocsrrdw_op:
		run->iocsr_io.len = 4;
		break;
	case iocsrrdd_op:
		run->iocsr_io.len = 8;
		break;
	case iocsrwrb_op:
		run->iocsr_io.len = 1;
		run->iocsr_io.is_write = 1;
		break;
	case iocsrwrh_op:
		run->iocsr_io.len = 2;
		run->iocsr_io.is_write = 1;
		break;
	case iocsrwrw_op:
		run->iocsr_io.len = 4;
		run->iocsr_io.is_write = 1;
		break;
	case iocsrwrd_op:
		run->iocsr_io.len = 8;
		run->iocsr_io.is_write = 1;
		break;
	default:
		ret = EMULATE_FAIL;
		break;
	}

	if (ret == EMULATE_DO_IOCSR) {
		if (run->iocsr_io.is_write) {
			val = vcpu->arch.gprs[rd];
			memcpy(run->iocsr_io.data, &val, run->iocsr_io.len);
		}
		vcpu->arch.io_gpr = rd;
	}

	return ret;
}

int kvm_complete_iocsr_read(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	enum emulation_result er = EMULATE_DONE;
	unsigned long *gpr = &vcpu->arch.gprs[vcpu->arch.io_gpr];

	switch (run->iocsr_io.len) {
	case 1:
		*gpr = *(s8 *)run->iocsr_io.data;
		break;
	case 2:
		*gpr = *(s16 *)run->iocsr_io.data;
		break;
	case 4:
		*gpr = *(s32 *)run->iocsr_io.data;
		break;
	case 8:
		*gpr = *(s64 *)run->iocsr_io.data;
		break;
	default:
		kvm_err("Bad IOCSR length: %d, addr is 0x%lx\n",
				run->iocsr_io.len, vcpu->arch.badv);
		er = EMULATE_FAIL;
		break;
	}

	return er;
}

int kvm_emu_idle(struct kvm_vcpu *vcpu)
{
	++vcpu->stat.idle_exits;
	trace_kvm_exit_idle(vcpu, KVM_TRACE_EXIT_IDLE);

	if (!kvm_arch_vcpu_runnable(vcpu)) {
		/*
		 * Switch to the software timer before halt-polling/blocking as
		 * the guest's timer may be a break event for the vCPU, and the
		 * hypervisor timer runs only when the CPU is in guest mode.
		 * Switch before halt-polling so that KVM recognizes an expired
		 * timer before blocking.
		 */
		kvm_save_timer(vcpu);
		kvm_vcpu_block(vcpu);
	}

	return EMULATE_DONE;
}

static int kvm_trap_handle_gspr(struct kvm_vcpu *vcpu)
{
	int rd, rj;
	unsigned int index;
	unsigned long curr_pc;
	larch_inst inst;
	enum emulation_result er = EMULATE_DONE;
	struct kvm_run *run = vcpu->run;

	/* Fetch the instruction */
	inst.word = vcpu->arch.badi;
	curr_pc = vcpu->arch.pc;
	update_pc(&vcpu->arch);

	trace_kvm_exit_gspr(vcpu, inst.word);
	er = EMULATE_FAIL;
	switch (((inst.word >> 24) & 0xff)) {
	case 0x0: /* CPUCFG GSPR */
		if (inst.reg2_format.opcode == 0x1B) {
			rd = inst.reg2_format.rd;
			rj = inst.reg2_format.rj;
			++vcpu->stat.cpucfg_exits;
			index = vcpu->arch.gprs[rj];
			er = EMULATE_DONE;
			/*
			 * By LoongArch Reference Manual 2.2.10.5
			 * return value is 0 for undefined cpucfg index
			 */
			if (index < KVM_MAX_CPUCFG_REGS)
				vcpu->arch.gprs[rd] = vcpu->arch.cpucfg[index];
			else
				vcpu->arch.gprs[rd] = 0;
		}
		break;
	case 0x4: /* CSR{RD,WR,XCHG} GSPR */
		er = kvm_handle_csr(vcpu, inst);
		break;
	case 0x6: /* Cache, Idle and IOCSR GSPR */
		switch (((inst.word >> 22) & 0x3ff)) {
		case 0x18: /* Cache GSPR */
			er = EMULATE_DONE;
			trace_kvm_exit_cache(vcpu, KVM_TRACE_EXIT_CACHE);
			break;
		case 0x19: /* Idle/IOCSR GSPR */
			switch (((inst.word >> 15) & 0x1ffff)) {
			case 0xc90: /* IOCSR GSPR */
				er = kvm_emu_iocsr(inst, run, vcpu);
				break;
			case 0xc91: /* Idle GSPR */
				er = kvm_emu_idle(vcpu);
				break;
			default:
				er = EMULATE_FAIL;
				break;
			}
			break;
		default:
			er = EMULATE_FAIL;
			break;
		}
		break;
	default:
		er = EMULATE_FAIL;
		break;
	}

	/* Rollback PC only if emulation was unsuccessful */
	if (er == EMULATE_FAIL) {
		kvm_err("[%#lx]%s: unsupported gspr instruction 0x%08x\n",
			curr_pc, __func__, inst.word);

		kvm_arch_vcpu_dump_regs(vcpu);
		vcpu->arch.pc = curr_pc;
	}

	return er;
}

/*
 * Trigger GSPR:
 * 1) Execute CPUCFG instruction;
 * 2) Execute CACOP/IDLE instructions;
 * 3) Access to unimplemented CSRs/IOCSRs.
 */
static int kvm_handle_gspr(struct kvm_vcpu *vcpu)
{
	int ret = RESUME_GUEST;
	enum emulation_result er = EMULATE_DONE;

	er = kvm_trap_handle_gspr(vcpu);

	if (er == EMULATE_DONE) {
		ret = RESUME_GUEST;
	} else if (er == EMULATE_DO_MMIO) {
		vcpu->run->exit_reason = KVM_EXIT_MMIO;
		ret = RESUME_HOST;
	} else if (er == EMULATE_DO_IOCSR) {
		vcpu->run->exit_reason = KVM_EXIT_LOONGARCH_IOCSR;
		ret = RESUME_HOST;
	} else {
		kvm_queue_exception(vcpu, EXCCODE_INE, 0);
		ret = RESUME_GUEST;
	}

	return ret;
}
