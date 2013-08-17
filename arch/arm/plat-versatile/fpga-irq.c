/*
 *  Support for Versatile FPGA-based IRQ controllers
 */
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/mach/irq.h>
#include <plat/fpga-irq.h>

#define IRQ_STATUS		0x00
#define IRQ_RAW_STATUS		0x04
#define IRQ_ENABLE_SET		0x08
#define IRQ_ENABLE_CLEAR	0x0c

static void fpga_irq_mask(struct irq_data *d)
{
	struct fpga_irq_data *f = irq_data_get_irq_chip_data(d);
	u32 mask = 1 << (d->irq - f->irq_start);

	writel(mask, f->base + IRQ_ENABLE_CLEAR);
}

static void fpga_irq_unmask(struct irq_data *d)
{
	struct fpga_irq_data *f = irq_data_get_irq_chip_data(d);
	u32 mask = 1 << (d->irq - f->irq_start);

	writel(mask, f->base + IRQ_ENABLE_SET);
}

static void fpga_irq_handle(unsigned int irq, struct irq_desc *desc)
{
	struct fpga_irq_data *f = irq_desc_get_handler_data(desc);
	u32 status = readl(f->base + IRQ_STATUS);

	if (status == 0) {
		do_bad_IRQ(irq, desc);
		return;
	}

	do {
		irq = ffs(status) - 1;
		status &= ~(1 << irq);

		generic_handle_irq(irq + f->irq_start);
	} while (status);
}

void __init fpga_irq_init(int parent_irq, u32 valid, struct fpga_irq_data *f)
{
	unsigned int i;

	f->chip.irq_ack = fpga_irq_mask;
	f->chip.irq_mask = fpga_irq_mask;
	f->chip.irq_unmask = fpga_irq_unmask;

	if (parent_irq != -1) {
		irq_set_handler_data(parent_irq, f);
		irq_set_chained_handler(parent_irq, fpga_irq_handle);
	}

	for (i = 0; i < 32; i++) {
		if (valid & (1 << i)) {
			unsigned int irq = f->irq_start + i;

			irq_set_chip_data(irq, f);
			irq_set_chip_and_handler(irq, &f->chip,
						 handle_level_irq);
			set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
		}
	}
}
