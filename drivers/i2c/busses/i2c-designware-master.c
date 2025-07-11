// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Synopsys DesignWare I2C adapter driver (master only).
 *
 * Based on the TI DAVINCI I2C adapter driver.
 *
 * Copyright (C) 2006 Texas Instruments.
 * Copyright (C) 2007 MontaVista Software Inc.
 * Copyright (C) 2009 Provigent Ltd.
 */

#define DEFAULT_SYMBOL_NAMESPACE	"I2C_DW"

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include "i2c-designware-core.h"

#define AMD_TIMEOUT_MIN_US	25
#define AMD_TIMEOUT_MAX_US	250
#define AMD_MASTERCFG_MASK	GENMASK(15, 0)

static void i2c_dw_configure_fifo_master(struct dw_i2c_dev *dev)
{
	/* Configure Tx/Rx FIFO threshold levels */
	regmap_write(dev->map, DW_IC_TX_TL, dev->tx_fifo_depth / 2);
	regmap_write(dev->map, DW_IC_RX_TL, 0);

	/* Configure the I2C master */
	regmap_write(dev->map, DW_IC_CON, dev->master_cfg);
}

static int i2c_dw_set_timings_master(struct dw_i2c_dev *dev)
{
	unsigned int comp_param1;
	u32 sda_falling_time, scl_falling_time;
	struct i2c_timings *t = &dev->timings;
	const char *fp_str = "";
	u32 ic_clk;
	int ret;

	ret = i2c_dw_acquire_lock(dev);
	if (ret)
		return ret;

	ret = regmap_read(dev->map, DW_IC_COMP_PARAM_1, &comp_param1);
	i2c_dw_release_lock(dev);
	if (ret)
		return ret;

	/* Set standard and fast speed dividers for high/low periods */
	sda_falling_time = t->sda_fall_ns ?: 300; /* ns */
	scl_falling_time = t->scl_fall_ns ?: 300; /* ns */

	/* Calculate SCL timing parameters for standard mode if not set */
	if (!dev->ss_hcnt || !dev->ss_lcnt) {
		ic_clk = i2c_dw_clk_rate(dev);
		dev->ss_hcnt =
			i2c_dw_scl_hcnt(dev,
					DW_IC_SS_SCL_HCNT,
					ic_clk,
					4000,	/* tHD;STA = tHIGH = 4.0 us */
					sda_falling_time,
					0);	/* No offset */
		dev->ss_lcnt =
			i2c_dw_scl_lcnt(dev,
					DW_IC_SS_SCL_LCNT,
					ic_clk,
					4700,	/* tLOW = 4.7 us */
					scl_falling_time,
					0);	/* No offset */
	}
	dev_dbg(dev->dev, "Standard Mode HCNT:LCNT = %d:%d\n",
		dev->ss_hcnt, dev->ss_lcnt);

	/*
	 * Set SCL timing parameters for fast mode or fast mode plus. Only
	 * difference is the timing parameter values since the registers are
	 * the same.
	 */
	if (t->bus_freq_hz == I2C_MAX_FAST_MODE_PLUS_FREQ) {
		/*
		 * Check are Fast Mode Plus parameters available. Calculate
		 * SCL timing parameters for Fast Mode Plus if not set.
		 */
		if (dev->fp_hcnt && dev->fp_lcnt) {
			dev->fs_hcnt = dev->fp_hcnt;
			dev->fs_lcnt = dev->fp_lcnt;
		} else {
			ic_clk = i2c_dw_clk_rate(dev);
			dev->fs_hcnt =
				i2c_dw_scl_hcnt(dev,
						DW_IC_FS_SCL_HCNT,
						ic_clk,
						260,	/* tHIGH = 260 ns */
						sda_falling_time,
						0);	/* No offset */
			dev->fs_lcnt =
				i2c_dw_scl_lcnt(dev,
						DW_IC_FS_SCL_LCNT,
						ic_clk,
						500,	/* tLOW = 500 ns */
						scl_falling_time,
						0);	/* No offset */
		}
		fp_str = " Plus";
	}
	/*
	 * Calculate SCL timing parameters for fast mode if not set. They are
	 * needed also in high speed mode.
	 */
	if (!dev->fs_hcnt || !dev->fs_lcnt) {
		ic_clk = i2c_dw_clk_rate(dev);
		dev->fs_hcnt =
			i2c_dw_scl_hcnt(dev,
					DW_IC_FS_SCL_HCNT,
					ic_clk,
					600,	/* tHD;STA = tHIGH = 0.6 us */
					sda_falling_time,
					0);	/* No offset */
		dev->fs_lcnt =
			i2c_dw_scl_lcnt(dev,
					DW_IC_FS_SCL_LCNT,
					ic_clk,
					1300,	/* tLOW = 1.3 us */
					scl_falling_time,
					0);	/* No offset */
	}
	dev_dbg(dev->dev, "Fast Mode%s HCNT:LCNT = %d:%d\n",
		fp_str, dev->fs_hcnt, dev->fs_lcnt);

	/* Check is high speed possible and fall back to fast mode if not */
	if ((dev->master_cfg & DW_IC_CON_SPEED_MASK) ==
		DW_IC_CON_SPEED_HIGH) {
		if ((comp_param1 & DW_IC_COMP_PARAM_1_SPEED_MODE_MASK)
			!= DW_IC_COMP_PARAM_1_SPEED_MODE_HIGH) {
			dev_err(dev->dev, "High Speed not supported!\n");
			t->bus_freq_hz = I2C_MAX_FAST_MODE_FREQ;
			dev->master_cfg &= ~DW_IC_CON_SPEED_MASK;
			dev->master_cfg |= DW_IC_CON_SPEED_FAST;
			dev->hs_hcnt = 0;
			dev->hs_lcnt = 0;
		} else if (!dev->hs_hcnt || !dev->hs_lcnt) {
			u32 t_high, t_low;

			/*
			 * The legal values stated in the databook for bus
			 * capacitance are only 100pF and 400pF.
			 * If dev->bus_capacitance_pF is greater than or equals
			 * to 400, t_high and t_low are assumed to be
			 * appropriate values for 400pF, otherwise 100pF.
			 */
			if (dev->bus_capacitance_pF >= 400) {
				/* assume bus capacitance is 400pF */
				t_high = dev->clk_freq_optimized ? 160 : 120;
				t_low = 320;
			} else {
				/* assume bus capacitance is 100pF */
				t_high = 60;
				t_low = dev->clk_freq_optimized ? 120 : 160;
			}

			ic_clk = i2c_dw_clk_rate(dev);
			dev->hs_hcnt =
				i2c_dw_scl_hcnt(dev,
						DW_IC_HS_SCL_HCNT,
						ic_clk,
						t_high,
						sda_falling_time,
						0);	/* No offset */
			dev->hs_lcnt =
				i2c_dw_scl_lcnt(dev,
						DW_IC_HS_SCL_LCNT,
						ic_clk,
						t_low,
						scl_falling_time,
						0);	/* No offset */
		}
		dev_dbg(dev->dev, "High Speed Mode HCNT:LCNT = %d:%d\n",
			dev->hs_hcnt, dev->hs_lcnt);
	}

	ret = i2c_dw_set_sda_hold(dev);
	if (ret)
		return ret;

	dev_dbg(dev->dev, "Bus speed: %s\n", i2c_freq_mode_string(t->bus_freq_hz));
	return 0;
}

