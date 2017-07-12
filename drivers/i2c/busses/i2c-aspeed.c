/*
 *  Aspeed 24XX/25XX I2C Controller.
 *
 *  Copyright (C) 2012-2017 ASPEED Technology Inc.
 *  Copyright 2017 IBM Corporation
 *  Copyright 2017 Google, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* I2C Register */
#define ASPEED_I2C_FUN_CTRL_REG				0x00
#define ASPEED_I2C_AC_TIMING_REG1			0x04
#define ASPEED_I2C_AC_TIMING_REG2			0x08
#define ASPEED_I2C_INTR_CTRL_REG			0x0c
#define ASPEED_I2C_INTR_STS_REG				0x10
#define ASPEED_I2C_CMD_REG				0x14
#define ASPEED_I2C_DEV_ADDR_REG				0x18
#define ASPEED_I2C_BYTE_BUF_REG				0x20

/* Global Register Definition */
/* 0x00 : I2C Interrupt Status Register  */
/* 0x08 : I2C Interrupt Target Assignment  */

/* Device Register Definition */
/* 0x00 : I2CD Function Control Register  */
#define ASPEED_I2CD_MULTI_MASTER_DIS			BIT(15)
#define ASPEED_I2CD_SDA_DRIVE_1T_EN			BIT(8)
#define ASPEED_I2CD_M_SDA_DRIVE_1T_EN			BIT(7)
#define ASPEED_I2CD_M_HIGH_SPEED_EN			BIT(6)
#define ASPEED_I2CD_SLAVE_EN				BIT(1)
#define ASPEED_I2CD_MASTER_EN				BIT(0)

/* 0x04 : I2CD Clock and AC Timing Control Register #1 */
#define ASPEED_I2CD_TIME_SCL_HIGH_SHIFT			16
#define ASPEED_I2CD_TIME_SCL_HIGH_MASK			GENMASK(19, 16)
#define ASPEED_I2CD_TIME_SCL_LOW_SHIFT			12
#define ASPEED_I2CD_TIME_SCL_LOW_MASK			GENMASK(15, 12)
#define ASPEED_I2CD_TIME_BASE_DIVISOR_MASK		GENMASK(3, 0)
#define ASPEED_I2CD_TIME_SCL_REG_MAX			GENMASK(3, 0)
/* 0x08 : I2CD Clock and AC Timing Control Register #2 */
#define ASPEED_NO_TIMEOUT_CTRL				0

/* 0x0c : I2CD Interrupt Control Register &
 * 0x10 : I2CD Interrupt Status Register
 *
 * These share bit definitions, so use the same values for the enable &
 * status bits.
 */
#define ASPEED_I2CD_INTR_SDA_DL_TIMEOUT			BIT(14)
#define ASPEED_I2CD_INTR_BUS_RECOVER_DONE		BIT(13)
#define ASPEED_I2CD_INTR_SLAVE_MATCH			BIT(7)
#define ASPEED_I2CD_INTR_SCL_TIMEOUT			BIT(6)
#define ASPEED_I2CD_INTR_ABNORMAL			BIT(5)
#define ASPEED_I2CD_INTR_NORMAL_STOP			BIT(4)
#define ASPEED_I2CD_INTR_ARBIT_LOSS			BIT(3)
#define ASPEED_I2CD_INTR_RX_DONE			BIT(2)
#define ASPEED_I2CD_INTR_TX_NAK				BIT(1)
#define ASPEED_I2CD_INTR_TX_ACK				BIT(0)
#define ASPEED_I2CD_INTR_ALL						       \
		(ASPEED_I2CD_INTR_SDA_DL_TIMEOUT |			       \
		 ASPEED_I2CD_INTR_BUS_RECOVER_DONE |			       \
		 ASPEED_I2CD_INTR_SCL_TIMEOUT |				       \
		 ASPEED_I2CD_INTR_ABNORMAL |				       \
		 ASPEED_I2CD_INTR_NORMAL_STOP |				       \
		 ASPEED_I2CD_INTR_ARBIT_LOSS |				       \
		 ASPEED_I2CD_INTR_RX_DONE |				       \
		 ASPEED_I2CD_INTR_TX_NAK |				       \
		 ASPEED_I2CD_INTR_TX_ACK)

/* 0x14 : I2CD Command/Status Register   */
#define ASPEED_I2CD_SCL_LINE_STS			BIT(18)
#define ASPEED_I2CD_SDA_LINE_STS			BIT(17)
#define ASPEED_I2CD_BUS_BUSY_STS			BIT(16)
#define ASPEED_I2CD_BUS_RECOVER_CMD			BIT(11)

