/* -*- mode: c; c-basic-offset: 8 -*- */

/* NCR Dual 700 MCA SCSI Driver
 *
 * Copyright (C) 2001 by James.Bottomley@HansenPartnership.com
**-----------------------------------------------------------------------------
**  
**  This program is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 2 of the License, or
**  (at your option) any later version.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with this program; if not, write to the Free Software
**  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**-----------------------------------------------------------------------------
 */

/* Notes:
 *
 * Most of the work is done in the chip specific module, 53c700.o
 *
 * TODO List:
 *
 * 1. Extract the SCSI ID from the voyager CMOS table (necessary to
 *    support multi-host environments.
 *
 * */


/* CHANGELOG 
 *
 * Version 2.2
 *
 * Added mca_set_adapter_name().
 *
 * Version 2.1
 *
 * Modularise the driver into a Board piece (this file) and a chip
 * piece 53c700.[ch] and 53c700.scr, added module options.  You can
 * now specify the scsi id by the parameters
 *
 * NCR_D700=slot:<n> [siop:<n>] id:<n> ....
 *
 * They need to be comma separated if compiled into the kernel
 *
 * Version 2.0
 *
 * Initial implementation of TCQ (Tag Command Queueing).  TCQ is full
 * featured and uses the clock algorithm to keep track of outstanding
 * tags and guard against individual tag starvation.  Also fixed a bug
 * in all of the 1.x versions where the D700_data_residue() function
 * was returning results off by 32 bytes (and thus causing the same 32
 * bytes to be written twice corrupting the data block).  It turns out
 * the 53c700 only has a 6 bit DBC and DFIFO registers not 7 bit ones
 * like the 53c710 (The 710 is the only data manual still available,
 * which I'd been using to program the 700).
 *
 * Version 1.2
 *
 * Much improved message handling engine
 *
 * Version 1.1
 *
 * Add code to handle selection reasonably correctly.  By the time we
 * get the selection interrupt, we've already responded, but drop off the
 * bus and hope the selector will go away.
 *
 * Version 1.0:
 *
 *   Initial release.  Fully functional except for procfs and tag
 * command queueing.  Has only been tested on cards with 53c700-66
 * chips and only single ended. Features are
 *
 * 1. Synchronous data transfers to offset 8 (limit of 700-66) and
 *    100ns (10MHz) limit of SCSI-2
 *
 * 2. Disconnection and reselection
 *
 * Testing:
 * 
 *  I've only really tested this with the 700-66 chip, but have done
 * soak tests in multi-device environments to verify that
 * disconnections and reselections are being processed correctly.
 * */

#define NCR_D700_VERSION "2.2"

#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mca.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_spi.h>

#include "53c700.h"
#include "NCR_D700.h"

static char *NCR_D700;		/* command line from insmod */

MODULE_AUTHOR("James Bottomley");
MODULE_DESCRIPTION("NCR Dual700 SCSI Driver");
MODULE_LICENSE("GPL");
module_param(NCR_D700, charp, 0);

static __u8 __devinitdata id_array[2*(MCA_MAX_SLOT_NR + 1)] =
	{ [0 ... 2*(MCA_MAX_SLOT_NR + 1)-1] = 7 };

#ifdef MODULE
#define ARG_SEP ' '
#else
#define ARG_SEP ','
#endif

static int __init
param_setup(char *string)
{
	char *pos = string, *next;
	int slot = -1, siop = -1;

	while(pos != NULL && (next = strchr(pos, ':')) != NULL) {
		int val = (int)simple_strtoul(++next, NULL, 0);

		if(!strncmp(pos, "slot:", 5))
			slot = val;
		else if(!strncmp(pos, "siop:", 5))
			siop = val;
		else if(!strncmp(pos, "id:", 3)) {
			if(slot == -1) {
				printk(KERN_WARNING "NCR D700: Must specify slot for id parameter\n");
			} else if(slot > MCA_MAX_SLOT_NR) {
				printk(KERN_WARNING "NCR D700: Illegal slot %d for id %d\n", slot, val);
			} else {
				if(siop != 0 && siop != 1) {
					id_array[slot*2] = val;
					id_array[slot*2 + 1] =val;
				} else {
					id_array[slot*2 + siop] = val;
				}
			}
		}
		if((pos = strchr(pos, ARG_SEP)) != NULL)
			pos++;
	}
	return 1;
}

