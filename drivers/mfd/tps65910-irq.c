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
#include <linux/gpio.h>
#include <linux/mfd/tps65910.h>

static inline int irq_to_tps65910_irq(struct tps65910 *tps65910,
							int irq)
{
	return (irq - tps65910->irq_base);
}

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
	u32 irq_sts;
	u32 irq_mask;
	u8 reg;
	int i;

	tps65910->read(tps65910, TPS65910_INT_STS, 1, &reg);
	irq_sts = reg;
	tps65910->read(tps65910, TPS65910_INT_STS2, 1, &reg);
	irq_sts |= reg << 8;
	switch (tps65910_chip_id(tps65910)) {
	case TPS65911:
		tps65910->read(tps65910, TPS65910_INT_STS3, 1, &reg);
		irq_sts |= reg << 16;
	}

	tps65910->read(tps65910, TPS65910_INT_MSK, 1, &reg);
	irq_mask = reg;
	tps65910->read(tps65910, TPS65910_INT_MSK2, 1, &reg);
	irq_mask |= reg << 8;
	switch (tps65910_chip_id(tps65910)) {
	case TPS65911:
		tps65910->read(tps65910, TPS65910_INT_MSK3, 1, &reg);
		irq_mask |= reg << 16;
	}

	irq_sts &= ~irq_mask;

	if (!irq_sts)
		return IRQ_NONE;

	for (i = 0; i < tps65910->irq_num; i++) {

		if (!(irq_sts & (1 << i)))
			continue;

		handle_nested_irq(tps65910->irq_base + i);
	}

	/* Write the STS register back to clear IRQs we handled */
	reg = irq_sts & 0xFF;
	irq_sts >>= 8;
	tps65910->write(tps65910, TPS65910_INT_STS, 1, &reg);
	reg = irq_sts & 0xFF;
	tps65910->write(tps65910, TPS65910_INT_STS2, 1, &reg);
	switch (tps65910_chip_id(tps65910)) {
	case TPS65911:
		reg = irq_sts >> 8;
		tps65910->write(tps65910, TPS65910_INT_STS3, 1, &reg);
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
	u8 reg;

	tps65910->read(tps65910, TPS65910_INT_MSK, 1, &reg);
	reg_mask = reg;
	tps65910->read(tps65910, TPS65910_INT_MSK2, 1, &reg);
	reg_mask |= reg << 8;
	switch (tps65910_chip_id(tps65910)) {
	case TPS65911:
		tps65910->read(tps65910, TPS65910_INT_MSK3, 1, &reg);
		reg_mask |= reg << 16;
	}

	if (tps65910->irq_mask != reg_mask) {
		reg = tps65910->irq_mask & 0xFF;
		tps65910->write(tps65910, TPS65910_INT_MSK, 1, &reg);
		reg = tps65910->irq_mask >> 8 & 0xFF;
		tps65910->write(tps65910, TPS65910_INT_MSK2, 1, &reg);
		switch (tps65910_chip_id(tps65910)) {
		case TPS65911:
			reg = tps65910->irq_mask >> 16;
			tps65910->write(tps65910, TPS65910_INT_MSK3, 1, &reg);
		}
	}
	mutex_unlock(&tps65910->irq_lock);
}

static void tps65910_irq_enable(struct irq_data *data)
{
	struct tps65910 *tps65910 = irq_data_get_irq_chip_data(data);

	tps65910->irq_mask &= ~( 1 << irq_to_tps65910_irq(tps65910, data->irq));
}

static void tps65910_irq_disable(struct irq_data *data)
{
	struct tps65910 *tps65910 = irq_data_get_irq_chip_data(data);

	tps65910->irq_mask |= ( 1 << irq_to_tps65910_irq(tps65910, data->irq));
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

int tps65910_irq_init(struct tps65910 *tps65910, int irq,
		    struct tps65910_platform_data *pdata)
{
	int ret, cur_irq;
	int flags = IRQF_ONESHOT;

	if (!irq) {
		dev_warn(tps65910->dev, "No interrupt support, no core IRQ\n");
		return -EINVAL;
	}

	if (!pdata || !pdata->irq_base) {
		dev_warn(tps65910->dev, "No interrupt support, no IRQ base\n");
		return -EINVAL;
	}

	tps65910->irq_mask = 0xFFFFFF;

	mutex_init(&tps65910->irq_lock);
	tps65910->chip_irq = irq;
	tps65910->irq_base = pdata->irq_base;

	switch (tps65910_chip_id(tps65910)) {
	case TPS65910:
		tps65910->irq_num = TPS65910_NUM_IRQ;
		break;
	case TPS65911:
		tps65910->irq_num = TPS65911_NUM_IRQ;
		break;
	}

	/* Register with genirq */
	for (cur_irq = tps65910->irq_base;
	     cur_irq < tps65910->irq_num + tps65910->irq_base;
	     cur_irq++) {
		irq_set_chip_data(cur_irq, tps65910);
		irq_set_chip_and_handler(cur_irq, &tps65910_irq_chip,
					 handle_edge_irq);
		irq_set_nested_thread(cur_irq, 1);

		/* ARM needs us to explicitly flag the IRQ as valid
		 * and will set them noprobe when we do so. */
#ifdef CONFIG_ARM
		set_irq_flags(cur_irq, IRQF_VALID);
#else
		irq_set_noprobe(cur_irq);
#endif
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
