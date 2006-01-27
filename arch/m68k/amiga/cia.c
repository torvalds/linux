/*
 *  linux/arch/m68k/amiga/cia.c - CIA support
 *
 *  Copyright (C) 1996 Roman Zippel
 *
 *  The concept of some functions bases on the original Amiga OS function
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>

#include <asm/irq.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>

struct ciabase {
	volatile struct CIA *cia;
	unsigned char icr_mask, icr_data;
	unsigned short int_mask;
	int handler_irq, cia_irq, server_irq;
	char *name;
	irq_handler_t irq_list[CIA_IRQS];
} ciaa_base = {
	.cia		= &ciaa,
	.int_mask	= IF_PORTS,
	.handler_irq	= IRQ_AMIGA_AUTO_2,
	.cia_irq	= IRQ_AMIGA_CIAA,
	.server_irq	= IRQ_AMIGA_PORTS,
	.name		= "CIAA handler"
}, ciab_base = {
	.cia		= &ciab,
	.int_mask	= IF_EXTER,
	.handler_irq	= IRQ_AMIGA_AUTO_6,
	.cia_irq	= IRQ_AMIGA_CIAB,
	.server_irq	= IRQ_AMIGA_EXTER,
	.name		= "CIAB handler"
};

/*
 *  Cause or clear CIA interrupts, return old interrupt status.
 */

unsigned char cia_set_irq(struct ciabase *base, unsigned char mask)
{
	unsigned char old;

	old = (base->icr_data |= base->cia->icr);
	if (mask & CIA_ICR_SETCLR)
		base->icr_data |= mask;
	else
		base->icr_data &= ~mask;
	if (base->icr_data & base->icr_mask)
		amiga_custom.intreq = IF_SETCLR | base->int_mask;
	return old & base->icr_mask;
}

/*
 *  Enable or disable CIA interrupts, return old interrupt mask,
 *  interrupts will only be enabled if a handler exists
 */

unsigned char cia_able_irq(struct ciabase *base, unsigned char mask)
{
	unsigned char old, tmp;
	int i;

	old = base->icr_mask;
	base->icr_data |= base->cia->icr;
	base->cia->icr = mask;
	if (mask & CIA_ICR_SETCLR)
		base->icr_mask |= mask;
	else
		base->icr_mask &= ~mask;
	base->icr_mask &= CIA_ICR_ALL;
	for (i = 0, tmp = 1; i < CIA_IRQS; i++, tmp <<= 1) {
		if ((tmp & base->icr_mask) && !base->irq_list[i].handler) {
			base->icr_mask &= ~tmp;
			base->cia->icr = tmp;
		}
	}
	if (base->icr_data & base->icr_mask)
		amiga_custom.intreq = IF_SETCLR | base->int_mask;
	return old;
}

int cia_request_irq(struct ciabase *base, unsigned int irq,
                    irqreturn_t (*handler)(int, void *, struct pt_regs *),
                    unsigned long flags, const char *devname, void *dev_id)
{
	unsigned char mask;

	base->irq_list[irq].handler = handler;
	base->irq_list[irq].flags   = flags;
	base->irq_list[irq].dev_id  = dev_id;
	base->irq_list[irq].devname = devname;

	/* enable the interrupt */
	mask = 1 << irq;
	cia_set_irq(base, mask);
	cia_able_irq(base, CIA_ICR_SETCLR | mask);
	return 0;
}

void cia_free_irq(struct ciabase *base, unsigned int irq, void *dev_id)
{
	if (base->irq_list[irq].dev_id != dev_id)
		printk("%s: removing probably wrong IRQ %i from %s\n",
		       __FUNCTION__, base->cia_irq + irq,
		       base->irq_list[irq].devname);

	base->irq_list[irq].handler = NULL;
	base->irq_list[irq].flags   = 0;

	cia_able_irq(base, 1 << irq);
}

static irqreturn_t cia_handler(int irq, void *dev_id, struct pt_regs *fp)
{
	struct ciabase *base = (struct ciabase *)dev_id;
	int mach_irq, i;
	unsigned char ints;

	mach_irq = base->cia_irq;
	irq = SYS_IRQS + mach_irq;
	ints = cia_set_irq(base, CIA_ICR_ALL);
	amiga_custom.intreq = base->int_mask;
	for (i = 0; i < CIA_IRQS; i++, irq++, mach_irq++) {
		if (ints & 1) {
			kstat_cpu(0).irqs[irq]++;
			base->irq_list[i].handler(mach_irq, base->irq_list[i].dev_id, fp);
		}
		ints >>= 1;
	}
	amiga_do_irq_list(base->server_irq, fp);
	return IRQ_HANDLED;
}

void __init cia_init_IRQ(struct ciabase *base)
{
	int i;

	/* init isr handlers */
	for (i = 0; i < CIA_IRQS; i++) {
		base->irq_list[i].handler = NULL;
		base->irq_list[i].flags   = 0;
	}

	/* clear any pending interrupt and turn off all interrupts */
	cia_set_irq(base, CIA_ICR_ALL);
	cia_able_irq(base, CIA_ICR_ALL);

	/* install CIA handler */
	request_irq(base->handler_irq, cia_handler, 0, base->name, base);

	amiga_custom.intena = IF_SETCLR | base->int_mask;
}

int cia_get_irq_list(struct ciabase *base, struct seq_file *p)
{
	int i, j;

	j = base->cia_irq;
	for (i = 0; i < CIA_IRQS; i++) {
		seq_printf(p, "cia  %2d: %10d ", j + i,
			       kstat_cpu(0).irqs[SYS_IRQS + j + i]);
		seq_puts(p, "  ");
		seq_printf(p, "%s\n", base->irq_list[i].devname);
	}
	return 0;
}
