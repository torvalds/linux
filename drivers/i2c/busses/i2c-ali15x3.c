/*
    ali15x3.c - Part of lm_sensors, Linux kernel modules for hardware
              monitoring
    Copyright (c) 1999  Frodo Looijaard <frodol@dds.nl> and
    Philip Edelbrock <phil@netroedge.com> and
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
    This is the driver for the SMB Host controller on
    Acer Labs Inc. (ALI) M1541 and M1543C South Bridges.

    The M1543C is a South bridge for desktop systems.
    The M1533 is a South bridge for portable systems.
    They are part of the following ALI chipsets:
       "Aladdin Pro 2": Includes the M1621 Slot 1 North bridge
       with AGP and 100MHz CPU Front Side bus
       "Aladdin V": Includes the M1541 Socket 7 North bridge
       with AGP and 100MHz CPU Front Side bus
       "Aladdin IV": Includes the M1541 Socket 7 North bridge
       with host bus up to 83.3 MHz.
    For an overview of these chips see http://www.acerlabs.com

    The M1533/M1543C devices appear as FOUR separate devices
    on the PCI bus. An output of lspci will show something similar
    to the following:

	00:02.0 USB Controller: Acer Laboratories Inc. M5237
	00:03.0 Bridge: Acer Laboratories Inc. M7101
	00:07.0 ISA bridge: Acer Laboratories Inc. M1533
	00:0f.0 IDE interface: Acer Laboratories Inc. M5229

    The SMB controller is part of the 7101 device, which is an
    ACPI-compliant Power Management Unit (PMU).

    The whole 7101 device has to be enabled for the SMB to work.
    You can't just enable the SMB alone.
    The SMB and the ACPI have separate I/O spaces.
    We make sure that the SMB is enabled. We leave the ACPI alone.

    This driver controls the SMB Host only.
    The SMB Slave controller on the M15X3 is not enabled.

    This driver does not use interrupts.
*/

/* Note: we assume there can only be one ALI15X3, with one SMBus interface */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <asm/io.h>

/* ALI15X3 SMBus address offsets */
#define SMBHSTSTS	(0 + ali15x3_smba)
#define SMBHSTCNT	(1 + ali15x3_smba)
#define SMBHSTSTART	(2 + ali15x3_smba)
#define SMBHSTCMD	(7 + ali15x3_smba)
#define SMBHSTADD	(3 + ali15x3_smba)
#define SMBHSTDAT0	(4 + ali15x3_smba)
#define SMBHSTDAT1	(5 + ali15x3_smba)
#define SMBBLKDAT	(6 + ali15x3_smba)

/* PCI Address Constants */
#define SMBCOM		0x004
#define SMBBA		0x014
#define SMBATPC		0x05B	/* used to unlock xxxBA registers */
#define SMBHSTCFG	0x0E0
#define SMBSLVC		0x0E1
#define SMBCLK		0x0E2
#define SMBREV		0x008

/* Other settings */
#define MAX_TIMEOUT		200	/* times 1/100 sec */
#define ALI15X3_SMB_IOSIZE	32

/* this is what the Award 1004 BIOS sets them to on a ASUS P5A MB.
   We don't use these here. If the bases aren't set to some value we
   tell user to upgrade BIOS and we fail.
*/
#define ALI15X3_SMB_DEFAULTBASE	0xE800

/* ALI15X3 address lock bits */
#define ALI15X3_LOCK		0x06

/* ALI15X3 command constants */
#define ALI15X3_ABORT		0x02
#define ALI15X3_T_OUT		0x04
#define ALI15X3_QUICK		0x00
#define ALI15X3_BYTE		0x10
#define ALI15X3_BYTE_DATA	0x20
#define ALI15X3_WORD_DATA	0x30
#define ALI15X3_BLOCK_DATA	0x40
#define ALI15X3_BLOCK_CLR	0x80

