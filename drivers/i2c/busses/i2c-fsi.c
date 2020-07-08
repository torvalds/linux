// SPDX-License-Identifier: GPL-2.0+
/*
 * FSI-attached I2C master algorithm
 *
 * Copyright 2018 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fsi.h>
#include <linux/i2c.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>

#define FSI_ENGID_I2C		0x7

#define I2C_DEFAULT_CLK_DIV	6

/* i2c registers */
#define I2C_FSI_FIFO		0x00
#define I2C_FSI_CMD		0x04
#define I2C_FSI_MODE		0x08
#define I2C_FSI_WATER_MARK	0x0C
#define I2C_FSI_INT_MASK	0x10
#define I2C_FSI_INT_COND	0x14
#define I2C_FSI_OR_INT_MASK	0x14
#define I2C_FSI_INTS		0x18
#define I2C_FSI_AND_INT_MASK	0x18
#define I2C_FSI_STAT		0x1C
#define I2C_FSI_RESET_I2C	0x1C
#define I2C_FSI_ESTAT		0x20
#define I2C_FSI_RESET_ERR	0x20
#define I2C_FSI_RESID_LEN	0x24
#define I2C_FSI_SET_SCL		0x24
#define I2C_FSI_PORT_BUSY	0x28
#define I2C_FSI_RESET_SCL	0x2C
#define I2C_FSI_SET_SDA		0x30
#define I2C_FSI_RESET_SDA	0x34

/* cmd register */
#define I2C_CMD_WITH_START	BIT(31)
#define I2C_CMD_WITH_ADDR	BIT(30)
#define I2C_CMD_RD_CONT		BIT(29)
#define I2C_CMD_WITH_STOP	BIT(28)
#define I2C_CMD_FORCELAUNCH	BIT(27)
#define I2C_CMD_ADDR		GENMASK(23, 17)
#define I2C_CMD_READ		BIT(16)
#define I2C_CMD_LEN		GENMASK(15, 0)

/* mode register */
#define I2C_MODE_CLKDIV		GENMASK(31, 16)
#define I2C_MODE_PORT		GENMASK(15, 10)
#define I2C_MODE_ENHANCED	BIT(3)
#define I2C_MODE_DIAG		BIT(2)
#define I2C_MODE_PACE_ALLOW	BIT(1)
#define I2C_MODE_WRAP		BIT(0)

/* watermark register */
#define I2C_WATERMARK_HI	GENMASK(15, 12)
#define I2C_WATERMARK_LO	GENMASK(7, 4)

#define I2C_FIFO_HI_LVL		4
#define I2C_FIFO_LO_LVL		4

/* interrupt register */
#define I2C_INT_INV_CMD		BIT(15)
#define I2C_INT_PARITY		BIT(14)
#define I2C_INT_BE_OVERRUN	BIT(13)
#define I2C_INT_BE_ACCESS	BIT(12)
#define I2C_INT_LOST_ARB	BIT(11)
#define I2C_INT_NACK		BIT(10)
#define I2C_INT_DAT_REQ		BIT(9)
#define I2C_INT_CMD_COMP	BIT(8)
#define I2C_INT_STOP_ERR	BIT(7)
#define I2C_INT_BUSY		BIT(6)
#define I2C_INT_IDLE		BIT(5)

/* status register */
#define I2C_STAT_INV_CMD	BIT(31)
#define I2C_STAT_PARITY		BIT(30)
#define I2C_STAT_BE_OVERRUN	BIT(29)
#define I2C_STAT_BE_ACCESS	BIT(28)
#define I2C_STAT_LOST_ARB	BIT(27)
#define I2C_STAT_NACK		BIT(26)
#define I2C_STAT_DAT_REQ	BIT(25)
#define I2C_STAT_CMD_COMP	BIT(24)
#define I2C_STAT_STOP_ERR	BIT(23)
#define I2C_STAT_MAX_PORT	GENMASK(22, 16)
#define I2C_STAT_ANY_INT	BIT(15)
#define I2C_STAT_SCL_IN		BIT(11)
#define I2C_STAT_SDA_IN		BIT(10)
#define I2C_STAT_PORT_BUSY	BIT(9)
#define I2C_STAT_SELF_BUSY	BIT(8)
#define I2C_STAT_FIFO_COUNT	GENMASK(7, 0)

