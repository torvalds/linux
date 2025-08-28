// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Secure AVIC Support (SEV-SNP Guests)
 *
 * Copyright (C) 2024 Advanced Micro Devices, Inc.
 *
 * Author: Neeraj Upadhyay <Neeraj.Upadhyay@amd.com>
 */

#include <linux/cc_platform.h>
#include <linux/percpu-defs.h>

#include <asm/apic.h>
#include <asm/sev.h>

#include "local.h"

struct secure_avic_page {
	u8 regs[PAGE_SIZE];
} __aligned(PAGE_SIZE);

static struct secure_avic_page __percpu *savic_page __ro_after_init;

static int savic_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	return x2apic_enabled() && cc_platform_has(CC_ATTR_SNP_SECURE_AVIC);
}

static void savic_setup(void)
{
	void *ap = this_cpu_ptr(savic_page);
	enum es_result res;
	unsigned long gpa;

	gpa = __pa(ap);

	/*
	 * The NPT entry for a vCPU's APIC backing page must always be
	 * present when the vCPU is running in order for Secure AVIC to
	 * function. A VMEXIT_BUSY is returned on VMRUN and the vCPU cannot
	 * be resumed if the NPT entry for the APIC backing page is not
	 * present. Notify GPA of the vCPU's APIC backing page to the
	 * hypervisor by calling savic_register_gpa(). Before executing
	 * VMRUN, the hypervisor makes use of this information to make sure
	 * the APIC backing page is mapped in NPT.
	 */
	res = savic_register_gpa(gpa);
	if (res != ES_OK)
		snp_abort();
}

static int savic_probe(void)
{
	if (!cc_platform_has(CC_ATTR_SNP_SECURE_AVIC))
		return 0;

	if (!x2apic_mode) {
		pr_err("Secure AVIC enabled in non x2APIC mode\n");
		snp_abort();
		/* unreachable */
	}

	savic_page = alloc_percpu(struct secure_avic_page);
	if (!savic_page)
		snp_abort();

	return 1;
}

static struct apic apic_x2apic_savic __ro_after_init = {

	.name				= "secure avic x2apic",
	.probe				= savic_probe,
	.acpi_madt_oem_check		= savic_acpi_madt_oem_check,
	.setup				= savic_setup,

	.dest_mode_logical		= false,

	.disable_esr			= 0,

	.cpu_present_to_apicid		= default_cpu_present_to_apicid,

	.max_apic_id			= UINT_MAX,
	.x2apic_set_max_apicid		= true,
	.get_apic_id			= x2apic_get_apic_id,

	.calc_dest_apicid		= apic_default_calc_apicid,

	.nmi_to_offline_cpu		= true,

	.read				= native_apic_msr_read,
	.write				= native_apic_msr_write,
	.eoi				= native_apic_msr_eoi,
	.icr_read			= native_x2apic_icr_read,
	.icr_write			= native_x2apic_icr_write,
};

apic_driver(apic_x2apic_savic);
