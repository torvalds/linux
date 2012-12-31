/*
 * max77686-irq.c - Interrupt controller support for MAX77686
 *
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * This driver is based on max8998-irq.c
 */

#include <linux/err.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/mfd/max77686.h>
#include <linux/mfd/max77686-private.h>

static const u8 max77686_mask_reg[] = {
	[PMIC_INT1] = MAX77686_REG_INT1MSK,
	[PMIC_INT2] = MAX77686_REG_INT2MSK,
};

static struct i2c_client *get_i2c(struct max77686_dev *max77686,
				enum max77686_irq_source src)
{
	switch (src) {
	case PMIC_INT1 ... PMIC_INT2:
		return max77686->i2c;
	default:
		return ERR_PTR(-EINVAL);
	}

	return ERR_PTR(-EINVAL);
}

struct max77686_irq_data {
	int mask;
	enum max77686_irq_source group;
};

#define DECLARE_IRQ(idx, _group, _mask)		\
	[(idx)] = { .group = (_group), .mask = (_mask) }
static const struct max77686_irq_data max77686_irqs[] = {
	DECLARE_IRQ(MAX77686_TOPSYSIRQ_PWRONF,	PMIC_INT1, 1 << 0),
	DECLARE_IRQ(MAX77686_TOPSYSIRQ_PWRONR,	PMIC_INT1, 1 << 1),
	DECLARE_IRQ(MAX77686_TOPSYSIRQ_JIGONF,	PMIC_INT1, 1 << 2),
	DECLARE_IRQ(MAX77686_TOPSYSIRQ_JIGONR,	PMIC_INT1, 1 << 3),
	DECLARE_IRQ(MAX77686_TOPSYSIRQ_ACOKBF,	PMIC_INT1, 1 << 4),
	DECLARE_IRQ(MAX77686_TOPSYSIRQ_ACOKBR,	PMIC_INT1, 1 << 5),
	DECLARE_IRQ(MAX77686_TOPSYSIRQ_ONKEY1S,	PMIC_INT1, 1 << 6),
	DECLARE_IRQ(MAX77686_TOPSYSIRQ_MRSTB,	PMIC_INT1, 1 << 7),

	DECLARE_IRQ(MAX77686_TOPSYSIRQ_120C,	PMIC_INT2, 1 << 0),
	DECLARE_IRQ(MAX77686_TOPSYSIRQ_140C,	PMIC_INT2, 1 << 1),
};

static void max77686_irq_lock(struct irq_data *data)
{
	struct max77686_dev *max77686 = irq_get_chip_data(data->irq);

	mutex_lock(&max77686->irqlock);
}

static void max77686_irq_sync_unlock(struct irq_data *data)
{
	struct max77686_dev *max77686 = irq_get_chip_data(data->irq);
	int i;

	for (i = 0; i < MAX77686_IRQ_GROUP_NR; i++) {
		u8 mask_reg = max77686_mask_reg[i];
		struct i2c_client *i2c = get_i2c(max77686, i);

		if (mask_reg == MAX77686_REG_INVALID ||
				IS_ERR_OR_NULL(i2c))
			continue;
		max77686->irq_masks_cache[i] = max77686->irq_masks_cur[i];

		max77686_write_reg(i2c, max77686_mask_reg[i],
				max77686->irq_masks_cur[i]);
	}

	mutex_unlock(&max77686->irqlock);
}

static const inline struct max77686_irq_data *
irq_to_max77686_irq(struct max77686_dev *max77686, int irq)
{
	return &max77686_irqs[irq - max77686->irq_base];
}

static void max77686_irq_mask(struct irq_data *data)
{
	struct max77686_dev *max77686 = irq_get_chip_data(data->irq);
	const struct max77686_irq_data *irq_data = irq_to_max77686_irq(max77686,
								data->irq);

	max77686->irq_masks_cur[irq_data->group] |= irq_data->mask;
}

static void max77686_irq_unmask(struct irq_data *data)
{
	struct max77686_dev *max77686 = irq_get_chip_data(data->irq);
	const struct max77686_irq_data *irq_data = irq_to_max77686_irq(max77686,
								data->irq);

	max77686->irq_masks_cur[irq_data->group] &= ~irq_data->mask;
}

