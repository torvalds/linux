/*
 * Synopsys DesignWare I2C adapter driver.
 *
 * Based on the TI DAVINCI I2C adapter driver.
 *
 * Copyright (C) 2006 Texas Instruments.
 * Copyright (C) 2007 MontaVista Software Inc.
 * Copyright (C) 2009 Provigent Ltd.
 *
 * ----------------------------------------------------------------------------
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * ----------------------------------------------------------------------------
 *
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>

#include "i2c-designware-core.h"

static char *abort_sources[] = {
	[ABRT_7B_ADDR_NOACK] =
		"slave address not acknowledged (7bit mode)",
	[ABRT_10ADDR1_NOACK] =
		"first address byte not acknowledged (10bit mode)",
	[ABRT_10ADDR2_NOACK] =
		"second address byte not acknowledged (10bit mode)",
	[ABRT_TXDATA_NOACK] =
		"data not acknowledged",
	[ABRT_GCALL_NOACK] =
		"no acknowledgement for a general call",
	[ABRT_GCALL_READ] =
		"read after general call",
	[ABRT_SBYTE_ACKDET] =
		"start byte acknowledged",
	[ABRT_SBYTE_NORSTRT] =
		"trying to send start byte when restart is disabled",
	[ABRT_10B_RD_NORSTRT] =
		"trying to read when restart is disabled (10bit mode)",
	[ABRT_MASTER_DIS] =
		"trying to use disabled adapter",
	[ARB_LOST] =
		"lost arbitration",
	[ABRT_SLAVE_FLUSH_TXFIFO] =
		"read command so flush old data in the TX FIFO",
	[ABRT_SLAVE_ARBLOST] =
		"slave lost the bus while transmitting data to a remote master",
	[ABRT_SLAVE_RD_INTX] =
		"incorrect slave-transmitter mode configuration",
};

u32 dw_readl(struct dw_i2c_dev *dev, int offset)
{
	u32 value;

	if (dev->flags & ACCESS_16BIT)
		value = readw_relaxed(dev->base + offset) |
			(readw_relaxed(dev->base + offset + 2) << 16);
	else
		value = readl_relaxed(dev->base + offset);

	if (dev->flags & ACCESS_SWAP)
		return swab32(value);
	else
		return value;
}

void dw_writel(struct dw_i2c_dev *dev, u32 b, int offset)
{
	if (dev->flags & ACCESS_SWAP)
		b = swab32(b);

	if (dev->flags & ACCESS_16BIT) {
		writew_relaxed((u16)b, dev->base + offset);
		writew_relaxed((u16)(b >> 16), dev->base + offset + 2);
	} else {
		writel_relaxed(b, dev->base + offset);
	}
}

u32 i2c_dw_scl_hcnt(u32 ic_clk, u32 tSYMBOL, u32 tf, int cond, int offset)
{
	/*
	 * DesignWare I2C core doesn't seem to have solid strategy to meet
	 * the tHD;STA timing spec.  Configuring _HCNT based on tHIGH spec
	 * will result in violation of the tHD;STA spec.
	 */
	if (cond)
		/*
		 * Conditional expression:
		 *
		 *   IC_[FS]S_SCL_HCNT + (1+4+3) >= IC_CLK * tHIGH
		 *
		 * This is based on the DW manuals, and represents an ideal
		 * configuration.  The resulting I2C bus speed will be
		 * faster than any of the others.
		 *
		 * If your hardware is free from tHD;STA issue, try this one.
		 */
		return (ic_clk * tSYMBOL + 500000) / 1000000 - 8 + offset;
	else
		/*
		 * Conditional expression:
		 *
		 *   IC_[FS]S_SCL_HCNT + 3 >= IC_CLK * (tHD;STA + tf)
		 *
		 * This is just experimental rule; the tHD;STA period turned
		 * out to be proportinal to (_HCNT + 3).  With this setting,
		 * we could meet both tHIGH and tHD;STA timing specs.
		 *
		 * If unsure, you'd better to take this alternative.
		 *
		 * The reason why we need to take into account "tf" here,
		 * is the same as described in i2c_dw_scl_lcnt().
		 */
		return (ic_clk * (tSYMBOL + tf) + 500000) / 1000000
			- 3 + offset;
}

u32 i2c_dw_scl_lcnt(u32 ic_clk, u32 tLOW, u32 tf, int offset)
{
	/*
	 * Conditional expression:
	 *
	 *   IC_[FS]S_SCL_LCNT + 1 >= IC_CLK * (tLOW + tf)
	 *
	 * DW I2C core starts counting the SCL CNTs for the LOW period
	 * of the SCL clock (tLOW) as soon as it pulls the SCL line.
	 * In order to meet the tLOW timing spec, we need to take into
	 * account the fall time of SCL signal (tf).  Default tf value
	 * should be 0.3 us, for safety.
	 */
	return ((ic_clk * (tLOW + tf) + 500000) / 1000000) - 1 + offset;
}

