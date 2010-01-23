/*
 *  Copyright (C) 2008 Ilya Yanok, Emcraft Systems
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/irq.h>
#include <linux/of_platform.h>
#include <linux/io.h>

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

#define socrates_fpga_irq_to_hw(virq)    ((unsigned int)irq_map[virq].hwirq)

static DEFINE_SPINLOCK(socrates_fpga_pic_lock);

static void __iomem *socrates_fpga_pic_iobase;
static struct irq_host *socrates_fpga_pic_irq_host;
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
		return NO_IRQ;

	spin_lock_irqsave(&socrates_fpga_pic_lock, flags);
	cause = socrates_fpga_pic_read(FPGA_PIC_IRQMASK(i));
	spin_unlock_irqrestore(&socrates_fpga_pic_lock, flags);
	for (i = SOCRATES_FPGA_NUM_IRQS - 1; i >= 0; i--) {
		if (cause >> (i + 16))
			break;
	}
	return irq_linear_revmap(socrates_fpga_pic_irq_host,
			(irq_hw_number_t)i);
}

void socrates_fpga_pic_cascade(unsigned int irq, struct irq_desc *desc)
{
	unsigned int cascade_irq;

	/*
	 * See if we actually have an interrupt, call generic handling code if
	 * we do.
	 */
	cascade_irq = socrates_fpga_pic_get_irq(irq);

	if (cascade_irq != NO_IRQ)
		generic_handle_irq(cascade_irq);
	desc->chip->eoi(irq);

}

static void socrates_fpga_pic_ack(unsigned int virq)
{
	unsigned long flags;
	unsigned int hwirq, irq_line;
	uint32_t mask;

	hwirq = socrates_fpga_irq_to_hw(virq);

	irq_line = fpga_irqs[hwirq].irq_line;
	spin_lock_irqsave(&socrates_fpga_pic_lock, flags);
	mask = socrates_fpga_pic_read(FPGA_PIC_IRQMASK(irq_line))
		& SOCRATES_FPGA_IRQ_MASK;
	mask |= (1 << (hwirq + 16));
	socrates_fpga_pic_write(FPGA_PIC_IRQMASK(irq_line), mask);
	spin_unlock_irqrestore(&socrates_fpga_pic_lock, flags);
}

static void socrates_fpga_pic_mask(unsigned int virq)
{
	unsigned long flags;
	unsigned int hwirq;
	int irq_line;
	u32 mask;

	hwirq = socrates_fpga_irq_to_hw(virq);

	irq_line = fpga_irqs[hwirq].irq_line;
	spin_lock_irqsave(&socrates_fpga_pic_lock, flags);
	mask = socrates_fpga_pic_read(FPGA_PIC_IRQMASK(irq_line))
		& SOCRATES_FPGA_IRQ_MASK;
	mask &= ~(1 << hwirq);
	socrates_fpga_pic_write(FPGA_PIC_IRQMASK(irq_line), mask);
	spin_unlock_irqrestore(&socrates_fpga_pic_lock, flags);
}

static void socrates_fpga_pic_mask_ack(unsigned int virq)
{
	unsigned long flags;
	unsigned int hwirq;
	int irq_line;
	u32 mask;

	hwirq = socrates_fpga_irq_to_hw(virq);

	irq_line = fpga_irqs[hwirq].irq_line;
	spin_lock_irqsave(&socrates_fpga_pic_lock, flags);
	mask = socrates_fpga_pic_read(FPGA_PIC_IRQMASK(irq_line))
		& SOCRATES_FPGA_IRQ_MASK;
	mask &= ~(1 << hwirq);
	mask |= (1 << (hwirq + 16));
	socrates_fpga_pic_write(FPGA_PIC_IRQMASK(irq_line), mask);
	spin_unlock_irqrestore(&socrates_fpga_pic_lock, flags);
}

static void socrates_fpga_pic_unmask(unsigned int virq)
{
	unsigned long flags;
	unsigned int hwirq;
	int irq_line;
	u32 mask;

	hwirq = socrates_fpga_irq_to_hw(virq);

	irq_line = fpga_irqs[hwirq].irq_line;
	spin_lock_irqsave(&socrates_fpga_pic_lock, flags);
	mask = socrates_fpga_pic_read(FPGA_PIC_IRQMASK(irq_line))
		& SOCRATES_FPGA_IRQ_MASK;
	mask |= (1 << hwirq);
	socrates_fpga_pic_write(FPGA_PIC_IRQMASK(irq_line), mask);
	spin_unlock_irqrestore(&socrates_fpga_pic_lock, flags);
}

