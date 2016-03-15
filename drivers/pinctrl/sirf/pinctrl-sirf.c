/*
 * pinmux driver for CSR SiRFprimaII
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
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

#include "pinctrl-sirf.h"

#define DRIVER_NAME "pinmux-sirf"

struct sirfsoc_gpio_bank {
	int id;
	int parent_irq;
	spinlock_t lock;
};

struct sirfsoc_gpio_chip {
	struct of_mm_gpio_chip chip;
	struct sirfsoc_gpio_bank sgpio_bank[SIRFSOC_GPIO_NO_OF_BANKS];
	spinlock_t lock;
};

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

static int sirfsoc_get_group_pins(struct pinctrl_dev *pctldev,
				unsigned selector,
				const unsigned **pins,
				unsigned *num_pins)
{
	*pins = sirfsoc_pin_groups[selector].pins;
	*num_pins = sirfsoc_pin_groups[selector].num_pins;
	return 0;
}

static void sirfsoc_pin_dbg_show(struct pinctrl_dev *pctldev,
				struct seq_file *s, unsigned offset)
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
		if (ret < 0) {
			of_node_put(np);
			return ret;
		}

		ret = of_property_count_strings(np, "sirf,pins");
		if (ret < 0) {
			of_node_put(np);
			return ret;
		}

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

static void sirfsoc_pinmux_endisable(struct sirfsoc_pmx *spmx,
					unsigned selector, bool enable)
{
	int i;
	const struct sirfsoc_padmux *mux =
		sirfsoc_pmx_functions[selector].padmux;
	const struct sirfsoc_muxmask *mask = mux->muxmask;

	for (i = 0; i < mux->muxmask_counts; i++) {
		u32 muxval;
		muxval = readl(spmx->gpio_virtbase +
			SIRFSOC_GPIO_PAD_EN(mask[i].group));
		if (enable)
			muxval = muxval & ~mask[i].mask;
		else
			muxval = muxval | mask[i].mask;
		writel(muxval, spmx->gpio_virtbase +
			SIRFSOC_GPIO_PAD_EN(mask[i].group));
	}

	if (mux->funcmask && enable) {
		u32 func_en_val;

		func_en_val =
			readl(spmx->rsc_virtbase + mux->ctrlreg);
		func_en_val =
			(func_en_val & ~mux->funcmask) | (mux->funcval);
		writel(func_en_val, spmx->rsc_virtbase + mux->ctrlreg);
	}
}

static int sirfsoc_pinmux_set_mux(struct pinctrl_dev *pmxdev,
				unsigned selector,
				unsigned group)
{
	struct sirfsoc_pmx *spmx;

	spmx = pinctrl_dev_get_drvdata(pmxdev);
	sirfsoc_pinmux_endisable(spmx, selector, true);

	return 0;
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

static int sirfsoc_pinmux_get_groups(struct pinctrl_dev *pctldev,
				unsigned selector,
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

	muxval = readl(spmx->gpio_virtbase +
		SIRFSOC_GPIO_PAD_EN(group));
	muxval = muxval | (1 << (offset - range->pin_base));
	writel(muxval, spmx->gpio_virtbase +
		SIRFSOC_GPIO_PAD_EN(group));

	return 0;
}

static struct pinmux_ops sirfsoc_pinmux_ops = {
	.set_mux = sirfsoc_pinmux_set_mux,
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

static void __iomem *sirfsoc_rsc_of_iomap(void)
{
	const struct of_device_id rsc_ids[]  = {
		{ .compatible = "sirf,prima2-rsc" },
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

	if (flags)
		*flags = gpiospec->args[1];

	return gpiospec->args[0];
}

static const struct of_device_id pinmux_ids[] = {
	{ .compatible = "sirf,prima2-pinctrl", .data = &prima2_pinctrl_data, },
	{ .compatible = "sirf,atlas6-pinctrl", .data = &atlas6_pinctrl_data, },
	{}
};

static int sirfsoc_pinmux_probe(struct platform_device *pdev)
{
	int ret;
	struct sirfsoc_pmx *spmx;
	struct device_node *np = pdev->dev.of_node;
	const struct sirfsoc_pinctrl_data *pdata;

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

	pdata = of_match_node(pinmux_ids, np)->data;
	sirfsoc_pin_groups = pdata->grps;
	sirfsoc_pingrp_cnt = pdata->grps_cnt;
	sirfsoc_pmx_functions = pdata->funcs;
	sirfsoc_pmxfunc_cnt = pdata->funcs_cnt;
	sirfsoc_pinmux_desc.pins = pdata->pads;
	sirfsoc_pinmux_desc.npins = pdata->pads_cnt;


	/* Now register the pin controller and all pins it handles */
	spmx->pmx = pinctrl_register(&sirfsoc_pinmux_desc, &pdev->dev, spmx);
	if (IS_ERR(spmx->pmx)) {
		dev_err(&pdev->dev, "could not register SIRFSOC pinmux driver\n");
		ret = PTR_ERR(spmx->pmx);
		goto out_no_pmx;
	}

	dev_info(&pdev->dev, "initialized SIRFSOC pinmux driver\n");

	return 0;

