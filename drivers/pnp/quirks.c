/*
 *  This file contains quirk handling code for PnP devices
 *  Some devices do not report all their resources, and need to have extra
 *  resources added. This is most easily accomplished at initialisation time
 *  when building up the resource structure for the first time.
 *
 *  Copyright (c) 2000 Peter Denison <peterd@pnd-pc.demon.co.uk>
 *
 *  Heavily based on PCI quirks handling which is
 *
 *  Copyright (c) 1999 Martin Mares <mj@ucw.cz>
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/pnp.h>
#include <linux/io.h>
#include "base.h"


static void quirk_awe32_resources(struct pnp_dev *dev)
{
	struct pnp_port *port, *port2, *port3;
	struct pnp_option *res = dev->dependent;

	/*
	 * Unfortunately the isapnp_add_port_resource is too tightly bound
	 * into the PnP discovery sequence, and cannot be used. Link in the
	 * two extra ports (at offset 0x400 and 0x800 from the one given) by
	 * hand.
	 */
	for ( ; res ; res = res->next ) {
		port2 = pnp_alloc(sizeof(struct pnp_port));
		if (!port2)
			return;
		port3 = pnp_alloc(sizeof(struct pnp_port));
		if (!port3) {
			kfree(port2);
			return;
		}
		port = res->port;
		memcpy(port2, port, sizeof(struct pnp_port));
		memcpy(port3, port, sizeof(struct pnp_port));
		port->next = port2;
		port2->next = port3;
		port2->min += 0x400;
		port2->max += 0x400;
		port3->min += 0x800;
		port3->max += 0x800;
	}
	printk(KERN_INFO "pnp: AWE32 quirk - adding two ports\n");
}

static void quirk_cmi8330_resources(struct pnp_dev *dev)
{
	struct pnp_option *res = dev->dependent;
	unsigned long tmp;

	for ( ; res ; res = res->next ) {

		struct pnp_irq *irq;
		struct pnp_dma *dma;

		for( irq = res->irq; irq; irq = irq->next ) {	// Valid irqs are 5, 7, 10
			tmp = 0x04A0;
			bitmap_copy(irq->map, &tmp, 16);	// 0000 0100 1010 0000
		}

		for( dma = res->dma; dma; dma = dma->next ) // Valid 8bit dma channels are 1,3
			if( ( dma->flags & IORESOURCE_DMA_TYPE_MASK ) == IORESOURCE_DMA_8BIT )
				dma->map = 0x000A;
	}
	printk(KERN_INFO "pnp: CMI8330 quirk - fixing interrupts and dma\n");
}

static void quirk_sb16audio_resources(struct pnp_dev *dev)
{
	struct pnp_port *port;
	struct pnp_option *res = dev->dependent;
	int    changed = 0;

	/*
	 * The default range on the mpu port for these devices is 0x388-0x388.
	 * Here we increase that range so that two such cards can be
	 * auto-configured.
	 */

	for( ; res ; res = res->next ) {
		port = res->port;
		if(!port)
			continue;
		port = port->next;
		if(!port)
			continue;
		port = port->next;
		if(!port)
			continue;
		if(port->min != port->max)
			continue;
		port->max += 0x70;
		changed = 1;
	}
	if(changed)
		printk(KERN_INFO "pnp: SB audio device quirk - increasing port range\n");
	return;
}

static int quirk_smc_fir_enabled(struct pnp_dev *dev)
{
	unsigned long firbase;
	u8 bank, high, low, chip;

	if (!pnp_port_valid(dev, 1))
		return 0;

	firbase = pnp_port_start(dev, 1);

	/* Select register bank 3 */
	bank = inb(firbase + 7);
	bank &= 0xf0;
	bank |= 3;
	outb(bank, firbase + 7);

	high = inb(firbase + 0);
	low  = inb(firbase + 1);
	chip = inb(firbase + 2);

	/* This corresponds to the check in smsc_ircc_present() */
	if (high == 0x10 && low == 0xb8 && (chip == 0xf1 || chip == 0xf2))
		return 1;

	return 0;
}

