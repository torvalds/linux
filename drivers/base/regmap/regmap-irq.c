// SPDX-License-Identifier: GPL-2.0
//
// regmap based irq_chip
//
// Copyright 2011 Wolfson Microelectronics plc
//
// Author: Mark Brown <broonie@opensource.wolfsonmicro.com>

#include <linux/device.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "internal.h"

struct regmap_irq_chip_data {
	struct mutex lock;
	struct irq_chip irq_chip;

	struct regmap *map;
	const struct regmap_irq_chip *chip;

	int irq_base;
	struct irq_domain *domain;

	int irq;
	int wake_count;

	void *status_reg_buf;
	unsigned int *main_status_buf;
	unsigned int *status_buf;
	unsigned int *mask_buf;
	unsigned int *mask_buf_def;
	unsigned int *wake_buf;
	unsigned int *type_buf;
	unsigned int *type_buf_def;
	unsigned int **config_buf;

	unsigned int irq_reg_stride;

	unsigned int (*get_irq_reg)(struct regmap_irq_chip_data *data,
				    unsigned int base, int index);

	unsigned int clear_status:1;
};

static inline const
struct regmap_irq *irq_to_regmap_irq(struct regmap_irq_chip_data *data,
				     int irq)
{
	return &data->chip->irqs[irq];
}

static bool regmap_irq_can_bulk_read_status(struct regmap_irq_chip_data *data)
{
	struct regmap *map = data->map;

	/*
	 * While possible that a user-defined ->get_irq_reg() callback might
	 * be linear enough to support bulk reads, most of the time it won't.
	 * Therefore only allow them if the default callback is being used.
	 */
	return data->irq_reg_stride == 1 && map->reg_stride == 1 &&
	       data->get_irq_reg == regmap_irq_get_irq_reg_linear &&
	       !map->use_single_read;
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
	int i, j, ret;
	u32 reg;
	u32 val;

	if (d->chip->runtime_pm) {
		ret = pm_runtime_get_sync(map->dev);
		if (ret < 0)
			dev_err(map->dev, "IRQ sync failed to resume: %d\n",
				ret);
	}

	if (d->clear_status) {
		for (i = 0; i < d->chip->num_regs; i++) {
			reg = d->get_irq_reg(d, d->chip->status_base, i);

			ret = regmap_read(map, reg, &val);
			if (ret)
				dev_err(d->map->dev,
					"Failed to clear the interrupt status bits\n");
		}

		d->clear_status = false;
	}

	/*
	 * If there's been a change in the mask write it back to the
	 * hardware.  We rely on the use of the regmap core cache to
	 * suppress pointless writes.
	 */
	for (i = 0; i < d->chip->num_regs; i++) {
		if (d->chip->handle_mask_sync)
			d->chip->handle_mask_sync(i, d->mask_buf_def[i],
						  d->mask_buf[i],
						  d->chip->irq_drv_data);

		if (d->chip->mask_base && !d->chip->handle_mask_sync) {
			reg = d->get_irq_reg(d, d->chip->mask_base, i);
			ret = regmap_update_bits(d->map, reg,
						 d->mask_buf_def[i],
						 d->mask_buf[i]);
			if (ret)
				dev_err(d->map->dev, "Failed to sync masks in %x\n", reg);
		}

		if (d->chip->unmask_base && !d->chip->handle_mask_sync) {
			reg = d->get_irq_reg(d, d->chip->unmask_base, i);
			ret = regmap_update_bits(d->map, reg,
					d->mask_buf_def[i], ~d->mask_buf[i]);
			if (ret)
				dev_err(d->map->dev, "Failed to sync masks in %x\n",
					reg);
		}

		reg = d->get_irq_reg(d, d->chip->wake_base, i);
		if (d->wake_buf) {
			if (d->chip->wake_invert)
				ret = regmap_update_bits(d->map, reg,
							 d->mask_buf_def[i],
							 ~d->wake_buf[i]);
			else
				ret = regmap_update_bits(d->map, reg,
							 d->mask_buf_def[i],
							 d->wake_buf[i]);
			if (ret != 0)
				dev_err(d->map->dev,
					"Failed to sync wakes in %x: %d\n",
					reg, ret);
		}

		if (!d->chip->init_ack_masked)
			continue;
		/*
		 * Ack all the masked interrupts unconditionally,
		 * OR if there is masked interrupt which hasn't been Acked,
		 * it'll be ignored in irq handler, then may introduce irq storm
		 */
		if (d->mask_buf[i] && (d->chip->ack_base || d->chip->use_ack)) {
			reg = d->get_irq_reg(d, d->chip->ack_base, i);

			/* some chips ack by write 0 */
			if (d->chip->ack_invert)
				ret = regmap_write(map, reg, ~d->mask_buf[i]);
			else
				ret = regmap_write(map, reg, d->mask_buf[i]);
			if (d->chip->clear_ack) {
				if (d->chip->ack_invert && !ret)
					ret = regmap_write(map, reg, UINT_MAX);
				else if (!ret)
					ret = regmap_write(map, reg, 0);
			}
			if (ret != 0)
				dev_err(d->map->dev, "Failed to ack 0x%x: %d\n",
					reg, ret);
		}
	}

	for (i = 0; i < d->chip->num_config_bases; i++) {
		for (j = 0; j < d->chip->num_config_regs; j++) {
			reg = d->get_irq_reg(d, d->chip->config_base[i], j);
			ret = regmap_write(map, reg, d->config_buf[i][j]);
			if (ret)
				dev_err(d->map->dev,
					"Failed to write config %x: %d\n",
					reg, ret);
		}
	}

	if (d->chip->runtime_pm)
		pm_runtime_put(map->dev);

	/* If we've changed our wakeup count propagate it to the parent */
	if (d->wake_count < 0)
		for (i = d->wake_count; i < 0; i++)
			irq_set_irq_wake(d->irq, 0);
	else if (d->wake_count > 0)
		for (i = 0; i < d->wake_count; i++)
			irq_set_irq_wake(d->irq, 1);

	d->wake_count = 0;

	mutex_unlock(&d->lock);
}

