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

#include <mach_ipi.h>

#ifdef CONFIG_HOTPLUG_CPU
#define DEFAULT_SEND_IPI	(1)
#else
#define DEFAULT_SEND_IPI	(0)
#endif

int no_broadcast = DEFAULT_SEND_IPI;

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

