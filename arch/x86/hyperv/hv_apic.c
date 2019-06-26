// SPDX-License-Identifier: GPL-2.0

/*
 * Hyper-V specific APIC code.
 *
 * Copyright (C) 2018, Microsoft, Inc.
 *
 * Author : K. Y. Srinivasan <kys@microsoft.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 */

#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/clockchips.h>
#include <linux/hyperv.h>
#include <linux/slab.h>
#include <linux/cpuhotplug.h>
#include <asm/hypervisor.h>
#include <asm/mshyperv.h>
#include <asm/apic.h>

#include <asm/trace/hyperv.h>

static struct apic orig_apic;

static u64 hv_apic_icr_read(void)
{
	u64 reg_val;

	rdmsrl(HV_X64_MSR_ICR, reg_val);
	return reg_val;
}

static void hv_apic_icr_write(u32 low, u32 id)
{
	u64 reg_val;

	reg_val = SET_APIC_DEST_FIELD(id);
	reg_val = reg_val << 32;
	reg_val |= low;

	wrmsrl(HV_X64_MSR_ICR, reg_val);
}

static u32 hv_apic_read(u32 reg)
{
	u32 reg_val, hi;

	switch (reg) {
	case APIC_EOI:
		rdmsr(HV_X64_MSR_EOI, reg_val, hi);
		return reg_val;
	case APIC_TASKPRI:
		rdmsr(HV_X64_MSR_TPR, reg_val, hi);
		return reg_val;

	default:
		return native_apic_mem_read(reg);
	}
}

static void hv_apic_write(u32 reg, u32 val)
{
	switch (reg) {
	case APIC_EOI:
		wrmsr(HV_X64_MSR_EOI, val, 0);
		break;
	case APIC_TASKPRI:
		wrmsr(HV_X64_MSR_TPR, val, 0);
		break;
	default:
		native_apic_mem_write(reg, val);
	}
}

static void hv_apic_eoi_write(u32 reg, u32 val)
{
	wrmsr(HV_X64_MSR_EOI, val, 0);
}

/*
 * IPI implementation on Hyper-V.
 */
static bool __send_ipi_mask_ex(const struct cpumask *mask, int vector)
{
	struct hv_send_ipi_ex **arg;
	struct hv_send_ipi_ex *ipi_arg;
	unsigned long flags;
	int nr_bank = 0;
	int ret = 1;

	if (!(ms_hyperv.hints & HV_X64_EX_PROCESSOR_MASKS_RECOMMENDED))
		return false;

	local_irq_save(flags);
	arg = (struct hv_send_ipi_ex **)this_cpu_ptr(hyperv_pcpu_input_arg);

	ipi_arg = *arg;
	if (unlikely(!ipi_arg))
		goto ipi_mask_ex_done;

	ipi_arg->vector = vector;
	ipi_arg->reserved = 0;
	ipi_arg->vp_set.valid_bank_mask = 0;

	if (!cpumask_equal(mask, cpu_present_mask)) {
		ipi_arg->vp_set.format = HV_GENERIC_SET_SPARSE_4K;
		nr_bank = cpumask_to_vpset(&(ipi_arg->vp_set), mask);
	}
	if (nr_bank < 0)
		goto ipi_mask_ex_done;
	if (!nr_bank)
		ipi_arg->vp_set.format = HV_GENERIC_SET_ALL;

	ret = hv_do_rep_hypercall(HVCALL_SEND_IPI_EX, 0, nr_bank,
			      ipi_arg, NULL);

ipi_mask_ex_done:
	local_irq_restore(flags);
	return ((ret == 0) ? true : false);
}

