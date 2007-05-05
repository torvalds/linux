/*
    i2c-viapro.c - Part of lm_sensors, Linux kernel modules for hardware
              monitoring
    Copyright (c) 1998 - 2002  Frodo Looijaard <frodol@dds.nl>,
    Philip Edelbrock <phil@netroedge.com>, Kyösti Mälkki <kmalkki@cc.hut.fi>,
    Mark D. Studebaker <mdsxyz123@yahoo.com>
    Copyright (C) 2005 - 2007  Jean Delvare <khali@linux-fr.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
   Supports the following VIA south bridges:

   Chip name          PCI ID  REV     I2C block
   VT82C596A          0x3050             no
   VT82C596B          0x3051             no
   VT82C686A          0x3057  0x30       no
   VT82C686B          0x3057  0x40       yes
   VT8231             0x8235             no?
   VT8233             0x3074             yes
   VT8233A            0x3147             yes?
   VT8235             0x3177             yes
   VT8237R            0x3227             yes
   VT8237A            0x3337             yes
   VT8251             0x3287             yes
   CX700              0x8324             yes

   Note: we assume there can only be one device, with one SMBus interface.
*/

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/ioport.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <asm/io.h>

static struct pci_dev *vt596_pdev;

#define SMBBA1		0x90
#define SMBBA2		0x80
#define SMBBA3		0xD0

/* SMBus address offsets */
static unsigned short vt596_smba;
#define SMBHSTSTS	(vt596_smba + 0)
#define SMBHSTCNT	(vt596_smba + 2)
#define SMBHSTCMD	(vt596_smba + 3)
#define SMBHSTADD	(vt596_smba + 4)
#define SMBHSTDAT0	(vt596_smba + 5)
#define SMBHSTDAT1	(vt596_smba + 6)
#define SMBBLKDAT	(vt596_smba + 7)

/* PCI Address Constants */

/* SMBus data in configuration space can be found in two places,
   We try to select the better one */

static unsigned short SMBHSTCFG = 0xD2;

/* Other settings */
#define MAX_TIMEOUT	500

/* VT82C596 constants */
#define VT596_QUICK		0x00
#define VT596_BYTE		0x04
#define VT596_BYTE_DATA		0x08
#define VT596_WORD_DATA		0x0C
#define VT596_BLOCK_DATA	0x14
#define VT596_I2C_BLOCK_DATA	0x34


/* If force is set to anything different from 0, we forcibly enable the
   VT596. DANGEROUS! */
static int force;
module_param(force, bool, 0);
MODULE_PARM_DESC(force, "Forcibly enable the SMBus. DANGEROUS!");

/* If force_addr is set to anything different from 0, we forcibly enable
   the VT596 at the given address. VERY DANGEROUS! */
static u16 force_addr;
module_param(force_addr, ushort, 0);
MODULE_PARM_DESC(force_addr,
		 "Forcibly enable the SMBus at the given address. "
		 "EXTREMELY DANGEROUS!");


static struct pci_driver vt596_driver;
static struct i2c_adapter vt596_adapter;

#define FEATURE_I2CBLOCK	(1<<0)
static unsigned int vt596_features;

#ifdef DEBUG
static void vt596_dump_regs(const char *msg, u8 size)
{
	dev_dbg(&vt596_adapter.dev, "%s: STS=%02x CNT=%02x CMD=%02x ADD=%02x "
		"DAT=%02x,%02x\n", msg, inb_p(SMBHSTSTS), inb_p(SMBHSTCNT),
		inb_p(SMBHSTCMD), inb_p(SMBHSTADD), inb_p(SMBHSTDAT0),
		inb_p(SMBHSTDAT1));

	if (size == VT596_BLOCK_DATA
	 || size == VT596_I2C_BLOCK_DATA) {
		int i;

		dev_dbg(&vt596_adapter.dev, "BLK=");
		for (i = 0; i < I2C_SMBUS_BLOCK_MAX / 2; i++)
			printk("%02x,", inb_p(SMBBLKDAT));
		printk("\n");
		dev_dbg(&vt596_adapter.dev, "    ");
		for (; i < I2C_SMBUS_BLOCK_MAX - 1; i++)
			printk("%02x,", inb_p(SMBBLKDAT));
		printk("%02x\n", inb_p(SMBBLKDAT));
	}
}
#else
static inline void vt596_dump_regs(const char *msg, u8 size) { }
#endif

