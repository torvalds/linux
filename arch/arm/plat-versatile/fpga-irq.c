/*
 *  Support for Versatile FPGA-based IRQ controllers
 */
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/exception.h>
#include <asm/mach/irq.h>
#include <plat/fpga-irq.h>

#define IRQ_STATUS		0x00
#define IRQ_RAW_STATUS		0x04
#define IRQ_ENABLE_SET		0x08
#define IRQ_ENABLE_CLEAR	0x0c
#define INT_SOFT_SET		0x10
#define INT_SOFT_CLEAR		0x14
#define FIQ_STATUS		0x20
#define FIQ_RAW_STATUS		0x24
#define FIQ_ENABLE		0x28
#define FIQ_ENABLE_SET		0x28
#define FIQ_ENABLE_CLEAR	0x2C

/**
 * struct fpga_irq_data - irq data container for the FPGA IRQ controller
 * @base: memory offset in virtual memory
 * @chip: chip container for this instance
 * @domain: IRQ domain for this instance
 * @valid: mask for valid IRQs on this controller
 * @used_irqs: number of active IRQs on this controller
 */
struct fpga_irq_data {
	void __iomem *base;
	struct irq_chip chip;
	u32 valid;
	struct irq_domain *domain;
	u8 used_irqs;
};

/* we cannot allocate memory when the controllers are initially registered */
static struct fpga_irq_data fpga_irq_devices[CONFIG_PLAT_VERSATILE_FPGA_IRQ_NR];
static int fpga_irq_id;

static void fpga_irq_mask(struct irq_data *d)
{
	struct fpga_irq_data *f = irq_data_get_irq_chip_data(d);
	u32 mask = 1 << d->hwirq;

	writel(mask, f->base + IRQ_ENABLE_CLEAR);
}

static void fpga_irq_unmask(struct irq_data *d)
{
	struct fpga_irq_data *f = irq_data_get_irq_chip_data(d);
	u32 mask = 1 << d->hwirq;

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
		generic_handle_irq(irq_find_mapping(f->domain, irq));
	} while (status);
}

/*
 * Handle each interrupt in a single FPGA IRQ controller.  Returns non-zero
 * if we've handled at least one interrupt.  This does a single read of the
 * status register and handles all interrupts in order from LSB first.
 */
static int handle_one_fpga(struct fpga_irq_data *f, struct pt_regs *regs)
{
	int handled = 0;
	int irq;
	u32 status;

	while ((status  = readl(f->base + IRQ_STATUS))) {
		irq = ffs(status) - 1;
		handle_IRQ(irq_find_mapping(f->domain, irq), regs);
		handled = 1;
	}

	return handled;
}

/*
 * Keep iterating over all registered FPGA IRQ controllers until there are
 * no pending interrupts.
 */
asmlinkage void __exception_irq_entry fpga_handle_irq(struct pt_regs *regs)
{
	int i, handled;

	do {
		for (i = 0, handled = 0; i < fpga_irq_id; ++i)
			handled |= handle_one_fpga(&fpga_irq_devices[i], regs);
	} while (handled);
}

static int fpga_irqdomain_map(struct irq_domain *d, unsigned int irq,
		irq_hw_number_t hwirq)
{
	struct fpga_irq_data *f = d->host_data;

	/* Skip invalid IRQs, only register handlers for the real ones */
	if (!(f->valid & (1 << hwirq)))
		return -ENOTSUPP;
	irq_set_chip_data(irq, f);
	irq_set_chip_and_handler(irq, &f->chip,
				handle_level_irq);
	set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	f->used_irqs++;
	return 0;
}

static struct irq_domain_ops fpga_irqdomain_ops = {
	.map = fpga_irqdomain_map,
	.xlate = irq_domain_xlate_onetwocell,
};

static __init struct fpga_irq_data *
fpga_irq_prep_struct(void __iomem *base, const char *name, u32 valid) {
	struct fpga_irq_data *f;

	if (fpga_irq_id >= ARRAY_SIZE(fpga_irq_devices)) {
		printk(KERN_ERR "%s: too few FPGA IRQ controllers, increase CONFIG_PLAT_VERSATILE_FPGA_IRQ_NR\n", __func__);
		return NULL;
	}
	f = &fpga_irq_devices[fpga_irq_id];
	f->base = base;
	f->chip.name = name;
	f->chip.irq_ack = fpga_irq_mask;
	f->chip.irq_mask = fpga_irq_mask;
	f->chip.irq_unmask = fpga_irq_unmask;
	f->valid = valid;
	fpga_irq_id++;

	return f;
}

void __init fpga_irq_init(void __iomem *base, const char *name, int irq_start,
			  int parent_irq, u32 valid, struct device_node *node)
{
	struct fpga_irq_data *f;

	f = fpga_irq_prep_struct(base, name, valid);
	if (!f)
		return;

	if (parent_irq != -1) {
		irq_set_handler_data(parent_irq, f);
		irq_set_chained_handler(parent_irq, fpga_irq_handle);
	}

	f->domain = irq_domain_add_legacy(node, fls(valid), irq_start, 0,
					  &fpga_irqdomain_ops, f);
	pr_info("FPGA IRQ chip %d \"%s\" @ %p, %u irqs\n",
		fpga_irq_id, name, base, f->used_irqs);
}

#ifdef CONFIG_OF
int __init fpga_irq_of_init(struct device_node *node,
			    struct device_node *parent)
{
	struct fpga_irq_data *f;
	void __iomem *base;
	u32 clear_mask;
	u32 valid_mask;

	if (WARN_ON(!node))
		return -ENODEV;

	base = of_iomap(node, 0);
	WARN(!base, "unable to map fpga irq registers\n");

	if (of_property_read_u32(node, "clear-mask", &clear_mask))
		clear_mask = 0;

	if (of_property_read_u32(node, "valid-mask", &valid_mask))
		valid_mask = 0;

	f = fpga_irq_prep_struct(base, node->name, valid_mask);
	if (!f)
		return -ENOMEM;

	writel(clear_mask, base + IRQ_ENABLE_CLEAR);
	writel(clear_mask, base + FIQ_ENABLE_CLEAR);

	f->domain = irq_domain_add_linear(node, fls(valid_mask), &fpga_irqdomain_ops, f);
	f->used_irqs = hweight32(valid_mask);

	pr_info("FPGA IRQ chip %d \"%s\" @ %p, %u irqs\n",
		fpga_irq_id, node->name, base, f->used_irqs);
	return 0;
}
#endif
