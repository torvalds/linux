// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2008 Ilya Yanok, Emcraft Systems
 */

#include <linux/irq.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/io.h>

#include "socrates_fpga_pic.h"

/*
 * The FPGA supports 9 interrupt sources, which can be routed to 3
 * interrupt request lines of the MPIC. The line to be used can be
 * specified through the third cell of FDT property  "interrupts".
 */

#define SOCRATES_FPGA_NUM_IRQS	9

#define FPGA_PIC_IRQCFG		(0x0)
#define FPGA_PIC_IRQMASK(n)	(0x4 + 0x4 * (n))

#define SOCRATES_FPGA_IRQ_MASK	((1 << SOCRATES_FPGA_NUM_IRQS) - 1)

struct socrates_fpga_irq_info {
	unsigned int irq_line;
	int type;
};

/*
 * Interrupt routing and type table
 *
 * IRQ_TYPE_NONE means the interrupt type is configurable,
 * otherwise it's fixed to the specified value.
 */
static struct socrates_fpga_irq_info fpga_irqs[SOCRATES_FPGA_NUM_IRQS] = {
	[0] = {0, IRQ_TYPE_NONE},
	[1] = {0, IRQ_TYPE_LEVEL_HIGH},
	[2] = {0, IRQ_TYPE_LEVEL_LOW},
	[3] = {0, IRQ_TYPE_NONE},
	[4] = {0, IRQ_TYPE_NONE},
	[5] = {0, IRQ_TYPE_NONE},
	[6] = {0, IRQ_TYPE_NONE},
	[7] = {0, IRQ_TYPE_NONE},
	[8] = {0, IRQ_TYPE_LEVEL_HIGH},
};

static DEFINE_RAW_SPINLOCK(socrates_fpga_pic_lock);

static void __iomem *socrates_fpga_pic_iobase;
static struct irq_domain *socrates_fpga_pic_irq_host;
static unsigned int socrates_fpga_irqs[3];

static inline uint32_t socrates_fpga_pic_read(int reg)
{
	return in_be32(socrates_fpga_pic_iobase + reg);
}

static inline void socrates_fpga_pic_write(int reg, uint32_t val)
{
	out_be32(socrates_fpga_pic_iobase + reg, val);
}

static inline unsigned int socrates_fpga_pic_get_irq(unsigned int irq)
{
	uint32_t cause;
	unsigned long flags;
	int i;

	/* Check irq line routed to the MPIC */
	for (i = 0; i < 3; i++) {
		if (irq == socrates_fpga_irqs[i])
			break;
	}
	if (i == 3)
		return 0;

	raw_spin_lock_irqsave(&socrates_fpga_pic_lock, flags);
	cause = socrates_fpga_pic_read(FPGA_PIC_IRQMASK(i));
	raw_spin_unlock_irqrestore(&socrates_fpga_pic_lock, flags);
	for (i = SOCRATES_FPGA_NUM_IRQS - 1; i >= 0; i--) {
		if (cause >> (i + 16))
			break;
	}
	return irq_linear_revmap(socrates_fpga_pic_irq_host,
			(irq_hw_number_t)i);
}

static void socrates_fpga_pic_cascade(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int irq = irq_desc_get_irq(desc);
	unsigned int cascade_irq;

	/*
	 * See if we actually have an interrupt, call generic handling code if
	 * we do.
	 */
	cascade_irq = socrates_fpga_pic_get_irq(irq);

	if (cascade_irq)
		generic_handle_irq(cascade_irq);
	chip->irq_eoi(&desc->irq_data);
}

static void socrates_fpga_pic_ack(struct irq_data *d)
{
	unsigned long flags;
	unsigned int irq_line, hwirq = irqd_to_hwirq(d);
	uint32_t mask;

	irq_line = fpga_irqs[hwirq].irq_line;
	raw_spin_lock_irqsave(&socrates_fpga_pic_lock, flags);
	mask = socrates_fpga_pic_read(FPGA_PIC_IRQMASK(irq_line))
		& SOCRATES_FPGA_IRQ_MASK;
	mask |= (1 << (hwirq + 16));
	socrates_fpga_pic_write(FPGA_PIC_IRQMASK(irq_line), mask);
	raw_spin_unlock_irqrestore(&socrates_fpga_pic_lock, flags);
}

