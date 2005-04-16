/*
 *    kernel/busses/i2c-prosavage.c
 *
 *    i2c bus driver for S3/VIA 8365/8375 graphics processor.
 *    Copyright (c) 2003 Henk Vergonet <henk@god.dyndns.org>
 *    Based on code written by:
 *	Frodo Looijaard <frodol@dds.nl>,
 *	Philip Edelbrock <phil@netroedge.com>,
 *	Ralph Metzler <rjkm@thp.uni-koeln.de>, and
 *	Mark D. Studebaker <mdsxyz123@yahoo.com>
 *	Simon Vogl
 *	and others
 *
 *    Please read the lm_sensors documentation for details on use.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
/*  18-05-2003 HVE - created
 *  14-06-2003 HVE - adapted for lm_sensors2
 *  17-06-2003 HVE - linux 2.5.xx compatible
 *  18-06-2003 HVE - codingstyle
 *  21-06-2003 HVE - compatibility lm_sensors2 and linux 2.5.xx
 *		     codingstyle, mmio enabled
 *
 *  This driver interfaces to the I2C bus of the VIA north bridge embedded
 *  ProSavage4/8 devices. Usefull for gaining access to the TV Encoder chips.
 *
 *  Graphics cores:
 *   S3/VIA KM266/VT8375 aka ProSavage8
 *   S3/VIA KM133/VT8365 aka Savage4
 *
 *  Two serial busses are implemented:
 *   SERIAL1 - I2C serial communications interface
 *   SERIAL2 - DDC2 monitor communications interface
 *
 *  Tested on a FX41 mainboard, see http://www.shuttle.com
 * 
 *
 *  TODO:
 *  - integration with prosavage framebuffer device
 *    (Additional documentation needed :(
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <asm/io.h>

/*
 * driver configuration
 */
#define MAX_BUSSES	2

struct s_i2c_bus {
	void __iomem *mmvga;
	int	i2c_reg;
	int	adap_ok;
	struct i2c_adapter		adap;
	struct i2c_algo_bit_data	algo;
};

struct s_i2c_chip {
	void __iomem *mmio;
	struct s_i2c_bus	i2c_bus[MAX_BUSSES];
};


/*
 * i2c configuration
 */
#ifndef I2C_HW_B_S3VIA
#define I2C_HW_B_S3VIA	0x18	/* S3VIA ProSavage adapter		*/
#endif

/* delays */
#define CYCLE_DELAY	10
#define TIMEOUT		(HZ / 2)


/* 
 * S3/VIA 8365/8375 registers
 */
#define VGA_CR_IX	0x3d4
#define VGA_CR_DATA	0x3d5

#define CR_SERIAL1	0xa0	/* I2C serial communications interface */
#define MM_SERIAL1	0xff20
#define CR_SERIAL2	0xb1	/* DDC2 monitor communications interface */

/* based on vt8365 documentation */
#define I2C_ENAB	0x10
#define I2C_SCL_OUT	0x01
#define I2C_SDA_OUT	0x02
#define I2C_SCL_IN	0x04
#define I2C_SDA_IN	0x08

#define SET_CR_IX(p, val)	writeb((val), (p)->mmvga + VGA_CR_IX)
#define SET_CR_DATA(p, val)	writeb((val), (p)->mmvga + VGA_CR_DATA)
#define GET_CR_DATA(p)		readb((p)->mmvga + VGA_CR_DATA)


/*
 * Serial bus line handling
 *
 * serial communications register as parameter in private data
 *
 * TODO: locks with other code sections accessing video registers?
 */
static void bit_s3via_setscl(void *bus, int val)
{
	struct s_i2c_bus *p = (struct s_i2c_bus *)bus;
	unsigned int r;

	SET_CR_IX(p, p->i2c_reg);
	r = GET_CR_DATA(p);
	r |= I2C_ENAB;
	if (val) {
		r |= I2C_SCL_OUT;
	} else {
		r &= ~I2C_SCL_OUT;
	}
	SET_CR_DATA(p, r);
}

static void bit_s3via_setsda(void *bus, int val)
{
	struct s_i2c_bus *p = (struct s_i2c_bus *)bus;
	unsigned int r;
	
	SET_CR_IX(p, p->i2c_reg);
	r = GET_CR_DATA(p);
	r |= I2C_ENAB;
	if (val) {
		r |= I2C_SDA_OUT;
	} else {
		r &= ~I2C_SDA_OUT;
	}
	SET_CR_DATA(p, r);
}

static int bit_s3via_getscl(void *bus)
{
	struct s_i2c_bus *p = (struct s_i2c_bus *)bus;

	SET_CR_IX(p, p->i2c_reg);
	return (0 != (GET_CR_DATA(p) & I2C_SCL_IN));
}

static int bit_s3via_getsda(void *bus)
{
	struct s_i2c_bus *p = (struct s_i2c_bus *)bus;

	SET_CR_IX(p, p->i2c_reg);
	return (0 != (GET_CR_DATA(p) & I2C_SDA_IN));
}


