/*
 *  linux/drivers/input/serio/sa1111ps2.c
 *
 *  Copyright (C) 2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <asm/io.h>

#include <asm/hardware/sa1111.h>

#define PS2CR		0x0000
#define PS2STAT		0x0004
#define PS2DATA		0x0008
#define PS2CLKDIV	0x000c
#define PS2PRECNT	0x0010

#define PS2CR_ENA	0x08
#define PS2CR_FKD	0x02
#define PS2CR_FKC	0x01

#define PS2STAT_STP	0x0100
#define PS2STAT_TXE	0x0080
#define PS2STAT_TXB	0x0040
#define PS2STAT_RXF	0x0020
#define PS2STAT_RXB	0x0010
#define PS2STAT_ENA	0x0008
#define PS2STAT_RXP	0x0004
#define PS2STAT_KBD	0x0002
#define PS2STAT_KBC	0x0001

struct ps2if {
	struct serio		*io;
	struct sa1111_dev	*dev;
	void __iomem		*base;
	unsigned int		open;
	spinlock_t		lock;
	unsigned int		head;
	unsigned int		tail;
	unsigned char		buf[4];
};

/*
 * Read all bytes waiting in the PS2 port.  There should be
 * at the most one, but we loop for safety.  If there was a
 * framing error, we have to manually clear the status.
 */
static irqreturn_t ps2_rxint(int irq, void *dev_id)
{
	struct ps2if *ps2if = dev_id;
	unsigned int scancode, flag, status;

	status = sa1111_readl(ps2if->base + PS2STAT);
	while (status & PS2STAT_RXF) {
		if (status & PS2STAT_STP)
			sa1111_writel(PS2STAT_STP, ps2if->base + PS2STAT);

		flag = (status & PS2STAT_STP ? SERIO_FRAME : 0) |
		       (status & PS2STAT_RXP ? 0 : SERIO_PARITY);

		scancode = sa1111_readl(ps2if->base + PS2DATA) & 0xff;

		if (hweight8(scancode) & 1)
			flag ^= SERIO_PARITY;

		serio_interrupt(ps2if->io, scancode, flag);

		status = sa1111_readl(ps2if->base + PS2STAT);
        }

        return IRQ_HANDLED;
}

/*
 * Completion of ps2 write
 */
static irqreturn_t ps2_txint(int irq, void *dev_id)
{
	struct ps2if *ps2if = dev_id;
	unsigned int status;

	spin_lock(&ps2if->lock);
	status = sa1111_readl(ps2if->base + PS2STAT);
	if (ps2if->head == ps2if->tail) {
		disable_irq_nosync(irq);
		/* done */
	} else if (status & PS2STAT_TXE) {
		sa1111_writel(ps2if->buf[ps2if->tail], ps2if->base + PS2DATA);
		ps2if->tail = (ps2if->tail + 1) & (sizeof(ps2if->buf) - 1);
	}
	spin_unlock(&ps2if->lock);

	return IRQ_HANDLED;
}

/*
 * Write a byte to the PS2 port.  We have to wait for the
 * port to indicate that the transmitter is empty.
 */
static int ps2_write(struct serio *io, unsigned char val)
{
	struct ps2if *ps2if = io->port_data;
	unsigned long flags;
	unsigned int head;

	spin_lock_irqsave(&ps2if->lock, flags);

	/*
	 * If the TX register is empty, we can go straight out.
	 */
	if (sa1111_readl(ps2if->base + PS2STAT) & PS2STAT_TXE) {
		sa1111_writel(val, ps2if->base + PS2DATA);
	} else {
		if (ps2if->head == ps2if->tail)
			enable_irq(ps2if->dev->irq[1]);
		head = (ps2if->head + 1) & (sizeof(ps2if->buf) - 1);
		if (head != ps2if->tail) {
			ps2if->buf[ps2if->head] = val;
			ps2if->head = head;
		}
	}

	spin_unlock_irqrestore(&ps2if->lock, flags);
	return 0;
}

static int ps2_open(struct serio *io)
{
	struct ps2if *ps2if = io->port_data;
	int ret;

	ret = sa1111_enable_device(ps2if->dev);
	if (ret)
		return ret;

	ret = request_irq(ps2if->dev->irq[0], ps2_rxint, 0,
			  SA1111_DRIVER_NAME(ps2if->dev), ps2if);
	if (ret) {
		printk(KERN_ERR "sa1111ps2: could not allocate IRQ%d: %d\n",
			ps2if->dev->irq[0], ret);
		sa1111_disable_device(ps2if->dev);
		return ret;
	}

	ret = request_irq(ps2if->dev->irq[1], ps2_txint, 0,
			  SA1111_DRIVER_NAME(ps2if->dev), ps2if);
	if (ret) {
		printk(KERN_ERR "sa1111ps2: could not allocate IRQ%d: %d\n",
			ps2if->dev->irq[1], ret);
		free_irq(ps2if->dev->irq[0], ps2if);
		sa1111_disable_device(ps2if->dev);
		return ret;
	}

	ps2if->open = 1;

	enable_irq_wake(ps2if->dev->irq[0]);

	sa1111_writel(PS2CR_ENA, ps2if->base + PS2CR);
	return 0;
}

static void ps2_close(struct serio *io)
{
	struct ps2if *ps2if = io->port_data;

	sa1111_writel(0, ps2if->base + PS2CR);

	disable_irq_wake(ps2if->dev->irq[0]);

	ps2if->open = 0;

	free_irq(ps2if->dev->irq[1], ps2if);
	free_irq(ps2if->dev->irq[0], ps2if);

	sa1111_disable_device(ps2if->dev);
}

