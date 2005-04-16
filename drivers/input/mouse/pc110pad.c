/*
 * $Id: pc110pad.c,v 1.12 2001/09/25 10:12:07 vojtech Exp $
 *
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 *
 *  Based on the work of:
 *	Alan Cox	Robin O'Leary	
 */

/*
 * IBM PC110 touchpad driver for Linux
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>

#include <asm/io.h>
#include <asm/irq.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("IBM PC110 touchpad driver");
MODULE_LICENSE("GPL");

#define PC110PAD_OFF	0x30
#define PC110PAD_ON	0x38

static int pc110pad_irq = 10;
static int pc110pad_io = 0x15e0;

static struct input_dev pc110pad_dev;
static int pc110pad_data[3];
static int pc110pad_count;
static int pc110pad_used;

static char *pc110pad_name = "IBM PC110 TouchPad";
static char *pc110pad_phys = "isa15e0/input0";

static irqreturn_t pc110pad_interrupt(int irq, void *ptr, struct pt_regs *regs)
{
	int value     = inb_p(pc110pad_io);
	int handshake = inb_p(pc110pad_io + 2);

	outb_p(handshake |  1, pc110pad_io + 2);
	outb_p(handshake & ~1, pc110pad_io + 2);
	inb_p(0x64);

	pc110pad_data[pc110pad_count++] = value;

	if (pc110pad_count < 3)
		return IRQ_HANDLED;
	
	input_regs(&pc110pad_dev, regs);
	input_report_key(&pc110pad_dev, BTN_TOUCH,
		pc110pad_data[0] & 0x01);
	input_report_abs(&pc110pad_dev, ABS_X,
		pc110pad_data[1] | ((pc110pad_data[0] << 3) & 0x80) | ((pc110pad_data[0] << 1) & 0x100));
	input_report_abs(&pc110pad_dev, ABS_Y,
		pc110pad_data[2] | ((pc110pad_data[0] << 4) & 0x80));
	input_sync(&pc110pad_dev);

	pc110pad_count = 0;
	return IRQ_HANDLED;
}

static void pc110pad_close(struct input_dev *dev)
{
	if (!--pc110pad_used)
		outb(PC110PAD_OFF, pc110pad_io + 2);
}

static int pc110pad_open(struct input_dev *dev)
{
	if (pc110pad_used++)
		return 0;

	pc110pad_interrupt(0,NULL,NULL);
	pc110pad_interrupt(0,NULL,NULL);
	pc110pad_interrupt(0,NULL,NULL);
	outb(PC110PAD_ON, pc110pad_io + 2);
	pc110pad_count = 0;

	return 0;
}

/*
 * We try to avoid enabling the hardware if it's not
 * there, but we don't know how to test. But we do know
 * that the PC110 is not a PCI system. So if we find any
 * PCI devices in the machine, we don't have a PC110.
 */
static int __init pc110pad_init(void)
{
	struct pci_dev *dev;

	dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, NULL);
	if (dev) {
		pci_dev_put(dev);
		return -ENOENT;
	}

	if (!request_region(pc110pad_io, 4, "pc110pad")) {
		printk(KERN_ERR "pc110pad: I/O area %#x-%#x in use.\n",
				pc110pad_io, pc110pad_io + 4);
		return -EBUSY;
	}

	outb(PC110PAD_OFF, pc110pad_io + 2);

	if (request_irq(pc110pad_irq, pc110pad_interrupt, 0, "pc110pad", NULL))
	{
		release_region(pc110pad_io, 4);
		printk(KERN_ERR "pc110pad: Unable to get irq %d.\n", pc110pad_irq);
		return -EBUSY;
	}

        pc110pad_dev.evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
        pc110pad_dev.absbit[0] = BIT(ABS_X) | BIT(ABS_Y);
        pc110pad_dev.keybit[LONG(BTN_TOUCH)] = BIT(BTN_TOUCH);

	pc110pad_dev.absmax[ABS_X] = 0x1ff;
	pc110pad_dev.absmax[ABS_Y] = 0x0ff;
        
	pc110pad_dev.open = pc110pad_open;
        pc110pad_dev.close = pc110pad_close;

	pc110pad_dev.name = pc110pad_name;
	pc110pad_dev.phys = pc110pad_phys;
	pc110pad_dev.id.bustype = BUS_ISA;
	pc110pad_dev.id.vendor = 0x0003;
	pc110pad_dev.id.product = 0x0001;
	pc110pad_dev.id.version = 0x0100;

	input_register_device(&pc110pad_dev);	

	printk(KERN_INFO "input: %s at %#x irq %d\n",
		pc110pad_name, pc110pad_io, pc110pad_irq);
	
	return 0;
}
 
static void __exit pc110pad_exit(void)
{
	input_unregister_device(&pc110pad_dev);	

	outb(PC110PAD_OFF, pc110pad_io + 2);

	free_irq(pc110pad_irq, NULL);
	release_region(pc110pad_io, 4);
}

module_init(pc110pad_init);
module_exit(pc110pad_exit);
