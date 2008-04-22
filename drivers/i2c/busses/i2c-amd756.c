/*
    amd756.c - Part of lm_sensors, Linux kernel modules for hardware
              monitoring

    Copyright (c) 1999-2002 Merlin Hughes <merlin@merlin.org>

    Shamelessly ripped from i2c-piix4.c:

    Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl> and
    Philip Edelbrock <phil@netroedge.com>

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
    2002-04-08: Added nForce support. (Csaba Halasz)
    2002-10-03: Fixed nForce PnP I/O port. (Michael Steil)
    2002-12-28: Rewritten into something that resembles a Linux driver (hch)
    2003-11-29: Added back AMD8111 removed by the previous rewrite.
                (Philip Pokorny)
*/

/*
   Supports AMD756, AMD766, AMD768, AMD8111 and nVidia nForce
   Note: we assume there can only be one device, with one SMBus interface.
*/

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/stddef.h>
#include <linux/ioport.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <asm/io.h>

/* AMD756 SMBus address offsets */
#define SMB_ADDR_OFFSET		0xE0
#define SMB_IOSIZE		16
#define SMB_GLOBAL_STATUS	(0x0 + amd756_ioport)
#define SMB_GLOBAL_ENABLE	(0x2 + amd756_ioport)
#define SMB_HOST_ADDRESS	(0x4 + amd756_ioport)
#define SMB_HOST_DATA		(0x6 + amd756_ioport)
#define SMB_HOST_COMMAND	(0x8 + amd756_ioport)
#define SMB_HOST_BLOCK_DATA	(0x9 + amd756_ioport)
#define SMB_HAS_DATA		(0xA + amd756_ioport)
#define SMB_HAS_DEVICE_ADDRESS	(0xC + amd756_ioport)
#define SMB_HAS_HOST_ADDRESS	(0xE + amd756_ioport)
#define SMB_SNOOP_ADDRESS	(0xF + amd756_ioport)

/* PCI Address Constants */

/* address of I/O space */
#define SMBBA		0x058		/* mh */
#define SMBBANFORCE	0x014

/* general configuration */
#define SMBGCFG		0x041		/* mh */

/* silicon revision code */
#define SMBREV		0x008

/* Other settings */
#define MAX_TIMEOUT	500

/* AMD756 constants */
#define AMD756_QUICK		0x00
#define AMD756_BYTE		0x01
#define AMD756_BYTE_DATA	0x02
#define AMD756_WORD_DATA	0x03
#define AMD756_PROCESS_CALL	0x04
#define AMD756_BLOCK_DATA	0x05

static struct pci_driver amd756_driver;
static unsigned short amd756_ioport;

/* 
  SMBUS event = I/O 28-29 bit 11
     see E0 for the status bits and enabled in E2
     
*/
#define GS_ABRT_STS	(1 << 0)
#define GS_COL_STS	(1 << 1)
#define GS_PRERR_STS	(1 << 2)
#define GS_HST_STS	(1 << 3)
#define GS_HCYC_STS	(1 << 4)
#define GS_TO_STS	(1 << 5)
#define GS_SMB_STS	(1 << 11)

#define GS_CLEAR_STS	(GS_ABRT_STS | GS_COL_STS | GS_PRERR_STS | \
			 GS_HCYC_STS | GS_TO_STS )

#define GE_CYC_TYPE_MASK	(7)
#define GE_HOST_STC		(1 << 3)
#define GE_ABORT		(1 << 5)


