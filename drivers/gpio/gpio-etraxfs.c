#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/of_gpio.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/basic_mmio_gpio.h>

#define ETRAX_FS_rw_pa_dout	0
#define ETRAX_FS_r_pa_din	4
#define ETRAX_FS_rw_pa_oe	8
#define ETRAX_FS_rw_intr_cfg	12
#define ETRAX_FS_rw_intr_mask	16
#define ETRAX_FS_rw_ack_intr	20
#define ETRAX_FS_r_intr		24
#define ETRAX_FS_r_masked_intr	28
#define ETRAX_FS_rw_pb_dout	32
#define ETRAX_FS_r_pb_din	36
#define ETRAX_FS_rw_pb_oe	40
#define ETRAX_FS_rw_pc_dout	48
#define ETRAX_FS_r_pc_din	52
#define ETRAX_FS_rw_pc_oe	56
#define ETRAX_FS_rw_pd_dout	64
#define ETRAX_FS_r_pd_din	68
#define ETRAX_FS_rw_pd_oe	72
#define ETRAX_FS_rw_pe_dout	80
#define ETRAX_FS_r_pe_din	84
#define ETRAX_FS_rw_pe_oe	88

#define ARTPEC3_r_pa_din	0
#define ARTPEC3_rw_pa_dout	4
#define ARTPEC3_rw_pa_oe	8
#define ARTPEC3_r_pb_din	44
#define ARTPEC3_rw_pb_dout	48
#define ARTPEC3_rw_pb_oe	52
#define ARTPEC3_r_pc_din	88
#define ARTPEC3_rw_pc_dout	92
#define ARTPEC3_rw_pc_oe	96
#define ARTPEC3_r_pd_din	116
#define ARTPEC3_rw_intr_cfg	120
#define ARTPEC3_rw_intr_pins	124
#define ARTPEC3_rw_intr_mask	128
#define ARTPEC3_rw_ack_intr	132
#define ARTPEC3_r_masked_intr	140

#define GIO_CFG_OFF		0
#define GIO_CFG_HI		1
#define GIO_CFG_LO		2
#define GIO_CFG_SET		3
#define GIO_CFG_POSEDGE		5
#define GIO_CFG_NEGEDGE		6
#define GIO_CFG_ANYEDGE		7

struct etraxfs_gpio_info;

struct etraxfs_gpio_block {
	spinlock_t lock;
	u32 mask;
	u32 cfg;
	u32 pins;
	unsigned int group[8];

	void __iomem *regs;
	const struct etraxfs_gpio_info *info;
};

struct etraxfs_gpio_chip {
	struct bgpio_chip bgc;
	struct etraxfs_gpio_block *block;
};

struct etraxfs_gpio_port {
	const char *label;
	unsigned int oe;
	unsigned int dout;
	unsigned int din;
	unsigned int ngpio;
};

struct etraxfs_gpio_info {
	unsigned int num_ports;
	const struct etraxfs_gpio_port *ports;

	unsigned int rw_ack_intr;
	unsigned int rw_intr_mask;
	unsigned int rw_intr_cfg;
	unsigned int rw_intr_pins;
	unsigned int r_masked_intr;
};

static const struct etraxfs_gpio_port etraxfs_gpio_etraxfs_ports[] = {
	{
		.label	= "A",
		.ngpio	= 8,
		.oe	= ETRAX_FS_rw_pa_oe,
		.dout	= ETRAX_FS_rw_pa_dout,
		.din	= ETRAX_FS_r_pa_din,
	},
	{
		.label	= "B",
		.ngpio	= 18,
		.oe	= ETRAX_FS_rw_pb_oe,
		.dout	= ETRAX_FS_rw_pb_dout,
		.din	= ETRAX_FS_r_pb_din,
	},
	{
		.label	= "C",
		.ngpio	= 18,
		.oe	= ETRAX_FS_rw_pc_oe,
		.dout	= ETRAX_FS_rw_pc_dout,
		.din	= ETRAX_FS_r_pc_din,
	},
	{
		.label	= "D",
		.ngpio	= 18,
		.oe	= ETRAX_FS_rw_pd_oe,
		.dout	= ETRAX_FS_rw_pd_dout,
		.din	= ETRAX_FS_r_pd_din,
	},
	{
		.label	= "E",
		.ngpio	= 18,
		.oe	= ETRAX_FS_rw_pe_oe,
		.dout	= ETRAX_FS_rw_pe_dout,
		.din	= ETRAX_FS_r_pe_din,
	},
};

