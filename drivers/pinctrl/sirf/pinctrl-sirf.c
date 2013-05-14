/*
 * pinmux driver for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/bitops.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <asm/mach/irq.h>

#include "pinctrl-sirf.h"

#define DRIVER_NAME "pinmux-sirf"

struct sirfsoc_gpio_bank {
	struct of_mm_gpio_chip chip;
	struct irq_domain *domain;
	int id;
	int parent_irq;
	spinlock_t lock;
	bool is_marco; /* for marco, some registers are different with prima2 */
};

static struct sirfsoc_gpio_bank sgpio_bank[SIRFSOC_GPIO_NO_OF_BANKS];
static DEFINE_SPINLOCK(sgpio_lock);

static struct sirfsoc_pin_group *sirfsoc_pin_groups;
static int sirfsoc_pingrp_cnt;

static int sirfsoc_get_groups_count(struct pinctrl_dev *pctldev)
{
	return sirfsoc_pingrp_cnt;
}

static const char *sirfsoc_get_group_name(struct pinctrl_dev *pctldev,
				       unsigned selector)
{
	return sirfsoc_pin_groups[selector].name;
}

static int sirfsoc_get_group_pins(struct pinctrl_dev *pctldev, unsigned selector,
			       const unsigned **pins,
			       unsigned *num_pins)
{
	*pins = sirfsoc_pin_groups[selector].pins;
	*num_pins = sirfsoc_pin_groups[selector].num_pins;
	return 0;
}

static void sirfsoc_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
		   unsigned offset)
{
	seq_printf(s, " " DRIVER_NAME);
}

static int sirfsoc_dt_node_to_map(struct pinctrl_dev *pctldev,
				 struct device_node *np_config,
				 struct pinctrl_map **map, unsigned *num_maps)
{
	struct sirfsoc_pmx *spmx = pinctrl_dev_get_drvdata(pctldev);
	struct device_node *np;
	struct property *prop;
	const char *function, *group;
	int ret, index = 0, count = 0;

	/* calculate number of maps required */
	for_each_child_of_node(np_config, np) {
		ret = of_property_read_string(np, "sirf,function", &function);
		if (ret < 0)
			return ret;

		ret = of_property_count_strings(np, "sirf,pins");
		if (ret < 0)
			return ret;

		count += ret;
	}

	if (!count) {
		dev_err(spmx->dev, "No child nodes passed via DT\n");
		return -ENODEV;
	}

	*map = kzalloc(sizeof(**map) * count, GFP_KERNEL);
	if (!*map)
		return -ENOMEM;

	for_each_child_of_node(np_config, np) {
		of_property_read_string(np, "sirf,function", &function);
		of_property_for_each_string(np, "sirf,pins", prop, group) {
			(*map)[index].type = PIN_MAP_TYPE_MUX_GROUP;
			(*map)[index].data.mux.group = group;
			(*map)[index].data.mux.function = function;
			index++;
		}
	}

	*num_maps = count;

	return 0;
}

static void sirfsoc_dt_free_map(struct pinctrl_dev *pctldev,
		struct pinctrl_map *map, unsigned num_maps)
{
	kfree(map);
}

static struct pinctrl_ops sirfsoc_pctrl_ops = {
	.get_groups_count = sirfsoc_get_groups_count,
	.get_group_name = sirfsoc_get_group_name,
	.get_group_pins = sirfsoc_get_group_pins,
	.pin_dbg_show = sirfsoc_pin_dbg_show,
	.dt_node_to_map = sirfsoc_dt_node_to_map,
	.dt_free_map = sirfsoc_dt_free_map,
};

static struct sirfsoc_pmx_func *sirfsoc_pmx_functions;
static int sirfsoc_pmxfunc_cnt;