out_no_pmx:
	iounmap(spmx->rsc_virtbase);
out_no_rsc_remap:
	iounmap(spmx->gpio_virtbase);
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int sirfsoc_pinmux_suspend_noirq(struct device *dev)
{
	int i, j;
	struct sirfsoc_pmx *spmx = dev_get_drvdata(dev);

	for (i = 0; i < SIRFSOC_GPIO_NO_OF_BANKS; i++) {
		for (j = 0; j < SIRFSOC_GPIO_BANK_SIZE; j++) {
			spmx->gpio_regs[i][j] = readl(spmx->gpio_virtbase +
				SIRFSOC_GPIO_CTRL(i, j));
		}
		spmx->ints_regs[i] = readl(spmx->gpio_virtbase +
			SIRFSOC_GPIO_INT_STATUS(i));
		spmx->paden_regs[i] = readl(spmx->gpio_virtbase +
			SIRFSOC_GPIO_PAD_EN(i));
	}
	spmx->dspen_regs = readl(spmx->gpio_virtbase + SIRFSOC_GPIO_DSP_EN0);

	for (i = 0; i < 3; i++)
		spmx->rsc_regs[i] = readl(spmx->rsc_virtbase + 4 * i);

	return 0;
}

static int sirfsoc_pinmux_resume_noirq(struct device *dev)
{
	int i, j;
	struct sirfsoc_pmx *spmx = dev_get_drvdata(dev);

	for (i = 0; i < SIRFSOC_GPIO_NO_OF_BANKS; i++) {
		for (j = 0; j < SIRFSOC_GPIO_BANK_SIZE; j++) {
			writel(spmx->gpio_regs[i][j], spmx->gpio_virtbase +
				SIRFSOC_GPIO_CTRL(i, j));
		}
		writel(spmx->ints_regs[i], spmx->gpio_virtbase +
			SIRFSOC_GPIO_INT_STATUS(i));
		writel(spmx->paden_regs[i], spmx->gpio_virtbase +
			SIRFSOC_GPIO_PAD_EN(i));
	}
	writel(spmx->dspen_regs, spmx->gpio_virtbase + SIRFSOC_GPIO_DSP_EN0);

	for (i = 0; i < 3; i++)
		writel(spmx->rsc_regs[i], spmx->rsc_virtbase + 4 * i);

	return 0;
}

static const struct dev_pm_ops sirfsoc_pinmux_pm_ops = {
	.suspend_noirq = sirfsoc_pinmux_suspend_noirq,
	.resume_noirq = sirfsoc_pinmux_resume_noirq,
	.freeze_noirq = sirfsoc_pinmux_suspend_noirq,
	.restore_noirq = sirfsoc_pinmux_resume_noirq,
};
#endif

static struct platform_driver sirfsoc_pinmux_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = pinmux_ids,
#ifdef CONFIG_PM_SLEEP
		.pm = &sirfsoc_pinmux_pm_ops,
#endif
	},
	.probe = sirfsoc_pinmux_probe,
};

static int __init sirfsoc_pinmux_init(void)
{
	return platform_driver_register(&sirfsoc_pinmux_driver);
}
arch_initcall(sirfsoc_pinmux_init);

static inline struct sirfsoc_gpio_bank *
sirfsoc_gpio_to_bank(struct sirfsoc_gpio_chip *sgpio, unsigned int offset)
{
	return &sgpio->sgpio_bank[offset / SIRFSOC_GPIO_BANK_SIZE];
}

static inline int sirfsoc_gpio_to_bankoff(unsigned int offset)
{
	return offset % SIRFSOC_GPIO_BANK_SIZE;
}

