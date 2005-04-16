/*
 * arch/m68k/bvme6000/bvmeints.c
 *
 * Copyright (C) 1997 Richard Hirst [richard@sleepie.demon.co.uk]
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

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/traps.h>

static irqreturn_t bvme6000_defhand (int irq, void *dev_id, struct pt_regs *fp);

/*
 * This should ideally be 4 elements only, for speed.
 */

static struct {
	irqreturn_t	(*handler)(int, void *, struct pt_regs *);
	unsigned long	flags;
	void		*dev_id;
	const char	*devname;
	unsigned	count;
} irq_tab[256];

/*
 * void bvme6000_init_IRQ (void)
 *
 * Parameters:	None
 *
 * Returns:	Nothing
 *
 * This function is called during kernel startup to initialize
 * the bvme6000 IRQ handling routines.
 */

void bvme6000_init_IRQ (void)
{
	int i;

	for (i = 0; i < 256; i++) {
		irq_tab[i].handler = bvme6000_defhand;
		irq_tab[i].flags = IRQ_FLG_STD;
		irq_tab[i].dev_id = NULL;
		irq_tab[i].devname = NULL;
		irq_tab[i].count = 0;
	}
}

int bvme6000_request_irq(unsigned int irq,
		irqreturn_t (*handler)(int, void *, struct pt_regs *),
                unsigned long flags, const char *devname, void *dev_id)
{
	if (irq > 255) {
		printk("%s: Incorrect IRQ %d from %s\n", __FUNCTION__, irq, devname);
		return -ENXIO;
	}
#if 0
	/* Nothing special about auto-vectored devices for the BVME6000,
	 * but treat it specially to avoid changes elsewhere.
	 */

	if (irq >= VEC_INT1 && irq <= VEC_INT7)
		return cpu_request_irq(irq - VEC_SPUR, handler, flags,
						devname, dev_id);
#endif
	if (!(irq_tab[irq].flags & IRQ_FLG_STD)) {
		if (irq_tab[irq].flags & IRQ_FLG_LOCK) {
			printk("%s: IRQ %d from %s is not replaceable\n",
			       __FUNCTION__, irq, irq_tab[irq].devname);
			return -EBUSY;
		}
		if (flags & IRQ_FLG_REPLACE) {
			printk("%s: %s can't replace IRQ %d from %s\n",
			       __FUNCTION__, devname, irq, irq_tab[irq].devname);
			return -EBUSY;
		}
	}
	irq_tab[irq].handler = handler;
	irq_tab[irq].flags   = flags;
	irq_tab[irq].dev_id  = dev_id;
	irq_tab[irq].devname = devname;
	return 0;
}

void bvme6000_free_irq(unsigned int irq, void *dev_id)
{
	if (irq > 255) {
		printk("%s: Incorrect IRQ %d\n", __FUNCTION__, irq);
		return;
	}
#if 0
	if (irq >= VEC_INT1 && irq <= VEC_INT7) {
		cpu_free_irq(irq - VEC_SPUR, dev_id);
		return;
	}
#endif
	if (irq_tab[irq].dev_id != dev_id)
		printk("%s: Removing probably wrong IRQ %d from %s\n",
		       __FUNCTION__, irq, irq_tab[irq].devname);

	irq_tab[irq].handler = bvme6000_defhand;
	irq_tab[irq].flags   = IRQ_FLG_STD;
	irq_tab[irq].dev_id  = NULL;
	irq_tab[irq].devname = NULL;
}

irqreturn_t bvme6000_process_int (unsigned long vec, struct pt_regs *fp)
{
	if (vec > 255) {
		printk ("bvme6000_process_int: Illegal vector %ld", vec);
		return IRQ_NONE;
	} else {
		irq_tab[vec].count++;
		irq_tab[vec].handler(vec, irq_tab[vec].dev_id, fp);
		return IRQ_HANDLED;
	}
}

int show_bvme6000_interrupts(struct seq_file *p, void *v)
{
	int i;

	for (i = 0; i < 256; i++) {
		if (irq_tab[i].count)
			seq_printf(p, "Vec 0x%02x: %8d  %s\n",
			    i, irq_tab[i].count,
			    irq_tab[i].devname ? irq_tab[i].devname : "free");
	}
	return 0;
}


static irqreturn_t bvme6000_defhand (int irq, void *dev_id, struct pt_regs *fp)
{
	printk ("Unknown interrupt 0x%02x\n", irq);
	return IRQ_NONE;
}

void bvme6000_enable_irq (unsigned int irq)
{
}


void bvme6000_disable_irq (unsigned int irq)
{
}

