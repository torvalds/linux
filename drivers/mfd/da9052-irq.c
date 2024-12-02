// SPDX-License-Identifier: GPL-2.0-only
/*
 * DA9052 interrupt support
 *
 * Author: Fabio Estevam <fabio.estevam@freescale.com>
 * Based on arizona-irq.c, which is:
 *
 * Copyright 2012 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/slab.h>
#include <linux/module.h>

#include <linux/mfd/da9052/da9052.h>
#include <linux/mfd/da9052/reg.h>

#define DA9052_NUM_IRQ_REGS		4
#define DA9052_IRQ_MASK_POS_1		0x01
#define DA9052_IRQ_MASK_POS_2		0x02
#define DA9052_IRQ_MASK_POS_3		0x04
#define DA9052_IRQ_MASK_POS_4		0x08
#define DA9052_IRQ_MASK_POS_5		0x10
#define DA9052_IRQ_MASK_POS_6		0x20
#define DA9052_IRQ_MASK_POS_7		0x40
#define DA9052_IRQ_MASK_POS_8		0x80

static const struct regmap_irq da9052_irqs[] = {
	[DA9052_IRQ_DCIN] = {
		.reg_offset = 0,
		.mask = DA9052_IRQ_MASK_POS_1,
	},
	[DA9052_IRQ_VBUS] = {
		.reg_offset = 0,
		.mask = DA9052_IRQ_MASK_POS_2,
	},
	[DA9052_IRQ_DCINREM] = {
		.reg_offset = 0,
		.mask = DA9052_IRQ_MASK_POS_3,
	},
	[DA9052_IRQ_VBUSREM] = {
		.reg_offset = 0,
		.mask = DA9052_IRQ_MASK_POS_4,
	},
	[DA9052_IRQ_VDDLOW] = {
		.reg_offset = 0,
		.mask = DA9052_IRQ_MASK_POS_5,
	},
	[DA9052_IRQ_ALARM] = {
		.reg_offset = 0,
		.mask = DA9052_IRQ_MASK_POS_6,
	},
	[DA9052_IRQ_SEQRDY] = {
		.reg_offset = 0,
		.mask = DA9052_IRQ_MASK_POS_7,
	},
	[DA9052_IRQ_COMP1V2] = {
		.reg_offset = 0,
		.mask = DA9052_IRQ_MASK_POS_8,
	},
	[DA9052_IRQ_NONKEY] = {
		.reg_offset = 1,
		.mask = DA9052_IRQ_MASK_POS_1,
	},
	[DA9052_IRQ_IDFLOAT] = {
		.reg_offset = 1,
		.mask = DA9052_IRQ_MASK_POS_2,
	},
	[DA9052_IRQ_IDGND] = {
		.reg_offset = 1,
		.mask = DA9052_IRQ_MASK_POS_3,
	},
	[DA9052_IRQ_CHGEND] = {
		.reg_offset = 1,
		.mask = DA9052_IRQ_MASK_POS_4,
	},
	[DA9052_IRQ_TBAT] = {
		.reg_offset = 1,
		.mask = DA9052_IRQ_MASK_POS_5,
	},
	[DA9052_IRQ_ADC_EOM] = {
		.reg_offset = 1,
		.mask = DA9052_IRQ_MASK_POS_6,
	},
	[DA9052_IRQ_PENDOWN] = {
		.reg_offset = 1,
		.mask = DA9052_IRQ_MASK_POS_7,
	},
	[DA9052_IRQ_TSIREADY] = {
		.reg_offset = 1,
		.mask = DA9052_IRQ_MASK_POS_8,
	},
	[DA9052_IRQ_GPI0] = {
		.reg_offset = 2,
		.mask = DA9052_IRQ_MASK_POS_1,
	},
	[DA9052_IRQ_GPI1] = {
		.reg_offset = 2,
		.mask = DA9052_IRQ_MASK_POS_2,
	},
	[DA9052_IRQ_GPI2] = {
		.reg_offset = 2,
		.mask = DA9052_IRQ_MASK_POS_3,
	},
	[DA9052_IRQ_GPI3] = {
		.reg_offset = 2,
		.mask = DA9052_IRQ_MASK_POS_4,
	},
	[DA9052_IRQ_GPI4] = {
		.reg_offset = 2,
		.mask = DA9052_IRQ_MASK_POS_5,
	},
	[DA9052_IRQ_GPI5] = {
		.reg_offset = 2,
		.mask = DA9052_IRQ_MASK_POS_6,
	},
	[DA9052_IRQ_GPI6] = {
		.reg_offset = 2,
		.mask = DA9052_IRQ_MASK_POS_7,
	},
	[DA9052_IRQ_GPI7] = {
		.reg_offset = 2,
		.mask = DA9052_IRQ_MASK_POS_8,
	},
	[DA9052_IRQ_GPI8] = {
		.reg_offset = 3,
		.mask = DA9052_IRQ_MASK_POS_1,
	},
	[DA9052_IRQ_GPI9] = {
		.reg_offset = 3,
		.mask = DA9052_IRQ_MASK_POS_2,
	},
	[DA9052_IRQ_GPI10] = {
		.reg_offset = 3,
		.mask = DA9052_IRQ_MASK_POS_3,
	},
	[DA9052_IRQ_GPI11] = {
		.reg_offset = 3,
		.mask = DA9052_IRQ_MASK_POS_4,
	},
	[DA9052_IRQ_GPI12] = {
		.reg_offset = 3,
		.mask = DA9052_IRQ_MASK_POS_5,
	},
	[DA9052_IRQ_GPI13] = {
		.reg_offset = 3,
		.mask = DA9052_IRQ_MASK_POS_6,
	},
	[DA9052_IRQ_GPI14] = {
		.reg_offset = 3,
		.mask = DA9052_IRQ_MASK_POS_7,
	},
	[DA9052_IRQ_GPI15] = {
		.reg_offset = 3,
		.mask = DA9052_IRQ_MASK_POS_8,
	},
};

static const struct regmap_irq_chip da9052_regmap_irq_chip = {
	.name = "da9052_irq",
	.status_base = DA9052_EVENT_A_REG,
	.mask_base = DA9052_IRQ_MASK_A_REG,
	.ack_base = DA9052_EVENT_A_REG,
	.num_regs = DA9052_NUM_IRQ_REGS,
	.irqs = da9052_irqs,
	.num_irqs = ARRAY_SIZE(da9052_irqs),
};

static int da9052_map_irq(struct da9052 *da9052, int irq)
{
	return regmap_irq_get_virq(da9052->irq_data, irq);
}

int da9052_enable_irq(struct da9052 *da9052, int irq)
{
	irq = da9052_map_irq(da9052, irq);
	if (irq < 0)
		return irq;

	enable_irq(irq);

	return 0;
}
EXPORT_SYMBOL_GPL(da9052_enable_irq);

int da9052_disable_irq(struct da9052 *da9052, int irq)
{
	irq = da9052_map_irq(da9052, irq);
	if (irq < 0)
		return irq;

	disable_irq(irq);

	return 0;
}
EXPORT_SYMBOL_GPL(da9052_disable_irq);

int da9052_disable_irq_nosync(struct da9052 *da9052, int irq)
{
	irq = da9052_map_irq(da9052, irq);
	if (irq < 0)
		return irq;

	disable_irq_nosync(irq);

	return 0;
}
EXPORT_SYMBOL_GPL(da9052_disable_irq_nosync);

int da9052_request_irq(struct da9052 *da9052, int irq, char *name,
			   irq_handler_t handler, void *data)
{
	irq = da9052_map_irq(da9052, irq);
	if (irq < 0)
		return irq;

	return request_threaded_irq(irq, NULL, handler,
				     IRQF_TRIGGER_LOW | IRQF_ONESHOT,
				     name, data);
}
EXPORT_SYMBOL_GPL(da9052_request_irq);

void da9052_free_irq(struct da9052 *da9052, int irq, void *data)
{
	irq = da9052_map_irq(da9052, irq);
	if (irq < 0)
		return;

	free_irq(irq, data);
}
EXPORT_SYMBOL_GPL(da9052_free_irq);

static irqreturn_t da9052_auxadc_irq(int irq, void *irq_data)
{
	struct da9052 *da9052 = irq_data;

	complete(&da9052->done);

	return IRQ_HANDLED;
}

int da9052_irq_init(struct da9052 *da9052)
{
	int ret;

	ret = regmap_add_irq_chip(da9052->regmap, da9052->chip_irq,
				  IRQF_TRIGGER_LOW | IRQF_ONESHOT,
				  -1, &da9052_regmap_irq_chip,
				  &da9052->irq_data);
	if (ret < 0) {
		dev_err(da9052->dev, "regmap_add_irq_chip failed: %d\n", ret);
		goto regmap_err;
	}

	enable_irq_wake(da9052->chip_irq);

	ret = da9052_request_irq(da9052, DA9052_IRQ_ADC_EOM, "adc-irq",
			    da9052_auxadc_irq, da9052);

	if (ret != 0) {
		dev_err(da9052->dev, "DA9052_IRQ_ADC_EOM failed: %d\n", ret);
		goto request_irq_err;
	}

	return 0;

request_irq_err:
	regmap_del_irq_chip(da9052->chip_irq, da9052->irq_data);
regmap_err:
	return ret;

}

int da9052_irq_exit(struct da9052 *da9052)
{
	da9052_free_irq(da9052, DA9052_IRQ_ADC_EOM, da9052);
	regmap_del_irq_chip(da9052->chip_irq, da9052->irq_data);

	return 0;
}
