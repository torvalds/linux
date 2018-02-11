/*
 * Arizona interrupt support
 *
 * Copyright 2012 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <linux/mfd/arizona/core.h>
#include <linux/mfd/arizona/registers.h>

#include "arizona.h"

#define ARIZONA_AOD_IRQ_INDEX 0
#define ARIZONA_MAIN_IRQ_INDEX 1

static int arizona_map_irq(struct arizona *arizona, int irq)
{
	int ret;

	if (arizona->aod_irq_chip) {
		ret = regmap_irq_get_virq(arizona->aod_irq_chip, irq);
		if (ret >= 0)
			return ret;
	}

	return regmap_irq_get_virq(arizona->irq_chip, irq);
}

int arizona_request_irq(struct arizona *arizona, int irq, char *name,
			   irq_handler_t handler, void *data)
{
	irq = arizona_map_irq(arizona, irq);
	if (irq < 0)
		return irq;

	return request_threaded_irq(irq, NULL, handler, IRQF_ONESHOT,
				    name, data);
}
EXPORT_SYMBOL_GPL(arizona_request_irq);

void arizona_free_irq(struct arizona *arizona, int irq, void *data)
{
	irq = arizona_map_irq(arizona, irq);
	if (irq < 0)
		return;

	free_irq(irq, data);
}
EXPORT_SYMBOL_GPL(arizona_free_irq);

int arizona_set_irq_wake(struct arizona *arizona, int irq, int on)
{
	irq = arizona_map_irq(arizona, irq);
	if (irq < 0)
		return irq;

	return irq_set_irq_wake(irq, on);
}
EXPORT_SYMBOL_GPL(arizona_set_irq_wake);

static irqreturn_t arizona_boot_done(int irq, void *data)
{
	struct arizona *arizona = data;

	dev_dbg(arizona->dev, "Boot done\n");

	return IRQ_HANDLED;
}

static irqreturn_t arizona_ctrlif_err(int irq, void *data)
{
	struct arizona *arizona = data;

	/*
	 * For pretty much all potential sources a register cache sync
	 * won't help, we've just got a software bug somewhere.
	 */
	dev_err(arizona->dev, "Control interface error\n");

	return IRQ_HANDLED;
}

static irqreturn_t arizona_irq_thread(int irq, void *data)
{
	struct arizona *arizona = data;
	bool poll;
	unsigned int val;
	int ret;

	ret = pm_runtime_get_sync(arizona->dev);
	if (ret < 0) {
		dev_err(arizona->dev, "Failed to resume device: %d\n", ret);
		return IRQ_NONE;
	}

	do {
		poll = false;

		if (arizona->aod_irq_chip) {
			/*
			 * Check the AOD status register to determine whether
			 * the nested IRQ handler should be called.
			 */
			ret = regmap_read(arizona->regmap,
					  ARIZONA_AOD_IRQ1, &val);
			if (ret)
				dev_warn(arizona->dev,
					"Failed to read AOD IRQ1 %d\n", ret);
			else if (val)
				handle_nested_irq(
					irq_find_mapping(arizona->virq, 0));
		}

		/*
		 * Check if one of the main interrupts is asserted and only
		 * check that domain if it is.
		 */
		ret = regmap_read(arizona->regmap, ARIZONA_IRQ_PIN_STATUS,
				  &val);
		if (ret == 0 && val & ARIZONA_IRQ1_STS) {
			handle_nested_irq(irq_find_mapping(arizona->virq, 1));
		} else if (ret != 0) {
			dev_err(arizona->dev,
				"Failed to read main IRQ status: %d\n", ret);
		}

		/*
		 * Poll the IRQ pin status to see if we're really done
		 * if the interrupt controller can't do it for us.
		 */
		if (!arizona->pdata.irq_gpio) {
			break;
		} else if (arizona->pdata.irq_flags & IRQF_TRIGGER_RISING &&
			   gpio_get_value_cansleep(arizona->pdata.irq_gpio)) {
			poll = true;
		} else if (arizona->pdata.irq_flags & IRQF_TRIGGER_FALLING &&
			   !gpio_get_value_cansleep(arizona->pdata.irq_gpio)) {
			poll = true;
		}
	} while (poll);

	pm_runtime_mark_last_busy(arizona->dev);
	pm_runtime_put_autosuspend(arizona->dev);

	return IRQ_HANDLED;
}

static void arizona_irq_enable(struct irq_data *data)
{
}

static void arizona_irq_disable(struct irq_data *data)
{
}

static int arizona_irq_set_wake(struct irq_data *data, unsigned int on)
{
	struct arizona *arizona = irq_data_get_irq_chip_data(data);

	return irq_set_irq_wake(arizona->irq, on);
}

