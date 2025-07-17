// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2006-2007 PA Semi, Inc
 *
 * SMBus host driver for PA Semi PWRficient
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/stddef.h>

#include "i2c-pasemi-core.h"

/* Register offsets */
#define REG_MTXFIFO	0x00
#define REG_MRXFIFO	0x04
#define REG_XFSTA	0x0c
#define REG_SMSTA	0x14
#define REG_IMASK	0x18
#define REG_CTL		0x1c
#define REG_REV		0x28

/* Register defs */
#define MTXFIFO_READ	BIT(10)
#define MTXFIFO_STOP	BIT(9)
#define MTXFIFO_START	BIT(8)
#define MTXFIFO_DATA_M	GENMASK(7, 0)

#define MRXFIFO_EMPTY	BIT(8)
#define MRXFIFO_DATA_M	GENMASK(7, 0)

#define SMSTA_XIP	BIT(28)
#define SMSTA_XEN	BIT(27)
#define SMSTA_JMD	BIT(25)
#define SMSTA_JAM	BIT(24)
#define SMSTA_MTO	BIT(23)
#define SMSTA_MTA	BIT(22)
#define SMSTA_MTN	BIT(21)
#define SMSTA_MRNE	BIT(19)
#define SMSTA_MTE	BIT(16)
#define SMSTA_TOM	BIT(6)

#define CTL_EN		BIT(11)
#define CTL_MRR		BIT(10)
#define CTL_MTR		BIT(9)
#define CTL_UJM		BIT(8)
#define CTL_CLK_M	GENMASK(7, 0)

/*
 * The hardware (supposedly) has a 25ms timeout for clock stretching, thus
 * use 100ms here which should be plenty.
 */
#define PASEMI_TRANSFER_TIMEOUT_MS	100

static inline void reg_write(struct pasemi_smbus *smbus, int reg, int val)
{
	dev_dbg(smbus->dev, "smbus write reg %x val %08x\n", reg, val);
	iowrite32(val, smbus->ioaddr + reg);
}

static inline int reg_read(struct pasemi_smbus *smbus, int reg)
{
	int ret;
	ret = ioread32(smbus->ioaddr + reg);
	dev_dbg(smbus->dev, "smbus read reg %x val %08x\n", reg, ret);
	return ret;
}

#define TXFIFO_WR(smbus, reg)	reg_write((smbus), REG_MTXFIFO, (reg))
#define RXFIFO_RD(smbus)	reg_read((smbus), REG_MRXFIFO)

static void pasemi_reset(struct pasemi_smbus *smbus)
{
	u32 val = (CTL_MTR | CTL_MRR | CTL_UJM | (smbus->clk_div & CTL_CLK_M));

	if (smbus->hw_rev >= 6)
		val |= CTL_EN;

	reg_write(smbus, REG_CTL, val);
	reinit_completion(&smbus->irq_completion);
}

static int pasemi_smb_clear(struct pasemi_smbus *smbus)
{
	unsigned int status;
	int ret;

	/* First wait for the bus to go idle */
	ret = readx_poll_timeout(ioread32, smbus->ioaddr + REG_SMSTA,
				 status, !(status & (SMSTA_XIP | SMSTA_JAM)),
				 USEC_PER_MSEC,
				 USEC_PER_MSEC * PASEMI_TRANSFER_TIMEOUT_MS);

	if (ret < 0) {
		dev_err(smbus->dev, "Bus is still stuck (status 0x%08x xfstatus 0x%08x)\n",
			 status, reg_read(smbus, REG_XFSTA));
		return -EIO;
	}

	/* If any badness happened or there is data in the FIFOs, reset the FIFOs */
	if ((status & (SMSTA_MRNE | SMSTA_JMD | SMSTA_MTO | SMSTA_TOM | SMSTA_MTN | SMSTA_MTA)) ||
	    !(status & SMSTA_MTE)) {
		dev_warn(smbus->dev, "Issuing reset due to status 0x%08x (xfstatus 0x%08x)\n",
			 status, reg_read(smbus, REG_XFSTA));
		pasemi_reset(smbus);
	}

	/* Clear the flags */
	reg_write(smbus, REG_SMSTA, status);

	return 0;
}

