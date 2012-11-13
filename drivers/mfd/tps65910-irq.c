/*
 * tps65910-irq.c  --  TI TPS6591x
 *
 * Copyright 2010 Texas Instruments Inc.
 *
 * Author: Graeme Gregory <gg@slimlogic.co.uk>
 * Author: Jorge Eduardo Candelaria <jedu@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bug.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/gpio.h>
#include <linux/mfd/tps65910.h>


static const struct regmap_irq tps65911_irqs[] = {
	/* INT_STS */
	[TPS65911_IRQ_PWRHOLD_F] = {
		.mask = INT_MSK_PWRHOLD_F_IT_MSK_MASK,
		.reg_offset = 0,
	},
	[TPS65911_IRQ_VBAT_VMHI] = {
		.mask = INT_MSK_VMBHI_IT_MSK_MASK,
		.reg_offset = 0,
	},
	[TPS65911_IRQ_PWRON] = {
		.mask = INT_MSK_PWRON_IT_MSK_MASK,
		.reg_offset = 0,
	},
	[TPS65911_IRQ_PWRON_LP] = {
		.mask = INT_MSK_PWRON_LP_IT_MSK_MASK,
		.reg_offset = 0,
	},
	[TPS65911_IRQ_PWRHOLD_R] = {
		.mask = INT_MSK_PWRHOLD_R_IT_MSK_MASK,
		.reg_offset = 0,
	},
	[TPS65911_IRQ_HOTDIE] = {
		.mask = INT_MSK_HOTDIE_IT_MSK_MASK,
		.reg_offset = 0,
	},
	[TPS65911_IRQ_RTC_ALARM] = {
		.mask = INT_MSK_RTC_ALARM_IT_MSK_MASK,
		.reg_offset = 0,
	},
	[TPS65911_IRQ_RTC_PERIOD] = {
		.mask = INT_MSK_RTC_PERIOD_IT_MSK_MASK,
		.reg_offset = 0,
	},

	/* INT_STS2 */
	[TPS65911_IRQ_GPIO0_R] = {
		.mask = INT_MSK2_GPIO0_R_IT_MSK_MASK,
		.reg_offset = 1,
	},
	[TPS65911_IRQ_GPIO0_F] = {
		.mask = INT_MSK2_GPIO0_F_IT_MSK_MASK,
		.reg_offset = 1,
	},
	[TPS65911_IRQ_GPIO1_R] = {
		.mask = INT_MSK2_GPIO1_R_IT_MSK_MASK,
		.reg_offset = 1,
	},
	[TPS65911_IRQ_GPIO1_F] = {
		.mask = INT_MSK2_GPIO1_F_IT_MSK_MASK,
		.reg_offset = 1,
	},
	[TPS65911_IRQ_GPIO2_R] = {
		.mask = INT_MSK2_GPIO2_R_IT_MSK_MASK,
		.reg_offset = 1,
	},
	[TPS65911_IRQ_GPIO2_F] = {
		.mask = INT_MSK2_GPIO2_F_IT_MSK_MASK,
		.reg_offset = 1,
	},
	[TPS65911_IRQ_GPIO3_R] = {
		.mask = INT_MSK2_GPIO3_R_IT_MSK_MASK,
		.reg_offset = 1,
	},
	[TPS65911_IRQ_GPIO3_F] = {
		.mask = INT_MSK2_GPIO3_F_IT_MSK_MASK,
		.reg_offset = 1,
	},

	/* INT_STS3 */
	[TPS65911_IRQ_GPIO4_R] = {
		.mask = INT_MSK3_GPIO4_R_IT_MSK_MASK,
		.reg_offset = 2,
	},
	[TPS65911_IRQ_GPIO4_F] = {
		.mask = INT_MSK3_GPIO4_F_IT_MSK_MASK,
		.reg_offset = 2,
	},
	[TPS65911_IRQ_GPIO5_R] = {
		.mask = INT_MSK3_GPIO5_R_IT_MSK_MASK,
		.reg_offset = 2,
	},
	[TPS65911_IRQ_GPIO5_F] = {
		.mask = INT_MSK3_GPIO5_F_IT_MSK_MASK,
		.reg_offset = 2,
	},
	[TPS65911_IRQ_WTCHDG] = {
		.mask = INT_MSK3_WTCHDG_IT_MSK_MASK,
		.reg_offset = 2,
	},
	[TPS65911_IRQ_VMBCH2_H] = {
		.mask = INT_MSK3_VMBCH2_H_IT_MSK_MASK,
		.reg_offset = 2,
	},
	[TPS65911_IRQ_VMBCH2_L] = {
		.mask = INT_MSK3_VMBCH2_L_IT_MSK_MASK,
		.reg_offset = 2,
	},
	[TPS65911_IRQ_PWRDN] = {
		.mask = INT_MSK3_PWRDN_IT_MSK_MASK,
		.reg_offset = 2,
	},
};

