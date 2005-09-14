/**
 *	i2c-ali1563.c - i2c driver for the ALi 1563 Southbridge
 *
 *	Copyright (C) 2004 Patrick Mochel
 *		      2005 Rudolf Marek <r.marek@sh.cvut.cz>
 *
 *	The 1563 southbridge is deceptively similar to the 1533, with a
 *	few notable exceptions. One of those happens to be the fact they
 *	upgraded the i2c core to be 2.0 compliant, and happens to be almost
 *	identical to the i2c controller found in the Intel 801 south
 *	bridges.
 *
 *	This driver is based on a mix of the 15x3, 1535, and i801 drivers,
 *	with a little help from the ALi 1563 spec.
 *
 *	This file is released under the GPLv2
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/pci.h>
#include <linux/init.h>

#define ALI1563_MAX_TIMEOUT	500
#define	ALI1563_SMBBA		0x80
#define ALI1563_SMB_IOEN	1
#define ALI1563_SMB_HOSTEN	2
#define ALI1563_SMB_IOSIZE	16

#define SMB_HST_STS	(ali1563_smba + 0)
#define SMB_HST_CNTL1	(ali1563_smba + 1)
#define SMB_HST_CNTL2	(ali1563_smba + 2)
#define SMB_HST_CMD	(ali1563_smba + 3)
#define SMB_HST_ADD	(ali1563_smba + 4)
#define SMB_HST_DAT0	(ali1563_smba + 5)
#define SMB_HST_DAT1	(ali1563_smba + 6)
#define SMB_BLK_DAT	(ali1563_smba + 7)

#define HST_STS_BUSY	0x01
#define HST_STS_INTR	0x02
#define HST_STS_DEVERR	0x04
#define HST_STS_BUSERR	0x08
#define HST_STS_FAIL	0x10
#define HST_STS_DONE	0x80
#define HST_STS_BAD	0x1c


#define HST_CNTL1_TIMEOUT	0x80
#define HST_CNTL1_LAST		0x40

#define HST_CNTL2_KILL		0x04
#define HST_CNTL2_START		0x40
#define HST_CNTL2_QUICK		0x00
#define HST_CNTL2_BYTE		0x01
#define HST_CNTL2_BYTE_DATA	0x02
#define HST_CNTL2_WORD_DATA	0x03
#define HST_CNTL2_BLOCK		0x05


#define HST_CNTL2_SIZEMASK	0x38

static unsigned short ali1563_smba;

static int ali1563_transaction(struct i2c_adapter * a, int size)
{
	u32 data;
	int timeout;

	dev_dbg(&a->dev, "Transaction (pre): STS=%02x, CNTL1=%02x, "
		"CNTL2=%02x, CMD=%02x, ADD=%02x, DAT0=%02x, DAT1=%02x\n",
		inb_p(SMB_HST_STS), inb_p(SMB_HST_CNTL1), inb_p(SMB_HST_CNTL2),
		inb_p(SMB_HST_CMD), inb_p(SMB_HST_ADD), inb_p(SMB_HST_DAT0),
		inb_p(SMB_HST_DAT1));

	data = inb_p(SMB_HST_STS);
	if (data & HST_STS_BAD) {
		dev_err(&a->dev, "ali1563: Trying to reset busy device\n");
		outb_p(data | HST_STS_BAD,SMB_HST_STS);
		data = inb_p(SMB_HST_STS);
		if (data & HST_STS_BAD)
			return -EBUSY;
	}
	outb_p(inb_p(SMB_HST_CNTL2) | HST_CNTL2_START, SMB_HST_CNTL2);

	timeout = ALI1563_MAX_TIMEOUT;
	do
		msleep(1);
	while (((data = inb_p(SMB_HST_STS)) & HST_STS_BUSY) && --timeout);

	dev_dbg(&a->dev, "Transaction (post): STS=%02x, CNTL1=%02x, "
		"CNTL2=%02x, CMD=%02x, ADD=%02x, DAT0=%02x, DAT1=%02x\n",
		inb_p(SMB_HST_STS), inb_p(SMB_HST_CNTL1), inb_p(SMB_HST_CNTL2),
		inb_p(SMB_HST_CMD), inb_p(SMB_HST_ADD), inb_p(SMB_HST_DAT0),
		inb_p(SMB_HST_DAT1));

	if (timeout && !(data & HST_STS_BAD))
		return 0;

	if (!timeout) {
		dev_err(&a->dev, "Timeout - Trying to KILL transaction!\n");
		/* Issue 'kill' to host controller */
		outb_p(HST_CNTL2_KILL,SMB_HST_CNTL2);
		data = inb_p(SMB_HST_STS);
 	}

	/* device error - no response, ignore the autodetection case */
	if ((data & HST_STS_DEVERR) && (size != HST_CNTL2_QUICK)) {
		dev_err(&a->dev, "Device error!\n");
	}

	/* bus collision */
	if (data & HST_STS_BUSERR) {
		dev_err(&a->dev, "Bus collision!\n");
		/* Issue timeout, hoping it helps */
		outb_p(HST_CNTL1_TIMEOUT,SMB_HST_CNTL1);
	}

	if (data & HST_STS_FAIL) {
		dev_err(&a->dev, "Cleaning fail after KILL!\n");
		outb_p(0x0,SMB_HST_CNTL2);
	}

	return -1;
}

