/*
 * wm8994-irq.c  --  Interrupt controller support for Wolfson WM8994
 *
 * Copyright 2010 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/mfd/core.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/regmap.h>

#include <linux/mfd/wm8994/core.h>
#include <linux/mfd/wm8994/pdata.h>
#include <linux/mfd/wm8994/registers.h>

#include <linux/delay.h>

static struct regmap_irq wm8994_irqs[] = {
	[WM8994_IRQ_TEMP_SHUT] = {
		.reg_offset = 1,
		.mask = WM8994_TEMP_SHUT_EINT,
	},
	[WM8994_IRQ_MIC1_DET] = {
		.reg_offset = 1,
		.mask = WM8994_MIC1_DET_EINT,
	},
	[WM8994_IRQ_MIC1_SHRT] = {
		.reg_offset = 1,
		.mask = WM8994_MIC1_SHRT_EINT,
	},
	[WM8994_IRQ_MIC2_DET] = {
		.reg_offset = 1,
		.mask = WM8994_MIC2_DET_EINT,
	},
	[WM8994_IRQ_MIC2_SHRT] = {
		.reg_offset = 1,
		.mask = WM8994_MIC2_SHRT_EINT,
	},
	[WM8994_IRQ_FLL1_LOCK] = {
		.reg_offset = 1,
		.mask = WM8994_FLL1_LOCK_EINT,
	},
	[WM8994_IRQ_FLL2_LOCK] = {
		.reg_offset = 1,
		.mask = WM8994_FLL2_LOCK_EINT,
	},
	[WM8994_IRQ_SRC1_LOCK] = {
		.reg_offset = 1,
		.mask = WM8994_SRC1_LOCK_EINT,
	},
	[WM8994_IRQ_SRC2_LOCK] = {
		.reg_offset = 1,
		.mask = WM8994_SRC2_LOCK_EINT,
	},
	[WM8994_IRQ_AIF1DRC1_SIG_DET] = {
		.reg_offset = 1,
		.mask = WM8994_AIF1DRC1_SIG_DET,
	},
	[WM8994_IRQ_AIF1DRC2_SIG_DET] = {
		.reg_offset = 1,
		.mask = WM8994_AIF1DRC2_SIG_DET_EINT,
	},
	[WM8994_IRQ_AIF2DRC_SIG_DET] = {
		.reg_offset = 1,
		.mask = WM8994_AIF2DRC_SIG_DET_EINT,
	},
	[WM8994_IRQ_FIFOS_ERR] = {
		.reg_offset = 1,
		.mask = WM8994_FIFOS_ERR_EINT,
	},
	[WM8994_IRQ_WSEQ_DONE] = {
		.reg_offset = 1,
		.mask = WM8994_WSEQ_DONE_EINT,
	},
	[WM8994_IRQ_DCS_DONE] = {
		.reg_offset = 1,
		.mask = WM8994_DCS_DONE_EINT,
	},
	[WM8994_IRQ_TEMP_WARN] = {
		.reg_offset = 1,
		.mask = WM8994_TEMP_WARN_EINT,
	},
	[WM8994_IRQ_GPIO(1)] = {
		.mask = WM8994_GP1_EINT,
	},
	[WM8994_IRQ_GPIO(2)] = {
		.mask = WM8994_GP2_EINT,
	},
	[WM8994_IRQ_GPIO(3)] = {
		.mask = WM8994_GP3_EINT,
	},
	[WM8994_IRQ_GPIO(4)] = {
		.mask = WM8994_GP4_EINT,
	},
	[WM8994_IRQ_GPIO(5)] = {
		.mask = WM8994_GP5_EINT,
	},
	[WM8994_IRQ_GPIO(6)] = {
		.mask = WM8994_GP6_EINT,
	},
	[WM8994_IRQ_GPIO(7)] = {
		.mask = WM8994_GP7_EINT,
	},
	[WM8994_IRQ_GPIO(8)] = {
		.mask = WM8994_GP8_EINT,
	},
	[WM8994_IRQ_GPIO(9)] = {
		.mask = WM8994_GP8_EINT,
	},
	[WM8994_IRQ_GPIO(10)] = {
		.mask = WM8994_GP10_EINT,
	},
	[WM8994_IRQ_GPIO(11)] = {
		.mask = WM8994_GP11_EINT,
	},
};

static struct regmap_irq_chip wm8994_irq_chip = {
	.name = "wm8994",
	.irqs = wm8994_irqs,
	.num_irqs = ARRAY_SIZE(wm8994_irqs),

	.num_regs = 2,
	.status_base = WM8994_INTERRUPT_STATUS_1,
	.mask_base = WM8994_INTERRUPT_STATUS_1_MASK,
	.ack_base = WM8994_INTERRUPT_STATUS_1,
	.runtime_pm = true,
};

static void wm8994_edge_irq_enable(struct irq_data *data)
{
}

static void wm8994_edge_irq_disable(struct irq_data *data)
{
}

static struct irq_chip wm8994_edge_irq_chip = {
	.name			= "wm8994_edge",
	.irq_disable		= wm8994_edge_irq_disable,
	.irq_enable		= wm8994_edge_irq_enable,
};

static irqreturn_t wm8994_edge_irq(int irq, void *data)
{
	struct wm8994 *wm8994 = data;

	while (gpio_get_value_cansleep(wm8994->pdata.irq_gpio))
		handle_nested_irq(irq_create_mapping(wm8994->edge_irq, 0));

	return IRQ_HANDLED;
}

static int wm8994_edge_irq_map(struct irq_domain *h, unsigned int virq,
			       irq_hw_number_t hw)
{
	struct wm8994 *wm8994 = h->host_data;

	irq_set_chip_data(virq, wm8994);
	irq_set_chip_and_handler(virq, &wm8994_edge_irq_chip, handle_edge_irq);
	irq_set_nested_thread(virq, 1);

	/* ARM needs us to explicitly flag the IRQ as valid
	 * and will set them noprobe when we do so. */
