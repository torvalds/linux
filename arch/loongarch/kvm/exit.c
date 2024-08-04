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
#include <trace/events/kvm.h>
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

static int kvm_emu_cpucfg(struct kvm_vcpu *vcpu, larch_inst inst)
{
	int rd, rj;
	unsigned int index, ret;

	if (inst.reg2_format.opcode != cpucfg_op)
		return EMULATE_FAIL;

	rd = inst.reg2_format.rd;
	rj = inst.reg2_format.rj;
	++vcpu->stat.cpucfg_exits;
	index = vcpu->arch.gprs[rj];

	/*
	 * By LoongArch Reference Manual 2.2.10.5
	 * Return value is 0 for undefined CPUCFG index
	 *
	 * Disable preemption since hw gcsr is accessed
	 */
	preempt_disable();
	switch (index) {
	case 0 ... (KVM_MAX_CPUCFG_REGS - 1):
		vcpu->arch.gprs[rd] = vcpu->arch.cpucfg[index];
		break;
	case CPUCFG_KVM_SIG:
		/* CPUCFG emulation between 0x40000000 -- 0x400000ff */
		vcpu->arch.gprs[rd] = *(unsigned int *)KVM_SIGNATURE;
		break;
	case CPUCFG_KVM_FEATURE:
		ret = KVM_FEATURE_IPI;
		if (kvm_pvtime_supported())
			ret |= KVM_FEATURE_STEAL_TIME;
		vcpu->arch.gprs[rd] = ret;
		break;
	default:
		vcpu->arch.gprs[rd] = 0;
		break;
	}
	preempt_enable();

	return EMULATE_DONE;
}

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

	if (!kvm_arch_vcpu_runnable(vcpu))
		kvm_vcpu_halt(vcpu);

	return EMULATE_DONE;
}

