/*
 * TI DaVinci GPIO Support
 *
 * Copyright (c) 2006-2007 David Brownell
 * Copyright (c) 2007, MontaVista Software, Inc. <source@mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/platform_data/gpio-davinci.h>
#include <linux/irqchip/chained_irq.h>

struct davinci_gpio_regs {
	u32	dir;
	u32	out_data;
	u32	set_data;
	u32	clr_data;
	u32	in_data;
	u32	set_rising;
	u32	clr_rising;
	u32	set_falling;
	u32	clr_falling;
	u32	intstat;
};

typedef struct irq_chip *(*gpio_get_irq_chip_cb_t)(unsigned int irq);

#define BINTEN	0x8 /* GPIO Interrupt Per-Bank Enable Register */
#define MAX_LABEL_SIZE 20

static void __iomem *gpio_base;
static unsigned int offset_array[5] = {0x10, 0x38, 0x60, 0x88, 0xb0};

static inline struct davinci_gpio_regs __iomem *irq2regs(struct irq_data *d)
{
	struct davinci_gpio_regs __iomem *g;

	g = (__force struct davinci_gpio_regs __iomem *)irq_data_get_irq_chip_data(d);

	return g;
}

static int davinci_gpio_irq_setup(struct platform_device *pdev);

/*--------------------------------------------------------------------------*/

/* board setup code *MUST* setup pinmux and enable the GPIO clock. */
static inline int __davinci_direction(struct gpio_chip *chip,
			unsigned offset, bool out, int value)
{
	struct davinci_gpio_controller *d = gpiochip_get_data(chip);
	struct davinci_gpio_regs __iomem *g;
	unsigned long flags;
	u32 temp;
	int bank = offset / 32;
	u32 mask = __gpio_mask(offset);

	g = d->regs[bank];
	spin_lock_irqsave(&d->lock, flags);
	temp = readl_relaxed(&g->dir);
	if (out) {
		temp &= ~mask;
		writel_relaxed(mask, value ? &g->set_data : &g->clr_data);
	} else {
		temp |= mask;
	}
	writel_relaxed(temp, &g->dir);
	spin_unlock_irqrestore(&d->lock, flags);

	return 0;
}

static int davinci_direction_in(struct gpio_chip *chip, unsigned offset)
{
	return __davinci_direction(chip, offset, false, 0);
}

static int
davinci_direction_out(struct gpio_chip *chip, unsigned offset, int value)
{
	return __davinci_direction(chip, offset, true, value);
}

/*
 * Read the pin's value (works even if it's set up as output);
 * returns zero/nonzero.
 *
 * Note that changes are synched to the GPIO clock, so reading values back
 * right after you've set them may give old values.
 */
static int davinci_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct davinci_gpio_controller *d = gpiochip_get_data(chip);
	struct davinci_gpio_regs __iomem *g;
	int bank = offset / 32;

	g = d->regs[bank];

	return !!(__gpio_mask(offset) & readl_relaxed(&g->in_data));
}

/*
 * Assuming the pin is muxed as a gpio output, set its output value.
 */
static void
davinci_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct davinci_gpio_controller *d = gpiochip_get_data(chip);
	struct davinci_gpio_regs __iomem *g;
	int bank = offset / 32;

	g = d->regs[bank];

	writel_relaxed(__gpio_mask(offset),
		       value ? &g->set_data : &g->clr_data);
}

static struct davinci_gpio_platform_data *
davinci_gpio_get_pdata(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	struct davinci_gpio_platform_data *pdata;
	int ret;
	u32 val;

	if (!IS_ENABLED(CONFIG_OF) || !pdev->dev.of_node)
		return dev_get_platdata(&pdev->dev);

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	ret = of_property_read_u32(dn, "ti,ngpio", &val);
	if (ret)
		goto of_err;

	pdata->ngpio = val;

	ret = of_property_read_u32(dn, "ti,davinci-gpio-unbanked", &val);
	if (ret)
		goto of_err;

	pdata->gpio_unbanked = val;

	return pdata;

of_err:
	dev_err(&pdev->dev, "Populating pdata from DT failed: err %d\n", ret);
	return NULL;
}