/* Command Bit */
#define ASPEED_I2CD_M_STOP_CMD				BIT(5)
#define ASPEED_I2CD_M_S_RX_CMD_LAST			BIT(4)
#define ASPEED_I2CD_M_RX_CMD				BIT(3)
#define ASPEED_I2CD_S_TX_CMD				BIT(2)
#define ASPEED_I2CD_M_TX_CMD				BIT(1)
#define ASPEED_I2CD_M_START_CMD				BIT(0)

/* 0x18 : I2CD Slave Device Address Register   */
#define ASPEED_I2CD_DEV_ADDR_MASK			GENMASK(6, 0)

enum aspeed_i2c_master_state {
	ASPEED_I2C_MASTER_START,
	ASPEED_I2C_MASTER_TX_FIRST,
	ASPEED_I2C_MASTER_TX,
	ASPEED_I2C_MASTER_RX_FIRST,
	ASPEED_I2C_MASTER_RX,
	ASPEED_I2C_MASTER_STOP,
	ASPEED_I2C_MASTER_INACTIVE,
};

enum aspeed_i2c_slave_state {
	ASPEED_I2C_SLAVE_START,
	ASPEED_I2C_SLAVE_READ_REQUESTED,
	ASPEED_I2C_SLAVE_READ_PROCESSED,
	ASPEED_I2C_SLAVE_WRITE_REQUESTED,
	ASPEED_I2C_SLAVE_WRITE_RECEIVED,
	ASPEED_I2C_SLAVE_STOP,
};

struct aspeed_i2c_bus {
	struct i2c_adapter		adap;
	struct device			*dev;
	void __iomem			*base;
	/* Synchronizes I/O mem access to base. */
	spinlock_t			lock;
	struct completion		cmd_complete;
	unsigned long			parent_clk_frequency;
	u32				bus_frequency;
	/* Transaction state. */
	enum aspeed_i2c_master_state	master_state;
	struct i2c_msg			*msgs;
	size_t				buf_index;
	size_t				msgs_index;
	size_t				msgs_count;
	bool				send_stop;
	int				cmd_err;
	/* Protected only by i2c_lock_bus */
	int				master_xfer_result;
#if IS_ENABLED(CONFIG_I2C_SLAVE)
	struct i2c_client		*slave;
	enum aspeed_i2c_slave_state	slave_state;
#endif /* CONFIG_I2C_SLAVE */
};

static int aspeed_i2c_reset(struct aspeed_i2c_bus *bus);

static int aspeed_i2c_recover_bus(struct aspeed_i2c_bus *bus)
{
	unsigned long time_left, flags;
	int ret = 0;
	u32 command;

	spin_lock_irqsave(&bus->lock, flags);
	command = readl(bus->base + ASPEED_I2C_CMD_REG);

	if (command & ASPEED_I2CD_SDA_LINE_STS) {
		/* Bus is idle: no recovery needed. */
		if (command & ASPEED_I2CD_SCL_LINE_STS)
			goto out;
		dev_dbg(bus->dev, "SCL hung (state %x), attempting recovery\n",
			command);

		reinit_completion(&bus->cmd_complete);
		writel(ASPEED_I2CD_M_STOP_CMD, bus->base + ASPEED_I2C_CMD_REG);
		spin_unlock_irqrestore(&bus->lock, flags);

		time_left = wait_for_completion_timeout(
				&bus->cmd_complete, bus->adap.timeout);

		spin_lock_irqsave(&bus->lock, flags);
		if (time_left == 0)
			goto reset_out;
		else if (bus->cmd_err)
			goto reset_out;
		/* Recovery failed. */
		else if (!(readl(bus->base + ASPEED_I2C_CMD_REG) &
			   ASPEED_I2CD_SCL_LINE_STS))
			goto reset_out;
	/* Bus error. */
	} else {
		dev_dbg(bus->dev, "SDA hung (state %x), attempting recovery\n",
			command);

		reinit_completion(&bus->cmd_complete);
		/* Writes 1 to 8 SCL clock cycles until SDA is released. */
		writel(ASPEED_I2CD_BUS_RECOVER_CMD,
		       bus->base + ASPEED_I2C_CMD_REG);
		spin_unlock_irqrestore(&bus->lock, flags);

		time_left = wait_for_completion_timeout(
				&bus->cmd_complete, bus->adap.timeout);

		spin_lock_irqsave(&bus->lock, flags);
		if (time_left == 0)
			goto reset_out;
		else if (bus->cmd_err)
			goto reset_out;
		/* Recovery failed. */
		else if (!(readl(bus->base + ASPEED_I2C_CMD_REG) &
			   ASPEED_I2CD_SDA_LINE_STS))
			goto reset_out;
	}

out:
	spin_unlock_irqrestore(&bus->lock, flags);

	return ret;

reset_out:
	spin_unlock_irqrestore(&bus->lock, flags);

	return aspeed_i2c_reset(bus);
}