static void regmap_irq_enable(struct irq_data *data)
{
	struct regmap_irq_chip_data *d = irq_data_get_irq_chip_data(data);
	struct regmap *map = d->map;
	const struct regmap_irq *irq_data = irq_to_regmap_irq(d, data->hwirq);
	unsigned int reg = irq_data->reg_offset / map->reg_stride;
	unsigned int mask;

	/*
	 * The type_in_mask flag means that the underlying hardware uses
	 * separate mask bits for each interrupt trigger type, but we want
	 * to have a single logical interrupt with a configurable type.
	 *
	 * If the interrupt we're enabling defines any supported types
	 * then instead of using the regular mask bits for this interrupt,
	 * use the value previously written to the type buffer at the
	 * corresponding offset in regmap_irq_set_type().
	 */
	if (d->chip->type_in_mask && irq_data->type.types_supported)
		mask = d->type_buf[reg] & irq_data->mask;
	else
		mask = irq_data->mask;

	if (d->chip->clear_on_unmask)
		d->clear_status = true;

	d->mask_buf[reg] &= ~mask;
}

static void regmap_irq_disable(struct irq_data *data)
{
	struct regmap_irq_chip_data *d = irq_data_get_irq_chip_data(data);
	struct regmap *map = d->map;
	const struct regmap_irq *irq_data = irq_to_regmap_irq(d, data->hwirq);

	d->mask_buf[irq_data->reg_offset / map->reg_stride] |= irq_data->mask;
}

static int regmap_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct regmap_irq_chip_data *d = irq_data_get_irq_chip_data(data);
	struct regmap *map = d->map;
	const struct regmap_irq *irq_data = irq_to_regmap_irq(d, data->hwirq);
	int reg, ret;
	const struct regmap_irq_type *t = &irq_data->type;

	if ((t->types_supported & type) != type)
		return 0;

	reg = t->type_reg_offset / map->reg_stride;

	if (d->chip->type_in_mask) {
		ret = regmap_irq_set_type_config_simple(&d->type_buf, type,
							irq_data, reg, d->chip->irq_drv_data);
		if (ret)
			return ret;
	}

	if (d->chip->set_type_config) {
		ret = d->chip->set_type_config(d->config_buf, type, irq_data,
					       reg, d->chip->irq_drv_data);
		if (ret)
			return ret;
	}

	return 0;
}

static int regmap_irq_set_wake(struct irq_data *data, unsigned int on)
{
	struct regmap_irq_chip_data *d = irq_data_get_irq_chip_data(data);
	struct regmap *map = d->map;
	const struct regmap_irq *irq_data = irq_to_regmap_irq(d, data->hwirq);

	if (on) {
		if (d->wake_buf)
			d->wake_buf[irq_data->reg_offset / map->reg_stride]
				&= ~irq_data->mask;
		d->wake_count++;
	} else {
		if (d->wake_buf)
			d->wake_buf[irq_data->reg_offset / map->reg_stride]
				|= irq_data->mask;
		d->wake_count--;
	}

	return 0;
}

