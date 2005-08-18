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

/* Partialy rewriten by Oleg I. Vdovikin for mmapped support of 
   for Alpha Processor Inc. UP-2000(+) boards */

#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/wait.h>

#include <linux/i2c.h>
#include <linux/i2c-algo-pcf.h>

#include <asm/io.h>
#include <asm/irq.h>

#include "../algos/i2c-algo-pcf.h"

#define DEFAULT_BASE 0x330

static int base;
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

/* ----- local functions ----------------------------------------------	*/

static void pcf_isa_setbyte(void *data, int ctl, int val)
{
	int address = ctl ? (base + 1) : base;

	/* enable irq if any specified for serial operation */
	if (ctl && irq && (val & I2C_PCF_ESO)) {
		val |= I2C_PCF_ENI;
	}

	pr_debug("i2c-elektor: Write 0x%X 0x%02X\n", address, val & 255);

	switch (mmapped) {
	case 0: /* regular I/O */
		outb(val, address);
		break;
	case 2: /* double mapped I/O needed for UP2000 board,
                   I don't know why this... */
		writeb(val, (void *)address);
		/* fall */
	case 1: /* memory mapped I/O */
		writeb(val, (void *)address);
		break;
	}
}

static int pcf_isa_getbyte(void *data, int ctl)
{
	int address = ctl ? (base + 1) : base;
	int val = mmapped ? readb((void *)address) : inb(address);

	pr_debug("i2c-elektor: Read 0x%X 0x%02X\n", address, val);

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

static void pcf_isa_waitforpin(void) {
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


static irqreturn_t pcf_isa_handler(int this_irq, void *dev_id, struct pt_regs *regs) {
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
		if (!request_region(base, 2, "i2c (isa bus adapter)")) {
			printk(KERN_ERR
			       "i2c-elektor: requested I/O region (0x%X:2) "
			       "is in use.\n", base);
			return -ENODEV;
		}
	}
	if (irq > 0) {
		if (request_irq(irq, pcf_isa_handler, 0, "PCF8584", NULL) < 0) {
			printk(KERN_ERR "i2c-elektor: Request irq%d failed\n", irq);
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
	.udelay	    = 10,
	.mdelay	    = 10,
	.timeout    = 100,
};

static struct i2c_adapter pcf_isa_ops = {
	.owner		= THIS_MODULE,
	.class		= I2C_CLASS_HWMON,
	.id		= I2C_HW_P_ELEK,
	.algo_data	= &pcf_isa_data,
	.name		= "PCF8584 ISA adapter",
};

static int __init i2c_pcfisa_init(void) 
{
#ifdef __alpha__
	/* check to see we have memory mapped PCF8584 connected to the 
	Cypress cy82c693 PCI-ISA bridge as on UP2000 board */
	if (base == 0) {
		struct pci_dev *cy693_dev;
		
		cy693_dev = pci_get_device(PCI_VENDOR_ID_CONTAQ, 
					   PCI_DEVICE_ID_CONTAQ_82C693, NULL);
		if (cy693_dev) {
			char config;
			/* yeap, we've found cypress, let's check config */
			if (!pci_read_config_byte(cy693_dev, 0x47, &config)) {
				
				pr_debug("i2c-elektor: found cy82c693, config register 0x47 = 0x%02x.\n", config);

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
                                        /* I don't know why we need to
                                           write twice */
					mmapped = 2;
                                        /* UP2000 drives ISA with
					   8.25 MHz (PCI/4) clock
					   (this can be read from cypress) */
					clock = I2C_PCF_CLK | I2C_PCF_TRNS90;
					printk(KERN_INFO "i2c-elektor: found API UP2000 like board, will probe PCF8584 later.\n");
				}
			}
			pci_dev_put(cy693_dev);
		}
	}
#endif

	/* sanity checks for mmapped I/O */
	if (mmapped && base < 0xc8000) {
		printk(KERN_ERR "i2c-elektor: incorrect base address (0x%0X) specified for mmapped I/O.\n", base);
		return -ENODEV;
	}

	printk(KERN_INFO "i2c-elektor: i2c pcf8584-isa adapter driver\n");

	if (base == 0) {
		base = DEFAULT_BASE;
	}

	init_waitqueue_head(&pcf_wait);
	if (pcf_isa_init())
		return -ENODEV;
	if (i2c_pcf_add_bus(&pcf_isa_ops) < 0)
		goto fail;
	
	printk(KERN_ERR "i2c-elektor: found device at %#x.\n", base);

	return 0;

 fail:
	if (irq > 0) {
		disable_irq(irq);
		free_irq(irq, NULL);
	}

	if (!mmapped)
		release_region(base , 2);
	return -ENODEV;
}

static void i2c_pcfisa_exit(void)
{
	i2c_pcf_del_bus(&pcf_isa_ops);

	if (irq > 0) {
		disable_irq(irq);
		free_irq(irq, NULL);
	}

	if (!mmapped)
		release_region(base , 2);
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
