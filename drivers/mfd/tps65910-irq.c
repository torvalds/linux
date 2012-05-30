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

/*
 * This is a threaded IRQ handler so can access I2C/SPI.  Since all
 * interrupts are clear on read the IRQ line will be reasserted and
 * the physical IRQ will be handled again if another interrupt is
 * asserted while we run - in the normal course of events this is a
 * rare occurrence so we save I2C/SPI reads.  We're also assuming that
 * it's rare to get lots of interrupts firing simultaneously so try to
 * minimise I/O.
 */
static irqreturn_t tps65910_irq(int irq, void *irq_data)
{
	struct tps65910 *tps65910 = irq_data;
	unsigned int reg;
	u32 irq_sts;
	u32 irq_mask;
	int i;

	tps65910_reg_read(tps65910, TPS65910_INT_STS, &reg);
	irq_sts = reg;
	tps65910_reg_read(tps65910, TPS65910_INT_STS2, &reg);
	irq_sts |= reg << 8;
	switch (tps65910_chip_id(tps65910)) {
	case TPS65911:
		tps65910_reg_read(tps65910, TPS65910_INT_STS3, &reg);
		irq_sts |= reg << 16;
	}

	tps65910_reg_read(tps65910, TPS65910_INT_MSK, &reg);
	irq_mask = reg;
	tps65910_reg_read(tps65910, TPS65910_INT_MSK2, &reg);
	irq_mask |= reg << 8;
	switch (tps65910_chip_id(tps65910)) {
	case TPS65911:
		tps65910_reg_read(tps65910, TPS65910_INT_MSK3, &reg);
		irq_mask |= reg << 16;
	}

	irq_sts &= ~irq_mask;

	if (!irq_sts)
		return IRQ_NONE;

	for (i = 0; i < tps65910->irq_num; i++) {

		if (!(irq_sts & (1 << i)))
			continue;

		handle_nested_irq(irq_find_mapping(tps65910->domain, i));
	}

	/* Write the STS register back to clear IRQs we handled */
	reg = irq_sts & 0xFF;
	irq_sts >>= 8;
	tps65910_reg_write(tps65910, TPS65910_INT_STS, reg);
	reg = irq_sts & 0xFF;
	tps65910_reg_write(tps65910, TPS65910_INT_STS2, reg);
	switch (tps65910_chip_id(tps65910)) {
	case TPS65911:
		reg = irq_sts >> 8;
		tps65910_reg_write(tps65910, TPS65910_INT_STS3, reg);
	}

	return IRQ_HANDLED;
}

static void tps65910_irq_lock(struct irq_data *data)
{
	struct tps65910 *tps65910 = irq_data_get_irq_chip_data(data);

	mutex_lock(&tps65910->irq_lock);
}

static void tps65910_irq_sync_unlock(struct irq_data *data)
{
	struct tps65910 *tps65910 = irq_data_get_irq_chip_data(data);
	u32 reg_mask;
	unsigned int reg;

	tps65910_reg_read(tps65910, TPS65910_INT_MSK, &reg);
	reg_mask = reg;
	tps65910_reg_read(tps65910, TPS65910_INT_MSK2, &reg);
	reg_mask |= reg << 8;
	switch (tps65910_chip_id(tps65910)) {
	case TPS65911:
		tps65910_reg_read(tps65910, TPS65910_INT_MSK3, &reg);
		reg_mask |= reg << 16;
	}

	if (tps65910->irq_mask != reg_mask) {
		reg = tps65910->irq_mask & 0xFF;
		tps65910_reg_write(tps65910, TPS65910_INT_MSK, reg);
		reg = tps65910->irq_mask >> 8 & 0xFF;
		tps65910_reg_write(tps65910, TPS65910_INT_MSK2, reg);
		switch (tps65910_chip_id(tps65910)) {
		case TPS65911:
			reg = tps65910->irq_mask >> 16;
			tps65910_reg_write(tps65910, TPS65910_INT_MSK3, reg);
		}
	}
	mutex_unlock(&tps65910->irq_lock);
}

