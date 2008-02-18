/*
 * linux/arch/h8300/kernel/irq.c
 *
 * Copyright 2007 Yoshinori Sato <ysato@users.sourceforge.jp>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/random.h>
#include <linux/bootmem.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

#include <asm/system.h>
#include <asm/traps.h>
#include <asm/io.h>
#include <asm/setup.h>
#include <asm/errno.h>

/*#define DEBUG*/

extern unsigned long *interrupt_redirect_table;
extern const int h8300_saved_vectors[];
extern const unsigned long h8300_trap_table[];
int h8300_enable_irq_pin(unsigned int irq);
void h8300_disable_irq_pin(unsigned int irq);

#define CPU_VECTOR ((unsigned long *)0x000000)
#define ADDR_MASK (0xffffff)

static inline int is_ext_irq(unsigned int irq)
{
	return (irq >= EXT_IRQ0 && irq <= (EXT_IRQ0 + EXT_IRQS));
}

static void h8300_enable_irq(unsigned int irq)
{
	if (is_ext_irq(irq))
		IER_REGS |= 1 << (irq - EXT_IRQ0);
}

static void h8300_disable_irq(unsigned int irq)
{
	if (is_ext_irq(irq))
		IER_REGS &= ~(1 << (irq - EXT_IRQ0));
}

static void h8300_end_irq(unsigned int irq)
{
}

static unsigned int h8300_startup_irq(unsigned int irq)
{
	if (is_ext_irq(irq))
		return h8300_enable_irq_pin(irq);
	else
		return 0;
}

static void h8300_shutdown_irq(unsigned int irq)
{
	if (is_ext_irq(irq))
		h8300_disable_irq_pin(irq);
}

/*
 * h8300 interrupt controller implementation
 */
struct irq_chip h8300irq_chip = {
	.name		= "H8300-INTC",
	.startup	= h8300_startup_irq,
	.shutdown	= h8300_shutdown_irq,
	.enable		= h8300_enable_irq,
	.disable	= h8300_disable_irq,
	.ack		= NULL,
	.end		= h8300_end_irq,
};

void ack_bad_irq(unsigned int irq)
{
	printk("unexpected IRQ trap at vector %02x\n", irq);
}

#if defined(CONFIG_RAMKERNEL)
static unsigned long __init *get_vector_address(void)
{
	unsigned long *rom_vector = CPU_VECTOR;
	unsigned long base,tmp;
	int vec_no;

	base = rom_vector[EXT_IRQ0] & ADDR_MASK;

	/* check romvector format */
	for (vec_no = EXT_IRQ1; vec_no <= EXT_IRQ0+EXT_IRQS; vec_no++) {
		if ((base+(vec_no - EXT_IRQ0)*4) != (rom_vector[vec_no] & ADDR_MASK))
			return NULL;
	}

	/* ramvector base address */
	base -= EXT_IRQ0*4;

	/* writerble check */
	tmp = ~(*(volatile unsigned long *)base);
	(*(volatile unsigned long *)base) = tmp;
	if ((*(volatile unsigned long *)base) != tmp)
		return NULL;
	return (unsigned long *)base;
}

static void __init setup_vector(void)
{
	int i;
	unsigned long *ramvec,*ramvec_p;
	const unsigned long *trap_entry;
	const int *saved_vector;

	ramvec = get_vector_address();
	if (ramvec == NULL)
		panic("interrupt vector serup failed.");
	else
		printk(KERN_INFO "virtual vector at 0x%08lx\n",(unsigned long)ramvec);

	/* create redirect table */
	ramvec_p = ramvec;
	trap_entry = h8300_trap_table;
	saved_vector = h8300_saved_vectors;
	for ( i = 0; i < NR_IRQS; i++) {
		if (i == *saved_vector) {
			ramvec_p++;
			saved_vector++;
		} else {
			if ( i < NR_TRAPS ) {
				if (*trap_entry)
					*ramvec_p = VECTOR(*trap_entry);
				ramvec_p++;
				trap_entry++;
			} else
				*ramvec_p++ = REDIRECT(interrupt_entry);
		}
	}
	interrupt_redirect_table = ramvec;
#ifdef DEBUG
	ramvec_p = ramvec;
	for (i = 0; i < NR_IRQS; i++) {
		if ((i % 8) == 0)
			printk(KERN_DEBUG "\n%p: ",ramvec_p);
		printk(KERN_DEBUG "%p ",*ramvec_p);
		ramvec_p++;
	}
	printk(KERN_DEBUG "\n");
#endif
}
#else
#define setup_vector() do { } while(0)
#endif

void __init init_IRQ(void)
{
	int c;

	setup_vector();

	for (c = 0; c < NR_IRQS; c++) {
		irq_desc[c].status = IRQ_DISABLED;
		irq_desc[c].action = NULL;
		irq_desc[c].depth = 1;
		irq_desc[c].chip = &h8300irq_chip;
	}
}

asmlinkage void do_IRQ(int irq)
{
	irq_enter();
	__do_IRQ(irq);
	irq_exit();
}

#if defined(CONFIG_PROC_FS)
int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v, j;
	struct irqaction * action;
	unsigned long flags;

	if (i == 0)
		seq_puts(p, "           CPU0");

	if (i < NR_IRQS) {
		spin_lock_irqsave(&irq_desc[i].lock, flags);
		action = irq_desc[i].action;
		if (!action)
			goto unlock;
		seq_printf(p, "%3d: ",i);
		seq_printf(p, "%10u ", kstat_cpu(j).irqs[i]);
		seq_printf(p, " %14s", irq_desc[i].chip->name);
		seq_printf(p, "-%-8s", irq_desc[i].name);
		seq_printf(p, "  %s", action->name);

		for (action=action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);
		seq_putc(p, '\n');
unlock:
		spin_unlock_irqrestore(&irq_desc[i].lock, flags);
	}
	return 0;
}
#endif