#ifdef CONFIG_ARM
	set_irq_flags(virq, IRQF_VALID);
#else
	irq_set_noprobe(virq);
#endif

	return 0;
}

static struct irq_domain_ops wm8994_edge_irq_ops = {
	.map	= wm8994_edge_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

int wm8994_irq_init(struct wm8994 *wm8994)
{
	int ret;
	unsigned long irqflags;
	struct wm8994_pdata *pdata = dev_get_platdata(wm8994->dev);

	if (!wm8994->irq) {
		dev_warn(wm8994->dev,
			 "No interrupt specified, no interrupts\n");
		wm8994->irq_base = 0;
		return 0;
	}

	/* select user or default irq flags */
	irqflags = IRQF_TRIGGER_HIGH | IRQF_ONESHOT;
	if (pdata->irq_flags)
		irqflags = pdata->irq_flags;

	/* use a GPIO for edge triggered controllers */
	if (irqflags & (IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING)) {
		if (gpio_to_irq(pdata->irq_gpio) != wm8994->irq) {
			dev_warn(wm8994->dev, "IRQ %d is not GPIO %d (%d)\n",
				 wm8994->irq, pdata->irq_gpio,
				 gpio_to_irq(pdata->irq_gpio));
			wm8994->irq = gpio_to_irq(pdata->irq_gpio);
		}

		ret = devm_gpio_request_one(wm8994->dev, pdata->irq_gpio,
					    GPIOF_IN, "WM8994 IRQ");

		if (ret != 0) {
			dev_err(wm8994->dev, "Failed to get IRQ GPIO: %d\n",
				ret);
			return ret;
		}

		wm8994->edge_irq = irq_domain_add_linear(NULL, 1,
							 &wm8994_edge_irq_ops,
							 wm8994);

		ret = regmap_add_irq_chip(wm8994->regmap,
					  irq_create_mapping(wm8994->edge_irq,
							     0),
					  IRQF_ONESHOT,
					  wm8994->irq_base, &wm8994_irq_chip,
					  &wm8994->irq_data);
		if (ret != 0) {
			dev_err(wm8994->dev, "Failed to get IRQ: %d\n",
				ret);
			return ret;
		}

		ret = request_threaded_irq(wm8994->irq,
					   NULL, wm8994_edge_irq,
					   irqflags,
					   "WM8994 edge", wm8994);
	} else {
		ret = regmap_add_irq_chip(wm8994->regmap, wm8994->irq,
					  irqflags,
					  wm8994->irq_base, &wm8994_irq_chip,
					  &wm8994->irq_data);
	}

	if (ret != 0) {
		dev_err(wm8994->dev, "Failed to register IRQ chip: %d\n", ret);
		return ret;
	}

	/* Enable top level interrupt if it was masked */
	wm8994_reg_write(wm8994, WM8994_INTERRUPT_CONTROL, 0);

	return 0;
}
EXPORT_SYMBOL(wm8994_irq_init);

void wm8994_irq_exit(struct wm8994 *wm8994)
{
	regmap_del_irq_chip(wm8994->irq, wm8994->irq_data);
}
EXPORT_SYMBOL(wm8994_irq_exit);
