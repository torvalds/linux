/*
    i801.c - Part of lm_sensors, Linux kernel modules for hardware
              monitoring
    Copyright (c) 1998 - 2002  Frodo Looijaard <frodol@dds.nl>,
    Philip Edelbrock <phil@netroedge.com>, and Mark D. Studebaker
    <mdsxyz123@yahoo.com>

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
    SUPPORTED DEVICES	PCI ID
    82801AA		2413           
    82801AB		2423           
    82801BA		2443           
    82801CA/CAM		2483           
    82801DB		24C3   (HW PEC supported, 32 byte buffer not supported)
    82801EB		24D3   (HW PEC supported, 32 byte buffer not supported)
    6300ESB		25A4
    ICH6		266A
    ICH7		27DA
    ESB2		269B
    ICH8		283E
    This driver supports several versions of Intel's I/O Controller Hubs (ICH).
    For SMBus support, they are similar to the PIIX4 and are part
    of Intel's '810' and other chipsets.
    See the doc/busses/i2c-i801 file for details.
    I2C Block Read and Process Call are not supported.
*/

/* Note: we assume there can only be one I801, with one SMBus interface */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <asm/io.h>

/* I801 SMBus address offsets */
#define SMBHSTSTS	(0 + i801_smba)
#define SMBHSTCNT	(2 + i801_smba)
#define SMBHSTCMD	(3 + i801_smba)
#define SMBHSTADD	(4 + i801_smba)
#define SMBHSTDAT0	(5 + i801_smba)
#define SMBHSTDAT1	(6 + i801_smba)
#define SMBBLKDAT	(7 + i801_smba)
#define SMBPEC		(8 + i801_smba)	/* ICH4 only */
#define SMBAUXSTS	(12 + i801_smba)	/* ICH4 only */
#define SMBAUXCTL	(13 + i801_smba)	/* ICH4 only */

/* PCI Address Constants */
#define SMBBA		0x020
#define SMBHSTCFG	0x040
#define SMBREV		0x008

/* Host configuration bits for SMBHSTCFG */
#define SMBHSTCFG_HST_EN	1
#define SMBHSTCFG_SMB_SMI_EN	2
#define SMBHSTCFG_I2C_EN	4

/* Other settings */
#define MAX_TIMEOUT		100
#define ENABLE_INT9		0	/* set to 0x01 to enable - untested */

/* I801 command constants */
#define I801_QUICK		0x00
#define I801_BYTE		0x04
#define I801_BYTE_DATA		0x08
#define I801_WORD_DATA		0x0C
#define I801_PROC_CALL		0x10	/* later chips only, unimplemented */
#define I801_BLOCK_DATA		0x14
#define I801_I2C_BLOCK_DATA	0x18	/* unimplemented */
#define I801_BLOCK_LAST		0x34
#define I801_I2C_BLOCK_LAST	0x38	/* unimplemented */
#define I801_START		0x40
#define I801_PEC_EN		0x80	/* ICH4 only */

/* insmod parameters */

/* If force_addr is set to anything different from 0, we forcibly enable
   the I801 at the given address. VERY DANGEROUS! */
static u16 force_addr;
module_param(force_addr, ushort, 0);
MODULE_PARM_DESC(force_addr,
		 "Forcibly enable the I801 at the given address. "
		 "EXTREMELY DANGEROUS!");

static int i801_transaction(void);
static int i801_block_transaction(union i2c_smbus_data *data, char read_write,
				  int command, int hwpec);

static unsigned short i801_smba;
static struct pci_driver i801_driver;
static struct pci_dev *I801_dev;
static int isich4;

