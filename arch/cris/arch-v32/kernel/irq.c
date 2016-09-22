/*
 * Copyright (C) 2003, Axis Communications AB.
 */

#include <asm/irq.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/profile.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/threads.h>
#include <linux/spinlock.h>
#include <linux/kernel_stat.h>
#include <hwregs/reg_map.h>
#include <hwregs/reg_rdwr.h>
#include <hwregs/intr_vect.h>
#include <hwregs/intr_vect_defs.h>

#define CPU_FIXED -1

/* IRQ masks (refer to comment for crisv32_do_multiple) */
#if TIMER0_INTR_VECT - FIRST_IRQ < 32
#define TIMER_MASK (1 << (TIMER0_INTR_VECT - FIRST_IRQ))
#undef TIMER_VECT1
#else
#define TIMER_MASK (1 << (TIMER0_INTR_VECT - FIRST_IRQ - 32))
#define TIMER_VECT1
#endif
#ifdef CONFIG_ETRAX_KGDB
#if defined(CONFIG_ETRAX_KGDB_PORT0)
#define IGNOREMASK (1 << (SER0_INTR_VECT - FIRST_IRQ))
#elif defined(CONFIG_ETRAX_KGDB_PORT1)
#define IGNOREMASK (1 << (SER1_INTR_VECT - FIRST_IRQ))
#elif defined(CONFIG_ETRAX_KGDB_PORT2)
#define IGNOREMASK (1 << (SER2_INTR_VECT - FIRST_IRQ))
#elif defined(CONFIG_ETRAX_KGDB_PORT3)
#define IGNOREMASK (1 << (SER3_INTR_VECT - FIRST_IRQ))
#endif
#endif

DEFINE_SPINLOCK(irq_lock);

struct cris_irq_allocation
{
  int cpu; /* The CPU to which the IRQ is currently allocated. */
  cpumask_t mask; /* The CPUs to which the IRQ may be allocated. */
};

struct cris_irq_allocation irq_allocations[NR_REAL_IRQS] =
  { [0 ... NR_REAL_IRQS - 1] = {0, CPU_MASK_ALL} };

static unsigned long irq_regs[NR_CPUS] =
{
  regi_irq,
};

#if NR_REAL_IRQS > 32
#define NBR_REGS 2
#else
#define NBR_REGS 1
#endif

unsigned long cpu_irq_counters[NR_CPUS];
unsigned long irq_counters[NR_REAL_IRQS];

/* From irq.c. */
extern void weird_irq(void);

/* From entry.S. */
extern void system_call(void);
extern void nmi_interrupt(void);
extern void multiple_interrupt(void);
extern void gdb_handle_exception(void);
extern void i_mmu_refill(void);
extern void i_mmu_invalid(void);
extern void i_mmu_access(void);
extern void i_mmu_execute(void);
extern void d_mmu_refill(void);
extern void d_mmu_invalid(void);
extern void d_mmu_access(void);
extern void d_mmu_write(void);

/* From kgdb.c. */
extern void kgdb_init(void);
extern void breakpoint(void);

/* From traps.c.  */
extern void breakh_BUG(void);

/*
 * Build the IRQ handler stubs using macros from irq.h.
 */
