// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2019 American Megatrends International LLC.
 *
 * Author: Karthikeyan Mani <karthikeyanm@amiindia.co.in>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/hashtable.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/string.h>

/*
 * MAX_NR_HW_GPIO represents the number of actual hardware-supported GPIOs (ie,
 * slots within the clocked serial GPIO data). Since each HW GPIO is both an
 * input and an output, we provide MAX_NR_HW_GPIO * 2 lines on our gpiochip
 * device.
 *
 * We use SGPIO_OUTPUT_OFFSET to define the split between the inputs and
 * outputs; the inputs start at line 0, the outputs start at OUTPUT_OFFSET.
 */
#define MAX_NR_HW_SGPIO			80
#define SGPIO_OUTPUT_OFFSET		MAX_NR_HW_SGPIO

#define ASPEED_SGPIO_CTRL		0x54

#define ASPEED_SGPIO_PINS_MASK		GENMASK(9, 6)
#define ASPEED_SGPIO_CLK_DIV_MASK	GENMASK(31, 16)
#define ASPEED_SGPIO_ENABLE		BIT(0)

struct aspeed_sgpio {
	struct gpio_chip chip;
	struct clk *pclk;
	spinlock_t lock;
	void __iomem *base;
	int irq;
	int n_sgpio;
};

struct aspeed_sgpio_bank {
	uint16_t    val_regs;
	uint16_t    rdata_reg;
	uint16_t    irq_regs;
	const char  names[4][3];
};

/*
 * Note: The "value" register returns the input value when the GPIO is
 *	 configured as an input.
 *
 *	 The "rdata" register returns the output value when the GPIO is
 *	 configured as an output.
 */
static const struct aspeed_sgpio_bank aspeed_sgpio_banks[] = {
	{
		.val_regs = 0x0000,
		.rdata_reg = 0x0070,
		.irq_regs = 0x0004,
		.names = { "A", "B", "C", "D" },
	},
	{
		.val_regs = 0x001C,
		.rdata_reg = 0x0074,
		.irq_regs = 0x0020,
		.names = { "E", "F", "G", "H" },
	},
	{
		.val_regs = 0x0038,
		.rdata_reg = 0x0078,
		.irq_regs = 0x003C,
		.names = { "I", "J" },
	},
};

enum aspeed_sgpio_reg {
	reg_val,
	reg_rdata,
	reg_irq_enable,
	reg_irq_type0,
	reg_irq_type1,
	reg_irq_type2,
	reg_irq_status,
};

#define GPIO_VAL_VALUE      0x00
#define GPIO_IRQ_ENABLE     0x00
#define GPIO_IRQ_TYPE0      0x04
#define GPIO_IRQ_TYPE1      0x08
#define GPIO_IRQ_TYPE2      0x0C
#define GPIO_IRQ_STATUS     0x10

static void __iomem *bank_reg(struct aspeed_sgpio *gpio,
				     const struct aspeed_sgpio_bank *bank,
				     const enum aspeed_sgpio_reg reg)
{
	switch (reg) {
	case reg_val:
		return gpio->base + bank->val_regs + GPIO_VAL_VALUE;
	case reg_rdata:
		return gpio->base + bank->rdata_reg;
	case reg_irq_enable:
		return gpio->base + bank->irq_regs + GPIO_IRQ_ENABLE;
	case reg_irq_type0:
		return gpio->base + bank->irq_regs + GPIO_IRQ_TYPE0;
	case reg_irq_type1:
		return gpio->base + bank->irq_regs + GPIO_IRQ_TYPE1;
	case reg_irq_type2:
		return gpio->base + bank->irq_regs + GPIO_IRQ_TYPE2;
	case reg_irq_status:
		return gpio->base + bank->irq_regs + GPIO_IRQ_STATUS;
	default:
		/* acturally if code runs to here, it's an error case */
		BUG();
	}
}