#define I2C_STAT_ERR		(I2C_STAT_INV_CMD |			\
				 I2C_STAT_PARITY |			\
				 I2C_STAT_BE_OVERRUN |			\
				 I2C_STAT_BE_ACCESS |			\
				 I2C_STAT_LOST_ARB |			\
				 I2C_STAT_NACK |			\
				 I2C_STAT_STOP_ERR)
#define I2C_STAT_ANY_RESP	(I2C_STAT_ERR |				\
				 I2C_STAT_DAT_REQ |			\
				 I2C_STAT_CMD_COMP)

/* extended status register */
#define I2C_ESTAT_FIFO_SZ	GENMASK(31, 24)
#define I2C_ESTAT_SCL_IN_SY	BIT(15)
#define I2C_ESTAT_SDA_IN_SY	BIT(14)
#define I2C_ESTAT_S_SCL		BIT(13)
#define I2C_ESTAT_S_SDA		BIT(12)
#define I2C_ESTAT_M_SCL		BIT(11)
#define I2C_ESTAT_M_SDA		BIT(10)
#define I2C_ESTAT_HI_WATER	BIT(9)
#define I2C_ESTAT_LO_WATER	BIT(8)
#define I2C_ESTAT_PORT_BUSY	BIT(7)
#define I2C_ESTAT_SELF_BUSY	BIT(6)
#define I2C_ESTAT_VERSION	GENMASK(4, 0)

/* port busy register */
#define I2C_PORT_BUSY_RESET	BIT(31)

/* wait for command complete or data request */
#define I2C_CMD_SLEEP_MAX_US	500
#define I2C_CMD_SLEEP_MIN_US	50

/* wait after reset; choose time from legacy driver */
#define I2C_RESET_SLEEP_MAX_US	2000
#define I2C_RESET_SLEEP_MIN_US	1000

/* choose timeout length from legacy driver; it's well tested */
#define I2C_ABORT_TIMEOUT	msecs_to_jiffies(100)

struct fsi_i2c_master {
	struct fsi_device	*fsi;
	u8			fifo_size;
	struct list_head	ports;
	struct mutex		lock;
};

struct fsi_i2c_port {
	struct list_head	list;
	struct i2c_adapter	adapter;
	struct fsi_i2c_master	*master;
	u16			port;
	u16			xfrd;
};

static int fsi_i2c_read_reg(struct fsi_device *fsi, unsigned int reg,
			    u32 *data)
{
	int rc;
	__be32 data_be;

	rc = fsi_device_read(fsi, reg, &data_be, sizeof(data_be));
	if (rc)
		return rc;

	*data = be32_to_cpu(data_be);

	return 0;
}

static int fsi_i2c_write_reg(struct fsi_device *fsi, unsigned int reg,
			     u32 *data)
{
	__be32 data_be = cpu_to_be32p(data);

	return fsi_device_write(fsi, reg, &data_be, sizeof(data_be));
}

static int fsi_i2c_dev_init(struct fsi_i2c_master *i2c)
{
	int rc;
	u32 mode = I2C_MODE_ENHANCED, extended_status, watermark;
	u32 interrupt = 0;

	/* since we use polling, disable interrupts */
	rc = fsi_i2c_write_reg(i2c->fsi, I2C_FSI_INT_MASK, &interrupt);
	if (rc)
		return rc;

	mode |= FIELD_PREP(I2C_MODE_CLKDIV, I2C_DEFAULT_CLK_DIV);
	rc = fsi_i2c_write_reg(i2c->fsi, I2C_FSI_MODE, &mode);
	if (rc)
		return rc;

	rc = fsi_i2c_read_reg(i2c->fsi, I2C_FSI_ESTAT, &extended_status);
	if (rc)
		return rc;

	i2c->fifo_size = FIELD_GET(I2C_ESTAT_FIFO_SZ, extended_status);
	watermark = FIELD_PREP(I2C_WATERMARK_HI,
			       i2c->fifo_size - I2C_FIFO_HI_LVL);
	watermark |= FIELD_PREP(I2C_WATERMARK_LO, I2C_FIFO_LO_LVL);

	return fsi_i2c_write_reg(i2c->fsi, I2C_FSI_WATER_MARK, &watermark);
}