#ifdef CONFIG_CRIS_MACH_ARTPEC3
BUILD_TIMER_IRQ(0x31, 0)
#else
BUILD_IRQ(0x31)
#endif
BUILD_IRQ(0x32)
BUILD_IRQ(0x33)
BUILD_IRQ(0x34)
BUILD_IRQ(0x35)
BUILD_IRQ(0x36)
BUILD_IRQ(0x37)
BUILD_IRQ(0x38)
BUILD_IRQ(0x39)
BUILD_IRQ(0x3a)
BUILD_IRQ(0x3b)
BUILD_IRQ(0x3c)
BUILD_IRQ(0x3d)
BUILD_IRQ(0x3e)
BUILD_IRQ(0x3f)
BUILD_IRQ(0x40)
BUILD_IRQ(0x41)
BUILD_IRQ(0x42)
BUILD_IRQ(0x43)
BUILD_IRQ(0x44)
BUILD_IRQ(0x45)
BUILD_IRQ(0x46)
BUILD_IRQ(0x47)
BUILD_IRQ(0x48)
BUILD_IRQ(0x49)
BUILD_IRQ(0x4a)
#ifdef CONFIG_ETRAXFS
BUILD_TIMER_IRQ(0x4b, 0)
#else
BUILD_IRQ(0x4b)
#endif
BUILD_IRQ(0x4c)
BUILD_IRQ(0x4d)
BUILD_IRQ(0x4e)
BUILD_IRQ(0x4f)
BUILD_IRQ(0x50)
#if MACH_IRQS > 32
BUILD_IRQ(0x51)
BUILD_IRQ(0x52)
BUILD_IRQ(0x53)
BUILD_IRQ(0x54)
BUILD_IRQ(0x55)
BUILD_IRQ(0x56)
BUILD_IRQ(0x57)
BUILD_IRQ(0x58)
BUILD_IRQ(0x59)
BUILD_IRQ(0x5a)
BUILD_IRQ(0x5b)
BUILD_IRQ(0x5c)
BUILD_IRQ(0x5d)
BUILD_IRQ(0x5e)
BUILD_IRQ(0x5f)
BUILD_IRQ(0x60)
BUILD_IRQ(0x61)
BUILD_IRQ(0x62)
BUILD_IRQ(0x63)
BUILD_IRQ(0x64)
BUILD_IRQ(0x65)
BUILD_IRQ(0x66)
BUILD_IRQ(0x67)
BUILD_IRQ(0x68)
BUILD_IRQ(0x69)
BUILD_IRQ(0x6a)
BUILD_IRQ(0x6b)
BUILD_IRQ(0x6c)
BUILD_IRQ(0x6d)
BUILD_IRQ(0x6e)
BUILD_IRQ(0x6f)
BUILD_IRQ(0x70)
#endif

/* Pointers to the low-level handlers. */
static void (*interrupt[MACH_IRQS])(void) = {
	IRQ0x31_interrupt, IRQ0x32_interrupt, IRQ0x33_interrupt,
	IRQ0x34_interrupt, IRQ0x35_interrupt, IRQ0x36_interrupt,
	IRQ0x37_interrupt, IRQ0x38_interrupt, IRQ0x39_interrupt,
	IRQ0x3a_interrupt, IRQ0x3b_interrupt, IRQ0x3c_interrupt,
	IRQ0x3d_interrupt, IRQ0x3e_interrupt, IRQ0x3f_interrupt,
	IRQ0x40_interrupt, IRQ0x41_interrupt, IRQ0x42_interrupt,
	IRQ0x43_interrupt, IRQ0x44_interrupt, IRQ0x45_interrupt,
	IRQ0x46_interrupt, IRQ0x47_interrupt, IRQ0x48_interrupt,
	IRQ0x49_interrupt, IRQ0x4a_interrupt, IRQ0x4b_interrupt,
	IRQ0x4c_interrupt, IRQ0x4d_interrupt, IRQ0x4e_interrupt,
	IRQ0x4f_interrupt, IRQ0x50_interrupt,
#if MACH_IRQS > 32
	IRQ0x51_interrupt, IRQ0x52_interrupt, IRQ0x53_interrupt,
	IRQ0x54_interrupt, IRQ0x55_interrupt, IRQ0x56_interrupt,
	IRQ0x57_interrupt, IRQ0x58_interrupt, IRQ0x59_interrupt,
	IRQ0x5a_interrupt, IRQ0x5b_interrupt, IRQ0x5c_interrupt,
	IRQ0x5d_interrupt, IRQ0x5e_interrupt, IRQ0x5f_interrupt,
	IRQ0x60_interrupt, IRQ0x61_interrupt, IRQ0x62_interrupt,
	IRQ0x63_interrupt, IRQ0x64_interrupt, IRQ0x65_interrupt,
	IRQ0x66_interrupt, IRQ0x67_interrupt, IRQ0x68_interrupt,
	IRQ0x69_interrupt, IRQ0x6a_interrupt, IRQ0x6b_interrupt,
	IRQ0x6c_interrupt, IRQ0x6d_interrupt, IRQ0x6e_interrupt,
	IRQ0x6f_interrupt, IRQ0x70_interrupt,
#endif
};