/**
 * i2c_dw_init_master() - Initialize the DesignWare I2C master hardware
 * @dev: device private data
 *
 * This functions configures and enables the I2C master.
 * This function is called during I2C init function, and in case of timeout at
 * run time.
 *
 * Return: 0 on success, or negative errno otherwise.
 */
static int i2c_dw_init_master(struct dw_i2c_dev *dev)
{
	int ret;

	ret = i2c_dw_acquire_lock(dev);
	if (ret)
		return ret;

	/* Disable the adapter */
	__i2c_dw_disable(dev);

	/* Write standard speed timing parameters */
	regmap_write(dev->map, DW_IC_SS_SCL_HCNT, dev->ss_hcnt);
	regmap_write(dev->map, DW_IC_SS_SCL_LCNT, dev->ss_lcnt);

	/* Write fast mode/fast mode plus timing parameters */
	regmap_write(dev->map, DW_IC_FS_SCL_HCNT, dev->fs_hcnt);
	regmap_write(dev->map, DW_IC_FS_SCL_LCNT, dev->fs_lcnt);

	/* Write high speed timing parameters if supported */
	if (dev->hs_hcnt && dev->hs_lcnt) {
		regmap_write(dev->map, DW_IC_HS_SCL_HCNT, dev->hs_hcnt);
		regmap_write(dev->map, DW_IC_HS_SCL_LCNT, dev->hs_lcnt);
	}

	/* Write SDA hold time if supported */
	if (dev->sda_hold_time)
		regmap_write(dev->map, DW_IC_SDA_HOLD, dev->sda_hold_time);

	i2c_dw_configure_fifo_master(dev);
	i2c_dw_release_lock(dev);

	return 0;
}

