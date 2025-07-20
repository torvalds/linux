/*
 * (C) Copyright 2009-2010
 * Nokia Siemens Networks, michael.lawnick.ext@nsn.com
 *
 * Portions Copyright (C) 2010 - 2016 Cavium, Inc.
 *
 * This file contains the shared part of the driver for the i2c adapter in
 * Cavium Networks' OCTEON processors and ThunderX SOCs.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "i2c-octeon-core.h"

#define INITIAL_DELTA_HZ		1000000
#define TWSI_MASTER_CLK_REG_DEF_VAL	0x18
#define TWSI_MASTER_CLK_REG_OTX2_VAL	0x3

/* interrupt service routine */
irqreturn_t octeon_i2c_isr(int irq, void *dev_id)
{
	struct octeon_i2c *i2c = dev_id;

	i2c->int_disable(i2c);
	wake_up(&i2c->queue);

	return IRQ_HANDLED;
}

static bool octeon_i2c_test_iflg(struct octeon_i2c *i2c)
{
	return (octeon_i2c_ctl_read(i2c) & TWSI_CTL_IFLG);
}

/**
 * octeon_i2c_wait - wait for the IFLG to be set
 * @i2c: The struct octeon_i2c
 *
 * Returns: 0 on success, otherwise a negative errno.
 */
static int octeon_i2c_wait(struct octeon_i2c *i2c)
{
	long time_left;

	/*
	 * Some chip revisions don't assert the irq in the interrupt
	 * controller. So we must poll for the IFLG change.
	 */
	if (i2c->broken_irq_mode) {
		u64 end = get_jiffies_64() + i2c->adap.timeout;

		while (!octeon_i2c_test_iflg(i2c) &&
		       time_before64(get_jiffies_64(), end))
			usleep_range(I2C_OCTEON_EVENT_WAIT / 2, I2C_OCTEON_EVENT_WAIT);

		return octeon_i2c_test_iflg(i2c) ? 0 : -ETIMEDOUT;
	}

	i2c->int_enable(i2c);
	time_left = wait_event_timeout(i2c->queue, octeon_i2c_test_iflg(i2c),
				       i2c->adap.timeout);
	i2c->int_disable(i2c);

	if (i2c->broken_irq_check && !time_left &&
	    octeon_i2c_test_iflg(i2c)) {
		dev_err(i2c->dev, "broken irq connection detected, switching to polling mode.\n");
		i2c->broken_irq_mode = true;
		return 0;
	}

	if (!time_left)
		return -ETIMEDOUT;

	return 0;
}

static bool octeon_i2c_hlc_test_valid(struct octeon_i2c *i2c)
{
	return (__raw_readq(i2c->twsi_base + OCTEON_REG_SW_TWSI(i2c)) & SW_TWSI_V) == 0;
}

static void octeon_i2c_hlc_int_clear(struct octeon_i2c *i2c)
{
	/* clear ST/TS events, listen for neither */
	octeon_i2c_write_int(i2c, TWSI_INT_ST_INT | TWSI_INT_TS_INT);
}

/*
 * Cleanup low-level state & enable high-level controller.
 */
static void octeon_i2c_hlc_enable(struct octeon_i2c *i2c)
{
	int try = 0;
	u64 val;

	if (i2c->hlc_enabled)
		return;
	i2c->hlc_enabled = true;

	while (1) {
		val = octeon_i2c_ctl_read(i2c);
		if (!(val & (TWSI_CTL_STA | TWSI_CTL_STP)))
			break;

		/* clear IFLG event */
		if (val & TWSI_CTL_IFLG)
			octeon_i2c_ctl_write(i2c, TWSI_CTL_ENAB);

		if (try++ > 100) {
			pr_err("%s: giving up\n", __func__);
			break;
		}

		/* spin until any start/stop has finished */
		udelay(10);
	}
	octeon_i2c_ctl_write(i2c, TWSI_CTL_CE | TWSI_CTL_AAK | TWSI_CTL_ENAB);
}

static void octeon_i2c_hlc_disable(struct octeon_i2c *i2c)
{
	if (!i2c->hlc_enabled)
		return;

	i2c->hlc_enabled = false;
	octeon_i2c_ctl_write(i2c, TWSI_CTL_ENAB);
}

static void octeon_i2c_block_enable(struct octeon_i2c *i2c)
{
	u64 mode;

	if (i2c->block_enabled || !OCTEON_REG_BLOCK_CTL(i2c))
		return;

	i2c->block_enabled = true;
	mode = __raw_readq(i2c->twsi_base + OCTEON_REG_MODE(i2c));
	mode |= TWSX_MODE_BLOCK_MODE;
	octeon_i2c_writeq_flush(mode, i2c->twsi_base + OCTEON_REG_MODE(i2c));
}

