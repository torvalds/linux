/*
 * Interrupt handling for GE FPGA based PIC
 *
 * Author: Martyn Welch <martyn.welch@ge.com>
 *
 * 2008 (c) GE Intelligent Platforms Embedded Systems, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/irq.h>

#include "gef_pic.h"

#define DEBUG
#undef DEBUG

#ifdef DEBUG
#define DBG(fmt...) do { printk(KERN_DEBUG "gef_pic: " fmt); } while (0)
#else
#define DBG(fmt...) do { } while (0)
#endif

#define GEF_PIC_NUM_IRQS	32

/* Interrupt Controller Interface Registers */
#define GEF_PIC_INTR_STATUS	0x0000

#define GEF_PIC_INTR_MASK(cpu)	(0x0010 + (0x4 * cpu))
#define GEF_PIC_CPU0_INTR_MASK	GEF_PIC_INTR_MASK(0)
#define GEF_PIC_CPU1_INTR_MASK	GEF_PIC_INTR_MASK(1)

#define GEF_PIC_MCP_MASK(cpu)	(0x0018 + (0x4 * cpu))
#define GEF_PIC_CPU0_MCP_MASK	GEF_PIC_MCP_MASK(0)
#define GEF_PIC_CPU1_MCP_MASK	GEF_PIC_MCP_MASK(1)

#define gef_irq_to_hw(virq)    ((unsigned int)irq_map[virq].hwirq)


static DEFINE_RAW_SPINLOCK(gef_pic_lock);

static void __iomem *gef_pic_irq_reg_base;
static struct irq_host *gef_pic_irq_host;
static int gef_pic_cascade_irq;

/*
 * Interrupt Controller Handling
 *
 * The interrupt controller handles interrupts for most on board interrupts,
 * apart from PCI interrupts. For example on SBC610:
 *
 * 17:31 RO Reserved
 * 16    RO PCI Express Doorbell 3 Status
 * 15    RO PCI Express Doorbell 2 Status
 * 14    RO PCI Express Doorbell 1 Status
 * 13    RO PCI Express Doorbell 0 Status
 * 12    RO Real Time Clock Interrupt Status
 * 11    RO Temperature Interrupt Status
 * 10    RO Temperature Critical Interrupt Status
 * 9     RO Ethernet PHY1 Interrupt Status
 * 8     RO Ethernet PHY3 Interrupt Status
 * 7     RO PEX8548 Interrupt Status
 * 6     RO Reserved
 * 5     RO Watchdog 0 Interrupt Status
 * 4     RO Watchdog 1 Interrupt Status
 * 3     RO AXIS Message FIFO A Interrupt Status
 * 2     RO AXIS Message FIFO B Interrupt Status
 * 1     RO AXIS Message FIFO C Interrupt Status
 * 0     RO AXIS Message FIFO D Interrupt Status
 *
 * Interrupts can be forwarded to one of two output lines. Nothing
 * clever is done, so if the masks are incorrectly set, a single input
 * interrupt could generate interrupts on both output lines!
 *
 * The dual lines are there to allow the chained interrupts to be easily
 * passed into two different cores. We currently do not use this functionality
 * in this driver.
 *
 * Controller can also be configured to generate Machine checks (MCP), again on
 * two lines, to be attached to two different cores. It is suggested that these
 * should be masked out.
 */

void gef_pic_cascade(unsigned int irq, struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int cascade_irq;

	/*
	 * See if we actually have an interrupt, call generic handling code if
	 * we do.
	 */
	cascade_irq = gef_pic_get_irq();

	if (cascade_irq != NO_IRQ)
		generic_handle_irq(cascade_irq);

	chip->irq_eoi(&desc->irq_data);
}

static void gef_pic_mask(struct irq_data *d)
{
	unsigned long flags;
	unsigned int hwirq;
	u32 mask;

	hwirq = gef_irq_to_hw(d->irq);

	raw_spin_lock_irqsave(&gef_pic_lock, flags);
	mask = in_be32(gef_pic_irq_reg_base + GEF_PIC_INTR_MASK(0));
	mask &= ~(1 << hwirq);
	out_be32(gef_pic_irq_reg_base + GEF_PIC_INTR_MASK(0), mask);
	raw_spin_unlock_irqrestore(&gef_pic_lock, flags);
}

static void gef_pic_mask_ack(struct irq_data *d)
{
	/* Don't think we actually have to do anything to ack an interrupt,
	 * we just need to clear down the devices interrupt and it will go away
	 */
	gef_pic_mask(d);
}

