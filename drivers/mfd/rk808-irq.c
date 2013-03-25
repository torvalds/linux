/*
 * rk808-irq.c 
 *
 * Author: zhangqing <zhangqing@rock-chips.com>
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
#include <linux/mfd/rk808.h>
#include <linux/wakelock.h>
#include <linux/kthread.h>

static inline int irq_to_rk808_irq(struct rk808 *rk808,
							int irq)
{
	return (irq - rk808->irq_base);
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
static irqreturn_t rk808_irq(int irq, void *irq_data)
{
	struct rk808 *rk808 = irq_data;
	u32 irq_sts;
	u32 irq_mask;
	u8 reg;
	int i;
	//printk(" rk808 irq %d \n",irq);	
	wake_lock(&rk808->irq_wake);	
	rk808_i2c_read(rk808, RK808_INT_STS_REG1, 1, &reg);
	irq_sts = reg;
	rk808_i2c_read(rk808, RK808_INT_STS_REG2, 1, &reg);
	irq_sts |= reg << 8;
	
	rk808_i2c_read(rk808, RK808_INT_STS_MSK_REG1, 1, &reg);
	irq_mask = reg;
	rk808_i2c_read(rk808, RK808_INT_STS_MSK_REG2, 1, &reg);
	irq_mask |= reg << 8;

	irq_sts &= ~irq_mask;

	if (!irq_sts)
	{
		wake_unlock(&rk808->irq_wake);
		return IRQ_NONE;
	}

	for (i = 0; i < rk808->irq_num; i++) {

		if (!(irq_sts & (1 << i)))
			continue;

		handle_nested_irq(rk808->irq_base + i);
	}

	/* Write the STS register back to clear IRQs we handled */
	reg = irq_sts & 0xFF;
	irq_sts >>= 8;
	rk808_i2c_write(rk808, RK808_INT_STS_REG1, 1, reg);
	reg = irq_sts & 0xFF;
	rk808_i2c_write(rk808, RK808_INT_STS_REG2, 1, reg);
	wake_unlock(&rk808->irq_wake);
	return IRQ_HANDLED;
}

static void rk808_irq_lock(struct irq_data *data)
{
	struct rk808 *rk808 = irq_data_get_irq_chip_data(data);

	mutex_lock(&rk808->irq_lock);
}

static void rk808_irq_sync_unlock(struct irq_data *data)
{
	struct rk808 *rk808 = irq_data_get_irq_chip_data(data);
	u32 reg_mask;
	u8 reg;

	rk808_i2c_read(rk808, RK808_INT_STS_MSK_REG1, 1, &reg);
	reg_mask = reg;
	rk808_i2c_read(rk808, RK808_INT_STS_MSK_REG2, 1, &reg);
	reg_mask |= reg << 8;

	if (rk808->irq_mask != reg_mask) {
		reg = rk808->irq_mask & 0xff;
//		rk808_i2c_write(rk808, RK808_INT_STS_MSK_REG1, 1, reg);
		reg = rk808->irq_mask >> 8 & 0xff;
//		rk808_i2c_write(rk808, RK808_INT_STS_MSK_REG2, 1, reg);
	}
	mutex_unlock(&rk808->irq_lock);
}

static void rk808_irq_enable(struct irq_data *data)
{
	struct rk808 *rk808 = irq_data_get_irq_chip_data(data);

	rk808->irq_mask &= ~( 1 << irq_to_rk808_irq(rk808, data->irq));
}

static void rk808_irq_disable(struct irq_data *data)
{
	struct rk808 *rk808 = irq_data_get_irq_chip_data(data);

	rk808->irq_mask |= ( 1 << irq_to_rk808_irq(rk808, data->irq));
}

#ifdef CONFIG_PM_SLEEP
static int rk808_irq_set_wake(struct irq_data *data, unsigned int enable)
{
	struct rk808 *rk808 = irq_data_get_irq_chip_data(data);
	return irq_set_irq_wake(rk808->chip_irq, enable);
}
#else
#define rk808_irq_set_wake NULL
#endif

static struct irq_chip rk808_irq_chip = {
	.name = "rk808",
	.irq_bus_lock = rk808_irq_lock,
	.irq_bus_sync_unlock = rk808_irq_sync_unlock,
	.irq_disable = rk808_irq_disable,
	.irq_enable = rk808_irq_enable,
	.irq_set_wake = rk808_irq_set_wake,
};

int rk808_irq_init(struct rk808 *rk808, int irq,struct rk808_platform_data *pdata)
{
	int ret, cur_irq;
	int flags = IRQF_ONESHOT;
	u8 reg;

	printk("%s,line=%d\n", __func__,__LINE__);


	if (!irq) {
		dev_warn(rk808->dev, "No interrupt support, no core IRQ\n");
		return 0;
	}

	if (!pdata || !pdata->irq_base) {
		dev_warn(rk808->dev, "No interrupt support, no IRQ base\n");
		return 0;
	}

	/* Clear unattended interrupts */
	rk808_i2c_read(rk808, RK808_INT_STS_REG1, 1, &reg);
	rk808_i2c_write(rk808, RK808_INT_STS_REG1, 1, reg);
	rk808_i2c_read(rk808, RK808_INT_STS_REG2, 1, &reg);
	rk808_i2c_write(rk808, RK808_INT_STS_REG2, 1, reg);
	rk808_i2c_read(rk808, RK808_RTC_STATUS_REG, 1, &reg);	
	rk808_i2c_write(rk808, RK808_RTC_STATUS_REG, 1, reg);//clear alarm and timer interrupt

	/* Mask top level interrupts */
	rk808->irq_mask = 0xFFFFFF;

	mutex_init(&rk808->irq_lock);	
	wake_lock_init(&rk808->irq_wake, WAKE_LOCK_SUSPEND, "rk808_irq_wake");
	rk808->chip_irq = irq;
	rk808->irq_base = pdata->irq_base;

	rk808->irq_num = RK808_NUM_IRQ;

	/* Register with genirq */
	for (cur_irq = rk808->irq_base;
	     cur_irq < rk808->irq_num + rk808->irq_base;
	     cur_irq++) {
		irq_set_chip_data(cur_irq, rk808);
		irq_set_chip_and_handler(cur_irq, &rk808_irq_chip,
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

	ret = request_threaded_irq(irq, NULL, rk808_irq, flags, "rk808", rk808);

	irq_set_irq_type(irq, IRQ_TYPE_LEVEL_LOW);

	if (ret != 0)
		dev_err(rk808->dev, "Failed to request IRQ: %d\n", ret);
	
	return ret;
}

int rk808_irq_exit(struct rk808 *rk808)
{
	if (rk808->chip_irq)
		free_irq(rk808->chip_irq, rk808);
	return 0;
}
