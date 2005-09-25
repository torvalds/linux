/*
    i2c-viapro.c - Part of lm_sensors, Linux kernel modules for hardware
              monitoring
    Copyright (c) 1998 - 2002  Frodo Looijaard <frodol@dds.nl>, 
    Philip Edelbrock <phil@netroedge.com>, Kyösti Mälkki <kmalkki@cc.hut.fi>,
    Mark D. Studebaker <mdsxyz123@yahoo.com>

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
   Supports Via devices:
	82C596A/B (0x3050)
	82C596B (0x3051)
	82C686A/B
	8231
	8233
	8233A (0x3147 and 0x3177)
	8235
	8237
   Note: we assume there can only be one device, with one SMBus interface.
*/

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <asm/io.h>

static struct pci_dev *vt596_pdev;

#define SMBBA1	   	 0x90
#define SMBBA2     	 0x80
#define SMBBA3     	 0xD0

/* SMBus address offsets */
static unsigned short vt596_smba;
#define SMBHSTSTS	(vt596_smba + 0)
#define SMBHSLVSTS	(vt596_smba + 1)
#define SMBHSTCNT	(vt596_smba + 2)
#define SMBHSTCMD	(vt596_smba + 3)
#define SMBHSTADD	(vt596_smba + 4)
#define SMBHSTDAT0	(vt596_smba + 5)
#define SMBHSTDAT1	(vt596_smba + 6)
#define SMBBLKDAT	(vt596_smba + 7)
#define SMBSLVCNT	(vt596_smba + 8)
#define SMBSHDWCMD	(vt596_smba + 9)
#define SMBSLVEVT	(vt596_smba + 0xA)
#define SMBSLVDAT	(vt596_smba + 0xC)

/* PCI Address Constants */

/* SMBus data in configuration space can be found in two places,
   We try to select the better one*/

static unsigned short smb_cf_hstcfg = 0xD2;

#define SMBHSTCFG   (smb_cf_hstcfg)
#define SMBSLVC     (smb_cf_hstcfg + 1)
#define SMBSHDW1    (smb_cf_hstcfg + 2)
#define SMBSHDW2    (smb_cf_hstcfg + 3)
#define SMBREV      (smb_cf_hstcfg + 4)

/* Other settings */
#define MAX_TIMEOUT	500
#define ENABLE_INT9	0

/* VT82C596 constants */
#define VT596_QUICK      0x00
#define VT596_BYTE       0x04
#define VT596_BYTE_DATA  0x08
#define VT596_WORD_DATA  0x0C
#define VT596_BLOCK_DATA 0x14


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


static struct i2c_adapter vt596_adapter;