static void sirfsoc_pinmux_endisable(struct sirfsoc_pmx *spmx, unsigned selector,
	bool enable)
{
	int i;
	const struct sirfsoc_padmux *mux = sirfsoc_pmx_functions[selector].padmux;
	const struct sirfsoc_muxmask *mask = mux->muxmask;

	for (i = 0; i < mux->muxmask_counts; i++) {
		u32 muxval;
		if (!spmx->is_marco) {
			muxval = readl(spmx->gpio_virtbase + SIRFSOC_GPIO_PAD_EN(mask[i].group));
			if (enable)
				muxval = muxval & ~mask[i].mask;
			else
				muxval = muxval | mask[i].mask;
			writel(muxval, spmx->gpio_virtbase + SIRFSOC_GPIO_PAD_EN(mask[i].group));
		} else {
			if (enable)
				writel(mask[i].mask, spmx->gpio_virtbase +
					SIRFSOC_GPIO_PAD_EN_CLR(mask[i].group));
			else
				writel(mask[i].mask, spmx->gpio_virtbase +
					SIRFSOC_GPIO_PAD_EN(mask[i].group));
		}
	}

	if (mux->funcmask && enable) {
		u32 func_en_val;
		func_en_val =
			readl(spmx->rsc_virtbase + SIRFSOC_RSC_PIN_MUX);
		func_en_val =
			(func_en_val & ~mux->funcmask) | (mux->
				funcval);
		writel(func_en_val, spmx->rsc_virtbase + SIRFSOC_RSC_PIN_MUX);
	}
}

static int sirfsoc_pinmux_enable(struct pinctrl_dev *pmxdev, unsigned selector,
	unsigned group)
{
	struct sirfsoc_pmx *spmx;

	spmx = pinctrl_dev_get_drvdata(pmxdev);
	sirfsoc_pinmux_endisable(spmx, selector, true);

	return 0;
}

static void sirfsoc_pinmux_disable(struct pinctrl_dev *pmxdev, unsigned selector,
	unsigned group)
{
	struct sirfsoc_pmx *spmx;

	spmx = pinctrl_dev_get_drvdata(pmxdev);
	sirfsoc_pinmux_endisable(spmx, selector, false);
}

static int sirfsoc_pinmux_get_funcs_count(struct pinctrl_dev *pmxdev)
{
	return sirfsoc_pmxfunc_cnt;
}

static const char *sirfsoc_pinmux_get_func_name(struct pinctrl_dev *pctldev,
					  unsigned selector)
{
	return sirfsoc_pmx_functions[selector].name;
}

static int sirfsoc_pinmux_get_groups(struct pinctrl_dev *pctldev, unsigned selector,
			       const char * const **groups,
			       unsigned * const num_groups)
{
	*groups = sirfsoc_pmx_functions[selector].groups;
	*num_groups = sirfsoc_pmx_functions[selector].num_groups;
	return 0;
}

static int sirfsoc_pinmux_request_gpio(struct pinctrl_dev *pmxdev,
	struct pinctrl_gpio_range *range, unsigned offset)
{
	struct sirfsoc_pmx *spmx;

	int group = range->id;

	u32 muxval;

	spmx = pinctrl_dev_get_drvdata(pmxdev);

	if (!spmx->is_marco) {
		muxval = readl(spmx->gpio_virtbase + SIRFSOC_GPIO_PAD_EN(group));
		muxval = muxval | (1 << (offset - range->pin_base));
		writel(muxval, spmx->gpio_virtbase + SIRFSOC_GPIO_PAD_EN(group));
	} else {
		writel(1 << (offset - range->pin_base), spmx->gpio_virtbase +
			SIRFSOC_GPIO_PAD_EN(group));
	}

	return 0;
}

static struct pinmux_ops sirfsoc_pinmux_ops = {
	.enable = sirfsoc_pinmux_enable,
	.disable = sirfsoc_pinmux_disable,
	.get_functions_count = sirfsoc_pinmux_get_funcs_count,
	.get_function_name = sirfsoc_pinmux_get_func_name,
	.get_function_groups = sirfsoc_pinmux_get_groups,
	.gpio_request_enable = sirfsoc_pinmux_request_gpio,
};

static struct pinctrl_desc sirfsoc_pinmux_desc = {
	.name = DRIVER_NAME,
	.pctlops = &sirfsoc_pctrl_ops,
	.pmxops = &sirfsoc_pinmux_ops,
	.owner = THIS_MODULE,
};

/*
 * Todo: bind irq_chip to every pinctrl_gpio_range
 */
static struct pinctrl_gpio_range sirfsoc_gpio_ranges[] = {
	{
		.name = "sirfsoc-gpio*",
		.id = 0,
		.base = 0,
		.pin_base = 0,
		.npins = 32,
	}, {
		.name = "sirfsoc-gpio*",
		.id = 1,
		.base = 32,
		.pin_base = 32,
		.npins = 32,
	}, {
		.name = "sirfsoc-gpio*",
		.id = 2,
		.base = 64,
		.pin_base = 64,
		.npins = 32,
	}, {
		.name = "sirfsoc-gpio*",
		.id = 3,
		.base = 96,
		.pin_base = 96,
		.npins = 19,
	},
};

