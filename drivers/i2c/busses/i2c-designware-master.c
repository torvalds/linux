/*
 * Synopsys DesignWare I2C adapter driver (master only).
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
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>

#include "i2c-designware-core.h"

static void i2c_dw_configure_fifo_master(struct dw_i2c_dev *dev)
{
	/* Configure Tx/Rx FIFO threshold levels */
	dw_writel(dev, dev->tx_fifo_depth / 2, DW_IC_TX_TL);
	dw_writel(dev, 0, DW_IC_RX_TL);

	/* Configure the I2C master */
	dw_writel(dev, dev->master_cfg, DW_IC_CON);
}

/**
 * i2c_dw_init() - Initialize the designware I2C master hardware
 * @dev: device private data
 *
 * This functions configures and enables the I2C master.
 * This function is called during I2C init function, and in case of timeout at
 * run time.
 */
static int i2c_dw_init_master(struct dw_i2c_dev *dev)
{
	u32 hcnt, lcnt;
	u32 reg, comp_param1;
	u32 sda_falling_time, scl_falling_time;
	int ret;

	ret = i2c_dw_acquire_lock(dev);
	if (ret)
		return ret;

	reg = dw_readl(dev, DW_IC_COMP_TYPE);
	if (reg == ___constant_swab32(DW_IC_COMP_TYPE_VALUE)) {
		/* Configure register endianess access */
		dev->flags |= ACCESS_SWAP;
	} else if (reg == (DW_IC_COMP_TYPE_VALUE & 0x0000ffff)) {
		/* Configure register access mode 16bit */
		dev->flags |= ACCESS_16BIT;
	} else if (reg != DW_IC_COMP_TYPE_VALUE) {
		dev_err(dev->dev,
			"Unknown Synopsys component type: 0x%08x\n", reg);
		i2c_dw_release_lock(dev);
		return -ENODEV;
	}

	comp_param1 = dw_readl(dev, DW_IC_COMP_PARAM_1);

	/* Disable the adapter */
	__i2c_dw_enable_and_wait(dev, false);

	/* Set standard and fast speed deviders for high/low periods */

	sda_falling_time = dev->sda_falling_time ?: 300; /* ns */
	scl_falling_time = dev->scl_falling_time ?: 300; /* ns */

	/* Set SCL timing parameters for standard-mode */
	if (dev->ss_hcnt && dev->ss_lcnt) {
		hcnt = dev->ss_hcnt;
		lcnt = dev->ss_lcnt;
	} else {
		hcnt = i2c_dw_scl_hcnt(i2c_dw_clk_rate(dev),
					4000,	/* tHD;STA = tHIGH = 4.0 us */
					sda_falling_time,
					0,	/* 0: DW default, 1: Ideal */
					0);	/* No offset */
		lcnt = i2c_dw_scl_lcnt(i2c_dw_clk_rate(dev),
					4700,	/* tLOW = 4.7 us */
					scl_falling_time,
					0);	/* No offset */
	}
	dw_writel(dev, hcnt, DW_IC_SS_SCL_HCNT);
	dw_writel(dev, lcnt, DW_IC_SS_SCL_LCNT);
	dev_dbg(dev->dev, "Standard-mode HCNT:LCNT = %d:%d\n", hcnt, lcnt);

	/* Set SCL timing parameters for fast-mode or fast-mode plus */
	if ((dev->clk_freq == 1000000) && dev->fp_hcnt && dev->fp_lcnt) {
		hcnt = dev->fp_hcnt;
		lcnt = dev->fp_lcnt;
	} else if (dev->fs_hcnt && dev->fs_lcnt) {
		hcnt = dev->fs_hcnt;
		lcnt = dev->fs_lcnt;
	} else {
		hcnt = i2c_dw_scl_hcnt(i2c_dw_clk_rate(dev),
					600,	/* tHD;STA = tHIGH = 0.6 us */
					sda_falling_time,
					0,	/* 0: DW default, 1: Ideal */
					0);	/* No offset */
		lcnt = i2c_dw_scl_lcnt(i2c_dw_clk_rate(dev),
					1300,	/* tLOW = 1.3 us */
					scl_falling_time,
					0);	/* No offset */
	}
	dw_writel(dev, hcnt, DW_IC_FS_SCL_HCNT);
	dw_writel(dev, lcnt, DW_IC_FS_SCL_LCNT);
	dev_dbg(dev->dev, "Fast-mode HCNT:LCNT = %d:%d\n", hcnt, lcnt);

	if ((dev->master_cfg & DW_IC_CON_SPEED_MASK) ==
		DW_IC_CON_SPEED_HIGH) {
		if ((comp_param1 & DW_IC_COMP_PARAM_1_SPEED_MODE_MASK)
			!= DW_IC_COMP_PARAM_1_SPEED_MODE_HIGH) {
			dev_err(dev->dev, "High Speed not supported!\n");
			dev->master_cfg &= ~DW_IC_CON_SPEED_MASK;
			dev->master_cfg |= DW_IC_CON_SPEED_FAST;
		} else if (dev->hs_hcnt && dev->hs_lcnt) {
			hcnt = dev->hs_hcnt;
			lcnt = dev->hs_lcnt;
			dw_writel(dev, hcnt, DW_IC_HS_SCL_HCNT);
			dw_writel(dev, lcnt, DW_IC_HS_SCL_LCNT);
			dev_dbg(dev->dev, "HighSpeed-mode HCNT:LCNT = %d:%d\n",
				hcnt, lcnt);
		}
	}

	/* Configure SDA Hold Time if required */
	reg = dw_readl(dev, DW_IC_COMP_VERSION);
	if (reg >= DW_IC_SDA_HOLD_MIN_VERS) {
		if (!dev->sda_hold_time) {
			/* Keep previous hold time setting if no one set it */
			dev->sda_hold_time = dw_readl(dev, DW_IC_SDA_HOLD);
		}
		/*
		 * Workaround for avoiding TX arbitration lost in case I2C
		 * slave pulls SDA down "too quickly" after falling egde of
		 * SCL by enabling non-zero SDA RX hold. Specification says it
		 * extends incoming SDA low to high transition while SCL is
		 * high but it apprears to help also above issue.
		 */
		if (!(dev->sda_hold_time & DW_IC_SDA_HOLD_RX_MASK))
			dev->sda_hold_time |= 1 << DW_IC_SDA_HOLD_RX_SHIFT;
		dw_writel(dev, dev->sda_hold_time, DW_IC_SDA_HOLD);
	} else {
		dev_warn(dev->dev,
			"Hardware too old to adjust SDA hold time.\n");
	}

	i2c_dw_configure_fifo_master(dev);
	i2c_dw_release_lock(dev);

	return 0;
}