static void sirfsoc_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct sirfsoc_gpio_chip *sgpio = gpiochip_get_data(gc);
	struct sirfsoc_gpio_bank *bank = sirfsoc_gpio_to_bank(sgpio, d->hwirq);
	int idx = sirfsoc_gpio_to_bankoff(d->hwirq);
	u32 val, offset;
	unsigned long flags;

	offset = SIRFSOC_GPIO_CTRL(bank->id, idx);

	spin_lock_irqsave(&sgpio->lock, flags);

	val = readl(sgpio->chip.regs + offset);

	writel(val, sgpio->chip.regs + offset);

	spin_unlock_irqrestore(&sgpio->lock, flags);
}

static void __sirfsoc_gpio_irq_mask(struct sirfsoc_gpio_chip *sgpio,
				    struct sirfsoc_gpio_bank *bank,
				    int idx)
{
	u32 val, offset;
	unsigned long flags;

	offset = SIRFSOC_GPIO_CTRL(bank->id, idx);

	spin_lock_irqsave(&sgpio->lock, flags);

	val = readl(sgpio->chip.regs + offset);
	val &= ~SIRFSOC_GPIO_CTL_INTR_EN_MASK;
	val &= ~SIRFSOC_GPIO_CTL_INTR_STS_MASK;
	writel(val, sgpio->chip.regs + offset);

	spin_unlock_irqrestore(&sgpio->lock, flags);
}

static void sirfsoc_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct sirfsoc_gpio_chip *sgpio = gpiochip_get_data(gc);
	struct sirfsoc_gpio_bank *bank = sirfsoc_gpio_to_bank(sgpio, d->hwirq);

	__sirfsoc_gpio_irq_mask(sgpio, bank, d->hwirq % SIRFSOC_GPIO_BANK_SIZE);
}

static void sirfsoc_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct sirfsoc_gpio_chip *sgpio = gpiochip_get_data(gc);
	struct sirfsoc_gpio_bank *bank = sirfsoc_gpio_to_bank(sgpio, d->hwirq);
	int idx = sirfsoc_gpio_to_bankoff(d->hwirq);
	u32 val, offset;
	unsigned long flags;

	offset = SIRFSOC_GPIO_CTRL(bank->id, idx);

	spin_lock_irqsave(&sgpio->lock, flags);

	val = readl(sgpio->chip.regs + offset);
	val &= ~SIRFSOC_GPIO_CTL_INTR_STS_MASK;
	val |= SIRFSOC_GPIO_CTL_INTR_EN_MASK;
	writel(val, sgpio->chip.regs + offset);

	spin_unlock_irqrestore(&sgpio->lock, flags);
}

static int sirfsoc_gpio_irq_type(struct irq_data *d, unsigned type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct sirfsoc_gpio_chip *sgpio = gpiochip_get_data(gc);
	struct sirfsoc_gpio_bank *bank = sirfsoc_gpio_to_bank(sgpio, d->hwirq);
	int idx = sirfsoc_gpio_to_bankoff(d->hwirq);
	u32 val, offset;
	unsigned long flags;

	offset = SIRFSOC_GPIO_CTRL(bank->id, idx);

	spin_lock_irqsave(&sgpio->lock, flags);

	val = readl(sgpio->chip.regs + offset);
	val &= ~(SIRFSOC_GPIO_CTL_INTR_STS_MASK | SIRFSOC_GPIO_CTL_OUT_EN_MASK);

	switch (type) {
	case IRQ_TYPE_NONE:
		break;
	case IRQ_TYPE_EDGE_RISING:
		val |= SIRFSOC_GPIO_CTL_INTR_HIGH_MASK |
			SIRFSOC_GPIO_CTL_INTR_TYPE_MASK;
		val &= ~SIRFSOC_GPIO_CTL_INTR_LOW_MASK;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		val &= ~SIRFSOC_GPIO_CTL_INTR_HIGH_MASK;
		val |= SIRFSOC_GPIO_CTL_INTR_LOW_MASK |
			SIRFSOC_GPIO_CTL_INTR_TYPE_MASK;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		val |= SIRFSOC_GPIO_CTL_INTR_HIGH_MASK |
			SIRFSOC_GPIO_CTL_INTR_LOW_MASK |
			SIRFSOC_GPIO_CTL_INTR_TYPE_MASK;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		val &= ~(SIRFSOC_GPIO_CTL_INTR_HIGH_MASK |
			SIRFSOC_GPIO_CTL_INTR_TYPE_MASK);
		val |= SIRFSOC_GPIO_CTL_INTR_LOW_MASK;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		val |= SIRFSOC_GPIO_CTL_INTR_HIGH_MASK;
		val &= ~(SIRFSOC_GPIO_CTL_INTR_LOW_MASK |
			SIRFSOC_GPIO_CTL_INTR_TYPE_MASK);
		break;
	}

	writel(val, sgpio->chip.regs + offset);

	spin_unlock_irqrestore(&sgpio->lock, flags);

	return 0;
}

