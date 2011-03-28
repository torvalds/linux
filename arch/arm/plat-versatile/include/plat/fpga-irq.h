#ifndef PLAT_FPGA_IRQ_H
#define PLAT_FPGA_IRQ_H

struct fpga_irq_data {
	void __iomem *base;
	unsigned int irq_start;
	struct irq_chip chip;
};

void fpga_irq_init(int, u32, struct fpga_irq_data *);

#endif