static void i2c_dw_xfer_init(struct dw_i2c_dev *dev)
{
	struct i2c_msg *msgs = dev->msgs;
	u32 ic_con, ic_tar = 0;

	/* Disable the adapter */
	__i2c_dw_enable_and_wait(dev, false);

	/* If the slave address is ten bit address, enable 10BITADDR */
	ic_con = dw_readl(dev, DW_IC_CON);
	if (msgs[dev->msg_write_idx].flags & I2C_M_TEN) {
		ic_con |= DW_IC_CON_10BITADDR_MASTER;
		/*
		 * If I2C_DYNAMIC_TAR_UPDATE is set, the 10-bit addressing
		 * mode has to be enabled via bit 12 of IC_TAR register.
		 * We set it always as I2C_DYNAMIC_TAR_UPDATE can't be
		 * detected from registers.
		 */
		ic_tar = DW_IC_TAR_10BITADDR_MASTER;
	} else {
		ic_con &= ~DW_IC_CON_10BITADDR_MASTER;
	}

	dw_writel(dev, ic_con, DW_IC_CON);

	/*
	 * Set the slave (target) address and enable 10-bit addressing mode
	 * if applicable.
	 */
	dw_writel(dev, msgs[dev->msg_write_idx].addr | ic_tar, DW_IC_TAR);

	/* Enforce disabled interrupts (due to HW issues) */
	i2c_dw_disable_int(dev);

	/* Enable the adapter */
	__i2c_dw_enable_and_wait(dev, true);

	/* Clear and enable interrupts */
	dw_readl(dev, DW_IC_CLR_INTR);
	dw_writel(dev, DW_IC_INTR_MASTER_MASK, DW_IC_INTR_MASK);
}