static void octeon_i2c_block_disable(struct octeon_i2c *i2c)
{
	u64 mode;

	if (!i2c->block_enabled || !OCTEON_REG_BLOCK_CTL(i2c))
		return;

	i2c->block_enabled = false;
	mode = __raw_readq(i2c->twsi_base + OCTEON_REG_MODE(i2c));
	mode &= ~TWSX_MODE_BLOCK_MODE;
	octeon_i2c_writeq_flush(mode, i2c->twsi_base + OCTEON_REG_MODE(i2c));
}

/**
 * octeon_i2c_hlc_wait - wait for an HLC operation to complete
 * @i2c: The struct octeon_i2c
 *
 * Returns: 0 on success, otherwise -ETIMEDOUT.
 */
static int octeon_i2c_hlc_wait(struct octeon_i2c *i2c)
{
	int time_left;

	/*
	 * Some cn38xx boards don't assert the irq in the interrupt
	 * controller. So we must poll for the valid bit change.
	 */
	if (i2c->broken_irq_mode) {
		u64 end = get_jiffies_64() + i2c->adap.timeout;

		while (!octeon_i2c_hlc_test_valid(i2c) &&
		       time_before64(get_jiffies_64(), end))
			usleep_range(I2C_OCTEON_EVENT_WAIT / 2, I2C_OCTEON_EVENT_WAIT);

		return octeon_i2c_hlc_test_valid(i2c) ? 0 : -ETIMEDOUT;
	}

	i2c->hlc_int_enable(i2c);
	time_left = wait_event_timeout(i2c->queue,
				       octeon_i2c_hlc_test_valid(i2c),
				       i2c->adap.timeout);
	i2c->hlc_int_disable(i2c);
	if (!time_left)
		octeon_i2c_hlc_int_clear(i2c);

	if (i2c->broken_irq_check && !time_left &&
	    octeon_i2c_hlc_test_valid(i2c)) {
		dev_err(i2c->dev, "broken irq connection detected, switching to polling mode.\n");
		i2c->broken_irq_mode = true;
		return 0;
	}

	if (!time_left)
		return -ETIMEDOUT;
	return 0;
}

static int octeon_i2c_check_status(struct octeon_i2c *i2c, int final_read)
{
	u8 stat;
	u64 mode;

	/*
	 * This is ugly... in HLC mode the status is not in the status register
	 * but in the lower 8 bits of OCTEON_REG_SW_TWSI.
	 */
	if (i2c->hlc_enabled)
		stat = __raw_readq(i2c->twsi_base + OCTEON_REG_SW_TWSI(i2c));
	else
		stat = octeon_i2c_stat_read(i2c);

	switch (stat) {
	/* Everything is fine */
	case STAT_IDLE:
	case STAT_AD2W_ACK:
	case STAT_RXADDR_ACK:
	case STAT_TXADDR_ACK:
	case STAT_TXDATA_ACK:
		return 0;

	/* ACK allowed on pre-terminal bytes only */
	case STAT_RXDATA_ACK:
		if (!final_read)
			return 0;
		return -EIO;

	/* NAK allowed on terminal byte only */
	case STAT_RXDATA_NAK:
		if (final_read)
			return 0;
		return -EIO;

	/* Arbitration lost */
	case STAT_LOST_ARB_38:
	case STAT_LOST_ARB_68:
	case STAT_LOST_ARB_78:
	case STAT_LOST_ARB_B0:
		return -EAGAIN;

	/* Being addressed as local target, should back off & listen */
	case STAT_SLAVE_60:
	case STAT_SLAVE_70:
	case STAT_GENDATA_ACK:
	case STAT_GENDATA_NAK:
		return -EOPNOTSUPP;

	/* Core busy as local target */
	case STAT_SLAVE_80:
	case STAT_SLAVE_88:
	case STAT_SLAVE_A0:
	case STAT_SLAVE_A8:
	case STAT_SLAVE_LOST:
	case STAT_SLAVE_NAK:
	case STAT_SLAVE_ACK:
		return -EOPNOTSUPP;

	case STAT_TXDATA_NAK:
	case STAT_BUS_ERROR:
		return -EIO;
	case STAT_TXADDR_NAK:
	case STAT_RXADDR_NAK:
	case STAT_AD2W_NAK:
		return -ENXIO;

	case STAT_WDOG_TOUT:
		mode = __raw_readq(i2c->twsi_base + OCTEON_REG_MODE(i2c));
		/* Set BUS_MON_RST to reset bus monitor */
		mode |= BUS_MON_RST_MASK;
		octeon_i2c_writeq_flush(mode, i2c->twsi_base + OCTEON_REG_MODE(i2c));
		return -EIO;
	default:
		dev_err(i2c->dev, "unhandled state: %d\n", stat);
		return -EIO;
	}
}