/* Host template.  The 53c700 routine NCR_700_detect will
 * fill in all of the missing routines */
static struct scsi_host_template NCR_D700_driver_template = {
	.module			= THIS_MODULE,
	.name			= "NCR Dual 700 MCA",
	.proc_name		= "NCR_D700",
	.this_id		= 7,
};

/* We needs this helper because we have two hosts per struct device */
struct NCR_D700_private {
	struct device		*dev;
	struct Scsi_Host	*hosts[2];
	char			name[30];
	char			pad;
};

static int __devinit
NCR_D700_probe_one(struct NCR_D700_private *p, int siop, int irq,
		   int slot, u32 region, int differential)
{
	struct NCR_700_Host_Parameters *hostdata;
	struct Scsi_Host *host;
	int ret;

	hostdata = kzalloc(sizeof(*hostdata), GFP_KERNEL);
	if (!hostdata) {
		printk(KERN_ERR "NCR D700: SIOP%d: Failed to allocate host"
		       "data, detatching\n", siop);
		return -ENOMEM;
	}

	if (!request_region(region, 64, "NCR_D700")) {
		printk(KERN_ERR "NCR D700: Failed to reserve IO region 0x%x\n",
				region);
		ret = -ENODEV;
		goto region_failed;
	}
		
	/* Fill in the three required pieces of hostdata */
	hostdata->base = ioport_map(region, 64);
	hostdata->differential = (((1<<siop) & differential) != 0);
	hostdata->clock = NCR_D700_CLOCK_MHZ;
	hostdata->burst_length = 8;

	/* and register the siop */
	host = NCR_700_detect(&NCR_D700_driver_template, hostdata, p->dev);
	if (!host) {
		ret = -ENOMEM;
		goto detect_failed;
	}

	p->hosts[siop] = host;
	/* FIXME: read this from SUS */
	host->this_id = id_array[slot * 2 + siop];
	host->irq = irq;
	host->base = region;
	scsi_scan_host(host);

	return 0;

 detect_failed:
	release_region(region, 64);
 region_failed:
	kfree(hostdata);

	return ret;
}

static int
NCR_D700_intr(int irq, void *data)
{
	struct NCR_D700_private *p = (struct NCR_D700_private *)data;
	int i, found = 0;

	for (i = 0; i < 2; i++)
		if (p->hosts[i] &&
		    NCR_700_intr(irq, p->hosts[i]) == IRQ_HANDLED)
			found++;

	return found ? IRQ_HANDLED : IRQ_NONE;
}

/* Detect a D700 card.  Note, because of the setup --- the chips are
 * essentially connectecd to the MCA bus independently, it is easier
 * to set them up as two separate host adapters, rather than one
 * adapter with two channels */