/*
 * Initiate (and continue) low level master read/write transaction.
 * This function is only called from i2c_dw_isr, and pumping i2c_msg
 * messages into the tx buffer.  Even if the size of i2c_msg data is
 * longer than the size of the tx buffer, it handles everything.
 */
static void
i2c_dw_xfer_msg(struct dw_i2c_dev *dev)
{
	struct i2c_msg *msgs = dev->msgs;
	u32 intr_mask;
	int tx_limit, rx_limit;
	u32 addr = msgs[dev->msg_write_idx].addr;
	u32 buf_len = dev->tx_buf_len;
	u8 *buf = dev->tx_buf;
	bool need_restart = false;

	intr_mask = DW_IC_INTR_MASTER_MASK;

	for (; dev->msg_write_idx < dev->msgs_num; dev->msg_write_idx++) {
		u32 flags = msgs[dev->msg_write_idx].flags;

		/*
		 * If target address has changed, we need to
		 * reprogram the target address in the I2C
		 * adapter when we are done with this transfer.
		 */
		if (msgs[dev->msg_write_idx].addr != addr) {
			dev_err(dev->dev,
				"%s: invalid target address\n", __func__);
			dev->msg_err = -EINVAL;
			break;
		}

		if (msgs[dev->msg_write_idx].len == 0) {
			dev_err(dev->dev,
				"%s: invalid message length\n", __func__);
			dev->msg_err = -EINVAL;
			break;
		}

		if (!(dev->status & STATUS_WRITE_IN_PROGRESS)) {
			/* new i2c_msg */
			buf = msgs[dev->msg_write_idx].buf;
			buf_len = msgs[dev->msg_write_idx].len;

			/* If both IC_EMPTYFIFO_HOLD_MASTER_EN and
			 * IC_RESTART_EN are set, we must manually
			 * set restart bit between messages.
			 */
			if ((dev->master_cfg & DW_IC_CON_RESTART_EN) &&
					(dev->msg_write_idx > 0))
				need_restart = true;
		}

		tx_limit = dev->tx_fifo_depth - dw_readl(dev, DW_IC_TXFLR);
		rx_limit = dev->rx_fifo_depth - dw_readl(dev, DW_IC_RXFLR);

		while (buf_len > 0 && tx_limit > 0 && rx_limit > 0) {
			u32 cmd = 0;

			/*
			 * If IC_EMPTYFIFO_HOLD_MASTER_EN is set we must
			 * manually set the stop bit. However, it cannot be
			 * detected from the registers so we set it always
			 * when writing/reading the last byte.
			 */

			/*
			 * i2c-core always sets the buffer length of
			 * I2C_FUNC_SMBUS_BLOCK_DATA to 1. The length will
			 * be adjusted when receiving the first byte.
			 * Thus we can't stop the transaction here.
			 */
			if (dev->msg_write_idx == dev->msgs_num - 1 &&
			    buf_len == 1 && !(flags & I2C_M_RECV_LEN))
				cmd |= BIT(9);

			if (need_restart) {
				cmd |= BIT(10);
				need_restart = false;
			}

			if (msgs[dev->msg_write_idx].flags & I2C_M_RD) {

				/* Avoid rx buffer overrun */
				if (dev->rx_outstanding >= dev->rx_fifo_depth)
					break;

				dw_writel(dev, cmd | 0x100, DW_IC_DATA_CMD);
				rx_limit--;
				dev->rx_outstanding++;
			} else
				dw_writel(dev, cmd | *buf++, DW_IC_DATA_CMD);
			tx_limit--; buf_len--;
		}

		dev->tx_buf = buf;
		dev->tx_buf_len = buf_len;

		/*
		 * Because we don't know the buffer length in the
		 * I2C_FUNC_SMBUS_BLOCK_DATA case, we can't stop
		 * the transaction here.
		 */
		if (buf_len > 0 || flags & I2C_M_RECV_LEN) {
			/* more bytes to be written */
			dev->status |= STATUS_WRITE_IN_PROGRESS;
			break;
		} else
			dev->status &= ~STATUS_WRITE_IN_PROGRESS;
	}

	/*
	 * If i2c_msg index search is completed, we don't need TX_EMPTY
	 * interrupt any more.
	 */
	if (dev->msg_write_idx == dev->msgs_num)
		intr_mask &= ~DW_IC_INTR_TX_EMPTY;

	if (dev->msg_err)
		intr_mask = 0;

	dw_writel(dev, intr_mask,  DW_IC_INTR_MASK);
}

