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

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

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

#include "../comedidev.h"

#include <linux/delay.h>

#include "ni_stc.h"
#include "8255.h"

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
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
	{.device_id = 0x010d,
	 .name = "DAQCard-ai-16xe-50",
	 .n_adchan = 16,
	 .adbits = 16,
	 .ai_fifo_depth = 1024,
	 .alwaysdither = 0,
	 .gainlkup = ai_gain_8,
	 .ai_speed = 5000,
	 .n_aochan = 0,
	 .aobits = 0,
	 .ao_fifo_depth = 0,
	 .ao_unipolar = 0,
	 .num_p0_dio_channels = 8,
	 .has_8255 = 0,
	 .caldac = {dac8800, dac8043},
	},
	{.device_id = 0x010c,
	 .name = "DAQCard-ai-16e-4",
	 .n_adchan = 16,
	 .adbits = 12,
	 .ai_fifo_depth = 1024,
	 .alwaysdither = 0,
	 .gainlkup = ai_gain_16,
	 .ai_speed = 4000,
	 .n_aochan = 0,
	 .aobits = 0,
	 .ao_fifo_depth = 0,
	 .ao_unipolar = 0,
	 .num_p0_dio_channels = 8,
	 .has_8255 = 0,
	 .caldac = {mb88341},	/* verified */
	},
	{.device_id = 0x02c4,
	 .name = "DAQCard-6062E",
	 .n_adchan = 16,
	 .adbits = 12,
	 .ai_fifo_depth = 8192,
	 .alwaysdither = 0,
	 .gainlkup = ai_gain_16,
	 .ai_speed = 2000,
	 .n_aochan = 2,
	 .aobits = 12,
	 .ao_fifo_depth = 2048,
	 .ao_range_table = &range_bipolar10,
	 .ao_unipolar = 0,
	 .ao_speed = 1176,
	 .num_p0_dio_channels = 8,
	 .has_8255 = 0,
	 .caldac = {ad8804_debug},	/* verified */
	},
	{.device_id = 0x075e,
	 .name = "DAQCard-6024E",	/* specs incorrect! */
	 .n_adchan = 16,
	 .adbits = 12,
	 .ai_fifo_depth = 1024,
	 .alwaysdither = 0,
	 .gainlkup = ai_gain_16,
	 .ai_speed = 5000,
	 .n_aochan = 2,
	 .aobits = 12,
	 .ao_fifo_depth = 0,
	 .ao_range_table = &range_bipolar10,
	 .ao_unipolar = 0,
	 .ao_speed = 1000000,
	 .num_p0_dio_channels = 8,
	 .has_8255 = 0,
	 .caldac = {ad8804_debug},
	},
	{.device_id = 0x0245,
	 .name = "DAQCard-6036E",	/* specs incorrect! */
	 .n_adchan = 16,
	 .adbits = 16,
	 .ai_fifo_depth = 1024,
	 .alwaysdither = 1,
	 .gainlkup = ai_gain_4,
	 .ai_speed = 5000,
	 .n_aochan = 2,
	 .aobits = 16,
	 .ao_fifo_depth = 0,
	 .ao_range_table = &range_bipolar10,
	 .ao_unipolar = 0,
	 .ao_speed = 1000000,
	 .num_p0_dio_channels = 8,
	 .has_8255 = 0,
	 .caldac = {ad8804_debug},
	},
#if 0
	{.device_id = 0x0000,	/* unknown */
	 .name = "DAQCard-6715",
	 .n_adchan = 0,
	 .n_aochan = 8,
	 .aobits = 12,
	 .ao_671x = 8192,
	 .num_p0_dio_channels = 8,
	 .caldac = {mb88341, mb88341},
	},
#endif
	/* N.B. Update ni_mio_cs_ids[] when entries added above. */
};

#define interrupt_pin(a)	0

#define IRQ_POLARITY 1

#define NI_E_IRQ_FLAGS		IRQF_SHARED

