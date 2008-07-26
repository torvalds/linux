#include <linux/linkage.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/sysdev.h>
#include <linux/bitops.h>

#include <asm/acpi.h>
#include <asm/atomic.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/hw_irq.h>
#include <asm/pgtable.h>
#include <asm/delay.h>
#include <asm/desc.h>
#include <asm/apic.h>
#include <asm/i8259.h>

/*
 * Common place to define all x86 IRQ vectors
 *
 * This builds up the IRQ handler stubs using some ugly macros in irq.h
 *
 * These macros create the low-level assembly IRQ routines that save
 * register context and call do_IRQ(). do_IRQ() then does all the
 * operations that are needed to keep the AT (or SMP IOAPIC)
 * interrupt-controller happy.
 */

#define IRQ_NAME2(nr) nr##_interrupt(void)
#define IRQ_NAME(nr) IRQ_NAME2(IRQ##nr)

/*
 *	SMP has a few special interrupts for IPI messages
 */

#define BUILD_IRQ(nr)				\
	asmlinkage void IRQ_NAME(nr);		\
	asm("\n.text\n.p2align\n"		\
	    "IRQ" #nr "_interrupt:\n\t"		\
	    "push $~(" #nr ") ; "		\
	    "jmp common_interrupt\n"		\
	    ".previous");

