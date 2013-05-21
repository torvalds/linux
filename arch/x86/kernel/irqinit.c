#include <linux/linkage.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/random.h>
#include <linux/kprobes.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/device.h>
#include <linux/bitops.h>
#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/delay.h>

#include <linux/atomic.h>
#include <asm/timer.h>
#include <asm/hw_irq.h>
#include <asm/pgtable.h>
#include <asm/desc.h>
#include <asm/apic.h>
#include <asm/setup.h>
#include <asm/i8259.h>
#include <asm/traps.h>
#include <asm/prom.h>

/*
 * ISA PIC or low IO-APIC triggered (INTA-cycle or APIC) interrupts:
 * (these are usually mapped to vectors 0x30-0x3f)
 */

/*
 * The IO-APIC gives us many more interrupt sources. Most of these
 * are unused but an SMP system is supposed to have enough memory ...
 * sometimes (mostly wrt. hw bugs) we get corrupted vectors all
 * across the spectrum, so we really want to be prepared to get all
 * of these. Plus, more powerful systems might have more than 64
 * IO-APIC registers.
 *
 * (these are usually mapped into the 0x30-0xff vector range)
 */

/*
 * IRQ2 is cascade interrupt to second interrupt controller
 */
static struct irqaction irq2 = {
	.handler = no_action,
	.name = "cascade",
	.flags = IRQF_NO_THREAD,
};

DEFINE_PER_CPU(vector_irq_t, vector_irq) = {
	[0 ... NR_VECTORS - 1] = -1,
};

int vector_used_by_percpu_irq(unsigned int vector)
{
	int cpu;

	for_each_online_cpu(cpu) {
		if (per_cpu(vector_irq, cpu)[vector] != -1)
			return 1;
	}

	return 0;
}

void __init init_ISA_irqs(void)
{
	struct irq_chip *chip = legacy_pic->chip;
	const char *name = chip->name;
	int i;

#if defined(CONFIG_X86_64) || defined(CONFIG_X86_LOCAL_APIC)
	init_bsp_APIC();
#endif
	legacy_pic->init(0);

	for (i = 0; i < legacy_pic->nr_legacy_irqs; i++)
		irq_set_chip_and_handler_name(i, chip, handle_level_irq, name);
}

void __init init_IRQ(void)
{
	int i;

	/*
	 * We probably need a better place for this, but it works for
	 * now ...
	 */
	x86_add_irq_domains();

	/*
	 * On cpu 0, Assign IRQ0_VECTOR..IRQ15_VECTOR's to IRQ 0..15.
	 * If these IRQ's are handled by legacy interrupt-controllers like PIC,
	 * then this configuration will likely be static after the boot. If
	 * these IRQ's are handled by more mordern controllers like IO-APIC,
	 * then this vector space can be freed and re-used dynamically as the
	 * irq's migrate etc.
	 */
	for (i = 0; i < legacy_pic->nr_legacy_irqs; i++)
		per_cpu(vector_irq, 0)[IRQ0_VECTOR + i] = i;

	x86_init.irqs.intr_init();
}

/*
 * Setup the vector to irq mappings.
 */
void setup_vector_irq(int cpu)
{
#ifndef CONFIG_X86_IO_APIC
	int irq;

	/*
	 * On most of the platforms, legacy PIC delivers the interrupts on the
	 * boot cpu. But there are certain platforms where PIC interrupts are
	 * delivered to multiple cpu's. If the legacy IRQ is handled by the
	 * legacy PIC, for the new cpu that is coming online, setup the static
	 * legacy vector to irq mapping:
	 */
	for (irq = 0; irq < legacy_pic->nr_legacy_irqs; irq++)
		per_cpu(vector_irq, cpu)[IRQ0_VECTOR + irq] = irq;
#endif

	__setup_vector_irq(cpu);
}

static void __init smp_intr_init(void)
{
#ifdef CONFIG_SMP
#if defined(CONFIG_X86_64) || defined(CONFIG_X86_LOCAL_APIC)
	/*
	 * The reschedule interrupt is a CPU-to-CPU reschedule-helper
	 * IPI, driven by wakeup.
	 */
	alloc_intr_gate(RESCHEDULE_VECTOR, reschedule_interrupt);

	/* IPI for generic function call */
	alloc_intr_gate(CALL_FUNCTION_VECTOR, call_function_interrupt);

	/* IPI for generic single function call */
	alloc_intr_gate(CALL_FUNCTION_SINGLE_VECTOR,
			call_function_single_interrupt);

	/* Low priority IPI to cleanup after moving an irq */
	set_intr_gate(IRQ_MOVE_CLEANUP_VECTOR, irq_move_cleanup_interrupt);
	set_bit(IRQ_MOVE_CLEANUP_VECTOR, used_vectors);

	/* IPI used for rebooting/stopping */
	alloc_intr_gate(REBOOT_VECTOR, reboot_interrupt);
#endif
#endif /* CONFIG_SMP */
}

static void __init apic_intr_init(void)
{
	smp_intr_init();

#ifdef CONFIG_X86_THERMAL_VECTOR
	alloc_intr_gate(THERMAL_APIC_VECTOR, thermal_interrupt);
#endif
#ifdef CONFIG_X86_MCE_THRESHOLD
	alloc_intr_gate(THRESHOLD_APIC_VECTOR, threshold_interrupt);
#endif

#if defined(CONFIG_X86_64) || defined(CONFIG_X86_LOCAL_APIC)
	/* self generated IPI for local APIC timer */
	alloc_intr_gate(LOCAL_TIMER_VECTOR, apic_timer_interrupt);

	/* IPI for X86 platform specific use */
	alloc_intr_gate(X86_PLATFORM_IPI_VECTOR, x86_platform_ipi);
#ifdef CONFIG_HAVE_KVM
	/* IPI for KVM to deliver posted interrupt */
	alloc_intr_gate(POSTED_INTR_VECTOR, kvm_posted_intr_ipi);
#endif

	/* IPI vectors for APIC spurious and error interrupts */
	alloc_intr_gate(SPURIOUS_APIC_VECTOR, spurious_interrupt);
	alloc_intr_gate(ERROR_APIC_VECTOR, error_interrupt);

	/* IRQ work interrupts: */
# ifdef CONFIG_IRQ_WORK
	alloc_intr_gate(IRQ_WORK_VECTOR, irq_work_interrupt);
# endif

#endif
}

void __init native_init_IRQ(void)
{
	int i;

	/* Execute any quirks before the call gates are initialised: */
	x86_init.irqs.pre_vector_init();

	apic_intr_init();

	/*
	 * Cover the whole vector space, no vector can escape
	 * us. (some of these will be overridden and become
	 * 'special' SMP interrupts)
	 */
	i = FIRST_EXTERNAL_VECTOR;
	for_each_clear_bit_from(i, used_vectors, NR_VECTORS) {
		/* IA32_SYSCALL_VECTOR could be used in trap_init already. */
		set_intr_gate(i, interrupt[i - FIRST_EXTERNAL_VECTOR]);
	}

	if (!acpi_ioapic && !of_ioapic)
		setup_irq(2, &irq2);

#ifdef CONFIG_X86_32
	irq_ctx_init(smp_processor_id());
#endif
}