static int octeon_i2c_recovery(struct octeon_i2c *i2c)
{
	int ret;

	ret = i2c_recover_bus(&i2c->adap);
	if (ret)
		/* recover failed, try hardware re-init */
		ret = octeon_i2c_init_lowlevel(i2c);
	return ret;
}

/**
 * octeon_i2c_start - send START to the bus
 * @i2c: The struct octeon_i2c
 *
 * Returns: 0 on success, otherwise a negative errno.
 */
static int octeon_i2c_start(struct octeon_i2c *i2c)
{
	int ret;
	u8 stat;

	octeon_i2c_hlc_disable(i2c);
	octeon_i2c_block_disable(i2c);

	octeon_i2c_ctl_write(i2c, TWSI_CTL_ENAB | TWSI_CTL_STA);
	ret = octeon_i2c_wait(i2c);
	if (ret)
		goto error;

	stat = octeon_i2c_stat_read(i2c);
	if (stat == STAT_START || stat == STAT_REP_START)
		/* START successful, bail out */
		return 0;

error:
	/* START failed, try to recover */
	ret = octeon_i2c_recovery(i2c);
	return (ret) ? ret : -EAGAIN;
}

/* send STOP to the bus */
static void octeon_i2c_stop(struct octeon_i2c *i2c)
{
	octeon_i2c_ctl_write(i2c, TWSI_CTL_ENAB | TWSI_CTL_STP);
}

/**
 * octeon_i2c_read - receive data from the bus via low-level controller
 * @i2c: The struct octeon_i2c
 * @target: Target address
 * @data: Pointer to the location to store the data
 * @rlength: Length of the data
 * @recv_len: flag for length byte
 *
 * The address is sent over the bus, then the data is read.
 *
 * Returns: 0 on success, otherwise a negative errno.
 */
static int octeon_i2c_read(struct octeon_i2c *i2c, int target,
			   u8 *data, u16 *rlength, bool recv_len)
{
	int i, result, length = *rlength;
	bool final_read = false;

	octeon_i2c_data_write(i2c, (target << 1) | 1);
	octeon_i2c_ctl_write(i2c, TWSI_CTL_ENAB);

	result = octeon_i2c_wait(i2c);
	if (result)
		return result;

	/* address OK ? */
	result = octeon_i2c_check_status(i2c, false);
	if (result)
		return result;

	for (i = 0; i < length; i++) {
		/*
		 * For the last byte to receive TWSI_CTL_AAK must not be set.
		 *
		 * A special case is I2C_M_RECV_LEN where we don't know the
		 * additional length yet. If recv_len is set we assume we're
		 * not reading the final byte and therefore need to set
		 * TWSI_CTL_AAK.
		 */
		if ((i + 1 == length) && !(recv_len && i == 0))
			final_read = true;

		/* clear iflg to allow next event */
		if (final_read)
			octeon_i2c_ctl_write(i2c, TWSI_CTL_ENAB);
		else
			octeon_i2c_ctl_write(i2c, TWSI_CTL_ENAB | TWSI_CTL_AAK);

		result = octeon_i2c_wait(i2c);
		if (result)
			return result;

		data[i] = octeon_i2c_data_read(i2c, &result);
		if (result)
			return result;
		if (recv_len && i == 0) {
			if (data[i] > I2C_SMBUS_BLOCK_MAX)
				return -EPROTO;
			length += data[i];
		}

		result = octeon_i2c_check_status(i2c, final_read);
		if (result)
			return result;
	}
	*rlength = length;
	return 0;
}

/**
 * octeon_i2c_write - send data to the bus via low-level controller
 * @i2c: The struct octeon_i2c
 * @target: Target address
 * @data: Pointer to the data to be sent
 * @length: Length of the data
 *
 * The address is sent over the bus, then the data.
 *
 * Returns: 0 on success, otherwise a negative errno.
 */
static int octeon_i2c_write(struct octeon_i2c *i2c, int target,
			    const u8 *data, int length)
{
	int i, result;

	octeon_i2c_data_write(i2c, target << 1);
	octeon_i2c_ctl_write(i2c, TWSI_CTL_ENAB);

	result = octeon_i2c_wait(i2c);
	if (result)
		return result;

	for (i = 0; i < length; i++) {
		result = octeon_i2c_check_status(i2c, false);
		if (result)
			return result;

		octeon_i2c_data_write(i2c, data[i]);
		octeon_i2c_ctl_write(i2c, TWSI_CTL_ENAB);

		result = octeon_i2c_wait(i2c);
		if (result)
			return result;
	}

	return 0;
}