static void tps65910_irq_enable(struct irq_data *data)
{
	struct tps65910 *tps65910 = irq_data_get_irq_chip_data(data);

	tps65910->irq_mask &= ~(1 << data->hwirq);
}

static void tps65910_irq_disable(struct irq_data *data)
{
	struct tps65910 *tps65910 = irq_data_get_irq_chip_data(data);

	tps65910->irq_mask |= (1 << data->hwirq);
}

#ifdef CONFIG_PM_SLEEP
static int tps65910_irq_set_wake(struct irq_data *data, unsigned int enable)
{
	struct tps65910 *tps65910 = irq_data_get_irq_chip_data(data);
	return irq_set_irq_wake(tps65910->chip_irq, enable);
}
#else
#define tps65910_irq_set_wake NULL
#endif

static struct irq_chip tps65910_irq_chip = {
	.name = "tps65910",
	.irq_bus_lock = tps65910_irq_lock,
	.irq_bus_sync_unlock = tps65910_irq_sync_unlock,
	.irq_disable = tps65910_irq_disable,
	.irq_enable = tps65910_irq_enable,
	.irq_set_wake = tps65910_irq_set_wake,
};

static int tps65910_irq_map(struct irq_domain *h, unsigned int virq,
				irq_hw_number_t hw)
{
	struct tps65910 *tps65910 = h->host_data;

	irq_set_chip_data(virq, tps65910);
	irq_set_chip_and_handler(virq, &tps65910_irq_chip, handle_edge_irq);
	irq_set_nested_thread(virq, 1);

	/* ARM needs us to explicitly flag the IRQ as valid
	 * and will set them noprobe when we do so. */
#ifdef CONFIG_ARM
	set_irq_flags(virq, IRQF_VALID);
#else
	irq_set_noprobe(virq);
#endif

	return 0;
}

static struct irq_domain_ops tps65910_domain_ops = {
	.map	= tps65910_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

int tps65910_irq_init(struct tps65910 *tps65910, int irq,
		    struct tps65910_platform_data *pdata)
{
	int ret;
	int flags = IRQF_ONESHOT;

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
		tps65910->irq_num = TPS65910_NUM_IRQ;
		break;
	case TPS65911:
		tps65910->irq_num = TPS65911_NUM_IRQ;
		break;
	}

	if (pdata->irq_base > 0) {
		pdata->irq_base = irq_alloc_descs(pdata->irq_base, 0,
					tps65910->irq_num, -1);
		if (pdata->irq_base < 0) {
			dev_warn(tps65910->dev, "Failed to alloc IRQs: %d\n",
					pdata->irq_base);
			return pdata->irq_base;
		}
	}

	tps65910->irq_mask = 0xFFFFFF;

	mutex_init(&tps65910->irq_lock);
	tps65910->chip_irq = irq;
	tps65910->irq_base = pdata->irq_base;

	if (pdata->irq_base > 0)
		tps65910->domain = irq_domain_add_legacy(tps65910->dev->of_node,
					tps65910->irq_num,
					pdata->irq_base,
					0,
					&tps65910_domain_ops, tps65910);
	else
		tps65910->domain = irq_domain_add_linear(tps65910->dev->of_node,
					tps65910->irq_num,
					&tps65910_domain_ops, tps65910);

	if (!tps65910->domain) {
		dev_err(tps65910->dev, "Failed to create IRQ domain\n");
		return -ENOMEM;
	}

	ret = request_threaded_irq(irq, NULL, tps65910_irq, flags,
				   "tps65910", tps65910);

	irq_set_irq_type(irq, IRQ_TYPE_LEVEL_LOW);

	if (ret != 0)
		dev_err(tps65910->dev, "Failed to request IRQ: %d\n", ret);

	return ret;
}

int tps65910_irq_exit(struct tps65910 *tps65910)
{
	if (tps65910->chip_irq)
		free_irq(tps65910->chip_irq, tps65910);
	return 0;
}
