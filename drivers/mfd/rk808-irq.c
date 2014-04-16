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
#include <linux/irqdomain.h>
#include <linux/regmap.h>


static inline int irq_to_rk808_irq(struct rk808 *rk808,
							int irq)
{
	return (irq - rk808->chip_irq);
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
	int i, cur_irq;
//	printk("%s,line=%d\n", __func__,__LINE__);	

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
		cur_irq = irq_find_mapping(rk808->irq_domain, i);

		if (cur_irq)
		handle_nested_irq(cur_irq);
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
static int rk808_irq_domain_map(struct irq_domain *d, unsigned int irq,
					irq_hw_number_t hw)
{
	struct rk808 *rk808 = d->host_data;

	irq_set_chip_data(irq, rk808);
	irq_set_chip_and_handler(irq, &rk808_irq_chip, handle_edge_irq);
	irq_set_nested_thread(irq, 1);
#ifdef CONFIG_ARM
	set_irq_flags(irq, IRQF_VALID);
#else
	irq_set_noprobe(irq);
#endif
	return 0;
}

static struct irq_domain_ops rk808_irq_domain_ops = {
	.map = rk808_irq_domain_map,
};

int rk808_irq_init(struct rk808 *rk808, int irq,struct rk808_board *pdata)
{
	struct irq_domain *domain;
	int ret,val,irq_type,flags;
	u8 reg;

//	printk("%s,line=%d\n", __func__,__LINE__);	
	if (!irq) {
		dev_warn(rk808->dev, "No interrupt support, no core IRQ\n");
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
	rk808->irq_num = RK808_NUM_IRQ;
	rk808->irq_gpio = pdata->irq_gpio;
	if (rk808->irq_gpio && !rk808->chip_irq) {
		rk808->chip_irq = gpio_to_irq(rk808->irq_gpio);

		if (rk808->irq_gpio) {
			ret = gpio_request(rk808->irq_gpio, "rk808_pmic_irq");
			if (ret < 0) {
				dev_err(rk808->dev,
					"Failed to request gpio %d with ret:"
					"%d\n",	rk808->irq_gpio, ret);
				return IRQ_NONE;
			}
			gpio_direction_input(rk808->irq_gpio);
			val = gpio_get_value(rk808->irq_gpio);
			if (val){
				irq_type = IRQ_TYPE_LEVEL_LOW;
				flags = IRQF_TRIGGER_FALLING;
			}
			else{
				irq_type = IRQ_TYPE_LEVEL_HIGH;
				flags = IRQF_TRIGGER_RISING;
			}
			gpio_free(rk808->irq_gpio);
			pr_info("%s: rk808_pmic_irq=%x\n", __func__, val);
		}
	}
	
	domain = irq_domain_add_linear(NULL, RK808_NUM_IRQ,
					&rk808_irq_domain_ops, rk808);
	if (!domain) {
		dev_err(rk808->dev, "could not create irq domain\n");
		return -ENODEV;
	}
	rk808->irq_domain = domain;

	ret = request_threaded_irq(rk808->chip_irq, NULL, rk808_irq, flags | IRQF_ONESHOT, "rk808", rk808);

	irq_set_irq_type(rk808->chip_irq, irq_type);
	enable_irq_wake(rk808->chip_irq);

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
