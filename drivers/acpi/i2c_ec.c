/*
 * SMBus driver for ACPI Embedded Controller ($Revision: 1.3 $)
 *
 * Copyright (c) 2002, 2005 Ducrot Bruno
 * Copyright (c) 2005 Rich Townsend (tiny hacks & tweaks)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/acpi.h>
#include <linux/delay.h>

#include "i2c_ec.h"

#define	xudelay(t)	udelay(t)
#define	xmsleep(t)	msleep(t)

#define ACPI_EC_HC_COMPONENT	0x00080000
#define ACPI_EC_HC_CLASS	"ec_hc_smbus"
#define ACPI_EC_HC_HID		"ACPI0001"
#define ACPI_EC_HC_DRIVER_NAME	"ACPI EC HC smbus driver"
#define ACPI_EC_HC_DEVICE_NAME	"EC HC smbus"

#define _COMPONENT		ACPI_EC_HC_COMPONENT

ACPI_MODULE_NAME("acpi_smbus")

static int acpi_ec_hc_add(struct acpi_device *device);
static int acpi_ec_hc_remove(struct acpi_device *device, int type);

static struct acpi_driver acpi_ec_hc_driver = {
	.name = ACPI_EC_HC_DRIVER_NAME,
	.class = ACPI_EC_HC_CLASS,
	.ids = ACPI_EC_HC_HID,
	.ops = {
		.add = acpi_ec_hc_add,
		.remove = acpi_ec_hc_remove,
		},
};

/* Various bit mask for EC_SC (R) */
#define OBF		0x01
#define IBF		0x02
#define CMD		0x08
#define BURST		0x10
#define SCI_EVT		0x20
#define SMI_EVT		0x40

/* Commands for EC_SC (W) */
#define RD_EC		0x80
#define WR_EC		0x81
#define BE_EC		0x82
#define BD_EC		0x83
#define QR_EC		0x84

/*
 * ACPI 2.0 chapter 13 SMBus 2.0 EC register model
 */

#define ACPI_EC_SMB_PRTCL	0x00	/* protocol, PEC */
#define ACPI_EC_SMB_STS		0x01	/* status */
#define ACPI_EC_SMB_ADDR	0x02	/* address */
#define ACPI_EC_SMB_CMD		0x03	/* command */
#define ACPI_EC_SMB_DATA	0x04	/* 32 data registers */
#define ACPI_EC_SMB_BCNT	0x24	/* number of data bytes */
#define ACPI_EC_SMB_ALRM_A	0x25	/* alarm address */
#define ACPI_EC_SMB_ALRM_D	0x26	/* 2 bytes alarm data */

#define ACPI_EC_SMB_STS_DONE	0x80
#define ACPI_EC_SMB_STS_ALRM	0x40
#define ACPI_EC_SMB_STS_RES	0x20
#define ACPI_EC_SMB_STS_STATUS	0x1f

#define ACPI_EC_SMB_STATUS_OK		0x00
#define ACPI_EC_SMB_STATUS_FAIL		0x07
#define ACPI_EC_SMB_STATUS_DNAK		0x10
#define ACPI_EC_SMB_STATUS_DERR		0x11
#define ACPI_EC_SMB_STATUS_CMD_DENY	0x12
#define ACPI_EC_SMB_STATUS_UNKNOWN	0x13
#define ACPI_EC_SMB_STATUS_ACC_DENY	0x17
#define ACPI_EC_SMB_STATUS_TIMEOUT	0x18
#define ACPI_EC_SMB_STATUS_NOTSUP	0x19
#define ACPI_EC_SMB_STATUS_BUSY		0x1A
#define ACPI_EC_SMB_STATUS_PEC		0x1F

