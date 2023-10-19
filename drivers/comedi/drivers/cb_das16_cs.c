// SPDX-License-Identifier: GPL-2.0+
/*
 * cb_das16_cs.c
 * Driver for Computer Boards PC-CARD DAS16/16.
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000, 2001, 2002 David A. Schleef <ds@schleef.org>
 *
 * PCMCIA support code for this driver is adapted from the dummy_cs.c
 * driver of the Linux PCMCIA Card Services package.
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 */

/*
 * Driver: cb_das16_cs
 * Description: Computer Boards PC-CARD DAS16/16
 * Devices: [ComputerBoards] PC-CARD DAS16/16 (cb_das16_cs),
 *   PC-CARD DAS16/16-AO
 * Author: ds
 * Updated: Mon, 04 Nov 2002 20:04:21 -0800
 * Status: experimental
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/comedi/comedi_pcmcia.h>
#include <linux/comedi/comedi_8254.h>

/*
 * Register I/O map
 */
#define DAS16CS_AI_DATA_REG		0x00
#define DAS16CS_AI_MUX_REG		0x02
#define DAS16CS_AI_MUX_HI_CHAN(x)	(((x) & 0xf) << 4)
#define DAS16CS_AI_MUX_LO_CHAN(x)	(((x) & 0xf) << 0)
#define DAS16CS_AI_MUX_SINGLE_CHAN(x)	(DAS16CS_AI_MUX_HI_CHAN(x) |	\
					 DAS16CS_AI_MUX_LO_CHAN(x))
#define DAS16CS_MISC1_REG		0x04
#define DAS16CS_MISC1_INTE		BIT(15)	/* 1=enable; 0=disable */
#define DAS16CS_MISC1_INT_SRC(x)	(((x) & 0x7) << 12) /* interrupt src */
#define DAS16CS_MISC1_INT_SRC_NONE	DAS16CS_MISC1_INT_SRC(0)
#define DAS16CS_MISC1_INT_SRC_PACER	DAS16CS_MISC1_INT_SRC(1)
#define DAS16CS_MISC1_INT_SRC_EXT	DAS16CS_MISC1_INT_SRC(2)
#define DAS16CS_MISC1_INT_SRC_FNE	DAS16CS_MISC1_INT_SRC(3)
#define DAS16CS_MISC1_INT_SRC_FHF	DAS16CS_MISC1_INT_SRC(4)
#define DAS16CS_MISC1_INT_SRC_EOS	DAS16CS_MISC1_INT_SRC(5)
#define DAS16CS_MISC1_INT_SRC_MASK	DAS16CS_MISC1_INT_SRC(7)
#define DAS16CS_MISC1_OVR		BIT(10)	/* ro - 1=FIFO overflow */
#define DAS16CS_MISC1_AI_CONV(x)	(((x) & 0x3) << 8) /* AI convert src */
#define DAS16CS_MISC1_AI_CONV_SW	DAS16CS_MISC1_AI_CONV(0)
#define DAS16CS_MISC1_AI_CONV_EXT_NEG	DAS16CS_MISC1_AI_CONV(1)
#define DAS16CS_MISC1_AI_CONV_EXT_POS	DAS16CS_MISC1_AI_CONV(2)
#define DAS16CS_MISC1_AI_CONV_PACER	DAS16CS_MISC1_AI_CONV(3)
#define DAS16CS_MISC1_AI_CONV_MASK	DAS16CS_MISC1_AI_CONV(3)
#define DAS16CS_MISC1_EOC		BIT(7)	/* ro - 0=busy; 1=ready */
#define DAS16CS_MISC1_SEDIFF		BIT(5)	/* 0=diff; 1=se */
#define DAS16CS_MISC1_INTB		BIT(4)	/* ro - 0=latched; 1=cleared */
#define DAS16CS_MISC1_MA_MASK		(0xf << 0) /* ro - current ai mux */
#define DAS16CS_MISC1_DAC1CS		BIT(3)	/* wo - DAC1 chip select */
#define DAS16CS_MISC1_DACCLK		BIT(2)	/* wo - Serial DAC clock */
#define DAS16CS_MISC1_DACSD		BIT(1)	/* wo - Serial DAC data */
#define DAS16CS_MISC1_DAC0CS		BIT(0)	/* wo - DAC0 chip select */
#define DAS16CS_MISC1_DAC_MASK		(0x0f << 0)
#define DAS16CS_MISC2_REG		0x06
#define DAS16CS_MISC2_BME		BIT(14)	/* 1=burst enable; 0=disable */
#define DAS16CS_MISC2_AI_GAIN(x)	(((x) & 0xf) << 8) /* AI gain */
#define DAS16CS_MISC2_AI_GAIN_1		DAS16CS_MISC2_AI_GAIN(4) /* +/-10V */
#define DAS16CS_MISC2_AI_GAIN_2		DAS16CS_MISC2_AI_GAIN(0) /* +/-5V */
#define DAS16CS_MISC2_AI_GAIN_4		DAS16CS_MISC2_AI_GAIN(1) /* +/-2.5V */
#define DAS16CS_MISC2_AI_GAIN_8		DAS16CS_MISC2_AI_GAIN(2) /* +-1.25V */
#define DAS16CS_MISC2_AI_GAIN_MASK	DAS16CS_MISC2_AI_GAIN(0xf)
#define DAS16CS_MISC2_UDIR		BIT(7)	/* 1=dio7:4 output; 0=input */
#define DAS16CS_MISC2_LDIR		BIT(6)	/* 1=dio3:0 output; 0=input */
#define DAS16CS_MISC2_TRGPOL		BIT(5)	/* 1=active lo; 0=hi */
#define DAS16CS_MISC2_TRGSEL		BIT(4)	/* 1=edge; 0=level */
#define DAS16CS_MISC2_FFNE		BIT(3)	/* ro - 1=FIFO not empty */
#define DAS16CS_MISC2_TRGCLR		BIT(3)	/* wo - 1=clr (monstable) */
#define DAS16CS_MISC2_CLK2		BIT(2)	/* 1=10 MHz; 0=1 MHz */
#define DAS16CS_MISC2_CTR1		BIT(1)	/* 1=int. 100 kHz; 0=ext. clk */
#define DAS16CS_MISC2_TRG0		BIT(0)	/* 1=enable; 0=disable */
#define DAS16CS_TIMER_BASE		0x08
#define DAS16CS_DIO_REG			0x10