#define GPIO_BANK(x)    ((x % SGPIO_OUTPUT_OFFSET) >> 5)
#define GPIO_OFFSET(x)  ((x % SGPIO_OUTPUT_OFFSET) & 0x1f)
#define GPIO_BIT(x)     BIT(GPIO_OFFSET(x))

static const struct aspeed_sgpio_bank *to_bank(unsigned int offset)
{
	unsigned int bank;

	bank = GPIO_BANK(offset);

	WARN_ON(bank >= ARRAY_SIZE(aspeed_sgpio_banks));
	return &aspeed_sgpio_banks[bank];
}

static int aspeed_sgpio_init_valid_mask(struct gpio_chip *gc,
		unsigned long *valid_mask, unsigned int ngpios)
{
	struct aspeed_sgpio *sgpio = gpiochip_get_data(gc);
	int n = sgpio->n_sgpio;
	int c = SGPIO_OUTPUT_OFFSET - n;

	WARN_ON(ngpios < MAX_NR_HW_SGPIO * 2);

	/* input GPIOs in the lower range */
	bitmap_set(valid_mask, 0, n);
	bitmap_clear(valid_mask, n, c);

	/* output GPIOS above SGPIO_OUTPUT_OFFSET */
	bitmap_set(valid_mask, SGPIO_OUTPUT_OFFSET, n);
	bitmap_clear(valid_mask, SGPIO_OUTPUT_OFFSET + n, c);

	return 0;
}

static void aspeed_sgpio_irq_init_valid_mask(struct gpio_chip *gc,
		unsigned long *valid_mask, unsigned int ngpios)
{
	struct aspeed_sgpio *sgpio = gpiochip_get_data(gc);
	int n = sgpio->n_sgpio;

	WARN_ON(ngpios < MAX_NR_HW_SGPIO * 2);

	/* input GPIOs in the lower range */
	bitmap_set(valid_mask, 0, n);
	bitmap_clear(valid_mask, n, ngpios - n);
}

static bool aspeed_sgpio_is_input(unsigned int offset)
{
	return offset < SGPIO_OUTPUT_OFFSET;
}

static int aspeed_sgpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct aspeed_sgpio *gpio = gpiochip_get_data(gc);
	const struct aspeed_sgpio_bank *bank = to_bank(offset);
	unsigned long flags;
	enum aspeed_sgpio_reg reg;
	int rc = 0;

	spin_lock_irqsave(&gpio->lock, flags);

	reg = aspeed_sgpio_is_input(offset) ? reg_val : reg_rdata;
	rc = !!(ioread32(bank_reg(gpio, bank, reg)) & GPIO_BIT(offset));

	spin_unlock_irqrestore(&gpio->lock, flags);

	return rc;
}

static int sgpio_set_value(struct gpio_chip *gc, unsigned int offset, int val)
{
	struct aspeed_sgpio *gpio = gpiochip_get_data(gc);
	const struct aspeed_sgpio_bank *bank = to_bank(offset);
	void __iomem *addr_r, *addr_w;
	u32 reg = 0;

	if (aspeed_sgpio_is_input(offset))
		return -EINVAL;

	/* Since this is an output, read the cached value from rdata, then
	 * update val. */
	addr_r = bank_reg(gpio, bank, reg_rdata);
	addr_w = bank_reg(gpio, bank, reg_val);

	reg = ioread32(addr_r);

	if (val)
		reg |= GPIO_BIT(offset);
	else
		reg &= ~GPIO_BIT(offset);

	iowrite32(reg, addr_w);

	return 0;
}

static void aspeed_sgpio_set(struct gpio_chip *gc, unsigned int offset, int val)
{
	struct aspeed_sgpio *gpio = gpiochip_get_data(gc);
	unsigned long flags;

	spin_lock_irqsave(&gpio->lock, flags);

	sgpio_set_value(gc, offset, val);

	spin_unlock_irqrestore(&gpio->lock, flags);
}