#define ACPI_EC_SMB_PRTCL_WRITE			0x00
#define ACPI_EC_SMB_PRTCL_READ			0x01
#define ACPI_EC_SMB_PRTCL_QUICK			0x02
#define ACPI_EC_SMB_PRTCL_BYTE			0x04
#define ACPI_EC_SMB_PRTCL_BYTE_DATA		0x06
#define ACPI_EC_SMB_PRTCL_WORD_DATA		0x08
#define ACPI_EC_SMB_PRTCL_BLOCK_DATA		0x0a
#define ACPI_EC_SMB_PRTCL_PROC_CALL		0x0c
#define ACPI_EC_SMB_PRTCL_BLOCK_PROC_CALL	0x0d
#define ACPI_EC_SMB_PRTCL_I2C_BLOCK_DATA	0x4a
#define ACPI_EC_SMB_PRTCL_PEC			0x80

/* Length of pre/post transaction sleep (msec) */
#define ACPI_EC_SMB_TRANSACTION_SLEEP		1
#define ACPI_EC_SMB_ACCESS_SLEEP1		1
#define ACPI_EC_SMB_ACCESS_SLEEP2		10

static int acpi_ec_smb_read(struct acpi_ec_smbus *smbus, u8 address, u8 * data)
{
	u8 val;
	int err;

	err = ec_read(smbus->base + address, &val);
	if (!err) {
		*data = val;
	}
	xmsleep(ACPI_EC_SMB_TRANSACTION_SLEEP);
	return (err);
}

static int acpi_ec_smb_write(struct acpi_ec_smbus *smbus, u8 address, u8 data)
{
	int err;

	err = ec_write(smbus->base + address, data);
	return (err);
}

static int
acpi_ec_smb_access(struct i2c_adapter *adap, u16 addr, unsigned short flags,
		   char read_write, u8 command, int size,
		   union i2c_smbus_data *data)
{
	struct acpi_ec_smbus *smbus = adap->algo_data;
	unsigned char protocol, len = 0, pec, temp[2] = { 0, 0 };
	int i;

	if (read_write == I2C_SMBUS_READ) {
		protocol = ACPI_EC_SMB_PRTCL_READ;
	} else {
		protocol = ACPI_EC_SMB_PRTCL_WRITE;
	}
	pec = (flags & I2C_CLIENT_PEC) ? ACPI_EC_SMB_PRTCL_PEC : 0;