/* high-level-controller pure read of up to 8 bytes */
static int octeon_i2c_hlc_read(struct octeon_i2c *i2c, struct i2c_msg *msgs)
{
	int i, j, ret = 0;
	u64 cmd;

	octeon_i2c_hlc_enable(i2c);
	octeon_i2c_hlc_int_clear(i2c);

	cmd = SW_TWSI_V | SW_TWSI_R | SW_TWSI_SOVR | SW_TWSI_OP_7;
	/* SIZE */
	cmd |= (u64)(msgs[0].len - 1) << SW_TWSI_SIZE_SHIFT;
	/* A */
	cmd |= (u64)(msgs[0].addr & 0x7full) << SW_TWSI_ADDR_SHIFT;

	octeon_i2c_writeq_flush(cmd, i2c->twsi_base + OCTEON_REG_SW_TWSI(i2c));
	ret = octeon_i2c_hlc_wait(i2c);
	if (ret)
		goto err;

	cmd = __raw_readq(i2c->twsi_base + OCTEON_REG_SW_TWSI(i2c));
	if ((cmd & SW_TWSI_R) == 0)
		return octeon_i2c_check_status(i2c, false);

	for (i = 0, j = msgs[0].len - 1; i  < msgs[0].len && i < 4; i++, j--)
		msgs[0].buf[j] = (cmd >> (8 * i)) & 0xff;

	if (msgs[0].len > 4) {
		cmd = __raw_readq(i2c->twsi_base + OCTEON_REG_SW_TWSI_EXT(i2c));
		for (i = 0; i  < msgs[0].len - 4 && i < 4; i++, j--)
			msgs[0].buf[j] = (cmd >> (8 * i)) & 0xff;
	}

err:
	return ret;
}

/* high-level-controller pure write of up to 8 bytes */
static int octeon_i2c_hlc_write(struct octeon_i2c *i2c, struct i2c_msg *msgs)
{
	int i, j, ret = 0;
	u64 cmd;

	octeon_i2c_hlc_enable(i2c);
	octeon_i2c_hlc_int_clear(i2c);

	cmd = SW_TWSI_V | SW_TWSI_SOVR | SW_TWSI_OP_7;
	/* SIZE */
	cmd |= (u64)(msgs[0].len - 1) << SW_TWSI_SIZE_SHIFT;
	/* A */
	cmd |= (u64)(msgs[0].addr & 0x7full) << SW_TWSI_ADDR_SHIFT;

	for (i = 0, j = msgs[0].len - 1; i  < msgs[0].len && i < 4; i++, j--)
		cmd |= (u64)msgs[0].buf[j] << (8 * i);

	if (msgs[0].len > 4) {
		u64 ext = 0;

		for (i = 0; i < msgs[0].len - 4 && i < 4; i++, j--)
			ext |= (u64)msgs[0].buf[j] << (8 * i);
		octeon_i2c_writeq_flush(ext, i2c->twsi_base + OCTEON_REG_SW_TWSI_EXT(i2c));
	}

	octeon_i2c_writeq_flush(cmd, i2c->twsi_base + OCTEON_REG_SW_TWSI(i2c));
	ret = octeon_i2c_hlc_wait(i2c);
	if (ret)
		goto err;

	cmd = __raw_readq(i2c->twsi_base + OCTEON_REG_SW_TWSI(i2c));
	if ((cmd & SW_TWSI_R) == 0)
		return octeon_i2c_check_status(i2c, false);

err:
	return ret;
}

/* Process hlc transaction */
static int octeon_i2c_hlc_cmd_send(struct octeon_i2c *i2c, u64 cmd)
{
	octeon_i2c_hlc_int_clear(i2c);
	octeon_i2c_writeq_flush(cmd, i2c->twsi_base + OCTEON_REG_SW_TWSI(i2c));

	return octeon_i2c_hlc_wait(i2c);
}

/* Generic consideration for extended internal addresses in i2c hlc r/w ops */
static bool octeon_i2c_hlc_ext(struct octeon_i2c *i2c, struct i2c_msg msg, u64 *cmd_in, u64 *ext)
{
	bool set_ext = false;
	u64 cmd = 0;

	if (msg.len == 2) {
		cmd |= SW_TWSI_EIA;
		*ext = (u64)msg.buf[0] << SW_TWSI_IA_SHIFT;
		cmd |= (u64)msg.buf[1] << SW_TWSI_IA_SHIFT;
		set_ext = true;
	} else {
		cmd |= (u64)msg.buf[0] << SW_TWSI_IA_SHIFT;
	}

	*cmd_in |= cmd;
	return set_ext;
}