static int i801_setup(struct pci_dev *dev)
{
	int error_return = 0;
	unsigned char temp;

	/* Note: we keep on searching until we have found 'function 3' */
	if(PCI_FUNC(dev->devfn) != 3)
		return -ENODEV;

	I801_dev = dev;
	if ((dev->device == PCI_DEVICE_ID_INTEL_82801DB_3) ||
	    (dev->device == PCI_DEVICE_ID_INTEL_82801EB_3) ||
	    (dev->device == PCI_DEVICE_ID_INTEL_ESB_4))
		isich4 = 1;
	else
		isich4 = 0;

	/* Determine the address of the SMBus areas */
	if (force_addr) {
		i801_smba = force_addr & 0xfff0;
	} else {
		pci_read_config_word(I801_dev, SMBBA, &i801_smba);
		i801_smba &= 0xfff0;
		if(i801_smba == 0) {
			dev_err(&dev->dev, "SMB base address uninitialized "
				"- upgrade BIOS or use force_addr=0xaddr\n");
			return -ENODEV;
		}
	}

	if (!request_region(i801_smba, (isich4 ? 16 : 8), i801_driver.name)) {
		dev_err(&dev->dev, "I801_smb region 0x%x already in use!\n",
			i801_smba);
		error_return = -EBUSY;
		goto END;
	}

	pci_read_config_byte(I801_dev, SMBHSTCFG, &temp);
	temp &= ~SMBHSTCFG_I2C_EN;	/* SMBus timing */
	pci_write_config_byte(I801_dev, SMBHSTCFG, temp);

	/* If force_addr is set, we program the new address here. Just to make
	   sure, we disable the device first. */
	if (force_addr) {
		pci_write_config_byte(I801_dev, SMBHSTCFG, temp & 0xfe);
		pci_write_config_word(I801_dev, SMBBA, i801_smba);
		pci_write_config_byte(I801_dev, SMBHSTCFG, temp | 0x01);
		dev_warn(&dev->dev, "WARNING: I801 SMBus interface set to "
			"new address %04x!\n", i801_smba);
	} else if ((temp & 1) == 0) {
		pci_write_config_byte(I801_dev, SMBHSTCFG, temp | 1);
		dev_warn(&dev->dev, "enabling SMBus device\n");
	}

	if (temp & 0x02)
		dev_dbg(&dev->dev, "I801 using Interrupt SMI# for SMBus.\n");
	else
		dev_dbg(&dev->dev, "I801 using PCI Interrupt for SMBus.\n");

	pci_read_config_byte(I801_dev, SMBREV, &temp);
	dev_dbg(&dev->dev, "SMBREV = 0x%X\n", temp);
	dev_dbg(&dev->dev, "I801_smba = 0x%X\n", i801_smba);

END:
	return error_return;
}

static int i801_transaction(void)
{
	int temp;
	int result = 0;
	int timeout = 0;

	dev_dbg(&I801_dev->dev, "Transaction (pre): CNT=%02x, CMD=%02x, "
		"ADD=%02x, DAT0=%02x, DAT1=%02x\n", inb_p(SMBHSTCNT),
		inb_p(SMBHSTCMD), inb_p(SMBHSTADD), inb_p(SMBHSTDAT0),
		inb_p(SMBHSTDAT1));

	/* Make sure the SMBus host is ready to start transmitting */
	/* 0x1f = Failed, Bus_Err, Dev_Err, Intr, Host_Busy */
	if ((temp = (0x1f & inb_p(SMBHSTSTS))) != 0x00) {
		dev_dbg(&I801_dev->dev, "SMBus busy (%02x). Resetting...\n",
			temp);
		outb_p(temp, SMBHSTSTS);
		if ((temp = (0x1f & inb_p(SMBHSTSTS))) != 0x00) {
			dev_dbg(&I801_dev->dev, "Failed! (%02x)\n", temp);
			return -1;
		} else {
			dev_dbg(&I801_dev->dev, "Successfull!\n");
		}
	}

	outb_p(inb(SMBHSTCNT) | I801_START, SMBHSTCNT);

	/* We will always wait for a fraction of a second! */
	do {
		msleep(1);
		temp = inb_p(SMBHSTSTS);
	} while ((temp & 0x01) && (timeout++ < MAX_TIMEOUT));

	/* If the SMBus is still busy, we give up */
	if (timeout >= MAX_TIMEOUT) {
		dev_dbg(&I801_dev->dev, "SMBus Timeout!\n");
		result = -1;
	}

	if (temp & 0x10) {
		result = -1;
		dev_dbg(&I801_dev->dev, "Error: Failed bus transaction\n");
	}

	if (temp & 0x08) {
		result = -1;
		dev_err(&I801_dev->dev, "Bus collision! SMBus may be locked "
			"until next hard reset. (sorry!)\n");
		/* Clock stops and slave is stuck in mid-transmission */
	}

	if (temp & 0x04) {
		result = -1;
		dev_dbg(&I801_dev->dev, "Error: no response!\n");
	}

	if ((inb_p(SMBHSTSTS) & 0x1f) != 0x00)
		outb_p(inb(SMBHSTSTS), SMBHSTSTS);

	if ((temp = (0x1f & inb_p(SMBHSTSTS))) != 0x00) {
		dev_dbg(&I801_dev->dev, "Failed reset at end of transaction "
			"(%02x)\n", temp);
	}
	dev_dbg(&I801_dev->dev, "Transaction (post): CNT=%02x, CMD=%02x, "
		"ADD=%02x, DAT0=%02x, DAT1=%02x\n", inb_p(SMBHSTCNT),
		inb_p(SMBHSTCMD), inb_p(SMBHSTADD), inb_p(SMBHSTDAT0),
		inb_p(SMBHSTDAT1));
	return result;
}

