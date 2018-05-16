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
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/clockchips.h>
#include <linux/hyperv.h>
#include <linux/slab.h>
#include <linux/cpuhotplug.h>
#include <asm/hypervisor.h>
#include <asm/mshyperv.h>

#ifdef CONFIG_X86_64
#if IS_ENABLED(CONFIG_HYPERV)

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

void __init hv_apic_init(void)
{
	if (ms_hyperv.hints & HV_X64_APIC_ACCESS_RECOMMENDED) {
		pr_info("Hyper-V: Using MSR based APIC access\n");
		apic_set_eoi_write(hv_apic_eoi_write);
		apic->read      = hv_apic_read;
		apic->write     = hv_apic_write;
		apic->icr_write = hv_apic_icr_write;
		apic->icr_read  = hv_apic_icr_read;
	}
}

#endif /* CONFIG_HYPERV */
#endif /* CONFIG_X86_64 */
