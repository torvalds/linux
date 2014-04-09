/*
    Copyright (c) 2002,2003 Alexander Malysh <amalysh@web.de>

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
   Status: beta

   Supports:
	SIS 630
	SIS 730
	SIS 964

   Notable differences between chips:
	+------------------------+--------------------+-------------------+
	|                        |     SIS630/730     |      SIS964       |
	+------------------------+--------------------+-------------------+
	| Clock                  | 14kHz/56kHz        | 55.56kHz/27.78kHz |
	| SMBus registers offset | 0x80               | 0xE0              |
	| SMB_CNT                | Bit 1 = Slave Busy | Bit 1 = Bus probe |
	|         (not used yet) | Bit 3 is reserved  | Bit 3 = Last byte |
	| SMB_PCOUNT		 | Offset + 0x06      | Offset + 0x14     |
	| SMB_COUNT              | 4:0 bits           | 5:0 bits          |
	+------------------------+--------------------+-------------------+
	(Other differences don't affect the functions provided by the driver)

   Note: we assume there can only be one device, with one SMBus interface.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/i2c.h>
#include <linux/acpi.h>
#include <linux/io.h>

/* SIS964 id is defined here as we are the only file using it */
#define PCI_DEVICE_ID_SI_964	0x0964

/* SIS630/730/964 SMBus registers */
#define SMB_STS			0x00	/* status */
#define SMB_CNT			0x02	/* control */
#define SMBHOST_CNT		0x03	/* host control */
#define SMB_ADDR		0x04	/* address */
#define SMB_CMD			0x05	/* command */
#define SMB_COUNT		0x07	/* byte count */
#define SMB_BYTE		0x08	/* ~0x8F data byte field */

/* SMB_STS register */
#define BYTE_DONE_STS		0x10	/* Byte Done Status / Block Array */
#define SMBCOL_STS		0x04	/* Collision */
#define SMBERR_STS		0x02	/* Device error */

/* SMB_CNT register */
#define MSTO_EN			0x40	/* Host Master Timeout Enable */
#define SMBCLK_SEL		0x20	/* Host master clock selection */
#define SMB_PROBE		0x02	/* Bus Probe/Slave busy */
#define SMB_HOSTBUSY		0x01	/* Host Busy */

/* SMBHOST_CNT register */
#define SMB_KILL		0x20	/* Kill */
#define SMB_START		0x10	/* Start */

/* register count for request_region
 * As we don't use SMB_PCOUNT, 20 is ok for SiS630 and SiS964
 */
#define SIS630_SMB_IOREGION	20

/* PCI address constants */
/* acpi base address register  */
#define SIS630_ACPI_BASE_REG	0x74
/* bios control register */
#define SIS630_BIOS_CTL_REG	0x40

/* Other settings */
#define MAX_TIMEOUT		500

/* SIS630 constants */
#define SIS630_QUICK		0x00
#define SIS630_BYTE		0x01
#define SIS630_BYTE_DATA	0x02
#define SIS630_WORD_DATA	0x03
#define SIS630_PCALL		0x04
#define SIS630_BLOCK_DATA	0x05

static struct pci_driver sis630_driver;

/* insmod parameters */
static bool high_clock;
static bool force;
module_param(high_clock, bool, 0);
MODULE_PARM_DESC(high_clock,
	"Set Host Master Clock to 56KHz (default 14KHz) (SIS630/730 only).");
module_param(force, bool, 0);
MODULE_PARM_DESC(force, "Forcibly enable the SIS630. DANGEROUS!");

/* SMBus base adress */
static unsigned short smbus_base;

/* supported chips */
static int supported[] = {
	PCI_DEVICE_ID_SI_630,
	PCI_DEVICE_ID_SI_730,
	PCI_DEVICE_ID_SI_760,
	0 /* terminates the list */
};

static inline u8 sis630_read(u8 reg)
{
	return inb(smbus_base + reg);
}

static inline void sis630_write(u8 reg, u8 data)
{
	outb(data, smbus_base + reg);
}

