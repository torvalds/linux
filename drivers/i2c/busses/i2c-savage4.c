/*
    i2c-savage4.c - Part of lm_sensors, Linux kernel modules for hardware
              monitoring
    Copyright (C) 1998-2003  The LM Sensors Team
    Alexander Wold <awold@bigfoot.com>
    Mark D. Studebaker <mdsxyz123@yahoo.com>
    
    Based on i2c-voodoo3.c.

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

/* This interfaces to the I2C bus of the Savage4 to gain access to
   the BT869 and possibly other I2C devices. The DDC bus is not
   yet supported because its register is not memory-mapped.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <asm/io.h>

/* device IDs */
#define PCI_CHIP_SAVAGE4	0x8A22
#define PCI_CHIP_SAVAGE2000	0x9102

#define REG			0xff20	/* Serial Port 1 Register */

/* bit locations in the register */
#define I2C_ENAB		0x00000020
#define I2C_SCL_OUT		0x00000001
#define I2C_SDA_OUT		0x00000002
#define I2C_SCL_IN		0x00000008
#define I2C_SDA_IN		0x00000010

/* delays */
#define CYCLE_DELAY		10
#define TIMEOUT			(HZ / 2)


static void __iomem *ioaddr;

/* The sav GPIO registers don't have individual masks for each bit
   so we always have to read before writing. */

static void bit_savi2c_setscl(void *data, int val)
{
	unsigned int r;
	r = readl(ioaddr + REG);
	if(val)
		r |= I2C_SCL_OUT;
	else
		r &= ~I2C_SCL_OUT;
	writel(r, ioaddr + REG);
	readl(ioaddr + REG);	/* flush posted write */
}

static void bit_savi2c_setsda(void *data, int val)
{
	unsigned int r;
	r = readl(ioaddr + REG);
	if(val)
		r |= I2C_SDA_OUT;
	else
		r &= ~I2C_SDA_OUT;
	writel(r, ioaddr + REG);
	readl(ioaddr + REG);	/* flush posted write */
}

/* The GPIO pins are open drain, so the pins always remain outputs.
   We rely on the i2c-algo-bit routines to set the pins high before
   reading the input from other chips. */

static int bit_savi2c_getscl(void *data)
{
	return (0 != (readl(ioaddr + REG) & I2C_SCL_IN));
}

static int bit_savi2c_getsda(void *data)
{
	return (0 != (readl(ioaddr + REG) & I2C_SDA_IN));
}

/* Configures the chip */

static int config_s4(struct pci_dev *dev)
{
	unsigned long cadr;

	/* map memory */
	cadr = dev->resource[0].start;
	cadr &= PCI_BASE_ADDRESS_MEM_MASK;
	ioaddr = ioremap_nocache(cadr, 0x0080000);
	if (ioaddr) {
		/* writel(0x8160, ioaddr + REG2); */
		writel(0x00000020, ioaddr + REG);
		dev_info(&dev->dev, "Using Savage4 at %p\n", ioaddr);
		return 0;
	}
	return -ENODEV;
}

static struct i2c_algo_bit_data sav_i2c_bit_data = {
	.setsda		= bit_savi2c_setsda,
	.setscl		= bit_savi2c_setscl,
	.getsda		= bit_savi2c_getsda,
	.getscl		= bit_savi2c_getscl,
	.udelay		= CYCLE_DELAY,
	.timeout	= TIMEOUT
};

static struct i2c_adapter savage4_i2c_adapter = {
	.owner		= THIS_MODULE,
	.id		= I2C_HW_B_SAVAGE,
	.name		= "I2C Savage4 adapter",
	.algo_data	= &sav_i2c_bit_data,
};

static struct pci_device_id savage4_ids[] __devinitdata = {
	{ PCI_DEVICE(PCI_VENDOR_ID_S3, PCI_CHIP_SAVAGE4) },
	{ PCI_DEVICE(PCI_VENDOR_ID_S3, PCI_CHIP_SAVAGE2000) },
	{ 0, }
};

MODULE_DEVICE_TABLE (pci, savage4_ids);

static int __devinit savage4_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int retval;

	retval = config_s4(dev);
	if (retval)
		return retval;

	/* set up the sysfs linkage to our parent device */
	savage4_i2c_adapter.dev.parent = &dev->dev;

	return i2c_bit_add_bus(&savage4_i2c_adapter);
}

static void __devexit savage4_remove(struct pci_dev *dev)
{
	i2c_del_adapter(&savage4_i2c_adapter);
	iounmap(ioaddr);
}

static struct pci_driver savage4_driver = {
	.name		= "savage4_smbus",
	.id_table	= savage4_ids,
	.probe		= savage4_probe,
	.remove		= __devexit_p(savage4_remove),
};

static int __init i2c_savage4_init(void)
{
	return pci_register_driver(&savage4_driver);
}

static void __exit i2c_savage4_exit(void)
{
	pci_unregister_driver(&savage4_driver);
}

MODULE_AUTHOR("Alexander Wold <awold@bigfoot.com> "
		"and Mark D. Studebaker <mdsxyz123@yahoo.com>");
MODULE_DESCRIPTION("Savage4 I2C/SMBus driver");
MODULE_LICENSE("GPL");

module_init(i2c_savage4_init);
module_exit(i2c_savage4_exit);