#define BI(x,y) \
	BUILD_IRQ(x##y)

#define BUILD_16_IRQS(x) \
	BI(x,0) BI(x,1) BI(x,2) BI(x,3) \
	BI(x,4) BI(x,5) BI(x,6) BI(x,7) \
	BI(x,8) BI(x,9) BI(x,a) BI(x,b) \
	BI(x,c) BI(x,d) BI(x,e) BI(x,f)

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
				      BUILD_16_IRQS(0x2) BUILD_16_IRQS(0x3)
BUILD_16_IRQS(0x4) BUILD_16_IRQS(0x5) BUILD_16_IRQS(0x6) BUILD_16_IRQS(0x7)
BUILD_16_IRQS(0x8) BUILD_16_IRQS(0x9) BUILD_16_IRQS(0xa) BUILD_16_IRQS(0xb)
BUILD_16_IRQS(0xc) BUILD_16_IRQS(0xd) BUILD_16_IRQS(0xe) BUILD_16_IRQS(0xf)

#undef BUILD_16_IRQS
#undef BI


#define IRQ(x,y) \
	IRQ##x##y##_interrupt

#define IRQLIST_16(x) \
	IRQ(x,0), IRQ(x,1), IRQ(x,2), IRQ(x,3), \
	IRQ(x,4), IRQ(x,5), IRQ(x,6), IRQ(x,7), \
	IRQ(x,8), IRQ(x,9), IRQ(x,a), IRQ(x,b), \
	IRQ(x,c), IRQ(x,d), IRQ(x,e), IRQ(x,f)

/* for the irq vectors */
static void (*__initdata interrupt[NR_VECTORS - FIRST_EXTERNAL_VECTOR])(void) = {
					  IRQLIST_16(0x2), IRQLIST_16(0x3),
	IRQLIST_16(0x4), IRQLIST_16(0x5), IRQLIST_16(0x6), IRQLIST_16(0x7),
	IRQLIST_16(0x8), IRQLIST_16(0x9), IRQLIST_16(0xa), IRQLIST_16(0xb),
	IRQLIST_16(0xc), IRQLIST_16(0xd), IRQLIST_16(0xe), IRQLIST_16(0xf)
};

#undef IRQ
#undef IRQLIST_16




/*
 * IRQ2 is cascade interrupt to second interrupt controller
 */

static struct irqaction irq2 = {
	.handler = no_action,
	.mask = CPU_MASK_NONE,
	.name = "cascade",
};
DEFINE_PER_CPU(vector_irq_t, vector_irq) = {
	[0 ... IRQ0_VECTOR - 1] = -1,
	[IRQ0_VECTOR] = 0,
	[IRQ1_VECTOR] = 1,
	[IRQ2_VECTOR] = 2,
	[IRQ3_VECTOR] = 3,
	[IRQ4_VECTOR] = 4,
	[IRQ5_VECTOR] = 5,
	[IRQ6_VECTOR] = 6,
	[IRQ7_VECTOR] = 7,
	[IRQ8_VECTOR] = 8,
	[IRQ9_VECTOR] = 9,
	[IRQ10_VECTOR] = 10,
	[IRQ11_VECTOR] = 11,
	[IRQ12_VECTOR] = 12,
	[IRQ13_VECTOR] = 13,
	[IRQ14_VECTOR] = 14,
	[IRQ15_VECTOR] = 15,
	[IRQ15_VECTOR + 1 ... NR_VECTORS - 1] = -1
};

static void __init init_ISA_irqs (void)
{
	int i;

	init_bsp_APIC();
	init_8259A(0);

	for (i = 0; i < NR_IRQS; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = NULL;
		irq_desc[i].depth = 1;

		if (i < 16) {
			/*
			 * 16 old-style INTA-cycle interrupts:
			 */
			set_irq_chip_and_handler_name(i, &i8259A_chip,
						      handle_level_irq, "XT");
		} else {
			/*
			 * 'high' PCI IRQs filled in on demand
			 */
			irq_desc[i].chip = &no_irq_chip;
		}
	}
}

void init_IRQ(void) __attribute__((weak, alias("native_init_IRQ")));

void __init native_init_IRQ(void)
{
	int i;

	init_ISA_irqs();
	/*
	 * Cover the whole vector space, no vector can escape
	 * us. (some of these will be overridden and become
	 * 'special' SMP interrupts)
	 */
	for (i = 0; i < (NR_VECTORS - FIRST_EXTERNAL_VECTOR); i++) {
		int vector = FIRST_EXTERNAL_VECTOR + i;
		if (vector != IA32_SYSCALL_VECTOR)
			set_intr_gate(vector, interrupt[i]);
	}

#ifdef CONFIG_SMP
	/*
	 * The reschedule interrupt is a CPU-to-CPU reschedule-helper
	 * IPI, driven by wakeup.
	 */
	alloc_intr_gate(RESCHEDULE_VECTOR, reschedule_interrupt);

	/* IPIs for invalidation */
	alloc_intr_gate(INVALIDATE_TLB_VECTOR_START+0, invalidate_interrupt0);
	alloc_intr_gate(INVALIDATE_TLB_VECTOR_START+1, invalidate_interrupt1);
	alloc_intr_gate(INVALIDATE_TLB_VECTOR_START+2, invalidate_interrupt2);
	alloc_intr_gate(INVALIDATE_TLB_VECTOR_START+3, invalidate_interrupt3);
	alloc_intr_gate(INVALIDATE_TLB_VECTOR_START+4, invalidate_interrupt4);
	alloc_intr_gate(INVALIDATE_TLB_VECTOR_START+5, invalidate_interrupt5);
	alloc_intr_gate(INVALIDATE_TLB_VECTOR_START+6, invalidate_interrupt6);
	alloc_intr_gate(INVALIDATE_TLB_VECTOR_START+7, invalidate_interrupt7);

	/* IPI for generic function call */
	alloc_intr_gate(CALL_FUNCTION_VECTOR, call_function_interrupt);

	/* IPI for generic single function call */
	alloc_intr_gate(CALL_FUNCTION_SINGLE_VECTOR,
			call_function_single_interrupt);

	/* Low priority IPI to cleanup after moving an irq */
	set_intr_gate(IRQ_MOVE_CLEANUP_VECTOR, irq_move_cleanup_interrupt);
#endif
	alloc_intr_gate(THERMAL_APIC_VECTOR, thermal_interrupt);
	alloc_intr_gate(THRESHOLD_APIC_VECTOR, threshold_interrupt);

	/* self generated IPI for local APIC timer */
	alloc_intr_gate(LOCAL_TIMER_VECTOR, apic_timer_interrupt);

	/* IPI vectors for APIC spurious and error interrupts */
	alloc_intr_gate(SPURIOUS_APIC_VECTOR, spurious_interrupt);
	alloc_intr_gate(ERROR_APIC_VECTOR, error_interrupt);

	if (!acpi_ioapic)
		setup_irq(2, &irq2);
}
