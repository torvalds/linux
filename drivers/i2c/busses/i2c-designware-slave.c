// SPDX-License-Identifier: GPL-2.0
/*
 * Synopsys DesignWare I2C adapter driver (slave only).
 *
 * Based on the Synopsys DesignWare I2C adapter driver (master).
 *
 * Copyright (C) 2016 Synopsys Inc.
 */

#define DEFAULT_SYMBOL_NAMESPACE	"I2C_DW"

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include "i2c-designware-core.h"

int i2c_dw_reg_slave(struct i2c_client *slave)
{
	struct dw_i2c_dev *dev = i2c_get_adapdata(slave->adapter);
	int ret;

	if (!i2c_check_functionality(slave->adapter, I2C_FUNC_SLAVE))
		return -EOPNOTSUPP;
	if (dev->slave)
		return -EBUSY;
	if (slave->flags & I2C_CLIENT_TEN)
		return -EAFNOSUPPORT;

	ret = i2c_dw_acquire_lock(dev);
	if (ret)
		return ret;

	pm_runtime_get_sync(dev->dev);
	__i2c_dw_disable_nowait(dev);
	dev->slave = slave;
	i2c_dw_set_mode(dev, DW_IC_SLAVE);

	i2c_dw_release_lock(dev);

	return 0;
}

int i2c_dw_unreg_slave(struct i2c_client *slave)
{
	struct dw_i2c_dev *dev = i2c_get_adapdata(slave->adapter);

	regmap_write(dev->map, DW_IC_INTR_MASK, 0);
	i2c_dw_disable(dev);
	synchronize_irq(dev->irq);
	dev->slave = NULL;
	i2c_dw_set_mode(dev, DW_IC_MASTER);
	pm_runtime_put_sync_suspend(dev->dev);

	return 0;
}

static u32 i2c_dw_read_clear_intrbits_slave(struct dw_i2c_dev *dev)
{
	unsigned int stat, dummy;

	/*
	 * The IC_INTR_STAT register just indicates "enabled" interrupts.
	 * The unmasked raw version of interrupt status bits is available
	 * in the IC_RAW_INTR_STAT register.
	 *
	 * That is,
	 *   stat = readl(IC_INTR_STAT);
	 * equals to,
	 *   stat = readl(IC_RAW_INTR_STAT) & readl(IC_INTR_MASK);
	 *
	 * The raw version might be useful for debugging purposes.
	 */
	regmap_read(dev->map, DW_IC_INTR_STAT, &stat);

	/*
	 * Do not use the IC_CLR_INTR register to clear interrupts, or
	 * you'll miss some interrupts, triggered during the period from
	 * readl(IC_INTR_STAT) to readl(IC_CLR_INTR).
	 *
	 * Instead, use the separately-prepared IC_CLR_* registers.
	 */
	if (stat & DW_IC_INTR_TX_ABRT)
		regmap_read(dev->map, DW_IC_CLR_TX_ABRT, &dummy);
	if (stat & DW_IC_INTR_RX_UNDER)
		regmap_read(dev->map, DW_IC_CLR_RX_UNDER, &dummy);
	if (stat & DW_IC_INTR_RX_OVER)
		regmap_read(dev->map, DW_IC_CLR_RX_OVER, &dummy);
	if (stat & DW_IC_INTR_TX_OVER)
		regmap_read(dev->map, DW_IC_CLR_TX_OVER, &dummy);
	if (stat & DW_IC_INTR_RX_DONE)
		regmap_read(dev->map, DW_IC_CLR_RX_DONE, &dummy);
	if (stat & DW_IC_INTR_ACTIVITY)
		regmap_read(dev->map, DW_IC_CLR_ACTIVITY, &dummy);
	if (stat & DW_IC_INTR_STOP_DET)
		regmap_read(dev->map, DW_IC_CLR_STOP_DET, &dummy);
	if (stat & DW_IC_INTR_START_DET)
		regmap_read(dev->map, DW_IC_CLR_START_DET, &dummy);
	if (stat & DW_IC_INTR_GEN_CALL)
		regmap_read(dev->map, DW_IC_CLR_GEN_CALL, &dummy);

	return stat;
}