static int amd756_transaction(struct i2c_adapter *adap)
{
	int temp;
	int result = 0;
	int timeout = 0;

	dev_dbg(&adap->dev, "Transaction (pre): GS=%04x, GE=%04x, ADD=%04x, "
		"DAT=%04x\n", inw_p(SMB_GLOBAL_STATUS),
		inw_p(SMB_GLOBAL_ENABLE), inw_p(SMB_HOST_ADDRESS),
		inb_p(SMB_HOST_DATA));

	/* Make sure the SMBus host is ready to start transmitting */
	if ((temp = inw_p(SMB_GLOBAL_STATUS)) & (GS_HST_STS | GS_SMB_STS)) {
		dev_dbg(&adap->dev, "SMBus busy (%04x). Waiting...\n", temp);
		do {
			msleep(1);
			temp = inw_p(SMB_GLOBAL_STATUS);
		} while ((temp & (GS_HST_STS | GS_SMB_STS)) &&
		         (timeout++ < MAX_TIMEOUT));
		/* If the SMBus is still busy, we give up */
		if (timeout >= MAX_TIMEOUT) {
			dev_dbg(&adap->dev, "Busy wait timeout (%04x)\n", temp);
			goto abort;
		}
		timeout = 0;
	}

	/* start the transaction by setting the start bit */
	outw_p(inw(SMB_GLOBAL_ENABLE) | GE_HOST_STC, SMB_GLOBAL_ENABLE);

	/* We will always wait for a fraction of a second! */
	do {
		msleep(1);
		temp = inw_p(SMB_GLOBAL_STATUS);
	} while ((temp & GS_HST_STS) && (timeout++ < MAX_TIMEOUT));

	/* If the SMBus is still busy, we give up */
	if (timeout >= MAX_TIMEOUT) {
		dev_dbg(&adap->dev, "Completion timeout!\n");
		goto abort;
	}

	if (temp & GS_PRERR_STS) {
		result = -1;
		dev_dbg(&adap->dev, "SMBus Protocol error (no response)!\n");
	}

	if (temp & GS_COL_STS) {
		result = -1;
		dev_warn(&adap->dev, "SMBus collision!\n");
	}

	if (temp & GS_TO_STS) {
		result = -1;
		dev_dbg(&adap->dev, "SMBus protocol timeout!\n");
	}

	if (temp & GS_HCYC_STS)
		dev_dbg(&adap->dev, "SMBus protocol success!\n");

	outw_p(GS_CLEAR_STS, SMB_GLOBAL_STATUS);

#ifdef DEBUG
	if (((temp = inw_p(SMB_GLOBAL_STATUS)) & GS_CLEAR_STS) != 0x00) {
		dev_dbg(&adap->dev,
			"Failed reset at end of transaction (%04x)\n", temp);
	}
#endif

	dev_dbg(&adap->dev,
		"Transaction (post): GS=%04x, GE=%04x, ADD=%04x, DAT=%04x\n",
		inw_p(SMB_GLOBAL_STATUS), inw_p(SMB_GLOBAL_ENABLE),
		inw_p(SMB_HOST_ADDRESS), inb_p(SMB_HOST_DATA));

	return result;

 abort:
	dev_warn(&adap->dev, "Sending abort\n");
	outw_p(inw(SMB_GLOBAL_ENABLE) | GE_ABORT, SMB_GLOBAL_ENABLE);
	msleep(100);
	outw_p(GS_CLEAR_STS, SMB_GLOBAL_STATUS);
	return -1;
}

/* Return -1 on error. */
static s32 amd756_access(struct i2c_adapter * adap, u16 addr,
		  unsigned short flags, char read_write,
		  u8 command, int size, union i2c_smbus_data * data)
{
	int i, len;

	/** TODO: Should I supporte the 10-bit transfers? */
	switch (size) {
	case I2C_SMBUS_PROC_CALL:
		dev_dbg(&adap->dev, "I2C_SMBUS_PROC_CALL not supported!\n");
		/* TODO: Well... It is supported, I'm just not sure what to do here... */
		return -1;
	case I2C_SMBUS_QUICK:
		outw_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMB_HOST_ADDRESS);
		size = AMD756_QUICK;
		break;
	case I2C_SMBUS_BYTE:
		outw_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMB_HOST_ADDRESS);
		if (read_write == I2C_SMBUS_WRITE)
			outb_p(command, SMB_HOST_DATA);
		size = AMD756_BYTE;
		break;
	case I2C_SMBUS_BYTE_DATA:
		outw_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMB_HOST_ADDRESS);
		outb_p(command, SMB_HOST_COMMAND);
		if (read_write == I2C_SMBUS_WRITE)
			outw_p(data->byte, SMB_HOST_DATA);
		size = AMD756_BYTE_DATA;
		break;
	case I2C_SMBUS_WORD_DATA:
		outw_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMB_HOST_ADDRESS);
		outb_p(command, SMB_HOST_COMMAND);
		if (read_write == I2C_SMBUS_WRITE)
			outw_p(data->word, SMB_HOST_DATA);	/* TODO: endian???? */
		size = AMD756_WORD_DATA;
		break;
	case I2C_SMBUS_BLOCK_DATA:
		outw_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMB_HOST_ADDRESS);
		outb_p(command, SMB_HOST_COMMAND);
		if (read_write == I2C_SMBUS_WRITE) {
			len = data->block[0];
			if (len < 0)
				len = 0;
			if (len > 32)
				len = 32;
			outw_p(len, SMB_HOST_DATA);
			/* i = inw_p(SMBHSTCNT); Reset SMBBLKDAT */
			for (i = 1; i <= len; i++)
				outb_p(data->block[i],
				       SMB_HOST_BLOCK_DATA);
		}
		size = AMD756_BLOCK_DATA;
		break;
	}

	/* How about enabling interrupts... */
	outw_p(size & GE_CYC_TYPE_MASK, SMB_GLOBAL_ENABLE);

	if (amd756_transaction(adap))	/* Error in transaction */
		return -1;

	if ((read_write == I2C_SMBUS_WRITE) || (size == AMD756_QUICK))
		return 0;


	switch (size) {
	case AMD756_BYTE:
		data->byte = inw_p(SMB_HOST_DATA);
		break;
	case AMD756_BYTE_DATA:
		data->byte = inw_p(SMB_HOST_DATA);
		break;
	case AMD756_WORD_DATA:
		data->word = inw_p(SMB_HOST_DATA);	/* TODO: endian???? */
		break;
	case AMD756_BLOCK_DATA:
		data->block[0] = inw_p(SMB_HOST_DATA) & 0x3f;
		if(data->block[0] > 32)
			data->block[0] = 32;
		/* i = inw_p(SMBHSTCNT); Reset SMBBLKDAT */
		for (i = 1; i <= data->block[0]; i++)
			data->block[i] = inb_p(SMB_HOST_BLOCK_DATA);
		break;
	}

	return 0;
}

