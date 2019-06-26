// SPDX-License-Identifier: GPL-2.0-only
/*
 * SMBus 2.0 driver for AMD-8111 IO-Hub.
 *
 * Copyright (c) 2002 Vojtech Pavlik
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/ioport.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/acpi.h>
#include <linux/slab.h>
#include <linux/io.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR ("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION("AMD8111 SMBus 2.0 driver");

struct amd_smbus {
	struct pci_dev *dev;
	struct i2c_adapter adapter;
	int base;
	int size;
};

static struct pci_driver amd8111_driver;

/*
 * AMD PCI control registers definitions.
 */

#define AMD_PCI_MISC	0x48

#define AMD_PCI_MISC_SCI	0x04	/* deliver SCI */
#define AMD_PCI_MISC_INT	0x02	/* deliver PCI IRQ */
#define AMD_PCI_MISC_SPEEDUP	0x01	/* 16x clock speedup */

/*
 * ACPI 2.0 chapter 13 PCI interface definitions.
 */

#define AMD_EC_DATA	0x00	/* data register */
#define AMD_EC_SC	0x04	/* status of controller */
#define AMD_EC_CMD	0x04	/* command register */
#define AMD_EC_ICR	0x08	/* interrupt control register */

#define AMD_EC_SC_SMI	0x04	/* smi event pending */
#define AMD_EC_SC_SCI	0x02	/* sci event pending */
#define AMD_EC_SC_BURST	0x01	/* burst mode enabled */
#define AMD_EC_SC_CMD	0x08	/* byte in data reg is command */
#define AMD_EC_SC_IBF	0x02	/* data ready for embedded controller */
#define AMD_EC_SC_OBF	0x01	/* data ready for host */

#define AMD_EC_CMD_RD	0x80	/* read EC */
#define AMD_EC_CMD_WR	0x81	/* write EC */
#define AMD_EC_CMD_BE	0x82	/* enable burst mode */
#define AMD_EC_CMD_BD	0x83	/* disable burst mode */
#define AMD_EC_CMD_QR	0x84	/* query EC */

/*
 * ACPI 2.0 chapter 13 access of registers of the EC
 */