static int sis630_transaction_start(struct i2c_adapter *adap, int size,
				    u8 *oldclock)
{
	int temp;

	/* Make sure the SMBus host is ready to start transmitting. */
	temp = sis630_read(SMB_CNT);
	if ((temp & (SMB_PROBE | SMB_HOSTBUSY)) != 0x00) {
		dev_dbg(&adap->dev, "SMBus busy (%02x). Resetting...\n", temp);
		/* kill smbus transaction */
		sis630_write(SMBHOST_CNT, SMB_KILL);

		temp = sis630_read(SMB_CNT);
		if (temp & (SMB_PROBE | SMB_HOSTBUSY)) {
			dev_dbg(&adap->dev, "Failed! (%02x)\n", temp);
			return -EBUSY;
		} else {
			dev_dbg(&adap->dev, "Successful!\n");
		}
	}

	/* save old clock, so we can prevent machine for hung */
	*oldclock = sis630_read(SMB_CNT);

	dev_dbg(&adap->dev, "saved clock 0x%02x\n", *oldclock);

	/* disable timeout interrupt,
	 * set Host Master Clock to 56KHz if requested */
	if (high_clock)
		sis630_write(SMB_CNT, SMBCLK_SEL);
	else
		sis630_write(SMB_CNT, (*oldclock & ~MSTO_EN));

	/* clear all sticky bits */
	temp = sis630_read(SMB_STS);
	sis630_write(SMB_STS, temp & 0x1e);

	/* start the transaction by setting bit 4 and size */
	sis630_write(SMBHOST_CNT, SMB_START | (size & 0x07));

	return 0;
}

static int sis630_transaction_wait(struct i2c_adapter *adap, int size)
{
	int temp, result = 0, timeout = 0;

	/* We will always wait for a fraction of a second! */
	do {
		msleep(1);
		temp = sis630_read(SMB_STS);
		/* check if block transmitted */
		if (size == SIS630_BLOCK_DATA && (temp & BYTE_DONE_STS))
			break;
	} while (!(temp & 0x0e) && (timeout++ < MAX_TIMEOUT));

	/* If the SMBus is still busy, we give up */
	if (timeout > MAX_TIMEOUT) {
		dev_dbg(&adap->dev, "SMBus Timeout!\n");
		result = -ETIMEDOUT;
	}

	if (temp & SMBERR_STS) {
		dev_dbg(&adap->dev, "Error: Failed bus transaction\n");
		result = -ENXIO;
	}

	if (temp & SMBCOL_STS) {
		dev_err(&adap->dev, "Bus collision!\n");
		result = -EAGAIN;
	}

	return result;
}

static void sis630_transaction_end(struct i2c_adapter *adap, u8 oldclock)
{
	/* clear all status "sticky" bits */
	sis630_write(SMB_STS, 0xFF);

	dev_dbg(&adap->dev,
		"SMB_CNT before clock restore 0x%02x\n", sis630_read(SMB_CNT));

	/*
	 * restore old Host Master Clock if high_clock is set
	 * and oldclock was not 56KHz
	 */
	if (high_clock && !(oldclock & SMBCLK_SEL))
		sis630_write(SMB_CNT, sis630_read(SMB_CNT) & ~SMBCLK_SEL);

	dev_dbg(&adap->dev,
		"SMB_CNT after clock restore 0x%02x\n", sis630_read(SMB_CNT));
}

static int sis630_transaction(struct i2c_adapter *adap, int size)
{
	int result = 0;
	u8 oldclock = 0;

	result = sis630_transaction_start(adap, size, &oldclock);
	if (!result) {
		result = sis630_transaction_wait(adap, size);
		sis630_transaction_end(adap, oldclock);
	}

	return result;
}

static int sis630_block_data(struct i2c_adapter *adap,
			     union i2c_smbus_data *data, int read_write)
{
	int i, len = 0, rc = 0;
	u8 oldclock = 0;