static void __iomem *sirfsoc_rsc_of_iomap(void)
{
	const struct of_device_id rsc_ids[]  = {
		{ .compatible = "sirf,prima2-rsc" },
		{ .compatible = "sirf,marco-rsc" },
		{}
	};
	struct device_node *np;

	np = of_find_matching_node(NULL, rsc_ids);
	if (!np)
		panic("unable to find compatible rsc node in dtb\n");

	return of_iomap(np, 0);
}

static int sirfsoc_gpio_of_xlate(struct gpio_chip *gc,
       const struct of_phandle_args *gpiospec,
       u32 *flags)
{
       if (gpiospec->args[0] > SIRFSOC_GPIO_NO_OF_BANKS * SIRFSOC_GPIO_BANK_SIZE)
               return -EINVAL;

       if (gc != &sgpio_bank[gpiospec->args[0] / SIRFSOC_GPIO_BANK_SIZE].chip.gc)
               return -EINVAL;

       if (flags)
               *flags = gpiospec->args[1];

       return gpiospec->args[0] % SIRFSOC_GPIO_BANK_SIZE;
}

static const struct of_device_id pinmux_ids[] = {
	{ .compatible = "sirf,prima2-pinctrl", .data = &prima2_pinctrl_data, },
	{ .compatible = "sirf,atlas6-pinctrl", .data = &atlas6_pinctrl_data, },
	{ .compatible = "sirf,marco-pinctrl", .data = &prima2_pinctrl_data, },
	{}
};

static int sirfsoc_pinmux_probe(struct platform_device *pdev)
{
	int ret;
	struct sirfsoc_pmx *spmx;
	struct device_node *np = pdev->dev.of_node;
	const struct sirfsoc_pinctrl_data *pdata;
	int i;

	/* Create state holders etc for this driver */
	spmx = devm_kzalloc(&pdev->dev, sizeof(*spmx), GFP_KERNEL);
	if (!spmx)
		return -ENOMEM;

	spmx->dev = &pdev->dev;

	platform_set_drvdata(pdev, spmx);

	spmx->gpio_virtbase = of_iomap(np, 0);
	if (!spmx->gpio_virtbase) {
		dev_err(&pdev->dev, "can't map gpio registers\n");
		return -ENOMEM;
	}

	spmx->rsc_virtbase = sirfsoc_rsc_of_iomap();
	if (!spmx->rsc_virtbase) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "can't map rsc registers\n");
		goto out_no_rsc_remap;
	}

	if (of_device_is_compatible(np, "sirf,marco-pinctrl"))
		spmx->is_marco = 1;

	pdata = of_match_node(pinmux_ids, np)->data;
	sirfsoc_pin_groups = pdata->grps;
	sirfsoc_pingrp_cnt = pdata->grps_cnt;
	sirfsoc_pmx_functions = pdata->funcs;
	sirfsoc_pmxfunc_cnt = pdata->funcs_cnt;
	sirfsoc_pinmux_desc.pins = pdata->pads;
	sirfsoc_pinmux_desc.npins = pdata->pads_cnt;


	/* Now register the pin controller and all pins it handles */
	spmx->pmx = pinctrl_register(&sirfsoc_pinmux_desc, &pdev->dev, spmx);
	if (!spmx->pmx) {
		dev_err(&pdev->dev, "could not register SIRFSOC pinmux driver\n");
		ret = -EINVAL;
		goto out_no_pmx;
	}

	for (i = 0; i < ARRAY_SIZE(sirfsoc_gpio_ranges); i++) {
		sirfsoc_gpio_ranges[i].gc = &sgpio_bank[i].chip.gc;
		pinctrl_add_gpio_range(spmx->pmx, &sirfsoc_gpio_ranges[i]);
	}

	dev_info(&pdev->dev, "initialized SIRFSOC pinmux driver\n");

	return 0;

out_no_pmx:
	iounmap(spmx->rsc_virtbase);
out_no_rsc_remap:
	iounmap(spmx->gpio_virtbase);
	return ret;
}

static struct platform_driver sirfsoc_pinmux_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = pinmux_ids,
	},
	.probe = sirfsoc_pinmux_probe,
};