static const struct regmap_irq tps65910_irqs[] = {
	/* INT_STS */
	[TPS65910_IRQ_VBAT_VMBDCH] = {
		.mask = TPS65910_INT_MSK_VMBDCH_IT_MSK_MASK,
		.reg_offset = 0,
	},
	[TPS65910_IRQ_VBAT_VMHI] = {
		.mask = TPS65910_INT_MSK_VMBHI_IT_MSK_MASK,
		.reg_offset = 0,
	},
	[TPS65910_IRQ_PWRON] = {
		.mask = TPS65910_INT_MSK_PWRON_IT_MSK_MASK,
		.reg_offset = 0,
	},
	[TPS65910_IRQ_PWRON_LP] = {
		.mask = TPS65910_INT_MSK_PWRON_LP_IT_MSK_MASK,
		.reg_offset = 0,
	},
	[TPS65910_IRQ_PWRHOLD] = {
		.mask = TPS65910_INT_MSK_PWRHOLD_IT_MSK_MASK,
		.reg_offset = 0,
	},
	[TPS65910_IRQ_HOTDIE] = {
		.mask = TPS65910_INT_MSK_HOTDIE_IT_MSK_MASK,
		.reg_offset = 0,
	},
	[TPS65910_IRQ_RTC_ALARM] = {
		.mask = TPS65910_INT_MSK_RTC_ALARM_IT_MSK_MASK,
		.reg_offset = 0,
	},
	[TPS65910_IRQ_RTC_PERIOD] = {
		.mask = TPS65910_INT_MSK_RTC_PERIOD_IT_MSK_MASK,
		.reg_offset = 0,
	},

	/* INT_STS2 */
	[TPS65910_IRQ_GPIO_R] = {
		.mask = TPS65910_INT_MSK2_GPIO0_F_IT_MSK_MASK,
		.reg_offset = 1,
	},
	[TPS65910_IRQ_GPIO_F] = {
		.mask = TPS65910_INT_MSK2_GPIO0_R_IT_MSK_MASK,
		.reg_offset = 1,
	},
};

static struct regmap_irq_chip tps65911_irq_chip = {
	.name = "tps65910",
	.irqs = tps65911_irqs,
	.num_irqs = ARRAY_SIZE(tps65911_irqs),
	.num_regs = 3,
	.irq_reg_stride = 2,
	.status_base = TPS65910_INT_STS,
	.mask_base = TPS65910_INT_MSK,
	.ack_base = TPS65910_INT_MSK,
};

static struct regmap_irq_chip tps65910_irq_chip = {
	.name = "tps65910",
	.irqs = tps65910_irqs,
	.num_irqs = ARRAY_SIZE(tps65910_irqs),
	.num_regs = 2,
	.irq_reg_stride = 2,
	.status_base = TPS65910_INT_STS,
	.mask_base = TPS65910_INT_MSK,
	.ack_base = TPS65910_INT_MSK,
};

int tps65910_irq_init(struct tps65910 *tps65910, int irq,
		    struct tps65910_platform_data *pdata)
{
	int ret = 0;
	static struct regmap_irq_chip *tps6591x_irqs_chip;

	if (!irq) {
		dev_warn(tps65910->dev, "No interrupt support, no core IRQ\n");
		return -EINVAL;
	}

	if (!pdata) {
		dev_warn(tps65910->dev, "No interrupt support, no pdata\n");
		return -EINVAL;
	}


	switch (tps65910_chip_id(tps65910)) {
	case TPS65910:
		tps6591x_irqs_chip = &tps65910_irq_chip;
		break;
	case TPS65911:
		tps6591x_irqs_chip = &tps65911_irq_chip;
		break;
	}

	tps65910->chip_irq = irq;
	ret = regmap_add_irq_chip(tps65910->regmap, tps65910->chip_irq,
		IRQF_ONESHOT, pdata->irq_base,
		tps6591x_irqs_chip, &tps65910->irq_data);
	if (ret < 0) {
		dev_warn(tps65910->dev,
				"Failed to add irq_chip %d\n", ret);
		return ret;
	}
	return ret;
}

int tps65910_irq_exit(struct tps65910 *tps65910)
{
	if (tps65910->chip_irq > 0)
		regmap_del_irq_chip(tps65910->chip_irq, tps65910->irq_data);
	return 0;
}