static void i2c_dw_xfer_init(struct dw_i2c_dev *dev)
{
	struct i2c_msg *msgs = dev->msgs;
	u32 ic_con = 0, ic_tar = 0;
	unsigned int dummy;

	/* Disable the adapter */
	__i2c_dw_disable(dev);

	/* If the slave address is ten bit address, enable 10BITADDR */
	if (msgs[dev->msg_write_idx].flags & I2C_M_TEN) {
		ic_con = DW_IC_CON_10BITADDR_MASTER;
		/*
		 * If I2C_DYNAMIC_TAR_UPDATE is set, the 10-bit addressing
		 * mode has to be enabled via bit 12 of IC_TAR register.
		 * We set it always as I2C_DYNAMIC_TAR_UPDATE can't be
		 * detected from registers.
		 */
		ic_tar = DW_IC_TAR_10BITADDR_MASTER;
	}

	regmap_update_bits(dev->map, DW_IC_CON, DW_IC_CON_10BITADDR_MASTER,
			   ic_con);

	/*
	 * Set the slave (target) address and enable 10-bit addressing mode
	 * if applicable.
	 */
	regmap_write(dev->map, DW_IC_TAR,
		     msgs[dev->msg_write_idx].addr | ic_tar);

	/* Enforce disabled interrupts (due to HW issues) */
	__i2c_dw_write_intr_mask(dev, 0);

	/* Enable the adapter */
	__i2c_dw_enable(dev);

	/* Dummy read to avoid the register getting stuck on Bay Trail */
	regmap_read(dev->map, DW_IC_ENABLE_STATUS, &dummy);

	/* Clear and enable interrupts */
	regmap_read(dev->map, DW_IC_CLR_INTR, &dummy);
	__i2c_dw_write_intr_mask(dev, DW_IC_INTR_MASTER_MASK);
}

/*
 * This function waits for the controller to be idle before disabling I2C
 * When the controller is not in the IDLE state, the MST_ACTIVITY bit
 * (IC_STATUS[5]) is set.
 *
 * Values:
 * 0x1 (ACTIVE): Controller not idle
 * 0x0 (IDLE): Controller is idle
 *
 * The function is called after completing the current transfer.
 *
 * Returns:
 * False when the controller is in the IDLE state.
 * True when the controller is in the ACTIVE state.
 */
static bool i2c_dw_is_controller_active(struct dw_i2c_dev *dev)
{
	u32 status;

	regmap_read(dev->map, DW_IC_STATUS, &status);
	if (!(status & DW_IC_STATUS_MASTER_ACTIVITY))
		return false;

	return regmap_read_poll_timeout(dev->map, DW_IC_STATUS, status,
				       !(status & DW_IC_STATUS_MASTER_ACTIVITY),
				       1100, 20000) != 0;
}

static int i2c_dw_check_stopbit(struct dw_i2c_dev *dev)
{
	u32 val;
	int ret;

	ret = regmap_read_poll_timeout(dev->map, DW_IC_INTR_STAT, val,
				       !(val & DW_IC_INTR_STOP_DET),
					1100, 20000);
	if (ret)
		dev_err(dev->dev, "i2c timeout error %d\n", ret);

	return ret;
}

static int i2c_dw_status(struct dw_i2c_dev *dev)
{
	int status;

	status = i2c_dw_wait_bus_not_busy(dev);
	if (status)
		return status;

	return i2c_dw_check_stopbit(dev);
}

/*
 * Initiate and continue master read/write transaction with polling
 * based transfer routine afterward write messages into the Tx buffer.
 */