/* All-inclusive block transaction function */
static int i801_block_transaction(union i2c_smbus_data *data, char read_write,
				  int command, int hwpec)
{
	int i, len;
	int smbcmd;
	int temp;
	int result = 0;
	int timeout;
	unsigned char hostc, errmask;

	if (command == I2C_SMBUS_I2C_BLOCK_DATA) {
		if (read_write == I2C_SMBUS_WRITE) {
			/* set I2C_EN bit in configuration register */
			pci_read_config_byte(I801_dev, SMBHSTCFG, &hostc);
			pci_write_config_byte(I801_dev, SMBHSTCFG,
					      hostc | SMBHSTCFG_I2C_EN);
		} else {
			dev_err(&I801_dev->dev,
				"I2C_SMBUS_I2C_BLOCK_READ not DB!\n");
			return -1;
		}
	}

	if (read_write == I2C_SMBUS_WRITE) {
		len = data->block[0];
		if (len < 1)
			len = 1;
		if (len > 32)
			len = 32;
		outb_p(len, SMBHSTDAT0);
		outb_p(data->block[1], SMBBLKDAT);
	} else {
		len = 32;	/* max for reads */
	}

	if(isich4 && command != I2C_SMBUS_I2C_BLOCK_DATA) {
		/* set 32 byte buffer */
	}

	for (i = 1; i <= len; i++) {
		if (i == len && read_write == I2C_SMBUS_READ)
			smbcmd = I801_BLOCK_LAST;
		else
			smbcmd = I801_BLOCK_DATA;
		outb_p(smbcmd | ENABLE_INT9, SMBHSTCNT);

		dev_dbg(&I801_dev->dev, "Block (pre %d): CNT=%02x, CMD=%02x, "
			"ADD=%02x, DAT0=%02x, BLKDAT=%02x\n", i,
			inb_p(SMBHSTCNT), inb_p(SMBHSTCMD), inb_p(SMBHSTADD),
			inb_p(SMBHSTDAT0), inb_p(SMBBLKDAT));

		/* Make sure the SMBus host is ready to start transmitting */
		temp = inb_p(SMBHSTSTS);
		if (i == 1) {
			/* Erronenous conditions before transaction: 
			 * Byte_Done, Failed, Bus_Err, Dev_Err, Intr, Host_Busy */
			errmask=0x9f; 
		} else {
			/* Erronenous conditions during transaction: 
			 * Failed, Bus_Err, Dev_Err, Intr */
			errmask=0x1e; 
		}
		if (temp & errmask) {
			dev_dbg(&I801_dev->dev, "SMBus busy (%02x). "
				"Resetting...\n", temp);
			outb_p(temp, SMBHSTSTS);
			if (((temp = inb_p(SMBHSTSTS)) & errmask) != 0x00) {
				dev_err(&I801_dev->dev,
					"Reset failed! (%02x)\n", temp);
				result = -1;
                                goto END;
			}
			if (i != 1) {
				/* if die in middle of block transaction, fail */
				result = -1;
				goto END;
			}
		}

		if (i == 1)
			outb_p(inb(SMBHSTCNT) | I801_START, SMBHSTCNT);

		/* We will always wait for a fraction of a second! */
		timeout = 0;
		do {
			temp = inb_p(SMBHSTSTS);
			msleep(1);
		}
		    while ((!(temp & 0x80))
			   && (timeout++ < MAX_TIMEOUT));

		/* If the SMBus is still busy, we give up */
		if (timeout >= MAX_TIMEOUT) {
			result = -1;
			dev_dbg(&I801_dev->dev, "SMBus Timeout!\n");
		}

		if (temp & 0x10) {
			result = -1;
			dev_dbg(&I801_dev->dev,
				"Error: Failed bus transaction\n");
		} else if (temp & 0x08) {
			result = -1;
			dev_err(&I801_dev->dev, "Bus collision!\n");
		} else if (temp & 0x04) {
			result = -1;
			dev_dbg(&I801_dev->dev, "Error: no response!\n");
		}

		if (i == 1 && read_write == I2C_SMBUS_READ) {
			len = inb_p(SMBHSTDAT0);
			if (len < 1)
				len = 1;
			if (len > 32)
				len = 32;
			data->block[0] = len;
		}

		/* Retrieve/store value in SMBBLKDAT */
		if (read_write == I2C_SMBUS_READ)
			data->block[i] = inb_p(SMBBLKDAT);
		if (read_write == I2C_SMBUS_WRITE && i+1 <= len)
			outb_p(data->block[i+1], SMBBLKDAT);
		if ((temp & 0x9e) != 0x00)
			outb_p(temp, SMBHSTSTS);  /* signals SMBBLKDAT ready */

		if ((temp = (0x1e & inb_p(SMBHSTSTS))) != 0x00) {
			dev_dbg(&I801_dev->dev,
				"Bad status (%02x) at end of transaction\n",
				temp);
		}
		dev_dbg(&I801_dev->dev, "Block (post %d): CNT=%02x, CMD=%02x, "
			"ADD=%02x, DAT0=%02x, BLKDAT=%02x\n", i,
			inb_p(SMBHSTCNT), inb_p(SMBHSTCMD), inb_p(SMBHSTADD),
			inb_p(SMBHSTDAT0), inb_p(SMBBLKDAT));

		if (result < 0)
			goto END;
	}

	if (hwpec) {
		/* wait for INTR bit as advised by Intel */
		timeout = 0;
		do {
			temp = inb_p(SMBHSTSTS);
			msleep(1);
		} while ((!(temp & 0x02))
			   && (timeout++ < MAX_TIMEOUT));

		if (timeout >= MAX_TIMEOUT) {
			dev_dbg(&I801_dev->dev, "PEC Timeout!\n");
		}
		outb_p(temp, SMBHSTSTS); 
	}
	result = 0;
END:
	if (command == I2C_SMBUS_I2C_BLOCK_DATA) {
		/* restore saved configuration register value */
		pci_write_config_byte(I801_dev, SMBHSTCFG, hostc);
	}
	return result;
}