static u8
i2c_dw_recv_len(struct dw_i2c_dev *dev, u8 len)
{
	struct i2c_msg *msgs = dev->msgs;
	u32 flags = msgs[dev->msg_read_idx].flags;

	/*
	 * Adjust the buffer length and mask the flag
	 * after receiving the first byte.
	 */
	len += (flags & I2C_CLIENT_PEC) ? 2 : 1;
	dev->tx_buf_len = len - min_t(u8, len, dev->rx_outstanding);
	msgs[dev->msg_read_idx].len = len;
	msgs[dev->msg_read_idx].flags &= ~I2C_M_RECV_LEN;

	return len;
}

static void
i2c_dw_read(struct dw_i2c_dev *dev)
{
	struct i2c_msg *msgs = dev->msgs;
	int rx_valid;

	for (; dev->msg_read_idx < dev->msgs_num; dev->msg_read_idx++) {
		u32 len;
		u8 *buf;

		if (!(msgs[dev->msg_read_idx].flags & I2C_M_RD))
			continue;

		if (!(dev->status & STATUS_READ_IN_PROGRESS)) {
			len = msgs[dev->msg_read_idx].len;
			buf = msgs[dev->msg_read_idx].buf;
		} else {
			len = dev->rx_buf_len;
			buf = dev->rx_buf;
		}

		rx_valid = dw_readl(dev, DW_IC_RXFLR);

		for (; len > 0 && rx_valid > 0; len--, rx_valid--) {
			u32 flags = msgs[dev->msg_read_idx].flags;

			*buf = dw_readl(dev, DW_IC_DATA_CMD);
			/* Ensure length byte is a valid value */
			if (flags & I2C_M_RECV_LEN &&
				*buf <= I2C_SMBUS_BLOCK_MAX && *buf > 0) {
				len = i2c_dw_recv_len(dev, *buf);
			}
			buf++;
			dev->rx_outstanding--;
		}

		if (len > 0) {
			dev->status |= STATUS_READ_IN_PROGRESS;
			dev->rx_buf_len = len;
			dev->rx_buf = buf;
			return;
		} else
			dev->status &= ~STATUS_READ_IN_PROGRESS;
	}
}

/*
 * Prepare controller for a transaction and call i2c_dw_xfer_msg.
 */
static int
i2c_dw_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	struct dw_i2c_dev *dev = i2c_get_adapdata(adap);
	int ret;

	dev_dbg(dev->dev, "%s: msgs: %d\n", __func__, num);

	pm_runtime_get_sync(dev->dev);

	reinit_completion(&dev->cmd_complete);
	dev->msgs = msgs;
	dev->msgs_num = num;
	dev->cmd_err = 0;
	dev->msg_write_idx = 0;
	dev->msg_read_idx = 0;
	dev->msg_err = 0;
	dev->status = STATUS_IDLE;
	dev->abort_source = 0;
	dev->rx_outstanding = 0;

	ret = i2c_dw_acquire_lock(dev);
	if (ret)
		goto done_nolock;

	ret = i2c_dw_wait_bus_not_busy(dev);
	if (ret < 0)
		goto done;

	/* Start the transfers */
	i2c_dw_xfer_init(dev);

	/* Wait for tx to complete */
	if (!wait_for_completion_timeout(&dev->cmd_complete, adap->timeout)) {
		dev_err(dev->dev, "controller timed out\n");
		/* i2c_dw_init implicitly disables the adapter */
		i2c_dw_init_master(dev);
		ret = -ETIMEDOUT;
		goto done;
	}

	/*
	 * We must disable the adapter before returning and signaling the end
	 * of the current transfer. Otherwise the hardware might continue
	 * generating interrupts which in turn causes a race condition with
	 * the following transfer.  Needs some more investigation if the
	 * additional interrupts are a hardware bug or this driver doesn't
	 * handle them correctly yet.
	 */
	__i2c_dw_enable(dev, false);

	if (dev->msg_err) {
		ret = dev->msg_err;
		goto done;
	}

	/* No error */
	if (likely(!dev->cmd_err && !dev->status)) {
		ret = num;
		goto done;
	}

	/* We have an error */
	if (dev->cmd_err == DW_IC_ERR_TX_ABRT) {
		ret = i2c_dw_handle_tx_abort(dev);
		goto done;
	}

	if (dev->status)
		dev_err(dev->dev,
			"transfer terminated early - interrupt latency too high?\n");

	ret = -EIO;

