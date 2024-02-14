// SPDX-License-Identifier: GPL-2.0-only
/*
 * Default generic APIC driver. This handles up to 8 CPUs.
 *
 * Copyright 2003 Andi Kleen, SuSE Labs.
 *
 * Generic x86 APIC driver probe layer.
 */
#include <linux/export.h>
#include <linux/errno.h>
#include <linux/smp.h>

#include <xen/xen.h>

#include <asm/io_apic.h>
#include <asm/apic.h>
#include <asm/acpi.h>

#include "local.h"

static u32 default_phys_pkg_id(u32 cpuid_apic, int index_msb)
{
	return cpuid_apic >> index_msb;
}

static u32 default_get_apic_id(u32 x)
{
	unsigned int ver = GET_APIC_VERSION(apic_read(APIC_LVR));

	if (APIC_XAPIC(ver) || boot_cpu_has(X86_FEATURE_EXTD_APICID))
		return (x >> 24) & 0xFF;
	else
		return (x >> 24) & 0x0F;
}

/* should be called last. */
static int probe_default(void)
{
	return 1;
}

static struct apic apic_default __ro_after_init = {

	.name				= "default",
	.probe				= probe_default,
	.apic_id_registered		= default_apic_id_registered,

	.dest_mode_logical		= true,

	.disable_esr			= 0,

	.check_apicid_used		= default_check_apicid_used,
	.init_apic_ldr			= default_init_apic_ldr,
	.ioapic_phys_id_map		= default_ioapic_phys_id_map,
	.cpu_present_to_apicid		= default_cpu_present_to_apicid,
	.phys_pkg_id			= default_phys_pkg_id,

	.max_apic_id			= 0xFE,
	.get_apic_id			= default_get_apic_id,

	.calc_dest_apicid		= apic_flat_calc_apicid,

	.send_IPI			= default_send_IPI_single,
	.send_IPI_mask			= default_send_IPI_mask_logical,
	.send_IPI_mask_allbutself	= default_send_IPI_mask_allbutself_logical,
	.send_IPI_allbutself		= default_send_IPI_allbutself,
	.send_IPI_all			= default_send_IPI_all,
	.send_IPI_self			= default_send_IPI_self,

	.read				= native_apic_mem_read,
	.write				= native_apic_mem_write,
	.eoi				= native_apic_mem_eoi,
	.icr_read			= native_apic_icr_read,
	.icr_write			= native_apic_icr_write,
	.wait_icr_idle			= apic_mem_wait_icr_idle,
	.safe_wait_icr_idle		= apic_mem_wait_icr_idle_timeout,
};

apic_driver(apic_default);

struct apic *apic __ro_after_init = &apic_default;
EXPORT_SYMBOL_GPL(apic);

static int cmdline_apic __initdata;
static int __init parse_apic(char *arg)
{
	struct apic **drv;

	if (!arg)
		return -EINVAL;

	for (drv = __apicdrivers; drv < __apicdrivers_end; drv++) {
		if (!strcmp((*drv)->name, arg)) {
			apic_install_driver(*drv);
			cmdline_apic = 1;
			return 0;
		}
	}

	/* Parsed again by __setup for debug/verbose */
	return 0;
}
early_param("apic", parse_apic);

void __init x86_32_probe_bigsmp_early(void)
{
	if (nr_cpu_ids <= 8 || xen_pv_domain())
		return;

	if (IS_ENABLED(CONFIG_X86_BIGSMP)) {
		switch (boot_cpu_data.x86_vendor) {
		case X86_VENDOR_INTEL:
			if (!APIC_XAPIC(boot_cpu_apic_version))
				break;
			/* P4 and above */
			fallthrough;
		case X86_VENDOR_HYGON:
		case X86_VENDOR_AMD:
			if (apic_bigsmp_possible(cmdline_apic))
				return;
			break;
		}
	}
	pr_info("Limiting to 8 possible CPUs\n");
	set_nr_cpu_ids(8);
}

void __init x86_32_install_bigsmp(void)
{
	if (nr_cpu_ids > 8 && !xen_pv_domain())
		apic_bigsmp_force();
}

void __init x86_32_probe_apic(void)
{
	if (!cmdline_apic) {
		struct apic **drv;

		for (drv = __apicdrivers; drv < __apicdrivers_end; drv++) {
			if ((*drv)->probe()) {
				apic_install_driver(*drv);
				break;
			}
		}
		/* Not visible without early console */
		if (drv == __apicdrivers_end)
			panic("Didn't find an APIC driver");
	}
}