static const struct etraxfs_gpio_info etraxfs_gpio_etraxfs = {
	.num_ports = ARRAY_SIZE(etraxfs_gpio_etraxfs_ports),
	.ports = etraxfs_gpio_etraxfs_ports,
	.rw_ack_intr	= ETRAX_FS_rw_ack_intr,
	.rw_intr_mask	= ETRAX_FS_rw_intr_mask,
	.rw_intr_cfg	= ETRAX_FS_rw_intr_cfg,
	.r_masked_intr	= ETRAX_FS_r_masked_intr,
};

static const struct etraxfs_gpio_port etraxfs_gpio_artpec3_ports[] = {
	{
		.label	= "A",
		.ngpio	= 32,
		.oe	= ARTPEC3_rw_pa_oe,
		.dout	= ARTPEC3_rw_pa_dout,
		.din	= ARTPEC3_r_pa_din,
	},
	{
		.label	= "B",
		.ngpio	= 32,
		.oe	= ARTPEC3_rw_pb_oe,
		.dout	= ARTPEC3_rw_pb_dout,
		.din	= ARTPEC3_r_pb_din,
	},
	{
		.label	= "C",
		.ngpio	= 16,
		.oe	= ARTPEC3_rw_pc_oe,
		.dout	= ARTPEC3_rw_pc_dout,
		.din	= ARTPEC3_r_pc_din,
	},
	{
		.label	= "D",
		.ngpio	= 32,
		.din	= ARTPEC3_r_pd_din,
	},
};

static const struct etraxfs_gpio_info etraxfs_gpio_artpec3 = {
	.num_ports = ARRAY_SIZE(etraxfs_gpio_artpec3_ports),
	.ports = etraxfs_gpio_artpec3_ports,
	.rw_ack_intr	= ARTPEC3_rw_ack_intr,
	.rw_intr_mask	= ARTPEC3_rw_intr_mask,
	.rw_intr_cfg	= ARTPEC3_rw_intr_cfg,
	.r_masked_intr	= ARTPEC3_r_masked_intr,
	.rw_intr_pins	= ARTPEC3_rw_intr_pins,
};

static struct etraxfs_gpio_chip *to_etraxfs(struct gpio_chip *gc)
{
	return container_of(gc, struct etraxfs_gpio_chip, bgc.gc);
}

static unsigned int etraxfs_gpio_chip_to_port(struct gpio_chip *gc)
{
	return gc->label[0] - 'A';
}

static int etraxfs_gpio_of_xlate(struct gpio_chip *gc,
			       const struct of_phandle_args *gpiospec,
			       u32 *flags)
{
	/*
	 * Port numbers are A to E, and the properties are integers, so we
	 * specify them as 0xA - 0xE.
	 */
	if (etraxfs_gpio_chip_to_port(gc) + 0xA != gpiospec->args[2])
		return -EINVAL;

	return of_gpio_simple_xlate(gc, gpiospec, flags);
}

static const struct of_device_id etraxfs_gpio_of_table[] = {
	{
		.compatible = "axis,etraxfs-gio",
		.data = &etraxfs_gpio_etraxfs,
	},
	{
		.compatible = "axis,artpec3-gio",
		.data = &etraxfs_gpio_artpec3,
	},
	{},
};

static unsigned int etraxfs_gpio_to_group_irq(unsigned int gpio)
{
	return gpio % 8;
}

static unsigned int etraxfs_gpio_to_group_pin(struct etraxfs_gpio_chip *chip,
					      unsigned int gpio)
{
	return 4 * etraxfs_gpio_chip_to_port(&chip->bgc.gc) + gpio / 8;
}