static int davinci_gpio_probe(struct platform_device *pdev)
{
	static int ctrl_num, bank_base;
	int gpio, bank;
	unsigned ngpio, nbank;
	struct davinci_gpio_controller *chips;
	struct davinci_gpio_platform_data *pdata;
	struct device *dev = &pdev->dev;
	struct resource *res;
	char label[MAX_LABEL_SIZE];

	pdata = davinci_gpio_get_pdata(pdev);
	if (!pdata) {
		dev_err(dev, "No platform data found\n");
		return -EINVAL;
	}

	dev->platform_data = pdata;

	/*
	 * The gpio banks conceptually expose a segmented bitmap,
	 * and "ngpio" is one more than the largest zero-based
	 * bit index that's valid.
	 */
	ngpio = pdata->ngpio;
	if (ngpio == 0) {
		dev_err(dev, "How many GPIOs?\n");
		return -EINVAL;
	}

	if (WARN_ON(ARCH_NR_GPIOS < ngpio))
		ngpio = ARCH_NR_GPIOS;

	nbank = DIV_ROUND_UP(ngpio, 32);
	chips = devm_kzalloc(dev,
			     nbank * sizeof(struct davinci_gpio_controller),
			     GFP_KERNEL);
	if (!chips)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	gpio_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(gpio_base))
		return PTR_ERR(gpio_base);

	snprintf(label, MAX_LABEL_SIZE, "davinci_gpio.%d", ctrl_num++);
	chips->chip.label = devm_kstrdup(dev, label, GFP_KERNEL);
		if (!chips->chip.label)
			return -ENOMEM;

	chips->chip.direction_input = davinci_direction_in;
	chips->chip.get = davinci_gpio_get;
	chips->chip.direction_output = davinci_direction_out;
	chips->chip.set = davinci_gpio_set;

	chips->chip.ngpio = ngpio;
	chips->chip.base = bank_base;

#ifdef CONFIG_OF_GPIO
	chips->chip.of_gpio_n_cells = 2;
	chips->chip.parent = dev;
	chips->chip.of_node = dev->of_node;
#endif
	spin_lock_init(&chips->lock);
	bank_base += ngpio;

	for (gpio = 0, bank = 0; gpio < ngpio; gpio += 32, bank++)
		chips->regs[bank] = gpio_base + offset_array[bank];

	gpiochip_add_data(&chips->chip, chips);
	platform_set_drvdata(pdev, chips);
	davinci_gpio_irq_setup(pdev);
	return 0;
}

/*--------------------------------------------------------------------------*/
/*
 * We expect irqs will normally be set up as input pins, but they can also be
 * used as output pins ... which is convenient for testing.
 *
 * NOTE:  The first few GPIOs also have direct INTC hookups in addition
 * to their GPIOBNK0 irq, with a bit less overhead.
 *
 * All those INTC hookups (direct, plus several IRQ banks) can also
 * serve as EDMA event triggers.
 */

static void gpio_irq_disable(struct irq_data *d)
{
	struct davinci_gpio_regs __iomem *g = irq2regs(d);
	u32 mask = (u32) irq_data_get_irq_handler_data(d);

	writel_relaxed(mask, &g->clr_falling);
	writel_relaxed(mask, &g->clr_rising);
}

static void gpio_irq_enable(struct irq_data *d)
{
	struct davinci_gpio_regs __iomem *g = irq2regs(d);
	u32 mask = (u32) irq_data_get_irq_handler_data(d);
	unsigned status = irqd_get_trigger_type(d);

	status &= IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING;
	if (!status)
		status = IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING;

	if (status & IRQ_TYPE_EDGE_FALLING)
		writel_relaxed(mask, &g->set_falling);
	if (status & IRQ_TYPE_EDGE_RISING)
		writel_relaxed(mask, &g->set_rising);
}

