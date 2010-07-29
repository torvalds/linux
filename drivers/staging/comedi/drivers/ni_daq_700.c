/*
 *     comedi/drivers/ni_daq_700.c
 *     Driver for DAQCard-700 DIO only
 *     copied from 8255
 *
 *     COMEDI - Linux Control and Measurement Device Interface
 *     Copyright (C) 1998 David A. Schleef <ds@schleef.org>
 *
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/*
Driver: ni_daq_700
Description: National Instruments PCMCIA DAQCard-700 DIO only
Author: Fred Brooks <nsaspook@nsaspook.com>,
  based on ni_daq_dio24 by Daniel Vecino Castel <dvecino@able.es>
Devices: [National Instruments] PCMCIA DAQ-Card-700 (ni_daq_700)
Status: works
Updated: Thu, 21 Feb 2008 12:07:20 +0000

The daqcard-700 appears in Comedi as a single digital I/O subdevice with
16 channels.  The channel 0 corresponds to the daqcard-700's output
port, bit 0; channel 8 corresponds to the input port, bit 0.

Direction configuration: channels 0-7 output, 8-15 input (8225 device
emu as port A output, port B input, port C N/A).

IRQ is assigned but not used.
*/

#include <linux/interrupt.h>
#include <linux/slab.h>
#include "../comedidev.h"

#include <linux/ioport.h>

#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

static struct pcmcia_device *pcmcia_cur_dev = NULL;

#define DIO700_SIZE 8		/*  size of io region used by board */

static int dio700_attach(struct comedi_device *dev,
			 struct comedi_devconfig *it);
static int dio700_detach(struct comedi_device *dev);

enum dio700_bustype { pcmcia_bustype };

struct dio700_board {
	const char *name;
	int device_id;		/*  device id for pcmcia board */
	enum dio700_bustype bustype;	/*  PCMCIA */
	int have_dio;		/*  have daqcard-700 dio */
	/*  function pointers so we can use inb/outb or readb/writeb */
	/*  as appropriate */
	unsigned int (*read_byte) (unsigned int address);
	void (*write_byte) (unsigned int byte, unsigned int address);
};

static const struct dio700_board dio700_boards[] = {
	{
	 .name = "daqcard-700",
	  /*  0x10b is manufacturer id, 0x4743 is device id */
	 .device_id = 0x4743,
	 .bustype = pcmcia_bustype,
	 .have_dio = 1,
	 },
	{
	 .name = "ni_daq_700",
	  /*  0x10b is manufacturer id, 0x4743 is device id */
	 .device_id = 0x4743,
	 .bustype = pcmcia_bustype,
	 .have_dio = 1,
	 },
};

/*
 * Useful for shorthand access to the particular board structure
 */
#define thisboard ((const struct dio700_board *)dev->board_ptr)

struct dio700_private {

	int data;		/* number of data points left to be taken */
};

#define devpriv ((struct dio700_private *)dev->private)

static struct comedi_driver driver_dio700 = {
	.driver_name = "ni_daq_700",
	.module = THIS_MODULE,
	.attach = dio700_attach,
	.detach = dio700_detach,
	.num_names = ARRAY_SIZE(dio700_boards),
	.board_name = &dio700_boards[0].name,
	.offset = sizeof(struct dio700_board),
};

/*	the real driver routines	*/

#define _700_SIZE 8

#define _700_DATA 0

#define DIO_W		0x04
#define DIO_R		0x05

struct subdev_700_struct {
	unsigned long cb_arg;
	int (*cb_func) (int, int, int, unsigned long);
	int have_irq;
};

#define CALLBACK_ARG	(((struct subdev_700_struct *)s->private)->cb_arg)
#define CALLBACK_FUNC	(((struct subdev_700_struct *)s->private)->cb_func)
#define subdevpriv	((struct subdev_700_struct *)s->private)

static void do_config(struct comedi_device *dev, struct comedi_subdevice *s);

