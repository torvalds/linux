// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Granite Rapids-D vGPIO driver
 *
 * Copyright (c) 2024, Intel Corporation.
 *
 * Author: Aapo Vienamo <aapo.vienamo@linux.intel.com>
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gfp_types.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/math.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <linux/gpio/driver.h>

#define GNR_NUM_PINS 128
#define GNR_PINS_PER_REG 32
#define GNR_NUM_REGS DIV_ROUND_UP(GNR_NUM_PINS, GNR_PINS_PER_REG)

#define GNR_CFG_BAR		0x00
#define GNR_CFG_LOCK_OFFSET	0x04
#define GNR_GPI_STATUS_OFFSET	0x20
#define GNR_GPI_ENABLE_OFFSET	0x24

#define GNR_CFG_DW_RX_MASK	GENMASK(25, 22)
#define GNR_CFG_DW_RX_DISABLE	FIELD_PREP(GNR_CFG_DW_RX_MASK, 2)
#define GNR_CFG_DW_RX_EDGE	FIELD_PREP(GNR_CFG_DW_RX_MASK, 1)
#define GNR_CFG_DW_RX_LEVEL	FIELD_PREP(GNR_CFG_DW_RX_MASK, 0)
#define GNR_CFG_DW_RXDIS	BIT(4)
#define GNR_CFG_DW_TXDIS	BIT(3)
#define GNR_CFG_DW_RXSTATE	BIT(1)
#define GNR_CFG_DW_TXSTATE	BIT(0)

/**
 * struct gnr_gpio - Intel Granite Rapids-D vGPIO driver state
 * @gc: GPIO controller interface
 * @reg_base: base address of the GPIO registers
 * @ro_bitmap: bitmap of read-only pins
 * @lock: guard the registers
 * @pad_backup: backup of the register state for suspend
 */
struct gnr_gpio {
	struct gpio_chip gc;
	void __iomem *reg_base;
	DECLARE_BITMAP(ro_bitmap, GNR_NUM_PINS);
	raw_spinlock_t lock;
	u32 pad_backup[];
};

static void __iomem *gnr_gpio_get_padcfg_addr(const struct gnr_gpio *priv,
					      unsigned int gpio)
{
	return priv->reg_base + gpio * sizeof(u32);
}

static int gnr_gpio_configure_line(struct gpio_chip *gc, unsigned int gpio,
				   u32 clear_mask, u32 set_mask)
{
	struct gnr_gpio *priv = gpiochip_get_data(gc);
	void __iomem *addr = gnr_gpio_get_padcfg_addr(priv, gpio);
	u32 dw;

	if (test_bit(gpio, priv->ro_bitmap))
		return -EACCES;

	guard(raw_spinlock_irqsave)(&priv->lock);

	dw = readl(addr);
	dw &= ~clear_mask;
	dw |= set_mask;
	writel(dw, addr);

	return 0;
}

static int gnr_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	const struct gnr_gpio *priv = gpiochip_get_data(gc);
	u32 dw;

	dw = readl(gnr_gpio_get_padcfg_addr(priv, gpio));

	return !!(dw & GNR_CFG_DW_RXSTATE);
}

static void gnr_gpio_set(struct gpio_chip *gc, unsigned int gpio, int value)
{
	u32 clear = 0;
	u32 set = 0;

	if (value)
		set = GNR_CFG_DW_TXSTATE;
	else
		clear = GNR_CFG_DW_TXSTATE;

	gnr_gpio_configure_line(gc, gpio, clear, set);
}

static int gnr_gpio_get_direction(struct gpio_chip *gc, unsigned int gpio)
{
	struct gnr_gpio *priv = gpiochip_get_data(gc);
	u32 dw;

	dw = readl(gnr_gpio_get_padcfg_addr(priv, gpio));

	if (dw & GNR_CFG_DW_TXDIS)
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
}

static int gnr_gpio_direction_input(struct gpio_chip *gc, unsigned int gpio)
{
	return gnr_gpio_configure_line(gc, gpio, GNR_CFG_DW_RXDIS, 0);
}

