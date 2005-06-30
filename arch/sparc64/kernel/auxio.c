/* auxio.c: Probing for the Sparc AUXIO register at boot time.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 *
 * Refactoring for unified NCR/PCIO support 2002 Eric Brower (ebrower@usa.net)
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ioport.h>

#include <asm/oplib.h>
#include <asm/io.h>
#include <asm/sbus.h>
#include <asm/ebus.h>
#include <asm/auxio.h>

/* This cannot be static, as it is referenced in irq.c */
void __iomem *auxio_register = NULL;

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

void __init auxio_probe(void)
{
        struct sbus_bus *sbus;
        struct sbus_dev *sdev = NULL;

        for_each_sbus(sbus) {
                for_each_sbusdev(sdev, sbus) {
                        if(!strcmp(sdev->prom_name, "auxio"))
				goto found_sdev;
                }
        }

found_sdev:
	if (sdev) {
		auxio_devtype  = AUXIO_TYPE_SBUS;
		auxio_register = sbus_ioremap(&sdev->resource[0], 0,
					      sdev->reg_addrs[0].reg_size,
					      "auxiliaryIO");
	}
#ifdef CONFIG_PCI
	else {
		struct linux_ebus *ebus;
		struct linux_ebus_device *edev = NULL;

		for_each_ebus(ebus) {
			for_each_ebusdev(edev, ebus) {
				if (!strcmp(edev->prom_name, "auxio"))
					goto ebus_done;
			}
		}
	ebus_done:
		if (edev) {
			auxio_devtype  = AUXIO_TYPE_EBUS;
			auxio_register =
				ioremap(edev->resource[0].start, sizeof(u32));
		}
	}
	auxio_set_led(AUXIO_LED_ON);
#endif
}
