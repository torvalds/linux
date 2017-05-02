/*
 * Common corrected MCE threshold handler code:
 */
#include <linux/interrupt.h>
#include <linux/kernel.h>

#include <asm/irq_vectors.h>
#include <asm/apic.h>
#include <asm/mce.h>
#include <asm/trace/irq_vectors.h>

static void default_threshold_interrupt(void)
{
	pr_err("Unexpected threshold interrupt at vector %x\n",
		THRESHOLD_APIC_VECTOR);
}

void (*mce_threshold_vector)(void) = default_threshold_interrupt;

static inline void __smp_threshold_interrupt(void)
{
	inc_irq_stat(irq_threshold_count);
	mce_threshold_vector();
}

asmlinkage __visible void __irq_entry smp_threshold_interrupt(void)
{
	entering_irq();
	__smp_threshold_interrupt();
	exiting_ack_irq();
}

asmlinkage __visible void __irq_entry smp_trace_threshold_interrupt(void)
{
	entering_irq();
	trace_threshold_apic_entry(THRESHOLD_APIC_VECTOR);
	__smp_threshold_interrupt();
	trace_threshold_apic_exit(THRESHOLD_APIC_VECTOR);
	exiting_ack_irq();
}
