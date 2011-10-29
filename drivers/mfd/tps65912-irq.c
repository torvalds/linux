/*
 * tps65912-irq.c  --  TI TPS6591x
 *
 * Copyright 2011 Texas Instruments Inc.
 *
 * Author: Margarita Olaya <magi@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 * This driver is based on wm8350 implementation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bug.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/mfd/tps65912.h>

static inline int irq_to_tps65912_irq(struct tps65912 *tps65912,
							int irq)
{
	return irq - tps65912->irq_base;
}

/*
 * This is a threaded IRQ handler so can access I2C/SPI.  Since the
 * IRQ handler explicitly clears the IRQ it handles the IRQ line
 * will be reasserted and the physical IRQ will be handled again if
 * another interrupt is asserted while we run - in the normal course
 * of events this is a rare occurrence so we save I2C/SPI reads. We're
 * also assuming that it's rare to get lots of interrupts firing
 * simultaneously so try to minimise I/O.
 */
static irqreturn_t tps65912_irq(int irq, void *irq_data)
{
	struct tps65912 *tps65912 = irq_data;
	u32 irq_sts;
	u32 irq_mask;
	u8 reg;
	int i;


	tps65912->read(tps65912, TPS65912_INT_STS, 1, &reg);
	irq_sts = reg;
	tps65912->read(tps65912, TPS65912_INT_STS2, 1, &reg);
	irq_sts |= reg << 8;
	tps65912->read(tps65912, TPS65912_INT_STS3, 1, &reg);
	irq_sts |= reg << 16;
	tps65912->read(tps65912, TPS65912_INT_STS4, 1, &reg);
	irq_sts |= reg << 24;

	tps65912->read(tps65912, TPS65912_INT_MSK, 1, &reg);
	irq_mask = reg;
	tps65912->read(tps65912, TPS65912_INT_MSK2, 1, &reg);
	irq_mask |= reg << 8;
	tps65912->read(tps65912, TPS65912_INT_MSK3, 1, &reg);
	irq_mask |= reg << 16;
	tps65912->read(tps65912, TPS65912_INT_MSK4, 1, &reg);
	irq_mask |= reg << 24;

	irq_sts &= ~irq_mask;
	if (!irq_sts)
		return IRQ_NONE;

	for (i = 0; i < tps65912->irq_num; i++) {
		if (!(irq_sts & (1 << i)))
			continue;

		handle_nested_irq(tps65912->irq_base + i);
	}

	/* Write the STS register back to clear IRQs we handled */
	reg = irq_sts & 0xFF;
	irq_sts >>= 8;
	if (reg)
		tps65912->write(tps65912, TPS65912_INT_STS, 1, &reg);
	reg = irq_sts & 0xFF;
	irq_sts >>= 8;
	if (reg)
		tps65912->write(tps65912, TPS65912_INT_STS2, 1, &reg);
	reg = irq_sts & 0xFF;
	irq_sts >>= 8;
	if (reg)
		tps65912->write(tps65912, TPS65912_INT_STS3, 1, &reg);
	reg = irq_sts & 0xFF;
	if (reg)
		tps65912->write(tps65912, TPS65912_INT_STS4, 1, &reg);

	return IRQ_HANDLED;
}

static void tps65912_irq_lock(struct irq_data *data)
{
	struct tps65912 *tps65912 = irq_data_get_irq_chip_data(data);

	mutex_lock(&tps65912->irq_lock);
}