struct ni_private {

	struct pcmcia_device *link;

 NI_PRIVATE_COMMON};

#define devpriv ((struct ni_private *)dev->private)

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
	unsigned long flags;

	comedi_spin_lock_irqsave(&devpriv->window_lock, flags);
	if (addr < 8) {
		ni_writew(data, addr * 2);
	} else {
		ni_writew(addr, Window_Address);
		ni_writew(data, Window_Data);
	}
	comedi_spin_unlock_irqrestore(&devpriv->window_lock, flags);
}

static uint16_t mio_cs_win_in(struct comedi_device *dev, int addr)
{
	unsigned long flags;
	uint16_t ret;

	comedi_spin_lock_irqsave(&devpriv->window_lock, flags);
	if (addr < 8) {
		ret = ni_readw(addr * 2);
	} else {
		ni_writew(addr, Window_Address);
		ret = ni_readw(Window_Data);
	}
	comedi_spin_unlock_irqrestore(&devpriv->window_lock, flags);

	return ret;
}

static int mio_cs_attach(struct comedi_device *dev, struct comedi_devconfig *it);
static int mio_cs_detach(struct comedi_device *dev);
static struct comedi_driver driver_ni_mio_cs = {
	.driver_name = "ni_mio_cs",
	.module = THIS_MODULE,
	.attach = mio_cs_attach,
	.detach = mio_cs_detach,
};

#include "ni_mio_common.c"

static int ni_getboardtype(struct comedi_device *dev, struct pcmcia_device *link);

/* clean up allocated resources */
/* called when driver is removed */
static int mio_cs_detach(struct comedi_device *dev)
{
	mio_common_detach(dev);

	/* PCMCIA layer frees the IO region */

	if (dev->irq) {
		comedi_free_irq(dev->irq, dev);
	}

	return 0;
}

static void mio_cs_config(struct pcmcia_device *link);
static void cs_release(struct pcmcia_device *link);
static void cs_detach(struct pcmcia_device *);

static struct pcmcia_device *cur_dev = NULL;
static const dev_info_t dev_info = "ni_mio_cs";
static dev_node_t dev_node = {
	"ni_mio_cs",
	COMEDI_MAJOR, 0,
	NULL
};
static int cs_attach(struct pcmcia_device *link)
{
	link->io.Attributes1 = IO_DATA_PATH_WIDTH_16;
	link->io.NumPorts1 = 16;
	link->irq.Attributes = IRQ_TYPE_EXCLUSIVE;
	link->irq.IRQInfo1 = IRQ_LEVEL_ID;
	link->conf.Attributes = CONF_ENABLE_IRQ;
	link->conf.IntType = INT_MEMORY_AND_IO;

	cur_dev = link;

	mio_cs_config(link);

	return 0;
}

static void cs_release(struct pcmcia_device *link)
{
	pcmcia_disable_device(link);
}

static void cs_detach(struct pcmcia_device *link)
{
	DPRINTK("cs_detach(link=%p)\n", link);

	if (link->dev_node) {
		cs_release(link);
	}
}

static int mio_cs_suspend(struct pcmcia_device *link)
{
	DPRINTK("pm suspend\n");

	return 0;
}

static int mio_cs_resume(struct pcmcia_device *link)
{
	DPRINTK("pm resume\n");
	return 0;
}

