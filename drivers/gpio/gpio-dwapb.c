// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011 Jamie Iles
 *
 * All enquiries to support@picochip.com
 */
#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/reset.h>
#include <linux/spinlock.h>
#include <linux/platform_data/gpio-dwapb.h>
#include <linux/slab.h>

#include "gpiolib.h"
#include "gpiolib-acpi.h"

#define GPIO_SWPORTA_DR		0x00
#define GPIO_SWPORTA_DDR	0x04
#define GPIO_SWPORTB_DR		0x0c
#define GPIO_SWPORTB_DDR	0x10
#define GPIO_SWPORTC_DR		0x18
#define GPIO_SWPORTC_DDR	0x1c
#define GPIO_SWPORTD_DR		0x24
#define GPIO_SWPORTD_DDR	0x28
#define GPIO_INTEN		0x30
#define GPIO_INTMASK		0x34
#define GPIO_INTTYPE_LEVEL	0x38
#define GPIO_INT_POLARITY	0x3c
#define GPIO_INTSTATUS		0x40
#define GPIO_PORTA_DEBOUNCE	0x48
#define GPIO_PORTA_EOI		0x4c
#define GPIO_EXT_PORTA		0x50
#define GPIO_EXT_PORTB		0x54
#define GPIO_EXT_PORTC		0x58
#define GPIO_EXT_PORTD		0x5c

#define DWAPB_DRIVER_NAME	"gpio-dwapb"
#define DWAPB_MAX_PORTS		4

#define GPIO_EXT_PORT_STRIDE	0x04 /* register stride 32 bits */
#define GPIO_SWPORT_DR_STRIDE	0x0c /* register stride 3*32 bits */
#define GPIO_SWPORT_DDR_STRIDE	0x0c /* register stride 3*32 bits */

#define GPIO_REG_OFFSET_V2	1

#define GPIO_INTMASK_V2		0x44
#define GPIO_INTTYPE_LEVEL_V2	0x34
#define GPIO_INT_POLARITY_V2	0x38
#define GPIO_INTSTATUS_V2	0x3c
#define GPIO_PORTA_EOI_V2	0x40

#define DWAPB_NR_CLOCKS		2

struct dwapb_gpio;

#ifdef CONFIG_PM_SLEEP
/* Store GPIO context across system-wide suspend/resume transitions */
struct dwapb_context {
	u32 data;
	u32 dir;
	u32 ext;
	u32 int_en;
	u32 int_mask;
	u32 int_type;
	u32 int_pol;
	u32 int_deb;
	u32 wake_en;
};
#endif

struct dwapb_gpio_port_irqchip {
	struct irq_chip		irqchip;
	unsigned int		nr_irqs;
	unsigned int		irq[DWAPB_MAX_GPIOS];
};

struct dwapb_gpio_port {
	struct gpio_chip	gc;
	struct dwapb_gpio_port_irqchip *pirq;
	struct dwapb_gpio	*gpio;
#ifdef CONFIG_PM_SLEEP
	struct dwapb_context	*ctx;
#endif
	unsigned int		idx;
};
#define to_dwapb_gpio(_gc) \
	(container_of(_gc, struct dwapb_gpio_port, gc)->gpio)

struct dwapb_gpio {
	struct	device		*dev;
	void __iomem		*regs;
	struct dwapb_gpio_port	*ports;
	unsigned int		nr_ports;
	unsigned int		flags;
	struct reset_control	*rst;
	struct clk_bulk_data	clks[DWAPB_NR_CLOCKS];
};

static inline u32 gpio_reg_v2_convert(unsigned int offset)
{
	switch (offset) {
	case GPIO_INTMASK:
		return GPIO_INTMASK_V2;
	case GPIO_INTTYPE_LEVEL:
		return GPIO_INTTYPE_LEVEL_V2;
	case GPIO_INT_POLARITY:
		return GPIO_INT_POLARITY_V2;
	case GPIO_INTSTATUS:
		return GPIO_INTSTATUS_V2;
	case GPIO_PORTA_EOI:
		return GPIO_PORTA_EOI_V2;
	}

	return offset;
}

