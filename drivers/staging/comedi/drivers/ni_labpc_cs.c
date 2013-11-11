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

static const struct labpc_boardinfo labpc_cs_boards[] = {
	{
		.name			= "daqcard-1200",
		.device_id		= 0x103,
		.ai_speed		= 10000,
		.register_layout	= labpc_1200_layout,
		.has_ao			= 1,
		.ai_range_table		= &range_labpc_1200_ai,
		.ai_range_code		= labpc_1200_ai_gain_bits,
	},
};

static int labpc_auto_attach(struct comedi_device *dev,
			     unsigned long context)
{
	struct pcmcia_device *link = comedi_to_pcmcia_dev(dev);
	struct labpc_private *devpriv;
	int ret;

	/* The ni_labpc driver needs the board_ptr */
	dev->board_ptr = &labpc_cs_boards[0];

	link->config_flags |= CONF_AUTO_SET_IO |
			      CONF_ENABLE_IRQ | CONF_ENABLE_PULSE_IRQ;
	ret = comedi_pcmcia_enable(dev, NULL);
	if (ret)
		return ret;
	dev->iobase = link->resource[0]->start;

	if (!link->irq)
		return -EINVAL;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	return labpc_common_attach(dev, link->irq, IRQF_SHARED);
}

static void labpc_detach(struct comedi_device *dev)
{
	labpc_common_detach(dev);
	comedi_pcmcia_disable(dev);
}

static struct comedi_driver driver_labpc_cs = {
	.driver_name	= "ni_labpc_cs",
	.module		= THIS_MODULE,
	.auto_attach	= labpc_auto_attach,
	.detach		= labpc_detach,
};

static int labpc_cs_attach(struct pcmcia_device *link)
{
	return comedi_pcmcia_auto_config(link, &driver_labpc_cs);
}

static const struct pcmcia_device_id labpc_cs_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x0103),	/* daqcard-1200 */
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, labpc_cs_ids);

static struct pcmcia_driver labpc_cs_driver = {
	.name		= "daqcard-1200",
	.owner		= THIS_MODULE,
	.id_table	= labpc_cs_ids,
	.probe		= labpc_cs_attach,
	.remove		= comedi_pcmcia_auto_unconfig,
};
module_comedi_pcmcia_driver(driver_labpc_cs, labpc_cs_driver);

MODULE_DESCRIPTION("Comedi driver for National Instruments Lab-PC");
MODULE_AUTHOR("Frank Mori Hess <fmhess@users.sourceforge.net>");
MODULE_LICENSE("GPL");