static int aspeed_sgpio_dir_in(struct gpio_chip *gc, unsigned int offset)
{
	return aspeed_sgpio_is_input(offset) ? 0 : -EINVAL;
}

static int aspeed_sgpio_dir_out(struct gpio_chip *gc, unsigned int offset, int val)
{
	struct aspeed_sgpio *gpio = gpiochip_get_data(gc);
	unsigned long flags;
	int rc;

	/* No special action is required for setting the direction; we'll
	 * error-out in sgpio_set_value if this isn't an output GPIO */

	spin_lock_irqsave(&gpio->lock, flags);
	rc = sgpio_set_value(gc, offset, val);
	spin_unlock_irqrestore(&gpio->lock, flags);

	return rc;
}

static int aspeed_sgpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	return !!aspeed_sgpio_is_input(offset);
}

static void irqd_to_aspeed_sgpio_data(struct irq_data *d,
					struct aspeed_sgpio **gpio,
					const struct aspeed_sgpio_bank **bank,
					u32 *bit, int *offset)
{
	struct aspeed_sgpio *internal;

	*offset = irqd_to_hwirq(d);
	internal = irq_data_get_irq_chip_data(d);
	WARN_ON(!internal);

	*gpio = internal;
	*bank = to_bank(*offset);
	*bit = GPIO_BIT(*offset);
}

static void aspeed_sgpio_irq_ack(struct irq_data *d)
{
	const struct aspeed_sgpio_bank *bank;
	struct aspeed_sgpio *gpio;
	unsigned long flags;
	void __iomem *status_addr;
	int offset;
	u32 bit;

	irqd_to_aspeed_sgpio_data(d, &gpio, &bank, &bit, &offset);

	status_addr = bank_reg(gpio, bank, reg_irq_status);

	spin_lock_irqsave(&gpio->lock, flags);

	iowrite32(bit, status_addr);

	spin_unlock_irqrestore(&gpio->lock, flags);
}

static void aspeed_sgpio_irq_set_mask(struct irq_data *d, bool set)
{
	const struct aspeed_sgpio_bank *bank;
	struct aspeed_sgpio *gpio;
	unsigned long flags;
	u32 reg, bit;
	void __iomem *addr;
	int offset;

	irqd_to_aspeed_sgpio_data(d, &gpio, &bank, &bit, &offset);
	addr = bank_reg(gpio, bank, reg_irq_enable);

	spin_lock_irqsave(&gpio->lock, flags);

	reg = ioread32(addr);
	if (set)
		reg |= bit;
	else
		reg &= ~bit;

	iowrite32(reg, addr);

	spin_unlock_irqrestore(&gpio->lock, flags);
}

static void aspeed_sgpio_irq_mask(struct irq_data *d)
{
	aspeed_sgpio_irq_set_mask(d, false);
}

static void aspeed_sgpio_irq_unmask(struct irq_data *d)
{
	aspeed_sgpio_irq_set_mask(d, true);
}

static int aspeed_sgpio_set_type(struct irq_data *d, unsigned int type)
{
	u32 type0 = 0;
	u32 type1 = 0;
	u32 type2 = 0;
	u32 bit, reg;
	const struct aspeed_sgpio_bank *bank;
	irq_flow_handler_t handler;
	struct aspeed_sgpio *gpio;
	unsigned long flags;
	void __iomem *addr;
	int offset;

	irqd_to_aspeed_sgpio_data(d, &gpio, &bank, &bit, &offset);

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_BOTH:
		type2 |= bit;
		fallthrough;
	case IRQ_TYPE_EDGE_RISING:
		type0 |= bit;
		fallthrough;
	case IRQ_TYPE_EDGE_FALLING:
		handler = handle_edge_irq;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		type0 |= bit;
		fallthrough;
	case IRQ_TYPE_LEVEL_LOW:
		type1 |= bit;
		handler = handle_level_irq;
		break;
	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&gpio->lock, flags);

	addr = bank_reg(gpio, bank, reg_irq_type0);
	reg = ioread32(addr);
	reg = (reg & ~bit) | type0;
	iowrite32(reg, addr);

	addr = bank_reg(gpio, bank, reg_irq_type1);
	reg = ioread32(addr);
	reg = (reg & ~bit) | type1;
	iowrite32(reg, addr);

	addr = bank_reg(gpio, bank, reg_irq_type2);
	reg = ioread32(addr);
	reg = (reg & ~bit) | type2;
	iowrite32(reg, addr);

	spin_unlock_irqrestore(&gpio->lock, flags);

	irq_set_handler_locked(d, handler);

	return 0;
}

