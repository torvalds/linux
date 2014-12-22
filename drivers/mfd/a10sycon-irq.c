/*
 *  Copyright (C) 2014 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * IRQ support for Altera MAX5 Arria10 System Control
 * Adapted from DA9052
 */

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/mfd/a10sycon.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define A10SYCON_NUM_IRQ_REGS		1

static struct regmap_irq a10sycon_irqs[] = {
	[0] = {
		.reg_offset = 0,
		.mask = BIT(A10SC_IRQ_DSW_O_SHIFT),
	},
	[1] = {
		.reg_offset = 0,
		.mask = BIT(A10SC_IRQ_DSW_1_SHIFT),
	},
	[2] = {
		.reg_offset = 0,
		.mask = BIT(A10SC_IRQ_DSW_2_SHIFT),
	},
	[3] = {
		.reg_offset = 0,
		.mask = BIT(A10SC_IRQ_DSW_3_SHIFT),
	},
	[4] = {
		.reg_offset = 0,
		.mask = BIT(A10SC_IRQ_PB_0_SHIFT),
	},
	[5] = {
		.reg_offset = 0,
		.mask = BIT(A10SC_IRQ_PB_1_SHIFT),
	},
	[6] = {
		.reg_offset = 0,
		.mask = BIT(A10SC_IRQ_PB_2_SHIFT),
	},
	[7] = {
		.reg_offset = 0,
		.mask = BIT(A10SC_IRQ_PB_3_SHIFT),
	},
};

static struct regmap_irq_chip a10sycon_regmap_irq_chip = {
	.name = "a10sycon_irq",
	.status_base = A10SYCON_PBDSW_IRQ_RD_REG,
	.mask_base   = A10SYCON_PBDSW_CLR_REG,
	.ack_base    = A10SYCON_PBDSW_CLR_REG,
	.num_regs    = A10SYCON_NUM_IRQ_REGS,
	.irqs        = a10sycon_irqs,
	.num_irqs    = ARRAY_SIZE(a10sycon_irqs),
};

int a10sycon_map_irq(struct a10sycon *a10sc, int irq)
{
	return regmap_irq_get_virq(a10sc->irq_data, irq);
}
EXPORT_SYMBOL_GPL(a10sycon_map_irq);

int a10sycon_enable_irq(struct a10sycon *a10sc, int irq)
{
	irq = a10sycon_map_irq(a10sc, irq);
	if (irq < 0)
		return irq;

	enable_irq(irq);

	return 0;
}
EXPORT_SYMBOL_GPL(a10sycon_enable_irq);

int a10sycon_disable_irq(struct a10sycon *a10sc, int irq)
{
	irq = a10sycon_map_irq(a10sc, irq);
	if (irq < 0)
		return irq;

	disable_irq(irq);

	return 0;
}
EXPORT_SYMBOL_GPL(a10sycon_disable_irq);

int a10sycon_disable_irq_nosync(struct a10sycon *a10sc, int irq)
{
	irq = a10sycon_map_irq(a10sc, irq);
	if (irq < 0)
		return irq;

	disable_irq_nosync(irq);

	return 0;
}
EXPORT_SYMBOL_GPL(a10sycon_disable_irq_nosync);

int a10sycon_request_irq(struct a10sycon *a10sc, int irq, const char *name,
			 irq_handler_t handler, void *data)
{
	irq = a10sycon_map_irq(a10sc, irq);
	if (irq < 0)
		return irq;

	return request_threaded_irq(irq, NULL, handler,
				     IRQF_TRIGGER_LOW | IRQF_ONESHOT,
				     name, data);
}
EXPORT_SYMBOL_GPL(a10sycon_request_irq);

void a10sycon_free_irq(struct a10sycon *a10sc, int irq, void *data)
{
	irq = a10sycon_map_irq(a10sc, irq);
	if (irq < 0)
		return;

	free_irq(irq, data);
}
EXPORT_SYMBOL_GPL(a10sycon_free_irq);

int a10sycon_irq_init(struct a10sycon *a10sc)
{
	int ret;

	ret = regmap_add_irq_chip(a10sc->regmap, a10sc->chip_irq,
				  IRQF_TRIGGER_LOW | IRQF_ONESHOT | IRQF_SHARED,
				  -1, &a10sycon_regmap_irq_chip,
				  &a10sc->irq_data);

	if (ret < 0) {
		dev_err(a10sc->dev, "regmap_add_irq_chip err: %d\n", ret);
		return ret;
	}

	return 0;
}

int a10sycon_irq_exit(struct a10sycon *a10sc)
{
	regmap_del_irq_chip(a10sc->chip_irq, a10sc->irq_data);

	return 0;
}