static void socrates_fpga_pic_eoi(unsigned int virq)
{
	unsigned long flags;
	unsigned int hwirq;
	int irq_line;
	u32 mask;

	hwirq = socrates_fpga_irq_to_hw(virq);

	irq_line = fpga_irqs[hwirq].irq_line;
	spin_lock_irqsave(&socrates_fpga_pic_lock, flags);
	mask = socrates_fpga_pic_read(FPGA_PIC_IRQMASK(irq_line))
		& SOCRATES_FPGA_IRQ_MASK;
	mask |= (1 << (hwirq + 16));
	socrates_fpga_pic_write(FPGA_PIC_IRQMASK(irq_line), mask);
	spin_unlock_irqrestore(&socrates_fpga_pic_lock, flags);
}

static int socrates_fpga_pic_set_type(unsigned int virq,
		unsigned int flow_type)
{
	unsigned long flags;
	unsigned int hwirq;
	int polarity;
	u32 mask;

	hwirq = socrates_fpga_irq_to_hw(virq);

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
	spin_lock_irqsave(&socrates_fpga_pic_lock, flags);
	mask = socrates_fpga_pic_read(FPGA_PIC_IRQCFG);
	if (polarity)
		mask |= (1 << hwirq);
	else
		mask &= ~(1 << hwirq);
	socrates_fpga_pic_write(FPGA_PIC_IRQCFG, mask);
	spin_unlock_irqrestore(&socrates_fpga_pic_lock, flags);
	return 0;
}

static struct irq_chip socrates_fpga_pic_chip = {
	.name		= " FPGA-PIC ",
	.ack		= socrates_fpga_pic_ack,
	.mask           = socrates_fpga_pic_mask,
	.mask_ack       = socrates_fpga_pic_mask_ack,
	.unmask         = socrates_fpga_pic_unmask,
	.eoi		= socrates_fpga_pic_eoi,
	.set_type	= socrates_fpga_pic_set_type,
};

static int socrates_fpga_pic_host_map(struct irq_host *h, unsigned int virq,
		irq_hw_number_t hwirq)
{
	/* All interrupts are LEVEL sensitive */
	irq_to_desc(virq)->status |= IRQ_LEVEL;
	set_irq_chip_and_handler(virq, &socrates_fpga_pic_chip,
			handle_fasteoi_irq);

	return 0;
}

static int socrates_fpga_pic_host_xlate(struct irq_host *h,
		struct device_node *ct,	const u32 *intspec, unsigned int intsize,
		irq_hw_number_t *out_hwirq, unsigned int *out_flags)
{
	struct socrates_fpga_irq_info *fpga_irq = &fpga_irqs[intspec[0]];

	*out_hwirq = intspec[0];
	if  (fpga_irq->type == IRQ_TYPE_NONE) {
		/* type is configurable */
		if (intspec[1] != IRQ_TYPE_LEVEL_LOW &&
		    intspec[1] != IRQ_TYPE_LEVEL_HIGH) {
			pr_warning("FPGA PIC: invalid irq type, "
				   "setting default active low\n");
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
		pr_warning("FPGA PIC: invalid irq routing\n");

	return 0;
}

static struct irq_host_ops socrates_fpga_pic_host_ops = {
	.map    = socrates_fpga_pic_host_map,
	.xlate  = socrates_fpga_pic_host_xlate,
};

void socrates_fpga_pic_init(struct device_node *pic)
{
	unsigned long flags;
	int i;

	/* Setup an irq_host structure */
	socrates_fpga_pic_irq_host = irq_alloc_host(pic, IRQ_HOST_MAP_LINEAR,
			SOCRATES_FPGA_NUM_IRQS,	&socrates_fpga_pic_host_ops,
			SOCRATES_FPGA_NUM_IRQS);
	if (socrates_fpga_pic_irq_host == NULL) {
		pr_err("FPGA PIC: Unable to allocate host\n");
		return;
	}

	for (i = 0; i < 3; i++) {
		socrates_fpga_irqs[i] = irq_of_parse_and_map(pic, i);
		if (socrates_fpga_irqs[i] == NO_IRQ) {
			pr_warning("FPGA PIC: can't get irq%d.\n", i);
			continue;
		}
		set_irq_chained_handler(socrates_fpga_irqs[i],
				socrates_fpga_pic_cascade);
	}

	socrates_fpga_pic_iobase = of_iomap(pic, 0);

	spin_lock_irqsave(&socrates_fpga_pic_lock, flags);
	socrates_fpga_pic_write(FPGA_PIC_IRQMASK(0),
			SOCRATES_FPGA_IRQ_MASK << 16);
	socrates_fpga_pic_write(FPGA_PIC_IRQMASK(1),
			SOCRATES_FPGA_IRQ_MASK << 16);
	socrates_fpga_pic_write(FPGA_PIC_IRQMASK(2),
			SOCRATES_FPGA_IRQ_MASK << 16);
	spin_unlock_irqrestore(&socrates_fpga_pic_lock, flags);

	pr_info("FPGA PIC: Setting up Socrates FPGA PIC\n");
}