static int pasemi_smb_waitready(struct pasemi_smbus *smbus)
{
	unsigned int status;

	if (smbus->use_irq) {
		reinit_completion(&smbus->irq_completion);
		reg_write(smbus, REG_IMASK, SMSTA_XEN | SMSTA_MTN);
		int ret = wait_for_completion_timeout(
				&smbus->irq_completion,
				msecs_to_jiffies(PASEMI_TRANSFER_TIMEOUT_MS));
		reg_write(smbus, REG_IMASK, 0);
		status = reg_read(smbus, REG_SMSTA);

		if (ret < 0) {
			dev_err(smbus->dev,
				"Completion wait failed with %d, status 0x%08x\n",
				ret, status);
			return ret;
		} else if (ret == 0) {
			dev_err(smbus->dev, "Timeout, status 0x%08x\n", status);
			return -ETIME;
		}
	} else {
		int ret = readx_poll_timeout(
				ioread32, smbus->ioaddr + REG_SMSTA,
				status, status & SMSTA_XEN,
				USEC_PER_MSEC,
				USEC_PER_MSEC * PASEMI_TRANSFER_TIMEOUT_MS);

		if (ret < 0) {
			dev_err(smbus->dev, "Timeout, status 0x%08x\n", status);
			return -ETIME;
		}
	}

	/* Controller timeout? */
	if (status & SMSTA_TOM) {
		dev_err(smbus->dev, "Controller timeout, status 0x%08x\n", status);
		return -EIO;
	}

	/* Peripheral timeout? */
	if (status & SMSTA_MTO) {
		dev_err(smbus->dev, "Peripheral timeout, status 0x%08x\n", status);
		return -ETIME;
	}

	/* Still stuck in a transaction? */
	if (status & SMSTA_XIP) {
		dev_err(smbus->dev, "Bus stuck, status 0x%08x\n", status);
		return -EIO;
	}

	/* Arbitration loss? */
	if (status & SMSTA_MTA) {
		dev_err(smbus->dev, "Arbitration loss, status 0x%08x\n", status);
		return -EBUSY;
	}

	/* Got NACK? */
	if (status & SMSTA_MTN) {
		dev_err(smbus->dev, "NACK, status 0x%08x\n", status);
		return -ENXIO;
	}

	/* Clear XEN */
	reg_write(smbus, REG_SMSTA, SMSTA_XEN);

	return 0;
}

static int pasemi_i2c_xfer_msg(struct i2c_adapter *adapter,
			       struct i2c_msg *msg, int stop)
{
	struct pasemi_smbus *smbus = adapter->algo_data;
	int read, i, err;
	u32 rd;

	read = msg->flags & I2C_M_RD ? 1 : 0;

	TXFIFO_WR(smbus, MTXFIFO_START | i2c_8bit_addr_from_msg(msg));

	if (read) {
		TXFIFO_WR(smbus, msg->len | MTXFIFO_READ |
				 (stop ? MTXFIFO_STOP : 0));

		err = pasemi_smb_waitready(smbus);
		if (err)
			goto reset_out;

		for (i = 0; i < msg->len; i++) {
			rd = RXFIFO_RD(smbus);
			if (rd & MRXFIFO_EMPTY) {
				err = -ENODATA;
				goto reset_out;
			}
			msg->buf[i] = rd & MRXFIFO_DATA_M;
		}
	} else {
		for (i = 0; i < msg->len - 1; i++)
			TXFIFO_WR(smbus, msg->buf[i]);

		TXFIFO_WR(smbus, msg->buf[msg->len-1] |
			  (stop ? MTXFIFO_STOP : 0));

		if (stop) {
			err = pasemi_smb_waitready(smbus);
			if (err)
				goto reset_out;
		}
	}

	return 0;

 reset_out:
	pasemi_reset(smbus);
	return err;
}

static int pasemi_i2c_xfer(struct i2c_adapter *adapter,
			   struct i2c_msg *msgs, int num)
{
	struct pasemi_smbus *smbus = adapter->algo_data;
	int ret, i;

	ret = pasemi_smb_clear(smbus);
	if (ret)
		return ret;

	for (i = 0; i < num && !ret; i++)
		ret = pasemi_i2c_xfer_msg(adapter, &msgs[i], (i == (num - 1)));

	return ret ? ret : num;
}

