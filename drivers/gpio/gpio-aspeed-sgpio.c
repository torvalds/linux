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
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#define SGPIO_G7_IRQ_STS_BASE 0x40
#define SGPIO_G7_IRQ_STS_OFFSET(x) (SGPIO_G7_IRQ_STS_BASE + (x) * 0x4)
#define SGPIO_G7_CTRL_REG_BASE 0x80
#define SGPIO_G7_CTRL_REG_OFFSET(x) (SGPIO_G7_CTRL_REG_BASE + (x) * 0x4)
#define SGPIO_G7_OUT_DATA BIT(0)
#define SGPIO_G7_IRQ_EN BIT(2)
#define SGPIO_G7_IRQ_TYPE0 BIT(3)
#define SGPIO_G7_IRQ_TYPE1 BIT(4)
#define SGPIO_G7_IRQ_TYPE2 BIT(5)
#define SGPIO_G7_RST_TOLERANCE BIT(6)
#define SGPIO_G7_INPUT_MASK BIT(9)
#define SGPIO_G7_HW_BYPASS_EN BIT(10)
#define SGPIO_G7_HW_IN_SEL BIT(11)
#define SGPIO_G7_IRQ_STS BIT(12)
#define SGPIO_G7_IN_DATA BIT(13)
#define SGPIO_G7_PARALLEL_IN_DATA BIT(14)

static inline u32 field_get(u32 _mask, u32 _val)
{
	return (((_val) & (_mask)) >> (ffs(_mask) - 1));
}

static inline u32 field_prep(u32 _mask, u32 _val)
{
	return (((_val) << (ffs(_mask) - 1)) & (_mask));
}

static inline void ast_write_bits(void __iomem *addr, u32 mask, u32 val)
{
	iowrite32((ioread32(addr) & ~(mask)) | field_prep(mask, val), addr);
}

static inline void ast_clr_bits(void __iomem *addr, u32 mask)
{
	iowrite32((ioread32(addr) & ~(mask)), addr);
}

#define ASPEED_SGPIO_CLK_DIV_MASK	GENMASK(31, 16)
#define ASPEED_SGPIO_ENABLE		BIT(0)
#define ASPEED_SGPIO_PINS_SHIFT		6

struct aspeed_sgpio_pdata {
	const u32 pin_mask;
	const u16 ctrl_reg;
	const int version;
	const bool slave;
};

struct aspeed_sgpio {
	struct gpio_chip chip;
	struct device *dev;
	struct clk *pclk;
	raw_spinlock_t lock;
	void __iomem *base;
	int irq;
	int version;
};

struct aspeed_sgpio_bank {
	u16    val_regs;
	u16    rdata_reg;
	u16    irq_regs;
	u16    tolerance_regs;
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
		.tolerance_regs = 0x0018,
		.names = { "A", "B", "C", "D" },
	},
	{
		.val_regs = 0x001C,
		.rdata_reg = 0x0074,
		.irq_regs = 0x0020,
		.tolerance_regs = 0x0034,
		.names = { "E", "F", "G", "H" },
	},
	{
		.val_regs = 0x0038,
		.rdata_reg = 0x0078,
		.irq_regs = 0x003C,
		.tolerance_regs = 0x0050,
		.names = { "I", "J", "K", "L" },
	},
	{
		.val_regs = 0x0090,
		.rdata_reg = 0x007C,
		.irq_regs = 0x0094,
		.tolerance_regs = 0x00A8,
		.names = { "M", "N", "O", "P" },
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
	reg_tolerance,
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
	case reg_tolerance:
		return gpio->base + bank->tolerance_regs;
	default:
		/* acturally if code runs to here, it's an error case */
		BUG();
	}
}

#define GPIO_BANK(x)    ((x) >> 6)
#define GPIO_OFFSET(x)  ((x) & GENMASK(5, 0))
#define GPIO_BIT(x)     BIT(GPIO_OFFSET(x) >> 1)

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
	bitmap_set(valid_mask, 0, ngpios);
	return 0;
}

static void aspeed_sgpio_irq_init_valid_mask(struct gpio_chip *gc,
		unsigned long *valid_mask, unsigned int ngpios)
{
	unsigned int i;

	/* input GPIOs are even bits */
	for (i = 0; i < ngpios; i++) {
		if (i % 2)
			clear_bit(i, valid_mask);
	}
}

static bool aspeed_sgpio_is_input(unsigned int offset)
{
	return !(offset % 2);
}

