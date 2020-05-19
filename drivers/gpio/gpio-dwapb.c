/*
 * Copyright (c) 2011 Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
#include <linux/irqdomain.h>
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

struct dwapb_gpio_port {
	struct gpio_chip	gc;
	bool			is_registered;
	struct dwapb_gpio	*gpio;
#ifdef CONFIG_PM_SLEEP
	struct dwapb_context	*ctx;
#endif
	unsigned int		idx;
};

struct dwapb_gpio {
	struct	device		*dev;
	void __iomem		*regs;
	struct dwapb_gpio_port	*ports;
	unsigned int		nr_ports;
	struct irq_domain	*domain;
	unsigned int		flags;
	struct reset_control	*rst;
	struct clk		*clk;
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

static int dwapb_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct dwapb_gpio_port *port = gpiochip_get_data(gc);
	struct dwapb_gpio *gpio = port->gpio;

	return irq_find_mapping(gpio->domain, offset);
}

static struct dwapb_gpio_port *dwapb_offs_to_port(struct dwapb_gpio *gpio, unsigned int offs)
{
	struct dwapb_gpio_port *port;
	int i;

	for (i = 0; i < gpio->nr_ports; i++) {
		port = &gpio->ports[i];
		if (port->idx == offs / 32)
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
	val = gc->get(gc, offs % 32);
	if (val)
		pol &= ~BIT(offs);
	else
		pol |= BIT(offs);

	dwapb_write(gpio, GPIO_INT_POLARITY, pol);
}

static u32 dwapb_do_irq(struct dwapb_gpio *gpio)
{
	u32 irq_status = dwapb_read(gpio, GPIO_INTSTATUS);
	u32 ret = irq_status;

	while (irq_status) {
		int hwirq = fls(irq_status) - 1;
		int gpio_irq = irq_find_mapping(gpio->domain, hwirq);

		generic_handle_irq(gpio_irq);
		irq_status &= ~BIT(hwirq);

		if ((irq_get_trigger_type(gpio_irq) & IRQ_TYPE_SENSE_MASK)
			== IRQ_TYPE_EDGE_BOTH)
			dwapb_toggle_trigger(gpio, hwirq);
	}

	return ret;
}

static void dwapb_irq_handler(struct irq_desc *desc)
{
	struct dwapb_gpio *gpio = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);

	dwapb_do_irq(gpio);

	if (chip->irq_eoi)
		chip->irq_eoi(irq_desc_get_irq_data(desc));
}

static void dwapb_irq_enable(struct irq_data *d)
{
	struct irq_chip_generic *igc = irq_data_get_irq_chip_data(d);
	struct dwapb_gpio *gpio = igc->private;
	struct gpio_chip *gc = &gpio->ports[0].gc;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&gc->bgpio_lock, flags);
	val = dwapb_read(gpio, GPIO_INTEN);
	val |= BIT(d->hwirq);
	dwapb_write(gpio, GPIO_INTEN, val);
	spin_unlock_irqrestore(&gc->bgpio_lock, flags);
}

static void dwapb_irq_disable(struct irq_data *d)
{
	struct irq_chip_generic *igc = irq_data_get_irq_chip_data(d);
	struct dwapb_gpio *gpio = igc->private;
	struct gpio_chip *gc = &gpio->ports[0].gc;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&gc->bgpio_lock, flags);
	val = dwapb_read(gpio, GPIO_INTEN);
	val &= ~BIT(d->hwirq);
	dwapb_write(gpio, GPIO_INTEN, val);
	spin_unlock_irqrestore(&gc->bgpio_lock, flags);
}

static int dwapb_irq_reqres(struct irq_data *d)
{
	struct irq_chip_generic *igc = irq_data_get_irq_chip_data(d);
	struct dwapb_gpio *gpio = igc->private;
	struct gpio_chip *gc = &gpio->ports[0].gc;
	int ret;

	ret = gpiochip_lock_as_irq(gc, irqd_to_hwirq(d));
	if (ret) {
		dev_err(gpio->dev, "unable to lock HW IRQ %lu for IRQ\n",
			irqd_to_hwirq(d));
		return ret;
	}
	return 0;
}

static void dwapb_irq_relres(struct irq_data *d)
{
	struct irq_chip_generic *igc = irq_data_get_irq_chip_data(d);
	struct dwapb_gpio *gpio = igc->private;
	struct gpio_chip *gc = &gpio->ports[0].gc;

	gpiochip_unlock_as_irq(gc, irqd_to_hwirq(d));
}

static int dwapb_irq_set_type(struct irq_data *d, u32 type)
{
	struct irq_chip_generic *igc = irq_data_get_irq_chip_data(d);
	struct dwapb_gpio *gpio = igc->private;
	struct gpio_chip *gc = &gpio->ports[0].gc;
	int bit = d->hwirq;
	unsigned long level, polarity, flags;

	if (type & ~(IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING |
		     IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW))
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

	irq_setup_alt_chip(d, type);

	dwapb_write(gpio, GPIO_INTTYPE_LEVEL, level);
	if (type != IRQ_TYPE_EDGE_BOTH)
		dwapb_write(gpio, GPIO_INT_POLARITY, polarity);
	spin_unlock_irqrestore(&gc->bgpio_lock, flags);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int dwapb_irq_set_wake(struct irq_data *d, unsigned int enable)
{
	struct irq_chip_generic *igc = irq_data_get_irq_chip_data(d);
	struct dwapb_gpio *gpio = igc->private;
	struct dwapb_context *ctx = gpio->ports[0].ctx;

	if (enable)
		ctx->wake_en |= BIT(d->hwirq);
	else
		ctx->wake_en &= ~BIT(d->hwirq);

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
		dwapb_write(gpio, GPIO_PORTA_DEBOUNCE, val_deb | mask);
	else
		dwapb_write(gpio, GPIO_PORTA_DEBOUNCE, val_deb & ~mask);

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

static irqreturn_t dwapb_irq_handler_mfd(int irq, void *dev_id)
{
	u32 worked;
	struct dwapb_gpio *gpio = dev_id;

	worked = dwapb_do_irq(gpio);

	return worked ? IRQ_HANDLED : IRQ_NONE;
}

static void dwapb_configure_irqs(struct dwapb_gpio *gpio,
				 struct dwapb_gpio_port *port,
				 struct dwapb_port_property *pp)
{
	struct gpio_chip *gc = &port->gc;
	struct fwnode_handle  *fwnode = pp->fwnode;
	struct irq_chip_generic	*irq_gc = NULL;
	unsigned int hwirq, ngpio = gc->ngpio;
	struct irq_chip_type *ct;
	int err, i;

	gpio->domain = irq_domain_create_linear(fwnode, ngpio,
						 &irq_generic_chip_ops, gpio);
	if (!gpio->domain)
		return;

	err = irq_alloc_domain_generic_chips(gpio->domain, ngpio, 2,
					     "gpio-dwapb", handle_level_irq,
					     IRQ_NOREQUEST, 0,
					     IRQ_GC_INIT_NESTED_LOCK);
	if (err) {
		dev_info(gpio->dev, "irq_alloc_domain_generic_chips failed\n");
		irq_domain_remove(gpio->domain);
		gpio->domain = NULL;
		return;
	}

	irq_gc = irq_get_domain_generic_chip(gpio->domain, 0);
	if (!irq_gc) {
		irq_domain_remove(gpio->domain);
		gpio->domain = NULL;
		return;
	}

	irq_gc->reg_base = gpio->regs;
	irq_gc->private = gpio;

	for (i = 0; i < 2; i++) {
		ct = &irq_gc->chip_types[i];
		ct->chip.irq_ack = irq_gc_ack_set_bit;
		ct->chip.irq_mask = irq_gc_mask_set_bit;
		ct->chip.irq_unmask = irq_gc_mask_clr_bit;
		ct->chip.irq_set_type = dwapb_irq_set_type;
		ct->chip.irq_enable = dwapb_irq_enable;
		ct->chip.irq_disable = dwapb_irq_disable;
		ct->chip.irq_request_resources = dwapb_irq_reqres;
		ct->chip.irq_release_resources = dwapb_irq_relres;
#ifdef CONFIG_PM_SLEEP
		ct->chip.irq_set_wake = dwapb_irq_set_wake;
#endif
		ct->regs.ack = gpio_reg_convert(gpio, GPIO_PORTA_EOI);
		ct->regs.mask = gpio_reg_convert(gpio, GPIO_INTMASK);
		ct->type = IRQ_TYPE_LEVEL_MASK;
	}

	irq_gc->chip_types[0].type = IRQ_TYPE_LEVEL_MASK;
	irq_gc->chip_types[1].type = IRQ_TYPE_EDGE_BOTH;
	irq_gc->chip_types[1].handler = handle_edge_irq;

	if (!pp->irq_shared) {
		int i;

		for (i = 0; i < pp->ngpio; i++) {
			if (pp->irq[i] >= 0)
				irq_set_chained_handler_and_data(pp->irq[i],
						dwapb_irq_handler, gpio);
		}
	} else {
		/*
		 * Request a shared IRQ since where MFD would have devices
		 * using the same irq pin
		 */
		err = devm_request_irq(gpio->dev, pp->irq[0],
				       dwapb_irq_handler_mfd,
				       IRQF_SHARED, "gpio-dwapb-mfd", gpio);
		if (err) {
			dev_err(gpio->dev, "error requesting IRQ\n");
			irq_domain_remove(gpio->domain);
			gpio->domain = NULL;
			return;
		}
	}

	for (hwirq = 0 ; hwirq < ngpio ; hwirq++)
		irq_create_mapping(gpio->domain, hwirq);

	port->gc.to_irq = dwapb_gpio_to_irq;
}

