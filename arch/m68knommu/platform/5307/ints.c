/*
 * linux/arch/m68knommu/kernel/ints.c -- General interrupt handling code
 *
 * Copyright (C) 1999-2002  Greg Ungerer (gerg@snapgear.com)
 * Copyright (C) 1998  D. Jeff Dionne <jeff@lineo.ca>,
 *                     Kenneth Albanowski <kjahds@kjahds.com>,
 * Copyright (C) 2000  Lineo Inc. (www.lineo.com) 
 *
 * Based on:
 *
 * linux/arch/m68k/kernel/ints.c -- Linux/m68k general interrupt handling code
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/config.h>
#include <linux/seq_file.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/irqnode.h>
#include <asm/traps.h>
#include <asm/page.h>
#include <asm/machdep.h>

/*
 *	This table stores the address info for each vector handler.
 */
irq_handler_t irq_list[SYS_IRQS];

#define NUM_IRQ_NODES 16
static irq_node_t nodes[NUM_IRQ_NODES];

/* The number of spurious interrupts */
volatile unsigned int num_spurious;

unsigned int local_bh_count[NR_CPUS];
unsigned int local_irq_count[NR_CPUS];

static irqreturn_t default_irq_handler(int irq, void *ptr, struct pt_regs *regs)
{
#if 1
	printk(KERN_INFO "%s(%d): default irq handler vec=%d [0x%x]\n",
		__FILE__, __LINE__, irq, irq);
#endif
	return(IRQ_HANDLED);
}

/*
 * void init_IRQ(void)
 *
 * Parameters:	None
 *
 * Returns:	Nothing
 *
 * This function should be called during kernel startup to initialize
 * the IRQ handling routines.
 */

void __init init_IRQ(void)
{
	int i;

	for (i = 0; i < SYS_IRQS; i++) {
		if (mach_default_handler)
			irq_list[i].handler = (*mach_default_handler)[i];
		else
			irq_list[i].handler = default_irq_handler;
		irq_list[i].flags   = IRQ_FLG_STD;
		irq_list[i].dev_id  = NULL;
		irq_list[i].devname = NULL;
	}

	for (i = 0; i < NUM_IRQ_NODES; i++)
		nodes[i].handler = NULL;

	if (mach_init_IRQ)
		mach_init_IRQ();
}

irq_node_t *new_irq_node(void)
{
	irq_node_t *node;
	short i;

	for (node = nodes, i = NUM_IRQ_NODES-1; i >= 0; node++, i--)
		if (!node->handler)
			return node;

	printk(KERN_INFO "new_irq_node: out of nodes\n");
	return NULL;
}

int request_irq(
	unsigned int irq,
	irqreturn_t (*handler)(int, void *, struct pt_regs *),
	unsigned long flags,
	const char *devname,
	void *dev_id)
{
	if (irq < 0 || irq >= NR_IRQS) {
		printk(KERN_WARNING "%s: Incorrect IRQ %d from %s\n", __FUNCTION__,
			irq, devname);
		return -ENXIO;
	}

	if (!(irq_list[irq].flags & IRQ_FLG_STD)) {
		if (irq_list[irq].flags & IRQ_FLG_LOCK) {
			printk(KERN_WARNING "%s: IRQ %d from %s is not replaceable\n",
			       __FUNCTION__, irq, irq_list[irq].devname);
			return -EBUSY;
		}
		if (flags & IRQ_FLG_REPLACE) {
			printk(KERN_WARNING "%s: %s can't replace IRQ %d from %s\n",
			       __FUNCTION__, devname, irq, irq_list[irq].devname);
			return -EBUSY;
		}
	}

	if (flags & IRQ_FLG_FAST) {
		extern asmlinkage void fasthandler(void);
		extern void set_evector(int vecnum, void (*handler)(void));
		set_evector(irq, fasthandler);
	}

	irq_list[irq].handler = handler;
	irq_list[irq].flags   = flags;
	irq_list[irq].dev_id  = dev_id;
	irq_list[irq].devname = devname;
	return 0;
}

EXPORT_SYMBOL(request_irq);