static bool __send_ipi_mask(const struct cpumask *mask, int vector)
{
	int cur_cpu, vcpu;
	struct hv_send_ipi ipi_arg;
	int ret = 1;

	trace_hyperv_send_ipi_mask(mask, vector);

	if (cpumask_empty(mask))
		return true;

	if (!hv_hypercall_pg)
		return false;

	if ((vector < HV_IPI_LOW_VECTOR) || (vector > HV_IPI_HIGH_VECTOR))
		return false;

	/*
	 * From the supplied CPU set we need to figure out if we can get away
	 * with cheaper HVCALL_SEND_IPI hypercall. This is possible when the
	 * highest VP number in the set is < 64. As VP numbers are usually in
	 * ascending order and match Linux CPU ids, here is an optimization:
	 * we check the VP number for the highest bit in the supplied set first
	 * so we can quickly find out if using HVCALL_SEND_IPI_EX hypercall is
	 * a must. We will also check all VP numbers when walking the supplied
	 * CPU set to remain correct in all cases.
	 */
	if (hv_cpu_number_to_vp_number(cpumask_last(mask)) >= 64)
		goto do_ex_hypercall;

	ipi_arg.vector = vector;
	ipi_arg.cpu_mask = 0;

	for_each_cpu(cur_cpu, mask) {
		vcpu = hv_cpu_number_to_vp_number(cur_cpu);
		if (vcpu == VP_INVAL)
			return false;

		/*
		 * This particular version of the IPI hypercall can
		 * only target upto 64 CPUs.
		 */
		if (vcpu >= 64)
			goto do_ex_hypercall;

		__set_bit(vcpu, (unsigned long *)&ipi_arg.cpu_mask);
	}

	ret = hv_do_fast_hypercall16(HVCALL_SEND_IPI, ipi_arg.vector,
				     ipi_arg.cpu_mask);
	return ((ret == 0) ? true : false);

do_ex_hypercall:
	return __send_ipi_mask_ex(mask, vector);
}

static bool __send_ipi_one(int cpu, int vector)
{
	struct cpumask mask = CPU_MASK_NONE;

	cpumask_set_cpu(cpu, &mask);
	return __send_ipi_mask(&mask, vector);
}

static void hv_send_ipi(int cpu, int vector)
{
	if (!__send_ipi_one(cpu, vector))
		orig_apic.send_IPI(cpu, vector);
}

static void hv_send_ipi_mask(const struct cpumask *mask, int vector)
{
	if (!__send_ipi_mask(mask, vector))
		orig_apic.send_IPI_mask(mask, vector);
}

static void hv_send_ipi_mask_allbutself(const struct cpumask *mask, int vector)
{
	unsigned int this_cpu = smp_processor_id();
	struct cpumask new_mask;
	const struct cpumask *local_mask;

	cpumask_copy(&new_mask, mask);
	cpumask_clear_cpu(this_cpu, &new_mask);
	local_mask = &new_mask;
	if (!__send_ipi_mask(local_mask, vector))
		orig_apic.send_IPI_mask_allbutself(mask, vector);
}

static void hv_send_ipi_allbutself(int vector)
{
	hv_send_ipi_mask_allbutself(cpu_online_mask, vector);
}

static void hv_send_ipi_all(int vector)
{
	if (!__send_ipi_mask(cpu_online_mask, vector))
		orig_apic.send_IPI_all(vector);
}

static void hv_send_ipi_self(int vector)
{
	if (!__send_ipi_one(smp_processor_id(), vector))
		orig_apic.send_IPI_self(vector);
}

void __init hv_apic_init(void)
{
	if (ms_hyperv.hints & HV_X64_CLUSTER_IPI_RECOMMENDED) {
		pr_info("Hyper-V: Using IPI hypercalls\n");
		/*
		 * Set the IPI entry points.
		 */
		orig_apic = *apic;

		apic->send_IPI = hv_send_ipi;
		apic->send_IPI_mask = hv_send_ipi_mask;
		apic->send_IPI_mask_allbutself = hv_send_ipi_mask_allbutself;
		apic->send_IPI_allbutself = hv_send_ipi_allbutself;
		apic->send_IPI_all = hv_send_ipi_all;
		apic->send_IPI_self = hv_send_ipi_self;
	}

	if (ms_hyperv.hints & HV_X64_APIC_ACCESS_RECOMMENDED) {
		pr_info("Hyper-V: Using MSR based APIC access\n");
		apic_set_eoi_write(hv_apic_eoi_write);
		apic->read      = hv_apic_read;
		apic->write     = hv_apic_write;
		apic->icr_write = hv_apic_icr_write;
		apic->icr_read  = hv_apic_icr_read;
	}
}
