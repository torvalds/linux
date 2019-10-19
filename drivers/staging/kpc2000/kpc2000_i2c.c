// SPDX-License-Identifier: GPL-2.0+
/*
 * KPC2000 i2c driver
 *
 * Adapted i2c-i801.c for use with Kadoka hardware.
 *
 * Copyright (C) 1998 - 2002
 *	Frodo Looijaard <frodol@dds.nl>,
 *	Philip Edelbrock <phil@netroedge.com>,
 *	Mark D. Studebaker <mdsxyz123@yahoo.com>
 * Copyright (C) 2007 - 2012
 *	Jean Delvare <khali@linux-fr.org>
 * Copyright (C) 2010 Intel Corporation
 *	David Woodhouse <dwmw2@infradead.org>
 * Copyright (C) 2014-2018 Daktronics
 *	Matt Sickler <matt.sickler@daktronics.com>,
 *	Jordon Hofer <jordon.hofer@daktronics.com>
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include "kpc.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Matt.Sickler@Daktronics.com");

struct i2c_device {
	unsigned long           smba;
	struct i2c_adapter      adapter;
	unsigned int            features;
};

/*****************************
 *** Part 1 - i2c Handlers ***
 *****************************/

#define REG_SIZE 8

/* I801 SMBus address offsets */
#define SMBHSTSTS(p)    ((0  * REG_SIZE) + (p)->smba)
#define SMBHSTCNT(p)    ((2  * REG_SIZE) + (p)->smba)
#define SMBHSTCMD(p)    ((3  * REG_SIZE) + (p)->smba)
#define SMBHSTADD(p)    ((4  * REG_SIZE) + (p)->smba)
#define SMBHSTDAT0(p)   ((5  * REG_SIZE) + (p)->smba)
#define SMBHSTDAT1(p)   ((6  * REG_SIZE) + (p)->smba)
#define SMBBLKDAT(p)    ((7  * REG_SIZE) + (p)->smba)
#define SMBPEC(p)       ((8  * REG_SIZE) + (p)->smba)   /* ICH3 and later */
#define SMBAUXSTS(p)    ((12 * REG_SIZE) + (p)->smba)   /* ICH4 and later */
#define SMBAUXCTL(p)    ((13 * REG_SIZE) + (p)->smba)   /* ICH4 and later */

/* PCI Address Constants */
#define SMBBAR      4
#define SMBHSTCFG   0x040

/* Host configuration bits for SMBHSTCFG */
#define SMBHSTCFG_HST_EN        1
#define SMBHSTCFG_SMB_SMI_EN    2
#define SMBHSTCFG_I2C_EN        4

/* Auxiliary control register bits, ICH4+ only */
#define SMBAUXCTL_CRC       1
#define SMBAUXCTL_E32B      2

/* kill bit for SMBHSTCNT */
#define SMBHSTCNT_KILL      2

/* Other settings */
#define MAX_RETRIES         400
#define ENABLE_INT9         0       /* set to 0x01 to enable - untested */

/* I801 command constants */
#define I801_QUICK              0x00
#define I801_BYTE               0x04
#define I801_BYTE_DATA          0x08
#define I801_WORD_DATA          0x0C
#define I801_PROC_CALL          0x10    /* unimplemented */
#define I801_BLOCK_DATA         0x14
#define I801_I2C_BLOCK_DATA     0x18    /* ICH5 and later */
#define I801_BLOCK_LAST         0x34
#define I801_I2C_BLOCK_LAST     0x38    /* ICH5 and later */
#define I801_START              0x40
#define I801_PEC_EN             0x80    /* ICH3 and later */

/* I801 Hosts Status register bits */
#define SMBHSTSTS_BYTE_DONE     0x80
#define SMBHSTSTS_INUSE_STS     0x40
#define SMBHSTSTS_SMBALERT_STS  0x20
#define SMBHSTSTS_FAILED        0x10
#define SMBHSTSTS_BUS_ERR       0x08
#define SMBHSTSTS_DEV_ERR       0x04
#define SMBHSTSTS_INTR          0x02
#define SMBHSTSTS_HOST_BUSY     0x01