static void gef_pic_unmask(struct irq_data *d)
{
	unsigned long flags;
	unsigned int hwirq;
	u32 mask;

	hwirq = gef_irq_to_hw(d->irq);

	raw_spin_lock_irqsave(&gef_pic_lock, flags);
	mask = in_be32(gef_pic_irq_reg_base + GEF_PIC_INTR_MASK(0));
	mask |= (1 << hwirq);
	out_be32(gef_pic_irq_reg_base + GEF_PIC_INTR_MASK(0), mask);
	raw_spin_unlock_irqrestore(&gef_pic_lock, flags);
}

static struct irq_chip gef_pic_chip = {
	.name		= "gefp",
	.irq_mask	= gef_pic_mask,
	.irq_mask_ack	= gef_pic_mask_ack,
	.irq_unmask	= gef_pic_unmask,
};


/* When an interrupt is being configured, this call allows some flexibilty
 * in deciding which irq_chip structure is used
 */
static int gef_pic_host_map(struct irq_host *h, unsigned int virq,
			  irq_hw_number_t hwirq)
{
	/* All interrupts are LEVEL sensitive */
	irq_set_status_flags(virq, IRQ_LEVEL);
	irq_set_chip_and_handler(virq, &gef_pic_chip, handle_level_irq);

	return 0;
}

static int gef_pic_host_xlate(struct irq_host *h, struct device_node *ct,
			    const u32 *intspec, unsigned int intsize,
			    irq_hw_number_t *out_hwirq, unsigned int *out_flags)
{

	*out_hwirq = intspec[0];
	if (intsize > 1)
		*out_flags = intspec[1];
	else
		*out_flags = IRQ_TYPE_LEVEL_HIGH;

	return 0;
}

static struct irq_host_ops gef_pic_host_ops = {
	.map	= gef_pic_host_map,
	.xlate	= gef_pic_host_xlate,
};


/*
 * Initialisation of PIC, this should be called in BSP
 */
void __init gef_pic_init(struct device_node *np)
{
	unsigned long flags;

	/* Map the devices registers into memory */
	gef_pic_irq_reg_base = of_iomap(np, 0);

	raw_spin_lock_irqsave(&gef_pic_lock, flags);

	/* Initialise everything as masked. */
	out_be32(gef_pic_irq_reg_base + GEF_PIC_CPU0_INTR_MASK, 0);
	out_be32(gef_pic_irq_reg_base + GEF_PIC_CPU1_INTR_MASK, 0);

	out_be32(gef_pic_irq_reg_base + GEF_PIC_CPU0_MCP_MASK, 0);
	out_be32(gef_pic_irq_reg_base + GEF_PIC_CPU1_MCP_MASK, 0);

	raw_spin_unlock_irqrestore(&gef_pic_lock, flags);

	/* Map controller */
	gef_pic_cascade_irq = irq_of_parse_and_map(np, 0);
	if (gef_pic_cascade_irq == NO_IRQ) {
		printk(KERN_ERR "SBC610: failed to map cascade interrupt");
		return;
	}

	/* Setup an irq_host structure */
	gef_pic_irq_host = irq_alloc_host(np, IRQ_HOST_MAP_LINEAR,
					  GEF_PIC_NUM_IRQS,
					  &gef_pic_host_ops, NO_IRQ);
	if (gef_pic_irq_host == NULL)
		return;

	/* Chain with parent controller */
	irq_set_chained_handler(gef_pic_cascade_irq, gef_pic_cascade);
}

/*
 * This is called when we receive an interrupt with apparently comes from this
 * chip - check, returning the highest interrupt generated or return NO_IRQ
 */
unsigned int gef_pic_get_irq(void)
{
	u32 cause, mask, active;
	unsigned int virq = NO_IRQ;
	int hwirq;

	cause = in_be32(gef_pic_irq_reg_base + GEF_PIC_INTR_STATUS);

	mask = in_be32(gef_pic_irq_reg_base + GEF_PIC_INTR_MASK(0));

	active = cause & mask;

	if (active) {
		for (hwirq = GEF_PIC_NUM_IRQS - 1; hwirq > -1; hwirq--) {
			if (active & (0x1 << hwirq))
				break;
		}
		virq = irq_linear_revmap(gef_pic_irq_host,
			(irq_hw_number_t)hwirq);
	}

	return virq;
}