static int fsi_i2c_set_port(struct fsi_i2c_port *port)
{
	int rc;
	struct fsi_device *fsi = port->master->fsi;
	u32 mode, dummy = 0;

	rc = fsi_i2c_read_reg(fsi, I2C_FSI_MODE, &mode);
	if (rc)
		return rc;

	if (FIELD_GET(I2C_MODE_PORT, mode) == port->port)
		return 0;

	mode = (mode & ~I2C_MODE_PORT) | FIELD_PREP(I2C_MODE_PORT, port->port);
	rc = fsi_i2c_write_reg(fsi, I2C_FSI_MODE, &mode);
	if (rc)
		return rc;

	/* reset engine when port is changed */
	return fsi_i2c_write_reg(fsi, I2C_FSI_RESET_ERR, &dummy);
}

static int fsi_i2c_start(struct fsi_i2c_port *port, struct i2c_msg *msg,
			 bool stop)
{
	struct fsi_i2c_master *i2c = port->master;
	u32 cmd = I2C_CMD_WITH_START | I2C_CMD_WITH_ADDR;

	port->xfrd = 0;

	if (msg->flags & I2C_M_RD)
		cmd |= I2C_CMD_READ;

	if (stop || msg->flags & I2C_M_STOP)
		cmd |= I2C_CMD_WITH_STOP;

	cmd |= FIELD_PREP(I2C_CMD_ADDR, msg->addr);
	cmd |= FIELD_PREP(I2C_CMD_LEN, msg->len);

	return fsi_i2c_write_reg(i2c->fsi, I2C_FSI_CMD, &cmd);
}

static int fsi_i2c_get_op_bytes(int op_bytes)
{
	/* fsi is limited to max 4 byte aligned ops */
	if (op_bytes > 4)
		return 4;
	else if (op_bytes == 3)
		return 2;
	return op_bytes;
}

static int fsi_i2c_write_fifo(struct fsi_i2c_port *port, struct i2c_msg *msg,
			      u8 fifo_count)
{
	int write;
	int rc;
	struct fsi_i2c_master *i2c = port->master;
	int bytes_to_write = i2c->fifo_size - fifo_count;
	int bytes_remaining = msg->len - port->xfrd;

	bytes_to_write = min(bytes_to_write, bytes_remaining);

	while (bytes_to_write) {
		write = fsi_i2c_get_op_bytes(bytes_to_write);

		rc = fsi_device_write(i2c->fsi, I2C_FSI_FIFO,
				      &msg->buf[port->xfrd], write);
		if (rc)
			return rc;

		port->xfrd += write;
		bytes_to_write -= write;
	}

	return 0;
}

static int fsi_i2c_read_fifo(struct fsi_i2c_port *port, struct i2c_msg *msg,
			     u8 fifo_count)
{
	int read;
	int rc;
	struct fsi_i2c_master *i2c = port->master;
	int bytes_to_read;
	int xfr_remaining = msg->len - port->xfrd;
	u32 dummy;

	bytes_to_read = min_t(int, fifo_count, xfr_remaining);

	while (bytes_to_read) {
		read = fsi_i2c_get_op_bytes(bytes_to_read);

		if (xfr_remaining) {
			rc = fsi_device_read(i2c->fsi, I2C_FSI_FIFO,
					     &msg->buf[port->xfrd], read);
			if (rc)
				return rc;

			port->xfrd += read;
			xfr_remaining -= read;
		} else {
			/* no more buffer but data in fifo, need to clear it */
			rc = fsi_device_read(i2c->fsi, I2C_FSI_FIFO, &dummy,
					     read);
			if (rc)
				return rc;
		}

		bytes_to_read -= read;
	}

	return 0;
}

static int fsi_i2c_get_scl(struct i2c_adapter *adap)
{
	u32 stat = 0;
	struct fsi_i2c_port *port = adap->algo_data;
	struct fsi_i2c_master *i2c = port->master;

	fsi_i2c_read_reg(i2c->fsi, I2C_FSI_STAT, &stat);

	return !!(stat & I2C_STAT_SCL_IN);
}

static void fsi_i2c_set_scl(struct i2c_adapter *adap, int val)
{
	u32 dummy = 0;
	struct fsi_i2c_port *port = adap->algo_data;
	struct fsi_i2c_master *i2c = port->master;

	if (val)
		fsi_i2c_write_reg(i2c->fsi, I2C_FSI_SET_SCL, &dummy);
	else
		fsi_i2c_write_reg(i2c->fsi, I2C_FSI_RESET_SCL, &dummy);
}

