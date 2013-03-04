/* ------------------------------------------------------------------------- */
/* i2c-elektor.c i2c-hw access for PCF8584 style isa bus adaptes             */
/* ------------------------------------------------------------------------- */
/*   Copyright (C) 1995-97 Simon G. Vogl
                   1998-99 Hans Berglund

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
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.		     */
/* ------------------------------------------------------------------------- */

/* With some changes from Kyösti Mälkki <kmalkki@cc.hut.fi> and even
   Frodo Looijaard <frodol@dds.nl> */

/* Partially rewriten by Oleg I. Vdovikin for mmapped support of
   for Alpha Processor Inc. UP-2000(+) boards */

#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/wait.h>

#include <linux/isa.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-pcf.h>
#include <linux/io.h>

#include <asm/irq.h>

#include "../algos/i2c-algo-pcf.h"

#define DEFAULT_BASE 0x330

static int base;
static u8 __iomem *base_iomem;

static int irq;
static int clock  = 0x1c;
static int own    = 0x55;
static int mmapped;

/* vdovikin: removed static struct i2c_pcf_isa gpi; code -
  this module in real supports only one device, due to missing arguments
  in some functions, called from the algo-pcf module. Sometimes it's
  need to be rewriten - but for now just remove this for simpler reading */

static wait_queue_head_t pcf_wait;
static int pcf_pending;
static spinlock_t lock;

static struct i2c_adapter pcf_isa_ops;

/* ----- local functions ----------------------------------------------	*/

static void pcf_isa_setbyte(void *data, int ctl, int val)
{
	u8 __iomem *address = ctl ? (base_iomem + 1) : base_iomem;

	/* enable irq if any specified for serial operation */
	if (ctl && irq && (val & I2C_PCF_ESO)) {
		val |= I2C_PCF_ENI;
	}

	pr_debug("%s: Write %p 0x%02X\n", pcf_isa_ops.name, address, val);
	iowrite8(val, address);
#ifdef __alpha__
	/* API UP2000 needs some hardware fudging to make the write stick */
	iowrite8(val, address);
#endif
}

static int pcf_isa_getbyte(void *data, int ctl)
{
	u8 __iomem *address = ctl ? (base_iomem + 1) : base_iomem;
	int val = ioread8(address);

	pr_debug("%s: Read %p 0x%02X\n", pcf_isa_ops.name, address, val);
	return (val);
}

static int pcf_isa_getown(void *data)
{
	return (own);
}


static int pcf_isa_getclock(void *data)
{
	return (clock);
}

static void pcf_isa_waitforpin(void *data)
{
	DEFINE_WAIT(wait);
	int timeout = 2;
	unsigned long flags;

	if (irq > 0) {
		spin_lock_irqsave(&lock, flags);
		if (pcf_pending == 0) {
			spin_unlock_irqrestore(&lock, flags);
			prepare_to_wait(&pcf_wait, &wait, TASK_INTERRUPTIBLE);
			if (schedule_timeout(timeout*HZ)) {
				spin_lock_irqsave(&lock, flags);
				if (pcf_pending == 1) {
					pcf_pending = 0;
				}
				spin_unlock_irqrestore(&lock, flags);
			}
			finish_wait(&pcf_wait, &wait);
		} else {
			pcf_pending = 0;
			spin_unlock_irqrestore(&lock, flags);
		}
	} else {
		udelay(100);
	}
}


static irqreturn_t pcf_isa_handler(int this_irq, void *dev_id) {
	spin_lock(&lock);
	pcf_pending = 1;
	spin_unlock(&lock);
	wake_up_interruptible(&pcf_wait);
	return IRQ_HANDLED;
}


static int pcf_isa_init(void)
{
	spin_lock_init(&lock);
	if (!mmapped) {
		if (!request_region(base, 2, pcf_isa_ops.name)) {
			printk(KERN_ERR "%s: requested I/O region (%#x:2) is "
			       "in use\n", pcf_isa_ops.name, base);
			return -ENODEV;
		}
		base_iomem = ioport_map(base, 2);
		if (!base_iomem) {
			printk(KERN_ERR "%s: remap of I/O region %#x failed\n",
			       pcf_isa_ops.name, base);
			release_region(base, 2);
			return -ENODEV;
		}
	} else {
		if (!request_mem_region(base, 2, pcf_isa_ops.name)) {
			printk(KERN_ERR "%s: requested memory region (%#x:2) "
			       "is in use\n", pcf_isa_ops.name, base);
			return -ENODEV;
		}
		base_iomem = ioremap(base, 2);
		if (base_iomem == NULL) {
			printk(KERN_ERR "%s: remap of memory region %#x "
			       "failed\n", pcf_isa_ops.name, base);
			release_mem_region(base, 2);
			return -ENODEV;
		}
	}
	pr_debug("%s: registers %#x remapped to %p\n", pcf_isa_ops.name, base,
		 base_iomem);

	if (irq > 0) {
		if (request_irq(irq, pcf_isa_handler, 0, pcf_isa_ops.name,
				NULL) < 0) {
			printk(KERN_ERR "%s: Request irq%d failed\n",
			       pcf_isa_ops.name, irq);
			irq = 0;
		} else
			enable_irq(irq);
	}
	return 0;
}