#define STATUS_FLAGS        (SMBHSTSTS_BYTE_DONE | SMBHSTSTS_FAILED | SMBHSTSTS_BUS_ERR | SMBHSTSTS_DEV_ERR | SMBHSTSTS_INTR)

/* Older devices have their ID defined in <linux/pci_ids.h> */
#define PCI_DEVICE_ID_INTEL_COUGARPOINT_SMBUS       0x1c22
#define PCI_DEVICE_ID_INTEL_PATSBURG_SMBUS          0x1d22
/* Patsburg also has three 'Integrated Device Function' SMBus controllers */
#define PCI_DEVICE_ID_INTEL_PATSBURG_SMBUS_IDF0     0x1d70
#define PCI_DEVICE_ID_INTEL_PATSBURG_SMBUS_IDF1     0x1d71
#define PCI_DEVICE_ID_INTEL_PATSBURG_SMBUS_IDF2     0x1d72
#define PCI_DEVICE_ID_INTEL_PANTHERPOINT_SMBUS      0x1e22
#define PCI_DEVICE_ID_INTEL_DH89XXCC_SMBUS          0x2330
#define PCI_DEVICE_ID_INTEL_5_3400_SERIES_SMBUS     0x3b30
#define PCI_DEVICE_ID_INTEL_LYNXPOINT_SMBUS         0x8c22
#define PCI_DEVICE_ID_INTEL_LYNXPOINT_LP_SMBUS      0x9c22

#define FEATURE_SMBUS_PEC       BIT(0)
#define FEATURE_BLOCK_BUFFER    BIT(1)
#define FEATURE_BLOCK_PROC      BIT(2)
#define FEATURE_I2C_BLOCK_READ  BIT(3)
/* Not really a feature, but it's convenient to handle it as such */
#define FEATURE_IDF             BIT(15)

// FIXME!
#undef inb_p
#define inb_p(a) readq((void __iomem *)a)
#undef outb_p
#define outb_p(d, a) writeq(d, (void __iomem *)a)

/* Make sure the SMBus host is ready to start transmitting.
 * Return 0 if it is, -EBUSY if it is not.
 */
static int i801_check_pre(struct i2c_device *priv)
{
	int status;

	status = inb_p(SMBHSTSTS(priv));
	if (status & SMBHSTSTS_HOST_BUSY) {
		dev_err(&priv->adapter.dev, "SMBus is busy, can't use it! (status=%x)\n", status);
		return -EBUSY;
	}

	status &= STATUS_FLAGS;
	if (status) {
		//dev_dbg(&priv->adapter.dev, "Clearing status flags (%02x)\n", status);
		outb_p(status, SMBHSTSTS(priv));
		status = inb_p(SMBHSTSTS(priv)) & STATUS_FLAGS;
		if (status) {
			dev_err(&priv->adapter.dev, "Failed clearing status flags (%02x)\n", status);
			return -EBUSY;
		}
	}
	return 0;
}

/* Convert the status register to an error code, and clear it. */
static int i801_check_post(struct i2c_device *priv, int status, int timeout)
{
	int result = 0;

	/* If the SMBus is still busy, we give up */
	if (timeout) {
		dev_err(&priv->adapter.dev, "Transaction timeout\n");
		/* try to stop the current command */
		dev_dbg(&priv->adapter.dev, "Terminating the current operation\n");
		outb_p(inb_p(SMBHSTCNT(priv)) | SMBHSTCNT_KILL, SMBHSTCNT(priv));
		usleep_range(1000, 2000);
		outb_p(inb_p(SMBHSTCNT(priv)) & (~SMBHSTCNT_KILL), SMBHSTCNT(priv));

		/* Check if it worked */
		status = inb_p(SMBHSTSTS(priv));
		if ((status & SMBHSTSTS_HOST_BUSY) || !(status & SMBHSTSTS_FAILED))
			dev_err(&priv->adapter.dev, "Failed terminating the transaction\n");
		outb_p(STATUS_FLAGS, SMBHSTSTS(priv));
		return -ETIMEDOUT;
	}

	if (status & SMBHSTSTS_FAILED) {
		result = -EIO;
		dev_err(&priv->adapter.dev, "Transaction failed\n");
	}
	if (status & SMBHSTSTS_DEV_ERR) {
		result = -ENXIO;
		dev_dbg(&priv->adapter.dev, "No response\n");
	}
	if (status & SMBHSTSTS_BUS_ERR) {
		result = -EAGAIN;
		dev_dbg(&priv->adapter.dev, "Lost arbitration\n");
	}

	if (result) {
		/* Clear error flags */
		outb_p(status & STATUS_FLAGS, SMBHSTSTS(priv));
		status = inb_p(SMBHSTSTS(priv)) & STATUS_FLAGS;
		if (status)
			dev_warn(&priv->adapter.dev, "Failed clearing status flags at end of transaction (%02x)\n", status);
	}

	return result;
}