static int ali1563_block_start(struct i2c_adapter * a)
{
	u32 data;
	int timeout;

	dev_dbg(&a->dev, "Block (pre): STS=%02x, CNTL1=%02x, "
		"CNTL2=%02x, CMD=%02x, ADD=%02x, DAT0=%02x, DAT1=%02x\n",
		inb_p(SMB_HST_STS), inb_p(SMB_HST_CNTL1), inb_p(SMB_HST_CNTL2),
		inb_p(SMB_HST_CMD), inb_p(SMB_HST_ADD), inb_p(SMB_HST_DAT0),
		inb_p(SMB_HST_DAT1));

	data = inb_p(SMB_HST_STS);
	if (data & HST_STS_BAD) {
		dev_warn(&a->dev,"ali1563: Trying to reset busy device\n");
		outb_p(data | HST_STS_BAD,SMB_HST_STS);
		data = inb_p(SMB_HST_STS);
		if (data & HST_STS_BAD)
			return -EBUSY;
	}

	/* Clear byte-ready bit */
	outb_p(data | HST_STS_DONE, SMB_HST_STS);

	/* Start transaction and wait for byte-ready bit to be set */
	outb_p(inb_p(SMB_HST_CNTL2) | HST_CNTL2_START, SMB_HST_CNTL2);

	timeout = ALI1563_MAX_TIMEOUT;
	do
		msleep(1);
	while (!((data = inb_p(SMB_HST_STS)) & HST_STS_DONE) && --timeout);

	dev_dbg(&a->dev, "Block (post): STS=%02x, CNTL1=%02x, "
		"CNTL2=%02x, CMD=%02x, ADD=%02x, DAT0=%02x, DAT1=%02x\n",
		inb_p(SMB_HST_STS), inb_p(SMB_HST_CNTL1), inb_p(SMB_HST_CNTL2),
		inb_p(SMB_HST_CMD), inb_p(SMB_HST_ADD), inb_p(SMB_HST_DAT0),
		inb_p(SMB_HST_DAT1));

	if (timeout && !(data & HST_STS_BAD))
		return 0;
	dev_err(&a->dev, "SMBus Error: %s%s%s%s%s\n",
		timeout ? "Timeout " : "",
		data & HST_STS_FAIL ? "Transaction Failed " : "",
		data & HST_STS_BUSERR ? "No response or Bus Collision " : "",
		data & HST_STS_DEVERR ? "Device Error " : "",
		!(data & HST_STS_DONE) ? "Transaction Never Finished " : "");
	return -1;
}

