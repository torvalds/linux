/*
 * linux/arch/h8300/kernel/ints.c
 *
 * Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 * Based on linux/arch/$(ARCH)/platform/$(PLATFORM)/ints.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Copyright 1996 Roman Zippel
 * Copyright 1999 D. Jeff Dionne <jeff@rt-control.com>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/random.h>
#include <linux/bootmem.h>
#include <linux/hardirq.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/io.h>
#include <asm/setup.h>
#include <asm/errno.h>

/*
 * This structure has only 4 elements for speed reasons
 */
typedef struct irq_handler {
	irqreturn_t (*handler)(int, void *, struct pt_regs *);
	int         flags;
	int         count;
	void	    *dev_id;
	const char  *devname;
} irq_handler_t;

static irq_handler_t *irq_list[NR_IRQS];
static int use_kmalloc;

extern unsigned long *interrupt_redirect_table;
extern const int h8300_saved_vectors[];
extern const unsigned long h8300_trap_table[];
int h8300_enable_irq_pin(unsigned int irq);
void h8300_disable_irq_pin(unsigned int irq);

#define CPU_VECTOR ((unsigned long *)0x000000)
#define ADDR_MASK (0xffffff)

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
#endif

void __init init_IRQ(void)
{
#if defined(CONFIG_RAMKERNEL)
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
#ifdef DUMP_VECTOR
	ramvec_p = ramvec;
	for (i = 0; i < NR_IRQS; i++) {
		if ((i % 8) == 0)
			printk(KERN_DEBUG "\n%p: ",ramvec_p);
		printk(KERN_DEBUG "%p ",*ramvec_p);
		ramvec_p++;
	}
	printk(KERN_DEBUG "\n");
#endif
#endif
}

int request_irq(unsigned int irq, 
		irqreturn_t (*handler)(int, void *, struct pt_regs *),
                unsigned long flags, const char *devname, void *dev_id)
{
	irq_handler_t *irq_handle;
	if (irq < 0 || irq >= NR_IRQS) {
		printk(KERN_ERR "Incorrect IRQ %d from %s\n", irq, devname);
		return -EINVAL;
	}

	if (irq_list[irq] || (h8300_enable_irq_pin(irq) == -EBUSY))
		return -EBUSY;

	if (use_kmalloc)
		irq_handle = kmalloc(sizeof(irq_handler_t), GFP_ATOMIC);
	else {
		/* use bootmem allocater */
		irq_handle = (irq_handler_t *)alloc_bootmem(sizeof(irq_handler_t));
		irq_handle = (irq_handler_t *)((unsigned long)irq_handle | 0x80000000);
	}

	if (irq_handle == NULL)
		return -ENOMEM;

	irq_handle->handler = handler;
	irq_handle->flags   = flags;
	irq_handle->count   = 0;
	irq_handle->dev_id  = dev_id;
	irq_handle->devname = devname;
	irq_list[irq] = irq_handle;

	if (irq_handle->flags & IRQF_SAMPLE_RANDOM)
		rand_initialize_irq(irq);

	enable_irq(irq);
	return 0;
}

EXPORT_SYMBOL(request_irq);

void free_irq(unsigned int irq, void *dev_id)
{
	if (irq >= NR_IRQS)
		return;

	if (!irq_list[irq] || irq_list[irq]->dev_id != dev_id)
		printk(KERN_WARNING "Removing probably wrong IRQ %d from %s\n",
		       irq, irq_list[irq]->devname);
	disable_irq(irq);
	h8300_disable_irq_pin(irq);
	if (((unsigned long)irq_list[irq] & 0x80000000) == 0) {
		kfree(irq_list[irq]);
		irq_list[irq] = NULL;
	}
}

EXPORT_SYMBOL(free_irq);

/*
 * Do we need these probe functions on the m68k?
 */
unsigned long probe_irq_on (void)
{
	return 0;
}

EXPORT_SYMBOL(probe_irq_on);

int probe_irq_off (unsigned long irqs)
{
	return 0;
}

EXPORT_SYMBOL(probe_irq_off);

void enable_irq(unsigned int irq)
{
	if (irq >= EXT_IRQ0 && irq <= (EXT_IRQ0 + EXT_IRQS))
		IER_REGS |= 1 << (irq - EXT_IRQ0);
}

void disable_irq(unsigned int irq)
{
	if (irq >= EXT_IRQ0 && irq <= (EXT_IRQ0 + EXT_IRQS))
		IER_REGS &= ~(1 << (irq - EXT_IRQ0));
}

asmlinkage void process_int(int irq, struct pt_regs *fp)
{
	irq_enter();
	h8300_clear_isr(irq);
	if (irq >= NR_TRAPS && irq < NR_IRQS) {
		if (irq_list[irq]) {
			irq_list[irq]->handler(irq, irq_list[irq]->dev_id, fp);
			irq_list[irq]->count++;
			if (irq_list[irq]->flags & IRQF_SAMPLE_RANDOM)
				add_interrupt_randomness(irq);
		}
	} else {
		BUG();
	}
	irq_exit();
}

int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v;

	if ((i < NR_IRQS) && (irq_list[i]!=NULL)) {
		seq_printf(p, "%3d: %10u ",i,irq_list[i]->count);
		seq_printf(p, "%s\n", irq_list[i]->devname);
	}

	return 0;
}

void init_irq_proc(void)
{
}

static int __init enable_kmalloc(void)
{
	use_kmalloc = 1;
	return 0;
}
core_initcall(enable_kmalloc);
