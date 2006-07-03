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
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/threads.h>
#include <linux/spinlock.h>
#include <linux/kernel_stat.h>
#include <asm/arch/hwregs/reg_map.h>
#include <asm/arch/hwregs/reg_rdwr.h>
#include <asm/arch/hwregs/intr_vect.h>
#include <asm/arch/hwregs/intr_vect_defs.h>

#define CPU_FIXED -1

/* IRQ masks (refer to comment for crisv32_do_multiple) */
#define TIMER_MASK (1 << (TIMER_INTR_VECT - FIRST_IRQ))
#ifdef CONFIG_ETRAX_KGDB
#if defined(CONFIG_ETRAX_KGDB_PORT0)
#define IGNOREMASK (1 << (SER0_INTR_VECT - FIRST_IRQ))
#elif defined(CONFIG_ETRAX_KGDB_PORT1)
#define IGNOREMASK (1 << (SER1_INTR_VECT - FIRST_IRQ))
#elif defined(CONFIG_ETRAX_KGB_PORT2)
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

struct cris_irq_allocation irq_allocations[NR_IRQS] =
  {[0 ... NR_IRQS - 1] = {0, CPU_MASK_ALL}};

static unsigned long irq_regs[NR_CPUS] =
{
  regi_irq,
#ifdef CONFIG_SMP
  regi_irq2,
#endif
};

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

/*
 * Build the IRQ handler stubs using macros from irq.h. First argument is the
 * IRQ number, the second argument is the corresponding bit in
 * intr_rw_vect_mask found in asm/arch/hwregs/intr_vect_defs.h.
 */
BUILD_IRQ(0x31, (1 << 0))	/* memarb */
BUILD_IRQ(0x32, (1 << 1))	/* gen_io */
BUILD_IRQ(0x33, (1 << 2))	/* iop0 */
BUILD_IRQ(0x34, (1 << 3))	/* iop1 */
BUILD_IRQ(0x35, (1 << 4))	/* iop2 */
BUILD_IRQ(0x36, (1 << 5))	/* iop3 */
BUILD_IRQ(0x37, (1 << 6))	/* dma0 */
BUILD_IRQ(0x38, (1 << 7))	/* dma1 */
BUILD_IRQ(0x39, (1 << 8))	/* dma2 */
BUILD_IRQ(0x3a, (1 << 9))	/* dma3 */
BUILD_IRQ(0x3b, (1 << 10))	/* dma4 */
BUILD_IRQ(0x3c, (1 << 11))	/* dma5 */
BUILD_IRQ(0x3d, (1 << 12))	/* dma6 */
BUILD_IRQ(0x3e, (1 << 13))	/* dma7 */
BUILD_IRQ(0x3f, (1 << 14))	/* dma8 */
BUILD_IRQ(0x40, (1 << 15))	/* dma9 */
BUILD_IRQ(0x41, (1 << 16))	/* ata */
BUILD_IRQ(0x42, (1 << 17))	/* sser0 */
BUILD_IRQ(0x43, (1 << 18))	/* sser1 */
BUILD_IRQ(0x44, (1 << 19))	/* ser0 */
BUILD_IRQ(0x45, (1 << 20))	/* ser1 */
BUILD_IRQ(0x46, (1 << 21))	/* ser2 */
BUILD_IRQ(0x47, (1 << 22))	/* ser3 */
BUILD_IRQ(0x48, (1 << 23))
BUILD_IRQ(0x49, (1 << 24))	/* eth0 */
BUILD_IRQ(0x4a, (1 << 25))	/* eth1 */
BUILD_TIMER_IRQ(0x4b, (1 << 26))/* timer */
BUILD_IRQ(0x4c, (1 << 27))	/* bif_arb */
BUILD_IRQ(0x4d, (1 << 28))	/* bif_dma */
BUILD_IRQ(0x4e, (1 << 29))	/* ext */
BUILD_IRQ(0x4f, (1 << 29))	/* ipi */

/* Pointers to the low-level handlers. */
static void (*interrupt[NR_IRQS])(void) = {
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
	IRQ0x4f_interrupt
};

void
block_irq(int irq, int cpu)
{
	int intr_mask;
        unsigned long flags;

        spin_lock_irqsave(&irq_lock, flags);
        intr_mask = REG_RD_INT(intr_vect, irq_regs[cpu], rw_mask);

	/* Remember; 1 let thru, 0 block. */
	intr_mask &= ~(1 << (irq - FIRST_IRQ));

	REG_WR_INT(intr_vect, irq_regs[cpu], rw_mask, intr_mask);
        spin_unlock_irqrestore(&irq_lock, flags);
}