static struct irq_chip arizona_irq_chip = {
	.name			= "arizona",
	.irq_disable		= arizona_irq_disable,
	.irq_enable		= arizona_irq_enable,
	.irq_set_wake		= arizona_irq_set_wake,
};

static struct lock_class_key arizona_irq_lock_class;
static struct lock_class_key arizona_irq_request_class;

static int arizona_irq_map(struct irq_domain *h, unsigned int virq,
			      irq_hw_number_t hw)
{
	struct arizona *data = h->host_data;

	irq_set_chip_data(virq, data);
	irq_set_lockdep_class(virq, &arizona_irq_lock_class,
		&arizona_irq_request_class);
	irq_set_chip_and_handler(virq, &arizona_irq_chip, handle_simple_irq);
	irq_set_nested_thread(virq, 1);
	irq_set_noprobe(virq);

	return 0;
}

static const struct irq_domain_ops arizona_domain_ops = {
	.map	= arizona_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

int arizona_irq_init(struct arizona *arizona)
{
	int flags = IRQF_ONESHOT;
	int ret;
	const struct regmap_irq_chip *aod, *irq;
	struct irq_data *irq_data;
	unsigned int virq;

	arizona->ctrlif_error = true;

	switch (arizona->type) {
#ifdef CONFIG_MFD_WM5102
	case WM5102:
		aod = &wm5102_aod;
		irq = &wm5102_irq;

		arizona->ctrlif_error = false;
		break;
#endif
#ifdef CONFIG_MFD_WM5110
	case WM5110:
	case WM8280:
		aod = &wm5110_aod;

		switch (arizona->rev) {
		case 0 ... 2:
			irq = &wm5110_irq;
			break;
		default:
			irq = &wm5110_revd_irq;
			break;
		}

		arizona->ctrlif_error = false;
		break;
#endif
#ifdef CONFIG_MFD_CS47L24
	case WM1831:
	case CS47L24:
		aod = NULL;
		irq = &cs47l24_irq;

		arizona->ctrlif_error = false;
		break;
#endif
#ifdef CONFIG_MFD_WM8997
	case WM8997:
		aod = &wm8997_aod;
		irq = &wm8997_irq;

		arizona->ctrlif_error = false;
		break;
#endif
#ifdef CONFIG_MFD_WM8998
	case WM8998:
	case WM1814:
		aod = &wm8998_aod;
		irq = &wm8998_irq;

		arizona->ctrlif_error = false;
		break;
#endif
	default:
		BUG_ON("Unknown Arizona class device" == NULL);
		return -EINVAL;
	}

	/* Disable all wake sources by default */
	regmap_write(arizona->regmap, ARIZONA_WAKE_CONTROL, 0);

	/* Read the flags from the interrupt controller if not specified */
	if (!arizona->pdata.irq_flags) {
		irq_data = irq_get_irq_data(arizona->irq);
		if (!irq_data) {
			dev_err(arizona->dev, "Invalid IRQ: %d\n",
				arizona->irq);
			return -EINVAL;
		}

		arizona->pdata.irq_flags = irqd_get_trigger_type(irq_data);
		switch (arizona->pdata.irq_flags) {
		case IRQF_TRIGGER_LOW:
		case IRQF_TRIGGER_HIGH:
		case IRQF_TRIGGER_RISING:
		case IRQF_TRIGGER_FALLING:
			break;

		case IRQ_TYPE_NONE:
		default:
			/* Device default */
			arizona->pdata.irq_flags = IRQF_TRIGGER_LOW;
			break;
		}
	}

	if (arizona->pdata.irq_flags & (IRQF_TRIGGER_HIGH |
					IRQF_TRIGGER_RISING)) {
		ret = regmap_update_bits(arizona->regmap, ARIZONA_IRQ_CTRL_1,
					 ARIZONA_IRQ_POL, 0);
		if (ret != 0) {
			dev_err(arizona->dev, "Couldn't set IRQ polarity: %d\n",
				ret);
			goto err;
		}
	}

	flags |= arizona->pdata.irq_flags;

	/* Allocate a virtual IRQ domain to distribute to the regmap domains */
	arizona->virq = irq_domain_add_linear(NULL, 2, &arizona_domain_ops,
					      arizona);
	if (!arizona->virq) {
		dev_err(arizona->dev, "Failed to add core IRQ domain\n");
		ret = -EINVAL;
		goto err;
	}

	if (aod) {
		virq = irq_create_mapping(arizona->virq, ARIZONA_AOD_IRQ_INDEX);
		if (!virq) {
			dev_err(arizona->dev, "Failed to map AOD IRQs\n");
			ret = -EINVAL;
			goto err_domain;
		}

		ret = regmap_add_irq_chip(arizona->regmap, virq, IRQF_ONESHOT,
					  0, aod, &arizona->aod_irq_chip);
		if (ret != 0) {
			dev_err(arizona->dev,
				"Failed to add AOD IRQs: %d\n", ret);
			goto err_map_aod;
		}
	}

	virq = irq_create_mapping(arizona->virq, ARIZONA_MAIN_IRQ_INDEX);
	if (!virq) {
		dev_err(arizona->dev, "Failed to map main IRQs\n");
		ret = -EINVAL;
		goto err_aod;
	}

	ret = regmap_add_irq_chip(arizona->regmap, virq, IRQF_ONESHOT,
				  0, irq, &arizona->irq_chip);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to add main IRQs: %d\n", ret);
		goto err_map_main_irq;
	}

	/* Used to emulate edge trigger and to work around broken pinmux */
	if (arizona->pdata.irq_gpio) {
		if (gpio_to_irq(arizona->pdata.irq_gpio) != arizona->irq) {
			dev_warn(arizona->dev, "IRQ %d is not GPIO %d (%d)\n",
				 arizona->irq, arizona->pdata.irq_gpio,
				 gpio_to_irq(arizona->pdata.irq_gpio));
			arizona->irq = gpio_to_irq(arizona->pdata.irq_gpio);
		}

		ret = devm_gpio_request_one(arizona->dev,
					    arizona->pdata.irq_gpio,
					    GPIOF_IN, "arizona IRQ");
		if (ret != 0) {
			dev_err(arizona->dev,
				"Failed to request IRQ GPIO %d:: %d\n",
				arizona->pdata.irq_gpio, ret);
			arizona->pdata.irq_gpio = 0;
		}
	}

	ret = request_threaded_irq(arizona->irq, NULL, arizona_irq_thread,
				   flags, "arizona", arizona);

	if (ret != 0) {
		dev_err(arizona->dev, "Failed to request primary IRQ %d: %d\n",
			arizona->irq, ret);
		goto err_main_irq;
	}

	/* Make sure the boot done IRQ is unmasked for resumes */
	ret = arizona_request_irq(arizona, ARIZONA_IRQ_BOOT_DONE, "Boot done",
				  arizona_boot_done, arizona);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to request boot done %d: %d\n",
			arizona->irq, ret);
		goto err_boot_done;
	}

	/* Handle control interface errors in the core */
	if (arizona->ctrlif_error) {
		ret = arizona_request_irq(arizona, ARIZONA_IRQ_CTRLIF_ERR,
					  "Control interface error",
					  arizona_ctrlif_err, arizona);
		if (ret != 0) {
			dev_err(arizona->dev,
				"Failed to request CTRLIF_ERR %d: %d\n",
				arizona->irq, ret);
			goto err_ctrlif;
		}
	}

	return 0;