static void etraxfs_gpio_irq_ack(struct irq_data *d)
{
	struct etraxfs_gpio_chip *chip =
		to_etraxfs(irq_data_get_irq_chip_data(d));
	struct etraxfs_gpio_block *block = chip->block;
	unsigned int grpirq = etraxfs_gpio_to_group_irq(d->hwirq);

	writel(BIT(grpirq), block->regs + block->info->rw_ack_intr);
}

static void etraxfs_gpio_irq_mask(struct irq_data *d)
{
	struct etraxfs_gpio_chip *chip =
		to_etraxfs(irq_data_get_irq_chip_data(d));
	struct etraxfs_gpio_block *block = chip->block;
	unsigned int grpirq = etraxfs_gpio_to_group_irq(d->hwirq);

	spin_lock(&block->lock);
	block->mask &= ~BIT(grpirq);
	writel(block->mask, block->regs + block->info->rw_intr_mask);
	spin_unlock(&block->lock);
}

static void etraxfs_gpio_irq_unmask(struct irq_data *d)
{
	struct etraxfs_gpio_chip *chip =
		to_etraxfs(irq_data_get_irq_chip_data(d));
	struct etraxfs_gpio_block *block = chip->block;
	unsigned int grpirq = etraxfs_gpio_to_group_irq(d->hwirq);

	spin_lock(&block->lock);
	block->mask |= BIT(grpirq);
	writel(block->mask, block->regs + block->info->rw_intr_mask);
	spin_unlock(&block->lock);
}

static int etraxfs_gpio_irq_set_type(struct irq_data *d, u32 type)
{
	struct etraxfs_gpio_chip *chip =
		to_etraxfs(irq_data_get_irq_chip_data(d));
	struct etraxfs_gpio_block *block = chip->block;
	unsigned int grpirq = etraxfs_gpio_to_group_irq(d->hwirq);
	u32 cfg;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		cfg = GIO_CFG_POSEDGE;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		cfg = GIO_CFG_NEGEDGE;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		cfg = GIO_CFG_ANYEDGE;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		cfg = GIO_CFG_LO;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		cfg = GIO_CFG_HI;
		break;
	default:
		return -EINVAL;
	}

	spin_lock(&block->lock);
	block->cfg &= ~(0x7 << (grpirq * 3));
	block->cfg |= (cfg << (grpirq * 3));
	writel(block->cfg, block->regs + block->info->rw_intr_cfg);
	spin_unlock(&block->lock);

	return 0;
}

static int etraxfs_gpio_irq_request_resources(struct irq_data *d)
{
	struct etraxfs_gpio_chip *chip =
		to_etraxfs(irq_data_get_irq_chip_data(d));
	struct etraxfs_gpio_block *block = chip->block;
	unsigned int grpirq = etraxfs_gpio_to_group_irq(d->hwirq);
	int ret = -EBUSY;

	spin_lock(&block->lock);
	if (block->group[grpirq])
		goto out;

	ret = gpiochip_lock_as_irq(&chip->bgc.gc, d->hwirq);
	if (ret)
		goto out;

	block->group[grpirq] = d->irq;
	if (block->info->rw_intr_pins) {
		unsigned int pin = etraxfs_gpio_to_group_pin(chip, d->hwirq);

		block->pins &= ~(0xf << (grpirq * 4));
		block->pins |= (pin << (grpirq * 4));

		writel(block->pins, block->regs + block->info->rw_intr_pins);
	}

out:
	spin_unlock(&block->lock);
	return ret;
}

static void etraxfs_gpio_irq_release_resources(struct irq_data *d)
{
	struct etraxfs_gpio_chip *chip =
		to_etraxfs(irq_data_get_irq_chip_data(d));
	struct etraxfs_gpio_block *block = chip->block;
	unsigned int grpirq = etraxfs_gpio_to_group_irq(d->hwirq);

	spin_lock(&block->lock);
	block->group[grpirq] = 0;
	gpiochip_unlock_as_irq(&chip->bgc.gc, d->hwirq);
	spin_unlock(&block->lock);
}

static struct irq_chip etraxfs_gpio_irq_chip = {
	.name		= "gpio-etraxfs",
	.irq_ack	= etraxfs_gpio_irq_ack,
	.irq_mask	= etraxfs_gpio_irq_mask,
	.irq_unmask	= etraxfs_gpio_irq_unmask,
	.irq_set_type	= etraxfs_gpio_irq_set_type,
	.irq_request_resources = etraxfs_gpio_irq_request_resources,
	.irq_release_resources = etraxfs_gpio_irq_release_resources,
};

