/*
 *  arch/ppc/amiga/cia.c - CIA support
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
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel_stat.h>
#include <linux/init.h>

#include <asm/irq.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>

struct ciabase {
	volatile struct CIA *cia;
	u_char icr_mask, icr_data;
	u_short int_mask;
	int handler_irq, cia_irq, server_irq;
	char *name;
} ciaa_base = {
	&ciaa, 0, 0, IF_PORTS,
	IRQ_AMIGA_AUTO_2, IRQ_AMIGA_CIAA,
	IRQ_AMIGA_PORTS,
	"CIAA handler"
}, ciab_base = {
	&ciab, 0, 0, IF_EXTER,
	IRQ_AMIGA_AUTO_6, IRQ_AMIGA_CIAB,
	IRQ_AMIGA_EXTER,
	"CIAB handler"
};

#define CIA_SET_BASE_ADJUST_IRQ(base, irq)	\
do {						\
	if (irq >= IRQ_AMIGA_CIAB) {		\
		base = &ciab_base;		\
		irq -= IRQ_AMIGA_CIAB;		\
	} else {				\
		base = &ciaa_base;		\
		irq -= IRQ_AMIGA_CIAA;		\
	}					\
} while (0)

/*
 *  Cause or clear CIA interrupts, return old interrupt status.
 */

static unsigned char cia_set_irq_private(struct ciabase *base,
					 unsigned char mask)
{
	u_char old;

	old = (base->icr_data |= base->cia->icr);
	if (mask & CIA_ICR_SETCLR)
		base->icr_data |= mask;
	else
		base->icr_data &= ~mask;
	if (base->icr_data & base->icr_mask)
		amiga_custom.intreq = IF_SETCLR | base->int_mask;
	return old & base->icr_mask;
}

unsigned char cia_set_irq(unsigned int irq, int set)
{
	struct ciabase *base;
	unsigned char mask;

	if (irq >= IRQ_AMIGA_CIAB)
		mask = (1 << (irq - IRQ_AMIGA_CIAB));
	else
		mask = (1 << (irq - IRQ_AMIGA_CIAA));
	mask |= (set) ? CIA_ICR_SETCLR : 0;

	CIA_SET_BASE_ADJUST_IRQ(base, irq);

	return cia_set_irq_private(base, mask);
}

unsigned char cia_get_irq_mask(unsigned int irq)
{
	struct ciabase *base;

	CIA_SET_BASE_ADJUST_IRQ(base, irq);

	return base->cia->icr;
}

/*
 *  Enable or disable CIA interrupts, return old interrupt mask.
 */

static unsigned char cia_able_irq_private(struct ciabase *base,
					  unsigned char mask)
{
	u_char old;

	old = base->icr_mask;
	base->icr_data |= base->cia->icr;
	base->cia->icr = mask;
	if (mask & CIA_ICR_SETCLR)
		base->icr_mask |= mask;
	else
		base->icr_mask &= ~mask;
	base->icr_mask &= CIA_ICR_ALL;

	if (base->icr_data & base->icr_mask)
		amiga_custom.intreq = IF_SETCLR | base->int_mask;
	return old;
}

unsigned char cia_able_irq(unsigned int irq, int enable)
{
	struct ciabase *base;
	unsigned char mask;

	if (irq >= IRQ_AMIGA_CIAB)
		mask = (1 << (irq - IRQ_AMIGA_CIAB));
	else
		mask = (1 << (irq - IRQ_AMIGA_CIAA));
	mask |= (enable) ? CIA_ICR_SETCLR : 0;

	CIA_SET_BASE_ADJUST_IRQ(base, irq);

	return cia_able_irq_private(base, mask);
}

static void cia_handler(int irq, void *dev_id, struct pt_regs *fp)
{
	struct ciabase *base = (struct ciabase *)dev_id;
	irq_desc_t *desc;
	struct irqaction *action;
	int i;
	unsigned char ints;

	irq = base->cia_irq;
	desc = irq_desc + irq;
	ints = cia_set_irq_private(base, CIA_ICR_ALL);
	amiga_custom.intreq = base->int_mask;
	for (i = 0; i < CIA_IRQS; i++, irq++) {
		if (ints & 1) {
			kstat_cpu(0).irqs[irq]++;
			action = desc->action;
			action->handler(irq, action->dev_id, fp);
		}
		ints >>= 1;
		desc++;
	}
	amiga_do_irq_list(base->server_irq, fp);
}

void __init cia_init_IRQ(struct ciabase *base)
{
	extern struct irqaction amiga_sys_irqaction[AUTO_IRQS];
	struct irqaction *action;

	/* clear any pending interrupt and turn off all interrupts */
	cia_set_irq_private(base, CIA_ICR_ALL);
	cia_able_irq_private(base, CIA_ICR_ALL);

	/* install CIA handler */
	action = &amiga_sys_irqaction[base->handler_irq-IRQ_AMIGA_AUTO];
	action->handler = cia_handler;
	action->dev_id = base;
	action->name = base->name;
	setup_irq(base->handler_irq, &amiga_sys_irqaction[base->handler_irq-IRQ_AMIGA_AUTO]);

	amiga_custom.intena = IF_SETCLR | base->int_mask;
}
