/*
 *	Machine specific setup for generic
 */

#include <linux/smp.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/acpi.h>
#include <asm/arch_hooks.h>
#include <asm/e820.h>
#include <asm/setup.h>

#ifdef CONFIG_HOTPLUG_CPU
#define DEFAULT_SEND_IPI	(1)
#else
#define DEFAULT_SEND_IPI	(0)
#endif

int no_broadcast=DEFAULT_SEND_IPI;

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
	init_ISA_irqs();
}

/*
 * IRQ2 is cascade interrupt to second interrupt controller
 */
static struct irqaction irq2 = {
	.handler = no_action,
	.mask = CPU_MASK_NONE,
	.name = "cascade",
};

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
#ifdef CONFIG_X86_LOCAL_APIC
	apic_intr_init();
#endif

	if (!acpi_ioapic)
		setup_irq(2, &irq2);
}

/**
 * pre_setup_arch_hook - hook called prior to any setup_arch() execution
 *
 * Description:
 *	generally used to activate any machine specific identification
 *	routines that may be needed before setup_arch() runs.  On VISWS
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
}

static struct irqaction irq0  = {
	.handler = timer_interrupt,
	.flags = IRQF_DISABLED | IRQF_NOBALANCING | IRQF_IRQPOLL,
	.mask = CPU_MASK_NONE,
	.name = "timer"
};

/**
 * time_init_hook - do any specific initialisations for the system timer.
 *
 * Description:
 *	Must plug the system timer interrupt source at HZ into the IRQ listed
 *	in irq_vectors.h:TIMER_IRQ
 **/
void __init time_init_hook(void)
{
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
	/* If I recall correctly, there's a whole bunch of other things that
	 * we can do to check for NMI problems, but that's all I know about
	 * at the moment.
	 */

	printk("NMI generated from unknown source!\n");
}
#endif

static __init int no_ipi_broadcast(char *str)
{
	get_option(&str, &no_broadcast);
	printk ("Using %s mode\n", no_broadcast ? "No IPI Broadcast" :
											"IPI Broadcast");
	return 1;
}

__setup("no_ipi_broadcast=", no_ipi_broadcast);

static int __init print_ipi_mode(void)
{
	printk ("Using IPI %s mode\n", no_broadcast ? "No-Shortcut" :
											"Shortcut");
	return 0;
}

late_initcall(print_ipi_mode);

/**
 * machine_specific_memory_setup - Hook for machine specific memory setup.
 *
 * Description:
 *	This is included late in kernel/setup.c so that it can make
 *	use of all of the static functions.
 **/

char * __init machine_specific_memory_setup(void)
{
	char *who;


	who = "BIOS-e820";

	/*
	 * Try to copy the BIOS-supplied E820-map.
	 *
	 * Otherwise fake a memory map; one section from 0k->640k,
	 * the next section from 1mb->appropriate_mem_k
	 */
	sanitize_e820_map(boot_params.e820_map, &boot_params.e820_entries);
	if (copy_e820_map(boot_params.e820_map, boot_params.e820_entries)
	    < 0) {
		unsigned long mem_size;

		/* compare results from other methods and take the greater */
		if (boot_params.alt_mem_k
		    < boot_params.screen_info.ext_mem_k) {
			mem_size = boot_params.screen_info.ext_mem_k;
			who = "BIOS-88";
		} else {
			mem_size = boot_params.alt_mem_k;
			who = "BIOS-e801";
		}

		e820.nr_map = 0;
		add_memory_region(0, LOWMEMSIZE(), E820_RAM);
		add_memory_region(HIGH_MEMORY, mem_size << 10, E820_RAM);
  	}
	return who;
}