void
block_irq(int irq, int cpu)
{
	int intr_mask;
        unsigned long flags;

	spin_lock_irqsave(&irq_lock, flags);
	/* Remember, 1 let thru, 0 block. */
	if (irq - FIRST_IRQ < 32) {
		intr_mask = REG_RD_INT_VECT(intr_vect, irq_regs[cpu],
			rw_mask, 0);
		intr_mask &= ~(1 << (irq - FIRST_IRQ));
		REG_WR_INT_VECT(intr_vect, irq_regs[cpu], rw_mask,
			0, intr_mask);
	} else {
		intr_mask = REG_RD_INT_VECT(intr_vect, irq_regs[cpu],
			rw_mask, 1);
		intr_mask &= ~(1 << (irq - FIRST_IRQ - 32));
		REG_WR_INT_VECT(intr_vect, irq_regs[cpu], rw_mask,
			1, intr_mask);
	}
        spin_unlock_irqrestore(&irq_lock, flags);
}

void
unblock_irq(int irq, int cpu)
{
	int intr_mask;
        unsigned long flags;

        spin_lock_irqsave(&irq_lock, flags);
	/* Remember, 1 let thru, 0 block. */
	if (irq - FIRST_IRQ < 32) {
		intr_mask = REG_RD_INT_VECT(intr_vect, irq_regs[cpu],
			rw_mask, 0);
		intr_mask |= (1 << (irq - FIRST_IRQ));
		REG_WR_INT_VECT(intr_vect, irq_regs[cpu], rw_mask,
			0, intr_mask);
	} else {
		intr_mask = REG_RD_INT_VECT(intr_vect, irq_regs[cpu],
			rw_mask, 1);
		intr_mask |= (1 << (irq - FIRST_IRQ - 32));
		REG_WR_INT_VECT(intr_vect, irq_regs[cpu], rw_mask,
			1, intr_mask);
	}
        spin_unlock_irqrestore(&irq_lock, flags);
}

/* Find out which CPU the irq should be allocated to. */
static int irq_cpu(int irq)
{
	int cpu;
        unsigned long flags;

        spin_lock_irqsave(&irq_lock, flags);
        cpu = irq_allocations[irq - FIRST_IRQ].cpu;

	/* Fixed interrupts stay on the local CPU. */
	if (cpu == CPU_FIXED)
        {
		spin_unlock_irqrestore(&irq_lock, flags);
		return smp_processor_id();
        }


	/* Let the interrupt stay if possible */
	if (cpumask_test_cpu(cpu, &irq_allocations[irq - FIRST_IRQ].mask))
		goto out;

	/* IRQ must be moved to another CPU. */
	cpu = cpumask_first(&irq_allocations[irq - FIRST_IRQ].mask);
	irq_allocations[irq - FIRST_IRQ].cpu = cpu;
out:
	spin_unlock_irqrestore(&irq_lock, flags);
	return cpu;
}

void crisv32_mask_irq(int irq)
{
	int cpu;

	for (cpu = 0; cpu < NR_CPUS; cpu++)
		block_irq(irq, cpu);
}

void crisv32_unmask_irq(int irq)
{
	unblock_irq(irq, irq_cpu(irq));
}


static void enable_crisv32_irq(struct irq_data *data)
{
	crisv32_unmask_irq(data->irq);
}

static void disable_crisv32_irq(struct irq_data *data)
{
	crisv32_mask_irq(data->irq);
}