struct das16cs_board {
	const char *name;
	int device_id;
	unsigned int has_ao:1;
	unsigned int has_4dio:1;
};

static const struct das16cs_board das16cs_boards[] = {
	{
		.name		= "PC-CARD DAS16/16-AO",
		.device_id	= 0x0039,
		.has_ao		= 1,
		.has_4dio	= 1,
	}, {
		.name		= "PCM-DAS16s/16",
		.device_id	= 0x4009,
	}, {
		.name		= "PC-CARD DAS16/16",
		.device_id	= 0x0000,	/* unknown */
	},
};

struct das16cs_private {
	unsigned short misc1;
	unsigned short misc2;
};

static const struct comedi_lrange das16cs_ai_range = {
	4, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
	}
};

static int das16cs_ai_eoc(struct comedi_device *dev,
			  struct comedi_subdevice *s,
			  struct comedi_insn *insn,
			  unsigned long context)
{
	unsigned int status;

	status = inw(dev->iobase + DAS16CS_MISC1_REG);
	if (status & DAS16CS_MISC1_EOC)
		return 0;
	return -EBUSY;
}

static int das16cs_ai_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct das16cs_private *devpriv = dev->private;
	int chan = CR_CHAN(insn->chanspec);
	int range = CR_RANGE(insn->chanspec);
	int aref = CR_AREF(insn->chanspec);
	int ret;
	int i;

	outw(DAS16CS_AI_MUX_SINGLE_CHAN(chan),
	     dev->iobase + DAS16CS_AI_MUX_REG);

	/* disable interrupts, software convert */
	devpriv->misc1 &= ~(DAS16CS_MISC1_INTE | DAS16CS_MISC1_INT_SRC_MASK |
			      DAS16CS_MISC1_AI_CONV_MASK);
	if (aref == AREF_DIFF)
		devpriv->misc1 &= ~DAS16CS_MISC1_SEDIFF;
	else
		devpriv->misc1 |= DAS16CS_MISC1_SEDIFF;
	outw(devpriv->misc1, dev->iobase + DAS16CS_MISC1_REG);

	devpriv->misc2 &= ~(DAS16CS_MISC2_BME | DAS16CS_MISC2_AI_GAIN_MASK);
	switch (range) {
	case 0:
		devpriv->misc2 |= DAS16CS_MISC2_AI_GAIN_1;
		break;
	case 1:
		devpriv->misc2 |= DAS16CS_MISC2_AI_GAIN_2;
		break;
	case 2:
		devpriv->misc2 |= DAS16CS_MISC2_AI_GAIN_4;
		break;
	case 3:
		devpriv->misc2 |= DAS16CS_MISC2_AI_GAIN_8;
		break;
	}
	outw(devpriv->misc2, dev->iobase + DAS16CS_MISC2_REG);

	for (i = 0; i < insn->n; i++) {
		outw(0, dev->iobase + DAS16CS_AI_DATA_REG);

		ret = comedi_timeout(dev, s, insn, das16cs_ai_eoc, 0);
		if (ret)
			return ret;

		data[i] = inw(dev->iobase + DAS16CS_AI_DATA_REG);
	}

	return i;
}