	if (read_write == I2C_SMBUS_WRITE) {
		len = data->block[0];
		if (len < 0)
			len = 0;
		else if (len > 32)
			len = 32;
		sis630_write(SMB_COUNT, len);
		for (i = 1; i <= len; i++) {
			dev_dbg(&adap->dev,
				"set data 0x%02x\n", data->block[i]);
			/* set data */
			sis630_write(SMB_BYTE + (i - 1) % 8, data->block[i]);
			if (i == 8 || (len < 8 && i == len)) {
				dev_dbg(&adap->dev,
					"start trans len=%d i=%d\n", len, i);
				/* first transaction */
				rc = sis630_transaction_start(adap,
						SIS630_BLOCK_DATA, &oldclock);
				if (rc)
					return rc;
			} else if ((i - 1) % 8 == 7 || i == len) {
				dev_dbg(&adap->dev,
					"trans_wait len=%d i=%d\n", len, i);
				if (i > 8) {
					dev_dbg(&adap->dev,
						"clear smbary_sts"
						" len=%d i=%d\n", len, i);
					/*
					   If this is not first transaction,
					   we must clear sticky bit.
					   clear SMBARY_STS
					*/
					sis630_write(SMB_STS, BYTE_DONE_STS);
				}
				rc = sis630_transaction_wait(adap,
						SIS630_BLOCK_DATA);
				if (rc) {
					dev_dbg(&adap->dev,
						"trans_wait failed\n");
					break;
				}
			}
		}
	} else {
		/* read request */
		data->block[0] = len = 0;
		rc = sis630_transaction_start(adap,
				SIS630_BLOCK_DATA, &oldclock);
		if (rc)
			return rc;
		do {
			rc = sis630_transaction_wait(adap, SIS630_BLOCK_DATA);
			if (rc) {
				dev_dbg(&adap->dev, "trans_wait failed\n");
				break;
			}
			/* if this first transaction then read byte count */
			if (len == 0)
				data->block[0] = sis630_read(SMB_COUNT);

			/* just to be sure */
			if (data->block[0] > 32)
				data->block[0] = 32;

			dev_dbg(&adap->dev,
				"block data read len=0x%x\n", data->block[0]);

			for (i = 0; i < 8 && len < data->block[0]; i++, len++) {
				dev_dbg(&adap->dev,
					"read i=%d len=%d\n", i, len);
				data->block[len + 1] = sis630_read(SMB_BYTE +
								   i);
			}

			dev_dbg(&adap->dev,
				"clear smbary_sts len=%d i=%d\n", len, i);

			/* clear SMBARY_STS */
			sis630_write(SMB_STS, BYTE_DONE_STS);
		} while (len < data->block[0]);
	}

	sis630_transaction_end(adap, oldclock);

	return rc;
}

/* Return negative errno on error. */
static s32 sis630_access(struct i2c_adapter *adap, u16 addr,
			 unsigned short flags, char read_write,
			 u8 command, int size, union i2c_smbus_data *data)
{
	int status;

	switch (size) {
	case I2C_SMBUS_QUICK:
		sis630_write(SMB_ADDR,
			     ((addr & 0x7f) << 1) | (read_write & 0x01));
		size = SIS630_QUICK;
		break;
	case I2C_SMBUS_BYTE:
		sis630_write(SMB_ADDR,
			     ((addr & 0x7f) << 1) | (read_write & 0x01));
		if (read_write == I2C_SMBUS_WRITE)
			sis630_write(SMB_CMD, command);
		size = SIS630_BYTE;
		break;
	case I2C_SMBUS_BYTE_DATA:
		sis630_write(SMB_ADDR,
			     ((addr & 0x7f) << 1) | (read_write & 0x01));
		sis630_write(SMB_CMD, command);
		if (read_write == I2C_SMBUS_WRITE)
			sis630_write(SMB_BYTE, data->byte);
		size = SIS630_BYTE_DATA;
		break;
	case I2C_SMBUS_PROC_CALL:
	case I2C_SMBUS_WORD_DATA:
		sis630_write(SMB_ADDR,
			     ((addr & 0x7f) << 1) | (read_write & 0x01));
		sis630_write(SMB_CMD, command);
		if (read_write == I2C_SMBUS_WRITE) {
			sis630_write(SMB_BYTE, data->word & 0xff);
			sis630_write(SMB_BYTE + 1, (data->word & 0xff00) >> 8);
		}
		size = (size == I2C_SMBUS_PROC_CALL ?
			SIS630_PCALL : SIS630_WORD_DATA);
		break;
	case I2C_SMBUS_BLOCK_DATA:
		sis630_write(SMB_ADDR,
			     ((addr & 0x7f) << 1) | (read_write & 0x01));
		sis630_write(SMB_CMD, command);
		size = SIS630_BLOCK_DATA;
		return sis630_block_data(adap, data, read_write);
	default:
		dev_warn(&adap->dev, "Unsupported transaction %d\n", size);
		return -EOPNOTSUPP;
	}

	status = sis630_transaction(adap, size);
	if (status)
		return status;

	if ((size != SIS630_PCALL) &&
		((read_write == I2C_SMBUS_WRITE) || (size == SIS630_QUICK))) {
		return 0;
	}

	switch (size) {
	case SIS630_BYTE:
	case SIS630_BYTE_DATA:
		data->byte = sis630_read(SMB_BYTE);
		break;
	case SIS630_PCALL:
	case SIS630_WORD_DATA:
		data->word = sis630_read(SMB_BYTE) +
			     (sis630_read(SMB_BYTE + 1) << 8);
		break;
	}

	return 0;
}

static u32 sis630_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
		I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
		I2C_FUNC_SMBUS_PROC_CALL | I2C_FUNC_SMBUS_BLOCK_DATA;
}