void free_irq(unsigned int irq, void *dev_id)
{
	if (irq >= NR_IRQS) {
		printk(KERN_WARNING "%s: Incorrect IRQ %d\n", __FUNCTION__, irq);
		return;
	}

	if (irq_list[irq].dev_id != dev_id)
		printk(KERN_WARNING "%s: Removing probably wrong IRQ %d from %s\n",
		       __FUNCTION__, irq, irq_list[irq].devname);

	if (irq_list[irq].flags & IRQ_FLG_FAST) {
		extern asmlinkage void inthandler(void);
		extern void set_evector(int vecnum, void (*handler)(void));
		set_evector(irq, inthandler);
	}

	if (mach_default_handler)
		irq_list[irq].handler = (*mach_default_handler)[irq];
	else
		irq_list[irq].handler = default_irq_handler;
	irq_list[irq].flags   = IRQ_FLG_STD;
	irq_list[irq].dev_id  = NULL;
	irq_list[irq].devname = NULL;
}

EXPORT_SYMBOL(free_irq);


int sys_request_irq(unsigned int irq, 
                    irqreturn_t (*handler)(int, void *, struct pt_regs *), 
                    unsigned long flags, const char *devname, void *dev_id)
{
	if (irq > IRQ7) {
		printk(KERN_WARNING "%s: Incorrect IRQ %d from %s\n",
		       __FUNCTION__, irq, devname);
		return -ENXIO;
	}

#if 0
	if (!(irq_list[irq].flags & IRQ_FLG_STD)) {
		if (irq_list[irq].flags & IRQ_FLG_LOCK) {
			printk(KERN_WARNING "%s: IRQ %d from %s is not replaceable\n",
			       __FUNCTION__, irq, irq_list[irq].devname);
			return -EBUSY;
		}
		if (!(flags & IRQ_FLG_REPLACE)) {
			printk(KERN_WARNING "%s: %s can't replace IRQ %d from %s\n",
			       __FUNCTION__, devname, irq, irq_list[irq].devname);
			return -EBUSY;
		}
	}
#endif

	irq_list[irq].handler = handler;
	irq_list[irq].flags   = flags;
	irq_list[irq].dev_id  = dev_id;
	irq_list[irq].devname = devname;
	return 0;
}

void sys_free_irq(unsigned int irq, void *dev_id)
{
	if (irq > IRQ7) {
		printk(KERN_WARNING "%s: Incorrect IRQ %d\n", __FUNCTION__, irq);
		return;
	}

	if (irq_list[irq].dev_id != dev_id)
		printk(KERN_WARNING "%s: Removing probably wrong IRQ %d from %s\n",
		       __FUNCTION__, irq, irq_list[irq].devname);

	irq_list[irq].handler = (*mach_default_handler)[irq];
	irq_list[irq].flags   = 0;
	irq_list[irq].dev_id  = NULL;
	irq_list[irq].devname = NULL;
}

/*
 * Do we need these probe functions on the m68k?
 *
 *  ... may be useful with ISA devices
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

asmlinkage void process_int(unsigned long vec, struct pt_regs *fp)
{
	if (vec >= VEC_INT1 && vec <= VEC_INT7) {
		vec -= VEC_SPUR;
		kstat_cpu(0).irqs[vec]++;
		irq_list[vec].handler(vec, irq_list[vec].dev_id, fp);
	} else {
		if (mach_process_int)
			mach_process_int(vec, fp);
		else
			panic("Can't process interrupt vector %ld\n", vec);
		return;
	}
}


int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v;

	if (i < NR_IRQS) {
		if (! (irq_list[i].flags & IRQ_FLG_STD)) {
			seq_printf(p, "%3d: %10u ", i,
				(i ? kstat_cpu(0).irqs[i] : num_spurious));
			if (irq_list[i].flags & IRQ_FLG_LOCK)
				seq_printf(p, "L ");
			else
				seq_printf(p, "  ");
			seq_printf(p, "%s\n", irq_list[i].devname);
		}
	}

	if (i == NR_IRQS && mach_get_irq_list)
		mach_get_irq_list(p, v);
	return 0;
}

void init_irq_proc(void)
{
	/* Insert /proc/irq driver here */
}