static int i801_transaction(struct i2c_device *priv, int xact)
{
	int status;
	int result;
	int timeout = 0;

	result = i801_check_pre(priv);
	if (result < 0)
		return result;
	/* the current contents of SMBHSTCNT can be overwritten, since PEC,
	 * INTREN, SMBSCMD are passed in xact
	 */
	outb_p(xact | I801_START, SMBHSTCNT(priv));

	/* We will always wait for a fraction of a second! */
	do {
		usleep_range(250, 500);
		status = inb_p(SMBHSTSTS(priv));
	} while ((status & SMBHSTSTS_HOST_BUSY) && (timeout++ < MAX_RETRIES));

	result = i801_check_post(priv, status, timeout > MAX_RETRIES);
	if (result < 0)
		return result;

	outb_p(SMBHSTSTS_INTR, SMBHSTSTS(priv));
	return 0;
}

/* wait for INTR bit as advised by Intel */
static void i801_wait_hwpec(struct i2c_device *priv)
{
	int timeout = 0;
	int status;

	do {
		usleep_range(250, 500);
		status = inb_p(SMBHSTSTS(priv));
	} while ((!(status & SMBHSTSTS_INTR)) && (timeout++ < MAX_RETRIES));

	if (timeout > MAX_RETRIES)
		dev_dbg(&priv->adapter.dev, "PEC Timeout!\n");

	outb_p(status, SMBHSTSTS(priv));
}

static int i801_block_transaction_by_block(struct i2c_device *priv, union i2c_smbus_data *data, char read_write, int hwpec)
{
	int i, len;
	int status;

	inb_p(SMBHSTCNT(priv)); /* reset the data buffer index */

	/* Use 32-byte buffer to process this transaction */
	if (read_write == I2C_SMBUS_WRITE) {
		len = data->block[0];
		outb_p(len, SMBHSTDAT0(priv));
		for (i = 0; i < len; i++)
			outb_p(data->block[i + 1], SMBBLKDAT(priv));
	}

	status = i801_transaction(priv, I801_BLOCK_DATA | ENABLE_INT9 | I801_PEC_EN * hwpec);
	if (status)
		return status;

	if (read_write == I2C_SMBUS_READ) {
		len = inb_p(SMBHSTDAT0(priv));
		if (len < 1 || len > I2C_SMBUS_BLOCK_MAX)
			return -EPROTO;

		data->block[0] = len;
		for (i = 0; i < len; i++)
			data->block[i + 1] = inb_p(SMBBLKDAT(priv));
	}
	return 0;
}