/* Return -1 on error. */
static s32 i801_access(struct i2c_adapter * adap, u16 addr,
		       unsigned short flags, char read_write, u8 command,
		       int size, union i2c_smbus_data * data)
{
	int hwpec;
	int block = 0;
	int ret, xact = 0;

	hwpec = isich4 && (flags & I2C_CLIENT_PEC)
		&& size != I2C_SMBUS_QUICK
		&& size != I2C_SMBUS_I2C_BLOCK_DATA;

	switch (size) {
	case I2C_SMBUS_QUICK:
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMBHSTADD);
		xact = I801_QUICK;
		break;
	case I2C_SMBUS_BYTE:
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMBHSTADD);
		if (read_write == I2C_SMBUS_WRITE)
			outb_p(command, SMBHSTCMD);
		xact = I801_BYTE;
		break;
	case I2C_SMBUS_BYTE_DATA:
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMBHSTADD);
		outb_p(command, SMBHSTCMD);
		if (read_write == I2C_SMBUS_WRITE)
			outb_p(data->byte, SMBHSTDAT0);
		xact = I801_BYTE_DATA;
		break;
	case I2C_SMBUS_WORD_DATA:
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMBHSTADD);
		outb_p(command, SMBHSTCMD);
		if (read_write == I2C_SMBUS_WRITE) {
			outb_p(data->word & 0xff, SMBHSTDAT0);
			outb_p((data->word & 0xff00) >> 8, SMBHSTDAT1);
		}
		xact = I801_WORD_DATA;
		break;
	case I2C_SMBUS_BLOCK_DATA:
	case I2C_SMBUS_I2C_BLOCK_DATA:
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMBHSTADD);
		outb_p(command, SMBHSTCMD);
		block = 1;
		break;
	case I2C_SMBUS_PROC_CALL:
	default:
		dev_err(&I801_dev->dev, "Unsupported transaction %d\n", size);
		return -1;
	}

	outb_p(hwpec, SMBAUXCTL);	/* enable/disable hardware PEC */

	if(block)
		ret = i801_block_transaction(data, read_write, size, hwpec);
	else {
		outb_p(xact | ENABLE_INT9, SMBHSTCNT);
		ret = i801_transaction();
	}

	if(block)
		return ret;
	if(ret)
		return -1;
	if ((read_write == I2C_SMBUS_WRITE) || (xact == I801_QUICK))
		return 0;

	switch (xact & 0x7f) {
	case I801_BYTE:	/* Result put in SMBHSTDAT0 */
	case I801_BYTE_DATA:
		data->byte = inb_p(SMBHSTDAT0);
		break;
	case I801_WORD_DATA:
		data->word = inb_p(SMBHSTDAT0) + (inb_p(SMBHSTDAT1) << 8);
		break;
	}
	return 0;
}


