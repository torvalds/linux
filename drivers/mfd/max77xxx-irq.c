/*
 * max77xxx-irq.c - Interrupt controller support for MAX77XXX
 *
 * Copyright (C) 2013 Google, Inc
 *
 * Copyright (C) 2012 Samsung Electronics Co.Ltd
 * Chiwoong Byun <woong.byun@samsung.com>
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
 * This driver is based on max8997-irq.c
 */

#include <linux/err.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/mfd/max77xxx.h>
#include <linux/mfd/max77xxx-private.h>
#include <linux/irqdomain.h>
#include <linux/regmap.h>

enum {
	MAX77XXX_DEBUG_IRQ_INFO = 1 << 0,
	MAX77XXX_DEBUG_IRQ_MASK = 1 << 1,
	MAX77XXX_DEBUG_IRQ_INT = 1 << 2,
};

static int debug_mask;
module_param(debug_mask, int, 0);
MODULE_PARM_DESC(debug_mask, "Set debug_mask : 0x0=off 0x1=IRQ_INFO  0x2=IRQ_MASK 0x4=IRQ_INI)");

static const u8 max77xxx_mask_reg[] = {
	[PMIC_INT1] = MAX77XXX_REG_INT1MSK,
	[PMIC_INT2] = MAX77XXX_REG_INT2MSK,
	[RTC_INT] = MAX77XXX_RTC_INTM,
};

static struct regmap *max77xxx_get_regmap(struct max77xxx_dev *max77xxx,
				enum max77xxx_irq_source src)
{
	switch (src) {
	case PMIC_INT1 ... PMIC_INT2:
		return max77xxx->regmap;
	case RTC_INT:
		return max77xxx->rtc_regmap;
	default:
		return ERR_PTR(-EINVAL);
	}
}

struct max77xxx_irq_data {
	int mask;
	enum max77xxx_irq_source group;
};

#define DECLARE_IRQ(idx, _group, _mask)		\
	[(idx)] = { .group = (_group), .mask = (_mask) }
static const struct max77xxx_irq_data max77xxx_irqs[] = {
	DECLARE_IRQ(MAX77XXX_PMICIRQ_PWRONF,	PMIC_INT1, 1 << 0),
	DECLARE_IRQ(MAX77XXX_PMICIRQ_PWRONR,	PMIC_INT1, 1 << 1),
	DECLARE_IRQ(MAX77XXX_PMICIRQ_JIGONBF,	PMIC_INT1, 1 << 2),
	DECLARE_IRQ(MAX77XXX_PMICIRQ_JIGONBR,	PMIC_INT1, 1 << 3),
	DECLARE_IRQ(MAX77XXX_PMICIRQ_ACOKBF,	PMIC_INT1, 1 << 4),
	DECLARE_IRQ(MAX77XXX_PMICIRQ_ACOKBR,	PMIC_INT1, 1 << 5),
	DECLARE_IRQ(MAX77XXX_PMICIRQ_ONKEY1S,	PMIC_INT1, 1 << 6),
	DECLARE_IRQ(MAX77XXX_PMICIRQ_MRSTB,		PMIC_INT1, 1 << 7),
	DECLARE_IRQ(MAX77XXX_PMICIRQ_140C,		PMIC_INT2, 1 << 0),
	DECLARE_IRQ(MAX77XXX_PMICIRQ_120C,		PMIC_INT2, 1 << 1),
	DECLARE_IRQ(MAX77XXX_RTCIRQ_RTC60S,		RTC_INT, 1 << 0),
	DECLARE_IRQ(MAX77XXX_RTCIRQ_RTCA1,		RTC_INT, 1 << 1),
	DECLARE_IRQ(MAX77XXX_RTCIRQ_RTCA2,		RTC_INT, 1 << 2),
	DECLARE_IRQ(MAX77XXX_RTCIRQ_SMPL,		RTC_INT, 1 << 3),
	DECLARE_IRQ(MAX77XXX_RTCIRQ_RTC1S,		RTC_INT, 1 << 4),
	DECLARE_IRQ(MAX77XXX_RTCIRQ_WTSR,		RTC_INT, 1 << 5),
};

static void max77xxx_irq_lock(struct irq_data *data)
{
	struct max77xxx_dev *max77xxx = irq_get_chip_data(data->irq);

	if (debug_mask & MAX77XXX_DEBUG_IRQ_MASK)
		pr_info("%s\n", __func__);

	mutex_lock(&max77xxx->irqlock);
}