static void mio_cs_config(struct pcmcia_device *link)
{
	tuple_t tuple;
	u_short buf[128];
	cisparse_t parse;
	int manfid = 0, prodid = 0;
	int ret;

	DPRINTK("mio_cs_config(link=%p)\n", link);

	tuple.TupleData = (cisdata_t *) buf;
	tuple.TupleOffset = 0;
	tuple.TupleDataMax = 255;
	tuple.Attributes = 0;

	tuple.DesiredTuple = CISTPL_CONFIG;
	ret = pcmcia_get_first_tuple(link, &tuple);
	ret = pcmcia_get_tuple_data(link, &tuple);
	ret = pcmcia_parse_tuple(&tuple, &parse);
	link->conf.ConfigBase = parse.config.base;
	link->conf.Present = parse.config.rmask[0];

#if 0
	tuple.DesiredTuple = CISTPL_LONGLINK_MFC;
	tuple.Attributes = TUPLE_RETURN_COMMON | TUPLE_RETURN_LINK;
	info->multi(first_tuple(link, &tuple, &parse) == 0);
#endif

	tuple.DesiredTuple = CISTPL_MANFID;
	tuple.Attributes = TUPLE_RETURN_COMMON;
	if ((pcmcia_get_first_tuple(link, &tuple) == 0) &&
		(pcmcia_get_tuple_data(link, &tuple) == 0)) {
		manfid = le16_to_cpu(buf[0]);
		prodid = le16_to_cpu(buf[1]);
	}
	/* printk("manfid = 0x%04x, 0x%04x\n",manfid,prodid); */

	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
	tuple.Attributes = 0;
	ret = pcmcia_get_first_tuple(link, &tuple);
	ret = pcmcia_get_tuple_data(link, &tuple);
	ret = pcmcia_parse_tuple(&tuple, &parse);

#if 0
	printk(" index: 0x%x\n", parse.cftable_entry.index);
	printk(" flags: 0x%x\n", parse.cftable_entry.flags);
	printk(" io flags: 0x%x\n", parse.cftable_entry.io.flags);
	printk(" io nwin: 0x%x\n", parse.cftable_entry.io.nwin);
	printk(" io base: 0x%x\n", parse.cftable_entry.io.win[0].base);
	printk(" io len: 0x%x\n", parse.cftable_entry.io.win[0].len);
	printk(" irq1: 0x%x\n", parse.cftable_entry.irq.IRQInfo1);
	printk(" irq2: 0x%x\n", parse.cftable_entry.irq.IRQInfo2);
	printk(" mem flags: 0x%x\n", parse.cftable_entry.mem.flags);
	printk(" mem nwin: 0x%x\n", parse.cftable_entry.mem.nwin);
	printk(" subtuples: 0x%x\n", parse.cftable_entry.subtuples);
#endif

#if 0
	link->io.NumPorts1 = 0x20;
	link->io.IOAddrLines = 5;
	link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
#endif
	link->io.NumPorts1 = parse.cftable_entry.io.win[0].len;
	link->io.IOAddrLines =
		parse.cftable_entry.io.flags & CISTPL_IO_LINES_MASK;
	link->io.NumPorts2 = 0;

	{
		int base;
		for (base = 0x000; base < 0x400; base += 0x20) {
			link->io.BasePort1 = base;
			ret = pcmcia_request_io(link, &link->io);
			/* printk("RequestIO 0x%02x\n",ret); */
			if (!ret)
				break;
		}
	}

	link->irq.IRQInfo1 = parse.cftable_entry.irq.IRQInfo1;
	link->irq.IRQInfo2 = parse.cftable_entry.irq.IRQInfo2;
	ret = pcmcia_request_irq(link, &link->irq);
	if (ret) {
		printk("pcmcia_request_irq() returned error: %i\n", ret);
	}
	/* printk("RequestIRQ 0x%02x\n",ret); */

	link->conf.ConfigIndex = 1;

	ret = pcmcia_request_configuration(link, &link->conf);
	/* printk("RequestConfiguration %d\n",ret); */

	link->dev_node = &dev_node;
}

static int mio_cs_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct pcmcia_device *link;
	unsigned int irq;
	int ret;

	DPRINTK("mio_cs_attach(dev=%p,it=%p)\n", dev, it);

	link = cur_dev;		/* XXX hack */
	if (!link)
		return -EIO;

	dev->driver = &driver_ni_mio_cs;
	dev->iobase = link->io.BasePort1;

	irq = link->irq.AssignedIRQ;

	printk("comedi%d: %s: DAQCard: io 0x%04lx, irq %u, ",
		dev->minor, dev->driver->driver_name, dev->iobase, irq);