static int amd_i2c_dw_xfer_quirk(struct i2c_adapter *adap, struct i2c_msg *msgs, int num_msgs)
{
	struct dw_i2c_dev *dev = i2c_get_adapdata(adap);
	int msg_wrt_idx, msg_itr_lmt, buf_len, data_idx;
	int cmd = 0, status;
	u8 *tx_buf;
	unsigned int val;

	/*
	 * In order to enable the interrupt for UCSI i.e. AMD NAVI GPU card,
	 * it is mandatory to set the right value in specific register
	 * (offset:0x474) as per the hardware IP specification.
	 */
	regmap_write(dev->map, AMD_UCSI_INTR_REG, AMD_UCSI_INTR_EN);

	dev->msgs = msgs;
	dev->msgs_num = num_msgs;
	i2c_dw_xfer_init(dev);

	/* Initiate messages read/write transaction */
	for (msg_wrt_idx = 0; msg_wrt_idx < num_msgs; msg_wrt_idx++) {
		tx_buf = msgs[msg_wrt_idx].buf;
		buf_len = msgs[msg_wrt_idx].len;

		if (!(msgs[msg_wrt_idx].flags & I2C_M_RD))
			regmap_write(dev->map, DW_IC_TX_TL, buf_len - 1);
		/*
		 * Initiate the i2c read/write transaction of buffer length,
		 * and poll for bus busy status. For the last message transfer,
		 * update the command with stop bit enable.
		 */
		for (msg_itr_lmt = buf_len; msg_itr_lmt > 0; msg_itr_lmt--) {
			if (msg_wrt_idx == num_msgs - 1 && msg_itr_lmt == 1)
				cmd |= BIT(9);

			if (msgs[msg_wrt_idx].flags & I2C_M_RD) {
				/* Due to hardware bug, need to write the same command twice. */
				regmap_write(dev->map, DW_IC_DATA_CMD, 0x100);
				regmap_write(dev->map, DW_IC_DATA_CMD, 0x100 | cmd);
				if (cmd) {
					regmap_write(dev->map, DW_IC_TX_TL, 2 * (buf_len - 1));
					regmap_write(dev->map, DW_IC_RX_TL, 2 * (buf_len - 1));
					/*
					 * Need to check the stop bit. However, it cannot be
					 * detected from the registers so we check it always
					 * when read/write the last byte.
					 */
					status = i2c_dw_status(dev);
					if (status)
						return status;

					for (data_idx = 0; data_idx < buf_len; data_idx++) {
						regmap_read(dev->map, DW_IC_DATA_CMD, &val);
						tx_buf[data_idx] = val;
					}
					status = i2c_dw_check_stopbit(dev);
					if (status)
						return status;
				}
			} else {
				regmap_write(dev->map, DW_IC_DATA_CMD, *tx_buf++ | cmd);
				usleep_range(AMD_TIMEOUT_MIN_US, AMD_TIMEOUT_MAX_US);
			}
		}
		status = i2c_dw_check_stopbit(dev);
		if (status)
			return status;
	}

	return 0;
}

/*
 * Initiate (and continue) low level master read/write transaction.
 * This function is only called from i2c_dw_isr(), and pumping i2c_msg
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
	unsigned int flr;

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

		if (!(dev->status & STATUS_WRITE_IN_PROGRESS)) {
			/* new i2c_msg */
			buf = msgs[dev->msg_write_idx].buf;
			buf_len = msgs[dev->msg_write_idx].len;

			/*
			 * If both IC_EMPTYFIFO_HOLD_MASTER_EN and
			 * IC_RESTART_EN are set, we must manually
			 * set restart bit between messages.
			 */
			if ((dev->master_cfg & DW_IC_CON_RESTART_EN) &&
					(dev->msg_write_idx > 0))
				need_restart = true;
		}

		regmap_read(dev->map, DW_IC_TXFLR, &flr);
		tx_limit = dev->tx_fifo_depth - flr;

		regmap_read(dev->map, DW_IC_RXFLR, &flr);
		rx_limit = dev->rx_fifo_depth - flr;

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

				regmap_write(dev->map, DW_IC_DATA_CMD,
					     cmd | 0x100);
				rx_limit--;
				dev->rx_outstanding++;
			} else {
				regmap_write(dev->map, DW_IC_DATA_CMD,
					     cmd | *buf++);
			}
			tx_limit--; buf_len--;
		}

		dev->tx_buf = buf;
		dev->tx_buf_len = buf_len;

		/*
		 * Because we don't know the buffer length in the
		 * I2C_FUNC_SMBUS_BLOCK_DATA case, we can't stop the
		 * transaction here. Also disable the TX_EMPTY IRQ
		 * while waiting for the data length byte to avoid the
		 * bogus interrupts flood.
		 */
		if (flags & I2C_M_RECV_LEN) {
			dev->status |= STATUS_WRITE_IN_PROGRESS;
			intr_mask &= ~DW_IC_INTR_TX_EMPTY;
			break;
		} else if (buf_len > 0) {
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

	__i2c_dw_write_intr_mask(dev, intr_mask);
}

