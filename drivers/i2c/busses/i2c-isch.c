// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Linux kernel driver for Intel SCH chipset SMBus
 *  - Based on i2c-piix4.c
 *  Copyright (c) 1998 - 2002 Frodo Looijaard <frodol@dds.nl> and
 *  Philip Edelbrock <phil@netroedge.com>
 *  - Intel SCH support
 *  Copyright (c) 2007 - 2008 Jacob Jun Pan <jacob.jun.pan@intel.com>
 */

/* Supports: Intel SCH chipsets (AF82US15W, AF82US15L, AF82UL11L) */

#include <linux/container_of.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gfp_types.h>
#include <linux/i2c.h>
#include <linux/iopoll.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sprintf.h>
#include <linux/stddef.h>
#include <linux/string_choices.h>
#include <linux/types.h>

/* SCH SMBus address offsets */
#define SMBHSTCNT	0x00
#define SMBHSTSTS	0x01
#define SMBHSTCLK	0x02
#define SMBHSTADD	0x04	/* TSA */
#define SMBHSTCMD	0x05
#define SMBHSTDAT0	0x06
#define SMBHSTDAT1	0x07
#define SMBBLKDAT	0x20

/* I2C constants */
#define SCH_QUICK		0x00
#define SCH_BYTE		0x01
#define SCH_BYTE_DATA		0x02
#define SCH_WORD_DATA		0x03
#define SCH_BLOCK_DATA		0x05

struct sch_i2c {
	struct i2c_adapter adapter;
	void __iomem *smba;
};

static int backbone_speed = 33000; /* backbone speed in kHz */
module_param(backbone_speed, int, 0600);
MODULE_PARM_DESC(backbone_speed, "Backbone speed in kHz, (default = 33000)");

static inline u8 sch_io_rd8(struct sch_i2c *priv, unsigned int offset)
{
	return ioread8(priv->smba + offset);
}

static inline void sch_io_wr8(struct sch_i2c *priv, unsigned int offset, u8 value)
{
	iowrite8(value, priv->smba + offset);
}

static inline u16 sch_io_rd16(struct sch_i2c *priv, unsigned int offset)
{
	return ioread16(priv->smba + offset);
}

static inline void sch_io_wr16(struct sch_i2c *priv, unsigned int offset, u16 value)
{
	iowrite16(value, priv->smba + offset);
}

/**
 * sch_transaction - Start the i2c transaction
 * @adap: the i2c adapter pointer
 *
 * The sch_access() will prepare the transaction and
 * this function will execute it.
 *
 * Return: 0 for success and others for failure.
 */
