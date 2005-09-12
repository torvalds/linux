/*
    SMBus driver for nVidia nForce2 MCP

    Added nForce3 Pro 150  Thomas Leibold <thomas@plx.com>,
	Ported to 2.5 Patrick Dreker <patrick@dreker.de>,
    Copyright (c) 2003  Hans-Frieder Vogt <hfvogt@arcor.de>,
    Based on
    SMBus 2.0 driver for AMD-8111 IO-Hub
    Copyright (c) 2002 Vojtech Pavlik

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
    SUPPORTED DEVICES		PCI ID
    nForce2 MCP			0064
    nForce2 Ultra 400 MCP	0084
    nForce3 Pro150 MCP		00D4
    nForce3 250Gb MCP		00E4
    nForce4 MCP			0052

    This driver supports the 2 SMBuses that are included in the MCP of the
    nForce2/3/4 chipsets.
*/

/* Note: we assume there can only be one nForce2, with two SMBus interfaces */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <asm/io.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR ("Hans-Frieder Vogt <hfvogt@arcor.de>");
MODULE_DESCRIPTION("nForce2 SMBus driver");


struct nforce2_smbus {
	struct pci_dev *dev;
	struct i2c_adapter adapter;
	int base;
	int size;
};


/*
 * nVidia nForce2 SMBus control register definitions
 */
#define NFORCE_PCI_SMB1	0x50
#define NFORCE_PCI_SMB2	0x54


/*
 * ACPI 2.0 chapter 13 SMBus 2.0 EC register model
 */
#define NVIDIA_SMB_PRTCL	(smbus->base + 0x00)	/* protocol, PEC */
#define NVIDIA_SMB_STS		(smbus->base + 0x01)	/* status */
#define NVIDIA_SMB_ADDR		(smbus->base + 0x02)	/* address */
#define NVIDIA_SMB_CMD		(smbus->base + 0x03)	/* command */
#define NVIDIA_SMB_DATA		(smbus->base + 0x04)	/* 32 data registers */
#define NVIDIA_SMB_BCNT		(smbus->base + 0x24)	/* number of data bytes */
#define NVIDIA_SMB_ALRM_A	(smbus->base + 0x25)	/* alarm address */
#define NVIDIA_SMB_ALRM_D	(smbus->base + 0x26)	/* 2 bytes alarm data */

#define NVIDIA_SMB_STS_DONE	0x80
#define NVIDIA_SMB_STS_ALRM	0x40
#define NVIDIA_SMB_STS_RES	0x20
#define NVIDIA_SMB_STS_STATUS	0x1f

#define NVIDIA_SMB_PRTCL_WRITE			0x00
#define NVIDIA_SMB_PRTCL_READ			0x01
#define NVIDIA_SMB_PRTCL_QUICK			0x02
#define NVIDIA_SMB_PRTCL_BYTE			0x04
#define NVIDIA_SMB_PRTCL_BYTE_DATA		0x06
#define NVIDIA_SMB_PRTCL_WORD_DATA		0x08
#define NVIDIA_SMB_PRTCL_BLOCK_DATA		0x0a
#define NVIDIA_SMB_PRTCL_PROC_CALL		0x0c
#define NVIDIA_SMB_PRTCL_BLOCK_PROC_CALL	0x0d
#define NVIDIA_SMB_PRTCL_I2C_BLOCK_DATA		0x4a
#define NVIDIA_SMB_PRTCL_PEC			0x80


static s32 nforce2_access(struct i2c_adapter *adap, u16 addr,
		       unsigned short flags, char read_write,
		       u8 command, int size, union i2c_smbus_data *data);
static u32 nforce2_func(struct i2c_adapter *adapter);


static struct i2c_algorithm smbus_algorithm = {
	.smbus_xfer = nforce2_access,
	.functionality = nforce2_func,
};

static struct i2c_adapter nforce2_adapter = {
	.owner          = THIS_MODULE,
	.class          = I2C_CLASS_HWMON,
	.algo           = &smbus_algorithm,
	.name   	= "unset",
};

