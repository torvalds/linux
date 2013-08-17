/*
 * max77802-irq.c - Interrupt controller support for MAX77802
 *
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
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
 * This driver is based on max77686-irq.c
 */

#include <linux/err.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/mfd/max77802.h>
#include <linux/mfd/max77802-private.h>

#undef MAX77802_IRQ_TEST

enum {
	MAX77802_DEBUG_IRQ_INFO = 1 << 0,
	MAX77802_DEBUG_IRQ_MASK = 1 << 1,
	MAX77802_DEBUG_IRQ_INT = 1 << 2,
};

static int debug_mask = MAX77802_DEBUG_IRQ_INFO | MAX77802_DEBUG_IRQ_MASK |
						MAX77802_DEBUG_IRQ_INT;

static const u8 max77802_mask_reg[] = {
	[PMIC_INT1] = MAX77802_REG_INT1MSK,
	[PMIC_INT2] = MAX77802_REG_INT2MSK,
	[RTC_INT] = MAX77802_RTC_INTM,
};

static struct i2c_client *max77802_get_i2c(struct max77802_dev *max77802,
				enum max77802_irq_source src)
{
	switch (src) {
	case PMIC_INT1:
	case PMIC_INT2:
	case RTC_INT:
		return max77802->i2c;
	default:
		return ERR_PTR(-EINVAL);
	}
}

struct max77802_irq_data {
	int mask;
	enum max77802_irq_source group;
};

#define DECLARE_IRQ(idx, _group, _mask)		\
	[(idx)] = { .group = (_group), .mask = (_mask) }
static const struct max77802_irq_data max77802_irqs[] = {
	DECLARE_IRQ(MAX77802_PMICIRQ_PWRONF,	PMIC_INT1, 1 << 0),
	DECLARE_IRQ(MAX77802_PMICIRQ_PWRONR,	PMIC_INT1, 1 << 1),
	DECLARE_IRQ(MAX77802_PMICIRQ_JIGONBF,	PMIC_INT1, 1 << 2),
	DECLARE_IRQ(MAX77802_PMICIRQ_JIGONBR,	PMIC_INT1, 1 << 3),
	DECLARE_IRQ(MAX77802_PMICIRQ_ACOKBF,	PMIC_INT1, 1 << 4),
	DECLARE_IRQ(MAX77802_PMICIRQ_ACOKBR,	PMIC_INT1, 1 << 5),
	DECLARE_IRQ(MAX77802_PMICIRQ_ONKEY1S,	PMIC_INT1, 1 << 6),
	DECLARE_IRQ(MAX77802_PMICIRQ_MRSTB,		PMIC_INT1, 1 << 7),
	DECLARE_IRQ(MAX77802_PMICIRQ_140C,		PMIC_INT2, 1 << 0),
	DECLARE_IRQ(MAX77802_PMICIRQ_120C,		PMIC_INT2, 1 << 1),
	DECLARE_IRQ(MAX77802_RTCIRQ_RTC60S,		RTC_INT, 1 << 0),
	DECLARE_IRQ(MAX77802_RTCIRQ_RTCA1,		RTC_INT, 1 << 1),
	DECLARE_IRQ(MAX77802_RTCIRQ_RTCA2,		RTC_INT, 1 << 2),
	DECLARE_IRQ(MAX77802_RTCIRQ_SMPL,		RTC_INT, 1 << 3),
	DECLARE_IRQ(MAX77802_RTCIRQ_RTC1S,		RTC_INT, 1 << 4),
	DECLARE_IRQ(MAX77802_RTCIRQ_WTSR,		RTC_INT, 1 << 5),
};

static void max77802_irq_lock(struct irq_data *data)
{
	struct max77802_dev *max77802 = irq_get_chip_data(data->irq);

	if (debug_mask & MAX77802_DEBUG_IRQ_MASK)
		pr_debug("%s\n", __func__);

	mutex_lock(&max77802->irqlock);
}

