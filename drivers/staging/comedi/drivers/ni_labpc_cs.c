/*
    comedi/drivers/ni_labpc_cs.c
    Driver for National Instruments daqcard-1200 boards
    Copyright (C) 2001, 2002, 2003 Frank Mori Hess <fmhess@users.sourceforge.net>

    PCMCIA crap is adapted from dummy_cs.c 1.31 2001/08/24 12:13:13
    from the pcmcia package.
    The initial developer of the pcmcia dummy_cs.c code is David A. Hinds
    <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
    are Copyright (C) 1999 David A. Hinds.

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
Driver: ni_labpc_cs
Description: National Instruments Lab-PC (& compatibles)
Author: Frank Mori Hess <fmhess@users.sourceforge.net>
Devices: [National Instruments] DAQCard-1200 (daqcard-1200)
Status: works

Thanks go to Fredrik Lingvall for much testing and perseverance in
helping to debug daqcard-1200 support.

The 1200 series boards have onboard calibration dacs for correcting
analog input/output offsets and gains.  The proper settings for these
caldacs are stored on the board's eeprom.  To read the caldac values
from the eeprom and store them into a file that can be then be used by
comedilib, use the comedi_calibrate program.

Configuration options:
  none

The daqcard-1200 has quirky chanlist requirements
when scanning multiple channels.  Multiple channel scan
sequence must start at highest channel, then decrement down to
channel 0.  Chanlists consisting of all one channel
are also legal, and allow you to pace conversions in bursts.

*/

/*

NI manuals:
340988a (daqcard-1200)

*/

#undef LABPC_DEBUG  /* debugging messages */

#include "../comedidev.h"

#include <linux/delay.h>
#include <linux/slab.h>

#include "8253.h"
#include "8255.h"
#include "comedi_fc.h"
#include "ni_labpc.h"

#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

static struct pcmcia_device *pcmcia_cur_dev;

static int labpc_attach(struct comedi_device *dev, struct comedi_devconfig *it);

static const struct labpc_board_struct labpc_cs_boards[] = {
	{
	 .name = "daqcard-1200",
	 .device_id = 0x103,	/* 0x10b is manufacturer id,
				   0x103 is device id */
	 .ai_speed = 10000,
	 .bustype = pcmcia_bustype,
	 .register_layout = labpc_1200_layout,
	 .has_ao = 1,
	 .ai_range_table = &range_labpc_1200_ai,
	 .ai_range_code = labpc_1200_ai_gain_bits,
	 .ai_range_is_unipolar = labpc_1200_is_unipolar,
	 .ai_scan_up = 0,
	 .memory_mapped_io = 0,
	 },
	/* duplicate entry, to support using alternate name */
	{
	 .name = "ni_labpc_cs",
	 .device_id = 0x103,
	 .ai_speed = 10000,
	 .bustype = pcmcia_bustype,
	 .register_layout = labpc_1200_layout,
	 .has_ao = 1,
	 .ai_range_table = &range_labpc_1200_ai,
	 .ai_range_code = labpc_1200_ai_gain_bits,
	 .ai_range_is_unipolar = labpc_1200_is_unipolar,
	 .ai_scan_up = 0,
	 .memory_mapped_io = 0,
	 },
};

/*
 * Useful for shorthand access to the particular board structure
 */
#define thisboard ((const struct labpc_board_struct *)dev->board_ptr)

static struct comedi_driver driver_labpc_cs = {
	.driver_name = "ni_labpc_cs",
	.module = THIS_MODULE,
	.attach = &labpc_attach,
	.detach = &labpc_common_detach,
	.num_names = ARRAY_SIZE(labpc_cs_boards),
	.board_name = &labpc_cs_boards[0].name,
	.offset = sizeof(struct labpc_board_struct),
};

static int labpc_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	unsigned long iobase = 0;
	unsigned int irq = 0;
	struct pcmcia_device *link;

	/* allocate and initialize dev->private */
	if (alloc_private(dev, sizeof(struct labpc_private)) < 0)
		return -ENOMEM;

	/*  get base address, irq etc. based on bustype */
	switch (thisboard->bustype) {
	case pcmcia_bustype:
		link = pcmcia_cur_dev;	/* XXX hack */
		if (!link)
			return -EIO;
		iobase = link->resource[0]->start;
		irq = link->irq;
		break;
	default:
		printk("bug! couldn't determine board type\n");
		return -EINVAL;
		break;
	}
	return labpc_common_attach(dev, iobase, irq, 0);
}

/*====================================================================*/

/*
   The event() function is this driver's Card Services event handler.
   It will be called by Card Services when an appropriate card status
   event is received.  The config() and release() entry points are
   used to configure or release a socket, in response to card
   insertion and ejection events.  They are invoked from the dummy
   event handler.

   Kernel version 2.6.16 upwards uses suspend() and resume() functions
   instead of an event() function.
*/

static void labpc_config(struct pcmcia_device *link);
static void labpc_release(struct pcmcia_device *link);
static int labpc_cs_suspend(struct pcmcia_device *p_dev);
static int labpc_cs_resume(struct pcmcia_device *p_dev);