void subdev_700_interrupt(struct comedi_device *dev, struct comedi_subdevice *s)
{
	short d;

	d = CALLBACK_FUNC(0, _700_DATA, 0, CALLBACK_ARG);

	comedi_buf_put(s->async, d);
	s->async->events |= COMEDI_CB_EOS;

	comedi_event(dev, s);
}
EXPORT_SYMBOL(subdev_700_interrupt);

static int subdev_700_cb(int dir, int port, int data, unsigned long arg)
{
	/* port is always A for output and B for input (8255 emu) */
	unsigned long iobase = arg;

	if (dir) {
		outb(data, iobase + DIO_W);
		return 0;
	} else {
		return inb(iobase + DIO_R);
	}
}

static int subdev_700_insn(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data)
{
	if (data[0]) {
		s->state &= ~data[0];
		s->state |= (data[0] & data[1]);

		if (data[0] & 0xff)
			CALLBACK_FUNC(1, _700_DATA, s->state & 0xff,
				      CALLBACK_ARG);
	}

	data[1] = s->state & 0xff;
	data[1] |= CALLBACK_FUNC(0, _700_DATA, 0, CALLBACK_ARG) << 8;

	return 2;
}

static int subdev_700_insn_config(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{

	switch (data[0]) {
	case INSN_CONFIG_DIO_INPUT:
		break;
	case INSN_CONFIG_DIO_OUTPUT:
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] =
		    (s->
		     io_bits & (1 << CR_CHAN(insn->chanspec))) ? COMEDI_OUTPUT :
		    COMEDI_INPUT;
		return insn->n;
		break;
	default:
		return -EINVAL;
	}

	return 1;
}

static void do_config(struct comedi_device *dev, struct comedi_subdevice *s)
{				/* use powerup defaults */
	return;
}

static int subdev_700_cmdtest(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_cmd *cmd)
{
	int err = 0;
	unsigned int tmp;

	/* step 1 */

	tmp = cmd->start_src;
	cmd->start_src &= TRIG_NOW;
	if (!cmd->start_src || tmp != cmd->start_src)
		err++;

	tmp = cmd->scan_begin_src;
	cmd->scan_begin_src &= TRIG_EXT;
	if (!cmd->scan_begin_src || tmp != cmd->scan_begin_src)
		err++;

	tmp = cmd->convert_src;
	cmd->convert_src &= TRIG_FOLLOW;
	if (!cmd->convert_src || tmp != cmd->convert_src)
		err++;

	tmp = cmd->scan_end_src;
	cmd->scan_end_src &= TRIG_COUNT;
	if (!cmd->scan_end_src || tmp != cmd->scan_end_src)
		err++;

	tmp = cmd->stop_src;
	cmd->stop_src &= TRIG_NONE;
	if (!cmd->stop_src || tmp != cmd->stop_src)
		err++;

	if (err)
		return 1;

	/* step 2 */

	if (err)
		return 2;

	/* step 3 */

	if (cmd->start_arg != 0) {
		cmd->start_arg = 0;
		err++;
	}
	if (cmd->scan_begin_arg != 0) {
		cmd->scan_begin_arg = 0;
		err++;
	}
	if (cmd->convert_arg != 0) {
		cmd->convert_arg = 0;
		err++;
	}
	if (cmd->scan_end_arg != 1) {
		cmd->scan_end_arg = 1;
		err++;
	}
	if (cmd->stop_arg != 0) {
		cmd->stop_arg = 0;
		err++;
	}

	if (err)
		return 3;

	/* step 4 */

	if (err)
		return 4;

	return 0;
}

static int subdev_700_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	/* FIXME */

	return 0;
}

static int subdev_700_cancel(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	/* FIXME */

	return 0;
}

int subdev_700_init(struct comedi_device *dev, struct comedi_subdevice *s,
		    int (*cb) (int, int, int, unsigned long), unsigned long arg)
{
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->n_chan = 16;
	s->range_table = &range_digital;
	s->maxdata = 1;

	s->private = kmalloc(sizeof(struct subdev_700_struct), GFP_KERNEL);
	if (!s->private)
		return -ENOMEM;

	CALLBACK_ARG = arg;
	if (cb == NULL)
		CALLBACK_FUNC = subdev_700_cb;
	 else
		CALLBACK_FUNC = cb;

	s->insn_bits = subdev_700_insn;
	s->insn_config = subdev_700_insn_config;

	s->state = 0;
	s->io_bits = 0x00ff;
	do_config(dev, s);

	return 0;
}
EXPORT_SYMBOL(subdev_700_init);

