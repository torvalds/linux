/*
 * Default generic APIC driver. This handles up to 8 CPUs.
 *
 * Copyright 2003 Andi Kleen, SuSE Labs.
 * Subject to the GNU Public License, v.2
 *
 * Generic x86 APIC driver probe layer.
 */
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <asm/fixmap.h>
#include <asm/mpspec.h>
#include <asm/apicdef.h>
#include <asm/apic.h>
#include <asm/setup.h>

#include <linux/threads.h>
#include <linux/cpumask.h>
#include <asm/mpspec.h>
#include <asm/fixmap.h>
#include <asm/apicdef.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <asm/ipi.h>

#include <linux/smp.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/acpi.h>
#include <asm/e820.h>
#include <asm/setup.h>

#ifdef CONFIG_HOTPLUG_CPU
#define DEFAULT_SEND_IPI	(1)
#else
#define DEFAULT_SEND_IPI	(0)
#endif

int no_broadcast = DEFAULT_SEND_IPI;

static __init int no_ipi_broadcast(char *str)
{
	get_option(&str, &no_broadcast);
	pr_info("Using %s mode\n",
		no_broadcast ? "No IPI Broadcast" : "IPI Broadcast");
	return 1;
}
__setup("no_ipi_broadcast=", no_ipi_broadcast);

static int __init print_ipi_mode(void)
{
	pr_info("Using IPI %s mode\n",
		no_broadcast ? "No-Shortcut" : "Shortcut");
	return 0;
}
late_initcall(print_ipi_mode);

void default_setup_apic_routing(void)
{
#ifdef CONFIG_X86_IO_APIC
	printk(KERN_INFO
		"Enabling APIC mode:  Flat.  Using %d I/O APICs\n",
		nr_ioapics);
#endif
}

static void default_vector_allocation_domain(int cpu, struct cpumask *retmask)
{
	/*
	 * Careful. Some cpus do not strictly honor the set of cpus
	 * specified in the interrupt destination when using lowest
	 * priority interrupt delivery mode.
	 *
	 * In particular there was a hyperthreading cpu observed to
	 * deliver interrupts to the wrong hyperthread when only one
	 * hyperthread was specified in the interrupt desitination.
	 */
	cpumask_clear(retmask);
	cpumask_bits(retmask)[0] = APIC_ALL_CPUS;
}

/* should be called last. */
static int probe_default(void)
{
	return 1;
}

struct apic apic_default = {

	.name				= "default",
	.probe				= probe_default,
	.acpi_madt_oem_check		= NULL,
	.apic_id_registered		= default_apic_id_registered,

	.irq_delivery_mode		= dest_LowestPrio,
	/* logical delivery broadcast to all CPUs: */
	.irq_dest_mode			= 1,

	.target_cpus			= default_target_cpus,
	.disable_esr			= 0,
	.dest_logical			= APIC_DEST_LOGICAL,
	.check_apicid_used		= default_check_apicid_used,
	.check_apicid_present		= default_check_apicid_present,

	.vector_allocation_domain	= default_vector_allocation_domain,
	.init_apic_ldr			= default_init_apic_ldr,

	.ioapic_phys_id_map		= default_ioapic_phys_id_map,
	.setup_apic_routing		= default_setup_apic_routing,
	.multi_timer_check		= NULL,
	.apicid_to_node			= default_apicid_to_node,
	.cpu_to_logical_apicid		= default_cpu_to_logical_apicid,
	.cpu_present_to_apicid		= default_cpu_present_to_apicid,
	.apicid_to_cpu_present		= default_apicid_to_cpu_present,
	.setup_portio_remap		= NULL,
	.check_phys_apicid_present	= default_check_phys_apicid_present,
	.enable_apic_mode		= NULL,
	.phys_pkg_id			= default_phys_pkg_id,
	.mps_oem_check			= NULL,

	.get_apic_id			= default_get_apic_id,
	.set_apic_id			= NULL,
	.apic_id_mask			= 0x0F << 24,

	.cpu_mask_to_apicid		= default_cpu_mask_to_apicid,
	.cpu_mask_to_apicid_and		= default_cpu_mask_to_apicid_and,

	.send_IPI_mask			= default_send_IPI_mask_logical,
	.send_IPI_mask_allbutself	= default_send_IPI_mask_allbutself_logical,
	.send_IPI_allbutself		= default_send_IPI_allbutself,
	.send_IPI_all			= default_send_IPI_all,
	.send_IPI_self			= default_send_IPI_self,