static int amd_ec_wait_write(struct amd_smbus *smbus)
{
	int timeout = 500;

	while ((inb(smbus->base + AMD_EC_SC) & AMD_EC_SC_IBF) && --timeout)
		udelay(1);

	if (!timeout) {
		dev_warn(&smbus->dev->dev,
			 "Timeout while waiting for IBF to clear\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int amd_ec_wait_read(struct amd_smbus *smbus)
{
	int timeout = 500;

	while ((~inb(smbus->base + AMD_EC_SC) & AMD_EC_SC_OBF) && --timeout)
		udelay(1);

	if (!timeout) {
		dev_warn(&smbus->dev->dev,
			 "Timeout while waiting for OBF to set\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int amd_ec_read(struct amd_smbus *smbus, unsigned char address,
		unsigned char *data)
{
	int status;

	status = amd_ec_wait_write(smbus);
	if (status)
		return status;
	outb(AMD_EC_CMD_RD, smbus->base + AMD_EC_CMD);

	status = amd_ec_wait_write(smbus);
	if (status)
		return status;
	outb(address, smbus->base + AMD_EC_DATA);

	status = amd_ec_wait_read(smbus);
	if (status)
		return status;
	*data = inb(smbus->base + AMD_EC_DATA);

	return 0;
}

static int amd_ec_write(struct amd_smbus *smbus, unsigned char address,
		unsigned char data)
{
	int status;

	status = amd_ec_wait_write(smbus);
	if (status)
		return status;
	outb(AMD_EC_CMD_WR, smbus->base + AMD_EC_CMD);

	status = amd_ec_wait_write(smbus);
	if (status)
		return status;
	outb(address, smbus->base + AMD_EC_DATA);

	status = amd_ec_wait_write(smbus);
	if (status)
		return status;
	outb(data, smbus->base + AMD_EC_DATA);

	return 0;
}

/*
 * ACPI 2.0 chapter 13 SMBus 2.0 EC register model
 */

#define AMD_SMB_PRTCL	0x00	/* protocol, PEC */
#define AMD_SMB_STS	0x01	/* status */
#define AMD_SMB_ADDR	0x02	/* address */
#define AMD_SMB_CMD	0x03	/* command */
#define AMD_SMB_DATA	0x04	/* 32 data registers */
#define AMD_SMB_BCNT	0x24	/* number of data bytes */
#define AMD_SMB_ALRM_A	0x25	/* alarm address */
#define AMD_SMB_ALRM_D	0x26	/* 2 bytes alarm data */

#define AMD_SMB_STS_DONE	0x80
#define AMD_SMB_STS_ALRM	0x40
#define AMD_SMB_STS_RES		0x20
#define AMD_SMB_STS_STATUS	0x1f

#define AMD_SMB_STATUS_OK	0x00
#define AMD_SMB_STATUS_FAIL	0x07
#define AMD_SMB_STATUS_DNAK	0x10
#define AMD_SMB_STATUS_DERR	0x11
#define AMD_SMB_STATUS_CMD_DENY	0x12
#define AMD_SMB_STATUS_UNKNOWN	0x13
#define AMD_SMB_STATUS_ACC_DENY	0x17
#define AMD_SMB_STATUS_TIMEOUT	0x18
#define AMD_SMB_STATUS_NOTSUP	0x19
#define AMD_SMB_STATUS_BUSY	0x1A
#define AMD_SMB_STATUS_PEC	0x1F

#define AMD_SMB_PRTCL_WRITE		0x00
#define AMD_SMB_PRTCL_READ		0x01
#define AMD_SMB_PRTCL_QUICK		0x02
#define AMD_SMB_PRTCL_BYTE		0x04
#define AMD_SMB_PRTCL_BYTE_DATA		0x06
#define AMD_SMB_PRTCL_WORD_DATA		0x08
#define AMD_SMB_PRTCL_BLOCK_DATA	0x0a
#define AMD_SMB_PRTCL_PROC_CALL		0x0c
#define AMD_SMB_PRTCL_BLOCK_PROC_CALL	0x0d
#define AMD_SMB_PRTCL_I2C_BLOCK_DATA	0x4a
#define AMD_SMB_PRTCL_PEC		0x80


static s32 amd8111_access(struct i2c_adapter * adap, u16 addr,
		unsigned short flags, char read_write, u8 command, int size,
		union i2c_smbus_data * data)
{
	struct amd_smbus *smbus = adap->algo_data;
	unsigned char protocol, len, pec, temp[2];
	int i, status;

	protocol = (read_write == I2C_SMBUS_READ) ? AMD_SMB_PRTCL_READ
						  : AMD_SMB_PRTCL_WRITE;
	pec = (flags & I2C_CLIENT_PEC) ? AMD_SMB_PRTCL_PEC : 0;

	switch (size) {
		case I2C_SMBUS_QUICK:
			protocol |= AMD_SMB_PRTCL_QUICK;
			read_write = I2C_SMBUS_WRITE;
			break;

		case I2C_SMBUS_BYTE:
			if (read_write == I2C_SMBUS_WRITE) {
				status = amd_ec_write(smbus, AMD_SMB_CMD,
						      command);
				if (status)
					return status;
			}
			protocol |= AMD_SMB_PRTCL_BYTE;
			break;

		case I2C_SMBUS_BYTE_DATA:
			status = amd_ec_write(smbus, AMD_SMB_CMD, command);
			if (status)
				return status;
			if (read_write == I2C_SMBUS_WRITE) {
				status = amd_ec_write(smbus, AMD_SMB_DATA,
						      data->byte);
				if (status)
					return status;
			}
			protocol |= AMD_SMB_PRTCL_BYTE_DATA;
			break;

		case I2C_SMBUS_WORD_DATA:
			status = amd_ec_write(smbus, AMD_SMB_CMD, command);
			if (status)
				return status;
			if (read_write == I2C_SMBUS_WRITE) {
				status = amd_ec_write(smbus, AMD_SMB_DATA,
						      data->word & 0xff);
				if (status)
					return status;
				status = amd_ec_write(smbus, AMD_SMB_DATA + 1,
						      data->word >> 8);
				if (status)
					return status;
			}
			protocol |= AMD_SMB_PRTCL_WORD_DATA | pec;
			break;

		case I2C_SMBUS_BLOCK_DATA:
			status = amd_ec_write(smbus, AMD_SMB_CMD, command);
			if (status)
				return status;
			if (read_write == I2C_SMBUS_WRITE) {
				len = min_t(u8, data->block[0],
					    I2C_SMBUS_BLOCK_MAX);
				status = amd_ec_write(smbus, AMD_SMB_BCNT, len);
				if (status)
					return status;
				for (i = 0; i < len; i++) {
					status =
					  amd_ec_write(smbus, AMD_SMB_DATA + i,
						       data->block[i + 1]);
					if (status)
						return status;
				}
			}
			protocol |= AMD_SMB_PRTCL_BLOCK_DATA | pec;
			break;

		case I2C_SMBUS_I2C_BLOCK_DATA:
			len = min_t(u8, data->block[0],
				    I2C_SMBUS_BLOCK_MAX);
			status = amd_ec_write(smbus, AMD_SMB_CMD, command);
			if (status)
				return status;
			status = amd_ec_write(smbus, AMD_SMB_BCNT, len);
			if (status)
				return status;
			if (read_write == I2C_SMBUS_WRITE)
				for (i = 0; i < len; i++) {
					status =
					  amd_ec_write(smbus, AMD_SMB_DATA + i,
						       data->block[i + 1]);
					if (status)
						return status;
				}
			protocol |= AMD_SMB_PRTCL_I2C_BLOCK_DATA;
			break;

		case I2C_SMBUS_PROC_CALL:
			status = amd_ec_write(smbus, AMD_SMB_CMD, command);
			if (status)
				return status;
			status = amd_ec_write(smbus, AMD_SMB_DATA,
					      data->word & 0xff);
			if (status)
				return status;
			status = amd_ec_write(smbus, AMD_SMB_DATA + 1,
					      data->word >> 8);
			if (status)
				return status;
			protocol = AMD_SMB_PRTCL_PROC_CALL | pec;
			read_write = I2C_SMBUS_READ;
			break;

		case I2C_SMBUS_BLOCK_PROC_CALL:
			len = min_t(u8, data->block[0],
				    I2C_SMBUS_BLOCK_MAX - 1);
			status = amd_ec_write(smbus, AMD_SMB_CMD, command);
			if (status)
				return status;
			status = amd_ec_write(smbus, AMD_SMB_BCNT, len);
			if (status)
				return status;
			for (i = 0; i < len; i++) {
				status = amd_ec_write(smbus, AMD_SMB_DATA + i,
						      data->block[i + 1]);
				if (status)
					return status;
			}
			protocol = AMD_SMB_PRTCL_BLOCK_PROC_CALL | pec;
			read_write = I2C_SMBUS_READ;
			break;

		default:
			dev_warn(&adap->dev, "Unsupported transaction %d\n", size);
			return -EOPNOTSUPP;
	}

	status = amd_ec_write(smbus, AMD_SMB_ADDR, addr << 1);
	if (status)
		return status;
	status = amd_ec_write(smbus, AMD_SMB_PRTCL, protocol);
	if (status)
		return status;

	status = amd_ec_read(smbus, AMD_SMB_STS, temp + 0);
	if (status)
		return status;

	if (~temp[0] & AMD_SMB_STS_DONE) {
		udelay(500);
		status = amd_ec_read(smbus, AMD_SMB_STS, temp + 0);
		if (status)
			return status;
	}

	if (~temp[0] & AMD_SMB_STS_DONE) {
		msleep(1);
		status = amd_ec_read(smbus, AMD_SMB_STS, temp + 0);
		if (status)
			return status;
	}

	if ((~temp[0] & AMD_SMB_STS_DONE) || (temp[0] & AMD_SMB_STS_STATUS))
		return -EIO;

	if (read_write == I2C_SMBUS_WRITE)
		return 0;

	switch (size) {
		case I2C_SMBUS_BYTE:
		case I2C_SMBUS_BYTE_DATA:
			status = amd_ec_read(smbus, AMD_SMB_DATA, &data->byte);
			if (status)
				return status;
			break;

		case I2C_SMBUS_WORD_DATA:
		case I2C_SMBUS_PROC_CALL:
			status = amd_ec_read(smbus, AMD_SMB_DATA, temp + 0);
			if (status)
				return status;
			status = amd_ec_read(smbus, AMD_SMB_DATA + 1, temp + 1);
			if (status)
				return status;
			data->word = (temp[1] << 8) | temp[0];
			break;

		case I2C_SMBUS_BLOCK_DATA:
		case I2C_SMBUS_BLOCK_PROC_CALL:
			status = amd_ec_read(smbus, AMD_SMB_BCNT, &len);
			if (status)
				return status;
			len = min_t(u8, len, I2C_SMBUS_BLOCK_MAX);
			/* fall through */
		case I2C_SMBUS_I2C_BLOCK_DATA:
			for (i = 0; i < len; i++) {
				status = amd_ec_read(smbus, AMD_SMB_DATA + i,
						     data->block + i + 1);
				if (status)
					return status;
			}
			data->block[0] = len;
			break;
	}

	return 0;
}


static u32 amd8111_func(struct i2c_adapter *adapter)
{
	return	I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
		I2C_FUNC_SMBUS_BYTE_DATA |
		I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_BLOCK_DATA |
		I2C_FUNC_SMBUS_PROC_CALL | I2C_FUNC_SMBUS_BLOCK_PROC_CALL |
		I2C_FUNC_SMBUS_I2C_BLOCK | I2C_FUNC_SMBUS_PEC;
}

static const struct i2c_algorithm smbus_algorithm = {
	.smbus_xfer = amd8111_access,
	.functionality = amd8111_func,
};


static const struct pci_device_id amd8111_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_8111_SMBUS2) },
	{ 0, }
};

MODULE_DEVICE_TABLE (pci, amd8111_ids);

static int amd8111_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct amd_smbus *smbus;
	int error;

	if (!(pci_resource_flags(dev, 0) & IORESOURCE_IO))
		return -ENODEV;

	smbus = kzalloc(sizeof(struct amd_smbus), GFP_KERNEL);
	if (!smbus)
		return -ENOMEM;

	smbus->dev = dev;
	smbus->base = pci_resource_start(dev, 0);
	smbus->size = pci_resource_len(dev, 0);

	error = acpi_check_resource_conflict(&dev->resource[0]);
	if (error) {
		error = -ENODEV;
		goto out_kfree;
	}

	if (!request_region(smbus->base, smbus->size, amd8111_driver.name)) {
		error = -EBUSY;
		goto out_kfree;
	}

	smbus->adapter.owner = THIS_MODULE;
	snprintf(smbus->adapter.name, sizeof(smbus->adapter.name),
		"SMBus2 AMD8111 adapter at %04x", smbus->base);
	smbus->adapter.class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	smbus->adapter.algo = &smbus_algorithm;
	smbus->adapter.algo_data = smbus;

	/* set up the sysfs linkage to our parent device */
	smbus->adapter.dev.parent = &dev->dev;

	pci_write_config_dword(smbus->dev, AMD_PCI_MISC, 0);
	error = i2c_add_adapter(&smbus->adapter);
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

static void amd8111_remove(struct pci_dev *dev)
{
	struct amd_smbus *smbus = pci_get_drvdata(dev);

	i2c_del_adapter(&smbus->adapter);
	release_region(smbus->base, smbus->size);
	kfree(smbus);
}

static struct pci_driver amd8111_driver = {
	.name		= "amd8111_smbus2",
	.id_table	= amd8111_ids,
	.probe		= amd8111_probe,
	.remove		= amd8111_remove,
};

module_pci_driver(amd8111_driver);