static void max77802_irq_sync_unlock(struct irq_data *data)
{
	struct max77802_dev *max77802 = irq_get_chip_data(data->irq);
	int i;

	for (i = 0; i < MAX77802_IRQ_GROUP_NR; i++) {
		u8 mask_reg = max77802_mask_reg[i];
		struct i2c_client *i2c = max77802_get_i2c(max77802, i);

		if (debug_mask & MAX77802_DEBUG_IRQ_MASK)
			pr_debug("%s: mask_reg[%d]=0x%x, cur=0x%x\n", __func__,
				i, mask_reg, max77802->irq_masks_cur[i]);

		if (mask_reg == MAX77802_REG_INVALID ||
				IS_ERR_OR_NULL(i2c))
			continue;

		max77802->irq_masks_cache[i] = max77802->irq_masks_cur[i];

		max77802_write_reg(i2c, max77802_mask_reg[i],
				max77802->irq_masks_cur[i]);
	}

	mutex_unlock(&max77802->irqlock);
}

static const inline struct max77802_irq_data *
irq_to_max77802_irq(struct max77802_dev *max77802, int irq)
{
	return &max77802_irqs[irq - max77802->irq_base];
}

static void max77802_irq_mask(struct irq_data *data)
{
	struct max77802_dev *max77802 = irq_get_chip_data(data->irq);
	const struct max77802_irq_data *irq_data = irq_to_max77802_irq(max77802,
								       data->irq);

	max77802->irq_masks_cur[irq_data->group] |= irq_data->mask;

	if (debug_mask & MAX77802_DEBUG_IRQ_MASK)
		pr_info("%s: group=%d, cur=0x%x\n",
			__func__, irq_data->group,
			max77802->irq_masks_cur[irq_data->group]);
}

static void max77802_irq_unmask(struct irq_data *data)
{
	struct max77802_dev *max77802 = irq_get_chip_data(data->irq);
	const struct max77802_irq_data *irq_data = irq_to_max77802_irq(max77802,
								       data->irq);

	max77802->irq_masks_cur[irq_data->group] &= ~irq_data->mask;

	if (debug_mask & MAX77802_DEBUG_IRQ_MASK)
		pr_info("%s: group=%d, cur=0x%x\n",
			__func__, irq_data->group,
			max77802->irq_masks_cur[irq_data->group]);
}

static struct irq_chip max77802_irq_chip = {
	.name			= "max77802",
	.irq_bus_lock		= max77802_irq_lock,
	.irq_bus_sync_unlock	= max77802_irq_sync_unlock,
	.irq_mask		= max77802_irq_mask,
	.irq_unmask		= max77802_irq_unmask,
};

static irqreturn_t max77802_irq_thread(int irq, void *data)
{
	struct max77802_dev *max77802 = data;
	u8 irq_reg[MAX77802_IRQ_GROUP_NR] = {};
	u8 irq_src;
	int ret;
	int i;

	ret = max77802_read_reg(max77802->i2c, MAX77802_REG_INTSRC, &irq_src);
	if (ret < 0) {
		dev_err(max77802->dev, "Failed to read interrupt source: %d\n",
				ret);
		return IRQ_NONE;
	}

	if (debug_mask & MAX77802_DEBUG_IRQ_INT)
		pr_info("%s: irq_src=0x%x\n", __func__, irq_src);

	/* MAX77802_IRQSRC_RTC may be set even if there are pending at INT1/2 */
	ret = max77802_read_reg(max77802->i2c, MAX77802_REG_INT1, &irq_reg[0]);
	ret = max77802_read_reg(max77802->i2c, MAX77802_REG_INT2, &irq_reg[1]);
	if (ret < 0) {
		dev_err(max77802->dev, "Failed to read pmic interrupt: %d\n",
				ret);
		return IRQ_NONE;
	}

	if (debug_mask & MAX77802_DEBUG_IRQ_INT)
		pr_info("%s: int1=0x%x, int2=0x%x\n",
			__func__, irq_reg[PMIC_INT1], irq_reg[PMIC_INT2]);

	if (irq_src & MAX77802_IRQSRC_RTC) {
#ifdef CONFIG_RTC_DRV_MAX77802
		ret = max77802_read_reg(max77802->i2c, MAX77802_RTC_INT, &irq_reg[RTC_INT]);
#else
		ret = -ENODEV;
#endif
		if (ret < 0) {
			dev_err(max77802->dev, "Failed to read rtc interrupt: %d\n",
					ret);
			return IRQ_NONE;
		}

		if (debug_mask & MAX77802_DEBUG_IRQ_INT)
			pr_info("%s: rtc int=0x%x\n", __func__, irq_reg[RTC_INT]);

	}

	for (i = 0; i < MAX77802_IRQ_NR; i++) {
		if (irq_reg[max77802_irqs[i].group] & max77802_irqs[i].mask)
			handle_nested_irq(max77802->irq_base + i);
	}

	return IRQ_HANDLED;
}