/* Another internally used function */
static int vt596_transaction(void)
{
	int temp;
	int result = 0;
	int timeout = 0;

	dev_dbg(&vt596_adapter.dev, "Transaction (pre): CNT=%02x, CMD=%02x, "
		"ADD=%02x, DAT0=%02x, DAT1=%02x\n", inb_p(SMBHSTCNT), 
		inb_p(SMBHSTCMD), inb_p(SMBHSTADD), inb_p(SMBHSTDAT0), 
		inb_p(SMBHSTDAT1));

	/* Make sure the SMBus host is ready to start transmitting */
	if ((temp = inb_p(SMBHSTSTS)) & 0x1F) {
		dev_dbg(&vt596_adapter.dev, "SMBus busy (0x%02x). "
				"Resetting...\n", temp);
		
		outb_p(temp, SMBHSTSTS);
		if ((temp = inb_p(SMBHSTSTS)) & 0x1F) {
			dev_dbg(&vt596_adapter.dev, "Failed! (0x%02x)\n", temp);
			
			return -1;
		} else {
			dev_dbg(&vt596_adapter.dev, "Successfull!\n");
		}
	}

	/* start the transaction by setting bit 6 */
	outb_p(inb(SMBHSTCNT) | 0x040, SMBHSTCNT);

	/* We will always wait for a fraction of a second! 
	   I don't know if VIA needs this, Intel did  */
	do {
		msleep(1);
		temp = inb_p(SMBHSTSTS);
	} while ((temp & 0x01) && (timeout++ < MAX_TIMEOUT));

	/* If the SMBus is still busy, we give up */
	if (timeout >= MAX_TIMEOUT) {
		result = -1;
		dev_dbg(&vt596_adapter.dev, "SMBus Timeout!\n");
	}

	if (temp & 0x10) {
		result = -1;
		dev_dbg(&vt596_adapter.dev, "Error: Failed bus transaction\n");
	}

	if (temp & 0x08) {
		result = -1;
		dev_info(&vt596_adapter.dev, "Bus collision! SMBus may be "
			"locked until next hard\nreset. (sorry!)\n");
		/* Clock stops and slave is stuck in mid-transmission */
	}

	if (temp & 0x04) {
		result = -1;
		dev_dbg(&vt596_adapter.dev, "Error: no response!\n");
	}

	if ((temp = inb_p(SMBHSTSTS)) & 0x1F) {
		outb_p(temp, SMBHSTSTS);
		if ((temp = inb_p(SMBHSTSTS)) & 0x1F) {
			dev_warn(&vt596_adapter.dev, "Failed reset at end "
				 "of transaction (%02x)\n", temp);
		}
	}

	dev_dbg(&vt596_adapter.dev, "Transaction (post): CNT=%02x, CMD=%02x, "
		"ADD=%02x, DAT0=%02x, DAT1=%02x\n", inb_p(SMBHSTCNT),
		inb_p(SMBHSTCMD), inb_p(SMBHSTADD), inb_p(SMBHSTDAT0), 
		inb_p(SMBHSTDAT1));
	
	return result;
}

/* Return -1 on error. */
static s32 vt596_access(struct i2c_adapter *adap, u16 addr,
		unsigned short flags,  char read_write, u8 command,
		int size,  union i2c_smbus_data *data)
{
	int i, len;

	switch (size) {
	case I2C_SMBUS_PROC_CALL:
		dev_info(&vt596_adapter.dev,
			 "I2C_SMBUS_PROC_CALL not supported!\n");
		return -1;
	case I2C_SMBUS_QUICK:
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMBHSTADD);
		size = VT596_QUICK;
		break;
	case I2C_SMBUS_BYTE:
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMBHSTADD);
		if (read_write == I2C_SMBUS_WRITE)
			outb_p(command, SMBHSTCMD);
		size = VT596_BYTE;
		break;
	case I2C_SMBUS_BYTE_DATA:
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMBHSTADD);
		outb_p(command, SMBHSTCMD);
		if (read_write == I2C_SMBUS_WRITE)
			outb_p(data->byte, SMBHSTDAT0);
		size = VT596_BYTE_DATA;
		break;
	case I2C_SMBUS_WORD_DATA:
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMBHSTADD);
		outb_p(command, SMBHSTCMD);
		if (read_write == I2C_SMBUS_WRITE) {
			outb_p(data->word & 0xff, SMBHSTDAT0);
			outb_p((data->word & 0xff00) >> 8, SMBHSTDAT1);
		}
		size = VT596_WORD_DATA;
		break;
	case I2C_SMBUS_BLOCK_DATA:
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMBHSTADD);
		outb_p(command, SMBHSTCMD);
		if (read_write == I2C_SMBUS_WRITE) {
			len = data->block[0];
			if (len < 0)
				len = 0;
			if (len > I2C_SMBUS_BLOCK_MAX)
				len = I2C_SMBUS_BLOCK_MAX;
			outb_p(len, SMBHSTDAT0);
			i = inb_p(SMBHSTCNT);	/* Reset SMBBLKDAT */
			for (i = 1; i <= len; i++)
				outb_p(data->block[i], SMBBLKDAT);
		}
		size = VT596_BLOCK_DATA;
		break;
	}

	outb_p((size & 0x1C) + (ENABLE_INT9 & 1), SMBHSTCNT);

	if (vt596_transaction()) /* Error in transaction */
		return -1;

	if ((read_write == I2C_SMBUS_WRITE) || (size == VT596_QUICK))
		return 0;

	switch (size) {
	case VT596_BYTE:
		/* Where is the result put? I assume here it is in
		 * SMBHSTDAT0 but it might just as well be in the
		 * SMBHSTCMD. No clue in the docs 
		 */
		data->byte = inb_p(SMBHSTDAT0);
		break;
	case VT596_BYTE_DATA:
		data->byte = inb_p(SMBHSTDAT0);
		break;
	case VT596_WORD_DATA:
		data->word = inb_p(SMBHSTDAT0) + (inb_p(SMBHSTDAT1) << 8);
		break;
	case VT596_BLOCK_DATA:
		data->block[0] = inb_p(SMBHSTDAT0);
		if (data->block[0] > I2C_SMBUS_BLOCK_MAX)
			data->block[0] = I2C_SMBUS_BLOCK_MAX;
		i = inb_p(SMBHSTCNT);	/* Reset SMBBLKDAT */
		for (i = 1; i <= data->block[0]; i++)
			data->block[i] = inb_p(SMBBLKDAT);
		break;
	}
	return 0;
}

