// SPDX-License-Identifier: GPL-2.0
/*
 * APIC driver for "bigsmp" xAPIC machines with more than 8 virtual CPUs.
 *
 * Drives the local APIC in "clustered mode".
 */
#include <linux/cpumask.h>
#include <linux/dmi.h>
#include <linux/smp.h>

#include <asm/apic.h>
#include <asm/io_apic.h>

#include "local.h"

static u32 bigsmp_get_apic_id(u32 x)
{
	return (x >> 24) & 0xFF;
}

static bool bigsmp_check_apicid_used(physid_mask_t *map, u32 apicid)
{
	return false;
}

static void bigsmp_ioapic_phys_id_map(physid_mask_t *phys_map, physid_mask_t *retmap)
{
	/* For clustered we don't have a good way to do this yet - hack */
	physids_promote(0xFFL, retmap);
}

static u32 bigsmp_phys_pkg_id(u32 cpuid_apic, int index_msb)
{
	return cpuid_apic >> index_msb;
}

static void bigsmp_send_IPI_allbutself(int vector)
{
	default_send_IPI_mask_allbutself_phys(cpu_online_mask, vector);
}

static void bigsmp_send_IPI_all(int vector)
{
	default_send_IPI_mask_sequence_phys(cpu_online_mask, vector);
}

static int dmi_bigsmp; /* can be set by dmi scanners */

static int hp_ht_bigsmp(const struct dmi_system_id *d)
{
	printk(KERN_NOTICE "%s detected: force use of apic=bigsmp\n", d->ident);
	dmi_bigsmp = 1;

	return 0;
}


static const struct dmi_system_id bigsmp_dmi_table[] = {
	{ hp_ht_bigsmp, "HP ProLiant DL760 G2",
		{	DMI_MATCH(DMI_BIOS_VENDOR, "HP"),
			DMI_MATCH(DMI_BIOS_VERSION, "P44-"),
		}
	},

	{ hp_ht_bigsmp, "HP ProLiant DL740",
		{	DMI_MATCH(DMI_BIOS_VENDOR, "HP"),
			DMI_MATCH(DMI_BIOS_VERSION, "P47-"),
		}
	},
	{ } /* NULL entry stops DMI scanning */
};

static int probe_bigsmp(void)
{
	return dmi_check_system(bigsmp_dmi_table);
}

static struct apic apic_bigsmp __ro_after_init = {

	.name				= "bigsmp",
	.probe				= probe_bigsmp,

	.dest_mode_logical		= false,

	.disable_esr			= 1,

	.check_apicid_used		= bigsmp_check_apicid_used,
	.ioapic_phys_id_map		= bigsmp_ioapic_phys_id_map,
	.cpu_present_to_apicid		= default_cpu_present_to_apicid,
	.phys_pkg_id			= bigsmp_phys_pkg_id,

	.max_apic_id			= 0xFE,
	.get_apic_id			= bigsmp_get_apic_id,
	.set_apic_id			= NULL,

	.calc_dest_apicid		= apic_default_calc_apicid,

	.send_IPI			= default_send_IPI_single_phys,
	.send_IPI_mask			= default_send_IPI_mask_sequence_phys,
	.send_IPI_mask_allbutself	= NULL,
	.send_IPI_allbutself		= bigsmp_send_IPI_allbutself,
	.send_IPI_all			= bigsmp_send_IPI_all,
	.send_IPI_self			= default_send_IPI_self,

	.read				= native_apic_mem_read,
	.write				= native_apic_mem_write,
	.eoi				= native_apic_mem_eoi,
	.icr_read			= native_apic_icr_read,
	.icr_write			= native_apic_icr_write,
	.wait_icr_idle			= apic_mem_wait_icr_idle,
	.safe_wait_icr_idle		= apic_mem_wait_icr_idle_timeout,
};

bool __init apic_bigsmp_possible(bool cmdline_override)
{
	return apic == &apic_bigsmp || !cmdline_override;
}

void __init apic_bigsmp_force(void)
{
	if (apic != &apic_bigsmp)
		apic_install_driver(&apic_bigsmp);
}

apic_driver(apic_bigsmp);