static int gpio_irq_type(struct irq_data *d, unsigned trigger)
{
	if (trigger & ~(IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING))
		return -EINVAL;

	return 0;
}

static struct irq_chip gpio_irqchip = {
	.name		= "GPIO",
	.irq_enable	= gpio_irq_enable,
	.irq_disable	= gpio_irq_disable,
	.irq_set_type	= gpio_irq_type,
	.flags		= IRQCHIP_SET_TYPE_MASKED,
};

static void gpio_irq_handler(struct irq_desc *desc)
{
	struct davinci_gpio_regs __iomem *g;
	u32 mask = 0xffff;
	int bank_num;
	struct davinci_gpio_controller *d;
	struct davinci_gpio_irq_data *irqdata;

	irqdata = (struct davinci_gpio_irq_data *)irq_desc_get_handler_data(desc);
	bank_num = irqdata->bank_num;
	g = irqdata->regs;
	d = irqdata->chip;

	/* we only care about one bank */
	if ((bank_num % 2) == 1)
		mask <<= 16;

	/* temporarily mask (level sensitive) parent IRQ */
	chained_irq_enter(irq_desc_get_chip(desc), desc);
	while (1) {
		u32		status;
		int		bit;
		irq_hw_number_t hw_irq;

		/* ack any irqs */
		status = readl_relaxed(&g->intstat) & mask;
		if (!status)
			break;
		writel_relaxed(status, &g->intstat);

		/* now demux them to the right lowlevel handler */

		while (status) {
			bit = __ffs(status);
			status &= ~BIT(bit);
			/* Max number of gpios per controller is 144 so
			 * hw_irq will be in [0..143]
			 */
			hw_irq = (bank_num / 2) * 32 + bit;

			generic_handle_irq(
				irq_find_mapping(d->irq_domain, hw_irq));
		}
	}
	chained_irq_exit(irq_desc_get_chip(desc), desc);
	/* now it may re-trigger */
}

static int gpio_to_irq_banked(struct gpio_chip *chip, unsigned offset)
{
	struct davinci_gpio_controller *d = gpiochip_get_data(chip);

	if (d->irq_domain)
		return irq_create_mapping(d->irq_domain, offset);
	else
		return -ENXIO;
}

static int gpio_to_irq_unbanked(struct gpio_chip *chip, unsigned offset)
{
	struct davinci_gpio_controller *d = gpiochip_get_data(chip);

	/*
	 * NOTE:  we assume for now that only irqs in the first gpio_chip
	 * can provide direct-mapped IRQs to AINTC (up to 32 GPIOs).
	 */
	if (offset < d->gpio_unbanked)
		return d->base_irq + offset;
	else
		return -ENODEV;
}

static int gpio_irq_type_unbanked(struct irq_data *data, unsigned trigger)
{
	struct davinci_gpio_controller *d;
	struct davinci_gpio_regs __iomem *g;
	u32 mask;

	d = (struct davinci_gpio_controller *)irq_data_get_irq_handler_data(data);
	g = (struct davinci_gpio_regs __iomem *)d->regs;
	mask = __gpio_mask(data->irq - d->base_irq);

	if (trigger & ~(IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING))
		return -EINVAL;

	writel_relaxed(mask, (trigger & IRQ_TYPE_EDGE_FALLING)
		     ? &g->set_falling : &g->clr_falling);
	writel_relaxed(mask, (trigger & IRQ_TYPE_EDGE_RISING)
		     ? &g->set_rising : &g->clr_rising);

	return 0;
}

