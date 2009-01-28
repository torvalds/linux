/*
 * APIC driver for the Unisys ES7000 chipset.
 */
#define APIC_DEFINITION 1
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <asm/mpspec.h>
#include <asm/genapic.h>
#include <asm/fixmap.h>
#include <asm/apicdef.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <asm/es7000/apicdef.h>
#include <linux/smp.h>
#include <asm/es7000/apic.h>
#include <asm/es7000/ipi.h>
#include <asm/es7000/mpparse.h>
#include <asm/mach-default/mach_wakecpu.h>

void __init es7000_update_genapic_to_cluster(void)
{
	apic->target_cpus = target_cpus_cluster;
	apic->irq_delivery_mode = INT_DELIVERY_MODE_CLUSTER;
	apic->irq_dest_mode = INT_DEST_MODE_CLUSTER;

	apic->init_apic_ldr = es7000_init_apic_ldr_cluster;

	apic->cpu_mask_to_apicid = es7000_cpu_mask_to_apicid_cluster;
}

static int probe_es7000(void)
{
	/* probed later in mptable/ACPI hooks */
	return 0;
}

static __init int
es7000_mps_oem_check(struct mpc_table *mpc, char *oem, char *productid)
{
	if (mpc->oemptr) {
		struct mpc_oemtable *oem_table =
			(struct mpc_oemtable *)mpc->oemptr;

		if (!strncmp(oem, "UNISYS", 6))
			return parse_unisys_oem((char *)oem_table);
	}
	return 0;
}

#ifdef CONFIG_ACPI
/* Hook from generic ACPI tables.c */
static int __init es7000_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	unsigned long oem_addr = 0;
	int check_dsdt;
	int ret = 0;

	/* check dsdt at first to avoid clear fix_map for oem_addr */
	check_dsdt = es7000_check_dsdt();

	if (!find_unisys_acpi_oem_table(&oem_addr)) {
		if (check_dsdt)
			ret = parse_unisys_oem((char *)oem_addr);
		else {
			setup_unisys();
			ret = 1;
		}
		/*
		 * we need to unmap it
		 */
		unmap_unisys_acpi_oem_table(oem_addr);
	}
	return ret;
}
#else
static int __init es7000_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	return 0;
}
#endif

static void es7000_vector_allocation_domain(int cpu, cpumask_t *retmask)
{
	/* Careful. Some cpus do not strictly honor the set of cpus
	 * specified in the interrupt destination when using lowest
	 * priority interrupt delivery mode.
	 *
	 * In particular there was a hyperthreading cpu observed to
	 * deliver interrupts to the wrong hyperthread when only one
	 * hyperthread was specified in the interrupt desitination.
	 */
	*retmask = (cpumask_t){ { [0] = APIC_ALL_CPUS, } };
}

struct genapic apic_es7000 = {

	.name				= "es7000",
	.probe				= probe_es7000,
	.acpi_madt_oem_check		= es7000_acpi_madt_oem_check,
	.apic_id_registered		= es7000_apic_id_registered,

	.irq_delivery_mode		= dest_Fixed,
	/* phys delivery to target CPUs: */
	.irq_dest_mode			= 0,

	.target_cpus			= es7000_target_cpus,
	.disable_esr			= 1,
	.dest_logical			= 0,
	.check_apicid_used		= es7000_check_apicid_used,
	.check_apicid_present		= es7000_check_apicid_present,

	.vector_allocation_domain	= es7000_vector_allocation_domain,
	.init_apic_ldr			= es7000_init_apic_ldr,

	.ioapic_phys_id_map		= es7000_ioapic_phys_id_map,
	.setup_apic_routing		= es7000_setup_apic_routing,
	.multi_timer_check		= NULL,
	.apicid_to_node			= es7000_apicid_to_node,
	.cpu_to_logical_apicid		= es7000_cpu_to_logical_apicid,
	.cpu_present_to_apicid		= es7000_cpu_present_to_apicid,
	.apicid_to_cpu_present		= es7000_apicid_to_cpu_present,
	.setup_portio_remap		= NULL,
	.check_phys_apicid_present	= es7000_check_phys_apicid_present,
	.enable_apic_mode		= es7000_enable_apic_mode,
	.phys_pkg_id			= es7000_phys_pkg_id,
	.mps_oem_check			= es7000_mps_oem_check,

	.get_apic_id			= es7000_get_apic_id,
	.set_apic_id			= NULL,
	.apic_id_mask			= 0xFF << 24,

	.cpu_mask_to_apicid		= es7000_cpu_mask_to_apicid,
	.cpu_mask_to_apicid_and		= es7000_cpu_mask_to_apicid_and,

	.send_IPI_mask			= es7000_send_IPI_mask,
	.send_IPI_mask_allbutself	= NULL,
	.send_IPI_allbutself		= es7000_send_IPI_allbutself,
	.send_IPI_all			= es7000_send_IPI_all,
	.send_IPI_self			= NULL,

	.wakeup_cpu			= NULL,
	.trampoline_phys_low		= DEFAULT_TRAMPOLINE_PHYS_LOW,
	.trampoline_phys_high		= DEFAULT_TRAMPOLINE_PHYS_HIGH,

	.wait_for_init_deassert		= default_wait_for_init_deassert,

	/* Nothing to do for most platforms, since cleared by the INIT cycle: */
	.smp_callin_clear_local_apic	= NULL,
	.store_NMI_vector		= NULL,
	.inquire_remote_apic		= default_inquire_remote_apic,
};
