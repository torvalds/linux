/*
 *  i2c-pca-isa.c driver for PCA9564 on ISA boards
 *    Copyright (C) 2004 Arcom Control Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/wait.h>

#include <linux/i2c.h>
#include <linux/i2c-algo-pca.h>

#include <asm/io.h>
#include <asm/irq.h>

#include "../algos/i2c-algo-pca.h"

#define IO_SIZE 4

#undef DEBUG_IO
//#define DEBUG_IO

static unsigned long base   = 0x330;
static int irq 	  = 10;

/* Data sheet recommends 59kHz for 100kHz operation due to variation
 * in the actual clock rate */
static int clock  = I2C_PCA_CON_59kHz;

static int own    = 0x55;

static wait_queue_head_t pca_wait;

static int pca_isa_getown(struct i2c_algo_pca_data *adap)
{
	return (own);
}

static int pca_isa_getclock(struct i2c_algo_pca_data *adap)
{
	return (clock);
}

static void
pca_isa_writebyte(struct i2c_algo_pca_data *adap, int reg, int val)
{
#ifdef DEBUG_IO
	static char *names[] = { "T/O", "DAT", "ADR", "CON" };
	printk("*** write %s at %#lx <= %#04x\n", names[reg], base+reg, val);
#endif
	outb(val, base+reg);
}

static int
pca_isa_readbyte(struct i2c_algo_pca_data *adap, int reg)
{
	int res = inb(base+reg);
#ifdef DEBUG_IO
	{
		static char *names[] = { "STA", "DAT", "ADR", "CON" };	
		printk("*** read  %s => %#04x\n", names[reg], res);
	}
#endif
	return res;
}

static int pca_isa_waitforinterrupt(struct i2c_algo_pca_data *adap)
{
	int ret = 0;

	if (irq > -1) {
		ret = wait_event_interruptible(pca_wait,
					       pca_isa_readbyte(adap, I2C_PCA_CON) & I2C_PCA_CON_SI);
	} else {
		while ((pca_isa_readbyte(adap, I2C_PCA_CON) & I2C_PCA_CON_SI) == 0) 
			udelay(100);
	}
	return ret;
}

static irqreturn_t pca_handler(int this_irq, void *dev_id) {
	wake_up_interruptible(&pca_wait);
	return IRQ_HANDLED;
}

static struct i2c_algo_pca_data pca_isa_data = {
	.get_own		= pca_isa_getown,
	.get_clock		= pca_isa_getclock,
	.write_byte		= pca_isa_writebyte,
	.read_byte		= pca_isa_readbyte,
	.wait_for_interrupt	= pca_isa_waitforinterrupt,
};

static struct i2c_adapter pca_isa_ops = {
	.owner          = THIS_MODULE,
	.id		= I2C_HW_A_ISA,
	.algo_data	= &pca_isa_data,
	.name		= "PCA9564 ISA Adapter",
};

static int __init pca_isa_init(void)
{

	init_waitqueue_head(&pca_wait);

	printk(KERN_INFO "i2c-pca-isa: i/o base %#08lx. irq %d\n", base, irq);

	if (!request_region(base, IO_SIZE, "i2c-pca-isa")) {
		printk(KERN_ERR "i2c-pca-isa: I/O address %#08lx is in use.\n", base);
		goto out;
	}

	if (irq > -1) {
		if (request_irq(irq, pca_handler, 0, "i2c-pca-isa", &pca_isa_ops) < 0) {
			printk(KERN_ERR "i2c-pca-isa: Request irq%d failed\n", irq);
			goto out_region;
		}
	}

	if (i2c_pca_add_bus(&pca_isa_ops) < 0) {
		printk(KERN_ERR "i2c-pca-isa: Failed to add i2c bus\n");
		goto out_irq;
	}

	return 0;

 out_irq:
	if (irq > -1)
		free_irq(irq, &pca_isa_ops);
 out_region:
	release_region(base, IO_SIZE);
 out:
	return -ENODEV;
}

static void pca_isa_exit(void)
{
	i2c_del_adapter(&pca_isa_ops);

	if (irq > 0) {
		disable_irq(irq);
		free_irq(irq, &pca_isa_ops);
	}
	release_region(base, IO_SIZE);
}

MODULE_AUTHOR("Ian Campbell <icampbell@arcom.com>");
MODULE_DESCRIPTION("ISA base PCA9564 driver");
MODULE_LICENSE("GPL");

module_param(base, ulong, 0);
MODULE_PARM_DESC(base, "I/O base address");

module_param(irq, int, 0);
MODULE_PARM_DESC(irq, "IRQ");
module_param(clock, int, 0);
MODULE_PARM_DESC(clock, "Clock rate as described in table 1 of PCA9564 datasheet");

module_param(own, int, 0); /* the driver can't do slave mode, so there's no real point in this */

module_init(pca_isa_init);
module_exit(pca_isa_exit);