err_ctrlif:
	arizona_free_irq(arizona, ARIZONA_IRQ_BOOT_DONE, arizona);
err_boot_done:
	free_irq(arizona->irq, arizona);
err_main_irq:
	regmap_del_irq_chip(irq_find_mapping(arizona->virq,
					     ARIZONA_MAIN_IRQ_INDEX),
			    arizona->irq_chip);
err_map_main_irq:
	irq_dispose_mapping(irq_find_mapping(arizona->virq,
					     ARIZONA_MAIN_IRQ_INDEX));
err_aod:
	regmap_del_irq_chip(irq_find_mapping(arizona->virq,
					     ARIZONA_AOD_IRQ_INDEX),
			    arizona->aod_irq_chip);
err_map_aod:
	irq_dispose_mapping(irq_find_mapping(arizona->virq,
					     ARIZONA_AOD_IRQ_INDEX));
err_domain:
	irq_domain_remove(arizona->virq);
err:
	return ret;
}

int arizona_irq_exit(struct arizona *arizona)
{
	unsigned int virq;

	if (arizona->ctrlif_error)
		arizona_free_irq(arizona, ARIZONA_IRQ_CTRLIF_ERR, arizona);
	arizona_free_irq(arizona, ARIZONA_IRQ_BOOT_DONE, arizona);

	virq = irq_find_mapping(arizona->virq, ARIZONA_MAIN_IRQ_INDEX);
	regmap_del_irq_chip(virq, arizona->irq_chip);
	irq_dispose_mapping(virq);

	virq = irq_find_mapping(arizona->virq, ARIZONA_AOD_IRQ_INDEX);
	regmap_del_irq_chip(virq, arizona->aod_irq_chip);
	irq_dispose_mapping(virq);

	irq_domain_remove(arizona->virq);

	free_irq(arizona->irq, arizona);

	return 0;
}