#if IS_ENABLED(CONFIG_I2C_SLAVE)
static bool aspeed_i2c_slave_irq(struct aspeed_i2c_bus *bus)
{
	u32 command, irq_status, status_ack = 0;
	struct i2c_client *slave = bus->slave;
	bool irq_handled = true;
	u8 value;

	spin_lock(&bus->lock);
	if (!slave) {
		irq_handled = false;
		goto out;
	}

	command = readl(bus->base + ASPEED_I2C_CMD_REG);
	irq_status = readl(bus->base + ASPEED_I2C_INTR_STS_REG);

	/* Slave was requested, restart state machine. */
	if (irq_status & ASPEED_I2CD_INTR_SLAVE_MATCH) {
		status_ack |= ASPEED_I2CD_INTR_SLAVE_MATCH;
		bus->slave_state = ASPEED_I2C_SLAVE_START;
	}

	/* Slave is not currently active, irq was for someone else. */
	if (bus->slave_state == ASPEED_I2C_SLAVE_STOP) {
		irq_handled = false;
		goto out;
	}

	dev_dbg(bus->dev, "slave irq status 0x%08x, cmd 0x%08x\n",
		irq_status, command);

	/* Slave was sent something. */
	if (irq_status & ASPEED_I2CD_INTR_RX_DONE) {
		value = readl(bus->base + ASPEED_I2C_BYTE_BUF_REG) >> 8;
		/* Handle address frame. */
		if (bus->slave_state == ASPEED_I2C_SLAVE_START) {
			if (value & 0x1)
				bus->slave_state =
						ASPEED_I2C_SLAVE_READ_REQUESTED;
			else
				bus->slave_state =
						ASPEED_I2C_SLAVE_WRITE_REQUESTED;
		}
		status_ack |= ASPEED_I2CD_INTR_RX_DONE;
	}

	/* Slave was asked to stop. */
	if (irq_status & ASPEED_I2CD_INTR_NORMAL_STOP) {
		status_ack |= ASPEED_I2CD_INTR_NORMAL_STOP;
		bus->slave_state = ASPEED_I2C_SLAVE_STOP;
	}
	if (irq_status & ASPEED_I2CD_INTR_TX_NAK) {
		status_ack |= ASPEED_I2CD_INTR_TX_NAK;
		bus->slave_state = ASPEED_I2C_SLAVE_STOP;
	}

	switch (bus->slave_state) {
	case ASPEED_I2C_SLAVE_READ_REQUESTED:
		if (irq_status & ASPEED_I2CD_INTR_TX_ACK)
			dev_err(bus->dev, "Unexpected ACK on read request.\n");
		bus->slave_state = ASPEED_I2C_SLAVE_READ_PROCESSED;

		i2c_slave_event(slave, I2C_SLAVE_READ_REQUESTED, &value);
		writel(value, bus->base + ASPEED_I2C_BYTE_BUF_REG);
		writel(ASPEED_I2CD_S_TX_CMD, bus->base + ASPEED_I2C_CMD_REG);
		break;
	case ASPEED_I2C_SLAVE_READ_PROCESSED:
		status_ack |= ASPEED_I2CD_INTR_TX_ACK;
		if (!(irq_status & ASPEED_I2CD_INTR_TX_ACK))
			dev_err(bus->dev,
				"Expected ACK after processed read.\n");
		i2c_slave_event(slave, I2C_SLAVE_READ_PROCESSED, &value);
		writel(value, bus->base + ASPEED_I2C_BYTE_BUF_REG);
		writel(ASPEED_I2CD_S_TX_CMD, bus->base + ASPEED_I2C_CMD_REG);
		break;
	case ASPEED_I2C_SLAVE_WRITE_REQUESTED:
		bus->slave_state = ASPEED_I2C_SLAVE_WRITE_RECEIVED;
		i2c_slave_event(slave, I2C_SLAVE_WRITE_REQUESTED, &value);
		break;
	case ASPEED_I2C_SLAVE_WRITE_RECEIVED:
		i2c_slave_event(slave, I2C_SLAVE_WRITE_RECEIVED, &value);
		break;
	case ASPEED_I2C_SLAVE_STOP:
		i2c_slave_event(slave, I2C_SLAVE_STOP, &value);
		break;
	default:
		dev_err(bus->dev, "unhandled slave_state: %d\n",
			bus->slave_state);
		break;
	}

	if (status_ack != irq_status)
		dev_err(bus->dev,
			"irq handled != irq. expected %x, but was %x\n",
			irq_status, status_ack);
	writel(status_ack, bus->base + ASPEED_I2C_INTR_STS_REG);

out:
	spin_unlock(&bus->lock);
	return irq_handled;
}
#endif /* CONFIG_I2C_SLAVE */