/* ALI15X3 status register bits */
#define ALI15X3_STS_IDLE	0x04
#define ALI15X3_STS_BUSY	0x08
#define ALI15X3_STS_DONE	0x10
#define ALI15X3_STS_DEV		0x20	/* device error */
#define ALI15X3_STS_COLL	0x40	/* collision or no response */
#define ALI15X3_STS_TERM	0x80	/* terminated by abort */
#define ALI15X3_STS_ERR		0xE0	/* all the bad error bits */


/* If force_addr is set to anything different from 0, we forcibly enable
   the device at the given address. */
static u16 force_addr;
module_param(force_addr, ushort, 0);
MODULE_PARM_DESC(force_addr,
		 "Initialize the base address of the i2c controller");

static struct pci_driver ali15x3_driver;
static unsigned short ali15x3_smba;

static int ali15x3_setup(struct pci_dev *ALI15X3_dev)
{
	u16 a;
	unsigned char temp;

	/* Check the following things:
		- SMB I/O address is initialized
		- Device is enabled
		- We can use the addresses
	*/

	/* Unlock the register.
	   The data sheet says that the address registers are read-only
	   if the lock bits are 1, but in fact the address registers
	   are zero unless you clear the lock bits.
	*/
	pci_read_config_byte(ALI15X3_dev, SMBATPC, &temp);
	if (temp & ALI15X3_LOCK) {
		temp &= ~ALI15X3_LOCK;
		pci_write_config_byte(ALI15X3_dev, SMBATPC, temp);
	}

	/* Determine the address of the SMBus area */
	pci_read_config_word(ALI15X3_dev, SMBBA, &ali15x3_smba);
	ali15x3_smba &= (0xffff & ~(ALI15X3_SMB_IOSIZE - 1));
	if (ali15x3_smba == 0 && force_addr == 0) {
		dev_err(&ALI15X3_dev->dev, "ALI15X3_smb region uninitialized "
			"- upgrade BIOS or use force_addr=0xaddr\n");
		return -ENODEV;
	}

	if(force_addr)
		ali15x3_smba = force_addr & ~(ALI15X3_SMB_IOSIZE - 1);

	if (!request_region(ali15x3_smba, ALI15X3_SMB_IOSIZE,
			    ali15x3_driver.name)) {
		dev_err(&ALI15X3_dev->dev,
			"ALI15X3_smb region 0x%x already in use!\n",
			ali15x3_smba);
		return -ENODEV;
	}

	if(force_addr) {
		dev_info(&ALI15X3_dev->dev, "forcing ISA address 0x%04X\n",
			ali15x3_smba);
		if (PCIBIOS_SUCCESSFUL != pci_write_config_word(ALI15X3_dev,
								SMBBA,
								ali15x3_smba))
			goto error;
		if (PCIBIOS_SUCCESSFUL != pci_read_config_word(ALI15X3_dev,
								SMBBA, &a))
			goto error;
		if ((a & ~(ALI15X3_SMB_IOSIZE - 1)) != ali15x3_smba) {
			/* make sure it works */
			dev_err(&ALI15X3_dev->dev,
				"force address failed - not supported?\n");
			goto error;
		}
	}
	/* check if whole device is enabled */
	pci_read_config_byte(ALI15X3_dev, SMBCOM, &temp);
	if ((temp & 1) == 0) {
		dev_info(&ALI15X3_dev->dev, "enabling SMBus device\n");
		pci_write_config_byte(ALI15X3_dev, SMBCOM, temp | 0x01);
	}

	/* Is SMB Host controller enabled? */
	pci_read_config_byte(ALI15X3_dev, SMBHSTCFG, &temp);
	if ((temp & 1) == 0) {
		dev_info(&ALI15X3_dev->dev, "enabling SMBus controller\n");
		pci_write_config_byte(ALI15X3_dev, SMBHSTCFG, temp | 0x01);
	}

	/* set SMB clock to 74KHz as recommended in data sheet */
	pci_write_config_byte(ALI15X3_dev, SMBCLK, 0x20);

	/*
	  The interrupt routing for SMB is set up in register 0x77 in the
	  1533 ISA Bridge device, NOT in the 7101 device.
	  Don't bother with finding the 1533 device and reading the register.
	if ((....... & 0x0F) == 1)
		dev_dbg(&ALI15X3_dev->dev, "ALI15X3 using Interrupt 9 for SMBus.\n");
	*/
	pci_read_config_byte(ALI15X3_dev, SMBREV, &temp);
	dev_dbg(&ALI15X3_dev->dev, "SMBREV = 0x%X\n", temp);
	dev_dbg(&ALI15X3_dev->dev, "iALI15X3_smba = 0x%X\n", ali15x3_smba);

	return 0;
error:
	release_region(ali15x3_smba, ALI15X3_SMB_IOSIZE);
	return -ENODEV;
}