#if 0
	{
		int i;

		printk(" board fingerprint:");
		for (i = 0; i < 32; i += 2) {
			printk(" %04x %02x", inw(dev->iobase + i),
				inb(dev->iobase + i + 1));
		}
		printk("\n");
		printk(" board fingerprint (windowed):");
		for (i = 0; i < 10; i++) {
			printk(" 0x%04x", win_in(i));
		}
		printk("\n");
	}
#endif

	dev->board_ptr = ni_boards + ni_getboardtype(dev, link);

	printk(" %s", boardtype.name);
	dev->board_name = boardtype.name;

	ret = comedi_request_irq(irq, ni_E_interrupt, NI_E_IRQ_FLAGS,
				 "ni_mio_cs", dev);
	if (ret < 0) {
		printk(" irq not available\n");
		return -EINVAL;
	}
	dev->irq = irq;

	/* allocate private area */
	ret = ni_alloc_private(dev);
	if (ret < 0)
		return ret;

	devpriv->stc_writew = &mio_cs_win_out;
	devpriv->stc_readw = &mio_cs_win_in;
	devpriv->stc_writel = &win_out2;
	devpriv->stc_readl = &win_in2;

	ret = ni_E_init(dev, it);

	if (ret < 0)
		return ret;

	return 0;
}

static int get_prodid(struct comedi_device *dev, struct pcmcia_device *link)
{
	tuple_t tuple;
	u_short buf[128];
	int prodid = 0;

	tuple.TupleData = (cisdata_t *) buf;
	tuple.TupleOffset = 0;
	tuple.TupleDataMax = 255;
	tuple.DesiredTuple = CISTPL_MANFID;
	tuple.Attributes = TUPLE_RETURN_COMMON;
	if ((pcmcia_get_first_tuple(link, &tuple) == 0) &&
		(pcmcia_get_tuple_data(link, &tuple) == 0)) {
		prodid = le16_to_cpu(buf[1]);
	}

	return prodid;
}

static int ni_getboardtype(struct comedi_device *dev, struct pcmcia_device *link)
{
	int id;
	int i;

	id = get_prodid(dev, link);

	for (i = 0; i < n_ni_boards; i++) {
		if (ni_boards[i].device_id == id) {
			return i;
		}
	}

	printk("unknown board 0x%04x -- pretend it is a ", id);

	return 0;
}

#ifdef MODULE

MODULE_LICENSE("GPL");

static struct pcmcia_device_id ni_mio_cs_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x010d),	/* DAQCard-ai-16xe-50 */
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x010c),	/* DAQCard-ai-16e-4 */
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x02c4),	/* DAQCard-6062E */
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x075e),	/* DAQCard-6024E */
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x0245),	/* DAQCard-6036E */
	PCMCIA_DEVICE_NULL
};

MODULE_DEVICE_TABLE(pcmcia, ni_mio_cs_ids);

struct pcmcia_driver ni_mio_cs_driver = {
	.probe = &cs_attach,
	.remove = &cs_detach,
	.suspend = &mio_cs_suspend,
	.resume = &mio_cs_resume,
	.id_table = ni_mio_cs_ids,
	.owner = THIS_MODULE,
	.drv = {
			.name = dev_info,
		},
};

int init_module(void)
{
	pcmcia_register_driver(&ni_mio_cs_driver);
	comedi_driver_register(&driver_ni_mio_cs);
	return 0;
}

void cleanup_module(void)
{
	pcmcia_unregister_driver(&ni_mio_cs_driver);
#if 0
	while (cur_dev != NULL)
		cs_detach(cur_dev->handle);
#endif
	comedi_driver_unregister(&driver_ni_mio_cs);
}
#endif