/*
 * adapter initialisation
 */
static int i2c_register_bus(struct pci_dev *dev, struct s_i2c_bus *p, void __iomem *mmvga, u32 i2c_reg)
{
	int ret;
	p->adap.owner	  = THIS_MODULE;
	p->adap.id	  = I2C_HW_B_S3VIA;
	p->adap.algo_data = &p->algo;
	p->adap.dev.parent = &dev->dev;
	p->algo.setsda	  = bit_s3via_setsda;
	p->algo.setscl	  = bit_s3via_setscl;
	p->algo.getsda	  = bit_s3via_getsda;
	p->algo.getscl	  = bit_s3via_getscl;
	p->algo.udelay	  = CYCLE_DELAY;
	p->algo.mdelay	  = CYCLE_DELAY;
	p->algo.timeout	  = TIMEOUT;
	p->algo.data	  = p;
	p->mmvga	  = mmvga;
	p->i2c_reg	  = i2c_reg;
    
	ret = i2c_bit_add_bus(&p->adap);
	if (ret) {
		return ret;
	}

	p->adap_ok = 1;
	return 0;
}


/*
 * Cleanup stuff
 */
static void prosavage_remove(struct pci_dev *dev)
{
	struct s_i2c_chip *chip;
	int i, ret;

	chip = (struct s_i2c_chip *)pci_get_drvdata(dev);

	if (!chip) {
		return;
	}
	for (i = MAX_BUSSES - 1; i >= 0; i--) {
		if (chip->i2c_bus[i].adap_ok == 0)
			continue;

		ret = i2c_bit_del_bus(&chip->i2c_bus[i].adap);
	        if (ret) {
			dev_err(&dev->dev, "%s not removed\n",
				chip->i2c_bus[i].adap.name);
		}
	}
	if (chip->mmio) {
		iounmap(chip->mmio);
	}
	kfree(chip);
}


/*
 * Detect chip and initialize it
 */
static int __devinit prosavage_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int ret;
	unsigned long base, len;
	struct s_i2c_chip *chip;
	struct s_i2c_bus  *bus;

        pci_set_drvdata(dev, kmalloc(sizeof(struct s_i2c_chip), GFP_KERNEL)); 
	chip = (struct s_i2c_chip *)pci_get_drvdata(dev);
	if (chip == NULL) {
		return -ENOMEM;
	}

	memset(chip, 0, sizeof(struct s_i2c_chip));

	base = dev->resource[0].start & PCI_BASE_ADDRESS_MEM_MASK;
	len  = dev->resource[0].end - base + 1;
	chip->mmio = ioremap_nocache(base, len);

	if (chip->mmio == NULL) {
		dev_err(&dev->dev, "ioremap failed\n");
		prosavage_remove(dev);
		return -ENODEV;
	}


	/*
	 * Chip initialisation
	 */
	/* Unlock Extended IO Space ??? */


	/*
	 * i2c bus registration
	 */
	bus = &chip->i2c_bus[0];
	snprintf(bus->adap.name, sizeof(bus->adap.name),
		"ProSavage I2C bus at %02x:%02x.%x",
		dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
	ret = i2c_register_bus(dev, bus, chip->mmio + 0x8000, CR_SERIAL1);
	if (ret) {
		goto err_adap;
	}
	/*
	 * ddc bus registration
	 */
	bus = &chip->i2c_bus[1];
	snprintf(bus->adap.name, sizeof(bus->adap.name),
		"ProSavage DDC bus at %02x:%02x.%x",
		dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
	ret = i2c_register_bus(dev, bus, chip->mmio + 0x8000, CR_SERIAL2);
	if (ret) {
		goto err_adap;
	}
	return 0;
err_adap:
	dev_err(&dev->dev, "%s failed\n", bus->adap.name);
	prosavage_remove(dev);
	return ret;
}


/*
 * Data for PCI driver interface
 */
static struct pci_device_id prosavage_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_S3, PCI_DEVICE_ID_S3_SAVAGE4) },
	{ PCI_DEVICE(PCI_VENDOR_ID_S3, PCI_DEVICE_ID_S3_PROSAVAGE8) },
	{ 0, },
};

MODULE_DEVICE_TABLE (pci, prosavage_pci_tbl);

static struct pci_driver prosavage_driver = {
	.name		=	"prosavage_smbus",
	.id_table	=	prosavage_pci_tbl,
	.probe		=	prosavage_probe,
	.remove		=	prosavage_remove,
};

static int __init i2c_prosavage_init(void)
{
	return pci_register_driver(&prosavage_driver);
}

static void __exit i2c_prosavage_exit(void)
{
	pci_unregister_driver(&prosavage_driver);
}

MODULE_DEVICE_TABLE(pci, prosavage_pci_tbl);
MODULE_AUTHOR("Henk Vergonet");
MODULE_DESCRIPTION("ProSavage VIA 8365/8375 smbus driver");
MODULE_LICENSE("GPL");

module_init (i2c_prosavage_init);
module_exit (i2c_prosavage_exit);
