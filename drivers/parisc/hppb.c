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
*/

#include <linux/types.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/ioport.h>

#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/parisc-device.h>

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
		card->next = kzalloc(sizeof(struct hppb_card), GFP_KERNEL);
		if(!card->next) {
			printk(KERN_ERR "HP-PB: Unable to allocate memory.\n");
			return 1;
		}
		card = card->next;
	}
        printk(KERN_INFO "Found GeckoBoa at 0x%x\n", dev->hpa.start);

	card->hpa = dev->hpa.start;
	card->mmio_region.name = "HP-PB Bus";
	card->mmio_region.flags = IORESOURCE_MEM;

	card->mmio_region.start = gsc_readl(dev->hpa.start + IO_IO_LOW);
	card->mmio_region.end = gsc_readl(dev->hpa.start + IO_IO_HIGH) - 1;

	status = ccio_request_resource(dev, &card->mmio_region);
	if(status < 0) {
		printk(KERN_ERR "%s: failed to claim HP-PB bus space (%08x, %08x)\n",
			__FILE__, card->mmio_region.start, card->mmio_region.end);
	}

        return 0;
}

static struct parisc_device_id hppb_tbl[] = {
        { HPHW_BCPORT, HVERSION_REV_ANY_ID, 0x500, 0xc }, /* E25 and K */
        { HPHW_BCPORT, 0x0, 0x501, 0xc }, /* E35 */
        { HPHW_BCPORT, 0x0, 0x502, 0xc }, /* E45 */
        { HPHW_BCPORT, 0x0, 0x503, 0xc }, /* E55 */
        { 0, }
};

static struct parisc_driver hppb_driver = {
        .name =         "gecko_boa",
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
