/*
 *	LASI Device Driver
 *
 *	(c) Copyright 1999 Red Hat Software
 *	Portions (c) Copyright 1999 The Puffin Group Inc.
 *	Portions (c) Copyright 1999 Hewlett-Packard
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *	by Alan Cox <alan@redhat.com> and 
 * 	   Alex deVries <alex@onefishtwo.ca>
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/types.h>

#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/led.h>

#include "gsc.h"


#define LASI_VER	0xC008	/* LASI Version */

#define LASI_IO_CONF	0x7FFFE	/* LASI primary configuration register */
#define LASI_IO_CONF2	0x7FFFF	/* LASI secondary configuration register */

static void lasi_choose_irq(struct parisc_device *dev, void *ctrl)
{
	int irq;

	switch (dev->id.sversion) {
		case 0x74:	irq =  7; break; /* Centronics */
		case 0x7B:	irq = 13; break; /* Audio */
		case 0x81:	irq = 14; break; /* Lasi itself */
		case 0x82:	irq =  9; break; /* SCSI */
		case 0x83:	irq = 20; break; /* Floppy */
		case 0x84:	irq = 26; break; /* PS/2 Keyboard */
		case 0x87:	irq = 18; break; /* ISDN */
		case 0x8A:	irq =  8; break; /* LAN */
		case 0x8C:	irq =  5; break; /* RS232 */
		case 0x8D:	irq = (dev->hw_path == 13) ? 16 : 17; break;
						 /* Telephone */
		default: 	return;		 /* unknown */
	}

	gsc_asic_assign_irq(ctrl, irq, &dev->irq);
}

static void __init
lasi_init_irq(struct gsc_asic *this_lasi)
{
	unsigned long lasi_base = this_lasi->hpa;

	/* Stop LASI barking for a bit */
	gsc_writel(0x00000000, lasi_base+OFFSET_IMR);

	/* clear pending interrupts */
	gsc_readl(lasi_base+OFFSET_IRR);

	/* We're not really convinced we want to reset the onboard
         * devices. Firmware does it for us...
	 */

	/* Resets */
	/* gsc_writel(0xFFFFFFFF, lasi_base+0x2000);*/	/* Parallel */
	if(pdc_add_valid(lasi_base+0x4004) == PDC_OK)
		gsc_writel(0xFFFFFFFF, lasi_base+0x4004);	/* Audio */
	/* gsc_writel(0xFFFFFFFF, lasi_base+0x5000);*/	/* Serial */ 
	/* gsc_writel(0xFFFFFFFF, lasi_base+0x6000);*/	/* SCSI */
	gsc_writel(0xFFFFFFFF, lasi_base+0x7000);	/* LAN */
	gsc_writel(0xFFFFFFFF, lasi_base+0x8000);	/* Keyboard */
	gsc_writel(0xFFFFFFFF, lasi_base+0xA000);	/* FDC */
	
	/* Ok we hit it on the head with a hammer, our Dog is now
	** comatose and muzzled.  Devices will now unmask LASI
	** interrupts as they are registered as irq's in the LASI range.
	*/
	/* XXX: I thought it was `awks that got `it on the `ead with an
	 * `ammer.  -- willy
	 */
}


/*
   ** lasi_led_init()
   ** 
   ** lasi_led_init() initializes the LED controller on the LASI.
   **
   ** Since Mirage and Electra machines use a different LED
   ** address register, we need to check for these machines 
   ** explicitly.
 */

#ifndef CONFIG_CHASSIS_LCD_LED

#define lasi_led_init(x)	/* nothing */

#else