void
unblock_irq(int irq, int cpu)
{
	int intr_mask;
        unsigned long flags;

        spin_lock_irqsave(&irq_lock, flags);
        intr_mask = REG_RD_INT(intr_vect, irq_regs[cpu], rw_mask);

	/* Remember; 1 let thru, 0 block. */
	intr_mask |= (1 << (irq - FIRST_IRQ));

	REG_WR_INT(intr_vect, irq_regs[cpu], rw_mask, intr_mask);
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
	if (cpu_isset(cpu, irq_allocations[irq - FIRST_IRQ].mask))
		goto out;

	/* IRQ must be moved to another CPU. */
	cpu = first_cpu(irq_allocations[irq - FIRST_IRQ].mask);
	irq_allocations[irq - FIRST_IRQ].cpu = cpu;
out:
	spin_unlock_irqrestore(&irq_lock, flags);
	return cpu;
}

void
mask_irq(int irq)
{
	int cpu;

	for (cpu = 0; cpu < NR_CPUS; cpu++)
		block_irq(irq, cpu);
}

void
unmask_irq(int irq)
{
	unblock_irq(irq, irq_cpu(irq));
}


static unsigned int startup_crisv32_irq(unsigned int irq)
{
	unmask_irq(irq);
	return 0;
}

static void shutdown_crisv32_irq(unsigned int irq)
{
	mask_irq(irq);
}

static void enable_crisv32_irq(unsigned int irq)
{
	unmask_irq(irq);
}

static void disable_crisv32_irq(unsigned int irq)
{
	mask_irq(irq);
}

static void ack_crisv32_irq(unsigned int irq)
{
}

static void end_crisv32_irq(unsigned int irq)
{
}

void set_affinity_crisv32_irq(unsigned int irq, cpumask_t dest)
{
	unsigned long flags;
	spin_lock_irqsave(&irq_lock, flags);
	irq_allocations[irq - FIRST_IRQ].mask = dest;
	spin_unlock_irqrestore(&irq_lock, flags);
}

static struct hw_interrupt_type crisv32_irq_type = {
	.typename =    "CRISv32",
	.startup =     startup_crisv32_irq,
	.shutdown =    shutdown_crisv32_irq,
	.enable =      enable_crisv32_irq,
	.disable =     disable_crisv32_irq,
	.ack =         ack_crisv32_irq,
	.end =         end_crisv32_irq,
	.set_affinity = set_affinity_crisv32_irq
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
	/* Interrupts that may not be moved to another CPU and
         * are IRQF_DISABLED may skip blocking. This is currently
         * only valid for the timer IRQ and the IPI and is used
         * for the timer interrupt to avoid watchdog starvation.
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
	int masked;
	int bit;

	cpu = smp_processor_id();

	/* An extra irq_enter here to prevent softIRQs to run after
         * each do_IRQ. This will decrease the interrupt latency.
	 */
	irq_enter();

	/* Get which IRQs that happend. */
	masked = REG_RD_INT(intr_vect, irq_regs[cpu], r_masked_vect);

	/* Calculate new IRQ mask with these IRQs disabled. */
	mask = REG_RD_INT(intr_vect, irq_regs[cpu], rw_mask);
	mask &= ~masked;

	/* Timer IRQ is never masked */
	if (masked & TIMER_MASK)
		mask |= TIMER_MASK;

	/* Block all the IRQs */
	REG_WR_INT(intr_vect, irq_regs[cpu], rw_mask, mask);

	/* Check for timer IRQ and handle it special. */
	if (masked & TIMER_MASK) {
	        masked &= ~TIMER_MASK;
		do_IRQ(TIMER_INTR_VECT, regs);
	}

#ifdef IGNORE_MASK
	/* Remove IRQs that can't be handled as multiple. */
	masked &= ~IGNORE_MASK;
#endif

	/* Handle the rest of the IRQs. */
	for (bit = 0; bit < 32; bit++)
	{
		if (masked & (1 << bit))
			do_IRQ(bit + FIRST_IRQ, regs);
	}

	/* Unblock all the IRQs. */
	mask = REG_RD_INT(intr_vect, irq_regs[cpu], rw_mask);
	mask |= masked;
	REG_WR_INT(intr_vect, irq_regs[cpu], rw_mask, mask);

	/* This irq_exit() will trigger the soft IRQs. */
	irq_exit();
}

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

	/* Clear all interrupts masks. */
	REG_WR(intr_vect, regi_irq, rw_mask, vect_mask);

	for (i = 0; i < 256; i++)
		etrax_irv->v[i] = weird_irq;

	/* Point all IRQ's to bad handlers. */
	for (i = FIRST_IRQ, j = 0; j < NR_IRQS; i++, j++) {
		irq_desc[j].chip = &crisv32_irq_type;
		set_exception_vector(i, interrupt[j]);
	}

        /* Mark Timer and IPI IRQs as CPU local */
	irq_allocations[TIMER_INTR_VECT - FIRST_IRQ].cpu = CPU_FIXED;
	irq_desc[TIMER_INTR_VECT].status |= IRQ_PER_CPU;
	irq_allocations[IPI_INTR_VECT - FIRST_IRQ].cpu = CPU_FIXED;
	irq_desc[IPI_INTR_VECT].status |= IRQ_PER_CPU;

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