static void dwapb_irq_teardown(struct dwapb_gpio *gpio)
{
	struct dwapb_gpio_port *port = &gpio->ports[0];
	struct gpio_chip *gc = &port->gc;
	unsigned int ngpio = gc->ngpio;
	irq_hw_number_t hwirq;

	if (!gpio->domain)
		return;

	for (hwirq = 0 ; hwirq < ngpio ; hwirq++)
		irq_dispose_mapping(irq_find_mapping(gpio->domain, hwirq));

	irq_domain_remove(gpio->domain);
	gpio->domain = NULL;
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

	dat = gpio->regs + GPIO_EXT_PORTA + (pp->idx * GPIO_EXT_PORT_STRIDE);
	set = gpio->regs + GPIO_SWPORTA_DR + (pp->idx * GPIO_SWPORT_DR_STRIDE);
	dirout = gpio->regs + GPIO_SWPORTA_DDR +
		(pp->idx * GPIO_SWPORT_DDR_STRIDE);

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

	if (pp->has_irq)
		dwapb_configure_irqs(gpio, port, pp);

	err = gpiochip_add_data(&port->gc, port);
	if (err) {
		dev_err(gpio->dev, "failed to register gpiochip for port%d\n",
			port->idx);
		return err;
	}

	/* Add GPIO-signaled ACPI event support */
	acpi_gpiochip_request_interrupts(&port->gc);

	port->is_registered = true;

	return 0;
}