static inline u32 gpio_reg_convert(struct dwapb_gpio *gpio, unsigned int offset)
{
	if (gpio->flags & GPIO_REG_OFFSET_V2)
		return gpio_reg_v2_convert(offset);

	return offset;
}

static inline u32 dwapb_read(struct dwapb_gpio *gpio, unsigned int offset)
{
	struct gpio_chip *gc	= &gpio->ports[0].gc;
	void __iomem *reg_base	= gpio->regs;

	return gc->read_reg(reg_base + gpio_reg_convert(gpio, offset));
}

static inline void dwapb_write(struct dwapb_gpio *gpio, unsigned int offset,
			       u32 val)
{
	struct gpio_chip *gc	= &gpio->ports[0].gc;
	void __iomem *reg_base	= gpio->regs;

	gc->write_reg(reg_base + gpio_reg_convert(gpio, offset), val);
}

static struct dwapb_gpio_port *dwapb_offs_to_port(struct dwapb_gpio *gpio, unsigned int offs)
{
	struct dwapb_gpio_port *port;
	int i;

	for (i = 0; i < gpio->nr_ports; i++) {
		port = &gpio->ports[i];
		if (port->idx == offs / DWAPB_MAX_GPIOS)
			return port;
	}

	return NULL;
}

static void dwapb_toggle_trigger(struct dwapb_gpio *gpio, unsigned int offs)
{
	struct dwapb_gpio_port *port = dwapb_offs_to_port(gpio, offs);
	struct gpio_chip *gc;
	u32 pol;
	int val;

	if (!port)
		return;
	gc = &port->gc;

	pol = dwapb_read(gpio, GPIO_INT_POLARITY);
	/* Just read the current value right out of the data register */
	val = gc->get(gc, offs % DWAPB_MAX_GPIOS);
	if (val)
		pol &= ~BIT(offs);
	else
		pol |= BIT(offs);

	dwapb_write(gpio, GPIO_INT_POLARITY, pol);
}

static u32 dwapb_do_irq(struct dwapb_gpio *gpio)
{
	struct gpio_chip *gc = &gpio->ports[0].gc;
	unsigned long irq_status;
	irq_hw_number_t hwirq;

	irq_status = dwapb_read(gpio, GPIO_INTSTATUS);
	for_each_set_bit(hwirq, &irq_status, DWAPB_MAX_GPIOS) {
		int gpio_irq = irq_find_mapping(gc->irq.domain, hwirq);
		u32 irq_type = irq_get_trigger_type(gpio_irq);

		generic_handle_irq(gpio_irq);

		if ((irq_type & IRQ_TYPE_SENSE_MASK) == IRQ_TYPE_EDGE_BOTH)
			dwapb_toggle_trigger(gpio, hwirq);
	}

	return irq_status;
}

static void dwapb_irq_handler(struct irq_desc *desc)
{
	struct dwapb_gpio *gpio = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);

	chained_irq_enter(chip, desc);
	dwapb_do_irq(gpio);
	chained_irq_exit(chip, desc);
}

static irqreturn_t dwapb_irq_handler_mfd(int irq, void *dev_id)
{
	return IRQ_RETVAL(dwapb_do_irq(dev_id));
}

static void dwapb_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct dwapb_gpio *gpio = to_dwapb_gpio(gc);
	u32 val = BIT(irqd_to_hwirq(d));
	unsigned long flags;

	spin_lock_irqsave(&gc->bgpio_lock, flags);
	dwapb_write(gpio, GPIO_PORTA_EOI, val);
	spin_unlock_irqrestore(&gc->bgpio_lock, flags);
}

static void dwapb_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct dwapb_gpio *gpio = to_dwapb_gpio(gc);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&gc->bgpio_lock, flags);
	val = dwapb_read(gpio, GPIO_INTMASK) | BIT(irqd_to_hwirq(d));
	dwapb_write(gpio, GPIO_INTMASK, val);
	spin_unlock_irqrestore(&gc->bgpio_lock, flags);
}