static const struct irq_chip regmap_irq_chip = {
	.irq_bus_lock		= regmap_irq_lock,
	.irq_bus_sync_unlock	= regmap_irq_sync_unlock,
	.irq_disable		= regmap_irq_disable,
	.irq_enable		= regmap_irq_enable,
	.irq_set_type		= regmap_irq_set_type,
	.irq_set_wake		= regmap_irq_set_wake,
};

static inline int read_sub_irq_data(struct regmap_irq_chip_data *data,
					   unsigned int b)
{
	const struct regmap_irq_chip *chip = data->chip;
	struct regmap *map = data->map;
	struct regmap_irq_sub_irq_map *subreg;
	unsigned int reg;
	int i, ret = 0;

	if (!chip->sub_reg_offsets) {
		reg = data->get_irq_reg(data, chip->status_base, b);
		ret = regmap_read(map, reg, &data->status_buf[b]);
	} else {
		/*
		 * Note we can't use ->get_irq_reg() here because the offsets
		 * in 'subreg' are *not* interchangeable with indices.
		 */
		subreg = &chip->sub_reg_offsets[b];
		for (i = 0; i < subreg->num_regs; i++) {
			unsigned int offset = subreg->offset[i];
			unsigned int index = offset / map->reg_stride;

			ret = regmap_read(map, chip->status_base + offset,
					  &data->status_buf[index]);
			if (ret)
				break;
		}
	}
	return ret;
}

static irqreturn_t regmap_irq_thread(int irq, void *d)
{
	struct regmap_irq_chip_data *data = d;
	const struct regmap_irq_chip *chip = data->chip;
	struct regmap *map = data->map;
	int ret, i;
	bool handled = false;
	u32 reg;

	if (chip->handle_pre_irq)
		chip->handle_pre_irq(chip->irq_drv_data);

	if (chip->runtime_pm) {
		ret = pm_runtime_get_sync(map->dev);
		if (ret < 0) {
			dev_err(map->dev, "IRQ thread failed to resume: %d\n",
				ret);
			goto exit;
		}
	}

	/*
	 * Read only registers with active IRQs if the chip has 'main status
	 * register'. Else read in the statuses, using a single bulk read if
	 * possible in order to reduce the I/O overheads.
	 */

	if (chip->no_status) {
		/* no status register so default to all active */
		memset32(data->status_buf, GENMASK(31, 0), chip->num_regs);
	} else if (chip->num_main_regs) {
		unsigned int max_main_bits;
		unsigned long size;

		size = chip->num_regs * sizeof(unsigned int);

		max_main_bits = (chip->num_main_status_bits) ?
				 chip->num_main_status_bits : chip->num_regs;
		/* Clear the status buf as we don't read all status regs */
		memset(data->status_buf, 0, size);

		/* We could support bulk read for main status registers
		 * but I don't expect to see devices with really many main
		 * status registers so let's only support single reads for the
		 * sake of simplicity. and add bulk reads only if needed
		 */
		for (i = 0; i < chip->num_main_regs; i++) {
			reg = data->get_irq_reg(data, chip->main_status, i);
			ret = regmap_read(map, reg, &data->main_status_buf[i]);
			if (ret) {
				dev_err(map->dev,
					"Failed to read IRQ status %d\n",
					ret);
				goto exit;
			}
		}

		/* Read sub registers with active IRQs */
		for (i = 0; i < chip->num_main_regs; i++) {
			unsigned int b;
			const unsigned long mreg = data->main_status_buf[i];

			for_each_set_bit(b, &mreg, map->format.val_bytes * 8) {
				if (i * map->format.val_bytes * 8 + b >
				    max_main_bits)
					break;
				ret = read_sub_irq_data(data, b);

				if (ret != 0) {
					dev_err(map->dev,
						"Failed to read IRQ status %d\n",
						ret);
					goto exit;
				}
			}

		}
	} else if (regmap_irq_can_bulk_read_status(data)) {

		u8 *buf8 = data->status_reg_buf;
		u16 *buf16 = data->status_reg_buf;
		u32 *buf32 = data->status_reg_buf;

		BUG_ON(!data->status_reg_buf);

		ret = regmap_bulk_read(map, chip->status_base,
				       data->status_reg_buf,
				       chip->num_regs);
		if (ret != 0) {
			dev_err(map->dev, "Failed to read IRQ status: %d\n",
				ret);
			goto exit;
		}

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
				goto exit;
			}
		}

	} else {
		for (i = 0; i < data->chip->num_regs; i++) {
			unsigned int reg = data->get_irq_reg(data,
					data->chip->status_base, i);
			ret = regmap_read(map, reg, &data->status_buf[i]);

			if (ret != 0) {
				dev_err(map->dev,
					"Failed to read IRQ status: %d\n",
					ret);
				goto exit;
			}
		}
	}

	if (chip->status_invert)
		for (i = 0; i < data->chip->num_regs; i++)
			data->status_buf[i] = ~data->status_buf[i];

	/*
	 * Ignore masked IRQs and ack if we need to; we ack early so
	 * there is no race between handling and acknowledging the
	 * interrupt.  We assume that typically few of the interrupts
	 * will fire simultaneously so don't worry about overhead from
	 * doing a write per register.
	 */
	for (i = 0; i < data->chip->num_regs; i++) {
		data->status_buf[i] &= ~data->mask_buf[i];

		if (data->status_buf[i] && (chip->ack_base || chip->use_ack)) {
			reg = data->get_irq_reg(data, data->chip->ack_base, i);

			if (chip->ack_invert)
				ret = regmap_write(map, reg,
						~data->status_buf[i]);
			else
				ret = regmap_write(map, reg,
						data->status_buf[i]);
			if (chip->clear_ack) {
				if (chip->ack_invert && !ret)
					ret = regmap_write(map, reg, UINT_MAX);
				else if (!ret)
					ret = regmap_write(map, reg, 0);
			}
			if (ret != 0)
				dev_err(map->dev, "Failed to ack 0x%x: %d\n",
					reg, ret);
		}
	}

	for (i = 0; i < chip->num_irqs; i++) {
		if (data->status_buf[chip->irqs[i].reg_offset /
				     map->reg_stride] & chip->irqs[i].mask) {
			handle_nested_irq(irq_find_mapping(data->domain, i));
			handled = true;
		}
	}

