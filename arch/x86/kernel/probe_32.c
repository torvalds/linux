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
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <asm/fixmap.h>
#include <asm/mpspec.h>
#include <asm/apicdef.h>
#include <asm/genapic.h>
#include <asm/setup.h>

#include <linux/threads.h>
#include <linux/cpumask.h>
#include <asm/mpspec.h>
#include <asm/genapic.h>
#include <asm/fixmap.h>
#include <asm/apicdef.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <asm/genapic.h>
#include <asm/ipi.h>

#include <linux/smp.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/acpi.h>
#include <asm/arch_hooks.h>
#include <asm/e820.h>
#include <asm/setup.h>

#include <asm/genapic.h>

#ifdef CONFIG_HOTPLUG_CPU
#define DEFAULT_SEND_IPI	(1)
#else
#define DEFAULT_SEND_IPI	(0)
#endif

int no_broadcast = DEFAULT_SEND_IPI;

#ifdef CONFIG_X86_LOCAL_APIC

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
	*retmask = (cpumask_t) { { [0] = APIC_ALL_CPUS } };
}

/* should be called last. */
static int probe_default(void)
{
	return 1;
}

struct genapic apic_default = {

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

	.wakeup_cpu			= NULL,
	.trampoline_phys_low		= DEFAULT_TRAMPOLINE_PHYS_LOW,
	.trampoline_phys_high		= DEFAULT_TRAMPOLINE_PHYS_HIGH,

	.wait_for_init_deassert		= default_wait_for_init_deassert,

	.smp_callin_clear_local_apic	= NULL,
	.store_NMI_vector		= NULL,
	.inquire_remote_apic		= default_inquire_remote_apic,

	.read				= native_apic_mem_read,
	.write				= native_apic_mem_write,
	.icr_read			= native_apic_icr_read,
	.icr_write			= native_apic_icr_write,
	.wait_icr_idle			= native_apic_wait_icr_idle,
	.safe_wait_icr_idle		= native_safe_apic_wait_icr_idle,
};

extern struct genapic apic_numaq;
extern struct genapic apic_summit;
extern struct genapic apic_bigsmp;
extern struct genapic apic_es7000;
extern struct genapic apic_default;

struct genapic *apic = &apic_default;

static struct genapic *apic_probe[] __initdata = {
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

	if (x86_quirks->update_genapic)
		x86_quirks->update_genapic();

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
			if (x86_quirks->update_genapic)
				x86_quirks->update_genapic();
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

		if (x86_quirks->update_genapic)
			x86_quirks->update_genapic();
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
			if (x86_quirks->update_genapic)
				x86_quirks->update_genapic();
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
			if (x86_quirks->update_genapic)
				x86_quirks->update_genapic();
			printk(KERN_INFO "Switched to APIC driver `%s'.\n",
			       apic->name);
		}
		return 1;
	}
	return 0;
}

#endif /* CONFIG_X86_LOCAL_APIC */

/**
 * pre_intr_init_hook - initialisation prior to setting up interrupt vectors
 *
 * Description:
 *	Perform any necessary interrupt initialisation prior to setting up
 *	the "ordinary" interrupt call gates.  For legacy reasons, the ISA
 *	interrupts should be initialised here if the machine emulates a PC
 *	in any way.
 **/
void __init pre_intr_init_hook(void)
{
	if (x86_quirks->arch_pre_intr_init) {
		if (x86_quirks->arch_pre_intr_init())
			return;
	}
	init_ISA_irqs();
}

/**
 * intr_init_hook - post gate setup interrupt initialisation
 *
 * Description:
 *	Fill in any interrupts that may have been left out by the general
 *	init_IRQ() routine.  interrupts having to do with the machine rather
 *	than the devices on the I/O bus (like APIC interrupts in intel MP
 *	systems) are started here.
 **/
void __init intr_init_hook(void)
{
	if (x86_quirks->arch_intr_init) {
		if (x86_quirks->arch_intr_init())
			return;
	}
}

/**
 * pre_setup_arch_hook - hook called prior to any setup_arch() execution
 *
 * Description:
 *	generally used to activate any machine specific identification
 *	routines that may be needed before setup_arch() runs.  On Voyager
 *	this is used to get the board revision and type.
 **/
void __init pre_setup_arch_hook(void)
{
}

/**
 * trap_init_hook - initialise system specific traps
 *
 * Description:
 *	Called as the final act of trap_init().  Used in VISWS to initialise
 *	the various board specific APIC traps.
 **/
void __init trap_init_hook(void)
{
	if (x86_quirks->arch_trap_init) {
		if (x86_quirks->arch_trap_init())
			return;
	}
}

static struct irqaction irq0  = {
	.handler = timer_interrupt,
	.flags = IRQF_DISABLED | IRQF_NOBALANCING | IRQF_IRQPOLL,
	.mask = CPU_MASK_NONE,
	.name = "timer"
};

/**
 * pre_time_init_hook - do any specific initialisations before.
 *
 **/
void __init pre_time_init_hook(void)
{
	if (x86_quirks->arch_pre_time_init)
		x86_quirks->arch_pre_time_init();
}

/**
 * time_init_hook - do any specific initialisations for the system timer.
 *
 * Description:
 *	Must plug the system timer interrupt source at HZ into the IRQ listed
 *	in irq_vectors.h:TIMER_IRQ
 **/
void __init time_init_hook(void)
{
	if (x86_quirks->arch_time_init) {
		/*
		 * A nonzero return code does not mean failure, it means
		 * that the architecture quirk does not want any
		 * generic (timer) setup to be performed after this:
		 */
		if (x86_quirks->arch_time_init())
			return;
	}

	irq0.mask = cpumask_of_cpu(0);
	setup_irq(0, &irq0);
}

#ifdef CONFIG_MCA
/**
 * mca_nmi_hook - hook into MCA specific NMI chain
 *
 * Description:
 *	The MCA (Microchannel Architecture) has an NMI chain for NMI sources
 *	along the MCA bus.  Use this to hook into that chain if you will need
 *	it.
 **/
void mca_nmi_hook(void)
{
	/*
	 * If I recall correctly, there's a whole bunch of other things that
	 * we can do to check for NMI problems, but that's all I know about
	 * at the moment.
	 */
	pr_warning("NMI generated from unknown source!\n");
}
#endif

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