/* Another internally used function */
static int ali15x3_transaction(struct i2c_adapter *adap)
{
	int temp;
	int result = 0;
	int timeout = 0;

	dev_dbg(&adap->dev, "Transaction (pre): STS=%02x, CNT=%02x, CMD=%02x, "
		"ADD=%02x, DAT0=%02x, DAT1=%02x\n", inb_p(SMBHSTSTS),
		inb_p(SMBHSTCNT), inb_p(SMBHSTCMD), inb_p(SMBHSTADD),
		inb_p(SMBHSTDAT0), inb_p(SMBHSTDAT1));

	/* get status */
	temp = inb_p(SMBHSTSTS);

	/* Make sure the SMBus host is ready to start transmitting */
	/* Check the busy bit first */
	if (temp & ALI15X3_STS_BUSY) {
	/*
	   If the host controller is still busy, it may have timed out in the
	   previous transaction, resulting in a "SMBus Timeout" Dev.
	   I've tried the following to reset a stuck busy bit.
		1. Reset the controller with an ABORT command.
		   (this doesn't seem to clear the controller if an external
		   device is hung)
		2. Reset the controller and the other SMBus devices with a
		   T_OUT command.  (this clears the host busy bit if an
		   external device is hung, but it comes back upon a new access
		   to a device)
		3. Disable and reenable the controller in SMBHSTCFG
	   Worst case, nothing seems to work except power reset.
	*/
	/* Abort - reset the host controller */
	/*
	   Try resetting entire SMB bus, including other devices -
	   This may not work either - it clears the BUSY bit but
	   then the BUSY bit may come back on when you try and use the chip again.
	   If that's the case you are stuck.
	*/
		dev_info(&adap->dev, "Resetting entire SMB Bus to "
			"clear busy condition (%02x)\n", temp);
		outb_p(ALI15X3_T_OUT, SMBHSTCNT);
		temp = inb_p(SMBHSTSTS);
	}

	/* now check the error bits and the busy bit */
	if (temp & (ALI15X3_STS_ERR | ALI15X3_STS_BUSY)) {
		/* do a clear-on-write */
		outb_p(0xFF, SMBHSTSTS);
		if ((temp = inb_p(SMBHSTSTS)) &
		    (ALI15X3_STS_ERR | ALI15X3_STS_BUSY)) {
			/* this is probably going to be correctable only by a power reset
			   as one of the bits now appears to be stuck */
			/* This may be a bus or device with electrical problems. */
			dev_err(&adap->dev, "SMBus reset failed! (0x%02x) - "
				"controller or device on bus is probably hung\n",
				temp);
			return -1;
		}
	} else {
		/* check and clear done bit */
		if (temp & ALI15X3_STS_DONE) {
			outb_p(temp, SMBHSTSTS);
		}
	}

	/* start the transaction by writing anything to the start register */
	outb_p(0xFF, SMBHSTSTART);

	/* We will always wait for a fraction of a second! */
	timeout = 0;
	do {
		msleep(1);
		temp = inb_p(SMBHSTSTS);
	} while ((!(temp & (ALI15X3_STS_ERR | ALI15X3_STS_DONE)))
		 && (timeout++ < MAX_TIMEOUT));

	/* If the SMBus is still busy, we give up */
	if (timeout >= MAX_TIMEOUT) {
		result = -1;
		dev_err(&adap->dev, "SMBus Timeout!\n");
	}

	if (temp & ALI15X3_STS_TERM) {
		result = -1;
		dev_dbg(&adap->dev, "Error: Failed bus transaction\n");
	}

	/*
	  Unfortunately the ALI SMB controller maps "no response" and "bus
	  collision" into a single bit. No reponse is the usual case so don't
	  do a printk.
	  This means that bus collisions go unreported.
	*/
	if (temp & ALI15X3_STS_COLL) {
		result = -1;
		dev_dbg(&adap->dev,
			"Error: no response or bus collision ADD=%02x\n",
			inb_p(SMBHSTADD));
	}

	/* haven't ever seen this */
	if (temp & ALI15X3_STS_DEV) {
		result = -1;
		dev_err(&adap->dev, "Error: device error\n");
	}
	dev_dbg(&adap->dev, "Transaction (post): STS=%02x, CNT=%02x, CMD=%02x, "
		"ADD=%02x, DAT0=%02x, DAT1=%02x\n", inb_p(SMBHSTSTS),
		inb_p(SMBHSTCNT), inb_p(SMBHSTCMD), inb_p(SMBHSTADD),
		inb_p(SMBHSTDAT0), inb_p(SMBHSTDAT1));
	return result;
}