static int aspeed_sgpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct aspeed_sgpio *gpio = gpiochip_get_data(gc);
	const struct aspeed_sgpio_bank *bank = to_bank(offset);
	void __iomem *addr = gpio->base + SGPIO_G7_CTRL_REG_OFFSET(offset >> 1);
	unsigned long flags;
	enum aspeed_sgpio_reg reg;
	int rc = 0;

	raw_spin_lock_irqsave(&gpio->lock, flags);

	if (gpio->version == 7) {
		reg = aspeed_sgpio_is_input(offset) ? SGPIO_G7_IN_DATA :
					      SGPIO_G7_OUT_DATA;
		rc = !!(field_get(reg, ioread32(addr)));
	} else {
		reg = aspeed_sgpio_is_input(offset) ? reg_val : reg_rdata;
		rc = !!(ioread32(bank_reg(gpio, bank, reg)) & GPIO_BIT(offset));
	}
	raw_spin_unlock_irqrestore(&gpio->lock, flags);

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

static int sgpio_g7_set_value(struct gpio_chip *gc, unsigned int offset,
			      int val)
{
	struct aspeed_sgpio *gpio = gpiochip_get_data(gc);
	void __iomem *addr = gpio->base + SGPIO_G7_CTRL_REG_OFFSET(offset >> 1);
	u32 reg = 0;

	if (aspeed_sgpio_is_input(offset))
		return -EINVAL;

	// Ensure the serial out value control by the software.
	ast_clr_bits(addr, SGPIO_G7_HW_BYPASS_EN | SGPIO_G7_HW_IN_SEL);
	reg = ioread32(addr);

	if (val)
		reg |= SGPIO_G7_OUT_DATA;
	else
		reg &= ~SGPIO_G7_OUT_DATA;

	iowrite32(reg, addr);

	return 0;
}