/* Construct and send i2c transaction core cmd for read ops */
static int octeon_i2c_hlc_read_cmd(struct octeon_i2c *i2c, struct i2c_msg msg, u64 cmd)
{
	u64 ext = 0;

	if (octeon_i2c_hlc_ext(i2c, msg, &cmd, &ext))
		octeon_i2c_writeq_flush(ext, i2c->twsi_base + OCTEON_REG_SW_TWSI_EXT(i2c));

	return octeon_i2c_hlc_cmd_send(i2c, cmd);
}

/* high-level-controller composite write+read, msg0=addr, msg1=data */
static int octeon_i2c_hlc_comp_read(struct octeon_i2c *i2c, struct i2c_msg *msgs)
{
	int i, j, ret = 0;
	u64 cmd;

	octeon_i2c_hlc_enable(i2c);

	cmd = SW_TWSI_V | SW_TWSI_R | SW_TWSI_SOVR | SW_TWSI_OP_7_IA;
	/* SIZE */
	cmd |= (u64)(msgs[1].len - 1) << SW_TWSI_SIZE_SHIFT;
	/* A */
	cmd |= (u64)(msgs[0].addr & 0x7full) << SW_TWSI_ADDR_SHIFT;

	/* Send core command */
	ret = octeon_i2c_hlc_read_cmd(i2c, msgs[0], cmd);
	if (ret)
		goto err;

	cmd = __raw_readq(i2c->twsi_base + OCTEON_REG_SW_TWSI(i2c));
	if ((cmd & SW_TWSI_R) == 0)
		return octeon_i2c_check_status(i2c, false);

	for (i = 0, j = msgs[1].len - 1; i  < msgs[1].len && i < 4; i++, j--)
		msgs[1].buf[j] = (cmd >> (8 * i)) & 0xff;

	if (msgs[1].len > 4) {
		cmd = __raw_readq(i2c->twsi_base + OCTEON_REG_SW_TWSI_EXT(i2c));
		for (i = 0; i  < msgs[1].len - 4 && i < 4; i++, j--)
			msgs[1].buf[j] = (cmd >> (8 * i)) & 0xff;
	}

err:
	return ret;
}

/* high-level-controller composite write+write, m[0]len<=2, m[1]len<=8 */
static int octeon_i2c_hlc_comp_write(struct octeon_i2c *i2c, struct i2c_msg *msgs)
{
	bool set_ext = false;
	int i, j, ret = 0;
	u64 cmd, ext = 0;

	octeon_i2c_hlc_enable(i2c);

	cmd = SW_TWSI_V | SW_TWSI_SOVR | SW_TWSI_OP_7_IA;
	/* SIZE */
	cmd |= (u64)(msgs[1].len - 1) << SW_TWSI_SIZE_SHIFT;
	/* A */
	cmd |= (u64)(msgs[0].addr & 0x7full) << SW_TWSI_ADDR_SHIFT;

	/* Set parameters for extended message (if required) */
	set_ext = octeon_i2c_hlc_ext(i2c, msgs[0], &cmd, &ext);

	for (i = 0, j = msgs[1].len - 1; i  < msgs[1].len && i < 4; i++, j--)
		cmd |= (u64)msgs[1].buf[j] << (8 * i);

	if (msgs[1].len > 4) {
		for (i = 0; i < msgs[1].len - 4 && i < 4; i++, j--)
			ext |= (u64)msgs[1].buf[j] << (8 * i);
		set_ext = true;
	}
	if (set_ext)
		octeon_i2c_writeq_flush(ext, i2c->twsi_base + OCTEON_REG_SW_TWSI_EXT(i2c));

	ret = octeon_i2c_hlc_cmd_send(i2c, cmd);
	if (ret)
		goto err;

	cmd = __raw_readq(i2c->twsi_base + OCTEON_REG_SW_TWSI(i2c));
	if ((cmd & SW_TWSI_R) == 0)
		return octeon_i2c_check_status(i2c, false);

err:
	return ret;
}

/**
 * octeon_i2c_hlc_block_comp_read - high-level-controller composite block read
 * @i2c: The struct octeon_i2c
 * @msgs: msg[0] contains address, place read data into msg[1]
 *
 * i2c core command is constructed and written into the SW_TWSI register.
 * The execution of the command will result in requested data being
 * placed into a FIFO buffer, ready to be read.
 * Used in the case where the i2c xfer is for greater than 8 bytes of read data.
 *
 * Returns: 0 on success, otherwise a negative errno.
 */
