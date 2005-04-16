/*
 *  linux/arch/m68k/hp300/ints.c
 *
 *  Copyright (C) 1998 Philip Blundell <philb@gnu.org>
 *
 *  This file contains the HP300-specific interrupt handling.
 *  We only use the autovector interrupts, and therefore we need to
 *  maintain lists of devices sharing each ipl.
 *  [ipl list code added by Peter Maydell <pmaydell@chiark.greenend.org.uk> 06/1998]
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <asm/machdep.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/traps.h>
#include <asm/ptrace.h>
#include <asm/errno.h>
#include "ints.h"

/* Each ipl has a linked list of interrupt service routines.
 * Service routines are added via hp300_request_irq() and removed
 * via hp300_free_irq(). The device driver should set IRQ_FLG_FAST
 * if it needs to be serviced early (eg FIFOless UARTs); this will
 * cause it to be added at the front of the queue rather than
 * the back.
 * Currently IRQ_FLG_SLOW and flags=0 are treated identically; if
 * we needed three levels of priority we could distinguish them
 * but this strikes me as mildly ugly...
 */

/* we start with no entries in any list */
static irq_node_t *hp300_irq_list[HP300_NUM_IRQS];

static spinlock_t irqlist_lock;

/* This handler receives all interrupts, dispatching them to the registered handlers */
static irqreturn_t hp300_int_handler(int irq, void *dev_id, struct pt_regs *fp)
{
        irq_node_t *t;
        /* We just give every handler on the chain an opportunity to handle
         * the interrupt, in priority order.
         */
        for(t = hp300_irq_list[irq]; t; t=t->next)
                t->handler(irq, t->dev_id, fp);
        /* We could put in some accounting routines, checks for stray interrupts,
         * etc, in here. Note that currently we can't tell whether or not
         * a handler handles the interrupt, though.
         */
	return IRQ_HANDLED;
}

static irqreturn_t hp300_badint(int irq, void *dev_id, struct pt_regs *fp)
{
	num_spurious += 1;
	return IRQ_NONE;
}

irqreturn_t (*hp300_default_handler[SYS_IRQS])(int, void *, struct pt_regs *) = {
	[0] = hp300_badint,
	[1] = hp300_int_handler,
	[2] = hp300_int_handler,
	[3] = hp300_int_handler,
	[4] = hp300_int_handler,
	[5] = hp300_int_handler,
	[6] = hp300_int_handler,
	[7] = hp300_int_handler
};

/* dev_id had better be unique to each handler because it's the only way we have
 * to distinguish handlers when removing them...
 *
 * It would be pretty easy to support IRQ_FLG_LOCK (handler is not replacable)
 * and IRQ_FLG_REPLACE (handler replaces existing one with this dev_id)
 * if we wanted to. IRQ_FLG_FAST is needed for devices where interrupt latency
 * matters (eg the dreaded FIFOless UART...)
 */
int hp300_request_irq(unsigned int irq,
                      irqreturn_t (*handler) (int, void *, struct pt_regs *),
                      unsigned long flags, const char *devname, void *dev_id)
{
        irq_node_t *t, *n = new_irq_node();

        if (!n)                                   /* oops, no free nodes */
                return -ENOMEM;

	spin_lock_irqsave(&irqlist_lock, flags);

        if (!hp300_irq_list[irq]) {
                /* no list yet */
                hp300_irq_list[irq] = n;
                n->next = NULL;
        } else if (flags & IRQ_FLG_FAST) {
                /* insert at head of list */
                n->next = hp300_irq_list[irq];
                hp300_irq_list[irq] = n;
        } else {
                /* insert at end of list */
                for(t = hp300_irq_list[irq]; t->next; t = t->next)
                        /* do nothing */;
                n->next = NULL;
                t->next = n;
        }

        /* Fill in n appropriately */
        n->handler = handler;
        n->flags = flags;
        n->dev_id = dev_id;
        n->devname = devname;
	spin_unlock_irqrestore(&irqlist_lock, flags);
	return 0;
}

void hp300_free_irq(unsigned int irq, void *dev_id)
{
        irq_node_t *t;
        unsigned long flags;

        spin_lock_irqsave(&irqlist_lock, flags);

        t = hp300_irq_list[irq];
        if (!t)                                   /* no handlers at all for that IRQ */
        {
                printk(KERN_ERR "hp300_free_irq: attempt to remove nonexistent handler for IRQ %d\n", irq);
                spin_unlock_irqrestore(&irqlist_lock, flags);
		return;
        }

        if (t->dev_id == dev_id)
        {                                         /* removing first handler on chain */
                t->flags = IRQ_FLG_STD;           /* we probably don't really need these */
                t->dev_id = NULL;
                t->devname = NULL;
                t->handler = NULL;                /* frees this irq_node_t */
                hp300_irq_list[irq] = t->next;
		spin_unlock_irqrestore(&irqlist_lock, flags);
		return;
        }

        /* OK, must be removing from middle of the chain */

        for (t = hp300_irq_list[irq]; t->next && t->next->dev_id != dev_id; t = t->next)
                /* do nothing */;
        if (!t->next)
        {
                printk(KERN_ERR "hp300_free_irq: attempt to remove nonexistent handler for IRQ %d\n", irq);
		spin_unlock_irqrestore(&irqlist_lock, flags);
		return;
        }
        /* remove the entry after t: */
        t->next->flags = IRQ_FLG_STD;
        t->next->dev_id = NULL;
	t->next->devname = NULL;
	t->next->handler = NULL;
        t->next = t->next->next;

	spin_unlock_irqrestore(&irqlist_lock, flags);
}

int show_hp300_interrupts(struct seq_file *p, void *v)
{
	return 0;
}

void __init hp300_init_IRQ(void)
{
	spin_lock_init(&irqlist_lock);
}