	switch (size) {

	case I2C_SMBUS_QUICK:
		protocol |= ACPI_EC_SMB_PRTCL_QUICK;
		read_write = I2C_SMBUS_WRITE;
		break;

	case I2C_SMBUS_BYTE:
		if (read_write == I2C_SMBUS_WRITE) {
			acpi_ec_smb_write(smbus, ACPI_EC_SMB_DATA, data->byte);
		}
		protocol |= ACPI_EC_SMB_PRTCL_BYTE;
		break;

	case I2C_SMBUS_BYTE_DATA:
		acpi_ec_smb_write(smbus, ACPI_EC_SMB_CMD, command);
		if (read_write == I2C_SMBUS_WRITE) {
			acpi_ec_smb_write(smbus, ACPI_EC_SMB_DATA, data->byte);
		}
		protocol |= ACPI_EC_SMB_PRTCL_BYTE_DATA;
		break;

	case I2C_SMBUS_WORD_DATA:
		acpi_ec_smb_write(smbus, ACPI_EC_SMB_CMD, command);
		if (read_write == I2C_SMBUS_WRITE) {
			acpi_ec_smb_write(smbus, ACPI_EC_SMB_DATA, data->word);
			acpi_ec_smb_write(smbus, ACPI_EC_SMB_DATA + 1,
					  data->word >> 8);
		}
		protocol |= ACPI_EC_SMB_PRTCL_WORD_DATA | pec;
		break;

	case I2C_SMBUS_BLOCK_DATA:
		acpi_ec_smb_write(smbus, ACPI_EC_SMB_CMD, command);
		if (read_write == I2C_SMBUS_WRITE) {
			len = min_t(u8, data->block[0], 32);
			acpi_ec_smb_write(smbus, ACPI_EC_SMB_BCNT, len);
			for (i = 0; i < len; i++)
				acpi_ec_smb_write(smbus, ACPI_EC_SMB_DATA + i,
						  data->block[i + 1]);
		}
		protocol |= ACPI_EC_SMB_PRTCL_BLOCK_DATA | pec;
		break;

	case I2C_SMBUS_I2C_BLOCK_DATA:
		len = min_t(u8, data->block[0], 32);
		acpi_ec_smb_write(smbus, ACPI_EC_SMB_CMD, command);
		acpi_ec_smb_write(smbus, ACPI_EC_SMB_BCNT, len);
		if (read_write == I2C_SMBUS_WRITE) {
			for (i = 0; i < len; i++) {
				acpi_ec_smb_write(smbus, ACPI_EC_SMB_DATA + i,
						  data->block[i + 1]);
			}
		}
		protocol |= ACPI_EC_SMB_PRTCL_I2C_BLOCK_DATA;
		break;

	case I2C_SMBUS_PROC_CALL:
		acpi_ec_smb_write(smbus, ACPI_EC_SMB_CMD, command);
		acpi_ec_smb_write(smbus, ACPI_EC_SMB_DATA, data->word);
		acpi_ec_smb_write(smbus, ACPI_EC_SMB_DATA + 1, data->word >> 8);
		protocol = ACPI_EC_SMB_PRTCL_PROC_CALL | pec;
		read_write = I2C_SMBUS_READ;
		break;

	case I2C_SMBUS_BLOCK_PROC_CALL:
		protocol |= pec;
		len = min_t(u8, data->block[0], 31);
		acpi_ec_smb_write(smbus, ACPI_EC_SMB_CMD, command);
		acpi_ec_smb_write(smbus, ACPI_EC_SMB_BCNT, len);
		for (i = 0; i < len; i++)
			acpi_ec_smb_write(smbus, ACPI_EC_SMB_DATA + i,
					  data->block[i + 1]);
		protocol = ACPI_EC_SMB_PRTCL_BLOCK_PROC_CALL | pec;
		read_write = I2C_SMBUS_READ;
		break;

	default:
		ACPI_DEBUG_PRINT((ACPI_DB_WARN, "EC SMBus adapter: "
				  "Unsupported transaction %d\n", size));
		return (-1);
	}

	acpi_ec_smb_write(smbus, ACPI_EC_SMB_ADDR, addr << 1);
	acpi_ec_smb_write(smbus, ACPI_EC_SMB_PRTCL, protocol);

	acpi_ec_smb_read(smbus, ACPI_EC_SMB_STS, temp + 0);

	if (~temp[0] & ACPI_EC_SMB_STS_DONE) {
		xudelay(500);
		acpi_ec_smb_read(smbus, ACPI_EC_SMB_STS, temp + 0);
	}
	if (~temp[0] & ACPI_EC_SMB_STS_DONE) {
		xmsleep(ACPI_EC_SMB_ACCESS_SLEEP2);
		acpi_ec_smb_read(smbus, ACPI_EC_SMB_STS, temp + 0);
	}
	if ((~temp[0] & ACPI_EC_SMB_STS_DONE)
	    || (temp[0] & ACPI_EC_SMB_STS_STATUS)) {
		return (-1);
	}

	if (read_write == I2C_SMBUS_WRITE) {
		return (0);
	}

	switch (size) {

	case I2C_SMBUS_BYTE:
	case I2C_SMBUS_BYTE_DATA:
		acpi_ec_smb_read(smbus, ACPI_EC_SMB_DATA, &data->byte);
		break;

	case I2C_SMBUS_WORD_DATA:
	case I2C_SMBUS_PROC_CALL:
		acpi_ec_smb_read(smbus, ACPI_EC_SMB_DATA, temp + 0);
		acpi_ec_smb_read(smbus, ACPI_EC_SMB_DATA + 1, temp + 1);
		data->word = (temp[1] << 8) | temp[0];
		break;

	case I2C_SMBUS_BLOCK_DATA:
	case I2C_SMBUS_BLOCK_PROC_CALL:
		len = 0;
		acpi_ec_smb_read(smbus, ACPI_EC_SMB_BCNT, &len);
		len = min_t(u8, len, 32);
	case I2C_SMBUS_I2C_BLOCK_DATA:
		for (i = 0; i < len; i++)
			acpi_ec_smb_read(smbus, ACPI_EC_SMB_DATA + i,
					 data->block + i + 1);
		data->block[0] = len;
		break;
	}