static struct irq_chip sirfsoc_irq_chip = {
	.name = "sirf-gpio-irq",
	.irq_ack = sirfsoc_gpio_irq_ack,
	.irq_mask = sirfsoc_gpio_irq_mask,
	.irq_unmask = sirfsoc_gpio_irq_unmask,
	.irq_set_type = sirfsoc_gpio_irq_type,
};

static void sirfsoc_gpio_handle_irq(struct irq_desc *desc)
{
	unsigned int irq = irq_desc_get_irq(desc);
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct sirfsoc_gpio_chip *sgpio = gpiochip_get_data(gc);
	struct sirfsoc_gpio_bank *bank;
	u32 status, ctrl;
	int idx = 0;
	struct irq_chip *chip = irq_desc_get_chip(desc);
	int i;

	for (i = 0; i < SIRFSOC_GPIO_NO_OF_BANKS; i++) {
		bank = &sgpio->sgpio_bank[i];
		if (bank->parent_irq == irq)
			break;
	}
	BUG_ON(i == SIRFSOC_GPIO_NO_OF_BANKS);

	chained_irq_enter(chip, desc);

	status = readl(sgpio->chip.regs + SIRFSOC_GPIO_INT_STATUS(bank->id));
	if (!status) {
		printk(KERN_WARNING
			"%s: gpio id %d status %#x no interrupt is flagged\n",
			__func__, bank->id, status);
		handle_bad_irq(desc);
		return;
	}

	while (status) {
		ctrl = readl(sgpio->chip.regs + SIRFSOC_GPIO_CTRL(bank->id, idx));

		/*
		 * Here we must check whether the corresponding GPIO's interrupt
		 * has been enabled, otherwise just skip it
		 */
		if ((status & 0x1) && (ctrl & SIRFSOC_GPIO_CTL_INTR_EN_MASK)) {
			pr_debug("%s: gpio id %d idx %d happens\n",
				__func__, bank->id, idx);
			generic_handle_irq(irq_find_mapping(gc->irqdomain, idx +
					bank->id * SIRFSOC_GPIO_BANK_SIZE));
		}

		idx++;
		status = status >> 1;
	}

	chained_irq_exit(chip, desc);
}

static inline void sirfsoc_gpio_set_input(struct sirfsoc_gpio_chip *sgpio,
					  unsigned ctrl_offset)
{
	u32 val;

	val = readl(sgpio->chip.regs + ctrl_offset);
	val &= ~SIRFSOC_GPIO_CTL_OUT_EN_MASK;
	writel(val, sgpio->chip.regs + ctrl_offset);
}

static int sirfsoc_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct sirfsoc_gpio_chip *sgpio = gpiochip_get_data(chip);
	struct sirfsoc_gpio_bank *bank = sirfsoc_gpio_to_bank(sgpio, offset);
	unsigned long flags;

	if (pinctrl_request_gpio(chip->base + offset))
		return -ENODEV;

	spin_lock_irqsave(&bank->lock, flags);

	/*
	 * default status:
	 * set direction as input and mask irq
	 */
	sirfsoc_gpio_set_input(sgpio, SIRFSOC_GPIO_CTRL(bank->id, offset));
	__sirfsoc_gpio_irq_mask(sgpio, bank, offset);

	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static void sirfsoc_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	struct sirfsoc_gpio_chip *sgpio = gpiochip_get_data(chip);
	struct sirfsoc_gpio_bank *bank = sirfsoc_gpio_to_bank(sgpio, offset);
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);

	__sirfsoc_gpio_irq_mask(sgpio, bank, offset);
	sirfsoc_gpio_set_input(sgpio, SIRFSOC_GPIO_CTRL(bank->id, offset));

	spin_unlock_irqrestore(&bank->lock, flags);

	pinctrl_free_gpio(chip->base + offset);
}

