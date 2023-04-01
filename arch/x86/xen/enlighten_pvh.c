// SPDX-License-Identifier: GPL-2.0
#include <linux/acpi.h>
#include <linux/export.h>

#include <xen/hvc-console.h>

#include <asm/io_apic.h>
#include <asm/hypervisor.h>
#include <asm/e820/api.h>

#include <xen/xen.h>
#include <asm/xen/interface.h>
#include <asm/xen/hypercall.h>

#include <xen/interface/memory.h>

#include "xen-ops.h"

/*
 * PVH variables.
 *
 * The variable xen_pvh needs to live in a data segment since it is used
 * after startup_{32|64} is invoked, which will clear the .bss segment.
 */
bool __ro_after_init xen_pvh;
EXPORT_SYMBOL_GPL(xen_pvh);

void __init xen_pvh_init(struct boot_params *boot_params)
{
	u32 msr;
	u64 pfn;

	xen_pvh = 1;
	xen_domain_type = XEN_HVM_DOMAIN;
	xen_start_flags = pvh_start_info.flags;

	msr = cpuid_ebx(xen_cpuid_base() + 2);
	pfn = __pa(hypercall_page);
	wrmsr_safe(msr, (u32)pfn, (u32)(pfn >> 32));

	if (xen_initial_domain())
		x86_init.oem.arch_setup = xen_add_preferred_consoles;
	x86_init.oem.banner = xen_banner;

	xen_efi_init(boot_params);

	if (xen_initial_domain()) {
		struct xen_platform_op op = {
			.cmd = XENPF_get_dom0_console,
		};
		long ret = HYPERVISOR_platform_op(&op);

		if (ret > 0)
			xen_init_vga(&op.u.dom0_console,
				     min(ret * sizeof(char),
					 sizeof(op.u.dom0_console)),
				     &boot_params->screen_info);
	}
}

void __init mem_map_via_hcall(struct boot_params *boot_params_p)
{
	struct xen_memory_map memmap;
	int rc;

	memmap.nr_entries = ARRAY_SIZE(boot_params_p->e820_table);
	set_xen_guest_handle(memmap.buffer, boot_params_p->e820_table);
	rc = HYPERVISOR_memory_op(XENMEM_memory_map, &memmap);
	if (rc) {
		xen_raw_printk("XENMEM_memory_map failed (%d)\n", rc);
		BUG();
	}
	boot_params_p->e820_entries = memmap.nr_entries;
}
