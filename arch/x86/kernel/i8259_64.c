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
static void (*interrupt[NR_VECTORS - FIRST_EXTERNAL_VECTOR])(void) = {
					  IRQLIST_16(0x2), IRQLIST_16(0x3),
	IRQLIST_16(0x4), IRQLIST_16(0x5), IRQLIST_16(0x6), IRQLIST_16(0x7),
	IRQLIST_16(0x8), IRQLIST_16(0x9), IRQLIST_16(0xa), IRQLIST_16(0xb),
	IRQLIST_16(0xc), IRQLIST_16(0xd), IRQLIST_16(0xe), IRQLIST_16(0xf)
};

#undef IRQ
#undef IRQLIST_16

/*
 * This is the 'legacy' 8259A Programmable Interrupt Controller,
 * present in the majority of PC/AT boxes.
 * plus some generic x86 specific things if generic specifics makes
 * any sense at all.
 * this file should become arch/i386/kernel/irq.c when the old irq.c
 * moves to arch independent land
 */

static int i8259A_auto_eoi;
DEFINE_SPINLOCK(i8259A_lock);
static void mask_and_ack_8259A(unsigned int);

static struct irq_chip i8259A_chip = {
	.name		= "XT-PIC",
	.mask		= disable_8259A_irq,
	.disable	= disable_8259A_irq,
	.unmask		= enable_8259A_irq,
	.mask_ack	= mask_and_ack_8259A,
};

/*
 * 8259A PIC functions to handle ISA devices:
 */

/*
 * This contains the irq mask for both 8259A irq controllers,
 */
static unsigned int cached_irq_mask = 0xffff;

#define __byte(x,y) 	(((unsigned char *)&(y))[x])
#define cached_21	(__byte(0,cached_irq_mask))
#define cached_A1	(__byte(1,cached_irq_mask))

/*
 * Not all IRQs can be routed through the IO-APIC, eg. on certain (older)
 * boards the timer interrupt is not really connected to any IO-APIC pin,
 * it's fed to the master 8259A's IR0 line only.
 *
 * Any '1' bit in this mask means the IRQ is routed through the IO-APIC.
 * this 'mixed mode' IRQ handling costs nothing because it's only used
 * at IRQ setup time.
 */
unsigned long io_apic_irqs;

void disable_8259A_irq(unsigned int irq)
{
	unsigned int mask = 1 << irq;
	unsigned long flags;

	spin_lock_irqsave(&i8259A_lock, flags);
	cached_irq_mask |= mask;
	if (irq & 8)
		outb(cached_A1,0xA1);
	else
		outb(cached_21,0x21);
	spin_unlock_irqrestore(&i8259A_lock, flags);
}

void enable_8259A_irq(unsigned int irq)
{
	unsigned int mask = ~(1 << irq);
	unsigned long flags;

	spin_lock_irqsave(&i8259A_lock, flags);
	cached_irq_mask &= mask;
	if (irq & 8)
		outb(cached_A1,0xA1);
	else
		outb(cached_21,0x21);
	spin_unlock_irqrestore(&i8259A_lock, flags);
}

int i8259A_irq_pending(unsigned int irq)
{
	unsigned int mask = 1<<irq;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&i8259A_lock, flags);
	if (irq < 8)
		ret = inb(0x20) & mask;
	else
		ret = inb(0xA0) & (mask >> 8);
	spin_unlock_irqrestore(&i8259A_lock, flags);

	return ret;
}

void make_8259A_irq(unsigned int irq)
{
	disable_irq_nosync(irq);
	io_apic_irqs &= ~(1<<irq);
	set_irq_chip_and_handler_name(irq, &i8259A_chip, handle_level_irq,
				      "XT");
	enable_irq(irq);
}

/*
 * This function assumes to be called rarely. Switching between
 * 8259A registers is slow.
 * This has to be protected by the irq controller spinlock
 * before being called.
 */
static inline int i8259A_irq_real(unsigned int irq)
{
	int value;
	int irqmask = 1<<irq;

	if (irq < 8) {
		outb(0x0B,0x20);		/* ISR register */
		value = inb(0x20) & irqmask;
		outb(0x0A,0x20);		/* back to the IRR register */
		return value;
	}
	outb(0x0B,0xA0);		/* ISR register */
	value = inb(0xA0) & (irqmask >> 8);
	outb(0x0A,0xA0);		/* back to the IRR register */
	return value;
}

