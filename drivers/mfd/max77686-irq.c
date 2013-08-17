/*
 * max77686-irq.c - Interrupt controller support for MAX77686
 *
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
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
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mfd/max77686.h>
#include <linux/mfd/max77686-private.h>

enum {
	MAX77686_DEBUG_IRQ_INFO = 1 << 0,
	MAX77686_DEBUG_IRQ_MASK = 1 << 1,
	MAX77686_DEBUG_IRQ_INT = 1 << 2,
};

static int debug_mask;

static const u8 max77686_mask_reg[] = {
	[PMIC_INT1] = MAX77686_REG_INT1MSK,
	[PMIC_INT2] = MAX77686_REG_INT2MSK,
	[RTC_INT] = MAX77686_RTC_INTM,
};

static struct i2c_client *max77686_get_i2c(struct max77686_dev *max77686,
					   enum max77686_irq_source src)
{
	switch (src) {
	case PMIC_INT1...PMIC_INT2:
		return max77686->i2c;
	case RTC_INT:
		return max77686->rtc;
	default:
		return ERR_PTR(-EINVAL);
	}
}

struct max77686_irq_data {
	int mask;
	enum max77686_irq_source group;
};

#define DECLARE_IRQ(_group, _mask)		\
	{ .group = (_group), .mask = (_mask) }
static const struct max77686_irq_data max77686_irqs[] = {
	[MAX77686_PMICIRQ_PWRONF]	= DECLARE_IRQ(PMIC_INT1, 1 << 0),
	[MAX77686_PMICIRQ_PWRONR]	= DECLARE_IRQ(PMIC_INT1, 1 << 1),
	[MAX77686_PMICIRQ_JIGONBF]	= DECLARE_IRQ(PMIC_INT1, 1 << 2),
	[MAX77686_PMICIRQ_JIGONBR]	= DECLARE_IRQ(PMIC_INT1, 1 << 3),
	[MAX77686_PMICIRQ_ACOKBF]	= DECLARE_IRQ(PMIC_INT1, 1 << 4),
	[MAX77686_PMICIRQ_ACOKBR]	= DECLARE_IRQ(PMIC_INT1, 1 << 5),
	[MAX77686_PMICIRQ_ONKEY1S]	= DECLARE_IRQ(PMIC_INT1, 1 << 6),
	[MAX77686_PMICIRQ_MRSTB]	= DECLARE_IRQ(PMIC_INT1, 1 << 7),
	[MAX77686_PMICIRQ_140C]		= DECLARE_IRQ(PMIC_INT2, 1 << 0),
	[MAX77686_PMICIRQ_120C]		= DECLARE_IRQ(PMIC_INT2, 1 << 1),
	[MAX77686_RTCIRQ_RTC60S]	= DECLARE_IRQ(RTC_INT, 1 << 0),
	[MAX77686_RTCIRQ_RTCA1]		= DECLARE_IRQ(RTC_INT, 1 << 1),
	[MAX77686_RTCIRQ_RTCA2]		= DECLARE_IRQ(RTC_INT, 1 << 2),
	[MAX77686_RTCIRQ_SMPL]		= DECLARE_IRQ(RTC_INT, 1 << 3),
	[MAX77686_RTCIRQ_RTC1S]		= DECLARE_IRQ(RTC_INT, 1 << 4),
	[MAX77686_RTCIRQ_WTSR]		= DECLARE_IRQ(RTC_INT, 1 << 5),
};

static void max77686_irq_lock(struct irq_data *data)
{
	struct max77686_dev *max77686 = irq_get_chip_data(data->irq);

	if (debug_mask & MAX77686_DEBUG_IRQ_MASK)
		pr_info("%s\n", __func__);

	mutex_lock(&max77686->irqlock);
}

static void max77686_irq_sync_unlock(struct irq_data *data)
{
	struct max77686_dev *max77686 = irq_get_chip_data(data->irq);
	int i;

	for (i = 0; i < MAX77686_IRQ_GROUP_NR; i++) {
		u8 mask_reg = max77686_mask_reg[i];
		struct i2c_client *i2c = max77686_get_i2c(max77686, i);

		if (debug_mask & MAX77686_DEBUG_IRQ_MASK)
			pr_info("%s: mask_reg[%d]=0x%x, cur=0x%x\n",
				__func__, i, mask_reg,
				max77686->irq_masks_cur[i]);

		if (mask_reg == MAX77686_REG_INVALID || IS_ERR_OR_NULL(i2c))
			continue;

		max77686->irq_masks_cache[i] = max77686->irq_masks_cur[i];

		max77686_write_reg(i2c, max77686_mask_reg[i],
				   max77686->irq_masks_cur[i]);
	}

	mutex_unlock(&max77686->irqlock);
}

static inline const struct max77686_irq_data *irq_to_max77686_irq(
					struct max77686_dev *max77686, int irq)
{
	return &max77686_irqs[irq - max77686->irq_base];
}

static void max77686_irq_mask(struct irq_data *data)
{
	struct max77686_dev *max77686 = irq_get_chip_data(data->irq);
	const struct max77686_irq_data *irq_data =
	    irq_to_max77686_irq(max77686, data->irq);

	max77686->irq_masks_cur[irq_data->group] |= irq_data->mask;

	if (debug_mask & MAX77686_DEBUG_IRQ_MASK)
		pr_info("%s: group=%d, cur=0x%x\n",
			__func__, irq_data->group,
			max77686->irq_masks_cur[irq_data->group]);
}

static void max77686_irq_unmask(struct irq_data *data)
{
	struct max77686_dev *max77686 = irq_get_chip_data(data->irq);
	const struct max77686_irq_data *irq_data =
	    irq_to_max77686_irq(max77686, data->irq);

	max77686->irq_masks_cur[irq_data->group] &= ~irq_data->mask;

	if (debug_mask & MAX77686_DEBUG_IRQ_MASK)
		pr_info("%s: group=%d, cur=0x%x\n",
			__func__, irq_data->group,
			max77686->irq_masks_cur[irq_data->group]);
}

static struct irq_chip max77686_irq_chip = {
	.name = "max77686",
	.irq_bus_lock = max77686_irq_lock,
	.irq_bus_sync_unlock = max77686_irq_sync_unlock,
	.irq_mask = max77686_irq_mask,
	.irq_unmask = max77686_irq_unmask,
};

static irqreturn_t max77686_irq_thread(int irq, void *data)
{
	struct max77686_dev *max77686 = data;
	u8 irq_reg[MAX77686_IRQ_GROUP_NR] = { };
	u8 irq_src;
	int ret;
	int i;

	ret = max77686_read_reg(max77686->i2c, MAX77686_REG_INTSRC, &irq_src);
	if (ret < 0) {
		dev_err(max77686->dev, "Failed to read interrupt source: %d\n",
			ret);
		return IRQ_NONE;
	}

	if (debug_mask & MAX77686_DEBUG_IRQ_INT)
		pr_info("%s: irq_src=0x%x\n", __func__, irq_src);

	/* MAX77686_IRQSRC_RTC may be set even if there are pending at INT1/2 */
	ret = max77686_read_reg(max77686->i2c, MAX77686_REG_INT1, &irq_reg[0]);
	ret = max77686_read_reg(max77686->i2c, MAX77686_REG_INT2, &irq_reg[1]);
	if (ret < 0) {
		dev_err(max77686->dev, "Failed to read pmic interrupt: %d\n",
			ret);
		return IRQ_NONE;
	}

	if (debug_mask & MAX77686_DEBUG_IRQ_INT)
		pr_info("%s: int1=0x%x, int2=0x%x\n",
			__func__, irq_reg[PMIC_INT1], irq_reg[PMIC_INT2]);

	if (irq_src & MAX77686_IRQSRC_RTC) {
#ifdef CONFIG_RTC_DRV_MAX77686
		ret =
		    max77686_read_reg(max77686->rtc, MAX77686_RTC_INT,
				      &irq_reg[RTC_INT]);
#else
		ret = -ENODEV;
#endif
		if (ret < 0) {
			dev_err(max77686->dev,
				"Failed to read rtc interrupt: %d\n", ret);
			return IRQ_NONE;
		}

		if (debug_mask & MAX77686_DEBUG_IRQ_INT)
			pr_info("%s: rtc int=0x%x\n", __func__,
				irq_reg[RTC_INT]);

	}

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
	int val;

	if (debug_mask & MAX77686_DEBUG_IRQ_INFO)
		pr_info("%s+\n", __func__);

	if (!max77686->irq_gpio) {
		dev_warn(max77686->dev, "No interrupt gpio specified.\n");
		max77686->irq_base = 0;
		return 0;
	}

	if (!max77686->irq_base) {
		dev_err(max77686->dev, "No interrupt base specified.\n");
		return 0;
	}

	mutex_init(&max77686->irqlock);

	max77686->irq = gpio_to_irq(max77686->irq_gpio);
	ret = gpio_request(max77686->irq_gpio, "pmic_irq");
	if (ret < 0 && ret != -EBUSY) {
		dev_err(max77686->dev,
			"Failed to request gpio %d with ret: %d\n",
			max77686->irq_gpio, ret);
		return IRQ_NONE;
	}
	if (ret == -EBUSY)
		dev_warn(max77686->dev, "gpio pmic_irq is already requested\n");

	gpio_direction_input(max77686->irq_gpio);
	val = gpio_get_value(max77686->irq_gpio);
	gpio_free(max77686->irq_gpio);

	if (debug_mask & MAX77686_DEBUG_IRQ_INT)
		pr_info("%s: gpio_irq=%x\n", __func__, val);

	/* Mask individual interrupt sources */
	for (i = 0; i < MAX77686_IRQ_GROUP_NR; i++) {
		struct i2c_client *i2c;

		max77686->irq_masks_cur[i] = 0xff;
		max77686->irq_masks_cache[i] = 0xff;
		i2c = max77686_get_i2c(max77686, i);

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
				   IRQF_TRIGGER_LOW | IRQF_ONESHOT,
				   "max77686-irq", max77686);

	if (ret) {
		dev_err(max77686->dev, "Failed to request IRQ %d: %d\n",
			max77686->irq, ret);
		return ret;
	}

	if (debug_mask & MAX77686_DEBUG_IRQ_INFO)
		pr_info("%s-\n", __func__);

	return 0;
}

void max77686_irq_exit(struct max77686_dev *max77686)
{
	if (max77686->irq)
		free_irq(max77686->irq, max77686);
}
