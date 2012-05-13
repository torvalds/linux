/*
 * regmap based irq_chip
 *
 * Copyright 2011 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/export.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/slab.h>

#include "internal.h"

struct regmap_irq_chip_data {
	struct mutex lock;

	struct regmap *map;
	struct regmap_irq_chip *chip;

	int irq_base;
	struct irq_domain *domain;

	void *status_reg_buf;
	unsigned int *status_buf;
	unsigned int *mask_buf;
	unsigned int *mask_buf_def;
};

static inline const
struct regmap_irq *irq_to_regmap_irq(struct regmap_irq_chip_data *data,
				     int irq)
{
	return &data->chip->irqs[irq];
}

static void regmap_irq_lock(struct irq_data *data)
{
	struct regmap_irq_chip_data *d = irq_data_get_irq_chip_data(data);

	mutex_lock(&d->lock);
}

static void regmap_irq_sync_unlock(struct irq_data *data)
{
	struct regmap_irq_chip_data *d = irq_data_get_irq_chip_data(data);
	struct regmap *map = d->map;
	int i, ret;

	/*
	 * If there's been a change in the mask write it back to the
	 * hardware.  We rely on the use of the regmap core cache to
	 * suppress pointless writes.
	 */
	for (i = 0; i < d->chip->num_regs; i++) {
		ret = regmap_update_bits(d->map, d->chip->mask_base +
						(i * map->reg_stride),
					 d->mask_buf_def[i], d->mask_buf[i]);
		if (ret != 0)
			dev_err(d->map->dev, "Failed to sync masks in %x\n",
				d->chip->mask_base + (i * map->reg_stride));
	}

	mutex_unlock(&d->lock);
}

static void regmap_irq_enable(struct irq_data *data)
{
	struct regmap_irq_chip_data *d = irq_data_get_irq_chip_data(data);
	struct regmap *map = d->map;
	const struct regmap_irq *irq_data = irq_to_regmap_irq(d, data->hwirq);

	d->mask_buf[irq_data->reg_offset / map->reg_stride] &= ~irq_data->mask;
}

static void regmap_irq_disable(struct irq_data *data)
{
	struct regmap_irq_chip_data *d = irq_data_get_irq_chip_data(data);
	struct regmap *map = d->map;
	const struct regmap_irq *irq_data = irq_to_regmap_irq(d, data->hwirq);

	d->mask_buf[irq_data->reg_offset / map->reg_stride] |= irq_data->mask;
}

static struct irq_chip regmap_irq_chip = {
	.name			= "regmap",
	.irq_bus_lock		= regmap_irq_lock,
	.irq_bus_sync_unlock	= regmap_irq_sync_unlock,
	.irq_disable		= regmap_irq_disable,
	.irq_enable		= regmap_irq_enable,
};