static int sch_transaction(struct i2c_adapter *adap)
{
	struct sch_i2c *priv = container_of(adap, struct sch_i2c, adapter);
	int temp;
	int rc;

	dev_dbg(&adap->dev,
		"Transaction (pre): CNT=%02x, CMD=%02x, ADD=%02x, DAT0=%02x, DAT1=%02x\n",
		sch_io_rd8(priv, SMBHSTCNT), sch_io_rd8(priv, SMBHSTCMD),
		sch_io_rd8(priv, SMBHSTADD),
		sch_io_rd8(priv, SMBHSTDAT0), sch_io_rd8(priv, SMBHSTDAT1));

	/* Make sure the SMBus host is ready to start transmitting */
	temp = sch_io_rd8(priv, SMBHSTSTS) & 0x0f;
	if (temp) {
		/* Can not be busy since we checked it in sch_access */
		if (temp & 0x01)
			dev_dbg(&adap->dev, "Completion (%02x). Clear...\n", temp);
		if (temp & 0x06)
			dev_dbg(&adap->dev, "SMBus error (%02x). Resetting...\n", temp);
		sch_io_wr8(priv, SMBHSTSTS, temp);
		temp = sch_io_rd8(priv, SMBHSTSTS) & 0x0f;
		if (temp) {
			dev_err(&adap->dev, "SMBus is not ready: (%02x)\n", temp);
			return -EAGAIN;
		}
	}

	/* Start the transaction by setting bit 4 */
	temp = sch_io_rd8(priv, SMBHSTCNT);
	temp |= 0x10;
	sch_io_wr8(priv, SMBHSTCNT, temp);

	rc = read_poll_timeout(sch_io_rd8, temp, !(temp & 0x08), 200, 500000, true, priv, SMBHSTSTS);
	/* If the SMBus is still busy, we give up */
	if (rc) {
		dev_err(&adap->dev, "SMBus Timeout!\n");
	} else if (temp & 0x04) {
		rc = -EIO;
		dev_dbg(&adap->dev, "Bus collision! SMBus may be locked until next hard reset. (sorry!)\n");
		/* Clock stops and target is stuck in mid-transmission */
	} else if (temp & 0x02) {
		rc = -EIO;
		dev_err(&adap->dev, "Error: no response!\n");
	} else if (temp & 0x01) {
		dev_dbg(&adap->dev, "Post complete!\n");
		sch_io_wr8(priv, SMBHSTSTS, temp & 0x0f);
		temp = sch_io_rd8(priv, SMBHSTSTS) & 0x07;
		if (temp & 0x06) {
			/* Completion clear failed */
			dev_dbg(&adap->dev,
				"Failed reset at end of transaction (%02x), Bus error!\n", temp);
		}
	} else {
		rc = -ENXIO;
		dev_dbg(&adap->dev, "No such address.\n");
	}
	dev_dbg(&adap->dev, "Transaction (post): CNT=%02x, CMD=%02x, ADD=%02x, DAT0=%02x, DAT1=%02x\n",
		sch_io_rd8(priv, SMBHSTCNT), sch_io_rd8(priv, SMBHSTCMD),
		sch_io_rd8(priv, SMBHSTADD),
		sch_io_rd8(priv, SMBHSTDAT0), sch_io_rd8(priv, SMBHSTDAT1));
	return rc;
}

/**
 * sch_access - the main access entry for i2c-sch access
 * @adap: the i2c adapter pointer
 * @addr: the i2c device bus address
 * @flags: I2C_CLIENT_* flags (usually zero or I2C_CLIENT_PEC)
 * @read_write: 0 for read and 1 for write
 * @command: Byte interpreted by slave, for protocols which use such bytes
 * @size: the i2c transaction type
 * @data: the union of transaction for data to be transferred or data read from bus
 *
 * Return: 0 for success and others for failure.
 */