static int das16cs_ao_insn_write(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct das16cs_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val = s->readback[chan];
	unsigned short misc1;
	int bit;
	int i;

	for (i = 0; i < insn->n; i++) {
		val = data[i];

		outw(devpriv->misc1, dev->iobase + DAS16CS_MISC1_REG);
		udelay(1);

		/* raise the DACxCS line for the non-selected channel */
		misc1 = devpriv->misc1 & ~DAS16CS_MISC1_DAC_MASK;
		if (chan)
			misc1 |= DAS16CS_MISC1_DAC0CS;
		else
			misc1 |= DAS16CS_MISC1_DAC1CS;

		outw(misc1, dev->iobase + DAS16CS_MISC1_REG);
		udelay(1);

		for (bit = 15; bit >= 0; bit--) {
			if ((val >> bit) & 0x1)
				misc1 |= DAS16CS_MISC1_DACSD;
			else
				misc1 &= ~DAS16CS_MISC1_DACSD;
			outw(misc1, dev->iobase + DAS16CS_MISC1_REG);
			udelay(1);
			outw(misc1 | DAS16CS_MISC1_DACCLK,
			     dev->iobase + DAS16CS_MISC1_REG);
			udelay(1);
		}
		/*
		 * Make both DAC0CS and DAC1CS high to load
		 * the new data and update analog the output
		 */
		outw(misc1 | DAS16CS_MISC1_DAC0CS | DAS16CS_MISC1_DAC1CS,
		     dev->iobase + DAS16CS_MISC1_REG);
	}
	s->readback[chan] = val;

	return insn->n;
}

static int das16cs_dio_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		outw(s->state, dev->iobase + DAS16CS_DIO_REG);

	data[1] = inw(dev->iobase + DAS16CS_DIO_REG);

	return insn->n;
}

static int das16cs_dio_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	struct das16cs_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int mask;
	int ret;

	if (chan < 4)
		mask = 0x0f;
	else
		mask = 0xf0;

	ret = comedi_dio_insn_config(dev, s, insn, data, mask);
	if (ret)
		return ret;

	if (s->io_bits & 0xf0)
		devpriv->misc2 |= DAS16CS_MISC2_UDIR;
	else
		devpriv->misc2 &= ~DAS16CS_MISC2_UDIR;
	if (s->io_bits & 0x0f)
		devpriv->misc2 |= DAS16CS_MISC2_LDIR;
	else
		devpriv->misc2 &= ~DAS16CS_MISC2_LDIR;
	outw(devpriv->misc2, dev->iobase + DAS16CS_MISC2_REG);

	return insn->n;
}