static void tps65912_irq_sync_unlock(struct irq_data *data)
{
	struct tps65912 *tps65912 = irq_data_get_irq_chip_data(data);
	u32 reg_mask;
	u8 reg;

	tps65912->read(tps65912, TPS65912_INT_MSK, 1, &reg);
	reg_mask = reg;
	tps65912->read(tps65912, TPS65912_INT_MSK2, 1, &reg);
	reg_mask |= reg << 8;
	tps65912->read(tps65912, TPS65912_INT_MSK3, 1, &reg);
	reg_mask |= reg << 16;
	tps65912->read(tps65912, TPS65912_INT_MSK4, 1, &reg);
	reg_mask |= reg << 24;

	if (tps65912->irq_mask != reg_mask) {
		reg = tps65912->irq_mask & 0xFF;
		tps65912->write(tps65912, TPS65912_INT_MSK, 1, &reg);
		reg = tps65912->irq_mask >> 8 & 0xFF;
		tps65912->write(tps65912, TPS65912_INT_MSK2, 1, &reg);
		reg = tps65912->irq_mask >> 16 & 0xFF;
		tps65912->write(tps65912, TPS65912_INT_MSK3, 1, &reg);
		reg = tps65912->irq_mask >> 24 & 0xFF;
		tps65912->write(tps65912, TPS65912_INT_MSK4, 1, &reg);
	}

	mutex_unlock(&tps65912->irq_lock);
}

static void tps65912_irq_enable(struct irq_data *data)
{
	struct tps65912 *tps65912 = irq_data_get_irq_chip_data(data);

	tps65912->irq_mask &= ~(1 << irq_to_tps65912_irq(tps65912, data->irq));
}

static void tps65912_irq_disable(struct irq_data *data)
{
	struct tps65912 *tps65912 = irq_data_get_irq_chip_data(data);

	tps65912->irq_mask |= (1 << irq_to_tps65912_irq(tps65912, data->irq));
}

static struct irq_chip tps65912_irq_chip = {
	.name = "tps65912",
	.irq_bus_lock = tps65912_irq_lock,
	.irq_bus_sync_unlock = tps65912_irq_sync_unlock,
	.irq_disable = tps65912_irq_disable,
	.irq_enable = tps65912_irq_enable,
};

int tps65912_irq_init(struct tps65912 *tps65912, int irq,
			    struct tps65912_platform_data *pdata)
{
	int ret, cur_irq;
	int flags = IRQF_ONESHOT;
	u8 reg;

	if (!irq) {
		dev_warn(tps65912->dev, "No interrupt support, no core IRQ\n");
		return 0;
	}

	if (!pdata || !pdata->irq_base) {
		dev_warn(tps65912->dev, "No interrupt support, no IRQ base\n");
		return 0;
	}

	/* Clear unattended interrupts */
	tps65912->read(tps65912, TPS65912_INT_STS, 1, &reg);
	tps65912->write(tps65912, TPS65912_INT_STS, 1, &reg);
	tps65912->read(tps65912, TPS65912_INT_STS2, 1, &reg);
	tps65912->write(tps65912, TPS65912_INT_STS2, 1, &reg);
	tps65912->read(tps65912, TPS65912_INT_STS3, 1, &reg);
	tps65912->write(tps65912, TPS65912_INT_STS3, 1, &reg);
	tps65912->read(tps65912, TPS65912_INT_STS4, 1, &reg);
	tps65912->write(tps65912, TPS65912_INT_STS4, 1, &reg);

	/* Mask top level interrupts */
	tps65912->irq_mask = 0xFFFFFFFF;

	mutex_init(&tps65912->irq_lock);
	tps65912->chip_irq = irq;
	tps65912->irq_base = pdata->irq_base;

	tps65912->irq_num = TPS65912_NUM_IRQ;

	/* Register with genirq */
	for (cur_irq = tps65912->irq_base;
	     cur_irq < tps65912->irq_num + tps65912->irq_base;
	     cur_irq++) {
		irq_set_chip_data(cur_irq, tps65912);
		irq_set_chip_and_handler(cur_irq, &tps65912_irq_chip,
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

	ret = request_threaded_irq(irq, NULL, tps65912_irq, flags,
				   "tps65912", tps65912);

	irq_set_irq_type(irq, IRQ_TYPE_LEVEL_LOW);
	if (ret != 0)
		dev_err(tps65912->dev, "Failed to request IRQ: %d\n", ret);

	return ret;
}

int tps65912_irq_exit(struct tps65912 *tps65912)
{
	free_irq(tps65912->chip_irq, tps65912);
	return 0;
}
