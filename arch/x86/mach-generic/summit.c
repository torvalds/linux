/*
 * APIC driver for the IBM "Summit" chipset.
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
#include <asm/summit/apicdef.h>
#include <linux/smp.h>
#include <asm/summit/apic.h>
#include <asm/summit/ipi.h>
#include <asm/summit/mpparse.h>
#include <asm/mach-default/mach_wakecpu.h>

static int probe_summit(void)
{
	/* probed later in mptable/ACPI hooks */
	return 0;
}

static void summit_vector_allocation_domain(int cpu, cpumask_t *retmask)
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

struct genapic apic_summit = {

	.name				= "summit",
	.probe				= probe_summit,
	.acpi_madt_oem_check		= summit_acpi_madt_oem_check,
	.apic_id_registered		= summit_apic_id_registered,

	.irq_delivery_mode		= dest_LowestPrio,
	/* logical delivery broadcast to all CPUs: */
	.irq_dest_mode			= 1,

	.target_cpus			= summit_target_cpus,
	.disable_esr			= 1,
	.dest_logical			= APIC_DEST_LOGICAL,
	.check_apicid_used		= summit_check_apicid_used,
	.check_apicid_present		= summit_check_apicid_present,

	.vector_allocation_domain	= summit_vector_allocation_domain,
	.init_apic_ldr			= summit_init_apic_ldr,

	.ioapic_phys_id_map		= summit_ioapic_phys_id_map,
	.setup_apic_routing		= summit_setup_apic_routing,
	.multi_timer_check		= NULL,
	.apicid_to_node			= summit_apicid_to_node,
	.cpu_to_logical_apicid		= summit_cpu_to_logical_apicid,
	.cpu_present_to_apicid		= summit_cpu_present_to_apicid,
	.apicid_to_cpu_present		= summit_apicid_to_cpu_present,
	.setup_portio_remap		= NULL,
	.check_phys_apicid_present	= summit_check_phys_apicid_present,
	.enable_apic_mode		= NULL,
	.phys_pkg_id			= summit_phys_pkg_id,
	.mps_oem_check			= summit_mps_oem_check,

	.get_apic_id			= summit_get_apic_id,
	.set_apic_id			= NULL,
	.apic_id_mask			= 0xFF << 24,

	.cpu_mask_to_apicid		= summit_cpu_mask_to_apicid,
	.cpu_mask_to_apicid_and		= summit_cpu_mask_to_apicid_and,

	.send_IPI_mask			= summit_send_IPI_mask,
	.send_IPI_mask_allbutself	= NULL,
	.send_IPI_allbutself		= summit_send_IPI_allbutself,
	.send_IPI_all			= summit_send_IPI_all,
	.send_IPI_self			= NULL,

	.wakeup_cpu			= NULL,
	.trampoline_phys_low		= DEFAULT_TRAMPOLINE_PHYS_LOW,
	.trampoline_phys_high		= DEFAULT_TRAMPOLINE_PHYS_HIGH,

	.wait_for_init_deassert		= default_wait_for_init_deassert,

	.smp_callin_clear_local_apic	= NULL,
	.store_NMI_vector		= NULL,
	.inquire_remote_apic		= inquire_remote_apic,
};