exit:
	if (chip->handle_post_irq)
		chip->handle_post_irq(chip->irq_drv_data);

	if (chip->runtime_pm)
		pm_runtime_put(map->dev);

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
	irq_set_chip(virq, &data->irq_chip);
	irq_set_nested_thread(virq, 1);
	irq_set_parent(virq, data->irq);
	irq_set_noprobe(virq);

	return 0;
}

static const struct irq_domain_ops regmap_domain_ops = {
	.map	= regmap_irq_map,
	.xlate	= irq_domain_xlate_onetwocell,
};

/**
 * regmap_irq_get_irq_reg_linear() - Linear IRQ register mapping callback.
 * @data: Data for the &struct regmap_irq_chip
 * @base: Base register
 * @index: Register index
 *
 * Returns the register address corresponding to the given @base and @index
 * by the formula ``base + index * regmap_stride * irq_reg_stride``.
 */
unsigned int regmap_irq_get_irq_reg_linear(struct regmap_irq_chip_data *data,
					   unsigned int base, int index)
{
	struct regmap *map = data->map;

	return base + index * map->reg_stride * data->irq_reg_stride;
}
EXPORT_SYMBOL_GPL(regmap_irq_get_irq_reg_linear);

/**
 * regmap_irq_set_type_config_simple() - Simple IRQ type configuration callback.
 * @buf: Buffer containing configuration register values, this is a 2D array of
 *       `num_config_bases` rows, each of `num_config_regs` elements.
 * @type: The requested IRQ type.
 * @irq_data: The IRQ being configured.
 * @idx: Index of the irq's config registers within each array `buf[i]`
 * @irq_drv_data: Driver specific IRQ data
 *
 * This is a &struct regmap_irq_chip->set_type_config callback suitable for
 * chips with one config register. Register values are updated according to
 * the &struct regmap_irq_type data associated with an IRQ.
 */
