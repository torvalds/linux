/*
 * max8997-irq.c - Interrupt controller support for MAX8997
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
#include <linux/mfd/max8997.h>
#include <linux/mfd/max8997-private.h>

static const u8 max8997_mask_reg[] = {
	[PMIC_INT1] = MAX8997_REG_INT1MSK,
	[PMIC_INT2] = MAX8997_REG_INT2MSK,
	[PMIC_INT3] = MAX8997_REG_INT3MSK,
	[PMIC_INT4] = MAX8997_REG_INT4MSK,
	[FUEL_GAUGE] = MAX8997_REG_INVALID,
	[MUIC_INT1] = MAX8997_MUIC_REG_INTMASK1,
	[MUIC_INT2] = MAX8997_MUIC_REG_INTMASK2,
	[MUIC_INT3] = MAX8997_MUIC_REG_INTMASK3,
	[GPIO_LOW] = MAX8997_REG_INVALID,
	[GPIO_HI] = MAX8997_REG_INVALID,
	[FLASH_STATUS] = MAX8997_REG_INVALID,
};

static struct i2c_client *get_i2c(struct max8997_dev *max8997,
				enum max8997_irq_source src)
{
	switch (src) {
	case PMIC_INT1 ... PMIC_INT4:
		return max8997->i2c;
	case FUEL_GAUGE:
		return NULL;
	case MUIC_INT1 ... MUIC_INT3:
		return max8997->muic;
	case GPIO_LOW ... GPIO_HI:
		return max8997->i2c;
	case FLASH_STATUS:
		return max8997->i2c;
	default:
		return ERR_PTR(-EINVAL);
	}
}

struct max8997_irq_data {
	int mask;
	enum max8997_irq_source group;
};

#define DECLARE_IRQ(idx, _group, _mask)		\
	[(idx)] = { .group = (_group), .mask = (_mask) }
static const struct max8997_irq_data max8997_irqs[] = {
	DECLARE_IRQ(MAX8997_PMICIRQ_PWRONR,	PMIC_INT1, 1 << 0),
	DECLARE_IRQ(MAX8997_PMICIRQ_PWRONF,	PMIC_INT1, 1 << 1),
	DECLARE_IRQ(MAX8997_PMICIRQ_PWRON1SEC,	PMIC_INT1, 1 << 3),
	DECLARE_IRQ(MAX8997_PMICIRQ_JIGONR,	PMIC_INT1, 1 << 4),
	DECLARE_IRQ(MAX8997_PMICIRQ_JIGONF,	PMIC_INT1, 1 << 5),
	DECLARE_IRQ(MAX8997_PMICIRQ_LOWBAT2,	PMIC_INT1, 1 << 6),
	DECLARE_IRQ(MAX8997_PMICIRQ_LOWBAT1,	PMIC_INT1, 1 << 7),

	DECLARE_IRQ(MAX8997_PMICIRQ_JIGR,	PMIC_INT2, 1 << 0),
	DECLARE_IRQ(MAX8997_PMICIRQ_JIGF,	PMIC_INT2, 1 << 1),
	DECLARE_IRQ(MAX8997_PMICIRQ_MR,		PMIC_INT2, 1 << 2),
	DECLARE_IRQ(MAX8997_PMICIRQ_DVS1OK,	PMIC_INT2, 1 << 3),
	DECLARE_IRQ(MAX8997_PMICIRQ_DVS2OK,	PMIC_INT2, 1 << 4),
	DECLARE_IRQ(MAX8997_PMICIRQ_DVS3OK,	PMIC_INT2, 1 << 5),
	DECLARE_IRQ(MAX8997_PMICIRQ_DVS4OK,	PMIC_INT2, 1 << 6),

	DECLARE_IRQ(MAX8997_PMICIRQ_CHGINS,	PMIC_INT3, 1 << 0),
	DECLARE_IRQ(MAX8997_PMICIRQ_CHGRM,	PMIC_INT3, 1 << 1),
	DECLARE_IRQ(MAX8997_PMICIRQ_DCINOVP,	PMIC_INT3, 1 << 2),
	DECLARE_IRQ(MAX8997_PMICIRQ_TOPOFFR,	PMIC_INT3, 1 << 3),
	DECLARE_IRQ(MAX8997_PMICIRQ_CHGRSTF,	PMIC_INT3, 1 << 5),
	DECLARE_IRQ(MAX8997_PMICIRQ_MBCHGTMEXPD,	PMIC_INT3, 1 << 7),

	DECLARE_IRQ(MAX8997_PMICIRQ_RTC60S,	PMIC_INT4, 1 << 0),
	DECLARE_IRQ(MAX8997_PMICIRQ_RTCA1,	PMIC_INT4, 1 << 1),
	DECLARE_IRQ(MAX8997_PMICIRQ_RTCA2,	PMIC_INT4, 1 << 2),
	DECLARE_IRQ(MAX8997_PMICIRQ_SMPL_INT,	PMIC_INT4, 1 << 3),
	DECLARE_IRQ(MAX8997_PMICIRQ_RTC1S,	PMIC_INT4, 1 << 4),
	DECLARE_IRQ(MAX8997_PMICIRQ_WTSR,	PMIC_INT4, 1 << 5),

	DECLARE_IRQ(MAX8997_MUICIRQ_ADCError,	MUIC_INT1, 1 << 2),
	DECLARE_IRQ(MAX8997_MUICIRQ_ADCLow,	MUIC_INT1, 1 << 1),
	DECLARE_IRQ(MAX8997_MUICIRQ_ADC,	MUIC_INT1, 1 << 0),

	DECLARE_IRQ(MAX8997_MUICIRQ_VBVolt,	MUIC_INT2, 1 << 4),
	DECLARE_IRQ(MAX8997_MUICIRQ_DBChg,	MUIC_INT2, 1 << 3),
	DECLARE_IRQ(MAX8997_MUICIRQ_DCDTmr,	MUIC_INT2, 1 << 2),
	DECLARE_IRQ(MAX8997_MUICIRQ_ChgDetRun,	MUIC_INT2, 1 << 1),
	DECLARE_IRQ(MAX8997_MUICIRQ_ChgTyp,	MUIC_INT2, 1 << 0),

	DECLARE_IRQ(MAX8997_MUICIRQ_OVP,	MUIC_INT3, 1 << 2),
};

static void max8997_irq_lock(struct irq_data *data)
{
	struct max8997_dev *max8997 = irq_get_chip_data(data->irq);

	mutex_lock(&max8997->irqlock);
}

static void max8997_irq_sync_unlock(struct irq_data *data)
{
	struct max8997_dev *max8997 = irq_get_chip_data(data->irq);
	int i;

	for (i = 0; i < MAX8997_IRQ_GROUP_NR; i++) {
		u8 mask_reg = max8997_mask_reg[i];
		struct i2c_client *i2c = get_i2c(max8997, i);

		if (mask_reg == MAX8997_REG_INVALID ||
				IS_ERR_OR_NULL(i2c))
			continue;
		max8997->irq_masks_cache[i] = max8997->irq_masks_cur[i];

		max8997_write_reg(i2c, max8997_mask_reg[i],
				max8997->irq_masks_cur[i]);
	}

	mutex_unlock(&max8997->irqlock);
}

static const inline struct max8997_irq_data *
irq_to_max8997_irq(struct max8997_dev *max8997, int irq)
{
	struct irq_data *data = irq_get_irq_data(irq);
	return &max8997_irqs[data->hwirq];
}

static void max8997_irq_mask(struct irq_data *data)
{
	struct max8997_dev *max8997 = irq_get_chip_data(data->irq);
	const struct max8997_irq_data *irq_data = irq_to_max8997_irq(max8997,
								data->irq);

	max8997->irq_masks_cur[irq_data->group] |= irq_data->mask;
}

static void max8997_irq_unmask(struct irq_data *data)
{
	struct max8997_dev *max8997 = irq_get_chip_data(data->irq);
	const struct max8997_irq_data *irq_data = irq_to_max8997_irq(max8997,
								data->irq);

	max8997->irq_masks_cur[irq_data->group] &= ~irq_data->mask;
}

static struct irq_chip max8997_irq_chip = {
	.name			= "max8997",
	.irq_bus_lock		= max8997_irq_lock,
	.irq_bus_sync_unlock	= max8997_irq_sync_unlock,
	.irq_mask		= max8997_irq_mask,
	.irq_unmask		= max8997_irq_unmask,
};

#define MAX8997_IRQSRC_PMIC		(1 << 1)
#define MAX8997_IRQSRC_FUELGAUGE	(1 << 2)
#define MAX8997_IRQSRC_MUIC		(1 << 3)
#define MAX8997_IRQSRC_GPIO		(1 << 4)
#define MAX8997_IRQSRC_FLASH		(1 << 5)
static irqreturn_t max8997_irq_thread(int irq, void *data)
{
	struct max8997_dev *max8997 = data;
	u8 irq_reg[MAX8997_IRQ_GROUP_NR] = {};
	u8 irq_src;
	int ret;
	int i, cur_irq;

	ret = max8997_read_reg(max8997->i2c, MAX8997_REG_INTSRC, &irq_src);
	if (ret < 0) {
		dev_err(max8997->dev, "Failed to read interrupt source: %d\n",
				ret);
		return IRQ_NONE;
	}

	if (irq_src & MAX8997_IRQSRC_PMIC) {
		/* PMIC INT1 ~ INT4 */
		max8997_bulk_read(max8997->i2c, MAX8997_REG_INT1, 4,
				&irq_reg[PMIC_INT1]);
	}
	if (irq_src & MAX8997_IRQSRC_FUELGAUGE) {
		/*
		 * TODO: FUEL GAUGE
		 *
		 * This is to be supported by Max17042 driver. When
		 * an interrupt incurs here, it should be relayed to a
		 * Max17042 device that is connected (probably by
		 * platform-data). However, we do not have interrupt
		 * handling in Max17042 driver currently. The Max17042 IRQ
		 * driver should be ready to be used as a stand-alone device and
		 * a Max8997-dependent device. Because it is not ready in
		 * Max17042-side and it is not too critical in operating
		 * Max8997, we do not implement this in initial releases.
		 */
		irq_reg[FUEL_GAUGE] = 0;
	}
	if (irq_src & MAX8997_IRQSRC_MUIC) {
		/* MUIC INT1 ~ INT3 */
		max8997_bulk_read(max8997->muic, MAX8997_MUIC_REG_INT1, 3,
				&irq_reg[MUIC_INT1]);
	}
	if (irq_src & MAX8997_IRQSRC_GPIO) {
		/* GPIO Interrupt */
		u8 gpio_info[MAX8997_NUM_GPIO];

		irq_reg[GPIO_LOW] = 0;
		irq_reg[GPIO_HI] = 0;

		max8997_bulk_read(max8997->i2c, MAX8997_REG_GPIOCNTL1,
				MAX8997_NUM_GPIO, gpio_info);
		for (i = 0; i < MAX8997_NUM_GPIO; i++) {
			bool interrupt = false;

			switch (gpio_info[i] & MAX8997_GPIO_INT_MASK) {
			case MAX8997_GPIO_INT_BOTH:
				if (max8997->gpio_status[i] != gpio_info[i])
					interrupt = true;
				break;
			case MAX8997_GPIO_INT_RISE:
				if ((max8997->gpio_status[i] != gpio_info[i]) &&
				    (gpio_info[i] & MAX8997_GPIO_DATA_MASK))
					interrupt = true;
				break;
			case MAX8997_GPIO_INT_FALL:
				if ((max8997->gpio_status[i] != gpio_info[i]) &&
				    !(gpio_info[i] & MAX8997_GPIO_DATA_MASK))
					interrupt = true;
				break;
			default:
				break;
			}

			if (interrupt) {
				if (i < 8)
					irq_reg[GPIO_LOW] |= (1 << i);
				else
					irq_reg[GPIO_HI] |= (1 << (i - 8));
			}

		}
	}
	if (irq_src & MAX8997_IRQSRC_FLASH) {
		/* Flash Status Interrupt */
		ret = max8997_read_reg(max8997->i2c, MAX8997_REG_FLASHSTATUS,
				&irq_reg[FLASH_STATUS]);
	}

	/* Apply masking */
	for (i = 0; i < MAX8997_IRQ_GROUP_NR; i++)
		irq_reg[i] &= ~max8997->irq_masks_cur[i];

	/* Report */
	for (i = 0; i < MAX8997_IRQ_NR; i++) {
		if (irq_reg[max8997_irqs[i].group] & max8997_irqs[i].mask) {
			cur_irq = irq_find_mapping(max8997->irq_domain, i);
			if (cur_irq)
				handle_nested_irq(cur_irq);
		}
	}

	return IRQ_HANDLED;
}