/*
 * Careful! The 8259A is a fragile beast, it pretty
 * much _has_ to be done exactly like this (mask it
 * first, _then_ send the EOI, and the order of EOI
 * to the two 8259s is important!
 */
static void mask_and_ack_8259A(unsigned int irq)
{
	unsigned int irqmask = 1 << irq;
	unsigned long flags;

	spin_lock_irqsave(&i8259A_lock, flags);
	/*
	 * Lightweight spurious IRQ detection. We do not want
	 * to overdo spurious IRQ handling - it's usually a sign
	 * of hardware problems, so we only do the checks we can
	 * do without slowing down good hardware unnecessarily.
	 *
	 * Note that IRQ7 and IRQ15 (the two spurious IRQs
	 * usually resulting from the 8259A-1|2 PICs) occur
	 * even if the IRQ is masked in the 8259A. Thus we
	 * can check spurious 8259A IRQs without doing the
	 * quite slow i8259A_irq_real() call for every IRQ.
	 * This does not cover 100% of spurious interrupts,
	 * but should be enough to warn the user that there
	 * is something bad going on ...
	 */
	if (cached_irq_mask & irqmask)
		goto spurious_8259A_irq;
	cached_irq_mask |= irqmask;

handle_real_irq:
	if (irq & 8) {
		inb(0xA1);		/* DUMMY - (do we need this?) */
		outb(cached_A1,0xA1);
		outb(0x60+(irq&7),0xA0);/* 'Specific EOI' to slave */
		outb(0x62,0x20);	/* 'Specific EOI' to master-IRQ2 */
	} else {
		inb(0x21);		/* DUMMY - (do we need this?) */
		outb(cached_21,0x21);
		outb(0x60+irq,0x20);	/* 'Specific EOI' to master */
	}
	spin_unlock_irqrestore(&i8259A_lock, flags);
	return;

spurious_8259A_irq:
	/*
	 * this is the slow path - should happen rarely.
	 */
	if (i8259A_irq_real(irq))
		/*
		 * oops, the IRQ _is_ in service according to the
		 * 8259A - not spurious, go handle it.
		 */
		goto handle_real_irq;

	{
		static int spurious_irq_mask;
		/*
		 * At this point we can be sure the IRQ is spurious,
		 * lets ACK and report it. [once per IRQ]
		 */
		if (!(spurious_irq_mask & irqmask)) {
			printk(KERN_DEBUG "spurious 8259A interrupt: IRQ%d.\n", irq);
			spurious_irq_mask |= irqmask;
		}
		atomic_inc(&irq_err_count);
		/*
		 * Theoretically we do not have to handle this IRQ,
		 * but in Linux this does not cause problems and is
		 * simpler for us.
		 */
		goto handle_real_irq;
	}
}

void init_8259A(int auto_eoi)
{
	unsigned long flags;

	i8259A_auto_eoi = auto_eoi;

	spin_lock_irqsave(&i8259A_lock, flags);

	outb(0xff, 0x21);	/* mask all of 8259A-1 */
	outb(0xff, 0xA1);	/* mask all of 8259A-2 */

	/*
	 * outb_p - this has to work on a wide range of PC hardware.
	 */
	outb_p(0x11, 0x20);	/* ICW1: select 8259A-1 init */
	outb_p(IRQ0_VECTOR, 0x21);	/* ICW2: 8259A-1 IR0-7 mapped to 0x30-0x37 */
	outb_p(0x04, 0x21);	/* 8259A-1 (the master) has a slave on IR2 */
	if (auto_eoi)
		outb_p(0x03, 0x21);	/* master does Auto EOI */
	else
		outb_p(0x01, 0x21);	/* master expects normal EOI */

	outb_p(0x11, 0xA0);	/* ICW1: select 8259A-2 init */
	outb_p(IRQ8_VECTOR, 0xA1);	/* ICW2: 8259A-2 IR0-7 mapped to 0x38-0x3f */
	outb_p(0x02, 0xA1);	/* 8259A-2 is a slave on master's IR2 */
	outb_p(0x01, 0xA1);	/* (slave's support for AEOI in flat mode
				    is to be investigated) */

	if (auto_eoi)
		/*
		 * in AEOI mode we just have to mask the interrupt
		 * when acking.
		 */
		i8259A_chip.mask_ack = disable_8259A_irq;
	else
		i8259A_chip.mask_ack = mask_and_ack_8259A;

	udelay(100);		/* wait for 8259A to initialize */

	outb(cached_21, 0x21);	/* restore master IRQ mask */
	outb(cached_A1, 0xA1);	/* restore slave IRQ mask */

	spin_unlock_irqrestore(&i8259A_lock, flags);
}