static int pasemi_smb_xfer(struct i2c_adapter *adapter,
		u16 addr, unsigned short flags, char read_write, u8 command,
		int size, union i2c_smbus_data *data)
{
	struct pasemi_smbus *smbus = adapter->algo_data;
	unsigned int rd;
	int read_flag, err;
	int len = 0, i;

	/* All our ops take 8-bit shifted addresses */
	addr <<= 1;
	read_flag = read_write == I2C_SMBUS_READ;

	err = pasemi_smb_clear(smbus);
	if (err)
		return err;

	switch (size) {
	case I2C_SMBUS_QUICK:
		TXFIFO_WR(smbus, addr | read_flag | MTXFIFO_START |
			  MTXFIFO_STOP);
		break;
	case I2C_SMBUS_BYTE:
		TXFIFO_WR(smbus, addr | read_flag | MTXFIFO_START);
		if (read_write)
			TXFIFO_WR(smbus, 1 | MTXFIFO_STOP | MTXFIFO_READ);
		else
			TXFIFO_WR(smbus, MTXFIFO_STOP | command);
		break;
	case I2C_SMBUS_BYTE_DATA:
		TXFIFO_WR(smbus, addr | MTXFIFO_START);
		TXFIFO_WR(smbus, command);
		if (read_write) {
			TXFIFO_WR(smbus, addr | I2C_SMBUS_READ | MTXFIFO_START);
			TXFIFO_WR(smbus, 1 | MTXFIFO_READ | MTXFIFO_STOP);
		} else {
			TXFIFO_WR(smbus, MTXFIFO_STOP | data->byte);
		}
		break;
	case I2C_SMBUS_WORD_DATA:
		TXFIFO_WR(smbus, addr | MTXFIFO_START);
		TXFIFO_WR(smbus, command);
		if (read_write) {
			TXFIFO_WR(smbus, addr | I2C_SMBUS_READ | MTXFIFO_START);
			TXFIFO_WR(smbus, 2 | MTXFIFO_READ | MTXFIFO_STOP);
		} else {
			TXFIFO_WR(smbus, data->word & MTXFIFO_DATA_M);
			TXFIFO_WR(smbus, MTXFIFO_STOP | (data->word >> 8));
		}
		break;
	case I2C_SMBUS_BLOCK_DATA:
		TXFIFO_WR(smbus, addr | MTXFIFO_START);
		TXFIFO_WR(smbus, command);
		if (read_write) {
			TXFIFO_WR(smbus, addr | I2C_SMBUS_READ | MTXFIFO_START);
			TXFIFO_WR(smbus, 1 | MTXFIFO_READ);
			rd = RXFIFO_RD(smbus);
			len = min_t(u8, (rd & MRXFIFO_DATA_M),
				    I2C_SMBUS_BLOCK_MAX);
			TXFIFO_WR(smbus, len | MTXFIFO_READ |
					 MTXFIFO_STOP);
		} else {
			len = min_t(u8, data->block[0], I2C_SMBUS_BLOCK_MAX);
			TXFIFO_WR(smbus, len);
			for (i = 1; i < len; i++)
				TXFIFO_WR(smbus, data->block[i]);
			TXFIFO_WR(smbus, data->block[len] | MTXFIFO_STOP);
		}
		break;
	case I2C_SMBUS_PROC_CALL:
		read_write = I2C_SMBUS_READ;
		TXFIFO_WR(smbus, addr | MTXFIFO_START);
		TXFIFO_WR(smbus, command);
		TXFIFO_WR(smbus, data->word & MTXFIFO_DATA_M);
		TXFIFO_WR(smbus, (data->word >> 8) & MTXFIFO_DATA_M);
		TXFIFO_WR(smbus, addr | I2C_SMBUS_READ | MTXFIFO_START);
		TXFIFO_WR(smbus, 2 | MTXFIFO_STOP | MTXFIFO_READ);
		break;
	case I2C_SMBUS_BLOCK_PROC_CALL:
		len = min_t(u8, data->block[0], I2C_SMBUS_BLOCK_MAX - 1);
		read_write = I2C_SMBUS_READ;
		TXFIFO_WR(smbus, addr | MTXFIFO_START);
		TXFIFO_WR(smbus, command);
		TXFIFO_WR(smbus, len);
		for (i = 1; i <= len; i++)
			TXFIFO_WR(smbus, data->block[i]);
		TXFIFO_WR(smbus, addr | I2C_SMBUS_READ);
		TXFIFO_WR(smbus, MTXFIFO_READ | 1);
		rd = RXFIFO_RD(smbus);
		len = min_t(u8, (rd & MRXFIFO_DATA_M),
			    I2C_SMBUS_BLOCK_MAX - len);
		TXFIFO_WR(smbus, len | MTXFIFO_READ | MTXFIFO_STOP);
		break;

	default:
		dev_warn(&adapter->dev, "Unsupported transaction %d\n", size);
		return -EINVAL;
	}

	err = pasemi_smb_waitready(smbus);
	if (err)
		goto reset_out;

	if (read_write == I2C_SMBUS_WRITE)
		return 0;

	switch (size) {
	case I2C_SMBUS_BYTE:
	case I2C_SMBUS_BYTE_DATA:
		rd = RXFIFO_RD(smbus);
		if (rd & MRXFIFO_EMPTY) {
			err = -ENODATA;
			goto reset_out;
		}
		data->byte = rd & MRXFIFO_DATA_M;
		break;
	case I2C_SMBUS_WORD_DATA:
	case I2C_SMBUS_PROC_CALL:
		rd = RXFIFO_RD(smbus);
		if (rd & MRXFIFO_EMPTY) {
			err = -ENODATA;
			goto reset_out;
		}
		data->word = rd & MRXFIFO_DATA_M;
		rd = RXFIFO_RD(smbus);
		if (rd & MRXFIFO_EMPTY) {
			err = -ENODATA;
			goto reset_out;
		}
		data->word |= (rd & MRXFIFO_DATA_M) << 8;
		break;
	case I2C_SMBUS_BLOCK_DATA:
	case I2C_SMBUS_BLOCK_PROC_CALL:
		data->block[0] = len;
		for (i = 1; i <= len; i ++) {
			rd = RXFIFO_RD(smbus);
			if (rd & MRXFIFO_EMPTY) {
				err = -ENODATA;
				goto reset_out;
			}
			data->block[i] = rd & MRXFIFO_DATA_M;
		}
		break;
	}

	return 0;

 reset_out:
	pasemi_reset(smbus);
	return err;
}