static int kvm_trap_handle_gspr(struct kvm_vcpu *vcpu)
{
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
		er = kvm_emu_cpucfg(vcpu, inst);
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

int kvm_emu_mmio_read(struct kvm_vcpu *vcpu, larch_inst inst)
{
	int ret;
	unsigned int op8, opcode, rd;
	struct kvm_run *run = vcpu->run;

	run->mmio.phys_addr = vcpu->arch.badv;
	vcpu->mmio_needed = 2;	/* signed */
	op8 = (inst.word >> 24) & 0xff;
	ret = EMULATE_DO_MMIO;

	switch (op8) {
	case 0x24 ... 0x27:	/* ldptr.w/d process */
		rd = inst.reg2i14_format.rd;
		opcode = inst.reg2i14_format.opcode;

		switch (opcode) {
		case ldptrw_op:
			run->mmio.len = 4;
			break;
		case ldptrd_op:
			run->mmio.len = 8;
			break;
		default:
			break;
		}
		break;
	case 0x28 ... 0x2e:	/* ld.b/h/w/d, ld.bu/hu/wu process */
		rd = inst.reg2i12_format.rd;
		opcode = inst.reg2i12_format.opcode;

		switch (opcode) {
		case ldb_op:
			run->mmio.len = 1;
			break;
		case ldbu_op:
			vcpu->mmio_needed = 1;	/* unsigned */
			run->mmio.len = 1;
			break;
		case ldh_op:
			run->mmio.len = 2;
			break;
		case ldhu_op:
			vcpu->mmio_needed = 1;	/* unsigned */
			run->mmio.len = 2;
			break;
		case ldw_op:
			run->mmio.len = 4;
			break;
		case ldwu_op:
			vcpu->mmio_needed = 1;	/* unsigned */
			run->mmio.len = 4;
			break;
		case ldd_op:
			run->mmio.len = 8;
			break;
		default:
			ret = EMULATE_FAIL;
			break;
		}
		break;
	case 0x38:	/* ldx.b/h/w/d, ldx.bu/hu/wu process */
		rd = inst.reg3_format.rd;
		opcode = inst.reg3_format.opcode;

		switch (opcode) {
		case ldxb_op:
			run->mmio.len = 1;
			break;
		case ldxbu_op:
			run->mmio.len = 1;
			vcpu->mmio_needed = 1;	/* unsigned */
			break;
		case ldxh_op:
			run->mmio.len = 2;
			break;
		case ldxhu_op:
			run->mmio.len = 2;
			vcpu->mmio_needed = 1;	/* unsigned */
			break;
		case ldxw_op:
			run->mmio.len = 4;
			break;
		case ldxwu_op:
			run->mmio.len = 4;
			vcpu->mmio_needed = 1;	/* unsigned */
			break;
		case ldxd_op:
			run->mmio.len = 8;
			break;
		default:
			ret = EMULATE_FAIL;
			break;
		}
		break;
	default:
		ret = EMULATE_FAIL;
	}

	if (ret == EMULATE_DO_MMIO) {
		/* Set for kvm_complete_mmio_read() use */
		vcpu->arch.io_gpr = rd;
		run->mmio.is_write = 0;
		vcpu->mmio_is_write = 0;
		trace_kvm_mmio(KVM_TRACE_MMIO_READ_UNSATISFIED, run->mmio.len,
				run->mmio.phys_addr, NULL);
	} else {
		kvm_err("Read not supported Inst=0x%08x @%lx BadVaddr:%#lx\n",
			inst.word, vcpu->arch.pc, vcpu->arch.badv);
		kvm_arch_vcpu_dump_regs(vcpu);
		vcpu->mmio_needed = 0;
	}

	return ret;
}

int kvm_complete_mmio_read(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	enum emulation_result er = EMULATE_DONE;
	unsigned long *gpr = &vcpu->arch.gprs[vcpu->arch.io_gpr];

	/* Update with new PC */
	update_pc(&vcpu->arch);
	switch (run->mmio.len) {
	case 1:
		if (vcpu->mmio_needed == 2)
			*gpr = *(s8 *)run->mmio.data;
		else
			*gpr = *(u8 *)run->mmio.data;
		break;
	case 2:
		if (vcpu->mmio_needed == 2)
			*gpr = *(s16 *)run->mmio.data;
		else
			*gpr = *(u16 *)run->mmio.data;
		break;
	case 4:
		if (vcpu->mmio_needed == 2)
			*gpr = *(s32 *)run->mmio.data;
		else
			*gpr = *(u32 *)run->mmio.data;
		break;
	case 8:
		*gpr = *(s64 *)run->mmio.data;
		break;
	default:
		kvm_err("Bad MMIO length: %d, addr is 0x%lx\n",
				run->mmio.len, vcpu->arch.badv);
		er = EMULATE_FAIL;
		break;
	}

	trace_kvm_mmio(KVM_TRACE_MMIO_READ, run->mmio.len,
			run->mmio.phys_addr, run->mmio.data);

	return er;
}

int kvm_emu_mmio_write(struct kvm_vcpu *vcpu, larch_inst inst)
{
	int ret;
	unsigned int rd, op8, opcode;
	unsigned long curr_pc, rd_val = 0;
	struct kvm_run *run = vcpu->run;
	void *data = run->mmio.data;

	/*
	 * Update PC and hold onto current PC in case there is
	 * an error and we want to rollback the PC
	 */
	curr_pc = vcpu->arch.pc;
	update_pc(&vcpu->arch);

	op8 = (inst.word >> 24) & 0xff;
	run->mmio.phys_addr = vcpu->arch.badv;
	ret = EMULATE_DO_MMIO;
	switch (op8) {
	case 0x24 ... 0x27:	/* stptr.w/d process */
		rd = inst.reg2i14_format.rd;
		opcode = inst.reg2i14_format.opcode;

		switch (opcode) {
		case stptrw_op:
			run->mmio.len = 4;
			*(unsigned int *)data = vcpu->arch.gprs[rd];
			break;
		case stptrd_op:
			run->mmio.len = 8;
			*(unsigned long *)data = vcpu->arch.gprs[rd];
			break;
		default:
			ret = EMULATE_FAIL;
			break;
		}
		break;
	case 0x28 ... 0x2e:	/* st.b/h/w/d  process */
		rd = inst.reg2i12_format.rd;
		opcode = inst.reg2i12_format.opcode;
		rd_val = vcpu->arch.gprs[rd];

		switch (opcode) {
		case stb_op:
			run->mmio.len = 1;
			*(unsigned char *)data = rd_val;
			break;
		case sth_op:
			run->mmio.len = 2;
			*(unsigned short *)data = rd_val;
			break;
		case stw_op:
			run->mmio.len = 4;
			*(unsigned int *)data = rd_val;
			break;
		case std_op:
			run->mmio.len = 8;
			*(unsigned long *)data = rd_val;
			break;
		default:
			ret = EMULATE_FAIL;
			break;
		}
		break;
	case 0x38:	/* stx.b/h/w/d process */
		rd = inst.reg3_format.rd;
		opcode = inst.reg3_format.opcode;

		switch (opcode) {
		case stxb_op:
			run->mmio.len = 1;
			*(unsigned char *)data = vcpu->arch.gprs[rd];
			break;
		case stxh_op:
			run->mmio.len = 2;
			*(unsigned short *)data = vcpu->arch.gprs[rd];
			break;
		case stxw_op:
			run->mmio.len = 4;
			*(unsigned int *)data = vcpu->arch.gprs[rd];
			break;
		case stxd_op:
			run->mmio.len = 8;
			*(unsigned long *)data = vcpu->arch.gprs[rd];
			break;
		default:
			ret = EMULATE_FAIL;
			break;
		}
		break;
	default:
		ret = EMULATE_FAIL;
	}

	if (ret == EMULATE_DO_MMIO) {
		run->mmio.is_write = 1;
		vcpu->mmio_needed = 1;
		vcpu->mmio_is_write = 1;
		trace_kvm_mmio(KVM_TRACE_MMIO_WRITE, run->mmio.len,
				run->mmio.phys_addr, data);
	} else {
		vcpu->arch.pc = curr_pc;
		kvm_err("Write not supported Inst=0x%08x @%lx BadVaddr:%#lx\n",
			inst.word, vcpu->arch.pc, vcpu->arch.badv);
		kvm_arch_vcpu_dump_regs(vcpu);
		/* Rollback PC if emulation was unsuccessful */
	}

	return ret;
}

static int kvm_handle_rdwr_fault(struct kvm_vcpu *vcpu, bool write)
{
	int ret;
	larch_inst inst;
	enum emulation_result er = EMULATE_DONE;
	struct kvm_run *run = vcpu->run;
	unsigned long badv = vcpu->arch.badv;

	ret = kvm_handle_mm_fault(vcpu, badv, write);
	if (ret) {
		/* Treat as MMIO */
		inst.word = vcpu->arch.badi;
		if (write) {
			er = kvm_emu_mmio_write(vcpu, inst);
		} else {
			/* A code fetch fault doesn't count as an MMIO */
			if (kvm_is_ifetch_fault(&vcpu->arch)) {
				kvm_queue_exception(vcpu, EXCCODE_ADE, EXSUBCODE_ADEF);
				return RESUME_GUEST;
			}

			er = kvm_emu_mmio_read(vcpu, inst);
		}
	}

	if (er == EMULATE_DONE) {
		ret = RESUME_GUEST;
	} else if (er == EMULATE_DO_MMIO) {
		run->exit_reason = KVM_EXIT_MMIO;
		ret = RESUME_HOST;
	} else {
		kvm_queue_exception(vcpu, EXCCODE_ADE, EXSUBCODE_ADEM);
		ret = RESUME_GUEST;
	}

	return ret;
}

static int kvm_handle_read_fault(struct kvm_vcpu *vcpu)
{
	return kvm_handle_rdwr_fault(vcpu, false);
}

static int kvm_handle_write_fault(struct kvm_vcpu *vcpu)
{
	return kvm_handle_rdwr_fault(vcpu, true);
}

/**
 * kvm_handle_fpu_disabled() - Guest used fpu however it is disabled at host
 * @vcpu:	Virtual CPU context.
 *
 * Handle when the guest attempts to use fpu which hasn't been allowed
 * by the root context.
 */
static int kvm_handle_fpu_disabled(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;

	if (!kvm_guest_has_fpu(&vcpu->arch)) {
		kvm_queue_exception(vcpu, EXCCODE_INE, 0);
		return RESUME_GUEST;
	}

	/*
	 * If guest FPU not present, the FPU operation should have been
	 * treated as a reserved instruction!
	 * If FPU already in use, we shouldn't get this at all.
	 */
	if (WARN_ON(vcpu->arch.aux_inuse & KVM_LARCH_FPU)) {
		kvm_err("%s internal error\n", __func__);
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		return RESUME_HOST;
	}

	kvm_own_fpu(vcpu);

	return RESUME_GUEST;
}

static long kvm_save_notify(struct kvm_vcpu *vcpu)
{
	unsigned long id, data;

	id   = kvm_read_reg(vcpu, LOONGARCH_GPR_A1);
	data = kvm_read_reg(vcpu, LOONGARCH_GPR_A2);
	switch (id) {
	case KVM_FEATURE_STEAL_TIME:
		if (!kvm_pvtime_supported())
			return KVM_HCALL_INVALID_CODE;

		if (data & ~(KVM_STEAL_PHYS_MASK | KVM_STEAL_PHYS_VALID))
			return KVM_HCALL_INVALID_PARAMETER;

		vcpu->arch.st.guest_addr = data;
		if (!(data & KVM_STEAL_PHYS_VALID))
			break;

		vcpu->arch.st.last_steal = current->sched_info.run_delay;
		kvm_make_request(KVM_REQ_STEAL_UPDATE, vcpu);
		break;
	default:
		break;
	};

	return 0;
};

/*
 * kvm_handle_lsx_disabled() - Guest used LSX while disabled in root.
 * @vcpu:      Virtual CPU context.
 *
 * Handle when the guest attempts to use LSX when it is disabled in the root
 * context.
 */
static int kvm_handle_lsx_disabled(struct kvm_vcpu *vcpu)
{
	if (kvm_own_lsx(vcpu))
		kvm_queue_exception(vcpu, EXCCODE_INE, 0);

	return RESUME_GUEST;
}

/*
 * kvm_handle_lasx_disabled() - Guest used LASX while disabled in root.
 * @vcpu:	Virtual CPU context.
 *
 * Handle when the guest attempts to use LASX when it is disabled in the root
 * context.
 */
static int kvm_handle_lasx_disabled(struct kvm_vcpu *vcpu)
{
	if (kvm_own_lasx(vcpu))
		kvm_queue_exception(vcpu, EXCCODE_INE, 0);

	return RESUME_GUEST;
}

static int kvm_send_pv_ipi(struct kvm_vcpu *vcpu)
{
	unsigned int min, cpu, i;
	unsigned long ipi_bitmap;
	struct kvm_vcpu *dest;

	min = kvm_read_reg(vcpu, LOONGARCH_GPR_A3);
	for (i = 0; i < 2; i++, min += BITS_PER_LONG) {
		ipi_bitmap = kvm_read_reg(vcpu, LOONGARCH_GPR_A1 + i);
		if (!ipi_bitmap)
			continue;

		cpu = find_first_bit((void *)&ipi_bitmap, BITS_PER_LONG);
		while (cpu < BITS_PER_LONG) {
			dest = kvm_get_vcpu_by_cpuid(vcpu->kvm, cpu + min);
			cpu = find_next_bit((void *)&ipi_bitmap, BITS_PER_LONG, cpu + 1);
			if (!dest)
				continue;

			/* Send SWI0 to dest vcpu to emulate IPI interrupt */
			kvm_queue_irq(dest, INT_SWI0);
			kvm_vcpu_kick(dest);
		}
	}

	return 0;
}

/*
 * Hypercall emulation always return to guest, Caller should check retval.
 */
static void kvm_handle_service(struct kvm_vcpu *vcpu)
{
	unsigned long func = kvm_read_reg(vcpu, LOONGARCH_GPR_A0);
	long ret;

	switch (func) {
	case KVM_HCALL_FUNC_IPI:
		kvm_send_pv_ipi(vcpu);
		ret = KVM_HCALL_SUCCESS;
		break;
	case KVM_HCALL_FUNC_NOTIFY:
		ret = kvm_save_notify(vcpu);
		break;
	default:
		ret = KVM_HCALL_INVALID_CODE;
		break;
	}

	kvm_write_reg(vcpu, LOONGARCH_GPR_A0, ret);
}

static int kvm_handle_hypercall(struct kvm_vcpu *vcpu)
{
	int ret;
	larch_inst inst;
	unsigned int code;

	inst.word = vcpu->arch.badi;
	code = inst.reg0i15_format.immediate;
	ret = RESUME_GUEST;

	switch (code) {
	case KVM_HCALL_SERVICE:
		vcpu->stat.hypercall_exits++;
		kvm_handle_service(vcpu);
		break;
	case KVM_HCALL_SWDBG:
		/* KVM_HCALL_SWDBG only in effective when SW_BP is enabled */
		if (vcpu->guest_debug & KVM_GUESTDBG_SW_BP_MASK) {
			vcpu->run->exit_reason = KVM_EXIT_DEBUG;
			ret = RESUME_HOST;
			break;
		}
		fallthrough;
	default:
		/* Treat it as noop intruction, only set return value */
		kvm_write_reg(vcpu, LOONGARCH_GPR_A0, KVM_HCALL_INVALID_CODE);
		break;
	}

	if (ret == RESUME_GUEST)
		update_pc(&vcpu->arch);

	return ret;
}

/*
 * LoongArch KVM callback handling for unimplemented guest exiting
 */
static int kvm_fault_ni(struct kvm_vcpu *vcpu)
{
	unsigned int ecode, inst;
	unsigned long estat, badv;

	/* Fetch the instruction */
	inst = vcpu->arch.badi;
	badv = vcpu->arch.badv;
	estat = vcpu->arch.host_estat;
	ecode = (estat & CSR_ESTAT_EXC) >> CSR_ESTAT_EXC_SHIFT;
	kvm_err("ECode: %d PC=%#lx Inst=0x%08x BadVaddr=%#lx ESTAT=%#lx\n",
			ecode, vcpu->arch.pc, inst, badv, read_gcsr_estat());
	kvm_arch_vcpu_dump_regs(vcpu);
	kvm_queue_exception(vcpu, EXCCODE_INE, 0);

	return RESUME_GUEST;
}

static exit_handle_fn kvm_fault_tables[EXCCODE_INT_START] = {
	[0 ... EXCCODE_INT_START - 1]	= kvm_fault_ni,
	[EXCCODE_TLBI]			= kvm_handle_read_fault,
	[EXCCODE_TLBL]			= kvm_handle_read_fault,
	[EXCCODE_TLBS]			= kvm_handle_write_fault,
	[EXCCODE_TLBM]			= kvm_handle_write_fault,
	[EXCCODE_FPDIS]			= kvm_handle_fpu_disabled,
	[EXCCODE_LSXDIS]		= kvm_handle_lsx_disabled,
	[EXCCODE_LASXDIS]		= kvm_handle_lasx_disabled,
	[EXCCODE_GSPR]			= kvm_handle_gspr,
	[EXCCODE_HVC]			= kvm_handle_hypercall,
};

int kvm_handle_fault(struct kvm_vcpu *vcpu, int fault)
{
	return kvm_fault_tables[fault](vcpu);
}