static int fsi_i2c_get_sda(struct i2c_adapter *adap)
{
	u32 stat = 0;
	struct fsi_i2c_port *port = adap->algo_data;
	struct fsi_i2c_master *i2c = port->master;

	fsi_i2c_read_reg(i2c->fsi, I2C_FSI_STAT, &stat);

	return !!(stat & I2C_STAT_SDA_IN);
}

static void fsi_i2c_set_sda(struct i2c_adapter *adap, int val)
{
	u32 dummy = 0;
	struct fsi_i2c_port *port = adap->algo_data;
	struct fsi_i2c_master *i2c = port->master;

	if (val)
		fsi_i2c_write_reg(i2c->fsi, I2C_FSI_SET_SDA, &dummy);
	else
		fsi_i2c_write_reg(i2c->fsi, I2C_FSI_RESET_SDA, &dummy);
}

static void fsi_i2c_prepare_recovery(struct i2c_adapter *adap)
{
	int rc;
	u32 mode;
	struct fsi_i2c_port *port = adap->algo_data;
	struct fsi_i2c_master *i2c = port->master;

	rc = fsi_i2c_read_reg(i2c->fsi, I2C_FSI_MODE, &mode);
	if (rc)
		return;

	mode |= I2C_MODE_DIAG;
	fsi_i2c_write_reg(i2c->fsi, I2C_FSI_MODE, &mode);
}

static void fsi_i2c_unprepare_recovery(struct i2c_adapter *adap)
{
	int rc;
	u32 mode;
	struct fsi_i2c_port *port = adap->algo_data;
	struct fsi_i2c_master *i2c = port->master;

	rc = fsi_i2c_read_reg(i2c->fsi, I2C_FSI_MODE, &mode);
	if (rc)
		return;

	mode &= ~I2C_MODE_DIAG;
	fsi_i2c_write_reg(i2c->fsi, I2C_FSI_MODE, &mode);
}

static int fsi_i2c_reset_bus(struct fsi_i2c_master *i2c,
			     struct fsi_i2c_port *port)
{
	int rc;
	u32 stat, dummy = 0;

	/* force bus reset, ignore errors */
	i2c_recover_bus(&port->adapter);

	/* reset errors */
	rc = fsi_i2c_write_reg(i2c->fsi, I2C_FSI_RESET_ERR, &dummy);
	if (rc)
		return rc;

	/* wait for command complete */
	usleep_range(I2C_RESET_SLEEP_MIN_US, I2C_RESET_SLEEP_MAX_US);

	rc = fsi_i2c_read_reg(i2c->fsi, I2C_FSI_STAT, &stat);
	if (rc)
		return rc;

	if (stat & I2C_STAT_CMD_COMP)
		return 0;

	/* failed to get command complete; reset engine again */
	rc = fsi_i2c_write_reg(i2c->fsi, I2C_FSI_RESET_I2C, &dummy);
	if (rc)
		return rc;

	/* re-init engine again */
	return fsi_i2c_dev_init(i2c);
}

static int fsi_i2c_reset_engine(struct fsi_i2c_master *i2c, u16 port)
{
	int rc;
	u32 mode, dummy = 0;

	/* reset engine */
	rc = fsi_i2c_write_reg(i2c->fsi, I2C_FSI_RESET_I2C, &dummy);
	if (rc)
		return rc;

	/* re-init engine */
	rc = fsi_i2c_dev_init(i2c);
	if (rc)
		return rc;

	rc = fsi_i2c_read_reg(i2c->fsi, I2C_FSI_MODE, &mode);
	if (rc)
		return rc;

	/* set port; default after reset is 0 */
	if (port) {
		mode &= ~I2C_MODE_PORT;
		mode |= FIELD_PREP(I2C_MODE_PORT, port);
		rc = fsi_i2c_write_reg(i2c->fsi, I2C_FSI_MODE, &mode);
		if (rc)
			return rc;
	}

	/* reset busy register; hw workaround */
	dummy = I2C_PORT_BUSY_RESET;
	rc = fsi_i2c_write_reg(i2c->fsi, I2C_FSI_PORT_BUSY, &dummy);
	if (rc)
		return rc;

	return 0;
}