/*
 * Clear the input buffer.
 */
static void __devinit ps2_clear_input(struct ps2if *ps2if)
{
	int maxread = 100;

	while (maxread--) {
		if ((sa1111_readl(ps2if->base + PS2DATA) & 0xff) == 0xff)
			break;
	}
}

static unsigned int __devinit ps2_test_one(struct ps2if *ps2if,
					   unsigned int mask)
{
	unsigned int val;

	sa1111_writel(PS2CR_ENA | mask, ps2if->base + PS2CR);

	udelay(2);

	val = sa1111_readl(ps2if->base + PS2STAT);
	return val & (PS2STAT_KBC | PS2STAT_KBD);
}

/*
 * Test the keyboard interface.  We basically check to make sure that
 * we can drive each line to the keyboard independently of each other.
 */
static int __devinit ps2_test(struct ps2if *ps2if)
{
	unsigned int stat;
	int ret = 0;

	stat = ps2_test_one(ps2if, PS2CR_FKC);
	if (stat != PS2STAT_KBD) {
		printk("PS/2 interface test failed[1]: %02x\n", stat);
		ret = -ENODEV;
	}

	stat = ps2_test_one(ps2if, 0);
	if (stat != (PS2STAT_KBC | PS2STAT_KBD)) {
		printk("PS/2 interface test failed[2]: %02x\n", stat);
		ret = -ENODEV;
	}

	stat = ps2_test_one(ps2if, PS2CR_FKD);
	if (stat != PS2STAT_KBC) {
		printk("PS/2 interface test failed[3]: %02x\n", stat);
		ret = -ENODEV;
	}

	sa1111_writel(0, ps2if->base + PS2CR);

	return ret;
}

/*
 * Add one device to this driver.
 */
static int __devinit ps2_probe(struct sa1111_dev *dev)
{
	struct ps2if *ps2if;
	struct serio *serio;
	int ret;

	ps2if = kzalloc(sizeof(struct ps2if), GFP_KERNEL);
	serio = kzalloc(sizeof(struct serio), GFP_KERNEL);
	if (!ps2if || !serio) {
		ret = -ENOMEM;
		goto free;
	}


	serio->id.type		= SERIO_8042;
	serio->write		= ps2_write;
	serio->open		= ps2_open;
	serio->close		= ps2_close;
	strlcpy(serio->name, dev_name(&dev->dev), sizeof(serio->name));
	strlcpy(serio->phys, dev_name(&dev->dev), sizeof(serio->phys));
	serio->port_data	= ps2if;
	serio->dev.parent	= &dev->dev;
	ps2if->io		= serio;
	ps2if->dev		= dev;
	sa1111_set_drvdata(dev, ps2if);

	spin_lock_init(&ps2if->lock);

	/*
	 * Request the physical region for this PS2 port.
	 */
	if (!request_mem_region(dev->res.start,
				dev->res.end - dev->res.start + 1,
				SA1111_DRIVER_NAME(dev))) {
		ret = -EBUSY;
		goto free;
	}

	/*
	 * Our parent device has already mapped the region.
	 */
	ps2if->base = dev->mapbase;

	sa1111_enable_device(ps2if->dev);

	/* Incoming clock is 8MHz */
	sa1111_writel(0, ps2if->base + PS2CLKDIV);
	sa1111_writel(127, ps2if->base + PS2PRECNT);

	/*
	 * Flush any pending input.
	 */
	ps2_clear_input(ps2if);

	/*
	 * Test the keyboard interface.
	 */
	ret = ps2_test(ps2if);
	if (ret)
		goto out;

	/*
	 * Flush any pending input.
	 */
	ps2_clear_input(ps2if);

	sa1111_disable_device(ps2if->dev);
	serio_register_port(ps2if->io);
	return 0;

 out:
	sa1111_disable_device(ps2if->dev);
	release_mem_region(dev->res.start, resource_size(&dev->res));
 free:
	sa1111_set_drvdata(dev, NULL);
	kfree(ps2if);
	kfree(serio);
	return ret;
}

/*
 * Remove one device from this driver.
 */
static int __devexit ps2_remove(struct sa1111_dev *dev)
{
	struct ps2if *ps2if = sa1111_get_drvdata(dev);

	serio_unregister_port(ps2if->io);
	release_mem_region(dev->res.start, resource_size(&dev->res));
	sa1111_set_drvdata(dev, NULL);

	kfree(ps2if);

	return 0;
}

/*
 * Our device driver structure
 */
static struct sa1111_driver ps2_driver = {
	.drv = {
		.name	= "sa1111-ps2",
		.owner	= THIS_MODULE,
	},
	.devid		= SA1111_DEVID_PS2,
	.probe		= ps2_probe,
	.remove		= __devexit_p(ps2_remove),
};

static int __init ps2_init(void)
{
	return sa1111_driver_register(&ps2_driver);
}

static void __exit ps2_exit(void)
{
	sa1111_driver_unregister(&ps2_driver);
}

module_init(ps2_init);
module_exit(ps2_exit);

MODULE_AUTHOR("Russell King <rmk@arm.linux.org.uk>");
MODULE_DESCRIPTION("SA1111 PS2 controller driver");
MODULE_LICENSE("GPL");