int subdev_700_init_irq(struct comedi_device *dev, struct comedi_subdevice *s,
			int (*cb) (int, int, int, unsigned long),
			unsigned long arg)
{
	int ret;

	ret = subdev_700_init(dev, s, cb, arg);
	if (ret < 0)
		return ret;

	s->do_cmdtest = subdev_700_cmdtest;
	s->do_cmd = subdev_700_cmd;
	s->cancel = subdev_700_cancel;

	subdevpriv->have_irq = 1;

	return 0;
}
EXPORT_SYMBOL(subdev_700_init_irq);

void subdev_700_cleanup(struct comedi_device *dev, struct comedi_subdevice *s)
{
	if (s->private)
		if (subdevpriv->have_irq)

			kfree(s->private);
}
EXPORT_SYMBOL(subdev_700_cleanup);

static int dio700_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	unsigned long iobase = 0;
#ifdef incomplete
	unsigned int irq = 0;
#endif
	struct pcmcia_device *link;

	/* allocate and initialize dev->private */
	if (alloc_private(dev, sizeof(struct dio700_private)) < 0)
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
	printk("comedi%d: ni_daq_700: %s, io 0x%lx", dev->minor,
	       thisboard->name, iobase);
#ifdef incomplete
	if (irq)
		printk(", irq %u", irq);

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

	/* DAQCard-700 dio */
	s = dev->subdevices + 0;
	subdev_700_init(dev, s, NULL, dev->iobase);

	return 0;
};

static int dio700_detach(struct comedi_device *dev)
{
	printk("comedi%d: ni_daq_700: cs-remove\n", dev->minor);

	if (dev->subdevices)
		subdev_700_cleanup(dev, dev->subdevices + 0);

	if (thisboard->bustype != pcmcia_bustype && dev->iobase)
		release_region(dev->iobase, DIO700_SIZE);
	if (dev->irq)
		free_irq(dev->irq, dev);

	return 0;
};

/* PCMCIA crap -- watch your words, please! */

static void dio700_config(struct pcmcia_device *link);
static void dio700_release(struct pcmcia_device *link);
static int dio700_cs_suspend(struct pcmcia_device *p_dev);
static int dio700_cs_resume(struct pcmcia_device *p_dev);

/*
   The attach() and detach() entry points are used to create and destroy
   "instances" of the driver, where each instance represents everything
   needed to manage one actual PCMCIA card.
*/

static int dio700_cs_attach(struct pcmcia_device *);
static void dio700_cs_detach(struct pcmcia_device *);

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

    dio700_cs_attach() creates an "instance" of the driver, allocating
    local data structures for one device.  The device is registered
    with Card Services.

    The dev_link structure is initialized, but we don't actually
    configure the card at this point -- we wait until we receive a
    card insertion event.

======================================================================*/

static int dio700_cs_attach(struct pcmcia_device *link)
{
	struct local_info_t *local;

	printk(KERN_INFO "ni_daq_700:  cs-attach\n");

	dev_dbg(&link->dev, "dio700_cs_attach()\n");

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

	dio700_config(link);

	return 0;
}				/* dio700_cs_attach */

/*======================================================================

    This deletes a driver "instance".  The device is de-registered
    with Card Services.  If it has been released, all local data
    structures are freed.  Otherwise, the structures will be freed
    when the device is released.

======================================================================*/

static void dio700_cs_detach(struct pcmcia_device *link)
{

	printk(KERN_INFO "ni_daq_700: cs-detach!\n");

	dev_dbg(&link->dev, "dio700_cs_detach\n");

	((struct local_info_t *)link->priv)->stop = 1;
	dio700_release(link);

	/* This points to the parent struct local_info_t struct */
	kfree(link->priv);

}				/* dio700_cs_detach */