int max77802_irq_resume(struct max77802_dev *max77802)
{
	if (max77802->irq && max77802->irq_base)
		max77802_irq_thread(max77802->irq_base, max77802);
	return 0;
}

int max77802_irq_init(struct max77802_dev *max77802)
{
	int i;
	int cur_irq;
	int ret;
	int val;
#ifdef MAX77802_IRQ_TEST
	u8 irq_reg[6] = { };
	u8 irq_src;
#endif

	if (debug_mask & MAX77802_DEBUG_IRQ_INFO)
		pr_info("%s+\n", __func__);

	if (!max77802->irq_gpio) {
		dev_warn(max77802->dev, "No interrupt gpio specified.\n");
		max77802->irq_base = 0;
		return 0;
	}

	if (!max77802->irq_base) {
		dev_err(max77802->dev, "No interrupt base specified.\n");
		return 0;
	}

	mutex_init(&max77802->irqlock);

	max77802->irq = gpio_to_irq(max77802->irq_gpio);
	ret = gpio_request(max77802->irq_gpio, "pmic_irq");
	if (ret < 0 && ret != -EBUSY) {
		dev_err(max77802->dev,
			"Failed to request gpio %d with ret: %d\n",
			max77802->irq_gpio, ret);
		return IRQ_NONE;
	}
	if (ret == -EBUSY)
		dev_warn(max77802->dev, "gpio pmic_irq is already requested\n");

	gpio_direction_input(max77802->irq_gpio);
	val = gpio_get_value(max77802->irq_gpio);
	gpio_free(max77802->irq_gpio);

	if (debug_mask & MAX77802_DEBUG_IRQ_INT)
		pr_info("%s: gpio_irq=%x\n", __func__, val);

#ifdef MAX77802_IRQ_TEST
	ret = max77802_read_reg(max77802->i2c, MAX77802_REG_INTSRC, &irq_src);
	if (ret < 0) {
		dev_err(max77802->dev, "Failed to read interrupt source: %d\n",
			ret);
		return IRQ_NONE;
	}

	pr_info("%s: irq_src=0x%x\n", __func__, irq_src);

	ret = max77802_bulk_read(max77802->i2c, MAX77802_REG_INT1, 6, irq_reg);
	if (ret < 0) {
		dev_err(max77802->dev, "Failed to read interrupt source: %d\n",
			ret);
		return IRQ_NONE;
	}

	for (i = 0; i < 6; i++)
		pr_info("%s: i[%d]=0x%x\n", __func__, i, irq_reg[i]);
#endif /* MAX77802_IRQ_TEST */

	/* Mask individual interrupt sources */
	for (i = 0; i < MAX77802_IRQ_GROUP_NR; i++) {
		struct i2c_client *i2c;

		max77802->irq_masks_cur[i] = 0xff;
		max77802->irq_masks_cache[i] = 0xff;
		i2c = max77802_get_i2c(max77802, i);

		if (IS_ERR_OR_NULL(i2c))
			continue;
		if (max77802_mask_reg[i] == MAX77802_REG_INVALID)
			continue;

		max77802_write_reg(i2c, max77802_mask_reg[i], 0xff);
	}

	/* Register with genirq */
	for (i = 0; i < MAX77802_IRQ_NR; i++) {
		cur_irq = i + max77802->irq_base;
		irq_set_chip_data(cur_irq, max77802);
		irq_set_chip_and_handler(cur_irq, &max77802_irq_chip,
					 handle_edge_irq);
		irq_set_nested_thread(cur_irq, 1);
#ifdef CONFIG_ARM
		set_irq_flags(cur_irq, IRQF_VALID);
#else
		irq_set_noprobe(cur_irq);
#endif
	}

	ret = request_threaded_irq(max77802->irq, NULL, max77802_irq_thread,
				   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				   "max77802-irq", max77802);

	if (ret) {
		dev_err(max77802->dev, "Failed to request IRQ %d: %d\n",
			max77802->irq, ret);
		return ret;
	}

	if (debug_mask & MAX77802_DEBUG_IRQ_INFO)
		pr_info("%s-\n", __func__);

	return 0;
}

void max77802_irq_exit(struct max77802_dev *max77802)
{
	if (max77802->irq)
		free_irq(max77802->irq, max77802);
}