int regmap_irq_set_type_config_simple(unsigned int **buf, unsigned int type,
				      const struct regmap_irq *irq_data,
				      int idx, void *irq_drv_data)
{
	const struct regmap_irq_type *t = &irq_data->type;

	if (t->type_reg_mask)
		buf[0][idx] &= ~t->type_reg_mask;
	else
		buf[0][idx] &= ~(t->type_falling_val |
				 t->type_rising_val |
				 t->type_level_low_val |
				 t->type_level_high_val);

	switch (type) {
	case IRQ_TYPE_EDGE_FALLING:
		buf[0][idx] |= t->type_falling_val;
		break;

	case IRQ_TYPE_EDGE_RISING:
		buf[0][idx] |= t->type_rising_val;
		break;

	case IRQ_TYPE_EDGE_BOTH:
		buf[0][idx] |= (t->type_falling_val |
				t->type_rising_val);
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		buf[0][idx] |= t->type_level_high_val;
		break;

	case IRQ_TYPE_LEVEL_LOW:
		buf[0][idx] |= t->type_level_low_val;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(regmap_irq_set_type_config_simple);

/**
 * regmap_add_irq_chip_fwnode() - Use standard regmap IRQ controller handling
 *
 * @fwnode: The firmware node where the IRQ domain should be added to.
 * @map: The regmap for the device.
 * @irq: The IRQ the device uses to signal interrupts.
 * @irq_flags: The IRQF_ flags to use for the primary interrupt.
 * @irq_base: Allocate at specific IRQ number if irq_base > 0.
 * @chip: Configuration for the interrupt controller.
 * @data: Runtime data structure for the controller, allocated on success.
 *
 * Returns 0 on success or an errno on failure.
 *
 * In order for this to be efficient the chip really should use a
 * register cache.  The chip driver is responsible for restoring the
 * register values used by the IRQ controller over suspend and resume.
 */
int regmap_add_irq_chip_fwnode(struct fwnode_handle *fwnode,
			       struct regmap *map, int irq,
			       int irq_flags, int irq_base,
			       const struct regmap_irq_chip *chip,
			       struct regmap_irq_chip_data **data)
{
	struct regmap_irq_chip_data *d;
	int i;
	int ret = -ENOMEM;
	u32 reg;

	if (chip->num_regs <= 0)
		return -EINVAL;

	if (chip->clear_on_unmask && (chip->ack_base || chip->use_ack))
		return -EINVAL;

	if (chip->mask_base && chip->unmask_base && !chip->mask_unmask_non_inverted)
		return -EINVAL;

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

	if (chip->num_main_regs) {
		d->main_status_buf = kcalloc(chip->num_main_regs,
					     sizeof(*d->main_status_buf),
					     GFP_KERNEL);

		if (!d->main_status_buf)
			goto err_alloc;
	}

	d->status_buf = kcalloc(chip->num_regs, sizeof(*d->status_buf),
				GFP_KERNEL);
	if (!d->status_buf)
		goto err_alloc;

	d->mask_buf = kcalloc(chip->num_regs, sizeof(*d->mask_buf),
			      GFP_KERNEL);
	if (!d->mask_buf)
		goto err_alloc;

	d->mask_buf_def = kcalloc(chip->num_regs, sizeof(*d->mask_buf_def),
				  GFP_KERNEL);
	if (!d->mask_buf_def)
		goto err_alloc;

	if (chip->wake_base) {
		d->wake_buf = kcalloc(chip->num_regs, sizeof(*d->wake_buf),
				      GFP_KERNEL);
		if (!d->wake_buf)
			goto err_alloc;
	}

	if (chip->type_in_mask) {
		d->type_buf_def = kcalloc(chip->num_regs,
					  sizeof(*d->type_buf_def), GFP_KERNEL);
		if (!d->type_buf_def)
			goto err_alloc;

		d->type_buf = kcalloc(chip->num_regs, sizeof(*d->type_buf), GFP_KERNEL);
		if (!d->type_buf)
			goto err_alloc;
	}

	if (chip->num_config_bases && chip->num_config_regs) {
		/*
		 * Create config_buf[num_config_bases][num_config_regs]
		 */
		d->config_buf = kcalloc(chip->num_config_bases,
					sizeof(*d->config_buf), GFP_KERNEL);
		if (!d->config_buf)
			goto err_alloc;

		for (i = 0; i < chip->num_config_regs; i++) {
			d->config_buf[i] = kcalloc(chip->num_config_regs,
						   sizeof(**d->config_buf),
						   GFP_KERNEL);
			if (!d->config_buf[i])
				goto err_alloc;
		}
	}

	d->irq_chip = regmap_irq_chip;
	d->irq_chip.name = chip->name;
	d->irq = irq;
	d->map = map;
	d->chip = chip;
	d->irq_base = irq_base;

	if (chip->irq_reg_stride)
		d->irq_reg_stride = chip->irq_reg_stride;
	else
		d->irq_reg_stride = 1;

	if (chip->get_irq_reg)
		d->get_irq_reg = chip->get_irq_reg;
	else
		d->get_irq_reg = regmap_irq_get_irq_reg_linear;

	if (regmap_irq_can_bulk_read_status(d)) {
		d->status_reg_buf = kmalloc_array(chip->num_regs,
						  map->format.val_bytes,
						  GFP_KERNEL);
		if (!d->status_reg_buf)
			goto err_alloc;
	}

	mutex_init(&d->lock);

	for (i = 0; i < chip->num_irqs; i++)
		d->mask_buf_def[chip->irqs[i].reg_offset / map->reg_stride]
			|= chip->irqs[i].mask;

	/* Mask all the interrupts by default */
	for (i = 0; i < chip->num_regs; i++) {
		d->mask_buf[i] = d->mask_buf_def[i];

		if (chip->handle_mask_sync) {
			ret = chip->handle_mask_sync(i, d->mask_buf_def[i],
						     d->mask_buf[i],
						     chip->irq_drv_data);
			if (ret)
				goto err_alloc;
		}

		if (chip->mask_base && !chip->handle_mask_sync) {
			reg = d->get_irq_reg(d, chip->mask_base, i);
			ret = regmap_update_bits(d->map, reg,
						 d->mask_buf_def[i],
						 d->mask_buf[i]);
			if (ret) {
				dev_err(map->dev, "Failed to set masks in 0x%x: %d\n",
					reg, ret);
				goto err_alloc;
			}
		}

		if (chip->unmask_base && !chip->handle_mask_sync) {
			reg = d->get_irq_reg(d, chip->unmask_base, i);
			ret = regmap_update_bits(d->map, reg,
					d->mask_buf_def[i], ~d->mask_buf[i]);
			if (ret) {
				dev_err(map->dev, "Failed to set masks in 0x%x: %d\n",
					reg, ret);
				goto err_alloc;
			}
		}

		if (!chip->init_ack_masked)
			continue;

		/* Ack masked but set interrupts */
		if (d->chip->no_status) {
			/* no status register so default to all active */
			d->status_buf[i] = GENMASK(31, 0);
		} else {
			reg = d->get_irq_reg(d, d->chip->status_base, i);
			ret = regmap_read(map, reg, &d->status_buf[i]);
			if (ret != 0) {
				dev_err(map->dev, "Failed to read IRQ status: %d\n",
					ret);
				goto err_alloc;
			}
		}

		if (chip->status_invert)
			d->status_buf[i] = ~d->status_buf[i];

		if (d->status_buf[i] && (chip->ack_base || chip->use_ack)) {
			reg = d->get_irq_reg(d, d->chip->ack_base, i);
			if (chip->ack_invert)
				ret = regmap_write(map, reg,
					~(d->status_buf[i] & d->mask_buf[i]));
			else
				ret = regmap_write(map, reg,
					d->status_buf[i] & d->mask_buf[i]);
			if (chip->clear_ack) {
				if (chip->ack_invert && !ret)
					ret = regmap_write(map, reg, UINT_MAX);
				else if (!ret)
					ret = regmap_write(map, reg, 0);
			}
			if (ret != 0) {
				dev_err(map->dev, "Failed to ack 0x%x: %d\n",
					reg, ret);
				goto err_alloc;
			}
		}
	}

	/* Wake is disabled by default */
	if (d->wake_buf) {
		for (i = 0; i < chip->num_regs; i++) {
			d->wake_buf[i] = d->mask_buf_def[i];
			reg = d->get_irq_reg(d, d->chip->wake_base, i);

			if (chip->wake_invert)
				ret = regmap_update_bits(d->map, reg,
							 d->mask_buf_def[i],
							 0);
			else
				ret = regmap_update_bits(d->map, reg,
							 d->mask_buf_def[i],
							 d->wake_buf[i]);
			if (ret != 0) {
				dev_err(map->dev, "Failed to set masks in 0x%x: %d\n",
					reg, ret);
				goto err_alloc;
			}
		}
	}

	if (irq_base)
		d->domain = irq_domain_create_legacy(fwnode, chip->num_irqs,
						     irq_base, 0,
						     &regmap_domain_ops, d);
	else
		d->domain = irq_domain_create_linear(fwnode, chip->num_irqs,
						     &regmap_domain_ops, d);
	if (!d->domain) {
		dev_err(map->dev, "Failed to create IRQ domain\n");
		ret = -ENOMEM;
		goto err_alloc;
	}

	ret = request_threaded_irq(irq, NULL, regmap_irq_thread,
				   irq_flags | IRQF_ONESHOT,
				   chip->name, d);
	if (ret != 0) {
		dev_err(map->dev, "Failed to request IRQ %d for %s: %d\n",
			irq, chip->name, ret);
		goto err_domain;
	}

	*data = d;

	return 0;

err_domain:
	/* Should really dispose of the domain but... */
err_alloc:
	kfree(d->type_buf);
	kfree(d->type_buf_def);
	kfree(d->wake_buf);
	kfree(d->mask_buf_def);
	kfree(d->mask_buf);
	kfree(d->status_buf);
	kfree(d->status_reg_buf);
	if (d->config_buf) {
		for (i = 0; i < chip->num_config_bases; i++)
			kfree(d->config_buf[i]);
		kfree(d->config_buf);
	}
	kfree(d);
	return ret;
}
EXPORT_SYMBOL_GPL(regmap_add_irq_chip_fwnode);

/**
 * regmap_add_irq_chip() - Use standard regmap IRQ controller handling
 *
 * @map: The regmap for the device.
 * @irq: The IRQ the device uses to signal interrupts.
 * @irq_flags: The IRQF_ flags to use for the primary interrupt.
 * @irq_base: Allocate at specific IRQ number if irq_base > 0.
 * @chip: Configuration for the interrupt controller.
 * @data: Runtime data structure for the controller, allocated on success.
 *
 * Returns 0 on success or an errno on failure.
 *
 * This is the same as regmap_add_irq_chip_fwnode, except that the firmware
 * node of the regmap is used.
 */
int regmap_add_irq_chip(struct regmap *map, int irq, int irq_flags,
			int irq_base, const struct regmap_irq_chip *chip,
			struct regmap_irq_chip_data **data)
{
	return regmap_add_irq_chip_fwnode(dev_fwnode(map->dev), map, irq,
					  irq_flags, irq_base, chip, data);
}
EXPORT_SYMBOL_GPL(regmap_add_irq_chip);

/**
 * regmap_del_irq_chip() - Stop interrupt handling for a regmap IRQ chip
 *
 * @irq: Primary IRQ for the device
 * @d: &regmap_irq_chip_data allocated by regmap_add_irq_chip()
 *
 * This function also disposes of all mapped IRQs on the chip.
 */
void regmap_del_irq_chip(int irq, struct regmap_irq_chip_data *d)
{
	unsigned int virq;
	int i, hwirq;

	if (!d)
		return;

	free_irq(irq, d);

	/* Dispose all virtual irq from irq domain before removing it */
	for (hwirq = 0; hwirq < d->chip->num_irqs; hwirq++) {
		/* Ignore hwirq if holes in the IRQ list */
		if (!d->chip->irqs[hwirq].mask)
			continue;

		/*
		 * Find the virtual irq of hwirq on chip and if it is
		 * there then dispose it
		 */
		virq = irq_find_mapping(d->domain, hwirq);
		if (virq)
			irq_dispose_mapping(virq);
	}

	irq_domain_remove(d->domain);
	kfree(d->type_buf);
	kfree(d->type_buf_def);
	kfree(d->wake_buf);
	kfree(d->mask_buf_def);
	kfree(d->mask_buf);
	kfree(d->status_reg_buf);
	kfree(d->status_buf);
	if (d->config_buf) {
		for (i = 0; i < d->chip->num_config_bases; i++)
			kfree(d->config_buf[i]);
		kfree(d->config_buf);
	}
	kfree(d);
}
EXPORT_SYMBOL_GPL(regmap_del_irq_chip);

static void devm_regmap_irq_chip_release(struct device *dev, void *res)
{
	struct regmap_irq_chip_data *d = *(struct regmap_irq_chip_data **)res;

	regmap_del_irq_chip(d->irq, d);
}

static int devm_regmap_irq_chip_match(struct device *dev, void *res, void *data)

{
	struct regmap_irq_chip_data **r = res;

	if (!r || !*r) {
		WARN_ON(!r || !*r);
		return 0;
	}
	return *r == data;
}

/**
 * devm_regmap_add_irq_chip_fwnode() - Resource managed regmap_add_irq_chip_fwnode()
 *
 * @dev: The device pointer on which irq_chip belongs to.
 * @fwnode: The firmware node where the IRQ domain should be added to.
 * @map: The regmap for the device.
 * @irq: The IRQ the device uses to signal interrupts
 * @irq_flags: The IRQF_ flags to use for the primary interrupt.
 * @irq_base: Allocate at specific IRQ number if irq_base > 0.
 * @chip: Configuration for the interrupt controller.
 * @data: Runtime data structure for the controller, allocated on success
 *
 * Returns 0 on success or an errno on failure.
 *
 * The &regmap_irq_chip_data will be automatically released when the device is
 * unbound.
 */
int devm_regmap_add_irq_chip_fwnode(struct device *dev,
				    struct fwnode_handle *fwnode,
				    struct regmap *map, int irq,
				    int irq_flags, int irq_base,
				    const struct regmap_irq_chip *chip,
				    struct regmap_irq_chip_data **data)
{
	struct regmap_irq_chip_data **ptr, *d;
	int ret;

	ptr = devres_alloc(devm_regmap_irq_chip_release, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = regmap_add_irq_chip_fwnode(fwnode, map, irq, irq_flags, irq_base,
					 chip, &d);
	if (ret < 0) {
		devres_free(ptr);
		return ret;
	}

	*ptr = d;
	devres_add(dev, ptr);
	*data = d;
	return 0;
}
EXPORT_SYMBOL_GPL(devm_regmap_add_irq_chip_fwnode);

/**
 * devm_regmap_add_irq_chip() - Resource managed regmap_add_irq_chip()
 *
 * @dev: The device pointer on which irq_chip belongs to.
 * @map: The regmap for the device.
 * @irq: The IRQ the device uses to signal interrupts
 * @irq_flags: The IRQF_ flags to use for the primary interrupt.
 * @irq_base: Allocate at specific IRQ number if irq_base > 0.
 * @chip: Configuration for the interrupt controller.
 * @data: Runtime data structure for the controller, allocated on success
 *
 * Returns 0 on success or an errno on failure.
 *
 * The &regmap_irq_chip_data will be automatically released when the device is
 * unbound.
 */
int devm_regmap_add_irq_chip(struct device *dev, struct regmap *map, int irq,
			     int irq_flags, int irq_base,
			     const struct regmap_irq_chip *chip,
			     struct regmap_irq_chip_data **data)
{
	return devm_regmap_add_irq_chip_fwnode(dev, dev_fwnode(map->dev), map,
					       irq, irq_flags, irq_base, chip,
					       data);
}
EXPORT_SYMBOL_GPL(devm_regmap_add_irq_chip);

/**
 * devm_regmap_del_irq_chip() - Resource managed regmap_del_irq_chip()
 *
 * @dev: Device for which the resource was allocated.
 * @irq: Primary IRQ for the device.
 * @data: &regmap_irq_chip_data allocated by regmap_add_irq_chip().
 *
 * A resource managed version of regmap_del_irq_chip().
 */
void devm_regmap_del_irq_chip(struct device *dev, int irq,
			      struct regmap_irq_chip_data *data)
{
	int rc;

	WARN_ON(irq != data->irq);
	rc = devres_release(dev, devm_regmap_irq_chip_release,
			    devm_regmap_irq_chip_match, data);

	if (rc != 0)
		WARN_ON(rc);
}
EXPORT_SYMBOL_GPL(devm_regmap_del_irq_chip);

/**
 * regmap_irq_chip_get_base() - Retrieve interrupt base for a regmap IRQ chip
 *
 * @data: regmap irq controller to operate on.
 *
 * Useful for drivers to request their own IRQs.
 */
int regmap_irq_chip_get_base(struct regmap_irq_chip_data *data)
{
	WARN_ON(!data->irq_base);
	return data->irq_base;
}
EXPORT_SYMBOL_GPL(regmap_irq_chip_get_base);

/**
 * regmap_irq_get_virq() - Map an interrupt on a chip to a virtual IRQ
 *
 * @data: regmap irq controller to operate on.
 * @irq: index of the interrupt requested in the chip IRQs.
 *
 * Useful for drivers to request their own IRQs.
 */
int regmap_irq_get_virq(struct regmap_irq_chip_data *data, int irq)
{
	/* Handle holes in the IRQ list */
	if (!data->chip->irqs[irq].mask)
		return -EINVAL;

	return irq_create_mapping(data->domain, irq);
}
EXPORT_SYMBOL_GPL(regmap_irq_get_virq);

/**
 * regmap_irq_get_domain() - Retrieve the irq_domain for the chip
 *
 * @data: regmap_irq controller to operate on.
 *
 * Useful for drivers to request their own IRQs and for integration
 * with subsystems.  For ease of integration NULL is accepted as a
 * domain, allowing devices to just call this even if no domain is
 * allocated.
 */
struct irq_domain *regmap_irq_get_domain(struct regmap_irq_chip_data *data)
{
	if (data)
		return data->domain;
	else
		return NULL;
}
EXPORT_SYMBOL_GPL(regmap_irq_get_domain);