static int i801_block_transaction_byte_by_byte(struct i2c_device *priv, union i2c_smbus_data *data, char read_write, int command, int hwpec)
{
	int i, len;
	int smbcmd;
	int status;
	int result;
	int timeout;

	result = i801_check_pre(priv);
	if (result < 0)
		return result;

	len = data->block[0];

	if (read_write == I2C_SMBUS_WRITE) {
		outb_p(len, SMBHSTDAT0(priv));
		outb_p(data->block[1], SMBBLKDAT(priv));
	}

	for (i = 1; i <= len; i++) {
		if (i == len && read_write == I2C_SMBUS_READ) {
			if (command == I2C_SMBUS_I2C_BLOCK_DATA)
				smbcmd = I801_I2C_BLOCK_LAST;
			else
				smbcmd = I801_BLOCK_LAST;
		} else {
			if (command == I2C_SMBUS_I2C_BLOCK_DATA && read_write == I2C_SMBUS_READ)
				smbcmd = I801_I2C_BLOCK_DATA;
			else
				smbcmd = I801_BLOCK_DATA;
		}
		outb_p(smbcmd | ENABLE_INT9, SMBHSTCNT(priv));

		if (i == 1)
			outb_p(inb(SMBHSTCNT(priv)) | I801_START, SMBHSTCNT(priv));
		/* We will always wait for a fraction of a second! */
		timeout = 0;
		do {
			usleep_range(250, 500);
			status = inb_p(SMBHSTSTS(priv));
		} while ((!(status & SMBHSTSTS_BYTE_DONE)) && (timeout++ < MAX_RETRIES));

		result = i801_check_post(priv, status, timeout > MAX_RETRIES);
		if (result < 0)
			return result;
		if (i == 1 && read_write == I2C_SMBUS_READ && command != I2C_SMBUS_I2C_BLOCK_DATA) {
			len = inb_p(SMBHSTDAT0(priv));
			if (len < 1 || len > I2C_SMBUS_BLOCK_MAX) {
				dev_err(&priv->adapter.dev, "Illegal SMBus block read size %d\n", len);
				/* Recover */
				while (inb_p(SMBHSTSTS(priv)) & SMBHSTSTS_HOST_BUSY)
					outb_p(SMBHSTSTS_BYTE_DONE, SMBHSTSTS(priv));
				outb_p(SMBHSTSTS_INTR, SMBHSTSTS(priv));
				return -EPROTO;
			}
			data->block[0] = len;
		}

		/* Retrieve/store value in SMBBLKDAT */
		if (read_write == I2C_SMBUS_READ)
			data->block[i] = inb_p(SMBBLKDAT(priv));
		if (read_write == I2C_SMBUS_WRITE && i + 1 <= len)
			outb_p(data->block[i + 1], SMBBLKDAT(priv));
		/* signals SMBBLKDAT ready */
		outb_p(SMBHSTSTS_BYTE_DONE | SMBHSTSTS_INTR, SMBHSTSTS(priv));
	}

	return 0;
}

static int i801_set_block_buffer_mode(struct i2c_device *priv)
{
	outb_p(inb_p(SMBAUXCTL(priv)) | SMBAUXCTL_E32B, SMBAUXCTL(priv));
	if ((inb_p(SMBAUXCTL(priv)) & SMBAUXCTL_E32B) == 0)
		return -EIO;
	return 0;
}

/* Block transaction function */
static int i801_block_transaction(struct i2c_device *priv, union i2c_smbus_data *data, char read_write, int command, int hwpec)
{
	int result = 0;
	//unsigned char hostc;

	if (command == I2C_SMBUS_I2C_BLOCK_DATA) {
		if (read_write == I2C_SMBUS_WRITE) {
			/* set I2C_EN bit in configuration register */
			//TODO: Figure out the right thing to do here...
			//pci_read_config_byte(priv->pci_dev, SMBHSTCFG, &hostc);
			//pci_write_config_byte(priv->pci_dev, SMBHSTCFG, hostc | SMBHSTCFG_I2C_EN);
		} else if (!(priv->features & FEATURE_I2C_BLOCK_READ)) {
			dev_err(&priv->adapter.dev, "I2C block read is unsupported!\n");
			return -EOPNOTSUPP;
		}
	}

	if (read_write == I2C_SMBUS_WRITE || command == I2C_SMBUS_I2C_BLOCK_DATA) {
		if (data->block[0] < 1)
			data->block[0] = 1;
		if (data->block[0] > I2C_SMBUS_BLOCK_MAX)
			data->block[0] = I2C_SMBUS_BLOCK_MAX;
	} else {
		data->block[0] = 32;	/* max for SMBus block reads */
	}

	/* Experience has shown that the block buffer can only be used for
	 * SMBus (not I2C) block transactions, even though the datasheet
	 * doesn't mention this limitation.
	 */
	if ((priv->features & FEATURE_BLOCK_BUFFER) && command != I2C_SMBUS_I2C_BLOCK_DATA && i801_set_block_buffer_mode(priv) == 0)
		result = i801_block_transaction_by_block(priv, data, read_write, hwpec);
	else
		result = i801_block_transaction_byte_by_byte(priv, data, read_write, command, hwpec);
	if (result == 0 && hwpec)
		i801_wait_hwpec(priv);
	if (command == I2C_SMBUS_I2C_BLOCK_DATA && read_write == I2C_SMBUS_WRITE) {
		/* restore saved configuration register value */
		//TODO: Figure out the right thing to do here...
		//pci_write_config_byte(priv->pci_dev, SMBHSTCFG, hostc);
	}
	return result;
}

