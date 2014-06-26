/*
    comedi/drivers/ni_mio_cs.c
    Hardware driver for NI PCMCIA MIO E series cards

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*/
/*
Driver: ni_mio_cs
Description: National Instruments DAQCard E series
Author: ds
Status: works
Devices: [National Instruments] DAQCard-AI-16XE-50 (ni_mio_cs),
  DAQCard-AI-16E-4, DAQCard-6062E, DAQCard-6024E, DAQCard-6036E
Updated: Thu Oct 23 19:43:17 CDT 2003

See the notes in the ni_atmio.o driver.
*/
/*
	The real guts of the driver is in ni_mio_common.c, which is
	included by all the E series drivers.

	References for specifications:

	   341080a.pdf  DAQCard E Series Register Level Programmer Manual

*/

#include <linux/module.h>
#include "../comedidev.h"

#include <linux/delay.h>

#include "ni_stc.h"
#include "8255.h"

#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

#define ATMIO 1
#undef PCIMIO

/*
 *  AT specific setup
 */

static const struct ni_board_struct ni_boards[] = {
	{
		.name		= "DAQCard-ai-16xe-50",
		.device_id	= 0x010d,
		.n_adchan	= 16,
		.adbits		= 16,
		.ai_fifo_depth	= 1024,
		.gainlkup	= ai_gain_8,
		.ai_speed	= 5000,
		.num_p0_dio_channels = 8,
		.caldac		= { dac8800, dac8043 },
	}, {
		.name		= "DAQCard-ai-16e-4",
		.device_id	= 0x010c,
		.n_adchan	= 16,
		.adbits		= 12,
		.ai_fifo_depth	= 1024,
		.gainlkup	= ai_gain_16,
		.ai_speed	= 4000,
		.num_p0_dio_channels = 8,
		.caldac		= { mb88341 },		/* verified */
	}, {
		.name		= "DAQCard-6062E",
		.device_id	= 0x02c4,
		.n_adchan	= 16,
		.adbits		= 12,
		.ai_fifo_depth	= 8192,
		.gainlkup	= ai_gain_16,
		.ai_speed	= 2000,
		.n_aochan	= 2,
		.aobits		= 12,
		.ao_fifo_depth	= 2048,
		.ao_range_table	= &range_bipolar10,
		.ao_speed	= 1176,
		.num_p0_dio_channels = 8,
		.caldac		= { ad8804_debug },	/* verified */
	 }, {
		/* specs incorrect! */
		.name		= "DAQCard-6024E",
		.device_id	= 0x075e,
		.n_adchan	= 16,
		.adbits		= 12,
		.ai_fifo_depth	= 1024,
		.gainlkup	= ai_gain_4,
		.ai_speed	= 5000,
		.n_aochan	= 2,
		.aobits		= 12,
		.ao_range_table	= &range_bipolar10,
		.ao_speed	= 1000000,
		.num_p0_dio_channels = 8,
		.caldac		= { ad8804_debug },
	}, {
		/* specs incorrect! */
		.name		= "DAQCard-6036E",
		.device_id	= 0x0245,
		.n_adchan	= 16,
		.adbits		= 16,
		.ai_fifo_depth	= 1024,
		.alwaysdither	= 1,
		.gainlkup	= ai_gain_4,
		.ai_speed	= 5000,
		.n_aochan	= 2,
		.aobits		= 16,
		.ao_range_table	= &range_bipolar10,
		.ao_speed	= 1000000,
		.num_p0_dio_channels = 8,
		.caldac		= { ad8804_debug },
	 },
#if 0
	{
		.name		= "DAQCard-6715",
		.device_id	= 0x0000,	/* unknown */
		.n_aochan	= 8,
		.aobits		= 12,
		.ao_671x	= 8192,
		.num_p0_dio_channels = 8,
		.caldac		= { mb88341, mb88341 },
	},
#endif
};

#include "ni_mio_common.c"

static const void *ni_getboardtype(struct comedi_device *dev,
				   struct pcmcia_device *link)
{
	static const struct ni_board_struct *board;
	int i;

	for (i = 0; i < ARRAY_SIZE(ni_boards); i++) {
		board = &ni_boards[i];
		if (board->device_id == link->card_id)
			return board;
	}
	return NULL;
}

static int mio_pcmcia_config_loop(struct pcmcia_device *p_dev, void *priv_data)
{
	int base, ret;

	p_dev->resource[0]->flags &= ~IO_DATA_PATH_WIDTH;
	p_dev->resource[0]->flags |= IO_DATA_PATH_WIDTH_16;

	for (base = 0x000; base < 0x400; base += 0x20) {
		p_dev->resource[0]->start = base;
		ret = pcmcia_request_io(p_dev);
		if (!ret)
			return 0;
	}
	return -ENODEV;
}

static int mio_cs_auto_attach(struct comedi_device *dev,
			      unsigned long context)
{
	struct pcmcia_device *link = comedi_to_pcmcia_dev(dev);
	static const struct ni_board_struct *board;
	struct ni_private *devpriv;
	int ret;

	board = ni_getboardtype(dev, link);
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	link->config_flags |= CONF_AUTO_SET_IO | CONF_ENABLE_IRQ;
	ret = comedi_pcmcia_enable(dev, mio_pcmcia_config_loop);
	if (ret)
		return ret;
	dev->iobase = link->resource[0]->start;

	link->priv = dev;
	ret = pcmcia_request_irq(link, ni_E_interrupt);
	if (ret)
		return ret;
	dev->irq = link->irq;

	ret = ni_alloc_private(dev);
	if (ret)
		return ret;

	devpriv = dev->private;

	return ni_E_init(dev, 0, 1);
}

static void mio_cs_detach(struct comedi_device *dev)
{
	mio_common_detach(dev);
	comedi_pcmcia_disable(dev);
}

static struct comedi_driver driver_ni_mio_cs = {
	.driver_name	= "ni_mio_cs",
	.module		= THIS_MODULE,
	.auto_attach	= mio_cs_auto_attach,
	.detach		= mio_cs_detach,
};

static int cs_attach(struct pcmcia_device *link)
{
	return comedi_pcmcia_auto_config(link, &driver_ni_mio_cs);
}

static const struct pcmcia_device_id ni_mio_cs_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x010d),	/* DAQCard-ai-16xe-50 */
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x010c),	/* DAQCard-ai-16e-4 */
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x02c4),	/* DAQCard-6062E */
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x075e),	/* DAQCard-6024E */
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x0245),	/* DAQCard-6036E */
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, ni_mio_cs_ids);

static struct pcmcia_driver ni_mio_cs_driver = {
	.name		= "ni_mio_cs",
	.owner		= THIS_MODULE,
	.id_table	= ni_mio_cs_ids,
	.probe		= cs_attach,
	.remove		= comedi_pcmcia_auto_unconfig,
};
module_comedi_pcmcia_driver(driver_ni_mio_cs, ni_mio_cs_driver);

MODULE_DESCRIPTION("Comedi driver for National Instruments DAQCard E series");
MODULE_AUTHOR("David A. Schleef <ds@schleef.org>");
MODULE_LICENSE("GPL");