/*
 * Interrupt service routine. This gets called whenever an I2C slave interrupt
 * occurs.
 */
irqreturn_t i2c_dw_isr_slave(struct dw_i2c_dev *dev)
{
	unsigned int raw_stat, stat, enabled, tmp;
	u8 val = 0, slave_activity;

	regmap_read(dev->map, DW_IC_ENABLE, &enabled);
	regmap_read(dev->map, DW_IC_RAW_INTR_STAT, &raw_stat);
	regmap_read(dev->map, DW_IC_STATUS, &tmp);
	slave_activity = ((tmp & DW_IC_STATUS_SLAVE_ACTIVITY) >> 6);

	if (!enabled || !(raw_stat & ~DW_IC_INTR_ACTIVITY) || !dev->slave)
		return IRQ_NONE;

	stat = i2c_dw_read_clear_intrbits_slave(dev);
	dev_dbg(dev->dev,
		"%#x STATUS SLAVE_ACTIVITY=%#x : RAW_INTR_STAT=%#x : INTR_STAT=%#x\n",
		enabled, slave_activity, raw_stat, stat);

	if (stat & DW_IC_INTR_RX_FULL) {
		if (!(dev->status & STATUS_WRITE_IN_PROGRESS)) {
			dev->status |= STATUS_WRITE_IN_PROGRESS;
			dev->status &= ~STATUS_READ_IN_PROGRESS;
			i2c_slave_event(dev->slave, I2C_SLAVE_WRITE_REQUESTED,
					&val);
		}

		do {
			regmap_read(dev->map, DW_IC_DATA_CMD, &tmp);
			if (tmp & DW_IC_DATA_CMD_FIRST_DATA_BYTE)
				i2c_slave_event(dev->slave,
						I2C_SLAVE_WRITE_REQUESTED,
						&val);
			val = tmp;
			i2c_slave_event(dev->slave, I2C_SLAVE_WRITE_RECEIVED,
					&val);
			regmap_read(dev->map, DW_IC_STATUS, &tmp);
		} while (tmp & DW_IC_STATUS_RFNE);
	}

	if (stat & DW_IC_INTR_RD_REQ) {
		if (slave_activity) {
			regmap_read(dev->map, DW_IC_CLR_RD_REQ, &tmp);

			if (!(dev->status & STATUS_READ_IN_PROGRESS)) {
				i2c_slave_event(dev->slave,
						I2C_SLAVE_READ_REQUESTED,
						&val);
				dev->status |= STATUS_READ_IN_PROGRESS;
				dev->status &= ~STATUS_WRITE_IN_PROGRESS;
			} else {
				i2c_slave_event(dev->slave,
						I2C_SLAVE_READ_PROCESSED,
						&val);
			}
			regmap_write(dev->map, DW_IC_DATA_CMD, val);
		}
	}

	if (stat & DW_IC_INTR_STOP_DET)
		i2c_slave_event(dev->slave, I2C_SLAVE_STOP, &val);

	return IRQ_HANDLED;
}

void i2c_dw_configure_slave(struct dw_i2c_dev *dev)
{
	if (dev->flags & ACCESS_POLLING)
		return;

	dev->functionality |= I2C_FUNC_SLAVE;

	dev->slave_cfg = DW_IC_CON_RX_FIFO_FULL_HLD_CTRL |
			 DW_IC_CON_RESTART_EN | DW_IC_CON_STOP_DET_IFADDRESSED;
}
EXPORT_SYMBOL_GPL(i2c_dw_configure_slave);

MODULE_AUTHOR("Luis Oliveira <lolivei@synopsys.com>");
MODULE_DESCRIPTION("Synopsys DesignWare I2C bus slave adapter");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("I2C_DW_COMMON");
