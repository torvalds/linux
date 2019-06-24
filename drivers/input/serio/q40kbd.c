// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 *
 *  Based on the work of:
 *	Richard Zidlicky <Richard.Zidlicky@stud.informatik.uni-erlangen.de>
 */

/*
 * Q40 PS/2 keyboard controller driver for Linux/m68k
 */

/*
 */

#include <linux/module.h>
#include <linux/serio.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/bitops.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <asm/io.h>
#include <linux/uaccess.h>
#include <asm/q40_master.h>
#include <asm/irq.h>
#include <asm/q40ints.h>

#define DRV_NAME	"q40kbd"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Q40 PS/2 keyboard controller driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);

struct q40kbd {
	struct serio *port;
	spinlock_t lock;
};

static irqreturn_t q40kbd_interrupt(int irq, void *dev_id)
{
	struct q40kbd *q40kbd = dev_id;
	unsigned long flags;

	spin_lock_irqsave(&q40kbd->lock, flags);

	if (Q40_IRQ_KEYB_MASK & master_inb(INTERRUPT_REG))
		serio_interrupt(q40kbd->port, master_inb(KEYCODE_REG), 0);

	master_outb(-1, KEYBOARD_UNLOCK_REG);

	spin_unlock_irqrestore(&q40kbd->lock, flags);

	return IRQ_HANDLED;
}

/*
 * q40kbd_flush() flushes all data that may be in the keyboard buffers
 */

static void q40kbd_flush(struct q40kbd *q40kbd)
{
	int maxread = 100;
	unsigned long flags;

	spin_lock_irqsave(&q40kbd->lock, flags);

	while (maxread-- && (Q40_IRQ_KEYB_MASK & master_inb(INTERRUPT_REG)))
		master_inb(KEYCODE_REG);

	spin_unlock_irqrestore(&q40kbd->lock, flags);
}

static void q40kbd_stop(void)
{
	master_outb(0, KEY_IRQ_ENABLE_REG);
	master_outb(-1, KEYBOARD_UNLOCK_REG);
}

/*
 * q40kbd_open() is called when a port is open by the higher layer.
 * It allocates the interrupt and enables in in the chip.
 */

static int q40kbd_open(struct serio *port)
{
	struct q40kbd *q40kbd = port->port_data;

	q40kbd_flush(q40kbd);

	/* off we go */
	master_outb(-1, KEYBOARD_UNLOCK_REG);
	master_outb(1, KEY_IRQ_ENABLE_REG);

	return 0;
}

static void q40kbd_close(struct serio *port)
{
	struct q40kbd *q40kbd = port->port_data;

	q40kbd_stop();
	q40kbd_flush(q40kbd);
}

static int q40kbd_probe(struct platform_device *pdev)
{
	struct q40kbd *q40kbd;
	struct serio *port;
	int error;

	q40kbd = kzalloc(sizeof(struct q40kbd), GFP_KERNEL);
	port = kzalloc(sizeof(struct serio), GFP_KERNEL);
	if (!q40kbd || !port) {
		error = -ENOMEM;
		goto err_free_mem;
	}

	q40kbd->port = port;
	spin_lock_init(&q40kbd->lock);

	port->id.type = SERIO_8042;
	port->open = q40kbd_open;
	port->close = q40kbd_close;
	port->port_data = q40kbd;
	port->dev.parent = &pdev->dev;
	strlcpy(port->name, "Q40 Kbd Port", sizeof(port->name));
	strlcpy(port->phys, "Q40", sizeof(port->phys));

	q40kbd_stop();

	error = request_irq(Q40_IRQ_KEYBOARD, q40kbd_interrupt, 0,
			    DRV_NAME, q40kbd);
	if (error) {
		dev_err(&pdev->dev, "Can't get irq %d.\n", Q40_IRQ_KEYBOARD);
		goto err_free_mem;
	}

	serio_register_port(q40kbd->port);

	platform_set_drvdata(pdev, q40kbd);
	printk(KERN_INFO "serio: Q40 kbd registered\n");

	return 0;

err_free_mem:
	kfree(port);
	kfree(q40kbd);
	return error;
}

static int q40kbd_remove(struct platform_device *pdev)
{
	struct q40kbd *q40kbd = platform_get_drvdata(pdev);

	/*
	 * q40kbd_close() will be called as part of unregistering
	 * and will ensure that IRQ is turned off, so it is safe
	 * to unregister port first and free IRQ later.
	 */
	serio_unregister_port(q40kbd->port);
	free_irq(Q40_IRQ_KEYBOARD, q40kbd);
	kfree(q40kbd);

	return 0;
}

static struct platform_driver q40kbd_driver = {
	.driver		= {
		.name	= "q40kbd",
	},
	.remove		= q40kbd_remove,
};

module_platform_driver_probe(q40kbd_driver, q40kbd_probe);