static struct irq_chip max77686_irq_chip = {
	.name			= "max77686",
	.irq_bus_lock		= max77686_irq_lock,
	.irq_bus_sync_unlock	= max77686_irq_sync_unlock,
	.irq_mask		= max77686_irq_mask,
	.irq_unmask		= max77686_irq_unmask,
};

#define MAX77686_IRQSRC_PMIC		(1 << 1)
static irqreturn_t max77686_irq_thread(int irq, void *data)
{
	struct max77686_dev *max77686 = data;
	u8 irq_reg[MAX77686_IRQ_GROUP_NR] = {};
	u8 irq_src;
	int ret;
	int i;

	ret = max77686_read_reg(max77686->i2c, MAX77686_REG_INTSRC, &irq_src);
	if (ret < 0) {
		dev_err(max77686->dev, "Failed to read interrupt source: %d\n",
				ret);
		return IRQ_NONE;
	}

	if (irq_src & MAX77686_IRQSRC_PMIC) {
		/* PMIC INT1 ~ INT4 */
		max77686_bulk_read(max77686->i2c, MAX77686_REG_INT1, 2,
				&irq_reg[PMIC_INT1]);
	}

	/* Apply masking */
	for (i = 0; i < MAX77686_IRQ_GROUP_NR; i++)
		irq_reg[i] &= ~max77686->irq_masks_cur[i];

	/* Report */
	for (i = 0; i < MAX77686_IRQ_NR; i++) {
		if (irq_reg[max77686_irqs[i].group] & max77686_irqs[i].mask)
			handle_nested_irq(max77686->irq_base + i);
	}

	return IRQ_HANDLED;
}

int max77686_irq_resume(struct max77686_dev *max77686)
{
	if (max77686->irq && max77686->irq_base)
		max77686_irq_thread(max77686->irq_base, max77686);
	return 0;
}

int max77686_irq_init(struct max77686_dev *max77686)
{
	int i;
	int cur_irq;
	int ret;

	if (!max77686->irq) {
		dev_warn(max77686->dev, "No interrupt specified.\n");
		max77686->irq_base = 0;
		return 0;
	}

	if (!max77686->irq_base) {
		dev_err(max77686->dev, "No interrupt base specified.\n");
		return 0;
	}

	mutex_init(&max77686->irqlock);

	/* Mask individual interrupt sources */
	for (i = 0; i < MAX77686_IRQ_GROUP_NR; i++) {
		struct i2c_client *i2c;

		max77686->irq_masks_cur[i] = 0xff;
		max77686->irq_masks_cache[i] = 0xff;
		i2c = get_i2c(max77686, i);

		if (IS_ERR_OR_NULL(i2c))
			continue;
		if (max77686_mask_reg[i] == MAX77686_REG_INVALID)
			continue;

		max77686_write_reg(i2c, max77686_mask_reg[i], 0xff);
	}

	/* Register with genirq */
	for (i = 0; i < MAX77686_IRQ_NR; i++) {
		cur_irq = i + max77686->irq_base;
		irq_set_chip_data(cur_irq, max77686);
		irq_set_chip_and_handler(cur_irq, &max77686_irq_chip,
				handle_edge_irq);
		irq_set_nested_thread(cur_irq, 1);
#ifdef CONFIG_ARM
		set_irq_flags(cur_irq, IRQF_VALID);
#else
		irq_set_noprobe(cur_irq);
#endif
	}

	ret = request_threaded_irq(max77686->irq, NULL, max77686_irq_thread,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			"max77686-irq", max77686);

	if (ret) {
		dev_err(max77686->dev, "Failed to request IRQ %d: %d\n",
				max77686->irq, ret);
		return ret;
	}

	if (!max77686->ono)
		return 0;

	ret = request_threaded_irq(max77686->ono, NULL, max77686_irq_thread,
			IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING |
			IRQF_ONESHOT, "max77686-ono", max77686);

	if (ret)
		dev_err(max77686->dev, "Failed to request ono-IRQ %d: %d\n",
				max77686->ono, ret);

	return 0;
}

void max77686_irq_exit(struct max77686_dev *max77686)
{
	if (max77686->ono)
		free_irq(max77686->ono, max77686);

	if (max77686->irq)
		free_irq(max77686->irq, max77686);
}