static int __init sirfsoc_pinmux_init(void)
{
	return platform_driver_register(&sirfsoc_pinmux_driver);
}
arch_initcall(sirfsoc_pinmux_init);

static inline int sirfsoc_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct sirfsoc_gpio_bank *bank = container_of(to_of_mm_gpio_chip(chip),
		struct sirfsoc_gpio_bank, chip);

	return irq_create_mapping(bank->domain, offset);
}

static inline int sirfsoc_gpio_to_offset(unsigned int gpio)
{
	return gpio % SIRFSOC_GPIO_BANK_SIZE;
}

static inline struct sirfsoc_gpio_bank *sirfsoc_gpio_to_bank(unsigned int gpio)
{
	return &sgpio_bank[gpio / SIRFSOC_GPIO_BANK_SIZE];
}

static inline struct sirfsoc_gpio_bank *sirfsoc_irqchip_to_bank(struct gpio_chip *chip)
{
	return container_of(to_of_mm_gpio_chip(chip), struct sirfsoc_gpio_bank, chip);
}

static void sirfsoc_gpio_irq_ack(struct irq_data *d)
{
	struct sirfsoc_gpio_bank *bank = irq_data_get_irq_chip_data(d);
	int idx = d->hwirq % SIRFSOC_GPIO_BANK_SIZE;
	u32 val, offset;
	unsigned long flags;

	offset = SIRFSOC_GPIO_CTRL(bank->id, idx);

	spin_lock_irqsave(&sgpio_lock, flags);

	val = readl(bank->chip.regs + offset);

	writel(val, bank->chip.regs + offset);

	spin_unlock_irqrestore(&sgpio_lock, flags);
}

static void __sirfsoc_gpio_irq_mask(struct sirfsoc_gpio_bank *bank, int idx)
{
	u32 val, offset;
	unsigned long flags;

	offset = SIRFSOC_GPIO_CTRL(bank->id, idx);

	spin_lock_irqsave(&sgpio_lock, flags);

	val = readl(bank->chip.regs + offset);
	val &= ~SIRFSOC_GPIO_CTL_INTR_EN_MASK;
	val &= ~SIRFSOC_GPIO_CTL_INTR_STS_MASK;
	writel(val, bank->chip.regs + offset);

	spin_unlock_irqrestore(&sgpio_lock, flags);
}

static void sirfsoc_gpio_irq_mask(struct irq_data *d)
{
	struct sirfsoc_gpio_bank *bank = irq_data_get_irq_chip_data(d);

	__sirfsoc_gpio_irq_mask(bank, d->hwirq % SIRFSOC_GPIO_BANK_SIZE);
}

static void sirfsoc_gpio_irq_unmask(struct irq_data *d)
{
	struct sirfsoc_gpio_bank *bank = irq_data_get_irq_chip_data(d);
	int idx = d->hwirq % SIRFSOC_GPIO_BANK_SIZE;
	u32 val, offset;
	unsigned long flags;

	offset = SIRFSOC_GPIO_CTRL(bank->id, idx);

	spin_lock_irqsave(&sgpio_lock, flags);

	val = readl(bank->chip.regs + offset);
	val &= ~SIRFSOC_GPIO_CTL_INTR_STS_MASK;
	val |= SIRFSOC_GPIO_CTL_INTR_EN_MASK;
	writel(val, bank->chip.regs + offset);

	spin_unlock_irqrestore(&sgpio_lock, flags);
}