static void dwapb_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct dwapb_gpio *gpio = to_dwapb_gpio(gc);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&gc->bgpio_lock, flags);
	val = dwapb_read(gpio, GPIO_INTMASK) & ~BIT(irqd_to_hwirq(d));
	dwapb_write(gpio, GPIO_INTMASK, val);
	spin_unlock_irqrestore(&gc->bgpio_lock, flags);
}

static void dwapb_irq_enable(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct dwapb_gpio *gpio = to_dwapb_gpio(gc);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&gc->bgpio_lock, flags);
	val = dwapb_read(gpio, GPIO_INTEN);
	val |= BIT(irqd_to_hwirq(d));
	dwapb_write(gpio, GPIO_INTEN, val);
	spin_unlock_irqrestore(&gc->bgpio_lock, flags);
}

static void dwapb_irq_disable(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct dwapb_gpio *gpio = to_dwapb_gpio(gc);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&gc->bgpio_lock, flags);
	val = dwapb_read(gpio, GPIO_INTEN);
	val &= ~BIT(irqd_to_hwirq(d));
	dwapb_write(gpio, GPIO_INTEN, val);
	spin_unlock_irqrestore(&gc->bgpio_lock, flags);
}

static int dwapb_irq_set_type(struct irq_data *d, u32 type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct dwapb_gpio *gpio = to_dwapb_gpio(gc);
	irq_hw_number_t bit = irqd_to_hwirq(d);
	unsigned long level, polarity, flags;

	if (type & ~IRQ_TYPE_SENSE_MASK)
		return -EINVAL;

	spin_lock_irqsave(&gc->bgpio_lock, flags);
	level = dwapb_read(gpio, GPIO_INTTYPE_LEVEL);
	polarity = dwapb_read(gpio, GPIO_INT_POLARITY);

	switch (type) {
	case IRQ_TYPE_EDGE_BOTH:
		level |= BIT(bit);
		dwapb_toggle_trigger(gpio, bit);
		break;
	case IRQ_TYPE_EDGE_RISING:
		level |= BIT(bit);
		polarity |= BIT(bit);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		level |= BIT(bit);
		polarity &= ~BIT(bit);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		level &= ~BIT(bit);
		polarity |= BIT(bit);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		level &= ~BIT(bit);
		polarity &= ~BIT(bit);
		break;
	}

	if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(d, handle_level_irq);
	else if (type & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(d, handle_edge_irq);

	dwapb_write(gpio, GPIO_INTTYPE_LEVEL, level);
	if (type != IRQ_TYPE_EDGE_BOTH)
		dwapb_write(gpio, GPIO_INT_POLARITY, polarity);
	spin_unlock_irqrestore(&gc->bgpio_lock, flags);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int dwapb_irq_set_wake(struct irq_data *d, unsigned int enable)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct dwapb_gpio *gpio = to_dwapb_gpio(gc);
	struct dwapb_context *ctx = gpio->ports[0].ctx;
	irq_hw_number_t bit = irqd_to_hwirq(d);

	if (enable)
		ctx->wake_en |= BIT(bit);
	else
		ctx->wake_en &= ~BIT(bit);

	return 0;
}
#endif

static int dwapb_gpio_set_debounce(struct gpio_chip *gc,
				   unsigned offset, unsigned debounce)
{
	struct dwapb_gpio_port *port = gpiochip_get_data(gc);
	struct dwapb_gpio *gpio = port->gpio;
	unsigned long flags, val_deb;
	unsigned long mask = BIT(offset);

	spin_lock_irqsave(&gc->bgpio_lock, flags);

	val_deb = dwapb_read(gpio, GPIO_PORTA_DEBOUNCE);
	if (debounce)
		val_deb |= mask;
	else
		val_deb &= ~mask;
	dwapb_write(gpio, GPIO_PORTA_DEBOUNCE, val_deb);

	spin_unlock_irqrestore(&gc->bgpio_lock, flags);

	return 0;
}

static int dwapb_gpio_set_config(struct gpio_chip *gc, unsigned offset,
				 unsigned long config)
{
	u32 debounce;

	if (pinconf_to_config_param(config) != PIN_CONFIG_INPUT_DEBOUNCE)
		return -ENOTSUPP;

	debounce = pinconf_to_config_argument(config);
	return dwapb_gpio_set_debounce(gc, offset, debounce);
}

static int dwapb_convert_irqs(struct dwapb_gpio_port_irqchip *pirq,
			      struct dwapb_port_property *pp)
{
	int i;

	/* Group all available IRQs into an array of parental IRQs. */
	for (i = 0; i < pp->ngpio; ++i) {
		if (!pp->irq[i])
			continue;

		pirq->irq[pirq->nr_irqs++] = pp->irq[i];
	}

	return pirq->nr_irqs ? 0 : -ENOENT;
}

static void dwapb_configure_irqs(struct dwapb_gpio *gpio,
				 struct dwapb_gpio_port *port,
				 struct dwapb_port_property *pp)
{
	struct dwapb_gpio_port_irqchip *pirq;
	struct gpio_chip *gc = &port->gc;
	struct gpio_irq_chip *girq;
	int err;

	pirq = devm_kzalloc(gpio->dev, sizeof(*pirq), GFP_KERNEL);
	if (!pirq)
		return;

	if (dwapb_convert_irqs(pirq, pp)) {
		dev_warn(gpio->dev, "no IRQ for port%d\n", pp->idx);
		goto err_kfree_pirq;
	}

	girq = &gc->irq;
	girq->handler = handle_bad_irq;
	girq->default_type = IRQ_TYPE_NONE;

	port->pirq = pirq;
	pirq->irqchip.name = DWAPB_DRIVER_NAME;
	pirq->irqchip.irq_ack = dwapb_irq_ack;
	pirq->irqchip.irq_mask = dwapb_irq_mask;
	pirq->irqchip.irq_unmask = dwapb_irq_unmask;
	pirq->irqchip.irq_set_type = dwapb_irq_set_type;
	pirq->irqchip.irq_enable = dwapb_irq_enable;
	pirq->irqchip.irq_disable = dwapb_irq_disable;
#ifdef CONFIG_PM_SLEEP
	pirq->irqchip.irq_set_wake = dwapb_irq_set_wake;
#endif

	if (!pp->irq_shared) {
		girq->num_parents = pirq->nr_irqs;
		girq->parents = pirq->irq;
		girq->parent_handler_data = gpio;
		girq->parent_handler = dwapb_irq_handler;
	} else {
		/* This will let us handle the parent IRQ in the driver */
		girq->num_parents = 0;
		girq->parents = NULL;
		girq->parent_handler = NULL;

		/*
		 * Request a shared IRQ since where MFD would have devices
		 * using the same irq pin
		 */
		err = devm_request_irq(gpio->dev, pp->irq[0],
				       dwapb_irq_handler_mfd,
				       IRQF_SHARED, DWAPB_DRIVER_NAME, gpio);
		if (err) {
			dev_err(gpio->dev, "error requesting IRQ\n");
			goto err_kfree_pirq;
		}
	}

	girq->chip = &pirq->irqchip;

	return;

err_kfree_pirq:
	devm_kfree(gpio->dev, pirq);
}

static int dwapb_gpio_add_port(struct dwapb_gpio *gpio,
			       struct dwapb_port_property *pp,
			       unsigned int offs)
{
	struct dwapb_gpio_port *port;
	void __iomem *dat, *set, *dirout;
	int err;

	port = &gpio->ports[offs];
	port->gpio = gpio;
	port->idx = pp->idx;

#ifdef CONFIG_PM_SLEEP
	port->ctx = devm_kzalloc(gpio->dev, sizeof(*port->ctx), GFP_KERNEL);
	if (!port->ctx)
		return -ENOMEM;
#endif

	dat = gpio->regs + GPIO_EXT_PORTA + pp->idx * GPIO_EXT_PORT_STRIDE;
	set = gpio->regs + GPIO_SWPORTA_DR + pp->idx * GPIO_SWPORT_DR_STRIDE;
	dirout = gpio->regs + GPIO_SWPORTA_DDR + pp->idx * GPIO_SWPORT_DDR_STRIDE;

	/* This registers 32 GPIO lines per port */
	err = bgpio_init(&port->gc, gpio->dev, 4, dat, set, NULL, dirout,
			 NULL, 0);
	if (err) {
		dev_err(gpio->dev, "failed to init gpio chip for port%d\n",
			port->idx);
		return err;
	}

#ifdef CONFIG_OF_GPIO
	port->gc.of_node = to_of_node(pp->fwnode);
#endif
	port->gc.ngpio = pp->ngpio;
	port->gc.base = pp->gpio_base;

	/* Only port A support debounce */
	if (pp->idx == 0)
		port->gc.set_config = dwapb_gpio_set_config;

	/* Only port A can provide interrupts in all configurations of the IP */
	if (pp->idx == 0)
		dwapb_configure_irqs(gpio, port, pp);

	err = devm_gpiochip_add_data(gpio->dev, &port->gc, port);
	if (err) {
		dev_err(gpio->dev, "failed to register gpiochip for port%d\n",
			port->idx);
		return err;
	}

	return 0;
}

static void dwapb_get_irq(struct device *dev, struct fwnode_handle *fwnode,
			  struct dwapb_port_property *pp)
{
	struct device_node *np = NULL;
	int irq = -ENXIO, j;

	if (fwnode_property_read_bool(fwnode, "interrupt-controller"))
		np = to_of_node(fwnode);

	for (j = 0; j < pp->ngpio; j++) {
		if (np)
			irq = of_irq_get(np, j);
		else if (has_acpi_companion(dev))
			irq = platform_get_irq_optional(to_platform_device(dev), j);
		if (irq > 0)
			pp->irq[j] = irq;
	}
}

static struct dwapb_platform_data *dwapb_gpio_get_pdata(struct device *dev)
{
	struct fwnode_handle *fwnode;
	struct dwapb_platform_data *pdata;
	struct dwapb_port_property *pp;
	int nports;
	int i;

	nports = device_get_child_node_count(dev);
	if (nports == 0)
		return ERR_PTR(-ENODEV);

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	pdata->properties = devm_kcalloc(dev, nports, sizeof(*pp), GFP_KERNEL);
	if (!pdata->properties)
		return ERR_PTR(-ENOMEM);

	pdata->nports = nports;

	i = 0;
	device_for_each_child_node(dev, fwnode)  {
		pp = &pdata->properties[i++];
		pp->fwnode = fwnode;

		if (fwnode_property_read_u32(fwnode, "reg", &pp->idx) ||
		    pp->idx >= DWAPB_MAX_PORTS) {
			dev_err(dev,
				"missing/invalid port index for port%d\n", i);
			fwnode_handle_put(fwnode);
			return ERR_PTR(-EINVAL);
		}

		if (fwnode_property_read_u32(fwnode, "ngpios", &pp->ngpio) &&
		    fwnode_property_read_u32(fwnode, "snps,nr-gpios", &pp->ngpio)) {
			dev_info(dev,
				 "failed to get number of gpios for port%d\n",
				 i);
			pp->ngpio = DWAPB_MAX_GPIOS;
		}

		pp->irq_shared	= false;
		pp->gpio_base	= -1;

		/*
		 * Only port A can provide interrupts in all configurations of
		 * the IP.
		 */
		if (pp->idx == 0)
			dwapb_get_irq(dev, fwnode, pp);
	}

	return pdata;
}

static void dwapb_assert_reset(void *data)
{
	struct dwapb_gpio *gpio = data;

	reset_control_assert(gpio->rst);
}

static int dwapb_get_reset(struct dwapb_gpio *gpio)
{
	int err;

	gpio->rst = devm_reset_control_get_optional_shared(gpio->dev, NULL);
	if (IS_ERR(gpio->rst)) {
		dev_err(gpio->dev, "Cannot get reset descriptor\n");
		return PTR_ERR(gpio->rst);
	}

	err = reset_control_deassert(gpio->rst);
	if (err) {
		dev_err(gpio->dev, "Cannot deassert reset lane\n");
		return err;
	}

	return devm_add_action_or_reset(gpio->dev, dwapb_assert_reset, gpio);
}

static void dwapb_disable_clks(void *data)
{
	struct dwapb_gpio *gpio = data;

	clk_bulk_disable_unprepare(DWAPB_NR_CLOCKS, gpio->clks);
}

static int dwapb_get_clks(struct dwapb_gpio *gpio)
{
	int err;

	/* Optional bus and debounce clocks */
	gpio->clks[0].id = "bus";
	gpio->clks[1].id = "db";
	err = devm_clk_bulk_get_optional(gpio->dev, DWAPB_NR_CLOCKS,
					 gpio->clks);
	if (err)
		return dev_err_probe(gpio->dev, err,
				     "Cannot get APB/Debounce clocks\n");

	err = clk_bulk_prepare_enable(DWAPB_NR_CLOCKS, gpio->clks);
	if (err) {
		dev_err(gpio->dev, "Cannot enable APB/Debounce clocks\n");
		return err;
	}

	return devm_add_action_or_reset(gpio->dev, dwapb_disable_clks, gpio);
}

static const struct of_device_id dwapb_of_match[] = {
	{ .compatible = "snps,dw-apb-gpio", .data = (void *)0},
	{ .compatible = "apm,xgene-gpio-v2", .data = (void *)GPIO_REG_OFFSET_V2},
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, dwapb_of_match);

static const struct acpi_device_id dwapb_acpi_match[] = {
	{"HISI0181", 0},
	{"APMC0D07", 0},
	{"APMC0D81", GPIO_REG_OFFSET_V2},
	{ }
};
MODULE_DEVICE_TABLE(acpi, dwapb_acpi_match);

static int dwapb_gpio_probe(struct platform_device *pdev)
{
	unsigned int i;
	struct dwapb_gpio *gpio;
	int err;
	struct device *dev = &pdev->dev;
	struct dwapb_platform_data *pdata = dev_get_platdata(dev);

	if (!pdata) {
		pdata = dwapb_gpio_get_pdata(dev);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
	}

	if (!pdata->nports)
		return -ENODEV;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->dev = &pdev->dev;
	gpio->nr_ports = pdata->nports;

	err = dwapb_get_reset(gpio);
	if (err)
		return err;

	gpio->ports = devm_kcalloc(&pdev->dev, gpio->nr_ports,
				   sizeof(*gpio->ports), GFP_KERNEL);
	if (!gpio->ports)
		return -ENOMEM;

	gpio->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gpio->regs))
		return PTR_ERR(gpio->regs);

	err = dwapb_get_clks(gpio);
	if (err)
		return err;

	gpio->flags = (uintptr_t)device_get_match_data(dev);

	for (i = 0; i < gpio->nr_ports; i++) {
		err = dwapb_gpio_add_port(gpio, &pdata->properties[i], i);
		if (err)
			return err;
	}

	platform_set_drvdata(pdev, gpio);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int dwapb_gpio_suspend(struct device *dev)
{
	struct dwapb_gpio *gpio = dev_get_drvdata(dev);
	struct gpio_chip *gc	= &gpio->ports[0].gc;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&gc->bgpio_lock, flags);
	for (i = 0; i < gpio->nr_ports; i++) {
		unsigned int offset;
		unsigned int idx = gpio->ports[i].idx;
		struct dwapb_context *ctx = gpio->ports[i].ctx;

		offset = GPIO_SWPORTA_DDR + idx * GPIO_SWPORT_DDR_STRIDE;
		ctx->dir = dwapb_read(gpio, offset);

		offset = GPIO_SWPORTA_DR + idx * GPIO_SWPORT_DR_STRIDE;
		ctx->data = dwapb_read(gpio, offset);

		offset = GPIO_EXT_PORTA + idx * GPIO_EXT_PORT_STRIDE;
		ctx->ext = dwapb_read(gpio, offset);

		/* Only port A can provide interrupts */
		if (idx == 0) {
			ctx->int_mask	= dwapb_read(gpio, GPIO_INTMASK);
			ctx->int_en	= dwapb_read(gpio, GPIO_INTEN);
			ctx->int_pol	= dwapb_read(gpio, GPIO_INT_POLARITY);
			ctx->int_type	= dwapb_read(gpio, GPIO_INTTYPE_LEVEL);
			ctx->int_deb	= dwapb_read(gpio, GPIO_PORTA_DEBOUNCE);

			/* Mask out interrupts */
			dwapb_write(gpio, GPIO_INTMASK, ~ctx->wake_en);
		}
	}
	spin_unlock_irqrestore(&gc->bgpio_lock, flags);

	clk_bulk_disable_unprepare(DWAPB_NR_CLOCKS, gpio->clks);

	return 0;
}