static int set_affinity_crisv32_irq(struct irq_data *data,
				    const struct cpumask *dest, bool force)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_lock, flags);
	irq_allocations[data->irq - FIRST_IRQ].mask = *dest;
	spin_unlock_irqrestore(&irq_lock, flags);
	return 0;
}

static struct irq_chip crisv32_irq_type = {
	.name			= "CRISv32",
	.irq_shutdown		= disable_crisv32_irq,
	.irq_enable		= enable_crisv32_irq,
	.irq_disable		= disable_crisv32_irq,
	.irq_set_affinity	= set_affinity_crisv32_irq,
};

void
set_exception_vector(int n, irqvectptr addr)
{
	etrax_irv->v[n] = (irqvectptr) addr;
}

extern void do_IRQ(int irq, struct pt_regs * regs);

void
crisv32_do_IRQ(int irq, int block, struct pt_regs* regs)
{
	/* Interrupts that may not be moved to another CPU may
	 * skip blocking. This is currently only valid for the
	 * timer IRQ and the IPI and is used for the timer
	 * interrupt to avoid watchdog starvation.
	 */
	if (!block) {
		do_IRQ(irq, regs);
		return;
	}

	block_irq(irq, smp_processor_id());
	do_IRQ(irq, regs);

	unblock_irq(irq, irq_cpu(irq));
}

/* If multiple interrupts occur simultaneously we get a multiple
 * interrupt from the CPU and software has to sort out which
 * interrupts that happened. There are two special cases here:
 *
 * 1. Timer interrupts may never be blocked because of the
 *    watchdog (refer to comment in include/asr/arch/irq.h)
 * 2. GDB serial port IRQs are unhandled here and will be handled
 *    as a single IRQ when it strikes again because the GDB
 *    stubb wants to save the registers in its own fashion.
 */
void
crisv32_do_multiple(struct pt_regs* regs)
{
	int cpu;
	int mask;
	int masked[NBR_REGS];
	int bit;
	int i;

	cpu = smp_processor_id();

	/* An extra irq_enter here to prevent softIRQs to run after
         * each do_IRQ. This will decrease the interrupt latency.
	 */
	irq_enter();

	for (i = 0; i < NBR_REGS; i++) {
		/* Get which IRQs that happened. */
		masked[i] = REG_RD_INT_VECT(intr_vect, irq_regs[cpu],
			r_masked_vect, i);

		/* Calculate new IRQ mask with these IRQs disabled. */
		mask = REG_RD_INT_VECT(intr_vect, irq_regs[cpu], rw_mask, i);
		mask &= ~masked[i];

	/* Timer IRQ is never masked */
#ifdef TIMER_VECT1
		if ((i == 1) && (masked[0] & TIMER_MASK))
			mask |= TIMER_MASK;
#else
		if ((i == 0) && (masked[0] & TIMER_MASK))
			mask |= TIMER_MASK;
#endif
		/* Block all the IRQs */
		REG_WR_INT_VECT(intr_vect, irq_regs[cpu], rw_mask, i, mask);

	/* Check for timer IRQ and handle it special. */
#ifdef TIMER_VECT1
		if ((i == 1) && (masked[i] & TIMER_MASK)) {
			masked[i] &= ~TIMER_MASK;
			do_IRQ(TIMER0_INTR_VECT, regs);
		}
#else
		if ((i == 0) && (masked[i] & TIMER_MASK)) {
			 masked[i] &= ~TIMER_MASK;
			 do_IRQ(TIMER0_INTR_VECT, regs);
		}
#endif
	}

#ifdef IGNORE_MASK
	/* Remove IRQs that can't be handled as multiple. */
	masked[0] &= ~IGNORE_MASK;
#endif

	/* Handle the rest of the IRQs. */
	for (i = 0; i < NBR_REGS; i++) {
		for (bit = 0; bit < 32; bit++) {
			if (masked[i] & (1 << bit))
				do_IRQ(bit + FIRST_IRQ + i*32, regs);
		}
	}

	/* Unblock all the IRQs. */
	for (i = 0; i < NBR_REGS; i++) {
		mask = REG_RD_INT_VECT(intr_vect, irq_regs[cpu], rw_mask, i);
		mask |= masked[i];
		REG_WR_INT_VECT(intr_vect, irq_regs[cpu], rw_mask, i, mask);
	}

	/* This irq_exit() will trigger the soft IRQs. */
	irq_exit();
}