static s32 sch_access(struct i2c_adapter *adap, u16 addr,
		 unsigned short flags, char read_write,
		 u8 command, int size, union i2c_smbus_data *data)
{
	struct sch_i2c *priv = container_of(adap, struct sch_i2c, adapter);
	int i, len, temp, rc;

	/* Make sure the SMBus host is not busy */
	temp = sch_io_rd8(priv, SMBHSTSTS) & 0x0f;
	if (temp & 0x08) {
		dev_dbg(&adap->dev, "SMBus busy (%02x)\n", temp);
		return -EAGAIN;
	}
	temp = sch_io_rd16(priv, SMBHSTCLK);
	if (!temp) {
		/*
		 * We can't determine if we have 33 or 25 MHz clock for
		 * SMBus, so expect 33 MHz and calculate a bus clock of
		 * 100 kHz. If we actually run at 25 MHz the bus will be
		 * run ~75 kHz instead which should do no harm.
		 */
		dev_notice(&adap->dev, "Clock divider uninitialized. Setting defaults\n");
		sch_io_wr16(priv, SMBHSTCLK, backbone_speed / (4 * 100));
	}

	dev_dbg(&adap->dev, "access size: %d %s\n", size, str_read_write(read_write));
	switch (size) {
	case I2C_SMBUS_QUICK:
		sch_io_wr8(priv, SMBHSTADD, (addr << 1) | read_write);
		size = SCH_QUICK;
		break;
	case I2C_SMBUS_BYTE:
		sch_io_wr8(priv, SMBHSTADD, (addr << 1) | read_write);
		if (read_write == I2C_SMBUS_WRITE)
			sch_io_wr8(priv, SMBHSTCMD, command);
		size = SCH_BYTE;
		break;
	case I2C_SMBUS_BYTE_DATA:
		sch_io_wr8(priv, SMBHSTADD, (addr << 1) | read_write);
		sch_io_wr8(priv, SMBHSTCMD, command);
		if (read_write == I2C_SMBUS_WRITE)
			sch_io_wr8(priv, SMBHSTDAT0, data->byte);
		size = SCH_BYTE_DATA;
		break;
	case I2C_SMBUS_WORD_DATA:
		sch_io_wr8(priv, SMBHSTADD, (addr << 1) | read_write);
		sch_io_wr8(priv, SMBHSTCMD, command);
		if (read_write == I2C_SMBUS_WRITE) {
			sch_io_wr8(priv, SMBHSTDAT0, data->word >> 0);
			sch_io_wr8(priv, SMBHSTDAT1, data->word >> 8);
		}
		size = SCH_WORD_DATA;
		break;
	case I2C_SMBUS_BLOCK_DATA:
		sch_io_wr8(priv, SMBHSTADD, (addr << 1) | read_write);
		sch_io_wr8(priv, SMBHSTCMD, command);
		if (read_write == I2C_SMBUS_WRITE) {
			len = data->block[0];
			if (len == 0 || len > I2C_SMBUS_BLOCK_MAX)
				return -EINVAL;
			sch_io_wr8(priv, SMBHSTDAT0, len);
			for (i = 1; i <= len; i++)
				sch_io_wr8(priv, SMBBLKDAT + i - 1, data->block[i]);
		}
		size = SCH_BLOCK_DATA;
		break;
	default:
		dev_warn(&adap->dev, "Unsupported transaction %d\n", size);
		return -EOPNOTSUPP;
	}
	dev_dbg(&adap->dev, "write size %d to 0x%04x\n", size, SMBHSTCNT);

	temp = sch_io_rd8(priv, SMBHSTCNT);
	temp = (temp & 0xb0) | (size & 0x7);
	sch_io_wr8(priv, SMBHSTCNT, temp);

	rc = sch_transaction(adap);
	if (rc)	/* Error in transaction */
		return rc;

	if ((read_write == I2C_SMBUS_WRITE) || (size == SCH_QUICK))
		return 0;

	switch (size) {
	case SCH_BYTE:
	case SCH_BYTE_DATA:
		data->byte = sch_io_rd8(priv, SMBHSTDAT0);
		break;
	case SCH_WORD_DATA:
		data->word = (sch_io_rd8(priv, SMBHSTDAT0) << 0) +
			     (sch_io_rd8(priv, SMBHSTDAT1) << 8);
		break;
	case SCH_BLOCK_DATA:
		data->block[0] = sch_io_rd8(priv, SMBHSTDAT0);
		if (data->block[0] == 0 || data->block[0] > I2C_SMBUS_BLOCK_MAX)
			return -EPROTO;
		for (i = 1; i <= data->block[0]; i++)
			data->block[i] = sch_io_rd8(priv, SMBBLKDAT + i - 1);
		break;
	}
	return 0;
}

static u32 sch_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
	    I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
	    I2C_FUNC_SMBUS_BLOCK_DATA;
}

static const struct i2c_algorithm smbus_algorithm = {
	.smbus_xfer	= sch_access,
	.functionality	= sch_func,
};

static int smbus_sch_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sch_i2c *priv;
	struct resource *res;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!res)
		return -EBUSY;

	priv->smba = devm_ioport_map(dev, res->start, resource_size(res));
	if (!priv->smba)
		return dev_err_probe(dev, -EBUSY, "SMBus region %pR already in use!\n", res);

	/* Set up the sysfs linkage to our parent device */
	priv->adapter.dev.parent = dev;
	priv->adapter.owner = THIS_MODULE;
	priv->adapter.class = I2C_CLASS_HWMON;
	priv->adapter.algo = &smbus_algorithm;

	snprintf(priv->adapter.name, sizeof(priv->adapter.name),
		 "SMBus SCH adapter at %04x", (unsigned short)res->start);

	return devm_i2c_add_adapter(dev, &priv->adapter);
}

static struct platform_driver smbus_sch_driver = {
	.driver = {
		.name = "isch_smbus",
	},
	.probe		= smbus_sch_probe,
};

module_platform_driver(smbus_sch_driver);

MODULE_AUTHOR("Jacob Pan <jacob.jun.pan@intel.com>");
MODULE_DESCRIPTION("Intel SCH SMBus driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:isch_smbus");