/* Return -1 on error, 0 on success */
static int vt596_transaction(u8 size)
{
	int temp;
	int result = 0;
	int timeout = 0;

	vt596_dump_regs("Transaction (pre)", size);

	/* Make sure the SMBus host is ready to start transmitting */
	if ((temp = inb_p(SMBHSTSTS)) & 0x1F) {
		dev_dbg(&vt596_adapter.dev, "SMBus busy (0x%02x). "
			"Resetting...\n", temp);

		outb_p(temp, SMBHSTSTS);
		if ((temp = inb_p(SMBHSTSTS)) & 0x1F) {
			dev_err(&vt596_adapter.dev, "SMBus reset failed! "
				"(0x%02x)\n", temp);
			return -1;
		}
	}

	/* Start the transaction by setting bit 6 */
	outb_p(0x40 | size, SMBHSTCNT);

	/* We will always wait for a fraction of a second */
	do {
		msleep(1);
		temp = inb_p(SMBHSTSTS);
	} while ((temp & 0x01) && (timeout++ < MAX_TIMEOUT));

	/* If the SMBus is still busy, we give up */
	if (timeout >= MAX_TIMEOUT) {
		result = -1;
		dev_err(&vt596_adapter.dev, "SMBus timeout!\n");
	}

	if (temp & 0x10) {
		result = -1;
		dev_err(&vt596_adapter.dev, "Transaction failed (0x%02x)\n",
			size);
	}

	if (temp & 0x08) {
		result = -1;
		dev_err(&vt596_adapter.dev, "SMBus collision!\n");
	}

	if (temp & 0x04) {
		int read = inb_p(SMBHSTADD) & 0x01;
		result = -1;
		/* The quick and receive byte commands are used to probe
		   for chips, so errors are expected, and we don't want
		   to frighten the user. */
		if (!((size == VT596_QUICK && !read) ||
		      (size == VT596_BYTE && read)))
			dev_err(&vt596_adapter.dev, "Transaction error!\n");
	}

	/* Resetting status register */
	if (temp & 0x1F)
		outb_p(temp, SMBHSTSTS);

	vt596_dump_regs("Transaction (post)", size);

	return result;
}

/* Return -1 on error, 0 on success */
static s32 vt596_access(struct i2c_adapter *adap, u16 addr,
		unsigned short flags, char read_write, u8 command,
		int size, union i2c_smbus_data *data)
{
	int i;

	switch (size) {
	case I2C_SMBUS_QUICK:
		size = VT596_QUICK;
		break;
	case I2C_SMBUS_BYTE:
		if (read_write == I2C_SMBUS_WRITE)
			outb_p(command, SMBHSTCMD);
		size = VT596_BYTE;
		break;
	case I2C_SMBUS_BYTE_DATA:
		outb_p(command, SMBHSTCMD);
		if (read_write == I2C_SMBUS_WRITE)
			outb_p(data->byte, SMBHSTDAT0);
		size = VT596_BYTE_DATA;
		break;
	case I2C_SMBUS_WORD_DATA:
		outb_p(command, SMBHSTCMD);
		if (read_write == I2C_SMBUS_WRITE) {
			outb_p(data->word & 0xff, SMBHSTDAT0);
			outb_p((data->word & 0xff00) >> 8, SMBHSTDAT1);
		}
		size = VT596_WORD_DATA;
		break;
	case I2C_SMBUS_I2C_BLOCK_DATA:
		if (!(vt596_features & FEATURE_I2CBLOCK))
			goto exit_unsupported;
		if (read_write == I2C_SMBUS_READ)
			outb_p(I2C_SMBUS_BLOCK_MAX, SMBHSTDAT0);
		/* Fall through */
	case I2C_SMBUS_BLOCK_DATA:
		outb_p(command, SMBHSTCMD);
		if (read_write == I2C_SMBUS_WRITE) {
			u8 len = data->block[0];
			if (len > I2C_SMBUS_BLOCK_MAX)
				len = I2C_SMBUS_BLOCK_MAX;
			outb_p(len, SMBHSTDAT0);
			inb_p(SMBHSTCNT);	/* Reset SMBBLKDAT */
			for (i = 1; i <= len; i++)
				outb_p(data->block[i], SMBBLKDAT);
		}
		size = (size == I2C_SMBUS_I2C_BLOCK_DATA) ?
		       VT596_I2C_BLOCK_DATA : VT596_BLOCK_DATA;
		break;
	default:
		goto exit_unsupported;
	}

	outb_p(((addr & 0x7f) << 1) | read_write, SMBHSTADD);

	if (vt596_transaction(size)) /* Error in transaction */
		return -1;

	if ((read_write == I2C_SMBUS_WRITE) || (size == VT596_QUICK))
		return 0;

	switch (size) {
	case VT596_BYTE:
	case VT596_BYTE_DATA:
		data->byte = inb_p(SMBHSTDAT0);
		break;
	case VT596_WORD_DATA:
		data->word = inb_p(SMBHSTDAT0) + (inb_p(SMBHSTDAT1) << 8);
		break;
	case VT596_I2C_BLOCK_DATA:
	case VT596_BLOCK_DATA:
		data->block[0] = inb_p(SMBHSTDAT0);
		if (data->block[0] > I2C_SMBUS_BLOCK_MAX)
			data->block[0] = I2C_SMBUS_BLOCK_MAX;
		inb_p(SMBHSTCNT);	/* Reset SMBBLKDAT */
		for (i = 1; i <= data->block[0]; i++)
			data->block[i] = inb_p(SMBBLKDAT);
		break;
	}
	return 0;

exit_unsupported:
	dev_warn(&vt596_adapter.dev, "Unsupported command invoked! (0x%02x)\n",
		 size);
	return -1;
}