/* precondition: bus.lock has been acquired. */
static void aspeed_i2c_do_start(struct aspeed_i2c_bus *bus)
{
	u32 command = ASPEED_I2CD_M_START_CMD | ASPEED_I2CD_M_TX_CMD;
	struct i2c_msg *msg = &bus->msgs[bus->msgs_index];
	u8 slave_addr = msg->addr << 1;

	bus->master_state = ASPEED_I2C_MASTER_START;
	bus->buf_index = 0;

	if (msg->flags & I2C_M_RD) {
		slave_addr |= 1;
		command |= ASPEED_I2CD_M_RX_CMD;
		/* Need to let the hardware know to NACK after RX. */
		if (msg->len == 1 && !(msg->flags & I2C_M_RECV_LEN))
			command |= ASPEED_I2CD_M_S_RX_CMD_LAST;
	}

	writel(slave_addr, bus->base + ASPEED_I2C_BYTE_BUF_REG);
	writel(command, bus->base + ASPEED_I2C_CMD_REG);
}

/* precondition: bus.lock has been acquired. */
static void aspeed_i2c_do_stop(struct aspeed_i2c_bus *bus)
{
	bus->master_state = ASPEED_I2C_MASTER_STOP;
	writel(ASPEED_I2CD_M_STOP_CMD, bus->base + ASPEED_I2C_CMD_REG);
}

/* precondition: bus.lock has been acquired. */
static void aspeed_i2c_next_msg_or_stop(struct aspeed_i2c_bus *bus)
{
	if (bus->msgs_index + 1 < bus->msgs_count) {
		bus->msgs_index++;
		aspeed_i2c_do_start(bus);
	} else {
		aspeed_i2c_do_stop(bus);
	}
}

static int aspeed_i2c_is_irq_error(u32 irq_status)
{
	if (irq_status & ASPEED_I2CD_INTR_ARBIT_LOSS)
		return -EAGAIN;
	if (irq_status & (ASPEED_I2CD_INTR_SDA_DL_TIMEOUT |
			  ASPEED_I2CD_INTR_SCL_TIMEOUT))
		return -EBUSY;
	if (irq_status & (ASPEED_I2CD_INTR_ABNORMAL))
		return -EPROTO;

	return 0;
}