static int dwapb_gpio_resume(struct device *dev)
{
	struct dwapb_gpio *gpio = dev_get_drvdata(dev);
	struct gpio_chip *gc	= &gpio->ports[0].gc;
	unsigned long flags;
	int i, err;

	err = clk_bulk_prepare_enable(DWAPB_NR_CLOCKS, gpio->clks);
	if (err) {
		dev_err(gpio->dev, "Cannot reenable APB/Debounce clocks\n");
		return err;
	}

	spin_lock_irqsave(&gc->bgpio_lock, flags);
	for (i = 0; i < gpio->nr_ports; i++) {
		unsigned int offset;
		unsigned int idx = gpio->ports[i].idx;
		struct dwapb_context *ctx = gpio->ports[i].ctx;

		offset = GPIO_SWPORTA_DR + idx * GPIO_SWPORT_DR_STRIDE;
		dwapb_write(gpio, offset, ctx->data);

		offset = GPIO_SWPORTA_DDR + idx * GPIO_SWPORT_DDR_STRIDE;
		dwapb_write(gpio, offset, ctx->dir);

		offset = GPIO_EXT_PORTA + idx * GPIO_EXT_PORT_STRIDE;
		dwapb_write(gpio, offset, ctx->ext);

		/* Only port A can provide interrupts */
		if (idx == 0) {
			dwapb_write(gpio, GPIO_INTTYPE_LEVEL, ctx->int_type);
			dwapb_write(gpio, GPIO_INT_POLARITY, ctx->int_pol);
			dwapb_write(gpio, GPIO_PORTA_DEBOUNCE, ctx->int_deb);
			dwapb_write(gpio, GPIO_INTEN, ctx->int_en);
			dwapb_write(gpio, GPIO_INTMASK, ctx->int_mask);

			/* Clear out spurious interrupts */
			dwapb_write(gpio, GPIO_PORTA_EOI, 0xffffffff);
		}
	}
	spin_unlock_irqrestore(&gc->bgpio_lock, flags);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(dwapb_gpio_pm_ops, dwapb_gpio_suspend,
			 dwapb_gpio_resume);

static struct platform_driver dwapb_gpio_driver = {
	.driver		= {
		.name	= DWAPB_DRIVER_NAME,
		.pm	= &dwapb_gpio_pm_ops,
		.of_match_table = dwapb_of_match,
		.acpi_match_table = dwapb_acpi_match,
	},
	.probe		= dwapb_gpio_probe,
};

module_platform_driver(dwapb_gpio_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jamie Iles");
MODULE_DESCRIPTION("Synopsys DesignWare APB GPIO driver");
MODULE_ALIAS("platform:" DWAPB_DRIVER_NAME);