static int fsi_i2c_abort(struct fsi_i2c_port *port, u32 status)
{
	int rc;
	unsigned long start;
	u32 cmd = I2C_CMD_WITH_STOP;
	u32 stat;
	struct fsi_i2c_master *i2c = port->master;
	struct fsi_device *fsi = i2c->fsi;

	rc = fsi_i2c_reset_engine(i2c, port->port);
	if (rc)
		return rc;

	rc = fsi_i2c_read_reg(fsi, I2C_FSI_STAT, &stat);
	if (rc)
		return rc;

	/* if sda is low, peform full bus reset */
	if (!(stat & I2C_STAT_SDA_IN)) {
		rc = fsi_i2c_reset_bus(i2c, port);
		if (rc)
			return rc;
	}

	/* skip final stop command for these errors */
	if (status & (I2C_STAT_PARITY | I2C_STAT_LOST_ARB | I2C_STAT_STOP_ERR))
		return 0;

	/* write stop command */
	rc = fsi_i2c_write_reg(fsi, I2C_FSI_CMD, &cmd);
	if (rc)
		return rc;

	/* wait until we see command complete in the master */
	start = jiffies;

	do {
		rc = fsi_i2c_read_reg(fsi, I2C_FSI_STAT, &status);
		if (rc)
			return rc;

		if (status & I2C_STAT_CMD_COMP)
			return 0;

		usleep_range(I2C_CMD_SLEEP_MIN_US, I2C_CMD_SLEEP_MAX_US);
	} while (time_after(start + I2C_ABORT_TIMEOUT, jiffies));

	return -ETIMEDOUT;
}

static int fsi_i2c_handle_status(struct fsi_i2c_port *port,
				 struct i2c_msg *msg, u32 status)
{
	int rc;
	u8 fifo_count;

	if (status & I2C_STAT_ERR) {
		rc = fsi_i2c_abort(port, status);
		if (rc)
			return rc;

		if (status & I2C_STAT_INV_CMD)
			return -EINVAL;

		if (status & (I2C_STAT_PARITY | I2C_STAT_BE_OVERRUN |
		    I2C_STAT_BE_ACCESS))
			return -EPROTO;

		if (status & I2C_STAT_NACK)
			return -ENXIO;

		if (status & I2C_STAT_LOST_ARB)
			return -EAGAIN;

		if (status & I2C_STAT_STOP_ERR)
			return -EBADMSG;

		return -EIO;
	}

	if (status & I2C_STAT_DAT_REQ) {
		fifo_count = FIELD_GET(I2C_STAT_FIFO_COUNT, status);

		if (msg->flags & I2C_M_RD)
			return fsi_i2c_read_fifo(port, msg, fifo_count);

		return fsi_i2c_write_fifo(port, msg, fifo_count);
	}

	if (status & I2C_STAT_CMD_COMP) {
		if (port->xfrd < msg->len)
			return -ENODATA;

		return msg->len;
	}

	return 0;
}

static int fsi_i2c_wait(struct fsi_i2c_port *port, struct i2c_msg *msg,
			unsigned long timeout)
{
	u32 status = 0;
	int rc;
	unsigned long start = jiffies;

	do {
		rc = fsi_i2c_read_reg(port->master->fsi, I2C_FSI_STAT,
				      &status);
		if (rc)
			return rc;

		if (status & I2C_STAT_ANY_RESP) {
			rc = fsi_i2c_handle_status(port, msg, status);
			if (rc < 0)
				return rc;

			/* cmd complete and all data xfrd */
			if (rc == msg->len)
				return 0;

			/* need to xfr more data, but maybe don't need wait */
			continue;
		}

		usleep_range(I2C_CMD_SLEEP_MIN_US, I2C_CMD_SLEEP_MAX_US);
	} while (time_after(start + timeout, jiffies));

	return -ETIMEDOUT;
}

static int fsi_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			int num)
{
	int i, rc;
	unsigned long start_time;
	struct fsi_i2c_port *port = adap->algo_data;
	struct fsi_i2c_master *master = port->master;
	struct i2c_msg *msg;

	mutex_lock(&master->lock);

	rc = fsi_i2c_set_port(port);
	if (rc)
		goto unlock;

	for (i = 0; i < num; i++) {
		msg = msgs + i;
		start_time = jiffies;

		rc = fsi_i2c_start(port, msg, i == num - 1);
		if (rc)
			goto unlock;

		rc = fsi_i2c_wait(port, msg,
				  adap->timeout - (jiffies - start_time));
		if (rc)
			goto unlock;
	}

unlock:
	mutex_unlock(&master->lock);
	return rc ? : num;
}

