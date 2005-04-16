/*
 * arch/m68k/mvme16x/16xints.c
 *
 * Copyright (C) 1995 Richard Hirst [richard@sleepie.demon.co.uk]
 *
 * based on amiints.c -- Amiga Linux interrupt handling code
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/seq_file.h>

#include <asm/system.h>
#include <asm/ptrace.h>
#include <asm/irq.h>

static irqreturn_t mvme16x_defhand (int irq, void *dev_id, struct pt_regs *fp);

/*
 * This should ideally be 4 elements only, for speed.
 */

static struct {
	irqreturn_t	(*handler)(int, void *, struct pt_regs *);
	unsigned long	flags;
	void		*dev_id;
	const char	*devname;
	unsigned	count;
} irq_tab[192];

/*
 * void mvme16x_init_IRQ (void)
 *
 * Parameters:	None
 *
 * Returns:	Nothing
 *
 * This function is called during kernel startup to initialize
 * the mvme16x IRQ handling routines.  Should probably ensure
 * that the base vectors for the VMEChip2 and PCCChip2 are valid.
 */

void mvme16x_init_IRQ (void)
{
	int i;

	for (i = 0; i < 192; i++) {
		irq_tab[i].handler = mvme16x_defhand;
		irq_tab[i].flags = IRQ_FLG_STD;
		irq_tab[i].dev_id = NULL;
		irq_tab[i].devname = NULL;
		irq_tab[i].count = 0;
	}
}

int mvme16x_request_irq(unsigned int irq,
		irqreturn_t (*handler)(int, void *, struct pt_regs *),
                unsigned long flags, const char *devname, void *dev_id)
{
	if (irq < 64 || irq > 255) {
		printk("%s: Incorrect IRQ %d from %s\n", __FUNCTION__, irq, devname);
		return -ENXIO;
	}

	if (!(irq_tab[irq-64].flags & IRQ_FLG_STD)) {
		if (irq_tab[irq-64].flags & IRQ_FLG_LOCK) {
			printk("%s: IRQ %d from %s is not replaceable\n",
			       __FUNCTION__, irq, irq_tab[irq-64].devname);
			return -EBUSY;
		}
		if (flags & IRQ_FLG_REPLACE) {
			printk("%s: %s can't replace IRQ %d from %s\n",
			       __FUNCTION__, devname, irq, irq_tab[irq-64].devname);
			return -EBUSY;
		}
	}
	irq_tab[irq-64].handler = handler;
	irq_tab[irq-64].flags   = flags;
	irq_tab[irq-64].dev_id  = dev_id;
	irq_tab[irq-64].devname = devname;
	return 0;
}

void mvme16x_free_irq(unsigned int irq, void *dev_id)
{
	if (irq < 64 || irq > 255) {
		printk("%s: Incorrect IRQ %d\n", __FUNCTION__, irq);
		return;
	}

	if (irq_tab[irq-64].dev_id != dev_id)
		printk("%s: Removing probably wrong IRQ %d from %s\n",
		       __FUNCTION__, irq, irq_tab[irq-64].devname);

	irq_tab[irq-64].handler = mvme16x_defhand;
	irq_tab[irq-64].flags   = IRQ_FLG_STD;
	irq_tab[irq-64].dev_id  = NULL;
	irq_tab[irq-64].devname = NULL;
}

irqreturn_t mvme16x_process_int (unsigned long vec, struct pt_regs *fp)
{
	if (vec < 64 || vec > 255) {
		printk ("mvme16x_process_int: Illegal vector %ld", vec);
		return IRQ_NONE;
	} else {
		irq_tab[vec-64].count++;
		irq_tab[vec-64].handler(vec, irq_tab[vec-64].dev_id, fp);
		return IRQ_HANDLED;
	}
}

int show_mvme16x_interrupts (struct seq_file *p, void *v)
{
	int i;

	for (i = 0; i < 192; i++) {
		if (irq_tab[i].count)
			seq_printf(p, "Vec 0x%02x: %8d  %s\n",
			    i+64, irq_tab[i].count,
			    irq_tab[i].devname ? irq_tab[i].devname : "free");
	}
	return 0;
}


static irqreturn_t mvme16x_defhand (int irq, void *dev_id, struct pt_regs *fp)
{
	printk ("Unknown interrupt 0x%02x\n", irq);
	return IRQ_NONE;
}


void mvme16x_enable_irq (unsigned int irq)
{
}


void mvme16x_disable_irq (unsigned int irq)
{
}