void __i2c_dw_enable(struct dw_i2c_dev *dev, bool enable)
{
	dw_writel(dev, enable, DW_IC_ENABLE);
}

void __i2c_dw_enable_and_wait(struct dw_i2c_dev *dev, bool enable)
{
	int timeout = 100;

	do {
		__i2c_dw_enable(dev, enable);
		if ((dw_readl(dev, DW_IC_ENABLE_STATUS) & 1) == enable)
			return;

		/*
		 * Wait 10 times the signaling period of the highest I2C
		 * transfer supported by the driver (for 400KHz this is
		 * 25us) as described in the DesignWare I2C databook.
		 */
		usleep_range(25, 250);
	} while (timeout--);

	dev_warn(dev->dev, "timeout in %sabling adapter\n",
		 enable ? "en" : "dis");
}

unsigned long i2c_dw_clk_rate(struct dw_i2c_dev *dev)
{
	/*
	 * Clock is not necessary if we got LCNT/HCNT values directly from
	 * the platform code.
	 */
	if (WARN_ON_ONCE(!dev->get_clk_rate_khz))
		return 0;
	return dev->get_clk_rate_khz(dev);
}

int i2c_dw_prepare_clk(struct dw_i2c_dev *dev, bool prepare)
{
	if (IS_ERR(dev->clk))
		return PTR_ERR(dev->clk);

	if (prepare)
		return clk_prepare_enable(dev->clk);

	clk_disable_unprepare(dev->clk);
	return 0;
}

int i2c_dw_acquire_lock(struct dw_i2c_dev *dev)
{
	int ret;

	if (!dev->acquire_lock)
		return 0;

	ret = dev->acquire_lock(dev);
	if (!ret)
		return 0;

	dev_err(dev->dev, "couldn't acquire bus ownership\n");

	return ret;
}

void i2c_dw_release_lock(struct dw_i2c_dev *dev)
{
	if (dev->release_lock)
		dev->release_lock(dev);
}

/*
 * Waiting for bus not busy
 */
int i2c_dw_wait_bus_not_busy(struct dw_i2c_dev *dev)
{
	int timeout = TIMEOUT;

	while (dw_readl(dev, DW_IC_STATUS) & DW_IC_STATUS_ACTIVITY) {
		if (timeout <= 0) {
			dev_warn(dev->dev, "timeout waiting for bus ready\n");
			i2c_recover_bus(&dev->adapter);

			if (dw_readl(dev, DW_IC_STATUS) & DW_IC_STATUS_ACTIVITY)
				return -ETIMEDOUT;
			return 0;
		}
		timeout--;
		usleep_range(1000, 1100);
	}

	return 0;
}

int i2c_dw_handle_tx_abort(struct dw_i2c_dev *dev)
{
	unsigned long abort_source = dev->abort_source;
	int i;

	if (abort_source & DW_IC_TX_ABRT_NOACK) {
		for_each_set_bit(i, &abort_source, ARRAY_SIZE(abort_sources))
			dev_dbg(dev->dev,
				"%s: %s\n", __func__, abort_sources[i]);
		return -EREMOTEIO;
	}

	for_each_set_bit(i, &abort_source, ARRAY_SIZE(abort_sources))
		dev_err(dev->dev, "%s: %s\n", __func__, abort_sources[i]);

	if (abort_source & DW_IC_TX_ARB_LOST)
		return -EAGAIN;
	else if (abort_source & DW_IC_TX_ABRT_GCALL_READ)
		return -EINVAL; /* wrong msgs[] data */
	else
		return -EIO;
}

u32 i2c_dw_func(struct i2c_adapter *adap)
{
	struct dw_i2c_dev *dev = i2c_get_adapdata(adap);

	return dev->functionality;
}

void i2c_dw_disable(struct dw_i2c_dev *dev)
{
	/* Disable controller */
	__i2c_dw_enable_and_wait(dev, false);

	/* Disable all interupts */
	dw_writel(dev, 0, DW_IC_INTR_MASK);
	dw_readl(dev, DW_IC_CLR_INTR);
}

void i2c_dw_disable_int(struct dw_i2c_dev *dev)
{
	dw_writel(dev, 0, DW_IC_INTR_MASK);
}

u32 i2c_dw_read_comp_param(struct dw_i2c_dev *dev)
{
	return dw_readl(dev, DW_IC_COMP_PARAM_1);
}
EXPORT_SYMBOL_GPL(i2c_dw_read_comp_param);

MODULE_DESCRIPTION("Synopsys DesignWare I2C bus adapter core");
MODULE_LICENSE("GPL");