/* Return negative errno on error. */
static s32 i801_access(struct i2c_adapter *adap, u16 addr, unsigned short flags, char read_write, u8 command, int size, union i2c_smbus_data *data)
{
	int hwpec;
	int block = 0;
	int ret, xact = 0;
	struct i2c_device *priv = i2c_get_adapdata(adap);

	hwpec = (priv->features & FEATURE_SMBUS_PEC) && (flags & I2C_CLIENT_PEC) && size != I2C_SMBUS_QUICK && size != I2C_SMBUS_I2C_BLOCK_DATA;

	switch (size) {
	case I2C_SMBUS_QUICK:
		dev_dbg(&priv->adapter.dev, "  [acc] SMBUS_QUICK\n");
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01), SMBHSTADD(priv));
		xact = I801_QUICK;
		break;
	case I2C_SMBUS_BYTE:
		dev_dbg(&priv->adapter.dev, "  [acc] SMBUS_BYTE\n");

		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01), SMBHSTADD(priv));
		if (read_write == I2C_SMBUS_WRITE)
			outb_p(command, SMBHSTCMD(priv));
		xact = I801_BYTE;
		break;
	case I2C_SMBUS_BYTE_DATA:
		dev_dbg(&priv->adapter.dev, "  [acc] SMBUS_BYTE_DATA\n");
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01), SMBHSTADD(priv));
		outb_p(command, SMBHSTCMD(priv));
		if (read_write == I2C_SMBUS_WRITE)
			outb_p(data->byte, SMBHSTDAT0(priv));
		xact = I801_BYTE_DATA;
		break;
	case I2C_SMBUS_WORD_DATA:
		dev_dbg(&priv->adapter.dev, "  [acc] SMBUS_WORD_DATA\n");
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01), SMBHSTADD(priv));
		outb_p(command, SMBHSTCMD(priv));
		if (read_write == I2C_SMBUS_WRITE) {
			outb_p(data->word & 0xff, SMBHSTDAT0(priv));
			outb_p((data->word & 0xff00) >> 8, SMBHSTDAT1(priv));
		}
		xact = I801_WORD_DATA;
		break;
	case I2C_SMBUS_BLOCK_DATA:
		dev_dbg(&priv->adapter.dev, "  [acc] SMBUS_BLOCK_DATA\n");
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01), SMBHSTADD(priv));
		outb_p(command, SMBHSTCMD(priv));
		block = 1;
		break;
	case I2C_SMBUS_I2C_BLOCK_DATA:
		dev_dbg(&priv->adapter.dev, "  [acc] SMBUS_I2C_BLOCK_DATA\n");
		/* NB: page 240 of ICH5 datasheet shows that the R/#W
		 * bit should be cleared here, even when reading
		 */
		outb_p((addr & 0x7f) << 1, SMBHSTADD(priv));
		if (read_write == I2C_SMBUS_READ) {
			/* NB: page 240 of ICH5 datasheet also shows
			 * that DATA1 is the cmd field when reading
			 */
			outb_p(command, SMBHSTDAT1(priv));
		} else {
			outb_p(command, SMBHSTCMD(priv));
		}
		block = 1;
		break;
	default:
		dev_dbg(&priv->adapter.dev, "  [acc] Unsupported transaction %d\n", size);
		return -EOPNOTSUPP;
	}

	if (hwpec) { /* enable/disable hardware PEC */
		dev_dbg(&priv->adapter.dev, "  [acc] hwpec: yes\n");
		outb_p(inb_p(SMBAUXCTL(priv)) | SMBAUXCTL_CRC, SMBAUXCTL(priv));
	} else {
		dev_dbg(&priv->adapter.dev, "  [acc] hwpec: no\n");
		outb_p(inb_p(SMBAUXCTL(priv)) & (~SMBAUXCTL_CRC), SMBAUXCTL(priv));
	}

	if (block) {
		//ret = 0;
		dev_dbg(&priv->adapter.dev, "  [acc] block: yes\n");
		ret = i801_block_transaction(priv, data, read_write, size, hwpec);
	} else {
		dev_dbg(&priv->adapter.dev, "  [acc] block: no\n");
		ret = i801_transaction(priv, xact | ENABLE_INT9);
	}

	/* Some BIOSes don't like it when PEC is enabled at reboot or resume
	 * time, so we forcibly disable it after every transaction. Turn off
	 * E32B for the same reason.
	 */
	if (hwpec || block) {
		dev_dbg(&priv->adapter.dev, "  [acc] hwpec || block\n");
		outb_p(inb_p(SMBAUXCTL(priv)) & ~(SMBAUXCTL_CRC | SMBAUXCTL_E32B), SMBAUXCTL(priv));
	}
	if (block) {
		dev_dbg(&priv->adapter.dev, "  [acc] block\n");
		return ret;
	}
	if (ret) {
		dev_dbg(&priv->adapter.dev, "  [acc] ret %d\n", ret);
		return ret;
	}
	if ((read_write == I2C_SMBUS_WRITE) || (xact == I801_QUICK)) {
		dev_dbg(&priv->adapter.dev, "  [acc] I2C_SMBUS_WRITE || I801_QUICK  -> ret 0\n");
		return 0;
	}

	switch (xact & 0x7f) {
	case I801_BYTE:  /* Result put in SMBHSTDAT0 */
	case I801_BYTE_DATA:
		dev_dbg(&priv->adapter.dev, "  [acc] I801_BYTE or I801_BYTE_DATA\n");
		data->byte = inb_p(SMBHSTDAT0(priv));
		break;
	case I801_WORD_DATA:
		dev_dbg(&priv->adapter.dev, "  [acc] I801_WORD_DATA\n");
		data->word = inb_p(SMBHSTDAT0(priv)) + (inb_p(SMBHSTDAT1(priv)) << 8);
		break;
	}
	return 0;
}