done:
	i2c_dw_release_lock(dev);

done_nolock:
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);

	return ret;
}

static const struct i2c_algorithm i2c_dw_algo = {
	.master_xfer = i2c_dw_xfer,
	.functionality = i2c_dw_func,
};

static u32 i2c_dw_read_clear_intrbits(struct dw_i2c_dev *dev)
{
	u32 stat;

	/*
	 * The IC_INTR_STAT register just indicates "enabled" interrupts.
	 * Ths unmasked raw version of interrupt status bits are available
	 * in the IC_RAW_INTR_STAT register.
	 *
	 * That is,
	 *   stat = dw_readl(IC_INTR_STAT);
	 * equals to,
	 *   stat = dw_readl(IC_RAW_INTR_STAT) & dw_readl(IC_INTR_MASK);
	 *
	 * The raw version might be useful for debugging purposes.
	 */
	stat = dw_readl(dev, DW_IC_INTR_STAT);

	/*
	 * Do not use the IC_CLR_INTR register to clear interrupts, or
	 * you'll miss some interrupts, triggered during the period from
	 * dw_readl(IC_INTR_STAT) to dw_readl(IC_CLR_INTR).
	 *
	 * Instead, use the separately-prepared IC_CLR_* registers.
	 */
	if (stat & DW_IC_INTR_RX_UNDER)
		dw_readl(dev, DW_IC_CLR_RX_UNDER);
	if (stat & DW_IC_INTR_RX_OVER)
		dw_readl(dev, DW_IC_CLR_RX_OVER);
	if (stat & DW_IC_INTR_TX_OVER)
		dw_readl(dev, DW_IC_CLR_TX_OVER);
	if (stat & DW_IC_INTR_RD_REQ)
		dw_readl(dev, DW_IC_CLR_RD_REQ);
	if (stat & DW_IC_INTR_TX_ABRT) {
		/*
		 * The IC_TX_ABRT_SOURCE register is cleared whenever
		 * the IC_CLR_TX_ABRT is read.  Preserve it beforehand.
		 */
		dev->abort_source = dw_readl(dev, DW_IC_TX_ABRT_SOURCE);
		dw_readl(dev, DW_IC_CLR_TX_ABRT);
	}
	if (stat & DW_IC_INTR_RX_DONE)
		dw_readl(dev, DW_IC_CLR_RX_DONE);
	if (stat & DW_IC_INTR_ACTIVITY)
		dw_readl(dev, DW_IC_CLR_ACTIVITY);
	if (stat & DW_IC_INTR_STOP_DET)
		dw_readl(dev, DW_IC_CLR_STOP_DET);
	if (stat & DW_IC_INTR_START_DET)
		dw_readl(dev, DW_IC_CLR_START_DET);
	if (stat & DW_IC_INTR_GEN_CALL)
		dw_readl(dev, DW_IC_CLR_GEN_CALL);

	return stat;
}

/*
 * Interrupt service routine. This gets called whenever an I2C master interrupt
 * occurs.
 */
