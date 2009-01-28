/*
 * APIC driver for "bigsmp" XAPIC machines with more than 8 virtual CPUs.
 * Drives the local APIC in "clustered mode".
 */
#define APIC_DEFINITION 1
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <asm/mpspec.h>
#include <asm/genapic.h>
#include <asm/fixmap.h>
#include <asm/apicdef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/dmi.h>
#include <asm/bigsmp/apicdef.h>
#include <linux/smp.h>
#include <asm/bigsmp/apic.h>
#include <asm/bigsmp/ipi.h>
#include <asm/mach-default/mach_mpparse.h>
#include <asm/mach-default/mach_wakecpu.h>

static int dmi_bigsmp; /* can be set by dmi scanners */

static int hp_ht_bigsmp(const struct dmi_system_id *d)
{
	printk(KERN_NOTICE "%s detected: force use of apic=bigsmp\n", d->ident);
	dmi_bigsmp = 1;
	return 0;
}


static const struct dmi_system_id bigsmp_dmi_table[] = {
	{ hp_ht_bigsmp, "HP ProLiant DL760 G2",
	{ DMI_MATCH(DMI_BIOS_VENDOR, "HP"),
	DMI_MATCH(DMI_BIOS_VERSION, "P44-"),}
	},

	{ hp_ht_bigsmp, "HP ProLiant DL740",
	{ DMI_MATCH(DMI_BIOS_VENDOR, "HP"),
	DMI_MATCH(DMI_BIOS_VERSION, "P47-"),}
	},
	 { }
};

static void bigsmp_vector_allocation_domain(int cpu, cpumask_t *retmask)
{
	cpus_clear(*retmask);
	cpu_set(cpu, *retmask);
}

static int probe_bigsmp(void)
{
	if (def_to_bigsmp)
		dmi_bigsmp = 1;
	else
		dmi_check_system(bigsmp_dmi_table);
	return dmi_bigsmp;
}

struct genapic apic_bigsmp = {

	.name				= "bigsmp",
	.probe				= probe_bigsmp,
	.acpi_madt_oem_check		= NULL,
	.apic_id_registered		= bigsmp_apic_id_registered,

	.irq_delivery_mode		= dest_Fixed,
	/* phys delivery to target CPU: */
	.irq_dest_mode			= 0,

	.target_cpus			= bigsmp_target_cpus,
	.disable_esr			= 1,
	.dest_logical			= 0,
	.check_apicid_used		= bigsmp_check_apicid_used,
	.check_apicid_present		= bigsmp_check_apicid_present,

	.vector_allocation_domain	= bigsmp_vector_allocation_domain,
	.init_apic_ldr			= bigsmp_init_apic_ldr,

	.ioapic_phys_id_map		= bigsmp_ioapic_phys_id_map,
	.setup_apic_routing		= bigsmp_setup_apic_routing,
	.multi_timer_check		= NULL,
	.apicid_to_node			= bigsmp_apicid_to_node,
	.cpu_to_logical_apicid		= cpu_to_logical_apicid,
	.cpu_present_to_apicid		= cpu_present_to_apicid,
	.apicid_to_cpu_present		= apicid_to_cpu_present,
	.setup_portio_remap		= setup_portio_remap,
	.check_phys_apicid_present	= check_phys_apicid_present,
	.enable_apic_mode		= enable_apic_mode,
	.phys_pkg_id			= phys_pkg_id,
	.mps_oem_check			= mps_oem_check,

	.get_apic_id			= get_apic_id,
	.set_apic_id			= NULL,
	.apic_id_mask			= APIC_ID_MASK,

	.cpu_mask_to_apicid		= cpu_mask_to_apicid,
	.cpu_mask_to_apicid_and		= cpu_mask_to_apicid_and,

	.send_IPI_mask			= send_IPI_mask,
	.send_IPI_mask_allbutself	= NULL,
	.send_IPI_allbutself		= send_IPI_allbutself,
	.send_IPI_all			= send_IPI_all,
	.send_IPI_self			= NULL,

	.wakeup_cpu			= NULL,
	.trampoline_phys_low		= TRAMPOLINE_PHYS_LOW,
	.trampoline_phys_high		= TRAMPOLINE_PHYS_HIGH,
	.wait_for_init_deassert		= wait_for_init_deassert,
	.smp_callin_clear_local_apic	= smp_callin_clear_local_apic,
	.store_NMI_vector		= store_NMI_vector,
	.restore_NMI_vector		= restore_NMI_vector,
	.inquire_remote_apic		= inquire_remote_apic,
};
