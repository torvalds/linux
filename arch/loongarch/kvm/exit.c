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
