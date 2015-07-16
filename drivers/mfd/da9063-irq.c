/* da9063-irq.c: Interrupts support for Dialog DA9063
 *
 * Copyright 2012 Dialog Semiconductor Ltd.
 * Copyright 2013 Philipp Zabel, Pengutronix
 *
 * Author: Michal Hajduk <michal.hajduk@diasemi.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/mfd/core.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/mfd/da9063/core.h>
#include <linux/mfd/da9063/pdata.h>

#define	DA9063_REG_EVENT_A_OFFSET	0
#define	DA9063_REG_EVENT_B_OFFSET	1
#define	DA9063_REG_EVENT_C_OFFSET	2
#define	DA9063_REG_EVENT_D_OFFSET	3
#define EVENTS_BUF_LEN			4

static const u8 mask_events_buf[] = { [0 ... (EVENTS_BUF_LEN - 1)] = ~0 };

struct da9063_irq_data {
	u16 reg;
	u8 mask;
};

static const struct regmap_irq da9063_irqs[] = {
	/* DA9063 event A register */
	[DA9063_IRQ_ONKEY] = {
		.reg_offset = DA9063_REG_EVENT_A_OFFSET,
		.mask = DA9063_M_ONKEY,
	},
	[DA9063_IRQ_ALARM] = {
		.reg_offset = DA9063_REG_EVENT_A_OFFSET,
		.mask = DA9063_M_ALARM,
	},
	[DA9063_IRQ_TICK] = {
		.reg_offset = DA9063_REG_EVENT_A_OFFSET,
		.mask = DA9063_M_TICK,
	},
	[DA9063_IRQ_ADC_RDY] = {
		.reg_offset = DA9063_REG_EVENT_A_OFFSET,
		.mask = DA9063_M_ADC_RDY,
	},
	[DA9063_IRQ_SEQ_RDY] = {
		.reg_offset = DA9063_REG_EVENT_A_OFFSET,
		.mask = DA9063_M_SEQ_RDY,
	},
	/* DA9063 event B register */
	[DA9063_IRQ_WAKE] = {
		.reg_offset = DA9063_REG_EVENT_B_OFFSET,
		.mask = DA9063_M_WAKE,
	},
	[DA9063_IRQ_TEMP] = {
		.reg_offset = DA9063_REG_EVENT_B_OFFSET,
		.mask = DA9063_M_TEMP,
	},
	[DA9063_IRQ_COMP_1V2] = {
		.reg_offset = DA9063_REG_EVENT_B_OFFSET,
		.mask = DA9063_M_COMP_1V2,
	},
	[DA9063_IRQ_LDO_LIM] = {
		.reg_offset = DA9063_REG_EVENT_B_OFFSET,
		.mask = DA9063_M_LDO_LIM,
	},
	[DA9063_IRQ_REG_UVOV] = {
		.reg_offset = DA9063_REG_EVENT_B_OFFSET,
		.mask = DA9063_M_UVOV,
	},
	[DA9063_IRQ_VDD_MON] = {
		.reg_offset = DA9063_REG_EVENT_B_OFFSET,
		.mask = DA9063_M_VDD_MON,
	},
	[DA9063_IRQ_WARN] = {
		.reg_offset = DA9063_REG_EVENT_B_OFFSET,
		.mask = DA9063_M_VDD_WARN,
	},
	/* DA9063 event C register */
	[DA9063_IRQ_GPI0] = {
		.reg_offset = DA9063_REG_EVENT_C_OFFSET,
		.mask = DA9063_M_GPI0,
	},
	[DA9063_IRQ_GPI1] = {
		.reg_offset = DA9063_REG_EVENT_C_OFFSET,
		.mask = DA9063_M_GPI1,
	},
	[DA9063_IRQ_GPI2] = {
		.reg_offset = DA9063_REG_EVENT_C_OFFSET,
		.mask = DA9063_M_GPI2,
	},
	[DA9063_IRQ_GPI3] = {
		.reg_offset = DA9063_REG_EVENT_C_OFFSET,
		.mask = DA9063_M_GPI3,
	},
	[DA9063_IRQ_GPI4] = {
		.reg_offset = DA9063_REG_EVENT_C_OFFSET,
		.mask = DA9063_M_GPI4,
	},
	[DA9063_IRQ_GPI5] = {
		.reg_offset = DA9063_REG_EVENT_C_OFFSET,
		.mask = DA9063_M_GPI5,
	},
	[DA9063_IRQ_GPI6] = {
		.reg_offset = DA9063_REG_EVENT_C_OFFSET,
		.mask = DA9063_M_GPI6,
	},
	[DA9063_IRQ_GPI7] = {
		.reg_offset = DA9063_REG_EVENT_C_OFFSET,
		.mask = DA9063_M_GPI7,
	},
	/* DA9063 event D register */
	[DA9063_IRQ_GPI8] = {
		.reg_offset = DA9063_REG_EVENT_D_OFFSET,
		.mask = DA9063_M_GPI8,
	},
	[DA9063_IRQ_GPI9] = {
		.reg_offset = DA9063_REG_EVENT_D_OFFSET,
		.mask = DA9063_M_GPI9,
	},
	[DA9063_IRQ_GPI10] = {
		.reg_offset = DA9063_REG_EVENT_D_OFFSET,
		.mask = DA9063_M_GPI10,
	},
	[DA9063_IRQ_GPI11] = {
		.reg_offset = DA9063_REG_EVENT_D_OFFSET,
		.mask = DA9063_M_GPI11,
	},
	[DA9063_IRQ_GPI12] = {
		.reg_offset = DA9063_REG_EVENT_D_OFFSET,
		.mask = DA9063_M_GPI12,
	},
	[DA9063_IRQ_GPI13] = {
		.reg_offset = DA9063_REG_EVENT_D_OFFSET,
		.mask = DA9063_M_GPI13,
	},
	[DA9063_IRQ_GPI14] = {
		.reg_offset = DA9063_REG_EVENT_D_OFFSET,
		.mask = DA9063_M_GPI14,
	},
	[DA9063_IRQ_GPI15] = {
		.reg_offset = DA9063_REG_EVENT_D_OFFSET,
		.mask = DA9063_M_GPI15,
	},
};

static const struct regmap_irq_chip da9063_irq_chip = {
	.name = "da9063-irq",
	.irqs = da9063_irqs,
	.num_irqs = DA9063_NUM_IRQ,

	.num_regs = 4,
	.status_base = DA9063_REG_EVENT_A,
	.mask_base = DA9063_REG_IRQ_MASK_A,
	.ack_base = DA9063_REG_EVENT_A,
	.init_ack_masked = true,
};

int da9063_irq_init(struct da9063 *da9063)
{
	int ret;

	if (!da9063->chip_irq) {
		dev_err(da9063->dev, "No IRQ configured\n");
		return -EINVAL;
	}

	ret = regmap_add_irq_chip(da9063->regmap, da9063->chip_irq,
			IRQF_TRIGGER_LOW | IRQF_ONESHOT | IRQF_SHARED,
			da9063->irq_base, &da9063_irq_chip,
			&da9063->regmap_irq);
	if (ret) {
		dev_err(da9063->dev, "Failed to reguest IRQ %d: %d\n",
				da9063->chip_irq, ret);
		return ret;
	}

	return 0;
}

void da9063_irq_exit(struct da9063 *da9063)
{
	regmap_del_irq_chip(da9063->chip_irq, da9063->regmap_irq);
}
