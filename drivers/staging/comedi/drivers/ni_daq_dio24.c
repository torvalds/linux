/*
    comedi/drivers/ni_daq_dio24.c
    Driver for National Instruments PCMCIA DAQ-Card DIO-24
    Copyright (C) 2002 Daniel Vecino Castel <dvecino@able.es>

    PCMCIA crap at end of file is adapted from dummy_cs.c 1.31 2001/08/24 12:13:13
    from the pcmcia package.
    The initial developer of the pcmcia dummy_cs.c code is David A. Hinds
    <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
    are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

************************************************************************
*/
/*
Driver: ni_daq_dio24
Description: National Instruments PCMCIA DAQ-Card DIO-24
Author: Daniel Vecino Castel <dvecino@able.es>
Devices: [National Instruments] PCMCIA DAQ-Card DIO-24 (ni_daq_dio24)
Status: ?
Updated: Thu, 07 Nov 2002 21:53:06 -0800

This is just a wrapper around the 8255.o driver to properly handle
the PCMCIA interface.
*/

			    /* #define LABPC_DEBUG *//*  enable debugging messages */
#undef LABPC_DEBUG

#include <linux/interrupt.h>
#include <linux/slab.h>
#include "../comedidev.h"

#include <linux/ioport.h>

#include "8255.h"

#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

static struct pcmcia_device *pcmcia_cur_dev = NULL;

#define DIO24_SIZE 4		/*  size of io region used by board */

static int dio24_attach(struct comedi_device *dev, struct comedi_devconfig *it);
static int dio24_detach(struct comedi_device *dev);

enum dio24_bustype { pcmcia_bustype };

struct dio24_board_struct {
	const char *name;
	int device_id;		/*  device id for pcmcia board */
	enum dio24_bustype bustype;	/*  PCMCIA */
	int have_dio;		/*  have 8255 chip */
	/*  function pointers so we can use inb/outb or readb/writeb as appropriate */
	unsigned int (*read_byte) (unsigned int address);
	void (*write_byte) (unsigned int byte, unsigned int address);
};

static const struct dio24_board_struct dio24_boards[] = {
	{
	 .name = "daqcard-dio24",
	 .device_id = 0x475c,	/*  0x10b is manufacturer id, 0x475c is device id */
	 .bustype = pcmcia_bustype,
	 .have_dio = 1,
	 },
	{
	 .name = "ni_daq_dio24",
	 .device_id = 0x475c,	/*  0x10b is manufacturer id, 0x475c is device id */
	 .bustype = pcmcia_bustype,
	 .have_dio = 1,
	 },
};

/*
 * Useful for shorthand access to the particular board structure
 */
#define thisboard ((const struct dio24_board_struct *)dev->board_ptr)

struct dio24_private {

	int data;		/* number of data points left to be taken */
};

#define devpriv ((struct dio24_private *)dev->private)

static struct comedi_driver driver_dio24 = {
	.driver_name = "ni_daq_dio24",
	.module = THIS_MODULE,
	.attach = dio24_attach,
	.detach = dio24_detach,
	.num_names = ARRAY_SIZE(dio24_boards),
	.board_name = &dio24_boards[0].name,
	.offset = sizeof(struct dio24_board_struct),
};

static int dio24_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	unsigned long iobase = 0;
#ifdef incomplete
	unsigned int irq = 0;
#endif
	struct pcmcia_device *link;

	/* allocate and initialize dev->private */
	if (alloc_private(dev, sizeof(struct dio24_private)) < 0)
		return -ENOMEM;

	/*  get base address, irq etc. based on bustype */
	switch (thisboard->bustype) {
	case pcmcia_bustype:
		link = pcmcia_cur_dev;	/* XXX hack */
		if (!link)
			return -EIO;
		iobase = link->resource[0]->start;
#ifdef incomplete
		irq = link->irq;
#endif
		break;
	default:
		printk("bug! couldn't determine board type\n");
		return -EINVAL;
		break;
	}
	printk("comedi%d: ni_daq_dio24: %s, io 0x%lx", dev->minor,
	       thisboard->name, iobase);
#ifdef incomplete
	if (irq) {
		printk(", irq %u", irq);
	}
#endif

	printk("\n");

	if (iobase == 0) {
		printk("io base address is zero!\n");
		return -EINVAL;
	}

	dev->iobase = iobase;

#ifdef incomplete
	/* grab our IRQ */
	dev->irq = irq;