static bool aspeed_i2c_master_irq(struct aspeed_i2c_bus *bus)
{
	u32 irq_status, status_ack = 0, command = 0;
	struct i2c_msg *msg;
	u8 recv_byte;
	int ret;

	spin_lock(&bus->lock);
	irq_status = readl(bus->base + ASPEED_I2C_INTR_STS_REG);
	/* Ack all interrupt bits. */
	writel(irq_status, bus->base + ASPEED_I2C_INTR_STS_REG);

	if (irq_status & ASPEED_I2CD_INTR_BUS_RECOVER_DONE) {
		bus->master_state = ASPEED_I2C_MASTER_INACTIVE;
		status_ack |= ASPEED_I2CD_INTR_BUS_RECOVER_DONE;
		goto out_complete;
	}

	/*
	 * We encountered an interrupt that reports an error: the hardware
	 * should clear the command queue effectively taking us back to the
	 * INACTIVE state.
	 */
	ret = aspeed_i2c_is_irq_error(irq_status);
	if (ret < 0) {
		dev_dbg(bus->dev, "received error interrupt: 0x%08x",
			irq_status);
		bus->cmd_err = ret;
		bus->master_state = ASPEED_I2C_MASTER_INACTIVE;
		goto out_complete;
	}

	/* We are in an invalid state; reset bus to a known state. */
	if (!bus->msgs && bus->master_state != ASPEED_I2C_MASTER_STOP) {
		dev_err(bus->dev, "bus in unknown state");
		bus->cmd_err = -EIO;
		aspeed_i2c_do_stop(bus);
		goto out_no_complete;
	}
	msg = &bus->msgs[bus->msgs_index];

	/*
	 * START is a special case because we still have to handle a subsequent
	 * TX or RX immediately after we handle it, so we handle it here and
	 * then update the state and handle the new state below.
	 */
	if (bus->master_state == ASPEED_I2C_MASTER_START) {
		if (unlikely(!(irq_status & ASPEED_I2CD_INTR_TX_ACK))) {
			pr_devel("no slave present at %02x", msg->addr);
			status_ack |= ASPEED_I2CD_INTR_TX_NAK;
			bus->cmd_err = -ENXIO;
			aspeed_i2c_do_stop(bus);
			goto out_no_complete;
		}
		status_ack |= ASPEED_I2CD_INTR_TX_ACK;
		if (msg->len == 0) { /* SMBUS_QUICK */
			aspeed_i2c_do_stop(bus);
			goto out_no_complete;
		}
		if (msg->flags & I2C_M_RD)
			bus->master_state = ASPEED_I2C_MASTER_RX_FIRST;
		else
			bus->master_state = ASPEED_I2C_MASTER_TX_FIRST;
	}

	switch (bus->master_state) {
	case ASPEED_I2C_MASTER_TX:
		if (unlikely(irq_status & ASPEED_I2CD_INTR_TX_NAK)) {
			dev_dbg(bus->dev, "slave NACKed TX");
			status_ack |= ASPEED_I2CD_INTR_TX_NAK;
			goto error_and_stop;
		} else if (unlikely(!(irq_status & ASPEED_I2CD_INTR_TX_ACK))) {
			dev_err(bus->dev, "slave failed to ACK TX");
			goto error_and_stop;
		}
		status_ack |= ASPEED_I2CD_INTR_TX_ACK;
		/* fallthrough intended */
	case ASPEED_I2C_MASTER_TX_FIRST:
		if (bus->buf_index < msg->len) {
			bus->master_state = ASPEED_I2C_MASTER_TX;
			writel(msg->buf[bus->buf_index++],
			       bus->base + ASPEED_I2C_BYTE_BUF_REG);
			writel(ASPEED_I2CD_M_TX_CMD,
			       bus->base + ASPEED_I2C_CMD_REG);
		} else {
			aspeed_i2c_next_msg_or_stop(bus);
		}
		goto out_no_complete;
	case ASPEED_I2C_MASTER_RX_FIRST:
		/* RX may not have completed yet (only address cycle) */
		if (!(irq_status & ASPEED_I2CD_INTR_RX_DONE))
			goto out_no_complete;
		/* fallthrough intended */
	case ASPEED_I2C_MASTER_RX:
		if (unlikely(!(irq_status & ASPEED_I2CD_INTR_RX_DONE))) {
			dev_err(bus->dev, "master failed to RX");
			goto error_and_stop;
		}
		status_ack |= ASPEED_I2CD_INTR_RX_DONE;

		recv_byte = readl(bus->base + ASPEED_I2C_BYTE_BUF_REG) >> 8;
		msg->buf[bus->buf_index++] = recv_byte;

		if (msg->flags & I2C_M_RECV_LEN) {
			if (unlikely(recv_byte > I2C_SMBUS_BLOCK_MAX)) {
				bus->cmd_err = -EPROTO;
				aspeed_i2c_do_stop(bus);
				goto out_no_complete;
			}
			msg->len = recv_byte +
					((msg->flags & I2C_CLIENT_PEC) ? 2 : 1);
			msg->flags &= ~I2C_M_RECV_LEN;
		}

		if (bus->buf_index < msg->len) {
			bus->master_state = ASPEED_I2C_MASTER_RX;
			command = ASPEED_I2CD_M_RX_CMD;
			if (bus->buf_index + 1 == msg->len)
				command |= ASPEED_I2CD_M_S_RX_CMD_LAST;
			writel(command, bus->base + ASPEED_I2C_CMD_REG);
		} else {
			aspeed_i2c_next_msg_or_stop(bus);
		}
		goto out_no_complete;
	case ASPEED_I2C_MASTER_STOP:
		if (unlikely(!(irq_status & ASPEED_I2CD_INTR_NORMAL_STOP))) {
			dev_err(bus->dev, "master failed to STOP");
			bus->cmd_err = -EIO;
			/* Do not STOP as we have already tried. */
		} else {
			status_ack |= ASPEED_I2CD_INTR_NORMAL_STOP;
		}

		bus->master_state = ASPEED_I2C_MASTER_INACTIVE;
		goto out_complete;
	case ASPEED_I2C_MASTER_INACTIVE:
		dev_err(bus->dev,
			"master received interrupt 0x%08x, but is inactive",
			irq_status);
		bus->cmd_err = -EIO;
		/* Do not STOP as we should be inactive. */
		goto out_complete;
	default:
		WARN(1, "unknown master state\n");
		bus->master_state = ASPEED_I2C_MASTER_INACTIVE;
		bus->cmd_err = -EINVAL;
		goto out_complete;
	}
error_and_stop:
	bus->cmd_err = -EIO;
	aspeed_i2c_do_stop(bus);
	goto out_no_complete;
out_complete:
	bus->msgs = NULL;
	if (bus->cmd_err)
		bus->master_xfer_result = bus->cmd_err;
	else
		bus->master_xfer_result = bus->msgs_index + 1;
	complete(&bus->cmd_complete);
out_no_complete:
	if (irq_status != status_ack)
		dev_err(bus->dev,
			"irq handled != irq. expected 0x%08x, but was 0x%08x\n",
			irq_status, status_ack);
	spin_unlock(&bus->lock);
	return !!irq_status;
}