static char irq_trigger[2];
/**
 * ELCR registers (0x4d0, 0x4d1) control edge/level of IRQ
 */
static void restore_ELCR(char *trigger)
{
	outb(trigger[0], 0x4d0);
	outb(trigger[1], 0x4d1);
}

static void save_ELCR(char *trigger)
{
	/* IRQ 0,1,2,8,13 are marked as reserved */
	trigger[0] = inb(0x4d0) & 0xF8;
	trigger[1] = inb(0x4d1) & 0xDE;
}

static int i8259A_resume(struct sys_device *dev)
{
	init_8259A(i8259A_auto_eoi);
	restore_ELCR(irq_trigger);
	return 0;
}

static int i8259A_suspend(struct sys_device *dev, pm_message_t state)
{
	save_ELCR(irq_trigger);
	return 0;
}

static int i8259A_shutdown(struct sys_device *dev)
{
	/* Put the i8259A into a quiescent state that
	 * the kernel initialization code can get it
	 * out of.
	 */
	outb(0xff, 0x21);	/* mask all of 8259A-1 */
	outb(0xff, 0xA1);	/* mask all of 8259A-1 */
	return 0;
}

static struct sysdev_class i8259_sysdev_class = {
	.name = "i8259",
	.suspend = i8259A_suspend,
	.resume = i8259A_resume,
	.shutdown = i8259A_shutdown,
};

static struct sys_device device_i8259A = {
	.id	= 0,
	.cls	= &i8259_sysdev_class,
};

static int __init i8259A_init_sysfs(void)
{
	int error = sysdev_class_register(&i8259_sysdev_class);
	if (!error)
		error = sysdev_register(&device_i8259A);
	return error;
}

device_initcall(i8259A_init_sysfs);

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

void __init init_ISA_irqs (void)
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

void __init init_IRQ(void)
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
	set_intr_gate(RESCHEDULE_VECTOR, reschedule_interrupt);

	/* IPIs for invalidation */
	set_intr_gate(INVALIDATE_TLB_VECTOR_START+0, invalidate_interrupt0);
	set_intr_gate(INVALIDATE_TLB_VECTOR_START+1, invalidate_interrupt1);
	set_intr_gate(INVALIDATE_TLB_VECTOR_START+2, invalidate_interrupt2);
	set_intr_gate(INVALIDATE_TLB_VECTOR_START+3, invalidate_interrupt3);
	set_intr_gate(INVALIDATE_TLB_VECTOR_START+4, invalidate_interrupt4);
	set_intr_gate(INVALIDATE_TLB_VECTOR_START+5, invalidate_interrupt5);
	set_intr_gate(INVALIDATE_TLB_VECTOR_START+6, invalidate_interrupt6);
	set_intr_gate(INVALIDATE_TLB_VECTOR_START+7, invalidate_interrupt7);

	/* IPI for generic function call */
	set_intr_gate(CALL_FUNCTION_VECTOR, call_function_interrupt);

	/* Low priority IPI to cleanup after moving an irq */
	set_intr_gate(IRQ_MOVE_CLEANUP_VECTOR, irq_move_cleanup_interrupt);
#endif
	set_intr_gate(THERMAL_APIC_VECTOR, thermal_interrupt);
	set_intr_gate(THRESHOLD_APIC_VECTOR, threshold_interrupt);

	/* self generated IPI for local APIC timer */
	set_intr_gate(LOCAL_TIMER_VECTOR, apic_timer_interrupt);

	/* IPI vectors for APIC spurious and error interrupts */
	set_intr_gate(SPURIOUS_APIC_VECTOR, spurious_interrupt);
	set_intr_gate(ERROR_APIC_VECTOR, error_interrupt);

	if (!acpi_ioapic)
		setup_irq(2, &irq2);
}
