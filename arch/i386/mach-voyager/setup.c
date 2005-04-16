/*
 *	Machine specific setup for generic
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <asm/acpi.h>
#include <asm/arch_hooks.h>

void __init pre_intr_init_hook(void)
{
	init_ISA_irqs();
}

/*
 * IRQ2 is cascade interrupt to second interrupt controller
 */
static struct irqaction irq2 = { no_action, 0, CPU_MASK_NONE, "cascade", NULL, NULL};

void __init intr_init_hook(void)
{
#ifdef CONFIG_SMP
	smp_intr_init();
#endif

	if (!acpi_ioapic)
		setup_irq(2, &irq2);
}

void __init pre_setup_arch_hook(void)
{
	/* Voyagers run their CPUs from independent clocks, so disable
	 * the TSC code because we can't sync them */
	tsc_disable = 1;
}

void __init trap_init_hook(void)
{
}

static struct irqaction irq0  = { timer_interrupt, SA_INTERRUPT, CPU_MASK_NONE, "timer", NULL, NULL};

void __init time_init_hook(void)
{
	setup_irq(0, &irq0);
}