static u32 vt596_func(struct i2c_adapter *adapter)
{
	u32 func = I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
	    I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
	    I2C_FUNC_SMBUS_BLOCK_DATA;

	if (vt596_features & FEATURE_I2CBLOCK)
		func |= I2C_FUNC_SMBUS_I2C_BLOCK;
	return func;
}

static const struct i2c_algorithm smbus_algorithm = {
	.smbus_xfer	= vt596_access,
	.functionality	= vt596_func,
};

static struct i2c_adapter vt596_adapter = {
	.owner		= THIS_MODULE,
	.id		= I2C_HW_SMBUS_VIA2,
	.class		= I2C_CLASS_HWMON,
	.algo		= &smbus_algorithm,
};

static int __devinit vt596_probe(struct pci_dev *pdev,
				 const struct pci_device_id *id)
{
	unsigned char temp;
	int error = -ENODEV;

	/* Determine the address of the SMBus areas */
	if (force_addr) {
		vt596_smba = force_addr & 0xfff0;
		force = 0;
		goto found;
	}

	if ((pci_read_config_word(pdev, id->driver_data, &vt596_smba)) ||
	    !(vt596_smba & 0x0001)) {
		/* try 2nd address and config reg. for 596 */
		if (id->device == PCI_DEVICE_ID_VIA_82C596_3 &&
		    !pci_read_config_word(pdev, SMBBA2, &vt596_smba) &&
		    (vt596_smba & 0x0001)) {
			SMBHSTCFG = 0x84;
		} else {
			/* no matches at all */
			dev_err(&pdev->dev, "Cannot configure "
				"SMBus I/O Base address\n");
			return -ENODEV;
		}
	}

	vt596_smba &= 0xfff0;
	if (vt596_smba == 0) {
		dev_err(&pdev->dev, "SMBus base address "
			"uninitialized - upgrade BIOS or use "
			"force_addr=0xaddr\n");
		return -ENODEV;
	}

found:
	if (!request_region(vt596_smba, 8, vt596_driver.name)) {
		dev_err(&pdev->dev, "SMBus region 0x%x already in use!\n",
			vt596_smba);
		return -ENODEV;
	}

	pci_read_config_byte(pdev, SMBHSTCFG, &temp);
	/* If force_addr is set, we program the new address here. Just to make
	   sure, we disable the VT596 first. */
	if (force_addr) {
		pci_write_config_byte(pdev, SMBHSTCFG, temp & 0xfe);
		pci_write_config_word(pdev, id->driver_data, vt596_smba);
		pci_write_config_byte(pdev, SMBHSTCFG, temp | 0x01);
		dev_warn(&pdev->dev, "WARNING: SMBus interface set to new "
			 "address 0x%04x!\n", vt596_smba);
	} else if (!(temp & 0x01)) {
		if (force) {
			/* NOTE: This assumes I/O space and other allocations
			 * WERE done by the Bios!  Don't complain if your
			 * hardware does weird things after enabling this.
			 * :') Check for Bios updates before resorting to
			 * this.
			 */
			pci_write_config_byte(pdev, SMBHSTCFG, temp | 0x01);
			dev_info(&pdev->dev, "Enabling SMBus device\n");
		} else {
			dev_err(&pdev->dev, "SMBUS: Error: Host SMBus "
				"controller not enabled! - upgrade BIOS or "
				"use force=1\n");
			goto release_region;
		}
	}

	dev_dbg(&pdev->dev, "VT596_smba = 0x%X\n", vt596_smba);

	switch (pdev->device) {
	case PCI_DEVICE_ID_VIA_CX700:
	case PCI_DEVICE_ID_VIA_8251:
	case PCI_DEVICE_ID_VIA_8237:
	case PCI_DEVICE_ID_VIA_8237A:
	case PCI_DEVICE_ID_VIA_8235:
	case PCI_DEVICE_ID_VIA_8233A:
	case PCI_DEVICE_ID_VIA_8233_0:
		vt596_features |= FEATURE_I2CBLOCK;
		break;
	case PCI_DEVICE_ID_VIA_82C686_4:
		/* The VT82C686B (rev 0x40) does support I2C block
		   transactions, but the VT82C686A (rev 0x30) doesn't */
		if (!pci_read_config_byte(pdev, PCI_REVISION_ID, &temp)
		 && temp >= 0x40)
			vt596_features |= FEATURE_I2CBLOCK;
		break;
	}

	vt596_adapter.dev.parent = &pdev->dev;
	snprintf(vt596_adapter.name, sizeof(vt596_adapter.name),
		 "SMBus Via Pro adapter at %04x", vt596_smba);

	vt596_pdev = pci_dev_get(pdev);
	if (i2c_add_adapter(&vt596_adapter)) {
		pci_dev_put(vt596_pdev);
		vt596_pdev = NULL;
	}

	/* Always return failure here.  This is to allow other drivers to bind
	 * to this pci device.  We don't really want to have control over the
	 * pci device, we only wanted to read as few register values from it.
	 */
	return -ENODEV;

release_region:
	release_region(vt596_smba, 8);
	return error;
}