static irqreturn_t etraxfs_gpio_interrupt(int irq, void *dev_id)
{
	struct etraxfs_gpio_block *block = dev_id;
	unsigned long intr = readl(block->regs + block->info->r_masked_intr);
	int bit;

	for_each_set_bit(bit, &intr, 8)
		generic_handle_irq(block->group[bit]);

	return IRQ_RETVAL(intr & 0xff);
}

static int etraxfs_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct etraxfs_gpio_info *info;
	const struct of_device_id *match;
	struct etraxfs_gpio_block *block;
	struct etraxfs_gpio_chip *chips;
	struct resource *res, *irq;
	bool allportsirq = false;
	void __iomem *regs;
	int ret;
	int i;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	match = of_match_node(etraxfs_gpio_of_table, dev->of_node);
	if (!match)
		return -EINVAL;

	info = match->data;

	chips = devm_kzalloc(dev, sizeof(*chips) * info->num_ports, GFP_KERNEL);
	if (!chips)
		return -ENOMEM;

	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!irq)
		return -EINVAL;

	block = devm_kzalloc(dev, sizeof(*block), GFP_KERNEL);
	if (!block)
		return -ENOMEM;

	spin_lock_init(&block->lock);

	block->regs = regs;
	block->info = info;

	writel(0, block->regs + info->rw_intr_mask);
	writel(0, block->regs + info->rw_intr_cfg);
	if (info->rw_intr_pins) {
		allportsirq = true;
		writel(0, block->regs + info->rw_intr_pins);
	}

	ret = devm_request_irq(dev, irq->start, etraxfs_gpio_interrupt,
			       IRQF_SHARED, dev_name(dev), block);
	if (ret) {
		dev_err(dev, "Unable to request irq %d\n", ret);
		return ret;
	}

	for (i = 0; i < info->num_ports; i++) {
		struct etraxfs_gpio_chip *chip = &chips[i];
		struct bgpio_chip *bgc = &chip->bgc;
		const struct etraxfs_gpio_port *port = &info->ports[i];
		unsigned long flags = BGPIOF_READ_OUTPUT_REG_SET;
		void __iomem *dat = regs + port->din;
		void __iomem *set = regs + port->dout;
		void __iomem *dirout = regs + port->oe;

		chip->block = block;

		if (dirout == set) {
			dirout = set = NULL;
			flags = BGPIOF_NO_OUTPUT;
		}

		ret = bgpio_init(bgc, dev, 4,
				 dat, set, NULL, dirout, NULL,
				 flags);
		if (ret) {
			dev_err(dev, "Unable to init port %s\n",
				port->label);
			continue;
		}

		bgc->gc.ngpio = port->ngpio;
		bgc->gc.label = port->label;

		bgc->gc.of_node = dev->of_node;
		bgc->gc.of_gpio_n_cells = 3;
		bgc->gc.of_xlate = etraxfs_gpio_of_xlate;

		ret = gpiochip_add(&bgc->gc);
		if (ret) {
			dev_err(dev, "Unable to register port %s\n",
				bgc->gc.label);
			continue;
		}

		if (i > 0 && !allportsirq)
			continue;

		ret = gpiochip_irqchip_add(&bgc->gc, &etraxfs_gpio_irq_chip, 0,
					   handle_level_irq, IRQ_TYPE_NONE);
		if (ret) {
			dev_err(dev, "Unable to add irqchip to port %s\n",
				bgc->gc.label);
		}
	}

	return 0;
}

static struct platform_driver etraxfs_gpio_driver = {
	.driver = {
		.name		= "etraxfs-gpio",
		.of_match_table = of_match_ptr(etraxfs_gpio_of_table),
	},
	.probe	= etraxfs_gpio_probe,
};

static int __init etraxfs_gpio_init(void)
{
	return platform_driver_register(&etraxfs_gpio_driver);
}

device_initcall(etraxfs_gpio_init);