/* ------------------------------------------------------------------------
 * Encapsulate the above functions in the correct operations structure.
 * This is only done when more than one hardware adapter is supported.
 */
static struct i2c_algo_pcf_data pcf_isa_data = {
	.setpcf	    = pcf_isa_setbyte,
	.getpcf	    = pcf_isa_getbyte,
	.getown	    = pcf_isa_getown,
	.getclock   = pcf_isa_getclock,
	.waitforpin = pcf_isa_waitforpin,
};

static struct i2c_adapter pcf_isa_ops = {
	.owner		= THIS_MODULE,
	.class		= I2C_CLASS_HWMON | I2C_CLASS_SPD,
	.algo_data	= &pcf_isa_data,
	.name		= "i2c-elektor",
};

static int elektor_match(struct device *dev, unsigned int id)
{
#ifdef __alpha__
	/* check to see we have memory mapped PCF8584 connected to the
	Cypress cy82c693 PCI-ISA bridge as on UP2000 board */
	if (base == 0) {
		struct pci_dev *cy693_dev;

		cy693_dev = pci_get_device(PCI_VENDOR_ID_CONTAQ,
					   PCI_DEVICE_ID_CONTAQ_82C693, NULL);
		if (cy693_dev) {
			unsigned char config;
			/* yeap, we've found cypress, let's check config */
			if (!pci_read_config_byte(cy693_dev, 0x47, &config)) {

				dev_dbg(dev, "found cy82c693, config "
					"register 0x47 = 0x%02x\n", config);

				/* UP2000 board has this register set to 0xe1,
				   but the most significant bit as seems can be
				   reset during the proper initialisation
				   sequence if guys from API decides to do that
				   (so, we can even enable Tsunami Pchip
				   window for the upper 1 Gb) */

				/* so just check for ROMCS at 0xe0000,
				   ROMCS enabled for writes
				   and external XD Bus buffer in use. */
				if ((config & 0x7f) == 0x61) {
					/* seems to be UP2000 like board */
					base = 0xe0000;
					mmapped = 1;
					/* UP2000 drives ISA with
					   8.25 MHz (PCI/4) clock
					   (this can be read from cypress) */
					clock = I2C_PCF_CLK | I2C_PCF_TRNS90;
					dev_info(dev, "found API UP2000 like "
						 "board, will probe PCF8584 "
						 "later\n");
				}
			}
			pci_dev_put(cy693_dev);
		}
	}
#endif

	/* sanity checks for mmapped I/O */
	if (mmapped && base < 0xc8000) {
		dev_err(dev, "incorrect base address (%#x) specified "
		       "for mmapped I/O\n", base);
		return 0;
	}

	if (base == 0) {
		base = DEFAULT_BASE;
	}
	return 1;
}

static int elektor_probe(struct device *dev, unsigned int id)
{
	init_waitqueue_head(&pcf_wait);
	if (pcf_isa_init())
		return -ENODEV;
	pcf_isa_ops.dev.parent = dev;
	if (i2c_pcf_add_bus(&pcf_isa_ops) < 0)
		goto fail;

	dev_info(dev, "found device at %#x\n", base);

	return 0;

 fail:
	if (irq > 0) {
		disable_irq(irq);
		free_irq(irq, NULL);
	}

	if (!mmapped) {
		ioport_unmap(base_iomem);
		release_region(base, 2);
	} else {
		iounmap(base_iomem);
		release_mem_region(base, 2);
	}
	return -ENODEV;
}

static int elektor_remove(struct device *dev, unsigned int id)
{
	i2c_del_adapter(&pcf_isa_ops);

	if (irq > 0) {
		disable_irq(irq);
		free_irq(irq, NULL);
	}

	if (!mmapped) {
		ioport_unmap(base_iomem);
		release_region(base, 2);
	} else {
		iounmap(base_iomem);
		release_mem_region(base, 2);
	}

	return 0;
}

static struct isa_driver i2c_elektor_driver = {
	.match		= elektor_match,
	.probe		= elektor_probe,
	.remove		= elektor_remove,
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "i2c-elektor",
	},
};

static int __init i2c_pcfisa_init(void)
{
	return isa_register_driver(&i2c_elektor_driver, 1);
}

static void __exit i2c_pcfisa_exit(void)
{
	isa_unregister_driver(&i2c_elektor_driver);
}

MODULE_AUTHOR("Hans Berglund <hb@spacetec.no>");
MODULE_DESCRIPTION("I2C-Bus adapter routines for PCF8584 ISA bus adapter");
MODULE_LICENSE("GPL");

module_param(base, int, 0);
module_param(irq, int, 0);
module_param(clock, int, 0);
module_param(own, int, 0);
module_param(mmapped, int, 0);

module_init(i2c_pcfisa_init);
module_exit(i2c_pcfisa_exit);