static int
davinci_gpio_irq_map(struct irq_domain *d, unsigned int irq,
		     irq_hw_number_t hw)
{
	struct davinci_gpio_controller *chips =
				(struct davinci_gpio_controller *)d->host_data;
	struct davinci_gpio_regs __iomem *g = chips->regs[hw / 32];

	irq_set_chip_and_handler_name(irq, &gpio_irqchip, handle_simple_irq,
				"davinci_gpio");
	irq_set_irq_type(irq, IRQ_TYPE_NONE);
	irq_set_chip_data(irq, (__force void *)g);
	irq_set_handler_data(irq, (void *)__gpio_mask(hw));

	return 0;
}

static const struct irq_domain_ops davinci_gpio_irq_ops = {
	.map = davinci_gpio_irq_map,
	.xlate = irq_domain_xlate_onetwocell,
};

static struct irq_chip *davinci_gpio_get_irq_chip(unsigned int irq)
{
	static struct irq_chip_type gpio_unbanked;

	gpio_unbanked = *irq_data_get_chip_type(irq_get_irq_data(irq));

	return &gpio_unbanked.chip;
};

static struct irq_chip *keystone_gpio_get_irq_chip(unsigned int irq)
{
	static struct irq_chip gpio_unbanked;

	gpio_unbanked = *irq_get_chip(irq);
	return &gpio_unbanked;
};

static const struct of_device_id davinci_gpio_ids[];

/*
 * NOTE:  for suspend/resume, probably best to make a platform_device with
 * suspend_late/resume_resume calls hooking into results of the set_wake()
 * calls ... so if no gpios are wakeup events the clock can be disabled,
 * with outputs left at previously set levels, and so that VDD3P3V.IOPWDN0
 * (dm6446) can be set appropriately for GPIOV33 pins.
 */