static u32 i801_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
	    I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
	    I2C_FUNC_SMBUS_BLOCK_DATA | I2C_FUNC_SMBUS_WRITE_I2C_BLOCK
	     | (isich4 ? I2C_FUNC_SMBUS_HWPEC_CALC : 0);
}

static struct i2c_algorithm smbus_algorithm = {
	.smbus_xfer	= i801_access,
	.functionality	= i801_func,
};

static struct i2c_adapter i801_adapter = {
	.owner		= THIS_MODULE,
	.class		= I2C_CLASS_HWMON,
	.algo		= &smbus_algorithm,
};

static struct pci_device_id i801_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801AA_3) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801AB_3) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801BA_2) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801CA_3) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801DB_3) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801EB_3) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ESB_4) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH6_16) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH7_17) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ESB2_17) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH8_5) },
	{ 0, }
};

MODULE_DEVICE_TABLE (pci, i801_ids);

static int __devinit i801_probe(struct pci_dev *dev, const struct pci_device_id *id)
{

	if (i801_setup(dev)) {
		dev_warn(&dev->dev,
			"I801 not detected, module not inserted.\n");
		return -ENODEV;
	}

	/* set up the driverfs linkage to our parent device */
	i801_adapter.dev.parent = &dev->dev;

	snprintf(i801_adapter.name, I2C_NAME_SIZE,
		"SMBus I801 adapter at %04x", i801_smba);
	return i2c_add_adapter(&i801_adapter);
}

static void __devexit i801_remove(struct pci_dev *dev)
{
	i2c_del_adapter(&i801_adapter);
	release_region(i801_smba, (isich4 ? 16 : 8));
}

static struct pci_driver i801_driver = {
	.name		= "i801_smbus",
	.id_table	= i801_ids,
	.probe		= i801_probe,
	.remove		= __devexit_p(i801_remove),
};

static int __init i2c_i801_init(void)
{
	return pci_register_driver(&i801_driver);
}

static void __exit i2c_i801_exit(void)
{
	pci_unregister_driver(&i801_driver);
}

MODULE_AUTHOR ("Frodo Looijaard <frodol@dds.nl>, "
		"Philip Edelbrock <phil@netroedge.com>, "
		"and Mark D. Studebaker <mdsxyz123@yahoo.com>");
MODULE_DESCRIPTION("I801 SMBus driver");
MODULE_LICENSE("GPL");

module_init(i2c_i801_init);
module_exit(i2c_i801_exit);