static int ali1563_block(struct i2c_adapter * a, union i2c_smbus_data * data, u8 rw)
{
	int i, len;
	int error = 0;

	/* Do we need this? */
	outb_p(HST_CNTL1_LAST,SMB_HST_CNTL1);

	if (rw == I2C_SMBUS_WRITE) {
		len = data->block[0];
		if (len < 1)
			len = 1;
		else if (len > 32)
			len = 32;
		outb_p(len,SMB_HST_DAT0);
		outb_p(data->block[1],SMB_BLK_DAT);
	} else
		len = 32;

	outb_p(inb_p(SMB_HST_CNTL2) | HST_CNTL2_BLOCK, SMB_HST_CNTL2);

	for (i = 0; i < len; i++) {
		if (rw == I2C_SMBUS_WRITE) {
			outb_p(data->block[i + 1], SMB_BLK_DAT);
			if ((error = ali1563_block_start(a)))
				break;
		} else {
			if ((error = ali1563_block_start(a)))
				break;
			if (i == 0) {
				len = inb_p(SMB_HST_DAT0);
				if (len < 1)
					len = 1;
				else if (len > 32)
					len = 32;
			}
			data->block[i+1] = inb_p(SMB_BLK_DAT);
		}
	}
	/* Do we need this? */
	outb_p(HST_CNTL1_LAST,SMB_HST_CNTL1);
	return error;
}

static s32 ali1563_access(struct i2c_adapter * a, u16 addr,
			  unsigned short flags, char rw, u8 cmd,
			  int size, union i2c_smbus_data * data)
{
	int error = 0;
	int timeout;
	u32 reg;

	for (timeout = ALI1563_MAX_TIMEOUT; timeout; timeout--) {
		if (!(reg = inb_p(SMB_HST_STS) & HST_STS_BUSY))
			break;
	}
	if (!timeout)
		dev_warn(&a->dev,"SMBus not idle. HST_STS = %02x\n",reg);
	outb_p(0xff,SMB_HST_STS);

	/* Map the size to what the chip understands */
	switch (size) {
	case I2C_SMBUS_PROC_CALL:
		dev_err(&a->dev, "I2C_SMBUS_PROC_CALL not supported!\n");
		error = -EINVAL;
		break;
	case I2C_SMBUS_QUICK:
		size = HST_CNTL2_QUICK;
		break;
	case I2C_SMBUS_BYTE:
		size = HST_CNTL2_BYTE;
		break;
	case I2C_SMBUS_BYTE_DATA:
		size = HST_CNTL2_BYTE_DATA;
		break;
	case I2C_SMBUS_WORD_DATA:
		size = HST_CNTL2_WORD_DATA;
		break;
	case I2C_SMBUS_BLOCK_DATA:
		size = HST_CNTL2_BLOCK;
		break;
	}

	outb_p(((addr & 0x7f) << 1) | (rw & 0x01), SMB_HST_ADD);
	outb_p((inb_p(SMB_HST_CNTL2) & ~HST_CNTL2_SIZEMASK) | (size << 3), SMB_HST_CNTL2);

	/* Write the command register */

	switch(size) {
	case HST_CNTL2_BYTE:
		if (rw== I2C_SMBUS_WRITE)
			/* Beware it uses DAT0 register and not CMD! */
			outb_p(cmd, SMB_HST_DAT0);
		break;
	case HST_CNTL2_BYTE_DATA:
		outb_p(cmd, SMB_HST_CMD);
		if (rw == I2C_SMBUS_WRITE)
			outb_p(data->byte, SMB_HST_DAT0);
		break;
	case HST_CNTL2_WORD_DATA:
		outb_p(cmd, SMB_HST_CMD);
		if (rw == I2C_SMBUS_WRITE) {
			outb_p(data->word & 0xff, SMB_HST_DAT0);
			outb_p((data->word & 0xff00) >> 8, SMB_HST_DAT1);
		}
		break;
	case HST_CNTL2_BLOCK:
		outb_p(cmd, SMB_HST_CMD);
		error = ali1563_block(a,data,rw);
		goto Done;
	}

	if ((error = ali1563_transaction(a, size)))
		goto Done;

	if ((rw == I2C_SMBUS_WRITE) || (size == HST_CNTL2_QUICK))
		goto Done;

	switch (size) {
	case HST_CNTL2_BYTE:	/* Result put in SMBHSTDAT0 */
		data->byte = inb_p(SMB_HST_DAT0);
		break;
	case HST_CNTL2_BYTE_DATA:
		data->byte = inb_p(SMB_HST_DAT0);
		break;
	case HST_CNTL2_WORD_DATA:
		data->word = inb_p(SMB_HST_DAT0) + (inb_p(SMB_HST_DAT1) << 8);
		break;
	}
Done:
	return error;
}