/*======================================================================

    dio700_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    device available to the system.

======================================================================*/

static int dio700_pcmcia_config_loop(struct pcmcia_device *p_dev,
				cistpl_cftable_entry_t *cfg,
				cistpl_cftable_entry_t *dflt,
				unsigned int vcc,
				void *priv_data)
{
	if (cfg->index == 0)
		return -ENODEV;

	/* Does this card need audio output? */
	if (cfg->flags & CISTPL_CFTABLE_AUDIO)
		p_dev->conf.Attributes |= CONF_ENABLE_SPKR;

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

static void dio700_config(struct pcmcia_device *link)
{
	win_req_t req;
	int ret;

	printk(KERN_INFO "ni_daq_700:  cs-config\n");

	dev_dbg(&link->dev, "dio700_config\n");

	ret = pcmcia_loop_config(link, dio700_pcmcia_config_loop, NULL);
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
	if (ret != 0)
		goto failed;

	/* Finally, report what we've done */
	dev_info(&link->dev, "index 0x%02x", link->conf.ConfigIndex);
	if (link->conf.Attributes & CONF_ENABLE_IRQ)
		printk(", irq %d", link->irq);
	if (link->resource[0])
		printk(", io %pR", link->resource[0]);
	if (link->resource[1])
		printk(" & %pR", link->resource[1]);
	printk("\n");

	return;

failed:
	printk(KERN_INFO "ni_daq_700 cs failed");
	dio700_release(link);

}				/* dio700_config */

static void dio700_release(struct pcmcia_device *link)
{
	dev_dbg(&link->dev, "dio700_release\n");

	pcmcia_disable_device(link);
}				/* dio700_release */

/*======================================================================

    The card status event handler.  Mostly, this schedules other
    stuff to run after an event is received.

    When a CARD_REMOVAL event is received, we immediately set a
    private flag to block future accesses to this device.  All the
    functions that actually access the device should check this flag
    to make sure the card is still present.

======================================================================*/

static int dio700_cs_suspend(struct pcmcia_device *link)
{
	struct local_info_t *local = link->priv;

	/* Mark the device as stopped, to block IO until later */
	local->stop = 1;
	return 0;
}				/* dio700_cs_suspend */

static int dio700_cs_resume(struct pcmcia_device *link)
{
	struct local_info_t *local = link->priv;

	local->stop = 0;
	return 0;
}				/* dio700_cs_resume */

/*====================================================================*/

static struct pcmcia_device_id dio700_cs_ids[] = {
	/* N.B. These IDs should match those in dio700_boards */
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x4743),	/* daqcard-700 */
	PCMCIA_DEVICE_NULL
};


MODULE_DEVICE_TABLE(pcmcia, dio700_cs_ids);
MODULE_AUTHOR("Fred Brooks <nsaspook@nsaspook.com>");
MODULE_DESCRIPTION("Comedi driver for National Instruments "
		   "PCMCIA DAQCard-700 DIO");
MODULE_LICENSE("GPL");

struct pcmcia_driver dio700_cs_driver = {
	.probe = dio700_cs_attach,
	.remove = dio700_cs_detach,
	.suspend = dio700_cs_suspend,
	.resume = dio700_cs_resume,
	.id_table = dio700_cs_ids,
	.owner = THIS_MODULE,
	.drv = {
		.name = "ni_daq_700",
		},
};

static int __init init_dio700_cs(void)
{
	pcmcia_register_driver(&dio700_cs_driver);
	return 0;
}

static void __exit exit_dio700_cs(void)
{
	pr_debug("ni_daq_700: unloading\n");
	pcmcia_unregister_driver(&dio700_cs_driver);
}

int __init init_module(void)
{
	int ret;

	ret = init_dio700_cs();
	if (ret < 0)
		return ret;

	return comedi_driver_register(&driver_dio700);
}

void __exit cleanup_module(void)
{
	exit_dio700_cs();
	comedi_driver_unregister(&driver_dio700);
}