static u32 pasemi_smb_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
	       I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
	       I2C_FUNC_SMBUS_BLOCK_DATA | I2C_FUNC_SMBUS_PROC_CALL |
	       I2C_FUNC_SMBUS_BLOCK_PROC_CALL | I2C_FUNC_I2C;
}

static const struct i2c_algorithm smbus_algorithm = {
	.xfer = pasemi_i2c_xfer,
	.smbus_xfer = pasemi_smb_xfer,
	.functionality = pasemi_smb_func,
};

int pasemi_i2c_common_probe(struct pasemi_smbus *smbus)
{
	int error;

	smbus->adapter.owner = THIS_MODULE;
	snprintf(smbus->adapter.name, sizeof(smbus->adapter.name),
		 "PA Semi SMBus adapter (%s)", dev_name(smbus->dev));
	smbus->adapter.algo = &smbus_algorithm;
	smbus->adapter.algo_data = smbus;

	/* set up the sysfs linkage to our parent device */
	smbus->adapter.dev.parent = smbus->dev;
	smbus->use_irq = 0;
	init_completion(&smbus->irq_completion);

	if (smbus->hw_rev != PASEMI_HW_REV_PCI)
		smbus->hw_rev = reg_read(smbus, REG_REV);

	reg_write(smbus, REG_IMASK, 0);

	pasemi_reset(smbus);

	error = devm_i2c_add_adapter(smbus->dev, &smbus->adapter);
	if (error)
		return error;

	return 0;
}
EXPORT_SYMBOL_GPL(pasemi_i2c_common_probe);

irqreturn_t pasemi_irq_handler(int irq, void *dev_id)
{
	struct pasemi_smbus *smbus = dev_id;

	reg_write(smbus, REG_IMASK, 0);
	complete(&smbus->irq_completion);
	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(pasemi_irq_handler);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Olof Johansson <olof@lixom.net>");
MODULE_DESCRIPTION("PA Semi PWRficient SMBus driver");
