/*
 * APIC driver for the IBM NUMAQ chipset.
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
#include <asm/numaq/apicdef.h>
#include <linux/smp.h>
#include <asm/numaq/apic.h>
#include <asm/numaq/ipi.h>
#include <asm/numaq/mpparse.h>
#include <asm/numaq/wakecpu.h>
#include <asm/numaq.h>

static int __numaq_mps_oem_check(struct mpc_table *mpc, char *oem, char *productid)
{
	numaq_mps_oem_check(mpc, oem, productid);
	return found_numaq;
}

static int probe_numaq(void)
{
	/* already know from get_memcfg_numaq() */
	return found_numaq;
}

static void numaq_vector_allocation_domain(int cpu, cpumask_t *retmask)
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

static void numaq_setup_portio_remap(void)
{
	int num_quads = num_online_nodes();

	if (num_quads <= 1)
       		return;

	printk("Remapping cross-quad port I/O for %d quads\n", num_quads);
	xquad_portio = ioremap(XQUAD_PORTIO_BASE, num_quads*XQUAD_PORTIO_QUAD);
	printk("xquad_portio vaddr 0x%08lx, len %08lx\n",
		(u_long) xquad_portio, (u_long) num_quads*XQUAD_PORTIO_QUAD);
}

struct genapic apic_numaq = {

	.name				= "NUMAQ",
	.probe				= probe_numaq,
	.acpi_madt_oem_check		= NULL,
	.apic_id_registered		= numaq_apic_id_registered,

	.irq_delivery_mode		= dest_LowestPrio,
	/* physical delivery on LOCAL quad: */
	.irq_dest_mode			= 0,

	.target_cpus			= numaq_target_cpus,
	.disable_esr			= 1,
	.dest_logical			= APIC_DEST_LOGICAL,
	.check_apicid_used		= numaq_check_apicid_used,
	.check_apicid_present		= numaq_check_apicid_present,

	.vector_allocation_domain	= numaq_vector_allocation_domain,
	.init_apic_ldr			= numaq_init_apic_ldr,

	.ioapic_phys_id_map		= numaq_ioapic_phys_id_map,
	.setup_apic_routing		= numaq_setup_apic_routing,
	.multi_timer_check		= numaq_multi_timer_check,
	.apicid_to_node			= numaq_apicid_to_node,
	.cpu_to_logical_apicid		= numaq_cpu_to_logical_apicid,
	.cpu_present_to_apicid		= numaq_cpu_present_to_apicid,
	.apicid_to_cpu_present		= numaq_apicid_to_cpu_present,
	.setup_portio_remap		= numaq_setup_portio_remap,
	.check_phys_apicid_present	= numaq_check_phys_apicid_present,
	.enable_apic_mode		= NULL,
	.phys_pkg_id			= numaq_phys_pkg_id,
	.mps_oem_check			= __numaq_mps_oem_check,

	.get_apic_id			= numaq_get_apic_id,
	.set_apic_id			= NULL,
	.apic_id_mask			= NUMAQ_APIC_ID_MASK,

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
