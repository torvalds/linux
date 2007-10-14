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

#include <asm/prom.h>
#include <asm/of_device.h>
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

static void __auxio_sbus_set(u8 bits_on, u8 bits_off)
{
	if (auxio_register) {
		unsigned char regval;
		unsigned long flags;
		unsigned char newval;

		spin_lock_irqsave(&auxio_lock, flags);

		regval =  sbus_readb(auxio_register);
		newval =  regval | bits_on;
		newval &= ~bits_off;
		newval &= ~AUXIO_AUX1_MASK;
		sbus_writeb(newval, auxio_register);
		
		spin_unlock_irqrestore(&auxio_lock, flags);
	}
}

static void __auxio_ebus_set(u8 bits_on, u8 bits_off)
{
	if (auxio_register) {
		unsigned char regval;
		unsigned long flags;
		unsigned char newval;

		spin_lock_irqsave(&auxio_lock, flags);

		regval =  (u8)readl(auxio_register);
		newval =  regval | bits_on;
		newval &= ~bits_off;
		writel((u32)newval, auxio_register);

		spin_unlock_irqrestore(&auxio_lock, flags);
	}
}

static inline void __auxio_ebus_set_led(int on)
{
	(on) ? __auxio_ebus_set(AUXIO_PCIO_LED, 0) :
		__auxio_ebus_set(0, AUXIO_PCIO_LED) ;
}

static inline void __auxio_sbus_set_led(int on)
{
	(on) ? __auxio_sbus_set(AUXIO_AUX1_LED, 0) :
		__auxio_sbus_set(0, AUXIO_AUX1_LED) ;
}

void auxio_set_led(int on)
{
	switch(auxio_devtype) {
	case AUXIO_TYPE_SBUS:
		__auxio_sbus_set_led(on);
		break;
	case AUXIO_TYPE_EBUS:
		__auxio_ebus_set_led(on);
		break;
	default:
		break;
	}
}

static inline void __auxio_sbus_set_lte(int on)
{
	(on) ? __auxio_sbus_set(AUXIO_AUX1_LTE, 0) : 
		__auxio_sbus_set(0, AUXIO_AUX1_LTE) ;
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

static struct of_device_id auxio_match[] = {
	{
		.name = "auxio",
	},
	{},
};

MODULE_DEVICE_TABLE(of, auxio_match);

static int __devinit auxio_probe(struct of_device *dev, const struct of_device_id *match)
{
	struct device_node *dp = dev->node;
	unsigned long size;

	if (!strcmp(dp->parent->name, "ebus")) {
		auxio_devtype = AUXIO_TYPE_EBUS;
		size = sizeof(u32);
	} else if (!strcmp(dp->parent->name, "sbus")) {
		auxio_devtype = AUXIO_TYPE_SBUS;
		size = 1;
	} else {
		printk("auxio: Unknown parent bus type [%s]\n",
		       dp->parent->name);
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

static struct of_platform_driver auxio_driver = {
	.match_table	= auxio_match,
	.probe		= auxio_probe,
	.driver		= {
		.name	= "auxio",
	},
};

static int __init auxio_init(void)
{
	return of_register_driver(&auxio_driver, &of_platform_bus_type);
}

/* Must be after subsys_initcall() so that busses are probed.  Must
 * be before device_initcall() because things like the floppy driver
 * need to use the AUXIO register.
 */
fs_initcall(auxio_init);