static irqreturn_t aspeed_i2c_bus_irq(int irq, void *dev_id)
{
	struct aspeed_i2c_bus *bus = dev_id;

#if IS_ENABLED(CONFIG_I2C_SLAVE)
	if (aspeed_i2c_slave_irq(bus)) {
		dev_dbg(bus->dev, "irq handled by slave.\n");
		return IRQ_HANDLED;
	}
#endif /* CONFIG_I2C_SLAVE */

	return aspeed_i2c_master_irq(bus) ? IRQ_HANDLED : IRQ_NONE;
}

static int aspeed_i2c_master_xfer(struct i2c_adapter *adap,
				  struct i2c_msg *msgs, int num)
{
	struct aspeed_i2c_bus *bus = i2c_get_adapdata(adap);
	unsigned long time_left, flags;
	int ret = 0;

	spin_lock_irqsave(&bus->lock, flags);
	bus->cmd_err = 0;

	/* If bus is busy, attempt recovery. We assume a single master
	 * environment.
	 */
	if (readl(bus->base + ASPEED_I2C_CMD_REG) & ASPEED_I2CD_BUS_BUSY_STS) {
		spin_unlock_irqrestore(&bus->lock, flags);
		ret = aspeed_i2c_recover_bus(bus);
		if (ret)
			return ret;
		spin_lock_irqsave(&bus->lock, flags);
	}

	bus->cmd_err = 0;
	bus->msgs = msgs;
	bus->msgs_index = 0;
	bus->msgs_count = num;

	reinit_completion(&bus->cmd_complete);
	aspeed_i2c_do_start(bus);
	spin_unlock_irqrestore(&bus->lock, flags);

	time_left = wait_for_completion_timeout(&bus->cmd_complete,
						bus->adap.timeout);

	if (time_left == 0)
		return -ETIMEDOUT;
	else
		return bus->master_xfer_result;
}

static u32 aspeed_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_SMBUS_BLOCK_DATA;
}

#if IS_ENABLED(CONFIG_I2C_SLAVE)
/* precondition: bus.lock has been acquired. */
static void __aspeed_i2c_reg_slave(struct aspeed_i2c_bus *bus, u16 slave_addr)
{
	u32 addr_reg_val, func_ctrl_reg_val;

	/* Set slave addr. */
	addr_reg_val = readl(bus->base + ASPEED_I2C_DEV_ADDR_REG);
	addr_reg_val &= ~ASPEED_I2CD_DEV_ADDR_MASK;
	addr_reg_val |= slave_addr & ASPEED_I2CD_DEV_ADDR_MASK;
	writel(addr_reg_val, bus->base + ASPEED_I2C_DEV_ADDR_REG);

	/* Turn on slave mode. */
	func_ctrl_reg_val = readl(bus->base + ASPEED_I2C_FUN_CTRL_REG);
	func_ctrl_reg_val |= ASPEED_I2CD_SLAVE_EN;
	writel(func_ctrl_reg_val, bus->base + ASPEED_I2C_FUN_CTRL_REG);
}

static int aspeed_i2c_reg_slave(struct i2c_client *client)
{
	struct aspeed_i2c_bus *bus = i2c_get_adapdata(client->adapter);
	unsigned long flags;

	spin_lock_irqsave(&bus->lock, flags);
	if (bus->slave) {
		spin_unlock_irqrestore(&bus->lock, flags);
		return -EINVAL;
	}

	__aspeed_i2c_reg_slave(bus, client->addr);

	bus->slave = client;
	bus->slave_state = ASPEED_I2C_SLAVE_STOP;
	spin_unlock_irqrestore(&bus->lock, flags);

	return 0;
}