static int sirfsoc_gpio_irq_type(struct irq_data *d, unsigned type)
{
	struct sirfsoc_gpio_bank *bank = irq_data_get_irq_chip_data(d);
	int idx = d->hwirq % SIRFSOC_GPIO_BANK_SIZE;
	u32 val, offset;
	unsigned long flags;

	offset = SIRFSOC_GPIO_CTRL(bank->id, idx);

	spin_lock_irqsave(&sgpio_lock, flags);

	val = readl(bank->chip.regs + offset);
	val &= ~SIRFSOC_GPIO_CTL_INTR_STS_MASK;

	switch (type) {
	case IRQ_TYPE_NONE:
		break;
	case IRQ_TYPE_EDGE_RISING:
		val |= SIRFSOC_GPIO_CTL_INTR_HIGH_MASK | SIRFSOC_GPIO_CTL_INTR_TYPE_MASK;
		val &= ~SIRFSOC_GPIO_CTL_INTR_LOW_MASK;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		val &= ~SIRFSOC_GPIO_CTL_INTR_HIGH_MASK;
		val |= SIRFSOC_GPIO_CTL_INTR_LOW_MASK | SIRFSOC_GPIO_CTL_INTR_TYPE_MASK;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		val |= SIRFSOC_GPIO_CTL_INTR_HIGH_MASK | SIRFSOC_GPIO_CTL_INTR_LOW_MASK |
			 SIRFSOC_GPIO_CTL_INTR_TYPE_MASK;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		val &= ~(SIRFSOC_GPIO_CTL_INTR_HIGH_MASK | SIRFSOC_GPIO_CTL_INTR_TYPE_MASK);
		val |= SIRFSOC_GPIO_CTL_INTR_LOW_MASK;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		val |= SIRFSOC_GPIO_CTL_INTR_HIGH_MASK;
		val &= ~(SIRFSOC_GPIO_CTL_INTR_LOW_MASK | SIRFSOC_GPIO_CTL_INTR_TYPE_MASK);
		break;
	}

	writel(val, bank->chip.regs + offset);

	spin_unlock_irqrestore(&sgpio_lock, flags);

	return 0;
}

static struct irq_chip sirfsoc_irq_chip = {
	.name = "sirf-gpio-irq",
	.irq_ack = sirfsoc_gpio_irq_ack,
	.irq_mask = sirfsoc_gpio_irq_mask,
	.irq_unmask = sirfsoc_gpio_irq_unmask,
	.irq_set_type = sirfsoc_gpio_irq_type,
};

static void sirfsoc_gpio_handle_irq(unsigned int irq, struct irq_desc *desc)
{
	struct sirfsoc_gpio_bank *bank = irq_get_handler_data(irq);
	u32 status, ctrl;
	int idx = 0;
	struct irq_chip *chip = irq_get_chip(irq);

	chained_irq_enter(chip, desc);

	status = readl(bank->chip.regs + SIRFSOC_GPIO_INT_STATUS(bank->id));
	if (!status) {
		printk(KERN_WARNING
			"%s: gpio id %d status %#x no interrupt is flaged\n",
			__func__, bank->id, status);
		handle_bad_irq(irq, desc);
		return;
	}

	while (status) {
		ctrl = readl(bank->chip.regs + SIRFSOC_GPIO_CTRL(bank->id, idx));

		/*
		 * Here we must check whether the corresponding GPIO's interrupt
		 * has been enabled, otherwise just skip it
		 */
		if ((status & 0x1) && (ctrl & SIRFSOC_GPIO_CTL_INTR_EN_MASK)) {
			pr_debug("%s: gpio id %d idx %d happens\n",
				__func__, bank->id, idx);
			generic_handle_irq(irq_find_mapping(bank->domain, idx));
		}

		idx++;
		status = status >> 1;
	}

	chained_irq_exit(chip, desc);
}

static inline void sirfsoc_gpio_set_input(struct sirfsoc_gpio_bank *bank, unsigned ctrl_offset)
{
	u32 val;

	val = readl(bank->chip.regs + ctrl_offset);
	val &= ~SIRFSOC_GPIO_CTL_OUT_EN_MASK;
	writel(val, bank->chip.regs + ctrl_offset);
}

static int sirfsoc_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct sirfsoc_gpio_bank *bank = sirfsoc_irqchip_to_bank(chip);
	unsigned long flags;

	if (pinctrl_request_gpio(chip->base + offset))
		return -ENODEV;

	spin_lock_irqsave(&bank->lock, flags);

	/*
	 * default status:
	 * set direction as input and mask irq
	 */
	sirfsoc_gpio_set_input(bank, SIRFSOC_GPIO_CTRL(bank->id, offset));
	__sirfsoc_gpio_irq_mask(bank, offset);

	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static void sirfsoc_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	struct sirfsoc_gpio_bank *bank = sirfsoc_irqchip_to_bank(chip);
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);

	__sirfsoc_gpio_irq_mask(bank, offset);
	sirfsoc_gpio_set_input(bank, SIRFSOC_GPIO_CTRL(bank->id, offset));

	spin_unlock_irqrestore(&bank->lock, flags);

	pinctrl_free_gpio(chip->base + offset);
}

