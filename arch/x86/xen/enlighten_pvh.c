// SPDX-License-Identifier: GPL-2.0
#include <linux/acpi.h>

#include <asm/io_apic.h>
#include <asm/hypervisor.h>

#include <xen/xen.h>
#include <asm/xen/interface.h>
#include <asm/xen/hypercall.h>

/*
 * PVH variables.
 *
 * The variable xen_pvh needs to live in the data segment since it is used
 * after startup_{32|64} is invoked, which will clear the .bss segment.
 */
bool xen_pvh __attribute__((section(".data"))) = 0;

void __init xen_pvh_init(void)
{
	u32 msr;
	u64 pfn;

	xen_pvh = 1;
	xen_start_flags = pvh_start_info.flags;

	msr = cpuid_ebx(xen_cpuid_base() + 2);
	pfn = __pa(hypercall_page);
	wrmsr_safe(msr, (u32)pfn, (u32)(pfn >> 32));
}
