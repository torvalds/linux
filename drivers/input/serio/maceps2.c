/*
 * SGI O2 MACE PS2 controller driver for linux
 *
 * Copyright (C) 2002 Vivien Chappelier
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/serio.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/err.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/ip32/mace.h>
#include <asm/ip32/ip32_ints.h>

MODULE_AUTHOR("Vivien Chappelier <vivien.chappelier@linux-mips.org");
MODULE_DESCRIPTION("SGI O2 MACE PS2 controller driver");
MODULE_LICENSE("GPL");

#define MACE_PS2_TIMEOUT 10000 /* in 50us unit */

#define PS2_STATUS_CLOCK_SIGNAL  BIT(0) /* external clock signal */
#define PS2_STATUS_CLOCK_INHIBIT BIT(1) /* clken output signal */
#define PS2_STATUS_TX_INPROGRESS BIT(2) /* transmission in progress */
#define PS2_STATUS_TX_EMPTY      BIT(3) /* empty transmit buffer */
#define PS2_STATUS_RX_FULL       BIT(4) /* full receive buffer */
#define PS2_STATUS_RX_INPROGRESS BIT(5) /* reception in progress */
#define PS2_STATUS_ERROR_PARITY  BIT(6) /* parity error */
#define PS2_STATUS_ERROR_FRAMING BIT(7) /* framing error */

#define PS2_CONTROL_TX_CLOCK_DISABLE BIT(0) /* inhibit clock signal after TX */
#define PS2_CONTROL_TX_ENABLE        BIT(1) /* transmit enable */
#define PS2_CONTROL_TX_INT_ENABLE    BIT(2) /* enable transmit interrupt */
#define PS2_CONTROL_RX_INT_ENABLE    BIT(3) /* enable receive interrupt */
#define PS2_CONTROL_RX_CLOCK_ENABLE  BIT(4) /* pause reception if set to 0 */
#define PS2_CONTROL_RESET            BIT(5) /* reset */

struct maceps2_data {
	struct mace_ps2port *port;
	int irq;
};

static struct maceps2_data port_data[2];
static struct serio *maceps2_port[2];
static struct platform_device *maceps2_device;

static int maceps2_write(struct serio *dev, unsigned char val)
{
	struct mace_ps2port *port = ((struct maceps2_data *)dev->port_data)->port;
	unsigned int timeout = MACE_PS2_TIMEOUT;

	do {
		if (port->status & PS2_STATUS_TX_EMPTY) {
			port->tx = val;
			return 0;
		}
		udelay(50);
	} while (timeout--);

	return -1;
}

static irqreturn_t maceps2_interrupt(int irq, void *dev_id)
{
	struct serio *dev = dev_id;
	struct mace_ps2port *port = ((struct maceps2_data *)dev->port_data)->port;
	unsigned long byte;

	if (port->status & PS2_STATUS_RX_FULL) {
		byte = port->rx;
		serio_interrupt(dev, byte & 0xff, 0);
        }

	return IRQ_HANDLED;
}

static int maceps2_open(struct serio *dev)
{
	struct maceps2_data *data = (struct maceps2_data *)dev->port_data;

	if (request_irq(data->irq, maceps2_interrupt, 0, "PS2 port", dev)) {
		printk(KERN_ERR "Could not allocate PS/2 IRQ\n");
		return -EBUSY;
	}

	/* Reset port */
	data->port->control = PS2_CONTROL_TX_CLOCK_DISABLE | PS2_CONTROL_RESET;
	udelay(100);

        /* Enable interrupts */
	data->port->control = PS2_CONTROL_RX_CLOCK_ENABLE |
			      PS2_CONTROL_TX_ENABLE |
			      PS2_CONTROL_RX_INT_ENABLE;

	return 0;
}

static void maceps2_close(struct serio *dev)
{
	struct maceps2_data *data = (struct maceps2_data *)dev->port_data;

	data->port->control = PS2_CONTROL_TX_CLOCK_DISABLE | PS2_CONTROL_RESET;
	udelay(100);
	free_irq(data->irq, dev);
}


static struct serio * __devinit maceps2_allocate_port(int idx)
{
	struct serio *serio;

	serio = kzalloc(sizeof(struct serio), GFP_KERNEL);
	if (serio) {
		serio->id.type		= SERIO_8042;
		serio->write		= maceps2_write;
		serio->open		= maceps2_open;
		serio->close		= maceps2_close;
		snprintf(serio->name, sizeof(serio->name), "MACE PS/2 port%d", idx);
		snprintf(serio->phys, sizeof(serio->phys), "mace/serio%d", idx);
		serio->port_data	= &port_data[idx];
		serio->dev.parent	= &maceps2_device->dev;
	}

	return serio;
}

static int __devinit maceps2_probe(struct platform_device *dev)
{
	maceps2_port[0] = maceps2_allocate_port(0);
	maceps2_port[1] = maceps2_allocate_port(1);
	if (!maceps2_port[0] || !maceps2_port[1]) {
		kfree(maceps2_port[0]);
		kfree(maceps2_port[1]);
		return -ENOMEM;
	}

	serio_register_port(maceps2_port[0]);
	serio_register_port(maceps2_port[1]);

	return 0;
}

static int __devexit maceps2_remove(struct platform_device *dev)
{
	serio_unregister_port(maceps2_port[0]);
	serio_unregister_port(maceps2_port[1]);

	return 0;
}

static struct platform_driver maceps2_driver = {
	.driver		= {
		.name	= "maceps2",
		.owner	= THIS_MODULE,
	},
	.probe		= maceps2_probe,
	.remove		= __devexit_p(maceps2_remove),
};

static int __init maceps2_init(void)
{
	int error;

	error = platform_driver_register(&maceps2_driver);
	if (error)
		return error;

	maceps2_device = platform_device_alloc("maceps2", -1);
	if (!maceps2_device) {
		error = -ENOMEM;
		goto err_unregister_driver;
	}

	port_data[0].port = &mace->perif.ps2.keyb;
	port_data[0].irq  = MACEISA_KEYB_IRQ;
	port_data[1].port = &mace->perif.ps2.mouse;
	port_data[1].irq  = MACEISA_MOUSE_IRQ;

	error = platform_device_add(maceps2_device);
	if (error)
		goto err_free_device;

	return 0;

 err_free_device:
	platform_device_put(maceps2_device);
 err_unregister_driver:
	platform_driver_unregister(&maceps2_driver);
	return error;
}

static void __exit maceps2_exit(void)
{
	platform_device_unregister(maceps2_device);
	platform_driver_unregister(&maceps2_driver);
}

module_init(maceps2_init);
module_exit(maceps2_exit);