static void dwapb_gpio_unregister(struct dwapb_gpio *gpio)
{
	unsigned int m;

	for (m = 0; m < gpio->nr_ports; ++m) {
		struct dwapb_gpio_port *port = &gpio->ports[m];

		if (!port->is_registered)
			continue;

		acpi_gpiochip_free_interrupts(&port->gc);
		gpiochip_remove(&port->gc);
	}
}

static struct dwapb_platform_data *
dwapb_gpio_get_pdata(struct device *dev)
{
	struct fwnode_handle *fwnode;
	struct dwapb_platform_data *pdata;
	struct dwapb_port_property *pp;
	int nports;
	int i, j;

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
		struct device_node *np = NULL;

		pp = &pdata->properties[i++];
		pp->fwnode = fwnode;

		if (fwnode_property_read_u32(fwnode, "reg", &pp->idx) ||
		    pp->idx >= DWAPB_MAX_PORTS) {
			dev_err(dev,
				"missing/invalid port index for port%d\n", i);
			fwnode_handle_put(fwnode);
			return ERR_PTR(-EINVAL);
		}

		if (fwnode_property_read_u32(fwnode, "snps,nr-gpios",
					 &pp->ngpio)) {
			dev_info(dev,
				 "failed to get number of gpios for port%d\n",
				 i);
			pp->ngpio = 32;
		}

		pp->irq_shared	= false;
		pp->gpio_base	= -1;

		/*
		 * Only port A can provide interrupts in all configurations of
		 * the IP.
		 */
		if (pp->idx != 0)
			continue;

		if (dev->of_node && fwnode_property_read_bool(fwnode,
						  "interrupt-controller")) {
			np = to_of_node(fwnode);
		}

		for (j = 0; j < pp->ngpio; j++) {
			pp->irq[j] = -ENXIO;

			if (np)
				pp->irq[j] = of_irq_get(np, j);
			else if (has_acpi_companion(dev))
				pp->irq[j] = platform_get_irq(to_platform_device(dev), j);

			if (pp->irq[j] >= 0)
				pp->has_irq = true;
		}

		if (!pp->has_irq)
			dev_warn(dev, "no irq for port%d\n", pp->idx);
	}

	return pdata;
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
	struct resource *res;
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

	gpio->rst = devm_reset_control_get_optional_shared(dev, NULL);
	if (IS_ERR(gpio->rst))
		return PTR_ERR(gpio->rst);

	reset_control_deassert(gpio->rst);

	gpio->ports = devm_kcalloc(&pdev->dev, gpio->nr_ports,
				   sizeof(*gpio->ports), GFP_KERNEL);
	if (!gpio->ports)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	gpio->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(gpio->regs))
		return PTR_ERR(gpio->regs);

	/* Optional bus clock */
	gpio->clk = devm_clk_get(&pdev->dev, "bus");
	if (!IS_ERR(gpio->clk)) {
		err = clk_prepare_enable(gpio->clk);
		if (err) {
			dev_info(&pdev->dev, "Cannot enable clock\n");
			return err;
		}
	}

	gpio->flags = 0;
	if (dev->of_node) {
		gpio->flags = (uintptr_t)of_device_get_match_data(dev);
	} else if (has_acpi_companion(dev)) {
		const struct acpi_device_id *acpi_id;

		acpi_id = acpi_match_device(dwapb_acpi_match, dev);
		if (acpi_id) {
			if (acpi_id->driver_data)
				gpio->flags = acpi_id->driver_data;
		}
	}

	for (i = 0; i < gpio->nr_ports; i++) {
		err = dwapb_gpio_add_port(gpio, &pdata->properties[i], i);
		if (err)
			goto out_unregister;
	}
	platform_set_drvdata(pdev, gpio);

	return 0;