static u32 i801_func(struct i2c_adapter *adapter)
{
	struct i2c_device *priv = i2c_get_adapdata(adapter);

	/* original settings
	 * u32 f = I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
	 * I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
	 * I2C_FUNC_SMBUS_BLOCK_DATA | I2C_FUNC_SMBUS_WRITE_I2C_BLOCK |
	 * ((priv->features & FEATURE_SMBUS_PEC) ? I2C_FUNC_SMBUS_PEC : 0) |
	 * ((priv->features & FEATURE_I2C_BLOCK_READ) ?
	 * I2C_FUNC_SMBUS_READ_I2C_BLOCK : 0);
	 */

	// http://lxr.free-electrons.com/source/include/uapi/linux/i2c.h#L85

	u32 f =
		I2C_FUNC_I2C                     | /* 0x00000001 (I enabled this one) */
		!I2C_FUNC_10BIT_ADDR             | /* 0x00000002 */
		!I2C_FUNC_PROTOCOL_MANGLING      | /* 0x00000004 */
		((priv->features & FEATURE_SMBUS_PEC) ? I2C_FUNC_SMBUS_PEC : 0) | /* 0x00000008 */
		!I2C_FUNC_SMBUS_BLOCK_PROC_CALL  | /* 0x00008000 */
		I2C_FUNC_SMBUS_QUICK             | /* 0x00010000 */
		!I2C_FUNC_SMBUS_READ_BYTE        | /* 0x00020000 */
		!I2C_FUNC_SMBUS_WRITE_BYTE       | /* 0x00040000 */
		!I2C_FUNC_SMBUS_READ_BYTE_DATA   | /* 0x00080000 */
		!I2C_FUNC_SMBUS_WRITE_BYTE_DATA  | /* 0x00100000 */
		!I2C_FUNC_SMBUS_READ_WORD_DATA   | /* 0x00200000 */
		!I2C_FUNC_SMBUS_WRITE_WORD_DATA  | /* 0x00400000 */
		!I2C_FUNC_SMBUS_PROC_CALL        | /* 0x00800000 */
		!I2C_FUNC_SMBUS_READ_BLOCK_DATA  | /* 0x01000000 */
		!I2C_FUNC_SMBUS_WRITE_BLOCK_DATA | /* 0x02000000 */
		((priv->features & FEATURE_I2C_BLOCK_READ) ? I2C_FUNC_SMBUS_READ_I2C_BLOCK : 0) | /* 0x04000000 */
		I2C_FUNC_SMBUS_WRITE_I2C_BLOCK   | /* 0x08000000 */

		I2C_FUNC_SMBUS_BYTE              | /* _READ_BYTE  _WRITE_BYTE */
		I2C_FUNC_SMBUS_BYTE_DATA         | /* _READ_BYTE_DATA  _WRITE_BYTE_DATA */
		I2C_FUNC_SMBUS_WORD_DATA         | /* _READ_WORD_DATA  _WRITE_WORD_DATA */
		I2C_FUNC_SMBUS_BLOCK_DATA        | /* _READ_BLOCK_DATA  _WRITE_BLOCK_DATA */
		!I2C_FUNC_SMBUS_I2C_BLOCK        | /* _READ_I2C_BLOCK  _WRITE_I2C_BLOCK */
		!I2C_FUNC_SMBUS_EMUL;              /* _QUICK  _BYTE  _BYTE_DATA  _WORD_DATA  _PROC_CALL  _WRITE_BLOCK_DATA  _I2C_BLOCK _PEC */
	return f;
}