static int aspeed_i2c_unreg_slave(struct i2c_client *client)
{
	struct aspeed_i2c_bus *bus = i2c_get_adapdata(client->adapter);
	u32 func_ctrl_reg_val;
	unsigned long flags;

	spin_lock_irqsave(&bus->lock, flags);
	if (!bus->slave) {
		spin_unlock_irqrestore(&bus->lock, flags);
		return -EINVAL;
	}

	/* Turn off slave mode. */
	func_ctrl_reg_val = readl(bus->base + ASPEED_I2C_FUN_CTRL_REG);
	func_ctrl_reg_val &= ~ASPEED_I2CD_SLAVE_EN;
	writel(func_ctrl_reg_val, bus->base + ASPEED_I2C_FUN_CTRL_REG);

	bus->slave = NULL;
	spin_unlock_irqrestore(&bus->lock, flags);

	return 0;
}
#endif /* CONFIG_I2C_SLAVE */

static const struct i2c_algorithm aspeed_i2c_algo = {
	.master_xfer	= aspeed_i2c_master_xfer,
	.functionality	= aspeed_i2c_functionality,
#if IS_ENABLED(CONFIG_I2C_SLAVE)
	.reg_slave	= aspeed_i2c_reg_slave,
	.unreg_slave	= aspeed_i2c_unreg_slave,
#endif /* CONFIG_I2C_SLAVE */
};

static u32 aspeed_i2c_get_clk_reg_val(u32 divisor)
{
	u32 base_clk, clk_high, clk_low, tmp;

	/*
	 * The actual clock frequency of SCL is:
	 *	SCL_freq = APB_freq / (base_freq * (SCL_high + SCL_low))
	 *		 = APB_freq / divisor
	 * where base_freq is a programmable clock divider; its value is
	 *	base_freq = 1 << base_clk
	 * SCL_high is the number of base_freq clock cycles that SCL stays high
	 * and SCL_low is the number of base_freq clock cycles that SCL stays
	 * low for a period of SCL.
	 * The actual register has a minimum SCL_high and SCL_low minimum of 1;
	 * thus, they start counting at zero. So
	 *	SCL_high = clk_high + 1
	 *	SCL_low	 = clk_low + 1
	 * Thus,
	 *	SCL_freq = APB_freq /
	 *		((1 << base_clk) * (clk_high + 1 + clk_low + 1))
	 * The documentation recommends clk_high >= 8 and clk_low >= 7 when
	 * possible; this last constraint gives us the following solution:
	 */
	base_clk = divisor > 33 ? ilog2((divisor - 1) / 32) + 1 : 0;
	tmp = divisor / (1 << base_clk);
	clk_high = tmp / 2 + tmp % 2;
	clk_low = tmp - clk_high;

	clk_high -= 1;
	clk_low -= 1;

	return ((clk_high << ASPEED_I2CD_TIME_SCL_HIGH_SHIFT)
		& ASPEED_I2CD_TIME_SCL_HIGH_MASK)
			| ((clk_low << ASPEED_I2CD_TIME_SCL_LOW_SHIFT)
			   & ASPEED_I2CD_TIME_SCL_LOW_MASK)
			| (base_clk & ASPEED_I2CD_TIME_BASE_DIVISOR_MASK);
}

/* precondition: bus.lock has been acquired. */
static int aspeed_i2c_init_clk(struct aspeed_i2c_bus *bus)
{
	u32 divisor, clk_reg_val;

	divisor = bus->parent_clk_frequency / bus->bus_frequency;
	clk_reg_val = aspeed_i2c_get_clk_reg_val(divisor);
	writel(clk_reg_val, bus->base + ASPEED_I2C_AC_TIMING_REG1);
	writel(ASPEED_NO_TIMEOUT_CTRL, bus->base + ASPEED_I2C_AC_TIMING_REG2);

	return 0;
}

/* precondition: bus.lock has been acquired. */
static int aspeed_i2c_init(struct aspeed_i2c_bus *bus,
			     struct platform_device *pdev)
{
	u32 fun_ctrl_reg = ASPEED_I2CD_MASTER_EN;
	int ret;

	/* Disable everything. */
	writel(0, bus->base + ASPEED_I2C_FUN_CTRL_REG);

	ret = aspeed_i2c_init_clk(bus);
	if (ret < 0)
		return ret;

	if (!of_property_read_bool(pdev->dev.of_node, "multi-master"))
		fun_ctrl_reg |= ASPEED_I2CD_MULTI_MASTER_DIS;

	/* Enable Master Mode */
	writel(readl(bus->base + ASPEED_I2C_FUN_CTRL_REG) | fun_ctrl_reg,
	       bus->base + ASPEED_I2C_FUN_CTRL_REG);

#if IS_ENABLED(CONFIG_I2C_SLAVE)
	/* If slave has already been registered, re-enable it. */
	if (bus->slave)
		__aspeed_i2c_reg_slave(bus, bus->slave->addr);
#endif /* CONFIG_I2C_SLAVE */

