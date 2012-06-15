#include <linux/init.h>
#include <linux/list.h>
#include <linux/io.h>

#include <asm/mach/irq.h>
#include <asm/hardware/iomd.h>
#include <asm/irq.h>
#include <asm/fiq.h>

static void iomd_ack_irq_a(struct irq_data *d)
{
	unsigned int val, mask;

	mask = 1 << d->irq;
	val = iomd_readb(IOMD_IRQMASKA);
	iomd_writeb(val & ~mask, IOMD_IRQMASKA);
	iomd_writeb(mask, IOMD_IRQCLRA);
}

static void iomd_mask_irq_a(struct irq_data *d)
{
	unsigned int val, mask;

	mask = 1 << d->irq;
	val = iomd_readb(IOMD_IRQMASKA);
	iomd_writeb(val & ~mask, IOMD_IRQMASKA);
}

static void iomd_unmask_irq_a(struct irq_data *d)
{
	unsigned int val, mask;

	mask = 1 << d->irq;
	val = iomd_readb(IOMD_IRQMASKA);
	iomd_writeb(val | mask, IOMD_IRQMASKA);
}

static struct irq_chip iomd_a_chip = {
	.irq_ack	= iomd_ack_irq_a,
	.irq_mask	= iomd_mask_irq_a,
	.irq_unmask	= iomd_unmask_irq_a,
};

static void iomd_mask_irq_b(struct irq_data *d)
{
	unsigned int val, mask;

	mask = 1 << (d->irq & 7);
	val = iomd_readb(IOMD_IRQMASKB);
	iomd_writeb(val & ~mask, IOMD_IRQMASKB);
}

static void iomd_unmask_irq_b(struct irq_data *d)
{
	unsigned int val, mask;

	mask = 1 << (d->irq & 7);
	val = iomd_readb(IOMD_IRQMASKB);
	iomd_writeb(val | mask, IOMD_IRQMASKB);
}

static struct irq_chip iomd_b_chip = {
	.irq_ack	= iomd_mask_irq_b,
	.irq_mask	= iomd_mask_irq_b,
	.irq_unmask	= iomd_unmask_irq_b,
};

static void iomd_mask_irq_dma(struct irq_data *d)
{
	unsigned int val, mask;

	mask = 1 << (d->irq & 7);
	val = iomd_readb(IOMD_DMAMASK);
	iomd_writeb(val & ~mask, IOMD_DMAMASK);
}

static void iomd_unmask_irq_dma(struct irq_data *d)
{
	unsigned int val, mask;

	mask = 1 << (d->irq & 7);
	val = iomd_readb(IOMD_DMAMASK);
	iomd_writeb(val | mask, IOMD_DMAMASK);
}

static struct irq_chip iomd_dma_chip = {
	.irq_ack	= iomd_mask_irq_dma,
	.irq_mask	= iomd_mask_irq_dma,
	.irq_unmask	= iomd_unmask_irq_dma,
};

static void iomd_mask_irq_fiq(struct irq_data *d)
{
	unsigned int val, mask;

	mask = 1 << (d->irq & 7);
	val = iomd_readb(IOMD_FIQMASK);
	iomd_writeb(val & ~mask, IOMD_FIQMASK);
}

static void iomd_unmask_irq_fiq(struct irq_data *d)
{
	unsigned int val, mask;

	mask = 1 << (d->irq & 7);
	val = iomd_readb(IOMD_FIQMASK);
	iomd_writeb(val | mask, IOMD_FIQMASK);
}

static struct irq_chip iomd_fiq_chip = {
	.irq_ack	= iomd_mask_irq_fiq,
	.irq_mask	= iomd_mask_irq_fiq,
	.irq_unmask	= iomd_unmask_irq_fiq,
};

extern unsigned char rpc_default_fiq_start, rpc_default_fiq_end;

void __init rpc_init_irq(void)
{
	unsigned int irq, flags;

	iomd_writeb(0, IOMD_IRQMASKA);
	iomd_writeb(0, IOMD_IRQMASKB);
	iomd_writeb(0, IOMD_FIQMASK);
	iomd_writeb(0, IOMD_DMAMASK);

	set_fiq_handler(&rpc_default_fiq_start,
		&rpc_default_fiq_end - &rpc_default_fiq_start);

	for (irq = 0; irq < NR_IRQS; irq++) {
		flags = IRQF_VALID;

		if (irq <= 6 || (irq >= 9 && irq <= 15))
			flags |= IRQF_PROBE;

		if (irq == 21 || (irq >= 16 && irq <= 19) ||
		    irq == IRQ_KEYBOARDTX)
			flags |= IRQF_NOAUTOEN;

		switch (irq) {
		case 0 ... 7:
			irq_set_chip_and_handler(irq, &iomd_a_chip,
						 handle_level_irq);
			set_irq_flags(irq, flags);
			break;

		case 8 ... 15:
			irq_set_chip_and_handler(irq, &iomd_b_chip,
						 handle_level_irq);
			set_irq_flags(irq, flags);
			break;

		case 16 ... 21:
			irq_set_chip_and_handler(irq, &iomd_dma_chip,
						 handle_level_irq);
			set_irq_flags(irq, flags);
			break;

		case 64 ... 71:
			irq_set_chip(irq, &iomd_fiq_chip);
			set_irq_flags(irq, IRQF_VALID);
			break;
		}
	}

	init_FIQ();
}