	return (0);
}

static u32 acpi_ec_smb_func(struct i2c_adapter *adapter)
{

	return (I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
		I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
		I2C_FUNC_SMBUS_BLOCK_DATA |
		I2C_FUNC_SMBUS_PROC_CALL |
		I2C_FUNC_SMBUS_BLOCK_PROC_CALL |
		I2C_FUNC_SMBUS_I2C_BLOCK | I2C_FUNC_SMBUS_HWPEC_CALC);
}

static const struct i2c_algorithm acpi_ec_smbus_algorithm = {
	.smbus_xfer = acpi_ec_smb_access,
	.functionality = acpi_ec_smb_func,
};

static int acpi_ec_hc_add(struct acpi_device *device)
{
	int status;
	unsigned long val;
	struct acpi_ec_hc *ec_hc;
	struct acpi_ec_smbus *smbus;

	if (!device) {
		return -EINVAL;
	}

	ec_hc = kmalloc(sizeof(struct acpi_ec_hc), GFP_KERNEL);
	if (!ec_hc) {
		return -ENOMEM;
	}
	memset(ec_hc, 0, sizeof(struct acpi_ec_hc));

	smbus = kmalloc(sizeof(struct acpi_ec_smbus), GFP_KERNEL);
	if (!smbus) {
		kfree(ec_hc);
		return -ENOMEM;
	}
	memset(smbus, 0, sizeof(struct acpi_ec_smbus));

	ec_hc->handle = device->handle;
	strcpy(acpi_device_name(device), ACPI_EC_HC_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_EC_HC_CLASS);
	acpi_driver_data(device) = ec_hc;

	status = acpi_evaluate_integer(ec_hc->handle, "_EC", NULL, &val);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Error obtaining _EC\n"));
		kfree(ec_hc);
		kfree(smbus);
		return -EIO;
	}

	smbus->ec = acpi_driver_data(device->parent);
	smbus->base = (val & 0xff00ull) >> 8;
	smbus->alert = val & 0xffull;

	smbus->adapter.owner = THIS_MODULE;
	smbus->adapter.algo = &acpi_ec_smbus_algorithm;
	smbus->adapter.algo_data = smbus;

	if (i2c_add_adapter(&smbus->adapter)) {
		ACPI_DEBUG_PRINT((ACPI_DB_WARN,
				  "EC SMBus adapter: Failed to register adapter\n"));
		kfree(smbus);
		kfree(ec_hc);
		return -EIO;
	}

	ec_hc->smbus = smbus;

	printk(KERN_INFO PREFIX "%s [%s]\n",
	       acpi_device_name(device), acpi_device_bid(device));

	return AE_OK;
}

static int acpi_ec_hc_remove(struct acpi_device *device, int type)
{
	struct acpi_ec_hc *ec_hc;

	if (!device) {
		return -EINVAL;
	}
	ec_hc = acpi_driver_data(device);

	i2c_del_adapter(&ec_hc->smbus->adapter);
	kfree(ec_hc->smbus);
	kfree(ec_hc);

	return AE_OK;
}

static int __init acpi_ec_hc_init(void)
{
	int result;

	result = acpi_bus_register_driver(&acpi_ec_hc_driver);
	if (result < 0) {
		return -ENODEV;
	}
	return 0;
}

static void __exit acpi_ec_hc_exit(void)
{
	acpi_bus_unregister_driver(&acpi_ec_hc_driver);
}

struct acpi_ec_hc *acpi_get_ec_hc(struct acpi_device *device)
{
	return acpi_driver_data(device->parent);
}

EXPORT_SYMBOL(acpi_get_ec_hc);

module_init(acpi_ec_hc_init);
module_exit(acpi_ec_hc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ducrot Bruno");
MODULE_DESCRIPTION("ACPI EC SMBus driver");