static int sis630_setup(struct pci_dev *sis630_dev)
{
	unsigned char b;
	struct pci_dev *dummy = NULL;
	int retval, i;
	/* acpi base address */
	unsigned short acpi_base;

	/* check for supported SiS devices */
	for (i = 0; supported[i] > 0; i++) {
		dummy = pci_get_device(PCI_VENDOR_ID_SI, supported[i], dummy);
		if (dummy)
			break; /* found */
	}

	if (dummy) {
		pci_dev_put(dummy);
	} else if (force) {
		dev_err(&sis630_dev->dev,
			"WARNING: Can't detect SIS630 compatible device, but "
			"loading because of force option enabled\n");
	} else {
		return -ENODEV;
	}

	/*
	   Enable ACPI first , so we can accsess reg 74-75
	   in acpi io space and read acpi base addr
	*/
	if (pci_read_config_byte(sis630_dev, SIS630_BIOS_CTL_REG, &b)) {
		dev_err(&sis630_dev->dev, "Error: Can't read bios ctl reg\n");
		retval = -ENODEV;
		goto exit;
	}
	/* if ACPI already enabled , do nothing */
	if (!(b & 0x80) &&
	    pci_write_config_byte(sis630_dev, SIS630_BIOS_CTL_REG, b | 0x80)) {
		dev_err(&sis630_dev->dev, "Error: Can't enable ACPI\n");
		retval = -ENODEV;
		goto exit;
	}

	/* Determine the ACPI base address */
	if (pci_read_config_word(sis630_dev,
				 SIS630_ACPI_BASE_REG, &acpi_base)) {
		dev_err(&sis630_dev->dev,
			"Error: Can't determine ACPI base address\n");
		retval = -ENODEV;
		goto exit;
	}

	dev_dbg(&sis630_dev->dev, "ACPI base at 0x%04hx\n", acpi_base);

	if (supported[i] == PCI_DEVICE_ID_SI_760)
		smbus_base = acpi_base + 0xE0;
	else
		smbus_base = acpi_base + 0x80;

	dev_dbg(&sis630_dev->dev, "SMBus base at 0x%04hx\n", smbus_base);

	retval = acpi_check_region(smbus_base + SMB_STS, SIS630_SMB_IOREGION,
				   sis630_driver.name);
	if (retval)
		goto exit;

	/* Everything is happy, let's grab the memory and set things up. */
	if (!request_region(smbus_base + SMB_STS, SIS630_SMB_IOREGION,
			    sis630_driver.name)) {
		dev_err(&sis630_dev->dev,
			"I/O Region 0x%04hx-0x%04hx for SMBus already in use.\n",
			smbus_base + SMB_STS,
			smbus_base + SMB_STS + SIS630_SMB_IOREGION - 1);
		retval = -EBUSY;
		goto exit;
	}

	retval = 0;

exit:
	if (retval)
		smbus_base = 0;
	return retval;
}


static const struct i2c_algorithm smbus_algorithm = {
	.smbus_xfer	= sis630_access,
	.functionality	= sis630_func,
};

static struct i2c_adapter sis630_adapter = {
	.owner		= THIS_MODULE,
	.class		= I2C_CLASS_HWMON | I2C_CLASS_SPD,
	.algo		= &smbus_algorithm,
	.retries	= 3
};

static const struct pci_device_id sis630_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_503) },
	{ PCI_DEVICE(PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_LPC) },
	{ PCI_DEVICE(PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_964) },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, sis630_ids);

static int sis630_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	if (sis630_setup(dev)) {
		dev_err(&dev->dev,
			"SIS630 compatible bus not detected, "
			"module not inserted.\n");
		return -ENODEV;
	}

	/* set up the sysfs linkage to our parent device */
	sis630_adapter.dev.parent = &dev->dev;

	snprintf(sis630_adapter.name, sizeof(sis630_adapter.name),
		 "SMBus SIS630 adapter at %04hx", smbus_base + SMB_STS);

	return i2c_add_adapter(&sis630_adapter);
}

static void sis630_remove(struct pci_dev *dev)
{
	if (smbus_base) {
		i2c_del_adapter(&sis630_adapter);
		release_region(smbus_base + SMB_STS, SIS630_SMB_IOREGION);
		smbus_base = 0;
	}
}


static struct pci_driver sis630_driver = {
	.name		= "sis630_smbus",
	.id_table	= sis630_ids,
	.probe		= sis630_probe,
	.remove		= sis630_remove,
};

module_pci_driver(sis630_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Malysh <amalysh@web.de>");
MODULE_DESCRIPTION("SIS630 SMBus driver");