static irqreturn_t regmap_irq_thread(int irq, void *d)
{
	struct regmap_irq_chip_data *data = d;
	struct regmap_irq_chip *chip = data->chip;
	struct regmap *map = data->map;
	int ret, i;
	u8 *buf8 = data->status_reg_buf;
	u16 *buf16 = data->status_reg_buf;
	u32 *buf32 = data->status_reg_buf;
	bool handled = false;

	ret = regmap_bulk_read(map, chip->status_base, data->status_reg_buf,
			       chip->num_regs);
	if (ret != 0) {
		dev_err(map->dev, "Failed to read IRQ status: %d\n", ret);
		return IRQ_NONE;
	}

	/*
	 * Ignore masked IRQs and ack if we need to; we ack early so
	 * there is no race between handling and acknowleding the
	 * interrupt.  We assume that typically few of the interrupts
	 * will fire simultaneously so don't worry about overhead from
	 * doing a write per register.
	 */
	for (i = 0; i < data->chip->num_regs; i++) {
		switch (map->format.val_bytes) {
		case 1:
			data->status_buf[i] = buf8[i];
			break;
		case 2:
			data->status_buf[i] = buf16[i];
			break;
		case 4:
			data->status_buf[i] = buf32[i];
			break;
		default:
			BUG();
			return IRQ_NONE;
		}

		data->status_buf[i] &= ~data->mask_buf[i];

		if (data->status_buf[i] && chip->ack_base) {
			ret = regmap_write(map, chip->ack_base +
						(i * map->reg_stride),
					   data->status_buf[i]);
			if (ret != 0)
				dev_err(map->dev, "Failed to ack 0x%x: %d\n",
					chip->ack_base + (i * map->reg_stride),
					ret);
		}
	}

	for (i = 0; i < chip->num_irqs; i++) {
		if (data->status_buf[chip->irqs[i].reg_offset /
				     map->reg_stride] & chip->irqs[i].mask) {
			handle_nested_irq(irq_find_mapping(data->domain, i));
			handled = true;
		}
	}

	if (handled)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static int regmap_irq_map(struct irq_domain *h, unsigned int virq,
			  irq_hw_number_t hw)
{
	struct regmap_irq_chip_data *data = h->host_data;

	irq_set_chip_data(virq, data);
	irq_set_chip_and_handler(virq, &regmap_irq_chip, handle_edge_irq);
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

static struct irq_domain_ops regmap_domain_ops = {
	.map	= regmap_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

/**
 * regmap_add_irq_chip(): Use standard regmap IRQ controller handling
 *
 * map:       The regmap for the device.
 * irq:       The IRQ the device uses to signal interrupts
 * irq_flags: The IRQF_ flags to use for the primary interrupt.
 * chip:      Configuration for the interrupt controller.
 * data:      Runtime data structure for the controller, allocated on success
 *
 * Returns 0 on success or an errno on failure.
 *
 * In order for this to be efficient the chip really should use a
 * register cache.  The chip driver is responsible for restoring the
 * register values used by the IRQ controller over suspend and resume.
 */
int regmap_add_irq_chip(struct regmap *map, int irq, int irq_flags,
			int irq_base, struct regmap_irq_chip *chip,
			struct regmap_irq_chip_data **data)
{
	struct regmap_irq_chip_data *d;
	int i;
	int ret = -ENOMEM;

	for (i = 0; i < chip->num_irqs; i++) {
		if (chip->irqs[i].reg_offset % map->reg_stride)
			return -EINVAL;
		if (chip->irqs[i].reg_offset / map->reg_stride >=
		    chip->num_regs)
			return -EINVAL;
	}

	if (irq_base) {
		irq_base = irq_alloc_descs(irq_base, 0, chip->num_irqs, 0);
		if (irq_base < 0) {
			dev_warn(map->dev, "Failed to allocate IRQs: %d\n",
				 irq_base);
			return irq_base;
		}
	}

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	*data = d;

	d->status_buf = kzalloc(sizeof(unsigned int) * chip->num_regs,
				GFP_KERNEL);
	if (!d->status_buf)
		goto err_alloc;

	d->status_reg_buf = kzalloc(map->format.val_bytes * chip->num_regs,
				    GFP_KERNEL);
	if (!d->status_reg_buf)
		goto err_alloc;

	d->mask_buf = kzalloc(sizeof(unsigned int) * chip->num_regs,
			      GFP_KERNEL);
	if (!d->mask_buf)
		goto err_alloc;

	d->mask_buf_def = kzalloc(sizeof(unsigned int) * chip->num_regs,
				  GFP_KERNEL);
	if (!d->mask_buf_def)
		goto err_alloc;

	d->map = map;
	d->chip = chip;
	d->irq_base = irq_base;
	mutex_init(&d->lock);

	for (i = 0; i < chip->num_irqs; i++)
		d->mask_buf_def[chip->irqs[i].reg_offset / map->reg_stride]
			|= chip->irqs[i].mask;

	/* Mask all the interrupts by default */
	for (i = 0; i < chip->num_regs; i++) {
		d->mask_buf[i] = d->mask_buf_def[i];
		ret = regmap_write(map, chip->mask_base + (i * map->reg_stride),
				   d->mask_buf[i]);
		if (ret != 0) {
			dev_err(map->dev, "Failed to set masks in 0x%x: %d\n",
				chip->mask_base + (i * map->reg_stride), ret);
			goto err_alloc;
		}
	}

	if (irq_base)
		d->domain = irq_domain_add_legacy(map->dev->of_node,
						  chip->num_irqs, irq_base, 0,
						  &regmap_domain_ops, d);
	else
		d->domain = irq_domain_add_linear(map->dev->of_node,
						  chip->num_irqs,
						  &regmap_domain_ops, d);
	if (!d->domain) {
		dev_err(map->dev, "Failed to create IRQ domain\n");
		ret = -ENOMEM;
		goto err_alloc;
	}

	ret = request_threaded_irq(irq, NULL, regmap_irq_thread, irq_flags,
				   chip->name, d);
	if (ret != 0) {
		dev_err(map->dev, "Failed to request IRQ %d: %d\n", irq, ret);
		goto err_domain;
	}

	return 0;

err_domain:
	/* Should really dispose of the domain but... */
err_alloc:
	kfree(d->mask_buf_def);
	kfree(d->mask_buf);
	kfree(d->status_reg_buf);
	kfree(d->status_buf);
	kfree(d);
	return ret;
}
EXPORT_SYMBOL_GPL(regmap_add_irq_chip);

/**
 * regmap_del_irq_chip(): Stop interrupt handling for a regmap IRQ chip
 *
 * @irq: Primary IRQ for the device
 * @d:   regmap_irq_chip_data allocated by regmap_add_irq_chip()
 */
void regmap_del_irq_chip(int irq, struct regmap_irq_chip_data *d)
{
	if (!d)
		return;

	free_irq(irq, d);
	/* We should unmap the domain but... */
	kfree(d->mask_buf_def);
	kfree(d->mask_buf);
	kfree(d->status_reg_buf);
	kfree(d->status_buf);
	kfree(d);
}
EXPORT_SYMBOL_GPL(regmap_del_irq_chip);

/**
 * regmap_irq_chip_get_base(): Retrieve interrupt base for a regmap IRQ chip
 *
 * Useful for drivers to request their own IRQs.
 *
 * @data: regmap_irq controller to operate on.
 */
int regmap_irq_chip_get_base(struct regmap_irq_chip_data *data)
{
	WARN_ON(!data->irq_base);
	return data->irq_base;
}
EXPORT_SYMBOL_GPL(regmap_irq_chip_get_base);

/**
 * regmap_irq_get_virq(): Map an interrupt on a chip to a virtual IRQ
 *
 * Useful for drivers to request their own IRQs.
 *
 * @data: regmap_irq controller to operate on.
 * @irq: index of the interrupt requested in the chip IRQs
 */
int regmap_irq_get_virq(struct regmap_irq_chip_data *data, int irq)
{
	return irq_create_mapping(data->domain, irq);
}
EXPORT_SYMBOL_GPL(regmap_irq_get_virq);