#endif

	dev->board_name = thisboard->name;

	if (alloc_subdevices(dev, 1) < 0)
		return -ENOMEM;

	/* 8255 dio */
	s = dev->subdevices + 0;
	subdev_8255_init(dev, s, NULL, dev->iobase);

	return 0;
};

static int dio24_detach(struct comedi_device *dev)
{
	printk("comedi%d: ni_daq_dio24: remove\n", dev->minor);

	if (dev->subdevices)
		subdev_8255_cleanup(dev, dev->subdevices + 0);

	if (thisboard->bustype != pcmcia_bustype && dev->iobase)
		release_region(dev->iobase, DIO24_SIZE);
	if (dev->irq)
		free_irq(dev->irq, dev);

	return 0;
};

/* PCMCIA crap -- watch your words! */

static void dio24_config(struct pcmcia_device *link);
static void dio24_release(struct pcmcia_device *link);
static int dio24_cs_suspend(struct pcmcia_device *p_dev);
static int dio24_cs_resume(struct pcmcia_device *p_dev);

/*
   The attach() and detach() entry points are used to create and destroy
   "instances" of the driver, where each instance represents everything
   needed to manage one actual PCMCIA card.
*/

static int dio24_cs_attach(struct pcmcia_device *);
static void dio24_cs_detach(struct pcmcia_device *);

/*
   You'll also need to prototype all the functions that will actually
   be used to talk to your device.  See 'memory_cs' for a good example
   of a fully self-sufficient driver; the other drivers rely more or
   less on other parts of the kernel.
*/

struct local_info_t {
	struct pcmcia_device *link;
	int stop;
	struct bus_operations *bus;
};

/*======================================================================

    dio24_cs_attach() creates an "instance" of the driver, allocating
    local data structures for one device.  The device is registered
    with Card Services.

    The dev_link structure is initialized, but we don't actually
    configure the card at this point -- we wait until we receive a
    card insertion event.

======================================================================*/

static int dio24_cs_attach(struct pcmcia_device *link)
{
	struct local_info_t *local;

	printk(KERN_INFO "ni_daq_dio24: HOLA SOY YO - CS-attach!\n");

	dev_dbg(&link->dev, "dio24_cs_attach()\n");

	/* Allocate space for private device-specific data */
	local = kzalloc(sizeof(struct local_info_t), GFP_KERNEL);
	if (!local)
		return -ENOMEM;
	local->link = link;
	link->priv = local;

	/*
	   General socket configuration defaults can go here.  In this
	   client, we assume very little, and rely on the CIS for almost
	   everything.  In most clients, many details (i.e., number, sizes,
	   and attributes of IO windows) are fixed by the nature of the
	   device, and can be hard-wired here.
	 */
	link->conf.Attributes = 0;
	link->conf.IntType = INT_MEMORY_AND_IO;

	pcmcia_cur_dev = link;

	dio24_config(link);

	return 0;
}				/* dio24_cs_attach */

/*======================================================================

    This deletes a driver "instance".  The device is de-registered
    with Card Services.  If it has been released, all local data
    structures are freed.  Otherwise, the structures will be freed
    when the device is released.

======================================================================*/

static void dio24_cs_detach(struct pcmcia_device *link)
{

	printk(KERN_INFO "ni_daq_dio24: HOLA SOY YO - cs-detach!\n");

	dev_dbg(&link->dev, "dio24_cs_detach\n");

	((struct local_info_t *)link->priv)->stop = 1;
	dio24_release(link);

	/* This points to the parent local_info_t struct */
	kfree(link->priv);

}				/* dio24_cs_detach */

/*======================================================================

    dio24_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    device available to the system.

======================================================================*/

static int dio24_pcmcia_config_loop(struct pcmcia_device *p_dev,
				cistpl_cftable_entry_t *cfg,
				cistpl_cftable_entry_t *dflt,
				unsigned int vcc,
				void *priv_data)
{
	if (cfg->index == 0)
		return -ENODEV;

	/* Does this card need audio output? */
	if (cfg->flags & CISTPL_CFTABLE_AUDIO) {
		p_dev->conf.Attributes |= CONF_ENABLE_SPKR;
		p_dev->conf.Status = CCSR_AUDIO_ENA;
	}

	/* Do we need to allocate an interrupt? */
	p_dev->conf.Attributes |= CONF_ENABLE_IRQ;

	/* IO window settings */
	p_dev->resource[0]->end = p_dev->resource[1]->end = 0;
	if ((cfg->io.nwin > 0) || (dflt->io.nwin > 0)) {
		cistpl_io_t *io = (cfg->io.nwin) ? &cfg->io : &dflt->io;
		p_dev->io_lines = io->flags & CISTPL_IO_LINES_MASK;
		p_dev->resource[0]->flags &= ~IO_DATA_PATH_WIDTH;
		p_dev->resource[0]->flags |=
			pcmcia_io_cfg_data_width(io->flags);
		p_dev->resource[0]->start = io->win[0].base;
		p_dev->resource[0]->end = io->win[0].len;
		if (io->nwin > 1) {
			p_dev->resource[1]->flags = p_dev->resource[0]->flags;
			p_dev->resource[1]->start = io->win[1].base;
			p_dev->resource[1]->end = io->win[1].len;
		}
		/* This reserves IO space but doesn't actually enable it */
		if (pcmcia_request_io(p_dev) != 0)
			return -ENODEV;
	}

	/* If we got this far, we're cool! */
	return 0;
}