/*
   The attach() and detach() entry points are used to create and destroy
   "instances" of the driver, where each instance represents everything
   needed to manage one actual PCMCIA card.
*/

static int labpc_cs_attach(struct pcmcia_device *);
static void labpc_cs_detach(struct pcmcia_device *);

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

    labpc_cs_attach() creates an "instance" of the driver, allocating
    local data structures for one device.  The device is registered
    with Card Services.

    The dev_link structure is initialized, but we don't actually
    configure the card at this point -- we wait until we receive a
    card insertion event.

======================================================================*/

static int labpc_cs_attach(struct pcmcia_device *link)
{
	struct local_info_t *local;

	dev_dbg(&link->dev, "labpc_cs_attach()\n");

	/* Allocate space for private device-specific data */
	local = kzalloc(sizeof(struct local_info_t), GFP_KERNEL);
	if (!local)
		return -ENOMEM;
	local->link = link;
	link->priv = local;

	pcmcia_cur_dev = link;

	labpc_config(link);

	return 0;
}				/* labpc_cs_attach */

/*======================================================================

    This deletes a driver "instance".  The device is de-registered
    with Card Services.  If it has been released, all local data
    structures are freed.  Otherwise, the structures will be freed
    when the device is released.

======================================================================*/

static void labpc_cs_detach(struct pcmcia_device *link)
{
	dev_dbg(&link->dev, "labpc_cs_detach\n");

	/*
	   If the device is currently configured and active, we won't
	   actually delete it yet.  Instead, it is marked so that when
	   the release() function is called, that will trigger a proper
	   detach().
	 */
	((struct local_info_t *)link->priv)->stop = 1;
	labpc_release(link);

	/* This points to the parent local_info_t struct (may be null) */
	kfree(link->priv);

}				/* labpc_cs_detach */

/*======================================================================

    labpc_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    device available to the system.

======================================================================*/

static int labpc_pcmcia_config_loop(struct pcmcia_device *p_dev,
				void *priv_data)
{
	if (p_dev->config_index == 0)
		return -EINVAL;

	return pcmcia_request_io(p_dev);
}


static void labpc_config(struct pcmcia_device *link)
{
	int ret;

	dev_dbg(&link->dev, "labpc_config\n");

	link->config_flags |= CONF_ENABLE_IRQ | CONF_ENABLE_PULSE_IRQ |
		CONF_AUTO_AUDIO | CONF_AUTO_SET_IO;

	ret = pcmcia_loop_config(link, labpc_pcmcia_config_loop, NULL);
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
	ret = pcmcia_enable_device(link);
	if (ret)
		goto failed;

	return;

failed:
	labpc_release(link);

}				/* labpc_config */

static void labpc_release(struct pcmcia_device *link)
{
	dev_dbg(&link->dev, "labpc_release\n");

	pcmcia_disable_device(link);
}				/* labpc_release */

/*======================================================================

    The card status event handler.  Mostly, this schedules other
    stuff to run after an event is received.

    When a CARD_REMOVAL event is received, we immediately set a
    private flag to block future accesses to this device.  All the
    functions that actually access the device should check this flag
    to make sure the card is still present.

======================================================================*/

static int labpc_cs_suspend(struct pcmcia_device *link)
{
	struct local_info_t *local = link->priv;

	/* Mark the device as stopped, to block IO until later */
	local->stop = 1;
	return 0;
}				/* labpc_cs_suspend */

static int labpc_cs_resume(struct pcmcia_device *link)
{
	struct local_info_t *local = link->priv;

	local->stop = 0;
	return 0;
}				/* labpc_cs_resume */

/*====================================================================*/

static struct pcmcia_device_id labpc_cs_ids[] = {
	/* N.B. These IDs should match those in labpc_cs_boards (ni_labpc.c) */
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x0103),	/* daqcard-1200 */
	PCMCIA_DEVICE_NULL
};

MODULE_DEVICE_TABLE(pcmcia, labpc_cs_ids);
MODULE_AUTHOR("Frank Mori Hess <fmhess@users.sourceforge.net>");
MODULE_DESCRIPTION("Comedi driver for National Instruments Lab-PC");
MODULE_LICENSE("GPL");

struct pcmcia_driver labpc_cs_driver = {
	.probe = labpc_cs_attach,
	.remove = labpc_cs_detach,
	.suspend = labpc_cs_suspend,
	.resume = labpc_cs_resume,
	.id_table = labpc_cs_ids,
	.owner = THIS_MODULE,
	.drv = {
		.name = "daqcard-1200",
		},
};

static int __init init_labpc_cs(void)
{
	pcmcia_register_driver(&labpc_cs_driver);
	return 0;
}

static void __exit exit_labpc_cs(void)
{
	pcmcia_unregister_driver(&labpc_cs_driver);
}

int __init labpc_init_module(void)
{
	int ret;

	ret = init_labpc_cs();
	if (ret < 0)
		return ret;

	return comedi_driver_register(&driver_labpc_cs);
}

void __exit labpc_exit_module(void)
{
	exit_labpc_cs();
	comedi_driver_unregister(&driver_labpc_cs);
}

module_init(labpc_init_module);
module_exit(labpc_exit_module);
