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
	struct labpc_private *devpriv;
	unsigned long iobase = 0;
	unsigned int irq = 0;
	struct pcmcia_device *link;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

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
		pr_err("bug! couldn't determine board type\n");
		return -EINVAL;
		break;
	}
	return labpc_common_attach(dev, iobase, irq, 0);
}

struct local_info_t {
	struct pcmcia_device *link;
	struct bus_operations *bus;
};

static int labpc_pcmcia_config_loop(struct pcmcia_device *p_dev,
				void *priv_data)
{
	if (p_dev->config_index == 0)
		return -EINVAL;

	return pcmcia_request_io(p_dev);
}

static int labpc_cs_attach(struct pcmcia_device *link)
{
	struct local_info_t *local;
	int ret;

	local = kzalloc(sizeof(*local), GFP_KERNEL);
	if (!local)
		return -ENOMEM;
	local->link = link;
	link->priv = local;

	pcmcia_cur_dev = link;

	link->config_flags |= CONF_ENABLE_IRQ | CONF_ENABLE_PULSE_IRQ |
		CONF_AUTO_AUDIO | CONF_AUTO_SET_IO;

	ret = pcmcia_loop_config(link, labpc_pcmcia_config_loop, NULL);
	if (ret) {
		dev_warn(&link->dev, "no configuration found\n");
		goto failed;
	}

	if (!link->irq)
		goto failed;

	ret = pcmcia_enable_device(link);
	if (ret)
		goto failed;

	return 0;

failed:
	pcmcia_disable_device(link);
	return ret;
}

static void labpc_cs_detach(struct pcmcia_device *link)
{
	pcmcia_disable_device(link);

	/* This points to the parent local_info_t struct (may be null) */
	kfree(link->priv);
}

static const struct pcmcia_device_id labpc_cs_ids[] = {
	/* N.B. These IDs should match those in labpc_cs_boards (ni_labpc.c) */
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x0103),	/* daqcard-1200 */
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, labpc_cs_ids);

static struct pcmcia_driver labpc_cs_driver = {
	.name		= "daqcard-1200",
	.owner		= THIS_MODULE,
	.id_table	= labpc_cs_ids,
	.probe		= labpc_cs_attach,
	.remove		= labpc_cs_detach,
};
module_comedi_pcmcia_driver(driver_labpc_cs, labpc_cs_driver);

MODULE_DESCRIPTION("Comedi driver for National Instruments Lab-PC");
MODULE_AUTHOR("Frank Mori Hess <fmhess@users.sourceforge.net>");
MODULE_LICENSE("GPL");