static void dio24_config(struct pcmcia_device *link)
{
	int ret;

	printk(KERN_INFO "ni_daq_dio24: HOLA SOY YO! - config\n");

	dev_dbg(&link->dev, "dio24_config\n");

	ret = pcmcia_loop_config(link, dio24_pcmcia_config_loop, NULL);
	if (ret) {
		dev_warn(&link->dev, "no configuration found\n");
		goto failed;
	}

	if (!link->irq)
		goto failed;

	/*
	   This actually configures the PCMCIA socket -- setting up
	   the I/O windows and the interrupt mapping, and putting the
	   card and host interface into "Memory and IO" mode.
	 */
	ret = pcmcia_request_configuration(link, &link->conf);
	if (ret)
		goto failed;

	/* Finally, report what we've done */
	dev_info(&link->dev, "index 0x%02x", link->conf.ConfigIndex);
	if (link->conf.Attributes & CONF_ENABLE_IRQ)
		printk(", irq %d", link->irq);
	if (link->resource[0])
		printk(" & %pR", link->resource[0]);
	if (link->resource[1])
		printk(" & %pR", link->resource[1]);
	printk("\n");

	return;

failed:
	printk(KERN_INFO "Fallo");
	dio24_release(link);

}				/* dio24_config */

static void dio24_release(struct pcmcia_device *link)
{
	dev_dbg(&link->dev, "dio24_release\n");

	pcmcia_disable_device(link);
}				/* dio24_release */

/*======================================================================

    The card status event handler.  Mostly, this schedules other
    stuff to run after an event is received.

    When a CARD_REMOVAL event is received, we immediately set a
    private flag to block future accesses to this device.  All the
    functions that actually access the device should check this flag
    to make sure the card is still present.

======================================================================*/

static int dio24_cs_suspend(struct pcmcia_device *link)
{
	struct local_info_t *local = link->priv;

	/* Mark the device as stopped, to block IO until later */
	local->stop = 1;
	return 0;
}				/* dio24_cs_suspend */

static int dio24_cs_resume(struct pcmcia_device *link)
{
	struct local_info_t *local = link->priv;

	local->stop = 0;
	return 0;
}				/* dio24_cs_resume */

/*====================================================================*/

static struct pcmcia_device_id dio24_cs_ids[] = {
	/* N.B. These IDs should match those in dio24_boards */
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x475c),	/* daqcard-dio24 */
	PCMCIA_DEVICE_NULL
};

MODULE_DEVICE_TABLE(pcmcia, dio24_cs_ids);
MODULE_AUTHOR("Daniel Vecino Castel <dvecino@able.es>");
MODULE_DESCRIPTION("Comedi driver for National Instruments "
		   "PCMCIA DAQ-Card DIO-24");
MODULE_LICENSE("GPL");

struct pcmcia_driver dio24_cs_driver = {
	.probe = dio24_cs_attach,
	.remove = dio24_cs_detach,
	.suspend = dio24_cs_suspend,
	.resume = dio24_cs_resume,
	.id_table = dio24_cs_ids,
	.owner = THIS_MODULE,
	.drv = {
		.name = "ni_daq_dio24",
		},
};

static int __init init_dio24_cs(void)
{
	printk("ni_daq_dio24: HOLA SOY YO!\n");
	pcmcia_register_driver(&dio24_cs_driver);
	return 0;
}

static void __exit exit_dio24_cs(void)
{
	pcmcia_unregister_driver(&dio24_cs_driver);
}

int __init init_module(void)
{
	int ret;

	ret = init_dio24_cs();
	if (ret < 0)
		return ret;

	return comedi_driver_register(&driver_dio24);
}

void __exit cleanup_module(void)
{
	exit_dio24_cs();
	comedi_driver_unregister(&driver_dio24);
}