static int __devinit
NCR_D700_probe(struct device *dev)
{
	struct NCR_D700_private *p;
	int differential;
	static int banner = 1;
	struct mca_device *mca_dev = to_mca_device(dev);
	int slot = mca_dev->slot;
	int found = 0;
	int irq, i;
	int pos3j, pos3k, pos3a, pos3b, pos4;
	__u32 base_addr, offset_addr;

	/* enable board interrupt */
	pos4 = mca_device_read_pos(mca_dev, 4);
	pos4 |= 0x4;
	mca_device_write_pos(mca_dev, 4, pos4);

	mca_device_write_pos(mca_dev, 6, 9);
	pos3j = mca_device_read_pos(mca_dev, 3);
	mca_device_write_pos(mca_dev, 6, 10);
	pos3k = mca_device_read_pos(mca_dev, 3);
	mca_device_write_pos(mca_dev, 6, 0);
	pos3a = mca_device_read_pos(mca_dev, 3);
	mca_device_write_pos(mca_dev, 6, 1);
	pos3b = mca_device_read_pos(mca_dev, 3);

	base_addr = ((pos3j << 8) | pos3k) & 0xfffffff0;
	offset_addr = ((pos3a << 8) | pos3b) & 0xffffff70;

	irq = (pos4 & 0x3) + 11;
	if(irq >= 13)
		irq++;
	if(banner) {
		printk(KERN_NOTICE "NCR D700: Driver Version " NCR_D700_VERSION "\n"
		       "NCR D700:  Copyright (c) 2001 by James.Bottomley@HansenPartnership.com\n"
		       "NCR D700:\n");
		banner = 0;
	}
	/* now do the bus related transforms */
	irq = mca_device_transform_irq(mca_dev, irq);
	base_addr = mca_device_transform_ioport(mca_dev, base_addr);
	offset_addr = mca_device_transform_ioport(mca_dev, offset_addr);

	printk(KERN_NOTICE "NCR D700: found in slot %d  irq = %d  I/O base = 0x%x\n", slot, irq, offset_addr);

	/*outb(BOARD_RESET, base_addr);*/

	/* clear any pending interrupts */
	(void)inb(base_addr + 0x08);
	/* get modctl, used later for setting diff bits */
	switch(differential = (inb(base_addr + 0x08) >> 6)) {
	case 0x00:
		/* only SIOP1 differential */
		differential = 0x02;
		break;
	case 0x01:
		/* Both SIOPs differential */
		differential = 0x03;
		break;
	case 0x03:
		/* No SIOPs differential */
		differential = 0x00;
		break;
	default:
		printk(KERN_ERR "D700: UNEXPECTED DIFFERENTIAL RESULT 0x%02x\n",
		       differential);
		differential = 0x00;
		break;
	}

	p = kmalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	memset(p, '\0', sizeof(*p));
	p->dev = dev;
	snprintf(p->name, sizeof(p->name), "D700(%s)", dev->bus_id);
	if (request_irq(irq, NCR_D700_intr, IRQF_SHARED, p->name, p)) {
		printk(KERN_ERR "D700: request_irq failed\n");
		kfree(p);
		return -EBUSY;
	}
	/* plumb in both 700 chips */
	for (i = 0; i < 2; i++) {
		int err;

		if ((err = NCR_D700_probe_one(p, i, irq, slot,
					      offset_addr + (0x80 * i),
					      differential)) != 0)
			printk("D700: SIOP%d: probe failed, error = %d\n",
			       i, err);
		else
			found++;
	}

	if (!found) {
		kfree(p);
		return -ENODEV;
	}

	mca_device_set_claim(mca_dev, 1);
	mca_device_set_name(mca_dev, "NCR_D700");
	dev_set_drvdata(dev, p);
	return 0;
}

static void __devexit
NCR_D700_remove_one(struct Scsi_Host *host)
{
	scsi_remove_host(host);
	NCR_700_release(host);
	kfree((struct NCR_700_Host_Parameters *)host->hostdata[0]);
	free_irq(host->irq, host);
	release_region(host->base, 64);
}

static int __devexit
NCR_D700_remove(struct device *dev)
{
	struct NCR_D700_private *p = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < 2; i++)
		NCR_D700_remove_one(p->hosts[i]);

	kfree(p);
	return 0;
}

static short NCR_D700_id_table[] = { NCR_D700_MCA_ID, 0 };

static struct mca_driver NCR_D700_driver = {
	.id_table = NCR_D700_id_table,
	.driver = {
		.name		= "NCR_D700",
		.bus		= &mca_bus_type,
		.probe		= NCR_D700_probe,
		.remove		= __devexit_p(NCR_D700_remove),
	},
};

static int __init NCR_D700_init(void)
{
#ifdef MODULE
	if (NCR_D700)
		param_setup(NCR_D700);
#endif

	return mca_register_driver(&NCR_D700_driver);
}

static void __exit NCR_D700_exit(void)
{
	mca_unregister_driver(&NCR_D700_driver);
}

module_init(NCR_D700_init);
module_exit(NCR_D700_exit);

__setup("NCR_D700=", param_setup);