static struct pci_device_id vt596_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C596_3),
	  .driver_data = SMBBA1 },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C596B_3),
	  .driver_data = SMBBA1 },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C686_4),
	  .driver_data = SMBBA1 },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8233_0),
	  .driver_data = SMBBA3 },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8233A),
	  .driver_data = SMBBA3 },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8235),
	  .driver_data = SMBBA3 },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8237),
	  .driver_data = SMBBA3 },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8237A),
	  .driver_data = SMBBA3 },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8231_4),
	  .driver_data = SMBBA1 },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8251),
	  .driver_data = SMBBA3 },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_CX700),
	  .driver_data = SMBBA3 },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, vt596_ids);

static struct pci_driver vt596_driver = {
	.name		= "vt596_smbus",
	.id_table	= vt596_ids,
	.probe		= vt596_probe,
};

static int __init i2c_vt596_init(void)
{
	return pci_register_driver(&vt596_driver);
}


static void __exit i2c_vt596_exit(void)
{
	pci_unregister_driver(&vt596_driver);
	if (vt596_pdev != NULL) {
		i2c_del_adapter(&vt596_adapter);
		release_region(vt596_smba, 8);
		pci_dev_put(vt596_pdev);
		vt596_pdev = NULL;
	}
}

MODULE_AUTHOR("Kyosti Malkki <kmalkki@cc.hut.fi>, "
	      "Mark D. Studebaker <mdsxyz123@yahoo.com> and "
	      "Jean Delvare <khali@linux-fr.org>");
MODULE_DESCRIPTION("vt82c596 SMBus driver");
MODULE_LICENSE("GPL");

module_init(i2c_vt596_init);
module_exit(i2c_vt596_exit);