static u32 vt596_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
	    I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
	    I2C_FUNC_SMBUS_BLOCK_DATA;
}

static struct i2c_algorithm smbus_algorithm = {
	.smbus_xfer	= vt596_access,
	.functionality	= vt596_func,
};

static struct i2c_adapter vt596_adapter = {
	.owner		= THIS_MODULE,
	.class		= I2C_CLASS_HWMON,
	.algo		= &smbus_algorithm,
	.name		= "unset",
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
	    !(vt596_smba & 0x1)) {
		/* try 2nd address and config reg. for 596 */
		if (id->device == PCI_DEVICE_ID_VIA_82C596_3 &&
		    !pci_read_config_word(pdev, SMBBA2, &vt596_smba) &&
		    (vt596_smba & 0x1)) {
			smb_cf_hstcfg = 0x84;
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
	if (!request_region(vt596_smba, 8, "viapro-smbus")) {
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
	} else if ((temp & 1) == 0) {
		if (force) {
			/* NOTE: This assumes I/O space and other allocations 
			 * WERE done by the Bios!  Don't complain if your 
			 * hardware does weird things after enabling this. 
			 * :') Check for Bios updates before resorting to 
			 * this.
			 */
			pci_write_config_byte(pdev, SMBHSTCFG, temp | 1);
			dev_info(&pdev->dev, "Enabling SMBus device\n");
		} else {
			dev_err(&pdev->dev, "SMBUS: Error: Host SMBus "
				"controller not enabled! - upgrade BIOS or "
				"use force=1\n");
			goto release_region;
		}
	}

	if ((temp & 0x0E) == 8)
		dev_dbg(&pdev->dev, "using Interrupt 9 for SMBus.\n");
	else if ((temp & 0x0E) == 0)
		dev_dbg(&pdev->dev, "using Interrupt SMI# for SMBus.\n");
	else
		dev_dbg(&pdev->dev, "Illegal Interrupt configuration "
			"(or code out of date)!\n");

	pci_read_config_byte(pdev, SMBREV, &temp);
	dev_dbg(&pdev->dev, "SMBREV = 0x%X\n", temp);
	dev_dbg(&pdev->dev, "VT596_smba = 0x%X\n", vt596_smba);

	vt596_adapter.dev.parent = &pdev->dev;
	snprintf(vt596_adapter.name, I2C_NAME_SIZE,
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
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8231_4),
	  .driver_data = SMBBA1 },
	{ 0, }
};

MODULE_DEVICE_TABLE (pci, vt596_ids);

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

MODULE_AUTHOR(
    "Frodo Looijaard <frodol@dds.nl> and "
    "Philip Edelbrock <phil@netroedge.com>");
MODULE_DESCRIPTION("vt82c596 SMBus driver");
MODULE_LICENSE("GPL");

module_init(i2c_vt596_init);
module_exit(i2c_vt596_exit);