static int sirfsoc_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	struct sirfsoc_gpio_bank *bank = sirfsoc_irqchip_to_bank(chip);
	int idx = sirfsoc_gpio_to_offset(gpio);
	unsigned long flags;
	unsigned offset;

	offset = SIRFSOC_GPIO_CTRL(bank->id, idx);

	spin_lock_irqsave(&bank->lock, flags);

	sirfsoc_gpio_set_input(bank, offset);

	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static inline void sirfsoc_gpio_set_output(struct sirfsoc_gpio_bank *bank, unsigned offset,
	int value)
{
	u32 out_ctrl;
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);

	out_ctrl = readl(bank->chip.regs + offset);
	if (value)
		out_ctrl |= SIRFSOC_GPIO_CTL_DATAOUT_MASK;
	else
		out_ctrl &= ~SIRFSOC_GPIO_CTL_DATAOUT_MASK;

	out_ctrl &= ~SIRFSOC_GPIO_CTL_INTR_EN_MASK;
	out_ctrl |= SIRFSOC_GPIO_CTL_OUT_EN_MASK;
	writel(out_ctrl, bank->chip.regs + offset);

	spin_unlock_irqrestore(&bank->lock, flags);
}

static int sirfsoc_gpio_direction_output(struct gpio_chip *chip, unsigned gpio, int value)
{
	struct sirfsoc_gpio_bank *bank = sirfsoc_irqchip_to_bank(chip);
	int idx = sirfsoc_gpio_to_offset(gpio);
	u32 offset;
	unsigned long flags;

	offset = SIRFSOC_GPIO_CTRL(bank->id, idx);

	spin_lock_irqsave(&sgpio_lock, flags);

	sirfsoc_gpio_set_output(bank, offset, value);

	spin_unlock_irqrestore(&sgpio_lock, flags);

	return 0;
}

static int sirfsoc_gpio_get_value(struct gpio_chip *chip, unsigned offset)
{
	struct sirfsoc_gpio_bank *bank = sirfsoc_irqchip_to_bank(chip);
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);

	val = readl(bank->chip.regs + SIRFSOC_GPIO_CTRL(bank->id, offset));

	spin_unlock_irqrestore(&bank->lock, flags);

	return !!(val & SIRFSOC_GPIO_CTL_DATAIN_MASK);
}

static void sirfsoc_gpio_set_value(struct gpio_chip *chip, unsigned offset,
	int value)
{
	struct sirfsoc_gpio_bank *bank = sirfsoc_irqchip_to_bank(chip);
	u32 ctrl;
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);

	ctrl = readl(bank->chip.regs + SIRFSOC_GPIO_CTRL(bank->id, offset));
	if (value)
		ctrl |= SIRFSOC_GPIO_CTL_DATAOUT_MASK;
	else
		ctrl &= ~SIRFSOC_GPIO_CTL_DATAOUT_MASK;
	writel(ctrl, bank->chip.regs + SIRFSOC_GPIO_CTRL(bank->id, offset));

	spin_unlock_irqrestore(&bank->lock, flags);
}

static int sirfsoc_gpio_irq_map(struct irq_domain *d, unsigned int irq,
				irq_hw_number_t hwirq)
{
	struct sirfsoc_gpio_bank *bank = d->host_data;

	if (!bank)
		return -EINVAL;

	irq_set_chip(irq, &sirfsoc_irq_chip);
	irq_set_handler(irq, handle_level_irq);
	irq_set_chip_data(irq, bank);
	set_irq_flags(irq, IRQF_VALID);

	return 0;
}

static const struct irq_domain_ops sirfsoc_gpio_irq_simple_ops = {
	.map = sirfsoc_gpio_irq_map,
	.xlate = irq_domain_xlate_twocell,
};

static void sirfsoc_gpio_set_pullup(const u32 *pullups)
{
	int i, n;
	const unsigned long *p = (const unsigned long *)pullups;

	for (i = 0; i < SIRFSOC_GPIO_NO_OF_BANKS; i++) {
		for_each_set_bit(n, p + i, BITS_PER_LONG) {
			u32 offset = SIRFSOC_GPIO_CTRL(i, n);
			u32 val = readl(sgpio_bank[i].chip.regs + offset);
			val |= SIRFSOC_GPIO_CTL_PULL_MASK;
			val |= SIRFSOC_GPIO_CTL_PULL_HIGH;
			writel(val, sgpio_bank[i].chip.regs + offset);
		}
	}
}

