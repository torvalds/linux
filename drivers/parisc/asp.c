/*
 *	ASP Device Driver
 *
 *	(c) Copyright 2000 The Puffin Group Inc.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *	by Helge Deller <deller@gmx.de>
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <asm/io.h>
#include <asm/led.h>

#include "gsc.h"

#define ASP_GSC_IRQ	3		/* hardcoded interrupt for GSC */

#define ASP_VER_OFFSET 	0x20		/* offset of ASP version */

#define ASP_LED_ADDR	0xf0800020

#define VIPER_INT_WORD  0xFFFBF088      /* addr of viper interrupt word */

static struct gsc_asic asp;

static void asp_choose_irq(struct parisc_device *dev, void *ctrl)
{
	int irq;

	switch (dev->id.sversion) {
	case 0x71:	irq =  9; break; /* SCSI */
	case 0x72:	irq =  8; break; /* LAN */
	case 0x73:	irq =  1; break; /* HIL */
	case 0x74:	irq =  7; break; /* Centronics */
	case 0x75:	irq = (dev->hw_path == 4) ? 5 : 6; break; /* RS232 */
	case 0x76:	irq = 10; break; /* EISA BA */
	case 0x77:	irq = 11; break; /* Graphics1 */
	case 0x7a:	irq = 13; break; /* Audio (Bushmaster) */
	case 0x7b:	irq = 13; break; /* Audio (Scorpio) */
	case 0x7c:	irq =  3; break; /* FW SCSI */
	case 0x7d:	irq =  4; break; /* FDDI */
	case 0x7f:	irq = 13; break; /* Audio (Outfield) */
	default:	return;		 /* Unknown */
	}

	gsc_asic_assign_irq(ctrl, irq, &dev->irq);

	switch (dev->id.sversion) {
	case 0x73:	irq =  2; break; /* i8042 High-priority */
	case 0x76:	irq =  0; break; /* EISA BA */
	default:	return;		 /* Other */
	}

	gsc_asic_assign_irq(ctrl, irq, &dev->aux_irq);
}

/* There are two register ranges we're interested in.  Interrupt /
 * Status / LED are at 0xf080xxxx and Asp special registers are at
 * 0xf082fxxx.  PDC only tells us that Asp is at 0xf082f000, so for
 * the purposes of interrupt handling, we have to tell other bits of
 * the kernel to look at the other registers.
 */
#define ASP_INTERRUPT_ADDR 0xf0800000

int __init
asp_init_chip(struct parisc_device *dev)
{
	struct gsc_irq gsc_irq;
	int ret;

	asp.version = gsc_readb(dev->hpa.start + ASP_VER_OFFSET) & 0xf;
	asp.name = (asp.version == 1) ? "Asp" : "Cutoff";
	asp.hpa = ASP_INTERRUPT_ADDR;

	printk(KERN_INFO "%s version %d at 0x%lx found.\n", 
		asp.name, asp.version, dev->hpa.start);

	/* the IRQ ASP should use */
	ret = -EBUSY;
	dev->irq = gsc_claim_irq(&gsc_irq, ASP_GSC_IRQ);
	if (dev->irq < 0) {
		printk(KERN_ERR "%s(): cannot get GSC irq\n", __func__);
		goto out;
	}

	asp.eim = ((u32) gsc_irq.txn_addr) | gsc_irq.txn_data;

	ret = request_irq(gsc_irq.irq, gsc_asic_intr, 0, "asp", &asp);
	if (ret < 0)
		goto out;

	/* Program VIPER to interrupt on the ASP irq */
	gsc_writel((1 << (31 - ASP_GSC_IRQ)),VIPER_INT_WORD);

	/* Done init'ing, register this driver */
	ret = gsc_common_setup(dev, &asp);
	if (ret)
		goto out;

	gsc_fixup_irqs(dev, &asp, asp_choose_irq);
	/* Mongoose is a sibling of Asp, not a child... */
	gsc_fixup_irqs(parisc_parent(dev), &asp, asp_choose_irq);

	/* initialize the chassis LEDs */ 
#ifdef CONFIG_CHASSIS_LCD_LED	
	register_led_driver(DISPLAY_MODEL_OLD_ASP, LED_CMD_REG_NONE, 
		    ASP_LED_ADDR);
#endif

 out:
	return ret;
}

static struct parisc_device_id asp_tbl[] = {
	{ HPHW_BA, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x00070 },
	{ 0, }
};

struct parisc_driver asp_driver = {
	.name =		"asp",
	.id_table =	asp_tbl,
	.probe =	asp_init_chip,
};
