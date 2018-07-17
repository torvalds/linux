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
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fsi.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>

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
#define I2C_STAT_MAX_PORT	GENMASK(19, 16)
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

struct fsi_i2c_master {
	struct fsi_device	*fsi;
	u8			fifo_size;
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

static int fsi_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			int num)
{
	return -EOPNOTSUPP;
}

static u32 fsi_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_PROTOCOL_MANGLING |
		I2C_FUNC_SMBUS_EMUL | I2C_FUNC_SMBUS_BLOCK_DATA;
}

static const struct i2c_algorithm fsi_i2c_algorithm = {
	.master_xfer = fsi_i2c_xfer,
	.functionality = fsi_i2c_functionality,
};

static int fsi_i2c_probe(struct device *dev)
{
	struct fsi_i2c_master *i2c;
	int rc;

	i2c = devm_kzalloc(dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	i2c->fsi = to_fsi_dev(dev);

	rc = fsi_i2c_dev_init(i2c);
	if (rc)
		return rc;

	dev_set_drvdata(dev, i2c);

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
	},
};

module_fsi_driver(fsi_i2c_driver);

MODULE_AUTHOR("Eddie James <eajames@us.ibm.com>");
MODULE_DESCRIPTION("FSI attached I2C master");
MODULE_LICENSE("GPL");