static void max77xxx_irq_sync_unlock(struct irq_data *data)
{
	struct max77xxx_dev *max77xxx = irq_get_chip_data(data->irq);
	int i;

	for (i = 0; i < MAX77XXX_IRQ_GROUP_NR; i++) {
		u8 mask_reg = max77xxx_mask_reg[i];
		struct regmap *map = max77xxx_get_regmap(max77xxx, i);

		if (debug_mask & MAX77XXX_DEBUG_IRQ_MASK)
			pr_debug("%s: mask_reg[%d]=0x%x, cur=0x%x\n",
			__func__, i, mask_reg, max77xxx->irq_masks_cur[i]);

		if (mask_reg == MAX77XXX_REG_INVALID ||
				IS_ERR_OR_NULL(map))
			continue;

		max77xxx->irq_masks_cache[i] = max77xxx->irq_masks_cur[i];

		regmap_write(map, max77xxx_mask_reg[i],
				max77xxx->irq_masks_cur[i]);
	}

	mutex_unlock(&max77xxx->irqlock);
}

static const inline struct max77xxx_irq_data *to_max77xxx_irq(int irq)
{
	struct irq_data *data = irq_get_irq_data(irq);
	return &max77xxx_irqs[data->hwirq];
}

static void max77xxx_irq_mask(struct irq_data *data)
{
	struct max77xxx_dev *max77xxx = irq_get_chip_data(data->irq);
	const struct max77xxx_irq_data *irq_data = to_max77xxx_irq(data->irq);

	max77xxx->irq_masks_cur[irq_data->group] |= irq_data->mask;

	if (debug_mask & MAX77XXX_DEBUG_IRQ_MASK)
		pr_info("%s: group=%d, cur=0x%x\n",
			__func__, irq_data->group,
			max77xxx->irq_masks_cur[irq_data->group]);
}

static void max77xxx_irq_unmask(struct irq_data *data)
{
	struct max77xxx_dev *max77xxx = irq_get_chip_data(data->irq);
	const struct max77xxx_irq_data *irq_data = to_max77xxx_irq(data->irq);

	max77xxx->irq_masks_cur[irq_data->group] &= ~irq_data->mask;

	if (debug_mask & MAX77XXX_DEBUG_IRQ_MASK)
		pr_info("%s: group=%d, cur=0x%x\n",
			__func__, irq_data->group,
			max77xxx->irq_masks_cur[irq_data->group]);
}

static struct irq_chip max77xxx_irq_chip = {
	.name			= "max77xxx",
	.irq_bus_lock		= max77xxx_irq_lock,
	.irq_bus_sync_unlock	= max77xxx_irq_sync_unlock,
	.irq_mask		= max77xxx_irq_mask,
	.irq_unmask		= max77xxx_irq_unmask,
};

static irqreturn_t max77xxx_irq_thread(int irq, void *data)
{
	struct max77xxx_dev *max77xxx = data;
	unsigned int irq_reg[MAX77XXX_IRQ_GROUP_NR] = {};
	unsigned int irq_src;
	int ret;
	int i, cur_irq;

	ret = regmap_read(max77xxx->regmap,  MAX77XXX_REG_INTSRC, &irq_src);
	if (ret < 0) {
		dev_err(max77xxx->dev, "Failed to read interrupt source: %d\n",
				ret);
		return IRQ_NONE;
	}

	if (debug_mask & MAX77XXX_DEBUG_IRQ_INT)
		pr_info("%s: irq_src=0x%x\n", __func__, irq_src);

	if (irq_src == MAX77XXX_IRQSRC_PMIC) {
		ret = regmap_bulk_read(max77xxx->regmap,
					 MAX77XXX_REG_INT1, irq_reg, 2);
		if (ret < 0) {
			dev_err(max77xxx->dev, "Failed to read interrupt source: %d\n",
					ret);
			return IRQ_NONE;
		}

		if (debug_mask & MAX77XXX_DEBUG_IRQ_INT)
			pr_info("%s: int1=0x%x, int2=0x%x\n", __func__,
				 irq_reg[PMIC_INT1], irq_reg[PMIC_INT2]);
	}

	if (irq_src & MAX77XXX_IRQSRC_RTC) {
		ret = regmap_read(max77xxx->rtc_regmap,
					MAX77XXX_RTC_INT, &irq_reg[RTC_INT]);
		if (ret < 0) {
			dev_err(max77xxx->dev, "Failed to read interrupt source: %d\n",
					ret);
			return IRQ_NONE;
		}

		if (debug_mask & MAX77XXX_DEBUG_IRQ_INT)
			pr_info("%s: rtc int=0x%x\n", __func__,
							 irq_reg[RTC_INT]);

	}

	for (i = 0; i < MAX77XXX_IRQ_GROUP_NR; i++)
		irq_reg[i] &= ~max77xxx->irq_masks_cur[i];

	for (i = 0; i < MAX77XXX_IRQ_NR; i++) {
		if (irq_reg[max77xxx_irqs[i].group] & max77xxx_irqs[i].mask) {
			cur_irq = irq_find_mapping(max77xxx->irq_domain, i);
			if (cur_irq)
				handle_nested_irq(cur_irq);
		}
	}

	return IRQ_HANDLED;
}