static int octeon_i2c_hlc_block_comp_read(struct octeon_i2c *i2c, struct i2c_msg *msgs)
{
	int ret;
	u16 len, i;
	u64 cmd;

	octeon_i2c_hlc_enable(i2c);
	octeon_i2c_block_enable(i2c);

	/* Write (size - 1) into block control register */
	len = msgs[1].len - 1;
	octeon_i2c_writeq_flush((u64)len, i2c->twsi_base + OCTEON_REG_BLOCK_CTL(i2c));

	/* Prepare core command */
	cmd = SW_TWSI_V | SW_TWSI_R | SW_TWSI_SOVR | SW_TWSI_OP_7_IA;
	cmd |= (u64)(msgs[0].addr & 0x7full) << SW_TWSI_ADDR_SHIFT;

	/* Send core command */
	ret = octeon_i2c_hlc_read_cmd(i2c, msgs[0], cmd);
	if (ret)
		goto err;

	cmd = __raw_readq(i2c->twsi_base + OCTEON_REG_SW_TWSI(i2c));
	if ((cmd & SW_TWSI_R) == 0) {
		octeon_i2c_block_disable(i2c);
		return octeon_i2c_check_status(i2c, false);
	}

	/* read data in FIFO */
	octeon_i2c_writeq_flush(TWSX_BLOCK_STS_RESET_PTR,
				i2c->twsi_base + OCTEON_REG_BLOCK_STS(i2c));
	for (i = 0; i <= len; i += 8) {
		/* Byte-swap FIFO data and copy into msg buffer */
		__be64 rd = cpu_to_be64(__raw_readq(i2c->twsi_base + OCTEON_REG_BLOCK_FIFO(i2c)));

		memcpy(&msgs[1].buf[i], &rd, min(8, msgs[1].len - i));
	}

err:
	octeon_i2c_block_disable(i2c);
	return ret;
}

/**
 * octeon_i2c_hlc_block_comp_write - high-level-controller composite block write
 * @i2c: The struct octeon_i2c
 * @msgs: msg[0] contains address, msg[1] contains data to be written
 *
 * i2c core command is constructed and write data is written into the FIFO buffer.
 * The execution of the command will result in HW write, using the data in FIFO.
 * Used in the case where the i2c xfer is for greater than 8 bytes of write data.
 *
 * Returns: 0 on success, otherwise a negative errno.
 */
static int octeon_i2c_hlc_block_comp_write(struct octeon_i2c *i2c, struct i2c_msg *msgs)
{
	bool set_ext;
	int ret;
	u16 len, i;
	u64 cmd, ext = 0;

	octeon_i2c_hlc_enable(i2c);
	octeon_i2c_block_enable(i2c);

	/* Write (size - 1) into block control register */
	len = msgs[1].len - 1;
	octeon_i2c_writeq_flush((u64)len, i2c->twsi_base + OCTEON_REG_BLOCK_CTL(i2c));

	/* Prepare core command */
	cmd = SW_TWSI_V | SW_TWSI_SOVR | SW_TWSI_OP_7_IA;
	cmd |= (u64)(msgs[0].addr & 0x7full) << SW_TWSI_ADDR_SHIFT;

	/* Set parameters for extended message (if required) */
	set_ext = octeon_i2c_hlc_ext(i2c, msgs[0], &cmd, &ext);

	/* Write msg into FIFO buffer */
	octeon_i2c_writeq_flush(TWSX_BLOCK_STS_RESET_PTR,
				i2c->twsi_base + OCTEON_REG_BLOCK_STS(i2c));
	for (i = 0; i <= len; i += 8) {
		__be64 buf = 0;

		/* Copy 8 bytes or remaining bytes from message buffer */
		memcpy(&buf, &msgs[1].buf[i], min(8, msgs[1].len - i));

		/* Byte-swap message data and write into FIFO */
		buf = cpu_to_be64(buf);
		octeon_i2c_writeq_flush((u64)buf, i2c->twsi_base + OCTEON_REG_BLOCK_FIFO(i2c));
	}
	if (set_ext)
		octeon_i2c_writeq_flush(ext, i2c->twsi_base + OCTEON_REG_SW_TWSI_EXT(i2c));

	/* Send command to core (send data in FIFO) */
	ret = octeon_i2c_hlc_cmd_send(i2c, cmd);
	if (ret)
		goto err;

	cmd = __raw_readq(i2c->twsi_base + OCTEON_REG_SW_TWSI(i2c));
	if ((cmd & SW_TWSI_R) == 0) {
		octeon_i2c_block_disable(i2c);
		return octeon_i2c_check_status(i2c, false);
	}

err:
	octeon_i2c_block_disable(i2c);
	return ret;
}

/**
 * octeon_i2c_xfer - The driver's xfer function
 * @adap: Pointer to the i2c_adapter structure
 * @msgs: Pointer to the messages to be processed
 * @num: Length of the MSGS array
 *
 * Returns: the number of messages processed, or a negative errno on failure.
 */