static void aspeed_sgpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct irq_chip *ic = irq_desc_get_chip(desc);
	struct aspeed_sgpio *data = gpiochip_get_data(gc);
	unsigned int i, p;
	unsigned long reg;

	chained_irq_enter(ic, desc);

	for (i = 0; i < ARRAY_SIZE(aspeed_sgpio_banks); i++) {
		const struct aspeed_sgpio_bank *bank = &aspeed_sgpio_banks[i];

		reg = ioread32(bank_reg(data, bank, reg_irq_status));

		for_each_set_bit(p, &reg, 32)
			generic_handle_domain_irq(gc->irq.domain, i * 32 + p);
	}

	chained_irq_exit(ic, desc);
}

static struct irq_chip aspeed_sgpio_irqchip = {
	.name       = "aspeed-sgpio",
	.irq_ack    = aspeed_sgpio_irq_ack,
	.irq_mask   = aspeed_sgpio_irq_mask,
	.irq_unmask = aspeed_sgpio_irq_unmask,
	.irq_set_type   = aspeed_sgpio_set_type,
};

static int aspeed_sgpio_setup_irqs(struct aspeed_sgpio *gpio,
				   struct platform_device *pdev)
{
	int rc, i;
	const struct aspeed_sgpio_bank *bank;
	struct gpio_irq_chip *irq;

	rc = platform_get_irq(pdev, 0);
	if (rc < 0)
		return rc;

	gpio->irq = rc;

	/* Disable IRQ and clear Interrupt status registers for all SGPIO Pins. */
	for (i = 0; i < ARRAY_SIZE(aspeed_sgpio_banks); i++) {
		bank =  &aspeed_sgpio_banks[i];
		/* disable irq enable bits */
		iowrite32(0x00000000, bank_reg(gpio, bank, reg_irq_enable));
		/* clear status bits */
		iowrite32(0xffffffff, bank_reg(gpio, bank, reg_irq_status));
	}

	irq = &gpio->chip.irq;
	irq->chip = &aspeed_sgpio_irqchip;
	irq->init_valid_mask = aspeed_sgpio_irq_init_valid_mask;
	irq->handler = handle_bad_irq;
	irq->default_type = IRQ_TYPE_NONE;
	irq->parent_handler = aspeed_sgpio_irq_handler;
	irq->parent_handler_data = gpio;
	irq->parents = &gpio->irq;
	irq->num_parents = 1;

	/* Apply default IRQ settings */
	for (i = 0; i < ARRAY_SIZE(aspeed_sgpio_banks); i++) {
		bank = &aspeed_sgpio_banks[i];
		/* set falling or level-low irq */
		iowrite32(0x00000000, bank_reg(gpio, bank, reg_irq_type0));
		/* trigger type is edge */
		iowrite32(0x00000000, bank_reg(gpio, bank, reg_irq_type1));
		/* single edge trigger */
		iowrite32(0x00000000, bank_reg(gpio, bank, reg_irq_type2));
	}

	return 0;
}

static const struct of_device_id aspeed_sgpio_of_table[] = {
	{ .compatible = "aspeed,ast2400-sgpio" },
	{ .compatible = "aspeed,ast2500-sgpio" },
	{}
};

