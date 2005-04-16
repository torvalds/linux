/*
** hppb.c:
**      HP-PB bus driver for the NOVA and K-Class systems.
**
**      (c) Copyright 2002 Ryan Bradetich
**      (c) Copyright 2002 Hewlett-Packard Company
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This Driver currently only supports the console (port 0) on the MUX.
** Additional work will be needed on this driver to enable the full
** functionality of the MUX.
**
*/

#include <linux/types.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/ioport.h>

#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/parisc-device.h>

#include <linux/pci.h>

struct hppb_card {
	unsigned long hpa;
	struct resource mmio_region;
	struct hppb_card *next;
};

struct hppb_card hppb_card_head = {
	.hpa = 0,
	.next = NULL,
};

#define IO_IO_LOW  offsetof(struct bc_module, io_io_low)
#define IO_IO_HIGH offsetof(struct bc_module, io_io_high)

/**
 * hppb_probe - Determine if the hppb driver should claim this device.
 * @dev: The device which has been found
 *
 * Determine if hppb driver should claim this chip (return 0) or not 
 * (return 1). If so, initialize the chip and tell other partners in crime 
 * they have work to do.
 */
static int hppb_probe(struct parisc_device *dev)
{
	int status;
	struct hppb_card *card = &hppb_card_head;

	while(card->next) {
		card = card->next;
	}

	if(card->hpa) {
		card->next = kmalloc(sizeof(struct hppb_card), GFP_KERNEL);
		if(!card->next) {
			printk(KERN_ERR "HP-PB: Unable to allocate memory.\n");
			return 1;
		}
		memset(card->next, '\0', sizeof(struct hppb_card));
		card = card->next;
	}
        printk(KERN_INFO "Found GeckoBoa at 0x%lx\n", dev->hpa);

	card->hpa = dev->hpa;
	card->mmio_region.name = "HP-PB Bus";
	card->mmio_region.flags = IORESOURCE_MEM;

	card->mmio_region.start = __raw_readl(dev->hpa + IO_IO_LOW);
	card->mmio_region.end = __raw_readl(dev->hpa + IO_IO_HIGH) - 1;

	status = ccio_request_resource(dev, &card->mmio_region);
	if(status < 0) {
		printk(KERN_ERR "%s: failed to claim HP-PB bus space (%08lx, %08lx)\n",
			__FILE__, card->mmio_region.start, card->mmio_region.end);
	}

        return 0;
}


static struct parisc_device_id hppb_tbl[] = {
        { HPHW_BCPORT, HVERSION_REV_ANY_ID, 0x500, 0xc }, 
        { 0, }
};

static struct parisc_driver hppb_driver = {
        .name =         "Gecko Boa",
        .id_table =     hppb_tbl,
	.probe =        hppb_probe,
};

/**
 * hppb_init - HP-PB bus initalization procedure.
 *
 * Register this driver.   
 */
void __init hppb_init(void)
{
        register_parisc_driver(&hppb_driver);
}