int octeon_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct octeon_i2c *i2c = i2c_get_adapdata(adap);
	int i, ret = 0;

	if (IS_LS_FREQ(i2c->twsi_freq)) {
		if (num == 1) {
			if (msgs[0].len > 0 && msgs[0].len <= 8) {
				if (msgs[0].flags & I2C_M_RD)
					ret = octeon_i2c_hlc_read(i2c, msgs);
				else
					ret = octeon_i2c_hlc_write(i2c, msgs);
				goto out;
			}
		} else if (num == 2) {
			if ((msgs[0].flags & I2C_M_RD) == 0 &&
			    (msgs[1].flags & I2C_M_RECV_LEN) == 0 &&
			    msgs[0].len > 0 && msgs[0].len <= 2 &&
			    msgs[1].len > 0 &&
			    msgs[0].addr == msgs[1].addr) {
				if (msgs[1].len <= 8) {
					if (msgs[1].flags & I2C_M_RD)
						ret = octeon_i2c_hlc_comp_read(i2c, msgs);
					else
						ret = octeon_i2c_hlc_comp_write(i2c, msgs);
					goto out;
				} else if (msgs[1].len <= 1024 && OCTEON_REG_BLOCK_CTL(i2c)) {
					if (msgs[1].flags & I2C_M_RD)
						ret = octeon_i2c_hlc_block_comp_read(i2c, msgs);
					else
						ret = octeon_i2c_hlc_block_comp_write(i2c, msgs);
					goto out;
				}
			}
		}
	}

	for (i = 0; ret == 0 && i < num; i++) {
		struct i2c_msg *pmsg = &msgs[i];

		/* zero-length messages are not supported */
		if (!pmsg->len) {
			ret = -EOPNOTSUPP;
			break;
		}

		ret = octeon_i2c_start(i2c);
		if (ret)
			return ret;

		if (pmsg->flags & I2C_M_RD)
			ret = octeon_i2c_read(i2c, pmsg->addr, pmsg->buf,
					      &pmsg->len, pmsg->flags & I2C_M_RECV_LEN);
		else
			ret = octeon_i2c_write(i2c, pmsg->addr, pmsg->buf,
					       pmsg->len);
	}
	octeon_i2c_stop(i2c);
out:
	return (ret != 0) ? ret : num;
}

/* calculate and set clock divisors */
void octeon_i2c_set_clock(struct octeon_i2c *i2c)
{
	int tclk, thp_base, inc, thp_idx, mdiv_idx, ndiv_idx, foscl, diff;
	bool is_plat_otx2;
	/*
	 * Find divisors to produce target frequency, start with large delta
	 * to cover wider range of divisors, note thp = TCLK half period and
	 * ds is OSCL output frequency divisor.
	 */
	unsigned int thp, mdiv_min, mdiv = 2, ndiv = 0, ds = 10;
	unsigned int delta_hz = INITIAL_DELTA_HZ;

	is_plat_otx2 = octeon_i2c_is_otx2(to_pci_dev(i2c->dev));

	if (is_plat_otx2) {
		thp = TWSI_MASTER_CLK_REG_OTX2_VAL;
		mdiv_min = 0;
		if (!IS_LS_FREQ(i2c->twsi_freq))
			ds = 15;
	} else {
		thp = TWSI_MASTER_CLK_REG_DEF_VAL;
		mdiv_min = 2;
	}

	for (ndiv_idx = 0; ndiv_idx < 8 && delta_hz != 0; ndiv_idx++) {
		/*
		 * An mdiv value of less than 2 seems to not work well
		 * with ds1337 RTCs, so we constrain it to larger values.
		 */
		for (mdiv_idx = 15; mdiv_idx >= mdiv_min && delta_hz != 0; mdiv_idx--) {
			/*
			 * For given ndiv and mdiv values check the
			 * two closest thp values.
			 */
			tclk = i2c->twsi_freq * (mdiv_idx + 1) * ds;
			tclk *= (1 << ndiv_idx);
			if (is_plat_otx2)
				thp_base = (i2c->sys_freq / tclk) - 2;
			else
				thp_base = (i2c->sys_freq / (tclk * 2)) - 1;

			for (inc = 0; inc <= 1; inc++) {
				thp_idx = thp_base + inc;
				if (thp_idx < 5 || thp_idx > 0xff)
					continue;

				if (is_plat_otx2)
					foscl = i2c->sys_freq / (thp_idx + 2);
				else
					foscl = i2c->sys_freq /
						(2 * (thp_idx + 1));
				foscl = foscl / (1 << ndiv_idx);
				foscl = foscl / (mdiv_idx + 1) / ds;
				if (foscl > i2c->twsi_freq)
					continue;
				diff = abs(foscl - i2c->twsi_freq);
				/*
				 * Diff holds difference between calculated frequency
				 * value vs desired frequency.
				 * Delta_hz is updated with last minimum diff.
				 */
				if (diff < delta_hz) {
					delta_hz = diff;
					thp = thp_idx;
					mdiv = mdiv_idx;
					ndiv = ndiv_idx;
				}
			}
		}
	}
	octeon_i2c_reg_write(i2c, SW_TWSI_OP_TWSI_CLK, thp);
	octeon_i2c_reg_write(i2c, SW_TWSI_EOP_TWSI_CLKCTL, (mdiv << 3) | ndiv);
	if (is_plat_otx2) {
		u64 mode;

		mode = __raw_readq(i2c->twsi_base + OCTEON_REG_MODE(i2c));
		/* Set REFCLK_SRC and HS_MODE in TWSX_MODE register */
		if (!IS_LS_FREQ(i2c->twsi_freq))
			mode |= TWSX_MODE_HS_MASK;
		else
			mode &= ~TWSX_MODE_HS_MASK;
		octeon_i2c_writeq_flush(mode, i2c->twsi_base + OCTEON_REG_MODE(i2c));
	}
}