static int i2c_dw_irq_handler_master(struct dw_i2c_dev *dev)
{
	u32 stat;

	stat = i2c_dw_read_clear_intrbits(dev);
	if (stat & DW_IC_INTR_TX_ABRT) {
		dev->cmd_err |= DW_IC_ERR_TX_ABRT;
		dev->status = STATUS_IDLE;

		/*
		 * Anytime TX_ABRT is set, the contents of the tx/rx
		 * buffers are flushed. Make sure to skip them.
		 */
		dw_writel(dev, 0, DW_IC_INTR_MASK);
		goto tx_aborted;
	}

	if (stat & DW_IC_INTR_RX_FULL)
		i2c_dw_read(dev);

	if (stat & DW_IC_INTR_TX_EMPTY)
		i2c_dw_xfer_msg(dev);

	/*
	 * No need to modify or disable the interrupt mask here.
	 * i2c_dw_xfer_msg() will take care of it according to
	 * the current transmit status.
	 */

tx_aborted:
	if ((stat & (DW_IC_INTR_TX_ABRT | DW_IC_INTR_STOP_DET)) || dev->msg_err)
		complete(&dev->cmd_complete);
	else if (unlikely(dev->flags & ACCESS_INTR_MASK)) {
		/* Workaround to trigger pending interrupt */
		stat = dw_readl(dev, DW_IC_INTR_MASK);
		i2c_dw_disable_int(dev);
		dw_writel(dev, stat, DW_IC_INTR_MASK);
	}

	return 0;
}

static irqreturn_t i2c_dw_isr(int this_irq, void *dev_id)
{
	struct dw_i2c_dev *dev = dev_id;
	u32 stat, enabled;

	enabled = dw_readl(dev, DW_IC_ENABLE);
	stat = dw_readl(dev, DW_IC_RAW_INTR_STAT);
	dev_dbg(dev->dev, "enabled=%#x stat=%#x\n", enabled, stat);
	if (!enabled || !(stat & ~DW_IC_INTR_ACTIVITY))
		return IRQ_NONE;

	i2c_dw_irq_handler_master(dev);

	return IRQ_HANDLED;
}

int i2c_dw_probe(struct dw_i2c_dev *dev)
{
	struct i2c_adapter *adap = &dev->adapter;
	unsigned long irq_flags;
	int ret;

	init_completion(&dev->cmd_complete);

	dev->init = i2c_dw_init_master;
	dev->disable = i2c_dw_disable;
	dev->disable_int = i2c_dw_disable_int;

	ret = dev->init(dev);
	if (ret)
		return ret;

	snprintf(adap->name, sizeof(adap->name),
		 "Synopsys DesignWare I2C adapter");
	adap->retries = 3;
	adap->algo = &i2c_dw_algo;
	adap->dev.parent = dev->dev;
	i2c_set_adapdata(adap, dev);

	if (dev->pm_disabled) {
		dev_pm_syscore_device(dev->dev, true);
		irq_flags = IRQF_NO_SUSPEND;
	} else {
		irq_flags = IRQF_SHARED | IRQF_COND_SUSPEND;
	}

	i2c_dw_disable_int(dev);
	ret = devm_request_irq(dev->dev, dev->irq, i2c_dw_isr, irq_flags,
			       dev_name(dev->dev), dev);
	if (ret) {
		dev_err(dev->dev, "failure requesting irq %i: %d\n",
			dev->irq, ret);
		return ret;
	}

	/*
	 * Increment PM usage count during adapter registration in order to
	 * avoid possible spurious runtime suspend when adapter device is
	 * registered to the device core and immediate resume in case bus has
	 * registered I2C slaves that do I2C transfers in their probe.
	 */
	pm_runtime_get_noresume(dev->dev);
	ret = i2c_add_numbered_adapter(adap);
	if (ret)
		dev_err(dev->dev, "failure adding adapter: %d\n", ret);
	pm_runtime_put_noidle(dev->dev);

	return ret;
}
EXPORT_SYMBOL_GPL(i2c_dw_probe);

MODULE_DESCRIPTION("Synopsys DesignWare I2C bus master adapter");
MODULE_LICENSE("GPL");
