/*
 * Celleb/Beat Interrupt controller
 *
 * (C) Copyright 2006-2007 TOSHIBA CORPORATION
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/percpu.h>
#include <linux/types.h>

#include <asm/machdep.h>

#include "interrupt.h"
#include "beat_wrapper.h"

#define	MAX_IRQS	NR_IRQS
static DEFINE_SPINLOCK(beatic_irq_mask_lock);
static uint64_t	beatic_irq_mask_enable[(MAX_IRQS+255)/64];
static uint64_t	beatic_irq_mask_ack[(MAX_IRQS+255)/64];

static struct irq_host *beatic_host = NULL;

/*
 * In this implementation, "virq" == "IRQ plug number",
 * "(irq_hw_number_t)hwirq" == "IRQ outlet number".
 */

/* assumption: locked */
static inline void beatic_update_irq_mask(unsigned int irq_plug)
{
	int off;
	unsigned long masks[4];

	off = (irq_plug / 256) * 4;
	masks[0] = beatic_irq_mask_enable[off + 0]
	           & beatic_irq_mask_ack[off + 0];
	masks[1] = beatic_irq_mask_enable[off + 1]
	           & beatic_irq_mask_ack[off + 1];
	masks[2] = beatic_irq_mask_enable[off + 2]
	           & beatic_irq_mask_ack[off + 2];
	masks[3] = beatic_irq_mask_enable[off + 3]
	           & beatic_irq_mask_ack[off + 3];
	if (beat_set_interrupt_mask(irq_plug&~255UL,
		masks[0], masks[1], masks[2], masks[3]) != 0)
		panic("Failed to set mask IRQ!");
}

static void beatic_mask_irq(unsigned int irq_plug)
{
	unsigned long flags;

	spin_lock_irqsave(&beatic_irq_mask_lock, flags);
	beatic_irq_mask_enable[irq_plug/64] &= ~(1UL << (63 - (irq_plug%64)));
	beatic_update_irq_mask(irq_plug);
	spin_unlock_irqrestore(&beatic_irq_mask_lock, flags);
}

static void beatic_unmask_irq(unsigned int irq_plug)
{
	unsigned long flags;

	spin_lock_irqsave(&beatic_irq_mask_lock, flags);
	beatic_irq_mask_enable[irq_plug/64] |= 1UL << (63 - (irq_plug%64));
	beatic_update_irq_mask(irq_plug);
	spin_unlock_irqrestore(&beatic_irq_mask_lock, flags);
}

static void beatic_ack_irq(unsigned int irq_plug)
{
	unsigned long flags;

	spin_lock_irqsave(&beatic_irq_mask_lock, flags);
	beatic_irq_mask_ack[irq_plug/64] &= ~(1UL << (63 - (irq_plug%64)));
	beatic_update_irq_mask(irq_plug);
	spin_unlock_irqrestore(&beatic_irq_mask_lock, flags);
}

static void beatic_end_irq(unsigned int irq_plug)
{
	s64 err;
	unsigned long flags;

	if ((err = beat_downcount_of_interrupt(irq_plug)) != 0) {
		if ((err & 0xFFFFFFFF) != 0xFFFFFFF5) /* -11: wrong state */
			panic("Failed to downcount IRQ! Error = %16lx", err);

		printk(KERN_ERR "IRQ over-downcounted, plug %d\n", irq_plug);
	}
	spin_lock_irqsave(&beatic_irq_mask_lock, flags);
	beatic_irq_mask_ack[irq_plug/64] |= 1UL << (63 - (irq_plug%64));
	beatic_update_irq_mask(irq_plug);
	spin_unlock_irqrestore(&beatic_irq_mask_lock, flags);
}

static struct irq_chip beatic_pic = {
	.typename = " CELL-BEAT ",
	.unmask = beatic_unmask_irq,
	.mask = beatic_mask_irq,
	.eoi = beatic_end_irq,
};

/*
 * Dispose binding hardware IRQ number (hw) and Virtuql IRQ number (virq),
 * update flags.
 *
 * Note that the number (virq) is already assigned at upper layer.
 */
static void beatic_pic_host_unmap(struct irq_host *h, unsigned int virq)
{
	beat_destruct_irq_plug(virq);
}

/*
 * Create or update binding hardware IRQ number (hw) and Virtuql
 * IRQ number (virq). This is called only once for a given mapping.
 *
 * Note that the number (virq) is already assigned at upper layer.
 */