static const struct i2c_algorithm smbus_algorithm = {
	.smbus_xfer     = i801_access,
	.functionality  = i801_func,
};

/********************************
 *** Part 2 - Driver Handlers ***
 ********************************/
static int pi2c_probe(struct platform_device *pldev)
{
	int err;
	struct i2c_device *priv;
	struct resource *res;

	priv = devm_kzalloc(&pldev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	i2c_set_adapdata(&priv->adapter, priv);
	priv->adapter.owner = THIS_MODULE;
	priv->adapter.class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	priv->adapter.algo = &smbus_algorithm;

	res = platform_get_resource(pldev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENXIO;

	priv->smba = (unsigned long)devm_ioremap_nocache(&pldev->dev,
							 res->start,
							 resource_size(res));
	if (!priv->smba)
		return -ENOMEM;

	platform_set_drvdata(pldev, priv);

	priv->features |= FEATURE_IDF;
	priv->features |= FEATURE_I2C_BLOCK_READ;
	priv->features |= FEATURE_SMBUS_PEC;
	priv->features |= FEATURE_BLOCK_BUFFER;

	//init_MUTEX(&lddata->sem);

	/* set up the sysfs linkage to our parent device */
	priv->adapter.dev.parent = &pldev->dev;

	/* Retry up to 3 times on lost arbitration */
	priv->adapter.retries = 3;

	//snprintf(priv->adapter.name, sizeof(priv->adapter.name), "Fake SMBus I801 adapter at %04lx", priv->smba);
	snprintf(priv->adapter.name, sizeof(priv->adapter.name), "Fake SMBus I801 adapter");

	err = i2c_add_adapter(&priv->adapter);
	if (err) {
		dev_err(&priv->adapter.dev, "Failed to add SMBus adapter\n");
		return err;
	}

	return 0;
}

static int pi2c_remove(struct platform_device *pldev)
{
	struct i2c_device *lddev;

	lddev = (struct i2c_device *)platform_get_drvdata(pldev);

	i2c_del_adapter(&lddev->adapter);

	//TODO: Figure out the right thing to do here...
	//pci_write_config_byte(dev, SMBHSTCFG, priv->original_hstcfg);
	//pci_release_region(dev, SMBBAR);
	//pci_set_drvdata(dev, NULL);

	//cdev_del(&lddev->cdev);

	return 0;
}

static struct platform_driver i2c_plat_driver_i = {
	.probe      = pi2c_probe,
	.remove     = pi2c_remove,
	.driver     = {
		.name   = KP_DRIVER_NAME_I2C,
	},
};

module_platform_driver(i2c_plat_driver_i);