static u8
i2c_dw_recv_len(struct dw_i2c_dev *dev, u8 len)
{
	struct i2c_msg *msgs = dev->msgs;
	u32 flags = msgs[dev->msg_read_idx].flags;
	unsigned int intr_mask;

	/*
	 * Adjust the buffer length and mask the flag
	 * after receiving the first byte.
	 */
	len += (flags & I2C_CLIENT_PEC) ? 2 : 1;
	dev->tx_buf_len = len - min_t(u8, len, dev->rx_outstanding);
	msgs[dev->msg_read_idx].len = len;
	msgs[dev->msg_read_idx].flags &= ~I2C_M_RECV_LEN;

	/*
	 * Received buffer length, re-enable TX_EMPTY interrupt
	 * to resume the SMBUS transaction.
	 */
	__i2c_dw_read_intr_mask(dev, &intr_mask);
	intr_mask |= DW_IC_INTR_TX_EMPTY;
	__i2c_dw_write_intr_mask(dev, intr_mask);

	return len;
}

static void
i2c_dw_read(struct dw_i2c_dev *dev)
{
	struct i2c_msg *msgs = dev->msgs;
	unsigned int rx_valid;

	for (; dev->msg_read_idx < dev->msgs_num; dev->msg_read_idx++) {
		unsigned int tmp;
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

		regmap_read(dev->map, DW_IC_RXFLR, &rx_valid);

		for (; len > 0 && rx_valid > 0; len--, rx_valid--) {
			u32 flags = msgs[dev->msg_read_idx].flags;

			regmap_read(dev->map, DW_IC_DATA_CMD, &tmp);
			tmp &= DW_IC_DATA_CMD_DAT;
			/* Ensure length byte is a valid value */
			if (flags & I2C_M_RECV_LEN) {
				/*
				 * if IC_EMPTYFIFO_HOLD_MASTER_EN is set, which cannot be
				 * detected from the registers, the controller can be
				 * disabled if the STOP bit is set. But it is only set
				 * after receiving block data response length in
				 * I2C_FUNC_SMBUS_BLOCK_DATA case. That needs to read
				 * another byte with STOP bit set when the block data
				 * response length is invalid to complete the transaction.
				 */
				if (!tmp || tmp > I2C_SMBUS_BLOCK_MAX)
					tmp = 1;

				len = i2c_dw_recv_len(dev, tmp);
			}
			*buf++ = tmp;
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

static u32 i2c_dw_read_clear_intrbits(struct dw_i2c_dev *dev)
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
	if (!(dev->flags & ACCESS_POLLING)) {
		regmap_read(dev->map, DW_IC_INTR_STAT, &stat);
	} else {
		regmap_read(dev->map, DW_IC_RAW_INTR_STAT, &stat);
		stat &= dev->sw_mask;
	}

	/*
	 * Do not use the IC_CLR_INTR register to clear interrupts, or
	 * you'll miss some interrupts, triggered during the period from
	 * readl(IC_INTR_STAT) to readl(IC_CLR_INTR).
	 *
	 * Instead, use the separately-prepared IC_CLR_* registers.
	 */
	if (stat & DW_IC_INTR_RX_UNDER)
		regmap_read(dev->map, DW_IC_CLR_RX_UNDER, &dummy);
	if (stat & DW_IC_INTR_RX_OVER)
		regmap_read(dev->map, DW_IC_CLR_RX_OVER, &dummy);
	if (stat & DW_IC_INTR_TX_OVER)
		regmap_read(dev->map, DW_IC_CLR_TX_OVER, &dummy);
	if (stat & DW_IC_INTR_RD_REQ)
		regmap_read(dev->map, DW_IC_CLR_RD_REQ, &dummy);
	if (stat & DW_IC_INTR_TX_ABRT) {
		/*
		 * The IC_TX_ABRT_SOURCE register is cleared whenever
		 * the IC_CLR_TX_ABRT is read.  Preserve it beforehand.
		 */
		regmap_read(dev->map, DW_IC_TX_ABRT_SOURCE, &dev->abort_source);
		regmap_read(dev->map, DW_IC_CLR_TX_ABRT, &dummy);
	}
	if (stat & DW_IC_INTR_RX_DONE)
		regmap_read(dev->map, DW_IC_CLR_RX_DONE, &dummy);
	if (stat & DW_IC_INTR_ACTIVITY)
		regmap_read(dev->map, DW_IC_CLR_ACTIVITY, &dummy);
	if ((stat & DW_IC_INTR_STOP_DET) &&
	    ((dev->rx_outstanding == 0) || (stat & DW_IC_INTR_RX_FULL)))
		regmap_read(dev->map, DW_IC_CLR_STOP_DET, &dummy);
	if (stat & DW_IC_INTR_START_DET)
		regmap_read(dev->map, DW_IC_CLR_START_DET, &dummy);
	if (stat & DW_IC_INTR_GEN_CALL)
		regmap_read(dev->map, DW_IC_CLR_GEN_CALL, &dummy);

	return stat;
}

static void i2c_dw_process_transfer(struct dw_i2c_dev *dev, unsigned int stat)
{
	if (stat & DW_IC_INTR_TX_ABRT) {
		dev->cmd_err |= DW_IC_ERR_TX_ABRT;
		dev->status &= ~STATUS_MASK;
		dev->rx_outstanding = 0;

		/*
		 * Anytime TX_ABRT is set, the contents of the tx/rx
		 * buffers are flushed. Make sure to skip them.
		 */
		__i2c_dw_write_intr_mask(dev, 0);
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
	if (((stat & (DW_IC_INTR_TX_ABRT | DW_IC_INTR_STOP_DET)) || dev->msg_err) &&
	     (dev->rx_outstanding == 0))
		complete(&dev->cmd_complete);
	else if (unlikely(dev->flags & ACCESS_INTR_MASK)) {
		/* Workaround to trigger pending interrupt */
		__i2c_dw_read_intr_mask(dev, &stat);
		__i2c_dw_write_intr_mask(dev, 0);
		__i2c_dw_write_intr_mask(dev, stat);
	}
}

/*
 * Interrupt service routine. This gets called whenever an I2C master interrupt
 * occurs.
 */
static irqreturn_t i2c_dw_isr(int this_irq, void *dev_id)
{
	struct dw_i2c_dev *dev = dev_id;
	unsigned int stat, enabled;

	regmap_read(dev->map, DW_IC_ENABLE, &enabled);
	regmap_read(dev->map, DW_IC_RAW_INTR_STAT, &stat);
	if (!enabled || !(stat & ~DW_IC_INTR_ACTIVITY))
		return IRQ_NONE;
	if (pm_runtime_suspended(dev->dev) || stat == GENMASK(31, 0))
		return IRQ_NONE;
	dev_dbg(dev->dev, "enabled=%#x stat=%#x\n", enabled, stat);

	stat = i2c_dw_read_clear_intrbits(dev);

	if (!(dev->status & STATUS_ACTIVE)) {
		/*
		 * Unexpected interrupt in driver point of view. State
		 * variables are either unset or stale so acknowledge and
		 * disable interrupts for suppressing further interrupts if
		 * interrupt really came from this HW (E.g. firmware has left
		 * the HW active).
		 */
		__i2c_dw_write_intr_mask(dev, 0);
		return IRQ_HANDLED;
	}

	i2c_dw_process_transfer(dev, stat);

	return IRQ_HANDLED;
}

static int i2c_dw_wait_transfer(struct dw_i2c_dev *dev)
{
	unsigned long timeout = dev->adapter.timeout;
	unsigned int stat;
	int ret;

	if (!(dev->flags & ACCESS_POLLING)) {
		ret = wait_for_completion_timeout(&dev->cmd_complete, timeout);
	} else {
		timeout += jiffies;
		do {
			ret = try_wait_for_completion(&dev->cmd_complete);
			if (ret)
				break;

			stat = i2c_dw_read_clear_intrbits(dev);
			if (stat)
				i2c_dw_process_transfer(dev, stat);
			else
				/* Try save some power */
				usleep_range(3, 25);
		} while (time_before(jiffies, timeout));
	}

	return ret ? 0 : -ETIMEDOUT;
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

	switch (dev->flags & MODEL_MASK) {
	case MODEL_AMD_NAVI_GPU:
		ret = amd_i2c_dw_xfer_quirk(adap, msgs, num);
		goto done_nolock;
	default:
		break;
	}

	reinit_completion(&dev->cmd_complete);
	dev->msgs = msgs;
	dev->msgs_num = num;
	dev->cmd_err = 0;
	dev->msg_write_idx = 0;
	dev->msg_read_idx = 0;
	dev->msg_err = 0;
	dev->status = 0;
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
	ret = i2c_dw_wait_transfer(dev);
	if (ret) {
		dev_err(dev->dev, "controller timed out\n");
		/* i2c_dw_init_master() implicitly disables the adapter */
		i2c_recover_bus(&dev->adapter);
		i2c_dw_init_master(dev);
		goto done;
	}

	/*
	 * This happens rarely (~1:500) and is hard to reproduce. Debug trace
	 * showed that IC_STATUS had value of 0x23 when STOP_DET occurred,
	 * if disable IC_ENABLE.ENABLE immediately that can result in
	 * IC_RAW_INTR_STAT.MASTER_ON_HOLD holding SCL low. Check if
	 * controller is still ACTIVE before disabling I2C.
	 */
	if (i2c_dw_is_controller_active(dev))
		dev_err(dev->dev, "controller active\n");

	/*
	 * We must disable the adapter before returning and signaling the end
	 * of the current transfer. Otherwise the hardware might continue
	 * generating interrupts which in turn causes a race condition with
	 * the following transfer. Needs some more investigation if the
	 * additional interrupts are a hardware bug or this driver doesn't
	 * handle them correctly yet.
	 */
	__i2c_dw_disable_nowait(dev);

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
	.xfer = i2c_dw_xfer,
	.functionality = i2c_dw_func,
};

static const struct i2c_adapter_quirks i2c_dw_quirks = {
	.flags = I2C_AQ_NO_ZERO_LEN,
};

void i2c_dw_configure_master(struct dw_i2c_dev *dev)
{
	struct i2c_timings *t = &dev->timings;

	dev->functionality = I2C_FUNC_10BIT_ADDR | DW_IC_DEFAULT_FUNCTIONALITY;

	dev->master_cfg = DW_IC_CON_MASTER | DW_IC_CON_SLAVE_DISABLE |
			  DW_IC_CON_RESTART_EN;

	dev->mode = DW_IC_MASTER;

	switch (t->bus_freq_hz) {
	case I2C_MAX_STANDARD_MODE_FREQ:
		dev->master_cfg |= DW_IC_CON_SPEED_STD;
		break;
	case I2C_MAX_HIGH_SPEED_MODE_FREQ:
		dev->master_cfg |= DW_IC_CON_SPEED_HIGH;
		break;
	default:
		dev->master_cfg |= DW_IC_CON_SPEED_FAST;
	}
}
EXPORT_SYMBOL_GPL(i2c_dw_configure_master);

static void i2c_dw_prepare_recovery(struct i2c_adapter *adap)
{
	struct dw_i2c_dev *dev = i2c_get_adapdata(adap);

	i2c_dw_disable(dev);
	reset_control_assert(dev->rst);
	i2c_dw_prepare_clk(dev, false);
}

static void i2c_dw_unprepare_recovery(struct i2c_adapter *adap)
{
	struct dw_i2c_dev *dev = i2c_get_adapdata(adap);

	i2c_dw_prepare_clk(dev, true);
	reset_control_deassert(dev->rst);
	i2c_dw_init_master(dev);
}

static int i2c_dw_init_recovery_info(struct dw_i2c_dev *dev)
{
	struct i2c_bus_recovery_info *rinfo = &dev->rinfo;
	struct i2c_adapter *adap = &dev->adapter;
	struct gpio_desc *gpio;

	gpio = devm_gpiod_get_optional(dev->dev, "scl", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(gpio))
		return PTR_ERR_OR_ZERO(gpio);

	rinfo->scl_gpiod = gpio;

	gpio = devm_gpiod_get_optional(dev->dev, "sda", GPIOD_IN);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);
	rinfo->sda_gpiod = gpio;

	rinfo->pinctrl = devm_pinctrl_get(dev->dev);
	if (IS_ERR(rinfo->pinctrl)) {
		if (PTR_ERR(rinfo->pinctrl) == -EPROBE_DEFER)
			return PTR_ERR(rinfo->pinctrl);

		rinfo->pinctrl = NULL;
		dev_err(dev->dev, "getting pinctrl info failed: bus recovery might not work\n");
	} else if (!rinfo->pinctrl) {
		dev_dbg(dev->dev, "pinctrl is disabled, bus recovery might not work\n");
	}

	rinfo->recover_bus = i2c_generic_scl_recovery;
	rinfo->prepare_recovery = i2c_dw_prepare_recovery;
	rinfo->unprepare_recovery = i2c_dw_unprepare_recovery;
	adap->bus_recovery_info = rinfo;

	dev_info(dev->dev, "running with GPIO recovery mode! scl%s",
		 rinfo->sda_gpiod ? ",sda" : "");

	return 0;
}

int i2c_dw_probe_master(struct dw_i2c_dev *dev)
{
	struct i2c_adapter *adap = &dev->adapter;
	unsigned long irq_flags;
	unsigned int ic_con;
	int ret;

	init_completion(&dev->cmd_complete);

	dev->init = i2c_dw_init_master;

	ret = i2c_dw_init_regmap(dev);
	if (ret)
		return ret;

	ret = i2c_dw_set_timings_master(dev);
	if (ret)
		return ret;

	ret = i2c_dw_set_fifo_size(dev);
	if (ret)
		return ret;

	/* Lock the bus for accessing DW_IC_CON */
	ret = i2c_dw_acquire_lock(dev);
	if (ret)
		return ret;

	/*
	 * On AMD platforms BIOS advertises the bus clear feature
	 * and enables the SCL/SDA stuck low. SMU FW does the
	 * bus recovery process. Driver should not ignore this BIOS
	 * advertisement of bus clear feature.
	 */
	ret = regmap_read(dev->map, DW_IC_CON, &ic_con);
	i2c_dw_release_lock(dev);
	if (ret)
		return ret;

	if (ic_con & DW_IC_CON_BUS_CLEAR_CTRL)
		dev->master_cfg |= DW_IC_CON_BUS_CLEAR_CTRL;

	ret = dev->init(dev);
	if (ret)
		return ret;

	if (!adap->name[0])
		scnprintf(adap->name, sizeof(adap->name),
			  "Synopsys DesignWare I2C adapter");
	adap->retries = 3;
	adap->algo = &i2c_dw_algo;
	adap->quirks = &i2c_dw_quirks;
	adap->dev.parent = dev->dev;
	i2c_set_adapdata(adap, dev);

	if (dev->flags & ACCESS_NO_IRQ_SUSPEND) {
		irq_flags = IRQF_NO_SUSPEND;
	} else {
		irq_flags = IRQF_SHARED | IRQF_COND_SUSPEND;
	}

	ret = i2c_dw_acquire_lock(dev);
	if (ret)
		return ret;

	__i2c_dw_write_intr_mask(dev, 0);
	i2c_dw_release_lock(dev);

	if (!(dev->flags & ACCESS_POLLING)) {
		ret = devm_request_irq(dev->dev, dev->irq, i2c_dw_isr,
				       irq_flags, dev_name(dev->dev), dev);
		if (ret) {
			dev_err(dev->dev, "failure requesting irq %i: %d\n",
				dev->irq, ret);
			return ret;
		}
	}

	ret = i2c_dw_init_recovery_info(dev);
	if (ret)
		return ret;

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
EXPORT_SYMBOL_GPL(i2c_dw_probe_master);

MODULE_DESCRIPTION("Synopsys DesignWare I2C bus master adapter");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("I2C_DW_COMMON");