/* Return -1 on error. See smbus.h for more information */
static s32 nforce2_access(struct i2c_adapter * adap, u16 addr,
		unsigned short flags, char read_write,
		u8 command, int size, union i2c_smbus_data * data)
{
	struct nforce2_smbus *smbus = adap->algo_data;
	unsigned char protocol, pec, temp;
	unsigned char len = 0; /* to keep the compiler quiet */
	int i;

	protocol = (read_write == I2C_SMBUS_READ) ? NVIDIA_SMB_PRTCL_READ :
		NVIDIA_SMB_PRTCL_WRITE;
	pec = (flags & I2C_CLIENT_PEC) ? NVIDIA_SMB_PRTCL_PEC : 0;

	switch (size) {

		case I2C_SMBUS_QUICK:
			protocol |= NVIDIA_SMB_PRTCL_QUICK;
			read_write = I2C_SMBUS_WRITE;
			break;

		case I2C_SMBUS_BYTE:
			if (read_write == I2C_SMBUS_WRITE)
				outb_p(command, NVIDIA_SMB_CMD);
			protocol |= NVIDIA_SMB_PRTCL_BYTE;
			break;

		case I2C_SMBUS_BYTE_DATA:
			outb_p(command, NVIDIA_SMB_CMD);
			if (read_write == I2C_SMBUS_WRITE)
				outb_p(data->byte, NVIDIA_SMB_DATA);
			protocol |= NVIDIA_SMB_PRTCL_BYTE_DATA;
			break;

		case I2C_SMBUS_WORD_DATA:
			outb_p(command, NVIDIA_SMB_CMD);
			if (read_write == I2C_SMBUS_WRITE) {
				 outb_p(data->word, NVIDIA_SMB_DATA);
				 outb_p(data->word >> 8, NVIDIA_SMB_DATA+1);
			}
			protocol |= NVIDIA_SMB_PRTCL_WORD_DATA | pec;
			break;

		case I2C_SMBUS_BLOCK_DATA:
			outb_p(command, NVIDIA_SMB_CMD);
			if (read_write == I2C_SMBUS_WRITE) {
				len = min_t(u8, data->block[0], 32);
				outb_p(len, NVIDIA_SMB_BCNT);
				for (i = 0; i < len; i++)
					outb_p(data->block[i + 1], NVIDIA_SMB_DATA+i);
			}
			protocol |= NVIDIA_SMB_PRTCL_BLOCK_DATA | pec;
			break;

		case I2C_SMBUS_I2C_BLOCK_DATA:
			len = min_t(u8, data->block[0], 32);
			outb_p(command, NVIDIA_SMB_CMD);
			outb_p(len, NVIDIA_SMB_BCNT);
			if (read_write == I2C_SMBUS_WRITE)
				for (i = 0; i < len; i++)
					outb_p(data->block[i + 1], NVIDIA_SMB_DATA+i);
			protocol |= NVIDIA_SMB_PRTCL_I2C_BLOCK_DATA;
			break;

		case I2C_SMBUS_PROC_CALL:
			dev_err(&adap->dev, "I2C_SMBUS_PROC_CALL not supported!\n");
			return -1;

		case I2C_SMBUS_BLOCK_PROC_CALL:
			dev_err(&adap->dev, "I2C_SMBUS_BLOCK_PROC_CALL not supported!\n");
			return -1;

		case I2C_SMBUS_WORD_DATA_PEC:
		case I2C_SMBUS_BLOCK_DATA_PEC:
		case I2C_SMBUS_PROC_CALL_PEC:
		case I2C_SMBUS_BLOCK_PROC_CALL_PEC:
			dev_err(&adap->dev, "Unexpected software PEC transaction %d\n.", size);
			return -1;

		default:
			dev_err(&adap->dev, "Unsupported transaction %d\n", size);
			return -1;
	}

	outb_p((addr & 0x7f) << 1, NVIDIA_SMB_ADDR);
	outb_p(protocol, NVIDIA_SMB_PRTCL);

	temp = inb_p(NVIDIA_SMB_STS);

	if (~temp & NVIDIA_SMB_STS_DONE) {
		udelay(500);
		temp = inb_p(NVIDIA_SMB_STS);
	}
	if (~temp & NVIDIA_SMB_STS_DONE) {
		msleep(10);
		temp = inb_p(NVIDIA_SMB_STS);
	}

	if ((~temp & NVIDIA_SMB_STS_DONE) || (temp & NVIDIA_SMB_STS_STATUS)) {
		dev_dbg(&adap->dev, "SMBus Timeout! (0x%02x)\n", temp);
		return -1;
	}

	if (read_write == I2C_SMBUS_WRITE)
		return 0;

	switch (size) {

		case I2C_SMBUS_BYTE:
		case I2C_SMBUS_BYTE_DATA:
			data->byte = inb_p(NVIDIA_SMB_DATA);
			break;

		case I2C_SMBUS_WORD_DATA:
		/* case I2C_SMBUS_PROC_CALL: not supported */
			data->word = inb_p(NVIDIA_SMB_DATA) | (inb_p(NVIDIA_SMB_DATA+1) << 8);
			break;

		case I2C_SMBUS_BLOCK_DATA:
		/* case I2C_SMBUS_BLOCK_PROC_CALL: not supported */
			len = inb_p(NVIDIA_SMB_BCNT);
			len = min_t(u8, len, 32);
		case I2C_SMBUS_I2C_BLOCK_DATA:
			for (i = 0; i < len; i++)
				data->block[i+1] = inb_p(NVIDIA_SMB_DATA + i);
			data->block[0] = len;
			break;
	}

	return 0;
}