int octeon_i2c_init_lowlevel(struct octeon_i2c *i2c)
{
	u8 status = 0;
	int tries;

	/* reset controller */
	octeon_i2c_reg_write(i2c, SW_TWSI_EOP_TWSI_RST, 0);

	for (tries = 10; tries && status != STAT_IDLE; tries--) {
		udelay(1);
		status = octeon_i2c_stat_read(i2c);
		if (status == STAT_IDLE)
			break;
	}

	if (status != STAT_IDLE) {
		dev_err(i2c->dev, "%s: TWSI_RST failed! (0x%x)\n",
			__func__, status);
		return -EIO;
	}

	/* toggle twice to force both teardowns */
	octeon_i2c_hlc_enable(i2c);
	octeon_i2c_hlc_disable(i2c);
	return 0;
}

static int octeon_i2c_get_scl(struct i2c_adapter *adap)
{
	struct octeon_i2c *i2c = i2c_get_adapdata(adap);
	u64 state;

	state = octeon_i2c_read_int(i2c);
	return state & TWSI_INT_SCL;
}

static void octeon_i2c_set_scl(struct i2c_adapter *adap, int val)
{
	struct octeon_i2c *i2c = i2c_get_adapdata(adap);

	octeon_i2c_write_int(i2c, val ? 0 : TWSI_INT_SCL_OVR);
}

static int octeon_i2c_get_sda(struct i2c_adapter *adap)
{
	struct octeon_i2c *i2c = i2c_get_adapdata(adap);
	u64 state;

	state = octeon_i2c_read_int(i2c);
	return state & TWSI_INT_SDA;
}

static void octeon_i2c_prepare_recovery(struct i2c_adapter *adap)
{
	struct octeon_i2c *i2c = i2c_get_adapdata(adap);

	octeon_i2c_hlc_disable(i2c);
	octeon_i2c_reg_write(i2c, SW_TWSI_EOP_TWSI_RST, 0);
	/* wait for software reset to settle */
	udelay(5);

	/*
	 * Bring control register to a good state regardless
	 * of HLC state.
	 */
	octeon_i2c_ctl_write(i2c, TWSI_CTL_ENAB);

	octeon_i2c_write_int(i2c, 0);
}

static void octeon_i2c_unprepare_recovery(struct i2c_adapter *adap)
{
	struct octeon_i2c *i2c = i2c_get_adapdata(adap);

	/*
	 * Generate STOP to finish the unfinished transaction.
	 * Can't generate STOP via the TWSI CTL register
	 * since it could bring the TWSI controller into an inoperable state.
	 */
	octeon_i2c_write_int(i2c, TWSI_INT_SDA_OVR | TWSI_INT_SCL_OVR);
	udelay(5);
	octeon_i2c_write_int(i2c, TWSI_INT_SDA_OVR);
	udelay(5);
	octeon_i2c_write_int(i2c, 0);
}

struct i2c_bus_recovery_info octeon_i2c_recovery_info = {
	.recover_bus = i2c_generic_scl_recovery,
	.get_scl = octeon_i2c_get_scl,
	.set_scl = octeon_i2c_set_scl,
	.get_sda = octeon_i2c_get_sda,
	.prepare_recovery = octeon_i2c_prepare_recovery,
	.unprepare_recovery = octeon_i2c_unprepare_recovery,
};
