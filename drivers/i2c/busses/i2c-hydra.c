/*
    i2c-hydra.c - Part of lm_sensors,  Linux kernel modules
                  for hardware monitoring

    i2c Support for the Apple `Hydra' Mac I/O

    Copyright (c) 1999-2004 Geert Uytterhoeven <geert@linux-m68k.org>

    Based on i2c Support for Via Technologies 82C586B South Bridge
    Copyright (c) 1998, 1999 Kyösti Mälkki <kmalkki@cc.hut.fi>

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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/hydra.h>


#define HYDRA_CPD_PD0	0x00000001	/* CachePD lines */
#define HYDRA_CPD_PD1	0x00000002
#define HYDRA_CPD_PD2	0x00000004
#define HYDRA_CPD_PD3	0x00000008

#define HYDRA_SCLK	HYDRA_CPD_PD0
#define HYDRA_SDAT	HYDRA_CPD_PD1
#define HYDRA_SCLK_OE	0x00000010
#define HYDRA_SDAT_OE	0x00000020

static inline void pdregw(void *data, u32 val)
{
	struct Hydra *hydra = (struct Hydra *)data;
	writel(val, &hydra->CachePD);
}

static inline u32 pdregr(void *data)
{
	struct Hydra *hydra = (struct Hydra *)data;
	return readl(&hydra->CachePD);
}

static void hydra_bit_setscl(void *data, int state)
{
	u32 val = pdregr(data);
	if (state)
		val &= ~HYDRA_SCLK_OE;
	else {
		val &= ~HYDRA_SCLK;
		val |= HYDRA_SCLK_OE;
	}
	pdregw(data, val);
}

static void hydra_bit_setsda(void *data, int state)
{
	u32 val = pdregr(data);
	if (state)
		val &= ~HYDRA_SDAT_OE;
	else {
		val &= ~HYDRA_SDAT;
		val |= HYDRA_SDAT_OE;
	}
	pdregw(data, val);
}

static int hydra_bit_getscl(void *data)
{
	return (pdregr(data) & HYDRA_SCLK) != 0;
}

static int hydra_bit_getsda(void *data)
{
	return (pdregr(data) & HYDRA_SDAT) != 0;
}

/* ------------------------------------------------------------------------ */

static struct i2c_algo_bit_data hydra_bit_data = {
	.setsda		= hydra_bit_setsda,
	.setscl		= hydra_bit_setscl,
	.getsda		= hydra_bit_getsda,
	.getscl		= hydra_bit_getscl,
	.udelay		= 5,
	.mdelay		= 5,
	.timeout	= HZ
};

static struct i2c_adapter hydra_adap = {
	.owner		= THIS_MODULE,
	.name		= "Hydra i2c",
	.id		= I2C_HW_B_HYDRA,
	.algo_data	= &hydra_bit_data,
};

static struct pci_device_id hydra_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_APPLE, PCI_DEVICE_ID_APPLE_HYDRA) },
	{ 0, }
};

MODULE_DEVICE_TABLE (pci, hydra_ids);

static int __devinit hydra_probe(struct pci_dev *dev,
				 const struct pci_device_id *id)
{
	unsigned long base = pci_resource_start(dev, 0);
	int res;

	if (!request_mem_region(base+offsetof(struct Hydra, CachePD), 4,
				hydra_adap.name))
		return -EBUSY;

	hydra_bit_data.data = ioremap(base, pci_resource_len(dev, 0));
	if (hydra_bit_data.data == NULL) {
		release_mem_region(base+offsetof(struct Hydra, CachePD), 4);
		return -ENODEV;
	}

	pdregw(hydra_bit_data.data, 0);		/* clear SCLK_OE and SDAT_OE */
	hydra_adap.dev.parent = &dev->dev;
	res = i2c_bit_add_bus(&hydra_adap);
	if (res < 0) {
		iounmap(hydra_bit_data.data);
		release_mem_region(base+offsetof(struct Hydra, CachePD), 4);
		return res;
	}
	return 0;
}

static void __devexit hydra_remove(struct pci_dev *dev)
{
	pdregw(hydra_bit_data.data, 0);		/* clear SCLK_OE and SDAT_OE */
	i2c_bit_del_bus(&hydra_adap);
	iounmap(hydra_bit_data.data);
	release_mem_region(pci_resource_start(dev, 0)+
			   offsetof(struct Hydra, CachePD), 4);
}


static struct pci_driver hydra_driver = {
	.name		= "hydra_smbus",
	.id_table	= hydra_ids,
	.probe		= hydra_probe,
	.remove		= __devexit_p(hydra_remove),
};

static int __init i2c_hydra_init(void)
{
	return pci_register_driver(&hydra_driver);
}


static void __exit i2c_hydra_exit(void)
{
	pci_unregister_driver(&hydra_driver);
}



MODULE_AUTHOR("Geert Uytterhoeven <geert@linux-m68k.org>");
MODULE_DESCRIPTION("i2c for Apple Hydra Mac I/O");
MODULE_LICENSE("GPL");

module_init(i2c_hydra_init);
module_exit(i2c_hydra_exit);