static int gnr_gpio_direction_output(struct gpio_chip *gc, unsigned int gpio, int value)
{
	u32 clear = GNR_CFG_DW_TXDIS;
	u32 set = value ? GNR_CFG_DW_TXSTATE : 0;

	return gnr_gpio_configure_line(gc, gpio, clear, set);
}

static const struct gpio_chip gnr_gpio_chip = {
	.owner		  = THIS_MODULE,
	.get		  = gnr_gpio_get,
	.set		  = gnr_gpio_set,
	.get_direction    = gnr_gpio_get_direction,
	.direction_input  = gnr_gpio_direction_input,
	.direction_output = gnr_gpio_direction_output,
};

static void __iomem *gnr_gpio_get_reg_addr(const struct gnr_gpio *priv,
					   unsigned int base,
					   unsigned int gpio)
{
	return priv->reg_base + base + gpio * sizeof(u32);
}

static void gnr_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct gnr_gpio *priv = gpiochip_get_data(gc);
	irq_hw_number_t gpio = irqd_to_hwirq(d);
	unsigned int reg_idx = gpio / GNR_PINS_PER_REG;
	unsigned int bit_idx = gpio % GNR_PINS_PER_REG;
	void __iomem *addr = gnr_gpio_get_reg_addr(priv, GNR_GPI_STATUS_OFFSET, reg_idx);
	u32 reg;

	guard(raw_spinlock_irqsave)(&priv->lock);

	reg = readl(addr);
	reg &= ~BIT(bit_idx);
	writel(reg, addr);
}

static void gnr_gpio_irq_mask_unmask(struct gpio_chip *gc, unsigned long gpio, bool mask)
{
	struct gnr_gpio *priv = gpiochip_get_data(gc);
	unsigned int reg_idx = gpio / GNR_PINS_PER_REG;
	unsigned int bit_idx = gpio % GNR_PINS_PER_REG;
	void __iomem *addr = gnr_gpio_get_reg_addr(priv, GNR_GPI_ENABLE_OFFSET, reg_idx);
	u32 reg;

	guard(raw_spinlock_irqsave)(&priv->lock);

	reg = readl(addr);
	if (mask)
		reg &= ~BIT(bit_idx);
	else
		reg |= BIT(bit_idx);
	writel(reg, addr);
}

static void gnr_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	gnr_gpio_irq_mask_unmask(gc, hwirq, true);
	gpiochip_disable_irq(gc, hwirq);
}

static void gnr_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	gpiochip_enable_irq(gc, hwirq);
	gnr_gpio_irq_mask_unmask(gc, hwirq, false);
}

static int gnr_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	irq_hw_number_t pin = irqd_to_hwirq(d);
	u32 mask = GNR_CFG_DW_RX_MASK;
	u32 set;

	/* Falling edge and level low triggers not supported by the GPIO controller */
	switch (type) {
	case IRQ_TYPE_NONE:
		set = GNR_CFG_DW_RX_DISABLE;
		break;
	case IRQ_TYPE_EDGE_RISING:
		set = GNR_CFG_DW_RX_EDGE;
		irq_set_handler_locked(d, handle_edge_irq);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		set = GNR_CFG_DW_RX_LEVEL;
		irq_set_handler_locked(d, handle_level_irq);
		break;
	default:
		return -EINVAL;
	}

	return gnr_gpio_configure_line(gc, pin, mask, set);
}

