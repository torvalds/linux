/*
 * X86 specific Hyper-V initialization code.
 *
 * Copyright (C) 2016, Microsoft, Inc.
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
#include <asm/hypervisor.h>
#include <asm/hyperv.h>
#include <asm/mshyperv.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>

void *hv_hypercall_pg;
/*
 * This function is to be invoked early in the boot sequence after the
 * hypervisor has been detected.
 *
 * 1. Setup the hypercall page.
 */
void hyperv_init(void)
{
	u64 guest_id;
	union hv_x64_msr_hypercall_contents hypercall_msr;

	if (x86_hyper != &x86_hyper_ms_hyperv)
		return;

	/*
	 * Setup the hypercall page and enable hypercalls.
	 * 1. Register the guest ID
	 * 2. Enable the hypercall and register the hypercall page
	 */
	guest_id = generate_guest_id(0, LINUX_VERSION_CODE, 0);
	wrmsrl(HV_X64_MSR_GUEST_OS_ID, guest_id);

	hv_hypercall_pg  = __vmalloc(PAGE_SIZE, GFP_KERNEL, PAGE_KERNEL_EXEC);
	if (hv_hypercall_pg == NULL) {
		wrmsrl(HV_X64_MSR_GUEST_OS_ID, 0);
		return;
	}

	rdmsrl(HV_X64_MSR_HYPERCALL, hypercall_msr.as_uint64);
	hypercall_msr.enable = 1;
	hypercall_msr.guest_physical_address = vmalloc_to_pfn(hv_hypercall_pg);
	wrmsrl(HV_X64_MSR_HYPERCALL, hypercall_msr.as_uint64);
}
EXPORT_SYMBOL_GPL(hv_hypercall_pg);