/* Return -1 on error. */
static s32 ali15x3_access(struct i2c_adapter * adap, u16 addr,
		   unsigned short flags, char read_write, u8 command,
		   int size, union i2c_smbus_data * data)
{
	int i, len;
	int temp;
	int timeout;

	/* clear all the bits (clear-on-write) */
	outb_p(0xFF, SMBHSTSTS);
	/* make sure SMBus is idle */
	temp = inb_p(SMBHSTSTS);
	for (timeout = 0;
	     (timeout < MAX_TIMEOUT) && !(temp & ALI15X3_STS_IDLE);
	     timeout++) {
		msleep(1);
		temp = inb_p(SMBHSTSTS);
	}
	if (timeout >= MAX_TIMEOUT) {
		dev_err(&adap->dev, "Idle wait Timeout! STS=0x%02x\n", temp);
	}

	switch (size) {
	case I2C_SMBUS_PROC_CALL:
		dev_err(&adap->dev, "I2C_SMBUS_PROC_CALL not supported!\n");
		return -1;
	case I2C_SMBUS_QUICK:
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMBHSTADD);
		size = ALI15X3_QUICK;
		break;
	case I2C_SMBUS_BYTE:
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMBHSTADD);
		if (read_write == I2C_SMBUS_WRITE)
			outb_p(command, SMBHSTCMD);
		size = ALI15X3_BYTE;
		break;
	case I2C_SMBUS_BYTE_DATA:
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMBHSTADD);
		outb_p(command, SMBHSTCMD);
		if (read_write == I2C_SMBUS_WRITE)
			outb_p(data->byte, SMBHSTDAT0);
		size = ALI15X3_BYTE_DATA;
		break;
	case I2C_SMBUS_WORD_DATA:
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMBHSTADD);
		outb_p(command, SMBHSTCMD);
		if (read_write == I2C_SMBUS_WRITE) {
			outb_p(data->word & 0xff, SMBHSTDAT0);
			outb_p((data->word & 0xff00) >> 8, SMBHSTDAT1);
		}
		size = ALI15X3_WORD_DATA;
		break;
	case I2C_SMBUS_BLOCK_DATA:
		outb_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMBHSTADD);
		outb_p(command, SMBHSTCMD);
		if (read_write == I2C_SMBUS_WRITE) {
			len = data->block[0];
			if (len < 0) {
				len = 0;
				data->block[0] = len;
			}
			if (len > 32) {
				len = 32;
				data->block[0] = len;
			}
			outb_p(len, SMBHSTDAT0);
			/* Reset SMBBLKDAT */
			outb_p(inb_p(SMBHSTCNT) | ALI15X3_BLOCK_CLR, SMBHSTCNT);
			for (i = 1; i <= len; i++)
				outb_p(data->block[i], SMBBLKDAT);
		}
		size = ALI15X3_BLOCK_DATA;
		break;
	}

	outb_p(size, SMBHSTCNT);	/* output command */

	if (ali15x3_transaction(adap))	/* Error in transaction */
		return -1;

	if ((read_write == I2C_SMBUS_WRITE) || (size == ALI15X3_QUICK))
		return 0;


	switch (size) {
	case ALI15X3_BYTE:	/* Result put in SMBHSTDAT0 */
		data->byte = inb_p(SMBHSTDAT0);
		break;
	case ALI15X3_BYTE_DATA:
		data->byte = inb_p(SMBHSTDAT0);
		break;
	case ALI15X3_WORD_DATA:
		data->word = inb_p(SMBHSTDAT0) + (inb_p(SMBHSTDAT1) << 8);
		break;
	case ALI15X3_BLOCK_DATA:
		len = inb_p(SMBHSTDAT0);
		if (len > 32)
			len = 32;
		data->block[0] = len;
		/* Reset SMBBLKDAT */
		outb_p(inb_p(SMBHSTCNT) | ALI15X3_BLOCK_CLR, SMBHSTCNT);
		for (i = 1; i <= data->block[0]; i++) {
			data->block[i] = inb_p(SMBBLKDAT);
			dev_dbg(&adap->dev, "Blk: len=%d, i=%d, data=%02x\n",
				len, i, data->block[i]);
		}
		break;
	}
	return 0;
}