static u32 fsi_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_PROTOCOL_MANGLING |
		I2C_FUNC_SMBUS_EMUL | I2C_FUNC_SMBUS_BLOCK_DATA;
}

static struct i2c_bus_recovery_info fsi_i2c_bus_recovery_info = {
	.recover_bus = i2c_generic_scl_recovery,
	.get_scl = fsi_i2c_get_scl,
	.set_scl = fsi_i2c_set_scl,
	.get_sda = fsi_i2c_get_sda,
	.set_sda = fsi_i2c_set_sda,
	.prepare_recovery = fsi_i2c_prepare_recovery,
	.unprepare_recovery = fsi_i2c_unprepare_recovery,
};

static const struct i2c_algorithm fsi_i2c_algorithm = {
	.master_xfer = fsi_i2c_xfer,
	.functionality = fsi_i2c_functionality,
};

static struct device_node *fsi_i2c_find_port_of_node(struct device_node *fsi,
						     int port)
{
	struct device_node *np;
	u32 port_no;
	int rc;

	for_each_child_of_node(fsi, np) {
		rc = of_property_read_u32(np, "reg", &port_no);
		if (!rc && port_no == port)
			return np;
	}

	return NULL;
}

static int fsi_i2c_probe(struct device *dev)
{
	struct fsi_i2c_master *i2c;
	struct fsi_i2c_port *port;
	struct device_node *np;
	u32 port_no, ports, stat;
	int rc;

	i2c = devm_kzalloc(dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	mutex_init(&i2c->lock);
	i2c->fsi = to_fsi_dev(dev);
	INIT_LIST_HEAD(&i2c->ports);

	rc = fsi_i2c_dev_init(i2c);
	if (rc)
		return rc;

	rc = fsi_i2c_read_reg(i2c->fsi, I2C_FSI_STAT, &stat);
	if (rc)
		return rc;

	ports = FIELD_GET(I2C_STAT_MAX_PORT, stat) + 1;
	dev_dbg(dev, "I2C master has %d ports\n", ports);

	for (port_no = 0; port_no < ports; port_no++) {
		np = fsi_i2c_find_port_of_node(dev->of_node, port_no);
		if (np && !of_device_is_available(np))
			continue;

		port = kzalloc(sizeof(*port), GFP_KERNEL);
		if (!port) {
			of_node_put(np);
			break;
		}

		port->master = i2c;
		port->port = port_no;

		port->adapter.owner = THIS_MODULE;
		port->adapter.dev.of_node = np;
		port->adapter.dev.parent = dev;
		port->adapter.algo = &fsi_i2c_algorithm;
		port->adapter.bus_recovery_info = &fsi_i2c_bus_recovery_info;
		port->adapter.algo_data = port;

		snprintf(port->adapter.name, sizeof(port->adapter.name),
			 "i2c_bus-%u", port_no);

		rc = i2c_add_adapter(&port->adapter);
		if (rc < 0) {
			dev_err(dev, "Failed to register adapter: %d\n", rc);
			kfree(port);
			continue;
		}

		list_add(&port->list, &i2c->ports);
	}

	dev_set_drvdata(dev, i2c);

	return 0;
}

static int fsi_i2c_remove(struct device *dev)
{
	struct fsi_i2c_master *i2c = dev_get_drvdata(dev);
	struct fsi_i2c_port *port, *tmp;

	list_for_each_entry_safe(port, tmp, &i2c->ports, list) {
		list_del(&port->list);
		i2c_del_adapter(&port->adapter);
		kfree(port);
	}

	return 0;
}

static const struct fsi_device_id fsi_i2c_ids[] = {
	{ FSI_ENGID_I2C, FSI_VERSION_ANY },
	{ }
};

static struct fsi_driver fsi_i2c_driver = {
	.id_table = fsi_i2c_ids,
	.drv = {
		.name = "i2c-fsi",
		.bus = &fsi_bus_type,
		.probe = fsi_i2c_probe,
		.remove = fsi_i2c_remove,
	},
};

module_fsi_driver(fsi_i2c_driver);

MODULE_AUTHOR("Eddie James <eajames@us.ibm.com>");
MODULE_DESCRIPTION("FSI attached I2C master");
MODULE_LICENSE("GPL");