int max8997_irq_resume(struct max8997_dev *max8997)
{
	if (max8997->irq && max8997->irq_domain)
		max8997_irq_thread(0, max8997);
	return 0;
}

static int max8997_irq_domain_map(struct irq_domain *d, unsigned int irq,
					irq_hw_number_t hw)
{
	struct max8997_dev *max8997 = d->host_data;

	irq_set_chip_data(irq, max8997);
	irq_set_chip_and_handler(irq, &max8997_irq_chip, handle_edge_irq);
	irq_set_nested_thread(irq, 1);
#ifdef CONFIG_ARM
	set_irq_flags(irq, IRQF_VALID);
#else
	irq_set_noprobe(irq);
#endif
	return 0;
}

static const struct irq_domain_ops max8997_irq_domain_ops = {
	.map = max8997_irq_domain_map,
};

int max8997_irq_init(struct max8997_dev *max8997)
{
	struct irq_domain *domain;
	int i;
	int ret;
	u8 val;

	if (!max8997->irq) {
		dev_warn(max8997->dev, "No interrupt specified.\n");
		return 0;
	}

	mutex_init(&max8997->irqlock);

	/* Mask individual interrupt sources */
	for (i = 0; i < MAX8997_IRQ_GROUP_NR; i++) {
		struct i2c_client *i2c;

		max8997->irq_masks_cur[i] = 0xff;
		max8997->irq_masks_cache[i] = 0xff;
		i2c = get_i2c(max8997, i);

		if (IS_ERR_OR_NULL(i2c))
			continue;
		if (max8997_mask_reg[i] == MAX8997_REG_INVALID)
			continue;

		max8997_write_reg(i2c, max8997_mask_reg[i], 0xff);
	}

	for (i = 0; i < MAX8997_NUM_GPIO; i++) {
		max8997->gpio_status[i] = (max8997_read_reg(max8997->i2c,
						MAX8997_REG_GPIOCNTL1 + i,
						&val)
					& MAX8997_GPIO_DATA_MASK) ?
					true : false;
	}

	domain = irq_domain_add_linear(NULL, MAX8997_IRQ_NR,
					&max8997_irq_domain_ops, max8997);
	if (!domain) {
		dev_err(max8997->dev, "could not create irq domain\n");
		return -ENODEV;
	}
	max8997->irq_domain = domain;

	ret = request_threaded_irq(max8997->irq, NULL, max8997_irq_thread,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			"max8997-irq", max8997);

	if (ret) {
		dev_err(max8997->dev, "Failed to request IRQ %d: %d\n",
				max8997->irq, ret);
		return ret;
	}

	if (!max8997->ono)
		return 0;

	ret = request_threaded_irq(max8997->ono, NULL, max8997_irq_thread,
			IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING |
			IRQF_ONESHOT, "max8997-ono", max8997);

	if (ret)
		dev_err(max8997->dev, "Failed to request ono-IRQ %d: %d\n",
				max8997->ono, ret);

	return 0;
}

void max8997_irq_exit(struct max8997_dev *max8997)
{
	if (max8997->ono)
		free_irq(max8997->ono, max8997);

	if (max8997->irq)
		free_irq(max8997->irq, max8997);
}