static int beatic_pic_host_map(struct irq_host *h, unsigned int virq,
			       irq_hw_number_t hw)
{
	struct irq_desc *desc = get_irq_desc(virq);
	int64_t	err;

	if ((err = beat_construct_and_connect_irq_plug(virq, hw)) < 0)
		return -EIO;

	desc->status |= IRQ_LEVEL;
	set_irq_chip_and_handler(virq, &beatic_pic, handle_fasteoi_irq);
	return 0;
}

/*
 * Update binding hardware IRQ number (hw) and Virtuql
 * IRQ number (virq). This is called only once for a given mapping.
 */
static void beatic_pic_host_remap(struct irq_host *h, unsigned int virq,
			       irq_hw_number_t hw)
{
	beat_construct_and_connect_irq_plug(virq, hw);
}

/*
 * Translate device-tree interrupt spec to irq_hw_number_t style (ulong),
 * to pass away to irq_create_mapping().
 *
 * Called from irq_create_of_mapping() only.
 * Note: We have only 1 entry to translate.
 */
static int beatic_pic_host_xlate(struct irq_host *h, struct device_node *ct,
				 u32 *intspec, unsigned int intsize,
				 irq_hw_number_t *out_hwirq,
				 unsigned int *out_flags)
{
	u64 *intspec2 = (u64 *)intspec;

	*out_hwirq = *intspec2;
	*out_flags |= IRQ_TYPE_LEVEL_LOW;
	return 0;
}

static int beatic_pic_host_match(struct irq_host *h, struct device_node *np)
{
	/* Match all */
	return 1;
}

static struct irq_host_ops beatic_pic_host_ops = {
	.map = beatic_pic_host_map,
	.remap = beatic_pic_host_remap,
	.unmap = beatic_pic_host_unmap,
	.xlate = beatic_pic_host_xlate,
	.match = beatic_pic_host_match,
};

/*
 * Get an IRQ number
 * Note: returns VIRQ
 */
static inline unsigned int beatic_get_irq_plug(void)
{
	int i;
	uint64_t	pending[4], ub;

	for (i = 0; i < MAX_IRQS; i += 256) {
		beat_detect_pending_interrupts(i, pending);
		__asm__ ("cntlzd %0,%1":"=r"(ub):
			"r"(pending[0] & beatic_irq_mask_enable[i/64+0]
			               & beatic_irq_mask_ack[i/64+0]));
		if (ub != 64)
			return i + ub + 0;
		__asm__ ("cntlzd %0,%1":"=r"(ub):
			"r"(pending[1] & beatic_irq_mask_enable[i/64+1]
			               & beatic_irq_mask_ack[i/64+1]));
		if (ub != 64)
			return i + ub + 64;
		__asm__ ("cntlzd %0,%1":"=r"(ub):
			"r"(pending[2] & beatic_irq_mask_enable[i/64+2]
			               & beatic_irq_mask_ack[i/64+2]));
		if (ub != 64)
			return i + ub + 128;
		__asm__ ("cntlzd %0,%1":"=r"(ub):
			"r"(pending[3] & beatic_irq_mask_enable[i/64+3]
			               & beatic_irq_mask_ack[i/64+3]));
		if (ub != 64)
			return i + ub + 192;
	}

	return NO_IRQ;
}
unsigned int beatic_get_irq(void)
{
	unsigned int ret;

	ret = beatic_get_irq_plug();
	if (ret != NO_IRQ)
		beatic_ack_irq(ret);
	return ret;
}

/*
 */
void __init beatic_init_IRQ(void)
{
	int	i;

	memset(beatic_irq_mask_enable, 0, sizeof(beatic_irq_mask_enable));
	memset(beatic_irq_mask_ack, 255, sizeof(beatic_irq_mask_ack));
	for (i = 0; i < MAX_IRQS; i += 256)
		beat_set_interrupt_mask(i, 0L, 0L, 0L, 0L);

	/* Set out get_irq function */
	ppc_md.get_irq = beatic_get_irq;

	/* Allocate an irq host */
	beatic_host = irq_alloc_host(NULL, IRQ_HOST_MAP_NOMAP, 0,
					 &beatic_pic_host_ops,
					 0);
	BUG_ON(beatic_host == NULL);
	irq_set_default_host(beatic_host);
}

#ifdef CONFIG_SMP

/* Nullified to compile with SMP mode */
void beatic_setup_cpu(int cpu)
{
}

void beatic_cause_IPI(int cpu, int mesg)
{
}

void beatic_request_IPIs(void)
{
}
#endif /* CONFIG_SMP */

void beatic_deinit_IRQ(void)
{
	int	i;

	for (i = 1; i < NR_IRQS; i++)
		beat_destruct_irq_plug(i);
}