static int das16cs_counter_insn_config(struct comedi_device *dev,
				       struct comedi_subdevice *s,
				       struct comedi_insn *insn,
				       unsigned int *data)
{
	struct das16cs_private *devpriv = dev->private;

	switch (data[0]) {
	case INSN_CONFIG_SET_CLOCK_SRC:
		switch (data[1]) {
		case 0:	/* internal 100 kHz */
			devpriv->misc2 |= DAS16CS_MISC2_CTR1;
			break;
		case 1:	/* external */
			devpriv->misc2 &= ~DAS16CS_MISC2_CTR1;
			break;
		default:
			return -EINVAL;
		}
		outw(devpriv->misc2, dev->iobase + DAS16CS_MISC2_REG);
		break;
	case INSN_CONFIG_GET_CLOCK_SRC:
		if (devpriv->misc2 & DAS16CS_MISC2_CTR1) {
			data[1] = 0;
			data[2] = I8254_OSC_BASE_100KHZ;
		} else {
			data[1] = 1;
			data[2] = 0;	/* unknown */
		}
		break;
	default:
		return -EINVAL;
	}

	return insn->n;
}

static const void *das16cs_find_boardinfo(struct comedi_device *dev,
					  struct pcmcia_device *link)
{
	const struct das16cs_board *board;
	int i;

	for (i = 0; i < ARRAY_SIZE(das16cs_boards); i++) {
		board = &das16cs_boards[i];
		if (board->device_id == link->card_id)
			return board;
	}

	return NULL;
}

static int das16cs_auto_attach(struct comedi_device *dev,
			       unsigned long context)
{
	struct pcmcia_device *link = comedi_to_pcmcia_dev(dev);
	const struct das16cs_board *board;
	struct das16cs_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	board = das16cs_find_boardinfo(dev, link);
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	link->config_flags |= CONF_AUTO_SET_IO | CONF_ENABLE_IRQ;
	ret = comedi_pcmcia_enable(dev, NULL);
	if (ret)
		return ret;
	dev->iobase = link->resource[0]->start;

	link->priv = dev;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	dev->pacer = comedi_8254_init(dev->iobase + DAS16CS_TIMER_BASE,
				      I8254_OSC_BASE_10MHZ, I8254_IO16, 0);
	if (!dev->pacer)
		return -ENOMEM;

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	/* Analog Input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND | SDF_DIFF;
	s->n_chan	= 16;
	s->maxdata	= 0xffff;
	s->range_table	= &das16cs_ai_range;
	s->insn_read	= das16cs_ai_insn_read;

	/* Analog Output subdevice */
	s = &dev->subdevices[1];
	if (board->has_ao) {
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_WRITABLE;
		s->n_chan	= 2;
		s->maxdata	= 0xffff;
		s->range_table	= &range_bipolar10;
		s->insn_write	= &das16cs_ao_insn_write;

		ret = comedi_alloc_subdev_readback(s);
		if (ret)
			return ret;
	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	/* Digital I/O subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= board->has_4dio ? 4 : 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= das16cs_dio_insn_bits;
	s->insn_config	= das16cs_dio_insn_config;

	/* Counter subdevice (8254) */
	s = &dev->subdevices[3];
	comedi_8254_subdevice_init(s, dev->pacer);

	dev->pacer->insn_config = das16cs_counter_insn_config;

	/* counters 1 and 2 are used internally for the pacer */
	comedi_8254_set_busy(dev->pacer, 1, true);
	comedi_8254_set_busy(dev->pacer, 2, true);

	return 0;
}

static struct comedi_driver driver_das16cs = {
	.driver_name	= "cb_das16_cs",
	.module		= THIS_MODULE,
	.auto_attach	= das16cs_auto_attach,
	.detach		= comedi_pcmcia_disable,
};

static int das16cs_pcmcia_attach(struct pcmcia_device *link)
{
	return comedi_pcmcia_auto_config(link, &driver_das16cs);
}

static const struct pcmcia_device_id das16cs_id_table[] = {
	PCMCIA_DEVICE_MANF_CARD(0x01c5, 0x0039),
	PCMCIA_DEVICE_MANF_CARD(0x01c5, 0x4009),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, das16cs_id_table);

static struct pcmcia_driver das16cs_driver = {
	.name		= "cb_das16_cs",
	.owner		= THIS_MODULE,
	.id_table	= das16cs_id_table,
	.probe		= das16cs_pcmcia_attach,
	.remove		= comedi_pcmcia_auto_unconfig,
};
module_comedi_pcmcia_driver(driver_das16cs, das16cs_driver);

MODULE_AUTHOR("David A. Schleef <ds@schleef.org>");
MODULE_DESCRIPTION("Comedi driver for Computer Boards PC-CARD DAS16/16");
MODULE_LICENSE("GPL");