static void sirfsoc_gpio_set_pulldown(const u32 *pulldowns)
{
	int i, n;
	const unsigned long *p = (const unsigned long *)pulldowns;

	for (i = 0; i < SIRFSOC_GPIO_NO_OF_BANKS; i++) {
		for_each_set_bit(n, p + i, BITS_PER_LONG) {
			u32 offset = SIRFSOC_GPIO_CTRL(i, n);
			u32 val = readl(sgpio_bank[i].chip.regs + offset);
			val |= SIRFSOC_GPIO_CTL_PULL_MASK;
			val &= ~SIRFSOC_GPIO_CTL_PULL_HIGH;
			writel(val, sgpio_bank[i].chip.regs + offset);
		}
	}
}

static int sirfsoc_gpio_probe(struct device_node *np)
{
	int i, err = 0;
	struct sirfsoc_gpio_bank *bank;
	void *regs;
	struct platform_device *pdev;
	bool is_marco = false;

	u32 pullups[SIRFSOC_GPIO_NO_OF_BANKS], pulldowns[SIRFSOC_GPIO_NO_OF_BANKS];

	pdev = of_find_device_by_node(np);
	if (!pdev)
		return -ENODEV;

	regs = of_iomap(np, 0);
	if (!regs)
		return -ENOMEM;

	if (of_device_is_compatible(np, "sirf,marco-pinctrl"))
		is_marco = 1;

	for (i = 0; i < SIRFSOC_GPIO_NO_OF_BANKS; i++) {
		bank = &sgpio_bank[i];
		spin_lock_init(&bank->lock);
		bank->chip.gc.request = sirfsoc_gpio_request;
		bank->chip.gc.free = sirfsoc_gpio_free;
		bank->chip.gc.direction_input = sirfsoc_gpio_direction_input;
		bank->chip.gc.get = sirfsoc_gpio_get_value;
		bank->chip.gc.direction_output = sirfsoc_gpio_direction_output;
		bank->chip.gc.set = sirfsoc_gpio_set_value;
		bank->chip.gc.to_irq = sirfsoc_gpio_to_irq;
		bank->chip.gc.base = i * SIRFSOC_GPIO_BANK_SIZE;
		bank->chip.gc.ngpio = SIRFSOC_GPIO_BANK_SIZE;
		bank->chip.gc.label = kstrdup(np->full_name, GFP_KERNEL);
		bank->chip.gc.of_node = np;
		bank->chip.gc.of_xlate = sirfsoc_gpio_of_xlate;
		bank->chip.gc.of_gpio_n_cells = 2;
		bank->chip.regs = regs;
		bank->id = i;
		bank->is_marco = is_marco;
		bank->parent_irq = platform_get_irq(pdev, i);
		if (bank->parent_irq < 0) {
			err = bank->parent_irq;
			goto out;
		}

		err = gpiochip_add(&bank->chip.gc);
		if (err) {
			pr_err("%s: error in probe function with status %d\n",
				np->full_name, err);
			goto out;
		}

		bank->domain = irq_domain_add_linear(np, SIRFSOC_GPIO_BANK_SIZE,
						&sirfsoc_gpio_irq_simple_ops, bank);

		if (!bank->domain) {
			pr_err("%s: Failed to create irqdomain\n", np->full_name);
			err = -ENOSYS;
			goto out;
		}

		irq_set_chained_handler(bank->parent_irq, sirfsoc_gpio_handle_irq);
		irq_set_handler_data(bank->parent_irq, bank);
	}

	if (!of_property_read_u32_array(np, "sirf,pullups", pullups,
		SIRFSOC_GPIO_NO_OF_BANKS))
		sirfsoc_gpio_set_pullup(pullups);

	if (!of_property_read_u32_array(np, "sirf,pulldowns", pulldowns,
		SIRFSOC_GPIO_NO_OF_BANKS))
		sirfsoc_gpio_set_pulldown(pulldowns);

	return 0;

out:
	iounmap(regs);
	return err;
}

static int __init sirfsoc_gpio_init(void)
{

	struct device_node *np;

	np = of_find_matching_node(NULL, pinmux_ids);

	if (!np)
		return -ENODEV;

	return sirfsoc_gpio_probe(np);
}
subsys_initcall(sirfsoc_gpio_init);

MODULE_AUTHOR("Rongjun Ying <rongjun.ying@csr.com>, "
	"Yuping Luo <yuping.luo@csr.com>, "
	"Barry Song <baohua.song@csr.com>");
MODULE_DESCRIPTION("SIRFSOC pin control driver");
MODULE_LICENSE("GPL");