static int sirfsoc_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	struct sirfsoc_gpio_chip *sgpio = gpiochip_get_data(chip);
	struct sirfsoc_gpio_bank *bank = sirfsoc_gpio_to_bank(sgpio, gpio);
	int idx = sirfsoc_gpio_to_bankoff(gpio);
	unsigned long flags;
	unsigned offset;

	offset = SIRFSOC_GPIO_CTRL(bank->id, idx);

	spin_lock_irqsave(&bank->lock, flags);

	sirfsoc_gpio_set_input(sgpio, offset);

	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static inline void sirfsoc_gpio_set_output(struct sirfsoc_gpio_chip *sgpio,
					   struct sirfsoc_gpio_bank *bank,
					   unsigned offset,
					   int value)
{
	u32 out_ctrl;
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);

	out_ctrl = readl(sgpio->chip.regs + offset);
	if (value)
		out_ctrl |= SIRFSOC_GPIO_CTL_DATAOUT_MASK;
	else
		out_ctrl &= ~SIRFSOC_GPIO_CTL_DATAOUT_MASK;

	out_ctrl &= ~SIRFSOC_GPIO_CTL_INTR_EN_MASK;
	out_ctrl |= SIRFSOC_GPIO_CTL_OUT_EN_MASK;
	writel(out_ctrl, sgpio->chip.regs + offset);

	spin_unlock_irqrestore(&bank->lock, flags);
}

static int sirfsoc_gpio_direction_output(struct gpio_chip *chip,
	unsigned gpio, int value)
{
	struct sirfsoc_gpio_chip *sgpio = gpiochip_get_data(chip);
	struct sirfsoc_gpio_bank *bank = sirfsoc_gpio_to_bank(sgpio, gpio);
	int idx = sirfsoc_gpio_to_bankoff(gpio);
	u32 offset;
	unsigned long flags;

	offset = SIRFSOC_GPIO_CTRL(bank->id, idx);

	spin_lock_irqsave(&sgpio->lock, flags);

	sirfsoc_gpio_set_output(sgpio, bank, offset, value);

	spin_unlock_irqrestore(&sgpio->lock, flags);

	return 0;
}

static int sirfsoc_gpio_get_value(struct gpio_chip *chip, unsigned offset)
{
	struct sirfsoc_gpio_chip *sgpio = gpiochip_get_data(chip);
	struct sirfsoc_gpio_bank *bank = sirfsoc_gpio_to_bank(sgpio, offset);
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);

	val = readl(sgpio->chip.regs + SIRFSOC_GPIO_CTRL(bank->id, offset));

	spin_unlock_irqrestore(&bank->lock, flags);

	return !!(val & SIRFSOC_GPIO_CTL_DATAIN_MASK);
}

static void sirfsoc_gpio_set_value(struct gpio_chip *chip, unsigned offset,
	int value)
{
	struct sirfsoc_gpio_chip *sgpio = gpiochip_get_data(chip);
	struct sirfsoc_gpio_bank *bank = sirfsoc_gpio_to_bank(sgpio, offset);
	u32 ctrl;
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);

	ctrl = readl(sgpio->chip.regs + SIRFSOC_GPIO_CTRL(bank->id, offset));
	if (value)
		ctrl |= SIRFSOC_GPIO_CTL_DATAOUT_MASK;
	else
		ctrl &= ~SIRFSOC_GPIO_CTL_DATAOUT_MASK;
	writel(ctrl, sgpio->chip.regs + SIRFSOC_GPIO_CTRL(bank->id, offset));

	spin_unlock_irqrestore(&bank->lock, flags);
}

static void sirfsoc_gpio_set_pullup(struct sirfsoc_gpio_chip *sgpio,
				    const u32 *pullups)
{
	int i, n;
	const unsigned long *p = (const unsigned long *)pullups;

	for (i = 0; i < SIRFSOC_GPIO_NO_OF_BANKS; i++) {
		for_each_set_bit(n, p + i, BITS_PER_LONG) {
			u32 offset = SIRFSOC_GPIO_CTRL(i, n);
			u32 val = readl(sgpio->chip.regs + offset);
			val |= SIRFSOC_GPIO_CTL_PULL_MASK;
			val |= SIRFSOC_GPIO_CTL_PULL_HIGH;
			writel(val, sgpio->chip.regs + offset);
		}
	}
}

static void sirfsoc_gpio_set_pulldown(struct sirfsoc_gpio_chip *sgpio,
				      const u32 *pulldowns)
{
	int i, n;
	const unsigned long *p = (const unsigned long *)pulldowns;

	for (i = 0; i < SIRFSOC_GPIO_NO_OF_BANKS; i++) {
		for_each_set_bit(n, p + i, BITS_PER_LONG) {
			u32 offset = SIRFSOC_GPIO_CTRL(i, n);
			u32 val = readl(sgpio->chip.regs + offset);
			val |= SIRFSOC_GPIO_CTL_PULL_MASK;
			val &= ~SIRFSOC_GPIO_CTL_PULL_HIGH;
			writel(val, sgpio->chip.regs + offset);
		}
	}
}