static int max77xxx_irq_domain_map(struct irq_domain *d, unsigned int irq,
					irq_hw_number_t hw)
{
	struct max77xxx_dev *max77xxx = d->host_data;

	irq_set_chip_data(irq, max77xxx);
	irq_set_chip_and_handler(irq, &max77xxx_irq_chip, handle_edge_irq);
	irq_set_nested_thread(irq, 1);
#ifdef CONFIG_ARM
	set_irq_flags(irq, IRQF_VALID);
#else
	irq_set_noprobe(irq);
#endif
	return 0;
}

static struct irq_domain_ops max77xxx_irq_domain_ops = {
	.map = max77xxx_irq_domain_map,
};

int max77xxx_irq_init(struct max77xxx_dev *max77xxx)
{
	struct irq_domain *domain;
	int i;
	int ret;
	int val;
	struct regmap *map;

	mutex_init(&max77xxx->irqlock);

	if (max77xxx->irq_gpio && !max77xxx->irq) {
		max77xxx->irq = gpio_to_irq(max77xxx->irq_gpio);

		if (debug_mask & MAX77XXX_DEBUG_IRQ_INT) {
			ret = gpio_request(max77xxx->irq_gpio, "pmic_irq");
			if (ret < 0) {
				dev_err(max77xxx->dev,
					"Failed to request gpio %d with ret:"
					"%d\n",	max77xxx->irq_gpio, ret);
				return IRQ_NONE;
			}

			gpio_direction_input(max77xxx->irq_gpio);
			val = gpio_get_value(max77xxx->irq_gpio);
			gpio_free(max77xxx->irq_gpio);
			pr_info("%s: gpio_irq=%x\n", __func__, val);
		}
	}

	if (!max77xxx->irq) {
		dev_err(max77xxx->dev, "irq is not specified\n");
		return -ENODEV;
	}

	/* Mask individual interrupt sources */
	for (i = 0; i < MAX77XXX_IRQ_GROUP_NR; i++) {
		max77xxx->irq_masks_cur[i] = 0xff;
		max77xxx->irq_masks_cache[i] = 0xff;
		map = max77xxx_get_regmap(max77xxx, i);

		if (IS_ERR_OR_NULL(map))
			continue;
		if (max77xxx_mask_reg[i] == MAX77XXX_REG_INVALID)
			continue;

		regmap_write(map, max77xxx_mask_reg[i], 0xff);
	}
	domain = irq_domain_add_linear(NULL, MAX77XXX_IRQ_NR,
					&max77xxx_irq_domain_ops, max77xxx);
	if (!domain) {
		dev_err(max77xxx->dev, "could not create irq domain\n");
		return -ENODEV;
	}
	max77xxx->irq_domain = domain;

	ret = request_threaded_irq(max77xxx->irq, NULL, max77xxx_irq_thread,
				   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				   "max77xxx-irq", max77xxx);

	if (ret)
		dev_err(max77xxx->dev, "Failed to request IRQ %d: %d\n",
			max77xxx->irq, ret);


	if (debug_mask & MAX77XXX_DEBUG_IRQ_INFO)
		pr_info("%s-\n", __func__);

	return 0;
}

void max77xxx_irq_exit(struct max77xxx_dev *max77xxx)
{
	if (max77xxx->irq)
		free_irq(max77xxx->irq, max77xxx);
}
