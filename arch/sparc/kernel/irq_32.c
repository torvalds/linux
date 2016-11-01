/*
 * Interrupt request handling routines. On the
 * Sparc the IRQs are basically 'cast in stone'
 * and you are supposed to probe the prom's device
 * node trees to find out who's got which IRQ.
 *
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *  Copyright (C) 1995,2002 Pete A. Zaitcev (zaitcev@yahoo.com)
 *  Copyright (C) 1996 Dave Redman (djhr@tadpole.co.uk)
 *  Copyright (C) 1998-2000 Anton Blanchard (anton@samba.org)
 */

#include <linux/kernel_stat.h>
#include <linux/seq_file.h>
#include <linux/export.h>

#include <asm/cacheflush.h>
#include <asm/cpudata.h>
#include <asm/setup.h>
#include <asm/pcic.h>
#include <asm/leon.h>

#include "kernel.h"
#include "irq.h"

/* platform specific irq setup */
struct sparc_config sparc_config;

unsigned long arch_local_irq_save(void)
{
	unsigned long retval;
	unsigned long tmp;

	__asm__ __volatile__(
		"rd	%%psr, %0\n\t"
		"or	%0, %2, %1\n\t"
		"wr	%1, 0, %%psr\n\t"
		"nop; nop; nop\n"
		: "=&r" (retval), "=r" (tmp)
		: "i" (PSR_PIL)
		: "memory");

	return retval;
}
EXPORT_SYMBOL(arch_local_irq_save);

void arch_local_irq_enable(void)
{
	unsigned long tmp;

	__asm__ __volatile__(
		"rd	%%psr, %0\n\t"
		"andn	%0, %1, %0\n\t"
		"wr	%0, 0, %%psr\n\t"
		"nop; nop; nop\n"
		: "=&r" (tmp)
		: "i" (PSR_PIL)
		: "memory");
}
EXPORT_SYMBOL(arch_local_irq_enable);

void arch_local_irq_restore(unsigned long old_psr)
{
	unsigned long tmp;

	__asm__ __volatile__(
		"rd	%%psr, %0\n\t"
		"and	%2, %1, %2\n\t"
		"andn	%0, %1, %0\n\t"
		"wr	%0, %2, %%psr\n\t"
		"nop; nop; nop\n"
		: "=&r" (tmp)
		: "i" (PSR_PIL), "r" (old_psr)
		: "memory");
}
EXPORT_SYMBOL(arch_local_irq_restore);

/*
 * Dave Redman (djhr@tadpole.co.uk)
 *
 * IRQ numbers.. These are no longer restricted to 15..
 *
 * this is done to enable SBUS cards and onboard IO to be masked
 * correctly. using the interrupt level isn't good enough.
 *
 * For example:
 *   A device interrupting at sbus level6 and the Floppy both come in
 *   at IRQ11, but enabling and disabling them requires writing to
 *   different bits in the SLAVIO/SEC.
 *
 * As a result of these changes sun4m machines could now support
 * directed CPU interrupts using the existing enable/disable irq code
 * with tweaks.
 *
 * Sun4d complicates things even further.  IRQ numbers are arbitrary
 * 32-bit values in that case.  Since this is similar to sparc64,
 * we adopt a virtual IRQ numbering scheme as is done there.
 * Virutal interrupt numbers are allocated by build_irq().  So NR_IRQS
 * just becomes a limit of how many interrupt sources we can handle in
 * a single system.  Even fully loaded SS2000 machines top off at
 * about 32 interrupt sources or so, therefore a NR_IRQS value of 64
 * is more than enough.
  *
 * We keep a map of per-PIL enable interrupts.  These get wired
 * up via the irq_chip->startup() method which gets invoked by
 * the generic IRQ layer during request_irq().
 */


/* Table of allocated irqs. Unused entries has irq == 0 */
static struct irq_bucket irq_table[NR_IRQS];
/* Protect access to irq_table */
static DEFINE_SPINLOCK(irq_table_lock);

/* Map between the irq identifier used in hw to the irq_bucket. */
struct irq_bucket *irq_map[SUN4D_MAX_IRQ];
/* Protect access to irq_map */
static DEFINE_SPINLOCK(irq_map_lock);

/* Allocate a new irq from the irq_table */
unsigned int irq_alloc(unsigned int real_irq, unsigned int pil)
{
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&irq_table_lock, flags);
	for (i = 1; i < NR_IRQS; i++) {
		if (irq_table[i].real_irq == real_irq && irq_table[i].pil == pil)
			goto found;
	}

	for (i = 1; i < NR_IRQS; i++) {
		if (!irq_table[i].irq)
			break;
	}

	if (i < NR_IRQS) {
		irq_table[i].real_irq = real_irq;
		irq_table[i].irq = i;
		irq_table[i].pil = pil;
	} else {
		printk(KERN_ERR "IRQ: Out of virtual IRQs.\n");
		i = 0;
	}
found:
	spin_unlock_irqrestore(&irq_table_lock, flags);

	return i;
}

/* Based on a single pil handler_irq may need to call several
 * interrupt handlers. Use irq_map as entry to irq_table,
 * and let each entry in irq_table point to the next entry.
 */
void irq_link(unsigned int irq)
{
	struct irq_bucket *p;
	unsigned long flags;
	unsigned int pil;

	BUG_ON(irq >= NR_IRQS);

	spin_lock_irqsave(&irq_map_lock, flags);

	p = &irq_table[irq];
	pil = p->pil;
	BUG_ON(pil >= SUN4D_MAX_IRQ);
	p->next = irq_map[pil];
	irq_map[pil] = p;

	spin_unlock_irqrestore(&irq_map_lock, flags);
}

