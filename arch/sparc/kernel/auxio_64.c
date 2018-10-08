// SPDX-License-Identifier: GPL-2.0
/* auxio.c: Probing for the Sparc AUXIO register at boot time.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 *
 * Refactoring for unified NCR/PCIO support 2002 Eric Brower (ebrower@usa.net)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/of_device.h>

#include <asm/prom.h>
#include <asm/io.h>
#include <asm/auxio.h>

void __iomem *auxio_register = NULL;
EXPORT_SYMBOL(auxio_register);

enum auxio_type {
	AUXIO_TYPE_NODEV,
	AUXIO_TYPE_SBUS,
	AUXIO_TYPE_EBUS
};

static enum auxio_type auxio_devtype = AUXIO_TYPE_NODEV;
static DEFINE_SPINLOCK(auxio_lock);

static void __auxio_rmw(u8 bits_on, u8 bits_off, int ebus)
{
	if (auxio_register) {
		unsigned long flags;
		u8 regval, newval;

		spin_lock_irqsave(&auxio_lock, flags);

		regval = (ebus ?
			  (u8) readl(auxio_register) :
			  sbus_readb(auxio_register));
		newval =  regval | bits_on;
		newval &= ~bits_off;
		if (!ebus)
			newval &= ~AUXIO_AUX1_MASK;
		if (ebus)
			writel((u32) newval, auxio_register);
		else
			sbus_writeb(newval, auxio_register);
		
		spin_unlock_irqrestore(&auxio_lock, flags);
	}
}

static void __auxio_set_bit(u8 bit, int on, int ebus)
{
	u8 bits_on = (ebus ? AUXIO_PCIO_LED : AUXIO_AUX1_LED);
	u8 bits_off = 0;

	if (!on) {
		u8 tmp = bits_off;
		bits_off = bits_on;
		bits_on = tmp;
	}
	__auxio_rmw(bits_on, bits_off, ebus);
}

void auxio_set_led(int on)
{
	int ebus = auxio_devtype == AUXIO_TYPE_EBUS;
	u8 bit;

	bit = (ebus ? AUXIO_PCIO_LED : AUXIO_AUX1_LED);
	__auxio_set_bit(bit, on, ebus);
}
EXPORT_SYMBOL(auxio_set_led);

static void __auxio_sbus_set_lte(int on)
{
	__auxio_set_bit(AUXIO_AUX1_LTE, on, 0);
}

void auxio_set_lte(int on)
{
	switch(auxio_devtype) {
	case AUXIO_TYPE_SBUS:
		__auxio_sbus_set_lte(on);
		break;
	case AUXIO_TYPE_EBUS:
		/* FALL-THROUGH */
	default:
		break;
	}
}
EXPORT_SYMBOL(auxio_set_lte);

static const struct of_device_id auxio_match[] = {
	{
		.name = "auxio",
	},
	{},
};

MODULE_DEVICE_TABLE(of, auxio_match);

static int auxio_probe(struct platform_device *dev)
{
	struct device_node *dp = dev->dev.of_node;
	unsigned long size;

	if (!strcmp(dp->parent->name, "ebus")) {
		auxio_devtype = AUXIO_TYPE_EBUS;
		size = sizeof(u32);
	} else if (!strcmp(dp->parent->name, "sbus")) {
		auxio_devtype = AUXIO_TYPE_SBUS;
		size = 1;
	} else {
		printk("auxio: Unknown parent bus type [%pOFn]\n",
		       dp->parent);
		return -ENODEV;
	}
	auxio_register = of_ioremap(&dev->resource[0], 0, size, "auxio");
	if (!auxio_register)
		return -ENODEV;

	printk(KERN_INFO "AUXIO: Found device at %s\n",
	       dp->full_name);

	if (auxio_devtype == AUXIO_TYPE_EBUS)
		auxio_set_led(AUXIO_LED_ON);

	return 0;
}

static struct platform_driver auxio_driver = {
	.probe		= auxio_probe,
	.driver = {
		.name = "auxio",
		.of_match_table = auxio_match,
	},
};

static int __init auxio_init(void)
{
	return platform_driver_register(&auxio_driver);
}

/* Must be after subsys_initcall() so that busses are probed.  Must
 * be before device_initcall() because things like the floppy driver
 * need to use the AUXIO register.
 */
fs_initcall(auxio_init);
