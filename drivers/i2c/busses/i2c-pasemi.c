/*
 * Copyright (C) 2006-2007 PA Semi, Inc
 *
 * SMBus host driver for PA Semi PWRficient
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/io.h>

static struct pci_driver pasemi_smb_driver;

struct pasemi_smbus {
	struct pci_dev		*dev;
	struct i2c_adapter	 adapter;
	unsigned long		 base;
	int			 size;
};

/* Register offsets */
#define REG_MTXFIFO	0x00
#define REG_MRXFIFO	0x04
#define REG_SMSTA	0x14
#define REG_CTL		0x1c

/* Register defs */
#define MTXFIFO_READ	0x00000400
#define MTXFIFO_STOP	0x00000200
#define MTXFIFO_START	0x00000100
#define MTXFIFO_DATA_M	0x000000ff

#define MRXFIFO_EMPTY	0x00000100
#define MRXFIFO_DATA_M	0x000000ff

#define SMSTA_XEN	0x08000000
#define SMSTA_MTN	0x00200000

#define CTL_MRR		0x00000400
#define CTL_MTR		0x00000200
#define CTL_CLK_M	0x000000ff

#define CLK_100K_DIV	84
#define CLK_400K_DIV	21

static inline void reg_write(struct pasemi_smbus *smbus, int reg, int val)
{
	dev_dbg(&smbus->dev->dev, "smbus write reg %lx val %08x\n",
		smbus->base + reg, val);
	outl(val, smbus->base + reg);
}

static inline int reg_read(struct pasemi_smbus *smbus, int reg)
{
	int ret;
	ret = inl(smbus->base + reg);
	dev_dbg(&smbus->dev->dev, "smbus read reg %lx val %08x\n",
		smbus->base + reg, ret);
	return ret;
}

#define TXFIFO_WR(smbus, reg)	reg_write((smbus), REG_MTXFIFO, (reg))
#define RXFIFO_RD(smbus)	reg_read((smbus), REG_MRXFIFO)

static void pasemi_smb_clear(struct pasemi_smbus *smbus)
{
	unsigned int status;

	status = reg_read(smbus, REG_SMSTA);
	reg_write(smbus, REG_SMSTA, status);
}

static unsigned int pasemi_smb_waitready(struct pasemi_smbus *smbus)
{
	int timeout = 10;
	unsigned int status;

	status = reg_read(smbus, REG_SMSTA);

	while (!(status & SMSTA_XEN) && timeout--) {
		msleep(1);
		status = reg_read(smbus, REG_SMSTA);
	}

	/* Got NACK? */
	if (status & SMSTA_MTN)
		return -ENXIO;

	if (timeout < 0) {
		dev_warn(&smbus->dev->dev, "Timeout, status 0x%08x\n", status);
		reg_write(smbus, REG_SMSTA, status);
		return -ETIME;
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

	TXFIFO_WR(smbus, MTXFIFO_START | (msg->addr << 1) | read);

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
	}

	return 0;

 reset_out:
	reg_write(smbus, REG_CTL, (CTL_MTR | CTL_MRR |
		  (CLK_100K_DIV & CTL_CLK_M)));
	return err;
}

static int pasemi_i2c_xfer(struct i2c_adapter *adapter,
			   struct i2c_msg *msgs, int num)
{
	struct pasemi_smbus *smbus = adapter->algo_data;
	int ret, i;

	pasemi_smb_clear(smbus);

	ret = 0;

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

	pasemi_smb_clear(smbus);

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
	reg_write(smbus, REG_CTL, (CTL_MTR | CTL_MRR |
		  (CLK_100K_DIV & CTL_CLK_M)));
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
	.master_xfer	= pasemi_i2c_xfer,
	.smbus_xfer	= pasemi_smb_xfer,
	.functionality	= pasemi_smb_func,
};

static int __devinit pasemi_smb_probe(struct pci_dev *dev,
				      const struct pci_device_id *id)
{
	struct pasemi_smbus *smbus;
	int error;

	if (!(pci_resource_flags(dev, 0) & IORESOURCE_IO))
		return -ENODEV;

	smbus = kzalloc(sizeof(struct pasemi_smbus), GFP_KERNEL);
	if (!smbus)
		return -ENOMEM;

	smbus->dev = dev;
	smbus->base = pci_resource_start(dev, 0);
	smbus->size = pci_resource_len(dev, 0);

	if (!request_region(smbus->base, smbus->size,
			    pasemi_smb_driver.name)) {
		error = -EBUSY;
		goto out_kfree;
	}

	smbus->adapter.owner = THIS_MODULE;
	snprintf(smbus->adapter.name, sizeof(smbus->adapter.name),
		 "PA Semi SMBus adapter at 0x%lx", smbus->base);
	smbus->adapter.class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	smbus->adapter.algo = &smbus_algorithm;
	smbus->adapter.algo_data = smbus;
	smbus->adapter.nr = PCI_FUNC(dev->devfn);

	/* set up the sysfs linkage to our parent device */
	smbus->adapter.dev.parent = &dev->dev;

	reg_write(smbus, REG_CTL, (CTL_MTR | CTL_MRR |
		  (CLK_100K_DIV & CTL_CLK_M)));

	error = i2c_add_numbered_adapter(&smbus->adapter);
	if (error)
		goto out_release_region;

	pci_set_drvdata(dev, smbus);

	return 0;

 out_release_region:
	release_region(smbus->base, smbus->size);
 out_kfree:
	kfree(smbus);
	return error;
}

static void __devexit pasemi_smb_remove(struct pci_dev *dev)
{
	struct pasemi_smbus *smbus = pci_get_drvdata(dev);

	i2c_del_adapter(&smbus->adapter);
	release_region(smbus->base, smbus->size);
	kfree(smbus);
}

static const struct pci_device_id pasemi_smb_ids[] = {
	{ PCI_DEVICE(0x1959, 0xa003) },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, pasemi_smb_ids);

static struct pci_driver pasemi_smb_driver = {
	.name		= "i2c-pasemi",
	.id_table	= pasemi_smb_ids,
	.probe		= pasemi_smb_probe,
	.remove		= __devexit_p(pasemi_smb_remove),
};

static int __init pasemi_smb_init(void)
{
	return pci_register_driver(&pasemi_smb_driver);
}

static void __exit pasemi_smb_exit(void)
{
	pci_unregister_driver(&pasemi_smb_driver);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR ("Olof Johansson <olof@lixom.net>");
MODULE_DESCRIPTION("PA Semi PWRficient SMBus driver");

module_init(pasemi_smb_init);
module_exit(pasemi_smb_exit);