	/* Set interrupt generation of I2C controller */
	writel(ASPEED_I2CD_INTR_ALL, bus->base + ASPEED_I2C_INTR_CTRL_REG);

	return 0;
}

static int aspeed_i2c_reset(struct aspeed_i2c_bus *bus)
{
	struct platform_device *pdev = to_platform_device(bus->dev);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&bus->lock, flags);

	/* Disable and ack all interrupts. */
	writel(0, bus->base + ASPEED_I2C_INTR_CTRL_REG);
	writel(0xffffffff, bus->base + ASPEED_I2C_INTR_STS_REG);

	ret = aspeed_i2c_init(bus, pdev);

	spin_unlock_irqrestore(&bus->lock, flags);

	return ret;
}

static int aspeed_i2c_probe_bus(struct platform_device *pdev)
{
	struct aspeed_i2c_bus *bus;
	struct clk *parent_clk;
	struct resource *res;
	int irq, ret;

	bus = devm_kzalloc(&pdev->dev, sizeof(*bus), GFP_KERNEL);
	if (!bus)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	bus->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(bus->base))
		return PTR_ERR(bus->base);

	parent_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(parent_clk))
		return PTR_ERR(parent_clk);
	bus->parent_clk_frequency = clk_get_rate(parent_clk);
	/* We just need the clock rate, we don't actually use the clk object. */
	devm_clk_put(&pdev->dev, parent_clk);

	ret = of_property_read_u32(pdev->dev.of_node,
				   "bus-frequency", &bus->bus_frequency);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Could not read bus-frequency property\n");
		bus->bus_frequency = 100000;
	}

	/* Initialize the I2C adapter */
	spin_lock_init(&bus->lock);
	init_completion(&bus->cmd_complete);
	bus->adap.owner = THIS_MODULE;
	bus->adap.retries = 0;
	bus->adap.timeout = 5 * HZ;
	bus->adap.algo = &aspeed_i2c_algo;
	bus->adap.dev.parent = &pdev->dev;
	bus->adap.dev.of_node = pdev->dev.of_node;
	strlcpy(bus->adap.name, pdev->name, sizeof(bus->adap.name));
	i2c_set_adapdata(&bus->adap, bus);

	bus->dev = &pdev->dev;

	/* Clean up any left over interrupt state. */
	writel(0, bus->base + ASPEED_I2C_INTR_CTRL_REG);
	writel(0xffffffff, bus->base + ASPEED_I2C_INTR_STS_REG);
	/*
	 * bus.lock does not need to be held because the interrupt handler has
	 * not been enabled yet.
	 */
	ret = aspeed_i2c_init(bus, pdev);
	if (ret < 0)
		return ret;

	irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	ret = devm_request_irq(&pdev->dev, irq, aspeed_i2c_bus_irq,
			       0, dev_name(&pdev->dev), bus);
	if (ret < 0)
		return ret;

	ret = i2c_add_adapter(&bus->adap);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, bus);

	dev_info(bus->dev, "i2c bus %d registered, irq %d\n",
		 bus->adap.nr, irq);

	return 0;
}

static int aspeed_i2c_remove_bus(struct platform_device *pdev)
{
	struct aspeed_i2c_bus *bus = platform_get_drvdata(pdev);
	unsigned long flags;

	spin_lock_irqsave(&bus->lock, flags);

	/* Disable everything. */
	writel(0, bus->base + ASPEED_I2C_FUN_CTRL_REG);
	writel(0, bus->base + ASPEED_I2C_INTR_CTRL_REG);

	spin_unlock_irqrestore(&bus->lock, flags);

	i2c_del_adapter(&bus->adap);

	return 0;
}

static const struct of_device_id aspeed_i2c_bus_of_table[] = {
	{ .compatible = "aspeed,ast2400-i2c-bus", },
	{ .compatible = "aspeed,ast2500-i2c-bus", },
	{ },
};
MODULE_DEVICE_TABLE(of, aspeed_i2c_bus_of_table);

static struct platform_driver aspeed_i2c_bus_driver = {
	.probe		= aspeed_i2c_probe_bus,
	.remove		= aspeed_i2c_remove_bus,
	.driver		= {
		.name		= "aspeed-i2c-bus",
		.of_match_table	= aspeed_i2c_bus_of_table,
	},
};
module_platform_driver(aspeed_i2c_bus_driver);

MODULE_AUTHOR("Brendan Higgins <brendanhiggins@google.com>");
MODULE_DESCRIPTION("Aspeed I2C Bus Driver");
MODULE_LICENSE("GPL v2");
