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

#undef DEBUG

#define ATMIO 1
#undef PCIMIO

/*
 *  AT specific setup
 */

#define NI_SIZE 0x20

#define MAX_N_CALDACS 32

static const struct ni_board_struct ni_boards[] = {
	{
		.device_id	= 0x010d,
		.name		= "DAQCard-ai-16xe-50",
		.n_adchan	= 16,
		.adbits		= 16,
		.ai_fifo_depth	= 1024,
		.gainlkup	= ai_gain_8,
		.ai_speed	= 5000,
		.num_p0_dio_channels = 8,
		.caldac		= { dac8800, dac8043 },
	}, {
		.device_id	= 0x010c,
		.name		= "DAQCard-ai-16e-4",
		.n_adchan	= 16,
		.adbits		= 12,
		.ai_fifo_depth	= 1024,
		.gainlkup	= ai_gain_16,
		.ai_speed	= 4000,
		.num_p0_dio_channels = 8,
		.caldac		= { mb88341 },		/* verified */
	}, {
		.device_id	= 0x02c4,
		.name		= "DAQCard-6062E",
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
		.device_id	= 0x075e,
		.name		= "DAQCard-6024E",
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
		.device_id	= 0x0245,
		.name		= "DAQCard-6036E",
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
		.device_id	= 0x0000,	/* unknown */
		.name		= "DAQCard-6715",
		.n_aochan	= 8,
		.aobits		= 12,
		.ao_671x	= 8192,
		.num_p0_dio_channels = 8,
		.caldac		= { mb88341, mb88341 },
	},
#endif
};

#define interrupt_pin(a)	0

#define IRQ_POLARITY 1

struct ni_private {

	struct pcmcia_device *link;

NI_PRIVATE_COMMON};

/* How we access registers */

#define ni_writel(a, b)		(outl((a), (b)+dev->iobase))
#define ni_readl(a)		(inl((a)+dev->iobase))
#define ni_writew(a, b)		(outw((a), (b)+dev->iobase))
#define ni_readw(a)		(inw((a)+dev->iobase))
#define ni_writeb(a, b)		(outb((a), (b)+dev->iobase))
#define ni_readb(a)		(inb((a)+dev->iobase))

/* How we access windowed registers */

/* We automatically take advantage of STC registers that can be
 * read/written directly in the I/O space of the board.  The
 * DAQCard devices map the low 8 STC registers to iobase+addr*2. */

static void mio_cs_win_out(struct comedi_device *dev, uint16_t data, int addr)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->window_lock, flags);
	if (addr < 8) {
		ni_writew(data, addr * 2);
	} else {
		ni_writew(addr, Window_Address);
		ni_writew(data, Window_Data);
	}
	spin_unlock_irqrestore(&devpriv->window_lock, flags);
}

static uint16_t mio_cs_win_in(struct comedi_device *dev, int addr)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;
	uint16_t ret;

	spin_lock_irqsave(&devpriv->window_lock, flags);
	if (addr < 8) {
		ret = ni_readw(addr * 2);
	} else {
		ni_writew(addr, Window_Address);
		ret = ni_readw(Window_Data);
	}
	spin_unlock_irqrestore(&devpriv->window_lock, flags);

	return ret;
}

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
	devpriv->stc_writew	= mio_cs_win_out;
	devpriv->stc_readw	= mio_cs_win_in;
	devpriv->stc_writel	= win_out2;
	devpriv->stc_readl	= win_in2;

	return ni_E_init(dev);
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