void __init lasi_led_init(unsigned long lasi_hpa)
{
	unsigned long datareg;

	switch (CPU_HVERSION) {
	/* Gecko machines have only one single LED, which can be permanently 
	   turned on by writing a zero into the power control register. */ 
	case 0x600:		/* Gecko (712/60) */
	case 0x601:		/* Gecko (712/80) */
	case 0x602:		/* Gecko (712/100) */
	case 0x603:		/* Anole 64 (743/64) */
	case 0x604:		/* Anole 100 (743/100) */
	case 0x605:		/* Gecko (712/120) */
		datareg = lasi_hpa + 0x0000C000;
		gsc_writeb(0, datareg);
		return; /* no need to register the LED interrupt-function */  

	/* Mirage and Electra machines need special offsets */
	case 0x60A:		/* Mirage Jr (715/64) */
	case 0x60B:		/* Mirage 100 */
	case 0x60C:		/* Mirage 100+ */
	case 0x60D:		/* Electra 100 */
	case 0x60E:		/* Electra 120 */
		datareg = lasi_hpa - 0x00020000;
		break;

	default:
		datareg = lasi_hpa + 0x0000C000;
		break;
	}

	register_led_driver(DISPLAY_MODEL_LASI, LED_CMD_REG_NONE, datareg);
}
#endif

/*
 * lasi_power_off
 *
 * Function for lasi to turn off the power.  This is accomplished by setting a
 * 1 to PWR_ON_L in the Power Control Register
 * 
 */

static unsigned long lasi_power_off_hpa __read_mostly;

static void lasi_power_off(void)
{
	unsigned long datareg;

	/* calculate addr of the Power Control Register */
	datareg = lasi_power_off_hpa + 0x0000C000;

	/* Power down the machine */
	gsc_writel(0x02, datareg);
}

int __init
lasi_init_chip(struct parisc_device *dev)
{
	extern void (*chassis_power_off)(void);
	struct gsc_asic *lasi;
	struct gsc_irq gsc_irq;
	int ret;

	lasi = kzalloc(sizeof(*lasi), GFP_KERNEL);
	if (!lasi)
		return -ENOMEM;

	lasi->name = "Lasi";
	lasi->hpa = dev->hpa.start;

	/* Check the 4-bit (yes, only 4) version register */
	lasi->version = gsc_readl(lasi->hpa + LASI_VER) & 0xf;
	printk(KERN_INFO "%s version %d at 0x%lx found.\n",
		lasi->name, lasi->version, lasi->hpa);

	/* initialize the chassis LEDs really early */ 
	lasi_led_init(lasi->hpa);

	/* Stop LASI barking for a bit */
	lasi_init_irq(lasi);

	/* the IRQ lasi should use */
	dev->irq = gsc_alloc_irq(&gsc_irq);
	if (dev->irq < 0) {
		printk(KERN_ERR "%s(): cannot get GSC irq\n",
				__func__);
		kfree(lasi);
		return -EBUSY;
	}

	lasi->eim = ((u32) gsc_irq.txn_addr) | gsc_irq.txn_data;

	ret = request_irq(gsc_irq.irq, gsc_asic_intr, 0, "lasi", lasi);
	if (ret < 0) {
		kfree(lasi);
		return ret;
	}

	/* enable IRQ's for devices below LASI */
	gsc_writel(lasi->eim, lasi->hpa + OFFSET_IAR);

	/* Done init'ing, register this driver */
	ret = gsc_common_setup(dev, lasi);
	if (ret) {
		kfree(lasi);
		return ret;
	}    

	gsc_fixup_irqs(dev, lasi, lasi_choose_irq);

	/* initialize the power off function */
	/* FIXME: Record the LASI HPA for the power off function.  This should
	 * ensure that only the first LASI (the one controlling the power off)
	 * should set the HPA here */
	lasi_power_off_hpa = lasi->hpa;
	chassis_power_off = lasi_power_off;
	
	return ret;
}

static struct parisc_device_id lasi_tbl[] = {
	{ HPHW_BA, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x00081 },
	{ 0, }
};

struct parisc_driver lasi_driver = {
	.name =		"lasi",
	.id_table =	lasi_tbl,
	.probe =	lasi_init_chip,
};
