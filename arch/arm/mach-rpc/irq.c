// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/list.h>
#include <linux/io.h>

#include <asm/mach/irq.h>
#include <asm/hardware/iomd.h>
#include <asm/irq.h>
#include <asm/fiq.h>

// These are offsets from the stat register for each IRQ bank
#define STAT	0x00
#define REQ	0x04
#define CLR	0x04
#define MASK	0x08

static const u8 irq_prio_h[256] = {
	 0, 8, 9, 8,10,10,10,10,11,11,11,11,10,10,10,10,
	12, 8, 9, 8,10,10,10,10,11,11,11,11,10,10,10,10,
	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10,
	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10,
	14,14,14,14,10,10,10,10,11,11,11,11,10,10,10,10,
	14,14,14,14,10,10,10,10,11,11,11,11,10,10,10,10,
	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10,
	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10,
	15,15,15,15,10,10,10,10,11,11,11,11,10,10,10,10,
	15,15,15,15,10,10,10,10,11,11,11,11,10,10,10,10,
	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10,
	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10,
	15,15,15,15,10,10,10,10,11,11,11,11,10,10,10,10,
	15,15,15,15,10,10,10,10,11,11,11,11,10,10,10,10,
	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10,
	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10,
};

static const u8 irq_prio_d[256] = {
	 0,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16,
	20,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16,
	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16,
	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16,
	22,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16,
	22,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16,
	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16,
	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16,
	23,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16,
	23,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16,
	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16,
	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16,
	22,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16,
	22,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16,
	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16,
	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16,
};

static const u8 irq_prio_l[256] = {
	 0, 0, 1, 0, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
	 4, 0, 1, 0, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
	 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	 6, 6, 6, 6, 6, 6, 6, 6, 3, 3, 3, 3, 3, 3, 3, 3,
	 6, 6, 6, 6, 6, 6, 6, 6, 3, 3, 3, 3, 3, 3, 3, 3,
	 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
};

static int iomd_get_irq_nr(void)
{
	int irq;
	u8 reg;

	/* get highest priority first */
	reg = readb(IOC_BASE + IOMD_IRQREQB);
	irq = irq_prio_h[reg];
	if (irq)
		return irq;

	/* get DMA  */
	reg = readb(IOC_BASE + IOMD_DMAREQ);
	irq = irq_prio_d[reg];
	if (irq)
		return irq;

	/* get low priority */
	reg = readb(IOC_BASE + IOMD_IRQREQA);
	irq = irq_prio_l[reg];
	if (irq)
		return irq;
	return 0;
}

static void iomd_handle_irq(struct pt_regs *regs)
{
	int irq;

	do {
		irq = iomd_get_irq_nr();
		if (irq)
			generic_handle_irq(irq);
	} while (irq);
}

static void __iomem *iomd_get_base(struct irq_data *d)
{
	void *cd = irq_data_get_irq_chip_data(d);

	return (void __iomem *)(unsigned long)cd;
}

static void iomd_set_base_mask(unsigned int irq, void __iomem *base, u32 mask)
{
	struct irq_data *d = irq_get_irq_data(irq);

	d->mask = mask;
	irq_set_chip_data(irq, (void *)(unsigned long)base);
}

static void iomd_irq_mask_ack(struct irq_data *d)
{
	void __iomem *base = iomd_get_base(d);
	unsigned int val, mask = d->mask;

	val = readb(base + MASK);
	writeb(val & ~mask, base + MASK);
	writeb(mask, base + CLR);
}

static void iomd_irq_mask(struct irq_data *d)
{
	void __iomem *base = iomd_get_base(d);
	unsigned int val, mask = d->mask;

	val = readb(base + MASK);
	writeb(val & ~mask, base + MASK);
}

static void iomd_irq_unmask(struct irq_data *d)
{
	void __iomem *base = iomd_get_base(d);
	unsigned int val, mask = d->mask;

	val = readb(base + MASK);
	writeb(val | mask, base + MASK);
}

static struct irq_chip iomd_chip_clr = {
	.irq_mask_ack	= iomd_irq_mask_ack,
	.irq_mask	= iomd_irq_mask,
	.irq_unmask	= iomd_irq_unmask,
};

static struct irq_chip iomd_chip_noclr = {
	.irq_mask	= iomd_irq_mask,
	.irq_unmask	= iomd_irq_unmask,
};

extern unsigned char rpc_default_fiq_start, rpc_default_fiq_end;

void __init rpc_init_irq(void)
{
	unsigned int irq, clr, set;

	iomd_writeb(0, IOMD_IRQMASKA);
	iomd_writeb(0, IOMD_IRQMASKB);
	iomd_writeb(0, IOMD_FIQMASK);
	iomd_writeb(0, IOMD_DMAMASK);

	set_fiq_handler(&rpc_default_fiq_start,
		&rpc_default_fiq_end - &rpc_default_fiq_start);

	set_handle_irq(iomd_handle_irq);

	for (irq = 0; irq < NR_IRQS; irq++) {
		clr = IRQ_NOREQUEST;
		set = 0;

		if (irq <= 6 || (irq >= 9 && irq <= 15))
			clr |= IRQ_NOPROBE;

		if (irq == 21 || (irq >= 16 && irq <= 19) ||
		    irq == IRQ_KEYBOARDTX)
			set |= IRQ_NOAUTOEN;

		switch (irq) {
		case 0 ... 7:
			irq_set_chip_and_handler(irq, &iomd_chip_clr,
						 handle_level_irq);
			irq_modify_status(irq, clr, set);
			iomd_set_base_mask(irq, IOMD_BASE + IOMD_IRQSTATA,
					   BIT(irq));
			break;

		case 8 ... 15:
			irq_set_chip_and_handler(irq, &iomd_chip_noclr,
						 handle_level_irq);
			irq_modify_status(irq, clr, set);
			iomd_set_base_mask(irq, IOMD_BASE + IOMD_IRQSTATB,
					   BIT(irq - 8));
			break;

		case 16 ... 21:
			irq_set_chip_and_handler(irq, &iomd_chip_noclr,
						 handle_level_irq);
			irq_modify_status(irq, clr, set);
			iomd_set_base_mask(irq, IOMD_BASE + IOMD_DMASTAT,
					   BIT(irq - 16));
			break;

		case 64 ... 71:
			irq_set_chip(irq, &iomd_chip_noclr);
			irq_modify_status(irq, clr, set);
			iomd_set_base_mask(irq, IOMD_BASE + IOMD_FIQSTAT,
					   BIT(irq - 64));
			break;
		}
	}

	init_FIQ(FIQ_START);
}