static void socrates_fpga_pic_mask(struct irq_data *d)
{
	unsigned long flags;
	unsigned int hwirq = irqd_to_hwirq(d);
	int irq_line;
	u32 mask;

	irq_line = fpga_irqs[hwirq].irq_line;
	raw_spin_lock_irqsave(&socrates_fpga_pic_lock, flags);
	mask = socrates_fpga_pic_read(FPGA_PIC_IRQMASK(irq_line))
		& SOCRATES_FPGA_IRQ_MASK;
	mask &= ~(1 << hwirq);
	socrates_fpga_pic_write(FPGA_PIC_IRQMASK(irq_line), mask);
	raw_spin_unlock_irqrestore(&socrates_fpga_pic_lock, flags);
}

static void socrates_fpga_pic_mask_ack(struct irq_data *d)
{
	unsigned long flags;
	unsigned int hwirq = irqd_to_hwirq(d);
	int irq_line;
	u32 mask;

	irq_line = fpga_irqs[hwirq].irq_line;
	raw_spin_lock_irqsave(&socrates_fpga_pic_lock, flags);
	mask = socrates_fpga_pic_read(FPGA_PIC_IRQMASK(irq_line))
		& SOCRATES_FPGA_IRQ_MASK;
	mask &= ~(1 << hwirq);
	mask |= (1 << (hwirq + 16));
	socrates_fpga_pic_write(FPGA_PIC_IRQMASK(irq_line), mask);
	raw_spin_unlock_irqrestore(&socrates_fpga_pic_lock, flags);
}

static void socrates_fpga_pic_unmask(struct irq_data *d)
{
	unsigned long flags;
	unsigned int hwirq = irqd_to_hwirq(d);
	int irq_line;
	u32 mask;

	irq_line = fpga_irqs[hwirq].irq_line;
	raw_spin_lock_irqsave(&socrates_fpga_pic_lock, flags);
	mask = socrates_fpga_pic_read(FPGA_PIC_IRQMASK(irq_line))
		& SOCRATES_FPGA_IRQ_MASK;
	mask |= (1 << hwirq);
	socrates_fpga_pic_write(FPGA_PIC_IRQMASK(irq_line), mask);
	raw_spin_unlock_irqrestore(&socrates_fpga_pic_lock, flags);
}

static void socrates_fpga_pic_eoi(struct irq_data *d)
{
	unsigned long flags;
	unsigned int hwirq = irqd_to_hwirq(d);
	int irq_line;
	u32 mask;

	irq_line = fpga_irqs[hwirq].irq_line;
	raw_spin_lock_irqsave(&socrates_fpga_pic_lock, flags);
	mask = socrates_fpga_pic_read(FPGA_PIC_IRQMASK(irq_line))
		& SOCRATES_FPGA_IRQ_MASK;
	mask |= (1 << (hwirq + 16));
	socrates_fpga_pic_write(FPGA_PIC_IRQMASK(irq_line), mask);
	raw_spin_unlock_irqrestore(&socrates_fpga_pic_lock, flags);
}

static int socrates_fpga_pic_set_type(struct irq_data *d,
		unsigned int flow_type)
{
	unsigned long flags;
	unsigned int hwirq = irqd_to_hwirq(d);
	int polarity;
	u32 mask;

	if (fpga_irqs[hwirq].type != IRQ_TYPE_NONE)
		return -EINVAL;

	switch (flow_type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_LEVEL_HIGH:
		polarity = 1;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		polarity = 0;
		break;
	default:
		return -EINVAL;
	}
	raw_spin_lock_irqsave(&socrates_fpga_pic_lock, flags);
	mask = socrates_fpga_pic_read(FPGA_PIC_IRQCFG);
	if (polarity)
		mask |= (1 << hwirq);
	else
		mask &= ~(1 << hwirq);
	socrates_fpga_pic_write(FPGA_PIC_IRQCFG, mask);
	raw_spin_unlock_irqrestore(&socrates_fpga_pic_lock, flags);
	return 0;
}

