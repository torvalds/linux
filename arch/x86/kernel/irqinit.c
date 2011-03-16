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
#include <linux/sysdev.h>
#include <linux/bitops.h>
#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/delay.h>

#include <asm/atomic.h>
#include <asm/system.h>
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

#ifdef CONFIG_X86_32
/*
 * Note that on a 486, we don't want to do a SIGFPE on an irq13
 * as the irq is unreliable, and exception 16 works correctly
 * (ie as explained in the intel literature). On a 386, you
 * can't use exception 16 due to bad IBM design, so we have to
 * rely on the less exact irq13.
 *
 * Careful.. Not only is IRQ13 unreliable, but it is also
 * leads to races. IBM designers who came up with it should
 * be shot.
 */

static irqreturn_t math_error_irq(int cpl, void *dev_id)
{
	outb(0, 0xF0);
	if (ignore_fpu_irq || !boot_cpu_data.hard_math)
		return IRQ_NONE;
	math_error(get_irq_regs(), 0, 16);
	return IRQ_HANDLED;
}

/*
 * New motherboards sometimes make IRQ 13 be a PCI interrupt,
 * so allow interrupt sharing.
 */
static struct irqaction fpu_irq = {
	.handler = math_error_irq,
	.name = "fpu",
	.flags = IRQF_NO_THREAD,
};
#endif

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

	/* IPIs for invalidation */
#define ALLOC_INVTLB_VEC(NR) \
	alloc_intr_gate(INVALIDATE_TLB_VECTOR_START+NR, \
		invalidate_interrupt##NR)

	switch (NUM_INVALIDATE_TLB_VECTORS) {
	default:
		ALLOC_INVTLB_VEC(31);
	case 31:
		ALLOC_INVTLB_VEC(30);
	case 30:
		ALLOC_INVTLB_VEC(29);
	case 29:
		ALLOC_INVTLB_VEC(28);
	case 28:
		ALLOC_INVTLB_VEC(27);
	case 27:
		ALLOC_INVTLB_VEC(26);
	case 26:
		ALLOC_INVTLB_VEC(25);
	case 25:
		ALLOC_INVTLB_VEC(24);
	case 24:
		ALLOC_INVTLB_VEC(23);
	case 23:
		ALLOC_INVTLB_VEC(22);
	case 22:
		ALLOC_INVTLB_VEC(21);
	case 21:
		ALLOC_INVTLB_VEC(20);
	case 20:
		ALLOC_INVTLB_VEC(19);
	case 19:
		ALLOC_INVTLB_VEC(18);
	case 18:
		ALLOC_INVTLB_VEC(17);
	case 17:
		ALLOC_INVTLB_VEC(16);
	case 16:
		ALLOC_INVTLB_VEC(15);
	case 15:
		ALLOC_INVTLB_VEC(14);
	case 14:
		ALLOC_INVTLB_VEC(13);
	case 13:
		ALLOC_INVTLB_VEC(12);
	case 12:
		ALLOC_INVTLB_VEC(11);
	case 11:
		ALLOC_INVTLB_VEC(10);
	case 10:
		ALLOC_INVTLB_VEC(9);
	case 9:
		ALLOC_INVTLB_VEC(8);
	case 8:
		ALLOC_INVTLB_VEC(7);
	case 7:
		ALLOC_INVTLB_VEC(6);
	case 6:
		ALLOC_INVTLB_VEC(5);
	case 5:
		ALLOC_INVTLB_VEC(4);
	case 4:
		ALLOC_INVTLB_VEC(3);
	case 3:
		ALLOC_INVTLB_VEC(2);
	case 2:
		ALLOC_INVTLB_VEC(1);
	case 1:
		ALLOC_INVTLB_VEC(0);
		break;
	}

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
#if defined(CONFIG_X86_MCE) && defined(CONFIG_X86_LOCAL_APIC)
	alloc_intr_gate(MCE_SELF_VECTOR, mce_self_interrupt);
#endif

#if defined(CONFIG_X86_64) || defined(CONFIG_X86_LOCAL_APIC)
	/* self generated IPI for local APIC timer */
	alloc_intr_gate(LOCAL_TIMER_VECTOR, apic_timer_interrupt);

	/* IPI for X86 platform specific use */
	alloc_intr_gate(X86_PLATFORM_IPI_VECTOR, x86_platform_ipi);

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
	for (i = FIRST_EXTERNAL_VECTOR; i < NR_VECTORS; i++) {
		/* IA32_SYSCALL_VECTOR could be used in trap_init already. */
		if (!test_bit(i, used_vectors))
			set_intr_gate(i, interrupt[i-FIRST_EXTERNAL_VECTOR]);
	}

	if (!acpi_ioapic && !of_ioapic)
		setup_irq(2, &irq2);

#ifdef CONFIG_X86_32
	/*
	 * External FPU? Set up irq13 if so, for
	 * original braindamaged IBM FERR coupling.
	 */
	if (boot_cpu_data.hard_math && !cpu_has_fpu)
		setup_irq(FPU_IRQ, &fpu_irq);

	irq_ctx_init(smp_processor_id());
#endif
}
