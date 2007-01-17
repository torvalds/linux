#include <linux/init.h>
#include <linux/list.h>

#include <asm/mach/irq.h>
#include <asm/hardware/iomd.h>
#include <asm/irq.h>
#include <asm/io.h>

static void iomd_ack_irq_a(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << irq;
	val = iomd_readb(IOMD_IRQMASKA);
	iomd_writeb(val & ~mask, IOMD_IRQMASKA);
	iomd_writeb(mask, IOMD_IRQCLRA);
}

static void iomd_mask_irq_a(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << irq;
	val = iomd_readb(IOMD_IRQMASKA);
	iomd_writeb(val & ~mask, IOMD_IRQMASKA);
}

static void iomd_unmask_irq_a(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << irq;
	val = iomd_readb(IOMD_IRQMASKA);
	iomd_writeb(val | mask, IOMD_IRQMASKA);
}

static struct irq_chip iomd_a_chip = {
	.ack	= iomd_ack_irq_a,
	.mask	= iomd_mask_irq_a,
	.unmask = iomd_unmask_irq_a,
};

static void iomd_mask_irq_b(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_IRQMASKB);
	iomd_writeb(val & ~mask, IOMD_IRQMASKB);
}

static void iomd_unmask_irq_b(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_IRQMASKB);
	iomd_writeb(val | mask, IOMD_IRQMASKB);
}

static struct irq_chip iomd_b_chip = {
	.ack	= iomd_mask_irq_b,
	.mask	= iomd_mask_irq_b,
	.unmask = iomd_unmask_irq_b,
};

static void iomd_mask_irq_dma(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_DMAMASK);
	iomd_writeb(val & ~mask, IOMD_DMAMASK);
}

static void iomd_unmask_irq_dma(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_DMAMASK);
	iomd_writeb(val | mask, IOMD_DMAMASK);
}

static struct irq_chip iomd_dma_chip = {
	.ack	= iomd_mask_irq_dma,
	.mask	= iomd_mask_irq_dma,
	.unmask = iomd_unmask_irq_dma,
};

static void iomd_mask_irq_fiq(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_FIQMASK);
	iomd_writeb(val & ~mask, IOMD_FIQMASK);
}

static void iomd_unmask_irq_fiq(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_FIQMASK);
	iomd_writeb(val | mask, IOMD_FIQMASK);
}

static struct irq_chip iomd_fiq_chip = {
	.ack	= iomd_mask_irq_fiq,
	.mask	= iomd_mask_irq_fiq,
	.unmask = iomd_unmask_irq_fiq,
};

void __init rpc_init_irq(void)
{
	unsigned int irq, flags;

	iomd_writeb(0, IOMD_IRQMASKA);
	iomd_writeb(0, IOMD_IRQMASKB);
	iomd_writeb(0, IOMD_FIQMASK);
	iomd_writeb(0, IOMD_DMAMASK);

	for (irq = 0; irq < NR_IRQS; irq++) {
		flags = IRQF_VALID;

		if (irq <= 6 || (irq >= 9 && irq <= 15))
			flags |= IRQF_PROBE;

		if (irq == 21 || (irq >= 16 && irq <= 19) ||
		    irq == IRQ_KEYBOARDTX)
			flags |= IRQF_NOAUTOEN;

		switch (irq) {
		case 0 ... 7:
			set_irq_chip(irq, &iomd_a_chip);
			set_irq_handler(irq, handle_level_irq);
			set_irq_flags(irq, flags);
			break;

		case 8 ... 15:
			set_irq_chip(irq, &iomd_b_chip);
			set_irq_handler(irq, handle_level_irq);
			set_irq_flags(irq, flags);
			break;

		case 16 ... 21:
			set_irq_chip(irq, &iomd_dma_chip);
			set_irq_handler(irq, handle_level_irq);
			set_irq_flags(irq, flags);
			break;

		case 64 ... 71:
			set_irq_chip(irq, &iomd_fiq_chip);
			set_irq_flags(irq, IRQF_VALID);
			break;
		}
	}

	init_FIQ();
}