static u32 amd756_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
	    I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
	    I2C_FUNC_SMBUS_BLOCK_DATA | I2C_FUNC_SMBUS_PROC_CALL;
}

static const struct i2c_algorithm smbus_algorithm = {
	.smbus_xfer	= amd756_access,
	.functionality	= amd756_func,
};

struct i2c_adapter amd756_smbus = {
	.owner		= THIS_MODULE,
	.id		= I2C_HW_SMBUS_AMD756,
	.class          = I2C_CLASS_HWMON,
	.algo		= &smbus_algorithm,
};

enum chiptype { AMD756, AMD766, AMD768, NFORCE, AMD8111 };
static const char* chipname[] = {
	"AMD756", "AMD766", "AMD768",
	"nVidia nForce", "AMD8111",
};

static struct pci_device_id amd756_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_VIPER_740B),
	  .driver_data = AMD756 },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_VIPER_7413),
	  .driver_data = AMD766 },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_OPUS_7443),
	  .driver_data = AMD768 },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_8111_SMBUS),
	  .driver_data = AMD8111 },
	{ PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_SMBUS),
	  .driver_data = NFORCE },
	{ 0, }
};

MODULE_DEVICE_TABLE (pci, amd756_ids);

static int __devinit amd756_probe(struct pci_dev *pdev,
				  const struct pci_device_id *id)
{
	int nforce = (id->driver_data == NFORCE);
	int error;
	u8 temp;
	
	/* driver_data might come from user-space, so check it */
	if (id->driver_data >= ARRAY_SIZE(chipname))
		return -EINVAL;

	if (amd756_ioport) {
		dev_err(&pdev->dev, "Only one device supported "
		       "(you have a strange motherboard, btw)\n");
		return -ENODEV;
	}

	if (nforce) {
		if (PCI_FUNC(pdev->devfn) != 1)
			return -ENODEV;

		pci_read_config_word(pdev, SMBBANFORCE, &amd756_ioport);
		amd756_ioport &= 0xfffc;
	} else { /* amd */
		if (PCI_FUNC(pdev->devfn) != 3)
			return -ENODEV;

		pci_read_config_byte(pdev, SMBGCFG, &temp);
		if ((temp & 128) == 0) {
			dev_err(&pdev->dev,
				"Error: SMBus controller I/O not enabled!\n");
			return -ENODEV;
		}

		/* Determine the address of the SMBus areas */
		/* Technically it is a dword but... */
		pci_read_config_word(pdev, SMBBA, &amd756_ioport);
		amd756_ioport &= 0xff00;
		amd756_ioport += SMB_ADDR_OFFSET;
	}

	if (!request_region(amd756_ioport, SMB_IOSIZE, amd756_driver.name)) {
		dev_err(&pdev->dev, "SMB region 0x%x already in use!\n",
			amd756_ioport);
		return -ENODEV;
	}

	pci_read_config_byte(pdev, SMBREV, &temp);
	dev_dbg(&pdev->dev, "SMBREV = 0x%X\n", temp);
	dev_dbg(&pdev->dev, "AMD756_smba = 0x%X\n", amd756_ioport);

	/* set up the sysfs linkage to our parent device */
	amd756_smbus.dev.parent = &pdev->dev;

	sprintf(amd756_smbus.name, "SMBus %s adapter at %04x",
		chipname[id->driver_data], amd756_ioport);

	error = i2c_add_adapter(&amd756_smbus);
	if (error) {
		dev_err(&pdev->dev,
			"Adapter registration failed, module not inserted\n");
		goto out_err;
	}

	return 0;

 out_err:
	release_region(amd756_ioport, SMB_IOSIZE);
	return error;
}

static void __devexit amd756_remove(struct pci_dev *dev)
{
	i2c_del_adapter(&amd756_smbus);
	release_region(amd756_ioport, SMB_IOSIZE);
}

static struct pci_driver amd756_driver = {
	.name		= "amd756_smbus",
	.id_table	= amd756_ids,
	.probe		= amd756_probe,
	.remove		= __devexit_p(amd756_remove),
	.dynids.use_driver_data = 1,
};

static int __init amd756_init(void)
{
	return pci_register_driver(&amd756_driver);
}

static void __exit amd756_exit(void)
{
	pci_unregister_driver(&amd756_driver);
}

MODULE_AUTHOR("Merlin Hughes <merlin@merlin.org>");
MODULE_DESCRIPTION("AMD756/766/768/8111 and nVidia nForce SMBus driver");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(amd756_smbus);

module_init(amd756_init)
module_exit(amd756_exit)