static int sirfsoc_gpio_probe(struct device_node *np)
{
	int i, err = 0;
	static struct sirfsoc_gpio_chip *sgpio;
	struct sirfsoc_gpio_bank *bank;
	void __iomem *regs;
	struct platform_device *pdev;

	u32 pullups[SIRFSOC_GPIO_NO_OF_BANKS], pulldowns[SIRFSOC_GPIO_NO_OF_BANKS];

	pdev = of_find_device_by_node(np);
	if (!pdev)
		return -ENODEV;

	sgpio = devm_kzalloc(&pdev->dev, sizeof(*sgpio), GFP_KERNEL);
	if (!sgpio)
		return -ENOMEM;
	spin_lock_init(&sgpio->lock);

	regs = of_iomap(np, 0);
	if (!regs)
		return -ENOMEM;

	sgpio->chip.gc.request = sirfsoc_gpio_request;
	sgpio->chip.gc.free = sirfsoc_gpio_free;
	sgpio->chip.gc.direction_input = sirfsoc_gpio_direction_input;
	sgpio->chip.gc.get = sirfsoc_gpio_get_value;
	sgpio->chip.gc.direction_output = sirfsoc_gpio_direction_output;
	sgpio->chip.gc.set = sirfsoc_gpio_set_value;
	sgpio->chip.gc.base = 0;
	sgpio->chip.gc.ngpio = SIRFSOC_GPIO_BANK_SIZE * SIRFSOC_GPIO_NO_OF_BANKS;
	sgpio->chip.gc.label = kstrdup(np->full_name, GFP_KERNEL);
	sgpio->chip.gc.of_node = np;
	sgpio->chip.gc.of_xlate = sirfsoc_gpio_of_xlate;
	sgpio->chip.gc.of_gpio_n_cells = 2;
	sgpio->chip.gc.parent = &pdev->dev;
	sgpio->chip.regs = regs;

	err = gpiochip_add_data(&sgpio->chip.gc, sgpio);
	if (err) {
		dev_err(&pdev->dev, "%s: error in probe function with status %d\n",
			np->full_name, err);
		goto out;
	}

	err =  gpiochip_irqchip_add(&sgpio->chip.gc,
		&sirfsoc_irq_chip,
		0, handle_level_irq,
		IRQ_TYPE_NONE);
	if (err) {
		dev_err(&pdev->dev,
			"could not connect irqchip to gpiochip\n");
		goto out_banks;
	}

	for (i = 0; i < SIRFSOC_GPIO_NO_OF_BANKS; i++) {
		bank = &sgpio->sgpio_bank[i];
		spin_lock_init(&bank->lock);
		bank->parent_irq = platform_get_irq(pdev, i);
		if (bank->parent_irq < 0) {
			err = bank->parent_irq;
			goto out_banks;
		}

		gpiochip_set_chained_irqchip(&sgpio->chip.gc,
			&sirfsoc_irq_chip,
			bank->parent_irq,
			sirfsoc_gpio_handle_irq);
	}

	err = gpiochip_add_pin_range(&sgpio->chip.gc, dev_name(&pdev->dev),
		0, 0, SIRFSOC_GPIO_BANK_SIZE * SIRFSOC_GPIO_NO_OF_BANKS);
	if (err) {
		dev_err(&pdev->dev,
			"could not add gpiochip pin range\n");
		goto out_no_range;
	}

	if (!of_property_read_u32_array(np, "sirf,pullups", pullups,
		SIRFSOC_GPIO_NO_OF_BANKS))
		sirfsoc_gpio_set_pullup(sgpio, pullups);

	if (!of_property_read_u32_array(np, "sirf,pulldowns", pulldowns,
		SIRFSOC_GPIO_NO_OF_BANKS))
		sirfsoc_gpio_set_pulldown(sgpio, pulldowns);

	return 0;

out_no_range:
out_banks:
	gpiochip_remove(&sgpio->chip.gc);
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

MODULE_AUTHOR("Rongjun Ying <rongjun.ying@csr.com>");
MODULE_AUTHOR("Yuping Luo <yuping.luo@csr.com>");
MODULE_AUTHOR("Barry Song <baohua.song@csr.com>");
MODULE_DESCRIPTION("SIRFSOC pin control driver");
MODULE_LICENSE("GPL");