static const struct irq_chip gnr_gpio_irq_chip = {
	.irq_ack	= gnr_gpio_irq_ack,
	.irq_mask	= gnr_gpio_irq_mask,
	.irq_unmask	= gnr_gpio_irq_unmask,
	.irq_set_type	= gnr_gpio_irq_set_type,
	.flags		= IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static void gnr_gpio_init_pin_ro_bits(struct device *dev,
				      const void __iomem *cfg_lock_base,
				      unsigned long *ro_bitmap)
{
	u32 tmp[GNR_NUM_REGS];

	memcpy_fromio(tmp, cfg_lock_base, sizeof(tmp));
	bitmap_from_arr32(ro_bitmap, tmp, GNR_NUM_PINS);
}

static irqreturn_t gnr_gpio_irq(int irq, void *data)
{
	struct gnr_gpio *priv = data;
	unsigned int handled = 0;

	for (unsigned int i = 0; i < GNR_NUM_REGS; i++) {
		const void __iomem *reg = priv->reg_base + i * sizeof(u32);
		unsigned long pending;
		unsigned long enabled;
		unsigned int bit_idx;

		scoped_guard(raw_spinlock, &priv->lock) {
			pending = readl(reg + GNR_GPI_STATUS_OFFSET);
			enabled = readl(reg + GNR_GPI_ENABLE_OFFSET);
		}

		/* Only enabled interrupts */
		pending &= enabled;

		for_each_set_bit(bit_idx, &pending, GNR_PINS_PER_REG) {
			unsigned int hwirq = i * GNR_PINS_PER_REG + bit_idx;

			generic_handle_domain_irq(priv->gc.irq.domain, hwirq);
		}

		handled += pending ? 1 : 0;

	}
	return IRQ_RETVAL(handled);
}

static int gnr_gpio_probe(struct platform_device *pdev)
{
	size_t num_backup_pins = IS_ENABLED(CONFIG_PM_SLEEP) ? GNR_NUM_PINS : 0;
	struct device *dev = &pdev->dev;
	struct gpio_irq_chip *girq;
	struct gnr_gpio *priv;
	void __iomem *regs;
	int irq, ret;

	priv = devm_kzalloc(dev, struct_size(priv, pad_backup, num_backup_pins), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, gnr_gpio_irq, IRQF_SHARED | IRQF_NO_THREAD,
			       dev_name(dev), priv);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request interrupt\n");

	priv->reg_base = regs + readl(regs + GNR_CFG_BAR);

	gnr_gpio_init_pin_ro_bits(dev, priv->reg_base + GNR_CFG_LOCK_OFFSET,
				  priv->ro_bitmap);

	priv->gc	= gnr_gpio_chip;
	priv->gc.label	= dev_name(dev);
	priv->gc.parent	= dev;
	priv->gc.ngpio	= GNR_NUM_PINS;
	priv->gc.base	= -1;

	girq = &priv->gc.irq;
	gpio_irq_chip_set_chip(girq, &gnr_gpio_irq_chip);
	girq->chip->name	= dev_name(dev);
	girq->parent_handler	= NULL;
	girq->num_parents	= 0;
	girq->parents		= NULL;
	girq->default_type	= IRQ_TYPE_NONE;
	girq->handler		= handle_bad_irq;

	platform_set_drvdata(pdev, priv);

	return devm_gpiochip_add_data(dev, &priv->gc, priv);
}

static int gnr_gpio_suspend(struct device *dev)
{
	struct gnr_gpio *priv = dev_get_drvdata(dev);
	unsigned int i;

	guard(raw_spinlock_irqsave)(&priv->lock);

	for_each_clear_bit(i, priv->ro_bitmap, priv->gc.ngpio)
		priv->pad_backup[i] = readl(gnr_gpio_get_padcfg_addr(priv, i));

	return 0;
}

static int gnr_gpio_resume(struct device *dev)
{
	struct gnr_gpio *priv = dev_get_drvdata(dev);
	unsigned int i;

	guard(raw_spinlock_irqsave)(&priv->lock);

	for_each_clear_bit(i, priv->ro_bitmap, priv->gc.ngpio)
		writel(priv->pad_backup[i], gnr_gpio_get_padcfg_addr(priv, i));

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(gnr_gpio_pm_ops, gnr_gpio_suspend, gnr_gpio_resume);

static const struct acpi_device_id gnr_gpio_acpi_match[] = {
	{ "INTC1109" },
	{}
};
MODULE_DEVICE_TABLE(acpi, gnr_gpio_acpi_match);

static struct platform_driver gnr_gpio_driver = {
	.driver = {
		.name		  = "gpio-graniterapids",
		.pm		  = pm_sleep_ptr(&gnr_gpio_pm_ops),
		.acpi_match_table = gnr_gpio_acpi_match,
	},
	.probe = gnr_gpio_probe,
};
module_platform_driver(gnr_gpio_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Aapo Vienamo <aapo.vienamo@linux.intel.com>");
MODULE_DESCRIPTION("Intel Granite Rapids-D vGPIO driver");