void irq_unlink(unsigned int irq)
{
	struct irq_bucket *p, **pnext;
	unsigned long flags;

	BUG_ON(irq >= NR_IRQS);

	spin_lock_irqsave(&irq_map_lock, flags);

	p = &irq_table[irq];
	BUG_ON(p->pil >= SUN4D_MAX_IRQ);
	pnext = &irq_map[p->pil];
	while (*pnext != p)
		pnext = &(*pnext)->next;
	*pnext = p->next;

	spin_unlock_irqrestore(&irq_map_lock, flags);
}


/* /proc/interrupts printing */
int arch_show_interrupts(struct seq_file *p, int prec)
{
	int j;

#ifdef CONFIG_SMP
	seq_printf(p, "RES: ");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", cpu_data(j).irq_resched_count);
	seq_printf(p, "     IPI rescheduling interrupts\n");
	seq_printf(p, "CAL: ");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", cpu_data(j).irq_call_count);
	seq_printf(p, "     IPI function call interrupts\n");
#endif
	seq_printf(p, "NMI: ");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", cpu_data(j).counter);
	seq_printf(p, "     Non-maskable interrupts\n");
	return 0;
}

void handler_irq(unsigned int pil, struct pt_regs *regs)
{
	struct pt_regs *old_regs;
	struct irq_bucket *p;

	BUG_ON(pil > 15);
	old_regs = set_irq_regs(regs);
	irq_enter();

	p = irq_map[pil];
	while (p) {
		struct irq_bucket *next = p->next;

		generic_handle_irq(p->irq);
		p = next;
	}
	irq_exit();
	set_irq_regs(old_regs);
}

#if defined(CONFIG_BLK_DEV_FD) || defined(CONFIG_BLK_DEV_FD_MODULE)
static unsigned int floppy_irq;

int sparc_floppy_request_irq(unsigned int irq, irq_handler_t irq_handler)
{
	unsigned int cpu_irq;
	int err;


	err = request_irq(irq, irq_handler, 0, "floppy", NULL);
	if (err)
		return -1;

	/* Save for later use in floppy interrupt handler */
	floppy_irq = irq;

	cpu_irq = (irq & (NR_IRQS - 1));

	/* Dork with trap table if we get this far. */
#define INSTANTIATE(table) \
	table[SP_TRAP_IRQ1+(cpu_irq-1)].inst_one = SPARC_RD_PSR_L0; \
	table[SP_TRAP_IRQ1+(cpu_irq-1)].inst_two = \
		SPARC_BRANCH((unsigned long) floppy_hardint, \
			     (unsigned long) &table[SP_TRAP_IRQ1+(cpu_irq-1)].inst_two);\
	table[SP_TRAP_IRQ1+(cpu_irq-1)].inst_three = SPARC_RD_WIM_L3; \
	table[SP_TRAP_IRQ1+(cpu_irq-1)].inst_four = SPARC_NOP;

	INSTANTIATE(sparc_ttable)

#if defined CONFIG_SMP
	if (sparc_cpu_model != sparc_leon) {
		struct tt_entry *trap_table;

		trap_table = &trapbase_cpu1;
		INSTANTIATE(trap_table)
		trap_table = &trapbase_cpu2;
		INSTANTIATE(trap_table)
		trap_table = &trapbase_cpu3;
		INSTANTIATE(trap_table)
	}
#endif
#undef INSTANTIATE
	/*
	 * XXX Correct thing whould be to flush only I- and D-cache lines
	 * which contain the handler in question. But as of time of the
	 * writing we have no CPU-neutral interface to fine-grained flushes.
	 */
	flush_cache_all();
	return 0;
}
EXPORT_SYMBOL(sparc_floppy_request_irq);

/*
 * These variables are used to access state from the assembler
 * interrupt handler, floppy_hardint, so we cannot put these in
 * the floppy driver image because that would not work in the
 * modular case.
 */
volatile unsigned char *fdc_status;
EXPORT_SYMBOL(fdc_status);

char *pdma_vaddr;
EXPORT_SYMBOL(pdma_vaddr);

unsigned long pdma_size;
EXPORT_SYMBOL(pdma_size);

volatile int doing_pdma;
EXPORT_SYMBOL(doing_pdma);

char *pdma_base;
EXPORT_SYMBOL(pdma_base);

unsigned long pdma_areasize;
EXPORT_SYMBOL(pdma_areasize);

/* Use the generic irq support to call floppy_interrupt
 * which was setup using request_irq() in sparc_floppy_request_irq().
 * We only have one floppy interrupt so we do not need to check
 * for additional handlers being wired up by irq_link()
 */
void sparc_floppy_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	struct pt_regs *old_regs;

	old_regs = set_irq_regs(regs);
	irq_enter();
	generic_handle_irq(floppy_irq);
	irq_exit();
	set_irq_regs(old_regs);
}
#endif

/* djhr
 * This could probably be made indirect too and assigned in the CPU
 * bits of the code. That would be much nicer I think and would also
 * fit in with the idea of being able to tune your kernel for your machine
 * by removing unrequired machine and device support.
 *
 */

void __init init_IRQ(void)
{
	switch (sparc_cpu_model) {
	case sun4m:
		pcic_probe();
		if (pcic_present())
			sun4m_pci_init_IRQ();
		else
			sun4m_init_IRQ();
		break;

	case sun4d:
		sun4d_init_IRQ();
		break;

	case sparc_leon:
		leon_init_IRQ();
		break;

	default:
		prom_printf("Cannot initialize IRQs on this Sun machine...");
		break;
	}
}