static u32 nforce2_func(struct i2c_adapter *adapter)
{
	/* other functionality might be possible, but is not tested */
	return I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
	    I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA /* |
	    I2C_FUNC_SMBUS_BLOCK_DATA */;
}


static struct pci_device_id nforce2_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE2_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE2S_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE3_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE3S_SMBUS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE4_SMBUS) },
	{ 0 }
};


MODULE_DEVICE_TABLE (pci, nforce2_ids);


static int __devinit nforce2_probe_smb (struct pci_dev *dev, int reg,
	struct nforce2_smbus *smbus, char *name)
{
	u16 iobase;
	int error;

	if (pci_read_config_word(dev, reg, &iobase) != PCIBIOS_SUCCESSFUL) {
		dev_err(&smbus->adapter.dev, "Error reading PCI config for %s\n", name);
		return -1;
	}
	smbus->dev  = dev;
	smbus->base = iobase & 0xfffc;
	smbus->size = 8;

	if (!request_region(smbus->base, smbus->size, "nForce2 SMBus")) {
		dev_err(&smbus->adapter.dev, "Error requesting region %02x .. %02X for %s\n",
			smbus->base, smbus->base+smbus->size-1, name);
		return -1;
	}
	smbus->adapter = nforce2_adapter;
	smbus->adapter.algo_data = smbus;
	smbus->adapter.dev.parent = &dev->dev;
	snprintf(smbus->adapter.name, I2C_NAME_SIZE,
		"SMBus nForce2 adapter at %04x", smbus->base);

	error = i2c_add_adapter(&smbus->adapter);
	if (error) {
		dev_err(&smbus->adapter.dev, "Failed to register adapter.\n");
		release_region(smbus->base, smbus->size);
		return -1;
	}
	dev_info(&smbus->adapter.dev, "nForce2 SMBus adapter at %#x\n", smbus->base);
	return 0;
}


static int __devinit nforce2_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct nforce2_smbus *smbuses;
	int res1, res2;

	/* we support 2 SMBus adapters */
	if (!(smbuses = (void *)kmalloc(2*sizeof(struct nforce2_smbus),
				       	GFP_KERNEL)))
		return -ENOMEM;
	memset (smbuses, 0, 2*sizeof(struct nforce2_smbus));
	pci_set_drvdata(dev, smbuses);

	/* SMBus adapter 1 */
	res1 = nforce2_probe_smb (dev, NFORCE_PCI_SMB1, &smbuses[0], "SMB1");
	if (res1 < 0) {
		dev_err(&dev->dev, "Error probing SMB1.\n");
		smbuses[0].base = 0;	/* to have a check value */
	}
	res2 = nforce2_probe_smb (dev, NFORCE_PCI_SMB2, &smbuses[1], "SMB2");
	if (res2 < 0) {
		dev_err(&dev->dev, "Error probing SMB2.\n");
		smbuses[1].base = 0;	/* to have a check value */
	}
	if ((res1 < 0) && (res2 < 0)) {
		/* we did not find even one of the SMBuses, so we give up */
		kfree(smbuses);
		return -ENODEV;
	}

	return 0;
}


static void __devexit nforce2_remove(struct pci_dev *dev)
{
	struct nforce2_smbus *smbuses = (void*) pci_get_drvdata(dev);

	if (smbuses[0].base) {
		i2c_del_adapter(&smbuses[0].adapter);
		release_region(smbuses[0].base, smbuses[0].size);
	}
	if (smbuses[1].base) {
		i2c_del_adapter(&smbuses[1].adapter);
		release_region(smbuses[1].base, smbuses[1].size);
	}
	kfree(smbuses);
}

static struct pci_driver nforce2_driver = {
	.name		= "nForce2_smbus",
	.id_table	= nforce2_ids,
	.probe		= nforce2_probe,
	.remove		= __devexit_p(nforce2_remove),
};

static int __init nforce2_init(void)
{
	return pci_register_driver(&nforce2_driver);
}

static void __exit nforce2_exit(void)
{
	pci_unregister_driver(&nforce2_driver);
}

module_init(nforce2_init);
module_exit(nforce2_exit);