static void aspeed_sgpio_set(struct gpio_chip *gc, unsigned int offset, int val)
{
	struct aspeed_sgpio *gpio = gpiochip_get_data(gc);
	unsigned long flags;

	raw_spin_lock_irqsave(&gpio->lock, flags);

	if (gpio->version == 7)
		sgpio_g7_set_value(gc, offset, val);
	else
		sgpio_set_value(gc, offset, val);

	raw_spin_unlock_irqrestore(&gpio->lock, flags);
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

	raw_spin_lock_irqsave(&gpio->lock, flags);
	if (gpio->version == 7)
		rc = sgpio_g7_set_value(gc, offset, val);
	else
		rc = sgpio_set_value(gc, offset, val);
	raw_spin_unlock_irqrestore(&gpio->lock, flags);

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

static void irqd_to_aspeed_g7_sgpio_data(struct irq_data *d,
					 struct aspeed_sgpio **gpio,
					 int *offset)
{
	struct aspeed_sgpio *internal;

	*offset = irqd_to_hwirq(d);
	internal = irq_data_get_irq_chip_data(d);
	WARN_ON(!internal);

	*gpio = internal;
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

	raw_spin_lock_irqsave(&gpio->lock, flags);

	iowrite32(bit, status_addr);

	raw_spin_unlock_irqrestore(&gpio->lock, flags);
}

static void aspeed_g7_sgpio_irq_ack(struct irq_data *d)
{
	struct aspeed_sgpio *gpio;
	unsigned long flags;
	void __iomem *status_addr;
	int offset;

	irqd_to_aspeed_g7_sgpio_data(d, &gpio, &offset);

	status_addr = gpio->base + SGPIO_G7_CTRL_REG_OFFSET(offset >> 1);

	raw_spin_lock_irqsave(&gpio->lock, flags);

	ast_write_bits(status_addr, SGPIO_G7_IRQ_STS, 1);

	raw_spin_unlock_irqrestore(&gpio->lock, flags);
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

	/* Unmasking the IRQ */
	if (set)
		gpiochip_enable_irq(&gpio->chip, irqd_to_hwirq(d));

	raw_spin_lock_irqsave(&gpio->lock, flags);

	reg = ioread32(addr);
	if (set)
		reg |= bit;
	else
		reg &= ~bit;

	iowrite32(reg, addr);

	raw_spin_unlock_irqrestore(&gpio->lock, flags);

	/* Masking the IRQ */
	if (!set)
		gpiochip_disable_irq(&gpio->chip, irqd_to_hwirq(d));


}

static void aspeed_g7_sgpio_irq_set_mask(struct irq_data *d, bool set)
{
	struct aspeed_sgpio *gpio;
	unsigned long flags;
	void __iomem *addr;
	int offset;

	irqd_to_aspeed_g7_sgpio_data(d, &gpio, &offset);
	addr = gpio->base + SGPIO_G7_CTRL_REG_OFFSET(offset >> 1);

	/* Unmasking the IRQ */
	if (set)
		gpiochip_enable_irq(&gpio->chip, irqd_to_hwirq(d));

	raw_spin_lock_irqsave(&gpio->lock, flags);
	if (set)
		ast_write_bits(addr, SGPIO_G7_IRQ_EN, 1);
	else
		ast_clr_bits(addr, SGPIO_G7_IRQ_EN);
	raw_spin_unlock_irqrestore(&gpio->lock, flags);

	/* Masking the IRQ */
	if (!set)
		gpiochip_disable_irq(&gpio->chip, irqd_to_hwirq(d));
}

static void aspeed_sgpio_irq_mask(struct irq_data *d)
{
	aspeed_sgpio_irq_set_mask(d, false);
}

static void aspeed_sgpio_irq_unmask(struct irq_data *d)
{
	aspeed_sgpio_irq_set_mask(d, true);
}

static void aspeed_g7_sgpio_irq_mask(struct irq_data *d)
{
	aspeed_g7_sgpio_irq_set_mask(d, false);
}

static void aspeed_g7_sgpio_irq_unmask(struct irq_data *d)
{
	aspeed_g7_sgpio_irq_set_mask(d, true);
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

	raw_spin_lock_irqsave(&gpio->lock, flags);

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

	raw_spin_unlock_irqrestore(&gpio->lock, flags);

	irq_set_handler_locked(d, handler);

	return 0;
}

static int aspeed_g7_sgpio_set_type(struct irq_data *d, unsigned int type)
{
	u32 type0 = 0;
	u32 type1 = 0;
	u32 type2 = 0;
	irq_flow_handler_t handler;
	struct aspeed_sgpio *gpio;
	unsigned long flags;
	void __iomem *addr;
	int offset;

	irqd_to_aspeed_g7_sgpio_data(d, &gpio, &offset);
	addr = gpio->base + SGPIO_G7_CTRL_REG_OFFSET(offset >> 1);

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_BOTH:
		type2 = 1;
		fallthrough;
	case IRQ_TYPE_EDGE_RISING:
		type0 = 1;
		fallthrough;
	case IRQ_TYPE_EDGE_FALLING:
		handler = handle_edge_irq;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		type0 = 1;
		fallthrough;
	case IRQ_TYPE_LEVEL_LOW:
		type1 = 1;
		handler = handle_level_irq;
		break;
	default:
		return -EINVAL;
	}

	raw_spin_lock_irqsave(&gpio->lock, flags);

	ast_write_bits(addr, SGPIO_G7_IRQ_TYPE2, type2);
	ast_write_bits(addr, SGPIO_G7_IRQ_TYPE1, type1);
	ast_write_bits(addr, SGPIO_G7_IRQ_TYPE0, type0);

	raw_spin_unlock_irqrestore(&gpio->lock, flags);

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
			generic_handle_domain_irq(gc->irq.domain, (i * 32 + p) * 2);
	}

	chained_irq_exit(ic, desc);
}

static void aspeed_g7_sgpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct irq_chip *ic = irq_desc_get_chip(desc);
	struct aspeed_sgpio *gpio = gpiochip_get_data(gc);
	unsigned int i, p, banks;
	unsigned long reg;
	void __iomem *addr;

	chained_irq_enter(ic, desc);

	banks = DIV_ROUND_UP(gpio->chip.ngpio >> 2, 32);
	for (i = 0; i < banks; i++) {
		addr = gpio->base + SGPIO_G7_IRQ_STS_OFFSET(i);

		reg = ioread32(addr);

		for_each_set_bit(p, &reg, 32)
			generic_handle_domain_irq(gc->irq.domain, (i * 32 + p) * 2);
	}
	chained_irq_exit(ic, desc);
}

static void aspeed_sgpio_irq_print_chip(struct irq_data *d, struct seq_file *p)
{
	const struct aspeed_sgpio_bank *bank;
	struct aspeed_sgpio *gpio;
	u32 bit;
	int offset;

	irqd_to_aspeed_sgpio_data(d, &gpio, &bank, &bit, &offset);
	seq_printf(p, dev_name(gpio->dev));
}