out_unregister:
	dwapb_gpio_unregister(gpio);
	dwapb_irq_teardown(gpio);
	clk_disable_unprepare(gpio->clk);

	return err;
}

static int dwapb_gpio_remove(struct platform_device *pdev)
{
	struct dwapb_gpio *gpio = platform_get_drvdata(pdev);

	dwapb_gpio_unregister(gpio);
	dwapb_irq_teardown(gpio);
	reset_control_assert(gpio->rst);
	clk_disable_unprepare(gpio->clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int dwapb_gpio_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dwapb_gpio *gpio = platform_get_drvdata(pdev);
	struct gpio_chip *gc	= &gpio->ports[0].gc;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&gc->bgpio_lock, flags);
	for (i = 0; i < gpio->nr_ports; i++) {
		unsigned int offset;
		unsigned int idx = gpio->ports[i].idx;
		struct dwapb_context *ctx = gpio->ports[i].ctx;

		BUG_ON(!ctx);

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
			dwapb_write(gpio, GPIO_INTMASK,
				    0xffffffff & ~ctx->wake_en);
		}
	}
	spin_unlock_irqrestore(&gc->bgpio_lock, flags);

	clk_disable_unprepare(gpio->clk);

	return 0;
}

static int dwapb_gpio_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dwapb_gpio *gpio = platform_get_drvdata(pdev);
	struct gpio_chip *gc	= &gpio->ports[0].gc;
	unsigned long flags;
	int i;

	if (!IS_ERR(gpio->clk))
		clk_prepare_enable(gpio->clk);

	spin_lock_irqsave(&gc->bgpio_lock, flags);
	for (i = 0; i < gpio->nr_ports; i++) {
		unsigned int offset;
		unsigned int idx = gpio->ports[i].idx;
		struct dwapb_context *ctx = gpio->ports[i].ctx;

		BUG_ON(!ctx);

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
		.name	= "gpio-dwapb",
		.pm	= &dwapb_gpio_pm_ops,
		.of_match_table = of_match_ptr(dwapb_of_match),
		.acpi_match_table = ACPI_PTR(dwapb_acpi_match),
	},
	.probe		= dwapb_gpio_probe,
	.remove		= dwapb_gpio_remove,
};

module_platform_driver(dwapb_gpio_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jamie Iles");
MODULE_DESCRIPTION("Synopsys DesignWare APB GPIO driver");