static u32 ali1563_func(struct i2c_adapter * a)
{
	return I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
	    I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
	    I2C_FUNC_SMBUS_BLOCK_DATA;
}


static void ali1563_enable(struct pci_dev * dev)
{
	u16 ctrl;

	pci_read_config_word(dev,ALI1563_SMBBA,&ctrl);
	ctrl |= 0x7;
	pci_write_config_word(dev,ALI1563_SMBBA,ctrl);
}

static int __devinit ali1563_setup(struct pci_dev * dev)
{
	u16 ctrl;

	pci_read_config_word(dev,ALI1563_SMBBA,&ctrl);
	printk("ali1563: SMBus control = %04x\n",ctrl);

	/* Check if device is even enabled first */
	if (!(ctrl & ALI1563_SMB_IOEN)) {
		dev_warn(&dev->dev,"I/O space not enabled, trying manually\n");
		ali1563_enable(dev);
	}
	if (!(ctrl & ALI1563_SMB_IOEN)) {
		dev_warn(&dev->dev,"I/O space still not enabled, giving up\n");
		goto Err;
	}
	if (!(ctrl & ALI1563_SMB_HOSTEN)) {
		dev_warn(&dev->dev,"Host Controller not enabled\n");
		goto Err;
	}

	/* SMB I/O Base in high 12 bits and must be aligned with the
	 * size of the I/O space. */
	ali1563_smba = ctrl & ~(ALI1563_SMB_IOSIZE - 1);
	if (!ali1563_smba) {
		dev_warn(&dev->dev,"ali1563_smba Uninitialized\n");
		goto Err;
	}
	if (!request_region(ali1563_smba,ALI1563_SMB_IOSIZE,"i2c-ali1563")) {
		dev_warn(&dev->dev,"Could not allocate I/O space");
		goto Err;
	}

	return 0;
Err:
	return -ENODEV;
}

static void ali1563_shutdown(struct pci_dev *dev)
{
	release_region(ali1563_smba,ALI1563_SMB_IOSIZE);
}

static struct i2c_algorithm ali1563_algorithm = {
	.smbus_xfer	= ali1563_access,
	.functionality	= ali1563_func,
};

static struct i2c_adapter ali1563_adapter = {
	.owner	= THIS_MODULE,
	.class	= I2C_CLASS_HWMON,
	.algo	= &ali1563_algorithm,
};

static int __devinit ali1563_probe(struct pci_dev * dev,
				const struct pci_device_id * id_table)
{
	int error;

	if ((error = ali1563_setup(dev)))
		return error;
	ali1563_adapter.dev.parent = &dev->dev;
	sprintf(ali1563_adapter.name,"SMBus ALi 1563 Adapter @ %04x",
		ali1563_smba);
	if ((error = i2c_add_adapter(&ali1563_adapter)))
		ali1563_shutdown(dev);
	printk("%s: Returning %d\n",__FUNCTION__,error);
	return error;
}

static void __devexit ali1563_remove(struct pci_dev * dev)
{
	i2c_del_adapter(&ali1563_adapter);
	ali1563_shutdown(dev);
}

static struct pci_device_id __devinitdata ali1563_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M1563) },
	{},
};

MODULE_DEVICE_TABLE (pci, ali1563_id_table);

static struct pci_driver ali1563_pci_driver = {
 	.name		= "ali1563_i2c",
	.id_table	= ali1563_id_table,
 	.probe		= ali1563_probe,
	.remove		= __devexit_p(ali1563_remove),
};

static int __init ali1563_init(void)
{
	return pci_register_driver(&ali1563_pci_driver);
}

module_init(ali1563_init);

static void __exit ali1563_exit(void)
{
	pci_unregister_driver(&ali1563_pci_driver);
}

module_exit(ali1563_exit);

MODULE_LICENSE("GPL");