static void aspeed_g7_sgpio_irq_print_chip(struct irq_data *d, struct seq_file *p)
{
	struct aspeed_sgpio *gpio;
	int offset;

	irqd_to_aspeed_g7_sgpio_data(d, &gpio, &offset);
	seq_printf(p, dev_name(gpio->dev));
}

static const struct irq_chip aspeed_sgpio_irq_chip = {
	.irq_ack = aspeed_sgpio_irq_ack,
	.irq_mask = aspeed_sgpio_irq_mask,
	.irq_unmask = aspeed_sgpio_irq_unmask,
	.irq_set_type = aspeed_sgpio_set_type,
	.irq_print_chip = aspeed_sgpio_irq_print_chip,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static const struct irq_chip aspeed_g7_sgpio_irq_chip = {
	.irq_ack = aspeed_g7_sgpio_irq_ack,
	.irq_mask = aspeed_g7_sgpio_irq_mask,
	.irq_unmask = aspeed_g7_sgpio_irq_unmask,
	.irq_set_type = aspeed_g7_sgpio_set_type,
	.irq_print_chip = aspeed_g7_sgpio_irq_print_chip,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
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

	if (gpio->version != 7)
		/* Disable IRQ and clear Interrupt status registers for all SGPIO Pins. */
		for (i = 0; i < ARRAY_SIZE(aspeed_sgpio_banks); i++) {
			bank =  &aspeed_sgpio_banks[i];
			/* disable irq enable bits */
			iowrite32(0x00000000, bank_reg(gpio, bank, reg_irq_enable));
			/* clear status bits */
			iowrite32(0xffffffff, bank_reg(gpio, bank, reg_irq_status));
		}

	irq = &gpio->chip.irq;
	if (gpio->version == 7)
		gpio_irq_chip_set_chip(irq, &aspeed_g7_sgpio_irq_chip);
	else
		gpio_irq_chip_set_chip(irq, &aspeed_sgpio_irq_chip);
	irq->init_valid_mask = aspeed_sgpio_irq_init_valid_mask;
	irq->handler = handle_bad_irq;
	irq->default_type = IRQ_TYPE_NONE;
	irq->parent_handler = (gpio->version == 7) ?
				      aspeed_g7_sgpio_irq_handler :
				      aspeed_sgpio_irq_handler;
	irq->parent_handler_data = gpio;
	irq->parents = &gpio->irq;
	irq->num_parents = 1;

	if (gpio->version != 7)
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

static const struct aspeed_sgpio_pdata ast2400_sgpio_pdata = {
	.pin_mask = GENMASK(9, 6),
	.ctrl_reg = 0x54,
};

static int aspeed_sgpio_reset_tolerance(struct gpio_chip *chip,
					unsigned int offset, bool enable)
{
	struct aspeed_sgpio *gpio = gpiochip_get_data(chip);
	unsigned long flags;
	void __iomem *reg;
	u32 val;

	reg = bank_reg(gpio, to_bank(offset), reg_tolerance);

	raw_spin_lock_irqsave(&gpio->lock, flags);

	val = readl(reg);

	if (enable)
		val |= GPIO_BIT(offset);
	else
		val &= ~GPIO_BIT(offset);

	writel(val, reg);

	raw_spin_unlock_irqrestore(&gpio->lock, flags);

	return 0;
}

static int aspeed_g7_sgpio_reset_tolerance(struct gpio_chip *chip,
					   unsigned int offset, bool enable)
{
	struct aspeed_sgpio *gpio = gpiochip_get_data(chip);
	unsigned long flags;
	void __iomem *reg;

	reg = gpio->base + SGPIO_G7_CTRL_REG_OFFSET(offset >> 1);

	raw_spin_lock_irqsave(&gpio->lock, flags);

	if (enable)
		ast_write_bits(reg, SGPIO_G7_RST_TOLERANCE, 1);
	else
		ast_clr_bits(reg, SGPIO_G7_RST_TOLERANCE);

	raw_spin_unlock_irqrestore(&gpio->lock, flags);

	return 0;
}

static int aspeed_sgpio_set_config(struct gpio_chip *chip, unsigned int offset,
				   unsigned long config)
{
	struct aspeed_sgpio *gpio = gpiochip_get_data(chip);
	unsigned long param = pinconf_to_config_param(config);
	u32 arg = pinconf_to_config_argument(config);

	if (param == PIN_CONFIG_PERSIST_STATE) {
		if (gpio->version == 7)
			return aspeed_g7_sgpio_reset_tolerance(chip, offset,
							       arg);
		else
			return aspeed_sgpio_reset_tolerance(chip, offset, arg);
	}

	return -ENOTSUPP;
}

static const struct aspeed_sgpio_pdata ast2600_sgpiom_pdata = {
	.pin_mask = GENMASK(10, 6),
	.ctrl_reg = 0x54,
};

static const struct aspeed_sgpio_pdata ast2700_sgpiom_pdata = {
	.pin_mask = GENMASK(11, 6),
	.ctrl_reg = 0x0,
	.version = 7,
};

static const struct aspeed_sgpio_pdata ast2700_sgpios_pdata = {
	.pin_mask = GENMASK(11, 6),
	.ctrl_reg = 0x0,
	.version = 7,
	.slave = 1,
};

static const struct of_device_id aspeed_sgpio_of_table[] = {
	{ .compatible = "aspeed,ast2400-sgpio", .data = &ast2400_sgpio_pdata, },
	{ .compatible = "aspeed,ast2500-sgpio", .data = &ast2400_sgpio_pdata, },
	{ .compatible = "aspeed,ast2600-sgpiom", .data = &ast2600_sgpiom_pdata, },
	{ .compatible = "aspeed,ast2700-sgpiom", .data = &ast2700_sgpiom_pdata, },
	{ .compatible = "aspeed,ast2700-sgpios", .data = &ast2700_sgpios_pdata, },
	{}
};

MODULE_DEVICE_TABLE(of, aspeed_sgpio_of_table);

static int __init aspeed_sgpio_probe(struct platform_device *pdev)
{
	u32 nr_gpios, sgpio_freq, sgpio_clk_div, gpio_cnt_regval, pin_mask;
	const struct aspeed_sgpio_pdata *pdata;
	struct aspeed_sgpio *gpio;
	unsigned long apb_freq;
	void __iomem *addr;
	int rc, i;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gpio->base))
		return PTR_ERR(gpio->base);

	gpio->dev = &pdev->dev;

	pdata = device_get_match_data(&pdev->dev);
	if (!pdata)
		return -EINVAL;

	pin_mask = pdata->pin_mask;
	gpio->version = pdata->version;

	rc = device_property_read_u32(&pdev->dev, "ngpios", &nr_gpios);
	if (rc < 0) {
		dev_err(&pdev->dev, "Could not read ngpios property\n");
		return -EINVAL;
	} else if (nr_gpios % 8) {
		dev_err(&pdev->dev, "Number of GPIOs not multiple of 8: %d\n",
			nr_gpios);
		return -EINVAL;
	}

	if (gpio->version == 7 && !pdata->slave)
		for (i = 0; i < nr_gpios; i++) {
			addr = gpio->base + SGPIO_G7_CTRL_REG_OFFSET(i);
			ast_clr_bits(addr, SGPIO_G7_HW_BYPASS_EN |
						   SGPIO_G7_HW_IN_SEL);
		}

	if (!pdata->slave) {
		rc = device_property_read_u32(&pdev->dev, "bus-frequency", &sgpio_freq);
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

		gpio_cnt_regval = ((nr_gpios / 8) << ASPEED_SGPIO_PINS_SHIFT) & pin_mask;
		iowrite32(FIELD_PREP(ASPEED_SGPIO_CLK_DIV_MASK, sgpio_clk_div) |
				gpio_cnt_regval | ASPEED_SGPIO_ENABLE,
			gpio->base + pdata->ctrl_reg);
	} else {
		iowrite32(ASPEED_SGPIO_ENABLE, gpio->base + pdata->ctrl_reg);
	}

	raw_spin_lock_init(&gpio->lock);

	gpio->chip.parent = &pdev->dev;
	gpio->chip.ngpio = nr_gpios * 2;
	gpio->chip.init_valid_mask = aspeed_sgpio_init_valid_mask;
	gpio->chip.direction_input = aspeed_sgpio_dir_in;
	gpio->chip.direction_output = aspeed_sgpio_dir_out;
	gpio->chip.get_direction = aspeed_sgpio_get_direction;
	gpio->chip.request = NULL;
	gpio->chip.free = NULL;
	gpio->chip.get = aspeed_sgpio_get;
	gpio->chip.set = aspeed_sgpio_set;
	gpio->chip.set_config = aspeed_sgpio_set_config;
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
