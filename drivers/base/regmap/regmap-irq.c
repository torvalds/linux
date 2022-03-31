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
	unsigned int **virt_buf;

	unsigned int irq_reg_stride;
	unsigned int type_reg_stride;

	bool clear_status:1;
};

static int sub_irq_reg(struct regmap_irq_chip_data *data,
		       unsigned int base_reg, int i)
{
	const struct regmap_irq_chip *chip = data->chip;
	struct regmap *map = data->map;
	struct regmap_irq_sub_irq_map *subreg;
	unsigned int offset;
	int reg = 0;

	if (!chip->sub_reg_offsets || !chip->not_fixed_stride) {
		/* Assume linear mapping */
		reg = base_reg + (i * map->reg_stride * data->irq_reg_stride);
	} else {
		subreg = &chip->sub_reg_offsets[i];
		offset = subreg->offset[0];
		reg = base_reg + offset;
	}

	return reg;
}

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

static int regmap_irq_update_bits(struct regmap_irq_chip_data *d,
				  unsigned int reg, unsigned int mask,
				  unsigned int val)
{
	if (d->chip->mask_writeonly)
		return regmap_write_bits(d->map, reg, mask, val);
	else
		return regmap_update_bits(d->map, reg, mask, val);
}

static void regmap_irq_sync_unlock(struct irq_data *data)
{
	struct regmap_irq_chip_data *d = irq_data_get_irq_chip_data(data);
	struct regmap *map = d->map;
	int i, j, ret;
	u32 reg;
	u32 unmask_offset;
	u32 val;

	if (d->chip->runtime_pm) {
		ret = pm_runtime_get_sync(map->dev);
		if (ret < 0)
			dev_err(map->dev, "IRQ sync failed to resume: %d\n",
				ret);
	}

	if (d->clear_status) {
		for (i = 0; i < d->chip->num_regs; i++) {
			reg = sub_irq_reg(d, d->chip->status_base, i);

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
		if (!d->chip->mask_base)
			continue;

		reg = sub_irq_reg(d, d->chip->mask_base, i);
		if (d->chip->mask_invert) {
			ret = regmap_irq_update_bits(d, reg,
					 d->mask_buf_def[i], ~d->mask_buf[i]);
		} else if (d->chip->unmask_base) {
			/* set mask with mask_base register */
			ret = regmap_irq_update_bits(d, reg,
					d->mask_buf_def[i], ~d->mask_buf[i]);
			if (ret < 0)
				dev_err(d->map->dev,
					"Failed to sync unmasks in %x\n",
					reg);
			unmask_offset = d->chip->unmask_base -
							d->chip->mask_base;
			/* clear mask with unmask_base register */
			ret = regmap_irq_update_bits(d,
					reg + unmask_offset,
					d->mask_buf_def[i],
					d->mask_buf[i]);
		} else {
			ret = regmap_irq_update_bits(d, reg,
					 d->mask_buf_def[i], d->mask_buf[i]);
		}
		if (ret != 0)
			dev_err(d->map->dev, "Failed to sync masks in %x\n",
				reg);

		reg = sub_irq_reg(d, d->chip->wake_base, i);
		if (d->wake_buf) {
			if (d->chip->wake_invert)
				ret = regmap_irq_update_bits(d, reg,
							 d->mask_buf_def[i],
							 ~d->wake_buf[i]);
			else
				ret = regmap_irq_update_bits(d, reg,
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
			reg = sub_irq_reg(d, d->chip->ack_base, i);

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

	/* Don't update the type bits if we're using mask bits for irq type. */
	if (!d->chip->type_in_mask) {
		for (i = 0; i < d->chip->num_type_reg; i++) {
			if (!d->type_buf_def[i])
				continue;
			reg = sub_irq_reg(d, d->chip->type_base, i);
			if (d->chip->type_invert)
				ret = regmap_irq_update_bits(d, reg,
					d->type_buf_def[i], ~d->type_buf[i]);
			else
				ret = regmap_irq_update_bits(d, reg,
					d->type_buf_def[i], d->type_buf[i]);
			if (ret != 0)
				dev_err(d->map->dev, "Failed to sync type in %x\n",
					reg);
		}
	}

	if (d->chip->num_virt_regs) {
		for (i = 0; i < d->chip->num_virt_regs; i++) {
			for (j = 0; j < d->chip->num_regs; j++) {
				reg = sub_irq_reg(d, d->chip->virt_reg_base[i],
						  j);
				ret = regmap_write(map, reg, d->virt_buf[i][j]);
				if (ret != 0)
					dev_err(d->map->dev,
						"Failed to write virt 0x%x: %d\n",
						reg, ret);
			}
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
	unsigned int mask, type;

	type = irq_data->type.type_falling_val | irq_data->type.type_rising_val;

	/*
	 * The type_in_mask flag means that the underlying hardware uses
	 * separate mask bits for rising and falling edge interrupts, but
	 * we want to make them into a single virtual interrupt with
	 * configurable edge.
	 *
	 * If the interrupt we're enabling defines the falling or rising
	 * masks then instead of using the regular mask bits for this
	 * interrupt, use the value previously written to the type buffer
	 * at the corresponding offset in regmap_irq_set_type().
	 */
	if (d->chip->type_in_mask && type)
		mask = d->type_buf[irq_data->reg_offset / map->reg_stride];
	else
		mask = irq_data->mask;

	if (d->chip->clear_on_unmask)
		d->clear_status = true;

	d->mask_buf[irq_data->reg_offset / map->reg_stride] &= ~mask;
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
	int reg;
	const struct regmap_irq_type *t = &irq_data->type;

	if ((t->types_supported & type) != type)
		return 0;

	reg = t->type_reg_offset / map->reg_stride;

	if (t->type_reg_mask)
		d->type_buf[reg] &= ~t->type_reg_mask;
	else
		d->type_buf[reg] &= ~(t->type_falling_val |
				      t->type_rising_val |
				      t->type_level_low_val |
				      t->type_level_high_val);
	switch (type) {
	case IRQ_TYPE_EDGE_FALLING:
		d->type_buf[reg] |= t->type_falling_val;
		break;

	case IRQ_TYPE_EDGE_RISING:
		d->type_buf[reg] |= t->type_rising_val;
		break;

	case IRQ_TYPE_EDGE_BOTH:
		d->type_buf[reg] |= (t->type_falling_val |
					t->type_rising_val);
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		d->type_buf[reg] |= t->type_level_high_val;
		break;

	case IRQ_TYPE_LEVEL_LOW:
		d->type_buf[reg] |= t->type_level_low_val;
		break;
	default:
		return -EINVAL;
	}

	if (d->chip->set_type_virt)
		return d->chip->set_type_virt(d->virt_buf, type, data->hwirq,
					      reg);

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
	int i, ret = 0;

	if (!chip->sub_reg_offsets) {
		/* Assume linear mapping */
		ret = regmap_read(map, chip->status_base +
				  (b * map->reg_stride * data->irq_reg_stride),
				   &data->status_buf[b]);
	} else {
		subreg = &chip->sub_reg_offsets[b];
		for (i = 0; i < subreg->num_regs; i++) {
			unsigned int offset = subreg->offset[i];

			if (chip->not_fixed_stride)
				ret = regmap_read(map,
						chip->status_base + offset,
						&data->status_buf[b]);
			else
				ret = regmap_read(map,
						chip->status_base + offset,
						&data->status_buf[offset]);

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

	if (chip->num_main_regs) {
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
			ret = regmap_read(map, chip->main_status +
				  (i * map->reg_stride
				   * data->irq_reg_stride),
				  &data->main_status_buf[i]);
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
	} else if (!map->use_single_read && map->reg_stride == 1 &&
		   data->irq_reg_stride == 1) {

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
			unsigned int reg = sub_irq_reg(data,
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
	 * there is no race between handling and acknowleding the
	 * interrupt.  We assume that typically few of the interrupts
	 * will fire simultaneously so don't worry about overhead from
	 * doing a write per register.
	 */
	for (i = 0; i < data->chip->num_regs; i++) {
		data->status_buf[i] &= ~data->mask_buf[i];

		if (data->status_buf[i] && (chip->ack_base || chip->use_ack)) {
			reg = sub_irq_reg(data, data->chip->ack_base, i);

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
	if (chip->runtime_pm)
		pm_runtime_put(map->dev);

	if (chip->handle_post_irq)
		chip->handle_post_irq(chip->irq_drv_data);

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
	int num_type_reg;
	u32 reg;
	u32 unmask_offset;

	if (chip->num_regs <= 0)
		return -EINVAL;

	if (chip->clear_on_unmask && (chip->ack_base || chip->use_ack))
		return -EINVAL;

	for (i = 0; i < chip->num_irqs; i++) {
		if (chip->irqs[i].reg_offset % map->reg_stride)
			return -EINVAL;
		if (chip->irqs[i].reg_offset / map->reg_stride >=
		    chip->num_regs)
			return -EINVAL;
	}

	if (chip->not_fixed_stride) {
		for (i = 0; i < chip->num_regs; i++)
			if (chip->sub_reg_offsets[i].num_regs != 1)
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
					     sizeof(unsigned int),
					     GFP_KERNEL);

		if (!d->main_status_buf)
			goto err_alloc;
	}

	d->status_buf = kcalloc(chip->num_regs, sizeof(unsigned int),
				GFP_KERNEL);
	if (!d->status_buf)
		goto err_alloc;

	d->mask_buf = kcalloc(chip->num_regs, sizeof(unsigned int),
			      GFP_KERNEL);
	if (!d->mask_buf)
		goto err_alloc;

	d->mask_buf_def = kcalloc(chip->num_regs, sizeof(unsigned int),
				  GFP_KERNEL);
	if (!d->mask_buf_def)
		goto err_alloc;

	if (chip->wake_base) {
		d->wake_buf = kcalloc(chip->num_regs, sizeof(unsigned int),
				      GFP_KERNEL);
		if (!d->wake_buf)
			goto err_alloc;
	}

	num_type_reg = chip->type_in_mask ? chip->num_regs : chip->num_type_reg;
	if (num_type_reg) {
		d->type_buf_def = kcalloc(num_type_reg,
					  sizeof(unsigned int), GFP_KERNEL);
		if (!d->type_buf_def)
			goto err_alloc;

		d->type_buf = kcalloc(num_type_reg, sizeof(unsigned int),
				      GFP_KERNEL);
		if (!d->type_buf)
			goto err_alloc;
	}

	if (chip->num_virt_regs) {
		/*
		 * Create virt_buf[chip->num_extra_config_regs][chip->num_regs]
		 */
		d->virt_buf = kcalloc(chip->num_virt_regs, sizeof(*d->virt_buf),
				      GFP_KERNEL);
		if (!d->virt_buf)
			goto err_alloc;

		for (i = 0; i < chip->num_virt_regs; i++) {
			d->virt_buf[i] = kcalloc(chip->num_regs,
						 sizeof(unsigned int),
						 GFP_KERNEL);
			if (!d->virt_buf[i])
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

	if (chip->type_reg_stride)
		d->type_reg_stride = chip->type_reg_stride;
	else
		d->type_reg_stride = 1;

	if (!map->use_single_read && map->reg_stride == 1 &&
	    d->irq_reg_stride == 1) {
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
		if (!chip->mask_base)
			continue;

		reg = sub_irq_reg(d, d->chip->mask_base, i);

		if (chip->mask_invert)
			ret = regmap_irq_update_bits(d, reg,
					 d->mask_buf[i], ~d->mask_buf[i]);
		else if (d->chip->unmask_base) {
			unmask_offset = d->chip->unmask_base -
					d->chip->mask_base;
			ret = regmap_irq_update_bits(d,
					reg + unmask_offset,
					d->mask_buf[i],
					d->mask_buf[i]);
		} else
			ret = regmap_irq_update_bits(d, reg,
					 d->mask_buf[i], d->mask_buf[i]);
		if (ret != 0) {
			dev_err(map->dev, "Failed to set masks in 0x%x: %d\n",
				reg, ret);
			goto err_alloc;
		}

		if (!chip->init_ack_masked)
			continue;

		/* Ack masked but set interrupts */
		reg = sub_irq_reg(d, d->chip->status_base, i);
		ret = regmap_read(map, reg, &d->status_buf[i]);
		if (ret != 0) {
			dev_err(map->dev, "Failed to read IRQ status: %d\n",
				ret);
			goto err_alloc;
		}

		if (chip->status_invert)
			d->status_buf[i] = ~d->status_buf[i];

		if (d->status_buf[i] && (chip->ack_base || chip->use_ack)) {
			reg = sub_irq_reg(d, d->chip->ack_base, i);
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
			reg = sub_irq_reg(d, d->chip->wake_base, i);

			if (chip->wake_invert)
				ret = regmap_irq_update_bits(d, reg,
							 d->mask_buf_def[i],
							 0);
			else
				ret = regmap_irq_update_bits(d, reg,
							 d->mask_buf_def[i],
							 d->wake_buf[i]);
			if (ret != 0) {
				dev_err(map->dev, "Failed to set masks in 0x%x: %d\n",
					reg, ret);
				goto err_alloc;
			}
		}
	}

	if (chip->num_type_reg && !chip->type_in_mask) {
		for (i = 0; i < chip->num_type_reg; ++i) {
			reg = sub_irq_reg(d, d->chip->type_base, i);

			ret = regmap_read(map, reg, &d->type_buf_def[i]);

			if (d->chip->type_invert)
				d->type_buf_def[i] = ~d->type_buf_def[i];

			if (ret) {
				dev_err(map->dev, "Failed to get type defaults at 0x%x: %d\n",
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
	if (d->virt_buf) {
		for (i = 0; i < chip->num_virt_regs; i++)
			kfree(d->virt_buf[i]);
		kfree(d->virt_buf);
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
	int hwirq;

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
 * devm_regmap_add_irq_chip() - Resource manager regmap_add_irq_chip()
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
 * @dev: Device for which which resource was allocated.
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