static void quirk_smc_enable(struct pnp_dev *dev)
{
	struct resource fir, sir, irq;

	pnp_activate_dev(dev);
	if (quirk_smc_fir_enabled(dev))
		return;

	/*
	 * Sometimes the BIOS claims the device is enabled, but it reports
	 * the wrong FIR resources or doesn't properly configure ISA or LPC
	 * bridges on the way to the device.
	 *
	 * HP nc6000 and nc8000/nw8000 laptops have known problems like
	 * this.  Fortunately, they do fix things up if we auto-configure
	 * the device using its _PRS and _SRS methods.
	 */
	dev_err(&dev->dev, "%s not responding at SIR 0x%lx, FIR 0x%lx; "
		"auto-configuring\n", dev->id->id,
		(unsigned long) pnp_port_start(dev, 0),
		(unsigned long) pnp_port_start(dev, 1));

	pnp_disable_dev(dev);
	pnp_init_resource_table(&dev->res);
	pnp_auto_config_dev(dev);
	pnp_activate_dev(dev);
	if (quirk_smc_fir_enabled(dev)) {
		dev_err(&dev->dev, "responds at SIR 0x%lx, FIR 0x%lx\n",
			(unsigned long) pnp_port_start(dev, 0),
			(unsigned long) pnp_port_start(dev, 1));
		return;
	}

	/*
	 * The Toshiba Portege 4000 _CRS reports the FIR region first,
	 * followed by the SIR region.  The BIOS will configure the bridge,
	 * but only if we call _SRS with SIR first, then FIR.  It also
	 * reports the IRQ as active high, when it is really active low.
	 */
	dev_err(&dev->dev, "not responding at SIR 0x%lx, FIR 0x%lx; "
		"swapping SIR/FIR and reconfiguring\n",
		(unsigned long) pnp_port_start(dev, 0),
		(unsigned long) pnp_port_start(dev, 1));

	/*
	 * Clear IORESOURCE_AUTO so pnp_activate_dev() doesn't reassign
	 * these resources any more.
	 */
	fir = dev->res.port_resource[0];
	sir = dev->res.port_resource[1];
	fir.flags &= ~IORESOURCE_AUTO;
	sir.flags &= ~IORESOURCE_AUTO;

	irq = dev->res.irq_resource[0];
	irq.flags &= ~IORESOURCE_AUTO;
	irq.flags &= ~IORESOURCE_BITS;
	irq.flags |= IORESOURCE_IRQ_LOWEDGE;

	pnp_disable_dev(dev);
	dev->res.port_resource[0] = sir;
	dev->res.port_resource[1] = fir;
	dev->res.irq_resource[0] = irq;
	pnp_activate_dev(dev);

	if (quirk_smc_fir_enabled(dev)) {
		dev_err(&dev->dev, "responds at SIR 0x%lx, FIR 0x%lx\n",
			(unsigned long) pnp_port_start(dev, 0),
			(unsigned long) pnp_port_start(dev, 1));
		return;
	}

	dev_err(&dev->dev, "giving up; try \"smsc-ircc2.nopnp\" and "
		"email bjorn.helgaas@hp.com\n");
}


/*
 *  PnP Quirks
 *  Cards or devices that need some tweaking due to incomplete resource info
 */

static struct pnp_fixup pnp_fixups[] = {
	/* Soundblaster awe io port quirk */
	{ "CTL0021", quirk_awe32_resources },
	{ "CTL0022", quirk_awe32_resources },
	{ "CTL0023", quirk_awe32_resources },
	/* CMI 8330 interrupt and dma fix */
	{ "@X@0001", quirk_cmi8330_resources },
	/* Soundblaster audio device io port range quirk */
	{ "CTL0001", quirk_sb16audio_resources },
	{ "CTL0031", quirk_sb16audio_resources },
	{ "CTL0041", quirk_sb16audio_resources },
	{ "CTL0042", quirk_sb16audio_resources },
	{ "CTL0043", quirk_sb16audio_resources },
	{ "CTL0044", quirk_sb16audio_resources },
	{ "CTL0045", quirk_sb16audio_resources },
	{ "SMCf010", quirk_smc_enable },
	{ "" }
};

void pnp_fixup_device(struct pnp_dev *dev)
{
	int i = 0;

	while (*pnp_fixups[i].id) {
		if (compare_pnp_id(dev->id,pnp_fixups[i].id)) {
			pnp_dbg("Calling quirk for %s",
		                  dev->dev.bus_id);
			pnp_fixups[i].quirk_function(dev);
		}
		i++;
	}
}