	.trampoline_phys_low		= DEFAULT_TRAMPOLINE_PHYS_LOW,
	.trampoline_phys_high		= DEFAULT_TRAMPOLINE_PHYS_HIGH,

	.wait_for_init_deassert		= default_wait_for_init_deassert,

	.smp_callin_clear_local_apic	= NULL,
	.inquire_remote_apic		= default_inquire_remote_apic,

	.read				= native_apic_mem_read,
	.write				= native_apic_mem_write,
	.icr_read			= native_apic_icr_read,
	.icr_write			= native_apic_icr_write,
	.wait_icr_idle			= native_apic_wait_icr_idle,
	.safe_wait_icr_idle		= native_safe_apic_wait_icr_idle,
};

extern struct apic apic_numaq;
extern struct apic apic_summit;
extern struct apic apic_bigsmp;
extern struct apic apic_es7000;
extern struct apic apic_es7000_cluster;
extern struct apic apic_default;

struct apic *apic = &apic_default;
EXPORT_SYMBOL_GPL(apic);

static struct apic *apic_probe[] __initdata = {
#ifdef CONFIG_X86_NUMAQ
	&apic_numaq,
#endif
#ifdef CONFIG_X86_SUMMIT
	&apic_summit,
#endif
#ifdef CONFIG_X86_BIGSMP
	&apic_bigsmp,
#endif
#ifdef CONFIG_X86_ES7000
	&apic_es7000,
	&apic_es7000_cluster,
#endif
	&apic_default,	/* must be last */
	NULL,
};

static int cmdline_apic __initdata;
static int __init parse_apic(char *arg)
{
	int i;

	if (!arg)
		return -EINVAL;

	for (i = 0; apic_probe[i]; i++) {
		if (!strcmp(apic_probe[i]->name, arg)) {
			apic = apic_probe[i];
			cmdline_apic = 1;
			return 0;
		}
	}

	/* Parsed again by __setup for debug/verbose */
	return 0;
}
early_param("apic", parse_apic);

void __init generic_bigsmp_probe(void)
{
#ifdef CONFIG_X86_BIGSMP
	/*
	 * This routine is used to switch to bigsmp mode when
	 * - There is no apic= option specified by the user
	 * - generic_apic_probe() has chosen apic_default as the sub_arch
	 * - we find more than 8 CPUs in acpi LAPIC listing with xAPIC support
	 */

	if (!cmdline_apic && apic == &apic_default) {
		if (apic_bigsmp.probe()) {
			apic = &apic_bigsmp;
			printk(KERN_INFO "Overriding APIC driver with %s\n",
			       apic->name);
		}
	}
#endif
}

void __init generic_apic_probe(void)
{
	if (!cmdline_apic) {
		int i;
		for (i = 0; apic_probe[i]; i++) {
			if (apic_probe[i]->probe()) {
				apic = apic_probe[i];
				break;
			}
		}
		/* Not visible without early console */
		if (!apic_probe[i])
			panic("Didn't find an APIC driver");
	}
	printk(KERN_INFO "Using APIC driver %s\n", apic->name);
}

/* These functions can switch the APIC even after the initial ->probe() */

int __init
generic_mps_oem_check(struct mpc_table *mpc, char *oem, char *productid)
{
	int i;

	for (i = 0; apic_probe[i]; ++i) {
		if (!apic_probe[i]->mps_oem_check)
			continue;
		if (!apic_probe[i]->mps_oem_check(mpc, oem, productid))
			continue;

		if (!cmdline_apic) {
			apic = apic_probe[i];
			printk(KERN_INFO "Switched to APIC driver `%s'.\n",
			       apic->name);
		}
		return 1;
	}
	return 0;
}

int __init default_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	int i;

	for (i = 0; apic_probe[i]; ++i) {
		if (!apic_probe[i]->acpi_madt_oem_check)
			continue;
		if (!apic_probe[i]->acpi_madt_oem_check(oem_id, oem_table_id))
			continue;

		if (!cmdline_apic) {
			apic = apic_probe[i];
			printk(KERN_INFO "Switched to APIC driver `%s'.\n",
			       apic->name);
		}
		return 1;
	}
	return 0;
}