static u32 ali15x3_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
	    I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
	    I2C_FUNC_SMBUS_BLOCK_DATA;
}

static struct i2c_algorithm smbus_algorithm = {
	.smbus_xfer	= ali15x3_access,
	.functionality	= ali15x3_func,
};

static struct i2c_adapter ali15x3_adapter = {
	.owner		= THIS_MODULE,
	.class          = I2C_CLASS_HWMON,
	.algo		= &smbus_algorithm,
};

static struct pci_device_id ali15x3_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M7101) },
	{ 0, }
};

MODULE_DEVICE_TABLE (pci, ali15x3_ids);

static int __devinit ali15x3_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	if (ali15x3_setup(dev)) {
		dev_err(&dev->dev,
			"ALI15X3 not detected, module not inserted.\n");
		return -ENODEV;
	}

	/* set up the driverfs linkage to our parent device */
	ali15x3_adapter.dev.parent = &dev->dev;

	snprintf(ali15x3_adapter.name, I2C_NAME_SIZE,
		"SMBus ALI15X3 adapter at %04x", ali15x3_smba);
	return i2c_add_adapter(&ali15x3_adapter);
}

static void __devexit ali15x3_remove(struct pci_dev *dev)
{
	i2c_del_adapter(&ali15x3_adapter);
	release_region(ali15x3_smba, ALI15X3_SMB_IOSIZE);
}

static struct pci_driver ali15x3_driver = {
	.name		= "ali15x3_smbus",
	.id_table	= ali15x3_ids,
	.probe		= ali15x3_probe,
	.remove		= __devexit_p(ali15x3_remove),
};

static int __init i2c_ali15x3_init(void)
{
	return pci_register_driver(&ali15x3_driver);
}

static void __exit i2c_ali15x3_exit(void)
{
	pci_unregister_driver(&ali15x3_driver);
}

MODULE_AUTHOR ("Frodo Looijaard <frodol@dds.nl>, "
		"Philip Edelbrock <phil@netroedge.com>, "
		"and Mark D. Studebaker <mdsxyz123@yahoo.com>");
MODULE_DESCRIPTION("ALI15X3 SMBus driver");
MODULE_LICENSE("GPL");

module_init(i2c_ali15x3_init);
module_exit(i2c_ali15x3_exit);