MODULE_DEVICE_TABLE(of, aspeed_sgpio_of_table);

static int __init aspeed_sgpio_probe(struct platform_device *pdev)
{
	struct aspeed_sgpio *gpio;
	u32 nr_gpios, sgpio_freq, sgpio_clk_div;
	int rc;
	unsigned long apb_freq;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gpio->base))
		return PTR_ERR(gpio->base);

	rc = of_property_read_u32(pdev->dev.of_node, "ngpios", &nr_gpios);
	if (rc < 0) {
		dev_err(&pdev->dev, "Could not read ngpios property\n");
		return -EINVAL;
	} else if (nr_gpios > MAX_NR_HW_SGPIO) {
		dev_err(&pdev->dev, "Number of GPIOs exceeds the maximum of %d: %d\n",
			MAX_NR_HW_SGPIO, nr_gpios);
		return -EINVAL;
	}
	gpio->n_sgpio = nr_gpios;

	rc = of_property_read_u32(pdev->dev.of_node, "bus-frequency", &sgpio_freq);
	if (rc < 0) {
		dev_err(&pdev->dev, "Could not read bus-frequency property\n");
		return -EINVAL;
	}

	gpio->pclk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(gpio->pclk)) {
		dev_err(&pdev->dev, "devm_clk_get failed\n");
		return PTR_ERR(gpio->pclk);
	}

	apb_freq = clk_get_rate(gpio->pclk);

	/*
	 * From the datasheet,
	 *	SGPIO period = 1/PCLK * 2 * (GPIO254[31:16] + 1)
	 *	period = 2 * (GPIO254[31:16] + 1) / PCLK
	 *	frequency = 1 / (2 * (GPIO254[31:16] + 1) / PCLK)
	 *	frequency = PCLK / (2 * (GPIO254[31:16] + 1))
	 *	frequency * 2 * (GPIO254[31:16] + 1) = PCLK
	 *	GPIO254[31:16] = PCLK / (frequency * 2) - 1
	 */
	if (sgpio_freq == 0)
		return -EINVAL;

	sgpio_clk_div = (apb_freq / (sgpio_freq * 2)) - 1;

	if (sgpio_clk_div > (1 << 16) - 1)
		return -EINVAL;

	iowrite32(FIELD_PREP(ASPEED_SGPIO_CLK_DIV_MASK, sgpio_clk_div) |
		  FIELD_PREP(ASPEED_SGPIO_PINS_MASK, (nr_gpios / 8)) |
		  ASPEED_SGPIO_ENABLE,
		  gpio->base + ASPEED_SGPIO_CTRL);

	spin_lock_init(&gpio->lock);

	gpio->chip.parent = &pdev->dev;
	gpio->chip.ngpio = MAX_NR_HW_SGPIO * 2;
	gpio->chip.init_valid_mask = aspeed_sgpio_init_valid_mask;
	gpio->chip.direction_input = aspeed_sgpio_dir_in;
	gpio->chip.direction_output = aspeed_sgpio_dir_out;
	gpio->chip.get_direction = aspeed_sgpio_get_direction;
	gpio->chip.request = NULL;
	gpio->chip.free = NULL;
	gpio->chip.get = aspeed_sgpio_get;
	gpio->chip.set = aspeed_sgpio_set;
	gpio->chip.set_config = NULL;
	gpio->chip.label = dev_name(&pdev->dev);
	gpio->chip.base = -1;

	aspeed_sgpio_setup_irqs(gpio, pdev);

	rc = devm_gpiochip_add_data(&pdev->dev, &gpio->chip, gpio);
	if (rc < 0)
		return rc;

	return 0;
}

static struct platform_driver aspeed_sgpio_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = aspeed_sgpio_of_table,
	},
};

module_platform_driver_probe(aspeed_sgpio_driver, aspeed_sgpio_probe);
MODULE_DESCRIPTION("Aspeed Serial GPIO Driver");
MODULE_LICENSE("GPL");