static int davinci_gpio_irq_setup(struct platform_device *pdev)
{
	unsigned	gpio, bank;
	int		irq;
	struct clk	*clk;
	u32		binten = 0;
	unsigned	ngpio, bank_irq;
	struct device *dev = &pdev->dev;
	struct resource	*res;
	struct davinci_gpio_controller *chips = platform_get_drvdata(pdev);
	struct davinci_gpio_platform_data *pdata = dev->platform_data;
	struct davinci_gpio_regs __iomem *g;
	struct irq_domain	*irq_domain = NULL;
	const struct of_device_id *match;
	struct irq_chip *irq_chip;
	struct davinci_gpio_irq_data *irqdata;
	gpio_get_irq_chip_cb_t gpio_get_irq_chip;

	/*
	 * Use davinci_gpio_get_irq_chip by default to handle non DT cases
	 */
	gpio_get_irq_chip = davinci_gpio_get_irq_chip;
	match = of_match_device(of_match_ptr(davinci_gpio_ids),
				dev);
	if (match)
		gpio_get_irq_chip = (gpio_get_irq_chip_cb_t)match->data;

	ngpio = pdata->ngpio;
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(dev, "Invalid IRQ resource\n");
		return -EBUSY;
	}

	bank_irq = res->start;

	if (!bank_irq) {
		dev_err(dev, "Invalid IRQ resource\n");
		return -ENODEV;
	}

	clk = devm_clk_get(dev, "gpio");
	if (IS_ERR(clk)) {
		printk(KERN_ERR "Error %ld getting gpio clock?\n",
		       PTR_ERR(clk));
		return PTR_ERR(clk);
	}
	clk_prepare_enable(clk);

	if (!pdata->gpio_unbanked) {
		irq = irq_alloc_descs(-1, 0, ngpio, 0);
		if (irq < 0) {
			dev_err(dev, "Couldn't allocate IRQ numbers\n");
			return irq;
		}

		irq_domain = irq_domain_add_legacy(dev->of_node, ngpio, irq, 0,
							&davinci_gpio_irq_ops,
							chips);
		if (!irq_domain) {
			dev_err(dev, "Couldn't register an IRQ domain\n");
			return -ENODEV;
		}
	}

	/*
	 * Arrange gpio_to_irq() support, handling either direct IRQs or
	 * banked IRQs.  Having GPIOs in the first GPIO bank use direct
	 * IRQs, while the others use banked IRQs, would need some setup
	 * tweaks to recognize hardware which can do that.
	 */
	chips->chip.to_irq = gpio_to_irq_banked;
	chips->irq_domain = irq_domain;

	/*
	 * AINTC can handle direct/unbanked IRQs for GPIOs, with the GPIO
	 * controller only handling trigger modes.  We currently assume no
	 * IRQ mux conflicts; gpio_irq_type_unbanked() is only for GPIOs.
	 */
	if (pdata->gpio_unbanked) {
		/* pass "bank 0" GPIO IRQs to AINTC */
		chips->chip.to_irq = gpio_to_irq_unbanked;
		chips->base_irq = bank_irq;
		chips->gpio_unbanked = pdata->gpio_unbanked;
		binten = GENMASK(pdata->gpio_unbanked / 16, 0);

		/* AINTC handles mask/unmask; GPIO handles triggering */
		irq = bank_irq;
		irq_chip = gpio_get_irq_chip(irq);
		irq_chip->name = "GPIO-AINTC";
		irq_chip->irq_set_type = gpio_irq_type_unbanked;

		/* default trigger: both edges */
		g = chips->regs[0];
		writel_relaxed(~0, &g->set_falling);
		writel_relaxed(~0, &g->set_rising);

		/* set the direct IRQs up to use that irqchip */
		for (gpio = 0; gpio < pdata->gpio_unbanked; gpio++, irq++) {
			irq_set_chip(irq, irq_chip);
			irq_set_handler_data(irq, chips);
			irq_set_status_flags(irq, IRQ_TYPE_EDGE_BOTH);
		}

		goto done;
	}

	/*
	 * Or, AINTC can handle IRQs for banks of 16 GPIO IRQs, which we
	 * then chain through our own handler.
	 */
	for (gpio = 0, bank = 0; gpio < ngpio; bank++, bank_irq++, gpio += 16) {
		/* disabled by default, enabled only as needed
		 * There are register sets for 32 GPIOs. 2 banks of 16
		 * GPIOs are covered by each set of registers hence divide by 2
		 */
		g = chips->regs[bank / 2];
		writel_relaxed(~0, &g->clr_falling);
		writel_relaxed(~0, &g->clr_rising);

		/*
		 * Each chip handles 32 gpios, and each irq bank consists of 16
		 * gpio irqs. Pass the irq bank's corresponding controller to
		 * the chained irq handler.
		 */
		irqdata = devm_kzalloc(&pdev->dev,
				       sizeof(struct
					      davinci_gpio_irq_data),
					      GFP_KERNEL);
		if (!irqdata)
			return -ENOMEM;

		irqdata->regs = g;
		irqdata->bank_num = bank;
		irqdata->chip = chips;

		irq_set_chained_handler_and_data(bank_irq, gpio_irq_handler,
						 irqdata);

		binten |= BIT(bank);
	}

done:
	/*
	 * BINTEN -- per-bank interrupt enable. genirq would also let these
	 * bits be set/cleared dynamically.
	 */
	writel_relaxed(binten, gpio_base + BINTEN);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id davinci_gpio_ids[] = {
	{ .compatible = "ti,keystone-gpio", keystone_gpio_get_irq_chip},
	{ .compatible = "ti,dm6441-gpio", davinci_gpio_get_irq_chip},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, davinci_gpio_ids);
#endif

static struct platform_driver davinci_gpio_driver = {
	.probe		= davinci_gpio_probe,
	.driver		= {
		.name		= "davinci_gpio",
		.of_match_table	= of_match_ptr(davinci_gpio_ids),
	},
};

/**
 * GPIO driver registration needs to be done before machine_init functions
 * access GPIO. Hence davinci_gpio_drv_reg() is a postcore_initcall.
 */
static int __init davinci_gpio_drv_reg(void)
{
	return platform_driver_register(&davinci_gpio_driver);
}
postcore_initcall(davinci_gpio_drv_reg);