static struct irq_chip socrates_fpga_pic_chip = {
	.name		= "FPGA-PIC",
	.irq_ack	= socrates_fpga_pic_ack,
	.irq_mask	= socrates_fpga_pic_mask,
	.irq_mask_ack	= socrates_fpga_pic_mask_ack,
	.irq_unmask	= socrates_fpga_pic_unmask,
	.irq_eoi	= socrates_fpga_pic_eoi,
	.irq_set_type	= socrates_fpga_pic_set_type,
};

static int socrates_fpga_pic_host_map(struct irq_domain *h, unsigned int virq,
		irq_hw_number_t hwirq)
{
	/* All interrupts are LEVEL sensitive */
	irq_set_status_flags(virq, IRQ_LEVEL);
	irq_set_chip_and_handler(virq, &socrates_fpga_pic_chip,
				 handle_fasteoi_irq);

	return 0;
}

static int socrates_fpga_pic_host_xlate(struct irq_domain *h,
		struct device_node *ct,	const u32 *intspec, unsigned int intsize,
		irq_hw_number_t *out_hwirq, unsigned int *out_flags)
{
	struct socrates_fpga_irq_info *fpga_irq = &fpga_irqs[intspec[0]];

	*out_hwirq = intspec[0];
	if  (fpga_irq->type == IRQ_TYPE_NONE) {
		/* type is configurable */
		if (intspec[1] != IRQ_TYPE_LEVEL_LOW &&
		    intspec[1] != IRQ_TYPE_LEVEL_HIGH) {
			pr_warn("FPGA PIC: invalid irq type, setting default active low\n");
			*out_flags = IRQ_TYPE_LEVEL_LOW;
		} else {
			*out_flags = intspec[1];
		}
	} else {
		/* type is fixed */
		*out_flags = fpga_irq->type;
	}

	/* Use specified interrupt routing */
	if (intspec[2] <= 2)
		fpga_irq->irq_line = intspec[2];
	else
		pr_warn("FPGA PIC: invalid irq routing\n");

	return 0;
}

static const struct irq_domain_ops socrates_fpga_pic_host_ops = {
	.map    = socrates_fpga_pic_host_map,
	.xlate  = socrates_fpga_pic_host_xlate,
};

void __init socrates_fpga_pic_init(struct device_node *pic)
{
	unsigned long flags;
	int i;

	/* Setup an irq_domain structure */
	socrates_fpga_pic_irq_host = irq_domain_add_linear(pic,
		    SOCRATES_FPGA_NUM_IRQS, &socrates_fpga_pic_host_ops, NULL);
	if (socrates_fpga_pic_irq_host == NULL) {
		pr_err("FPGA PIC: Unable to allocate host\n");
		return;
	}

	for (i = 0; i < 3; i++) {
		socrates_fpga_irqs[i] = irq_of_parse_and_map(pic, i);
		if (!socrates_fpga_irqs[i]) {
			pr_warn("FPGA PIC: can't get irq%d\n", i);
			continue;
		}
		irq_set_chained_handler(socrates_fpga_irqs[i],
					socrates_fpga_pic_cascade);
	}

	socrates_fpga_pic_iobase = of_iomap(pic, 0);

	raw_spin_lock_irqsave(&socrates_fpga_pic_lock, flags);
	socrates_fpga_pic_write(FPGA_PIC_IRQMASK(0),
			SOCRATES_FPGA_IRQ_MASK << 16);
	socrates_fpga_pic_write(FPGA_PIC_IRQMASK(1),
			SOCRATES_FPGA_IRQ_MASK << 16);
	socrates_fpga_pic_write(FPGA_PIC_IRQMASK(2),
			SOCRATES_FPGA_IRQ_MASK << 16);
	raw_spin_unlock_irqrestore(&socrates_fpga_pic_lock, flags);

	pr_info("FPGA PIC: Setting up Socrates FPGA PIC\n");
}