static int crisv32_irq_map(struct irq_domain *h, unsigned int virq,
			   irq_hw_number_t hw_irq_num)
{
	irq_set_chip_and_handler(virq, &crisv32_irq_type, handle_simple_irq);

	return 0;
}

static struct irq_domain_ops crisv32_irq_ops = {
	.map	= crisv32_irq_map,
	.xlate	= irq_domain_xlate_onecell,
};

/*
 * This is called by start_kernel. It fixes the IRQ masks and setup the
 * interrupt vector table to point to bad_interrupt pointers.
 */
void __init
init_IRQ(void)
{
	int i;
	int j;
	reg_intr_vect_rw_mask vect_mask = {0};
	struct device_node *np;
	struct irq_domain *domain;

	/* Clear all interrupts masks. */
	for (i = 0; i < NBR_REGS; i++)
		REG_WR_VECT(intr_vect, regi_irq, rw_mask, i, vect_mask);

	for (i = 0; i < 256; i++)
		etrax_irv->v[i] = weird_irq;

	np = of_find_compatible_node(NULL, NULL, "axis,crisv32-intc");
	domain = irq_domain_add_legacy(np, NBR_INTR_VECT - FIRST_IRQ,
				       FIRST_IRQ, FIRST_IRQ,
				       &crisv32_irq_ops, NULL);
	BUG_ON(!domain);
	irq_set_default_host(domain);
	of_node_put(np);

	for (i = FIRST_IRQ, j = 0; j < NBR_INTR_VECT && j < MACH_IRQS; i++, j++)
		set_exception_vector(i, interrupt[j]);

	/* Mark Timer and IPI IRQs as CPU local */
	irq_allocations[TIMER0_INTR_VECT - FIRST_IRQ].cpu = CPU_FIXED;
	irq_set_status_flags(TIMER0_INTR_VECT, IRQ_PER_CPU);
	irq_allocations[IPI_INTR_VECT - FIRST_IRQ].cpu = CPU_FIXED;
	irq_set_status_flags(IPI_INTR_VECT, IRQ_PER_CPU);

	set_exception_vector(0x00, nmi_interrupt);
	set_exception_vector(0x30, multiple_interrupt);

	/* Set up handler for various MMU bus faults. */
	set_exception_vector(0x04, i_mmu_refill);
	set_exception_vector(0x05, i_mmu_invalid);
	set_exception_vector(0x06, i_mmu_access);
	set_exception_vector(0x07, i_mmu_execute);
	set_exception_vector(0x08, d_mmu_refill);
	set_exception_vector(0x09, d_mmu_invalid);
	set_exception_vector(0x0a, d_mmu_access);
	set_exception_vector(0x0b, d_mmu_write);

#ifdef CONFIG_BUG
	/* Break 14 handler, used to implement cheap BUG().  */
	set_exception_vector(0x1e, breakh_BUG);
#endif

	/* The system-call trap is reached by "break 13". */
	set_exception_vector(0x1d, system_call);

	/* Exception handlers for debugging, both user-mode and kernel-mode. */

	/* Break 8. */
	set_exception_vector(0x18, gdb_handle_exception);
	/* Hardware single step. */
	set_exception_vector(0x3, gdb_handle_exception);
	/* Hardware breakpoint. */
	set_exception_vector(0xc, gdb_handle_exception);

#ifdef CONFIG_ETRAX_KGDB
	kgdb_init();
	/* Everything is set up; now trap the kernel. */
	breakpoint();
#endif
}

